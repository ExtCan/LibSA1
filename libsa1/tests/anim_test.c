/*
 * anim_test.c - round 0.9.9a (099a-anim): a class check of what the character render draws, so that a class of break cannot hide (sadc build/research/r099a/anim/diagnosis.md; the owner's N1, N4, N5, N6).
 *
 *   anim_test [--only CASES] [--tsv FILE] [--verbose] GDI     (GDI: or $SA1_TEST_GDI)   cases: gameplay, catalogue, upgrades, follower, knuckles_names
 *
 * Built and run by tests/build_anim.py (a snapshot of build/<target>/libsa1.a and LIB's headers; build.py --test does not run it). The scene is emote_test's: native coordinates, a flat GRID floor, characters made
 * with SA1_CREATE_WORLD, one pair per tick. Everything is measured on sa1_character_render's output, the triangles a host draws, at several alphas (0.25, 0.5, 0.75: between two pairs; 1: the pair).
 *
 * Measures, against the character's standing pose (its body triangles in the settled stand; the same triangle of the same draw):
 *   edge    the worst lengthening of a triangle's edge (units)
 *   seam    the worst opening of corners of different triangles that coincide in the stand
 *   added   what the interpolation adds between two pairs, above the larger of the two pairs around it
 *   detour  the length of a vertex's way in a transition above what any arc up to half a turn needs (a whole turn of the body has no straight line at all)
 *   bad     a NaN or an infinity in a vertex
 * Every entry of every list of every kind, with and without the kind's upgrades, at the game's rate and at 0.5x: entering, playing, held, leaving by a stop with a blend, leaving by the stick. The poses of steady play are
 * what the game's own display draws: recorded in the notes and the TSV, not limited (a game's own pose is kept, whatever it looks like).
 * Fails on the 0.9.9 archive (build/research/r099a/anim/base099.log): Eggman's seam 12.75 between the pairs (the motion hint); leaving: Big's seam 29.15, edge 16.36 (the queue's link: the scale blend), Tails' whole turn; Sonic's brake 4.02.
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
#include "sa1_emote.h"
#include "test_util.h"

static int g_fail, g_ok, g_verbose;
static const char *g_gdi;
static FILE *g_tsv;
static sa1_context *g_ctx;

#define CHECK(cond, ...)                                                                                                \
    do {                                                                                                                \
        int c_ = (cond) ? 1 : 0;                                                                                        \
        printf(c_ ? "  ok    " : "  FAIL  ");                                                                           \
        printf(__VA_ARGS__);                                                                                            \
        printf("\n");                                                                                                   \
        if (c_) {                                                                                                       \
            g_ok++;                                                                                                     \
        } else {                                                                                                        \
            g_fail++;                                                                                                   \
        }                                                                                                               \
    } while (0)

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level <= SA1_LOG_ERROR) {
        printf("    [libsa1] %s\n", msg);
    }
}

/* ---- the scene ---------------------------------------------------------------------------------------------------- */
static int scene(void)
{
    sa1_collision_tri t[2];
    float q[4][3] = { { -3000, 0, -3000 }, { -3000, 0, 3000 }, { 3000, 0, 3000 }, { 3000, 0, -3000 } };
    memset(t, 0, sizeof t);
    memcpy(t[0].v[0], q[0], 12);
    memcpy(t[0].v[1], q[1], 12);
    memcpy(t[0].v[2], q[2], 12);
    memcpy(t[1].v[0], q[0], 12);
    memcpy(t[1].v[1], q[2], 12);
    memcpy(t[1].v[2], q[3], 12);
    t[0].surface = t[1].surface = SA1_SURFACE_SOLID;
    return SA1_FAILED(sa1_collision_set_static(g_ctx, t, 2)) ? -1 : 0;
}

#define MK_GENERAL  1
#define MK_UPGRADES 2

/* the game's event flags of each kind's upgrades (G-SA1's table, session.cpp kUpgrades): written into the world's B_8C799920 before its first step, where PInitialize reads them */
#define ADDR_EVENT_FLAGS 0x8C799920u

static void set_upgrades(sa1_character_id id, int32_t kind)
{
    static const struct { int32_t kind; uint16_t flag; } k[] = {
        { SA1_CHAR_SONIC, 0x8D }, { SA1_CHAR_SONIC, 0x8E }, { SA1_CHAR_SONIC, 0x9F }, { SA1_CHAR_TAILS, 0xCD }, { SA1_CHAR_TAILS, 0xDB }, { SA1_CHAR_KNUCKLES, 0x119 }, { SA1_CHAR_KNUCKLES, 0x11A },
        { SA1_CHAR_AMY, 0x152 }, { SA1_CHAR_AMY, 0x15E }, { SA1_CHAR_GAMMA, 0x189 }, { SA1_CHAR_GAMMA, 0x18A }, { SA1_CHAR_BIG, 0x1D0 }, { SA1_CHAR_BIG, 0x1D1 }, { SA1_CHAR_BIG, 0x1DE }, { SA1_CHAR_BIG, 0x1DF }, { SA1_CHAR_BIG, 0x1E0 }, { SA1_CHAR_BIG, 0x1E1 },
    };
    struct sa1g_world *w = sa1_character_world(g_ctx, id);
    size_t i;
    if (w == NULL) {
        return;
    }
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (k[i].kind == (kind == SA1_CHAR_SUPER_SONIC ? SA1_CHAR_SONIC : kind)) {
            uint32_t a = ADDR_EVENT_FLAGS + k[i].flag, word = a & ~3u, v = 0, shift = 8u * (a & 3u);
            sa1g_world_rd32(w, word, &v);
            sa1g_world_wr32(w, word, (v & ~(0xFFu << shift)) | (1u << shift));
        }
    }
}

static sa1_character_id mk(int32_t kind, int how)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_START_RINGS;
    d.position[0] = 100.0f;
    d.position[1] = 0.5f;
    d.position[2] = 100.0f;
    d.start_rings = 60;
    if (kind == SA1_CHAR_SUPER_SONIC) {
        d.flags |= SA1_CREATE_SUPER;
        d.super_flags = SA1_SUPER_NO_DRAIN;
    } else if (how & MK_GENERAL) {
        d.flags |= SA1_CREATE_SUPER | SA1_CREATE_SUPER_CAPABLE;
        d.super_flags = SA1_SUPER_GENERAL | SA1_SUPER_NO_DRAIN | SA1_SUPER_START;
    }
    st = sa1_character_create_ex(g_ctx, &d, &id);
    if (SA1_FAILED(st)) {
        printf("  create kind %d: %s: %s\n", (int)kind, sa1_status_string(st), sa1_last_error(g_ctx));
        return 0;
    }
    if (how & MK_UPGRADES) {
        set_upgrades(id, kind);
    }
    return id;
}

static void poll_all(void)
{
    sa1_event ev[64];
    uint32_t n;
    do {
        n = 0;
        sa1_poll_events(g_ctx, ev, 64, &n);
    } while (n == 64);
}

static int tick(sa1_character_id id, float sx, float sy, uint32_t buttons)
{
    sa1_input in;
    sa1_status st;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    st = sa1_character_tick(g_ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    poll_all();
    if (st != SA1_OK) {
        printf("  tick: %s: %s\n", sa1_status_string(st), sa1_last_error(g_ctx));
        return -1;
    }
    return 0;
}

static void xstate(sa1_character_id id, sa1_state_ex *x)
{
    memset(x, 0, sizeof *x);
    x->struct_size = sizeof *x;
    sa1_character_state_ex(g_ctx, id, x);
}

/* ---- the measure -------------------------------------------------------------------------------------------------- */
typedef struct ref_t {
    uint32_t nt;                /* body triangles */
    uint32_t *tri;              /* their indices in the render's arrays */
    float *pos;                 /* 9 per body triangle: the standing pose, relative to the drawn root */
    uint32_t ndraw;
    uint32_t *dsig;             /* 2 per body draw: first, count */
    uint32_t *seam;             /* 2 per corner pair: body corner a, b (3 * t + k) that coincide in the stand, on different triangles */
    uint32_t nseam;
    float span;                 /* the longest edge in the stand */
} ref_t;

typedef struct meas_t {
    float edge_pair, ratio_pair;    /* the worst lengthening of an edge against the stand at the pairs (the pose the game's display drew, or a bridge's) */
    float gap_pair;                 /* the worst seam opening at the pairs */
    float edge_btw, ratio_btw;      /* the same between the pairs (0.25, 0.5, 0.75) */
    float gap_btw;
    float ex_edge, ex_gap;          /* what the interpolation added: between the pairs, above the larger of the two pairs around it */
    uint32_t bad, mismatch, renders;
    uint32_t pair_edge, pair_gap, pair_ex;   /* when */
    float prev_edge, prev_gap;      /* the pair before (the running state of a sweep) */
    int have_prev;
} meas_t;

static int is_body(const sa1_draw *d)
{
    return d->prim == SA1_PRIM_TRIANGLES && d->source == SA1_SOURCE_CHARACTER &&
           !(d->flags & (SA1_DRAW_SHADOW | SA1_DRAW_EFFECT | SA1_DRAW_AFTERIMAGE | SA1_DRAW_OBJECT | SA1_DRAW_BOARD));
}

static void ref_free(ref_t *r)
{
    free(r->tri);
    free(r->pos);
    free(r->dsig);
    free(r->seam);
    memset(r, 0, sizeof *r);
}

typedef struct corner_t {
    float p[3];
    uint32_t c;
} corner_t;

static int corner_cmp(const void *a, const void *b)
{
    const corner_t *x = (const corner_t *)a, *y = (const corner_t *)b;
    int k;
    for (k = 0; k < 3; k++) {
        if (x->p[k] != y->p[k]) {
            return x->p[k] < y->p[k] ? -1 : 1;
        }
    }
    return 0;
}

static int body_sig(const sa1_render *rd, const ref_t *r)
{
    uint32_t i, n = 0;
    for (i = 0; i < rd->num_draws; i++) {
        const sa1_draw *d = &rd->draws[i];
        if (!is_body(d)) {
            continue;
        }
        if (n >= r->ndraw || r->dsig[2u * n] != d->first || r->dsig[2u * n + 1u] != d->count) {
            return 0;
        }
        n++;
    }
    return n == r->ndraw;
}

static int ref_take(sa1_character_id id, ref_t *r)
{
    const sa1_render *rd = NULL;
    uint32_t i, t, k, n = 0, nc, a, b, cap = 0;
    corner_t *cs;
    memset(r, 0, sizeof *r);
    if (sa1_character_render(g_ctx, id, 1.0f, &rd) != SA1_OK || rd == NULL) {
        return -1;
    }
    for (i = 0; i < rd->num_draws; i++) {
        if (is_body(&rd->draws[i])) {
            n += rd->draws[i].count;
            r->ndraw++;
        }
    }
    r->nt = n;
    r->tri = (uint32_t *)malloc(sizeof(uint32_t) * (n + 1u));
    r->pos = (float *)malloc(sizeof(float) * 9u * (n + 1u));
    r->dsig = (uint32_t *)malloc(sizeof(uint32_t) * 2u * (r->ndraw + 1u));
    n = 0;
    r->ndraw = 0;
    for (i = 0; i < rd->num_draws; i++) {
        const sa1_draw *d = &rd->draws[i];
        if (!is_body(d)) {
            continue;
        }
        r->dsig[2u * r->ndraw] = d->first;
        r->dsig[2u * r->ndraw + 1u] = d->count;
        r->ndraw++;
        for (t = d->first; t < d->first + d->count; t++) {
            r->tri[n] = t;
            for (k = 0; k < 9u; k++) {
                r->pos[9u * n + k] = rd->positions[9u * t + k] - rd->position[k % 3u];
            }
            n++;
        }
    }
    for (t = 0; t < r->nt; t++) {
        for (k = 0; k < 3u; k++) {
            const float *p = &r->pos[9u * t + 3u * k], *q = &r->pos[9u * t + 3u * ((k + 1u) % 3u)];
            float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2], l = sqrtf(dx * dx + dy * dy + dz * dz);
            r->span = l > r->span ? l : r->span;
        }
    }
    /* the seams: corners that coincide, on different triangles */
    nc = 3u * r->nt;
    cs = (corner_t *)malloc(sizeof(corner_t) * (nc + 1u));
    for (i = 0; i < nc; i++) {
        memcpy(cs[i].p, &r->pos[3u * i], 12);
        cs[i].c = i;
    }
    qsort(cs, nc, sizeof(corner_t), corner_cmp);
    for (a = 0; a < nc; a = b) {
        for (b = a + 1u; b < nc; b++) {
            float dx = cs[b].p[0] - cs[a].p[0], dy = cs[b].p[1] - cs[a].p[1], dz = cs[b].p[2] - cs[a].p[2];
            if (dx * dx + dy * dy + dz * dz > 1e-6f) {
                break;
            }
        }
        for (i = a + 1u; i < b; i++) {
            if (cs[i].c / 3u == cs[a].c / 3u) {
                continue;
            }
            if (r->nseam >= cap) {
                cap = cap ? 2u * cap : 4096u;
                r->seam = (uint32_t *)realloc(r->seam, sizeof(uint32_t) * 2u * cap);
            }
            r->seam[2u * r->nseam] = cs[a].c;
            r->seam[2u * r->nseam + 1u] = cs[i].c;
            r->nseam++;
        }
    }
    free(cs);
    return 0;
}

static void meas_init(meas_t *m)
{
    memset(m, 0, sizeof *m);
}

/* the phase's worst values, the running state stays the sweep's */
static void meas_phase(meas_t *to, const meas_t *cur)
{
    to->prev_edge = cur->prev_edge;
    to->prev_gap = cur->prev_gap;
    to->have_prev = cur->have_prev;
}

static const float k_alphas[4] = { 0.25f, 0.5f, 0.75f, 1.0f };

/* one pair at the alphas: the pair's values first (alpha 1), then the ones between, the excess above the pairs around them */
static void measure(sa1_character_id id, const ref_t *r, uint32_t pair, meas_t *m)
{
    int ai;
    float pe = 0.0f, pg = 0.0f, be = 0.0f, bg = 0.0f, pr = 0.0f, br = 0.0f;
    int ok_pair = 0, ok_btw = 0;
    for (ai = 3; ai >= 0; ai--) {
        const sa1_render *rd = NULL;
        int w = k_alphas[ai] >= 1.0f;
        uint32_t i, k;
        const float *pos;
        float e = 0.0f, ra = 0.0f, g = 0.0f;
        if (sa1_character_render(g_ctx, id, k_alphas[ai], &rd) != SA1_OK || rd == NULL) {
            m->bad++;
            continue;
        }
        m->renders++;
        if (!body_sig(rd, r)) {
            m->mismatch++;
            continue;
        }
        pos = rd->positions;
        for (i = 0; i < r->nt; i++) {
            const float *a = &pos[9u * r->tri[i]], *b = &r->pos[9u * i];
            for (k = 0; k < 9u; k++) {
                if (!isfinite(a[k])) {
                    m->bad++;
                    break;
                }
            }
            for (k = 0; k < 3u; k++) {
                const float *p = a + 3u * k, *q = a + 3u * ((k + 1u) % 3u);
                const float *p0 = b + 3u * k, *q0 = b + 3u * ((k + 1u) % 3u);
                float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2], l = sqrtf(dx * dx + dy * dy + dz * dz);
                float ex = p0[0] - q0[0], ey = p0[1] - q0[1], ez = p0[2] - q0[2], l0 = sqrtf(ex * ex + ey * ey + ez * ez);
                if (l - l0 > e) {
                    e = l - l0;
                    ra = l / (l0 > 0.01f ? l0 : 0.01f);
                }
            }
        }
        for (i = 0; i < r->nseam; i++) {
            uint32_t ca = r->seam[2u * i], cb = r->seam[2u * i + 1u];
            const float *p = &pos[9u * r->tri[ca / 3u] + 3u * (ca % 3u)], *q = &pos[9u * r->tri[cb / 3u] + 3u * (cb % 3u)];
            float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2], l = sqrtf(dx * dx + dy * dy + dz * dz);
            g = l > g ? l : g;
        }
        if (w) {
            pe = e;
            pr = ra;
            pg = g;
            ok_pair = 1;
        } else {
            if (e > be) {
                be = e;
                br = ra;
            }
            bg = g > bg ? g : bg;
            ok_btw = 1;
        }
    }
    if (ok_pair) {
        if (pe > m->edge_pair) {
            m->edge_pair = pe;
            m->ratio_pair = pr;
            m->pair_edge = pair;
        }
        if (pg > m->gap_pair) {
            m->gap_pair = pg;
            m->pair_gap = pair;
        }
    }
    if (ok_btw) {
        if (be > m->edge_btw) {
            m->edge_btw = be;
            m->ratio_btw = br;
        }
        m->gap_btw = bg > m->gap_btw ? bg : m->gap_btw;
    }
    if (ok_pair && ok_btw && m->have_prev) {
        float xe = be - (pe > m->prev_edge ? pe : m->prev_edge), xg = bg - (pg > m->prev_gap ? pg : m->prev_gap);
        if (xe > m->ex_edge) {
            m->ex_edge = xe;
            m->pair_ex = pair;
        }
        if (xg > m->ex_gap) {
            m->ex_gap = xg;
            m->pair_ex = pair;
        }
    }
    m->have_prev = ok_pair;
    m->prev_edge = pe;
    m->prev_gap = pg;
}

/* ---- the path of a transition: how far a vertex leaves the straight line between where it started and where it ended ------------------------------- */
#define TRACK_MAX 64u
typedef struct track_t {
    float *f[TRACK_MAX];
    uint32_t n, nt;
} track_t;

static void track_init(track_t *t, const ref_t *r)
{
    memset(t, 0, sizeof *t);
    t->nt = r->nt;
}

static void track_add(track_t *t, sa1_character_id id, const ref_t *r)
{
    const sa1_render *rd = NULL;
    uint32_t i, k;
    float *o;
    if (t->n >= TRACK_MAX || sa1_character_render(g_ctx, id, 1.0f, &rd) != SA1_OK || rd == NULL || !body_sig(rd, r)) {
        return;
    }
    o = (float *)malloc(sizeof(float) * 9u * (r->nt + 1u));
    for (i = 0; i < r->nt; i++) {
        for (k = 0; k < 9u; k++) {
            o[9u * i + k] = rd->positions[9u * r->tri[i] + k] - rd->position[k % 3u];
        }
    }
    t->f[t->n++] = o;
}

/* the worst detour of a vertex: the length of its way, pair by pair, above what any arc up to half a turn needs from where it started to where it ended (half a turn: pi / 2 x the straight line, 1.6). A limb's
   arc adds nothing; a whole turn of the body has no straight line at all and adds 6 r */
static float track_sweep(const track_t *t)
{
    uint32_t a, i;
    float worst = 0.0f;
    if (t->n < 3u) {
        return 0.0f;
    }
    for (i = 0; i < 3u * t->nt; i++) {
        const float *s = &t->f[0][3u * i], *e = &t->f[t->n - 1u][3u * i];
        float dx = e[0] - s[0], dy = e[1] - s[1], dz = e[2] - s[2], len = 0.0f, chord = sqrtf(dx * dx + dy * dy + dz * dz);
        for (a = 1; a < t->n; a++) {
            const float *p = &t->f[a - 1u][3u * i], *q = &t->f[a][3u * i];
            float qx = q[0] - p[0], qy = q[1] - p[1], qz = q[2] - p[2];
            len += sqrtf(qx * qx + qy * qy + qz * qz);
        }
        worst = len - 1.6f * chord > worst ? len - 1.6f * chord : worst;
    }
    return worst;
}

static void track_free(track_t *t)
{
    uint32_t i;
    for (i = 0; i < t->n; i++) {
        free(t->f[i]);
    }
    t->n = 0;
}

/* ---- the catalogue sweep ------------------------------------------------------------------------------------------- */
enum { PH_ENTER, PH_PLAY, PH_HOLD, PH_LEAVE, PH_CANCEL, PH_COUNT };
static const char *const k_ph[PH_COUNT] = { "enter", "play", "hold", "leave", "cancel" };

typedef struct cat_list {
    int32_t kind;
    uint32_t form;
    int how;
    const char *name;
} cat_list;

static const cat_list k_cat[8] = {
    { SA1_CHAR_SONIC, SA1_EMOTE_FORM_NORMAL, 0, "Sonic" }, { SA1_CHAR_TAILS, 0, 0, "Tails" },
    { SA1_CHAR_KNUCKLES, 0, 0, "Knuckles" },               { SA1_CHAR_AMY, 0, 0, "Amy" },
    { SA1_CHAR_GAMMA, 0, 0, "E-102" },                     { SA1_CHAR_BIG, 0, 0, "Big" },
    { SA1_CHAR_SONIC, SA1_EMOTE_FORM_SUPER, MK_GENERAL, "SuperGeneral" }, { SA1_CHAR_SUPER_SONIC, 0, 0, "SuperSonic" },
};

/* the limits of the class check, SA1 units; characters are 10-20 units tall. What a pair of a transition may show: a seam open below this (at the first pairs of a bridge from a pose whose own
   welds the stand does not share); what the interpolation may add between two pairs; how long a vertex's way in a transition may be above the straight line (a limb's arc is fine, a turn of the whole body is not) */
#define LIM_GAP_PAIR   0.75f
#define LIM_EX_GAP     0.75f
#define LIM_EX_EDGE    2.00f
#define LIM_EDGE_PAIR  1.00f
#define LIM_SWEEP      35.0f   /* every entry: the long tail of Tails' attack rows has 33 on a clean way back */
#define LIM_SWEEP_POSE 12.0f   /* the top of a wheel: 0.9.9 had 18-41 here, a clean bridge 3 */

static sa1_status start_entry(sa1_character_id id, const sa1_emote_info *info, float speed, uint32_t flags, int play)
{
    sa1_emote e;
    memset(&e, 0, sizeof e);
    e.struct_size = sizeof e;
    e.count = 1;
    e.items[0].source = info->source;
    e.items[0].index = info->index;
    e.items[0].play = play;
    e.items[0].speed = speed;
    e.link = -1;
    e.flags = flags;
    return sa1_character_emote(g_ctx, id, &e);
}

typedef struct worst_t {
    float v;
    char name[48];
} worst_t;

static void worst_set(worst_t *w, float v, const char *name)
{
    if (v > w->v) {
        w->v = v;
        snprintf(w->name, sizeof w->name, "%s", name);
    }
}

/* round 100-EV: the rows and extras of a list - sa1_emote_count less the cutscene animations at its end (SA1_EMOTE_EVENT:
   loaded from the disc before they play; libsa1's emote_test T3 / T7 play them) */
static uint32_t emote_base_count(const sa1_context *c, int32_t kind, uint32_t form)
{
    uint32_t n = sa1_emote_count(c, kind, form);
    while (n > 0) {
        sa1_emote_info info;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        if (sa1_emote_get(c, kind, form, n - 1u, &info) != SA1_OK || info.source != SA1_EMOTE_EVENT) {
            break;
        }
        n--;
    }
    return n;
}

static void sweep_list(const cat_list *L, float speed)
{
    sa1_character_id id = mk(L->kind, L->how);
    uint32_t n, i, p, plays = 0;
    ref_t ref;
    worst_t w_gap_leave = { 0 }, w_edge_leave = { 0 }, w_ex_gap = { 0 }, w_ex_edge = { 0 }, w_sweep = { 0 }, w_gap_play = { 0 }, w_edge_play = { 0 }, w_gap_cancel = { 0 }, w_sweep_cancel = { 0 };
    uint32_t bad = 0, refused = 0;
    if (!id) {
        CHECK(0, "%s: world", L->name);
        return;
    }
    for (p = 0; p < 12; p++) {
        tick(id, 0, 0, 0);
    }
    if (ref_take(id, &ref) != 0) {
        CHECK(0, "%s: the stand", L->name);
        return;
    }
    n = emote_base_count(g_ctx, L->kind, L->form);
    for (i = 0; i < n; i++) {
        sa1_emote_info info;
        sa1_status st = SA1_ERR_UNSUPPORTED;
        meas_t ph[PH_COUNT], run;
        track_t tr;
        uint32_t tries, pair = 0, maxp, flags;
        int loop, q;
        float sweep, sweep_cancel = 0.0f, own_edge, own_gap;
        sa1_state_ex x;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        sa1_emote_get(g_ctx, L->kind, L->form, i, &info);
        loop = info.play == SA1_EMOTE_PLAY_LOOP;
        flags = loop ? 0u : SA1_EMOTE_HOLD;
        for (q = 0; q < PH_COUNT; q++) {
            meas_init(&ph[q]);
        }
        meas_init(&run);
        for (tries = 0; tries < 60 && st == SA1_ERR_UNSUPPORTED; tries++) {
            st = start_entry(id, &info, speed, flags, SA1_EMOTE_PLAY_DEFAULT);
            if (st == SA1_ERR_UNSUPPORTED) {
                tick(id, 0, 0, 0);
            }
        }
        if (st != SA1_OK) {
            refused++;
            printf("  %u %s: %s (%s)\n", (unsigned)i, info.internal_name, sa1_status_string(st), sa1_last_error(g_ctx));
            continue;
        }
        plays++;
        maxp = (info.frames ? (uint32_t)((float)info.frames / (speed * 0.2f)) : 200u) + 40u;
        maxp = maxp > 900u ? 900u : maxp;
        for (pair = 0; pair < maxp; pair++) {
            tick(id, 0, 0, 0);
            xstate(id, &x);
            q = pair < 12u ? PH_ENTER : PH_PLAY;
            meas_phase(&ph[q], &run);
            measure(id, &ref, pair, &ph[q]);
            meas_phase(&run, &ph[q]);
            if (x.emote_state == SA1_EMOTE_STATE_HOLDING || x.emote_state == SA1_EMOTE_STATE_NONE) {
                break;
            }
            if (loop && pair >= 60u) {
                break;
            }
        }
        for (p = 0; p < 4u; p++) {
            tick(id, 0, 0, 0);
            meas_phase(&ph[PH_HOLD], &run);
            measure(id, &ref, pair + p, &ph[PH_HOLD]);
            meas_phase(&run, &ph[PH_HOLD]);
        }
        pair += 4u;
        own_edge = fmaxf(ph[PH_HOLD].edge_pair, 0.0f);
        own_gap = fmaxf(ph[PH_HOLD].gap_pair, 0.0f);
        /* leaving: a stop with a blend */
        track_init(&tr, &ref);
        track_add(&tr, id, &ref);
        sa1_character_emote_stop(g_ctx, id, SA1_EMOTE_STOP_BLEND);
        for (p = 0; p < 400u; p++) {
            tick(id, 0, 0, 0);
            meas_phase(&ph[PH_LEAVE], &run);
            measure(id, &ref, pair + p, &ph[PH_LEAVE]);
            meas_phase(&run, &ph[PH_LEAVE]);
            track_add(&tr, id, &ref);
            xstate(id, &x);
            if (x.emote_state == SA1_EMOTE_STATE_NONE) {
                break;
            }
        }
        for (q = 0; q < 2; q++) {
            tick(id, 0, 0, 0);
            track_add(&tr, id, &ref);
        }
        sweep = track_sweep(&tr);
        track_free(&tr);
        /* leaving: the stick (a cancel), then stand again */
        st = start_entry(id, &info, speed, flags, SA1_EMOTE_PLAY_DEFAULT);
        if (st == SA1_OK) {
            meas_init(&run);
            for (p = 0; p < 16u; p++) {
                tick(id, 0, 0, 0);
            }
            track_init(&tr, &ref);
            track_add(&tr, id, &ref);
            for (p = 0; p < 12u; p++) {
                tick(id, 0, -0.3f, 0);
                meas_phase(&ph[PH_CANCEL], &run);
                measure(id, &ref, p, &ph[PH_CANCEL]);
                meas_phase(&run, &ph[PH_CANCEL]);
                track_add(&tr, id, &ref);
            }
            sweep_cancel = track_sweep(&tr);
            track_free(&tr);
            for (p = 0; p < 60u; p++) {
                tick(id, 0, 0, 0);
                xstate(id, &x);
                if (p > 20u && x.mode == (L->kind == SA1_CHAR_SUPER_SONIC ? 71 : 1)) {
                    break;
                }
            }
        }
        if (g_tsv) {
            fprintf(g_tsv, "%s\t%g\t%u\t%s\t%c%u\t%s", L->name, (double)speed, (unsigned)i, info.internal_name, info.source == SA1_EMOTE_EXTRA ? 'x' : 'r', (unsigned)info.index, loop ? "loop" : "once");
            for (q = 0; q < PH_COUNT; q++) {
                fprintf(g_tsv, "\t%.3f\t%.2f\t%.3f\t%.2f\t%.3f\t%.3f\t%.3f\t%.3f\t%u\t%u", (double)ph[q].edge_pair, (double)ph[q].ratio_pair, (double)ph[q].edge_btw, (double)ph[q].ratio_btw, (double)ph[q].gap_pair, (double)ph[q].gap_btw, (double)ph[q].ex_edge,
                        (double)ph[q].ex_gap, (unsigned)ph[q].bad, (unsigned)ph[q].mismatch);
            }
            fprintf(g_tsv, "\t%.3f\t%.3f\n", (double)sweep, (double)sweep_cancel);
        }
        for (q = 0; q < PH_COUNT; q++) {
            bad += ph[q].bad;
            worst_set(&w_ex_gap, ph[q].ex_gap, info.internal_name);
            worst_set(&w_ex_edge, ph[q].ex_edge, info.internal_name);
        }
        worst_set(&w_gap_play, fmaxf(ph[PH_PLAY].gap_pair, ph[PH_HOLD].gap_pair), info.internal_name);
        worst_set(&w_edge_play, fmaxf(ph[PH_PLAY].edge_pair, ph[PH_HOLD].edge_pair), info.internal_name);
        if (ph[PH_HOLD].mismatch == 0u) {
            /* (the ball's rows: other triangles than the stand's, no reference at the hold) */
            worst_set(&w_gap_leave, ph[PH_LEAVE].gap_pair - own_gap, info.internal_name);
            worst_set(&w_edge_leave, ph[PH_LEAVE].edge_pair - own_edge, info.internal_name);
        }
        worst_set(&w_sweep, sweep, info.internal_name);
        worst_set(&w_gap_cancel, ph[PH_CANCEL].gap_pair, info.internal_name);
        worst_set(&w_sweep_cancel, sweep_cancel, info.internal_name);
        if (g_verbose) {
            printf("  %3u %-22s %s:", (unsigned)i, info.internal_name, loop ? "loop" : "once");
            for (q = 0; q < PH_COUNT; q++) {
                printf(" %s %.2f/%.2f g%.2f/%.2f", k_ph[q], (double)ph[q].edge_pair, (double)ph[q].edge_btw, (double)ph[q].gap_pair, (double)ph[q].gap_btw);
            }
            printf(" sweep %.2f %.2f\n", (double)sweep, (double)sweep_cancel);
        }
    }
    printf("%s x%.2f: %u entries played (%u refused); the stand: %u body triangles in %u draws, %u seams\n", L->name, (double)speed, (unsigned)plays, (unsigned)refused, (unsigned)ref.nt, (unsigned)ref.ndraw, (unsigned)ref.nseam);
    printf("  note  in play (the game's own display, recorded, not limited): edge %.2f (%s), seam %.2f (%s)\n", (double)w_edge_play.v, w_edge_play.name, (double)w_gap_play.v, w_gap_play.name);
    CHECK(bad == 0 && refused == 0, "%s: no NaN, no refusal", L->name);
    CHECK(w_ex_gap.v <= LIM_EX_GAP, "%s: the interpolation opens no seam between two pairs: at most %.2f above the pairs (%s), limit %.2f", L->name, (double)w_ex_gap.v, w_ex_gap.name, (double)LIM_EX_GAP);
    CHECK(w_ex_edge.v <= LIM_EX_EDGE, "%s: the interpolation stretches no edge between two pairs: at most %.2f above the pairs (%s), limit %.2f", L->name, (double)w_ex_edge.v, w_ex_edge.name, (double)LIM_EX_EDGE);
    CHECK(w_gap_leave.v <= LIM_GAP_PAIR, "%s: leaving, no seam opens at a pair: at most %.2f above the held pose (%s), limit %.2f", L->name, (double)w_gap_leave.v, w_gap_leave.name, (double)LIM_GAP_PAIR);
    CHECK(w_edge_leave.v <= LIM_EDGE_PAIR, "%s: leaving, no edge stretches at a pair: at most %.2f above the held pose (%s), limit %.2f", L->name, (double)w_edge_leave.v, w_edge_leave.name, (double)LIM_EDGE_PAIR);
    CHECK(w_sweep.v <= LIM_SWEEP, "%s: leaving, no vertex takes a detour: at most %.2f (%s), limit %.2f (a whole turn of the body is 40 or more)", L->name, (double)w_sweep.v, w_sweep.name, (double)LIM_SWEEP);
    printf("  note  a stick cancel: seam %.2f (%s), detour %.2f (%s)\n", (double)w_gap_cancel.v, w_gap_cancel.name, (double)w_sweep_cancel.v, w_sweep_cancel.name);
    ref_free(&ref);
    sa1_character_destroy(g_ctx, id);
}

/* ---- gameplay ---------------------------------------------------------------------------------------------------- */
static void gameplay_one(const char *name, int32_t kind, int how)
{
    sa1_character_id id = mk(kind, how);
    ref_t ref;
    uint32_t p, seg;
    float wg = 0.0f, we = 0.0f;
    static const struct { const char *what; float sx, sy; uint32_t pairs; } script[] = {
        { "start", 0, -0.35f, 30 }, { "run", 0, -1.0f, 90 }, { "brake", 0, 1.0f, 15 }, { "stop", 0, 0, 40 }, { "turn", 1.0f, 0, 30 }, { "stop2", 0, 0, 40 },
    };
    if (!id) {
        CHECK(0, "%s: world", name);
        return;
    }
    for (p = 0; p < 20; p++) {
        tick(id, 0, 0, 0);
    }
    if (ref_take(id, &ref) != 0) {
        CHECK(0, "%s: the stand", name);
        return;
    }
    printf("%s gameplay: the stand: %u body triangles, %u seams, longest edge %.2f\n", name, (unsigned)ref.nt, (unsigned)ref.nseam, (double)ref.span);
    for (seg = 0; seg < sizeof script / sizeof script[0]; seg++) {
        meas_t m;
        meas_init(&m);
        for (p = 0; p < script[seg].pairs; p++) {
            sa1_state_ex x;
            float e0 = m.ex_edge, g0 = m.ex_gap;
            tick(id, script[seg].sx, script[seg].sy, 0);
            xstate(id, &x);
            measure(id, &ref, p, &m);
            if (g_verbose && (m.ex_edge > e0 + 0.3f || m.ex_gap > g0 + 0.3f)) {
                printf("    %s %-6s p%-3u mode %d action %d frame %.1f: added edge %.2f, seam %.2f\n", name, script[seg].what, (unsigned)p, (int)x.mode, (int)x.action, (double)x.frame, (double)m.ex_edge, (double)m.ex_gap);
            }
        }
        printf("  %-6s: at the pairs edge %.3f (x%.1f), seam %.3f; between them edge %.3f (x%.1f), seam %.3f; added edge %.3f, seam %.3f (pair %u); bad %u, topology differs %u of %u\n", script[seg].what, (double)m.edge_pair, (double)m.ratio_pair, (double)m.gap_pair, (double)m.edge_btw,
               (double)m.ratio_btw, (double)m.gap_btw, (double)m.ex_edge, (double)m.ex_gap, (unsigned)m.pair_ex, (unsigned)m.bad, (unsigned)m.mismatch, (unsigned)m.renders);
        if (g_tsv) {
            fprintf(g_tsv, "gameplay\t%s\t%s\t%.3f\t%.2f\t%.3f\t%.2f\t%.3f\t%.3f\t%.3f\t%.3f\t%u\t%u\n", name, script[seg].what, (double)m.edge_pair, (double)m.ratio_pair, (double)m.edge_btw, (double)m.ratio_btw, (double)m.gap_pair, (double)m.gap_btw, (double)m.ex_edge, (double)m.ex_gap,
                    (unsigned)m.bad, (unsigned)m.mismatch);
        }
        wg = m.ex_gap > wg ? m.ex_gap : wg;
        we = m.ex_edge > we ? m.ex_edge : we;
    }
    CHECK(wg <= LIM_EX_GAP, "%s: walking, running, braking, turning: the interpolation opens no seam: at most %.2f above the pairs, limit %.2f", name, (double)wg, (double)LIM_EX_GAP);
    CHECK(we <= LIM_EX_EDGE, "%s: walking, running, braking, turning: the interpolation stretches no edge: at most %.2f above the pairs, limit %.2f", name, (double)we, (double)LIM_EX_EDGE);
    ref_free(&ref);
    sa1_character_destroy(g_ctx, id);
}

static void t_gameplay(void)
{
    static const struct { const char *name; int32_t kind; int how; } k[] = {
        { "Sonic", SA1_CHAR_SONIC, 0 }, { "Tails", SA1_CHAR_TAILS, 0 }, { "Knuckles", SA1_CHAR_KNUCKLES, 0 }, { "Amy", SA1_CHAR_AMY, 0 },
        { "E-102", SA1_CHAR_GAMMA, 0 }, { "Big", SA1_CHAR_BIG, 0 },     { "Eggman", SA1_CHAR_EGGMAN, 0 },     { "Tikal", SA1_CHAR_TIKAL, 0 },
        { "Sonic+up", SA1_CHAR_SONIC, MK_UPGRADES }, { "Tails+up", SA1_CHAR_TAILS, MK_UPGRADES }, { "Knuckles+up", SA1_CHAR_KNUCKLES, MK_UPGRADES }, { "Amy+up", SA1_CHAR_AMY, MK_UPGRADES },
        { "E-102+up", SA1_CHAR_GAMMA, MK_UPGRADES }, { "Big+up", SA1_CHAR_BIG, MK_UPGRADES },
    };
    size_t i;
    printf("== gameplay: walk, run, brake, stop, turn (N1: Eggman's legs)\n");
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        gameplay_one(k[i].name, k[i].kind, k[i].how);
    }
}

static void t_catalogue(void)
{
    size_t li;
    printf("== catalogue: every entry of every list, entering and leaving, at the game's rate\n");
    for (li = 0; li < sizeof k_cat / sizeof k_cat[0]; li++) {
        sweep_list(&k_cat[li], 1.0f);
    }
}

static void t_upgrades(void)
{
    size_t li;
    printf("== catalogue with every upgrade of the kind on (what G-SA1 gives by default), at 0.5x as the owner played\n");
    for (li = 0; li < sizeof k_cat / sizeof k_cat[0]; li++) {
        cat_list L = k_cat[li];
        char nm[32];
        snprintf(nm, sizeof nm, "%s+up", L.name);
        L.name = nm;
        L.how |= MK_UPGRADES;
        sweep_list(&L, 0.5f);
    }
}

/* ---- the owner's test of N6: followers on the top of their wheels, the player's emote ends ------------------------------------------------------------- */
static void t_follower(void)
{
    static const struct { int32_t kind; uint32_t row; const char *name; } k[] = {
        { SA1_CHAR_TAILS, 54, "Tails m_kimepose0" }, { SA1_CHAR_KNUCKLES, 39, "Knuckles k_kimepose0" }, { SA1_CHAR_KNUCKLES, 40, "Knuckles k_kimepose1" }, { SA1_CHAR_AMY, 42, "Amy a_kimepose0" }, { SA1_CHAR_BIG, 48, "Big b_kimepose0" }, { SA1_CHAR_SONIC, 75, "Sonic s_kimepose0" },
        { SA1_CHAR_GAMMA, 67, "E-102 e_taiki0" },
    };
    size_t ki;
    printf("== followers: the player's emote ended: STOP_BLEND on a follower id (N6)\n");
    for (ki = 0; ki < sizeof k / sizeof k[0]; ki++) {
        sa1_character_desc d;
        sa1_follower_desc fd;
        sa1_character_id L = 0, F = 0;
        sa1_emote e;
        ref_t ref;
        track_t tr;
        meas_t m;
        sa1_state_ex x;
        uint32_t p, took = 0;
        float sweep;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.kind = SA1_CHAR_SONIC;
        d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE;
        d.position[0] = 100.0f;
        d.position[1] = 12.0f;
        d.position[2] = 100.0f;
        if (SA1_FAILED(sa1_character_create_ex(g_ctx, &d, &L))) {
            CHECK(0, "%s: the leader: %s", k[ki].name, sa1_last_error(g_ctx));
            continue;
        }
        memset(&fd, 0, sizeof fd);
        fd.struct_size = sizeof fd;
        fd.kind = k[ki].kind;
        fd.leader = L;
        fd.flags = SA1_FOLLOW_NO_SET;
        if (SA1_FAILED(sa1_follower_create(g_ctx, &fd, &F))) {
            CHECK(0, "%s: the follower: %s", k[ki].name, sa1_last_error(g_ctx));
            sa1_character_destroy(g_ctx, L);
            continue;
        }
        for (p = 0; p < 240u; p++) {
            tick(L, 0, 0, 0);
            tick(F, 0, 0, 0);
        }
        ref_take(F, &ref);
        memset(&e, 0, sizeof e);
        e.struct_size = sizeof e;
        e.count = 1;
        e.items[0].source = SA1_EMOTE_ROW;
        e.items[0].index = k[ki].row;
        e.items[0].speed = 0.5f;
        e.link = -1;
        e.flags = SA1_EMOTE_HOLD;
        for (p = 0; p < 200u && !took; p++) {
            took = sa1_character_emote(g_ctx, F, &e) == SA1_OK;
            tick(L, 0, 0, 0);
            tick(F, 0, 0, 0);
        }
        for (p = 0; p < 300u; p++) {
            tick(L, 0, 0, 0);
            tick(F, 0, 0, 0);
            xstate(F, &x);
            if (x.emote_state == SA1_EMOTE_STATE_HOLDING) {
                break;
            }
        }
        meas_init(&m);
        track_init(&tr, &ref);
        track_add(&tr, F, &ref);
        sa1_character_emote_stop(g_ctx, F, SA1_EMOTE_STOP_BLEND);
        for (p = 0; p < 300u; p++) {
            tick(L, 0, 0, 0);
            tick(F, 0, 0, 0);
            measure(F, &ref, p, &m);
            track_add(&tr, F, &ref);
            xstate(F, &x);
            if (x.emote_state == SA1_EMOTE_STATE_NONE) {
                break;
            }
        }
        for (p = 0; p < 2u; p++) {
            tick(L, 0, 0, 0);
            tick(F, 0, 0, 0);
            track_add(&tr, F, &ref);
        }
        sweep = track_sweep(&tr);
        track_free(&tr);
        CHECK(took && sweep <= LIM_SWEEP_POSE && m.gap_pair <= LIM_GAP_PAIR + 0.5f && m.ex_gap <= LIM_EX_GAP, "follower %-18s: leaving the held pose: detour %.2f (limit %.2f), seam %.2f at the pairs, added between them %.2f, edge %.2f", k[ki].name, (double)sweep, (double)LIM_SWEEP_POSE, (double)m.gap_pair, (double)m.ex_gap, (double)m.edge_pair);
        ref_free(&ref);
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

/* ---- N4: Knuckles' names ---------------------------------------------------------------------------------------------------------------------------------- */
static void t_knuckles_names(void)
{
    uint32_t n = sa1_emote_count(g_ctx, SA1_CHAR_KNUCKLES, 0), i;
    const char *r39 = "", *r54 = "";
    printf("== Knuckles' catalogue (N4): row 39 is what the game requests at level clear (code_8C0C61F4.c case 19), the real first punch is row 54\n");
    for (i = 0; i < n; i++) {
        sa1_emote_info info;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        sa1_emote_get(g_ctx, SA1_CHAR_KNUCKLES, 0, i, &info);
        if (info.source == SA1_EMOTE_ROW && info.index == 39u) {
            r39 = info.internal_name;
        }
        if (info.source == SA1_EMOTE_ROW && info.index == 54u) {
            r54 = info.internal_name;
        }
    }
    CHECK(strcmp(r39, "k_kimepose0") == 0, "row 39 is k_kimepose0: %s", r39);
    CHECK(strcmp(r54, "k_panch0") == 0, "row 54 is k_panch0: %s", r54);
}

typedef struct test_case {
    const char *name;
    void (*fn)(void);
} test_case;

static const test_case k_tests[] = {
    { "gameplay", t_gameplay },
    { "catalogue", t_catalogue },
    { "upgrades", t_upgrades },
    { "follower", t_follower },
    { "knuckles_names", t_knuckles_names },
};

static int selected(const char *only, const char *name)
{
    size_t n = strlen(name);
    const char *p = only;
    if (only == NULL) {
        return 1;
    }
    while ((p = strstr(p, name)) != NULL) {
        if ((p == only || p[-1] == ',') && (p[n] == 0 || p[n] == ',')) {
            return 1;
        }
        p += n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *only = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_status st;
    size_t i;
    int a;
    double t0;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--verbose")) {
            g_verbose = 1;
        } else if (!strcmp(argv[a], "--only") && a + 1 < argc) {
            only = argv[++a];
        } else if (!strcmp(argv[a], "--tsv") && a + 1 < argc) {
            g_tsv = fopen(argv[++a], "w");
        } else {
            g_gdi = argv[a];
        }
    }
    if (g_gdi == NULL) {
        g_gdi = getenv("SA1_TEST_GDI");
    }
    if (g_gdi == NULL) {
        printf("anim_test: needs the disc (GDI argument or SA1_TEST_GDI)\n");
        return 2;
    }
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_INFO;
    if (sa1_init(&h, &g_ctx) != SA1_OK) {
        printf("sa1_init failed\n");
        return 1;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(g_ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) | (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SUPER_SONIC) | (1u << SA1_CHAR_EGGMAN) |
                    (1u << SA1_CHAR_TIKAL);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS;
    st = sa1_load_disc(g_ctx, g_gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(g_ctx));
        sa1_shutdown(g_ctx);
        return 1;
    }
    if (scene() != 0) {
        sa1_shutdown(g_ctx);
        return 1;
    }
    for (i = 0; i < sizeof k_tests / sizeof k_tests[0]; i++) {
        if (!selected(only, k_tests[i].name)) {
            continue;
        }
        t0 = tu_now_ms();
        k_tests[i].fn();
        printf("  (%s: %.1f s)\n", k_tests[i].name, (tu_now_ms() - t0) / 1000.0);
    }
    sa1_shutdown(g_ctx);
    if (g_tsv) {
        fclose(g_tsv);
    }
    printf("anim_test: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
