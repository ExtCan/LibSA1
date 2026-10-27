/*
 * sa1_world_charopt.c - the world side of the per-character settings (round 0.9.1; include/libsa1.h "lives, companions,
 * game options", the API in src/sa1_charopt.c; sadc docs/research/gsa1_070_drown.md E2, gsa1_070_bird.md F1 with its
 * Review R.2 / R.4 (a) (b), gsa1_rounds_plan.md 14.1h).
 *
 *   lives        B_8C752BE0 = n (sa1g_world_set_lives, sa1_world_boot.c), before the world's first step or between ticks.
 *   companions   Amy's Birdie, the game's own task (1ST_READ 8C0DECDA init, 8C0DE964 exec, 8C0DE8C0 display; matching
 *                C), made with the call of AmyMain's spawner 8C0DEDC0, CreateElementalTask(2, 3, 8C0DECDA), and freed with
 *                FreeTask 8C098786 (DestroyTask runs at its next exec), against the spawner's own gate read from the world
 *                (CurrentCharacter 5, tcrf_LevelSelectVar 4 / 5 / 9, the stage in D_8C15C808 or 0x1A00-0x22FF with no
 *                event). Before the world's first step (w->steps == 0: the create, or the attach of a world built later)
 *                the game makes its own bird in step 0 when the gate passes (AmyMain mode 0), so ON makes one only when
 *                the gate fails, and OFF frees the game's bird right after the first pair (sa1g_charopt_pre, before the
 *                executor of step 2: the bird's first draw would be in step 2, its exec draws on sub step 0 only, and its
 *                display returns at once on sub step 1). Later, ON makes one when the world has none (the spawner runs
 *                in mode 0 only), OFF frees every bird task. A world whose player 0 is not that Amy (CurrentCharacter or
 *                playertwp[0]->counter.b[1] not 5: a follower world, sadc gsa1_rounds_plan.md 12.2) is refused: the bird
 *                follows playertwp[0] and reads Amy's state through player 0 (code_8C0DE8C0.c).
 *   game options PC-only switches in the lowered game code's pcdata (one copy per world), found by their magic words:
 *                SA1_GOPT_TAILS_SNAP_TAILS = the SA1_TAILSNAP block of sadc src/1st_read/code_8C0BA820.c (SA1_GA only);
 *                round 0.9.3a: SA1_GOPT_SUPER_IDLE_BY_FORM = SA1_SUPERVOICE of src/1st_read/code_8C07DCE0.c ({on, level,
 *                act}: the normal lines' level is the world's own, level 1 act 0 for a world booted on level 19),
 *                SA1_GOPT_SUPER_AURA_ANIM = SA1_AURAANIM of src/overlays/b_chaos7/code_0C91D200.c (an overlay TU's
 *                pcdata: the same placement as 1ST_READ's, sa1g_pcdata.c); round 0.9.4: SA1_GOPT_ITEM_ICON_LIMIT = the
 *                SA1_ICONLIMIT block of src/1st_read/code_8C05E0F8.c ({on}: the item icon list's 20 entries are a bound).
 *   Super options (round 0.9.3a, sa1_character_set_super_option): the world's sg options (src/world/sa1_world_super.c
 *                sa1w_world_supopt), applied at once while a General Super Sonic is Super. Round 0.9.4a: the bit
 *                SA1_SUPER_EFFECTS_DUST of SA1_SUPOPT_SUPER_EFFECTS = the SA1_SUPERDUST block of
 *                src/1st_read/code_8C0B4040.c ({on, tex_a, tex_b}: B_CHAOS7's ss_ball texlists 0CA6B2D0 / 0CA6B2D8),
 *                written in General worlds only (their image holds B_CHAOS7), at once and at every attach
 *                (sa1g_charopt_superdust).
 *
 * The bridge (sa1_api_world.c sa1w_world_create) installs sa1g_charopt_attach (called last by sa1ho_world_attach: the
 * settings the host gave before the world was built) and sa1g_charopt_pre (called first by hostobj_pre in every step) in
 * character worlds. Worlds whose host never called the setters run unchanged: nothing here writes to them.
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"

#include <stddef.h>
#include <string.h>

#define CO_FN_FREETASK      0x8C098786u     /* FreeTask(tp): exec = DestroyTask 8C09865C */
#define CO_FN_BIRD_INIT     0x8C0DECDAu     /* the bird's init exec (8C0DEDC0's CreateElementalTask(2, 3, 8C0DECDA)) */
#define CO_PHYS_BIRD_INIT   0x0C0DECDAu     /* exec words & 0x1FFFFFFF */
#define CO_PHYS_BIRD_EXEC   0x0C0DE964u     /* the bird's exec after its init */
#define CO_ADDR_STAGE_LIST  0x8C15C808u     /* Uint16 D_8C15C808[]: 8C0DEDC0's stages, 0x7FFF-terminated */
#define CO_MAX_BIRDS        16u

/* the SA1_TAILSNAP pcdata block of the lowered code_8C0BA820.c: {Uint32 magic[2]; Sint32 snap} */
#define CO_TAILSNAP_MAGIC0  0x4C494154u     /* "TAIL" */
#define CO_TAILSNAP_MAGIC1  0x30324142u     /* "BA20" */
#define CO_TAILSNAP_SNAP    0x08u
#define CO_TAILSNAP_BYTES   0x0Cu
/* round 0.9.3a: the SA1_SUPERVOICE block of code_8C07DCE0.c {Uint32 magic[2]; Sint32 on, level, act} and the SA1_AURAANIM
   block of B_CHAOS7's code_0C91D200.c {Uint32 magic[2]; Sint32 anim} */
#define CO_SUPERVOICE_MAGIC0 0x43494F56u    /* "VOIC" */
#define CO_SUPERVOICE_MAGIC1 0x30454344u    /* "DCE0" */
#define CO_SUPERVOICE_BYTES  0x14u
#define CO_AURAANIM_MAGIC0   0x41525541u    /* "AURA" */
#define CO_AURAANIM_MAGIC1   0x32323244u    /* "D222" */
#define CO_AURAANIM_BYTES    0x0Cu
/* round 0.9.4: the SA1_ICONLIMIT block of code_8C05E0F8.c {Uint32 magic[2]; Sint32 on} */
#define CO_ICONLIMIT_MAGIC0  0x4E4F4349u    /* "ICON" */
#define CO_ICONLIMIT_MAGIC1  0x38463045u    /* "E0F8" */
#define CO_ICONLIMIT_BYTES   0x0Cu
/* round 0.9.4a: the SA1_SUPERDUST block of code_8C0B4040.c {Uint32 magic[2]; Sint32 on; Uint32 tex_a, tex_b}
   (super2_plan.md 6.1 task 5, super2_lsd.md 4.2 M1) */
#define CO_SUPERDUST_MAGIC0  0x54535544u    /* "DUST" */
#define CO_SUPERDUST_MAGIC1  0x30343034u    /* "4040" */
#define CO_SUPERDUST_BYTES   0x14u
#define CO_SUPERDUST_TEX_A   0x0CA6B2D0u    /* B_CHAOS7's ss_ball_a texlist: where the game picked D_8C1C694C (the Super
                                               after-images' own pick, code_0C91DB60.c) */
#define CO_SUPERDUST_TEX_B   0x0CA6B2D8u    /* ss_ball_b: where the game picked D_8C1C6954 */

/* ---- guest memory (arena words, bounded) ------------------------------------------------------------------------- */
static uint32_t co_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (g == 0 || sa1g_world_rd32(w, g, &v) != 0) {
        return 0;
    }
    return v;
}

static uint32_t co_rd16(const sa1g_world *w, uint32_t g)
{
    return (co_rd32(w, g & ~3u) >> (8u * (g & 2u))) & 0xFFFFu;
}

static uint32_t co_rd8(const sa1g_world *w, uint32_t g)
{
    return (co_rd32(w, g & ~3u) >> (8u * (g & 3u))) & 0xFFu;
}

/* ---- Amy's bird ------------------------------------------------------------------------------------------------- */
/* the bird tasks of the world (top-level tasks of the eight lists whose exec is the bird's init or exec; a task freed
 * by FreeTask has DestroyTask as its exec and is not one); returns their count, the first `cap` in tps */
static uint32_t co_birds(const sa1g_world *w, uint32_t *tps, uint32_t cap)
{
    uint32_t n = 0, lv;
    for (lv = 0; lv < 8u; lv++) {
        uint32_t tp = co_rd32(w, SA1G_ADDR_TASK_LISTS + 4u * lv), guard = 0;
        while (tp != 0 && guard++ < 0x1000u) {
            uint32_t ex = co_rd32(w, tp + 0x10u) & 0x1FFFFFFFu;
            if (ex == CO_PHYS_BIRD_INIT || ex == CO_PHYS_BIRD_EXEC) {
                if (tps != NULL && n < cap) {
                    tps[n] = tp;
                }
                n++;
            }
            tp = co_rd32(w, tp);
        }
    }
    return n;
}

/* 8C0DEDC0's gate, read from the world (the stage number is GetStageNumber's (level << 8) | act) */
static int co_bird_gate(const sa1g_world *w)
{
    uint32_t mode = co_rd32(w, SA1G_ADDR_GAME_MODE), stg, k;
    if ((int16_t)co_rd16(w, SA1G_ADDR_CUR_CHAR) != 5 || (mode != 4u && mode != 5u && mode != 9u)) {
        return 0;
    }
    stg = ((co_rd16(w, SA1G_ADDR_CUR_LEVEL) & 0xFFu) << 8) | (co_rd16(w, SA1G_ADDR_CUR_ACT) & 0xFFu);
    if (stg < 0xF00u) {
        for (k = 0; k < 16u; k++) {
            uint32_t e = co_rd16(w, CO_ADDR_STAGE_LIST + 2u * k);
            if (e == stg) {
                return 1;
            }
            if (e == 0x7FFFu) {
                return 0;
            }
        }
        return 0;
    }
    /* 0x1A00-0x22FF: while no event runs (8C049A4E: B_8C7492AC != 0) */
    return stg >= 0x1A00u && stg < 0x2300u && co_rd32(w, SA1G_ADDR_EVENT_RUNNING) == 0;
}

/* player 0 is the world's Amy (the bird follows playertwp[0] and reads Amy's state through player 0) */
static int co_player0_amy(const sa1g_world *w)
{
    uint32_t twp = co_rd32(w, SA1G_ADDR_PLAYER_TWP);
    return (int16_t)co_rd16(w, SA1G_ADDR_CUR_CHAR) == 5 && twp != 0 && co_rd8(w, twp + 0x09u) == 5u;
}

/* inside the world: free every bird task (FreeTask; DestroyTask removes it, and a mirror child, at its next exec) */
static void co_free_birds_in(sa1g_world *w)
{
    uint32_t tps[CO_MAX_BIRDS], n = co_birds(w, tps, CO_MAX_BIRDS), k;
    void (*freetask)(void *) = (void (*)(void *))sa1_fn(SA1_GFN_C(CO_FN_FREETASK), 0u);
    for (k = 0; k < n && k < CO_MAX_BIRDS; k++) {
        freetask(sa1g_world_ptr(w, tps[k]));
    }
}

static void co_free_birds_cb(sa1g_world *w, void *user)
{
    (void)user;
    co_free_birds_in(w);
}

/* inside the world: 8C0DEDC0's call when the world has no bird */
static void co_make_bird_cb(sa1g_world *w, void *user)
{
    void *(*create)(uint16_t, int32_t, sa1_gfn) = (void *(*)(uint16_t, int32_t, sa1_gfn))sa1_fn(
        SA1_GFN_C(SA1G_FN_CREATE_TASK), 0u);
    (void)user;
    if (co_birds(w, NULL, 0) == 0) {
        (void)create(2u, 3, SA1_GFN_C(CO_FN_BIRD_INIT));
    }
}

static sa1_status co_companions(sa1_context *ctx, sa1g_world *w, const sa1w_charopt *o)
{
    uint32_t bit = 1u << SA1_COMPANION_BIRD, n;
    int on, boot;
    if (!(o->known & bit)) {
        return SA1_OK;
    }
    if (w->dead) {
        return w->dead_status;
    }
    if (!w->booted) {
        return SA1_OK;          /* applied by the attach hook of the world's boot */
    }
    if (!co_player0_amy(w)) {
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "companion: the world's player 0 is not its Amy (CurrentCharacter %d, "
                        "character %u): the game's bird follows player 0", (int)(int16_t)co_rd16(w, SA1G_ADDR_CUR_CHAR),
                        (unsigned)co_rd8(w, co_rd32(w, SA1G_ADDR_PLAYER_TWP) + 0x09u));
    }
    on = (o->companions & bit) != 0;
    boot = w->steps == 0;
    n = co_birds(w, NULL, 0);
    if (on) {
        w->ho.companion_off &= ~bit;
        if (n != 0 || (boot && co_bird_gate(w))) {
            return SA1_OK;      /* there is one, or the game makes it in step 0 */
        }
        return sa1g_world_call(w, co_make_bird_cb, NULL);
    }
    if (boot && co_bird_gate(w)) {
        w->ho.companion_off |= bit;     /* the game's own bird of step 0 goes right after the first pair */
        return SA1_OK;
    }
    w->ho.companion_off &= ~bit;
    return n != 0 ? sa1g_world_call(w, co_free_birds_cb, NULL) : SA1_OK;
}

/* hostobj_pre (inside the step, before the executor): the game's bird of a world whose wish was off at its boot */
void sa1g_charopt_pre(sa1g_world *w, int32_t sub)
{
    (void)sub;
    if (w->ho.companion_off != 0 && w->steps >= 2u) {
        w->ho.companion_off = 0;
        co_free_birds_in(w);
    }
}

sa1_status sa1w_world_companion(const struct sa1g_world *w, uint32_t companion, int32_t *present)
{
    if (present == NULL || companion != SA1_COMPANION_BIRD) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    *present = (w != NULL && w->ram != NULL && w->booted && !w->dead && co_birds(w, NULL, 0) != 0) ? 1 : 0;
    return SA1_OK;
}

/* ---- game options ------------------------------------------------------------------------------------------------ */
/* a block in the world's pcdata (the start of segment 0x18) by its magic words, NULL when the lowered code has none */
static uint8_t *co_block(const sa1g_world *w, uint32_t magic0, uint32_t magic1, size_t bytes)
{
    size_t off, end = w->pcdata_size;
    if (w->ext == NULL || end > w->ext_used || end < bytes) {
        return NULL;
    }
    for (off = 0; off + bytes <= end; off += 4u) {
        uint8_t *p = w->ext + off;
        uint32_t a, b;
        memcpy(&a, p, 4);
        memcpy(&b, p + 4, 4);
        if (a == magic0 && b == magic1) {
            return p;
        }
    }
    return NULL;
}

static sa1_status co_gopts(sa1_context *ctx, sa1g_world *w, const sa1w_charopt *o)
{
    static const struct {
        uint32_t option, magic0, magic1, bytes;
        const char *what;
    } k_blocks[4] = {
        { SA1_GOPT_TAILS_SNAP_TAILS, CO_TAILSNAP_MAGIC0, CO_TAILSNAP_MAGIC1, CO_TAILSNAP_BYTES,
          "SA1_TAILSNAP block (sadc src/1st_read/code_8C0BA820.c)" },
        { SA1_GOPT_SUPER_IDLE_BY_FORM, CO_SUPERVOICE_MAGIC0, CO_SUPERVOICE_MAGIC1, CO_SUPERVOICE_BYTES,
          "SA1_SUPERVOICE block (sadc src/1st_read/code_8C07DCE0.c)" },
        { SA1_GOPT_SUPER_AURA_ANIM, CO_AURAANIM_MAGIC0, CO_AURAANIM_MAGIC1, CO_AURAANIM_BYTES,
          "SA1_AURAANIM block (sadc src/overlays/b_chaos7/code_0C91D200.c)" },
        { SA1_GOPT_ITEM_ICON_LIMIT, CO_ICONLIMIT_MAGIC0, CO_ICONLIMIT_MAGIC1, CO_ICONLIMIT_BYTES,
          "SA1_ICONLIMIT block (sadc src/1st_read/code_8C05E0F8.c)" },
    };
    uint32_t i;
    sa1_status st = SA1_OK;
    if (w->dead) {
        return w->dead_status;
    }
    for (i = 0; i < 4u; i++) {
        uint32_t opt = k_blocks[i].option;
        int32_t v = opt < SA1W_GOPT_MAX ? o->gopt_value[opt] : 0;
        uint8_t *p = co_block(w, k_blocks[i].magic0, k_blocks[i].magic1, k_blocks[i].bytes);
        if (!(o->gopts & (1u << opt))) {
            v = 0;
        }
        if (p == NULL) {
            if (v != 0 && st == SA1_OK) {
                st = sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "game option %u: the lowered game code of this build has no %s",
                              (unsigned)opt, k_blocks[i].what);
            }
            continue;
        }
        if (opt == SA1_GOPT_TAILS_SNAP_TAILS || opt == SA1_GOPT_ITEM_ICON_LIMIT) {
            v = v ? 1 : 0;
            memcpy(p + 0x08u, &v, 4);
        } else if (opt == SA1_GOPT_SUPER_IDLE_BY_FORM) {
            /* the normal lines: the world's own stage, a plain level for a world booted on level 19 (kind 8, a 0.9.3-style
               super-capable Sonic: design 4.9) */
            int32_t on = v ? 1 : 0, lv = w->level == 19 ? 1 : w->level, act = w->level == 19 ? 0 : w->act;
            memcpy(p + 0x08u, &on, 4);
            memcpy(p + 0x0Cu, &lv, 4);
            memcpy(p + 0x10u, &act, 4);
        } else {
            memcpy(p + 0x08u, &v, 4);
        }
    }
    return st;
}

/* round 0.9.4a: the SA1_SUPERDUST block from the world's SA1_SUPOPT_SUPER_EFFECTS bits (General worlds; sa1w_world_supopt
 * calls it at once). SA1_ERR_NOT_IMPLEMENTED: the bit is set and the lowered code of this build has no block */
sa1_status sa1g_charopt_superdust(sa1g_world *w)
{
    uint8_t *p;
    int32_t on;
    uint32_t a = CO_SUPERDUST_TEX_A, b = CO_SUPERDUST_TEX_B;
    if (w == NULL || w->ram == NULL || !(w->super & SA1G_SUPER_GENERAL)) {
        return SA1_OK;
    }
    on = (w->sg.effects & (int32_t)SA1_SUPER_EFFECTS_DUST) ? 1 : 0;
    p = co_block(w, CO_SUPERDUST_MAGIC0, CO_SUPERDUST_MAGIC1, CO_SUPERDUST_BYTES);
    if (p == NULL) {
        return on ? SA1_ERR_NOT_IMPLEMENTED : SA1_OK;
    }
    memcpy(p + 0x08u, &on, 4);
    memcpy(p + 0x0Cu, &a, 4);
    memcpy(p + 0x10u, &b, 4);
    return SA1_OK;
}

static sa1_status co_supopts(sa1_context *ctx, sa1g_world *w, const sa1w_charopt *o)
{
    uint32_t opt;
    sa1_status st = SA1_OK, s;
    for (opt = 1; opt < SA1W_SUPOPT_MAX; opt++) {
        if (o->supopts & (1u << opt)) {
            s = sa1w_world_supopt(w, opt, o->supopt_value[opt]);
            if (s == SA1_ERR_NOT_IMPLEMENTED && st == SA1_OK) {
                s = sa1_fail(ctx, s, "Super option %u: the lowered game code of this build has no SA1_SUPERDUST block "
                             "(sadc src/1st_read/code_8C0B4040.c)", (unsigned)opt);
            }
            st = st != SA1_OK ? st : s;
        }
    }
    return st;
}

/* ---- the bridge -------------------------------------------------------------------------------------------------- */
sa1_status sa1w_world_charopt(sa1_context *ctx, struct sa1g_world *w, const sa1w_charopt *o, uint32_t what)
{
    sa1_status st = SA1_OK, s;
    if (ctx == NULL || w == NULL || o == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->ram == NULL || !w->booted) {
        return SA1_OK;          /* the attach hook applies them */
    }
    if ((what & SA1W_SET_LIVES) && (o->set & SA1W_SET_LIVES)) {
        s = sa1g_world_set_lives(w, o->lives);
        st = st != SA1_OK ? st : s;
    }
    if ((what & SA1W_SET_GOPTS) && (o->set & SA1W_SET_GOPTS)) {
        s = co_gopts(ctx, w, o);
        st = st != SA1_OK ? st : s;
    }
    if ((what & SA1W_SET_COMPANIONS) && (o->set & SA1W_SET_COMPANIONS)) {
        s = co_companions(ctx, w, o);
        st = st != SA1_OK ? st : s;
    }
    if ((what & SA1W_SET_SUPOPTS) && (o->set & SA1W_SET_SUPOPTS)) {
        s = co_supopts(ctx, w, o);
        st = st != SA1_OK ? st : s;
    }
    return st;
}

/* sa1ho_world_attach, last: the settings the host gave the character before its world was built (before step 0) */
sa1_status sa1g_charopt_attach(sa1_context *ctx, sa1g_world *w)
{
    const sa1_api_ext *x;
    uint32_t slot;
    if (ctx == NULL || w == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    sa1g_set_attach(ctx, w);    /* round 0.9.4: the SET layer's hooks (src/world/sa1_world_set.c) */
    x = sa1_api_ext_of(ctx);
    for (slot = 0; slot < SA1_MAX_INSTANCES; slot++) {
        const sa1w_charopt *o = &x->charopt[slot];
        sa1_character_id id = (sa1_character_id)(((uint32_t)ctx->inst_gen[slot] << 16) | (slot + 1u));
        sa1_status st;
        if (ctx->inst[slot] == NULL || o->id != id || o->set == 0 || sa1_character_world(ctx, id) != w) {
            continue;
        }
        st = sa1w_world_charopt(ctx, w, o, SA1W_SET_LIVES | SA1W_SET_GOPTS | SA1W_SET_COMPANIONS | SA1W_SET_SUPOPTS);
        if (st == SA1_ERR_UNSUPPORTED || st == SA1_ERR_NOT_IMPLEMENTED) {
            /* a follower world's bird, a build without the switch: the world is built without that setting */
            sa1_log(ctx, SA1_LOG_WARN, "character %d: a setting does not apply to its world: %s", (int)id, ctx->error);
            st = SA1_OK;
        }
        return st;
    }
    return SA1_OK;
}
