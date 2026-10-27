/*
 * sa1_world_hostobj.c - host objects inside the worlds (API 0.3; sadc docs/libsa1_interaction.md 2.3-2.8 as corrected
 * by its addendum 6.4, and the addendum's 6.1-6.6; round 5's H2 (5-lib-core) and H3 + H4 + HOLDABLE + BALLOON
 * (5-lib-game)): the proxies, the LIBSA1_HOST code image that serves their exec, the window, the hooks of the default
 * step, the hit snapshot, the events, the character side (damage requests, options, HURT / DEATH events, the held
 * state) and the ring API inside the world.
 *
 * A proxy is built as the game builds its enemies (design 2.3): CreateElementalTask(2, 3, 0x7F000100) (a taskwk, level
 * 3: after the character at level 1, before E-102's lock search at level 6), the 0x23C enemy work set up by the game's
 * own initialiser 8C047040 (EnemyInitialize, sadc src/1st_read/code_8C047040.c: CAlloc'd into tp->mwp, the home
 * position, its defaults, +0x4E |= 0x480), an OBJ_CONDITION stand-in as tp->ocp (E-102's firing code 8C0D46CE
 * dereferences ocp, 6.4 row 4), taskwk.id 0 and no flag 0x40 (never liftable for 8C06C880), counter.w[0] = the slot,
 * counter.w[1] = 'HO', and CCL_Init(tp, template, id, 1, n) (8C02F4B0) with id 3 (ENEMY, TARGET, BALLOON) or 4
 * (BREAKABLE, HAZARD, HOLDABLE). A HOLDABLE (6.1.2) is a CreateElementalTask(3, 3, ...) task instead (a taskwk and a
 * motion work: the game's throw writes htp->mwp->spd) set up by the game's movable-object initialiser 8C0586A0 (sadc
 * code_8C0586A0.c: taskwk.id = the lift class 4 or 5, flag 0x240 / 0x640, rad, weight), with two CCL cylinders made
 * from the host's shape 0 (the grab cylinder and the body). Before any of it the heap must hold a free block of 4 KB
 * (a native walk of its free list): CCL_Init loops forever on a failed CAlloc, and when its second CAlloc fails it
 * returns with taskwk.cwp unset (6.4 row 3), which is checked too. A refusal is SA1_EVENT_CAPACITY
 * (SA1_CAPACITY_HOST_OBJECTS), once per episode; the object is tried again at the next pair.
 *
 * Code address: 0x7F000100 (addr & 0x1FFFFFFF = 0x1F000100, SH-4 area 7, never game code) is served by the synthetic
 * code-map image LIBSA1_HOST (sa1g_hostobj_image below), which sa1_world_image.c appends to every world's fn_images
 * after every rebuild: the game's executor 8C0988A4 calls it with the signature id of "v:p" (0xDA9162B5, pclower's hash,
 * the same at every exec call site), DestroyTask its dest (none here, unless E-102 hooked it), the world's own walker
 * sa1g_run_tasks by name.
 *
 * Per world, in segment 0x18 (design 2.3 with 6.4 row 2: the extension segment is host memory outside the arena, reset
 * by every rebuild, never part of a RAM image): one ho_state block, created at the first pair start that finds host
 * objects in the context. It holds the 64 slots (object id, the proxy's task, the latched pose of the pair, the
 * template's levels, the cooldown, the hit snapshot, E-102's marker, the hold state), the game-visible part of each slot
 * (the OBJ_CONDITION stand-in and the CCL_INFO template, guest addresses the game dereferences), the event queue (256,
 * overflow counted) and the window's scratch. The character side (options, the kind mask, damage requests, the hurt
 * bookkeeping of one step, the held state) is host memory of the world itself (sa1g_world.ho): it works without host
 * objects and survives rebuilds (options and mask).
 *
 * One step (design 2.4, 2.5, 6.1.2, 6.2.2):
 *   pre, sub step 0   latch the registry: the objects within enter_radius of player 0 (kept until leave_radius; a held
 *                     one always), nearest first by (squared distance, id), at most `window`, only the kinds of the
 *                     world's mask; a held HOLDABLE that must go is dropped first by its holder's own drop function
 *                     (Sonic 8C0B1D60, Tails 8C0BD440, Knuckles 8C0C6DE0, E-102 8C0D15C0, Amy 8C0DC75E, Big 8C0E55E2):
 *                     RELEASED (FORCED); proxies of objects that left, were removed (KILLED: enemy work +0x4E |=
 *                     0x1000, SILENT: &= ~0x1000) or changed kind / shape count are freed (FreeTask 8C098786: DestroyTask
 *                     frees them at level 3 of this step, after the character's exec read the last pass, and runs the
 *                     dest E-102 may have hooked: the time bonus 8C0D5064 for a KILLED counted target); new ones are
 *                     created in ascending id in the lowest free slot not freed in this pair; sa1_hostobj_release
 *                     requests drop held objects (FORCED); every live slot latches its pose (prev = the last pair's end,
 *                     next = the registry's, converted to SA1 space here, in the world's FP mode; a held HOLDABLE is the
 *                     character's, a released one keeps its release point until the host moves it) and a changed
 *                     description rebuilds its template.
 *   pre, every step   the character side (design 2.5): host damage requests (taskwk.flag 4, FACE_SOURCE turns the
 *                     character to the source first, KILL is the game's death request 8C07BBC2; during invulnerability
 *                     HURT IGNORED; round 0.9.2: only with player 0's playerwk, held while the player task is still in
 *                     mode 0 before its first exec, dropped without a playerwk in any other mode: ho_char_pre), the hurt
 *                     attribution of a pending hit (the host request, a proxy as the character's
 *                     colliwk.hit_cwp, or the HURT surface), the options (NO_DAMAGE: wtimer 1; NO_DEATH / KEEP_RINGS: a
 *                     phantom barrier, playerwk.item |= 1 without the barrier's display task), rings / item / wtimer
 *                     before the executor.
 *   exec, level 3     hostobj_exec: 1. the last pass (the hit fields are still the pass's: the proxy's own CCL_Entry
 *                     resets them): the cooldown; on taskwk.flag 4 the game's damage check 8C0489E4 (squash, "killed by
 *                     a player", score +10, the bounce 8C048980) when the snapshot's attacker is still the same colliwk
 *                     of the same live task, else the same effects without dereferencing it; a BALLOON instead launches
 *                     the attacking character (8C07C720, or SetVelocityP 8C07C676 with LAUNCH_BOUNCE) and rumbles
 *                     (8C109EAC); a hit by E-102's shot releases the lock-on marker at once (8C0D5D90); a HIT event
 *                     classified from the snapshot (2.6); then the cooldown (defence 3 on every shape, out of the target
 *                     lists, enemy work 0x1000); TOUCH events (POP_ON_TOUCH: a touch hits a BALLOON); round 0.9.2: an
 *                     ENEMY without a hit asks the game's damage check for the bomb item's sphere (ho_bomb: one HIT per
 *                     sphere). 2. E-102: LOCKED
 *                     when a marker appears in the enemy work (+0x40), TARGETED when its shot slot (marker awp +0x14) is
 *                     set, UNLOCKED when it goes. 3. the pose of the step (sub step 0: the pair's midpoint, sub step 1:
 *                     its end), a rebuilt template copied into the colliwk's shapes (hit fields kept) and 8C02F400, the
 *                     target flag 0x800, NO_LOCKON. 4. CCL_Entry 8C02F63C.
 *                     A HOLDABLE (6.1.2): held (taskwk.flag 0x1000 and a player's playerwk.htp is the proxy): LIFTED
 *                     once, then nothing (as the game's movable step 8C05872A: no pose, no CCL_Entry: the holder never
 *                     collides with it); released: RELEASED with the reason (the holder's action: throw, aimed throw,
 *                     put-down, else drop), the game's speed and the aimed target, then the speed is cleared (the host
 *                     owns the flight); HOLD_LOCKED as flag 0x1000; HOLD_HITS hits; the pose; CCL_Entry.
 *   post, after CCL   the hit snapshot of every proxy hit in this pass (not of a held one): the attacker's colliwk, its
 *                     owner task and exec, its shape and attack level, its centre (computed natively: the game's
 *                     8C02FC12 would use the matrix stack), its speed (the attacking player's motionwk), and that
 *                     player's state (mode, flag, item, equipment); player 0's hurt (its wtimer became 120 in this step):
 *                     SA1_EVENT_HURT, the death request (playerwk.item 0x4000 appeared): SA1_EVENT_DEATH, a phantom
 *                     barrier the game did not consume is removed; the held state of player 0. Round 0.9.1 (E1,
 *                     ho_death_rules): drowning (smode 39 appeared) and death zones (the death-zone task got its death
 *                     child) are deaths too, the world's first RESTART id 2 without a death before it gives DEATH (OTHER), and
 *                     E-102's time-up death pushes SA1_EVENT_TIME_UP before its DEATH.
 *
 * Determinism: the registry is read only at the pair start; conversions and the window run in the world's FP mode with
 * single-precision arithmetic and no CRT maths (the angle to a damage source is the game's own atan2f, sa1g_atan2f
 * through the code map); the order is (squared distance, id), creation is in ascending id into the lowest free slot.
 * Nothing here runs or allocates while a world has never seen a host object, a request or an option: the reference
 * replays run unchanged.
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_hostobj.h"

#include <stddef.h>
#include <string.h>

/* ---- the game's addresses -------------------------------------------------------------------------------------- */
#define HO_CODE             0x7F000100u     /* the proxies' exec (LIBSA1_HOST) */
#define HO_KEY              0x1F000100u
#define HO_SIG_VP           0xDA9162B5u     /* pclower's signature id of "v:p" (the executor's exec / disp / dest calls) */
#define HO_TAG              0x484Fu         /* taskwk.counter.w[1] of a proxy: 'HO' */
#define HO_FN_ENEMY_INIT    0x8C047040u     /* the enemy work initialiser (tp, twp) -> ewp (sadc code_8C047040.c) */
#define HO_FN_MOVABLE_INIT  0x8C0586A0u     /* the movable-object set-up (twp, mwp, class) -> 0 / 1 (code_8C0586A0.c) */
#define HO_FN_FREETASK      0x8C098786u     /* FreeTask(tp): exec = DestroyTask */
#define HO_FN_CCL_INIT      0x8C02F4B0u     /* CCL_Init(tp, info, id, 1, n) */
#define HO_FN_CCL_ENTRY     0x8C02F63Cu     /* CCL_Entry(twp) */
#define HO_FN_CCL_RANGE     0x8C02F400u     /* colliwk.range from the shapes */
#define HO_FN_DAMAGE        0x8C0489E4u     /* the enemy damage check (twp, ewp) */
#define HO_FN_BOUNCE        0x8C048980u     /* the bounce of player pno */
#define HO_FN_SCORE         0x8C08A2A2u     /* score += n (chain bonus 8C08A272) */
#define HO_FN_RINGS_ADD     0x8C08A198u     /* rings += n, the 1-up per 100 */
#define HO_FN_DEATH         0x8C07BBC2u     /* the death request (pno): smode 50, item 0x4000, task 8C07BA44 */
#define HO_FN_DEATHZONE     0x8C07BAC0u     /* round 0.9.1: the death-zone task's exec (8C07BD48 gives it the death child) */
#define HO_SMODE_DROWN      39              /* round 0.9.1: SetInputP(0, 39), the drowning countdown 8C07BE6E at 0x30C */
#define HO_RESTART_DEATH    2u              /* round 0.9.1: 8C04E386(2), "restart the act after a death or a timeout" */
#define HO_FN_SETVEL        0x8C07C676u     /* SetVelocityP(pno, x, y, z): world speed */
#define HO_FN_SPRING        0x8C07C720u     /* SetInputP(pno, 3) + SetVelocityP: the ground spring's request */
#define HO_FN_RUMBLE        0x8C109EACu     /* pad vibration (pno, 4, 15, 0): a RUMBLE event in libsa1 */
#define HO_FN_MARKER_REL    0x8C0D5D90u     /* E-102: release the lock-on marker of a target (tp) */
#define HO_FN_TIME_BONUS    0x8C0D5064u     /* E-102: the time bonus of a destroyed target (group task or NULL) */
#define HO_FN_ATAN2         0x8C10D0F8u     /* sa1g_atan2f(y, x): the SDK's atan2f (pclower's runtime) */
#define HO_ADDR_HEAP_FREE   0x8C79F03Cu     /* MEMBLK *B_8C79F03C: the heap's free list (code_8C098998.c) */
#define HO_ADDR_LIVES       0x8C752BE0u     /* Sint8 B_8C752BE0 */
#define HO_ADDR_SCORE       0x8C752BF4u     /* Sint32 B_8C752BF4 */
#define HO_ADDR_TARGETS0    0x8C73EE38u     /* PL_TARGET B_8C73EE38[]: player 0's target list {twp, dist}, NULL-terminated */
#define HO_ADDR_TARGETS1    0x8C742288u     /* the other players' list */
#define HO_ADDR_BONUS_TASK  0x8C423048u     /* task *D_8C423048: the running E-102 bonus counter (8C0D520A) */
#define HO_ADDR_BOMB_C      0x8C78C384u     /* round 0.9.2: NJS_POINT3 B_8C78C384, the bomb item's sphere centre (8C05E582) */
#define HO_ADDR_BOMB_R      0x8C78C390u     /* Float B_8C78C390: its radius (< 0: off; 8C05E59A grows it, 8C05E5C2 tests it) */
#define HO_BOMB_LEVEL       3u              /* the bomb's HIT: SA1_ATTACK_OTHER at the strongest level (it destroys every
                                               enemy it reaches, whatever its armour) */
#define HO_EXEC_LASER       0x0C0D3DA0u     /* & 0x1FFFFFFF: E-102's shot */
#define HO_EXEC_BLAST       0x0C0D65BAu     /* its impact sphere */
#define HO_EXEC_LURE        0x0C0E943Cu     /* Big's lure */
#define HO_EXEC_SPARKLE     0x0C0B5994u     /* Sonic's sparkles (design 2.6) */
#define HO_EXEC_SPARKLE2    0x0C0B5B80u     /* the sparkles' creator as design 1.1 names it */
#define HO_DEST_E102        0x0C0D32C0u     /* the dest E-102's lock search hooks into its targets (8C0D332C) */

#define HO_SLOTS            SA1_HOSTOBJ_MAX_WINDOW
#define HO_EVQ              256u
#define HO_INFO             0x34u           /* CCL_INFO */
#define HO_GBLOCK           (0x10u + HO_INFO * SA1_HOSTOBJ_MAX_SHAPES)  /* OBJ_CONDITION stand-in + the template */
#define HO_EWORK            0x23Cu          /* the enemy work */
#define HO_HEADROOM         4096u           /* bytes in one free heap block before a proxy is created */
#define HO_ENTER            280.0f          /* SA1 units (design 2.7) */
#define HO_LEAVE            320.0f
#define HO_COOLDOWN         20u
#define HO_SPRING_SPEED     5.0f            /* objexec_SPRING's launch (D_8C1550AC = (0, 5, 0)): SA1 units per step */
#define HO_GRAB_REACH       8.0f            /* HOLDABLE: SA1 units added to the body radius (6.1.2) */
#define HO_GRAB_MIN_R       15.0f           /* the grab cylinder of the game's sample O_SAMPLE_S (D_8C1BA668 shape 0) */
#define HO_GRAB_MIN_H       6.0f            /* its top >= 6 above the body centre: 8C0301A0's SIDE test fires */
#define HO_MAGIC            0x4F484153u     /* "SAHO" */

/* the hold state of a HOLDABLE slot */
#define HO_FREE             0u
#define HO_HELD             1u

/* the hit snapshot of a proxy, taken right after the CCL pass (hostobj_post) */
typedef struct ho_snap {
    uint32_t valid;             /* the proxy's colliwk was hit in the pass (colliwk.flag 2, hit_cwp set) */
    uint32_t acwp;              /* the attacker's colliwk (guest) */
    uint32_t atp, aexec;        /* its owner task and that task's exec */
    uint32_t aid;               /* the attacker's colliwk id (0 players, 1 their projectiles) */
    int32_t ashape, myshape;    /* the attacker's shape (proxy colliwk.hit_num), the proxy's own (my_num) */
    uint32_t alevel, aattr;     /* the attacking shape's attack level and attr */
    uint32_t ap0;               /* the attacker's owner is player 0 */
    int32_t apno;               /* the attacker's owner is player apno (playertwp[apno]), -1: not a player */
    uint32_t achr, amode, aflag, aitem, aequip;     /* its taskwk (counter.b[1], mode, flag), playerwk item / equipment */
    float apos[3];              /* the attacking shape's centre (SA1) */
    float aspd[3];              /* the attacking player's motionwk.spd (SA1 units per step) */
} ho_snap;

typedef struct ho_slot {
    uint32_t id;                /* host object id, 0 = free */
    uint32_t tp, twp;           /* the proxy's task and taskwk (guest) */
    uint32_t kind, flags, ccl_id, nshapes;
    int32_t armor, touch;
    uint32_t cooldown_steps, cooldown;
    uint32_t desc_version;
    uint32_t freed_pair;        /* S->pairs of the pair it was freed in (no reuse in the same pair) */
    uint32_t dirty;             /* the template changed: copy it into the colliwk at the next exec */
    uint32_t touching;          /* the character touched it in the last pass */
    uint64_t user;
    float prev[3], next[3];     /* SA1 space: the pose at the last pair's end and at this pair's end */
    int32_t prev_yaw, next_yaw; /* BAMS */
    uint8_t tflag[SA1_HOSTOBJ_MAX_SHAPES];  /* the template's flag bytes (levels restored after a cooldown) */
    int32_t box_yaw[SA1_HOSTOBJ_MAX_SHAPES];/* BOX shapes: their own yaw (BAMS), the object's yaw is added each step */
    uint8_t is_box[SA1_HOSTOBJ_MAX_SHAPES];
    /* round 5 */
    float launch;               /* BALLOON: SA1 units per step upward (0: the spring's 5) */
    float reach;                /* HOLDABLE: SA1 units added to the body radius */
    uint32_t hstate, holder;    /* HOLDABLE: HO_FREE / HO_HELD, the holder's player number */
    uint32_t keep_pose, rel_pv; /* HOLDABLE: the release point is kept while the entry's pose_version is rel_pv */
    uint32_t release_seq;       /* HOLDABLE: the entry's release_seq this slot has seen */
    uint32_t marker, shot_seen; /* E-102: the lock-on marker task seen in the enemy work, its shot reported */
    uint32_t group, group_valid;/* E-102: the bonus group (marker awp +0x2C) of a marker released at a hit */
    ho_snap snap;
} ho_slot;

/* an event in SA1 space (the poll converts) */
typedef struct ho_ev {
    uint64_t user;
    uint32_t type, object, attack, level, damage, shape, flags, step, reason, target;
    float pos[3], dir[3], speed;    /* speed: SA1 units per step */
} ho_ev;

typedef struct ho_cand {
    float d2;
    uint32_t id, idx;
} ho_cand;

typedef struct ho_state {
    uint32_t magic, self;       /* self: the block's guest address */
    uint32_t live;              /* proxies */
    uint32_t pairs;             /* pair starts latched */
    uint32_t cap_active;        /* a CAPACITY episode is running */
    uint32_t created, destroyed, refused;
    uint32_t ev_head, ev_count, ev_dropped;
    uint8_t slot_of[SA1HO_MAX]; /* registry entry -> slot + 1 (0: none; valid while the slot's id is the entry's) */
    uint8_t sel[SA1HO_MAX];     /* selected by this pair's window */
    ho_slot slot[HO_SLOTS];
    ho_ev ev[HO_EVQ];
    ho_cand cand[SA1HO_MAX], tmp[SA1HO_MAX];
    uint8_t gblock[HO_SLOTS][HO_GBLOCK];    /* game-visible: OBJ_CONDITION stand-in (0x10), CCL_INFO template (4 x 0x34) */
    /* round 0.9.2, per slot, after the game-visible blocks (their guest addresses, which the arena holds, stay put): the
     * ENEMY was hit by the bomb sphere that is running (one HIT per sphere), and the sphere's radius at that hit (a
     * smaller radius later is a new bomb) */
    uint8_t bombed[HO_SLOTS];
    float bomb_r[HO_SLOTS];
} ho_state;

/* ---- guest memory (the arena, bounded; inside and outside a world) -------------------------------------------- */
static int ho_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static uint8_t *ho_at(const sa1g_world *w, uint32_t g, uint32_t n)
{
    if (!ho_arena(g) || (g & 0xFFFFFFu) > SA1G_ARENA_SIZE - n) {
        return NULL;
    }
    return w->ram + (g & 0xFFFFFFu);
}

static uint32_t ho_rd32(const sa1g_world *w, uint32_t g)
{
    const uint8_t *p = ho_at(w, g, 4);
    uint32_t v = 0;
    if (p != NULL) {
        memcpy(&v, p, 4);
    }
    return v;
}

static uint32_t ho_rd16(const sa1g_world *w, uint32_t g)
{
    const uint8_t *p = ho_at(w, g, 2);
    uint16_t v = 0;
    if (p != NULL) {
        memcpy(&v, p, 2);
    }
    return v;
}

static uint32_t ho_rd8(const sa1g_world *w, uint32_t g)
{
    const uint8_t *p = ho_at(w, g, 1);
    return p != NULL ? *p : 0u;
}

static float ho_rdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = ho_rd32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static void ho_wr32(const sa1g_world *w, uint32_t g, uint32_t v)
{
    uint8_t *p = ho_at(w, g, 4);
    if (p != NULL) {
        memcpy(p, &v, 4);
    }
}

static void ho_wr16(const sa1g_world *w, uint32_t g, uint32_t v)
{
    uint8_t *p = ho_at(w, g, 2);
    uint16_t h = (uint16_t)v;
    if (p != NULL) {
        memcpy(p, &h, 2);
    }
}

static void ho_wrf(const sa1g_world *w, uint32_t g, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    ho_wr32(w, g, v);
}

static void ho_rd3(const sa1g_world *w, uint32_t g, float v[3])
{
    uint32_t k;
    for (k = 0; k < 3; k++) {
        v[k] = ho_rdf(w, g + 4u * k);
    }
}

static void ho_put32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static void ho_putf(uint8_t *p, float f)
{
    memcpy(p, &f, 4);
}

static void *ho_fn(uint32_t dc)
{
    return sa1_fn(SA1_GFN_C(dc), 0u);
}

static float ho_abs(float x)
{
    return x < 0.0f ? -x : x;
}

/* playertwp / playermwp / playerpwp[pno] (guest; 0: none) */
static uint32_t ho_ptwp(const sa1g_world *w, uint32_t pno)
{
    return pno < 8u ? ho_rd32(w, SA1G_ADDR_PLAYER_TWP + 4u * pno) : 0u;
}

static uint32_t ho_pmwp(const sa1g_world *w, uint32_t pno)
{
    return pno < 8u ? ho_rd32(w, SA1G_ADDR_PLAYER_MWP + 4u * pno) : 0u;
}

static uint32_t ho_ppwp(const sa1g_world *w, uint32_t pno)
{
    return pno < 8u ? ho_rd32(w, SA1G_ADDR_PLAYER_PWP + 4u * pno) : 0u;
}

/* ---- the state ------------------------------------------------------------------------------------------------- */
static ho_state *ho_state_of(const sa1g_world *w)
{
    ho_state *S;
    uint32_t g;
    if (w == NULL || w->ext == NULL || w->hostobj == 0) {
        return NULL;
    }
    g = w->hostobj;
    if (((g >> 24) & 31u) != SA1G_SEG_EXT || (size_t)(g & 0xFFFFFFu) + sizeof(ho_state) > w->ext_used) {
        return NULL;
    }
    S = (ho_state *)(void *)(w->ext + (g & 0xFFFFFFu));
    return S->magic == HO_MAGIC && S->self == g ? S : NULL;
}

static ho_state *ho_state_create(sa1g_world *w)
{
    ho_state *S = NULL;
    uint32_t g = sa1g_world_ext_alloc(w, sizeof(ho_state), 16, (void **)&S);
    if (g == 0 || S == NULL) {
        return NULL;
    }
    memset(S, 0, sizeof *S);
    S->magic = HO_MAGIC;
    S->self = g;
    w->hostobj = g;
    return S;
}

static uint32_t ho_gblock_guest(const ho_state *S, uint32_t k)
{
    return S->self + (uint32_t)offsetof(ho_state, gblock) + k * HO_GBLOCK;
}

/* the slot whose proxy is the task gtp (-1: not a proxy of this world) */
static int32_t ho_slot_of_task(const sa1g_world *w, const ho_state *S, uint32_t gtp)
{
    uint32_t twp, k;
    if (S == NULL || !ho_arena(gtp)) {
        return -1;
    }
    twp = ho_rd32(w, gtp + 0x20u);
    if (!ho_arena(twp) || ho_rd16(w, twp + 0x0Au) != HO_TAG) {
        return -1;
    }
    k = ho_rd16(w, twp + 0x08u);
    return k < HO_SLOTS && S->slot[k].id != 0 && S->slot[k].tp == gtp ? (int32_t)k : -1;
}

/* ---- conversions (host -> SA1, in the world's FP mode; no CRT maths) ------------------------------------------- */
static void ho_point(const sa1_coords *c, const float h[3], float s[3])
{
    const float *a = c->axis;
    float inv = 1.0f / c->scale;
    s[0] = (a[0] * h[0] + a[3] * h[1] + a[6] * h[2]) * inv;
    s[1] = (a[1] * h[0] + a[4] * h[1] + a[7] * h[2]) * inv;
    s[2] = (a[2] * h[0] + a[5] * h[1] + a[8] * h[2]) * inv;
}

/* the half extents along the SA1 axes of a box with half extents h along the host axes */
static void ho_extent(const sa1_coords *c, const float h[3], float s[3])
{
    const float *a = c->axis;
    float inv = 1.0f / c->scale;
    int j;
    for (j = 0; j < 3; j++) {
        s[j] = (ho_abs(a[j]) * h[0] + ho_abs(a[3 + j]) * h[1] + ho_abs(a[6 + j]) * h[2]) * inv;
    }
}

/* host yaw (radians, the sa1_pose.yaw convention) -> taskwk.ang.y (BAMS): the game turns by -yaw (sa1_character.c) */
static int32_t ho_bams(float yaw)
{
    double t = -(double)yaw * (65536.0 / 6.28318530717958647692);
    int32_t i = (int32_t)(t >= 0.0 ? t + 0.5 : t - 0.5);
    return i & 0xFFFF;
}

static uint32_t ho_ccl_id(uint32_t kind)
{
    return kind == SA1_HOSTOBJ_ENEMY || kind == SA1_HOSTOBJ_TARGET || kind == SA1_HOSTOBJ_BALLOON ? 3u : 4u;
}

/* the CCL shape count of a description (a HOLDABLE always has its grab cylinder and its body) */
static uint32_t ho_nshapes(const sa1_hostobj_desc *d)
{
    return d->kind == SA1_HOSTOBJ_HOLDABLE ? 2u : d->num_shapes;
}

/* v * Ry(0x8000 - ang.y) * Rx(ang.x) * Rz(ang.z): the rotation of the characters' hold updates (8C0B1DE0: njRotateZ,
 * njRotateX, njRotateY on the unit matrix, njCalcVector; the SDK's row-vector convention, rt_matrix.c) */
static void ho_rot_hold(const uint32_t ang[3], const float v[3], float out[3])
{
    float s, c, x = v[0], y = v[1], z = v[2], t;
    sa1g_fsca((int32_t)((0x8000u - ang[1]) & 0xFFFFu), &s, &c);
    t = x * c + z * s;
    z = z * c - x * s;
    x = t;
    sa1g_fsca((int32_t)(ang[0] & 0xFFFFu), &s, &c);
    t = y * c - z * s;
    z = y * s + z * c;
    y = t;
    sa1g_fsca((int32_t)(ang[2] & 0xFFFFu), &s, &c);
    out[0] = x * c - y * s;
    out[1] = x * s + y * c;
    out[2] = z;
}

/* ---- events ---------------------------------------------------------------------------------------------------- */
static ho_ev *ho_ev_new(sa1g_world *w, ho_state *S, uint32_t type, const ho_slot *sl)
{
    ho_ev *e;
    if (S->ev_count >= HO_EVQ) {
        S->ev_dropped++;
        return NULL;
    }
    e = &S->ev[(S->ev_head + S->ev_count) % HO_EVQ];
    S->ev_count++;
    memset(e, 0, sizeof *e);
    e->type = type;
    e->object = sl->id;
    e->user = sl->user;
    e->step = (uint32_t)w->steps;
    return e;
}

/* direction and speed of a velocity (SA1 units per step) */
static void ho_ev_vel(ho_ev *e, const float v[3])
{
    float s2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2], sp = s2 > 0.0f ? sa1g_sqrtf(s2) : 0.0f;
    if (sp > 0.0f) {
        e->dir[0] = v[0] / sp;
        e->dir[1] = v[1] / sp;
        e->dir[2] = v[2] / sp;
    }
    e->speed = sp;
}

static void ho_push(sa1g_world *w, ho_state *S, uint32_t type, const ho_slot *sl, uint32_t attack, uint32_t level,
                    uint32_t damage, uint32_t shape, uint32_t flags, const ho_snap *sn)
{
    ho_ev *e = ho_ev_new(w, S, type, sl);
    if (e == NULL) {
        return;
    }
    e->attack = attack;
    e->level = level;
    e->damage = damage;
    e->shape = shape;
    e->flags = flags;
    if (sn != NULL && sn->valid) {
        memcpy(e->pos, sn->apos, sizeof e->pos);
        ho_ev_vel(e, sn->aspd);
    } else {
        ho_rd3(w, sl->twp + 0x20u, e->pos);
    }
}

/* ---- the heap ---------------------------------------------------------------------------------------------------- */
static void ho_heap_walk(const sa1g_world *w, uint32_t *total, uint32_t *largest)
{
    uint32_t blk = ho_rd32(w, HO_ADDR_HEAP_FREE), t = 0, l = 0, n = 0;
    while (ho_arena(blk) && n < 0x10000u) {
        uint32_t end = ho_rd32(w, blk + 8u), len;
        if (end == 0) {
            break;                              /* the arena-end header */
        }
        if (!ho_arena(end) || (end & 0xFFFFFFu) <= (blk & 0xFFFFFFu)) {
            break;
        }
        len = (end & 0xFFFFFFu) - (blk & 0xFFFFFFu);
        t += len;
        l = len > l ? len : l;
        blk = ho_rd32(w, blk);
        n++;
    }
    if (total != NULL) {
        *total = t;
    }
    if (largest != NULL) {
        *largest = l;
    }
}

/* ---- templates (design 2.3 table; the addendum's flags) ----------------------------------------------------------- */
static void ho_slot_desc(const sa1_context *ctx, ho_slot *sl, const sa1ho_entry *e)
{
    const sa1_hostobj_desc *d = &e->desc;
    sl->kind = d->kind;
    sl->flags = d->flags;
    sl->ccl_id = ho_ccl_id(d->kind);
    sl->nshapes = ho_nshapes(d);
    sl->armor = d->armor < 0 ? 0 : d->armor;
    sl->touch = d->touch >= 0 ? d->touch : d->kind == SA1_HOSTOBJ_ENEMY ? 1 : d->kind == SA1_HOSTOBJ_HAZARD ? 2 : 0;
    if (d->kind == SA1_HOSTOBJ_HOLDABLE || d->kind == SA1_HOSTOBJ_BALLOON) {
        sl->touch = 0;                  /* never hurts */
    }
    sl->cooldown_steps = d->cooldown_steps ? d->cooldown_steps : HO_COOLDOWN;
    sl->user = d->user;
    sl->desc_version = e->desc_version;
    /* host units/s -> SA1 units per step, host units -> SA1 units (the world's FP mode) */
    sl->launch = d->launch_speed > 0.0f ? d->launch_speed / ctx->coords.scale / 60.0f : 0.0f;
    sl->reach = d->grab_reach > 0.0f ? d->grab_reach / ctx->coords.scale : HO_GRAB_REACH;
}

/* HOLDABLE (6.1.2): the grab cylinder (shape 0) and the body (shape 1) from the host's shape 0; a sphere body gives two
 * spheres (never lifted: 8C02FF7E gives no side contact). attr 0x800 keeps Sonic out of his push mode against it, 0x80
 * (damageable, defence = armor) is the game's sample's value (O_SAMPLE_S, D_8C1BA668: attr 0x881) */
static void ho_template_hold(const sa1_context *ctx, ho_state *S, uint32_t k, const sa1_hostobj_desc *d)
{
    ho_slot *sl = &S->slot[k];
    uint8_t *t = S->gblock[k] + 0x10u;
    const sa1_hostobj_shape *s = &d->shapes[0];
    float c[3], e[3], r, h, ga, gb, inv = 1.0f / ctx->coords.scale;
    uint32_t form = s->form == SA1_SHAPE_SPHERE ? 0u : 1u, base, i;
    int solid = !(sl->flags & SA1_HOSTOBJ_NOT_SOLID);
    memset(t, 0, HO_INFO * SA1_HOSTOBJ_MAX_SHAPES);
    ho_point(&ctx->coords, s->center, c);
    if (s->form == SA1_SHAPE_BOX) {
        ho_extent(&ctx->coords, s->size, e);
        r = sa1g_sqrtf(e[0] * e[0] + e[2] * e[2]);     /* the horizontal half diagonal */
        h = e[1];
    } else if (s->form == SA1_SHAPE_CYLINDER) {
        r = s->size[0] * inv;
        h = s->size[1] * inv;
    } else {
        r = s->size[0] * inv;
        h = r;
    }
    ga = r + sl->reach;
    if (ga < HO_GRAB_MIN_R) {
        ga = HO_GRAB_MIN_R;
    }
    gb = h < HO_GRAB_MIN_H ? HO_GRAB_MIN_H : h;
    base = form | 0x800u | 0x2000u | 0xE0000u;
    if (!(s->flags & SA1_HSHAPE_NO_DAMAGE)) {
        base |= 0x80u;
    }
    if ((sl->flags & SA1_HOSTOBJ_DISABLED) || (s->flags & SA1_HSHAPE_OFF)) {
        base |= 0x10u;
    }
    for (i = 0; i < 2u; i++) {
        uint8_t *ci = t + HO_INFO * i;
        float a = i == 0 ? ga : r, b = i == 0 ? gb : h;
        ho_put32(ci + 0x00, base);
        ci[0x04] = (uint8_t)((uint32_t)sl->armor << 2);
        ci[0x05] = (uint8_t)(0xF0u | (i == 1 && solid ? 1u : 0u));
        ho_putf(ci + 0x08, c[0]);
        ho_putf(ci + 0x0C, c[1]);
        ho_putf(ci + 0x10, c[2]);
        ho_putf(ci + 0x14, a);
        ho_putf(ci + 0x18, form == 0 ? a : b);
        ci[0x2C] = 0xFF;
        ci[0x2D] = 0xFF;
        sl->tflag[i] = ci[0x04];
        sl->is_box[i] = 0;
        sl->box_yaw[i] = 0;
    }
}

static void ho_template(const sa1_context *ctx, ho_state *S, uint32_t k, const sa1_hostobj_desc *d)
{
    ho_slot *sl = &S->slot[k];
    uint8_t *t = S->gblock[k] + 0x10u;
    int solid = sl->kind == SA1_HOSTOBJ_ENEMY ? !(sl->flags & SA1_HOSTOBJ_NOT_SOLID)
                                              : (sl->flags & SA1_HOSTOBJ_SOLID) && !(sl->flags & SA1_HOSTOBJ_NOT_SOLID);
    uint32_t i;
    if (sl->kind == SA1_HOSTOBJ_HOLDABLE) {
        ho_template_hold(ctx, S, k, d);
        return;
    }
    memset(t, 0, HO_INFO * SA1_HOSTOBJ_MAX_SHAPES);
    for (i = 0; i < sl->nshapes; i++) {
        const sa1_hostobj_shape *s = &d->shapes[i];
        uint8_t *ci = t + HO_INFO * i;
        uint32_t attr = s->form | 0x2000u | 0xE0000u, flag;
        float c[3], e[3] = { 0.0f, 0.0f, 0.0f };
        if (sl->kind != SA1_HOSTOBJ_HAZARD && !(s->flags & SA1_HSHAPE_NO_DAMAGE)) {
            attr |= 0x80u;              /* damageable */
        }
        if (i == 0 && (sl->kind == SA1_HOSTOBJ_TARGET || sl->kind == SA1_HOSTOBJ_BALLOON ||
                       (sl->flags & (SA1_HOSTOBJ_NO_BONUS | SA1_HOSTOBJ_NO_LOCKON)))) {
            attr |= 0x100000u;          /* E-102: lockable but not counted */
        }
        if ((sl->flags & SA1_HOSTOBJ_DISABLED) || (s->flags & SA1_HSHAPE_OFF)) {
            attr |= 0x10u;
        }
        switch (sl->kind) {
        case SA1_HOSTOBJ_ENEMY:
            flag = ((uint32_t)sl->touch & 3u) | ((uint32_t)sl->armor << 2) | 0x20u;
            break;
        case SA1_HOSTOBJ_HAZARD:
            flag = ((uint32_t)sl->touch & 3u) | (3u << 2) | 0x20u;
            break;
        default:                        /* TARGET, BREAKABLE, BALLOON: never hurt */
            flag = (uint32_t)sl->armor << 2;
            break;
        }
        if (s->flags & SA1_HSHAPE_NO_TOUCH) {
            flag &= ~3u;
        }
        ho_point(&ctx->coords, s->center, c);
        if (s->form == SA1_SHAPE_BOX) {
            ho_extent(&ctx->coords, s->size, e);
        } else {
            float h[3] = { s->size[0], s->size[1], 0.0f }, inv = 1.0f / ctx->coords.scale;
            e[0] = h[0] * inv;
            e[1] = s->form == SA1_SHAPE_CYLINDER ? h[1] * inv : e[0];  /* a sphere: b = a, as the players' tables */
        }
        ho_put32(ci + 0x00, attr);
        ci[0x04] = (uint8_t)flag;
        ci[0x05] = (uint8_t)(0xF0u | (solid ? 1u : 0u));
        ho_putf(ci + 0x08, c[0]);
        ho_putf(ci + 0x0C, c[1]);
        ho_putf(ci + 0x10, c[2]);
        ho_putf(ci + 0x14, e[0]);
        ho_putf(ci + 0x18, e[1]);
        ho_putf(ci + 0x1C, e[2]);
        sl->is_box[i] = s->form == SA1_SHAPE_BOX;
        sl->box_yaw[i] = sl->is_box[i] ? ho_bams(s->yaw) : 0;
        ho_put32(ci + 0x24, (uint32_t)sl->box_yaw[i]);
        ci[0x2C] = 0xFF;                /* hit_info -1 */
        ci[0x2D] = 0xFF;
        sl->tflag[i] = (uint8_t)flag;
    }
}

/* ---- proxies ------------------------------------------------------------------------------------------------------ */
/* the slot's task is still its proxy (not destroyed or reused by someone else) */
static int ho_proxy_ok(const sa1g_world *w, const ho_slot *sl, uint32_t k)
{
    uint32_t twp;
    if (!ho_arena(sl->tp) || (ho_rd32(w, sl->tp + 0x10u) & 0x1FFFFFFFu) != HO_KEY) {
        return 0;
    }
    twp = ho_rd32(w, sl->tp + 0x20u);
    return twp == sl->twp && ho_arena(twp) && ho_rd16(w, twp + 0x08u) == k && ho_rd16(w, twp + 0x0Au) == HO_TAG;
}

/* the holder's drop function of a game character id (design 6.1.2): (twp, mwp, pwp), Tails (twp, pwp); 0: none */
static uint32_t ho_drop_fn(uint32_t chr)
{
    switch (chr) {
    case 0:
        return 0x8C0B1D60u;     /* Sonic */
    case 2:
        return 0x8C0BD440u;     /* Tails (twp, pwp) */
    case 3:
        return 0x8C0C6DE0u;     /* Knuckles */
    case 5:
        return 0x8C0DC75Eu;     /* Amy */
    case 6:
        return 0x8C0D15C0u;     /* E-102 */
    case 7:
        return 0x8C0E55E2u;     /* Big */
    default:
        return 0;
    }
}

/* the players holding the proxy tp drop it, each by its own drop function (inside the world, before the executor): it
 * clears the character's 0x800 and the object's 0x1000 and sets htp = NULL; a holder without one (or one the function
 * left holding) is let go natively. Returns the number of holders. */
static uint32_t ho_drop_holders(sa1g_world *w, uint32_t gtp)
{
    uint32_t pno, n = 0;
    for (pno = 0; pno < 8u; pno++) {
        uint32_t pwp = ho_ppwp(w, pno), twp = ho_ptwp(w, pno), mwp = ho_pmwp(w, pno), fn;
        if (!ho_arena(pwp) || !ho_arena(twp) || ho_rd32(w, pwp + 0x68u) != gtp) {
            continue;
        }
        n++;
        fn = ho_drop_fn(ho_rd8(w, twp + 0x09u));
        if (fn == 0x8C0BD440u) {
            ((void (*)(void *, void *))ho_fn(fn))(sa1_decode(twp), sa1_decode(pwp));
        } else if (fn != 0) {
            ((void (*)(void *, void *, void *))ho_fn(fn))(sa1_decode(twp), sa1_decode(mwp), sa1_decode(pwp));
        }
        if (ho_rd32(w, pwp + 0x68u) == gtp) {
            ho_wr32(w, pwp + 0x68u, 0);
            ho_wr16(w, twp + 0x04u, ho_rd16(w, twp + 0x04u) & ~0x800u);
        }
    }
    return n;
}

/* a HOLDABLE leaves its holder (sa1_hostobj_release, removal, kind change, mask): the holder's own drop, RELEASED
 * (FORCED) with the speed the drop gave, then the speed is cleared and the release point kept (6.1.2) */
static void ho_force_drop(sa1g_world *w, ho_state *S, ho_slot *sl, uint32_t k)
{
    uint32_t mwp, twp;
    if (sl->kind != SA1_HOSTOBJ_HOLDABLE || !ho_proxy_ok(w, sl, k)) {
        return;
    }
    twp = sl->twp;
    mwp = ho_rd32(w, sl->tp + 0x24u);
    if (ho_drop_holders(w, sl->tp) == 0 && sl->hstate != HO_HELD) {
        return;
    }
    ho_wr16(w, twp + 0x04u, ho_rd16(w, twp + 0x04u) & ~0x1000u);
    if (sl->hstate == HO_HELD) {
        ho_ev *e = ho_ev_new(w, S, SA1_HOSTOBJ_EVENT_RELEASED, sl);
        float v[3] = { 0.0f, 0.0f, 0.0f };
        if (ho_arena(mwp)) {
            ho_rd3(w, mwp + 0x04u, v);
        }
        if (e != NULL) {
            ho_rd3(w, twp + 0x20u, e->pos);
            ho_ev_vel(e, v);
            e->reason = SA1_RELEASE_FORCED;
        }
        sl->hstate = HO_FREE;
    }
    if (ho_arena(mwp)) {
        uint32_t j;
        for (j = 0; j < 6u; j++) {
            ho_wr32(w, mwp + 0x04u + 4u * j, 0);    /* spd, acc */
        }
    }
    ho_rd3(w, twp + 0x20u, sl->next);
    memcpy(sl->prev, sl->next, sizeof sl->prev);
    sl->keep_pose = 1;
    sl->rel_pv = 0xFFFFFFFFu;               /* set from the entry at the next latch */
}

static void ho_free(sa1g_world *w, ho_state *S, uint32_t k, uint32_t how)
{
    ho_slot *sl = &S->slot[k];
    uint32_t idx = (sl->id & SA1HO_IDX_MASK) - 1u;
    if (ho_proxy_ok(w, sl, k)) {
        if (sl->kind == SA1_HOSTOBJ_HOLDABLE) {
            ho_force_drop(w, S, sl, k);     /* never destroyed under its holder (6.4 row 6) */
        } else {
            uint32_t ewp = ho_rd32(w, sl->tp + 0x24u), f;
            if (ho_arena(ewp)) {
                f = ho_rd16(w, ewp + 0x4Eu);
                f = how == SA1_HOSTOBJ_REMOVE_KILLED ? (f | 0x1000u) : (f & ~0x1000u);
                /* E-102: a marker released at a hit (ho_hit) took the target's link to its shot group with it; a KILLED,
                 * counted target pays the time bonus as the hooked dest 8C0D32C0 would have with the marker: the group
                 * when it is still the running bonus task, else the game's markerless path (NULL) */
                if (how == SA1_HOSTOBJ_REMOVE_KILLED && sl->group_valid && ho_rd32(w, ewp + 0x40u) == 0 &&
                    (ho_rd32(w, sl->tp + 0x18u) & 0x1FFFFFFFu) == HO_DEST_E102) {
                    uint32_t cwp = ho_rd32(w, sl->twp + 0x38u), info = ho_arena(cwp) ? ho_rd32(w, cwp + 0x0Cu) : 0u;
                    if (ho_arena(info) && !(ho_rd32(w, info) & 0x100000u)) {
                        uint32_t g = sl->group, run = ho_rd32(w, HO_ADDR_BONUS_TASK);
                        ((void (*)(void *))ho_fn(HO_FN_TIME_BONUS))(g != 0 && g == run ? sa1_decode(g) : NULL);
                        f &= ~0x1000u;      /* the dest must not pay it again */
                    }
                }
                ho_wr16(w, ewp + 0x4Eu, f);
            }
            if (sl->marker != 0) {
                ho_push(w, S, SA1_HOSTOBJ_EVENT_UNLOCKED, sl, 0, 0, 0, 0, 0, NULL);   /* the dest releases it */
            }
        }
        ((void (*)(void *))ho_fn(HO_FN_FREETASK))(sa1_decode(sl->tp));
    }
    if (idx < SA1HO_MAX && S->slot_of[idx] == k + 1u) {
        S->slot_of[idx] = 0;
    }
    memset(sl, 0, sizeof *sl);
    sl->freed_pair = S->pairs;
    S->live--;
    S->destroyed++;
}

/* a proxy of entry e in the free slot k (inside the world); 0 = refused (pool, heap) */
static int ho_create(sa1g_world *w, ho_state *S, uint32_t k, const sa1ho_entry *e, uint32_t idx)
{
    const sa1_context *ctx = w->hostobj_ctx;
    ho_slot *sl = &S->slot[k];
    uint8_t *gb = S->gblock[k];
    uint32_t gaddr = ho_gblock_guest(S, k), largest = 0, gtp, gtwp, j;
    int hold = e->desc.kind == SA1_HOSTOBJ_HOLDABLE;
    void *tp;
    float pos[3];
    int32_t yaw;
    memset(sl, 0, sizeof *sl);
    S->bombed[k] = 0;
    S->bomb_r[k] = 0.0f;
    ho_slot_desc(ctx, sl, e);
    ho_template(ctx, S, k, &e->desc);
    ho_point(&ctx->coords, e->desc.position, pos);
    yaw = ho_bams(e->desc.yaw);
    ho_heap_walk(w, NULL, &largest);
    if (largest < HO_HEADROOM) {
        memset(sl, 0, sizeof *sl);
        return 0;
    }
    /* a HOLDABLE also gets a motion work (im & 1): the game's throw writes htp->mwp->spd (6.1.2) */
    tp = ((void *(*)(uint16_t, int32_t, sa1_gfn))ho_fn(SA1G_FN_CREATE_TASK))(hold ? 3 : 2, 3, SA1_GFN_C(HO_CODE));
    if (tp == NULL) {
        memset(sl, 0, sizeof *sl);
        return 0;
    }
    gtp = sa1_encode(tp);
    gtwp = ho_rd32(w, gtp + 0x20u);
    if (!ho_arena(gtwp) || (hold && !ho_arena(ho_rd32(w, gtp + 0x24u)))) {
        ((void (*)(void *))ho_fn(HO_FN_FREETASK))(tp);
        memset(sl, 0, sizeof *sl);
        return 0;
    }
    /* the taskwk: id 0, flag 0 (CAlloc'd zero), the slot and the tag, the pose */
    ho_wr16(w, gtwp + 0x08u, k);
    ho_wr16(w, gtwp + 0x0Au, HO_TAG);
    ho_wr32(w, gtwp + 0x18u, (uint32_t)yaw);
    for (j = 0; j < 3; j++) {
        ho_wrf(w, gtwp + 0x20u + 4u * j, pos[j]);
    }
    if (hold) {
        /* the game's movable-object set-up 8C0586A0 (ObjectMovableInitialize): taskwk.id = the lift class (4, HEAVY 5),
         * flag |= 0x240 (0x640), the radius and fall weight of the class in the motion work */
        int32_t cls = (sl->flags & SA1_HOSTOBJ_HEAVY) ? 5 : 4;
        ((int32_t (*)(void *, void *, int32_t))ho_fn(HO_FN_MOVABLE_INIT))(sa1_decode(gtwp),
                                                                          sa1_decode(ho_rd32(w, gtp + 0x24u)), cls);
        if (sl->flags & SA1_HOSTOBJ_HOLD_LOCKED) {
            ho_wr16(w, gtwp + 0x04u, ho_rd16(w, gtwp + 0x04u) | 0x1000u);
        }
    } else {
        /* the enemy work: the game's own initialiser 8C047040 (EnemyInitialize: CAlloc(1, 0x23C) into tp->mwp, the home
         * position twp->pos at +0x50, its defaults, +0x4E |= 0x480 &= 0xCFFF; DestroyTask frees it). Without memory it
         * frees the task itself and returns NULL */
        void *ewp = ((void *(*)(void *, void *))ho_fn(HO_FN_ENEMY_INIT))(tp, sa1_decode(gtwp));
        if (ewp == NULL) {
            memset(sl, 0, sizeof *sl);
            return 0;
        }
    }
    ((void (*)(void *, void *, uint8_t, int32_t, uint8_t))ho_fn(HO_FN_CCL_INIT))(tp, sa1_decode(gaddr + 0x10u),
                                                                               (uint8_t)sl->ccl_id, 1,
                                                                               (uint8_t)sl->nshapes);
    if (!ho_arena(ho_rd32(w, gtwp + 0x38u))) {
        /* CCL_Init's second CAlloc failed (6.4 row 3): its colliwk is lost, as in the game */
        ((void (*)(void *))ho_fn(HO_FN_FREETASK))(tp);
        memset(sl, 0, sizeof *sl);
        return 0;
    }
    /* the OBJ_CONDITION stand-in (ssCondition 1, pTask = the proxy) */
    memset(gb, 0, 0x10u);
    gb[2] = 1;
    ho_put32(gb + 4, gtp);
    ho_wr32(w, gtp + 0x1Cu, gaddr);                     /* tp->ocp */
    sl->id = e->id;
    sl->tp = gtp;
    sl->twp = gtwp;
    sl->release_seq = e->release_seq;
    memcpy(sl->prev, pos, sizeof sl->prev);
    memcpy(sl->next, pos, sizeof sl->next);
    sl->prev_yaw = sl->next_yaw = yaw;
    S->slot_of[idx] = (uint8_t)(k + 1u);
    S->live++;
    S->created++;
    return 1;
}

/* ---- the window (pre, sub step 0) -------------------------------------------------------------------------------- */
static int ho_less(const ho_cand *a, const ho_cand *b)
{
    return a->d2 < b->d2 || (a->d2 == b->d2 && a->id < b->id);
}

/* stable bottom-up merge sort of S->cand[0..n) by (d2, id) */
static void ho_sort(ho_state *S, uint32_t n)
{
    ho_cand *src = S->cand, *dst = S->tmp, *t;
    uint32_t width, i;
    for (width = 1; width < n; width *= 2u) {
        for (i = 0; i < n; i += 2u * width) {
            uint32_t a = i, am = i + width < n ? i + width : n, b = am, bm = i + 2u * width < n ? i + 2u * width : n;
            uint32_t k = i;
            while (a < am && b < bm) {
                dst[k++] = ho_less(&src[b], &src[a]) ? src[b++] : src[a++];
            }
            while (a < am) {
                dst[k++] = src[a++];
            }
            while (b < bm) {
                dst[k++] = src[b++];
            }
        }
        t = src;
        src = dst;
        dst = t;
    }
    if (src != S->cand) {
        memcpy(S->cand, src, (size_t)n * sizeof *src);
    }
}

static int ho_player_pos(const sa1g_world *w, float p[3])
{
    uint32_t twp = ho_rd32(w, SA1G_ADDR_PLAYER_TWP);
    if (!ho_arena(twp)) {
        return 0;
    }
    ho_rd3(w, twp + 0x20u, p);
    return p[0] == p[0] && p[1] == p[1] && p[2] == p[2];
}

/* the world mirrors this kind (sa1_hostobj_world_mask) */
static int ho_kind_on(const sa1g_world *w, uint32_t kind)
{
    return w->ho.kind_mask == 0 || (w->ho.kind_mask & SA1_HOSTOBJ_KIND_BIT(kind)) != 0;
}

static void ho_window(sa1g_world *w, ho_state *S, const sa1ho_registry *R)
{
    const sa1_context *ctx = w->hostobj_ctx;
    float p[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t n = 0, m = 0, i, k, refused = 0;
    memset(S->sel, 0, sizeof S->sel);
    if (R != NULL && ho_player_pos(w, p)) {
        uint32_t win = R->window ? R->window : HO_SLOTS;
        float enter = R->enter_radius > 0.0f ? R->enter_radius / ctx->coords.scale : HO_ENTER;
        float leave = R->leave_radius > 0.0f ? R->leave_radius / ctx->coords.scale : HO_LEAVE, e2, l2;
        if (win > HO_SLOTS) {
            win = HO_SLOTS;
        }
        if (leave < enter) {
            leave = enter;
        }
        e2 = enter * enter;
        l2 = leave * leave;
        for (i = 0; i < R->hiwater && i < SA1HO_MAX; i++) {
            const sa1ho_entry *e = &R->ent[i];
            float s[3], dx, dy, dz, d2;
            uint32_t sl;
            int held;
            if (e->id == 0 || !ho_kind_on(w, e->desc.kind)) {
                continue;
            }
            ho_point(&ctx->coords, e->desc.position, s);
            dx = s[0] - p[0];
            dy = s[1] - p[1];
            dz = s[2] - p[2];
            d2 = dx * dx + dy * dy + dz * dz;
            sl = S->slot_of[i];
            held = sl != 0 && S->slot[sl - 1u].id == e->id;
            if (held && S->slot[sl - 1u].hstate == HO_HELD && e->desc.kind == SA1_HOSTOBJ_HOLDABLE) {
                d2 = 0.0f;              /* a held object is at the hands: it never leaves the window */
            }
            if (d2 < e2 || (held && d2 < l2)) {
                S->cand[n].d2 = d2;
                S->cand[n].id = e->id;
                S->cand[n].idx = i;
                n++;
            }
        }
        ho_sort(S, n);
        if (n > win) {
            n = win;
        }
        for (k = 0; k < n; k++) {
            S->sel[S->cand[k].idx] = 1;
        }
    }
    /* 1. the proxies of objects that are gone, left the window, changed kind / shape count or left the mask */
    for (k = 0; k < HO_SLOTS; k++) {
        ho_slot *sl = &S->slot[k];
        const sa1ho_entry *e;
        uint32_t idx;
        if (sl->id == 0) {
            continue;
        }
        idx = (sl->id & SA1HO_IDX_MASK) - 1u;
        e = sa1ho_entry_of(R, sl->id);
        if (e == NULL) {
            uint32_t how = SA1_HOSTOBJ_REMOVE_SILENT;
            if (R != NULL && idx < SA1HO_MAX && R->ent[idx].removed_id == sl->id) {
                how = R->ent[idx].removed_how;
            }
            ho_free(w, S, k, how);
        } else if (!S->sel[idx] || ho_ccl_id(e->desc.kind) != sl->ccl_id || ho_nshapes(&e->desc) != sl->nshapes ||
                   (e->desc.kind == SA1_HOSTOBJ_HOLDABLE) != (sl->kind == SA1_HOSTOBJ_HOLDABLE) ||
                   (sl->kind == SA1_HOSTOBJ_HOLDABLE &&
                    ((e->desc.flags ^ sl->flags) & SA1_HOSTOBJ_HEAVY))) {
            ho_free(w, S, k, SA1_HOSTOBJ_REMOVE_SILENT);
        } else if (!ho_proxy_ok(w, sl, k)) {
            /* destroyed by someone else (nothing in the game does that today): recreated below */
            if (S->slot_of[idx] == k + 1u) {
                S->slot_of[idx] = 0;
            }
            memset(sl, 0, sizeof *sl);
            sl->freed_pair = S->pairs;
            S->live--;
            S->destroyed++;
        } else if (sl->kind == SA1_HOSTOBJ_HOLDABLE && e->release_seq != sl->release_seq) {
            sl->release_seq = e->release_seq;   /* sa1_hostobj_release: the holder drops it */
            ho_force_drop(w, S, sl, k);
        }
    }
    /* 2. new proxies in ascending id, each in the lowest free slot not freed in this pair */
    for (k = 0; k < n; k++) {
        uint32_t idx = S->cand[k].idx, sl = S->slot_of[idx];
        if (!(sl != 0 && S->slot[sl - 1u].id == S->cand[k].id)) {
            ho_cand c = S->cand[k];
            uint32_t j = m;
            while (j > 0 && S->tmp[j - 1u].id > c.id) {
                S->tmp[j] = S->tmp[j - 1u];
                j--;
            }
            S->tmp[j] = c;
            m++;
        }
    }
    for (i = 0; i < m; i++) {
        const sa1ho_entry *e = sa1ho_entry_of(R, S->tmp[i].id);
        for (k = 0; k < HO_SLOTS; k++) {
            if (S->slot[k].id == 0 && S->slot[k].freed_pair != S->pairs) {
                break;
            }
        }
        if (k == HO_SLOTS || e == NULL) {
            continue;               /* the slots freed in this pair are free at the next one */
        }
        if (!ho_create(w, S, k, e, S->tmp[i].idx)) {
            refused++;
        }
    }
    S->refused += refused;
    if (refused != 0) {
        if (!S->cap_active) {
            sa1g_world_event_push(w, SA1_EVENT_CAPACITY, SA1_CAPACITY_HOST_OBJECTS, 0u, p, refused);
        }
        S->cap_active = 1;
    } else {
        S->cap_active = 0;
    }
    /* 3. this pair's poses and descriptions */
    for (k = 0; k < HO_SLOTS; k++) {
        ho_slot *sl = &S->slot[k];
        const sa1ho_entry *e = sa1ho_entry_of(R, sl->id);
        if (sl->id == 0 || e == NULL) {
            continue;
        }
        if (e->desc_version != sl->desc_version) {
            uint32_t cool = sl->cooldown;
            ho_slot_desc(ctx, sl, e);
            ho_template(ctx, S, k, &e->desc);
            sl->cooldown = cool;
            sl->dirty = 1;
        }
        if (sl->kind == SA1_HOSTOBJ_HOLDABLE) {
            if (sl->hstate == HO_HELD) {
                continue;               /* the character owns its pose */
            }
            if (sl->keep_pose) {
                if (sl->rel_pv == 0xFFFFFFFFu) {
                    sl->rel_pv = e->pose_version;
                }
                if (e->pose_version == sl->rel_pv) {
                    memcpy(sl->prev, sl->next, sizeof sl->prev);
                    sl->prev_yaw = sl->next_yaw;
                    continue;           /* the release point until the host moves the object */
                }
                sl->keep_pose = 0;
            }
        }
        memcpy(sl->prev, sl->next, sizeof sl->prev);
        sl->prev_yaw = sl->next_yaw;
        ho_point(&ctx->coords, e->desc.position, sl->next);
        sl->next_yaw = ho_bams(e->desc.yaw);
    }
}

/* ---- the character side before the executor (design 2.5), every step --------------------------------------------- */
static void ho_hurt_push(sa1g_world *w, uint32_t object, uint32_t lost, uint32_t flags)
{
    uint32_t twp = ho_ptwp(w, 0);
    float pos[3] = { 0.0f, 0.0f, 0.0f };
    if (ho_arena(twp)) {
        ho_rd3(w, twp + 0x20u, pos);
    }
    sa1g_world_event_push(w, SA1_EVENT_HURT, object, (uint32_t)(int16_t)ho_rd16(w, SA1G_ADDR_RINGS) & 0xFFFFu, pos,
                          (lost & 0xFFFFu) | (flags << 16));
}

/* the host object of a proxy colliwk (0: none) */
static uint32_t ho_object_of_cwp(const sa1g_world *w, const ho_state *S, uint32_t cwp)
{
    int32_t k;
    if (!ho_arena(cwp)) {
        return 0;
    }
    k = ho_slot_of_task(w, S, ho_rd32(w, cwp + 0x10u));
    return k >= 0 ? S->slot[k].id : 0u;
}

/* round 0.9.1 (E1): the death-zone task (exec 8C07BAC0: the boot's SA1G_BOOT_DEATHZONE in DISC / stage worlds, the
 * collision adapter's for GRID kill planes, the harness's stage start), 0: none */
static uint32_t ho_deathzone_task(const sa1g_world *w)
{
    uint32_t lv;
    for (lv = 0; lv < 8u; lv++) {
        uint32_t tp = ho_rd32(w, SA1G_ADDR_TASK_LISTS + 4u * lv), guard = 0;
        while (ho_arena(tp) && guard++ < 0x1000u) {
            if ((ho_rd32(w, tp + 0x10u) & 0x1FFFFFFFu) == (HO_FN_DEATHZONE & 0x1FFFFFFFu)) {
                return tp;
            }
            tp = ho_rd32(w, tp);
        }
    }
    return 0;
}

static void ho_char_pre(sa1g_world *w, const ho_state *S)
{
    sa1g_ho_char *c = &w->ho;
    uint32_t twp = ho_ptwp(w, 0), pwp = ho_ppwp(w, 0), mwp = ho_pmwp(w, 0), tflag, wt, item, i;
    int32_t rings;
    int pend;
    /* round 0.9.1 (E1, sadc docs/research/gsa1_070_drown.md with its Review R3.6 / R4.2): what hostobj_post's death rules
       compare with (read only) */
    c->ev_pre = w->events.count;
    c->smode_before = ho_arena(twp) ? (int32_t)(int8_t)ho_rd8(w, twp + 0x01u) : 0;
    c->dz_task = ho_deathzone_task(w);
    c->dz_child_before = c->dz_task != 0 ? ho_rd32(w, c->dz_task + 0x0Cu) : 0u;
    c->src_object = 0;
    c->src_flags = 0;
    c->phantom = 0;
    c->death_cause = SA1_DEATH_OTHER;
    c->death_object = 0;
    if (!ho_arena(twp)) {
        c->ndmg = 0;
        c->rings_before = 0;
        c->item_before = 0;
        return;
    }
    rings = (int16_t)ho_rd16(w, SA1G_ADDR_RINGS);
    item = ho_arena(pwp) ? ho_rd16(w, pwp + 0x06u) : 0u;
    c->rings_before = rings;
    c->item_before = item;
    /* 1. host damage requests (sa1_character_damage).
     * Round 0.9.2 (sadc docs/research/gsa1_090_faults.md F1 with its Review R.3.5): only with player 0's playerwk in the
     * arena. Before the character's first step the player task has not run PInitialize (its case 0 creates the playerwk
     * and enters the CCL lists), and the game cannot take a hit there: Sonic's damage check reads pwp->equipment through
     * NULL (8C0AFB10, read 00000004), Tails' writes pwp->mj.reqaction (8C0BBAE2), the death request writes
     * playerpwp[0]->item (8C07BBC2), the others clear the hurt bit in their first exec (the hit is lost); the original
     * code freezes in mode 82 (harness, faults 1.5). So while the task is still in mode 0 (taskwk byte 0) the queue is
     * HELD and applied at the next step, the first one with the playerwk (one step later than asked, as a request made
     * one step later); a missing playerwk in any other state DROPS the queue, so a request never waits for a playerwk
     * that may never come. */
    if (c->ndmg != 0 && !ho_arena(pwp)) {
        if (ho_rd8(w, twp) != 0u) {
            c->ndmg = 0;
        }
        goto attribution;
    }
    for (i = 0; i < c->ndmg && i < SA1G_HO_DAMAGE_MAX; i++) {
        const sa1g_ho_damage *d = &c->dmg[i];
        tflag = ho_rd16(w, twp + 0x04u);
        wt = ho_rd16(w, twp + 0x06u);
        item = ho_arena(pwp) ? ho_rd16(w, pwp + 0x06u) : 0u;
        if (d->flags & SA1_DAMAGE_KILL) {
            if (!(item & 0x4000u)) {
                ((void (*)(uint8_t))ho_fn(HO_FN_DEATH))(0);     /* the game's death request */
                c->death_cause = SA1_DEATH_HOST;
                c->death_object = d->object;
            }
            continue;
        }
        if ((c->opts & (1u << SA1_COPT_NO_DAMAGE)) || wt != 0 || (item & 0x8000u)) {
            ho_hurt_push(w, d->object, 0, SA1_HURT_IGNORED | SA1_HURT_HOST);
            continue;
        }
        if (d->flags & SA1_DAMAGE_FACE_SOURCE) {
            float s[3], p[3], dx, dz;
            ho_point(&w->hostobj_ctx->coords, d->source, s);
            ho_rd3(w, twp + 0x20u, p);
            dx = s[0] - p[0];
            dz = s[2] - p[2];
            if (dx != 0.0f || dz != 0.0f) {
                /* the facing of ang.y = a is (cos a, 0, sin a): a = atan2(dz, dx), the game's conversion
                 * (SA1_FTOI(atan2f(z, x) * 10430.381f), e.g. 8C0D3DA0) */
                float r = ((float (*)(float, float))ho_fn(HO_FN_ATAN2))(dz, dx);
                uint32_t a = (uint32_t)(int32_t)(r * 10430.381f) & 0xFFFFu;
                ho_wr32(w, twp + 0x18u, a);
                if (ho_arena(mwp)) {
                    ho_wr32(w, mwp + 0x20u, a);         /* motionwk.ang_aim.y */
                }
            }
        }
        ho_wr16(w, twp + 0x04u, ho_rd16(w, twp + 0x04u) | 4u);   /* what an enemy contact leaves after a pass */
        c->src_object = d->object;
        c->src_flags = SA1_HURT_HOST;
    }
    c->ndmg = 0;
attribution:
    /* 2. the hurt attribution of a pending hit (before the character's exec reads it and its CCL_Entry resets it) */
    tflag = ho_rd16(w, twp + 0x04u);
    wt = ho_rd16(w, twp + 0x06u);
    item = ho_arena(pwp) ? ho_rd16(w, pwp + 0x06u) : 0u;
    pend = (tflag & 4u) || (ho_arena(pwp) && (ho_rd32(w, pwp + 0x5Cu) & 0x10000u));
    if (pend && c->src_flags == 0) {
        if (tflag & 4u) {
            uint32_t cwp = ho_rd32(w, twp + 0x38u);
            c->src_object = ho_arena(cwp) ? ho_object_of_cwp(w, S, ho_rd32(w, cwp + 0x18u)) : 0u;
        } else {
            c->src_flags = SA1_HURT_SURFACE;
        }
    }
    /* 3. the options, without touching game code (design 2.5) */
    if (pend && wt == 0 && !(item & 0x8000u) && ho_arena(pwp) && c->opts != 0) {
        if (c->opts & (1u << SA1_COPT_NO_DAMAGE)) {
            ho_wr16(w, twp + 0x06u, 1);     /* the mode machine drops the hit (wtimer--, flag &= ~4); 1 & 2 = 0: no blink */
        } else if (!(item & 3u) && ((c->opts & (1u << SA1_COPT_KEEP_RINGS)) ||
                                    ((c->opts & (1u << SA1_COPT_NO_DEATH)) && rings <= 0))) {
            ho_wr16(w, pwp + 0x06u, item | 1u);     /* a phantom barrier: consumed like a barrier, no display task */
            c->phantom = 1;
        }
    }
}

static void ho_pre(sa1g_world *w, int32_t sub, void *user)
{
    const sa1ho_registry *R;
    ho_state *S;
    uint32_t twp;
    (void)user;
    if (w->charopt_pre != NULL) {
        w->charopt_pre(w, sub);     /* round 0.9.1: a companion freed right after the first pair (sa1_world_charopt.c) */
    }
    S = ho_state_of(w);
    if (sub == 0) {
        R = sa1ho_registry_of(w->hostobj_ctx);
        if (!((R == NULL || R->count == 0) && (S == NULL || S->live == 0))) {
            if (S == NULL) {
                S = ho_state_create(w);     /* NULL: segment 0x18 is full (16 MB): no proxies in this world */
            }
            if (S != NULL) {
                S->pairs++;
                ho_window(w, S, R);
            }
        }
    }
    ho_char_pre(w, S);
    twp = ho_rd32(w, SA1G_ADDR_PLAYER_TWP);
    w->ho_wtimer = ho_arena(twp) ? ho_rd16(w, twp + 0x06u) : 0u;
}

/* ---- the exec (level 3) ------------------------------------------------------------------------------------------- */
/* design 2.6: the attack kind of a snapshot */
static uint32_t ho_classify(const ho_snap *sn)
{
    if (sn->aid == 1u) {
        uint32_t x = sn->aexec & 0x1FFFFFFFu;
        if (x == HO_EXEC_LASER) {
            return SA1_ATTACK_LASER;
        }
        if (x == HO_EXEC_BLAST) {
            return SA1_ATTACK_LASER_BLAST;
        }
        if (x == HO_EXEC_LURE) {
            return SA1_ATTACK_LURE;
        }
        if (x == HO_EXEC_SPARKLE || x == HO_EXEC_SPARKLE2) {
            return SA1_ATTACK_SPARKLE;
        }
        return SA1_ATTACK_OTHER;
    }
    if (sn->aid != 0u || sn->apno < 0) {
        return SA1_ATTACK_OTHER;
    }
    if (sn->aitem & 0x8000u) {
        return SA1_ATTACK_INVINCIBLE;
    }
    if (sn->aequip & 0x8000u) {
        return SA1_ATTACK_SUPER;
    }
    switch (sn->achr) {
    case 2:     /* Tails */
        if (sn->ashape == 1 || sn->ashape == 2) {
            return SA1_ATTACK_TAIL;
        }
        if (sn->ashape == 3) {
            return SA1_ATTACK_TAILS_FLIGHT;
        }
        break;
    case 3:     /* Knuckles */
        if (sn->ashape == 1) {
            return SA1_ATTACK_PUNCH;
        }
        if (sn->ashape == 2 || sn->ashape == 3) {
            return SA1_ATTACK_DIG;
        }
        break;
    case 5:     /* Amy */
        if (sn->ashape == 1) {
            return SA1_ATTACK_HAMMER;
        }
        break;
    case 7:     /* Big */
        if (sn->ashape == 1) {
            return SA1_ATTACK_BIG_ATTACK;
        }
        break;
    default:
        break;
    }
    if (sn->achr == 0u) {
        if (sn->amode == 14u || sn->amode == 77u) {
            return SA1_ATTACK_HOMING;
        }
        if (sn->amode == 44u) {
            return SA1_ATTACK_LIGHT_SPEED_ATTACK;
        }
        /* the spin dash: the charge (mode 4) and the roll it releases (mode 5, action 15: 8C0B11C0); the light speed
         * dash (mode 6, 8C0B1352: attack 1, defence 3) counts as one too. (Design 2.6 named mode 6 the spin dash's
         * release; the code says mode 5, and mode 6 is the light speed dash.) */
        if (sn->amode == 4u || sn->amode == 5u || sn->amode == 6u) {
            return SA1_ATTACK_SPIN_DASH;
        }
    }
    if (sn->achr == 3u && sn->amode == 18u) {
        return SA1_ATTACK_MAX_HEAT;
    }
    if (sn->aflag & 0x100u) {
        return SA1_ATTACK_SPIN;
    }
    return SA1_ATTACK_OTHER;
}

/* the defence level of every shape: 3 (cooldown) or the template's */
static void ho_defence(const sa1g_world *w, const ho_slot *sl, uint32_t cwp, int cool)
{
    uint32_t info = ho_rd32(w, cwp + 0x0Cu), i;
    if (!ho_arena(info)) {
        return;
    }
    for (i = 0; i < sl->nshapes; i++) {
        uint8_t *f = ho_at(w, info + HO_INFO * i + 4u, 1);
        if (f != NULL) {
            *f = (uint8_t)((*f & ~0x0Cu) | (cool ? 0x0Cu : (sl->tflag[i] & 0x0Cu)));
        }
    }
}

/* BALLOON (6.2.2): the attacking character's launch; returns SA1_HITF_LAUNCHED or 0 */
static uint32_t ho_launch(sa1g_world *w, const ho_slot *sl, const ho_snap *sn)
{
    uint32_t pno, ptwp, pmwp, pf, chr;
    float v, hx = 0.0f, hz = 0.0f;
    if (!sn->valid || sn->aid != 0u || sn->apno < 0) {
        return 0;                       /* a projectile pops it without launching anybody */
    }
    pno = (uint32_t)sn->apno;
    ptwp = ho_ptwp(w, pno);
    pmwp = ho_pmwp(w, pno);
    if (!ho_arena(ptwp) || !ho_arena(pmwp) || !ho_arena(ho_ppwp(w, pno))) {
        return 0;
    }
    v = sl->launch > 0.0f ? sl->launch : HO_SPRING_SPEED;
    if (sl->flags & SA1_HOSTOBJ_KEEP_HSPEED) {
        hx = ho_rdf(w, pmwp + 0x04u);
        hz = ho_rdf(w, pmwp + 0x0Cu);
    }
    pf = ho_rd16(w, ptwp + 0x04u);
    chr = ho_rd8(w, ptwp + 0x09u);
    if ((sl->flags & SA1_HOSTOBJ_LAUNCH_BOUNCE) && (pf & 0x100u) && !(pf & 3u) && (chr == 0u || chr == 2u || chr == 3u)) {
        ((void (*)(uint8_t, float, float, float))ho_fn(HO_FN_SETVEL))((uint8_t)pno, hx, v, hz);
    } else {
        ((void (*)(uint8_t, float, float, float))ho_fn(HO_FN_SPRING))((uint8_t)pno, hx, v, hz);
    }
    ((void (*)(int32_t, int32_t, int32_t, int32_t))ho_fn(HO_FN_RUMBLE))((int32_t)pno, 4, 15, 0);
    return SA1_HITF_LAUNCHED;
}

/* step 1 on taskwk.flag 4 (or a BALLOON's POP_ON_TOUCH contact): the game's damage check (a BALLOON: the launch), a HIT
 * event, the marker release of a hit by E-102's shot, the cooldown */
static void ho_hit(sa1g_world *w, ho_state *S, ho_slot *sl, uint32_t twp, uint32_t cwp, uint32_t ewp)
{
    const ho_snap *sn = &sl->snap;
    uint32_t hc = ho_rd32(w, cwp + 0x18u), flags = 0, attack = SA1_ATTACK_OTHER;
    uint32_t level = 0, damage, f, m;
    int same = 0;
    if (sn->valid) {
        uint32_t atwp = ho_arena(sn->atp) ? ho_rd32(w, sn->atp + 0x20u) : 0u;
        same = hc == sn->acwp && ho_arena(sn->atp) && ho_rd32(w, sn->atp + 0x10u) == sn->aexec && ho_arena(atwp)
               && ho_rd32(w, atwp + 0x38u) == sn->acwp;
        attack = ho_classify(sn);
        level = sn->alevel;
        if (sn->aid == 1u) {
            flags |= SA1_HITF_PROJECTILE;
        }
        /* 8C048980's conditions, as the damage check is about to test them: the attacking player's ball, in the air,
         * Sonic, Tails or Knuckles (8C0489E4 picks pno 0 for player 0's body, else 1; a projectile's owner is never a
         * player: nobody bounces) */
        if (sl->kind != SA1_HOSTOBJ_BALLOON && sn->aid == 0u && (sn->apno == 0 || sn->apno == 1) &&
            !(sn->aattr & 0x200u)) {
            uint32_t pt = ho_ptwp(w, (uint32_t)sn->apno);
            if (ho_arena(pt)) {
                uint32_t pf = ho_rd16(w, pt + 0x04u), chr = ho_rd8(w, pt + 0x09u);
                if ((pf & 0x100u) && !(pf & 3u) && (chr == 0u || chr == 2u || chr == 3u)) {
                    flags |= SA1_HITF_BOUNCED;
                }
            }
        }
    }
    m = ho_rd32(w, ewp + 0x40u);
    if (m != 0) {
        flags |= SA1_HITF_LOCKED;
    }
    if (sl->kind == SA1_HOSTOBJ_BALLOON) {
        /* no squash, no score, no enemy bounce: the launch (6.2.2) */
        flags |= ho_launch(w, sl, sn);
        if (sn->valid && (sn->aid == 0u || sn->aid == 1u)) {
            ho_wr16(w, ewp + 0x4Eu, ho_rd16(w, ewp + 0x4Eu) | 0x1000u);     /* hit by a player / projectile */
        }
    } else if (same) {
        ((int32_t (*)(void *, void *))ho_fn(HO_FN_DAMAGE))(sa1_decode(twp), sa1_decode(ewp));
    } else {
        /* the attacker is gone since the pass: 8C0489E4's effects without dereferencing it */
        ho_wrf(w, ewp + 0x1A0u, 0.35f);
        if (sn->valid && (sn->aid == 0u || sn->aid == 1u)) {
            f = ho_rd16(w, ewp + 0x4Eu);
            ho_wr16(w, ewp + 0x4Eu, f | 0x1000u);
            ((void (*)(int32_t))ho_fn(HO_FN_SCORE))(10);
            if (sn->aattr & 0x200u) {
                ho_wrf(w, ewp + 0x08u, 0.7f);
                ho_wr16(w, ewp + 0x4Eu, ho_rd16(w, ewp + 0x4Eu) | 0x800u);
            } else {
                ((void (*)(int8_t))ho_fn(HO_FN_BOUNCE))((int8_t)(sn->ap0 ? 0 : 1));
                ho_wr16(w, ewp + 0x4Eu, ho_rd16(w, ewp + 0x4Eu) & 0xFBFFu);
            }
        }
    }
    /* E-102's shot: release the marker at the hit (8C0D5D90; the game releases it at the target's death, one step later),
     * so a shot never stays glued to a surviving target. The marker's shot group (its awp +0x2C) is remembered for the
     * time bonus of a later KILLED removal (ho_free) */
    if (m != 0 && sn->valid && sn->aid == 1u && (sn->aexec & 0x1FFFFFFFu) == HO_EXEC_LASER) {
        uint32_t awp = ho_arena(m) ? ho_rd32(w, m + 0x2Cu) : 0u;
        sl->group = ho_arena(awp) ? ho_rd32(w, awp + 0x2Cu) : 0u;
        sl->group_valid = 1;
        ((void (*)(void *))ho_fn(HO_FN_MARKER_REL))(sa1_decode(sl->tp));
    }
    damage = level > (uint32_t)sl->armor ? level - (uint32_t)sl->armor : 1u;
    ho_push(w, S, SA1_HOSTOBJ_EVENT_HIT, sl, attack, level, damage,
            sn->valid && sn->myshape >= 0 ? (uint32_t)sn->myshape : 0u, flags, sn);
    /* the cooldown: defence 3, out of the target lists, "killed" for E-102's lock search */
    sl->cooldown = sl->cooldown_steps;
    ho_defence(w, sl, cwp, 1);
    ho_wr16(w, cwp + 0x02u, ho_rd16(w, cwp + 0x02u) & ~0x800u);
    ho_wr16(w, ewp + 0x4Eu, ho_rd16(w, ewp + 0x4Eu) | 0x1000u);
}

/* round 0.9.2 (sadc docs/research/gsa1_090_faults.md F2, gsa1_090_plan.md 6.4, owner default: the bomb also hurts GMod
 * NPCs): the bomb item's sphere. A game enemy runs its damage check 8C0489E4 in every exec; without a hit in the pass
 * (taskwk.flag 4 clear) the check returns 8C05E5C2(&twp->pos): 1 inside the bomb sphere (centre B_8C78C384, radius
 * B_8C78C390, started at 10 by the bomb's handler 8C05D930 -> 8C05E582, grown by 10 per step and ended past 500 by the
 * item icon list task 8C05E14E -> 8C05E59A, which the world boot creates), and the enemy is destroyed. A proxy runs that
 * check only on flag 4 (ho_hit), so here an ENEMY proxy (hostile / neutral NPCs, PvP players in G-SA1; not a TARGET,
 * BREAKABLE, HAZARD, BALLOON or HOLDABLE: the game's bomb only reaches enemies) asks the same check while it has no hit
 * and no cooldown: inside, it takes one HIT per sphere (SA1_ATTACK_OTHER, level 3, at the sphere's centre, no
 * velocity) and the hit's cooldown. A DISABLED object is never hit. Reference worlds have no proxies: nothing changes
 * there. */
static void ho_bomb(sa1g_world *w, ho_state *S, ho_slot *sl, uint32_t k, uint32_t twp, uint32_t cwp, uint32_t ewp)
{
    float r = ho_rdf(w, HO_ADDR_BOMB_R);
    uint32_t damage;
    ho_snap sn;
    if (!(r >= 0.0f)) {
        S->bombed[k] = 0;                           /* no sphere */
        return;
    }
    if (S->bombed[k] && r < S->bomb_r[k]) {
        S->bombed[k] = 0;                           /* a new bomb started while the last one ran */
    }
    if (sl->kind != SA1_HOSTOBJ_ENEMY || (sl->flags & SA1_HOSTOBJ_DISABLED) || S->bombed[k] || sl->cooldown != 0 ||
        (ho_rd16(w, twp + 0x04u) & 4u)) {
        return;
    }
    if (((int32_t (*)(void *, void *))ho_fn(HO_FN_DAMAGE))(sa1_decode(twp), sa1_decode(ewp)) == 0) {
        return;
    }
    S->bombed[k] = 1;
    S->bomb_r[k] = r;
    memset(&sn, 0, sizeof sn);
    sn.valid = 1;                                   /* the event's position: the sphere's centre, no velocity */
    sn.apno = -1;
    ho_rd3(w, HO_ADDR_BOMB_C, sn.apos);
    damage = HO_BOMB_LEVEL > (uint32_t)sl->armor ? HO_BOMB_LEVEL - (uint32_t)sl->armor : 1u;
    ho_push(w, S, SA1_HOSTOBJ_EVENT_HIT, sl, SA1_ATTACK_OTHER, HO_BOMB_LEVEL, damage, 0u, 0u, &sn);
    /* ho_hit's cooldown: defence 3, out of the target lists, "killed" for E-102's lock search */
    sl->cooldown = sl->cooldown_steps;
    ho_defence(w, sl, cwp, 1);
    ho_wr16(w, cwp + 0x02u, ho_rd16(w, cwp + 0x02u) & ~0x800u);
    ho_wr16(w, ewp + 0x4Eu, ho_rd16(w, ewp + 0x4Eu) | 0x1000u);
}

/* step 3: the pose of this step: sub step 0 the pair's midpoint, sub step 1 its end (as moving collision objects); a
 * rebuilt template; the boxes' yaw */
static void ho_pose(sa1g_world *w, ho_state *S, ho_slot *sl, uint32_t k, uint32_t twp, uint32_t cwp)
{
    uint32_t sub = ho_rd32(w, SA1G_ADDR_SUB_STEP), info, i;
    int32_t yaw;
    if (sub == 0) {
        for (i = 0; i < 3; i++) {
            ho_wrf(w, twp + 0x20u + 4u * i, (sl->prev[i] + sl->next[i]) * 0.5f);
        }
        yaw = sl->prev_yaw + (int32_t)(int16_t)(uint16_t)((uint32_t)(sl->next_yaw - sl->prev_yaw) & 0xFFFFu) / 2;
    } else {
        for (i = 0; i < 3; i++) {
            ho_wrf(w, twp + 0x20u + 4u * i, sl->next[i]);
        }
        yaw = sl->next_yaw;
    }
    yaw &= 0xFFFF;
    ho_wr32(w, twp + 0x18u, (uint32_t)yaw);
    info = ho_rd32(w, cwp + 0x0Cu);
    if (ho_arena(info)) {
        if (sl->dirty) {
            const uint8_t *t = S->gblock[k] + 0x10u;
            for (i = 0; i < sl->nshapes; i++) {
                uint8_t *ci = ho_at(w, info + HO_INFO * i, HO_INFO);
                if (ci != NULL) {
                    memcpy(ci, t + HO_INFO * i, 0x2Cu);     /* geometry and bits; hit_info / hit_cwp kept */
                }
            }
            ho_defence(w, sl, cwp, sl->cooldown != 0);
            ((void (*)(void *))ho_fn(HO_FN_CCL_RANGE))(sa1_decode(cwp));
            sl->dirty = 0;
        }
        for (i = 0; i < sl->nshapes; i++) {
            if (sl->is_box[i]) {
                /* a box turns with the object (its own yaw + the object's; 0x2000 keeps the game from adding ang.y) */
                ho_wr32(w, info + HO_INFO * i + 0x24u, (uint32_t)((sl->box_yaw[i] + yaw) & 0xFFFF));
            }
        }
    }
}

/* the cooldown's countdown (step 1) */
static void ho_cooldown(sa1g_world *w, ho_slot *sl, uint32_t cwp, uint32_t ewp)
{
    if (sl->cooldown != 0 && --sl->cooldown == 0) {
        ho_defence(w, sl, cwp, 0);
        if (ewp != 0 && !(sl->flags & SA1_HOSTOBJ_NO_LOCKON)) {
            ho_wr16(w, ewp + 0x4Eu, ho_rd16(w, ewp + 0x4Eu) & ~0x1000u);
        }
    }
}

/* the release reason from the holder's requested action (the throw / put-down tables of 6.1.1, by character) */
static uint32_t ho_release_reason(uint32_t chr, uint32_t act)
{
    static const struct {
        uint8_t chr, aim1, aim2, fix1, fix2, put1, put2;
    } t[] = {
        { 0, 59, 61, 60, 62, 63, 63 },      /* Sonic 8C0B199A */
        { 2, 84, 86, 85, 87, 88, 88 },      /* Tails 8C0BD098 */
        { 3, 96, 98, 97, 99, 100, 100 },    /* Knuckles 8C0C6A7A */
        { 6, 35, 37, 36, 38, 39, 39 },      /* E-102 8C0D11DC */
        { 5, 67, 69, 68, 70, 71, 71 },      /* Amy 8C0DC3C0 */
        { 7, 41, 43, 42, 44, 80, 45 },      /* Big 8C0E5166 (80 / 45: let go) */
    };
    uint32_t i;
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        if (t[i].chr != chr) {
            continue;
        }
        if (act == t[i].aim1 || act == t[i].aim2) {
            return SA1_RELEASE_THROW_AIMED;
        }
        if (act == t[i].fix1 || act == t[i].fix2) {
            return SA1_RELEASE_THROW;
        }
        if (act == t[i].put1 || act == t[i].put2) {
            return SA1_RELEASE_PUT_DOWN;
        }
    }
    return SA1_RELEASE_DROP;
}

/* the host object of the nearest entry of player pno's target list (as the aimed throws choose it: the smallest dist,
 * the first of equals; 0: empty or not a proxy) */
static uint32_t ho_aim_target(const sa1g_world *w, const ho_state *S, uint32_t pno)
{
    uint32_t list = pno == 0 ? HO_ADDR_TARGETS0 : HO_ADDR_TARGETS1, best = 0, n, t;
    float dmin = 0.0f;
    for (n = 0; n < 1160u; n++) {
        float d;
        t = ho_rd32(w, list + 8u * n);
        if (t == 0) {
            break;
        }
        d = ho_rdf(w, list + 8u * n + 4u);
        if (n == 0 || dmin > d) {
            dmin = d;
            best = t;
        }
    }
    if (best != 0 && ho_arena(best)) {
        uint32_t cwp = ho_rd32(w, best + 0x38u);
        return ho_arena(cwp) ? ho_object_of_cwp(w, S, cwp) : 0u;
    }
    return 0;
}

/* the holder of the proxy gtp (pno, -1: nobody): a player whose playerwk.htp is it */
static int32_t ho_holder(const sa1g_world *w, uint32_t gtp)
{
    uint32_t pno;
    for (pno = 0; pno < 8u; pno++) {
        uint32_t pwp = ho_ppwp(w, pno);
        if (ho_arena(pwp) && ho_rd32(w, pwp + 0x68u) == gtp) {
            return (int32_t)pno;
        }
    }
    return -1;
}

/* HOLDABLE (6.1.2) */
static void ho_exec_hold(sa1g_world *w, ho_state *S, ho_slot *sl, uint32_t k, uint32_t gtp, uint32_t twp, uint32_t cwp)
{
    uint32_t mwp = ho_rd32(w, gtp + 0x24u), tflag = ho_rd16(w, twp + 0x04u), cf, hc;
    int32_t holder = ho_holder(w, gtp);
    int touch_now;
    if (holder >= 0 && (tflag & 0x1000u)) {
        if (sl->hstate != HO_HELD) {
            sl->hstate = HO_HELD;
            sl->holder = (uint32_t)holder;
            sl->keep_pose = 0;
            ho_wr16(w, twp + 0x04u, tflag & ~0x3Cu);    /* this pass's bits (CCL_Entry no longer runs) */
            ho_push(w, S, SA1_HOSTOBJ_EVENT_LIFTED, sl, 0, 0, 0, 0, 0, NULL);
        }
        return;                         /* as the game's movable step 8C05872A: nothing while held */
    }
    if (sl->hstate == HO_HELD) {
        /* the holder threw it, put it down or dropped it in its exec of this step (its 0x1000 is clear) */
        uint32_t ptwp = ho_ptwp(w, sl->holder), ppwp = ho_ppwp(w, sl->holder), chr, act, j;
        float v[3] = { 0.0f, 0.0f, 0.0f };
        ho_ev *e;
        chr = ho_arena(ptwp) ? ho_rd8(w, ptwp + 0x09u) : 0xFFu;
        act = ho_arena(ppwp) ? ho_rd16(w, ppwp + 0x124u) : 0u;     /* playerwk.mj.reqaction */
        if (ho_arena(mwp)) {
            ho_rd3(w, mwp + 0x04u, v);
        }
        e = ho_ev_new(w, S, SA1_HOSTOBJ_EVENT_RELEASED, sl);
        if (e != NULL) {
            ho_rd3(w, twp + 0x20u, e->pos);
            ho_ev_vel(e, v);
            e->reason = ho_release_reason(chr, act);
            if (e->reason == SA1_RELEASE_THROW_AIMED) {
                e->target = ho_aim_target(w, S, sl->holder);
            }
        }
        if (ho_arena(mwp)) {
            for (j = 0; j < 6u; j++) {
                ho_wr32(w, mwp + 0x04u + 4u * j, 0);    /* spd, acc: the host owns the flight */
            }
        }
        sl->hstate = HO_FREE;
        ho_rd3(w, twp + 0x20u, sl->next);
        memcpy(sl->prev, sl->next, sizeof sl->prev);
        sl->keep_pose = 1;
        sl->rel_pv = 0xFFFFFFFFu;
        tflag = ho_rd16(w, twp + 0x04u) & ~0x3Cu;
    }
    /* the game's own "already held": 8C06C880 skips it */
    tflag = (sl->flags & SA1_HOSTOBJ_HOLD_LOCKED) ? (tflag | 0x1000u) : (tflag & ~0x1000u);
    ho_wr16(w, twp + 0x04u, tflag);
    ho_cooldown(w, sl, cwp, 0);
    cf = ho_rd16(w, cwp + 0x02u);
    hc = ho_rd32(w, cwp + 0x18u);
    touch_now = (cf & 2u) && ho_arena(hc) && ho_rd16(w, hc) == 0u;
    if ((tflag & 4u) && (sl->flags & SA1_HOSTOBJ_HOLD_HITS) && sl->cooldown == 0) {
        const ho_snap *sn = &sl->snap;
        uint32_t level = sn->valid ? sn->alevel : 0u;
        ho_push(w, S, SA1_HOSTOBJ_EVENT_HIT, sl, sn->valid ? ho_classify(sn) : SA1_ATTACK_OTHER, level,
                level > (uint32_t)sl->armor ? level - (uint32_t)sl->armor : 1u,
                sn->valid && sn->myshape >= 0 ? (uint32_t)sn->myshape : 0u,
                sn->valid && sn->aid == 1u ? SA1_HITF_PROJECTILE : 0u, sn);
        sl->cooldown = sl->cooldown_steps;
        ho_defence(w, sl, cwp, 1);
    } else if ((sl->flags & SA1_HOSTOBJ_REPORT_TOUCH) && touch_now && !sl->touching) {
        int32_t my = (int16_t)ho_rd16(w, cwp + 0x14u);
        ho_push(w, S, SA1_HOSTOBJ_EVENT_TOUCH, sl, 0, 0, 0, my >= 0 ? (uint32_t)my : 0u, 0, NULL);
    }
    sl->touching = (uint32_t)touch_now;
    ho_pose(w, S, sl, k, twp, cwp);
    ((void (*)(void *))ho_fn(HO_FN_CCL_ENTRY))(sa1_decode(twp));
}

static void ho_exec(void *tpn)
{
    sa1g_world *w = sa1g_world_current();
    ho_state *S = ho_state_of(w);
    uint32_t gtp = sa1_encode(tpn), twp, cwp, ewp, k, tflag, cf, hc, m;
    ho_slot *sl;
    int touch_now;
    if (w == NULL) {
        return;
    }
    twp = ho_rd32(w, gtp + 0x20u);
    k = ho_rd16(w, twp + 0x08u);
    if (S == NULL || !ho_arena(twp) || ho_rd16(w, twp + 0x0Au) != HO_TAG || k >= HO_SLOTS || S->slot[k].id == 0
        || S->slot[k].tp != gtp) {
        /* an orphan (a proxy of a state that is gone): the game's own end */
        ((void (*)(void *))ho_fn(HO_FN_FREETASK))(tpn);
        return;
    }
    sl = &S->slot[k];
    cwp = ho_rd32(w, twp + 0x38u);
    if (!ho_arena(cwp)) {
        return;
    }
    if (sl->kind == SA1_HOSTOBJ_HOLDABLE) {
        ho_exec_hold(w, S, sl, k, gtp, twp, cwp);
        return;
    }
    ewp = ho_rd32(w, gtp + 0x24u);
    if (!ho_arena(ewp)) {
        return;
    }
    /* 1. the last pass (CCL_Entry has not reset it yet) */
    tflag = ho_rd16(w, twp + 0x04u);
    ho_cooldown(w, sl, cwp, ewp);
    cf = ho_rd16(w, cwp + 0x02u);
    hc = ho_rd32(w, cwp + 0x18u);
    touch_now = (cf & 2u) && ho_arena(hc) && ho_rd16(w, hc) == 0u;
    if ((tflag & 4u) && sl->cooldown == 0) {
        ho_hit(w, S, sl, twp, cwp, ewp);
    } else if (sl->kind == SA1_HOSTOBJ_BALLOON && (sl->flags & SA1_HOSTOBJ_POP_ON_TOUCH) && touch_now && !sl->touching
               && sl->cooldown == 0) {
        ho_hit(w, S, sl, twp, cwp, ewp);
    } else if ((sl->flags & SA1_HOSTOBJ_REPORT_TOUCH) && touch_now && !sl->touching) {
        int32_t my = (int16_t)ho_rd16(w, cwp + 0x14u);
        ho_push(w, S, SA1_HOSTOBJ_EVENT_TOUCH, sl, 0, 0, 0, my >= 0 ? (uint32_t)my : 0u, 0, NULL);
    }
    sl->touching = (uint32_t)touch_now;
    ho_bomb(w, S, sl, k, twp, cwp, ewp);    /* round 0.9.2: the bomb item's sphere */
    /* 2. E-102: the lock-on marker (8C0D38C6 stores it in the enemy work +0x40) and its shot (marker awp +0x14) */
    m = ho_rd32(w, ewp + 0x40u);
    if (m != sl->marker) {
        if (sl->marker != 0) {
            ho_push(w, S, SA1_HOSTOBJ_EVENT_UNLOCKED, sl, 0, 0, 0, 0, 0, NULL);
        }
        if (m != 0) {
            ho_push(w, S, SA1_HOSTOBJ_EVENT_LOCKED, sl, 0, 0, 0, 0, 0, NULL);
            sl->group_valid = 0;
        }
        sl->marker = m;
        sl->shot_seen = 0;
    }
    if (m != 0 && !sl->shot_seen) {
        uint32_t awp = ho_arena(m) ? ho_rd32(w, m + 0x2Cu) : 0u;
        if (ho_arena(awp) && ho_rd32(w, awp + 0x14u) != 0) {
            ho_push(w, S, SA1_HOSTOBJ_EVENT_TARGETED, sl, 0, 0, 0, 0, 0, NULL);
            sl->shot_seen = 1;
        }
    }
    /* 3. the pose, the target flag, NO_LOCKON */
    ho_pose(w, S, sl, k, twp, cwp);
    cf = ho_rd16(w, cwp + 0x02u);
    if (sl->ccl_id == 3u && !(sl->flags & (SA1_HOSTOBJ_NO_HOMING | SA1_HOSTOBJ_DISABLED)) && sl->cooldown == 0) {
        cf |= 0x800u;
    } else {
        cf &= ~0x800u;
    }
    ho_wr16(w, cwp + 0x02u, cf);
    if (sl->flags & SA1_HOSTOBJ_NO_LOCKON) {
        ho_wr16(w, ewp + 0x4Eu, ho_rd16(w, ewp + 0x4Eu) | 0x1000u);
    }
    /* 4. this step's lists */
    ((void (*)(void *))ho_fn(HO_FN_CCL_ENTRY))(sa1_decode(twp));
}

/* ---- post (after the CCL pass): the hit snapshot, player 0's hurt, the held state ----------------------------------- */
/* the game's rotation of ang.y (BAMS) applied to v: (x cos a - z sin a, y, x sin a + z cos a) (the facing of ang.y = a
 * is (cos a, 0, sin a)) */
static void ho_turn_y(int32_t a, const float v[3], float out[3])
{
    float s, c;
    sa1g_fsca(a, &s, &c);
    out[0] = v[0] * c - v[2] * s;
    out[1] = v[1];
    out[2] = v[0] * s + v[2] * c;
}

static void ho_snapshot(const sa1g_world *w, ho_slot *sl)
{
    ho_snap *sn = &sl->snap;
    uint32_t cwp = ho_rd32(w, sl->twp + 0x38u), hc, info, nb, atwp, k;
    memset(sn, 0, sizeof *sn);
    sn->apno = -1;
    if (!ho_arena(cwp) || !(ho_rd16(w, cwp + 0x02u) & 2u)) {
        return;
    }
    hc = ho_rd32(w, cwp + 0x18u);
    if (!ho_arena(hc)) {
        return;
    }
    sn->valid = 1;
    sn->acwp = hc;
    sn->aid = ho_rd16(w, hc);
    sn->ashape = (int16_t)ho_rd16(w, cwp + 0x16u);
    sn->myshape = (int16_t)ho_rd16(w, cwp + 0x14u);
    sn->atp = ho_rd32(w, hc + 0x10u);
    sn->aexec = ho_arena(sn->atp) ? ho_rd32(w, sn->atp + 0x10u) : 0u;
    atwp = ho_arena(sn->atp) ? ho_rd32(w, sn->atp + 0x20u) : 0u;
    info = ho_rd32(w, hc + 0x0Cu);
    nb = ho_rd16(w, hc + 0x04u);
    if (ho_arena(atwp)) {
        sn->amode = ho_rd8(w, atwp);
        sn->aflag = ho_rd16(w, atwp + 0x04u);
        sn->achr = ho_rd8(w, atwp + 0x09u);
        for (k = 0; k < 8u; k++) {
            if (ho_ptwp(w, k) == atwp) {
                sn->apno = (int32_t)k;
                break;
            }
        }
        sn->ap0 = sn->apno == 0;
        ho_rd3(w, atwp + 0x20u, sn->apos);
    }
    if (ho_arena(info) && sn->ashape >= 0 && (uint32_t)sn->ashape < nb) {
        uint32_t ci = info + HO_INFO * (uint32_t)sn->ashape;
        float c[3], r[3];
        sn->aattr = ho_rd32(w, ci);
        sn->alevel = ho_rd8(w, ci + 0x04u) & 3u;
        ho_rd3(w, ci + 0x08u, c);
        if (sn->aattr & 0x20u) {
            memcpy(sn->apos, c, sizeof sn->apos);               /* absolute (the players' shape 0) */
        } else if (ho_arena(atwp)) {
            if (sn->aattr & 0x2000u) {
                memcpy(r, c, sizeof r);                         /* an unrotated offset */
            } else {
                ho_turn_y((int32_t)ho_rd32(w, atwp + 0x18u), c, r);   /* the owner's yaw (x / z turns left out) */
            }
            for (k = 0; k < 3; k++) {
                sn->apos[k] += r[k];
            }
        }
    }
    if (sn->apno >= 0) {
        uint32_t pwp = ho_ppwp(w, (uint32_t)sn->apno), mwp = ho_pmwp(w, (uint32_t)sn->apno);
        if (ho_arena(pwp)) {
            sn->aequip = ho_rd16(w, pwp + 0x04u);
            sn->aitem = ho_rd16(w, pwp + 0x06u);
        }
        if (ho_arena(mwp)) {
            ho_rd3(w, mwp + 0x04u, sn->aspd);
        }
    }
}

/* the lift mode of a game character id (its lift handler's mode for a class-0x200 object; 6.1.1) */
static uint32_t ho_lift_mode(uint32_t chr)
{
    switch (chr) {
    case 0:
        return 21;      /* Sonic 8C0B16BC (action 33) */
    case 2:
        return 21;      /* Tails 8C0BCDD0 (58) */
    case 3:
        return 27;      /* Knuckles 8C0C67BC (70) */
    case 5:
        return 23;      /* Amy 8C0DBC16 (43) */
    case 6:
        return 15;      /* E-102 8C0D0F0C (21) */
    case 7:
        return 15;      /* Big 8C0E4E60 (78 / 26) */
    default:
        return 0xFFFFFFFFu;
    }
}

/* the held state of player 0 after the step (6.1.2: position, previous position, facing, hand anchor, phase); round 0.9.3a
 * (sadc docs/research/super_general_design.md 4.12): after sub step 0, the step whose display the character renderer draws,
 * also the drawn position and yaw of the last two pairs (display only: reads of guest memory) */
static void ho_held_update(sa1g_world *w, const ho_state *S, int32_t sub)
{
    sa1g_ho_char *c = &w->ho;
    uint32_t twp = ho_ptwp(w, 0), pwp = ho_ppwp(w, 0), obj = 0, phase = SA1_HOLD_NONE, chr, mode;
    int32_t k = -1;
    if (S != NULL && ho_arena(twp) && ho_arena(pwp)) {
        uint32_t htp = ho_rd32(w, pwp + 0x68u);
        chr = ho_rd8(w, twp + 0x09u);
        mode = ho_rd8(w, twp);
        k = ho_slot_of_task(w, S, htp);
        if (k >= 0 && S->slot[k].kind == SA1_HOSTOBJ_HOLDABLE && (ho_rd16(w, S->slot[k].twp + 0x04u) & 0x1000u)) {
            uint32_t r = ho_release_reason(chr, ho_rd16(w, pwp + 0x124u));
            phase = r == SA1_RELEASE_DROP ? SA1_HOLD_CARRYING : SA1_HOLD_THROWING;
        } else {
            k = -1;
            if (mode == ho_lift_mode(chr) && !(ho_rd16(w, twp + 0x04u) & 0x800u)) {
                uint32_t cwp = ho_rd32(w, twp + 0x38u), hc = ho_arena(cwp) ? ho_rd32(w, cwp + 0x18u) : 0u;
                k = ho_arena(hc) ? ho_slot_of_task(w, S, ho_rd32(w, hc + 0x10u)) : -1;
                if (k >= 0 && S->slot[k].kind == SA1_HOSTOBJ_HOLDABLE) {
                    phase = SA1_HOLD_LIFTING;
                } else {
                    k = -1;
                }
            }
        }
    }
    if (k < 0) {
        c->held_object = 0;
        c->held_phase = SA1_HOLD_NONE;
        c->held_drawn_obj = 0;
        return;
    }
    obj = S->slot[k].id;
    {
        const ho_slot *sl = &S->slot[k];
        uint32_t ang[3], cwp = ho_rd32(w, twp + 0x38u), info = ho_arena(cwp) ? ho_rd32(w, cwp + 0x0Cu) : 0u, j;
        uint32_t act = ho_rd16(w, pwp + 0x124u);
        float pos[3], centre[3] = { 0.0f, 0.0f, 0.0f }, hm[3], rh[3], lh[3], rv[3], out[3];
        ho_rd3(w, sl->twp + 0x20u, pos);
        if (c->held_object != obj) {
            memcpy(c->held_prev, pos, sizeof c->held_prev);
        } else {
            memcpy(c->held_prev, c->held_pos, sizeof c->held_prev);
        }
        memcpy(c->held_pos, pos, sizeof c->held_pos);
        for (j = 0; j < 3; j++) {
            ang[j] = ho_rd32(w, twp + 0x14u + 4u * j);
        }
        c->held_ang = ang[1] & 0xFFFFu;
        if (sub == 0) {
            if (c->held_drawn_obj == obj) {
                memcpy(c->held_drawn_prev, c->held_drawn, sizeof c->held_drawn_prev);
                c->held_drawn_prev_ang = c->held_drawn_ang;
            } else {
                memcpy(c->held_drawn_prev, pos, sizeof c->held_drawn_prev);
                c->held_drawn_prev_ang = c->held_ang;
            }
            memcpy(c->held_drawn, pos, sizeof c->held_drawn);
            c->held_drawn_ang = c->held_ang;
            c->held_drawn_obj = obj;
        }
        /* the hand anchor of 8C0B1DE0: the character's shape 0 centre (absolute) + R * (the hands' midpoint, or the right
         * hand + 2.5 x its vector in Sonic's throw actions 59-62); the other characters' hold updates use the same form
         * (UNVERIFIED for every throw action) */
        if (ho_arena(info)) {
            ho_rd3(w, info + 0x08u, centre);
        }
        ho_rd3(w, pwp + 0x14Cu, rh);
        ho_rd3(w, pwp + 0x158u, lh);
        ho_rd3(w, pwp + 0x194u, rv);
        if (chr == 0 && act >= 59u && act <= 62u) {
            for (j = 0; j < 3; j++) {
                hm[j] = rv[j] * 2.5f + rh[j];
            }
        } else {
            for (j = 0; j < 3; j++) {
                hm[j] = (rh[j] + lh[j]) * 0.5f;
            }
        }
        ho_rot_hold(ang, hm, out);
        for (j = 0; j < 3; j++) {
            c->held_hand[j] = centre[j] + out[j];
        }
    }
    c->held_object = obj;
    c->held_phase = phase;
}

/* round 0.9.1 (E1; sadc docs/research/gsa1_070_drown.md D3 / E1 with its Review R3.6 / R4.2): the deaths the death request
 * (playerwk.item 0x4000, above) does not cover, once each, in the step the game starts them:
 *   drowning   8C07BE6E's SetInputP(0, 39) at countdown frame 0x30C: taskwk.smode became 39 (the drown action follows; the
 *              game's own restart request comes 240 steps later) -> SA1_DEATH_DROWN;
 *   death zone the death-zone task (8C07BAC0, whoever created it) got its death child (8C07BD48: the 121-step timer
 *              8C07BA44, the scream, the death camera) -> SA1_DEATH_ZONE;
 *   fallback   the game's restart request 8C04E386(2) (its boundary stub pushed SA1_EVENT_RESTART id 2 in this step: the
 *              executor's tasks call it) when the world has had no DEATH and no such RESTART before -> SA1_DEATH_OTHER,
 *              after the RESTART (a latch, not reset by the RESTART as the Review R4.2 first put it: E-102's time-up
 *              requests the restart twice, 8C07BBC2 at mode 49 and again at mode 51, which would count a second death).
 * The harness's oracle runs the original 8C04E386 (no RESTART event there), so the fallback never fires in it. */
static void ho_death_rules(sa1g_world *w, sa1g_ho_char *c, uint32_t twp, int death)
{
    float pos[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t cause = 0xFFFFFFFFu, i, n;
    if (ho_arena(twp)) {
        ho_rd3(w, twp + 0x20u, pos);
    }
    if (death) {
        c->death_seen = 1;
    } else if (ho_arena(twp)) {
        if ((int32_t)(int8_t)ho_rd8(w, twp + 0x01u) == HO_SMODE_DROWN && c->smode_before != HO_SMODE_DROWN) {
            cause = SA1_DEATH_DROWN;
        } else if (c->dz_task != 0 && c->dz_child_before == 0 && ho_rd32(w, c->dz_task + 0x0Cu) != 0) {
            cause = SA1_DEATH_ZONE;
        }
        if (cause != 0xFFFFFFFFu) {
            sa1g_world_event_push(w, SA1_EVENT_DEATH, 0u, 0u, pos, cause);
            c->death_seen = 1;
        }
    }
    n = w->events.count;
    for (i = c->ev_pre; i < n && i < SA1G_EVENT_QUEUE; i++) {
        const sa1_event *e = &w->events.ev[(w->events.head + i) % SA1G_EVENT_QUEUE];
        if (e->type != SA1_EVENT_RESTART || e->id != HO_RESTART_DEATH) {
            continue;
        }
        if (!c->death_seen) {
            sa1g_world_event_push(w, SA1_EVENT_DEATH, 0u, 0u, pos, SA1_DEATH_OTHER);
        }
        c->death_seen = 1;      /* a latch: a second request of the same death (E-102's time-up asks twice, 8C07BBC2 at
                                   mode 49 and again at 51) is no new death */
    }
}

static void ho_post(sa1g_world *w, int32_t sub, void *user)
{
    ho_state *S = ho_state_of(w);
    sa1g_ho_char *c = &w->ho;
    uint32_t twp = ho_rd32(w, SA1G_ADDR_PLAYER_TWP), pwp = ho_ppwp(w, 0), wt, k, item;
    int hurt = 0, death = 0;
    (void)sub;
    (void)user;
    /* player 0's hurt: the damage checks set wtimer to exactly 120 (design 2.5) */
    wt = ho_arena(twp) ? ho_rd16(w, twp + 0x06u) : 0u;
    if (wt == 120u && w->ho_wtimer != 120u) {
        w->ho_hurt_steps = 0;
        hurt = ho_arena(twp);
    } else if (w->ho_hurt_steps != 0xFFFFFFFFu) {
        w->ho_hurt_steps++;
    }
    item = ho_arena(pwp) ? ho_rd16(w, pwp + 0x06u) : 0u;
    death = ho_arena(pwp) && (item & 0x4000u) && !(c->item_before & 0x4000u);
    if (hurt) {
        int32_t rings = (int16_t)ho_rd16(w, SA1G_ADDR_RINGS);
        uint32_t flags = c->src_flags, lost = c->rings_before > rings ? (uint32_t)(c->rings_before - rings) : 0u;
        if (c->phantom && !(item & 1u)) {
            flags |= SA1_HURT_ABSORBED;
        } else if ((c->item_before & 3u) && !(item & 3u)) {
            flags |= SA1_HURT_BARRIER;
        }
        if (death) {
            flags |= SA1_HURT_FATAL;
        }
        ho_hurt_push(w, c->src_object, lost, flags);
    }
    if (death) {
        float pos[3];
        uint32_t cause = c->death_cause != SA1_DEATH_OTHER ? c->death_cause : hurt ? SA1_DEATH_HURT : SA1_DEATH_OTHER;
        ho_rd3(w, twp + 0x20u, pos);
        if (cause == SA1_DEATH_OTHER && ho_rd8(w, twp + 0x09u) == 6u && ho_rd8(w, twp) == 49u) {
            /* round 0.9.1: E-102's time-up death (8C0D23E4: mode 49, then the death request); the DEATH keeps cause OTHER
               (the oracle's references recorded it so: hud_gamma_fe1 step 11160) */
            sa1g_world_event_push(w, SA1_EVENT_TIME_UP, 0u, 0u, pos, 0u);
        }
        sa1g_world_event_push(w, SA1_EVENT_DEATH, c->death_cause == SA1_DEATH_HOST ? c->death_object
                                                                                   : hurt ? c->src_object : 0u,
                              0u, pos, cause);
    }
    ho_death_rules(w, c, twp, death);
    if (c->phantom && ho_arena(pwp) && (ho_rd16(w, pwp + 0x06u) & 1u)) {
        ho_wr16(w, pwp + 0x06u, ho_rd16(w, pwp + 0x06u) & ~1u);    /* not consumed (a mode without the damage check) */
    }
    c->phantom = 0;
    c->item_before = item;
    if (S != NULL && S->live != 0) {
        for (k = 0; k < HO_SLOTS; k++) {
            ho_slot *sl = &S->slot[k];
            if (sl->id != 0 && ho_proxy_ok(w, sl, k) && sl->hstate != HO_HELD) {
                ho_snapshot(w, sl);
            }
        }
    }
    if (S != NULL || c->held_object != 0) {
        ho_held_update(w, S, sub);
    }
}

/* ---- the LIBSA1_HOST code image ----------------------------------------------------------------------------------- */
static const sa1g_fnent ho_fns[1] = {
    { HO_KEY, HO_CODE, HO_SIG_VP, (void (*)(void))ho_exec, "hostobj_exec" },
};

static const sa1g_fnent *ho_lookup(uint32_t g)
{
    return (g & 0x1FFFFFFFu) == HO_KEY ? &ho_fns[0] : NULL;
}

const sa1g_fnimage sa1g_hostobj_image = { "LIBSA1_HOST", "", "", 0x1F000000u, 0x1F010000u, ho_lookup, ho_fns, 1u };

/* ---- the API side ------------------------------------------------------------------------------------------------- */
void sa1g_hostobj_reset_run(struct sa1g_world *w)
{
    uint32_t opts, mask;
    if (w == NULL) {
        return;
    }
    opts = w->ho.opts;
    mask = w->ho.kind_mask;
    memset(&w->ho, 0, sizeof w->ho);
    w->ho.opts = opts;
    w->ho.kind_mask = mask;
}

sa1_status sa1ho_world_attach(sa1_context *ctx, struct sa1g_world *w, int32_t start_rings)
{
    const sa1ho_registry *R;
    uint32_t i;
    if (ctx == NULL || w == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    w->hostobj_ctx = ctx;
    w->hostobj_pre = ho_pre;
    w->hostobj_post = ho_post;
    /* the settings the host gave the character before its world was built (sa1_character_set_option, world_mask) */
    R = sa1ho_registry_of(ctx);
    for (i = 0; R != NULL && i < SA1_MAX_INSTANCES; i++) {
        const sa1ho_charset *cs = &R->chars[i];
        if (cs->id > 0 && cs->set != 0 && sa1_character_world(ctx, cs->id) == w) {
            if (cs->set & SA1HO_SET_OPTS) {
                w->ho.opts = cs->opts;
            }
            if (cs->set & SA1HO_SET_MASK) {
                w->ho.kind_mask = cs->mask;
            }
        }
    }
    if (start_rings >= 0) {
        sa1_status st = sa1g_world_start_rings(w, start_rings);
        if (st != SA1_OK) {
            return st;
        }
    }
    /* round 0.9.1: the character's lives, companions and game options (character worlds of the bridge only) */
    return w->charopt_attach != NULL ? w->charopt_attach(ctx, w) : SA1_OK;
}

void sa1ho_world_tail(const struct sa1g_world *w, sa1ho_tail *out)
{
    uint32_t twp;
    memset(out, 0, sizeof *out);
    if (w == NULL || w->ram == NULL) {
        return;
    }
    out->rings = (int16_t)ho_rd16(w, SA1G_ADDR_RINGS);
    out->lives = (int8_t)ho_rd8(w, HO_ADDR_LIVES);
    out->score = ho_rd32(w, HO_ADDR_SCORE);
    twp = ho_rd32(w, SA1G_ADDR_PLAYER_TWP);
    out->invulnerable_steps = ho_arena(twp) ? ho_rd16(w, twp + 0x06u) : 0u;
    out->hurt_steps = w->ho_hurt_steps;
    out->held_object = w->ho.held_phase == SA1_HOLD_CARRYING || w->ho.held_phase == SA1_HOLD_THROWING
                       ? w->ho.held_object : 0u;
}

static void ho_rings_cb(sa1g_world *w, void *user)
{
    int32_t n = *(const int32_t *)user, r = (int16_t)ho_rd16(w, SA1G_ADDR_RINGS), t = r + n;
    if (t < 0) {
        t = 0;
    }
    if (t > 999) {
        t = 999;
    }
    n = t - r;
    if (n != 0) {
        ((void (*)(int16_t))ho_fn(HO_FN_RINGS_ADD))((int16_t)n);
    }
}

sa1_status sa1ho_world_add_rings(struct sa1g_world *w, int32_t n)
{
    if (w == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (n > 1000) {
        n = 1000;
    }
    if (n < -1000) {
        n = -1000;
    }
    return sa1g_world_call(w, ho_rings_cb, &n);
}

sa1_status sa1ho_world_damage(struct sa1g_world *w, const sa1_damage *d)
{
    sa1g_ho_damage *q;
    if (w == NULL || d == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->ho.ndmg >= SA1G_HO_DAMAGE_MAX) {
        return SA1_ERR_LIMIT;
    }
    q = &w->ho.dmg[w->ho.ndmg++];
    q->flags = d->flags;
    q->object = d->object;
    memcpy(q->source, d->source, sizeof q->source);
    return SA1_OK;
}

void sa1ho_world_settings(struct sa1g_world *w, uint32_t set, uint32_t opts, uint32_t mask)
{
    if (w == NULL) {
        return;
    }
    if (set & SA1HO_SET_OPTS) {
        w->ho.opts = opts;
    }
    if (set & SA1HO_SET_MASK) {
        w->ho.kind_mask = mask;     /* applied at the world's next pair start (ho_window) */
    }
}

void sa1ho_world_held(const sa1_context *ctx, const struct sa1g_world *w, sa1_hostobj_held *out)
{
    memset(out, 0, sizeof *out);
    out->struct_size = sizeof *out;
    if (w == NULL || w->ho.held_object == 0) {
        return;
    }
    out->object = w->ho.held_object;
    out->phase = w->ho.held_phase;
    sa1_to_host_point(ctx, w->ho.held_pos, out->position);
    sa1_to_host_point(ctx, w->ho.held_prev, out->position_prev);
    sa1_to_host_point(ctx, w->ho.held_hand, out->hand);
    out->yaw = (float)(-(double)(int16_t)(uint16_t)(w->ho.held_ang & 0xFFFFu) * (6.28318530717958647692 / 65536.0));
    /* round 0.9.3a: the drawn steps (before the first drawn step of a hold: the held position) */
    if (w->ho.held_drawn_obj == w->ho.held_object) {
        sa1_to_host_point(ctx, w->ho.held_drawn, out->drawn);
        sa1_to_host_point(ctx, w->ho.held_drawn_prev, out->drawn_prev);
        out->drawn_yaw = (float)(-(double)(int16_t)(uint16_t)(w->ho.held_drawn_ang & 0xFFFFu) *
                                 (6.28318530717958647692 / 65536.0));
        out->drawn_yaw_prev = (float)(-(double)(int16_t)(uint16_t)(w->ho.held_drawn_prev_ang & 0xFFFFu) *
                                      (6.28318530717958647692 / 65536.0));
    } else {
        memcpy(out->drawn, out->position, sizeof out->drawn);
        memcpy(out->drawn_prev, out->position, sizeof out->drawn_prev);
        out->drawn_yaw = out->drawn_yaw_prev = out->yaw;
    }
}

uint32_t sa1ho_world_poll(const sa1_context *ctx, struct sa1g_world *w, sa1_hostobj_event *out, uint32_t cap)
{
    ho_state *S = ho_state_of(w);
    uint32_t n = 0;
    if (S == NULL || out == NULL) {
        return 0;
    }
    while (n < cap && S->ev_count > 0) {
        const ho_ev *e = &S->ev[S->ev_head];
        sa1_hostobj_event *o = &out[n++];
        memset(o, 0, sizeof *o);
        o->type = e->type;
        o->object = e->object;
        o->attack = e->attack;
        o->level = e->level;
        o->damage = e->damage;
        o->shape = e->shape;
        o->flags = e->flags;
        o->user = e->user;
        o->step = e->step;
        o->reason = e->reason;
        o->target = e->target;
        sa1_to_host_point(ctx, e->pos, o->position);
        sa1_to_host_dir(ctx, e->dir, o->direction);
        o->speed = e->speed * 60.0f * ctx->coords.scale;
        S->ev_head = (S->ev_head + 1u) % HO_EVQ;
        S->ev_count--;
    }
    return n;
}

void sa1ho_world_info(const struct sa1g_world *w, sa1_hostobj_info *out)
{
    const ho_state *S = ho_state_of(w);
    if (S != NULL) {
        out->proxies = S->live;
        out->created = S->created;
        out->destroyed = S->destroyed;
        out->refused = S->refused;
        out->events_dropped = S->ev_dropped;
    }
    if (w != NULL && w->ram != NULL && w->booted) {
        ho_heap_walk(w, &out->heap_free, &out->heap_largest);
    }
}

uint32_t sa1ho_world_proxy_twp(const struct sa1g_world *w, uint32_t object)
{
    const ho_state *S = ho_state_of(w);
    uint32_t k;
    if (S == NULL || object == 0) {
        return 0;
    }
    for (k = 0; k < HO_SLOTS; k++) {
        if (S->slot[k].id == object) {
            return S->slot[k].twp;
        }
    }
    return 0;
}

void sa1ho_world_heap(const struct sa1g_world *w, uint32_t *total_free, uint32_t *largest)
{
    if (w == NULL || w->ram == NULL) {
        if (total_free != NULL) {
            *total_free = 0;
        }
        if (largest != NULL) {
            *largest = 0;
        }
        return;
    }
    ho_heap_walk(w, total_free, largest);
}
