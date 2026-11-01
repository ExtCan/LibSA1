/*
 * sa1_coladapt_world.c - the world side of the collision adapter (sadc docs/libsa1_integration.md 5.4-5.8;
 * the host side, synthesis and stored descriptions are src/sa1_coladapt.c / sa1_collision.c).
 *
 * sa1ca_world_attach runs once after a world was booted with an EMPTY / WINDOW landtable (GRID collision
 * or no static collision; DISC and FAITHFUL worlds use their landtable in place and are left alone):
 *   - GRID: the cells of the current geometry generation are synthesized into the context's static
 *     collision segment if they are not there yet (sa1ca_build, once per generation, then sealed); with prepared
 *     geometry (sa1_collision_set_prepared) they are already in the prepared segment, which is the world's;
 *   - the adapter state goes into the world's extension segment, anchored in the window landtable's
 *     header (words +0x1C / +0x20, which only the landtable manager's draw functions read; that task
 *     does not run in libsa1 worlds), so nothing is allocated per world outside its own segments and
 *     the world core needs no field for it;
 *   - kill planes: DeathZoneTable[level] (8C1BE404) of the world -> an act table whose every entry is
 *     the synthesized DEATHZONE list, and the stage start's death zone task 8C07BAC0 (created after the
 *     player, as the stage start does): when the character is below a kill triangle by at most 30 units
 *     the game's own death sequence starts (scream, then the restart request 8C04E386 -> SA1_EVENT_RESTART);
 *   - the per-step refresh w->adapter_fn, which the tick runs after the executor and CCL and before the
 *     land collision update 8C028EC2 (design 5.8).
 *
 * The refresh (inside the step: no allocation through the host, no CRT maths; trigonometry through the
 * world's FSCA / atan2 models so that every target computes the same bits):
 *   moving objects (5.5)  every sa1_collision_object_* object of the context gets a slot: an NJS_OBJECT +
 *                         NJS_MODEL per collidable surface word (extension segment, model in object space,
 *                         evalflags 0, center 0 so that 8C0528AE never sets 0x10000000), a proxy task
 *                         (CreateElementalTask(6, 0, NULL): taskwk + force work, the game's empty exec)
 *                         and DynamicCOL entries 8C0528AE(attr | 0x08000000: rebuilt every update). A move
 *                         reaches the world at its next pair: half of it at sub step 0, the rest at sub 1.
 *                         The force work of the proxy gets the motion of the last update at the
 *                         character's position (pos_spd = M_new * M_old^-1 * p - p) and the change of the
 *                         character's facing (ang_spd.y): 8C07506E adds them when the character stands on
 *                         the object in the next step (the polygons it collides with are the ones built
 *                         now). Removed objects are withdrawn (8C052940); slots keep their task and memory
 *                         for the next object.
 *   window (5.4)          the GRID objects whose COL sphere comes within the gather range (D_8C19E96C, 400)
 *                         of the character, nearest first (by the distance to the object's box, which the GRID
 *                         data carries: of overlapping cell parts and stacked layers, the ones under and around
 *                         the character come first), copied into the window landtable within the
 *                         game's limits: at most window_objects (128) static entries and 191 land objects
 *                         with the moving ones in range (8C028EC2's object cache: 192, overflow is a NULL
 *                         write), the polygon budget (window_polygons, the pool holds 8192), and the query
 *                         bound (round 0.9.2, sadc docs/research/gsa1_090_collision.md C3 / F1a with its Review
 *                         R.4.1): 8C073160 / 8C0777E0 copy EVERY land object of the character's query into
 *                         hits[16] / sorted[16], touched or not, and the query (8C0522C0 from 8C028BFE: centre the
 *                         character's position + r along its up vector, radius r = playerwk.p.height / 2) takes
 *                         every object whose COL sphere reaches it; so of the objects whose COL sphere comes within
 *                         2 r + 20 of the character (water ones for E-102 only: 8C073160 skips them for the others)
 *                         at most 15 minus the moving entries in that range go in, nearest box first, the nearest
 *                         object always (the rest: SA1_CAPACITY_LAND_QUERY). The lowered 8C073160 / 8C0777E0 hold
 *                         192 per call besides (SA1_GA, SA1_HITCAP blocks in pcdata: cw_hitcap_*): the bound is a
 *                         mitigation, the arrays the fix (before 0.9.2 the guard counted 14 objects within 40
 *                         units of box distance, and a GRID cell's spheres reached far beyond: a host crash on
 *                         rp_downtown_v4c with E-102). Round 0.9.3: with both arrays there (and without
 *                         SA1_GRID_DC_LIMITS) the bound is the window's own 191 (stats.query_cap): every object in
 *                         reach is in the query, as on the DC (the 15 left out floors and ceilings in reach in
 *                         dense layered places; the window never hands out more than the arrays hold). The nearest
 *                         object and the others within 40 units may use the whole polygon pool beyond the
 *                         budget: the object under the character is never left out for the budget alone (GRID
 *                         objects hold at most 1024 triangles). Objects left out within 200 units are counted
 *                         and reported once per episode as SA1_EVENT_CAPACITY (farther ones only counted:
 *                         nearest first, the window ends there; round 0.9.3: the polygon budget is reported only
 *                         when an object within 40 units was left out even with the whole pool). W1 (round 0.9.1, cw_water): the lowest water object
 *                         above the character is always in the window, within the game's 128 copies and the pool (the
 *                         farthest entries make room), with a COL radius the water probe's 200-unit gather reaches: deep
 *                         host water keeps the game's air timer, water physics and drowning (not for E-102); land between
 *                         the character and that water (GRID land, and since round 0.9.2 the moving objects: water Review
 *                         R3.4) leaves it alone. WTR-A (round 0.9.2, cw_host_water / cw_water_dry, sadc
 *                         docs/research/gsa1_090_water.md): with the host's own water at the character (sa1_input.host_flags)
 *                         the copy leaves out water over a character the host says is dry (a room under a river) and W1
 *                         reaches deep water over a submerged ledge for one the host says is in water; without the flags
 *                         the window is 0.9.1's, byte for byte. Round 6a (SA1_CREATE_WATER_FLOOR, a per-world remap:
 *                         super_sonic.md Review R.3.4): the window copies every water object as a solid floor (COL bits
 *                         0x400002 cleared, 0x1 set; the GRID keeps only the upward faces of water), WTR-A's dry test does
 *                         not take it out and W1 adds no water above the character; the static collision is untouched.
 *   NEAR-poly list        PC-only capacity (design 5.7, sadc docs/research/gsa1_ramp_freeze.md fix 3): the lowered
 *                         8C028BFE (sadc src/1st_read/code_8C028BDC.c, SA1_GA) collects into a raised list of 512
 *                         (round 0.9.2, gsa1_090_collision.md F5; 256 before) in its pcdata (segment 0x18, one per
 *                         world) when the world sets its capacity; attach finds the block by its magic words and
 *                         sets 512 unless the geometry asks for the DC limits
 *                         (SA1_GRID_DC_LIMITS). Reference worlds (DISC / FAITHFUL) never attach: their list stays the
 *                         game's B_8C73E280[96]. Queries that fill the list are reported as SA1_CAPACITY_NEAR_POLYGONS.
 *   matrix stack          sa1ca_world_step_begin (start of every step, sa1_world_tick.c): a push 8C029B00 left behind
 *                         is undone (fix 10).
 *   front-only polygons   (round 0.9.3, sadc docs/research/gsa1_091b_ceiling.md FIX-C1 with its Review R.5.3) attach
 *                         enables the SA1_FRONTONLY block of the lowered 8C029B00 (sadc src/1st_read/code_8C029B00.c,
 *                         SA1_GA): a polygon with SA1_MAT_FRONT_ONLY of a land object in libsa1's GRID segment or of a
 *                         moving object's model (extension segment) is not touched by a sphere that starts behind it
 *                         (a thin roof's underside: a ceiling from below, absent from above), nor the slab's top over it
 *                         (SA1CA_MAT_THIN_TOP, set by GRID c2) by a sphere that starts more than its radius behind it moving
 *                         towards its front (rising from under the slab); the moving objects' models carry a third material
 *                         for SA1_SURFACE_FRONT_ONLY. Reference worlds never attach.
 *   contacts              (API 0.3, sa1_character_contacts: pushing physics props) at the start of each refresh, before
 *                         the moving objects move: which of them player 0 touched in this step, measured read-only on
 *                         the game's own result (cw_contacts below); player 0's motionwk.spd at the step's start is
 *                         taken by sa1ca_world_step_begin.
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_coladapt.h"
#include "sa1_api_world.h"
#include "sa1_hostobj.h"

#include <string.h>

#define CW_MAGIC        0x57434153u     /* "SACW" */
#define CW_ANCHOR_MAGIC 0x1Cu           /* window landtable header: pBinaryFileName (unused) */
#define CW_ANCHOR_STATE 0x20u           /* pLoadBinary (unused): guest address of the state */
#define CW_SLOTS        64u             /* moving objects per world */
#define CW_CAND         4096u           /* window candidates per step */
#define CW_MAX_LAND     191u            /* 8C028EC2's object cache holds 192, the last is never handed out */
#define CW_NEAR_RANGE   40.0f           /* objects this close to the character (box distance) may use the whole polygon pool */
/* the query bound (round 0.9.2, sadc docs/research/gsa1_090_collision.md C3 / F1a with its Review R.4.1): 8C073160 /
   8C0777E0 copy every land object of the character's query (8C028BFE -> 8C0522C0(cp->pos, r): centre the character's
   position + r along its up vector, radius r = playerwk.p.height / 2) into 16-entry arrays. An object can be gathered when
   its COL sphere comes within 2 r of the character; CW_QUERY_MARGIN covers the step's own movement before the query and
   8C074CF0's change of the offset. */
#define CW_QUERY_MAX    15u             /* land objects of one query the window lets through (moving ones included) when
                                           the lowered game code has no SA1_HITCAP arrays or the geometry keeps the DC
                                           limits; round 0.9.3: with the arrays (192) the bound is the window's own
                                           CW_MAX_LAND (S->stats.query_cap): the 15 left floors and ceilings in reach out
                                           of the query (gm_highway14800: 22.6 % of the windows of random play, the
                                           owner's 65 "query took 15 objects" log lines; gm_bigcity 9213 10134: a ceiling
                                           object left out, a jump went through) */
#define CW_QUERY_MARGIN 20.0f
#define CW_QUERY_RADIUS 5.0f            /* r without a player work (the camera's position: no land collision query) */
#define CW_FAR          200.0f          /* objects left out farther than this are not reported (the character's own
                                           probes reach 200 units: the shadow / water probe 8C0925A0) */
/* W1 (round 0.9.1): the water above the character */
#define CW_WATER_ATTR   0x400002u       /* the water probe's COL bits (8C0925A0: lc[i].slAttribute & 0x400002) */
#define CW_WATER_UP     8192.0f         /* water surfaces at most this far above the character */
#define CW_WATER_PROBE  200.0f          /* the water probe's gather radius (8C0925A0 -> 8C0522C0(pos, 200)) */
#define CW_WATER_SLACK  64.0f           /* the widened COL radius reaches this far beyond the character's position at the
                                           refresh: the probe of the next step runs after the character moved */
/* WTR-A (round 0.9.2): the host's water at the character (sa1_input.host_flags) */
#define CW_WATER_DRY_REACH 32.0f        /* KNOWN dry: water polygons this close to the character's vertical line (horizontal
                                           distance) count as over it: the probe of the next step runs after the character
                                           moved (two steps at the game's 16-unit speed cap), so crossing from one GRID
                                           cell's water piece into the next one's never finds the next one in the window */
#define CW_WATER_WET_REACH 1024.0f      /* KNOWN | IN_WATER: W1 widens without its land test up to this far above the
                                           character (4096 HU; the deepest host water measured is 476 units); farther water
                                           keeps the land test (where the host's own water top was cut away over the
                                           character - a dock - the next water above can be a far one: Review R3.3) */

#define CW_FN_DYNCOL_ADD    0x8C0528AEu     /* DynamicCOL_Add(attr, tp, obj) */
#define CW_FN_DYNCOL_REMOVE 0x8C052940u     /* DynamicCOL_Remove(tp, obj) */
#define CW_FN_ATAN2         0x8C10D0F8u     /* atan2f(y, x) */
#define CW_ADDR_DYN_COUNT   0x8C759704u     /* Sint16 B_8C759704 */
#define CW_ADDR_DYN_LIST    0x8C759708u     /* _OBJ_LANDCOLL B_8C759708[256]: attr, object, task */
#define CW_DYN_MAX          256u
#define CW_POOL_HARD        8160u           /* 8C028EC2's polygon pool holds 8192 (overflow leaves empty lists that crash the
                                               probes 8C02D060 / 8C02D2E0); the objects near the character may use it all */

/* the raised NEAR-poly list of the lowered 8C028BFE (sadc src/1st_read/code_8C028BDC.c, SA1_NEARCAP): a pcdata block
   {Uint32 magic[2]; Sint32 cap, full, peak; NEARPOLY *pool[256]} in DC layout (pointers are guest words) */
#define CW_NEARCAP_MAGIC0   0x5041434Eu     /* "NCAP" */
#define CW_NEARCAP_MAGIC1   0x45464238u     /* "8BFE" */
#define CW_NEARCAP_CAP      0x08u
#define CW_NEARCAP_FULL     0x0Cu
#define CW_NEARCAP_PEAK     0x10u
#define CW_NEARCAP_BYTES    (0x14u + 4u * 512u)
#define CW_NEAR_RAISED      512u        /* round 0.9.2 (gsa1_090_collision.md F5; 256 before): SA1_NEARCAP_MAX */
#define CW_NEAR_DC          96u
/* the land-collision arrays of the lowered 8C073160 / 8C0777E0 (sadc src/1st_read/code_8C0730D8.c, code_8C0777E0.c,
   SA1_HITCAP, round 0.9.2: gsa1_090_collision.md F1b): a pcdata block each {Uint32 magic[2]; Sint32 cap, peak, dropped,
   calls; _OBJ_LANDCOLL hits[192], sorted[192]; Uint32 canary} in DC layout */
#define CW_HITCAP_MAGIC0    0x50435448u     /* "HTCP" */
#define CW_HITCAP_MAGIC_A   0x30363137u     /* "7160": 8C073160 */
#define CW_HITCAP_MAGIC_B   0x30453737u     /* "77E0": 8C0777E0 */
#define CW_HITCAP_CAP       0x08u
#define CW_HITCAP_PEAK      0x0Cu
#define CW_HITCAP_DROPPED   0x10u
#define CW_HITCAP_MAX       192u
#define CW_HITCAP_CANARY    (0x18u + 2u * 12u * CW_HITCAP_MAX)
#define CW_HITCAP_BYTES     (CW_HITCAP_CANARY + 4u)
#define CW_HITCAP_WORD      0x5A1CA9A5u     /* the canary: written at attach, never by the game code */
/* front-only polygons of the lowered 8C029B00 (sadc src/1st_read/code_8C029B00.c, SA1_FRONTONLY, round 0.9.3:
   sadc docs/research/gsa1_091b_ceiling.md FIX-C1 with its Review R.5.3): a pcdata block {Uint32 magic[2]; Uint32 on, mask;
   Uint32 lo[2], hi[2]; Uint32 skipped, topmask, topskipped} */
#define CW_FRONTONLY_MAGIC0 0x4C4E4F46u     /* "FONL" */
#define CW_FRONTONLY_MAGIC1 0x30304239u     /* "9B00" */
#define CW_FRONTONLY_ON     0x08u
#define CW_FRONTONLY_MASK   0x0Cu
#define CW_FRONTONLY_LO     0x10u
#define CW_FRONTONLY_HI     0x18u
#define CW_FRONTONLY_SKIPPED 0x20u
#define CW_FRONTONLY_TOPMASK 0x24u
#define CW_FRONTONLY_TOPSKIPPED 0x28u
#define CW_FRONTONLY_BYTES  0x2Cu
#define CW_ADDR_MTX_DEPTH   0x8C88F69Cu     /* B_8C88F69C: Ninja matrix stack depth (1 after njInitMatrix) */
#define CW_ADDR_MTX_PTR     0x8C88F5F8u     /* B_8C88F5F8: the next free slot */
#define CW_ADDR_MTX_BASE    0x8C88F5FCu     /* B_8C88F5FC: the stack base (njInitMatrix) */
#define CW_ADDR_MTX_LIMIT   0x8C88F698u     /* B_8C88F698: slots (128) */

typedef struct cw_ent {
    uint32_t obj;               /* guest NJS_OBJECT */
    uint32_t col;               /* COL word */
    uint32_t ntris;
    uint32_t reg;               /* registered as a DynamicCOL entry */
} cw_ent;

typedef struct cw_slot {
    uint32_t id;                /* sa1_col_object id; 0 = free */
    uint32_t version, mid_version;
    uint32_t tp;                /* proxy task (guest), kept for the next object */
    uint32_t mem, mem_cap;      /* extension memory of the models, kept for the next object */
    uint32_t nent, ntris;
    cw_ent ent[SA1_MAX_OBJECT_GROUPS];
    float pos[3];               /* the transform in the arena */
    int32_t ang[3];
} cw_slot;

typedef struct cw_cand {
    float d;                    /* distance of the object's box from the character (0: inside; without boxes: of the COL
                                   sphere, negative inside) */
    uint32_t idx;               /* COL index */
    float q;                    /* |c - p| - COL radius: the gathers take the object when it is below their radius */
} cw_cand;

/* a contact of player 0 with a moving object in one step (SA1 space; sa1_contact without the conversion) */
#define CW_CONTACTS     128u            /* kept per world until polled */
#define CW_CT_TOL       1.0f            /* SA1 units beyond the collision sphere that still count as touching */
#define CW_CT_LAND      0.47f           /* normal y of a landing (cos of the game's landing slope 0x2C00) */
typedef struct cw_contact {
    uint32_t object, type, flags, step;
    float point[3], normal[3], v0[3], v1[3], depth;
} cw_contact;

typedef struct cw_state {
    uint32_t magic;
    uint32_t self;              /* guest address of this state */
    sa1_context *ctx;
    uint32_t grid;              /* GRID header (guest; 0 = none) */
    uint32_t hdr, cols, cap;    /* window landtable */
    uint32_t win_objects, win_polys;
    uint32_t cap_active;        /* SA1_CAPACITY_* situations of the last step (bit n = id n) */
    uint32_t nearcap;           /* guest address of the raised NEAR-poly block (0: none) */
    uint32_t near_full;         /* its `full` counter at the last refresh */
    uint32_t hitcap[2];         /* guest addresses of the SA1_HITCAP blocks of 8C073160 / 8C0777E0 (0: none) */
    uint32_t hit_dropped[2];    /* their `dropped` counters at the last refresh */
    uint32_t frontonly;         /* guest address of the SA1_FRONTONLY block of 8C029B00 (0: none) */
    uint32_t mx_pending;        /* sa1ca_world_step_begin restored the matrix stack in this step */
    sa1ca_world_stats stats;
    cw_slot slot[CW_SLOTS];
    cw_cand *cand, *tmp;        /* GRID worlds: CW_CAND each (extension segment) */
    /* contacts with the moving objects (API 0.3, sa1_character_contacts) */
    float ct_v0[3];             /* player 0's motionwk.spd at the start of the step (sa1ca_world_step_begin) */
    uint64_t ct_prev;           /* slots in contact in the step before */
    uint32_t ct_head, ct_count, ct_dropped;
    cw_contact ct[CW_CONTACTS];
    /* W1 (round 0.9.1): the water above the character kept in the window (sa1ca_world_water) */
    sa1g_water_stats water;
    /* round 0.9.5 (the board, gsa1_095_plan.md 6.2 task 3 with its Review R.1 / R.4): the GRID's board bits (guest: one byte
       per COL entry, sa1ca_built.board; 0: none) */
    uint32_t board;
} cw_state;

/* round 0.9.5 (the board): the land word a COL entry / a moving group has in this world while its player rides
   (w->board_col, src/world/sa1_world_board.c): SA1_BOARD_TERRAIN_SNOW / SAND replace the class bits 0x9C of every land word
   (attr & 1; not water 0x400002) by 0x8 / 0x10 (Ice Cap's snow, Sand Hill's sand); TERRAIN_MAP ORs 0x8 / 0x10 into the
   words of the objects whose board bits say board snow / sand (SA1_SURFACE_BOARD_*; review R.1: per object, never per
   triangle). On foot, or in a world without the lane: the word unchanged */
static uint32_t cw_board_word(const sa1g_world *w, uint32_t col, uint32_t board)
{
    uint32_t bc = w->board_col;
    if (!(bc & SA1WB_COL_RIDING) || !(col & 0x1u) || (col & 0x400002u)) {
        return col;
    }
    switch (bc & 0xFu) {
    case SA1_BOARD_TERRAIN_SNOW:
        return (col & ~0x9Cu) | 0x8u;
    case SA1_BOARD_TERRAIN_SAND:
        return (col & ~0x9Cu) | 0x10u;
    default:
        return col | ((board & 1u) ? 0x8u : 0u) | ((board & 2u) ? 0x10u : 0u);
    }
}

/* a window entry of the step (cw_window: the W1 pass gives up the farthest ones for the water above the character) */
typedef struct cw_sel {
    uint32_t idx;               /* COL index */
    uint32_t np;                /* its triangles */
    float d;                    /* its rank distance (the candidate's d) */
} cw_sel;

/* ---- guest memory --------------------------------------------------------------------------------- */
static uint32_t cw_rd32(const void *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void cw_wr32(void *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static float cw_rdf(const void *p)
{
    float v;
    memcpy(&v, p, 4);
    return v;
}

static void cw_wrf(void *p, float v)
{
    memcpy(p, &v, 4);
}

static void cw_wr16(void *p, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(p, &h, 2);
}

static void *cw_ptr(const sa1g_world *w, uint32_t g)
{
    return sa1g_world_ptr(w, g);
}

static void *cw_fn(uint32_t dc)
{
    return sa1_fn(SA1_GFN_C(dc), 0u);
}

/* floor of a float without the C library (|x| < 2^31) */
static int32_t cw_floor(float x)
{
    int32_t i = (int32_t)x;
    return (float)i > x ? i - 1 : i;
}

/* ---- the state ------------------------------------------------------------------------------------ */
static cw_state *cw_state_of(const sa1g_world *w)
{
    uint8_t *hdr = NULL;
    cw_state *S;
    uint32_t g;
    if (w == NULL || w->ext == NULL || (w->land_mode != SA1G_LAND_EMPTY && w->land_mode != SA1G_LAND_WINDOW)) {
        return NULL;
    }
    if (sa1g_world_window((sa1g_world *)(uintptr_t)w, (void **)&hdr, NULL, NULL) != 0 || hdr == NULL) {
        return NULL;
    }
    if (cw_rd32(hdr + CW_ANCHOR_MAGIC) != CW_MAGIC) {
        return NULL;
    }
    g = cw_rd32(hdr + CW_ANCHOR_STATE);
    if (((g >> 24) & 31u) != SA1G_SEG_EXT || (size_t)(g & 0xFFFFFFu) + sizeof(cw_state) > w->ext_used) {
        return NULL;
    }
    S = (cw_state *)(void *)(w->ext + (g & 0xFFFFFFu));
    return S->magic == CW_MAGIC && S->self == g ? S : NULL;
}

static void cw_capacity(sa1g_world *w, cw_state *S, uint32_t *active, uint32_t id, uint32_t count, const float *p)
{
    *active |= 1u << id;
    if (!(S->cap_active & (1u << id))) {
        float z[3] = { 0.0f, 0.0f, 0.0f };
        sa1g_world_event_push(w, SA1_EVENT_CAPACITY, id, 0u, p ? p : z, count);
    }
}

/* ---- GRID synthesis into the collision segment (outside ticks) ------------------------------------ */
static uint32_t cw_colseg_alloc(void *user, size_t n, size_t align, void **host)
{
    return sa1g_colseg_alloc((sa1g_shared *)user, n, align, host);
}

static int cw_grid_valid(const sa1g_shared *sh, uint32_t grid, uint32_t generation, uint32_t token)
{
    const uint8_t *h;
    size_t off;
    if (sh == NULL || sh->col == NULL || grid < SA1G_COL_GUEST) {
        return 0;
    }
    off = (size_t)(grid - SA1G_COL_GUEST);
    if (off + SA1CA_GRID_HDR_SIZE > sh->col_used) {
        return 0;
    }
    h = sh->col + off;
    return cw_rd32(h + SA1CA_GH_MAGIC) == SA1CA_GRID_MAGIC && cw_rd32(h + SA1CA_GH_GENERATION) == generation
           && cw_rd32(h + SA1CA_GH_TOKEN) == token;
}

static sa1_status cw_grid_build(sa1_context *ctx, sa1g_shared *sh, sa1ca_static *s, uint32_t *grid)
{
    sa1ca_built b;
    sa1_status st;
    *grid = 0;
    if (sh == NULL || sh->col == NULL) {
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "collision adapter: the world has no collision segment");
    }
    if (s->built.grid != 0 && s->built.generation == s->generation
        && cw_grid_valid(sh, s->built.grid, s->generation, s->built.token)) {
        *grid = s->built.grid;
        return SA1_OK;
    }
    if (sa1g_colseg_seal(sh, 0) != 0) {
        return sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "collision adapter: the collision segment cannot be made writable");
    }
    memset(&b, 0, sizeof b);
    st = sa1ca_build(ctx, cw_colseg_alloc, sh, &b);
    if (sa1g_colseg_seal(sh, 1) != 0 && st == SA1_OK) {
        st = sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "collision adapter: the collision segment cannot be sealed");
    }
    if (st != SA1_OK) {
        return st;
    }
    *grid = b.grid;
    return SA1_OK;
}

/* inside the world: the world's death zones are the GRID kill planes */
static void cw_attach_kill(sa1g_world *w, void *user)
{
    cw_state *S = (cw_state *)user;
    const uint8_t *gh = (const uint8_t *)cw_ptr(w, S->grid);
    void *(*create)(uint16_t, int32_t, sa1_gfn) = (void *(*)(uint16_t, int32_t, sa1_gfn))cw_fn(SA1G_FN_CREATE_TASK);
    uint16_t level = 0;
    memcpy(&level, cw_ptr(w, SA1G_ADDR_CUR_LEVEL), 2);
    if ((level & 0xFFu) >= SA1CA_LEVELS) {
        sa1g_fatal(0, "collision adapter: kill planes need a stage number below 43 (DeathZoneTable)");
    }
    /* 8C07BAC0: DeathZoneTable[(GetStageNumber() & 0xFF00) >> 8][act] */
    if (sa1g_world_wr32(w, SA1CA_DEATHZONE_TABLE + 4u * (uint32_t)(level & 0xFFu),
                        cw_rd32(gh + SA1CA_GH_KILL_ACTS)) != 0) {
        sa1g_fatal(0, "collision adapter: DeathZoneTable outside the arena");
    }
    if (create(2, 1, SA1_GFN_C(SA1CA_FN_DEATHZONE)) == NULL) {
        sa1g_fatal(SA1G_FN_CREATE_TASK, "collision adapter: CreateElementalTask failed for the death zone task");
    }
    S->stats.kill_objects = cw_rd32(gh + SA1CA_GH_KILLS);
}

static void cw_step(sa1g_world *w, void *user);

/* a block of the lowered game code in the world's pcdata (the start of segment 0x18), found by its magic words; 0 if the
 * lowered game code has none (a build without the C file, or the harness's) */
static uint32_t cw_pcdata_find(const sa1g_world *w, uint32_t magic0, uint32_t magic1, uint32_t bytes)
{
    size_t off, end = w->pcdata_size;
    if (w->ext == NULL || end > w->ext_used || end < bytes) {
        return 0;
    }
    for (off = 0; off + bytes <= end; off += 4u) {
        const uint8_t *p = w->ext + off;
        if (cw_rd32(p) == magic0 && cw_rd32(p + 4) == magic1) {
            return SA1G_PCDATA_GUEST + (uint32_t)off;
        }
    }
    return 0;
}

/* the raised NEAR-poly block (sadc src/1st_read/code_8C028BDC.c SA1_NEARCAP) */
static uint32_t cw_nearcap_find(const sa1g_world *w)
{
    return cw_pcdata_find(w, CW_NEARCAP_MAGIC0, CW_NEARCAP_MAGIC1, CW_NEARCAP_BYTES);
}

/* host worlds: the land-collision arrays of 8C073160 / 8C0777E0 (SA1_HITCAP) get their canary; the adapter reads their
 * counters after every step (cw_hitcap_step) */
static void cw_hitcap_attach(sa1g_world *w, cw_state *S)
{
    static const uint32_t magic[2] = { CW_HITCAP_MAGIC_A, CW_HITCAP_MAGIC_B };
    uint32_t k;
    for (k = 0; k < 2u; k++) {
        uint8_t *p;
        S->hitcap[k] = cw_pcdata_find(w, CW_HITCAP_MAGIC0, magic[k], CW_HITCAP_BYTES);
        if (S->hitcap[k] == 0) {
            continue;
        }
        p = (uint8_t *)cw_ptr(w, S->hitcap[k]);
        if (cw_rd32(p + CW_HITCAP_CAP) != CW_HITCAP_MAX) {
            S->hitcap[k] = 0;       /* not the layout this adapter knows */
            continue;
        }
        cw_wr32(p + CW_HITCAP_PEAK, 0u);
        cw_wr32(p + CW_HITCAP_CANARY, CW_HITCAP_WORD);
        S->hit_dropped[k] = cw_rd32(p + CW_HITCAP_DROPPED);
        S->stats.hits_cap = CW_HITCAP_MAX;
    }
}

/* after the step: the most objects a land-collision call collected, the objects left out, the canary */
static void cw_hitcap_step(sa1g_world *w, cw_state *S, uint32_t *active, const float *p)
{
    uint32_t k, peak = 0, dropped = 0;
    for (k = 0; k < 2u; k++) {
        uint8_t *b;
        uint32_t v;
        if (S->hitcap[k] == 0) {
            continue;
        }
        b = (uint8_t *)cw_ptr(w, S->hitcap[k]);
        v = cw_rd32(b + CW_HITCAP_PEAK);
        peak = v > peak ? v : peak;
        cw_wr32(b + CW_HITCAP_PEAK, 0u);
        v = cw_rd32(b + CW_HITCAP_DROPPED);
        dropped += v - S->hit_dropped[k];
        S->hit_dropped[k] = v;
        if (cw_rd32(b + CW_HITCAP_CANARY) != CW_HITCAP_WORD) {
            S->stats.hits_canary++;
            cw_wr32(b + CW_HITCAP_CANARY, CW_HITCAP_WORD);
        }
    }
    S->stats.hits_peak = peak;
    if (peak > S->stats.hits_max) {
        S->stats.hits_max = peak;
    }
    if (dropped != 0) {
        S->stats.hits_dropped += dropped;
        cw_capacity(w, S, active, SA1_CAPACITY_LAND_HITS, dropped, p);
    }
}

/* host worlds (round 0.9.3, FIX-C1): the sphere test skips a front-only polygon (SA1_MAT_FRONT_ONLY) of libsa1's own land
 * objects - the GRID objects in the static collision segment, the moving objects' models in the extension segment - when
 * the sphere starts behind it; the game's own land objects (arena) keep the game's rule */
static void cw_frontonly_attach(sa1g_world *w, cw_state *S)
{
    uint8_t *p;
    S->frontonly = cw_pcdata_find(w, CW_FRONTONLY_MAGIC0, CW_FRONTONLY_MAGIC1, CW_FRONTONLY_BYTES);
    if (S->frontonly == 0) {
        return;     /* lowered game code without the block (before round 0.9.3): the game's rule */
    }
    p = (uint8_t *)cw_ptr(w, S->frontonly);
    cw_wr32(p + CW_FRONTONLY_MASK, SA1_MAT_FRONT_ONLY);
    cw_wr32(p + CW_FRONTONLY_TOPMASK, SA1CA_MAT_THIN_TOP);
    cw_wr32(p + CW_FRONTONLY_LO, SA1G_COL_GUEST);
    cw_wr32(p + CW_FRONTONLY_HI, SA1G_COL_GUEST + SA1G_COL_SIZE);
    cw_wr32(p + CW_FRONTONLY_LO + 4u, SA1G_EXT_GUEST);
    cw_wr32(p + CW_FRONTONLY_HI + 4u, SA1G_EXT_GUEST + SA1G_EXT_SIZE);
    cw_wr32(p + CW_FRONTONLY_ON, 1u);
    S->stats.front_only_on = 1;
}

/* host worlds: the raised list (512) unless the geometry keeps the DC limits */
static void cw_nearcap_attach(sa1g_world *w, cw_state *S, int dc_limits)
{
    uint8_t *p;
    S->nearcap = cw_nearcap_find(w);
    S->stats.near_cap = CW_NEAR_DC;
    if (S->nearcap == 0) {
        return;
    }
    p = (uint8_t *)cw_ptr(w, S->nearcap);
    cw_wr32(p + CW_NEARCAP_CAP, dc_limits ? CW_NEAR_DC : CW_NEAR_RAISED);
    S->near_full = cw_rd32(p + CW_NEARCAP_FULL);
    cw_wr32(p + CW_NEARCAP_PEAK, 0u);
    S->stats.near_cap = dc_limits ? CW_NEAR_DC : CW_NEAR_RAISED;
}

sa1_status sa1ca_world_attach(sa1_context *ctx, struct sa1g_world *w, uint32_t *notes)
{
    sa1ca_static *s;
    uint8_t *hdr = NULL;
    void *cols = NULL;
    uint32_t cap = 0, grid = 0, g;
    cw_state *S = NULL;
    sa1_status st;
    uint32_t board = 0;         /* round 0.9.5: the GRID's board bits (sa1ca_built.board) */
    if (ctx == NULL || w == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    if (!w->booted || (w->land_mode != SA1G_LAND_EMPTY && w->land_mode != SA1G_LAND_WINDOW)) {
        return SA1_OK;      /* DISC / FAITHFUL / transplants: the landtable is used in place */
    }
    if (sa1g_world_window(w, (void **)&hdr, &cols, &cap) != 0 || cw_state_of(w) != NULL) {
        return SA1_OK;
    }
    s = sa1ca_static_of(ctx);
    if (sa1ca_prepared_of(s) != NULL) {
        /* sa1_collision_set_prepared: the cells are in the prepared segment, which is this world's (w->shared) */
        const struct sa1_collision_prepared *prep = sa1ca_prepared_of(s);
        if (prep->mode == SA1_COLLISION_MODE_GRID && prep->built.grid != 0) {
            if (w->shared != prep->seg || !cw_grid_valid(w->shared, prep->built.grid, prep->generation, prep->built.token)) {
                return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT,
                                "collision adapter: the world is not on the prepared collision segment");
            }
            grid = prep->built.grid;
            board = prep->built.board;
        }
    } else if (s != NULL && s->mode == SA1_COLLISION_MODE_GRID && (s->num_flat != 0 || s->num_groups != 0)) {
        st = cw_grid_build(ctx, w->shared, s, &grid);
        if (st != SA1_OK) {
            return st;
        }
        board = grid != 0 && grid == s->built.grid ? s->built.board : 0u;
    }
    g = sa1g_world_ext_alloc(w, sizeof(cw_state), 16, (void **)&S);
    if (g == 0 || S == NULL) {
        return sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "collision adapter: no room for the adapter state in segment 0x18");
    }
    memset(S, 0, sizeof *S);
    S->magic = CW_MAGIC;
    S->self = g;
    S->ctx = ctx;
    S->grid = grid;
    S->board = board;
    S->hdr = w->landtable;
    S->cols = w->window_cols;
    S->cap = cap;
    S->win_objects = 128u;
    S->win_polys = 7680u;
    S->water.last_index = 0xFFFFFFFFu;
    if (grid != 0) {
        const uint8_t *gh = (const uint8_t *)sa1g_world_ptr(w, grid);
        S->win_objects = cw_rd32(gh + SA1CA_GH_WIN_OBJECTS);
        S->win_polys = cw_rd32(gh + SA1CA_GH_WIN_POLYS);
    }
    if (S->win_objects > 128u) {
        S->win_objects = 128u;
    }
    if (S->win_objects > cap) {
        S->win_objects = cap;
    }
    if (grid != 0) {
        void *cb = NULL;
        if (sa1g_world_ext_alloc(w, 2u * CW_CAND * sizeof(cw_cand), 16, &cb) == 0 || cb == NULL) {
            return sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "collision adapter: no room for the window buffers");
        }
        S->cand = (cw_cand *)cb;
        S->tmp = S->cand + CW_CAND;
    }
    S->stats.attached = 1;
    S->stats.grid = grid;
    {
        int dc_limits = grid != 0 && (cw_rd32((const uint8_t *)sa1g_world_ptr(w, grid) + SA1CA_GH_FLAGS)
                                      & SA1_GRID_DC_LIMITS) != 0;
        cw_nearcap_attach(w, S, dc_limits);
        cw_hitcap_attach(w, S);
        /* the query bound (round 0.9.3): none below the window's own when both land-collision calls hold 192 */
        S->stats.query_cap = (S->hitcap[0] != 0 && S->hitcap[1] != 0 && !dc_limits) ? CW_MAX_LAND : CW_QUERY_MAX;
    }
    cw_frontonly_attach(w, S);
    {
        uint32_t depth = 0;
        sa1g_world_rd32(w, CW_ADDR_MTX_DEPTH, &depth);
        S->stats.matrix_base = depth;
    }
    cw_wr32(hdr + CW_ANCHOR_MAGIC, CW_MAGIC);
    cw_wr32(hdr + CW_ANCHOR_STATE, g);
    if (grid != 0 && cw_rd32((const uint8_t *)sa1g_world_ptr(w, grid) + SA1CA_GH_KILLS) != 0) {
        st = sa1g_world_call(w, cw_attach_kill, S);
        if (st != SA1_OK) {
            return sa1_fail(ctx, st, "collision adapter: kill planes: %s", w->fault.msg);
        }
    }
    w->adapter_fn = cw_step;
    if (grid != 0) {
        w->land_mode = SA1G_LAND_WINDOW;
        if (notes != NULL) {
            *notes &= ~(uint32_t)SA1W_NOTE_GRID_PENDING;
        }
    }
    return SA1_OK;
}

/* ---- diagnostics: the NEAR-poly list (sa1_debug_near_polys, G-SA1 request 12) ----------------------------------- */
#define CW_ADDR_NEAR_COUNT  0x8C73E27Cu     /* B_8C73E27C: entries of the last query */
#define CW_ADDR_NEAR_DC     0x8C73E280u     /* B_8C73E280[96]: the game's list */
#define CW_ADDR_NEAR_LISTS  0x8C02D548u     /* D_8C02D548: the land objects' polygon lists (next +0, object +8, near +0x1C,
                                               near_num +0x24) */

/* a guest word of the arena or of the world's extension segment (0 outside) */
static uint32_t cw_peek32(const sa1g_world *w, uint32_t g)
{
    uint32_t seg = (g >> 24) & 31u, off = g & 0xFFFFFFu;
    if (g == 0 || (seg == SA1G_SEG_ARENA && off <= SA1G_ARENA_SIZE - 4u)
        || (seg == SA1G_SEG_EXT && (size_t)off + 4u <= w->ext_used)) {
        return g != 0 ? cw_rd32(cw_ptr(w, g)) : 0u;
    }
    return 0;
}

sa1_status sa1w_world_near(const sa1_context *ctx, const struct sa1g_world *w, sa1_near_info *info, sa1_near_poly *out,
                           uint32_t capacity, uint32_t *count)
{
    sa1_near_info ni;
    const cw_state *S;
    uint32_t block, list, n, i, written = 0;
    memset(&ni, 0, sizeof ni);
    ni.struct_size = sizeof ni;
    if (count != NULL) {
        *count = 0;
    }
    if (w == NULL || w->ram == NULL || w->dead) {
        return SA1_ERR_NOT_LOADED;
    }
    block = cw_nearcap_find(w);
    S = cw_state_of(w);
    list = CW_ADDR_NEAR_DC;
    ni.capacity = CW_NEAR_DC;
    ni.block = block;
    if (block != 0) {
        uint32_t cap = cw_peek32(w, block + CW_NEARCAP_CAP);
        if (cap > CW_NEAR_DC) {
            list = block + 0x14u;
            ni.capacity = cap < CW_NEAR_RAISED ? cap : CW_NEAR_RAISED;
        }
        ni.full = cw_peek32(w, block + CW_NEARCAP_FULL);
        ni.peak = cw_peek32(w, block + CW_NEARCAP_PEAK);
    }
    if (S != NULL) {
        /* host worlds: the adapter takes the block's peak at each refresh (cw_step) */
        ni.peak = S->stats.near_peak > ni.peak ? S->stats.near_peak : ni.peak;
        ni.peak_life = S->stats.near_max > ni.peak ? S->stats.near_max : ni.peak;
    }
    n = cw_peek32(w, CW_ADDR_NEAR_COUNT);
    ni.count = n < ni.capacity ? n : ni.capacity;
    ni.list = list;
    ni.count_address = CW_ADDR_NEAR_COUNT;
    for (i = 0; out != NULL && i < ni.count && written < capacity; i++) {
        uint32_t pg = cw_peek32(w, list + 4u * i), lp, k, guard;
        sa1_near_poly *q = &out[written];
        float c[3], v[3], rr;
        memset(q, 0, sizeof *q);
        q->guest = pg;
        if (pg == 0 || ((pg >> 24) & 31u) != SA1G_SEG_ARENA) {
            written++;
            continue;
        }
        for (k = 0; k < 3; k++) {
            uint32_t a = cw_peek32(w, pg + 0x08u + 4u * k);
            memcpy(&c[k], &a, 4);
        }
        {
            uint32_t a = cw_peek32(w, pg + 0x14u);
            memcpy(&rr, &a, 4);
        }
        sa1_to_host_point(ctx, c, q->center);
        q->radius = (rr > 0.0f ? sa1g_sqrtf(rr) : 0.0f) * ctx->coords.scale;
        for (k = 0; k < 3; k++) {
            uint32_t j;
            for (j = 0; j < 3; j++) {
                uint32_t a = cw_peek32(w, pg + 0x18u + 12u * k + 4u * j);
                memcpy(&v[j], &a, 4);
            }
            sa1_to_host_point(ctx, v, q->v[k]);
        }
        q->attr = cw_peek32(w, pg + 0x3Cu);
        /* the land object whose polygon list put this entry in: [near, near + near_num) of its list */
        for (lp = cw_peek32(w, CW_ADDR_NEAR_LISTS), guard = 0; lp != 0 && guard < 4096u; lp = cw_peek32(w, lp), guard++) {
            uint32_t first = cw_peek32(w, lp + 0x1Cu), num = cw_peek32(w, lp + 0x24u), at = list + 4u * i;
            if (first != 0 && at >= first && at < first + 4u * num) {
                q->object = cw_peek32(w, lp + 0x08u);
                break;
            }
        }
        written++;
    }
    if (count != NULL) {
        *count = written;
    }
    if (info != NULL) {
        sa1_copy_out(info, &ni, sizeof ni);
    }
    return SA1_OK;
}

void sa1ca_world_stats_get(const struct sa1g_world *w, sa1ca_world_stats *out)
{
    const cw_state *S = cw_state_of(w);
    memset(out, 0, sizeof *out);
    if (S != NULL) {
        *out = S->stats;
        if (S->frontonly != 0) {
            out->front_only_skipped = cw_peek32(w, S->frontonly + CW_FRONTONLY_SKIPPED);
            out->thin_top_skipped = cw_peek32(w, S->frontonly + CW_FRONTONLY_TOPSKIPPED);
        }
    }
}

/* round 0.9.2 (sa1_state_ex.water_attr / water_level): playerwk.shadow.Attr_top (+0x1E4) and y_top (+0x1EC) of player 0 */
int sa1ca_world_player_water(const struct sa1g_world *w, uint32_t *attr, float *y_top)
{
    sa1g_player_ptrs pp;
    uint32_t a = 0, y = 0;
    *attr = 0;
    *y_top = 0.0f;
    if (w == NULL || w->ram == NULL || w->dead) {
        return 0;
    }
    sa1g_world_player(w, &pp);
    if (pp.pwp == 0 || sa1g_world_rd32(w, pp.pwp + 0x1E4u, &a) != 0 || sa1g_world_rd32(w, pp.pwp + 0x1ECu, &y) != 0) {
        return 0;
    }
    *attr = a;
    memcpy(y_top, &y, 4);
    return 1;
}

void sa1ca_world_water(const sa1g_world *w, sa1g_water_stats *out)
{
    const cw_state *S = cw_state_of(w);
    memset(out, 0, sizeof *out);
    out->last_index = 0xFFFFFFFFu;
    if (S != NULL) {
        *out = S->water;
    }
}

/* ---- moving objects (5.5) ------------------------------------------------------------------------- */
/* R = Rz(ang[2]) * Ry(ang[1]) * Rx(ang[0]) (column vectors, row-major), as the game's njRotateZ/Y/X build it */
static void cw_rot(const int32_t ang[3], float m[9])
{
    float sx, cx, sy, cy, sz, cz;
    sa1g_fsca(ang[0], &sx, &cx);
    sa1g_fsca(ang[1], &sy, &cy);
    sa1g_fsca(ang[2], &sz, &cz);
    m[0] = cz * cy;
    m[1] = cz * sy * sx - sz * cx;
    m[2] = cz * sy * cx + sz * sx;
    m[3] = sz * cy;
    m[4] = sz * sy * sx + cz * cx;
    m[5] = sz * sy * cx - cz * sx;
    m[6] = -sy;
    m[7] = cy * sx;
    m[8] = cy * cx;
}

/* out = R1 * R0^T * v */
static void cw_rerot(const float r0[9], const float r1[9], const float v[3], float out[3])
{
    float l[3];
    int k;
    for (k = 0; k < 3; k++) {
        l[k] = r0[k] * v[0] + r0[3 + k] * v[1] + r0[6 + k] * v[2];
    }
    for (k = 0; k < 3; k++) {
        out[k] = r1[3 * k] * l[0] + r1[3 * k + 1] * l[1] + r1[3 * k + 2] * l[2];
    }
}

static int32_t cw_bams_of(float y, float x)
{
    float (*at2)(float, float) = (float (*)(float, float))cw_fn(CW_FN_ATAN2);
    float a = at2(y, x) * 10430.381f;
    return (int32_t)a;
}

static uint32_t cw_task_word(const sa1g_world *w, uint32_t tp, uint32_t off)
{
    return cw_rd32((const uint8_t *)cw_ptr(w, tp) + off);
}

/* the transform of a slot into its objects and its proxy's taskwk */
static void cw_slot_put(sa1g_world *w, cw_slot *sl)
{
    uint32_t e, k, twp = cw_task_word(w, sl->tp, 0x20u);
    for (e = 0; e < sl->nent; e++) {
        uint8_t *obj = (uint8_t *)cw_ptr(w, sl->ent[e].obj);
        for (k = 0; k < 3; k++) {
            cw_wrf(obj + 0x08 + 4u * k, sl->pos[k]);
            cw_wr32(obj + 0x14 + 4u * k, (uint32_t)sl->ang[k]);
        }
    }
    if (twp != 0) {
        uint8_t *t = (uint8_t *)cw_ptr(w, twp);
        for (k = 0; k < 3; k++) {
            cw_wr32(t + 0x14 + 4u * k, (uint32_t)sl->ang[k]);
            cw_wrf(t + 0x20 + 4u * k, sl->pos[k]);
        }
    }
}

/* force work fwp[0] (player 0) and fwp[1] (the others) of the proxy */
static void cw_slot_force(sa1g_world *w, const cw_slot *sl, const float spd[3], int32_t angy)
{
    uint32_t fwp = cw_task_word(w, sl->tp, 0x28u), i, k;
    if (fwp == 0) {
        return;
    }
    for (i = 0; i < 2; i++) {
        uint8_t *f = (uint8_t *)cw_ptr(w, fwp + 0x1Cu * i);
        cw_wr32(f + 0x00, 0u);                  /* call_back: none (pos_spd is computed here) */
        cw_wr32(f + 0x04, 0u);
        cw_wr32(f + 0x08, (uint32_t)angy);
        cw_wr32(f + 0x0C, 0u);
        for (k = 0; k < 3; k++) {
            cw_wrf(f + 0x10 + 4u * k, spd[k]);
        }
    }
}

static uint32_t cw_dyn_count(const sa1g_world *w)
{
    int16_t n = 0;
    memcpy(&n, cw_ptr(w, CW_ADDR_DYN_COUNT), 2);
    return n < 0 ? 0u : (uint32_t)n;
}

/* the material of a moving object's triangle: 0 plain, 1 double-sided, 2 front-only (round 0.9.3) */
#define CW_OBJ_MATS 3u
static uint32_t cw_mat_index(uint32_t m)
{
    return (m & SA1_MAT_DOUBLE_SIDE) ? 1u : ((m & SA1_MAT_FRONT_ONLY) ? 2u : 0u);
}

/* meshset runs of one group (materials 0 / double-sided / front-only) */
static uint32_t cw_group_runs(const sa1_col_object *o, const sa1ca_objx *x, uint32_t g)
{
    uint32_t i, runs = 0, last = 0xFFFFFFFFu;
    for (i = 0; i < x->group_count[g]; i++) {
        uint32_t m = cw_mat_index(sa1ca_surface_material(o->tris[x->group_first[g] + i].surface));
        runs += m != last;
        last = m;
    }
    return runs;
}

static uint32_t cw_group_bytes(uint32_t n, uint32_t runs)
{
    uint32_t b = SA1CA_OBJECT_SIZE + SA1CA_MODEL_SIZE + CW_OBJ_MATS * SA1CA_MATERIAL_SIZE + SA1CA_MESHSET_SIZE * runs + 36u * n
                 + ((6u * n + 3u) & ~3u);
    return (b + 15u) & ~15u;
}

/* bytes of the models of an object */
static uint32_t cw_models_bytes(const sa1_col_object *o, const sa1ca_objx *x)
{
    uint32_t g, bytes = 0;
    for (g = 0; g < x->ngroups; g++) {
        bytes += cw_group_bytes(x->group_count[g], cw_group_runs(o, x, g));
    }
    return bytes;
}

/* the models of an object at sl->mem (object space, 3 points per triangle, materials {0, double-sided, front-only}) */
static void cw_models_write(sa1g_world *w, cw_slot *sl, const sa1_col_object *o, const sa1ca_objx *x)
{
    uint32_t g, i, k, off = 0;
    sl->nent = 0;
    sl->ntris = 0;
    for (g = 0; g < x->ngroups; g++) {
        uint32_t n = x->group_count[g], runs = cw_group_runs(o, x, g), last = 0xFFFFFFFFu, base = sl->mem + off;
        uint32_t o_model = base + SA1CA_OBJECT_SIZE, o_mats = o_model + SA1CA_MODEL_SIZE;
        uint32_t o_ms = o_mats + CW_OBJ_MATS * SA1CA_MATERIAL_SIZE, o_pts = o_ms + SA1CA_MESHSET_SIZE * runs;
        uint32_t o_mesh = o_pts + 36u * n, run = 0, r = 0, bytes = cw_group_bytes(n, runs);
        uint8_t *obj = (uint8_t *)cw_ptr(w, base), *model = (uint8_t *)cw_ptr(w, o_model), *ms = NULL;
        memset(obj, 0, bytes);
        cw_wr32(obj + 0x00, 0u);                /* evalflags: the full transform */
        cw_wr32(obj + 0x04, o_model);
        for (k = 0; k < 3; k++) {
            cw_wrf(obj + 0x20 + 4u * k, 1.0f);
        }
        cw_wr32(model + 0x00, o_pts);
        cw_wr32(model + 0x08, 3u * n);
        cw_wr32(model + 0x0C, o_ms);
        cw_wr32(model + 0x10, o_mats);
        cw_wr16(model + 0x14, runs);
        cw_wr16(model + 0x16, CW_OBJ_MATS);
        cw_wrf(model + 0x24, x->radius);
        cw_wr32((uint8_t *)cw_ptr(w, o_mats + SA1CA_MATERIAL_SIZE) + 0x10, SA1_MAT_DOUBLE_SIDE);
        cw_wr32((uint8_t *)cw_ptr(w, o_mats + 2u * SA1CA_MATERIAL_SIZE) + 0x10, SA1_MAT_FRONT_ONLY);
        for (i = 0; i < n; i++) {
            const sa1_collision_tri *t = &o->tris[x->group_first[g] + i];
            uint32_t m = cw_mat_index(sa1ca_surface_material(t->surface));
            uint8_t *p = (uint8_t *)cw_ptr(w, o_pts + 36u * i), *mesh = (uint8_t *)cw_ptr(w, o_mesh + 6u * i);
            for (k = 0; k < 9; k++) {
                cw_wrf(p + 4u * k, t->v[k / 3u][k % 3u]);
            }
            for (k = 0; k < 3; k++) {
                cw_wr16(mesh + 2u * k, 3u * i + k);
            }
            if (m != last || ms == NULL) {
                ms = (uint8_t *)cw_ptr(w, o_ms + SA1CA_MESHSET_SIZE * r);
                cw_wr16(ms, m);                 /* type 0 (triangles), material 0 / 1 / 2 */
                cw_wr32(ms + 4, o_mesh + 6u * i);
                r++;
                run = 0;
                last = m;
            }
            run++;
            cw_wr16(ms + 2, run);
        }
        sl->ent[sl->nent].obj = base;
        sl->ent[sl->nent].col = x->group_col[g];
        sl->ent[sl->nent].ntris = n;
        sl->ent[sl->nent].reg = 0;
        sl->nent++;
        sl->ntris += n;
        off += bytes;
    }
}

/* a new object in a free slot; 0 = no room (slot left free) */
static int cw_slot_open(sa1g_world *w, cw_slot *sl, const sa1_col_object *o, const sa1ca_objx *x, uint32_t *full)
{
    void (*add)(uint32_t, void *, void *) = (void (*)(uint32_t, void *, void *))cw_fn(CW_FN_DYNCOL_ADD);
    uint32_t need = cw_models_bytes(o, x), e, k;
    float zero[3] = { 0.0f, 0.0f, 0.0f };
    if (need > sl->mem_cap) {
        uint32_t g = sa1g_world_ext_alloc(w, need, 16, NULL);
        if (g == 0) {
            *full |= 2u;
            return 0;
        }
        sl->mem = g;
        sl->mem_cap = need;
    }
    if (sl->tp == 0) {
        void *(*create)(uint16_t, int32_t, sa1_gfn) = (void *(*)(uint16_t, int32_t, sa1_gfn))cw_fn(SA1G_FN_CREATE_TASK);
        void *tp = create(6, 0, SA1_GFN_NULL);  /* taskwk + force work, the empty exec 8C098440 */
        if (tp == NULL) {
            *full |= 2u;
            return 0;
        }
        sl->tp = sa1_encode(tp);
    }
    cw_models_write(w, sl, o, x);
    for (k = 0; k < 3; k++) {
        sl->pos[k] = x->pos[k];
        sl->ang[k] = x->ang[k];
    }
    sl->id = o->id;
    sl->version = x->version;
    sl->mid_version = x->version;
    cw_slot_put(w, sl);
    cw_slot_force(w, sl, zero, 0);
    for (e = 0; e < sl->nent; e++) {
        uint32_t before = cw_dyn_count(w);
        if (before >= CW_DYN_MAX) {
            *full |= 4u;
            continue;
        }
        /* round 0.9.5: the riding word (cw_board_word; unchanged on foot); the entry index is the group index */
        add(cw_board_word(w, sl->ent[e].col, x->group_board[e]) | 0x08000000u, cw_ptr(w, sl->tp),
            cw_ptr(w, sl->ent[e].obj));
        sl->ent[e].reg = cw_dyn_count(w) > before;
        if (!sl->ent[e].reg) {
            *full |= 4u;
        }
    }
    return 1;
}

static void cw_slot_close(sa1g_world *w, cw_slot *sl)
{
    void (*rem)(void *, void *) = (void (*)(void *, void *))cw_fn(CW_FN_DYNCOL_REMOVE);
    float zero[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t e = sl->nent;
    while (e-- > 0) {
        if (sl->ent[e].reg) {
            rem(cw_ptr(w, sl->tp), cw_ptr(w, sl->ent[e].obj));
            sl->ent[e].reg = 0;
        }
    }
    cw_slot_force(w, sl, zero, 0);
    sl->id = 0;
    sl->nent = 0;
    sl->ntris = 0;
}

/* the next transform of a slot (half of a new move at sub step 0, all of it at sub 1) and the force work */
static void cw_slot_move(sa1g_world *w, cw_slot *sl, const sa1ca_objx *x, const float *p, int32_t pang_y,
                         uint32_t sub)
{
    float npos[3], spd[3] = { 0.0f, 0.0f, 0.0f };
    int32_t nang[3], angy = 0;
    int moved = 0, k;
    for (k = 0; k < 3; k++) {
        npos[k] = sl->pos[k];
        nang[k] = sl->ang[k];
    }
    if (sl->version != x->version) {
        if (sub == 0 && sl->mid_version != x->version) {
            for (k = 0; k < 3; k++) {
                npos[k] = (sl->pos[k] + x->pos[k]) * 0.5f;
                nang[k] = sl->ang[k] + (int32_t)(int16_t)(uint16_t)((uint32_t)(x->ang[k] - sl->ang[k]) & 0xFFFFu) / 2;
            }
            sl->mid_version = x->version;
        } else {
            for (k = 0; k < 3; k++) {
                npos[k] = x->pos[k];
                nang[k] = x->ang[k];
            }
            sl->version = x->version;
            sl->mid_version = x->version;
        }
        for (k = 0; k < 3; k++) {
            moved |= npos[k] != sl->pos[k] || ((uint32_t)(nang[k] ^ sl->ang[k]) & 0xFFFFu) != 0;
        }
    }
    if (moved && p != NULL) {
        float r0[9], r1[9], d[3], q[3];
        cw_rot(sl->ang, r0);
        cw_rot(nang, r1);
        for (k = 0; k < 3; k++) {
            d[k] = p[k] - sl->pos[k];
        }
        cw_rerot(r0, r1, d, q);
        for (k = 0; k < 3; k++) {
            spd[k] = q[k] + npos[k] - p[k];
        }
        if (((uint32_t)(nang[0] ^ sl->ang[0]) & 0xFFFFu) == 0 && (nang[2] & 0xFFFF) == 0 && (sl->ang[2] & 0xFFFF) == 0) {
            /* a turn about the vertical: the facing (cos a, 0, sin a) of ang.y = a turns by -dy */
            angy = -(int32_t)(int16_t)(uint16_t)((uint32_t)(nang[1] - sl->ang[1]) & 0xFFFFu);
        } else {
            float s, c, f[3], g[3];
            sa1g_fsca(pang_y, &s, &c);
            f[0] = c;
            f[1] = 0.0f;
            f[2] = s;
            cw_rerot(r0, r1, f, g);
            angy = (int32_t)(int16_t)(uint16_t)((uint32_t)(cw_bams_of(g[2], g[0]) - cw_bams_of(f[2], f[0])) & 0xFFFFu);
        }
    }
    if (moved) {
        for (k = 0; k < 3; k++) {
            sl->pos[k] = npos[k];
            sl->ang[k] = nang[k];
        }
        cw_slot_put(w, sl);
    }
    cw_slot_force(w, sl, spd, angy);
}

/* round 0.9.5 (review R.4): DynamicCOL_Add runs only when a slot opens, so at the two edges of a ride (w->board_col
   changed) every open slot of this world is registered again with its new words (DynamicCOL_Remove + Add, as cw_slot_close
   / cw_slot_open do): no prop keeps a riding word after OFF and none misses it at ON */
static void cw_slot_reregister(sa1g_world *w, cw_slot *sl, const sa1ca_objx *x)
{
    void (*add)(uint32_t, void *, void *) = (void (*)(uint32_t, void *, void *))cw_fn(CW_FN_DYNCOL_ADD);
    void (*rem)(void *, void *) = (void (*)(void *, void *))cw_fn(CW_FN_DYNCOL_REMOVE);
    uint32_t e = sl->nent;
    while (e-- > 0) {
        if (sl->ent[e].reg) {
            rem(cw_ptr(w, sl->tp), cw_ptr(w, sl->ent[e].obj));
            sl->ent[e].reg = 0;
        }
    }
    for (e = 0; e < sl->nent; e++) {
        uint32_t before = cw_dyn_count(w);
        if (before >= CW_DYN_MAX) {
            continue;
        }
        add(cw_board_word(w, sl->ent[e].col, x->group_board[e]) | 0x08000000u, cw_ptr(w, sl->tp),
            cw_ptr(w, sl->ent[e].obj));
        sl->ent[e].reg = cw_dyn_count(w) > before;
    }
}

static void cw_moving(sa1g_world *w, cw_state *S, const float *p, int32_t pang_y, uint32_t sub, uint32_t *active)
{
    const sa1_context *ctx = S->ctx;
    const sa1_col_object *objs = SA1_VEC(ctx->col_objects, sa1_col_object);
    uint64_t seen = 0;
    uint32_t n = ctx->col_objects.count, i, j, over = 0, full = 0, ents = 0, moving = 0;
    int reword = w->board_col != w->board_col_applied;     /* round 0.9.5: an edge of a ride */
    w->board_col_applied = w->board_col;
    for (i = 0; i < n; i++) {
        const sa1_col_object *o = &objs[i];
        const sa1ca_objx *x = sa1ca_objx_of(o);
        cw_slot *sl = NULL;
        if (x == NULL || x->ngroups == 0) {
            continue;
        }
        for (j = 0; j < CW_SLOTS; j++) {
            if (S->slot[j].id == o->id) {
                sl = &S->slot[j];
                break;
            }
        }
        if (sl == NULL) {
            for (j = 0; j < CW_SLOTS; j++) {
                if (S->slot[j].id == 0 && !(seen & ((uint64_t)1 << j))) {
                    sl = &S->slot[j];
                    break;
                }
            }
            if (sl == NULL || !cw_slot_open(w, sl, o, x, &full)) {
                over++;
                continue;
            }
        } else if (reword) {
            cw_slot_reregister(w, sl, x);
        }
        seen |= (uint64_t)1 << (uint32_t)(sl - S->slot);
        cw_slot_move(w, sl, x, p, pang_y, sub);
    }
    for (j = 0; j < CW_SLOTS; j++) {
        cw_slot *sl = &S->slot[j];
        if (sl->id != 0 && !(seen & ((uint64_t)1 << j))) {
            cw_slot_close(w, sl);
        }
        if (sl->id != 0) {
            moving++;
            for (i = 0; i < sl->nent; i++) {
                ents += sl->ent[i].reg;
            }
        }
    }
    S->stats.moving = moving;
    S->stats.moving_entries = ents;
    if (over || full) {
        S->stats.over_moving++;
        cw_capacity(w, S, active, SA1_CAPACITY_MOVING_OBJECTS, over, p);
    }
}

/* ---- the window (5.4) ----------------------------------------------------------------------------- */
static int cw_cand_less(const cw_cand *a, const cw_cand *b)
{
    return a->d < b->d || (a->d == b->d && a->idx < b->idx);
}

/* stable bottom-up merge sort of S->cand[0..n) */
static void cw_sort(cw_state *S, uint32_t n)
{
    cw_cand *src = S->cand, *dst = S->tmp, *t;
    uint32_t width, i;
    for (width = 1; width < n; width *= 2u) {
        for (i = 0; i < n; i += 2u * width) {
            uint32_t a = i, am = i + width < n ? i + width : n, b = am, bm = i + 2u * width < n ? i + 2u * width : n;
            uint32_t k = i;
            while (a < am && b < bm) {
                dst[k++] = cw_cand_less(&src[b], &src[a]) ? src[b++] : src[a++];
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

/* triangles of a model (type-0 / quad / strip meshsets; strips counted generously) */
static uint32_t cw_model_tris(const sa1g_world *w, uint32_t model)
{
    const uint8_t *m = (const uint8_t *)cw_ptr(w, model);
    uint32_t ms = cw_rd32(m + 0x0C), nms, i, n = 0;
    uint16_t h = 0;
    memcpy(&h, m + 0x14, 2);
    nms = h;
    for (i = 0; i < nms && i < 1024u; i++) {
        const uint8_t *s = (const uint8_t *)cw_ptr(w, ms + SA1CA_MESHSET_SIZE * i);
        uint16_t type = 0, nb = 0;
        memcpy(&type, s, 2);
        memcpy(&nb, s + 2, 2);
        n += (type & 0xC000u) == 0 ? nb : (type & 0xC000u) == 0x4000u ? 2u * nb : 64u * nb;
    }
    return n;
}

/* the COL entries of a GRID column (the builder's hash, as the window's gather below); 0 when the column is empty */
static uint32_t cw_column(const uint8_t *hash, uint32_t hsize, int32_t ix, int32_t iz, uint32_t *first)
{
    uint32_t h, probes = 0;
    *first = 0;
    for (h = sa1ca_grid_hash(ix, iz) & (hsize - 1u); probes < hsize; h = (h + 1u) & (hsize - 1u), probes++) {
        const uint8_t *he = hash + SA1CA_GRID_HASH_ENTRY * h;
        uint32_t cnt = cw_rd32(he + 12);
        if (cnt == 0) {
            return 0;
        }
        if ((int32_t)cw_rd32(he) == ix && (int32_t)cw_rd32(he + 4) == iz) {
            *first = cw_rd32(he + 8);
            return cnt;
        }
    }
    return 0;
}

/*
 * W1 (round 0.9.1; sadc docs/research/gsa1_070_drown.md W1 with its Review R4.1 / R4.10): the water above the character.
 * The game finds the water level above the character with the water probe 8C0925A0: 8C0522C0(pos, 200) gathers the land
 * objects whose COL sphere comes within 200 units (|c - p| - 200 < xWidth, code_8C0522C0.c), out of the window entries
 * 8C052518 copied (in window order, at most 128: code_8C052518.c), and keeps the lowest water surface above. GRID cuts host
 * water into objects of one 256-unit cell (COL radius about 200-260), so below about 390-440 units of host water the water
 * object is out of that reach: no air timer, no water physics (the game's own stage water comes in objects big enough
 * that this never bites). The pass finds the lowest object with the water bits in the 3 x 3 columns around the character
 * whose box covers the character's x / z and lies above it (at most CW_WATER_UP), puts it into the window when it is not
 * there (at an index the game copies: the window holds at most cap_obj <= 128 entries; when it is full the farthest
 * entries go first, never the nearest one or one within 40 units; its triangles count against the whole pool, like the
 * object under the character) and gives its window copy the COL radius |c - p| - 200 + CW_WATER_SLACK when that is more,
 * so that both gathers take it. Host adaptation in the window only (the window copy is rebuilt every step), not game code;
 * E-102 is left alone (his main has no water probe, and 8C073160 takes water as land for him). Land between the character
 * and that water (any land triangle the vertical line through the character crosses between the probe's start, 1 above
 * the character, and the water: a cave or a room under a lake; GRID land and, since round 0.9.2, the moving objects'
 * land: water Review R3.4) leaves the window as it is (round 0.9.2, WTR-A: not up to CW_WATER_WET_REACH while the host
 * says the character is in water; no W1 at all while it says it is dry): the reach is widened only for a
 * character in the water column, so no dry place under water becomes water (where the game's own gathers reach the
 * water already, nothing changes either). Returns the new count.
 */
/* the height at which the vertical line through (x, z) crosses triangle a b c (world space); 0 when it misses it */
static int cw_vline(float x, float z, const float a[3], const float b[3], const float c[3], float *y)
{
    float e1x = b[0] - a[0], e1z = b[2] - a[2], e2x = c[0] - a[0], e2z = c[2] - a[2], px = x - a[0], pz = z - a[2];
    float d = e1x * e2z - e2x * e1z, u, v;
    if (!(d > 1e-6f || d < -1e-6f)) {
        return 0;               /* a vertical triangle (a wall) */
    }
    u = (px * e2z - e2x * pz) / d;
    v = (e1x * pz - px * e1z) / d;
    if (!(u >= 0.0f && v >= 0.0f && u + v <= 1.0f)) {
        return 0;
    }
    *y = a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1]);
    return 1;
}

/* the point i of a model as world space through its object's position (GRID objects: evalflags 6, no rotation); P is the
 * host address of the model's points (round 0.9.2: taken once per model - the segments of the collision segment are one
 * host block, so P + 12 i is what cw_ptr(pts + 12 i) gives, the same bytes - which takes about a fifth off W1's land
 * test: WTR-W1V's cost check, 0.37-0.41 ms per step over 20,000 triangles on x64) */
static void cw_point(const uint8_t *P, const float pos[3], uint32_t i, float out[3])
{
    const uint8_t *q = P + 12u * i;
    out[0] = pos[0] + cw_rdf(q);
    out[1] = pos[1] + cw_rdf(q + 4);
    out[2] = pos[2] + cw_rdf(q + 8);
}

/* W1: does a land triangle cross the vertical line through the character between y0 = p.y + 1 and ytop? (the land
 * objects of the 3 x 3 columns whose box covers the character's x / z and reaches into that span; GRID models are
 * triangles and one-triangle strips, longer strips are walked too; round 0.9.2 (WTR-W1V, water Review R3.4): and the
 * moving objects, cw_slot_blocked) */
static int cw_slot_blocked(const sa1g_world *w, const cw_state *S, const float p[3], float ytop);

static int cw_water_blocked(const sa1g_world *w, const cw_state *S, const uint8_t *cols, const uint8_t *hash,
                            uint32_t hsize, uint32_t nobj, const uint8_t *boxes, float cell, const float p[3], float ytop)
{
    float y0 = p[1] + 1.0f;
    int32_t ix, iz, cx = cw_floor(p[0] / cell), cz = cw_floor(p[2] / cell);
    for (ix = cx - 1; ix <= cx + 1; ix++) {
        for (iz = cz - 1; iz <= cz + 1; iz++) {
            uint32_t first = 0, cnt = cw_column(hash, hsize, ix, iz, &first), k;
            for (k = 0; k < cnt && first + k < nobj; k++) {
                uint32_t idx = first + k, attr, obj, model, pts, ms, nms, m;
                const uint8_t *c = cols + SA1CA_COL_SIZE * idx, *bx = boxes + 24u * idx, *P;
                float pos[3];
                attr = cw_rd32(c + 0x20);
                if (!(attr & 0x1u) || (attr & CW_WATER_ATTR)) {
                    continue;
                }
                if (p[0] < cw_rdf(bx) || p[0] > cw_rdf(bx + 12) || p[2] < cw_rdf(bx + 8) || p[2] > cw_rdf(bx + 20)
                    || !(cw_rdf(bx + 16) > y0) || !(cw_rdf(bx + 4) < ytop)) {
                    continue;
                }
                obj = cw_rd32(c + 0x18);
                if (obj == 0) {
                    continue;
                }
                model = cw_rd32((const uint8_t *)cw_ptr(w, obj) + 4);
                if (model == 0) {
                    continue;
                }
                pos[0] = cw_rdf((const uint8_t *)cw_ptr(w, obj) + 0x08);
                pos[1] = cw_rdf((const uint8_t *)cw_ptr(w, obj) + 0x0C);
                pos[2] = cw_rdf((const uint8_t *)cw_ptr(w, obj) + 0x10);
                pts = cw_rd32((const uint8_t *)cw_ptr(w, model));
                P = (const uint8_t *)cw_ptr(w, pts);
                ms = cw_rd32((const uint8_t *)cw_ptr(w, model) + 0x0C);
                {
                    uint16_t h = 0;
                    memcpy(&h, (const uint8_t *)cw_ptr(w, model) + 0x14, 2);
                    nms = h;
                }
                for (m = 0; m < nms && m < 1024u; m++) {
                    const uint8_t *s = (const uint8_t *)cw_ptr(w, ms + SA1CA_MESHSET_SIZE * m);
                    uint16_t type = 0, nb = 0;
                    const uint8_t *mesh;
                    uint32_t t;
                    memcpy(&type, s, 2);
                    memcpy(&nb, s + 2, 2);
                    mesh = (const uint8_t *)cw_ptr(w, cw_rd32(s + 4));
                    for (t = 0; t < nb; t++) {
                        uint16_t i0 = 0, i1 = 0, i2 = 0, hdr = 0;
                        float a[3], b[3], d[3], y;
                        uint32_t len, j;
                        if ((type & 0xC000u) == 0) {
                            memcpy(&i0, mesh, 2);
                            memcpy(&i1, mesh + 2, 2);
                            memcpy(&i2, mesh + 4, 2);
                            mesh += 6;
                            cw_point(P, pos, i0, a);
                            cw_point(P, pos, i1, b);
                            cw_point(P, pos, i2, d);
                            if (cw_vline(p[0], p[2], a, b, d, &y) && y > y0 && y < ytop) {
                                return 1;
                            }
                            continue;
                        }
                        if ((type & 0xC000u) != 0x8000u) {
                            break;      /* quads / N-gons: not in GRID models */
                        }
                        memcpy(&hdr, mesh, 2);
                        len = hdr & 0x3FFFu;
                        memcpy(&i0, mesh + 2, 2);
                        memcpy(&i1, mesh + 4, 2);
                        for (j = 2; j < len; j++) {
                            memcpy(&i2, mesh + 2 + 2 * j, 2);
                            cw_point(P, pos, i0, a);
                            cw_point(P, pos, i1, b);
                            cw_point(P, pos, i2, d);
                            if (cw_vline(p[0], p[2], a, b, d, &y) && y > y0 && y < ytop) {
                                return 1;
                            }
                            i0 = i1;
                            i1 = i2;
                        }
                        mesh += 2 + 2 * len;
                    }
                }
            }
        }
    }
    return cw_slot_blocked(w, S, p, ytop) ? 2 : 0;     /* 2: a moving object */
}

/* W1V (round 0.9.2; sadc docs/research/gsa1_090_water.md Review R3.4): a moving object (sa1_collision_object_*: a host's
 * brush entity, a prop) between the character and the water is land too: its land groups' triangles in the transform the
 * refresh just gave the slot (cw_moving runs before the window: what the game collides with in the next step), object
 * space through R = cw_rot(ang) and the position. 0.9.1 looked at GRID land only and widened the water through such a
 * ceiling into a dry room. */
static int cw_slot_blocked(const sa1g_world *w, const cw_state *S, const float p[3], float ytop)
{
    float y0 = p[1] + 1.0f;
    uint32_t j, e, i, k;
    for (j = 0; j < CW_SLOTS; j++) {
        const cw_slot *sl = &S->slot[j];
        float rs[9], rmax = 0.0f, dx, dz;
        if (sl->id == 0 || sl->nent == 0) {
            continue;
        }
        for (e = 0; e < sl->nent; e++) {
            const uint8_t *obj = (const uint8_t *)cw_ptr(w, sl->ent[e].obj);
            float mr = cw_rdf((const uint8_t *)cw_ptr(w, cw_rd32(obj + 4)) + 0x24);
            rmax = mr > rmax ? mr : rmax;
        }
        dx = p[0] - sl->pos[0];
        dz = p[2] - sl->pos[2];
        if (dx * dx + dz * dz > rmax * rmax || !(sl->pos[1] + rmax > y0) || !(sl->pos[1] - rmax < ytop)) {
            continue;
        }
        cw_rot(sl->ang, rs);
        for (e = 0; e < sl->nent; e++) {
            const uint8_t *obj, *pts;
            if (!(sl->ent[e].col & 0x1u) || (sl->ent[e].col & CW_WATER_ATTR)) {
                continue;
            }
            obj = (const uint8_t *)cw_ptr(w, sl->ent[e].obj);
            pts = (const uint8_t *)cw_ptr(w, cw_rd32((const uint8_t *)cw_ptr(w, cw_rd32(obj + 4))));
            for (i = 0; i < sl->ent[e].ntris; i++) {
                float v[3][3], q[3], y;
                uint32_t c;
                for (c = 0; c < 3; c++) {
                    for (k = 0; k < 3; k++) {
                        q[k] = cw_rdf(pts + 36u * i + 12u * c + 4u * k);
                    }
                    for (k = 0; k < 3; k++) {
                        v[c][k] = rs[3u * k] * q[0] + rs[3u * k + 1u] * q[1] + rs[3u * k + 2u] * q[2] + sl->pos[k];
                    }
                }
                if (cw_vline(p[0], p[2], v[0], v[1], v[2], &y) && y > y0 && y < ytop) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* ---- WTR-A (round 0.9.2; sadc docs/research/gsa1_090_water.md WTR-A with its Review R4 a-c, R3.3-R3.5): the host's water
 * at the character. sa1_input.host_flags arrive per tick (sa1g_world_host_flags, sa1_world_tick.c: w->hostwater); the host
 * knows where its water is (a Source engine host: util.PointContents at the character's feet + 1 unit, from the state
 * after the previous tick), where the game's water probe 8C0925A0 only sees GMod water as its top faces and counts a dry
 * room, sewer or tunnel under that water as under water whenever its ceiling is out of 8C02D2E0's short reach. Window
 * adaptation only (the copy is rebuilt every step), never game code, and never for E-102 (no water probe; 8C073160 takes
 * water as land for him):
 *   KNOWN, not IN_WATER   (cw_water_dry) every water object with a water polygon over the character's probe point (on
 *                         its vertical line, within CW_WATER_DRY_REACH: the next step's probe runs after it moved) stays
 *                         out of the window copy, and W1 does not run. Not a box test: one GRID cube can hold two water
 *                         levels (Review R4 a). Water below the probe point (the shadow on it, landing in it) is
 *                         unaffected; so is a surface the character came down through since the flags were taken (below
 *                         the position the host tested, with no land between: it fell or dived into that water during
 *                         the tick, whose dry flag is older than the dive);
 *   KNOWN | IN_WATER      W1 without its land test up to CW_WATER_WET_REACH (deep water over a submerged ledge stays
 *                         reachable; the shelf of Review R4 b);
 *   neither               0.9.1's rules, byte for byte (the reference worlds, other hosts).
 * Limits: the flags are up to one host tick late on a hitch (the pairs of a tick share them); where the host's geometry cut
 * the water top away over the character (a dock, a pontoon, a walkway resting on the surface) no water polygon lies over
 * it, so IN_WATER cannot create water there (Review R3.3; owner question B10 territory, the game's own probe rule); water
 * of moving objects (DynamicCOL entries, gathered by the game itself) is not filtered. */
static int cw_host_water(const sa1g_world *w, uint32_t chr, int player)
{
    uint32_t f = w->hostwater.flags;
    if (!player || chr == 6u || w->replay || !(f & SA1_INPUT_HOST_WATER_KNOWN)) {
        return 0;
    }
    return (f & SA1_INPUT_HOST_IN_WATER) ? 2 : 1;
}

/* the squared horizontal distance from (x, z) to segment a b */
static float cw_seg2(float x, float z, const float a[3], const float b[3])
{
    float ex = b[0] - a[0], ez = b[2] - a[2], px = x - a[0], pz = z - a[2], l = ex * ex + ez * ez, t = 0.0f, dx, dz;
    if (l > 0.0f) {
        t = (px * ex + pz * ez) / l;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }
    dx = px - t * ex;
    dz = pz - t * ez;
    return dx * dx + dz * dz;
}

/* the height of triangle a b c's plane over (x, z) and the squared horizontal distance from (x, z) to the triangle (0
 * inside, cw_vline's test); 0 for a vertical triangle */
static int cw_vplane(float x, float z, const float a[3], const float b[3], const float c[3], float *y, float *d2)
{
    float e1x = b[0] - a[0], e1z = b[2] - a[2], e2x = c[0] - a[0], e2z = c[2] - a[2], px = x - a[0], pz = z - a[2];
    float d = e1x * e2z - e2x * e1z, u, v, s;
    if (!(d > 1e-6f || d < -1e-6f)) {
        return 0;
    }
    u = (px * e2z - e2x * pz) / d;
    v = (e1x * pz - px * e1z) / d;
    *y = a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1]);
    if (u >= 0.0f && v >= 0.0f && u + v <= 1.0f) {
        *d2 = 0.0f;
        return 1;
    }
    *d2 = cw_seg2(x, z, a, b);
    s = cw_seg2(x, z, b, c);
    *d2 = s < *d2 ? s : *d2;
    s = cw_seg2(x, z, c, a);
    *d2 = s < *d2 ? s : *d2;
    return 1;
}

/* KNOWN dry: does GRID water object idx (a window candidate) have a water polygon over the character? 1 = leave it out */
static int cw_water_dry(sa1g_world *w, const cw_state *S, const float p[3], uint32_t idx)
{
    const uint8_t *gh = (const uint8_t *)cw_ptr(w, S->grid);
    uint32_t hsize = cw_rd32(gh + SA1CA_GH_HASH_SIZE), nobj = cw_rd32(gh + SA1CA_GH_OBJECTS), obj, model, pts, ms, nms, m;
    const uint8_t *cols = (const uint8_t *)cw_ptr(w, cw_rd32(gh + SA1CA_GH_COLS));
    const uint8_t *hash = hsize ? (const uint8_t *)cw_ptr(w, cw_rd32(gh + SA1CA_GH_HASH)) : NULL;
    const uint8_t *boxes = cw_rd32(gh + SA1CA_GH_BOXES) ? (const uint8_t *)cw_ptr(w, cw_rd32(gh + SA1CA_GH_BOXES)) : NULL;
    const uint8_t *c = cols + SA1CA_COL_SIZE * idx, *o, *P;
    sa1g_hostwater *hw = &w->hostwater;
    float y0 = p[1] + 1.0f, yref = p[1], reach2 = CW_WATER_DRY_REACH * CW_WATER_DRY_REACH, hmin = 0.0f, pos[3];
    int have = 0;
    uint16_t h16 = 0;
    if (hw->ref_valid && hw->ref[1] > yref) {
        yref = hw->ref[1];
    }
    if (boxes != NULL) {
        const uint8_t *bx = boxes + 24u * idx;
        if (p[0] < cw_rdf(bx) - CW_WATER_DRY_REACH || p[0] > cw_rdf(bx + 12) + CW_WATER_DRY_REACH
            || p[2] < cw_rdf(bx + 8) - CW_WATER_DRY_REACH || p[2] > cw_rdf(bx + 20) + CW_WATER_DRY_REACH
            || !(cw_rdf(bx + 16) > y0)) {
            return 0;
        }
    } else {
        float ex = cw_rdf(c) - p[0], ez = cw_rdf(c + 8) - p[2], rr = cw_rdf(c + 0x0C) + CW_WATER_DRY_REACH;
        if (ex * ex + ez * ez > rr * rr || !(cw_rdf(c + 4) + cw_rdf(c + 0x0C) > y0)) {
            return 0;
        }
    }
    obj = cw_rd32(c + 0x18);
    if (obj == 0) {
        return 0;
    }
    o = (const uint8_t *)cw_ptr(w, obj);
    model = cw_rd32(o + 4);
    if (model == 0) {
        return 0;
    }
    pos[0] = cw_rdf(o + 0x08);
    pos[1] = cw_rdf(o + 0x0C);
    pos[2] = cw_rdf(o + 0x10);
    pts = cw_rd32((const uint8_t *)cw_ptr(w, model));
    P = (const uint8_t *)cw_ptr(w, pts);
    ms = cw_rd32((const uint8_t *)cw_ptr(w, model) + 0x0C);
    memcpy(&h16, (const uint8_t *)cw_ptr(w, model) + 0x14, 2);
    nms = h16;
    /* the lowest water polygon over the character (GRID models: triangles and strips, as cw_water_blocked walks them) */
    for (m = 0; m < nms && m < 1024u; m++) {
        const uint8_t *s = (const uint8_t *)cw_ptr(w, ms + SA1CA_MESHSET_SIZE * m), *mesh;
        uint16_t type = 0, nb = 0;
        uint32_t t;
        memcpy(&type, s, 2);
        memcpy(&nb, s + 2, 2);
        mesh = (const uint8_t *)cw_ptr(w, cw_rd32(s + 4));
        for (t = 0; t < nb; t++) {
            uint16_t i0 = 0, i1 = 0, i2 = 0, hdr = 0;
            float a[3], b[3], d[3], y, d2;
            uint32_t len, j;
            if ((type & 0xC000u) == 0) {
                memcpy(&i0, mesh, 2);
                memcpy(&i1, mesh + 2, 2);
                memcpy(&i2, mesh + 4, 2);
                mesh += 6;
                cw_point(P, pos, i0, a);
                cw_point(P, pos, i1, b);
                cw_point(P, pos, i2, d);
                if (cw_vplane(p[0], p[2], a, b, d, &y, &d2) && d2 <= reach2 && y > y0 && (!have || y < hmin)) {
                    hmin = y;
                    have = 1;
                }
                continue;
            }
            if ((type & 0xC000u) != 0x8000u) {
                break;      /* quads / N-gons: not in GRID models */
            }
            memcpy(&hdr, mesh, 2);
            len = hdr & 0x3FFFu;
            memcpy(&i0, mesh + 2, 2);
            memcpy(&i1, mesh + 4, 2);
            for (j = 2; j < len; j++) {
                memcpy(&i2, mesh + 2 + 2 * j, 2);
                cw_point(P, pos, i0, a);
                cw_point(P, pos, i1, b);
                cw_point(P, pos, i2, d);
                if (cw_vplane(p[0], p[2], a, b, d, &y, &d2) && d2 <= reach2 && y > y0 && (!have || y < hmin)) {
                    hmin = y;
                    have = 1;
                }
                i0 = i1;
                i1 = i2;
            }
            mesh += 2 + 2 * len;
        }
    }
    if (!have) {
        return 0;           /* no water over the probe point: below it (its shadow, a landing) or beside it */
    }
    if (hmin > yref + 1.0f || (boxes != NULL && hash != NULL
                               && cw_water_blocked(w, S, cols, hash, hsize, nobj, boxes, cw_rdf(gh + SA1CA_GH_CELL), p,
                                                   hmin))) {
        hw->dry_left_out++;
        return 1;
    }
    hw->dry_entered++;      /* below where the host tested, nothing between: it came down into that water in this tick */
    return 0;
}

#define CW_WATER_MAX    4u              /* water objects at the lowest level over the character (a cell corner: four) */

static uint32_t cw_water(sa1g_world *w, cw_state *S, const float p[3], cw_sel *sel, uint32_t n, uint32_t cap_obj,
                         uint32_t hard, uint32_t *npoly, uint32_t *over_obj, uint32_t *over_poly, uint32_t *over_far)
{
    const uint8_t *gh = (const uint8_t *)cw_ptr(w, S->grid);
    uint32_t hsize = cw_rd32(gh + SA1CA_GH_HASH_SIZE), nobj = cw_rd32(gh + SA1CA_GH_OBJECTS);
    const uint8_t *cols = nobj ? (const uint8_t *)cw_ptr(w, cw_rd32(gh + SA1CA_GH_COLS)) : NULL;
    const uint8_t *polys = nobj ? (const uint8_t *)cw_ptr(w, cw_rd32(gh + SA1CA_GH_POLYS)) : NULL;
    const uint8_t *hash = hsize ? (const uint8_t *)cw_ptr(w, cw_rd32(gh + SA1CA_GH_HASH)) : NULL;
    const uint8_t *boxes = nobj && cw_rd32(gh + SA1CA_GH_BOXES) ? (const uint8_t *)cw_ptr(w, cw_rd32(gh + SA1CA_GH_BOXES))
                                                                : NULL;
    uint8_t *win = (uint8_t *)cw_ptr(w, S->cols);
    float cell = cw_rdf(gh + SA1CA_GH_CELL), lowest = 0.0f, need[CW_WATER_MAX], dx, dy, dz;
    uint32_t hit[CW_WATER_MAX], slot[CW_WATER_MAX], nhit = 0, j, q, pass, todo = 0;
    int32_t ix, iz, cx, cz;
    int have = 0, host = cw_host_water(w, (uint32_t)w->chr, 1), blk;
    const uint8_t *c;
    if (host == 1) {
        w->hostwater.dry_steps++;
        return n;           /* WTR-A, KNOWN dry: no deep water over a character the host says is dry */
    }
    if (hash == NULL || cols == NULL || polys == NULL || !(cell > 0.0f)) {
        return n;
    }
    w->hostwater.wet_steps += host == 2;
    cx = cw_floor(p[0] / cell);
    cz = cw_floor(p[2] / cell);
    /* pass 0: the lowest water level over the character; pass 1: the objects at that level (pieces of neighbouring
       cells meet at the cell edges, where the probe's own edge rules may take either: all of them go in) */
    for (pass = 0; pass < 2u; pass++) {
        for (ix = cx - 1; ix <= cx + 1; ix++) {
            for (iz = cz - 1; iz <= cz + 1; iz++) {
                uint32_t first = 0, cnt = cw_column(hash, hsize, ix, iz, &first), k;
                for (k = 0; k < cnt && first + k < nobj; k++) {
                    uint32_t idx = first + k;
                    float bot;
                    c = cols + SA1CA_COL_SIZE * idx;
                    if (!(cw_rd32(c + 0x20) & CW_WATER_ATTR)) {
                        continue;
                    }
                    if (boxes != NULL) {
                        const uint8_t *bx = boxes + 24u * idx;
                        if (p[0] < cw_rdf(bx) || p[0] > cw_rdf(bx + 12) || p[2] < cw_rdf(bx + 8)
                            || p[2] > cw_rdf(bx + 20)) {
                            continue;
                        }
                        bot = cw_rdf(bx + 4);
                    } else {
                        float ex = cw_rdf(c) - p[0], ez = cw_rdf(c + 8) - p[2], rr = cw_rdf(c + 0x0C);
                        if (ex * ex + ez * ez > rr * rr) {
                            continue;
                        }
                        bot = cw_rdf(c + 4);
                    }
                    if (!(bot > p[1]) || bot - p[1] > CW_WATER_UP) {
                        continue;
                    }
                    if (pass == 0) {
                        if (!have || bot < lowest) {
                            lowest = bot;
                            have = 1;
                        }
                    } else if (bot <= lowest + 1.0f && nhit < CW_WATER_MAX) {
                        hit[nhit++] = idx;
                    }
                }
            }
        }
        if (!have) {
            return n;
        }
    }
    S->water.found++;
    S->water.last_index = hit[0];
    S->water.last_bottom = lowest;
    /* the radius both gathers need: 8C0522C0 takes an object while |c - p| - 200 < xWidth */
    for (q = 0; q < nhit; q++) {
        c = cols + SA1CA_COL_SIZE * hit[q];
        dx = cw_rdf(c) - p[0];
        dy = cw_rdf(c + 4) - p[1];
        dz = cw_rdf(c + 8) - p[2];
        need[q] = sa1g_sqrtf(dx * dx + dy * dy + dz * dz) - CW_WATER_PROBE + CW_WATER_SLACK;
        slot[q] = 0xFFFFFFFFu;
        for (j = 0; j < n && slot[q] == 0xFFFFFFFFu; j++) {
            if (sel[j].idx == hit[q]) {
                slot[q] = j;
            }
        }
        if (slot[q] == 0xFFFFFFFFu || need[q] > cw_rdf(win + SA1CA_COL_SIZE * slot[q] + 0x0C)) {
            todo++;
        }
    }
    if (todo == 0) {
        S->water.last_slot = slot[0];   /* the game's own gathers reach them: nothing to do */
        S->water.last_radius = cw_rdf(win + SA1CA_COL_SIZE * slot[0] + 0x0C);
        return n;
    }
    if (host == 2 && lowest - p[1] <= CW_WATER_WET_REACH) {
        w->hostwater.wet_through++;     /* WTR-A, IN_WATER: no land test (deep water over a submerged ledge) */
    } else if (boxes != NULL && (blk = cw_water_blocked(w, S, cols, hash, hsize, nobj, boxes, cell, p, lowest)) != 0) {
        S->water.blocked++;             /* land between: the character is not in this water (a cave under it) */
        w->hostwater.wet_far += host == 2;
        w->hostwater.moving_blocked += blk == 2;
        return n;
    }
    /* room for the ones not in the window, for all of them at once: the farthest entries go first; the nearest one, those
       within 40 units and the water objects already in the window stay (the others move up) */
    {
        uint32_t nin = 0, npin = 0, jj;
        for (q = 0; q < nhit; q++) {
            if (slot[q] == 0xFFFFFFFFu) {
                nin++;
                npin += cw_rd32(polys + 4u * hit[q]);
            }
        }
        for (jj = n; nin != 0 && jj-- > 1u && (n + nin > cap_obj || *npoly + npin > hard);) {
            int keep = sel[jj].d < CW_NEAR_RANGE;
            for (q = 0; q < nhit && !keep; q++) {
                keep = slot[q] == jj;
            }
            if (keep) {
                continue;
            }
            S->water.replaced++;
            if (!(sel[jj].d < CW_FAR)) {
                (*over_far)++;
            } else if (n + nin > cap_obj) {
                (*over_obj)++;
            } else {
                (*over_poly)++;
            }
            *npoly -= sel[jj].np;
            memmove(win + SA1CA_COL_SIZE * jj, win + SA1CA_COL_SIZE * (jj + 1u), SA1CA_COL_SIZE * (n - 1u - jj));
            memmove(&sel[jj], &sel[jj + 1u], sizeof *sel * (n - 1u - jj));
            n--;
            for (q = 0; q < nhit; q++) {
                if (slot[q] != 0xFFFFFFFFu && slot[q] > jj) {
                    slot[q]--;
                }
            }
        }
    }
    for (q = 0; q < nhit; q++) {
        float r;
        if (slot[q] == 0xFFFFFFFFu) {
            uint32_t np = cw_rd32(polys + 4u * hit[q]);
            if (n >= cap_obj || *npoly + np > hard) {
                S->water.refused++;
                continue;
            }
            memcpy(win + SA1CA_COL_SIZE * n, cols + SA1CA_COL_SIZE * hit[q], SA1CA_COL_SIZE);
            sel[n].idx = hit[q];
            sel[n].np = np;
            sel[n].d = -1.0f;
            slot[q] = n++;
            *npoly += np;
            S->water.inserted++;
        }
        r = cw_rdf(win + SA1CA_COL_SIZE * slot[q] + 0x0C);
        if (need[q] > r) {
            cw_wrf(win + SA1CA_COL_SIZE * slot[q] + 0x0C, need[q]);
            r = need[q];
            S->water.widened++;
        }
        if (q == 0) {
            S->water.last_slot = slot[q];
            S->water.last_radius = r;
        }
    }
    return n;
}

/* round 0.9.3a: the top of a GRID object (its box's high y; without boxes the top of its COL sphere) */
static float cw_water_top(const uint8_t *c, const uint8_t *boxes, uint32_t idx)
{
    if (boxes != NULL) {
        return cw_rdf(boxes + 24u * idx + 16u);
    }
    return cw_rdf(c + 4) + cw_rdf(c + 0x0C);
}

static void cw_window(sa1g_world *w, cw_state *S, const float p[3], uint32_t chr, int player, float rad, uint32_t *active)
{
    cw_sel sel[CW_MAX_LAND];
    /* the query bound: objects whose COL sphere comes within qr of the character can be in its next land-collision query */
    float qr = 2.0f * rad + CW_QUERY_MARGIN;
    const uint8_t *gh = (const uint8_t *)cw_ptr(w, S->grid);
    uint32_t hsize = cw_rd32(gh + SA1CA_GH_HASH_SIZE), nobj = cw_rd32(gh + SA1CA_GH_OBJECTS);
    const uint8_t *cols = nobj ? (const uint8_t *)cw_ptr(w, cw_rd32(gh + SA1CA_GH_COLS)) : NULL;
    const uint8_t *polys = nobj ? (const uint8_t *)cw_ptr(w, cw_rd32(gh + SA1CA_GH_POLYS)) : NULL;
    const uint8_t *hash = hsize ? (const uint8_t *)cw_ptr(w, cw_rd32(gh + SA1CA_GH_HASH)) : NULL;
    const uint8_t *boxes = nobj && cw_rd32(gh + SA1CA_GH_BOXES) ? (const uint8_t *)cw_ptr(w, cw_rd32(gh + SA1CA_GH_BOXES))
                                                                : NULL;
    float cell = cw_rdf(gh + SA1CA_GH_CELL), maxr = cw_rdf(gh + SA1CA_GH_MAX_RADIUS), range, reach;
    uint8_t *hdr = (uint8_t *)cw_ptr(w, S->hdr), *win = (uint8_t *)cw_ptr(w, S->cols);
    uint32_t ncand = 0, cand_over = 0, dn, i, n = 0, npoly = 0, dyn_n = 0, dyn_polys = 0, dyn_near = 0;
    uint32_t cap_obj, budget, hard, near_cap, near_n = 0, over_obj = 0, over_poly = 0, over_near = 0, over_far = 0;
    uint32_t must_over = 0;
    int32_t ix, iz, ix0, ix1, iz0, iz1;
    int host = cw_host_water(w, chr, player);   /* WTR-A (round 0.9.2): the host's water at the character */
    /* round 0.9.3a (SA1_SUPER_WATER_WHILE_SUPER; sadc docs/research/super_general_design.md 4.10 with its Review RC5): the
       water floor only while player 0 is Super, and only for water whose top is at most the tolerance above him */
    int wsuper = 0;
    float wsuper_top = p[1] + rad * SA1_SUPER_WATER_TOLERANCE;
    if ((w->super & SA1G_SUPER_WATER) && player) {
        sa1g_player_ptrs wpp;
        sa1g_world_player(w, &wpp);
        wsuper = wpp.pwp != 0 && (cw_rd32((const uint8_t *)cw_ptr(w, wpp.pwp) + 4) & 0x8000u) != 0;
    }
    range = cw_rdf(cw_ptr(w, SA1CA_ADDR_GATHER_RANGE));
    if (!(range > 0.0f) || range > 100000.0f) {
        range = 400.0f;
    }
    /* the moving entries the game gathers (8C052518: |pos + center - p| < r + range) */
    dn = cw_dyn_count(w);
    for (i = 0; i < dn && i < CW_DYN_MAX; i++) {
        const uint8_t *e = (const uint8_t *)cw_ptr(w, CW_ADDR_DYN_LIST + 12u * i);
        uint32_t attr = cw_rd32(e), og = cw_rd32(e + 4), tg = cw_rd32(e + 8), twp, mg;
        const uint8_t *o, *m;
        float d[3], rr, dd;
        if (og == 0 || tg == 0) {
            continue;
        }
        twp = cw_task_word(w, tg, 0x20u);
        if (twp == 0 || !(cw_rd32((const uint8_t *)cw_ptr(w, twp) + 4) & 0x100u)) {
            continue;
        }
        o = (const uint8_t *)cw_ptr(w, og);
        mg = cw_rd32(o + 4);
        if (mg == 0) {
            continue;
        }
        m = (const uint8_t *)cw_ptr(w, mg);
        d[0] = cw_rdf(o + 0x08) + cw_rdf(m + 0x18) - p[0];
        d[1] = cw_rdf(o + 0x0C) + cw_rdf(m + 0x1C) - p[1];
        d[2] = cw_rdf(o + 0x10) + cw_rdf(m + 0x20) - p[2];
        dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        rr = cw_rdf(m + 0x24) + range + 16.0f;
        if (dd < rr * rr) {
            dyn_n++;
            dyn_polys += cw_model_tris(w, mg);
            /* 8C0522C0 takes a moving entry while |pos + center - c_query| < model r + r + 30 */
            rr = cw_rdf(m + 0x24) + 30.0f + qr;
            if (dd < rr * rr && (chr == 6u || !(attr & 0x400002u))) {
                dyn_near++;
            }
        }
    }
    /* the GRID objects within the gather range, nearest first */
    reach = range + maxr;
    if (hash != NULL && cell > 0.0f) {
        ix0 = cw_floor((p[0] - reach) / cell);
        ix1 = cw_floor((p[0] + reach) / cell);
        iz0 = cw_floor((p[2] - reach) / cell);
        iz1 = cw_floor((p[2] + reach) / cell);
        for (ix = ix0; ix <= ix1; ix++) {
            for (iz = iz0; iz <= iz1; iz++) {
                uint32_t h, first = 0, cnt = 0, k, probes = 0;
                for (h = sa1ca_grid_hash(ix, iz) & (hsize - 1u); probes < hsize; h = (h + 1u) & (hsize - 1u), probes++) {
                    const uint8_t *he = hash + SA1CA_GRID_HASH_ENTRY * h;
                    cnt = cw_rd32(he + 12);
                    if (cnt == 0) {
                        break;
                    }
                    if ((int32_t)cw_rd32(he) == ix && (int32_t)cw_rd32(he + 4) == iz) {
                        first = cw_rd32(he + 8);
                        break;
                    }
                    cnt = 0;
                }
                for (k = 0; k < cnt && first + k < nobj; k++) {
                    const uint8_t *c = cols + SA1CA_COL_SIZE * (first + k);
                    float dx = cw_rdf(c) - p[0], dy = cw_rdf(c + 4) - p[1], dz = cw_rdf(c + 8) - p[2], d, q;
                    d = sa1g_sqrtf(dx * dx + dy * dy + dz * dz) - cw_rdf(c + 0x0C);
                    if (!(d < range)) {
                        continue;   /* the game's gather 8C052518 tests the COL sphere */
                    }
                    q = d;
                    if (boxes != NULL) {
                        /* ranked by the distance to the object's box: of the objects whose spheres reach the character
                           (overlapping cell parts, stacked layers), the ones under and around it come first */
                        const uint8_t *bx = boxes + 24u * (first + k);
                        float e[3];
                        int a;
                        for (a = 0; a < 3; a++) {
                            float lo = cw_rdf(bx + 4u * (uint32_t)a), hi = cw_rdf(bx + 12u + 4u * (uint32_t)a);
                            e[a] = p[a] < lo ? lo - p[a] : (p[a] > hi ? p[a] - hi : 0.0f);
                        }
                        d = sa1g_sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
                    }
                    if (ncand >= CW_CAND) {
                        cand_over++;
                        continue;
                    }
                    S->cand[ncand].d = d;
                    S->cand[ncand].idx = first + k;
                    S->cand[ncand].q = q;
                    ncand++;
                }
            }
        }
    }
    cw_sort(S, ncand);
    cap_obj = S->win_objects;
    if (cap_obj > CW_MAX_LAND - (dyn_n < CW_MAX_LAND ? dyn_n : CW_MAX_LAND)) {
        cap_obj = CW_MAX_LAND - (dyn_n < CW_MAX_LAND ? dyn_n : CW_MAX_LAND);
    }
    budget = S->win_polys > dyn_polys ? S->win_polys - dyn_polys : 0u;
    hard = CW_POOL_HARD > dyn_polys ? CW_POOL_HARD - dyn_polys : 0u;
    near_cap = S->stats.query_cap > dyn_near ? S->stats.query_cap - dyn_near : 0u;
    for (i = 0; i < ncand; i++) {
        const uint8_t *c = cols + SA1CA_COL_SIZE * S->cand[i].idx;
        uint32_t np = cw_rd32(polys + 4u * S->cand[i].idx), attr = cw_rd32(c + 0x20);
        /* round 6a (SA1_CREATE_WATER_FLOOR, super_sonic.md Review R.3.4): in this world a water object (its upward faces:
           the GRID keeps no other faces of WATER / HEAVY_WATER) is copied as a solid floor, before the WTR-A test below */
        int wfloor = (attr & CW_WATER_ATTR) && ((w->super & SA1G_WATER_FLOOR) ||
                                                 (wsuper && cw_water_top(c, boxes, S->cand[i].idx) <= wsuper_top));
        uint32_t wattr = wfloor ? ((attr & ~CW_WATER_ATTR) | 0x1u) : attr;
        /* in the character's next query (the game's own sphere test, 8C0522C0; 8C073160 skips water but for E-102):
           at most near_cap of them, nearest box first */
        int near;
        attr = wattr;
        near = S->cand[i].q < qr && (chr == 6u || !(attr & 0x400002u));
        /* the nearest object and those within 40 units: the whole pool (never left out for the budget alone) */
        int must = n == 0 || S->cand[i].d < CW_NEAR_RANGE;
        if (host == 1 && (attr & CW_WATER_ATTR) && cw_water_dry(w, S, p, S->cand[i].idx)) {
            continue;           /* WTR-A (round 0.9.2): water over a character the host says is dry */
        }
        if (n >= cap_obj || npoly + np > (must ? hard : budget)) {
            must_over += must && n < cap_obj;
            if (!(S->cand[i].d < CW_FAR)) {
                over_far++;
            } else if (n >= cap_obj) {
                over_obj++;
            } else {
                over_poly++;
            }
            continue;
        }
        if (near && near_n >= near_cap && n > 0) {
            over_near++;        /* the query bound (the nearest object always goes in: the floor under the character) */
            continue;
        }
        memcpy(win + SA1CA_COL_SIZE * n, c, SA1CA_COL_SIZE);
        if (wfloor) {
            cw_wr32(win + SA1CA_COL_SIZE * n + 0x20u, attr);
            S->stats.water_floors++;
        }
        if (w->board_col != 0) {
            /* round 0.9.5 (the board: gsa1_095_plan.md 6.2 task 3, review R.1): this world's player rides - the terrain
               override, or the board bits of this object (the copy only: the static segment is never written) */
            uint32_t bd = S->board ? ((const uint8_t *)cw_ptr(w, S->board))[S->cand[i].idx] : 0u;
            uint32_t ba = cw_board_word(w, attr, bd);
            if (ba != attr) {
                cw_wr32(win + SA1CA_COL_SIZE * n + 0x20u, ba);
            }
        }
        sel[n].idx = S->cand[i].idx;
        sel[n].np = np;
        sel[n].d = S->cand[i].d;
        n++;
        npoly += np;
        near_n += (uint32_t)near;
    }
    if (player && chr != 6u && !(w->super & SA1G_WATER_FLOOR)) {
        /* W1 (round 0.9.1): the water above the character, within the game's 128-entry gather and the pool (round 6a:
           not with SA1_CREATE_WATER_FLOOR, whose water is floor) */
        n = cw_water(w, S, p, sel, n, cap_obj, hard, &npoly, &over_obj, &over_poly, &over_far);
    }
    cw_wr16(hdr, n);
    S->stats.window_objects = n;
    S->stats.window_polygons = npoly;
    S->stats.candidates = ncand + cand_over;
    S->stats.dyn_near = dyn_n;
    S->stats.dyn_polygons = dyn_polys;
    S->stats.query_radius_x100 = (uint32_t)(rad * 100.0f + 0.5f);
    S->stats.query_near = near_n + dyn_near;
    if (S->stats.query_near > S->stats.query_max) {
        S->stats.query_max = S->stats.query_near;
    }
    if (over_far) {
        S->stats.over_far++;
    }
    if (over_obj || cand_over) {
        S->stats.over_objects++;
        cw_capacity(w, S, active, SA1_CAPACITY_WINDOW_OBJECTS, over_obj + cand_over, p);
    }
    if (must_over) {
        S->stats.must_over++;
    }
    if (over_poly) {
        S->stats.over_polygons++;
        /* round 0.9.3: reported only when an object within 40 units was left out even with the whole pool (must_over);
           the budget leaves farther objects out all the time on dense maps (gm_highway14800: 22 % of the windows), and
           objects within 40 units always got in - the owner's log lines said nothing */
        if (must_over) {
            cw_capacity(w, S, active, SA1_CAPACITY_WINDOW_POLYGONS, must_over, p);
        }
    }
    if (over_near) {
        S->stats.over_near++;
        cw_capacity(w, S, active, SA1_CAPACITY_LAND_QUERY, over_near, p);
    }
}

/* ---- contacts with the moving objects (API 0.3: pushing physics props) ------------------------------------------ */
static float cw_dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* the point of triangle (a, b, c) nearest to p (Ericson, Real-Time Collision Detection 5.1.5) */
static void cw_closest(const float p[3], const float a[3], const float b[3], const float c[3], float out[3])
{
    float ab[3], ac[3], ap[3], bp[3], cp[3], d1, d2, d3, d4, d5, d6, va, vb, vc, v, t, den;
    int k;
    for (k = 0; k < 3; k++) {
        ab[k] = b[k] - a[k];
        ac[k] = c[k] - a[k];
        ap[k] = p[k] - a[k];
        bp[k] = p[k] - b[k];
        cp[k] = p[k] - c[k];
    }
    d1 = cw_dot(ab, ap);
    d2 = cw_dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        memcpy(out, a, 3 * sizeof *out);
        return;
    }
    d3 = cw_dot(ab, bp);
    d4 = cw_dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        memcpy(out, b, 3 * sizeof *out);
        return;
    }
    vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f && d1 - d3 != 0.0f) {
        v = d1 / (d1 - d3);
        for (k = 0; k < 3; k++) {
            out[k] = a[k] + v * ab[k];
        }
        return;
    }
    d5 = cw_dot(ab, cp);
    d6 = cw_dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        memcpy(out, c, 3 * sizeof *out);
        return;
    }
    vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f && d2 - d6 != 0.0f) {
        t = d2 / (d2 - d6);
        for (k = 0; k < 3; k++) {
            out[k] = a[k] + t * ac[k];
        }
        return;
    }
    va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f && (d4 - d3) + (d5 - d6) != 0.0f) {
        t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        for (k = 0; k < 3; k++) {
            out[k] = b[k] + t * (c[k] - b[k]);
        }
        return;
    }
    den = va + vb + vc;
    if (den == 0.0f) {
        memcpy(out, a, 3 * sizeof *out);    /* degenerate */
        return;
    }
    v = vb / den;
    t = vc / den;
    for (k = 0; k < 3; k++) {
        out[k] = a[k] + ab[k] * v + ac[k] * t;
    }
}

/* Which moving objects player 0 touched in this step: its collision sphere where the game's land collision left it
 * (radius csts.radius, the centre half the height above taskwk.pos along the character's up: PSetPosition 8C0730D8's
 * ofs), against each slot's polygons in the transform the game collided with in this step (the refresh moves them only
 * afterwards). Read-only: guest memory is only read, nothing of the game is called. */
static void cw_contacts(sa1g_world *w, cw_state *S)
{
    sa1g_player_ptrs pp;
    const uint8_t *t, *pw;
    float pos[3], c[3], r, rm[9], wall[3], floor_n[3], v1[3], wl;
    int32_t ang[3] = { 0, 0, 0 };
    uint32_t j, k, ttp;
    uint64_t mask = 0;
    sa1g_world_player(w, &pp);
    if (pp.twp == 0 || pp.mwp == 0 || pp.pwp == 0) {
        S->ct_prev = 0;
        return;
    }
    t = (const uint8_t *)cw_ptr(w, pp.twp);
    pw = (const uint8_t *)cw_ptr(w, pp.pwp);
    r = pp.csts != 0 ? cw_rdf(cw_ptr(w, pp.csts)) : 0.0f;
    if (!(r > 0.0f && r < 1000.0f)) {
        r = cw_rdf(pw + 0x110) * 0.5f;      /* playerwk.p.height / 2: what 8C0730D8 gives csts.radius */
    }
    if (!(r > 0.0f && r < 1000.0f)) {
        S->ct_prev = 0;
        return;
    }
    ang[0] = (int32_t)cw_rd32(t + 0x14);
    ang[2] = (int32_t)cw_rd32(t + 0x1C);
    cw_rot(ang, rm);
    for (k = 0; k < 3; k++) {
        pos[k] = cw_rdf(t + 0x20 + 4u * k);
        c[k] = pos[k] + rm[3u * k + 1u] * r;   /* the character's up: the y column of its x / z turn */
        wall[k] = cw_rdf(pw + 0x44 + 4u * k);
        floor_n[k] = cw_rdf(pw + 0x50 + 4u * k);
        v1[k] = cw_rdf((const uint8_t *)cw_ptr(w, pp.mwp) + 0x04 + 4u * k);
    }
    ttp = cw_rd32(pw + 0x6C);
    wl = cw_dot(wall, wall);
    for (j = 0; j < CW_SLOTS; j++) {
        const cw_slot *sl = &S->slot[j];
        float rs[9], d[3], q[3], best = 1e30f, bp[3] = { 0.0f, 0.0f, 0.0f }, bn[3] = { 0.0f, 1.0f, 0.0f }, rmax = 0.0f;
        float pt[3], n[3], dist, lim;
        uint32_t e;
        cw_contact *o;
        if (sl->id == 0 || sl->nent == 0) {
            continue;
        }
        for (e = 0; e < sl->nent; e++) {
            const uint8_t *obj = (const uint8_t *)cw_ptr(w, sl->ent[e].obj);
            float mr = cw_rdf((const uint8_t *)cw_ptr(w, cw_rd32(obj + 4)) + 0x24);
            rmax = mr > rmax ? mr : rmax;
        }
        for (k = 0; k < 3; k++) {
            d[k] = c[k] - sl->pos[k];
        }
        lim = rmax + r + CW_CT_TOL;
        if (cw_dot(d, d) > lim * lim) {
            continue;
        }
        cw_rot(sl->ang, rs);
        for (k = 0; k < 3; k++) {
            q[k] = rs[k] * d[0] + rs[3 + k] * d[1] + rs[6 + k] * d[2];     /* R^T (c - pos): object space */
        }
        for (e = 0; e < sl->nent; e++) {
            const uint8_t *obj = (const uint8_t *)cw_ptr(w, sl->ent[e].obj);
            const uint8_t *model = (const uint8_t *)cw_ptr(w, cw_rd32(obj + 4));
            const uint8_t *pts = (const uint8_t *)cw_ptr(w, cw_rd32(model));
            uint32_t i;
            for (i = 0; i < sl->ent[e].ntris; i++) {
                float a[3], b[3], cc[3], cp[3], dd[3], d2;
                for (k = 0; k < 3; k++) {
                    a[k] = cw_rdf(pts + 36u * i + 4u * k);
                    b[k] = cw_rdf(pts + 36u * i + 12u + 4u * k);
                    cc[k] = cw_rdf(pts + 36u * i + 24u + 4u * k);
                }
                cw_closest(q, a, b, cc, cp);
                for (k = 0; k < 3; k++) {
                    dd[k] = q[k] - cp[k];
                }
                d2 = cw_dot(dd, dd);
                if (d2 < best) {
                    float u[3], v[3];
                    best = d2;
                    memcpy(bp, cp, sizeof bp);
                    for (k = 0; k < 3; k++) {
                        u[k] = b[k] - a[k];
                        v[k] = cc[k] - a[k];
                    }
                    bn[0] = u[1] * v[2] - u[2] * v[1];
                    bn[1] = u[2] * v[0] - u[0] * v[2];
                    bn[2] = u[0] * v[1] - u[1] * v[0];
                    if (cw_dot(bn, dd) < 0.0f) {
                        bn[0] = -bn[0];
                        bn[1] = -bn[1];
                        bn[2] = -bn[2];
                    }
                }
            }
        }
        lim = r + CW_CT_TOL;
        if (!(best <= lim * lim)) {
            continue;
        }
        dist = sa1g_sqrtf(best);
        for (k = 0; k < 3; k++) {
            pt[k] = rs[3u * k] * bp[0] + rs[3u * k + 1u] * bp[1] + rs[3u * k + 2u] * bp[2] + sl->pos[k];
            n[k] = c[k] - pt[k];
        }
        if (dist < 1e-4f) {
            for (k = 0; k < 3; k++) {
                n[k] = rs[3u * k] * bn[0] + rs[3u * k + 1u] * bn[1] + rs[3u * k + 2u] * bn[2];
            }
        }
        {
            float l2 = cw_dot(n, n);
            float il = l2 > 0.0f ? 1.0f / sa1g_sqrtf(l2) : 0.0f;
            for (k = 0; k < 3; k++) {
                n[k] *= il;
            }
        }
        if (S->ct_count >= CW_CONTACTS) {
            S->ct_head = (S->ct_head + 1u) % CW_CONTACTS;     /* the oldest goes */
            S->ct_count--;
            S->ct_dropped++;
        }
        o = &S->ct[(S->ct_head + S->ct_count) % CW_CONTACTS];
        S->ct_count++;
        memset(o, 0, sizeof *o);
        o->object = sl->id;
        o->step = (uint32_t)w->steps;
        memcpy(o->point, pt, sizeof o->point);
        memcpy(o->normal, n, sizeof o->normal);
        memcpy(o->v0, S->ct_v0, sizeof o->v0);
        memcpy(o->v1, v1, sizeof o->v1);
        o->depth = r - dist;
        if (sl->tp != 0 && (ttp & 0x1FFFFFFFu) == (sl->tp & 0x1FFFFFFFu)) {
            o->type = SA1_CONTACT_LANDING;
            o->flags |= SA1_CONTACTF_RIDING;
            if (cw_dot(floor_n, floor_n) > 0.25f) {
                memcpy(o->normal, floor_n, sizeof o->normal);       /* the game's floor normal */
            }
        } else if (n[1] >= CW_CT_LAND) {
            o->type = SA1_CONTACT_LANDING;
        } else if (n[1] <= -CW_CT_LAND) {
            o->type = SA1_CONTACT_HEAD;
        } else {
            o->type = SA1_CONTACT_SIDE;
            if (wl > 0.25f && cw_dot(wall, n) > 0.7f * sa1g_sqrtf(wl)) {
                o->flags |= SA1_CONTACTF_WALL;
                for (k = 0; k < 3; k++) {
                    o->normal[k] = wall[k] / sa1g_sqrtf(wl);        /* the game's wall normal */
                }
            }
        }
        if (!(S->ct_prev & ((uint64_t)1 << j))) {
            o->flags |= SA1_CONTACTF_FIRST;
        }
        mask |= (uint64_t)1 << j;
    }
    S->ct_prev = mask;
}

uint32_t sa1ca_world_contacts(const sa1_context *ctx, struct sa1g_world *w, sa1_contact *out, uint32_t cap)
{
    cw_state *S = cw_state_of(w);
    uint32_t n = 0;
    float sc;
    if (S == NULL || out == NULL) {
        return 0;
    }
    sc = ctx->coords.scale;
    while (n < cap && S->ct_count > 0) {
        const cw_contact *c = &S->ct[S->ct_head];
        sa1_contact *o = &out[n++];
        float v[3];
        uint32_t k;
        memset(o, 0, sizeof *o);
        o->object = c->object;
        o->type = c->type;
        o->flags = c->flags;
        o->step = c->step;
        sa1_to_host_point(ctx, c->point, o->point);
        sa1_to_host_dir(ctx, c->normal, o->normal);
        for (k = 0; k < 3; k++) {
            v[k] = c->v0[k] * 60.0f;
        }
        sa1_to_host_point(ctx, v, o->velocity);
        for (k = 0; k < 3; k++) {
            v[k] = c->v1[k] * 60.0f;
        }
        sa1_to_host_point(ctx, v, o->velocity_after);
        o->depth = c->depth * sc;
        S->ct_head = (S->ct_head + 1u) % CW_CONTACTS;
        S->ct_count--;
    }
    return n;
}

/* ---- the refresh (inside each step, before the land collision update) ------------------------------ */
static void cw_step(sa1g_world *w, void *user)
{
    cw_state *S = cw_state_of(w);
    sa1g_player_ptrs pp;
    float p[3] = { 0.0f, 0.0f, 0.0f }, rad = CW_QUERY_RADIUS;
    int32_t pang_y = 0;
    uint32_t sub = 0, active = 0, k;
    int have_p = 0, have_any = 0;
    (void)user;
    if (S == NULL) {
        return;
    }
    S->stats.steps++;
    sa1g_world_player(w, &pp);
    if (pp.twp != 0) {
        const uint8_t *t = (const uint8_t *)cw_ptr(w, pp.twp);
        for (k = 0; k < 3; k++) {
            p[k] = cw_rdf(t + 0x20 + 4u * k);
        }
        pang_y = (int32_t)cw_rd32(t + 0x18);
        have_p = have_any = 1;
        if (pp.pwp != 0) {
            /* playerwk.p.height / 2: the radius 8C0730D8 gives csts (and its query) */
            float h = cw_rdf((const uint8_t *)cw_ptr(w, pp.pwp) + 0x110);
            if (h > 0.0f && h < 1000.0f) {
                rad = 0.5f * h;
            }
        }
    } else {
        uint32_t cam = 0;
        sa1g_world_rd32(w, SA1G_ADDR_CAMERA_TWP, &cam);
        if (cam != 0 && ((cam >> 24) & 31u) == SA1G_SEG_ARENA) {
            const uint8_t *t = (const uint8_t *)cw_ptr(w, cam);
            for (k = 0; k < 3; k++) {
                p[k] = cw_rdf(t + 0x20 + 4u * k);
            }
            have_any = 1;
        }
    }
    sa1g_world_rd32(w, SA1G_ADDR_SUB_STEP, &sub);
    if (S->ctx->col_objects.count != 0 || S->stats.moving != 0) {
        if (have_p && S->stats.moving != 0) {
            cw_contacts(w, S);      /* before the objects move: the transforms the game collided with in this step */
        }
        cw_moving(w, S, have_p ? p : NULL, pang_y, sub, &active);
    }
    if (S->hitcap[0] != 0 || S->hitcap[1] != 0) {
        /* the land-collision calls of this step (the player's physics in the executor ran before this refresh) */
        cw_hitcap_step(w, S, &active, have_any ? p : NULL);
    }
    if (S->grid != 0) {
        if (have_any) {
            cw_window(w, S, p, (uint32_t)w->chr, have_p, rad, &active);
        } else {
            cw_wr16(cw_ptr(w, S->hdr), 0u);
        }
    }
    if (S->nearcap != 0) {
        /* the NEAR-poly queries of this step (the player's physics in the executor ran before this refresh) */
        uint8_t *nc = (uint8_t *)cw_ptr(w, S->nearcap);
        uint32_t full = cw_rd32(nc + CW_NEARCAP_FULL);
        S->stats.near_peak = cw_rd32(nc + CW_NEARCAP_PEAK);
        if (S->stats.near_peak > S->stats.near_max) {
            S->stats.near_max = S->stats.near_peak;
        }
        cw_wr32(nc + CW_NEARCAP_PEAK, 0u);
        if (full != S->near_full) {
            S->stats.over_near_polys++;
            cw_capacity(w, S, &active, SA1_CAPACITY_NEAR_POLYGONS, full - S->near_full, have_any ? p : NULL);
            S->near_full = full;
        }
    }
    if (S->mx_pending) {
        active |= 1u << SA1_CAPACITY_MATRIX_STACK;
        S->mx_pending = 0;
    }
    S->cap_active = active;
}

void sa1ca_world_step_begin(struct sa1g_world *w)
{
    cw_state *S = cw_state_of(w);
    uint32_t depth = 0, base = 0, limit = 0, slots;
    if (S != NULL && S->stats.moving != 0) {
        /* contacts (API 0.3): player 0's velocity entering the step (read only) */
        sa1g_player_ptrs pp;
        uint32_t k;
        sa1g_world_player(w, &pp);
        for (k = 0; k < 3; k++) {
            S->ct_v0[k] = pp.mwp != 0 ? cw_rdf((const uint8_t *)cw_ptr(w, pp.mwp) + 0x04 + 4u * k) : 0.0f;
        }
    }
    if (S == NULL || S->stats.matrix_base == 0 || sa1g_world_rd32(w, CW_ADDR_MTX_DEPTH, &depth) != 0
        || depth <= S->stats.matrix_base) {
        return;
    }
    if (sa1g_world_rd32(w, CW_ADDR_MTX_BASE, &base) != 0 || sa1g_world_rd32(w, CW_ADDR_MTX_LIMIT, &limit) != 0
        || depth > limit) {
        return;     /* not njInitMatrix's stack: leave it */
    }
    slots = depth - S->stats.matrix_base;
    sa1g_world_wr32(w, CW_ADDR_MTX_DEPTH, S->stats.matrix_base);
    sa1g_world_wr32(w, CW_ADDR_MTX_PTR, base + 64u * (S->stats.matrix_base - 1u));
    S->stats.matrix_restores++;
    S->stats.matrix_slots += slots;
    if (!(S->cap_active & (1u << SA1_CAPACITY_MATRIX_STACK))) {
        sa1g_player_ptrs pp;
        float p[3] = { 0.0f, 0.0f, 0.0f };
        uint32_t k;
        sa1g_world_player(w, &pp);
        if (pp.twp != 0) {
            const uint8_t *t = (const uint8_t *)cw_ptr(w, pp.twp);
            for (k = 0; k < 3; k++) {
                p[k] = cw_rdf(t + 0x20 + 4u * k);
            }
        }
        sa1g_world_event_push(w, SA1_EVENT_CAPACITY, SA1_CAPACITY_MATRIX_STACK, 0u, p, slots);
        S->cap_active |= 1u << SA1_CAPACITY_MATRIX_STACK;
    }
    S->mx_pending = 1;
}
