/*
 * decal_test.c - eyes, irises, pupils and decals stay in front of the faces they lie on at every alpha of the render
 * interpolation (sadc docs/research/gsa1_060_render.md 3.5 / 3.6 and its Review R3.4 / R3.5 / R4; owner report on G-SA1
 * 0.6.0: "Big's eye textures have Z-fighting").
 *
 * The characters' eye layers are separate models 0.016-0.2 HU in front of the face (Big's eye whites over the dark eye
 * patch, his pupils over the eye whites, Sonic's irises over his eye whites, Tails', Knuckles', Amy's and E-102's eyes).
 * libsa1 0.2 interpolated every drawn model's world matrix on its own: in a turn the layers drifted apart by up to 2 HU
 * on the frames between the pairs. The anchored interpolation (sa1_character.c plan_build) interpolates a model rigidly
 * attached to another relative to it.
 *
 * Each character runs Emerald Coast act 1's own collision (DISC, its start table, Source coordinates: 4 HU per unit),
 * one pair per tick, the script 20n30f20c20C30n15R15L (stand, run, run in curves left and right, stand, turn in place
 * right and left). After every pair the separation of the upper layer above the lower one (along the lower triangle's
 * normal, at the centroids of their overlaps seen along it; tests the probe's sep_of) is measured in
 * sa1_character_render at alpha 0 and 1 (the two pairs) and at .25, .5, .75. A pair whose overlap exists at both ends
 * FAILS when, at a mid alpha, the overlap is lost (Review R3.4: Big's eye whites moved more than 2 HU away), drops below
 * half the ends' overlap count, or its smallest separation is more than 0.01 HU below the smaller of the two ends'.
 * Required (Review R7.2): 0 failing pairs for Big (both layers), Tails, Amy and E-102; at most 2 for Sonic; at most 7 for
 * Knuckles: in action 22 (a turn in place whose own pose already puts the eyes inside the face at pair frames) without
 * further limit, in other actions dipping less than 0.005 HU below the face (the doc's "all 7 in action 22" does not
 * hold for its own prototype, render_probe.c method 3: 3 of its 7 pairs are in actions 10 / 11, by the same amounts as
 * here, -0.0022 HU at worst: the eye's own motion against the face). The pair after the spawn (R3.5: libsa1 0.2 put
 * Big's eye whites 1.64 HU behind the patch there) is counted like the others.
 * libsa1 0.2: Big 14 (12 losing the overlap) + 4, Tails 41, E-102 9, Amy 1, Sonic 23, Knuckles 33.
 *
 *   decal_test [--verbose] GDI        (build / run: tests/build_decal.py --run)
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"

static int g_fail, g_verbose;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok    ");                                                \
        } else {                                                               \
            printf("  FAIL  ");                                                \
            g_fail++;                                                          \
        }                                                                      \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
    } while (0)

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN)
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : "warn", msg);
}

static sa1_context *mk_ctx(const char *gdi, int32_t kind)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_collision_static col;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK)
        return NULL;
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = 1u << kind;
    lo.flags = SA1_LOAD_DEFAULT;
    memset(&col, 0, sizeof col);
    col.struct_size = sizeof col;
    col.mode = SA1_COLLISION_MODE_DISC;
    col.disc_level = 1;
    col.disc_act = 0;
    if (SA1_FAILED(sa1_load_disc(ctx, gdi, &lo)) || sa1_collision_set_static_ex(ctx, &col) != SA1_OK) {
        printf("  context: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

/* ---- geometry (the render probe's: sadc build/research/render/render_probe.c) ---- */
typedef struct v3 {
    double x, y, z;
} v3;

static v3 vsub(v3 a, v3 b)
{
    v3 r;
    r.x = a.x - b.x;
    r.y = a.y - b.y;
    r.z = a.z - b.z;
    return r;
}

static v3 vcross(v3 a, v3 b)
{
    v3 r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}

static double vdot(v3 a, v3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static double vlen(v3 a)
{
    return sqrt(vdot(a, a));
}

static v3 tri_pt(const float *p, uint32_t t, int k)
{
    v3 r;
    r.x = p[9u * t + 3u * k];
    r.y = p[9u * t + 3u * k + 1u];
    r.z = p[9u * t + 3u * k + 2u];
    return r;
}

/* 2D convex clip (Sutherland-Hodgman): poly (n points) clipped by triangle c (ccw); returns the area of the result and
 * its centroid */
static double clip_area(const double *px, const double *py, int n, const double cx[3], const double cy[3], double *mx,
                        double *my)
{
    double ax[32], ay[32], bx[32], by[32], area = 0.0, sx = 0.0, sy = 0.0;
    int i, e, m;
    for (i = 0; i < n; i++) {
        ax[i] = px[i];
        ay[i] = py[i];
    }
    m = n;
    for (e = 0; e < 3 && m > 0; e++) {
        double ex = cx[(e + 1) % 3] - cx[e], ey = cy[(e + 1) % 3] - cy[e];
        int k = 0;
        for (i = 0; i < m; i++) {
            int j = (i + 1) % m;
            double si = ex * (ay[i] - cy[e]) - ey * (ax[i] - cx[e]);
            double sj = ex * (ay[j] - cy[e]) - ey * (ax[j] - cx[e]);
            if (si >= 0) {
                bx[k] = ax[i];
                by[k] = ay[i];
                k++;
            }
            if ((si >= 0) != (sj >= 0) && k < 31) {
                double t = si / (si - sj);
                bx[k] = ax[i] + t * (ax[j] - ax[i]);
                by[k] = ay[i] + t * (ay[j] - ay[i]);
                k++;
            }
        }
        m = k;
        for (i = 0; i < m; i++) {
            ax[i] = bx[i];
            ay[i] = by[i];
        }
    }
    for (i = 0; i < m; i++) {
        int j = (i + 1) % m;
        double cr = ax[i] * ay[j] - ax[j] * ay[i];
        area += cr;
        sx += (ax[i] + ax[j]) * cr;
        sy += (ay[i] + ay[j]) * cr;
    }
    area *= 0.5;
    if (fabs(area) > 1e-12) {
        *mx = sx / (6 * area);
        *my = sy / (6 * area);
    }
    return fabs(area);
}

static int tex_is(const sa1_context *ctx, int32_t kind, const sa1_draw *d, const char *name)
{
    sa1_texture_info ti;
    if (d->archive < 0 || d->texture < 0)
        return 0;
    memset(&ti, 0, sizeof ti);
    ti.struct_size = sizeof ti;
    return sa1_texture_get(ctx, kind, (uint32_t)d->archive, (uint32_t)d->texture, &ti) == SA1_OK && ti.name &&
           strcmp(ti.name, name) == 0;
}

/* the smallest separation of tb's triangles above ta's (host units, along ta's normal, at the centroid of each
 * overlap of a near-parallel pair within 2 HU); returns the number of overlapping pairs */
static uint32_t sep_of(const sa1_context *ctx, int32_t kind, const sa1_render *r, const char *ta, const char *tb,
                       double *smin)
{
    uint32_t d, e, t, u, n = 0;
    *smin = 1e30;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *A = &r->draws[d];
        if (A->prim != SA1_PRIM_TRIANGLES || !tex_is(ctx, kind, A, ta))
            continue;
        for (e = 0; e < r->num_draws; e++) {
            const sa1_draw *B = &r->draws[e];
            if (B->prim != SA1_PRIM_TRIANGLES || !tex_is(ctx, kind, B, tb))
                continue;
            for (t = A->first; t < A->first + A->count; t++) {
                v3 a0 = tri_pt(r->positions, t, 0), a1 = tri_pt(r->positions, t, 1), a2 = tri_pt(r->positions, t, 2);
                v3 na = vcross(vsub(a1, a0), vsub(a2, a0)), e1, e2;
                double la = vlen(na), ax[3], ay[3], l1;
                if (la < 1e-9)
                    continue;
                na.x /= la;
                na.y /= la;
                na.z /= la;
                e1 = vsub(a1, a0);
                l1 = vlen(e1);
                e1.x /= l1;
                e1.y /= l1;
                e1.z /= l1;
                e2 = vcross(na, e1);
                ax[0] = 0;
                ay[0] = 0;
                ax[1] = vdot(vsub(a1, a0), e1);
                ay[1] = vdot(vsub(a1, a0), e2);
                ax[2] = vdot(vsub(a2, a0), e1);
                ay[2] = vdot(vsub(a2, a0), e2);
                for (u = B->first; u < B->first + B->count; u++) {
                    v3 b[3], nb;
                    double px[16], py[16], mx = 0, my = 0, area, lb, sep;
                    int k;
                    for (k = 0; k < 3; k++)
                        b[k] = tri_pt(r->positions, u, k);
                    nb = vcross(vsub(b[1], b[0]), vsub(b[2], b[0]));
                    lb = vlen(nb);
                    if (lb < 1e-9)
                        continue;
                    nb.x /= lb;
                    nb.y /= lb;
                    nb.z /= lb;
                    if (vdot(na, nb) < 0.7)
                        continue;
                    for (k = 0; k < 3; k++) {
                        px[k] = vdot(vsub(b[k], a0), e1);
                        py[k] = vdot(vsub(b[k], a0), e2);
                    }
                    if ((px[1] - px[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (px[2] - px[0]) < 0) {
                        double tx = px[1], ty = py[1];
                        px[1] = px[2];
                        py[1] = py[2];
                        px[2] = tx;
                        py[2] = ty;
                    }
                    area = clip_area(px, py, 3, ax, ay, &mx, &my);
                    if (area < 1e-5)
                        continue;
                    {
                        v3 c;
                        c.x = a0.x + mx * e1.x + my * e2.x;
                        c.y = a0.y + mx * e1.y + my * e2.y;
                        c.z = a0.z + mx * e1.z + my * e2.z;
                        sep = vdot(nb, vsub(b[0], c)) / vdot(nb, na);
                    }
                    if (fabs(sep) > 2.0)
                        continue;
                    n++;
                    if (sep < *smin)
                        *smin = sep;
                }
            }
        }
    }
    return n;
}

typedef struct layer {
    const char *base, *upper;
} layer;

typedef struct cdef {
    int32_t kind;
    layer layers[2];
    uint32_t nlayers;
    uint32_t allowed;               /* failing pairs allowed */
    int32_t allowed_action;         /* -1: any; else every failing pair must be in this action */
} cdef;

static const cdef k_chars[] = {
    { SA1_CHAR_BIG, { { "btx_panda", "btx_eye1" }, { "btx_eye1", "b_eye0" } }, 2, 0, -1 },
    { SA1_CHAR_TAILS, { { "mtx_white", "mtx_eye3" }, { NULL, NULL } }, 1, 0, -1 },
    { SA1_CHAR_AMY, { { "a_weye", "atx_eye00" }, { NULL, NULL } }, 1, 0, -1 },
    { SA1_CHAR_GAMMA, { { "etx_eyes", "etx_red7" }, { NULL, NULL } }, 1, 0, -1 },
    { SA1_CHAR_SONIC, { { "stx_eye00", "stx_eye2" }, { NULL, NULL } }, 1, 2, -1 },
    { SA1_CHAR_KNUCKLES, { { "ktx_siro", "ktx_eye2" }, { NULL, NULL } }, 1, 7, 22 },
};

#define SCRIPT "20n30f20c20C30n15R15L"

static void run_char(const char *gdi, const cdef *c)
{
    static const float mids[3] = { 0.25f, 0.5f, 0.75f };
    sa1_context *ctx = mk_ctx(gdi, c->kind);
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_input in;
    const char *p = SCRIPT;
    uint32_t tick = 0, L;
    uint32_t pairs[2] = { 0, 0 }, failing[2] = { 0, 0 }, other_action[2] = { 0, 0 }, lost[2] = { 0, 0 };
    double worst[2] = { 1e30, 1e30 }, other_sink[2] = { 1e30, 1e30 };
    int ok = ctx != NULL;
    if (ok) {
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.kind = c->kind;
        d.flags = SA1_CREATE_WORLD | SA1_CREATE_START_TABLE;
        ok = !SA1_FAILED(sa1_character_create_ex(ctx, &d, &id));
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.camera_yaw = 0.0f;
    while (ok && *p) {
        char *e;
        long n = strtol(p, &e, 10), i;
        char cmd;
        if (e == p || !*e)
            break;
        cmd = *e;
        p = e + 1;
        in.stick_y = (cmd == 'f' || cmd == 'c' || cmd == 'C') ? 1.0f : 0.0f;
        in.stick_x = (cmd == 'R' || cmd == 'c') ? 1.0f : (cmd == 'L' || cmd == 'C') ? -1.0f : 0.0f;
        in.buttons = 0u;
        for (i = 0; i < n && ok; i++, tick++) {
            sa1_state_ex x;
            if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
                printf("  tick: %s\n", sa1_last_error(ctx));
                ok = 0;
                break;
            }
            memset(&x, 0, sizeof x);
            x.struct_size = sizeof x;
            sa1_character_state_ex(ctx, id, &x);
            if (tick == 0)
                continue;
            for (L = 0; L < c->nlayers; L++) {
                const sa1_render *r = NULL;
                double m0 = 0, m1 = 0, ends, mm[3] = { 0, 0, 0 };
                uint32_t n0, n1, nm[3], k, nmin;
                int bad = 0;
                if (sa1_character_render(ctx, id, 0.0f, &r) != SA1_OK)
                    break;
                n0 = sep_of(ctx, c->kind, r, c->layers[L].base, c->layers[L].upper, &m0);
                if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK)
                    break;
                n1 = sep_of(ctx, c->kind, r, c->layers[L].base, c->layers[L].upper, &m1);
                if (n0 == 0 || n1 == 0)
                    continue;
                pairs[L]++;
                ends = m0 < m1 ? m0 : m1;
                nmin = n0 < n1 ? n0 : n1;
                for (k = 0; k < 3; k++) {
                    if (sa1_character_render(ctx, id, mids[k], &r) != SA1_OK)
                        break;
                    nm[k] = sep_of(ctx, c->kind, r, c->layers[L].base, c->layers[L].upper, &mm[k]);
                    if (nm[k] == 0) {
                        bad = 1;
                        lost[L]++;
                    } else if (2u * nm[k] < nmin || mm[k] < ends - 0.01) {
                        bad = 1;
                    }
                    if (nm[k] && mm[k] < worst[L])
                        worst[L] = mm[k];
                }
                if (bad) {
                    double sink = 1e30;
                    failing[L]++;
                    for (k = 0; k < 3; k++)
                        if (nm[k] && mm[k] < sink)
                            sink = mm[k];
                    if (c->allowed_action >= 0 && x.action != c->allowed_action) {
                        other_action[L]++;
                        if (sink < other_sink[L])
                            other_sink[L] = sink;
                    }
                }
                if (g_verbose && (bad || tick <= 2))
                    printf("    t%3u action %3d %s/%s: ends %+.4f (%u, %u overlaps) | .25 %+.4f (%u) .5 %+.4f (%u) .75 "
                           "%+.4f (%u)%s\n", (unsigned)tick, (int)x.action, c->layers[L].upper, c->layers[L].base, ends,
                           (unsigned)n0, (unsigned)n1, nm[0] ? mm[0] : 0.0, (unsigned)nm[0], nm[1] ? mm[1] : 0.0,
                           (unsigned)nm[1], nm[2] ? mm[2] : 0.0, (unsigned)nm[2], bad ? "  FAILS" : "");
            }
        }
    }
    if (!ok) {
        CHECK(0, "%s: context / character / ticks", sa1_character_name(c->kind));
    } else {
        for (L = 0; L < c->nlayers; L++) {
            /* Knuckles: the failing pairs in action 22 are the game's own pose (the eyes inside the face at the pair
             * frames); a failing pair in another action may only dip by less than 0.005 HU below the face (the
             * prototype, render_probe.c method 3, fails there in the same 3 pairs by the same amounts: t51 action 11
             * -0.0016, t75 action 10 -0.0022, t145 action 10 +0.0179, the eye's own motion against the face) */
            CHECK(pairs[L] >= 100u && failing[L] <= c->allowed && (other_action[L] == 0 || other_sink[L] > -0.005),
                  "%s: %s over %s stays in front between the pairs in %u of %u pairs (allowed to fail: %u; overlap lost at "
                  "a mid alpha %u times; smallest separation between the pairs %+.4f HU)%s",
                  sa1_character_name(c->kind), c->layers[L].upper, c->layers[L].base, (unsigned)(pairs[L] - failing[L]),
                  (unsigned)pairs[L], (unsigned)c->allowed, (unsigned)lost[L], worst[L] < 1e29 ? worst[L] : 0.0,
                  c->allowed_action < 0 ? ""
                  : other_action[L] ? "; failing pairs outside action 22 stay within 0.005 HU of the face"
                                    : "; every failing pair is in action 22");
            if (c->allowed_action >= 0 && other_action[L])
                printf("        (%u failing pairs outside action %d, deepest %+.4f HU)\n", (unsigned)other_action[L],
                       (int)c->allowed_action, other_sink[L]);
        }
    }
    if (ctx)
        sa1_shutdown(ctx);
}

int main(int argc, char **argv)
{
    const char *gdi = getenv("SA1_TEST_GDI");
    size_t k;
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--verbose"))
            g_verbose = 1;
        else
            gdi = argv[i];
    }
    if (!gdi || !*gdi) {
        printf("usage: decal_test [--verbose] GDI (or SA1_TEST_GDI)\n");
        return 2;
    }
    printf("decal_test: eye layers stay in front of their faces at alpha .25 / .5 / .75 (gsa1_060_render.md 3.6), script "
           SCRIPT ", Emerald Coast act 1 (DISC):\n");
    for (k = 0; k < sizeof k_chars / sizeof k_chars[0]; k++)
        run_char(gdi, &k_chars[k]);
    printf("decal_test: %s (%d failed)\n", g_fail ? "FAIL" : "ok", g_fail);
    return g_fail ? 1 : 0;
}
