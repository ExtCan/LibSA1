/*
 * sa1_world_hud.c - the world side of the HUD (round 0.9.1; include/libsa1.h "screen / HUD", the API in src/sa1_screen.c;
 * sadc docs/research/gsa1_070_hud.md 4.1.1 / 4.1.2, F3 / F4 with its Review R.3.1, R.3.5, R.4.2, R.5.1 and R.5.2).
 *
 *   the pass     sa1g_hud_pass, installed in sa1g_world.hud_fn when the host asks for a HUD part: sa1g_world_pair runs it
 *                after sub step 1 and before the frame advance 8C08A5BE, where the game's frame steps 8C04E95A (action
 *                stages: 8C08A4BA rings / lives, then 8C08A724 time) and 8C04EA4A (adventure fields: 8C08A4BA only) make
 *                their HUD calls. The game's own functions draw (njDrawSprite2D into the display capture: rt_draw.c), split
 *                into parts with the game's own visibility flags (B_8C752BDE lives, B_8C752BDF rings):
 *                  guard = B_8C752BDD >= 0 && 8C0A1218() == 0 (not Twinkle Circuit) && GetPlayerNumber() != 7 (not Big)
 *                  (8C08A4BA's own test, evaluated first with the game's functions: Review R.5.1)
 *                  guard false:                  no 8C08A4BA call (the game's would do nothing; nothing is forced)
 *                  guard, rings and lives off:   BDE = BDF = -1, 8C08A4BA: draws nothing, writes BDE / BDF as the game's
 *                  rings on:                     BDE = -1, BDF = its value, 8C08A4BA: the TIME label / ring icon and the
 *                                                ring count only
 *                  lives on:                     BDE = its value, BDF = -1, 8C08A4BA: the life icon and lives only
 *                  time on (and 8C04C960() != 1, an action stage's frame step): 8C08A724 (the timer, or Sand Hill's score)
 *                Every 8C08A4BA call ends by writing BDE = BDF = BDD (-1 in adventure fields), so after the pass the flags
 *                are the game's in every case (hud_test case 3: Sonic, Big and a running event, guest RAM against the
 *                game's single call). The ring blink phase B_8C7997AC advances inside the rings call only, once per pair
 *                as in the game. Each part's records are noted (the capture header's count before and after the call,
 *                read from the runtime's capture block in the world's pcdata) for sa1_character_screen's parts.
 *   the clock    SA1_COPT_CLOCK: the game's own 8C08A5AE (on) / 8C08A5B6 (off), once. Worlds boot with the clock stopped
 *                (sa1_world_boot.c: 8C08A5B6), as the reference simulator runs.
 *   E-102        SA1_COPT_E102_NO_TIME_LIMIT on an E-102 world: hold_clock, read by the step tail (sa1_world_tick.c): the
 *                clock's switch D_8C1BF198 = 0 right before every 8C08A5CC, so his countdown never moves and game code
 *                that starts the clock again (8C06132A) cannot run it. A one-time B_8C7A2EAA = 0 is not enough: a lock-on
 *                kill's time bonus re-arms it (8C0D4C30, 8C0D51FC; Review R.3.5); the time never reaches 00:00:00 while it
 *                is held, so the time-up death 8C0D23E4 never comes. Side effect, as with today's stopped clock: the
 *                score-chain window B_8C7997A0 is not counted down by 8C08A5CC either.
 *   the time     sa1_character_set_time: 8C08AB20(minutes, seconds, frames) (code_8C08A71E.c), clamped to 0..99 / 0..59 /
 *                0..59. E-102's 00:00:00 is his time-up: a respawn that carried it would die again at once (Review R.4.2,
 *                measured: part Z, death at pair 29); the game restarts the act at 03:00:00 (8C08A580), so 00:00:00 gives
 *                03:00:00 for an E-102 world.
 * Worlds whose host never asked for any of it (the reference replays, every other test) run unchanged: hud_fn NULL, no
 * hold, no write.
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_capture.h"
#include "sa1_screen.h"

#include <stddef.h>
#include <string.h>

#define HUD_FN_RINGS_LIVES  0x8C08A4BAu     /* the common HUD wrapper (code_8C08A4BA.c) */
#define HUD_FN_TIME         0x8C08A724u     /* the timer (code_8C08A71E.c) */
#define HUD_FN_TWINKLE      0x8C0A1218u     /* 1 in Twinkle Circuit (level 35) */
#define HUD_FN_PLAYER_NO    0x8C04F698u     /* GetPlayerNumber */
#define HUD_FN_SET_TIME     0x8C08AB20u     /* (Sint8 min, Sint8 sec, Sint8 frames) */
#define HUD_ADDR_BDD        0x8C752BDDu     /* Sint8: HUD visibility (all) */
#define HUD_ADDR_BDE        0x8C752BDEu     /* Sint8: lives */
#define HUD_ADDR_BDF        0x8C752BDFu     /* Sint8: rings */
#define HUD_ADDR_TIME       0x8C752BE0u     /* the word of B_8C752BE0 (lives) .. BE3 (frames) */
#define HUD_EXT_GUEST       0x98000000u     /* segment 0x18: R4 pcdata (the runtime's capture block lives there) */

/* ---- the runtime's capture header (rt_draw.c RT_CAP_HDR, sa1_capture.h sa1_cap_hdr) -------------------------------- */
static const uint8_t *hud_cap_header(sa1g_world *w)
{
    sa1g_hud *h = &w->hud;
    size_t off, end;
    if (h->cap_block == 1u) {
        return NULL;
    }
    if (h->cap_block != 0u) {
        return w->ext + (h->cap_block - HUD_EXT_GUEST);
    }
    h->cap_block = 1u;
    end = w->pcdata_size;
    if (w->ext == NULL || end > w->ext_used || end < sizeof(sa1_cap_hdr)) {
        return NULL;
    }
    for (off = 0; off + sizeof(sa1_cap_hdr) <= end; off += 4u) {
        const uint8_t *p = w->ext + off;
        sa1_cap_hdr hd;
        if (sa1_rd32(p) != SA1_CAP_MAGIC0) {
            continue;
        }
        memcpy(&hd, p, sizeof hd);
        if (hd.magic[1] == SA1_CAP_MAGIC1 && hd.magic[2] == SA1_CAP_MAGIC2 && hd.magic[3] == SA1_CAP_MAGIC3 &&
            hd.version == SA1_CAP_VERSION && hd.hdr_size == sizeof(sa1_cap_hdr) && hd.rec_size == sizeof(sa1_cap_rec)) {
            h->cap_block = HUD_EXT_GUEST + (uint32_t)off;
            return p;
        }
    }
    return NULL;
}

/* the records of the current pair so far: the header's count when its frame counter is the pair's (rt_cap_new restarts
 * the buffer at the first record of a new frame counter value), else 0 */
static uint32_t hud_cap_count(sa1g_world *w)
{
    const uint8_t *p = hud_cap_header(w);
    uint32_t frame, count, cap;
    if (p == NULL) {
        return 0;
    }
    frame = sa1_rd32(p + offsetof(sa1_cap_hdr, frame));
    count = sa1_rd32(p + offsetof(sa1_cap_hdr, count));
    cap = sa1_rd32(p + offsetof(sa1_cap_hdr, capacity));
    if (frame != *(const uint32_t *)SA1_GV(SA1G_ADDR_FRAME_COUNTER)) {
        return 0;
    }
    return count < cap ? count : cap;
}

/* ---- the pass (inside the world) ---------------------------------------------------------------------------------- */
static void hud_part(sa1g_world *w, uint32_t k, uint32_t fn)
{
    w->hud.first[k] = hud_cap_count(w);
    sa1g_call_dc(fn);
    w->hud.end[k] = hud_cap_count(w);
    if (w->hud.end[k] < w->hud.first[k]) {
        w->hud.end[k] = w->hud.first[k];
    }
}

void sa1g_hud_pass(sa1g_world *w)
{
    sa1g_hud *h = &w->hud;
    int8_t *bdd = (int8_t *)SA1_GV(HUD_ADDR_BDD), *bde = (int8_t *)SA1_GV(HUD_ADDR_BDE);
    int8_t *bdf = (int8_t *)SA1_GV(HUD_ADDR_BDF);
    int8_t e = *bde, f = *bdf;
    int guard;
    uint32_t k;
    h->pass_pair = w->pairs + 1u;
    h->pass_frame = *(const int32_t *)SA1_GV(SA1G_ADDR_FRAME_COUNTER);
    for (k = 0; k < SA1G_HUD_NPARTS; k++) {
        h->first[k] = h->end[k] = 0;
    }
    h->passes++;
    if (h->single) {
        /* tests: the game's frame step tail as it is (8C04E95A; 8C04EA4A has no 8C08A724) */
        sa1g_call_dc(HUD_FN_RINGS_LIVES);
        if (sa1g_call_dc_i32(SA1G_FN_LEVEL_TYPE) != 1) {
            sa1g_call_dc(HUD_FN_TIME);
        }
        return;
    }
    guard = *bdd >= 0 && sa1g_call_dc_i32(HUD_FN_TWINKLE) == 0 && sa1g_call_dc_i32(HUD_FN_PLAYER_NO) != 7;
    if (guard) {
        if (!(h->parts & (SA1_HUD_RINGS | SA1_HUD_LIVES))) {
            *bde = -1;
            *bdf = -1;
            sa1g_call_dc(HUD_FN_RINGS_LIVES);   /* draws nothing; writes BDE / BDF as the game's call does */
        }
        if (h->parts & SA1_HUD_RINGS) {
            *bde = -1;
            *bdf = f;
            hud_part(w, 0, HUD_FN_RINGS_LIVES);
        }
        if (h->parts & SA1_HUD_LIVES) {
            *bde = e;
            *bdf = -1;
            hud_part(w, 1, HUD_FN_RINGS_LIVES);
        }
    }
    if ((h->parts & SA1_HUD_TIME) && sa1g_call_dc_i32(SA1G_FN_LEVEL_TYPE) != 1) {
        hud_part(w, 2, HUD_FN_TIME);
    }
}

/* ---- settings (outside ticks) ------------------------------------------------------------------------------------- */
typedef struct hud_set_run {
    const sa1s_charcfg *cfg;
    uint32_t what;
} hud_set_run;

static void hud_set_cb(sa1g_world *w, void *user)
{
    const hud_set_run *r = (const hud_set_run *)user;
    const sa1s_charcfg *c = r->cfg;
    if ((r->what & SA1S_SET_TIME) && (c->set & SA1S_SET_TIME)) {
        int32_t m = c->time[0], s = c->time[1], fr = c->time[2];
        m = m < 0 ? 0 : m > 99 ? 99 : m;
        s = s < 0 ? 0 : s > 59 ? 59 : s;
        fr = fr < 0 ? 0 : fr > 59 ? 59 : fr;
        if (w->chr == 6 && m == 0 && s == 0 && fr == 0) {
            m = 3;      /* E-102's time-up: the game's restart time (8C08A580), not a death loop */
        }
        ((void (*)(int8_t, int8_t, int8_t))sa1_fn(SA1_GFN_C(HUD_FN_SET_TIME), 0u))((int8_t)m, (int8_t)s, (int8_t)fr);
    }
    if ((r->what & SA1S_SET_CLOCK) && (c->set & SA1S_SET_CLOCK)) {
        sa1g_call_dc(c->clock ? SA1G_FN_CLOCK_ON : SA1G_FN_CLOCK_OFF);
    }
}

sa1_status sa1s_world_apply(sa1_context *ctx, struct sa1g_world *w, const sa1s_charcfg *cfg, uint32_t what)
{
    hud_set_run run;
    (void)ctx;
    if (w == NULL || cfg == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->ram == NULL || !w->booted) {
        return SA1_OK;          /* the attach of the world's build applies them */
    }
    if (w->dead) {
        return w->dead_status;
    }
    if ((what & SA1S_SET_PARTS) && (cfg->set & SA1S_SET_PARTS)) {
        w->hud.parts = cfg->parts & (SA1_HUD_RINGS | SA1_HUD_LIVES | SA1_HUD_TIME);
        w->hud_fn = w->hud.parts != 0 ? sa1g_hud_pass : NULL;
    }
    if ((what & SA1S_SET_E102) && (cfg->set & SA1S_SET_E102)) {
        w->hud.hold_clock = cfg->e102_free && w->chr == 6;
    }
    run.cfg = cfg;
    run.what = what & (SA1S_SET_TIME | SA1S_SET_CLOCK);
    if (!(run.what & cfg->set)) {
        return SA1_OK;
    }
    return sa1g_world_call(w, hud_set_cb, &run);
}

void sa1s_world_tail(const struct sa1g_world *w, sa1s_tail *out)
{
    uint32_t t = 0, sw = 0;
    memset(out, 0, sizeof *out);
    if (w == NULL || w->ram == NULL || !w->booted || sa1g_world_rd32(w, HUD_ADDR_TIME, &t) != 0 ||
        sa1g_world_rd32(w, SA1G_ADDR_CLOCK_SWITCH, &sw) != 0) {
        return;
    }
    out->valid = 1;
    out->min = (int8_t)(t >> 8);
    out->sec = (int8_t)(t >> 16);
    out->frames = (int8_t)(t >> 24);
    out->clock_running = sw != 0 && !w->hud.hold_clock;
}

int sa1s_world_ranges(const struct sa1g_world *w, int32_t frame, uint32_t first[3], uint32_t end[3])
{
    uint32_t k;
    for (k = 0; k < SA1G_HUD_NPARTS; k++) {
        first[k] = end[k] = 0;
    }
    if (w == NULL || w->hud.pass_pair == 0 || w->hud.pass_frame != frame) {
        return 0;
    }
    for (k = 0; k < SA1G_HUD_NPARTS; k++) {
        first[k] = w->hud.first[k];
        end[k] = w->hud.end[k];
    }
    return 1;
}

void sa1s_world_test_single(struct sa1g_world *w, int32_t single)
{
    if (w != NULL) {
        w->hud.single = single != 0;
    }
}

static void hud_test_pass_cb(sa1g_world *w, void *user)
{
    (void)user;
    sa1g_hud_pass(w);
}

int sa1s_world_test_pass(struct sa1g_world *w, uint32_t parts)
{
    uint32_t keep;
    sa1_status st;
    if (w == NULL) {
        return -1;
    }
    keep = w->hud.parts;
    w->hud.parts = parts & (SA1_HUD_RINGS | SA1_HUD_LIVES | SA1_HUD_TIME);
    st = sa1g_world_call(w, hud_test_pass_cb, NULL);
    w->hud.parts = keep;
    return st == SA1_OK ? 0 : -1;
}
