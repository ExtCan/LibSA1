/*
 * Ground item box O_ITEMBOX (1ST_READ 8C05D0A0-8C05D760, chunk
 * asm/1st_read/text/8C05D0A0.src; staged by dc-itembox-ground for G-SA1
 * 0.9.4, rounds plan 14.1q). All eight functions MATCH (tools/shcdiff.py).
 *
 *  func_8C05D0A0      spawn a ground box with item kind `item` at pos (no SET
 *                     entry; only caller 8C066AA6, the item drop called by
 *                     8C05AD00 of the CMN_KUSA chunk, with item 4 or 6)
 *  objexec_O_ITEMBOX  exec (SET object type 13): item kind in twp->scl.x
 *                     (clamped: < 0 -> 8, >= 9 -> 0), copied to B_8C78C378;
 *                     modes 0 init, 1 grow in, 2 active, 3 break, 4 / 5
 *                     broken shell (collision only), 6 re-init
 *  func_8C05D360      mode 1: scl.z += 0.11 up to 5.0, then mode 2 (drawn
 *                     at scale scl.z * 0.2 until then; no collision entry
 *                     in mode 1: 46 frames after every spawn)
 *  func_8C05D386      mode 2: the break test and the item give; the item
 *                     panel spins (scl.y += 3 degrees per frame)
 *  func_8C05D4CE      mode 3: condition bit 0x0004, break debris, lock-on release,
 *                     collision replaced by the shell (CCL id 4), mode 4
 *  func_8C05D56E      disp of the whole box (modes 0-2)
 *  func_8C05D67E      disp of the broken box (modes 3 / 4): base only
 *  func_8C05D6AC      break debris: one particle 5 units above the box
 * The dest func_8C05D760 (empty) is its own chunk: code_8C05D760.c.
 *
 * Break rule (func_8C05D386; needs D_8C19B468 == 0, i.e. not in an event):
 * the box's collision hit a player collision this frame (cwp->flag & 2,
 * hit_cwp->id 0 or 1) and either the hit shape of the box is its lid (shape
 * 1 of D_8C1B9AC0: sphere r 5 at y + 10) or the box is hurt (twp->flag & 4:
 * an attack that out-levels the box's shapes, e.g. the spin / jump ball;
 * SADX community name Status_Hurt, UNVERIFIED here). A player who hits the
 * lid gets SetVelocityP(pno, 0, 2, 0) when player 0's taskwk has no ground
 * bit (flag & 1) - the game tests playertwp[0] for player 1 too. The item
 * handler D_8C1B9BA0[item].func(twp) runs, and func_8C05E0F8 queues the HUD
 * icon, when the hit came from player 0's task, or the story character
 * (GetPlayerNumber) is Sonic (0), E-102 (6) or Big (7), or the pair is
 * (player 0 Sonic, player 1 Tails) or (player 0 Tails, player 1 not Sonic);
 * otherwise the box breaks without giving the item.
 *
 * SET condition (OBJ_CONDITION.ssCondition of tp->ocp): mode 0 sets 0x10
 * (func_8C091B5E); a box whose bit 0x0004 is set starts broken (mode 3, no
 * debris); breaking sets 0x0004 (func_8C091B54). Bit 15 is never cleared
 * (no func_8C091BA4): the shell stays and respawns broken.
 *
 * PORTABILITY: B_8C78C378 = (Uint32)scl.x is defined on PC because the clamp
 * leaves -1 < scl.x < 9 (NaN: SA1_FTOI gives a negative value -> 8.0);
 * func_8C05D56E converts again without the clamp (the exec clamps first in
 * every frame). The display writes game data (D_8C1B9BA0[6].tex, the panel
 * model's material texture): per world on PC, as the air box
 * (code_8C05DC60.c). tp->mwp is a 0x23C-byte block for E-102's lock-on (see
 * code_8C0D5600.c E102_TARGETBUF), not a motionwk. STG03 0x0C907998 is called
 * only in stage 0x0302.
 *
 * Matching notes: B_8C78C378 must be Uint32 (the "> 2^31 ? x - 2^32 : x"
 * conversion template; a Sint32 target gives the FADD / FSUB template);
 * func_8C05D386 needs the nested "if ((id = hit->id) == 0 || id == 1)"
 * (id in R5; a separate statement puts it in R4, && in one condition spills
 * it to the stack); func_8C05D4CE needs the empty then-branch of the
 * condition test (R2 / R3 choice); func_8C05D56E needs the "?:" rotation
 * with the angle expression written twice (SHC keeps it in a stack slot,
 * as code_8C0DA106.c); 8C05D360 "scl.z >= 5.0f" (the "!(5.0f > scl.z)" form
 * does not match).
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"
#include "sa1/protos.h"     /* func_8C07C676, func_8C07CE40, func_8C099500 */

/* Item kind table (same as code_8C05DF18.c / code_8C05DC60.c): icon texture
 * number and pickup handler (code pointer: remapped by the PC loader). */
typedef struct {
    Sint32 tex;                 /* 0x00 */
    void (*func)(taskwk *twp);  /* 0x04 called with the box's taskwk */
} ITEMBOX_ITEM;                 /* 0x08 */

SA1_ASSERT_SIZE(ITEMBOX_ITEM, 0x08);

extern Uint32 B_8C78C378;               /* item kind of the box being run / drawn */
extern CCL_INFO D_8C1B9AC0[];           /* 2 shapes, CCL id 2: cylinder r 5 h 5 at y + 5 (attr 0x100081), lid sphere r 5 at y + 10 */
extern CCL_INFO D_8C1B9B28[];           /* 2 shapes, CCL id 4 (broken shell): cylinders r 6 h 1.5 and r 2 h 1.8 (attr 1) */
extern ITEMBOX_ITEM D_8C1B9BA0[];       /* 9 item kinds (handlers 8C05D764 .. 8C05D964) */
extern Sint32 D_8C1B9BE8[];             /* 1-up icon texture per character (GetPlayerNumber) */
extern NJS_POINT3 D_8C154C34;           /* debris particle velocity (0, 1, 0) */
extern NJS_MODEL D_8C1AC944;            /* Item_Box root model (base) */
extern NJS_MODEL D_8C1AC1A8;            /* Item_Box child 1 (translucent case) */
extern NJS_MODEL D_8C1ABD30;            /* Item_Box child 2 (lid) */
extern NJS_MODEL D_8C1AB994;            /* Item_Box child 3: item panel quad (material texture = item icon) */

/* Function of the STG03 (Twinkle Park) overlay (overlays load at
 * 0x0C900000), called with the box task in stage 0x0302: it creates a child
 * task of the box (CreateChildTask(8, 0x0C907900, tp)). PC: provided by the
 * overlay loader (libsa1: void policy). */
#if defined(TARGET_PC)
extern void func_0C907998(task *tp);
#define STG03_FUNC_0C907998 func_0C907998
#else
#define STG03_FUNC_0C907998 ((TaskFuncPtr)0x0C907998)
#endif

void func_8C091B54(task *tp);           /* ocp->ssCondition |= 4 (code_8C091B54.c) */
void func_8C091B5E(task *tp);           /* ocp->ssCondition |= 0x10 (code_8C091B5E.c) */
Sint32 func_8C091B68(task *tp);         /* ocp->ssCondition & 4 (code_8C091B54.c) */
void func_8C05E0F8(Sint32 kind);        /* queue the HUD item icon (code_8C05E0F8.c) */
void func_8C0D5D90(task *tp);           /* release E-102's lock-on marker (code_8C0D5D90.c) */
void func_8C09E3E8(NJS_POINT3 *pos, NJS_POINT3 *vec, Float scl); /* one particle (code_8C09E340.c) */

void func_8C05D760(task *tp);           /* dest (empty, code_8C05D760.c) */
void objexec_O_ITEMBOX(task *tp);
void func_8C05D360(taskwk *twp);
void func_8C05D386(task *tp);
void func_8C05D4CE(task *tp);
void func_8C05D56E(task *tp);
void func_8C05D67E(task *tp);
void func_8C05D6AC(taskwk *twp);

/* Spawn a ground item box (no SET entry: tp->ocp stays NULL) that starts in
 * mode 1. Called by 8C066AA6 (CMN_KUSA's drop: item 4 random rings, 6 1-up). */
void func_8C05D0A0(NJS_POINT3 *pos, Sint32 item)
{
    task *tp;
    taskwk *twp;

    tp = CreateElementalTask(2, 3, objexec_O_ITEMBOX);
    twp = tp->twp;
    twp->pos = *pos;
    twp->scl.x = (Float)item;
    tp->dest = func_8C05D760;
    tp->disp = func_8C05D56E;
    CCL_Init(tp, D_8C1B9AC0, 2, 1, 2);
    twp->mode = 1;
    twp->scl.z = 0.0f;
    /* 0x23C-byte block in place of the motionwk: E-102's lock-on target work */
    tp->mwp = (motionwk *)CAlloc(1, 0x23C);
}

/*
 * Exec. Nothing runs while the SET manager reports the box out of range
 * (func_8C0919CC). The display is called from here every frame (tp->disp is
 * kept in step for the pause draw); mode 5 draws nothing. In mode 2 the box
 * registers a land position (func_8C07D6E0, r 3).
 */
void objexec_O_ITEMBOX(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (func_8C0919CC(tp)) {
        return;
    }
    if (twp->mode == 0 || twp->mode == 2 || twp->mode == 1) {
        tp->disp = func_8C05D56E;
    } else if (twp->mode == 3 || twp->mode == 4) {
        tp->disp = func_8C05D67E;
    }
    if (SA1_FTOI(twp->scl.x) < 0) {
        twp->scl.x = 8.0f;
    } else if (SA1_FTOI(twp->scl.x) >= 9) {
        twp->scl.x = 0.0f;
    }
    /* in range: the clamp above leaves -1 < scl.x < 9 (NaN gives 8.0) */
    B_8C78C378 = (Uint32)twp->scl.x;
    switch (twp->mode) {
    case 0:
        if (tp->ocp) {
            func_8C091B5E(tp);
        }
        tp->dest = func_8C05D760;
        tp->disp = func_8C05D56E;
        CCL_Init(tp, D_8C1B9AC0, 2, 1, 2);
        twp->mode = 1;
        twp->scl.z = 0.0f;
        tp->mwp = (motionwk *)CAlloc(1, 0x23C);
        if (GetStageNumber() == 0x302) {    /* Twinkle Park act 2 */
            STG03_FUNC_0C907998(tp);
        }
        if (func_8C091B68(tp)) {            /* broken before: start as the shell */
            twp->mode = 3;
        }
        break;
    case 1:
        func_8C05D360(twp);
        break;
    case 2:
        func_8C05D386(tp);
        break;
    case 3:
        func_8C05D4CE(tp);
        break;
    case 4:
    case 5:
        func_8C02F63C(twp);
        break;
    case 6:
    default:
        twp->mode = 0;
        break;
    }
    if (twp->mode == 0 || twp->mode == 2 || twp->mode == 1) {
        func_8C05D56E(tp);
    } else if (twp->mode == 3 || twp->mode == 4) {
        func_8C05D67E(tp);
    }
    if (func_8C0919CC(tp)) {
        /* the original tests the result again and ignores it */
    }
    if (twp->mode == 2) {
        func_8C07D6E0(twp->pos.x, twp->pos.y, twp->pos.z, 3.0f);
    }
    func_8C098E98(tp);
}

/* Mode 1: grow in (scl.z 0 -> 5 by 0.11; drawn at scale scl.z * 0.2), then
 * mode 2 with scl.z = 1. */
void func_8C05D360(taskwk *twp)
{
    twp->scl.z += 0.11f;
    if (twp->scl.z >= 5.0f) {
        twp->mode = 2;
        twp->scl.z = 1.0f;
    }
}
/*
 * Mode 2: the break test and the item give (rule in the file header). The
 * collision entry runs and the item panel turns by 3 degrees in every frame
 * of mode 2 that is not an event frame.
 */
void func_8C05D386(task *tp)
{
    taskwk *twp;
    colliwk *hit;
    Uint16 id;

    twp = tp->twp;
    hit = CWP(twp)->hit_cwp;
    if (D_8C19B468 != 0) {
        return;
    }
    if (CWP(twp)->flag & 2) {
        if ((id = hit->id) == 0 || id == 1) {
            if (CWP(twp)->my_num == 1) {
                /* the lid was hit: break, and bounce an airborne player */
                twp->mode = 3;
                if (CWP(twp)->hit_cwp->mytask == GetPlayerTaskPointer(0) && !(playertwp[0]->flag & 1)) {
                    func_8C07C676(0, 0.0f, 2.0f, 0.0f);
                } else if (CWP(twp)->hit_cwp->mytask == GetPlayerTaskPointer(1) && !(playertwp[0]->flag & 1)) {
                    func_8C07C676(1, 0.0f, 2.0f, 0.0f);
                }
            }
            if (twp->flag & 4) {
                twp->mode = 3;
            }
            if (twp->mode == 3) {
                if (D_8C1B9BA0[B_8C78C378].func != NULL) {
                    if (CWP(twp)->hit_cwp->mytask == GetPlayerTaskPointer(0)
                        || GetPlayerNumber() == 6 || GetPlayerNumber() == 7
                        || GetPlayerNumber() == 0
                        || (func_8C07CE40(1) == 2 && func_8C07CE40(0) == 0)
                        || (func_8C07CE40(0) == 2 && func_8C07CE40(1) != 0)) {
                        D_8C1B9BA0[B_8C78C378].func(twp);
                        func_8C05E0F8(B_8C78C378);
                    }
                }
            }
        }
    }
    func_8C02F63C(twp);
    twp->scl.y += 3.0f;
}

/*
 * Mode 3: a SET box sets condition bit 0x0004 and throws the debris only the
 * first time (a box loaded broken skips both); a spawned box always throws
 * it. Then E-102's lock-on marker is released, the collision freed and
 * replaced by the shell's (CCL id 4, D_8C1B9B28), mode 4.
 */
void func_8C05D4CE(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (tp->ocp) {
        if (func_8C091B68(tp)) {
        } else {
            func_8C091B54(tp);
            func_8C05D6AC(twp);
        }
    } else {
        func_8C05D6AC(twp);
    }
    func_8C0D5D90(tp);
    func_8C0344A0(twp);
    CCL_Init(tp, D_8C1B9B28, 4, 1, 2);
    twp->mode = 4;
}

/*
 * Disp of the whole box (texlist B_8C79F0F0, OBJ_REGULAR in ordinary stages,
 * through func_8C099500): when visible (radius 20) the base, the case and
 * the lid at the box (scaled by scl.z * 0.2 while growing in), and the item
 * panel 7.5 units up, turned by scl.y degrees, with the item's icon as its
 * texture. The 1-up icon is set to the current character's first.
 */
void func_8C05D56E(task *tp)
{
    taskwk *twp;
    Float s;

    twp = tp->twp;
    D_8C1B9BA0[6].tex = D_8C1B9BE8[GetPlayerNumber()];
    if (func_8C038D00(twp->pos.x, twp->pos.y, twp->pos.z, 20.0f)) {
        func_8C099500();
        njPushMatrix(NULL);
        njTranslateV(NULL, &twp->pos);
        if (twp->mode != 2) {
            s = twp->scl.z * 0.19999999f;
            njScale(NULL, s, s, s);
        }
        func_8C03700C(&D_8C1AC944);
        njPushMatrix(NULL);
        func_8C03700C(&D_8C1AC1A8);
        njPopMatrix(1);
        njPushMatrix(NULL);
        func_8C03700C(&D_8C1ABD30);
        njPopMatrix(1);
        njPushMatrix(NULL);
        njTranslate(NULL, 0.0f, 7.5f, 0.0f);
        SA1_FTOI(twp->scl.y * 182.044434f) ? njRotateY(NULL, SA1_FTOI(twp->scl.y * 182.044434f)) : (void)0;
        /* in range: the exec clamps scl.x before it draws (-1 < scl.x < 9) */
        B_8C78C378 = (Uint32)tp->twp->scl.x;
        D_8C1AB994.mats->attr_texId = D_8C1B9BA0[B_8C78C378].tex;
        func_8C03700C(&D_8C1AB994);
        njPopMatrix(2);
    }
}

/* Disp of the broken box: the base model only (no visibility test). */
void func_8C05D67E(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    func_8C099500();
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    func_8C03700C(&D_8C1AC944);
    njPopMatrix(1);
}

/* Break debris: one default particle 5 units above the box, velocity
 * D_8C154C34 (0, 1, 0), scale 1. */
void func_8C05D6AC(taskwk *twp)
{
    NJS_POINT3 pos;

    pos = twp->pos;
    pos.y += 5.0f;
    func_8C09E3E8(&pos, &D_8C154C34, 1.0f);
}
