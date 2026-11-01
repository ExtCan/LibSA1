/*
 * camera_test.c - round 0.9.5 (095-lib-board): the host camera in libsa1 worlds (sa1_input.host_cam_pos / host_cam_target,
 * SA1_INPUT_HOST_CAMERA; sadc docs/research/board095_cam.md 5 and 6.2 C1-C9, gsa1_095_plan.md 5.3 / 6.2 task 7). Built from
 * the research probe SADC/build/research/board095/cam/cam_probe.c, whose host camera was a SIMULATION (set_no_eye and a
 * write of w->camera.pos); here every camera goes through the real input path: sa1_character_tick with the tail and the bit.
 *
 *   py -3.12 tests/build_camera.py --run [--target all] [--debug] [-- --only C1,C2,...,C9]   (SA1_TEST_GDI)
 *
 * Coordinates: the Source preset (host = 4 x (x, -z, y)), as G-SA1. Cases:
 *   C1   Big's line alpha with the camera 15 units above the lure: the lure end >= 0x9B on every pair the lure is in the
 *        water, every vertex the game's formula (255 - d^2 / 4, 0x40..0xE0) against camera_twp->pos within 1
 *   C1b  the camera 2,000 units away: every vertex 0x40; without the bit: the camera taskwk is the virtual eye (Big + (-40, 7,
 *        0) at yaw 0) and every vertex 0x40, as 0.9.4f
 *   C2   a Sonic world, 100 pairs with a moving host camera: camera_twp->pos = sa1w_set_point(host) bit for bit (computed in
 *        the world's FP mode), ang = (0, camera_yaw BAMS - 0x4000, 0), smode 0; XMTRX and the state bit-equal to a world
 *        without the camera
 *   C3   live play of six characters, 300 pairs: no camera / a far camera / the owner's chase camera - 0 differing pairs in
 *        sa1_state_ex, the event stream and the render arrays
 *   C4   the SET spawner's centre while Big's lure is in the water (set_test (11)'s hook): a RING 30 units from the host
 *        camera and 420 from Big spawns, one 30 from Big and 480 from the camera does not; without the camera both spawn
 *        (the virtual eye 40 behind Big is the centre, 0.9.4f)
 *   C5   the board (sa1_character_board at pair 10): 0 / 200 differing pairs with a far or a chase camera; yaw 180: pspd.x
 *        0.460 at pair 60
 *   C6   invalid input (a NaN, an infinity or 1e31 in any of the six floats; the bit without the tail; the tail without the
 *        bit): the arena byte-identical to a run without a camera
 *   C7   x86 = x64: the DETERMINISM lines (camera_twp positions of C2, C1's line alphas)
 *   C8   a world re-created mid-run: the camera is back at the next tick; no fault
 *   C9   order B of the SET window (range 1600; sa1_world_set.c st_window: tier 0 within 400 units nearest first, tier 1 in
 *        front of the plane through w->camera.pos perpendicular to the camera's yaw, tier 2 the rest) with the host eye
 *        (095-fix: the 0.9.5 verifier's test gap): Sonic at the origin, camera yaw 0 (forward +X), ring_window 14; 4 RINGs
 *        within 400 units (W), 10 at x 500-950 (N), 10 at x 1100-1460 (F), 10 at x -500..-950 (B). No host camera (the
 *        virtual eye 40 behind Sonic): W + N selected (N nearer in tier 1). The host eye at x 1,000 (between N and F): W + F
 *        selected (only F in front of its plane: tier 1 follows the host eye), W unchanged. The host eye on the virtual
 *        eye's plane (300 units higher): the selection of the run without a camera.
 */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_world.h"

static int g_fail, g_pass;
static const char *g_only;
#define CHECK(cond, ...)                                                                                                  \
    do {                                                                                                                  \
        if (cond) {                                                                                                       \
            g_pass++;                                                                                                     \
            printf("  ok    ");                                                                                           \
        } else {                                                                                                          \
            g_fail++;                                                                                                     \
            printf("  FAIL  ");                                                                                           \
        }                                                                                                                 \
        printf(__VA_ARGS__);                                                                                              \
        printf("\n");                                                                                                     \
    } while (0)
#define NOTE(...)                                                                                                         \
    do {                                                                                                                  \
        printf("  note  ");                                                                                               \
        printf(__VA_ARGS__);                                                                                              \
        printf("\n");                                                                                                     \
    } while (0)

#define ADDR_PLAYER_TWP 0x8C78C608u
#define ADDR_LURE_TP    0x8C7A40C8u
#define ADDR_CAM_TWP    0x8C18B7ECu
#define ADDR_BIGBITS    0x8C7A40E8u
#define PI 3.14159265358979323846

static int want(const char *name)
{
    const char *p;
    size_t n = strlen(name);
    if (g_only == NULL) {
        return 1;
    }
    for (p = g_only; *p;) {
        const char *e = strchr(p, ',');
        size_t m = e ? (size_t)(e - p) : strlen(p);
        if (m == n && strncmp(p, name, n) == 0) {
            return 1;
        }
        p += m + (e ? 1 : 0);
        if (!e) {
            break;
        }
    }
    return 0;
}

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN) {
        printf("    [libsa1] %s\n", msg);
    }
}

static uint8_t *wp(sa1g_world *w, uint32_t g, uint32_t n)
{
    if (g == 0 || ((g >> 24) & 31u) != SA1G_SEG_ARENA || (g & 0xFFFFFFu) > SA1G_ARENA_SIZE - n) {
        return NULL;
    }
    return w->ram + (g & 0xFFFFFFu);
}
static uint32_t r32(sa1g_world *w, uint32_t g) { uint32_t v = 0; uint8_t *p = wp(w, g, 4); if (p) memcpy(&v, p, 4); return v; }
static uint32_t r16(sa1g_world *w, uint32_t g) { uint16_t v = 0; uint8_t *p = wp(w, g, 2); if (p) memcpy(&v, p, 2); return v; }
static int32_t r8s(sa1g_world *w, uint32_t g) { uint8_t *p = wp(w, g, 1); return p ? (int8_t)*p : 0; }
static float rf(sa1g_world *w, uint32_t g) { float v = 0; uint8_t *p = wp(w, g, 4); if (p) memcpy(&v, p, 4); return v; }
static void w16(sa1g_world *w, uint32_t g, uint32_t v) { uint16_t x = (uint16_t)v; uint8_t *p = wp(w, g, 2); if (p) memcpy(p, &x, 2); }

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

/* Source preset: host = 4 x (x, -z, y); SA1 = (h0, h2, -h1) / 4 */
static void to_host(const float s[3], float h[3]) { h[0] = s[0] * 4.0f; h[1] = -s[2] * 4.0f; h[2] = s[1] * 4.0f; }
static void to_sa1(const float h[3], float s[3]) { s[0] = h[0] * 0.25f; s[1] = h[2] * 0.25f; s[2] = -h[1] * 0.25f; }

/* ---- geometry (cam_probe.c) ------------------------------------------------------------------------------------- */
typedef struct tris { sa1_collision_tri *t; uint32_t n, cap; } tris;
static void tri_add(tris *b, const float a[3], const float c[3], const float d[3], uint32_t surf)
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        uint32_t cap = b->cap ? 2u * b->cap : 1024u;
        sa1_collision_tri *nt = (sa1_collision_tri *)realloc(b->t, cap * sizeof *nt);
        if (!nt) {
            return;
        }
        b->t = nt;
        b->cap = cap;
    }
    t = &b->t[b->n++];
    memcpy(t->v[0], a, 12);
    memcpy(t->v[1], c, 12);
    memcpy(t->v[2], d, 12);
    t->surface = surf;
}
static void rect(tris *b, float x0, float y0, float x1, float y1, float z, float tile, uint32_t surf)
{
    float x, y;
    for (x = x0; x < x1 - 0.01f; x += tile) {
        for (y = y0; y < y1 - 0.01f; y += tile) {
            float xe = x + tile < x1 ? x + tile : x1, ye = y + tile < y1 ? y + tile : y1;
            float a[3] = { x, y, z }, c[3] = { xe, y, z }, d[3] = { xe, ye, z }, e[3] = { x, ye, z };
            tri_add(b, a, c, d, surf);
            tri_add(b, a, d, e, surf);
        }
    }
}

static sa1_coords g_co;

/* ground 0: flat SOLID 16,384 HU square at z 0; 1: review_probe's island (600 HU square at z 0, water at -20, sea bed -600) */
static sa1_context *make_ctx(const char *gdi, int ground)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    tris b;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&g_co, 0, sizeof g_co);
    g_co.struct_size = sizeof g_co;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &g_co);
    sa1_set_coords(ctx, &g_co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) |
                    (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_WORLD_IMAGES;
    if (SA1_FAILED(sa1_load_disc(ctx, gdi, &lo))) {
        printf("load_disc: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    memset(&b, 0, sizeof b);
    if (ground == 1) {
        rect(&b, -300, -300, 300, 300, 0, 300, SA1_SURFACE_SOLID);
        rect(&b, -6000, -6000, 6000, -300, -20, 1000, SA1_SURFACE_WATER);
        rect(&b, -6000, 300, 6000, 6000, -20, 1000, SA1_SURFACE_WATER);
        rect(&b, -6000, -300, -300, 300, -20, 600, SA1_SURFACE_WATER);
        rect(&b, 300, -300, 6000, 300, -20, 600, SA1_SURFACE_WATER);
        rect(&b, -6000, -6000, 6000, 6000, -600, 1000, SA1_SURFACE_SOLID);
    } else {
        rect(&b, -8192, -8192, 8192, 8192, 0, 1024, SA1_SURFACE_SOLID);
    }
    if (SA1_FAILED(sa1_collision_set_static(ctx, b.t, b.n))) {
        printf("collision: %s\n", sa1_last_error(ctx));
        free(b.t);
        sa1_shutdown(ctx);
        return NULL;
    }
    free(b.t);
    return ctx;
}

static sa1_character_id make_char(sa1_context *ctx, int32_t kind, const float sa1_pos[3])
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD;
    if (sa1_pos != NULL) {
        d.flags |= SA1_CREATE_SA1_SPACE;
        memcpy(d.position, sa1_pos, 12);
    }
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("    create: %s\n", sa1_last_error(ctx));
        return 0;
    }
    return id;
}

/* ---- the host camera of a tick -------------------------------------------------------------------------------------- */
enum { CAM_NONE = 0, CAM_FIXED, CAM_CHASE, CAM_LURE15, CAM_FAR2000, CAM_MOVING };
enum { BAD_NONE = 0, BAD_NAN, BAD_INF, BAD_BIG, BAD_NOTAIL, BAD_NOBIT };
typedef struct camsel {
    int mode;
    float fixed[3];             /* CAM_FIXED: SA1 space */
    float dist_hu, height_hu, pitch_deg;
    int bad, bad_k;             /* C6 */
    const char *name;
} camsel;
static float g_center;

static int player_pos(sa1g_world *w, float p[3])
{
    uint32_t twp = r32(w, ADDR_PLAYER_TWP);
    if (!twp) {
        return 0;
    }
    p[0] = rf(w, twp + 0x20u);
    p[1] = rf(w, twp + 0x24u);
    p[2] = rf(w, twp + 0x28u);
    return 1;
}
static int lure_pos(sa1g_world *w, float p[3])
{
    uint32_t lt = r32(w, ADDR_LURE_TP), twp;
    if (!lt) {
        return 0;
    }
    twp = r32(w, lt + 0x20u);
    if (!twp) {
        return 0;
    }
    p[0] = rf(w, twp + 0x20u);
    p[1] = rf(w, twp + 0x24u);
    p[2] = rf(w, twp + 0x28u);
    return 1;
}
static int lure_mode(sa1g_world *w)
{
    uint32_t lt = r32(w, ADDR_LURE_TP);
    return lt ? (int)r8s(w, r32(w, lt + 0x20u)) : -1;
}

/* the tick's input: the stick, the camera yaw and (unless CAM_NONE) the host camera (host space) with the bit */
static int g_pair;
static void make_input(sa1_input *in, sa1g_world *w, const camsel *c, float sx, float sy, uint32_t b, float yaw, float hp[3])
{
    float p[3] = { 0.0f, 0.0f, 0.0f }, q[3], h[3], t[3];
    uint32_t k;
    memset(in, 0, sizeof *in);
    in->struct_size = sizeof *in;
    in->stick_x = sx;
    in->stick_y = sy;
    in->buttons = b;
    in->camera_yaw = yaw;
    if (c == NULL || c->mode == CAM_NONE) {
        return;
    }
    switch (c->mode) {
    case CAM_FIXED:
        memcpy(p, c->fixed, sizeof p);
        break;
    case CAM_MOVING:
        p[0] = 30.0f * sinf(0.05f * (float)g_pair);
        p[1] = 20.0f + 0.3f * (float)g_pair;
        p[2] = -50.0f + 0.7f * (float)g_pair;
        break;
    case CAM_CHASE: {
        double pr = c->pitch_deg * PI / 180.0, f[3];
        if (!player_pos(w, q)) {
            return;
        }
        f[0] = cos(pr) * cos((double)yaw);
        f[1] = -sin(pr);
        f[2] = -cos(pr) * sin((double)yaw);
        p[0] = (float)(q[0] - c->dist_hu / 4.0 * f[0]);
        p[1] = (float)(q[1] + g_center + c->height_hu / 4.0 - c->dist_hu / 4.0 * f[1]);
        p[2] = (float)(q[2] - c->dist_hu / 4.0 * f[2]);
        break;
    }
    default:
        if (!lure_pos(w, q) && !player_pos(w, q)) {
            return;
        }
        p[0] = q[0];
        p[1] = q[1] + (c->mode == CAM_LURE15 ? 15.0f : 0.0f);
        p[2] = q[2] + (c->mode == CAM_FAR2000 ? 2000.0f : 0.0f);
        break;
    }
    to_host(p, h);
    for (k = 0; k < 3; k++) {
        t[k] = h[k] + (k == 0 ? 100.0f : 0.0f);
    }
    memcpy(in->host_cam_pos, h, sizeof h);
    memcpy(in->host_cam_target, t, sizeof t);
    in->host_flags = SA1_INPUT_HOST_CAMERA;
    if (hp != NULL) {
        memcpy(hp, h, sizeof h);
    }
    switch (c->bad) {
    case BAD_NAN:
    case BAD_INF:
    case BAD_BIG: {
        float v = c->bad == BAD_NAN ? NAN : c->bad == BAD_INF ? INFINITY : 1e31f;
        if (c->bad_k < 3) {
            in->host_cam_pos[c->bad_k] = v;
        } else {
            in->host_cam_target[c->bad_k - 3] = v;
        }
        break;
    }
    case BAD_NOTAIL:
        in->struct_size = (uint32_t)offsetof(sa1_input, host_cam_pos);
        break;
    case BAD_NOBIT:
        in->host_flags = 0;
        break;
    default:
        break;
    }
}

static int tick_cam(sa1_context *ctx, sa1_character_id id, sa1g_world *w, const camsel *c, float sx, float sy, uint32_t b,
                    float yaw, float hp[3])
{
    sa1_input in;
    make_input(&in, w, c, sx, sy, b, yaw, hp);
    if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
        printf("    tick: %s\n", sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

/* the conversion the world does, computed inside the world's FP mode (sa1w_set_point, the SET layer's arithmetic) */
typedef struct conv_job { float h[3], s[3]; } conv_job;
static void conv_cb(struct sa1g_world *w, void *user)
{
    conv_job *j = (conv_job *)user;
    (void)w;
    sa1w_set_point(&g_co, j->h, j->s);
}

static void cam_twp_pos(sa1g_world *w, float p[3])
{
    uint32_t ct = r32(w, ADDR_CAM_TWP);
    p[0] = rf(w, ct + 0x20u);
    p[1] = rf(w, ct + 0x24u);
    p[2] = rf(w, ct + 0x28u);
}

/* ---- C1 / C1b: Big's line ---------------------------------------------------------------------------------------- */
typedef struct line_stat { int n, a_lure, amin, amax, bad; } line_stat;
static void line_check(sa1_context *ctx, sa1_character_id id, sa1g_world *w, line_stat *o)
{
    const sa1_render *r = NULL;
    float cp[3];
    uint32_t i;
    memset(o, 0, sizeof *o);
    o->amin = 999;
    o->amax = -1;
    o->a_lure = -1;
    cam_twp_pos(w, cp);
    if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || !r) {
        return;
    }
    for (i = 0; i < r->num_line_vertices; i++) {
        float s[3], d2;
        int a = r->line_vertices[i].color[3], e;
        to_sa1(r->line_vertices[i].position, s);
        d2 = (s[0] - cp[0]) * (s[0] - cp[0]) + (s[1] - cp[1]) * (s[1] - cp[1]) + (s[2] - cp[2]) * (s[2] - cp[2]);
        e = 255 - (int)(d2 * 0.25f);
        e = e > 0xE0 ? 0xE0 : e < 0x40 ? 0x40 : e;
        o->bad += abs(e - a) > 1;
        o->amin = a < o->amin ? a : o->amin;
        o->amax = a > o->amax ? a : o->amax;
        if (i + 1 == r->num_line_vertices) {
            o->a_lure = a;
        }
    }
    o->n = (int)r->num_line_vertices;
}

static uint32_t g_det_line = 2166136261u, g_det_cam = 2166136261u;

static void c1_run(sa1_context *ctx, const camsel *c, int *wet, int *lmin, int *amin, int *amax, int *bad, float eye0[3])
{
    sa1_character_id id = make_char(ctx, SA1_CHAR_BIG, NULL);
    sa1g_world *w = sa1_character_world(ctx, id);
    sa1_physics ph;
    int p;
    *wet = 0;
    *lmin = 999;
    *amin = 999;
    *amax = -1;
    *bad = 0;
    memset(&ph, 0, sizeof ph);
    sa1_physics_get(ctx, SA1_CHAR_BIG, &ph);
    g_center = ph.center_height;
    for (p = 0; p < 200; p++) {
        float sy = 0.0f;
        uint32_t b = 0;
        line_stat ls;
        if (p >= 10 && p < 50) {
            b = SA1_BUTTON_ACTION;
        }
        if (p >= 25 && p < 50) {
            sy = 1.0f;
        }
        g_pair = p;
        if (tick_cam(ctx, id, w, c, 0.0f, sy, b, 0.0f, NULL)) {
            break;
        }
        if (p == 2 && eye0 != NULL) {
            cam_twp_pos(w, eye0);
        }
        line_check(ctx, id, w, &ls);
        *bad += ls.bad;
        if (ls.n > 0 && (r16(w, ADDR_BIGBITS) & 0x40u) && lure_mode(w) == 5) {
            (*wet)++;
            *lmin = ls.a_lure < *lmin ? ls.a_lure : *lmin;
            *amin = ls.amin < *amin ? ls.amin : *amin;
            *amax = ls.amax > *amax ? ls.amax : *amax;
            g_det_line = fnv(g_det_line, &ls.a_lure, sizeof ls.a_lure);
        }
    }
    sa1_character_destroy(ctx, id);
}

static void t_c1(const char *gdi)
{
    sa1_context *ctx = make_ctx(gdi, 1);
    camsel cl, cf, cn;
    int wet, lmin, amin, amax, bad;
    float eye0[3] = { 0, 0, 0 }, bp[3];
    if (!ctx) {
        CHECK(0, "C1 context");
        return;
    }
    printf("\n== C1 / C1b: Big's fishing line (review_probe's island, the cast: action 10-49, stick forward 25-49)\n");
    memset(&cl, 0, sizeof cl);
    cl.mode = CAM_LURE15;
    c1_run(ctx, &cl, &wet, &lmin, &amin, &amax, &bad, NULL);
    CHECK(wet > 50 && lmin >= 0x9B && bad == 0, "C1 the camera 15 units above the lure: %d pairs in the water, the lure end "
          ">= 0x9B on every one (min %d), every vertex the game's formula against camera_twp->pos (%d off)", wet, lmin, bad);
    memset(&cf, 0, sizeof cf);
    cf.mode = CAM_FAR2000;
    c1_run(ctx, &cf, &wet, &lmin, &amin, &amax, &bad, NULL);
    CHECK(wet > 50 && amin == 0x40 && amax == 0x40 && bad == 0, "C1b the camera 2,000 units away: every vertex 0x40 (%d..%d, %d "
          "pairs, %d off)", amin, amax, wet, bad);
    memset(&cn, 0, sizeof cn);
    cn.mode = CAM_NONE;
    c1_run(ctx, &cn, &wet, &lmin, &amin, &amax, &bad, eye0);
    {
        sa1_character_id id = make_char(ctx, SA1_CHAR_BIG, NULL);
        sa1g_world *w = sa1_character_world(ctx, id);
        tick_cam(ctx, id, w, &cn, 0, 0, 0, 0.0f, NULL);
        tick_cam(ctx, id, w, &cn, 0, 0, 0, 0.0f, NULL);
        tick_cam(ctx, id, w, &cn, 0, 0, 0, 0.0f, NULL);
        player_pos(w, bp);
        cam_twp_pos(w, eye0);
        sa1_character_destroy(ctx, id);
    }
    CHECK(wet > 50 && amin == 0x40 && amax == 0x40 && fabsf(eye0[0] - (bp[0] - 40.0f)) < 1e-3f &&
          fabsf(eye0[1] - (bp[1] + 7.0f)) < 1e-3f && fabsf(eye0[2] - bp[2]) < 1e-3f,
          "C1b without the bit: the camera taskwk is the virtual eye (%.2f %.2f %.2f) = Big (%.2f %.2f %.2f) + (-40, 7, 0), "
          "every vertex 0x40 (%d..%d)", (double)eye0[0], (double)eye0[1], (double)eye0[2], (double)bp[0], (double)bp[1],
          (double)bp[2], amin, amax);
    sa1_shutdown(ctx);
}

/* ---- C2: the taskwk and the view ----------------------------------------------------------------------------------- */
static void t_c2(const char *gdi)
{
    sa1_context *ctx = make_ctx(gdi, 0);
    sa1_character_id a, b;
    sa1g_world *wa, *wb;
    camsel cm, cn;
    int p, posbad = 0, angbad = 0, xbad = 0, stbad = 0;
    if (!ctx) {
        CHECK(0, "C2 context");
        return;
    }
    printf("\n== C2: the camera taskwk with a moving host camera (Sonic, 100 pairs)\n");
    a = make_char(ctx, SA1_CHAR_SONIC, NULL);
    b = make_char(ctx, SA1_CHAR_SONIC, NULL);
    wa = sa1_character_world(ctx, a);
    wb = sa1_character_world(ctx, b);
    memset(&cm, 0, sizeof cm);
    cm.mode = CAM_MOVING;
    memset(&cn, 0, sizeof cn);
    for (p = 0; p < 100; p++) {
        float h[3], yaw = 0.01f * (float)p, cp[3];
        conv_job j;
        uint32_t ct, k, want_y;
        sa1_state_ex sa, sb;
        g_pair = p;
        tick_cam(ctx, a, wa, &cm, 0.0f, p >= 10 ? 1.0f : 0.0f, 0u, yaw, h);
        tick_cam(ctx, b, wb, &cn, 0.0f, p >= 10 ? 1.0f : 0.0f, 0u, yaw, NULL);
        memcpy(j.h, h, sizeof h);
        sa1g_world_call(wa, conv_cb, &j);
        cam_twp_pos(wa, cp);
        posbad += memcmp(cp, j.s, sizeof cp) != 0;
        g_det_cam = fnv(g_det_cam, cp, sizeof cp);
        ct = r32(wa, ADDR_CAM_TWP);
        want_y = (uint32_t)((int64_t)floor((double)yaw / (2.0 * PI) * 65536.0 + 0.5) - 0x4000) & 0xFFFFu;
        angbad += (r32(wa, ct + 0x14u) & 0xFFFFu) != 0 || (r32(wa, ct + 0x18u) & 0xFFFFu) != want_y ||
                  (r32(wa, ct + 0x1Cu) & 0xFFFFu) != 0 || r8s(wa, ct + 1u) != 0;
        for (k = 0; k < 16; k++) {
            xbad += memcmp(&wa->xmtrx[k], &wb->xmtrx[k], 4) != 0;
        }
        memset(&sa, 0, sizeof sa);
        memset(&sb, 0, sizeof sb);
        sa.struct_size = sb.struct_size = sizeof sa;
        sa1_character_state_ex(ctx, a, &sa);
        sa1_character_state_ex(ctx, b, &sb);
        stbad += memcmp(&sa, &sb, sizeof sa) != 0;
    }
    CHECK(posbad == 0, "camera_twp->pos = the host point converted (sa1w_set_point in the world's FP mode) bit for bit on every "
          "pair (%d differ)", posbad);
    CHECK(angbad == 0, "ang = (0, camera_yaw's BAMS - 0x4000, 0), smode 0 (%d pairs off)", angbad);
    CHECK(xbad == 0 && stbad == 0, "XMTRX bit-equal to a world without the camera (%d words off), the state too (%d pairs)",
          xbad, stbad);
    sa1_shutdown(ctx);
}

/* ---- C3: live play of six characters ------------------------------------------------------------------------------ */
#define D_PAIRS 300
typedef struct dsnap {
    sa1_state_ex s;
    uint32_t ev_n, ev_h, ntri, nspr, nline, h_tpos, h_tcol, h_spr, h_line;
} dsnap;
static void d_script(int i, float *sx, float *sy, uint32_t *b, float *yaw)
{
    *sx = 0;
    *sy = 0;
    *b = 0;
    *yaw = 0.004f * (float)i;
    if (i >= 10 && i < 70) *sy = 1.0f;
    else if (i >= 70 && i < 100) { *sx = 0.8f; *sy = 0.6f; }
    else if (i >= 100 && i < 104) { *sy = 1.0f; *b = SA1_BUTTON_JUMP; }
    else if (i >= 104 && i < 140) *sy = 1.0f;
    else if (i >= 140 && i < 151) *b = SA1_BUTTON_ACTION;
    else if (i >= 151 && i < 200) { *sx = -0.6f; *sy = 0.8f; }
    else if (i >= 200 && i < 261) { *sy = 0.5f; if (i % 10 < 2) *b = SA1_BUTTON_ACTION; }
    else if (i >= 261) { *sy = 1.0f; if (i == 270 || i == 271 || i == 280 || i == 281) *b = SA1_BUTTON_JUMP; }
}
static void d_run(sa1_context *ctx, int32_t kind, const camsel *cam, dsnap *out)
{
    sa1_character_id id = make_char(ctx, kind, NULL);
    sa1g_world *w = sa1_character_world(ctx, id);
    sa1_physics ph;
    int i;
    memset(&ph, 0, sizeof ph);
    sa1_physics_get(ctx, kind, &ph);
    g_center = ph.center_height;
    for (i = 0; i < D_PAIRS; i++) {
        float sx, sy, yaw;
        uint32_t b, n = 0, k;
        sa1_event ev[256];
        const sa1_render *r = NULL;
        dsnap *o = &out[i];
        d_script(i, &sx, &sy, &b, &yaw);
        g_pair = i;
        memset(o, 0, sizeof *o);
        if (tick_cam(ctx, id, w, cam, sx, sy, b, yaw, NULL)) {
            break;
        }
        o->s.struct_size = sizeof o->s;
        sa1_character_state_ex(ctx, id, &o->s);
        o->ev_h = 2166136261u;
        while (sa1_poll_events(ctx, ev, 256, &n) == SA1_OK && n > 0) {
            for (k = 0; k < n; k++) {
                ev[k].character = 0;
                o->ev_h = fnv(o->ev_h, &ev[k], sizeof ev[k]);
            }
            o->ev_n += n;
            n = 0;
        }
        if (sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK && r) {
            o->ntri = r->num_triangles;
            o->nspr = r->num_sprites;
            o->nline = r->num_line_vertices;
            o->h_tpos = fnv(2166136261u, r->positions, (size_t)r->num_triangles * 36u);
            o->h_tcol = fnv(2166136261u, r->colors, (size_t)r->num_triangles * 12u);
            o->h_spr = fnv(2166136261u, r->sprites, (size_t)r->num_sprites * sizeof *r->sprites);
            o->h_line = fnv(2166136261u, r->line_vertices, (size_t)r->num_line_vertices * sizeof *r->line_vertices);
        }
    }
    sa1_character_destroy(ctx, id);
}
static int d_diff(const dsnap *a, const dsnap *b)
{
    int i, n = 0;
    for (i = 0; i < D_PAIRS; i++) {
        dsnap x = a[i], y = b[i];
        n += memcmp(&x.s, &y.s, sizeof x.s) != 0 || x.ev_n != y.ev_n || x.ev_h != y.ev_h || x.ntri != y.ntri ||
             x.h_tpos != y.h_tpos || x.h_tcol != y.h_tcol || x.nspr != y.nspr || x.h_spr != y.h_spr ||
             x.nline != y.nline || x.h_line != y.h_line;
    }
    return n;
}
static void t_c3(const char *gdi)
{
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA, SA1_CHAR_BIG };
    static const char *const names[] = { "Sonic", "Tails", "Knuckles", "Amy", "E-102", "Big" };
    static dsnap A[D_PAIRS], B[D_PAIRS], C[D_PAIRS];
    sa1_context *ctx = make_ctx(gdi, 0);
    camsel ce, cf, cc;
    int k;
    if (!ctx) {
        CHECK(0, "C3 context");
        return;
    }
    printf("\n== C3: live play unchanged (300 pairs each: no camera / a far camera / the owner's chase camera 60 / 6)\n");
    memset(&ce, 0, sizeof ce);
    memset(&cf, 0, sizeof cf);
    memset(&cc, 0, sizeof cc);
    cf.mode = CAM_FIXED;
    cf.fixed[0] = 3000.0f;
    cf.fixed[1] = 800.0f;
    cf.fixed[2] = -2500.0f;
    cc.mode = CAM_CHASE;
    cc.dist_hu = 60;
    cc.height_hu = 6;
    for (k = 0; k < 6; k++) {
        int df, dc;
        d_run(ctx, kinds[k], &ce, A);
        d_run(ctx, kinds[k], &cf, B);
        d_run(ctx, kinds[k], &cc, C);
        df = d_diff(A, B);
        dc = d_diff(A, C);
        CHECK(df == 0 && dc == 0, "%s: %d / %d pairs differ (far / chase) in sa1_state_ex, the events and the render arrays",
              names[k], df, dc);
    }
    sa1_shutdown(ctx);
}

/* ---- C4: the SET spawner's centre while Big fishes --------------------------------------------------------------------- */
static void hold_fish(struct sa1g_world *w, int32_t sub)
{
    (void)sub;
    w16(w, ADDR_BIGBITS, r16(w, ADDR_BIGBITS) | 0x40u);
}
static uint32_t set_rec(sa1_context *ctx, float x, float y, float z)
{
    sa1_set_record r;
    float s[3] = { x, y, z };
    uint32_t id = 0;
    memset(&r, 0, sizeof r);
    r.struct_size = sizeof r;
    r.type = SA1_OBJTYPE_RING;
    to_host(s, r.position);
    sa1_set_add(ctx, &r, &id);
    return id;
}
static int is_live(sa1_context *ctx, sa1_character_id ch, uint32_t rec)
{
    uint32_t ids[64], n = 0, i;
    if (sa1_set_live(ctx, ch, ids, 64, &n) != SA1_OK) {
        return 0;
    }
    for (i = 0; i < n && i < 64; i++) {
        if (ids[i] == rec) {
            return 1;
        }
    }
    return 0;
}
static void t_c4(const char *gdi)
{
    sa1_context *ctx = make_ctx(gdi, 0);
    int pass;
    const float bx = 1500.0f;
    if (!ctx) {
        CHECK(0, "C4 context");
        return;
    }
    printf("\n== C4: the SET spawner's centre while Big's lure is in the water (the bit held: set_test (11)'s hook)\n");
    for (pass = 0; pass < 2; pass++) {
        float sp[3] = { bx, 0.5f, 0.0f };
        sa1_character_id b = make_char(ctx, SA1_CHAR_BIG, sp);
        sa1g_world *w = sa1_character_world(ctx, b);
        camsel c;
        uint32_t r1, r2, i;
        memset(&c, 0, sizeof c);
        c.mode = pass == 0 ? CAM_FIXED : CAM_NONE;
        c.fixed[0] = bx - 450.0f;
        c.fixed[1] = 6.0f;
        c.fixed[2] = 0.0f;
        sa1w_set_world_test(w, 0, hold_fish);
        r1 = set_rec(ctx, bx - 420.0f, 6.0f, 0.0f);      /* 30 from the host camera, 420 from Big */
        r2 = set_rec(ctx, bx + 30.0f, 6.0f, 0.0f);       /* 30 from Big, 480 from the host camera */
        for (i = 0; i < 6; i++) {
            tick_cam(ctx, b, w, &c, 0, 0, 0, 0.0f, NULL);
        }
        if (pass == 0) {
            CHECK(is_live(ctx, b, r1) && !is_live(ctx, b, r2), "with the host camera: the RING 30 units from it (420 from Big) "
                  "spawns, the one 30 from Big (480 from the camera) does not (live %d / %d)", is_live(ctx, b, r1),
                  is_live(ctx, b, r2));
        } else {
            CHECK(is_live(ctx, b, r1) && is_live(ctx, b, r2), "without it: the virtual eye 40 behind Big is the centre, both spawn "
                  "(0.9.4f; live %d / %d)", is_live(ctx, b, r1), is_live(ctx, b, r2));
        }
        sa1w_set_world_test(w, 0, NULL);
        sa1_set_remove(ctx, r1);
        sa1_set_remove(ctx, r2);
        sa1_character_destroy(ctx, b);
    }
    sa1_shutdown(ctx);
}

/* ---- C5: the board --------------------------------------------------------------------------------------------------- */
static void b_run(sa1_context *ctx, const camsel *c, float yaw, sa1_state_ex *out, float *p60)
{
    sa1_character_id id = make_char(ctx, SA1_CHAR_SONIC, NULL);
    sa1g_world *w = sa1_character_world(ctx, id);
    sa1_board_desc d;
    int p;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.on = SA1_BOARD_ON;
    for (p = 0; p < 200; p++) {
        float sx = (p >= 100 && p < 130) ? 0.7f : 0.0f, sy = p >= 30 ? 1.0f : 0.0f;
        uint32_t b = p == 150 ? SA1_BUTTON_JUMP : 0u;
        if (p == 10) {
            sa1_character_board(ctx, id, &d);
        }
        g_pair = p;
        tick_cam(ctx, id, w, c, sx, sy, b, yaw, NULL);
        memset(&out[p], 0, sizeof out[p]);
        out[p].struct_size = sizeof out[p];
        sa1_character_state_ex(ctx, id, &out[p]);
        if (p == 60 && p60) {
            *p60 = out[p].player_speed[0];
        }
    }
    sa1_character_destroy(ctx, id);
}
static void t_c5(const char *gdi)
{
    static sa1_state_ex A[200], B[200], C[200], D[200];
    sa1_context *ctx = make_ctx(gdi, 0);
    camsel ce, cf, cc;
    int p, df = 0, dc = 0, dy = 0;
    float v0 = 0, v180 = 0;
    if (!ctx) {
        CHECK(0, "C5 context");
        return;
    }
    printf("\n== C5: the board (sa1_character_board at pair 10, stick forward from 30, right 0.7 at 100-129, jump at 150)\n");
    memset(&ce, 0, sizeof ce);
    memset(&cf, 0, sizeof cf);
    memset(&cc, 0, sizeof cc);
    cf.mode = CAM_FIXED;
    cf.fixed[0] = 3000.0f;
    cf.fixed[1] = 800.0f;
    cf.fixed[2] = -2500.0f;
    cc.mode = CAM_CHASE;
    cc.dist_hu = 60;
    cc.height_hu = 6;
    b_run(ctx, &ce, 0.0f, A, &v0);
    b_run(ctx, &cf, 0.0f, B, NULL);
    b_run(ctx, &cc, 0.0f, C, NULL);
    b_run(ctx, &ce, (float)PI, D, &v180);
    for (p = 0; p < 200; p++) {
        df += memcmp(&A[p], &B[p], sizeof A[p]) != 0;
        dc += memcmp(&A[p], &C[p], sizeof A[p]) != 0;
        dy += memcmp(&A[p], &D[p], sizeof A[p]) != 0;
    }
    CHECK(df == 0 && dc == 0 && (A[20].flags & SA1_STATE_ON_BOARD), "0 / 200 pairs differ with a far (%d) or a chase (%d) "
          "camera (riding: %s)", df, dc, (A[20].flags & SA1_STATE_ON_BOARD) ? "yes" : "no");
    CHECK(fabsf(v180 - 0.460f) < 0.0015f && fabsf(v0 - 1.000f) < 0.001f, "camera yaw 180: pspd.x %.3f at pair 60 (0.460; yaw 0: "
          "%.3f), %d pairs differ", (double)v180, (double)v0, dy);
    sa1_shutdown(ctx);
}

/* ---- C6: invalid input ------------------------------------------------------------------------------------------- */
static uint32_t c6_run(sa1_context *ctx, const camsel *c)
{
    sa1_character_id id = make_char(ctx, SA1_CHAR_SONIC, NULL);
    sa1g_world *w = sa1_character_world(ctx, id);
    uint32_t h;
    int p;
    for (p = 0; p < 60; p++) {
        g_pair = p;
        tick_cam(ctx, id, w, c, 0.0f, p >= 10 ? 1.0f : 0.0f, 0u, 0.02f * (float)p, NULL);
    }
    h = fnv(2166136261u, w->ram, SA1G_ARENA_SIZE);
    sa1_character_destroy(ctx, id);
    return h;
}
static void t_c6(const char *gdi)
{
    sa1_context *ctx = make_ctx(gdi, 0);
    camsel c;
    uint32_t ref, h, k, bad = 0, n = 0, valid;
    int kind;
    if (!ctx) {
        CHECK(0, "C6 context");
        return;
    }
    printf("\n== C6: invalid camera input leaves the world as without a camera\n");
    memset(&c, 0, sizeof c);
    ref = c6_run(ctx, &c);
    c.mode = CAM_MOVING;
    valid = c6_run(ctx, &c);
    for (kind = BAD_NAN; kind <= BAD_BIG; kind++) {
        for (k = 0; k < 6; k++) {
            c.bad = kind;
            c.bad_k = (int)k;
            h = c6_run(ctx, &c);
            bad += h != ref;
            n++;
        }
    }
    c.bad = BAD_NOTAIL;
    bad += c6_run(ctx, &c) != ref;
    n++;
    c.bad = BAD_NOBIT;
    bad += c6_run(ctx, &c) != ref;
    n++;
    CHECK(bad == 0 && valid != ref, "%u invalid inputs (NaN / inf / 1e31 in each float, the bit without the tail, the tail "
          "without the bit): the arena byte-identical to a run without a camera in all (%u differ; a valid camera differs: %s)",
          (unsigned)n, (unsigned)bad, valid != ref ? "yes" : "NO");
    sa1_shutdown(ctx);
}

/* ---- C8: rebuild ---------------------------------------------------------------------------------------------------- */
static void t_c8(const char *gdi)
{
    sa1_context *ctx = make_ctx(gdi, 0);
    sa1_character_id id;
    sa1g_world *w;
    camsel c;
    float h[3], cp[3];
    conv_job j;
    int p, ok = 1;
    if (!ctx) {
        CHECK(0, "C8 context");
        return;
    }
    printf("\n== C8: a world re-created mid-run\n");
    memset(&c, 0, sizeof c);
    c.mode = CAM_MOVING;
    id = make_char(ctx, SA1_CHAR_SONIC, NULL);
    w = sa1_character_world(ctx, id);
    for (p = 0; p < 30; p++) {
        g_pair = p;
        ok &= tick_cam(ctx, id, w, &c, 0, 1.0f, 0, 0.0f, NULL) == 0;
    }
    sa1_character_destroy(ctx, id);
    id = make_char(ctx, SA1_CHAR_SONIC, NULL);
    w = sa1_character_world(ctx, id);
    g_pair = 30;
    ok &= tick_cam(ctx, id, w, &c, 0, 1.0f, 0, 0.0f, h) == 0;
    w = sa1_character_world(ctx, id);
    memcpy(j.h, h, sizeof h);
    sa1g_world_call(w, conv_cb, &j);
    cam_twp_pos(w, cp);
    CHECK(ok && w != NULL && !w->dead && memcmp(cp, j.s, sizeof cp) == 0, "the camera is back at the first tick of the new "
          "world (%.2f %.2f %.2f), no fault", (double)cp[0], (double)cp[1], (double)cp[2]);
    sa1_character_destroy(ctx, id);
    sa1_shutdown(ctx);
}

/* ---- C9: order B of the SET window with the host eye (095-fix) ---------------------------------------------------- */
#define C9_N 10
typedef struct c9_sel { uint32_t w, n, f, b; float eye[3]; } c9_sel;
static void c9_run(sa1_context *ctx, const camsel *c, const uint32_t *W, const uint32_t *N, const uint32_t *F,
                   const uint32_t *B, c9_sel *o)
{
    float sp[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id s = make_char(ctx, SA1_CHAR_SONIC, sp);
    sa1g_world *w = sa1_character_world(ctx, s);
    uint32_t i;
    memset(o, 0, sizeof *o);
    for (i = 0; i < 8; i++) {
        g_pair = (int)i;
        tick_cam(ctx, s, w, c, 0.0f, 0.0f, 0u, 0.0f, NULL);
    }
    w = sa1_character_world(ctx, s);
    if (w != NULL) {
        cam_twp_pos(w, o->eye);
        for (i = 0; i < 4; i++) {
            o->w += sa1w_set_world_slot(w, W[i]) >= 0;
        }
        for (i = 0; i < C9_N; i++) {
            o->n += sa1w_set_world_slot(w, N[i]) >= 0;
            o->f += sa1w_set_world_slot(w, F[i]) >= 0;
            o->b += sa1w_set_world_slot(w, B[i]) >= 0;
        }
    }
    sa1_character_destroy(ctx, s);
}
static void t_c9(const char *gdi)
{
    static const float wq[4][2] = { { -200.0f, 0.0f }, { 0.0f, 250.0f }, { 0.0f, -250.0f }, { 300.0f, 0.0f } };
    sa1_context *ctx = make_ctx(gdi, 0);
    sa1_set_config sc;
    uint32_t W[4], N[C9_N], F[C9_N], B[C9_N], i;
    camsel cn, ch, cv;
    c9_sel a, h, v;
    if (!ctx) {
        CHECK(0, "C9 context");
        return;
    }
    printf("\n== C9: order B of the SET window (range 1600, ring_window 14) with the host eye\n");
    memset(&sc, 0, sizeof sc);
    sc.struct_size = sizeof sc;
    sc.ring_window = 4u + C9_N;
    sc.box_window = 32u;
    sc.margin = 100.0f;
    sc.list = SA1_SETLIST_LIBSA1;
    sc.range = 1600.0f;
    if (sa1_set_configure(ctx, &sc) != SA1_OK) {
        CHECK(0, "C9 sa1_set_configure: %s", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return;
    }
    for (i = 0; i < 4; i++) {
        W[i] = set_rec(ctx, wq[i][0], 6.0f, wq[i][1]);              /* within 400 units: tier 0 */
    }
    for (i = 0; i < C9_N; i++) {
        float z = (i & 1u) ? 60.0f : -60.0f;
        N[i] = set_rec(ctx, 500.0f + 50.0f * (float)i, 6.0f, z);    /* beyond 400, before x 1,000 */
        F[i] = set_rec(ctx, 1100.0f + 40.0f * (float)i, 6.0f, z);   /* beyond x 1,000 */
        B[i] = set_rec(ctx, -500.0f - 50.0f * (float)i, 6.0f, z);   /* behind Sonic */
    }
    memset(&cn, 0, sizeof cn);
    memset(&ch, 0, sizeof ch);
    memset(&cv, 0, sizeof cv);
    ch.mode = CAM_FIXED;
    ch.fixed[0] = 1000.0f;
    ch.fixed[1] = 60.0f;
    ch.fixed[2] = 0.0f;
    cv.mode = CAM_FIXED;
    cv.fixed[0] = -40.0f;                   /* the virtual eye's plane (SA1G_VIEW_DIST behind Sonic at yaw 0) */
    cv.fixed[1] = 300.0f;
    cv.fixed[2] = 0.0f;
    c9_run(ctx, &cn, W, N, F, B, &a);
    c9_run(ctx, &ch, W, N, F, B, &h);
    c9_run(ctx, &cv, W, N, F, B, &v);
    CHECK(a.w == 4u && a.n == C9_N && a.f == 0u && a.b == 0u, "C9 no host camera (the virtual eye %.1f %.1f %.1f): within 400 "
          "%u/4, tier 1 nearest first: N %u/%u selected, F %u, B %u", (double)a.eye[0], (double)a.eye[1], (double)a.eye[2],
          (unsigned)a.w, (unsigned)a.n, (unsigned)C9_N, (unsigned)a.f, (unsigned)a.b);
    CHECK(h.w == 4u && h.f == C9_N && h.n == 0u && h.b == 0u, "C9 the host eye at x 1,000 (camera_twp %.1f %.1f %.1f): within "
          "400 %u/4 (unchanged), F %u/%u selected before the nearer N (%u) - tier 1 is in front of the host eye's plane; B %u",
          (double)h.eye[0], (double)h.eye[1], (double)h.eye[2], (unsigned)h.w, (unsigned)h.f, (unsigned)C9_N, (unsigned)h.n,
          (unsigned)h.b);
    CHECK(v.w == a.w && v.n == a.n && v.f == a.f && v.b == a.b, "C9 the host eye on the virtual eye's plane, 300 units up "
          "(camera_twp %.1f %.1f %.1f): the selection of the run without a camera (W %u, N %u, F %u, B %u)",
          (double)v.eye[0], (double)v.eye[1], (double)v.eye[2], (unsigned)v.w, (unsigned)v.n, (unsigned)v.f, (unsigned)v.b);
    for (i = 0; i < 4; i++) {
        sa1_set_remove(ctx, W[i]);
    }
    for (i = 0; i < C9_N; i++) {
        sa1_set_remove(ctx, N[i]);
        sa1_set_remove(ctx, F[i]);
        sa1_set_remove(ctx, B[i]);
    }
    sa1_shutdown(ctx);
}

int main(int argc, char **argv)
{
    const char *gdi = NULL;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            g_only = argv[++i];
        } else {
            gdi = argv[i];
        }
    }
    if (gdi == NULL) {
        printf("usage: camera_test [--only C1,C2,...] GDI\n");
        return 2;
    }
    printf("camera_test: libsa1 %d.%d.%d, sizeof sa1_input %u (SA1_INPUT_HOST_CAMERA 0x%X)\n", SA1_VERSION_MAJOR,
           SA1_VERSION_MINOR, SA1_VERSION_PATCH, (unsigned)sizeof(sa1_input), (unsigned)SA1_INPUT_HOST_CAMERA);
    if (want("C1")) {
        t_c1(gdi);
    }
    if (want("C2")) {
        t_c2(gdi);
    }
    if (want("C3")) {
        t_c3(gdi);
    }
    if (want("C4")) {
        t_c4(gdi);
    }
    if (want("C5")) {
        t_c5(gdi);
    }
    if (want("C6")) {
        t_c6(gdi);
    }
    if (want("C8")) {
        t_c8(gdi);
    }
    if (want("C9")) {
        t_c9(gdi);
    }
    printf("DETERMINISM camera: taskwk %08X line %08X\n", (unsigned)g_det_cam, (unsigned)g_det_line);
    printf("\n%s: %d passed, %d failed\n", g_fail ? "FAIL" : "PASS", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
