#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0AE588-8C0AE73C: Sonic running-action selectors (chunk
 * 8C0A9660-8C0AE760, functions 7/8 and 8/8). Both are called by SonicMain
 * (8C0A77BC) and SonicModeMachine (8C0A966C) with (twp, mwp, pwp).
 *
 * The effective speed is pwp->spd.x + mwp->height * 5 (motionwk +0x38); it is
 * compared with the physics thresholds (jog/run/rush/dash speed) to pick the
 * run action in pwp->mj.reqaction. Afterwards pwp->work.f is set to
 * pwp->spd.x + mwp->height * 20.
 */

/*
 * func_8C0AE3D6 (the stand-action selector, still assembly here) precedes
 * these functions in the original translation unit. It is not called from
 * this file, but its pending literal-pool entries make SHC dump the pool in
 * the middle of func_8C0AE588 (8C0AE640), which turns the three early returns
 * into BF/BRA far branches and leaves 20.0f for the shared pool after
 * func_8C0AE6D8 (8C0AE740), as in the original. Without this stub
 * func_8C0AE588 compiles to short BT returns and does not match.
 */
ASM_STUB(void func_8C0AE3D6(taskwk *twp, motionwk *mwp, playerwk *pwp))

/*
 * Run action selector. Leaves actions 29, 30 and 36 alone. With
 * twp->flag & 0x800 (SADX Status_HoldObject) picks 34 or 38; when moving
 * faster than jog_speed with no stick input on a surface with attr & 4 picks
 * 71; otherwise 13/12/11/10/9 by dash/rush/run/jog speed.
 */
void func_8C0AE588(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float spd;

    spd = SA1_FMAC(mwp->height, 5.0f, pwp->spd.x);
    if (pwp->mj.reqaction == 29 || pwp->mj.reqaction == 30 || pwp->mj.reqaction == 36) {
        return;
    }
    if (twp->flag & 0x800) {
        if (spd > pwp->p.run_speed) {
            pwp->mj.reqaction = 34;
        } else {
            pwp->mj.reqaction = 38;
        }
    } else if (pwp->p.jog_speed < pwp->spd.x && !GetAnalog(twp, NULL, NULL)
               && (pwp->attr & 4)) {
        pwp->mj.reqaction = 71;
    } else if (spd > pwp->p.dash_speed) {
        pwp->mj.reqaction = 13;
    } else if (spd > pwp->p.rush_speed) {
        pwp->mj.reqaction = 12;
    } else if (spd > pwp->p.run_speed) {
        pwp->mj.reqaction = 11;
    } else if (spd > pwp->p.jog_speed) {
        pwp->mj.reqaction = 10;
    } else {
        pwp->mj.reqaction = 9;
    }
    pwp->work.f = SA1_FMAC(mwp->height, 20.0f, pwp->spd.x);
}

/* Same speed selection for actions 134/133/132/131 (rush/run/jog/else). */
void func_8C0AE6D8(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float spd;

    spd = SA1_FMAC(mwp->height, 5.0f, pwp->spd.x);
    if (spd > pwp->p.rush_speed) {
        pwp->mj.reqaction = 134;
    } else if (spd > pwp->p.run_speed) {
        pwp->mj.reqaction = 133;
    } else if (spd > pwp->p.jog_speed) {
        pwp->mj.reqaction = 132;
    } else {
        pwp->mj.reqaction = 131;
    }
    pwp->work.f = SA1_FMAC(mwp->height, 20.0f, pwp->spd.x);
}
