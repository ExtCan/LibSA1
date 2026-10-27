#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C070EDA-8C0714B6): braking/ground-friction
 * acceleration helpers used by the character mains.
 *  func_8C070EDA  acceleration = motion acc + gravity * weight, converted to
 *                 player space, damped by air_resist/_y/_z; run_break *
 *                 mwp->weight against the X and Z speed; on the ground
 *                 (twp->flag & 3) grd_frict / grd_frict_z friction, the
 *                 lim_frict dead zone when standing still and a clamp that
 *                 keeps the braking from reversing the speed; then turns
 *                 towards mwp->ang_aim.y (func_8C071EC0). Result in pwp->acc.
 *  func_8C0711D2  same without braking and turning; on the ground the
 *                 collision's anaspdv (scaled by max_x_spd / (weight * 5/7),
 *                 then cleared) is added to Y and Z.
 * Event-locked input (twp->flag & 0x4000) leaves pwp->acc unchanged.
 */

void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleY-like */

void func_8C070EDA(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR spd;
    NJS_VECTOR vec;
    Float lim;
    Float s;

    spd = mwp->acc;
    vec.z = 0.0f;
    vec.y = 0.0f;
    vec.x = 0.0f;
    if (twp->flag & 0x4000) {
        return;
    }
    SA1_FMAC_ACC(spd.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(spd.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(spd.z, Gravity.z, pwp->p.weight);
    PConvertVector_G2P(twp, &spd);
    SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x);
    SA1_FMAC_ACC(spd.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(spd.z, pwp->p.air_resist_z, pwp->spd.z);
    if (pwp->spd.x > 0.0f) {
        SA1_FMAC_ACC(vec.x, pwp->p.run_break, mwp->weight);
    } else if (pwp->spd.x < 0.0f) {
        vec.x -= pwp->p.run_break * mwp->weight;
    }
    if (pwp->spd.z > 0.0f) {
        SA1_FMAC_ACC(vec.z, pwp->p.run_break, mwp->weight);
    } else if (pwp->spd.z < 0.0f) {
        vec.z -= pwp->p.run_break * mwp->weight;
    }
    if (twp->flag & 3) {
        if (pwp->spd.x == 0.0f) {
            lim = spd.y * pwp->p.lim_frict * mwp->weight;
            if (lim > spd.x && spd.x > -lim) {
                spd.x = 0.0f;
            }
        } else {
            if (spd.y < 0.0f) {
                lim = spd.y * pwp->p.grd_frict * mwp->weight;
            } else {
                lim = 0.0f;
            }
            s = spd.x + vec.x;
            if (s > 0.0f) {
                s -= lim;
                if (s < 0.0f) {
                    s = 0.0f;
                }
            } else if (s < 0.0f) {
                s += lim;
                if (s > 0.0f) {
                    s = 0.0f;
                }
            }
            spd.x = s;
            if (pwp->spd.x > 0.0f && -spd.x > pwp->spd.x) {
                spd.x = -pwp->spd.x;
            } else if (pwp->spd.x < 0.0f && -spd.x < pwp->spd.x) {
                spd.x = -pwp->spd.x;
            }
        }
        if (pwp->spd.z == 0.0f) {
            lim = spd.y * pwp->p.lim_frict * mwp->weight;
            if (lim > spd.z && spd.z > -lim) {
                spd.z = 0.0f;
            }
        } else {
            if (spd.y < 0.0f) {
                lim = spd.y * pwp->p.grd_frict_z * mwp->weight;
            } else {
                lim = 0.0f;
            }
            if (spd.z > 0.0f) {
                vec.z -= lim;
            } else if (spd.z < 0.0f) {
                vec.z += lim;
            }
            spd.z += vec.z;
            if (pwp->spd.z > 0.0f && -spd.z > pwp->spd.z) {
                spd.z = -pwp->spd.z;
            } else if (pwp->spd.z < 0.0f && -spd.z < pwp->spd.z) {
                spd.z = -pwp->spd.z;
            }
        }
    }
    func_8C071EC0(twp, mwp, pwp, mwp->ang_aim.y);
    pwp->acc = spd;
}

void func_8C0711D2(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR spd;
    NJS_VECTOR vec;
    NJS_VECTOR v;
    csts *cp;
    Float f;
    Float lim;

    spd = mwp->acc;
    vec.x = vec.y = vec.z = 0.0f;
    if (twp->flag & 0x4000) {
        return;
    }
    SA1_FMAC_ACC(spd.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(spd.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(spd.z, Gravity.z, pwp->p.weight);
    PConvertVector_G2P(twp, &spd);
    if (twp->flag & 3) {
        cp = pwp->cstsp;
        f = pwp->p.max_x_spd / (pwp->p.weight * 0.71428569f);
        v.x = cp->anaspdv.x / f;
        v.y = cp->anaspdv.y / f;
        v.z = cp->anaspdv.z / f;
        cp->anaspdv.y = 0.0f;
        cp->anaspdv.x = 0.0f;
        cp->anaspdv.z = 0.0f;
        PConvertVector_G2P(twp, &v);
        spd.y += v.y;
        spd.z += v.z;
    }
    SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x);
    SA1_FMAC_ACC(spd.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(spd.z, pwp->p.air_resist_z, pwp->spd.z);
    if (twp->flag & 3) {
        if (pwp->spd.x == 0.0f) {
            lim = spd.y * pwp->p.lim_frict * mwp->weight;
            if (lim > spd.x && spd.x > -lim) {
                spd.x = 0.0f;
            }
        } else {
            if (spd.y < 0.0f) {
                lim = spd.y * pwp->p.grd_frict * mwp->weight;
            } else {
                lim = 0.0f;
            }
            if (spd.x > 0.0f) {
                vec.x -= lim;
            } else if (spd.x < 0.0f) {
                vec.x += lim;
            }
            spd.x += vec.x;
            if (pwp->spd.x > 0.0f && -spd.x > pwp->spd.x) {
                spd.x = -pwp->spd.x;
            } else if (pwp->spd.x < 0.0f && -spd.x < pwp->spd.x) {
                spd.x = -pwp->spd.x;
            }
        }
        if (pwp->spd.z == 0.0f) {
            lim = spd.y * pwp->p.lim_frict * mwp->weight;
            if (lim > spd.z && spd.z > -lim) {
                spd.z = 0.0f;
            }
        } else {
            if (spd.y < 0.0f) {
                lim = spd.y * pwp->p.grd_frict_z * mwp->weight;
            } else {
                lim = 0.0f;
            }
            if (spd.z > 0.0f) {
                vec.z -= lim;
            } else if (spd.z < 0.0f) {
                vec.z += lim;
            }
            spd.z += vec.z;
            if (pwp->spd.z > 0.0f && -spd.z > pwp->spd.z) {
                spd.z = -pwp->spd.z;
            } else if (pwp->spd.z < 0.0f && -spd.z < pwp->spd.z) {
                spd.z = -pwp->spd.z;
            }
        }
    }
    pwp->acc = spd;
}

ASM_STUB(void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang))
