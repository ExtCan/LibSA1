/*
 * sa1_screen.h - the HUD and the 2D screen output (round 0.9.1; include/libsa1.h "screen / HUD"; sadc
 * docs/research/gsa1_070_hud.md F2-F6 with its Review R.5). Internal: not part of the public API.
 *
 *   src/sa1_screen.c          the API (asset layer, plain C99, no world headers): the per-character settings kept by the
 *                             context (sa1_api_ext.screen), sa1_character_set_time, the HUD options of
 *                             sa1_character_set_option (SA1_COPT_CLOCK, SA1_COPT_E102_NO_TIME_LIMIT, SA1_COPT_HUD_PARTS)
 *                             and sa1_character_screen, built from the character's display capture (sa1_capture.h)
 *   src/world/sa1_world_hud.c the world side: the HUD pass (sa1g_world.hud_fn), the clock hold, the time, the record
 *                             ranges of the pass's parts
 *
 * Only opaque world types cross this header.
 */
#ifndef SA1_SCREEN_H
#define SA1_SCREEN_H

#include "sa1_internal.h"

struct sa1g_world;

/* what the host set (sa1s_charcfg.set; each is applied only when it was set: a world whose host never asked runs as the
 * game does) */
#define SA1S_SET_PARTS  0x1u    /* SA1_COPT_HUD_PARTS */
#define SA1S_SET_CLOCK  0x2u    /* SA1_COPT_CLOCK */
#define SA1S_SET_E102   0x4u    /* SA1_COPT_E102_NO_TIME_LIMIT */
#define SA1S_SET_TIME   0x8u    /* sa1_character_set_time */
#define SA1S_SET_ALL    0xFu

/* the per-character settings of the HUD layer, kept by the context by character slot (a record of another id is stale:
 * a destroyed character) */
typedef struct sa1s_charcfg {
    int32_t id;                 /* sa1_character_id (0: unused) */
    uint32_t set;               /* SA1S_SET_* */
    uint32_t parts;             /* SA1_HUD_* */
    int32_t clock;              /* 1: the game clock runs (8C08A5AE), 0: stopped (8C08A5B6) */
    int32_t e102_free;          /* 1: E-102's clock held: his time limit never runs out */
    int32_t time[3];            /* minutes 0..99, seconds 0..59, frames 0..59 */
} sa1s_charcfg;

/* the HUD state of a world for sa1_state_ex (the game's own words) */
typedef struct sa1s_tail {
    int32_t valid;              /* a booted world */
    int32_t min, sec, frames;   /* B_8C752BE1 / BE2 / BE3 */
    uint32_t clock_running;     /* D_8C1BF198 != 0 and not held */
} sa1s_tail;

#if !defined(SA1_NO_WORLD)
/* ---- the world side (src/world/sa1_world_hud.c; outside ticks) ---- */
/* applies `what` (SA1S_SET_*) of cfg to w now (a world that is not booted yet: nothing, its attach applies them) */
sa1_status sa1s_world_apply(sa1_context *ctx, struct sa1g_world *w, const sa1s_charcfg *cfg, uint32_t what);
void sa1s_world_tail(const struct sa1g_world *w, sa1s_tail *out);
/* the record index ranges of the HUD pass's parts (rings, lives, time) in the capture snapshot of frame counter `frame`:
 * 1 when the world's last pass made records with that frame counter (else 0 and empty ranges) */
int sa1s_world_ranges(const struct sa1g_world *w, int32_t frame, uint32_t first[3], uint32_t end[3]);
/* tests (tests/hud_test.c): the game's own single HUD calls instead of the split (the RAM comparison of the review) */
void sa1s_world_test_single(struct sa1g_world *w, int32_t single);
/* tests: run the HUD pass now (inside the world: a replay runs it between its single steps, the way a live pair does
 * between sub step 1 and the frame advance); parts as SA1_COPT_HUD_PARTS. 0 on success */
int sa1s_world_test_pass(struct sa1g_world *w, uint32_t parts);
#endif

/* ---- the API side (src/sa1_screen.c), called by sa1_character.c / sa1_hostobj.c ---- */
/* sa1_character.c ensure_world: the character's settings onto its new world (before its first step) */
sa1_status sa1_screen_world_attach(sa1_context *ctx, sa1_character_id id, struct sa1g_world *w);
/* sa1_hostobj.c sa1_character_set_option: SA1_COPT_CLOCK .. SA1_COPT_HUD_PARTS */
sa1_status sa1_screen_set_option(sa1_context *ctx, sa1_character_id id, uint32_t option, int32_t value);
/* sa1_character.c state_tail: the time fields of sa1_state_ex (a world's; zeros without one) */
void sa1_screen_state_tail(const struct sa1g_world *w, sa1_state_ex *x);
/* sa1_character.c sa1_characters_destroy_all (also sa1_shutdown's): the context's screen state */
void sa1_screen_free_all(sa1_context *ctx);

#endif /* SA1_SCREEN_H */
