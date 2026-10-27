/*
 * prepare_test.c - preparation on host threads (include/libsa1.h "Preparation on host threads": G-SA1 request 11) and the
 * NEAR-poly debug query (request 12).
 *
 *   prepare_test [--only NAME]... [--verbose] SADC GDI          (build / run: tests/build_prepare.py)
 *
 *   api       argument checks; DISC is refused and the object holds the failure; a failed preparation is refused by
 *             sa1_collision_set_prepared; NULL clears; info and reference counts; the host's reference released before
 *             the world's (the world keeps the segment); sa1_collision_info / _stats on prepared geometry
 *   same      a prepared GRID geometry is exactly the synthesis that sa1_collision_set_static's first world makes: the
 *             collision segments are byte-identical (but the GRID header's generation and token words), and Sonic and
 *             Knuckles runs on both are bit-identical step by step (position, speeds, angles, mode, action)
 *   faithful  the Emerald Coast act 1 landtable dump (SADC build/traces/land/L01A0) prepared FAITHFUL against
 *             sa1_collision_set_static_ex FAITHFUL: 300 pairs of Sonic from the stage's start, bit-identical
 *   threads   8 preparations of one map on 4 threads while the main thread ticks a character on the same context:
 *             every segment byte-identical to the reference; sa1_character_prepare on a thread while the main thread
 *             creates every kind (the images installed once); the counting allocator balances after shutdown
 *   timing    a big map: the first creation after sa1_collision_set_static (the GRID synthesis on the calling thread)
 *             against prepared geometry; creations (respawns) with prepared geometry and images: p50 / p90 / max per
 *             kind, the first tick and the first render; SA1_LOAD_WORLD_IMAGES
 *   near      sa1_debug_near_polys: the raised list (512; 256 before round 0.9.2) and its guest address, entries equal the world's memory, the
 *             objects they belong to; SA1_GRID_DC_LIMITS: the game's own list (96); a full list counted
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_world.h"
#include "sa1_api_world.h"
#include "sa1_coladapt.h"

#include "guard_alloc.c"       /* SA1_GUARD_ALLOC / --guard-alloc: tests/guard_alloc.h */

#define PI 3.14159265358979323846
#define YAW_X ((float)(-5.9 * PI / 180.0))     /* camera yaw for a forward stick along +x (grid_test.c) */

static int g_fail;
static int g_verbose;
static volatile int32_t g_allocs, g_frees;

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

/* ---- threads ------------------------------------------------------------------------------------------ */
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

/* ---- the host --------------------------------------------------------------------------------------------- */
static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : level >= SA1_LOG_WARN ? "warn" : "debug", msg);
    }
}

/* thread-safe counting allocator (the preparations allocate on worker threads) */
static void *alloc_cb(void *user, size_t n)
{
    (void)user;
    sa1_atomic_add32(&g_allocs, 1);
    return ga_malloc(n);
}

static void free_cb(void *user, void *p)
{
    (void)user;
    if (p != NULL) {
        sa1_atomic_add32(&g_frees, 1);
    }
    ga_free(p);
}

static sa1_context *make_ctx(const char *gdi, uint32_t chars, uint32_t flags)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    sa1_status st;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = g_verbose ? SA1_LOG_DEBUG : SA1_LOG_WARN;
    h.alloc = alloc_cb;
    h.free = free_cb;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = chars;
    lo.flags = SA1_LOAD_DEFAULT | flags;
    st = sa1_load_disc(ctx, gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("    sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static double now_ms(void)
{
    return sa1_now_ms();
}

static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* p50 / p90 / max of v[n] (sorted in place) */
static void pstats(double *v, int n, double *p50, double *p90, double *mx)
{
    qsort(v, (size_t)n, sizeof *v, cmpd);
    *p50 = v[n / 2];
    *p90 = v[(n * 9) / 10 < n ? (n * 9) / 10 : n - 1];
    *mx = v[n - 1];
}

/* ---- scenes ------------------------------------------------------------------------------------------- */
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

/* a planar quad p0..p3 (in order around it) whose front faces along n */
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

/* a plank ramp along +x from (x0, y0): a slab t thick, deg degrees, len long (top, underside, ends, sides) */
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

/* a map: bumpy terrain of n x n 8-unit quads around the origin, plank ramps, boxes, long thin strips */
static void build_map(tbuf *b, int n)
{
    int i, k;
    float h0 = -4.0f * (float)n;
    memset(b, 0, sizeof *b);
    for (i = 0; i < n; i++) {
        for (k = 0; k < n; k++) {
            float x0 = h0 + 8.0f * i, z0 = h0 + 8.0f * k, x1 = x0 + 8.0f, z1 = z0 + 8.0f;
#define HGT(x, z) (2.0f * sinf((x) * 0.011f) * cosf((z) * 0.013f) + ((x) > 300.0f ? 0.0f : 0.0f))
            float a[3] = { x0, HGT(x0, z0), z0 }, c[3] = { x1, HGT(x1, z0), z0 }, d[3] = { x1, HGT(x1, z1), z1 },
                  e[3] = { x0, HGT(x0, z1), z1 };
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

/* ---- internals the checks read (tests only) ------------------------------------------------------------- */
/* the bytes of a synthesis in a collision segment (NULL: outside what it holds) */
static const uint8_t *seg_bytes(const sa1g_shared *sh, uint32_t base, uint32_t bytes)
{
    size_t off;
    if (sh == NULL || sh->col == NULL || base < SA1G_COL_GUEST) {
        return NULL;
    }
    off = (size_t)(base - SA1G_COL_GUEST);
    return off + bytes <= sh->col_used ? sh->col + off : NULL;
}

/* two GRID syntheses equal but for the header's generation and token words (+4, +8: they name the synthesis) */
static int grid_same(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    if (a == NULL || b == NULL || n < 12u) {
        return 0;
    }
    return memcmp(a, b, 4) == 0 && memcmp(a + 12, b + 12, n - 12u) == 0;
}

/* the context's own collision segment (sa1_collision_set_static's syntheses) */
static const sa1g_shared *ctx_segment(sa1_context *ctx)
{
    struct sa1w_ctx *wc = sa1_api_ext_of(ctx)->world;
    return wc != NULL ? &((const sa1g_wctx *)(const void *)wc)->shared : NULL;
}

static int prepared_info(const sa1_collision_prepared *p, sa1_collision_prepared_info *i)
{
    memset(i, 0, sizeof *i);
    i->struct_size = sizeof *i;
    return sa1_collision_prepared_info_get(p, i) == SA1_OK;
}

/* ---- runs ------------------------------------------------------------------------------------------------ */
#define RUN_PAIRS 400
#define FP_BEGIN offsetof(sa1_state_ex, step)
#define FP_END offsetof(sa1_state_ex, attach_valid)
#define FP_SIZE (FP_END - FP_BEGIN)

typedef struct runrec {
    uint8_t fp[RUN_PAIRS][FP_SIZE];     /* the game's player state after each pair (bit for bit) */
    uint32_t pairs;
    sa1_status st;
    float end[3];
} runrec;

static sa1_status create_at(sa1_context *ctx, int32_t kind, float x, float y, float z, uint32_t extra, sa1_character_id *id)
{
    sa1_character_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | extra;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    return sa1_character_create_ex(ctx, &d, id);
}

static sa1_status pair_in(sa1_context *ctx, sa1_character_id id, float sx, float sy, float yaw, uint32_t buttons)
{
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.camera_yaw = yaw;
    in.buttons = buttons;
    return sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
}

/* a scripted run: stand, run along +x with jumps, a spin dash, run on while the camera turns */
static void scripted_run(sa1_context *ctx, int32_t kind, float x, float y, float z, uint32_t extra, runrec *r)
{
    sa1_character_id id = 0;
    sa1_state_ex sx;
    uint32_t i;
    memset(r, 0, sizeof *r);
    r->st = create_at(ctx, kind, x, y, z, extra, &id);
    if (SA1_FAILED(r->st)) {
        return;
    }
    for (i = 0; i < RUN_PAIRS; i++) {
        float sy = i < 30 ? 0.0f : 1.0f, yaw = YAW_X;
        uint32_t b = 0;
        if (i >= 30 && i < 200 && (i % 60) < 6) {
            b = SA1_BUTTON_JUMP;
        }
        if (i >= 200 && i < 230) {
            sy = 0.0f;
            b = SA1_BUTTON_ACTION;
        }
        if (i >= 230) {
            yaw = YAW_X + (float)(i - 230) * 0.02f;
        }
        r->st = pair_in(ctx, id, 0.0f, sy, yaw, b);
        memset(&sx, 0, sizeof sx);
        sx.struct_size = sizeof sx;
        sa1_character_state_ex(ctx, id, &sx);
        memcpy(r->fp[i], (const uint8_t *)&sx + FP_BEGIN, FP_SIZE);
        r->pairs++;
        memcpy(r->end, sx.sa1_position, sizeof r->end);
        if (SA1_FAILED(r->st)) {
            break;
        }
    }
    sa1_character_destroy(ctx, id);
}

/* the first pair whose state differs (-1: none) */
static int run_diff(const runrec *a, const runrec *b)
{
    uint32_t i, n = a->pairs < b->pairs ? a->pairs : b->pairs;
    for (i = 0; i < n; i++) {
        if (memcmp(a->fp[i], b->fp[i], FP_SIZE) != 0) {
            return (int)i;
        }
    }
    return a->pairs == b->pairs ? -1 : (int)n;
}

/* ---- the tests ------------------------------------------------------------------------------------------- */
typedef struct G {
    const char *sadc, *gdi;
} G;

static void test_api(G *E)
{
    sa1_context *ctx = make_ctx(E->gdi, (1u << SA1_CHAR_SONIC), 0);
    sa1_collision_prepared *p = NULL, *q = NULL;
    sa1_collision_prepared_info pi;
    sa1_collision_info info;
    sa1_collision_static d;
    sa1_collision_grid gp;
    sa1_character_id id = 0;
    uint32_t tris = 0;
    tbuf b;
    sa1_status st;
    printf("api:\n");
    if (ctx == NULL) {
        CHECK(0, "context");
        return;
    }
    memset(&b, 0, sizeof b);
    tb_floor(&b, -500, 500, -500, 500, 0, 1, SA1_SURFACE_SOLID);
    tb_box(&b, 100, 160, 0, 30, -30, 30, SA1_SURFACE_SOLID);
    CHECK(sa1_collision_prepare_tris(NULL, b.t, b.n, NULL, &p) == SA1_ERR_INVALID_ARGUMENT && p == NULL
              && sa1_collision_prepare_tris(ctx, b.t, b.n, NULL, NULL) == SA1_ERR_INVALID_ARGUMENT
              && sa1_collision_prepare(ctx, NULL, &p) == SA1_ERR_INVALID_ARGUMENT && p == NULL,
          "NULL context / output / description refused (no object)");
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.mode = SA1_COLLISION_MODE_DISC;
    d.disc_level = 1;
    st = sa1_collision_prepare(ctx, &d, &p);
    CHECK(st == SA1_ERR_UNSUPPORTED && p != NULL && prepared_info(p, &pi) && pi.status == SA1_ERR_UNSUPPORTED
              && strstr(pi.message, "DISC") != NULL && pi.refs == 1,
          "DISC: SA1_ERR_UNSUPPORTED, the object holds the failure (\"%s\")", p ? pi.message : "");
    CHECK(p != NULL && sa1_collision_set_prepared(ctx, p) == SA1_ERR_INVALID_ARGUMENT,
          "a failed preparation is refused by sa1_collision_set_prepared");
    sa1_collision_prepared_release(p);
    memset(&gp, 0, sizeof gp);
    gp.struct_size = sizeof gp;
    gp.flags = 0x80u;
    st = sa1_collision_prepare_tris(ctx, b.t, b.n, &gp, &p);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT && p != NULL && prepared_info(p, &pi) && pi.message[0] != 0,
          "unknown sa1_collision_grid flags: refused, message \"%s\"", p ? pi.message : "");
    sa1_collision_prepared_release(p);
    CHECK(strcmp(sa1_last_error(ctx), "") == 0 || strstr(sa1_last_error(ctx), "grid") == NULL,
          "the failures left sa1_last_error alone (\"%s\")", sa1_last_error(ctx));
    st = sa1_collision_prepare_tris(ctx, b.t, b.n, NULL, &p);
    CHECK(st == SA1_OK && p != NULL && prepared_info(p, &pi) && pi.status == SA1_OK && pi.mode == SA1_COLLISION_MODE_GRID
              && pi.triangles == b.n && pi.grid_objects > 0 && pi.bytes > 0 && pi.grid_cell_size == 256.0f && pi.refs == 1,
          "prepared GRID: %u triangles -> %u in %u objects, %u bytes, %.2f ms, 1 reference", (unsigned)pi.triangles,
          (unsigned)pi.grid_triangles, (unsigned)pi.grid_objects, (unsigned)pi.bytes, (double)pi.ms);
    CHECK(sa1_collision_set_prepared(ctx, p) == SA1_OK && prepared_info(p, &pi) && pi.refs == 2,
          "sa1_collision_set_prepared: the context holds a reference (refs %u)", (unsigned)pi.refs);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_collision_info_get(ctx, &info);
    sa1_collision_stats(ctx, &tris, NULL, NULL);
    CHECK(info.prepared == 1 && info.mode == SA1_COLLISION_MODE_GRID && info.triangles == b.n && tris == b.n
              && info.grid_objects == pi.grid_objects && info.grid_triangles == pi.grid_triangles && info.bytes == pi.bytes,
          "sa1_collision_info / _stats describe the prepared geometry (prepared %u, %u objects)", (unsigned)info.prepared,
          (unsigned)info.grid_objects);
    st = create_at(ctx, SA1_CHAR_SONIC, 0, 10, 0, 0, &id);
    CHECK(st == SA1_OK && prepared_info(p, &pi) && pi.refs == 3 && sa1_character_world(ctx, id) != NULL
              && sa1_character_world(ctx, id)->shared == p->seg && sa1_character_world(ctx, id)->prepared == p,
          "a world created on it maps its segment by reference (refs %u)", (unsigned)pi.refs);
    sa1_collision_prepared_release(p);
    CHECK(sa1_collision_set_prepared(ctx, NULL) == SA1_OK && prepared_info(p, &pi) && pi.refs == 1,
          "the host's reference released, the geometry cleared: the world still holds it (refs %u)", (unsigned)pi.refs);
    {
        uint32_t i;
        sa1_state_ex sx;
        for (i = 0; i < 60; i++) {
            st = pair_in(ctx, id, 0.0f, i > 10 ? 1.0f : 0.0f, YAW_X, 0);
        }
        memset(&sx, 0, sizeof sx);
        sx.struct_size = sizeof sx;
        sa1_character_state_ex(ctx, id, &sx);
        CHECK(st == SA1_OK && (sx.flags & SA1_STATE_ON_GROUND) && fabsf(sx.sa1_position[1]) < 0.5f && sx.sa1_position[0] > 20.0f,
              "the world ticks on it: on the floor at (%.1f %.2f %.1f)", sx.sa1_position[0], sx.sa1_position[1],
              sx.sa1_position[2]);
    }
    sa1_character_destroy(ctx, id);
    /* p was freed with the world: a new preparation must not reuse anything of it */
    st = sa1_collision_prepare_tris(ctx, b.t, b.n, NULL, &q);
    CHECK(st == SA1_OK && q != NULL && prepared_info(q, &pi) && pi.refs == 1, "the last release freed it; a new one works");
    sa1_collision_prepared_release(q);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_collision_info_get(ctx, &info);
    CHECK(info.prepared == 0 && info.triangles == 0, "sa1_collision_set_prepared(NULL) cleared the static geometry");
    sa1_shutdown(ctx);
    tb_free(&b);
}

static void test_same(G *E)
{
    static runrec ra, rb;
    sa1_context *ctx = make_ctx(E->gdi, (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_KNUCKLES), 0);
    sa1_collision_prepared *p = NULL;
    const sa1ca_static *s;
    sa1ca_built old;
    sa1_character_id id = 0;
    tbuf b;
    int k;
    printf("same: prepared GRID = the synthesis of sa1_collision_set_static's first world\n");
    if (ctx == NULL) {
        CHECK(0, "context");
        return;
    }
    build_map(&b, 160);
    sa1_collision_set_static(ctx, b.t, b.n);
    create_at(ctx, SA1_CHAR_SONIC, 0, 10, 0, 0, &id);
    sa1_character_destroy(ctx, id);
    s = sa1ca_static_of(ctx);
    memset(&old, 0, sizeof old);
    if (s != NULL) {
        old = s->built;
    }
    sa1_collision_prepare_tris(ctx, b.t, b.n, NULL, &p);
    CHECK(p != NULL && p->status == SA1_OK && old.grid != 0 && old.bytes == p->built.bytes && old.base == p->built.base
              && old.objects == p->built.objects && old.triangles == p->built.triangles
              && old.undersides == p->built.undersides && old.undersides > 0
              && grid_same(seg_bytes(ctx_segment(ctx), old.base, old.bytes), seg_bytes(p->seg, p->built.base, p->built.bytes),
                           old.bytes),
          "%u triangles: %u bytes at %08X, %u objects, %u triangles (%u undersides left out), byte-identical but the header's "
          "generation / token", (unsigned)b.n, (unsigned)old.bytes, (unsigned)old.base, (unsigned)old.objects,
          (unsigned)old.triangles, (unsigned)old.undersides);
    for (k = 0; k < 2; k++) {
        /* each run in a fresh context: the old path synthesizes at the start of its collision segment, as the prepared
           geometry did (the same guest addresses); the prepared geometry comes from the first context */
        int32_t kind = k ? SA1_CHAR_KNUCKLES : SA1_CHAR_SONIC;
        sa1_context *ca = make_ctx(E->gdi, 1u << kind, 0), *cb = make_ctx(E->gdi, 1u << kind, 0);
        int dif;
        if (ca == NULL || cb == NULL) {
            CHECK(0, "contexts");
            break;
        }
        sa1_collision_set_static(ca, b.t, b.n);
        scripted_run(ca, kind, -200.0f, 8.0f, -380.0f + 70.0f * 3.0f + 25.0f, 0, &ra);
        sa1_collision_set_prepared(cb, p);
        scripted_run(cb, kind, -200.0f, 8.0f, -380.0f + 70.0f * 3.0f + 25.0f, 0, &rb);
        sa1_shutdown(ca);
        sa1_shutdown(cb);
        dif = run_diff(&ra, &rb);
        CHECK(ra.st == SA1_OK && rb.st == SA1_OK && ra.pairs == RUN_PAIRS && dif < 0,
              "%s, %u pairs over the terrain and a plank ramp: bit-identical on both (end (%.1f %.1f %.1f)%s%d)",
              kind == SA1_CHAR_SONIC ? "Sonic" : "Knuckles", (unsigned)ra.pairs, ra.end[0], ra.end[1], ra.end[2],
              dif < 0 ? "" : ", first difference at pair ", dif);
    }
    sa1_collision_prepared_release(p);
    sa1_shutdown(ctx);
    tb_free(&b);
}

static void test_faithful(G *E)
{
    static runrec ra, rb;
    sa1_context *ctx = make_ctx(E->gdi, (1u << SA1_CHAR_SONIC), 0);
    sa1_collision_prepared *p = NULL;
    sa1ca_dump dump;
    char prefix[1100];
    sa1_status st;
    int dif;
    printf("faithful: the Emerald Coast act 1 landtable dump, prepared against sa1_collision_set_static_ex\n");
    if (ctx == NULL) {
        CHECK(0, "context");
        return;
    }
    snprintf(prefix, sizeof prefix, "%s/build/traces/land/L01A0", E->sadc);
    memset(&dump, 0, sizeof dump);
    st = sa1ca_dump_load(ctx, prefix, &dump);
    if (st != SA1_OK) {
        INFO("skipped: no landtable dump %s (%s)", prefix, sa1_status_string(st));
        sa1_shutdown(ctx);
        return;
    }
    sa1_collision_set_static_ex(ctx, &dump.desc);
    scripted_run(ctx, SA1_CHAR_SONIC, 0, 0, 0, SA1_CREATE_START_TABLE, &ra);
    st = sa1_collision_prepare(ctx, &dump.desc, &p);
    CHECK(st == SA1_OK && p != NULL && p->mode == SA1_COLLISION_MODE_FAITHFUL && p->built.landtable != 0,
          "prepared FAITHFUL: %u groups, landtable %08X, %u bytes", p ? (unsigned)p->groups : 0u,
          p ? (unsigned)p->built.landtable : 0u, p ? (unsigned)p->built.bytes : 0u);
    {
        const sa1ca_static *s = sa1ca_static_of(ctx);
        CHECK(s != NULL && s->built.landtable == p->built.landtable && s->built.bytes == p->built.bytes
                  && memcmp(seg_bytes(ctx_segment(ctx), s->built.base, s->built.bytes),
                            seg_bytes(p->seg, p->built.base, p->built.bytes), p->built.bytes) == 0,
              "the landtable bytes equal set_static_ex's synthesis");
    }
    sa1_collision_set_prepared(ctx, p);
    scripted_run(ctx, SA1_CHAR_SONIC, 0, 0, 0, SA1_CREATE_START_TABLE, &rb);
    dif = run_diff(&ra, &rb);
    CHECK(ra.st == SA1_OK && rb.st == SA1_OK && ra.pairs == RUN_PAIRS && dif < 0,
          "Sonic from the stage's start, %u pairs: bit-identical (end (%.1f %.1f %.1f)%s%d)", (unsigned)ra.pairs, ra.end[0],
          ra.end[1], ra.end[2], dif < 0 ? "" : ", first difference at pair ", dif);
    sa1_collision_prepared_release(p);
    sa1ca_dump_free(ctx, &dump);
    sa1_shutdown(ctx);
}

/* ---- threads ------------------------------------------------------------------------------------------------ */
typedef struct prep_job {
    const sa1_context *ctx;
    const tbuf *b;
    sa1_collision_prepared *out[2];
    sa1_status st[2];
    double ms[2];
    volatile int32_t *done;
} prep_job;

static void prep_worker(void *a)
{
    prep_job *j = (prep_job *)a;
    int i;
    for (i = 0; i < 2; i++) {
        double t0 = now_ms();
        j->st[i] = sa1_collision_prepare_tris(j->ctx, j->b->t, j->b->n, NULL, &j->out[i]);
        j->ms[i] = now_ms() - t0;
    }
    sa1_atomic_add32(j->done, 1);
}

typedef struct img_job {
    sa1_context *ctx;
    sa1_status st;
    double ms;
} img_job;

static void img_worker(void *a)
{
    img_job *j = (img_job *)a;
    double t0 = now_ms();
    j->st = sa1_character_prepare(j->ctx, 0);
    j->ms = now_ms() - t0;
}

static const int32_t k_kinds[6] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA, SA1_CHAR_BIG };

static const sa1g_wctx *ctx_wctx(sa1_context *ctx)
{
    struct sa1w_ctx *wc = sa1_api_ext_of(ctx)->world;
    return wc != NULL ? (const sa1g_wctx *)(const void *)wc : NULL;
}

static void test_threads(G *E)
{
    sa1_context *ctx = make_ctx(E->gdi, 0, 0), *c2 = NULL, *c3 = NULL;
    sa1_collision_prepared *ref = NULL;
    prep_job job[4];
    thr th[4];
    volatile int32_t done = 0;
    sa1_character_id id = 0;
    uint32_t pairs = 0;
    int i, k, same = 0, total = 0, okst = 1;
    double tmax = 0.0;
    tbuf b;
    printf("threads: preparations on worker threads while the main thread ticks\n");
    if (ctx == NULL) {
        CHECK(0, "context");
        return;
    }
    build_map(&b, 200);
    sa1_collision_prepare_tris(ctx, b.t, b.n, NULL, &ref);
    sa1_collision_set_prepared(ctx, ref);
    create_at(ctx, SA1_CHAR_SONIC, -200.0f, 8.0f, 0.0f, 0, &id);
    for (i = 0; i < 4; i++) {
        memset(&job[i], 0, sizeof job[i]);
        job[i].ctx = ctx;
        job[i].b = &b;
        job[i].done = &done;
        if (thr_start(&th[i], prep_worker, &job[i]) != 0) {
            CHECK(0, "thread start");
            return;
        }
    }
    /* the main thread ticks the character on the same context the workers prepare from */
    while (sa1_atomic_load32(&done) < 4 || pairs < 30) {
        sa1_status st = pair_in(ctx, id, 0.0f, 1.0f, YAW_X + (float)pairs * 0.01f, (pairs % 50) < 5 ? SA1_BUTTON_JUMP : 0u);
        okst &= st == SA1_OK;
        pairs++;
    }
    for (i = 0; i < 4; i++) {
        thr_join(&th[i]);
    }
    for (i = 0; i < 4; i++) {
        for (k = 0; k < 2; k++) {
            sa1_collision_prepared *q = job[i].out[k];
            sa1_collision_prepared_info pi;
            total++;
            tmax = job[i].ms[k] > tmax ? job[i].ms[k] : tmax;
            if (job[i].st[k] == SA1_OK && q != NULL && prepared_info(q, &pi) && pi.refs == 1
                && q->built.bytes == ref->built.bytes && q->generation != ref->generation
                && grid_same(seg_bytes(ref->seg, ref->built.base, ref->built.bytes),
                             seg_bytes(q->seg, q->built.base, q->built.bytes), ref->built.bytes)) {
                same++;
            }
            sa1_collision_prepared_release(q);
        }
    }
    CHECK(same == total && okst,
          "%d of %d preparations on 4 threads byte-identical to the reference (%u bytes; up to %.0f ms each, concurrently), "
          "while the main thread ticked %u pairs on the same context", same, total, (unsigned)ref->built.bytes, tmax,
          (unsigned)pairs);
    sa1_character_destroy(ctx, id);
    sa1_collision_set_prepared(ctx, NULL);
    sa1_collision_prepared_release(ref);
    sa1_shutdown(ctx);
    /* the world images: a worker prepares every kind while the main thread creates them (whoever is first installs) */
    c2 = make_ctx(E->gdi, 0, 0);
    c3 = make_ctx(E->gdi, 0, 0);
    if (c2 != NULL && c3 != NULL) {
        img_job ij;
        thr t;
        int created = 0, eq = 0;
        sa1_collision_set_static(c2, b.t, 0);
        memset(&ij, 0, sizeof ij);
        ij.ctx = c2;
        thr_start(&t, img_worker, &ij);
        for (k = 0; k < 6; k++) {
            sa1_character_id cid = 0;
            if (create_at(c2, k_kinds[k], 0.0f, 10.0f, 0.0f, 0, &cid) == SA1_PARTIAL || cid != 0) {
                created++;
                pair_in(c2, cid, 0.0f, 1.0f, YAW_X, 0);
                sa1_character_destroy(c2, cid);
            }
        }
        thr_join(&t);
        /* the same images composed on one thread (c3) */
        sa1_character_prepare(c3, 0);
        for (k = 0; k < 6; k++) {
            const sa1g_wctx *w2 = ctx_wctx(c2), *w3 = ctx_wctx(c3);
            int32_t kd = k_kinds[k];
            uint32_t sp;
            int same_img = w2 != NULL && w3 != NULL && w2->composed[kd] && w3->composed[kd]
                           && w2->images[kd].num_spans == w3->images[kd].num_spans
                           && w2->images[kd].num_patches == w3->images[kd].num_patches;
            for (sp = 0; same_img && sp < w2->images[kd].num_spans; sp++) {
                same_img = w2->images[kd].spans[sp].size == w3->images[kd].spans[sp].size
                           && strcmp(w2->images[kd].spans[sp].sha1, w3->images[kd].spans[sp].sha1) == 0
                           && memcmp(w2->images[kd].spans[sp].data, w3->images[kd].spans[sp].data, w2->images[kd].spans[sp].size) == 0
                           && w2->images[kd].ctx == c2;
            }
            eq += same_img;
        }
        CHECK(ij.st == SA1_OK && created == 6 && eq == 6,
              "sa1_character_prepare on a thread (%.0f ms) while the main thread created all 6 kinds: every image composed "
              "once, identical to images composed on one thread (%d of 6), owned by the context", ij.ms, eq);
    } else {
        CHECK(0, "contexts");
    }
    sa1_shutdown(c2);
    sa1_shutdown(c3);
    tb_free(&b);
}

/* ---- timing ----------------------------------------------------------------------------------------------- */
typedef struct bg_prep {
    const sa1_context *ctx;
    const tbuf *b;
    sa1_collision_prepared *p;
    double ms;
} bg_prep;

static void bg_prep_worker(void *a)
{
    bg_prep *j = (bg_prep *)a;
    double t0 = now_ms();
    sa1_collision_prepare_tris(j->ctx, j->b->t, j->b->n, NULL, &j->p);
    j->ms = now_ms() - t0;
}

#define RESPAWNS 60

static void test_timing(G *E)
{
    sa1_context *ca = NULL, *cb = NULL;
    sa1_character_id id = 0;
    double t0, first_old, load_b, set_ms, first[6], cr[RESPAWNS], tk[RESPAWNS], rn[RESPAWNS], de[RESPAWNS];
    double p50, p90, mx, q50, q90, qmx, r50, r90, rmx, d50, d90, dmx, oldr[RESPAWNS], o50, o90, omx;
    bg_prep bg;
    thr t;
    tbuf b;
    int k, i;
    printf("timing: the game thread's share of creating characters on a big map\n");
    build_map(&b, 360);
    /* the old path: the GRID synthesis and the image in the first creation */
    ca = make_ctx(E->gdi, 0, 0);
    if (ca == NULL) {
        CHECK(0, "context");
        tb_free(&b);
        return;
    }
    sa1_collision_set_static(ca, b.t, b.n);
    t0 = now_ms();
    create_at(ca, SA1_CHAR_SONIC, -200.0f, 8.0f, 0.0f, 0, &id);
    first_old = now_ms() - t0;
    sa1_character_destroy(ca, id);
    for (i = 0; i < RESPAWNS; i++) {
        t0 = now_ms();
        create_at(ca, SA1_CHAR_SONIC, -200.0f, 8.0f, 0.0f, 0, &id);
        oldr[i] = now_ms() - t0;
        sa1_character_destroy(ca, id);
    }
    pstats(oldr, RESPAWNS, &o50, &o90, &omx);
    INFO("sa1_collision_set_static (%u triangles): the first creation %.1f ms (GRID synthesis + Sonic's image on the "
         "calling thread); Sonic respawns p50 %.2f / p90 %.2f / max %.2f ms", (unsigned)b.n, first_old, o50, o90, omx);
    sa1_shutdown(ca);
    /* prepared: the images at load (SA1_LOAD_WORLD_IMAGES), the geometry on a worker */
    t0 = now_ms();
    cb = make_ctx(E->gdi, 0, SA1_LOAD_WORLD_IMAGES);
    load_b = now_ms() - t0;
    if (cb == NULL) {
        CHECK(0, "context");
        tb_free(&b);
        return;
    }
    memset(&bg, 0, sizeof bg);
    bg.ctx = cb;
    bg.b = &b;
    thr_start(&t, bg_prep_worker, &bg);
    thr_join(&t);
    t0 = now_ms();
    sa1_collision_set_prepared(cb, bg.p);
    set_ms = now_ms() - t0;
    CHECK(bg.p != NULL && bg.p->status == SA1_OK && set_ms < 1.0,
          "the GRID synthesis on a worker: %.1f ms (%u -> %u triangles, %u objects, %u undersides left out); "
          "sa1_collision_set_prepared on the game thread %.3f ms", bg.ms, (unsigned)b.n, bg.p ? (unsigned)bg.p->built.triangles : 0u,
          bg.p ? (unsigned)bg.p->built.objects : 0u, bg.p ? (unsigned)bg.p->built.undersides : 0u, set_ms);
    INFO("sa1_load_disc with SA1_LOAD_WORLD_IMAGES: %.0f ms (the six kinds' images included)", load_b);
    for (k = 0; k < 6; k++) {
        t0 = now_ms();
        create_at(cb, k_kinds[k], -200.0f, 8.0f, 0.0f, 0, &id);
        first[k] = now_ms() - t0;
        sa1_character_destroy(cb, id);
    }
    INFO("first creation per kind on prepared geometry and images: Sonic %.2f, Tails %.2f, Knuckles %.2f, Amy %.2f, "
         "E-102 %.2f, Big %.2f ms", first[0], first[1], first[2], first[3], first[4], first[5]);
    for (i = 0; i < RESPAWNS; i++) {
        const sa1_render *r = NULL;
        int32_t kind = k_kinds[i % 6];
        t0 = now_ms();
        create_at(cb, kind, -200.0f + (float)(i % 7) * 30.0f, 8.0f, (float)(i % 5) * 40.0f, 0, &id);
        cr[i] = now_ms() - t0;
        t0 = now_ms();
        sa1_character_render(cb, id, 1.0f, &r);
        rn[i] = now_ms() - t0;
        t0 = now_ms();
        pair_in(cb, id, 0.0f, 0.0f, YAW_X, 0);
        tk[i] = now_ms() - t0;
        t0 = now_ms();
        sa1_character_destroy(cb, id);
        de[i] = now_ms() - t0;
    }
    pstats(cr, RESPAWNS, &p50, &p90, &mx);
    pstats(tk, RESPAWNS, &q50, &q90, &qmx);
    pstats(rn, RESPAWNS, &r50, &r90, &rmx);
    pstats(de, RESPAWNS, &d50, &d90, &dmx);
    {
        double fmax = 0.0;
        for (k = 0; k < 6; k++) {
            fmax = first[k] > fmax ? first[k] : fmax;
        }
        CHECK(p90 < 10.0 && fmax < 10.0,
              "sa1_character_create_ex on prepared geometry and images (%d creations of the six kinds): p50 %.2f / p90 %.2f / "
              "max %.2f ms (first of each kind up to %.2f ms; target under 10)", RESPAWNS, p50, p90, mx, fmax);
    }
    INFO("after each creation: sa1_character_render(1.0) p50 %.2f / p90 %.2f / max %.2f ms; the first pair (the land cache "
         "build) p50 %.2f / p90 %.2f / max %.2f ms; sa1_character_destroy p50 %.2f / p90 %.2f / max %.2f ms", r50, r90, rmx,
         q50, q90, qmx, d50, d90, dmx);
    sa1_collision_prepared_release(bg.p);
    sa1_shutdown(cb);
    tb_free(&b);
}

/* ---- the NEAR-poly debug query --------------------------------------------------------------------------- */
static void test_near(G *E)
{
    static sa1_near_poly polys[300];
    sa1_context *ctx = make_ctx(E->gdi, (1u << SA1_CHAR_SONIC), 0);
    sa1_character_id id = 0;
    sa1_near_info ni;
    uint32_t n = 0, i, raw = 0, bad = 0, noobj = 0, maxn = 0;
    tbuf b;
    int pass;
    printf("near: sa1_debug_near_polys\n");
    if (ctx == NULL) {
        CHECK(0, "context");
        return;
    }
    /* a dense floor of 1-unit tiles (at running speed the NEAR list collects more than 96 polygons around the sphere) */
    memset(&b, 0, sizeof b);
    for (i = 0; i < 120u * 120u; i++) {
        float x0 = -60.0f + 1.0f * (float)(i % 120u), z0 = -60.0f + 1.0f * (float)(i / 120u);
        tb_floor(&b, x0, x0 + 1.0f, z0, z0 + 1.0f, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    }
    tb_floor(&b, -1000, 1000, -1000, 1000, -0.5f, 1.0f, SA1_SURFACE_SOLID);
    for (pass = 0; pass < 2; pass++) {
        sa1_collision_static d;
        sa1_collision_grid gp;
        sa1_collision_group grp;
        float *pts = (float *)malloc((size_t)b.n * 9u * sizeof(float));
        uint32_t *idx = (uint32_t *)malloc((size_t)b.n * 3u * sizeof(uint32_t)), full0;
        sa1g_world *w;
        for (i = 0; i < b.n; i++) {
            memcpy(pts + 9u * i, b.t[i].v, 36);
            idx[3u * i] = 3u * i;
            idx[3u * i + 1] = 3u * i + 1;
            idx[3u * i + 2] = 3u * i + 2;
        }
        memset(&gp, 0, sizeof gp);
        gp.struct_size = sizeof gp;
        gp.flags = pass ? SA1_GRID_DC_LIMITS : 0u;
        memset(&grp, 0, sizeof grp);
        grp.struct_size = sizeof grp;
        grp.surface = SA1_SURFACE_SOLID;
        grp.num_points = 3u * b.n;
        grp.num_tris = b.n;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.mode = SA1_COLLISION_MODE_GRID;
        d.points = pts;
        d.num_points = 3u * b.n;
        d.indices = idx;
        d.num_tris = b.n;
        d.groups = &grp;
        d.num_groups = 1;
        d.grid = &gp;
        sa1_collision_set_static_ex(ctx, &d);
        free(pts);
        free(idx);
        memset(&ni, 0, sizeof ni);
        ni.struct_size = sizeof ni;
        id = 0;
        create_at(ctx, SA1_CHAR_SONIC, 0.0f, 3.0f, 0.0f, 0, &id);
        CHECK(sa1_debug_near_polys(ctx, id, &ni, polys, 300, &n) == SA1_OK && n == 0,
              "%s: before the first tick: an empty list", pass ? "SA1_GRID_DC_LIMITS" : "defaults");
        full0 = ni.full;
        maxn = 0;
        bad = noobj = 0;
        for (i = 0; i < 90; i++) {
            uint32_t k;
            /* stand, then run in a circle on the tiles */
            pair_in(ctx, id, 0.0f, i > 20 ? 1.0f : 0.0f, YAW_X + (float)i * 0.08f, 0);
            memset(&ni, 0, sizeof ni);
            ni.struct_size = sizeof ni;
            sa1_debug_near_polys(ctx, id, &ni, polys, 300, &n);
            w = sa1_character_world(ctx, id);
            sa1g_world_rd32(w, 0x8C73E27Cu, &raw);
            maxn = n > maxn ? n : maxn;
            bad += n != (raw < ni.capacity ? raw : ni.capacity);
            if (g_verbose) {
                uint32_t blk[5] = { 0, 0, 0, 0, 0 };
                if (ni.block) {
                    memcpy(blk, sa1g_world_ptr(w, ni.block), sizeof blk);
                }
                printf("      pair %2u: count %u (raw %u), capacity %u, list %08X, block %08X, full %u, peak %u / %u; block words "
                       "%08X %08X cap %u full %u peak %u\n", (unsigned)i, (unsigned)n, (unsigned)raw, (unsigned)ni.capacity,
                       (unsigned)ni.list, (unsigned)ni.block, (unsigned)ni.full, (unsigned)ni.peak, (unsigned)ni.peak_life,
                       (unsigned)blk[0], (unsigned)blk[1], (unsigned)blk[2], (unsigned)blk[3], (unsigned)blk[4]);
            }
            for (k = 0; k < n; k++) {
                uint32_t word = 0;
                float c[3];
                memcpy(&word, (const uint8_t *)sa1g_world_ptr(w, ni.list) + 4u * k, 4);
                memcpy(c, (const uint8_t *)sa1g_world_ptr(w, word) + 8, 12);
                bad += polys[k].guest != word || fabsf(c[0] - polys[k].center[0]) > 1e-4f
                       || fabsf(c[1] - polys[k].center[1]) > 1e-4f || fabsf(c[2] - polys[k].center[2]) > 1e-4f;
                noobj += polys[k].object == 0;
            }
        }
        if (!pass) {
            CHECK(ni.capacity == 512 && ni.block != 0 && ni.list == ni.block + 0x14u && ni.count_address == 0x8C73E27Cu
                      && maxn > 96 && ni.peak_life >= maxn && bad == 0 && noobj * 100u <= maxn,
                  "defaults: the raised list (512 since round 0.9.2) at %08X (block %08X), up to %u entries; every entry equals the world's "
                  "memory (guest word, centre) and has its land object (%u differ, %u without)", (unsigned)ni.list,
                  (unsigned)ni.block, (unsigned)maxn, (unsigned)bad, (unsigned)noobj);
        } else {
            CHECK(ni.capacity == 96 && ni.list == 0x8C73E280u && maxn == 96 && ni.full > full0 && bad == 0 && noobj * 100u <= maxn,
                  "SA1_GRID_DC_LIMITS: the game's list B_8C73E280 (96), full (%u queries filled it), entries equal the "
                  "world's memory (capacity %u, list %08X, up to %u entries, %u differ, %u without an object)",
                  (unsigned)(ni.full - full0), (unsigned)ni.capacity, (unsigned)ni.list, (unsigned)maxn, (unsigned)bad,
                  (unsigned)noobj);
        }
        sa1_character_destroy(ctx, id);
    }
    CHECK(sa1_debug_near_polys(ctx, 12345, &ni, NULL, 0, NULL) == SA1_ERR_NOT_FOUND, "an unknown character: SA1_ERR_NOT_FOUND");
    sa1_shutdown(ctx);
    tb_free(&b);
}

/* ---- main ------------------------------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        void (*fn)(G *);
    } tests[] = {
        { "api", test_api }, { "same", test_same }, { "faithful", test_faithful }, { "threads", test_threads },
        { "timing", test_timing }, { "near", test_near },
    };
    const char *only[16];
    int nonly = 0, i, t, nargs = 0;
    const char *args[2] = { NULL, NULL };
    G E;
    ga_args(&argc, argv);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--only") && i + 1 < argc && nonly < 16) {
            only[nonly++] = argv[++i];
        } else if (!strcmp(argv[i], "--verbose")) {
            g_verbose = 1;
        } else if (nargs < 2) {
            args[nargs++] = argv[i];
        }
    }
    if (nargs < 2) {
        printf("usage: prepare_test [--only api|same|faithful|threads|timing|near]... [--verbose] SADC GDI\n");
        return 2;
    }
    E.sadc = args[0];
    E.gdi = args[1];
    printf("libsa1 %s preparation tests (%d-bit), game code: %s\n", sa1_version_string(), (int)(sizeof(void *) * 8),
           sa1g_game_info());
    if (!sa1g_game_ready()) {
        printf("the step's game code is not linked\n");
        return 1;
    }
    for (t = 0; t < (int)(sizeof tests / sizeof tests[0]); t++) {
        int run_it = nonly == 0;
        double t0 = now_ms();
        for (i = 0; i < nonly; i++) {
            run_it |= !strcmp(only[i], tests[t].name);
        }
        if (run_it) {
            tests[t].fn(&E);
            printf("  (%s: %.1f s)\n", tests[t].name, (now_ms() - t0) / 1000.0);
        }
    }
    CHECK(sa1_atomic_load32(&g_allocs) == sa1_atomic_load32(&g_frees),
          "every allocation of the host allocator was freed (%d allocations)", (int)sa1_atomic_load32(&g_allocs));
    printf("%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
