#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06E904): air acceleration.
 *  func_8C06E904  acceleration of a player in the air (gravity scaled by the
 *                 weight, air resistance on the player-space speed, stick
 *                 input scaled by air_accel / air_break); the result goes to
 *                 pwp->acc. Under input lock (twp->flag & 0x4000) the
 *                 locked variant func_8C06EFCE is used instead.
 */

/* rotation towards the aim angle in the air (8C071EC0, R4-R7) */
void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang);

void func_8C06EFCE(taskwk *twp, motionwk *mwp, playerwk *pwp);

void func_8C06E904(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    Angle ang;
    Float mag;
    Sint32 diff;

    if (twp->flag & 0x4000) {
        func_8C06EFCE(twp, mwp, pwp);
        return;
    }
    v = mwp->acc;
    /* the six accumulations below are FMACs (fused on PC); SA1_FMAC(a, b, v.x)
     * loads the addend first and changes the SHC output, SA1_FMAC_ACC keeps
     * the "+=" statement for SHC */
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
            if (mwp->spd.y < 0.0f) {
                mag = pwp->p.air_accel * mag * 2.0f;
            } else {
                mag = pwp->p.air_accel * mag;
            }
        }
        func_8C071EC0(twp, mwp, pwp, mwp->ang_aim.y);
    }
    v.x += mag;
    pwp->acc = v;
}

ASM_STUB(void func_8C06EFCE(taskwk *twp, motionwk *mwp, playerwk *pwp))
