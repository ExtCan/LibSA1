/*
 * hud_test.c - the HUD and the 2D screen output (round 0.9.1; include/libsa1.h "screen / HUD"; sadc
 * docs/research/gsa1_070_hud.md F2-F6 and F7's libsa1 half, with its Review R.5).
 *
 *   hud_test [--part LIST] [--out DIR] SADC [GDI]       (GDI: or $SA1_TEST_GDI; LIST: comma-separated 1-12, default all)
 *
 * Built by tests/build_hud.py (the world tests' flags; -I<SADC>/harness/src for the trace headers).
 *
 *  1. Sonic on Emerald Coast act 1 (DISC, the stage's tasks) with every HUD part and the clock on: 15 sprites per pair
 *     (the TIME label / ring icon and 3 ring digits, the life icon and 2 lives digits, 8 timer digits) at the game's
 *     positions (gsa1_070_hud.md 2 C), the clock in sa1_state_ex, CON_REGULAR textures (case 8).
 *  2. Parts masks: rings off 11, lives off 12, time off 7 (the label cropped to the ring-only icon at (16, 49), Review
 *     R.4.4), time only 8, none 0.
 *  3. RAM: the split pass against the game's own single calls (8C08A4BA, 8C08A724), 300 pairs with inputs: Sonic, Big
 *     and Sonic with a running event (B_8C752BDD -1): guest RAM identical (Review R.5.1, part G); against no pass at all:
 *     the player bitwise equal and only the HUD's display words differ (part D).
 *  4. Replays never run the pass (hud_fn NULL without settings; w->replay set: no pass).
 *  5. Big's own HUD (exec 8C0EB160, decompiled by hud-decomp / landed by 091-land): WEIGHT, rings, lives as CHARACTER
 *     draws with FISHING / CON_REGULAR textures; none of the common parts (8C08A4BA / 8C08A724 skip player 7).
 *  6. E-102 (Final Egg act 1): the game's time limit from 00:02:00 (TIME_UP / DEATH); SA1_COPT_E102_NO_TIME_LIMIT:
 *     no death in 400 pairs, the time held through a game-code 8C08A5AE and a lock-on time bonus 8C0D5064; a respawn
 *     that carries 00:00:00 starts at 03:00:00 (no death loop, Review R.4.2).
 *  7. Counters across a re-creation: sa1_character_set_time 1:23:45 / set_lives 2 before the build appear in sa1_state_ex
 *     and in the HUD's digits.
 *  8. Archives: CON_REGULAR loaded for every character (E-102: 8 archives, all his own ones), no "more than N texture
 *     archives" warning.
 *  9. Big fishing (Emerald Coast act 3): the tension bar is a POLY2D record (njDrawPolygon2D, F2) emitted as an opaque
 *     gouraud quad drawn before the translucent gauge frame (priority -1.82 behind -1.819).
 * 10. Colours: the zero-ring blink (r 1, g = b pulsing), E-102's timer red under 15 s.
 * 11. sa1_render is unchanged by the HUD (draw and triangle counts with and without it, every pair).
 * 12. F7, libsa1 half: the hud_* and big_drown_ec3 references replayed (FAITHFUL dump geometry, the record's inputs, the
 *     host-object sidecar) with the HUD pass after every sub step 1, the 2D records of each step written in the
 *     harness's NAME.d2d.txt format to OUT/<name>.lib.d2d.txt (tests/build_hud.py compares them with the references:
 *     harness/tools/draw2d.py's --lib rules); every record of the trace must stay identical with the pass (decoded
 *     fields).
 */
#include "world_replay.c"

#include <ctype.h>
#include <math.h>

#include "sa1_api_world.h"
#include "sa1_capture.h"
#include "sa1_coladapt.h"
#include "sa1_screen.h"

static int g_fail, g_skip;
static int g_arch_warn;

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

#define INJ_FULL (SA1G_INJ_PAD | SA1G_INJ_ANALOG | SA1G_INJ_RAND | SA1G_INJ_XMTRX | SA1G_INJ_CAMERA)

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (strstr(msg, "texture archives") != NULL) {
        g_arch_warn++;
    }
    if (level >= SA1_LOG_WARN) {
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : "warn", msg);
    }
}

typedef struct env {
    const char *sadc, *gdi, *out;
    uint32_t parts;
} env;

/* ---- contexts and characters --------------------------------------------------------------------------------------- */
static sa1_context *new_ctx(const env *E, uint32_t chars)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = chars;
    lo.flags = SA1_LOAD_DEFAULT;
    if (SA1_FAILED(sa1_load_disc(ctx, E->gdi, &lo))) {
        printf("    sa1_load_disc: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static int set_disc(sa1_context *ctx, int32_t level, int32_t act)
{
    sa1_collision_static col;
    memset(&col, 0, sizeof col);
    col.struct_size = sizeof col;
    col.mode = SA1_COLLISION_MODE_DISC;
    col.disc_level = level;
    col.disc_act = act;
    if (sa1_collision_set_static_ex(ctx, &col) != SA1_OK) {
        printf("    DISC %d/%d: %s\n", (int)level, (int)act, sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

/* a character at the stage's start with the stage's tasks; the world is built by the first tick (the settings given
 * before it reach the world through the attach) */
static sa1_character_id stage_char(sa1_context *ctx, int32_t kind, int32_t level, int32_t act)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_START_TABLE | SA1_CREATE_STAGE | SA1_CREATE_STAGE_TASKS;
    d.level = level;
    d.act = act;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("    create %s: %s\n", sa1_character_name(kind), sa1_last_error(ctx));
        return 0;
    }
    return id;
}

static int hud_on(sa1_context *ctx, sa1_character_id id, uint32_t parts, int clock)
{
    return sa1_character_set_option(ctx, id, SA1_COPT_HUD_PARTS, (int32_t)parts) == SA1_OK &&
           sa1_character_set_option(ctx, id, SA1_COPT_CLOCK, clock) == SA1_OK;
}

static int tick(sa1_context *ctx, sa1_character_id id, uint32_t buttons, float sy)
{
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.buttons = buttons;
    in.stick_y = sy;
    if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
        printf("    tick: %s\n", sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

/* the input of pair k of a 300-pair script: stand, run, jump, action, turn */
static void script_input(uint32_t k, uint32_t *buttons, float *sy)
{
    uint32_t m = k % 100u;
    *buttons = 0;
    *sy = 0.0f;
    if (m >= 10u && m < 50u) {
        *sy = 1.0f;
    }
    if (m == 20u || m == 21u || m == 60u) {
        *buttons |= SA1_BUTTON_JUMP;
    }
    if (m >= 70u && m < 74u) {
        *buttons |= SA1_BUTTON_ACTION;
    }
}

static const sa1_screen *screen_of(sa1_context *ctx, sa1_character_id id)
{
    const sa1_screen *s = NULL;
    if (sa1_character_screen(ctx, id, &s) != SA1_OK) {
        printf("    sa1_character_screen: %s\n", sa1_last_error(ctx));
        return NULL;
    }
    return s;
}

static int32_t archive_named(const sa1_context *ctx, int32_t kind, const char *name)
{
    uint32_t a;
    for (a = 0; a < 16u; a++) {
        const char *nm = NULL;
        uint32_t n = 0;
        if (sa1_texture_archive(ctx, kind, a, &nm, &n) != SA1_OK) {
            break;
        }
        if (nm != NULL && strcmp(nm, name) == 0) {
            return (int32_t)a;
        }
    }
    return -1;
}

/* the sprite i of the screen and its draw */
static const sa1_screen_draw *draw_of_sprite(const sa1_screen *s, uint32_t i)
{
    uint32_t d;
    for (d = 0; d < s->num_draws; d++) {
        const sa1_screen_draw *dr = &s->draws[d];
        if (dr->prim == SA1_PRIM_SPRITES && i >= dr->first && i < dr->first + dr->count) {
            return dr;
        }
    }
    return NULL;
}

static uint32_t count_part(const sa1_screen *s, uint32_t part)
{
    uint32_t i, n = 0;
    for (i = 0; s != NULL && i < s->num_sprites; i++) {
        n += s->sprites[i].part == part;
    }
    return n;
}

/* a sprite of `part` at pivot (x, y) (0: none) */
static const sa1_screen_sprite *sprite_at(const sa1_screen *s, uint32_t part, float x, float y)
{
    uint32_t i;
    for (i = 0; s != NULL && i < s->num_sprites; i++) {
        const sa1_screen_sprite *p = &s->sprites[i];
        if (p->part == part && p->pivot[0] == x && p->pivot[1] == y) {
            return p;
        }
    }
    return NULL;
}

static uint32_t sprite_index(const sa1_screen *s, const sa1_screen_sprite *p)
{
    return (uint32_t)(p - s->sprites);
}

/* the texture of the sprite at (x, y) of `part` (-1: none / other archive) */
static int32_t tex_at(const sa1_screen *s, uint32_t part, float x, float y, int32_t arc)
{
    const sa1_screen_sprite *p = sprite_at(s, part, x, y);
    const sa1_screen_draw *d = p != NULL ? draw_of_sprite(s, sprite_index(s, p)) : NULL;
    return (d != NULL && d->archive == arc) ? d->texture : -1;
}

/* ---- 1, 2, 8: Sonic's HUD -------------------------------------------------------------------------------------- */
static void test_sonic(const env *E)
{
    sa1_context *ctx = new_ctx(E, (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_GAMMA));
    sa1_character_id id;
    const sa1_screen *s;
    sa1_state_ex x;
    int32_t con;
    uint32_t k, bad = 0, i, ok;
    printf("1. Sonic, every part, the clock on (Emerald Coast act 1, DISC):\n");
    if (ctx == NULL || set_disc(ctx, 1, 0) != 0) {
        CHECK(0, "context");
        if (ctx)
            sa1_shutdown(ctx);
        return;
    }
    id = stage_char(ctx, SA1_CHAR_SONIC, 1, 0);
    CHECK(id != 0 && hud_on(ctx, id, SA1_HUD_ALL, 1), "SA1_COPT_HUD_PARTS SA1_HUD_ALL and SA1_COPT_CLOCK 1 before the build");
    for (k = 0; k < 60u && id; k++) {
        if (tick(ctx, id, 0, 0.0f) != 0) {
            break;
        }
        s = screen_of(ctx, id);
        if (s == NULL || s->num_sprites != 15u || count_part(s, SA1_HUD_RINGS) != 4u || count_part(s, SA1_HUD_LIVES) != 3u ||
            count_part(s, SA1_HUD_TIME) != 8u) {
            if (bad++ == 0) {
                printf("    pair %u: %u sprites (rings %u, lives %u, time %u, character %u)\n", (unsigned)k,
                       s ? (unsigned)s->num_sprites : 0u, (unsigned)count_part(s, SA1_HUD_RINGS),
                       (unsigned)count_part(s, SA1_HUD_LIVES), (unsigned)count_part(s, SA1_HUD_TIME),
                       (unsigned)count_part(s, SA1_HUD_CHARACTER));
            }
        }
    }
    CHECK(k == 60u && bad == 0, "60 pairs: 15 sprites every pair (rings 4, lives 3, time 8; %u pairs other)", (unsigned)bad);
    s = id ? screen_of(ctx, id) : NULL;
    con = archive_named(ctx, SA1_CHAR_SONIC, "CON_REGULAR");
    ok = s != NULL && sprite_at(s, SA1_HUD_RINGS, 16.0f, 32.0f) && sprite_at(s, SA1_HUD_RINGS, 46.0f, 52.0f) &&
         sprite_at(s, SA1_HUD_RINGS, 62.0f, 52.0f) && sprite_at(s, SA1_HUD_RINGS, 78.0f, 52.0f) &&
         sprite_at(s, SA1_HUD_LIVES, 16.0f, 400.0f) && sprite_at(s, SA1_HUD_LIVES, 49.0f, 416.0f) &&
         sprite_at(s, SA1_HUD_LIVES, 65.0f, 416.0f);
    for (i = 0; ok && i < 8u; i++) {
        ok = sprite_at(s, SA1_HUD_TIME, 64.0f + 16.0f * (float)i, 33.0f) != NULL;
    }
    CHECK(ok, "positions: label (16, 32), rings (46 / 62 / 78, 52), life icon (16, 400), lives (49 / 65, 416), timer "
          "(64 + 16k, 33)");
    memset(&x, 0, sizeof x);
    x.struct_size = sizeof x;
    sa1_character_state_ex(ctx, id, &x);
    CHECK(x.time_minutes == 0 && x.time_seconds == 2 && x.time_frames == 0 && x.clock_running == 1 && x.lives == 4,
          "sa1_state_ex after 60 pairs (120 steps): %02d:%02d:%02d, clock_running %u, lives %d (want 00:02:00, 1, 4)",
          (int)x.time_minutes, (int)x.time_seconds, (int)x.time_frames, (unsigned)x.clock_running, (int)x.lives);
    /* 8: textures */
    {
        uint32_t nres = 0, nall = 0;
        for (i = 0; s != NULL && i < s->num_sprites; i++) {
            const sa1_screen_draw *d = draw_of_sprite(s, i);
            nall++;
            nres += d != NULL && d->archive == con && con >= 0 && d->texture >= 0 && d->texture < 24 &&
                    !(d->flags & SA1_DRAW_NO_TEXTURE) && (d->flags & SA1_DRAW_BLEND) && d->blend_src == SA1_BLEND_SRC_ALPHA &&
                    d->blend_dst == SA1_BLEND_INV_SRC_ALPHA;
        }
        CHECK(con >= 0 && nall == 15u && nres == nall,
              "8. every HUD sprite resolves into CON_REGULAR (archive %d) cells 0-23, blended SRC_ALPHA / INV_SRC_ALPHA "
              "(%u of %u)", (int)con, (unsigned)nres, (unsigned)nall);
        CHECK(tex_at(s, SA1_HUD_RINGS, 16.0f, 32.0f, con) == 11 && tex_at(s, SA1_HUD_LIVES, 16.0f, 400.0f, con) == 12 &&
              tex_at(s, SA1_HUD_TIME, 64.0f + 32.0f, 33.0f, con) == 10 && tex_at(s, SA1_HUD_TIME, 64.0f + 48.0f, 33.0f, con) == 0
              && tex_at(s, SA1_HUD_TIME, 64.0f + 64.0f, 33.0f, con) == 2,
              "cells: label 11 (hyoji_head), Sonic's life icon 12, the timer 00:02:00 (colon 10, '0', '2')");
    }
    /* 2: masks (one pair each) */
    printf("2. parts masks:\n");
    {
        static const struct { uint32_t parts, want; const char *what; } m[] = {
            { SA1_HUD_LIVES | SA1_HUD_TIME, 11u, "rings off" },
            { SA1_HUD_RINGS | SA1_HUD_TIME, 12u, "lives off" },
            { SA1_HUD_RINGS | SA1_HUD_LIVES, 7u, "time off" },
            { SA1_HUD_TIME, 8u, "time only" },
            { SA1_HUD_CHARACTER, 0u, "character only (Sonic's world draws no 2D of its own)" },
            { 0u, 0u, "none" },
        };
        for (i = 0; i < sizeof m / sizeof m[0] && id; i++) {
            sa1_character_set_option(ctx, id, SA1_COPT_HUD_PARTS, (int32_t)m[i].parts);
            tick(ctx, id, 0, 0.0f);
            s = screen_of(ctx, id);
            CHECK(s != NULL && s->num_sprites == m[i].want, "%s: %u sprites (want %u)", m[i].what,
                  s ? (unsigned)s->num_sprites : 0u, (unsigned)m[i].want);
            if (m[i].parts == (SA1_HUD_RINGS | SA1_HUD_LIVES) && s != NULL) {
                const sa1_screen_sprite *p = sprite_at(s, SA1_HUD_RINGS, 16.0f, 49.0f);
                CHECK(p != NULL && sprite_at(s, SA1_HUD_RINGS, 16.0f, 32.0f) == NULL && fabsf(p->uv[1] - 68.0f / 256.0f) < 1e-6f &&
                      fabsf((p->rect[3] - p->rect[1]) - 47.0f) < 1e-4f,
                      "time off: the TIME label drawn as the ring-only icon D_8C1BF5B4 at (16, 49), v0 68/256, 47 px high");
            }
        }
    }
    /* 10: the zero-ring blink */
    printf("10. colours:\n");
    {
        uint32_t gmin = 255, gmax = 0, good = 1;
        sa1_character_set_option(ctx, id, SA1_COPT_HUD_PARTS, (int32_t)SA1_HUD_ALL);
        for (k = 0; k < 40u && id; k++) {
            const sa1_screen_sprite *p;
            tick(ctx, id, 0, 0.0f);
            s = screen_of(ctx, id);
            p = s ? sprite_at(s, SA1_HUD_RINGS, 46.0f, 52.0f) : NULL;
            if (p == NULL || p->color[0] != 255 || p->color[3] != 255 || p->color[1] != p->color[2]) {
                good = 0;
            } else {
                gmin = p->color[1] < gmin ? p->color[1] : gmin;
                gmax = p->color[1] > gmax ? p->color[1] : gmax;
            }
        }
        CHECK(good && gmin < 64u && gmax > 192u, "0 rings: the ring digits blink red: r 255, a 255, g = b in %u..%u",
              (unsigned)gmin, (unsigned)gmax);
    }
    /* 7: the counters of a re-created character */
    printf("7. counters across a re-creation:\n");
    {
        sa1_character_id id2 = stage_char(ctx, SA1_CHAR_SONIC, 1, 0);
        /* 01:23:45 -> the hundredths 8C08A7E0 shows: (Uint32)(45 x 1.6666666f) in the world's round-toward-zero mode =
           (Uint32)74.99999 = 74 */
        int32_t want[8] = { 0, 1, 10, 2, 3, 10, 7, 4 };
        ok = id2 != 0 && sa1_character_set_time(ctx, id2, 1, 23, 45) == SA1_OK &&
             sa1_character_set_lives(ctx, id2, 2) == SA1_OK && hud_on(ctx, id2, SA1_HUD_ALL, 0) && tick(ctx, id2, 0, 0.0f) == 0;
        memset(&x, 0, sizeof x);
        x.struct_size = sizeof x;
        sa1_character_state_ex(ctx, id2, &x);
        CHECK(ok && x.time_minutes == 1 && x.time_seconds == 23 && x.time_frames == 45 && x.lives == 2 && !x.clock_running,
              "set_time 1:23:45 and set_lives 2 before the build: sa1_state_ex %02d:%02d:%02d, lives %d, clock %u",
              (int)x.time_minutes, (int)x.time_seconds, (int)x.time_frames, (int)x.lives, (unsigned)x.clock_running);
        s = id2 ? screen_of(ctx, id2) : NULL;
        for (i = 0; ok && i < 8u; i++) {
            ok = tex_at(s, SA1_HUD_TIME, 64.0f + 16.0f * (float)i, 33.0f, con) == want[i];
        }
        CHECK(ok && tex_at(s, SA1_HUD_LIVES, 49.0f, 416.0f, con) == 0 && tex_at(s, SA1_HUD_LIVES, 65.0f, 416.0f, con) == 2,
              "the HUD shows 01:23:74 and 02 lives");
        if (id2) {
            sa1_character_destroy(ctx, id2);
        }
    }
    if (id) {
        sa1_character_destroy(ctx, id);
    }
    sa1_shutdown(ctx);
}

/* ---- 3, 11: the pass against the game's single calls and against no pass ------------------------------------------ */
/* the HUD's display words, the only RAM the pass may change (gsa1_070_hud.md 2 D): D_8C1BF4E0's position, the current
 * texlist, the ring blink phase, the constant material */
static int display_word(uint32_t g)
{
    return (g >= 0x8C1BF4E0u && g < 0x8C1BF500u) || (g >= 0x8C673480u && g < 0x8C673484u) ||
           (g >= 0x8C7997ACu && g < 0x8C7997B0u) || (g >= 0x8C88F668u && g < 0x8C88F678u);
}

typedef struct ram_diff {
    uint32_t bytes, other, first_other;
} ram_diff;

static void diff_arena(const sa1g_world *a, const sa1g_world *b, ram_diff *d)
{
    uint32_t i;
    memset(d, 0, sizeof *d);
    for (i = 0x10000u; i < SA1G_ARENA_SIZE; i++) {
        if (a->ram[i] != b->ram[i]) {
            uint32_t g = 0x8C000000u + i;
            d->bytes++;
            if (!display_word(g) && d->other++ == 0) {
                d->first_other = g;
            }
        }
    }
}

static void event_on(sa1g_world *w, void *user)
{
    (void)user;
    *(int32_t *)sa1g_world_ptr(w, SA1G_ADDR_EVENT_RUNNING) = 1;
}

static void test_ram(const env *E, int32_t kind, int32_t level, int32_t act, int event)
{
    sa1_context *ctx = new_ctx(E, 1u << kind);
    sa1_character_id a, b, c;
    sa1g_world *wa, *wb, *wc;
    ram_diff ab, ac;
    uint32_t k, pos_bad = 0, render_bad = 0, passes0;
    const char *nm = sa1_character_name(kind);
    printf("3. RAM: %s%s, 300 pairs, the split pass vs the game's single calls vs no pass:\n", nm,
           event ? " with a running event (B_8C7492AC 1: B_8C752BDD -1)" : "");
    if (ctx == NULL || set_disc(ctx, level, act) != 0) {
        CHECK(0, "context");
        if (ctx)
            sa1_shutdown(ctx);
        return;
    }
    a = stage_char(ctx, kind, level, act);
    b = stage_char(ctx, kind, level, act);
    c = stage_char(ctx, kind, level, act);
    if (!a || !b || !c || !hud_on(ctx, a, SA1_HUD_ALL, 1) || !hud_on(ctx, b, SA1_HUD_ALL, 1) || !hud_on(ctx, c, 0, 1) ||
        tick(ctx, a, 0, 0.0f) || tick(ctx, b, 0, 0.0f) || tick(ctx, c, 0, 0.0f)) {
        CHECK(0, "three %s worlds", nm);
        sa1_shutdown(ctx);
        return;
    }
    wa = sa1_character_world(ctx, a);
    wb = sa1_character_world(ctx, b);
    wc = sa1_character_world(ctx, c);
    sa1s_world_test_single(wb, 1);
    passes0 = wa->hud.passes;
    for (k = 1; k < 300u; k++) {
        uint32_t bt;
        float sy;
        const sa1_render *ra = NULL, *rc = NULL;
        sa1_state_ex xa, xc;
        script_input(k, &bt, &sy);
        if (event) {
            sa1g_world_call(wa, event_on, NULL);
            sa1g_world_call(wb, event_on, NULL);
            sa1g_world_call(wc, event_on, NULL);
        }
        if (tick(ctx, a, bt, sy) || tick(ctx, b, bt, sy) || tick(ctx, c, bt, sy)) {
            break;
        }
        memset(&xa, 0, sizeof xa);
        xa.struct_size = sizeof xa;
        xc = xa;
        sa1_character_state_ex(ctx, a, &xa);
        sa1_character_state_ex(ctx, c, &xc);
        if (memcmp(xa.sa1_position, xc.sa1_position, sizeof xa.sa1_position) != 0 || xa.mode != xc.mode ||
            xa.action != xc.action || memcmp(xa.speed, xc.speed, sizeof xa.speed) != 0) {
            pos_bad++;
        }
        /* 11: the render output does not see the HUD */
        if (sa1_character_render(ctx, a, 1.0f, &ra) != SA1_OK || sa1_character_render(ctx, c, 1.0f, &rc) != SA1_OK ||
            ra->num_draws != rc->num_draws || ra->num_triangles != rc->num_triangles || ra->num_sprites != rc->num_sprites) {
            render_bad++;
        }
    }
    diff_arena(wa, wb, &ab);
    diff_arena(wa, wc, &ac);
    CHECK(k == 300u && wa->hud.passes - passes0 == 299u && wb->hud.passes - passes0 == 299u,
          "%u pairs, the pass ran in every pair of both (%u, %u)", (unsigned)k, (unsigned)(wa->hud.passes - passes0),
          (unsigned)(wb->hud.passes - passes0));
    CHECK(ab.bytes == 0, "the split pass leaves guest RAM as the game's single calls do: %u bytes differ (first %08X)",
          (unsigned)ab.bytes, (unsigned)ab.first_other);
    CHECK(ac.other == 0 && pos_bad == 0,
          "against no pass: %u bytes differ, all in the HUD's display words (%u elsewhere, first %08X); the player "
          "(position, speed, mode, action) bitwise equal in every pair (%u differ)", (unsigned)ac.bytes,
          (unsigned)ac.other, (unsigned)ac.first_other, (unsigned)pos_bad);
    CHECK(render_bad == 0, "11. sa1_render: the same draws, triangles and sprites with and without the HUD in every pair "
          "(%u differ)", (unsigned)render_bad);
    sa1_shutdown(ctx);
}

/* ---- 4: replays and worlds without settings never run the pass --------------------------------------------------- */
static void test_replay_safety(const env *E)
{
    sa1_context *ctx = new_ctx(E, 1u << SA1_CHAR_SONIC);
    sa1_character_id id;
    sa1g_world *w;
    sa1g_hud zero;
    uint32_t p0, k;
    printf("4. worlds without HUD settings and replay mode:\n");
    if (ctx == NULL || set_disc(ctx, 1, 0) != 0) {
        CHECK(0, "context");
        if (ctx)
            sa1_shutdown(ctx);
        return;
    }
    id = stage_char(ctx, SA1_CHAR_SONIC, 1, 0);
    tick(ctx, id, 0, 0.0f);
    w = sa1_character_world(ctx, id);
    memset(&zero, 0, sizeof zero);
    CHECK(w != NULL && w->hud_fn == NULL && memcmp(&w->hud, &zero, sizeof zero) == 0,
          "a world whose host gave no HUD setting: hud_fn NULL, no hold, nothing noted");
    if (w != NULL && sa1_character_set_option(ctx, id, SA1_COPT_HUD_PARTS, (int32_t)SA1_HUD_ALL) == SA1_OK) {
        p0 = w->hud.passes;
        w->replay = 1;
        for (k = 0; k < 4u; k++) {
            w->inject.flags = 0;
            sa1g_world_pair(w);
        }
        CHECK(w->hud_fn != NULL && w->hud.passes == p0, "replay mode: 4 pairs with the pass installed run it %u times",
              (unsigned)(w->hud.passes - p0));
        w->replay = 0;
    }
    sa1_shutdown(ctx);
}

/* ---- 5, 9: Big ------------------------------------------------------------------------------------------------- */
static void test_big(const env *E)
{
    sa1_context *ctx = new_ctx(E, 1u << SA1_CHAR_BIG);
    sa1_character_id id;
    const sa1_screen *s;
    int32_t fishing, con;
    uint32_t k, i, common = 0, mine = 0, fish_tex = 0, con19 = 0, bar_pairs = 0, order_ok = 0, bar_ok = 0;
    printf("5. Big's own HUD (Emerald Coast act 3):\n");
    if (ctx == NULL || set_disc(ctx, 1, 2) != 0) {
        CHECK(0, "context");
        if (ctx)
            sa1_shutdown(ctx);
        return;
    }
    id = stage_char(ctx, SA1_CHAR_BIG, 1, 2);
    fishing = archive_named(ctx, SA1_CHAR_BIG, "FISHING");
    con = archive_named(ctx, SA1_CHAR_BIG, "CON_REGULAR");
    if (!id || !hud_on(ctx, id, SA1_HUD_ALL, 1)) {
        CHECK(0, "Big");
        sa1_shutdown(ctx);
        return;
    }
    for (k = 0; k < 30u; k++) {
        tick(ctx, id, 0, 0.0f);
    }
    s = screen_of(ctx, id);
    for (i = 0; s != NULL && i < s->num_sprites; i++) {
        const sa1_screen_draw *d = draw_of_sprite(s, i);
        common += s->sprites[i].part != SA1_HUD_CHARACTER;
        if (s->sprites[i].part == SA1_HUD_CHARACTER && s->sprites[i].source == 0x8C0EB160u) {
            mine++;
            fish_tex += d != NULL && d->archive == fishing && fishing >= 0;
            con19 += d != NULL && d->archive == con && d->texture == 19;
        }
    }
    CHECK(s != NULL && common == 0 && mine >= 5u && fish_tex >= 2u && con19 == 1u,
          "%u sprites of his HUD task (exec 8C0EB160: WEIGHT and the ring with FISHING textures %u, the life icon "
          "CON_REGULAR 19 %u, numbers); none of the common HUD (%u: 8C08A4BA / 8C08A724 skip player 7)",
          (unsigned)mine, (unsigned)fish_tex, (unsigned)con19, (unsigned)common);
    printf("9. Big fishing: the tension bar (njDrawPolygon2D) behind the gauge frame:\n");
    {
        /* the probe's cast (gsa1_070_hud.md 2 B): 20 pairs nothing, 30 action, 40 nothing, 60 action, 90 nothing */
        static const struct { uint32_t n, b; } seq[] = { { 20, 0 }, { 30, SA1_BUTTON_ACTION }, { 40, 0 },
                                                         { 60, SA1_BUTTON_ACTION }, { 90, 0 } };
        uint32_t q;
        for (q = 0; q < sizeof seq / sizeof seq[0]; q++) {
            for (k = 0; k < seq[q].n; k++) {
                int32_t bar = -1, frame = -1;
                uint32_t d;
                tick(ctx, id, seq[q].b, 0.0f);
                s = screen_of(ctx, id);
                for (d = 0; s != NULL && d < s->num_draws; d++) {
                    const sa1_screen_draw *dr = &s->draws[d];
                    if (dr->prim == SA1_PRIM_TRIANGLES && dr->source == 0x8C0E943Cu && bar < 0) {
                        const sa1_screen_vertex *v = &s->vertices[dr->first];
                        bar = (int32_t)d;
                        /* an opaque gouraud quad x 574..606 from y 368 up; base cyan-ish (r c, g 255, b 255 - c), top g 0 */
                        bar_ok += dr->count == 6u && !(dr->flags & SA1_DRAW_BLEND) && dr->archive < 0 &&
                                  fabsf(dr->priority + 1.82f) < 1e-5f && v[0].x >= 574.0f && v[0].x <= 606.0f;
                    }
                    if (dr->prim == SA1_PRIM_SPRITES && dr->source == 0x8C0E943Cu && dr->archive == fishing &&
                        fabsf(dr->priority + 1.819f) < 1e-5f && frame < 0) {
                        frame = (int32_t)d;
                    }
                }
                if (bar >= 0) {
                    bar_pairs++;
                    order_ok += frame > bar;
                }
            }
        }
        CHECK(bar_pairs > 0 && order_ok == bar_pairs && bar_ok == bar_pairs,
              "%u pairs with the lure out drew the bar: an opaque 6-vertex triangle draw at -1.82 (%u), before the "
              "translucent gauge frame at -1.819 in every one (%u)", (unsigned)bar_pairs, (unsigned)bar_ok,
              (unsigned)order_ok);
    }
    sa1_shutdown(ctx);
}

/* ---- 6, 8, 10: E-102 ------------------------------------------------------------------------------------------- */
static void clock_on_cb(sa1g_world *w, void *user)
{
    (void)w;
    (void)user;
    sa1g_call_dc(SA1G_FN_CLOCK_ON);
}

static void bonus_cb(sa1g_world *w, void *user)
{
    (void)w;
    (void)user;
    ((void (*)(void *))sa1_fn(SA1_GFN_C(0x8C0D5064u), 0u))(NULL);    /* the lock-on kill's time bonus */
}

/* pairs until a TIME_UP / DEATH event of id (0: none within n) */
static uint32_t pairs_to_death(sa1_context *ctx, sa1_character_id id, uint32_t n, uint32_t *time_up)
{
    uint32_t k, c, i;
    sa1_event ev[64];
    *time_up = 0;
    for (k = 1; k <= n; k++) {
        if (tick(ctx, id, 0, 0.0f) != 0) {
            return 0;
        }
        while (sa1_poll_events(ctx, ev, 64u, &c) == SA1_OK && c > 0) {
            for (i = 0; i < c; i++) {
                if (ev[i].character == id && ev[i].type == SA1_EVENT_TIME_UP) {
                    *time_up = 1;
                }
                if (ev[i].character == id && ev[i].type == SA1_EVENT_DEATH) {
                    return k;
                }
            }
        }
    }
    return 0;
}

static void state_of_id(sa1_context *ctx, sa1_character_id id, sa1_state_ex *x)
{
    memset(x, 0, sizeof *x);
    x->struct_size = sizeof *x;
    sa1_character_state_ex(ctx, id, x);
}

static void test_gamma(const env *E)
{
    sa1_context *ctx = new_ctx(E, 1u << SA1_CHAR_GAMMA);
    sa1_character_id id;
    sa1_state_ex x;
    uint32_t k, d, tu, a, n = 0, held = 1, lowest = 0xFFFFFFFFu, highest = 0, red = 1, nred = 0;
    static const char *const own[5] = { "E102", "E102BEAM", "E102EFFECT", "E102TIME", "E102HIT" };
    printf("6. E-102's time limit (Final Egg act 1):\n");
    if (ctx == NULL || set_disc(ctx, 10, 0) != 0) {
        CHECK(0, "context");
        if (ctx)
            sa1_shutdown(ctx);
        return;
    }
    /* 8: his archives */
    for (a = 0; a < 16u; a++) {
        const char *nm = NULL;
        uint32_t c = 0;
        if (sa1_texture_archive(ctx, SA1_CHAR_GAMMA, a, &nm, &c) != SA1_OK) {
            break;
        }
        n++;
    }
    for (a = 0, k = 0; a < 5u; a++) {
        k += archive_named(ctx, SA1_CHAR_GAMMA, own[a]) == (int32_t)a;
    }
    CHECK(n == 8u && k == 5u && archive_named(ctx, SA1_CHAR_GAMMA, "CON_REGULAR") == 7 && g_arch_warn == 0,
          "8. E-102 has %u archives (his 5 at 0-4: %u, OBJ_REGULAR, EFF_REGULAR, CON_REGULAR 7); no \"more than N "
          "texture archives\" warning so far (%d)", (unsigned)n, (unsigned)k, g_arch_warn);
    /* the game's rule */
    id = stage_char(ctx, SA1_CHAR_GAMMA, 10, 0);
    if (!id || !hud_on(ctx, id, SA1_HUD_ALL, 1) || sa1_character_set_time(ctx, id, 0, 2, 0) != SA1_OK) {
        CHECK(0, "E-102");
        sa1_shutdown(ctx);
        return;
    }
    d = pairs_to_death(ctx, id, 150u, &tu);
    CHECK(d > 0 && d <= 100u && tu, "the game's rule from 00:02:00: SA1_EVENT_TIME_UP and SA1_EVENT_DEATH at pair %u",
          (unsigned)d);
    /* the respawn after it: 00:00:00 carried gives 03:00:00 */
    state_of_id(ctx, id, &x);
    {
        int32_t m = x.time_minutes, s = x.time_seconds, f = x.time_frames;
        sa1_character_id id2 = stage_char(ctx, SA1_CHAR_GAMMA, 10, 0);
        sa1_character_destroy(ctx, id);
        id = id2;
        if (!id || !hud_on(ctx, id, SA1_HUD_ALL, 1) || sa1_character_set_time(ctx, id, m, s, f) != SA1_OK ||
            sa1_character_set_lives(ctx, id, 3) != SA1_OK || tick(ctx, id, 0, 0.0f) != 0) {
            CHECK(0, "the respawn");
            sa1_shutdown(ctx);
            return;
        }
        state_of_id(ctx, id, &x);
        d = pairs_to_death(ctx, id, 100u, &tu);
        CHECK(m == 0 && s == 0 && f == 0 && x.time_minutes == 2 && x.time_seconds == 59 && x.time_frames == 58 &&
                  x.lives == 3 && d == 0 && !tu,
              "a respawn that carries the dead world's %02d:%02d:%02d starts at 03:00:00 (after one pair %02d:%02d:%02d), "
              "3 lives: no death in 100 pairs (no death loop)", (int)m, (int)s, (int)f, (int)x.time_minutes,
              (int)x.time_seconds, (int)x.time_frames);
        sa1_character_destroy(ctx, id);
    }
    /* SA1_COPT_E102_NO_TIME_LIMIT */
    id = stage_char(ctx, SA1_CHAR_GAMMA, 10, 0);
    if (!id || !hud_on(ctx, id, SA1_HUD_ALL, 1) || sa1_character_set_time(ctx, id, 0, 2, 0) != SA1_OK ||
        sa1_character_set_option(ctx, id, SA1_COPT_E102_NO_TIME_LIMIT, 1) != SA1_OK) {
        CHECK(0, "E-102 without the limit");
        sa1_shutdown(ctx);
        return;
    }
    tu = 0;
    d = 0;
    for (k = 0; k < 400u; k++) {
        uint32_t t1, c, i;
        sa1_event ev[64];
        sa1g_world *w;
        if (tick(ctx, id, 0, 0.0f) != 0) {
            break;
        }
        w = sa1_character_world(ctx, id);
        if (k == 100u) {
            sa1g_world_call(w, clock_on_cb, NULL);      /* game code starting the clock again (8C06132A's 8C08A5AE) */
        }
        if (k == 200u) {
            sa1g_world_call(w, bonus_cb, NULL);         /* a lock-on kill's time bonus (re-arms B_8C7A2EAA) */
        }
        while (sa1_poll_events(ctx, ev, 64u, &c) == SA1_OK && c > 0) {
            for (i = 0; i < c; i++) {
                tu |= ev[i].type == SA1_EVENT_TIME_UP;
                d |= ev[i].type == SA1_EVENT_DEATH;
            }
        }
        state_of_id(ctx, id, &x);
        t1 = (uint32_t)(x.time_minutes * 3600 + x.time_seconds * 60 + x.time_frames);
        lowest = t1 < lowest ? t1 : lowest;
        highest = t1 > highest ? t1 : highest;
        held &= x.clock_running == 0;
        /* 10: under 15 s the timer digits are red (8C08A7E0: g = |sin|, b 0); before the time bonus */
        if (k < 200u) {
            const sa1_screen *s = screen_of(ctx, id);
            uint32_t i2;
            for (i2 = 0; s != NULL && i2 < s->num_sprites; i2++) {
                if (s->sprites[i2].part == SA1_HUD_TIME) {
                    nred++;
                    red &= s->sprites[i2].color[0] == 255 && s->sprites[i2].color[2] == 0;
                }
            }
        }
    }
    CHECK(k == 400u && !tu && !d && held && lowest == 120u,
          "SA1_COPT_E102_NO_TIME_LIMIT from 00:02:00: no TIME_UP / DEATH in 400 pairs, the clock held (never below "
          "%u frames = 00:02:00, clock_running 0 every pair) through a game-code 8C08A5AE at pair 100 and the time bonus "
          "8C0D5064 at pair 200 (the time went up to %u frames)", (unsigned)lowest, (unsigned)highest);
    CHECK(nred > 0 && red, "10. E-102's timer under 15 s: every timer digit red (r 255, b 0; %u digits)", (unsigned)nred);
    sa1_shutdown(ctx);
}

/* ---- 12: F7's libsa1 half: the references' 2D draws ------------------------------------------------------------- */
static int arena_addr(uint32_t a)
{
    return a != 0 && ((a >> 24) & 31u) == SA1G_SEG_ARENA;
}

static uint32_t g_rd(const sa1g_world *w, uint32_t a)
{
    uint32_t v = 0;
    if (!arena_addr(a) || sa1g_world_rd32(w, a & ~3u, &v) != 0) {
        return 0;
    }
    return v;
}

/* the common columns of harness/src/t_sim.c d2d_common */
static void d2d_common(FILE *f, const sa1_cap_rec *r, char kind, char phase, uint32_t step, uint32_t seq, uint32_t exec,
                       uint32_t pri, uint32_t texlist)
{
    fprintf(f, "%c %c %u %u %d %d %08X %08X %08X %d %08X %08X %08X %08X %08X %08X %08X %04X %08X %08X %08X", kind, phase,
            (unsigned)step, (unsigned)seq, (int)r->frame, (int)r->sub, (unsigned)exec, 0u, (unsigned)r->obj, (int)r->n,
            (unsigned)pri, (unsigned)r->attr, (unsigned)texlist, (unsigned)r->cmat[0], (unsigned)r->cmat[1],
            (unsigned)r->cmat[2], (unsigned)r->cmat[3], (unsigned)(r->blend & 0xFFFFu), (unsigned)r->ctl3d,
            (unsigned)r->attr_and, (unsigned)r->attr_or);
}

static void d2d_record(FILE *f, const sa1g_world *w, const sa1_capture *cap, const sa1_cap_rec *r, char phase,
                       uint32_t step, uint32_t seq)
{
    uint32_t k;
    if (r->kind == SA1_CAP_SPRITE2D) {
        const uint8_t *p = sa1_capture_payload(cap, r, SA1_CAP_SPRITE2D_PAY), *ta;
        uint32_t tlist, texid, tex = 0, s;
        char name[32];
        if (p == NULL) {
            d2d_common(f, r, 'S', phase, step, seq, 0, 0, 0);
            fprintf(f, " -\n");
            return;
        }
        d2d_common(f, r, 'S', phase, step, seq, sa1_rd32(p + 0x38), sa1_rd32(p + 0x34), sa1_rd32(p + 0x3C));
        for (k = 0; k < 8u; k++) {
            fprintf(f, " %08X", (unsigned)sa1_rd32(p + 4u * k));
        }
        ta = p + 0x20;
        for (k = 0; k < 9u; k++) {
            fprintf(f, " %d", (int)(int16_t)sa1_rd16(ta + 2u * k));
        }
        fprintf(f, " %04X", (unsigned)sa1_rd16(ta + 18));
        tlist = sa1_rd32(p + 0x18);
        texid = (uint32_t)(int32_t)(int16_t)sa1_rd16(ta + 16);
        if (arena_addr(tlist) && (int32_t)texid >= 0 && texid < g_rd(w, tlist + 4u) && arena_addr(g_rd(w, tlist))) {
            tex = g_rd(w, tlist) + 12u * texid;
        }
        name[0] = 0;
        s = tex ? g_rd(w, tex) : 0u;
        if (arena_addr(s)) {
            for (k = 0; k < sizeof name - 1u; k++) {
                int ch = (int)w->ram[(s + k) & 0xFFFFFFu];
                if (!ch || !(isalnum(ch) || ch == '_' || ch == '-' || ch == '.')) {
                    break;
                }
                name[k] = (char)ch;
            }
            name[k] = 0;
        }
        fprintf(f, " %08X %s\n", (unsigned)tex, name[0] ? name : "-");
        return;
    }
    {
        const uint8_t *p = sa1_capture_payload(cap, r, SA1_CAP_POLY2D_HEAD), *pt, *pc, *pu;
        uint32_t num, hasc, hast;
        if (p == NULL) {
            d2d_common(f, r, 'P', phase, step, seq, 0, 0, 0);
            fprintf(f, " -\n");
            return;
        }
        num = sa1_rd32(p);
        hasc = sa1_rd32(p + 4);
        hast = sa1_rd32(p + 8);
        d2d_common(f, r, 'P', phase, step, seq, sa1_rd32(p + 0x14), sa1_rd32(p + 0x10), sa1_rd32(p + 0x18));
        fprintf(f, " %08X %08X %08X %u", (unsigned)g_rd(w, r->obj), (unsigned)g_rd(w, r->obj + 4u),
                (unsigned)g_rd(w, r->obj + 8u), (unsigned)sa1_rd32(p + 12));
        if (num > 64u) {
            num = 64u;
        }
        pt = p + SA1_CAP_POLY2D_HEAD;
        pc = pt + 8u * sa1_rd32(p);
        pu = pc + (hasc ? 4u * sa1_rd32(p) : 0u);
        for (k = 0; k < num; k++) {
            fprintf(f, " %08X,%08X", (unsigned)sa1_rd32(pt + 8u * k), (unsigned)sa1_rd32(pt + 8u * k + 4u));
            if (hasc) {
                fprintf(f, ",%08X", (unsigned)sa1_rd32(pc + 4u * k));
            } else {
                fprintf(f, ",-");
            }
            if (hast) {
                fprintf(f, ",%08X", (unsigned)sa1_rd32(pu + 4u * k));
            } else {
                fprintf(f, ",-");
            }
        }
        fprintf(f, "\n");
    }
}

/* the 2D records the last capture window added (0 when it recorded nothing new); writes them, returns the count */
static uint32_t d2d_window(FILE *f, const sa1g_world *w, sa1_capture *cap, uint32_t *total, char phase, uint32_t step)
{
    sa1_capture_stats st;
    const sa1_cap_rec *recs = NULL;
    uint32_t n, i, seq = 0;
    sa1_capture_get_stats(cap, &st);
    if (st.total == *total) {
        return 0;
    }
    *total = st.total;
    n = sa1_capture_records(cap, &recs);
    for (i = 0; i < n; i++) {
        if (recs[i].kind == SA1_CAP_SPRITE2D || recs[i].kind == SA1_CAP_POLY2D) {
            d2d_record(f, w, cap, &recs[i], phase, step, seq++);
        }
    }
    return seq;
}

/* one replay of reference t in a fresh world of ctx: with_pass = the HUD pass after every sub step 1 (its 2D draws and
 * the steps' written to f when f != NULL); out[i] = the world's record of step i; the reference comparison (decoded
 * fields) into *diff. Returns the steps run (t->n when no fault) */
static uint32_t replay_once(const env *E, sa1_context *ctx, const wr_trace *t, const char *name, int with_pass, FILE *f,
                            sa1_trace_rec *out, wr_diff *diff, uint32_t *draws, uint32_t *fdraws, uint32_t *passes)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1g_world *w;
    sa1_capture *cap = NULL;
    uint32_t i, k, total = 0, steps = 0;
    (void)E;
    *draws = *fdraws = *passes = 0;
    wr_diff_init(diff);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = t->h.chr;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_STAGE | SA1_CREATE_STAGE_TASKS;
    memcpy(d.position, t->h.start_pos, sizeof d.position);
    for (k = 0; k < 3u; k++) {
        d.angle[k] = (int32_t)t->h.start_ang[k];
    }
    d.level = t->h.level;
    d.act = t->h.act;
    if (sa1_character_create_ex(ctx, &d, &id) != SA1_OK || (w = sa1_character_world(ctx, id)) == NULL) {
        printf("    %s: create: %s\n", name, sa1_last_error(ctx));
        return 0;
    }
    wr_screen_of(w, &t->h);
    if (sa1_capture_attach(ctx, w, &cap) != SA1_OK) {
        printf("    %s: the world has no capture block\n", name);
        sa1_character_destroy(ctx, id);
        return 0;
    }
    sa1_capture_set_archives(cap, &ctx->chars[t->h.chr]);
    w->replay = 1;
    for (i = 0; i < t->n; i++) {
        const sa1_trace_rec *r = &t->r[i];
        wr_inject(w, r, INJ_FULL, 0);
        sa1_capture_open(cap);
        if (sa1g_world_step(w, (int32_t)r->sub) != SA1_OK) {
            sa1_capture_close(cap);
            printf("    %s: step %u: %s\n", name, (unsigned)i, w->fault.msg);
            break;
        }
        sa1_capture_close(cap);
        steps++;
        if (f != NULL) {
            *draws += d2d_window(f, w, cap, &total, 'S', r->step);
        }
        wr_record(w, r, &out[i]);
        wr_compare(r, &out[i], 0u, diff, (int)i);
        if (r->sub == 1 && with_pass) {
            sa1_capture_open(cap);
            if (sa1s_world_test_pass(w, SA1_HUD_RINGS | SA1_HUD_LIVES | SA1_HUD_TIME) != 0) {
                sa1_capture_close(cap);
                printf("    %s: the HUD pass after step %u: %s\n", name, (unsigned)i, w->fault.msg);
                break;
            }
            sa1_capture_close(cap);
            (*passes)++;
            if (f != NULL) {
                k = d2d_window(f, w, cap, &total, 'F', r->step);
                *draws += k;
                *fdraws += k;
            }
        }
    }
    sa1_capture_free(ctx, cap);
    sa1_character_destroy(ctx, id);
    return steps;
}

/* references whose replay differs from the reference with or without the HUD (known, not the pass's) */
static const char *known_replay_gap(const char *name)
{
    if (!strcmp(name, "hud_knux_rm3")) {
        return "libsa1's replay of hud_knux_rm3 leaves the reference at step 398 (macc, then position) and does not die "
               "at step 425 as the reference does (mode 9): the same in tests/replay_test.c V6 / V5 and without any HUD "
               "pass (Red Mountain act 3's own objects or kill zone; a world gap, not the HUD's)";
    }
    return NULL;
}

static void replay_2d(const env *E, const char *name)
{
    char path[1100], prefix[1100];
    wr_trace t;
    sa1_context *ctx;
    sa1ca_dump dump;
    FILE *f;
    wr_diff d1, d2;
    sa1_trace_rec *o1, *o2;
    uint32_t s1, s2, draws, fdraws, passes, n2, n3, n4;
    const char *gap = known_replay_gap(name);
    printf("12. %s: the 2D draws of the replay with the HUD pass:\n", name);
    snprintf(path, sizeof path, "%s/build/traces/ref/%s.bin", E->sadc, name);
    if (wr_trace_load(path, &t) != 0) {
        CHECK(0, "reference %s", path);
        return;
    }
    ctx = new_ctx(E, 1u << t.h.chr);
    snprintf(prefix, sizeof prefix, "%s/build/traces/land/L%02dA%d", E->sadc, (int)t.h.level, (int)t.h.act);
    memset(&dump, 0, sizeof dump);
    o1 = (sa1_trace_rec *)calloc(t.n + 1u, sizeof *o1);
    o2 = (sa1_trace_rec *)calloc(t.n + 1u, sizeof *o2);
    if (ctx == NULL || o1 == NULL || o2 == NULL || sa1ca_dump_load(ctx, prefix, &dump) != SA1_OK ||
        sa1_collision_set_static_ex(ctx, &dump.desc) != SA1_OK) {
        CHECK(0, "%s: context / landtable dump %s: %s", name, prefix, ctx ? sa1_last_error(ctx) : "-");
        if (ctx) {
            sa1ca_dump_free(ctx, &dump);
            sa1_shutdown(ctx);
        }
        free(o1);
        free(o2);
        wr_trace_free(&t);
        return;
    }
    snprintf(path, sizeof path, "%s/%s.lib.d2d.txt", E->out, name);
    f = fopen(path, "w");
    if (f == NULL) {
        CHECK(0, "cannot write %s", path);
    } else {
        fprintf(f, "# sa1 draw2d log v1 (libsa1 tests/hud_test.c): level %d act %d chr %d, trace %s, %u steps; phase F = the "
                "HUD pass (sa1g_hud_pass, every part) after each sub step 1\n", (int)t.h.level, (int)t.h.act,
                (int)t.h.chr, name, (unsigned)t.n);
    }
    s1 = replay_once(E, ctx, &t, name, 1, f, o1, &d1, &draws, &fdraws, &passes);
    if (f != NULL) {
        fclose(f);
    }
    s2 = replay_once(E, ctx, &t, name, 0, NULL, o2, &d2, &n2, &n3, &n4);
    CHECK(s1 == t.n && s2 == t.n && memcmp(o1, o2, (size_t)t.n * sizeof *o1) == 0,
          "%s: %u steps replayed with %u HUD passes and %u without: every record identical to the bit (the pass changes "
          "nothing the trace holds); %u 2D draws (%u of the pass) -> %s", name, (unsigned)s1, (unsigned)passes,
          (unsigned)s2, (unsigned)draws, (unsigned)fdraws, path);
    if (gap != NULL) {
        printf("  known %s: %u of %u records differ from the reference (decoded fields) with the pass, %u without: %s\n",
               name, (unsigned)d1.nsteps_bad, (unsigned)t.n, (unsigned)d2.nsteps_bad, gap);
    } else {
        CHECK(d1.nsteps_bad == 0, "%s: the replay with the pass equals the reference in every record's decoded fields "
              "(%u differ)", name, (unsigned)d1.nsteps_bad);
        if (d1.nsteps_bad) {
            wr_diff_print(&d1, 8);
        }
    }
    free(o1);
    free(o2);
    sa1ca_dump_free(ctx, &dump);
    sa1_shutdown(ctx);
    wr_trace_free(&t);
}

static int part_on(const env *E, uint32_t p)
{
    return (E->parts & (1u << p)) != 0;
}

int main(int argc, char **argv)
{
    static const char *const refs[] = { "hud_sonic_ec1", "hud_tails_sh1", "hud_knux_rm3", "hud_amy_tp2", "hud_big_ec3",
                                        "hud_gamma_fe1", "big_drown_ec3" };
    env E;
    int i;
    uint32_t k;
    (void)wr_replay;
    (void)wr_result_print;
    (void)wr_trace_write;
    (void)wr_rand_ahead;
    ga_args(&argc, argv);
    memset(&E, 0, sizeof E);
    E.gdi = getenv("SA1_TEST_GDI");
    E.out = "build/hud";
    E.parts = 0xFFFFFFFFu;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--part") && i + 1 < argc) {
            const char *p = argv[++i];
            E.parts = 0;
            while (*p) {
                char *e;
                long v = strtol(p, &e, 10);
                if (e == p) {
                    break;
                }
                if (v >= 0 && v < 32) {
                    E.parts |= 1u << v;
                }
                p = *e == ',' ? e + 1 : e;
            }
        } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            E.out = argv[++i];
        } else if (E.sadc == NULL) {
            E.sadc = argv[i];
        } else {
            E.gdi = argv[i];
        }
    }
    if (E.sadc == NULL || E.gdi == NULL || !*E.gdi) {
        printf("usage: hud_test [--part LIST] [--out DIR] SADC [GDI]\n");
        return 2;
    }
    printf("libsa1 %s HUD / screen test: %d-bit, game code: %s\n", sa1_version_string(), (int)(sizeof(void *) * 8),
           sa1g_game_info());
    if (!sa1g_game_ready()) {
        printf("FAIL: the step's game code is not linked\n");
        return 1;
    }
    if (part_on(&E, 1) || part_on(&E, 2) || part_on(&E, 7) || part_on(&E, 8) || part_on(&E, 10)) {
        test_sonic(&E);
    }
    if (part_on(&E, 3) || part_on(&E, 11)) {
        test_ram(&E, SA1_CHAR_SONIC, 1, 0, 0);
        test_ram(&E, SA1_CHAR_BIG, 1, 2, 0);
        test_ram(&E, SA1_CHAR_SONIC, 1, 0, 1);
    }
    if (part_on(&E, 4)) {
        test_replay_safety(&E);
    }
    if (part_on(&E, 5) || part_on(&E, 9)) {
        test_big(&E);
    }
    if (part_on(&E, 6) || part_on(&E, 8) || part_on(&E, 10)) {
        test_gamma(&E);
    }
    if (part_on(&E, 12)) {
        for (k = 0; k < sizeof refs / sizeof refs[0]; k++) {
            replay_2d(&E, refs[k]);
        }
    }
    CHECK(g_arch_warn == 0, "8. no character logged \"more than N texture archives\" (%d)", g_arch_warn);
    printf("hud_test: %s (%d failed, %d skipped)\n", g_fail ? "FAIL" : "PASS", g_fail, g_skip);
    return g_fail ? 1 : 0;
}
