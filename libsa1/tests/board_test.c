/*
 * board_test.c - round 0.9.5 (095-lib-board): Sonic's and Tails' board by the game's own code (include/libsa1.h "the board",
 * SA1_API_BOARD; sadc docs/research/gsa1_095_plan.md 5.3 and 6.2 task 7 with its Review R.1-R.13, board095_lib.md 4 and 6.1).
 *
 *   py -3.12 tests/build_board.py --run [--target all] [--debug] [-- [--only a,b,...] [--verbose]]   (SA1_TEST_GDI)
 *
 * Ground: the research probe's GRID (board095_probe.c: 64-unit quads over x -256..6144, z -512..512, flat or y = -tan(a) x
 * for x >= 0, SA1 space through the NATIVE coords preset), input "f" (neutral board pairs 1-60, forward from 61, right 0.7
 * at 121-150, jump at 171). Each case says what it measures; the numbers it checks are the research's (board095_lib.md 2.3)
 * or the review's. Every case fails before 0.9.5 (no API) or with 0.9.4f's measured behaviour (the fatal 0C900AE0, the
 * untextured board at the SA1 origin, the compounding jump, the second board, the falling kept board).
 *
 * API NOTES (095-lib-board, for the G-SA1 lanes; libsa1.h "the board" is the contract, these are the measured facts):
 *   CALLS. sa1_character_board is LATCHED: the world carries the request out at the next pair start (pre, sub step 0,
 *   before the executor) - the moment the stage code's own SetInputP(0, 44) + CreateElementalTask(3, 2, 8C0B2EE6 /
 *   8C0BF7CE) take effect when made between pairs; case (l) shows the world state is byte-identical to those raw calls.
 *   A request before the world's first player waits for him. The game's answer shows after that pair: SA1_EVENT_BOARD
 *   EV_ON (arg = the board mode: Sonic 65, Tails 51) or EV_REFUSED (arg = the mode that did not take it). While latched or
 *   posted, sa1_board_state.flags = SA1_BSTATE_PENDING. ON while riding / pending: terrain and flags only (never a second
 *   order 44). OFF while pending: carried out after EV_ON. OFF not riding: SA1_OK, nothing. FREE: board_task 0 at once,
 *   FreeTask at the next pair start (EV_FREED arg 3 then). SA1_STATE_ON_BOARD in sa1_state.flags from the mode range.
 *   REFUSALS (SA1_ERR_UNSUPPORTED, logged at DEBUG; sa1_last_error): "only Sonic and Tails ride a board (Knuckles
 *   doesn't)" (Amy / E-102 / Big), "Super Sonic doesn't ride a board" (kind 8), "not riding on the ground" (PUSH),
 *   "riding" / "no board left behind" (FREE). SA1_ERR_NOT_LOADED "...'s board actions are not loaded (SA1_LOAD_BOARDS)".
 *   A General Gameplay Super Sonic (kind 0) boards (SA1_OK; the body draws from archive 5, SUPERSONIC: owner question O7).
 *   THE KEPT BOARD. After OFF the board is KEPT in the exit step; on flat ground it rests at once and is SETTLED 30 steps
 *   later (measured 17 pairs after the OFF call, Sonic and Tails); on a slope it may slide first (the game's free object).
 *   A kept board 128 units below the last height where it stood is freed (EV_FREED arg 1): measured 114 steps after it last
 *   stood when the player is far away (the game's fall starts slowly); KEEP_FALLEN keeps it (y -2,566 after 400 pairs).
 *   The draws of a board task are left out until it was held once (first board draw = first held pair, board pair 5).
 *   THE POSE. model_position / model_rotation (host space) = the game's display chain (pos, Rz Rx Ry, the -5.22 / -5.190199
 *   drop when not held); a SA1_PREVIEW_BOARD copy (object space) drawn there equals the kept board to 0.0000 units (r).
 *   THE PUSH (STG08 0C90E810, MATCH, run at LIBSA1_BOARD 0x7F010200): starts in the step the game takes the order (so a
 *   refused ON never raises the speed), 102 steps (push_steps 0 at board pair 52), 4.000 u/step = 960 HU/s on flat ground
 *   (3.99987 at board pair 150 after it ended), freed in the step the board mode ends and at EV_REFUSED; PUSH again while
 *   riding on the ground (EV_PUSH arg 1). A camera yaw that follows the board keeps 4.000 for 300 pairs (n).
 *   THE GROUND. TERRAIN_SNOW / SAND (worlds with the GRID window or an empty landtable): 15-degree SOLID 4.403 / 2.897 at
 *   board pair 100 (MAP 0.987). SA1_SURFACE_BOARD_SNOW / _SAND: GRID objects and moving-object groups are keyed by them
 *   (a host counts (COL word, board bits) as one word against SA1_MAX_OBJECT_GROUPS); on foot every state field equals plain
 *   SOLID; riding 0x8 / 0x10 in attr (4.403 / 2.897 at pair 100); the dirt half of a cell shared with board snow stays
 *   dirt (s2); a moving object's words are re-registered at both edges of the ride (s3: on foot after OFF 2.061 u/step).
 *   TEXTURES. Archives after every other one (SA1_LOAD_BOARDS): Sonic MIW_B001 6, SB_BOARD1 7 (5, 6 without
 *   SA1_LOAD_SUPER_FORMS), Tails MTX_BOARD0 4; 64 x 64. Sonic's board is the game's pick (8C0B2EE6: frame counter % 2):
 *   libsa1's frame counter moves by 2 a pair, so (095-fix, owner question O12 "keep the random pick") Sonic's board task
 *   runs that init with the counter read as C + (C / 2) % 2: the pick alternates with the pair the request lands in -
 *   variant 0 MIW_B001 on even pairs, 1 SB_BOARD1 on odd ones (k). sa1_board_state.board_variant says which; the preview
 *   draws either (params[0]). A just-made Sonic board task's exec is LIBSA1_BOARD's pick exec 0x7F010300 until its first
 *   step (then 8C0B2F46).
 *   DRAWS. SA1_DRAW_BOARD on the board task's draws and the preview's; a kept board's models are world-space (no root).
 *   COST (u, the -O0 debug archive): Sonic 0.48 ms, Tails 0.73 ms per riding pair with the render capture.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_world.h"

static int g_fail, g_pass, g_verbose;
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
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1 %d] %s\n", (int)level, msg);
    }
}

/* ---- ground (board095_probe.c) ------------------------------------------------------------------------------------ */
static sa1_collision_tri *g_tris;
static uint32_t g_ntri, g_cap;

static void tris_cap(uint32_t cap)
{
    if (cap > g_cap) {
        free(g_tris);
        g_tris = (sa1_collision_tri *)calloc(cap, sizeof *g_tris);
        g_cap = cap;
    }
}

static void quad2(float x0, float x1, float z0, float z1, float k, uint32_t s0, uint32_t s1)
{
    float y0 = -k * (x0 < 0.0f ? 0.0f : x0), y1 = -k * (x1 < 0.0f ? 0.0f : x1);
    sa1_collision_tri *t = &g_tris[g_ntri++];
    t->v[0][0] = x0; t->v[0][1] = y0; t->v[0][2] = z0;
    t->v[1][0] = x0; t->v[1][1] = y0; t->v[1][2] = z1;
    t->v[2][0] = x1; t->v[2][1] = y1; t->v[2][2] = z0;
    t->surface = s0;
    t = &g_tris[g_ntri++];
    t->v[0][0] = x1; t->v[0][1] = y1; t->v[0][2] = z0;
    t->v[1][0] = x0; t->v[1][1] = y0; t->v[1][2] = z1;
    t->v[2][0] = x1; t->v[2][1] = y1; t->v[2][2] = z1;
    t->surface = s1;
}

/* the probe's ground; `half` != 0: the half z < 0 has `surface`, the half z >= 0 has `other` (review R.1's seam test: the
   quads are 64 units and the halves meet at z = 0 inside the GRID cells; the two triangles of one quad differ when
   `split` is set) */
static sa1_status ground_ex(sa1_context *ctx, float slope_deg, uint32_t surface, uint32_t other, int half, int split)
{
    const float step = 64.0f, len = 6144.0f, w = 512.0f;
    float k = tanf(slope_deg * 3.14159265f / 180.0f), x, z;
    uint32_t cap = (uint32_t)(((len + 256.0f) / step + 4) * (2 * w / step + 2) * 2 + 64);
    if (cap > g_cap) {
        free(g_tris);
        g_tris = (sa1_collision_tri *)calloc(cap, sizeof *g_tris);
        g_cap = cap;
    }
    g_ntri = 0;
    for (x = -256.0f; x < len; x += step) {
        for (z = -w; z < w; z += step) {
            uint32_t s = (half && z >= 0.0f) ? other : surface;
            quad2(x, x + step, z, z + step, k, s, split ? other : s);
        }
    }
    return sa1_collision_set_static(ctx, g_tris, g_ntri);
}

static sa1_status ground(sa1_context *ctx, float slope_deg, uint32_t surface)
{
    return ground_ex(ctx, slope_deg, surface, surface, 0, 0);
}

/* review R.1's seam: z < seam has s0, z >= seam has s1 (quads of 64 units; GRID cells of 256: a seam at 64 lies inside
   the cell z 0..256) */
static sa1_status ground_seam(sa1_context *ctx, float slope_deg, uint32_t s0, uint32_t s1, float seam)
{
    const float step = 64.0f, len = 6144.0f, w = 512.0f;
    float k = tanf(slope_deg * 3.14159265f / 180.0f), x, z;
    tris_cap((uint32_t)(((len + 256.0f) / step + 4) * (2 * w / step + 2) * 2 + 64));
    g_ntri = 0;
    for (x = -256.0f; x < len; x += step) {
        for (z = -w; z < w; z += step) {
            uint32_t sf = z >= seam ? s1 : s0;
            quad2(x, x + step, z, z + step, k, sf, sf);
        }
    }
    return sa1_collision_set_static(ctx, g_tris, g_ntri);
}

/* ---- the context -------------------------------------------------------------------------------------------------- */
static sa1_context *make_ctx(const char *gdi, uint32_t load_flags)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_DEBUG;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) |
                    (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SUPER_SONIC);
    lo.flags = load_flags;
    if (SA1_FAILED(sa1_load_disc(ctx, gdi, &lo))) {
        printf("load: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static float g_start_z;
static sa1_character_id make(sa1_context *ctx, int32_t kind, uint32_t flags, uint32_t super_flags, float x, float y)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | flags;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = g_start_z;
    d.super_flags = super_flags;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("    create %d: %s\n", (int)kind, sa1_last_error(ctx));
        return 0;
    }
    return id;
}

/* ---- events -------------------------------------------------------------------------------------------------------- */
#define EVMAX 8192
static sa1_event g_ev[EVMAX];
static uint32_t g_nev, g_unimpl;

static void poll_all(sa1_context *ctx)
{
    uint32_t got = 0;
    sa1_event tmp[256];
    while (sa1_poll_events(ctx, tmp, 256, &got) == SA1_OK && got) {
        uint32_t i;
        for (i = 0; i < got && g_nev < EVMAX; i++) {
            if (tmp[i].type == SA1_EVENT_BOARD) {      /* only the board's (sounds would fill the list) */
                g_ev[g_nev++] = tmp[i];
            }
            g_unimpl += tmp[i].type == SA1_EVENT_UNIMPLEMENTED;
        }
        got = 0;
    }
}

static uint32_t ev_count(sa1_character_id id, uint32_t type, uint32_t evid, int32_t arg, uint32_t from)
{
    uint32_t i, n = 0;
    for (i = from; i < g_nev; i++) {
        if (g_ev[i].character == id && g_ev[i].type == type && g_ev[i].id == evid && (arg < 0 || g_ev[i].arg == (uint32_t)arg)) {
            n++;
        }
    }
    return n;
}

static const sa1_event *ev_find(sa1_character_id id, uint32_t type, uint32_t evid, uint32_t from)
{
    uint32_t i;
    for (i = from; i < g_nev; i++) {
        if (g_ev[i].character == id && g_ev[i].type == type && g_ev[i].id == evid) {
            return &g_ev[i];
        }
    }
    return NULL;
}

/* ---- ticking ------------------------------------------------------------------------------------------------------- */
static sa1_input g_in;

static void input_f(uint32_t i)
{
    g_in.stick_y = (i < 60) ? 0.0f : 1.0f;
    g_in.stick_x = (i >= 120 && i < 150) ? 0.7f : 0.0f;
    g_in.buttons = (i == 170) ? SA1_BUTTON_JUMP : 0u;
}

static void input_set(float sx, float sy, uint32_t buttons)
{
    g_in.stick_x = sx;
    g_in.stick_y = sy;
    g_in.buttons = buttons;
}

static sa1_status tick(sa1_context *ctx, sa1_character_id id)
{
    sa1_status st;
    g_in.struct_size = sizeof g_in;
    st = sa1_character_tick(ctx, id, &g_in, 1.0f / 30.0f, NULL, NULL);
    poll_all(ctx);
    if (st != SA1_OK) {
        printf("    tick: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
    }
    return st;
}

static sa1_status ticks(sa1_context *ctx, sa1_character_id id, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        sa1_status st = tick(ctx, id);
        if (st != SA1_OK) {
            return st;
        }
    }
    return SA1_OK;
}

static void state(sa1_context *ctx, sa1_character_id id, sa1_state_ex *s)
{
    memset(s, 0, sizeof *s);
    s->struct_size = sizeof *s;
    sa1_character_state_ex(ctx, id, s);
}

static void bstate(sa1_context *ctx, sa1_character_id id, sa1_board_state *b)
{
    memset(b, 0, sizeof *b);
    b->struct_size = sizeof *b;
    sa1_character_board_state(ctx, id, b);
}

static sa1_status board(sa1_context *ctx, sa1_character_id id, int32_t on, uint32_t terrain, uint32_t flags)
{
    sa1_board_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.on = on;
    d.terrain = terrain;
    d.flags = flags;
    return sa1_character_board(ctx, id, &d);
}

static float pspd(sa1_context *ctx, sa1_character_id id)
{
    sa1_state_ex s;
    state(ctx, id, &s);
    return s.player_speed[0];
}

/* ---- the world, read directly ---------------------------------------------------------------------------------------- */
static uint32_t wrd(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (g == 0 || sa1g_world_rd32(w, g, &v) != 0) {
        return 0;
    }
    return v;
}

static float wrdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = wrd(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static int is_board_exec(uint32_t e, int with_init)
{
    e &= 0x1FFFFFFFu;
    /* 0x1F010300: LIBSA1_BOARD's pick exec 0x7F010300, Sonic's board task's first exec (it calls the game's init 8C0B2EE6:
       095-fix, sa1_world_board.c "Sonic's board pick") */
    return e == 0x0C0B2F46u || e == 0x0C0BF804u ||
           (with_init && (e == 0x0C0B2EE6u || e == 0x0C0BF7CEu || e == 0x1F010300u));
}

/* the tasks of the eight lists whose exec is a board task's (init too with with_init); the first in *first */
static uint32_t count_board_tasks(const sa1g_world *w, int with_init, uint32_t *first)
{
    uint32_t n = 0, lv;
    if (first) {
        *first = 0;
    }
    for (lv = 0; lv < 8u; lv++) {
        uint32_t tp = wrd(w, SA1G_ADDR_TASK_LISTS + 4u * lv), guard = 0;
        while (tp != 0 && guard++ < 0x2000u) {
            if (is_board_exec(wrd(w, tp + 0x10u), with_init)) {
                if (first && *first == 0) {
                    *first = tp;
                }
                n++;
            }
            tp = wrd(w, tp);
        }
    }
    return n;
}

/* the board draws of the last pair (SA1_DRAW_BOARD) and their triangles; *untextured: such draws with NO_TEXTURE */
static uint32_t board_draws(sa1_context *ctx, sa1_character_id id, uint32_t *tris, uint32_t *untextured, int32_t *archive,
                            int32_t *texture, float centre[3])
{
    const sa1_render *r = NULL;
    uint32_t q, n = 0, t = 0, u = 0, c = 0;
    double acc[3] = { 0, 0, 0 };
    if (archive) {
        *archive = -2;
    }
    if (texture) {
        *texture = -2;
    }
    if (sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK && r) {
        for (q = 0; q < r->num_draws; q++) {
            const sa1_draw *d = &r->draws[q];
            if (!(d->flags & SA1_DRAW_BOARD) || d->prim != SA1_PRIM_TRIANGLES) {
                continue;
            }
            n++;
            t += d->count;
            u += (d->flags & SA1_DRAW_NO_TEXTURE) != 0;
            if (archive && *archive == -2) {
                *archive = d->archive;
            }
            if (texture && *texture == -2) {
                *texture = d->texture;
            }
            if (centre) {
                uint32_t k, v;
                for (k = d->first; k < d->first + d->count; k++) {
                    for (v = 0; v < 9; v++) {
                        acc[v % 3] += r->positions[9u * k + v];
                    }
                    c += 3;
                }
            }
        }
    }
    if (centre) {
        uint32_t v;
        for (v = 0; v < 3; v++) {
            centre[v] = c ? (float)(acc[v] / c) : 0.0f;
        }
    }
    if (tris) {
        *tris = t;
    }
    if (untextured) {
        *untextured = u;
    }
    return n;
}

static float tails_jmp(const sa1g_world *w)
{
    uint32_t pwp = wrd(w, SA1G_ADDR_PLAYER_PWP);
    return pwp ? wrdf(w, pwp + 0x9Cu + 0x18u) : 0.0f;
}

/* ---- (a) enter: Sonic / Tails on flat SOLID ------------------------------------------------------------------------------ */
static void t_enter(sa1_context *ctx, int32_t kind)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex s;
    sa1_board_state b;
    uint32_t i, e0, tp = 0, n, tris = 0, untex = 0, first_action = 0;
    const char *who = kind == SA1_CHAR_TAILS ? "Tails" : "Sonic";
    printf("\n== (a) enter, %s, flat SOLID\n", who);
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    id = make(ctx, kind, 0, 0, 0.0f, 12.0f);
    w = sa1_character_world(ctx, id);
    if (!id || ticks(ctx, id, 10) != SA1_OK || w == NULL) {
        CHECK(0, "%s world", who);
        return;
    }
    memset(&g_in, 0, sizeof g_in);
    e0 = g_nev;
    CHECK(board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0) == SA1_OK, "sa1_character_board(ON) -> SA1_OK");
    bstate(ctx, id, &b);
    CHECK(b.flags == SA1_BSTATE_PENDING && b.board_task == 0, "before the pair: PENDING only (flags 0x%X)", (unsigned)b.flags);
    tick(ctx, id);
    state(ctx, id, &s);
    CHECK((s.flags & SA1_STATE_ON_BOARD) != 0, "SA1_STATE_ON_BOARD after 1 pair (mode %d)", (int)s.mode);
    CHECK(ev_count(id, SA1_EVENT_BOARD, SA1_BOARD_EV_ON, -1, e0) == 1, "one SA1_BOARD_EV_ON (arg %u)",
          ev_find(id, SA1_EVENT_BOARD, SA1_BOARD_EV_ON, e0) ? (unsigned)ev_find(id, SA1_EVENT_BOARD, SA1_BOARD_EV_ON, e0)->arg : 0u);
    for (i = 0; i < 10; i++) {
        state(ctx, id, &s);
        if ((kind == SA1_CHAR_TAILS && s.action >= 107 && s.action <= 127) ||
            (kind != SA1_CHAR_TAILS && s.action >= 102 && s.action <= 124)) {
            first_action = i + 1;
            break;
        }
        tick(ctx, id);
    }
    CHECK(first_action != 0, "a board action within 10 pairs (action %d after %u more pairs)", (int)s.action,
          (unsigned)(first_action ? first_action - 1 : 0));
    ticks(ctx, id, 2);
    bstate(ctx, id, &b);
    n = count_board_tasks(w, 0, &tp);
    CHECK(n == 1 && tp == b.board_task && (wrd(w, tp + 0x10u) & 0x1FFFFFFFu) ==
          (kind == SA1_CHAR_TAILS ? 0x0C0BF804u : 0x0C0B2F46u),
          "one board task, board_task 0x%08X, exec %08X", (unsigned)b.board_task, (unsigned)wrd(w, tp + 0x10u));
    CHECK(tp && fabsf(wrdf(w, wrd(w, tp + 0x24u) + 0x3Cu) - 0.02f) < 1e-6f, "its motion weight 0.02 (%.4f)",
          tp ? (double)wrdf(w, wrd(w, tp + 0x24u) + 0x3Cu) : -1.0);
    CHECK((b.flags & (SA1_BSTATE_ON | SA1_BSTATE_HELD)) == (SA1_BSTATE_ON | SA1_BSTATE_HELD) && b.board_kind == kind,
          "state ON | HELD, board_kind %d, variant %u", (int)b.board_kind, (unsigned)b.board_variant);
    n = board_draws(ctx, id, &tris, &untex, NULL, NULL, NULL);
    CHECK(n >= 1 && tris == 44, "the board's draws carry SA1_DRAW_BOARD: %u draw(s), %u triangles", (unsigned)n, (unsigned)tris);
    sa1_character_destroy(ctx, id);
}

/* ---- (b) the snow lean: 0C900AE0 void ------------------------------------------------------------------------------- */
static void t_snow(sa1_context *ctx)
{
    static const struct { int32_t kind; float slope; uint32_t surf; const char *name; } C[] = {
        { SA1_CHAR_SONIC, 15.0f, SA1_SURFACE_SNOW, "Sonic 15 SNOW" }, { SA1_CHAR_SONIC, 15.0f, SA1_SURFACE_SLIPPERY, "Sonic 15 SLIPPERY" },
        { SA1_CHAR_SONIC, 30.0f, SA1_SURFACE_SNOW, "Sonic 30 SNOW" }, { SA1_CHAR_SONIC, 30.0f, SA1_SURFACE_SLIPPERY, "Sonic 30 SLIPPERY" },
        { SA1_CHAR_TAILS, 15.0f, SA1_SURFACE_SNOW, "Tails 15 SNOW" }, { SA1_CHAR_TAILS, 15.0f, SA1_SURFACE_SLIPPERY, "Tails 15 SLIPPERY" },
        { SA1_CHAR_TAILS, 30.0f, SA1_SURFACE_SNOW, "Tails 30 SNOW" }, { SA1_CHAR_TAILS, 30.0f, SA1_SURFACE_SLIPPERY, "Tails 30 SLIPPERY" },
    };
    uint32_t c;
    printf("\n== (b) the snow lean (0C900AE0 void): 300 board pairs, input f\n");
    for (c = 0; c < sizeof C / sizeof C[0]; c++) {
        sa1_character_id id;
        uint32_t i, ok = 1;
        float at[6] = { -1, -1, -1, -1, -1, -1 };
        static const uint32_t marks[6] = { 20, 60, 100, 150, 200, 300 };
        ground(ctx, C[c].slope, SA1_SURFACE_SOLID | C[c].surf);
        id = make(ctx, C[c].kind, 0, 0, 0.0f, 12.0f);
        memset(&g_in, 0, sizeof g_in);
        ticks(ctx, id, 10);
        board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
        for (i = 0; i < 300 && ok; i++) {
            uint32_t k;
            input_f(i);
            ok = tick(ctx, id) == SA1_OK;
            for (k = 0; k < 6; k++) {
                if (i + 1 == marks[k]) {
                    at[k] = pspd(ctx, id);
                }
            }
        }
        CHECK(ok, "%s: no error in 300 pairs (0.9.4f: fatal 0C900AE0 at board pair 62-85): %.3f / %.3f / %.3f / %.3f / %.3f / %.3f",
              C[c].name, (double)at[0], (double)at[1], (double)at[2], (double)at[3], (double)at[4], (double)at[5]);
        if (c == 0) {
            CHECK(fabsf(at[2] - 4.403f) <= 0.001f, "Sonic 15-degree SNOW at board pair 100: %.4f (4.403 +- 0.001, board095_lib.md 2.3 B)",
                  (double)at[2]);
        }
        sa1_character_destroy(ctx, id);
    }
}

/* ---- (f) Tails: enter / exit / enter, the jump speed ----------------------------------------------------------------- */
static float tails_ride(sa1_context *ctx, sa1_character_id id, uint32_t flags, float *during)
{
    sa1g_world *w = sa1_character_world(ctx, id);
    memset(&g_in, 0, sizeof g_in);
    board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, flags);
    ticks(ctx, id, 30);
    if (during) {
        *during = tails_jmp(w);
    }
    board(ctx, id, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
    ticks(ctx, id, 30);
    return tails_jmp(w);
}

static void t_tails_jump(sa1_context *ctx)
{
    sa1_character_id id;
    float j0, j1, j2, d1, d2;
    printf("\n== (f) Tails: enter / exit / enter (the jump speed, rule 7)\n");
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    id = make(ctx, SA1_CHAR_TAILS, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    j0 = tails_jmp(sa1_character_world(ctx, id));
    j1 = tails_ride(ctx, id, 0, &d1);
    j2 = tails_ride(ctx, id, 0, &d2);
    CHECK(fabsf(j0 - 1.66f) < 1e-5f && fabsf(j1 - 1.66f) < 1e-5f && fabsf(j2 - 1.66f) < 1e-5f,
          "jmp_y_spd %.4f -> (ride %.4f) %.4f -> (ride %.4f) %.4f: restored after each exit (0.9.4f: 1.992, 2.3904)", (double)j0,
          (double)d1, (double)j1, (double)d2, (double)j2);
    sa1_character_destroy(ctx, id);
    id = make(ctx, SA1_CHAR_TAILS, 0, 0, 0.0f, 12.0f);
    ticks(ctx, id, 10);
    j1 = tails_ride(ctx, id, SA1_BOARD_FAITHFUL_EXIT, NULL);
    j2 = tails_ride(ctx, id, SA1_BOARD_FAITHFUL_EXIT, NULL);
    CHECK(fabsf(j1 - 1.992f) < 1e-4f && fabsf(j2 - 2.3904f) < 1e-4f, "with SA1_BOARD_FAITHFUL_EXIT the game's: %.4f, %.4f "
          "(1.992, 2.3904)", (double)j1, (double)j2);
    sa1_character_destroy(ctx, id);
}

/* ---- (g) the kinds ----------------------------------------------------------------------------------------------------- */
static void t_kinds(sa1_context *ctx, const char *gdi)
{
    static const int32_t K[] = { SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA, SA1_CHAR_BIG, SA1_CHAR_SUPER_SONIC };
    static const char *const N[] = { "Knuckles", "Amy", "E-102", "Big", "Super Sonic (kind 8)" };
    uint32_t i;
    sa1_context *c2;
    printf("\n== (g) the kinds: only Sonic and Tails\n");
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    for (i = 0; i < sizeof K / sizeof K[0]; i++) {
        sa1_character_id id = make(ctx, K[i], 0, 0, 0.0f, 12.0f);
        sa1g_world *w;
        sa1_status st;
        uint32_t before = 0, after = 0, tasks;
        ticks(ctx, id, 10);
        w = sa1_character_world(ctx, id);
        if (w != NULL) {
            uint32_t twp = wrd(w, SA1G_ADDR_PLAYER_TWP);
            before = twp ? wrd(w, twp) : 0;
        }
        st = board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
        CHECK(st == SA1_ERR_UNSUPPORTED, "%s: SA1_ERR_UNSUPPORTED (\"%s\")", N[i], sa1_last_error(ctx));
        if (w != NULL) {
            uint32_t twp = wrd(w, SA1G_ADDR_PLAYER_TWP);
            after = twp ? wrd(w, twp) : 0;
        }
        tick(ctx, id);
        tasks = w != NULL ? count_board_tasks(w, 1, NULL) : 0;
        CHECK(before == after && tasks == 0 && (w == NULL || w->board.pre == NULL), "%s: nothing posted (mode / smode word "
              "0x%08X), no board task, no lane", N[i], (unsigned)after);
        sa1_character_destroy(ctx, id);
    }
    c2 = make_ctx(gdi, SA1_LOAD_SUPER);
    if (c2 != NULL) {
        sa1_character_id id;
        sa1_status st;
        ground(c2, 0.0f, SA1_SURFACE_SOLID);
        id = make(c2, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
        memset(&g_in, 0, sizeof g_in);
        ticks(c2, id, 4);
        st = board(c2, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
        CHECK(st == SA1_ERR_NOT_LOADED, "a world without SA1_LOAD_BOARDS: SA1_ERR_NOT_LOADED (%s: \"%s\")", sa1_status_string(st),
              sa1_last_error(c2));
        sa1_character_destroy(c2, id);
        sa1_shutdown(c2);
    } else {
        CHECK(0, "a context without SA1_LOAD_BOARDS");
    }
}

/* ---- (h) refused: Sonic holding a HOLDABLE host object (hostobj_test's lift) ---------------------------------------------- */
static void t_refused(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    sa1_board_state b;
    uint32_t obj = 0, i, e0, held = 0;
    const sa1_event *e;
    printf("\n== (h) refused: Sonic holding a HOLDABLE host object\n");
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    id = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    w = sa1_character_world(ctx, id);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 20);
    state(ctx, id, &s);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_HOSTOBJ_HOLDABLE;
    d.position[0] = s.position[0] + 10.0f;
    d.position[1] = s.position[1];
    d.position[2] = s.position[2];
    d.armor = -1;
    d.touch = -1;
    d.num_shapes = 1;
    d.shapes[0].form = SA1_SHAPE_CYLINDER;
    d.shapes[0].size[0] = 2.0f;
    d.shapes[0].size[1] = 2.0f;
    d.shapes[0].center[2] = 2.0f;
    if (sa1_hostobj_add(ctx, &d, &obj) != SA1_OK) {
        CHECK(0, "a HOLDABLE host object: %s", sa1_last_error(ctx));
        sa1_character_destroy(ctx, id);
        return;
    }
    ticks(ctx, id, 2);
    for (i = 0; i < 40 && !held; i++) {
        input_set(0.0f, 0.0f, i == 0 ? SA1_BUTTON_ACTION : 0u);
        tick(ctx, id);
        state(ctx, id, &s);
        held = s.held_object == obj;
    }
    input_set(0.0f, 0.0f, 0u);
    ticks(ctx, id, 10);
    state(ctx, id, &s);
    CHECK(s.held_object == obj, "Sonic holds it (mode %d, held_object %u)", (int)s.mode, (unsigned)s.held_object);
    e0 = g_nev;
    CHECK(board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0) == SA1_OK, "ON -> SA1_OK (the game answers after the pair)");
    tick(ctx, id);
    e = ev_find(id, SA1_EVENT_BOARD, SA1_BOARD_EV_REFUSED, e0);
    CHECK(e != NULL, "SA1_BOARD_EV_REFUSED after the pair (arg = mode %d)", e ? (int)e->arg : -1);
    tick(ctx, id);
    bstate(ctx, id, &b);
    state(ctx, id, &s);
    CHECK(count_board_tasks(w, 1, NULL) == 0 && b.board_task == 0 && !(s.flags & SA1_STATE_ON_BOARD) && b.flags == 0,
          "no board task left, SA1_STATE_ON_BOARD off, board state flags 0x%X", (unsigned)b.flags);
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    sa1_character_destroy(ctx, id);
}

/* ---- (i) re-entry: a new board on every get-on (4.4 a), REUSE_KEPT, ON while riding ------------------------------------ */
static void t_reentry(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_board_state b;
    uint32_t r, e0, n, tris = 0, kept_task = 0, tp = 0;
    printf("\n== (i) re-entry: ON / OFF / ON / OFF / ON\n");
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    id = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    w = sa1_character_world(ctx, id);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    for (r = 0; r < 3; r++) {
        e0 = g_nev;
        board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
        tick(ctx, id);              /* review R.9: counted one pair after the call */
        n = count_board_tasks(w, 0, &tp);
        CHECK(n == 1, "ride %u: exactly one task with a board exec one pair after ON (%u; 0.9.4f: two boards)", (unsigned)r + 1,
              (unsigned)n);
        if (r > 0) {
            /* review R.9: the new task may take the freed one's memory (the free list's head) */
            CHECK(ev_count(id, SA1_EVENT_BOARD, SA1_BOARD_EV_FREED, 2, e0) == 1,
                  "ride %u: the kept board 0x%08X freed (SA1_BOARD_EV_FREED arg 2), the new task 0x%08X%s", (unsigned)r + 1,
                  (unsigned)kept_task, (unsigned)tp, tp == kept_task ? " (the freed struct, reused)" : "");
        }
        ticks(ctx, id, 20);
        board_draws(ctx, id, &tris, NULL, NULL, NULL, NULL);
        CHECK(tris == 44, "ride %u: one board drawn, %u triangles (0.9.4f's second ride: 88)", (unsigned)r + 1, (unsigned)tris);
        if (r < 2) {
            board(ctx, id, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
            ticks(ctx, id, 20);
            bstate(ctx, id, &b);
            kept_task = b.board_task;
            CHECK((b.flags & SA1_BSTATE_KEPT) && kept_task != 0, "after OFF: a KEPT board (flags 0x%X)", (unsigned)b.flags);
        }
    }
    /* REUSE_KEPT: the game's re-entry, the kept board snaps back */
    board(ctx, id, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
    input_set(0.0f, 1.0f, 0u);
    ticks(ctx, id, 40);             /* walk away from it */
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    bstate(ctx, id, &b);
    kept_task = b.board_task;
    e0 = g_nev;
    board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, SA1_BOARD_REUSE_KEPT);
    ticks(ctx, id, 5);
    n = count_board_tasks(w, 0, &tp);
    bstate(ctx, id, &b);
    {
        sa1_state_ex s;
        state(ctx, id, &s);
        CHECK(n == 1 && tp == kept_task && ev_count(id, SA1_EVENT_BOARD, SA1_BOARD_EV_FREED, -1, e0) == 0 &&
              (b.flags & SA1_BSTATE_HELD) && fabsf(b.model_position[0] - s.position[0]) < 3.0f,
              "REUSE_KEPT: the same task 0x%08X takes the player again (held, at x %.2f by him at %.2f)", (unsigned)tp,
              (double)b.model_position[0], (double)s.position[0]);
    }
    sa1_character_destroy(ctx, id);
    /* Tails: ON while riding posts nothing (his handler consumes any order) */
    {
        sa1_character_id a = make(ctx, SA1_CHAR_TAILS, 0, 0, 0.0f, 12.0f), c = make(ctx, SA1_CHAR_TAILS, 0, 0, 0.0f, 12.0f);
        sa1_state_ex sa, sc;
        uint32_t i, diff = 0;
        memset(&g_in, 0, sizeof g_in);
        ticks(ctx, a, 10);
        ticks(ctx, c, 10);
        board(ctx, a, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
        board(ctx, c, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
        for (i = 0; i < 60; i++) {
            input_f(i + 40);
            if (i == 20) {
                CHECK(board(ctx, a, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0) == SA1_OK, "Tails riding: ON again -> SA1_OK");
            }
            tick(ctx, a);
            tick(ctx, c);
            state(ctx, a, &sa);
            state(ctx, c, &sc);
            diff += memcmp(sa.sa1_position, sc.sa1_position, sizeof sa.sa1_position) != 0 || sa.mode != sc.mode ||
                    sa.action != sc.action || memcmp(sa.player_speed, sc.player_speed, sizeof sa.player_speed) != 0 ||
                    sa.rand_state != sc.rand_state;
        }
        CHECK(diff == 0, "and his state equals a run without the call on every pair (%u differ)", (unsigned)diff);
        sa1_character_destroy(ctx, a);
        sa1_character_destroy(ctx, c);
    }
}

/* ---- (j) the kept board: the fall rule, KEEP_FALLEN, REMOVE_ON_EXIT ------------------------------------------------------ */
static void t_kept(sa1_context *ctx, uint32_t flags, const char *label)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_board_state b;
    uint32_t i, e0, left_step = 0, task, kept_seen = 0;
    float stand_y = 0.0f, prev_y = 0.0f;
    const sa1_event *ef, *eo;
    printf("\n== (j) the kept board: OFF, walk away 400 pairs (%s)\n", label);
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    id = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    w = sa1_character_world(ctx, id);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, flags);
    ticks(ctx, id, 30);
    e0 = g_nev;
    board(ctx, id, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
    tick(ctx, id);
    bstate(ctx, id, &b);
    task = b.board_task;
    if (flags & SA1_BOARD_REMOVE_ON_EXIT) {
        ef = ev_find(id, SA1_EVENT_BOARD, SA1_BOARD_EV_FREED, e0);
        eo = ev_find(id, SA1_EVENT_BOARD, SA1_BOARD_EV_OFF, e0);
        CHECK(ef != NULL && eo != NULL && ef->arg == 0 && ef->step == eo->step && b.board_task == 0,
              "REMOVE_ON_EXIT: freed in the exit step (EV_FREED arg 0 at step %u, EV_OFF at %u)", ef ? (unsigned)ef->step : 0u,
              eo ? (unsigned)eo->step : 0u);
        sa1_character_destroy(ctx, id);
        return;
    }
    input_set(0.0f, 1.0f, 0u);
    for (i = 0; i < 400; i++) {
        uint32_t twp;
        tick(ctx, id);
        twp = task ? wrd(w, task + 0x20u) : 0;
        if (twp && is_board_exec(wrd(w, task + 0x10u), 0) && (wrd(w, twp + 4u) & 1u)) {
            left_step = (uint32_t)w->steps;     /* the last pair it stood on its land */
            stand_y = wrdf(w, twp + 0x24u);
            kept_seen = 1;
        }
        if (ev_find(id, SA1_EVENT_BOARD, SA1_BOARD_EV_FREED, e0)) {
            break;
        }
        if (twp && is_board_exec(wrd(w, task + 0x10u), 0)) {
            prev_y = wrdf(w, twp + 0x24u);
        }
    }
    ef = ev_find(id, SA1_EVENT_BOARD, SA1_BOARD_EV_FREED, e0);
    if (flags & SA1_BOARD_KEEP_FALLEN) {
        float y = task ? wrdf(w, wrd(w, task + 0x20u) + 0x24u) : 0.0f;
        CHECK(ef == NULL && is_board_exec(wrd(w, task + 0x10u), 0), "KEEP_FALLEN: it lives (the game's; y %.2f after 400 pairs)",
              (double)y);
    } else {
        /* rule 8 (libsa1.h): freed 128 units below the last height where it stood. The plan's "within 30 steps of leaving
           its land" does not hold with that rule: the game's free-object fall starts slowly (measured below; board095_lib.md
           2.3 E: -10.38 at the first printed pair, -189.38 50 pairs later), so the check is that it goes in the pair it
           passes the 128 units */
        CHECK(ef != NULL && ef->arg == 1 && kept_seen && prev_y >= stand_y - 128.0f - 12.0f,
              "freed (EV_FREED arg 1) in the pair it fell 128 units below where it last stood (y %.2f -> %.2f in the pair "
              "before; 0.9.4f: alive at y -2,331)", (double)stand_y, (double)prev_y);
        NOTE("measured: %d steps from the last step on its land (%u) to the free (%u); the player %u pairs away",
             ef && left_step ? (int)(ef->step - left_step) : -1, (unsigned)left_step, ef ? (unsigned)ef->step : 0u,
             (unsigned)i);
    }
    sa1_character_destroy(ctx, id);
}

/* ---- (l) the origin pairs; the latch against the stage code's own calls between pairs ------------------------------------- */
typedef struct raw_call {
    int32_t order;
    uint32_t init;
} raw_call;

static void raw_cb(struct sa1g_world *w, void *user)
{
    raw_call *c = (raw_call *)user;
    void (*setinp)(uint8_t, int32_t) = (void (*)(uint8_t, int32_t))sa1_fn(SA1_GFN_C(0x8C07C65Eu), 0u);
    (void)w;
    setinp(0, c->order);
    if (c->init) {
        void *(*create)(uint16_t, int32_t, sa1_gfn) =
            (void *(*)(uint16_t, int32_t, sa1_gfn))sa1_fn(SA1_GFN_C(SA1G_FN_CREATE_TASK), 0u);
        create(3, 2, SA1_GFN_C(c->init));
    }
}

static void t_origin(sa1_context *ctx, int32_t kind)
{
    sa1_character_id a, c;
    sa1g_world *wa, *wc;
    uint32_t i, r, bad = 0, diff = 0, first_draw = 0, first_held = 0;
    raw_call rc;
    printf("\n== (l) the origin pairs (%s): no board draw before it was held\n", kind == SA1_CHAR_TAILS ? "Tails" : "Sonic");
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    a = make(ctx, kind, 0, 0, 0.0f, 12.0f);
    c = make(ctx, kind, 0, 0, 0.0f, 12.0f);
    wa = sa1_character_world(ctx, a);
    wc = sa1_character_world(ctx, c);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, a, 10);
    ticks(ctx, c, 10);
    board(ctx, a, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
    rc.order = 44;
    rc.init = kind == SA1_CHAR_TAILS ? 0x8C0BF7CEu : 0x8C0B2EE6u;
    sa1g_world_call(wc, raw_cb, &rc);
    for (i = 0; i < 100; i++) {
        sa1_state_ex sa, sc;
        sa1_board_state b;
        uint32_t nd;
        input_f(i);
        tick(ctx, a);
        tick(ctx, c);
        bstate(ctx, a, &b);
        nd = board_draws(ctx, a, NULL, NULL, NULL, NULL, NULL);
        if (b.flags & SA1_BSTATE_HELD && !first_held) {
            first_held = i + 1;
        }
        if (nd && !first_draw) {
            first_draw = i + 1;
        }
        bad += nd != 0 && !(b.flags & (SA1_BSTATE_HELD | SA1_BSTATE_KEPT));
        state(ctx, a, &sa);
        state(ctx, c, &sc);
        sa.struct_size = sc.struct_size;
        diff += memcmp(&sa, &sc, sizeof sa) != 0;
    }
    CHECK(bad == 0 && first_draw >= first_held && first_held != 0, "the first board draw at board pair %u, first held at %u "
          "(0.9.4f: drawn at the SA1 origin for 3 pairs)", (unsigned)first_draw, (unsigned)first_held);
    CHECK(diff == 0, "the world state equals the stage code's own calls made between pairs (sa1_state_ex, 100 pairs: %u differ)",
          (unsigned)diff);
    {
        uint32_t k, nd = 0;
        for (k = 0; k < SA1G_ARENA_SIZE; k += 4) {
            nd += memcmp(wa->ram + k, wc->ram + k, 4) != 0;
        }
        NOTE("arena words that differ between the API's world and the raw calls' world: %u (task memory of the lane's own "
             "bookkeeping lives in segment 0x18, not the arena)", (unsigned)nd);
    }
    /* review R.9: the second and third ride (ON / OFF / FREE / ON / OFF / ON) and REUSE_KEPT */
    for (r = 0; r < 3; r++) {
        board(ctx, a, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
        memset(&g_in, 0, sizeof g_in);
        ticks(ctx, a, 40);
        if (r == 0) {
            board(ctx, a, SA1_BOARD_FREE, SA1_BOARD_TERRAIN_MAP, 0);
            ticks(ctx, a, 2);
        }
        board(ctx, a, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, r == 2 ? SA1_BOARD_REUSE_KEPT : 0u);
        bad = 0;
        for (i = 0; i < 30; i++) {
            sa1_board_state b;
            uint32_t nd;
            tick(ctx, a);
            bstate(ctx, a, &b);
            nd = board_draws(ctx, a, NULL, NULL, NULL, NULL, NULL);
            bad += nd != 0 && !(b.flags & (SA1_BSTATE_HELD | SA1_BSTATE_KEPT));
        }
        CHECK(bad == 0, "ride %u%s: no board draw before it was held (%u pairs drew it unheld)", (unsigned)r + 2,
              r == 2 ? " (REUSE_KEPT)" : r == 0 ? " (after FREE)" : "", (unsigned)bad);
    }
    sa1_character_destroy(ctx, a);
    sa1_character_destroy(ctx, c);
}

/* ---- (m) flying Tails boards ------------------------------------------------------------------------------------------ */
static void t_fly(sa1_context *ctx)
{
    sa1_character_id id;
    sa1_state_ex s;
    uint32_t i, m51 = 0, m48 = 0;
    printf("\n== (m) flying Tails boards\n");
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    id = make(ctx, SA1_CHAR_TAILS, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    input_set(0, 0, SA1_BUTTON_JUMP);
    tick(ctx, id);
    input_set(0, 0, 0);
    ticks(ctx, id, 8);
    input_set(0, 0, SA1_BUTTON_JUMP);
    tick(ctx, id);
    input_set(0, 0, 0);
    ticks(ctx, id, 4);
    state(ctx, id, &s);
    NOTE("Tails before the call: mode %d", (int)s.mode);
    board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
    for (i = 0; i < 40; i++) {
        tick(ctx, id);
        state(ctx, id, &s);
        if (s.mode == 51 && !m51) {
            m51 = i + 1;
        }
        if (s.mode == 48 && m51 && !m48) {
            m48 = i + 1;
        }
    }
    CHECK(m51 == 1 && m48 > m51, "mode 51 in the air at board pair %u, then 48 on landing at %u", (unsigned)m51, (unsigned)m48);
    sa1_character_destroy(ctx, id);
}

/* ---- (o) determinism ------------------------------------------------------------------------------------------------ */
static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

static void t_determinism(sa1_context *ctx)
{
    sa1_character_id a, c;
    uint32_t i, diff = 0, h = 2166136261u;
    sa1_board_state ba, bc;
    printf("\n== (o) determinism: two worlds, the same inputs\n");
    ground(ctx, 15.0f, SA1_SURFACE_SOLID | SA1_SURFACE_SNOW);
    a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    c = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, a, 10);
    ticks(ctx, c, 10);
    board(ctx, a, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
    board(ctx, c, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
    for (i = 0; i < 250; i++) {
        sa1_state_ex sa, sc;
        input_f(i);
        if (i == 200) {
            board(ctx, a, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
            board(ctx, c, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
        }
        tick(ctx, a);
        tick(ctx, c);
        state(ctx, a, &sa);
        state(ctx, c, &sc);
        diff += memcmp(&sa, &sc, sizeof sa) != 0;
        h = fnv(h, sa.sa1_position, sizeof sa.sa1_position);
        h = fnv(h, sa.player_speed, sizeof sa.player_speed);
        h = fnv(h, &sa.rand_state, sizeof sa.rand_state);
    }
    bstate(ctx, a, &ba);
    bstate(ctx, c, &bc);
    h = fnv(h, ba.model_position, sizeof ba.model_position);
    h = fnv(h, ba.model_rotation, sizeof ba.model_rotation);
    CHECK(diff == 0 && memcmp(&ba, &bc, sizeof ba) == 0, "identical on every pair (%u differ), the board states equal",
          (unsigned)diff);
    printf("DETERMINISM board 15-degree snow ride: %08X\n", (unsigned)h);
    sa1_character_destroy(ctx, a);
    sa1_character_destroy(ctx, c);
}

/* ---- (p) General Gameplay Super Sonic --------------------------------------------------------------------------------- */
static void t_super(sa1_context *ctx)
{
    sa1_character_id id;
    sa1_state_ex s;
    sa1_status st;
    const sa1_render *r = NULL;
    uint32_t e0, q;
    printf("\n== (p) General Gameplay Super Sonic (kind 0, SA1_SUPER_GENERAL, Super)\n");
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    id = make(ctx, SA1_CHAR_SONIC, SA1_CREATE_SUPER_CAPABLE | SA1_CREATE_SUPER, SA1_SUPER_GENERAL | SA1_SUPER_NO_DRAIN, 0.0f,
              12.0f);
    if (!id) {
        CHECK(0, "a General Super Sonic world");
        return;
    }
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    sa1_character_order(ctx, id, SA1_ORDER_SUPER);
    ticks(ctx, id, 4);
    state(ctx, id, &s);
    e0 = g_nev;
    st = board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
    ticks(ctx, id, 10);
    CHECK(st == SA1_OK && ev_count(id, SA1_EVENT_BOARD, SA1_BOARD_EV_ON, -1, e0) == 1, "SA1_OK, EV_ON (Super before: %s)",
          (s.flags & SA1_STATE_SUPER) ? "yes" : "no");
    if (sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK && r) {
        for (q = 0; q < r->num_draws; q++) {
            if (r->draws[q].source == SA1_SOURCE_CHARACTER && r->draws[q].prim == SA1_PRIM_TRIANGLES &&
                !(r->draws[q].flags & SA1_DRAW_BOARD)) {
                NOTE("the body's first draw: archive %d (the look is owner question O7)", (int)r->draws[q].archive);
                break;
            }
        }
    }
    sa1_character_destroy(ctx, id);
}

/* ---- (q) FREE, (q2) REMOVE_ON_EXIT at OFF only, (r) the settle steps ------------------------------------------------------ */
static void t_free(sa1_context *ctx, float slope)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_board_state b;
    uint32_t i, e0, task, settled = 0;
    printf("\n== (q) FREE / (r) SETTLED (%s)\n", slope > 0.0f ? "15-degree slope" : "flat");
    ground(ctx, slope, SA1_SURFACE_SOLID);
    id = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    w = sa1_character_world(ctx, id);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    CHECK(board(ctx, id, SA1_BOARD_FREE, SA1_BOARD_TERRAIN_MAP, 0) == SA1_ERR_UNSUPPORTED, "FREE without a kept board: "
          "SA1_ERR_UNSUPPORTED (\"%s\")", sa1_last_error(ctx));
    board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
    ticks(ctx, id, 30);
    CHECK(board(ctx, id, SA1_BOARD_FREE, SA1_BOARD_TERRAIN_MAP, 0) == SA1_ERR_UNSUPPORTED, "FREE while riding: "
          "SA1_ERR_UNSUPPORTED (\"%s\")", sa1_last_error(ctx));
    board(ctx, id, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
    for (i = 0; i < 200; i++) {
        tick(ctx, id);
        bstate(ctx, id, &b);
        if (b.flags & SA1_BSTATE_SETTLED) {
            settled = i + 1;
            break;
        }
    }
    if (slope == 0.0f) {
        CHECK(settled != 0, "SETTLED %u pairs after OFF (still_steps %u; the model at %.2f %.2f %.2f)", (unsigned)settled,
              (unsigned)b.still_steps, (double)b.model_position[0], (double)b.model_position[1], (double)b.model_position[2]);
    } else {
        NOTE("on the slope: SETTLED %s (%u pairs after OFF; still_steps %u; the model at %.2f %.2f %.2f) - a kept board is "
             "the game's free object: it slides down a slope (G-SA1 hands over at SETTLED or after 2 s)",
             settled ? "yes" : "not within 200 pairs", (unsigned)settled, (unsigned)b.still_steps,
             (double)b.model_position[0], (double)b.model_position[1], (double)b.model_position[2]);
        bstate(ctx, id, &b);
        if (!(b.flags & SA1_BSTATE_KEPT)) {
            sa1_character_destroy(ctx, id);
            return;
        }
    }
    task = b.board_task;
    e0 = g_nev;
    CHECK(board(ctx, id, SA1_BOARD_FREE, SA1_BOARD_TERRAIN_MAP, 0) == SA1_OK, "FREE -> SA1_OK");
    bstate(ctx, id, &b);
    CHECK(b.board_task == 0 && !(b.flags & SA1_BSTATE_KEPT), "board_task 0 at once (review R.9)");
    tick(ctx, id);
    CHECK(ev_count(id, SA1_EVENT_BOARD, SA1_BOARD_EV_FREED, 3, e0) == 1 && count_board_tasks(w, 1, NULL) == 0,
          "EV_FREED arg 3, the task destroyed after one pair (task 0x%08X exec %08X)", (unsigned)task,
          (unsigned)wrd(w, task + 0x10u));
    /* (q2) REMOVE_ON_EXIT given at OFF only (review R.5) */
    board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
    ticks(ctx, id, 30);
    e0 = g_nev;
    board(ctx, id, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, SA1_BOARD_REMOVE_ON_EXIT);
    tick(ctx, id);
    {
        const sa1_event *ef = ev_find(id, SA1_EVENT_BOARD, SA1_BOARD_EV_FREED, e0);
        const sa1_event *eo = ev_find(id, SA1_EVENT_BOARD, SA1_BOARD_EV_OFF, e0);
        CHECK(ef && eo && ef->arg == 0 && ef->step == eo->step, "(q2) OFF with REMOVE_ON_EXIT: freed in the exit step, EV_FREED "
              "arg 0 (step %u / %u)", ef ? (unsigned)ef->step : 0u, eo ? (unsigned)eo->step : 0u);
    }
    sa1_character_destroy(ctx, id);
}

/* ---- (u) measured, no pass / fail ----------------------------------------------------------------------------------- */
static double now_ms(void)
{
    return sa1_now_ms();
}

static void t_measure(sa1_context *ctx)
{
    int32_t kinds[2] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS };
    uint32_t k;
    printf("\n== (u) measured\n");
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    for (k = 0; k < 2; k++) {
        sa1_character_id id = make(ctx, kinds[k], 0, 0, 0.0f, 12.0f);
        uint32_t i;
        double t0, t1;
        sa1_status st;
        memset(&g_in, 0, sizeof g_in);
        ticks(ctx, id, 10);
        board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
        ticks(ctx, id, 10);
        t0 = now_ms();
        for (i = 0; i < 200; i++) {
            input_f(i);
            tick(ctx, id);
        }
        t1 = now_ms();
        NOTE("%s riding: %.3f ms per pair (200 pairs, input f, ticks incl. the render capture)",
             kinds[k] == SA1_CHAR_TAILS ? "Tails" : "Sonic", (t1 - t0) / 200.0);
        if (kinds[k] == SA1_CHAR_TAILS) {
            st = sa1_character_order(ctx, id, SA1_ORDER_SUPER);
            NOTE("sa1_character_order(46) to a riding Tails: %s (\"%s\") - the API takes 46 / 47 only for Super-capable Sonic "
                 "and Super Sonic, so his board handler never sees it (review R.2.8)", sa1_status_string(st),
                 sa1_last_error(ctx));
            st = sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
            NOTE("sa1_character_order(47) to a riding Tails: %s", sa1_status_string(st));
        }
        sa1_character_destroy(ctx, id);
    }
}

/* ---- (k) the textures: the board archives (MIW_B001 / SB_BOARD1 / MTX_BOARD0) ---------------------------------------- */
static void t_textures(sa1_context *ctx, int32_t kind)
{
    sa1_character_id id;
    sa1_board_state b;
    uint32_t i, seen = 0, ok_frames = 0, untex_frames = 0, a, variants = 0, pick_bad = 0;
    const char *want0 = kind == SA1_CHAR_TAILS ? "MTX_BOARD0" : NULL;
    printf("\n== (k) the board's textures (%s)\n", kind == SA1_CHAR_TAILS ? "Tails" : "Sonic");
    for (a = 0; a < 12; a++) {
        const char *nm = NULL;
        uint32_t nt = 0;
        if (sa1_texture_archive(ctx, kind, a, &nm, &nt) == SA1_OK && nm != NULL) {
            if (strcmp(nm, "MIW_B001") == 0 || strcmp(nm, "SB_BOARD1") == 0 || strcmp(nm, "MTX_BOARD0") == 0) {
                sa1_texture_info ti;
                memset(&ti, 0, sizeof ti);
                ti.struct_size = sizeof ti;
                CHECK(nt == 1 && sa1_texture_get(ctx, kind, a, 0, &ti) == SA1_OK && ti.width == 64 && ti.height == 64 &&
                      ti.rgba != NULL, "archive %u: %s, %u texture, %u x %u", (unsigned)a, nm, (unsigned)nt,
                      (unsigned)ti.width, (unsigned)ti.height);
            }
        }
    }
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    for (i = 0; i < 4; i++) {
        uint32_t k, n, tris = 0, untex = 0, fc = 0, want_v;
        int32_t arch = -2, tex = -2;
        const char *nm = NULL;
        sa1g_world *w;
        id = make(ctx, kind, 0, 0, 0.0f, 12.0f);
        memset(&g_in, 0, sizeof g_in);
        ticks(ctx, id, 10 + i);         /* the frame counter decides Sonic's pick (8C0B2EE6: func_8C08A71E() % 2) */
        /* 095-fix (owner question O12, "keep the random pick"): the pair that carries the request out sees this counter C;
           Sonic's init reads it as C + (C / 2) % 2 (sa1_world_board.c "Sonic's board pick"), so the game's pick alternates
           with the pair: SB_BOARD1 when that is odd */
        w = sa1_character_world(ctx, id);
        fc = w != NULL ? wrd(w, 0x8C752BE8u) : 0u;
        want_v = kind == SA1_CHAR_TAILS ? 0u : (fc + ((fc >> 1) & 1u)) & 1u;
        board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
        for (k = 0; k < 40; k++) {
            input_set(0.0f, k >= 20 ? 1.0f : 0.0f, 0u);   /* standing (njDrawObject) and running (the motion draw) */
            tick(ctx, id);
            n = board_draws(ctx, id, &tris, &untex, &arch, &tex, NULL);
            if (n) {
                bstate(ctx, id, &b);
                sa1_texture_archive(ctx, kind, (uint32_t)(arch >= 0 ? arch : 0), &nm, NULL);
                seen++;
                if (arch >= 0 && tex == 0 && untex == 0 && nm != NULL &&
                    strcmp(nm, kind == SA1_CHAR_TAILS ? "MTX_BOARD0" : (b.board_variant ? "SB_BOARD1" : "MIW_B001")) == 0) {
                    ok_frames++;
                } else {
                    untex_frames++;
                }
            }
        }
        bstate(ctx, id, &b);
        NOTE("ride %u: frame counter %u at the get-on pair, board_variant %u (expected %u), archive %d (%s) texture %d",
             (unsigned)i, (unsigned)fc, (unsigned)b.board_variant, (unsigned)want_v, (int)arch, nm ? nm : "-", (int)tex);
        variants |= 1u << (b.board_variant & 1u);
        pick_bad += b.board_variant != want_v || b.board_kind != kind;
        sa1_character_destroy(ctx, id);
        if (kind == SA1_CHAR_TAILS) {
            break;
        }
    }
    (void)want0;
    CHECK(seen > 0 && untex_frames == 0, "every board draw textured from its archive as board_variant says (%u pairs ok, %u not; "
          "0.9.4f: archive -1, SA1_DRAW_NO_TEXTURE)", (unsigned)ok_frames, (unsigned)untex_frames);
    if (kind == SA1_CHAR_SONIC) {
        /* 095-fix: the 0.9.5 verifier's fail 1 (rides 0-3 were all variant 0 / MIW_B001) */
        CHECK(variants == 3u && pick_bad == 0, "Sonic's board, the game's random pick: rides 0-3 (10-13 pairs before the "
              "get-on) give both boards (%s), each as the pair's parity says (%u off; before the fix: MIW_B001 on every ride)",
              variants == 3u ? "MIW_B001 and SB_BOARD1" : variants == 1u ? "MIW_B001 only" : "SB_BOARD1 only",
              (unsigned)pick_bad);
    } else {
        CHECK(variants == 1u && pick_bad == 0, "Tails' one board: variant 0 (MTX_BOARD0)");
    }
}

/* ---- (c) terrain overrides; (s) / (s2) / (s3) the board-time surface kinds ---------------------------------------------- */
static uint32_t colseg_hash(const sa1g_world *w)
{
    if (w == NULL || w->shared == NULL || w->shared->col == NULL) {
        return 0;
    }
    return fnv(2166136261u, w->shared->col, w->shared->col_used);
}

/* rides `pairs` board pairs (input f) and returns pspd at board pair 100; the attr bits seen while riding on the ground */
static float ride_measure(sa1_context *ctx, sa1_character_id id, uint32_t terrain, uint32_t pairs, uint32_t *attr_or,
                          uint32_t *attr_and)
{
    uint32_t i;
    float at100 = -1.0f;
    *attr_or = 0;
    *attr_and = 0xFFFFFFFFu;
    board(ctx, id, SA1_BOARD_ON, terrain, 0);
    memset(&g_in, 0, sizeof g_in);
    for (i = 0; i < pairs; i++) {
        sa1_state_ex s;
        input_f(i);
        if (tick(ctx, id) != SA1_OK) {
            break;
        }
        state(ctx, id, &s);
        if (i >= 2 && (s.flags & SA1_STATE_ON_GROUND)) {
            *attr_or |= s.attr;
            *attr_and &= s.attr;
        }
        if (i + 1 == 100) {
            at100 = s.player_speed[0];
        }
    }
    return at100;
}

static void t_terrain(sa1_context *ctx)
{
    sa1_character_id a, b2, c;
    uint32_t aor, aand, i, diff = 0, after_or = 0;
    float v;
    (void)aor;
    printf("\n== (c) the terrain overrides (SA1_BOARD_TERRAIN_SNOW / SAND)\n");
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    b2 = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    c = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, a, 10);
    ticks(ctx, b2, 10);
    ticks(ctx, c, 10);
    board(ctx, a, SA1_BOARD_ON, SA1_BOARD_TERRAIN_SNOW, 0);
    for (i = 0; i < 60; i++) {
        sa1_state_ex sa, sb, sc;
        tick(ctx, a);
        tick(ctx, b2);
        tick(ctx, c);
        state(ctx, a, &sa);
        state(ctx, b2, &sb);
        state(ctx, c, &sc);
        if (i >= 2) {
            after_or |= (sa.attr & 0x8u) ? 0u : 1u;
        }
        sb.struct_size = sc.struct_size;
        diff += memcmp(&sb, &sc, sizeof sb) != 0 || (sb.attr & 0x8u);
    }
    CHECK(after_or == 0, "flat SOLID + TERRAIN_SNOW: state_ex.attr has 0x8 on every pair of the ride");
    CHECK(diff == 0, "a second world of the context on foot: its state equals a third world's (and no 0x8): %u differ",
          (unsigned)diff);
    board(ctx, a, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
    ticks(ctx, a, 10);
    {
        sa1_state_ex s;
        state(ctx, a, &s);
        CHECK(!(s.attr & 0x8u), "after OFF: attr 0x%X (no 0x8)", (unsigned)s.attr);
    }
    sa1_character_destroy(ctx, a);
    sa1_character_destroy(ctx, b2);
    sa1_character_destroy(ctx, c);
    ground(ctx, 15.0f, SA1_SURFACE_SOLID);
    a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, a, 10);
    v = ride_measure(ctx, a, SA1_BOARD_TERRAIN_SNOW, 100, &aor, &aand);
    CHECK(v >= 4.40f, "15-degree SOLID + TERRAIN_SNOW: %.3f at board pair 100 (>= 4.40; MAP: 0.987)", (double)v);
    sa1_character_destroy(ctx, a);
    a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, a, 10);
    v = ride_measure(ctx, a, SA1_BOARD_TERRAIN_SAND, 100, &aor, &aand);
    CHECK(fabsf(v - 2.897f) < 0.01f && (aand & 0x10u), "15-degree SOLID + TERRAIN_SAND: %.3f at board pair 100 (table A SAND "
          "2.897), attr 0x10", (double)v);
    sa1_character_destroy(ctx, a);
    a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, a, 10);
    v = ride_measure(ctx, a, SA1_BOARD_TERRAIN_MAP, 100, &aor, &aand);
    CHECK(fabsf(v - 0.987f) < 0.002f, "15-degree SOLID + TERRAIN_MAP: %.3f (0.987)", (double)v);
    sa1_character_destroy(ctx, a);
}

static void t_kinds_surface(sa1_context *ctx, uint32_t kind_bit, uint32_t col_bit, float want, const char *name)
{
    sa1_character_id a, b2;
    sa1g_world *w;
    static sa1_state_ex ref[300];
    uint32_t i, diff = 0, aor, aand, h0, h1, foot_diff = 0;
    float v;
    printf("\n== (s) the board-time surface kind %s (15-degree GRID slope)\n", name);
    ground(ctx, 15.0f, SA1_SURFACE_SOLID);
    a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    for (i = 0; i < 300; i++) {
        input_f(i);
        tick(ctx, a);
        state(ctx, a, &ref[i]);
    }
    sa1_character_destroy(ctx, a);
    ground(ctx, 15.0f, SA1_SURFACE_SOLID | kind_bit);
    a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    for (i = 0; i < 300; i++) {
        sa1_state_ex s;
        input_f(i);
        tick(ctx, a);
        state(ctx, a, &s);
        s.struct_size = ref[i].struct_size;
        diff += memcmp(&s, &ref[i], sizeof s) != 0;
    }
    CHECK(diff == 0, "on foot on SOLID | %s: Sonic's whole sa1_state_ex equals a plain SOLID run on every one of 300 pairs (%u "
          "differ)", name, (unsigned)diff);
    sa1_character_destroy(ctx, a);
    a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    b2 = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    w = sa1_character_world(ctx, a);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, a, 10);
    ticks(ctx, b2, 10);
    h0 = colseg_hash(w);
    board(ctx, a, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
    aor = 0;
    aand = 0xFFFFFFFFu;
    v = -1.0f;
    for (i = 0; i < 100; i++) {
        sa1_state_ex s, sb;
        input_f(i);
        tick(ctx, a);
        tick(ctx, b2);
        state(ctx, a, &s);
        state(ctx, b2, &sb);
        if (i >= 2 && (s.flags & SA1_STATE_ON_GROUND)) {
            aor |= s.attr;
            aand &= s.attr;
        }
        foot_diff += (sb.attr & col_bit) != 0;
        if (i + 1 == 100) {
            v = s.player_speed[0];
        }
    }
    h1 = colseg_hash(w);
    CHECK((aand & col_bit) && fabsf(v - want) < (want > 4.0f ? 0.03f : 0.01f) && (want < 4.0f || v >= 4.40f),
          "riding: attr & 0x%X on every grounded pair, %.3f at board pair 100 (%s %.3f)", (unsigned)col_bit, (double)v,
          want > 4.0f ? ">= 4.40, Ice Cap snow" : "table A SAND", (double)want);
    CHECK(foot_diff == 0, "a second world of the context on foot: no 0x%X (%u pairs had it)", (unsigned)col_bit,
          (unsigned)foot_diff);
    CHECK(h0 == h1 && h0 != 0, "the static collision segment byte-identical before / after the ride (%08X)", (unsigned)h0);
    sa1_character_destroy(ctx, a);
    sa1_character_destroy(ctx, b2);
}

static void t_seam(sa1_context *ctx, float slope)
{
    const uint32_t dirt = SA1_SURFACE_SOLID | SA1_SURFACE_DIG, snow = SA1_SURFACE_SOLID | SA1_SURFACE_DIG | SA1_SURFACE_BOARD_SNOW;
    sa1_character_id a;
    uint32_t aor, aand;
    float v;
    printf("\n== (s2) half dirt, half board snow, the seam inside the GRID cells (%s)\n", slope > 0 ? "15-degree slope" : "flat");
    ground_seam(ctx, slope, dirt, snow, 64.0f);
    g_start_z = 0.0f;
    a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, a, 10);
    v = ride_measure(ctx, a, SA1_BOARD_TERRAIN_MAP, 100, &aor, &aand);
    CHECK(!(aor & 0x8u), "riding on the dirt half (z 0, cell z 0..256 shared with snow from z 64): no 0x8 in attr (seen "
          "0x%X), %.3f at pair 100", (unsigned)aor, (double)v);
    sa1_character_destroy(ctx, a);
    g_start_z = 160.0f;
    a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, a, 10);
    v = ride_measure(ctx, a, SA1_BOARD_TERRAIN_MAP, 100, &aor, &aand);
    CHECK((aand & 0x8u) && (slope == 0.0f || v >= 4.40f), "riding on the snow half (z 160): attr 0x8 on every grounded pair "
          "(and 0x%X, or 0x%X), %.3f at pair 100%s", (unsigned)aand, (unsigned)aor, (double)v, slope > 0 ? " (>= 4.40)" : "");
    sa1_character_destroy(ctx, a);
    g_start_z = 0.0f;
    /* on foot across the seam: how far Sonic's state is from an all-dirt run (report, no pass / fail) */
    {
        static sa1_state_ex ref[200];
        uint32_t i, nd = 0;
        float maxd = 0.0f;
        ground(ctx, slope, dirt);
        g_start_z = 40.0f;
        a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
        for (i = 0; i < 200; i++) {
            input_set(i >= 30 ? 0.5f : 0.0f, 1.0f, 0u);
            tick(ctx, a);
            state(ctx, a, &ref[i]);
        }
        sa1_character_destroy(ctx, a);
        ground_seam(ctx, slope, dirt, snow, 64.0f);
        a = make(ctx, SA1_CHAR_SONIC, 0, 0, 0.0f, 12.0f);
        for (i = 0; i < 200; i++) {
            sa1_state_ex s;
            uint32_t k;
            input_set(i >= 30 ? 0.5f : 0.0f, 1.0f, 0u);
            tick(ctx, a);
            state(ctx, a, &s);
            for (k = 0; k < 3; k++) {
                float d = fabsf(s.sa1_position[k] - ref[i].sa1_position[k]);
                maxd = d > maxd ? d : maxd;
            }
            nd += memcmp(s.sa1_position, ref[i].sa1_position, sizeof s.sa1_position) != 0;
        }
        NOTE("on foot across the seam (z 40, stick turning right from pair 30): %u of 200 pairs differ in position from an "
             "all-dirt run, at most %.4f units (the object split moves the game's push-back averaging only where objects "
             "meet)", (unsigned)nd, (double)maxd);
        sa1_character_destroy(ctx, a);
        g_start_z = 0.0f;
    }
}

static void t_moving_kind(sa1_context *ctx, uint32_t surface, uint32_t terrain, const char *label)
{
    sa1_collision_tri t[2];
    sa1_character_id a;
    uint32_t obj = 0, i, aor = 0, aand = 0xFFFFFFFFu;
    float pos[3] = { 0.0f, 0.0f, 0.0f }, foot;
    sa1_state_ex s;
    printf("\n== (s3) a resting moving object (%s), registered before ON\n", label);
    sa1_collision_set_static(ctx, NULL, 0);
    memset(t, 0, sizeof t);
    t[0].v[0][0] = -690.0f; t[0].v[0][2] = -690.0f;
    t[0].v[1][0] = -690.0f; t[0].v[1][2] = 690.0f;
    t[0].v[2][0] = 690.0f;  t[0].v[2][2] = -690.0f;
    t[1].v[0][0] = 690.0f;  t[1].v[0][2] = -690.0f;
    t[1].v[1][0] = -690.0f; t[1].v[1][2] = 690.0f;
    t[1].v[2][0] = 690.0f;  t[1].v[2][2] = 690.0f;
    t[0].surface = t[1].surface = surface;
    if (sa1_collision_object_add(ctx, t, 2, pos, NULL, &obj) != SA1_OK) {
        CHECK(0, "the moving object: %s", sa1_last_error(ctx));
        return;
    }
    a = make(ctx, SA1_CHAR_SONIC, 0, 0, -600.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, a, 10);
    board(ctx, a, SA1_BOARD_ON, terrain, 0);
    for (i = 0; i < 40; i++) {
        input_set(0.0f, i >= 10 ? 1.0f : 0.0f, 0u);
        tick(ctx, a);
        state(ctx, a, &s);
        if (i >= 2 && (s.flags & SA1_STATE_ON_GROUND)) {
            aor |= s.attr;
            aand &= s.attr;
        }
    }
    CHECK((aand & 0x8u) != 0, "riding on it: attr & 0x8 on every grounded pair (and 0x%X, or 0x%X)", (unsigned)aand,
          (unsigned)aor);
    board(ctx, a, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, a, 30);
    /* on foot again: from standing, 45 pairs forward (board095_gsa1.md F10: 0.351 u/step on 0x8, 2.061 on plain land) */
    for (i = 0; i < 45; i++) {
        input_set(0.0f, 1.0f, 0u);
        tick(ctx, a);
    }
    state(ctx, a, &s);
    foot = s.player_speed[0];
    CHECK(!(s.attr & 0x8u) && foot > 1.5f, "on foot after OFF: attr 0x%X (no 0x8), %.3f u/step after 45 pairs (0x8 would give "
          "0.351, plain 2.061)", (unsigned)s.attr, (double)foot);
    sa1_character_destroy(ctx, a);
    sa1_collision_object_remove(ctx, obj);
}

/* ---- (t) the BOARD preview; (r) the pose: the preview at the kept board's pose is the kept board -------------------------- */
static sa1_status preview_add(sa1_context *ctx, int32_t kind, float variant, uint32_t *id)
{
    sa1_preview_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.type = SA1_PREVIEW_BOARD;
    d.params[0] = variant;
    d.state = SA1_SET_STATE_PRESENT;
    return sa1_preview_add(ctx, &d, id);
}

/* the copy's board draws (SA1_DRAW_BOARD) after its pairs: triangles, the first draw's archive */
static uint32_t preview_board(sa1_context *ctx, uint32_t id, int32_t *archive, const sa1_render **out)
{
    const sa1_render *r = NULL;
    uint32_t q, t = 0;
    *archive = -2;
    if (sa1_preview_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) {
        return 0;
    }
    for (q = 0; q < r->num_draws; q++) {
        if ((r->draws[q].flags & SA1_DRAW_BOARD) && r->draws[q].prim == SA1_PRIM_TRIANGLES) {
            if (*archive == -2) {
                *archive = r->draws[q].archive;
            }
            t += r->draws[q].count;
        }
    }
    if (out) {
        *out = r;
    }
    return t;
}

static void t_preview(sa1_context *ctx)
{
    static const struct { int32_t kind; float variant; const char *name; } C[] = {
        { SA1_CHAR_SONIC, 0.0f, "MIW_B001" }, { SA1_CHAR_SONIC, 1.0f, "SB_BOARD1" }, { SA1_CHAR_TAILS, 0.0f, "MTX_BOARD0" },
    };
    sa1_preview_type pt;
    uint32_t c, id, k;
    sa1_status st;
    printf("\n== (t) the BOARD preview\n");
    memset(&pt, 0, sizeof pt);
    pt.struct_size = sizeof pt;
    st = sa1_preview_type_get(ctx, SA1_PREVIEW_BOARD, &pt);
    CHECK(st == SA1_OK && pt.available == 1 && pt.rotation == SA1_PREVIEW_ROT_OUTER && pt.settle_steps == 0,
          "sa1_preview_type_get(SA1_PREVIEW_BOARD): available %u, rotation %u (ROT_OUTER), settle %u", (unsigned)pt.available,
          (unsigned)pt.rotation, (unsigned)pt.settle_steps);
    for (c = 0; c < 3; c++) {
        int32_t arch = -2;
        const char *nm = NULL;
        uint32_t tris;
        st = preview_add(ctx, C[c].kind, C[c].variant, &id);
        if (st != SA1_OK) {
            CHECK(0, "%s copy: %s (%s)", C[c].name, sa1_status_string(st), sa1_last_error(ctx));
            continue;
        }
        for (k = 0; k < 3; k++) {
            sa1_preview_tick(ctx, 1.0f / 30.0f);
        }
        tris = preview_board(ctx, id, &arch, NULL);
        if (arch >= 0) {
            sa1_texture_archive(ctx, C[c].kind, (uint32_t)arch, &nm, NULL);
        }
        CHECK(tris == 44 && nm != NULL && strcmp(nm, C[c].name) == 0, "%s (kind %d, params[0] %.0f): one copy, %u triangles, "
              "archive %d %s, SA1_DRAW_BOARD", C[c].name, (int)C[c].kind, (double)C[c].variant, (unsigned)tris, (int)arch,
              nm ? nm : "-");
        sa1_preview_remove(ctx, id);
    }
    {
        /* (u) measured: the cost of a BOARD copy per showcase pair (16 copies of Sonic's board, 60 pairs, the renders) */
        uint32_t ids[16], n = 0, q;
        double t0, t1, e0, e1;
        for (q = 0; q < 16; q++) {
            if (preview_add(ctx, SA1_CHAR_SONIC, 0.0f, &ids[n]) == SA1_OK) {
                n++;
            }
        }
        sa1_preview_tick(ctx, 1.0f / 30.0f);
        t0 = sa1_now_ms();
        for (q = 0; q < 60; q++) {
            sa1_preview_tick(ctx, 1.0f / 30.0f);
        }
        t1 = sa1_now_ms();
        for (q = 0; q < n; q++) {
            sa1_preview_remove(ctx, ids[q]);
        }
        for (q = 0; q < 70; q++) {
            sa1_preview_tick(ctx, 1.0f / 30.0f);   /* the empty showcase's own pairs (it lives 60 idle pairs) */
        }
        id = 0;
        preview_add(ctx, SA1_CHAR_SONIC, 0.0f, &id);
        sa1_preview_tick(ctx, 1.0f / 30.0f);
        e0 = sa1_now_ms();
        for (q = 0; q < 60; q++) {
            sa1_preview_tick(ctx, 1.0f / 30.0f);
        }
        e1 = sa1_now_ms();
        sa1_preview_remove(ctx, id);
        NOTE("(u) the showcase pair with %u BOARD copies %.3f ms, with 1 copy %.3f ms: about %.4f ms per copy and pair "
             "(the capture and the split included)", (unsigned)n, (t1 - t0) / 60.0, (e1 - e0) / 60.0,
             n > 1 ? ((t1 - t0) - (e1 - e0)) / 60.0 / (double)(n - 1) : 0.0);
    }
    st = preview_add(ctx, SA1_CHAR_KNUCKLES, 0.0f, &id);
    CHECK(st == SA1_ERR_UNSUPPORTED, "kind 3 -> SA1_ERR_UNSUPPORTED (%s)", sa1_status_string(st));
    st = preview_add(ctx, SA1_CHAR_SUPER_SONIC, 0.0f, &id);
    CHECK(st == SA1_ERR_UNSUPPORTED, "kind 8 -> SA1_ERR_UNSUPPORTED (%s)", sa1_status_string(st));
    {
        sa1_set_record rec;
        uint32_t sid = 0;
        memset(&rec, 0, sizeof rec);
        rec.struct_size = sizeof rec;
        rec.type = SA1_PREVIEW_BOARD;
        st = sa1_set_add(ctx, &rec, &sid);
        CHECK(SA1_FAILED(st), "sa1_set_add with type 0x100 refused: %s (\"%s\"; sa1_set.c's unknown type)",
              sa1_status_string(st), sa1_last_error(ctx));
    }
    sa1_preview_clear(ctx);
}

static void t_pose(sa1_context *ctx, int32_t kind, float slope)
{
    sa1_character_id ch;
    sa1_board_state b;
    const sa1_render *r = NULL, *pr = NULL;
    uint32_t i, q, pid = 0, settled = 0, n_kept = 0, first_kept = 0, n_prev = 0, first_prev = 0, cmp = 0;
    int32_t arch;
    float worst = 0.0f;
    printf("\n== (r) the kept board's pose (%s, %s)\n", kind == SA1_CHAR_TAILS ? "Tails" : "Sonic",
           slope > 0 ? "15-degree slope" : "flat");
    ground(ctx, slope, SA1_SURFACE_SOLID);
    ch = make(ctx, kind, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, ch, 10);
    board(ctx, ch, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
    for (i = 0; i < 40; i++) {
        input_set(0.0f, i >= 10 ? 1.0f : 0.0f, 0u);
        tick(ctx, ch);
    }
    memset(&g_in, 0, sizeof g_in);
    board(ctx, ch, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
    for (i = 0; i < 120; i++) {
        tick(ctx, ch);
        bstate(ctx, ch, &b);
        if ((b.flags & SA1_BSTATE_SETTLED) && !settled) {
            settled = i + 1;
            break;
        }
    }
    bstate(ctx, ch, &b);
    if (slope == 0.0f) {
        CHECK(settled != 0, "SETTLED %u pairs (%u steps) after OFF", (unsigned)settled, (unsigned)(2u * settled));
    } else {
        NOTE("SETTLED %s (%u pairs after OFF)", settled ? "yes" : "not within 120 pairs (it slides)", (unsigned)settled);
    }
    if (sa1_character_render(ctx, ch, 1.0f, &r) == SA1_OK && r) {
        for (q = 0; q < r->num_draws; q++) {
            if ((r->draws[q].flags & SA1_DRAW_BOARD) && r->draws[q].prim == SA1_PRIM_TRIANGLES) {
                if (!n_kept) {
                    first_kept = r->draws[q].first;
                }
                n_kept += r->draws[q].count;
            }
        }
    }
    if (preview_add(ctx, kind, (float)b.board_variant, &pid) == SA1_OK) {
        for (i = 0; i < 2; i++) {
            sa1_preview_tick(ctx, 1.0f / 30.0f);
        }
        n_prev = preview_board(ctx, pid, &arch, &pr);
        if (pr) {
            for (q = 0; q < pr->num_draws; q++) {
                if ((pr->draws[q].flags & SA1_DRAW_BOARD) && pr->draws[q].prim == SA1_PRIM_TRIANGLES) {
                    first_prev = pr->draws[q].first;
                    break;
                }
            }
        }
    }
    if (n_kept == 44 && n_prev == 44 && r && pr) {
        uint32_t t, v;
        for (t = 0; t < 44; t++) {
            for (v = 0; v < 3; v++) {
                const float *pv = &pr->positions[9u * (first_prev + t) + 3u * v];
                const float *kv = &r->positions[9u * (first_kept + t) + 3u * v];
                float w3[3], d;
                uint32_t a;
                for (a = 0; a < 3; a++) {
                    w3[a] = b.model_position[a] + b.model_rotation[3 * a + 0] * pv[0] + b.model_rotation[3 * a + 1] * pv[1] +
                            b.model_rotation[3 * a + 2] * pv[2];
                }
                d = sqrtf((w3[0] - kv[0]) * (w3[0] - kv[0]) + (w3[1] - kv[1]) * (w3[1] - kv[1]) +
                          (w3[2] - kv[2]) * (w3[2] - kv[2]));
                worst = d > worst ? d : worst;
                cmp++;
            }
        }
    }
    CHECK(cmp == 132 && worst * 4.0f <= 0.05f, "the BOARD preview's triangles at model_position / model_rotation equal the kept "
          "board's captured triangles: %u vertices, worst %.4f SA1 units (x 4 HU <= 0.05 HU; kept %u, preview %u triangles; pose (%.2f %.2f %.2f))",
          (unsigned)cmp, (double)worst, (unsigned)n_kept, (unsigned)n_prev, (double)b.model_position[0],
          (double)b.model_position[1], (double)b.model_position[2]);
    if (pid) {
        sa1_preview_remove(ctx, pid);
    }
    sa1_preview_clear(ctx);
    sa1_character_destroy(ctx, ch);
}

/* ---- (v) a rider onto a HURT strip and into a WATER volume (review R.13) ------------------------------------------------- */
static void t_hurt_water(sa1_context *ctx, int32_t kind, int with_hurt)
{
    const float step = 64.0f;
    float x, z;
    sa1_character_id id;
    sa1_status st = SA1_OK;
    int32_t modes[64], last_mode = -1;
    uint32_t nm = 0, i, k, u0, hurt_pair = 0, water_pair = 0, back = 0;
    printf("\n== (v) %s riding onto a SOLID | HURT strip (x 64..128) and into WATER (top y 20 over x 160..2048)\n",
           kind == SA1_CHAR_TAILS ? "Tails" : "Sonic");
    tris_cap(4096);
    g_ntri = 0;
    for (x = -256.0f; x < 2048.0f; x += step) {
        for (z = -512.0f; z < 512.0f; z += step) {
            uint32_t s = (with_hurt && x >= 64.0f && x < 128.0f) ? (SA1_SURFACE_SOLID | SA1_SURFACE_HURT) : SA1_SURFACE_SOLID;
            quad2(x, x + step, z, z + step, 0.0f, s, s);
        }
    }
    for (x = 160.0f; x < 2048.0f; x += step) {
        for (z = -512.0f; z < 512.0f; z += step) {
            sa1_collision_tri *t = &g_tris[g_ntri];
            quad2(x, x + step, z, z + step, 0.0f, SA1_SURFACE_WATER, SA1_SURFACE_WATER);
            for (k = 0; k < 2; k++) {
                t[k].v[0][1] = t[k].v[1][1] = t[k].v[2][1] = 20.0f;
            }
        }
    }
    sa1_collision_set_static(ctx, g_tris, g_ntri);
    id = make(ctx, kind, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    u0 = g_unimpl;
    board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
    for (i = 0; i < 400; i++) {
        sa1_state_ex s;
        input_set(0.0f, i >= 20 ? 1.0f : 0.0f, 0u);
        st = tick(ctx, id);
        if (st != SA1_OK) {
            break;
        }
        state(ctx, id, &s);
        for (k = 0; k < nm && modes[k] != s.mode; k++) {
        }
        if (k == nm && nm < 64) {
            modes[nm++] = s.mode;
        }
        if (!hurt_pair && s.sa1_position[0] >= 64.0f) {
            hurt_pair = i + 1;
        }
        if (!water_pair && s.sa1_position[0] >= 160.0f) {
            water_pair = i + 1;
        }
        if (water_pair && (s.flags & SA1_STATE_ON_BOARD)) {
            back++;
        }
        last_mode = s.mode;
        if (i % 50 == 0 || i == 399) {
            printf("    pair %u: mode %d action %d pos (%.1f %.1f %.1f) pspd %.3f attr 0x%X\n", (unsigned)i, (int)s.mode,
                   (int)s.action, (double)s.sa1_position[0], (double)s.sa1_position[1], (double)s.sa1_position[2],
                   (double)s.player_speed[0], (unsigned)s.attr);
        }
        if (s.sa1_position[0] > 900.0f) {
            break;
        }
    }
    CHECK(st == SA1_OK && g_unimpl == u0, "no fault, no unimplemented call in %u pairs (%s)", (unsigned)i,
          st == SA1_OK ? "ok" : sa1_last_error(ctx));
    printf("  note  modes seen:");
    for (k = 0; k < nm; k++) {
        printf(" %d", (int)modes[k]);
    }
    printf("; %s at pair %u, the water at pair %u; pairs in a board mode after the water: %u; the last mode %d\n",
           with_hurt ? "the HURT strip reached" : "(no HURT strip)", (unsigned)hurt_pair, (unsigned)water_pair,
           (unsigned)back, (int)last_mode);
    sa1_character_destroy(ctx, id);
}

/* ---- (d) / (d2) / (d3) / (e) / (n) the start push (STG08 0C90E810) ------------------------------------------------------ */
static void t_push(sa1_context *ctx, int32_t kind)
{
    static const uint32_t marks[4] = { 20, 60, 100, 150 };
    sa1_character_id id;
    sa1_board_state b;
    uint32_t i, k, e0, zero_at = 0;
    float at[4] = { -1, -1, -1, -1 }, v;
    const char *who = kind == SA1_CHAR_TAILS ? "Tails" : "Sonic";
    printf("\n== (d) the start push, %s, flat SOLID, input f\n", who);
    ground(ctx, 0.0f, SA1_SURFACE_SOLID);
    id = make(ctx, kind, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    e0 = g_nev;
    CHECK(board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, SA1_BOARD_START_PUSH) == SA1_OK, "ON | START_PUSH -> SA1_OK");
    for (i = 0; i < 160; i++) {
        input_f(i);
        tick(ctx, id);
        for (k = 0; k < 4; k++) {
            if (i + 1 == marks[k]) {
                at[k] = pspd(ctx, id);
            }
        }
        bstate(ctx, id, &b);
        if (!zero_at && b.push_steps == 0) {
            zero_at = i + 1;
        }
    }
    CHECK(fabsf(at[0] - 4.0f) < 5e-4f && fabsf(at[1] - 4.0f) < 5e-4f && fabsf(at[2] - 4.0f) < 5e-4f &&
          fabsf(at[3] - 4.0f) < 5e-4f, "%.5f / %.5f / %.5f / %.5f at board pairs 20 / 60 / 100 / 150 (4.000: 960 HU/s)",
          (double)at[0], (double)at[1], (double)at[2], (double)at[3]);
    CHECK((zero_at == 51 || zero_at == 52) && ev_count(id, SA1_EVENT_BOARD, SA1_BOARD_EV_PUSH, 0, e0) == 1,
          "push_steps 0 after 102 steps (board pair %u: it starts in the step the order is taken), SA1_BOARD_EV_PUSH arg 0",
          (unsigned)zero_at);
    sa1_character_destroy(ctx, id);
    if (kind == SA1_CHAR_TAILS) {
        return;
    }
    /* without the flag */
    id = make(ctx, kind, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, 0);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 20);
    v = pspd(ctx, id);
    CHECK(fabsf(v - 1.0f) < 1e-4f, "without the flag: %.3f at board pair 20 (1.000)", (double)v);
    sa1_character_destroy(ctx, id);
    /* (d2) review R.2: OFF at board pair 20, stick neutral */
    printf("\n== (d2) START_PUSH, OFF at board pair 20, stick neutral (review R.2)\n");
    id = make(ctx, kind, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, SA1_BOARD_START_PUSH);
    ticks(ctx, id, 20);
    board(ctx, id, SA1_BOARD_OFF, SA1_BOARD_TERRAIN_MAP, 0);
    tick(ctx, id);
    bstate(ctx, id, &b);
    k = b.push_steps;
    ticks(ctx, id, 2);
    v = pspd(ctx, id);
    CHECK(k == 0 && v < 4.0f, "push_steps %u in the exit pair, spd.x %.3f two pairs later (< 4.0; without the rule: 4.000 on foot "
          "for pairs 30-49)", (unsigned)k, (double)v);
    sa1_character_destroy(ctx, id);
    /* (d3) review R.2: a refused ON with START_PUSH leaves no push task */
    printf("\n== (d3) START_PUSH with a refused ON (review R.2)\n");
    {
        sa1_hostobj_desc d;
        sa1_state_ex s, s2;
        sa1_character_id c;
        uint32_t obj = 0, obj2 = 0, held = 0;
        id = make(ctx, kind, 0, 0, 0.0f, 12.0f);
        c = make(ctx, kind, 0, 0, 0.0f, 12.0f);
        memset(&g_in, 0, sizeof g_in);
        ticks(ctx, id, 20);
        ticks(ctx, c, 20);
        state(ctx, id, &s);
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.kind = SA1_HOSTOBJ_HOLDABLE;
        d.position[0] = s.position[0] + 10.0f;
        d.position[1] = s.position[1];
        d.position[2] = s.position[2];
        d.armor = -1;
        d.touch = -1;
        d.num_shapes = 1;
        d.shapes[0].form = SA1_SHAPE_CYLINDER;
        d.shapes[0].size[0] = 2.0f;
        d.shapes[0].size[1] = 2.0f;
        d.shapes[0].center[2] = 2.0f;
        sa1_hostobj_add(ctx, &d, &obj);
        (void)obj2;
        ticks(ctx, id, 2);
        ticks(ctx, c, 2);
        for (i = 0; i < 40 && !held; i++) {
            input_set(0.0f, 0.0f, i == 0 ? SA1_BUTTON_ACTION : 0u);
            tick(ctx, id);
            tick(ctx, c);
            state(ctx, id, &s);
            held = s.held_object == obj;
        }
        memset(&g_in, 0, sizeof g_in);
        ticks(ctx, id, 4);
        ticks(ctx, c, 4);
        e0 = g_nev;
        board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, SA1_BOARD_START_PUSH);
        ticks(ctx, id, 3);
        ticks(ctx, c, 3);
        bstate(ctx, id, &b);
        state(ctx, id, &s);
        state(ctx, c, &s2);
        CHECK(held && ev_count(id, SA1_EVENT_BOARD, SA1_BOARD_EV_REFUSED, -1, e0) == 1 && b.push_steps == 0 &&
              s.player_speed[0] == s2.player_speed[0], "refused (holding: %s): no push task left (push_steps %u), spd.x %.4f "
              "as a run without the call (%.4f)", held ? "yes" : "NO", (unsigned)b.push_steps, (double)s.player_speed[0],
              (double)s2.player_speed[0]);
        sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
        sa1_character_destroy(ctx, id);
        sa1_character_destroy(ctx, c);
    }
    /* (e) PUSH after the jump */
    printf("\n== (e) PUSH after the jump (the call at board pair 200)\n");
    id = make(ctx, kind, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    CHECK(board(ctx, id, SA1_BOARD_PUSH, SA1_BOARD_TERRAIN_MAP, 0) == SA1_ERR_UNSUPPORTED, "PUSH not riding: "
          "SA1_ERR_UNSUPPORTED (\"%s\")", sa1_last_error(ctx));
    board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, SA1_BOARD_START_PUSH);
    for (i = 0; i < 300; i++) {
        input_f(i);
        if (i == 171) {
            sa1_status st = board(ctx, id, SA1_BOARD_PUSH, SA1_BOARD_TERRAIN_MAP, 0);
            CHECK(st == SA1_ERR_UNSUPPORTED, "PUSH in the air (board pair 172, after the jump): SA1_ERR_UNSUPPORTED (\"%s\")",
                  sa1_last_error(ctx));
        }
        if (i == 199) {
            e0 = g_nev;
            CHECK(board(ctx, id, SA1_BOARD_PUSH, SA1_BOARD_TERRAIN_MAP, 0) == SA1_OK, "PUSH at board pair 200 -> SA1_OK (%.3f "
                  "before)", (double)pspd(ctx, id));
        }
        tick(ctx, id);
        if (i + 1 == 200) {
            at[0] = pspd(ctx, id);
        }
        if (i + 1 == 300) {
            at[1] = pspd(ctx, id);
        }
    }
    CHECK(fabsf(at[0] - 4.0f) < 1e-4f && fabsf(at[1] - 4.0f) < 1e-4f && ev_count(id, SA1_EVENT_BOARD, SA1_BOARD_EV_PUSH, 1, e0) == 1,
          "%.3f at 200, %.3f at 300 (4.000), SA1_BOARD_EV_PUSH arg 1", (double)at[0], (double)at[1]);
    sa1_character_destroy(ctx, id);
    /* (n) a camera yaw that follows the character, push, stick forward + left */
    printf("\n== (n) a camera yaw that follows the board, START_PUSH, stick forward + left 0.7 from board pair 31\n");
    id = make(ctx, kind, 0, 0, 0.0f, 12.0f);
    memset(&g_in, 0, sizeof g_in);
    ticks(ctx, id, 10);
    board(ctx, id, SA1_BOARD_ON, SA1_BOARD_TERRAIN_MAP, SA1_BOARD_START_PUSH);
    {
        uint32_t below = 0;
        float minv = 99.0f;
        for (i = 0; i < 300; i++) {
            sa1_state s;
            memset(&s, 0, sizeof s);
            s.struct_size = sizeof s;
            sa1_character_state(ctx, id, (sa1_state *)&s);
            g_in.camera_yaw = s.yaw;
            input_set(i >= 30 ? -0.7f : 0.0f, 1.0f, 0u);
            tick(ctx, id);
            v = pspd(ctx, id);
            if (i >= 2) {
                below += v < 4.0f - 5e-4f;
                minv = v < minv ? v : minv;
            }
        }
        g_in.camera_yaw = 0.0f;
        CHECK(below == 0, "4.000 for 300 pairs (min %.5f; a fixed camera: 0.45-0.49 after the turn)", (double)minv);
    }
    sa1_character_destroy(ctx, id);
}

/* ---- main --------------------------------------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    const char *gdi = NULL;
    sa1_context *ctx;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            g_only = argv[++i];
        } else if (strcmp(argv[i], "--verbose") == 0) {
            g_verbose = 1;
        } else {
            gdi = argv[i];
        }
    }
    if (gdi == NULL) {
        printf("usage: board_test [--only a,b,...] [--verbose] GDI\n");
        return 2;
    }
    printf("board_test: libsa1 %d.%d.%d, SA1_API_BOARD %d, sizeof sa1_board_state %u, sa1_input %u\n", SA1_VERSION_MAJOR,
           SA1_VERSION_MINOR, SA1_VERSION_PATCH, SA1_API_BOARD, (unsigned)sizeof(sa1_board_state), (unsigned)sizeof(sa1_input));
    ctx = make_ctx(gdi, SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS);
    if (ctx == NULL) {
        return 1;
    }
    if (want("a")) {
        t_enter(ctx, SA1_CHAR_SONIC);
        t_enter(ctx, SA1_CHAR_TAILS);
    }
    if (want("b")) {
        t_snow(ctx);
    }
    if (want("c")) {
        t_terrain(ctx);
    }
    if (want("s")) {
        t_kinds_surface(ctx, SA1_SURFACE_BOARD_SNOW, 0x8u, 4.403f, "SA1_SURFACE_BOARD_SNOW");
        t_kinds_surface(ctx, SA1_SURFACE_BOARD_SAND, 0x10u, 2.897f, "SA1_SURFACE_BOARD_SAND");
    }
    if (want("s2")) {
        t_seam(ctx, 0.0f);
        t_seam(ctx, 15.0f);
    }
    if (want("s3")) {
        t_moving_kind(ctx, SA1_SURFACE_SOLID | SA1_SURFACE_BOARD_SNOW, SA1_BOARD_TERRAIN_MAP, "SOLID | BOARD_SNOW, TERRAIN_MAP");
        t_moving_kind(ctx, SA1_SURFACE_SOLID, SA1_BOARD_TERRAIN_SNOW, "plain SOLID, TERRAIN_SNOW");
    }
    if (want("d")) {
        t_push(ctx, SA1_CHAR_SONIC);
        t_push(ctx, SA1_CHAR_TAILS);
    }
    if (want("f")) {
        t_tails_jump(ctx);
    }
    if (want("g")) {
        t_kinds(ctx, gdi);
    }
    if (want("h")) {
        t_refused(ctx);
    }
    if (want("i")) {
        t_reentry(ctx);
    }
    if (want("j")) {
        t_kept(ctx, 0, "default");
        t_kept(ctx, SA1_BOARD_KEEP_FALLEN, "KEEP_FALLEN");
        t_kept(ctx, SA1_BOARD_REMOVE_ON_EXIT, "REMOVE_ON_EXIT at ON");
    }
    if (want("k")) {
        t_textures(ctx, SA1_CHAR_SONIC);
        t_textures(ctx, SA1_CHAR_TAILS);
    }
    if (want("l")) {
        t_origin(ctx, SA1_CHAR_SONIC);
        t_origin(ctx, SA1_CHAR_TAILS);
    }
    if (want("m")) {
        t_fly(ctx);
    }
    if (want("o")) {
        t_determinism(ctx);
    }
    if (want("p")) {
        t_super(ctx);
    }
    if (want("q")) {
        t_free(ctx, 0.0f);
        t_free(ctx, 15.0f);
    }
    if (want("t")) {
        t_preview(ctx);
    }
    if (want("r")) {
        t_pose(ctx, SA1_CHAR_SONIC, 0.0f);
        t_pose(ctx, SA1_CHAR_TAILS, 0.0f);
        t_pose(ctx, SA1_CHAR_SONIC, 15.0f);
    }
    if (want("v")) {
        t_hurt_water(ctx, SA1_CHAR_SONIC, 1);
        t_hurt_water(ctx, SA1_CHAR_TAILS, 1);
        t_hurt_water(ctx, SA1_CHAR_SONIC, 0);
        t_hurt_water(ctx, SA1_CHAR_TAILS, 0);
    }
    if (want("u")) {
        t_measure(ctx);
    }
    sa1_shutdown(ctx);
    free(g_tris);
    printf("\n%s: %d passed, %d failed\n", g_fail ? "FAIL" : "PASS", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
