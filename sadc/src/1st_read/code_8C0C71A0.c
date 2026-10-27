/*
 * 1ST_READ 8C0C71A0-8C0C7600: Knuckles mode helpers and the flight-wobble
 * task.
 *
 *  func_8C0C71A0  KnucklesModeMachine check: button D_8C19E38C pressed:
 *                 mode 4, action 15, twp->flag |= 0x500, work.f = spd.x
 *  func_8C0C71DC  KnucklesModeMachine check: button D_8C19E38C pressed:
 *                 actions 54-56 stop chaining (next = 0), mode 26,
 *                 action 54, free.w[5] = 0
 *  func_8C0C7222  KnucklesModeMachine check: below run_speed: moving
 *                 forward gives mode 2 (action 9), else Knuckles stands
 *                 (8C0C4F40). Returns 0 at run speed or faster.
 *  func_8C0C725E  dest of the task below: restores the two morph nodes'
 *                 models (D_8C353098[1] / [2])
 *  func_8C0C726E  task exec (started by KnucklesMain): while the player
 *                 plays an action of the Knuckles or Knuckles_Flight
 *                 model, bends the vertices of that model into the shared
 *                 morph model D_8C353098[0] with three Z rotations whose
 *                 angle swings with the player's speed (njCos of an
 *                 accumulated angle), and points both nodes at it
 *  func_8C0C75AC  empty dest (set by 8C0C7694)
 */
#include "common.h"
#include "sa1/sa1.h"

/* Knuckles: back to standing (code_8C0C4C2E.c; Sonic func_8C0A9660) */
extern void func_8C0C4F40(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* Knuckles data (extern only) */
extern NJS_MODEL *D_8C353098[3];    /* morph models {dst, Knuckles' model, Knuckles_Flight's model} */
extern NJS_OBJECT D_8C35A8D0[];     /* node of Knuckles whose model is switched (array: a plain
                                       struct extern makes SHC fold sym+4 into the pool) */
extern NJS_OBJECT D_8C3620A8[];     /* node of Knuckles_Flight whose model is switched */
extern NJS_OBJECT Knuckles;         /* 8C35B094 */
extern NJS_OBJECT Knuckles_Flight;  /* 8C36286C */
extern NJS_MATRIX B_8C7A2620;       /* rotation of the upper vertices (y >= 0.2) */
extern NJS_MATRIX B_8C7A2660;       /* rotation / 2 (x < -2) */
extern NJS_MATRIX B_8C7A26A0;       /* rotation / 4 (|z| > 2) */

Sint32 func_8C0C71A0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        twp->mode = 4;
        pwp->mj.reqaction = 15;
        twp->flag |= 0x500;
        pwp->work.f = pwp->spd.x;
        return 1;
    }
    return 0;
}

/*
 * The original stores a halfword at +6 of KnucklesActionList[54..56]
 * (MOV.W: PL_ACTION.next and the pad byte after it; sa1/motion.h has next
 * as Uint8 because PSetMotion reads it with MOV.B). On PC (little endian,
 * -fno-strict-aliasing) the store sets next = 0 and pad07 = 0, as on the
 * DC. Same idiom as func_8C0DCB22 (code_8C0DCA6C.c).
 */
Sint32 func_8C0C71DC(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        *(Sint16 *)&KnucklesActionList[54].next = 0;
        *(Sint16 *)&KnucklesActionList[55].next = 0;
        *(Sint16 *)&KnucklesActionList[56].next = 0;
        twp->mode = 26;
        pwp->mj.reqaction = 54;
        pwp->free.w[5] = 0;
        return 1;
    }
    return 0;
}

Sint32 func_8C0C7222(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp->spd.x < pwp->p.run_speed) {
        if (pwp->spd.x <= 0.0f) {
            func_8C0C4F40(twp, mwp, pwp);
        } else {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
        }
        return 1;
    }
    return 0;
}

void func_8C0C725E(task *tp)
{
    D_8C35A8D0[0].model = D_8C353098[1];
    D_8C3620A8[0].model = D_8C353098[2];
}

/*
 * NON_MATCHING (the C is complete and functionally faithful; 29 of 332
 * instructions differ, all of them register allocation). Sister functions:
 * Sonic func_8C0B2376 (code_8C0B2338.c, matched), Tails func_8C0BF402
 * (code_8C0BF262.c) and Amy func_8C0DD16E (code_8C0DD166.c).
 * What reproduces the original:
 *  - src/n assigned in each branch of the action test (not through a
 *    "model" local at the join): SHC cross-jumps the two copies into the
 *    block before the switch, which gives the original prelude registers
 *    (pwp/model R5, D_8C353098 R4, KnucklesActionList R6) and leaves the
 *    first switch BT without a delay slot (with a join, SHC moves the src
 *    load into a BT/S slot);
 *  - "if ((a = ... + 0x800) > 0x1400)": the assignment inside the test
 *    gives the original schedule of the two MOV/SHLL8 constants (a
 *    separate "a = ...; if (a > 0x1400)" hoists MOV #20 above the FMUL;
 *    the same leftover is in Tails' func_8C0BF402);
 *  - "Uint8 pno" shared by the playermwp index and the GetPlayerWorkPtr
 *    argument (one MOV.B in R4, EXTU.B for the index);
 *  - "f = njCos(...)" in a variable and "0.97999997f * mwp->spd.y"
 *    (constant first: MOVA + FMOV.S);
 *  - the rotations as "c ? njRotateZ(m, c) : (void)0" (CSE temps in sp+0,
 *    stored in the BT/S delay slot), the loop matrices as
 *    (NJS_MATRIX_PTR)&B_8C7A26x0 (not CSEd with the njUnitMatrix
 *    arguments);
 *  - the sqrt argument: the SH-4 computes FMUL y*y, FMAC x*x, FMAC z*z,
 *    which SHC emits for "x*x + y*y + z*z" (it reassociates sums of
 *    products); the PC spelling is the fused form the SH-4 computes.
 * Remaining: tp and n swap R10/R9 (with the branch assignments n always
 * outranks tp; with a join it does not, but then the BT/S slot is filled),
 * which moves the loop matrices (orig B_8C7A2620 sp+0, B_8C7A2660 R8,
 * B_8C7A26A0 R10), and the loop constants 1.0f/3.0f come out in
 * FR14/FR15 (orig FR15/FR14). SHC breaks register-priority ties
 * chaotically: equivalent spellings that emit the same instructions
 * (tp->twp vs twp in the playertwp test, D_8C353098 through a local,
 * statement order of twp/mwp, matrix argument spellings, a dead "mwp =
 * tp->mwp") reshuffle the R4-R6, FR12-FR15 and R8-R10 choices. The
 * tp->twp test below is the one that gives the original FR12-FR15
 * assignment of the spd components, 2.0f (FR13) and 0.2f (FR12). Several
 * hundred combinations of such spellings were searched: none fixes tp/n.
 */
#ifdef NON_MATCHING
void func_8C0C726E(task *tp)
{
    Sint32 a;
    NJS_POINT3 v;
    taskwk *twp;
    motionwk *mwp;
    motionwk *pmwp;
    playerwk *pwp;
    NJS_POINT3 *src;
    NJS_POINT3 *dst;
    Uint32 n;
    Uint32 i;
    Float spd;
    Float f;
    Uint8 pno;

    twp = tp->twp;
    mwp = tp->mwp;
    if (playertwp[(Uint8)tp->twp->counter.b[0]] == NULL) {
        DestroyTask(tp);
        return;
    }
    pno = twp->counter.b[0];
    pmwp = playermwp[pno];
    pwp = GetPlayerWorkPtr(pno);
    dst = D_8C353098[0]->points;
    if (KnucklesActionList[pwp->mj.reqaction].actptr->object == &Knuckles) {
        src = D_8C353098[1]->points;
        n = D_8C353098[1]->nbPoint;
    } else if (KnucklesActionList[pwp->mj.reqaction].actptr->object == &Knuckles_Flight) {
        src = D_8C353098[2]->points;
        n = D_8C353098[2]->nbPoint;
    } else {
        return;
    }
    switch (twp->mode) {
    case 0:
        twp->mode = 1;
        tp->dest = func_8C0C725E;
        break;
    case 1:
        spd = njSqrt(SH_ONLY(pmwp->spd.x * pmwp->spd.x + pmwp->spd.y * pmwp->spd.y + pmwp->spd.z * pmwp->spd.z)
                     PC_ONLY(SA1_FMAC(pmwp->spd.z, pmwp->spd.z, SA1_FMAC(pmwp->spd.x, pmwp->spd.x, pmwp->spd.y * pmwp->spd.y)))) * 2.0f;
        if (spd > _builtin_fabsf(mwp->spd.y)) {
            if (spd < 0.0f) {
                spd = -spd;
            }
            if (spd > 5.0f) {
                spd = 5.0f;
            }
            mwp->spd.y = spd;
        }
        if (mwp->spd.y >= 0.19999999f || mwp->spd.y <= -0.19999999f) {
            mwp->spd.y = 0.97999997f * mwp->spd.y;
        } else {
            mwp->spd.y = 0.0f;
        }
        if ((a = SA1_FTOI(_builtin_fabsf(mwp->spd.y * 512.0f)) + 0x800) > 0x1400) {
            a = 0x1400;
        }
        mwp->ang_spd.z += a;
        f = njCos(mwp->ang_spd.z);
        twp->ang.z = SA1_FTOI(f * mwp->spd.y * 819.19997f);
        njUnitMatrix(B_8C7A2620);
        twp->ang.z ? njRotateZ(B_8C7A2620, twp->ang.z) : (void)0;
        njUnitMatrix(B_8C7A2660);
        twp->ang.z >> 1 ? njRotateZ(B_8C7A2660, twp->ang.z >> 1) : (void)0;
        njUnitMatrix(B_8C7A26A0);
        twp->ang.z >> 2 ? njRotateZ(B_8C7A26A0, twp->ang.z >> 2) : (void)0;
        for (i = 0; i < n; i++, src++, dst++) {
            if (!(src->y < 0.19999999f)) {
                v.x = src->x + 1.0f;
                v.y = src->y + 3.0f;
                v.z = src->z;
                njCalcVector((NJS_MATRIX_PTR)&B_8C7A2620, &v, dst);
                dst->x -= 1.0f;
                dst->y -= 3.0f;
            } else if (src->x < -2.0f) {
                v.x = src->x + 1.0f;
                v.y = src->y + 3.0f;
                v.z = src->z;
                njCalcVector((NJS_MATRIX_PTR)&B_8C7A2660, &v, dst);
                dst->x -= 1.0f;
                dst->y -= 3.0f;
            } else if (_builtin_fabsf(src->z) > 2.0f) {
                v.x = src->x + 1.0f;
                v.y = src->y + 3.0f;
                v.z = src->z;
                njCalcVector((NJS_MATRIX_PTR)&B_8C7A26A0, &v, dst);
                dst->x -= 1.0f;
                dst->y -= 3.0f;
            }
        }
        break;
    default:
        DestroyTask(tp);
        break;
    }
    D_8C35A8D0[0].model = D_8C353098[0];
    D_8C3620A8[0].model = D_8C353098[0];
}
#else
ASM_STUB(void func_8C0C726E(task *tp))
#endif

void func_8C0C75AC(task *tp)
{
}
