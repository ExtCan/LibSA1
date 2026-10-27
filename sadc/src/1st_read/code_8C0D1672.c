#include "sa1/pending.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0CBE00-8C0D2080):
 *
 *  func_8C0D1672  carry the held object (pwp->htp, called by E102Main):
 *                 objects of class 0x200 (8C06C880) are moved to E-102's
 *                 collision centre and then offset by the hand point
 *                 (2.5 * righthand_vec ahead of the right hand while
 *                 reqaction is 35-38, else midway between the hands),
 *                 rotated by twp->ang; other objects are dropped
 *                 (8C0D15C0) when E-102 touches something else, with mode
 *                 1 (8C0CD09C, SE 0x30E) on the ground or mode 7 /
 *                 action 8 in the air.
 *  func_8C0D18B8  destructor of the E-102 look task (tp->dest set by
 *                 8C0D1916): restores the saved head/eye angles; same code
 *                 as Sonic's func_8C0B2890.
 */

/* positional-SE enable flag (MOV.B + TST) */
extern Sint8 B_8C7A2EA8;

/* E-102: set mode 1 (code_8C0CD09C.c) */
void func_8C0CD09C(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* E-102: release the held object (BSR from here) */
ASM_STUB(void func_8C0D15C0(taskwk *twp, motionwk *mwp, playerwk *pwp))

/*
 * Matching notes: the drop path must be nested positive ifs ending in a
 * return, with the carry code at function level (early returns or an
 * if/else give the same instructions with other temporary registers).
 * The rotations are conditional expressions: SHC then keeps the angle in a
 * temporary that it parks in the stack slot shared with mwp (store in the
 * delay slot, reload before the call), as the original does; plain ifs
 * reload the angle from twp instead.
 */
void func_8C0D1672(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    NJS_POINT3 out;
    CCL_INFO *info;
    taskwk *htwp;

    htwp = pwp->htp->twp;
    info = CWP(twp)->info;
    if (htwp == NULL) {
        return;
    }
    if (!(func_8C06C880(twp) & 0x200)) {
        if (CWP(twp)->hit_cwp != NULL) {
            if (htwp != CWP(twp)->hit_cwp->mytask->twp) {
                func_8C0D15C0(twp, mwp, pwp);
                if (pwp->htp == NULL) {
                    if (twp->flag & 3) {
                        func_8C0CD09C(twp, mwp, pwp);
                        if (B_8C7A2EA8) {
                            func_8C09413A(0x30E, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
                        }
                    } else {
                        twp->mode = 7;
                        pwp->mj.reqaction = 8;
                    }
                }
            }
        }
        return;
    }
    htwp->pos = info->center;
    if (pwp->mj.reqaction == 35 || pwp->mj.reqaction == 36
        || pwp->mj.reqaction == 37 || pwp->mj.reqaction == 38) {
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

void func_8C0D18B8(task *tp)
{
    lookwk *lwp;

    lwp = (lookwk *)tp->mwp;
    lwp->head->ang[0] = lwp->head_ang[0];
    lwp->head->ang[1] = lwp->head_ang[1];
    lwp->head->ang[2] = lwp->head_ang[2];
    lwp->eyeR->ang[0] = lwp->eyeR_ang[0];
    lwp->eyeR->ang[1] = lwp->eyeR_ang[1];
    lwp->eyeR->ang[2] = lwp->eyeR_ang[2];
    lwp->eyeL->ang[0] = lwp->eyeL_ang[0];
    lwp->eyeL->ang[1] = lwp->eyeL_ang[1];
    lwp->eyeL->ang[2] = lwp->eyeL_ang[2];
}