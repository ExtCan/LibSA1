/*
 * sa1_world_boot.c - world boot, player creation and the replay executor (sadc
 * docs/libsa1_integration.md 6.2-6.7, 9.1 V3-V5, 11 M0/M1).
 *
 * Boot (6.2) runs inside the world on an applied image, every game step through the code map:
 *   runtime   njInitMatrix(B_8C7EC8E0, 128, 0), the screen of njInitSystem / njInitView (640 x 480, aspect
 *             1, clip rectangle (0, 0, 639, 479)) and the screen and draw state of 8C0544E2 (640 x 480,
 *             projection angle 0x31C7: distance 457.06), srand (the game's srand(0xDEAD0CAB) by default), the
 *             shape-motion buffer of the game init 8C0546C0 (B_8C89002C = B_8C782984);
 *   memory    heap 8C098A10 (0x90000 at B_8C7EE8E0, 0xFF fill), tasks 8C09844A (0x1000 x 0x30);
 *   collision init 8C028E60, CCL counters 8C02F81A;
 *   globals   the tick contract of 6.5 (B_8C754EC4 / EC8, Gravity 8C052A8A, CurrentCharacter, level /
 *             act, game mode 4, tcrf_CollisionDisplayVar -1, D_8C19B468 0, event flags, rand), the
 *             game init's B_8C754C00 = 1, rings 0 / lives 4 (8C08A04A), the game clock stopped (8C08A5B6,
 *             D_8C1BF198 = 0 as in the references) or started (SA1G_BOOT_CLOCK);
 *   texlist   the stage-start texlist B_8C79F0F0 of 8C0990DE (OBJ_REGULAR; sa1g_boot_stage_texlist), which the
 *             blob shadow's texture setup 8C099500 sets;
 *   camera    a level-0 adapter task (CreateElementalTask(2, 0, NULL): the game's own empty exec
 *             8C098440) whose taskwk is camera_twp D_8C18B7EC; the world writes the host camera into
 *             it before each live step, replay writes the record's camera blob at player entry (6.7);
 *   landtable what the landtable manager task 8C0519C0 sets up in its first exec (8C051A16, the object
 *             pool 8C052A00, D_8C19E96C = 400, the latched level / act, D_8C19E974 / 978 = 1) and
 *             B_8C7596F4 = an empty landtable (EMPTY), the stage's own (DISC) or the window landtable
 *             of the collision adapter (WINDOW, both in segment 0x18);
 *   player    LoadPlayerTask 8C04F3C0 for player 0 (6.3): clear the slots 8C06C300,
 *             CreateElementalTask(7, 1, <main>), counter.b[1] = character, counter.b[0] = 0,
 *             playertwp[0] / playermwp[0], the start position of 8C04F8FA (the API's, or the game's
 *             start table), the position history 8C07CEF2(0, 0, &pos, NULL), the game clock reset
 *             8C08A580 and E-102's timer set-up; the main's first exec (step 0) runs PInitialize;
 *   stage     SA1G_BOOT_STAGE_TASKS: the stage's own tasks (sa1_world_stage.c: the level's list-2 task and the
 *             level task of the stage start, real code or a stand-in), created before the player as there;
 *   death zone task 8C07BAC0 (SA1G_BOOT_DEATHZONE; created after the player as the stage start does);
 *   item icons the item icon list task of 8C05E0CE (round 0.9.2; next in the stage start's order, every world with a
 *             player): the icons of the items picked up and the bomb item's sphere;
 *   effects   the particle system of the stage start (8C09DDDA(B_8C7493D4, 0x200), then the lists of 8C09E2DA
 *             through 8C09E2A0, all decompiled): the 512-node pool and the effect lists of the common table
 *             D_8C1C40C4 (and of the level's table D_8C1C4140[level] only when the image holds the stage overlay,
 *             whose data that table points into: 8C09E2DA itself reads it unconditionally; round 6a: or when the table
 *             and its definitions lie in 1ST_READ, levels 19 and 39), so the decompiled
 *             spawners (8C0465E0, 8C046A08 ...) allocate particles and draw rand() as in the game; the step runs
 *             the particle manager 8C09DF64, which recycles the nodes of ended particles;
 *   then one land collision update 8C028EC2 (unless SA1G_BOOT_NO_WORLD_UPDATE: the stage start builds
 *   the cache only at the end of step 0, which M1b reproduces).
 *
 * Replay executor (sa1g_run_tasks): func_8C0988A4's walk (list 7 as level 8, then 0..6; exec and next
 * read before the call, D_8C1C1D40 = the running exec) done by the world, so that
 *   - the record's player-entry state (trace v2 px_*: rand, XMTRX, camera taskwk, frame counter, rings)
 *     is written at the first entry of player 0's exec in the step, where the reference captured it;
 *   - execs can be overridden (sa1g_world_xhook) and execs without native code frozen
 *     (freeze_unknown): the RAM transplant of 9.1 V4 / M1a keeps the camera 8C0197BA, the sky
 *     8C052FA0, the stage tasks and the sound / effect tasks of the reference RAM as frozen tasks and
 *     runs the landtable manager's mode-0 set-up without its per-frame draw step 8C051D50.
 */
#include "sa1_world.h"

#include <stdio.h>
#include <string.h>

/* ---- guest memory of the ticking world (arena only; anything else is a world fault) -------------- */
static uint32_t gr32(sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (sa1g_world_rd32(w, g, &v) != 0) {
        sa1g_fatal(0, "world: guest word outside the arena");
    }
    return v;
}

static void gw32(sa1g_world *w, uint32_t g, uint32_t v)
{
    if (sa1g_world_wr32(w, g, v) != 0) {
        sa1g_fatal(0, "world: guest word outside the arena");
    }
}

static void gw16(sa1g_world *w, uint32_t g, uint16_t v)
{
    memcpy((uint8_t *)sa1g_world_ptr(w, g), &v, 2);
}

static uint16_t gr16(sa1g_world *w, uint32_t g)
{
    uint16_t v;
    memcpy(&v, (const uint8_t *)sa1g_world_ptr(w, g), 2);
    return v;
}

static void gwf(sa1g_world *w, uint32_t g, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    gw32(w, g, v);
}

static int sa1g_in_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static void *gfn(uint32_t dc)
{
    return sa1_fn(SA1_GFN_C(dc), 0u);
}

/* round 0.9.2: the stage start's item icon list task (8C058E48 -> 8C05E0CE; sadc code_8C05E0CE.c) */
#define SA1G_FN_ITEM_ICONS      0x8C05E0CEu

/* ---- character mains and start tables (8C04F3C0, 8C04F8FA) ------------------------------------- */
typedef struct sa1g_chr_desc {
    int32_t chr;
    uint32_t main;              /* the exec LoadPlayerTask gives player 0 */
    uint32_t start_table;       /* {Sint16 level, act; Float pos[3]; Angle ang_y} x n, level 0x2B ends */
} sa1g_chr_desc;

static const sa1g_chr_desc k_chr[] = {
    { 0, 0x8C0A77BCu, 0x8C19C4F8u },    /* Sonic */
    { 2, 0x8C0B6148u, 0x8C19CA98u },    /* Tails */
    { 3, 0x8C0C0640u, 0x8C19CDCCu },    /* Knuckles */
    { 5, 0x8C0D6B40u, 0x8C19D394u },    /* Amy */
    { 6, 0x8C0CBE00u, 0x8C19D6C8u },    /* E-102 */
    { 7, 0x8C0DFFA0u, 0x8C19D0D8u },    /* Big */
};

static const sa1g_chr_desc *sa1g_chr_of(int32_t chr)
{
    size_t i;
    for (i = 0; i < sizeof k_chr / sizeof k_chr[0]; i++) {
        if (k_chr[i].chr == chr) {
            return &k_chr[i];
        }
    }
    return NULL;
}

uint32_t sa1g_character_main(int32_t chr)
{
    const sa1g_chr_desc *d = sa1g_chr_of(chr);
    return d ? d->main : 0u;
}

static int sa1g_is_main(uint32_t exec)
{
    size_t i;
    for (i = 0; i < sizeof k_chr / sizeof k_chr[0]; i++) {
        if ((k_chr[i].main & 0x1FFFFFFFu) == (exec & 0x1FFFFFFFu)) {
            return 1;
        }
    }
    return 0;
}

void sa1g_boot_defaults(sa1g_boot_cfg *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->chr = 0;
    cfg->level = 1;
    cfg->act = 0;
    cfg->seed = 0xDEAD0CABu;
    cfg->land_mode = SA1G_LAND_EMPTY;
}

/* ---- boot pieces ------------------------------------------------------------------------------------ */
/* The screen of njInitSystem 8C604702 (the game's video init 8C010180 calls njInitSystem(1, 0, 1): mode 1, the VGA
 * mode the reference simulator's BIOS reports; NTSC / PAL give the same 640 x 480) as the reference simulator sets it
 * since 2026-10-20 (its default; trace header flag 8; sadc docs/research/visibility_fidelity.md): the mode words
 * B_8C88F620 / 24 / 28, the screen size as floats (B_8C88F5E8 / EC) and integers (B_8C88F644 / 48), and the part of
 * njInitView 8C604BA0 the game reads - aspect 1, 1 (B_8C88F618 / 1C) and the 2D clip rectangle (0, 0, w - 1, h - 1)
 * (B_8C88F600..0C). njInitView's other words (distance 500, centre, near / far) are overwritten by 8C0544E2, as in
 * sa1g_boot_runtime; its Kamui context selection (B_8C88FCB4 = &B_8C88F834[0]) is left out: only the SDK's own draws
 * read the context, and the runtime's draws model the NULL context (rt_draw.c 1.). Before 2026-10-20 the simulator
 * stubbed njInitSystem and all of these stayed 0 (so did the distance below): the sphere visibility test 8C038D00
 * never passed (tests that replay such a reference zero them again: tests/world_replay.c wr_screen_of). */
#define SA1G_ADDR_SCREEN_DIST   0x8C88F5E4u     /* B_8C88F5E4: distance (njSetPerspective) */
#define SA1G_ADDR_SCREEN_W      0x8C88F5E8u     /* B_8C88F5E8 / EC: screen width / height (float) */
#define SA1G_ADDR_SCREEN_CLIP   0x8C88F600u     /* B_8C88F600..0C: 2D clip rectangle x0, y0, x1, y1 */
#define SA1G_ADDR_SCREEN_ASPECT 0x8C88F618u     /* B_8C88F618 / 1C: aspect x, y */
#define SA1G_ADDR_SCREEN_MODE   0x8C88F620u     /* B_8C88F620 / 24 / 28: njInitSystem's mode, frame, count */
#define SA1G_ADDR_SCREEN_WI     0x8C88F644u     /* B_8C88F644 / 48: screen width / height (integer) */
#define SA1G_ADDR_SCREEN_NEAR   0x8C88F610u     /* B_8C88F610 / 14: near / far clip (njClipZ) */
static void sa1g_boot_screen(sa1g_world *w)
{
    gw32(w, SA1G_ADDR_SCREEN_MODE, 1u);
    gw32(w, SA1G_ADDR_SCREEN_MODE + 4u, 0u);
    gw32(w, SA1G_ADDR_SCREEN_MODE + 8u, 1u);
    gwf(w, SA1G_ADDR_SCREEN_W, 640.0f);
    gwf(w, SA1G_ADDR_SCREEN_W + 4u, 480.0f);
    gw32(w, SA1G_ADDR_SCREEN_WI, 0x280u);
    gw32(w, SA1G_ADDR_SCREEN_WI + 4u, 0x1E0u);
    gwf(w, SA1G_ADDR_SCREEN_ASPECT, 1.0f);
    gwf(w, SA1G_ADDR_SCREEN_ASPECT + 4u, 1.0f);
    gwf(w, SA1G_ADDR_SCREEN_CLIP, 0.0f);
    gwf(w, SA1G_ADDR_SCREEN_CLIP + 4u, 0.0f);
    gwf(w, SA1G_ADDR_SCREEN_CLIP + 8u, 639.0f);
    gwf(w, SA1G_ADDR_SCREEN_CLIP + 12u, 479.0f);
}

/* the runtime part of 8C0544E2 (Ninja set-up after the SDK init): the matrix stack, the view
 * 640 x 480 (B_8C788A00..0C), the SDK screen centre (8C640528) and distance (8C640500(0x31C7):
 * B_8C88F5E8 / (2 njTan(0x31C7 / 2)) = 457.06 with njInitSystem's 640, sa1g_boot_screen), the
 * depth clip of its njClipZ(-3, -3000) (8C64036C clamps both to [-65535, -1] and stores them
 * negated: near 3, far 3000 in B_8C88F610 / 14, as in the reference RAM after the stage start;
 * the game's camera, which libsa1 worlds do not run, later sets each stage's far clip and changes
 * the distance with the view angle), the draw state (control 3D 0x36C, constant attr ~0 / 0x2000,
 * constant material 0) and srand(seed) */
static void sa1g_boot_runtime(sa1g_world *w, uint32_t seed)
{
    float (*tan_fn)(int32_t) = (float (*)(int32_t))gfn(0x8C63A9F4u);    /* njTan */
    float w2 = 640.0f * 0.5f, h2 = 480.0f * 0.5f, t, d = 0.0f, mat[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    uint32_t v;
    sa1g_boot_screen(w);
    ((void (*)(float *, int32_t, int32_t))gfn(SA1G_FN_INIT_MATRIX))((float *)SA1_GV(SA1G_ADDR_MATRIX_STACK), 128, 0);
    gwf(w, 0x8C788A00u, 640.0f);
    gwf(w, 0x8C788A04u, 480.0f);
    gwf(w, 0x8C788A08u, w2);
    gwf(w, 0x8C788A0Cu, h2);
    gwf(w, 0x8C88F5F0u, w2);
    gwf(w, 0x8C88F5F4u, h2);
    t = tan_fn(0x31C7 >> 1);
    v = gr32(w, SA1G_ADDR_SCREEN_W);
    memcpy(&d, &v, 4);
    gwf(w, SA1G_ADDR_SCREEN_DIST, d / (t + t));
    gwf(w, SA1G_ADDR_SCREEN_NEAR, 3.0f);
    gwf(w, SA1G_ADDR_SCREEN_NEAR + 4u, 3000.0f);
    ((void (*)(uint32_t))gfn(0x8C60EDE0u))(0x36Cu);
    ((void (*)(uint32_t, uint32_t))gfn(0x8C60EDE6u))(0xFFFFFFFFu, 0x2000u);
    ((void (*)(void *))gfn(0x8C60EDF0u))(mat);
    ((void (*)(uint32_t))gfn(0x8C10D39Cu))(seed);    /* srand */
    /* the game init 8C0546C0: njInitShape(&B_8C782984) (8C60D1F2 only stores the buffer; the motion
     * player's shape motions write through it) */
    gw32(w, SA1G_ADDR_SHAPE_BUFFER, SA1G_SHAPE_BUFFER);
}

/* the landtable manager's mode-0 set-up (8C0519C0 -> 8C051A16, 8C052A00, D_8C19E96C, 8C051CC0) with
 * a given landtable; mgr_twp = the manager's taskwk (0: none) */
static void sa1g_land_setup(sa1g_world *w, uint32_t landtable, uint32_t mgr_twp)
{
    ((void (*)(void *))gfn(SA1G_FN_LANDMGR_RESET))(sa1_decode(0));    /* ignores its task */
    gw32(w, SA1G_ADDR_LAND_MGR_TWP, mgr_twp);
    sa1g_call_dc(SA1G_FN_OBJPOOL_CLEAR);
    gwf(w, SA1G_ADDR_LAND_RANGE, 400.0f);
    /* 8C051CC0: latch the stage (the manager is in mode 0), enable the gathers */
    *(int8_t *)sa1g_world_ptr(w, SA1G_ADDR_LAND_LEVEL) = (int8_t)(int16_t)gr16(w, SA1G_ADDR_CUR_LEVEL);
    *(int8_t *)sa1g_world_ptr(w, SA1G_ADDR_LAND_LEVEL + 1u) = (int8_t)(int16_t)gr16(w, SA1G_ADDR_CUR_ACT);
    gw32(w, SA1G_ADDR_LANDTABLE, landtable);
    gw32(w, SA1G_ADDR_LAND_PRESENT, landtable != 0);
    gw32(w, SA1G_ADDR_LAND_GATHER, landtable != 0);
}

/* ---- the particle system of the stage start (8C04E660: 8C09DDDA(B_8C7493D4, 0x200), then 8C09E2DA) ---------- */
#define SA1G_ADDR_FX_POOL       0x8C7493D4u     /* B_8C7493D4: 0x200 particle nodes of 0x4C bytes */
#define SA1G_FX_NODES           0x200u
#define SA1G_ADDR_FX_COMMON     0x8C1C40C4u     /* D_8C1C40C4: entries of the common effect lists */
#define SA1G_ADDR_FX_LEVELS     0x8C1C4140u     /* D_8C1C4140[level]: the level's entries (stage overlay data) */
#define SA1G_FN_FX_INIT         0x8C09DDDAu     /* the pool, its free list (8C09DDA0), no lists (code_8C09DDA0.c) */
#define SA1G_FN_FX_LISTS        0x8C09E2A0u     /* the lists of a definition table {desc, extra, init, list} until
                                                   NULL: list = 8C09DE3A(desc, extra), init(list) (code_8C09E2A0.c) */

/* the world image holds the stage overlay STGnn of level (DISC mode, stage tasks) */
static int sa1g_image_has_stage(const sa1g_world *w, int32_t level)
{
    char name[16];
    uint32_t i;
    snprintf(name, sizeof name, "STG%02d.PRS", (int)level);
    for (i = 0; w->image != NULL && i < w->image->num_spans; i++) {
        if (strcmp(w->image->spans[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* round 6a (super_sonic.md Review R.3.1): the level's table D_8C1C4140[level] and every definition it lists lie in
 * 1ST_READ (levels 19 and 39: level 19's single entry 0x8C19C06C is also the common table's tenth, so the game creates the
 * D_8C19C078 list twice there; 8C046A9C spawns Super Sonic's water splash into the second). Tables without an entry
 * (levels 1, 4) create nothing either way. */
#define SA1G_FIRST_READ_END     0x8C67C680u     /* 1ST_READ.BIN's end (the BSS starts here) */
static int sa1g_in_base(uint32_t g)
{
    return (g & 0x1FFFFFFFu) >= 0x0C010000u && (g & 0x1FFFFFFFu) < (SA1G_FIRST_READ_END & 0x1FFFFFFFu);
}

/* every definition {desc, extra, init, list} of the table, its descriptor and its init (when it has one) in 1ST_READ */
static int sa1g_fx_level_in_base(sa1g_world *w, uint32_t t)
{
    uint32_t k, e, init;
    if (!sa1g_in_base(t)) {
        return 0;
    }
    for (k = 0; k < 64u; k++) {
        e = gr32(w, t + 4u * k);
        if (e == 0) {
            return k != 0;
        }
        init = gr32(w, e + 8u);
        if (!sa1g_in_base(e) || !sa1g_in_base(gr32(w, e)) || (init != 0 && !sa1g_in_base(init))) {
            return 0;
        }
    }
    return 0;
}

static void sa1g_boot_effects(sa1g_world *w, int level_table)
{
    void (*lists)(void *) = (void (*)(void *))gfn(SA1G_FN_FX_LISTS);
    uint16_t lv;
    /* 8C09DDDA(pool, n): the pool and its size, the free list 8C09DDA0, no lists yet, B_8C7A0D4C = 0.0f */
    ((void (*)(void *, uint32_t))gfn(SA1G_FN_FX_INIT))(sa1_decode(SA1G_ADDR_FX_POOL), SA1G_FX_NODES);
    /* 8C09E2DA: the common lists, then the level's (its entries are stage overlay data: only with the stage in the
     * image, or - round 6a - when the table and its definitions lie in 1ST_READ) */
    lists(sa1_decode(SA1G_ADDR_FX_COMMON));
    lv = gr16(w, SA1G_ADDR_CUR_LEVEL);
    if (lv < 0x2Bu) {
        uint32_t t = gr32(w, SA1G_ADDR_FX_LEVELS + 4u * lv);
        if (t != 0 && (level_table || sa1g_fx_level_in_base(w, t))) {
            lists(sa1_decode(t));
        }
    }
}

/* ---- the stage-start texlist of the blob shadow (8C04E490 -> 8C0990DE(mode)) ------------------------------------ */
#define SA1G_ADDR_STAGE_TEXLISTS 0x8C1C24A8u    /* PTR_8C1C24A8[mode]: {char *name; NJS_TEXLIST *list}[] of 8C0990DE */
#define SA1G_ADDR_STAGE_TEXLIST  0x8C79F0F0u    /* B_8C79F0F0: entry 0's texlist (sadc code_8C099500.c) */
/* The stage start 8C04E490 calls 8C0990DE(2) for levels 0x24 / 0x25 and 8C0990DE(0) otherwise. 8C0990DE (no C yet)
 * stores the texlist of entry 0 of PTR_8C1C24A8[mode] in B_8C79F0F0 (mode 0: OBJ_REGULAR 0x8C19F530; mode 2:
 * EFF_REGULAR 0x8C1C3068) and loads every entry's archive (8C098EE0 / 8C099750); the shadow texture setup 8C099500
 * then sets B_8C79F0F0 before every Character_Shadow_Blob draw. This stand-in does the store, read from the image as
 * the stage start reads it; the loads are the host's: sa1_loader.c loads OBJ_REGULAR and EFF_REGULAR as every
 * character's common archives, so the capture resolves the blob's texture 58 "shadow" into OBJ_REGULAR (sadc
 * docs/research/gsa1_060_shadow.md L1 / L2). Boots only: a transplant keeps its RAM image's own (Review R.3.6). The
 * level's own object texlists 8C099220 stores next (B_8C79F0F4.., read by 8C099538) stay 0: their archives are not
 * loaded. */
static void sa1g_boot_stage_texlist(sa1g_world *w)
{
    uint32_t lv = gr16(w, SA1G_ADDR_CUR_LEVEL) & 0xFFu, e;
    e = gr32(w, SA1G_ADDR_STAGE_TEXLISTS + 4u * ((lv == 0x24u || lv == 0x25u) ? 2u : 0u));
    gw32(w, SA1G_ADDR_STAGE_TEXLIST, e != 0 ? gr32(w, e + 4u) : 0u);
}

/* D_8C19E9FC[level][act] of the arena */
static uint32_t sa1g_stage_landtable(sa1g_world *w, int32_t level, int32_t act)
{
    if (level < 0 || level >= 0x2B || act < 0 || act >= 8) {
        return 0;
    }
    return gr32(w, SA1G_ADDR_LANDTABLES + 4u * (uint32_t)(level * 8 + act));
}

/* an empty landtable in segment 0x18 with room for cap COL entries (EMPTY / WINDOW) */
static uint32_t sa1g_window_create(sa1g_world *w, uint32_t cap)
{
    uint8_t *hdr;
    uint32_t lt, cols;
    lt = sa1g_world_ext_alloc(w, 0x24u, 16, (void **)&hdr);
    cols = sa1g_world_ext_alloc(w, (size_t)cap * 0x24u, 16, NULL);
    if (lt == 0 || cols == 0) {
        sa1g_fatal(0, "world: no room for the window landtable in segment 0x18");
    }
    memcpy(hdr + 0x0C, &cols, 4);       /* pLandEntry; ssCount 0, everything else 0 */
    w->window_cols = cols;
    w->window_cap = cap;
    return lt;
}

int sa1g_world_window(sa1g_world *w, void **header, void **cols, uint32_t *cap)
{
    if (w == NULL || w->window_cols == 0 || w->landtable == 0) {
        return -1;
    }
    if (header) {
        *header = sa1g_world_ptr(w, w->landtable);
    }
    if (cols) {
        *cols = sa1g_world_ptr(w, w->window_cols);
    }
    if (cap) {
        *cap = w->window_cap;
    }
    return 0;
}

/* the start position of 8C04F8FA: {Sint16 level, act; Float pos[3]; Angle ang.y}, 20 B per entry */
static void sa1g_start_from_table(sa1g_world *w, const sa1g_chr_desc *d, uint8_t *twp)
{
    uint32_t e = d->start_table, k;
    int16_t level = (int16_t)gr16(w, SA1G_ADDR_CUR_LEVEL), act = (int16_t)gr16(w, SA1G_ADDR_CUR_ACT);
    for (k = 0; k < 256; k++, e += 20u) {
        int16_t l = (int16_t)gr16(w, e), a = (int16_t)gr16(w, e + 2u);
        if (l == level && a == act) {
            uint32_t ang = gr32(w, e + 16u), p0 = gr32(w, e + 4u), p1 = gr32(w, e + 8u), p2 = gr32(w, e + 12u);
            memcpy(twp + 0x18, &ang, 4);
            memcpy(twp + 0x20, &p0, 4);
            memcpy(twp + 0x24, &p1, 4);
            memcpy(twp + 0x28, &p2, 4);
            return;
        }
        if (l == 0x2B) {
            break;
        }
    }
    memset(twp + 0x18, 0, 4);           /* no entry: (0, 0, 0), as the game does */
    memset(twp + 0x20, 0, 12);
}

/* LoadPlayerTask 8C04F3C0 for player 0, with 8C04F8FA's start position */
static uint32_t sa1g_create_player(sa1g_world *w, const sa1g_boot_cfg *c)
{
    void *(*create)(uint16_t, int32_t, sa1_gfn) = (void *(*)(uint16_t, int32_t, sa1_gfn))gfn(SA1G_FN_CREATE_TASK);
    const sa1g_chr_desc *d = sa1g_chr_of(c->chr);
    uint8_t *tp, *twp;
    uint32_t gtp, gtwp, gmwp, k;
    if (d == NULL) {
        sa1g_fatal(0, "boot: no character main for this character id");
    }
    sa1g_call_dc(SA1G_FN_PLAYER_SLOTS);
    tp = (uint8_t *)create(7, 1, SA1_GFN_C(d->main));
    if (tp == NULL) {
        sa1g_fatal(SA1G_FN_CREATE_TASK, "boot: CreateElementalTask failed for player 0");
    }
    gtp = sa1_encode(tp);
    gtwp = gr32(w, gtp + 0x20u);
    gmwp = gr32(w, gtp + 0x24u);
    twp = (uint8_t *)sa1g_world_ptr(w, gtwp);
    twp[9] = (uint8_t)c->chr;           /* counter.b[1]: character */
    twp[8] = 0;                         /* counter.b[0]: player number */
    gw32(w, SA1G_ADDR_PLAYER_TWP, gtwp);
    gw32(w, SA1G_ADDR_PLAYER_MWP, gmwp);
    /* 8C04F8FA (no restart point: 8C064E92 = 0) */
    if (gr16(w, 0x8C7493C0u) == gr16(w, SA1G_ADDR_CUR_ACT)) {
        sa1g_call_dc(SA1G_FN_TIMER_RESET);
    }
    if (c->chr == 6) {
        ((void (*)(int8_t, int8_t))gfn(0x8C08AB10u))(3, 0);
    }
    if (c->flags & SA1G_BOOT_POS) {
        for (k = 0; k < 3; k++) {
            memcpy(twp + 0x14 + 4u * k, &c->ang[k], 4);
            memcpy(twp + 0x20 + 4u * k, &c->pos[k], 4);
        }
    } else {
        sa1g_start_from_table(w, d, twp);
    }
    ((int32_t (*)(uint8_t, int32_t, void *, void *))gfn(SA1G_FN_POS_HISTORY))(0, 0, twp + 0x20, NULL);
    if (c->chr == 6) {
        uint8_t mm = 0, ss = 0;
        ((void (*)(void *, void *))gfn(0x8C08AB02u))(&mm, &ss);
        if ((uint32_t)mm * 60u + ss < 60u) {
            ((void (*)(int8_t, int8_t))gfn(0x8C08AB10u))(1, 0);
        }
    }
    /* level < 15: 8C064EF6 stores the checkpoint (a void stub: the host owns respawns) */
    /*
     * Not done (design D2: one character per world, player 0 only): LoadPlayerTask's second iteration (slot 1),
     * 8C081AC0(player 0's task) for Sonic (the Tails AI follower) and 8C07F560(chr, 1, level), which for Tails
     * only (chr 2) sets B_8C79884C = 0, D_8C1BEBE4 = 8C0816F4() and, per level, the race: Sonic as player 1
     * (SonicMain, counter.b[0] = 1, id 2, with the exec 8C07F74A task) on levels 2, 6, 8, 9, and on Speed
     * Highway act 0 B_8C798850 = 1 with the race task 8C07FC4C (-> 8C07FC70; the reference on SH1 also holds the race
     * gauge task 8C067456, set up by 8C08173E / 8C081764 with the gauge texlist D_8C1BE9C0 that 8C0817E0 draws).
     * Their code has no C; no decompiled function reads B_8C798850 / D_8C1BEBE4, and the race result B_8C79884C
     * that Tails' mode code (code_8C0BBDD6.c, code_8C0BC61C.c, code_8C0BCBCA.c) tests through 8C0818DE stays 0 as
     * in the game until a race ends. tails_sh1 replays identically without them (Phase 7, tests/replay_test.c): the
     * SH1 race task's 3 rand() per step run before the player and are absorbed by the replay's px_rand injection.
     */
    return gtp;
}

typedef struct sa1g_boot_run {
    const sa1g_boot_cfg *cfg;
} sa1g_boot_run;

static void sa1g_boot_cb(sa1g_world *w, void *user)
{
    const sa1g_boot_cfg *c = ((sa1g_boot_run *)user)->cfg;
    void *(*create)(uint16_t, int32_t, sa1_gfn) = (void *(*)(uint16_t, int32_t, sa1_gfn))gfn(SA1G_FN_CREATE_TASK);
    uint32_t lt = 0, k;
    /* 2. runtime */
    sa1g_boot_runtime(w, c->seed);
    /* 3. heap and tasks */
    sa1g_call_dc(SA1G_FN_HEAP_INIT);
    sa1g_call_dc(SA1G_FN_TASK_INIT);
    /* 4. land collision cache, CCL lists */
    (void)sa1g_call_dc_i32(SA1G_FN_COL_INIT);
    sa1g_call_dc(SA1G_FN_CCL_RESET);
    /* 5. the globals of 6.5 */
    gw32(w, SA1G_ADDR_STEPS_PER_FRAME, 2u);
    gw32(w, SA1G_ADDR_SUB_STEP, 0u);
    sa1g_call_dc(SA1G_FN_GRAVITY);
    gw16(w, SA1G_ADDR_CUR_CHAR, (uint16_t)c->chr);
    gw16(w, SA1G_ADDR_CUR_LEVEL, (uint16_t)c->level);
    gw16(w, SA1G_ADDR_CUR_ACT, (uint16_t)c->act);
    gw32(w, SA1G_ADDR_GAME_MODE, 4u);
    gw32(w, SA1G_ADDR_INIT_FLAG, 1u);
    sa1g_call_dc(SA1G_FN_NEW_GAME);
    sa1g_call_dc((c->flags & SA1G_BOOT_CLOCK) ? SA1G_FN_CLOCK_ON : SA1G_FN_CLOCK_OFF);
    *(int8_t *)sa1g_world_ptr(w, SA1G_ADDR_COLDISP) = -1;
    gw16(w, SA1G_ADDR_FORCE_MODE83, 0);
    for (k = 0; k < 128u; k++) {
        if (c->event_flags[k >> 5] & (1u << (k & 31u))) {
            *(uint8_t *)sa1g_world_ptr(w, SA1G_ADDR_EVENT_FLAGS + k) = 1;
        }
    }
    /* 5b. the stage-start texlist of the blob shadow (8C0990DE's store) */
    sa1g_boot_stage_texlist(w);
    /* the camera adapter (6.7) */
    if (!(c->flags & SA1G_BOOT_NO_CAMERA)) {
        uint8_t *tp = (uint8_t *)create(2, 0, SA1_GFN_NULL);
        if (tp == NULL) {
            sa1g_fatal(SA1G_FN_CREATE_TASK, "boot: CreateElementalTask failed for the camera adapter");
        }
        w->camera_tp = sa1_encode(tp);
        w->camera_twp = gr32(w, w->camera_tp + 0x20u);
        gw32(w, SA1G_ADDR_CAMERA_TWP, w->camera_twp);
    }
    /* the landtable */
    switch (c->land_mode) {
    case SA1G_LAND_EMPTY:
    case SA1G_LAND_WINDOW:
        lt = sa1g_window_create(w, c->window_cap ? c->window_cap : 191u);
        break;
    case SA1G_LAND_DISC:
        lt = c->landtable ? c->landtable : sa1g_stage_landtable(w, c->level, c->act);
        if (lt == 0) {
            sa1g_fatal(0, "boot: DISC mode without a landtable for this stage");
        }
        break;
    default:
        break;
    }
    w->landtable = lt;
    w->land_mode = c->land_mode;
    if (c->land_mode != SA1G_LAND_NONE) {
        sa1g_land_setup(w, lt, 0);
        gw32(w, SA1G_ADDR_LAND_ALL, 0);
    }
    /* 6. the stage's own tasks (the stage start creates them before LoadPlayerTask), player 0, then (stage
     * start order) the death zone task: created after the player, it runs before it (the head of list 1) */
    /* round 6a: a super-capable world (Super Sonic's) gets the Perfect Chaos level task (its stand-in, sa1_world_stage.c)
     * without SA1G_BOOT_STAGE_TASKS too (gsa1_rounds_plan.md 11.1) */
    w->super = c->super;
    if (c->super & SA1G_SUPER_GENERAL) {
        /* round 0.9.3a: a General world (any stage, B_CHAOS7 in the slot) gets the stand-in made directly, never through
           the level's exec tables (sa1_world_stage.c sa1g_stage_pc_create) */
        sa1g_stage_pc_create(w);
    } else if ((c->flags & SA1G_BOOT_STAGE_TASKS) || (c->super & SA1G_SUPER_WORLD)) {
        sa1g_stage_tasks(w);
    }
    if (!(c->flags & SA1G_BOOT_NO_PLAYER)) {
        w->player_tp = sa1g_create_player(w, c);
    }
    sa1g_super_boot(w);     /* round 6a: the revert fix (sa1_world_super.c; nothing in other worlds) */
    if (c->flags & SA1G_BOOT_DEATHZONE) {
        if (create(2, 1, SA1_GFN_C(SA1G_FN_DEATHZONE)) == NULL) {
            sa1g_fatal(SA1G_FN_CREATE_TASK, "boot: CreateElementalTask failed for the death zone task");
        }
    }
    /* 6b. round 0.9.2: the item icon list task, as the stage start makes it after the death zone task (8C04E660 ->
     * 8C058E48, whose last call is the JMP 8C05E0CE at 8C058EB6), so every reference world has it: 8C05E0CE creates
     * CreateElementalTask(2, 3, 8C05E14E) (disp 8C05E33C, dest 8C05E57E), empties the list B_8C78C394 and turns the bomb
     * sphere off (B_8C78C390 = -1). Its exec slides in, holds and shrinks the icon of every item picked up (8C05E0F8
     * appends; the exec draws through its disp: njDrawSprite2D, sa1_character_screen's SA1_HUD_CHARACTER) and then empties
     * the list (the game's list has no bound: without the task the 21st item of a world wrote over B_8C78C4E0), and it
     * grows the bomb item's sphere and ends it past 500 (8C05E59A; without it the sphere kept radius 10 and the enemy
     * test 8C05E5C2 kept firing at the bomb point). In mode 0 it does nothing (sadc docs/research/gsa1_090_faults.md 2.4,
     * F2 with its Review R.3.2; gsa1_090_plan.md 6.4). */
    if (!(c->flags & SA1G_BOOT_NO_PLAYER)) {
        sa1g_call_dc(SA1G_FN_ITEM_ICONS);
    }
    /* 7. the particle system (the stage start's order: after the player and the death zone task) */
    sa1g_boot_effects(w, sa1g_image_has_stage(w, c->level));
    if (!(c->flags & SA1G_BOOT_NO_PLAYER) && !(c->flags & SA1G_BOOT_NO_WORLD_UPDATE)) {
        (void)sa1g_call_dc_i32(SA1G_FN_LAND_UPDATE);
    }
}

sa1_status sa1g_world_boot(sa1g_world *w, const sa1g_boot_cfg *cfg)
{
    sa1g_boot_run b;
    sa1_status st;
    if (w == NULL || cfg == NULL || w->reserve == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    if (w->booted || w->image == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;    /* boot once, on a freshly applied image */
    }
    if (!sa1g_game_ready()) {
        return SA1_ERR_NOT_IMPLEMENTED;
    }
    if (!(cfg->flags & SA1G_BOOT_NO_PLAYER) && sa1g_chr_of(cfg->chr) == NULL) {
        return SA1_ERR_UNSUPPORTED;         /* Eggman, Tikal (own create path, 6.3a) */
    }
    if (cfg->land_mode < SA1G_LAND_NONE || cfg->land_mode > SA1G_LAND_WINDOW) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    b.cfg = cfg;
    w->chr = cfg->chr;
    w->level = cfg->level;
    w->act = cfg->act;
    st = sa1g_world_call(w, sa1g_boot_cb, &b);
    if (st == SA1_OK) {
        w->booted = 1;
    }
    return st;
}

/* ---- start rings (API 0.3: sa1_character_desc.start_rings, sadc docs/libsa1_interaction.md 9.1 ring API) ---------- */
#define SA1G_FN_RINGS_ZERO  0x8C08A074u     /* B_8C752BE4 = 0 */
#define SA1G_FN_RINGS_ADD   0x8C08A198u     /* B_8C752BE4 += n, the 1-up per 100 (8C08A120) */

static void sa1g_start_rings_cb(sa1g_world *w, void *user)
{
    int32_t n = *(const int32_t *)user;
    (void)w;
    sa1g_call_dc(SA1G_FN_RINGS_ZERO);
    ((void (*)(int16_t))gfn(SA1G_FN_RINGS_ADD))((int16_t)n);
}

sa1_status sa1g_world_start_rings(sa1g_world *w, int32_t n)
{
    if (w == NULL || !w->booted) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (n < 0) {
        n = 0;
    }
    if (n > 99) {
        n = 99;     /* a start never gives a 1-up (round 6a's Super Sonic starts with 50) */
    }
    return sa1g_world_call(w, sa1g_start_rings_cb, &n);
}

/* ---- lives (round 0.9.1: sa1_character_set_lives, sadc docs/research/gsa1_070_drown.md E2 with its Review R4.6) ----- */
#define SA1G_ADDR_LIVES     0x8C752BE0u     /* Sint8 B_8C752BE0 (8C08A04A sets 4; 8C08A120(n) adds, an overflow capped at
                                               127) */

sa1_status sa1g_world_set_lives(sa1g_world *w, int32_t n)
{
    uint32_t v = 0;
    if (w == NULL || !w->booted) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    if (n < 0) {
        n = 0;
    }
    if (n > 127) {
        n = 127;
    }
    if (sa1g_world_rd32(w, SA1G_ADDR_LIVES, &v) != 0) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    v = (v & ~0xFFu) | (uint32_t)n;     /* the byte at 0x8C752BE0 (little-endian arena) */
    return sa1g_world_wr32(w, SA1G_ADDR_LIVES, v) == 0 ? SA1_OK : SA1_ERR_INVALID_ARGUMENT;
}

/* ---- transplant (9.1 V4) --------------------------------------------------------------------------- */
void sa1g_exec_landmgr_setup(sa1g_world *w, uint32_t tp, void *user)
{
    uint32_t twp = gr32(w, tp + 0x20u);
    uint8_t *t = (uint8_t *)sa1g_world_ptr(w, twp);
    int8_t level, act;
    (void)user;
    if (!sa1g_in_arena(twp) || t[0] != 0) {
        return;                         /* set up: the per-frame step 8C051D50 (drawing) is not run */
    }
    level = (int8_t)(int16_t)gr16(w, SA1G_ADDR_CUR_LEVEL);
    act = (int8_t)(int16_t)gr16(w, SA1G_ADDR_CUR_ACT);
    sa1g_land_setup(w, sa1g_stage_landtable(w, level, act), twp);
    t[0] = 1;                           /* twp->mode = 1 */
}

sa1_status sa1g_world_transplant(sa1g_world *w, const void *ram, size_t n)
{
    sa1_status st;
    if (w != NULL && (w->super & SA1G_SUPER_GENERAL)) {
        /* round 0.9.3a (review RC16): a General world's GT / GW live in its extension segment, which a RAM image does not
           carry: never transplanted */
        return SA1_ERR_UNSUPPORTED;
    }
    st = sa1g_world_apply_ram(w, ram, n);
    if (st != SA1_OK) {
        return st;
    }
    w->freeze_unknown = 1;
    if (sa1g_world_xhook(w, SA1G_FN_LANDMGR, sa1g_exec_landmgr_setup, NULL) != 0) {
        return SA1_ERR_LIMIT;
    }
    /* round 0.9.4: the game's SET manager 8C090FA0 has native code now (sadc src/1st_read/code_8C090FA0.c), but the RAM
       image of a reference holds the stage's manager in mode 0, whose 8C091016 (the stage-start init) is still a fatal
       stub, and its spawner would make the stage's objects from the image's own list: frozen, as it was while it had no
       native code (live worlds create their manager in mode 1: src/world/sa1_world_set.c) */
    if (sa1g_world_xhook(w, SA1G_FN_SET_MANAGER, NULL, NULL) != 0) {
        return SA1_ERR_LIMIT;
    }
    w->chr = (int16_t)gr16(w, SA1G_ADDR_CUR_CHAR);
    w->level = (int16_t)gr16(w, SA1G_ADDR_CUR_LEVEL);
    w->act = (int16_t)gr16(w, SA1G_ADDR_CUR_ACT);
    sa1g_world_rd32(w, SA1G_ADDR_LANDTABLE, &w->landtable);
    w->booted = 1;
    return SA1_OK;
}

/* ---- replay executor --------------------------------------------------------------------------------- */
int sa1g_world_xhook(sa1g_world *w, uint32_t exec, sa1g_exec_fn fn, void *user)
{
    uint32_t i;
    for (i = 0; i < w->num_xhooks; i++) {
        if ((w->xhooks[i].exec & 0x1FFFFFFFu) == (exec & 0x1FFFFFFFu)) {
            break;
        }
    }
    if (i == w->num_xhooks) {
        if (w->num_xhooks >= SA1G_MAX_XHOOKS) {
            return -1;
        }
        w->num_xhooks++;
    }
    w->xhooks[i].exec = exec;
    w->xhooks[i].fn = fn;
    w->xhooks[i].user = user;
    w->xhooks[i].calls = 0;
    return 0;
}

/* trace v2's player-0 test: playertp[0], or before PInitialize filled it the task whose taskwk has
 * counter.b[0] == 0 */
static int sa1g_is_player0(sa1g_world *w, uint32_t tp, uint32_t exec)
{
    uint32_t p0 = gr32(w, SA1G_ADDR_PLAYER_TP), twp;
    if (!(sa1g_is_main(exec) || (sa1g_in_arena(p0) && (gr32(w, p0 + 0x10u) & 0x1FFFFFFFu) == (exec & 0x1FFFFFFFu)))) {
        return 0;
    }
    if (sa1g_in_arena(p0)) {
        return tp == p0;
    }
    twp = gr32(w, tp + 0x20u);
    return sa1g_in_arena(twp) && *(const uint8_t *)sa1g_world_ptr(w, twp + 8u) == 0;
}

/* trace v2 px_capture of this world */
static void sa1g_px_capture(sa1g_world *w)
{
    sa1g_inject *in = &w->inject;
    uint32_t k;
    in->px_rand = gr32(w, SA1G_ADDR_RAND);
    in->px_frame_counter = gr32(w, SA1G_ADDR_FRAME_COUNTER);
    in->px_rings = (int16_t)gr16(w, SA1G_ADDR_RINGS);
    for (k = 0; k < 3; k++) {
        in->px_gravity[k] = gr32(w, SA1G_ADDR_GRAVITY + 4u * k);
    }
    memcpy(in->px_xmtrx, w->xmtrx, sizeof in->px_xmtrx);
    in->px_cam_twp = gr32(w, SA1G_ADDR_CAMERA_TWP);
    if (sa1g_in_arena(in->px_cam_twp)) {
        memcpy(in->px_cam_raw, sa1g_world_ptr(w, in->px_cam_twp), sizeof in->px_cam_raw);
    } else {
        memset(in->px_cam_raw, 0, sizeof in->px_cam_raw);
    }
}

/* the record's state at player entry (trace v2 px_capture, in reverse) */
static void sa1g_player_entry(sa1g_world *w, uint32_t exec)
{
    sa1g_inject *in = &w->inject;
    if (in->player_entries++ != 0) {
        return;
    }
    in->player_exec = exec;
    in->pre_rand = gr32(w, SA1G_ADDR_RAND);
    memcpy(in->pre_xmtrx, w->xmtrx, sizeof in->pre_xmtrx);
    if (!w->replay) {
        sa1g_px_capture(w);
        return;
    }
    if (in->flags & SA1G_INJ_RAND) {
        gw32(w, SA1G_ADDR_RAND, in->rand);
    }
    if (in->flags & SA1G_INJ_XMTRX) {
        memcpy(w->xmtrx, in->xmtrx, sizeof w->xmtrx);
    }
    if (in->flags & SA1G_INJ_CAMERA) {
        uint32_t t = in->cam_twp ? in->cam_twp : w->camera_twp;
        if (sa1g_in_arena(t)) {
            /* a world's own adapter keeps its pointer words (cwp / ewp) */
            memcpy(sa1g_world_ptr(w, t), in->cam_raw, in->cam_twp ? 0x40u : 0x38u);
            gw32(w, SA1G_ADDR_CAMERA_TWP, t);
        }
    }
    if (in->flags & SA1G_INJ_FRAME) {
        gw32(w, SA1G_ADDR_FRAME_COUNTER, in->frame_counter);
    }
    if (in->flags & SA1G_INJ_RINGS) {
        gw16(w, SA1G_ADDR_RINGS, (uint16_t)in->rings);
    }
    sa1g_px_capture(w);
}

static void sa1g_exec_task(sa1g_world *w, uint32_t tp, uint32_t exec)
{
    uint32_t i;
    if (sa1g_is_player0(w, tp, exec)) {
        sa1g_player_entry(w, exec);
    }
    for (i = 0; i < w->num_xhooks; i++) {
        sa1g_xhook *h = &w->xhooks[i];
        if ((h->exec & 0x1FFFFFFFu) == (exec & 0x1FFFFFFFu)) {
            h->calls++;
            if (h->fn != NULL) {
                h->fn(w, tp, h->user);
            }
            return;
        }
    }
    if (w->freeze_unknown && sa1_fn_name(exec) == NULL) {
        w->frozen_skips++;
        w->frozen_last = exec;
        return;
    }
    ((void (*)(void *))sa1_fn(SA1_GFN_C(exec), 0u))(sa1_decode(tp));
}

/* func_8C09878C(level) */
static void sa1g_run_level(sa1g_world *w, int32_t level)
{
    uint32_t tp, guard = 0;
    if (level == 8) {
        level = 7;
    }
    tp = gr32(w, SA1G_ADDR_TASK_LISTS + 4u * (uint32_t)level);
    while (tp != 0) {
        uint32_t exec = gr32(w, tp + 0x10u), next = gr32(w, tp + 0x00u);
        gw32(w, SA1G_ADDR_CUR_EXEC, exec);
        if (exec != 0) {
            sa1g_exec_task(w, tp, exec);
        }
        gw32(w, SA1G_ADDR_CUR_EXEC, 0);
        tp = next;
        if (++guard > 0x1000u) {
            sa1g_fatal(0, "task list loops");
        }
    }
}

void sa1g_run_tasks(sa1g_world *w)
{
    int32_t i;
    w->inject.player_entries = 0;
    w->inject.player_exec = 0;
    sa1g_run_level(w, 8);
    for (i = 0; i < 7; i++) {
        sa1g_run_level(w, i);
    }
}

/* ---- player 0 --------------------------------------------------------------------------------------- */
void sa1g_world_player(const sa1g_world *w, sa1g_player_ptrs *out)
{
    memset(out, 0, sizeof *out);
    if (w == NULL || w->ram == NULL) {
        return;
    }
    sa1g_world_rd32(w, SA1G_ADDR_PLAYER_TP, &out->tp);
    sa1g_world_rd32(w, SA1G_ADDR_PLAYER_TWP, &out->twp);
    sa1g_world_rd32(w, SA1G_ADDR_PLAYER_MWP, &out->mwp);
    sa1g_world_rd32(w, SA1G_ADDR_PLAYER_PWP, &out->pwp);
    if (out->tp == 0) {
        out->tp = w->player_tp;
    }
    if (!sa1g_in_arena(out->pwp) && sa1g_in_arena(out->mwp)) {
        sa1g_world_rd32(w, out->mwp, &out->pwp);        /* mwp->work.pwp */
    }
    if (sa1g_in_arena(out->pwp)) {
        sa1g_world_rd32(w, out->pwp + 0x64u, &out->csts);
    }
    if (!sa1g_in_arena(out->tp)) out->tp = 0;
    if (!sa1g_in_arena(out->twp)) out->twp = 0;
    if (!sa1g_in_arena(out->mwp)) out->mwp = 0;
    if (!sa1g_in_arena(out->pwp)) out->pwp = 0;
    if (!sa1g_in_arena(out->csts)) out->csts = 0;
}
