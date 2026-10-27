/*
 * tailsnap_test.c - round 0.9.1: the game option SA1_GOPT_TAILS_SNAP_TAILS (sadc docs/research/gsa1_rounds_plan.md 14.1h,
 * the owner's answer to gsa1_070_tails.md T4; the SA1_GA block SA1_TAILSNAP of sadc src/1st_read/code_8C0BA820.c).
 *
 *   tailsnap_test [--verbose] GDI
 *
 * Built and run by tests/build_charopt.py. Two Tails worlds on the same flat floor with the same input (stand, run 150
 * pairs, stand 150 pairs): S with the option set right after create_ex, G without it (the game). After every pair it
 * reads the world's tail morph state: pwp->free.f[6] (the morph weight, +0x94), the morph nodes' models (D_8C25824C /
 * D_8C2588D8 ->model), the morph buffers' points (D_8C275060 / D_8C27566C) against the bent tails' (D_8C273E3C /
 * D_8C274448) and the angle between the two tails (D_8C2582E8->ang[0] - D_8C258974->ang[0]).
 *
 *   S: while 0 < f[6] < 1 the nodes draw the blades from f[6] = 0.5 and below it the buffers holding exactly the bent
 *      tails; the angle between the tails is 0 or 0x8000 in every pair
 *   G: the game's blend: some pair with 0.1 < f[6] < 0.4 draws a buffer that is neither, and some angle is in between
 *   both: f[6], the position and the mode of every pair are bit-identical (the option changes the display only)
 *   an unknown option: SA1_ERR_INVALID_ARGUMENT; an unknown id: SA1_ERR_NOT_FOUND; switching it off returns to the game's
 *   blend (the next pairs of a run-up draw blended buffers again)
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

static int g_fail, g_ok, g_verbose;
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

#define PHYS(a)         ((a) & 0x1FFFFFFFu)
#define NODE_A          0x8C25824Cu     /* tail A morph node (NJS_OBJECT, model at +4) */
#define NODE_B          0x8C2588D8u
#define BLADE_A         0x8C25FF64u     /* NJS_MODEL at f[6] = 1 */
#define BLADE_B         0x8C2605E4u
#define BENT_A          0x8C273E3Cu     /* NJS_MODEL at f[6] = 0 (bent by 8C0BDC80) */
#define BENT_B          0x8C274448u
#define BUF_A           0x8C275060u     /* the morph buffers */
#define BUF_B           0x8C27566Cu
#define TAIL_A          0x8C258974u     /* NJS_OBJECT, ang[0] at +0x14 */
#define TAIL_B          0x8C2582E8u

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

static uint32_t wrd(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (w == NULL || sa1g_world_rd32(w, g, &v) != 0) {
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

/* the largest difference between the points of two models (their nbPoint * 3 floats) */
static float pts_diff(const sa1g_world *w, uint32_t ma, uint32_t mb)
{
    uint32_t pa = wrd(w, ma), pb = wrd(w, mb), n = wrd(w, ma + 8u), i;
    float worst = 0.0f;
    if (n == 0 || n > 4096u || n != wrd(w, mb + 8u)) {
        return 1e9f;
    }
    for (i = 0; i < 3u * n; i++) {
        float d = fabsf(wrdf(w, pa + 4u * i) - wrdf(w, pb + 4u * i));
        worst = d > worst ? d : worst;
    }
    return worst;
}

typedef struct tstate {
    float f6;
    uint32_t ma, mb;            /* the nodes' models */
    float bent_diff;            /* buffer A vs bent A (when node A draws the buffer) */
    float blade_diff;           /* buffer A vs blade A */
    uint32_t dang;              /* tail B - tail A (BAMS, 16 bits) */
    sa1_state_ex x;
} tstate;

static void read_state(sa1_context *ctx, sa1_character_id id, tstate *s)
{
    const sa1g_world *w = sa1_character_world(ctx, id);
    sa1g_player_ptrs pp;
    memset(s, 0, sizeof *s);
    s->x.struct_size = sizeof s->x;
    sa1_character_state_ex(ctx, id, &s->x);
    sa1g_world_player(w, &pp);
    s->f6 = wrdf(w, pp.pwp + 0x94u);
    s->ma = PHYS(wrd(w, NODE_A + 4u));
    s->mb = PHYS(wrd(w, NODE_B + 4u));
    s->bent_diff = pts_diff(w, BUF_A, BENT_A);
    s->blade_diff = pts_diff(w, BUF_A, BLADE_A);
    s->dang = (wrd(w, TAIL_B + 0x14u) - wrd(w, TAIL_A + 0x14u)) & 0xFFFFu;
}

static int tick(sa1_context *ctx, sa1_character_id id, float sy)
{
    sa1_input in;
    sa1_event ev[64];
    uint32_t n;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_y = sy;
    if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
        printf("  tick: %s\n", sa1_last_error(ctx));
        return -1;
    }
    do {
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
    } while (n == 64);
    return 0;
}

static sa1_character_id mk_tails(sa1_context *ctx)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_TAILS;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[0] = -2000.0f;
    d.position[1] = 2.0f;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("  create: %s\n", sa1_last_error(ctx));
        return 0;
    }
    return id;
}

int main(int argc, char **argv)
{
    const char *gdi = NULL;
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_collision_tri t[2];
    sa1_character_id s, g;
    uint32_t i, mid = 0, snap_ok = 0, snap_bad = 0, ang_ok = 1, blades = 0, bents = 0, game_blend = 0, game_ang = 0;
    uint32_t same = 1, maxed = 0, reblend = 0;
    int a;
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--verbose")) {
            g_verbose = 1;
        } else {
            gdi = argv[a];
        }
    }
    if (gdi == NULL) {
        gdi = getenv("SA1_TEST_GDI");
    }
    if (gdi == NULL) {
        printf("usage: tailsnap_test [--verbose] GDI\n");
        return 2;
    }
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return 1;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = 1u << SA1_CHAR_TAILS;
    lo.flags = SA1_LOAD_DEFAULT;
    memset(t, 0, sizeof t);
    {
        float p[4][3] = { { -6000, 0, -6000 }, { -6000, 0, 6000 }, { 6000, 0, 6000 }, { 6000, 0, -6000 } };
        memcpy(t[0].v[0], p[0], 12);
        memcpy(t[0].v[1], p[1], 12);
        memcpy(t[0].v[2], p[2], 12);
        memcpy(t[1].v[0], p[0], 12);
        memcpy(t[1].v[1], p[2], 12);
        memcpy(t[1].v[2], p[3], 12);
        t[0].surface = t[1].surface = SA1_SURFACE_SOLID;
    }
    if (SA1_FAILED(sa1_load_disc(ctx, gdi, &lo)) || sa1_collision_set_static(ctx, t, 2) != SA1_OK) {
        printf("context: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return 1;
    }
    printf("libsa1 %s\n", sa1_version_string());
    s = mk_tails(ctx);
    g = mk_tails(ctx);
    if (s == 0 || g == 0) {
        sa1_shutdown(ctx);
        return 1;
    }
    CHECK(sa1_character_set_game_option(ctx, s, SA1_GOPT_TAILS_SNAP_TAILS, 1) == SA1_OK, "S: set_game_option(TAILS_SNAP, "
          "1) right after create_ex");
    CHECK(sa1_character_set_game_option(ctx, s, 99u, 1) == SA1_ERR_INVALID_ARGUMENT, "an unknown option: "
          "SA1_ERR_INVALID_ARGUMENT");
    CHECK(sa1_character_set_game_option(ctx, (sa1_character_id)0x7FFF00FF, SA1_GOPT_TAILS_SNAP_TAILS, 1) ==
              SA1_ERR_NOT_FOUND, "an unknown id: SA1_ERR_NOT_FOUND");
    for (i = 0; i < 320; i++) {
        float sy = (i >= 20 && i < 170) ? 1.0f : 0.0f;
        tstate ss, gs;
        if (tick(ctx, s, sy) || tick(ctx, g, sy)) {
            g_fail++;
            break;
        }
        read_state(ctx, s, &ss);
        read_state(ctx, g, &gs);
        if (memcmp(&ss.f6, &gs.f6, 4) != 0 || memcmp(ss.x.sa1_position, gs.x.sa1_position, 12) != 0 ||
            ss.x.mode != gs.x.mode || ss.x.action != gs.x.action) {
            if (same && g_verbose) {
                printf("    pair %u: S f6 %.3f mode %d, G f6 %.3f mode %d\n", (unsigned)i, ss.f6, (int)ss.x.mode, gs.f6,
                       (int)gs.x.mode);
            }
            same = 0;
        }
        maxed |= ss.f6 >= 1.0f;
        if (ss.f6 > 0.0f && ss.f6 < 1.0f) {
            int blade = ss.f6 >= 0.5f;
            mid++;
            if (blade ? (ss.ma == PHYS(BLADE_A) && ss.mb == PHYS(BLADE_B))
                      : (ss.ma == PHYS(BUF_A) && ss.mb == PHYS(BUF_B) && ss.bent_diff == 0.0f)) {
                snap_ok++;
                blades += blade;
                bents += !blade;
            } else {
                if (snap_bad++ < 4) {
                    printf("    S pair %u: f6 %.3f node A model %08X (blade %08X, buffer %08X), buffer - bent %.4f\n",
                           (unsigned)i, ss.f6, (unsigned)ss.ma, (unsigned)PHYS(BLADE_A), (unsigned)PHYS(BUF_A),
                           ss.bent_diff);
                }
            }
        }
        if (ss.dang != 0u && ss.dang != 0x8000u) {
            if (ang_ok && g_verbose) {
                printf("    S pair %u: f6 %.3f angle between the tails %04X\n", (unsigned)i, ss.f6, (unsigned)ss.dang);
            }
            ang_ok = 0;
        }
        if (gs.f6 > 0.1f && gs.f6 < 0.4f && gs.ma == PHYS(BUF_A) && gs.bent_diff > 0.01f && gs.blade_diff > 0.01f) {
            game_blend++;
        }
        if (gs.f6 > 0.1f && gs.f6 < 0.9f && gs.dang != 0u && gs.dang != 0x8000u) {
            game_ang++;
        }
    }
    CHECK(maxed && mid > 0 && snap_ok == mid && snap_bad == 0 && blades > 0 && bents > 0,
          "S: in %u pairs with 0 < f[6] < 1 the nodes drew the blades (%u pairs, f[6] >= 0.5) or exactly the bent tails "
          "(%u pairs); %u pairs drew anything else", (unsigned)mid, (unsigned)blades, (unsigned)bents, (unsigned)snap_bad);
    CHECK(ang_ok, "S: the angle between the two tails is 0 or 0x8000 in every pair");
    CHECK(game_blend > 0 && game_ang > 0, "G (the game): %u pairs drew a blended buffer, %u pairs an angle in between",
          (unsigned)game_blend, (unsigned)game_ang);
    CHECK(same, "f[6], position, mode and action identical in S and G in every pair (the option is display only)");
    /* switched off: a new run-up blends again */
    CHECK(sa1_character_set_game_option(ctx, s, SA1_GOPT_TAILS_SNAP_TAILS, 0) == SA1_OK, "S: set_game_option(TAILS_SNAP, "
          "0)");
    for (i = 0; i < 60; i++) {
        tstate ss;
        if (tick(ctx, s, 1.0f)) {
            g_fail++;
            break;
        }
        read_state(ctx, s, &ss);
        if (ss.f6 > 0.1f && ss.f6 < 0.4f && ss.ma == PHYS(BUF_A) && ss.bent_diff > 0.01f && ss.blade_diff > 0.01f) {
            reblend++;
        }
    }
    CHECK(reblend > 0, "S after the switch went off: %u pairs of the new run-up drew a blended buffer", (unsigned)reblend);
    sa1_character_destroy(ctx, s);
    sa1_character_destroy(ctx, g);
    sa1_shutdown(ctx);
    printf("tailsnap_test: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
