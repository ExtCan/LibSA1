/*
 * sa1_hostobj.h - host objects (API 0.3, sadc docs/libsa1_interaction.md 2.2-2.8 and the addendum, section 6): the
 * context-wide registry (src/sa1_hostobj.c, asset layer, plain C99) and the world side (src/world/sa1_world_hostobj.c:
 * the proxies, the LIBSA1_HOST code image, the pre / post hooks of the step; src/world/sa1_coladapt_world.c: the contacts
 * with moving collision objects). Internal: not part of the public API.
 *
 * Only opaque world types cross this header (struct sa1g_world), so the asset layer compiles without the world core's
 * include paths. The registry holds the host's descriptions in host space, untouched: every world converts them to SA1
 * space itself, inside its own floating-point mode, when it latches them at a pair start (determinism, design 2.8).
 */
#ifndef SA1_HOSTOBJ_H
#define SA1_HOSTOBJ_H

#include "sa1_internal.h"

#define SA1HO_MAX       SA1_HOSTOBJ_MAX_OBJECTS
#define SA1HO_IDX_BITS  11u                         /* id = generation << 11 | (entry index + 1): never 0 */
#define SA1HO_IDX_MASK  ((1u << SA1HO_IDX_BITS) - 1u)
#define SA1HO_MAGIC     0x52484153u                 /* "SAHR" */

/* the size of the 0.3 core description (later tails are copied when the caller's struct_size covers them) */
#define SA1HO_DESC_MIN  ((uint32_t)(offsetof(sa1_hostobj_desc, shapes) + SA1_HOSTOBJ_MAX_SHAPES * sizeof(sa1_hostobj_shape)))

typedef struct sa1ho_entry {
    uint32_t id;                /* 0: free */
    uint32_t gen;               /* generation of the last id given out from this entry */
    uint32_t removed_id;        /* the last object removed from this entry and how (SA1_HOSTOBJ_REMOVE_*): a world whose */
    uint32_t removed_how;       /* proxy names an id that is gone learns here whether it was killed */
    uint32_t desc_version;      /* changes with every add / update */
    uint32_t pose_version;      /* changes with every add / update / move */
    uint32_t release_seq;       /* HOLDABLE: sa1_hostobj_release calls (each world drops it once per change) */
    sa1_hostobj_desc desc;      /* host space, as given (struct_size = sizeof) */
} sa1ho_entry;

/* per-character settings kept by the context (sa1_character_set_option, sa1_hostobj_world_mask): applied to the world at
 * once and to a world built later (sa1ho_world_attach finds its character) */
#define SA1HO_SET_OPTS 0x1u
#define SA1HO_SET_MASK 0x2u
typedef struct sa1ho_charset {
    int32_t id;                 /* sa1_character_id (0: unused) */
    uint32_t set;               /* SA1HO_SET_* */
    uint32_t opts;              /* 1 << SA1_COPT_* */
    uint32_t mask;              /* SA1_HOSTOBJ_KIND_BIT bits, 0 = every kind */
} sa1ho_charset;

typedef struct sa1ho_registry {
    uint32_t magic;
    const sa1_context *ctx;
    uint32_t count;             /* live entries */
    uint32_t hiwater;           /* entries [0, hiwater) have been used */
    uint32_t config_version;
    uint32_t window;            /* 1..64 */
    float enter_radius;         /* host units; 0 = the default (280 SA1 units) */
    float leave_radius;         /* host units; 0 = the default (320 SA1 units) */
    uint32_t free_head, free_count;     /* FIFO of freed entries (reused last: a stale id stays recognisable longest) */
    uint16_t freeq[SA1HO_MAX];
    sa1ho_entry ent[SA1HO_MAX];
    sa1ho_charset chars[SA1_MAX_INSTANCES];     /* by character slot */
} sa1ho_registry;

/* The registry of a context (NULL: no object was ever added). Any thread; the lookup takes a short process lock. */
sa1ho_registry *sa1ho_registry_of(const sa1_context *ctx);
/* Frees the registry of a context (sa1_collision_free, at sa1_shutdown). */
void sa1ho_context_free(sa1_context *ctx);

/* the live entry of an id (NULL: no such object) */
static inline const sa1ho_entry *sa1ho_entry_of(const sa1ho_registry *r, uint32_t id)
{
    uint32_t idx = (id & SA1HO_IDX_MASK) - 1u;
    if (r == NULL || id == 0 || idx >= SA1HO_MAX || r->ent[idx].id != id) {
        return NULL;
    }
    return &r->ent[idx];
}

/* ---- the world side (src/world/sa1_world_hostobj.c; not in SA1_NO_WORLD builds) --------------------------------- */
struct sa1g_world;

/* the sa1_state_ex tail of a world */
typedef struct sa1ho_tail {
    int32_t rings, lives;
    uint32_t score, invulnerable_steps, hurt_steps;
    uint32_t held_object;       /* round 5: the HOLDABLE the character holds (0: none) */
} sa1ho_tail;

/* Attaches the host-object hooks to a freshly built character world (sa1_character.c ensure_world) and, with
 * start_rings >= 0, sets its start rings (sa1g_world_start_rings: 8C08A074, 8C08A198(n), n clamped to 0..99). */
sa1_status sa1ho_world_attach(sa1_context *ctx, struct sa1g_world *w, int32_t start_rings);
/* rings, lives, score, wtimer of player 0 and the steps since the last hurt (zero without a player) */
void sa1ho_world_tail(const struct sa1g_world *w, sa1ho_tail *out);
/* sa1_character_add_rings inside the world (outside ticks) */
sa1_status sa1ho_world_add_rings(struct sa1g_world *w, int32_t n);
/* pops up to cap events of a world, positions in ctx's host space (character left 0) */
uint32_t sa1ho_world_poll(const sa1_context *ctx, struct sa1g_world *w, sa1_hostobj_event *out, uint32_t cap);
/* the world's counts (proxies, created, destroyed, refused, events_dropped, heap) into an info struct */
void sa1ho_world_info(const struct sa1g_world *w, sa1_hostobj_info *out);

/* round 5 (the character side, outside ticks): a damage request (queued for the world's next step; SA1_ERR_LIMIT when 4
 * are pending), the options (1 << SA1_COPT_*) and the kind mask (0 = every kind), the held state in ctx's host space */
sa1_status sa1ho_world_damage(struct sa1g_world *w, const sa1_damage *d);
void sa1ho_world_settings(struct sa1g_world *w, uint32_t set, uint32_t opts, uint32_t mask);
void sa1ho_world_held(const sa1_context *ctx, const struct sa1g_world *w, sa1_hostobj_held *out);

/* tests and diagnostics: the guest taskwk of the world's proxy of an object (0: none), and the heap's free blocks */
uint32_t sa1ho_world_proxy_twp(const struct sa1g_world *w, uint32_t object);
void sa1ho_world_heap(const struct sa1g_world *w, uint32_t *total_free, uint32_t *largest);

/* ---- contacts with moving collision objects (src/world/sa1_coladapt_world.c) ------------------------------------ */
/* pops up to cap contacts of a world, host space (character left 0) */
uint32_t sa1ca_world_contacts(const sa1_context *ctx, struct sa1g_world *w, sa1_contact *out, uint32_t cap);

#endif /* SA1_HOSTOBJ_H */
