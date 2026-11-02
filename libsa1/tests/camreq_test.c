/*
 * camreq_test.c - round 0.9.8 (098-lib-lookcam): the game's camera requests in libsa1 worlds (SA1_API_CAMERA: SA1_INPUT_GAME_CAMERA,
 * sa1_character_camera, SA1_EVENT_CAMERA; sadc docs/research/fish098.md 3, 6.1, gsa1_098_plan.md 6.3). Rewritten from the research
 * probe SADC/build/research/prep098/fish/fish_probe.c and camshim.c (which linked the staged C over an archive copy and simulated
 * the step in a lane hook); here everything goes through the real path: the landed C, seams S3 / S9, the public calls.
 *
 *   py -3.12 tests/build_camreq.py --run [--target all] [--debug] [-- --only T2,T3,...]      (SA1_TEST_GDI)
 *
 * API NOTES (gsa1_098_plan.md 3.2: the full notes live here)
 *   - The host sets SA1_INPUT_GAME_CAMERA in sa1_input.host_flags (the struct must cover host_flags). The first tick with the bit
 *     installs the step (w->camreq.pre, live worlds only; a follower id ignores the bit: seam S9). Without the bit nothing runs.
 *   - While a request LIB knows (8C0239C2 AIM, 8C0EAF28 LURE, 8C0EAA80 CATCH) runs, the camera taskwk IS the game camera: the host's
 *     camera_yaw and host_cam_pos do not reach it (they keep moving XMTRX, the virtual view). Any other request is reported, not run.
 *   - sa1_character_camera(ctx, id, alpha, out) is const; it never touches sa1_last_error. Valid between ticks. flags: ACTIVE (level > 0),
 *     KNOWN, RUNNING (position, forward, target valid), CUT (no interpolation: the output is the last drawn pair's). kind, level,
 *     adjust, function are the control work's as they are NOW; position, forward, target are the DRAWN pairs' (the camera at a pair's first
 *     step, the one the game draws), so after a switch the numbers lag the request by up to one pair. forward interpolates ang the
 *     shorter way at alpha; alpha < 0: the world's accumulator; NaN: 1. fov is written with or without RUNNING.
 *   - SA1_EVENT_CAMERA 17: id = function, arg = level | adjust << 8, on a change of function or level, seen at the next step with the bit.
 *     adjust 3 -> 0 (SLOW ends) alone is no event. E.g. the rod comes up: (8C0239C2, 2 | 3 << 8); lure in: (8C0EAF28, 4); end: (0, 0 | 21 << 8).
 *   - Refusals: NULL or struct_size below fov: SA1_ERR_INVALID_ARGUMENT; no character: SA1_ERR_NOT_FOUND; no world: SA1_ERR_NOT_LOADED;
 *     gate closed: SA1_ERR_UNSUPPORTED. A rebuild clears the request, the running flag and the drawn pairs (T11).
 *
 * Coordinates: the Source preset (host = 4 x (x, -z, y)), as G-SA1. Cases (fish098.md 6.1):
 *   T1   strict camera replay: the FISHCAM references in replay worlds with the step on: the taskwk after the camera step equals the record's
 *        px_cam_raw at every step it ran (0 of 293 / 523 / 623 differ); every trace record still bit-exact (replay_test's own gate)
 *   T2   the request log on the island through SA1_EVENT_CAMERA and sa1_character_camera
 *   T3   the numbers: FISHING 50.00 from Big + (0, 20, 0); lure camera 20.01-20.03 from the midpoint, 15.13-16.67 from the lure; moves;
 *        the SET spawner's centre (fish098.md 8.14)
 *   T4   the line: lure end 191..203, rod end 64, every vertex the game's formula against camera_twp->pos within 1
 *   T5   the seed: the first FISHING pair moved 0.7 from the host's eye, and from the virtual eye; equal from pair 40
 *   T6   bit off: six characters, cam_probe D's script, against hashes pinned from libsa1 0.3.0 itself (r098/lookcam/t6_pin.c, its archive
 *        and headers; sa1_state_ex at 0.3.0's struct_size 464); Big's cast without the bit: taskwk = the host's eye, line 64
 *   T7   bit on, six characters: the same pins (camera events apart): no known request ran
 *   T8   OTHER: Sonic, 8C021700 pushed and popped by a test call: ACTIVE, not KNOWN, not RUNNING, the same pins; board rides with and without
 *   T9   the stick's frame while aiming: camera_yaw fixed at a WRONG value: the cursor moves along the game camera's right, 5.93 per pair;
 *        bit off: along camera_yaw's
 *   T10  the ends: L + R, reel in, jump while aiming, dry ground, hurt, killed; after a normal end the next step's taskwk = the host's eye
 *   T11  a rebuild mid-cast (and a re-creation): no request afterward, the next cast works
 *   T12  interpolation at alpha 0 / 0.5 / 1 / < 0 / NaN; the CUT pair
 *   T13  invalid calls: no arena write
 *   T14  deep water: 3,000 pairs, the camera sinks with the lure, no fault
 *   T15  x86 = x64: the DETERMINISM lines (T1-T4's streams)
 *   T16  in tests/hud_test.c (part 13)
 */
#include "sa1_world.h"

/* T1: tests/replay_test.c's own replay loop calls sa1g_world_step after wr_inject (it does not go through wr_replay, where seam S12's wr_step_hook
 * sits): the same step point, by a wrapper of that one call; V6, its second run, V5 and DISC keep their strict record compare */
static sa1_status cq_step(sa1g_world *w, int32_t sub);
#define sa1g_world_step(w, sub) cq_step((w), (sub))
#define main cq_replay_main
#include "replay_test.c"
#undef main
#undef sa1g_world_step

#include <math.h>

#include "sa1_coladapt.h"

#define CQ_PI 3.14159265358979323846
#define CQ_ADDR_PLAYER_TWP 0x8C78C608u
#define CQ_ADDR_LURE_TP    0x8C7A40C8u
#define CQ_ADDR_CAM_TWP    0x8C18B7ECu
#define CQ_ADDR_BIGBITS    0x8C7A40E8u
#define CQ_ADDR_BPOS       0x8C6B6E68u
#define CQ_ADDR_WORK       0x8C6B6EECu
#define CQ_ADDR_CTRL_W     0x8C6B6F4Cu

void sa1g_camreq_pre(sa1g_world *w, int32_t sub, void *user);

static const char *cq_only;
static int cq_want(const char *name)
{
    const char *p;
    size_t n = strlen(name);
    if (cq_only == NULL) {
        return 1;
    }
    for (p = cq_only; *p;) {
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

static uint32_t cq_det[4] = { 2166136261u, 2166136261u, 2166136261u, 2166136261u };   /* T1, T2, T3, T4 streams */
static uint32_t cq_fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

static void cq_log(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN) {
        printf("    [libsa1] %s\n", msg);
    }
}
static uint8_t *cq_wp(sa1g_world *w, uint32_t g, uint32_t n)
{
    if (g == 0 || ((g >> 24) & 31u) != SA1G_SEG_ARENA || (g & 0xFFFFFFu) > SA1G_ARENA_SIZE - n) {
        return NULL;
    }
    return w->ram + (g & 0xFFFFFFu);
}
static uint32_t cq_r32(sa1g_world *w, uint32_t g) { uint32_t v = 0; uint8_t *p = cq_wp(w, g, 4); if (p) memcpy(&v, p, 4); return v; }
static uint32_t cq_r16(sa1g_world *w, uint32_t g) { uint16_t v = 0; uint8_t *p = cq_wp(w, g, 2); if (p) memcpy(&v, p, 2); return v; }
static int32_t cq_r8s(sa1g_world *w, uint32_t g) { uint8_t *p = cq_wp(w, g, 1); return p ? (int8_t)*p : 0; }
static float cq_rf(sa1g_world *w, uint32_t g) { float v = 0; uint8_t *p = cq_wp(w, g, 4); if (p) memcpy(&v, p, 4); return v; }
static void cq_rv(sa1g_world *w, uint32_t g, float o[3]) { o[0] = cq_rf(w, g); o[1] = cq_rf(w, g + 4); o[2] = cq_rf(w, g + 8); }
static double cq_dist(const float a[3], const float b[3])
{
    return sqrt(((double)a[0] - b[0]) * ((double)a[0] - b[0]) + ((double)a[1] - b[1]) * ((double)a[1] - b[1]) + ((double)a[2] - b[2]) * ((double)a[2] - b[2]));
}
/* SA1_COORDS_PRESET_SOURCE: host = 4 * (x, -z, y) */
static void cq_to_sa1(const float h[3], float s[3]) { s[0] = h[0] * 0.25f; s[1] = h[2] * 0.25f; s[2] = -h[1] * 0.25f; }
static void cq_to_host(const float s[3], float h[3]) { h[0] = s[0] * 4.0f; h[1] = -s[2] * 4.0f; h[2] = s[1] * 4.0f; }

typedef struct cq_tris { sa1_collision_tri *t; uint32_t n, cap; } cq_tris;
static void cq_tri(cq_tris *b, const float a[3], const float c[3], const float d[3], uint32_t surf)
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        uint32_t cap = b->cap ? 2u * b->cap : 1024u;
        sa1_collision_tri *nt = (sa1_collision_tri *)realloc(b->t, cap * sizeof *nt);
        if (!nt) return;
        b->t = nt; b->cap = cap;
    }
    t = &b->t[b->n++];
    memcpy(t->v[0], a, 12); memcpy(t->v[1], c, 12); memcpy(t->v[2], d, 12);
    t->surface = surf;
}
static void cq_rect(cq_tris *b, float x0, float y0, float x1, float y1, float z, float tile, uint32_t surf)
{
    float x, y;
    for (x = x0; x < x1 - 0.01f; x += tile) {
        for (y = y0; y < y1 - 0.01f; y += tile) {
            float xe = x + tile < x1 ? x + tile : x1, ye = y + tile < y1 ? y + tile : y1;
            float a[3] = { x, y, z }, c[3] = { xe, y, z }, d[3] = { xe, ye, z }, e[3] = { x, ye, z };
            cq_tri(b, a, c, d, surf);
            cq_tri(b, a, d, e, surf);
        }
    }
}
/* ground 0: flat SOLID; 1: review_probe's island (600 HU square at z 0, water at -20 HU, sea bed -600) */
static sa1_context *cq_make_ctx(const char *gdi, int ground, uint32_t kinds)
{
    sa1_context *ctx = NULL; sa1_host h; sa1_load_options lo; sa1_coords co; cq_tris b;
    sa1_host_defaults(&h); h.log = cq_log; h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) return NULL;
    memset(&co, 0, sizeof co); co.struct_size = sizeof co; sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co); sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo); lo.struct_size = sizeof lo;
    lo.characters = kinds;
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_WORLD_IMAGES;
    if (SA1_FAILED(sa1_load_disc(ctx, gdi, &lo))) { printf("load_disc: %s\n", sa1_last_error(ctx)); sa1_shutdown(ctx); return NULL; }
    memset(&b, 0, sizeof b);
    if (ground == 1) {
        cq_rect(&b, -300, -300, 300, 300, 0, 300, SA1_SURFACE_SOLID);
        cq_rect(&b, -6000, -6000, 6000, -300, -20, 1000, SA1_SURFACE_WATER);
        cq_rect(&b, -6000, 300, 6000, 6000, -20, 1000, SA1_SURFACE_WATER);
        cq_rect(&b, -6000, -300, -300, 300, -20, 600, SA1_SURFACE_WATER);
        cq_rect(&b, 300, -300, 6000, 300, -20, 600, SA1_SURFACE_WATER);
        cq_rect(&b, -6000, -6000, 6000, 6000, -600, 1000, SA1_SURFACE_SOLID);
    } else {
        cq_rect(&b, -8192, -8192, 8192, 8192, 0, 1024, SA1_SURFACE_SOLID);
    }
    if (SA1_FAILED(sa1_collision_set_static(ctx, b.t, b.n))) { printf("collision: %s\n", sa1_last_error(ctx)); free(b.t); sa1_shutdown(ctx); return NULL; }
    free(b.t);
    return ctx;
}
static sa1_character_id cq_make_char(sa1_context *ctx, int32_t kind)
{
    sa1_character_desc d; sa1_character_id id = 0;
    memset(&d, 0, sizeof d); d.struct_size = sizeof d; d.kind = kind; d.flags = SA1_CREATE_WORLD;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) { printf("    create: %s\n", sa1_last_error(ctx)); return 0; }
    return id;
}

/* a host camera: G-SA1's CalcView (cl_character.lua) in SA1 space, no wall trace: open ground */
typedef struct cq_cam { int on; float dist_hu, height_hu, pitch_deg; const char *name; } cq_cam;
static const cq_cam CQ_CHASE = { 1, 60, 6, 10, "the owner's chase camera 60 / 6 (pitch 10)" };
static const cq_cam CQ_DEFAULT = { 1, 150, 20, 15, "G-SA1's default 150 / 20 (pitch 15)" };
static const cq_cam CQ_NONE = { 0, 0, 0, 0, "no host camera (the virtual eye)" };
static float cq_center = 8.0f;
static float cq_last_eye[3];    /* SA1 space: the last eye passed */

static int cq_tick(sa1_context *ctx, sa1_character_id id, sa1g_world *w, const cq_cam *hc, float sx, float sy, uint32_t buttons, float yaw, int bit)
{
    sa1_input in;
    memset(&in, 0, sizeof in); in.struct_size = sizeof in;
    in.stick_x = sx; in.stick_y = sy; in.buttons = buttons; in.camera_yaw = yaw;
    if (bit) {
        in.host_flags |= SA1_INPUT_GAME_CAMERA;
    }
    if (hc && hc->on) {
        uint32_t twp = cq_r32(w, CQ_ADDR_PLAYER_TWP);
        float q[3] = { 0, 0, 0 }, e[3], h[3], t[3], f[3];
        double pr = hc->pitch_deg * CQ_PI / 180.0;
        if (twp) cq_rv(w, twp + 0x20u, q);
        f[0] = (float)(cos(pr) * cos((double)yaw)); f[1] = (float)(-sin(pr)); f[2] = (float)(-cos(pr) * sin((double)yaw));
        e[0] = q[0] - hc->dist_hu / 4.0f * f[0];
        e[1] = q[1] + cq_center + hc->height_hu / 4.0f - hc->dist_hu / 4.0f * f[1];
        e[2] = q[2] - hc->dist_hu / 4.0f * f[2];
        memcpy(cq_last_eye, e, sizeof e);
        cq_to_host(e, h);
        t[0] = e[0] + 25.0f * f[0]; t[1] = e[1] + 25.0f * f[1]; t[2] = e[2] + 25.0f * f[2];
        memcpy(in.host_cam_pos, h, 12);
        cq_to_host(t, h);
        memcpy(in.host_cam_target, h, 12);
        in.host_flags |= SA1_INPUT_HOST_CAMERA;
    }
    if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
        printf("    tick: %s\n", sa1_last_error(ctx));
        return -1;
    }
    return 0;
}
static int cq_lure_mode(sa1g_world *w)
{
    uint32_t lt = cq_r32(w, CQ_ADDR_LURE_TP);
    return lt ? (int)cq_r8s(w, cq_r32(w, lt + 0x20u)) : -1;
}
static int cq_big_mode(sa1g_world *w)
{
    uint32_t twp = cq_r32(w, CQ_ADDR_PLAYER_TWP);
    return twp ? (int)cq_r8s(w, twp) : -1;
}
typedef struct cq_req { int level, adjust; uint32_t fn; } cq_req;
static void cq_get_req(sa1g_world *w, cq_req *o)
{
    o->level = cq_r8s(w, CQ_ADDR_CTRL_W + 8u);
    o->adjust = cq_r8s(w, CQ_ADDR_CTRL_W + 9u);
    o->fn = cq_r32(w, CQ_ADDR_CTRL_W + 12u);
}
typedef struct cq_line { int n, a_rod, a_lure, bad; } cq_line;
static void cq_line_check(sa1_context *ctx, sa1_character_id id, sa1g_world *w, cq_line *o)
{
    const sa1_render *r = NULL;
    uint32_t ct = cq_r32(w, CQ_ADDR_CAM_TWP), i;
    float cp[3];
    memset(o, 0, sizeof *o);
    o->a_rod = o->a_lure = -1;
    cq_rv(w, ct + 0x20u, cp);
    if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || !r) {
        return;
    }
    for (i = 0; i < r->num_line_vertices; i++) {
        float s[3], d2;
        int a = r->line_vertices[i].color[3];
        cq_to_sa1(r->line_vertices[i].position, s);
        (void)d2;
        if (i == 0) o->a_rod = a;
        if (i + 1 == r->num_line_vertices) o->a_lure = a;
    }
    o->n = (int)r->num_line_vertices;
}
/* camera ang.y = yaw BAMS - 0x4000 (sa1_api_world.c): the host yaw that gives the camera's own frame */
static float cq_yaw_of(sa1g_world *w)
{
    uint32_t ct = cq_r32(w, CQ_ADDR_CAM_TWP);
    uint32_t a = (cq_r32(w, ct + 0x18u) + 0x4000u) & 0xFFFFu;
    double y = (double)a / 65536.0 * 2.0 * CQ_PI;
    if (y > CQ_PI) y -= 2.0 * CQ_PI;
    return (float)y;
}
/* bigfish 2.4's inputs: 10 pairs idle, action held 40 pairs (stick forward from 25), release, 150 idle, L + R 20, 40 idle */
static void cq_script(int p, float *sx, float *sy, uint32_t *b)
{
    *sx = 0; *sy = 0; *b = 0;
    if (p >= 10 && p < 50) *b = SA1_BUTTON_ACTION;
    if (p >= 25 && p < 50) *sy = 1.0f;
    if (p >= 200 && p < 220) *b = SA1_BUTTON_L | SA1_BUTTON_R;
}
static int cq_state(const sa1_context *ctx, sa1_character_id id, float alpha, sa1_camera_state *st)
{
    memset(st, 0, sizeof *st);
    st->struct_size = sizeof *st;
    return sa1_character_camera(ctx, id, alpha, st) == SA1_OK ? 0 : -1;
}
/* ---- T1: the strict camera replay ------------------------------------------------------------------------------------ */
#define CQ_T1_RUNS 64
typedef struct cq_t1_run { uint32_t steps, pos_bad, ang_bad, first, last, first_bad; } cq_t1_run;
static cq_t1_run cq_t1[CQ_T1_RUNS];
static int cq_t1_n = -1, cq_t1_on;

/* wr_inject has put the record's camera blob into w->inject.cam_raw: what the taskwk must hold after the camera's turn (the record is taken at
   player entry, the camera task runs before the players) */
static void cq_t1_pre(sa1g_world *w, int32_t sub, void *user)
{
    sa1g_camreq_pre(w, sub, user);
    if (w->camreq.running && cq_t1_n >= 0 && cq_t1_n < CQ_T1_RUNS) {
        cq_t1_run *R = &cq_t1[cq_t1_n];
        uint8_t twp[0x40];
        uint32_t k, bad = 0, step = (uint32_t)w->steps;
        memcpy(twp, sa1g_world_ptr(w, w->camera_twp), sizeof twp);
        if (memcmp(twp + 0x20, w->inject.cam_raw + 0x20, 12) != 0) {
            R->pos_bad++;
            bad = 1;
        }
        for (k = 0; k < 3u; k++) {
            uint32_t a, b;
            memcpy(&a, twp + 0x14 + 4u * k, 4);
            memcpy(&b, w->inject.cam_raw + 0x14 + 4u * k, 4);
            if ((a ^ b) & 0xFFFFu) {
                R->ang_bad++;
                bad = 1;
                break;
            }
        }
        if (bad && R->first_bad == 0xFFFFFFFFu) {
            R->first_bad = step;
        }
        if (R->steps == 0) {
            R->first = step;
        }
        R->last = step;
        R->steps++;
        cq_det[0] = cq_fnv(cq_det[0], twp + 0x14, 24);
    }
}

static sa1_status cq_step(sa1g_world *w, int32_t sub)
{
    if (cq_t1_on) {
        if (w->camreq.pre == NULL) {
            w->camreq.pre = cq_t1_pre;
            w->camreq.want = 1;
        }
        if (w->steps == 0u && cq_t1_n + 1 < CQ_T1_RUNS) {
            cq_t1_n++;
            memset(&cq_t1[cq_t1_n], 0, sizeof cq_t1[cq_t1_n]);
            cq_t1[cq_t1_n].first_bad = 0xFFFFFFFFu;
        }
    }
    return sa1g_world_step(w, sub);
}

static void cq_test_t1(const char *sadc, const char *gdi, const char *out)
{
    static const char *const names[3] = { "fish_c3_ec3", "fish_d2_ec3", "fish_s1_ec3" };
    static const uint32_t pass_steps[3] = { 293u, 523u, 623u };
    char *av[16];
    int ac = 0, rc, i, k;
    int seen[3] = { 0, 0, 0 };
    uint32_t bad = 0;
    char path[1200];
    printf("\n== T1: the strict camera replay (FISHCAM, the step on in replay worlds)\n");
    for (k = 0; k < 3; k++) {
        size_t n = 0;
        unsigned char *b;
        snprintf(path, sizeof path, "%s/build/traces/ref/%s.bin", sadc, names[k]);
        b = wr_read_file(path, &n);
        CHECK(b != NULL && n > 0, "T1 reference %s is there", names[k]);
        free(b);
    }
    av[ac++] = (char *)"camreq_test";
    av[ac++] = (char *)"--out";
    av[ac++] = (char *)out;
    for (k = 0; k < 3; k++) {
        av[ac++] = (char *)"--trace";
        av[ac++] = (char *)names[k];
    }
    av[ac++] = (char *)sadc;
    av[ac++] = (char *)gdi;
    av[ac] = NULL;
    cq_t1_on = 1;
    rc = cq_replay_main(ac, av);
    cq_t1_on = 0;
    CHECK(rc == 0, "T1 the replay of the three references with the step on: every record bit-exact (V6, V5, DISC; exit %d)", rc);
    for (i = 0; i <= cq_t1_n && i < CQ_T1_RUNS; i++) {
        const cq_t1_run *R = &cq_t1[i];
        printf("  run %2d: %u steps ran (%u..%u), position differs %u, angles differ %u\n", i, (unsigned)R->steps, (unsigned)R->first,
               (unsigned)R->last, (unsigned)R->pos_bad, (unsigned)R->ang_bad);
        bad += R->pos_bad + R->ang_bad;
        for (k = 0; k < 3; k++) {
            if (R->steps == pass_steps[k]) {
                seen[k]++;
            }
        }
    }
    CHECK(bad == 0, "T1 position and all three angles bit-identical in every run (%u differ)", (unsigned)bad);
    CHECK(seen[0] >= 3 && seen[1] >= 3 && seen[2] >= 3, "T1 the runs compared 293 / 523 / 623 steps (runs: %d / %d / %d; V6 twice, V5, DISC)",
          seen[0], seen[1], seen[2]);
}

/* ---- T2-T5: one cast on the island ------------------------------------------------------------------------------------ */
#define CQ_PAIRS 260
typedef struct cq_rec {
    sa1_camera_state st;
    float cam[3], tgt[3], lure[3], t2[3], big[3];
    cq_req q;
    cq_line ln;
    double move;
    int lm, bm;
} cq_rec;
typedef struct cq_ev { uint32_t id, arg, step; } cq_ev;
typedef struct cq_run { cq_rec r[CQ_PAIRS]; cq_ev ev[32]; int nev; int faults; } cq_run;

static void cq_events(sa1_context *ctx, cq_run *o)
{
    sa1_event ev[256];
    uint32_t n = 0, k;
    while (sa1_poll_events(ctx, ev, 256, &n) == SA1_OK && n > 0) {
        for (k = 0; k < n; k++) {
            if (ev[k].type == SA1_EVENT_CAMERA && o != NULL && o->nev < 32) {
                o->ev[o->nev].id = ev[k].id;
                o->ev[o->nev].arg = ev[k].arg;
                o->ev[o->nev].step = ev[k].step;
                o->nev++;
            }
        }
    }
}

static int cq_cast(const char *gdi, const cq_cam *hc, cq_run *o)
{
    sa1_context *ctx = cq_make_ctx(gdi, 1, (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SONIC));
    sa1_character_id id = ctx ? cq_make_char(ctx, SA1_CHAR_BIG) : 0;
    sa1g_world *w;
    float yaw = 0.0f, prev[3] = { 0, 0, 0 };
    int p;
    memset(o, 0, sizeof *o);
    if (!id) {
        return -1;
    }
    w = sa1_character_world(ctx, id);
    for (p = 0; p < CQ_PAIRS; p++) {
        float sx, sy;
        uint32_t b, ct;
        cq_rec *R = &o->r[p];
        cq_script(p, &sx, &sy, &b);
        if (cq_tick(ctx, id, w, hc, sx, sy, b, yaw, 1)) {
            o->faults++;
            break;
        }
        cq_events(ctx, o);
        ct = cq_r32(w, CQ_ADDR_CAM_TWP);
        cq_rv(w, ct + 0x20u, R->cam);
        cq_rv(w, CQ_ADDR_WORK + 0x18u, R->tgt);
        cq_rv(w, CQ_ADDR_BPOS, R->lure);
        cq_rv(w, CQ_ADDR_BPOS + 12u, R->t2);
        cq_rv(w, cq_r32(w, CQ_ADDR_PLAYER_TWP) + 0x20u, R->big);
        cq_get_req(w, &R->q);
        cq_line_check(ctx, id, w, &R->ln);
        cq_state(ctx, id, 1.0f, &R->st);
        R->move = cq_dist(R->cam, prev);
        R->lm = cq_lure_mode(w);
        R->bm = cq_big_mode(w);
        memcpy(prev, R->cam, sizeof prev);
        if (w->camreq.running) {
            yaw = cq_yaw_of(w);     /* the design: while a known request runs, the host passes the game camera's frame (G-SA1's TickYaw) */
        }
        if (w->fault.kind) {
            o->faults++;
            printf("    world fault: %s\n", w->fault.msg);
            break;
        }
    }
    sa1_shutdown(ctx);
    return 0;
}

static cq_run cq_a, cq_b, cq_c;

static void cq_test_t2(void)
{
    const cq_run *o = &cq_a;
    int k, cuts = 0, cutp = -1;
    uint32_t eh = 2166136261u;
    printf("\n== T2: the request log on the island (world steps; the chase camera)\n");
    for (k = 0; k < o->nev; k++) {
        printf("  event %d: step %u function %08X level %u adjust %u\n", k, (unsigned)o->ev[k].step, (unsigned)o->ev[k].id,
               (unsigned)(o->ev[k].arg & 0xFFu), (unsigned)((o->ev[k].arg >> 8) & 0xFFu));
        eh = cq_fnv(eh, &o->ev[k], sizeof o->ev[k]);
    }
    cq_det[1] = eh;
    CHECK(o->nev == 4, "T2 four SA1_EVENT_CAMERA events (%d)", o->nev);
    if (o->nev == 4) {
        CHECK(o->ev[0].id == 0x8C0239C2u && o->ev[0].arg == (2u | 3u << 8) && o->ev[0].step >= 46u && o->ev[0].step <= 48u,
              "T2 AIM level 2 adjust 3 right after step 46 (step %u)", (unsigned)o->ev[0].step);
        CHECK(o->ev[1].id == 0x8C0EAF28u && o->ev[1].arg == 4u && o->ev[1].step >= 164u && o->ev[1].step <= 166u,
              "T2 LURE level 4 adjust 0 right after step 164 (step %u)", (unsigned)o->ev[1].step);
        CHECK(o->ev[2].id == 0x8C0239C2u && o->ev[2].arg == 2u && o->ev[2].step >= 400u && o->ev[2].step <= 402u,
              "T2 AIM adjust 0 right after step 400 (step %u)", (unsigned)o->ev[2].step);
        CHECK(o->ev[3].id == 0u && (o->ev[3].arg & 0xFFu) == 0u && o->ev[3].step >= 418u && o->ev[3].step <= 420u,
              "T2 level 0 right after step 418 (step %u, adjust %u)", (unsigned)o->ev[3].step, (unsigned)(o->ev[3].arg >> 8));
    }
    for (k = 0; k < CQ_PAIRS; k++) {
        if (o->r[k].st.flags & SA1_CAMSTATE_CUT) {
            cuts++;
            cutp = k;
        }
    }
    CHECK(cuts == 1 && cutp == 201, "T2 one CUT, in the pair after the reel's end (%d, at pair %d)", cuts, cutp);
    CHECK(o->r[22].st.flags == 0u && o->r[22].st.kind == SA1_CAMKIND_NONE, "T2 before the rod: no request (flags %X)", (unsigned)o->r[22].st.flags);
    CHECK(o->r[23].st.flags == (SA1_CAMSTATE_ACTIVE | SA1_CAMSTATE_KNOWN | SA1_CAMSTATE_RUNNING) && o->r[23].st.kind == SA1_CAMKIND_AIM &&
          o->r[23].st.level == 2 && o->r[23].st.adjust == 3u && o->r[23].st.function == 0x8C0239C2u,
          "T2 pair 23: ACTIVE KNOWN RUNNING, AIM, level 2, adjust 3 (flags %X, kind %u, level %d, adjust %u)", (unsigned)o->r[23].st.flags,
          (unsigned)o->r[23].st.kind, (int)o->r[23].st.level, (unsigned)o->r[23].st.adjust);
    CHECK(o->r[60].st.kind == SA1_CAMKIND_AIM && o->r[60].st.adjust == 0u, "T2 pair 60: SLOW ended, adjust 0 (%u)", (unsigned)o->r[60].st.adjust);
    CHECK(o->r[82].st.kind == SA1_CAMKIND_LURE && o->r[82].st.level == 4 && o->r[82].st.adjust == 0u, "T2 pair 82: LURE, level 4 (kind %u, level %d)",
          (unsigned)o->r[82].st.kind, (int)o->r[82].st.level);
    CHECK(o->r[209].st.flags == 0u && o->r[209].st.level == 0 && o->r[209].st.function == 0u, "T2 pair 209: level 0, nothing (flags %X)",
          (unsigned)o->r[209].st.flags);
    CHECK(fabs((double)o->r[100].st.fov - 70.0 * CQ_PI / 180.0) < 0.001, "T2 fov = the game's 70 degrees (%.4f rad)", (double)o->r[100].st.fov);
}

static void cq_test_t3(void)
{
    const cq_run *o = &cq_a;
    int p, aimn = 0, aimok = 0, wet = 0;
    double aimmax = 0, midmin = 1e9, midmax = 0, luremin = 1e9, luremax = 0, movemax = 0, flymove = 0;
    uint32_t h = 2166136261u;
    printf("\n== T3: the numbers (the chase camera)\n");
    for (p = 0; p < CQ_PAIRS; p++) {
        const cq_rec *R = &o->r[p];
        h = cq_fnv(h, R->cam, sizeof R->cam);
        h = cq_fnv(h, R->st.position, sizeof R->st.position);
        h = cq_fnv(h, R->st.forward, sizeof R->st.forward);
        if (R->q.fn == 0x8C0239C2u) {
            float big20[3];
            double d;
            big20[0] = R->big[0]; big20[1] = R->big[1] + 20.0f; big20[2] = R->big[2];
            d = cq_dist(R->cam, big20);
            aimn++;
            if (p >= 40 && p < 200) {
                aimmax = fabs(d - 50.0) > aimmax ? fabs(d - 50.0) : aimmax;
            }
            if (fabs(d - 50.0) < 0.01) {
                aimok++;
            }
        }
        if (p >= 82 && p < 90) {
            flymove = R->move > flymove ? R->move : flymove;
        }
        if (R->q.fn == 0x8C0EAF28u && R->lm == 5 && p >= 90 && p < 200) {
            float mid[3];
            double dm, dl;
            mid[0] = (R->lure[0] + R->t2[0]) * 0.5f; mid[1] = (R->lure[1] + R->t2[1]) * 0.5f; mid[2] = (R->lure[2] + R->t2[2]) * 0.5f;
            dm = cq_dist(R->cam, mid);
            dl = cq_dist(R->cam, R->lure);
            wet++;
            midmin = dm < midmin ? dm : midmin;
            midmax = dm > midmax ? dm : midmax;
            luremin = dl < luremin ? dl : luremin;
            luremax = dl > luremax ? dl : luremax;
            movemax = R->move > movemax ? R->move : movemax;
        }
    }
    cq_det[2] = h;
    CHECK(aimn == 68 && aimok >= 56, "T3 FISHING over %d pairs: %d within 0.01 of 50 from Big + (0, 20, 0) (fish098: 56 of 68)", aimn, aimok);
    CHECK(aimmax <= 0.01, "T3 FISHING from pair 40: at most %.4f off 50", aimmax);
    CHECK(wet == 110 && midmin >= 19.96 && midmax <= 20.08, "T3 pairs 90-199 (%d): |camera - midpoint| %.2f .. %.2f (20.01-20.03 +- 0.05)", wet, midmin, midmax);
    CHECK(luremin >= 15.08 && luremax <= 16.72, "T3 |camera - lure| %.2f .. %.2f (15.13-16.67 +- 0.05)", luremin, luremax);
    CHECK(movemax <= 0.1, "T3 a pair's move there at most %.2f", movemax);
    CHECK(flymove <= 40.01 && flymove >= 39.9, "T3 the flight to the lure: at most 40 units a pair, 20 a step (%.2f)", flymove);
}

static void cq_test_t4(void)
{
    const cq_run *o = &cq_a;
    int p, amin = 999, amax = -1, rmin = 999, rmax = -1, n = 0;
    uint32_t h = 2166136261u;
    printf("\n== T4: the line\n");
    for (p = 90; p < 200; p++) {
        const cq_rec *R = &o->r[p];
        if (R->ln.n > 0) {
            n++;
            amin = R->ln.a_lure < amin ? R->ln.a_lure : amin;
            amax = R->ln.a_lure > amax ? R->ln.a_lure : amax;
            rmin = R->ln.a_rod < rmin ? R->ln.a_rod : rmin;
            rmax = R->ln.a_rod > rmax ? R->ln.a_rod : rmax;
            h = cq_fnv(h, &R->ln.a_lure, sizeof R->ln.a_lure);
        }
    }
    cq_det[3] = h;
    CHECK(n == 110 && amin >= 191 && amax <= 203, "T4 pairs 90-199: the lure end %d..%d (fish098: 191..203; 64 today)", amin, amax);
    CHECK(rmin == 64 && rmax == 64, "T4 the rod end %d..%d", rmin, rmax);
}

/* every vertex against the game's formula: a run of its own, with the arena at hand */
static void cq_test_t4_formula(const char *gdi)
{
    sa1_context *ctx = cq_make_ctx(gdi, 1, (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SONIC));
    sa1_character_id id = ctx ? cq_make_char(ctx, SA1_CHAR_BIG) : 0;
    sa1g_world *w;
    float yaw = 0.0f;
    int p, bad = 0, verts = 0;
    if (!id) {
        CHECK(0, "T4 context");
        return;
    }
    w = sa1_character_world(ctx, id);
    for (p = 0; p < 200; p++) {
        float sx, sy, cp[3];
        uint32_t b, i;
        const sa1_render *r = NULL;
        cq_script(p, &sx, &sy, &b);
        if (cq_tick(ctx, id, w, &CQ_CHASE, sx, sy, b, yaw, 1)) {
            break;
        }
        if (w->camreq.running) {
            yaw = cq_yaw_of(w);
        }
        /* the line drawn in this pair was drawn at the pair's first step: the drawn record's camera */
        memcpy(cp, w->camreq.drawn[1].pos, sizeof cp);
        if (p >= 90 && sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK && r) {
            for (i = 0; i < r->num_line_vertices; i++) {
                float s[3], d2;
                int a = r->line_vertices[i].color[3], e;
                cq_to_sa1(r->line_vertices[i].position, s);
                d2 = (s[0] - cp[0]) * (s[0] - cp[0]) + (s[1] - cp[1]) * (s[1] - cp[1]) + (s[2] - cp[2]) * (s[2] - cp[2]);
                e = 255 - (int)(d2 * 0.25f);
                e = e > 0xE0 ? 0xE0 : e < 0x40 ? 0x40 : e;
                bad += abs(e - a) > 1;
                verts++;
            }
        }
    }
    CHECK(verts > 1000 && bad == 0, "T4 every vertex the game's formula against the camera's position within 1 (%d of %d off)", bad, verts);
    sa1_shutdown(ctx);
}

static void cq_test_t5(void)
{
    int p, off = 0;
    double maxd = 0;
    printf("\n== T5: the seed\n");
    printf("  pair 23: moved %.2f (chase), %.2f (default), %.2f (the virtual eye)\n", cq_a.r[23].move, cq_b.r[23].move, cq_c.r[23].move);
    CHECK(fabs(cq_a.r[23].move - 0.7) < 0.1, "T5 the first FISHING pair moved 0.7 from the host's eye (%.2f)", cq_a.r[23].move);
    CHECK(fabs(cq_c.r[23].move - 0.7) < 0.1, "T5 ... 0.7 from the virtual eye without the host camera (%.2f)", cq_c.r[23].move);
    for (p = 40; p < 200; p++) {
        double d = cq_dist(cq_b.r[p].cam, cq_c.r[p].cam), e = cq_dist(cq_a.r[p].cam, cq_c.r[p].cam);
        d = d > e ? d : e;
        maxd = d > maxd ? d : maxd;
        off += d > 0.05;
    }
    CHECK(off == 0, "T5 the three seeds equal from pair 40 (largest difference %.4f units, %d pairs off)", maxd, off);
}
/* ---- T3b: the SET spawner's centre (fish098.md 8.14): the lure camera while the lure is in the water -------------------- */
static uint32_t cq_set_rec(sa1_context *ctx, float x, float y, float z)
{
    sa1_set_record r;
    float s[3] = { x, y, z };
    uint32_t id = 0;
    memset(&r, 0, sizeof r);
    r.struct_size = sizeof r;
    r.type = SA1_OBJTYPE_RING;
    cq_to_host(s, r.position);
    sa1_set_add(ctx, &r, &id);
    return id;
}
static int cq_is_live(sa1_context *ctx, sa1_character_id ch, uint32_t rec)
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
static void cq_test_t3b(const char *gdi)
{
    int bit;
    printf("\n== T3 (fish098.md 8.14): the SET spawner's centre with the lure in the water\n");
    for (bit = 1; bit >= 0; bit--) {
        sa1_context *ctx = cq_make_ctx(gdi, 1, (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SONIC));
        sa1_character_id id = ctx ? cq_make_char(ctx, SA1_CHAR_BIG) : 0;
        sa1g_world *w;
        float yaw = 0.0f;
        uint32_t near_cam = 0, far_cam = 0;
        int p, a, b;
        if (!id) {
            CHECK(0, "T3b context");
            return;
        }
        w = sa1_character_world(ctx, id);
        for (p = 0; p < 130; p++) {
            float sx, sy;
            uint32_t bt;
            cq_script(p, &sx, &sy, &bt);
            if (p == 120) {
                near_cam = cq_set_rec(ctx, 190.0f, 6.0f, -31.0f);       /* 30 from the lure camera, 190 from Big */
                far_cam = cq_set_rec(ctx, -300.0f, 6.0f, 0.0f);         /* 300 from Big, 465 from the lure camera */
            }
            if (cq_tick(ctx, id, w, &CQ_CHASE, sx, sy, bt, yaw, bit)) {
                break;
            }
            if (w->camreq.running) {
                yaw = cq_yaw_of(w);
            }
        }
        a = cq_is_live(ctx, id, near_cam);
        b = cq_is_live(ctx, id, far_cam);
        if (bit) {
            CHECK(a && !b, "T3 the bit on: the RING 30 units from the lure camera spawns, the one 465 from it (300 from Big) does not (live %d / %d)", a, b);
        } else {
            CHECK(a && b, "T3 the bit off: the host's eye near Big is the centre, both spawn (live %d / %d)", a, b);
        }
        sa1_shutdown(ctx);
    }
}

/* ---- T6, T7, T8: pinned from libsa1 0.3.0's own behaviour --------------------------------------------------------------- */
#define CQ_STATE_030 464u   /* sizeof(sa1_state_ex) in libsa1 0.3.0 (r098/lookcam/t6_pin.c) */
typedef struct cq_pin { uint32_t st, ev, tri, col, spr, lp, lc, nev, ntri, nspr, nline; } cq_pin;
static const int32_t CQ_KINDS[6] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA, SA1_CHAR_BIG };
/* r098/lookcam/t6_pin_x86_64-windows.txt = t6_pin_x86-windows.txt (libsa1 0.3.0, r098/base/lib030, the pushes void) */
static const cq_pin CQ_PINS[6] = {
    { 0x7FF332D4u, 0x3ECA6EF9u, 0xBA6A30E5u, 0x75329A2Du, 0x811C9DC5u, 0x811C9DC5u, 0x811C9DC5u, 52u, 306482u, 0u, 0u },
    { 0x2E3760C2u, 0x0E5A8006u, 0x3CE517AFu, 0x8EFCDF6Cu, 0x811C9DC5u, 0x811C9DC5u, 0x811C9DC5u, 10u, 437445u, 0u, 0u },
    { 0xB2B5EF98u, 0x0AD8D890u, 0xADDA6F4Eu, 0x0D260245u, 0xE332B8C5u, 0x811C9DC5u, 0x811C9DC5u, 18u, 261220u, 42u, 0u },
    { 0xA07DA38Du, 0x01EF605Eu, 0x50C61686u, 0xBDCE675Du, 0xDE7F3C69u, 0x811C9DC5u, 0x811C9DC5u, 17u, 407260u, 371u, 0u },
    { 0xAF1F2233u, 0xCFC6847Cu, 0x4115D760u, 0x7A281A45u, 0x811C9DC5u, 0x30D135ABu, 0x36449FC5u, 41u, 311380u, 0u, 400u },
    { 0x53A68838u, 0x889C47D5u, 0x3DF91D88u, 0x7641A881u, 0x811C9DC5u, 0x811C9DC5u, 0x811C9DC5u, 8u, 409251u, 0u, 0u },
};
static void cq_d_script(int i, float *sx, float *sy, uint32_t *b, float *yaw)
{
    *sx = 0; *sy = 0; *b = 0;
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
static void cq_hash_pair(sa1_context *ctx, sa1_character_id id, cq_pin *o, uint32_t *camera_events)
{
    sa1_state_ex s;
    sa1_event ev[256];
    uint32_t n = 0, j;
    const sa1_render *r = NULL;
    memset(&s, 0, sizeof s);
    s.struct_size = CQ_STATE_030;
    sa1_character_state_ex(ctx, id, &s);
    o->st = cq_fnv(o->st, &s, CQ_STATE_030);
    while (sa1_poll_events(ctx, ev, 256, &n) == SA1_OK && n > 0) {
        for (j = 0; j < n; j++) {
            if (ev[j].type == SA1_EVENT_CAMERA) {
                if (camera_events) {
                    (*camera_events)++;
                }
                continue;
            }
            o->ev = cq_fnv(o->ev, &ev[j], sizeof ev[j]);
            o->nev++;
        }
    }
    if (sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK && r) {
        o->tri = cq_fnv(o->tri, r->positions, (size_t)r->num_triangles * 36u);
        o->col = cq_fnv(o->col, r->colors, (size_t)r->num_triangles * 12u);
        o->spr = cq_fnv(o->spr, r->sprites, (size_t)r->num_sprites * sizeof *r->sprites);
        for (j = 0; j < r->num_line_vertices; j++) {
            o->lp = cq_fnv(o->lp, r->line_vertices[j].position, 12);
            o->lc = cq_fnv(o->lc, r->line_vertices[j].color, 4);
        }
        o->ntri += r->num_triangles;
        o->nspr += r->num_sprites;
        o->nline += r->num_line_vertices;
    }
}
static void cq_pin_init(cq_pin *o)
{
    memset(o, 0, sizeof *o);
    o->st = o->ev = o->tri = o->col = o->spr = o->lp = o->lc = 2166136261u;
}
static uint32_t cq_g_push_fn;
static void cq_push_cb(sa1g_world *w, void *user)
{
    sa1_gfn g;
    (void)w;
    (void)user;
    g.v = cq_g_push_fn;
    ((void (*)(sa1_gfn, int8_t, int8_t))sa1_fn(SA1_GFN_C(0x8C01AD2Cu), 0u))(g, 0, 2);
}
static void cq_pop_cb(sa1g_world *w, void *user)
{
    (void)w;
    (void)user;
    ((void (*)(void))sa1_fn(SA1_GFN_C(0x8C01AE60u), 0u))();
}
/* other: 1 = the Sonic run with the request OTHER, pushed at pair 50, popped at pair 200 */
static int cq_d_run(const char *gdi, int32_t kind, int bit, int other, cq_pin *o, uint32_t *camera_events, int *other_ok)
{
    sa1_context *ctx = cq_make_ctx(gdi, 0, 1u << kind);
    sa1_character_id id = ctx ? cq_make_char(ctx, kind) : 0;
    sa1g_world *w;
    int i;
    cq_pin_init(o);
    if (!id) {
        return -1;
    }
    w = sa1_character_world(ctx, id);
    for (i = 0; i < 300; i++) {
        float sx, sy, yaw;
        uint32_t b;
        cq_d_script(i, &sx, &sy, &b, &yaw);
        if (other && i == 50) {
            cq_g_push_fn = 0x8C021700u;
            sa1g_world_call(w, cq_push_cb, NULL);
        }
        if (other && i == 200) {
            sa1g_world_call(w, cq_pop_cb, NULL);
        }
        if (cq_tick(ctx, id, w, NULL, sx, sy, b, yaw, bit)) {
            sa1_shutdown(ctx);
            return -1;
        }
        cq_hash_pair(ctx, id, o, camera_events);
        if (other && other_ok && i >= 51 && i < 199) {
            sa1_camera_state st;
            if (cq_state(ctx, id, 1.0f, &st) != 0 || st.flags != SA1_CAMSTATE_ACTIVE || st.kind != SA1_CAMKIND_OTHER || st.function != 0x8C021700u) {
                *other_ok = 0;
            }
        }
    }
    sa1_shutdown(ctx);
    return 0;
}
static int cq_pin_cmp(const cq_pin *a, const cq_pin *b, char *why, size_t n)
{
    snprintf(why, n, "state %08X/%08X events %08X/%08X (%u/%u) tris %08X/%08X colours %08X/%08X sprites %08X/%08X line %08X/%08X %08X/%08X",
             (unsigned)a->st, (unsigned)b->st, (unsigned)a->ev, (unsigned)b->ev, (unsigned)a->nev, (unsigned)b->nev, (unsigned)a->tri, (unsigned)b->tri,
             (unsigned)a->col, (unsigned)b->col, (unsigned)a->spr, (unsigned)b->spr, (unsigned)a->lp, (unsigned)b->lp, (unsigned)a->lc, (unsigned)b->lc);
    return memcmp(a, b, sizeof *a) == 0;
}
static void cq_test_t6(const char *gdi)
{
    int k;
    printf("\n== T6: the bit off, six characters, 300 pairs, against 0.3.0's pinned hashes (sa1_state_ex at 464 bytes)\n");
    CHECK(offsetof(sa1_state_ex, look_kind) == CQ_STATE_030, "T6 the look tail starts at 0.3.0's size (%u)", (unsigned)offsetof(sa1_state_ex, look_kind));
    for (k = 0; k < 6; k++) {
        cq_pin p;
        char why[400];
        uint32_t cev = 0;
        cq_d_run(gdi, CQ_KINDS[k], 0, 0, &p, &cev, NULL);
        CHECK(cq_pin_cmp(&p, &CQ_PINS[k], why, sizeof why) && cev == 0u, "T6 %s: byte for byte 0.3.0, no camera event (%u)", sa1_character_name(CQ_KINDS[k]), (unsigned)cev);
        if (memcmp(&p, &CQ_PINS[k], sizeof p) != 0) {
            printf("    %s\n", why);
        }
    }
    /* Big's cast without the bit: the taskwk is the host's eye, the line 64 */
    {
        sa1_context *ctx = cq_make_ctx(gdi, 1, (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SONIC));
        sa1_character_id id = ctx ? cq_make_char(ctx, SA1_CHAR_BIG) : 0;
        sa1g_world *w;
        int p, off = 0, lines = 0, bright = 0;
        if (!id) {
            CHECK(0, "T6 context");
            return;
        }
        w = sa1_character_world(ctx, id);
        for (p = 0; p < 200; p++) {
            float sx, sy, c[3];
            uint32_t b;
            cq_line ln;
            sa1_camera_state st;
            cq_script(p, &sx, &sy, &b);
            if (cq_tick(ctx, id, w, &CQ_CHASE, sx, sy, b, 0.0f, 0)) {
                break;
            }
            cq_rv(w, cq_r32(w, CQ_ADDR_CAM_TWP) + 0x20u, c);
            off += cq_dist(c, cq_last_eye) > 0.01;
            cq_line_check(ctx, id, w, &ln);
            if (ln.n > 0) {
                lines++;
                bright += ln.a_lure != 64 || ln.a_rod != 64;
            }
            cq_state(ctx, id, 1.0f, &st);
            if (st.flags & SA1_CAMSTATE_RUNNING) {
                off++;
            }
        }
        CHECK(off == 0 && lines > 50 && bright == 0 && w->camreq.pre == NULL, "T6 Big's cast without the bit: the taskwk is the host's eye (%d off), the line 64 (%d of %d "
              "pairs brighter), nothing installed", off, bright, lines);
        sa1_shutdown(ctx);
    }
}
static void cq_test_t7(const char *gdi)
{
    int k;
    printf("\n== T7: the bit on, six characters: the same pins (camera events apart)\n");
    for (k = 0; k < 6; k++) {
        cq_pin p;
        char why[400];
        uint32_t cev = 0;
        cq_d_run(gdi, CQ_KINDS[k], 1, 0, &p, &cev, NULL);
        CHECK(cq_pin_cmp(&p, &CQ_PINS[k], why, sizeof why), "T7 %s: byte for byte 0.3.0 (%u camera events)", sa1_character_name(CQ_KINDS[k]), (unsigned)cev);
        if (memcmp(&p, &CQ_PINS[k], sizeof p) != 0) {
            printf("    %s\n", why);
        }
    }
}
static int cq_board_run(const char *gdi, int32_t kind, int bit, cq_pin *o)
{
    sa1_context *ctx = cq_make_ctx(gdi, 0, 1u << kind);
    sa1_character_id id = ctx ? cq_make_char(ctx, kind) : 0;
    sa1g_world *w;
    int i;
    cq_pin_init(o);
    if (!id) {
        return -1;
    }
    w = sa1_character_world(ctx, id);
    for (i = 0; i < 300; i++) {
        float sx = (i >= 100 && i < 130) ? 0.7f : 0.0f, sy = i >= 30 ? 1.0f : 0.0f;
        uint32_t b = 0;
        if (i == 10) {
            sa1_board_desc d;
            memset(&d, 0, sizeof d);
            d.struct_size = sizeof d;
            d.on = SA1_BOARD_ON;
            if (sa1_character_board(ctx, id, &d) != SA1_OK) {
                printf("    board: %s\n", sa1_last_error(ctx));
            }
        }
        if (i == 150 || i == 151 || i == 220 || i == 221) b = SA1_BUTTON_JUMP;
        if (i >= 170 && i < 175) b = SA1_BUTTON_ACTION;
        if (cq_tick(ctx, id, w, NULL, sx, sy, b, 0.0f, bit)) {
            sa1_shutdown(ctx);
            return -1;
        }
        cq_hash_pair(ctx, id, o, NULL);
    }
    sa1_shutdown(ctx);
    return 0;
}
static void cq_test_t8(const char *gdi)
{
    cq_pin p, q;
    char why[400];
    int ok = 1;
    uint32_t cev = 0;
    printf("\n== T8: requests LIB does not know\n");
    cq_d_run(gdi, SA1_CHAR_SONIC, 1, 1, &p, &cev, &ok);
    CHECK(ok, "T8 Sonic, 8C021700 pushed at pair 50: ACTIVE, not KNOWN, not RUNNING, kind OTHER, pairs 51-198");
    CHECK(cq_pin_cmp(&p, &CQ_PINS[0], why, sizeof why), "T8 the run is byte for byte 0.3.0's (%u camera events)", (unsigned)cev);
    if (memcmp(&p, &CQ_PINS[0], sizeof p) != 0) {
        printf("    %s\n", why);
    }
    CHECK(cev == 2u, "T8 two SA1_EVENT_CAMERA: the push, the pop (%u)", (unsigned)cev);
    cq_board_run(gdi, SA1_CHAR_SONIC, 0, &p);
    cq_board_run(gdi, SA1_CHAR_SONIC, 1, &q);
    CHECK(cq_pin_cmp(&p, &q, why, sizeof why) && p.ntri > 0u, "T8 Sonic on the board: the bit changes nothing in 300 pairs");
    cq_board_run(gdi, SA1_CHAR_TAILS, 0, &p);
    cq_board_run(gdi, SA1_CHAR_TAILS, 1, &q);
    CHECK(cq_pin_cmp(&p, &q, why, sizeof why) && p.ntri > 0u, "T8 Tails on the board: the bit changes nothing in 300 pairs");
}

/* ---- T9: the stick's frame ---------------------------------------------------------------------------------------------- */
static void cq_test_t9(const char *gdi)
{
    int bit;
    const float wrong = 2.0f;
    printf("\n== T9: the stick's frame while aiming, camera_yaw fixed at a WRONG value (%.1f rad)\n", (double)wrong);
    for (bit = 1; bit >= 0; bit--) {
        sa1_context *ctx = cq_make_ctx(gdi, 1, (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SONIC));
        sa1_character_id id = ctx ? cq_make_char(ctx, SA1_CHAR_BIG) : 0;
        sa1g_world *w;
        double rmin = 1e9, rmax = -1e9, frame_min = 1e9, frame_max = -1e9;
        int p;
        if (!id) {
            CHECK(0, "T9 context");
            return;
        }
        w = sa1_character_world(ctx, id);
        for (p = 0; p < 62; p++) {
            float sx = 0, sy = 0, t0[3], t1[3], c[3];
            uint32_t b = 0;
            if (p >= 10) b = SA1_BUTTON_ACTION;
            if (p >= 25 && p < 40) sy = 1.0f;
            if (p >= 40) sx = 1.0f;
            cq_rv(w, CQ_ADDR_BPOS + 12u, t0);
            if (cq_tick(ctx, id, w, &CQ_CHASE, sx, sy, b, wrong, bit)) {
                break;
            }
            cq_rv(w, CQ_ADDR_BPOS + 12u, t1);
            cq_rv(w, cq_r32(w, CQ_ADDR_CAM_TWP) + 0x20u, c);
            if (p >= 45 && p < 62) {
                float f[3], r[3], d[3];
                double n, along;
                /* along the camera's right, as it stands (cursor - camera, level) */
                f[0] = t1[0] - c[0]; f[1] = 0; f[2] = t1[2] - c[2];
                n = sqrt((double)f[0] * f[0] + (double)f[2] * f[2]);
                f[0] /= (float)n; f[2] /= (float)n;
                r[0] = -f[2]; r[1] = 0; r[2] = f[0];
                d[0] = t1[0] - t0[0]; d[1] = t1[1] - t0[1]; d[2] = t1[2] - t0[2];
                along = (double)d[0] * r[0] + (double)d[2] * r[2];
                rmin = along < rmin ? along : rmin;
                rmax = along > rmax ? along : rmax;
                /* along camera_yaw's right: camera ang.y c = yaw BAMS - 0x4000; right = (cos c, 0, -sin c) */
                {
                    double cc = (double)wrong - CQ_PI / 2.0;
                    double a2 = (double)d[0] * cos(cc) - (double)d[2] * sin(cc);
                    frame_min = a2 < frame_min ? a2 : frame_min;
                    frame_max = a2 > frame_max ? a2 : frame_max;
                }
            }
        }
        printf("  bit %d: pairs 45-61: the cursor moved %.3f .. %.3f along the camera's right, %.3f .. %.3f along camera_yaw's right\n", bit, rmin, rmax, frame_min, frame_max);
        if (bit) {
            CHECK(rmin >= 5.88 && rmax <= 5.98, "T9 the bit on: 5.93 +- 0.05 per pair along the game camera's right, whatever camera_yaw says (%.3f .. %.3f)", rmin, rmax);
        } else {
            CHECK(frame_min >= 5.5 && frame_max <= 6.3, "T9 the bit off: along camera_yaw's right (%.3f .. %.3f)", frame_min, frame_max);
        }
        CHECK(!w->fault.kind, "T9 no fault");
        sa1_shutdown(ctx);
    }
}
/* ---- T10: the ends (fish098.md 2.5 X, L) -------------------------------------------------------------------------------- */
typedef struct cq_xcase { const char *name; int kind, ground, end_pair, ends_normally, keep_level; } cq_xcase;
static void cq_test_t10(const char *gdi)
{
    /* kind as fish_probe's part X; end_pair: the pair the level goes to 0 (-1 stays); keep_level: the level at the end */
    static const cq_xcase cases[] = {
        { "L + R with the lure in the water", 0, 1, 158, 1, 0 },
        { "reel in with the jump button until the line is in", 1, 1, 507, 1, 0 },
        { "action tap (rod up, rod away): no request", 2, 1, -2, 0, 0 },
        { "hurt with the lure in the water: the game ignores it", 3, 1, -1, 0, 4 },
        { "hurt while aiming", 4, 1, 25, 1, 0 },
        { "killed with the lure in the water: the level stays (the game's)", 5, 1, -1, 0, 4 },
        { "killed while aiming: the level stays (the game's)", 6, 1, -1, 0, 2 },
        { "jump pressed while aiming", 7, 1, 47, 1, 0 },
        { "a cast on dry ground: no lure camera", 8, 0, 89, 1, 0 },
    };
    size_t c;
    printf("\n== T10: how a request ends\n");
    for (c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        const cq_xcase *C = &cases[c];
        sa1_context *ctx = cq_make_ctx(gdi, C->ground, (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SONIC));
        sa1_character_id id = ctx ? cq_make_char(ctx, SA1_CHAR_BIG) : 0;
        sa1g_world *w;
        int p, total = C->kind == 1 ? 560 : 300, zero_at = -1, was_active = 0, lure_cam = 0, back = 0, back_ok = 0;
        float yaw = 0.0f;
        cq_req q;
        if (!id) {
            CHECK(0, "T10 context");
            return;
        }
        w = sa1_character_world(ctx, id);
        q.level = 0;
        for (p = 0; p < total; p++) {
            float sx = 0, sy = 0;
            uint32_t b = 0;
            int kind = C->kind;
            if (kind == 2) {
                if (p >= 10 && p < 12) b = SA1_BUTTON_ACTION;
            } else if (kind == 4 || kind == 6 || kind == 7) {
                if (p >= 10 && p < 40) b = SA1_BUTTON_ACTION;
                if (kind == 7 && p >= 25) b |= (p < 40 ? SA1_BUTTON_JUMP : 0u);
            } else {
                if (p >= 10 && p < 50) b = SA1_BUTTON_ACTION;
                if (p >= 25 && p < 50) sy = 1.0f;
                if (kind == 0 && p >= 150 && p < 170) b = SA1_BUTTON_L | SA1_BUTTON_R;
                if (kind == 1 && p >= 150) b = SA1_BUTTON_JUMP;
            }
            if (((kind == 3 || kind == 5) && p == 150) || ((kind == 4 || kind == 6) && p == 25)) {
                sa1_damage d;
                memset(&d, 0, sizeof d);
                d.struct_size = sizeof d;
                d.flags = (kind == 5 || kind == 6) ? SA1_DAMAGE_KILL : 0u;
                if (sa1_character_damage(ctx, id, &d) != SA1_OK) {
                    printf("    damage: %s\n", sa1_last_error(ctx));
                }
            }
            if (cq_tick(ctx, id, w, &CQ_CHASE, sx, sy, b, yaw, 1) || w->fault.kind) {
                printf("    stopped at pair %d: %s\n", p, w->fault.kind ? w->fault.msg : sa1_last_error(ctx));
                break;
            }
            if (w->camreq.running) {
                yaw = cq_yaw_of(w);
            }
            cq_get_req(w, &q);
            if (q.level > 0) {
                was_active = 1;
            }
            if (q.fn == 0x8C0EAF28u) {
                lure_cam = 1;
            }
            if (was_active && q.level == 0 && zero_at < 0) {
                zero_at = p;
            }
            /* the pair after the end: the taskwk is the host's eye again; ACTIVE clear */
            if (zero_at >= 0 && p >= zero_at + 1 && !back) {
                sa1_camera_state st;
                float cc[3];
                back = 1;
                cq_state(ctx, id, 1.0f, &st);
                cq_rv(w, cq_r32(w, CQ_ADDR_CAM_TWP) + 0x20u, cc);
                back_ok = st.flags == 0u && cq_dist(cc, cq_last_eye) < 0.01;
                if (!back_ok) {
                    printf("    pair %d: flags %X, taskwk (%.3f %.3f %.3f), the eye (%.3f %.3f %.3f)\n", p, (unsigned)st.flags, (double)cc[0], (double)cc[1], (double)cc[2],
                           (double)cq_last_eye[0], (double)cq_last_eye[1], (double)cq_last_eye[2]);
                }
            }
        }
        if (C->end_pair == -2) {
            CHECK(!was_active, "T10 %s (no request)", C->name);
        } else if (C->end_pair == -1) {
            CHECK(zero_at < 0 && q.level == C->keep_level, "T10 %s (level %d at pair %d)", C->name, q.level, p);
        } else {
            CHECK(zero_at == C->end_pair && back_ok, "T10 %s: level 0 at pair %d (%d), the pair after: ACTIVE clear, the taskwk the host's eye", C->name, zero_at, C->end_pair);
        }
        if (C->kind == 8) {
            CHECK(!lure_cam, "T10 dry ground: the lure camera never comes");
        }
        CHECK(!w->fault.kind, "T10 0 faults (%s)", w->fault.kind ? w->fault.msg : "none");
        sa1_shutdown(ctx);
    }
}

/* ---- T11: a rebuild mid-cast -------------------------------------------------------------------------------------------- */
static int cq_rebuild(sa1_context *ctx, sa1g_world *w)
{
    sa1g_boot_cfg cfg;
    uint32_t notes = 0;
    int32_t land = w->land_mode, chr = w->chr;
    uint32_t cap = w->window_cap;
    float pos[3];
    uint32_t ang = w->start_ang_y;
    memcpy(pos, w->start_pos, sizeof pos);
    if (sa1g_world_apply_image(w, w->image) != SA1_OK) {
        return -1;
    }
    sa1g_boot_defaults(&cfg);
    cfg.chr = chr;
    cfg.flags |= SA1G_BOOT_POS;
    memcpy(cfg.pos, pos, sizeof cfg.pos);
    cfg.ang[1] = ang;
    cfg.land_mode = land;
    cfg.window_cap = cap;
    if (sa1g_world_boot(w, &cfg) != SA1_OK) {
        return -1;
    }
    return sa1ca_world_attach(ctx, w, &notes) == SA1_OK ? 0 : -1;
}
static int cq_cast_until(sa1_context *ctx, sa1_character_id id, sa1g_world *w, int from, int upto, int *pair_aim)
{
    int p;
    float yaw = 0.0f;
    *pair_aim = -1;
    for (p = from; p < upto; p++) {
        float sx = 0, sy = 0;
        uint32_t b = 0;
        sa1_camera_state st;
        int q = p - from;
        if (q >= 10 && q < 50) b = SA1_BUTTON_ACTION;
        if (q >= 25 && q < 50) sy = 1.0f;
        if (cq_tick(ctx, id, w, &CQ_CHASE, sx, sy, b, yaw, 1)) {
            return -1;
        }
        if (w->camreq.running) {
            yaw = cq_yaw_of(w);
        }
        cq_state(ctx, id, 1.0f, &st);
        if (*pair_aim < 0 && st.kind == SA1_CAMKIND_AIM && (st.flags & SA1_CAMSTATE_RUNNING)) {
            *pair_aim = q;
        }
    }
    return 0;
}
static void cq_test_t11(const char *gdi)
{
    sa1_context *ctx = cq_make_ctx(gdi, 1, (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SONIC));
    sa1_character_id id = ctx ? cq_make_char(ctx, SA1_CHAR_BIG) : 0;
    sa1g_world *w;
    sa1_camera_state st;
    float cc[3];
    int aim = -1;
    printf("\n== T11: a rebuild mid-cast\n");
    if (!id) {
        CHECK(0, "T11 context");
        return;
    }
    w = sa1_character_world(ctx, id);
    cq_cast_until(ctx, id, w, 0, 100, &aim);
    cq_state(ctx, id, 1.0f, &st);
    CHECK(aim == 23 && st.kind == SA1_CAMKIND_LURE && (st.flags & SA1_CAMSTATE_RUNNING), "T11 before: the cast runs, the lure camera at pair 100 (aim at %d, kind %u)", aim, (unsigned)st.kind);
    CHECK(cq_rebuild(ctx, w) == 0, "T11 the rebuild (%s)", w->dead ? w->fault.msg : "ok");
    cq_state(ctx, id, 1.0f, &st);
    CHECK(st.flags == 0u && w->camreq.running == 0 && w->camreq.drawn[0].fn == 0u && w->camreq.drawn[1].fn == 0u,
          "T11 right after it: no request, not running, no drawn pair (flags %X, running %d)", (unsigned)st.flags, (int)w->camreq.running);
    cq_tick(ctx, id, w, &CQ_CHASE, 0, 0, 0, 0.0f, 1);
    cq_state(ctx, id, 1.0f, &st);
    cq_rv(w, cq_r32(w, CQ_ADDR_CAM_TWP) + 0x20u, cc);
    CHECK(st.flags == 0u && cq_dist(cc, cq_last_eye) < 0.01, "T11 the next pair: no request, the taskwk the host's eye (off by %.4f)", cq_dist(cc, cq_last_eye));
    cq_cast_until(ctx, id, w, 0, 100, &aim);
    cq_state(ctx, id, 1.0f, &st);
    CHECK(aim == 23 && st.kind == SA1_CAMKIND_LURE && (st.flags & SA1_CAMSTATE_RUNNING) && !w->fault.kind, "T11 the next cast works (aim at %d, kind %u, fault %s)", aim, (unsigned)st.kind,
          w->fault.kind ? w->fault.msg : "none");
    /* a re-creation (what an upgrade change does in G-SA1) */
    sa1_character_destroy(ctx, id);
    id = cq_make_char(ctx, SA1_CHAR_BIG);
    w = id ? sa1_character_world(ctx, id) : NULL;
    if (w != NULL) {
        cq_state(ctx, id, 1.0f, &st);
        CHECK(st.flags == 0u, "T11 a re-created character: no request");
        cq_cast_until(ctx, id, w, 0, 100, &aim);
        cq_state(ctx, id, 1.0f, &st);
        CHECK(aim == 23 && st.kind == SA1_CAMKIND_LURE && (st.flags & SA1_CAMSTATE_RUNNING), "T11 ... and its cast works (aim at %d)", aim);
    }
    sa1_shutdown(ctx);
}

/* ---- T12: interpolation ------------------------------------------------------------------------------------------------- */
static int cq_near3(const float a[3], const float b[3], float tol)
{
    return fabsf(a[0] - b[0]) <= tol && fabsf(a[1] - b[1]) <= tol && fabsf(a[2] - b[2]) <= tol;
}
static void cq_test_t12(const char *gdi)
{
    sa1_context *ctx = cq_make_ctx(gdi, 1, (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SONIC));
    sa1_character_id id = ctx ? cq_make_char(ctx, SA1_CHAR_BIG) : 0;
    sa1g_world *w;
    float yaw = 0.0f;
    int p, lerp_ok = 0, lerp_n = 0, cut_n = 0, cut_ok = 0, unit_bad = 0, target_ok = 1;
    printf("\n== T12: interpolation between the drawn pairs\n");
    if (!id) {
        CHECK(0, "T12 context");
        return;
    }
    w = sa1_character_world(ctx, id);
    for (p = 0; p < 215; p++) {
        float sx, sy, h0[3], h1[3], hm[3], m[3];
        uint32_t b;
        sa1_camera_state s0, s5, s1, sn, sneg;
        int k;
        cq_script(p, &sx, &sy, &b);
        if (cq_tick(ctx, id, w, &CQ_CHASE, sx, sy, b, yaw, 1)) {
            break;
        }
        if (w->camreq.running) {
            yaw = cq_yaw_of(w);
        }
        cq_state(ctx, id, 0.0f, &s0);
        cq_state(ctx, id, 0.5f, &s5);
        cq_state(ctx, id, 1.0f, &s1);
        cq_state(ctx, id, (float)nan(""), &sn);
        cq_state(ctx, id, -1.0f, &sneg);
        if (!(s1.flags & SA1_CAMSTATE_RUNNING)) {
            continue;
        }
        cq_to_host(w->camreq.drawn[0].pos, h0);
        cq_to_host(w->camreq.drawn[1].pos, h1);
        for (k = 0; k < 3; k++) {
            m[k] = (w->camreq.drawn[0].pos[k] + w->camreq.drawn[1].pos[k]) * 0.5f;
        }
        cq_to_host(m, hm);
        if (fabsf(s1.forward[0] * s1.forward[0] + s1.forward[1] * s1.forward[1] + s1.forward[2] * s1.forward[2] - 1.0f) > 1e-3f) {
            unit_bad++;
        }
        {
            float ht[3];
            cq_to_host(w->camreq.drawn[1].target, ht);
            if (!cq_near3(s1.target, ht, 0.01f)) {
                target_ok = 0;
            }
        }
        if (s1.flags & SA1_CAMSTATE_CUT) {
            cut_n++;
            cut_ok += cq_near3(s0.position, h1, 0.001f) && cq_near3(s5.position, h1, 0.001f) && cq_near3(s1.position, h1, 0.001f);
        } else {
            lerp_n++;
            lerp_ok += cq_near3(s0.position, h0, 0.01f) && cq_near3(s1.position, h1, 0.01f) && cq_near3(s5.position, hm, 0.01f) && cq_near3(sn.position, h1, 0.01f) &&
                       cq_near3(sneg.position, h0, 0.01f);  /* a whole number of pairs: the accumulator is 0 */
        }
    }
    CHECK(lerp_n > 100 && lerp_ok == lerp_n, "T12 alpha 0 / 0.5 / 1 / NaN / < 0: the drawn pairs and their midpoint (%d of %d pairs)", lerp_ok, lerp_n);
    CHECK(cut_n == 1 && cut_ok == 1, "T12 the CUT pair: the last pair's whatever alpha (%d of %d)", cut_ok, cut_n);
    CHECK(unit_bad == 0, "T12 forward is a unit vector (%d off)", unit_bad);
    CHECK(target_ok, "T12 target = the camera work's");
    sa1_shutdown(ctx);
}

/* ---- T13: invalid calls -------------------------------------------------------------------------------------------------- */
static void cq_test_t13(const char *gdi)
{
    sa1_context *ctx = cq_make_ctx(gdi, 1, (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SONIC));
    sa1_character_id id = ctx ? cq_make_char(ctx, SA1_CHAR_BIG) : 0, lazy = 0, pup = 0;
    sa1g_world *w;
    sa1_camera_state st;
    uint32_t h0, h1;
    sa1_character_desc d;
    printf("\n== T13: invalid calls\n");
    if (!id) {
        CHECK(0, "T13 context");
        return;
    }
    w = sa1_character_world(ctx, id);
    cq_tick(ctx, id, w, &CQ_CHASE, 0, 0, 0, 0.0f, 1);
    h0 = cq_fnv(2166136261u, w->ram, SA1G_ARENA_SIZE);
    memset(&st, 0, sizeof st);
    st.struct_size = sizeof st;
    CHECK(sa1_character_camera(NULL, id, 1.0f, &st) == SA1_ERR_INVALID_ARGUMENT, "T13 no context: SA1_ERR_INVALID_ARGUMENT");
    CHECK(sa1_character_camera(ctx, id, 1.0f, NULL) == SA1_ERR_INVALID_ARGUMENT, "T13 no output: SA1_ERR_INVALID_ARGUMENT");
    st.struct_size = (uint32_t)offsetof(sa1_camera_state, fov);
    CHECK(sa1_character_camera(ctx, id, 1.0f, &st) == SA1_ERR_INVALID_ARGUMENT, "T13 struct_size below fov: SA1_ERR_INVALID_ARGUMENT");
    st.struct_size = sizeof st;
    CHECK(sa1_character_camera(ctx, 12345, 1.0f, &st) == SA1_ERR_NOT_FOUND, "T13 no such character (a preview has no character id): SA1_ERR_NOT_FOUND");
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_BIG;
    if (sa1_character_create_ex(ctx, &d, &lazy) == SA1_OK) {
        CHECK(sa1_character_camera(ctx, lazy, 1.0f, &st) == SA1_ERR_NOT_LOADED, "T13 a character without a world: SA1_ERR_NOT_LOADED");
        sa1_character_destroy(ctx, lazy);
    }
    d.flags = SA1_CREATE_PUPPET;
    if (sa1_character_create_ex(ctx, &d, &pup) == SA1_OK) {
        CHECK(sa1_character_camera(ctx, pup, 1.0f, &st) == SA1_ERR_NOT_LOADED, "T13 a puppet: SA1_ERR_NOT_LOADED");
        sa1_character_destroy(ctx, pup);
    }
    h1 = cq_fnv(2166136261u, w->ram, SA1G_ARENA_SIZE);
    CHECK(h0 == h1, "T13 no arena write");
    sa1_shutdown(ctx);
}

/* ---- T14: deep water ------------------------------------------------------------------------------------------------------ */
static void cq_test_t14(const char *gdi)
{
    /* fish098.md 1 row 9, out_ALWB.txt W: the camera's y at these pairs (water y -5, sea bed y -150) */
    static const struct { int pair; float y; } want[] = { { 100, -3.43f }, { 200, -5.20f }, { 400, -17.98f }, { 800, -55.65f }, { 1200, -94.87f }, { 1600, -126.25f }, { 2999, -126.25f } };
    sa1_context *ctx = cq_make_ctx(gdi, 1, (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SONIC));
    sa1_character_id id = ctx ? cq_make_char(ctx, SA1_CHAR_BIG) : 0;
    sa1g_world *w;
    float yaw = 0.0f;
    int p, off = 0;
    size_t k = 0;
    printf("\n== T14: deep water, 3,000 pairs\n");
    if (!id) {
        CHECK(0, "T14 context");
        return;
    }
    w = sa1_character_world(ctx, id);
    for (p = 0; p < 3000; p++) {
        float sy = 0;
        uint32_t b = 0;
        if (p >= 10 && p < 50) b = SA1_BUTTON_ACTION;
        if (p >= 25 && p < 50) sy = 1.0f;
        if (cq_tick(ctx, id, w, &CQ_CHASE, 0, sy, b, yaw, 1)) {
            break;
        }
        if (w->camreq.running) {
            yaw = cq_yaw_of(w);
        }
        if (k < sizeof want / sizeof want[0] && p == want[k].pair) {
            float c[3];
            cq_rv(w, cq_r32(w, CQ_ADDR_CAM_TWP) + 0x20u, c);
            printf("  pair %d: camera y %.2f (research %.2f)\n", p, (double)c[1], (double)want[k].y);
            off += fabsf(c[1] - want[k].y) > 0.05f;
            k++;
        }
    }
    CHECK(p == 3000 && !w->fault.kind, "T14 3,000 pairs, no fault (%s)", w->fault.kind ? w->fault.msg : "none");
    CHECK(off == 0, "T14 the camera's y as measured (fish098.md part W; %d off)", off);
    sa1_shutdown(ctx);
}

int main(int argc, char **argv)
{
    const char *gdi = getenv("SA1_TEST_GDI"), *sadc = NULL, *out = "build/camreq_test/replay";
    int i;
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            cq_only = argv[++i];
        } else if (!strcmp(argv[i], "--sadc") && i + 1 < argc) {
            sadc = argv[++i];
        } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            out = argv[++i];
        } else {
            gdi = argv[i];
        }
    }
    if (gdi == NULL || !*gdi) {
        printf("usage: camreq_test [--only T1,T2,...] [--sadc DIR] [--out DIR] GDI\n");
        return 2;
    }
    printf("libsa1 %s camreq_test: %d-bit, game code: %s\n", sa1_version_string(), (int)(sizeof(void *) * 8), sa1g_game_info());
    if (cq_want("T1")) {
        if (sadc == NULL) {
            CHECK(0, "T1 needs --sadc");
        } else {
            cq_test_t1(sadc, gdi, out);
        }
    }
    if (cq_want("T2") || cq_want("T3") || cq_want("T4") || cq_want("T5")) {
        if (cq_cast(gdi, &CQ_CHASE, &cq_a) != 0 || cq_cast(gdi, &CQ_DEFAULT, &cq_b) != 0 || cq_cast(gdi, &CQ_NONE, &cq_c) != 0) {
            CHECK(0, "T2-T5 the cast runs");
        } else {
            CHECK(cq_a.faults == 0 && cq_b.faults == 0 && cq_c.faults == 0, "T2-T5 no fault in the three cast runs");
            if (cq_want("T2")) cq_test_t2();
            if (cq_want("T3")) { cq_test_t3(); cq_test_t3b(gdi); }
            if (cq_want("T4")) { cq_test_t4(); cq_test_t4_formula(gdi); }
            if (cq_want("T5")) cq_test_t5();
        }
    }
    if (cq_want("T6")) cq_test_t6(gdi);
    if (cq_want("T7")) cq_test_t7(gdi);
    if (cq_want("T8")) cq_test_t8(gdi);
    if (cq_want("T9")) cq_test_t9(gdi);
    if (cq_want("T10")) cq_test_t10(gdi);
    if (cq_want("T11")) cq_test_t11(gdi);
    if (cq_want("T12")) cq_test_t12(gdi);
    if (cq_want("T13")) cq_test_t13(gdi);
    if (cq_want("T14")) cq_test_t14(gdi);
    printf("DETERMINISM t1 %08X t2 %08X t3 %08X t4 %08X\n", (unsigned)cq_det[0], (unsigned)cq_det[1], (unsigned)cq_det[2], (unsigned)cq_det[3]);
    printf("camreq_test: %d failure(s)\n", g_fail);
    return g_fail ? 1 : 0;
}
