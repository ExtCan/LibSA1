#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06E4E0-8C06E904): acceleration of a player
 * standing on a surface with attribute bit 0x4000 (PGetAcceleration
 * 8C06D254 calls it when pwp->attr & 0x4000, after the twp->flag & 0x4000
 * and pwp->attr & 0x20000 cases).
 *  func_8C06E4E0  acc = mwp->acc + gravity * weight, speed resistance per
 *                 axis, stick input turned into forward acceleration
 *                 (mwp->rad) and a turn through the PAdjustAngleY family,
 *                 then static friction on x and z; result in pwp->acc.
 *
 * NON_MATCHING (functionally complete): two register choices differ.
 *  - plain-turn path (8C06E738): the original loads ang into R3, SHC gives R2.
 *  - z static friction (8C06E864): the original multiplies the hoisted spd.y
 *    in place (FR4); SHC keeps spd.y * lim_frict live (CSE of the repeated
 *    product) and puts the product in FR5 (one extra FMOV). A named variable
 *    for the product instead swaps FR4/FR5 for the whole z block.
 * The 0.8 and plain turn paths share one call of func_8C071EC0 through the
 * per-path argument a (this gives the original's per-path R7 setup); a
 * separate call in each path makes SHC keep &func_8C071EC0 in R8.
 * mwp->rad receives the input acceleration and mwp->height the part of the
 * x acceleration cut off by the friction limit (motionwk 0x34/0x38).
 * The "+=" products are FMACs (SA1_FMAC_ACC: fused on PC, the plain "+=" for
 * SHC; SA1_FMAC(a, b, c) changes the SHC output).
 */

void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleY-like */
void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleYQ-like (turn on the spot) */
void func_8C0720A0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleYS-like */

#ifdef NON_MATCHING
void func_8C06E4E0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR spd;
    NJS_VECTOR vec;
    Angle ang;
    Float mag;
    Sint32 analog;
    Sint32 diff;
    Float lim;
    Float s;
    Angle a;

    spd = mwp->acc;
    vec.x = vec.y = vec.z = 0.0f;
    SA1_FMAC_ACC(spd.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(spd.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(spd.z, Gravity.z, pwp->p.weight);
    analog = GetAnalog(twp, &ang, &mag);
    if (analog == 0 && pwp->spd.x > pwp->p.run_speed) {
        SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x);
    } else if (pwp->spd.x > pwp->p.rush_speed) {
        SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x - pwp->p.rush_speed);
    } else if (pwp->spd.x < 0.0f) {
        SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x);
    }
    SA1_FMAC_ACC(spd.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(spd.z, pwp->p.air_resist_z, pwp->spd.z);
    if (analog == 0) {
        if (pwp->spd.x > 0.0f) {
            vec.x = pwp->p.slow_down;
        } else if (pwp->spd.x < 0.0f) {
            vec.x = -pwp->p.slow_down;
        }
        mwp->rad = 0.0f;
    } else {
        if (pwp->p.run_speed > pwp->spd.x) {
            mag *= pwp->p.run_accel;
        } else {
            mag *= pwp->p.run_accel * 0.39999998f;
        }
        diff = DiffAngle(twp->ang.y, ang);
        if (pwp->spd.x == 0.0f && diff > 0x1000) {
            mwp->ang_aim.y = ang;
            mag = 0.0f;
            func_8C072000(twp, mwp, pwp, mwp->ang_aim.y);
        } else if (pwp->spd.x >= (pwp->p.jog_speed + pwp->p.run_speed) * 0.5f && diff > 0x1000) {
            mwp->ang_aim.y = ang;
            mag = pwp->p.slow_down;
            func_8C071EC0(twp, mwp, pwp, mwp->ang_aim.y);
        } else if (pwp->spd.x >= pwp->p.jog_speed && diff < 0x1000) {
            mwp->ang_aim.y = ang;
            func_8C0720A0(twp, mwp, pwp, mwp->ang_aim.y);
        } else {
            if (pwp->spd.x >= pwp->p.jog_speed && pwp->spd.x <= pwp->p.rush_speed && diff > 0x2000) {
                mwp->ang_aim.y = ang;
                mag *= 0.79999996f;
                a = mwp->ang_aim.y;
            } else {
                mwp->ang_aim.y = ang;
                a = mwp->ang_aim.y;
            }
            func_8C071EC0(twp, mwp, pwp, a);
        }
        mwp->rad = mag;
        vec.x = mag;
    }
    if (pwp->spd.x == 0.0f) {
        lim = spd.y * pwp->p.lim_frict * mwp->weight;
        spd.x += vec.x;
        if (analog == 0 && lim > spd.x && spd.x > -lim) {
            spd.x = 0.0f;
        }
    } else if (vec.x < 0.0f) {
        if (pwp->spd.x > 0.0f) {
            s = spd.x + vec.x;
            if (s * vec.x < 0.0f) {
                s = 0.0f;
            }
            spd.x = s;
        } else {
            lim = spd.y * pwp->p.lim_frict * mwp->weight;
            lim += vec.x + spd.x;
            if (!(lim < 0.0f)) {
                lim = 0.0f;
            }
            spd.x = lim;
        }
    } else {
        lim = spd.y * pwp->p.lim_frict * mwp->weight;
        if (pwp->spd.x < 0.0f) {
            spd.x += vec.x;
            if (spd.x * vec.x < 0.0f) {
                spd.x = 0.0f;
            }
        } else if (lim > 0.0f && vec.x > lim) {
            s = vec.x + spd.x;
            if (s > lim) {
                mwp->height = s - lim;
                s = lim;
            }
            spd.x = s;
        } else {
            spd.x += vec.x;
        }
    }
    if (pwp->spd.z == 0.0f) {
        if (spd.y * pwp->p.lim_frict * mwp->weight > spd.z && spd.z > -(spd.y * pwp->p.lim_frict * mwp->weight)) {
            spd.z = 0.0f;
        }
    } else {
        if (spd.y < 0.0f) {
            s = spd.y * pwp->p.grd_frict_z * mwp->weight;
        } else {
            s = 0.0f;
        }
        if (spd.z > 0.0f) {
            vec.z -= s;
        } else if (spd.z < 0.0f) {
            vec.z += s;
        }
        s = spd.z + vec.z;
        if (spd.z != 0.0f && vec.z != 0.0f && s * spd.z < 0.0f) {
            s = 0.0f;
        }
        spd.z = s;
    }
    spd.y += vec.y;
    pwp->acc = spd;
}
#else
ASM_STUB(void func_8C06E4E0(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
