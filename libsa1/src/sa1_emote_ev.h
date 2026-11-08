/*
 * sa1_emote_ev.h - cutscene animations in the emote catalogue (round 100-EV, SA1_API_EMOTE_EVENT; sadc
 * docs/research/emotes_ev.md 3 with its review RX.2, RX.6, RX.10). Internal: the generated catalogue (src/sa1_emote_events.c,
 * tools/gen_emote_events.py: numbers, names and SHA-1 only) and the loader / cache (src/sa1_emote_ev.c). Plain C99: no world
 * headers (the world side reaches the blocks through src/sa1_emote.h's seam, sa1em_motion_block).
 */
#ifndef SA1_EMOTE_EV_H
#define SA1_EMOTE_EV_H

#include "sa1_internal.h"
#include "sa1_emote.h"

/* ---- the catalogue (GENERATED) ----------------------------------------------------------------------------------------- */
typedef struct sa1ev_file {
    uint16_t    event;          /* the cutscene: EVxxxx.PRS */
    uint32_t    prs_size;       /* the file on the US 1.005 disc */
    uint32_t    image_size;     /* unpacked (loaded at 0x0CB80000) */
    uint32_t    cast;           /* bit (1 << SA1_CHAR_*) per character with animations in it */
    const char *sha1;           /* of the file, 40 hex digits */
} sa1ev_file;

typedef struct sa1ev_entry {
    uint16_t        event;      /* its home cutscene (the first that holds it) */
    uint8_t         order;      /* 1.. among this character's entries of that cutscene, by address ("EV0003 #2") */
    uint8_t         object;     /* 0: the kind's main body, 1: its other body of the same tree (Tails' flight body) */
    uint16_t        file;       /* sa1ev_files[] of the home cutscene */
    uint16_t        frames;     /* NJS_MOTION.nbFrame */
    uint8_t         nodes;      /* the body's nodes (the per-node table) */
    uint8_t         type;       /* NJS_MOTION.type: 3 (position, angle) or 7 (and scale) */
    uint16_t        inp_fn;     /* NJS_MOTION.inp_fn */
    uint32_t        action;     /* the NJS_ACTION in the home image */
    uint32_t        motion;     /* its NJS_MOTION */
    uint32_t        bytes;      /* the copy: header 12 + per-node table + keys */
    float           pace;       /* the cutscene's speed (frames per 1/30 s; 1 when no call was found) */
    float           lift;       /* SA1 units (SA1_EMOTE_F_LIFTED), else 0 */
    uint16_t        flags;      /* SA1_EMOTE_F_* */
    uint8_t         loop;       /* 1: the cutscenes loop it (SA1_EMOTE_PLAY_LOOP by default) */
    uint8_t         num_events; /* the cutscenes that use the same keys */
    const uint16_t *events;     /* their numbers, ascending */
    const char     *name;       /* "ev0003_2" (unique per kind) */
} sa1ev_entry;

typedef struct sa1ev_list {
    int32_t            kind;
    uint32_t           count;
    const sa1ev_entry *e;
} sa1ev_list;

extern const sa1ev_file sa1ev_files[];
extern const uint32_t sa1ev_num_files;

/* the EVENT entries of a kind (0, 2, 3, 5, 6, 7), NULL: none (Eggman, Tikal, Super Sonic) */
const sa1ev_list *sa1ev_list_of(int32_t kind);
/* entry `index` of a kind, NULL: none */
const sa1ev_entry *sa1ev_entry_of(int32_t kind, uint32_t index);
/* the file row of a cutscene number, NULL: none */
const sa1ev_file *sa1ev_file_of(uint32_t event);
/* the guest address of the body an entry is played on (the kind's main body; Tails' flight body for object 1), 0: none */
uint32_t sa1ev_body(int32_t kind, uint32_t object);

/* ---- the cache (src/sa1_emote_ev.c) ------------------------------------------------------------------------------------- */
/* the loaded block of an entry (NULL: not loaded). *st: SA1_OK loaded, SA1_ERR_NOT_LOADED not yet, or the cached failure
 * (SA1_ERR_BAD_HASH, SA1_ERR_BAD_FORMAT) with its reason in why. The block stays valid until the disc is unloaded */
const sa1em_motion_block *sa1ev_block(const sa1_context *ctx, int32_t kind, uint32_t index, sa1_status *st, char *why,
                                      size_t n);
/* the context's cache and options, freed with the disc (sa1_unload_disc) */
void sa1ev_free(sa1_context *ctx);
/* tests and diagnostics: the entries loaded and their bytes */
void sa1ev_stats(const sa1_context *ctx, uint32_t *loaded, uint32_t *bytes);
/* SA1_COPT_EMOTE_LIFT (routed by src/sa1_hostobj.c sa1_character_set_option) */
sa1_status sa1ev_set_lift(sa1_context *ctx, sa1_character_id id, int32_t value);
/* the lift (SA1 units) the render output adds to the body's draws of the instance `inst` (ctx->inst[slot]) after its last
 * pair: SA1_COPT_EMOTE_LIFT on and an EVENT entry with a lift drawn, times the bridge's weight; else 0 */
float sa1ev_lift_of(const sa1_context *ctx, const void *inst, const struct sa1g_world *w);

#endif /* SA1_EMOTE_EV_H */
