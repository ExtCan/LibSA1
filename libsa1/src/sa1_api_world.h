/*
 * sa1_api_world.h - glue between the public character API and the world core (sadc
 * docs/libsa1_integration.md 6.1, 6.8, 7.1, D2). Internal: not part of the public API.
 *
 *   src/sa1_character.c, src/sa1_core.c   the API (plain C99, the asset-layer flags: no world headers)
 *   src/world/sa1_api_world.c             the bridge (world flags: sa1_world.h, sadc's sa1/guest.h)
 *
 * Only opaque world types cross this header, so the asset layer compiles without the world core's
 * include paths. One world per ticking character (D2): sa1_character_create_ex(SA1_CREATE_WORLD) or the
 * first sa1_character_tick builds it from the character kind's pristine image (composed once per context
 * from the .gdi of the last sa1_load_disc), boots it (6.2, 6.3) with the context's static collision
 * (FAITHFUL: synthesized once per geometry generation into the context's collision segment; DISC: the
 * stage overlay in place of B_CHAOS7; none / GRID: an empty landtable) and ticks it in pairs (6.4).
 *
 * The world core is linked by build.py always and by CMake with SA1_WORLD=ON. A build without it must
 * define SA1_NO_WORLD (MSVC, which cannot build the world core, is detected): sa1_character_tick then
 * reports SA1_ERR_NOT_IMPLEMENTED as in API 0.1.
 */
#ifndef SA1_API_WORLD_H
#define SA1_API_WORLD_H

#include "sa1_internal.h"

#if !defined(SA1_NO_WORLD) && defined(_MSC_VER)
#define SA1_NO_WORLD 1
#endif

struct sa1g_world;
struct sa1w_ctx;
struct sa1s_ctx;

/* ---- per-character settings of the world layer (round 0.9.1: src/sa1_charopt.c the API, src/world/sa1_world_charopt.c
 *      the world side): sa1_character_set_lives / set_companion / set_game_option, kept by the context ---------------- */
#define SA1W_SET_LIVES      0x1u
#define SA1W_SET_COMPANIONS 0x2u
#define SA1W_SET_GOPTS      0x4u
#define SA1W_SET_SUPOPTS    0x8u    /* round 0.9.3a: sa1_character_set_super_option */
#define SA1W_GOPT_MAX       5u      /* SA1_GOPT_* ids 1..4 (round 0.9.4: SA1_GOPT_ITEM_ICON_LIMIT) */
#define SA1W_SUPOPT_MAX     7u      /* SA1_SUPOPT_* ids 1..6 (round 0.9.4a: SUPER_ANIMS 5, LSD_GOLD 6) */
typedef struct sa1w_charopt {
    int32_t id;                 /* sa1_character_id (0: unused; a record of another id is stale: a destroyed character) */
    uint32_t set;               /* SA1W_SET_*: what the host gave */
    int32_t lives;              /* 0..127 */
    uint32_t companions;        /* companions wished on: 1 << SA1_COMPANION_* (the others of `known` are wished off) */
    uint32_t known;             /* companions the host set (on or off) */
    uint32_t gopts;             /* 1 << SA1_GOPT_* (the option's value is not 0) */
    /* round 0.9.3a (sadc docs/research/super_general_design.md 5): the values of the game options (SA1_GOPT_SUPER_AURA_ANIM
       takes 0..2) and the Super options (sa1_character_set_super_option), by option id */
    int32_t gopt_value[SA1W_GOPT_MAX];
    uint32_t supopts;           /* 1 << SA1_SUPOPT_* the host set */
    int32_t supopt_value[SA1W_SUPOPT_MAX];
} sa1w_charopt;

/* ---- per-context state of the API (sa1_core.c allocates it right behind the sa1_context) ---------- */
typedef struct sa1_api_ext {
    struct sa1w_ctx *world;     /* the bridge's per-context state (images, collision segment); NULL: none yet */
    int32_t gdi_capture;        /* 1: remember the next file opened successfully (sa1_load_disc opens its .gdi
                                   first; sa1_characters_destroy_all, which sa1_load_disc calls before, arms it) */
    char gdi[1024];             /* the .gdi of the last sa1_load_disc ("" unknown) */
    sa1w_charopt charopt[SA1_MAX_INSTANCES];    /* round 0.9.1: by character slot */
    struct sa1s_ctx *screen;    /* round 0.9.1 HUD (src/sa1_screen.c): the screen settings and outputs; NULL: none yet */
} sa1_api_ext;

sa1_api_ext *sa1_api_ext_of(const sa1_context *ctx);

/* ---- world creation (sa1_character.c -> bridge) -------------------------------------------------- */
#define SA1W_START_TABLE 0x1u   /* the game's start position of level / act (8C04F8FA) instead of pos / ang */
#define SA1W_STAGE_TASKS 0x2u   /* the stage's own tasks (SA1_CREATE_STAGE_TASKS): the image with STGnn, boot with
                                   SA1G_BOOT_STAGE_TASKS | SA1G_BOOT_DEATHZONE */
/* round 6a (Super Sonic, API 0.4) */
#define SA1W_SUPER_CAPABLE 0x4u /* SA1_CREATE_SUPER_CAPABLE (kind 0; kind 8 always is) */
#define SA1W_SUPER_GIVEN   0x8u /* SA1_CREATE_SUPER: super_flags is the host's (else the defaults) */
#define SA1W_WATER_FLOOR   0x10u /* SA1_CREATE_WATER_FLOOR */

typedef struct sa1w_create {
    int32_t kind;               /* SA1_CHAR_* (0-7 are the game's own character ids) */
    int32_t level, act;         /* CurrentLevel / CurrentAct the world reports */
    uint32_t flags;             /* SA1W_* */
    float pos[3];               /* SA1 space */
    uint32_t ang[3];            /* BAMS (taskwk.ang) */
    uint32_t seed;              /* rand() state after boot */
    uint32_t event_flags[4];    /* bits 0..127 of the event / upgrade flags B_8C799920 */
    uint32_t super_flags;       /* round 6a: SA1_SUPER_* (with SA1W_SUPER_GIVEN) */
} sa1w_create;

/* creation results that are not errors (sa1w_world_create *notes) */
#define SA1W_NOTE_NO_GEOMETRY   0x1u    /* no static collision was set: the world has an empty landtable */
#define SA1W_NOTE_GRID_PENDING  0x2u    /* GRID geometry is stored, its synthesis is not implemented yet (5.4):
                                           the world has an empty landtable */

/* The player state of a world, SA1 space and units (per 60 Hz step), as the game stores it (the decoded
 * fields of a trace record, harness/src/sa1_trace.h, plus the attach points of PJoinVertexes). */
typedef struct sa1w_pstate {
    int32_t valid;              /* player 0 exists */
    uint64_t steps;             /* 60 Hz steps the world ran */
    int32_t mode, smode;        /* taskwk.mode / smode */
    uint32_t twp_flag;          /* taskwk.flag (16 bits) */
    uint32_t ang[3];            /* taskwk.ang (BAMS) */
    float pos[3];               /* taskwk.pos */
    float mspd[3], macc[3];     /* motionwk.spd / acc (world space) */
    uint32_t ang_aim[3];        /* motionwk.ang_aim */
    float pspd[3], pacc[3];     /* playerwk.spd / acc (player space) */
    float floor_normal[3];
    float dotp;
    uint32_t attr, last_attr;   /* surface attribute words (COL bits) */
    uint32_t pwp_flag;          /* playerwk.flag (16 bits) */
    int32_t jumptimer, nocontimer, breathtimer, waittimer;
    uint32_t reqaction, action, lastaction, nextaction;
    float nframe;
    uint32_t equipment, item;   /* 16-bit words */
    uint32_t land_count;        /* B_8C754EF0 */
    uint32_t rand_state;        /* D_8C5FBB08 */
    float attach_pos[6][3];     /* playerwk righthand_pos .. user1_pos */
    float attach_vec[6][3];     /* playerwk righthand_vec .. user1_vec */
    /* the drawn step (sa1_world.h drawn_*, sadc docs/research/gsa1_vertical_flicker.md fix B): taskwk.pos / ang right
       after sub step 0 of the last pair, whose display the character renderer draws; drawn_valid only when that sub
       step 0 is the one just before the world's last step (a whole pair ran) */
    int32_t drawn_valid;
    float drawn_pos[3];         /* SA1 space */
    uint32_t drawn_ang[3];      /* BAMS */
    int32_t mtnmode;            /* round 6a: playerwk.mj.mtnmode (Sonic's display picks the texlist by reqaction, or by
                                   action while it is 2) */
    int32_t super_look;         /* round 0.9.3a: the General form's look is on (the Super body / ball on Sonic's actions,
                                   the SUPERSONIC names in texlist_SONIC: src/world/sa1_world_super.c) */
    int32_t super_anims;        /* round 0.9.4a (super2_plan.md 5.4): look && anims after the pair, as super_look (the table
                                   the NEXT pair draws with; review R.8) */
    float   super_lift;         /* the lift L of GA's Super motions (6.4 - 5.4 from the game's data); 0 while super_anims 0 */
    /* round 0.9.4a (faces_wheel_plan.md 5.4): sa1w_world_pstate sets the "none" values (face_from / face_to / face_letter,
       emote_item / emote_row / emote_extra -1, the rest 0), then calls w->face.fill and w->emote.fill */
    uint32_t face_mode, face_shown, face_activity;
    int32_t  face_from, face_to, face_letter;
    float    face_rate;
    uint32_t emote_state;
    int32_t  emote_item, emote_row, emote_extra;
    float    emote_frame;
    /* round 0.9.4a (sa1_world.h drawn_frame; the owner's 0.9.4 note on runs at extreme speed): the drawn step's motion
       frame and action, meaningful when drawn_valid and drawn_frame_valid */
    int32_t  drawn_frame_valid;
    float    drawn_frame;
    uint32_t drawn_action;
} sa1w_pstate;
#define SA1W_PSTATE_DRAWN 1     /* sa1w_pstate has drawn_valid / drawn_pos / drawn_ang */
#define SA1W_PSTATE_DRAWN_FRAME 1   /* round 0.9.4a: and drawn_frame_valid / drawn_frame / drawn_action */

/* world core and the step's game code linked (sa1g_game_ready) */
int sa1w_ready(void);
/* per-context state for `gdi` (its pristine images are composed on first use) */
sa1_status sa1w_ctx_create(sa1_context *ctx, const char *gdi, struct sa1w_ctx **out);
void sa1w_ctx_destroy(struct sa1w_ctx *wc);
/* the context's world state (sa1_api_ext.world), created from the .gdi of the last sa1_load_disc when there is none yet,
 * under the world core's table lock (sa1_character_prepare may create it on another thread); no sa1_fail */
sa1_status sa1w_ctx_get(sa1_context *ctx, struct sa1w_ctx **out);
/* sa1_character_prepare: the world images of the kinds in `kinds` (1u << SA1_CHAR_*), composed with a worker context
 * (any thread) and installed under the table lock; returns the status of the first kind that failed */
sa1_status sa1w_prepare_images(sa1_context *ctx, uint32_t kinds);

/* ---- prepared static collision (sa1_collision.c -> bridge; any thread) ------------------------------------------- */
struct sa1_collision_prepared;
struct sa1ca_static;
/* the synthesis of s into a collision segment of p's own (p->seg, p->built), sealed; wctx: a worker context
 * (sa1_worker_ctx_create: errors land in its error text) */
sa1_status sa1w_prepare_build(sa1_context *wctx, struct sa1ca_static *s, struct sa1_collision_prepared *p);
/* frees p's collision segment (the last release) */
void sa1w_prepare_free(struct sa1_collision_prepared *p);
/* a booted world for one character (errors are recorded with sa1_fail); notes: SA1W_NOTE_* */
sa1_status sa1w_world_create(struct sa1w_ctx *wc, const sa1w_create *c, struct sa1g_world **out, uint32_t *notes);
void sa1w_world_destroy(struct sa1w_ctx *wc, struct sa1g_world *w);
/* live mode: input -> port A and the camera adapter, whole pairs from dt (6.4); *pairs: pairs run */
sa1_status sa1w_world_tick(struct sa1g_world *w, const sa1_input *in, float dt, uint32_t *pairs);
/* sa1w_world_tick in its parts, for a tick that runs several pairs (sa1_character_tick captures the pair before the
 * last one too: sadc docs/research/gsa1_060_pops.md F1). sa1w_world_tick(w, in, dt, p) == sa1w_world_input(w, in) +
 * sa1w_world_run(w, dt, 0, 0, p). Setting the same input again adds no tap (sa1g_world_set_pad keeps new presses only).
 *   input      the pad and the camera yaw (live mode)
 *   pairs_due  the pairs sa1w_world_run(w, dt, 0, 0, ...) would run now (the accumulator's own arithmetic, at most
 *              SA1G_MAX_PAIRS_PER_TICK); 0 for an invalid dt
 *   run        whole pairs from dt, at most max_pairs (0: SA1G_MAX_PAIRS_PER_TICK); keep_backlog 0: time beyond
 *              max_pairs is dropped as sa1w_world_tick drops it; 1: it stays in the accumulator for the next run (the
 *              same double arithmetic: when pairs_due(dt) == k >= 2, run(dt, 1, 1), k - 2 times run(0, 1, 1) and
 *              run(0, 1, 0) make the decisions of run(dt, 0, 0) to the bit, and so does run(dt, k - 1, 1) + run(0, 1,
 *              0)). Both paths check first, as sa1w_world_tick does, and return before the accumulator changes:
 *              SA1_ERR_INVALID_ARGUMENT (no world, no step, a bad dt), the dead world's status, SA1_ERR_NOT_IMPLEMENTED
 *              (the game step without the game code), SA1_ERR_NOT_LOADED (the game step before the boot) */
void sa1w_world_input(struct sa1g_world *w, const sa1_input *in);
uint32_t sa1w_world_pairs_due(const struct sa1g_world *w, float dt);
sa1_status sa1w_world_run(struct sa1g_world *w, float dt, uint32_t max_pairs, int keep_backlog, uint32_t *pairs);
void sa1w_world_pstate(const struct sa1g_world *w, sa1w_pstate *out);
/* pops up to cap events (SA1 space positions); returns the number written */
uint32_t sa1w_world_events(struct sa1g_world *w, sa1_event *out, uint32_t cap);
/* the world's fault message ("" if it is alive) */
const char *sa1w_world_fault(const struct sa1g_world *w);
/* sa1_debug_near_polys: the NEAR-poly list of the world's last query (src/world/sa1_coladapt_world.c), positions in ctx's
 * host space */
sa1_status sa1w_world_near(const sa1_context *ctx, const struct sa1g_world *w, sa1_near_info *info, sa1_near_poly *out,
                           uint32_t capacity, uint32_t *count);

/* ---- round 0.9.1: the per-character settings on a built world (src/world/sa1_world_charopt.c; outside ticks) ---- */
/* applies `what` (SA1W_SET_*) of o to w now; SA1_ERR_UNSUPPORTED (companions: player 0 is not that Amy), a faulted
 * world's status */
sa1_status sa1w_world_charopt(sa1_context *ctx, struct sa1g_world *w, const sa1w_charopt *o, uint32_t what);
/* *present = 1 when a task of the companion lives in w */
sa1_status sa1w_world_companion(const struct sa1g_world *w, uint32_t companion, int32_t *present);

/* ---- round 6a (Super Sonic, API 0.4; src/world/sa1_world_super.c; outside ticks) ---------------------------------- */
/* sa1_character_order on a built world: SA1_ORDER_SUPER / NORMAL (SetInputP(0, order) in the world unless player 0 is
 * in that form already); SA1_ERR_UNSUPPORTED (not a super-capable world, another order), SA1_ERR_NOT_LOADED (no player
 * 0), the faulted world's status */
sa1_status sa1w_world_order(struct sa1g_world *w, int32_t order);
/* round 0.9.3a: the warnings the General form left for the host's log since the last call (SA1W_SUPER_WARN_* bits; the
 * world cannot log from inside a pair) and its counters for tests (NULL: none) */
#define SA1W_SUPER_WARN_GUARD 0x1u  /* RC7: an action without a Super object drawn while the look is on */
#define SA1W_SUPER_WARN_LEAK  0x2u  /* player 0 of a General world in the game's Super modes 71-78 */
#define SA1W_SUPER_WARN_WELDS 0x4u  /* the weld set-up after the first pair did not run (the look has no Super welds) */
#define SA1W_SUPER_WARN_GA    0x8u  /* round 0.9.4a: GA (SA1_SUPOPT_SUPER_ANIMS' table) could not be built: the look keeps GT */
uint32_t sa1w_world_super_warnings(struct sa1g_world *w);
/* round 0.9.3a: one Super option's value (SA1_SUPOPT_*, validated by sa1_character_set_super_option) into a built world,
 * applied at once while a General Super Sonic is Super (the speed set, the aura); SA1_ERR_INVALID_ARGUMENT unknown option,
 * the faulted world's status */
sa1_status sa1w_world_supopt(struct sa1g_world *w, uint32_t option, int32_t value);
/* round 0.9.4a (super2_plan.md 6.1 task 2 with owner answer O1): the rows of GA, SA1_SUPOPT_SUPER_ANIMS' table - Sonic's
   action -> the game's Super action it draws (0: the row stays GT's, Sonic's motion on the Super body). The stand 0, the
   alternate stand 1 -> 2, the slope stands 7 / 8 and the jump-panel stand 74 float (130); his idle after standing still
   (4 -> 5 -> 6, the foot tap) stays Sonic's (owner answer O1); the walk / run family 9-13 takes the game's speed bands
   (131-134: 8C0AE6D8's jog / run / rush, the thresholds of Sonic's own selector 8C0AE588). Shared by the world
   (sa1_world_super.c su_build_ga) and the host (sa1_character.c: the CPU pose and the interpolation hint) */
static inline uint32_t sa1w_super_anims_row(uint32_t action)
{
    switch (action) {
    case 0: case 1: case 2: case 7: case 8: case 74:
        return 130u;
    case 9:
        return 131u;
    case 10:
        return 132u;
    case 11:
        return 133u;
    case 12: case 13:
        return 134u;
    default:
        return 0u;
    }
}
/* the GA rows that keep Sonic's mtnmode and next (a chain into another row: 1 -> 2) */
static inline int sa1w_super_anims_chain(uint32_t action)
{
    return action == 1u;
}
/* round 0.9.4a: the golden Light Speed charge (super2_plan.md 5.4); src/sa1_capture.c reads it once per snapshot */
#define SA1W_HAS_LSD_GOLD 1
int sa1w_world_lsd_gold(const struct sa1g_world *w);   /* 1: the pair just run was drawn with the General look on (the look
                                                          as texlist_SONIC had it during the pair's display, latched before
                                                          the after-pair look update) and SA1_SUPOPT_LSD_GOLD is 1; 0 for any
                                                          other world, a world without a pair yet, or w NULL */
/* round 0.9.4a (faces_wheel_plan.md 5.4): the guest address of GA's NJS_ACTION {the Super body, the lifted copy} for the
   game's Super actions 130-134 (the actptr of GA rows 0 / 9 / 10 / 11 / 12; 133 and 134 share one copy); 0 for any other
   action, a world that is not General, or before GA is built (src/world/sa1_world_super.c) */
#define SA1W_HAS_SUPER_LIFTED 1
uint32_t sa1w_world_super_lifted(const struct sa1g_world *w, uint32_t action);
/* round 0.9.4a (faces_wheel_plan.md 5.4): the character's world, built if needed (sa1_character.c's ensure_world);
   SA1_ERR_NOT_FOUND no character, SA1_ERR_UNSUPPORTED a puppet (no world), SA1_ERR_NOT_IMPLEMENTED with SA1_NO_WORLD; *out
   NULL on any error; SA1_PARTIAL (built without land) is no error: *out set (review R.12) */
#define SA1W_HAS_WORLD_ENSURE 1
sa1_status sa1w_character_world_ensure(sa1_context *ctx, sa1_character_id id, struct sa1g_world **out);

/* ---- round 0.9.4: SET objects (SA1_API_SET; src/sa1_set.c the registry and the API, src/world/sa1_world_set.c the world
 *      side; sadc docs/research/gsa1_094_plan.md 6.3 with its Review) ---------------------------------------------------- */
/* The registry of a context: the host's records in host space, as given (the worlds convert them in their own FP mode at
 * a pair start), their state, the window's configuration. A process table keyed by the context (as the host objects'),
 * created by the first sa1_set_add / sa1_set_configure, freed by sa1_collision_free at sa1_shutdown. */
#define SA1SET_MAGIC     0x54455353u    /* "SSET" */
#define SA1SET_IDX_BITS  15u            /* id = generation << 15 | (entry index + 1): never 0. 14 until round 0.9.4b:
                                           the last entry (index 16383) then gave index + 1 = 0x4000 inside the
                                           generation bits, a record no call could reach or remove (set_test r6
                                           after (10): a ghost ring taking a window unit); the ids keep their order */
#define SA1SET_IDX_MASK  ((1u << SA1SET_IDX_BITS) - 1u)
#define SA1SET_NTYPES    4u             /* SA1_OBJTYPE_RING .. SA1_OBJTYPE_ITEMBOX_AIR */

/* the catalogue (gen/objtypes.tsv; sa1_set.c) */
typedef struct sa1set_type {
    uint32_t type;              /* SA1_OBJTYPE_* */
    const char *name;           /* the game's list name */
    uint32_t exec;              /* DC address */
    uint8_t im, level;
    uint16_t attr;              /* the list entry's attribute (0: the default range) */
    float range;                /* SA1 units (the spawner's default 400 for attribute 0) */
    uint32_t cls, model, rotation_order, states, budget;
} sa1set_type;
/* the catalogue entry of a type (NULL: unknown) */
const sa1set_type *sa1set_type_of(uint32_t type);

typedef struct sa1set_entry {
    uint32_t id;                /* 0: free */
    uint32_t gen;
    uint32_t desc_version;      /* changes with every add / update (never 0 for a live entry) */
    uint32_t state_version;     /* changes with every sa1_set_state that changes the state or the bits */
    uint32_t state;             /* SA1_SET_STATE_* */
    uint32_t user_bits;         /* O_GRING: 0..0xFF */
    sa1_set_record rec;         /* host space, struct_size = sizeof */
} sa1set_entry;

typedef struct sa1set_registry {
    uint32_t magic;
    const sa1_context *ctx;
    uint32_t count;             /* live entries */
    uint32_t hiwater;           /* entries [0, hiwater) have been used */
    uint32_t ring_window, box_window;
    float margin;               /* SA1 units */
    uint32_t config_version;
    uint32_t unavailable;       /* tests (sa1set_test_unavailable): types made unavailable, bit 1u << type */
    uint32_t free_head, free_count;     /* FIFO of freed entries (reused last) */
    uint16_t freeq[SA1_SET_MAX_RECORDS];
    sa1set_entry ent[SA1_SET_MAX_RECORDS];
    /* round 0.9.4b (objects2_plan.md 5.4): sa1_set_configure's range and view (src/sa1_set.c writes them; src/world/
       sa1_world_set.c reads them at a pair start, with config_version) */
    float range;                    /* SA1 units; 0 = not configured: the catalogue's 400 */
    float view_tan;                 /* 0 = the game's screen */
    int32_t tier_hold;              /* tests (set_test (R6), as set_test (10) sets the windows): < 0 = SA1_SET_TIER_HOLD
                                       (registry_get's default), else the window's tier hold in pairs */
} sa1set_registry;

/* the registry of a context (NULL: no record was ever added and nothing configured) */
sa1set_registry *sa1set_registry_of(const sa1_context *ctx);
#define SA1SET_HAS_RANGE 1
/* round 0.9.4b: a record's host -> game conversions (src/world/sa1_world_set.c; also the showcase's, src/world/
   sa1_world_preview.c): a host point to SA1 space, a host rotation matrix to the BAMS angles of a rotation order
   (SA1_SET_ROT_*), exactly as a record is written into a world's SET entry */
void sa1w_set_point(const sa1_coords *c, const float h[3], float s[3]);
void sa1w_set_angles(const sa1_coords *c, const float rh[9], uint32_t order, int32_t ang[3]);

/* round 0.9.4b (review R.3): the image cache and shared segments of the context's worlds (struct sa1w_ctx is private to
   src/world/sa1_api_world.c) for a world that is not a character's - the object showcase (src/world/sa1_world_preview.c:
   sa1w_ctx_get, then sa1g_wctx_image and sa1g_world_create(w, &wc->shared)); NULL for NULL */
#define SA1W_HAS_WCTX_WC 1
struct sa1g_wctx;
struct sa1g_wctx *sa1w_ctx_wc(struct sa1w_ctx *wc);
/* frees it (sa1_collision_free, at sa1_shutdown) */
void sa1set_context_free(sa1_context *ctx);
/* the live entry of an id (NULL: none) */
static inline const sa1set_entry *sa1set_entry_of(const sa1set_registry *r, uint32_t id)
{
    uint32_t idx = (id & SA1SET_IDX_MASK) - 1u;
    if (r == NULL || id == 0 || idx >= SA1_SET_MAX_RECORDS || r->ent[idx].id != id) {
        return NULL;
    }
    return &r->ent[idx];
}
/* the ring tasks (SA1_SET_BUDGET_RING) or boxes (SA1_SET_BUDGET_BOX: 1) a record costs */
uint32_t sa1set_cost(const sa1_set_record *r);
/* tests: a type made unavailable (sa1_set_add -> SA1_ERR_UNSUPPORTED) or available again; SA1_ERR_NOT_FOUND unknown type */
sa1_status sa1set_test_unavailable(sa1_context *ctx, uint32_t type, int32_t unavailable);

/* the world side (src/world/sa1_world_set.c; not in SA1_NO_WORLD builds) */
struct sa1g_world;
/* 1 when the code map of this build has native code for the DC address */
int sa1w_set_exec_native(uint32_t exec);
/* pops up to cap SET events of a world, host space (character left 0) */
uint32_t sa1w_set_world_poll(const sa1_context *ctx, struct sa1g_world *w, sa1_set_event *out, uint32_t cap);
/* tests and diagnostics: a world's SET counts */
typedef struct sa1set_world_info {
    uint32_t active;            /* 1: the world has its SET state (it saw records) */
    uint32_t manager;           /* guest address of the manager task (0: none) */
    uint32_t slots;             /* condition slots in use (window + leaving) */
    uint32_t hiwater;           /* B_8C79A3DC */
    uint32_t window_records;    /* records selected by the last window */
    uint32_t ring_units, box_units;     /* budget units of the selected records */
    uint32_t live_ring_tasks;   /* RING tasks and ring-group children alive now (whatever made them) */
    uint32_t live_boxes;        /* item box tasks of the records alive now */
    uint32_t refused_window, refused_heap;  /* records left out / spawn holds over the world's life */
    uint32_t heap_largest;      /* the game heap's largest free block at the last check (bytes) */
    uint32_t spawned, despawned, events, events_dropped;
    uint32_t deferred;          /* frees / forced writes deferred by the touch rule (review R.8) */
    /* round 0.9.4b */
    float list_range;           /* the range written into the generated list's entries (0: the game's, attribute 0) */
    uint32_t tier_flips;        /* window tier changes between 1 and 2 taken (order B) */
    uint32_t tier_held;         /* ... held back by the tier hold */
    float objview_scale;        /* the rt_objview kx_scale written (0: the game's screen) */
    uint32_t objview_found;     /* 1: the world's pcdata has the rt_objview block */
} sa1set_world_info;
void sa1w_set_world_info(const struct sa1g_world *w, sa1set_world_info *out);
/* tests: the condition slot of a record in a world (-1: none) and the world's state of it */
int32_t sa1w_set_world_slot(const struct sa1g_world *w, uint32_t id);
/* round 0.9.4b (sa1_set_live, review R.6): the ids of the records whose task lives in w after its last pair, ascending;
 * returns how many (out gets the first cap of them; out may be NULL with cap 0) */
uint32_t sa1w_set_world_live(const struct sa1g_world *w, uint32_t *out, uint32_t cap);
/* tests (set_test (11)): 1 = the live step does not write the world's eye into the camera taskwk (its position stays
 * whatever it was); and a hook run right before the executor of every step (after the SET layer's own pre hook) */
void sa1w_set_world_test(struct sa1g_world *w, int32_t no_eye, void (*pre)(struct sa1g_world *w, int32_t sub));

/* ---- round 0.9.4a tests (tests/super_test.c general_anims_hint; super2_plan.md 6.1 test (10), review R.1): the render
 *      interpolation's motion hint of a character's last captured pair - the game's steady action, the drawn action /
 *      object / motion (pair_info: the General look's map), and what hint_node gives the first character model it finds
 *      (addresses & 0x1FFFFFFF of the loaded set's object and motion); SA1_ERR_NOT_LOADED before a captured pair.
 *      sa1w_test_hint_legacy(1): later pairs take 0.9.4's hint (no drawn map), for the before / after comparison -------- */
typedef struct sa1w_hint_info {
    int32_t valid, dvalid;      /* the pair's rinfo: a steady action; df known */
    int32_t action, eff;        /* the game's action, the action whose mtnmode / racio the drawn motion plays with */
    uint32_t dobj, dmot;        /* the drawn object / motion (0: the action's own) */
    double df;                  /* frames advanced since the pair before */
    uint32_t model;             /* the model hint_node was asked for (0: none found) */
    int32_t node;               /* its node, -1 none */
    uint32_t hobj, hmot;        /* the object / motion of the hint */
    /* the owner's 0.9.4 note "the run animations break a little bit when moving at extreme speed" (General Super Sonic,
       mode 2 action 13): the root-frame models of the last plan (sa1_character_render with 0 < alpha < 1 after the pair)
       whose node is in the DRAWN object and motion (the general_drawn map, whatever sa1w_test_hint_legacy says), each
       model's turn between the pairs (sa1_xlerp.phi) against that motion's own net turn over the pair's frame advance */
    uint32_t checked;           /* such models (0: no plan for this pair, or no steady action) */
    uint32_t wrong;             /* of them, a turn more than 20 degrees off the drawn motion's (or a SNAP) */
    uint32_t snapped;           /* of them, SNAPs (no usable hint: the last pair's rotation at once) */
    double worst;               /* the largest | |phi| - the motion's turn | (radians) */
    double worst_mid;           /* the largest | |phi| / 2 - the motion's turn over the first half of the pair | */
    double tdf;                 /* the drawn motion's frame advance between the drawn frames (frames) */
} sa1w_hint_info;
sa1_status sa1w_test_hint(const sa1_context *ctx, sa1_character_id id, sa1w_hint_info *out);
void sa1w_test_hint_legacy(int on);

/* ---- tests (tests/replay_test.c): the world of a character (NULL: not created yet, or none) -------- */
struct sa1g_world *sa1_character_world(const sa1_context *ctx, sa1_character_id id);

#endif /* SA1_API_WORLD_H */
