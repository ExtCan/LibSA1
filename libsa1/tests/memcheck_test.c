/*
 * memcheck_test.c - libsa1's GRID synthesis under the page-guard allocator (tests/guard_alloc.h): every heap block of the
 * library against a no-access page, so an out-of-bounds access faults where it happens (round 4b2's verifier found
 * ca_grid_undersides storing a normal into cov[ncov] after the last cover had been stored: silent heap corruption on
 * G-SA1's map-job worker on some maps, e.g. gm_goldencity_v2_day, that every other test missed because the write landed
 * in allocator slack).
 *
 *   memcheck_test [--only selftest|scenes|maps]... [--maps DIR] [--threads N] [--quick] [--no-guard] [--verbose] [GDI]
 *       (build / run: tests/build_memcheck.py; GDI: or $SA1_TEST_GDI; DIR: or $SA1_MEMCHECK_MAPS)
 *
 *   selftest  the guard allocator itself, in child processes (each must end with exit code 139 and the right report):
 *             a write one past the end, a read further on, a write into the alignment slack (found by the canary when
 *             the block is freed), an underrun (canary), a use after free, a double free, damage of a block still live
 *             at exit, an overflow on a worker thread, and the pre-fix ca_grid_undersides loop replayed on 96-byte covers;
 *             a clean child (threads allocating and freeing) must end with 0 and intact canaries
 *   scenes    synthetic scenes (native coordinates): a floor; the verifier's floor + 4-unit wall box (the pre-fix
 *             overflow: a wall piece after the last cover); prepare_test's map; crash_probe's soup (planks, 3000 random
 *             triangles, water); walls only (no cover); covers only; stacked thin slabs with a wall last; kill planes and
 *             water facing both ways; degenerate, far, huge and tiny triangles; dense cells (an object split, 9000 kill
 *             triangles in one kill cell, 300 stacked floors in one column); an empty list; non-finite and out-of-range
 *             triangles (every path must refuse them with SA1_ERR_INVALID_ARGUMENT)
 *   maps      every DIR/tris_*.bin (raw sa1_collision_tri arrays of GMod maps in Source units, as the verifier dumped
 *             them; map-derived data is never part of the repository: without DIR the part is skipped)
 *
 * (Round 0.9.3: the scene "front-only / curved undersides" and the parameter sets UNDERSIDES_DROP, with CUT and reach 64.)
 * Every scene runs with every GRID parameter set (defaults by NULL and by value, under_reach 0.5 / 6 / 64, KEEP_UNDERSIDES,
 * CUT, CUT + reach 64, DC_LIMITS, the smallest and the largest cells and edges, everything at once, a struct_size that
 * ends before under_reach) through both preparation paths:
 *   A  sa1_collision_prepare_tris (flat list) and sa1_collision_prepare (the same triangles as one GRID group per surface)
 *      on worker threads (as G-SA1's map job), --threads at a time;
 *   B  on the main thread at the same time: sa1_collision_set_static (defaults) or sa1_collision_set_static_ex (the
 *      groups, with the parameters), then the first sa1_character_create_ex (the synthesis on the creating thread) and 20
 *      ticks of Sonic running (the world's window over the objects), then destroy. Needs the disc (skipped without).
 * A preparation may fail cleanly (SA1_ERR_LIMIT for the smallest cells on a big map); a fault ends the test with the guard
 * report. B's synthesis must equal A's (triangles, objects, undersides). The guard allocator is always on (--no-guard: off,
 * for comparison runs).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_coladapt.h"     /* SA1CA_COLSEG_BYTES */

#include "guard_alloc.c"

#define PI 3.14159265358979323846
#define YAW_X ((float)(-5.9 * PI / 180.0))

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
        fflush(stdout);                                                        \
    } while (0)

#define INFO(...)                                                              \
    do {                                                                       \
        printf("  info  ");                                                    \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
        fflush(stdout);                                                        \
    } while (0)

/* ---- threads ------------------------------------------------------------------------------------------------------ */
typedef void (*thr_fn)(void *);
typedef struct thr {
    thr_fn fn;
    void *arg;
#if defined(_WIN32)
    HANDLE h;
#else
    pthread_t t;
#endif
} thr;

#if defined(_WIN32)
static DWORD WINAPI thr_main(LPVOID p)
{
    thr *t = (thr *)p;
    t->fn(t->arg);
    return 0;
}
#else
static void *thr_main(void *p)
{
    thr *t = (thr *)p;
    t->fn(t->arg);
    return NULL;
}
#endif

static int thr_start(thr *t, thr_fn fn, void *arg)
{
    t->fn = fn;
    t->arg = arg;
#if defined(_WIN32)
    t->h = CreateThread(NULL, 0, thr_main, t, 0, NULL);
    return t->h != NULL ? 0 : -1;
#else
    return pthread_create(&t->t, NULL, thr_main, t);
#endif
}

static void thr_join(thr *t)
{
#if defined(_WIN32)
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
#else
    pthread_join(t->t, NULL);
#endif
}

/* ---- scenes (prepare_test.c's helpers) ------------------------------------------------------------------------------ */
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
        if (b->t == NULL) {
            printf("out of memory\n");
            exit(3);
        }
    }
    t = &b->t[b->n++];
    memcpy(t->v[0], a, 12);
    memcpy(t->v[1], c, 12);
    memcpy(t->v[2], d, 12);
    t->surface = s;
}

static void tb_quad(tbuf *b, const float p[4][3], const float n[3], uint32_t s)
{
    float e1[3], e2[3], c[3];
    int k;
    for (k = 0; k < 3; k++) {
        e1[k] = p[1][k] - p[0][k];
        e2[k] = p[2][k] - p[0][k];
    }
    c[0] = e1[1] * e2[2] - e1[2] * e2[1];
    c[1] = e1[2] * e2[0] - e1[0] * e2[2];
    c[2] = e1[0] * e2[1] - e1[1] * e2[0];
    if (c[0] * n[0] + c[1] * n[1] + c[2] * n[2] >= 0.0f) {
        tb_tri(b, p[0], p[1], p[2], s);
        tb_tri(b, p[0], p[2], p[3], s);
    } else {
        tb_tri(b, p[0], p[2], p[1], s);
        tb_tri(b, p[0], p[3], p[2], s);
    }
}

static void tb_floor(tbuf *b, float x0, float x1, float z0, float z1, float y, float up, uint32_t s)
{
    float p[4][3] = { { x0, y, z0 }, { x1, y, z0 }, { x1, y, z1 }, { x0, y, z1 } };
    float n[3] = { 0.0f, up, 0.0f };
    tb_quad(b, p, n, s);
}

static void tb_wall(tbuf *b, float x0, float x1, float z, float y0, float y1, float facing, uint32_t s)
{
    float p[4][3] = { { x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { x0, y1, z } };
    float n[3] = { 0.0f, 0.0f, facing };
    tb_quad(b, p, n, s);
}

static void tb_box(tbuf *b, float x0, float x1, float y0, float y1, float z0, float z1, uint32_t s)
{
    float P[8][3];
    static const int f[6][4] = { { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 } };
    static const float nn[6][3] = { { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } };
    int i, k;
    for (i = 0; i < 8; i++) {
        P[i][0] = (i & 4) ? x1 : x0;
        P[i][1] = (i & 2) ? y1 : y0;
        P[i][2] = (i & 1) ? z1 : z0;
    }
    for (i = 0; i < 6; i++) {
        float q[4][3];
        for (k = 0; k < 4; k++) {
            memcpy(q[k], P[f[i][k]], 12);
        }
        tb_quad(b, q, nn[i], s);
    }
}

static void tb_plank(tbuf *b, float x0, float y0, float z0, float deg, float len, float wid, float t)
{
    float c = cosf(deg * (float)PI / 180.0f), s = sinf(deg * (float)PI / 180.0f);
    float nx = -s, ny = c, ax = x0, ay = y0, bx = x0 + len * c, by = y0 + len * s;
    float a2x = ax - t * nx, a2y = ay - t * ny, b2x = bx - t * nx, b2y = by - t * ny, z1 = z0 + wid;
    float top[4][3] = { { ax, ay, z0 }, { bx, by, z0 }, { bx, by, z1 }, { ax, ay, z1 } };
    float und[4][3] = { { a2x, a2y, z0 }, { b2x, b2y, z0 }, { b2x, b2y, z1 }, { a2x, a2y, z1 } };
    float foot[4][3] = { { a2x, a2y, z0 }, { ax, ay, z0 }, { ax, ay, z1 }, { a2x, a2y, z1 } };
    float head[4][3] = { { bx, by, z0 }, { b2x, b2y, z0 }, { b2x, b2y, z1 }, { bx, by, z1 } };
    float side0[4][3] = { { ax, ay, z0 }, { bx, by, z0 }, { b2x, b2y, z0 }, { a2x, a2y, z0 } };
    float side1[4][3] = { { ax, ay, z1 }, { bx, by, z1 }, { b2x, b2y, z1 }, { a2x, a2y, z1 } };
    float n_top[3] = { nx, ny, 0 }, n_und[3] = { -nx, -ny, 0 }, n_foot[3] = { -c, -s, 0 }, n_head[3] = { c, s, 0 };
    float n_s0[3] = { 0, 0, -1 }, n_s1[3] = { 0, 0, 1 };
    tb_quad(b, top, n_top, SA1_SURFACE_SOLID);
    tb_quad(b, und, n_und, SA1_SURFACE_SOLID);
    tb_quad(b, foot, n_foot, SA1_SURFACE_SOLID);
    tb_quad(b, head, n_head, SA1_SURFACE_SOLID);
    tb_quad(b, side0, n_s0, SA1_SURFACE_SOLID);
    tb_quad(b, side1, n_s1, SA1_SURFACE_SOLID);
}

/* prepare_test.c's map: bumpy terrain of n x n 8-unit quads, plank ramps, boxes, long thin strips */
static void build_map(tbuf *b, int n)
{
    int i, k;
    float h0 = -4.0f * (float)n;
    for (i = 0; i < n; i++) {
        for (k = 0; k < n; k++) {
            float x0 = h0 + 8.0f * i, z0 = h0 + 8.0f * k, x1 = x0 + 8.0f, z1 = z0 + 8.0f;
#define HGT(x, z) (2.0f * sinf((x) * 0.011f) * cosf((z) * 0.013f))
            float a[3] = { x0, HGT(x0, z0), z0 }, c[3] = { x1, HGT(x1, z0), z0 }, d[3] = { x1, HGT(x1, z1), z1 },
                  e[3] = { x0, HGT(x0, z1), z1 };
#undef HGT
            tb_tri(b, a, e, d, SA1_SURFACE_SOLID);
            tb_tri(b, a, d, c, SA1_SURFACE_SOLID);
        }
    }
    for (i = 0; i < 12; i++) {
        tb_plank(b, -300.0f + 50.0f * (float)(i % 4), 2.5f, -400.0f + 70.0f * (float)i, 15.0f + 2.5f * (float)i, 260.0f,
                 50.0f, 0.5f + (float)(i % 6));
    }
    for (i = 0; i < 20; i++) {
        float x = -500.0f + 53.0f * (float)i, z = 300.0f + 7.0f * (float)i;
        tb_box(b, x, x + 30.0f, -2.0f, 10.0f + (float)(i % 5) * 6.0f, z, z + 30.0f, SA1_SURFACE_SOLID);
    }
    for (i = 0; i < 40; i++) {
        float z = -600.0f + 4.0f * (float)i;
        tb_floor(b, 400.0f, 1700.0f, z, z + 4.0f, 30.0f, 1.0f, SA1_SURFACE_SOLID);
        tb_floor(b, 400.0f, 1700.0f, z, z + 4.0f, 27.0f, -1.0f, SA1_SURFACE_SOLID);
    }
}

static uint32_t g_rng;
static float frand(void)
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(g_rng >> 8) / 16777216.0f;
}

/* crash_probe.c's soup (round 4b2's verifier): a map, random planks, 3000 random triangles (every 17th water) */
static void build_soup(tbuf *b, uint32_t seed, int n)
{
    int i;
    g_rng = seed;
    build_map(b, n);
    for (i = 0; i < 60; i++) {
        tb_plank(b, -800.0f + 1600.0f * frand(), -5.0f + 40.0f * frand(), -800.0f + 1600.0f * frand(), 5.0f + 40.0f * frand(),
                 50.0f + 400.0f * frand(), 10.0f + 80.0f * frand(), 0.25f + 14.0f * frand());
    }
    for (i = 0; i < 3000; i++) {
        float a[3], c[3], d[3], x = -900.0f + 1800.0f * frand(), y = -20.0f + 80.0f * frand(), z = -900.0f + 1800.0f * frand();
        float s = 2.0f + 300.0f * frand() * frand();
        a[0] = x;
        a[1] = y;
        a[2] = z;
        c[0] = x + s * (frand() - 0.5f);
        c[1] = y + s * (frand() - 0.5f);
        c[2] = z + s * (frand() - 0.5f);
        d[0] = x + s * (frand() - 0.5f);
        d[1] = y + s * (frand() - 0.5f);
        d[2] = z + s * (frand() - 0.5f);
        tb_tri(b, a, c, d, (i % 17) == 0 ? SA1_SURFACE_WATER : SA1_SURFACE_SOLID);
    }
}

static void build_slabs(tbuf *b)
{
    int i;
    tb_floor(b, -400.0f, 400.0f, -400.0f, 400.0f, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    for (i = 0; i < 24; i++) {
        float y = 5.0f + 9.0f * (float)i, t = 0.25f + 0.6f * (float)i, x = -300.0f + 20.0f * (float)i;
        tb_box(b, x, x + 120.0f, y - t, y, -60.0f, 60.0f, SA1_SURFACE_SOLID);       /* thin slab: underside within reach */
        tb_floor(b, x, x + 120.0f, -60.0f, 60.0f, y - t - 0.01f, -1.0f, SA1_SURFACE_SOLID);
    }
    for (i = 0; i < 8; i++) {
        tb_plank(b, -200.0f + 40.0f * (float)i, 1.0f, -300.0f + 70.0f * (float)i, 10.0f + 4.0f * (float)i, 180.0f, 40.0f,
                 0.3f + 1.7f * (float)i);
    }
    tb_wall(b, -400.0f, 400.0f, 300.0f, 0.0f, 80.0f, -1.0f, SA1_SURFACE_SOLID);     /* a non-cover piece last */
}

/* round 0.9.3: host-marked FRONT_ONLY / CURVED undersides and tops (c2 keeps, marks and drops them; the thin tops over
 * front-only undersides get a material bit), FRONT_ONLY with DOUBLE_SIDED, a curved wall last */
static void build_fronts(tbuf *b)
{
    int i;
    tb_floor(b, -400.0f, 400.0f, -400.0f, 400.0f, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    for (i = 0; i < 20; i++) {
        float y = 6.0f + 8.0f * (float)i, t = 0.3f + 0.7f * (float)i, x = -300.0f + 25.0f * (float)i;
        uint32_t curved = (i % 4 == 1) ? SA1_SURFACE_CURVED : 0u;
        uint32_t under = SA1_SURFACE_SOLID | curved | ((i % 3 == 0) ? SA1_SURFACE_FRONT_ONLY : 0u)
                         | ((i % 5 == 2) ? SA1_SURFACE_FRONT_ONLY | SA1_SURFACE_DOUBLE_SIDED : 0u);
        tb_floor(b, x, x + 100.0f, -60.0f, 60.0f, y, 1.0f, SA1_SURFACE_SOLID | curved);
        tb_floor(b, x, x + 100.0f, -60.0f, 60.0f, y - t, -1.0f, under);
    }
    tb_wall(b, -400.0f, 400.0f, 300.0f, 0.0f, 80.0f, -1.0f, SA1_SURFACE_SOLID | SA1_SURFACE_CURVED);
}

static void build_walls(tbuf *b)
{
    int i;
    for (i = 0; i < 30; i++) {
        tb_wall(b, -300.0f, 300.0f, -300.0f + 20.0f * (float)i, -50.0f, 50.0f, (i & 1) ? 1.0f : -1.0f, SA1_SURFACE_SOLID);
    }
}

static void build_covers(tbuf *b)
{
    int i;
    for (i = 0; i < 30; i++) {
        float y = 3.0f * (float)i;
        tb_floor(b, -200.0f + 5.0f * (float)i, 200.0f, -200.0f, 200.0f - 5.0f * (float)i, y, 1.0f, SA1_SURFACE_SOLID);
    }
}

static void build_kill_water(tbuf *b)
{
    int i;
    tb_floor(b, -500.0f, 500.0f, -500.0f, 500.0f, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    for (i = 0; i < 6; i++) {
        float y = -100.0f - 30.0f * (float)i;
        tb_floor(b, -2000.0f, 2000.0f, -2000.0f, 2000.0f, y, 1.0f, SA1_SURFACE_KILL);
        tb_floor(b, -2000.0f, 2000.0f, -2000.0f, 2000.0f, y - 5.0f, -1.0f, SA1_SURFACE_KILL);     /* facing down: dropped */
        tb_floor(b, -300.0f, 300.0f, -300.0f, 300.0f, 20.0f + 10.0f * (float)i, 1.0f,
                 (i & 1) ? SA1_SURFACE_WATER : SA1_SURFACE_HEAVY_WATER);
        tb_floor(b, -300.0f, 300.0f, -300.0f, 300.0f, 18.0f + 10.0f * (float)i, -1.0f, SA1_SURFACE_WATER);
        tb_floor(b, -50.0f, 50.0f, -50.0f, 50.0f, 50.0f + (float)i, 1.0f, SA1_SURFACE_SOLID | SA1_SURFACE_KILL);
        tb_floor(b, 100.0f, 160.0f, -50.0f, 50.0f, 10.0f + (float)i, -1.0f, SA1_SURFACE_SOLID | SA1_SURFACE_DOUBLE_SIDED);
    }
    tb_box(b, 200.0f, 230.0f, 0.0f, 40.0f, 200.0f, 230.0f, SA1_SURFACE_SOLID | SA1_SURFACE_HURT | SA1_SURFACE_NO_CLIMB);
}

/* degenerate, collinear, far (inside the coordinate range), huge (split into many pieces) and tiny triangles */
static void build_odd(tbuf *b)
{
    float a[3] = { 0, 0, 0 }, c[3] = { 10, 0, 0 }, d[3] = { 0, 0, 10 }, sl[3] = { 20, 0, 0 };
    float f0[3] = { 9.9e8f, 0.0f, 9.9e8f }, f1[3] = { 9.9e8f, 0.0f, 9.9e8f - 100.0f }, f2[3] = { 9.9e8f - 100.0f, 0.0f, 9.9e8f };
    float e0[3] = { -1e9f, 5.0f, -1e9f }, e1[3] = { -1e9f, 5.0f, -1e9f + 4096.0f }, e2[3] = { -1e9f + 4096.0f, 5.0f, -1e9f };
    float h0[3] = { -10000.0f, -3.0f, -10000.0f }, h1[3] = { 10000.0f, -3.0f, -10000.0f }, h2[3] = { 0.0f, -3.0f, 10000.0f };
    float w0[3] = { -3000.0f, -50.0f, 77.0f }, w1[3] = { 3000.0f, -50.0f, 77.0f }, w2[3] = { 0.0f, 900.0f, 77.0f };
    float t0[3] = { 1.0f, 1.0f, 1.0f }, t1[3] = { 1.0f + 1e-4f, 1.0f, 1.0f }, t2[3] = { 1.0f, 1.0f, 1.0f + 1e-4f };
    tb_tri(b, a, c, d, SA1_SURFACE_SOLID);
    tb_tri(b, a, a, d, SA1_SURFACE_SOLID);               /* degenerate */
    tb_tri(b, a, c, sl, SA1_SURFACE_SOLID);              /* collinear */
    tb_tri(b, f0, f2, f1, SA1_SURFACE_SOLID);            /* far */
    tb_tri(b, e0, e1, e2, SA1_SURFACE_SOLID | SA1_SURFACE_KILL);   /* at the edge of the coordinate range */
    tb_tri(b, h0, h2, h1, SA1_SURFACE_SOLID);            /* huge: split into many pieces */
    tb_tri(b, t0, t2, t1, SA1_SURFACE_SOLID);            /* tiny */
    tb_tri(b, w0, w1, w2, SA1_SURFACE_SOLID);            /* huge wall, last */
    tb_tri(b, a, d, c, 0u);                              /* no surface bit: solid, facing down */
}

/* not finite or beyond the coordinate range: refused as a whole */
static void build_invalid(tbuf *b)
{
    float nanf_ = (float)NAN, inf = (float)INFINITY;
    float a[3] = { 0, 0, 0 }, c[3] = { 10, 0, 0 }, d[3] = { 0, 0, 10 }, n1[3] = { nanf_, 0, 0 }, i1[3] = { inf, 0, 0 };
    float g0[3] = { 2e9f, 0.0f, 0.0f }, g1[3] = { 2e9f, 0.0f, 10.0f }, g2[3] = { 2e9f + 1000.0f, 0.0f, 0.0f };
    tb_tri(b, a, c, d, SA1_SURFACE_SOLID);
    tb_tri(b, a, n1, d, SA1_SURFACE_SOLID);
    tb_tri(b, a, c, i1, SA1_SURFACE_SOLID);
    tb_tri(b, g0, g1, g2, SA1_SURFACE_SOLID);
}

static void build_dense(tbuf *b)
{
    int i;
    g_rng = 7u;
    for (i = 0; i < 3000; i++) {        /* land: more than 1024 triangles of one COL word in one cell (an object split) */
        float x = 10.0f + 200.0f * frand(), z = 10.0f + 200.0f * frand(), y = 50.0f * frand();
        float a[3] = { x, y, z }, c[3] = { x + 3.0f, y, z }, d[3] = { x, y, z + 3.0f };
        tb_tri(b, a, d, c, SA1_SURFACE_SOLID);
    }
    for (i = 0; i < 9000; i++) {        /* kill: 9000 small triangles in one kill cell (objects of at most 8192) */
        float x = 10.0f + 1500.0f * frand(), z = 10.0f + 1500.0f * frand(), y = -200.0f + 50.0f * frand();
        float a[3] = { x, y, z }, c[3] = { x + 2.0f, y, z }, d[3] = { x, y, z + 2.0f };
        tb_tri(b, a, d, c, SA1_SURFACE_KILL);
    }
    for (i = 0; i < 300; i++) {         /* stacked floors (some double-sided) in one column */
        float y = 0.5f * (float)i;
        tb_floor(b, 300.0f, 330.0f, 300.0f, 330.0f, y, (i & 1) ? 1.0f : -1.0f, SA1_SURFACE_SOLID | ((i % 3) ? 0u : SA1_SURFACE_DOUBLE_SIDED));
    }
}

/* ---- GRID parameter sets ------------------------------------------------------------------------------------------ */
typedef struct pset {
    const char *name;
    int null_grid;              /* pass NULL (the defaults) */
    int old_size;               /* struct_size ending before under_reach */
    uint32_t flags;
    float cell, edge, reach;
    int heavy;                  /* many pieces: skipped with --quick on maps */
} pset;

static const pset k_psets[] = {
    { "defaults (NULL)", 1, 0, 0, 0.0f, 0.0f, 0.0f, 0 },
    { "defaults by value", 0, 0, 0, 256.0f, 128.0f, 12.0f, 0 },
    { "under_reach 0.5", 0, 0, 0, 0.0f, 0.0f, 0.5f, 0 },
    { "under_reach 6", 0, 0, 0, 0.0f, 0.0f, 6.0f, 0 },
    { "under_reach 64", 0, 0, 0, 0.0f, 0.0f, 64.0f, 0 },
    { "KEEP_UNDERSIDES", 0, 0, SA1_GRID_KEEP_UNDERSIDES, 0.0f, 0.0f, 0.0f, 0 },
    { "CUT", 0, 0, SA1_GRID_CUT, 0.0f, 0.0f, 0.0f, 0 },
    { "CUT + under_reach 64", 0, 0, SA1_GRID_CUT, 0.0f, 0.0f, 64.0f, 0 },
    { "DC_LIMITS", 0, 0, SA1_GRID_DC_LIMITS, 0.0f, 0.0f, 0.0f, 0 },
    { "cell 16 edge 8", 0, 0, 0, 16.0f, 8.0f, 0.0f, 1 },
    { "cell 2048 edge 220", 0, 0, 0, 2048.0f, 220.0f, 0.0f, 0 },
    { "all flags, cell 64 edge 32", 0, 0, SA1_GRID_CUT | SA1_GRID_DC_LIMITS | SA1_GRID_KEEP_UNDERSIDES, 64.0f, 32.0f, 0.5f, 1 },
    { "CUT cell 16 edge 8 reach 64", 0, 0, SA1_GRID_CUT, 16.0f, 8.0f, 64.0f, 1 },
    { "struct_size before under_reach", 0, 1, 0, 0.0f, 0.0f, 0.0f, 0 },
    /* round 0.9.3 (SA1_API_CEILING) */
    { "UNDERSIDES_DROP", 0, 0, SA1_GRID_UNDERSIDES_DROP, 0.0f, 0.0f, 0.0f, 0 },
    { "UNDERSIDES_DROP + CUT + reach 64", 0, 0, SA1_GRID_UNDERSIDES_DROP | SA1_GRID_CUT, 0.0f, 0.0f, 64.0f, 0 },
};
#define NPSETS ((int)(sizeof k_psets / sizeof k_psets[0]))

static void pset_grid(const pset *ps, sa1_collision_grid *g)
{
    memset(g, 0, sizeof *g);
    g->struct_size = ps->old_size ? (uint32_t)offsetof(sa1_collision_grid, under_reach) : (uint32_t)sizeof *g;
    g->flags = ps->flags;
    g->cell_size = ps->cell;
    g->max_edge = ps->edge;
    if (!ps->old_size) {
        g->under_reach = ps->reach;
    }
}

/* the triangles as one GRID group per surface word (sa1_collision_set_static_ex / sa1_collision_prepare), in host space */
typedef struct gdesc {
    float *points;
    uint32_t *indices;
    sa1_collision_group *groups;
    uint32_t ngroups;
    sa1_collision_grid grid;
    sa1_collision_static desc;
} gdesc;

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static int gdesc_make(gdesc *d, const sa1_collision_tri *t, uint32_t n, const pset *ps)
{
    uint32_t *surf = (uint32_t *)malloc(((size_t)n + 1u) * sizeof *surf), ns = 0, i, g, w = 0;
    memset(d, 0, sizeof *d);
    d->points = (float *)malloc(((size_t)n * 9u + 1u) * sizeof(float));
    d->indices = (uint32_t *)malloc(((size_t)n * 3u + 1u) * sizeof(uint32_t));
    if (surf == NULL || d->points == NULL || d->indices == NULL) {
        free(surf);
        return 0;
    }
    for (i = 0; i < n; i++) {
        surf[i] = t[i].surface;
    }
    qsort(surf, n, sizeof *surf, cmp_u32);
    for (i = 0; i < n; i++) {
        if (i == 0 || surf[i] != surf[ns - 1u]) {
            surf[ns++] = surf[i];
        }
    }
    d->groups = (sa1_collision_group *)calloc(ns ? ns : 1u, sizeof *d->groups);
    if (d->groups == NULL) {
        free(surf);
        return 0;
    }
    for (g = 0; g < ns; g++) {
        sa1_collision_group *gr = &d->groups[g];
        gr->struct_size = sizeof *gr;
        gr->surface = surf[g];
        gr->first_point = w * 3u;
        gr->first_tri = w;
        for (i = 0; i < n; i++) {
            if (t[i].surface == surf[g]) {
                uint32_t k;
                memcpy(d->points + 9u * (size_t)w, t[i].v, 36);
                for (k = 0; k < 3; k++) {
                    d->indices[3u * (size_t)w + k] = 3u * (w - gr->first_tri) + k;
                }
                w++;
            }
        }
        gr->num_tris = w - gr->first_tri;
        gr->num_points = 3u * gr->num_tris;
    }
    free(surf);
    d->ngroups = ns;
    pset_grid(ps, &d->grid);
    d->desc.struct_size = sizeof d->desc;
    d->desc.mode = SA1_COLLISION_MODE_GRID;
    d->desc.points = d->points;
    d->desc.num_points = 3u * n;
    d->desc.indices = d->indices;
    d->desc.num_tris = n;
    d->desc.groups = d->groups;
    d->desc.num_groups = ns;
    d->desc.grid = ps->null_grid ? NULL : &d->grid;
    return 1;
}

static void gdesc_free(gdesc *d)
{
    free(d->points);
    free(d->indices);
    free(d->groups);
    memset(d, 0, sizeof *d);
}

/* ---- jobs --------------------------------------------------------------------------------------------------------- */
typedef struct prep_res {
    sa1_status st;
    uint32_t tris, objects, kills, undersides, bytes;
    char msg[160];
} prep_res;

typedef struct scene {
    char name[96];
    tbuf b;
    int source;                 /* Source units (map dumps), else native */
    int map;
    int invalid;                /* every path must refuse it (SA1_ERR_INVALID_ARGUMENT) */
} scene;

typedef struct job {
    const scene *sc;
    const pset *ps;
    sa1_context *ctx;           /* A's context (NULL: skipped) */
    prep_res a1, a2;            /* prepare_tris, prepare (groups) */
    double ms;
    volatile int32_t done;      /* A finished */
    /* B */
    int brun, bticked, bfresh;
    sa1_status bst;
    sa1_collision_info bi;
} job;

static void prep_info(sa1_status st, sa1_collision_prepared *p, prep_res *r)
{
    sa1_collision_prepared_info in;
    memset(r, 0, sizeof *r);
    r->st = st;
    if (p == NULL) {
        snprintf(r->msg, sizeof r->msg, "no object");
        return;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    if (sa1_collision_prepared_info_get(p, &in) == SA1_OK) {
        r->st = in.status;
        r->tris = in.grid_triangles;
        r->objects = in.grid_objects;
        r->kills = in.grid_kill_objects;
        r->undersides = in.grid_undersides;
        r->bytes = in.bytes;
        snprintf(r->msg, sizeof r->msg, "%s", in.message);
    }
}

static void run_job(job *j)
{
    sa1_collision_grid g;
    sa1_collision_prepared *p = NULL;
    sa1_status st;
    gdesc d;
    double t0 = sa1_now_ms();
    pset_grid(j->ps, &g);
    st = sa1_collision_prepare_tris(j->ctx, j->sc->b.t, j->sc->b.n, j->ps->null_grid ? NULL : &g, &p);
    prep_info(st, p, &j->a1);
    sa1_collision_prepared_release(p);
    p = NULL;
    if (gdesc_make(&d, j->sc->b.t, j->sc->b.n, j->ps)) {
        st = sa1_collision_prepare(j->ctx, &d.desc, &p);
        prep_info(st, p, &j->a2);
        sa1_collision_prepared_release(p);
        gdesc_free(&d);
    } else {
        j->a2.st = SA1_ERR_OUT_OF_MEMORY;
    }
    j->ms = sa1_now_ms() - t0;
    sa1_atomic_add32(&j->done, 1);
}

typedef struct pool {
    job **jobs;
    int n;
    volatile int32_t next;
} pool;

static void pool_worker(void *arg)
{
    pool *pl = (pool *)arg;
    for (;;) {
        int i = (int)sa1_atomic_add32(&pl->next, 1) - 1;
        if (i >= pl->n) {
            return;
        }
        run_job(pl->jobs[i]);
    }
}

/* ---- path B: set_static[_ex] + create + ticks ----------------------------------------------------------------------- */
static sa1_status create_tick(sa1_context *ctx, int ticks, sa1_collision_info *ci)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    int i;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SONIC;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[1] = 20.0f;
    st = sa1_character_create_ex(ctx, &d, &id);
    memset(ci, 0, sizeof *ci);
    ci->struct_size = sizeof *ci;
    sa1_collision_info_get(ctx, ci);
    if (SA1_FAILED(st)) {
        return st;
    }
    for (i = 0; i < ticks && !SA1_FAILED(st); i++) {
        sa1_input in;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.stick_y = i < 4 ? 0.0f : 1.0f;
        in.camera_yaw = YAW_X;
        in.buttons = (i % 9) == 5 ? SA1_BUTTON_JUMP : 0u;
        st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    }
    sa1_character_destroy(ctx, id);
    return st;
}

static sa1_status run_b(sa1_context *ctx, const scene *sc, const pset *ps, sa1_collision_info *ci, int *ticked)
{
    sa1_status st;
    *ticked = 0;
    memset(ci, 0, sizeof *ci);
    if (ps->null_grid) {
        st = sa1_collision_set_static(ctx, sc->b.t, sc->b.n);
    } else {
        gdesc d;
        if (!gdesc_make(&d, sc->b.t, sc->b.n, ps)) {
            return SA1_ERR_OUT_OF_MEMORY;
        }
        st = sa1_collision_set_static_ex(ctx, &d.desc);
        gdesc_free(&d);
    }
    if (SA1_FAILED(st)) {
        return st;
    }
    st = create_tick(ctx, 20, ci);
    *ticked = !SA1_FAILED(st);
    return st;
}

/* ---- contexts ----------------------------------------------------------------------------------------------------- */
static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (g_verbose) {     /* the expected refusals (SA1_ERR_LIMIT ...) are in the CHECK lines */
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : level >= SA1_LOG_WARN ? "warn" : "debug", msg);
    }
}

static sa1_context *make_ctx(int source, const char *gdi, int *loaded)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_coords co;
    *loaded = 0;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = g_verbose ? SA1_LOG_DEBUG : SA1_LOG_WARN;
    ga_host(&h);
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(source ? SA1_COORDS_PRESET_SOURCE : SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    if (gdi != NULL && *gdi) {
        sa1_load_options lo;
        sa1_status st;
        memset(&lo, 0, sizeof lo);
        lo.struct_size = sizeof lo;
        lo.characters = 1u << SA1_CHAR_SONIC;
        lo.flags = SA1_LOAD_DEFAULT;
        st = sa1_load_disc(ctx, gdi, &lo);
        if (SA1_FAILED(st)) {
            printf("    sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        } else {
            *loaded = 1;
        }
    }
    return ctx;
}

static int clean_status(sa1_status st, const scene *sc)
{
    if (sc->invalid) {
        return st == SA1_ERR_INVALID_ARGUMENT;
    }
    return st == SA1_OK || st == SA1_ERR_LIMIT || st == SA1_ERR_OUT_OF_MEMORY;
}

/* A: one context per coordinate system for the whole run (preparations need no disc) */
static sa1_context *g_ctx[2];
static int g_ctx_tried[2];

static sa1_context *ctx_get(int source)
{
    int loaded;
    if (!g_ctx_tried[source]) {
        g_ctx_tried[source] = 1;
        g_ctx[source] = make_ctx(source, NULL, &loaded);
    }
    return g_ctx[source];
}

/* B: a context with the disc per coordinate system. The context's static collision segment (64 MB, SA1CA_COLSEG_BYTES)
 * takes every synthesis of a new geometry generation behind the previous ones and is never compacted (src/world
 * sa1g_colseg_alloc), so a context that has synthesized about 64 MB fails every later creation with "no room for N
 * bytes of GRID collision": B starts a fresh context before a synthesis would not fit (A's size of it tells). */
static sa1_context *g_bctx[2];
static int g_bloaded[2], g_btried[2];
static uint64_t g_bused[2];
static uint32_t g_bcontexts;

static sa1_context *bctx_get(int source, const char *gdi, uint32_t need, int *fresh)
{
    *fresh = 0;
    if (g_bctx[source] != NULL && g_bused[source] + need + (1u << 20) > (uint64_t)SA1CA_COLSEG_BYTES) {
        sa1_shutdown(g_bctx[source]);
        g_bctx[source] = NULL;
        g_btried[source] = 0;
    }
    if (!g_btried[source]) {
        g_btried[source] = 1;
        g_bused[source] = 0;
        g_bctx[source] = make_ctx(source, gdi, &g_bloaded[source]);
        g_bcontexts++;
        *fresh = 1;
    }
    return g_bloaded[source] ? g_bctx[source] : NULL;
}

static void wait_done(const job *j)
{
    while (sa1_atomic_add32((volatile int32_t *)&j->done, 0) == 0) {
#if defined(_WIN32)
        Sleep(1);
#else
        usleep(1000);
#endif
    }
}

/* every scene x every parameter set: A on worker threads, B on this thread at the same time (each B after its A) */
static void run_scenes(scene *sc, int nsc, const char *gdi, int threads, int quick)
{
    int s, p, i, nj = 0, wide = sizeof(void *) >= 8;
    job *jobs;
    pool pl;
    thr th[16];
    for (s = 0; s < nsc; s++) {
        if (ctx_get(sc[s].source ? 1 : 0) == NULL) {
            CHECK(0, "sa1_init (%s coordinates)", sc[s].source ? "Source" : "native");
            return;
        }
    }
    jobs = (job *)calloc((size_t)nsc * NPSETS, sizeof *jobs);
    memset(&pl, 0, sizeof pl);
    pl.jobs = (job **)calloc((size_t)nsc * NPSETS, sizeof *pl.jobs);
    if (jobs == NULL || pl.jobs == NULL) {
        CHECK(0, "out of memory");
        free(jobs);
        free(pl.jobs);
        return;
    }
    for (s = 0; s < nsc; s++) {
        for (p = 0; p < NPSETS; p++) {
            job *j = &jobs[nj++];
            j->sc = &sc[s];
            j->ps = &k_psets[p];
            j->ctx = ctx_get(sc[s].source ? 1 : 0);
            if (quick && sc[s].map && k_psets[p].heavy) {
                j->ctx = NULL;          /* skipped */
            } else {
                pl.jobs[pl.n++] = j;
            }
        }
    }
    if (threads < 1) {
        threads = 1;
    }
    if (threads > 16) {
        threads = 16;
    }
    for (i = 0; i < threads; i++) {
        if (thr_start(&th[i], pool_worker, &pl) != 0) {
            threads = i;
            break;
        }
    }
    if (threads == 0) {
        pool_worker(&pl);
    }
    /* B on this thread meanwhile, each after its A (whose synthesis size decides whether B needs a fresh context) */
    for (i = 0; i < nj; i++) {
        job *j = &jobs[i];
        const prep_res *ref = j->ps->null_grid ? &j->a1 : &j->a2;
        int c = j->sc->source ? 1 : 0;
        sa1_context *bc;
        if (j->ctx == NULL) {
            continue;
        }
        wait_done(j);
        bc = bctx_get(c, gdi, ref->st == SA1_OK ? ref->bytes : 0u, &j->bfresh);
        if (bc == NULL) {
            continue;
        }
        j->bst = run_b(bc, j->sc, j->ps, &j->bi, &j->bticked);
        j->brun = 1;
        if (j->bst == SA1_OK) {
            g_bused[c] += (uint64_t)j->bi.bytes + 16u;
        }
    }
    for (i = 0; i < threads; i++) {
        thr_join(&th[i]);
    }
    /* the report, in scene order */
    for (i = 0; i < nj; i++) {
        job *j = &jobs[i];
        const prep_res *ref = j->ps->null_grid ? &j->a1 : &j->a2;
        int ok, same = 1;
        if (j->ctx == NULL) {
            INFO("%s / %s: skipped (--quick)", j->sc->name, j->ps->name);
            continue;
        }
        ok = clean_status(j->a1.st, j->sc) && clean_status(j->a2.st, j->sc);
        if (j->brun) {
            ok = ok && clean_status(j->bst, j->sc);
            if (j->bst == SA1_OK && ref->st == SA1_OK) {
                same = j->bi.grid_triangles == ref->tris && j->bi.grid_objects == ref->objects
                       && j->bi.grid_undersides == ref->undersides && j->bi.grid_kill_objects == ref->kills;
            } else if (ref->st == SA1_OK && wide) {
                same = 0;       /* 64-bit: what A synthesized, B must too (32-bit: B may run out of address space) */
            }
        }
        CHECK(ok && same,
              "%s (%u) / %s: prepare_tris %s %u tris %u objs %u under | prepare %s %u tris %u under | %s %s%s%s "
              "(%.0f ms A)%s",
              j->sc->name, (unsigned)j->sc->b.n, j->ps->name, sa1_status_string(j->a1.st), (unsigned)j->a1.tris,
              (unsigned)j->a1.objects, (unsigned)j->a1.undersides, sa1_status_string(j->a2.st), (unsigned)j->a2.tris,
              (unsigned)j->a2.undersides, j->ps->null_grid ? "set_static+create" : "set_static_ex+create",
              j->brun ? sa1_status_string(j->bst) : "skipped (no disc)", j->brun && j->bticked ? " +20 ticks" : "",
              j->bfresh ? " (fresh context)" : "", j->ms, same ? "" : " - B differs from A");
        if (j->a1.st != SA1_OK && j->a1.msg[0] && g_verbose) {
            INFO("    %s", j->a1.msg);
        }
    }
    free(pl.jobs);
    free(jobs);
}

/* ---- map dumps ---------------------------------------------------------------------------------------------------- */
static int load_tris(const char *path, tbuf *b)
{
    FILE *f = fopen(path, "rb");
    long sz;
    memset(b, 0, sizeof *b);
    if (f == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz % (long)sizeof(sa1_collision_tri) != 0) {
        fclose(f);
        return 0;
    }
    b->n = b->cap = (uint32_t)(sz / (long)sizeof(sa1_collision_tri));
    b->t = (sa1_collision_tri *)malloc((size_t)sz);
    if (b->t == NULL || fread(b->t, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        tb_free(b);
        return 0;
    }
    fclose(f);
    return 1;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* DIR/tris_*.bin, sorted */
static int list_maps(const char *dir, char ***out)
{
    char **v = NULL;
    int n = 0, cap = 0;
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    char pat[1024];
    HANDLE h;
    snprintf(pat, sizeof pat, "%s\\tris_*.bin", dir);
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (n == cap) {
                cap = cap ? 2 * cap : 16;
                v = (char **)realloc(v, (size_t)cap * sizeof *v);
            }
            v[n] = (char *)malloc(strlen(dir) + strlen(fd.cFileName) + 2);
            sprintf(v[n], "%s\\%s", dir, fd.cFileName);
            n++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d != NULL && (e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        if (strncmp(e->d_name, "tris_", 5) != 0 || l < 9 || strcmp(e->d_name + l - 4, ".bin") != 0) {
            continue;
        }
        if (n == cap) {
            cap = cap ? 2 * cap : 16;
            v = (char **)realloc(v, (size_t)cap * sizeof *v);
        }
        v[n] = (char *)malloc(strlen(dir) + l + 2);
        sprintf(v[n], "%s/%s", dir, e->d_name);
        n++;
    }
    if (d != NULL) {
        closedir(d);
    }
#endif
    if (n > 1) {
        qsort(v, (size_t)n, sizeof *v, cmp_str);
    }
    *out = v;
    return n;
}

/* ---- selftest: the guard allocator in child processes ----------------------------------------------------------- */
typedef struct cover96 {
    double v[9];
    double n[3];
} cover96;

static void worker_overflow(void *arg)
{
    volatile uint8_t *p = (volatile uint8_t *)ga_malloc(48);
    (void)arg;
    p[48 + 7] = 1;
}

static void worker_clean(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < 4000; i++) {
        uint8_t *p = (uint8_t *)ga_malloc((size_t)(1 + (i * 37) % 9000));
        memset(p, i & 0xFF, (size_t)(1 + (i * 37) % 9000));
        ga_free(p);
    }
}

/* the pre-fix ca_grid_undersides: the slot of the next cover is taken (and the normal written into it) before the
 * fragment is known to be a cover; after the last cover a non-cover fragment writes past the end of the array */
static void replay_prefix_loop(void)
{
    static const double ny[] = { 1.0, 0.0, 0.9, -1.0, 0.5, 0.0 };   /* covers (ny >= 0.3) and walls; a wall last */
    uint32_t ncov = 0, f, k;
    cover96 *cov;
    for (f = 0; f < 6; f++) {
        ncov += ny[f] >= 0.3;
    }
    cov = (cover96 *)ga_malloc(ncov * sizeof *cov);
    ncov = 0;
    for (f = 0; f < 6; f++) {
        cover96 *c = &cov[ncov];
        c->n[0] = 0.0;              /* ca_frag_normal(&b->fr[f], c->n) */
        c->n[1] = ny[f];
        c->n[2] = 0.0;
        if (c->n[1] < 0.3) {
            continue;
        }
        for (k = 0; k < 9; k++) {
            c->v[k] = (double)k;
        }
        ncov++;
    }
    ga_free(cov);
}

static int selftest_child(const char *kind)
{
    volatile uint8_t *p;
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    ga_enable(NULL);
    if (!strcmp(kind, "overflow")) {
        p = (volatile uint8_t *)ga_malloc(64);
        p[64] = 1;
    } else if (!strcmp(kind, "overread")) {
        p = (volatile uint8_t *)ga_malloc(4000);
        printf("%d\n", p[4000 + 100]);
    } else if (!strcmp(kind, "slack")) {
        p = (volatile uint8_t *)ga_malloc(13);
        p[13] = 1;
        ga_free((void *)p);
    } else if (!strcmp(kind, "underflow")) {
        p = (volatile uint8_t *)ga_malloc(100);
        p[-1] = 1;
        ga_free((void *)p);
    } else if (!strcmp(kind, "uaf")) {
        p = (volatile uint8_t *)ga_malloc(100);
        ga_free((void *)p);
        printf("%d\n", p[10]);
    } else if (!strcmp(kind, "double")) {
        p = (volatile uint8_t *)ga_malloc(100);
        ga_free((void *)p);
        ga_free((void *)p);
    } else if (!strcmp(kind, "atexit")) {
        p = (volatile uint8_t *)ga_malloc(21);
        p[22] = 1;          /* damaged, never freed: found by the check at exit */
    } else if (!strcmp(kind, "worker")) {
        thr t;
        thr_start(&t, worker_overflow, NULL);
        thr_join(&t);
    } else if (!strcmp(kind, "coverslot")) {
        replay_prefix_loop();
    } else if (!strcmp(kind, "clean")) {
        thr t[4];
        int i;
        for (i = 0; i < 4; i++) {
            thr_start(&t[i], worker_clean, NULL);
        }
        for (i = 0; i < 4; i++) {
            thr_join(&t[i]);
        }
    } else {
        return 2;
    }
    fprintf(stderr, "memcheck_test child %s: not detected\n", kind);
    return 0;
}

/* runs this executable with --selftest-child KIND, stderr into report; returns the exit code (-1: could not run) */
static int run_child(const char *self, const char *kind, const char *report)
{
#if defined(_WIN32)
    char cmd[4096];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD ec = 0;
    snprintf(cmd, sizeof cmd, "\"%s\" --selftest-child %s --report \"%s\"", self, kind, report);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        return -1;
    }
    if (WaitForSingleObject(pi.hProcess, 120000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)ec;
#else
    pid_t pid = fork();
    int stt = 0;
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execl(self, self, "--selftest-child", kind, "--report", report, (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, &stt, 0) < 0) {
        return -1;
    }
    return WIFEXITED(stt) ? WEXITSTATUS(stt) : 128 + (WIFSIGNALED(stt) ? WTERMSIG(stt) : 0);
#endif
}

static char *read_text(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc(n > 0 ? (size_t)n + 1u : 1u);
    if (buf != NULL && n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf != NULL) {
        buf[n > 0 ? n : 0] = 0;
        *len = n > 0 ? (size_t)n : 0u;
    }
    return buf;
}

static void selftest(const char *self, const char *tmpdir)
{
    static const struct {
        const char *kind, *want;
        int code;
    } k[] = {
        { "overflow", "HEAP BUFFER OVERFLOW: write", GA_EXIT_CODE },
        { "overread", "HEAP BUFFER OVERFLOW: read", GA_EXIT_CODE },
        { "slack", "HEAP BUFFER OVERFLOW (found by the canary)", GA_EXIT_CODE },
        { "underflow", "HEAP BUFFER UNDERFLOW (found by the canary)", GA_EXIT_CODE },
        { "uaf", "USE AFTER FREE: read", GA_EXIT_CODE },
        { "double", "DOUBLE FREE", GA_EXIT_CODE },
        { "atexit", "canaries DAMAGED", GA_EXIT_CODE },
        { "worker", "HEAP BUFFER OVERFLOW: write", GA_EXIT_CODE },
        { "coverslot", "72 byte(s) past the end of a live 288-byte block", GA_EXIT_CODE },
        { "clean", "canaries intact", 0 },
    };
    int i;
#if !defined(_WIN32)
    (void)k;
#endif
    for (i = 0; i < (int)(sizeof k / sizeof k[0]); i++) {
        char report[1100];
        size_t n = 0;
        char *text;
        int code;
        snprintf(report, sizeof report, "%s/memcheck_selftest_%s.txt", tmpdir, k[i].kind);
        remove(report);
        code = run_child(self, k[i].kind, report);
        text = read_text(report, &n);
        CHECK(code == k[i].code && text != NULL && strstr(text, k[i].want) != NULL,
              "selftest %-9s: exit %d (want %d), report has \"%s\"%s", k[i].kind, code, k[i].code, k[i].want,
              text != NULL && strstr(text, k[i].want) != NULL ? "" : " - MISSING");
        if (text != NULL && (g_verbose || code != k[i].code || strstr(text, k[i].want) == NULL)) {
            printf("%s\n", text);
        }
        if (text != NULL && !strcmp(k[i].kind, "coverslot")) {
            /* the backtrace names the loop that wrote */
            CHECK(strstr(text, "replay_prefix_loop") != NULL || strstr(text, "memcheck_test") != NULL,
                  "selftest coverslot: the faulting thread's backtrace names the writer");
        }
        free(text);
        remove(report);
    }
}

/* ---- main --------------------------------------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    const char *only[8], *gdi = NULL, *maps = NULL, *report = NULL, *child = NULL;
    int nonly = 0, i, threads = sizeof(void *) >= 8 ? 4 : 2, quick = 0, guard = 1;
    char self[1024], tmpdir[1024];
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--only") && i + 1 < argc && nonly < 8) {
            only[nonly++] = argv[++i];
        } else if (!strcmp(argv[i], "--maps") && i + 1 < argc) {
            maps = argv[++i];
        } else if (!strcmp(argv[i], "--threads") && i + 1 < argc) {
            threads = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--quick")) {
            quick = 1;
        } else if (!strcmp(argv[i], "--no-guard")) {
            guard = 0;
        } else if (!strcmp(argv[i], "--verbose")) {
            g_verbose = 1;
        } else if (!strcmp(argv[i], "--selftest-child") && i + 1 < argc) {
            child = argv[++i];
        } else if (!strcmp(argv[i], "--report") && i + 1 < argc) {
            report = argv[++i];
        } else if (!strncmp(argv[i], "--guard-alloc", 13)) {
            ga_enable(argv[i][13] == '=' ? argv[i] + 14 : NULL);
        } else if (argv[i][0] != '-' && gdi == NULL) {
            gdi = argv[i];
        } else {
            printf("usage: memcheck_test [--only selftest|scenes|maps]... [--maps DIR] [--threads N] [--quick] [--no-guard] "
                   "[--verbose] [GDI]\n");
            return 2;
        }
    }
    if (child != NULL) {
        if (report != NULL && freopen(report, "w", stderr) == NULL) {
            return 3;
        }
        setvbuf(stderr, NULL, _IONBF, 0);
        return selftest_child(child);
    }
    if (gdi == NULL) {
        gdi = getenv("SA1_TEST_GDI");
    }
    if (maps == NULL) {
        maps = getenv("SA1_MEMCHECK_MAPS");
    }
    if (guard) {
        const char *e = getenv("SA1_GUARD_ALLOC");
        ga_enable(e != NULL && *e && strcmp(e, "0") != 0 ? e : NULL);
    }
    printf("memcheck_test: libsa1 %s, guard allocator %s, %d worker thread(s)%s\n", sa1_version_string(),
           ga_enabled() ? "ON" : "off", threads, quick ? ", quick" : "");
    {
        int want_self = nonly == 0, want_scenes = nonly == 0, want_maps = nonly == 0, k;
        for (k = 0; k < nonly; k++) {
            want_self |= !strcmp(only[k], "selftest");
            want_scenes |= !strcmp(only[k], "scenes");
            want_maps |= !strcmp(only[k], "maps");
        }
        if (want_self) {
#if defined(_WIN32)
            DWORD n = GetModuleFileNameA(NULL, self, sizeof self);
            DWORD t = GetTempPathA(sizeof tmpdir, tmpdir);
            self[n < sizeof self ? n : sizeof self - 1] = 0;
            if (t == 0 || t >= sizeof tmpdir) {
                snprintf(tmpdir, sizeof tmpdir, ".");
            } else if (t > 0 && (tmpdir[t - 1] == '\\' || tmpdir[t - 1] == '/')) {
                tmpdir[t - 1] = 0;
            }
#else
            ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
            const char *td = getenv("TMPDIR");
            if (n <= 0) {
                snprintf(self, sizeof self, "%s", argv[0]);
            } else {
                self[n] = 0;
            }
            snprintf(tmpdir, sizeof tmpdir, "%s", td != NULL && *td ? td : "/tmp");
#endif
            printf("== selftest: the guard allocator (child processes)\n");
            selftest(self, tmpdir);
        }
        if (want_scenes) {
            scene sc[16];
            int ns = 0;
            memset(sc, 0, sizeof sc);
            printf("== scenes: synthetic, every GRID parameter set, paths A (worker threads) and B (set_static + create)%s\n",
                   gdi != NULL && *gdi ? "" : " - B skipped: no GDI (SA1_TEST_GDI)");
            snprintf(sc[ns].name, sizeof sc[ns].name, "floor");
            tb_floor(&sc[ns++].b, -100.0f, 100.0f, -100.0f, 100.0f, 0.0f, 1.0f, SA1_SURFACE_SOLID);
            snprintf(sc[ns].name, sizeof sc[ns].name, "floor + wall box");
            tb_floor(&sc[ns].b, -100.0f, 100.0f, -100.0f, 100.0f, 0.0f, 1.0f, SA1_SURFACE_SOLID);
            tb_box(&sc[ns++].b, 20.0f, 24.0f, 0.0f, 40.0f, -50.0f, 50.0f, SA1_SURFACE_SOLID);
            snprintf(sc[ns].name, sizeof sc[ns].name, "prepare_test map 200");
            build_map(&sc[ns++].b, 200);
            snprintf(sc[ns].name, sizeof sc[ns].name, "crash_probe soup");
            build_soup(&sc[ns++].b, 1u, 60);
            snprintf(sc[ns].name, sizeof sc[ns].name, "walls only");
            build_walls(&sc[ns++].b);
            snprintf(sc[ns].name, sizeof sc[ns].name, "covers only");
            build_covers(&sc[ns++].b);
            snprintf(sc[ns].name, sizeof sc[ns].name, "thin slabs, a wall last");
            build_slabs(&sc[ns++].b);
            snprintf(sc[ns].name, sizeof sc[ns].name, "front-only / curved undersides (round 0.9.3)");
            build_fronts(&sc[ns++].b);
            snprintf(sc[ns].name, sizeof sc[ns].name, "kill planes and water");
            build_kill_water(&sc[ns++].b);
            snprintf(sc[ns].name, sizeof sc[ns].name, "odd triangles");
            build_odd(&sc[ns++].b);
            snprintf(sc[ns].name, sizeof sc[ns].name, "dense cells");
            build_dense(&sc[ns++].b);
            snprintf(sc[ns].name, sizeof sc[ns].name, "not finite / out of range (refused)");
            sc[ns].invalid = 1;
            build_invalid(&sc[ns++].b);
            snprintf(sc[ns].name, sizeof sc[ns].name, "empty");
            ns++;
            run_scenes(sc, ns, gdi, threads, 0);
            for (k = 0; k < ns; k++) {
                tb_free(&sc[k].b);
            }
        }
        if (want_maps) {
            char **files = NULL;
            int nf = maps != NULL && *maps ? list_maps(maps, &files) : 0;
            if (nf == 0) {
                printf("== maps: skipped (no tris_*.bin: --maps DIR or SA1_MEMCHECK_MAPS)\n");
            } else {
                printf("== maps: %d map dump(s) in %s, every GRID parameter set%s\n", nf, maps,
                       quick ? " (--quick: the heavy ones left out)" : "");
            }
            for (k = 0; k < nf; k++) {
                scene s1;
                const char *b = strrchr(files[k], '/'), *b2 = strrchr(files[k], '\\');
                memset(&s1, 0, sizeof s1);
                b = b2 > b ? b2 : b;
                snprintf(s1.name, sizeof s1.name, "%s", b ? b + 1 : files[k]);
                s1.source = 1;
                s1.map = 1;
                if (!load_tris(files[k], &s1.b)) {
                    CHECK(0, "%s: cannot read", files[k]);
                } else {
                    run_scenes(&s1, 1, gdi, threads, quick);
                }
                tb_free(&s1.b);
                free(files[k]);
            }
            free(files);
        }
    }
    {
        ga_stats gs;
        uint32_t bad = ga_check();
        ga_stats_get(&gs);
        if (ga_enabled()) {
            CHECK(bad == 0, "guard allocator: %llu blocks, peak %.1f MB, every live block's canaries intact at the end",
                  (unsigned long long)gs.allocs, (double)gs.peak_bytes / 1048576.0);
        }
    }
    if (g_bcontexts) {
        INFO("path B used %u context(s) with the disc (a fresh one before the collision segment would overflow)",
             (unsigned)g_bcontexts);
    }
    sa1_shutdown(g_ctx[0]);
    sa1_shutdown(g_ctx[1]);
    sa1_shutdown(g_bctx[0]);
    sa1_shutdown(g_bctx[1]);
    printf("memcheck_test: %s (%d failure%s)\n", g_fail ? "FAILED" : "passed", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
