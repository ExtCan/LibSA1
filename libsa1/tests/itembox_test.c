/*
 * itembox_test.c - the air item box and what it gives (round 0.9.2, 092-lib-host; sadc docs/research/gsa1_090_faults.md
 * F2 with its Review R.3.1-R.3.3, gsa1_090_plan.md 6.4; built from the research scratch
 * sadc/build/research/faults/faults_repro.c parts B-D, with LIB's own headers and archive: tests/build_itembox.py).
 *
 *   itembox_test [--only NAME[,NAME]] [--seeds N] [--verbose] GDI     (GDI: or $SA1_TEST_GDI)
 *
 * Every world: native coordinates (SA1 units, y up), a 1200 x 1200 SOLID | DIG floor at y 0 (what G-SA1 sends for grass,
 * dirt, sand ...), a character made with SA1_CREATE_WORLD, one pair (1/30 s) per sa1_character_tick, no host camera.
 *
 *   boxes   an air box made by the game's own spawner 8C05D9C0 (what Knuckles' dig outcome 5, 8C0C8534, calls) at the
 *           character, Knuckles and Sonic, items 0..8: no fault and no UNIMPLEMENTED event; speed shoes: the task 8C07D2DA
 *           (8C07D274 after its first exec);
 *           invincibility: item 0x8000, cleared by the timer 8C04430A after 1,260 frames; 5 / 10 rings; random rings: +1, 5,
 *           10, 15, 20 or 40 (D_8C154C40); barrier: item 1; 1-up: lives + 1; magnetic barrier: item 2; the box's model
 *           drawn while it is whole (its display 8C05DC60, 8C057460 -> 8C03700C, captured into sa1_character_render with
 *           OBJ_REGULAR's textures); the box task gone after its mode 4 (about 44 steps in the reference airbox_ec1)
 *   screen  (with boxes) SA1_COPT_HUD_PARTS = SA1_HUD_CHARACTER: the item icon popup of the item icon list task 8C05E0CE
 *           (exec 8C05E14E, which calls its display 8C05E33C -> 8C05E3A0 -> njDrawSprite2D) reaches sa1_character_screen
 *           with its texture resolved (D_8C19F530, OBJ_REGULAR)
 *   digs    Knuckles with the Shovel Claw digs at (50, 0, 50), seeds 1..N (SA1_CREATE_SEED): about 5 % of the outcomes are
 *           item boxes (15 of 300 with the default seeds), none faults
 *   drain   30 boxes (5 rings each) in one Sonic world, each made after the previous item's icon has left the screen (the
 *           list task's drain: slide in (mode 1), 60 frames held (mode 2), 60 frames shrinking (mode 3), then the list is
 *           emptied): B_8C78C394.num is back to 0 between boxes and never above 1, the jump panel list B_8C78C4E0 behind it
 *           is unchanged, 150 rings given. The game's list has no bound (8C05E0F8): boxes faster than the drain overflow in
 *           the original too (review R.3.3), so the spacing is part of the test; the measured drain is printed.
 *   bomb    the bomb (item 7): the sphere B_8C78C390 starts at 10 at the box (8C05E582), grows by 10 every step and ends
 *           (-1) past 500 through the icon list task (8C05E59A; it runs after the box in list 3, so the break step
 *           already ends at 20); host ENEMY proxies inside it take the game's hit (their enemy damage check 8C0489E4 ->
 *           8C05E5C2), one HIT each per bomb (SA1_ATTACK_OTHER, level 3, at the step the sphere reaches them); a TARGET
 *           and a DISABLED ENEMY are not hit
 *
 * Prints "DETERMINISM_ITEMBOX <drain arena> <bomb arena> <bomb events>" (build_itembox.py compares x86 / x64).
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_world.h"
#include "sa1_api_world.h"
#include "test_util.h"
#include "guard_alloc.c"    /* SA1_GUARD_ALLOC=1 or --guard-alloc: every library heap block against a guard page */

static int g_fail, g_checks, g_verbose;
static uint32_t g_seeds = 300;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (cond) {                                                            \
            printf("  ok    ");                                                \
        } else {                                                               \
            printf("  FAIL  ");                                                \
            g_fail++;                                                          \
        }                                                                      \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
    } while (0)

#define ADDR_TASK_LISTS   0x8C79F04Cu
#define ADDR_ICONS        0x8C78C394u   /* B_8C78C394: {mode, num, timer, entry[20] (16 B)} */
#define ADDR_JUMPPANELS   0x8C78C4E0u   /* B_8C78C4E0: the jump panel task list, right behind the icon list */
#define ADDR_BOMB_C       0x8C78C384u   /* NJS_POINT3 B_8C78C384: the bomb sphere's centre */
#define ADDR_BOMB_R       0x8C78C390u   /* Float B_8C78C390: its radius, < 0 = off */
#define EXEC_ITEMBOXAIR   0x8C05D9F6u   /* objexec_O_ItemBoxAir */
#define EXEC_ICONS        0x8C05E14Eu   /* the item icon list task's exec (8C05E0CE creates it) */
#define EXEC_SPEEDSHOES   0x8C07D2DAu   /* the speed shoes task (8C07D3A0): its first exec ... */
#define EXEC_SPEEDTIMER   0x8C07D274u   /* ... installs this one (the timer; dest 8C07D2A2 restores the physics) */
#define EXEC_DIG_SE       0x8C0C84FCu   /* the dig's SE timer task (8C0C8534) */
#define FN_SPAWN_AIRBOX   0x8C05D9C0u   /* void (NJS_POINT3 *pos, Sint32 item) */
#define FLAG_SHOVEL_CLAW  0x119u
#define FLAG_GLOVES       0x11Au

static const char *k_items[9] = { "speed shoes", "invincibility", "5 rings", "10 rings", "random rings", "barrier",
                                  "1-up", "bomb", "magnetic barrier" };

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

/* ---- guest memory (through the world's own accessors) ------------------------------------------------------------ */
static uint32_t rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (w == NULL || sa1g_world_rd32(w, g & ~3u, &v) != 0) {
        return 0;
    }
    return v;
}

static uint32_t rd8(const sa1g_world *w, uint32_t g)
{
    return (rd32(w, g & ~3u) >> (8u * (g & 3u))) & 0xFFu;
}

static float rdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = rd32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static uint32_t crc_guest(const sa1g_world *w, uint32_t g, uint32_t n)
{
    uint32_t crc = 0, i;
    for (i = 0; i < n; i += 4u) {
        uint32_t v = rd32(w, g + i);
        crc = tu_crc32(crc, (const uint8_t *)&v, 4);
    }
    return crc;
}

static uint32_t arena_crc(const sa1g_world *w)
{
    return w != NULL && w->ram != NULL ? tu_crc32(0, w->ram, SA1G_ARENA_SIZE) : 0u;
}

/* the tasks of all levels whose exec is `exec` (the top-level lists, as the executor walks them) */
static uint32_t find_tasks(const sa1g_world *w, uint32_t exec, uint32_t *first)
{
    uint32_t lvl, n = 0;
    if (first) {
        *first = 0;
    }
    for (lvl = 0; lvl < 8; lvl++) {
        uint32_t tp = rd32(w, ADDR_TASK_LISTS + 4u * lvl), guard = 0;
        while (tp != 0 && guard++ < 0x2000u) {
            if ((rd32(w, tp + 0x10u) & 0x1FFFFFFFu) == (exec & 0x1FFFFFFFu)) {
                if (first && n == 0) {
                    *first = tp;
                }
                n++;
            }
            tp = rd32(w, tp);
        }
    }
    return n;
}

/* ---- context, characters ----------------------------------------------------------------------------------------- */
static void quad(sa1_collision_tri *t, float x0, float x1, float z0, float z1, float y, uint32_t s)
{
    float q[4][3] = { { x0, y, z0 }, { x0, y, z1 }, { x1, y, z1 }, { x1, y, z0 } };
    memcpy(t[0].v[0], q[0], 12);
    memcpy(t[0].v[1], q[1], 12);
    memcpy(t[0].v[2], q[2], 12);
    t[0].surface = s;
    memcpy(t[1].v[0], q[0], 12);
    memcpy(t[1].v[1], q[2], 12);
    memcpy(t[1].v[2], q[3], 12);
    t[1].surface = s;
}

static sa1_context *make_ctx(const char *gdi)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    sa1_collision_tri tris[2];
    sa1_status st;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = g_verbose ? SA1_LOG_DEBUG : SA1_LOG_WARN;
    ga_host(&h);
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);      /* SA1 units, y up */
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_KNUCKLES);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_WORLD_IMAGES;
    st = sa1_load_disc(ctx, gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    quad(tris, -600.0f, 600.0f, -600.0f, 600.0f, 0.0f, SA1_SURFACE_SOLID | SA1_SURFACE_DIG);
    st = sa1_collision_set_static(ctx, tris, 2);
    if (SA1_FAILED(st)) {
        printf("sa1_collision_set_static: %s\n", sa1_status_string(st));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static sa1_character_id mk(sa1_context *ctx, int32_t kind, float x, float y, float z, uint32_t seed, int32_t rings)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | (seed ? SA1_CREATE_SEED : 0u) |
              (rings >= 0 ? SA1_CREATE_START_RINGS : 0u);
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    d.seed = seed;
    d.start_rings = rings;
    st = sa1_character_create_ex(ctx, &d, &id);
    if (SA1_FAILED(st)) {
        printf("    create %s: %s: %s\n", sa1_character_name(kind), sa1_status_string(st), sa1_last_error(ctx));
        return 0;
    }
    return id;
}

/* an event flag (one byte each from 0x8C799920), as G-SA1's Session::set_event_flags writes the upgrades */
static void set_flag(sa1_context *ctx, sa1_character_id id, uint32_t f)
{
    sa1g_world *w = sa1_character_world(ctx, id);
    uint32_t a = 0x8C799920u + f, word = a & ~3u, v = 0, sh = 8u * (a & 3u);
    if (w && !sa1g_world_rd32(w, word, &v)) {
        sa1g_world_wr32(w, word, (v & ~(0xFFu << sh)) | (1u << sh));
    }
}

static sa1_status pair(sa1_context *ctx, sa1_character_id id, uint32_t buttons)
{
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.buttons = buttons;
    return sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
}

static void state_of(sa1_context *ctx, sa1_character_id id, sa1_state_ex *s)
{
    memset(s, 0, sizeof *s);
    s->struct_size = sizeof *s;
    sa1_character_state_ex(ctx, id, s);
}

/* game events of the context: the count of a type since the last call (the queue is drained) */
static uint32_t drain_events(sa1_context *ctx, uint32_t type)
{
    sa1_event ev[256];
    uint32_t got = 0, n = 0, i;
    do {
        got = 0;
        sa1_poll_events(ctx, ev, 256, &got);
        for (i = 0; i < got; i++) {
            n += ev[i].type == type;
        }
    } while (got == 256);
    return n;
}

/* ---- the air box (8C05D9C0 at player 0's position) ---------------------------------------------------------------- */
static void spawn_cb(sa1g_world *w, void *user)
{
    int32_t item = *(const int32_t *)user;
    sa1g_player_ptrs pp;
    uint8_t *twp;
    sa1g_world_player(w, &pp);
    twp = (uint8_t *)sa1g_world_ptr(w, pp.twp);
    ((void (*)(void *, int32_t))sa1_fn(SA1_GFN_C(FN_SPAWN_AIRBOX), 0u))(twp + 0x20, item);
}

static sa1_status spawn_box(sa1g_world *w, int32_t item)
{
    return sa1g_world_call(w, spawn_cb, &item);
}

/* the box's model in the last pair's render: triangles of the game's model renderer drawn with another archive than the
 * character's own (OBJ_REGULAR's: the box's textures are on the stage-start texlist 8C099500 sets), not the blob shadow */
static uint32_t box_tris(sa1_context *ctx, sa1_character_id id)
{
    const sa1_render *r = NULL;
    uint32_t i, n = 0;
    if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) {
        return 0;
    }
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        if (d->prim == SA1_PRIM_TRIANGLES && d->archive > 0 && !(d->flags & SA1_DRAW_SHADOW) &&
            d->source == SA1_SOURCE_CHARACTER) {
            n += d->count;
        }
    }
    return n;
}

/* the item icon popup on the screen: sprites drawn by the icon list task, and how many of them have their texture */
static uint32_t icon_sprites(sa1_context *ctx, sa1_character_id id, uint32_t *textured)
{
    const sa1_screen *s = NULL;
    uint32_t i, n = 0;
    *textured = 0;
    if (sa1_character_screen(ctx, id, &s) != SA1_OK || s == NULL) {
        return 0;
    }
    for (i = 0; i < s->num_draws; i++) {
        const sa1_screen_draw *d = &s->draws[i];
        if (d->prim == SA1_PRIM_SPRITES && (d->source & 0x1FFFFFFFu) == (EXEC_ICONS & 0x1FFFFFFFu)) {
            n += d->count;
            if (d->archive >= 0 && d->texture >= 0 && !(d->flags & SA1_DRAW_NO_TEXTURE)) {
                *textured += d->count;
            }
        }
    }
    return n;
}

/* ---- boxes (with screen) ------------------------------------------------------------------------------------------ */
typedef struct box_run {
    sa1_status st;
    uint32_t fault_pair, broke_pair, gone_pair, unimpl, tris0, tris_box, tris_break, speed_tasks, icons, icons_textured;
    uint32_t inv_set, inv_clear;
    sa1_state_ex s0, s1;
} box_run;

static void box_once(sa1_context *ctx, int32_t chr, int32_t item, box_run *o)
{
    sa1_character_id id = mk(ctx, chr, 0.0f, 10.0f, 0.0f, 0, 5);
    sa1g_world *w = sa1_character_world(ctx, id);
    uint32_t i, box = 0, n, seen = 0, prev_mode = 0, limit = item == 1 ? 760u : 60u;
    memset(o, 0, sizeof *o);
    o->st = SA1_ERR_INVALID_ARGUMENT;
    if (id == 0 || w == NULL) {
        return;
    }
    sa1_character_set_option(ctx, id, SA1_COPT_HUD_PARTS, SA1_HUD_CHARACTER);
    for (i = 0; i < 20; i++) {
        pair(ctx, id, 0);
    }
    drain_events(ctx, 0);
    state_of(ctx, id, &o->s0);
    o->tris0 = box_tris(ctx, id);
    o->st = spawn_box(w, item);
    for (i = 1; i <= limit && o->st == SA1_OK; i++) {
        uint32_t t, tx = 0, ic, it;
        o->st = pair(ctx, id, 0);
        if (o->st != SA1_OK) {
            o->fault_pair = i;
            printf("    tick %u: %s: %s\n", (unsigned)i, sa1_status_string(o->st), sa1_last_error(ctx));
            break;
        }
        o->unimpl += drain_events(ctx, SA1_EVENT_UNIMPLEMENTED);
        n = find_tasks(w, EXEC_ITEMBOXAIR, &box);
        if (n != 0) {
            uint32_t mode = rd8(w, rd32(w, box + 0x20u));
            seen = 1;
            if (mode >= 2 && o->broke_pair == 0) {
                o->broke_pair = i;
            }
            t = box_tris(ctx, id);
            if (mode <= 1 && i >= 2 && t > o->tris_box) {
                o->tris_box = t;
            }
            if (prev_mode >= 2 && t > o->tris_break) {
                o->tris_break = t;      /* information: the breaking box (drawn in modes 2-4) */
            }
            prev_mode = mode;
        } else if (seen && o->gone_pair == 0) {
            o->gone_pair = i;
        }
        t = find_tasks(w, EXEC_SPEEDSHOES, NULL) + find_tasks(w, EXEC_SPEEDTIMER, NULL);
        if (t > o->speed_tasks) {
            o->speed_tasks = t;
        }
        ic = icon_sprites(ctx, id, &tx);
        if (ic > o->icons) {
            o->icons = ic;
            o->icons_textured = tx;
        }
        state_of(ctx, id, &o->s1);
        it = o->s1.item;
        if ((it & 0x8000u) && o->inv_set == 0) {
            o->inv_set = i;
        }
        if (o->inv_set != 0 && !(it & 0x8000u) && o->inv_clear == 0) {
            o->inv_clear = i;
            if (o->gone_pair != 0) {
                break;
            }
        }
    }
    state_of(ctx, id, &o->s1);
    sa1_character_destroy(ctx, id);
    drain_events(ctx, 0);
}

static void t_boxes(sa1_context *ctx, int screen)
{
    static const int32_t chars[2] = { SA1_CHAR_KNUCKLES, SA1_CHAR_SONIC };
    static const int32_t rr[6] = { 1, 5, 10, 15, 20, 40 };
    uint32_t c;
    printf("\n== boxes: an air box from the game's spawner 8C05D9C0 at the character, items 0..8 (Knuckles, Sonic)\n");
    for (c = 0; c < 2; c++) {
        int32_t item;
        for (item = 0; item <= 8; item++) {
            const char *nm = sa1_character_name(chars[c]);
            box_run o;
            int32_t dr;
            int ok_item = 0, k;
            box_once(ctx, chars[c], item, &o);
            dr = (int32_t)o.s1.rings - (int32_t)o.s0.rings;
            switch (item) {
            case 0: ok_item = o.speed_tasks >= 1; break;
            case 1: ok_item = o.inv_set != 0 && o.inv_clear != 0; break;
            case 2: ok_item = dr == 5; break;
            case 3: ok_item = dr == 10; break;
            case 4:
                for (k = 0; k < 6; k++) {
                    ok_item |= dr == rr[k];
                }
                break;
            case 5: ok_item = (o.s1.item & 1u) != 0; break;
            case 6: ok_item = o.s1.lives == o.s0.lives + 1; break;
            case 7: ok_item = dr == 0 && o.s1.lives == o.s0.lives; break;     /* the sphere: part bomb */
            case 8: ok_item = (o.s1.item & 2u) != 0; break;
            }
            CHECK(o.st == SA1_OK && o.unimpl == 0 && o.broke_pair != 0 && ok_item,
                  "%-8s item %d (%s): %s, broken at pair %u, rings %d -> %d, lives %d -> %d, item 0x%04X, speed tasks %u, "
                  "UNIMPLEMENTED %u", nm, (int)item, k_items[item], sa1_status_string(o.st), (unsigned)o.broke_pair,
                  (int)o.s0.rings, (int)o.s1.rings, (int)o.s0.lives, (int)o.s1.lives, (unsigned)o.s1.item,
                  (unsigned)o.speed_tasks, (unsigned)o.unimpl);
            if (item == 1) {
                uint32_t frames = 2u * (o.inv_clear - o.inv_set);
                CHECK(o.inv_set != 0 && o.inv_clear != 0 && frames >= 1256u && frames <= 1264u,
                      "%-8s invincibility: item 0x8000 from pair %u to pair %u: %u frames (the timer 8C04430A: 1,260)", nm,
                      (unsigned)o.inv_set, (unsigned)o.inv_clear, (unsigned)frames);
            }
            CHECK(o.gone_pair != 0 && o.gone_pair > o.broke_pair && o.gone_pair <= 30u,
                  "%-8s item %d: the box task gone at pair %u (steps %u-%u; broken at pair %u)", nm, (int)item,
                  (unsigned)o.gone_pair, (unsigned)(2u * o.gone_pair - 1u), (unsigned)(2u * o.gone_pair),
                  (unsigned)o.broke_pair);
            CHECK(o.tris_box > o.tris0, "%-8s item %d: the whole box drawn (8C05DC60): %u model triangles of OBJ_REGULAR "
                  "while it lives, %u before (breaking, modes 2-4: up to %u; its fading shell node is drawn with "
                  "8C6215D6, which the capture records but does not walk)", nm, (int)item, (unsigned)o.tris_box,
                  (unsigned)o.tris0, (unsigned)o.tris_break);
            if (screen) {
                CHECK(o.icons >= 1 && o.icons_textured == o.icons,
                      "%-8s item %d: the item icon on the screen (SA1_HUD_CHARACTER, source 8C05E14E): %u sprite(s), %u "
                      "textured", nm, (int)item, (unsigned)o.icons, (unsigned)o.icons_textured);
            }
        }
    }
}

/* ---- digs ---------------------------------------------------------------------------------------------------------- */
static void t_digs(sa1_context *ctx)
{
    uint32_t seed, i, n_dug = 0, n_none = 0, n_rings = 0, n_obj = 0, n_box = 0, n_box_fault = 0, n_other_fault = 0;
    printf("\n== digs: Knuckles digs (action + jump, Shovel Claw, SOLID|DIG floor) at (50, 0, 50), seeds 1..%u\n",
           (unsigned)g_seeds);
    for (seed = 1; seed <= g_seeds; seed++) {
        sa1_character_id id = mk(ctx, SA1_CHAR_KNUCKLES, 50.0f, 10.0f, 50.0f, seed, 0);
        sa1g_world *w = sa1_character_world(ctx, id);
        sa1_status st = SA1_OK;
        uint32_t se = 0, box = 0, seen_box = 0, after = 0, dug = 0;
        int32_t box_item = -1;
        if (id == 0 || w == NULL) {
            continue;
        }
        set_flag(ctx, id, FLAG_SHOVEL_CLAW);
        set_flag(ctx, id, FLAG_GLOVES);
        for (i = 0; i < 20 && st == SA1_OK; i++) {
            st = pair(ctx, id, 0);
        }
        if (st == SA1_OK) {
            st = pair(ctx, id, SA1_BUTTON_ACTION | SA1_BUTTON_JUMP);
        }
        for (i = 0; i < 240 && st == SA1_OK; i++) {
            sa1g_player_ptrs pp;
            uint32_t t = 0;
            st = pair(ctx, id, 0);
            sa1g_world_player(w, &pp);
            if (pp.twp != 0 && (int8_t)rd8(w, pp.twp) == 19) {
                dug = 1;
            }
            if (!se && find_tasks(w, EXEC_DIG_SE, &t) && t) {
                se = rd32(w, rd32(w, t + 0x20u) + 0x14u) & 0xFFFFu;
            }
            if (find_tasks(w, EXEC_ITEMBOXAIR, &box) && !seen_box) {
                seen_box = 1;
                box_item = (int32_t)rdf(w, rd32(w, box + 0x20u) + 0x2Cu);
            }
            if (se && ++after > 60) {
                break;
            }
        }
        n_dug += dug;
        n_none += se == 0x480;
        n_rings += se == 0x481;
        n_obj += se == 0x482;
        if (seen_box) {
            n_box++;
            n_box_fault += st != SA1_OK;
            if (st != SA1_OK || g_verbose) {
                printf("    seed %3u: item box dug (item %d, %s): %s%s%s\n", (unsigned)seed, (int)box_item,
                       box_item >= 0 && box_item <= 8 ? k_items[box_item] : "?", sa1_status_string(st),
                       st == SA1_OK ? "" : ": ", st == SA1_OK ? "" : sa1_last_error(ctx));
            }
        } else if (st != SA1_OK) {
            n_other_fault++;
            printf("    seed %3u: fault without an item box (SE %03X): %s\n", (unsigned)seed, (unsigned)se,
                   sa1_last_error(ctx));
        }
        sa1_character_destroy(ctx, id);
        drain_events(ctx, 0);
    }
    printf("    digs %u of %u runs; outcomes by SE: nothing (0x480) %u, rings (0x481) %u, object (0x482) %u\n",
           (unsigned)n_dug, (unsigned)g_seeds, (unsigned)n_none, (unsigned)n_rings, (unsigned)n_obj);
    CHECK(n_box_fault == 0 && n_other_fault == 0 && n_box >= 1 && (g_seeds != 300u || n_box == 15u),
          "%u item boxes dug (15 with the default 300 seeds), %u of them faulted, %u other faults", (unsigned)n_box,
          (unsigned)n_box_fault, (unsigned)n_other_fault);
}

/* ---- per-step records (hostobj_post: after the executor and the CCL pass of every step) ---------------------------- */
#define SREC_MAX 4096u
typedef struct srec {
    uint32_t step;
    float r;
    uint32_t mode, num;
} srec;

static srec g_srec[SREC_MAX];
static uint32_t g_nsrec;
static sa1g_step_fn g_post_orig;

static void rec_post(sa1g_world *w, int32_t sub, void *user)
{
    if (g_nsrec < SREC_MAX) {
        srec *r = &g_srec[g_nsrec++];
        r->step = (uint32_t)w->steps;
        r->r = rdf(w, ADDR_BOMB_R);
        r->mode = rd32(w, ADDR_ICONS);
        r->num = rd32(w, ADDR_ICONS + 4u);
    }
    if (g_post_orig != NULL) {
        g_post_orig(w, sub, user);
    }
}

static void rec_attach(sa1g_world *w)
{
    g_nsrec = 0;
    g_post_orig = w->hostobj_post;
    w->hostobj_post = rec_post;
}

/* ---- drain --------------------------------------------------------------------------------------------------------- */
static uint32_t g_det_drain;

static void t_drain(sa1_context *ctx)
{
    sa1_character_id id = mk(ctx, SA1_CHAR_SONIC, 0.0f, 10.0f, 0.0f, 0, 5);
    sa1g_world *w = sa1_character_world(ctx, id);
    sa1_status st = SA1_OK;
    sa1_state_ex s0, s1;
    uint32_t b, i, crc0, max_num = 0, not_drained = 0, drain_min = 0xFFFFFFFFu, drain_max = 0, boxes = 0;
    printf("\n== drain: 30 boxes in one Sonic world, each after the previous icon has left (B_8C78C394)\n");
    if (id == 0 || w == NULL) {
        CHECK(0, "Sonic");
        return;
    }
    for (i = 0; i < 20; i++) {
        pair(ctx, id, 0);
    }
    state_of(ctx, id, &s0);
    crc0 = crc_guest(w, ADDR_JUMPPANELS, 0x100u);
    printf("    after boot: icon list mode %u num %u, bomb radius %.1f\n", (unsigned)rd32(w, ADDR_ICONS),
           (unsigned)rd32(w, ADDR_ICONS + 4u), (double)rdf(w, ADDR_BOMB_R));
    for (b = 0; b < 30 && st == SA1_OK; b++) {
        uint32_t first = 0, drained = 0;
        int32_t item = 2;
        st = spawn_box(w, item);
        for (i = 1; i <= 300 && st == SA1_OK; i++) {
            uint32_t num, mode;
            st = pair(ctx, id, 0);
            num = rd32(w, ADDR_ICONS + 4u);
            mode = rd32(w, ADDR_ICONS);
            max_num = num > max_num ? num : max_num;
            if (num != 0 && first == 0) {
                first = i;
            }
            if (first != 0 && num == 0 && mode == 0 && find_tasks(w, EXEC_ITEMBOXAIR, NULL) == 0) {
                drained = i;
                break;
            }
        }
        if (st != SA1_OK) {
            printf("    box %u: %s: %s\n", (unsigned)b, sa1_status_string(st), sa1_last_error(ctx));
            break;
        }
        boxes++;
        if (drained == 0 || first == 0) {
            not_drained++;
            if (not_drained <= 3) {
                printf("    box %u: the icon list did not drain within 300 pairs (num %u, mode %u)\n", (unsigned)b,
                       (unsigned)rd32(w, ADDR_ICONS + 4u), (unsigned)rd32(w, ADDR_ICONS));
            }
        } else {
            uint32_t d = drained - first;
            drain_min = d < drain_min ? d : drain_min;
            drain_max = d > drain_max ? d : drain_max;
        }
        for (i = 0; i < 5 && st == SA1_OK; i++) {
            st = pair(ctx, id, 0);      /* more than one full drain apart (review R.3.3) */
        }
    }
    state_of(ctx, id, &s1);
    printf("    the drain (from the icon's arrival to an empty list): %u..%u pairs (%u..%u steps)\n",
           drain_min == 0xFFFFFFFFu ? 0u : (unsigned)drain_min, (unsigned)drain_max,
           drain_min == 0xFFFFFFFFu ? 0u : (unsigned)(2u * drain_min), (unsigned)(2u * drain_max));
    CHECK(st == SA1_OK && boxes == 30u, "30 boxes, no fault (%s)", sa1_status_string(st));
    CHECK(not_drained == 0 && max_num <= 1u, "the icon list back to 0 after every box (%u not drained), num at most %u",
          (unsigned)not_drained, (unsigned)max_num);
    CHECK(crc_guest(w, ADDR_JUMPPANELS, 0x100u) == crc0, "B_8C78C4E0 (the jump panel list behind the icon list) unchanged");
    CHECK((int32_t)s1.rings == (int32_t)s0.rings + 150 && s1.lives == s0.lives + 1,
          "150 rings given: %d -> %d, the 1-up at 100: lives %d -> %d", (int)s0.rings, (int)s1.rings, (int)s0.lives,
          (int)s1.lives);
    g_det_drain = arena_crc(w);
    sa1_character_destroy(ctx, id);
    drain_events(ctx, 0);
}

/* ---- bomb ---------------------------------------------------------------------------------------------------------- */
static uint32_t g_det_bomb, g_det_bomb_ev;

typedef struct bomb_obj {
    const char *what;
    uint32_t kind, flags;
    float dx, dz;
    int expect_hit;
    uint32_t id, hits, hit_step, attack, level;
    float d;
} bomb_obj;

static void t_bomb(sa1_context *ctx)
{
    bomb_obj ob[4] = {
        { "ENEMY 60 units away", SA1_HOSTOBJ_ENEMY, 0, 60.0f, 0.0f, 1, 0, 0, 0, 0, 0, 0.0f },
        { "ENEMY 200 units away", SA1_HOSTOBJ_ENEMY, 0, 0.0f, 200.0f, 1, 0, 0, 0, 0, 0, 0.0f },
        { "TARGET 60 units away", SA1_HOSTOBJ_TARGET, 0, -60.0f, 0.0f, 0, 0, 0, 0, 0, 0, 0.0f },
        { "DISABLED ENEMY 120 units away", SA1_HOSTOBJ_ENEMY, SA1_HOSTOBJ_DISABLED, 0.0f, -120.0f, 0, 0, 0, 0, 0, 0, 0.0f },
    };
    sa1_character_id id = mk(ctx, SA1_CHAR_SONIC, 0.0f, 10.0f, 0.0f, 0, 5);
    sa1g_world *w = sa1_character_world(ctx, id);
    sa1_state_ex s;
    sa1_hostobj_event ev[256];
    sa1_status st = SA1_OK;
    uint32_t i, k, got, start = 0, end_step = 0, bad_inc = 0, first_r_step = 0, nrec_start, ev_crc = 0, after_end = 0;
    float c[3], rmax = -1.0f, first_r = -1.0f;
    printf("\n== bomb: item 7 at Sonic, host objects around him (B_8C78C390, 8C05E59A, 8C0489E4 -> 8C05E5C2)\n");
    if (id == 0 || w == NULL) {
        CHECK(0, "Sonic");
        return;
    }
    for (i = 0; i < 20; i++) {
        pair(ctx, id, 0);
    }
    state_of(ctx, id, &s);
    for (k = 0; k < 4; k++) {
        sa1_hostobj_desc d;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.kind = ob[k].kind;
        d.flags = ob[k].flags | SA1_HOSTOBJ_NOT_SOLID;
        d.position[0] = s.position[0] + ob[k].dx;
        d.position[1] = s.position[1] + 5.0f;
        d.position[2] = s.position[2] + ob[k].dz;
        d.armor = -1;
        d.touch = 0;
        d.num_shapes = 1;
        d.shapes[0].form = SA1_SHAPE_SPHERE;
        d.shapes[0].size[0] = 5.0f;
        d.user = 100u + k;
        CHECK(sa1_hostobj_add(ctx, &d, &ob[k].id) == SA1_OK, "%s added", ob[k].what);
    }
    for (i = 0; i < 3; i++) {
        pair(ctx, id, 0);
    }
    got = 0;
    sa1_poll_hostobj_events(ctx, ev, 256, &got);
    rec_attach(w);
    st = spawn_box(w, 7);
    nrec_start = g_nsrec;
    for (i = 0; i < 120 && st == SA1_OK; i++) {
        st = pair(ctx, id, 0);
        got = 0;
        sa1_poll_hostobj_events(ctx, ev, 256, &got);
        for (k = 0; k < got; k++) {
            uint32_t j;
            if (ev[k].type != SA1_HOSTOBJ_EVENT_HIT) {
                continue;
            }
            for (j = 0; j < 4; j++) {
                if (ev[k].object == ob[j].id) {
                    if (ob[j].hits++ == 0) {
                        ob[j].hit_step = ev[k].step;
                        ob[j].attack = ev[k].attack;
                        ob[j].level = ev[k].level;
                    }
                    {
                        uint32_t v[5] = { j, ev[k].attack, ev[k].level, ev[k].damage, ev[k].step };
                        ev_crc = tu_crc32(ev_crc, (const uint8_t *)v, sizeof v);
                    }
                }
            }
        }
    }
    CHECK(st == SA1_OK, "120 pairs with the bomb: %s", sa1_status_string(st));
    c[0] = rdf(w, ADDR_BOMB_C);
    c[1] = rdf(w, ADDR_BOMB_C + 4u);
    c[2] = rdf(w, ADDR_BOMB_C + 8u);
    /* the radius after every step: 10 at the box's break, + 10 per step, -1 past 500 */
    for (i = nrec_start; i < g_nsrec; i++) {
        const srec *r = &g_srec[i];
        if (start == 0 && r->r >= 0.0f) {
            start = i + 1;
            first_r = r->r;
            first_r_step = r->step;
        } else if (start != 0 && end_step == 0) {
            const srec *p = &g_srec[i - 1];
            if (r->r < 0.0f) {
                end_step = r->step;
                if (!(p->r + 10.0f > 500.0f)) {
                    bad_inc++;
                }
            } else if (r->r != p->r + 10.0f) {
                bad_inc++;
            }
        } else if (end_step != 0 && r->r >= 0.0f) {
            after_end++;
        }
        rmax = r->r > rmax ? r->r : rmax;
    }
    /* The break step's own value is 10 (8C05E582) plus the icon task's +10 when the icon task runs after the box in
     * that step: measured here, the box and the proxies (made later) run before the boot's older icon task in list 3,
     * so the break step ends at 20 and a proxy sees a radius one step after the icon task made it. */
    printf("    sphere: centre (%.1f, %.1f, %.1f), radius %.0f after the break step %u, ended at step %u (largest %.0f)\n",
           (double)c[0], (double)c[1], (double)c[2], (double)first_r, (unsigned)first_r_step, (unsigned)end_step,
           (double)rmax);
    CHECK(start != 0 && (first_r == 10.0f || first_r == 20.0f) && bad_inc == 0 && rmax == 500.0f &&
              end_step == first_r_step + (uint32_t)((500.0f - first_r) / 10.0f) + 1u && after_end == 0,
          "the sphere: 10 at the break (%.0f after that step's icon task), +10 every step to 500, then off (-1) through "
          "the icon list task (%u bad steps)", (double)first_r, (unsigned)bad_inc);
    for (k = 0; k < 4; k++) {
        float p[3], dd;
        uint32_t expect = 0;
        p[0] = s.position[0] + ob[k].dx;
        p[1] = s.position[1] + 5.0f;
        p[2] = s.position[2] + ob[k].dz;
        dd = sqrtf((p[0] - c[0]) * (p[0] - c[0]) + (p[1] - c[1]) * (p[1] - c[1]) + (p[2] - c[2]) * (p[2] - c[2]));
        /* the first step whose end radius exceeds the distance; the proxy, ahead of the icon task in list 3, sees it in
         * its next step (8C05E5C2: radius > |pos - centre|) */
        for (i = nrec_start; i < g_nsrec && expect == 0; i++) {
            if (g_srec[i].r > dd) {
                expect = g_srec[i].step + 1u;
            }
        }
        if (ob[k].expect_hit) {
            CHECK(ob[k].hits == 1 && ob[k].attack == SA1_ATTACK_OTHER && ob[k].level == 3u && expect != 0 &&
                      ob[k].hit_step + 1u >= expect && ob[k].hit_step <= expect,
                  "%s (%.1f from the centre): %u HIT (attack %u, level %u) at step %u (the sphere reaches it at step %u)",
                  ob[k].what, (double)dd, (unsigned)ob[k].hits, (unsigned)ob[k].attack, (unsigned)ob[k].level,
                  (unsigned)ob[k].hit_step, (unsigned)expect);
        } else {
            CHECK(ob[k].hits == 0, "%s (%.1f from the centre): no HIT (%u)", ob[k].what, (double)dd, (unsigned)ob[k].hits);
        }
    }
    for (k = 0; k < 4; k++) {
        sa1_hostobj_remove(ctx, ob[k].id, SA1_HOSTOBJ_REMOVE_SILENT);
    }
    pair(ctx, id, 0);
    g_det_bomb = arena_crc(w);
    g_det_bomb_ev = ev_crc;
    w->hostobj_post = g_post_orig;
    sa1_character_destroy(ctx, id);
    got = 0;
    sa1_poll_hostobj_events(ctx, ev, 256, &got);
    drain_events(ctx, 0);
}

/* ---- main ---------------------------------------------------------------------------------------------------------- */
static int want(const char *only, const char *name)
{
    return only == NULL || strstr(only, name) != NULL;
}

int main(int argc, char **argv)
{
    const char *gdi = getenv("SA1_TEST_GDI"), *only = NULL;
    sa1_context *ctx;
    int i;
    ga_args(&argc, argv);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            only = argv[++i];
        } else if (!strcmp(argv[i], "--seeds") && i + 1 < argc) {
            g_seeds = (uint32_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--verbose")) {
            g_verbose = 1;
        } else {
            gdi = argv[i];
        }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("libsa1 %s item box test (round 0.9.2, sadc docs/research/gsa1_090_faults.md F2), %u-bit\n",
           sa1_version_string(), (unsigned)(sizeof(void *) * 8u));
    if (gdi == NULL || gdi[0] == 0) {
        printf("no disc (GDI argument or SA1_TEST_GDI): nothing to run\n");
        return 0;
    }
    ctx = make_ctx(gdi);
    if (ctx == NULL) {
        return 2;
    }
    if (want(only, "boxes") || want(only, "screen")) {
        t_boxes(ctx, want(only, "screen"));
    }
    if (want(only, "digs")) {
        t_digs(ctx);
    }
    if (want(only, "drain")) {
        t_drain(ctx);
    }
    if (want(only, "bomb")) {
        t_bomb(ctx);
    }
    if (want(only, "drain") && want(only, "bomb")) {
        printf("DETERMINISM_ITEMBOX %08X %08X %08X\n", (unsigned)g_det_drain, (unsigned)g_det_bomb,
               (unsigned)g_det_bomb_ev);
    }
    sa1_shutdown(ctx);
    if (ga_enabled()) {
        uint32_t damaged = ga_check();
        CHECK(damaged == 0, "guard allocator: %u damaged blocks", (unsigned)damaged);
    }
    printf("\nitembox_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
