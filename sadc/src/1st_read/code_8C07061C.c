#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C07061C-8C07095E): acceleration helpers used by
 * the character mains (SonicMain 8C0A77BC, TailsMain 8C0B6148).
 *  func_8C07061C  acceleration = motion acc minus weight on the global Y axis,
 *                 converted to player space, damped by air_resist_air/_y/_z.
 *                 With stick input the forward acceleration is air_accel (or
 *                 air_break when running faster than run_speed and turning
 *                 by more than 0x6000) and the player turns towards the stick
 *                 (func_8C0720A0). Without input, below jog_speed the player
 *                 turns (func_8C072000, at most 0x200 per frame) towards the
 *                 horizontal direction of the shadow-tilted up vector.
 *                 Event-locked input (twp->flag & 0x4000) goes to
 *                 func_8C06EFCE instead.
 *  func_8C070814  same with an extra -0.05 on Y and no X damping; without
 *                 input the aim is the current facing.
 * Matching notes:
 *  - the shadow tilt rotations are written "a ? njRotateZ(NULL, a) : (void)0"
 *    with no local: SHC keeps the common subexpression a in a stack slot of
 *    its own (stored in the BT/S delay slot, reloaded for the call) placed
 *    above the named scalars, and alternates R2/R3 for the two loads like
 *    the original. A volatile local gives the same store/reload but takes a
 *    slot below ang (reference-count order) and loads both angles into R2.
 *  - the "+=" products are FMACs; written SA1_FMAC_ACC(c, a, b), i.e.
 *    c += a * b for SHC (SA1_FMAC's a * b + c loads the operands in another
 *    order) and fused on PC like the FMAC.
 */

void func_8C06EFCE(taskwk *twp, motionwk *mwp, playerwk *pwp);            /* locked-input acceleration */
void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleYQ-like (turn on the spot) */
void func_8C0720A0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleYS-like */

void func_8C07061C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR spd;
    NJS_VECTOR vec;
    Angle ang;
    Float mag;
    Sint32 diff;

    if (twp->flag & 0x4000) {
        func_8C06EFCE(twp, mwp, pwp);
        return;
    }
    spd = mwp->acc;
    spd.y += -pwp->p.weight;
    PConvertVector_G2P(twp, &spd);
    SA1_FMAC_ACC(spd.x, pwp->p.air_resist_air, pwp->spd.x);
    SA1_FMAC_ACC(spd.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(spd.z, pwp->p.air_resist_z, pwp->spd.z);
    if (GetAnalog(twp, &ang, &mag) == 0) {
        mag = 0.0f;
        if (pwp->spd.x <= pwp->p.jog_speed) {
            vec.x = 0.0f;
            vec.y = 1.0f;
            vec.z = 0.0f;
            func_8C057B40();
            pwp->shadow.angz ? njRotateZ(NULL, pwp->shadow.angz) : (void)0;
            pwp->shadow.angx ? njRotateX(NULL, pwp->shadow.angx) : (void)0;
            njCalcVector(NULL, &vec, &vec);
            njPopMatrix(1);
            vec.y = 0.0f;
            if (njScalor(&vec) > 0.01f) {
                mwp->ang_aim.y = AdjustAngle(twp->ang.y, SA1_FTOI(atan2f(vec.z, vec.x) * 10430.381f), 0x200);
                func_8C072000(twp, mwp, pwp, mwp->ang_aim.y);
            }
        } else {
            mwp->ang_aim.y = twp->ang.y;
        }
    } else {
        diff = DiffAngle(twp->ang.y, ang);
        if (pwp->spd.x > pwp->p.run_speed && diff > 0x6000) {
            mag = pwp->p.air_break * mag;
        } else {
            mwp->ang_aim.y = ang;
            mag = pwp->p.air_accel * mag;
        }
        func_8C0720A0(twp, mwp, pwp, mwp->ang_aim.y);
    }
    spd.x += mag;
    pwp->acc = spd;
}

void func_8C070814(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR spd;
    Angle ang;
    Float mag;
    Sint32 diff;

    if (twp->flag & 0x4000) {
        func_8C06EFCE(twp, mwp, pwp);
        return;
    }
    spd = mwp->acc;
    spd.y += -pwp->p.weight + -0.0499999975f;
    PConvertVector_G2P(twp, &spd);
    SA1_FMAC_ACC(spd.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(spd.z, pwp->p.air_resist_z, pwp->spd.z);
    if (GetAnalog(twp, &ang, &mag) == 0) {
        mag = 0.0f;
        mwp->ang_aim.y = twp->ang.y;
    } else {
        diff = DiffAngle(twp->ang.y, ang);
        if (pwp->spd.x > pwp->p.run_speed && diff > 0x6000) {
            mag = pwp->p.air_break * mag;
        } else {
            mag = pwp->p.air_accel * mag;
        }
        func_8C0720A0(twp, mwp, pwp, mwp->ang_aim.y);
    }
    spd.x += mag;
    pwp->acc = spd;
}
