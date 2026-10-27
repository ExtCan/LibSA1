/*
 * sa1_world_set.c - SET objects inside the worlds (API SA1_API_SET, round 0.9.4; sadc docs/research/gsa1_094_plan.md 6.3
 * task 3 with its Review R.3-R.5, R.8, R.10, R.12, R.16; gsa1_094_items.md 1.3, 1.4, 5.1, 6.1-6.5; gsa1_090_rings.md 4.1
 * with its Review R.2.4-R.2.9, R.3.1-R.3.7; v1_arch.md 5.2 with its Review R.2.1-R.2.3, G2, G3, G5-G8): the game's SET
 * manager in every character world whose context has records, the window that gives the records condition slots, the
 * forced state, the spawn hold of the heap guard, the touch latch and the events. The context side (the registry, the API)
 * is src/sa1_set.c.
 *
 * Attach (review R.12): sa1g_charopt_attach (src/world/sa1_world_charopt.c), which sa1ho_world_attach calls last on every
 * world the bridge creates, calls sa1g_set_attach first: it installs w->set_pre / w->set_post, which sa1g_step_game
 * (sa1_world_tick.c) runs right after hostobj_pre (before the executor) and right after hostobj_post (after the CCL pass).
 * Both return at once while the context never had a record: such a world runs exactly as before (no task, no write).
 *
 * Per world, in segment 0x18 (reset by every rebuild; the state is made again at the next pair start and every record
 * re-enters from the registry): one st_state block, created at the first pair start that finds records. It holds the
 * game-visible part - the generated object list OBJ_ITEMTABLE {0, 4, entries} with its four 0x14-byte OBJ_ITEMENTRYs (im,
 * level, attribute 0, range 0, ptr 0, the exec, the name) and one 0x20-byte SET entry per condition slot (usID = the
 * type's index in that list, the BAMS rotation in the type's order, the SA1 position, scl = params) - and libsa1's
 * bookkeeping: the slot map, the window's scratch, the touch latches, the event queue (256, the oldest dropped).
 *
 * The manager (gsa1_094_items.md 6.1, v1_arch review R.2.2 / G2): CreateElementalTask(2, 8, 8C090FA0) - level 8, which
 * the task system links into list 7 (level 7 is linked into none) - and its mode 0 done here: B_8C79A3CC = the generated
 * list, B_8C79A3D0 = NULL, B_8C79E3D0 / B_8C79E3D1 = 0, D_8C1C0ED0 = 1, D_8C1C0ED4 = 0 (no act change: 8C0915D2 is never
 * reached), D_8C1C0ED8 = 0, the condition array cleared, B_8C79A3DC = 0, the task in mode 1. Its mode 1 is the game's:
 * 8C0915A2 picks 8C091380 (no player 1) and spawns every condition with bit 15 and without bit 0 whose SET entry is within
 * 400 units of player 0 (of the camera taskwk while Big's lure is in the water: sa1_world_tick.c writes the virtual eye).
 *
 * One pair start (pre, sub step 0; the registry is read only here):
 *   window      the records of the world's kind within (range + margin) of player 0, nearest first by (squared distance,
 *               id), within the two budgets (ring tasks: RING 1, a line n + 1, a circle n; boxes 1 each); a refusal is
 *               SA1_EVENT_CAPACITY SA1_CAPACITY_SET_WINDOW once per episode. A selected record without a slot takes the
 *               lowest free one: SET entry and condition written from the registry (present: 0x8000, broken: | 0x0004,
 *               scUserFlag = its bits), B_8C79A3DC = the high-water mark. A slot whose record was not selected (or was
 *               removed) is LEAVING. The registry's state / description versions are latched per slot.
 * Every step (pre, before the executor):
 *   leaving     the task is freed (FreeTask 8C098786: exec = DestroyTask, which runs at its next exec; silent) and bit 15
 *               cleared; the slot is released (the condition and the SET entry zeroed) once no task points at it
 *   update      (sa1_set_update) the task freed, the SET entry and the condition rewritten from the registry once no task
 *               points at the slot: the spawner makes it again at the new place
 *   forced      (sa1_set_state) PRESENT 0: bit 15 cleared, the task freed; PRESENT 1: bit 15 set once no task points at
 *               the slot (or its task still has bit 0: then nothing spawns twice); ITEMBOX BROKEN 1: 0x0004 set and an
 *               intact box (mode < 3) freed; BROKEN 0: 0x0004 cleared, bit 15 from PRESENT, the broken box (mode >= 4)
 *               freed; O_GRING bits the slot lacks: ORed in, the group freed (the spawner re-makes it without those rings;
 *               the world's own bits coming back are no change: review R.10). No event (the post hook compares with the
 *               state after the write: rings review R.3.7)
 *   the touch   (review R.8) none of the three frees while the game is between a touch and its own end of the object:
 *               a RING in modes 2-3 (the sparkle, then 8C091BA4), a group with a child in modes 2-3, an air box in modes
 *               2-4 (8C091BA4 at mode 4), a ground box in mode 3 (8C091B54 sets 0x0004 there). They wait, step by step
 *   heap guard  a slot that could spawn in this step (bit 15, no bit 0, no task) is held (bit 0 set without a task, so
 *               the spawner passes it) while the game heap's largest free block is below 32 KB (CCL_Init never returns
 *               from a failed CAlloc); SA1_EVENT_CAPACITY SA1_CAPACITY_SET_HEAP once per episode; post clears the holds
 * After the CCL pass (post, every step):
 *   tasks       a slot's task = its pTask while that task's ocp is the slot; a new one: SPAWNED; one that ended (its ocp
 *               is not the slot any more, or no exec): DESPAWNED
 *   GONE        the slot's task carries DestroyTask (8C09865C: the mark 8C091BA4 and FreeTask leave) or ended, with bit
 *               15 clear, and libsa1 did not free it: once per task (a level rule, review R.4: E-102's firing clears bit
 *               15 at pair 66, the air box's 8C091BA4 runs at pair 79; 8C091B8A alone raises nothing)
 *   BROKEN      0x0004 went 0 -> 1; USERBITS: scUserFlag changed
 *   the latch   (review R.5) every step the last CCL hit of the slot's object: a RING / group child with colliwk.flag 2:
 *               BODY, or MAGNET when its taskwk.wtimer > 0 (8C08789A pulled it: review R.3); a box with flag 2 and
 *               hit_cwp->id 0: BODY (attacker = the character), id 1: PROJECTILE (attacker = hit_cwp->mytask's
 *               position); with the character's position and the step. When the object's exec takes a hit (a ring 1 ->
 *               2, an air box 1 -> 2, a ground box 2 -> 3, a group child newly in mode 2) the latch of the pass BEFORE
 *               that exec is frozen (that exec read the previous pass; the air box re-makes its colliwk and the ground
 *               box re-enters CCL in it); events take the frozen latch, else the last one, else UNKNOWN.
 *
 * Determinism (as round 5): the registry is read at the pair start, positions converted in the world's FP mode with
 * single-precision arithmetic and no CRT maths (the game's atan2f sa1g_atan2f, sa1g_sqrtf), the order is (squared
 * distance, id), slots are the lowest free ones, no host time, no rand().
 *
 * Round 0.9.4b (sadc docs/research/objects2_plan.md 6.1 with its Review R.3, R.4, R.6, R.12, R.13, R.15; 11.0 O1; 11.1;
 * objects2_range.md): host options, never set by the reference worlds (they have no records), each 0.9.4's exactly at 0:
 *   the range   sa1_set_config.range above 400: the generated list's four entries get attribute bit 0 and fRange = range
 *               squared (st_list_range), which the game's unchanged spawner 8C091380 reads; the window's reach is range +
 *               margin (the catalogue's 400 + margin at 0)
 *   the order   above 400 (order B, objects2_range.md 1.3 G): tier 0 = the records whose place lies within the game's 400
 *               units (nearest first), tier 1 = those in front of the plane through the world's virtual eye (w->camera.pos,
 *               written by sa1g_camera_view at the step's start) perpendicular to the camera's yaw, tier 2 = the rest; a
 *               record keeps tier 1 or 2 for SA1_SET_TIER_HOLD pairs after a change between them (st_tier; tier 0 is
 *               never held: review R.12). At every range a ring line or circle is ordered by its NEAREST RING (11.1: the
 *               records of one big circle share its centre, so ordering them by their place kept the same lowest ids
 *               live): st_gring_near, the game's own layout of objexec_O_GRING. The reach stays the record's place (the
 *               spawner tests the SET entry's position: 8C09111A on entry->pos), so no slot goes to a record the game
 *               cannot make and none is refused that it makes
 *   the sides   sa1_set_config.view_tan: the rt's rt_objview block in the world's pcdata (st_objview) gets kx_scale = the
 *               game's tan / view_tan and the four SET execs; 8C038D00 then widens its two sideways tests while one of
 *               them runs (src/rt/rt_draw.c section 6)
 *   live        sa1w_set_world_live: the records whose task lives after the last pair (sa1_set_live, review R.6)
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "pc/sh4_fpmodel.h"

#include <stddef.h>
#include <string.h>

/* ---- the game's addresses ------------------------------------------------------------------------------------------ */
#define ST_ADDR_ITEMTABLE   0x8C79A3CCu     /* SET_ITEMTABLE *B_8C79A3CC: the object list (a pointer: rings review R.2.9) */
#define ST_ADDR_SETDATA     0x8C79A3D0u     /* void *B_8C79A3D0: the stage's SET file (none) */
#define ST_ADDR_COUNT       0x8C79A3DCu     /* Sint16 B_8C79A3DC: conditions in use */
#define ST_ADDR_COND        0x8C79A3E0u     /* OBJ_CONDITION B_8C79A3E0[1024] (0x10 each) */
#define ST_ADDR_MGR_LVL     0x8C79E3D0u     /* Sint8 B_8C79E3D0 / B_8C79E3D1: the manager's level / act */
#define ST_ADDR_ED0         0x8C1C0ED0u     /* Sint32 D_8C1C0ED0 (mode 0: 1) */
#define ST_ADDR_ACTCHANGE   0x8C1C0ED4u     /* Sint32 D_8C1C0ED4: the act-change request (kept 0) */
#define ST_ADDR_FIRSTPASS   0x8C1C0ED8u     /* Sint32 D_8C1C0ED8: 0 until the first spawner pass */
#define ST_ADDR_HEAP_FREE   0x8C79F03Cu     /* MEMBLK *B_8C79F03C: the heap's free list (code_8C098998.c) */
#define ST_FN_FREETASK      0x8C098786u     /* FreeTask(tp): exec = DestroyTask */
#define ST_EXEC_DESTROY     0x0C09865Cu     /* DestroyTask, & 0x1FFFFFFF */
#define ST_EXEC_RING        0x0C0869E8u     /* objexec_RING, & 0x1FFFFFFF */
#define ST_EXEC_GRING       0x0C087B80u     /* objexec_O_GRING (it runs its rings through 8C098E98: review "holds") */
#define ST_EXEC_GBOX        0x0C05D10Cu     /* objexec_O_ITEMBOX */
#define ST_EXEC_ABOX        0x0C05D9F6u     /* objexec_O_ItemBoxAir */
/* round 0.9.4b: the screen the boot sets (sa1_world_boot.c; rt_draw.c section 6 reads the same words) */
#define ST_ADDR_SCR_DIST    0x8C88F5E4u     /* Float B_8C88F5E4: the screen distance (457.06) */
#define ST_ADDR_SCR_CX      0x8C88F5F0u     /* Float B_8C88F5F0: the screen centre x (320) */
#define ST_ADDR_CLIP_X0     0x8C88F600u     /* Float B_8C88F600[0]: the 2D clip rectangle's x minimum (0) */
#define ST_ADDR_ASPECT_X    0x8C88F618u     /* Float B_8C88F618: aspect x (1) */
#define ST_RANGE_GAME       400.0f          /* the spawner's range for attribute 0 (160,000 = 400^2) */
#define ST_OBJV_MAGIC0      0x564A424Fu     /* "OBJV": rt_draw.c's RT_OBJVIEW (R4 pcdata, one copy per world) */
#define ST_OBJV_MAGIC1      0x30304438u     /* "8D00" */
#define ST_OBJV_BYTES       36u             /* {magic[2], version, kx_scale, n, exec[4]} */
#define ST_OBJV_NONE        0xFFFFFFFFu

#define ST_MAGIC            0x54455357u     /* "WSET" */
#define ST_SLOTS            1023u           /* the condition array holds 1024; 8C0916EE caps the game at 0x3FF */
#define ST_EVQ              256u
#define ST_ITEMENTRY        0x14u
#define ST_SETENTRY         0x20u
#define ST_HEADROOM         32768u          /* bytes in one free heap block before a spawn (review: rings R.4 item 4) */
#define ST_MAX_CHILDREN     32u

/* slot phases */
#define ST_FREE             0u
#define ST_ACTIVE           1u              /* in the window */
#define ST_LEAVING          2u              /* left the window (or removed): freed, released once no task points at it */

typedef struct st_latch {
    uint32_t valid, cause, step;
    float touch[3], attacker[3];            /* SA1 space */
} st_latch;

typedef struct st_slot {
    uint32_t id, idx, type;                 /* the record (id 0: the slot is free) */
    uint32_t cost, box;                     /* the record's budget units and budget (1: boxes) when its slot was written */
    uint64_t user;                          /* the record's host tag (events after its removal keep it) */
    uint32_t phase;
    uint32_t desc_ver, state_ver;           /* the registry versions the slot has taken */
    uint32_t want, want_state, want_bits;   /* a forced state to apply (latched at the pair start) */
    uint32_t upd;                           /* the record changed: free, then rewrite */
    uint32_t rewrite;                       /* write the SET entry and the condition once no task points at the slot */
    uint32_t tp;                            /* the slot's task (guest), 0: none */
    uint32_t freed;                         /* libsa1 freed that task (FreeTask) */
    uint32_t gone;                          /* GONE was raised for that task */
    uint32_t held;                          /* the spawn hold of this step */
    uint32_t cond_prev, bits_prev, mode_prev;
    uint32_t child_seen;                    /* O_GRING: the children's bits seen collected (mode >= 2) */
    st_latch latch, frozen;
    uint32_t held_mark;                     /* round 0.9.4b (11.1): the window pass (S->pairs) that counted it as held */
} st_slot;

typedef struct st_ev {
    uint64_t user;
    uint32_t type, record, condition, user_bits, exec, step, cause, touch_step;
    float pos[3], att[3];                   /* SA1 space */
} st_ev;

typedef struct st_cand {
    float d2;                               /* squared distance of its place (a ring group: of its nearest ring) */
    uint32_t id, idx;
    uint32_t tier;                          /* round 0.9.4b: 0, 1, 2 (order B above 400); 0 at 0 / 400 */
} st_cand;

typedef struct st_state {
    uint32_t magic, self;                   /* self: the block's guest address */
    uint32_t manager;                       /* the manager task (guest) */
    uint32_t pairs;                         /* pair starts */
    uint32_t hiwater;                       /* slots [0, hiwater) may be in use: B_8C79A3DC */
    uint32_t window_records, ring_units, box_units;
    uint32_t cap_window, cap_heap;          /* a CAPACITY episode is running */
    uint32_t refused_window, refused_heap, deferred;
    uint32_t heap_largest;
    uint32_t spawned, despawned, events;
    uint32_t ev_head, ev_count, ev_dropped;
    /* round 0.9.4b */
    float list_range;                       /* the range in the list's entries (0: the game's, attribute 0) */
    uint32_t objview;                       /* rt_objview: its pcdata offset + 1 (0: not looked for, ST_OBJV_NONE: none) */
    float objview_scale;                    /* the kx_scale last written */
    uint32_t tier_flips, tier_held;         /* tier 1 <-> 2 changes taken / held back by SA1_SET_TIER_HOLD */
    uint8_t tier_mem[SA1_SET_MAX_RECORDS];  /* registry entry -> its window tier 1 / 2 (0: none or tier 0) */
    uint32_t tier_id[SA1_SET_MAX_RECORDS];  /* the record that memory belongs to */
    uint32_t tier_pair[SA1_SET_MAX_RECORDS];/* the pair of its last change between tiers 1 and 2 */
    uint16_t slot_of[SA1_SET_MAX_RECORDS];  /* registry entry -> slot + 1 (valid while the slot's id is the entry's) */
    uint32_t sel[SA1_SET_MAX_RECORDS];      /* the pair (pairs) whose window selected the entry */
    st_cand cand[SA1_SET_MAX_RECORDS], tmp[SA1_SET_MAX_RECORDS];
    st_slot slot[ST_SLOTS];
    st_ev ev[ST_EVQ];
    /* game-visible (guest addresses the game code dereferences): the object list and the SET entries */
    uint8_t table[8];                       /* {Sint16 ssAttribute, Sint16 ssCount, SET_ITEMENTRY *pObjItemEntry} */
    uint8_t items[SA1SET_NTYPES][ST_ITEMENTRY];
    char names[SA1SET_NTYPES][16];
    uint8_t sets[ST_SLOTS][ST_SETENTRY];
} st_state;

/* ---- guest memory (the arena, bounded) ----------------------------------------------------------------------------- */
static int st_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static uint32_t st_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (!st_arena(g) || sa1g_world_rd32(w, g & ~3u, &v) != 0) {
        return 0;
    }
    return v;
}

static uint32_t st_rd16(const sa1g_world *w, uint32_t g)
{
    return (st_rd32(w, g & ~3u) >> (8u * (g & 2u))) & 0xFFFFu;
}

static uint32_t st_rd8(const sa1g_world *w, uint32_t g)
{
    return (st_rd32(w, g & ~3u) >> (8u * (g & 3u))) & 0xFFu;
}

static float st_rdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = st_rd32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static void st_rd3(const sa1g_world *w, uint32_t g, float v[3])
{
    v[0] = st_rdf(w, g);
    v[1] = st_rdf(w, g + 4u);
    v[2] = st_rdf(w, g + 8u);
}

static void st_wr32(sa1g_world *w, uint32_t g, uint32_t v)
{
    if (st_arena(g)) {
        sa1g_world_wr32(w, g & ~3u, v);
    }
}

static void st_wr16(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint32_t a = g & ~3u, sh = 8u * (g & 2u), o = st_rd32(w, a);
    st_wr32(w, a, (o & ~(0xFFFFu << sh)) | ((v & 0xFFFFu) << sh));
}

static void st_wr8(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint32_t a = g & ~3u, sh = 8u * (g & 3u), o = st_rd32(w, a);
    st_wr32(w, a, (o & ~(0xFFu << sh)) | ((v & 0xFFu) << sh));
}

static void st_put16(uint8_t *p, uint32_t v)
{
    uint16_t x = (uint16_t)v;
    memcpy(p, &x, 2);
}

static void st_put32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static void st_putf(uint8_t *p, float f)
{
    memcpy(p, &f, 4);
}

static int st_same(uint32_t a, uint32_t b)
{
    return (a & 0x1FFFFFFFu) == (b & 0x1FFFFFFFu);
}

static uint32_t st_cond(uint32_t k)
{
    return ST_ADDR_COND + 0x10u * k;
}

/* ---- the state ------------------------------------------------------------------------------------------------------- */
static st_state *st_state_of(const sa1g_world *w)
{
    st_state *S;
    uint32_t g;
    if (w == NULL || w->ext == NULL || w->set_state == 0) {
        return NULL;
    }
    g = w->set_state;
    if (((g >> 24) & 31u) != SA1G_SEG_EXT || (size_t)(g & 0xFFFFFFu) + sizeof(st_state) > w->ext_used) {
        return NULL;
    }
    S = (st_state *)(void *)(w->ext + (g & 0xFFFFFFu));
    return S->magic == ST_MAGIC && S->self == g ? S : NULL;
}

static uint32_t st_guest(const st_state *S, const void *p)
{
    return S->self + (uint32_t)((const uint8_t *)p - (const uint8_t *)S);
}

/* the SET entry of slot k (guest) */
static uint32_t st_set_guest(const st_state *S, uint32_t k)
{
    return st_guest(S, S->sets[k]);
}

/* the generated object list (rings review R.2.9: B_8C79A3CC points to it) */
static void st_build_list(st_state *S)
{
    uint32_t t;
    st_put16(S->table + 0, 0u);
    st_put16(S->table + 2, SA1SET_NTYPES);
    st_put32(S->table + 4, st_guest(S, S->items[0]));
    for (t = 0; t < SA1SET_NTYPES; t++) {
        const sa1set_type *ty = sa1set_type_of(t + 1u);
        uint8_t *e = S->items[t];
        size_t n = strlen(ty->name);
        e[0] = ty->im;
        e[1] = ty->level;
        st_put16(e + 2, ty->attr);
        st_putf(e + 4, 0.0f);               /* fRange: unused for attribute 0 (the spawner's default) */
        st_put32(e + 8, 0u);                /* ptr: copied to twp->counter */
        st_put32(e + 12, ty->exec);
        memcpy(S->names[t], ty->name, n < sizeof S->names[t] ? n : sizeof S->names[t] - 1u);
        st_put32(e + 16, st_guest(S, S->names[t]));
    }
}

/* round 0.9.4b (objects2_plan.md 6.1 task 3): sa1_set_config.range as the game's own per-type range of the list entries.
 * READ (sadc src/1st_read/code_8C090FA0.c func_8C091380, :319-400): for every condition with bit 15 and without bit 0 the
 * spawner takes its entry ie = &B_8C79A3CC->pObjItemEntry[usID & 0x7FFF] and uses range = ie->fRange when ie->ssAttribute
 * has bit 0, -100 ("always") for bit 1, else 160,000 (:355-366); range is a SQUARED distance: the sphere test 8C09111A
 * compares x^2, then + y^2, then + z^2 against it (:176-197). So attribute | 1 with fRange = range * range makes the game's
 * own spawner, unchanged, make these four types within `range` units - the mechanism 583 entries of the disc's own lists use
 * (Twinkle Circuit: 2,000 for O ITEMBOX, ITEMBOX AIR and O_GRING). The spawner then writes fRange + 100 into the
 * condition's unionStatus (:393): the range-out word of 8C0919CC, which libsa1 stubs "ret 0" (gen/policy/stubs.tsv:128),
 * so that word changes nothing (the window frees what leaves, as before). Range 0 / 400 writes the list exactly as 0.9.4
 * (attribute 0, fRange 0). Single precision in the world's FP mode. */
static void st_list_range(st_state *S, float range)
{
    uint32_t t;
    int on = range > ST_RANGE_GAME;
    float f = on ? range * range : 0.0f;
    for (t = 0; t < SA1SET_NTYPES; t++) {
        const sa1set_type *ty = sa1set_type_of(t + 1u);
        uint8_t *e = S->items[t];
        st_put16(e + 2, on ? (uint32_t)ty->attr | 1u : (uint32_t)ty->attr);
        st_putf(e + 4, f);
    }
    S->list_range = on ? range : 0.0f;
}

/* the context's range above the game's (0: the game's 400) */
static float st_range(const sa1set_registry *R)
{
    return R != NULL && R->range > ST_RANGE_GAME ? R->range : 0.0f;
}

static st_state *st_state_create(sa1g_world *w)
{
    st_state *S = NULL;
    uint32_t g = sa1g_world_ext_alloc(w, sizeof(st_state), 16, (void **)&S);
    if (g == 0 || S == NULL) {
        return NULL;
    }
    memset(S, 0, sizeof *S);
    S->magic = ST_MAGIC;
    S->self = g;
    w->set_state = g;
    st_build_list(S);
    return S;
}

/* ---- events ---------------------------------------------------------------------------------------------------------- */
static st_ev *st_ev_new(st_state *S)
{
    st_ev *e;
    if (S->ev_count == ST_EVQ) {
        S->ev_head = (S->ev_head + 1u) % ST_EVQ;    /* the oldest goes */
        S->ev_count--;
        S->ev_dropped++;
    }
    e = &S->ev[(S->ev_head + S->ev_count) % ST_EVQ];
    S->ev_count++;
    S->events++;
    memset(e, 0, sizeof *e);
    return e;
}

/* the object's place: its task's position, else its SET entry's */
static void st_place(const sa1g_world *w, const st_state *S, uint32_t k, float p[3])
{
    const st_slot *sl = &S->slot[k];
    uint32_t twp = sl->tp != 0 ? st_rd32(w, sl->tp + 0x20u) : 0u;
    if (st_arena(twp)) {
        st_rd3(w, twp + 0x20u, p);
    } else {
        memcpy(p, S->sets[k] + 8, 12);
    }
}

static void st_push(sa1g_world *w, st_state *S, uint32_t k, uint32_t type, uint32_t cond, uint32_t bits)
{
    const st_slot *sl = &S->slot[k];
    const st_latch *l = sl->frozen.valid ? &sl->frozen : sl->latch.valid ? &sl->latch : NULL;
    st_ev *e = st_ev_new(S);
    e->user = sl->user;
    e->type = type;
    e->record = sl->id;
    e->condition = cond & 0xFFFFu;
    e->user_bits = bits & 0xFFu;
    e->exec = sa1set_type_of(sl->type) != NULL ? sa1set_type_of(sl->type)->exec : 0u;
    e->step = (uint32_t)w->steps;
    if (l != NULL && (type == SA1_SET_EVENT_GONE || type == SA1_SET_EVENT_BROKEN || type == SA1_SET_EVENT_USERBITS)) {
        e->cause = l->cause;
        e->touch_step = l->step;
        memcpy(e->pos, l->touch, sizeof e->pos);
        memcpy(e->att, l->attacker, sizeof e->att);
    } else {
        e->cause = SA1_SET_CAUSE_UNKNOWN;
        st_place(w, S, k, e->pos);
        memcpy(e->att, e->pos, sizeof e->att);
    }
}

/* ---- tasks ----------------------------------------------------------------------------------------------------------- */
/* the task that points at slot k now (its pTask while that task's ocp is the slot), 0: none; *marked = it carries
 * DestroyTask (it ends at its next exec) */
static uint32_t st_task(const sa1g_world *w, uint32_t k, int *marked)
{
    uint32_t c = st_cond(k), tp = st_rd32(w, c + 4u), ex;
    if (marked != NULL) {
        *marked = 0;
    }
    if (!st_arena(tp) || st_rd32(w, tp + 0x1Cu) != c) {
        return 0;
    }
    ex = st_rd32(w, tp + 0x10u);
    if (ex == 0) {
        return 0;
    }
    if (marked != NULL) {
        *marked = st_same(ex, ST_EXEC_DESTROY);
    }
    return tp;
}

static uint32_t st_mode(const sa1g_world *w, uint32_t tp)
{
    uint32_t twp = st_arena(tp) ? st_rd32(w, tp + 0x20u) : 0u;
    return st_arena(twp) ? st_rd8(w, twp) : 0u;
}

/* a group's children (tp->ctp, then ->next) */
static uint32_t st_children(const sa1g_world *w, uint32_t tp, uint32_t *c, uint32_t cap)
{
    uint32_t n = 0, ct = st_rd32(w, tp + 0x0Cu), guard = 0;
    while (st_arena(ct) && n < cap && guard++ < 64u) {
        c[n++] = ct;
        ct = st_rd32(w, ct);
    }
    return n;
}

/* review R.8: the game is between a touch and its own end of the object */
static int st_protected(const sa1g_world *w, uint32_t type, uint32_t tp)
{
    uint32_t m = st_mode(w, tp), c[ST_MAX_CHILDREN], n, i;
    switch (type) {
    case SA1_OBJTYPE_RING:
        return m == 2u || m == 3u;
    case SA1_OBJTYPE_O_GRING:
        n = st_children(w, tp, c, ST_MAX_CHILDREN);
        for (i = 0; i < n; i++) {
            uint32_t cm = st_mode(w, c[i]);
            if (cm == 2u || cm == 3u) {
                return 1;
            }
        }
        return 0;
    case SA1_OBJTYPE_ITEMBOX:
        return m == 3u;
    case SA1_OBJTYPE_ITEMBOX_AIR:
        return m >= 2u && m <= 4u;
    default:
        return 0;
    }
}

/* FreeTask inside the world (silent: DestroyTask runs the task's dest at its next exec) */
static void st_free(sa1g_world *w, st_slot *sl, uint32_t tp)
{
    (void)w;                                /* inside the world: sa1_decode maps through the entered segments */
    ((void (*)(void *))sa1_fn(SA1_GFN_C(ST_FN_FREETASK), 0u))(sa1_decode(tp));
    if (sl->tp == tp || sl->tp == 0) {
        sl->tp = tp;
        sl->freed = 1;
    }
}

/* ---- the heap (the free list of 8C098998's allocator; as sa1_world_hostobj.c) --------------------------------------- */
static uint32_t st_heap_largest(const sa1g_world *w)
{
    uint32_t blk = st_rd32(w, ST_ADDR_HEAP_FREE), l = 0, n = 0;
    while (st_arena(blk) && n < 0x10000u) {
        uint32_t end = st_rd32(w, blk + 8u), len;
        if (end == 0) {
            break;                              /* the arena-end header */
        }
        if (!st_arena(end) || (end & 0xFFFFFFu) <= (blk & 0xFFFFFFu)) {
            break;
        }
        len = (end & 0xFFFFFFu) - (blk & 0xFFFFFFu);
        l = len > l ? len : l;
        blk = st_rd32(w, blk);
        n++;
    }
    return l;
}

/* ---- conversions (host -> SA1, in the world's FP mode; no CRT maths) ----------------------------------------------- */
/* round 0.9.4b (objects2_plan.md 5.4): exported unchanged as sa1w_set_point / sa1w_set_angles (the showcase's,
 * src/world/sa1_world_preview.c, write their copies' SET entries exactly as a record is written here) */
static void st_point(const sa1_coords *c, const float h[3], float s[3])
{
    const float *a = c->axis;
    float inv = 1.0f / c->scale;
    s[0] = (a[0] * h[0] + a[3] * h[1] + a[6] * h[2]) * inv;
    s[1] = (a[1] * h[0] + a[4] * h[1] + a[7] * h[2]) * inv;
    s[2] = (a[2] * h[0] + a[5] * h[1] + a[8] * h[2]) * inv;
}

static int32_t st_bams(float rad)
{
    float t = rad * 10430.378f;     /* 65536 / 2 pi */
    int32_t i = (int32_t)(t >= 0.0f ? t + 0.5f : t - 0.5f);
    return i & 0xFFFF;
}

/* the SET entry's angles (BAMS x, y, z) of a host rotation for the type's order: R_sa1 = A^T Rh A (column vectors),
 * which the type's display builds as njRotateZ(z), njRotateX(x), njRotateY(y) = Rz(z) Rx(x) Ry(y) (rt_matrix.c: the
 * SDK's row-vector rotations are the standard right-handed ones transposed) */
/* R_sa1 = A^T Rh A (row-major, column vectors: an SA1-space local vector v turns to r v) */
static void st_rmat(const sa1_coords *c, const float rh[9], float r[9])
{
    const float *a = c->axis;
    float m[9];
    int i, j, k;
    for (i = 0; i < 3; i++) {           /* m = Rh A */
        for (j = 0; j < 3; j++) {
            float s = 0.0f;
            for (k = 0; k < 3; k++) {
                s += rh[3 * i + k] * a[3 * k + j];
            }
            m[3 * i + j] = s;
        }
    }
    for (i = 0; i < 3; i++) {           /* r = A^T m */
        for (j = 0; j < 3; j++) {
            float s = 0.0f;
            for (k = 0; k < 3; k++) {
                s += a[3 * k + i] * m[3 * k + j];
            }
            r[3 * i + j] = s;
        }
    }
}

static void st_angles(const sa1_coords *c, const float rh[9], uint32_t order, int32_t ang[3])
{
    float r[9], sx, cx;
    ang[0] = ang[1] = ang[2] = 0;
    if (order == SA1_SET_ROT_NONE) {
        return;
    }
    st_rmat(c, rh, r);
    if (order == SA1_SET_ROT_Y) {
        ang[1] = st_bams(sa1g_atan2f(-r[6], r[8]));
        return;
    }
    sx = r[7] > 1.0f ? 1.0f : r[7] < -1.0f ? -1.0f : r[7];
    cx = sa1g_sqrtf(1.0f - sx * sx);
    ang[0] = st_bams(sa1g_atan2f(sx, cx));
    if (cx > 0.0001f) {
        ang[1] = st_bams(sa1g_atan2f(-r[6], r[8]));
        ang[2] = st_bams(sa1g_atan2f(-r[1], r[4]));
    } else {
        ang[1] = st_bams(sa1g_atan2f(r[2], r[0]));     /* gimbal: z = 0 */
    }
}

void sa1w_set_point(const sa1_coords *c, const float h[3], float s[3])
{
    st_point(c, h, s);
}

void sa1w_set_angles(const sa1_coords *c, const float rh[9], uint32_t order, int32_t ang[3])
{
    st_angles(c, rh, order, ang);
}

static int st_player_pos(const sa1g_world *w, float p[3])
{
    uint32_t twp = st_rd32(w, SA1G_ADDR_PLAYER_TWP);
    if (!st_arena(twp)) {
        return 0;
    }
    st_rd3(w, twp + 0x20u, p);
    return p[0] == p[0] && p[1] == p[1] && p[2] == p[2];
}

/* ---- slots ----------------------------------------------------------------------------------------------------------- */
static void st_count_write(sa1g_world *w, st_state *S)
{
    uint32_t k = S->hiwater;
    while (k > 0 && S->slot[k - 1u].phase == ST_FREE) {
        k--;
    }
    S->hiwater = k;
    st_wr16(w, ST_ADDR_COUNT, k);
}

/* the SET entry and the condition of slot k from the registry entry e (no task points at the slot) */
static void st_write_slot(sa1g_world *w, st_state *S, uint32_t k, const sa1set_entry *e)
{
    const sa1set_type *t = sa1set_type_of(e->rec.type);
    st_slot *sl = &S->slot[k];
    uint8_t *se = S->sets[k];
    float pos[3];
    int32_t ang[3];
    uint32_t c = st_cond(k), cond, bits;
    st_point(&w->set_ctx->coords, e->rec.position, pos);
    st_angles(&w->set_ctx->coords, e->rec.rotation, t->rotation_order, ang);
    memset(se, 0, ST_SETENTRY);
    st_put16(se + 0, e->rec.type - 1u);     /* usID: the type's index in the generated list */
    st_put16(se + 2, (uint32_t)ang[0]);
    st_put16(se + 4, (uint32_t)ang[1]);
    st_put16(se + 6, (uint32_t)ang[2]);
    st_putf(se + 8, pos[0]);
    st_putf(se + 12, pos[1]);
    st_putf(se + 16, pos[2]);
    st_putf(se + 20, e->rec.params[0]);
    st_putf(se + 24, e->rec.params[1]);
    st_putf(se + 28, e->rec.params[2]);
    cond = ((e->state & SA1_SET_STATE_PRESENT) ? 0x8000u : 0u) |
           ((e->state & SA1_SET_STATE_BROKEN) && e->rec.type == SA1_OBJTYPE_ITEMBOX ? 0x0004u : 0u);
    bits = e->rec.type == SA1_OBJTYPE_O_GRING ? (e->user_bits & 0xFFu) : 0u;
    st_wr32(w, c + 0u, (bits << 8) | (cond << 16));     /* scCount 0, scUserFlag, ssCondition */
    st_wr32(w, c + 4u, 0u);                             /* pTask */
    st_wr32(w, c + 8u, st_set_guest(S, k));             /* pObjEditEntry */
    st_wr32(w, c + 12u, 0u);                            /* unionStatus */
    sl->type = e->rec.type;
    sl->user = e->rec.user;
    sl->cost = sa1set_cost(&e->rec);
    sl->box = t->budget == SA1_SET_BUDGET_BOX;
    sl->desc_ver = e->desc_version;
    sl->state_ver = e->state_version;
    sl->want = 0;
    sl->upd = 0;
    sl->rewrite = 0;
    sl->cond_prev = cond;
    sl->bits_prev = bits;
    sl->child_seen = bits;
}

static void st_release(sa1g_world *w, st_state *S, uint32_t k)
{
    st_slot *sl = &S->slot[k];
    uint32_t c = st_cond(k), i;
    if (sl->idx < SA1_SET_MAX_RECORDS && S->slot_of[sl->idx] == k + 1u) {
        S->slot_of[sl->idx] = 0;
    }
    for (i = 0; i < 16u; i += 4u) {
        st_wr32(w, c + i, 0u);
    }
    memset(S->sets[k], 0, ST_SETENTRY);
    memset(sl, 0, sizeof *sl);
}

/* ---- the manager ----------------------------------------------------------------------------------------------------- */
static int st_manager_live(const sa1g_world *w, const st_state *S)
{
    return st_arena(S->manager) && st_same(st_rd32(w, S->manager + 0x10u), SA1G_FN_SET_MANAGER);
}

static void st_manager(sa1g_world *w, st_state *S)
{
    uint32_t tp, twp, k;
    /* mode 0's work (8C090FA0 case 0 and 8C091016, done by libsa1: v1_arch review G2) */
    st_wr32(w, ST_ADDR_ITEMTABLE, st_guest(S, S->table));
    st_wr32(w, ST_ADDR_SETDATA, 0u);
    st_wr8(w, ST_ADDR_MGR_LVL, 0u);
    st_wr8(w, ST_ADDR_MGR_LVL + 1u, 0u);
    st_wr32(w, ST_ADDR_ED0, 1u);
    st_wr32(w, ST_ADDR_ACTCHANGE, 0u);
    st_wr32(w, ST_ADDR_FIRSTPASS, 0u);
    if (S->hiwater == 0) {
        for (k = 0; k < 0x10u * (ST_SLOTS + 1u); k += 4u) {
            st_wr32(w, ST_ADDR_COND + k, 0u);
        }
        st_wr16(w, ST_ADDR_COUNT, 0u);
    }
    tp = sa1g_create_task(2u, 8, SA1G_FN_SET_MANAGER);  /* level 8: list 7 (gsa1_094_items.md 6.1) */
    twp = st_arena(tp) ? st_rd32(w, tp + 0x20u) : 0u;
    if (st_arena(twp)) {
        st_wr8(w, twp, 1u);                 /* mode 1: the spawner every step */
        S->manager = tp;
    }
}

/* ---- the window (pair start) ------------------------------------------------------------------------------------------ */
static int st_less(const st_cand *a, const st_cand *b)
{
    if (a->tier != b->tier) {
        return a->tier < b->tier;
    }
    return a->d2 < b->d2 || (a->d2 == b->d2 && a->id < b->id);
}

/* round 0.9.4b (objects2_plan.md 11.1): the squared distance from p to the nearest ring of a ring line or circle record
 * whose place is q (SA1 space), *near = that ring's place; the rings the registry's user bits name (taken) are left out.
 * The game's layout (sadc src/1st_read/code_8C087B80.c): objexec_O_GRING mode 0 runs i = 0 .. (Sint32)scl.x, a circle
 * (scl.z 1) skipping i = 0; func_8C087EEE (a line) puts ring i at local z = scl.y * ceil(i / 2) for odd i and
 * -(scl.y * i * 0.5) for even i, func_8C088000 (a circle) at local (scl.y sin a, 0, scl.y cos a) with a =
 * (Sint32)(i * 360 / scl.x * 182.04444) BAMS; both turned by njRotateZ / X / Y of the entry's angles, i.e. the record's
 * rotation (here its SA1-space matrix before the conversion to BAMS) and the FSCA model's sine. Returns -1 when the record
 * has no ring left (then its place counts) */
static float st_gring_near(const sa1_coords *co, const sa1set_entry *e, const float q[3], const float p[3], float near[3])
{
    const float *pr = e->rec.params;
    float r[9], best = -1.0f;
    int32_t n = (int32_t)pr[0], i;
    int circle = pr[2] == 1.0f;
    if (n < 0 || n > 15) {
        return -1.0f;
    }
    st_rmat(co, e->rec.rotation, r);
    for (i = circle ? 1 : 0; i <= n; i++) {
        float v[3] = { 0.0f, 0.0f, 0.0f }, x[3], d[3], d2;
        int k;
        if (i < 8 && (e->user_bits & (1u << (uint32_t)i))) {
            continue;                       /* taken (the game's 8 user bits) */
        }
        if (!circle) {
            v[2] = (i & 1) ? pr[1] * (float)((i + 1) / 2) : -(pr[1] * (float)i * 0.5f);
        } else {
            float sn, cs;
            uint32_t a = (uint32_t)(int32_t)((float)i * 360.0f / pr[0] * 182.04444f);
            sh4_fsca(a & 0xFFFFu, &sn, &cs);
            v[0] = pr[1] * sn;
            v[2] = pr[1] * cs;
        }
        for (k = 0; k < 3; k++) {
            x[k] = q[k] + (r[3 * k + 0] * v[0] + r[3 * k + 1] * v[1] + r[3 * k + 2] * v[2]);
            d[k] = x[k] - p[k];
        }
        d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (best < 0.0f || d2 < best) {
            best = d2;
            memcpy(near, x, sizeof x);
        }
    }
    return best;
}

/* round 0.9.4b: the window tier (1 or 2) a record beyond the game's 400 units keeps (review R.12 / R.13): a change between
 * 1 and 2 is taken only SA1_SET_TIER_HOLD pairs (the registry's test override: tier_hold >= 0) after the last one; a record
 * first seen beyond 400, or coming back from tier 0, takes its tier at once */
static uint32_t st_tier(st_state *S, uint32_t idx, uint32_t id, uint32_t raw, uint32_t hold)
{
    if (S->tier_id[idx] != id || S->tier_mem[idx] == 0) {
        S->tier_id[idx] = id;
        S->tier_mem[idx] = (uint8_t)raw;
        S->tier_pair[idx] = S->pairs - hold;
        return raw;
    }
    if (raw != S->tier_mem[idx]) {
        if (S->pairs - S->tier_pair[idx] >= hold) {
            S->tier_mem[idx] = (uint8_t)raw;
            S->tier_pair[idx] = S->pairs;
            S->tier_flips++;
        } else {
            S->tier_held++;
        }
    }
    return S->tier_mem[idx];
}

/* round 0.9.4b (objects2_plan.md 6.1 task 6): the rt's object-view block (src/rt/rt_draw.c RT_OBJVIEW, R4 pcdata at the
 * start of segment 0x18, found by its magic as sa1_world_hud.c finds the capture header) gets kx_scale = the game's
 * horizontal tan / view_tan (0: off) and the four SET execs. The game's tan is read from the world's screen: (centre x -
 * clip x0) / (aspect x * distance) = 320 / 457.06 = 0.700 with the boot's screen. Written at a pair start whenever the
 * block differs (a configuration change; an image rebuild resets segment 0x18 and with it this state and the block).
 * The four displays' VISIBLE paths only draw (read for this round; the verifier reads them again):
 *   RING 8C086D00 (sadc code_8C0869E0.c:192-216): the blob shadow 8C092124 (draws only: code_8C092124.c:45-61), the attr
 *     masks saved / cleared / restored (8C056926 / 8C05695C / 8C056938: draw state B_8C88F660 / 664 and their copies
 *     B_8C788C38 / 3C, code_8C056860.c), njSetTexture (8C099500), the model 8C03700C; called by objexec_RING in mode 1, so
 *     D_8C1C1D40 holds 8C0869E8 (a group's rings: 8C087B80, whose 8C098E98 does not set it);
 *   the ground box 8C05D56E (code_8C05D0A0.c:304-336): models and the panel; B_8C78C378 is written there and again by the
 *     exec before its use (:162), the panel's texture id is model draw state; D_8C1B9BA0[6].tex is written BEFORE the
 *     test (one value per world: the HUD's 1-up icon); called by objexec_O_ITEMBOX (:200);
 *   the air box 8C05DC60 (code_8C05DC60.c): B_8C78C380 (re-written by the exec before every use: code_8C05D9C0.c:103),
 *     scale, blending mode, the constant material (8C043A20, 8C05DDBE) - draw state only; called by its exec.
 * NOT the dropped / dug ring 8C0870EA (its own exec: not in the list). Every other caller of 8C038D00 (the characters'
 * displays, whose visible paths run logic for Tails and Knuckles) keeps the game's screen. */
static void st_objview(sa1g_world *w, st_state *S, const sa1set_registry *R)
{
    static const uint32_t execs[4] = { ST_EXEC_RING, ST_EXEC_GRING, ST_EXEC_GBOX, ST_EXEC_ABOX };
    float vt = R != NULL ? R->view_tan : 0.0f, scale = 0.0f, cur;
    uint8_t *b;
    uint32_t i, n;
    if (S->objview == 0) {
        size_t off, end = w->pcdata_size;
        S->objview = ST_OBJV_NONE;
        if (w->ext != NULL && end <= w->ext_used && end >= ST_OBJV_BYTES) {
            for (off = 0; off + ST_OBJV_BYTES <= end; off += 4u) {
                uint32_t m0, m1, ver;
                memcpy(&m0, w->ext + off, 4);
                memcpy(&m1, w->ext + off + 4u, 4);
                memcpy(&ver, w->ext + off + 8u, 4);
                if (m0 == ST_OBJV_MAGIC0 && m1 == ST_OBJV_MAGIC1 && ver == 1u) {
                    S->objview = (uint32_t)off + 1u;
                    break;
                }
            }
        }
    }
    if (S->objview == ST_OBJV_NONE) {
        return;
    }
    b = w->ext + (S->objview - 1u);
    if (vt > 0.0f) {
        float game = (st_rdf(w, ST_ADDR_SCR_CX) - st_rdf(w, ST_ADDR_CLIP_X0)) /
                     (st_rdf(w, ST_ADDR_ASPECT_X) * st_rdf(w, ST_ADDR_SCR_DIST));
        scale = game > 0.0f && game == game ? game / vt : 0.0f;
    }
    memcpy(&cur, b + 12u, 4);
    memcpy(&n, b + 16u, 4);
    if (memcmp(&cur, &scale, 4) != 0 || n != 4u) {
        n = 4u;
        memcpy(b + 12u, &scale, 4);
        memcpy(b + 16u, &n, 4);
        for (i = 0; i < 4u; i++) {
            memcpy(b + 20u + 4u * i, &execs[i], 4);
        }
    }
    S->objview_scale = scale;
}

static void st_sort(st_state *S, uint32_t n)
{
    uint32_t width, i;
    st_cand *src = S->cand, *dst = S->tmp, *t;
    for (width = 1; width < n; width *= 2u) {
        for (i = 0; i < n; i += 2u * width) {
            uint32_t a = i, am = i + width < n ? i + width : n, b = am, bm = i + 2u * width < n ? i + 2u * width : n, o = i;
            while (a < am && b < bm) {
                dst[o++] = st_less(&src[b], &src[a]) ? src[b++] : src[a++];
            }
            while (a < am) {
                dst[o++] = src[a++];
            }
            while (b < bm) {
                dst[o++] = src[b++];
            }
        }
        t = src;
        src = dst;
        dst = t;
    }
    if (src != S->cand) {
        memcpy(S->cand, src, n * sizeof *src);
    }
}

static void st_window(sa1g_world *w, st_state *S, const sa1set_registry *R)
{
    const sa1_coords *co = &w->set_ctx->coords;
    uint32_t kbit = w->kind >= 0 && w->kind < 32 ? 1u << (uint32_t)w->kind : 0u;
    uint32_t n = 0, i, k, ring_used = 0, box_used = 0, refused = 0, selected = 0, held_ring = 0, held_box = 0, pass;
    uint32_t sel_mark;
    uint32_t ring_max = R != NULL ? R->ring_window : 64u, box_max = R != NULL ? R->box_window : 32u;
    uint32_t hold = R != NULL && R->tier_hold >= 0 ? (uint32_t)R->tier_hold : SA1_SET_TIER_HOLD;
    float margin = R != NULL ? R->margin : 100.0f, p[3], range = st_range(R), fwd[3] = { 0.0f, 0.0f, 0.0f };
    if (!st_player_pos(w, p)) {
        return;                             /* no player 0: the window waits */
    }
    if (range > 0.0f) {
        /* order B: the plane through the virtual eye (sa1g_camera_view: SA1G_VIEW_DIST behind player 0 along the camera's
           yaw, written into w->camera.pos at this step's start) perpendicular to that yaw: forward = (-sin c, 0, -cos c) */
        float sn, cs;
        sh4_fsca(w->camera.ang[1] & 0xFFFFu, &sn, &cs);
        fwd[0] = -sn;
        fwd[2] = -cs;
    }
    for (i = 0; R != NULL && i < R->hiwater; i++) {
        const sa1set_entry *e = &R->ent[i];
        const sa1set_type *t;
        float q[3], d[3], d2, r;
        if (e->id == 0 || (e->rec.kinds != 0 && !(e->rec.kinds & kbit)) || (R->unavailable & (1u << e->rec.type))) {
            continue;
        }
        t = sa1set_type_of(e->rec.type);
        if (t == NULL) {
            continue;
        }
        st_point(co, e->rec.position, q);
        d[0] = q[0] - p[0];
        d[1] = q[1] - p[1];
        d[2] = q[2] - p[2];
        d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        r = (range > 0.0f ? range : t->range) + margin;
        if (!(d2 <= r * r)) {
            continue;                       /* the reach: the record's place, where the spawner tests it */
        }
        S->cand[n].d2 = d2;
        S->cand[n].id = e->id;
        S->cand[n].idx = i;
        S->cand[n].tier = 0;
        if (e->rec.type == SA1_OBJTYPE_O_GRING) {
            float nr[3], g2 = st_gring_near(co, e, q, p, nr);
            if (g2 >= 0.0f) {
                S->cand[n].d2 = g2;         /* 11.1: ordered by its nearest ring */
                memcpy(q, nr, sizeof q);
            }
        }
        if (range > 0.0f) {
            if (d2 <= ST_RANGE_GAME * ST_RANGE_GAME) {
                S->tier_mem[i] = 0;         /* tier 0: what the game itself would have live; never held */
                S->tier_id[i] = e->id;
            } else {
                float f = (q[0] - w->camera.pos[0]) * fwd[0] + (q[1] - w->camera.pos[1]) * fwd[1] +
                          (q[2] - w->camera.pos[2]) * fwd[2];
                S->cand[n].tier = st_tier(S, i, e->id, f > 0.0f ? 1u : 2u, hold);
            }
        }
        n++;
    }
    st_sort(S, n);
    /* the budgets count every task the records have: a slot that left the window keeps its units while its task lives
     * (the touch rule keeps a sparkling ring or a breaking box), so the selection gets what is left; a slot that would
     * leave in this pass with such a task holds its units too (a second pass then selects with less) */
    for (k = 0; k < S->hiwater; k++) {
        const st_slot *sl = &S->slot[k];
        if (sl->phase == ST_LEAVING && st_task(w, k, NULL) != 0) {
            if (sl->box) {
                held_box += sl->cost;
            } else {
                held_ring += sl->cost;
            }
        }
    }
    for (pass = 0; pass < 4u; pass++) {
        uint32_t more_ring = 0, more_box = 0;
        ring_used = held_ring;
        box_used = held_box;
        selected = 0;
        refused = 0;
        for (i = 0; i < n; i++) {
            const sa1set_entry *e = &R->ent[S->cand[i].idx];
            uint32_t cost = sa1set_cost(&e->rec);
            int box = sa1set_type_of(e->rec.type)->budget == SA1_SET_BUDGET_BOX;
            if (box ? box_used + cost <= box_max : ring_used + cost <= ring_max) {
                if (box) {
                    box_used += cost;
                } else {
                    ring_used += cost;
                }
                S->sel[S->cand[i].idx] = S->pairs + pass * 0x40000000u;
                selected++;
            } else {
                S->sel[S->cand[i].idx] = 0;
                refused++;
            }
        }
        for (k = 0; k < S->hiwater; k++) {
            st_slot *sl = &S->slot[k];
            uint32_t tp;
            if (sl->phase != ST_ACTIVE || (sa1set_entry_of(R, sl->id) != NULL && S->sel[sl->idx] ==
                                           S->pairs + pass * 0x40000000u)) {
                continue;
            }
            if (sl->held_mark == S->pairs) {
                continue;                   /* round 0.9.4b (11.1): already held by an earlier pass (no under-fill) */
            }
            tp = st_task(w, k, NULL);
            if (tp != 0 && st_protected(w, sl->type, tp)) {
                sl->held_mark = S->pairs;
                if (sl->box) {
                    more_box += sl->cost;
                } else {
                    more_ring += sl->cost;
                }
            }
        }
        if (more_ring == 0 && more_box == 0) {
            break;
        }
        held_ring += more_ring;
        held_box += more_box;
    }
    sel_mark = S->pairs + (pass < 4u ? pass : 3u) * 0x40000000u;
    for (i = 0; i < n; i++) {
        if (S->sel[S->cand[i].idx] == sel_mark) {
            S->sel[S->cand[i].idx] = S->pairs;
        }
    }
    S->window_records = selected;
    S->ring_units = ring_used;
    S->box_units = box_used;
    S->refused_window += refused;
    if (refused != 0) {
        if (!S->cap_window) {
            sa1g_world_event_push(w, SA1_EVENT_CAPACITY, SA1_CAPACITY_SET_WINDOW, 0u, p, refused);
        }
        S->cap_window = 1;
    } else {
        S->cap_window = 0;
    }
    /* leaves: records not selected, or removed / replaced in the registry */
    for (k = 0; k < S->hiwater; k++) {
        st_slot *sl = &S->slot[k];
        const sa1set_entry *e;
        if (sl->phase != ST_ACTIVE) {
            continue;
        }
        e = sa1set_entry_of(R, sl->id);
        if (e == NULL || S->sel[sl->idx] != S->pairs) {
            sl->phase = ST_LEAVING;
            sl->want = 0;
            sl->upd = 0;
            sl->rewrite = 0;
        }
    }
    /* enters, nearest first; and the registry's versions of the slots in the window */
    for (i = 0; i < n; i++) {
        uint32_t idx = S->cand[i].idx, s1;
        const sa1set_entry *e = &R->ent[idx];
        st_slot *sl;
        if (S->sel[idx] != S->pairs) {
            continue;
        }
        s1 = S->slot_of[idx];
        if (s1 != 0 && S->slot[s1 - 1u].id == e->id) {
            sl = &S->slot[s1 - 1u];
            if (sl->phase == ST_LEAVING) {
                int marked = 0;
                uint32_t tp = st_task(w, s1 - 1u, &marked);
                sl->phase = ST_ACTIVE;
                if (tp == 0 || marked) {
                    /* back before its release, its task freed (bit 15 cleared) or gone: rewritten from the registry
                     * once no task points at the slot */
                    sl->rewrite = 1;
                    sl->want = 0;
                    sl->upd = 0;
                    sl->desc_ver = e->desc_version;
                    sl->state_ver = e->state_version;
                    continue;
                }
                /* its task was never freed (the touch rule kept it): it goes on; the registry's changes apply below */
            }
            if (sl->desc_ver != e->desc_version) {
                sl->upd = 1;
                sl->desc_ver = e->desc_version;
                sl->state_ver = e->state_version;
                sl->want = 0;
            } else if (sl->state_ver != e->state_version) {
                sl->want = 1;
                sl->want_state = e->state;
                sl->want_bits = e->user_bits;
                sl->state_ver = e->state_version;
            }
            continue;
        }
        for (k = 0; k < ST_SLOTS && S->slot[k].phase != ST_FREE; k++) {
        }
        if (k == ST_SLOTS) {
            S->refused_window++;
            continue;
        }
        sl = &S->slot[k];
        memset(sl, 0, sizeof *sl);
        sl->id = e->id;
        sl->idx = idx;
        sl->phase = ST_ACTIVE;
        S->slot_of[idx] = (uint16_t)(k + 1u);
        st_write_slot(w, S, k, e);
        if (k + 1u > S->hiwater) {
            S->hiwater = k + 1u;
        }
    }
    st_count_write(w, S);
}

/* ---- every step before the executor ----------------------------------------------------------------------------------- */
/* a forced state on slot k; 1: done, 0: wait (the touch rule, or bit 15 waits for the task to go) */
static int st_force(sa1g_world *w, st_state *S, uint32_t k)
{
    st_slot *sl = &S->slot[k];
    uint32_t c = st_cond(k), word = st_rd32(w, c), cond = word >> 16, bits = (word >> 8) & 0xFFu, tp, add;
    int marked = 0;
    tp = st_task(w, k, &marked);
    if (tp != 0 && !marked && st_protected(w, sl->type, tp)) {
        S->deferred++;
        return 0;
    }
    if (marked) {
        tp = 0;                             /* ending already */
    }
    if (sl->type == SA1_OBJTYPE_ITEMBOX) {
        uint32_t want4 = (sl->want_state & SA1_SET_STATE_BROKEN) ? 4u : 0u;
        if ((cond & 4u) != want4) {
            cond = (cond & ~4u) | want4;
            if (tp != 0 && (want4 ? st_mode(w, tp) < 3u : st_mode(w, tp) >= 4u)) {
                st_free(w, sl, tp);         /* the spawner makes the other form */
                tp = 0;
                marked = 1;
            }
        }
    }
    if (sl->type == SA1_OBJTYPE_O_GRING) {
        add = sl->want_bits & ~bits & 0xFFu;
        if (add != 0) {
            bits |= add;
            if (tp != 0) {
                st_free(w, sl, tp);         /* re-made without those rings */
                tp = 0;
                marked = 1;
            }
        }
    }
    if (!(sl->want_state & SA1_SET_STATE_PRESENT)) {
        cond &= ~0x8000u;
        if (tp != 0) {
            st_free(w, sl, tp);             /* silent removal */
        }
    } else if (!(cond & 0x8000u)) {
        if ((tp != 0 && !(cond & 1u)) || marked) {
            /* a task still points at the slot without bit 0 (E-102's firing) or is ending: bit 15 once it is gone */
            st_wr32(w, c, (word & 0xFFu) | (bits << 8) | (cond << 16));
            sl->cond_prev = cond;
            sl->bits_prev = bits;
            return 0;
        }
        cond |= 0x8000u;
    }
    st_wr32(w, c, (word & 0xFFu) | (bits << 8) | (cond << 16));
    sl->cond_prev = cond;                   /* no event for a forced write */
    sl->bits_prev = bits;
    sl->child_seen |= bits;
    return 1;
}

static void st_apply(sa1g_world *w, st_state *S)
{
    const sa1set_registry *R = sa1set_registry_of(w->set_ctx);
    uint32_t k;
    int changed = 0;
    for (k = 0; k < S->hiwater; k++) {
        st_slot *sl = &S->slot[k];
        uint32_t tp, c = st_cond(k);
        int marked = 0;
        if (sl->phase == ST_FREE) {
            continue;
        }
        tp = st_task(w, k, &marked);
        if (sl->phase == ST_LEAVING || sl->upd) {
            if (tp != 0 && !marked) {
                if (st_protected(w, sl->type, tp)) {
                    S->deferred++;
                    continue;               /* review R.8: after the game's own end */
                }
                st_free(w, sl, tp);
                marked = 1;
                if (sl->phase == ST_LEAVING) {
                    /* no spawn before its release (libsa1 freed it: no GONE) */
                    st_wr16(w, c + 2u, st_rd16(w, c + 2u) & ~0x8000u);
                    sl->cond_prev = st_rd16(w, c + 2u);
                }
            }
            if (sl->phase == ST_LEAVING) {
                if (tp != 0 || marked) {
                    continue;               /* released once the task is gone (DestroyTask clears bit 0) */
                }
                st_release(w, S, k);
                changed = 1;
                continue;
            }
            if (tp != 0 || marked) {
                continue;                   /* the update waits for the task to go */
            }
            sl->upd = 0;
            sl->rewrite = 1;
        }
        if (sl->rewrite) {
            const sa1set_entry *e = sa1set_entry_of(R, sl->id);
            if (tp != 0 || marked) {
                continue;
            }
            if (e != NULL) {
                st_write_slot(w, S, k, e);
            }
            sl->rewrite = 0;
            continue;
        }
        if (sl->want && st_force(w, S, k)) {
            sl->want = 0;
        }
    }
    if (changed) {
        st_count_write(w, S);
    }
}

/* the heap guard: hold the slots that could spawn in this step while the largest free block is below 32 KB */
static void st_hold(sa1g_world *w, st_state *S)
{
    uint32_t k, ready = 0, largest;
    float p[3] = { 0.0f, 0.0f, 0.0f };
    for (k = 0; k < S->hiwater && !ready; k++) {
        uint32_t cond;
        if (S->slot[k].phase != ST_ACTIVE) {
            continue;
        }
        cond = st_rd16(w, st_cond(k) + 2u);
        ready = (cond & 0x8000u) && !(cond & 1u) && st_task(w, k, NULL) == 0;
    }
    if (!ready) {
        return;
    }
    largest = st_heap_largest(w);
    S->heap_largest = largest;
    if (largest >= ST_HEADROOM) {
        S->cap_heap = 0;
        return;
    }
    for (k = 0; k < S->hiwater; k++) {
        uint32_t c = st_cond(k), cond;
        if (S->slot[k].phase != ST_ACTIVE) {
            continue;
        }
        cond = st_rd16(w, c + 2u);
        if ((cond & 0x8000u) && !(cond & 1u) && st_task(w, k, NULL) == 0) {
            st_wr16(w, c + 2u, cond | 1u);  /* the spawner passes a condition with bit 0 */
            S->slot[k].held = 1;
            S->refused_heap++;
        }
    }
    if (!S->cap_heap) {
        st_player_pos(w, p);
        sa1g_world_event_push(w, SA1_EVENT_CAPACITY, SA1_CAPACITY_SET_HEAP, 0u, p, largest);
    }
    S->cap_heap = 1;
}

static void st_pre(sa1g_world *w, int32_t sub, void *user)
{
    st_state *S = st_state_of(w);
    (void)user;
    if (sub == 0) {
        const sa1set_registry *R = sa1set_registry_of(w->set_ctx);
        if (S == NULL && R != NULL && R->count != 0) {
            S = st_state_create(w);         /* NULL: segment 0x18 is full: no SET objects in this world */
        }
        if (S != NULL) {
            float range = st_range(R);
            if (!st_manager_live(w, S) && R != NULL && R->count != 0) {
                st_manager(w, S);
            }
            S->pairs++;
            if (memcmp(&range, &S->list_range, sizeof range) != 0) {
                st_list_range(S, range);    /* round 0.9.4b: a range change rewrites the four entries now */
            }
            st_objview(w, S, R);
            st_window(w, S, R);
        }
    }
    if (S != NULL) {
        st_apply(w, S);
        st_hold(w, S);
    }
    if (w->set_test_pre != NULL) {
        w->set_test_pre(w, sub);
    }
}

/* ---- after the CCL pass ------------------------------------------------------------------------------------------------- */
static void st_hit(sa1g_world *w, st_slot *sl, uint32_t twp, int ring, const float p0[3], int have_p0)
{
    uint32_t cwp = st_rd32(w, twp + 0x38u), hit, hid;
    st_latch l;
    if (!st_arena(cwp) || !(st_rd16(w, cwp + 2u) & 2u) || !have_p0) {
        return;
    }
    memset(&l, 0, sizeof l);
    l.valid = 1;
    l.step = (uint32_t)w->steps;
    memcpy(l.touch, p0, sizeof l.touch);
    memcpy(l.attacker, p0, sizeof l.attacker);
    if (ring) {
        l.cause = SA1_SET_CAUSE_BODY;
        if (st_rd16(w, twp + 0x06u) != 0) {
            l.cause = SA1_SET_CAUSE_MAGNET;     /* 8C08789A counts wtimer up once the pull began (review R.3) */
            st_rd3(w, twp + 0x20u, l.attacker);
        }
    } else {
        hit = st_rd32(w, cwp + 0x18u);
        if (!st_arena(hit)) {
            return;
        }
        hid = st_rd16(w, hit);
        if (hid == 0u) {
            l.cause = SA1_SET_CAUSE_BODY;
        } else if (hid == 1u) {
            uint32_t atp = st_rd32(w, hit + 0x10u), atwp = st_arena(atp) ? st_rd32(w, atp + 0x20u) : 0u;
            l.cause = SA1_SET_CAUSE_PROJECTILE;
            if (st_arena(atwp)) {
                st_rd3(w, atwp + 0x20u, l.attacker);
            }
        } else {
            return;                             /* not a player's collision */
        }
    }
    sl->latch = l;
}

static void st_touch(sa1g_world *w, st_slot *sl, uint32_t tp, const float p0[3], int have_p0)
{
    uint32_t twp = st_rd32(w, tp + 0x20u), m = st_mode(w, tp), c[ST_MAX_CHILDREN], n, i;
    int freeze = 0;
    if (!st_arena(twp)) {
        return;
    }
    switch (sl->type) {
    case SA1_OBJTYPE_RING:
        freeze = m == 2u && sl->mode_prev == 1u;
        break;
    case SA1_OBJTYPE_ITEMBOX:
        freeze = m == 3u && sl->mode_prev == 2u;
        break;
    case SA1_OBJTYPE_ITEMBOX_AIR:
        freeze = m >= 2u && sl->mode_prev == 1u;
        break;
    case SA1_OBJTYPE_O_GRING:
        n = st_children(w, tp, c, ST_MAX_CHILDREN);
        for (i = 0; i < n; i++) {
            uint32_t ctwp = st_rd32(w, c[i] + 0x20u), bit;
            if (!st_arena(ctwp)) {
                continue;
            }
            bit = st_rd8(w, ctwp + 0x0Cu);      /* timer.b[0]: the ring's bit */
            if (st_rd8(w, ctwp) >= 2u && !(sl->child_seen & bit)) {
                sl->child_seen |= bit;
                freeze = 1;
            }
        }
        break;
    default:
        break;
    }
    if (freeze && sl->latch.valid) {
        sl->frozen = sl->latch;
    }
    if (sl->type == SA1_OBJTYPE_O_GRING) {
        n = st_children(w, tp, c, ST_MAX_CHILDREN);
        for (i = 0; i < n; i++) {
            uint32_t ctwp = st_rd32(w, c[i] + 0x20u);
            if (st_arena(ctwp) && st_rd8(w, ctwp) == 1u) {
                st_hit(w, sl, ctwp, 1, p0, have_p0);
            }
        }
    } else if (!(sl->type == SA1_OBJTYPE_RING && m != 1u)) {
        st_hit(w, sl, twp, sl->type == SA1_OBJTYPE_RING, p0, have_p0);
    }
    sl->mode_prev = m;
}

static void st_post(sa1g_world *w, int32_t sub, void *user)
{
    st_state *S = st_state_of(w);
    uint32_t k;
    float p0[3] = { 0.0f, 0.0f, 0.0f };
    int have_p0;
    (void)sub;
    (void)user;
    if (S == NULL) {
        return;
    }
    have_p0 = st_player_pos(w, p0);
    for (k = 0; k < S->hiwater; k++) {
        st_slot *sl = &S->slot[k];
        uint32_t c = st_cond(k), word, cond, bits, tp;
        int marked = 0;
        if (sl->phase == ST_FREE) {
            continue;
        }
        if (sl->held) {
            sl->held = 0;
            if (st_task(w, k, NULL) == 0) {
                st_wr16(w, c + 2u, st_rd16(w, c + 2u) & ~1u);   /* the hold ends */
            }
        }
        word = st_rd32(w, c);
        cond = word >> 16;
        bits = (word >> 8) & 0xFFu;
        /* the current task: ended, marked, or still running */
        if (sl->tp != 0) {
            uint32_t t = sl->tp, ex = st_rd32(w, t + 0x10u);
            int ended = st_rd32(w, t + 0x1Cu) != c || ex == 0;
            int mark = !ended && st_same(ex, ST_EXEC_DESTROY);
            if ((ended || mark) && !(cond & 0x8000u) && !sl->freed && !sl->gone) {
                st_push(w, S, k, SA1_SET_EVENT_GONE, cond, bits);
                sl->gone = 1;
            }
            if (!ended && !mark) {
                st_touch(w, sl, t, p0, have_p0);
            }
            if (ended) {
                st_push(w, S, k, SA1_SET_EVENT_DESPAWNED, cond, bits);
                S->despawned++;
                sl->tp = 0;
            }
        }
        if (sl->tp == 0) {
            tp = st_task(w, k, &marked);
            if (tp != 0 && !marked && (cond & 1u)) {
                sl->tp = tp;
                sl->freed = 0;
                sl->gone = 0;
                sl->mode_prev = st_mode(w, tp);
                memset(&sl->latch, 0, sizeof sl->latch);
                memset(&sl->frozen, 0, sizeof sl->frozen);
                st_push(w, S, k, SA1_SET_EVENT_SPAWNED, cond, bits);
                S->spawned++;
            }
        }
        if ((cond & 4u) && !(sl->cond_prev & 4u)) {
            st_push(w, S, k, SA1_SET_EVENT_BROKEN, cond, bits);
        }
        if (bits != sl->bits_prev) {
            st_push(w, S, k, SA1_SET_EVENT_USERBITS, cond, bits);
        }
        sl->cond_prev = cond;
        sl->bits_prev = bits;
    }
}

/* ---- the bridge ------------------------------------------------------------------------------------------------------- */
void sa1g_set_attach(sa1_context *ctx, sa1g_world *w)
{
    if (ctx == NULL || w == NULL) {
        return;
    }
    w->set_ctx = ctx;
    w->set_pre = st_pre;
    w->set_post = st_post;
}

int sa1w_set_exec_native(uint32_t exec)
{
    return sa1g_game_linked() && sa1_fn_name(exec) != NULL;
}

uint32_t sa1w_set_world_poll(const sa1_context *ctx, struct sa1g_world *w, sa1_set_event *out, uint32_t cap)
{
    st_state *S = st_state_of(w);
    uint32_t n = 0;
    if (S == NULL || out == NULL) {
        return 0;
    }
    while (n < cap && S->ev_count != 0) {
        const st_ev *e = &S->ev[S->ev_head];
        sa1_set_event *o = &out[n++];
        memset(o, 0, sizeof *o);
        o->type = e->type;
        o->record = e->record;
        o->user = e->user;
        o->condition = e->condition;
        o->user_bits = e->user_bits;
        o->exec = e->exec;
        o->step = e->step;
        o->cause = e->cause;
        o->touch_step = e->touch_step;
        sa1_to_host_point(ctx, e->pos, o->position);
        sa1_to_host_point(ctx, e->att, o->attacker);
        S->ev_head = (S->ev_head + 1u) % ST_EVQ;
        S->ev_count--;
    }
    return n;
}

void sa1w_set_world_info(const struct sa1g_world *w, sa1set_world_info *out)
{
    const st_state *S = st_state_of(w);
    uint32_t k, lv;
    memset(out, 0, sizeof *out);
    if (S == NULL) {
        return;
    }
    out->active = 1;
    out->manager = st_manager_live(w, S) ? S->manager : 0u;
    out->hiwater = st_rd16(w, ST_ADDR_COUNT);
    out->window_records = S->window_records;
    out->ring_units = S->ring_units;
    out->box_units = S->box_units;
    out->refused_window = S->refused_window;
    out->refused_heap = S->refused_heap;
    out->heap_largest = st_heap_largest(w);
    out->spawned = S->spawned;
    out->despawned = S->despawned;
    out->events = S->events;
    out->events_dropped = S->ev_dropped;
    out->deferred = S->deferred;
    out->list_range = S->list_range;
    out->tier_flips = S->tier_flips;
    out->tier_held = S->tier_held;
    out->objview_scale = S->objview_scale;
    out->objview_found = S->objview != 0 && S->objview != ST_OBJV_NONE;
    for (k = 0; k < S->hiwater; k++) {
        uint32_t tp, c[ST_MAX_CHILDREN], n, i;
        if (S->slot[k].phase == ST_FREE) {
            continue;
        }
        out->slots++;
        tp = st_task(w, k, NULL);
        if (tp == 0) {
            continue;
        }
        if (S->slot[k].type == SA1_OBJTYPE_ITEMBOX || S->slot[k].type == SA1_OBJTYPE_ITEMBOX_AIR) {
            out->live_boxes++;
        } else if (S->slot[k].type == SA1_OBJTYPE_O_GRING) {
            n = st_children(w, tp, c, ST_MAX_CHILDREN);
            for (i = 0; i < n; i++) {
                out->live_ring_tasks += st_same(st_rd32(w, c[i] + 0x10u), ST_EXEC_RING) ? 1u : 0u;
            }
        }
    }
    /* RING tasks in the task lists (whoever made them: the spawner's; libsa1 worlds make no others) */
    for (lv = 0; lv < 8u; lv++) {
        uint32_t tp = st_rd32(w, SA1G_ADDR_TASK_LISTS + 4u * lv), guard = 0;
        while (st_arena(tp) && guard++ < 0x2000u) {
            if (st_same(st_rd32(w, tp + 0x10u), ST_EXEC_RING)) {
                out->live_ring_tasks++;
            }
            tp = st_rd32(w, tp);
        }
    }
}

/* round 0.9.4b (review R.6): the records whose task lives after the last pair - the post hook's rule: a slot's task is
 * its pTask while that task's ocp is the slot (SPAWNED raised, DESPAWNED not yet) - ascending; returns how many (the first
 * cap written). No world state changes. */
uint32_t sa1w_set_world_live(const struct sa1g_world *w, uint32_t *out, uint32_t cap)
{
    const st_state *S = st_state_of(w);
    uint32_t k, n = 0, i;
    if (S == NULL) {
        return 0;
    }
    for (k = 0; k < S->hiwater; k++) {
        const st_slot *sl = &S->slot[k];
        uint32_t id = sl->id;
        if (sl->phase == ST_FREE || sl->tp == 0 || id == 0) {
            continue;
        }
        if (out != NULL && n < cap) {
            /* insertion into the ascending list (at most ST_SLOTS) */
            for (i = n; i > 0 && out[i - 1u] > id; i--) {
                out[i] = out[i - 1u];
            }
            out[i] = id;
        } else if (out != NULL && cap != 0 && out[cap - 1u] > id) {
            for (i = cap - 1u; i > 0 && out[i - 1u] > id; i--) {
                out[i] = out[i - 1u];
            }
            out[i] = id;
        }
        n++;
    }
    return n;
}

int32_t sa1w_set_world_slot(const struct sa1g_world *w, uint32_t id)
{
    const st_state *S = st_state_of(w);
    uint32_t k;
    if (S == NULL || id == 0) {
        return -1;
    }
    for (k = 0; k < S->hiwater; k++) {
        if (S->slot[k].phase != ST_FREE && S->slot[k].id == id) {
            return (int32_t)k;
        }
    }
    return -1;
}

void sa1w_set_world_test(struct sa1g_world *w, int32_t no_eye, void (*pre)(struct sa1g_world *w, int32_t sub))
{
    if (w != NULL) {
        w->set_no_eye = no_eye;
        w->set_test_pre = pre;
    }
}
