/*
 * sa1_emote.h - emotes (round 0.9.4a, SA1_API_EMOTE; sadc docs/research/faces_wheel_plan.md 4.5, 5.3, 6.3 with its
 * Review R.1, R.2, R.7-R.9, R.13-R.17; emote_wheel_lib.md 2-5). Internal: the header between the asset-layer side
 * (src/sa1_emote.c: the public API, plain C99, no world headers) and the world side (src/world/sa1_world_emote.c: the
 * lane's hooks inside the world), plus the generated catalogue (src/sa1_emote_names.c, tools/gen_emote_names.py).
 *
 * Only opaque world types cross this header (review R.13): the src C files compile with -std=c99 -Iinclude -Isrc and
 * never see world/sa1_world.h (it needs SADC's sa1/guest.h).
 */
#ifndef SA1_EMOTE_H
#define SA1_EMOTE_H

#include "sa1_internal.h"
#include "sa1_api_world.h"

/* ---- the catalogue (src/sa1_emote_names.c, GENERATED: names and numbers only) ---------------------------------------- */
typedef struct sa1em_entry {
    uint8_t     source;         /* SA1_EMOTE_ROW / SA1_EMOTE_EXTRA */
    uint8_t     cls;            /* SA1_EMOTE_CLASS_* */
    uint8_t     flags;          /* SA1_EMOTE_F_* */
    uint16_t    index;          /* the action row, or the extra's index among the kind's offered extras */
    int16_t     dup_of;         /* the list position of the first entry with the same drawn object and motion, -1 none */
    uint16_t    frames;         /* extras: NJS_MOTION.nbFrame (review R.2); rows: 0 (sa1_action_info_get) */
    uint32_t    motion;         /* extras: the motion's DC address (on the kind's main object); rows: 0 */
    const char *name;           /* the internal name ("s_kimepose0") */
    const char *label;          /* the SA Tools label */
} sa1em_entry;

typedef struct sa1em_list {
    int32_t            kind;    /* SA1_CHAR_* */
    uint32_t           form;    /* SA1_EMOTE_FORM_* (kind 8: 0, its only list) */
    uint32_t           count;
    const sa1em_entry *e;
} sa1em_list;

/* round 0.9.9a: sa1em_entry.flags of an extra that loops by default (the rows have their play mode from the character set): the middle of a begin / loop / end triple */
#define SA1EM_F_LOOP 0x40u

/* the list of a character kind and form (kind 8: any form), NULL: none (Eggman, Tikal, a form a kind does not have) */
const sa1em_list *sa1em_list_of(int32_t kind, uint32_t form);

/* the entry of a list by (source, index), its list position in *pos; NULL: not offered */
const sa1em_entry *sa1em_find(const sa1em_list *l, uint32_t source, uint32_t index, uint32_t *pos);

/* ---- the world side (src/world/sa1_world_emote.c; not in SA1_NO_WORLD builds) --------------------------------------- */
#define SA1EM_WHY_MAX 96u

/* the character's kind and current form in its world (FORM_SUPER: kind 0 with the General look on; kind 8: 0) */
void sa1em_world_form(const struct sa1g_world *w, int32_t *kind, uint32_t *form);

/* the gameplay check of a start against the world's state now (outside ticks): SA1_OK, or SA1_ERR_UNSUPPORTED with the
 * plain-word reason ("in the air", "hurt", "carrying something", "on the board", "moving", "busy"), or SA1_ERR_LIMIT
 * ("the game's memory is full"); SA1_PARTIAL: the world has no player yet (its first pair has not run: the start waits
 * for it). `entries`: the queue entries the start needs at most (the heap headroom, review R.7) */
sa1_status sa1em_world_check(const struct sa1g_world *w, const sa1_emote *e, uint32_t entries, char *why, size_t n);

/* round 0.9.9, a follower's world (fw_ai_exec, inside the world): 1 while an emote plays or waits to start, so the AI's output is dropped */
int sa1em_world_wants_hold(const struct sa1g_world *w);

/* the latch (applied at the world's next pair start; the last call before a pair wins). Installs the lane's hooks at the
 * first call for a world. `early`: the call came before the world's first pair (sa1em_world_check said SA1_PARTIAL): the
 * request then also waits while the character settles (a new world starts falling onto its floor: "busy" / "in the
 * air" keep it latched instead of refusing it). SA1_ERR_OUT_OF_MEMORY: no room for the block in segment 0x18. */
sa1_status sa1em_world_latch(sa1_context *ctx, struct sa1g_world *w, const sa1_emote *e, int early);
sa1_status sa1em_world_stop(sa1_context *ctx, struct sa1g_world *w, uint32_t how);

/* a refusal or warning the world recorded at a pair start since the last call (the world cannot log from inside a pair):
 * returns 1 and the text, 0 none; clears it */
int sa1em_world_take_note(struct sa1g_world *w, char *why, size_t n);

/* tests and diagnostics: the lane's counters of a world (zero when it never had an emote call) */
typedef struct sa1em_world_info {
    uint32_t active;            /* the lane's block exists */
    uint32_t state;             /* SA1_EMOTE_STATE_* */
    uint32_t starts, ends, cancels_input, cancels_state, stops, refused, dropped;
    uint32_t groups;            /* groups queued (review R.1: one clear + queue per unblended transition) */
    uint32_t entries;           /* queue entries made (8C04B4D2 / 8C04B528 calls) */
    uint32_t waits_jvmode;      /* pair starts that kept a request latched because jvmode was not 1 (review R.8) */
    uint32_t waits_settle;      /* pair starts that kept an early request latched while the new character settled */
    uint32_t heap_largest;      /* the game heap's largest free block at the last check (bytes) */
    uint32_t pending;           /* a start or stop is latched */
    uint32_t group, groups_total;   /* the running emote: its group being played (0..), its groups */
    /* round 0.9.4b (SA1_EMOTE_HOLD): holds reached (the last frame held), holds released by a STOP_BLEND, the phase now
       (0 none, 1 stepping onto the last frame, 2 held) */
    uint32_t holds, hold_releases, hold_phase;
} sa1em_world_info;
void sa1em_world_info_get(const struct sa1g_world *w, sa1em_world_info *out);

/* the largest free block of the game heap's free list (B_8C79F03C, as sa1_world_set.c st_heap_largest) */
uint32_t sa1em_world_heap_largest(const struct sa1g_world *w);

/* ---- round 100-EV (SA1_API_EMOTE_EVENT; sadc docs/research/emotes_ev.md 3.3, 3.4, 6): a motion copied into a world ------
 * A validated, relocatable NJS_MOTION for one body of a kind: `data` holds the header {mdata, nbFrame, type, inp_fn} at 0,
 * the per-node table (nodes x {nf pointers, nf counts}) at 12, then the keys (16 bytes each); every pointer is an offset from
 * `data` (0: none). The cutscene loader (src/sa1_emote_ev.c) makes them; photo mode's loaded motions (SA1_EMOTE_MOTION, a
 * later round) are meant to produce the same block and reuse the slab, the copy and the queue path unchanged. */
typedef struct sa1em_motion_block {
    uint32_t       size;        /* bytes of data */
    uint32_t       nodes;       /* the body's node count */
    uint32_t       frames;      /* nbFrame */
    uint16_t       type, inp_fn;
    const uint8_t *data;
} sa1em_motion_block;
/* structural only: the body's node count, every channel inside the block after the table, key counts <= nbFrame + 1, key
 * frames inside 0..nbFrame, channels 1..3 of 16-byte keys, the size cap (a slab). SA1_OK or SA1_ERR_BAD_FORMAT */
sa1_status sa1em_motion_check(int32_t kind, uint32_t object, const sa1em_motion_block *b);
#define SA1EM_SLAB_SIZE (320u * 1024u)      /* one of a world's two slabs in segment 0x18 (Big's 8 largest: 259,560 bytes) */

/* sa1em_world_latch with the blocks of the request's SA1_EMOTE_EVENT items (blocks[i] for item i, NULL for the others;
 * the same block for two items is copied once): copied into the world slab that the running, holding or fading emote does
 * not use, relocated to guest addresses there. SA1_ERR_OUT_OF_MEMORY: no room for the slabs in segment 0x18; SA1_ERR_LIMIT:
 * the copies are larger than a slab */
sa1_status sa1em_world_latch_ev(sa1_context *ctx, struct sa1g_world *w, const sa1_emote *e, int early,
                                const sa1em_motion_block *const *blocks);
/* the lift (SA1 units) of the EVENT entry drawn after the world's last pair times the bridge's weight (in: 0 -> 1, out:
 * 1 -> 0, between two entries from one lift to the other); 0 without an emote or an entry with a lift. The option is
 * applied by the caller (src/sa1_emote_ev.c sa1ev_lift_of) */
float sa1em_world_lift(const struct sa1g_world *w);

#endif /* SA1_EMOTE_H */
