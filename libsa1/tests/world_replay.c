/*
 * world_replay.c - trace replay helpers shared by the world tests (transplant_test.c,
 * world_boot_test.c); included by them, not compiled on its own.
 *
 * A trace (SADC/harness/src/sa1_trace.h, version 2) is the reference behaviour of the original game,
 * one record per 60 Hz logic step. Replay (sadc docs/libsa1_integration.md 6.6, 9.1 V4-V6): for each
 * record, the world gets the record's input (raw stick / buttons for the real pad update, then the
 * record's on / press / analog over the computed ones), its sub step, and at the first entry of
 * player 0's exec the record's player-entry state (rand, XMTRX, camera taskwk); one step runs; the
 * player state is read back into a record of the same format (the harness's fill_record) and compared
 * like `sa1trace.py diff --ulps 0 [--raw [--heap-by-structure]]`.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_trace.h"
#include "sa1_trace_ext.h"
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_hostobj.h"

/* The page-guard allocator (tests/guard_alloc.h): with SA1_GUARD_ALLOC=1 every test that includes this file puts
 * libsa1's heap blocks against no-access pages. sa1_host_defaults installs it (a test's own counting callbacks call
 * ga_malloc / ga_free instead). */
#include "guard_alloc.c"
#define sa1_host_defaults(h) (sa1_host_defaults(h), ga_host(h))

/* ---- files ------------------------------------------------------------------------------------------- */
static unsigned char *wr_read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long sz;
    *n = 0;
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (unsigned char *)malloc(sz > 0 ? (size_t)sz : 1u);
    if (b != NULL && sz > 0 && fread(b, 1, (size_t)sz, f) != (size_t)sz) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *n = sz > 0 ? (size_t)sz : 0u;
    return b;
}

static int wr_write_file(const char *path, const void *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    if (n != 0 && fwrite(p, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    return fclose(f);
}

/* ---- traces ------------------------------------------------------------------------------------------ */
typedef struct wr_trace {
    sa1_trace_header h;
    sa1_trace_rec *r;
    uint32_t n;
} wr_trace;

/* round 5: the host-object sidecar of a reference (below) */
static void wr_obj_load(const char *path, const wr_trace *t);
static void wr_obj_unload(const wr_trace *t);
static int wr_arena(uint32_t g);
static uint32_t wr_g32(const sa1g_world *w, uint32_t g);

static int wr_trace_load(const char *path, wr_trace *t)
{
    size_t n = 0;
    unsigned char *b = wr_read_file(path, &n);
    memset(t, 0, sizeof *t);
    if (b == NULL || n < sizeof t->h) {
        printf("    cannot read trace %s\n", path);
        free(b);
        return -1;
    }
    memcpy(&t->h, b, sizeof t->h);
    if (memcmp(t->h.magic, SA1_TRACE_MAGIC, 8) != 0 || t->h.version != 2 || t->h.record_size != sizeof(sa1_trace_rec)
        || t->h.header_size != sizeof(sa1_trace_header)
        || n < sizeof t->h + (size_t)t->h.count * sizeof(sa1_trace_rec)) {
        printf("    %s: not a version-2 SA1 trace\n", path);
        free(b);
        return -1;
    }
    t->n = t->h.count;
    t->r = (sa1_trace_rec *)malloc((size_t)t->n * sizeof(sa1_trace_rec) + 1u);
    if (t->r == NULL) {
        free(b);
        return -1;
    }
    memcpy(t->r, b + sizeof t->h, (size_t)t->n * sizeof(sa1_trace_rec));
    free(b);
    wr_obj_load(path, t);       /* round 5: the host-object sidecar NAME.obj.bin, when the reference has one */
    return 0;
}

static void wr_trace_free(wr_trace *t)
{
    wr_obj_unload(t);
    free(t->r);
    memset(t, 0, sizeof *t);
}

/* ---- host objects (round 5): the reference's NAME.obj.bin sidecar ------------------------------------------------
 * A reference recorded with `sa1h trace --hostobj FILE` (sadc harness/src/ho_oracle.c: libsa1's own host-object layer on
 * the original game code) or `--create-task` has a sidecar (sadc harness/src/sa1_trace_ext.h, sa1_trace_obj_*): the host
 * calls made before each step (in the host space of libsa1's default coordinates), the game tasks made after
 * LoadPlayerTask, and what each step gave (host-object events, the layer's world events HURT / DEATH / CAPACITY, the held
 * state, the proxies). wr_trace_load reads it; wr_screen_of (called right after a character is created for a replay)
 * starts a run on that world: a fresh host-object registry of the world's context (the reference's object ids), the
 * sidecar's tasks made in the world; wr_inject makes the record's host calls before each step through the public API;
 * wr_record builds the world's sidecar record after the step and compares it with the reference's (first differences
 * printed); wr_trace_write writes the run's sidecar next to the world's trace, which `sa1trace.py diff` compares
 * (sadc gsa1_rounds_plan.md 9.4; libsa1_interaction.md 4.3). Traces without a sidecar replay exactly as before. */
#define WR_OBJ_MAX 16u
#define WR_OBJ_H   64u      /* script handles 1..63 */

typedef struct wr_obj {
    const void *key;                /* the wr_trace (address only) */
    const sa1_trace_header *hdr;    /* &t->h: the trace in wr_screen_of */
    const sa1_trace_rec *recs;      /* t->r, t->n: its records in wr_inject / wr_record */
    uint32_t nrecs;
    sa1_trace_obj_header h;
    sa1_trace_obj_task_ext ext[SA1_TOBJ_MAX_TASKS];     /* round 0.9.2: version 2's task fields (version 1: zero) */
    uint32_t hsize;                 /* the file's header size: sizeof h (version 1) or sizeof(sa1_trace_obj_header_v2) */
    sa1_trace_obj_rec *r;
    sa1_trace_obj_rec_ext *rx;      /* round 0.9.2: version 2's per-record OBJ_CONDITION words (NULL: version 1) */
    uint32_t n;
} wr_obj;
static wr_obj wr_objs[WR_OBJ_MAX];

static struct {
    const wr_obj *o;                /* the run in progress (NULL: none) */
    sa1g_world *w;
    sa1_context *ctx;
    sa1_character_id id;
    uint32_t idm[WR_OBJ_H], last[WR_OBJ_H], kind[WR_OBJ_H];
    uint32_t task_tp[SA1_TOBJ_MAX_TASKS];   /* the sidecar's tasks made in the world (reported as handle 0x80 + k) */
    uint8_t task_seen[SA1_TOBJ_MAX_TASKS], task_gone[SA1_TOBJ_MAX_TASKS];   /* round 0.9.2: reported live / ended */
    sa1_trace_obj_rec *out;         /* this run's records */
    sa1_trace_obj_rec_ext *outx;    /* round 0.9.2: their version-2 part (a version-2 sidecar) */
    uint32_t nout, cap;
    uint32_t bad;                   /* records that differ from the reference's */
    int first_bad;
} wr_run;

static void wr_obj_load(const char *path, const wr_trace *t)
{
    char p[1100];
    size_t n = 0, L = strlen(path), rsz;
    unsigned char *b;
    wr_obj *o = NULL;
    uint32_t k;
    int v2;
    if (L > 4 && !strcmp(path + L - 4, ".bin")) {
        L -= 4;
    }
    snprintf(p, sizeof p, "%.*s.obj.bin", (int)L, path);
    b = wr_read_file(p, &n);
    if (b == NULL) {
        return;
    }
    for (k = 0; k < WR_OBJ_MAX && o == NULL; k++) {
        if (wr_objs[k].key == NULL) {
            o = &wr_objs[k];
        }
    }
    if (o == NULL || n < sizeof o->h) {
        free(b);
        return;
    }
    memcpy(&o->h, b, sizeof o->h);
    memset(o->ext, 0, sizeof o->ext);
    /* version 1 (280-byte header) or, round 0.9.2, version 2: the v1 header, then the tasks' scale / OBJ_CONDITION
       (sadc harness/src/sa1_trace_ext.h sa1_trace_obj_header_v2; `trace --create-task .../SX,SY,SZ+ocp`) */
    v2 = o->h.x.version == SA1_TRACE_EXT_OBJ_VERSION2;
    o->hsize = v2 ? (uint32_t)sizeof(sa1_trace_obj_header_v2) : (uint32_t)sizeof o->h;
    rsz = v2 ? sizeof(sa1_trace_obj_rec_v2) : sizeof(sa1_trace_obj_rec);
    if (memcmp(o->h.x.magic, SA1_TRACE_EXT_MAGIC_OBJ, 8) != 0
        || (o->h.x.version != SA1_TRACE_EXT_OBJ_VERSION && !v2)
        || o->h.x.header_size != o->hsize || o->h.x.record_size != rsz
        || n < o->hsize + (size_t)o->h.x.count * rsz) {
        printf("    %s: not a version-%d / %d host-object sidecar (ignored)\n", p, SA1_TRACE_EXT_OBJ_VERSION,
               SA1_TRACE_EXT_OBJ_VERSION2);
        free(b);
        return;
    }
    if (v2) {
        memcpy(o->ext, b + offsetof(sa1_trace_obj_header_v2, task_ext), sizeof o->ext);
    }
    o->n = o->h.x.count;
    o->r = (sa1_trace_obj_rec *)malloc((size_t)o->n * sizeof *o->r + 1u);
    o->rx = v2 ? (sa1_trace_obj_rec_ext *)calloc((size_t)o->n + 1u, sizeof *o->rx) : NULL;
    if (o->r == NULL || (v2 && o->rx == NULL)) {
        free(o->r);
        free(o->rx);
        o->r = NULL;
        o->rx = NULL;
        free(b);
        return;
    }
    for (k = 0; k < o->n; k++) {    /* version 2: each record is the v1 record + sa1_trace_obj_rec_ext */
        memcpy(&o->r[k], b + o->hsize + (size_t)k * rsz, sizeof *o->r);
        if (v2) {
            memcpy(&o->rx[k], b + o->hsize + (size_t)k * rsz + sizeof *o->r, sizeof *o->rx);
        }
    }
    free(b);
    o->key = t;
    o->hdr = &t->h;
    o->recs = t->r;
    o->nrecs = t->n;
    printf("    %s: host-object sidecar (%u records, %u task(s)%s)\n", p, (unsigned)o->n, (unsigned)o->h.ntasks,
           (o->h.x.flags & SA1_TOBJ_H_HOSTOBJ) ? ", host objects" : "");
}

static void wr_obj_unload(const wr_trace *t)
{
    uint32_t k;
    for (k = 0; k < WR_OBJ_MAX; k++) {
        if (wr_objs[k].key == (const void *)t) {
            if (wr_run.o == &wr_objs[k]) {
                wr_run.o = NULL;
            }
            free(wr_objs[k].r);
            free(wr_objs[k].rx);
            memset(&wr_objs[k], 0, sizeof wr_objs[k]);
        }
    }
}

/* a task of the sidecar header, made in the world as `trace --create-task` made it after LoadPlayerTask */
static void wr_obj_task_cb(sa1g_world *w, void *user)
{
    uint32_t idx = (uint32_t)(uintptr_t)user;
    const sa1_trace_obj_task *t = &wr_run.o->h.task[idx];
    void *tp = ((void *(*)(uint16_t, int32_t, sa1_gfn))sa1_fn(SA1_GFN_C(0x8C09852Eu), 0u))(
        (uint16_t)t->im, (int32_t)t->list, SA1_GFN_C(t->exec));
    const sa1_trace_obj_task_ext *x = &wr_run.o->ext[idx];
    uint32_t twp = 0, k, v;
    wr_run.task_tp[idx] = tp != NULL ? sa1_encode(tp) : 0u;
    if (tp == NULL || sa1g_world_rd32(w, sa1_encode(tp) + 0x20u, &twp) != 0) {
        twp = 0;
    }
    if (tp != NULL && t->has_pos && twp != 0) {
        for (k = 0; k < 3; k++) {
            memcpy(&v, &t->pos[k], 4);
            sa1g_world_wr32(w, twp + 0x20u + 4u * k, v);
        }
        sa1g_world_wr32(w, twp + 0x18u, t->angy);
    }
    /* round 0.9.2 (sidecar version 2, sadc harness/src/t_sim.c lpt_hook): the task's scale, and an OBJ_CONDITION in the
       harness's slot of B_8C79A3E0 as the SET spawner 8C091380 writes it: scCount 1, scUserFlag 0, ssCondition 0x8001,
       pTask, pObjEditEntry 0, unionStatus.f = the range; tp->ocp = the slot. Round 0.9.4 (sadc gsa1_094_plan.md 6.4,
       Review R.7): `+ocp=HEX` (SA1_TOBJ_T_OCPW) gives another initial ssCondition, e.g. 0x8005 = a ground item box
       already broken (it spawns as the shell) */
    if (tp != NULL && (x->flags & SA1_TOBJ_T_SCL) && twp != 0) {
        for (k = 0; k < 3; k++) {
            memcpy(&v, &x->scl[k], 4);
            sa1g_world_wr32(w, twp + 0x2Cu + 4u * k, v);
        }
    }
    if (tp != NULL && (x->flags & SA1_TOBJ_T_OCP) && x->ocp_slot < SA1_TOBJ_OCP_SLOTS) {
        uint32_t c = SA1_TOBJ_OCP_ARRAY + 16u * x->ocp_slot;
#ifdef SA1_TOBJ_OCP_WORD
        /* scCount 1, scUserFlag 0, ssCondition 0x8001 or the +ocp=HEX word (little-endian) */
        sa1g_world_wr32(w, c + 0u, ((uint32_t)SA1_TOBJ_OCP_WORD(x) << 16) | 0x0001u);
#else
        sa1g_world_wr32(w, c + 0u, 0x80010001u);    /* scCount 1, scUserFlag 0, ssCondition 0x8001 (little-endian) */
#endif
        sa1g_world_wr32(w, c + 4u, sa1_encode(tp));
        sa1g_world_wr32(w, c + 8u, 0u);
        memcpy(&v, &x->ocp_range, 4);
        sa1g_world_wr32(w, c + 12u, v);
        sa1g_world_wr32(w, sa1_encode(tp) + 0x1Cu, c);
    }
}

/* round 0.9.1: the sidecar task of Amy's bird (`trace --create-task 8C0DECDA:2:3`) is made by sa1_character_set_companion;
 * this finds it (list 3, exec 8C0DECDA before its first step, 8C0DE964 after) for the sidecar's handle 0x80 + k */
#define WR_BIRD_INIT 0x0C0DECDAu    /* & 0x1FFFFFFF */
#define WR_BIRD_EXEC 0x0C0DE964u
static void wr_obj_bird_cb(sa1g_world *w, void *user)
{
    uint32_t idx = (uint32_t)(uintptr_t)user, tp = 0, guard = 0, ex;
    sa1g_world_rd32(w, SA1G_ADDR_TASK_LISTS + 4u * 3u, &tp);
    wr_run.task_tp[idx] = 0;
    while (tp != 0 && guard++ < 0x1000u) {
        ex = 0;
        sa1g_world_rd32(w, tp + 0x10u, &ex);
        if ((ex & 0x1FFFFFFFu) == WR_BIRD_INIT || (ex & 0x1FFFFFFFu) == WR_BIRD_EXEC) {
            wr_run.task_tp[idx] = tp;
            return;
        }
        if (sa1g_world_rd32(w, tp, &tp) != 0) {
            return;
        }
    }
}

/* wr_screen_of: a replay of a trace with a sidecar starts on world w */
static void wr_obj_start(sa1g_world *w, const sa1_trace_header *h)
{
    const wr_obj *o = NULL;
    sa1_hostobj_config cfg;
    uint32_t k;
    for (k = 0; k < WR_OBJ_MAX && o == NULL; k++) {
        if (wr_objs[k].key != NULL && wr_objs[k].hdr == h) {
            o = &wr_objs[k];
        }
    }
    if (o == NULL || w == NULL || w->hostobj_ctx == NULL) {
        wr_run.o = NULL;        /* a run without a sidecar ends the last one */
        return;
    }
    wr_run.o = o;
    wr_run.w = w;
    wr_run.ctx = w->hostobj_ctx;
    wr_run.id = 0;
    for (k = 0; k < SA1_MAX_INSTANCES; k++) {
        sa1_character_id id = (sa1_character_id)(((uint32_t)wr_run.ctx->inst_gen[k] << 16) | (k + 1u));
        if (wr_run.ctx->inst[k] != NULL && sa1_character_world(wr_run.ctx, id) == w) {
            wr_run.id = id;
        }
    }
    memset(wr_run.idm, 0, sizeof wr_run.idm);
    memset(wr_run.last, 0, sizeof wr_run.last);
    memset(wr_run.kind, 0, sizeof wr_run.kind);
    memset(wr_run.task_tp, 0, sizeof wr_run.task_tp);
    memset(wr_run.task_seen, 0, sizeof wr_run.task_seen);
    memset(wr_run.task_gone, 0, sizeof wr_run.task_gone);
    wr_run.nout = 0;
    wr_run.bad = 0;
    wr_run.first_bad = -1;
    if (wr_run.cap < o->nrecs) {
        free(wr_run.out);
        free(wr_run.outx);
        wr_run.cap = o->nrecs;
        wr_run.out = (sa1_trace_obj_rec *)calloc(wr_run.cap + 1u, sizeof *wr_run.out);
        wr_run.outx = (sa1_trace_obj_rec_ext *)calloc(wr_run.cap + 1u, sizeof *wr_run.outx);
        if (wr_run.out == NULL || wr_run.outx == NULL) {
            free(wr_run.out);
            free(wr_run.outx);
            wr_run.out = NULL;
            wr_run.outx = NULL;
            wr_run.cap = 0;
        }
    }
    if (wr_run.ctx->coords.scale != o->h.coords_scale
        || memcmp(wr_run.ctx->coords.axis, o->h.coords_axis, sizeof o->h.coords_axis) != 0) {
        printf("    host-object sidecar: the context's coordinates are not the sidecar's (scale %g): the calls are "
               "replayed as they are\n", (double)o->h.coords_scale);
    }
    /* a fresh registry: the reference's object ids (generation 1, entries from 0), outside the steps */
    sa1ho_context_free(wr_run.ctx);
    memset(&cfg, 0, sizeof cfg);
    cfg.struct_size = sizeof cfg;
    sa1_hostobj_configure(wr_run.ctx, &cfg);
    for (k = 0; k < o->h.ntasks && k < SA1_TOBJ_MAX_TASKS; k++) {
        const sa1_trace_obj_task *t = &o->h.task[k];
        if ((t->exec & 0x1FFFFFFFu) == WR_BIRD_INIT && t->im == 2u && t->list == 3u && !t->has_pos && o->ext[k].flags == 0) {
            /* round 0.9.1 (sadc docs/research/gsa1_070_bird.md F3 with its Review R.2.7 / R.4): Amy's bird through the
               public API, outside any world call (the tick lock is not recursive); its task found by a world call */
            sa1_status st = sa1_character_set_companion(wr_run.ctx, wr_run.id, SA1_COMPANION_BIRD, 1);
            if (st != SA1_OK) {
                printf("    host-object sidecar: sa1_character_set_companion(BIRD, 1): %s: %s\n", sa1_status_string(st),
                       sa1_last_error(wr_run.ctx));
            }
            sa1g_world_call(w, wr_obj_bird_cb, (void *)(uintptr_t)k);
            continue;
        }
        sa1g_world_call(w, wr_obj_task_cb, (void *)(uintptr_t)k);
    }
}

/* the record index of r in the run's trace, or -1 */
static int32_t wr_obj_index(const sa1g_world *w, const sa1_trace_rec *r)
{
    const wr_obj *o = wr_run.o;
    if (o == NULL || w != wr_run.w || r < o->recs || r >= o->recs + o->nrecs || (uint32_t)(r - o->recs) >= o->n) {
        return -1;
    }
    return (int32_t)(r - o->recs);
}

/* wr_inject: the record's host calls (before its step) */
static void wr_obj_feed(sa1g_world *w, const sa1_trace_rec *r)
{
    int32_t i = wr_obj_index(w, r);
    uint32_t k;
    if (i < 0) {
        return;
    }
    for (k = 0; k < wr_run.o->r[i].nops && k < SA1_TOBJ_MAX_OPS; k++) {
        const sa1_trace_obj_op *o = &wr_run.o->r[i].op[k];
        uint32_t h = o->handle < WR_OBJ_H ? o->handle : 0u, j;
        sa1_status st = SA1_OK;
        switch (o->op) {
        case SA1_TOBJ_OP_ADD:
        case SA1_TOBJ_OP_UPDATE: {
            sa1_hostobj_desc d;
            memset(&d, 0, sizeof d);
            d.struct_size = sizeof d;
            d.kind = o->kind;
            d.flags = o->flags;
            memcpy(d.position, o->pos, sizeof d.position);
            d.yaw = o->yaw;
            d.armor = o->armor;
            d.touch = o->touch;
            d.cooldown_steps = o->cooldown;
            d.user = o->handle;
            d.num_shapes = o->nshapes;
            for (j = 0; j < o->nshapes && j < SA1_HOSTOBJ_MAX_SHAPES; j++) {
                d.shapes[j].form = o->shape[j].form;
                d.shapes[j].flags = o->shape[j].flags;
                memcpy(d.shapes[j].center, o->shape[j].center, sizeof d.shapes[j].center);
                memcpy(d.shapes[j].size, o->shape[j].size, sizeof d.shapes[j].size);
                d.shapes[j].yaw = o->shape[j].yaw;
            }
            d.launch_speed = o->launch_speed;
            d.grab_reach = o->grab_reach;
            if (o->op == SA1_TOBJ_OP_ADD) {
                uint32_t id = 0;
                st = sa1_hostobj_add(wr_run.ctx, &d, &id);
                wr_run.idm[h] = wr_run.last[h] = id;
            } else {
                st = sa1_hostobj_update(wr_run.ctx, wr_run.idm[h], &d);
            }
            wr_run.kind[h] = o->kind;
            break;
        }
        case SA1_TOBJ_OP_MOVE:
            st = sa1_hostobj_move(wr_run.ctx, wr_run.idm[h], o->pos, o->yaw);
            break;
        case SA1_TOBJ_OP_REMOVE:
            st = sa1_hostobj_remove(wr_run.ctx, wr_run.idm[h], (uint32_t)o->arg[0]);
            wr_run.idm[h] = 0;
            break;
        case SA1_TOBJ_OP_RELEASE:
            st = sa1_hostobj_release(wr_run.ctx, wr_run.idm[h]);
            break;
        case SA1_TOBJ_OP_DAMAGE: {
            sa1_damage dm;
            memset(&dm, 0, sizeof dm);
            dm.struct_size = sizeof dm;
            dm.flags = (uint32_t)o->arg[0];
            memcpy(dm.source, o->src, sizeof dm.source);
            dm.object = (uint32_t)o->arg[1] < WR_OBJ_H ? wr_run.last[o->arg[1]] : 0u;
            st = sa1_character_damage(wr_run.ctx, wr_run.id, &dm);
            break;
        }
        case SA1_TOBJ_OP_OPTION:
            st = sa1_character_set_option(wr_run.ctx, wr_run.id, (uint32_t)o->arg[0], o->arg[1]);
            break;
        case SA1_TOBJ_OP_RINGS:
            st = sa1_character_add_rings(wr_run.ctx, wr_run.id, o->arg[0]);
            break;
        case SA1_TOBJ_OP_MASK:
            st = sa1_hostobj_world_mask(wr_run.ctx, wr_run.id, (uint32_t)o->arg[0]);
            break;
        case SA1_TOBJ_OP_CONFIG: {
            sa1_hostobj_config c;
            memset(&c, 0, sizeof c);
            c.struct_size = sizeof c;
            c.window = (uint32_t)o->arg[0];
            c.enter_radius = o->src[0];
            c.leave_radius = o->src[1];
            st = sa1_hostobj_configure(wr_run.ctx, &c);
            break;
        }
        default:
            break;
        }
        if (st != SA1_OK) {
            printf("    host-object sidecar: step %d: call %u (object %u): %s\n", (int)i, (unsigned)o->op, (unsigned)h,
                   sa1_status_string(st));
        }
    }
}

static uint32_t wr_obj_handle(uint32_t id)
{
    uint32_t h;
    for (h = 1; id != 0 && h < WR_OBJ_H; h++) {
        if (wr_run.last[h] == id) {
            return h;
        }
    }
    return 0;
}

/* wr_record: the world's sidecar record of the step just run, compared with the reference's */
static void wr_obj_collect(const sa1g_world *cw, const sa1_trace_rec *in)
{
    sa1g_world *w = (sa1g_world *)(uintptr_t)cw;
    int32_t i = wr_obj_index(cw, in);
    sa1_trace_obj_rec m, a, b;
    sa1_trace_obj_rec_ext mx;
    sa1_hostobj_event ev[16];
    sa1_event we[16];
    sa1_hostobj_held held;
    sa1_hostobj_info info;
    uint32_t n, k, h;
    if (i < 0 || wr_run.out == NULL) {
        return;
    }
    memset(&m, 0, sizeof m);
    m.step = wr_run.o->r[i].step;
    m.nops = wr_run.o->r[i].nops;
    memcpy(m.op, wr_run.o->r[i].op, sizeof m.op);
    do {
        n = 0;
        sa1_poll_hostobj_events(wr_run.ctx, ev, 16, &n);
        for (k = 0; k < n; k++) {
            sa1_trace_obj_ev *e;
            if (m.nevents >= SA1_TOBJ_MAX_EVENTS) {
                m.events_lost++;
                continue;
            }
            e = &m.ev[m.nevents++];
            e->type = ev[k].type;
            e->handle = wr_obj_handle(ev[k].object);
            e->attack = ev[k].attack;
            e->level = ev[k].level;
            e->damage = ev[k].damage;
            e->shape = ev[k].shape;
            e->flags = ev[k].flags;
            e->reason = ev[k].reason;
            e->target = wr_obj_handle(ev[k].target);
            memcpy(e->pos, ev[k].position, sizeof e->pos);
            memcpy(e->dir, ev[k].direction, sizeof e->dir);
            e->speed = ev[k].speed;
        }
    } while (n == 16);
    do {
        n = sa1g_world_poll_events(w, we, 16);  /* the world's own events too: the queue never fills up in a replay */
        for (k = 0; k < n; k++) {
            sa1_trace_obj_ev *e;
            if (we[k].type != SA1_EVENT_HURT && we[k].type != SA1_EVENT_DEATH
                && !(we[k].type == SA1_EVENT_CAPACITY && we[k].id == SA1_CAPACITY_HOST_OBJECTS)) {
                continue;
            }
            if (m.nevents >= SA1_TOBJ_MAX_EVENTS) {
                m.events_lost++;
                continue;
            }
            e = &m.ev[m.nevents++];
            e->type = SA1_TOBJ_WORLD | we[k].type;
            e->handle = wr_obj_handle(we[k].id);
            e->arg = we[k].arg;
            e->bank = we[k].bank;
            memcpy(e->pos, we[k].position, sizeof e->pos);
        }
    } while (n == 16);
    memset(&held, 0, sizeof held);
    held.struct_size = sizeof held;
    sa1_character_held(wr_run.ctx, wr_run.id, &held);
    m.held_object = wr_obj_handle(held.object);
    m.held_phase = held.phase;
    memcpy(m.held_pos, held.position, sizeof m.held_pos);
    memcpy(m.held_hand, held.hand, sizeof m.held_hand);
    m.held_yaw = held.yaw;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_hostobj_info_get(wr_run.ctx, wr_run.id, &info);
    m.proxies = info.proxies;
    m.created = info.created;
    m.destroyed = info.destroyed;
    m.refused = info.refused;
    for (h = 1; h < WR_OBJ_H && m.nobjs < SA1_TOBJ_MAX_OBJS; h++) {
        sa1_trace_obj_st *s;
        uint32_t twp, cwp = 0, tp = 0, ewp = 0, v = 0;
        if (wr_run.idm[h] == 0) {
            continue;
        }
        s = &m.obj[m.nobjs++];
        s->handle = h;
        twp = sa1ho_world_proxy_twp(w, wr_run.idm[h]);
        if (!wr_arena(twp)) {
            continue;
        }
        s->twp = twp;
        s->mode = (uint16_t)(wr_g32(w, twp) & 0xFFu);
        s->tflag = (uint16_t)((wr_g32(w, twp + 4u)) & 0xFFFFu);
        s->angy = wr_g32(w, twp + 0x18u) & 0xFFFFu;
        for (k = 0; k < 3; k++) {
            v = wr_g32(w, twp + 0x20u + 4u * k);
            memcpy(&s->pos[k], &v, 4);
        }
        cwp = wr_g32(w, twp + 0x38u);
        if (wr_arena(cwp)) {
            s->cflag = (uint16_t)((wr_g32(w, cwp) >> 16) & 0xFFFFu);
            tp = wr_g32(w, cwp + 0x10u);
        }
        if (wr_run.kind[h] != SA1_HOSTOBJ_HOLDABLE && wr_arena(tp)) {
            ewp = wr_g32(w, tp + 0x24u);
            if (wr_arena(ewp)) {
                s->eflag = (uint16_t)((wr_g32(w, ewp + 0x4Cu) >> 16) & 0xFFFFu);
            }
        }
    }
    /* the sidecar's tasks (handle 0x80 + k), after the proxies: a live task (its taskwk unchanged, not DestroyTask).
       Round 0.9.2 (as sadc harness/src/ho_oracle.c): a task that has ended (seen live, then not) stays gone, so a later
       task in its freed block is not reported as it */
    for (k = 0; k < wr_run.o->h.ntasks && k < SA1_TOBJ_MAX_TASKS && m.nobjs < SA1_TOBJ_MAX_OBJS; k++) {
        sa1_trace_obj_st *s = &m.obj[m.nobjs++];
        uint32_t tp = wr_run.task_tp[k], twp, cwp, v = 0, j;
        s->handle = 0x80u + k;
        if (wr_run.task_gone[k]) {
            continue;
        }
        twp = wr_arena(tp) ? wr_g32(w, tp + 0x20u) : 0u;
        if (!wr_arena(tp) || !wr_arena(twp) || (wr_g32(w, tp + 0x10u) & 0x1FFFFFFFu) == 0x0C09865Cu) {
            if (wr_run.task_seen[k]) {
                wr_run.task_gone[k] = 1;
            }
            continue;
        }
        wr_run.task_seen[k] = 1;
        s->twp = twp;
        s->mode = (uint16_t)(wr_g32(w, twp) & 0xFFu);
        s->tflag = (uint16_t)(wr_g32(w, twp + 4u) & 0xFFFFu);
        s->angy = wr_g32(w, twp + 0x18u) & 0xFFFFu;
        for (j = 0; j < 3; j++) {
            v = wr_g32(w, twp + 0x20u + 4u * j);
            memcpy(&s->pos[j], &v, 4);
        }
        cwp = wr_g32(w, twp + 0x38u);
        if (wr_arena(cwp)) {
            s->cflag = (uint16_t)((wr_g32(w, cwp) >> 16) & 0xFFFFu);
        }
    }
    /* round 0.9.2 (version 2): the +ocp tasks' OBJ_CONDITION word 0 after the step (the game's "collected" signal) */
    memset(&mx, 0, sizeof mx);
    for (k = 0; wr_run.o->rx != NULL && k < wr_run.o->h.ntasks && k < SA1_TOBJ_MAX_TASKS; k++) {
        if ((wr_run.o->ext[k].flags & SA1_TOBJ_T_OCP) && wr_run.o->ext[k].ocp_slot < SA1_TOBJ_OCP_SLOTS) {
            mx.ocp_word[k] = wr_g32(w, SA1_TOBJ_OCP_ARRAY + 16u * wr_run.o->ext[k].ocp_slot);
        }
    }
    if ((uint32_t)i < wr_run.cap) {
        wr_run.out[i] = m;
        wr_run.outx[i] = mx;
        if ((uint32_t)i + 1u > wr_run.nout) {
            wr_run.nout = (uint32_t)i + 1u;
        }
    }
    /* the comparison: everything but the proxies' guest addresses */
    a = wr_run.o->r[i];
    b = m;
    for (k = 0; k < SA1_TOBJ_MAX_OBJS; k++) {
        a.obj[k].twp = b.obj[k].twp = 0;
    }
    if (memcmp(&a, &b, sizeof a) != 0
        || (wr_run.o->rx != NULL && memcmp(&wr_run.o->rx[i], &mx, sizeof mx) != 0)) {
        if (wr_run.bad++ == 0) {
            const char *what = "the proxies";
            wr_run.first_bad = i;
            if (wr_run.o->rx != NULL && memcmp(&wr_run.o->rx[i], &mx, sizeof mx) != 0 && memcmp(&a, &b, sizeof a) == 0) {
                what = "the OBJ_CONDITIONs";
                printf("    host-object sidecar: step %d: OBJ_CONDITION words %08X %08X / %08X %08X (reference / world)\n",
                       (int)i, (unsigned)wr_run.o->rx[i].ocp_word[0], (unsigned)wr_run.o->rx[i].ocp_word[1],
                       (unsigned)mx.ocp_word[0], (unsigned)mx.ocp_word[1]);
            }
            if (a.nevents != b.nevents || memcmp(a.ev, b.ev, sizeof a.ev) != 0 || a.events_lost != b.events_lost) {
                what = "the events";
            } else if (a.held_object != b.held_object || a.held_phase != b.held_phase
                       || memcmp(a.held_pos, b.held_pos, sizeof a.held_pos) != 0
                       || memcmp(a.held_hand, b.held_hand, sizeof a.held_hand) != 0 || a.held_yaw != b.held_yaw) {
                what = "the held state";
            } else if (a.proxies != b.proxies || a.created != b.created || a.destroyed != b.destroyed
                       || a.refused != b.refused) {
                what = "the proxy counts";
            }
            printf("    host-object sidecar: step %d: %s differ from the reference's (events %u / %u", (int)i, what,
                   (unsigned)a.nevents, (unsigned)b.nevents);
            if (a.nevents && b.nevents) {
                printf(", first: type %u / %u, object %u / %u, attack %u / %u, flags %X / %X", (unsigned)a.ev[0].type,
                       (unsigned)b.ev[0].type, (unsigned)a.ev[0].handle, (unsigned)b.ev[0].handle,
                       (unsigned)a.ev[0].attack, (unsigned)b.ev[0].attack, (unsigned)a.ev[0].flags,
                       (unsigned)b.ev[0].flags);
            }
            printf(")\n");
        }
    }
}

/* wr_trace_write: the run's sidecar next to the world's trace (PATH minus .bin + .obj.bin) */
static void wr_obj_write(const char *path, const sa1_trace_header *ref, uint32_t n)
{
    char p[1100];
    size_t L = strlen(path), sz, rsz;
    unsigned char *b;
    sa1_trace_obj_header h;
    uint32_t k;
    if (wr_run.o == NULL || wr_run.o->hdr != ref || wr_run.out == NULL) {
        return;
    }
    if (n > wr_run.nout) {
        n = wr_run.nout;
    }
    if (L > 4 && !strcmp(path + L - 4, ".bin")) {
        L -= 4;
    }
    snprintf(p, sizeof p, "%.*s.obj.bin", (int)L, path);
    h = wr_run.o->h;
    h.x.count = n;
    /* the reference's version (round 0.9.2: 1, or 2 with the tasks' fields and each record's OBJ_CONDITION words) */
    rsz = wr_run.o->rx != NULL ? sizeof(sa1_trace_obj_rec_v2) : sizeof(sa1_trace_obj_rec);
    sz = wr_run.o->hsize + (size_t)n * rsz;
    b = (unsigned char *)calloc(1, sz);
    if (b == NULL) {
        return;
    }
    memcpy(b, &h, sizeof h);
    if (wr_run.o->hsize == sizeof(sa1_trace_obj_header_v2)) {
        memcpy(b + offsetof(sa1_trace_obj_header_v2, task_ext), wr_run.o->ext, sizeof wr_run.o->ext);
    }
    for (k = 0; k < n; k++) {
        memcpy(b + wr_run.o->hsize + (size_t)k * rsz, &wr_run.out[k], sizeof *wr_run.out);
        if (wr_run.o->rx != NULL) {
            memcpy(b + wr_run.o->hsize + (size_t)k * rsz + sizeof *wr_run.out, &wr_run.outx[k], sizeof *wr_run.outx);
        }
    }
    if (wr_write_file(p, b, sz) == 0) {
        printf("    wrote %s (host-object sidecar: %u records, %u differ from the reference's%s)\n", p, (unsigned)n,
               (unsigned)wr_run.bad, wr_run.bad ? "" : "");
    }
    free(b);
}

/* a trace of the world's records (same header, flags bit 2: native code) for sa1trace.py */
static int wr_trace_write(const char *path, const sa1_trace_header *ref, const sa1_trace_rec *r, uint32_t n)
{
    sa1_trace_header h = *ref;
    size_t sz = sizeof h + (size_t)n * sizeof *r;
    unsigned char *b = (unsigned char *)malloc(sz);
    int rc;
    if (b == NULL) {
        return -1;
    }
    h.count = n;
    h.flags = (uint16_t)(h.flags | 4u);
    memcpy(b, &h, sizeof h);
    memcpy(b + sizeof h, r, (size_t)n * sizeof *r);
    rc = wr_write_file(path, b, sz);
    free(b);
    if (rc == 0) {
        wr_obj_write(path, ref, n);     /* round 5: the run's host-object sidecar */
    }
    return rc;
}

/* The reference's screen (trace header flag 8; sadc docs/research/visibility_fidelity.md). Worlds boot with the
 * screen of njInitSystem (src/world/sa1_world_boot.c sa1g_boot_screen), as the reference simulator runs since
 * 2026-10-20. A reference recorded before that (build/traces/ref/<name>.noscreen.bin) or with trace --no-screen had
 * the zero screen: its words are zeroed again (the state libsa1's boot had then), so such a reference replays as it
 * did. Call after the world's boot, before its first step; 1 when the screen was zeroed. */
#define WR_TRACE_FLAG_SCREEN 8u
static int wr_screen_of(sa1g_world *w, const sa1_trace_header *h)
{
    static const uint32_t words[] = {
        0x8C88F5E4u, 0x8C88F5E8u, 0x8C88F5ECu,                  /* distance, width, height */
        0x8C88F600u, 0x8C88F604u, 0x8C88F608u, 0x8C88F60Cu,     /* 2D clip rectangle */
        0x8C88F618u, 0x8C88F61Cu,                               /* aspect */
        0x8C88F620u, 0x8C88F624u, 0x8C88F628u,                  /* njInitSystem's mode words */
        0x8C88F644u, 0x8C88F648u,                               /* width, height (integers) */
    };
    size_t i;
    wr_obj_start(w, h);         /* round 5: a reference with a host-object sidecar starts its run on this world */
    if (w == NULL || h == NULL || (h->flags & WR_TRACE_FLAG_SCREEN) != 0) {
        return 0;
    }
    for (i = 0; i < sizeof words / sizeof words[0]; i++) {
        sa1g_world_wr32(w, words[i], 0u);
    }
    return 1;
}

/* ---- inject / record --------------------------------------------------------------------------------- */
/* the record's input and player-entry state for the next step. cam_from_record: the camera blob goes
 * to the record's px_cam_twp (a transplanted arena), else to the world's camera adapter */
static void wr_inject(sa1g_world *w, const sa1_trace_rec *r, uint32_t flags, int cam_from_record)
{
    sa1g_inject *in = &w->inject;
    in->flags = flags;
    in->stick_x = r->stick_x;
    in->stick_y = r->stick_y;
    in->buttons = r->buttons;
    in->pad_on = r->pad_on;
    in->pad_press = r->pad_press;
    in->analog_ang = r->analog_ang;
    in->analog_stroke = r->analog_stroke;
    if (r->px_calls == 0) {
        in->flags &= ~(uint32_t)(SA1G_INJ_RAND | SA1G_INJ_XMTRX | SA1G_INJ_CAMERA | SA1G_INJ_FRAME | SA1G_INJ_RINGS);
    }
    in->rand = r->px_rand;
    memcpy(in->xmtrx, r->px_xmtrx, sizeof in->xmtrx);
    in->cam_twp = cam_from_record ? r->px_cam_twp : 0u;
    if (cam_from_record && ((r->px_cam_twp >> 24) & 31u) != SA1G_SEG_ARENA) {
        in->flags &= ~(uint32_t)SA1G_INJ_CAMERA;
    }
    memcpy(in->cam_raw, r->px_cam_raw, sizeof in->cam_raw);
    in->frame_counter = r->px_frame_counter;
    in->rings = r->px_rings;
    wr_obj_feed(w, r);          /* round 5: the record's host calls (a trace with a host-object sidecar) */
}

static int wr_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static void wr_copy(const sa1g_world *w, uint8_t *dst, uint32_t g, uint32_t n)
{
    if (!wr_arena(g) || (g & 0xFFFFFFu) > SA1G_ARENA_SIZE - n) {
        memset(dst, 0, n);
        return;
    }
    memcpy(dst, w->ram + (g & 0xFFFFFFu), n);
}

static uint32_t wr_u32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint16_t wr_u16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

static float wr_f32(const uint8_t *p)
{
    float v;
    memcpy(&v, p, 4);
    return v;
}

static uint32_t wr_g32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    sa1g_world_rd32(w, g, &v);
    return v;
}

/* the harness's fill_record on the world, plus the v2 block */
static void wr_record(const sa1g_world *w, const sa1_trace_rec *in, sa1_trace_rec *r)
{
    sa1g_player_ptrs pp;
    int i;
    memset(r, 0, sizeof *r);
    r->step = in->step;
    r->frame = in->frame;
    r->sub = in->sub;
    r->stick_x = in->stick_x;
    r->stick_y = in->stick_y;
    r->buttons = in->buttons;
    r->pad_on = wr_g32(w, SA1G_ADDR_PAD_REC + 0x08u);
    r->pad_press = wr_g32(w, SA1G_ADDR_PAD_REC + 0x10u);
    r->analog_ang = wr_g32(w, SA1G_ADDR_ANALOG);
    {
        uint32_t s = wr_g32(w, SA1G_ADDR_ANALOG + 4u);
        memcpy(&r->analog_stroke, &s, 4);
    }
    sa1g_world_player(w, &pp);
    wr_copy(w, r->twp_raw, pp.twp, 0x40);
    wr_copy(w, r->mwp_raw, pp.mwp, 0x40);
    wr_copy(w, r->pwp_raw, pp.pwp, 0x200);
    wr_copy(w, r->csts_raw, pp.csts, 0x84);
    r->mode = (int8_t)r->twp_raw[0];
    r->smode = (int8_t)r->twp_raw[1];
    r->id = (int8_t)r->twp_raw[2];
    r->btimer = (int8_t)r->twp_raw[3];
    r->twp_flag = (int16_t)wr_u16(r->twp_raw + 4);
    r->wtimer = wr_u16(r->twp_raw + 6);
    for (i = 0; i < 3; i++) {
        r->ang[i] = wr_u32(r->twp_raw + 0x14 + 4 * i);
        r->pos[i] = wr_f32(r->twp_raw + 0x20 + 4 * i);
        r->mspd[i] = wr_f32(r->mwp_raw + 0x04 + 4 * i);
        r->macc[i] = wr_f32(r->mwp_raw + 0x10 + 4 * i);
        r->ang_aim[i] = wr_u32(r->mwp_raw + 0x1C + 4 * i);
        r->pacc[i] = wr_f32(r->pwp_raw + 0x2C + 4 * i);
        r->pspd[i] = wr_f32(r->pwp_raw + 0x38 + 4 * i);
        r->floor_normal[i] = wr_f32(r->pwp_raw + 0x50 + 4 * i);
    }
    r->dotp = wr_f32(r->pwp_raw + 0x1C);
    r->attr = wr_u32(r->pwp_raw + 0x5C);
    r->last_attr = wr_u32(r->pwp_raw + 0x60);
    r->equipment = wr_u16(r->pwp_raw + 0x04);
    r->item = wr_u16(r->pwp_raw + 0x06);
    r->jumptimer = (int16_t)wr_u16(r->pwp_raw + 0x08);
    r->nocontimer = (int16_t)wr_u16(r->pwp_raw + 0x0A);
    r->waittimer = (int16_t)wr_u16(r->pwp_raw + 0x0E);
    r->pwp_flag = (int16_t)wr_u16(r->pwp_raw + 0x10);
    r->reqaction = wr_u16(r->pwp_raw + 0x124);
    r->action = wr_u16(r->pwp_raw + 0x126);
    r->lastaction = wr_u16(r->pwp_raw + 0x128);
    r->nextaction = wr_u16(r->pwp_raw + 0x12A);
    r->nframe = wr_f32(r->pwp_raw + 0x130);
    r->land_count = wr_g32(w, SA1G_ADDR_LAND_COUNT);
    r->rand_state = wr_g32(w, SA1G_ADDR_RAND);
    /* v2 */
    if (w->inject.player_entries != 0) {
        r->px_exec = w->inject.player_exec;
        r->px_calls = w->inject.player_entries;
        r->px_rand = w->inject.px_rand;
        r->px_frame_counter = w->inject.px_frame_counter;
        r->px_rings = w->inject.px_rings;
        memcpy(r->px_gravity, w->inject.px_gravity, sizeof r->px_gravity);
        memcpy(r->px_xmtrx, w->inject.px_xmtrx, sizeof r->px_xmtrx);
        r->px_cam_twp = w->inject.px_cam_twp;
        memcpy(r->px_cam_raw, w->inject.px_cam_raw, sizeof r->px_cam_raw);
    }
    r->frame_counter = wr_g32(w, SA1G_ADDR_FRAME_COUNTER);
    r->rings = (int16_t)wr_g32(w, SA1G_ADDR_RINGS);
    for (i = 0; i < 3; i++) {
        uint32_t v = wr_g32(w, SA1G_ADDR_GRAVITY + 4u * (uint32_t)i);
        memcpy(&r->gravity[i], &v, 4);
    }
    r->ptp_addr = pp.tp;
    r->twp_addr = pp.twp;
    r->mwp_addr = pp.mwp;
    r->pwp_addr = pp.pwp;
    r->csts_addr = pp.csts;
    wr_obj_collect(w, in);      /* round 5: the step's host-object record (a trace with a host-object sidecar) */
    if (wr_run.o == NULL && getenv("SA1_REPLAY_EVENTS") != NULL) {
        /* round 0.9.1 diagnostics (SA1_REPLAY_EVENTS=1): the deaths, restarts and jingles of a replay without a sidecar
           (big_drown_ec3: SA1_EVENT_DEATH cause DROWN, sadc docs/research/gsa1_070_refs.md 5.4); reads the queue only */
        sa1_event we[16];
        uint32_t n, k;
        do {
            n = sa1g_world_poll_events((sa1g_world *)(uintptr_t)w, we, 16);
            for (k = 0; k < n; k++) {
                if (we[k].type == SA1_EVENT_DEATH || we[k].type == SA1_EVENT_RESTART || we[k].type == SA1_EVENT_JINGLE
                    || we[k].type == SA1_EVENT_TIME_UP) {
                    printf("    replay event: step %u type %u id %u arg 0x%X\n", (unsigned)we[k].step, (unsigned)we[k].type,
                           (unsigned)we[k].id, (unsigned)we[k].arg);
                }
            }
        } while (n == 16);
    }
}

/* ---- comparison (sa1trace.py diff --ulps 0 [--raw [--heap-by-structure]]) ------------------------------ */
enum { WR_I8, WR_I16, WR_U16, WR_U32, WR_F32, WR_ANG, WR_PTR };

typedef struct wr_field {
    const char *name;
    uint16_t off;
    uint8_t n, kind;
} wr_field;

#define WR_F(f, n, k) { #f, (uint16_t)offsetof(sa1_trace_rec, f), (n), (k) }
static const wr_field k_wr_fields[] = {
    WR_F(stick_x, 1, WR_I16), WR_F(stick_y, 1, WR_I16), WR_F(buttons, 1, WR_U32),
    WR_F(pad_on, 1, WR_U32), WR_F(pad_press, 1, WR_U32), WR_F(analog_ang, 1, WR_ANG), WR_F(analog_stroke, 1, WR_F32),
    WR_F(mode, 1, WR_I8), WR_F(smode, 1, WR_I8), WR_F(id, 1, WR_I8), WR_F(btimer, 1, WR_I8),
    WR_F(twp_flag, 1, WR_I16), WR_F(wtimer, 1, WR_U16),
    WR_F(ang, 3, WR_ANG), WR_F(pos, 3, WR_F32), WR_F(mspd, 3, WR_F32), WR_F(macc, 3, WR_F32), WR_F(ang_aim, 3, WR_ANG),
    WR_F(pspd, 3, WR_F32), WR_F(pacc, 3, WR_F32), WR_F(floor_normal, 3, WR_F32), WR_F(dotp, 1, WR_F32),
    WR_F(attr, 1, WR_U32), WR_F(last_attr, 1, WR_U32),
    WR_F(pwp_flag, 1, WR_I16), WR_F(jumptimer, 1, WR_I16), WR_F(nocontimer, 1, WR_I16), WR_F(waittimer, 1, WR_I16),
    WR_F(reqaction, 1, WR_U16), WR_F(action, 1, WR_U16), WR_F(lastaction, 1, WR_U16), WR_F(nextaction, 1, WR_U16),
    WR_F(nframe, 1, WR_F32), WR_F(equipment, 1, WR_U16), WR_F(item, 1, WR_U16), WR_F(land_count, 1, WR_U32),
    WR_F(rand_state, 1, WR_U32),
    /* v2 scalars */
    WR_F(px_calls, 1, WR_U32), WR_F(px_rand, 1, WR_U32), WR_F(px_frame_counter, 1, WR_U32), WR_F(px_rings, 1, WR_I16),
    WR_F(px_gravity, 3, WR_F32), WR_F(frame_counter, 1, WR_U32), WR_F(rings, 1, WR_I16), WR_F(gravity, 3, WR_F32),
    /* addresses (compared as pointers; only with raw) */
    WR_F(px_exec, 1, WR_PTR), WR_F(px_cam_twp, 1, WR_PTR), WR_F(ptp_addr, 1, WR_PTR), WR_F(twp_addr, 1, WR_PTR),
    WR_F(mwp_addr, 1, WR_PTR), WR_F(pwp_addr, 1, WR_PTR), WR_F(csts_addr, 1, WR_PTR),
};
#define WR_NFIELDS ((int)(sizeof k_wr_fields / sizeof k_wr_fields[0]))

/* blobs: (name, record offset, size); pointer words / union words as sa1trace.py PTR_WORDS / UNION_WORDS */
enum { WR_TWP, WR_MWP, WR_PWP, WR_CSTS, WR_CAM, WR_XMTRX, WR_NBLOBS };
static const struct {
    const char *name;
    uint16_t off, size;
} k_wr_blobs[WR_NBLOBS] = {
    { "twp_raw", (uint16_t)offsetof(sa1_trace_rec, twp_raw), 0x40 },
    { "mwp_raw", (uint16_t)offsetof(sa1_trace_rec, mwp_raw), 0x40 },
    { "pwp_raw", (uint16_t)offsetof(sa1_trace_rec, pwp_raw), 0x200 },
    { "csts_raw", (uint16_t)offsetof(sa1_trace_rec, csts_raw), 0x84 },
    { "px_cam_raw", (uint16_t)offsetof(sa1_trace_rec, px_cam_raw), 0x40 },
    { "px_xmtrx", (uint16_t)offsetof(sa1_trace_rec, px_xmtrx), 0x40 },
};

static int wr_is_ptr_word(int blob, uint32_t off)
{
    switch (blob) {
    case WR_TWP:
    case WR_CAM:
        return off == 0x38 || off == 0x3C;
    case WR_MWP:
        return off == 0x00;
    case WR_PWP:
        return off == 0x64 || off == 0x68 || off == 0x6C || off == 0x70 || off == 0x74 || off == 0x78 || off == 0x138
               || off == 0x13C || off == 0x140 || off == 0x144 || off == 0x148;
    default:
        return 0;
    }
}

static int wr_is_union_word(int blob, uint32_t off)
{
    switch (blob) {
    case WR_TWP:
    case WR_CAM:
        return off == 0x08 || off == 0x0C || off == 0x10;
    case WR_PWP:
        return off == 0x000 || off == 0x07C;
    default:
        return 0;
    }
}

static int wr_guest_ptr(uint32_t v)
{
    return (v & 0x1C000000u) == 0x0C000000u && v < 0xC0000000u;
}

/* comparison key of a pointer (sa1trace.py canon_ptr): physical address; with heap_struct, a pointer into
 * one of the record's own blobs is "blob + offset" (1 << 32 | blob << 16 | offset), any other heap pointer
 * "heap" (2 << 32) */
static uint64_t wr_canon(uint32_t v, const sa1_trace_rec *r, uint32_t heap_struct)
{
    static const struct {
        uint16_t field, size;
    } st[] = {
        { (uint16_t)offsetof(sa1_trace_rec, twp_addr), 0x40 }, { (uint16_t)offsetof(sa1_trace_rec, mwp_addr), 0x40 },
        { (uint16_t)offsetof(sa1_trace_rec, pwp_addr), 0x200 }, { (uint16_t)offsetof(sa1_trace_rec, csts_addr), 0x84 },
        { (uint16_t)offsetof(sa1_trace_rec, ptp_addr), 0x30 }, { (uint16_t)offsetof(sa1_trace_rec, px_cam_twp), 0x40 },
    };
    uint32_t q;
    size_t k;
    if (v == 0) {
        return 0;
    }
    q = v & 0x1FFFFFFFu;
    if (heap_struct && wr_guest_ptr(v)) {
        for (k = 0; k < sizeof st / sizeof st[0]; k++) {
            uint32_t base;
            memcpy(&base, (const uint8_t *)r + st[k].field, 4);
            if (base != 0 && wr_guest_ptr(base) && q >= (base & 0x1FFFFFFFu) && q < (base & 0x1FFFFFFFu) + st[k].size) {
                return (1ull << 32) | ((uint64_t)k << 16) | (q - (base & 0x1FFFFFFFu));
            }
        }
        if (q >= (SA1G_ADDR_HEAP & 0x1FFFFFFFu) && q < (SA1G_ADDR_HEAP & 0x1FFFFFFFu) + SA1G_HEAP_SIZE) {
            return 2ull << 32;
        }
    }
    return q;
}

/* what to compare */
#define WR_CMP_RAW        0x01u   /* raw blobs, camera blob, XMTRX, blob addresses */
#define WR_CMP_HEAP       0x02u   /* pointers by structure (another heap layout) */
#define WR_CMP_NO_LAND    0x04u   /* skip land_count, attr, last_attr (M0: empty landtable) */
#define WR_CMP_NO_V2      0x08u   /* skip the v2 scalars */
#define WR_CMP_NO_LANDCNT 0x10u   /* skip land_count only */
#define WR_CMP_NO_RAND    0x20u   /* skip rand_state only */

typedef struct wr_diff {
    int first_step[64];
    char first_msg[64][160];
    uint32_t nsteps_bad;
    int first_bad_step;
} wr_diff;

static void wr_diff_init(wr_diff *d)
{
    int k;
    memset(d, 0, sizeof *d);
    for (k = 0; k < 64; k++) {
        d->first_step[k] = -1;
    }
    d->first_bad_step = -1;
}

static int wr_skip_field(const wr_field *f, uint32_t how)
{
    if ((how & WR_CMP_NO_LAND)
        && (!strcmp(f->name, "land_count") || !strcmp(f->name, "attr") || !strcmp(f->name, "last_attr"))) {
        return 1;
    }
    if ((how & WR_CMP_NO_V2) && f->off >= SA1_TRACE_REC_V1_SIZE) {
        return 1;
    }
    if (((how & WR_CMP_NO_LANDCNT) && !strcmp(f->name, "land_count"))
        || ((how & WR_CMP_NO_RAND) && !strcmp(f->name, "rand_state"))) {
        return 1;
    }
    if (f->kind == WR_PTR && !(how & WR_CMP_RAW)) {
        return 1;
    }
    return 0;
}

static void wr_fmt(char *buf, size_t n, int kind, const uint8_t *p)
{
    switch (kind) {
    case WR_F32:
        snprintf(buf, n, "%.9g (%08X)", (double)wr_f32(p), (unsigned)wr_u32(p));
        break;
    case WR_I8:
        snprintf(buf, n, "%d", (int)(int8_t)p[0]);
        break;
    case WR_I16:
    case WR_U16:
        snprintf(buf, n, "%X", (unsigned)wr_u16(p));
        break;
    default:
        snprintf(buf, n, "%08X", (unsigned)wr_u32(p));
        break;
    }
}

/* compares one record; returns the number of differing fields / blobs */
static int wr_compare(const sa1_trace_rec *ref, const sa1_trace_rec *out, uint32_t how, wr_diff *d, int step)
{
    int k, bad = 0;
    for (k = 0; k < WR_NFIELDS; k++) {
        const wr_field *f = &k_wr_fields[k];
        const uint8_t *a = (const uint8_t *)ref + f->off, *b = (const uint8_t *)out + f->off;
        int j, diff = 0, size = f->kind == WR_I8 ? 1 : (f->kind == WR_I16 || f->kind == WR_U16) ? 2 : 4;
        if (wr_skip_field(f, how)) {
            continue;
        }
        for (j = 0; j < f->n && !diff; j++) {
            const uint8_t *pa = a + size * j, *pb = b + size * j;
            switch (f->kind) {
            case WR_I8:
                diff = pa[0] != pb[0];
                break;
            case WR_I16:
            case WR_U16:
                diff = wr_u16(pa) != wr_u16(pb);
                break;
            case WR_U32:
                diff = wr_u32(pa) != wr_u32(pb);
                break;
            case WR_ANG:
                diff = ((wr_u32(pa) - wr_u32(pb)) & 0xFFFFu) != 0;
                break;
            case WR_F32: {
                float x = wr_f32(pa), y = wr_f32(pb);
                diff = !(x != x && y != y) && wr_u32(pa) != wr_u32(pb);
                break;
            }
            case WR_PTR:
                diff = wr_canon(wr_u32(pa), ref, how & WR_CMP_HEAP) != wr_canon(wr_u32(pb), out, how & WR_CMP_HEAP);
                break;
            }
            if (diff) {
                bad++;
                if (d->first_step[k] < 0) {
                    char va[64], vb[64];
                    d->first_step[k] = step;
                    wr_fmt(va, sizeof va, f->kind, pa);
                    wr_fmt(vb, sizeof vb, f->kind, pb);
                    snprintf(d->first_msg[k], sizeof d->first_msg[k], "%s[%d]: reference %s, world %s", f->name, j, va,
                             vb);
                }
            }
        }
    }
    if (how & WR_CMP_RAW) {
        for (k = 0; k < WR_NBLOBS; k++) {
            const uint8_t *a = (const uint8_t *)ref + k_wr_blobs[k].off, *b = (const uint8_t *)out + k_wr_blobs[k].off;
            uint32_t off, nd = 0, first = 0, pa = 0, pb = 0;
            if ((how & WR_CMP_NO_V2) && (k == WR_CAM || k == WR_XMTRX)) {
                continue;
            }
            for (off = 0; off + 4 <= k_wr_blobs[k].size; off += 4) {
                uint32_t p = wr_u32(a + off), q = wr_u32(b + off);
                if (p == q) {
                    continue;
                }
                if (wr_is_ptr_word(k, off) || (wr_is_union_word(k, off) && wr_guest_ptr(p) && wr_guest_ptr(q))) {
                    if (wr_canon(p, ref, how & WR_CMP_HEAP) == wr_canon(q, out, how & WR_CMP_HEAP)) {
                        continue;
                    }
                }
                if (nd++ == 0) {
                    first = off;
                    pa = p;
                    pb = q;
                }
            }
            if (nd != 0) {
                bad++;
                if (d->first_step[WR_NFIELDS + k] < 0) {
                    d->first_step[WR_NFIELDS + k] = step;
                    snprintf(d->first_msg[WR_NFIELDS + k], sizeof d->first_msg[WR_NFIELDS + k],
                             "%s+0x%03X: reference %08X, world %08X (%u words differ)", k_wr_blobs[k].name,
                             (unsigned)first, (unsigned)pa, (unsigned)pb, (unsigned)nd);
                }
            }
        }
    }
    if (bad) {
        d->nsteps_bad++;
        if (d->first_bad_step < 0) {
            d->first_bad_step = step;
        }
    }
    return bad;
}

/* the first difference of each field / blob, in step order */
static void wr_diff_print(const wr_diff *d, int max_lines)
{
    int k, shown = 0, s = -1;
    for (;;) {
        int next = 0x7FFFFFFF;
        for (k = 0; k < WR_NFIELDS + WR_NBLOBS; k++) {
            if (d->first_step[k] > s && d->first_step[k] < next) {
                next = d->first_step[k];
            }
        }
        if (next == 0x7FFFFFFF) {
            return;
        }
        for (k = 0; k < WR_NFIELDS + WR_NBLOBS; k++) {
            if (d->first_step[k] == next) {
                if (shown++ >= max_lines) {
                    return;
                }
                printf("      step %d: %s\n", next, d->first_msg[k]);
            }
        }
        s = next;
    }
}

/* ---- one replay ------------------------------------------------------------------------------------- */
typedef struct wr_result {
    uint32_t steps;             /* steps run */
    int fault;                  /* a step failed */
    uint32_t pad_mismatch;      /* steps where the pad update computed other on/press/analog than the record */
    int first_pad_mismatch;
    uint32_t rand_pre_mismatch; /* steps where the world's rand at player entry differed before the injection */
    int first_rand_pre;
    uint32_t xmtrx_pre_mismatch;
    uint32_t no_entry;          /* steps without a player-exec entry */
    wr_diff d;
} wr_result;

/* replay records [first, end) of t into w (single steps with the record's sub); out (may be NULL) gets
 * the world's records (out[0] = record first) */
static void wr_replay(sa1g_world *w, const wr_trace *t, uint32_t first, uint32_t end, uint32_t inject,
                      int cam_from_record, uint32_t how, sa1_trace_rec *out, wr_result *res)
{
    uint32_t i;
    sa1_trace_rec mine;
    memset(res, 0, sizeof *res);
    wr_diff_init(&res->d);
    res->first_pad_mismatch = -1;
    res->first_rand_pre = -1;
    w->replay = 1;
    for (i = first; i < end && i < t->n; i++) {
        const sa1_trace_rec *r = &t->r[i];
        sa1_status st;
        wr_inject(w, r, inject, cam_from_record);
        st = sa1g_world_step(w, (int32_t)r->sub);
        if (st != SA1_OK) {
            printf("    step %u: %s: %s\n", (unsigned)i, sa1g_status_string(st), w->fault.msg);
            res->fault = 1;
            break;
        }
        res->steps++;
        if (w->inject.pad_computed_on != r->pad_on || w->inject.pad_computed_press != r->pad_press
            || ((w->inject.analog_computed_ang ^ r->analog_ang) & 0xFFFFu) != 0
            || memcmp(&w->inject.analog_computed_stroke, &r->analog_stroke, 4) != 0) {
            if (res->pad_mismatch++ == 0) {
                res->first_pad_mismatch = (int)i;
            }
        }
        if (w->inject.player_entries == 0) {
            res->no_entry++;
        } else {
            if (w->inject.pre_rand != r->px_rand && res->rand_pre_mismatch++ == 0) {
                res->first_rand_pre = (int)i;
            }
            if (memcmp(w->inject.pre_xmtrx, r->px_xmtrx, sizeof r->px_xmtrx) != 0) {
                res->xmtrx_pre_mismatch++;
            }
        }
        wr_record(w, r, &mine);
        wr_compare(r, &mine, how, &res->d, (int)i);
        if (out != NULL) {
            out[i - first] = mine;
        }
    }
}

/* the game's rand(): x * 0x41C64E6D + 0x3039 */
static uint32_t wr_lcg(uint32_t x)
{
    return x * 0x41C64E6Du + 0x3039u;
}

/* m >= 1 such that m draws turn a into b (0: none within 64) */
static uint32_t wr_rand_ahead(uint32_t a, uint32_t b)
{
    uint32_t m;
    for (m = 1; m <= 64; m++) {
        a = wr_lcg(a);
        if (a == b) {
            return m;
        }
    }
    return 0;
}

static void wr_result_print(const char *what, const wr_result *res, uint32_t n)
{
    printf("    %s: %u of %u steps run, %u with differences%s\n", what, (unsigned)res->steps, (unsigned)n,
           (unsigned)res->d.nsteps_bad, res->fault ? " (stopped by a fault)" : "");
    printf("      input: the pad update computed the record's on/press/analog in %u steps (first other: %d);"
           " player entries missing in %u steps\n", (unsigned)(res->steps - res->pad_mismatch), res->first_pad_mismatch,
           (unsigned)res->no_entry);
    printf("      before injection: rand equal to the record's px_rand in %u steps (first other: %d), XMTRX in %u\n",
           (unsigned)(res->steps - res->no_entry - res->rand_pre_mismatch), res->first_rand_pre,
           (unsigned)(res->steps - res->no_entry - res->xmtrx_pre_mismatch));
    wr_diff_print(&res->d, 24);
}
