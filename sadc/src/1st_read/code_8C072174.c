#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C072174-8C07266E): speed integration.
 *  PGetSpeed      pwp->spd += pwp->acc with stop snapping: X stops when it
 *                 changes sign, or with the stick released below
 *                 0.87 * jog_speed while dotp > 0.7; Z stops when it drops
 *                 below 0.87 * jog_speed or stays unchanged under it. Adds
 *                 run_break above 0.8 * jog_speed (shadow.Attr_top & 0x400000)
 *                 or above (run + jog) / 2 (& 0x400002), converts to global
 *                 space (along the floor normal when pwp->attr & 0x4000)
 *                 into mwp->spd, adds pwp->eff, then clamps and snaps.
 *  func_8C0723E0  clamps mwp->spd to +-lim_h_spd (X, Z) and +-lim_v_spd (Y).
 *  func_8C072510  zeroes |mwp->spd| components < 0.02 when the stick is idle.
 *  func_8C07256A  PClearSpeed.
 *  func_8C0725C0  X-only speed integration (Y and Z cleared).
 */

void func_8C079000(NJS_POINT3 *nrm, NJS_POINT3 *v, Angle angy); /* player->global along a floor normal */

void func_8C0723E0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C072510(taskwk *twp, motionwk *mwp);

void PGetSpeed(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 spd;
    Float lim;
    Float s;

    s = pwp->spd.x;
    pwp->spd.x += pwp->acc.x;
    lim = pwp->p.jog_speed * 0.86999995f;
    if (s > 0.0f) {
        if (pwp->spd.x < 0.0f) {
            pwp->spd.x = 0.0f;
        } else if (!GetAnalog(twp, NULL, NULL) && pwp->spd.x < lim && pwp->dotp > 0.69999999f) {
            pwp->spd.x = 0.0f;
        }
    } else if (s < 0.0f) {
        if (pwp->spd.x > -0.0f) {
            pwp->spd.x = 0.0f;
        } else if (!GetAnalog(twp, NULL, NULL) && pwp->spd.x > -lim && pwp->dotp > 0.69999999f) {
            pwp->spd.x = 0.0f;
        }
    }
    pwp->spd.y += pwp->acc.y;
    s = pwp->spd.z;
    pwp->spd.z += pwp->acc.z;
    if (s > 0.0f) {
        if (s < lim && s == pwp->spd.z) {
            pwp->spd.z = 0.0f;
        } else if (s >= lim && pwp->spd.z < lim) {
            pwp->spd.z = 0.0f;
        }
    } else if (s < 0.0f) {
        if (s > -lim && s == pwp->spd.z) {
            pwp->spd.z = 0.0f;
        } else if (s <= -lim && pwp->spd.z > -lim) {
            pwp->spd.z = 0.0f;
        }
    }
    if (pwp->shadow.Attr_top & 0x400000) {
        if (pwp->spd.x >= pwp->p.jog_speed * 0.79999996f) {
            pwp->spd.x += pwp->p.run_break;
        }
    } else if (pwp->shadow.Attr_top & 0x400002) {
        if (pwp->spd.x >= (pwp->p.run_speed + pwp->p.jog_speed) * 0.5f) {
            pwp->spd.x += pwp->p.run_break;
        }
    }
    spd = pwp->spd;
    if (pwp->attr & 0x4000) {
        func_8C079000(&pwp->floor_normal, &spd, twp->ang.y);
    } else {
        PConvertVector_P2G(twp, &spd);
    }
    mwp->spd = spd;
    njAddVector(&mwp->spd, &pwp->eff);
    func_8C0723E0(twp, mwp, pwp);
    func_8C072510(twp, mwp);
}

void func_8C0723E0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (twp->flag & 0x800) {
        if (pwp->p.lim_h_spd < mwp->spd.x) {
            mwp->spd.x = pwp->p.lim_h_spd;
        }
        if (mwp->spd.x < -pwp->p.lim_h_spd) {
            mwp->spd.x = -pwp->p.lim_h_spd;
        }
        if (pwp->p.lim_h_spd < mwp->spd.z) {
            mwp->spd.z = pwp->p.lim_h_spd;
        }
        if (mwp->spd.z < -pwp->p.lim_h_spd) {
            mwp->spd.z = -pwp->p.lim_h_spd;
        }
        if (pwp->p.lim_v_spd < mwp->spd.y) {
            mwp->spd.y = pwp->p.lim_v_spd;
        }
        if (mwp->spd.y < -pwp->p.lim_v_spd) {
            mwp->spd.y = -pwp->p.lim_v_spd;
        }
    } else {
        if (pwp->p.lim_h_spd < mwp->spd.x) {
            mwp->spd.x = pwp->p.lim_h_spd;
        }
        if (mwp->spd.x < -pwp->p.lim_h_spd) {
            mwp->spd.x = -pwp->p.lim_h_spd;
        }
        if (pwp->p.lim_h_spd < mwp->spd.z) {
            mwp->spd.z = pwp->p.lim_h_spd;
        }
        if (mwp->spd.z < -pwp->p.lim_h_spd) {
            mwp->spd.z = -pwp->p.lim_h_spd;
        }
        if (pwp->p.lim_v_spd < mwp->spd.y) {
            mwp->spd.y = pwp->p.lim_v_spd;
        }
        if (mwp->spd.y < -pwp->p.lim_v_spd) {
            mwp->spd.y = -pwp->p.lim_v_spd;
        }
    }
}

void func_8C072510(taskwk *twp, motionwk *mwp)
{
    if (!GetAnalog(twp, NULL, NULL)) {
        if (mwp->spd.x < 0.02f && mwp->spd.x > -0.02f) {
            mwp->spd.x = 0.0f;
        }
        if (mwp->spd.y < 0.02f && mwp->spd.y > -0.02f) {
            mwp->spd.y = 0.0f;
        }
        if (mwp->spd.z < 0.02f && mwp->spd.z > -0.02f) {
            mwp->spd.z = 0.0f;
        }
    }
}

void func_8C07256A(motionwk *mwp, playerwk *pwp)
{
    if (pwp) {
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
    }
    if (mwp) {
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
    }
}

void func_8C0725C0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 spd;
    Float s;

    s = pwp->spd.x;
    pwp->spd.x += pwp->acc.x;
    if (s > 0.0f) {
        if (pwp->spd.x < 0.0f) {
            pwp->spd.x = 0.0f;
        } else if (s >= 0.099999996f && pwp->spd.x < 0.099999996f) {
            pwp->spd.x = 0.0f;
        }
    } else if (s < 0.0f) {
        if (pwp->spd.x > -0.0f) {
            pwp->spd.x = 0.0f;
        } else if (s <= -0.099999996f && pwp->spd.x > -0.099999996f) {
            pwp->spd.x = 0.0f;
        }
    }
    pwp->spd.y = 0.0f;
    pwp->spd.z = 0.0f;
    spd = pwp->spd;
    PConvertVector_P2G(twp, &spd);
    mwp->spd = spd;
    func_8C0723E0(twp, mwp, pwp);
}
