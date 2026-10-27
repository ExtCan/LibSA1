/*
 * ramp_diag.c - diagnostics for two reports on G-SA1 0.3 (2026-10-20): "going up a ramp too fast makes you fall
 * through it" and "the game is randomly freezing up". Not a pass / fail test: it measures and prints.
 *
 *   ramp_diag [--only NAME]... [--trace CASE] [--verbose] [--keep-undersides] [--undersides-drop] [--legacy-split] GDI
 *                                                                    (build: tests/build_ramp_diag.py [--touch-hook])
 *
 *   --keep-undersides  every scene with SA1_GRID_KEEP_UNDERSIDES (GRID as in round 4b: undersides within reach kept)
 *   --undersides-drop  every scene with SA1_GRID_UNDERSIDES_DROP (round 0.9.3: the undersides within reach left out, as
 *                      before 0.9.3; by default the flat ones are kept as front-only polygons)
 *   --legacy-split     every scene's triangles halved along their longest edge down to 128 units first, with
 *                      SA1_GRID_DC_LIMITS (input order, the game's 96-entry NEAR list): the GRID cutting before round 4b
 *   --touch-hook       (build option) the section "case" prints every TOUCH call (8C029B00) of the steps around the
 *                      fall: the sphere, the object's NEAR-poly entries, the touches (height before / after the move,
 *                      which one was selected, the push-back) - the evidence of sadc docs/research/gsa1_ramp_freeze.md
 *                      section 8 (mechanism B)
 *
 * Every scene is a host triangle soup through the public API (sa1_collision_set_static / _ex, GRID collision,
 * SA1_COORDS_PRESET_NATIVE: host space = SA1 space, y up), characters created with sa1_character_create_ex and driven
 * through sa1_character_tick in 1/30 s pairs. Internals (diagnostics only): the world's step function is wrapped so
 * that every 60 Hz step is timed and the player / csts / NEAR / TOUCH / matrix-stack state is recorded after it.
 *
 *   ramps    ramps of 10..60 degrees (300 units high) from flat ground, as 2 huge triangles and tessellated (32-unit
 *            tiles), approached walking, jogging, running, at top running speed, by spin dash (Sonic) and with the
 *            ground speed set to 6 / 10 / 16 units per step at x = -150 (a dash panel); Sonic, Tails, Knuckles.
 *            A run "falls through" when the feet go more than 6 units below the ramp / plateau surface while over it.
 *   params   the failing set-ups again with the GRID parameters varied (max_edge 32 / 64 / 128 / 220, SA1_GRID_CUT
 *            with 64- and 256-unit cells): whether splitting / cells / gather culling change the outcome.
 *   smooth   the same steep ramps with a rounded foot (the slope reached in 2 / 4 / 8 segments over 40 / 80 units,
 *            the way SA1's own stages blend floors into slopes).
 *   entry    where it happens: from flat ground, landing on the ramp from a jump, starting on the ramp, the top edge.
 *   stress   tick cost: per-step time on dense, huge, overlapping and sliver geometry; the longest step, the
 *            Ninja matrix stack depth (B_8C88F69C: 8C029B00 leaves one push behind when 16 polygons of one object are
 *            touched; nothing resets the stack in a libsa1 world), the NEAR / TOUCH counts.
 *   threshold  the lowest dash-panel speed (2..16) that falls through, per slope (10..70 degrees).
 *   slab     plank ramps: the slope and the platform as slabs T thick (undersides within reach of the sphere; GRID
 *            leaves such undersides out since 2026-10-21: 0 of 264 fall, 10 with --keep-undersides).
 *   under    the same slope over nothing / a floor / a slab's underside: what turns the entry dip into a fall.
 *   stairs   slopes flagged SOLID | STAIRS.
 *   needle   gm_freespace_13's big ramp rebuilt (a 1328 x 59 plank): long triangles against 32-unit tiles (the GRID of
 *            2026-10-20 halved long triangles into needles that filled the 96-entry NEAR-poly list; GRID now cuts them into
 *            well-shaped pieces and host worlds have a 256-entry list: sadc docs/research/gsa1_ramp_freeze.md fixes 1, 3).
 *            NEAR counts are compared with the world's list (sa1ca_world_stats.near_cap: 256, 96 with SA1_GRID_DC_LIMITS).
 *   wallspawn  characters created 2..8 units from a wall.
 *   case     one run printed step by step: RAMP_CASE="kind deg slab tile fillet_n fillet_len approach floor_under".
 *   audio    the cost of starting a music / voice clip (the whole ADX read from the disc image): RAMP_GDI=GDI.
 * Findings: sadc/docs/research/gsa1_ramp_freeze.md.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_world.h"
#include "sa1_api_world.h"
#include "sa1_coladapt.h"
#include "libsa1_audio.h"

#include "guard_alloc.c"       /* SA1_GUARD_ALLOC / --guard-alloc: tests/guard_alloc.h */

#define PI 3.14159265358979323846
#define YAW_X ((float)(-5.9 * PI / 180.0))   /* camera yaw for a forward stick along +x (grid_test.c) */

/* guest addresses (game globals) */
#define A_NEAR_COUNT   0x8C73E27Cu      /* B_8C73E27C: NEAR-poly entries of the last 8C028BFE call (the world's list:
                                           96 in the game, 256 in host worlds) */
#define A_TOUCH_COUNT  0x8C73E400u      /* B_8C73E400: TOUCH-poly entries of the last 8C029B00 call (16 max) */
#define A_MTX_DEPTH    0x8C88F69Cu      /* B_8C88F69C: Ninja matrix stack depth (1 after njInitMatrix) */
#define A_MTX_LIMIT    0x8C88F698u      /* B_8C88F698: its limit (128) */

static int g_verbose;

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

static sa1_context *make_ctx(const char *gdi)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    sa1_status st;
    sa1_host_defaults(&h);
    ga_host(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES);
    lo.flags = SA1_LOAD_DEFAULT;
    st = sa1_load_disc(ctx, gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

/* ---- triangle buffer -------------------------------------------------------------------------------- */
typedef struct tbuf {
    sa1_collision_tri *t;
    uint32_t n, cap;
} tbuf;

static void tb_free(tbuf *b)
{
    free(b->t);
    memset(b, 0, sizeof *b);
}

static void tb_tri(tbuf *b, const float a[3], const float c[3], const float d[3], uint32_t s)
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        b->cap = b->cap ? 2u * b->cap : 256u;
        b->t = (sa1_collision_tri *)realloc(b->t, (size_t)b->cap * sizeof *b->t);
    }
    t = &b->t[b->n++];
    memcpy(t->v[0], a, 12);
    memcpy(t->v[1], c, 12);
    memcpy(t->v[2], d, 12);
    t->surface = s;
}

/* quad p0..p3 (counter-clockwise seen from above: front up) as 2 triangles, or as nx x nz tiles (bilinear) */
static void tb_quad_tiles(tbuf *b, const float p[4][3], int nx, int nz, uint32_t s)
{
    int i, k;
    for (i = 0; i < nx; i++) {
        for (k = 0; k < nz; k++) {
            float q[4][3];
            int c, a;
            for (c = 0; c < 4; c++) {
                float u = (float)(i + (c == 1 || c == 2)) / (float)nx, v = (float)(k + (c >= 2)) / (float)nz;
                for (a = 0; a < 3; a++) {
                    float e0 = p[0][a] + (p[1][a] - p[0][a]) * u, e1 = p[3][a] + (p[2][a] - p[3][a]) * u;
                    q[c][a] = e0 + (e1 - e0) * v;
                }
            }
            tb_tri(b, q[0], q[2], q[1], s);
            tb_tri(b, q[0], q[3], q[2], s);
        }
    }
}

/* ---- the step recorder ---------------------------------------------------------------------------- */
typedef struct steprec {
    uint64_t step;
    int32_t mode, action;
    uint32_t twflag, pwflag, attr;
    float pos[3], mspd[3], pspd[3], fn[3];
    int32_t ang[3];
    float cpos[3], tnorm[3], normal2[3];
    uint32_t find_count;
    int32_t near_n, near_cap, touch_n, land_n, mtx;
    uint32_t win_obj, win_poly, cand, over;     /* the collision adapter's window after the step */
    double ms;
} steprec;

#define REC_MAX 8192
static steprec g_rec[REC_MAX];
static uint32_t g_nrec;
static sa1g_step_fn g_orig_step;
static float g_boost_v, g_boost_x;  /* set pwp->spd.x = v once when x > boost_x (a dash panel); 0 = none */
static int g_boosted;
static double g_step_max_ms;
static int32_t g_mtx_max;

static uint32_t rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    sa1g_world_rd32(w, g, &v);
    return v;
}

static float rdf(const uint8_t *p)
{
    float f;
    memcpy(&f, p, 4);
    return f;
}

static void diag_step(sa1g_world *w, int32_t sub, void *user)
{
    sa1g_player_ptrs pp;
    steprec *r;
    double t0, t1;
    int k;
    sa1g_world_player(w, &pp);
    if (g_boost_v > 0.0f && !g_boosted && pp.twp && pp.pwp) {
        const uint8_t *t = (const uint8_t *)sa1g_world_ptr(w, pp.twp);
        if (rdf(t + 0x20) > g_boost_x) {
            float v = g_boost_v;
            memcpy((uint8_t *)sa1g_world_ptr(w, pp.pwp) + 0x38, &v, 4);
            g_boosted = 1;
        }
    }
    t0 = sa1_now_ms();
    g_orig_step(w, sub, user);
    t1 = sa1_now_ms();
    if (t1 - t0 > g_step_max_ms) {
        g_step_max_ms = t1 - t0;
    }
    if (g_nrec >= REC_MAX) {
        return;
    }
    sa1g_world_player(w, &pp);
    r = &g_rec[g_nrec++];
    memset(r, 0, sizeof *r);
    r->step = w->steps + 1u;
    r->ms = t1 - t0;
    r->near_n = (int32_t)rd32(w, A_NEAR_COUNT);
    r->touch_n = (int32_t)rd32(w, A_TOUCH_COUNT);
    r->land_n = (int32_t)rd32(w, SA1G_ADDR_LAND_COUNT);
    r->mtx = (int32_t)rd32(w, A_MTX_DEPTH);
    if (r->mtx > g_mtx_max) {
        g_mtx_max = r->mtx;
    }
    {
        sa1ca_world_stats ws;
        sa1ca_world_stats_get(w, &ws);
        r->near_cap = ws.near_cap ? (int32_t)ws.near_cap : 96;
        r->win_obj = ws.window_objects;
        r->win_poly = ws.window_polygons;
        r->cand = ws.candidates;
        r->over = ws.over_objects + ws.over_polygons + ws.over_near;
    }
    if (pp.twp && pp.mwp && pp.pwp) {
        const uint8_t *t = (const uint8_t *)sa1g_world_ptr(w, pp.twp);
        const uint8_t *m = (const uint8_t *)sa1g_world_ptr(w, pp.mwp);
        const uint8_t *p = (const uint8_t *)sa1g_world_ptr(w, pp.pwp);
        uint16_t h;
        r->mode = (int8_t)t[0];
        memcpy(&h, t + 4, 2);
        r->twflag = h;
        memcpy(&h, p + 0x10, 2);
        r->pwflag = h;
        memcpy(&r->attr, p + 0x5C, 4);
        memcpy(&h, p + 0x126, 2);
        r->action = h;
        for (k = 0; k < 3; k++) {
            r->pos[k] = rdf(t + 0x20 + 4 * k);
            memcpy(&r->ang[k], t + 0x14 + 4 * k, 4);
            r->mspd[k] = rdf(m + 0x04 + 4 * k);
            r->pspd[k] = rdf(p + 0x38 + 4 * k);
            r->fn[k] = rdf(p + 0x50 + 4 * k);
        }
        if (pp.csts) {
            const uint8_t *c = (const uint8_t *)sa1g_world_ptr(w, pp.csts);
            for (k = 0; k < 3; k++) {
                r->cpos[k] = rdf(c + 0x04 + 4 * k);
                r->tnorm[k] = rdf(c + 0x1C + 4 * k);
                r->normal2[k] = rdf(c + 0x48 + 4 * k);
            }
            memcpy(&h, c + 0x28, 2);
            r->find_count = h;
        }
    }
}

static void rec_reset(void)
{
    g_nrec = 0;
    g_boost_v = 0.0f;
    g_boosted = 0;
    g_step_max_ms = 0.0;
    g_mtx_max = 0;
}

static void rec_print(const steprec *r, float depth)
{
    printf("      step %5u %c%c mode %2d act %3d pos (%9.3f %8.3f %7.3f) d %6.2f mspd (%6.2f %6.2f %6.2f) pspd.x %5.2f "
           "fn (%5.2f %5.2f) tn (%5.2f %5.2f) n2 (%5.2f %5.2f) angz %5d NEAR %2d TOUCH %2d land %3d mtx %d win %u/%u "
           "cand %u over %u %.2f ms\n",
           (unsigned)r->step, (r->twflag & 1) ? 'G' : '-', (r->twflag & 2) ? 'g' : '-', (int)r->mode, (int)r->action,
           r->pos[0], r->pos[1], r->pos[2], depth, r->mspd[0], r->mspd[1], r->mspd[2], r->pspd[0], r->fn[0], r->fn[1],
           r->tnorm[0], r->tnorm[1], r->normal2[0], r->normal2[1], (int)(int16_t)r->ang[2], (int)r->near_n,
           (int)r->touch_n, (int)r->land_n, (int)r->mtx, (unsigned)r->win_obj, (unsigned)r->win_poly,
           (unsigned)r->cand, (unsigned)r->over, r->ms);
}

#ifdef RAMP_TOUCH_HOOK
/* ---- the touch log (tests/build_ramp_diag.py --touch-hook) ---------------------------------------------
 * The diagnostic build links its own copy of the lowered 8C029B00 (TOUCH, build/pclower/<profile>/lowered/
 * code_8C029B00.c) that calls sa1diag_touch() before each of its returns: every call of every step is logged with the
 * sphere it was given, the NEAR-poly entries of the object (8C028BFE's list) and the touches (B_8C73E404: polygon,
 * normal2, height before / after the move, push-back), so a fall can be followed call by call (section "case"). */
#define TL_CALLS 4096
#define TL_POLYS 65536
typedef struct tl_poly {
    float v[9];
    uint32_t attr;
    float n2[3], dist, dist2, pshbk[3];  /* touches only */
} tl_poly;
typedef struct tl_call {
    uint64_t step;
    uint32_t obj;               /* guest NJS_OBJECT */
    int32_t ret, sel, nnear, ntouch;
    float radius, pos[3], spd[3], pshbk[3], onpoly[3], n2[3];
    uint32_t near0, touch0;     /* first entries in g_tl_poly */
} tl_call;
static tl_call g_tl[TL_CALLS];
static tl_poly g_tlp[TL_POLYS];
static uint32_t g_ntl, g_ntlp;
static int g_tl_on;

static void tl_vec(const uint8_t *p, float out[3])
{
    memcpy(out, p, 12);
}

/* a guest word of the arena or the extension segment (the raised NEAR-poly list lives in the world's pcdata) */
static uint32_t tl_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t seg = (g >> 24) & 31u, v = 0;
    if (g == 0 || (seg != SA1G_SEG_ARENA && seg != SA1G_SEG_EXT) || (seg == SA1G_SEG_EXT && (g & 0xFFFFFFu) + 4u > w->ext_used)) {
        return 0;
    }
    memcpy(&v, sa1g_world_ptr(w, g), 4);
    return v;
}

void sa1diag_touch(const void *cpv, const void *object, int32_t ret);
void sa1diag_touch(const void *cpv, const void *object, int32_t ret)
{
    sa1g_world *w = sa1g_world_current();
    const uint8_t *cp = (const uint8_t *)cpv;
    tl_call *c;
    uint32_t lp, i, n;
    uint16_t h;
    if (!g_tl_on || w == NULL || g_ntl >= TL_CALLS) {
        return;
    }
    c = &g_tl[g_ntl++];
    memset(c, 0, sizeof *c);
    c->step = w->steps + 1u;
    c->obj = sa1_encode((void *)(uintptr_t)object);
    c->ret = ret;
    memcpy(&c->radius, cp, 4);
    tl_vec(cp + 0x04, c->pos);
    tl_vec(cp + 0x10, c->spd);
    tl_vec(cp + 0x48, c->n2);
    tl_vec(cp + 0x54, c->onpoly);
    tl_vec(cp + 0x60, c->pshbk);
    memcpy(&h, cp + 0x2A, 2);
    c->sel = ret ? (int32_t)h : -1;
    /* the object's NEAR-poly entries: the polygon lists D_8C02D548 (next +0, object +8, near +0x1C, near_num +0x24) */
    c->near0 = g_ntlp;
    for (lp = tl_rd32(w, 0x8C02D548u); lp != 0; lp = tl_rd32(w, lp)) {
        if (tl_rd32(w, lp + 8u) == c->obj) {
            uint32_t arr = tl_rd32(w, lp + 0x1Cu), cnt = tl_rd32(w, lp + 0x24u);
            for (i = 0; i < cnt && arr != 0 && g_ntlp < TL_POLYS; i++) {
                uint32_t pg = tl_rd32(w, arr + 4u * i);
                const uint8_t *pp = (const uint8_t *)sa1g_world_ptr(w, pg);
                if (pg == 0) {
                    break;
                }
                tl_poly *q = &g_tlp[g_ntlp++];
                memset(q, 0, sizeof *q);
                memcpy(q->v, pp + 0x18, 36);
                memcpy(&q->attr, pp + 0x3C, 4);
                c->nnear++;
            }
            break;
        }
    }
    /* the touches B_8C73E404[B_8C73E400] (0x9C each: COLPOLY at +4, TOUCHINFO at +0x44) */
    c->touch0 = g_ntlp;
    n = rd32(w, 0x8C73E400u);
    for (i = 0; i < n && i < 16u && g_ntlp < TL_POLYS && ret; i++) {
        const uint8_t *t = (const uint8_t *)sa1g_world_ptr(w, 0x8C73E404u + 0x9Cu * i);
        tl_poly *q = &g_tlp[g_ntlp++];
        memset(q, 0, sizeof *q);
        memcpy(q->v, t + 4 + 0x18, 36);
        memcpy(&q->attr, t + 4 + 0x3C, 4);
        tl_vec(t + 0x44 + 0x14, q->n2);
        memcpy(&q->dist, t + 0x44 + 0x50, 4);
        memcpy(&q->dist2, t + 0x44 + 0x54, 4);
        tl_vec(t + 0x44 + 0x2C, q->pshbk);
        c->ntouch++;
    }
}

static void tl_normal(const float *v, float n[3])
{
    float a[3], b[3], l;
    int k;
    for (k = 0; k < 3; k++) {
        a[k] = v[3 + k] - v[k];
        b[k] = v[6 + k] - v[k];
    }
    n[0] = a[1] * b[2] - a[2] * b[1];
    n[1] = a[2] * b[0] - a[0] * b[2];
    n[2] = a[0] * b[1] - a[1] * b[0];
    l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l > 0.0f) {
        n[0] /= l;
        n[1] /= l;
        n[2] /= l;
    }
}

/* every logged call of the steps [s0, s1] */
static void tl_print(uint64_t s0, uint64_t s1)
{
    uint32_t i, k;
    for (i = 0; i < g_ntl; i++) {
        const tl_call *c = &g_tl[i];
        if (c->step < s0 || c->step > s1) {
            continue;
        }
        printf("      step %5u TOUCH(obj %08X) r %.2f sphere (%.2f %.2f %.2f) spd (%.2f %.2f %.2f): NEAR %d, touched %d%s",
               (unsigned)c->step, (unsigned)c->obj, c->radius, c->pos[0], c->pos[1], c->pos[2], c->spd[0], c->spd[1],
               c->spd[2], c->nnear, c->ntouch, c->ret ? "" : " (none)\n");
        if (c->ret) {
            printf(", selected #%d: n2 (%.2f %.2f %.2f), push-back (%.2f %.2f %.2f) = sphere %+.2f %+.2f %+.2f\n", c->sel,
                   c->n2[0], c->n2[1], c->n2[2], c->pshbk[0], c->pshbk[1], c->pshbk[2], c->pshbk[0] - c->pos[0],
                   c->pshbk[1] - c->pos[1], c->pshbk[2] - c->pos[2]);
        }
        for (k = 0; k < (uint32_t)c->nnear; k++) {
            const tl_poly *q = &g_tlp[c->near0 + k];
            float n[3], ce[3];
            int a;
            tl_normal(q->v, n);
            for (a = 0; a < 3; a++) {
                ce[a] = (q->v[a] + q->v[3 + a] + q->v[6 + a]) / 3.0f;
            }
            printf("        near %2u: centre (%8.2f %8.2f %8.2f) normal (%5.2f %5.2f %5.2f)%s height of the sphere %+7.2f\n", k,
                   ce[0], ce[1], ce[2], n[0], n[1], n[2], (q->attr & 0x800000u) ? " 2-sided" : "",
                   n[0] * (c->pos[0] - q->v[0]) + n[1] * (c->pos[1] - q->v[1]) + n[2] * (c->pos[2] - q->v[2]));
        }
        for (k = 0; k < (uint32_t)c->ntouch; k++) {
            const tl_poly *q = &g_tlp[c->touch0 + k];
            float n[3];
            tl_normal(q->v, n);
            printf("        touch %u%s: normal (%5.2f %5.2f %5.2f) n2 (%5.2f %5.2f %5.2f) height %+6.2f after the move %+6.2f "
                   "push-back (%.2f %.2f %.2f)%s\n", k, (int32_t)k == c->sel ? "*" : " ", n[0], n[1], n[2], q->n2[0],
                   q->n2[1], q->n2[2], q->dist, q->dist2, q->pshbk[0], q->pshbk[1], q->pshbk[2],
                   q->dist < 0.0f ? "  <- touched from BEHIND" : "");
        }
    }
}
#endif

/* ---- characters ------------------------------------------------------------------------------------- */
static sa1_status char_at(sa1_context *ctx, int32_t kind, float x, float y, float z, int32_t angy, sa1_character_id *id)
{
    sa1_character_desc d;
    sa1g_world *w;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    d.angle[1] = angy;
    st = sa1_character_create_ex(ctx, &d, id);
    if (SA1_FAILED(st)) {
        return st;
    }
    w = sa1_character_world(ctx, *id);
    if (w == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    if (g_orig_step == NULL) {
        g_orig_step = w->step;
    }
    w->step = diag_step;
    return st;
}

/* the inverse of the game's dead zone (G-SA1's exact_stick, session.cpp): the game decodes this stick as (x, y) */
static void exact_stick(float x, float y, float *sx, float *sy)
{
    float m = sqrtf(x * x + y * y), dx, dy, px, py;
    if (m < 1e-4f) {
        *sx = *sy = 0.0f;
        return;
    }
    if (m > 1.0f) {
        x /= m;
        y /= m;
    }
    dx = x * 115.0f;
    dy = -y * 115.0f;
    px = roundf(dx + (dx >= 0.0f ? 12.0f : -12.0f));
    py = roundf(dy + (dy >= 0.0f ? 12.0f : -12.0f));
    px = px < -127.0f ? -127.0f : (px > 127.0f ? 127.0f : px);
    py = py < -127.0f ? -127.0f : (py > 127.0f ? 127.0f : py);
    *sx = px / 127.0f;
    *sy = -py / 127.0f;
}

static sa1_status pair(sa1_context *ctx, sa1_character_id id, float sx, float sy, uint32_t buttons, sa1_state_ex *x)
{
    sa1_input in;
    sa1_status st;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    exact_stick(sx, sy, &in.stick_x, &in.stick_y);
    in.camera_yaw = 0.0f;
    in.buttons = buttons;
    st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    x->struct_size = sizeof *x;
    sa1_character_state_ex(ctx, id, x);
    return st;
}

static const char *kind_name(int32_t k)
{
    return k == SA1_CHAR_SONIC ? "Sonic" : k == SA1_CHAR_TAILS ? "Tails" : k == SA1_CHAR_KNUCKLES ? "Knuckles" : "?";
}

/* ---- ramp scenes -------------------------------------------------------------------------------------- */
#define PROF_MAX 64
typedef struct ramp_scene {
    float deg, H, W;            /* slope, height, half width (z) */
    float tile;                 /* 0: every flat piece 2 triangles; else tiles of this size */
    float slab;                 /* > 0: the slope and the plateau are slabs this thick (undersides, sides, ends) over a
                                   floor at y = 0 that runs on under them (a plank ramp onto a thin platform) */
    uint32_t slope_surface;     /* SA1_SURFACE_* of the sloped pieces (0: SOLID) */
    int floor_under;            /* plain: the floor goes on under the slope and the plateau (1); slab: the floor stops at
                                   the foot of the slope (-1; default 0 = goes on under the slab) */
    int fillet_n;               /* rounded foot: the slope reached in this many segments (0: a sharp kink) */
    float fillet_len;           /* over this length (x) */
    float px[PROF_MAX], py[PROF_MAX];   /* profile y(x), built by scene_profile */
    int np;
    float x_top;                /* where the plateau begins */
} ramp_scene;

static void scene_profile(ramp_scene *s)
{
    double t = tan(s->deg * PI / 180.0), x = 0.0, y = 0.0;
    int i;
    s->np = 0;
    s->px[s->np] = -4200.0f;
    s->py[s->np++] = 0.0f;
    s->px[s->np] = 0.0f;
    s->py[s->np++] = 0.0f;
    for (i = 1; i <= s->fillet_n; i++) {
        double a = s->deg * PI / 180.0 * (double)i / (double)(s->fillet_n + 1);
        x += s->fillet_len / s->fillet_n;
        y += tan(a) * s->fillet_len / s->fillet_n;
        s->px[s->np] = (float)x;
        s->py[s->np++] = (float)y;
    }
    x += (s->H - y) / t;
    s->px[s->np] = (float)x;
    s->py[s->np++] = s->H;
    s->x_top = (float)x;
    s->px[s->np] = (float)x + 1200.0f;
    s->py[s->np++] = s->H;
}

static float scene_surf(const ramp_scene *s, float x)
{
    int i;
    if (x <= s->px[0]) {
        return s->py[0];
    }
    for (i = 0; i + 1 < s->np; i++) {
        if (x <= s->px[i + 1]) {
            float u = (x - s->px[i]) / (s->px[i + 1] - s->px[i]);
            return s->py[i] + (s->py[i + 1] - s->py[i]) * u;
        }
    }
    return s->py[s->np - 1];
}

static float g_slab_tile;   /* > 0: the slab scene's faces are cut into tiles of about this size (well-shaped) */

static void quad4(tbuf *b, float ax, float ay, float bx, float by, float z0, float z1, int up, uint32_t surf)
{
    /* the band from (ax, ay) to (bx, by) in x / y, z0..z1; up: front faces the left of a -> b (up for a -> b along +x) */
    float p[4][3] = { { ax, ay, z0 }, { bx, by, z0 }, { bx, by, z1 }, { ax, ay, z1 } };
    int nx = 1, nz = 1;
    if (!up) {
        float q[4][3] = { { bx, by, z0 }, { ax, ay, z0 }, { ax, ay, z1 }, { bx, by, z1 } };
        memcpy(p, q, sizeof p);
    }
    if (g_slab_tile > 0.0f) {
        float len = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
        nx = (int)ceilf(len / g_slab_tile);
        nz = (int)ceilf((z1 - z0) / g_slab_tile);
        nx = nx < 1 ? 1 : (nx > 400 ? 400 : nx);
        nz = nz < 1 ? 1 : (nz > 400 ? 400 : nz);
    }
    tb_quad_tiles(b, p, nx, nz, surf);
}

static void side_z(tbuf *b, const float *xs, const float *ys, int n, float z, int out)
{
    /* a polygon in the plane z (fan from vertex 0), front facing +z (out > 0) or -z */
    int i;
    for (i = 1; i + 1 < n; i++) {
        float a[3] = { xs[0], ys[0], z }, c[3] = { xs[i], ys[i], z }, d[3] = { xs[i + 1], ys[i + 1], z };
        float e1x = c[0] - a[0], e1y = c[1] - a[1], e2x = d[0] - a[0], e2y = d[1] - a[1];
        float nz = e1x * e2y - e1y * e2x;   /* z of (c - a) x (d - a) */
        if ((nz > 0.0f) == (out > 0)) {
            tb_tri(b, a, c, d, SA1_SURFACE_SOLID);
        } else {
            tb_tri(b, a, d, c, SA1_SURFACE_SOLID);
        }
    }
}

static void scene_build_slab(const ramp_scene *s, tbuf *b)
{
    /* floor y = 0 from -4200 to the plateau's end; the slope (the profile from x = 0 to x_top) as a slab of
       thickness T (perpendicular) and the plateau (x_top .. end) as a box T thick */
    float T = s->slab, xe = s->px[s->np - 1], W = s->W;
    int i;
    quad4(b, -4200.0f, 0.0f, s->floor_under < 0 ? 0.0f : xe + 600.0f, 0.0f, -W - 300.0f, W + 300.0f, 1, SA1_SURFACE_SOLID);
    for (i = 1; i + 2 < s->np; i++) {
        float ax = s->px[i], ay = s->py[i], bx = s->px[i + 1], by = s->py[i + 1];
        float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy), nx = -dy / l, ny = dx / l;
        float a2x = ax - T * nx, a2y = ay - T * ny, b2x = bx - T * nx, b2y = by - T * ny;
        float xs[4] = { ax, bx, b2x, a2x }, ys[4] = { ay, by, b2y, a2y };
        uint32_t surf = s->slope_surface ? s->slope_surface : SA1_SURFACE_SOLID;
        quad4(b, ax, ay, bx, by, -W, W, 1, surf);           /* top */
        quad4(b, a2x, a2y, b2x, b2y, -W, W, 0, SA1_SURFACE_SOLID); /* underside */
        quad4(b, a2x, a2y, ax, ay, -W, W, 1, SA1_SURFACE_SOLID);   /* foot end */
        quad4(b, bx, by, b2x, b2y, -W, W, 1, SA1_SURFACE_SOLID);   /* top end */
        side_z(b, xs, ys, 4, W, 1);
        side_z(b, xs, ys, 4, -W, -1);
    }
    {
        float x0 = s->x_top, H = s->H;
        float xs[4] = { x0, xe, xe, x0 }, ys[4] = { H, H, H - T, H - T };
        quad4(b, x0, H, xe, H, -W, W, 1, SA1_SURFACE_SOLID);
        quad4(b, x0, H - T, xe, H - T, -W, W, 0, SA1_SURFACE_SOLID);
        quad4(b, x0, H - T, x0, H, -W, W, 1, SA1_SURFACE_SOLID);
        quad4(b, xe, H, xe, H - T, -W, W, 1, SA1_SURFACE_SOLID);
        side_z(b, xs, ys, 4, W, 1);
        side_z(b, xs, ys, 4, -W, -1);
    }
}

static void scene_build(const ramp_scene *s, tbuf *b)
{
    int i;
    memset(b, 0, sizeof *b);
    if (s->slab > 0.0f) {
        scene_build_slab(s, b);
        return;
    }
    for (i = 0; i + 1 < s->np; i++) {
        float p[4][3] = { { s->px[i], s->py[i], -s->W }, { s->px[i + 1], s->py[i + 1], -s->W },
                          { s->px[i + 1], s->py[i + 1], s->W }, { s->px[i], s->py[i], s->W } };
        int nx = 1, nz = 1;
        if (s->tile > 0.0f) {
            float len = sqrtf((s->px[i + 1] - s->px[i]) * (s->px[i + 1] - s->px[i])
                              + (s->py[i + 1] - s->py[i]) * (s->py[i + 1] - s->py[i]));
            nx = (int)ceilf(len / s->tile);
            nz = (int)ceilf(2.0f * s->W / s->tile);
            nx = nx < 1 ? 1 : nx;
            if (i == 0) {
                nx = (int)ceilf(len / 64.0f);   /* the run-up floor: 64-unit tiles are enough */
                nz = (int)ceilf(2.0f * s->W / 64.0f);
            }
        }
        tb_quad_tiles(b, p, nx, nz, (i > 0 && i + 2 < s->np && s->slope_surface) ? s->slope_surface : SA1_SURFACE_SOLID);
    }
    if (s->floor_under > 0) {
        /* open space under the slope and the plateau: the floor goes on under them (a zero-thickness plank ramp) */
        quad4(b, 0.0f, 0.0f, s->px[s->np - 1] + 600.0f, 0.0f, -s->W - 300.0f, s->W + 300.0f, 1, SA1_SURFACE_SOLID);
    }
}

typedef struct gridp {
    const char *name;
    float cell, max_edge;
    uint32_t flags;
} gridp;

/* --keep-undersides / --legacy-split (comparisons): extra SA1_GRID_* flags for every scene, and the input triangles halved
   along their longest edge down to 128 units first (the GRID cutting before 2026-10-20, ca_grid_split, with the game's input
   order and 96-entry list: SA1_GRID_DC_LIMITS) */
static uint32_t g_extra_flags;
static int g_legacy_split;

static void legacy_halve(tbuf *o, const float *a, const float *c, const float *d, uint32_t surf, int depth)
{
    const float *v[3] = { a, c, d };
    float l[3], m[3];
    int e, k;
    for (e = 0; e < 3; e++) {
        const float *p = v[e], *q = v[(e + 1) % 3];
        l[e] = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]);
    }
    e = l[0] >= l[1] ? (l[0] >= l[2] ? 0 : 2) : (l[1] >= l[2] ? 1 : 2);
    if (l[e] <= 128.0f * 128.0f || depth > 30) {
        tb_tri(o, a, c, d, surf);
        return;
    }
    for (k = 0; k < 3; k++) {
        m[k] = 0.5f * (v[e][k] + v[(e + 1) % 3][k]);
    }
    legacy_halve(o, v[e], m, v[(e + 2) % 3], surf, depth + 1);
    legacy_halve(o, m, v[(e + 1) % 3], v[(e + 2) % 3], surf, depth + 1);
}

static sa1_status set_static_grid(sa1_context *ctx, const tbuf *b_in, const gridp *g)
{
    sa1_collision_static d;
    sa1_collision_grid gp;
    sa1_collision_group grp;
    float *pts;
    uint32_t *idx, i, k;
    sa1_status st;
    tbuf halved;
    const tbuf *b = b_in;
    static const gridp k_default = { "defaults", 0.0f, 0.0f, 0u };
    if (g == NULL && g_extra_flags == 0 && !g_legacy_split) {
        return sa1_collision_set_static(ctx, b->t, b->n);
    }
    if (g == NULL) {
        g = &k_default;
    }
    memset(&halved, 0, sizeof halved);
    if (g_legacy_split) {
        for (i = 0; i < b_in->n; i++) {
            legacy_halve(&halved, b_in->t[i].v[0], b_in->t[i].v[1], b_in->t[i].v[2], b_in->t[i].surface, 0);
        }
        b = &halved;
    }
    pts = (float *)malloc((size_t)b->n * 9u * sizeof(float));
    idx = (uint32_t *)malloc((size_t)b->n * 3u * sizeof(uint32_t));
    for (i = 0; i < b->n; i++) {
        memcpy(pts + 9u * i, b->t[i].v, 9u * sizeof(float));
        for (k = 0; k < 3; k++) {
            idx[3u * i + k] = 3u * i + k;
        }
    }
    memset(&gp, 0, sizeof gp);
    gp.struct_size = sizeof gp;
    gp.flags = g->flags | g_extra_flags | (g_legacy_split ? SA1_GRID_DC_LIMITS : 0u);
    gp.cell_size = g->cell;
    gp.max_edge = g->max_edge;
    memset(&grp, 0, sizeof grp);
    grp.struct_size = sizeof grp;
    grp.surface = SA1_SURFACE_SOLID;
    grp.num_points = 3u * b->n;
    grp.num_tris = b->n;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.mode = SA1_COLLISION_MODE_GRID;
    d.points = pts;
    d.num_points = 3u * b->n;
    d.indices = idx;
    d.num_tris = b->n;
    d.groups = &grp;
    d.num_groups = 1;
    d.grid = &gp;
    st = sa1_collision_set_static_ex(ctx, &d);
    free(pts);
    free(idx);
    tb_free(&halved);
    return st;
}

/* ---- approaches ------------------------------------------------------------------------------------- */
typedef struct approach {
    const char *name;
    float start;                /* x of the start (on the run-up floor) */
    float stick;
    float boost;                /* ground speed set once at x > -150 (a dash panel); 0 none */
    int spin;                   /* spin dash: hold ACTION standing, release with the stick forward */
    float start_y;              /* > 0: start in the air / on the ramp at this height (entry section) */
} approach;

static const approach k_ap[] = {
    { "walk", -400.0f, 0.5f, 0.0f, 0, 0.0f },
    { "jog", -500.0f, 0.75f, 0.0f, 0, 0.0f },
    { "run", -600.0f, 1.0f, 0.0f, 0, 0.0f },
    { "full", -4000.0f, 1.0f, 0.0f, 0, 0.0f },
    { "spindash", -400.0f, 1.0f, 0.0f, 1, 0.0f },
    { "boost6", -800.0f, 1.0f, 6.0f, 0, 0.0f },
    { "boost10", -800.0f, 1.0f, 10.0f, 0, 0.0f },
    { "boost16", -800.0f, 1.0f, 16.0f, 0, 0.0f },
};
#define NAP ((int)(sizeof k_ap / sizeof k_ap[0]))

typedef struct ramp_result {
    int created, reached, fell, back;
    float entry_speed, vmax;    /* |mspd| when crossing x = 0; largest |mspd| */
    float maxdepth;
    int first_fall;             /* index into g_rec of the first step deeper than 6 units, -1 none */
    int first_below;            /* first step deeper than 1 unit, -1 none */
    int dips;                   /* times the feet went more than 3 below the surface and came back within 5 steps */
    float fall_x, fall_u;       /* where: x, and x / x_top */
    int near_max, near_full, touch_max;
    int32_t mtx_max;
    uint32_t pairs;
    float maxx, endy;
    double step_max_ms;
    char why[64];
} ramp_result;

/* depth of a step below the scene's surface (0 when not over it) */
static float rec_depth(const ramp_scene *s, const steprec *r)
{
    float x = r->pos[0];
    if (x > 1.0f && x < s->px[s->np - 1] - 10.0f && fabsf(r->pos[2]) < s->W - 8.0f) {
        return scene_surf(s, x) - r->pos[1];
    }
    return 0.0f;
}

static float g_jump_x = 1e30f;     /* jump (6 pairs) once x > this */

static void ramp_run(sa1_context *ctx, const ramp_scene *s, const approach *ap, int32_t kind, ramp_result *R)
{
    sa1_character_id id = 0;
    sa1_state_ex x;
    uint32_t i, k, prev = 0;
    int on_top = 0, crossed = 0, fall_pair = -1, jump_left = -1, below_run = 0, below_start = -1;
    sa1_status st;
    memset(R, 0, sizeof *R);
    R->first_fall = R->first_below = -1;
    rec_reset();
    st = char_at(ctx, kind, ap->start, ap->start_y > 0.0f ? ap->start_y : 3.0f, 0.0f, 0, &id);
    if (SA1_FAILED(st)) {
        snprintf(R->why, sizeof R->why, "create: %s", sa1_status_string(st));
        return;
    }
    R->created = 1;
    for (i = 0; i < 8; i++) {
        pair(ctx, id, 0.0f, 0.0f, 0u, &x);
    }
    if (ap->spin) {
        for (i = 0; i < 30; i++) {
            pair(ctx, id, 0.0f, 0.0f, SA1_BUTTON_ACTION, &x);
        }
    }
    if (ap->boost > 0.0f) {
        g_boost_v = ap->boost;
        g_boost_x = ap->start_y > 0.0f ? -1e30f : -150.0f;
    }
    prev = g_nrec;
    crossed = ap->start > 0.0f;
    for (i = 0; i < 700; i++) {
        uint32_t btn = 0;
        if (jump_left < 0 && x.sa1_position[0] > g_jump_x) {
            jump_left = 6;
        }
        if (jump_left > 0) {
            btn = SA1_BUTTON_JUMP;
            jump_left--;
        }
        st = pair(ctx, id, 0.0f, ap->stick, btn, &x);
        R->pairs++;
        if (SA1_FAILED(st)) {
            snprintf(R->why, sizeof R->why, "tick: %s", sa1_status_string(st));
            break;
        }
        for (k = prev; k < g_nrec; k++) {
            const steprec *r = &g_rec[k];
            float d = rec_depth(s, r);
            float v = sqrtf(r->mspd[0] * r->mspd[0] + r->mspd[1] * r->mspd[1] + r->mspd[2] * r->mspd[2]);
            if (v > R->vmax) {
                R->vmax = v;
            }
            if (!crossed && r->pos[0] > 0.0f) {
                crossed = 1;
                R->entry_speed = v;
            }
            if (d > R->maxdepth) {
                R->maxdepth = d;
            }
            if (d > 1.0f && R->first_below < 0) {
                R->first_below = (int)k;
            }
            /* fell through: more than 3 below the surface for 6 steps in a row (a dip that the next steps push
               back up is not a fall) */
            if (d > 3.0f) {
                if (below_run++ == 0) {
                    below_start = (int)k;
                }
                if (below_run >= 6 && R->first_fall < 0) {
                    R->first_fall = below_start;
                    R->fall_x = g_rec[below_start].pos[0];
                    R->fall_u = R->fall_x / s->x_top;
                }
            } else {
                if (below_run > 0 && below_run < 6) {
                    R->dips++;
                }
                below_run = 0;
            }
            if (r->near_n > R->near_max) {
                R->near_max = r->near_n;
            }
            R->near_full += r->near_n >= (r->near_cap ? r->near_cap : 96);
            if (r->touch_n > R->touch_max) {
                R->touch_max = r->touch_n;
            }
            if (r->pos[0] > R->maxx) {
                R->maxx = r->pos[0];
            }
        }
        prev = g_nrec;
        if (x.sa1_position[0] > s->x_top + 60.0f && (x.flags & SA1_STATE_ON_GROUND)
            && fabsf(x.sa1_position[1] - s->H) < 1.0f) {
            if (++on_top >= 8) {
                R->reached = 1;
                break;
            }
        }
        if (R->first_fall >= 0 && fall_pair < 0) {
            fall_pair = (int)i;
        }
        if (fall_pair >= 0 && (x.sa1_position[1] < scene_surf(s, x.sa1_position[0]) - 60.0f || (int)i > fall_pair + 30)) {
            break;
        }
        if (x.sa1_position[0] < ap->start - 400.0f || x.sa1_position[0] > s->px[s->np - 1] || x.sa1_position[1] < -300.0f) {
            break;
        }
        if (crossed && x.sa1_position[0] < -60.0f && (x.flags & SA1_STATE_ON_GROUND)) {
            R->back = 1;    /* slid back down onto the run-up floor */
            break;
        }
    }
    R->fell = R->first_fall >= 0;
    R->endy = x.sa1_position[1];
    if (g_verbose) {
        printf("      [%s %s: %u pairs, end (%.1f %.1f %.1f) mode %d action %d, max x %.1f, vmax %.2f]\n", kind_name(kind),
               ap->name, (unsigned)R->pairs, x.sa1_position[0], x.sa1_position[1], x.sa1_position[2], (int)x.mode,
               (int)x.action, R->maxx, R->vmax);
    }
    R->mtx_max = g_mtx_max;
    R->step_max_ms = g_step_max_ms;
    sa1_character_destroy(ctx, id);
}

static const char *res_word(const ramp_result *R)
{
    return !R->created ? "ERROR" : R->fell ? "FELL" : R->reached ? "up" : R->back ? "back" : "stuck";
}

static void trace_fall(const ramp_scene *s, const ramp_result *R, int before, int after)
{
    int k, a = R->first_fall - before, e = R->first_fall + after;
    if (R->first_fall < 0) {
        return;
    }
    a = a < 0 ? 0 : a;
    e = e > (int)g_nrec ? (int)g_nrec : e;
    for (k = a; k < e; k++) {
        rec_print(&g_rec[k], rec_depth(s, &g_rec[k]));
    }
}

/* ---- sections ------------------------------------------------------------------------------------------ */
typedef struct G {
    sa1_context *ctx;
    int trace;                  /* traces of failing runs still to print */
} G;

static sa1_status scene_set(sa1_context *ctx, const ramp_scene *s, const gridp *g, uint32_t *ntri)
{
    tbuf b;
    sa1_status st;
    scene_build(s, &b);
    st = set_static_grid(ctx, &b, g);
    if (ntri) {
        *ntri = b.n;
    }
    tb_free(&b);
    return st;
}

static void cell_print(const ramp_result *R)
{
    if (R->fell) {
        printf(" FELL@%3.0f%% %4.1f ", R->fall_u * 100.0f, R->entry_speed);
    } else {
        printf(" %-4s %4.1f %4.1f%c", res_word(R), R->entry_speed, R->maxdepth, R->dips ? '*' : ' ');
    }
}

typedef struct failcase {
    ramp_scene s;
    int ap;
    int32_t kind;
    const gridp *g;
} failcase;

#define FAIL_MAX 64
static failcase g_fails[FAIL_MAX];
static int g_nfails;

static void note_fail(const ramp_scene *s, int ap, int32_t kind, const gridp *g)
{
    if (g_nfails < FAIL_MAX) {
        g_fails[g_nfails].s = *s;
        g_fails[g_nfails].ap = ap;
        g_fails[g_nfails].kind = kind;
        g_fails[g_nfails].g = g;
        g_nfails++;
    }
}

static void print_traces(G *E, int max)
{
    int i;
    for (i = 0; i < g_nfails && i < max; i++) {
        failcase *f = &g_fails[i];
        ramp_result R;
        if (scene_set(E->ctx, &f->s, f->g, NULL) != SA1_OK) {
            continue;
        }
        ramp_run(E->ctx, &f->s, &k_ap[f->ap], f->kind, &R);
        printf("    trace: %s, %.0f deg ramp (%s%s), %s: entry speed %.2f, %s at x %.1f (%.0f%% of the slope), depth "
               "up to %.1f\n", kind_name(f->kind), f->s.deg, f->s.tile > 0.0f ? "tiles" : "2 triangles",
               f->s.fillet_n ? ", rounded foot" : "", k_ap[f->ap].name, R.entry_speed, res_word(&R), R.fall_x,
               R.fall_u * 100.0f, R.maxdepth);
        trace_fall(&f->s, &R, 4, 6);
    }
    g_nfails = 0;
}

static void sec_ramps(G *E)
{
    static const float degs[] = { 10, 20, 30, 40, 45, 50, 60 };
    static const float tiles[] = { 0.0f, 32.0f };
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES };
    int mi, di, ki, a, total = 0, fell = 0;
    printf("ramps: 300 units high from flat ground, 1200 wide; cells: result, speed at the foot (units / step), deepest "
           "below the surface, * = dipped more than 3 below and was pushed back (FELL@n%%: more than 3 below for 6 "
           "steps, n%% up the slope)\n");
    for (ki = 0; ki < 3; ki++) {
        for (mi = 0; mi < 2; mi++) {
            printf("  %s, ramp as %s (GRID defaults: cells 256, max_edge 128)\n", kind_name(kinds[ki]),
                   tiles[mi] > 0.0f ? "32-unit tiles" : "2 triangles per surface");
            printf("    deg ");
            for (a = 0; a < NAP; a++) {
                if (k_ap[a].spin && kinds[ki] != SA1_CHAR_SONIC) {
                    continue;
                }
                printf(" %-15s", k_ap[a].name);
            }
            printf("\n");
            for (di = 0; di < (int)(sizeof degs / sizeof degs[0]); di++) {
                ramp_scene s;
                memset(&s, 0, sizeof s);
                s.deg = degs[di];
                s.H = 300.0f;
                s.W = 600.0f;
                s.tile = tiles[mi];
                scene_profile(&s);
                if (scene_set(E->ctx, &s, NULL, NULL) != SA1_OK) {
                    printf("    set_static: %s\n", sa1_last_error(E->ctx));
                    continue;
                }
                printf("    %3.0f ", s.deg);
                for (a = 0; a < NAP; a++) {
                    ramp_result R;
                    if (k_ap[a].spin && kinds[ki] != SA1_CHAR_SONIC) {
                        continue;
                    }
                    ramp_run(E->ctx, &s, &k_ap[a], kinds[ki], &R);
                    cell_print(&R);
                    total++;
                    if (R.fell) {
                        fell++;
                        note_fail(&s, a, kinds[ki], NULL);
                    }
                }
                printf("\n");
                fflush(stdout);
            }
        }
    }
    printf("  %d of %d runs fell through\n", fell, total);
    print_traces(E, E->trace);
}

/* the lowest entry speed that falls through, per slope */
static void sec_threshold(G *E)
{
    static const float degs[] = { 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70 };
    static const struct {
        const char *name;
        float tile;
        int fillet_n;
        float fillet_len;
        int32_t kind;
    } kinds[] = {
        { "Sonic, 2 triangles", 0.0f, 0, 0.0f, SA1_CHAR_SONIC },
        { "Sonic, 32-unit tiles", 32.0f, 0, 0.0f, SA1_CHAR_SONIC },
        { "Sonic, foot rounded (4 x 20)", 0.0f, 4, 80.0f, SA1_CHAR_SONIC },
        { "Knuckles, 2 triangles", 0.0f, 0, 0.0f, SA1_CHAR_KNUCKLES },
    };
    int di, ki, v;
    printf("threshold: the ground speed set at x = -150 (a dash panel) is raised 2, 3 .. 16 units / step; the first that "
           "falls through (speed at the foot of the slope, where)\n");
    for (ki = 0; ki < (int)(sizeof kinds / sizeof kinds[0]); ki++) {
        printf("  %s\n", kinds[ki].name);
        for (di = 0; di < (int)(sizeof degs / sizeof degs[0]); di++) {
            ramp_scene s;
            approach ap = k_ap[5];
            int found = 0;
            float up_max = 0.0f;
            memset(&s, 0, sizeof s);
            s.deg = degs[di];
            s.H = 300.0f;
            s.W = 600.0f;
            s.tile = kinds[ki].tile;
            s.fillet_n = kinds[ki].fillet_n;
            s.fillet_len = kinds[ki].fillet_len;
            scene_profile(&s);
            if (scene_set(E->ctx, &s, NULL, NULL) != SA1_OK) {
                continue;
            }
            printf("    %3.0f deg:", s.deg);
            for (v = 2; v <= 16 && !found; v++) {
                ramp_result R;
                ap.boost = (float)v;
                ramp_run(E->ctx, &s, &ap, kinds[ki].kind, &R);
                if (R.fell) {
                    printf(" falls from %2d (entry %.2f, at %.0f%% of the slope, depth %.1f)", v, R.entry_speed,
                           R.fall_u * 100.0f, R.maxdepth);
                    found = 1;
                } else if (R.reached && R.entry_speed > up_max) {
                    up_max = R.entry_speed;
                }
            }
            if (!found) {
                printf(" never (up to 16; fastest entry that made it up %.2f)", up_max);
            } else if (up_max > 0.0f) {
                printf("; made it up below that (entry up to %.2f)", up_max);
            }
            printf("\n");
            fflush(stdout);
        }
    }
}

static const gridp k_gp[] = {
    { "defaults (cell 256, edge 128)", 0.0f, 0.0f, 0u },
    { "max_edge 32", 256.0f, 32.0f, 0u },
    { "max_edge 64", 256.0f, 64.0f, 0u },
    { "max_edge 220", 256.0f, 220.0f, 0u },
    { "CUT, cell 64", 64.0f, 0.0f, SA1_GRID_CUT },
    { "CUT, cell 256", 256.0f, 0.0f, SA1_GRID_CUT },
};

static void sec_params(G *E)
{
    static const float degs[] = { 30, 45, 60 };
    static const int aps[] = { 2, 3, 5, 6, 7 };
    int gi, di, a;
    printf("params: Sonic, 2-triangle ramps, GRID parameters varied\n");
    for (gi = 0; gi < (int)(sizeof k_gp / sizeof k_gp[0]); gi++) {
        printf("  %-30s", k_gp[gi].name);
        for (a = 0; a < (int)(sizeof aps / sizeof aps[0]); a++) {
            printf(" %-15s", k_ap[aps[a]].name);
        }
        printf("\n");
        for (di = 0; di < 3; di++) {
            ramp_scene s;
            uint32_t n = 0;
            memset(&s, 0, sizeof s);
            s.deg = degs[di];
            s.H = 300.0f;
            s.W = 600.0f;
            scene_profile(&s);
            if (scene_set(E->ctx, &s, gi == 0 ? NULL : &k_gp[gi], &n) != SA1_OK) {
                printf("    set_static_ex: %s\n", sa1_last_error(E->ctx));
                continue;
            }
            printf("    %3.0f deg %19s", s.deg, "");
            for (a = 0; a < (int)(sizeof aps / sizeof aps[0]); a++) {
                ramp_result R;
                ramp_run(E->ctx, &s, &k_ap[aps[a]], SA1_CHAR_SONIC, &R);
                cell_print(&R);
            }
            {
                sa1_collision_info info;
                memset(&info, 0, sizeof info);
                info.struct_size = sizeof info;
                sa1_collision_info_get(E->ctx, &info);
                printf("   (%u GRID triangles, %u objects)", (unsigned)info.grid_triangles, (unsigned)info.grid_objects);
            }
            printf("\n");
            fflush(stdout);
        }
    }
}

static void sec_smooth(G *E)
{
    static const float degs[] = { 30, 45, 60 };
    static const struct {
        int n;
        float len;
    } fil[] = { { 0, 0.0f }, { 2, 40.0f }, { 4, 80.0f }, { 8, 160.0f } };
    static const int aps[] = { 2, 3, 5, 6, 7 };
    int fi, di, a;
    printf("smooth: Sonic, the foot of the slope rounded (the slope reached in n segments over a length)\n");
    for (fi = 0; fi < 4; fi++) {
        printf("  foot: %d segments over %3.0f units  ", fil[fi].n, fil[fi].len);
        for (a = 0; a < (int)(sizeof aps / sizeof aps[0]); a++) {
            printf(" %-15s", k_ap[aps[a]].name);
        }
        printf("\n");
        for (di = 0; di < 3; di++) {
            ramp_scene s;
            memset(&s, 0, sizeof s);
            s.deg = degs[di];
            s.H = 300.0f;
            s.W = 600.0f;
            s.fillet_n = fil[fi].n;
            s.fillet_len = fil[fi].len;
            scene_profile(&s);
            if (scene_set(E->ctx, &s, NULL, NULL) != SA1_OK) {
                continue;
            }
            printf("    %3.0f deg %24s", s.deg, "");
            for (a = 0; a < (int)(sizeof aps / sizeof aps[0]); a++) {
                ramp_result R;
                ramp_run(E->ctx, &s, &k_ap[aps[a]], SA1_CHAR_SONIC, &R);
                cell_print(&R);
                if (R.fell) {
                    note_fail(&s, aps[a], SA1_CHAR_SONIC, NULL);
                }
            }
            printf("\n");
            fflush(stdout);
        }
    }
    print_traces(E, E->trace > 2 ? 2 : E->trace);
}

/* plank ramps: the slope and the plateau as slabs of thickness T over a floor (undersides within reach) */
static void sec_slab(G *E)
{
    static const float degs[] = { 20, 30, 45 };
    static const float thick[] = { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f };
    static const int aps[] = { 2, 3, 4, 5, 6, 7 };
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_KNUCKLES };
    int di, ti, a, ki, total = 0, fell = 0;
    printf("slab: plank ramps (the slope a slab T thick, with its underside, ends and sides) up to a platform T thick, "
           "over a floor (G-SA1 thickens floors thinner than 3 SA1 units = 12 HU: BspWorldOptions::thin_floor_min)\n");
    for (ki = 0; ki < 2; ki++) {
        for (di = 0; di < 3; di++) {
            printf("  %s, %.0f deg     ", kind_name(kinds[ki]), degs[di]);
            for (a = 0; a < (int)(sizeof aps / sizeof aps[0]); a++) {
                if (k_ap[aps[a]].spin && kinds[ki] != SA1_CHAR_SONIC) {
                    continue;
                }
                printf(" %-15s", k_ap[aps[a]].name);
            }
            printf("\n");
            for (ti = 0; ti < (int)(sizeof thick / sizeof thick[0]); ti++) {
                ramp_scene s;
                memset(&s, 0, sizeof s);
                s.deg = degs[di];
                s.H = 300.0f;
                s.W = 600.0f;
                s.slab = thick[ti];
                scene_profile(&s);
                if (scene_set(E->ctx, &s, NULL, NULL) != SA1_OK) {
                    printf("    set_static: %s\n", sa1_last_error(E->ctx));
                    continue;
                }
                printf("    T %4.1f (%2.0f HU)", s.slab, s.slab * 4.0f);
                for (a = 0; a < (int)(sizeof aps / sizeof aps[0]); a++) {
                    ramp_result R;
                    if (k_ap[aps[a]].spin && kinds[ki] != SA1_CHAR_SONIC) {
                        continue;
                    }
                    ramp_run(E->ctx, &s, &k_ap[aps[a]], kinds[ki], &R);
                    cell_print(&R);
                    total++;
                    if (R.fell) {
                        fell++;
                        note_fail(&s, aps[a], kinds[ki], NULL);
                    }
                }
                printf("\n");
                fflush(stdout);
            }
        }
    }
    printf("  %d of %d runs fell through\n", fell, total);
    print_traces(E, E->trace);
}

/* what is under the slope: nothing (a solid wedge), the floor going on under it, a slab's underside */
static void sec_under(G *E)
{
    static const struct {
        const char *name;
        float slab;
        int floor_under;
    } kinds_s[] = {
        { "solid wedge (nothing under the slope)", 0.0f, 0 },
        { "surface only, floor goes on under it", 0.0f, 1 },
        { "slab 8, floor stops at the foot", 8.0f, -1 },
        { "slab 8, floor goes on under it", 8.0f, 0 },
        { "slab 3, floor stops at the foot", 3.0f, -1 },
        { "slab 3, floor goes on under it", 3.0f, 0 },
    };
    static const float degs[] = { 20, 30, 45 };
    static const int aps[] = { 2, 3, 4, 5, 6, 7 };
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_KNUCKLES };
    int si, di, a, ki;
    printf("under: what lies under the slope decides the fall-through (cells: result, speed at the foot, deepest, * = dipped "
           "more than 3 and came back)\n");
    for (ki = 0; ki < 2; ki++) {
        for (si = 0; si < (int)(sizeof kinds_s / sizeof kinds_s[0]); si++) {
            printf("  %s, %-40s", kind_name(kinds[ki]), kinds_s[si].name);
            for (a = 0; a < (int)(sizeof aps / sizeof aps[0]); a++) {
                if (k_ap[aps[a]].spin && kinds[ki] != SA1_CHAR_SONIC) {
                    continue;
                }
                printf(" %-15s", k_ap[aps[a]].name);
            }
            printf("\n");
            for (di = 0; di < 3; di++) {
                ramp_scene s;
                memset(&s, 0, sizeof s);
                s.deg = degs[di];
                s.H = 300.0f;
                s.W = 600.0f;
                s.slab = kinds_s[si].slab;
                s.floor_under = kinds_s[si].floor_under;
                scene_profile(&s);
                if (scene_set(E->ctx, &s, NULL, NULL) != SA1_OK) {
                    continue;
                }
                printf("    %3.0f deg %39s", s.deg, "");
                for (a = 0; a < (int)(sizeof aps / sizeof aps[0]); a++) {
                    ramp_result R;
                    if (k_ap[aps[a]].spin && kinds[ki] != SA1_CHAR_SONIC) {
                        continue;
                    }
                    ramp_run(E->ctx, &s, &k_ap[aps[a]], kinds[ki], &R);
                    cell_print(&R);
                }
                printf("\n");
                fflush(stdout);
            }
        }
    }
}

/* ramps flagged STAIRS (G-SA1 lays SOLID | STAIRS ramps over Source stairs: BspWorldOptions::stair_ramps) */
static void sec_stairs(G *E)
{
    static const float degs[] = { 20, 30, 35, 40, 45 };
    static const int aps[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    int di, a, sl, total = 0, fell = 0;
    printf("stairs: ramps whose slope is SOLID | STAIRS (COL 0x4000, the game's stair mode), plain and as slabs 3 thick\n");
    for (sl = 0; sl < 2; sl++) {
        printf("  %s    ", sl ? "slab 3" : "plain ");
        for (a = 0; a < (int)(sizeof aps / sizeof aps[0]); a++) {
            printf(" %-15s", k_ap[aps[a]].name);
        }
        printf("\n");
        for (di = 0; di < (int)(sizeof degs / sizeof degs[0]); di++) {
            ramp_scene s;
            memset(&s, 0, sizeof s);
            s.deg = degs[di];
            s.H = 300.0f;
            s.W = 600.0f;
            s.slab = sl ? 3.0f : 0.0f;
            s.slope_surface = SA1_SURFACE_SOLID | SA1_SURFACE_STAIRS;
            scene_profile(&s);
            if (scene_set(E->ctx, &s, NULL, NULL) != SA1_OK) {
                continue;
            }
            printf("    %3.0f deg ", s.deg);
            for (a = 0; a < (int)(sizeof aps / sizeof aps[0]); a++) {
                ramp_result R;
                ramp_run(E->ctx, &s, &k_ap[aps[a]], SA1_CHAR_SONIC, &R);
                cell_print(&R);
                total++;
                if (R.fell) {
                    fell++;
                    note_fail(&s, aps[a], SA1_CHAR_SONIC, NULL);
                }
            }
            printf("\n");
            fflush(stdout);
        }
    }
    printf("  %d of %d runs fell through\n", fell, total);
    print_traces(E, E->trace);
}

/* where: from flat ground, on the slope, landing on it from a jump */

static void sec_entry(G *E)
{
    static const float degs[] = { 30, 45 };
    int di, c;
    printf("entry: Sonic, 2-triangle ramps: where the fall-through happens\n");
    for (di = 0; di < 2; di++) {
        ramp_scene s;
        memset(&s, 0, sizeof s);
        s.deg = degs[di];
        s.H = 300.0f;
        s.W = 600.0f;
        scene_profile(&s);
        if (scene_set(E->ctx, &s, NULL, NULL) != SA1_OK) {
            continue;
        }
        for (c = 0; c < 7; c++) {
            approach ap = k_ap[3];
            ramp_result R;
            const char *what = "";
            g_jump_x = 1e30f;
            switch (c) {
            case 0: ap = k_ap[6]; what = "from flat ground, speed 10 at x = -150"; break;
            case 1: ap = k_ap[7]; what = "from flat ground, speed 16 at x = -150"; break;
            case 2: ap.start = s.x_top * 0.3f; ap.start_y = scene_surf(&s, ap.start) + 4.0f; ap.stick = 1.0f;
                    what = "standing on the slope (30%), running up"; break;
            case 3: ap.start = s.x_top * 0.3f; ap.start_y = scene_surf(&s, ap.start) + 4.0f; ap.boost = 10.0f;
                    what = "on the slope (30%), speed 10 set there"; break;
            case 4: ap.start = s.x_top * 0.3f; ap.start_y = scene_surf(&s, ap.start) + 4.0f; ap.boost = 16.0f;
                    what = "on the slope (30%), speed 16 set there"; break;
            case 5: ap = k_ap[6]; g_jump_x = -70.0f; what = "speed 10, jumping at x = -70 onto the slope"; break;
            case 6: ap = k_ap[3]; g_jump_x = -40.0f; what = "top running speed, jumping at x = -40 onto the slope"; break;
            }
            ramp_run(E->ctx, &s, &ap, SA1_CHAR_SONIC, &R);
            printf("  %2.0f deg, %-50s %-5s entry %5.2f, deepest %6.2f%s", s.deg, what, res_word(&R), R.entry_speed,
                   R.maxdepth, R.fell ? "" : "\n");
            if (R.fell) {
                printf(" at x %.1f (%.0f%% of the slope)\n", R.fall_x, R.fall_u * 100.0f);
                if (E->trace > 0) {
                    trace_fall(&s, &R, 3, 5);
                }
            }
            fflush(stdout);
        }
    }
    g_jump_x = 1e30f;
}

/* ---- stress: tick cost on odd geometry ----------------------------------------------------------------- */
static void stress_scene(int which, tbuf *b, const char **name, const gridp **gpo)
{
    static const gridp edge220 = { "max_edge 220", 256.0f, 220.0f, 0u };
    int i, k;
    memset(b, 0, sizeof *b);
    *gpo = NULL;
    switch (which) {
    case 0: {
        float p[4][3] = { { -2048, 0, -2048 }, { 2048, 0, -2048 }, { 2048, 0, 2048 }, { -2048, 0, 2048 } };
        tb_quad_tiles(b, p, 1, 1, SA1_SURFACE_SOLID);
        *name = "a plain floor (2 triangles, reference)";
        break;
    }
    case 1: case 2: case 3: {
        float t = which == 1 ? 8.0f : which == 2 ? 4.0f : 2.0f, half = which == 3 ? 160.0f : 320.0f;
        float p[4][3] = { { -half, 0, -half }, { half, 0, -half }, { half, 0, half }, { -half, 0, half } };
        float q[4][3] = { { -2048, -0.5f, -2048 }, { 2048, -0.5f, -2048 }, { 2048, -0.5f, 2048 }, { -2048, -0.5f, 2048 } };
        tb_quad_tiles(b, p, (int)(2 * half / t), (int)(2 * half / t), SA1_SURFACE_SOLID);
        tb_quad_tiles(b, q, 1, 1, SA1_SURFACE_SOLID);   /* a floor half a unit below, as a safety net */
        *name = which == 1 ? "dense floor: 8-unit tiles (640 x 640), a plain floor 0.5 below"
              : which == 2 ? "dense floor: 4-unit tiles (640 x 640), a plain floor 0.5 below"
                           : "dense floor: 2-unit tiles (320 x 320), a plain floor 0.5 below";
        break;
    }
    case 4: {
        /* bumpy terrain like a displacement: 8-unit grid, +-6 units */
        int n = 96;
        float c = 8.0f;
        for (i = 0; i < n; i++) {
            for (k = 0; k < n; k++) {
                float x0 = (i - n / 2) * c, z0 = (k - n / 2) * c, v[4][3];
                int a;
                for (a = 0; a < 4; a++) {
                    float x = x0 + ((a == 1 || a == 2) ? c : 0.0f), z = z0 + (a >= 2 ? c : 0.0f);
                    v[a][0] = x;
                    v[a][1] = 6.0f * sinf(x * 0.05f) * cosf(z * 0.043f) + 2.0f * sinf((x + z) * 0.21f);
                    v[a][2] = z;
                }
                tb_tri(b, v[0], v[2], v[1], SA1_SURFACE_SOLID);
                tb_tri(b, v[0], v[3], v[2], SA1_SURFACE_SOLID);
            }
        }
        *name = "bumpy terrain (8-unit grid, 768 x 768, +-8 units: a displacement)";
        break;
    }
    case 5: {
        float p[4][3] = { { -1024, 0, -1024 }, { 1024, 0, -1024 }, { 1024, 0, 1024 }, { -1024, 0, 1024 } };
        for (i = 0; i < 20; i++) {
            tb_quad_tiles(b, p, 1, 1, SA1_SURFACE_SOLID);
        }
        *name = "20 coplanar copies of one floor (overlapping brush faces)";
        break;
    }
    case 6: {
        /* a fan of 1000 slivers around the origin */
        float c[3] = { 0, 0, 0 };
        for (i = 0; i < 1000; i++) {
            float a0 = (float)(2.0 * PI * i / 1000.0), a1 = (float)(2.0 * PI * (i + 1) / 1000.0);
            float p0[3] = { 900.0f * cosf(a0), 0, -900.0f * sinf(a0) }, p1[3] = { 900.0f * cosf(a1), 0, -900.0f * sinf(a1) };
            tb_tri(b, c, p0, p1, SA1_SURFACE_SOLID);
        }
        *name = "a fan of 1000 slivers (5.6 units wide at 900 from the centre)";
        break;
    }
    case 7: {
        float p[4][3] = { { -1024, 0, -1024 }, { 1024, 0, -1024 }, { 1024, 0, 1024 }, { -1024, 0, 1024 } };
        for (i = 0; i < 40; i++) {
            for (k = 0; k < 4; k++) {
                p[k][1] = -(float)i;
            }
            tb_quad_tiles(b, p, 1, 1, SA1_SURFACE_SOLID);
        }
        *name = "40 floors stacked 1 unit apart";
        break;
    }
    case 8: {
        float p[4][3] = { { -20000, 0, -20000 }, { 20000, 0, -20000 }, { 20000, 0, 20000 }, { -20000, 0, 20000 } };
        tb_quad_tiles(b, p, 1, 1, SA1_SURFACE_SOLID);
        *name = "a 40000-unit floor of 2 triangles, max_edge 220";
        *gpo = &edge220;
        break;
    }
    }
}

static void sec_stress(G *E)
{
    int which;
    printf("stress: Sonic for 600 pairs (runs in circles at full stick, a spin dash every 100 pairs, a jump every 50); "
           "per-step time of the 60 Hz steps\n");
    for (which = 0; which <= 8; which++) {
        tbuf b;
        const char *name = "";
        const gridp *gp = NULL;
        sa1_character_id id = 0;
        sa1_state_ex x;
        sa1_collision_info info;
        sa1ca_world_stats ws;
        double t0, t_set, t_create, sum = 0.0;
        uint32_t i, k, slow1 = 0, slow5 = 0, slow50 = 0, near_full = 0, nsteps = 0;
        int touch_max = 0, near_max = 0;
        float miny = 1e9f;
        sa1_status st;
        stress_scene(which, &b, &name, &gp);
        t0 = sa1_now_ms();
        st = set_static_grid(E->ctx, &b, gp);
        t_set = sa1_now_ms() - t0;
        if (st != SA1_OK) {
            printf("  %s: set_static: %s\n", name, sa1_last_error(E->ctx));
            tb_free(&b);
            continue;
        }
        rec_reset();
        t0 = sa1_now_ms();
        st = char_at(E->ctx, SA1_CHAR_SONIC, 3.0f, 12.0f, 5.0f, 0, &id);
        t_create = sa1_now_ms() - t0;
        if (SA1_FAILED(st)) {
            printf("  %s: create: %s %s\n", name, sa1_status_string(st), sa1_last_error(E->ctx));
            tb_free(&b);
            continue;
        }
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        sa1_collision_info_get(E->ctx, &info);
        for (i = 0; i < 600; i++) {
            uint32_t btn = 0;
            float sx = 0.0f, sy = 1.0f;
            sa1_input in;
            if (i % 100 >= 70 && i % 100 < 90) {
                btn = SA1_BUTTON_ACTION;
                sy = 0.0f;
            } else if (i % 50 == 25) {
                btn = SA1_BUTTON_JUMP;
            }
            memset(&in, 0, sizeof in);
            in.struct_size = sizeof in;
            exact_stick(sx, sy, &in.stick_x, &in.stick_y);
            in.camera_yaw = 0.035f * (float)i;
            in.buttons = btn;
            st = sa1_character_tick(E->ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
            x.struct_size = sizeof x;
            sa1_character_state_ex(E->ctx, id, &x);
            if (SA1_FAILED(st)) {
                printf("    tick: %s %s\n", sa1_status_string(st), sa1_last_error(E->ctx));
                break;
            }
            if (x.sa1_position[1] < miny) {
                miny = x.sa1_position[1];
            }
        }
        {
            /* the first step below every floor of the scene (fell through), with the steps before it */
            float lowest = which == 4 ? -8.5f : which == 7 ? -40.0f : which >= 1 && which <= 3 ? -0.5f : 0.0f;
            for (k = 0; k < g_nrec; k++) {
                if (g_rec[k].pos[1] < lowest - 3.0f) {
                    uint32_t a = k > 8 ? k - 8 : 0, e = k + 3 < g_nrec ? k + 3 : g_nrec;
                    printf("    fell below the lowest floor (y %.1f) at step %u:\n", lowest, (unsigned)g_rec[k].step);
                    for (; a < e; a++) {
                        rec_print(&g_rec[a], 0.0f);
                    }
                    break;
                }
            }
        }
        for (k = 0; k < g_nrec; k++) {
            const steprec *r = &g_rec[k];
            nsteps++;
            sum += r->ms;
            slow1 += r->ms > 1.0;
            slow5 += r->ms > 5.0;
            slow50 += r->ms > 50.0;
            near_full += r->near_n >= (r->near_cap ? r->near_cap : 96);
            near_max = r->near_n > near_max ? r->near_n : near_max;
            touch_max = r->touch_n > touch_max ? r->touch_n : touch_max;
        }
        sa1ca_world_stats_get(sa1_character_world(E->ctx, id), &ws);
        printf("  %s\n    %u triangles -> %u GRID triangles in %u objects; set_static %.1f ms, create (with the GRID "
               "build) %.1f ms\n    steps %u: mean %.3f ms, longest %.2f ms, %u > 1 ms, %u > 5 ms, %u > 50 ms; NEAR up to "
               "%d (full in %u steps), TOUCH up to %d, matrix depth up to %d (limit %u); window last %u objects / %u "
               "polygons, steps with objects left out: %u (objects) %u (polygons) %u (near); lowest y %.2f\n",
               name, (unsigned)b.n, (unsigned)info.grid_triangles, (unsigned)info.grid_objects, t_set, t_create,
               (unsigned)nsteps, nsteps ? sum / nsteps : 0.0, g_step_max_ms, (unsigned)slow1, (unsigned)slow5,
               (unsigned)slow50, near_max, (unsigned)near_full, touch_max, (int)g_mtx_max,
               (unsigned)rd32(sa1_character_world(E->ctx, id), A_MTX_LIMIT), (unsigned)ws.window_objects,
               (unsigned)ws.window_polygons, (unsigned)ws.over_objects, (unsigned)ws.over_polygons,
               (unsigned)ws.over_near, miny);
        fflush(stdout);
        sa1_character_destroy(E->ctx, id);
        tb_free(&b);
    }
}

/* gm_freespace_13's big ramp, rebuilt: a plank 1328 x 59 SA1 units (5312 x 236 Hammer units) at 24.5 degrees, 3 thick,
   from a floor up to a platform, its faces as single long triangles (as the BSP conversion sends them) or cut into
   well-shaped 32-unit tiles; GRID defaults (sa1_collision_set_static: longest edges halved down to 128) */
static void sec_needle(G *E)
{
    static const int aps[] = { 2, 3, 4, 5, 6, 7 };
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_KNUCKLES };
    static const float tiles[] = { 0.0f, 32.0f };
    int ti, ki, a;
    printf("needle: a long narrow plank ramp (1328 x 59, 24.5 deg, 3 thick); cells: result, speed at the foot, deepest, the "
           "fullest NEAR-poly list (of the world's 256)\n");
    for (ti = 0; ti < 2; ti++) {
        ramp_scene s;
        uint32_t n = 0;
        sa1_collision_info info;
        memset(&s, 0, sizeof s);
        s.deg = 24.5f;
        s.H = 1328.0f * tanf(24.5f * (float)PI / 180.0f);
        s.W = 29.5f;
        s.slab = 3.0f;
        scene_profile(&s);
        g_slab_tile = tiles[ti];
        if (scene_set(E->ctx, &s, NULL, &n) != SA1_OK) {
            g_slab_tile = 0.0f;
            printf("  set_static: %s\n", sa1_last_error(E->ctx));
            continue;
        }
        g_slab_tile = 0.0f;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        sa1_collision_info_get(E->ctx, &info);
        for (ki = 0; ki < 2; ki++) {
            printf("  %s, faces %s (%u triangles in)", kind_name(kinds[ki]), tiles[ti] > 0.0f ? "in 32-unit tiles" : "as long triangles",
                   (unsigned)n);
            for (a = 0; a < (int)(sizeof aps / sizeof aps[0]); a++) {
                ramp_result R;
                if (k_ap[aps[a]].spin && kinds[ki] != SA1_CHAR_SONIC) {
                    continue;
                }
                ramp_run(E->ctx, &s, &k_ap[aps[a]], kinds[ki], &R);
                printf(" | %s:", k_ap[aps[a]].name);
                cell_print(&R);
                printf(" N%d", R.near_max);
            }
            printf("\n");
            fflush(stdout);
        }
    }
}

/* a character created close to a wall (G-SA1 spawns at the player's position: the GMod player hull keeps 16 Hammer units
   = 4 SA1 units between its centre and a wall; Knuckles' land sphere has a radius of 5.7, Gamma's 10.5, Big's 8.5) */
static void sec_wallspawn(G *E)
{
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES };
    static const float dist[] = { 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };
    int fl, ki, di;
    printf("wallspawn: created standing 2 units above a floor, d units from a wall (the wall's face x = 0, facing -x; "
           "floor y = 0); the lowest y in 3 s and where it ends\n");
    for (fl = 0; fl < 2; fl++) {
        tbuf b;
        memset(&b, 0, sizeof b);
        if (fl == 0) {
            float p[4][3] = { { -600, 0, -600 }, { 600, 0, -600 }, { 600, 0, 600 }, { -600, 0, 600 } };
            tb_quad_tiles(&b, p, 1, 1, SA1_SURFACE_SOLID);
        } else {
            /* the floor as needles: 172 x 4 strips (gm_construct's floor next to its garage wall) */
            int i;
            for (i = 0; i < 60; i++) {
                float z0 = -120.0f + 4.0f * i, z1 = z0 + 4.0f;
                float a[3] = { -172, 0, z0 }, c[3] = { -172, 0, z1 }, d[3] = { 0, 0, z1 }, e[3] = { 0, 0, z0 };
                tb_tri(&b, a, c, d, SA1_SURFACE_SOLID);
                tb_tri(&b, a, d, e, SA1_SURFACE_SOLID);
            }
            {
                float p[4][3] = { { -600, -0.0f, -600 }, { -172, 0, -600 }, { -172, 0, 600 }, { -600, 0, 600 } };
                tb_quad_tiles(&b, p, 1, 1, SA1_SURFACE_SOLID);
            }
        }
        {
            /* the wall: x = 0, from y = -20 to 60, facing -x */
            float p[4][3] = { { 0, -20, -600 }, { 0, -20, 600 }, { 0, 60, 600 }, { 0, 60, -600 } };
            float e1[3] = { 0, 0, 1200 }, e2[3] = { 0, 80, 1200 };
            float nx = e1[1] * e2[2] - e1[2] * e2[1];
            if (nx < 0.0f) {
                tb_tri(&b, p[0], p[1], p[2], SA1_SURFACE_SOLID);
                tb_tri(&b, p[0], p[2], p[3], SA1_SURFACE_SOLID);
            } else {
                tb_tri(&b, p[0], p[2], p[1], SA1_SURFACE_SOLID);
                tb_tri(&b, p[0], p[3], p[2], SA1_SURFACE_SOLID);
            }
        }
        set_static_grid(E->ctx, &b, NULL);
        tb_free(&b);
        printf("  floor %s\n", fl ? "of 172 x 4 needles" : "of 2 triangles");
        for (ki = 0; ki < 3; ki++) {
            printf("    %-9s", kind_name(kinds[ki]));
            for (di = 0; di < (int)(sizeof dist / sizeof dist[0]); di++) {
                sa1_character_id id = 0;
                sa1_state_ex x;
                float miny = 1e9f;
                int i;
                rec_reset();
                if (SA1_FAILED(char_at(E->ctx, kinds[ki], -dist[di], 2.0f, 0.0f, 0, &id))) {
                    printf(" d %.0f: ERR", dist[di]);
                    continue;
                }
                for (i = 0; i < 90; i++) {
                    pair(E->ctx, id, 0.0f, 0.0f, 0u, &x);
                    miny = x.sa1_position[1] < miny ? x.sa1_position[1] : miny;
                }
                printf(" | d %.0f: lowest %7.2f end y %8.2f%s", dist[di], miny, x.sa1_position[1],
                       x.sa1_position[1] < -3.0f ? " FELL" : "");
                sa1_character_destroy(E->ctx, id);
            }
            printf("\n");
        }
    }
}

/* one run, every step printed from 12 steps before the foot: RAMP_CASE="kind deg slab tile fillet_n fillet_len approach"
   (kind: 0 Sonic 2 Tails 3 Knuckles; approach: an index into k_ap) */
static void sec_case(G *E)
{
    const char *env = getenv("RAMP_CASE");
    ramp_scene s;
    ramp_result R;
    int kind = 0, ap = 7, k, first = -1, n = 0;
    if (env == NULL) {
        printf("case: set RAMP_CASE=\"kind deg slab tile fillet_n fillet_len approach\"\n");
        return;
    }
    memset(&s, 0, sizeof s);
    s.H = 300.0f;
    s.W = 600.0f;
    if (sscanf(env, "%d %f %f %f %d %f %d %d", &kind, &s.deg, &s.slab, &s.tile, &s.fillet_n, &s.fillet_len, &ap,
               &s.floor_under) < 2 || ap < 0
        || ap >= NAP) {
        printf("case: bad RAMP_CASE\n");
        return;
    }
    scene_profile(&s);
    if (scene_set(E->ctx, &s, NULL, NULL) != SA1_OK) {
        printf("case: set_static: %s\n", sa1_last_error(E->ctx));
        return;
    }
#ifdef RAMP_TOUCH_HOOK
    g_ntl = g_ntlp = 0;
    g_tl_on = 1;
#endif
    ramp_run(E->ctx, &s, &k_ap[ap], kind, &R);
#ifdef RAMP_TOUCH_HOOK
    g_tl_on = 0;
#endif
    printf("case: %s, %.0f deg, slab %.1f, tile %.0f, foot %d x %.0f, %s: %s, entry %.2f, deepest %.2f, fell at x %.1f\n",
           kind_name(kind), s.deg, s.slab, s.tile, s.fillet_n, s.fillet_len, k_ap[ap].name, res_word(&R), R.entry_speed,
           R.maxdepth, R.fall_x);
    for (k = 0; k < (int)g_nrec; k++) {
        if (g_rec[k].pos[0] > -60.0f) {
            first = k;
            break;
        }
    }
    for (k = first < 0 ? 0 : first; k < (int)g_nrec && n < 40; k++, n++) {
        rec_print(&g_rec[k], rec_depth(&s, &g_rec[k]));
    }
#ifdef RAMP_TOUCH_HOOK
    {
        /* every TOUCH call of the steps around the first step more than 3 below the surface (or the deepest dip) */
        int at = R.first_fall >= 0 ? R.first_fall : R.first_below;
        if (at < 0) {
            at = first;
        }
        if (at >= 0 && at < (int)g_nrec) {
            uint64_t s0 = g_rec[at].step > 3u ? g_rec[at].step - 3u : 0u, s1 = g_rec[at].step + 1u;
            printf("    TOUCH calls (8C029B00) of steps %u..%u (%u calls logged in the run):\n", (unsigned)s0, (unsigned)s1,
                   (unsigned)g_ntl);
            for (k = 0; k < (int)g_nrec; k++) {
                if (g_rec[k].step >= s0 && g_rec[k].step <= s1) {
                    rec_print(&g_rec[k], rec_depth(&s, &g_rec[k]));
                    tl_print(g_rec[k].step, g_rec[k].step);
                }
            }
        }
    }
#endif
}

/* what a jingle / music / voice event costs the host: the mixer reads the whole ADX file from the disc image when the
   clip starts (sa1_audio_mix.c start_clip -> sa1a_read_adx), on the thread that hands it the event (G-SA1: the Lua
   thread, Session::take_events -> Audio::event). sa1_audio_stream_open does the same read. */
static void sec_audio(G *E)
{
    const char *gdi = getenv("RAMP_GDI");
    sa1_audio_desc d;
    sa1_audio *a = NULL;
    uint32_t i, nm, nv;
    double worst = 0.0, sum = 0.0;
    const char *worst_name = "";
    if (gdi == NULL) {
        printf("audio: (no RAMP_GDI)\n");
        return;
    }
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.gdi_path = gdi;
    d.stage = 1;
    if (SA1_FAILED(sa1_audio_open(E->ctx, &d, &a)) || a == NULL) {
        printf("audio: sa1_audio_open failed: %s\n", sa1_last_error(E->ctx));
        return;
    }
    nm = sa1_audio_music_count(a);
    nv = sa1_audio_voice_count(a);
    printf("audio: %u music files, %u voices; time of sa1_audio_stream_open (the whole ADX read from the disc image)\n", nm, nv);
    for (i = 0; i < nm; i++) {
        sa1_audio_stream *st = NULL;
        sa1_audio_stream_info info;
        double t0 = sa1_now_ms(), t;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        if (SA1_FAILED(sa1_audio_stream_open(a, SA1_AUDIO_SOURCE_MUSIC, i, &st, &info))) {
            continue;
        }
        t = sa1_now_ms() - t0;
        sum += t;
        if (t > worst) {
            worst = t;
            worst_name = sa1_audio_music_name(a, i);
        }
        if (t > 20.0) {
            printf("    music %3u %-14s %.1f ms\n", i, sa1_audio_music_name(a, i), t);
        }
        sa1_audio_stream_close(st);
    }
    printf("  music: %.0f ms in all, the slowest %s %.1f ms\n", sum, worst_name, worst);
    worst = sum = 0.0;
    for (i = 0; i < nv && i < 400; i++) {
        sa1_audio_stream *st = NULL;
        sa1_audio_stream_info info;
        double t0 = sa1_now_ms(), t;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        if (SA1_FAILED(sa1_audio_stream_open(a, SA1_AUDIO_SOURCE_VOICE, i, &st, &info))) {
            continue;
        }
        t = sa1_now_ms() - t0;
        sum += t;
        worst = t > worst ? t : worst;
        sa1_audio_stream_close(st);
    }
    printf("  voices 0..%u: %.0f ms in all, the slowest %.1f ms\n", nv < 400 ? nv - 1 : 399, sum, worst);
    sa1_audio_close(a);
}

/* ---- main ---------------------------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        void (*fn)(G *);
    } secs[] = {
        { "ramps", sec_ramps }, { "threshold", sec_threshold }, { "params", sec_params }, { "smooth", sec_smooth },
        { "entry", sec_entry }, { "slab", sec_slab }, { "stairs", sec_stairs }, { "stress", sec_stress },
        { "case", sec_case }, { "under", sec_under }, { "wallspawn", sec_wallspawn }, { "needle", sec_needle },
        { "audio", sec_audio },
    };
    const char *only[16], *gdi = NULL;
    int nonly = 0, i, s;
    G E;
    memset(&E, 0, sizeof E);
    E.trace = 3;
    ga_args(&argc, argv);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--only") && i + 1 < argc && nonly < 16) {
            only[nonly++] = argv[++i];
        } else if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
            E.trace = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--verbose")) {
            g_verbose = 1;
        } else if (!strcmp(argv[i], "--keep-undersides")) {
            g_extra_flags |= SA1_GRID_KEEP_UNDERSIDES;
        } else if (!strcmp(argv[i], "--undersides-drop")) {
            g_extra_flags |= SA1_GRID_UNDERSIDES_DROP;      /* round 0.9.3: the undersides left out as before (comparison) */
        } else if (!strcmp(argv[i], "--legacy-split")) {
            g_legacy_split = 1;
        } else {
            gdi = argv[i];
        }
    }
    if (gdi == NULL) {
        printf("usage: ramp_diag [--only ramps|threshold|params|smooth|entry|slab|stairs|stress]... [--trace N] [--verbose] "
               "[--keep-undersides] [--undersides-drop] [--legacy-split] GDI\n");
        return 2;
    }
    printf("libsa1 %s ramp / tick diagnostics (%d-bit), game code: %s\n", sa1_version_string(),
           (int)(sizeof(void *) * 8), sa1g_game_info());
    if (!sa1g_game_ready()) {
        printf("the step's game code is not linked\n");
        return 1;
    }
    E.ctx = make_ctx(gdi);
    if (E.ctx == NULL) {
        return 1;
    }
    for (s = 0; s < (int)(sizeof secs / sizeof secs[0]); s++) {
        int run_it = nonly == 0;
        double t0 = sa1_now_ms();
        for (i = 0; i < nonly; i++) {
            run_it |= !strcmp(only[i], secs[s].name);
        }
        if (!run_it) {
            continue;
        }
        secs[s].fn(&E);
        printf("  (%s: %.1f s)\n", secs[s].name, (sa1_now_ms() - t0) / 1000.0);
        fflush(stdout);
    }
    sa1_collision_set_static(E.ctx, NULL, 0);
    sa1_shutdown(E.ctx);
    return 0;
}
