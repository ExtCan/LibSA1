/*
 * visuals_test.c - round 0.9.4 (agent 094-lib-render): game OBJECT draws between the pairs (sadc docs/research/
 * gsa1_094_plan.md decision 3 and 6.2 with its Review R.1 / R.2; gsa1_091b_visuals.md 2 and 4.2 R-F1..R-F4 with its Review
 * R.2.3-R.2.6, R.3.1, R.3.6-R.3.9, R.4.2 (a)-(d)). The body of SADC build/research/visuals/visuals_probe.c parts "rings" and
 * "drops", and of the review's rv_extra.c "blink", with the cases the plan adds.
 *
 *   visuals_test [--only NAME]... [--verbose] GDI
 *
 * Built and run by tests/build_render_tests.py visuals (a snapshot of build/<target>/libsa1.a and its headers; --before DIR
 * builds it against an older library: the cases then show the faults this round fixes). The context is in host SOURCE
 * coordinates (4 HU per SA1 unit, z up) on a flat GRID floor; one pair per tick; every pair is rendered at alpha 0, 0.25,
 * 0.5, 0.75 and 1. A ring model is a 96-triangle piece of an env-mapped, non-shadow triangle draw of a ring's size.
 *
 *   rings   (1) 8 RINGs (objexec_RING 8C0869E8, made outside the steps as the SET spawner makes them) 10 units apart,
 *           made far to near, Knuckles and Sonic run through them: every ring model at alpha 0.25 / 0.5 / 0.75 within
 *           1 HU of its ring (0.9.3a: 20 HU in the pairs after a collection: the n-th draw of the ring model was
 *           matched with the n-th draw of the pair before); made near to far (no slide before either); (1b) the rings'
 *           blob shadows within 2 HU of the ground under their ring; (2) each sparkle sprite at alpha < 1 within 2 HU of
 *           its own last-pair position (0.9.1a: 48.8 / 58.3 HU)
 *   groups  (1c) an O_GRING line (scl 7, 10, 0: 8 rings) and a circle (7, 20, 1: 7 rings), collected one ring at a time:
 *           every remaining child model within 1 HU of its ring (a group's children draw with the group's exec 8C087B80:
 *           gsa1_094_plan.md Review R.2)
 *   drops   (3) dug rings (8C087A9E, the dropped ring 8C0870EA): where Knuckles stands, 30 units ahead (10 rings) and the
 *           fountain's 32 rings 20 units ahead; ring models within 2 HU of a plausible position (a ring of the pair
 *           before moving straight to one of the last pair, or a ring of the last pair), their sparkles within 2 HU
 *   lying   (3c) 10 dug rings lying 80 units to the side while Knuckles runs past: every ring model within 1 HU of where it
 *           lies (0.9.1a: 12.9 HU; lying rings draw every other pair, so each drawn pair had no partner)
 *   punch   (5) a draw of the player's own task new in a pair while the character runs keeps moving with it: Knuckles'
 *           punch sprites at alpha 0.5 within 1 HU of their place relative to the drawn root at alpha 1
 *   tear    (7) Sonic and Knuckles running with turns and jumps: at the ball -> body changes the new draws of the
 *           character (blob shadows excepted) move with the root alike (spread < 1 HU; the charset-only prototype: 10 HU)
 *   tails   (8) Tails running with turns and jumps: his draws at alpha 0.5 no farther from the midpoint of their alpha-1
 *           places relative to the root than in 0.9.3a + 0.5 HU (VISUALS_TAILS_MID_093A; the charset-only prototype: 3.5-6.4
 *           vs 0.6-2.3 HU)
 *   aura    (9) both Super Sonics of 0.9.3a (Vs. Perfect Chaos, kind 8; General, SA1_SUPER_GENERAL | START) dashing: every
 *           aura draw (additive, SUPERSONIC textures) at alpha 0.5 within 2 HU of its place relative to the root at alpha
 *           1, the pairs where the aura model switches included (auras are not OBJECT draws: unchanged from 0.9.3a)
 *   hash    not a check: a hash of every render (positions, sprite places) of the tear / tails / aura runs, for comparing a
 *           build with 0.9.3a's (no game object in them, so the hashes must be equal)
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_world.h"
#include "sa1_api_world.h"
#include "sa1_capture.h"

#define EXEC_RING        0x8C0869E8u   /* objexec_RING */
#define EXEC_GRING       0x8C087B80u   /* objexec_O_GRING */
#define EXEC_DROP        0x8C0870EAu   /* the dropped / scattered / dug ring */
#define FN_CREATE        0x8C09852Eu   /* CreateElementalTask(im, level, exec) */
#define FN_DROPSPAWN     0x8C087A9Eu   /* func_8C087A9E(n, x, y, z): n dropped rings (hurt scatter, Knuckles' dig) */
#define ADDR_TASK_LISTS  0x8C79F04Cu
#define PHYS(a)          ((a) & 0x1FFFFFFFu)

/* (8): the 0.9.3a value of the Tails midpoint metric of t_tails (HU): 9.082, measured with this test against the LIB build
 * of 2026-10-26 15:05 (G-SA1 0.9.3a + 094-land; build_render_tests.py visuals --before), x86_64-windows. It is the worst of
 * every draw of Tails over the run, his tail copies included (their arc between the pairs against the chord's midpoint) */
#ifndef VISUALS_TAILS_MID_093A
#define VISUALS_TAILS_MID_093A 9.083
#endif

static int g_fail, g_checks, g_verbose;
static const char *g_only[16];
static int g_nonly;

#define CHECK(cond, ...)                                                                                              \
    do {                                                                                                              \
        int c_ = (cond) ? 1 : 0;                                                                                      \
        g_checks++;                                                                                                   \
        printf(c_ ? "  ok    " : "  FAIL  ");                                                                         \
        if (!c_) g_fail++;                                                                                            \
        printf(__VA_ARGS__);                                                                                          \
        printf("\n");                                                                                                 \
        fflush(stdout);                                                                                               \
    } while (0)

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN) {
        printf("    [libsa1] %s\n", msg);
    }
}

typedef struct tris {
    sa1_collision_tri *t;
    uint32_t n, cap;
} tris;

static void tri_add(tris *b, const float a[3], const float c[3], const float d[3])
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        uint32_t cap = b->cap ? 2u * b->cap : 1024u;
        sa1_collision_tri *nt = (sa1_collision_tri *)realloc(b->t, cap * sizeof *nt);
        if (nt == NULL) {
            return;
        }
        b->t = nt;
        b->cap = cap;
    }
    t = &b->t[b->n++];
    memcpy(t->v[0], a, sizeof t->v[0]);
    memcpy(t->v[1], c, sizeof t->v[1]);
    memcpy(t->v[2], d, sizeof t->v[2]);
    t->surface = SA1_SURFACE_SOLID;
}

/* SOURCE coordinates (4 HU per unit, z up), a flat GRID floor of 1024-HU tiles over +-16384 HU at z 0 */
static sa1_context *make_ctx(const char *gdi)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    tris b;
    float x, y;
    sa1_status st;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) |
                    (1u << SA1_CHAR_SUPER_SONIC);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_WORLD_IMAGES | SA1_LOAD_SUPER_FORMS;
    st = sa1_load_disc(ctx, gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    memset(&b, 0, sizeof b);
    for (x = -16384.0f; x < 16384.0f; x += 1024.0f) {
        for (y = -16384.0f; y < 16384.0f; y += 1024.0f) {
            float a[3] = { x, y, 0 }, c[3] = { x + 1024.0f, y, 0 }, d[3] = { x + 1024.0f, y + 1024.0f, 0 },
                  e[3] = { x, y + 1024.0f, 0 };
            tri_add(&b, a, c, d);
            tri_add(&b, a, d, e);
        }
    }
    st = sa1_collision_set_static(ctx, b.t, b.n);
    free(b.t);
    if (SA1_FAILED(st)) {
        printf("sa1_collision_set_static: %s\n", sa1_status_string(st));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

/* ---- guest memory ------------------------------------------------------------------------------------------------ */
static uint8_t *gp(const sa1g_world *w, uint32_t g)
{
    if (g == 0 || ((g >> 24) & 31u) != SA1G_SEG_ARENA) {
        return NULL;
    }
    return (uint8_t *)sa1g_world_ptr(w, g);
}

static uint32_t rd32(const sa1g_world *w, uint32_t g)
{
    const uint8_t *p = gp(w, g);
    uint32_t v = 0;
    if (p != NULL) memcpy(&v, p, 4);
    return v;
}

static void wr32(const sa1g_world *w, uint32_t g, uint32_t v)
{
    uint8_t *p = gp(w, g);
    if (p != NULL) memcpy(p, &v, 4);
}

static void wrf(const sa1g_world *w, uint32_t g, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    wr32(w, g, v);
}

/* ---- characters -------------------------------------------------------------------------------------------------- */
static sa1_character_id make_char_ex(sa1_context *ctx, int32_t kind, float x, float y, float z, uint32_t flags,
                                     int32_t rings, int super_given, uint32_t super_flags)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | flags;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    if (rings >= 0) {
        d.flags |= SA1_CREATE_START_RINGS;
        d.start_rings = rings;
    }
    if (super_given) {
        d.flags |= SA1_CREATE_SUPER;
        d.super_flags = super_flags;
    }
    st = sa1_character_create_ex(ctx, &d, &id);
    if (SA1_FAILED(st)) {
        printf("    create: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return 0;
    }
    return id;
}

static sa1_character_id make_char(sa1_context *ctx, int32_t kind, float x, float y, float z)
{
    return make_char_ex(ctx, kind, x, y, z, 0u, -1, 0, 0u);
}

static sa1_status tick(sa1_context *ctx, sa1_character_id id, float sx, float sy, uint32_t buttons)
{
    sa1_input in;
    sa1_status st;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    if (st != SA1_OK) {
        printf("    tick: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
    }
    return st;
}

static void drain(sa1_context *ctx)
{
    sa1_event ev[256];
    uint32_t got;
    do {
        got = 0;
        sa1_poll_events(ctx, ev, 256, &got);
    } while (got == 256);
}

static void state_of(sa1_context *ctx, sa1_character_id id, sa1_state_ex *s)
{
    memset(s, 0, sizeof *s);
    s->struct_size = sizeof *s;
    sa1_character_state_ex(ctx, id, s);
}

static void player_sa1(const sa1g_world *w, float p[3])
{
    sa1g_player_ptrs pp;
    sa1g_world_player(w, &pp);
    p[0] = p[1] = p[2] = 0.0f;
    if (pp.twp != 0) memcpy(p, sa1g_world_ptr(w, pp.twp + 0x20u), 12);
}

/* SA1 -> host for SA1_COORDS_PRESET_SOURCE */
static void to_host(const float s[3], float h[3])
{
    h[0] = s[0] * 4.0f;
    h[1] = -s[2] * 4.0f;
    h[2] = s[1] * 4.0f;
}

static double dist3f(const float *a, const float *b)
{
    double dx = (double)a[0] - b[0], dy = (double)a[1] - b[1], dz = (double)a[2] - b[2];
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static int32_t archive_named(const sa1_context *ctx, int32_t kind, const char *name)
{
    uint32_t a;
    for (a = 0; a < 16u; a++) {
        const char *nm = NULL;
        uint32_t cnt = 0;
        if (sa1_texture_archive(ctx, kind, a, &nm, &cnt) != SA1_OK) {
            break;
        }
        if (nm != NULL && strcmp(nm, name) == 0) {
            return (int32_t)a;
        }
    }
    return -1;
}

/* ---- calling game functions outside the steps ------------------------------------------------------------------- */
typedef struct mkreq {
    uint32_t exec;
    uint32_t out_tp;
} mkreq;

static void mk_cb(sa1g_world *w, void *user)
{
    mkreq *m = (mkreq *)user;
    typedef void *(*create_fn)(uint16_t, int32_t, sa1_gfn);
    void *tp = ((create_fn)sa1_fn(SA1_GFN_C(FN_CREATE), 0u))(2, 3, SA1_GFN_C(m->exec));
    (void)w;
    m->out_tp = tp != NULL ? sa1_encode(tp) : 0u;
}

/* a task with exec `exec` (CreateElementalTask(2, 3, exec), as the SET spawner 8C091380 makes one from a list entry) at
 * pos, angle y, scale scl; returns its task */
static uint32_t make_task(sa1g_world *w, uint32_t exec, const float pos[3], uint32_t ang_y, const float scl[3])
{
    mkreq m;
    uint32_t twp, k;
    m.exec = exec;
    m.out_tp = 0;
    if (sa1g_world_call(w, mk_cb, &m) != SA1_OK || m.out_tp == 0) return 0;
    twp = rd32(w, m.out_tp + 0x20u);
    for (k = 0; k < 3; k++) {
        wr32(w, twp + 0x14u + 4u * k, k == 1 ? ang_y : 0u);
        wrf(w, twp + 0x20u + 4u * k, pos[k]);
        wrf(w, twp + 0x2Cu + 4u * k, scl ? scl[k] : 0.0f);
    }
    return m.out_tp;
}

static uint32_t make_ring(sa1g_world *w, const float pos[3])
{
    return make_task(w, EXEC_RING, pos, 0u, NULL);
}

typedef struct dropreq {
    int32_t n;
    float x, y, z;
} dropreq;

static void drop_cb(sa1g_world *w, void *user)
{
    dropreq *d = (dropreq *)user;
    typedef void (*fn)(int32_t, float, float, float);
    (void)w;
    ((fn)sa1_fn(SA1_GFN_C(FN_DROPSPAWN), 0u))(d->n, d->x, d->y, d->z);
}

/* tasks with this exec (top-level lists and children): their guest tp addresses */
static uint32_t list_tasks_in(const sa1g_world *w, uint32_t tp, uint32_t exec, int depth, uint32_t *out, uint32_t n,
                              uint32_t cap)
{
    uint32_t guard = 0;
    while (tp != 0 && guard++ < 0x4000u) {
        if (PHYS(rd32(w, tp + 0x10u)) == PHYS(exec)) {
            if (n < cap) out[n] = tp;
            n++;
        }
        if (depth < 4) n = list_tasks_in(w, rd32(w, tp + 0x0Cu), exec, depth + 1, out, n, cap);
        tp = rd32(w, tp);
    }
    return n;
}

static uint32_t list_tasks(const sa1g_world *w, uint32_t exec, uint32_t *out, uint32_t cap)
{
    uint32_t lvl, n = 0;
    for (lvl = 0; lvl < 8; lvl++) n = list_tasks_in(w, rd32(w, ADDR_TASK_LISTS + 4u * lvl), exec, 0, out, n, cap);
    return n;
}

/* ---- what a render holds ------------------------------------------------------------------------------------------ */
#define MAXCH 256
typedef struct chunks {
    float c[MAXCH][3];
    uint32_t n;
} chunks;

/* the ring models of a render: 96-triangle pieces of env-mapped, non-shadow triangle draws, about a ring's size */
static void ring_chunks(const sa1_render *r, chunks *out)
{
    uint32_t d, t, k, q;
    out->n = 0;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->prim != SA1_PRIM_TRIANGLES || !(dr->flags & SA1_DRAW_ENV_MAP) || (dr->flags & SA1_DRAW_SHADOW) ||
            dr->count == 0 || dr->count % 96u != 0)
            continue;
        for (q = 0; q < dr->count / 96u; q++) {
            float c[3] = { 0, 0, 0 }, ext = 0.0f;
            uint32_t t0 = dr->first + 96u * q;
            for (t = t0; t < t0 + 96u; t++)
                for (k = 0; k < 9; k++) c[k % 3] += r->positions[9u * t + k];
            for (k = 0; k < 3; k++) c[k] /= 288.0f;
            for (t = t0; t < t0 + 96u; t++)
                for (k = 0; k < 3; k++) {
                    float dd = (float)dist3f(r->positions + 9u * t + 3u * k, c);
                    if (dd > ext) ext = dd;
                }
            if (ext < 8.0f || ext > 60.0f || out->n >= MAXCH) continue;
            memcpy(out->c[out->n++], c, sizeof c);
        }
    }
}

/* whether a draw belongs to a game object (SA1_DRAW_OBJECT; a build without it: every draw but the ones within 2 HU of
 * the root, as the research probe did) */
static int object_draw(const sa1_draw *dr)
{
#ifdef SA1_DRAW_OBJECT
    return (dr->flags & SA1_DRAW_OBJECT) != 0;
#else
    (void)dr;
    return 1;
#endif
}

/* the sprites of game objects (sa1_render.sprites of SA1_DRAW_OBJECT sprite draws; without the flag: every sprite) */
typedef struct sprset {
    float p[MAXCH][3];
    uint32_t n;
} sprset;

static void object_sprites(const sa1_render *r, sprset *s)
{
    uint32_t d, i;
    s->n = 0;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->prim != SA1_PRIM_SPRITES || !object_draw(dr))
            continue;
        for (i = dr->first; i < dr->first + dr->count && i < r->num_sprites && s->n < MAXCH; i++)
            memcpy(s->p[s->n++], r->sprites[i].position, 12);
    }
}

/* the centres of the game objects' blob shadow pieces (2 triangles each) farther than 2 HU from the root horizontally */
static uint32_t object_shadows(const sa1_render *r, float (*out)[3], uint32_t cap)
{
    uint32_t d, t, k, piece, n = 0;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->prim != SA1_PRIM_TRIANGLES || !(dr->flags & SA1_DRAW_SHADOW) || dr->count == 0 || !object_draw(dr))
            continue;
        for (piece = 0; piece + 2u <= dr->count; piece += 2u) {
            float c[3] = { 0, 0, 0 };
            double dc;
            for (t = dr->first + piece; t < dr->first + piece + 2u; t++)
                for (k = 0; k < 9; k++) c[k % 3] += r->positions[9u * t + k];
            for (k = 0; k < 3; k++) c[k] /= 6.0f;
            dc = sqrt((double)(c[0] - r->position[0]) * (c[0] - r->position[0]) +
                      (double)(c[1] - r->position[1]) * (c[1] - r->position[1]));
            if (dc < 2.0 || n >= cap) continue;     /* the character's own blob (a build without SA1_DRAW_OBJECT) */
            memcpy(out[n++], c, sizeof c);
        }
    }
    return n;
}

static const float k_alphas[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

static int part_on(const char *name)
{
    int i;
    if (g_nonly == 0) return 1;
    for (i = 0; i < g_nonly; i++)
        if (strcmp(g_only[i], name) == 0) return 1;
    return 0;
}

/* ==== rings: RING 8C0869E8 in a line (the SET ring; 0.9.4's placeable rings) =========================================== */
typedef struct rsum {
    double err_collect, err_other, spr_dev, shadow_err;
    uint32_t pairs_collect, spr_pairs, shadow_pieces;
} rsum;

/* one pair's renders: every ring model at alpha 0.25 / 0.5 / 0.75 against the nearest of the static ring places R (any
 * distance is the render's), the game objects' blob shadows at alpha < 1 against the ground under the rings
 * (horizontally), each OBJECT sprite at alpha < 1 against its own place at alpha 1 */
static void ring_frame(sa1_context *ctx, sa1_character_id id, const float (*R)[3], uint32_t nr, int collected_now,
                       uint32_t pair, rsum *S)
{
    const sa1_render *r = NULL;
    chunks C;
    sprset S1, Sa;
    float sh[64][3];
    double emax = 0.0, sdev = 0.0;
    uint32_t a, k, j, ns;
    S1.n = 0;
    if (sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK && r) {
        object_sprites(r, &S1);
    }
    for (a = 0; a < 4; a++) {
        if (sa1_character_render(ctx, id, k_alphas[a], &r) != SA1_OK || r == NULL) continue;
        if (a > 0) {
            ring_chunks(r, &C);
            for (k = 0; k < C.n; k++) {
                double best = 1e30;
                for (j = 0; j < nr; j++) {
                    double d = dist3f(C.c[k], R[j]);
                    if (d < best) best = d;
                }
                if (best > emax) emax = best;
            }
        }
        ns = object_shadows(r, sh, 64);
        for (k = 0; k < ns; k++) {
            double best = 1e30;
            for (j = 0; j < nr; j++) {
                double d = sqrt((double)(sh[k][0] - R[j][0]) * (sh[k][0] - R[j][0]) +
                                (double)(sh[k][1] - R[j][1]) * (sh[k][1] - R[j][1]));
                if (d < best) best = d;
            }
            S->shadow_pieces++;
            if (best > S->shadow_err) S->shadow_err = best;
            if (best > 2.0 && g_verbose)
                printf("    pair %3u alpha %.2f: a blob shadow %.1f HU from the ground under every ring\n", (unsigned)pair,
                       (double)k_alphas[a], best);
        }
        object_sprites(r, &Sa);
        if (Sa.n == S1.n)
            for (k = 0; k < Sa.n; k++) {
                double d = dist3f(Sa.p[k], S1.p[k]);
                if (d > sdev) sdev = d;
            }
    }
    if (collected_now) {
        S->pairs_collect++;
        if (emax > S->err_collect) S->err_collect = emax;
    } else if (emax > S->err_other) {
        S->err_other = emax;
    }
    if (S1.n) {
        S->spr_pairs++;
        if (sdev > S->spr_dev) S->spr_dev = sdev;
    }
    if (g_verbose && (emax > 1.0 || sdev > 2.0 || collected_now))
        printf("    pair %3u%s: ring models off their ring at alpha .25-.75 up to %.2f HU; OBJECT sprites %u, off their alpha-1 "
               "place up to %.2f HU\n", (unsigned)pair, collected_now ? " (collection drawn)" : "", emax, (unsigned)S1.n,
               sdev);
}

static void ring_line(sa1_context *ctx, int32_t kind, const char *name, int far_first)
{
    const uint32_t nrings = 8;
    const float spacing = 10.0f;
    sa1_character_id id = make_char(ctx, kind, 0.0f, 0.0f, 8.0f);
    sa1g_world *w = id ? sa1_character_world(ctx, id) : NULL;
    float p[3], R[32][3];
    uint32_t i, k, lastn = nrings;
    int32_t last = 0;
    rsum S;
    sa1_state_ex s;
    printf("\n-- %s runs through %u RINGs %.0f units apart, made %s first\n", name, (unsigned)nrings, (double)spacing,
           far_first ? "far" : "near");
    if (w == NULL) {
        CHECK(0, "%s created", name);
        return;
    }
    memset(&S, 0, sizeof S);
    for (i = 0; i < 20; i++) tick(ctx, id, 0.0f, 0.0f, 0);
    drain(ctx);
    player_sa1(w, p);
    for (k = 0; k < nrings; k++) {
        uint32_t kk = far_first ? nrings - 1u - k : k;
        float q[3];
        q[0] = p[0] + 25.0f + spacing * (float)kk;
        q[1] = p[1] + 6.0f;
        q[2] = p[2];
        make_ring(w, q);
        to_host(q, R[k]);
    }
    tick(ctx, id, 0.0f, 0.0f, 0);
    tick(ctx, id, 0.0f, 0.0f, 0);
    drain(ctx);
    for (i = 0; i < 120; i++) {
        const sa1_render *r = NULL;
        chunks C;
        uint32_t nnow = lastn;
        tick(ctx, id, 0.0f, i < 90 ? 1.0f : 0.0f, 0);
        drain(ctx);
        state_of(ctx, id, &s);
        if (sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK && r) {
            ring_chunks(r, &C);
            nnow = C.n;
        }
        /* a collection drawn: the first pair whose draws no longer have the collected ring */
        ring_frame(ctx, id, (const float (*)[3])R, nrings, nnow < lastn, i, &S);
        lastn = nnow;
        last = s.rings;
    }
    printf("    rings %d; ring models off their ring at alpha .25-.75: %.2f HU in the %u pairs that drew a collection, %.2f HU in"
           " the others; blob shadows (%u pieces) %.2f HU off the ground under their ring; OBJECT sprites in %u pairs, up to "
           "%.2f HU off their alpha-1 place\n", (int)last, S.err_collect, (unsigned)S.pairs_collect, S.err_other,
           (unsigned)S.shadow_pieces, S.shadow_err, (unsigned)S.spr_pairs, S.spr_dev);
    CHECK(last + 1 >= (int32_t)nrings && S.pairs_collect > 0, "%s collected the rings (%d of %u)", name, (int)last,
          (unsigned)nrings);
    CHECK(S.err_collect < 1.0 && S.err_other < 1.0, "(1) %s, made %s first: no ring model slides (%.2f HU after a collection,"
          " %.2f HU otherwise; 0.9.3a: 20 HU after a collection when made far first)", name, far_first ? "far" : "near",
          S.err_collect, S.err_other);
    CHECK(S.shadow_pieces > 0 && S.shadow_err < 2.0, "(1b) %s: the rings' blob shadows on the ground under their ring (%.2f "
          "HU)", name, S.shadow_err);
    CHECK(S.spr_pairs > 0 && S.spr_dev < 2.0, "(2) %s: each sparkle sprite within 2 HU of its own last-pair place (%.2f HU; "
          "0.9.1a: 48.8 / 58.3 HU)", name, S.spr_dev);
    sa1_character_destroy(ctx, id);
    drain(ctx);
}

static void t_rings(sa1_context *ctx)
{
    printf("\n== rings: RING 8C0869E8 in a line (the SET ring; the 0.9.4 placeable rings)\n");
    ring_line(ctx, SA1_CHAR_KNUCKLES, "Knuckles", 1);
    ring_line(ctx, SA1_CHAR_SONIC, "Sonic", 1);
    ring_line(ctx, SA1_CHAR_KNUCKLES, "Knuckles", 0);
}

/* ==== groups: O_GRING 8C087B80, a line and a circle (gsa1_094_plan.md Review R.2) ======================================= */
#define MAXR 64
typedef struct rset {
    float p[MAXR][3];
    uint32_t n;
} rset;

static void rset_add(rset *R, const float *c)
{
    uint32_t j;
    for (j = 0; j < R->n; j++)
        if (dist3f(R->p[j], c) < 0.5) return;
    if (R->n < MAXR) memcpy(R->p[R->n++], c, 12);
}

static void group_run(sa1_context *ctx, const char *name, const float scl[3], uint32_t ang_y, float dz, uint32_t want)
{
    sa1_character_id id = make_char(ctx, SA1_CHAR_KNUCKLES, 0.0f, 0.0f, 8.0f);
    sa1g_world *w = id ? sa1_character_world(ctx, id) : NULL;
    float p[3], pos[3];
    rset R;
    uint32_t i, k, j, a, collections = 0, multi = 0, children = 0, pairs_after = 0;
    int32_t last = 0;
    double worst = 0.0, worst_after = 0.0;
    sa1_state_ex s;
    printf("\n-- Knuckles runs through an O_GRING %s (scl %.0f, %.0f, %.0f)\n", name, (double)scl[0], (double)scl[1],
           (double)scl[2]);
    if (w == NULL) {
        CHECK(0, "Knuckles created");
        return;
    }
    memset(&R, 0, sizeof R);
    for (i = 0; i < 20; i++) tick(ctx, id, 0.0f, 0.0f, 0);
    drain(ctx);
    player_sa1(w, p);
    pos[0] = p[0] + 60.0f;
    pos[1] = p[1] + 6.0f;
    pos[2] = p[2] + dz;
    if (make_task(w, EXEC_GRING, pos, ang_y, scl) == 0) {
        CHECK(0, "the group task made");
        return;
    }
    for (i = 0; i < 4; i++) tick(ctx, id, 0.0f, 0.0f, 0);
    drain(ctx);
    children = list_tasks(w, EXEC_RING, NULL, 0);
    state_of(ctx, id, &s);
    last = s.rings;
    for (i = 0; i < 120; i++) {
        const sa1_render *r = NULL;
        chunks C;
        double e = 0.0;
        int32_t before = last;
        tick(ctx, id, 0.0f, i < 90 ? 1.0f : 0.0f, 0);
        drain(ctx);
        state_of(ctx, id, &s);
        if (s.rings > last) {
            collections += (uint32_t)(s.rings - last);
            multi += s.rings - last > 1;
            last = s.rings;
        }
        if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) break;
        ring_chunks(r, &C);
        for (k = 0; k < C.n; k++) rset_add(&R, C.c[k]);     /* static rings: their alpha-1 places are where they are */
        for (a = 1; a < 4; a++) {
            if (sa1_character_render(ctx, id, k_alphas[a], &r) != SA1_OK || r == NULL) continue;
            ring_chunks(r, &C);
            for (k = 0; k < C.n; k++) {
                double best = 1e30;
                for (j = 0; j < R.n; j++) {
                    double d = dist3f(C.c[k], R.p[j]);
                    if (d < best) best = d;
                }
                if (best > e) e = best;
            }
        }
        if (e > worst) worst = e;
        if (collections > 0) {
            pairs_after++;
            if (e > worst_after) worst_after = e;
        }
        if (g_verbose && (e > 1.0 || s.rings != before))
            printf("    pair %3u rings %d: ring models %u, worst offset at alpha .25-.75 %.2f HU\n", (unsigned)i, (int)s.rings,
                   (unsigned)C.n, e);
    }
    printf("    %u child RING tasks; %u rings collected (%u pairs with more than one); %u ring places; ring models off "
           "their ring at alpha .25-.75: %.2f HU (%.2f HU in the %u pairs after the first collection)\n", (unsigned)children,
           (unsigned)collections, (unsigned)multi, (unsigned)R.n, worst, worst_after, (unsigned)pairs_after);
    CHECK(children == want && collections >= 2 && multi == 0, "(1c) %s: %u children (%u), %u collected one ring at a time",
          name, (unsigned)children, (unsigned)want, (unsigned)collections);
    CHECK(worst < 1.0, "(1c) %s: every remaining child model within 1 HU of its ring (%.2f HU)", name, worst);
    sa1_character_destroy(ctx, id);
    drain(ctx);
}

static void t_groups(sa1_context *ctx)
{
    static const float line[3] = { 7.0f, 10.0f, 0.0f }, circle[3] = { 7.0f, 20.0f, 1.0f };
    printf("\n== groups: O_GRING 8C087B80 (its children draw with the group's exec)\n");
    group_run(ctx, "line", line, 0x4000u, 0.0f, 8);
    /* the circle's rings at i * 360 / 7 degrees (x = 20 sin, z = 20 cos): the ones at 102.9 and 257.1 degrees lie on the
       running line when the centre is 4.45 units beside it */
    group_run(ctx, "circle", circle, 0u, 4.45f, 7);
}

/* ==== drops: dug / scattered rings (8C0870EA), made by the dig's 8C087A9E ============================================= */
/* the distance of x from the nearest plausible position at alpha a: a ring of the pair before moving straight to one of
 * the last pair (within `gate` HU of each other), or a ring of the last pair as drawn */
static double drop_err(const float *x, const chunks *P, const chunks *C, float a, float gate)
{
    double best = 1e30;
    uint32_t j, k, m;
    for (k = 0; k < C->n; k++) {
        double d = dist3f(x, C->c[k]);
        if (d < best) best = d;
        for (j = 0; j < P->n; j++) {
            float l[3];
            if (dist3f(P->c[j], C->c[k]) > gate) continue;
            for (m = 0; m < 3; m++) l[m] = P->c[j][m] + a * (C->c[k][m] - P->c[j][m]);
            d = dist3f(x, l);
            if (d < best) best = d;
        }
    }
    return best;
}

static void drops_run(sa1_context *ctx, const char *name, float ahead, uint32_t start_move, int32_t n,
                      uint32_t move_pairs)
{
    sa1_character_id id = make_char(ctx, SA1_CHAR_KNUCKLES, 0.0f, 0.0f, 8.0f);
    sa1g_world *w = id ? sa1_character_world(ctx, id) : NULL;
    float p[3];
    chunks P, C, A;
    dropreq dq;
    uint32_t i, k, a, made;
    int32_t last = 0;
    double worst = 0.0, worst_c = 0.0, worst_spr = 0.0;
    sa1_state_ex s;
    printf("\n-- %s\n", name);
    if (w == NULL) {
        CHECK(0, "Knuckles created");
        return;
    }
    for (i = 0; i < 20; i++) tick(ctx, id, 0.0f, 0.0f, 0);
    drain(ctx);
    player_sa1(w, p);
    dq.n = n;
    dq.x = p[0] + ahead;
    dq.y = ahead == 0.0f ? p[1] - 8.0f : p[1];   /* the dig: 8C0C8534 spawns at twp->pos.y - 8 (code_8C0C8534.c:150-158) */
    dq.z = p[2];
    sa1g_world_call(w, drop_cb, &dq);
    made = list_tasks(w, EXEC_DROP, NULL, 0);
    memset(&P, 0, sizeof P);
    for (i = 0; i < 120; i++) {
        const sa1_render *r = NULL;
        double e = 0.0, se = 0.0;
        sprset S1, Sa;
        tick(ctx, id, 0.0f, i >= start_move && i < start_move + move_pairs ? 1.0f : 0.0f, 0);
        drain(ctx);
        state_of(ctx, id, &s);
        if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) break;
        ring_chunks(r, &C);
        object_sprites(r, &S1);
        for (a = 1; a < 4; a++) {
            if (sa1_character_render(ctx, id, k_alphas[a], &r) != SA1_OK || r == NULL) continue;
            ring_chunks(r, &A);
            for (k = 0; k < A.n; k++) {
                double d = drop_err(A.c[k], &P, &C, k_alphas[a], 16.0f);
                if (d > e) e = d;
            }
            object_sprites(r, &Sa);
            if (Sa.n == S1.n)
                for (k = 0; k < Sa.n; k++) {
                    double d = dist3f(Sa.p[k], S1.p[k]);
                    if (d > se) se = d;
                }
        }
        if (se > worst_spr) worst_spr = se;
        if (s.rings != last) {
            if (e > worst_c) worst_c = e;
        } else if (e > worst) {
            worst = e;
        }
        if (g_verbose && (e > 2.0 || s.rings != last || se > 2.0))
            printf("    pair %3u rings %2d%s: ring models %2u (pair before %2u), worst offset at alpha .25-.75 %5.2f HU; "
                   "OBJECT sprites %2u, moved %5.2f HU\n", (unsigned)i, (int)s.rings, s.rings != last ? " +" : "  ",
                   (unsigned)C.n, (unsigned)P.n, e, (unsigned)S1.n, se);
        last = s.rings;
        P = C;
    }
    printf("    %u dropped-ring tasks made; rings %d; ring models away from every plausible position at alpha .25-.75: "
           "%.2f HU in pairs with a collection, %.2f HU in the others; OBJECT sprites moved up to %.2f HU\n",
           (unsigned)made, (int)last, worst_c, worst, worst_spr);
    CHECK(made == (uint32_t)n && last > 0, "%s: %u rings made, %d collected", name, (unsigned)made, (int)last);
    CHECK(worst_c < 2.0 && worst < 2.0 && worst_spr < 2.0, "(3) %s: ring models and sparkles where the game drew them "
          "(%.2f / %.2f / %.2f HU; 0.9.1a: up to 45.8 HU)", name, worst_c, worst, worst_spr);
    sa1_character_destroy(ctx, id);
    drain(ctx);
}

static void t_drops(sa1_context *ctx)
{
    printf("\n== drops: dug / scattered rings (8C0870EA), made by the dig's 8C087A9E\n");
    drops_run(ctx, "10 rings come up where Knuckles stands (the dig); he runs off through them after 6 pairs", 0.0f, 6, 10,
              14);
    drops_run(ctx, "10 rings come up 30 units ahead; Knuckles runs through them while they fly", 30.0f, 5, 10, 40);
    drops_run(ctx, "32 rings (the dig's ring fountain count) 20 units ahead; Knuckles runs through", 20.0f, 10, 32, 20);
}

/* ==== lying: dug rings lying beside Knuckles' path (gsa1_091b_visuals.md Review R.3.1, R.4.2 (c) case 3c) =============== */
static void t_lying(sa1_context *ctx)
{
    sa1_character_id id = make_char(ctx, SA1_CHAR_KNUCKLES, 0.0f, 0.0f, 8.0f);
    sa1g_world *w = id ? sa1_character_world(ctx, id) : NULL;
    float p[3];
    dropreq dq;
    uint32_t i, k, a, tl[64], run_from = 0, drawn_pairs = 0;
    double worst = 0.0;
    int32_t rings0 = 0;
    sa1_state_ex s;
    printf("\n== lying: 10 dug rings lying 80 units to the side of Knuckles' path; he runs past them\n");
    if (w == NULL) {
        CHECK(0, "Knuckles created");
        return;
    }
    for (i = 0; i < 20; i++) tick(ctx, id, 0.0f, 0.0f, 0);
    drain(ctx);
    player_sa1(w, p);
    dq.n = 10;
    dq.x = p[0] + 60.0f;
    dq.y = p[1] - 8.0f;
    dq.z = p[2] + 80.0f;
    sa1g_world_call(w, drop_cb, &dq);
    state_of(ctx, id, &s);
    rings0 = s.rings;
    for (i = 0; i < 160; i++) {
        const sa1_render *r = NULL;
        chunks C, A;
        uint32_t live, lying = 0;
        int running = run_from && i >= run_from && i < run_from + 40;
        double e = 0.0;
        live = list_tasks(w, EXEC_DROP, tl, 64);
        for (k = 0; k < live && k < 64; k++) {
            const uint8_t *m = gp(w, rd32(w, tl[k] + 0x20u));
            lying += m != NULL && *m == 2;
        }
        if (!run_from && lying == live && live > 0) run_from = i + 1;   /* all rings lie: start running next pair */
        tick(ctx, id, 0.0f, running ? 1.0f : 0.0f, 0);
        drain(ctx);
        state_of(ctx, id, &s);
        if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) break;
        ring_chunks(r, &C);
        if (!running || C.n == 0) continue;
        drawn_pairs++;
        for (a = 1; a < 4; a++) {
            if (sa1_character_render(ctx, id, k_alphas[a], &r) != SA1_OK || r == NULL) continue;
            ring_chunks(r, &A);
            for (k = 0; k < A.n; k++) {
                double best = 1e30;
                uint32_t j;
                for (j = 0; j < C.n; j++) {
                    double d = dist3f(A.c[k], C.c[j]);
                    if (d < best) best = d;
                }
                if (best > e) e = best;
            }
        }
        if (e > worst) worst = e;
        if (g_verbose)
            printf("    pair %3u running, lying %u of %u: ring models %2u, worst offset from where they lie at alpha .25-.75 "
                   "%5.2f HU\n", (unsigned)i, (unsigned)lying, (unsigned)live, (unsigned)C.n, e);
    }
    printf("    rings collected %d (want 0); pairs with lying rings drawn while running %u: worst %.2f HU\n",
           (int)(s.rings - rings0), (unsigned)drawn_pairs, worst);
    CHECK(drawn_pairs > 0 && s.rings == rings0, "lying: %u running pairs drew the lying rings, none collected",
          (unsigned)drawn_pairs);
    CHECK(worst < 1.0, "(3c) lying dug rings stay where they lie while Knuckles runs past (%.2f HU; 0.9.1a: 12.9 HU)", worst);
    sa1_character_destroy(ctx, id);
    drain(ctx);
}

/* ==== the character's own draws: unchanged rules (cases 5, 7, 8, 9) ==================================================== */
static uint64_t g_hash = 1469598103934665603ull;

static void hash_bytes(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) g_hash = (g_hash ^ b[i]) * 1099511628211ull;
}

/* the render's geometry into g_hash (positions, sprite and line places, draw ranges and flags) */
static void hash_render(const sa1_render *r)
{
    uint32_t d;
    hash_bytes(r->position, 12);
    hash_bytes(&r->num_triangles, 4);
    if (r->num_triangles) hash_bytes(r->positions, 36u * (size_t)r->num_triangles);
    for (d = 0; d < r->num_draws; d++) {
        hash_bytes(&r->draws[d].prim, 12);
        hash_bytes(&r->draws[d].flags, 4);
    }
    for (d = 0; d < r->num_sprites; d++) hash_bytes(r->sprites[d].position, 12);
    if (r->num_line_vertices) hash_bytes(r->line_vertices, sizeof(sa1_line_vertex) * (size_t)r->num_line_vertices);
}

/* a triangle draw's centroid relative to the render's root (host units) */
static void draw_centroid(const sa1_render *r, const sa1_draw *dr, float out[3])
{
    uint32_t t, k;
    out[0] = out[1] = out[2] = 0.0f;
    if (dr->count == 0) return;
    for (t = dr->first; t < dr->first + dr->count; t++)
        for (k = 0; k < 9; k++) out[k % 3] += r->positions[9u * t + k];
    for (k = 0; k < 3; k++) out[k] = out[k] / (3.0f * (float)dr->count) - r->position[k];
}

#define MAXD 512
typedef struct dlist {
    uint32_t n;
    int32_t arc[MAXD], tex[MAXD];
    uint32_t count[MAXD], flags[MAXD];
    float c[MAXD][3];
} dlist;

/* the triangle draws of a render (key and root-relative centroid), the game objects' left out */
static void draws_of(const sa1_render *r, dlist *L)
{
    uint32_t d;
    L->n = 0;
    for (d = 0; d < r->num_draws && L->n < MAXD; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->prim != SA1_PRIM_TRIANGLES || dr->count == 0)
            continue;
#ifdef SA1_DRAW_OBJECT
        if (dr->flags & SA1_DRAW_OBJECT)
            continue;
#endif
        L->arc[L->n] = dr->archive;
        L->tex[L->n] = dr->texture;
        L->count[L->n] = dr->count;
        L->flags[L->n] = dr->flags;
        draw_centroid(r, dr, L->c[L->n]);
        L->n++;
    }
}

static int same_key(const dlist *a, uint32_t i, const dlist *b, uint32_t j)
{
    return a->arc[i] == b->arc[j] && a->tex[i] == b->tex[j] && a->count[i] == b->count[j] &&
           (a->flags[i] & ~(uint32_t)SA1_DRAW_AFTERIMAGE) == (b->flags[j] & ~(uint32_t)SA1_DRAW_AFTERIMAGE);
}

/* the input of the review's runs with turns and jumps (visualsreview/rv_dump.c "turn") */
static void turn_input(uint32_t i, float *sx, float *sy, uint32_t *b)
{
    *sx = (float)sin(i * 0.05);
    *sy = (float)cos(i * 0.05);
    *b = (i % 40u) < 8u ? SA1_BUTTON_JUMP : 0u;
}

/* (5): Knuckles' punch sprites while he runs */
static void t_punch(sa1_context *ctx)
{
    sa1_character_id id = make_char(ctx, SA1_CHAR_KNUCKLES, 0.0f, 0.0f, 8.0f);
    uint32_t i, k, measured = 0, with_sprites = 0;
    double worst = 0.0, step_max = 0.0;
    printf("\n== punch: (5) the player's own draws new in a pair keep moving with the character (Knuckles runs and punches)\n");
    if (!id) {
        CHECK(0, "Knuckles created");
        return;
    }
    for (i = 0; i < 20; i++) tick(ctx, id, 0.0f, 0.0f, 0);
    for (i = 0; i < 180; i++) {
        const sa1_render *r = NULL;
        float s5[MAXCH][3], root5[3], root1[3];
        uint32_t n5 = 0, n1 = 0;
        sa1_lerp_stats ls;
        double off = 0.0;
        tick(ctx, id, 0.0f, 1.0f, (i % 12u) == 6u ? SA1_BUTTON_ACTION : 0u);
        drain(ctx);
        if (sa1_character_render(ctx, id, 0.5f, &r) != SA1_OK || r == NULL) continue;
        memcpy(root5, r->position, 12);
        for (k = 0; k < r->num_draws; k++) {
            const sa1_draw *dr = &r->draws[k];
            uint32_t s;
            if (dr->prim != SA1_PRIM_SPRITES || !(dr->archive >= 0) || (dr->flags & SA1_DRAW_SHADOW))
                continue;
#ifdef SA1_DRAW_OBJECT
            if (dr->flags & SA1_DRAW_OBJECT)
                continue;
#endif
            for (s = dr->first; s < dr->first + dr->count && n5 < MAXCH; s++) {
                s5[n5][0] = r->sprites[s].position[0] - root5[0];
                s5[n5][1] = r->sprites[s].position[1] - root5[1];
                s5[n5][2] = r->sprites[s].position[2] - root5[2];
                n5++;
            }
        }
        memset(&ls, 0, sizeof ls);
        ls.struct_size = sizeof ls;
        sa1_character_lerp_stats(ctx, id, &ls);
        if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) continue;
        memcpy(root1, r->position, 12);
        if (dist3f(root1, root5) * 2.0 > step_max) step_max = dist3f(root1, root5) * 2.0;
        for (k = 0; k < r->num_draws; k++) {
            const sa1_draw *dr = &r->draws[k];
            uint32_t s;
            if (dr->prim != SA1_PRIM_SPRITES || !(dr->archive >= 0) || (dr->flags & SA1_DRAW_SHADOW))
                continue;
#ifdef SA1_DRAW_OBJECT
            if (dr->flags & SA1_DRAW_OBJECT)
                continue;
#endif
            for (s = dr->first; s < dr->first + dr->count && n1 < MAXCH; s++) {
                float rel[3];
                rel[0] = r->sprites[s].position[0] - root1[0];
                rel[1] = r->sprites[s].position[1] - root1[1];
                rel[2] = r->sprites[s].position[2] - root1[2];
                if (n1 < n5) {
                    double d = dist3f(rel, s5[n1]);
                    if (d > off) off = d;
                }
                n1++;
            }
        }
        if (n1) with_sprites++;
        if (n1 == 0 || n1 != n5 || ls.same_topology)
            continue;       /* a pair that drew the same sprites as the one before lerps them (0.9.3a's rule) */
        measured++;
        if (off > worst) worst = off;
        if (g_verbose)
            printf("    pair %3u: %u own sprites new in the pair, root step %.1f HU: offset from their place relative to the "
                   "root at alpha 1: %.3f HU\n", (unsigned)i, (unsigned)n1, dist3f(root1, root5) * 2.0, off);
    }
    printf("    %u pairs with Knuckles' own sprites, %u of them drew other sprites than the pair before (measured); root "
           "step up to %.1f HU per pair; worst offset %.3f HU\n", (unsigned)with_sprites, (unsigned)measured, step_max,
           worst);
    CHECK(measured > 0 && step_max > 4.0 && worst < 1.0, "(5) Knuckles' punch sprites new in a pair while he runs keep "
          "moving with him (%u pairs, %.3f HU)", (unsigned)measured, worst);
    sa1_character_destroy(ctx, id);
    drain(ctx);
}

/* (7): at the ball -> body changes the character's new draws move with the root alike */
static void tear_run(sa1_context *ctx, int32_t kind, const char *name, uint32_t pairs)
{
    sa1_character_id id = make_char(ctx, kind, 0.0f, 0.0f, 8.0f);
    static dlist L1, L5, P;
    uint32_t i, k, changes = 0, measured = 0;
    double worst = 0.0;
    uint64_t h0 = g_hash;
    printf("\n-- %s runs with turns and jumps for %u pairs\n", name, (unsigned)pairs);
    if (!id) {
        CHECK(0, "%s created", name);
        return;
    }
    P.n = 0;
    for (i = 0; i < 20; i++) tick(ctx, id, 0.0f, 0.0f, 0);
    for (i = 0; i < pairs; i++) {
        const sa1_render *r = NULL;
        float sx, sy;
        uint32_t b, big = 0, nnew = 0, ref = 0;
        int big_new = 0;
        double spread = 0.0;
        turn_input(i, &sx, &sy, &b);
        tick(ctx, id, sx, sy, b);
        drain(ctx);
        if (sa1_character_render(ctx, id, 0.5f, &r) != SA1_OK || r == NULL) break;
        hash_render(r);
        draws_of(r, &L5);
        if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) break;
        hash_render(r);
        draws_of(r, &L1);
        if (L1.n == L5.n && P.n) {
            uint8_t isnew[MAXD];
            for (k = 0; k < L1.n; k++) {
                uint32_t j;
                isnew[k] = !(L1.flags[k] & SA1_DRAW_SHADOW);
                for (j = 0; j < P.n && isnew[k]; j++)
                    if (same_key(&L1, k, &P, j)) isnew[k] = 0;
                if (L1.count[k] > L1.count[big]) big = k;
                if (isnew[k] && (nnew == 0 || L1.count[k] > L1.count[ref])) ref = k;
                nnew += isnew[k];
            }
            big_new = isnew[big];
            if (big_new && nnew >= 2) {
                float sref[3];
                for (k = 0; k < 3; k++) sref[k] = L5.c[ref][k] - L1.c[ref][k];
                for (k = 0; k < L1.n; k++) {
                    float sh[3];
                    double d;
                    if (!isnew[k]) continue;
                    sh[0] = L5.c[k][0] - L1.c[k][0];
                    sh[1] = L5.c[k][1] - L1.c[k][1];
                    sh[2] = L5.c[k][2] - L1.c[k][2];
                    d = dist3f(sh, sref);
                    if (d > spread) spread = d;
                }
                changes++;
                measured += nnew;
                if (spread > worst) worst = spread;
                if (g_verbose)
                    printf("    pair %3u: the largest draw is new (%u new draws): spread of their shift %.3f HU\n",
                           (unsigned)i, (unsigned)nnew, spread);
            }
        }
        P = L1;
    }
    printf("    %u pairs where the largest draw is new (%u new draws in them): spread of their root-relative shift at alpha "
           "0.5 up to %.3f HU; render hash %016llX\n", (unsigned)changes, (unsigned)measured, worst,
           (unsigned long long)(g_hash ^ h0));
    CHECK(changes > 0 && worst < 1.0, "(7) %s: at the ball <-> body changes the new draws move with the root alike (%u "
          "pairs, spread %.3f HU, blobs excepted)", name, (unsigned)changes, worst);
    sa1_character_destroy(ctx, id);
    drain(ctx);
}

static void t_tear(sa1_context *ctx)
{
    printf("\n== tear: (7) ball <-> body changes while running\n");
    tear_run(ctx, SA1_CHAR_SONIC, "Sonic", 200);
    tear_run(ctx, SA1_CHAR_KNUCKLES, "Knuckles", 200);
}

/* (8): Tails' draws at alpha 0.5 against the midpoint of their alpha-1 places (pairs drawn like the one before) */
static void t_tails(sa1_context *ctx)
{
    sa1_character_id id = make_char(ctx, SA1_CHAR_TAILS, 0.0f, 0.0f, 8.0f);
    static dlist L1, L5, P;
    uint32_t i, k, pairs = 0;
    double worst = 0.0, worst_fade = 0.0;
    printf("\n== tails: (8) Tails runs with turns and jumps for 200 pairs\n");
    if (!id) {
        CHECK(0, "Tails created");
        return;
    }
    P.n = 0;
    for (i = 0; i < 20; i++) tick(ctx, id, 0.0f, 0.0f, 0);
    for (i = 0; i < 200; i++) {
        const sa1_render *r = NULL;
        float sx, sy;
        uint32_t b;
        int same = 1;
        turn_input(i, &sx, &sy, &b);
        tick(ctx, id, sx, sy, b);
        drain(ctx);
        if (sa1_character_render(ctx, id, 0.5f, &r) != SA1_OK || r == NULL) break;
        hash_render(r);
        draws_of(r, &L5);
        if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) break;
        hash_render(r);
        draws_of(r, &L1);
        if (P.n != L1.n || L5.n != L1.n) same = 0;
        for (k = 0; k < L1.n && same; k++)
            same = L1.arc[k] == P.arc[k] && L1.tex[k] == P.tex[k] && L1.count[k] == P.count[k] &&
                   L1.flags[k] == P.flags[k];
        if (same && L1.n) {
            pairs++;
            for (k = 0; k < L1.n; k++) {
                float mid[3];
                double d;
                if (L1.flags[k] & SA1_DRAW_SHADOW) continue;
                mid[0] = 0.5f * (P.c[k][0] + L1.c[k][0]);
                mid[1] = 0.5f * (P.c[k][1] + L1.c[k][1]);
                mid[2] = 0.5f * (P.c[k][2] + L1.c[k][2]);
                d = dist3f(L5.c[k], mid);
                if (d > worst) worst = d;
                if ((L1.flags[k] & SA1_DRAW_AFTERIMAGE) && d > worst_fade) worst_fade = d;
            }
        }
        P = L1;
    }
    printf("    %u pairs drawn like the pair before: a draw at alpha 0.5 off the midpoint of its alpha-1 places up to %.3f "
           "HU (the tail copies %.3f HU); 0.9.3a: %.3f HU\n", (unsigned)pairs, worst, worst_fade,
           (double)VISUALS_TAILS_MID_093A);
    CHECK(pairs > 50 && worst <= (double)VISUALS_TAILS_MID_093A + 0.5, "(8) Tails' tails and tail copies no farther from the "
          "midpoint of their alpha-1 places than in 0.9.3a + 0.5 HU (%.3f HU <= %.3f + 0.5)", worst,
          (double)VISUALS_TAILS_MID_093A);
    sa1_character_destroy(ctx, id);
    drain(ctx);
}

/* (9): the aura draws (additive, SUPERSONIC textures) of a dashing Super Sonic */
static void aura_run(sa1_context *ctx, const char *name, int32_t kind, uint32_t flags, int32_t rings, uint32_t super_flags)
{
    sa1_character_id id = make_char_ex(ctx, kind, 0.0f, 0.0f, 8.0f, flags, rings, 1, super_flags);
    int32_t a_ss = archive_named(ctx, kind, "SUPERSONIC");
    uint32_t i, k, pairs = 0, switches = 0, prev_sig = 0;
    double worst = 0.0, worst_switch = 0.0, step_max = 0.0;
    sa1_state_ex s;
    printf("\n-- %s dashes (archive SUPERSONIC %d)\n", name, (int)a_ss);
    if (!id || a_ss < 0) {
        CHECK(0, "%s created", name);
        return;
    }
    for (i = 0; i < 10; i++) tick(ctx, id, 0.0f, 0.0f, 0);
    for (i = 0; i < 150; i++) {
        const sa1_render *r = NULL;
        float c5[MAXD][3], root5[3];
        uint32_t n5 = 0, n1 = 0, sig = 2166136261u;
        double off = 0.0;
        tick(ctx, id, 0.0f, 1.0f, 0);
        drain(ctx);
        if (sa1_character_render(ctx, id, 0.5f, &r) != SA1_OK || r == NULL) break;
        hash_render(r);
        memcpy(root5, r->position, 12);
        for (k = 0; k < r->num_draws && n5 < MAXD; k++) {
            const sa1_draw *dr = &r->draws[k];
            if (dr->prim != SA1_PRIM_TRIANGLES || !(dr->flags & SA1_DRAW_ADDITIVE) || dr->archive != a_ss) continue;
            draw_centroid(r, dr, c5[n5++]);
        }
        if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) break;
        hash_render(r);
        if (dist3f(root5, r->position) * 2.0 > step_max) step_max = dist3f(root5, r->position) * 2.0;
        for (k = 0; k < r->num_draws; k++) {
            const sa1_draw *dr = &r->draws[k];
            float c1[3];
            if (dr->prim != SA1_PRIM_TRIANGLES || !(dr->flags & SA1_DRAW_ADDITIVE) || dr->archive != a_ss) continue;
            sig = (sig ^ (uint32_t)dr->texture) * 16777619u;
            sig = (sig ^ dr->count) * 16777619u;
            draw_centroid(r, dr, c1);
            if (n1 < n5) {
                double d = dist3f(c1, c5[n1]);
                if (d > off) off = d;
            }
            n1++;
        }
        if (n1 == 0 || n1 != n5) continue;
        pairs++;
        if (off > worst) worst = off;
        if (prev_sig && sig != prev_sig) {
            switches++;
            if (off > worst_switch) worst_switch = off;
        }
        prev_sig = sig;
    }
    state_of(ctx, id, &s);
    printf("    %u pairs with aura draws (%u model switches); root step up to %.1f HU per pair; an aura draw at alpha 0.5 off "
           "its place relative to the root at alpha 1: up to %.3f HU (%.3f HU in the switch pairs); speed %.0f HU/s\n",
           (unsigned)pairs, (unsigned)switches, step_max, worst, worst_switch,
           sqrt((double)s.velocity[0] * s.velocity[0] + (double)s.velocity[1] * s.velocity[1]));
    CHECK(pairs > 50 && switches > 0 && worst < 2.0, "(9) %s: every aura draw within 2 HU of its root-relative place at "
          "alpha 1 (%.3f HU; the switch pairs %.3f HU)", name, worst, worst_switch);
    sa1_character_destroy(ctx, id);
    drain(ctx);
}

static void t_aura(sa1_context *ctx)
{
    printf("\n== aura: (9) both Super Sonics of 0.9.3a dashing (auras are not OBJECT draws)\n");
    aura_run(ctx, "Super Sonic (Vs. Perfect Chaos, kind 8)", SA1_CHAR_SUPER_SONIC, 0u, -1, SA1_SUPER_NO_DRAIN);
    aura_run(ctx, "Super Sonic (General Gameplay)", SA1_CHAR_SONIC, SA1_CREATE_SUPER_CAPABLE, 50,
             SA1_SUPER_GENERAL | SA1_SUPER_START | SA1_SUPER_NO_DRAIN);
}

int main(int argc, char **argv)
{
    sa1_context *ctx;
    const char *gdi = NULL;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            if (g_nonly < 16) g_only[g_nonly++] = argv[++i];
        } else if (strcmp(argv[i], "--verbose") == 0) {
            g_verbose = 1;
        } else {
            gdi = argv[i];
        }
    }
    if (gdi == NULL) gdi = getenv("SA1_TEST_GDI");
    if (gdi == NULL) {
        printf("usage: visuals_test [--only rings|groups|drops|lying|punch|tear|tails|aura]... [--verbose] GDI\n");
        return 2;
    }
    printf("visuals_test: libsa1 %s; SA1_DRAW_OBJECT %s\n", sa1_version_string(),
#ifdef SA1_DRAW_OBJECT
           "in this build"
#else
           "NOT in this build (an older library: the game objects are not told apart)"
#endif
    );
    ctx = make_ctx(gdi);
    if (ctx == NULL) return 1;
    if (part_on("rings")) t_rings(ctx);
    if (part_on("groups")) t_groups(ctx);
    if (part_on("drops")) t_drops(ctx);
    if (part_on("lying")) t_lying(ctx);
    if (part_on("punch")) t_punch(ctx);
    g_hash = 1469598103934665603ull;
    if (part_on("tear")) t_tear(ctx);
    if (part_on("tails")) t_tails(ctx);
    if (part_on("aura")) t_aura(ctx);
    printf("\nHASH render of the tear / tails / aura runs (no game object in them): %016llX\n", (unsigned long long)g_hash);
    printf("%d checks, %d failed\n", g_checks, g_fail);
    sa1_shutdown(ctx);
    return g_fail ? 1 : 0;
}
