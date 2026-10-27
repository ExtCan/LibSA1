/*
 * ball_test.c - the render interpolation of round 0.9.1 (sadc docs/research/gsa1_070_tails.md F-T1 as corrected by its
 * Review R.4.1 / R.4.2, gsa1_070_bird.md Review R.4 F5; src/sa1_character.c, the comment above ANCHOR_MAX).
 *
 *   ball_test [--part ball,limbs,bird] [--verbose] GDI          (build / run: tests/build_ball.py --run)
 *
 * Every part ticks one pair per tick (dt 1/30) and renders each pair at alpha k/8, k = 0..8 (Review R.3.6: three mid
 * alphas under-read). Distances in HU are host units of the default coordinates (Source: 4 HU per SA1 unit, z up).
 *
 * ball (T-ball). Tails with the upgrade event flags G-SA1 sets (Jet Anklet 0xCD, Rhythm Badge 0xDB of B_8C799920), on a
 *   flat floor (two triangles, 32,768 HU square) with the scripts roll 10n60f3b30f3b30f3b30f and jump
 *   10n60f1J40f1J40f1J40f, and on Emerald Coast act 1 (DISC, its start table) with 20n40f2b30f1J30f. On a ball pair (the
 *   spin ball mtx_spin00 drawn) the game draws the curled body's translucent copies (AFTERIMAGE) inside the opaque ball;
 *   how far the copies' tail corners (mtx_tail) lie outside the ball (from its corner centroid, beyond its smallest
 *   corner distance) at the mid alphas may exceed the larger of the two ends by at most 0.5 HU (the game's own landing
 *   squash puts slivers outside at the pair frames already). On a body pair of modes 4 / 6 (the opaque curled body,
 *   matched with the copy of the ball pair before) the opaque corners' centroid, relative to sa1_render.position, stays
 *   within 1.5 HU of the straight line between its two ends (Review R.2.3, R.4.2). In modes 4 / 6 the opaque corners'
 *   smallest principal extent at every mid alpha is at least 0.95 of the smaller end (T2: the body does not flatten).
 *   Measured before the screw (libsa1 of 2026-10-21 15:58): roll 38 of 49 ball pairs fail (worst 7.62 HU), jump 4 of 42,
 *   Emerald Coast 4 of 15; the body pairs stray up to 9.56 HU.
 * limbs (T-limbs, Review R.4.2). Sonic, Tails, Knuckles, Amy, E-102 and Big on the flat floor, script
 *   10n150f40c40C20n10a30n10J40f (stand, run, curves right and left, stand, action, stand, jump). Per pair (from the
 *   fourth): the opaque corners' largest distance from their centroid at the mid alphas is at most 1.12 x the larger end,
 *   and the lowest opaque corner at least the lower end - 4.0 HU (feet do not swing through the floor). The doc's
 *   prototype (the screw for every model without an anchor) failed both (Knuckles 1.364 and 10.05 HU).
 * bird (F5). Amy in SA1 units (SA1_COORDS_PRESET_NATIVE) on a flat floor with her bird (Birdie: the game's task
 *   CreateElementalTask(2, 3, 8C0DECDA), made right after the character as the harness's --create-task makes it), 200
 *   pairs of run, reverse, stick right, stick left, a zigzag and standing (the bird review's measurement G): every bird
 *   corner (archive 0 textures 44-52) at alpha 0.5 lies within 0.25 units of the straight lerp of alpha 0 and 1. Before
 *   F5: 5 of 199 pairs fail, up to 5.25 units at a 161-degree turn (the bird was interpolated in Amy's turning frame).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_world.h"
#include "sa1_capture.h"

static int g_fail, g_verbose;
static const char *g_parts;

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

#define NA 9                        /* alphas k/8 */

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN)
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : "warn", msg);
}

/* a context with one character kind; flat: a flat floor (host z = 0, the two triangles of the reviews' probes), else
 * Emerald Coast act 1's own collision (DISC); native: SA1 coordinates and the bird review's floor of 100-unit squares */
static sa1_context *mk_ctx(const char *gdi, int32_t kind, int flat, int native)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_status st;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK)
        return NULL;
    if (native) {
        sa1_coords co;
        memset(&co, 0, sizeof co);
        co.struct_size = sizeof co;
        sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
        sa1_set_coords(ctx, &co);
    }
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = 1u << kind;
    lo.flags = SA1_LOAD_DEFAULT;
    st = sa1_load_disc(ctx, gdi, &lo);
    if (!SA1_FAILED(st)) {
        if (native) {
            static sa1_collision_tri tr[288];
            uint32_t n = 0;
            int i, j;
            for (i = -6; i < 6; i++) {
                for (j = -6; j < 6; j++) {
                    float x0 = 100.0f * (float)i, x1 = x0 + 100.0f, z0 = 100.0f * (float)j, z1 = z0 + 100.0f;
                    float p[4][3] = { { x0, 0.0f, z0 }, { x0, 0.0f, z1 }, { x1, 0.0f, z1 }, { x1, 0.0f, z0 } };
                    int q;
                    for (q = 0; q < 2; q++) {
                        memset(&tr[n], 0, sizeof tr[n]);
                        memcpy(tr[n].v[0], p[0], 12);
                        memcpy(tr[n].v[1], p[1 + q], 12);
                        memcpy(tr[n].v[2], p[2 + q], 12);
                        tr[n].surface = SA1_SURFACE_SOLID;
                        n++;
                    }
                }
            }
            st = sa1_collision_set_static(ctx, tr, n);
        } else if (flat) {
            static const float S = 16384.0f;
            sa1_collision_tri tr[2];
            memset(tr, 0, sizeof tr);
            tr[0].v[0][0] = -S;
            tr[0].v[0][1] = -S;
            tr[0].v[1][0] = S;
            tr[0].v[1][1] = -S;
            tr[0].v[2][0] = S;
            tr[0].v[2][1] = S;
            tr[1].v[0][0] = -S;
            tr[1].v[0][1] = -S;
            tr[1].v[1][0] = S;
            tr[1].v[1][1] = S;
            tr[1].v[2][0] = -S;
            tr[1].v[2][1] = S;
            tr[0].surface = tr[1].surface = SA1_SURFACE_SOLID;
            st = sa1_collision_set_static(ctx, tr, 2);
        } else {
            sa1_collision_static col;
            memset(&col, 0, sizeof col);
            col.struct_size = sizeof col;
            col.mode = SA1_COLLISION_MODE_DISC;
            col.disc_level = 1;
            col.disc_act = 0;
            st = sa1_collision_set_static_ex(ctx, &col);
        }
    }
    if (SA1_FAILED(st)) {
        printf("  context: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static sa1_character_id mk_char(sa1_context *ctx, int32_t kind, int flat)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_START_TABLE;
    if (flat) {
        d.flags = SA1_CREATE_WORLD;
        d.position[2] = 8.0f;
    }
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("  create %s: %s\n", sa1_character_name(kind), sa1_last_error(ctx));
        return 0;
    }
    if (kind == SA1_CHAR_TAILS) {
        /* the upgrades G-SA1 sets (event flags 0xCD Jet Anklet, 0xDB Rhythm Badge: bytes of B_8C799920) */
        static const uint16_t f[] = { 0xCD, 0xDB };
        sa1g_world *w = sa1_character_world(ctx, id);
        uint32_t i;
        for (i = 0; i < 2 && w; i++) {
            uint32_t a = 0x8C799920u + f[i], word = a & ~3u, v = 0, sh = 8u * (a & 3u);
            if (sa1g_world_rd32(w, word, &v) == 0)
                sa1g_world_wr32(w, word, (v & ~(0xFFu << sh)) | (1u << sh));
        }
    }
    return id;
}

/* "<n><cmd>" groups: n nothing, f forward, b action + forward, a action, j jump, J jump + forward, c / C forward +
 * right / left */
static char *expand(const char *script, uint32_t *count)
{
    const char *p = script;
    char *out = NULL;
    uint32_t n = 0;
    while (*p) {
        char *e;
        long k = strtol(p, &e, 10), i;
        char *grown;
        if (e == p || !*e) {
            free(out);
            return NULL;
        }
        grown = (char *)realloc(out, n + (size_t)k + 1u);
        if (grown == NULL) {
            free(out);
            return NULL;
        }
        out = grown;
        for (i = 0; i < k; i++)
            out[n++] = *e;
        p = e + 1;
    }
    *count = n;
    return out;
}

static void input_of(char cmd, sa1_input *in)
{
    memset(in, 0, sizeof *in);
    in->struct_size = sizeof *in;
    in->stick_y = (cmd == 'f' || cmd == 'b' || cmd == 'J' || cmd == 'c' || cmd == 'C') ? 1.0f : 0.0f;
    in->stick_x = cmd == 'c' ? 1.0f : cmd == 'C' ? -1.0f : 0.0f;
    in->buttons = (cmd == 'a' || cmd == 'b') ? SA1_BUTTON_ACTION : (cmd == 'j' || cmd == 'J') ? SA1_BUTTON_JUMP : 0u;
}

/* the (archive, index) of a texture of the character by name; arc -1 if none */
static void tex_find(const sa1_context *ctx, int32_t kind, const char *name, int32_t *arc, int32_t *idx)
{
    uint32_t a, n, i;
    *arc = -1;
    *idx = -1;
    for (a = 0; a < SA1_MAX_ARCHIVES; a++) {
        n = 0;
        if (sa1_texture_archive(ctx, kind, a, NULL, &n) != SA1_OK)
            continue;
        for (i = 0; i < n; i++) {
            sa1_texture_info ti;
            memset(&ti, 0, sizeof ti);
            ti.struct_size = sizeof ti;
            if (sa1_texture_get(ctx, kind, a, i, &ti) == SA1_OK && ti.name && !strcmp(ti.name, name)) {
                *arc = (int32_t)a;
                *idx = (int32_t)i;
                return;
            }
        }
    }
}

/* the smallest principal extent (square root of the smallest eigenvalue of the covariance, cyclic Jacobi) */
static double min_extent(const double c[6])
{
    double a[3][3], e = 1e300;
    int it, p, q, k;
    a[0][0] = c[0];
    a[1][1] = c[1];
    a[2][2] = c[2];
    a[0][1] = a[1][0] = c[3];
    a[0][2] = a[2][0] = c[4];
    a[1][2] = a[2][1] = c[5];
    for (it = 0; it < 50; it++) {
        double off = fabs(a[0][1]) + fabs(a[0][2]) + fabs(a[1][2]);
        if (off < 1e-14)
            break;
        for (p = 0; p < 2; p++) {
            for (q = p + 1; q < 3; q++) {
                double th, t, cs, sn;
                if (fabs(a[p][q]) < 1e-300)
                    continue;
                th = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                t = (th >= 0.0 ? 1.0 : -1.0) / (fabs(th) + sqrt(th * th + 1.0));
                cs = 1.0 / sqrt(t * t + 1.0);
                sn = t * cs;
                for (k = 0; k < 3; k++) {
                    double akp = a[k][p], akq = a[k][q];
                    a[k][p] = cs * akp - sn * akq;
                    a[k][q] = sn * akp + cs * akq;
                }
                for (k = 0; k < 3; k++) {
                    double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = cs * apk - sn * aqk;
                    a[q][k] = sn * apk + cs * aqk;
                }
            }
        }
    }
    for (k = 0; k < 3; k++)
        e = fmin(e, a[k][k]);
    return sqrt(fmax(e, 0.0));
}

typedef struct meas {
    double cop[3], root[3];
    uint32_t nop;
    double rop;                     /* the opaque corners' largest distance from their centroid */
    double zmin;                    /* the lowest opaque corner (host z: up) */
    double emin;                    /* the opaque corners' smallest principal extent */
    int ball;                       /* the spin ball is drawn */
    double bc[3], rin, tail_out;    /* its centroid and smallest corner distance; how far the copies' tails lie outside */
} meas;

#define OPAQUE(f) (!((f) & (SA1_DRAW_BLEND | SA1_DRAW_EFFECT | SA1_DRAW_SHADOW | SA1_DRAW_AFTERIMAGE)))

static void measure(const sa1_render *r, int32_t ball_arc, int32_t ball_tex, int32_t tail_arc, int32_t tail_tex, meas *m)
{
    uint32_t d, t, k, nb = 0;
    double cv[6] = { 0, 0, 0, 0, 0, 0 };
    memset(m, 0, sizeof *m);
    for (k = 0; k < 3; k++)
        m->root[k] = r->position[k];
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        int op = OPAQUE(dr->flags), ball = ball_arc >= 0 && dr->archive == ball_arc && dr->texture == ball_tex;
        if (dr->prim != SA1_PRIM_TRIANGLES)
            continue;
        for (t = dr->first; t < dr->first + dr->count && t < r->num_triangles; t++) {
            for (k = 0; k < 9; k++) {
                double p = r->positions[9u * t + k];
                if (op)
                    m->cop[k % 3] += p;
                if (ball)
                    m->bc[k % 3] += p;
            }
        }
        if (op)
            m->nop += 3u * dr->count;
        if (ball)
            nb += 3u * dr->count;
    }
    for (k = 0; k < 3; k++) {
        if (m->nop)
            m->cop[k] /= m->nop;
        if (nb)
            m->bc[k] /= nb;
    }
    m->ball = nb > 0;
    m->rin = 1e30;
    m->zmin = 1e30;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        int op = OPAQUE(dr->flags), ball = m->ball && dr->archive == ball_arc && dr->texture == ball_tex;
        int tail = m->ball && (dr->flags & SA1_DRAW_AFTERIMAGE) && dr->archive == tail_arc && dr->texture == tail_tex;
        if (dr->prim != SA1_PRIM_TRIANGLES)
            continue;
        for (t = dr->first; t < dr->first + dr->count && t < r->num_triangles; t++) {
            for (k = 0; k < 3; k++) {
                const float *p = r->positions + 9u * t + 3u * k;
                double dx, dy, dz, l;
                if (op) {
                    dx = p[0] - m->cop[0];
                    dy = p[1] - m->cop[1];
                    dz = p[2] - m->cop[2];
                    l = sqrt(dx * dx + dy * dy + dz * dz);
                    m->rop = fmax(m->rop, l);
                    m->zmin = fmin(m->zmin, p[2]);
                    cv[0] += dx * dx;
                    cv[1] += dy * dy;
                    cv[2] += dz * dz;
                    cv[3] += dx * dy;
                    cv[4] += dx * dz;
                    cv[5] += dy * dz;
                }
                if (ball) {
                    dx = p[0] - m->bc[0];
                    dy = p[1] - m->bc[1];
                    dz = p[2] - m->bc[2];
                    m->rin = fmin(m->rin, sqrt(dx * dx + dy * dy + dz * dz));
                }
                (void)tail;
            }
        }
    }
    if (m->nop) {
        for (k = 0; k < 6; k++)
            cv[k] /= m->nop;
        m->emin = min_extent(cv);
    }
    if (!m->ball)
        return;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->prim != SA1_PRIM_TRIANGLES || !(dr->flags & SA1_DRAW_AFTERIMAGE) || dr->archive != tail_arc ||
            dr->texture != tail_tex)
            continue;
        for (t = dr->first; t < dr->first + dr->count && t < r->num_triangles; t++) {
            for (k = 0; k < 3; k++) {
                const float *p = r->positions + 9u * t + 3u * k;
                double dx = p[0] - m->bc[0], dy = p[1] - m->bc[1], dz = p[2] - m->bc[2];
                m->tail_out = fmax(m->tail_out, sqrt(dx * dx + dy * dy + dz * dz) - m->rin);
            }
        }
    }
}

/* how far the opaque centroid relative to the root strays from the line between its ends (largest over the mid alphas) */
static double stray(const meas *m)
{
    double worst = 0.0;
    int a, k;
    for (a = 1; a < NA - 1; a++) {
        double al = (double)a / (NA - 1), s = 0.0;
        for (k = 0; k < 3; k++) {
            double e0 = m[0].cop[k] - m[0].root[k], e1 = m[NA - 1].cop[k] - m[NA - 1].root[k];
            double ea = m[a].cop[k] - m[a].root[k], dd = ea - (e0 + (e1 - e0) * al);
            s += dd * dd;
        }
        worst = fmax(worst, sqrt(s));
    }
    return worst;
}

/* ---- the turn of a rigid point set (Horn's quaternion method): the rotation taking a (n points) onto b ---- */
static void jacobi4(double a[4][4], double v[4][4])
{
    int it, p, q, k;
    for (p = 0; p < 4; p++)
        for (q = 0; q < 4; q++)
            v[p][q] = p == q ? 1.0 : 0.0;
    for (it = 0; it < 100; it++) {
        double off = 0.0;
        for (p = 0; p < 4; p++)
            for (q = p + 1; q < 4; q++)
                off += fabs(a[p][q]);
        if (off < 1e-15)
            break;
        for (p = 0; p < 3; p++) {
            for (q = p + 1; q < 4; q++) {
                double th, t, cs, sn;
                if (fabs(a[p][q]) < 1e-300)
                    continue;
                th = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                t = (th >= 0.0 ? 1.0 : -1.0) / (fabs(th) + sqrt(th * th + 1.0));
                cs = 1.0 / sqrt(t * t + 1.0);
                sn = t * cs;
                for (k = 0; k < 4; k++) {
                    double akp = a[k][p], akq = a[k][q];
                    a[k][p] = cs * akp - sn * akq;
                    a[k][q] = sn * akp + cs * akq;
                }
                for (k = 0; k < 4; k++) {
                    double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = cs * apk - sn * aqk;
                    a[q][k] = sn * apk + cs * aqk;
                }
                for (k = 0; k < 4; k++) {
                    double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = cs * vkp - sn * vkq;
                    v[k][q] = sn * vkp + cs * vkq;
                }
            }
        }
    }
}

/* the angle (degrees, 0..180) and unit axis of the best-fit rotation from the points a to the points b (n each) */
static double turn_of(const double *a, const double *b, uint32_t n, double axis[3])
{
    double ca[3] = { 0, 0, 0 }, cb[3] = { 0, 0, 0 }, S[3][3], N[4][4], V[4][4], q[4], l;
    uint32_t i;
    int r, s, best = 0;
    memset(S, 0, sizeof S);
    for (i = 0; i < n; i++)
        for (r = 0; r < 3; r++) {
            ca[r] += a[3u * i + r] / n;
            cb[r] += b[3u * i + r] / n;
        }
    for (i = 0; i < n; i++)
        for (r = 0; r < 3; r++)
            for (s = 0; s < 3; s++)
                S[r][s] += (a[3u * i + r] - ca[r]) * (b[3u * i + s] - cb[s]);
    N[0][0] = S[0][0] + S[1][1] + S[2][2];
    N[1][1] = S[0][0] - S[1][1] - S[2][2];
    N[2][2] = -S[0][0] + S[1][1] - S[2][2];
    N[3][3] = -S[0][0] - S[1][1] + S[2][2];
    N[0][1] = N[1][0] = S[1][2] - S[2][1];
    N[0][2] = N[2][0] = S[2][0] - S[0][2];
    N[0][3] = N[3][0] = S[0][1] - S[1][0];
    N[1][2] = N[2][1] = S[0][1] + S[1][0];
    N[1][3] = N[3][1] = S[2][0] + S[0][2];
    N[2][3] = N[3][2] = S[1][2] + S[2][1];
    jacobi4(N, V);
    for (r = 1; r < 4; r++)
        if (N[r][r] > N[best][best])
            best = r;
    for (r = 0; r < 4; r++)
        q[r] = V[r][best];
    if (q[0] < 0.0)
        for (r = 0; r < 4; r++)
            q[r] = -q[r];
    l = sqrt(q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (r = 0; r < 3; r++)
        axis[r] = l > 1e-12 ? q[r + 1] / l : (r == 0);
    return 2.0 * atan2(l, q[0]) * 57.29577951308232;
}

/* the opaque corners of r relative to sa1_render.position into *buf (grown as needed); their count */
static uint32_t opaque_corners(const sa1_render *r, double **buf, uint32_t *cap)
{
    uint32_t d, t, k, n = 0;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->prim != SA1_PRIM_TRIANGLES || !OPAQUE(dr->flags))
            continue;
        for (t = dr->first; t < dr->first + dr->count && t < r->num_triangles; t++) {
            for (k = 0; k < 3; k++) {
                int c;
                if (n >= *cap) {
                    uint32_t nc = *cap ? 2u * *cap : 4096u;
                    double *nb = (double *)realloc(*buf, 3u * sizeof(double) * nc);
                    if (nb == NULL)
                        return n;
                    *buf = nb;
                    *cap = nc;
                }
                for (c = 0; c < 3; c++)
                    (*buf)[3u * n + c] = (double)r->positions[9u * t + 3u * k + c] - r->position[c];
                n++;
            }
        }
    }
    return n;
}

typedef struct run_result {
    uint32_t pairs, ball_pairs, ball_fail, body_pairs, stray_fail, ext_pairs, ext_fail, rad_fail, dip_fail;
    uint32_t screw, world, root_frame, anchored, tb_pairs, tb_fail;
    double ball_worst, stray_worst, ext_worst, rad_worst, dip_worst, tb_worst;
    uint32_t ball_worst_p, stray_worst_p, rad_worst_p, dip_worst_p, tb_worst_p;
} run_result;

/* one character through a script, every pair measured at the nine alphas */
static int run_script(const char *gdi, int32_t kind, int flat, const char *script, run_result *R)
{
    sa1_context *ctx = mk_ctx(gdi, kind, flat, 0);
    sa1_character_id id = ctx ? mk_char(ctx, kind, flat) : 0;
    int32_t ball_arc = -1, ball_tex = -1, tail_arc = -1, tail_tex = -1;
    uint32_t n = 0, i, a, cap[3] = { 0, 0, 0 }, nc[3] = { 0, 0, 0 };
    double *cb[3] = { NULL, NULL, NULL };
    int prev_ball = 0;
    char *cmds = expand(script, &n);
    memset(R, 0, sizeof *R);
    R->rad_worst = 0.0;
    if (!id || cmds == NULL) {
        free(cmds);
        sa1_shutdown(ctx);
        return -1;
    }
    if (kind == SA1_CHAR_TAILS) {
        tex_find(ctx, kind, "mtx_spin00", &ball_arc, &ball_tex);
        tex_find(ctx, kind, "mtx_tail", &tail_arc, &tail_tex);
    }
    for (i = 0; i < n; i++) {
        sa1_input in;
        sa1_state_ex x;
        sa1_lerp_stats ls;
        meas m[NA];
        int ok = 1;
        input_of(cmds[i], &in);
        if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
            printf("  tick: %s\n", sa1_last_error(ctx));
            free(cmds);
            for (a = 0; a < 3; a++)
                free(cb[a]);
            sa1_shutdown(ctx);
            return -1;
        }
        for (a = 0; a < NA && ok; a++) {
            const sa1_render *r = NULL;
            ok = sa1_character_render(ctx, id, (float)a / (NA - 1), &r) == SA1_OK && r != NULL;
            if (ok)
                measure(r, ball_arc, ball_tex, tail_arc, tail_tex, &m[a]);
            if (ok && (a % 4u) == 0u)
                nc[a / 4u] = opaque_corners(r, &cb[a / 4u], &cap[a / 4u]);
        }
        if (!ok)
            continue;
        memset(&x, 0, sizeof x);
        x.struct_size = sizeof x;
        sa1_character_state_ex(ctx, id, &x);
        memset(&ls, 0, sizeof ls);
        ls.struct_size = sizeof ls;
        sa1_character_lerp_stats(ctx, id, &ls);
        R->pairs++;
#ifdef SA1_LERP_STATS_WORLD_SCREW
        R->screw += ls.screw;
        R->world += ls.world;
#endif                              /* (a library before round 0.9.1 has neither: 0) */
        R->root_frame += ls.root_frame;
        R->anchored += ls.anchored;
        if (i < 3 || m[0].nop == 0) {
            prev_ball = m[0].ball;
            continue;
        }
        if (prev_ball && !m[0].ball && (x.mode == 4 || x.mode == 6) && nc[0] > 0 && nc[0] == nc[1] && nc[0] == nc[2]) {
            /* a body pair after a ball pair (Review R.3.3): the opaque curled body turns the shorter way, at alpha 0.5
               by half its turn between the pairs (not the long way round, backwards) */
            double ax1[3], axh[3], e = turn_of(cb[0], cb[2], nc[0], ax1), h = turn_of(cb[0], cb[1], nc[0], axh), err;
            if (ax1[0] * axh[0] + ax1[1] * axh[1] + ax1[2] * axh[2] < 0.0)
                h = -h;
            err = fabs(h - 0.5 * e);
            R->tb_pairs++;
            R->tb_fail += err > 10.0;
            if (err > R->tb_worst) {
                R->tb_worst = err;
                R->tb_worst_p = i;
            }
            if (g_verbose && err > 10.0)
                printf("    p%u: body pair after a ball pair turns %.1f degrees, %.1f at alpha 0.5\n", (unsigned)i, e, h);
        }
        prev_ball = m[0].ball;
        if (m[0].ball && m[NA / 2].ball && m[NA - 1].ball) {
            /* a ball pair: the copies' tails stay inside the ball between the pairs */
            double ends = fmax(m[0].tail_out, m[NA - 1].tail_out), mid = 0.0, ex;
            for (a = 1; a < NA - 1; a++)
                mid = fmax(mid, m[a].tail_out);
            ex = mid - ends;
            R->ball_pairs++;
            R->ball_fail += ex > 0.5;
            if (ex > R->ball_worst) {
                R->ball_worst = ex;
                R->ball_worst_p = i;
            }
            if (g_verbose && ex > 0.5)
                printf("    p%u mode %d action %d: ball pair, copy tails %.2f HU further out between the pairs\n",
                       (unsigned)i, (int)x.mode, (int)x.action, ex);
        } else if (x.mode == 4 || x.mode == 6) {
            /* a body pair in the ball modes: the opaque curled body stays on its line */
            double s = stray(m);
            R->body_pairs++;
            R->stray_fail += s > 1.5;
            if (s > R->stray_worst) {
                R->stray_worst = s;
                R->stray_worst_p = i;
            }
            if (g_verbose && s > 1.5)
                printf("    p%u mode %d action %d: body pair, opaque centroid %.2f HU off its line\n", (unsigned)i,
                       (int)x.mode, (int)x.action, s);
        }
        if (x.mode == 4 || x.mode == 6) {
            double lo = fmin(m[0].emin, m[NA - 1].emin), mn = 1e300, rr;
            for (a = 1; a < NA - 1; a++)
                mn = fmin(mn, m[a].emin);
            rr = lo > 1e-9 ? mn / lo : 1.0;
            R->ext_pairs++;
            R->ext_fail += rr < 0.95;
            if (R->ext_pairs == 1 || rr < R->ext_worst)
                R->ext_worst = rr;
        }
        {
            /* whole body: the opaque corners' spread and the lowest corner between the pairs */
            double e = fmax(m[0].rop, m[NA - 1].rop), mx = 0.0, rad, lo = fmin(m[0].zmin, m[NA - 1].zmin), mn = 1e30, dip;
            for (a = 1; a < NA - 1; a++) {
                mx = fmax(mx, m[a].rop);
                mn = fmin(mn, m[a].zmin);
            }
            rad = e > 0.0 ? mx / e : 1.0;
            dip = lo - mn;
            R->rad_fail += rad > 1.12;
            R->dip_fail += dip > 4.0;
            if (rad > R->rad_worst) {
                R->rad_worst = rad;
                R->rad_worst_p = i;
            }
            if (dip > R->dip_worst) {
                R->dip_worst = dip;
                R->dip_worst_p = i;
            }
            if (g_verbose && (rad > 1.12 || dip > 4.0))
                printf("    p%u mode %d action %d: spread %.3f x the larger end, lowest corner %.2f HU below the lower end\n",
                       (unsigned)i, (int)x.mode, (int)x.action, rad, dip);
        }
    }
    free(cmds);
    for (a = 0; a < 3; a++)
        free(cb[a]);
    sa1_shutdown(ctx);
    return 0;
}

static void part_ball(const char *gdi)
{
    static const struct {
        const char *name, *script;
        int flat;
    } runs[] = {
        { "roll (flat floor)", "10n60f3b30f3b30f3b30f", 1 },
        { "jump (flat floor)", "10n60f1J40f1J40f1J40f", 1 },
        { "Emerald Coast act 1 roll + jump", "20n40f2b30f1J30f", 0 },
    };
    uint32_t k;
    printf("ball: Tails' spin ball and the curled body inside it (gsa1_070_tails.md F-T1, Review R.4.1 / R.4.2):\n");
    for (k = 0; k < sizeof runs / sizeof runs[0]; k++) {
        run_result R;
        if (run_script(gdi, SA1_CHAR_TAILS, runs[k].flat, runs[k].script, &R) != 0) {
            CHECK(0, "Tails, %s: context / character / ticks", runs[k].name);
            continue;
        }
        CHECK(R.ball_pairs > 0 && R.ball_fail == 0,
              "Tails, %s: %u ball pairs, %u with the copies' tails more than 0.5 HU further outside the ball between the "
              "pairs than at their ends (worst %.2f HU, pair %u)",
              runs[k].name, (unsigned)R.ball_pairs, (unsigned)R.ball_fail, R.ball_worst, (unsigned)R.ball_worst_p);
        CHECK(R.stray_fail == 0,
              "Tails, %s: %u body pairs in the ball modes, %u whose opaque centroid strays more than 1.5 HU from its line "
              "(worst %.2f HU, pair %u)",
              runs[k].name, (unsigned)R.body_pairs, (unsigned)R.stray_fail, R.stray_worst, (unsigned)R.stray_worst_p);
        CHECK(R.ext_pairs > 0 && R.ext_fail == 0,
              "Tails, %s: in %u pairs of the ball modes the opaque body's smallest principal extent between the pairs "
              "stays at least 0.95 of the smaller end (worst ratio %.3f; %u fail)",
              runs[k].name, (unsigned)R.ext_pairs, R.ext_worst, (unsigned)R.ext_fail);
        CHECK(R.tb_fail == 0,
              "Tails, %s: %u body pairs after a ball pair; %u whose opaque body at alpha 0.5 is more than 10 degrees from half "
              "its turn between the pairs, the shorter way (worst %.1f degrees, pair %u)", runs[k].name, (unsigned)R.tb_pairs,
              (unsigned)R.tb_fail, R.tb_worst, (unsigned)R.tb_worst_p);
        CHECK(R.screw > 0, "Tails, %s: rigid groups moved along the screw in the plans: %u (root frame %u, anchored %u, "
              "world %u)", runs[k].name, (unsigned)R.screw, (unsigned)R.root_frame, (unsigned)R.anchored,
              (unsigned)R.world);
    }
}

static void part_limbs(const char *gdi)
{
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA,
                                     SA1_CHAR_BIG };
    static const char *const script = "10n150f40c40C20n10a30n10J40f";
    uint32_t k;
    printf("limbs: whole bodies between the pairs (gsa1_070_tails.md Review R.4.2 T-limbs; flat floor, %s):\n", script);
    for (k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        run_result R;
        if (run_script(gdi, kinds[k], 1, script, &R) != 0) {
            CHECK(0, "%s: context / character / ticks", sa1_character_name(kinds[k]));
            continue;
        }
        CHECK(R.pairs > 300 && R.rad_fail == 0 && R.dip_fail == 0,
              "%s: %u pairs; the opaque corners' spread between the pairs at most 1.12 x the larger end (worst %.3f, pair "
              "%u) and the lowest corner at most 4 HU below the lower end (worst %.2f HU, pair %u); screw roots %u, world "
              "space %u",
              sa1_character_name(kinds[k]), (unsigned)R.pairs, R.rad_worst, (unsigned)R.rad_worst_p, R.dip_worst,
              (unsigned)R.dip_worst_p, (unsigned)R.screw, (unsigned)R.world);
    }
}

/* ---- bird: Amy's companion interpolated in world space (F5) ---- */
#define BIRD_INIT 0x8C0DECDAu       /* the bird task's first exec (AmyMain's spawner 8C0DEDC0 creates it) */
static void cb_create_bird(sa1g_world *w, void *user)
{
    void *tp = ((void *(*)(uint16_t, int32_t, sa1_gfn))sa1_fn(SA1_GFN_C(SA1G_FN_CREATE_TASK), 0u))(2u, 3,
                                                                                                    SA1_GFN_C(BIRD_INIT));
    (void)w;
    *(int *)user = tp != NULL;
}

static uint32_t bird_verts(sa1_context *ctx, sa1_character_id id, float alpha, float *out, uint32_t cap)
{
    const sa1_render *r = NULL;
    uint32_t d, t, k, n = 0;
    if (sa1_character_render(ctx, id, alpha, &r) != SA1_OK || !r)
        return 0;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->prim != SA1_PRIM_TRIANGLES || dr->archive != 0 || dr->texture < 44 || dr->texture > 52 ||
            (dr->flags & SA1_DRAW_SHADOW))
            continue;
        for (t = dr->first; t < dr->first + dr->count && t < r->num_triangles; t++)
            for (k = 0; k < 9 && n < cap; k++)
                out[n++] = r->positions[9u * t + k];
    }
    return n;
}

static void part_bird(const char *gdi)
{
    static float v0[8192], vh[8192], v1[8192];
    sa1_context *ctx = mk_ctx(gdi, SA1_CHAR_AMY, 1, 1);
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1g_world *w;
    uint32_t i, k, bad = 0, total = 0, worst_p = 0, world_pairs = 0;
    double worst = 0.0;
    int made = 0;
    printf("bird: Amy's bird between the pairs of sharp turns (gsa1_070_bird.md Review R.4 F5; SA1 units):\n");
    if (ctx == NULL) {
        CHECK(0, "bird: context");
        return;
    }
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_AMY;
    d.flags = SA1_CREATE_WORLD;
    d.position[0] = 13.7f;
    d.position[1] = 2.0f;
    d.position[2] = 7.3f;
    d.yaw = 0.6f;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id)) || (w = sa1_character_world(ctx, id)) == NULL ||
        sa1g_world_call(w, cb_create_bird, &made) != SA1_OK || !made) {
        CHECK(0, "bird: Amy and her bird (%s)", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return;
    }
    for (i = 0; i < 200; i++) {
        sa1_input in;
        sa1_lerp_stats ls;
        uint32_t n0, nh, n1;
        double dev = 0.0;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        if (i < 40) {
            in.stick_y = 1.0f;          /* run */
        } else if (i < 60) {
            in.stick_y = -1.0f;         /* reverse */
        } else if (i < 120) {
            in.stick_x = 1.0f;          /* stick right */
        } else if (i < 140) {
            in.stick_x = -1.0f;         /* stick left */
        } else if (i < 170) {
            in.stick_x = (i & 4) ? 1.0f : -1.0f;    /* zigzag */
            in.stick_y = 0.3f;
        } else {
            in.buttons = i == 175 ? SA1_BUTTON_ACTION : 0u;     /* stand */
        }
        if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
            CHECK(0, "bird: tick %u: %s", (unsigned)i, sa1_last_error(ctx));
            break;
        }
        n0 = bird_verts(ctx, id, 0.0f, v0, 8192);
        nh = bird_verts(ctx, id, 0.5f, vh, 8192);
        n1 = bird_verts(ctx, id, 1.0f, v1, 8192);
        memset(&ls, 0, sizeof ls);
        ls.struct_size = sizeof ls;
        sa1_character_lerp_stats(ctx, id, &ls);
#ifdef SA1_LERP_STATS_WORLD_SCREW
        world_pairs += ls.world > 0;
#endif
        if (n0 == 0 || n0 != nh || n0 != n1)
            continue;
        total++;
        for (k = 0; k < n0; k += 3) {
            double dx = vh[k] - 0.5 * ((double)v0[k] + v1[k]), dy = vh[k + 1] - 0.5 * ((double)v0[k + 1] + v1[k + 1]);
            double dz = vh[k + 2] - 0.5 * ((double)v0[k + 2] + v1[k + 2]);
            dev = fmax(dev, sqrt(dx * dx + dy * dy + dz * dz));
        }
        if (dev > 0.25) {
            bad++;
            if (g_verbose)
                printf("    pair %u: a bird corner %.3f units off the straight lerp at alpha 0.5\n", (unsigned)(i + 1), dev);
        }
        if (dev > worst) {
            worst = dev;
            worst_p = i + 1;
        }
    }
    CHECK(total >= 190 && bad == 0,
          "bird: %u of %u pairs drew the bird; %u with a corner more than 0.25 units off the straight lerp of alpha 0 and 1 "
          "at alpha 0.5 (worst %.3f units, pair %u); %u pairs interpolated a model in world space",
          (unsigned)total, (unsigned)i, (unsigned)bad, worst, (unsigned)worst_p, (unsigned)world_pairs);
    sa1_shutdown(ctx);
}

static int part_on(const char *name)
{
    const char *p = g_parts;
    size_t n = strlen(name);
    if (p == NULL)
        return 1;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == n && strncmp(p, name, n) == 0)
            return 1;
        if (e == NULL)
            break;
        p = e + 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *gdi = NULL;
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--verbose"))
            g_verbose = 1;
        else if (!strcmp(argv[i], "--part") && i + 1 < argc)
            g_parts = argv[++i];
        else
            gdi = argv[i];
    }
    if (gdi == NULL)
        gdi = getenv("SA1_TEST_GDI");
    if (gdi == NULL) {
        printf("usage: ball_test [--part ball,limbs,bird] [--verbose] GDI\n");
        return 2;
    }
    printf("libsa1 %s ball test (render interpolation of round 0.9.1)\n", sa1_version_string());
    if (part_on("ball"))
        part_ball(gdi);
    if (part_on("limbs"))
        part_limbs(gdi);
    if (part_on("bird"))
        part_bird(gdi);
    printf("%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
