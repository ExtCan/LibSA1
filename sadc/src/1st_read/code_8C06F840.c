#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06F840): air accelerations driven by the raw
 * stick of the player's pad record (B_8C754C08[pno]).
 *  func_8C06F840  gravity scaled by the weight, half the air resistance on
 *                 the player-space speed; the stick x turns the heading
 *                 directly, a pulled-back stick y (dead zone 3072) adds a
 *                 quadratic term scaled by air_accel. Result in pwp->acc.
 *  func_8C06F9A2  (KnucklesMain) mwp->acc.y = weight * 0.95, gravity, half
 *                 the air resistance, stick magnitude * air_accel, aim angle
 *                 from GetAnalog, rotation by 8C071EC0. Result in pwp->acc.
 *  func_8C06FB04  (KnucklesMain) pwp->acc from the raw stick only
 *                 (x = -y * k, z = x * k), zero without stick input.
 * All three do nothing under input lock (twp->flag & 0x4000).
 */

/* rotation towards the aim angle in the air (8C071EC0, R4-R7) */
void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang);

void func_8C06F840(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    Float mag;
    Float fx;
    Float fy;
    Angle ang;

    v = mwp->acc;
    if (twp->flag & 0x4000) {
        return;
    }
    SA1_FMAC_ACC(v.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(v.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(v.z, Gravity.z, pwp->p.weight);
    PConvertVector_G2P(twp, &v);
    SA1_FMAC_ACC(v.x, pwp->p.air_resist * pwp->spd.x, 0.5f);
    SA1_FMAC_ACC(v.y, pwp->p.air_resist_y * pwp->spd.y, 0.5f);
    SA1_FMAC_ACC(v.z, pwp->p.air_resist_z * pwp->spd.z, 0.5f);
    if (GetAnalog(twp, NULL, NULL) == 0) {
        mag = 0.0f;
        mwp->ang_aim.y = twp->ang.y;
    } else {
        fx = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8);
        fy = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8);
        /* fx is dead: SHC drops the arithmetic but keeps the compare
         * (FCMP/GT at 8C06F932) */
        if (fx >= 0.0f) {
            fx -= 3072.0f;
        } else {
            fx += 3072.0f;
        }
        if (fy >= 0.0f) {
            fy -= 3072.0f;
        } else {
            fy += 3072.0f;
        }
        ang = twp->ang.y + ((B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8) >> 6);
        mag = fy;
        if (fy > 0.0f) {
            mag = 0.0f;
        }
        mag = mag * mag * 1.13397661e-09f;
        mwp->ang_aim.y = ang;
        twp->ang.y = ang;
        mag = pwp->p.air_accel * mag;
    }
    v.x += mag;
    pwp->acc = v;
}

void func_8C06F9A2(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    Angle ang;
    Float mag;

    if (twp->flag & 0x4000) {
        return;
    }
    mwp->acc.y = pwp->p.weight * 0.95f;
    v = mwp->acc;
    SA1_FMAC_ACC(v.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(v.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(v.z, Gravity.z, pwp->p.weight);
    PConvertVector_G2P(twp, &v);
    SA1_FMAC_ACC(v.x, pwp->p.air_resist * pwp->spd.x, 0.5f);
    SA1_FMAC_ACC(v.y, pwp->p.air_resist_y * pwp->spd.y, 0.5f);
    SA1_FMAC_ACC(v.z, pwp->p.air_resist_z * pwp->spd.z, 0.5f);
    if (GetAnalog(twp, &ang, &mag) == 0) {
        mag = 0.0f;
        mwp->ang_aim.y = twp->ang.y;
    } else {
        mag = pwp->p.air_accel * mag;
        mwp->ang_aim.y = ang;
        func_8C071EC0(twp, mwp, pwp, mwp->ang_aim.y);
    }
    v.x += mag;
    pwp->acc = v;
}

void func_8C06FB04(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    Float fx;
    Float fy;

    if (twp->flag & 0x4000) {
        return;
    }
    if (GetAnalog(twp, NULL, NULL)) {
        fx = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8);
        fy = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8);
        v.x = -(fy * 2.44140615e-05f);
        v.y = 0.0f;
        v.z = fx * 2.44140615e-05f;
    } else {
        v.x = v.y = v.z = 0.0f;
    }
    pwp->acc = v;
}
