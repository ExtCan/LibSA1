#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06EAC0): ground acceleration of a player whose
 * movement is driven by an event (twp->flag & 0x4000, eventwk.lock); called
 * by PGetAcceleration 8C06D254. Same resistance/friction tail as the free
 * ground acceleration func_8C06E4E0, with the stick input replaced by:
 *  lock 2  walk to the point ewp+0x28 (target speed f34 or run_speed, accel
 *          f38 or run_accel); within 1.0 of it: PClearSpeed, lock = 1
 *  lock 3  run along the direction ewp+0x28, its length being the speed
 *  lock 4  push along the vector ewp+0x28 (added to the acceleration)
 *  lock 1  idle; any other value releases the lock (flag &= ~0x4000)
 *
 * Matching notes: the three "PClearSpeed; lock = 1; break" copies are
 * merged by SHC into the one in case 4 (a single copy or a goto changes
 * which constants are kept in R8/R9); "vec.x = vec.y = vec.z = 0.0f" and the
 * "x += a * b" spelling of the FMACs (SA1_FMAC_ACC; SA1_FMAC(a, b, x) loads
 * the addend first) are needed for the register/scheduling choices.
 */

/* PAdjustAngleYQ-like (turn on the spot), R4-R7; also declared locally by
 * code_8C06E4E0.c and code_8C06EFCE.c */
void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang);

/* eventwk +0x28 holds a point/vector (three floats) for locks 2-4; the
 * header declares it as Angle ang[3] (same size and alignment) */
#define EWP_POS(ewp) (*(NJS_POINT3 *)(ewp)->ang)

void func_8C06EAC0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR vec;
    NJS_VECTOR spd;
    NJS_VECTOR v;
    eventwk *ewp;
    Float s;
    Float lim;

    ewp = (eventwk *)twp->ewp;
    spd = mwp->acc;
    vec.x = vec.y = vec.z = 0.0f;
    SA1_FMAC_ACC(spd.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(spd.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(spd.z, Gravity.z, pwp->p.weight);
    switch ((Uint8)ewp->lock) {
    case 2:
        v = EWP_POS(ewp);
        njSubVector(&v, &twp->pos);
        v.y = 0.0f;
        s = njScalor(&v);
        if (s > 1.0f) {
            mwp->ang_aim.y = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f) & 0xFFFF;
            s = ewp->f34;
            if (s == 0.0f) {
                s = pwp->p.run_speed;
            }
            lim = ewp->f38;
            if (lim == 0.0f) {
                lim = pwp->p.run_accel;
            }
            if ((vec.x = s - pwp->spd.x) > 0.0f) {
                if (vec.x > lim) {
                    vec.x = lim;
                }
            } else {
                vec.x = 0.0f;
            }
            break;
        }
        PClearSpeed(NULL, pwp);
        ewp->lock = 1;
        break;
    case 3:
        v = EWP_POS(ewp);
        v.y = 0.0f;
        s = njScalor(&v);
        if (s == 0.0f) {
            PClearSpeed(NULL, pwp);
            ewp->lock = 1;
            break;
        }
        mwp->ang_aim.y = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f) & 0xFFFF;
        if ((vec.x = s - pwp->spd.x) > 0.0f) {
            if (vec.x > pwp->p.run_accel) {
                vec.x = pwp->p.run_accel;
            }
        } else {
            vec.x = 0.0f;
        }
        break;
    case 4:
        v = EWP_POS(ewp);
        v.y = 0.0f;
        s = njScalor(&v);
        if (s == 0.0f) {
            PClearSpeed(NULL, pwp);
            ewp->lock = 1;
            break;
        }
        mwp->ang_aim.y = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f) & 0xFFFF;
        njAddVector(&spd, &v);
        break;
    case 1:
        break;
    default:
        twp->flag &= ~0x4000;
        break;
    }
    PConvertVector_G2P(twp, &spd);
    if (pwp->spd.x > pwp->p.max_x_spd) {
        SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x - pwp->p.max_x_spd);
    } else if (pwp->spd.x < 0.0f) {
        SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x);
    }
    SA1_FMAC_ACC(spd.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(spd.z, pwp->p.air_resist_z, pwp->spd.z);
    func_8C072000(twp, mwp, pwp, mwp->ang_aim.y);
    if (pwp->spd.x == 0.0f) {
        lim = spd.y * pwp->p.lim_frict * mwp->weight;
        spd.x += vec.x;
        if (vec.x == 0.0f && lim > spd.x && spd.x > -lim) {
            spd.x = 0.0f;
        }
        if (vec.x == 0.0f && spd.x < 0.051f && spd.x > -0.051f) {
            spd.x = 0.0f;
        }
    } else if (vec.x < 0.0f) {
        if (pwp->spd.x > 0.0f) {
            s = spd.x + vec.x;
            if (s * vec.x < 0.0f) {
                s = 0.0f;
            }
        } else if (vec.x == 0.0f && pwp->spd.x <= pwp->p.jog_speed && spd.x < 0.051f && spd.x > -0.051f) {
            s = 0.0f;
        } else {
            lim = spd.y * pwp->p.lim_frict * mwp->weight;
            s = lim + vec.x;
            if (!(s < 0.0f)) {
                s = 0.0f;
            }
        }
        spd.x = s;
    } else {
        lim = spd.y * pwp->p.lim_frict * mwp->weight;
        if (pwp->spd.x < 0.0f && spd.x < 0.051f && spd.x > -0.051f) {
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
        } else if (vec.x == 0.0f && pwp->spd.x <= pwp->p.jog_speed && spd.x < 0.051f && spd.x > -0.051f) {
            spd.x = 0.0f;
        } else {
            spd.x += vec.x;
        }
    }
    if (pwp->spd.z == 0.0f) {
        lim = spd.y * pwp->p.lim_frict * mwp->weight;
        if (lim > spd.z && spd.z > -lim) {
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
    pwp->acc = spd;
}
