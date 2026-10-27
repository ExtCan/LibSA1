/*
 * coladapt_test.c - tests of the collision adapter (src/sa1_coladapt.c, sadc docs/libsa1_integration.md
 * section 5) and of the grouped static collision API (sa1_collision_set_static_ex).
 *
 *   coladapt_test [<game.gdi> | -] [<dump prefix> | -]
 *       game.gdi     the user's disc (or SA1_TEST_GDI): DISC mode, STG01 from the disc
 *       dump prefix  a harness landtable dump v2 (default ../sadc/build/traces/land/L01A0, made by
 *                    sa1h trace --level 1 --act 0 ... --dump-land build/traces/land/L01A0)
 *
 * 1. API: validation, host -> SA1 conversion, derived evalflags / bounds, the store, info, modes.
 * 2. Synthesis layout: a small FAITHFUL description built into the static collision segment and read
 *    back field by field (COL, NJS_OBJECT, NJS_MODEL, materials, type-0 polygons, odd one-triangle
 *    strips {0x8003, a, c, b}).
 * 3. Exactness (dump + disc): the dump goes through the public API (FAITHFUL, SA1 space, raw flags)
 *    into the collision segment; the Emerald Coast act 1 landtable of the user's disc (STG01.PRS at
 *    0x0C900000, D_8C19E9FC[1][0] = 0x0C981554) is used in place in a world arena. Both landtables
 *    are walked exactly like the game's cache build: 8C028EC2 (object matrix unit * T * Rz * Ry * Rx
 *    * S, colobj.center = njCalcPoint(model->center)), 8C028B00 (children first), 8C0287A0 /
 *    8C028666 (strip buffer, centroid (v0 + v1 + v2) * 0.3333f in strip order, corner order by
 *    (flip + i) & 1, radius^2 by FIPR, material attrflags, z extent B_8C73EDF0, bucket key
 *    cz - B_8C73EDEC), with the Ninja matrix functions modelled on the SH-4 FPU models of the
 *    reference traces (sadc include/pc/sh4_fpmodel.h, as libsa1/src/rt/rt_matrix.c does for
 *    m == NULL) under MXCSR round-toward-zero + FTZ + DAZ. Every polygon record and every colobj
 *    must be bit-identical between DISC and FAITHFUL, and DISC's corners must equal the dump's (the
 *    interpreter ran the original SDK code), which checks the walker itself.
 * 4. With the lowered collision code linked (tests/build_coladapt.py --game): the real 8C028E60 /
 *    8C028EC2 / 8C052518 run in two worlds, one on each landtable, at positions along ec1_run_jump;
 *    the colobj / colpoly caches and the gathered lists must be identical (object pointers compared
 *    by COL index).
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_coladapt.h"
#include "pc/sh4_fpmodel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>

#include "guard_alloc.c"       /* SA1_GUARD_ALLOC: tests/guard_alloc.h */

static int g_fail, g_skip, g_known;

#define CHECK(cond, ...)                                                                                  \
    do {                                                                                                  \
        if (!(cond)) {                                                                                    \
            g_fail++;                                                                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                   \
            printf(__VA_ARGS__);                                                                          \
            printf("\n");                                                                                 \
        }                                                                                                 \
    } while (0)

#define SKIP(...)                                                                                         \
    do {                                                                                                  \
        g_skip++;                                                                                         \
        printf("SKIP: ");                                                                                 \
        printf(__VA_ARGS__);                                                                              \
        printf("\n");                                                                                     \
    } while (0)

static long g_live_allocs;

static void *alloc_cb(void *user, size_t n)
{
    void *p = ga_malloc(n ? n : 1);
    (void)user;
    g_live_allocs += p != NULL;
    return p;
}

static void free_cb(void *user, void *p)
{
    (void)user;
    g_live_allocs -= p != NULL;
    ga_free(p);
}

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    printf("  [libsa1 %d] %s\n", (int)level, msg);
}

static uint32_t f2u(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/* ---- guest memory: the world's arena and the context's collision segment ------------------------ */
typedef struct gmem {
    const sa1g_world *w;
    const sa1g_shared *sh;
    int bad;                    /* an access outside mapped guest memory happened */
} gmem;

static const uint8_t *gptr(gmem *m, uint32_t g, uint32_t len)
{
    uint32_t seg = (g >> 24) & 0x1Fu, off = g & 0xFFFFFFu;
    if (g != 0 && seg == SA1G_SEG_ARENA && off + (uint64_t)len <= SA1G_ARENA_SIZE) {
        return (const uint8_t *)sa1g_world_ptr(m->w, g);
    }
    if (g != 0 && seg >= SA1G_SEG_COL0 && seg < SA1G_SEG_COL0 + SA1G_SEG_COL_COUNT && m->sh != NULL) {
        uint64_t o = (uint64_t)(g & 0x1FFFFFFFu) - (SA1G_COL_GUEST & 0x1FFFFFFFu);
        if (o + len <= m->sh->col_used) {
            return m->sh->col + o;
        }
    }
    m->bad++;
    return NULL;
}

static uint32_t g32(gmem *m, uint32_t g)
{
    const uint8_t *p = gptr(m, g, 4);
    return p ? sa1_rd32(p) : 0u;
}

static uint32_t g16(gmem *m, uint32_t g)
{
    const uint8_t *p = gptr(m, g, 2);
    return p ? sa1_rd16(p) : 0u;
}

static float gf(gmem *m, uint32_t g)
{
    uint32_t u = g32(m, g);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* ---- the Ninja matrix functions on XMTRX (m == NULL), as the SDK computes them -------------------- */
static void mx_translate(float *m, const float v[3])
{
    float in[4], r[4];
    in[0] = v[0];
    in[1] = v[1];
    in[2] = v[2];
    in[3] = 1.0f;
    sh4_ftrv(m, in, r);
    memcpy(m + 12, r, sizeof r);
}

static void mx_rows(float *m, int ra, int rb, const float va[4], const float vb[4])
{
    float t[16], oa[4], ob[4];
    memcpy(t, m, sizeof t);
    sh4_ftrv(t, va, oa);
    sh4_ftrv(t, vb, ob);
    memcpy(m + 4 * ra, oa, sizeof oa);
    memcpy(m + 4 * rb, ob, sizeof ob);
}

static float fneg(float x)
{
    return sh4_u2f(sh4_f2u(x) ^ 0x80000000u);
}

static void mx_rot(float *m, int axis, uint32_t ang)
{
    float s, c, va[4], vb[4];
    sh4_fsca(ang, &s, &c);
    if (axis == 0) {            /* njRotateX: rows 1, 2 */
        va[0] = 0.0f; va[1] = c; va[2] = s; va[3] = 0.0f;
        vb[0] = 0.0f; vb[1] = fneg(s); vb[2] = c; vb[3] = 0.0f;
        mx_rows(m, 1, 2, va, vb);
    } else if (axis == 1) {     /* njRotateY: rows 0, 2 */
        va[0] = c; va[1] = 0.0f; va[2] = fneg(s); va[3] = 0.0f;
        vb[0] = s; vb[1] = 0.0f; vb[2] = c; vb[3] = 0.0f;
        mx_rows(m, 0, 2, va, vb);
    } else {                    /* njRotateZ: rows 0, 1 */
        va[0] = c; va[1] = s; va[2] = 0.0f; va[3] = 0.0f;
        vb[0] = fneg(s); vb[1] = c; vb[2] = 0.0f; vb[3] = 0.0f;
        mx_rows(m, 0, 1, va, vb);
    }
}

static void mx_scale(float *m, const float v[3])
{
    int i;
    for (i = 0; i < 4; i++) {
        m[i] = sh4_canon(m[i] * v[0]);
        m[4 + i] = sh4_canon(m[4 + i] * v[1]);
        m[8 + i] = sh4_canon(m[8 + i] * v[2]);
    }
}

static void mx_point(const float *m, const float p[3], float out[3])
{
    float in[4], r[4];
    in[0] = p[0];
    in[1] = p[1];
    in[2] = p[2];
    in[3] = 1.0f;
    sh4_ftrv(m, in, r);
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
}

/* 8C028EC2 / 8C028B00: T(pos), Rz, Ry, Rx (non-zero angles only), S(scl) on the current matrix */
static void mx_node(gmem *g, float *m, uint32_t obj)
{
    float v[3];
    int k;
    for (k = 0; k < 3; k++) {
        v[k] = gf(g, obj + 0x08 + 4u * (uint32_t)k);
    }
    mx_translate(m, v);
    if (g32(g, obj + 0x1C)) {
        mx_rot(m, 2, g32(g, obj + 0x1C));
    }
    if (g32(g, obj + 0x18)) {
        mx_rot(m, 1, g32(g, obj + 0x18));
    }
    if (g32(g, obj + 0x14)) {
        mx_rot(m, 0, g32(g, obj + 0x14));
    }
    for (k = 0; k < 3; k++) {
        v[k] = gf(g, obj + 0x20 + 4u * (uint32_t)k);
    }
    mx_scale(m, v);
}

/* ---- the cache build of one object ---------------------------------------------------------------- */
#define POLY_WORDS 18           /* key, center[3], r, v[9], attr, node, mset, mesh */

typedef struct wobj {
    uint32_t center[3];         /* colobj.center (bits) */
    uint32_t edf0;              /* colobj.unk28 = B_8C73EDF0 after 8C028B00 */
    uint32_t npoly;
    uint32_t *poly;             /* POLY_WORDS per polygon; node/mset/mesh are bookkeeping only */
    uint32_t cap;
} wobj;

typedef struct walker {
    gmem *g;
    wobj *o;
    float edec;                 /* B_8C73EDEC: colobj.center.z */
    float edf0;                 /* B_8C73EDF0 */
    uint32_t node;
    int err;
} walker;

static const float k_third = 0x1.554c98p-2f;   /* 0.3333f rounded toward zero, as SHC built it */

static void w_emit(walker *W, const float v[3][3], int odd, uint32_t attr, uint32_t mset, uint32_t mesh)
{
    float x0 = v[0][0], x1 = v[1][0], x2 = v[2][0], y0 = v[0][1], y1 = v[1][1], y2 = v[2][1];
    float z0 = v[0][2], z1 = v[1][2], z2 = v[2][2], cx, cy, cz, d, e, f, r, t[4];
    uint32_t *p;
    wobj *o = W->o;
    if (o->npoly == o->cap) {
        uint32_t ncap = o->cap ? 2u * o->cap : 256u;
        uint32_t *np = (uint32_t *)realloc(o->poly, (size_t)ncap * POLY_WORDS * sizeof(uint32_t));
        if (np == NULL) {
            W->err = 1;
            return;
        }
        o->poly = np;
        o->cap = ncap;
    }
    p = o->poly + (size_t)o->npoly++ * POLY_WORDS;
    cx = (x0 + x1 + x2) * k_third;
    cy = (y0 + y1 + y2) * k_third;
    cz = (z0 + z1 + z2) * k_third;
    p[0] = f2u(cz - W->edec);   /* 8C02CFC0's bucket key */
    if (!odd) {
        p[5] = f2u(x0); p[6] = f2u(y0); p[7] = f2u(z0);
        p[8] = f2u(x1); p[9] = f2u(y1); p[10] = f2u(z1);
        p[11] = f2u(x2); p[12] = f2u(y2); p[13] = f2u(z2);
    } else {
        p[5] = f2u(x0); p[6] = f2u(y0); p[7] = f2u(z0);
        p[8] = f2u(x2); p[9] = f2u(y2); p[10] = f2u(z2);
        p[11] = f2u(x1); p[12] = f2u(y1); p[13] = f2u(z1);
    }
    d = sh4_u2f(sh4_f2u(z1 - z0) & 0x7FFFFFFFu);
    e = sh4_u2f(sh4_f2u(z2 - z1) & 0x7FFFFFFFu);
    f = sh4_u2f(sh4_f2u(z0 - z2) & 0x7FFFFFFFu);
    if (e > d) {
        d = e;
    }
    if (f > d) {
        d = f;
    }
    if (d > W->edf0) {
        W->edf0 = d;
    }
    x0 -= cx; y0 -= cy; z0 -= cz;
    x1 -= cx; y1 -= cy; z1 -= cz;
    x2 -= cx; y2 -= cy; z2 -= cz;
    p[1] = f2u(cx);
    p[2] = f2u(cy);
    p[3] = f2u(cz);
    t[0] = x0; t[1] = y0; t[2] = z0; t[3] = 0.0f;
    r = sh4_fipr(t, t);
    t[0] = x1; t[1] = y1; t[2] = z1;
    d = sh4_fipr(t, t);
    if (d > r) {
        r = d;
    }
    t[0] = x2; t[1] = y2; t[2] = z2;
    d = sh4_fipr(t, t);
    if (d > r) {
        r = d;
    }
    p[4] = f2u(r);
    p[14] = attr;
    p[15] = W->node;
    p[16] = mset;
    p[17] = mesh;
}

/* 8C0287A0 + 8C028666 */
static void w_polys(walker *W, const float *m, uint32_t obj)
{
    static float buf[0x400][3];
    gmem *g = W->g;
    uint32_t model = g32(g, obj + 4), pts, msets, mats, nms, k;
    if (model == 0) {
        W->err = 2;             /* the game dereferences it unconditionally */
        return;
    }
    pts = g32(g, model);
    msets = g32(g, model + 0x0C);
    mats = g32(g, model + 0x10);
    nms = g16(g, model + 0x14);
    for (k = 0; k < nms; k++) {
        uint32_t ms = msets + k * 0x18u, tm = g16(g, ms), type = tm & 0xC000u, nmesh = g16(g, ms + 2);
        uint32_t cur = g32(g, ms + 4), attr = mats ? g32(g, mats + 0x14u * (tm & 0x3FFFu) + 0x10u) : 0u, j;
        int32_t n = type == 0 ? 3 : 4;
        for (j = 0; j < nmesh; j++) {
            int flip = 0, i;
            if (type & 0x8000u) {
                uint32_t h = g16(g, cur);
                n = (int32_t)(h & 0x7FFFu);
                flip = (h & 0x8000u) != 0;
                if (n > 0x400) {
                    n = 0x400;
                }
                cur += 2;
            }
            for (i = 0; i < n; i++) {
                int16_t idx = (int16_t)g16(g, cur);
                float pt[3];
                uint32_t pa = pts + (uint32_t)(12 * (int32_t)idx);
                cur += 2;
                pt[0] = gf(g, pa);
                pt[1] = gf(g, pa + 4);
                pt[2] = gf(g, pa + 8);
                mx_point(m, pt, buf[i]);
            }
            for (i = 0; i < n - 2; i++) {
                w_emit(W, (const float (*)[3])&buf[i], ((flip + i) & 1) != 0, attr, k, j);
            }
        }
    }
}

/* 8C028B00: returns the sibling */
static uint32_t w_node(walker *W, const float *parent, uint32_t obj, int depth)
{
    float m[16];
    uint32_t child;
    memcpy(m, parent, sizeof m);
    mx_node(W->g, m, obj);
    child = g32(W->g, obj + 0x2C);
    while (child != 0 && depth < 64 && !W->err) {
        child = w_node(W, m, child, depth + 1);
    }
    w_polys(W, m, obj);
    W->node++;
    return g32(W->g, obj + 0x30);
}

/* 8C028EC2 for one object: colobj center, then 8C028B00 from the unit matrix */
static int walk_object(gmem *g, const float unit[16], uint32_t obj, wobj *o)
{
    walker W;
    float m[16], c[3], out[3];
    uint32_t model, k;
    memset(&W, 0, sizeof W);
    o->npoly = 0;
    W.g = g;
    W.o = o;
    model = g32(g, obj + 4);
    if (obj == 0 || model == 0) {
        return -1;
    }
    memcpy(m, unit, sizeof m);
    mx_node(g, m, obj);
    for (k = 0; k < 3; k++) {
        c[k] = gf(g, model + 0x18 + 4 * k);
    }
    mx_point(m, c, out);
    for (k = 0; k < 3; k++) {
        o->center[k] = f2u(out[k]);
    }
    W.edec = out[2];
    W.edf0 = 0.0f;
    (void)w_node(&W, unit, obj, 0);
    o->edf0 = f2u(W.edf0);
    return W.err ? -1 : 0;
}

static void wobj_free(wobj *o)
{
    free(o->poly);
    memset(o, 0, sizeof *o);
}

static int walk_mx(gmem *g, const float unit[16], uint32_t obj, wobj *o)
{
    unsigned old = _mm_getcsr();
    int r;
    _mm_setcsr(0xFFC0u);        /* the game's FP mode: round toward zero, FTZ, DAZ, exceptions masked */
    r = walk_object(g, unit, obj, o);
    _mm_setcsr(old);
    return r;
}

static uint32_t colseg_alloc(void *user, size_t n, size_t align, void **host)
{
    return sa1g_colseg_alloc((sa1g_shared *)user, n, align, host);
}

/* ---- 1. API --------------------------------------------------------------------------------------- */
static void group_init(sa1_collision_group *g)
{
    memset(g, 0, sizeof *g);
    g->struct_size = sizeof *g;
}

static uint32_t generation(const sa1_context *ctx)
{
    sa1_collision_info info;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    return sa1_collision_info_get(ctx, &info) == SA1_OK ? info.generation : 0xFFFFFFFFu;
}

static void test_api(sa1_context *ctx)
{
    static const float pts[4][3] = { { 0, 0, 0 }, { 4, 0, 0 }, { 4, 0, -4 }, { 0, 0, -4 } };  /* host */
    uint32_t idx[9] = { 0, 1, 2, 0, 2, 3, 1, 3, 2 }, mats[3] = { 0, SA1_MAT_DOUBLE_SIDE, SA1_MAT_DOUBLE_SIDE };
    uint8_t tfl[3] = { 0, 0, SA1_COLTRI_STRIP_ODD };
    float bad[4][3];
    sa1_collision_static d;
    sa1_collision_group g[2];
    sa1_collision_info info;
    sa1_collision_tri flat[2];
    const sa1ca_static *s;
    uint32_t st_tris = 0, gen;
    sa1ca_built b;
    int k;

    /* the 0.1 API keeps working */
    memset(flat, 0, sizeof flat);
    flat[0].v[1][0] = 1.0f;
    flat[0].v[2][1] = 1.0f;
    flat[0].surface = SA1_SURFACE_SOLID;
    flat[1] = flat[0];
    CHECK(sa1_collision_set_static(ctx, flat, 2) == SA1_OK, "set_static");
    CHECK(sa1_collision_stats(ctx, &st_tris, NULL, NULL) == SA1_OK && st_tris == 2, "stats after set_static: %u",
          (unsigned)st_tris);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    CHECK(sa1_collision_info_get(ctx, &info) == SA1_OK && info.mode == SA1_COLLISION_MODE_GRID && info.triangles == 2
              && info.groups == 1 && info.collidable_triangles == 2,
          "info after set_static (mode %u, %u tris)", (unsigned)info.mode, (unsigned)info.triangles);
    CHECK(sa1ca_build(ctx, colseg_alloc, NULL, &b) == SA1_ERR_OUT_OF_MEMORY,
          "GRID synthesis needs room in a collision segment (GRID itself: tests/grid_test.c)");
    gen = generation(ctx);

    /* validation: every failure keeps the current geometry */
    CHECK(sa1_collision_set_static_ex(ctx, NULL) == SA1_ERR_INVALID_ARGUMENT, "NULL description");
    memset(&d, 0, sizeof d);
    d.struct_size = (uint32_t)offsetof(sa1_collision_static, grid) - 4;    /* below the 0.2-draft size */
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "short struct_size");
    d.struct_size = sizeof d;
    d.mode = 3;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "unknown mode");
    d.mode = SA1_COLLISION_MODE_DISC;
    d.disc_level = 12;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "DISC level out of range");
    d.mode = SA1_COLLISION_MODE_FAITHFUL;
    d.num_groups = 1;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "groups missing");
    group_init(&g[0]);
    group_init(&g[1]);
    g[0].num_points = 4;
    g[0].num_tris = 3;
    g[0].surface = SA1_SURFACE_SOLID;
    d.groups = g;
    d.points = &pts[0][0];
    d.num_points = 4;
    d.indices = idx;
    d.num_tris = 3;
    d.materials = mats;
    d.tri_flags = tfl;
    g[0].struct_size = 8;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "group struct_size");
    g[0].struct_size = sizeof g[0];
    idx[8] = 4;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "index outside the group");
    idx[8] = 2;
    g[0].num_points = 5;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "group range outside the points");
    g[0].num_points = 4;
    memcpy(bad, pts, sizeof bad);
    bad[2][1] = NAN;
    d.points = &bad[0][0];
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "NaN point");
    d.points = &pts[0][0];
    tfl[1] = 0x80;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "unknown triangle flag");
    tfl[1] = 0;
    g[0].flags = 0x100;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "unknown group flag");
    g[0].flags = 0;
    CHECK(generation(ctx) == gen && sa1_collision_stats(ctx, &st_tris, NULL, NULL) == SA1_OK && st_tris == 2,
          "failed calls keep the previous static geometry");

    /* host space (default Source coords: host = 4 * (x, -z, y)), derived evalflags and bounds */
    d.num_groups = 2;
    g[1].surface = 0;                       /* not collidable, no geometry */
    g[1].position[0] = 40.0f;               /* host x 40 = SA1 x 10 */
    g[1].angle[1] = 0x4000;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_OK, "host-space FAITHFUL description");
    s = sa1ca_static_of(ctx);
    CHECK(s != NULL && s->mode == SA1_COLLISION_MODE_FAITHFUL && s->num_groups == 2 && s->generation != gen,
          "store replaced");
    if (s != NULL && s->num_groups == 2) {
        const sa1ca_group *g0 = &s->groups[0], *g1 = &s->groups[1];
        const float *p2 = s->points + 6;    /* host (4, 0, -4) -> SA1 (1, -1, 0) */
        CHECK(p2[0] == 1.0f && p2[1] == -1.0f && p2[2] == 0.0f, "host -> SA1 point (%g %g %g)", (double)p2[0],
              (double)p2[1], (double)p2[2]);
        CHECK(g0->col_flags == 0x1u && g0->evalflags == 7u && g0->scl[0] == 1.0f,
              "SOLID -> COL 0x1, evalflags 7, scale 0 -> 1 (%X %X)", (unsigned)g0->col_flags, (unsigned)g0->evalflags);
        CHECK(g1->evalflags == 4u && g1->pos[0] == 10.0f, "evalflags of a moved, turned group: %X", (unsigned)g1->evalflags);
        for (k = 0; k < 4; k++) {
            const float *q = s->points + 3 * k;
            float dx = q[0] - g0->model_center[0], dy = q[1] - g0->model_center[1], dz = q[2] - g0->model_center[2];
            CHECK(dx * dx + dy * dy + dz * dz <= g0->model_r * g0->model_r, "point %d inside the model sphere", k);
        }
        CHECK(fabsf(g0->col_center[0] - 0.5f) < 1e-6f && fabsf(g0->col_center[1] + 0.5f) < 1e-6f
                  && g0->col_radius >= g0->model_r,
              "COL sphere = model sphere placed by the transform");
        CHECK(s->collidable_groups == 1 && s->collidable_tris == 3 && s->odd_tris == 1, "counts %u %u %u",
              (unsigned)s->collidable_groups, (unsigned)s->collidable_tris, (unsigned)s->odd_tris);
    }
    CHECK(sa1_collision_stats(ctx, &st_tris, NULL, NULL) == SA1_OK && st_tris == 3, "stats after set_static_ex");

    /* DISC and the empty geometry */
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.mode = SA1_COLLISION_MODE_DISC;
    d.disc_level = 1;
    d.disc_act = 0;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_OK, "DISC description");
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    CHECK(sa1_collision_info_get(ctx, &info) == SA1_OK && info.mode == SA1_COLLISION_MODE_DISC && info.disc_level == 1
              && info.disc_act == 0 && info.triangles == 0,
          "info of DISC");
    CHECK(sa1ca_build(ctx, colseg_alloc, NULL, &b) == SA1_ERR_UNSUPPORTED, "DISC needs no synthesis");
    d.mode = SA1_COLLISION_MODE_FAITHFUL;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_OK && sa1ca_build(ctx, colseg_alloc, NULL, &b) == SA1_OK
              && b.landtable == 0,
          "no groups: no landtable (B_8C7596F4 stays NULL)");
    CHECK(sa1_collision_set_static(ctx, NULL, 0) == SA1_OK && sa1ca_static_of(ctx) != NULL
              && sa1_collision_stats(ctx, &st_tris, NULL, NULL) == SA1_OK && st_tris == 0,
          "set_static(NULL, 0) clears");
}

/* ---- 2. synthesis layout ------------------------------------------------------------------------- */
static void test_layout(sa1_context *ctx, sa1g_world *w, sa1g_shared *sh)
{
    static const float pts[4][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, -1 }, { 0, 0, -1 } };   /* SA1 */
    static const float unit[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    uint32_t idx[9] = { 0, 1, 2, 0, 2, 3, 1, 3, 2 }, mats[3] = { 0, SA1_MAT_DOUBLE_SIDE, SA1_MAT_DOUBLE_SIDE };
    uint8_t tfl[3] = { 0, 0, SA1_COLTRI_STRIP_ODD };
    sa1_collision_static d;
    sa1_collision_group g[2];
    sa1_collision_info info;
    sa1ca_built b;
    gmem m;
    wobj o;
    uint32_t lt, col0, obj, model, ms, mesh, matp;
    sa1_status st;
    memset(&d, 0, sizeof d);
    memset(&o, 0, sizeof o);
    group_init(&g[0]);
    group_init(&g[1]);
    d.struct_size = sizeof d;
    d.mode = SA1_COLLISION_MODE_FAITHFUL;
    d.flags = SA1_COLLISION_SA1_SPACE;
    d.points = &pts[0][0];
    d.num_points = 4;
    d.indices = idx;
    d.materials = mats;
    d.tri_flags = tfl;
    d.num_tris = 3;
    d.groups = g;
    d.num_groups = 2;
    g[0].flags = SA1_COLGROUP_RAW_FLAGS;
    g[0].surface = 0xA0100001u;
    g[0].num_points = 4;
    g[0].num_tris = 3;
    g[0].position[1] = 4.0f;
    g[0].col_ywidth = 7.0f;
    g[0].col_blockbit = 0x55u;
    g[1].flags = SA1_COLGROUP_RAW_FLAGS | SA1_COLGROUP_EVALFLAGS;
    g[1].surface = 0x80000000u;
    g[1].evalflags = 0x17u;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_OK, "layout description");
    st = sa1ca_build(ctx, colseg_alloc, sh, &b);
    CHECK(st == SA1_OK && b.landtable >= SA1G_COL_GUEST && b.objects == 2 && b.materials == 2 && b.meshsets == 3
              && b.triangles == 3 && b.odd_triangles == 1,
          "FAITHFUL build: %s, landtable %08X, %u objects, %u materials, %u meshsets", sa1_status_string(st),
          (unsigned)b.landtable, (unsigned)b.objects, (unsigned)b.materials, (unsigned)b.meshsets);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    CHECK(sa1_collision_info_get(ctx, &info) == SA1_OK && info.landtable == b.landtable && info.builds == 1
              && info.bytes == b.bytes,
          "info records the build");
    if (st != SA1_OK) {
        return;
    }
    memset(&m, 0, sizeof m);
    m.w = w;
    m.sh = sh;
    lt = b.landtable;
    CHECK((int16_t)g16(&m, lt) == 2 && g16(&m, lt + 2) == 0 && g16(&m, lt + 4) == 0 && g32(&m, lt + 0x0C) == lt + 0x24
              && g32(&m, lt + 0x10) == 0,
          "_OBJ_LANDTABLE header");
    col0 = lt + 0x24;
    obj = g32(&m, col0 + 0x18);
    CHECK(g32(&m, col0 + 0x20) == 0xA0100001u && g32(&m, col0 + 0x1C) == 0x55u && gf(&m, col0 + 0x10) == 7.0f
              && gf(&m, col0 + 0x04) == 4.0f && gf(&m, col0 + 0x0C) > 0.5f,
          "COL 0 (attr %08X)", (unsigned)g32(&m, col0 + 0x20));
    CHECK(g32(&m, obj) == 0x6u && gf(&m, obj + 0x0C) == 4.0f && gf(&m, obj + 0x20) == 1.0f && g32(&m, obj + 0x2C) == 0
              && g32(&m, obj + 0x30) == 0,
          "NJS_OBJECT 0 (evalflags %X)", (unsigned)g32(&m, obj));
    model = g32(&m, obj + 4);
    ms = g32(&m, model + 0x0C);
    matp = g32(&m, model + 0x10);
    CHECK(g32(&m, model + 0x08) == 4 && g16(&m, model + 0x14) == 3 && g16(&m, model + 0x16) == 2
              && gf(&m, g32(&m, model) + 24) == 1.0f && gf(&m, g32(&m, model) + 32) == -1.0f,
          "NJS_MODEL 0");
    CHECK(g32(&m, matp + 0x10) == 0 && g32(&m, matp + 0x24) == SA1_MAT_DOUBLE_SIDE, "materials in first-use order");
    CHECK(g16(&m, ms) == 0x0000 && g16(&m, ms + 2) == 1 && g16(&m, ms + 0x18) == 0x0001 && g16(&m, ms + 0x30) == 0x8001
              && g16(&m, ms + 0x32) == 1,
          "meshsets: polygons (mat 0), polygons (mat 1), strip (mat 1) [%04X %04X %04X]", (unsigned)g16(&m, ms),
          (unsigned)g16(&m, ms + 0x18), (unsigned)g16(&m, ms + 0x30));
    mesh = g32(&m, ms + 0x30 + 4);
    CHECK(g16(&m, mesh) == 0x8003 && g16(&m, mesh + 2) == 1 && g16(&m, mesh + 4) == 2 && g16(&m, mesh + 6) == 3,
          "odd triangle (1, 3, 2) as the strip {8003, 1, 2, 3}");
    mesh = g32(&m, ms + 4);
    CHECK(g16(&m, mesh) == 0 && g16(&m, mesh + 2) == 1 && g16(&m, mesh + 4) == 2, "even triangle (0, 1, 2)");
    obj = g32(&m, col0 + 0x24 + 0x18);
    model = g32(&m, obj + 4);
    CHECK(g32(&m, obj) == 0x17u && g32(&m, col0 + 0x24 + 0x20) == 0x80000000u && g32(&m, model) == 0
              && g32(&m, model + 0x0C) == 0 && g32(&m, model + 0x10) == 0 && g16(&m, model + 0x14) == 0,
          "an empty group: no points, meshsets or materials");
    /* the cache build sees the triangles in winding order: (0,1,2), (0,2,3), (1,3,2) */
    obj = g32(&m, col0 + 0x18);
    CHECK(walk_mx(&m, unit, obj, &o) == 0 && o.npoly == 3 && m.bad == 0, "walk of the synthesized object");
    if (o.npoly == 3) {
        CHECK(o.poly[2 * POLY_WORDS + 5] == f2u(1.0f) && o.poly[2 * POLY_WORDS + 8] == f2u(0.0f)
                  && o.poly[2 * POLY_WORDS + 10] == f2u(-1.0f) && o.poly[2 * POLY_WORDS + 11] == f2u(1.0f)
                  && o.poly[2 * POLY_WORDS + 13] == f2u(-1.0f) && o.poly[2 * POLY_WORDS + 14] == SA1_MAT_DOUBLE_SIDE,
              "odd triangle emitted as (p1, p3, p2) with its material");
        CHECK(o.poly[6] == f2u(4.0f) && o.poly[14] == 0, "world y = pos.y 4, material 0");
    }
    wobj_free(&o);
}

/* ---- 3. exactness: FAITHFUL from the dump vs DISC ------------------------------------------------- */
#define FIELDS_POLY 15          /* compared words of a polygon record (bookkeeping excluded) */

static int poly_same(const uint32_t *a, const uint32_t *b)
{
    return memcmp(a, b, FIELDS_POLY * sizeof(uint32_t)) == 0;
}

/* walk every COL of two landtables (a, b; same count) and compare; returns differing polygons */
static uint32_t compare_landtables(gmem *ga, uint32_t lta, gmem *gb, uint32_t ltb, const float unit[16], const char *what,
                                   uint32_t *npoly_out)
{
    uint32_t n = g16(ga, lta), ca = g32(ga, lta + 0x0C), cb = g32(gb, ltb + 0x0C), i, k, diff = 0, objdiff = 0, npoly = 0;
    int shown = 0;
    wobj oa, ob;
    memset(&oa, 0, sizeof oa);
    memset(&ob, 0, sizeof ob);
    for (i = 0; i < n; i++) {
        uint32_t ea = ca + 0x24u * i, eb = cb + 0x24u * i;
        int ra = walk_mx(ga, unit, g32(ga, ea + 0x18), &oa), rb = walk_mx(gb, unit, g32(gb, eb + 0x18), &ob);
        if (ra != 0 || rb != 0) {
            CHECK(0, "%s: COL %u cannot be walked", what, (unsigned)i);
            continue;
        }
        npoly += oa.npoly;
        if (memcmp(oa.center, ob.center, sizeof oa.center) != 0 || oa.edf0 != ob.edf0 || oa.npoly != ob.npoly) {
            objdiff++;
            if (shown++ < 5) {
                printf("  %s: COL %u object: center %08X %08X %08X / %08X %08X %08X, z extent %08X / %08X, %u / %u "
                       "polygons\n", what, (unsigned)i, (unsigned)oa.center[0], (unsigned)oa.center[1],
                       (unsigned)oa.center[2], (unsigned)ob.center[0], (unsigned)ob.center[1], (unsigned)ob.center[2],
                       (unsigned)oa.edf0, (unsigned)ob.edf0, (unsigned)oa.npoly, (unsigned)ob.npoly);
            }
        }
        for (k = 0; k < oa.npoly && k < ob.npoly; k++) {
            if (!poly_same(oa.poly + (size_t)k * POLY_WORDS, ob.poly + (size_t)k * POLY_WORDS)) {
                diff++;
                if (shown++ < 5) {
                    const uint32_t *p = oa.poly + (size_t)k * POLY_WORDS, *q = ob.poly + (size_t)k * POLY_WORDS;
                    printf("  %s: COL %u polygon %u (mset %u mesh %u): key %08X/%08X c %08X/%08X r %08X/%08X "
                           "v0x %08X/%08X attr %08X/%08X\n", what, (unsigned)i, (unsigned)k, (unsigned)p[16],
                           (unsigned)p[17], (unsigned)p[0], (unsigned)q[0], (unsigned)p[1], (unsigned)q[1],
                           (unsigned)p[4], (unsigned)q[4], (unsigned)p[5], (unsigned)q[5], (unsigned)p[14],
                           (unsigned)q[14]);
                }
            }
        }
    }
    wobj_free(&oa);
    wobj_free(&ob);
    if (npoly_out) {
        *npoly_out = npoly;
    }
    return diff + objdiff;
}

/* the world image of the DISC tests: 1ST_READ.BIN + STG01 (0x0C900000), both from the user's disc */
static int stage_image(sa1_context *ctx, const char *gdi, sa1g_image *img, uint32_t *landtable)
{
    sa1ca_stage stage;
    const sa1g_span *fr;
    sa1_status st;
    *landtable = 0;
    st = sa1g_image_compose(ctx, gdi, SA1G_KIND_BARE, 0, img);
    if (st != SA1_OK) {
        CHECK(0, "world image from %s: %s", gdi, sa1_last_error(ctx));
        return -1;
    }
    st = sa1ca_disc_stage(ctx, gdi, 1, 0, &stage);
    CHECK(st == SA1_OK && stage.load == 0x0C900000u && strcmp(stage.name, "STG01.PRS") == 0,
          "STG01.PRS from the disc: %s", st == SA1_OK ? stage.sha1 : sa1_last_error(ctx));
    if (st != SA1_OK) {
        return -1;
    }
    st = sa1g_image_add_span(img, stage.name, stage.load, stage.data, stage.size);
    sa1ca_stage_free(ctx, &stage);
    if (st == SA1_OK) {
        st = sa1g_image_finalize(img);
    }
    CHECK(st == SA1_OK, "image with the stage overlay: %s", sa1_last_error(ctx));
    fr = sa1g_image_span_at(img, 0x8C010000u, 4);
    if (st != SA1_OK || fr == NULL) {
        return -1;
    }
    *landtable = sa1ca_disc_landtable(fr->data, fr->size, 1, 0);
    CHECK(*landtable == 0x0C981554u, "D_8C19E9FC[1][0] = %08X (Emerald Coast act 1: 0C981554)", (unsigned)*landtable);
    return *landtable ? 0 : -1;
}

typedef struct exact_ctx {
    sa1_context *ctx;
    sa1g_shared *sh;
    sa1g_world *w;              /* arena: the DISC image (or empty) */
    int have_disc;
    uint32_t disc_lt;           /* DISC landtable (arena) */
    uint32_t faith_lt;          /* FAITHFUL landtable (collision segment) */
    sa1ca_dump dump;
} exact_ctx;

static void test_exact(exact_ctx *E)
{
    static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    sa1ca_dump *D = &E->dump;
    sa1_collision_info info;
    sa1_collision_static plain;
    sa1ca_built b, b2;
    float unit[16];
    gmem m;
    wobj o;
    uint32_t i, k, diff, npoly = 0, wdiff = 0, cols = 0, odd_lt;
    sa1_status st;
    memset(&o, 0, sizeof o);
    memset(&m, 0, sizeof m);
    m.w = E->w;
    m.sh = E->sh;

    /* the dump through the public API */
    st = sa1_collision_set_static_ex(E->ctx, &D->desc);
    CHECK(st == SA1_OK, "set_static_ex(dump): %s", sa1_last_error(E->ctx));
    st = sa1ca_build(E->ctx, colseg_alloc, E->sh, &b);
    CHECK(st == SA1_OK && b.objects == D->ncol && b.triangles == D->ntri && b.odd_triangles == D->odd_triangles,
          "FAITHFUL build of the dump: %s (%u objects, %u triangles, %u odd)", sa1_status_string(st),
          (unsigned)b.objects, (unsigned)b.triangles, (unsigned)b.odd_triangles);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_collision_info_get(E->ctx, &info);
    printf("FAITHFUL landtable %08X: %u COLs (%u collidable, %u triangles), %u meshsets, %u materials, %u bytes\n",
           (unsigned)b.landtable, (unsigned)info.groups, (unsigned)info.collidable_groups,
           (unsigned)info.collidable_triangles, (unsigned)info.meshsets, (unsigned)info.materials, (unsigned)info.bytes);
    CHECK(info.collidable_groups == 151 && info.collidable_triangles == 13701,
          "EC1: 151 collidable COLs with 13,701 triangles (%u, %u)", (unsigned)info.collidable_groups,
          (unsigned)info.collidable_triangles);
    if (st != SA1_OK) {
        return;
    }
    E->faith_lt = b.landtable;

    /* the unit matrix 8C028EC2 pushes: D_8C67C640 of the image (identity) */
    memcpy(unit, ident, sizeof unit);
    if (E->have_disc) {
        for (k = 0; k < 16; k++) {
            unit[k] = gf(&m, 0x8C67C640u + 4 * k);
        }
        CHECK(memcmp(unit, ident, sizeof unit) == 0, "D_8C67C640 is the identity");
    }

    /* FAITHFUL walk == the dump's corners (the interpreter's original SDK code): checks the walker and
     * the synthesis without the disc */
    for (i = 0; i < D->ncol; i++) {
        const sa1_collision_group *g = &D->groups[i];
        uint32_t obj = g32(&m, b.cols + 0x24u * i + 0x18);
        if (walk_mx(&m, unit, obj, &o) != 0 || o.npoly != g->num_tris) {
            CHECK(0, "FAITHFUL COL %u: %u polygons, dump %u", (unsigned)i, (unsigned)o.npoly, (unsigned)g->num_tris);
            continue;
        }
        cols++;
        for (k = 0; k < o.npoly; k++) {
            const float *wv = D->world + 9u * (size_t)(g->first_tri + k);
            if (memcmp(o.poly + (size_t)k * POLY_WORDS + 5, wv, 36) != 0) {
                wdiff++;
            }
        }
    }
    CHECK(wdiff == 0 && cols == D->ncol && m.bad == 0,
          "FAITHFUL corners == dump corners (8C028EC2's njCalcPoint results): %u differ", (unsigned)wdiff);

    if (!E->have_disc) {
        SKIP("DISC comparison (set SA1_TEST_GDI=<game.gdi> or pass it)");
        wobj_free(&o);
        return;
    }
    /* DISC landtable (in place) vs FAITHFUL: COL entries and objects field by field */
    CHECK(E->disc_lt == D->landtable && g16(&m, E->disc_lt) == D->ncol, "the dump is of the DISC landtable (%08X, %u COLs)",
          (unsigned)D->landtable, (unsigned)g16(&m, E->disc_lt));
    diff = 0;
    for (i = 0; i < D->ncol; i++) {
        uint32_t ea = g32(&m, E->disc_lt + 0x0C) + 0x24u * i, eb = b.cols + 0x24u * i;
        uint32_t oa = g32(&m, ea + 0x18), ob = g32(&m, eb + 0x18), ma = g32(&m, oa + 4), mb = g32(&m, ob + 4);
        for (k = 0; k < 0x24; k += 4) {
            diff += k != 0x18 && g32(&m, ea + k) != g32(&m, eb + k);
        }
        diff += g32(&m, oa) != g32(&m, ob);
        for (k = 0x08; k < 0x2C; k += 4) {
            diff += g32(&m, oa + k) != g32(&m, ob + k);
        }
        diff += g32(&m, ma + 0x08) != g32(&m, mb + 0x08);
        for (k = 0x18; k < 0x28; k += 4) {
            diff += g32(&m, ma + k) != g32(&m, mb + k);
        }
    }
    CHECK(diff == 0 && m.bad == 0, "COL entries, NJS_OBJECT transforms and model bounds identical (%u words differ)",
          (unsigned)diff);
    /* the cache build on both: every colobj and polygon bit-identical */
    diff = compare_landtables(&m, E->disc_lt, &m, b.landtable, unit, "DISC/FAITHFUL", &npoly);
    printf("cache build walk: %u COLs, %u polygons compared\n", (unsigned)D->ncol, (unsigned)npoly);
    CHECK(diff == 0 && npoly == D->ntri && m.bad == 0, "DISC and FAITHFUL caches bit-identical: %u differ", (unsigned)diff);

    /* why the odd strip triangles are encoded as strips: the same geometry as plain type-0 triangles */
    plain = D->desc;
    plain.tri_flags = NULL;
    odd_lt = 0;
    if (sa1_collision_set_static_ex(E->ctx, &plain) == SA1_OK && sa1ca_build(E->ctx, colseg_alloc, E->sh, &b2) == SA1_OK) {
        odd_lt = b2.landtable;
        diff = compare_landtables(&m, E->disc_lt, &m, odd_lt, unit, "DISC/type-0 only", NULL);
        printf("info: with every triangle as a type-0 polygon %u of %u polygons/objects differ from DISC (centroid sum "
               "order of the %u odd strip triangles)\n", (unsigned)diff, (unsigned)npoly, (unsigned)D->odd_triangles);
    }
    CHECK(sa1_collision_set_static_ex(E->ctx, &D->desc) == SA1_OK, "dump description restored");
    wobj_free(&o);
}

/* ---- 4. the lowered collision code on both landtables -------------------------------------------- */
#define FN_LAND_INIT   0x8C028E60u
#define FN_LAND_UPDATE 0x8C028EC2u
#define FN_GATHER      0x8C052518u
#define FN_INIT_MATRIX 0x8C638FE4u
#define ADDR_MATSTACK  0x8C7EC8E0u
#define ADDR_COLDISP   0x8C752BDCu  /* tcrf_CollisionDisplayVar: -1 = no debug print */
#define ADDR_CAMERA    0x8C18B7ECu  /* camera_twp */
#define ADDR_PLAYERS   0x8C78C608u  /* playertwp[8] */

typedef struct game_run {
    float pos[3];
    int gather_all;
} game_run;

static void game_boot_cb(sa1g_world *w, void *user)
{
    void (*init_matrix)(float *, int32_t, int32_t) =
        (void (*)(float *, int32_t, int32_t))sa1_fn(SA1_GFN_C(FN_INIT_MATRIX), 0u);
    int32_t (*land_init)(void) = (int32_t (*)(void))sa1_fn(SA1_GFN_C(FN_LAND_INIT), 0u);
    (void)user;
    init_matrix((float *)sa1g_world_ptr(w, ADDR_MATSTACK), 128, 0);
    land_init();
}

static void game_update_cb(sa1g_world *w, void *user)
{
    int32_t (*land_update)(void) = (int32_t (*)(void))sa1_fn(SA1_GFN_C(FN_LAND_UPDATE), 0u);
    const game_run *r = (const game_run *)user;
    uint32_t twp = 0;
    sa1g_world_rd32(w, ADDR_PLAYERS, &twp);
    memcpy(sa1g_world_ptr(w, twp + 0x20), r->pos, 12);
    memcpy(sa1g_world_ptr(w, twp + 0x20 + 0x40), r->pos, 12);     /* the camera task's taskwk */
    if (r->gather_all) {
        sa1g_world_wr32(w, 0x8C196044u, 1);
    }
    land_update();
}

static int game_world(sa1g_world *w, const sa1g_image *img, uint32_t landtable)
{
    uint32_t twp;
    void *h;
    if (sa1g_world_apply_image(w, img) != SA1_OK) {
        return -1;
    }
    twp = sa1g_world_ext_alloc(w, 0x80, 16, &h);    /* player 1 taskwk, then the camera's */
    if (twp == 0) {
        return -1;
    }
    sa1g_world_wr32(w, ADDR_PLAYERS, twp);
    sa1g_world_wr32(w, ADDR_CAMERA, twp + 0x40);
    *(uint8_t *)sa1g_world_ptr(w, ADDR_COLDISP) = 0xFF;
    sa1g_world_wr32(w, SA1CA_ADDR_LANDTABLE, landtable);
    return sa1g_world_call(w, game_boot_cb, NULL) == SA1_OK ? 0 : -1;
}

/* COL index of an object pointer of a landtable (-1: not one) */
static int32_t obj_index(gmem *m, uint32_t lt, uint32_t v)
{
    uint32_t n = g16(m, lt), cols = g32(m, lt + 0x0C), i;
    if (v == 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (g32(m, cols + 0x24u * i + 0x18) == v) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* arenas equal word for word, but for the landtable pointer and object pointers of the same COL */
static uint32_t arena_diff(sa1g_world *wa, sa1g_world *wb, const sa1g_shared *sh, uint32_t lta, uint32_t ltb,
                           uint32_t *objptrs)
{
    gmem ma, mb;
    uint32_t off, n = 0, shown = 0;
    memset(&ma, 0, sizeof ma);
    memset(&mb, 0, sizeof mb);
    ma.w = wa;
    ma.sh = sh;
    mb.w = wb;
    mb.sh = sh;
    *objptrs = 0;
    for (off = 0; off < SA1G_ARENA_SIZE; off += 4) {
        uint32_t a, b;
        int32_t ia, ib;
        memcpy(&a, wa->ram + off, 4);
        memcpy(&b, wb->ram + off, 4);
        if (a == b || 0x8C000000u + off == SA1CA_ADDR_LANDTABLE) {
            continue;
        }
        ia = obj_index(&ma, lta, a);
        ib = ia >= 0 ? obj_index(&mb, ltb, b) : -1;
        if (ia >= 0 && ia == ib) {
            (*objptrs)++;
            continue;
        }
        if (shown++ < 8) {
            printf("  arena %08X: %08X / %08X\n", (unsigned)(0x8C000000u + off), (unsigned)a, (unsigned)b);
        }
        n++;
    }
    return n;
}

static void test_game(exact_ctx *E, const sa1g_image *img)
{
    static const game_run runs[] = {
        { { -9.0f, 16.0f, 4.0f }, 0 },          /* ec1_run_jump start */
        { { -9.0f, 4.0f, 4.0f }, 0 },
        { { -60.0f, 4.0f, 30.0f }, 0 },
        { { -130.0f, 4.0f, 70.0f }, 0 },
        { { -220.6f, 8.03f, 114.3f }, 0 },      /* step 180 */
        { { -285.6f, 2.76f, 146.96f }, 0 },     /* step 210 */
        { { -371.4f, 4.0f, 159.2f }, 0 },       /* step 240 */
        { { -462.4f, 4.0f, 196.75f }, 0 },      /* step 270 */
        { { -700.0f, 10.0f, 300.0f }, 0 },
        { { -9.0f, 4.0f, 4.0f }, 1 },           /* 8C052E30: everything (the polygon pool overflows) */
        { { -9.0f, 4.0f, 4.0f }, 0 },
        { { 3000.0f, 0.0f, 3000.0f }, 0 },      /* nothing near: every object leaves the cache */
    };
    static sa1g_world wa, wb;
    uint32_t i, d, objptrs, total_objs = 0;
    int ok = 1;
    if (!sa1g_game_linked() || sa1_fn_name(FN_LAND_UPDATE) == NULL || sa1_fn_name(FN_GATHER) == NULL
        || sa1_fn_name(FN_INIT_MATRIX) == NULL) {
        SKIP("lowered collision code (game code: %s; build with tests/build_coladapt.py --game)", sa1g_game_info());
        return;
    }
    if (sa1g_world_create(&wa, E->sh) != 0 || sa1g_world_create(&wb, E->sh) != 0) {
        CHECK(0, "game worlds");
        return;
    }
    if (game_world(&wa, img, E->disc_lt) != 0 || game_world(&wb, img, E->faith_lt) != 0) {
        CHECK(0, "boot of the game worlds: %s / %s", wa.fault.msg, wb.fault.msg);
        ok = 0;
    }
    for (i = 0; ok && i < sizeof runs / sizeof runs[0]; i++) {
        sa1_status sa = sa1g_world_call(&wa, game_update_cb, (void *)&runs[i]);
        sa1_status sb = sa1g_world_call(&wb, game_update_cb, (void *)&runs[i]);
        uint32_t na = 0, nb = 0, objs = 0, polys = 0, k;
        if (sa != SA1_OK || sb != SA1_OK) {
            /* A fault of the world core / lowering, not of the landtable, shows up in both worlds alike:
             * 8C02CF20 unlinking the first in-use colobj (prev = the negated head address 0x73FD2AB8,
             * segment 0x13) stores that word back through a native pointer (ga64 encode fault). */
            int same = sa != SA1_OK && sb != SA1_OK && wa.fault.kind == wb.fault.kind
                       && wa.fault.dc_func == wb.fault.dc_func && strcmp(wa.fault.msg, wb.fault.msg) == 0;
            printf("  run %2u: world fault in %s (%s, guest %08X): %s%s\n", (unsigned)i,
                   wa.fault.func[0] ? wa.fault.func : "?", sa1g_fault_name(wa.fault.kind), (unsigned)wa.fault.guest,
                   wa.fault.msg, same ? " - identical in both worlds" : "");
            CHECK(same, "run %u: DISC and FAITHFUL fault alike (%s / %s)", (unsigned)i, wa.fault.msg, wb.fault.msg);
            if (same && wa.fault.kind == SA1G_FAULT_ENCODE) {
                g_known++;
                printf("KNOWN ISSUE (world core / pclower, not the adapter): ga64 cannot store the segment-0x13 "
                       "head link of the colobj in-use list back to memory (8C02CF20: next->prev = prev)\n");
            }
            ok = 0;
            break;
        }
        sa1g_world_rd32(&wa, 0x8C754EF0u, &na);
        sa1g_world_rd32(&wb, 0x8C754EF0u, &nb);
        for (k = 0; k < 192; k++) {
            uint32_t v = 0;
            sa1g_world_rd32(&wa, 0x8C6BB27Cu + 0x40u * k + 8, &v);
            objs += v != 0;
        }
        for (k = 0; k < 0x2000; k++) {
            uint32_t v = 0;
            sa1g_world_rd32(&wa, 0x8C6BE27Cu + 0x40u * k + 0x14, &v);
            polys += v != 0;
        }
        d = arena_diff(&wa, &wb, E->sh, E->disc_lt, E->faith_lt, &objptrs);
        printf("  run %2u (%9.3f %8.3f %8.3f%s): gathered %u / %u, cache %u objects, %u polygon slots used so far; %u object "
               "pointers by COL index, %u other words differ\n", (unsigned)i, (double)runs[i].pos[0],
               (double)runs[i].pos[1], (double)runs[i].pos[2], runs[i].gather_all ? ", all" : "", (unsigned)na,
               (unsigned)nb, (unsigned)objs, (unsigned)polys, (unsigned)objptrs, (unsigned)d);
        CHECK(d == 0 && na == nb, "run %u: the game's land collision state is identical on DISC and FAITHFUL", (unsigned)i);
        total_objs += objs;
    }
    CHECK(!ok || total_objs > 0, "the runs gathered something");
    sa1g_world_destroy(&wa);
    sa1g_world_destroy(&wb);
}

int main(int argc, char **argv)
{
    static sa1g_shared sh;
    static sa1g_world w;
    static sa1g_image img;
    static exact_ctx E;
    const char *gdi = argc > 1 && strcmp(argv[1], "-") != 0 ? argv[1] : getenv("SA1_TEST_GDI");
    const char *prefix = argc > 2 && strcmp(argv[2], "-") != 0 ? argv[2] : "../sadc/build/traces/land/L01A0";
    sa1_context *ctx = NULL;
    sa1_host host;
    sa1_status st;
    unsigned mx = _mm_getcsr();
    if (gdi != NULL && gdi[0] == 0) {
        gdi = NULL;
    }
    sa1_host_defaults(&host);
    host.log = log_cb;
    host.log_level = SA1_LOG_WARN;
    host.alloc = alloc_cb;
    host.free = free_cb;
    if (sa1_init(&host, &ctx) != SA1_OK) {
        printf("FAIL: sa1_init\n");
        return 1;
    }
    printf("libsa1 collision adapter tests: %d-bit, game code: %s\n", (int)(sizeof(void *) * 8), sa1g_game_info());
    test_api(ctx);
    if (sa1g_shared_create(&sh) != 0 || sa1g_world_create(&w, &sh) != 0) {
        printf("FAIL: world / collision segment\n");
        return 1;
    }
    test_layout(ctx, &w, &sh);

    E.ctx = ctx;
    E.sh = &sh;
    E.w = &w;
    st = sa1ca_dump_load(ctx, prefix, &E.dump);
    if (st != SA1_OK) {
        SKIP("landtable dump %s (%s): make it with sa1h trace --level 1 --act 0 --no-objects --input "
             "@harness/tests/sim/ec1_run_jump.txt --dump-land build/traces/land/L01A0", prefix, sa1_last_error(ctx));
    } else {
        printf("dump %s: landtable %08X, %u COLs, %u triangles; %u strips: flip bit deduced for %u, assumed 0 for %u "
               "one-triangle strips, %u conflicts; %u odd triangles\n", prefix, (unsigned)E.dump.landtable,
               (unsigned)E.dump.ncol, (unsigned)E.dump.ntri, (unsigned)E.dump.strips, (unsigned)E.dump.parity_deduced,
               (unsigned)E.dump.parity_assumed, (unsigned)E.dump.parity_conflicts, (unsigned)E.dump.odd_triangles);
        CHECK(E.dump.parity_conflicts == 0, "every strip's corner order is explained by a flip bit");
        if (gdi != NULL) {
            if (stage_image(ctx, gdi, &img, &E.disc_lt) == 0 && sa1g_world_apply_image(&w, &img) == SA1_OK) {
                E.have_disc = 1;
            }
        }
        test_exact(&E);
        if (E.have_disc && E.faith_lt) {
            test_game(&E, &img);
        } else {
            SKIP("lowered collision code on both landtables (needs the disc and the dump)");
        }
        sa1ca_dump_free(ctx, &E.dump);
    }
    CHECK((_mm_getcsr() & 0xFFC0u) == (mx & 0xFFC0u), "host MXCSR modes unchanged (the sticky status bits aside)");
    sa1g_world_destroy(&w);
    sa1g_shared_destroy(&sh);
    sa1g_image_free(&img);
    sa1_shutdown(ctx);
    CHECK(g_live_allocs == 0, "every host allocation freed (%ld live)", g_live_allocs);
    printf("%s: %d failure(s), %d skipped, %d known issue(s) outside the adapter\n", g_fail ? "FAIL" : "PASS",
           g_fail, g_skip, g_known);
    return g_fail ? 1 : 0;
}
