/*
 * sa1_world_super.c - Super Sonic in a world (round 6a; sadc docs/research/super_sonic.md 4 with its Review R.2-R.4,
 * gsa1_rounds_plan.md 11.1 / 11.2 / 14.1a; round 0.9.3a: the General form, sadc docs/research/super_general_design.md 4
 * with its Review R.4; the API: include/libsa1.h "Super Sonic (API 0.4)" and "THE GENERAL FORM").
 *
 * A super-capable world (SA1G_SUPER_WORLD: Super Sonic's, kind 8, and a Sonic world built with SA1_CREATE_SUPER_CAPABLE)
 * is a Sonic world whose image holds B_CHAOS7. Kind 8 and a 0.9.3-style super-capable Sonic are booted on the Perfect
 * Chaos stage (level 19 act 0); a General world (SA1G_SUPER_GENERAL) on the host's stage. Its level task 0x0C900000 is the
 * stand-in of sa1_world_stage.c (sa1g_stage_pc_level). This file holds the rest:
 *
 *   the ring drain     sa1g_super_drain, called by the stand-in once per step after its children: the decompiled
 *                      0C917088 (the boss's SuperSonic_SubtractRings: one ring every 60 calls, at 0 rings request 47)
 *                      with the stand-in's own taskwk (the counter at +0x0E), while player 0's equipment has 0x8000,
 *                      unless SA1G_SUPER_NO_DRAIN; with SA1G_SUPER_KEEP only while the ring count is above 0 (the game
 *                      code never sees 0). A host composition of game code: the game gates the call by the boss's state.
 *                      General worlds: only while the rings are above 0; in the step in which the drain would send
 *                      request 47 (0 rings) the game's 8C08A074 and the General revert instead (unless KEEP / NO_DRAIN):
 *                      the game drops request 47 in Sonic's normal modes (res-game measured 100+ pairs Super at 0).
 *   the revert fix     sa1g_super_boot installs an executor override of the physics task's exec 8C0B3280 (unless
 *                      SA1G_SUPER_FAITHFUL). 8C0B3280 frees the task once player 0's equipment lost 0x8000; DestroyTask
 *                      runs the destructor 8C0B3240 at once, which restores the wrong fields (saved air_resist ->
 *                      max_x_spd, saved air_resist_air -> run_accel; air_resist / air_resist_air keep the Super values:
 *                      sadc code_8C0B3240.c). The override reads, before it calls the original exec, max_x_spd and
 *                      run_accel (their values right before the destructor) and the task's saved values (awp +4 / +8,
 *                      gone after DestroyTask: FreeAnywk), and writes them back after it (max_x_spd, run_accel,
 *                      air_resist, air_resist_air; air_accel and center_height are the destructor's own, right). The speed-up
 *                      tasks 8C07D2A2 / 8C0B2EA0, which save and restore max_x_spd too, keep working (Review R.3.5).
 *   orders             sa1w_world_order (sa1_character_order). Request worlds: SetInputP(0, 46 / 47) through the code
 *                      map, outside ticks; the game's request handlers carry it out at the next step (a request already
 *                      pending is not sent again; the voice option plays when the bit is set: RC11). General worlds: the
 *                      transformation / revert below at once (before the first pair: queued until after it).
 *
 * THE GENERAL FORM (SA1G_SUPER_GENERAL; design 4.2-4.6, 4.8, 4.9, review RC7 / RC11 / RC12 / RC14):
 *   welds              after the world's first pair (SonicMain's mode 0 ran: jvmode 1, pljvptr = SonicWeldList) the 12
 *                      Super entries of SonicWeldList (0x8C1C5FA4; objptr the Super body 0C909D2C) are copied with the
 *                      disc's inpmode 2 (mode 0 wrote 0 off stage 0x13xx) into a list TMP in segment 0x18, together with
 *                      copies of Sonic's hand / foot / head entries (inpmode 4-9, srcobj in Sonic's body tree) re-pointed
 *                      at the Super body's node of the same depth-first index; the game's own PJoinVertexes 8C07A55C runs
 *                      jvmode 0 on TMP (pljvptr / jvmode saved and restored around the call: the org buffers come from the
 *                      world's heap, the depths are set). GW = SonicWeldList with the 12 Super entries replaced by TMP's
 *                      copies, then TMP's hand entries: pljvptr = GW for the world's life (only SonicMain's mode 0 writes
 *                      it; jvmode 2 frees every org buffer through GW once). Super entries mode 0 set up itself (a
 *                      General world on level 19) are copied as they are.
 *   the look           GT: a copy of SonicActionList (0x8C1C55A0, 142 entries) whose actions on Sonic's body 8C1CE76C
 *                      become {Super body 0C909D2C, the same motion}, on the ball 8C1DBB2C {Super ball 0C90BD04, the
 *                      same motion} and on the curled body 8C1D630C {Super ball, Sonic_Spin_Jumpball 8C232E80} (as the
 *                      game's own actions 135-141 pair them); objnum is bounded by the new object's node count (the link
 *                      builder walks that many nodes). On: pljvptr's actions = GT and texlist_SONIC (0x8C1C6C94) names
 *                      the SUPERSONIC textures {0x0C901B1C, 73}; off: SonicActionList and {0x8C1C6B44, 28}. A link action
 *                      (mtnmode 2: the display draws mj.actwkptr) is re-pointed at the other table's object at the switch.
 *   transformation     request 46's body (sadc code_8C0AFF1A.c case 46) without its mode lines: equipment |= 0x8000,
 *                      the aura task CreateElementalTask(2, 2, 0C900480) (counter.b[0] = the player) unless the host's
 *                      no-aura option, the physics task CreateElementalTask(8, 2, 8C0B32B6) (awp byte = the player), the
 *                      look, the host's voice tone (8C0940DA(tone, 0, 0, 0), the push grunt's call). Inside the world,
 *                      between pairs.
 *   revert             equipment &= ~0x8000 and the look off; the aura (0C91D486) and physics (8C0B3280) tasks free
 *                      themselves at their next exec (the revert fix restores the physics).
 *   after each pair    sa1g_super_pair: the weld set-up, SA1G_SUPER_START or a queued order, the look following the
 *                      bit (the game clearing it itself: requests 47 / 50 through mode 20's Super handler), the guard
 *                      (RC7: a drawn action whose object is neither Super object gets Sonic's names back for that pair)
 *                      and the leak check (modes 71-78: RC12's audit found no libsa1 or G-SA1 path that sends requests 9,
 *                      10, 12-17 or 19-23, so it is logged and counted, not switched).
 *   speed              an executor override of the physics task's first exec 8C0B32B6: the original (it saves Sonic's
 *                      air_resist / air_resist_air / air_accel at awp +4 / +8 / +0xC and writes the Super values), then
 *                      the host's set (SA1_SUPER_SPEED_*) from those saves; a set change while Super writes it at once.
 *   effects            executor overrides of the first execs of Sonic's trail 8C0B48B4 and ball after-images 8C0B3FC0:
 *                      while the host's effects option is on and player 0 is Super, the game's Super clones 0C9004F2 /
 *                      0C9004B6 (B_CHAOS7: the Super modes' own twins, made by the same kind of creator) run instead.
 *
 * The executor overrides make the world run its tasks through the world's walker (sa1_world_tick.c: the same walk as the
 * game's 8C0988A4), as every stage stand-in does. Nothing here runs in a world without SA1G_SUPER_WORLD, and a kind-8 or
 * 0.9.3-style world runs as before unless the host set the voice option (the reference worlds never do).
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"

#include <string.h>

#define SU_FN_DRAIN         0x0C917088u     /* B_CHAOS7 SuperSonic_SubtractRings (code_0C917088.c) */
#define SU_FN_PHYS_EXEC     0x8C0B3280u     /* the physics task's exec (code_8C0B3240.c) */
#define SU_FN_PHYS_INIT     0x8C0B32B6u     /* its first exec (request 46's CreateElementalTask(8, 2, ...)) */
#define SU_FN_SET_INPUT     0x8C07C65Eu     /* SetInputP(pno, mode) (code_8C07C550.c) */
#define SU_FN_PJOIN         0x8C07A55Cu     /* PJoinVertexes(twp, mwp, pwp) (code_8C07A55C.c) */
#define SU_FN_SE            0x8C0940DAu     /* the one-shot sound call (se, 0, 0, 0) */
#define SU_FN_FREETASK      0x8C098786u     /* FreeTask(tp): exec = DestroyTask */
#define SU_FN_RINGS_ZERO    0x8C08A074u     /* B_8C752BE4 = 0 (the drain's own call at 0 rings) */
#define SU_FN_AURA_INIT     0x0C900480u     /* the aura task's first exec (SUPERSONIC_EXEC) */
#define SU_FN_AURA          0x0C91D486u     /* the aura task's exec */
#define SU_FN_TRAIL         0x8C0B48B4u     /* Sonic's trail task, first exec (8C0B2C8C) */
#define SU_FN_TRAIL_SS      0x0C9004F2u     /* its Super twin (8C0B2CB2) */
#define SU_FN_BALLAI        0x8C0B3FC0u     /* Sonic's jump-ball after-images, first exec (8C0B2D66) */
#define SU_FN_BALLAI_SS     0x0C9004B6u     /* its Super twin (8C0B2D8C) */
#define SU_PWP_EQUIPMENT    0x004u          /* playerwk.equipment (Sint16; 0x8000 = Super Sonic) */
#define SU_PWP_FREE_W1      0x07Eu          /* playerwk.free.w[1] (Sonic's display: action 32 rule) */
#define SU_PWP_MAX_X_SPD    0x0ACu          /* playerwk.p.max_x_spd */
#define SU_PWP_RUN_ACCEL    0x0D8u          /* playerwk.p.run_accel */
#define SU_PWP_AIR_ACCEL    0x0DCu          /* playerwk.p.air_accel */
#define SU_PWP_AIR_RES_AIR  0x0ECu          /* playerwk.p.air_resist_air */
#define SU_PWP_AIR_RESIST   0x0F0u          /* playerwk.p.air_resist */
#define SU_PWP_CENTER_H     0x11Cu          /* playerwk.p.center_height */
#define SU_ADDR_PHYS_SONIC  0x8C1BDFE4u     /* player_parameter[0]: Sonic's physics (p.center_height at +0x80) */
#define SU_PWP_MTNMODE      0x120u          /* playerwk.mj.mtnmode (Sint16) */
#define SU_PWP_JVMODE       0x122u          /* playerwk.mj.jvmode (Sint16) */
#define SU_PWP_REQACTION    0x124u          /* playerwk.mj.reqaction (Uint16) */
#define SU_PWP_PLACTPTR     0x140u          /* playerwk.mj.plactptr */
#define SU_PWP_PLJVPTR      0x144u          /* playerwk.mj.pljvptr */
#define SU_PWP_ACTWKPTR     0x148u          /* playerwk.mj.actwkptr */
#define SU_ADDR_ACTIONS     0x8C1C55A0u     /* SonicActionList: PL_ACTION[142] (16 bytes) */
#define SU_NUM_ACTIONS      142u
#define SU_ADDR_WELDS       0x8C1C5FA4u     /* SonicWeldList: PL_JOIN_VERTEX[] (0x18 bytes), objptr 0 ends it */
#define SU_WELD_SIZE        0x18u
#define SU_MAX_WELDS        64u
#define SU_ADDR_TEXLIST     0x8C1C6C94u     /* texlist_SONIC: {textures, nbTexture} */
#define SU_NAMES_SONIC      0x8C1C6B44u     /* its own names (SONIC, 28) */
#define SU_NAMES_SONIC_N    28u
#define SU_NAMES_SUPER      0x0C901B1Cu     /* TEXLIST_SUPERSONIC's names (B_CHAOS7 0x0C901EA0: SUPERSONIC, 73) */
#define SU_NAMES_SUPER_N    73u
#define SU_OBJ_SONIC_BODY   0x8C1CE76Cu
#define SU_OBJ_SONIC_CURLED 0x8C1D630Cu
#define SU_OBJ_SONIC_BALL   0x8C1DBB2Cu
#define SU_OBJ_SUPER_BODY   0x0C909D2Cu     /* Super Sonic's model (actions 130-140, the 12 welds' objptr) */
#define SU_OBJ_SUPER_BALL   0x0C90BD04u     /* his jump ball (action 141) */
#define SU_MOT_SPIN_BALL    0x8C232E80u     /* Sonic_Spin_Jumpball (action 141's motion) */
/* round 0.9.4a (super2_plan.md 6.1 task 2 with owner answer O1, super2_anim.md 6.1): GA's data */
#define SU_ADDR_PHYS_CH     0x8C0B337Cu     /* the physics task 8C0B32B6's center_height pool word (L8C0B337C: 6.4) */
#define SU_MOT_SS_WAIT      0x0C914D00u     /* 130 Super Sonic Stationary (ss_wait0, 32 frames) */
#define SU_MOT_SS_MOVE0     0x0C90EEA4u     /* 131 Begin Moving (ss_move0, 8) */
#define SU_MOT_SS_MOVE1     0x0C90FFC8u     /* 132 Low Speed (ss_move1, 8) */
#define SU_MOT_SS_MOVE2     0x0C91116Cu     /* 133 = 134 High Speed (ss_move2, 8) */
#define SU_MDATA2_SIZE      16u             /* NJS_MDATA2 {p[2], nb[2]} (type 3: position and angle channels) */
#define SU_MKEY_SIZE        16u             /* NJS_MKEY_F {keyframe, key[3]} */
#define SU_MAX_KEYS         1024u
/* the physics task's Super values (its pool words: code_8C0B3240.c) */
#define SU_SUPER_AIR_RESIST 0xBA83126Eu     /* -0.001 */
#define SU_SUPER_AIR_RES_AIR 0xBB03126Eu    /* -0.002 */
#define SU_SUPER_AIR_ACCEL  0x3D4CCCCCu     /* 0.05 */

typedef void (*su_tp_fn)(void *);
typedef void *(*su_cet_fn)(uint16_t, int32_t, sa1_gfn);

static int su_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

/* a pointer the General form's data may hold: the arena, or segment 0x18 (GT, GW, TMP and GT's actions live there) */
static int su_valid(uint32_t g)
{
    return g != 0 && (((g >> 24) & 31u) == SA1G_SEG_ARENA || ((g >> 24) & 31u) == SA1G_SEG_EXT);
}

/* the host bytes of a guest word in the arena or in the allocated part of segment 0x18 (NULL elsewhere) */
static uint8_t *su_ptr(const sa1g_world *w, uint32_t g)
{
    uint32_t off = g & 0xFFFFFCu;
    if (((g >> 24) & 31u) == SA1G_SEG_ARENA) {
        return off <= SA1G_ARENA_SIZE - 4u ? w->ram + off : NULL;
    }
    if (((g >> 24) & 31u) == SA1G_SEG_EXT && w->ext != NULL) {
        return (size_t)off + 4u <= w->ext_used ? w->ext + off : NULL;
    }
    return NULL;
}

static uint32_t su_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    const uint8_t *p = su_ptr(w, g);
    if (p != NULL) {
        memcpy(&v, p, 4);
    }
    return v;
}

static void su_wr32(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint8_t *p = su_ptr(w, g);
    if (p != NULL) {
        memcpy(p, &v, 4);
    }
}

static uint32_t su_rd16(const sa1g_world *w, uint32_t g)
{
    return (su_rd32(w, g & ~3u) >> (8u * (g & 2u))) & 0xFFFFu;
}

static void su_wr16(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint32_t a = g & ~3u, sh = 8u * (g & 2u), o = su_rd32(w, a);
    su_wr32(w, a, (o & ~(0xFFFFu << sh)) | ((v & 0xFFFFu) << sh));
}

static uint32_t su_rd8(const sa1g_world *w, uint32_t g)
{
    return (su_rd32(w, g & ~3u) >> (8u * (g & 3u))) & 0xFFu;
}

static void su_wr8(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint32_t a = g & ~3u, sh = 8u * (g & 3u), o = su_rd32(w, a);
    su_wr32(w, a, (o & ~(0xFFu << sh)) | ((v & 0xFFu) << sh));
}

static int su_same(uint32_t a, uint32_t b)
{
    return (a & 0x1FFFFFFFu) == (b & 0x1FFFFFFFu);
}

/* player 0's playerwk (guest; 0: none yet: PInitialize runs at the main's first exec) */
static uint32_t su_pwp0(const sa1g_world *w)
{
    uint32_t pwp = su_rd32(w, SA1G_ADDR_PLAYER_PWP);
    return su_arena(pwp) ? pwp : 0u;
}

static int su_super(const sa1g_world *w, uint32_t pwp)
{
    return pwp != 0 && (su_rd32(w, pwp + SU_PWP_EQUIPMENT) & 0x8000u) != 0;
}

static void *su_fn(uint32_t dc)
{
    return (void *)sa1_fn(SA1_GFN_C(dc), 0u);
}

/* the top-level tasks of the eight lists whose exec is one of two addresses (a freed task's exec is DestroyTask and is not
 * one); their count, the first `cap` in tps */
static uint32_t su_tasks(const sa1g_world *w, uint32_t exec_a, uint32_t exec_b, uint32_t *tps, uint32_t cap)
{
    uint32_t n = 0, lv;
    for (lv = 0; lv < 8u; lv++) {
        uint32_t tp = su_rd32(w, SA1G_ADDR_TASK_LISTS + 4u * lv), guard = 0;
        while (su_arena(tp) && guard++ < 0x1000u) {
            uint32_t ex = su_rd32(w, tp + 0x10u);
            if (su_same(ex, exec_a) || su_same(ex, exec_b)) {
                if (tps != NULL && n < cap) {
                    tps[n] = tp;
                }
                n++;
            }
            tp = su_rd32(w, tp);
        }
    }
    return n;
}

/* ---- the ring drain (the level task stand-in, once per step after its children) ------------------------------------- */
static void su_general_normal(sa1g_world *w);

void sa1g_super_drain(sa1g_world *w, uint32_t level_twp)
{
    uint32_t pwp;
    int16_t rings;
    if (!(w->super & SA1G_SUPER_WORLD) || (w->super & SA1G_SUPER_NO_DRAIN) || !su_arena(level_twp)) {
        return;
    }
    pwp = su_pwp0(w);
    if (!su_super(w, pwp)) {
        return;
    }
    memcpy(&rings, sa1g_world_ptr(w, SA1G_ADDR_RINGS), 2);
    if (w->super & SA1G_SUPER_GENERAL) {
        /* round 0.9.3a (design 4.6): the drain only while there are rings; at 0 the General revert in place of the drain's
           request 47, which the game would drop in Sonic's normal modes */
        if (rings > 0) {
            w->super_drains++;
            ((su_tp_fn)su_fn(SU_FN_DRAIN))(sa1_decode(level_twp));
        } else if (!(w->super & SA1G_SUPER_KEEP)) {
            ((void (*)(void))su_fn(SU_FN_RINGS_ZERO))();
            su_general_normal(w);
            w->sg.drain_reverts++;
        }
        return;
    }
    if (w->super & SA1G_SUPER_KEEP) {
        if (rings <= 0) {
            return;     /* KEEP_AT_ZERO: the drain would revert at 0 */
        }
    }
    w->super_drains++;
    ((su_tp_fn)su_fn(SU_FN_DRAIN))(sa1_decode(level_twp));
}

/* ---- the revert fix: the physics task's exec 8C0B3280 ------------------------------------------------------------- */
static void su_physics_exec(sa1g_world *w, uint32_t tp, void *user)
{
    uint32_t awp = su_rd32(w, tp + 0x2Cu), pwp = 0, v[4] = { 0, 0, 0, 0 };
    int fix = 0;
    (void)user;
    if (su_arena(awp) && *(const uint8_t *)sa1g_world_ptr(w, awp) == 0) {
        pwp = su_pwp0(w);       /* GetPlayerWorkPtr(0): the only player of a libsa1 world */
    }
    if (pwp != 0 && !su_super(w, pwp)) {
        fix = 1;                /* the exec frees the task now: DestroyTask -> the destructor 8C0B3240 */
        v[0] = su_rd32(w, pwp + SU_PWP_MAX_X_SPD);
        v[1] = su_rd32(w, pwp + SU_PWP_RUN_ACCEL);
        v[2] = su_rd32(w, awp + 4u);    /* saved air_resist */
        v[3] = su_rd32(w, awp + 8u);    /* saved air_resist_air */
    }
    ((su_tp_fn)su_fn(SU_FN_PHYS_EXEC))(sa1_decode(tp));
    if (fix) {
        su_wr32(w, pwp + SU_PWP_MAX_X_SPD, v[0]);
        su_wr32(w, pwp + SU_PWP_RUN_ACCEL, v[1]);
        su_wr32(w, pwp + SU_PWP_AIR_RESIST, v[2]);
        su_wr32(w, pwp + SU_PWP_AIR_RES_AIR, v[3]);
        w->super_reverts++;
    }
}

/* ---- the General form: speed (design 4.5) ------------------------------------------------------------------------- */
static uint32_t su_fbits(float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    return v;
}

static float su_float(uint32_t v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* the set's three values from the physics task's saves of Sonic's (awp +4 air_resist, +8 air_resist_air, +0xC air_accel);
 * inside the world (HALF's arithmetic in the world's FP mode) */
static void su_speed_write(sa1g_world *w, uint32_t pwp, uint32_t awp)
{
    uint32_t s_ar = su_rd32(w, awp + 4u), s_ara = su_rd32(w, awp + 8u), s_aa = su_rd32(w, awp + 0x0Cu);
    uint32_t ar = SU_SUPER_AIR_RESIST, ara = SU_SUPER_AIR_RES_AIR, aa = SU_SUPER_AIR_ACCEL;
    switch (w->sg.speed) {
    case SA1_SUPER_SPEED_RUN:
        ara = s_ara;
        aa = s_aa;
        break;
    case SA1_SUPER_SPEED_HALF:
        ar = su_fbits((su_float(s_ar) + su_float(SU_SUPER_AIR_RESIST)) * 0.5f);
        ara = su_fbits((su_float(s_ara) + su_float(SU_SUPER_AIR_RES_AIR)) * 0.5f);
        aa = su_fbits((su_float(s_aa) + su_float(SU_SUPER_AIR_ACCEL)) * 0.5f);
        break;
    case SA1_SUPER_SPEED_SONIC:
        ar = s_ar;
        ara = s_ara;
        aa = s_aa;
        break;
    default:
        break;                  /* GAME: the task's own values */
    }
    su_wr32(w, pwp + SU_PWP_AIR_RESIST, ar);
    su_wr32(w, pwp + SU_PWP_AIR_RES_AIR, ara);
    su_wr32(w, pwp + SU_PWP_AIR_ACCEL, aa);
}

/* the physics task's first exec in a General world: the original, then the host's set, and Sonic's own center_height
 * (round 0.9.3a, measured: Sonic's motions on the Super body at the task's 6.4 draw his lowest point 1.02 units above the
 * ground, plain Sonic's 0.40; at Sonic's 5.4 it is 0.02: he stands on the ground. The game's 6.4 belongs to its own Super
 * motions, which a General world does not use. The destructor 8C0B3240 writes 5.4 back itself: the revert stays exact) */
static void su_physics_init(sa1g_world *w, uint32_t tp, void *user)
{
    uint32_t awp = su_rd32(w, tp + 0x2Cu), pwp;
    (void)user;
    ((su_tp_fn)su_fn(SU_FN_PHYS_INIT))(sa1_decode(tp));
    pwp = su_pwp0(w);
    if (su_arena(awp) && pwp != 0 && su_rd8(w, awp) == 0) {
        if (w->sg.speed != SA1_SUPER_SPEED_GAME) {
            su_speed_write(w, pwp, awp);
        }
        su_wr32(w, pwp + SU_PWP_CENTER_H, su_rd32(w, SU_ADDR_PHYS_SONIC + 0x80u));
        w->sg.speed_hooks++;
    }
}

/* a set change while Super: the running physics task's saves (a task still before its first exec gets it there) */
static void su_speed_now(sa1g_world *w)
{
    uint32_t tps[4], n = su_tasks(w, SU_FN_PHYS_EXEC, SU_FN_PHYS_EXEC, tps, 4), k, pwp = su_pwp0(w);
    if (pwp == 0 || !su_super(w, pwp)) {
        return;
    }
    for (k = 0; k < n && k < 4u; k++) {
        uint32_t awp = su_rd32(w, tps[k] + 0x2Cu);
        if (su_arena(awp) && su_rd8(w, awp) == 0) {
            su_speed_write(w, pwp, awp);
        }
    }
}

/* ---- the General form: effects (review RC14) ---------------------------------------------------------------------- */
static int su_clone_ok(sa1g_world *w, uint32_t tp)
{
    uint32_t twp = su_rd32(w, tp + 0x20u);
    return (w->sg.effects & SA1_SUPER_EFFECTS_CLONES) && su_super(w, su_pwp0(w)) && su_arena(twp) &&
           su_rd8(w, twp + 0x08u) == 0;
}

static void su_trail_init(sa1g_world *w, uint32_t tp, void *user)
{
    (void)user;
    if (su_clone_ok(w, tp)) {
        w->sg.effect_clones++;
        ((su_tp_fn)su_fn(SU_FN_TRAIL_SS))(sa1_decode(tp));
    } else {
        ((su_tp_fn)su_fn(SU_FN_TRAIL))(sa1_decode(tp));
    }
}

static void su_ballai_init(sa1g_world *w, uint32_t tp, void *user)
{
    (void)user;
    if (su_clone_ok(w, tp)) {
        w->sg.effect_clones++;
        ((su_tp_fn)su_fn(SU_FN_BALLAI_SS))(sa1_decode(tp));
    } else {
        ((su_tp_fn)su_fn(SU_FN_BALLAI))(sa1_decode(tp));
    }
}

void sa1g_super_boot(sa1g_world *w)
{
    if (!(w->super & SA1G_SUPER_WORLD)) {
        return;
    }
    if (!(w->super & SA1G_SUPER_FAITHFUL) && sa1g_world_xhook(w, SU_FN_PHYS_EXEC, su_physics_exec, NULL) != 0) {
        sa1g_fatal(0, "Super Sonic: no executor override left for the revert fix");
    }
    if ((w->super & SA1G_SUPER_GENERAL) &&
        (sa1g_world_xhook(w, SU_FN_PHYS_INIT, su_physics_init, NULL) != 0 ||
         sa1g_world_xhook(w, SU_FN_TRAIL, su_trail_init, NULL) != 0 ||
         sa1g_world_xhook(w, SU_FN_BALLAI, su_ballai_init, NULL) != 0)) {
        sa1g_fatal(0, "Super Sonic: no executor override left for the General form");
    }
}

/* ---- the General form: model trees ----------------------------------------------------------------------------------- */
/* node n of a tree in the game's depth-first order (node, its child subtree, then its sibling: NJS_OBJECT child +0x2C,
 * sibling +0x30); 0 if the tree is smaller */
static uint32_t su_node_of(const sa1g_world *w, uint32_t root, int32_t n)
{
    uint32_t stack[128], sp = 0, obj = root;
    int32_t k = 0;
    while (su_arena(obj) || sp) {
        uint32_t sib;
        if (!su_arena(obj)) {
            obj = stack[--sp];
            continue;
        }
        if (k++ == n) {
            return obj;
        }
        sib = su_rd32(w, obj + 0x30u);
        if (su_arena(sib) && sp < 128u) {
            stack[sp++] = sib;
        }
        obj = su_rd32(w, obj + 0x2Cu);
        if (k > 512) {
            break;
        }
    }
    return 0;
}

static int32_t su_index_of(const sa1g_world *w, uint32_t root, uint32_t obj)
{
    int32_t n;
    for (n = 0; n < 160; n++) {
        uint32_t o = su_node_of(w, root, n);
        if (o == 0) {
            return -1;
        }
        if (su_same(o, obj)) {
            return n;
        }
    }
    return -1;
}

static uint32_t su_node_count(const sa1g_world *w, uint32_t root)
{
    uint32_t n = 0;
    while (n < 255u && su_node_of(w, root, (int32_t)n) != 0) {
        n++;
    }
    return n;
}

/* ---- the General form: GT (the look's action table, design 4.3) ---------------------------------------------------- */
static int su_build_gt(sa1g_world *w)
{
    uint32_t gt, acts, i, nb_body = su_node_count(w, SU_OBJ_SUPER_BODY), nb_ball = su_node_count(w, SU_OBJ_SUPER_BALL);
    if (nb_body == 0 || nb_ball == 0) {
        return -1;                          /* no B_CHAOS7 models in the image */
    }
    gt = sa1g_world_ext_alloc(w, SU_NUM_ACTIONS * 16u, 4, NULL);
    acts = sa1g_world_ext_alloc(w, SU_NUM_ACTIONS * 8u, 4, NULL);
    if (gt == 0 || acts == 0) {
        return -1;
    }
    for (i = 0; i < SU_NUM_ACTIONS; i++) {
        uint32_t src = SU_ADDR_ACTIONS + 16u * i, dst = gt + 16u * i, act = su_rd32(w, src), k, obj, mot, nobj = 0, nn = 0;
        for (k = 0; k < 16u; k += 4u) {
            su_wr32(w, dst + k, su_rd32(w, src + k));
        }
        if (!su_arena(act)) {
            continue;
        }
        obj = su_rd32(w, act);
        mot = su_rd32(w, act + 4u);
        if (su_same(obj, SU_OBJ_SONIC_BODY)) {
            nobj = SU_OBJ_SUPER_BODY;
            nn = nb_body;
        } else if (su_same(obj, SU_OBJ_SONIC_BALL)) {
            nobj = SU_OBJ_SUPER_BALL;
            nn = nb_ball;
        } else if (su_same(obj, SU_OBJ_SONIC_CURLED)) {
            nobj = SU_OBJ_SUPER_BALL;
            mot = SU_MOT_SPIN_BALL;         /* as action 141 */
            nn = 1u;
        } else {
            continue;
        }
        su_wr32(w, acts + 8u * i, nobj);
        su_wr32(w, acts + 8u * i + 4u, mot);
        su_wr32(w, dst, acts + 8u * i);
        if (su_rd8(w, dst + 4u) > nn) {
            su_wr8(w, dst + 4u, nn);        /* objnum: the link builder 8C079C76 walks that many nodes of the object */
        }
    }
    w->sg.gt = gt;
    return 0;
}

/* ---- the General form: GA (SA1_SUPOPT_SUPER_ANIMS; super2_plan.md 6.1 task 2, super2_anim.md 6.1, owner answer O1) ---- */
/* a copy of motion `m` of the Super body (nn nodes) whose node 0 position keys are lifted by `lift` (y): the original keys
 * + (0, lift, 0), or, where node 0 has no position keys, one key per frame at the root's rest position + (0, lift, 0);
 * every other NJS_MDATA2 entry copied (pointers only). Returns the copy's guest address (0: failed) */
static uint32_t su_lift_motion(sa1g_world *w, uint32_t m, uint32_t nn, float lift, const float rest[3])
{
    uint32_t md = su_rd32(w, m), nf = su_rd32(w, m + 4u), type = su_rd16(w, m + 8u), inp = su_rd16(w, m + 0x0Au);
    uint32_t mot, mdc, keys, p0, nb0, k, i, n;
    if (!su_arena(md) || nf == 0 || nf > SU_MAX_KEYS || (type & 0x0Fu) != 3u || nn == 0) {
        return 0;
    }
    p0 = su_rd32(w, md);
    nb0 = su_rd32(w, md + 8u);
    if (nb0 > SU_MAX_KEYS || (nb0 != 0 && !su_arena(p0))) {
        return 0;
    }
    n = nb0 != 0 ? nb0 : nf;
    mot = sa1g_world_ext_alloc(w, 12u, 4, NULL);
    mdc = sa1g_world_ext_alloc(w, nn * SU_MDATA2_SIZE, 4, NULL);
    keys = sa1g_world_ext_alloc(w, n * SU_MKEY_SIZE, 4, NULL);
    if (mot == 0 || mdc == 0 || keys == 0) {
        return 0;
    }
    for (i = 0; i < nn * SU_MDATA2_SIZE; i += 4u) {
        su_wr32(w, mdc + i, su_rd32(w, md + i));
    }
    for (k = 0; k < n; k++) {
        uint32_t d = keys + SU_MKEY_SIZE * k;
        if (nb0 != 0) {
            uint32_t src = p0 + SU_MKEY_SIZE * k;
            su_wr32(w, d, su_rd32(w, src));
            su_wr32(w, d + 4u, su_rd32(w, src + 4u));
            su_wr32(w, d + 8u, su_fbits(su_float(su_rd32(w, src + 8u)) + lift));
            su_wr32(w, d + 12u, su_rd32(w, src + 12u));
        } else {
            su_wr32(w, d, k);
            su_wr32(w, d + 4u, su_fbits(rest[0]));
            su_wr32(w, d + 8u, su_fbits(rest[1] + lift));
            su_wr32(w, d + 12u, su_fbits(rest[2]));
        }
    }
    su_wr32(w, mdc, keys);
    su_wr32(w, mdc + 8u, n);
    su_wr32(w, mot, mdc);
    su_wr32(w, mot + 4u, nf);
    su_wr32(w, mot + 8u, (inp << 16) | type);
    return mot;
}

/* GA = GT with the stand / move rows drawing the game's own Super motions, lifted (super2_plan.md 6.1 task 2; the rows:
 * sa1w_super_anims_row, sa1_api_world.h). Built right after GT, in segment 0x18 (reset by apply_image). L = the physics
 * task's center_height (its pool word, 6.4) minus Sonic's (player_parameter[0] +0x80: 5.4), both read from the image.
 * Inside the world (after a pair). 0 built, -1 failed */
static int su_build_ga(sa1g_world *w)
{
    static const uint32_t k_mot[4] = { SU_MOT_SS_WAIT, SU_MOT_SS_MOVE0, SU_MOT_SS_MOVE1, SU_MOT_SS_MOVE2 };
    uint32_t ga, acts, i, k, nn = su_node_count(w, SU_OBJ_SUPER_BODY);
    float ch_super = su_float(su_rd32(w, SU_ADDR_PHYS_CH)), ch_sonic = su_float(su_rd32(w, SU_ADDR_PHYS_SONIC + 0x80u));
    float lift = ch_super - ch_sonic, rest[3];
    if (w->sg.gt == 0 || nn == 0 || nn > 255u || !(lift > 0.0f && lift < 4.0f)) {
        return -1;                          /* no GT, no Super body, or the two center_heights are not the game's */
    }
    for (k = 0; k < 3u; k++) {
        rest[k] = su_float(su_rd32(w, SU_OBJ_SUPER_BODY + 8u + 4u * k));     /* NJS_OBJECT.pos of the root */
    }
    acts = sa1g_world_ext_alloc(w, 4u * 8u, 4, NULL);
    ga = sa1g_world_ext_alloc(w, SU_NUM_ACTIONS * 16u, 4, NULL);
    if (acts == 0 || ga == 0) {
        return -1;
    }
    for (k = 0; k < 4u; k++) {
        uint32_t m = su_lift_motion(w, k_mot[k], nn, lift, rest);
        if (m == 0) {
            return -1;
        }
        w->sg.ga_mot[k] = m;
        w->sg.ga_act[k] = acts + 8u * k;
        su_wr32(w, acts + 8u * k, SU_OBJ_SUPER_BODY);
        su_wr32(w, acts + 8u * k + 4u, m);
    }
    for (i = 0; i < SU_NUM_ACTIONS * 16u; i += 4u) {
        su_wr32(w, ga + i, su_rd32(w, w->sg.gt + i));
    }
    for (i = 0; i < SU_NUM_ACTIONS; i++) {
        uint32_t sa = sa1w_super_anims_row(i), dst = ga + 16u * i, src;
        if (sa == 0) {
            continue;
        }
        src = SU_ADDR_ACTIONS + 16u * sa;
        su_wr32(w, dst, w->sg.ga_act[sa == 134u ? 3u : sa - 130u]);
        su_wr8(w, dst + 4u, nn < 73u ? nn : 73u);                       /* objnum: the Super body's 73 nodes */
        su_wr32(w, dst + 8u, su_rd32(w, src + 8u));                     /* link speed: the Super action's */
        su_wr32(w, dst + 12u, su_rd32(w, src + 12u));                   /* racio: the Super action's */
        if (!sa1w_super_anims_chain(i)) {
            su_wr8(w, dst + 5u, su_rd8(w, src + 5u));                   /* loop rows: its mtnmode (3) */
            su_wr8(w, dst + 6u, i);                                     /* next: the row itself (Sonic's numbering) */
        }                                                               /* chain rows keep Sonic's mtnmode and next */
    }
    w->sg.lift = lift;
    w->sg.ga = ga;
    return 0;
}

/* ---- the General form: GW (the welds, design 4.2) ------------------------------------------------------------------ */
typedef struct su_setup {
    uint32_t tmp;
    int ok;
} su_setup;

static void su_copy_weld(sa1g_world *w, uint32_t d, uint32_t s)
{
    uint32_t k;
    for (k = 0; k < SU_WELD_SIZE; k += 4u) {
        su_wr32(w, d + k, su_rd32(w, s + k));
    }
}

/* the weld set-up after the first pair: 1 done, 0 not yet (SonicMain's mode 0 has not run), -1 failed. Inside the world. */
static int su_build_gw(sa1g_world *w)
{
    sa1g_player_ptrs pp;
    uint32_t nw = 0, e, i, j, tmp, gw, ntmp = 0, nhand = 0, nsuper = 0, k;
    uint32_t need[SU_MAX_WELDS];
    uint32_t save_jv;
    uint32_t save_mode;
    sa1g_world_player(w, &pp);
    if (pp.twp == 0 || pp.mwp == 0 || pp.pwp == 0) {
        return 0;
    }
    if ((int16_t)su_rd16(w, pp.pwp + SU_PWP_JVMODE) != 1 || !su_same(su_rd32(w, pp.pwp + SU_PWP_PLJVPTR), SU_ADDR_WELDS)) {
        return 0;
    }
    for (e = SU_ADDR_WELDS; su_rd32(w, e) != 0 && nw < SU_MAX_WELDS; e += SU_WELD_SIZE) {
        nw++;
    }
    tmp = sa1g_world_ext_alloc(w, (nw + 1u) * SU_WELD_SIZE, 4, NULL);
    if (tmp == 0) {
        return -1;
    }
    /* the Super entries that need the set-up (inpmode 0 or no org: off stage 0x13xx), with the disc's inpmode 2 */
    for (i = 0; i < nw; i++) {
        uint32_t s = SU_ADDR_WELDS + SU_WELD_SIZE * i;
        need[i] = 0;
        if (!su_same(su_rd32(w, s), SU_OBJ_SUPER_BODY)) {
            continue;
        }
        nsuper++;
        if (su_rd8(w, s + 0x0Du) != 0 && su_rd32(w, s + 0x10u) != 0) {
            continue;                       /* set up by mode 0 itself (stage 0x13xx) */
        }
        need[i] = 1;
        su_copy_weld(w, tmp + SU_WELD_SIZE * ntmp, s);
        su_wr8(w, tmp + SU_WELD_SIZE * ntmp + 0x0Du, 2u);
        su_wr32(w, tmp + SU_WELD_SIZE * ntmp + 0x10u, 0u);
        ntmp++;
    }
    /* Sonic's hand / foot / head points (inpmode 4-9) on the Super body's node of the same index (the Light Speed Shoes'
       own entries are not in the body tree and stay Sonic's) */
    for (i = 0; i < nw; i++) {
        uint32_t s = SU_ADDR_WELDS + SU_WELD_SIZE * i, inp = su_rd8(w, s + 0x0Du), d, node;
        int32_t n;
        if (!su_same(su_rd32(w, s), SU_OBJ_SONIC_BODY) || inp < 4u || inp > 9u) {
            continue;
        }
        n = su_index_of(w, SU_OBJ_SONIC_BODY, su_rd32(w, s + 4u));
        node = n >= 0 ? su_node_of(w, SU_OBJ_SUPER_BODY, n) : 0u;
        if (node == 0) {
            continue;
        }
        d = tmp + SU_WELD_SIZE * (ntmp + nhand);
        su_copy_weld(w, d, s);
        su_wr32(w, d, SU_OBJ_SUPER_BODY);
        su_wr32(w, d + 4u, node);
        nhand++;
        if (ntmp + nhand >= nw) {
            break;
        }
    }
    for (k = 0; k < SU_WELD_SIZE; k += 4u) {
        su_wr32(w, tmp + SU_WELD_SIZE * (ntmp + nhand) + k, 0u);
    }
    /* the game's own set-up (PJoinVertexes jvmode 0) on TMP, pljvptr / jvmode put back around it */
    save_jv = su_rd32(w, pp.pwp + SU_PWP_PLJVPTR);
    save_mode = su_rd16(w, pp.pwp + SU_PWP_JVMODE);
    su_wr32(w, pp.pwp + SU_PWP_PLJVPTR, tmp);
    su_wr16(w, pp.pwp + SU_PWP_JVMODE, 0u);
    ((void (*)(void *, void *, void *))su_fn(SU_FN_PJOIN))(sa1_decode(pp.twp), sa1_decode(pp.mwp), sa1_decode(pp.pwp));
    k = su_rd16(w, pp.pwp + SU_PWP_JVMODE);
    su_wr32(w, pp.pwp + SU_PWP_PLJVPTR, save_jv);
    su_wr16(w, pp.pwp + SU_PWP_JVMODE, save_mode);
    if (k != 1u) {
        return -1;
    }
    /* GW: SonicWeldList with the set-up copies in place of its Super entries, then the hand entries */
    gw = sa1g_world_ext_alloc(w, (nw + nhand + 1u) * SU_WELD_SIZE, 4, NULL);
    if (gw == 0) {
        return -1;
    }
    for (i = 0, j = 0; i < nw; i++) {
        su_copy_weld(w, gw + SU_WELD_SIZE * i, need[i] ? tmp + SU_WELD_SIZE * j++ : SU_ADDR_WELDS + SU_WELD_SIZE * i);
    }
    for (i = 0; i < nhand; i++) {
        su_copy_weld(w, gw + SU_WELD_SIZE * (nw + i), tmp + SU_WELD_SIZE * (ntmp + i));
    }
    for (k = 0; k < SU_WELD_SIZE; k += 4u) {
        su_wr32(w, gw + SU_WELD_SIZE * (nw + nhand) + k, 0u);
    }
    su_wr32(w, pp.pwp + SU_PWP_PLJVPTR, gw);
    w->sg.tmp = tmp;
    w->sg.gw = gw;
    w->sg.gw_entries = nw + nhand;
    (void)nsuper;
    return 1;
}

/* ---- the General form: the look (design 4.3, review RC7) ------------------------------------------------------------ */
static void su_names(sa1g_world *w, int super)
{
    su_wr32(w, SU_ADDR_TEXLIST, super ? SU_NAMES_SUPER : SU_NAMES_SONIC);
    su_wr32(w, SU_ADDR_TEXLIST + 4u, super ? SU_NAMES_SUPER_N : SU_NAMES_SONIC_N);
    w->sg.names_super = super;
}

/* the object of action a in an action table (0: none) */
static uint32_t su_action_object(const sa1g_world *w, uint32_t table, uint32_t a)
{
    uint32_t act;
    if (a >= SU_NUM_ACTIONS) {
        return 0;
    }
    act = su_rd32(w, table + 16u * a);
    return su_valid(act) ? su_rd32(w, act) : 0u;
}

/* round 0.9.4a (super2_plan.md 6.1 task 3): the look's table - GA while SA1_SUPOPT_SUPER_ANIMS is on and GA exists, else GT */
static uint32_t su_look_table(const sa1g_world *w)
{
    return (w->sg.anims && w->sg.ga != 0) ? w->sg.ga : w->sg.gt;
}

/* on: GT (GA with the Super poses) and the SUPERSONIC names; off: SonicActionList and Sonic's names. A link action being
 * drawn (mtnmode 2) gets the other table's object of the requested action (design 4.3 "Link actions", V12); "from" is the
 * table now in plactptr (round 0.9.4a: GT and GA name the same objects, so the fix is the same for both). Inside the world. */
static void su_look_set(sa1g_world *w, int on)
{
    uint32_t pwp = su_pwp0(w), from, to, req, lk, cur;
    if (pwp == 0 || w->sg.gt == 0) {
        return;
    }
    cur = su_rd32(w, pwp + SU_PWP_PLACTPTR);
    from = (su_same(cur, SU_ADDR_ACTIONS) || cur == w->sg.gt || (w->sg.ga != 0 && cur == w->sg.ga))
               ? cur : (on ? SU_ADDR_ACTIONS : w->sg.gt);
    to = on ? su_look_table(w) : SU_ADDR_ACTIONS;
    req = su_rd16(w, pwp + SU_PWP_REQACTION);
    lk = su_rd32(w, pwp + SU_PWP_ACTWKPTR);
    if ((int16_t)su_rd16(w, pwp + SU_PWP_MTNMODE) == 2 && su_arena(lk)) {
        uint32_t obj = su_rd32(w, lk), want = su_action_object(w, to, req);
        if (want != 0 && su_same(obj, su_action_object(w, from, req)) && !su_same(obj, want)) {
            su_wr32(w, lk, want);
            w->sg.link_fixes++;
        }
    }
    su_wr32(w, pwp + SU_PWP_PLACTPTR, to);
    su_names(w, on);
    w->sg.look = on;
    w->sg.guard_on = 0;
}

/* RC7: while the look is on, the object Sonic's display will draw (8C0AE17A: the link action while mtnmode is 2, else the
 * requested action, 32 for the spin dash's flag rule) must be a Super object; else Sonic's names for that pair (the game's
 * own Super actions 130-141 bind TEXLIST_SUPERSONIC themselves). Inside the world, after a pair. */
static void su_guard(sa1g_world *w)
{
    sa1g_player_ptrs pp;
    uint32_t a, obj, lk;
    int ok;
    sa1g_world_player(w, &pp);
    if (pp.pwp == 0 || pp.twp == 0) {
        return;
    }
    a = su_rd16(w, pp.pwp + SU_PWP_REQACTION);
    lk = su_rd32(w, pp.pwp + SU_PWP_ACTWKPTR);
    if ((int16_t)su_rd16(w, pp.pwp + SU_PWP_MTNMODE) == 2 && su_arena(lk)) {
        a = su_rd16(w, pp.pwp + SU_PWP_REQACTION + 2u);    /* mj.action */
        obj = su_rd32(w, lk);
    } else {
        if ((su_rd16(w, pp.twp + 0x04u) & 0x100u) && a != 141u && (su_rd16(w, pp.pwp + SU_PWP_FREE_W1) & 0x11u)) {
            a = 32u;
        }
        obj = su_action_object(w, su_rd32(w, pp.pwp + SU_PWP_PLACTPTR), a);
    }
    ok = su_same(obj, SU_OBJ_SUPER_BODY) || su_same(obj, SU_OBJ_SUPER_BALL) || (a >= 130u && a <= 141u);
    if (ok && !w->sg.names_super) {
        su_names(w, 1);
        w->sg.guard_on = 0;
    } else if (!ok && w->sg.names_super) {
        su_names(w, 0);
        if (!w->sg.guard_on) {
            w->sg.guard_warns++;
            w->sg.warn_pending |= SA1W_SUPER_WARN_GUARD;
        }
        w->sg.guard_on = 1;
    }
}

/* ---- the General form: the transformation and the revert (design 4.4) ---------------------------------------------- */
static void su_voice(sa1g_world *w)
{
    if (w->sg.voice > 0 && w->sg.voice < 2048) {
        ((void (*)(int32_t, int32_t, int32_t, int32_t))su_fn(SU_FN_SE))(w->sg.voice, 0, 0, 0);
        w->sg.voices++;
    }
}

static void su_make_aura(sa1g_world *w, uint32_t pno)
{
    void *tp = ((su_cet_fn)su_fn(SA1G_FN_CREATE_TASK))(2u, 2, SA1_GFN_C(SU_FN_AURA_INIT));
    if (tp != NULL) {
        uint32_t t = sa1_encode(tp), twp = su_rd32(w, t + 0x20u);
        if (su_arena(twp)) {
            su_wr8(w, twp + 0x08u, pno);    /* tp->twp->counter.b[0] = the player */
        }
    }
}

static void su_free_auras(sa1g_world *w)
{
    uint32_t tps[8], n = su_tasks(w, SU_FN_AURA_INIT, SU_FN_AURA, tps, 8), k;
    for (k = 0; k < n && k < 8u; k++) {
        ((su_tp_fn)su_fn(SU_FN_FREETASK))(sa1_decode(tps[k]));
    }
}

/* request 46's body without its three mode lines, then the look and the voice; 1 done, 0 already Super, -1 no player.
 * Round 0.9.4 (the 0.9.3a verifier): the aura and physics tasks of the last Super stretch free themselves only at their
 * next exec once the bit is gone; a NORMAL and a SUPER order with no pair between leave them alive, so a task that is
 * still running (not freed: its exec is not DestroyTask) is kept instead of a second one being made - one aura, one
 * physics task, whose saved values (awp +4 / +8 / +0xC) are still Sonic's own */
static int su_general_super(sa1g_world *w, int voice)
{
    uint32_t pwp = su_pwp0(w), twp = su_rd32(w, SA1G_ADDR_PLAYER_TWP), pno;
    void *tp;
    if (pwp == 0 || !su_arena(twp)) {
        return -1;
    }
    if (su_super(w, pwp)) {
        return 0;
    }
    pno = su_rd8(w, twp + 0x08u);
    su_wr16(w, pwp + SU_PWP_EQUIPMENT, su_rd16(w, pwp + SU_PWP_EQUIPMENT) | 0x8000u);
    if (!w->sg.no_aura && su_tasks(w, SU_FN_AURA_INIT, SU_FN_AURA, NULL, 0) == 0) {
        su_make_aura(w, pno);
    }
    tp = su_tasks(w, SU_FN_PHYS_INIT, SU_FN_PHYS_EXEC, NULL, 0) == 0
             ? ((su_cet_fn)su_fn(SA1G_FN_CREATE_TASK))(8u, 2, SA1_GFN_C(SU_FN_PHYS_INIT)) : NULL;
    if (tp != NULL) {
        uint32_t awp = su_rd32(w, sa1_encode(tp) + 0x2Cu);
        if (su_arena(awp)) {
            su_wr8(w, awp, pno);
        }
    }
    su_look_set(w, 1);
    if (voice) {
        su_voice(w);
    }
    w->sg.transforms++;
    return 1;
}

static void su_general_normal(sa1g_world *w)
{
    uint32_t pwp = su_pwp0(w);
    if (pwp != 0 && su_super(w, pwp)) {
        su_wr16(w, pwp + SU_PWP_EQUIPMENT, su_rd16(w, pwp + SU_PWP_EQUIPMENT) & 0x7FFFu);
        w->sg.reverts++;
    }
    if (w->sg.look) {
        su_look_set(w, 0);
    }
}

/* ---- after each live pair (sa1g_world_pair, inside the world) -------------------------------------------------------- */
void sa1g_super_pair(sa1g_world *w)
{
    uint32_t pwp, twp, mode;
    int sup;
    if (!(w->super & SA1G_SUPER_WORLD)) {
        return;
    }
    if (w->super & SA1G_SUPER_GENERAL) {
        /* round 0.9.4a (super2_plan.md 5.4): the look the pair's display drew with, before anything below can change it
           (sa1w_world_lsd_gold) */
        w->sg.drawn_look = w->sg.look;
        w->sg.drawn_pair = 1;
    }
    pwp = su_pwp0(w);
    if (pwp == 0) {
        return;
    }
    if (!(w->super & SA1G_SUPER_GENERAL)) {
        /* request worlds (RC11): the voice of the host's request 46 once the game took it */
        if (w->sg.voice_armed && su_super(w, pwp)) {
            w->sg.voice_armed = 0;
            su_voice(w);
        }
        return;
    }
    if (w->sg.welds == 0) {
        int r = su_build_gw(w);
        if (r != 0) {
            w->sg.welds = r;
            if (r < 0) {
                w->sg.warn_pending |= SA1W_SUPER_WARN_WELDS;
            }
            if (w->sg.gt == 0 && su_build_gt(w) != 0) {
                w->sg.welds = -1;
                w->sg.warn_pending |= SA1W_SUPER_WARN_WELDS;
            }
        }
    }
    if (w->sg.gt != 0 && w->sg.ga == 0 && !w->sg.ga_failed && su_build_ga(w) != 0) {
        /* round 0.9.4a: GA failed (no B_CHAOS7 motions, the game's values not found, segment 0x18 full): the look keeps
           GT (SA1_SUPOPT_SUPER_ANIMS then changes nothing) */
        w->sg.ga = 0;
        w->sg.ga_failed = 1;
        w->sg.warn_pending |= SA1W_SUPER_WARN_GA;
    }
    if (w->sg.welds != 0 && w->sg.gt != 0) {
        if ((w->super & SA1G_SUPER_START) && !w->sg.start_done) {
            w->sg.start_done = 1;
            su_general_super(w, 0);         /* SA1_SUPER_START: no voice (RC11) */
        }
        if (w->sg.pending == SA1_ORDER_SUPER) {
            su_general_super(w, w->sg.pending_voice);
        } else if (w->sg.pending == SA1_ORDER_NORMAL) {
            su_general_normal(w);
        }
        w->sg.pending = 0;
        w->sg.pending_voice = 0;
        /* the look follows the bit (the game may clear it itself: requests 47 / 50 in mode 20's Super handler) */
        sup = su_super(w, pwp);
        if (sup && !w->sg.look) {
            su_look_set(w, 1);
        } else if (!sup && w->sg.look) {
            su_look_set(w, 0);
            w->sg.reverts++;
            w->sg.bit_reverts++;
        }
        if (w->sg.look) {
            su_guard(w);
        }
    }
    twp = su_rd32(w, SA1G_ADDR_PLAYER_TWP);
    mode = su_arena(twp) ? su_rd8(w, twp) : 0u;
    if (mode >= 71u && mode <= 78u) {
        w->sg.leaks++;
        if (!w->sg.leak_seen) {
            w->sg.leak_seen = 1;
            w->sg.warn_pending |= SA1W_SUPER_WARN_LEAK;
        }
    }
}

uint32_t sa1w_world_super_warnings(struct sa1g_world *w)
{
    uint32_t v;
    if (w == NULL) {
        return 0;
    }
    v = w->sg.warn_pending;
    w->sg.warn_pending = 0;
    return v;
}

/* ---- orders (sa1_character_order) ------------------------------------------------------------------------------------ */
typedef struct su_order {
    int32_t order;
    int32_t result;     /* 1 done / sent, 2 queued for the first pair, 0 already in that form (or pending), -1 no player 0 */
} su_order;

static void su_order_cb(sa1g_world *w, void *user)
{
    su_order *o = (su_order *)user;
    uint32_t twp = su_rd32(w, SA1G_ADDR_PLAYER_TWP), pwp = su_pwp0(w);
    int sup;
    if (!su_arena(twp)) {
        o->result = -1;
        return;
    }
    sup = su_super(w, pwp);     /* before the first step (no playerwk yet) he is not Super: the request waits for it */
    if ((o->order == SA1_ORDER_SUPER && sup) || (o->order == SA1_ORDER_NORMAL && !sup)) {
        o->result = 0;
        if (o->order == SA1_ORDER_NORMAL) {
            w->sg.voice_armed = 0;
        }
        return;
    }
    if (o->order == SA1_ORDER_NORMAL) {
        w->sg.voice_armed = 0;
    }
    /* round 0.9.3a (RC11): the same request still pending (taskwk.flag 0x1000, smode = the order) is not sent again: the
       game would store the same two values */
    if ((su_rd16(w, twp + 0x04u) & 0x1000u) && su_rd8(w, twp + 0x01u) == (uint32_t)o->order) {
        o->result = 0;
        return;
    }
    ((void (*)(uint8_t, int32_t))su_fn(SU_FN_SET_INPUT))(0, o->order);
    if (o->order == SA1_ORDER_SUPER) {
        w->sg.voice_armed = 1;  /* the voice option plays when the game takes it (sa1g_super_pair) */
    }
    o->result = 1;
}

/* a General world: the transformation / revert at once, or queued until the weld set-up after the first pair */
static void su_order_general_cb(sa1g_world *w, void *user)
{
    su_order *o = (su_order *)user;
    uint32_t twp = su_rd32(w, SA1G_ADDR_PLAYER_TWP), pwp = su_pwp0(w);
    if (!su_arena(twp)) {
        o->result = -1;
        return;
    }
    if (pwp == 0 || w->sg.welds == 0 || w->sg.gt == 0) {
        if (o->order == SA1_ORDER_SUPER) {
            w->sg.pending = SA1_ORDER_SUPER;
            w->sg.pending_voice = 1;
        } else {
            w->sg.pending = 0;          /* NORMAL before the first pair: a waiting order and SA1_SUPER_START are off */
            w->sg.pending_voice = 0;
            w->sg.start_done = 1;
        }
        o->result = 2;
        return;
    }
    if (o->order == SA1_ORDER_SUPER) {
        o->result = su_general_super(w, 1);
    } else if (su_super(w, pwp)) {
        su_general_normal(w);
        o->result = 1;
    } else {
        o->result = 0;
    }
}

sa1_status sa1w_world_order(struct sa1g_world *w, int32_t order)
{
    su_order o;
    sa1_status st;
    if (w == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    if (!(w->super & SA1G_SUPER_WORLD) || (order != SA1_ORDER_SUPER && order != SA1_ORDER_NORMAL)) {
        return SA1_ERR_UNSUPPORTED;
    }
    o.order = order;
    o.result = -1;
    st = sa1g_world_call(w, (w->super & SA1G_SUPER_GENERAL) ? su_order_general_cb : su_order_cb, &o);
    if (st != SA1_OK) {
        return st;
    }
    return o.result < 0 ? SA1_ERR_NOT_LOADED : SA1_OK;
}

/* ---- the host's Super options (sa1_character_set_super_option; outside ticks) ------------------------------------ */
typedef struct su_opt {
    uint32_t option;
} su_opt;

static void su_option_cb(sa1g_world *w, void *user)
{
    const su_opt *o = (const su_opt *)user;
    uint32_t pwp = su_pwp0(w);
    if (!(w->super & SA1G_SUPER_GENERAL) || pwp == 0) {
        return;
    }
    if (o->option == SA1_SUPOPT_SUPER_ANIMS) {
        /* round 0.9.4a (super2_plan.md 6.1 task 3): while the look is on, plactptr = GA / GT at once (measured: no frame
           fix-up needed, super2_anim.md 5.5; GT and GA name the same objects, so no link fix either) */
        uint32_t to = su_look_table(w);
        if (w->sg.look && su_rd32(w, pwp + SU_PWP_PLACTPTR) != to) {
            su_wr32(w, pwp + SU_PWP_PLACTPTR, to);
            w->sg.ga_switches++;
        }
        return;
    }
    if (!su_super(w, pwp)) {
        return;
    }
    if (o->option == SA1_SUPOPT_SPEED) {
        su_speed_now(w);
    } else if (o->option == SA1_SUPOPT_NO_AURA) {
        if (w->sg.no_aura) {
            su_free_auras(w);
        } else if (su_tasks(w, SU_FN_AURA_INIT, SU_FN_AURA, NULL, 0) == 0) {
            su_make_aura(w, su_rd8(w, su_rd32(w, SA1G_ADDR_PLAYER_TWP) + 0x08u));
        }
    }
}

/* one option's value into the world (the bridge validated it); applied at once while he is Super (General worlds) */
sa1_status sa1w_world_supopt(struct sa1g_world *w, uint32_t option, int32_t value)
{
    su_opt o;
    if (w == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    switch (option) {
    case SA1_SUPOPT_SPEED:
        w->sg.speed = value;
        break;
    case SA1_SUPOPT_NO_AURA:
        w->sg.no_aura = value ? 1 : 0;
        break;
    case SA1_SUPOPT_VOICE:
        w->sg.voice = value;
        return SA1_OK;
    case SA1_SUPOPT_SUPER_EFFECTS:
        /* round 0.9.4a: the bits (SA1_SUPER_EFFECTS_CLONES | _DUST); the clones are tested at their first execs
           (su_clone_ok), the dust's SA1_SUPERDUST block is written at once (sa1_world_charopt.c) */
        w->sg.effects = value & (int32_t)(SA1_SUPER_EFFECTS_CLONES | SA1_SUPER_EFFECTS_DUST);
        return sa1g_charopt_superdust(w);
    case SA1_SUPOPT_SUPER_ANIMS:
        w->sg.anims = value ? 1 : 0;
        break;
    case SA1_SUPOPT_LSD_GOLD:
        w->sg.lsd_gold = value ? 1 : 0;     /* read by the capture after each pair (sa1w_world_lsd_gold) */
        return SA1_OK;
    default:
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    if (w->ram == NULL || !w->booted || !(w->super & SA1G_SUPER_GENERAL)) {
        return SA1_OK;
    }
    o.option = option;
    return sa1g_world_call(w, su_option_cb, &o);
}

/* ---- round 0.9.4a: the contract's accessors (sa1_api_world.h; super2_plan.md 5.4, faces_wheel_plan.md 5.4) ---------- */
int sa1w_world_lsd_gold(const struct sa1g_world *w)
{
    if (w == NULL || !(w->super & SA1G_SUPER_GENERAL) || !w->sg.drawn_pair) {
        return 0;
    }
    return (w->sg.drawn_look && w->sg.lsd_gold) ? 1 : 0;
}

uint32_t sa1w_world_super_lifted(const struct sa1g_world *w, uint32_t action)
{
    if (w == NULL || !(w->super & SA1G_SUPER_GENERAL) || w->sg.ga == 0 || action < 130u || action > 134u) {
        return 0;
    }
    return w->sg.ga_act[action == 134u ? 3u : action - 130u];
}
