/*
 * follower_test.c - the follower API on GRID (SA1_API_FOLLOWER, round 0.9.8; sadc docs/research/gsa1_098_plan.md 5.3, 6.2,
 * gsa1_096_plan.md 6.1 tasks 9, 10, follow096_lib.md 5.12). Runs on any archive with src/sa1_follower.c's gate open (SA1FO_BUILT 1).
 *
 *   py -3.12 tests/build_follower.py --run [--debug] [--target x86_64-windows] [-- --long] [-- --only NAME]
 *   follower_test [--long] [--only CASE] GDI
 *
 * API notes (what a host needs, the header's section says the rest):
 *   - Make the leader first, sa1_follower_create builds the follower's world at once from the leader's state. Tick every leader before
 *     its followers (chain order); a follower of a libsa1 leader runs the leader's steps whatever dt says, at most 8 pairs a tick.
 *   - A host-fed follower (leader 0): sa1_follower_feed every host tick; it runs on dt with its own accumulator.
 *   - sa1_character_tick on a follower reads camera_yaw, the water bits and the host camera; never stick or buttons.
 *   - The world is the game's 1.5P: CurrentCharacter 0 on stage 0x0100; slot 0 is the proxy, slot 1 the follower.
 *   - sa1_follower_kinds() is 0x0FF (round 0.9.9e: 0x0ED and Eggman, Tikal; FPL says what they do): Super Sonic answers SA1_ERR_UNSUPPORTED with sa1_follower_kind_reason's text.
 *   - SA1_FOLLOW_EV_RESPAWN's arg says why: 0 distance > 1000, 1 off the leader's camera 300 frames, 2 sa1_follower_respawn. The point is 50 units above the camera: the host's eye with
 *     SA1_INPUT_HOST_CAMERA, else the eye 40 behind the leader along camera_yaw. A respawn while riding ends the ride (the board stays kept: the host may ask for ON again).
 *   - A hold parks slot 1 a million units above the leader (AI mode 6, SA1_FINFO_HELD): do not draw it. Off places it 10 behind the leader. A hold on a rider without an OFF keeps it in the
 *     board mode up there (FH4): send the OFF first.
 *   - sa1_character_board on a follower id rides at slot 1; Sonic's board variant is the follower world's own pair parity: read board_variant, never copy the leader's.
 *   - SA1_FOLLOW_EV_ITEM reports what the game gave player 0 (arg 1 invincibility, 5 barrier, 8 magnetic barrier); speed shoes go to the hitter, slot 1, with no event.
 *   - Host water reaches slot 1: the air timer runs; a drowning follower is knocked back once (SA1_EVENT_HURT, the game's player-1 rule), never dies. E-102 has no air timer.
 *   - Faces and orders on a follower id are refused. Emotes act on slot 1 (0.9.9): the call refuses "on the way" (over 30 units from its spot) and SA1_EMOTE_IN_AIR; the AI's output is dropped while one plays or waits. SA1_INPUT_GAME_CAMERA on a follower id is ignored; sa1_character_camera on it reports a request made in that world (never RUNNING).
 *   - sa1_follower_stand (0.9.9a): on = a stopped follower reaches the game's own stand and idle (FST1, FST3); never called = the game's AI: its stick of 0.005 keeps it in its walk at speed 0 on one frame, as the real game's Tails (FST2). E-102's hover keeps its static pose under any stick (FST4).
 *   - sa1_follower_speed: 1 writes nothing; above 1 the k of follow099.md 3.5. sa1_follower_spot: a host-space offset of the proxy (it scales as a point: 64 HU are 16 units), eased 2 units per step, at once before the first step; a lone follower needs none. The fall rule is always on when a spot is set.
 *   - With SA1_FOLLOW_GAME_VIEW the game's visibility test hides slot 1 in 194-258 of 600 `run` pairs; the render output is there anyway, the CPU pose at slot 1. Default: 0 hidden.
 *   - A follower costs about 17.1 MB (64-bit) / 17.5 MB (32-bit) of private memory and about 0.17 ms per pair (-O2, FM2's 99 Knuckles; COST prints the view's share).
 *
 * Cases (follow096_lib.md 5.12 with the plan's additions); a pass value is in each case's CHECK:
 *   FS1 FA1 FA2 FA3 FA4 FW1 FV1 FV2 FT1 FT2 FT3 FP1 FP2 FP3 FC1 FL1 FH1 FH2 FH3 FH4 FD1 FU1 FI1 FN1 FB1 FB2 FB3 FB4 FB5 FB6 FR1 FR2 FR3 FM1 FM2 FX1 FK1 FE1,
 *   round 0.9.9a (N7, sadc build/research/r099a/stop/diagnosis.md): FST1-7, a third DETERMINISM line (DET099A);
 *   FG1 FG2 (a Big and a Tails follower with the fishing C and the camera bit, gsa1_098_plan.md 6.2), BRINGUP (--long, F4: every kind on
 *   the bring-up courses), SOAK (--long: 30,000 random-leader pairs per kind), COST (--long: the view's share of the time);
 *   round 0.9.9 (gsa1_099_plan.md 6.4, follow099.md 9): FSP1-9, FSA1-6, FEM1-3, 5, 6, FEM4 (--long), FCOST (--long), a second DETERMINISM line for the new paths; the combinations of follow099.md 8: FEM7 (speed, a spot's change and a head look during an emote), FSB1 (a board), FSA7 (a chain).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define PSAPI_VERSION 2 /* K32GetProcessMemoryInfo, in kernel32: build_board.py links no psapi */
#include <windows.h>
#include <psapi.h>
#endif

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_world.h"
#include "sa1_capture.h"

static int g_fail, g_long;
static const char *g_only;
static sa1_context *g_ctx;

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

/* --only A or --only A,B,C */
static int want(const char *name)
{
    size_t n = strlen(name);
    const char *q = g_only;
    if (q == NULL) {
        return 1;
    }
    while (*q) {
        const char *c = strchr(q, ',');
        size_t m = c != NULL ? (size_t)(c - q) : strlen(q);
        if (m == n && strncmp(q, name, n) == 0) {
            return 1;
        }
        if (c == NULL) {
            break;
        }
        q = c + 1;
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

/* ---- guest memory --------------------------------------------------------------------------------------------- */
#define A_PLAYER_TWP   SA1G_ADDR_PLAYER_TWP
#define A_PLAYER_PWP   SA1G_ADDR_PLAYER_PWP

static uint32_t rd(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    sa1g_world_rd32(w, g, &v);
    return v;
}
static float rdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = rd(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static uint8_t rd8(const sa1g_world *w, uint32_t g)
{
    return *(const uint8_t *)sa1g_world_ptr(w, g);
}
static int in_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}
static void *gfn(uint32_t dc)
{
    return sa1_fn(SA1_GFN_C(dc), 0u);
}

/* ---- grounds (SA1 space, the NATIVE coordinates: host units are SA1 units) -------------------------------------- */
static sa1_collision_tri *g_tris;
static uint32_t g_ntri, g_cap;

static void tri(const float a[3], const float b[3], const float c[3], uint32_t surface)
{
    sa1_collision_tri *t;
    if (g_ntri >= g_cap) {
        g_cap = g_cap ? g_cap * 2u : 8192u;
        g_tris = (sa1_collision_tri *)realloc(g_tris, g_cap * sizeof *g_tris);
    }
    t = &g_tris[g_ntri++];
    memcpy(t->v[0], a, 12);
    memcpy(t->v[1], b, 12);
    memcpy(t->v[2], c, 12);
    t->surface = surface;
}

static float g_slope_k;
static float ground_y(float x)
{
    return x < 0.0f ? 0.0f : -g_slope_k * x;
}

static void hquad(float x0, float x1, float z0, float z1, float y_fixed, int use_fixed, uint32_t surface)
{
    float y0 = use_fixed ? y_fixed : ground_y(x0), y1 = use_fixed ? y_fixed : ground_y(x1);
    float a[3] = { x0, y0, z0 }, b[3] = { x0, y0, z1 }, c[3] = { x1, y1, z0 }, d[3] = { x1, y1, z1 };
    tri(a, b, c, surface);
    tri(c, b, d, surface);
}

static void wall(float x0, float z0, float x1, float z1, float y0, float y1, uint32_t surface)
{
    float a[3] = { x0, y0, z0 }, b[3] = { x0, y1, z0 }, c[3] = { x1, y0, z1 }, d[3] = { x1, y1, z1 };
    tri(a, b, c, surface);
    tri(c, b, d, surface);
    tri(a, c, b, surface);
    tri(c, d, b, surface);
}

enum { G_FLAT, G_LEDGE, G_PIT, G_GAPS, G_WALL, G_VOID_LEDGE, G_POOL };

static void build_ground(int kind)
{
    float x, z;
    g_ntri = 0;
    g_slope_k = 0.0f;
    for (x = -4096.0f; x < 4096.0f; x += 128.0f) {
        for (z = -4096.0f; z < 4096.0f; z += 128.0f) {
            if (kind == G_LEDGE && x >= 384.0f && x < 640.0f && z >= -256.0f && z < 256.0f) {
                continue;
            }
            if (kind == G_PIT && x >= 256.0f && x < 768.0f && z >= -768.0f && z < 768.0f) {
                continue;
            }
            if (kind == G_GAPS && x >= 128.0f && x < 1536.0f && (((int)((x - 128.0f) / 128.0f) % 6) >= 3)) {
                continue;
            }
            if (kind == G_VOID_LEDGE && !(x < -128.0f)) {
                continue;
            }
            hquad(x, x + 128.0f, z, z + 128.0f, 0.0f, 1, SA1_SURFACE_SOLID);
        }
    }
    if (kind == G_LEDGE) {
        for (x = 384.0f; x < 640.0f; x += 64.0f) {
            for (z = -256.0f; z < 256.0f; z += 64.0f) {
                hquad(x, x + 64.0f, z, z + 64.0f, 60.0f, 1, SA1_SURFACE_SOLID);
            }
        }
        for (z = -256.0f; z < 256.0f; z += 64.0f) {
            wall(384.0f, z, 384.0f, z + 64.0f, 0.0f, 60.0f, SA1_SURFACE_SOLID);
            wall(640.0f, z, 640.0f, z + 64.0f, 0.0f, 60.0f, SA1_SURFACE_SOLID);
        }
        for (x = 384.0f; x < 640.0f; x += 64.0f) {
            wall(x, -256.0f, x + 64.0f, -256.0f, 0.0f, 60.0f, SA1_SURFACE_SOLID);
            wall(x, 256.0f, x + 64.0f, 256.0f, 0.0f, 60.0f, SA1_SURFACE_SOLID);
        }
    }
    if (kind == G_WALL) {
        for (z = -512.0f; z < 512.0f; z += 64.0f) {
            wall(256.0f, z, 256.0f, z + 64.0f, 0.0f, 128.0f, SA1_SURFACE_SOLID);
        }
    }
    if (kind == G_POOL) {
        /* a pool 40 deep: the floor stays at 0 under a water sheet at 40 */
        for (x = 128.0f; x < 640.0f; x += 64.0f) {
            for (z = -256.0f; z < 256.0f; z += 64.0f) {
                hquad(x, x + 64.0f, z, z + 64.0f, 40.0f, 1, SA1_SURFACE_WATER);
            }
        }
    }
}

static void build_slope(float deg, uint32_t surface)
{
    float x, z;
    g_ntri = 0;
    g_slope_k = tanf(deg * 3.14159265f / 180.0f);
    for (x = -512.0f; x < 12288.0f; x += 64.0f) {
        for (z = -1024.0f; z < 1024.0f; z += 64.0f) {
            hquad(x, x + 64.0f, z, z + 64.0f, 0.0f, 0, surface);
        }
    }
}

/* The same triangles as the last time: nothing to do. The GRID a world builds from them takes room in the context that does not come back within a run (a strip is 3 MB: with one per case
   the run's last cases got "no room for 2999656 bytes of GRID collision", no world at all) */
static sa1_collision_tri *g_set_tris;
static uint32_t g_set_ntri;

static int set_ground(void)
{
    sa1_status st;
    if (g_set_tris != NULL && g_set_ntri == g_ntri && memcmp(g_set_tris, g_tris, g_ntri * sizeof *g_tris) == 0) {
        return 0;
    }
    st = sa1_collision_set_static(g_ctx, g_tris, g_ntri);
    if (st != SA1_OK) {
        printf("  sa1_collision_set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(g_ctx));
        g_fail++;
        return -1;
    }
    free(g_set_tris);
    g_set_tris = (sa1_collision_tri *)malloc(g_ntri * sizeof *g_tris);
    if (g_set_tris != NULL) {
        memcpy(g_set_tris, g_tris, g_ntri * sizeof *g_tris);
        g_set_ntri = g_ntri;
    }
    return 0;
}

/* ---- characters --------------------------------------------------------------------------------------------------- */
static const int32_t k_kinds[6] = { SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_SONIC, SA1_CHAR_GAMMA, SA1_CHAR_AMY, SA1_CHAR_BIG };

static const char *kind_name(int32_t k)
{
    switch (k) {
    case SA1_CHAR_SONIC: return "Sonic";
    case SA1_CHAR_TAILS: return "Tails";
    case SA1_CHAR_KNUCKLES: return "Knuckles";
    case SA1_CHAR_AMY: return "Amy";
    case SA1_CHAR_GAMMA: return "E-102";
    case SA1_CHAR_BIG: return "Big";
    case SA1_CHAR_EGGMAN: return "Eggman";
    case SA1_CHAR_TIKAL: return "Tikal";
    default: return "?";
    }
}

static sa1_character_id leader_new(int32_t kind, float x, float y, float z)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_input in;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    st = sa1_character_create_ex(g_ctx, &d, &id);
    if (SA1_FAILED(st)) {
        printf("  FAIL  leader: %s: %s\n", sa1_status_string(st), sa1_last_error(g_ctx));
        g_fail++;       /* a case on no world must not pass by looking at zeros */
        return 0;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    sa1_character_tick(g_ctx, id, &in, 0.0f, NULL, NULL);
    return id;
}

static sa1_status follower_new_ex(int32_t kind, sa1_character_id leader, uint32_t flags, const sa1_leader_state *host,
                                  sa1_character_id *out)
{
    sa1_follower_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.leader = leader;
    d.flags = flags;
    if (host != NULL) {
        d.host_leader = *host;
    }
    return sa1_follower_create(g_ctx, &d, out);
}

static sa1_character_id follower_new(int32_t kind, sa1_character_id leader, uint32_t flags)
{
    sa1_character_id id = 0;
    sa1_status st = follower_new_ex(kind, leader, flags, NULL, &id);
    if (SA1_FAILED(st)) {
        printf("  FAIL  sa1_follower_create(%s): %s: %s\n", kind_name(kind), sa1_status_string(st), sa1_last_error(g_ctx));
        g_fail++;
        return 0;
    }
    return id;
}

static void host_state(sa1_leader_state *s, float x, float y, float z, float vx_per_step, float vz_per_step)
{
    memset(s, 0, sizeof *s);
    s->struct_size = sizeof *s;
    s->position[0] = x;
    s->position[1] = y;
    s->position[2] = z;
    s->velocity[0] = vx_per_step * 60.0f;
    s->velocity[2] = vz_per_step * 60.0f;
    s->flags = SA1_LEADER_ON_GROUND;
    s->kind = -1;
}

typedef struct stats {
    uint32_t faults, unimpl, respawn[3], lost[3], item[9], sounds, board_on, board_off, board_refused;
    int first_respawn_step;
} stats;

static void poll_events(sa1_character_id id, stats *s)
{
    sa1_event ev[128];
    uint32_t n = 0, i;
    while (sa1_poll_events(g_ctx, ev, 128, &n) == SA1_OK && n) {
        for (i = 0; i < n; i++) {
            if (ev[i].type == SA1_EVENT_FAULT && (s != NULL)) {
                s->faults++;
                printf("    FAULT event: %08X in %08X (character %d)\n", (unsigned)ev[i].id, (unsigned)ev[i].arg, (int)ev[i].character);
            }
            if (ev[i].type == SA1_EVENT_UNIMPLEMENTED && (s != NULL)) {
                s->unimpl++;
                printf("    UNIMPLEMENTED event: %08X in %08X (character %d)\n", (unsigned)ev[i].id, (unsigned)ev[i].arg,
                       (int)ev[i].character);
            }
            if (s == NULL || ev[i].character != id) {
                continue;
            }
            if (ev[i].type == SA1_EVENT_FOLLOW) {
                if (ev[i].id == SA1_FOLLOW_EV_RESPAWN && ev[i].arg < 3u) {
                    s->respawn[ev[i].arg]++;
                    if (s->first_respawn_step < 0) {
                        s->first_respawn_step = (int)ev[i].step;
                    }
                } else if (ev[i].id == SA1_FOLLOW_EV_LEADER_LOST && ev[i].arg < 3u) {
                    s->lost[ev[i].arg]++;
                } else if (ev[i].id == SA1_FOLLOW_EV_ITEM && ev[i].arg < 9u) {
                    s->item[ev[i].arg]++;
                }
            } else if (ev[i].type == SA1_EVENT_SOUND) {
                s->sounds++;
            } else if (ev[i].type == SA1_EVENT_BOARD) {
                s->board_on += ev[i].id == SA1_BOARD_EV_ON;
                s->board_off += ev[i].id == SA1_BOARD_EV_OFF;
                s->board_refused += ev[i].id == SA1_BOARD_EV_REFUSED;
            }
        }
        n = 0;
    }
}

static sa1_status tick_input(sa1_character_id id, float sx, float sy, uint32_t buttons, float yaw, float dt)
{
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    in.camera_yaw = yaw;
    return sa1_character_tick(g_ctx, id, &in, dt, NULL, NULL);
}

static void state_of(sa1_character_id id, sa1_state *s)
{
    memset(s, 0, sizeof *s);
    s->struct_size = sizeof *s;
    sa1_character_state(g_ctx, id, s);
}

static void info_of(sa1_character_id id, sa1_follower_info *o)
{
    memset(o, 0, sizeof *o);
    o->struct_size = sizeof *o;
    sa1_follower_info_get(g_ctx, id, o);
}

static float dist_h(const float a[3], const float b[3])
{
    float x = a[0] - b[0], z = a[2] - b[2];
    return sqrtf(x * x + z * z);
}

static float dist_3(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

/* slot 1's mode, action */
static int32_t slot_mode(sa1_character_id id)
{
    const sa1g_world *w = sa1_character_world(g_ctx, id);
    uint32_t t = w != NULL ? rd(w, A_PLAYER_TWP + 4u) : 0u;
    return in_arena(t) ? (int32_t)(int8_t)rd8(w, t) : -1;
}

/* the centre of the render's triangles (no shadow): 0 no triangles */
static int render_centre(sa1_character_id id, float c[3])
{
    const sa1_render *r = NULL;
    double sum[3] = { 0, 0, 0 }, n = 0;
    uint32_t q, t, v;
    if (sa1_character_render(g_ctx, id, 1.0f, &r) != SA1_OK || r == NULL) {
        return 0;
    }
    for (q = 0; q < r->num_draws; q++) {
        const sa1_draw *dd = &r->draws[q];
        if (dd->prim != SA1_PRIM_TRIANGLES || (dd->flags & SA1_DRAW_SHADOW)) {
            continue;
        }
        for (t = dd->first; t < dd->first + dd->count; t++) {
            for (v = 0; v < 9; v++) {
                sum[v % 3] += r->positions[9u * t + v];
            }
        }
        n += 3.0 * dd->count;
    }
    if (n <= 0) {
        return 0;
    }
    c[0] = (float)(sum[0] / n);
    c[1] = (float)(sum[1] / n);
    c[2] = (float)(sum[2] / n);
    return 1;
}

/* ---- leader scripts ------------------------------------------------------------------------------------------------ */
static void script_run(uint32_t p, float *sx, float *sy, uint32_t *b)
{
    *sx = *sy = 0.0f;
    *b = 0;
    if (p >= 30 && p < 150) {
        *sy = 1.0f;
    } else if (p >= 150 && p < 210) {
        *sy = 1.0f;
        *sx = 0.6f;
    } else if (p == 300) {
        *b = SA1_BUTTON_JUMP;
    } else if (p >= 360 && p < 450) {
        *sy = 1.0f;
        if (p == 380 || p == 381 || p == 420 || p == 421) {
            *b = SA1_BUTTON_JUMP;
        }
    }
}

static uint32_t g_lcg;
static uint32_t lcg(void)
{
    g_lcg = g_lcg * 1664525u + 1013904223u;
    return g_lcg >> 8;
}
static float g_soak_sx, g_soak_sy;
static void script_soak(uint32_t p, const float pos[3], float *sx, float *sy, uint32_t *b)
{
    if (p % 45u == 0u) {
        uint32_t r = lcg() % 8u;
        float a = (float)(lcg() % 6283u) / 1000.0f, mag = r < 2 ? 0.0f : r < 4 ? 0.5f : 1.0f;
        g_soak_sx = mag * sinf(a);
        g_soak_sy = mag * cosf(a);
    }
    *sx = g_soak_sx;
    *sy = g_soak_sy;
    *b = 0;
    if (pos[0] * pos[0] + pos[2] * pos[2] > 2500.0f * 2500.0f) {
        float d = sqrtf(pos[0] * pos[0] + pos[2] * pos[2]);
        *sy = -pos[0] / d;
        *sx = -pos[2] / d;
    }
    if (lcg() % 20u == 0u) {
        *b = SA1_BUTTON_JUMP;
    }
    if (lcg() % 97u == 0u) {
        *b |= SA1_BUTTON_ACTION;
    }
}

/* ---- FS1: the production boot against the probe's conversion ----------------------------------------------------------- */
typedef struct conv {
    int32_t kind;
    const sa1g_world *leader;
    uint32_t px_twp, px_mwp, px_pwp, px_csts, ai_tp;
    uint64_t cursor;
} conv;

static void conv_write(sa1g_world *w, const conv *c, const sa1g_snap *s)
{
    uint32_t k, fl;
    for (k = 0; k < 3; k++) {
        uint32_t v;
        memcpy(&v, &s->pos[k], 4);
        sa1g_world_wr32(w, c->px_twp + 0x20u + 4u * k, v);
        sa1g_world_wr32(w, c->px_twp + 0x14u + 4u * k, s->ang[k]);
        memcpy(&v, &s->mspd[k], 4);
        sa1g_world_wr32(w, c->px_mwp + 4u + 4u * k, v);
        memcpy(&v, &s->pspd[k], 4);
        sa1g_world_wr32(w, c->px_pwp + 0x38u + 4u * k, v);
        memcpy(&v, &s->tnorm[k], 4);
        sa1g_world_wr32(w, c->px_csts + 0x1Cu + 4u * k, v);
    }
    fl = rd(w, c->px_twp + 4u);
    sa1g_world_wr32(w, c->px_twp + 4u, (fl & ~3u) | (s->flag & 3u));
}

static void conv_pre(sa1g_world *w, int32_t sub, void *user)
{
    conv *c = (conv *)w->emote.ctx;
    (void)sub;
    (void)user;
    conv_write(w, c, &c->leader->ring[c->cursor % SA1G_RING]);
}

static void conv_post(sa1g_world *w, int32_t sub, void *user)
{
    conv *c = (conv *)w->emote.ctx;
    (void)sub;
    (void)user;
    conv_write(w, c, &c->leader->ring[(c->cursor + 1u) % SA1G_RING]);
    c->cursor++;
}

static void conv_ai(sa1g_world *w, uint32_t tp, void *user)
{
    (void)w;
    (void)user;
    ((void (*)(void *))gfn(0x8C081D4Eu))(sa1_decode(tp));
}

static void conv_cb(sa1g_world *w, void *user)
{
    conv *c = (conv *)user;
    void *(*create)(uint16_t, int32_t, sa1_gfn) = (void *(*)(uint16_t, int32_t, sa1_gfn))gfn(0x8C09852Eu);
    void *(*calloc_fn)(int32_t, int32_t) = (void *(*)(int32_t, int32_t))gfn(0x8C098B0Cu);
    uint32_t tp = w->player_tp, twp = rd(w, tp + 0x20u), mwp = rd(w, tp + 0x24u), pwp, k;
    uint8_t *pp;
    sa1g_snap first;
    sa1g_world_wr32(w, tp + 0x10u, 0x8C098440u);
    pwp = sa1_encode(calloc_fn(1, 0x200));
    pp = (uint8_t *)sa1g_world_ptr(w, pwp);
    for (k = 0; k < 0x84u; k += 4u) {
        memcpy(pp + 0x9Cu + k, sa1g_world_ptr(w, 0x8C1BDFE4u + k), 4);
    }
    sa1g_world_wr32(w, mwp, pwp);
    c->px_csts = sa1_encode(calloc_fn(1, 0x84));
    sa1g_world_wr32(w, pwp + 0x64u, c->px_csts);
    *(uint8_t *)sa1g_world_ptr(w, twp + 8u) = 0;
    *(uint8_t *)sa1g_world_ptr(w, twp + 9u) = 0;
    sa1g_world_wr32(w, SA1G_ADDR_PLAYER_TP, tp);
    sa1g_world_wr32(w, A_PLAYER_TWP, twp);
    sa1g_world_wr32(w, SA1G_ADDR_PLAYER_MWP, mwp);
    sa1g_world_wr32(w, A_PLAYER_PWP, pwp);
    c->px_twp = twp;
    c->px_mwp = mwp;
    c->px_pwp = pwp;
    first = c->leader->ring[0];
    conv_write(w, c, &first);
    *(int16_t *)sa1g_world_ptr(w, SA1G_ADDR_CUR_CHAR) = 0;
    *(int8_t *)sa1g_world_ptr(w, 0x8C78B45Au) = 1;
    if (c->kind == SA1_CHAR_TAILS) {
        void *ai = ((void *(*)(void *))gfn(0x8C081AC0u))(sa1_decode(tp));
        c->ai_tp = ai ? sa1_encode(ai) : 0u;
    } else {
        void *ai = create(2, 0, SA1_GFN_C(0x8C081D4Eu)), *ftp;
        uint32_t atp = ai ? sa1_encode(ai) : 0u, atwp, req;
        if (!atp) {
            return;
        }
        atwp = rd(w, atp + 0x20u);
        sa1g_world_wr32(w, 0x8C78C248u, atp);
        *(uint8_t *)sa1g_world_ptr(w, atwp + 9u) = 2;
        *(uint8_t *)sa1g_world_ptr(w, atwp + 8u) = 1;
        sa1g_world_wr32(w, atp + 0x18u, 0x8C081B66u);
        req = sa1_encode(calloc_fn(1, 12));
        sa1g_world_wr32(w, req, c->kind == SA1_CHAR_SONIC ? 0u : c->kind == SA1_CHAR_KNUCKLES ? 2u : 4u);
        ftp = ((void *(*)(void *))gfn(0x8C083F7Cu))(sa1g_world_ptr(w, req));
        if (ftp) {
            uint32_t ftwp = rd(w, sa1_encode(ftp) + 0x20u);
            ((void (*)(void *, void *, float))gfn(0x8C0826B0u))(sa1g_world_ptr(w, twp), sa1g_world_ptr(w, ftwp + 0x20u), 10.0f);
        }
        *(uint8_t *)sa1g_world_ptr(w, atwp) = 0;
        sa1g_world_wr32(w, 0x8C78C24Cu, 0u);
        c->ai_tp = atp;
    }
}

static int g_fs_soak;

static void test_fs1(void)
{
    static const int32_t kinds[4] = { SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_SONIC, SA1_CHAR_GAMMA };
    int ki;
    printf("FS1 the production boot against the probe's conversion (600 pairs of `run`, slot 1's state pair for pair):\n");
    build_ground(G_FLAT);
    set_ground();
    for (ki = 0; ki < 4; ki++) {
        conv c;
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F, P = 0;
        sa1_character_desc d;
        sa1g_world *pw;
        uint32_t p, bad = 0;
        int first = -1;
        F = follower_new(kinds[ki], L, SA1_FOLLOW_GAME_VIEW | SA1_FOLLOW_GAME_HISTORY | SA1_FOLLOW_NO_SET);
        memset(&c, 0, sizeof c);
        c.kind = kinds[ki];
        c.leader = sa1_character_world(g_ctx, L);
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.kind = kinds[ki];
        d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE;
        memcpy(d.position, c.leader->ring[0].pos, sizeof d.position);
        if (SA1_FAILED(sa1_character_create_ex(g_ctx, &d, &P))) {
            CHECK(0, "FS1 the probe's world: %s", sa1_last_error(g_ctx));
            continue;
        }
        pw = sa1_character_world(g_ctx, P);
        sa1g_world_call(pw, conv_cb, &c);
        pw->emote.ctx = (sa1_context *)&c;
        pw->emote.pre = conv_pre;
        pw->emote.post = conv_post;
        sa1g_world_xhook(pw, 0x8C081D4Eu, conv_ai, NULL);
        g_lcg = 0x12345678u;
        g_soak_sx = g_soak_sy = 0.0f;
        for (p = 0; p < (g_fs_soak ? 30000u : 600u); p++) {
            float sx, sy;
            uint32_t b, k;
            sa1g_player_ptrs a, q;
            sa1_status s1;
            if (g_fs_soak) {
                sa1_state ls;
                state_of(L, &ls);
                script_soak(p, ls.position, &sx, &sy, &b);
            } else {
                script_run(p, &sx, &sy, &b);
            }
            tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            s1 = tick_input(P, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            if (s1 != SA1_OK) {
                printf("    probe's world: %s\n", sa1_last_error(g_ctx));
                break;
            }
            {
                const sa1g_world *fw = sa1_character_world(g_ctx, F);
                memset(&a, 0, sizeof a);
                a.twp = rd(fw, A_PLAYER_TWP + 4u);
                memset(&q, 0, sizeof q);
                q.twp = rd(pw, A_PLAYER_TWP + 4u);
                if (in_arena(a.twp) && in_arena(q.twp)) {
                    for (k = 0; k < 0x2Cu; k += 4u) {
                        if (k >= 8 && k < 0x14) {
                            continue;   /* counter, timer, unions */
                        }
                        if (rd(fw, a.twp + k) != rd(pw, q.twp + k)) {
                            if (bad++ == 0) {
                                first = (int)p;
                                printf("      first difference: pair %u taskwk +0x%02X: production %08X, conversion %08X\n", (unsigned)p,
                                       (unsigned)k, (unsigned)rd(fw, a.twp + k), (unsigned)rd(pw, q.twp + k));
                            }
                            break;
                        }
                    }
                }
            }
            poll_events(0, NULL);
        }
        CHECK(bad == 0, "FS1 %s: %u of 600 pairs differ (first: %d)", kind_name(kinds[ki]), (unsigned)bad, first);
        sa1_character_destroy(g_ctx, P);
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

/* ---- FA1, FA2: following a live Sonic -------------------------------------------------------------------------------- */
static void test_fa1(void)
{
    /* follow096_lib.md 3.1, re-measured on 0.3.0 (prep098/recheck/followlib/out/run_all_030.txt) */
    static const struct { int32_t kind; float mean_dh; uint32_t respawns; } tbl[6] = {
        { SA1_CHAR_TAILS, 61.3f, 1 }, { SA1_CHAR_KNUCKLES, 54.2f, 0 }, { SA1_CHAR_SONIC, 53.5f, 0 },
        { SA1_CHAR_GAMMA, 97.1f, 1 }, { SA1_CHAR_AMY, 131.2f, 1 }, { SA1_CHAR_BIG, 131.1f, 1 },
    };
    int ki;
    printf("FA1 `run`, each kind behind a live Sonic, 600 pairs, the game's view and history (the numbers of follow096_lib.md 3.1):\n");
    build_ground(G_FLAT);
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
        sa1_character_id F = follower_new(tbl[ki].kind, L, SA1_FOLLOW_GAME_VIEW | SA1_FOLLOW_GAME_HISTORY);
        stats s;
        double sum = 0.0;
        float maxd = 0.0f;
        uint32_t p;
        sa1_follower_info oi;
        memset(&s, 0, sizeof s);
        s.first_respawn_step = -1;
        for (p = 0; p < 600u; p++) {
            float sx, sy, d;
            uint32_t b;
            sa1_state ls, fs;
            script_run(p, &sx, &sy, &b);
            tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
            state_of(L, &ls);
            state_of(F, &fs);
            d = dist_h(ls.position, fs.position);
            sum += d;
            maxd = d > maxd ? d : maxd;
        }
        info_of(F, &oi);
        CHECK(s.faults == 0 && s.unimpl == 0, "FA1 %s: 0 faults and unimplemented (%u, %u); sounds %u; AI mode %d", kind_name(tbl[ki].kind),
              (unsigned)s.faults, (unsigned)s.unimpl, (unsigned)s.sounds, (int)oi.ai_mode);
        CHECK(fabs(sum / 600.0 - tbl[ki].mean_dh) <= 0.10 * tbl[ki].mean_dh, "FA1 %s: mean dh %.1f (table %.1f +- 10 %%), max %.1f",
              kind_name(tbl[ki].kind), sum / 600.0, (double)tbl[ki].mean_dh, (double)maxd);
        CHECK(oi.respawns == tbl[ki].respawns, "FA1 %s: respawns %u (distance %u, off-screen %u; table %u)", kind_name(tbl[ki].kind),
              (unsigned)oi.respawns, (unsigned)oi.respawns_distance, (unsigned)oi.respawns_offscreen, (unsigned)tbl[ki].respawns);
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

static void test_fa2(void)
{
    sa1_character_id L, F;
    stats s;
    uint32_t p;
    sa1_status st = SA1_OK;
    printf("FA2 a Sonic follower's first 10 pairs (without the proxy's csts: a fault in step 2):\n");
    build_ground(G_FLAT);
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_SONIC, L, 0);
    memset(&s, 0, sizeof s);
    for (p = 0; p < 10u && st == SA1_OK; p++) {
        tick_input(L, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
        st = tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &s);
    }
    CHECK(st == SA1_OK && s.faults == 0, "FA2: %s, %u faults", sa1_status_string(st), (unsigned)s.faults);
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* ---- FA3, FA4: the Knuckles loop (--long) ------------------------------------------------------------------------------- */
static void soak_run(int32_t kind, uint32_t flags, uint32_t pairs, stats *s, float *mean, float *maxd, uint32_t *hidden)
{
    sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    sa1_character_id F = follower_new(kind, L, flags);
    float lp[3] = { 0.0f, 12.0f, 0.0f };
    double sum = 0.0;
    uint32_t p;
    sa1_follower_info oi;
    memset(s, 0, sizeof *s);
    s->first_respawn_step = -1;
    *maxd = 0.0f;
    *hidden = 0;
    g_lcg = 0x12345678u;
    g_soak_sx = g_soak_sy = 0.0f;
    for (p = 0; p < pairs; p++) {
        float sx, sy, d;
        uint32_t b;
        sa1_state ls, fs;
        script_soak(p, lp, &sx, &sy, &b);
        tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, s);
        state_of(L, &ls);
        state_of(F, &fs);
        memcpy(lp, ls.position, sizeof lp);
        d = dist_h(ls.position, fs.position);
        sum += d;
        *maxd = d > *maxd ? d : *maxd;
        if (p % 300u == 299u) {
            info_of(F, &oi);
            *hidden += oi.hidden_pairs;
        }
    }
    *mean = (float)(sum / pairs);
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

static void test_fa3(void)
{
    stats s;
    float mean, maxd;
    uint32_t hidden;
    printf("FA3, FA4 Knuckles' soak, 30,000 pairs (before: 3,841 respawns; with the rewrite 18):\n");
    build_ground(G_FLAT);
    set_ground();
    soak_run(SA1_CHAR_KNUCKLES, 0, 30000u, &s, &mean, &maxd, &hidden);
    CHECK(s.respawn[0] + s.respawn[1] <= 50u && s.faults == 0 && s.unimpl == 0, "FA3: respawns %u (distance %u, off-screen %u), mean dh %.1f, max %.1f, %u faults",
          (unsigned)(s.respawn[0] + s.respawn[1]), (unsigned)s.respawn[0], (unsigned)s.respawn[1], (double)mean, (double)maxd, (unsigned)s.faults);
    /* The loop needs the ground gone at the old place too: with the window on slot 1 it does not come back on flat ground (reported). FA4 below
       tests the flag itself */
    soak_run(SA1_CHAR_KNUCKLES, SA1_FOLLOW_GAME_HISTORY | SA1_FOLLOW_GAME_VIEW, 30000u, &s, &mean, &maxd, &hidden);
    printf("    the same soak with SA1_FOLLOW_GAME_HISTORY | SA1_FOLLOW_GAME_VIEW: respawns %u, mean dh %.1f, max %.1f (the probe, with the window on the proxy: 3,841)\n",
           (unsigned)(s.respawn[0] + s.respawn[1]), (double)mean, (double)maxd);
}

/* the position history a respawn leaves: frame n of slot 1 (func_8C07CE82's arrays B_8C78C688, B_8C78C690) */
static void history_of(const sa1g_world *w, uint32_t frame, float p[3])
{
    uint32_t ring = rd8(w, 0x8C78C688u + 1u), idx = (ring - frame) & 255u, g = 0x8C78C690u + idx * 0x60u + 12u;
    p[0] = rdf(w, g);
    p[1] = rdf(w, g + 4u);
    p[2] = rdf(w, g + 8u);
}

static void test_fa4(void)
{
    static const uint32_t flags[2] = { 0u, SA1_FOLLOW_GAME_HISTORY };
    int fi;
    printf("FA4 the history a respawn leaves: every frame at the new place by default, frame 0 only with SA1_FOLLOW_GAME_HISTORY (Knuckles):\n");
    build_ground(G_FLAT);
    set_ground();
    for (fi = 0; fi < 2; fi++) {
        sa1_leader_state hs;
        sa1_character_id F = 0;
        uint32_t p;
        stats s;
        memset(&s, 0, sizeof s);
        host_state(&hs, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        follower_new_ex(SA1_CHAR_KNUCKLES, 0, flags[fi], &hs, &F);
        for (p = 0; p < 60u; p++) {
            sa1_follower_feed(g_ctx, F, &hs);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
        }
        host_state(&hs, 1500.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        sa1_follower_feed(g_ctx, F, &hs);
        for (p = 0; p < 2u && s.respawn[0] == 0u; p++) {
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
        }
        {
            const sa1g_world *w = sa1_character_world(g_ctx, F);
            float h[3];
            history_of(w, 100u, h);
            if (fi == 0) {
                CHECK(s.respawn[0] == 1u && h[1] > 40.0f, "FA4 default: EV_RESPAWN x%u, history frame 100 at y %.1f (the new place: 57)", (unsigned)s.respawn[0], (double)h[1]);
            } else {
                CHECK(s.respawn[0] == 1u && h[1] < 20.0f, "FA4 SA1_FOLLOW_GAME_HISTORY: EV_RESPAWN x%u, history frame 100 at y %.1f (the old place: the ground)", (unsigned)s.respawn[0], (double)h[1]);
            }
        }
        sa1_character_destroy(g_ctx, F);
    }
}

/* ---- FW1: the window on slot 1 ------------------------------------------------------------------------------------------ */
static void test_fw1(void)
{
    static const int32_t kinds[2] = { SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES };
    int ki;
    printf("FW1 `outrun` (a host-fed leader at 6 units per step): the ground stays under slot 1 (before: it ended at dh 531 / 529):\n");
    build_ground(G_FLAT);
    set_ground();
    for (ki = 0; ki < 2; ki++) {
        sa1_leader_state hs;
        sa1_character_id F = 0;
        uint32_t p, below = 0;
        float at_dh = 0.0f, maxdh = 0.0f;
        host_state(&hs, -3500.0f, 0.0f, 0.0f, 6.0f, 0.0f);
        follower_new_ex(kinds[ki], 0, 0, &hs, &F);
        for (p = 0; p < 300u; p++) {
            sa1_state fs;
            float d;
            host_state(&hs, -3500.0f + 6.0f * 2.0f * (float)p, 0.0f, 0.0f, 6.0f, 0.0f);
            sa1_follower_feed(g_ctx, F, &hs);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
            state_of(F, &fs);
            d = dist_h(hs.position, fs.position);
            maxdh = d > maxdh ? d : maxdh;
            if (fs.position[1] < -1.0f && d < 1000.0f && below++ == 0) {
                at_dh = d;
            }
        }
        CHECK(below == 0, "FW1 %s: pairs below y -1 while dh < 1,000: %u (first at dh %.1f); max dh %.1f", kind_name(kinds[ki]), (unsigned)below,
              (double)at_dh, (double)maxdh);
        sa1_character_destroy(g_ctx, F);
    }
}

/* ---- FV1, FV2: the render at slot 1 ------------------------------------------------------------------------------------- */
static void test_fv(uint32_t flags, const char *name)
{
    static const int32_t kinds[4] = { SA1_CHAR_TAILS, SA1_CHAR_SONIC, SA1_CHAR_KNUCKLES, SA1_CHAR_BIG };
    int ki;
    printf("%s `run`: the follower id's render at slot 1 in every pair (before: at the leader in 258 / 194 / 219 / 230 of 600):\n", name);
    build_ground(G_FLAT);
    set_ground();
    for (ki = 0; ki < 4; ki++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
        sa1_character_id F = follower_new(kinds[ki], L, flags);
        uint32_t p, at_f = 0, other = 0, none = 0, state_bad = 0, hidden = 0;
        for (p = 0; p < 600u; p++) {
            float sx, sy, c[3];
            uint32_t b, tw;
            sa1_state fs;
            const sa1g_world *w;
            sa1_follower_info oi;
            script_run(p, &sx, &sy, &b);
            tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
            state_of(F, &fs);
            w = sa1_character_world(g_ctx, F);
            tw = rd(w, A_PLAYER_TWP + 4u);
            if (in_arena(tw) && (rdf(w, tw + 0x20u) != fs.position[0] || rdf(w, tw + 0x28u) != fs.position[2])) {
                state_bad++;
            }
            if (render_centre(F, c)) {
                if (dist_3(c, fs.position) < 30.0f) {
                    at_f++;
                } else {
                    other++;
                }
            } else {
                none++;
            }
            if (p % 300u == 299u) {
                info_of(F, &oi);
                hidden += oi.hidden_pairs;
            }
        }
        /* a respawn's pair: the body was drawn at sub step 0, the state stands at the new place */
        {
            sa1_follower_info fi;
            info_of(F, &fi);
            CHECK(at_f + fi.respawns >= 600u && state_bad == 0,
                  "%s %s: render at slot 1 %u, elsewhere %u (respawns %u), none %u; sa1_character_state = slot 1 (%u differ); hidden pairs %u", name,
                  kind_name(kinds[ki]), (unsigned)at_f, (unsigned)other, (unsigned)fi.respawns, (unsigned)none, (unsigned)state_bad, (unsigned)hidden);
        }
        if (!(flags & SA1_FOLLOW_GAME_VIEW)) {
            CHECK(hidden == 0, "%s %s: the view behind slot 1: 0 hidden pairs (%u)", name, kind_name(kinds[ki]), (unsigned)hidden);
        }
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

/* ---- FT1, FT2, FT3 ---------------------------------------------------------------------------------------------------------- */
static void test_ft1(void)
{
    int ki;
    printf("FT1 the leader teleports by 1,510 units at pair 60:\n");
    build_ground(G_FLAT);
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        sa1_leader_state hs;
        sa1_character_id F = 0;
        stats s;
        uint32_t p;
        int ev_pair = -1;
        float py = 0.0f;
        int32_t mode_after = -1;
        memset(&s, 0, sizeof s);
        s.first_respawn_step = -1;
        host_state(&hs, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        follower_new_ex(k_kinds[ki], 0, 0, &hs, &F);
        for (p = 0; p < 100u; p++) {
            uint32_t before = s.respawn[0];
            sa1_state fs;
            host_state(&hs, p < 60u ? 0.0f : 1500.0f, 0.0f, p < 60u ? 0.0f : 0.0f, 0.0f, 0.0f);
            sa1_follower_feed(g_ctx, F, &hs);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
            state_of(F, &fs);
            if (before == 0u && s.respawn[0] != 0u) {
                ev_pair = (int)p;
                py = fs.position[1];
                mode_after = slot_mode(F);
            }
        }
        CHECK(s.respawn[0] == 1u && ev_pair >= 60 && ev_pair <= 61 && fabsf(py - 57.0f) < 8.0f,
              "FT1 %s: EV_RESPAWN arg 0 x%u at pair %d (the teleport at 60); y %.1f (camera + 50 = 57); mode %d", kind_name(k_kinds[ki]),
              (unsigned)s.respawn[0], ev_pair, (double)py, (int)mode_after);
        if (k_kinds[ki] == SA1_CHAR_TAILS) {
            CHECK(mode_after == 15, "FT1 Tails flies in: mode %d", (int)mode_after);
        }
        sa1_character_destroy(g_ctx, F);
    }
}

static void test_ft23(void)
{
    static const int32_t kinds[3] = { SA1_CHAR_TAILS, SA1_CHAR_SONIC, SA1_CHAR_KNUCKLES };
    int ki;
    printf("FT2, FT3 the leader on a 60-unit block (Tails flies up, the others by the off-screen respawn):\n");
    build_ground(G_LEDGE);
    set_ground();
    for (ki = 0; ki < 3; ki++) {
        sa1_leader_state hs;
        sa1_character_id F = 0;
        stats s;
        uint32_t p;
        int on_top = -1;
        memset(&s, 0, sizeof s);
        s.first_respawn_step = -1;
        host_state(&hs, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        follower_new_ex(kinds[ki], 0, 0, &hs, &F);
        for (p = 0; p < 300u; p++) {
            sa1_state fs;
            host_state(&hs, p < 30u ? 0.0f : 512.0f, p < 30u ? 0.0f : 60.0f, 0.0f, 0.0f, 0.0f);
            sa1_follower_feed(g_ctx, F, &hs);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
            state_of(F, &fs);
            if (on_top < 0 && fs.position[1] > 55.0f && fs.position[1] < 70.0f && dist_h(fs.position, hs.position) < 40.0f
                && (fs.flags & SA1_STATE_ON_GROUND)) {
                on_top = (int)p - 30;
            }
        }
        if (kinds[ki] == SA1_CHAR_TAILS) {
            CHECK(s.respawn[0] + s.respawn[1] == 0u && on_top >= 0 && on_top <= 120, "FT2 Tails: no respawn (%u), on the top after %d pairs (at most 120)",
                  (unsigned)(s.respawn[0] + s.respawn[1]), on_top);
        } else {
            CHECK(s.respawn[1] == 1u && on_top >= 0 && on_top <= 200, "FT3 %s: EV_RESPAWN arg 1 x%u, on the top after %d pairs (at most 200)", kind_name(kinds[ki]),
                  (unsigned)s.respawn[1], on_top);
        }
        sa1_character_destroy(g_ctx, F);
    }
}

/* ---- FP1, FP2, FP3 -------------------------------------------------------------------------------------------------------- */
static uint64_t world_steps(sa1_character_id id)
{
    const sa1g_world *w = sa1_character_world(g_ctx, id);
    return w != NULL ? w->steps : 0u;
}

static void test_fp(void)
{
    sa1_character_id L, F;
    stats s;
    sa1_follower_info oi;
    static const float dts[4] = { 0.0f, 1.0f / 30.0f, 4.0f / 30.0f, 9.0f / 30.0f };
    uint32_t i, bad = 0;
    printf("FP1 pairs slaved (the leader with dt 0, 1/30, 4/30, 9/30; the follower with arbitrary dt):\n");
    build_ground(G_FLAT);
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_TAILS, L, 0);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 40u; i++) {
        uint64_t l0 = world_steps(L), f0 = world_steps(F), dl, df;
        tick_input(L, 0, 1.0f, 0, 0.0f, dts[i % 4u]);
        tick_input(F, 0, 0, 0, 0.0f, (float)(i % 3u) * 0.05f);
        dl = world_steps(L) - l0;
        df = world_steps(F) - f0;
        if (dl != df) {
            bad++;
        }
        poll_events(F, &s);
    }
    info_of(F, &oi);
    CHECK(bad == 0 && oi.steps_behind == 0u && oi.steps_dropped == 0u && s.lost[2] == 0u, "FP1: the follower's steps = the leader's in %u of 40 ticks, behind %u, dropped %u",
          (unsigned)(40u - bad), (unsigned)oi.steps_behind, (unsigned)oi.steps_dropped);
    /* the backlog: the leader 3 ticks of 8 pairs with no follower tick: the ring (16 steps) overruns */
    tick_input(L, 0, 1.0f, 0, 0.0f, 8.0f / 30.0f);
    tick_input(L, 0, 1.0f, 0, 0.0f, 8.0f / 30.0f);
    tick_input(L, 0, 1.0f, 0, 0.0f, 8.0f / 30.0f);
    {
        uint64_t f0 = world_steps(F);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &s);
        info_of(F, &oi);
        CHECK(world_steps(F) - f0 == 16u && oi.steps_dropped == 32u && oi.steps_behind == 0u && s.lost[2] == 1u,
              "FP1 overrun: 16 steps run, %u dropped, EV_LEADER_LOST arg 2 x%u, behind %u", (unsigned)oi.steps_dropped, (unsigned)s.lost[2], (unsigned)oi.steps_behind);
    }
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);

    printf("FP2 the follower ticked before its leader in every frame:\n");
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_TAILS, L, 0);
    memset(&s, 0, sizeof s);
    bad = 0;
    for (i = 0; i < 60u; i++) {
        uint64_t f0 = world_steps(F), df;
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(L, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
        df = world_steps(F) - f0;
        if ((i == 0u && df != 0u) || (i > 0u && df != 2u)) {
            bad++;
        }
        poll_events(F, &s);
    }
    info_of(F, &oi);
    CHECK(bad == 0 && oi.steps_dropped == 0u && oi.steps_behind == 2u && s.lost[2] == 0u, "FP2: one pair late, never a drop or a doubled step (%u wrong ticks, %u dropped, behind %u)",
          (unsigned)bad, (unsigned)oi.steps_dropped, (unsigned)oi.steps_behind);
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);

    printf("FP3 sa1_follower_set_leader to a leader made in the same frame (review R.16):\n");
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_TAILS, L, 0);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 30u; i++) {
        tick_input(L, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    }
    poll_events(F, &s);
    {
        sa1_character_id L2 = leader_new(SA1_CHAR_KNUCKLES, 0.0f, 12.0f, 40.0f);
        uint64_t f0;
        sa1_status st = sa1_follower_set_leader(g_ctx, F, L2);
        tick_input(L2, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
        f0 = world_steps(F);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &s);
        info_of(F, &oi);
        CHECK(st == SA1_OK && s.lost[0] + s.lost[1] + s.lost[2] == 0u && oi.steps_dropped == 0u && world_steps(F) - f0 == 2u && oi.leader == L2,
              "FP3: no EV_LEADER_LOST (%u), dropped %u, the first pair ran (%u steps), leader %d", (unsigned)(s.lost[0] + s.lost[1] + s.lost[2]), (unsigned)oi.steps_dropped,
              (unsigned)(world_steps(F) - f0), (int)oi.leader);
        sa1_character_destroy(g_ctx, L2);
    }
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* ---- FC1, FL1 ------------------------------------------------------------------------------------------------------------- */
static void test_fc1(void)
{
    sa1_character_id L, K, T;
    stats s;
    uint32_t p;
    float mk = 0.0f, mt = 0.0f;
    printf("FC1 a chain: Tails follows a Knuckles follower that follows Sonic:\n");
    build_ground(G_FLAT);
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    K = follower_new(SA1_CHAR_KNUCKLES, L, 0);
    T = follower_new(SA1_CHAR_TAILS, K, 0);
    memset(&s, 0, sizeof s);
    for (p = 0; p < 400u; p++) {
        float sx, sy;
        uint32_t b;
        sa1_state ls, ks, ts;
        script_run(p, &sx, &sy, &b);
        tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
        tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(T, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(T, &s);
        state_of(L, &ls);
        state_of(K, &ks);
        state_of(T, &ts);
        mk += dist_h(ls.position, ks.position);
        mt += dist_h(ks.position, ts.position);
    }
    CHECK(s.faults == 0 && mk / 400.0f < 200.0f && mt / 400.0f < 200.0f, "FC1: both follow: mean dh Knuckles-Sonic %.1f, Tails-Knuckles %.1f; %u faults", (double)(mk / 400.0f),
          (double)(mt / 400.0f), (unsigned)s.faults);
    {
        sa1_status st = sa1_follower_set_leader(g_ctx, K, T);
        CHECK(st == SA1_ERR_INVALID_ARGUMENT, "FC1 a cycle is refused: %s (%s)", sa1_status_string(st), sa1_last_error(g_ctx));
        st = sa1_follower_set_leader(g_ctx, K, K);
        CHECK(st == SA1_ERR_INVALID_ARGUMENT, "FC1 itself is refused: %s", sa1_status_string(st));
    }
    sa1_character_destroy(g_ctx, T);
    sa1_character_destroy(g_ctx, K);
    sa1_character_destroy(g_ctx, L);
}

static void fatal_cb(sa1g_world *w, void *user)
{
    (void)w;
    (void)user;
    sa1g_fatal(0, "follower_test: this world dies here");
}

static void test_fl1(void)
{
    sa1_character_id L, F;
    stats s;
    sa1_follower_info oi;
    sa1_state a, b;
    uint32_t i;
    printf("FL1 the leader destroyed, faulted:\n");
    build_ground(G_FLAT);
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_TAILS, L, 0);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 60u; i++) {
        tick_input(L, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    }
    state_of(L, &a);
    sa1_character_destroy(g_ctx, L);
    for (i = 0; i < 150u; i++) {
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &s);
    }
    info_of(F, &oi);
    state_of(F, &b);
    CHECK(s.lost[0] == 1u && (oi.flags & SA1_FINFO_LEADER_LOST) && (oi.flags & SA1_FINFO_HOST_FED) && oi.leader == 0 && dist_h(a.position, b.position) < 30.0f,
          "FL1 destroyed: EV_LEADER_LOST arg 0 x%u, flags %X, the follower stands %.1f units from the last snapshot", (unsigned)s.lost[0], (unsigned)oi.flags,
          (double)dist_h(a.position, b.position));
    sa1_character_destroy(g_ctx, F);

    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_TAILS, L, 0);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 30u; i++) {
        tick_input(L, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    }
    sa1g_world_call(sa1_character_world(g_ctx, L), fatal_cb, NULL);
    for (i = 0; i < 10u; i++) {
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &s);
    }
    info_of(F, &oi);
    CHECK(s.lost[1] == 1u && (oi.flags & SA1_FINFO_LEADER_LOST), "FL1 faulted: EV_LEADER_LOST arg 1 x%u, flags %X", (unsigned)s.lost[1], (unsigned)oi.flags);
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* ---- FH1: host-fed against the internal feed ----------------------------------------------------------------------------- */
static void test_fh1(void)
{
    sa1_leader_state hs;
    sa1_character_id A = 0, B = 0;
    uint32_t p;
    float worst = 0.0f;
    printf("FH1 host-fed through sa1_follower_feed along `outrun`, against the same path written to the proxy directly:\n");
    build_ground(G_FLAT);
    set_ground();
    host_state(&hs, -3500.0f, 0.0f, 0.0f, 3.0f, 0.0f);
    follower_new_ex(SA1_CHAR_TAILS, 0, 0, &hs, &A);
    follower_new_ex(SA1_CHAR_TAILS, 0, 0, &hs, &B);
    for (p = 0; p < 300u; p++) {
        sa1g_world *bw = sa1_character_world(g_ctx, B);
        sa1_state a, b;
        float d;
        sa1g_snap sn;
        uint32_t k;
        host_state(&hs, -3500.0f + 3.0f * 2.0f * (float)p, 0.0f, 0.0f, 3.0f, 0.0f);
        sa1_follower_feed(g_ctx, A, &hs);
        memset(&sn, 0, sizeof sn);
        for (k = 0; k < 3; k++) {
            sn.pos[k] = hs.position[k];
            sn.mspd[k] = hs.velocity[k] / 60.0f;
        }
        sn.pspd[0] = 3.0f;
        sn.flag = 1;
        sn.tnorm[1] = 1.0f;
        sn.jog_speed = rdf(bw, 0x8C1BDFE4u + 0x24u);
        sn.run_speed = rdf(bw, 0x8C1BDFE4u + 0x28u);
        sn.valid = 1;
        bw->fol.host_pre = sn;
        bw->fol.host_mid = sn;
        tick_input(A, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(B, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
        state_of(A, &a);
        state_of(B, &b);
        d = dist_3(a.position, b.position);
        worst = d > worst ? d : worst;
    }
    CHECK(worst <= 0.01f, "FH1: slot 1 within 0.01 units over 300 pairs: worst %.4f", (double)worst);
    sa1_character_destroy(g_ctx, A);
    sa1_character_destroy(g_ctx, B);
}

/* ---- FH2, FH4: the hold ------------------------------------------------------------------------------------------------------- */
static void test_fh2(void)
{
    sa1_character_id L, F;
    uint32_t i;
    sa1_follower_info oi;
    sa1_state fs, ls;
    int mode6_at = -1, mode4 = 0;
    printf("FH2 sa1_follower_hold on / off, and the follower follows again (review R.5):\n");
    build_ground(G_FLAT);
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_TAILS, L, 0);
    for (i = 0; i < 30u; i++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
    }
    CHECK(sa1_follower_hold(g_ctx, F, 1) == SA1_OK, "FH2 hold on");
    for (i = 0; i < 4u; i++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
        info_of(F, &oi);
        if (oi.ai_mode == 6 && mode6_at < 0) {
            mode6_at = (int)i + 1;
        }
    }
    CHECK(mode6_at >= 1 && mode6_at <= 2 && (oi.flags & SA1_FINFO_HELD), "FH2 AI mode 6 after %d pairs (at most 2), SA1_FINFO_HELD %X", mode6_at, (unsigned)oi.flags);
    state_of(F, &fs);
    state_of(L, &ls);
    printf("    held: slot 1 stands %.0f above its leader (review R.15: the host draws it elsewhere)\n", (double)(fs.position[1] - ls.position[1]));
    CHECK(sa1_follower_hold(g_ctx, F, 0) == SA1_OK, "FH2 hold off");
    for (i = 0; i < 2u; i++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
    }
    info_of(F, &oi);
    state_of(F, &fs);
    state_of(L, &ls);
    CHECK(!(oi.flags & SA1_FINFO_HELD) && dist_h(fs.position, ls.position) <= 12.0f && fabsf(fs.position[1] - ls.position[1]) < 40.0f,
          "FH2 off: within 12 units of the point behind the leader: dh %.1f, dy %.1f, AI mode %d", (double)dist_h(fs.position, ls.position),
          (double)(fs.position[1] - ls.position[1]), (int)oi.ai_mode);
    for (i = 0; i < 120u; i++) {
        tick_input(L, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
        info_of(F, &oi);
        mode4 += oi.ai_mode == 4;
    }
    state_of(F, &fs);
    state_of(L, &ls);
    CHECK(oi.ai_mode == 4 && dist_h(fs.position, ls.position) < 400.0f, "FH2 follows again: AI mode %d (%d of 120 pairs in 4), dh %.1f", (int)oi.ai_mode, mode4,
          (double)dist_h(fs.position, ls.position));
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* ---- FR1, FR2, FR3 ---------------------------------------------------------------------------------------------------------- */
static void test_fr(void)
{
    sa1_leader_state hs;
    sa1_character_id F = 0;
    stats s;
    uint32_t i;
    sa1_state fs;
    static const float yaws[3] = { 0.0f, 1.5707963f, 3.1415927f };
    int y;
    printf("FR1, FR2, FR3 the game's respawn request, the camera yaw, the host camera:\n");
    build_ground(G_FLAT);
    set_ground();
    host_state(&hs, 100.0f, 0.0f, 200.0f, 0.0f, 0.0f);
    follower_new_ex(SA1_CHAR_TAILS, 0, 0, &hs, &F);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 20u; i++) {
        sa1_follower_feed(g_ctx, F, &hs);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &s);
    }
    CHECK(sa1_follower_respawn(g_ctx, F) == SA1_OK, "FR1 sa1_follower_respawn");
    tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    poll_events(F, &s);
    state_of(F, &fs);
    CHECK(s.respawn[2] == 1u && fs.position[1] > 40.0f, "FR1: EV_RESPAWN arg 2 x%u, y %.1f (above the camera)", (unsigned)s.respawn[2], (double)fs.position[1]);
    for (y = 0; y < 3; y++) {
        for (i = 0; i < 20u; i++) {
            sa1_follower_feed(g_ctx, F, &hs);
            tick_input(F, 0, 0, 0, yaws[y], 1.0f / 30.0f);
            poll_events(F, &s);
        }
        sa1_follower_respawn(g_ctx, F);
        tick_input(F, 0, 0, 0, yaws[y], 1.0f / 30.0f);
        poll_events(F, &s);
        state_of(F, &fs);
        {
            /* the eye 40 behind the proxy along the camera's forward (sa1_world_tick.c: forward = (-sin c, 0, -cos c), c = yaw - 0x4000 in BAMS: (cos yaw, 0, -sin yaw)) */
            float ex = hs.position[0] - 40.0f * cosf(yaws[y]), ez = hs.position[2] + 40.0f * sinf(yaws[y]);
            float dx = fs.position[0] - ex, dz = fs.position[2] - ez, d = sqrtf(dx * dx + dz * dz);
            CHECK(d < 6.0f, "FR2 yaw %.0f degrees: the respawn point is %.1f units from the eye 40 behind the leader along it", (double)yaws[y] * 57.29578, (double)d);
        }
    }
    {
        sa1_input in;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.host_flags = SA1_INPUT_HOST_CAMERA;
        in.host_cam_pos[0] = 300.0f;
        in.host_cam_pos[1] = 25.0f;
        in.host_cam_pos[2] = -150.0f;
        for (i = 0; i < 20u; i++) {
            sa1_follower_feed(g_ctx, F, &hs);
            sa1_character_tick(g_ctx, F, &in, 1.0f / 30.0f, NULL, NULL);
            poll_events(F, &s);
        }
        sa1_follower_respawn(g_ctx, F);
        sa1_character_tick(g_ctx, F, &in, 1.0f / 30.0f, NULL, NULL);
        poll_events(F, &s);
        state_of(F, &fs);
        CHECK(fabsf(fs.position[0] - 300.0f) < 6.0f && fabsf(fs.position[1] - 75.0f) < 6.0f && fabsf(fs.position[2] + 150.0f) < 6.0f,
              "FR3 the host camera: the respawn point (%.1f %.1f %.1f) = the camera + 50 Y (300 75 -150)", (double)fs.position[0], (double)fs.position[1], (double)fs.position[2]);
    }
    sa1_character_destroy(g_ctx, F);
}

/* ---- FK1, FE1 --------------------------------------------------------------------------------------------------------------- */
static void test_fk_fe(void)
{
    uint32_t kinds = sa1_follower_kinds();
    int32_t k;
    sa1_character_id L, F, x = 0;
    sa1_status st;
    sa1_emote em;
    sa1_state a, b;
    printf("FK1, FE1 the kinds mask, the refusals:\n");
    for (k = 0; k < SA1_CHAR_COUNT; k++) {
        int on = (kinds >> k) & 1;
        const char *why = sa1_follower_kind_reason(k);
        CHECK(on ? why == NULL : why != NULL, "FK1 kind %d: bit %d, reason %s", (int)k, on, why ? why : "(none)");
    }
    build_ground(G_FLAT);
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    st = follower_new_ex(SA1_CHAR_SUPER_SONIC, L, 0, NULL, &x);
    CHECK(st == SA1_ERR_UNSUPPORTED && x == 0, "FK1 kind 8: %s (%s)", sa1_status_string(st), sa1_last_error(g_ctx));
    /* round 0.9.9e: Eggman and Tikal can follow (FPL has what they do); only kind 8 and what is no character are refused */
    CHECK(((kinds >> SA1_CHAR_EGGMAN) & 1u) && ((kinds >> SA1_CHAR_TIKAL) & 1u) && sa1_follower_kind_reason(SA1_CHAR_EGGMAN) == NULL && sa1_follower_kind_reason(SA1_CHAR_TIKAL) == NULL,
          "FK1 Eggman and Tikal have their bits and no reason: 0x%X", (unsigned)kinds);
    F = follower_new(SA1_CHAR_SONIC, L, 0);
    for (k = 0; k < 120; k++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    }
    poll_events(0, NULL);
    /* round 0.9.9: the emote on a follower id, refused only by the rules (FEM3 has each rule); faces and orders as before, with today's text */
    memset(&em, 0, sizeof em);
    em.struct_size = sizeof em;
    st = sa1_character_emote(g_ctx, F, &em);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT, "FE1 emote, an empty list: the syntax: %s (%s)", sa1_status_string(st), sa1_last_error(g_ctx));
    em.count = 1;
    em.items[0].source = SA1_EMOTE_ROW;
    em.items[0].index = 75u;
    em.link = -1;
    st = sa1_character_emote(g_ctx, F, &em);
    CHECK(st == SA1_OK, "FE1 emote: %s (%s)", sa1_status_string(st), st == SA1_OK ? "ok" : sa1_last_error(g_ctx));
    st = sa1_character_emote_stop(g_ctx, F, SA1_EMOTE_STOP_NOW);
    CHECK(st == SA1_OK, "FE1 emote stop: %s", sa1_status_string(st));
    state_of(F, &a);
    st = sa1_character_face(g_ctx, F, "A", 0u);
    CHECK(st == SA1_ERR_UNSUPPORTED && strstr(sa1_last_error(g_ctx), "followers do not take") != NULL, "FE1 face: %s (%s)", sa1_status_string(st), sa1_last_error(g_ctx));
    st = sa1_character_face_mode(g_ctx, F, 1u);
    CHECK(st == SA1_ERR_UNSUPPORTED, "FE1 face mode: %s", sa1_status_string(st));
    st = sa1_character_face_level(g_ctx, F, 0.5f, 0u);
    CHECK(st == SA1_ERR_UNSUPPORTED, "FE1 face level: %s", sa1_status_string(st));
    st = sa1_character_order(g_ctx, F, SA1_ORDER_SUPER);
    CHECK(st == SA1_ERR_UNSUPPORTED && strstr(sa1_last_error(g_ctx), "followers do not take") != NULL, "FE1 order: %s (%s)", sa1_status_string(st), sa1_last_error(g_ctx));
    state_of(F, &b);
    CHECK(memcmp(&a, &b, sizeof a) == 0, "FE1 the world is unchanged");
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* ---- FD1, FU1 ------------------------------------------------------------------------------------------------------------------ */
static void test_fd1(void)
{
    sa1_character_id L, F;
    sa1_damage dm;
    uint32_t i, hurt = 0, n = 0;
    sa1_event ev[64];
    const sa1g_world *w;
    uint32_t t1, wt = 0;
    int32_t m;
    printf("FD1 sa1_character_damage on a follower hits slot 1 (the game's player-1 rule: knocked back, no ring scatter):\n");
    build_ground(G_FLAT);
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_TAILS, L, 0);
    for (i = 0; i < 40u; i++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
    }
    memset(&dm, 0, sizeof dm);
    dm.struct_size = sizeof dm;
    dm.object = 777u;
    CHECK(sa1_character_damage(g_ctx, F, &dm) == SA1_OK, "FD1 the request");
    tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    while (sa1_poll_events(g_ctx, ev, 64u, &n) == SA1_OK && n) {
        for (i = 0; i < n; i++) {
            hurt += ev[i].type == SA1_EVENT_HURT && ev[i].character == F;
        }
        n = 0;
    }
    w = sa1_character_world(g_ctx, F);
    t1 = rd(w, A_PLAYER_TWP + 4u);
    m = slot_mode(F);
    if (in_arena(t1)) {
        wt = rd(w, t1 + 4u) >> 16;
    }
    CHECK(hurt == 1u && wt > 100u && m == 13, "FD1: HURT of the follower's id x%u, slot 1 mode %d (13), wtimer %u", (unsigned)hurt, (int)m, (unsigned)wt);
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

static void test_fu1(void)
{
    sa1_character_id L;
    uint32_t eq = 0;
    printf("FU1 SA1_FOLLOW_SEED and SA1_FOLLOW_UPGRADES reach the world:\n");
    build_ground(G_FLAT);
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    {
        sa1_follower_desc d;
        sa1_character_id F = 0;
        const sa1g_world *w;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.kind = SA1_CHAR_TAILS;
        d.leader = L;
        d.flags = SA1_FOLLOW_SEED;
        d.seed = 0x12345u;
        sa1_follower_create(g_ctx, &d, &F);
        w = sa1_character_world(g_ctx, F);
        CHECK(w != NULL && rd(w, SA1G_ADDR_RAND) == 0x12345u, "FU1 the seed: rand state %X after the boot", (unsigned)(w != NULL ? rd(w, SA1G_ADDR_RAND) : 0u));
        sa1_character_destroy(g_ctx, F);
    }
    {
        sa1_follower_desc d;
        sa1_character_id F = 0;
        const sa1g_world *w;
        uint32_t pw, p;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.kind = SA1_CHAR_SONIC;
        d.leader = L;
        d.flags = SA1_FOLLOW_UPGRADES | SA1_FOLLOW_NO_SET;
        d.upgrade_flags[0] = 0x8Du;     /* Sonic's light shoes: equipment 2 (PInitialize, code_8C06C300.c:83) */
        d.num_upgrade_flags = 1u;
        sa1_follower_create(g_ctx, &d, &F);
        for (p = 0; p < 2u; p++) {
            tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        }
        poll_events(0, NULL);
        w = sa1_character_world(g_ctx, F);
        pw = rd(w, A_PLAYER_PWP + 4u);
        if (in_arena(pw)) {
            eq = rd(w, pw + 4u) & 0xFFFFu;
        }
        sa1_character_destroy(g_ctx, F);
    }
    CHECK(eq == 2u, "FU1 event flag 0x8D gives slot 1 the equipment %X (2)", (unsigned)eq);
    sa1_character_destroy(g_ctx, L);
}

/* ---- FN1, FI1: the game's objects -------------------------------------------------------------------------------------------- */
static uint32_t add_record(uint32_t type, float x, float y, float z, float p0)
{
    sa1_set_record r;
    uint32_t id = 0;
    memset(&r, 0, sizeof r);
    r.struct_size = sizeof r;
    r.type = type;
    r.position[0] = x;
    r.position[1] = y;
    r.position[2] = z;
    r.params[0] = p0;
    if (sa1_set_add(g_ctx, &r, &id) != SA1_OK) {
        printf("  sa1_set_add: %s\n", sa1_last_error(g_ctx));
    }
    return id;
}

static void test_fn1(void)
{
    sa1_character_id L, F;
    uint32_t i, recs[32], live = 0, n = 0;
    printf("FN1 SA1_FOLLOW_NO_SET with 32 rings round the follower:\n");
    build_ground(G_FLAT);
    set_ground();
    for (i = 0; i < 32u; i++) {
        recs[i] = add_record(SA1_OBJTYPE_RING, 30.0f * cosf((float)i * 0.19635f), 8.0f, 30.0f * sinf((float)i * 0.19635f), 0.0f);
    }
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_TAILS, L, SA1_FOLLOW_NO_SET);
    for (i = 0; i < 20u; i++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
    }
    {
        uint32_t tmp[64];
        n = 0;
        sa1_set_live(g_ctx, F, tmp, 64u, &n);
        live = n;
        n = 0;
        sa1_set_live(g_ctx, L, tmp, 64u, &n);
    }
    CHECK(live == 0u && n > 0u, "FN1: the follower's world holds %u records (the leader's %u)", (unsigned)live, (unsigned)n);
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
    for (i = 0; i < 32u; i++) {
        sa1_set_remove(g_ctx, recs[i]);
    }
}

/* the first task of the world running exec (0: none) */
static uint32_t find_task(const sa1g_world *w, uint32_t exec)
{
    uint32_t l;
    for (l = 0; l < 8u; l++) {
        uint32_t tp = rd(w, SA1G_ADDR_TASK_LISTS + 4u * l), guard = 0;
        while (tp != 0u && guard++ < 0x1000u) {
            if ((rd(w, tp + 0x10u) & 0x1FFFFFFFu) == (exec & 0x1FFFFFFFu)) {
                return tp;
            }
            tp = rd(w, tp);
        }
    }
    return 0u;
}

static void test_fi1(void)
{
    /* D_8C1B9BA0's kinds: 0 speed shoes, 5 barrier (itembox_test.c). A host-fed leader in the air right above the box: the AI jumps after
       him and the Sonic follower's jump ball breaks the box */
    static const struct { uint32_t kind; const char *name; } boxes[2] = { { 5u, "barrier" }, { 0u, "speed shoes" } };
    int bi;
    printf("FI1 a box broken by a Sonic follower:\n");
    build_ground(G_FLAT);
    set_ground();
    for (bi = 0; bi < 2; bi++) {
        sa1_leader_state hs;
        sa1_character_id F = 0;
        uint32_t rec, i, item_after = 0xFFFFu, broke = 0, shoes_pno = 0xFFu, after_draws = 0;
        stats s;
        const sa1g_world *w;
        sa1_set_event se[32];
        rec = add_record(SA1_OBJTYPE_ITEMBOX, 30.0f, 0.0f, 0.0f, (float)boxes[bi].kind);
        memset(&s, 0, sizeof s);
        host_state(&hs, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        follower_new_ex(SA1_CHAR_SONIC, 0, 0, &hs, &F);
        w = sa1_character_world(g_ctx, F);
        for (i = 0; i < 600u && s.faults == 0u; i++) {
            uint32_t n = 0, k;
            host_state(&hs, i < 30u ? 0.0f : 34.0f, i < 30u ? 0.0f : 24.0f, 0.0f, 0.0f, 0.0f);
            hs.flags = i < 30u ? SA1_LEADER_ON_GROUND : 0u;
            sa1_follower_feed(g_ctx, F, &hs);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
            sa1_poll_set_events(g_ctx, se, 32u, &n);
            for (k = 0; k < n; k++) {
                broke += se[k].type == SA1_SET_EVENT_BROKEN && se[k].character == F;
            }
            if (s.item[5] != 0u && item_after == 0xFFFFu) {
                /* the refresh that reported it took it off */
                uint32_t pw = rd(w, A_PLAYER_PWP);
                item_after = in_arena(pw) ? (rd(w, pw + 4u) >> 16) & 0xFFFFu : 0xFFFFu;
            }
            if (bi == 1 && shoes_pno == 0xFFu) {
                /* 8C07D3A0(pno): the speed-up task (8C07D2DA, then its exec 8C07D274) with the hitter's number in its awp (task +0x2C) */
                uint32_t tp = find_task(w, 0x8C07D274u);
                uint32_t awp = tp ? rd(w, tp + 0x2Cu) : 0u;
                shoes_pno = in_arena(awp) ? rd8(w, awp) : 0xFFu;
            }
            if (broke != 0u && i > 40u && (bi == 1 ? shoes_pno != 0xFFu : s.item[5] != 0u)) {
                uint32_t q;
                /* the next pairs: no draw at the leader's place (the barrier's display frees itself when the bit is clear) */
                for (q = 0; q < 8u; q++) {
                    const sa1_render *r = NULL;
                    uint32_t d;
                    sa1_follower_feed(g_ctx, F, &hs);
                    tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
                    poll_events(F, &s);
                    if (sa1_character_render(g_ctx, F, 1.0f, &r) == SA1_OK && r != NULL) {
                        sa1_state fs;
                        state_of(F, &fs);
                        for (d = 0; d < r->num_draws; d++) {
                            const sa1_draw *dd = &r->draws[d];
                            if (dd->prim == SA1_PRIM_TRIANGLES && dd->count != 0u) {
                                float c[3];
                                memcpy(c, r->positions + 9u * dd->first, sizeof c);
                                if (dist_3(c, hs.position) < 12.0f && dist_3(c, fs.position) > 15.0f) {
                                    after_draws++;
                                }
                            }
                        }
                    }
                }
                break;
            }
        }
        printf("    %s: broke after %u pairs (%u events), EV_ITEM arg 5 x%u, the proxy's item word %X, the speed-up task's player %u, draws at the leader's place after it %u\n",
               boxes[bi].name, (unsigned)i, (unsigned)broke, (unsigned)s.item[5], (unsigned)item_after, (unsigned)shoes_pno, (unsigned)after_draws);
        CHECK(s.faults == 0u, "FI1 %s: %u faults", boxes[bi].name, (unsigned)s.faults);
        CHECK(broke != 0u, "FI1 %s: the follower broke the box", boxes[bi].name);
        if (bi == 0) {
            CHECK(s.item[5] == 1u && item_after == 0u && after_draws == 0u, "FI1 barrier: EV_ITEM arg 5 x%u, the proxy's item taken off: %X, no barrier draw at the leader (%u)",
                  (unsigned)s.item[5], (unsigned)item_after, (unsigned)after_draws);
        } else {
            CHECK(s.item[1] + s.item[5] + s.item[8] == 0u && shoes_pno == 1u, "FI1 speed shoes: no EV_ITEM: they go to the hitter, slot 1 (the task's player %u)", (unsigned)shoes_pno);
        }
        sa1_character_destroy(g_ctx, F);
        sa1_set_remove(g_ctx, rec);
    }
}

/* ---- the board on slot 1 ----------------------------------------------------------------------------------------------------- */
static sa1_status board_of(sa1_character_id id, int32_t on, uint32_t terrain, uint32_t flags)
{
    sa1_board_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.on = on;
    d.terrain = terrain;
    d.flags = flags;
    return sa1_character_board(g_ctx, id, &d);
}

static void bstate_of(sa1_character_id id, sa1_board_state *b)
{
    memset(b, 0, sizeof *b);
    b->struct_size = sizeof *b;
    sa1_character_board_state(g_ctx, id, b);
}

static int board_draws(sa1_character_id id, uint32_t *arch)
{
    const sa1_render *r = NULL;
    uint32_t q;
    int n = 0;
    if (sa1_character_render(g_ctx, id, 1.0f, &r) != SA1_OK || r == NULL) {
        return 0;
    }
    for (q = 0; q < r->num_draws; q++) {
        if (r->draws[q].flags & SA1_DRAW_BOARD) {
            n++;
            if (arch != NULL) {
                *arch = (uint32_t)r->draws[q].archive;
            }
        }
    }
    return n;
}

static int riding_mode(int32_t kind, int32_t mode)
{
    return kind == SA1_CHAR_TAILS ? (mode >= 48 && mode <= 54) : (mode >= 62 && mode <= 68);
}

static void test_fb1(void)
{
    static const int32_t kinds[2] = { SA1_CHAR_TAILS, SA1_CHAR_SONIC };
    int ki;
    printf("FB1 a Tails / Sonic follower gets on a board on a 15-degree snow slope behind a riding leader (U1, U2, U6, Row 16):\n");
    build_slope(15.0f, SA1_SURFACE_SOLID | SA1_SURFACE_SNOW);
    set_ground();
    for (ki = 0; ki < 2; ki++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
        sa1_character_id F = follower_new(kinds[ki], L, 0);
        stats s;
        uint32_t p, ride = 0, held = 0, board_pairs = 0, arch = 0, counter = 0xFFu;
        int mode_after = -1;
        sa1_status st;
        sa1_board_state bs;
        memset(&s, 0, sizeof s);
        for (p = 0; p < 300u; p++) {
            sa1_state ls;
            float yaw;
            state_of(L, &ls);
            yaw = p ? ls.yaw : 0.0f;
            if (p == 10u) {
                board_of(L, SA1_BOARD_ON, SA1_BOARD_TERRAIN_SNOW, 0);
                st = board_of(F, SA1_BOARD_ON, SA1_BOARD_TERRAIN_SNOW, 0);
                CHECK(st == SA1_OK, "FB1 %s: sa1_character_board(ON): %s %s", kind_name(kinds[ki]), sa1_status_string(st), st == SA1_OK ? "" : sa1_last_error(g_ctx));
            }
            tick_input(L, 0, p >= 10u ? 1.0f : 0.0f, 0, yaw, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, yaw, 1.0f / 30.0f);
            poll_events(F, &s);
            if (p == 10u) {
                mode_after = slot_mode(F);
            }
            if (p > 10u) {
                ride += riding_mode(kinds[ki], slot_mode(F));
                bstate_of(F, &bs);
                held += (bs.flags & SA1_BSTATE_HELD) != 0;
                if (board_draws(F, &arch)) {
                    board_pairs++;
                }
                if (p == 12u && bs.board_task != 0) {
                    const sa1g_world *w = sa1_character_world(g_ctx, F);
                    counter = rd8(w, rd(w, bs.board_task + 0x20u) + 8u);
                }
            }
        }
        bstate_of(F, &bs);
        CHECK(riding_mode(kinds[ki], mode_after) && s.board_on >= 1u, "FB1 %s: in a board mode after 1 pair (mode %d), EV_ON x%u", kind_name(kinds[ki]), (int)mode_after, (unsigned)s.board_on);
        CHECK(counter == 1u, "FB1 %s: the board task's counter.b[0] = %u", kind_name(kinds[ki]), (unsigned)counter);
        CHECK(held >= 240u, "FB1 %s: held %u pairs, in a board mode %u (of 289; the probe's 278, 286 of 300)", kind_name(kinds[ki]), (unsigned)held, (unsigned)ride);
        {
            const char *an = NULL;
            uint32_t ant = 0;
            sa1_texture_archive(g_ctx, kinds[ki], arch, &an, &ant);
            /* U6, Row 16: the draws carry the archive board_variant says, never asserted to be variant 0 */
            CHECK(board_pairs >= 200u && an != NULL && strcmp(an, kinds[ki] == SA1_CHAR_TAILS ? "MTX_BOARD0" : bs.board_variant ? "SB_BOARD1" : "MIW_B001") == 0,
                  "FB1 %s: board draws in %u pairs, archive %u (%s); variant %u (the world's own pair parity)", kind_name(kinds[ki]), (unsigned)board_pairs, (unsigned)arch,
                  an != NULL ? an : "?", (unsigned)bs.board_variant);
        }
        CHECK(s.faults == 0u, "FB1 %s: 0 faults (%u); mean: AI steers it through the analog record", kind_name(kinds[ki]), (unsigned)s.faults);
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

static void test_fb2_6(void)
{
    sa1_character_id L, F;
    stats s;
    uint32_t p, variants = 0, r;
    sa1_board_state bs;
    sa1_status st;
    printf("FB2 OFF, kept board, FREE, re-entry on a follower; one ride per parity (Row 16):\n");
    build_slope(15.0f, SA1_SURFACE_SOLID | SA1_SURFACE_SNOW);
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_SONIC, L, 0);
    memset(&s, 0, sizeof s);
    for (p = 0; p < 10u; p++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &s);
    }
    for (r = 0; r < 4u; r++) {
        board_of(F, SA1_BOARD_ON, SA1_BOARD_TERRAIN_SNOW, 0);
        for (p = 0; p < 21u + r; p++) {
            tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
        }
        bstate_of(F, &bs);
        if (bs.flags & SA1_BSTATE_ON) {
            variants |= 1u << bs.board_variant;
        }
        board_of(F, SA1_BOARD_OFF, 0, 0);
        for (p = 0; p < 6u; p++) {
            tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
        }
        bstate_of(F, &bs);
        CHECK(!(bs.flags & SA1_BSTATE_ON) && (bs.flags & SA1_BSTATE_KEPT), "FB2 ride %u: off, a kept board (flags %X)", (unsigned)r, (unsigned)bs.flags);
    }
    CHECK(s.board_on == 4u && s.board_off == 4u, "FB2: EV_ON x%u, EV_OFF x%u", (unsigned)s.board_on, (unsigned)s.board_off);
    CHECK(variants == 3u, "FB2: one ride per parity gives both variants on a Sonic follower (mask %u)", (unsigned)variants);
    /* the game's re-entry: the kept board takes the rider again (board_test (j) on slot 1) */
    board_of(F, SA1_BOARD_ON, SA1_BOARD_TERRAIN_SNOW, SA1_BOARD_REUSE_KEPT);
    for (p = 0; p < 4u; p++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &s);
    }
    bstate_of(F, &bs);
    CHECK((bs.flags & SA1_BSTATE_ON) && s.board_on == 5u, "FB2 re-entry with SA1_BOARD_REUSE_KEPT: flags %X, EV_ON x%u", (unsigned)bs.flags, (unsigned)s.board_on);
    board_of(F, SA1_BOARD_OFF, 0, 0);
    for (p = 0; p < 6u; p++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &s);
    }
    st = board_of(F, SA1_BOARD_FREE, 0, 0);
    tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    poll_events(F, &s);
    bstate_of(F, &bs);
    CHECK(st == SA1_OK && !(bs.flags & SA1_BSTATE_KEPT), "FB2 FREE: %s, flags %X", sa1_status_string(st), (unsigned)bs.flags);
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);

    printf("FB3 a respawn while riding (the AI's order 24), and the long slope of recheck 3.5:\n");
    {
        sa1_leader_state hs;
        sa1_character_id K = 0;
        int mode_after;
        memset(&s, 0, sizeof s);
        host_state(&hs, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        follower_new_ex(SA1_CHAR_TAILS, 0, 0, &hs, &K);
        for (p = 0; p < 10u; p++) {
            sa1_follower_feed(g_ctx, K, &hs);
            tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(K, &s);
        }
        board_of(K, SA1_BOARD_ON, SA1_BOARD_TERRAIN_SNOW, 0);
        for (p = 0; p < 20u; p++) {
            sa1_follower_feed(g_ctx, K, &hs);
            tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(K, &s);
        }
        sa1_follower_respawn(g_ctx, K);
        sa1_follower_feed(g_ctx, K, &hs);
        tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(K, &s);
        mode_after = slot_mode(K);
        bstate_of(K, &bs);
        CHECK(s.board_off == 1u && (bs.flags & SA1_BSTATE_KEPT) && mode_after == 15, "FB3: EV_OFF x%u, a kept board (flags %X), Tails flies (mode %d)", (unsigned)s.board_off,
              (unsigned)bs.flags, (int)mode_after);
        sa1_character_destroy(g_ctx, K);

        /* the owner's board went 7,800 HU in 38 s: 4.4 units per step: a host-fed leader pulling away down the slope */
        memset(&s, 0, sizeof s);
        host_state(&hs, 0.0f, 0.0f, 0.0f, 4.4f, 0.0f);
        follower_new_ex(SA1_CHAR_TAILS, 0, 0, &hs, &K);
        for (p = 0; p < 10u; p++) {
            sa1_follower_feed(g_ctx, K, &hs);
            tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(K, &s);
        }
        board_of(K, SA1_BOARD_ON, SA1_BOARD_TERRAIN_SNOW, 0);
        for (p = 0; p < 1140u; p++) {
            float x = 4.4f * 2.0f * (float)p;
            host_state(&hs, x, ground_y(x), 0.0f, 4.4f, 0.0f);
            sa1_follower_feed(g_ctx, K, &hs);
            tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(K, &s);
        }
        printf("    the long slope: 1,140 pairs behind a leader at 4.4 units per step: respawns %u + %u, EV_ON x%u, EV_OFF x%u, faults %u\n", (unsigned)s.respawn[0], (unsigned)s.respawn[1],
               (unsigned)s.board_on, (unsigned)s.board_off, (unsigned)s.faults);
        CHECK(s.faults == 0u && s.board_off <= s.respawn[0] + s.respawn[1], "FB3 the long slope: a respawn ends the ride, nothing else: %u faults, EV_OFF %u, respawns %u", (unsigned)s.faults,
              (unsigned)s.board_off, (unsigned)(s.respawn[0] + s.respawn[1]));
        sa1_character_destroy(g_ctx, K);
    }

    printf("FB4 ON with SA1_BOARD_START_PUSH in a follower world:\n");
    {
        sa1_leader_state hs;
        sa1_character_id K = 0;
        uint32_t fast = 0;
        host_state(&hs, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        follower_new_ex(SA1_CHAR_SONIC, 0, 0, &hs, &K);
        for (p = 0; p < 10u; p++) {
            sa1_follower_feed(g_ctx, K, &hs);
            tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
        }
        board_of(K, SA1_BOARD_ON, SA1_BOARD_TERRAIN_SNOW, SA1_BOARD_START_PUSH);
        tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        for (p = 0; p < 40u; p++) {
            const sa1g_world *w = sa1_character_world(g_ctx, K);
            uint32_t pw1, pw0;
            sa1_follower_feed(g_ctx, K, &hs);
            tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
            pw1 = rd(w, A_PLAYER_PWP + 4u);
            pw0 = rd(w, A_PLAYER_PWP);
            if (in_arena(pw1) && rdf(w, pw1 + 0x38u) >= 4.0f && in_arena(pw0) && rdf(w, pw0 + 0x38u) == 0.0f) {
                fast++;
            }
        }
        CHECK(fast >= 40u, "FB4: slot 1 at >= 4.0 per step, the proxy restored by its refresh, in %u of 40 pairs", (unsigned)fast);
        sa1_character_destroy(g_ctx, K);
    }

    printf("FB5 riding on SA1_SURFACE_BOARD_SNOW: attr & 0x8 in the follower's world only (U3):\n");
    build_slope(15.0f, SA1_SURFACE_SOLID | SA1_SURFACE_BOARD_SNOW);
    set_ground();
    {
        sa1_character_id L2 = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 40.0f);
        sa1_character_id K = follower_new(SA1_CHAR_TAILS, L2, 0);
        sa1_state_ex xs;
        uint32_t f_on = 0, f_walk = 0, l_on = 0;
        for (p = 0; p < 150u; p++) {
            if (p == 60u) {
                board_of(K, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
            }
            tick_input(L2, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
            memset(&xs, 0, sizeof xs);
            xs.struct_size = sizeof xs;
            sa1_character_state_ex(g_ctx, K, &xs);
            if (xs.flags & SA1_STATE_ON_GROUND) {
                if (p < 60u) {
                    f_walk += (xs.attr & 0x8u) != 0u;
                } else if (riding_mode(SA1_CHAR_TAILS, slot_mode(K))) {
                    f_on += (xs.attr & 0x8u) != 0u;
                }
            }
            sa1_character_state_ex(g_ctx, L2, &xs);
            if (xs.flags & SA1_STATE_ON_GROUND) {
                l_on += (xs.attr & 0x8u) != 0u;
            }
        }
        CHECK(f_on > 30u && f_walk == 0u && l_on == 0u, "FB5: attr & 8 on the follower riding %u pairs, on foot %u, the leader %u", (unsigned)f_on, (unsigned)f_walk, (unsigned)l_on);
        sa1_character_destroy(g_ctx, K);
        sa1_character_destroy(g_ctx, L2);
    }

    printf("FB6 a Knuckles follower refused; a Sonic follower of a Super leader:\n");
    build_slope(15.0f, SA1_SURFACE_SOLID | SA1_SURFACE_SNOW);
    set_ground();
    {
        sa1_character_id L2 = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
        sa1_character_id K = follower_new(SA1_CHAR_KNUCKLES, L2, 0);
        st = board_of(K, SA1_BOARD_ON, SA1_BOARD_TERRAIN_SNOW, 0);
        CHECK(st == SA1_ERR_UNSUPPORTED, "FB6 Knuckles: %s (%s)", sa1_status_string(st), sa1_last_error(g_ctx));
        sa1_character_destroy(g_ctx, K);
        sa1_character_destroy(g_ctx, L2);
        L2 = leader_new(SA1_CHAR_SUPER_SONIC, 0.0f, 12.0f, 0.0f);
        if (L2 != 0) {
            K = follower_new(SA1_CHAR_SONIC, L2, 0);
            st = board_of(K, SA1_BOARD_ON, SA1_BOARD_TERRAIN_SNOW, 0);
            CHECK(st == SA1_OK, "FB6 a Sonic follower of a Super leader: %s (the follower is not Super)", sa1_status_string(st));
            for (p = 0; p < 10u; p++) {
                tick_input(L2, 0, 0, 0, 0.0f, 1.0f / 30.0f);
                tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
                poll_events(0, NULL);
            }
            CHECK(riding_mode(SA1_CHAR_SONIC, slot_mode(K)), "FB6 it rides: mode %d", (int)slot_mode(K));
            sa1_character_destroy(g_ctx, K);
            sa1_character_destroy(g_ctx, L2);
        } else {
            CHECK(0, "FB6: no Super leader: %s", sa1_last_error(g_ctx));
        }
    }

    printf("FH4 (reported, not gated: review R.5) a hold on a riding Tails follower without an OFF, 60 pairs:\n");
    {
        sa1_character_id L2 = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
        sa1_character_id K = follower_new(SA1_CHAR_TAILS, L2, 0);
        board_of(K, SA1_BOARD_ON, SA1_BOARD_TERRAIN_SNOW, 0);
        for (p = 0; p < 10u; p++) {
            tick_input(L2, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
        }
        sa1_follower_hold(g_ctx, K, 1);
        for (p = 0; p < 60u; p++) {
            sa1_state fs;
            sa1_follower_info fi;
            tick_input(L2, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            tick_input(K, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
            if (p % 20u == 0u || p == 59u) {
                state_of(K, &fs);
                info_of(K, &fi);
                bstate_of(K, &bs);
                printf("    pair %2u: AI mode %d, slot 1 mode %d, board flags %X, y %.0f\n", (unsigned)p, (int)fi.ai_mode, (int)slot_mode(K), (unsigned)bs.flags, (double)fs.position[1]);
            }
        }
        sa1_character_destroy(g_ctx, K);
        sa1_character_destroy(g_ctx, L2);
    }
}

/* ---- FG1, FG2: the fishing C and the camera bit in follower worlds (recheck 4.5 items 1, 2) -------------------------------------- */
static void push_cb(sa1g_world *w, void *user)
{
    (void)w;
    (void)user;
    /* 8C01AD2C: the EVENT level's push: mode function, adjust, kind (fish098.md 2.1) */
    ((void (*)(sa1_gfn, int8_t, int8_t))gfn(0x8C01AD2Cu))(SA1_GFN_C(0x8C0239C2u), 3, 2);
}

static void test_fg(void)
{
    sa1_character_id L, F;
    stats s;
    uint32_t p;
    sa1_input in;
    printf("FG1 a Big follower: a request made in its world is reported for that id only; the camera bit on a follower id is ignored, 600 pairs:\n");
    build_ground(G_FLAT);
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_BIG, L, 0);
    memset(&s, 0, sizeof s);
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.host_flags = SA1_INPUT_GAME_CAMERA;
    for (p = 0; p < 600u; p++) {
        float sx, sy;
        uint32_t b;
        script_run(p, &sx, &sy, &b);
        tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
        sa1_character_tick(g_ctx, F, &in, 1.0f / 30.0f, NULL, NULL);
        poll_events(F, &s);
    }
    {
        sa1g_world *w = sa1_character_world(g_ctx, F);
        sa1_camera_state cf, cl;
        sa1_status sf, sl;
        CHECK(s.faults == 0u && w->camreq.want == 0 && w->camreq.pre == NULL && w->camreq.running == 0, "FG1: 0 faults (%u); want %d, pre %s, running %d",
              (unsigned)s.faults, (int)w->camreq.want, w->camreq.pre ? "set" : "NULL", (int)w->camreq.running);
        /* a request in this world: the game's push of the FISHING mode (8C01AD2C(fn, adjust, kind), as Big's cast makes it) */
        sa1g_world_call(w, push_cb, NULL);
        memset(&cf, 0, sizeof cf);
        cf.struct_size = sizeof cf;
        memset(&cl, 0, sizeof cl);
        cl.struct_size = sizeof cl;
        sf = sa1_character_camera(g_ctx, F, 1.0f, &cf);
        sl = sa1_character_camera(g_ctx, L, 1.0f, &cl);
        if (sf == SA1_ERR_UNSUPPORTED) {
            printf("    sa1_character_camera is not built yet in this archive: the report is not checked here (098-lib-lookcam's camreq_test)\n");
        } else {
            CHECK(sf == SA1_OK && (cf.flags & SA1_CAMSTATE_ACTIVE) && !(cf.flags & SA1_CAMSTATE_RUNNING) && sl == SA1_OK && cl.flags == 0u,
                  "FG1: the follower's world reports ACTIVE, not RUNNING (flags %X); the leader's nothing (%X)", (unsigned)cf.flags, (unsigned)cl.flags);
        }
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        sa1_character_tick(g_ctx, F, &in, 1.0f / 30.0f, NULL, NULL);
        poll_events(F, &s);
        CHECK(s.faults == 0u && w->camreq.running == 0, "FG1: after the request 0 faults (%u), not running (%d)", (unsigned)s.faults, (int)w->camreq.running);
    }
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);

    printf("FG2 a Tails follower's real pushes of the camera functions (2 requests in 300 pairs on the probe): 0 faults:\n");
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_TAILS, L, 0);
    memset(&s, 0, sizeof s);
    for (p = 0; p < 600u; p++) {
        float sx, sy;
        uint32_t b;
        script_run(p, &sx, &sy, &b);
        tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &s);
    }
    CHECK(s.faults == 0u && s.unimpl == 0u, "FG2: %u faults, %u unimplemented", (unsigned)s.faults, (unsigned)s.unimpl);
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* ---- FH3: the hidden pair's head ------------------------------------------------------------------------------------------------ */
static void look_cb(sa1g_world *w, void *user)
{
    (void)w;
    /* the game's own setter of the event look, player 0 */
    ((void (*)(uint8_t, void *))gfn(0x8C07C318u))(0, user);
}

/* the zero screen (tests/world_replay.c wr_screen_of): the sphere visibility test never passes: every pair from here on is hidden */
static void screen_off(sa1g_world *w)
{
    static const uint32_t words[] = { 0x8C88F5E4u, 0x8C88F5E8u, 0x8C88F5ECu, 0x8C88F600u, 0x8C88F604u, 0x8C88F608u, 0x8C88F60Cu,
                                      0x8C88F618u, 0x8C88F61Cu, 0x8C88F620u, 0x8C88F624u, 0x8C88F628u, 0x8C88F644u, 0x8C88F648u };
    size_t i;
    for (i = 0; i < sizeof words / sizeof words[0]; i++) {
        sa1g_world_wr32(w, words[i], 0u);
    }
}

/* the triangles of the render as a copy (no shadow draws): 9 floats each */
static float *render_copy(sa1_character_id id, uint32_t *n)
{
    const sa1_render *r = NULL;
    float *o;
    uint32_t q, m = 0;
    *n = 0;
    if (sa1_character_render(g_ctx, id, 1.0f, &r) != SA1_OK || r == NULL) {
        return NULL;
    }
    o = (float *)malloc(9u * sizeof(float) * (r->num_triangles + 1u));
    for (q = 0; q < r->num_draws; q++) {
        const sa1_draw *dd = &r->draws[q];
        if (dd->prim == SA1_PRIM_TRIANGLES && !(dd->flags & SA1_DRAW_SHADOW)) {
            memcpy(o + 9u * m, r->positions + 9u * dd->first, 9u * sizeof(float) * dd->count);
            m += dd->count;
        }
    }
    *n = m;
    return o;
}

/* the triangles that differ by more than 0.5 units, their count and centre (in a) */
static uint32_t moved_set(const float *a, const float *b, uint32_t n, float c[3])
{
    uint32_t t, v, m = 0;
    double s[3] = { 0, 0, 0 };
    for (t = 0; t < n; t++) {
        float d = 0.0f;
        for (v = 0; v < 9; v++) {
            float x = a[9u * t + v] - b[9u * t + v];
            d += x * x;
        }
        if (d > 0.25f) {
            for (v = 0; v < 9; v++) {
                s[v % 3] += a[9u * t + v];
            }
            m++;
        }
    }
    if (m != 0u) {
        c[0] = (float)(s[0] / (3.0 * m));
        c[1] = (float)(s[1] / (3.0 * m));
        c[2] = (float)(s[2] / (3.0 * m));
    }
    return m;
}

static void test_fh3(void)
{
    sa1_character_id A, B;
    sa1g_world *wa, *wb;
    float pt[3] = { 0.0f, 20.0f, 100.0f };
    uint32_t p, nd_a = 0, nd_b = 0, np_a = 0, np_b = 0, md, mp;
    float *da, *db, *pa, *pb, cd[3] = { 0, 0, 0 }, cp[3] = { 0, 0, 0 };
    printf("FH3 the hidden pair's head: a Knuckles world looking left through the game's own setter, the pair forced hidden:\n");
    build_ground(G_FLAT);
    set_ground();
    A = leader_new(SA1_CHAR_KNUCKLES, 0.0f, 12.0f, 0.0f);
    B = leader_new(SA1_CHAR_KNUCKLES, 0.0f, 12.0f, 0.0f);
    wa = sa1_character_world(g_ctx, A);
    wb = sa1_character_world(g_ctx, B);
    for (p = 0; p < 30u; p++) {
        tick_input(A, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(B, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    }
    for (p = 0; p < 16u; p++) {
        sa1g_world_call(wa, look_cb, pt);
        tick_input(A, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(B, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    }
    poll_events(0, NULL);
    da = render_copy(A, &nd_a);
    db = render_copy(B, &nd_b);
    screen_off(wa);
    screen_off(wb);
    sa1g_world_call(wa, look_cb, pt);
    tick_input(A, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    tick_input(B, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    poll_events(0, NULL);
    pa = render_copy(A, &np_a);
    pb = render_copy(B, &np_b);
    md = da != NULL && db != NULL && nd_a == nd_b ? moved_set(da, db, nd_a, cd) : 0u;
    mp = pa != NULL && pb != NULL && np_a == np_b ? moved_set(pa, pb, np_a, cp) : 0u;
    {
        float dx = cd[0] - cp[0], dy = cd[1] - cp[1], dz = cd[2] - cp[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
        printf("    drawn: %u triangles, %u moved by the look; the CPU pose: %u triangles, %u moved; hidden %d; the moved centre %.3f units apart\n", (unsigned)nd_a, (unsigned)md,
               (unsigned)np_a, (unsigned)mp, sa1_capture_hidden(sa1_character_capture(g_ctx, A)), (double)d);
        CHECK(sa1_capture_hidden(sa1_character_capture(g_ctx, A)) && md >= 300u && mp >= 300u, "FH3: the look moves the head in the hidden pair too: %u triangles (drawn %u), not at rest", (unsigned)mp, (unsigned)md);
        CHECK(d <= 0.125f, "FH3: the CPU pose's head stands where it was drawn: %.3f units (0.5 HU at the Source scale is 0.125 units)", (double)d);
    }
    free(da);
    free(db);
    free(pa);
    free(pb);
    sa1_character_destroy(g_ctx, A);
    sa1_character_destroy(g_ctx, B);
}

/* ---- FM1, FM2, FX1 -------------------------------------------------------------------------------------------------------------- */
static double private_mb(void)
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc;
    memset(&pmc, 0, sizeof pmc);
    pmc.cb = sizeof pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof pmc);
    return (double)pmc.PrivateUsage / (1024.0 * 1024.0);
#else
    return 0.0;
#endif
}

static double now_ms(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER f;
    LARGE_INTEGER t;
    if (!f.QuadPart) {
        QueryPerformanceFrequency(&f);
    }
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
#else
    return 0.0;
#endif
}

/* Private memory over create / destroy cycles is not steady: it moves between 109.5 and 114 MB with no trend, on the 0.4.0 archive too, and the old check (cycle 12 at most 0.5 MB over cycle 1) failed or passed by the binary's heap
   layout (sadc build/research/r099/base/fm1_probe.log). The check that means "no leak" is a trend: the last third against the first third of 24 cycles; a world that stays would be 17 MB per cycle */
static void test_fm1(void)
{
    double first = 0.0, last = 0.0, m = 0.0;
    uint32_t c, p;
    printf("FM1 24 create / tick / destroy cycles:\n");
    build_ground(G_FLAT);
    set_ground();
    for (c = 0; c < 24u; c++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
        sa1_character_id F = follower_new(SA1_CHAR_KNUCKLES, L, 0);
        for (p = 0; p < 30u; p++) {
            tick_input(L, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
        }
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
        m = private_mb();
        if (c < 8u) {
            first += m / 8.0;
        } else if (c >= 16u) {
            last += m / 8.0;
        }
    }
    CHECK(last - first <= 1.5, "FM1: no growth: private memory, the mean of cycles 1-8 %.2f MB, of cycles 17-24 %.2f MB (at most 1.5 more)", first, last);
}

static void test_fm2(void)
{
    uint32_t n = sizeof(void *) == 8 ? 99u : 32u, i, p, faults = 0, tickerr = 0;
    static sa1_character_id F[128];
    sa1_character_id L;
    double m0, m1, t0, sum = 0.0;
    stats s;
    printf("FM2 %u Knuckles, 600 pairs (review R.10: 32 on the 32-bit target):\n", (unsigned)n);
    build_ground(G_FLAT);
    set_ground();
    m0 = private_mb();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    for (i = 0; i < n; i++) {
        F[i] = follower_new(SA1_CHAR_KNUCKLES, L, SA1_FOLLOW_NO_SET);
        if (F[i] == 0) {
            n = i;
            break;
        }
    }
    m1 = private_mb();
    memset(&s, 0, sizeof s);
    for (p = 0; p < 600u; p++) {
        float sx, sy;
        uint32_t b;
        script_run(p, &sx, &sy, &b);
        tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
        t0 = now_ms();
        for (i = 0; i < n; i++) {
            if (tick_input(F[i], 0, 0, 0, 0.0f, 1.0f / 30.0f) != SA1_OK) {
                tickerr++;
            }
        }
        sum += now_ms() - t0;
        poll_events(F[0], &s);
    }
    faults = s.faults;
    printf("    %u followers: +%.0f MB private (%.2f each); %.3f ms per pair for all (%.4f per follower)\n", (unsigned)n, m1 - m0, (m1 - m0) / (double)(n ? n : 1), sum / 600.0,
           sum / 600.0 / (double)(n ? n : 1));
    CHECK(faults == 0u && tickerr == 0u, "FM2: 0 faults (%u), 0 tick errors (%u)", (unsigned)faults, (unsigned)tickerr);
    for (i = 0; i < n; i++) {
        sa1_character_destroy(g_ctx, F[i]);
    }
    sa1_character_destroy(g_ctx, L);
}

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

/* calls: 1: every follower is told speed 1 and spot 0, before its first tick (FSP1) */
static void fx1_run(int calls, uint32_t *out_a, uint32_t *out_k)
{
    static const float zero[3] = { 0.0f, 0.0f, 0.0f };
    sa1_character_id L, A, B;
    uint32_t p, ha = 2166136261u, hb = 2166136261u, kinds, k;
    build_ground(G_FLAT);
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    A = follower_new(SA1_CHAR_TAILS, L, 0);
    B = follower_new(SA1_CHAR_TAILS, L, 0);
    if (calls) {
        sa1_follower_speed(g_ctx, A, 1.0f);
        sa1_follower_spot(g_ctx, A, zero);
        sa1_follower_speed(g_ctx, B, 1.0f);
        sa1_follower_spot(g_ctx, B, zero);
    }
    for (p = 0; p < 300u; p++) {
        float sx, sy;
        uint32_t b;
        sa1_state a, c;
        script_run(p, &sx, &sy, &b);
        tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
        tick_input(A, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(B, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
        state_of(A, &a);
        state_of(B, &c);
        ha = fnv(ha, a.position, sizeof a.position);
        hb = fnv(hb, c.position, sizeof c.position);
    }
    CHECK(ha == hb, "FX1: identical followers: %08X, %08X", (unsigned)ha, (unsigned)hb);
    sa1_character_destroy(g_ctx, A);
    sa1_character_destroy(g_ctx, B);
    /* one more for the cross-target line: every kind */
    kinds = 2166136261u;
    for (k = 0; k < 6u; k++) {
        sa1_character_id F = follower_new(k_kinds[k], L, 0);
        if (calls) {
            sa1_follower_speed(g_ctx, F, 1.0f);
            sa1_follower_spot(g_ctx, F, zero);
        }
        for (p = 0; p < 120u; p++) {
            sa1_state a;
            tick_input(L, 0, 1.0f, (p % 40u) == 20u ? SA1_BUTTON_JUMP : 0u, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
            state_of(F, &a);
            kinds = fnv(kinds, a.position, sizeof a.position);
        }
        sa1_character_destroy(g_ctx, F);
    }
    sa1_character_destroy(g_ctx, L);
    *out_a = ha;
    *out_k = kinds;
}

static void test_fx1(void)
{
    uint32_t ha, kinds;
    printf("FX1 two identical followers identical; x86 = x64 (the DETERMINISM line):\n");
    fx1_run(0, &ha, &kinds);
    printf("DETERMINISM follower %08X %08X\n", (unsigned)ha, (unsigned)kinds);
}

/* ---- BRINGUP (--long, F4) ------------------------------------------------------------------------------------------------------- */
typedef struct course_stats {
    uint32_t faults, unimpl, respawn_d, respawn_o;
    float mean_dh, max_dh;
} course_stats;

static void course_run(int32_t kind, int ground, float leader_y, uint32_t pairs, int mode, course_stats *o)
{
    /* mode 0 a live Sonic runs along +X with jumps, 1 he stands, 2 he stands backed against the wall (a host-fed point), 3 a host-fed point on a ledge over a void */
    sa1_character_id L = 0, F = 0;
    stats s;
    double sum = 0.0;
    uint32_t p;
    sa1_leader_state hs;
    float px = ground == G_VOID_LEDGE ? -200.0f : ground == G_WALL ? 240.0f : 0.0f;
    memset(&s, 0, sizeof s);
    memset(o, 0, sizeof *o);
    (void)leader_y;
    host_state(&hs, px, 0.0f, 0.0f, 0.0f, 0.0f);
    if (mode == 0) {
        L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
        F = follower_new(kind, L, 0);
    } else {
        follower_new_ex(kind, 0, 0, &hs, &F);
    }
    for (p = 0; p < pairs; p++) {
        sa1_state fs, ls;
        float d;
        if (mode == 0) {
            tick_input(L, 0, (p % 300u) < 200u ? 1.0f : 0.0f, (p % 60u) == 30u ? SA1_BUTTON_JUMP : 0u, 0.0f, 1.0f / 30.0f);
            state_of(L, &ls);
        } else {
            sa1_follower_feed(g_ctx, F, &hs);
            memcpy(ls.position, hs.position, sizeof ls.position);
        }
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &s);
        state_of(F, &fs);
        d = dist_h(ls.position, fs.position);
        sum += d;
        o->max_dh = d > o->max_dh ? d : o->max_dh;
    }
    o->mean_dh = (float)(sum / pairs);
    o->faults = s.faults;
    o->unimpl = s.unimpl;
    o->respawn_d = s.respawn[0];
    o->respawn_o = s.respawn[1];
    sa1_character_destroy(g_ctx, F);
    if (L != 0) {
        sa1_character_destroy(g_ctx, L);
    }
}

/* ---- FPL: Eggman and Tikal as followers (round 0.9.9e; the owner, rounds plan 14.1bc: "Eggman and Tikal should be spawnable followers"). Their worlds run the game's own mains, nothing
 * repaired. The AI task 8C081D4E steers them as it steers anyone (per[1], analog[1]), and what their code does with that is what happens: this case PINS it, with the numbers on its lines. Read:
 * sadc code_8C0ED880.c, tikal_prog/code_0CB00000.c: no NAct, so no case 12, 24, 48: the drop-in's flight order does nothing and they fall from above the camera; Eggman's debug and action buttons come
 * from pad 0 (nobody presses them), Tikal's from her own pad, and the AI's "flying" button 0x402 is her debug button (mode 11, free movement: no gravity: she hangs in the air) and her "back to the
 * spawn point" (SA1_EVENT_RESPAWN). Measured below: they follow on foot, jump, land and go on after a drop-in; Tikal hangs until the AI presses jump, which it does when the leader moves away. --- */
typedef struct fpl_stats {
    uint32_t modes[32], modes_after[32];
    float moved, moved_after, mean_dh, max_dh, last_y;
} fpl_stats;

static void fpl_mode_note(sa1_character_id F, uint32_t *modes)
{
    sa1_state_ex ex;
    memset(&ex, 0, sizeof ex);
    ex.struct_size = sizeof ex;
    if (!SA1_FAILED(sa1_character_state_ex(g_ctx, F, &ex)) && ex.mode >= 0 && ex.mode < 32) {
        modes[ex.mode]++;
    }
}

static void fpl_modes_text(const uint32_t *modes, char *out, size_t cap)
{
    size_t n = 0;
    int m;
    out[0] = 0;
    for (m = 0; m < 32 && n + 16 < cap; m++) {
        if (modes[m] != 0u) {
            n += (size_t)sprintf(out + n, " %d x%u", m, (unsigned)modes[m]);
        }
    }
}

static void test_fpl(void)
{
    static const int32_t kk[2] = { SA1_CHAR_EGGMAN, SA1_CHAR_TIKAL };
    uint32_t kinds = sa1_follower_kinds();
    int ki;
    printf("FPL Eggman and Tikal as followers, the game's own code, nothing repaired:\n");
    CHECK(kinds == 0x0FFu && sa1_follower_kind_reason(SA1_CHAR_EGGMAN) == NULL && sa1_follower_kind_reason(SA1_CHAR_TIKAL) == NULL && sa1_follower_kind_reason(SA1_CHAR_SUPER_SONIC) != NULL,
          "FPL1 sa1_follower_kinds() 0x%X, no reason for 1 and 4, kind 8 still has one", (unsigned)kinds);
    build_ground(G_FLAT);
    set_ground();
    for (ki = 0; ki < 2; ki++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F = 0;
        sa1_status st = follower_new_ex(kk[ki], L, 0, NULL, &F);
        fpl_stats o;
        stats s;
        sa1_state fs, ls, f0;
        sa1_follower_info info;
        uint32_t p;
        double sum = 0.0;
        char text[256];
        memset(&o, 0, sizeof o);
        memset(&s, 0, sizeof s);
        CHECK(st == SA1_OK && F != 0, "FPL2 %s: sa1_follower_create: %s (%s)", kind_name(kk[ki]), sa1_status_string(st), sa1_last_error(g_ctx));
        if (st != SA1_OK) {
            sa1_character_destroy(g_ctx, L);
            continue;
        }
        state_of(F, &f0);
        /* the leader runs along +X with jumps, as course_run */
        for (p = 0; p < 900u; p++) {
            tick_input(L, 0, (p % 300u) < 200u ? 1.0f : 0.0f, (p % 60u) == 30u ? SA1_BUTTON_JUMP : 0u, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
            state_of(L, &ls);
            state_of(F, &fs);
            sum += dist_h(ls.position, fs.position);
            if (dist_h(ls.position, fs.position) > o.max_dh) {
                o.max_dh = dist_h(ls.position, fs.position);
            }
            fpl_mode_note(F, o.modes);
        }
        o.moved = dist_h(fs.position, f0.position);
        o.mean_dh = (float)(sum / 900.0);
        fpl_modes_text(o.modes, text, sizeof text);
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        sa1_follower_info_get(g_ctx, F, &info);
        CHECK(s.faults == 0u && s.unimpl == 0u, "FPL3 %s behind a running Sonic, 900 pairs: %u faults, %u unimplemented; moved %.0f units, mean %.1f, max %.1f from him; AI mode %d smode %d, %u respawns (%u distance, %u off screen); modes%s",
              kind_name(kk[ki]), (unsigned)s.faults, (unsigned)s.unimpl, (double)o.moved, (double)o.mean_dh, (double)o.max_dh, (int)info.ai_mode, (int)info.ai_smode, (unsigned)info.respawns, (unsigned)info.respawns_distance, (unsigned)info.respawns_offscreen, text);
        CHECK(o.moved > 50.0f, "FPL3 %s: the AI's stick moves it (%.0f units)", kind_name(kk[ki]), (double)o.moved);
        /* the leader runs off the end of G_FLAT above if he goes on: from here he walks back on it. The game's own respawn: orders 24 and 48, which their code has no case for */
        for (p = 0; p < 150u; p++) {
            tick_input(L, 0, -1.0f, 0u, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
        }
        for (p = 0; p < 150u; p++) {
            tick_input(L, 0, 0, 0u, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
        }
        {
            sa1_state_ex ex;
            memset(&ex, 0, sizeof ex);
            ex.struct_size = sizeof ex;
            sa1_character_state_ex(g_ctx, F, &ex);
            state_of(F, &fs);
            state_of(L, &ls);
            printf("  info  FPL4 %s stands behind him before any drop-in: mode %d smode %d action %d, %.1f from him, y %.1f (he %.1f)\n", kind_name(kk[ki]), (int)ex.mode, (int)ex.smode, (int)fs.action, (double)dist_h(ls.position, fs.position), (double)fs.position[1], (double)ls.position[1]);
        }
        st = sa1_follower_respawn(g_ctx, F);
        CHECK(st == SA1_OK, "FPL4 %s: sa1_follower_respawn: %s", kind_name(kk[ki]), sa1_status_string(st));
        {
            sa1_state a;
            sa1_state_ex ex;
            float top = -1e9f;
            state_of(F, &a);
            for (p = 0; p < 150u; p++) {
                tick_input(L, 0, 0, 0u, 0.0f, 1.0f / 30.0f);
                tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
                poll_events(F, &s);
                state_of(F, &fs);
                top = fs.position[1] > top ? fs.position[1] : top;
                fpl_mode_note(F, o.modes_after);
            }
            memset(&ex, 0, sizeof ex);
            ex.struct_size = sizeof ex;
            sa1_character_state_ex(g_ctx, F, &ex);
            state_of(L, &ls);
            sa1_follower_info_get(g_ctx, F, &info);
            printf("  info  FPL4 %s 150 pairs after the drop-in, he stands: highest y %.1f, now y %.1f, %.1f from him, mode %d smode %d action %d, AI mode %d smode %d, %u respawns\n", kind_name(kk[ki]), (double)top, (double)fs.position[1],
                   (double)dist_h(ls.position, fs.position), (int)ex.mode, (int)ex.smode, (int)fs.action, (int)info.ai_mode, (int)info.ai_smode, (unsigned)info.respawns);
            state_of(F, &a);
            /* he walks and jumps again: it still follows, on its own code */
            for (p = 0; p < 300u; p++) {
                tick_input(L, 0, p < 100u ? -1.0f : 0.0f, p == 40u ? SA1_BUTTON_JUMP : 0u, 0.0f, 1.0f / 30.0f);
                tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
                poll_events(F, &s);
                state_of(F, &fs);
                fpl_mode_note(F, o.modes_after);
            }
            o.moved_after = dist_h(fs.position, a.position);
            o.last_y = fs.position[1];
        }
        fpl_modes_text(o.modes_after, text, sizeof text);
        sa1_follower_info_get(g_ctx, F, &info);
        state_of(L, &ls);
        CHECK(s.faults == 0u && s.unimpl == 0u, "FPL4 %s after the drop-in, 450 pairs: %u faults, %u unimplemented; it moved %.0f units with him, now %.1f from him, y %.1f; AI mode %d smode %d, %u respawns; modes%s",
              kind_name(kk[ki]), (unsigned)s.faults, (unsigned)s.unimpl, (double)o.moved_after, (double)dist_h(ls.position, fs.position), (double)o.last_y, (int)info.ai_mode, (int)info.ai_smode, (unsigned)info.respawns, text);
        CHECK(o.moved_after > 50.0f && dist_h(ls.position, fs.position) < 200.0f, "FPL4 %s still follows after the drop-in (%.0f units, %.1f from him)", kind_name(kk[ki]), (double)o.moved_after, (double)dist_h(ls.position, fs.position));
        /* whose code ran: their modes differ, and the brokenness is pinned: Eggman's buttons are pad 0's, Tikal's her own */
        if (kk[ki] == SA1_CHAR_EGGMAN) {
            CHECK(o.modes[5] > 0u && o.modes_after[17] == 0u, "FPL4 Eggman jumps with his own code (mode 5 x%u) and never reaches his debug mode (mode 17 x%u): nobody presses pad 0", (unsigned)o.modes[5], (unsigned)o.modes_after[17]);
        } else {
            CHECK(o.modes[3] > 0u && o.modes_after[11] > 0u, "FPL4 Tikal jumps with her own code (mode 3 x%u), and after a drop-in the AI's flying button is her debug button (mode 11 x%u)", (unsigned)o.modes[3], (unsigned)o.modes_after[11]);
        }
        /* the hold */
        st = sa1_follower_hold(g_ctx, F, 1);
        for (p = 0; st == SA1_OK && p < 120u; p++) {
            tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
        }
        state_of(F, &fs);
        sa1_follower_info_get(g_ctx, F, &info);
        printf("  info  FPL5 %s held 120 pairs: %s, y %.1f, AI mode %d\n", kind_name(kk[ki]), sa1_status_string(st), (double)fs.position[1], (int)info.ai_mode);
        st = sa1_follower_hold(g_ctx, F, 0);
        for (p = 0; st == SA1_OK && p < 120u; p++) {
            tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
        }
        state_of(F, &fs);
        sa1_follower_info_get(g_ctx, F, &info);
        CHECK(st == SA1_OK && s.faults == 0u && s.unimpl == 0u, "FPL5 %s let go again: %s, y %.1f, AI mode %d, %u faults", kind_name(kk[ki]), sa1_status_string(st), (double)fs.position[1], (int)info.ai_mode, (unsigned)s.faults);
        /* it can be followed itself: a chain */
        {
            sa1_character_id G = 0;
            st = follower_new_ex(SA1_CHAR_TAILS, F, 0, NULL, &G);
            CHECK(st == SA1_OK, "FPL6 %s can be followed itself: %s", kind_name(kk[ki]), sa1_status_string(st));
            for (p = 0; st == SA1_OK && p < 60u; p++) {
                tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
                tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
                tick_input(G, 0, 0, 0, 0.0f, 1.0f / 30.0f);
                poll_events(F, &s);
            }
            if (G != 0) {
                sa1_character_destroy(g_ctx, G);
            }
        }
        /* the host's options as for any follower: the catch-up speed, a spot, the stand */
        {
            float spot[3] = { 40.0f, 0.0f, 40.0f };
            sa1_state_ex ex;
            sa1_status a1 = sa1_follower_speed(g_ctx, F, 4.0f), a2 = sa1_follower_spot(g_ctx, F, spot), a3 = sa1_follower_stand(g_ctx, F, 1);
            for (p = 0; p < 300u; p++) {
                tick_input(L, 0, p < 120u ? -1.0f : 0.0f, 0u, 0.0f, 1.0f / 30.0f);
                tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
                poll_events(F, &s);
            }
            memset(&ex, 0, sizeof ex);
            ex.struct_size = sizeof ex;
            sa1_character_state_ex(g_ctx, F, &ex);
            state_of(F, &fs);
            state_of(L, &ls);
            sa1_follower_info_get(g_ctx, F, &info);
            CHECK(a1 == SA1_OK && a2 == SA1_OK && a3 == SA1_OK && s.faults == 0u && s.unimpl == 0u && dist_h(ls.position, fs.position) < 200.0f, "FPL9 %s with speed 4, a spot and the stand: %.1f from him, speed factor %.2f, mode %d action %d, %u faults", kind_name(kk[ki]),
                  (double)dist_h(ls.position, fs.position), (double)info.speed_factor, (int)ex.mode, (int)fs.action, (unsigned)s.faults);
            CHECK(ex.mode == 1, "FPL9 %s stands: the game's own stand with the host's stick off (mode %d, action %d)", kind_name(kk[ki]), (int)ex.mode, (int)fs.action);
            sa1_follower_speed(g_ctx, F, 1.0f);
            sa1_follower_stand(g_ctx, F, 0);
        }
        /* what stays refused, as for the playable one: an emote */
        {
            sa1_emote em;
            memset(&em, 0, sizeof em);
            em.struct_size = sizeof em;
            em.count = 1;
            em.items[0].source = SA1_EMOTE_ROW;
            em.items[0].index = 0u;
            st = sa1_character_emote(g_ctx, F, &em);
            CHECK(SA1_FAILED(st), "FPL7 %s: an emote is refused as for the playable one: %s (%s)", kind_name(kk[ki]), sa1_status_string(st), sa1_last_error(g_ctx));
        }
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
    {
        static const struct { const char *name; int ground; int mode; } courses[] = {
            { "flat", G_FLAT, 0 }, { "ledge 60", G_LEDGE, 0 }, { "pit", G_PIT, 0 }, { "gaps", G_GAPS, 0 }, { "wall", G_WALL, 2 }, { "ledge over a void", G_VOID_LEDGE, 3 },
        };
        uint32_t c;
        for (c = 0; c < sizeof courses / sizeof courses[0]; c++) {
            build_ground(courses[c].ground);
            set_ground();
            for (ki = 0; ki < 2; ki++) {
                course_stats o;
                course_run(kk[ki], courses[c].ground, 0.0f, 900u, courses[c].mode, &o);
                CHECK(o.faults == 0u && o.unimpl == 0u, "FPL8 %-18s %-9s mean dh %6.1f max %7.1f respawns %3u distance %3u off-screen; %u faults, %u unimplemented", courses[c].name,
                      kind_name(kk[ki]), (double)o.mean_dh, (double)o.max_dh, (unsigned)o.respawn_d, (unsigned)o.respawn_o, (unsigned)o.faults, (unsigned)o.unimpl);
            }
        }
    }
}

static void test_bringup(void)
{
    static const struct { const char *name; int ground; int mode; } courses[] = {
        { "flat", G_FLAT, 0 }, { "ledge 60", G_LEDGE, 0 }, { "pit", G_PIT, 0 }, { "gaps", G_GAPS, 0 }, { "wall", G_WALL, 2 }, { "ledge over a void", G_VOID_LEDGE, 3 },
    };
    uint32_t c;
    int ki;
    printf("BRINGUP every kind on the courses (F4; distance statistics, respawns by cause, faults):\n");
    for (c = 0; c < sizeof courses / sizeof courses[0]; c++) {
        build_ground(courses[c].ground);
        set_ground();
        for (ki = 0; ki < 6; ki++) {
            course_stats o;
            course_run(k_kinds[ki], courses[c].ground, 0.0f, 900u, courses[c].mode, &o);
            CHECK(o.faults == 0u && o.unimpl == 0u, "BRINGUP %-18s %-9s mean dh %6.1f max %7.1f respawns %3u distance %3u off-screen; %u faults, %u unimplemented", courses[c].name,
                  kind_name(k_kinds[ki]), (double)o.mean_dh, (double)o.max_dh, (unsigned)o.respawn_d, (unsigned)o.respawn_o, (unsigned)o.faults, (unsigned)o.unimpl);
        }
    }
    {
        static const float degs[2] = { 15.0f, 30.0f };
        int dg;
        for (dg = 0; dg < 2; dg++) {
            build_slope(degs[dg], SA1_SURFACE_SOLID);
            set_ground();
            for (ki = 0; ki < 6; ki++) {
                course_stats o;
                course_run(k_kinds[ki], G_FLAT, 0.0f, 600u, 0, &o);
                CHECK(o.faults == 0u && o.unimpl == 0u, "BRINGUP ramp %.0f degrees  %-9s mean dh %6.1f max %7.1f respawns %3u distance %3u off-screen; %u faults, %u unimplemented", (double)degs[dg],
                      kind_name(k_kinds[ki]), (double)o.mean_dh, (double)o.max_dh, (unsigned)o.respawn_d, (unsigned)o.respawn_o, (unsigned)o.faults, (unsigned)o.unimpl);
            }
        }
    }
}

/* the rest of F4's list: a moving platform (8C07506E carries players through fwp[pno]), host water (drowning sets flag 4 for slot 1,
   code_8C07BE68.c:91-92), SET rings and both item boxes, host objects of the followers' mask, an E-102 and a Knuckles leader (G6) */
static void test_bringup2(void)
{
    int ki;
    printf("BRINGUP a moving platform (the leader on it, it rides 200 units to and fro):\n");
    build_ground(G_FLAT);
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        sa1_collision_tri pt[2];
        float a[3] = { -80.0f, 0.0f, -80.0f }, b[3] = { -80.0f, 0.0f, 80.0f }, c[3] = { 80.0f, 0.0f, -80.0f }, d[3] = { 80.0f, 0.0f, 80.0f };
        float pos[3] = { 0.0f, 20.0f, 0.0f };
        uint32_t obj = 0, p;
        sa1_character_id F = 0;
        sa1_leader_state hs;
        stats s;
        memcpy(pt[0].v[0], a, 12);
        memcpy(pt[0].v[1], b, 12);
        memcpy(pt[0].v[2], c, 12);
        pt[0].surface = SA1_SURFACE_SOLID;
        memcpy(pt[1].v[0], c, 12);
        memcpy(pt[1].v[1], b, 12);
        memcpy(pt[1].v[2], d, 12);
        pt[1].surface = SA1_SURFACE_SOLID;
        sa1_collision_object_add(g_ctx, pt, 2u, pos, NULL, &obj);
        memset(&s, 0, sizeof s);
        host_state(&hs, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f);
        follower_new_ex(k_kinds[ki], 0, 0, &hs, &F);
        for (p = 0; p < 600u; p++) {
            float x = 200.0f * sinf((float)p * 0.02f);
            pos[0] = x;
            sa1_collision_object_move(g_ctx, obj, pos, NULL);
            host_state(&hs, x, 20.0f, 0.0f, 200.0f * 0.02f * cosf((float)p * 0.02f) / 2.0f, 0.0f);
            sa1_follower_feed(g_ctx, F, &hs);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
        }
        CHECK(s.faults == 0u && s.unimpl == 0u, "BRINGUP platform %-9s %u respawns; %u faults, %u unimplemented", kind_name(k_kinds[ki]), (unsigned)(s.respawn[0] + s.respawn[1]),
              (unsigned)s.faults, (unsigned)s.unimpl);
        sa1_character_destroy(g_ctx, F);
        sa1_collision_object_remove(g_ctx, obj);
    }
    printf("BRINGUP host water (the leader in a pool, 1,200 pairs: slot 1 drowns by the game's player-1 rule: at breath 1,080 + 780 steps flag 4, a knock-back taken in the same step, no death; E-102 has no breath):\n");
    build_ground(G_POOL);
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        sa1_character_id F = 0;
        sa1_leader_state hs;
        stats s;
        uint32_t p, flag4 = 0, deaths = 0, resp = 0, hurt = 0;
        int max_breath = 0;
        sa1_input in;
        sa1_event ev[64];
        memset(&s, 0, sizeof s);
        host_state(&hs, 384.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        follower_new_ex(k_kinds[ki], 0, 0, &hs, &F);
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.host_flags = SA1_INPUT_HOST_WATER_KNOWN | SA1_INPUT_HOST_IN_WATER;
        for (p = 0; p < 1200u; p++) {
            const sa1g_world *w = sa1_character_world(g_ctx, F);
            uint32_t t, n = 0, k;
            sa1_follower_feed(g_ctx, F, &hs);
            sa1_character_tick(g_ctx, F, &in, 1.0f / 30.0f, NULL, NULL);
            t = rd(w, A_PLAYER_TWP + 4u);
            if (in_arena(t) && (rd(w, t + 4u) & 4u)) {
                flag4++;
            }
            {
                sa1_state_ex x;
                memset(&x, 0, sizeof x);
                x.struct_size = sizeof x;
                if (sa1_character_state_ex(g_ctx, F, &x) == SA1_OK && x.breath_timer > max_breath) {
                    max_breath = x.breath_timer;
                }
            }
            while (sa1_poll_events(g_ctx, ev, 64u, &n) == SA1_OK && n) {
                for (k = 0; k < n; k++) {
                    s.faults += ev[k].type == SA1_EVENT_FAULT;
                    s.unimpl += ev[k].type == SA1_EVENT_UNIMPLEMENTED;
                    deaths += ev[k].type == SA1_EVENT_DEATH;
                    resp += ev[k].type == SA1_EVENT_FOLLOW && ev[k].id == SA1_FOLLOW_EV_RESPAWN;
                    hurt += ev[k].type == SA1_EVENT_HURT;
                }
                n = 0;
            }
        }
        CHECK(s.faults == 0u && s.unimpl == 0u && deaths == 0u && (k_kinds[ki] == SA1_CHAR_GAMMA ? max_breath == 0 && hurt == 0u : max_breath >= 1860 && hurt == 1u),
              "BRINGUP water %-9s longest breath %d, HURT x%u, death events %u, flag 4 at a pair's end %u, respawns %u; %u faults, %u unimplemented", kind_name(k_kinds[ki]), max_breath, (unsigned)hurt, (unsigned)deaths,
              (unsigned)flag4, (unsigned)resp,
              (unsigned)s.faults, (unsigned)s.unimpl);
        sa1_character_destroy(g_ctx, F);
    }
    printf("BRINGUP SET rings and both item boxes on the course, host objects of the mask ENEMY | HAZARD:\n");
    build_ground(G_FLAT);
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        sa1_character_id L, F;
        stats s;
        uint32_t i, p, recs[64], nrec = 0, ho[6], taken = 0;
        sa1_set_event se[32];
        for (i = 0; i < 40u; i++) {
            recs[nrec++] = add_record(SA1_OBJTYPE_RING, 60.0f + 12.0f * (float)i, 8.0f, 0.0f, 0.0f);
        }
        for (i = 0; i < 9u; i++) {
            recs[nrec++] = add_record(i % 2u ? SA1_OBJTYPE_ITEMBOX_AIR : SA1_OBJTYPE_ITEMBOX, 80.0f + 50.0f * (float)i, i % 2u ? 12.0f : 0.0f, 12.0f, (float)i);
        }
        for (i = 0; i < 6u; i++) {
            sa1_hostobj_desc hd;
            memset(&hd, 0, sizeof hd);
            hd.struct_size = sizeof hd;
            hd.kind = i % 2u ? SA1_HOSTOBJ_HAZARD : SA1_HOSTOBJ_ENEMY;
            hd.position[0] = 100.0f + 70.0f * (float)i;
            hd.position[1] = 6.0f;
            hd.position[2] = -6.0f;
            hd.armor = -1;
            hd.touch = -1;
            hd.num_shapes = 1;
            hd.shapes[0].form = SA1_SHAPE_SPHERE;
            hd.shapes[0].size[0] = 6.0f;
            ho[i] = 0;
            sa1_hostobj_add(g_ctx, &hd, &ho[i]);
        }
        L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
        sa1_hostobj_world_mask(g_ctx, L, 0u);
        F = follower_new(k_kinds[ki], L, 0);
        sa1_hostobj_world_mask(g_ctx, F, (1u << SA1_HOSTOBJ_ENEMY) | (1u << SA1_HOSTOBJ_HAZARD));
        memset(&s, 0, sizeof s);
        for (p = 0; p < 900u; p++) {
            uint32_t n = 0, k;
            tick_input(L, 0, (p % 300u) < 150u ? 0.6f : -0.6f, (p % 90u) == 45u ? SA1_BUTTON_JUMP : 0u, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
            sa1_poll_set_events(g_ctx, se, 32u, &n);
            for (k = 0; k < n; k++) {
                taken += se[k].character == F && se[k].type == SA1_SET_EVENT_GONE;
            }
        }
        CHECK(s.faults == 0u && s.unimpl == 0u, "BRINGUP objects %-9s the follower's world took %u records (GONE), EV_ITEM %u %u %u; %u respawns; %u faults, %u unimplemented", kind_name(k_kinds[ki]),
              (unsigned)taken, (unsigned)s.item[1], (unsigned)s.item[5], (unsigned)s.item[8], (unsigned)(s.respawn[0] + s.respawn[1]), (unsigned)s.faults, (unsigned)s.unimpl);
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
        for (i = 0; i < nrec; i++) {
            sa1_set_remove(g_ctx, recs[i]);
        }
        for (i = 0; i < 6u; i++) {
            sa1_hostobj_remove(g_ctx, ho[i], SA1_HOSTOBJ_REMOVE_SILENT);
        }
    }
    printf("BRINGUP a leader of each other kind (G6: counter.b[1], the physics row), a Sonic and a Tails follower, 300 pairs:\n");
    for (ki = 0; ki < 6; ki++) {
        sa1_character_id L = leader_new(k_kinds[ki], 0.0f, 12.0f, 0.0f);
        sa1_character_id F = follower_new(SA1_CHAR_SONIC, L, 0), G = follower_new(SA1_CHAR_TAILS, L, 0);
        stats s, s2;
        uint32_t p;
        memset(&s, 0, sizeof s);
        memset(&s2, 0, sizeof s2);
        for (p = 0; p < 300u; p++) {
            tick_input(L, 0, 1.0f, (p % 60u) == 30u ? SA1_BUTTON_JUMP : 0u, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            tick_input(G, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &s);
        }
        CHECK(s.faults == 0u && s.unimpl == 0u, "BRINGUP leader %-9s %u faults, %u unimplemented", kind_name(k_kinds[ki]), (unsigned)s.faults, (unsigned)s.unimpl);
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, G);
        sa1_character_destroy(g_ctx, L);
    }
}

/* COST (--only COST, --long): the cost of the view behind slot 1, the same run with and without SA1_FOLLOW_GAME_VIEW */
static void test_cost(void)
{
    static const int32_t kinds[2] = { SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES };
    uint32_t n = sizeof(void *) == 8 ? 32u : 16u, ki, mode;
    printf("COST %u followers of one leader, 300 pairs of `run`, ms per follower and pair (read the archive's build and the machine's load with it):\n", (unsigned)n);
    build_ground(G_FLAT);
    set_ground();
    for (ki = 0; ki < 2; ki++) {
        for (mode = 0; mode < 2; mode++) {
            static sa1_character_id F[64];
            sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
            double sum = 0.0, best = 1e30, t0, t1;
            uint32_t i, p;
            for (i = 0; i < n; i++) {
                F[i] = follower_new(kinds[ki], L, SA1_FOLLOW_NO_SET | (mode ? SA1_FOLLOW_GAME_VIEW : 0u));
            }
            for (p = 0; p < 300u; p++) {
                float sx, sy;
                uint32_t b;
                script_run(p, &sx, &sy, &b);
                tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
                t0 = now_ms();
                for (i = 0; i < n; i++) {
                    tick_input(F[i], 0, 0, 0, 0.0f, 1.0f / 30.0f);
                }
                t1 = now_ms() - t0;
                sum += t1;
                if (t1 < best) {
                    best = t1;
                }
            }
            printf("    %-8s %-22s mean %.4f, best pair %.4f\n", kind_name(kinds[ki]), mode ? "SA1_FOLLOW_GAME_VIEW" : "view behind slot 1", sum / 300.0 / (double)n, best / (double)n);
            for (i = 0; i < n; i++) {
                sa1_character_destroy(g_ctx, F[i]);
            }
            sa1_character_destroy(g_ctx, L);
        }
    }
}

static void test_soak(void)
{
    int ki;
    printf("SOAK 30,000 random-leader pairs per kind:\n");
    build_ground(G_FLAT);
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        stats s;
        float mean, maxd;
        uint32_t hidden;
        soak_run(k_kinds[ki], 0, 30000u, &s, &mean, &maxd, &hidden);
        CHECK(s.faults == 0u && s.unimpl == 0u, "SOAK %-9s mean dh %6.1f max %7.1f respawns %u distance %u off-screen, hidden (sampled) %u; %u faults, %u unimplemented; sounds %u", kind_name(k_kinds[ki]),
              (double)mean, (double)maxd, (unsigned)s.respawn[0], (unsigned)s.respawn[1], (unsigned)hidden, (unsigned)s.faults, (unsigned)s.unimpl, (unsigned)s.sounds);
    }
}

#if defined(FT_STUBHITS)
/* a scratch probe only (r098/follow/stubhits.py): which boundary stub of gen/policy/stubs.tsv a follower world reaches, per kind */
extern uint32_t sa1g_stubhit[];
extern const char *const sa1g_stubname[];
extern const uint32_t sa1g_stubcount;

static void test_stubs(void)
{
    int ki;
    build_ground(G_FLAT);
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        stats st;
        float mean, maxd;
        uint32_t hidden, i;
        memset(sa1g_stubhit, 0, sizeof(uint32_t) * sa1g_stubcount);
        soak_run(k_kinds[ki], 0, 3000u, &st, &mean, &maxd, &hidden);
        printf("STUBS %-9s:", kind_name(k_kinds[ki]));
        for (i = 0; i < sa1g_stubcount; i++) {
            if (sa1g_stubhit[i] != 0u) {
                printf(" %s x%u", sa1g_stubname[i], (unsigned)sa1g_stubhit[i]);
            }
        }
        printf("\n");
    }
}
#endif

/* ---- round 0.9.9: keeping up, staying apart, emotes on a follower id (SA1_API_FOLLOWER 2; sadc docs/research/gsa1_099_plan.md 6.4, follow099.md 3.5, 4.2, 5.2, 9) ------------- */
#define A_PHYSICS       0x8C1BDFE4u     /* player_parameter[8], 0x84 each: the world's own copy */
#define PWP_SPD         0x38u
#define PWP_FREE_B5     0x81u           /* free.b[5]: Amy's hammer (8C0DE46C) */
#define PWP_PARAM       0x9Cu
#define PWP_NFRAME      0x130u          /* mj.nframe */
#define PARAM_MAX_X     0x10u
#define PARAM_RUN_ACCEL 0x3Cu
static const uint32_t k_sp_fields[13] = { 0x10u, 0x24u, 0x28u, 0x2Cu, 0x30u, 0x34u, 0x40u, 0x44u, 0x48u, 0x4Cu, 0x08u, 0x68u, 0x64u };

static float slot_speed(const sa1g_world *w, uint32_t slot)
{
    uint32_t p = rd(w, A_PLAYER_PWP + 4u * slot);
    return in_arena(p) ? rdf(w, p + PWP_SPD) : 0.0f;
}

static sa1_status set_spot(sa1_character_id F, float x, float z)
{
    float o[3];
    o[0] = x;
    o[1] = 0.0f;
    o[2] = z;
    return sa1_follower_spot(g_ctx, F, o);
}

/* Fo.SpotOf (g-sa1 feat/sh_follower.lua), units: i is 1-based */
static void spot_of(int i, float s, float *x, float *z)
{
    float r = s * sqrtf((float)i), a = (float)i * 2.39996323f;
    *x = r * cosf(a);
    *z = r * sinf(a);
}

/* x -2048 .. 32768, z -4096 .. 4096 */
static void build_strip(void)
{
    float x, z;
    g_ntri = 0;
    g_slope_k = 0.0f;
    for (x = -2048.0f; x < 32768.0f; x += 128.0f) {
        for (z = -4096.0f; z < 4096.0f; z += 128.0f) {
            hquad(x, x + 128.0f, z, z + 128.0f, 0.0f, 1, SA1_SURFACE_SOLID);
        }
    }
}

/* a solid block across the strip: each face one triangle pair, one side, the outward winding, as a BSP brush (map collision) */
static void add_block(float x0, float x1, float h)
{
    float z;
    for (z = -1024.0f; z < 1024.0f; z += 64.0f) {
        float a[3] = { x0, 0.0f, z }, b[3] = { x0, h, z }, c[3] = { x0, 0.0f, z + 64.0f }, d[3] = { x0, h, z + 64.0f };
        float e[3] = { x1, 0.0f, z }, f[3] = { x1, h, z }, g[3] = { x1, 0.0f, z + 64.0f }, q[3] = { x1, h, z + 64.0f };
        tri(a, c, b, SA1_SURFACE_SOLID);
        tri(c, d, b, SA1_SURFACE_SOLID);
        tri(e, f, g, SA1_SURFACE_SOLID);
        tri(g, f, q, SA1_SURFACE_SOLID);
        tri(b, d, f, SA1_SURFACE_SOLID);
        tri(d, q, f, SA1_SURFACE_SOLID);
    }
}

/* ---- FSP1: at speed 1 and with no spot nothing changes ------------------------------------------------------------------------------- */
static void test_fsp1(void)
{
    uint32_t ha0, hk0, ha1, hk1;
    printf("FSP1 speed 1 and spot 0, never called and called: the DETERMINISM run is identical, nothing is written:\n");
    fx1_run(0, &ha0, &hk0);
    fx1_run(1, &ha1, &hk1);
    CHECK(ha0 == ha1 && hk0 == hk1, "FSP1: never called %08X %08X, called %08X %08X", (unsigned)ha0, (unsigned)hk0, (unsigned)ha1, (unsigned)hk1);
    {
        sa1_character_id L, F;
        sa1g_world *w;
        uint32_t p;
        float accel0;
        build_strip();
        set_ground();
        L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
        F = follower_new(SA1_CHAR_TAILS, L, SA1_FOLLOW_NO_SET);
        w = sa1_character_world(g_ctx, F);
        accel0 = rdf(w, A_PHYSICS + 2u * 0x84u + PARAM_RUN_ACCEL);
        sa1_follower_speed(g_ctx, F, 1.0f);
        for (p = 0; p < 200u; p++) {
            tick_input(L, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
        }
        {
            sa1_follower_info oi;
            info_of(F, &oi);
            CHECK(!w->fol.speed_based && w->fol.speed_k == 1.0f && oi.speed_factor == 1.0f && rdf(w, A_PHYSICS + 2u * 0x84u + PARAM_RUN_ACCEL) == accel0,
                  "FSP1: 200 pairs at speed 1 behind a running leader (dh %.0f): no first reading of the table, k %.2f, speed_factor %.2f, the table %.3f (%.3f)", (double)oi.distance, (double)w->fol.speed_k,
                  (double)oi.speed_factor, (double)rdf(w, A_PHYSICS + 2u * 0x84u + PARAM_RUN_ACCEL), (double)accel0);
        }
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

/* ---- FSP2, FSP3, the straight run behind a Sonic at sustained top speed ----------------------------------------------------------------- */
static int g_fst_on;     /* FST5: fsp_run with the stand on */

typedef struct fsp_out {
    uint32_t respawn_d, respawn_o, faults;
    double mean_dh, max_dh, mean_v, lead_v, worst, frames, lead_frames;
    float top;
} fsp_out;

static void fsp_run(int32_t kind, float speed, uint32_t pairs, fsp_out *o)
{
    sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F = follower_new(kind, L, SA1_FOLLOW_NO_SET);
    const sa1g_world *w = sa1_character_world(g_ctx, F), *lw = sa1_character_world(g_ctx, L);
    stats st;
    uint32_t p, n = 0, nf = 0;
    float lastf = 0.0f, lastl = 0.0f;
    memset(o, 0, sizeof *o);
    memset(&st, 0, sizeof st);
    if (speed != 1.0f) {
        sa1_follower_speed(g_ctx, F, speed);
    }
    if (g_fst_on) {
        sa1_follower_stand(g_ctx, F, 1);
    }
    for (p = 0; p < pairs; p++) {
        sa1_state ls, fs;
        uint32_t pw, lp;
        float d, v, lv, nfr = 0.0f, nlf = 0.0f;
        tick_input(L, 0.0f, 1.0f, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &st);
        state_of(L, &ls);
        state_of(F, &fs);
        d = dist_h(ls.position, fs.position);
        v = slot_speed(w, 1u);
        lv = slot_speed(lw, 0u);
        pw = rd(w, A_PLAYER_PWP + 4u);
        lp = rd(lw, A_PLAYER_PWP);
        if (in_arena(pw) && in_arena(lp)) {
            nfr = rdf(w, pw + PWP_NFRAME);
            nlf = rdf(lw, lp + PWP_NFRAME);
        }
        o->top = v > o->top ? v : o->top;
        if (p >= pairs / 2u) {
            o->mean_dh += d;
            o->max_dh = d > o->max_dh ? d : o->max_dh;
            o->mean_v += v;
            o->lead_v += lv;
            n++;
            if (nfr > lastf && nlf > lastl) {
                o->frames += nfr - lastf;
                o->lead_frames += nlf - lastl;
                nf++;
            }
        }
        if (p + 300u >= pairs) {
            float e = fabsf(v - lv);
            o->worst = e > o->worst ? e : o->worst;
        }
        lastf = nfr;
        lastl = nlf;
    }
    o->mean_dh /= n ? n : 1u;
    o->mean_v /= n ? n : 1u;
    o->lead_v /= n ? n : 1u;
    o->frames /= nf ? nf : 1u;
    o->lead_frames /= nf ? nf : 1u;
    o->respawn_d = st.respawn[0];
    o->respawn_o = st.respawn[1];
    o->faults = st.faults + st.unimpl;
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

static void test_fsp23(void)
{
    static const float speeds[3] = { 1.0f, 3.5f, 4.0f };
    int ki, si;
    printf("FSP2, FSP3 behind a Sonic at top speed, 900 pairs on the flat (speed 1: the game's own five drop-ins in 30 s):\n");
    build_strip();
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        for (si = 0; si < 3; si++) {
            fsp_out o;
            fsp_run(k_kinds[ki], speeds[si], 900u, &o);
            if (si == 0) {
                CHECK(o.respawn_o == 5u && o.respawn_d == 0u && o.faults == 0u, "FSP2 %-8s speed 1: drop-ins %u off-screen + %u distance (the game's 5 + 0), dh mean %.0f, speed mean %.2f; %u faults", kind_name(k_kinds[ki]),
                      (unsigned)o.respawn_o, (unsigned)o.respawn_d, o.mean_dh, o.mean_v, (unsigned)o.faults);
            } else {
                CHECK(o.respawn_o + o.respawn_d == 0u && fabs(o.mean_v - o.lead_v) <= 0.05 && o.max_dh < 60.0 && o.faults == 0u,
                      "FSP2 %-8s speed %.1f: 0 drop-ins (%u), speed mean %.3f against the leader's %.3f, dh mean %.1f max %.1f; %u faults; animation frames per pair %.2f (the leader's %.2f); top speed %.2f", kind_name(k_kinds[ki]),
                      (double)speeds[si], (unsigned)(o.respawn_o + o.respawn_d), o.mean_v, o.lead_v, o.mean_dh, o.max_dh, (unsigned)o.faults, o.frames, o.lead_frames, (double)o.top);
                if (si == 1) {
                    CHECK(o.worst <= 0.5, "FSP3 %-8s speed 3.5: no saw-tooth: the follower's speed within 0.5 of the leader's over the last 300 pairs: worst %.3f", kind_name(k_kinds[ki]), o.worst);
                }
            }
        }
    }
}

/* ---- FSP4: stop and go, 5 s run, 3 s stand, 4 cycles: a constant scale leaves E-102 circling for ever ------------------------------------- */
static void fsp4_run(int32_t kind, float speed)
{
    sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F = follower_new(kind, L, SA1_FOLLOW_NO_SET);
    const sa1g_world *w = sa1_character_world(g_ctx, F);
    stats st;
    uint32_t p, calm = 0, stops = 0, settled = 0, faults;
    int settle_at = -1, stand_p = 0;
    double sum = 0.0;
    memset(&st, 0, sizeof st);
    sa1_follower_speed(g_ctx, F, speed);
    for (p = 0; p < 960u; p++) {
        uint32_t ph = p % 240u;
        sa1_state ls, fs;
        float d, v;
        tick_input(L, 0.0f, ph < 150u ? 1.0f : 0.0f, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, &st);
        state_of(L, &ls);
        state_of(F, &fs);
        d = dist_h(ls.position, fs.position);
        v = slot_speed(w, 1u);
        if (ph == 150u) {
            stand_p = 0;
            settle_at = -1;
            calm = 0;
        }
        if (ph >= 150u) {
            stand_p++;
            if (d < 20.0f && fabsf(v) < 0.05f) {
                calm++;
                if (calm >= 15u && settle_at < 0) {
                    settle_at = stand_p - 15;
                }
            } else {
                calm = 0;
            }
            if (ph == 239u) {
                stops++;
                if (settle_at >= 0) {
                    settled++;
                    sum += settle_at;
                }
            }
        }
    }
    faults = st.faults + st.unimpl;
    CHECK(settled == 4u && stops == 4u && faults == 0u, "FSP4 %-8s speed %.1f: %u of %u stands settle (dh < 20, speed < 0.05 for 15 pairs), mean %.1f pairs; drop-ins %u; %u faults", kind_name(kind), (double)speed, (unsigned)settled, (unsigned)stops,
          settled ? sum / settled : 0.0, (unsigned)(st.respawn[0] + st.respawn[1]), (unsigned)faults);
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

static void test_fsp4(void)
{
    printf("FSP4 stop and go (E-102 circles his leader for ever with a constant scale):\n");
    build_strip();
    set_ground();
    fsp4_run(SA1_CHAR_GAMMA, 2.0f);
    fsp4_run(SA1_CHAR_GAMMA, 3.5f);
    fsp4_run(SA1_CHAR_KNUCKLES, 3.5f);
    fsp4_run(SA1_CHAR_BIG, 4.0f);
}

/* ---- FSP5: back to 1 ----------------------------------------------------------------------------------------------------------------- */
static void test_fsp5(void)
{
    sa1_character_id L, F, G;
    sa1g_world *w;
    const sa1g_world *wg;
    uint32_t p, row, i, pw, tw, bad = 0;
    float accel0, base[13], maxx;
    sa1_follower_info oi;
    printf("FSP5 speed 3 and back to 1: the fields, the world's own table, a second world's table:\n");
    build_strip();
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_TAILS, L, SA1_FOLLOW_NO_SET);
    G = follower_new(SA1_CHAR_TAILS, L, SA1_FOLLOW_NO_SET);
    w = sa1_character_world(g_ctx, F);
    wg = sa1_character_world(g_ctx, G);
    row = 2u;
    accel0 = rdf(w, A_PHYSICS + row * 0x84u + PARAM_RUN_ACCEL);
    for (i = 0; i < 13u; i++) {
        base[i] = rdf(w, A_PHYSICS + row * 0x84u + k_sp_fields[i]);
    }
    CHECK(sa1_follower_speed(g_ctx, F, 3.0f) == SA1_OK, "FSP5 speed 3");
    for (p = 0; p < 40u; p++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(G, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    }
    poll_events(0, NULL);
    info_of(F, &oi);
    CHECK(oi.speed_factor == 1.0f, "FSP5 near the spot: speed_factor %.3f (1), dh %.1f", (double)oi.speed_factor, (double)oi.distance);
    for (p = 0; p < 200u; p++) {
        tick_input(L, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(G, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
    }
    info_of(F, &oi);
    pw = rd(w, A_PLAYER_PWP + 4u);
    tw = rd(w, A_PLAYER_TWP + 4u);
    maxx = rdf(w, pw + PWP_PARAM + PARAM_MAX_X);
    {
        float k = w->fol.speed_k, ta = rdf(w, A_PHYSICS + row * 0x84u + PARAM_RUN_ACCEL);
        CHECK(rd8(w, tw + 9u) == row && oi.speed_factor == k && k > 1.0f && k <= 3.0f && fabsf(maxx - base[0] * k) < 1e-4f && fabsf(ta - accel0 * k) < 1e-5f && rdf(w, pw + PWP_PARAM + PARAM_RUN_ACCEL) == ta,
              "FSP5 while running: k %.3f = speed_factor %.3f, max_x %.3f (table %.3f x k), the table's run_accel %.4f = live %.4f (%.4f x k), dh %.1f", (double)k, (double)oi.speed_factor, (double)maxx, (double)base[0], (double)ta,
              (double)rdf(w, pw + PWP_PARAM + PARAM_RUN_ACCEL), (double)accel0, (double)oi.distance);
    }
    CHECK(rdf(wg, A_PHYSICS + row * 0x84u + PARAM_RUN_ACCEL) == accel0, "FSP5 a second world's table untouched: %.4f (%.4f)", (double)rdf(wg, A_PHYSICS + row * 0x84u + PARAM_RUN_ACCEL), (double)accel0);
    CHECK(sa1_follower_speed(g_ctx, F, 1.0f) == SA1_OK, "FSP5 back to 1");
    for (p = 0; p < 4u; p++) {
        tick_input(L, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(G, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
    }
    for (i = 0; i < 13u; i++) {
        bad += rdf(w, pw + PWP_PARAM + k_sp_fields[i]) != base[i];
        bad += rdf(w, A_PHYSICS + row * 0x84u + k_sp_fields[i]) != base[i];
    }
    info_of(F, &oi);
    CHECK(bad == 0u && rdf(w, A_PHYSICS + row * 0x84u + PARAM_RUN_ACCEL) == accel0 && rdf(w, pw + PWP_PARAM + PARAM_RUN_ACCEL) == accel0 && oi.speed_factor == 1.0f,
          "FSP5 at 1: every field = the table's (%u differ), the table = its first value, run_accel live %.4f table %.4f, speed_factor %.3f", (unsigned)bad, (double)rdf(w, pw + PWP_PARAM + PARAM_RUN_ACCEL), (double)rdf(w, A_PHYSICS + row * 0x84u + PARAM_RUN_ACCEL),
          (double)oi.speed_factor);
    {
        /* then nothing is written: a value of the test's own stays (the game writes max_x_spd only in its speed tasks) */
        uint32_t v;
        float mark = 2.5f;
        memcpy(&v, &mark, 4);
        sa1g_world_wr32(w, pw + PWP_PARAM + PARAM_MAX_X, v);
        for (p = 0; p < 4u; p++) {
            tick_input(L, 0, 1.0f, 0, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            tick_input(G, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
        }
        CHECK(rdf(w, pw + PWP_PARAM + PARAM_MAX_X) == mark, "FSP5 after that nothing is written: a marked max_x_spd stays %.3f", (double)rdf(w, pw + PWP_PARAM + PARAM_MAX_X));
    }
    {
        /* struct_size: an older caller's shorter struct gets no write past its end */
        sa1_follower_info s;
        memset(&s, 0, sizeof s);
        s.struct_size = (uint32_t)offsetof(sa1_follower_info, speed_factor);
        s.speed_factor = 77.0f;
        CHECK(sa1_follower_info_get(g_ctx, F, &s) == SA1_OK && s.speed_factor == 77.0f, "FSP5 sa1_follower_info: struct_size covers the tail: %.1f stays", (double)s.speed_factor);
    }
    sa1_character_destroy(g_ctx, G);
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* ---- FSP6: bad arguments ------------------------------------------------------------------------------------------------------------- */
static void test_fsp6(void)
{
    sa1_character_id L, F;
    float nan_v = 0.0f, inf_v, big[3] = { 1.0f, 0.0f, 0.0f };
    const char *e;
    sa1_status st;
    printf("FSP6 the calls' refusals:\n");
    build_strip();
    set_ground();
    nan_v = nan_v / nan_v;
    inf_v = 1.0f / (nan_v == nan_v ? 1.0f : 0.0f);
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_SONIC, L, 0);
    CHECK(sa1_follower_speed(NULL, F, 2.0f) == SA1_ERR_INVALID_ARGUMENT, "FSP6 no context");
    CHECK(sa1_follower_speed(g_ctx, F, nan_v) == SA1_ERR_INVALID_ARGUMENT, "FSP6 speed NaN");
    CHECK(sa1_follower_speed(g_ctx, F, inf_v) == SA1_ERR_INVALID_ARGUMENT, "FSP6 speed infinity");
    CHECK(sa1_follower_speed(g_ctx, F, 0.99f) == SA1_ERR_INVALID_ARGUMENT, "FSP6 speed under 1");
    CHECK(sa1_follower_speed(g_ctx, F, 8.01f) == SA1_ERR_INVALID_ARGUMENT, "FSP6 speed over 8");
    CHECK(sa1_follower_speed(g_ctx, F, SA1_FOLLOWER_SPEED_MAX) == SA1_OK, "FSP6 speed 8 is taken");
    CHECK(sa1_follower_speed(g_ctx, F, 1.0f) == SA1_OK, "FSP6 speed 1 is taken");
    st = sa1_follower_speed(g_ctx, L, 2.0f);
    e = sa1_last_error(g_ctx);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT && strstr(e, "not a follower") != NULL, "FSP6 a character that is not a follower: %s (%s): a reference world cannot use it", sa1_status_string(st), e);
    st = sa1_follower_speed(g_ctx, 987654, 2.0f);
    CHECK(st == SA1_ERR_NOT_FOUND, "FSP6 speed, no such character: %s", sa1_status_string(st));
    CHECK(sa1_follower_spot(NULL, F, big) == SA1_ERR_INVALID_ARGUMENT, "FSP6 spot, no context");
    CHECK(sa1_follower_spot(g_ctx, F, NULL) == SA1_ERR_INVALID_ARGUMENT, "FSP6 spot NULL");
    big[1] = nan_v;
    CHECK(sa1_follower_spot(g_ctx, F, big) == SA1_ERR_INVALID_ARGUMENT, "FSP6 spot NaN");
    big[1] = inf_v;
    CHECK(sa1_follower_spot(g_ctx, F, big) == SA1_ERR_INVALID_ARGUMENT, "FSP6 spot infinity");
    big[1] = 0.0f;
    st = sa1_follower_spot(g_ctx, L, big);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT && strstr(sa1_last_error(g_ctx), "not a follower") != NULL, "FSP6 spot on a character that is not a follower: %s", sa1_status_string(st));
    st = sa1_follower_spot(g_ctx, 987654, big);
    CHECK(st == SA1_ERR_NOT_FOUND, "FSP6 spot, no such character: %s", sa1_status_string(st));
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* ---- FSP7: speed 4 into a solid block --------------------------------------------------------------------------------------------------- */
static void test_fsp7(void)
{
    int ki;
    printf("FSP7 speed 4, 850 units of run-up to a solid block (its face at x 1000):\n");
    build_strip();
    add_block(1000.0f, 1128.0f, 128.0f);
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        sa1_leader_state hs;
        sa1_character_id F = 0;
        const sa1g_world *w;
        stats st;
        uint32_t p;
        float maxx = -1e9f, top = 0.0f;
        memset(&st, 0, sizeof st);
        host_state(&hs, 150.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        follower_new_ex(k_kinds[ki], 0, SA1_FOLLOW_NO_SET, &hs, &F);
        w = sa1_character_world(g_ctx, F);
        sa1_follower_speed(g_ctx, F, 4.0f);
        for (p = 0; p < 300u; p++) {
            sa1_state fs;
            float v;
            state_of(F, &fs);
            hs.position[0] = fs.position[0] + 500.0f;   /* 500 units ahead: speed 4 all the way, until the block stops it */
            sa1_follower_feed(g_ctx, F, &hs);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &st);
            state_of(F, &fs);
            v = slot_speed(w, 1u);
            top = v > top ? v : top;
            if (st.respawn[0] + st.respawn[1] == 0u && fs.position[1] < 60.0f && fs.position[0] > maxx) {
                maxx = fs.position[0];
            }
        }
        CHECK(maxx <= 1000.0f + 0.01f && st.faults + st.unimpl == 0u, "FSP7 %-8s top speed %.2f units per step; furthest x before a drop-in %.1f; %u faults", kind_name(k_kinds[ki]), (double)top, (double)maxx, (unsigned)(st.faults + st.unimpl));
        sa1_character_destroy(g_ctx, F);
    }
}

/* ---- FSP8: under host water -------------------------------------------------------------------------------------------------------------- */
/* a host-fed leader 60 units behind it on the camera's axis, so it runs at the full k and stays on the leader's camera: no off-screen drop-in resets anything */
static void fsp8_run(float speed, float *ratio, float *k_out, int *hurt_step, uint32_t *faults)
{
    sa1_leader_state hs;
    sa1_character_id F = 0;
    sa1_input in;
    const sa1g_world *w;
    uint32_t p, pw;
    sa1_event ev[64];
    *hurt_step = -1;
    *ratio = 0.0f;
    *k_out = 1.0f;
    *faults = 0u;
    host_state(&hs, 14000.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    follower_new_ex(SA1_CHAR_SONIC, 0, SA1_FOLLOW_NO_SET, &hs, &F);
    w = sa1_character_world(g_ctx, F);
    if (speed != 1.0f) {
        sa1_follower_speed(g_ctx, F, speed);
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.host_flags = SA1_INPUT_HOST_WATER_KNOWN | SA1_INPUT_HOST_IN_WATER;
    for (p = 0; p < 1100u; p++) {
        sa1_state fs;
        uint32_t n = 0, k;
        state_of(F, &fs);
        hs.position[0] = fs.position[0] - 60.0f;
        sa1_follower_feed(g_ctx, F, &hs);
        sa1_character_tick(g_ctx, F, &in, 1.0f / 30.0f, NULL, NULL);
        while (sa1_poll_events(g_ctx, ev, 64u, &n) == SA1_OK && n) {
            for (k = 0; k < n; k++) {
                *faults += ev[k].type == SA1_EVENT_FAULT || ev[k].type == SA1_EVENT_UNIMPLEMENTED;
                if (ev[k].type == SA1_EVENT_HURT && *hurt_step < 0) {
                    *hurt_step = (int)ev[k].step;
                }
            }
            n = 0;
        }
        if (p == 120u) {
            /* the land step has run with the table's row at k: 8C075214 multiplied it by the water's 0.65 */
            uint32_t t = rd(w, A_PLAYER_TWP + 4u), row = in_arena(t) ? rd8(w, t + 9u) : 0u;
            pw = rd(w, A_PLAYER_PWP + 4u);
            *k_out = w->fol.speed_k;
            *ratio = rdf(w, pw + PWP_PARAM + PARAM_RUN_ACCEL) / rdf(w, A_PHYSICS + row * 0x84u + PARAM_RUN_ACCEL);
        }
    }
    sa1_character_destroy(g_ctx, F);
}

static void test_fsp8(void)
{
    float r1, k1, r2, k2;
    int h1, h2;
    uint32_t f1, f2;
    printf("FSP8 host water over a follower at speed 2: run_accel = the table's x 0.65 at k; the air timer runs per step (equal steps to the knock back):\n");
    build_strip();
    {
        /* a water sheet 40 above the floor along its whole way (the flag cannot create water the collision window does not have: libsa1.h, SA1_INPUT_HOST_IN_WATER) */
        float x, z;
        for (x = -512.0f; x < 14208.0f; x += 64.0f) {
            for (z = -192.0f; z < 192.0f; z += 64.0f) {
                hquad(x, x + 64.0f, z, z + 64.0f, 40.0f, 1, SA1_SURFACE_WATER);
            }
        }
    }
    set_ground();
    fsp8_run(1.0f, &r1, &k1, &h1, &f1);
    fsp8_run(2.0f, &r2, &k2, &h2, &f2);
    CHECK(fabsf(r1 - 0.65f) < 0.001f && k1 == 1.0f, "FSP8 speed 1: k %.3f, live run_accel / table's %.4f", (double)k1, (double)r1);
    CHECK(fabsf(r2 - 0.65f) < 0.001f && k2 > 1.5f, "FSP8 speed 2: k %.3f, live run_accel / table's %.4f", (double)k2, (double)r2);
    CHECK(h1 > 0 && h1 == h2 && f1 + f2 == 0u, "FSP8 the knock back at step %d at speed 1, %d at speed 2; %u faults", h1, h2, (unsigned)(f1 + f2));
}

/* ---- FSP9: no extra step ------------------------------------------------------------------------------------------------------------------- */
static void test_fsp9(void)
{
    static const float speeds[4] = { 1.0f, 1.5f, 2.0f, 4.0f };
    int si;
    printf("FSP9 the follower's steps = the leader's at every speed:\n");
    build_strip();
    set_ground();
    for (si = 0; si < 4; si++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F = follower_new(SA1_CHAR_KNUCKLES, L, SA1_FOLLOW_NO_SET);
        uint32_t p, off = 0;
        sa1_follower_speed(g_ctx, F, speeds[si]);
        for (p = 0; p < 300u; p++) {
            float sx, sy;
            uint32_t b;
            script_run(p, &sx, &sy, &b);
            tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
            off += world_steps(L) != world_steps(F);
        }
        CHECK(off == 0u && world_steps(L) == 600u, "FSP9 speed %.1f: %llu steps, the leader %llu; pairs that differ %u", (double)speeds[si], (unsigned long long)world_steps(F), (unsigned long long)world_steps(L), (unsigned)off);
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

/* ---- a crowd ----------------------------------------------------------------------------------------------------------------------------------- */
#define CROWD_MAX 128
typedef struct crowd {
    int n;
    sa1_character_id L, F[CROWD_MAX];
    int32_t kind[CROWD_MAX];
    sa1g_world *w[CROWD_MAX];
    float rad[CROWD_MAX], lrad;
    uint32_t drops[CROWD_MAX], faults;
} crowd;

static float rad_of(int32_t kind)
{
    sa1_physics ph;
    memset(&ph, 0, sizeof ph);
    return sa1_physics_get(g_ctx, kind, &ph) == SA1_OK ? ph.rad : 4.0f;
}

static void crowd_make(crowd *c, const int32_t *kinds, int nk, int n, float speed, float s, float sx, float sy, float sz)
{
    int i;
    memset(c, 0, sizeof *c);
    c->n = n;
    c->L = leader_new(SA1_CHAR_SONIC, sx, sy, sz);
    c->lrad = rad_of(SA1_CHAR_SONIC);
    for (i = 0; i < n; i++) {
        c->kind[i] = kinds[i % nk];
        c->F[i] = follower_new(c->kind[i], c->L, SA1_FOLLOW_NO_SET);
        c->w[i] = sa1_character_world(g_ctx, c->F[i]);
        c->rad[i] = rad_of(c->kind[i]);
        if (speed != 1.0f) {
            sa1_follower_speed(g_ctx, c->F[i], speed);
        }
        if (s > 0.0f) {
            float x, z;
            spot_of(i + 1, s, &x, &z);
            set_spot(c->F[i], x, z);
        }
    }
}

static void crowd_free(crowd *c)
{
    int i;
    for (i = c->n - 1; i >= 0; i--) {
        sa1_character_destroy(g_ctx, c->F[i]);
    }
    sa1_character_destroy(g_ctx, c->L);
}

static void crowd_poll(crowd *c)
{
    sa1_event ev[128];
    uint32_t n = 0, i;
    int j;
    while (sa1_poll_events(g_ctx, ev, 128, &n) == SA1_OK && n) {
        for (i = 0; i < n; i++) {
            c->faults += ev[i].type == SA1_EVENT_FAULT || ev[i].type == SA1_EVENT_UNIMPLEMENTED;
            if (ev[i].type == SA1_EVENT_FOLLOW && ev[i].id == SA1_FOLLOW_EV_RESPAWN) {
                for (j = 0; j < c->n; j++) {
                    c->drops[j] += c->F[j] == ev[i].character;
                }
            }
        }
        n = 0;
    }
}

static void crowd_tick(crowd *c, float sx, float sy, uint32_t b)
{
    int i;
    tick_input(c->L, sx, sy, b, 0.0f, 1.0f / 30.0f);
    for (i = 0; i < c->n; i++) {
        tick_input(c->F[i], 0, 0, 0, 0.0f, 1.0f / 30.0f);
    }
    crowd_poll(c);
}

static uint32_t crowd_drops(const crowd *c)
{
    uint32_t t = 0;
    int i;
    for (i = 0; i < c->n; i++) {
        t += c->drops[i];
    }
    return t;
}

/* "overlapping" = horizontal distance under the two rads with less than 10 units of height between them */
typedef struct olap {
    uint32_t pairs, overlaps, leader_n, overlaps_leader;
    double closest, leader_closest;
} olap;

static void olap_reset(olap *o)
{
    memset(o, 0, sizeof *o);
    o->closest = 1e9;
    o->leader_closest = 1e9;
}

static void olap_sample(olap *o, const crowd *c)
{
    sa1_state ls;
    static sa1_state st[CROWD_MAX];
    int i, j;
    state_of(c->L, &ls);
    for (i = 0; i < c->n; i++) {
        state_of(c->F[i], &st[i]);
    }
    for (i = 0; i < c->n; i++) {
        float dl = dist_h(st[i].position, ls.position);
        o->leader_n++;
        o->leader_closest = dl < o->leader_closest ? dl : o->leader_closest;
        o->overlaps_leader += dl < c->rad[i] + c->lrad && fabsf(st[i].position[1] - ls.position[1]) < 10.0f;
        for (j = i + 1; j < c->n; j++) {
            float d = dist_h(st[i].position, st[j].position);
            o->pairs++;
            o->closest = d < o->closest ? d : o->closest;
            o->overlaps += d < c->rad[i] + c->rad[j] && fabsf(st[i].position[1] - st[j].position[1]) < 10.0f;
        }
    }
}

/* ---- FSA1 ------------------------------------------------------------------------------------------------------------------------------------- */
/* follow099.md 4.1's course: stand, run, stop, stand, weave, stop, stand, a jump in place */
static void fsa1_run(const char *name, const int32_t *kinds, int nk, int n, float speed, float s)
{
    crowd c;
    olap settle, running, after;
    uint32_t p;
    crowd_make(&c, kinds, nk, n, speed, s, 0.0f, 12.0f, 0.0f);
    olap_reset(&settle);
    olap_reset(&running);
    olap_reset(&after);
    for (p = 0; p < 1500u; p++) {
        float sx = 0.0f, sy = 0.0f;
        uint32_t b = 0;
        if (p >= 150u && p < 450u) {
            sy = 1.0f;
        } else if (p >= 750u && p < 1050u) {
            sy = 1.0f;
            sx = 0.7f * sinf((float)p / 30.0f);
        } else if (p == 1320u || p == 1321u) {
            b = SA1_BUTTON_JUMP;
        }
        crowd_tick(&c, sx, sy, b);
        if ((p >= 120u && p < 150u) || (p >= 700u && p < 750u) || (p >= 1270u && p < 1320u)) {
            olap_sample(&settle, &c);
        }
        if (p >= 300u && p < 450u && (p % 2u) == 0u) {
            olap_sample(&running, &c);
        }
        if (p >= 1400u && p < 1500u) {
            olap_sample(&after, &c);
        }
    }
    CHECK(settle.overlaps == 0u && settle.overlaps_leader == 0u && after.overlaps == 0u && after.overlaps_leader == 0u && c.faults == 0u,
          "FSA1 %s, %d followers, speed %.0f, spots of %.0f units: standing %u of %u pairs overlap (closest %.1f), %u with the leader (closest %.1f); after a jump %u; running %u of %u (%.1f %%, not asserted); %u drop-ins, %u faults", name, n, (double)speed, (double)s,
          (unsigned)settle.overlaps, (unsigned)settle.pairs, settle.closest, (unsigned)settle.overlaps_leader, settle.leader_closest, (unsigned)after.overlaps, (unsigned)running.overlaps, (unsigned)running.pairs,
          running.pairs ? 100.0 * running.overlaps / running.pairs : 0.0, (unsigned)crowd_drops(&c), (unsigned)c.faults);
    crowd_free(&c);
}

static void test_fsa1(void)
{
    static const int32_t tails[1] = { SA1_CHAR_TAILS };
    static const int32_t knux[1] = { SA1_CHAR_KNUCKLES };
    static const int32_t mix[6] = { SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_SONIC, SA1_CHAR_GAMMA, SA1_CHAR_AMY, SA1_CHAR_BIG };
    int n = sizeof(void *) == 8 ? 99 : 32;
    printf("FSA1 no follower inside another or the leader, standing (without spots: 100 %%, FX1 proves it):\n");
    build_strip();
    set_ground();
    fsa1_run("8 Tails", tails, 1, 8, 4.0f, 16.0f);
    fsa1_run("8 mixed kinds", mix, 6, 8, 4.0f, 16.0f);
    fsa1_run("2 Tails", tails, 1, 2, 2.0f, 16.0f);
    {
        /* the egg's crowd: spots of 10 units (G-SA1: 40 HU), standing and after a run */
        crowd c;
        olap a, b;
        uint32_t p;
        crowd_make(&c, knux, 1, n, 1.0f, 10.0f, 0.0f, 12.0f, 0.0f);
        olap_reset(&a);
        olap_reset(&b);
        for (p = 0; p < 500u; p++) {
            crowd_tick(&c, 0.0f, (p >= 150u && p < 250u) ? 1.0f : 0.0f, 0);
            if (p >= 120u && p < 150u) {
                olap_sample(&a, &c);
            }
            if (p >= 470u) {
                olap_sample(&b, &c);
            }
        }
        CHECK(a.overlaps + b.overlaps == 0u && c.faults == 0u, "FSA1 %d Knuckles, spots of 10 units: %u of %u pairs overlap standing (closest %.1f), %u of %u after a run (closest %.1f); %u with the leader; %u drop-ins, %u faults", n, (unsigned)a.overlaps, (unsigned)a.pairs,
              a.closest, (unsigned)b.overlaps, (unsigned)b.pairs, b.closest, (unsigned)(a.overlaps_leader + b.overlaps_leader), (unsigned)crowd_drops(&c), (unsigned)c.faults);
        crowd_free(&c);
    }
}

/* ---- FSA2: the easing, the host's units -------------------------------------------------------------------------------------------------------- */
static void test_fsa2(void)
{
    sa1_character_id L, F;
    sa1g_world *w;
    uint32_t p;
    float worst = 0.0f, lastx, lastz;
    sa1_state ls, fs;
    sa1_follower_info oi;
    printf("FSA2 the proxy moves at most 2 units per step, before the first step at once; the info stays the true leader's:\n");
    build_strip();
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_KNUCKLES, L, SA1_FOLLOW_NO_SET);
    w = sa1_character_world(g_ctx, F);
    CHECK(w->steps == 0u && set_spot(F, 40.0f, -24.0f) == SA1_OK && w->fol.spot_cur[0] == 40.0f && w->fol.spot_cur[1] == -24.0f, "FSA2 before the first step: in force at once: %.1f, %.1f", (double)w->fol.spot_cur[0], (double)w->fol.spot_cur[1]);
    tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
    poll_events(0, NULL);
    state_of(L, &ls);
    lastx = rdf(w, w->fol.proxy_twp + 0x20u);
    lastz = rdf(w, w->fol.proxy_twp + 0x28u);
    CHECK(fabsf(lastx - ls.position[0] - 40.0f) < 0.001f && fabsf(lastz - ls.position[2] + 24.0f) < 0.001f && w->fol.last.pos[0] == ls.position[0],
          "FSA2 the first pair: the proxy at the leader + the spot: %.2f, %.2f (leader %.2f, %.2f; what is kept as the leader: x %.2f)", (double)lastx, (double)lastz, (double)ls.position[0], (double)ls.position[2], (double)w->fol.last.pos[0]);
    for (p = 0; p < 150u; p++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
    }
    state_of(L, &ls);
    state_of(F, &fs);
    info_of(F, &oi);
    CHECK(dist_h(fs.position, ls.position) > 25.0f && fabsf(oi.distance - dist_3(fs.position, ls.position)) < 0.01f, "FSA2 it stands by its spot, %.1f from the leader; sa1_follower_info.distance %.1f is the true leader's",
          (double)dist_h(fs.position, ls.position), (double)oi.distance);
    set_spot(F, -40.0f, 24.0f);
    lastx = rdf(w, w->fol.proxy_twp + 0x20u);
    lastz = rdf(w, w->fol.proxy_twp + 0x28u);
    for (p = 0; p < 60u; p++) {
        float x, z, dx, dz;
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
        x = rdf(w, w->fol.proxy_twp + 0x20u);
        z = rdf(w, w->fol.proxy_twp + 0x28u);
        dx = fabsf(x - lastx) / 2.0f;
        dz = fabsf(z - lastz) / 2.0f;
        worst = dx > worst ? dx : worst;
        worst = dz > worst ? dz : worst;
        lastx = x;
        lastz = z;
    }
    state_of(L, &ls);
    CHECK(worst <= 2.0f + 0.001f && worst >= 1.9f && fabsf(lastx - ls.position[0] + 40.0f) < 0.001f && fabsf(lastz - ls.position[2] - 24.0f) < 0.001f, "FSA2 a change after the first step: at most %.3f units per step on an axis; it reaches the new spot: %.2f, %.2f",
          (double)worst, (double)lastx, (double)lastz);
    {
        /* host space: G-SA1 passes Hammer units in Source axes: host = 4 * (x, -z, y). The host's up is ignored */
        sa1_coords co;
        float o[3] = { 64.0f, -32.0f, 100.0f };
        sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);
        sa1_set_coords(g_ctx, &co);
        CHECK(sa1_follower_spot(g_ctx, F, o) == SA1_OK && w->fol.spot[0] == 16.0f && w->fol.spot[1] == 8.0f, "FSA2 SA1_COORDS_PRESET_SOURCE: { 64, -32, 100 } HU is { %.1f, %.1f } units (16, 8)", (double)w->fol.spot[0], (double)w->fol.spot[1]);
        sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
        sa1_set_coords(g_ctx, &co);
    }
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* ---- FSA3: a solid block beside the leader, spots beyond it ------------------------------------------------------------------------------------ */
static void fsa3_run(const char *name, const int32_t *kinds, int nk, float s)
{
    crowd c;
    uint32_t p, past = 0;
    float far_x = -1e9f;
    sa1_state ls;
    crowd_make(&c, kinds, nk, 8, 2.0f, s, 0.0f, 12.0f, 0.0f);
    for (p = 0; p < 900u; p++) {
        int i;
        crowd_tick(&c, 0.0f, p < 120u ? 1.0f : 0.0f, 0);
        for (i = 0; i < 8; i++) {
            sa1_state f8;
            state_of(c.F[i], &f8);
            if (f8.position[1] < 120.0f) {
                past += f8.position[0] > 200.5f;
                far_x = f8.position[0] > far_x ? f8.position[0] : far_x;
            }
        }
    }
    state_of(c.L, &ls);
    CHECK(past == 0u && c.faults == 0u, "FSA3 %s, spots of %.0f units: the leader at x %.1f; follower-frames past the block's face (x 200) %u of 7,200, the furthest x %.1f; %u drop-ins, %u faults", name, (double)s, (double)ls.position[0], (unsigned)past, (double)far_x,
          (unsigned)crowd_drops(&c), (unsigned)c.faults);
    crowd_free(&c);
}

static void test_fsa3(void)
{
    static const int32_t knux[1] = { SA1_CHAR_KNUCKLES };
    static const int32_t mix[6] = { SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_SONIC, SA1_CHAR_GAMMA, SA1_CHAR_AMY, SA1_CHAR_BIG };
    printf("FSA3 a solid block beside the leader, spots beyond it: nobody passes it:\n");
    build_strip();
    add_block(200.0f, 328.0f, 128.0f);
    set_ground();
    fsa3_run("8 Knuckles", knux, 1, 16.0f);
    fsa3_run("8 mixed kinds", mix, 6, 16.0f);
}

/* ---- FSA4: slopes ---------------------------------------------------------------------------------------------------------------------------------- */
static void test_fsa4(void)
{
    static const int32_t mix[6] = { SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_SONIC, SA1_CHAR_GAMMA, SA1_CHAR_AMY, SA1_CHAR_BIG };
    static const float degs[2] = { 15.0f, 30.0f };
    int dg;
    printf("FSA4 slopes: the world's own collision keeps every follower's feet on the ground at its spot:\n");
    for (dg = 0; dg < 2; dg++) {
        crowd c;
        uint32_t p;
        int i;
        float worst = 0.0f;
        build_slope(degs[dg], SA1_SURFACE_SOLID);
        set_ground();
        crowd_make(&c, mix, 6, 6, 4.0f, 16.0f, 1000.0f, -g_slope_k * 1000.0f + 12.0f, 0.0f);
        for (p = 0; p < 600u; p++) {
            crowd_tick(&c, 0.0f, 0.0f, 0);
        }
        for (i = 0; i < 6; i++) {
            sa1_state s6;
            float e;
            state_of(c.F[i], &s6);
            e = fabsf(s6.position[1] - ground_y(s6.position[0]));
            worst = e > worst ? e : worst;
        }
        CHECK(worst < 0.1f && c.faults == 0u, "FSA4 %.0f degrees, 6 mixed kinds, spots of 16 units: |y - ground| at most %.3f; %u drop-ins, %u faults", (double)degs[dg], (double)worst, (unsigned)crowd_drops(&c), (unsigned)c.faults);
        crowd_free(&c);
    }
}

/* ---- FSA5: a hold and its release ---------------------------------------------------------------------------------------------------------------- */
static void test_fsa5(void)
{
    sa1_character_id L, F;
    sa1g_world *w;
    sa1_state ls, fs;
    uint32_t i;
    float d;
    printf("FSA5 a hold and its release place it 10 units behind its spot:\n");
    build_strip();
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_KNUCKLES, L, SA1_FOLLOW_NO_SET);
    w = sa1_character_world(g_ctx, F);
    set_spot(F, 60.0f, 0.0f);
    for (i = 0; i < 120u; i++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
    }
    sa1_follower_hold(g_ctx, F, 1);
    for (i = 0; i < 30u; i++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
    }
    sa1_follower_hold(g_ctx, F, 0);
    for (i = 0; i < 2u; i++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
    }
    state_of(L, &ls);
    state_of(F, &fs);
    {
        float sx = ls.position[0] + w->fol.spot_cur[0] - fs.position[0], sz = ls.position[2] + w->fol.spot_cur[1] - fs.position[2];
        d = sqrtf(sx * sx + sz * sz);
    }
    CHECK(d <= 12.0f && fabsf(fs.position[1] - ls.position[1]) < 40.0f, "FSA5: %.1f units from its spot (at most 12, as FH2 from the leader), %.1f from the leader, dy %.1f", (double)d, (double)dist_h(fs.position, ls.position), (double)(fs.position[1] - ls.position[1]));
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* ---- FSA6: the ledge, the fall rule -------------------------------------------------------------------------------------------------------------- */
static void fsa6_run(int rule_on, uint32_t *total, uint32_t *worst, float *min_part_end, uint32_t *faults)
{
    static const int32_t knux[1] = { SA1_CHAR_KNUCKLES };
    crowd c;
    uint32_t p;
    int i, stopped = 0;
    crowd_make(&c, knux, 1, 8, 2.0f, 16.0f, 0.0f, 12.0f, 0.0f);
    for (p = 0; p < 1200u; p++) {
        sa1_state ls;
        float sy = 0.0f;
        state_of(c.L, &ls);
        if (p >= 30u && ls.position[0] < 190.0f && !stopped) {
            sy = 1.0f;
        } else if (p >= 30u) {
            stopped = 1;
        }
        if (!rule_on) {
            for (i = 0; i < 8; i++) {
                c.w[i]->fol.spot_part = 1.0f;   /* the control: the rule knocked out from outside */
            }
        }
        crowd_tick(&c, 0.0f, sy, 0);
    }
    *total = crowd_drops(&c);
    *worst = 0u;
    for (i = 0; i < 8; i++) {
        *worst = c.drops[i] > *worst ? c.drops[i] : *worst;
    }
    /* the leader runs off, away from the ledge: the spots come back */
    for (p = 0; p < 320u; p++) {
        crowd_tick(&c, 0.0f, -1.0f, 0);
    }
    *min_part_end = 1.0f;
    for (i = 0; i < 8; i++) {
        *min_part_end = c.w[i]->fol.spot_part < *min_part_end ? c.w[i]->fol.spot_part : *min_part_end;
    }
    *faults = c.faults;
    crowd_free(&c);
}

static void test_fsa6(void)
{
    float x, z, part_on, part_off;
    uint32_t total_on, worst_on, faults_on, total_off, worst_off, faults_off;
    printf("FSA6 the ledge: the plane ends at x 256, the leader stands 5 units from the drop, 8 Knuckles, spots of 16 units, 40 s:\n");
    g_ntri = 0;
    g_slope_k = 0.0f;
    for (x = -4096.0f; x < 192.0f; x += 128.0f) {
        for (z = -4096.0f; z < 4096.0f; z += 128.0f) {
            hquad(x, x + 128.0f, z, z + 128.0f, 0.0f, 1, SA1_SURFACE_SOLID);
        }
    }
    set_ground();
    fsa6_run(1, &total_on, &worst_on, &part_on, &faults_on);
    fsa6_run(0, &total_off, &worst_off, &part_off, &faults_off);
    CHECK(worst_on <= 1u && faults_on == 0u, "FSA6 the fall rule: %u drop-ins in all, at most %u per follower (at most 1); %u faults", (unsigned)total_on, (unsigned)worst_on, (unsigned)faults_on);
    CHECK(part_on == 1.0f, "FSA6 the leader runs off: the spots come back: the least spot_part %.3f", (double)part_on);
    CHECK(total_off > total_on * 3u, "FSA6 the control, the rule knocked out from outside: %u drop-ins in all (follow099.md measured 27); %u faults", (unsigned)total_off, (unsigned)faults_off);
}

/* ---- emotes on a follower id ---------------------------------------------------------------------------------------------------------------------- */
static uint32_t top_row(int32_t kind)
{
    /* the default wheels' top slot, g-sa1 W.DEFAULTS (feat/sh_wheel.lua) */
    switch (kind) {
    case SA1_CHAR_SONIC: return 75u;
    case SA1_CHAR_TAILS: return 54u;
    case SA1_CHAR_KNUCKLES: return 40u;
    case SA1_CHAR_AMY: return 42u;
    case SA1_CHAR_GAMMA: return 67u;
    default: return 48u;
    }
}

static sa1_status emote_of(sa1_character_id id, uint32_t row, uint32_t flags)
{
    sa1_emote e;
    memset(&e, 0, sizeof e);
    e.struct_size = sizeof e;
    e.count = 1;
    e.items[0].source = SA1_EMOTE_ROW;
    e.items[0].index = row;
    e.link = -1;
    e.flags = flags;
    return sa1_character_emote(g_ctx, id, &e);
}

static void ex_of(sa1_character_id id, sa1_state_ex *x)
{
    memset(x, 0, sizeof *x);
    x->struct_size = sizeof *x;
    sa1_character_state_ex(g_ctx, id, x);
}

static void pairs_of(sa1_character_id L, sa1_character_id F, uint32_t n, float sy, stats *st)
{
    uint32_t p;
    for (p = 0; p < n; p++) {
        tick_input(L, 0, sy, 0, 0.0f, 1.0f / 30.0f);
        tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(F, st);
    }
}

static void test_fem1(void)
{
    int ki;
    printf("FEM1 the top slot of each kind's default wheel, on a follower at rest:\n");
    build_strip();
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F = follower_new(k_kinds[ki], L, SA1_FOLLOW_NO_SET);
        uint32_t p, mode_one = 0;
        sa1_status s;
        sa1_state_ex x;
        sa1_state a;
        float moved = 0.0f, p0[3];
        stats st;
        int started = -1;
        memset(&st, 0, sizeof st);
        pairs_of(L, F, 240u, 0.0f, &st);
        s = emote_of(F, top_row(k_kinds[ki]), SA1_EMOTE_HOLD);
        state_of(F, &a);
        memcpy(p0, a.position, sizeof p0);
        for (p = 0; p < 300u; p++) {
            float d;
            pairs_of(L, F, 1u, 0.0f, &st);
            state_of(F, &a);
            ex_of(F, &x);
            d = dist_h(a.position, p0);
            moved = d > moved ? d : moved;
            mode_one += x.mode == 1;
            if (started < 0 && x.emote_state != SA1_EMOTE_STATE_NONE) {
                started = (int)p;
            }
        }
        CHECK(s == SA1_OK && moved == 0.0f && mode_one == 300u && (x.emote_state == SA1_EMOTE_STATE_HOLDING || x.emote_state == SA1_EMOTE_STATE_PLAYING) && x.emote_row == (int32_t)top_row(k_kinds[ki]) && st.faults + st.unimpl == 0u && st.respawn[0] + st.respawn[1] == 0u,
              "FEM1 %-8s row %u: %s, on at pair %d, moved %.2f units in 300 pairs, mode 1 in %u of 300, state %u (row %d, frame %.1f); %u faults", kind_name(k_kinds[ki]), (unsigned)top_row(k_kinds[ki]), sa1_status_string(s), started, (double)moved, (unsigned)mode_one,
              (unsigned)x.emote_state, (int)x.emote_row, (double)x.emote_frame, (unsigned)(st.faults + st.unimpl));
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

/* the stop rules: what ends it, how many pairs it took, and the AI is back: the leader runs 40 pairs and stops, 120 pairs later the follower is near */
enum { FEM2_HURT, FEM2_RESPAWN, FEM2_BOARD, FEM2_HOLD, FEM2_NOW, FEM2_BLEND };

static void fem2_case(const char *what, int32_t kind, int how, uint32_t limit)
{
    sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F = follower_new(kind, L, SA1_FOLLOW_NO_SET);
    sa1_state_ex x;
    sa1_state ls, fs;
    stats st;
    sa1_status s, r = SA1_OK;
    uint32_t p, ended = 0xFFFFFFFFu;
    int was_on;
    memset(&st, 0, sizeof st);
    pairs_of(L, F, 240u, 0.0f, &st);
    s = emote_of(F, top_row(kind), SA1_EMOTE_HOLD);
    pairs_of(L, F, 30u, 0.0f, &st);
    ex_of(F, &x);
    was_on = x.emote_state != SA1_EMOTE_STATE_NONE;
    switch (how) {
    case FEM2_HURT: {
        sa1_damage dm;
        memset(&dm, 0, sizeof dm);
        dm.struct_size = sizeof dm;
        dm.object = 777u;
        r = sa1_character_damage(g_ctx, F, &dm);
        break;
    }
    case FEM2_RESPAWN: r = sa1_follower_respawn(g_ctx, F); break;
    case FEM2_BOARD: r = board_of(F, 1, 0u, 0u); break;
    case FEM2_HOLD: r = sa1_follower_hold(g_ctx, F, 1); break;
    case FEM2_NOW: r = sa1_character_emote_stop(g_ctx, F, SA1_EMOTE_STOP_NOW); break;
    default: r = sa1_character_emote_stop(g_ctx, F, SA1_EMOTE_STOP_BLEND); break;
    }
    for (p = 0; p < 120u && ended == 0xFFFFFFFFu; p++) {
        pairs_of(L, F, 1u, 0.0f, &st);
        ex_of(F, &x);
        if (x.emote_state == SA1_EMOTE_STATE_NONE) {
            ended = p + 1u;
        }
    }
    if (how == FEM2_BOARD) {
        board_of(F, 0, 0u, 0u);
    } else if (how == FEM2_HOLD) {
        sa1_follower_hold(g_ctx, F, 0);
    }
    pairs_of(L, F, 150u, 0.0f, &st);    /* a hit's recovery, a respawn's fall */
    pairs_of(L, F, 40u, 1.0f, &st);
    pairs_of(L, F, 150u, 0.0f, &st);
    state_of(L, &ls);
    state_of(F, &fs);
    CHECK(s == SA1_OK && was_on && r == SA1_OK && ended <= limit && dist_h(ls.position, fs.position) < 30.0f && st.faults + st.unimpl == 0u,
          "FEM2 %-22s %-8s: on %d, the request %s, state 0 after %u pairs (at most %u); the AI is back: dh %.1f after the leader ran and stopped; %u faults", what, kind_name(kind), was_on, sa1_status_string(r), (unsigned)ended, (unsigned)limit,
          (double)dist_h(ls.position, fs.position), (unsigned)(st.faults + st.unimpl));
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

static void test_fem2(void)
{
    printf("FEM2 what ends it:\n");
    build_strip();
    set_ground();
    fem2_case("a hit", SA1_CHAR_TAILS, FEM2_HURT, 1u);
    fem2_case("sa1_follower_respawn", SA1_CHAR_TAILS, FEM2_RESPAWN, 1u);
    fem2_case("the board goes on", SA1_CHAR_SONIC, FEM2_BOARD, 1u);
    fem2_case("sa1_follower_hold", SA1_CHAR_KNUCKLES, FEM2_HOLD, 2u);
    fem2_case("SA1_EMOTE_STOP_NOW", SA1_CHAR_KNUCKLES, FEM2_NOW, 1u);
    fem2_case("SA1_EMOTE_STOP_BLEND", SA1_CHAR_KNUCKLES, FEM2_BLEND, 60u);
}

static void test_fem3(void)
{
    sa1_character_id L, F;
    stats st;
    sa1_status s;
    uint32_t p;
    int on_way = 0, moving = 0, took = -1;
    printf("FEM3 the refusals, each by the rules:\n");
    build_strip();
    set_ground();
    memset(&st, 0, sizeof st);
    /* moving, on the way, then taken: a slow follower behind a leader that stopped */
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_BIG, L, SA1_FOLLOW_NO_SET);
    pairs_of(L, F, 150u, 1.0f, &st);
    for (p = 0; p < 300u && took < 0; p++) {
        s = emote_of(F, top_row(SA1_CHAR_BIG), SA1_EMOTE_HOLD);
        if (s == SA1_OK) {
            took = (int)p;
        } else {
            on_way += s == SA1_ERR_UNSUPPORTED && strcmp(sa1_last_error(g_ctx), "on the way") == 0;
            moving += s == SA1_ERR_UNSUPPORTED && strcmp(sa1_last_error(g_ctx), "moving") == 0;
            pairs_of(L, F, 1u, 0.0f, &st);
        }
    }
    {
        sa1_state ls, fs;
        state_of(L, &ls);
        state_of(F, &fs);
        CHECK(moving > 0 && on_way > 0 && took >= 0 && dist_h(ls.position, fs.position) < 30.0f, "FEM3 Big on his way: \"moving\" x%d, \"on the way\" x%d, taken at pair %d, %.1f from the leader", moving, on_way, took, (double)dist_h(ls.position, fs.position));
    }
    sa1_character_destroy(g_ctx, F);
    F = follower_new(SA1_CHAR_TAILS, L, SA1_FOLLOW_NO_SET);
    pairs_of(L, F, 120u, 0.0f, &st);
    pairs_of(L, F, 120u, 1.0f, &st);
    s = emote_of(F, top_row(SA1_CHAR_TAILS), SA1_EMOTE_HOLD);
    CHECK(s == SA1_ERR_UNSUPPORTED && strcmp(sa1_last_error(g_ctx), "moving") == 0, "FEM3 at speed: %s (%s)", sa1_status_string(s), sa1_last_error(g_ctx));
    {
        /* SA1_EMOTE_IN_AIR on a follower id: the player's option, refused before any rule */
        s = emote_of(F, top_row(SA1_CHAR_TAILS), SA1_EMOTE_IN_AIR);
        CHECK(s == SA1_ERR_INVALID_ARGUMENT && strstr(sa1_last_error(g_ctx), "SA1_EMOTE_IN_AIR") != NULL, "FEM3 SA1_EMOTE_IN_AIR: %s (%s)", sa1_status_string(s), sa1_last_error(g_ctx));
    }
    pairs_of(L, F, 150u, 0.0f, &st);
    sa1_follower_respawn(g_ctx, F);
    pairs_of(L, F, 3u, 0.0f, &st);
    s = emote_of(F, top_row(SA1_CHAR_TAILS), SA1_EMOTE_HOLD);
    CHECK(s == SA1_ERR_UNSUPPORTED && strcmp(sa1_last_error(g_ctx), "in the air") == 0, "FEM3 dropping in: %s (%s)", sa1_status_string(s), sa1_last_error(g_ctx));
    sa1_character_destroy(g_ctx, F);
    F = follower_new(SA1_CHAR_SONIC, L, SA1_FOLLOW_NO_SET);
    pairs_of(L, F, 120u, 0.0f, &st);
    board_of(F, 1, 0u, 0u);
    pairs_of(L, F, 6u, 0.0f, &st);
    s = emote_of(F, top_row(SA1_CHAR_SONIC), SA1_EMOTE_HOLD);
    CHECK(s == SA1_ERR_UNSUPPORTED && strcmp(sa1_last_error(g_ctx), "on the board") == 0, "FEM3 riding: %s (%s)", sa1_status_string(s), sa1_last_error(g_ctx));
    board_of(F, 0, 0u, 0u);
    sa1_character_destroy(g_ctx, F);
    F = follower_new(SA1_CHAR_KNUCKLES, L, SA1_FOLLOW_NO_SET);
    pairs_of(L, F, 150u, 0.0f, &st);
    set_spot(F, 300.0f, 0.0f);          /* the place it has to reach is far: it may not stand and emote where it is */
    pairs_of(L, F, 16u, 0.0f, &st);
    s = emote_of(F, top_row(SA1_CHAR_KNUCKLES), SA1_EMOTE_HOLD);
    {
        const char *why = sa1_last_error(g_ctx);
        CHECK(s == SA1_ERR_UNSUPPORTED && (strcmp(why, "on the way") == 0 || strcmp(why, "moving") == 0 || strcmp(why, "in the air") == 0), "FEM3 a far spot: on its way to it, not taken: %s (%s)", sa1_status_string(s), why);
    }
    CHECK(st.faults + st.unimpl == 0u, "FEM3: %u faults", (unsigned)(st.faults + st.unimpl));
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* --long: every catalogue entry of each kind on a follower */
static void test_fem4(void)
{
    int ki;
    uint32_t total = 0, taken = 0;
    printf("FEM4 every catalogue entry of each kind on a follower, 45 pairs each, stopped, 8 pairs between:\n");
    build_strip();
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F = follower_new(k_kinds[ki], L, SA1_FOLLOW_NO_SET);
        uint32_t n = sa1_emote_count(g_ctx, k_kinds[ki], 0u), i, took = 0, refused = 0;
        stats st;
        memset(&st, 0, sizeof st);
        pairs_of(L, F, 240u, 0.0f, &st);
        for (i = 0; i < n; i++) {
            sa1_emote_info info;
            sa1_emote e;
            memset(&info, 0, sizeof info);
            info.struct_size = sizeof info;
            sa1_emote_get(g_ctx, k_kinds[ki], 0u, i, &info);
            memset(&e, 0, sizeof e);
            e.struct_size = sizeof e;
            e.count = 1;
            e.items[0].source = info.source;
            e.items[0].index = info.index;
            e.link = -1;
            e.flags = SA1_EMOTE_HOLD;
            if (sa1_character_emote(g_ctx, F, &e) == SA1_OK) {
                took++;
            } else {
                refused++;
                printf("    %s: %s refused: %s\n", kind_name(k_kinds[ki]), info.internal_name, sa1_last_error(g_ctx));
            }
            pairs_of(L, F, 45u, 0.0f, &st);
            sa1_character_emote_stop(g_ctx, F, SA1_EMOTE_STOP_NOW);
            pairs_of(L, F, 8u, 0.0f, &st);
        }
        total += n;
        taken += took;
        CHECK(took == n && refused == 0u && st.faults + st.unimpl == 0u && st.respawn[0] + st.respawn[1] == 0u, "FEM4 %-8s %u entries: taken %u, refused %u; drop-ins %u; %u faults", kind_name(k_kinds[ki]), (unsigned)n, (unsigned)took, (unsigned)refused,
              (unsigned)(st.respawn[0] + st.respawn[1]), (unsigned)(st.faults + st.unimpl));
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
    printf("    %u of %u\n", (unsigned)taken, (unsigned)total);
}

static void test_fem5(void)
{
    sa1_character_id L, F;
    const sa1g_world *w;
    stats st;
    sa1_status s;
    sa1_state_ex x;
    uint32_t p, slot_on = 0, proxy_on = 0, pw, ppw;
    printf("FEM5 Amy's attack row on slot 1: 8C0DE46C gets 1, no hammer on the proxy:\n");
    build_strip();
    set_ground();
    memset(&st, 0, sizeof st);
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_AMY, L, SA1_FOLLOW_NO_SET);
    w = sa1_character_world(g_ctx, F);
    pairs_of(L, F, 240u, 0.0f, &st);
    s = emote_of(F, 81u, 0u);
    pw = rd(w, A_PLAYER_PWP + 4u);
    ppw = rd(w, A_PLAYER_PWP);
    for (p = 0; p < 150u; p++) {
        pairs_of(L, F, 1u, 0.0f, &st);
        ex_of(F, &x);
        slot_on += x.emote_state != SA1_EMOTE_STATE_NONE && rd8(w, pw + PWP_FREE_B5) != 0u;
        proxy_on += rd8(w, ppw + PWP_FREE_B5) != 0u;
    }
    CHECK(s == SA1_OK && slot_on > 0u && proxy_on == 0u && x.emote_state == SA1_EMOTE_STATE_NONE && rd8(w, pw + PWP_FREE_B5) == 0u && st.faults + st.unimpl == 0u,
          "FEM5: %s; the hammer on slot 1 in %u pairs, on the proxy in %u; state %u after 150 pairs, the hammer away (%u); %u faults", sa1_status_string(s), (unsigned)slot_on, (unsigned)proxy_on, (unsigned)x.emote_state, (unsigned)rd8(w, pw + PWP_FREE_B5), (unsigned)(st.faults + st.unimpl));
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

static void test_fem6(void)
{
    sa1_character_id L, A, B;
    uint32_t p;
    stats st;
    sa1_status sa, sb;
    sa1_state_ex xa, xb;
    printf("FEM6 a chain: Tails follows Sonic, Knuckles follows Tails; both play:\n");
    build_strip();
    set_ground();
    memset(&st, 0, sizeof st);
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    A = follower_new(SA1_CHAR_TAILS, L, SA1_FOLLOW_NO_SET);
    B = follower_new(SA1_CHAR_KNUCKLES, A, SA1_FOLLOW_NO_SET);
    for (p = 0; p < 240u; p++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(A, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(B, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, &st);
    }
    sa = emote_of(A, top_row(SA1_CHAR_TAILS), SA1_EMOTE_HOLD);
    sb = emote_of(B, top_row(SA1_CHAR_KNUCKLES), SA1_EMOTE_HOLD);
    for (p = 0; p < 200u; p++) {
        tick_input(L, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(A, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        tick_input(B, 0, 0, 0, 0.0f, 1.0f / 30.0f);
        poll_events(0, &st);
    }
    ex_of(A, &xa);
    ex_of(B, &xb);
    CHECK(sa == SA1_OK && sb == SA1_OK && xa.emote_state == SA1_EMOTE_STATE_HOLDING && xb.emote_state == SA1_EMOTE_STATE_HOLDING && st.faults + st.unimpl == 0u, "FEM6: %s, %s; 200 pairs later states %u, %u (3 = holding); %u faults", sa1_status_string(sa),
          sa1_status_string(sb), (unsigned)xa.emote_state, (unsigned)xb.emote_state, (unsigned)(st.faults + st.unimpl));
    sa1_character_destroy(g_ctx, B);
    sa1_character_destroy(g_ctx, A);
    sa1_character_destroy(g_ctx, L);
}

/* ---- the combinations of follow099.md 8 ---------------------------------------------------------------------------------------------------------------- */
/* speed and a spot's change during an emote: it does not move; stopped, it walks to the new spot. A head look on top: both run */
static void test_fem7(void)
{
    sa1_character_id L, F;
    stats st;
    sa1_status s, sl;
    sa1_state a, b, ls;
    sa1_state_ex x;
    sa1_look lk;
    float moved, d;
    printf("FEM7 speed 3, a spot that changes by 56 units and a head look, all during an emote:\n");
    build_strip();
    set_ground();
    memset(&st, 0, sizeof st);
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_KNUCKLES, L, SA1_FOLLOW_NO_SET);
    sa1_follower_speed(g_ctx, F, 3.0f);
    pairs_of(L, F, 240u, 0.0f, &st);
    s = emote_of(F, top_row(SA1_CHAR_KNUCKLES), SA1_EMOTE_HOLD);
    memset(&lk, 0, sizeof lk);
    lk.struct_size = sizeof lk;
    lk.kind = SA1_LOOK_POINT;
    lk.point[0] = 100.0f;
    lk.point[1] = 10.0f;
    sl = sa1_character_look(g_ctx, F, &lk);
    pairs_of(L, F, 30u, 0.0f, &st);
    state_of(F, &a);
    set_spot(F, 40.0f, 40.0f);
    pairs_of(L, F, 120u, 0.0f, &st);
    state_of(F, &b);
    ex_of(F, &x);
    moved = dist_h(a.position, b.position);
    CHECK(s == SA1_OK && sl == SA1_OK && moved == 0.0f && x.emote_state != SA1_EMOTE_STATE_NONE && x.look_kind == SA1_LOOK_POINT && x.head_angle[0] != 0, "FEM7: the emote %s, the look %s; moved %.2f units; emote state %u, look kind %u, head yaw %d", sa1_status_string(s), sa1_status_string(sl), (double)moved,
          (unsigned)x.emote_state, (unsigned)x.look_kind, (int)x.head_angle[0]);
    sa1_character_emote_stop(g_ctx, F, SA1_EMOTE_STOP_BLEND);
    pairs_of(L, F, 120u, 0.0f, &st);
    state_of(F, &b);
    state_of(L, &ls);
    d = sqrtf((b.position[0] - ls.position[0] - 40.0f) * (b.position[0] - ls.position[0] - 40.0f) + (b.position[2] - ls.position[2] - 40.0f) * (b.position[2] - ls.position[2] - 40.0f));
    CHECK(d < 12.0f && st.faults + st.unimpl == 0u, "FEM7 stopped: 120 pairs later %.1f units from its new spot; %u faults", (double)d, (unsigned)(st.faults + st.unimpl));
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* boards with speed and a spot (15 degrees of snow, as follow099.md 3.3's table): no harm, it rides, and an emote does not start on the board */
static void test_fsb1(void)
{
    static const int32_t kinds[2] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS };
    int ki;
    printf("FSB1 a board on a follower with speed 3 and a spot of 20 units:\n");
    build_slope(15.0f, SA1_SURFACE_SOLID | SA1_SURFACE_SNOW);
    set_ground();
    for (ki = 0; ki < 2; ki++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, -100.0f, 12.0f, 0.0f), F = follower_new(kinds[ki], L, SA1_FOLLOW_NO_SET);
        stats st;
        uint32_t p, rides = 0;
        memset(&st, 0, sizeof st);
        sa1_follower_speed(g_ctx, F, 3.0f);
        set_spot(F, 20.0f, 0.0f);
        for (p = 0; p < 600u; p++) {
            sa1_board_state bs;
            if (p == 30u) {
                board_of(L, 1, 0u, 0u);
                board_of(F, 1, 0u, 0u);
            }
            tick_input(L, 0.0f, 1.0f, 0, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &st);
            bstate_of(F, &bs);
            rides += (bs.flags & SA1_BSTATE_ON) != 0u;
        }
        CHECK(st.faults + st.unimpl == 0u && rides > 100u, "FSB1 %-6s riding %u of 600 pairs, drop-ins %u; %u faults", kind_name(kinds[ki]), (unsigned)rides, (unsigned)(st.respawn[0] + st.respawn[1]), (unsigned)(st.faults + st.unimpl));
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

/* a chain of 8, each with the speed and a spot: each spot is around the one it follows */
static void test_fsa7(void)
{
    crowd c;
    olap o;
    uint32_t p;
    int i;
    printf("FSA7 a chain of 8 Knuckles, speed 2, spots of 16 units (follow099.md 4.1: 0.8 %% overlap standing, 16.5 %% with none):\n");
    build_strip();
    set_ground();
    memset(&c, 0, sizeof c);
    c.n = 8;
    c.L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    c.lrad = rad_of(SA1_CHAR_SONIC);
    for (i = 0; i < 8; i++) {
        float x, z;
        c.kind[i] = SA1_CHAR_KNUCKLES;
        c.F[i] = follower_new(SA1_CHAR_KNUCKLES, i == 0 ? c.L : c.F[i - 1], SA1_FOLLOW_NO_SET);
        c.w[i] = sa1_character_world(g_ctx, c.F[i]);
        c.rad[i] = rad_of(SA1_CHAR_KNUCKLES);
        sa1_follower_speed(g_ctx, c.F[i], 2.0f);
        spot_of(i + 1, 16.0f, &x, &z);
        set_spot(c.F[i], x, z);
    }
    olap_reset(&o);
    for (p = 0; p < 1500u; p++) {
        float sx = 0.0f, sy = 0.0f;
        uint32_t b = 0;
        if (p >= 150u && p < 450u) {
            sy = 1.0f;
        } else if (p >= 750u && p < 1050u) {
            sy = 1.0f;
            sx = 0.7f * sinf((float)p / 30.0f);
        } else if (p == 1320u || p == 1321u) {
            b = SA1_BUTTON_JUMP;
        }
        crowd_tick(&c, sx, sy, b);
        if ((p >= 120u && p < 150u) || (p >= 700u && p < 750u) || (p >= 1270u && p < 1320u)) {
            olap_sample(&o, &c);
        }
    }
    CHECK(o.overlaps * 100u <= o.pairs * 5u && c.faults == 0u, "FSA7: standing %u of %u pairs overlap (%.1f %%, closest %.1f); %u with the leader; %u drop-ins, %u faults", (unsigned)o.overlaps, (unsigned)o.pairs, o.pairs ? 100.0 * o.overlaps / o.pairs : 0.0, o.closest,
          (unsigned)o.overlaps_leader, (unsigned)crowd_drops(&c), (unsigned)c.faults);
    crowd_free(&c);
}

/* the new paths on both Windows targets: a second DETERMINISM line (the first, the release's, stays with no call made). Literal spots: no libm in the test's own input */
static void test_det099(void)
{
    static const int32_t kinds[3] = { SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_BIG };
    static const float spots[3][2] = { { 16.0f, 0.0f }, { -13.25f, 19.5f }, { 3.5f, -27.75f } };
    sa1_character_id L, F[3];
    uint32_t p, h = 2166136261u;
    int i;
    stats st;
    printf("FSP, FSA, FEM together on the two Windows targets:\n");
    build_strip();
    set_ground();
    memset(&st, 0, sizeof st);
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    for (i = 0; i < 3; i++) {
        F[i] = follower_new(kinds[i], L, SA1_FOLLOW_NO_SET);
        sa1_follower_speed(g_ctx, F[i], 2.5f);
        set_spot(F[i], spots[i][0], spots[i][1]);
    }
    for (p = 0; p < 600u; p++) {
        float sx, sy;
        uint32_t b;
        script_run(p, &sx, &sy, &b);
        tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
        for (i = 0; i < 3; i++) {
            sa1_state a;
            tick_input(F[i], 0, 0, 0, 0.0f, 1.0f / 30.0f);
            state_of(F[i], &a);
            h = fnv(h, a.position, sizeof a.position);
        }
        if (p == 480u) {
            for (i = 0; i < 3; i++) {
                emote_of(F[i], top_row(kinds[i]), SA1_EMOTE_HOLD);
            }
        }
        poll_events(0, &st);
    }
    {
        sa1_state a;
        state_of(F[0], &a);
        CHECK(L != 0 && F[0] != 0 && F[1] != 0 && F[2] != 0 && a.position[0] > 300.0f && st.faults + st.unimpl == 0u, "FSP, FSA, FEM together: Tails at x %.1f after 600 pairs; %u faults", (double)a.position[0], (unsigned)(st.faults + st.unimpl));
    }
    for (i = 0; i < 3; i++) {
        sa1_character_destroy(g_ctx, F[i]);
    }
    sa1_character_destroy(g_ctx, L);
    printf("DETERMINISM follower099 %08X\n", (unsigned)h);
}

/* ---- round 0.9.9a: a follower that stops reaches the game's own stand and idle (sa1_follower_stand; N7, sadc build/research/r099a/stop/diagnosis.md) ------------------------------------------- */
#define PWP_WAITTIMER   0x00Eu
#define PWP_ACTION_W    0x124u          /* mj.reqaction, mj.action */

typedef struct fst_sample {
    int32_t mode;
    uint32_t action, wait;
    float frame, spd, stroke, off_deg;
} fst_sample;

static void fst_sample_of(const sa1g_world *w, uint32_t slot, fst_sample *s)
{
    uint32_t t = rd(w, A_PLAYER_TWP + 4u * slot), p = rd(w, A_PLAYER_PWP + 4u * slot);
    memset(s, 0, sizeof *s);
    s->mode = -1;
    if (in_arena(t)) {
        int32_t d = (int16_t)(rd(w, SA1G_ADDR_ANALOG + 8u * slot) - rd(w, t + 0x14u + 4u));
        s->mode = (int32_t)(int8_t)rd8(w, t);
        s->off_deg = fabsf((float)d) * 360.0f / 65536.0f;
    }
    if (in_arena(p)) {
        s->action = rd(w, p + PWP_ACTION_W) >> 16;
        s->wait = (rd(w, p + (PWP_WAITTIMER & ~3u)) >> 16) & 0xFFFFu;
        s->frame = rdf(w, p + PWP_NFRAME);
        s->spd = rdf(w, p + PWP_SPD);
    }
    s->stroke = rdf(w, SA1G_ADDR_ANALOG + 8u * slot + 4u);
}

typedef struct fst_out {
    fst_sample end;
    int first_stand;            /* pairs after the stop: the first sample in mode 1 action 0; -1 never */
    uint32_t not_stand_after;   /* samples after it not in mode 1 */
    float frames_per_pair;      /* the last 100 pairs */
    float off_at_stand;
    uint32_t drops, faults;
} fst_out;

/* a run of run_p pairs, a stop, hold_p pairs; slot 1 of every follower */
static void fst_run(int32_t kind, int n, float speed, float spacing, int on, uint32_t run_p, uint32_t hold_p, fst_out *o)
{
    static crowd c;
    int i;
    uint32_t p;
    float last[8];
    crowd_make(&c, &kind, 1, n, speed, n >= 2 ? spacing : 0.0f, 0.0f, 12.0f, 0.0f);
    for (i = 0; i < n; i++) {
        memset(&o[i], 0, sizeof o[i]);
        o[i].first_stand = -1;
        last[i] = 0.0f;
        if (on) {
            sa1_status st = sa1_follower_stand(g_ctx, c.F[i], 1);
            if (st != SA1_OK) {
                printf("  FAIL  sa1_follower_stand: %s\n", sa1_status_string(st));
                g_fail++;
            }
        }
    }
    for (p = 0; p < run_p + hold_p; p++) {
        crowd_tick(&c, 0.0f, p < run_p ? 1.0f : 0.0f, 0u);
        for (i = 0; i < n; i++) {
            fst_sample s;
            fst_sample_of(c.w[i], 1u, &s);
            if (p >= run_p) {
                if (o[i].first_stand >= 0) {
                    o[i].not_stand_after += s.mode != 1;
                } else if (s.mode == 1 && s.action == 0u) {
                    o[i].first_stand = (int)(p - run_p);
                    o[i].off_at_stand = s.off_deg;
                }
                if (p + 100u >= run_p + hold_p && s.frame >= last[i]) {
                    o[i].frames_per_pair += (s.frame - last[i]) / 100.0f;
                }
            }
            last[i] = s.frame;
            o[i].end = s;
        }
    }
    for (i = 0; i < n; i++) {
        o[i].drops = c.drops[i];
        o[i].faults = c.faults;
    }
    crowd_free(&c);
}

/* FST1: the configurations of the lane's text, on: every kind stands. E-102 in his hover (mode 31) is the game's own whoever steers him: FST4 */
static void test_fst1(void)
{
    static const struct { const char *name; int n; float speed, spacing; } k_c[] = {
        { "alone, speed 1", 1, 1.0f, 0.0f }, { "three, speed 1, no room", 3, 1.0f, 0.0f }, { "three, speed 1, 64 HU", 3, 1.0f, 16.0f }, { "three, speed 3, 64 HU", 3, 3.0f, 16.0f },
        { "alone, speed 3", 1, 3.0f, 0.0f }, { "three, speed 1, 16 HU", 3, 1.0f, 4.0f }, { "three, speed 1.31, 16 HU", 3, 1.31f, 4.0f },
    };
    unsigned ci;
    int ki, i;
    printf("FST1 on: 240 pairs of a run, the stop, 700 pairs: the game's own stand, frames moving, for every kind in every configuration:\n");
    build_strip();
    set_ground();
    for (ci = 0; ci < sizeof k_c / sizeof k_c[0]; ci++) {
        for (ki = 0; ki < 6; ki++) {
            fst_out o[3];
            fst_run(k_kinds[ki], k_c[ci].n, k_c[ci].speed, k_c[ci].spacing, 1, 240u, 700u, o);
            for (i = 0; i < k_c[ci].n; i++) {
                if (o[i].end.mode == 31 && k_kinds[ki] == SA1_CHAR_GAMMA) {
                    CHECK(k_c[ci].speed > 1.0f && o[i].end.spd == 0.0f, "FST1 %-26s %-8s f%d: his hover, mode 31, speed %.3f (the game's own, FST4; only above speed 1 here)", k_c[ci].name, kind_name(k_kinds[ki]), i, (double)o[i].end.spd);
                    continue;
                }
                CHECK(o[i].first_stand >= 0 && o[i].first_stand < 300 && o[i].not_stand_after == 0u && o[i].end.mode == 1 && o[i].end.action == 0u && o[i].end.spd == 0.0f && o[i].end.stroke == 0.0f && o[i].frames_per_pair > 0.1f && o[i].faults == 0u,
                      "FST1 %-26s %-8s f%d: stands from pair %d (after it %u pairs elsewhere), at the end mode %d action %u speed %.3f stick %.4f, %.2f frames a pair, facing off by %.0f degrees at the stand; %u drop-ins, %u faults", k_c[ci].name, kind_name(k_kinds[ki]), i,
                      o[i].first_stand, (unsigned)o[i].not_stand_after, (int)o[i].end.mode, (unsigned)o[i].end.action, (double)o[i].end.spd, (double)o[i].end.stroke, (double)o[i].frames_per_pair, (double)o[i].off_at_stand, (unsigned)o[i].drops, (unsigned)o[i].faults);
            }
        }
    }
}

/* FST2: off, as never called: the game's own AI, the static pose the real game's Tails has on the oracle (mode 2, action 9, frame frozen, stick 0.005; r099a/stop stop_wv2) */
static void test_fst2(void)
{
    int ki;
    printf("FST2 off: the game's own: the walk at speed 0 on one frame:\n");
    build_strip();
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        fst_out o[1];
        fst_run(k_kinds[ki], 1, 1.0f, 0.0f, 0, 240u, 700u, o);
        if (k_kinds[ki] == SA1_CHAR_AMY) {
            CHECK(o[0].end.mode == 1 || o[0].end.mode == 2, "FST2 %-8s (she may stop within 1 unit of the AI's point, the one stand the game gives): mode %d action %u", kind_name(k_kinds[ki]), (int)o[0].end.mode, (unsigned)o[0].end.action);
        } else {
            CHECK(o[0].end.mode == 2 && o[0].end.spd == 0.0f && o[0].end.stroke == 0.005f && o[0].frames_per_pair == 0.0f, "FST2 %-8s mode %d action %u speed %.3f stick %.4f, %.3f frames a pair", kind_name(k_kinds[ki]), (int)o[0].end.mode, (unsigned)o[0].end.action,
                  (double)o[0].end.spd, (double)o[0].end.stroke, (double)o[0].frames_per_pair);
        }
    }
    /* on at any moment, off at any moment */
    {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F = follower_new(SA1_CHAR_TAILS, L, SA1_FOLLOW_NO_SET);
        const sa1g_world *w = sa1_character_world(g_ctx, F);
        fst_sample s;
        stats st;
        int m0, m1, m2, m3;
        memset(&st, 0, sizeof st);
        pairs_of(L, F, 120u, 1.0f, &st);
        pairs_of(L, F, 200u, 0.0f, &st);
        fst_sample_of(w, 1u, &s);
        m0 = s.mode;
        sa1_follower_stand(g_ctx, F, 1);
        pairs_of(L, F, 60u, 0.0f, &st);
        fst_sample_of(w, 1u, &s);
        m1 = s.mode;
        sa1_follower_stand(g_ctx, F, 0);
        pairs_of(L, F, 30u, 0.0f, &st);
        fst_sample_of(w, 1u, &s);
        m2 = s.mode;
        pairs_of(L, F, 120u, 1.0f, &st);
        pairs_of(L, F, 200u, 0.0f, &st);
        fst_sample_of(w, 1u, &s);
        m3 = s.mode;
        CHECK(m0 == 2 && m1 == 1 && m3 == 2 && st.faults + st.unimpl == 0u, "FST2 the option at any moment: static (mode %d), on: stands (mode %d), off: the AI's stick is back and it is in mode %d, the next stop the game's own again (mode %d); %u faults", m0, m1, m2, m3, (unsigned)(st.faults + st.unimpl));
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

/* FST3: the idle: the same first idle action as the kind's own player, 1800 steps after it stood */
static void test_fst3(void)
{
    int ki;
    printf("FST3 on: the game's idle comes, as for a player (the wait of 1800 steps = 900 pairs):\n");
    build_strip();
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        sa1_character_id P = leader_new(k_kinds[ki], 0.0f, 12.0f, 0.0f);
        const sa1g_world *pw = sa1_character_world(g_ctx, P);
        sa1_character_id L = 0, F = 0;
        const sa1g_world *w;
        uint32_t p, idle_p = 0, idle_f = 0;
        int at_p = -1, at_f = -1, stand_at = -1;
        for (p = 0; p < 1000u; p++) {
            fst_sample s;
            tick_input(P, 0.0f, p < 30u ? 1.0f : 0.0f, 0u, 0.0f, 1.0f / 30.0f);
            fst_sample_of(pw, 0u, &s);
            if (at_p < 0 && s.mode == 1 && s.action != 0u && p > 200u) {
                at_p = (int)p;
                idle_p = s.action;
            }
        }
        poll_events(0, NULL);
        sa1_character_destroy(g_ctx, P);
        L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
        F = follower_new(k_kinds[ki], L, SA1_FOLLOW_NO_SET);
        w = sa1_character_world(g_ctx, F);
        sa1_follower_stand(g_ctx, F, 1);
        for (p = 0; p < 1200u; p++) {
            fst_sample s;
            tick_input(L, 0.0f, p < 30u ? 1.0f : 0.0f, 0u, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(0, NULL);
            fst_sample_of(w, 1u, &s);
            if (stand_at < 0 && s.mode == 1 && s.action == 0u && p > 30u) {
                stand_at = (int)p;
            }
            if (at_f < 0 && stand_at >= 0 && s.mode == 1 && s.action != 0u) {
                at_f = (int)p;
                idle_f = s.action;
            }
        }
        CHECK(at_p >= 0 && at_f >= 0 && idle_f == idle_p && at_f - stand_at > 850 && at_f - stand_at < 950, "FST3 %-8s the follower stands at pair %d, its first idle action %u at pair %d (%d later); the kind's own player: action %u at pair %d", kind_name(k_kinds[ki]), stand_at, (unsigned)idle_f, at_f,
              at_f - stand_at, (unsigned)idle_p, at_p);
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

/* FST4: E-102's hover is the game's own: under a player's stick he stays in it too (the oracle: r099a/stop stop_ec, mode 33 on water, speed 0, 600 steps) */
static void test_fst4(void)
{
    sa1_character_id P;
    const sa1g_world *w;
    uint32_t p;
    fst_sample s;
    printf("FST4 E-102's hover, under a player's stick let go:\n");
    build_strip();
    set_ground();
    P = leader_new(SA1_CHAR_GAMMA, 0.0f, 12.0f, 0.0f);
    w = sa1_character_world(g_ctx, P);
    for (p = 0; p < 1000u; p++) {
        tick_input(P, 0.0f, p < 400u ? 1.0f : 0.0f, 0u, 0.0f, 1.0f / 30.0f);
        poll_events(0, NULL);
    }
    fst_sample_of(w, 0u, &s);
    CHECK(s.mode == 31 && s.spd == 0.0f, "FST4 a player's E-102, 600 pairs after the stick was let go: mode %d action %u speed %.3f", (int)s.mode, (unsigned)s.action, (double)s.spd);
    sa1_character_destroy(g_ctx, P);
}

/* FST5: what N2 praises: the catch-up numbers of follow099.md 3.4, 3.5 are the same with the stand on: the straight run at each kind's own value, stop and go */
static void test_fst5(void)
{
    static const struct { int32_t kind; float speed; } k_v[] = {
        { SA1_CHAR_SONIC, 1.2f }, { SA1_CHAR_KNUCKLES, 1.3f }, { SA1_CHAR_TAILS, 1.5f }, { SA1_CHAR_GAMMA, 2.5f }, { SA1_CHAR_BIG, 3.5f }, { SA1_CHAR_AMY, 4.0f },
    };
    unsigned i;
    printf("FST5 the catch-up, off against on (follow099.md 3.4): the straight run behind a Sonic at top speed, 900 pairs, and stop and go:\n");
    build_strip();
    set_ground();
    for (i = 0; i < sizeof k_v / sizeof k_v[0]; i++) {
        fsp_out a, b;
        g_fst_on = 0;
        fsp_run(k_v[i].kind, k_v[i].speed, 900u, &a);
        g_fst_on = 1;
        fsp_run(k_v[i].kind, k_v[i].speed, 900u, &b);
        g_fst_on = 0;
        CHECK(a.respawn_o + a.respawn_d == b.respawn_o + b.respawn_d && fabs(a.mean_dh - b.mean_dh) < 1.0 && fabs(a.mean_v - b.mean_v) < 0.02 && a.respawn_o + a.respawn_d == 0u && b.faults == 0u,
              "FST5 %-8s speed %.2f: off: dh %.2f, speed %.3f, %u drop-ins; on: dh %.2f, speed %.3f, %u drop-ins", kind_name(k_v[i].kind), (double)k_v[i].speed, a.mean_dh, a.mean_v, (unsigned)(a.respawn_o + a.respawn_d), b.mean_dh, b.mean_v, (unsigned)(b.respawn_o + b.respawn_d));
    }
    /* 5 s of a run, 3 s of a stand, 4 cycles: every stand stands, and it leaves the stand when the leader runs again */
    for (i = 0; i < sizeof k_v / sizeof k_v[0]; i++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F = follower_new(k_v[i].kind, L, SA1_FOLLOW_NO_SET);
        const sa1g_world *w = sa1_character_world(g_ctx, F);
        stats st;
        uint32_t p, stood = 0, left = 0, hover = 0;
        int this_stood = 0;
        float maxd = 0.0f;
        memset(&st, 0, sizeof st);
        sa1_follower_speed(g_ctx, F, k_v[i].speed);
        sa1_follower_stand(g_ctx, F, 1);
        for (p = 0; p < 960u; p++) {
            uint32_t ph = p % 240u;
            fst_sample s;
            sa1_state ls, fs;
            tick_input(L, 0.0f, ph < 150u ? 1.0f : 0.0f, 0, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &st);
            fst_sample_of(w, 1u, &s);
            state_of(L, &ls);
            state_of(F, &fs);
            if (ph == 0u) {
                this_stood = 0;
            }
            if (ph >= 150u && !this_stood && s.mode == 1 && s.action == 0u) {
                this_stood = 1;
                stood++;
            }
            if (ph == 60u && s.mode != 1 && p >= 240u) {
                left++;
            }
            if (ph == 239u && !this_stood && s.mode == 31 && s.spd == 0.0f && k_v[i].kind == SA1_CHAR_GAMMA) {
                hover++;        /* the game's own, FST4 */
            }
            if (ph >= 60u && ph < 150u) {
                float d = dist_h(ls.position, fs.position);
                maxd = d > maxd ? d : maxd;
            }
        }
        CHECK(stood + hover == 4u && left == 3u && st.respawn[0] + st.respawn[1] == 0u && st.faults + st.unimpl == 0u, "FST5 %-8s speed %.2f, stop and go: stood in %u of 4 stops, in his hover in %u (the game's own, FST4), left the stand in %u of 3 restarts, dh at most %.1f while running; drop-ins %u; %u faults",
              kind_name(k_v[i].kind), (double)k_v[i].speed, (unsigned)stood, (unsigned)hover, (unsigned)left, (double)maxd, (unsigned)(st.respawn[0] + st.respawn[1]), (unsigned)(st.faults + st.unimpl));
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

/* FST6: the arguments */
static void test_fst6(void)
{
    sa1_character_id L, F;
    printf("FST6 sa1_follower_stand's arguments:\n");
    build_strip();
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    F = follower_new(SA1_CHAR_TAILS, L, SA1_FOLLOW_NO_SET);
    CHECK(sa1_follower_stand(NULL, F, 1) == SA1_ERR_INVALID_ARGUMENT, "FST6 no context");
    CHECK(sa1_follower_stand(g_ctx, L, 1) == SA1_ERR_INVALID_ARGUMENT, "FST6 a character that is not a follower");
    CHECK(SA1_FAILED(sa1_follower_stand(g_ctx, 99999, 1)), "FST6 no such character");
    CHECK(sa1_follower_stand(g_ctx, F, 1) == SA1_OK && sa1_follower_stand(g_ctx, F, 0) == SA1_OK && sa1_follower_stand(g_ctx, F, 7) == SA1_OK, "FST6 on, off, any other value is on");
    sa1_character_destroy(g_ctx, F);
    sa1_character_destroy(g_ctx, L);
}

/* FST7: an emote on a standing follower, its end; a slope; a random leader: nothing stays in its walk at speed 0, nothing faults */
static void test_fst7(void)
{
    int ki;
    printf("FST7 on: an emote and its end, a slope, a random leader:\n");
    build_strip();
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F = follower_new(k_kinds[ki], L, SA1_FOLLOW_NO_SET);
        const sa1g_world *w = sa1_character_world(g_ctx, F);
        stats st;
        fst_sample s;
        sa1_status took;
        sa1_state_ex x;
        int was_on;
        memset(&st, 0, sizeof st);
        sa1_follower_stand(g_ctx, F, 1);
        pairs_of(L, F, 240u, 0.0f, &st);
        took = emote_of(F, top_row(k_kinds[ki]), SA1_EMOTE_HOLD);
        pairs_of(L, F, 60u, 0.0f, &st);
        ex_of(F, &x);
        was_on = x.emote_state != SA1_EMOTE_STATE_NONE;
        sa1_character_emote_stop(g_ctx, F, SA1_EMOTE_STOP_BLEND);
        pairs_of(L, F, 60u, 0.0f, &st);
        fst_sample_of(w, 1u, &s);
        CHECK(took == SA1_OK && was_on && s.mode == 1 && s.action == 0u && st.faults + st.unimpl == 0u, "FST7 %-8s an emote: taken %d, playing %d; 60 pairs after its end mode %d action %u; %u faults", kind_name(k_kinds[ki]), took == SA1_OK, was_on, (int)s.mode, (unsigned)s.action,
              (unsigned)(st.faults + st.unimpl));
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
    {
        static const float deg[2] = { 15.0f, 30.0f };
        int di;
        for (di = 0; di < 2; di++) {
            build_slope(deg[di], SA1_SURFACE_SOLID);
            set_ground();
            for (ki = 0; ki < 6; ki++) {
                sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, ground_y(0.0f) + 12.0f, 0.0f), P = leader_new(k_kinds[ki], 0.0f, ground_y(0.0f) + 12.0f, 40.0f), F;
                const sa1g_world *w, *pw = sa1_character_world(g_ctx, P);
                stats st;
                fst_sample s, ps;
                uint32_t p, run = 0, worst = 0;
                F = follower_new(k_kinds[ki], L, SA1_FOLLOW_NO_SET);
                w = sa1_character_world(g_ctx, F);
                memset(&st, 0, sizeof st);
                sa1_follower_stand(g_ctx, F, 1);
                for (p = 0; p < 400u; p++) {
                    tick_input(L, 0.0f, p < 60u ? 1.0f : 0.0f, 0, 0.0f, 1.0f / 30.0f);
                    tick_input(P, 0.0f, p < 60u ? 1.0f : 0.0f, 0, 0.0f, 1.0f / 30.0f);
                    tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
                    poll_events(F, &st);
                    fst_sample_of(w, 1u, &s);
                    run = s.mode == 2 && s.spd == 0.0f ? run + 1u : 0u;
                    worst = run > worst ? run : worst;
                }
                fst_sample_of(w, 1u, &s);
                fst_sample_of(pw, 0u, &ps);
                /* with its stick at 0 a follower on a slope is what a player who lets go is: there both may stay in the walk with a speed of a hair (the game's own), so what counts is that the rest stick holds nothing */
                CHECK(worst <= 45u && st.faults + st.unimpl == 0u, "FST7 %-8s on a %.0f degree slope: at most %u pairs in its walk at speed 0; the follower mode %d action %u speed %.3f; a player of the kind after the same run mode %d action %u speed %.3f; %u faults", kind_name(k_kinds[ki]), (double)deg[di], (unsigned)worst,
                      (int)s.mode, (unsigned)s.action, (double)s.spd, (int)ps.mode, (unsigned)ps.action, (double)ps.spd, (unsigned)(st.faults + st.unimpl));
                sa1_character_destroy(g_ctx, F);
                sa1_character_destroy(g_ctx, P);
                sa1_character_destroy(g_ctx, L);
            }
        }
    }
    build_strip();
    set_ground();
    for (ki = 0; ki < 6; ki++) {
        sa1_character_id L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f), F = follower_new(k_kinds[ki], L, SA1_FOLLOW_NO_SET);
        const sa1g_world *w = sa1_character_world(g_ctx, F);
        stats st;
        uint32_t p, run = 0, worst = 0;
        memset(&st, 0, sizeof st);
        g_lcg = 0x099A0001u + (uint32_t)ki;
        sa1_follower_stand(g_ctx, F, 1);
        for (p = 0; p < 6000u; p++) {
            float sx, sy;
            uint32_t b;
            sa1_state ls;
            fst_sample s;
            state_of(L, &ls);
            script_soak(p, ls.position, &sx, &sy, &b);
            tick_input(L, sx, sy, b, 0.0f, 1.0f / 30.0f);
            tick_input(F, 0, 0, 0, 0.0f, 1.0f / 30.0f);
            poll_events(F, &st);
            fst_sample_of(w, 1u, &s);
            run = s.mode == 2 && s.spd == 0.0f ? run + 1u : 0u;
            worst = run > worst ? run : worst;
        }
        CHECK(worst <= 45u && st.faults + st.unimpl == 0u, "FST7 %-8s a random leader, 6000 pairs: the longest stay in its walk at speed 0 is %u pairs; %u drop-ins; %u faults", kind_name(k_kinds[ki]), (unsigned)worst, (unsigned)(st.respawn[0] + st.respawn[1]), (unsigned)(st.faults + st.unimpl));
        sa1_character_destroy(g_ctx, F);
        sa1_character_destroy(g_ctx, L);
    }
}

/* the new path on the two Windows targets */
static void test_det099a(void)
{
    static const int32_t kinds[3] = { SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_BIG };
    sa1_character_id L, F[3];
    uint32_t p, h = 2166136261u;
    int i;
    printf("the stand on the two Windows targets:\n");
    build_strip();
    set_ground();
    L = leader_new(SA1_CHAR_SONIC, 0.0f, 12.0f, 0.0f);
    for (i = 0; i < 3; i++) {
        float x, z;
        F[i] = follower_new(kinds[i], L, SA1_FOLLOW_NO_SET);
        sa1_follower_stand(g_ctx, F[i], 1);
        sa1_follower_speed(g_ctx, F[i], 2.5f);
        spot_of(i + 1, 16.0f, &x, &z);
        set_spot(F[i], x, z);
    }
    for (p = 0; p < 900u; p++) {
        tick_input(L, 0.0f, p % 300u < 120u ? 1.0f : 0.0f, 0u, 0.0f, 1.0f / 30.0f);
        for (i = 0; i < 3; i++) {
            sa1_state a;
            fst_sample s;
            tick_input(F[i], 0, 0, 0, 0.0f, 1.0f / 30.0f);
            state_of(F[i], &a);
            fst_sample_of(sa1_character_world(g_ctx, F[i]), 1u, &s);
            h = fnv(h, a.position, sizeof a.position);
            h = fnv(h, &s.frame, sizeof s.frame);
            h = fnv(h, &s.action, sizeof s.action);
        }
        poll_events(0, NULL);
    }
    for (i = 0; i < 3; i++) {
        sa1_character_destroy(g_ctx, F[i]);
    }
    sa1_character_destroy(g_ctx, L);
    printf("DETERMINISM follower099a %08X\n", (unsigned)h);
}

/* the cost at 99 followers, one thread (--only FCOST, --long): the same course today, with the speed, with spots, with both; three rounds: the machine may be shared, so the spread counts */
static void test_fcost(void)
{
    static const int32_t knux[1] = { SA1_CHAR_KNUCKLES };
    int n = sizeof(void *) == 8 ? 99 : 32, round, cfg;
    printf("FCOST %d Knuckles, 300 pairs, ms per pair for all (three rounds; the machine may be shared):\n", n);
    build_strip();
    set_ground();
    for (round = 0; round < 3; round++) {
        for (cfg = 0; cfg < 4; cfg++) {
            crowd c;
            uint32_t p;
            double sum = 0.0;
            crowd_make(&c, knux, 1, n, (cfg & 1) ? 2.0f : 1.0f, (cfg & 2) ? 16.0f : 0.0f, 0.0f, 12.0f, 0.0f);
            for (p = 0; p < 300u; p++) {
                float sx, sy;
                uint32_t b;
                int i;
                double t0;
                script_run(p, &sx, &sy, &b);
                tick_input(c.L, sx, sy, b, 0.0f, 1.0f / 30.0f);
                t0 = now_ms();
                for (i = 0; i < c.n; i++) {
                    tick_input(c.F[i], 0, 0, 0, 0.0f, 1.0f / 30.0f);
                }
                sum += now_ms() - t0;
                crowd_poll(&c);
            }
            printf("    round %d, %s: %.2f ms per pair, %u drop-ins, %u faults\n", round, cfg == 0 ? "no call" : cfg == 1 ? "speed 2" : cfg == 2 ? "spots of 16" : "both", sum / 300.0, (unsigned)crowd_drops(&c), (unsigned)c.faults);
            crowd_free(&c);
        }
    }
}

int main(int argc, char **argv)
{
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    const char *gdi = getenv("SA1_TEST_GDI");
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--long")) {
            g_long = 1;
        } else if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            g_only = argv[++i];
        } else {
            gdi = argv[i];
        }
    }
    if (gdi == NULL || !*gdi) {
        printf("usage: follower_test [--long] [--only CASE] GDI\n");
        return 2;
    }
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &g_ctx) != SA1_OK) {
        return 1;
    }
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(g_ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) | (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG) |
                    (1u << SA1_CHAR_SUPER_SONIC) | (1u << SA1_CHAR_EGGMAN) | (1u << SA1_CHAR_TIKAL);
    lo.flags = SA1_LOAD_DEFAULT;
    if (SA1_FAILED(sa1_load_disc(g_ctx, gdi, &lo))) {
        printf("load: %s\n", sa1_last_error(g_ctx));
        return 1;
    }
    printf("follower_test: libsa1 %s, %d-bit, kinds mask %03X\n", sa1_version_string(), (int)(sizeof(void *) * 8), (unsigned)sa1_follower_kinds());
    if (sa1_follower_kinds() == 0u) {
        printf("FAIL: \"%s\": this is the optimised archive with the gate closed; run the -O0 build\n", sa1_follower_kind_reason(0));
        return 1;
    }
    if (want("FS1")) test_fs1();
    if (g_only != NULL && !strcmp(g_only, "FS2")) {
        g_fs_soak = 1;
        test_fs1();
    }
    if (want("FA1")) test_fa1();
    if (want("FA2")) test_fa2();
    if (want("FA4")) test_fa4();
    if (want("FW1")) test_fw1();
    if (want("FV1")) test_fv(0u, "FV1");
    if (want("FV2")) test_fv(SA1_FOLLOW_GAME_VIEW, "FV2");
    if (want("FT1")) test_ft1();
    if (want("FT2")) test_ft23();
    if (want("FP")) test_fp();
    if (want("FC1")) test_fc1();
    if (want("FL1")) test_fl1();
    if (want("FH1")) test_fh1();
    if (want("FH2")) test_fh2();
    if (want("FH3")) test_fh3();
    if (want("FR")) test_fr();
    if (want("FK")) test_fk_fe();
    if (want("FPL")) test_fpl();
    if (want("FD1")) test_fd1();
    if (want("FU1")) test_fu1();
    if (want("FN1")) test_fn1();
    if (want("FI1")) test_fi1();
    if (want("FB1")) test_fb1();
    if (want("FB2")) test_fb2_6();
    if (want("FG")) test_fg();
    if (want("FM1")) test_fm1();
    if (want("FX1")) test_fx1();
    if (want("FM2")) test_fm2();
    if (want("FSP1")) test_fsp1();
    if (want("FSP2")) test_fsp23();
    if (want("FSP4")) test_fsp4();
    if (want("FSP5")) test_fsp5();
    if (want("FSP6")) test_fsp6();
    if (want("FSP7")) test_fsp7();
    if (want("FSP8")) test_fsp8();
    if (want("FSP9")) test_fsp9();
    if (want("FSA1")) test_fsa1();
    if (want("FSA2")) test_fsa2();
    if (want("FSA3")) test_fsa3();
    if (want("FSA4")) test_fsa4();
    if (want("FSA5")) test_fsa5();
    if (want("FSA6")) test_fsa6();
    if (want("FEM1")) test_fem1();
    if (want("FEM2")) test_fem2();
    if (want("FEM3")) test_fem3();
    if (want("FEM5")) test_fem5();
    if (want("FEM6")) test_fem6();
    if (want("FEM7")) test_fem7();
    if (want("FSB1")) test_fsb1();
    if (want("FSA7")) test_fsa7();
    if (want("DET099")) test_det099();
    if (want("FST1")) test_fst1();
    if (want("FST2")) test_fst2();
    if (want("FST3")) test_fst3();
    if (want("FST4")) test_fst4();
    if (want("FST5")) test_fst5();
    if (want("FST6")) test_fst6();
    if (want("FST7")) test_fst7();
    if (want("DET099A")) test_det099a();
    if (g_long || (g_only != NULL && !strcmp(g_only, "FEM4"))) test_fem4();
    if (g_long || (g_only != NULL && !strcmp(g_only, "FCOST"))) test_fcost();
    if (g_long || (g_only != NULL && !strcmp(g_only, "COST"))) test_cost();
    if (g_long || (g_only != NULL && !strcmp(g_only, "FA3"))) {
        if (want("FA3") || g_long) test_fa3();
    }
    if (g_long || (g_only != NULL && !strcmp(g_only, "BRINGUP"))) test_bringup();
    if (g_long || (g_only != NULL && !strcmp(g_only, "BRINGUP2"))) test_bringup2();
    if (g_long || (g_only != NULL && !strcmp(g_only, "SOAK"))) test_soak();
#if defined(FT_STUBHITS)
    if (g_only != NULL && !strcmp(g_only, "STUBS")) test_stubs();
#endif
    sa1_shutdown(g_ctx);
    printf("follower_test: %d failures\n", g_fail);
    return g_fail ? 1 : 0;
}
