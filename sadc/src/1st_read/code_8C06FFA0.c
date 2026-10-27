#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06FFA0-8C0700C2).
 *  func_8C06FFA0  acceleration helper: motion acc minus weight (x and y),
 *                 turned into player space, air resistance per axis
 *                 (x only above max_x_spd, doubled), forward acceleration
 *                 run_accel * 0.3 below max_x_spd (stored in mwp->rad),
 *                 turn towards angle 0; result in pwp->acc. Skipped while
 *                 twp->flag & 0x4000. The weight is subtracted from both
 *                 acc.y and acc.x (as in the original; 8C06E4E0 uses
 *                 Gravity * weight instead).
 */

void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleYQ-like (turn on the spot) */

void func_8C06FFA0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR spd;
    NJS_VECTOR vec;
    Float f;

    spd = mwp->acc;
    vec.z = 0.0f;
    vec.y = 0.0f;
    vec.x = 0.0f;
    if (twp->flag & 0x4000) {
        return;
    }
    spd.y += -pwp->p.weight;
    spd.x += -pwp->p.weight;
    PConvertVector_G2P(twp, &spd);
    if (pwp->spd.x > pwp->p.max_x_spd) {
        spd.x = SA1_FMAC(pwp->p.air_resist * (pwp->spd.x - pwp->p.max_x_spd), 2.0f, spd.x);
    }
    /* FMAC; written c + a * b (SA1_FMAC's a * b + c order does not match) */
    SA1_FMAC_ACC(spd.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(spd.z, pwp->p.air_resist_z, pwp->spd.z);
    if (pwp->spd.x < pwp->p.max_x_spd) {
        f = pwp->p.run_accel * 0.29999999f;
    } else {
        f = 0.0f;
    }
    mwp->rad = f;
    vec.x = f;
    mwp->ang_aim.y = 0;
    func_8C072000(twp, mwp, pwp, mwp->ang_aim.y);
    spd.x += vec.x;
    pwp->acc = spd;
}
