/*
 * sa1_face.h - the game's event faces (API SA1_API_FACE, round 0.9.4a; sadc docs/research/faces_wheel_plan.md 4.2-4.4 with
 * its Review R.2-R.6, R.8, R.10, R.11, R.13, faces_events.md 2-8). Internal: shared by the API side src/sa1_face.c (plain
 * C99, the asset-layer flags: no world headers, struct sa1g_world opaque) and the world side src/world/sa1_world_face.c.
 *
 * The API side validates a call (syntax, the character, its kind), builds the character's world (sa1w_character_world_ensure)
 * and hands the request to the world side, which checks the kind's letter mask against the world's own arena and latches it
 * in the world's face block (segment 0x18) or in w->face.persist[] (the mode and the lip-sync level, which survive an image
 * rebuild). The world applies the latched requests at the start of the character's next pair (w->face.pre, sub step 0).
 *
 * The per-kind numbers (sa1_face_info) are computed by one function, sa1f_compute, from any memory view of the game's images:
 * 1ST_READ.BIN / TIKAL_PROG.PRS read from the disc (sa1_face_info_get, no world) or a world's arena (the letter mask of a
 * call). Only numbers leave it; no game bytes are kept.
 */
#ifndef SA1_FACE_H
#define SA1_FACE_H

#include "sa1_internal.h"
#include "sa1_api_world.h"      /* SA1_NO_WORLD (defined there for MSVC), sa1w_character_world_ensure */

struct sa1g_world;

/* ---- the game's addresses (US v1.005; sadc config/symbols.1st_read.txt, symbols.tikal_prog.txt) ---------------------- */
#define SA1F_ADDR_FACETAB   0x8C19C32Cu     /* D_8C19C32C: faceTbl, 31 pointers to {Sint8 time, Sint8 key} steps */
#define SA1F_NUM_ANIMS      31u
#define SA1F_MAX_STEPS      64u             /* a sanity bound on one table sequence (the longest, 'Q', has 18 steps) */
#define SA1F_MAX_NODES      64u             /* a sanity bound on an EV head tree (Big's has 7 nodes) */
#define SA1F_SCRIPT_MAX     64u             /* sa1_character_face: 1..64 letters */

/* one character's EV head: the object and the shape motion its face task creator stores (code_8C0B2338.c:50-58 Sonic,
 * code_8C0BF262.c:257-290 Tails (both parents share the head), code_8C0C8A80.c:199-212 Knuckles, code_8C0DDA00.c:183-196
 * Amy, code_8C0E59C0.c:35-48 Big, overlays/tikal_prog/code_0CB00780.c:334-347 Tikal); 0: the kind has no EV head */
typedef struct sa1f_kind {
    uint32_t ev_head;           /* NJS_OBJECT (Sonic_EV_Head 0x8C1DFBD8 ...) */
    uint32_t motion;            /* NJS_MOTION of NJS_MDATA2 vertex keys (sonic_face_s_head 0x8C1FBBEC ...) */
} sa1f_kind;
/* the EV head of a kind (0..8; NULL for any other); ev_head 0 for Eggman, E-102 and Super Sonic */
const sa1f_kind *sa1f_kind_of(int32_t kind);

/* A read-only view of guest memory: the host bytes of [guest, guest + n), or NULL when they are not all in the view. */
typedef struct sa1f_mem {
    const uint8_t *(*ptr)(const void *user, uint32_t guest, uint32_t n);
    const void *user;
} sa1f_mem;

/* The numbers of sa1_face_info for a kind from a view holding 1ST_READ (and TIKAL_PROG for Tikal): keys = the fewest vertex
 * keys of the EV head's keyed nodes (8C07B080 indexes every keyed node by the same key), letters = bit n when every key of
 * faceTbl[n] is below keys, the loudness ladder by kind (round 0.9.4c, sadc docs/research/phoneme_plan.md 4.6: Sonic 0 4 16,
 * Tails 0 4 7 8, Knuckles 0 4 7 16, Amy 0 7 4 8 16, Big 0 4 5, Tikal 0 5 4 6 7; round 0.9.4a had 0 5 4 6 7 for every kind
 * with 20 or more keys), steps / first / last_key of the 31 sequences. out->struct_size is not touched.
 * SA1_ERR_BAD_FORMAT when the view lacks the data. */
sa1_status sa1f_compute(const sa1f_mem *m, int32_t kind, sa1_face_info *out);

/* A step of the step data the game reads ({Sint8 time, Sint8 key}; time 0 ends a sequence) */
#define SA1F_STEP_TIME_MAX  127

/* round 0.9.4c: the value of SA1_FACE_SAME_PAIR (libsa1.h), kept here so the lane builds whether or not the public header
 * defines it (phoneme_plan.md 5.3: when SAME_PAIR is not shipped the public define is removed and 0x200 is an unknown flag) */
#define SA1F_SAME_PAIR      0x200u
#if defined(SA1_FACE_SAME_PAIR)
typedef char sa1f_same_pair_value_check[(SA1_FACE_SAME_PAIR == SA1F_SAME_PAIR) ? 1 : -1];
#endif

/* ---- the world side (src/world/sa1_world_face.c; not in SA1_NO_WORLD builds) --------------------------------------- */
#if !defined(SA1_NO_WORLD)
/* SA1_CHAR_* of the world (-1: none) */
int32_t sa1wf_world_kind(const struct sa1g_world *w);
/* The letter mask of the world's character, computed from its own arena (sa1f_compute); 0: none (no EV head) */
uint32_t sa1wf_world_letters(const struct sa1g_world *w);
/* Latch a request for the world's next pair start (installs the hooks at the first call; SA1_ERR_OUT_OF_MEMORY when the
 * world's segment 0x18 is full). The arguments are validated by the caller. */
sa1_status sa1wf_latch_mode(sa1_context *ctx, struct sa1g_world *w, uint32_t mode);
sa1_status sa1wf_latch_script(sa1_context *ctx, struct sa1g_world *w, const char *script, uint32_t len, uint32_t flags);
sa1_status sa1wf_latch_sequence(sa1_context *ctx, struct sa1g_world *w, const sa1_face_entry *e, uint32_t count,
                                uint32_t flags);
sa1_status sa1wf_latch_stop(sa1_context *ctx, struct sa1g_world *w);
/* the level quantized by the caller: q = level x 4096 rounded (0..4096), or -1 = off; style SA1_FACE_LEVEL_LADDER (optionally
 * | SA1_FACE_SAME_PAIR, round 0.9.4c) or SA1_FACE_LEVEL_TALK, validated by the caller */
sa1_status sa1wf_latch_level(sa1_context *ctx, struct sa1g_world *w, int32_t q, uint32_t style);
/* round 0.9.4c (SA1_API_FACE_MOUTH; sadc docs/research/phoneme_plan.md 5.4): the host's mouth - the face block's keys key_a /
 * key_b and the weight q (quantized by the caller as the level's: 0..4096, -1 = off), flags 0 or SA1_FACE_SAME_PAIR
 * (validated by the caller). Shares the level's latch (persist[SF_P_LEVEL] / [SF_P_STYLE]; the keys in [SF_P_KEYS]). The keys
 * are checked against the EV head's key count of the world's character (*keys_out = that count, 0 when the world has no EV
 * head data): SA1_ERR_INVALID_ARGUMENT, nothing latched, nothing installed. q = -1 in a world without a face hook installs
 * nothing (as the level). Per frame: no allocation, no log line. */
sa1_status sa1wf_latch_mouth(sa1_context *ctx, struct sa1g_world *w, uint32_t key_a, uint32_t key_b, int32_t q,
                             uint32_t flags, uint32_t *keys_out);
/* round 0.9.4c: the face lane's counters and its block (tests and diagnostics; all 0 without a block) */
typedef struct sa1wf_stats {
    uint32_t pairs, requests, cancels, rearms;  /* pair starts seen, requests applied, PFaceCancel calls, re-arms */
    uint32_t sp_runs, sp_links, sp_skips;       /* SAME_PAIR: step-0 blends run, early links, pairs the guard skipped */
    uint32_t sp_unlinks;                        /* SAME_PAIR: early links put back after the step (Tails' parent switch) */
    uint32_t keys;                              /* the EV head's key count */
    uint32_t block, block_size;                 /* the block in segment 0x18 (guest address, bytes) */
} sa1wf_stats;
void sa1wf_get_stats(const struct sa1g_world *w, sa1wf_stats *out);
#endif

#endif /* SA1_FACE_H */
