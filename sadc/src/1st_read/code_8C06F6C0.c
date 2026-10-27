#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06F6C0): air acceleration, variant with a
 * weaker stick response (mag * 0.1f, * 1.5f with equipment bit 3).
 *  func_8C06F6C0  gravity scaled by the weight plus air resistance on the
 *                 player-space speed; stick input turns the aim angle.
 *                 The result goes to pwp->acc. Does nothing under input
 *                 lock (twp->flag & 0x4000). Called by 8C0BD8B2.
 */

/* rotation towards the aim angle in the air (8C071EC0, R4-R7) */
void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang);

void func_8C06F6C0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    Angle ang;
    Float mag;
    Sint32 diff;

    v = mwp->acc;
    if (twp->flag & 0x4000) {
        return;
    }
    /* the six accumulations below are FMACs (fused on PC); SA1_FMAC_ACC keeps
     * the "+=" statement for SHC (see code_8C06E904.c) */
    SA1_FMAC_ACC(v.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(v.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(v.z, Gravity.z, pwp->p.weight);
    PConvertVector_G2P(twp, &v);
    SA1_FMAC_ACC(v.x, pwp->p.air_resist_air, pwp->spd.x);
    SA1_FMAC_ACC(v.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(v.z, pwp->p.air_resist_z, pwp->spd.z);
    if (GetAnalog(twp, &ang, &mag) == 0) {
        mag = 0.0f;
        mwp->ang_aim.y = twp->ang.y;
    } else {
        diff = DiffAngle(twp->ang.y, ang);
        if (pwp->spd.x > pwp->p.run_speed && diff > 0x6000) {
            mag = pwp->p.air_break * mag;
        } else if (diff > 0x1000) {
            mwp->ang_aim.y = ang;
            mag = 0.0f;
        } else {
            mwp->ang_aim.y = ang;
            mag = mag * 0.099999996f;
            if ((Sint16)pwp->equipment & 8) {
                mag = mag * 1.5f;
            }
        }
        func_8C071EC0(twp, mwp, pwp, mwp->ang_aim.y);
    }
    v.x += mag;
    pwp->acc = v;
}
