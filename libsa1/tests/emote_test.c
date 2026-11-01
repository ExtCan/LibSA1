/*
 * emote_test.c - round 0.9.4a (094a-lib-emote): emotes - any of a character's animations played through the cutscenes' event
 * action queue (include/libsa1.h "emotes", SA1_API_EMOTE; src/sa1_emote.c, src/world/sa1_world_emote.c,
 * src/sa1_emote_names.c; sadc docs/research/faces_wheel_plan.md 4.5 / 5.3 / 6.3 with its Review R.1, R.2, R.7-R.9, R.13-R.17,
 * emote_wheel_lib.md). Round 0.9.4b (094b-lib-emote; objects2_plan.md 11.2): the end mode SA1_EMOTE_HOLD (E20 hold, E21
 * hold_memcheck, E22 hold_catalogue).
 *
 *   emote_test [--only NAME[,NAME]] [--list] [--verbose] [--guard-alloc] [--tsv DIR] GDI     (GDI: or $SA1_TEST_GDI)
 *
 * Built and run by tests/build_emote.py (a snapshot of build/<target>/libsa1.a and LIB's headers). Scene as set_test: native
 * coordinates, a flat GRID floor 6000 x 6000 at y = 0, characters made with SA1_CREATE_WORLD facing +X, one pair per tick,
 * the host camera left at its default (it keeps the character in view: the queue moves only on drawn pairs, review R.17).
 *
 * ---- API NOTES (094a-lib-emote -> 094a-gsa1-wheel; the full text, faces_wheel_plan.md 3.2) ----------------------------------
 * Header: include/libsa1.h "emotes" (SA1_API_EMOTE 1), exactly faces_wheel_plan.md 5.3. Functions:
 *   uint32_t   sa1_emote_count(ctx, kind, form)            0 without a disc or when the kind's set is not loaded
 *   sa1_status sa1_emote_get(ctx, kind, form, i, &info)    NOT_LOADED / NOT_FOUND (i past the list); no world needed
 *   sa1_status sa1_character_emote(ctx, id, &emote)        latched, applied at the character's next pair start
 *   sa1_status sa1_character_emote_stop(ctx, id, how)      STOP_NOW (EV_ClrAction at the next pair start) / STOP_BLEND
 *   sa1_state_ex.emote_state / emote_item / emote_row / emote_extra / emote_frame (after the pair; none: 0, -1, -1, -1, 0)
 * Lists (kind, form): Sonic 107 (form 0), General Super Sonic 119 (kind 0, form SA1_EMOTE_FORM_SUPER: GT rows + 130-141),
 *   Super Sonic kind 8: 92 (any form), Tails 140 (131 rows + 9 extras), Knuckles 122 (110 + 12), Amy 105 (97 + 8),
 *   E-102 89 (86 + 3), Big 96 (93 + 3); Eggman / Tikal: none. The form of a Sonic world: SA1_EMOTE_FORM_SUPER while the
 *   General look is on (a Sonic who turned Super keeps the "sonic" wheel; his list then is the Super one: rows 0-129 still
 *   offered on the golden body, plus 130-141).
 * Keys: a row = (SA1_EMOTE_ROW, row); an extra = (SA1_EMOTE_EXTRA, index among the kind's offered extras, list order).
 *   dup_of: the list position of the first entry with the same DRAWN object and motion (k_panch0 39 and 54 are different
 *   motions: not dups; s_kimepose0 75 / 139 and ss_move2 133 / 134 are dups in the Super lists).
 * Refusals (sa1_last_error holds exactly these words): SA1_ERR_UNSUPPORTED "in the air", "hurt", "carrying something",
 *   "on the board", "moving", "busy" (and "<Name> cannot play emotes" for Eggman / Tikal, a puppet's own text);
 *   SA1_ERR_INVALID_ARGUMENT "<internal name> is not available for <Sonic|Super Sonic|Tails|Knuckles|Amy|E-102|Big>" (or
 *   "row N" / "extra N" when the entry has no name); SA1_ERR_LIMIT "the game's memory is full"; SA1_ERR_NOT_FOUND no
 *   character. A call before the world's first pair is latched (SA1_OK) and checked at the pair start that applies it
 *   (it waits while the new character lands: a world starts him falling for a pair or two); a refusal at a pair start
 *   is logged (SA1_LOG_WARN "emote: <why>") at the next emote call. Every start / group start needs jvmode 1 (review
 *   R.8): it stays latched otherwise. The game heap is checked before queueing (largest free block > 4 KB + 48 bytes
 *   per entry, review R.7).
 * Groups (review R.1): a blend (link, default 8) only between entries of one object; an unblended transition (another
 *   object, link 0) starts a new group after a clear, so the game heap never leaks (E14: 220 emotes, free total back).
 * Measured (this test, x64 = x86): every entry of the 8 lists plays (E2); pacing 2 x racio per pair (Sonic / Tails 1.0,
 *   Knuckles / Amy 2.0, E-102 0.6, Big 0.4, kind 8's 130 0.5 frames per pair for row 0 / 130); the trailing stand ends
 *   with a jump of 0.003 units (NO_TRAIL: the game's pop 0.023); the General form's 130 = kind 8's (feet -1.17..-0.40,
 *   top 11.04..11.64), B_CHAOS7's own 1.000 lower; Amy's hammer 932 -> 998 triangles.
 * Pacing: frames per 60 Hz step = PL_ACTION.racio (mtnmode 9/10/11: 1; 12 or racio 0: 0.5; extras 1) x the item's speed;
 *   the queue frame advances 2 x that per pair for every kind (Amy / E-102 advance twice per pair at x1, the others once
 *   at x2). Rows loop when mtnmode is 3/9/10/11/12 (sa1_emote_info.play LOOP), hold rows (F_HOLD) play once.
 * Cancels: before a pair whose input moves him (stick past the game's dead zone, the jump / action presses) unless
 *   KEEP_INPUT; after a pair that left the standing mode (not with IN_AIR), hurt him, changed the held object, the form
 *   (the transformation / revert) or put him on a board. Moving the stick ends an emote AT ONCE (the gameplay of that pair
 *   is the no-emote world's). A once emote ends by itself with a blend into the stand (state ENDING while the trailing
 *   stand plays; item = count). STOP_BLEND: the stand after the current group (a loop finishes its cycle).
 * The queue moves only on pairs the character is drawn: off-camera an emote pauses (README known limit).
 * README lines: "Emote wheel animations: every animation of each character by its internal name; animations made for props
 *   (carrying, set pieces, Big's fishing, the board) play without the prop; moving, jumping or the action button ends an
 *   animation at once; an animation pauses while the character is off-screen and goes on when he is seen again."
 *
 * ---- API NOTES (round 0.9.4b, 094b-lib-emote -> 094b-gsa1-wheel; objects2_plan.md 11.2): SA1_EMOTE_HOLD --------------
 * Header (landed by 094b-lib-range from objects2/lanes/emote_header.txt): SA1_EMOTE_HOLD 0x20u (sa1_emote.flags),
 *   SA1_API_EMOTE_HOLD 1, SA1_EMOTE_STATE_HOLDING 3u (sa1_state_ex.emote_state). No new function, no struct change.
 * What it does: when the emote's LAST queued entry plays once (a once / hold row or extra, PLAY_ONCE, or the last row of
 *   its CHAIN), that entry stays on its motion's last frame (nbFrame - 1, the game's mtnmode 5 rule) instead of the
 *   trailing stand entry (HOLD implies no trailing stand). It reaches the last frame at the very pair the 0.9.4a emote
 *   would have started blending into the stand (E20: Sonic 75 at pair 32 both ways), then emote_state = HOLDING,
 *   emote_frame = nbFrame - 1, emote_item / emote_row / emote_extra = the held entry's, until: movement input (the stick
 *   past the dead zone, jump, action: ended before that pair, the gameplay of that pair is the no-emote world's), the
 *   state rules (left the standing mode, hurt, the held object, the form, a board), another sa1_character_emote (it
 *   replaces the pose at the next pair), sa1_character_emote_stop: STOP_NOW (the gameplay pose next pair) or STOP_BLEND
 *   (the entry ends at its next drawn pair and the stand blends in from the held frame: exactly the 0.9.4a trailing
 *   stand's blend, largest change between pairs 0.756 both ways). Measured: still HOLDING after 40 s without input (the
 *   game's idle timer does not end it; the idle voice may still play: the gameplay underneath is unchanged).
 *   A LOOPING last entry (a loop row by default, or PLAY_LOOP) is not affected: it loops until a cancel or a stop.
 *   IN_AIR / KEEP_INPUT / NO_PROPS / CHAIN combine as before; NO_TRAIL is implied.
 * Every once entry of all 8 lists holds on its last frame without a fault (E22: Sonic 56, Tails 80, Knuckles 72, Amy 57,
 *   E-102 61, Big 55, General Super Sonic 60, kind 8 43); the held world's gameplay equals a twin's in every pair (E20);
 *   no game-heap or host-heap leak over 20 held plays ended every way (E21, also under SA1_GUARD_ALLOC).
 * For G-SA1's wheel: pass SA1_EMOTE_HOLD for a slot whose "Hold the last pose" is on (default on for once entries; a loop
 *   entry ignores it); treat STATE_HOLDING like PLAYING where the wheel decides "chosen again: back to standing"
 *   (cl_wheel.lua W.PlayBody checks PLAYING / ENDING today) - STOP_BLEND then blends the held pose into the stand.
 * README line: "Emote wheel: an animation that ends in a pose (the victory pose and others) now holds its last frame until
 *   you move, jump, press the action button or pick another animation (option 'Hold the last pose', per slot)."
 * ------------------------------------------------------------------------------------------------------------------------
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
#include "sa1_emote.h"
#include "sa1_hostobj.h"    /* sa1ho_world_heap: the game heap */
#include "test_util.h"
#include "guard_alloc.c"    /* SA1_GUARD_ALLOC=1 or --guard-alloc: every library heap block against a guard page */
#define sa1_host_defaults(h) (sa1_host_defaults(h), ga_host(h))

#if !defined(_WIN32)
#include <pthread.h>
#endif

#ifndef EMOTE_TEST_FACES
#define EMOTE_TEST_FACES 0
#endif

static int g_fail, g_ok, g_verbose, g_faults;
static const char *g_gdi;
static const char *g_tsv;

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
#define NOTE(...)                                                                                                       \
    do {                                                                                                                \
        printf("  note  ");                                                                                             \
        printf(__VA_ARGS__);                                                                                            \
        printf("\n");                                                                                                   \
    } while (0)

/* ---- addresses (US v1.005) ---------------------------------------------------------------------------------------- */
#define A_PLAYER_TWP    0x8C78C608u
#define A_PLAYER_PWP    0x8C78C668u
#define T_FLAG          0x04u
#define T_EWP           0x3Cu
#define T_POS           0x20u
#define E_F3C           0x3Cu
#define E_LIST40        0x40u
#define E_BUF44         0x44u
#define E_FACE          0x5Cu           /* the face block 0x5C..0x6B */
#define P_JVMODE        0x122u
#define P_PLACT         0x140u
#define FN_MALLOC       0x8C098A80u     /* void *MAlloc(Sint32) */
#define FN_MFREE        0x8C098B42u     /* void MFree(void *) */
#define FN_SPRING       0x8C07C720u     /* (pno, x, y, z): the ground spring's request (smode 3 with a speed) */
#define FN_EV_SETACTION 0x8C04B4D2u
#define FN_EV_CLEAR     0x8C04B48Cu

static const char *k_kinds[9] = { "Sonic", "Eggman", "Tails", "Knuckles", "Tikal", "Amy", "E-102", "Big", "Super Sonic" };

/* round 0.9.5 (board095_perf.md 4, L1): the lines the host's callback gets, counted per level while g_logcap is set */
static int g_logcap;
static uint32_t g_log_error, g_log_warn, g_log_other;

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (g_logcap) {
        if (level >= SA1_LOG_ERROR) {
            g_log_error++;
        } else if (level == SA1_LOG_WARN) {
            g_log_warn++;
        } else {
            g_log_other++;
        }
    }
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

/* ---- guest memory --------------------------------------------------------------------------------------------------- */
static int arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static uint32_t rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (!arena(g) || sa1g_world_rd32(w, g & ~3u, &v) != 0) {
        return 0;
    }
    return v;
}

static uint32_t rd16(const sa1g_world *w, uint32_t g)
{
    return (rd32(w, g & ~3u) >> (8u * (g & 2u))) & 0xFFFFu;
}

static uint32_t rd8(const sa1g_world *w, uint32_t g)
{
    return (rd32(w, g & ~3u) >> (8u * (g & 3u))) & 0xFFu;
}

static void wr32(sa1g_world *w, uint32_t g, uint32_t v)
{
    if (arena(g)) {
        sa1g_world_wr32(w, g & ~3u, v);
    }
}

static void wr16(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint32_t a = g & ~3u, sh = 8u * (g & 2u), o = rd32(w, a);
    wr32(w, a, (o & ~(0xFFFFu << sh)) | ((v & 0xFFFFu) << sh));
}

/* any guest word the world maps (the arena, segment 0x18) */
static uint32_t rdx32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    const void *p;
    if (g == 0) {
        return 0;
    }
    if (arena(g)) {
        return rd32(w, g);
    }
    if (((g >> 24) & 31u) == SA1G_SEG_EXT && (size_t)(g & 0xFFFFFFu) + 4u <= w->ext_used) {
        p = sa1g_world_ptr(w, g);
        memcpy(&v, p, 4);
    }
    return v;
}

static uint32_t twp0(const sa1g_world *w)
{
    return rd32(w, A_PLAYER_TWP);
}

static uint32_t pwp0(const sa1g_world *w)
{
    return rd32(w, A_PLAYER_PWP);
}

static uint32_t ewp0(const sa1g_world *w)
{
    uint32_t t = twp0(w);
    return t ? rd32(w, t + T_EWP) : 0u;
}

static uint32_t heap_total(const sa1g_world *w)
{
    uint32_t total = 0, largest = 0;
    sa1ho_world_heap(w, &total, &largest);
    return total;
}

static uint32_t heap_largest(const sa1g_world *w)
{
    uint32_t total = 0, largest = 0;
    sa1ho_world_heap(w, &total, &largest);
    return largest;
}

/* ---- the scene and the characters -------------------------------------------------------------------------------- */
static int scene(sa1_context *ctx)
{
    sa1_collision_tri t[2];
    float q[4][3] = { { -3000, 0, -3000 }, { -3000, 0, 3000 }, { 3000, 0, 3000 }, { 3000, 0, -3000 } };
    sa1_status st;
    memset(t, 0, sizeof t);
    memcpy(t[0].v[0], q[0], 12);
    memcpy(t[0].v[1], q[1], 12);
    memcpy(t[0].v[2], q[2], 12);
    memcpy(t[1].v[0], q[0], 12);
    memcpy(t[1].v[1], q[2], 12);
    memcpy(t[1].v[2], q[3], 12);
    t[0].surface = t[1].surface = SA1_SURFACE_SOLID;
    st = sa1_collision_set_static(ctx, t, 2);
    if (SA1_FAILED(st)) {
        printf("  set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

#define MK_GENERAL      1   /* kind 0 super-capable General, Super from the start (SA1_SUPER_START), no drain */
#define MK_GENERAL_OFF  2   /* kind 0 super-capable General, normal at the start */
#define MK_PUPPET       4

static sa1_character_id mk(sa1_context *ctx, int32_t kind, float y, int how)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = (how & MK_PUPPET) ? SA1_CREATE_PUPPET : (SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES |
                                                        SA1_CREATE_START_RINGS);
    d.position[0] = 100.0f;
    d.position[1] = y;
    d.position[2] = 100.0f;
    d.start_rings = 60;
    if (how & MK_PUPPET) {
        d.flags |= SA1_CREATE_SA1_SPACE;
    } else if (kind == SA1_CHAR_SUPER_SONIC) {
        d.flags |= SA1_CREATE_SUPER;
        d.super_flags = SA1_SUPER_NO_DRAIN;
    } else if (how & (MK_GENERAL | MK_GENERAL_OFF)) {
        d.flags |= SA1_CREATE_SUPER | SA1_CREATE_SUPER_CAPABLE;
        d.super_flags = SA1_SUPER_GENERAL | SA1_SUPER_NO_DRAIN | ((how & MK_GENERAL) ? SA1_SUPER_START : 0u);
    }
    st = sa1_character_create_ex(ctx, &d, &id);
    if (SA1_FAILED(st)) {
        printf("  create %s: %s: %s\n", kind >= 0 && kind < 9 ? k_kinds[kind] : "?", sa1_status_string(st),
               sa1_last_error(ctx));
        return 0;
    }
    return id;
}

static void poll_all(sa1_context *ctx)
{
    sa1_event ev[64];
    uint32_t n;
    do {
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
    } while (n == 64);
}

static int tick(sa1_context *ctx, sa1_character_id id, float sx, float sy, uint32_t buttons)
{
    sa1_input in;
    sa1_status st;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    poll_all(ctx);
    if (st != SA1_OK) {
        g_faults++;
        printf("  tick: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

static int run(sa1_context *ctx, sa1_character_id id, int pairs)
{
    int i;
    for (i = 0; i < pairs; i++) {
        if (tick(ctx, id, 0, 0, 0) != 0) {
            return -1;
        }
    }
    return 0;
}

static void xstate(sa1_context *ctx, sa1_character_id id, sa1_state_ex *x)
{
    memset(x, 0, sizeof *x);
    x->struct_size = sizeof *x;
    sa1_character_state_ex(ctx, id, x);
}

static sa1_status emote_ex(sa1_context *ctx, sa1_character_id id, uint32_t n, const uint32_t *src, const uint32_t *idx,
                           int32_t last_play, uint32_t flags, int32_t link)
{
    sa1_emote e;
    uint32_t i;
    memset(&e, 0, sizeof e);
    e.struct_size = sizeof e;
    e.count = n;
    for (i = 0; i < n; i++) {
        e.items[i].source = src[i];
        e.items[i].index = idx[i];
        e.items[i].play = i + 1u == n ? last_play : SA1_EMOTE_PLAY_DEFAULT;
    }
    e.link = link;
    e.flags = flags;
    return sa1_character_emote(ctx, id, &e);
}

static sa1_status emote_row(sa1_context *ctx, sa1_character_id id, uint32_t row, int32_t play, uint32_t flags)
{
    uint32_t s = SA1_EMOTE_ROW;
    return emote_ex(ctx, id, 1, &s, &row, play, flags, -1);
}

static void winfo(sa1_context *ctx, sa1_character_id id, sa1em_world_info *i)
{
    sa1em_world_info_get(sa1_character_world(ctx, id), i);
}

/* the opaque body draws of the last render: count, triangles, archives, extents relative to the drawn root */
typedef struct body_t {
    uint32_t draws, tris, archives;
    float lo, hi, span, cx, cy, cz;
} body_t;

static void body(sa1_context *ctx, sa1_character_id id, float alpha, body_t *b)
{
    const sa1_render *rd = NULL;
    uint32_t i, k, t, nv = 0;
    float lo = 1e9f, hi = -1e9f, xlo = 1e9f, xhi = -1e9f, zlo = 1e9f, zhi = -1e9f, sx = 0, sy = 0, sz = 0;
    memset(b, 0, sizeof *b);
    if (sa1_character_render(ctx, id, alpha, &rd) != SA1_OK || rd == NULL) {
        return;
    }
    for (i = 0; i < rd->num_draws; i++) {
        const sa1_draw *d = &rd->draws[i];
        if (d->prim != SA1_PRIM_TRIANGLES || d->source != SA1_SOURCE_CHARACTER || d->archive < 0 ||
            (d->flags & (SA1_DRAW_SHADOW | SA1_DRAW_BLEND | SA1_DRAW_EFFECT | SA1_DRAW_AFTERIMAGE))) {
            /* the opaque body only (not Super Sonic's aura, the after-images, effects or the blob shadow) */
            continue;
        }
        b->draws++;
        b->tris += d->count;
        if (d->archive < 32) {
            b->archives |= 1u << d->archive;
        }
        for (t = d->first; t < d->first + d->count; t++) {
            for (k = 0; k < 3; k++) {
                const float *p = &rd->positions[9 * t + 3 * k];
                float y = p[1] - rd->position[1], x = p[0] - rd->position[0], z = p[2] - rd->position[2];
                lo = y < lo ? y : lo;
                hi = y > hi ? y : hi;
                xlo = x < xlo ? x : xlo;
                xhi = x > xhi ? x : xhi;
                zlo = z < zlo ? z : zlo;
                zhi = z > zhi ? z : zhi;
                sx += x;
                sy += y;
                sz += z;
                nv++;
            }
        }
    }
    if (nv) {
        b->lo = lo;
        b->hi = hi;
        b->span = (xhi - xlo) > (zhi - zlo) ? (xhi - xlo) : (zhi - zlo);
        b->cx = sx / (float)nv;
        b->cy = sy / (float)nv;
        b->cz = sz / (float)nv;
    }
}

static float body_diff(const body_t *a, const body_t *b)
{
    float d = fabsf(a->lo - b->lo);
    d = fmaxf(d, fabsf(a->hi - b->hi));
    d = fmaxf(d, fabsf(a->span - b->span));
    d = fmaxf(d, fabsf(a->cx - b->cx));
    d = fmaxf(d, fabsf(a->cy - b->cy));
    d = fmaxf(d, fabsf(a->cz - b->cz));
    return d;
}

/* gameplay snapshot (identity) */
typedef struct snap_t {
    float pos[3], spd[3];
    int32_t mode, action;
    uint32_t rnd, flags;
    int32_t rings;
} snap_t;

static void snap(sa1_context *ctx, sa1_character_id id, snap_t *s)
{
    sa1_state_ex x;
    xstate(ctx, id, &x);
    memset(s, 0, sizeof *s);
    memcpy(s->pos, x.sa1_position, 12);
    memcpy(s->spd, x.speed, 12);
    s->mode = x.mode;
    s->action = x.action;
    s->rnd = x.rand_state;
    s->flags = x.game_flags;
    s->rings = x.rings;
}

static int snap_eq(const snap_t *a, const snap_t *b)
{
    return memcmp(a->pos, b->pos, 12) == 0 && memcmp(a->spd, b->spd, 12) == 0 && a->mode == b->mode &&
           a->action == b->action && a->rnd == b->rnd && a->flags == b->flags && a->rings == b->rings;
}

/* frames per pair of a row as the lane plays it (the TSVs' ev_speed x 2 / the per-pair advance) */
static float row_per_pair(sa1_context *ctx, int32_t kind, uint32_t row)
{
    sa1_action_info ai;
    float base;
    memset(&ai, 0, sizeof ai);
    ai.struct_size = sizeof ai;
    if (sa1_action_info_get(ctx, kind, row, &ai) != SA1_OK) {
        return 0.0f;
    }
    if (ai.mode == 9 || ai.mode == 10 || ai.mode == 11) {
        base = 1.0f;
    } else if (ai.mode == 12 || !(ai.speed > 0.0f)) {
        base = 0.5f;
    } else {
        base = ai.speed;
    }
    return 2.0f * base;
}

/* calls inside a world (tests only) */
typedef struct call_req {
    uint32_t fn, a;
    int32_t n;
    float f[3];
    uint32_t ret;
} call_req;

static void malloc_cb(sa1g_world *w, void *user)
{
    call_req *r = (call_req *)user;
    void *p = ((void *(*)(int32_t))sa1_fn(SA1_GFN_C(FN_MALLOC), 0u))(r->n);
    (void)w;
    r->ret = p != NULL ? sa1_encode(p) : 0u;
}

static void mfree_cb(sa1g_world *w, void *user)
{
    call_req *r = (call_req *)user;
    (void)w;
    ((void (*)(void *))sa1_fn(SA1_GFN_C(FN_MFREE), 0u))(sa1_decode(r->a));
}

static void spring_cb(sa1g_world *w, void *user)
{
    call_req *r = (call_req *)user;
    (void)w;
    ((void (*)(uint8_t, float, float, float))sa1_fn(SA1_GFN_C(FN_SPRING), 0u))(0, r->f[0], r->f[1], r->f[2]);
}

/* the raw queue (tests only: the research probe's way, in a world the lane never touched) */
static void rawq_cb(sa1g_world *w, void *user)
{
    call_req *r = (call_req *)user;
    void *t = sa1_decode(twp0(w));
    ((void (*)(void *))sa1_fn(SA1_GFN_C(FN_EV_CLEAR), 0u))(t);
    if (r->a != 0) {
        ((void (*)(void *, void *, void *, float, int32_t, int32_t))sa1_fn(SA1_GFN_C(FN_EV_SETACTION), 0u))(
            t, sa1_decode(r->a), NULL, r->f[0], 1, 0);
    }
}

/* ---- E1 identity -------------------------------------------------------------------------------------------------------- */
static void t_identity(sa1_context *ctx)
{
    sa1_character_id ia = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0), ib = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    static const uint32_t sched_pair[5] = { 20, 130, 200, 255, 0xFFFFFFFFu };
    static const uint32_t sched_row[4] = { 26, 3, 81, 26 };
    uint32_t next = 0, p, emoting = 0, accepted = 0, refusals = 0, cancels = 0;
    int first = -1;
    sa1em_world_info wi;
    printf("== E1 identity: two Sonic worlds, 1,300 pairs of the same input, B emotes (cancelled by input)\n");
    if (!ia || !ib) {
        CHECK(0, "worlds");
        return;
    }
    for (p = 0; p < 1300; p++) {
        float sy = 0.0f;
        uint32_t bt = 0;
        snap_t sa, sb;
        sa1_state_ex xb;
        if (p >= 60 && p < 120) {
            sy = 1.0f;          /* run */
        }
        if (p == 160) {
            bt = SA1_BUTTON_JUMP;
        }
        if (p >= 240 && p < 250) {
            sy = 0.6f;          /* a short walk while B emotes: the input cancels it */
        }
        if (next < 4 && p >= sched_pair[next]) {
            sa1_status st = emote_row(ctx, ib, sched_row[next], SA1_EMOTE_PLAY_DEFAULT, SA1_EMOTE_CHAIN);
            if (st == SA1_OK) {
                accepted++;
                next++;
            } else {
                refusals++;     /* not standing yet (after the run, the jump): the next pair again */
            }
        }
        if (tick(ctx, ia, 0, sy, bt) != 0 || tick(ctx, ib, 0, sy, bt) != 0) {
            break;
        }
        xstate(ctx, ib, &xb);
        emoting += xb.emote_state != SA1_EMOTE_STATE_NONE;
        snap(ctx, ia, &sa);
        snap(ctx, ib, &sb);
        if (first < 0 && !snap_eq(&sa, &sb)) {
            first = (int)p;
            printf("  first difference at pair %u: A mode %d act %d pos %.3f %.3f %.3f rand %08X | B mode %d act %d pos "
                   "%.3f %.3f %.3f rand %08X\n", (unsigned)p, (int)sa.mode, (int)sa.action, (double)sa.pos[0],
                   (double)sa.pos[1], (double)sa.pos[2], (unsigned)sa.rnd, (int)sb.mode, (int)sb.action,
                   (double)sb.pos[0], (double)sb.pos[1], (double)sb.pos[2], (unsigned)sb.rnd);
        }
    }
    winfo(ctx, ib, &wi);
    cancels = wi.cancels_input;
    CHECK(first < 0, "position, speed, mode, action, flags, rings and rand identical in all 1,300 pairs");
    CHECK(emoting >= 1100u, "B emoted %u pairs (>= 1,100), %u emotes accepted (%u refusals while not standing), %u "
          "cancels by input", (unsigned)emoting, (unsigned)accepted, (unsigned)refusals, (unsigned)cancels);
    CHECK(cancels >= 2u, "the run and the walk cancelled emotes before their pairs (%u)", (unsigned)cancels);
    sa1_character_destroy(ctx, ia);
    sa1_character_destroy(ctx, ib);
}

/* ---- E2 catalogue: every entry of every list plays -------------------------------------------------------------------- */
typedef struct cat_list {
    int32_t kind;
    uint32_t form;
    int how;
    uint32_t want;              /* the TSVs' offered count */
} cat_list;

static const cat_list k_cat[8] = {
    { SA1_CHAR_SONIC, SA1_EMOTE_FORM_NORMAL, 0, 107 },     { SA1_CHAR_TAILS, 0, 0, 140 },
    { SA1_CHAR_KNUCKLES, 0, 0, 122 },                      { SA1_CHAR_AMY, 0, 0, 105 },
    { SA1_CHAR_GAMMA, 0, 0, 89 },                          { SA1_CHAR_BIG, 0, 0, 96 },
    { SA1_CHAR_SONIC, SA1_EMOTE_FORM_SUPER, MK_GENERAL, 119 }, { SA1_CHAR_SUPER_SONIC, 0, 0, 92 },
};

static void t_catalogue(sa1_context *ctx)
{
    size_t li;
    printf("== E2 catalogue: every entry of every list plays without a fault (one item, NO_TRAIL)\n");
    for (li = 0; li < sizeof k_cat / sizeof k_cat[0]; li++) {
        const cat_list *L = &k_cat[li];
        uint32_t n = sa1_emote_count(ctx, L->kind, L->form), i, ok = 0, bad = 0, late = 0, arch_bad = 0, mode_bad = 0;
        uint32_t stand_mode = L->kind == SA1_CHAR_SUPER_SONIC ? 71u : 1u;
        uint32_t want_arch = L->kind == SA1_CHAR_SUPER_SONIC ? 1u : (L->how & MK_GENERAL) ? (1u << 5) : 0u;
        sa1_character_id id = mk(ctx, L->kind, 0.5f, L->how);
        char first_bad[160] = "";
        const char *name = L->how & MK_GENERAL ? "General Super Sonic" : k_kinds[L->kind];
        if (!id || run(ctx, id, 8) != 0) {
            CHECK(0, "%s: world", name);
            continue;
        }
        for (i = 0; i < n; i++) {
            sa1_emote_info info;
            sa1_status st = SA1_ERR_UNSUPPORTED;
            uint32_t pairs, maxp, draws = 0, arch = 0, tries;
            int loop, ended = 0, faulted = 0;
            float per_pair;
            memset(&info, 0, sizeof info);
            info.struct_size = sizeof info;
            sa1_emote_get(ctx, L->kind, L->form, i, &info);
            for (tries = 0; tries < 60 && st == SA1_ERR_UNSUPPORTED; tries++) {
                uint32_t s = info.source, x = info.index;
                st = emote_ex(ctx, id, 1, &s, &x, SA1_EMOTE_PLAY_DEFAULT, SA1_EMOTE_NO_TRAIL, -1);
                if (st == SA1_ERR_UNSUPPORTED) {
                    run(ctx, id, 1);
                }
            }
            if (st != SA1_OK) {
                bad++;
                if (!first_bad[0]) {
                    snprintf(first_bad, sizeof first_bad, "%u %s: %s (%s)", (unsigned)i, info.internal_name,
                             sa1_status_string(st), sa1_last_error(ctx));
                }
                continue;
            }
            loop = info.play == SA1_EMOTE_PLAY_LOOP;
            per_pair = info.source == SA1_EMOTE_EXTRA ? 2.0f : row_per_pair(ctx, L->kind, info.index);
            maxp = loop ? 12u : (uint32_t)ceilf((float)info.frames / (per_pair > 0.0f ? per_pair : 1.0f)) + 2u;
            for (pairs = 0; pairs < maxp + 40u; pairs++) {
                sa1_state_ex x;
                body_t b;
                if (tick(ctx, id, 0, 0, 0) != 0) {
                    faulted = 1;
                    break;
                }
                xstate(ctx, id, &x);
                if ((uint32_t)x.mode != stand_mode) {
                    mode_bad++;
                }
                if (pairs == 0 || x.emote_state != SA1_EMOTE_STATE_NONE) {
                    /* the first pair drew the entry (a short one may end in it: Sonic's 82 at 30 frames per pair) */
                    body(ctx, id, 1.0f, &b);
                    draws = b.draws > draws ? b.draws : draws;
                    arch |= b.archives;
                }
                if (x.emote_state == SA1_EMOTE_STATE_NONE) {
                    ended = 1;
                    break;
                }
                if (loop && pairs + 1u >= maxp) {
                    break;
                }
            }
            if (faulted) {
                bad++;
                if (!first_bad[0]) {
                    snprintf(first_bad, sizeof first_bad, "%u %s: world fault", (unsigned)i, info.internal_name);
                }
                sa1_character_destroy(ctx, id);
                id = mk(ctx, L->kind, 0.5f, L->how);
                run(ctx, id, 8);
                continue;
            }
            if (loop) {
                sa1_character_emote_stop(ctx, id, SA1_EMOTE_STOP_NOW);
                run(ctx, id, 1);
            } else if (!ended || pairs + 1u > maxp) {
                late++;
                if (!first_bad[0]) {
                    snprintf(first_bad, sizeof first_bad, "%u %s: once entry %s after %u pairs (bound %u)", (unsigned)i,
                             info.internal_name, ended ? "ended" : "still playing", (unsigned)pairs + 1u, (unsigned)maxp);
                }
                sa1_character_emote_stop(ctx, id, SA1_EMOTE_STOP_NOW);
                run(ctx, id, 1);
            }
            if (draws == 0) {
                bad++;
                if (!first_bad[0]) {
                    snprintf(first_bad, sizeof first_bad, "%u %s: no body draws", (unsigned)i, info.internal_name);
                }
                continue;
            }
            if (arch != want_arch && !(want_arch == 0u && (arch & ~0x1Fu) == 0u)) {
                arch_bad++;
                if (!first_bad[0]) {
                    snprintf(first_bad, sizeof first_bad, "%u %s: archives %X (want %X)", (unsigned)i,
                             info.internal_name, (unsigned)arch, (unsigned)want_arch);
                }
            }
            ok++;
            run(ctx, id, 2);
        }
        CHECK(n == L->want && bad == 0 && late == 0 && arch_bad == 0 && mode_bad == 0,
              "%s: %u entries (TSVs: %u): %u played, %u failed, %u once entries late, %u archive mismatches, %u pairs out "
              "of the standing mode%s%s", name, (unsigned)n, (unsigned)L->want, (unsigned)ok, (unsigned)bad, (unsigned)late,
              (unsigned)arch_bad, (unsigned)mode_bad, first_bad[0] ? "; first: " : "", first_bad);
        sa1_character_destroy(ctx, id);
    }
}

/* ---- E3 cross_object ------------------------------------------------------------------------------------------------------ */
static void t_cross_object(sa1_context *ctx)
{
    static const struct { int32_t kind; uint32_t a, b; } k[2] = { { SA1_CHAR_SONIC, 82, 83 }, { SA1_CHAR_KNUCKLES, 33, 34 } };
    size_t i;
    printf("== E3 cross_object: two items of different objects (link 8): no fault, the second starts unblended\n");
    for (i = 0; i < 2; i++) {
        sa1_character_id id = mk(ctx, k[i].kind, 0.5f, 0);
        sa1g_world *w;
        uint32_t src[2] = { SA1_EMOTE_ROW, SA1_EMOTE_ROW }, idx[2], p, second = 0, link2 = 0xFF, faults0 = (uint32_t)g_faults;
        sa1em_world_info wi;
        sa1_status st;
        idx[0] = k[i].a;
        idx[1] = k[i].b;
        if (!id || run(ctx, id, 8) != 0) {
            CHECK(0, "world");
            continue;
        }
        w = sa1_character_world(ctx, id);
        st = emote_ex(ctx, id, 2, src, idx, SA1_EMOTE_PLAY_ONCE, SA1_EMOTE_NO_TRAIL, 8);
        for (p = 0; p < 200 && st == SA1_OK; p++) {
            sa1_state_ex x;
            if (tick(ctx, id, 0, 0, 0) != 0) {
                break;
            }
            xstate(ctx, id, &x);
            if (x.emote_item == 1 && !second) {
                uint32_t head = rd32(w, ewp0(w) + E_LIST40);
                second = p + 1u;
                link2 = head ? rd8(w, head + 1u) : 0xFEu;
            }
            if (x.emote_state == SA1_EMOTE_STATE_NONE) {
                break;
            }
        }
        winfo(ctx, id, &wi);
        CHECK(st == SA1_OK && (uint32_t)g_faults == faults0 && second > 0 && link2 == 0 && wi.groups >= 2u,
              "%s %u then %u: no fault, the second item from pair %u with linkframe %u, %u groups queued",
              k_kinds[k[i].kind], (unsigned)k[i].a, (unsigned)k[i].b, (unsigned)second, (unsigned)link2,
              (unsigned)wi.groups);
        sa1_character_destroy(ctx, id);
    }
}

/* ---- E4 pacing ------------------------------------------------------------------------------------------------------------- */
static void t_pacing(sa1_context *ctx)
{
    static const struct { int32_t kind; uint32_t row; } k[7] = {
        { SA1_CHAR_SONIC, 0 }, { SA1_CHAR_TAILS, 0 }, { SA1_CHAR_KNUCKLES, 0 }, { SA1_CHAR_AMY, 0 },
        { SA1_CHAR_GAMMA, 0 }, { SA1_CHAR_BIG, 0 },   { SA1_CHAR_SUPER_SONIC, 130 },
    };
    size_t i;
    printf("== E4 pacing: the queue frame advance per pair = 2 x racio for every kind (kind 8 included)\n");
    for (i = 0; i < 7; i++) {
        sa1_character_id id = mk(ctx, k[i].kind, 0.5f, 0);
        float want = row_per_pair(ctx, k[i].kind, k[i].row), sum = 0, prev = -1;
        int n = 0, p;
        if (!id || run(ctx, id, 8) != 0) {
            CHECK(0, "world");
            continue;
        }
        emote_row(ctx, id, k[i].row, SA1_EMOTE_PLAY_LOOP, 0);
        for (p = 0; p < 12; p++) {
            sa1_state_ex x;
            tick(ctx, id, 0, 0, 0);
            xstate(ctx, id, &x);
            if (prev >= 0.0f && x.emote_frame > prev) {
                sum += x.emote_frame - prev;
                n++;
            }
            prev = x.emote_frame;
        }
        CHECK(n > 0 && fabsf(sum / (float)n - want) < 1e-4f, "%s row %u: %.4f frames per pair (2 x racio %.4f)",
              k_kinds[k[i].kind], (unsigned)k[i].row, n ? (double)(sum / (float)n) : 0.0, (double)want);
        sa1_character_destroy(ctx, id);
    }
}

/* ---- E5 cancel -------------------------------------------------------------------------------------------------------------- */
static void cancel_input_case(sa1_context *ctx, const char *what, float sy, uint32_t bt, uint32_t flags)
{
    sa1_character_id ia = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0), ib = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    sa1_state_ex xb;
    snap_t sa, sb;
    int p;
    run(ctx, ia, 8);
    run(ctx, ib, 8);
    emote_row(ctx, ib, 81, SA1_EMOTE_PLAY_DEFAULT, flags);
    for (p = 0; p < 6; p++) {
        tick(ctx, ia, 0, 0, 0);
        tick(ctx, ib, 0, 0, 0);
    }
    tick(ctx, ia, 0, sy, bt);
    tick(ctx, ib, 0, sy, bt);
    xstate(ctx, ib, &xb);
    snap(ctx, ia, &sa);
    snap(ctx, ib, &sb);
    if (flags & SA1_EMOTE_KEEP_INPUT) {
        sa1em_world_info wi;
        winfo(ctx, ib, &wi);
        CHECK(xb.emote_state != SA1_EMOTE_STATE_NONE && wi.cancels_input == 0 && snap_eq(&sa, &sb),
              "%s with KEEP_INPUT | IN_AIR (the host cancels; the walk leaves the standing mode, which IN_AIR keeps): "
              "still playing after the pair, no input cancel, the state equals the no-emote world's", what);
    } else {
        CHECK(xb.emote_state == SA1_EMOTE_STATE_NONE && snap_eq(&sa, &sb),
              "%s: the queue empty before the pair; that pair's state equals the no-emote world's", what);
    }
    sa1_character_destroy(ctx, ia);
    sa1_character_destroy(ctx, ib);
}

static void cancel_state_case(sa1_context *ctx, const char *what, int how)
{
    sa1_character_id id = mk(ctx, SA1_CHAR_SONIC, 0.5f, how == 3 || how == 4 ? (how == 3 ? MK_GENERAL_OFF : MK_GENERAL) : 0);
    sa1g_world *w;
    sa1_state_ex x0, x1;
    sa1em_world_info wi;
    sa1_status st = SA1_OK;
    if (!id || run(ctx, id, 8) != 0) {
        CHECK(0, "%s: world", what);
        return;
    }
    w = sa1_character_world(ctx, id);
    emote_row(ctx, id, 81, SA1_EMOTE_PLAY_DEFAULT, 0);
    run(ctx, id, 4);
    xstate(ctx, id, &x0);
    if (how == 0) {
        sa1_damage d;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        st = sa1_character_damage(ctx, id, &d);
    } else if (how == 1) {
        call_req r;
        memset(&r, 0, sizeof r);
        r.f[1] = 4.0f;
        st = sa1g_world_call(w, spring_cb, &r);
    } else if (how == 2) {
        /* leaving the ground: a moving floor that drops away - here the host moves him 40 units up between pairs */
        uint32_t t = twp0(w), v;
        float y;
        v = rd32(w, t + T_POS + 4u);
        memcpy(&y, &v, 4);
        y += 40.0f;
        memcpy(&v, &y, 4);
        wr32(w, t + T_POS + 4u, v);
    } else if (how == 3) {
        st = sa1_character_order(ctx, id, SA1_ORDER_SUPER);
    } else {
        st = sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
    }
    tick(ctx, id, 0, 0, 0);
    tick(ctx, id, 0, 0, 0);
    xstate(ctx, id, &x1);
    winfo(ctx, id, &wi);
    CHECK(st == SA1_OK && x0.emote_state != SA1_EMOTE_STATE_NONE && x1.emote_state == SA1_EMOTE_STATE_NONE &&
              wi.cancels_state >= 1u,
          "%s: cancelled (playing before, none after; mode %d -> %d, %u state cancels)", what, (int)x0.mode, (int)x1.mode,
          (unsigned)wi.cancels_state);
    sa1_character_destroy(ctx, id);
}

static void t_cancel(sa1_context *ctx)
{
    printf("== E5 cancel: input before the pair, the gameplay after it\n");
    cancel_input_case(ctx, "stick", 0.6f, 0, 0);
    cancel_input_case(ctx, "jump", 0.0f, SA1_BUTTON_JUMP, 0);
    cancel_input_case(ctx, "action", 0.0f, SA1_BUTTON_ACTION, 0);
    cancel_input_case(ctx, "stick", 0.6f, 0, SA1_EMOTE_KEEP_INPUT | SA1_EMOTE_IN_AIR);
    cancel_state_case(ctx, "hurt (sa1_character_damage)", 0);
    cancel_state_case(ctx, "a spring (the game's spring request 8C07C720)", 1);
    cancel_state_case(ctx, "leaving the ground", 2);
    cancel_state_case(ctx, "the transformation (SA1_ORDER_SUPER, General)", 3);
    cancel_state_case(ctx, "the revert (SA1_ORDER_NORMAL, General)", 4);
}

/* ---- E6 end ------------------------------------------------------------------------------------------------------------------ */
static float end_jump(sa1_context *ctx, uint32_t flags, int *pairs_out)
{
    sa1_character_id id = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    body_t prev, b;
    float jump = -1.0f;
    int p;
    run(ctx, id, 8);
    emote_row(ctx, id, 3, SA1_EMOTE_PLAY_ONCE, flags);
    memset(&prev, 0, sizeof prev);
    for (p = 0; p < 200; p++) {
        sa1_state_ex x;
        tick(ctx, id, 0, 0, 0);
        xstate(ctx, id, &x);
        body(ctx, id, 1.0f, &b);
        if (x.emote_state == SA1_EMOTE_STATE_NONE && p > 0) {
            jump = body_diff(&b, &prev);
            break;
        }
        prev = b;
    }
    *pairs_out = p + 1;
    sa1_character_destroy(ctx, id);
    return jump;
}

static void t_end(sa1_context *ctx)
{
    int pairs = 0, p, loop_len;
    float j;
    sa1_character_id id;
    printf("== E6 end: the trailing stand, NO_TRAIL, STOP_BLEND, STOP_NOW\n");
    j = end_jump(ctx, 0, &pairs);
    CHECK(j >= 0.0f && j < 0.05f, "row 3 once with the trailing stand: ended after %d pairs, end jump %.3f (< 0.05)", pairs,
          (double)j);
    j = end_jump(ctx, SA1_EMOTE_NO_TRAIL, &pairs);
    CHECK(j >= 0.0f && j <= 0.25f, "row 3 with NO_TRAIL: the game's pop %.3f (0.02-0.20 measured) after %d pairs", (double)j,
          pairs);
    /* STOP_BLEND on a loop (row 81, 63 frames at 2 per pair: 32 pairs a cycle) */
    id = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    run(ctx, id, 8);
    emote_row(ctx, id, 81, SA1_EMOTE_PLAY_DEFAULT, 0);
    run(ctx, id, 10);
    sa1_character_emote_stop(ctx, id, SA1_EMOTE_STOP_BLEND);
    loop_len = (int)ceilf(63.0f / row_per_pair(ctx, SA1_CHAR_SONIC, 81)) + 8 + (int)ceilf(32.0f / row_per_pair(ctx, 0, 0));
    {
        int ending = 0;
        for (p = 0; p < 200; p++) {
            sa1_state_ex x;
            tick(ctx, id, 0, 0, 0);
            xstate(ctx, id, &x);
            ending |= x.emote_state == SA1_EMOTE_STATE_ENDING;
            if (x.emote_state == SA1_EMOTE_STATE_NONE) {
                break;
            }
        }
        CHECK(ending && p + 1 <= loop_len + 2, "STOP_BLEND on loop 81: blended into the stand (ENDING seen), ended after %d "
              "pairs (one loop + link + the stand: <= %d)", p + 1, loop_len + 2);
    }
    emote_row(ctx, id, 81, SA1_EMOTE_PLAY_DEFAULT, 0);
    run(ctx, id, 10);
    sa1_character_emote_stop(ctx, id, SA1_EMOTE_STOP_NOW);
    tick(ctx, id, 0, 0, 0);
    {
        sa1_state_ex x;
        sa1g_world *w = sa1_character_world(ctx, id);
        xstate(ctx, id, &x);
        CHECK(x.emote_state == SA1_EMOTE_STATE_NONE && rd32(w, ewp0(w) + E_LIST40) == 0 && rd32(w, ewp0(w) + E_BUF44) == 0,
              "STOP_NOW: the gameplay pose the next pair (queue and blend source empty)");
    }
    sa1_character_destroy(ctx, id);
}

/* ---- E7 air ---------------------------------------------------------------------------------------------------------------- */
static void t_air(sa1_context *ctx)
{
    sa1_character_id ia = mk(ctx, SA1_CHAR_SONIC, 60.0f, 0), ib = mk(ctx, SA1_CHAR_SONIC, 60.0f, 0);
    sa1_status st0, st1;
    char err[128];
    int p, first = -1, land_a = -1, land_b = -1, playing = 0, pairs = 0;
    printf("== E7 air: IN_AIR from 60 units up: falls and lands as without, the queue plays throughout\n");
    if (!ia || !ib || tick(ctx, ia, 0, 0, 0) != 0 || tick(ctx, ib, 0, 0, 0) != 0) {
        CHECK(0, "worlds");
        return;
    }
    st0 = emote_row(ctx, ib, 81, SA1_EMOTE_PLAY_DEFAULT, 0);
    snprintf(err, sizeof err, "%s", sa1_last_error(ctx));
    CHECK(st0 == SA1_ERR_UNSUPPORTED && strcmp(err, "in the air") == 0,
          "without IN_AIR: %s \"%s\" (SA1_ERR_UNSUPPORTED \"in the air\")", sa1_status_string(st0), err);
    st1 = emote_row(ctx, ib, 81, SA1_EMOTE_PLAY_DEFAULT, SA1_EMOTE_IN_AIR);
    for (p = 0; p < 60 && st1 == SA1_OK; p++) {
        snap_t sa, sb;
        sa1_state_ex xa, xb;
        if (tick(ctx, ia, 0, 0, 0) != 0 || tick(ctx, ib, 0, 0, 0) != 0) {
            break;
        }
        snap(ctx, ia, &sa);
        snap(ctx, ib, &sb);
        xstate(ctx, ia, &xa);
        xstate(ctx, ib, &xb);
        if (first < 0 && !snap_eq(&sa, &sb)) {
            first = p;
        }
        if (land_a < 0 && (xa.game_flags & 3u)) {
            land_a = p;
        }
        if (land_b < 0 && (xb.game_flags & 3u)) {
            land_b = p;
        }
        playing += xb.emote_state != SA1_EMOTE_STATE_NONE;
        pairs++;
    }
    CHECK(st1 == SA1_OK && first < 0 && land_a > 0 && land_a == land_b && playing == pairs && pairs == 60,
          "with IN_AIR: %s; identical state in all %d pairs (first difference %d), landed at pair %d / %d, the queue played "
          "%d of %d pairs", sa1_status_string(st1), pairs, first, land_a, land_b, playing, pairs);
    sa1_character_destroy(ctx, ia);
    sa1_character_destroy(ctx, ib);
}

/* ---- E8 first_pair ------------------------------------------------------------------------------------------------------- */
static void t_first_pair(sa1_context *ctx)
{
    sa1_character_id id = mk(ctx, SA1_CHAR_SONIC, 0.0f, 0);    /* on the floor: standing from the first pair */
    sa1em_world_info w0, w1, w2;
    sa1_state_ex x1, x2;
    sa1g_world *w;
    sa1_status st;
    uint32_t jv1 = 0xFFFF;
    int p2;
    char why[SA1EM_WHY_MAX] = "";
    printf("== E8 first_pair: an emote asked before the world's first pair runs after it (never with jvmode != 1)\n");
    if (!id) {
        CHECK(0, "world");
        return;
    }
    st = emote_row(ctx, id, 81, SA1_EMOTE_PLAY_DEFAULT, 0);
    winfo(ctx, id, &w0);
    tick(ctx, id, 0, 0, 0);
    xstate(ctx, id, &x1);
    winfo(ctx, id, &w1);
    w = sa1_character_world(ctx, id);
    if (w != NULL && pwp0(w) != 0) {
        jv1 = rd16(w, pwp0(w) + P_JVMODE);
    }
    for (p2 = 0; p2 < 30; p2++) {
        tick(ctx, id, 0, 0, 0);
        xstate(ctx, id, &x2);
        if (x2.emote_state != SA1_EMOTE_STATE_NONE) {
            break;
        }
    }
    winfo(ctx, id, &w2);
    NOTE("after the first pair: mode %d smode %d flag %X action %d spd %.3f %.3f %.3f step %u", (int)x1.mode, (int)x1.smode,
         (unsigned)x1.game_flags, (int)x1.action, (double)x1.player_speed[0], (double)x1.player_speed[1],
         (double)x1.player_speed[2], (unsigned)x1.step);
    if (w != NULL && sa1em_world_take_note(w, why, sizeof why)) {
        NOTE("refused at the pair start: %s (mode %d)", why, (int)x2.mode);
    }
    CHECK(st == SA1_OK && w0.pending && w0.starts == 0 && x1.emote_state == SA1_EMOTE_STATE_NONE && w1.starts == 0 &&
              w1.pending && jv1 == 1u && x2.emote_state == SA1_EMOTE_STATE_PLAYING && w2.starts == 1 && !w2.pending &&
              w2.refused == 0 && x2.mode == 1,
          "latched (%s, pending %u); after the first pair not started (state %u, jvmode then %u); it waited %u pair(s) "
          "while he landed (the world starts him falling) and started at pair %d (state %u, mode %d, starts %u)",
          sa1_status_string(st), (unsigned)w0.pending, (unsigned)x1.emote_state, (unsigned)jv1,
          (unsigned)w2.waits_settle, p2 + 2, (unsigned)x2.emote_state, (int)x2.mode, (unsigned)w2.starts);

    sa1_character_destroy(ctx, id);
}

/* ---- E9 super ------------------------------------------------------------------------------------------------------------- */
typedef struct bob_t {
    float lo_min, lo_max, hi_min, hi_max;
    uint32_t archives, pairs;
} bob_t;

/* the body's feet / top range over `pairs` pairs while the queue plays */
static void bob(sa1_context *ctx, sa1_character_id id, int pairs, bob_t *o)
{
    int p;
    o->lo_min = o->hi_min = 1e9f;
    o->lo_max = o->hi_max = -1e9f;
    o->archives = 0;
    o->pairs = 0;
    for (p = 0; p < pairs; p++) {
        body_t b;
        if (tick(ctx, id, 0, 0, 0) != 0) {
            return;
        }
        body(ctx, id, 1.0f, &b);
        if (b.draws == 0) {
            continue;
        }
        o->lo_min = fminf(o->lo_min, b.lo);
        o->lo_max = fmaxf(o->lo_max, b.lo);
        o->hi_min = fminf(o->hi_min, b.hi);
        o->hi_max = fmaxf(o->hi_max, b.hi);
        o->archives |= b.archives;
        o->pairs++;
    }
}

static void t_super(sa1_context *ctx)
{
    static const uint32_t curled[4] = { 14, 15, 83, 125 };
    sa1_character_id k8 = mk(ctx, SA1_CHAR_SUPER_SONIC, 0.5f, 0), gen = mk(ctx, SA1_CHAR_SONIC, 0.5f, MK_GENERAL);
    sa1_character_id raw = mk(ctx, SA1_CHAR_SONIC, 0.5f, MK_GENERAL);
    bob_t bk8, bgen, braw, b3k8, b3gen;
    uint32_t i, refused = 0;
    sa1_status st;
    sa1g_world *wr;
    sa1_state_ex x;
    printf("== E9 super: kind 8 (Sonic's rows on the Super body, 130-141 own, curled rows refused) and the General form\n");
    if (!k8 || !gen || !raw || run(ctx, k8, 8) != 0 || run(ctx, gen, 8) != 0 || run(ctx, raw, 8) != 0) {
        CHECK(0, "worlds");
        return;
    }
    for (i = 0; i < 4; i++) {
        refused += emote_row(ctx, k8, curled[i], SA1_EMOTE_PLAY_DEFAULT, 0) == SA1_ERR_INVALID_ARGUMENT;
    }
    CHECK(refused == 4, "kind 8: the curled-body rows 14, 15, 83, 125 refused (%u of 4: SA1_ERR_INVALID_ARGUMENT)",
          (unsigned)refused);
    emote_row(ctx, k8, 3, SA1_EMOTE_PLAY_ONCE, SA1_EMOTE_NO_TRAIL);
    bob(ctx, k8, 20, &b3k8);
    emote_row(ctx, gen, 3, SA1_EMOTE_PLAY_ONCE, SA1_EMOTE_NO_TRAIL);
    bob(ctx, gen, 20, &b3gen);
    run(ctx, k8, 20);
    run(ctx, gen, 20);
    CHECK(b3k8.pairs > 0 && b3k8.archives == 1u, "kind 8 row 3 (Sonic's look-around on the Super body): body archives %X "
          "(archive 0 = SUPERSONIC only)", (unsigned)b3k8.archives);
    CHECK(b3gen.pairs > 0 && b3gen.archives == (1u << 5), "General row 3 (a GT row): body archives %X (archive 5 = "
          "SUPERSONIC in Sonic's set only)", (unsigned)b3gen.archives);
    /* 130 Stationary: kind 8 (its own action at center_height 6.4), the General form through sa1w_world_super_lifted, and
       B_CHAOS7's own action queued raw in a General world (1.0 lower at Sonic's 5.4) */
    st = emote_row(ctx, k8, 130, SA1_EMOTE_PLAY_LOOP, 0);
    bob(ctx, k8, 40, &bk8);
    emote_row(ctx, gen, 130, SA1_EMOTE_PLAY_LOOP, 0);
    bob(ctx, gen, 40, &bgen);
    wr = sa1_character_world(ctx, raw);
    {
        call_req r;
        uint32_t plact = rd32(wr, pwp0(wr) + P_PLACT);
        memset(&r, 0, sizeof r);
        r.a = rdx32(wr, plact + 16u * 130u);       /* GT lives in segment 0x18 */
        {
            uint32_t v = rdx32(wr, plact + 16u * 130u + 12u);
            float racio;
            memcpy(&racio, &v, 4);
            r.f[0] = 2.0f * racio;  /* one advance per pair (Sonic's display): 2 x racio, as the lane's */
        }
        sa1g_world_call(wr, rawq_cb, &r);
        bob(ctx, raw, 40, &braw);
        memset(&r, 0, sizeof r);
        sa1g_world_call(wr, rawq_cb, &r);   /* clear */
    }
    NOTE("130 kind 8: feet %.2f..%.2f top %.2f..%.2f | General (lifted): feet %.2f..%.2f top %.2f..%.2f | B_CHAOS7's own "
         "in a General world: feet %.2f..%.2f top %.2f..%.2f", (double)bk8.lo_min, (double)bk8.lo_max,
         (double)bk8.hi_min, (double)bk8.hi_max, (double)bgen.lo_min, (double)bgen.lo_max, (double)bgen.hi_min,
         (double)bgen.hi_max, (double)braw.lo_min, (double)braw.lo_max, (double)braw.hi_min, (double)braw.hi_max);
    CHECK(st == SA1_OK && bk8.pairs > 30 && bk8.archives == 1u, "kind 8 row 130 (its own motion): played, archives %X",
          (unsigned)bk8.archives);
    CHECK(bgen.pairs > 30 && fabsf(bgen.lo_min - bk8.lo_min) <= 0.02f && fabsf(bgen.lo_max - bk8.lo_max) <= 0.02f &&
              fabsf(bgen.hi_min - bk8.hi_min) <= 0.02f && fabsf(bgen.hi_max - bk8.hi_max) <= 0.02f &&
              fabsf(bgen.lo_min - -1.17f) <= 0.03f && fabsf(bgen.hi_max - 11.64f) <= 0.03f,
          "General row 130 from sa1w_world_super_lifted: feet / top within 0.02 of kind 8's (plan: -1.17..-0.40 / "
          "11.04..11.64)");
    CHECK(braw.pairs > 30 && fabsf((bgen.lo_min - braw.lo_min) - 1.0f) <= 0.03f &&
              fabsf((bgen.hi_max - braw.hi_max) - 1.0f) <= 0.03f,
          "B_CHAOS7's own 130 queued raw in a General world sits %.3f / %.3f lower (the lift 1.0)",
          (double)(bgen.lo_min - braw.lo_min), (double)(bgen.hi_max - braw.hi_max));
    /* the revert cancels (the General form's emote of a Super row) */
    st = sa1_character_order(ctx, gen, SA1_ORDER_NORMAL);
    run(ctx, gen, 2);
    xstate(ctx, gen, &x);
    CHECK(st == SA1_OK && x.emote_state == SA1_EMOTE_STATE_NONE && !(x.equipment & 0x8000u),
          "the revert (SA1_ORDER_NORMAL) cancelled the General form's 130 (state %u, equipment %04X)",
          (unsigned)x.emote_state, (unsigned)x.equipment);
    st = emote_row(ctx, gen, 130, SA1_EMOTE_PLAY_LOOP, 0);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT, "after the revert 130 is not offered (%s: %s)", sa1_status_string(st),
          sa1_last_error(ctx));
    sa1_character_destroy(ctx, k8);
    sa1_character_destroy(ctx, gen);
    sa1_character_destroy(ctx, raw);
}

/* ---- E10 amy ---------------------------------------------------------------------------------------------------------------- */
/* the most body triangles over `pairs` pairs (stops early when the emote ends and `until_end`) */
static uint32_t max_tris(sa1_context *ctx, sa1_character_id id, int pairs, int until_end)
{
    uint32_t m = 0;
    int p;
    for (p = 0; p < pairs; p++) {
        body_t b;
        sa1_state_ex x;
        tick(ctx, id, 0, 0, 0);
        body(ctx, id, 1.0f, &b);
        m = b.tris > m ? b.tris : m;
        xstate(ctx, id, &x);
        if (until_end && x.emote_state == SA1_EMOTE_STATE_NONE) {
            break;
        }
    }
    return m;
}

static uint32_t tris_now(sa1_context *ctx, sa1_character_id id)
{
    body_t b;
    body(ctx, id, 1.0f, &b);
    return b.tris;
}

static void t_amy(sa1_context *ctx)
{
    sa1_character_id id = mk(ctx, SA1_CHAR_AMY, 0.5f, 0);
    uint32_t t0, t_play, t_end, t_np, t_cancel_play, t_cancel_end;
    sa1_state_ex x;
    printf("== E10 amy: an attack row shows the hammer (+66 triangles); its end, a cancel and NO_PROPS do not\n");
    if (!id || run(ctx, id, 8) != 0) {
        CHECK(0, "world");
        return;
    }
    t0 = tris_now(ctx, id);
    emote_row(ctx, id, 81, SA1_EMOTE_PLAY_ONCE, 0);
    t_play = max_tris(ctx, id, 200, 1);
    run(ctx, id, 15);
    t_end = tris_now(ctx, id);
    emote_row(ctx, id, 81, SA1_EMOTE_PLAY_ONCE, SA1_EMOTE_NO_PROPS);
    t_np = max_tris(ctx, id, 200, 1);
    run(ctx, id, 15);
    emote_row(ctx, id, 85, SA1_EMOTE_PLAY_LOOP, 0);
    t_cancel_play = max_tris(ctx, id, 10, 0);
    tick(ctx, id, 0, 0.6f, 0);          /* the stick cancels it before the pair */
    xstate(ctx, id, &x);
    run(ctx, id, 30);
    t_cancel_end = tris_now(ctx, id);
    CHECK(t_play == t0 + 66u && t_end == t0, "row 81 (a_attack1): %u -> %u triangles while it plays, %u after its end "
          "(the stand: %u; +66 = the hammer)", (unsigned)t0, (unsigned)t_play, (unsigned)t_end, (unsigned)t0);
    CHECK(t_np == t0, "row 81 with NO_PROPS: %u triangles (no hammer)", (unsigned)t_np);
    CHECK(t_cancel_play == t0 + 66u && x.emote_state == SA1_EMOTE_STATE_NONE && t_cancel_end == t0,
          "row 85 (a_rollat0) loop: %u while it plays; cancelled by the stick, %u after", (unsigned)t_cancel_play,
          (unsigned)t_cancel_end);
    sa1_character_destroy(ctx, id);
}

/* ---- E11 refusals -------------------------------------------------------------------------------------------------------- */
static int refused_nothing(sa1_context *ctx, sa1_character_id id, sa1_status st, sa1_status want)
{
    sa1g_world *w = sa1_character_world(ctx, id);
    sa1em_world_info wi;
    winfo(ctx, id, &wi);
    return st == want && !wi.pending && wi.state == SA1_EMOTE_STATE_NONE && (w == NULL || rd32(w, ewp0(w) + E_LIST40) == 0);
}

static void t_refusals(sa1_context *ctx)
{
    sa1_character_id so = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0), ta = mk(ctx, SA1_CHAR_TAILS, 0.5f, 0);
    sa1_character_id bi = mk(ctx, SA1_CHAR_BIG, 0.5f, 0), pu = mk(ctx, SA1_CHAR_SONIC, 0.5f, MK_PUPPET);
    uint32_t x, n, i, found = 0, src = SA1_EMOTE_EXTRA;
    sa1_status st;
    sa1_emote e;
    printf("== E11 refusals: entries not offered, a puppet, bad arguments - nothing queued\n");
    if (!so || !ta || !bi || !pu || run(ctx, so, 8) != 0 || run(ctx, ta, 8) != 0 || run(ctx, bi, 8) != 0) {
        CHECK(0, "worlds");
        return;
    }
    x = 0;
    st = emote_ex(ctx, so, 1, &src, &x, SA1_EMOTE_PLAY_DEFAULT, 0, -1);
    CHECK(refused_nothing(ctx, so, st, SA1_ERR_INVALID_ARGUMENT), "Sonic extra 0 (he has none): %s \"%s\"",
          sa1_status_string(st), sa1_last_error(ctx));
    x = 9;
    st = emote_ex(ctx, ta, 1, &src, &x, SA1_EMOTE_PLAY_DEFAULT, 0, -1);
    CHECK(refused_nothing(ctx, ta, st, SA1_ERR_INVALID_ARGUMENT), "Tails extra 9 (past his 9 extras): %s \"%s\"",
          sa1_status_string(st), sa1_last_error(ctx));
    st = emote_row(ctx, so, 88, SA1_EMOTE_PLAY_DEFAULT, 0);
    CHECK(refused_nothing(ctx, so, st, SA1_ERR_INVALID_ARGUMENT), "Sonic row 88 (a stage row, STG07): %s \"%s\"",
          sa1_status_string(st), sa1_last_error(ctx));
    st = emote_row(ctx, bi, 77, SA1_EMOTE_PLAY_DEFAULT, 0);
    CHECK(refused_nothing(ctx, bi, st, SA1_ERR_INVALID_ARGUMENT), "Big row 77 (empty in the game's table): %s \"%s\"",
          sa1_status_string(st), sa1_last_error(ctx));
    st = emote_row(ctx, so, 130, SA1_EMOTE_PLAY_DEFAULT, 0);
    CHECK(refused_nothing(ctx, so, st, SA1_ERR_INVALID_ARGUMENT) && strstr(sa1_last_error(ctx), "ss_wait0") != NULL,
          "Sonic row 130 in a plain world (Super-only): %s \"%s\"", sa1_status_string(st), sa1_last_error(ctx));
    n = sa1_emote_count(ctx, SA1_CHAR_AMY, 0);
    for (i = 0; i < n; i++) {
        sa1_emote_info info;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        if (sa1_emote_get(ctx, SA1_CHAR_AMY, 0, i, &info) == SA1_OK && strcmp(info.internal_name, "ad_dwalk") == 0) {
            found++;
        }
    }
    CHECK(n > 0 && found == 0, "Amy's ad_dwalk (the barrel's own motion: crashed the research probe) is not in her list "
          "(%u entries)", (unsigned)n);
    st = emote_row(ctx, pu, 3, SA1_EMOTE_PLAY_DEFAULT, 0);
    CHECK(st == SA1_ERR_UNSUPPORTED, "a puppet: %s \"%s\"", sa1_status_string(st), sa1_last_error(ctx));
    st = emote_row(ctx, (sa1_character_id)0x7FFF0007, 3, SA1_EMOTE_PLAY_DEFAULT, 0);
    CHECK(st == SA1_ERR_NOT_FOUND, "no such character: %s", sa1_status_string(st));
    /* syntax */
    memset(&e, 0, sizeof e);
    e.struct_size = sizeof e;
    e.link = -1;
    e.count = 0;
    st = sa1_character_emote(ctx, so, &e);
    CHECK(refused_nothing(ctx, so, st, SA1_ERR_INVALID_ARGUMENT), "count 0: %s", sa1_status_string(st));
    e.count = 2;
    e.items[0].source = e.items[1].source = SA1_EMOTE_ROW;
    e.items[0].index = 26;
    e.items[0].play = SA1_EMOTE_PLAY_LOOP;
    e.items[1].index = 3;
    st = sa1_character_emote(ctx, so, &e);
    CHECK(refused_nothing(ctx, so, st, SA1_ERR_INVALID_ARGUMENT), "a loop that is not the last item: %s \"%s\"",
          sa1_status_string(st), sa1_last_error(ctx));
    e.count = 1;
    e.items[0].play = SA1_EMOTE_PLAY_DEFAULT;
    e.items[0].speed = 100.0f;
    st = sa1_character_emote(ctx, so, &e);
    CHECK(refused_nothing(ctx, so, st, SA1_ERR_INVALID_ARGUMENT), "speed 100: %s", sa1_status_string(st));
    e.items[0].speed = 0.0f;
    e.flags = 0x100u;
    st = sa1_character_emote(ctx, so, &e);
    CHECK(refused_nothing(ctx, so, st, SA1_ERR_INVALID_ARGUMENT), "unknown flags: %s", sa1_status_string(st));
    e.flags = 0;
    e.struct_size = 8;
    st = sa1_character_emote(ctx, so, &e);
    CHECK(refused_nothing(ctx, so, st, SA1_ERR_INVALID_ARGUMENT), "struct_size 8: %s", sa1_status_string(st));
    st = sa1_character_emote_stop(ctx, so, 7);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT, "emote_stop how 7: %s", sa1_status_string(st));
    /* the plain words of the gameplay refusals */
    tick(ctx, so, 0, 1.0f, 0);
    tick(ctx, so, 0, 1.0f, 0);
    g_log_error = g_log_warn = g_log_other = 0;
    g_logcap = 1;
    st = emote_row(ctx, so, 3, SA1_EMOTE_PLAY_DEFAULT, 0);
    g_logcap = 0;
    CHECK(refused_nothing(ctx, so, st, SA1_ERR_UNSUPPORTED) && strcmp(sa1_last_error(ctx), "moving") == 0,
          "running: %s \"%s\"", sa1_status_string(st), sa1_last_error(ctx));
    /* round 0.9.5 (board095_perf.md 4, L1): a refusal of the game's state is logged at DEBUG (the host's INFO level drops
       it); sa1_last_error keeps the words */
    CHECK(g_log_error == 0 && g_log_warn == 0, "L1: no SA1_LOG_ERROR / WARN line for the UNSUPPORTED refusal (%u error, %u warn; "
          "0.9.4f: one \"libsa1 error: moving\")", (unsigned)g_log_error, (unsigned)g_log_warn);
    run(ctx, so, 40);
    tick(ctx, so, 0, 0, SA1_BUTTON_JUMP);
    g_log_error = g_log_warn = g_log_other = 0;
    g_logcap = 1;
    st = emote_row(ctx, so, 3, SA1_EMOTE_PLAY_DEFAULT, 0);
    g_logcap = 0;
    CHECK(refused_nothing(ctx, so, st, SA1_ERR_UNSUPPORTED) && strcmp(sa1_last_error(ctx), "in the air") == 0,
          "jumping: %s \"%s\"", sa1_status_string(st), sa1_last_error(ctx));
    CHECK(g_log_error == 0 && g_log_warn == 0, "L1: no SA1_LOG_ERROR / WARN line for \"in the air\" (%u error, %u warn)",
          (unsigned)g_log_error, (unsigned)g_log_warn);
    /* an entry not offered stays an ERROR (a host mismatch, not a refusal) */
    g_log_error = g_log_warn = g_log_other = 0;
    g_logcap = 1;
    st = emote_row(ctx, so, 130, SA1_EMOTE_PLAY_DEFAULT, 0);
    g_logcap = 0;
    CHECK(st == SA1_ERR_INVALID_ARGUMENT && g_log_error == 1 && strstr(sa1_last_error(ctx), "is not available for") != NULL,
          "L1: an entry not offered (row 130 for Sonic): %s, %u SA1_LOG_ERROR line (\"%s\")", sa1_status_string(st),
          (unsigned)g_log_error, sa1_last_error(ctx));
    run(ctx, so, 40);
    {
        sa1_damage d;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        sa1_character_damage(ctx, so, &d);
        run(ctx, so, 2);
        st = emote_row(ctx, so, 3, SA1_EMOTE_PLAY_DEFAULT, 0);
        CHECK(refused_nothing(ctx, so, st, SA1_ERR_UNSUPPORTED) && strcmp(sa1_last_error(ctx), "hurt") == 0,
              "hurt: %s \"%s\"", sa1_status_string(st), sa1_last_error(ctx));
    }
    sa1_character_destroy(ctx, so);
    sa1_character_destroy(ctx, ta);
    sa1_character_destroy(ctx, bi);
    sa1_character_destroy(ctx, pu);
}

/* ---- E12 list --------------------------------------------------------------------------------------------------------------- */
typedef struct tsv_e {
    char name[64], label[96];
    uint32_t frames;
} tsv_e;

/* the offered entries of a (kind, form) list from the research TSVs, in list order; -1: the file is missing */
static int tsv_list(int32_t kind, uint32_t form, tsv_e *out, int max)
{
    static const char *files[9] = { "sonic", NULL, "tails", "knuckles", NULL, "amy", "gamma", "big", "supersonic" };
    char path[1024], line[8192], *cell[40];
    const char *f = (kind == SA1_CHAR_SONIC && form == SA1_EMOTE_FORM_SUPER) ? "supersonic" : files[kind];
    int n = 0, pass, hdr_n = 0, c_act = -1, c_name = -1, c_label = -1, c_wheel = -1, c_frames = -1, c_col = -1, c;
    FILE *fp;
    char hdr[40][32];
    snprintf(path, sizeof path, "%s/emote_actions_%s.tsv", g_tsv, f);
    for (pass = 0; pass < 2; pass++) {
        if ((fp = fopen(path, "rb")) == NULL) {
            return -1;
        }
        hdr_n = 0;
        while (fgets(line, sizeof line, fp)) {
            int nc = 0;
            char *p = line;
            size_t L = strlen(line);
            while (L && (line[L - 1] == '\n' || line[L - 1] == '\r')) {
                line[--L] = 0;
            }
            if (line[0] == '#' || line[0] == 0) {
                continue;
            }
            cell[nc++] = p;
            while ((p = strchr(p, '\t')) != NULL && nc < 40) {
                *p++ = 0;
                cell[nc++] = p;
            }
            if (hdr_n == 0) {
                for (c = 0; c < nc; c++) {
                    snprintf(hdr[c], sizeof hdr[c], "%s", cell[c]);
                    c_act = !strcmp(cell[c], "action") ? c : c_act;
                    c_name = !strcmp(cell[c], "internal_name") ? c : c_name;
                    c_label = !strcmp(cell[c], "label") ? c : c_label;
                    c_wheel = !strcmp(cell[c], "wheel") ? c : c_wheel;
                    c_frames = !strcmp(cell[c], "frames") ? c : c_frames;
                    if (kind == SA1_CHAR_SUPER_SONIC && !strcmp(cell[c], "kind8")) {
                        c_col = c;
                    }
                    if (kind == SA1_CHAR_SONIC && form == SA1_EMOTE_FORM_SUPER && !strcmp(cell[c], "general")) {
                        c_col = c;
                    }
                }
                hdr_n = nc;
                continue;
            }
            if (c_col >= 0) {
                if (pass == 1 || c_col >= nc || !strncmp(cell[c_col], "no", 2)) {
                    continue;
                }
            } else {
                int extra = !strcmp(cell[c_act], "-");
                if (c_wheel >= nc || strcmp(cell[c_wheel], "yes") || extra != pass) {
                    continue;
                }
            }
            if (n < max) {
                snprintf(out[n].name, sizeof out[n].name, "%s", cell[c_name]);
                snprintf(out[n].label, sizeof out[n].label, "%s", cell[c_label]);
                out[n].frames = (uint32_t)strtoul(cell[c_frames], NULL, 10);
            }
            n++;
        }
        fclose(fp);
    }
    (void)hdr;
    return n;
}

/* the drawn (object, motion) of an entry in a world of its kind (the lane's own choice; review R.9) */
static void drawn_of(sa1g_world *w, int32_t kind, const sa1_emote_info *in, uint32_t *obj, uint32_t *mot)
{
    const sa1_char_desc *d = sa1_manifest_char(kind);
    *obj = *mot = 0;
    if (in->source == SA1_EMOTE_EXTRA) {
        const sa1em_entry *ce = sa1em_find(sa1em_list_of(kind, SA1_EMOTE_FORM_NORMAL), SA1_EMOTE_EXTRA, in->index, NULL);
        *obj = d != NULL ? d->main_object : 0u;
        *mot = ce != NULL ? ce->motion : 0u;
    } else {
        uint32_t plact = rd32(w, pwp0(w) + P_PLACT), act = rdx32(w, plact + 16u * in->index), i;
        *obj = rdx32(w, act);
        *mot = rdx32(w, act + 4u);
        if (kind == SA1_CHAR_SUPER_SONIC && in->index < 130u && d != NULL) {
            for (i = 0; i < d->num_subst; i++) {
                if ((d->subst[i][0] & 0x1FFFFFFFu) == (*obj & 0x1FFFFFFFu)) {
                    *obj = d->subst[i][1];
                }
            }
        }
    }
    *obj &= 0x1FFFFFFFu;
    *mot &= 0x1FFFFFFFu;
}

static void t_list(sa1_context *ctx)
{
    size_t li;
    sa1_context *c2 = NULL;
    sa1_host h;
    printf("== E12 list: the catalogue without a world equals the TSVs; frames; dup_of by the drawn object and motion\n");
    sa1_host_defaults(&h);
    h.log = log_cb;
    if (sa1_init(&h, &c2) == SA1_OK) {
        sa1_emote_info info;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        CHECK(sa1_emote_count(c2, SA1_CHAR_SONIC, 0) == 0 && sa1_emote_get(c2, SA1_CHAR_SONIC, 0, 0, &info) ==
              SA1_ERR_NOT_LOADED, "a context without a disc: count 0, get SA1_ERR_NOT_LOADED");
        sa1_shutdown(c2);
    }
    for (li = 0; li < sizeof k_cat / sizeof k_cat[0]; li++) {
        const cat_list *L = &k_cat[li];
        static tsv_e T[256];
        int nt = tsv_list(L->kind, L->form, T, 256);
        uint32_t n = sa1_emote_count(ctx, L->kind, L->form), i, j, name_bad = 0, frames_bad = 0, dup_bad = 0, extras = 0;
        uint32_t (*dk)[2] = (uint32_t(*)[2])calloc(n ? n : 1u, sizeof *dk);
        sa1_character_id id = mk(ctx, L->kind, 0.5f, L->how);
        sa1g_world *w;
        const char *nm = L->how & MK_GENERAL ? "General Super Sonic" : k_kinds[L->kind];
        char first[200] = "";
        if (!id || run(ctx, id, 4) != 0 || dk == NULL) {
            CHECK(0, "%s: world", nm);
            free(dk);
            continue;
        }
        w = sa1_character_world(ctx, id);
        for (i = 0; i < n; i++) {
            sa1_emote_info info;
            sa1_action_info ai;
            memset(&info, 0, sizeof info);
            info.struct_size = sizeof info;
            sa1_emote_get(ctx, L->kind, L->form, i, &info);
            if (nt < 0 || (int)i >= nt || strcmp(info.internal_name, T[i].name) || strcmp(info.label, T[i].label)) {
                name_bad++;
                if (!first[0]) {
                    snprintf(first, sizeof first, "entry %u %s / %s vs TSV %s / %s", (unsigned)i, info.internal_name,
                             info.label, (nt >= 0 && (int)i < nt) ? T[i].name : "-",
                             (nt >= 0 && (int)i < nt) ? T[i].label : "-");
                }
            }
            drawn_of(w, L->kind, &info, &dk[i][0], &dk[i][1]);
            if (info.source == SA1_EMOTE_EXTRA) {
                uint32_t nb = rdx32(w, dk[i][1] + 4u);
                extras++;
                if (nb != info.frames || !(info.flags & SA1_EMOTE_F_EXTRA)) {
                    frames_bad++;
                    if (!first[0]) {
                        snprintf(first, sizeof first, "extra %u %s: table %u, nbFrame on the disc %u", (unsigned)i,
                                 info.internal_name, (unsigned)info.frames, (unsigned)nb);
                    }
                }
            } else {
                memset(&ai, 0, sizeof ai);
                ai.struct_size = sizeof ai;
                sa1_action_info_get(ctx, L->kind, info.index, &ai);
                if (info.frames != ai.num_frames || (nt >= 0 && (int)i < nt && info.frames != T[i].frames)) {
                    frames_bad++;
                    if (!first[0]) {
                        snprintf(first, sizeof first, "row %u %s: frames %u, action info %u, TSV %u",
                                 (unsigned)info.index, info.internal_name, (unsigned)info.frames,
                                 (unsigned)ai.num_frames, (nt >= 0 && (int)i < nt) ? (unsigned)T[i].frames : 0u);
                    }
                }
            }
        }
        for (i = 0; i < n; i++) {
            sa1_emote_info info;
            int32_t want = -1;
            memset(&info, 0, sizeof info);
            info.struct_size = sizeof info;
            sa1_emote_get(ctx, L->kind, L->form, i, &info);
            for (j = 0; j < i && want < 0; j++) {
                if (dk[j][0] == dk[i][0] && dk[j][1] == dk[i][1] && dk[i][0] != 0) {
                    want = (int32_t)j;
                }
            }
            if (info.dup_of != want) {
                dup_bad++;
                if (!first[0]) {
                    snprintf(first, sizeof first, "entry %u %s (row %u): dup_of %d, by the drawn object and motion %d",
                             (unsigned)i, info.internal_name, (unsigned)info.index, (int)info.dup_of, (int)want);
                }
            }
        }
        CHECK(nt == (int)n && n == L->want && name_bad == 0 && frames_bad == 0 && dup_bad == 0,
              "%s: %u entries (%u extras; TSVs %d): %u name / label, %u frame, %u dup_of mismatches%s%s", nm, (unsigned)n,
              (unsigned)extras, nt, (unsigned)name_bad, (unsigned)frames_bad, (unsigned)dup_bad, first[0] ? "; first: " : "",
              first);
        if (L->kind == SA1_CHAR_KNUCKLES) {
            uint32_t p39 = 0, p54 = 0;
            const sa1em_list *kl = sa1em_list_of(SA1_CHAR_KNUCKLES, 0);
            const sa1em_entry *e39 = sa1em_find(kl, SA1_EMOTE_ROW, 39, &p39), *e54 = sa1em_find(kl, SA1_EMOTE_ROW, 54, &p54);
            CHECK(e39 != NULL && e54 != NULL && !strcmp(e39->name, "k_panch0") && !strcmp(e54->name, "k_panch0") &&
                      e54->dup_of != (int16_t)p39 && (dk[p39][0] != dk[p54][0] || dk[p39][1] != dk[p54][1]),
                  "Knuckles rows 39 and 54 are both k_panch0 and NOT duplicates (different motions; review R.9)");
        }
        free(dk);
        sa1_character_destroy(ctx, id);
    }
}

/* ---- E13 with_faces ------------------------------------------------------------------------------------------------------- */
#if EMOTE_TEST_FACES
static void face_block(sa1g_world *w, uint32_t out[4])
{
    uint32_t e = ewp0(w), k;
    for (k = 0; k < 4; k++) {
        out[k] = e ? rd32(w, e + E_FACE + 4u * k) : 0u;
    }
}
#endif

static void t_with_faces(sa1_context *ctx)
{
#if EMOTE_TEST_FACES
    sa1_character_id ia = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0), ib = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    sa1_face_entry fe[2];
    sa1_status s1, s2, s3;
    int p, diff = -1, emoting = 0, faced = 0;
    printf("== E13 with_faces: an emote and a face sequence at once: both play, the face block's timeline is unchanged\n");
    if (!ia || !ib || run(ctx, ia, 8) != 0 || run(ctx, ib, 8) != 0) {
        CHECK(0, "worlds");
        return;
    }
    memset(fe, 0, sizeof fe);
    fe[0].anim = 2;     /* 'C' */
    fe[0].enter = 6;
    fe[0].hold = 40;
    fe[1].anim = 7;     /* 'H' */
    fe[1].hold = 30;
    s1 = sa1_character_face_sequence(ctx, ia, fe, 2, 0);
    s2 = sa1_character_face_sequence(ctx, ib, fe, 2, 0);
    s3 = emote_row(ctx, ib, 81, SA1_EMOTE_PLAY_DEFAULT, 0);
    for (p = 0; p < 90; p++) {
        uint32_t fa[4], fb[4];
        sa1_state_ex xb;
        tick(ctx, ia, 0, 0, 0);
        tick(ctx, ib, 0, 0, 0);
        face_block(sa1_character_world(ctx, ia), fa);
        face_block(sa1_character_world(ctx, ib), fb);
        if (diff < 0 && memcmp(fa, fb, sizeof fa) != 0) {
            diff = p;
        }
        xstate(ctx, ib, &xb);
        emoting += xb.emote_state != SA1_EMOTE_STATE_NONE;
        faced += xb.face_activity != 0u;
    }
    CHECK(s1 == SA1_OK && s2 == SA1_OK && s3 == SA1_OK && diff < 0 && emoting == 90 && faced > 30,
          "face sequence %s / %s, emote %s: the face block equal in all 90 pairs (first difference %d); emoting %d, face "
          "activity %d pairs", sa1_status_string(s1), sa1_status_string(s2), sa1_status_string(s3), diff, emoting, faced);
    sa1_character_destroy(ctx, ia);
    sa1_character_destroy(ctx, ib);
#else
    (void)ctx;
    printf("== E13 with_faces: not run: faces not landed (this snapshot's libsa1.a has no sa1_character_face_sequence)\n");
    NOTE("E13 not run: faces not landed");
#endif
}

/* ---- E14 memcheck ----------------------------------------------------------------------------------------------------------- */
/* plays until the emote is over (bounded); 1 when it ended */
static int until_none(sa1_context *ctx, sa1_character_id id, int max)
{
    int p;
    for (p = 0; p < max; p++) {
        sa1_state_ex x;
        tick(ctx, id, 0, 0, 0);
        xstate(ctx, id, &x);
        if (x.emote_state == SA1_EMOTE_STATE_NONE) {
            return 1;
        }
    }
    return 0;
}

/* one of the memcheck cases on world B (A gets the same input, no emote); k = the case number */
static void mem_case(sa1_context *ctx, sa1_character_id ia, sa1_character_id ib, int32_t kind, uint32_t k)
{
    uint32_t src[2] = { SA1_EMOTE_ROW, SA1_EMOTE_ROW }, idx[2];
    int p;
    uint32_t c = kind == SA1_CHAR_SONIC ? k % 7u : kind == SA1_CHAR_TAILS ? (k % 2u ? 9u : 7u) : (k % 2u ? 8u : 10u);
    switch (c) {
    case 0:     /* a once row with the trailing stand, to its end */
        emote_row(ctx, ib, 3, SA1_EMOTE_PLAY_ONCE, 0);
        break;
    case 1:     /* 82 (body) then 83 (curled body): two groups */
        idx[0] = 82;
        idx[1] = 83;
        emote_ex(ctx, ib, 2, src, idx, SA1_EMOTE_PLAY_ONCE, 0, 8);
        break;
    case 2:     /* two items of one object with link 0: two groups */
        idx[0] = 3;
        idx[1] = 5;
        emote_ex(ctx, ib, 2, src, idx, SA1_EMOTE_PLAY_ONCE, 0, 0);
        break;
    case 3:     /* a loop stopped with a blend */
        emote_row(ctx, ib, 81, SA1_EMOTE_PLAY_LOOP, 0);
        run(ctx, ia, 7);
        run(ctx, ib, 7);
        sa1_character_emote_stop(ctx, ib, SA1_EMOTE_STOP_BLEND);
        break;
    case 4:     /* a loop stopped at once */
        emote_row(ctx, ib, 26, SA1_EMOTE_PLAY_LOOP, 0);
        run(ctx, ia, 5);
        run(ctx, ib, 5);
        sa1_character_emote_stop(ctx, ib, SA1_EMOTE_STOP_NOW);
        break;
    case 5:     /* a start that replaces a running emote, then the stick cancels */
        emote_row(ctx, ib, 26, SA1_EMOTE_PLAY_LOOP, 0);
        run(ctx, ia, 3);
        run(ctx, ib, 3);
        idx[0] = 82;
        idx[1] = 83;
        emote_ex(ctx, ib, 2, src, idx, SA1_EMOTE_PLAY_LOOP, 0, 8);
        run(ctx, ia, 2);
        run(ctx, ib, 2);
        tick(ctx, ia, 0, 0.5f, 0);
        tick(ctx, ib, 0, 0.5f, 0);
        break;
    case 6:     /* CHAIN in one object (Sonic 1 > 2, a loop), then a blended stop */
        emote_row(ctx, ib, 1, SA1_EMOTE_PLAY_ONCE, SA1_EMOTE_CHAIN);
        run(ctx, ia, 45);
        run(ctx, ib, 45);
        sa1_character_emote_stop(ctx, ib, SA1_EMOTE_STOP_BLEND);
        break;
    case 10:    /* Knuckles: 33 (body) then 34 (curled body), link 8: two groups (the probe's fault) */
        idx[0] = 33;
        idx[1] = 34;
        emote_ex(ctx, ib, 2, src, idx, SA1_EMOTE_PLAY_ONCE, 0, 8);
        break;
    case 7:     /* Tails: m_atack_1 (the flight model) with the trailing stand (another object: two groups) */
        emote_row(ctx, ib, 43, SA1_EMOTE_PLAY_ONCE, 0);
        break;
    case 8:     /* Knuckles: CHAIN across objects 18 (flight model) > 19 (body, loop), then a blended stop */
        emote_row(ctx, ib, 18, SA1_EMOTE_PLAY_DEFAULT, SA1_EMOTE_CHAIN);
        run(ctx, ia, 30);
        run(ctx, ib, 30);
        sa1_character_emote_stop(ctx, ib, SA1_EMOTE_STOP_BLEND);
        break;
    default:    /* Tails: m_flyup (flight model) loop, a blended stop: the stand at its wrap */
        emote_row(ctx, ib, 37, SA1_EMOTE_PLAY_LOOP, 0);
        run(ctx, ia, 9);
        run(ctx, ib, 9);
        sa1_character_emote_stop(ctx, ib, SA1_EMOTE_STOP_BLEND);
        break;
    }
    for (p = 0; p < 200; p++) {
        sa1_state_ex x;
        tick(ctx, ia, 0, 0, 0);
        tick(ctx, ib, 0, 0, 0);
        xstate(ctx, ib, &x);
        if (x.emote_state == SA1_EMOTE_STATE_NONE) {
            break;
        }
    }
    run(ctx, ia, 2);
    run(ctx, ib, 2);
}

static void t_memcheck(sa1_context *ctx)
{
    static const int32_t kinds[3] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES };
    static const uint32_t runs[3] = { 140, 30, 30 };
    ga_stats g0, g1;
    int guard = ga_enabled();
    uint32_t total = 0, ki;
    printf("== E14 memcheck: 200 emotes started / cancelled / ended (unblended transitions included): the game heap and "
           "the host heap come back (%s)\n", guard ? "SA1_GUARD_ALLOC" : "the CRT heap: run with SA1_GUARD_ALLOC=1");
    /* the context's own lazily made state (each kind's world image, caches) is made by a first world of each kind, which
       lives until sa1_shutdown: made here, before the count */
    for (ki = 0; ki < 3; ki++) {
        sa1_character_id iw = mk(ctx, kinds[ki], 0.5f, 0);
        run(ctx, iw, 2);
        sa1_character_destroy(ctx, iw);
    }
    ga_stats_get(&g0);
    for (ki = 0; ki < 3; ki++) {
        sa1_character_id ia = mk(ctx, kinds[ki], 0.5f, 0), ib = mk(ctx, kinds[ki], 0.5f, 0);
        sa1g_world *wa, *wb;
        uint32_t a0, b0, a1, b1, k, groups;
        sa1em_world_info wi;
        if (!ia || !ib || run(ctx, ia, 30) != 0 || run(ctx, ib, 30) != 0) {
            CHECK(0, "%s: worlds", k_kinds[kinds[ki]]);
            continue;
        }
        wa = sa1_character_world(ctx, ia);
        wb = sa1_character_world(ctx, ib);
        a0 = heap_total(wa);
        b0 = heap_total(wb);
        for (k = 0; k < runs[ki]; k++) {
            mem_case(ctx, ia, ib, kinds[ki], k);
        }
        run(ctx, ia, 30);
        run(ctx, ib, 30);
        a1 = heap_total(wa);
        b1 = heap_total(wb);
        winfo(ctx, ib, &wi);
        groups = wi.groups;
        total += wi.starts;
        CHECK(b1 - b0 == a1 - a0 && wi.state == SA1_EMOTE_STATE_NONE && rd32(wb, ewp0(wb) + E_BUF44) == 0,
              "%s: %u emotes (%u groups queued, %u entries, %u ends, %u input / %u state cancels, %u stops): the game "
              "heap's free total %u -> %u (the no-emote world: %u -> %u); the blend source freed", k_kinds[kinds[ki]],
              (unsigned)wi.starts, (unsigned)groups, (unsigned)wi.entries, (unsigned)wi.ends, (unsigned)wi.cancels_input,
              (unsigned)wi.cancels_state, (unsigned)wi.stops, (unsigned)b0, (unsigned)b1, (unsigned)a0, (unsigned)a1);
        sa1_character_destroy(ctx, ia);
        sa1_character_destroy(ctx, ib);
    }
    ga_stats_get(&g1);
    CHECK(total >= 200u, "%u emotes started in all", (unsigned)total);
    if (guard) {
        CHECK(g1.live == g0.live && ga_check() == 0, "host heap (guard allocator): %llu live blocks before, %llu after the "
              "worlds were destroyed; canaries intact", (unsigned long long)g0.live, (unsigned long long)g1.live);
    } else {
        NOTE("host heap not checked (no SA1_GUARD_ALLOC)");
    }
}

/* ---- E15 threads ----------------------------------------------------------------------------------------------------------- */
static uint32_t arena_hash(const sa1g_world *w, int masked)
{
    uint32_t h = 2166136261u, i, v, k;
    for (i = 0; i < SA1G_ARENA_SIZE; i += 4u) {
        memcpy(&v, w->ram + i, 4);
        if (masked && v != 0 && ((v >> 24) & 31u) == SA1G_SEG_EXT) {
            v = 0;
        }
        for (k = 0; k < 4u; k++) {
            h = (h ^ ((v >> (8u * k)) & 0xFFu)) * 16777619u;
        }
    }
    return h;
}

/* a fixed emote scenario on a Sonic world: 600 pairs of emotes, cancels, stops and runs; the arena hash */
static uint32_t det_scenario(sa1_context *ctx, uint32_t *masked)
{
    sa1_character_id id = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    uint32_t p, h;
    if (!id) {
        return 0;
    }
    for (p = 0; p < 600; p++) {
        float sy = (p >= 200 && p < 230) ? 0.8f : 0.0f;
        uint32_t src[2] = { SA1_EMOTE_ROW, SA1_EMOTE_ROW }, idx[2] = { 82, 83 };
        if (p == 10 || p == 300) {
            emote_row(ctx, id, 26, SA1_EMOTE_PLAY_LOOP, 0);
        }
        if (p == 90) {
            sa1_character_emote_stop(ctx, id, SA1_EMOTE_STOP_BLEND);
        }
        if (p == 150) {
            emote_ex(ctx, id, 2, src, idx, SA1_EMOTE_PLAY_ONCE, SA1_EMOTE_CHAIN, 8);
        }
        if (p == 260 || p == 420) {
            emote_row(ctx, id, 3, SA1_EMOTE_PLAY_ONCE, SA1_EMOTE_CHAIN);
        }
        if (p == 500) {
            emote_row(ctx, id, 81, SA1_EMOTE_PLAY_LOOP, 0);
        }
        if (tick(ctx, id, 0, sy, p == 350 ? SA1_BUTTON_JUMP : 0u) != 0) {
            break;
        }
    }
    h = arena_hash(sa1_character_world(ctx, id), 0);
    if (masked) {
        *masked = arena_hash(sa1_character_world(ctx, id), 1);
    }
    sa1_character_destroy(ctx, id);
    return h;
}

typedef struct det_job {
    uint32_t hash, masked;
    int ok;
} det_job;

static int new_ctx(sa1_context **out)
{
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_context *c = NULL;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &c) != SA1_OK) {
        return -1;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(c, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) |
                    (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SUPER_SONIC);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS;     /* as the main context: the same world image */
    if (SA1_FAILED(sa1_load_disc(c, g_gdi, &lo)) || scene(c) != 0) {
        sa1_shutdown(c);
        return -1;
    }
    *out = c;
    return 0;
}

#if defined(_WIN32)
static DWORD WINAPI det_thread(LPVOID arg)
#else
static void *det_thread(void *arg)
#endif
{
    det_job *j = (det_job *)arg;
    sa1_context *c = NULL;
    if (new_ctx(&c) == 0) {
        j->hash = det_scenario(c, &j->masked);
        j->ok = j->hash != 0;
        sa1_shutdown(c);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void t_threads(sa1_context *ctx)
{
    det_job j[2];
    uint32_t main_masked = 0, main_hash;
    int i;
#if defined(_WIN32)
    HANDLE th[2];
#else
    pthread_t th[2];
#endif
    printf("== E15 threads: the same emote scenario on the main thread and in two worlds on two threads (x86 = x64: "
           "build_emote.py compares the DETERMINISM lines)\n");
    main_hash = det_scenario(ctx, &main_masked);
    printf("DETERMINISM %08X\n", (unsigned)main_hash);
    printf("DETERMINISM_MASKED %08X\n", (unsigned)main_masked);
    memset(j, 0, sizeof j);
    for (i = 0; i < 2; i++) {
#if defined(_WIN32)
        th[i] = CreateThread(NULL, 0, det_thread, &j[i], 0, NULL);
#else
        pthread_create(&th[i], NULL, det_thread, &j[i]);
#endif
    }
    for (i = 0; i < 2; i++) {
#if defined(_WIN32)
        WaitForSingleObject(th[i], INFINITE);
        CloseHandle(th[i]);
#else
        pthread_join(th[i], NULL);
#endif
    }
    CHECK(main_hash != 0 && j[0].ok && j[1].ok && j[0].hash == j[1].hash && j[0].hash == main_hash,
          "arena hash: main %08X, thread 1 %08X, thread 2 %08X (identical)", (unsigned)main_hash, (unsigned)j[0].hash,
          (unsigned)j[1].hash);
}

/* ---- E16 no_call ------------------------------------------------------------------------------------------------------------ */
static void t_no_call(sa1_context *ctx)
{
    sa1_character_id ia = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0), ib = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    sa1g_world *wa, *wb;
    sa1_state_ex x;
    sa1_emote_info info;
    uint32_t p, ha, hb;
    sa1_status st;
    printf("== E16 no_call: a world without an emote call has no hooks and runs exactly as before\n");
    if (!ia || !ib) {
        CHECK(0, "worlds");
        return;
    }
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_emote_count(ctx, SA1_CHAR_SONIC, 0);
    sa1_emote_get(ctx, SA1_CHAR_SONIC, 0, 3, &info);
    st = sa1_character_emote_stop(ctx, ib, SA1_EMOTE_STOP_NOW);  /* a stop never installs the lane */
    for (p = 0; p < 300; p++) {
        float sy = (p >= 100 && p < 140) ? 1.0f : 0.0f;
        uint32_t b = p == 200 ? SA1_BUTTON_JUMP : 0u;
        tick(ctx, ia, 0, sy, b);
        tick(ctx, ib, 0, sy, b);
        if (p == 250) {
            sa1_character_emote_stop(ctx, ib, SA1_EMOTE_STOP_BLEND);
        }
    }
    wa = sa1_character_world(ctx, ia);
    wb = sa1_character_world(ctx, ib);
    xstate(ctx, ib, &x);
    ha = arena_hash(wa, 0);
    hb = arena_hash(wb, 0);
    CHECK(st == SA1_OK && wb->emote.pre == NULL && wb->emote.post == NULL && wb->emote.fill == NULL &&
              wb->emote.state == 0 && wa->emote.pre == NULL,
          "no emote call (the catalogue and two stops only): w->emote.pre / post / fill NULL, no block");
    CHECK(x.emote_state == 0 && x.emote_item == -1 && x.emote_row == -1 && x.emote_extra == -1 && x.emote_frame == 0.0f,
          "sa1_state_ex: emote_state 0, item / row / extra -1, frame 0");
    CHECK(ha == hb, "the arena after 300 pairs equals a world without any call (%08X)", (unsigned)ha);
    sa1_character_destroy(ctx, ia);
    sa1_character_destroy(ctx, ib);
}

/* ---- E17 heap_low ------------------------------------------------------------------------------------------------------------ */
static void t_heap_low(sa1_context *ctx)
{
    sa1_character_id id = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    sa1g_world *w;
    uint32_t held[256], nh = 0, i, largest0, largest1, k;
    sa1_status st, st2, st3;
    char err[128];
    sa1em_world_info wi, wi2;
    sa1_state_ex x;
    printf("== E17 heap_low: the game heap nearly full: SA1_ERR_LIMIT \"the game's memory is full\", nothing queued\n");
    if (!id || run(ctx, id, 8) != 0) {
        CHECK(0, "world");
        return;
    }
    w = sa1_character_world(ctx, id);
    largest0 = heap_largest(w);
    /* take blocks until the largest free one is below 4 KB + one entry, keeping about 3 KB for the game */
    while (nh < 256u && (k = heap_largest(w)) > 4096u + 48u) {
        call_req r;
        memset(&r, 0, sizeof r);
        r.n = (int32_t)(k > 4096u + 3072u ? k - 3072u : k - 2048u);
        sa1g_world_call(w, malloc_cb, &r);
        if (r.ret == 0) {
            break;
        }
        held[nh++] = r.ret;
    }
    largest1 = heap_largest(w);
    st = emote_row(ctx, id, 81, SA1_EMOTE_PLAY_DEFAULT, 0);
    snprintf(err, sizeof err, "%s", sa1_last_error(ctx));
    winfo(ctx, id, &wi);
    CHECK(st == SA1_ERR_LIMIT && strcmp(err, "the game's memory is full") == 0 && !wi.pending &&
              rd32(w, ewp0(w) + E_LIST40) == 0,
          "largest free block %u -> %u bytes (%u blocks taken): %s \"%s\", nothing latched or queued", (unsigned)largest0,
          (unsigned)largest1, (unsigned)nh, sa1_status_string(st), err);
    /* the pair-start check: latched while there was room, then the heap filled before the pair */
    for (i = 0; i < nh; i++) {
        call_req r;
        memset(&r, 0, sizeof r);
        r.a = held[i];
        sa1g_world_call(w, mfree_cb, &r);
    }
    nh = 0;
    st2 = emote_row(ctx, id, 81, SA1_EMOTE_PLAY_DEFAULT, 0);
    while (nh < 256u && (k = heap_largest(w)) > 4096u + 48u) {
        call_req r;
        memset(&r, 0, sizeof r);
        r.n = (int32_t)(k > 4096u + 3072u ? k - 3072u : k - 2048u);
        sa1g_world_call(w, malloc_cb, &r);
        if (r.ret == 0) {
            break;
        }
        held[nh++] = r.ret;
    }
    tick(ctx, id, 0, 0, 0);
    winfo(ctx, id, &wi2);
    xstate(ctx, id, &x);
    CHECK(st2 == SA1_OK && wi2.refused >= 1u && wi2.starts == 0 && x.emote_state == SA1_EMOTE_STATE_NONE &&
              rd32(w, ewp0(w) + E_LIST40) == 0,
          "latched with room, the heap filled before the pair: refused at the pair start (%u refusals), no fault, nothing "
          "queued", (unsigned)wi2.refused);
    for (i = 0; i < nh; i++) {
        call_req r;
        memset(&r, 0, sizeof r);
        r.a = held[i];
        sa1g_world_call(w, mfree_cb, &r);
    }
    st3 = emote_row(ctx, id, 81, SA1_EMOTE_PLAY_DEFAULT, 0);
    tick(ctx, id, 0, 0, 0);
    xstate(ctx, id, &x);
    CHECK(st3 == SA1_OK && x.emote_state == SA1_EMOTE_STATE_PLAYING && heap_largest(w) + 64u >= largest0 - 4096u,
          "freed again: the same emote plays (state %u; largest free block %u)", (unsigned)x.emote_state,
          (unsigned)heap_largest(w));
    sa1_character_destroy(ctx, id);
}

/* ---- E18 jvmode_guard ------------------------------------------------------------------------------------------------------- */
static uint32_t g_jv_zero_steps;     /* E18: the set_post test hook writes jvmode 0 this many times when a group ends */

static void jv_post(sa1g_world *w, int32_t sub, void *user)
{
    uint32_t e = ewp0(w);
    sa1em_world_info wi;
    (void)sub;
    (void)user;
    sa1em_world_info_get(w, &wi);
    if (g_jv_zero_steps > 0 && e != 0 && rd32(w, e + E_LIST40) == 0 && wi.state != SA1_EMOTE_STATE_NONE &&
        wi.group + 1u < wi.groups_total) {
        wr16(w, pwp0(w) + P_JVMODE, 0);     /* the queue ran empty between groups: the next group must wait */
        g_jv_zero_steps--;
    }
}

static void t_jvmode_guard(sa1_context *ctx)
{
    sa1_character_id id = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    sa1g_world *w;
    sa1em_world_info a, b, c;
    sa1_state_ex x1, x2;
    uint32_t src[2] = { SA1_EMOTE_ROW, SA1_EMOTE_ROW }, idx[2] = { 3, 5 };
    int p;
    printf("== E18 jvmode_guard: no queueing at a pair start (or a group start) that reads jvmode != 1\n");
    if (!id || run(ctx, id, 8) != 0) {
        CHECK(0, "world");
        return;
    }
    w = sa1_character_world(ctx, id);
    /* between pairs: the test writes 0 (as a re-made player task would read) */
    emote_row(ctx, id, 81, SA1_EMOTE_PLAY_DEFAULT, 0);
    wr16(w, pwp0(w) + P_JVMODE, 0);
    winfo(ctx, id, &a);
    tick(ctx, id, 0, 0, 0);
    winfo(ctx, id, &b);
    xstate(ctx, id, &x1);
    tick(ctx, id, 0, 0, 0);
    winfo(ctx, id, &c);
    xstate(ctx, id, &x2);
    CHECK(x1.emote_state == SA1_EMOTE_STATE_NONE && b.waits_jvmode > a.waits_jvmode && b.pending &&
              x2.emote_state == SA1_EMOTE_STATE_PLAYING && c.starts == a.starts + 1u,
          "jvmode 0 at the pair start: kept latched (%u waits), started at the next pair that read 1",
          (unsigned)(b.waits_jvmode - a.waits_jvmode));
    sa1_character_emote_stop(ctx, id, SA1_EMOTE_STOP_NOW);
    run(ctx, id, 4);
    /* between groups (link 0: two groups): the set_post test hook writes 0 when the first group has run out */
    w->set_post = jv_post;
    g_jv_zero_steps = 1;
    winfo(ctx, id, &a);
    emote_ex(ctx, id, 2, src, idx, SA1_EMOTE_PLAY_ONCE, SA1_EMOTE_NO_TRAIL, 0);
    for (p = 0; p < 200; p++) {
        sa1_state_ex x;
        tick(ctx, id, 0, 0, 0);
        xstate(ctx, id, &x);
        if (x.emote_state == SA1_EMOTE_STATE_NONE) {
            break;
        }
    }
    w->set_post = NULL;
    winfo(ctx, id, &b);
    CHECK(g_jv_zero_steps == 0 && b.waits_jvmode > a.waits_jvmode && b.groups == a.groups + 2u && b.ends == a.ends + 1u &&
              g_faults == 0,
          "jvmode 0 when the first group ran out: the second group waited (%u waits), then played; the emote ended "
          "(no hang: build_emote.py runs this binary with a time limit)", (unsigned)(b.waits_jvmode - a.waits_jvmode));
    sa1_character_destroy(ctx, id);
}

/* ---- E19 groups -------------------------------------------------------------------------------------------------------------- */
static void t_groups(sa1_context *ctx)
{
    sa1_character_id id = mk(ctx, SA1_CHAR_TAILS, 0.5f, 0);
    sa1g_world *w;
    sa1em_world_info a, b;
    uint32_t h0, h1, empty = 0, pairs = 0, stand_from = 0, first_end = 0;
    int p, wrapped = 0;
    float prevf = -1.0f;
    printf("== E19 groups: an unblended transition is a new group (a clear and a queue in one post hook)\n");
    if (!id || run(ctx, id, 30) != 0) {
        CHECK(0, "world");
        return;
    }
    w = sa1_character_world(ctx, id);
    h0 = heap_total(w);
    winfo(ctx, id, &a);
    emote_row(ctx, id, 43, SA1_EMOTE_PLAY_ONCE, 0);     /* m_atack_1 (flight model) + the trailing stand (body) */
    for (p = 0; p < 200; p++) {
        sa1_state_ex x;
        tick(ctx, id, 0, 0, 0);
        xstate(ctx, id, &x);
        if (x.emote_state == SA1_EMOTE_STATE_NONE) {
            break;
        }
        pairs++;
        empty += rd32(w, ewp0(w) + E_LIST40) == 0;
        if (x.emote_item == 1 && !stand_from) {
            stand_from = (uint32_t)p + 1u;
        }
        if (x.emote_item == 0) {
            first_end = (uint32_t)p + 1u;
        }
    }
    run(ctx, id, 4);
    h1 = heap_total(w);
    winfo(ctx, id, &b);
    CHECK(b.groups == a.groups + 2u && empty == 0 && stand_from == first_end + 1u && h1 == h0,
          "Tails 43 then the stand: %u groups, the stand from pair %u right after the flight entry's last pair %u, the queue "
          "never empty in %u emote pairs, the game heap's free total %u -> %u", (unsigned)(b.groups - a.groups),
          (unsigned)stand_from, (unsigned)first_end, (unsigned)pairs, (unsigned)h0, (unsigned)h1);
    /* a loop of another object (m_flyup, flight model) with STOP_BLEND: the stand at its wrap */
    winfo(ctx, id, &a);
    emote_row(ctx, id, 37, SA1_EMOTE_PLAY_LOOP, 0);
    run(ctx, id, 5);
    sa1_character_emote_stop(ctx, id, SA1_EMOTE_STOP_BLEND);
    stand_from = 0;
    for (p = 0; p < 200; p++) {
        sa1_state_ex x;
        tick(ctx, id, 0, 0, 0);
        xstate(ctx, id, &x);
        if (x.emote_state == SA1_EMOTE_STATE_NONE) {
            break;
        }
        if (x.emote_item == 0 && prevf >= 0.0f && x.emote_frame < prevf) {
            wrapped = 1;
        }
        if (x.emote_item == 0) {
            prevf = x.emote_frame;
        }
        if (x.emote_item == 1 && !stand_from) {
            stand_from = (uint32_t)p + 1u;
        }
    }
    run(ctx, id, 4);
    winfo(ctx, id, &b);
    CHECK(b.groups == a.groups + 2u && stand_from > 0 && !wrapped && p < 200 && heap_total(w) == h0,
          "Tails 37 loop + STOP_BLEND: the stand queued as its own group at the loop's wrap (pair %u after the stop), the "
          "emote ended (pair %d); heap %u", (unsigned)stand_from, p + 1, (unsigned)heap_total(w));
    sa1_character_destroy(ctx, id);
}

/* ---- E20 hold (round 0.9.4b, SA1_EMOTE_HOLD; objects2_plan.md 11.2) -------------------------------------------------------- */
#define N_MODE   0x00u          /* EV_ACTION_LIST.mode (bit 0 loop) */
#define N_MOTION 0x08u          /* .action.motion */
#define N_NEXT   0x10u          /* .next */
#define N_SPEED  0x14u          /* .speed */

static float rdfx(const sa1g_world *w, uint32_t g)
{
    uint32_t v = rdx32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* the queue head of player 0: its node, mode byte, next, speed, the queue frame (f3C), the blend source and its motion's
   last frame (nbFrame - 1; -1 none) */
typedef struct qhead_t {
    uint32_t node, mode, next, buf;
    float speed, frame, last;
} qhead_t;

static void qhead(const sa1g_world *w, qhead_t *q)
{
    uint32_t e = ewp0(w), mot;
    memset(q, 0, sizeof *q);
    q->last = -1.0f;
    if (!arena(e)) {
        return;
    }
    q->node = rd32(w, e + E_LIST40);
    q->buf = rd32(w, e + E_BUF44);
    q->frame = rdfx(w, e + E_F3C);
    if (!arena(q->node)) {
        q->node = 0;
        return;
    }
    q->mode = rd8(w, q->node + N_MODE);
    q->next = rd32(w, q->node + N_NEXT);
    q->speed = rdfx(w, q->node + N_SPEED);
    mot = rd32(w, q->node + N_MOTION);
    if (mot != 0) {
        q->last = (float)rdx32(w, mot + 4u) - 1.0f;
    }
}

/* one pair in both twin worlds with the same input; 1 when their gameplay states are equal */
static int tick2(sa1_context *ctx, sa1_character_id ia, sa1_character_id ib, float sx, float sy, uint32_t bt)
{
    snap_t a, b;
    tick(ctx, ia, sx, sy, bt);
    tick(ctx, ib, sx, sy, bt);
    snap(ctx, ia, &a);
    snap(ctx, ib, &b);
    return snap_eq(&a, &b);
}

/* pairs until the emote's state is `want` (1..max), 0: not within max (NONE ends the wait early unless want is NONE).
   ia: a twin that gets the same input (0: none); *ident &= the twins' equality */
static int until_state(sa1_context *ctx, sa1_character_id ia, sa1_character_id ib, uint32_t want, int max, int *ident)
{
    int p;
    for (p = 0; p < max; p++) {
        sa1_state_ex x;
        if (ia) {
            int eq = tick2(ctx, ia, ib, 0, 0, 0);
            if (ident) {
                *ident &= eq;
            }
        } else {
            tick(ctx, ib, 0, 0, 0);
        }
        xstate(ctx, ib, &x);
        if (x.emote_state == want) {
            return p + 1;
        }
        if (x.emote_state == SA1_EMOTE_STATE_NONE) {
            return 0;
        }
    }
    return 0;
}

/* emote_row, asked again each pair for up to 60 pairs while the character is not ready ("moving", "busy"); the twin ia (0:
   none) gets the same pairs */
static sa1_status emote_row_ready(sa1_context *ctx, sa1_character_id ia, sa1_character_id ib, uint32_t row, int32_t play,
                                  uint32_t flags, int *ident)
{
    sa1_status st = SA1_ERR_UNSUPPORTED;
    int t;
    for (t = 0; t < 60 && st == SA1_ERR_UNSUPPORTED; t++) {
        st = emote_row(ctx, ib, row, play, flags);
        if (st != SA1_ERR_UNSUPPORTED) {
            break;
        }
        if (ia) {
            int eq = tick2(ctx, ia, ib, 0, 0, 0);
            if (ident) {
                *ident &= eq;
            }
        } else {
            tick(ctx, ib, 0, 0, 0);
        }
    }
    return st;
}

static void t_hold(sa1_context *ctx)
{
    sa1_character_id ia, ib, id;
    sa1g_world *wb, *w;
    sa1_state_ex x;
    sa1em_world_info wi;
    qhead_t q, q0;
    body_t b0, b, ba, prev;
    int p, ident = 1, to_hold, held_ok = 1, ending_at = 0, holding = 0, wraps, waited;
    float drift = 0.0f, pose_vs_stand, jump, jump0, lastf, jump_trail;
    const uint32_t hold_chain = SA1_EMOTE_HOLD | SA1_EMOTE_CHAIN;
    printf("== E20 hold: SA1_EMOTE_HOLD keeps a once emote on its last frame until a cancel, a stop or another emote\n");

    /* (a) Sonic 75 s_kimepose0 (the level-clear pose: a hold row, mtnmode 5, 65 frames) as G-SA1 plays it (CHAIN) + HOLD,
       next to a twin world that gets the same input and never emotes */
    ia = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    ib = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    if (!ia || !ib || run(ctx, ia, 8) != 0 || run(ctx, ib, 8) != 0) {
        CHECK(0, "worlds");
        return;
    }
    wb = sa1_character_world(ctx, ib);
    CHECK(emote_row(ctx, ib, 75, SA1_EMOTE_PLAY_DEFAULT, hold_chain) == SA1_OK,
          "Sonic 75 s_kimepose0 with HOLD | CHAIN accepted");
    to_hold = until_state(ctx, ia, ib, SA1_EMOTE_STATE_HOLDING, 150, &ident);
    xstate(ctx, ib, &x);
    qhead(wb, &q0);
    /* the pair that reached HOLDING drew the step before the last frame (the queue draws f3C, then advances it): the
       held drawing starts with the next pair */
    ident &= tick2(ctx, ia, ib, 0, 0, 0);
    body(ctx, ib, 1.0f, &b0);
    CHECK(to_hold > 0 && q0.last == 64.0f && fabsf(q0.frame - q0.last) < 0.001f && (q0.mode & 1u) && q0.speed == 0.0f &&
              q0.next == 0 && x.emote_row == 75 && x.emote_item == 0 && fabsf(x.emote_frame - 64.0f) < 0.001f,
          "HOLDING after %d pairs: the queue head is row 75 at frame %.3f of its last %.0f (emote_frame %.3f, item %d), "
          "its loop bit set and its speed %.3f, nothing queued behind it (no trailing stand)", to_hold, (double)q0.frame,
          (double)q0.last, (double)x.emote_frame, (int)x.emote_item, (double)q0.speed);
    for (p = 0; p < 90; p++) {
        ident &= tick2(ctx, ia, ib, 0, 0, 0);
        xstate(ctx, ib, &x);
        qhead(wb, &q);
        body(ctx, ib, 1.0f, &b);
        held_ok &= x.emote_state == SA1_EMOTE_STATE_HOLDING && q.node == q0.node && fabsf(q.frame - q0.last) < 0.001f &&
                   fabsf(x.emote_frame - 64.0f) < 0.001f;
        drift = fmaxf(drift, body_diff(&b, &b0));
    }
    body(ctx, ia, 1.0f, &ba);
    pose_vs_stand = body_diff(&b, &ba);
    winfo(ctx, ib, &wi);
    CHECK(held_ok && drift < 0.001f && pose_vs_stand > 0.5f && wi.holds == 1u && wi.hold_phase == 2u,
          "90 more pairs (3 s) without input: still HOLDING on frame 64 of the same queue entry; the body moved %.5f "
          "(< 0.001); it differs from the twin's stand by %.2f (> 0.5: the pose, not the stand)", (double)drift,
          (double)pose_vs_stand);
    ident &= tick2(ctx, ia, ib, 0, 0.6f, 0);
    xstate(ctx, ib, &x);
    qhead(wb, &q);
    winfo(ctx, ib, &wi);
    CHECK(x.emote_state == SA1_EMOTE_STATE_NONE && q.node == 0 && q.buf == 0 && wi.cancels_input == 1u,
          "a stick input: the emote ended before that pair (state NONE; the queue and the blend source empty; %u input "
          "cancel)", (unsigned)wi.cancels_input);
    for (p = 0; p < 60; p++) {
        ident &= tick2(ctx, ia, ib, 0, 0, 0);
    }
    body(ctx, ia, 1.0f, &ba);
    body(ctx, ib, 1.0f, &b);
    xstate(ctx, ib, &x);
    CHECK(x.mode == 1 && body_diff(&b, &ba) < 0.001f,
          "60 pairs after the stick was let go he stands again (mode %d), drawn as the twin that never emoted (difference "
          "%.5f)", (int)x.mode, (double)body_diff(&b, &ba));
    CHECK(ident, "every pair of the emoting world equals its twin's (position, speed, mode, action, flags, rings, rand): "
          "HOLD writes nothing of the gameplay");
    sa1_character_destroy(ctx, ia);
    sa1_character_destroy(ctx, ib);

    /* (b) without HOLD (0.9.4a unchanged): the stand blends in at the pair HOLD held instead; the emote ends by itself */
    id = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    run(ctx, id, 8);
    emote_row(ctx, id, 75, SA1_EMOTE_PLAY_DEFAULT, SA1_EMOTE_CHAIN);
    memset(&prev, 0, sizeof prev);
    jump = 0.0f;
    for (p = 0; p < 200; p++) {
        tick(ctx, id, 0, 0, 0);
        xstate(ctx, id, &x);
        body(ctx, id, 1.0f, &b);
        holding |= x.emote_state == SA1_EMOTE_STATE_HOLDING;
        if (x.emote_state == SA1_EMOTE_STATE_ENDING && !ending_at) {
            ending_at = p + 1;
        }
        if (ending_at && ending_at < p + 1) {
            jump = fmaxf(jump, body_diff(&b, &prev));
        }
        prev = b;
        if (x.emote_state == SA1_EMOTE_STATE_NONE) {
            break;
        }
    }
    winfo(ctx, id, &wi);
    jump_trail = jump;
    CHECK(!holding && ending_at == to_hold && p < 200 && wi.holds == 0u,
          "without HOLD: the trailing stand at pair %d (HOLD held at pair %d), ended by itself after %d pairs, never "
          "HOLDING (the largest change between two pairs while the stand blends in: %.3f)", ending_at, to_hold, p + 1,
          (double)jump_trail);

    /* (c) STOP_BLEND on the held pose: the entry gets back its once mode and speed, the stand blends in from frame 64 */
    w = sa1_character_world(ctx, id);
    emote_row(ctx, id, 75, SA1_EMOTE_PLAY_DEFAULT, hold_chain);
    waited = until_state(ctx, 0, id, SA1_EMOTE_STATE_HOLDING, 150, NULL);
    run(ctx, id, 10);
    body(ctx, id, 1.0f, &prev);
    sa1_character_emote_stop(ctx, id, SA1_EMOTE_STOP_BLEND);
    tick(ctx, id, 0, 0, 0);
    body(ctx, id, 1.0f, &b);
    jump0 = body_diff(&b, &prev);
    jump = jump0;
    prev = b;
    xstate(ctx, id, &x);
    ending_at = x.emote_state == SA1_EMOTE_STATE_ENDING;
    for (p = 1; p < 100 && x.emote_state != SA1_EMOTE_STATE_NONE; p++) {
        tick(ctx, id, 0, 0, 0);
        body(ctx, id, 1.0f, &b);
        jump = fmaxf(jump, body_diff(&b, &prev));
        prev = b;
        xstate(ctx, id, &x);
        ending_at |= x.emote_state == SA1_EMOTE_STATE_ENDING;
    }
    winfo(ctx, id, &wi);
    qhead(w, &q);
    CHECK(waited > 0 && ending_at && x.emote_state == SA1_EMOTE_STATE_NONE &&
              p <= 1 + 8 + (int)ceilf(32.0f / row_per_pair(ctx, SA1_CHAR_SONIC, 0)) + 4 && wi.hold_releases == 1u &&
              jump0 < 0.001f && jump <= jump_trail + 0.01f && q.node == 0 && q.buf == 0,
          "STOP_BLEND on the held pose: the first pair still draws frame 64 (change %.5f), then the stand blends in "
          "from it (ENDING; the largest change between two pairs %.3f, the trailing stand's without HOLD %.3f); ended "
          "after %d pairs; 1 release", (double)jump0, (double)jump, (double)jump_trail, p);

    /* (d) STOP_NOW on a held pose: the gameplay pose at the next pair */
    emote_row(ctx, id, 75, SA1_EMOTE_PLAY_DEFAULT, hold_chain);
    waited = until_state(ctx, 0, id, SA1_EMOTE_STATE_HOLDING, 150, NULL);
    run(ctx, id, 5);
    sa1_character_emote_stop(ctx, id, SA1_EMOTE_STOP_NOW);
    tick(ctx, id, 0, 0, 0);
    xstate(ctx, id, &x);
    qhead(w, &q);
    CHECK(waited > 0 && x.emote_state == SA1_EMOTE_STATE_NONE && q.node == 0 && q.buf == 0,
          "STOP_NOW on a held pose: state NONE the next pair, the queue and the blend source empty");

    /* (e) another emote replaces the held pose (the wheel again): it plays and ends as without the hold */
    emote_row(ctx, id, 75, SA1_EMOTE_PLAY_DEFAULT, hold_chain);
    waited = until_state(ctx, 0, id, SA1_EMOTE_STATE_HOLDING, 150, NULL);
    run(ctx, id, 5);
    emote_row(ctx, id, 3, SA1_EMOTE_PLAY_ONCE, 0);
    tick(ctx, id, 0, 0, 0);
    xstate(ctx, id, &x);
    {
        int playing = x.emote_state == SA1_EMOTE_STATE_PLAYING && x.emote_row == 3;
        int ended = until_state(ctx, 0, id, SA1_EMOTE_STATE_NONE, 200, NULL) > 0;
        winfo(ctx, id, &wi);
        CHECK(waited > 0 && playing && ended && wi.state == SA1_EMOTE_STATE_NONE,
              "another emote (row 3 once, no HOLD) replaces the held pose at the next pair (PLAYING row 3) and ends by "
              "itself");
    }

    /* (f) looping animations are unaffected: a loop row with HOLD loops; a once row played as a LOOP with HOLD loops */
    {
        static const struct { uint32_t row; int32_t play; const char *what; } k_loops[2] = {
            { 81, SA1_EMOTE_PLAY_DEFAULT, "row 81 s_furatsuki0 (a loop row)" },
            { 75, SA1_EMOTE_PLAY_LOOP, "row 75 played as a LOOP" },
        };
        int li;
        for (li = 0; li < 2; li++) {
            float spd0 = -1.0f;
            int bad = 0;
            holding = 0;
            wraps = 0;
            lastf = -1.0f;
            emote_row_ready(ctx, 0, id, k_loops[li].row, k_loops[li].play, hold_chain, NULL);
            for (p = 0; p < 120; p++) {
                tick(ctx, id, 0, 0, 0);
                xstate(ctx, id, &x);
                qhead(w, &q);
                holding |= x.emote_state == SA1_EMOTE_STATE_HOLDING;
                bad |= x.emote_state == SA1_EMOTE_STATE_NONE || !(q.mode & 1u);
                if (spd0 < 0.0f && q.node != 0 && q.buf == 0) {
                    spd0 = q.speed;
                }
                bad |= q.node != 0 && q.buf == 0 && q.speed != spd0;
                if (lastf >= 0.0f && x.emote_frame < lastf) {
                    wraps++;
                }
                lastf = x.emote_frame;
            }
            tick(ctx, id, 0, 0.6f, 0);
            xstate(ctx, id, &x);
            run(ctx, id, 40);
            CHECK(!holding && !bad && wraps >= 2 && spd0 > 0.0f && x.emote_state == SA1_EMOTE_STATE_NONE,
                  "%s with HOLD: loops for 120 pairs (%d wraps, its speed %.2f unchanged, the loop bit kept), never "
                  "HOLDING; a stick input ends it", k_loops[li].what, wraps, (double)spd0);
        }
    }
    winfo(ctx, id, &wi);
    CHECK(wi.holds == 3u && wi.hold_releases == 1u, "Sonic's lane: %u holds reached, %u released by STOP_BLEND",
          (unsigned)wi.holds, (unsigned)wi.hold_releases);
    sa1_character_destroy(ctx, id);

    /* (g) another drawn object (Tails 43 m_atack_1, the flight model) and an extra: held; STOP_BLEND on the flight model
       queues the stand as its own group when the released entry ends */
    id = mk(ctx, SA1_CHAR_TAILS, 0.5f, 0);
    run(ctx, id, 30);
    w = sa1_character_world(ctx, id);
    {
        uint32_t h0 = heap_total(w), n = sa1_emote_count(ctx, SA1_CHAR_TAILS, 0), i, xi = 0xFFFFFFFFu;
        sa1_emote_info info;
        sa1em_world_info a;
        int got, ended;
        winfo(ctx, id, &a);
        emote_row(ctx, id, 43, SA1_EMOTE_PLAY_ONCE, SA1_EMOTE_HOLD);
        got = until_state(ctx, 0, id, SA1_EMOTE_STATE_HOLDING, 100, NULL);
        qhead(w, &q);
        run(ctx, id, 30);
        xstate(ctx, id, &x);
        sa1_character_emote_stop(ctx, id, SA1_EMOTE_STOP_BLEND);
        ended = until_state(ctx, 0, id, SA1_EMOTE_STATE_NONE, 100, NULL) > 0;
        run(ctx, id, 4);
        winfo(ctx, id, &wi);
        CHECK(got > 0 && fabsf(q.frame - q.last) < 0.001f && x.emote_state == SA1_EMOTE_STATE_HOLDING && ended &&
                  wi.groups == a.groups + 2u && heap_total(w) == h0,
              "Tails 43 (the flight model) with HOLD: held on frame %.0f of %.0f for 30 pairs; STOP_BLEND: the stand as "
              "its own group (%u groups), ended; the game heap's free total %u -> %u", (double)q.frame, (double)q.last,
              (unsigned)(wi.groups - a.groups), (unsigned)h0, (unsigned)heap_total(w));
        for (i = 0; i < n && xi == 0xFFFFFFFFu; i++) {
            memset(&info, 0, sizeof info);
            info.struct_size = sizeof info;
            if (sa1_emote_get(ctx, SA1_CHAR_TAILS, 0, i, &info) == SA1_OK && info.source == SA1_EMOTE_EXTRA &&
                info.play == SA1_EMOTE_PLAY_ONCE) {
                xi = i;
            }
        }
        if (xi != 0xFFFFFFFFu) {
            uint32_t s = SA1_EMOTE_EXTRA, ix = info.index;
            emote_ex(ctx, id, 1, &s, &ix, SA1_EMOTE_PLAY_DEFAULT, SA1_EMOTE_HOLD, -1);
            got = until_state(ctx, 0, id, SA1_EMOTE_STATE_HOLDING, 200, NULL);
            qhead(w, &q);
            run(ctx, id, 10);
            xstate(ctx, id, &x);
            CHECK(got > 0 && x.emote_state == SA1_EMOTE_STATE_HOLDING && x.emote_extra == (int32_t)info.index &&
                      fabsf(q.frame - q.last) < 0.001f && q.last == (float)info.frames - 1.0f,
                  "Tails' extra %s (%u frames) with HOLD: held on frame %.0f after %d pairs, still after 10 more",
                  info.internal_name, (unsigned)info.frames, (double)q.frame, got);
            tick(ctx, id, 0.6f, 0, 0);
            run(ctx, id, 30);
        } else {
            CHECK(0, "Tails has a once extra");
        }
    }
    sa1_character_destroy(ctx, id);

    /* (h) a long hold: 40 s without input (the gameplay's own idle timer runs underneath) */
    id = mk(ctx, SA1_CHAR_SONIC, 0.5f, 0);
    run(ctx, id, 8);
    emote_row(ctx, id, 75, SA1_EMOTE_PLAY_DEFAULT, hold_chain);
    waited = until_state(ctx, 0, id, SA1_EMOTE_STATE_HOLDING, 150, NULL);
    {
        int left = -1;
        for (p = 0; p < 1200; p++) {
            tick(ctx, id, 0, 0, 0);
            xstate(ctx, id, &x);
            if (x.emote_state != SA1_EMOTE_STATE_HOLDING) {
                left = p + 1;
                break;
            }
        }
        winfo(ctx, id, &wi);
        CHECK(waited > 0 && left < 0 && wi.cancels_state == 0u,
              "a long hold: %s (the gameplay underneath: mode %d, action %d; %u state cancels)",
              left < 0 ? "still HOLDING after 1,200 pairs (40 s) without input" : "ENDED", (int)x.mode, (int)x.action,
              (unsigned)wi.cancels_state);
    }
    sa1_character_destroy(ctx, id);
}

/* ---- E21 hold_memcheck --------------------------------------------------------------------------------------------------- */
static void t_hold_memcheck(sa1_context *ctx)
{
    static const int32_t kinds[2] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS };
    ga_stats g0, g1;
    int guard = ga_enabled();
    uint32_t ki, total_holds = 0, plays = 0;
    printf("== E21 hold_memcheck: held plays ended every way (the stick, jump, STOP_BLEND, STOP_NOW, another emote): the game "
           "heap and the host heap come back (0.9.4a review R.1; %s)\n",
           guard ? "SA1_GUARD_ALLOC" : "the CRT heap: run with SA1_GUARD_ALLOC=1");
    for (ki = 0; ki < 2; ki++) {
        sa1_character_id iw = mk(ctx, kinds[ki], 0.5f, 0);
        run(ctx, iw, 2);
        sa1_character_destroy(ctx, iw);
    }
    ga_stats_get(&g0);
    for (ki = 0; ki < 2; ki++) {
        sa1_character_id ia = mk(ctx, kinds[ki], 0.5f, 0), ib = mk(ctx, kinds[ki], 0.5f, 0);
        sa1g_world *wa, *wb;
        sa1em_world_info wi;
        uint32_t a0, b0, a1, b1, k, nk = kinds[ki] == SA1_CHAR_SONIC ? 12u : 6u, held = 0;
        int ident = 1;
        if (!ia || !ib || run(ctx, ia, 30) != 0 || run(ctx, ib, 30) != 0) {
            CHECK(0, "%s: worlds", k_kinds[kinds[ki]]);
            continue;
        }
        wa = sa1_character_world(ctx, ia);
        wb = sa1_character_world(ctx, ib);
        a0 = heap_total(wa);
        b0 = heap_total(wb);
        for (k = 0; k < nk; k++) {
            uint32_t row = kinds[ki] == SA1_CHAR_SONIC ? (k % 2u ? 3u : 75u) : (k % 2u ? 56u : 43u);
            uint32_t fl = SA1_EMOTE_HOLD | (k % 3u == 0u ? SA1_EMOTE_CHAIN : 0u);
            int p;
            plays++;
            emote_row_ready(ctx, ia, ib, row, SA1_EMOTE_PLAY_DEFAULT, fl, &ident);
            held += until_state(ctx, ia, ib, SA1_EMOTE_STATE_HOLDING, 200, &ident) > 0;
            for (p = 0; p < 5; p++) {
                ident &= tick2(ctx, ia, ib, 0, 0, 0);
            }
            switch (k % 5u) {
            case 0:     /* the stick */
                ident &= tick2(ctx, ia, ib, 0, 0.6f, 0);
                break;
            case 1:     /* a blended stop: released, the stand */
                sa1_character_emote_stop(ctx, ib, SA1_EMOTE_STOP_BLEND);
                break;
            case 2:     /* at once */
                sa1_character_emote_stop(ctx, ib, SA1_EMOTE_STOP_NOW);
                break;
            case 3:     /* another held emote replaces it, then the jump button */
                emote_row_ready(ctx, ia, ib, kinds[ki] == SA1_CHAR_SONIC ? 3u : 43u, SA1_EMOTE_PLAY_ONCE, SA1_EMOTE_HOLD,
                                &ident);
                held += until_state(ctx, ia, ib, SA1_EMOTE_STATE_HOLDING, 200, &ident) > 0;
                plays++;
                ident &= tick2(ctx, ia, ib, 0, 0, SA1_BUTTON_JUMP);
                break;
            default:    /* the stick sideways */
                ident &= tick2(ctx, ia, ib, 0.6f, 0, 0);
                break;
            }
            for (p = 0; p < 200; p++) {
                sa1_state_ex x;
                ident &= tick2(ctx, ia, ib, 0, 0, 0);
                xstate(ctx, ib, &x);
                if (x.emote_state == SA1_EMOTE_STATE_NONE) {
                    break;
                }
            }
            for (p = 0; p < 60; p++) {
                ident &= tick2(ctx, ia, ib, 0, 0, 0);
            }
        }
        a1 = heap_total(wa);
        b1 = heap_total(wb);
        winfo(ctx, ib, &wi);
        total_holds += wi.holds;
        CHECK(b1 - b0 == a1 - a0 && wi.state == SA1_EMOTE_STATE_NONE && rd32(wb, ewp0(wb) + E_BUF44) == 0 &&
                  rd32(wb, ewp0(wb) + E_LIST40) == 0 && wi.holds == held && ident,
              "%s: %u emotes, %u held (%u releases, %u input / %u state cancels, %u stops): the game heap's free total "
              "%u -> %u (the twin: %u -> %u); queue and blend source empty; every pair equal to the twin's",
              k_kinds[kinds[ki]], (unsigned)wi.starts, (unsigned)wi.holds, (unsigned)wi.hold_releases,
              (unsigned)wi.cancels_input, (unsigned)wi.cancels_state, (unsigned)wi.stops, (unsigned)b0, (unsigned)b1,
              (unsigned)a0, (unsigned)a1);
        sa1_character_destroy(ctx, ia);
        sa1_character_destroy(ctx, ib);
    }
    ga_stats_get(&g1);
    CHECK(total_holds >= 10u, "%u held plays in all (%u emotes)", (unsigned)total_holds, (unsigned)plays);
    if (guard) {
        CHECK(g1.live == g0.live && ga_check() == 0, "host heap (guard allocator): %llu live blocks before, %llu after the "
              "worlds were destroyed; canaries intact", (unsigned long long)g0.live, (unsigned long long)g1.live);
    } else {
        NOTE("host heap not checked (no SA1_GUARD_ALLOC)");
    }
}

/* ---- E22 hold_catalogue: every once entry of every list, held on its last frame ------------------------------------------ */
static void t_hold_catalogue(sa1_context *ctx)
{
    size_t li;
    printf("== E22 hold_catalogue: every once entry of every list with HOLD: held on its motion's last frame, no fault\n");
    for (li = 0; li < sizeof k_cat / sizeof k_cat[0]; li++) {
        const cat_list *L = &k_cat[li];
        uint32_t n = sa1_emote_count(ctx, L->kind, L->form), i, once = 0, ok = 0, bad = 0, nb_diff = 0;
        sa1_character_id id = mk(ctx, L->kind, 0.5f, L->how);
        sa1g_world *w;
        char first_bad[160] = "";
        const char *name = L->how & MK_GENERAL ? "General Super Sonic" : k_kinds[L->kind];
        int maxp_seen = 0;
        if (!id || run(ctx, id, 8) != 0) {
            CHECK(0, "%s: world", name);
            continue;
        }
        w = sa1_character_world(ctx, id);
        for (i = 0; i < n; i++) {
            sa1_emote_info info;
            sa1_status st = SA1_ERR_UNSUPPORTED;
            uint32_t tries, s, x;
            int got, p, held = 1, faulted;
            float per_pair;
            qhead_t q, q2;
            sa1_state_ex xs;
            memset(&info, 0, sizeof info);
            info.struct_size = sizeof info;
            sa1_emote_get(ctx, L->kind, L->form, i, &info);
            if (info.play != SA1_EMOTE_PLAY_ONCE) {
                continue;
            }
            once++;
            s = info.source;
            x = info.index;
            for (tries = 0; tries < 60 && st == SA1_ERR_UNSUPPORTED; tries++) {
                st = emote_ex(ctx, id, 1, &s, &x, SA1_EMOTE_PLAY_DEFAULT, SA1_EMOTE_HOLD, -1);
                if (st == SA1_ERR_UNSUPPORTED) {
                    run(ctx, id, 1);
                }
            }
            if (st != SA1_OK) {
                bad++;
                if (!first_bad[0]) {
                    snprintf(first_bad, sizeof first_bad, "%u %s: %s (%s)", (unsigned)i, info.internal_name,
                             sa1_status_string(st), sa1_last_error(ctx));
                }
                continue;
            }
            per_pair = info.source == SA1_EMOTE_EXTRA ? 2.0f : row_per_pair(ctx, L->kind, info.index);
            faulted = g_faults;
            got = until_state(ctx, 0, id, SA1_EMOTE_STATE_HOLDING,
                              (int)ceilf((float)info.frames / (per_pair > 0.0f ? per_pair : 1.0f)) + 40, NULL);
            maxp_seen = got > maxp_seen ? got : maxp_seen;
            qhead(w, &q);
            for (p = 0; p < 3 && got > 0; p++) {
                tick(ctx, id, 0, 0, 0);
                xstate(ctx, id, &xs);
                qhead(w, &q2);
                held &= xs.emote_state == SA1_EMOTE_STATE_HOLDING && q2.node == q.node && q2.frame == q.frame;
            }
            nb_diff += got > 0 && q.last != (float)info.frames - 1.0f;
            if (g_faults != faulted || got <= 0 || !held || fabsf(q.frame - q.last) >= 0.001f) {
                bad++;
                if (!first_bad[0]) {
                    snprintf(first_bad, sizeof first_bad, "%u %s: %s (frame %.3f of %.0f, after %d pairs)", (unsigned)i,
                             info.internal_name, g_faults != faulted ? "world fault" : got <= 0 ? "never HOLDING" :
                             !held ? "did not stay" : "not on the last frame", (double)q.frame, (double)q.last, got);
                }
                if (g_faults != faulted) {
                    sa1_character_destroy(ctx, id);
                    id = mk(ctx, L->kind, 0.5f, L->how);
                    run(ctx, id, 8);
                    w = sa1_character_world(ctx, id);
                    continue;
                }
            } else {
                ok++;
            }
            sa1_character_emote_stop(ctx, id, SA1_EMOTE_STOP_NOW);
            run(ctx, id, 2);
        }
        CHECK(bad == 0 && ok == once && once > 0,
              "%s: %u once entries held on their last frame (of %u entries; %u failed; the longest wait %d pairs; %u "
              "motions whose nbFrame differs from the catalogue's frames)%s%s", name, (unsigned)ok, (unsigned)n,
              (unsigned)bad, maxp_seen, (unsigned)nb_diff, first_bad[0] ? "; first: " : "", first_bad);
        sa1_character_destroy(ctx, id);
    }
}

/* ---- main ------------------------------------------------------------------------------------------------------------------- */
typedef struct test_case {
    const char *name;
    void (*fn)(sa1_context *);
} test_case;

static const test_case k_tests[] = {
    { "identity", t_identity },   { "catalogue", t_catalogue }, { "cross_object", t_cross_object },
    { "pacing", t_pacing },       { "cancel", t_cancel },       { "end", t_end },
    { "air", t_air },             { "first_pair", t_first_pair }, { "super", t_super },
    { "amy", t_amy },             { "refusals", t_refusals },   { "list", t_list },
    { "with_faces", t_with_faces }, { "memcheck", t_memcheck }, { "threads", t_threads },
    { "no_call", t_no_call },     { "heap_low", t_heap_low },   { "jvmode_guard", t_jvmode_guard },
    { "groups", t_groups },       { "hold", t_hold },           { "hold_memcheck", t_hold_memcheck },
    { "hold_catalogue", t_hold_catalogue },
};

static int selected(const char *only, const char *name)
{
    size_t n = strlen(name);
    const char *p = only;
    if (only == NULL) {
        return 1;
    }
    while ((p = strstr(p, name)) != NULL) {
        if ((p == only || p[-1] == ',') && (p[n] == 0 || p[n] == ',')) {
            return 1;
        }
        p += n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *only = NULL;
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_status st;
    size_t i;
    int a;
    double t0;
    setvbuf(stdout, NULL, _IONBF, 0);
    ga_args(&argc, argv);
    g_tsv = "../sadc/docs/research";
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--verbose")) {
            g_verbose = 1;
        } else if (!strcmp(argv[a], "--only") && a + 1 < argc) {
            only = argv[++a];
        } else if (!strcmp(argv[a], "--tsv") && a + 1 < argc) {
            g_tsv = argv[++a];
        } else if (!strcmp(argv[a], "--list")) {
            for (i = 0; i < sizeof k_tests / sizeof k_tests[0]; i++) {
                printf("%s\n", k_tests[i].name);
            }
            return 0;
        } else {
            g_gdi = argv[a];
        }
    }
    if (g_gdi == NULL) {
        g_gdi = getenv("SA1_TEST_GDI");
    }
    if (g_gdi == NULL) {
        printf("emote_test: needs the disc (GDI argument or SA1_TEST_GDI)\n");
        return 2;
    }
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_INFO;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        printf("sa1_init failed\n");
        return 1;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) |
                    (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SUPER_SONIC);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS;
    st = sa1_load_disc(ctx, g_gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return 1;
    }
    if (scene(ctx) != 0) {
        sa1_shutdown(ctx);
        return 1;
    }
    printf("libsa1 %s (SA1_API_EMOTE %d; faces %s)\n", sa1_version_string(), SA1_API_EMOTE,
           EMOTE_TEST_FACES ? "landed: E13 runs" : "not landed: E13 not run");
    for (i = 0; i < sizeof k_tests / sizeof k_tests[0]; i++) {
        if (!selected(only, k_tests[i].name)) {
            continue;
        }
        t0 = tu_now_ms();
        k_tests[i].fn(ctx);
        printf("  (%s: %.1f s)\n", k_tests[i].name, (tu_now_ms() - t0) / 1000.0);
    }
    sa1_shutdown(ctx);
    printf("emote_test: %d ok, %d failed, %d tick faults\n", g_ok, g_fail, g_faults);
    return g_fail || g_faults ? 1 : 0;
}
