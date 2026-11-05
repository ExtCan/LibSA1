/*
 * playable_test.c - Eggman and Tikal as libsa1 characters (SA1_API_PLAYABLE, round 0.9.9; sadc docs/research/gsa1_099_plan.md 5.10, 6.3,
 * playable099.md 2.4-2.6, 3, 7.2). Public API only, a flat GRID ground in the Source preset (4 HU per SA1 unit, z up).
 *
 *   py -3.12 tests/build_playable.py --run --gdi GDI [--debug] [--target x86_64-windows]
 *   playable_test GDI
 *
 * This test PINS THE BROKENNESS (owner, rounds plan 14.1al: "keep the brokeness and the debug button"): a repair of their game code, or a host stand-in
 * for a move, a hurt state, a pose or a drown, fails it. It runs on any archive; a kind with no player row prints one "SKIP behaviour: <kind> is not built yet" line.
 *
 * What a host needs to know (the header's section says the rest):
 *   - sa1_character_create_ex(kind 1 or 4, SA1_CREATE_WORLD) makes a world by the create path of every kind and runs the game's own main.
 *   - SA1_BUTTON_X is their debug button: mode 11 (Tikal) / 17 (Eggman), in the pair of the press, in the air too. Free movement: 20 HU a step along the stick relative to the camera
 *     (the camera's yaw counts; no ground, no gravity). X held with the stick: up or down by the stick's sign, 20 HU a step. A leaves it. B in it: SA1_EVENT_RESPAWN (8C04F8FA's standing stub;
 *     on a Dreamcast an address error). A second X press does nothing.
 *   - The air timer's end: one SA1_EVENT_DEATH with SA1_DEATH_DROWN (the host-object layer sees smode 39, in any world), but their code does nothing with it: smode 39 stays, the mode stays, no
 *     drown pose, no voice, the stick still moves them. The shared countdown task (sadc code_8C07BE68.c:116-118) still asks for the act's restart 240 steps later, as for anyone: one
 *     SA1_EVENT_RESTART id 2. A host that respawns on the DEATH adds a drown the game does not have (its own request comes 4 s later).
 *   - A hit: SA1_OK and nothing happens. The game's death request (SA1_DAMAGE_KILL): SA1_EVENT_DEATH, SA1_EVENT_RESTART 121 steps later. Tikal goes to mode 10. Eggman's code has no switch on the request
 *     (func_8C0EE96C), so his mode 16 is never reached, he stays in the mode he is in and the stick still walks him until the host restarts: MEASURED, and his C is instruction-identical there.
 *   - Eggman's model has two meshes with no texture (materials 94002400, 96002400): draws of archive -1 without SA1_DRAW_NO_TEXTURE, 10 triangles. Tikal has none.
 *   - SA1_CREATE_START_TABLE gives (0, 0, 0): the game has no table for them.
 *   - sa1_character_order, emotes, faces, the look, boards: SA1_ERR_UNSUPPORTED with the reason in sa1_last_error. Round 0.9.9e: sa1_follower_create takes these kinds (tests/follower_test.c FPL). A follower can follow them.
 *
 * Cases: P1 world, moves, jumps; P2 triangles; P3 the debug button; P4 B in debug; P5 a hit, KILL; P5c the stick after KILL; P5b the air timer's end; P6 refusals by their words; P7 a Sonic follower behind them; P8 the action
 * list; P10 the start table; P11 the order; DETERMINISM (x86 = x64: build_board.py's main compares the lines). P9 is the strict gate: `build.py --test` (SA1_BUTTON_X never set).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"

static sa1_context *g_ctx;
static int g_fail, g_ok;
static uint32_t g_hash = 2166136261u;

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
    if (level <= SA1_LOG_WARN) {
        printf("    [libsa1] %s\n", msg);
    }
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

/* ---- the per-kind numbers, all MEASURED: Tikal's in playable099.md 2.5, Eggman's here when his row landed. Two of his differed from the prediction, with P1, P3 and his strict references holding, so
 * the test takes what was measured (review R99.9): the death request leaves his mode at 1 (not 16), and two of his 54 draws have no texture ------------- */
typedef struct kind_row {
    int32_t kind;
    const char *name, *who;
    int32_t debug_mode, death_mode, jump_mode;
    uint32_t triangles, nodes, actions, motions, welds, untextured_draws;
} kind_row;

static const kind_row k_rows[2] = {
    { SA1_CHAR_TIKAL, "tikal", "Tikal", 11, 10, 3, 948u, 103u, 9u, 4u, 14u, 0u },
    { SA1_CHAR_EGGMAN, "eggman", "Eggman", 17, 1, 5, 1138u, 24u, 24u, 5u, 14u, 2u },
};

/* ---- ground ---------------------------------------------------------------------------------------------------------------------- */
typedef struct tbuf {
    sa1_collision_tri *t;
    uint32_t n, cap;
} tbuf;

static void tb_tri(tbuf *b, const float a[3], const float c[3], const float d[3], uint32_t surface)
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        b->cap = b->cap ? 2u * b->cap : 1024u;
        b->t = (sa1_collision_tri *)realloc(b->t, (size_t)b->cap * sizeof *b->t);
    }
    t = &b->t[b->n++];
    memset(t, 0, sizeof *t);
    memcpy(t->v[0], a, 12);
    memcpy(t->v[1], c, 12);
    memcpy(t->v[2], d, 12);
    t->surface = surface;
}

/* a plane z = height; up: its normal points at +z */
static void tb_plane(tbuf *b, float x0, float x1, float y0, float y1, float z, float tile, int up, uint32_t surface)
{
    float x, y;
    for (x = x0; x < x1 - 0.01f; x += tile) {
        for (y = y0; y < y1 - 0.01f; y += tile) {
            float xe = x + tile < x1 ? x + tile : x1, ye = y + tile < y1 ? y + tile : y1;
            float p0[3] = { x, y, z }, p1[3] = { xe, y, z }, p2[3] = { xe, ye, z }, p3[3] = { x, ye, z };
            if (up) {
                tb_tri(b, p0, p1, p2, surface);
                tb_tri(b, p0, p2, p3, surface);
            } else {
                tb_tri(b, p0, p2, p1, surface);
                tb_tri(b, p0, p3, p2, surface);
            }
        }
    }
}

static void tb_wall(tbuf *b, float x0, float y0, float x1, float y1, float z0, float z1, uint32_t surface)
{
    float p0[3] = { x0, y0, z0 }, p1[3] = { x1, y1, z0 }, p2[3] = { x1, y1, z1 }, p3[3] = { x0, y0, z1 };
    tb_tri(b, p0, p1, p2, surface);
    tb_tri(b, p0, p2, p3, surface);
}

static int scene_flat(void)
{
    tbuf b;
    sa1_status st;
    memset(&b, 0, sizeof b);
    tb_plane(&b, -8192.0f, 8192.0f, -8192.0f, 8192.0f, 0.0f, 1024.0f, 1, SA1_SURFACE_SOLID);
    st = sa1_collision_set_static(g_ctx, b.t, b.n);
    free(b.t);
    if (SA1_FAILED(st)) {
        printf("  set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(g_ctx));
        return -1;
    }
    return 0;
}

/* a solid floor 600 HU (150 units) under the top of a closed water box, as tests/drown_test.c's chars case */
static int scene_water(float floor)
{
    tbuf b;
    sa1_status st;
    const float W = 6000.0f;
    memset(&b, 0, sizeof b);
    tb_plane(&b, -8192.0f, 8192.0f, -8192.0f, 8192.0f, floor, 2048.0f, 1, SA1_SURFACE_SOLID);
    tb_plane(&b, -W, W, -W, W, 0.0f, 2.0f * W, 1, SA1_SURFACE_WATER);
    tb_plane(&b, -W, W, -W, W, floor, 2.0f * W, 0, SA1_SURFACE_WATER);
    tb_wall(&b, -W, -W, W, -W, floor, 0.0f, SA1_SURFACE_WATER);
    tb_wall(&b, W, -W, W, W, floor, 0.0f, SA1_SURFACE_WATER);
    tb_wall(&b, W, W, -W, W, floor, 0.0f, SA1_SURFACE_WATER);
    tb_wall(&b, -W, W, -W, -W, floor, 0.0f, SA1_SURFACE_WATER);
    st = sa1_collision_set_static(g_ctx, b.t, b.n);
    free(b.t);
    if (SA1_FAILED(st)) {
        printf("  set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(g_ctx));
        return -1;
    }
    return 0;
}

/* ---- running ---------------------------------------------------------------------------------------------------------------------- */
typedef struct evstats {
    uint32_t faults, unimpl, sounds, respawns, deaths, restarts, hurts, follows, last_death_cause;
    uint32_t death_step, restart_step, restart_id, respawn_step;
} evstats;

static void pump(evstats *s)
{
    sa1_event ev[64];
    uint32_t n, i;
    do {
        n = 0;
        if (sa1_poll_events(g_ctx, ev, 64, &n) != SA1_OK) {
            return;
        }
        for (i = 0; i < n && s != NULL; i++) {
            switch (ev[i].type) {
            case SA1_EVENT_FAULT: s->faults++; printf("    FAULT event: id %08X arg %08X\n", (unsigned)ev[i].id, (unsigned)ev[i].arg); break;
            case SA1_EVENT_UNIMPLEMENTED: s->unimpl++; printf("    UNIMPLEMENTED event: %08X\n", (unsigned)ev[i].id); break;
            case SA1_EVENT_SOUND: case SA1_EVENT_VOICE: s->sounds++; break;
            case SA1_EVENT_RESPAWN: s->respawns++; s->respawn_step = ev[i].step; break;
            case SA1_EVENT_DEATH: s->deaths++; s->last_death_cause = ev[i].arg; s->death_step = ev[i].step; break;
            case SA1_EVENT_RESTART: s->restarts++; s->restart_step = ev[i].step; s->restart_id = ev[i].id; break;
            case SA1_EVENT_HURT: s->hurts++; break;
            case SA1_EVENT_FOLLOW: s->follows++; break;
            default: break;
            }
        }
    } while (n == 64);
}

static sa1_status tick(sa1_character_id id, float sx, float sy, uint32_t buttons, float yaw)
{
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    in.camera_yaw = yaw;
    return sa1_character_tick(g_ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
}

static void state_of(sa1_character_id id, sa1_state_ex *x)
{
    memset(x, 0, sizeof *x);
    x->struct_size = sizeof *x;
    sa1_character_state_ex(g_ctx, id, x);
}

/* n pairs with one input; 0: every tick returned SA1_OK */
static int run(sa1_character_id id, int n, float sx, float sy, uint32_t buttons, float yaw, evstats *s, sa1_state_ex *x)
{
    int i;
    for (i = 0; i < n; i++) {
        sa1_status st = tick(id, sx, sy, buttons, yaw);
        if (st != SA1_OK) {
            printf("    tick %d: %s: %s\n", i, sa1_status_string(st), sa1_last_error(g_ctx));
            return -1;
        }
        pump(s);
        if (x != NULL) {
            state_of(id, x);
        }
    }
    return 0;
}

static sa1_status create(int32_t kind, float x, float y, float z, uint32_t flags, sa1_character_id *id)
{
    sa1_character_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | flags;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    *id = 0;
    return sa1_character_create_ex(g_ctx, &d, id);
}

static float dist3(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return (float)sqrt((double)(dx * dx + dy * dy + dz * dz));
}

static float disth(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1];
    return (float)sqrt((double)(dx * dx + dy * dy));
}

static int near(float a, float b, float tol)
{
    return fabsf(a - b) <= tol;
}

/* ---- P1, P2 ------------------------------------------------------------------------------------------------------------------------- */
static void test_p1_p2(const kind_row *K)
{
    sa1_character_id id = 0;
    sa1_state_ex x, a;
    evstats s;
    const sa1_render *r = NULL;
    sa1_status st;
    int p, moved_ok, jumped = 0;
    float zmax = 0.0f;
    uint32_t q, other = 0, untextured = 0;
    memset(&s, 0, sizeof s);
    printf("P1, P2 %s: a world, 300 pairs, moves, jumps; the triangles standing\n", K->name);
    st = create(K->kind, 0.0f, 0.0f, 40.0f, 0u, &id);
    CHECK(st == SA1_OK && id != 0, "P1 create_ex(%d, SA1_CREATE_WORLD): %s", (int)K->kind, sa1_status_string(st));
    if (st != SA1_OK) {
        printf("    %s\n", sa1_last_error(g_ctx));
        return;
    }
    if (run(id, 30, 0, 0, 0u, 0.0f, &s, &x) != 0) {
        CHECK(0, "P1 the first 30 pairs");
        sa1_character_destroy(g_ctx, id);
        return;
    }
    g_hash = fnv(g_hash, x.position, sizeof x.position);
    CHECK(x.mode == 1, "P1 standing after 30 pairs: mode %d (1), at (%.1f %.1f %.1f)", (int)x.mode, (double)x.position[0], (double)x.position[1], (double)x.position[2]);
    st = sa1_character_render(g_ctx, id, 1.0f, &r);
    if (st == SA1_OK && r != NULL) {
        for (q = 0; q < r->num_draws; q++) {
            const sa1_draw *dr = &r->draws[q];
            if (dr->archive == -1 && !(dr->flags & SA1_DRAW_NO_TEXTURE)) {
                /* a material with no texture in the model */
                untextured++;
            } else if (dr->archive != 0) {
                printf("    draw %u, not archive 0: archive %d texture %d, %u triangles, flags %X, material %08X, source %u\n", (unsigned)q, (int)dr->archive, (int)dr->texture, (unsigned)dr->count, (unsigned)dr->flags, (unsigned)dr->material, (unsigned)dr->source);
                other++;
            }
        }
        g_hash = fnv(g_hash, &r->num_triangles, sizeof r->num_triangles);
        g_hash = fnv(g_hash, &r->num_draws, sizeof r->num_draws);
        CHECK(r->num_triangles == K->triangles && other == 0u && untextured == K->untextured_draws && r->num_sprites == 0u, "P2 standing: %u triangles (%u), %u draws, %u of them neither archive 0 nor the model's untextured (%u; %u), %u sprites",
              (unsigned)r->num_triangles, (unsigned)K->triangles, (unsigned)r->num_draws, (unsigned)other, (unsigned)untextured, (unsigned)K->untextured_draws, (unsigned)r->num_sprites);
    } else {
        CHECK(0, "P2 render: %s", sa1_status_string(st));
    }
    a = x;
    for (p = 0; p < 60; p++) {
        run(id, 1, 0, 1.0f, 0u, 0.0f, &s, &x);
        g_hash = fnv(g_hash, x.position, sizeof x.position);
        g_hash = fnv(g_hash, &x.mode, sizeof x.mode);
        g_hash = fnv(g_hash, &x.action, sizeof x.action);
    }
    moved_ok = disth(a.position, x.position) > 300.0f && x.mode == 2;
    CHECK(moved_ok, "P1 the stick forward, 60 pairs: moved %.1f HU, mode %d (2), action %d", (double)disth(a.position, x.position), (int)x.mode, (int)x.action);
    for (p = 0; p < 40; p++) {
        run(id, 1, 0, 1.0f, p == 0 ? SA1_BUTTON_JUMP : 0u, 0.0f, &s, &x);
        g_hash = fnv(g_hash, x.position, sizeof x.position);
        if (x.mode == K->jump_mode) {
            jumped = 1;
        }
        if (x.position[2] > zmax) {
            zmax = x.position[2];
        }
    }
    CHECK(jumped && zmax > 20.0f, "P1 jump: mode %d seen (%d), the top %.1f HU", jumped ? (int)K->jump_mode : -1, (int)K->jump_mode, (double)zmax);
    if (run(id, 170, 0, 0, 0u, 0.0f, &s, &x) != 0) {
        CHECK(0, "P1 the rest of the 300 pairs");
    }
    g_hash = fnv(g_hash, x.position, sizeof x.position);
    CHECK(s.faults == 0u && s.unimpl == 0u && x.mode == 1, "P1 300 pairs: %u faults, %u unimplemented, mode %d (1), %u sounds", (unsigned)s.faults, (unsigned)s.unimpl, (int)x.mode, (unsigned)s.sounds);
    sa1_character_destroy(g_ctx, id);
}

/* ---- P3, P4 ------------------------------------------------------------------------------------------------------------------------- */
static void test_p3_p4(const kind_row *K)
{
    sa1_character_id id = 0;
    sa1_state_ex x, a, b;
    evstats s;
    const float yaw90 = 1.5707963f;
    float d;
    printf("P3, P4 %s: the debug button\n", K->name);
    memset(&s, 0, sizeof s);
    if (create(K->kind, 0.0f, 0.0f, 40.0f, 0u, &id) != SA1_OK) {
        CHECK(0, "P3 create: %s", sa1_last_error(g_ctx));
        return;
    }
    run(id, 30, 0, 0, 0u, 0.0f, &s, &a);
    CHECK(a.mode == 1, "P3 before the press: mode %d (1)", (int)a.mode);
    run(id, 1, 0, 0, SA1_BUTTON_X, 0.0f, &s, &x);
    CHECK(x.mode == K->debug_mode, "P3 SA1_BUTTON_X: mode %d (%d) in the pair of the press", (int)x.mode, (int)K->debug_mode);
    run(id, 5, 0, 0, 0u, 0.0f, &s, &a);
    CHECK(a.mode == K->debug_mode, "P3 no stick: stays: mode %d", (int)a.mode);
    run(id, 1, 0, 0, SA1_BUTTON_X, 0.0f, &s, &b);
    CHECK(b.mode == K->debug_mode && dist3(a.position, b.position) == 0.0f, "P3 a second press does nothing: mode %d, moved %.3f HU", (int)b.mode, (double)dist3(a.position, b.position));
    run(id, 1, 0, 0, 0u, 0.0f, &s, &a);
    run(id, 40, 0, 1.0f, 0u, 0.0f, &s, &x);
    d = dist3(a.position, x.position);
    g_hash = fnv(g_hash, x.position, sizeof x.position);
    CHECK(near(d, 1600.0f, 1.0f) && near(x.position[0] - a.position[0], 1600.0f, 1.0f) && near(x.position[2], a.position[2], 0.01f),
          "P3 80 steps of stick at camera yaw 0: moved (%.2f %.2f %.2f) HU, 1600 along +x, no gravity", (double)(x.position[0] - a.position[0]), (double)(x.position[1] - a.position[1]),
          (double)(x.position[2] - a.position[2]));
    run(id, 1, 0, 0, 0u, yaw90, &s, &a);
    run(id, 40, 0, 1.0f, 0u, yaw90, &s, &x);
    d = dist3(a.position, x.position);
    g_hash = fnv(g_hash, x.position, sizeof x.position);
    CHECK(near(d, 1600.0f, 1.0f) && near(x.position[1] - a.position[1], 1600.0f, 1.0f) && near(x.position[2], a.position[2], 0.01f),
          "P3 80 steps of stick at camera yaw 90 degrees: moved (%.2f %.2f %.2f) HU, 1600 along +y", (double)(x.position[0] - a.position[0]), (double)(x.position[1] - a.position[1]),
          (double)(x.position[2] - a.position[2]));
    run(id, 1, 0, 0, 0u, 0.0f, &s, &a);
    run(id, 20, 0, 1.0f, SA1_BUTTON_X, 0.0f, &s, &x);
    g_hash = fnv(g_hash, x.position, sizeof x.position);
    CHECK(near(x.position[2] - a.position[2], 800.0f, 1.0f) && disth(a.position, x.position) < 0.01f, "P3 X + stick forward, 40 steps: %.2f HU up (800), %.3f sideways",
          (double)(x.position[2] - a.position[2]), (double)disth(a.position, x.position));
    run(id, 1, 0, 0, 0u, 0.0f, &s, &a);
    run(id, 10, 0, -1.0f, SA1_BUTTON_X, 0.0f, &s, &x);
    CHECK(near(x.position[2] - a.position[2], -200.0f - 200.0f, 1.0f), "P3 X + stick back, 20 steps: %.2f HU (-400)", (double)(x.position[2] - a.position[2]));
    /* P4: B in debug */
    run(id, 1, 0, 0, 0u, 0.0f, &s, &a);
    memset(&s, 0, sizeof s);
    run(id, 1, 0, 0, SA1_BUTTON_ACTION, 0.0f, &s, &x);
    run(id, 5, 0, 0, 0u, 0.0f, &s, &b);
    CHECK(s.respawns == 1u && s.faults == 0u && b.mode == K->debug_mode && dist3(a.position, b.position) == 0.0f,
          "P4 B in debug: %u SA1_EVENT_RESPAWN (1), %u faults, mode %d, moved %.3f HU", (unsigned)s.respawns, (unsigned)s.faults, (int)b.mode, (double)dist3(a.position, b.position));
    /* A leaves; the character falls and lands */
    run(id, 1, 0, 0, SA1_BUTTON_JUMP, 0.0f, &s, &x);
    CHECK(x.mode != K->debug_mode, "P3 A: mode %d, not %d", (int)x.mode, (int)K->debug_mode);
    a = x;
    run(id, 1, 0, 0, 0u, 0.0f, &s, &x);
    CHECK(x.position[2] < a.position[2], "P3 A: it falls: z %.2f then %.2f", (double)a.position[2], (double)x.position[2]);
    run(id, 90, 0, 0, 0u, 0.0f, &s, &x);
    g_hash = fnv(g_hash, x.position, sizeof x.position);
    CHECK(s.faults == 0u && x.mode == 1 && x.position[2] < 2.0f, "P3 back on the ground: mode %d, z %.2f, %u faults", (int)x.mode, (double)x.position[2], (unsigned)s.faults);
    /* the press in the air */
    run(id, 1, 0, 0, SA1_BUTTON_JUMP, 0.0f, &s, &x);
    run(id, 5, 0, 0, 0u, 0.0f, &s, &x);
    a = x;
    run(id, 1, 0, 0, SA1_BUTTON_X, 0.0f, &s, &x);
    CHECK(x.mode == K->debug_mode && a.position[2] > 2.0f, "P3 the press in the air (z %.1f): mode %d (%d)", (double)a.position[2], (int)x.mode, (int)K->debug_mode);
    sa1_character_destroy(g_ctx, id);
}

/* ---- P5, P5b ------------------------------------------------------------------------------------------------------------------------ */
static void test_p5(const kind_row *K)
{
    sa1_character_id id = 0;
    sa1_state_ex x, a;
    evstats s;
    sa1_damage dm;
    sa1_status st;
    uint32_t hurt0;
    int p;
    printf("P5 %s: a hit does nothing; the game's death request works\n", K->name);
    memset(&s, 0, sizeof s);
    if (create(K->kind, 0.0f, 0.0f, 40.0f, 0u, &id) != SA1_OK) {
        CHECK(0, "P5 create: %s", sa1_last_error(g_ctx));
        return;
    }
    run(id, 30, 0, 0, 0u, 0.0f, &s, &a);
    hurt0 = a.hurt_steps;
    memset(&dm, 0, sizeof dm);
    dm.struct_size = sizeof dm;
    st = sa1_character_damage(g_ctx, id, &dm);
    run(id, 150, 0, 0, 0u, 0.0f, &s, &x);
    CHECK(st == SA1_OK && x.mode == 1 && x.rings == a.rings && x.hurt_steps == hurt0 && x.invulnerable_steps == 0u && s.deaths == 0u,
          "P5 a hit with 0 rings: %s, mode %d, rings %d, hurt_steps %u (%u), invulnerable %u, %u deaths, %u hurt events", sa1_status_string(st), (int)x.mode, (int)x.rings,
          (unsigned)x.hurt_steps, (unsigned)hurt0, (unsigned)x.invulnerable_steps, (unsigned)s.deaths, (unsigned)s.hurts);
    sa1_character_add_rings(g_ctx, id, 20);
    run(id, 1, 0, 0, 0u, 0.0f, &s, &a);
    st = sa1_character_damage(g_ctx, id, &dm);
    run(id, 150, 0, 0, 0u, 0.0f, &s, &x);
    CHECK(st == SA1_OK && x.mode == 1 && x.rings == 20 && a.rings == 20 && x.hurt_steps == hurt0 && x.invulnerable_steps == 0u && s.deaths == 0u,
          "P5 a hit with 20 rings: %s, mode %d, rings %d (20), hurt_steps %u, %u deaths", sa1_status_string(st), (int)x.mode, (int)x.rings, (unsigned)x.hurt_steps, (unsigned)s.deaths);
    memset(&s, 0, sizeof s);
    dm.flags = SA1_DAMAGE_KILL;
    st = sa1_character_damage(g_ctx, id, &dm);
    a = x;
    run(id, 1, 0, 0, 0u, 0.0f, &s, &x);
    CHECK(st == SA1_OK && x.mode == K->death_mode && s.deaths == 1u && s.last_death_cause == SA1_DEATH_HOST,
          "P5 SA1_DAMAGE_KILL: %s, mode %d (%d), %u SA1_EVENT_DEATH cause %u (%u)", sa1_status_string(st), (int)x.mode, (int)K->death_mode, (unsigned)s.deaths,
          (unsigned)s.last_death_cause, (unsigned)SA1_DEATH_HOST);
    for (p = 0; p < 150 && s.restarts == 0u; p++) {
        run(id, 1, 0, 0, 0u, 0.0f, &s, &x);
    }
    CHECK(s.restarts == 1u && s.restart_step - s.death_step == 121u, "P5 SA1_EVENT_RESTART %u steps after the death (121), %u of them", (unsigned)(s.restart_step - s.death_step), (unsigned)s.restarts);
    CHECK(x.mode == K->death_mode && s.faults == 0u, "P5 the mode stays %d (%d) to the restart, %u faults", (int)x.mode, (int)K->death_mode, (unsigned)s.faults);
    sa1_character_destroy(g_ctx, id);
    /* P5c: the stick after the death request: Tikal's mode 10 skips her machine and she stays; Eggman's machine goes on, so he still walks */
    if (create(K->kind, 0.0f, 0.0f, 40.0f, 0u, &id) == SA1_OK) {
        memset(&s, 0, sizeof s);
        run(id, 30, 0, 0, 0u, 0.0f, &s, &a);
        memset(&dm, 0, sizeof dm);
        dm.struct_size = sizeof dm;
        dm.flags = SA1_DAMAGE_KILL;
        sa1_character_damage(g_ctx, id, &dm);
        run(id, 40, 0, 1.0f, 0u, 0.0f, &s, &x);
        CHECK(K->kind == SA1_CHAR_TIKAL ? disth(a.position, x.position) < 1.0f && x.mode == 10 : disth(a.position, x.position) > 100.0f && x.mode == 2,
              "P5c the stick for 40 pairs after the death request: moved %.1f HU, mode %d, %u faults", (double)disth(a.position, x.position), (int)x.mode, (unsigned)s.faults);
        sa1_character_destroy(g_ctx, id);
    }
}

static void test_p5b(const kind_row *K)
{
    sa1_character_id id = 0;
    sa1_state_ex x, a;
    evstats s;
    const float floor_z = -600.0f;
    int p;
    uint32_t max_breath = 0u;
    printf("P5b %s: host water until the air timer ends (review R99.6)\n", K->name);
    memset(&s, 0, sizeof s);
    if (scene_water(floor_z) != 0 || create(K->kind, 0.0f, 0.0f, floor_z + 8.0f, 0u, &id) != SA1_OK) {
        CHECK(0, "P5b scene or create: %s", sa1_last_error(g_ctx));
        scene_flat();
        return;
    }
    for (p = 0; p < 1400 && s.deaths == 0u; p++) {
        if (run(id, 1, 0, 0, 0u, 0.0f, &s, &x) != 0) {
            break;
        }
        if (x.breath_timer > 0 && (uint32_t)x.breath_timer > max_breath) {
            max_breath = (uint32_t)x.breath_timer;
        }
    }
    CHECK(s.deaths == 1u && s.last_death_cause == SA1_DEATH_DROWN, "P5b %u SA1_EVENT_DEATH, cause %u (SA1_DEATH_DROWN %u), at step %u, after %d pairs; breath up to %u", (unsigned)s.deaths,
          (unsigned)s.last_death_cause, (unsigned)SA1_DEATH_DROWN, (unsigned)s.death_step, p, (unsigned)max_breath);
    CHECK(x.smode == 39 && x.mode == 1, "P5b the end: smode %d (39), mode %d (1), z %.1f", (int)x.smode, (int)x.mode, (double)x.position[2]);
    g_hash = fnv(g_hash, &s.death_step, sizeof s.death_step);
    a = x;
    if (run(id, 300, 0, 0, 0u, 0.0f, &s, &x) != 0) {
        CHECK(0, "P5b the next 600 steps");
    }
    CHECK(s.deaths == 1u && x.smode == 39 && x.mode == 1 && disth(a.position, x.position) < 1.0f, "P5b the next 600 steps: %u deaths (1), smode %d (39), mode %d (1), moved %.2f HU",
          (unsigned)s.deaths, (int)x.smode, (int)x.mode, (double)disth(a.position, x.position));
    /* MEASURED against review R99.6's "no SA1_EVENT_RESTART": the game's own countdown task asks for it (func_8C07BE6E, timer 0x3FC, code_8C07BE68.c:116-118), for every kind */
    CHECK(s.restarts == 1u && s.restart_id == 2u && s.restart_step - s.death_step == 240u, "P5b the game's own request: %u SA1_EVENT_RESTART (1), id %u (2), %u steps after the death (240)", (unsigned)s.restarts,
          (unsigned)s.restart_id, (unsigned)(s.restart_step - s.death_step));
    a = x;
    run(id, 60, 0, 1.0f, 0u, 0.0f, &s, &x);
    CHECK(disth(a.position, x.position) > 20.0f && s.faults == 0u && s.deaths == 1u && s.restarts == 1u, "P5b the stick still moves them: %.1f HU in 60 pairs, mode %d, smode %d, %u faults, still %u restart",
          (double)disth(a.position, x.position), (int)x.mode, (int)x.smode, (unsigned)s.faults, (unsigned)s.restarts);
    sa1_character_destroy(g_ctx, id);
    scene_flat();
}

/* ---- P6, P11 ------------------------------------------------------------------------------------------------------------------------ */
static void test_p6(const kind_row *K)
{
    sa1_character_id id = 0;
    sa1_status st;
    sa1_emote em;
    sa1_look lk;
    sa1_board_desc bd;
    char want[160];
    const char *why;
    printf("P6, P11 %s: what stays refused, by its words\n", K->name);
    if (create(K->kind, 0.0f, 0.0f, 40.0f, 0u, &id) != SA1_OK) {
        CHECK(0, "P6 create: %s", sa1_last_error(g_ctx));
        return;
    }
    run(id, 2, 0, 0, 0u, 0.0f, NULL, NULL);
    memset(&em, 0, sizeof em);
    em.struct_size = sizeof em;
    em.count = 1u;
    st = sa1_character_emote(g_ctx, id, &em);
    snprintf(want, sizeof want, "%s has no emote list", K->who);
    CHECK(st == SA1_ERR_UNSUPPORTED && strstr(sa1_last_error(g_ctx), want) != NULL, "P6 emote: %s: %s", sa1_status_string(st), sa1_last_error(g_ctx));
    st = sa1_character_face(g_ctx, id, "A", 0u);
    CHECK(st == SA1_ERR_UNSUPPORTED && strstr(sa1_last_error(g_ctx), K->kind == SA1_CHAR_TIKAL ? "Tikal has no face in libsa1 yet (her ladder was never measured)" : "this character has no event face") != NULL,
          "P6 face: %s: %s", sa1_status_string(st), sa1_last_error(g_ctx));
    memset(&lk, 0, sizeof lk);
    lk.struct_size = sizeof lk;
    lk.kind = SA1_LOOK_OFF;
    st = sa1_character_look(g_ctx, id, &lk);
    CHECK(st == SA1_ERR_UNSUPPORTED && strstr(sa1_last_error(g_ctx), K->kind == SA1_CHAR_TIKAL ? "Tikal has no look in libsa1 yet" : "this character has no look in libsa1") != NULL,
          "P6 look: %s: %s", sa1_status_string(st), sa1_last_error(g_ctx));
    memset(&bd, 0, sizeof bd);
    bd.struct_size = sizeof bd;
    bd.on = SA1_BOARD_ON;
    st = sa1_character_board(g_ctx, id, &bd);
    snprintf(want, sizeof want, "only Sonic and Tails ride a board (%s doesn't)", K->who);
    CHECK(st == SA1_ERR_UNSUPPORTED && strstr(sa1_last_error(g_ctx), want) != NULL, "P6 board: %s: %s", sa1_status_string(st), sa1_last_error(g_ctx));
    /* round 0.9.9e: they can follow now (tests/follower_test.c FPL has what they do): no reason, and the bit is there */
    why = sa1_follower_kind_reason(K->kind);
    CHECK(why == NULL && ((sa1_follower_kinds() >> K->kind) & 1u), "P6 sa1_follower_kinds has kind %d, no reason: %s", (int)K->kind, why ? why : "(none)");
    st = sa1_character_order(g_ctx, id, 1);
    CHECK(st == SA1_ERR_UNSUPPORTED, "P11 sa1_character_order(1): %s: %s", sa1_status_string(st), sa1_last_error(g_ctx));
    sa1_character_destroy(g_ctx, id);
}

/* ---- P7 ------------------------------------------------------------------------------------------------------------------------------ */
static void test_p7(const kind_row *K)
{
    sa1_character_id lid = 0, fid = 0;
    sa1_follower_desc fd;
    sa1_status st;
    evstats s;
    sa1_state_ex lx, fx;
    int p;
    float far_d = 0.0f;
    printf("P7 a Sonic follower behind %s: 400 pairs\n", K->name);
    memset(&s, 0, sizeof s);
    if (create(K->kind, -3000.0f, 0.0f, 40.0f, 0u, &lid) != SA1_OK) {
        CHECK(0, "P7 create: %s", sa1_last_error(g_ctx));
        return;
    }
    memset(&fd, 0, sizeof fd);
    fd.struct_size = sizeof fd;
    fd.kind = SA1_CHAR_SONIC;
    fd.leader = lid;
    st = sa1_follower_create(g_ctx, &fd, &fid);
    CHECK(st == SA1_OK && fid != 0, "P7 sa1_follower_create behind kind %d: %s", (int)K->kind, st == SA1_OK ? "ok" : sa1_last_error(g_ctx));
    if (st != SA1_OK) {
        sa1_character_destroy(g_ctx, lid);
        return;
    }
    for (p = 0; p < 400; p++) {
        float d;
        if (tick(lid, 0, (p > 30 && p < 140) ? 1.0f : 0.0f, 0u, 0.0f) != SA1_OK || tick(fid, 0, 0, 0u, 0.0f) != SA1_OK) {
            printf("    tick %d: %s\n", p, sa1_last_error(g_ctx));
            break;
        }
        pump(&s);
        state_of(lid, &lx);
        state_of(fid, &fx);
        d = disth(lx.position, fx.position);
        far_d = d > far_d ? d : far_d;
        g_hash = fnv(g_hash, fx.position, sizeof fx.position);
    }
    CHECK(p == 400 && s.faults == 0u && s.unimpl == 0u, "P7 400 pairs: %u faults, %u unimplemented, the most apart %.0f HU, %u follow events", (unsigned)s.faults, (unsigned)s.unimpl, (double)far_d, (unsigned)s.follows);
    CHECK(disth(lx.position, fx.position) < 400.0f, "P7 it follows: leader (%.0f %.0f %.0f), follower (%.0f %.0f %.0f), %.1f HU apart at the end", (double)lx.position[0], (double)lx.position[1], (double)lx.position[2],
          (double)fx.position[0], (double)fx.position[1], (double)fx.position[2], (double)disth(lx.position, fx.position));
    sa1_character_destroy(g_ctx, fid);
    sa1_character_destroy(g_ctx, lid);
}

/* ---- P8, P10 ------------------------------------------------------------------------------------------------------------------------- */
static void test_p8_p10(const kind_row *K)
{
    sa1_character_info info;
    sa1_character_id id = 0;
    sa1_status st;
    sa1_state_ex x;
    evstats s;
    printf("P8, P10 %s\n", K->name);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    st = sa1_character_info_get(g_ctx, K->kind, &info);
    CHECK(st == SA1_OK && info.loaded && info.num_actions == K->actions && info.num_actions_available == K->actions && info.num_nodes == K->nodes && info.num_motions == K->motions && info.num_welds == K->welds,
          "P8 info: %u nodes (%u), %u / %u actions (%u), %u motions (%u), %u welds (%u), %u triangles, %u archives", (unsigned)info.num_nodes, (unsigned)K->nodes, (unsigned)info.num_actions_available,
          (unsigned)info.num_actions, (unsigned)K->actions, (unsigned)info.num_motions, (unsigned)K->motions, (unsigned)info.num_welds, (unsigned)K->welds, (unsigned)info.num_triangles, (unsigned)info.num_archives);
    memset(&s, 0, sizeof s);
    st = create(K->kind, 123.0f, 456.0f, 789.0f, SA1_CREATE_START_TABLE, &id);
    CHECK(st == SA1_OK, "P10 SA1_CREATE_START_TABLE: %s", sa1_status_string(st));
    if (st == SA1_OK) {
        st = tick(id, 0, 0, 0u, 0.0f);
        pump(&s);
        state_of(id, &x);
        CHECK(st == SA1_OK && s.faults == 0u && x.position[0] == 0.0f && x.position[1] == 0.0f && fabsf(x.position[2]) < 40.0f,
              "P10 the first pair: %s, %u faults, at (%.2f %.2f %.2f): (0, 0, 0), no read of a table at address 0", sa1_status_string(st), (unsigned)s.faults, (double)x.position[0], (double)x.position[1],
              (double)x.position[2]);
        sa1_character_destroy(g_ctx, id);
    }
}

/* ---- main ----------------------------------------------------------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    const char *gdi = argc > 1 ? argv[1] : getenv("SA1_TEST_GDI");
    uint32_t k;
    if (gdi == NULL || !*gdi) {
        printf("usage: playable_test GDI\n");
        return 2;
    }
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &g_ctx) != SA1_OK) {
        return 1;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);
    sa1_set_coords(g_ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_EGGMAN) | (1u << SA1_CHAR_TIKAL);
    lo.flags = SA1_LOAD_DEFAULT;
    if (SA1_FAILED(sa1_load_disc(g_ctx, gdi, &lo))) {
        printf("load: %s\n", sa1_last_error(g_ctx));
        return 1;
    }
    printf("playable_test: libsa1 %s, %d-bit\n", sa1_version_string(), (int)(sizeof(void *) * 8));
    if (scene_flat() != 0) {
        return 1;
    }
    for (k = 0; k < 2u; k++) {
        const kind_row *K = &k_rows[k];
        sa1_character_id id = 0;
        sa1_status st = create(K->kind, 0.0f, 0.0f, 40.0f, 0u, &id);
        if (st == SA1_ERR_UNSUPPORTED) {
            printf("SKIP behaviour: %s is not built yet (%s)\n", K->name, sa1_last_error(g_ctx));
            continue;
        }
        if (st == SA1_OK) {
            sa1_character_destroy(g_ctx, id);
        }
        test_p8_p10(K);
        test_p1_p2(K);
        test_p3_p4(K);
        test_p5(K);
        test_p5b(K);
        test_p6(K);
        test_p7(K);
    }
    printf("DETERMINISM playable %08X\n", (unsigned)g_hash);
    sa1_shutdown(g_ctx);
    printf("playable_test: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
