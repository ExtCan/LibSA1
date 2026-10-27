#include "sa1/sa1.h"

/*
 * Amy (1ST_READ 8C0DC7DE-8C0DCA6C, chunk 8C0D6B40, functions 31-32), both
 * called with (twp, mwp, pwp):
 *  func_8C0DC7DE  carry the held object (pwp->htp, called by AmyMain; E-102:
 *                 8C0D1672): objects of class 0x200 (8C06C880) are moved
 *                 to Amy's collision centre and then offset by the hand
 *                 point (2.5 * righthand_vec ahead of the right hand while
 *                 reqaction is 67-70, else midway between the hands),
 *                 rotated by twp->ang; other objects are dropped (8C0DC75E)
 *                 when Amy touches something else, then mode 1 (8C0DA6E0)
 *                 on the ground or mode 8 / action 18 in the air.
 *  func_8C0DC9BE  mode-machine check (AmyModeMachine 8C0D7960), returns 1
 *                 when it set a new mode: on the ground returns 0; in the
 *                 air with twp->flag 0x100 mode 5; else, while holding
 *                 (TWF_HOLD), a 0x200-class object gives mode 30 / action
 *                 65, any other held object is dropped (8C0DC75E); then
 *                 mode 8 / action 18.
 */

/* Amy: release the held object (8C0DC75E, BSR from here; earlier in the
 * chunk; Sonic 8C0B1D60, E-102 8C0D15C0) */
ASM_STUB(void func_8C0DC75E(taskwk *twp, motionwk *mwp, playerwk *pwp))

/* Amy: mode 1 + stand selector (code_8C0DA106.c) */
extern void func_8C0DA6E0(taskwk *twp, motionwk *mwp, playerwk *pwp);

#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): 221 of 221 instructions,
 * only 2 differ: the reload of twp->cwp for the hit test goes to R4 here,
 * R0 in the original (MOV.L @(56,R13),R0; MOV.L @(24,R0),R3). Control flow,
 * stack frame (mwp spilled to @0 and shared with the parked rotation angle,
 * info @4, out @8, v @20), literal use, the 3 FMACs and the rotations match.
 * Same source as E-102's matched func_8C0D1672 (code_8C0D1672.c) without
 * its htwp == NULL test; Sonic 8C0B1DE0, Tails 8C0BD4CC, Knuckles 8C0C6E80
 * and Big 8C0E5698 have the same R0, R3, R1, R0 chain in the original.
 * The colliwk local is what makes the rest of the chain match: with
 * CWP(twp)->hit_cwp->mytask->twp written inline SHC rotates its temporaries
 * R3, R1, R0, R3 (5 differ); taking cwp out of the temporary rotation gives
 * the original's R3, R1, R0 for hit_cwp, mytask and twp, but every local or
 * CSE form tried lands in R4, never R0 (block-scope local with initializer,
 * function-scope local in any declaration order, assignment inside the
 * condition, void * local, register, early-return form, locals for hit_cwp,
 * the hit task or its twp, a #pragma inline helper, CSE through a repeated
 * && test). Earlier attempts without the local: early returns, gotos to the
 * end (all 16 goto/return mixes), if/else with either block first, a single
 * && (CSEs twp->cwp), swapped compare operands, == with an empty then-block,
 * class-bits local, 0x200U masks, declaration and statement orders, a switch
 * on reqaction, Sonic's Uint32 angle local (21 differ); the carry path does
 * not influence these registers.
 */
void func_8C0DC7DE(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    NJS_POINT3 out;
    CCL_INFO *info;
    taskwk *htwp;
    colliwk *cwp;

    htwp = pwp->htp->twp;
    info = CWP(twp)->info;
    if (!(func_8C06C880(twp) & 0x200)) {
        if (CWP(twp)->hit_cwp != NULL) {
            cwp = CWP(twp);
            if (htwp != cwp->hit_cwp->mytask->twp) {
                func_8C0DC75E(twp, mwp, pwp);
                if (pwp->htp == NULL) {
                    if (twp->flag & TWF_GROUND) {
                        func_8C0DA6E0(twp, mwp, pwp);
                    } else {
                        twp->mode = 8;
                        pwp->mj.reqaction = 18;
                    }
                }
            }
        }
        return;
    }
    htwp->pos = info->center;
    if (pwp->mj.reqaction == 67 || pwp->mj.reqaction == 68
        || pwp->mj.reqaction == 69 || pwp->mj.reqaction == 70) {
        v.x = SA1_FMAC(2.5f, pwp->righthand_vec.x, pwp->righthand_pos.x);
        v.y = SA1_FMAC(2.5f, pwp->righthand_vec.y, pwp->righthand_pos.y);
        v.z = SA1_FMAC(2.5f, pwp->righthand_vec.z, pwp->righthand_pos.z);
    } else {
        v.x = (pwp->righthand_pos.x + pwp->lefthand_pos.x) * 0.5f;
        v.y = (pwp->righthand_pos.y + pwp->lefthand_pos.y) * 0.5f;
        v.z = (pwp->righthand_pos.z + pwp->lefthand_pos.z) * 0.5f;
    }
    v.x -= CWP(htwp)->info->center.x;
    v.y -= CWP(htwp)->info->center.y;
    v.z -= CWP(htwp)->info->center.z;
    njPushMatrix(D_8C67C640);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    njCalcVector(NULL, &v, &out);
    njPopMatrix(1);
    njAddVector(&htwp->pos, &out);
}
#else
ASM_STUB(void func_8C0DC7DE(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

/*
 * Matching notes: the return 0 must be the last statement (the original
 * branches to it past the literal pool), the mode-5 branch needs its own
 * return 1 (gives the "BRA; NOP" of the original) and the hold test must be
 * an else-if (a separate if puts the 0x800 mask in R3 instead of R2).
 */
Sint32 func_8C0DC9BE(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!(twp->flag & TWF_GROUND)) {
        if (twp->flag & 0x100) {
            twp->mode = 5;
            return 1;
        } else if (twp->flag & TWF_HOLD) {
            if (func_8C06C880(twp) & 0x200) {
                twp->mode = 30;
                pwp->mj.reqaction = 65;
                return 1;
            }
            func_8C0DC75E(twp, mwp, pwp);
        }
        twp->mode = 8;
        pwp->mj.reqaction = 18;
        return 1;
    }
    return 0;
}
