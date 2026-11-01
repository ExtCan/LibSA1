/*
 * sa1_preview.c - object previews, the context side (API SA1_API_PREVIEW, round 0.9.4b; sadc docs/research/objects2_plan.md
 * 5.3 and 6.2 tasks 1, 3, 5, 7 with its Review R.1-R.3, R.10, R.18, R.20; objects2_view.md 5). The world side is
 * src/world/sa1_world_preview.c; the overview src/sa1_preview.h.
 *
 * The registry: a process table keyed by the context (a short spin lock guards the table, never a world), as sa1_set.c's,
 * created by the first sa1_preview_add / sa1_preview_ring_places and freed by sa1pv_destroy_all (sa1_characters_destroy_all:
 * sa1_unload_disc, sa1_load_disc, sa1_shutdown). It holds one showcase per character kind (SA1_CHAR_*: Sonic, Tails,
 * Knuckles, Amy, E-102, Big; Eggman, Tikal and Super Sonic have none) and the copies. Ids: a process-wide serial << 12 |
 * (index + 1): never 0, never given twice in a process (an id of an unloaded disc stays SA1_ERR_NOT_FOUND after a reload).
 *
 * A pair of a showcase (sa1_preview_tick, task 5): its capture window open, one pair of its world (sa1wp_pair: the events
 * drained and dropped), the window closed, the snapshot emitted once (sa1_capture_emit_into: effects, exec arrays, the blob
 * shadow's model, dedupe as sa1_character_render's emit, but clip 0 - sa1_character_render's emit drops draws beyond
 * SA1_MESH_CLIP_RADIUS of the character: review R.1) and SPLIT per copy without a capture change: every drawn model belongs to
 * the slot nearest to its model -> world matrix's translation (sa1_meshbuf.node_wm), a triangle to the model owning its first
 * vertex (node_v), vertices of no model (3D polygons), sprites and line segments by their own position. Each copy gets its own
 * draws - a libsa1 draw that merged several copies (objects2_view.md M7) is cut into one draw per copy with the same state,
 * the game's order kept - with its positions in OBJECT SPACE (host units and axes): a drawn model's vertices from their
 * model-local points and the model's matrix in double minus the slot (the emitter's float world positions lose half an ulp
 * at the grid's corners, 15,360 units out: review R.1), other vertices, sprites and lines from their host position minus
 * the slot's, in double. Each copy keeps its last two pairs; sa1_preview_render interpolates between them (alpha 1 or more: the last pair;
 * 0: the pair before; between: a vertex lerp of positions, normals lerped and normalised, the last pair's colours, UVs and
 * draws, when both pairs have the same draws; else the last pair at every alpha; alpha < 0: the previews' accumulator).
 *
 * A showcase that faults: every copy of its kind returns SA1_ERR_GAME_FAULT (sa1_last_error names the address) until
 * sa1_preview_clear or until all of them are removed (the showcase is then freed: the next add makes a new one); the
 * character worlds and the other showcases keep running. A showcase without copies runs no pairs once its removed copies'
 * tasks have ended, and is freed SA1_PREVIEW_IDLE_PAIRS pairs later (counted by sa1_preview_tick; review R.18).
 *
 * Threading: like sa1_set_* and sa1_hostobj_*, these calls belong to the thread that ticks the context's characters (one
 * world runs at a time: never inside a character world's tick).
 */
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_capture.h"
#include "sa1_preview.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PV_MAGIC   0x56454950u              /* "PIEV" */
#define PV_TABLE   64u                      /* contexts with previews per process */
#define PV_INDEX   (SA1_CHAR_COUNT * SA1_PREVIEW_MAX)   /* copy index space (12 bits) */
#define PV_PAIR    (1.0 / 30.0)
#define PV_MAX_PAIRS 8u                     /* pairs per sa1_preview_tick (SA1G_MAX_PAIRS_PER_TICK) */

/* slot states */
#define PV_FREE    0u
#define PV_LIVE    1u
#define PV_LEAVING 2u                       /* the copy was removed: its task freed, its condition not released yet */

/* the kinds with a showcase (the ticking kinds without Super Sonic) */
#define PV_KINDS ((1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) | \
                  (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG))

#if !defined(SA1_NO_WORLD)
/* the types' preview table (task 7): rotation and settle steps (60 Hz steps until a new copy looks settled) */
typedef struct pv_tinfo {
    uint32_t rotation, settle, records, exec;
} pv_tinfo;
/* settle: the steps a new copy has run when its draws first have their final size (preview_test "settle" measures it):
 * RING / O_GRING 0 (no grow-in); ITEMBOX 47: its mode-0 step, then scl.z 0 -> 5 by 0.11 a step in mode 1 (46 steps,
 * code_8C05D0A0.c func_8C05D360), drawn at full size by the exec of age 47 (mode 2); ITEMBOX_AIR 23: its work's scale (+0x1C)
 * rises 0.05 a step from age 2 to 1.0 (code_8C05DF18.c:67-69), drawn at that scale before the rise in modes 0 / 1
 * (code_8C05DC60.c:97-99; review R.10): full size from the exec of age 23 (the review's "20 steps from 0" before the mode-0
 * step and the draw-then-grow order). A broken ground box has no grow-in (mode 3 at once: code_8C05D0A0.c:177-179).
 * records: the capture records a copy is expected to take per pair (task 3; a line n + 1, a circle n). */
static const pv_tinfo k_tinfo[SA1SET_NTYPES] = {
    { SA1_PREVIEW_ROT_OUTER, 0u, 1u, 0x8C0869E8u },
    { SA1_PREVIEW_ROT_INNER, 0u, 0u, 0x8C087B80u },
    { SA1_PREVIEW_ROT_NONE, 47u, 8u, 0x8C05D10Cu },
    { SA1_PREVIEW_ROT_OUTER, 23u, 7u, 0x8C05D9F6u },
};

static int pv_finite(float f)
{
    return sa1_finite(f);
}

static int pv_is_int(float f, float lo, float hi)
{
    return pv_finite(f) && f >= lo && f <= hi && (float)(int32_t)f == f;
}

static int pv_kind_has_showcase(int32_t kind)
{
    return kind >= 0 && kind < SA1_CHAR_COUNT && (PV_KINDS & (1u << kind)) != 0;
}

#endif

sa1_status sa1_preview_type_get(const sa1_context *ctx, uint32_t type, sa1_preview_type *out)
{
    sa1_preview_type info;
    if (ctx == NULL || out == NULL || out->struct_size < sizeof(uint32_t)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
#if defined(SA1_NO_WORLD)
    (void)type;
    (void)info;
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    if (type == SA1_PREVIEW_BOARD) {
        /* round 0.9.5 (SA1_PREVIEW_BOARD, gsa1_095_plan.md 5.3): the board model alone (kinds 0 / 2); not available in the
           optimised archives before lib_done.txt (SA1WB_GATED) */
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        info.type = type;
        info.available = (uint32_t)(!SA1WB_GATED && sa1w_ready());
        info.rotation = SA1_PREVIEW_ROT_OUTER;
        info.settle_steps = 0u;
        sa1_copy_out(out, &info, sizeof info);
        return SA1_OK;
    }
    if (sa1set_type_of(type) == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.type = type;
    info.available = (uint32_t)(sa1w_ready() && sa1w_set_exec_native(k_tinfo[type - 1u].exec));
    info.rotation = k_tinfo[type - 1u].rotation;
    info.settle_steps = k_tinfo[type - 1u].settle;
    sa1_copy_out(out, &info, sizeof info);
    return SA1_OK;
#endif
}

#if defined(SA1_NO_WORLD)
/* ---- without the world core: nothing runs ---------------------------------------------------------------------------- */
sa1_status sa1_preview_add(sa1_context *ctx, const sa1_preview_desc *d, uint32_t *out_id)
{
    (void)d;
    if (out_id != NULL) {
        *out_id = 0;
    }
    return ctx ? SA1_ERR_NOT_IMPLEMENTED : SA1_ERR_INVALID_ARGUMENT;
}
sa1_status sa1_preview_remove(sa1_context *ctx, uint32_t id)
{
    (void)id;
    return ctx ? SA1_ERR_NOT_IMPLEMENTED : SA1_ERR_INVALID_ARGUMENT;
}
sa1_status sa1_preview_clear(sa1_context *ctx)
{
    return ctx ? SA1_ERR_NOT_IMPLEMENTED : SA1_ERR_INVALID_ARGUMENT;
}
sa1_status sa1_preview_tick(sa1_context *ctx, float dt)
{
    (void)dt;
    return ctx ? SA1_ERR_NOT_IMPLEMENTED : SA1_ERR_INVALID_ARGUMENT;
}
sa1_status sa1_preview_render(sa1_context *ctx, uint32_t id, float alpha, const sa1_render **out)
{
    (void)id;
    (void)alpha;
    if (out != NULL) {
        *out = NULL;
    }
    return ctx ? SA1_ERR_NOT_IMPLEMENTED : SA1_ERR_INVALID_ARGUMENT;
}
sa1_status sa1_preview_info_get(const sa1_context *ctx, uint32_t id, sa1_preview_info *out)
{
    (void)id;
    (void)out;
    return ctx ? SA1_ERR_NOT_IMPLEMENTED : SA1_ERR_INVALID_ARGUMENT;
}
sa1_status sa1_preview_ring_places(sa1_context *ctx, int32_t kind, const float params[3], const float rotation[9],
                                   float *places, uint32_t *bits, uint32_t capacity, uint32_t *count)
{
    (void)kind;
    (void)params;
    (void)rotation;
    (void)places;
    (void)bits;
    (void)capacity;
    if (count != NULL) {
        *count = 0;
    }
    return ctx ? SA1_ERR_NOT_IMPLEMENTED : SA1_ERR_INVALID_ARGUMENT;
}
void sa1pv_destroy_all(sa1_context *ctx)
{
    (void)ctx;
}
sa1_status sa1pv_test_fault(sa1_context *ctx, uint32_t id)
{
    (void)ctx;
    (void)id;
    return SA1_ERR_NOT_IMPLEMENTED;
}
sa1_status sa1pv_test_showcase(const sa1_context *ctx, int32_t kind, sa1pv_test_info *out, struct sa1g_world **world)
{
    (void)ctx;
    (void)kind;
    if (out != NULL) {
        memset(out, 0, sizeof *out);
    }
    if (world != NULL) {
        *world = NULL;
    }
    return SA1_ERR_NOT_IMPLEMENTED;
}
int32_t sa1pv_test_slot(const sa1_context *ctx, uint32_t id)
{
    (void)ctx;
    (void)id;
    return -1;
}
#else

/* ---- the registry --------------------------------------------------------------------------------------------------- */
/* one pair of a copy, object space */
typedef struct pv_frame {
    uint32_t nt, nd, ns, nl;                /* triangles, draws, sprites, line vertices */
    uint32_t cap_t, cap_d, cap_s, cap_l;
    float *pos, *nrm, *uv;                  /* 9 / 9 / 6 per triangle */
    uint8_t *col;                           /* 12 per triangle */
    sa1_draw *draws;
    sa1_sprite *spr;
    sa1_line_vertex *lv;
    float bounds[6];
    uint32_t src;                           /* the split: the source draw of the frame's last draw (0xFFFFFFFF none) */
} pv_frame;

typedef struct pv_copy {
    uint32_t id;
    int32_t kind;
    uint32_t type, slot, tp, records;
    uint32_t steps, pairs;
    pv_frame fr[2];
    int32_t cur;                            /* the last pair's frame */
    int32_t fill;                           /* the frame the split fills (-1: none this pair) */
    uint32_t nfr;                           /* frames held: 0..2 */
    sa1_render r;
    float *opos, *onrm;                     /* the interpolated output (9 per triangle each) */
    uint32_t cap_o;
    sa1_sprite *ospr;
    uint32_t cap_os;
    sa1_line_vertex *olv;
    uint32_t cap_ol;
} pv_copy;

typedef struct pv_show {
    int32_t kind;
    struct sa1g_world *w;
    sa1_capture *cap;
    const sa1_charset *cs;
    sa1_meshbuf mb;
    uint32_t copies, leaving, idle, pairs, records_est, cap_capacity;
    int32_t dead;
    sa1_status dead_status;
    char fault[256];
    uint16_t slot_copy[SA1PV_SLOTS];        /* copy index + 1 of a live slot */
    uint8_t slot_state[SA1PV_SLOTS];        /* PV_FREE / PV_LIVE / PV_LEAVING */
    double hslot[SA1PV_SLOTS][3];           /* each slot's host position */
    int16_t *vslot;                         /* the split's scratch: the slot of each emitted vertex */
    uint32_t cap_vslot;
    int32_t *vnode;                         /* ... and its drawn model (-1: none, a 3D polygon's) */
    uint32_t cap_vnode;
} pv_show;

typedef struct pv_reg {
    uint32_t magic;
    const sa1_context *ctx;
    pv_show *show[SA1_CHAR_COUNT];
    pv_copy *copy[PV_INDEX];
    uint32_t hiwater;                       /* copy indices [0, hiwater) may be in use */
    double acc;                             /* the previews' accumulator (seconds) */
} pv_reg;

static struct {
    const sa1_context *ctx;
    pv_reg *r;
} g_tab[PV_TABLE];
static volatile int32_t g_lock;
static volatile int32_t g_serial;

static void tab_lock(void)
{
    while (sa1_atomic_add32(&g_lock, 1) != 1) {
        sa1_atomic_add32(&g_lock, -1);
    }
}

static void tab_unlock(void)
{
    sa1_atomic_add32(&g_lock, -1);
}

static pv_reg *reg_of(const sa1_context *ctx)
{
    pv_reg *r = NULL;
    uint32_t i;
    if (ctx == NULL) {
        return NULL;
    }
    tab_lock();
    for (i = 0; i < PV_TABLE; i++) {
        if (g_tab[i].ctx == ctx) {
            r = g_tab[i].r;
            break;
        }
    }
    tab_unlock();
    return r;
}

static sa1_status reg_get(sa1_context *ctx, pv_reg **out)
{
    pv_reg *r = reg_of(ctx);
    uint32_t i, slot = PV_TABLE;
    *out = r;
    if (r != NULL) {
        return SA1_OK;
    }
    r = (pv_reg *)sa1_mem_alloc(ctx, sizeof *r);
    if (r == NULL) {
        return sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "previews: cannot allocate the registry (%u bytes)", (unsigned)sizeof *r);
    }
    r->magic = PV_MAGIC;
    r->ctx = ctx;
    tab_lock();
    for (i = 0; i < PV_TABLE; i++) {
        if (g_tab[i].ctx == NULL && slot == PV_TABLE) {
            slot = i;
        }
    }
    if (slot < PV_TABLE) {
        g_tab[slot].ctx = ctx;
        g_tab[slot].r = r;
    }
    tab_unlock();
    if (slot == PV_TABLE) {
        sa1_mem_free(ctx, r);
        return sa1_fail(ctx, SA1_ERR_LIMIT, "previews: more than %u contexts with previews in this process",
                        (unsigned)PV_TABLE);
    }
    *out = r;
    return SA1_OK;
}

static pv_copy *copy_of(const pv_reg *R, uint32_t id)
{
    uint32_t idx = (id & 0xFFFu) - 1u;
    if (R == NULL || id == 0 || idx >= PV_INDEX || R->copy[idx] == NULL || R->copy[idx]->id != id) {
        return NULL;
    }
    return R->copy[idx];
}

/* grows a zeroed array of elem-sized items to hold need (keeps used items) */
static int grow(const sa1_context *ctx, void **p, uint32_t *cap, uint32_t need, uint32_t used, size_t elem)
{
    uint32_t c;
    void *q;
    if (need <= *cap) {
        return 1;
    }
    c = *cap ? *cap : 64u;
    while (c < need) {
        c = c > 0x40000000u ? need : 2u * c;
    }
    q = sa1_mem_array(ctx, c, elem);
    if (q == NULL) {
        return 0;
    }
    if (*p != NULL && used) {
        memcpy(q, *p, (size_t)used * elem);
    }
    sa1_mem_free(ctx, *p);
    *p = q;
    *cap = c;
    return 1;
}

static void frame_free(const sa1_context *ctx, pv_frame *f)
{
    sa1_mem_free(ctx, f->pos);
    sa1_mem_free(ctx, f->nrm);
    sa1_mem_free(ctx, f->uv);
    sa1_mem_free(ctx, f->col);
    sa1_mem_free(ctx, f->draws);
    sa1_mem_free(ctx, f->spr);
    sa1_mem_free(ctx, f->lv);
    memset(f, 0, sizeof *f);
}

static void copy_free(const sa1_context *ctx, pv_copy *c)
{
    if (c == NULL) {
        return;
    }
    frame_free(ctx, &c->fr[0]);
    frame_free(ctx, &c->fr[1]);
    sa1_mem_free(ctx, c->opos);
    sa1_mem_free(ctx, c->onrm);
    sa1_mem_free(ctx, c->ospr);
    sa1_mem_free(ctx, c->olv);
    sa1_mem_free(ctx, c);
}

static void show_free(sa1_context *ctx, pv_show *S)
{
    if (S == NULL) {
        return;
    }
    /* the capture reads the world's memory: it goes first */
    sa1_capture_free(ctx, S->cap);
    sa1_meshbuf_free_exec(ctx, &S->mb);
    sa1_meshbuf_free(ctx, &S->mb);
    sa1wp_destroy(ctx, S->w);
    sa1_mem_free(ctx, S->vslot);
    sa1_mem_free(ctx, S->vnode);
    sa1_mem_free(ctx, S);
}

/* frees the copy at index idx (bookkeeping only; its slot is the caller's) */
static void copy_drop(sa1_context *ctx, pv_reg *R, uint32_t idx)
{
    copy_free(ctx, R->copy[idx]);
    R->copy[idx] = NULL;
    while (R->hiwater > 0 && R->copy[R->hiwater - 1u] == NULL) {
        R->hiwater--;
    }
}

/* every copy of a kind and its showcase, now */
static void kind_clear(sa1_context *ctx, pv_reg *R, int32_t kind)
{
    uint32_t i;
    for (i = 0; i < R->hiwater; i++) {
        if (R->copy[i] != NULL && R->copy[i]->kind == kind) {
            copy_drop(ctx, R, i);
        }
    }
    show_free(ctx, R->show[kind]);
    R->show[kind] = NULL;
}

void sa1pv_destroy_all(sa1_context *ctx)
{
    pv_reg *R = NULL;
    uint32_t i;
    int32_t k;
    if (ctx == NULL) {
        return;
    }
    tab_lock();
    for (i = 0; i < PV_TABLE; i++) {
        if (g_tab[i].ctx == ctx) {
            R = g_tab[i].r;
            g_tab[i].ctx = NULL;
            g_tab[i].r = NULL;
        }
    }
    tab_unlock();
    if (R == NULL) {
        return;
    }
    for (k = 0; k < SA1_CHAR_COUNT; k++) {
        kind_clear(ctx, R, k);
    }
    for (i = 0; i < PV_INDEX; i++) {
        copy_free(ctx, R->copy[i]);
    }
    R->magic = 0;
    sa1_mem_free(ctx, R);
}

/* ---- the showcase of a kind ------------------------------------------------------------------------------------------- */
static const char *kind_name(int32_t kind)
{
    const char *n = sa1_character_name(kind);
    return n != NULL ? n : "this kind";
}

/* checks shared by sa1_preview_add and sa1_preview_ring_places: the kind and the disc */
static sa1_status kind_check(sa1_context *ctx, int32_t kind)
{
    if (kind < 0 || kind >= SA1_CHAR_COUNT) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "previews: kind %d (SA1_CHAR_*)", (int)kind);
    }
    if (!pv_kind_has_showcase(kind)) {
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "previews: %s has no showcase (Sonic, Tails, Knuckles, Amy, E-102, Big)",
                        kind_name(kind));
    }
    if (!ctx->disc_loaded || !sa1_api_ext_of(ctx)->gdi[0]) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "previews: no disc loaded (a showcase is built from the disc)");
    }
    if (!ctx->chars[kind].loaded) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "previews: %s was not loaded from the disc (sa1_load_options.characters)",
                        kind_name(kind));
    }
    if (!sa1w_ready()) {
        return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "previews need the lowered game code (not in this build)");
    }
    return SA1_OK;
}

/* an error recorded without a log line (sa1_last_error): calls a host makes every frame (render, info, add on a faulted kind)
 * must not flood its log; the fault itself was logged once by the pair that met it */
static sa1_status quiet(sa1_context *ctx, sa1_status st, const char *what, const char *detail)
{
    snprintf(ctx->error, sizeof ctx->error, "%s%s", what, detail);
    return st;
}

/* each slot's host position (sa1_to_host_point in double; the context's coordinates at this pair) */
static void show_hslots(const sa1_context *ctx, pv_show *S)
{
    const float *a = ctx->coords.axis;
    double sc = (double)ctx->coords.scale;
    uint32_t k;
    for (k = 0; k < SA1PV_SLOTS; k++) {
        float s[3];
        sa1pv_slot_pos(k, s);
        S->hslot[k][0] = sc * ((double)a[0] * s[0] + (double)a[1] * s[1] + (double)a[2] * s[2]);
        S->hslot[k][1] = sc * ((double)a[3] * s[0] + (double)a[4] * s[1] + (double)a[5] * s[2]);
        S->hslot[k][2] = sc * ((double)a[6] * s[0] + (double)a[7] * s[1] + (double)a[8] * s[2]);
    }
}

static sa1_status show_get(sa1_context *ctx, pv_reg *R, int32_t kind, pv_show **out)
{
    pv_show *S = R->show[kind];
    sa1_capture_stats cst;
    const uint8_t *h;
    sa1_status st;
    *out = S;
    if (S != NULL) {
        if (S->dead) {
            return quiet(ctx, S->dead_status, "previews: the kind's showcase faulted (sa1_preview_clear, or remove its "
                         "copies): ", S->fault);
        }
        return SA1_OK;
    }
    S = (pv_show *)sa1_mem_alloc(ctx, sizeof *S);
    if (S == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    S->kind = kind;
    S->cs = &ctx->chars[kind];
    st = sa1wp_create(ctx, kind, &S->w);
    if (st == SA1_OK) {
        st = sa1_capture_attach(ctx, S->w, &S->cap);
        if (st != SA1_OK) {
            st = sa1_fail(ctx, st == SA1_ERR_NOT_FOUND ? SA1_ERR_NOT_IMPLEMENTED : st,
                          "previews: the showcase world has no display capture block");
        }
    }
    if (st == SA1_OK) {
        sa1_capture_set_archives(S->cap, S->cs);
        sa1_capture_reserve(ctx, S->cap, 16384u, 16384u, 1024u);
        if (sa1_meshbuf_reserve(ctx, &S->mb, 16384u, 16384u, 1024u) != SA1_OK ||
            sa1_meshbuf_reserve_extra(ctx, &S->mb, 1024u, 256u, 256u) != SA1_OK ||
            sa1_meshbuf_reserve_scratch(ctx, &S->mb, 4096u, 2048u) != SA1_OK ||
            sa1_meshbuf_reserve_exec(ctx, &S->mb) != SA1_OK) {
            st = SA1_ERR_OUT_OF_MEMORY;
        }
    }
    if (st != SA1_OK) {
        show_free(ctx, S);
        return st;
    }
    /* the capture's records per pair (its header: 1,024 since round 0.9.4b, 512 before) */
    sa1_capture_get_stats(S->cap, &cst);
    h = sa1_capture_read(S->cap, cst.block, (uint32_t)sizeof(sa1_cap_hdr));
    S->cap_capacity = h != NULL ? sa1_rd32(h + offsetof(sa1_cap_hdr, capacity)) : SA1_CAP_MAX;
    show_hslots(ctx, S);
    R->show[kind] = S;
    *out = S;
    return SA1_OK;
}

/* ---- the description (task 3) --------------------------------------------------------------------------------------- */
/* the farthest ring centre of a group from its record (SA1 units): a line's ring i lies at spacing * ceil(i / 2) (odd i)
 * or spacing * i / 2 (even i) along its axis (8C087EEE), a circle's rings at the radius (8C088000) */
static float group_extent(const float p[3])
{
    int32_t n = (int32_t)p[0];
    if (p[2] == 1.0f) {
        return fabsf(p[1]);
    }
    return fabsf(p[1]) * (float)((n + 1) / 2);
}

/* a host rotation: finite, all zero (none) or R R^T = I within 0.01 (as sa1_set_add); *zero = 1 for none */
static sa1_status rot_check(sa1_context *ctx, const float rot[9], uint32_t *zero)
{
    uint32_t i, j;
    *zero = 1;
    for (i = 0; i < 9; i++) {
        if (!pv_finite(rot[i])) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "previews: rotation[%u] is not finite", (unsigned)i);
        }
        *zero &= rot[i] == 0.0f;
    }
    if (*zero) {
        return SA1_OK;
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            const float *a = &rot[3 * i], *b = &rot[3 * j];
            float e = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] - (i == j ? 1.0f : 0.0f);
            if (fabsf(e) > 0.01f) {
                return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "previews: rotation is not a rotation matrix (rows %u / %u: "
                                "%g)", (unsigned)i, (unsigned)j, (double)e);
            }
        }
    }
    return SA1_OK;
}

/* the O_GRING params (as sa1_set_add) */
static sa1_status gring_check(sa1_context *ctx, const float p[3])
{
    if (!pv_is_int(p[0], 0.0f, 15.0f) || !(p[2] == 0.0f || p[2] == 1.0f) || !pv_finite(p[1]) || fabsf(p[1]) > 1000.0f) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "O_GRING: params (%g, %g, %g): an integer count 0..15, a spacing / "
                        "radius within 1000 SA1 units, 0 (line) or 1 (circle)", (double)p[0], (double)p[1], (double)p[2]);
    }
    return SA1_OK;
}

static sa1_status desc_take(sa1_context *ctx, const sa1_preview_desc *in, sa1_preview_desc *d, float *extent)
{
    const sa1set_type *t;
    sa1_status st;
    uint32_t i, zero = 1;
    if (in == NULL || in->struct_size < offsetof(sa1_preview_desc, user_bits) + sizeof(uint32_t)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_preview_desc: struct_size %u (at least %u)",
                        in != NULL ? (unsigned)in->struct_size : 0u,
                        (unsigned)(offsetof(sa1_preview_desc, user_bits) + sizeof(uint32_t)));
    }
    memset(d, 0, sizeof *d);
    memcpy(d, in, in->struct_size < sizeof *d ? in->struct_size : sizeof *d);
    d->struct_size = sizeof *d;
    if (d->type == SA1_PREVIEW_BOARD) {
        /* round 0.9.5 (SA1_PREVIEW_BOARD): kind 0 (params[0] 0 MIW_B001 / 1 SB_BOARD1) or 2 (params[0] 0), no rotation (the
           host applies a kept board's pose), PRESENT only */
        if (SA1WB_GATED) {
            return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, SA1WB_NOT_BUILT);
        }
        if (d->kind != SA1_CHAR_SONIC && d->kind != SA1_CHAR_TAILS) {
            return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "SA1_PREVIEW_BOARD: only Sonic's (kind 0) and Tails' "
                              "(kind 2) boards");
        }
        for (i = 0; i < 9; i++) {
            zero &= d->rotation[i] == 0.0f;
        }
        if (!zero || d->params[1] != 0.0f || d->params[2] != 0.0f ||
            !(d->params[0] == 0.0f || (d->params[0] == 1.0f && d->kind == SA1_CHAR_SONIC)) ||
            d->state != SA1_SET_STATE_PRESENT || d->user_bits != 0) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_preview_desc: SA1_PREVIEW_BOARD takes params[0] = the variant "
                            "(Sonic 0 MIW_B001 / 1 SB_BOARD1, Tails 0), no rotation, state SA1_SET_STATE_PRESENT");
        }
        *extent = 20.0f;
        return SA1_OK;
    }
    t = sa1set_type_of(d->type);
    if (t == NULL) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_preview_desc: unknown type %u (1..%u)", (unsigned)d->type,
                        (unsigned)SA1SET_NTYPES);
    }
    for (i = 0; i < 3; i++) {
        if (!pv_finite(d->params[i]) || fabsf(d->params[i]) > 1000000.0f) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_preview_desc: params[%u] %g", (unsigned)i,
                            (double)d->params[i]);
        }
    }
    st = rot_check(ctx, d->rotation, &zero);
    if (st != SA1_OK) {
        return st;
    }
    if (!zero && k_tinfo[d->type - 1u].rotation != SA1_PREVIEW_ROT_INNER) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_preview_desc: %s takes no rotation (the host applies it: "
                        "sa1_preview_type.rotation)", t->name);
    }
    if (!(d->state & SA1_SET_STATE_PRESENT) || (d->state & ~(SA1_SET_STATE_PRESENT | SA1_SET_STATE_BROKEN)) ||
        ((d->state & SA1_SET_STATE_BROKEN) && d->type != SA1_OBJTYPE_ITEMBOX)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_preview_desc: state 0x%X (SA1_SET_STATE_PRESENT, | BROKEN for "
                        "ITEMBOX)", (unsigned)d->state);
    }
    if (d->user_bits > 0xFFu || (d->user_bits && d->type != SA1_OBJTYPE_O_GRING)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_preview_desc: user_bits 0x%X (O_GRING only, 0..0xFF)",
                        (unsigned)d->user_bits);
    }
    *extent = 20.0f;
    switch (d->type) {
    case SA1_OBJTYPE_RING:
        if (d->params[0] != 0.0f || d->params[1] != 0.0f || d->params[2] != 0.0f) {
            /* review R.20: the ground-following ring (-1) reads camera_twp and may make a dropped ring with rand() */
            return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "sa1_preview_desc: RING params (%g, %g, %g): only (0, 0, 0) in a "
                            "showcase", (double)d->params[0], (double)d->params[1], (double)d->params[2]);
        }
        break;
    case SA1_OBJTYPE_O_GRING:
        st = gring_check(ctx, d->params);
        if (st != SA1_OK) {
            return st;
        }
        *extent = group_extent(d->params);
        if (*extent + 10.0f > SA1PV_REACH) {
            return sa1_fail(ctx, SA1_ERR_LIMIT, "O_GRING: the layout (%g, %g, %g) reaches %g SA1 units from its centre: a "
                            "showcase copy must stay within %g of its slot", (double)d->params[0], (double)d->params[1],
                            (double)d->params[2], (double)(*extent + 10.0f), (double)SA1PV_REACH);
        }
        break;
    case SA1_OBJTYPE_ITEMBOX:
    case SA1_OBJTYPE_ITEMBOX_AIR:
        if (!pv_is_int(d->params[0], 0.0f, 8.0f)) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: contents %g (an integer 0..8)", t->name,
                            (double)d->params[0]);
        }
        break;
    default:
        break;
    }
    return SA1_OK;
}

/* capture records a copy is expected to take per pair (task 3) */
static uint32_t desc_records(const sa1_preview_desc *d)
{
    if (d->type == SA1_PREVIEW_BOARD) {
        return 2u;                  /* round 0.9.5: one njDrawModel (a record per material run kept: 2) */
    }
    if (d->type == SA1_OBJTYPE_O_GRING) {
        uint32_t n = (uint32_t)(int32_t)d->params[0];
        return d->params[2] == 1.0f ? n : n + 1u;
    }
    return k_tinfo[d->type - 1u].records;
}

/* the eye's distance in front of the slot: every ring centre's 20-unit sphere inside the game's 640 x 480 screen (its
 * narrowest half view, +-35 degrees sideways: d >= 1.744 extent + 9) and before its far plane (3,000 + 20; extent <= 990:
 * d + extent <= 2,872) - review R.20 asked for at least extent / tan 30 degrees = 1.732 extent */
static float view_dist(float extent)
{
    float d = 1.85f * extent + 50.0f;
    return d > SA1PV_VIEW_MIN ? d : SA1PV_VIEW_MIN;
}

/* ---- the API ---------------------------------------------------------------------------------------------------------- */
sa1_status sa1_preview_add(sa1_context *ctx, const sa1_preview_desc *in, uint32_t *out_id)
{
    sa1_preview_desc d;
    sa1pv_wcopy wcp;
    pv_reg *R;
    pv_show *S;
    pv_copy *c;
    float extent = 0.0f;
    uint32_t i, idx, slot, records, tp = 0, serial;
    sa1_status st;
    if (out_id != NULL) {
        *out_id = 0;
    }
    if (ctx == NULL || out_id == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = desc_take(ctx, in, &d, &extent);
    if (st != SA1_OK) {
        return st;
    }
    st = kind_check(ctx, d.kind);
    if (st != SA1_OK) {
        return st;
    }
    if (d.type != SA1_PREVIEW_BOARD && !sa1w_set_exec_native(k_tinfo[d.type - 1u].exec)) {
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "sa1_preview_desc: type %u: its code (%08X) is not in this build",
                        (unsigned)d.type, (unsigned)k_tinfo[d.type - 1u].exec);
    }
    st = reg_get(ctx, &R);
    if (st != SA1_OK) {
        return st;
    }
    st = show_get(ctx, R, d.kind, &S);
    if (st != SA1_OK) {
        return st;
    }
    if (S->copies >= SA1_PREVIEW_MAX) {
        return sa1_fail(ctx, SA1_ERR_LIMIT, "previews: %u copies of %s (SA1_PREVIEW_MAX)", (unsigned)S->copies,
                        kind_name(d.kind));
    }
    records = desc_records(&d);
    if (S->records_est + records + SA1PV_CAP_SPARE > S->cap_capacity) {
        return sa1_fail(ctx, SA1_ERR_LIMIT, "previews: the showcase of %s would draw %u capture records a pair (capacity "
                        "%u, %u kept free)", kind_name(d.kind), (unsigned)(S->records_est + records),
                        (unsigned)S->cap_capacity, (unsigned)SA1PV_CAP_SPARE);
    }
    if (sa1wp_heap_largest(S->w) < SA1PV_HEADROOM) {
        return sa1_fail(ctx, SA1_ERR_LIMIT, "previews: the showcase of %s has no %u-byte heap block left",
                        kind_name(d.kind), (unsigned)SA1PV_HEADROOM);
    }
    /* the free slot nearest the origin (the lowest index among equals): the game's own float arithmetic at a slot (a group
       places its rings at slot + offset) keeps half an ulp of the slot's coordinates, 0.00006 units at the 4 middle slots
       and 0.0005 at the corners */
    slot = SA1PV_SLOTS;
    {
        uint32_t k, bd = 0xFFFFFFFFu;
        for (k = 0; k < SA1PV_SLOTS; k++) {
            int32_t gx = 2 * (int32_t)(k % SA1PV_GRID) - 15, gz = 2 * (int32_t)(k / SA1PV_GRID) - 15;
            uint32_t dd = (uint32_t)(gx * gx + gz * gz);
            if (S->slot_state[k] == PV_FREE && dd < bd) {
                bd = dd;
                slot = k;
            }
        }
    }
    for (idx = 0; idx < PV_INDEX && R->copy[idx] != NULL; idx++) {
    }
    if (slot == SA1PV_SLOTS || idx == PV_INDEX) {
        return sa1_fail(ctx, SA1_ERR_LIMIT, "previews: no free slot in the showcase of %s (removed copies are released "
                        "at its next pair)", kind_name(d.kind));
    }
    c = (pv_copy *)sa1_mem_alloc(ctx, sizeof *c);
    if (c == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    memset(&wcp, 0, sizeof wcp);
    wcp.type = d.type;
    wcp.slot = slot;
    memcpy(wcp.params, d.params, sizeof wcp.params);
    memcpy(wcp.rotation, d.rotation, sizeof wcp.rotation);
    wcp.cond = 0x8001u | ((d.state & SA1_SET_STATE_BROKEN) ? 0x0004u : 0u);
    wcp.bits = d.user_bits;
    sa1wp_set_view(S->w, slot, view_dist(extent));
    st = sa1wp_add(S->w, &wcp, &tp);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, c);
        if (st == SA1_ERR_LIMIT) {
            return sa1_fail(ctx, st, "previews: the task system of the showcase of %s is full", kind_name(d.kind));
        }
        if (S->w != NULL && sa1wp_fault(S->w)[0]) {
            S->dead = 1;
            S->dead_status = st;
            snprintf(S->fault, sizeof S->fault, "%s", sa1wp_fault(S->w));
            return sa1_fail(ctx, st, "previews: the showcase of %s faulted: %s", kind_name(d.kind), S->fault);
        }
        return st;
    }
    do {
        serial = (uint32_t)sa1_atomic_add32(&g_serial, 1) & 0xFFFFFu;
    } while (serial == 0);
    c->id = (serial << 12) | (idx + 1u);
    c->kind = d.kind;
    c->type = d.type;
    c->slot = slot;
    c->tp = tp;
    c->records = records;
    c->fill = -1;
    R->copy[idx] = c;
    if (idx + 1u > R->hiwater) {
        R->hiwater = idx + 1u;
    }
    S->slot_state[slot] = PV_LIVE;
    S->slot_copy[slot] = (uint16_t)(idx + 1u);
    S->copies++;
    S->records_est += records;
    S->idle = 0;
    for (i = 0; i < 2; i++) {
        c->fr[i].src = 0xFFFFFFFFu;
    }
    *out_id = c->id;
    return SA1_OK;
}

sa1_status sa1_preview_remove(sa1_context *ctx, uint32_t id)
{
    pv_reg *R;
    pv_copy *c;
    pv_show *S;
    uint32_t idx;
    int32_t kind;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    R = reg_of(ctx);
    c = copy_of(R, id);
    if (c == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    idx = (id & 0xFFFu) - 1u;
    kind = c->kind;
    S = R->show[kind];
    if (S != NULL) {
        S->copies--;
        S->records_est -= c->records;
        S->slot_copy[c->slot] = 0;
        if (S->dead) {
            S->slot_state[c->slot] = PV_FREE;
        } else {
            sa1_status st = sa1wp_remove(S->w, c->slot, c->tp);
            S->slot_state[c->slot] = PV_LEAVING;
            S->leaving++;
            if (st != SA1_OK) {
                S->dead = 1;
                S->dead_status = st;
                snprintf(S->fault, sizeof S->fault, "%s", sa1wp_fault(S->w));
            }
        }
    }
    copy_drop(ctx, R, idx);
    if (S != NULL && S->dead && S->copies == 0) {
        /* every copy of a faulted showcase removed: the next add makes a new one */
        show_free(ctx, S);
        R->show[kind] = NULL;
    }
    return SA1_OK;
}

sa1_status sa1_preview_clear(sa1_context *ctx)
{
    pv_reg *R;
    int32_t k;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    R = reg_of(ctx);
    if (R == NULL) {
        return SA1_OK;
    }
    for (k = 0; k < SA1_CHAR_COUNT; k++) {
        kind_clear(ctx, R, k);
    }
    R->acc = 0.0;
    return SA1_OK;
}

/* ---- the split (task 5) ------------------------------------------------------------------------------------------------ */
static pv_copy *copy_at(pv_reg *R, const pv_show *S, uint32_t slot)
{
    uint32_t i;
    if (slot >= SA1PV_SLOTS || S->slot_state[slot] != PV_LIVE || S->slot_copy[slot] == 0) {
        return NULL;
    }
    i = S->slot_copy[slot] - 1u;
    return i < PV_INDEX ? R->copy[i] : NULL;
}

static uint32_t host_slot_of(const sa1_context *ctx, const float h[3])
{
    float s[3];
    sa1_from_host_point(ctx, h, s);
    return sa1pv_slot_of((double)s[0], (double)s[2]);
}

/* the copy's frame being filled, with a draw for source draw i appended when its last draw is not i's */
static sa1_draw *frame_draw(const sa1_context *ctx, pv_frame *f, const sa1_draw *src, uint32_t i, uint32_t first)
{
    sa1_draw *d;
    if (f->src == i && f->nd > 0) {
        return &f->draws[f->nd - 1u];
    }
    if (!grow(ctx, (void **)&f->draws, &f->cap_d, f->nd + 1u, f->nd, sizeof(sa1_draw))) {
        return NULL;
    }
    d = &f->draws[f->nd++];
    *d = *src;
    d->first = first;
    d->count = 0;
    f->src = i;
    return d;
}

/* a new zeroed array of n items holding the first `used` items of old (old is not freed) */
static void *arr_copy(const sa1_context *ctx, const void *old, uint32_t used, uint32_t n, size_t elem)
{
    void *q = sa1_mem_array(ctx, n, elem);
    if (q != NULL && old != NULL && used) {
        memcpy(q, old, (size_t)used * elem);
    }
    return q;
}

/* room for `need` triangles in a frame (positions, normals, UVs, colours grow together) */
static int frame_room_tris(const sa1_context *ctx, pv_frame *f, uint32_t need)
{
    uint32_t c;
    float *p, *n, *u;
    uint8_t *k;
    if (need <= f->cap_t) {
        return 1;
    }
    c = f->cap_t ? f->cap_t : 128u;
    while (c < need) {
        c *= 2u;
    }
    p = (float *)arr_copy(ctx, f->pos, 9u * f->nt, 9u * c, sizeof(float));
    n = (float *)arr_copy(ctx, f->nrm, 9u * f->nt, 9u * c, sizeof(float));
    u = (float *)arr_copy(ctx, f->uv, 6u * f->nt, 6u * c, sizeof(float));
    k = (uint8_t *)arr_copy(ctx, f->col, 12u * f->nt, 12u * c, 1);
    if (p == NULL || n == NULL || u == NULL || k == NULL) {
        sa1_mem_free(ctx, p);
        sa1_mem_free(ctx, n);
        sa1_mem_free(ctx, u);
        sa1_mem_free(ctx, k);
        return 0;
    }
    sa1_mem_free(ctx, f->pos);
    sa1_mem_free(ctx, f->nrm);
    sa1_mem_free(ctx, f->uv);
    sa1_mem_free(ctx, f->col);
    f->pos = p;
    f->nrm = n;
    f->uv = u;
    f->col = k;
    f->cap_t = c;
    return 1;
}

static void frame_bounds(pv_frame *f)
{
    uint32_t i, k, any = 0;
    float lo[3] = { 0.0f, 0.0f, 0.0f }, hi[3] = { 0.0f, 0.0f, 0.0f };
#define PV_BOUND(p)                                                                                                     \
    do {                                                                                                                \
        for (k = 0; k < 3; k++) {                                                                                       \
            if (!any || (p)[k] < lo[k]) {                                                                               \
                lo[k] = (p)[k];                                                                                         \
            }                                                                                                           \
            if (!any || (p)[k] > hi[k]) {                                                                               \
                hi[k] = (p)[k];                                                                                         \
            }                                                                                                           \
        }                                                                                                               \
        any = 1;                                                                                                        \
    } while (0)
    for (i = 0; i < 3u * f->nt; i++) {
        PV_BOUND(&f->pos[3u * i]);
    }
    for (i = 0; i < f->ns; i++) {
        PV_BOUND(f->spr[i].position);
    }
    for (i = 0; i < f->nl; i++) {
        PV_BOUND(f->lv[i].position);
    }
#undef PV_BOUND
    for (k = 0; k < 3; k++) {
        f->bounds[k] = lo[k];
        f->bounds[3 + k] = hi[k];
    }
}

/* a vertex in object space (host units and axes, origin = slot sl): a drawn model's vertex from its model-local point and
 * its model -> world matrix in double (sa1_pose.c's emitter rounds m * p to float in SA1 world space first, which far from
 * the origin - the grid's corners lie 15,360 units out - costs half a float ulp, 0.0005 units: review R.1), minus the slot
 * in SA1 space, then the host's scale and axes; any other vertex (a 3D polygon's) from its host position, minus the slot's
 * host position, in double */
static void obj_point(const sa1_context *ctx, const sa1_meshbuf *b, const pv_show *S, uint32_t sl, uint32_t v, float o[3])
{
    const float *a = ctx->coords.axis;
    double sc = (double)ctx->coords.scale, d[3];
    float s[3];
    int32_t n;
    uint32_t k;
    if (v >= b->nv) {
        o[0] = o[1] = o[2] = 0.0f;
        return;
    }
    n = S->vnode[v];
    if (n < 0) {
        for (k = 0; k < 3; k++) {
            o[k] = (float)((double)b->pos[3u * v + k] - S->hslot[sl][k]);
        }
        return;
    }
    {
        const double *m = &b->node_wm[12u * (uint32_t)n];
        const float *lp = &b->lpos[3u * v];
        double x = lp[0], y = lp[1], z = lp[2];
        sa1pv_slot_pos(sl, s);
        d[0] = m[0] * x + m[1] * y + m[2] * z + m[3] - (double)s[0];
        d[1] = m[4] * x + m[5] * y + m[6] * z + m[7] - (double)s[1];
        d[2] = m[8] * x + m[9] * y + m[10] * z + m[11] - (double)s[2];
    }
    for (k = 0; k < 3; k++) {
        o[k] = (float)(sc * ((double)a[3u * k] * d[0] + (double)a[3u * k + 1u] * d[1] + (double)a[3u * k + 2u] * d[2]));
    }
}

static sa1_status split(sa1_context *ctx, pv_reg *R, pv_show *S)
{
    const sa1_meshbuf *b = &S->mb;
    uint32_t i, k, v, t;
    int oom = 0;
    /* the frames this pair fills: every live copy's other frame */
    for (i = 0; i < R->hiwater; i++) {
        pv_copy *c = R->copy[i];
        pv_frame *f;
        if (c == NULL || c->kind != S->kind) {
            continue;
        }
        c->fill = c->nfr == 0 ? 0 : (c->cur ^ 1);
        f = &c->fr[c->fill];
        f->nt = f->nd = f->ns = f->nl = 0;
        f->src = 0xFFFFFFFFu;
    }
    /* the slot of every vertex: its drawn model's, else its own position's */
    if (!grow(ctx, (void **)&S->vslot, &S->cap_vslot, b->nv + 1u, 0, sizeof(int16_t)) ||
        !grow(ctx, (void **)&S->vnode, &S->cap_vnode, b->nv + 1u, 0, sizeof(int32_t))) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    for (v = 0; v < b->nv; v++) {
        S->vslot[v] = -1;
        S->vnode[v] = -1;
    }
    for (k = 0; k < b->nn && b->node_v != NULL && b->node_wm != NULL; k++) {
        uint32_t first = b->node_v[2u * k], n = b->node_v[2u * k + 1u];
        int16_t sl = (int16_t)sa1pv_slot_of(b->node_wm[12u * k + 3u], b->node_wm[12u * k + 11u]);
        for (v = first; v < first + n && v < b->nv; v++) {
            S->vslot[v] = sl;
            S->vnode[v] = b->lpos != NULL ? (int32_t)k : -1;
        }
    }
    for (v = 0; v < b->nv; v++) {
        if (S->vslot[v] < 0) {
            S->vslot[v] = (int16_t)host_slot_of(ctx, &b->pos[3u * v]);
        }
    }
    /* the draws in the game's order */
    for (i = 0; i < b->ndraw && !oom; i++) {
        const sa1_draw *src = &b->draws[i];
        if (src->prim == SA1_PRIM_TRIANGLES) {
            for (t = src->first; t < src->first + src->count && t < b->nt; t++) {
                uint32_t v0 = b->idx[3u * t], sl = v0 < b->nv ? (uint32_t)S->vslot[v0] : SA1PV_SLOTS;
                pv_copy *c = copy_at(R, S, sl);
                pv_frame *f;
                sa1_draw *d;
                if (c == NULL || c->fill < 0) {
                    continue;
                }
                f = &c->fr[c->fill];
                d = frame_draw(ctx, f, src, i, f->nt);
                if (d == NULL || !frame_room_tris(ctx, f, f->nt + 1u)) {
                    oom = 1;
                    break;
                }
                for (k = 0; k < 3; k++) {
                    uint32_t vi = b->idx[3u * t + k], a;
                    obj_point(ctx, b, S, sl, vi, &f->pos[9u * f->nt + 3u * k]);
                    for (a = 0; a < 3; a++) {
                        f->nrm[9u * f->nt + 3u * k + a] = vi < b->nv ? b->nrm[3u * vi + a] : 0.0f;
                    }
                }
                memcpy(&f->uv[6u * f->nt], &b->uv[6u * t], 6u * sizeof(float));
                memcpy(&f->col[12u * f->nt], &b->col[12u * t], 12u);
                f->nt++;
                d->count++;
            }
        } else if (src->prim == SA1_PRIM_SPRITES) {
            for (t = src->first; t < src->first + src->count && t < b->nspr; t++) {
                uint32_t sl = host_slot_of(ctx, b->spr[t].position), a;
                pv_copy *c = copy_at(R, S, sl);
                pv_frame *f;
                sa1_draw *d;
                if (c == NULL || c->fill < 0) {
                    continue;
                }
                f = &c->fr[c->fill];
                d = frame_draw(ctx, f, src, i, f->ns);
                if (d == NULL || !grow(ctx, (void **)&f->spr, &f->cap_s, f->ns + 1u, f->ns, sizeof(sa1_sprite))) {
                    oom = 1;
                    break;
                }
                f->spr[f->ns] = b->spr[t];
                for (a = 0; a < 3; a++) {
                    f->spr[f->ns].position[a] = (float)((double)b->spr[t].position[a] - S->hslot[sl][a]);
                }
                f->ns++;
                d->count++;
            }
        } else if (src->prim == SA1_PRIM_LINES) {
            for (t = src->first; t < src->first + src->count && 2u * t + 1u < b->nlv; t++) {
                uint32_t sl = host_slot_of(ctx, b->lv[2u * t].position), a, e;
                pv_copy *c = copy_at(R, S, sl);
                pv_frame *f;
                sa1_draw *d;
                if (c == NULL || c->fill < 0) {
                    continue;
                }
                f = &c->fr[c->fill];
                d = frame_draw(ctx, f, src, i, f->nl / 2u);
                if (d == NULL || !grow(ctx, (void **)&f->lv, &f->cap_l, f->nl + 2u, f->nl, sizeof(sa1_line_vertex))) {
                    oom = 1;
                    break;
                }
                for (e = 0; e < 2; e++) {
                    f->lv[f->nl] = b->lv[2u * t + e];
                    for (a = 0; a < 3; a++) {
                        f->lv[f->nl].position[a] = (float)((double)b->lv[2u * t + e].position[a] - S->hslot[sl][a]);
                    }
                    f->nl++;
                }
                d->count++;
            }
        }
    }
    /* the filled frames become the last pair */
    for (i = 0; i < R->hiwater; i++) {
        pv_copy *c = R->copy[i];
        if (c == NULL || c->kind != S->kind || c->fill < 0) {
            continue;
        }
        frame_bounds(&c->fr[c->fill]);
        c->cur = c->fill;
        c->fill = -1;
        if (c->nfr < 2u) {
            c->nfr++;
        }
    }
    return oom ? SA1_ERR_OUT_OF_MEMORY : SA1_OK;
}

/* ---- the pair of a showcase (task 5) -------------------------------------------------------------------------------- */
static sa1_status show_pair(sa1_context *ctx, pv_reg *R, pv_show *S)
{
    sa1_capture_emit_opts o;
    sa1_geometry g;
    sa1_status st;
    uint32_t i, k;
    sa1_capture_open(S->cap);
    st = sa1wp_pair(S->w);
    sa1_capture_close(S->cap);
    if (st != SA1_OK) {
        S->dead = 1;
        S->dead_status = st;
        snprintf(S->fault, sizeof S->fault, "%s", sa1wp_fault(S->w));
        return sa1_fail(ctx, st, "previews: the showcase of %s faulted: %s", kind_name(S->kind), S->fault);
    }
    S->pairs++;
    for (i = 0; i < R->hiwater; i++) {
        pv_copy *c = R->copy[i];
        if (c != NULL && c->kind == S->kind) {
            c->pairs++;
            c->steps += 2u;
        }
    }
    memset(&o, 0, sizeof o);
    o.texlist0 = S->cs->num_archives ? S->cs->archives[0].texlist_addr : 0u;
    o.shadow_model = sa1_capture_shadow_model(S->cap);
    o.clip = 0;                         /* review R.1: the copies lie up to 21,723 units from the origin */
    o.cs = S->cs;
    o.effects = 1;
    o.dedupe = 1;
    o.exec_arrays = 1;
    st = sa1_capture_emit_into(ctx, S->cap, &o, &S->mb, &g);
    if (st == SA1_OK) {
        show_hslots(ctx, S);
        st = split(ctx, R, S);
    }
    /* removed copies: their slot is released once their task has ended (DestroyTask ran at this pair's first exec) */
    for (k = 0; k < SA1PV_SLOTS; k++) {
        if (S->slot_state[k] == PV_LEAVING && !sa1wp_slot_busy(S->w, k)) {
            sa1wp_release(S->w, k);
            S->slot_state[k] = PV_FREE;
            S->leaving--;
        }
    }
    return st;
}

sa1_status sa1_preview_tick(sa1_context *ctx, float dt)
{
    sa1_status st, first = SA1_OK;
    pv_reg *R;
    uint32_t n = 0, p;
    int32_t k;
    if (ctx == NULL || !(dt >= 0.0f) || dt > 3600.0f) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    R = reg_of(ctx);
    if (R == NULL) {
        return SA1_OK;
    }
    /* the character worlds' accumulator rules (sa1_world_tick.c sa1g_world_tick): whole pairs, at most 8, the rest dropped */
    R->acc += (double)dt;
    while (R->acc + 1e-9 >= PV_PAIR && n < PV_MAX_PAIRS) {
        R->acc -= PV_PAIR;
        n++;
    }
    if (R->acc + 1e-9 >= PV_PAIR) {
        uint32_t lost = (uint32_t)((R->acc + 1e-9) / PV_PAIR);
        R->acc -= (double)lost * PV_PAIR;
    }
    if (R->acc < 0.0) {
        R->acc = 0.0;
    }
    for (p = 0; p < n; p++) {
        for (k = 0; k < SA1_CHAR_COUNT; k++) {
            pv_show *S = R->show[k];
            if (S == NULL || S->dead) {
                continue;
            }
            if (S->copies != 0 || S->leaving != 0) {
                S->idle = 0;
                st = show_pair(ctx, R, S);
                if (st != SA1_OK && first == SA1_OK) {
                    first = st;
                }
            } else if (++S->idle >= SA1_PREVIEW_IDLE_PAIRS) {
                show_free(ctx, S);
                R->show[k] = NULL;
            }
        }
    }
    return first;
}

/* ---- the output -------------------------------------------------------------------------------------------------------- */
static int same_draws(const pv_frame *a, const pv_frame *b)
{
    uint32_t i;
    if (a->nt != b->nt || a->nd != b->nd || a->ns != b->ns || a->nl != b->nl) {
        return 0;
    }
    for (i = 0; i < a->nd; i++) {
        const sa1_draw *x = &a->draws[i], *y = &b->draws[i];
        if (x->prim != y->prim || x->first != y->first || x->count != y->count || x->archive != y->archive ||
            x->texture != y->texture || x->flags != y->flags) {
            return 0;
        }
    }
    return 1;
}

static void lerp3f(const float *a, const float *b, float t, float *o)
{
    o[0] = a[0] + (b[0] - a[0]) * t;
    o[1] = a[1] + (b[1] - a[1]) * t;
    o[2] = a[2] + (b[2] - a[2]) * t;
}

static void frame_out(sa1_render *r, const pv_frame *f)
{
    r->num_triangles = f->nt;
    r->positions = f->pos;
    r->normals = f->nrm;
    r->uvs = f->uv;
    r->colors = f->col;
    r->num_draws = f->nd;
    r->draws = f->draws;
    r->num_sprites = f->ns;
    r->sprites = f->spr;
    r->num_line_vertices = f->nl;
    r->line_vertices = f->lv;
}

sa1_status sa1_preview_render(sa1_context *ctx, uint32_t id, float alpha, const sa1_render **out)
{
    pv_reg *R;
    pv_copy *c;
    pv_show *S;
    sa1_render *r;
    const pv_frame *cur, *prev;
    uint32_t i;
    if (out != NULL) {
        *out = NULL;
    }
    if (ctx == NULL || out == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    R = reg_of(ctx);
    c = copy_of(R, id);
    if (c == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    S = R->show[c->kind];
    if (S == NULL || S->dead) {
        return quiet(ctx, S != NULL ? S->dead_status : SA1_ERR_GAME_FAULT, "previews: the copy's showcase faulted: ",
                     S != NULL ? S->fault : "");
    }
    if (!(alpha == alpha)) {
        alpha = 1.0f;
    }
    if (alpha < 0.0f) {
        alpha = (float)(R->acc * 30.0);
    }
    alpha = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
    r = &c->r;
    memset(r, 0, sizeof *r);
    r->struct_size = sizeof *r;
    r->generation = c->pairs;
    r->alpha = 1.0f;
    *out = r;
    if (c->nfr == 0) {
        return SA1_OK;                  /* not drawn yet: no draws */
    }
    cur = &c->fr[c->cur];
    prev = c->nfr > 1u ? &c->fr[c->cur ^ 1] : NULL;
    if (prev == NULL || alpha >= 1.0f || !same_draws(prev, cur)) {
        frame_out(r, cur);
        return SA1_OK;
    }
    r->alpha = alpha;
    r->interpolated = 1;
    if (alpha <= 0.0f) {
        frame_out(r, prev);             /* the pair before, exactly */
        return SA1_OK;
    }
    if (c->cap_o < cur->nt + 1u) {
        uint32_t nc = cur->nt + 64u;
        float *p = (float *)sa1_mem_array(ctx, 9u * nc, sizeof(float)), *n = (float *)sa1_mem_array(ctx, 9u * nc,
                                                                                                       sizeof(float));
        if (p == NULL || n == NULL) {
            sa1_mem_free(ctx, p);
            sa1_mem_free(ctx, n);
            return SA1_ERR_OUT_OF_MEMORY;
        }
        sa1_mem_free(ctx, c->opos);
        sa1_mem_free(ctx, c->onrm);
        c->opos = p;
        c->onrm = n;
        c->cap_o = nc;              /* triangles */
    }
    if (!grow(ctx, (void **)&c->ospr, &c->cap_os, cur->ns + 1u, 0, sizeof(sa1_sprite)) ||
        !grow(ctx, (void **)&c->olv, &c->cap_ol, cur->nl + 1u, 0, sizeof(sa1_line_vertex))) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    for (i = 0; i < 3u * cur->nt; i++) {
        float *n = &c->onrm[3u * i];
        float l;
        lerp3f(&prev->pos[3u * i], &cur->pos[3u * i], alpha, &c->opos[3u * i]);
        lerp3f(&prev->nrm[3u * i], &cur->nrm[3u * i], alpha, n);
        l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 1e-12f) {
            n[0] /= l;
            n[1] /= l;
            n[2] /= l;
        } else {
            memcpy(n, &cur->nrm[3u * i], 3u * sizeof(float));
        }
    }
    for (i = 0; i < cur->ns; i++) {
        c->ospr[i] = cur->spr[i];
        lerp3f(prev->spr[i].position, cur->spr[i].position, alpha, c->ospr[i].position);
    }
    for (i = 0; i < cur->nl; i++) {
        c->olv[i] = cur->lv[i];
        lerp3f(prev->lv[i].position, cur->lv[i].position, alpha, c->olv[i].position);
    }
    frame_out(r, cur);
    r->positions = c->opos;
    r->normals = c->onrm;
    r->sprites = c->ospr;
    r->line_vertices = c->olv;
    return SA1_OK;
}

sa1_status sa1_preview_info_get(const sa1_context *ctx, uint32_t id, sa1_preview_info *out)
{
    const pv_reg *R;
    const pv_copy *c;
    sa1_preview_info info;
    if (ctx == NULL || out == NULL || out->struct_size < sizeof(uint32_t)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    R = reg_of(ctx);
    c = copy_of(R, id);
    if (c == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.kind = c->kind;
    info.type = c->type;
    info.steps = c->steps;
    info.settled = c->type == SA1_PREVIEW_BOARD ? 1u : c->steps >= k_tinfo[c->type - 1u].settle;
    if (c->nfr > 0) {
        const pv_frame *f = &c->fr[c->cur];
        info.draws = f->nd;
        info.triangles = f->nt;
        memcpy(info.bounds, f->bounds, sizeof info.bounds);
    }
    sa1_copy_out(out, &info, sizeof info);
    if (R->show[c->kind] != NULL && R->show[c->kind]->dead) {
        return R->show[c->kind]->dead_status;
    }
    return SA1_OK;
}

sa1_status sa1_preview_ring_places(sa1_context *ctx, int32_t kind, const float params[3], const float rotation[9],
                                   float *places, uint32_t *bits, uint32_t capacity, uint32_t *count)
{
    float sp[48], p[3], rot[9];
    uint32_t idx[16], n = 0, i, zero = 1;
    pv_reg *R;
    pv_show *S;
    sa1_status st;
    if (count != NULL) {
        *count = 0;
    }
    if (ctx == NULL || params == NULL || count == NULL || (capacity != 0 && places == NULL)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    /* an O_GRING record's params and rotation, checked as sa1_set_add does (no showcase reach: no task is made) */
    memcpy(p, params, sizeof p);
    memset(rot, 0, sizeof rot);
    if (rotation != NULL) {
        memcpy(rot, rotation, sizeof rot);
    }
    st = gring_check(ctx, p);
    if (st == SA1_OK) {
        st = rot_check(ctx, rot, &zero);
    }
    if (st != SA1_OK) {
        return st;
    }
    st = kind_check(ctx, kind);
    if (st != SA1_OK) {
        return st;
    }
    st = reg_get(ctx, &R);
    if (st != SA1_OK) {
        return st;
    }
    st = show_get(ctx, R, kind, &S);
    if (st != SA1_OK) {
        return st;
    }
    S->idle = 0;
    st = sa1wp_ring_places(S->w, p, rot, sp, idx, &n);
    if (st != SA1_OK) {
        if (sa1wp_fault(S->w)[0]) {
            S->dead = 1;
            S->dead_status = st;
            snprintf(S->fault, sizeof S->fault, "%s", sa1wp_fault(S->w));
        }
        return sa1_fail(ctx, st, "previews: the ring places failed (%s)", S->fault);
    }
    *count = n;
    if (capacity < n) {
        return SA1_ERR_BUFFER_TOO_SMALL;
    }
    for (i = 0; i < n; i++) {
        sa1_to_host_point(ctx, &sp[3u * i], &places[3u * i]);   /* object space: the record's position is the origin */
        if (bits != NULL) {
            bits[i] = 1u << idx[i];
        }
    }
    return SA1_OK;
}

/* ---- tests ------------------------------------------------------------------------------------------------------------- */
sa1_status sa1pv_test_fault(sa1_context *ctx, uint32_t id)
{
    pv_reg *R = reg_of(ctx);
    pv_copy *c = copy_of(R, id);
    pv_show *S;
    if (c == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    S = R->show[c->kind];
    if (S == NULL || S->dead) {
        return SA1_ERR_GAME_FAULT;
    }
    return sa1wp_test_break(S->w, c->tp);
}

sa1_status sa1pv_test_showcase(const sa1_context *ctx, int32_t kind, sa1pv_test_info *out, struct sa1g_world **world)
{
    const pv_reg *R = reg_of(ctx);
    const pv_show *S;
    sa1_capture_stats cst;
    if (out != NULL) {
        memset(out, 0, sizeof *out);
    }
    if (world != NULL) {
        *world = NULL;
    }
    if (out == NULL || kind < 0 || kind >= SA1_CHAR_COUNT) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    S = R != NULL ? R->show[kind] : NULL;
    if (S == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    out->exists = 1;
    out->dead = (uint32_t)S->dead;
    out->copies = S->copies;
    out->leaving = S->leaving;
    out->idle_pairs = S->idle;
    out->pairs = S->pairs;
    out->records_est = S->records_est;
    out->cap_capacity = S->cap_capacity;
    sa1_capture_get_stats(S->cap, &cst);
    out->cap_records = cst.records;
    sa1wp_test_counts(S->w, out);
    if (world != NULL) {
        *world = S->w;
    }
    return SA1_OK;
}

int32_t sa1pv_test_slot(const sa1_context *ctx, uint32_t id)
{
    const pv_copy *c = copy_of(reg_of(ctx), id);
    return c != NULL ? (int32_t)c->slot : -1;
}
#endif /* !SA1_NO_WORLD */
