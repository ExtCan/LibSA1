#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C0700C2-8C07061C).
 *  func_8C0700C2  snowboard acceleration (SADX PGetAccelerationSnowBoard
 *                 analogue; callers SonicMain and TailsMain, max_spd in FR4).
 *                 Motion acc minus weight turned into player space, air
 *                 resistance (stronger above max_spd / when going backwards),
 *                 stick steering relative to the camera (D_8C18B7EC; its smode
 *                 byte mirrors the side input), the side input is kept in
 *                 mwp->rad; forward push run_accel (x0.4 above max_x_spd) up
 *                 to jog_speed; the speed is blended towards its horizontal
 *                 length by 0.6..0.9 depending on spd.x, then ground friction
 *                 (lim_frict on x, lim_frict / grd_frict_z on z) is applied.
 *                 Result in pwp->acc; skipped while twp->flag & 0x4000.
 * Notes: the atan2f result is unused in the original (dead call kept).
 * The pool constants 4.6/9.2 are one ulp below the truncated decimals
 * (0x40933332, 0x41133332; = 0.46f * 10 / * 20) and 0x41DE9BD5 = 256 / that
 * 9.2; the literals below are in the portable window for those bits.
 * The 12 FMACs are SA1_FMAC / SA1_FMAC_ACC (the accumulates stay "c += a * b"
 * for SHC; SA1_FMAC(a, b, acc) there would change the SHC output).
 */

void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleYQ-like (turn on the spot) */

extern taskwk *D_8C18B7EC;      /* camera taskwk */

void func_8C0700C2(taskwk *twp, motionwk *mwp, playerwk *pwp, Float max_spd)
{
    NJS_VECTOR acc;
    NJS_VECTOR add;
    NJS_VECTOR dir;
    Sint32 analog;
    Angle ang;
    Float mag;
    Sint32 diff;
    Sint32 sang;
    Sint32 rot;
    Float s;
    Float r1;
    Float r2;
    Float t;
    Float sx;

    acc = mwp->acc;
    add.z = 0.0f;
    add.y = 0.0f;
    add.x = 0.0f;
    if (twp->flag & 0x4000) {
        return;
    }
    acc.y += -pwp->p.weight;
    PConvertVector_G2P(twp, &acc);
    analog = GetAnalog(twp, &ang, &mag);
    if (pwp->spd.x > max_spd) {
        SA1_FMAC_ACC(acc.x, pwp->p.air_resist, pwp->spd.x);
        SA1_FMAC_ACC(acc.z, pwp->p.air_resist * pwp->spd.z, 1.5f);
    } else if (pwp->spd.x < 0.0f) {
        SA1_FMAC_ACC(acc.x, pwp->p.air_resist * pwp->spd.x, 1.5f);
        SA1_FMAC_ACC(acc.z, pwp->p.air_resist * pwp->spd.z, 2.0f);
    }
    SA1_FMAC_ACC(acc.y, pwp->p.air_resist_y, pwp->spd.y);
    dir.z = 0.0f;
    dir.x = 0.0f;
    dir.y = -1.0f;
    PConvertVector_G2P(twp, &dir);
    if (SA1_FMAC(dir.x, dir.x, dir.z * dir.z) > 0.0099999998f) {
        atan2f(dir.z, dir.x);
    }
    if (analog) {
        diff = DiffAngle(twp->ang.y, ang);
        if (pwp->spd.x == 0.0f && diff > 0x1000) {
            mwp->ang_aim.y = ang;
            func_8C072000(twp, mwp, pwp, mwp->ang_aim.y);
        } else {
            sang = SubAngle(0xC000 - D_8C18B7EC->ang.y, ang);
            if (D_8C18B7EC->smode) {
                sang = 0x10000 - sang;
            }
            mwp->rad = mag * njSin(sang);
            if (pwp->mj.mtnmode == 12) {
                t = pwp->mj.nframe / (Float)pwp->mj.plactptr->actptr->motion->nbFrame;
                mag *= 0.5f;
                SA1_FMAC_ACC(mag, t, 0.005f);
            }
            s = mag * njSin(sang);
            rot = 0x200 - SA1_FTOI((pwp->spd.x - 9.199999f) * 27.8260903f);
            if (rot < 0x120) {
                rot = 0x120;
            } else if (rot > 0x280) {
                rot = 0x280;
            }
            rot = twp->ang.y + SA1_FTOI((Float)rot * s);
            mwp->ang_aim.y = rot;
            ang = DiffAngle(twp->ang.y, mwp->ang_aim.y) >> 4;
            twp->ang.y = AdjustAngle(twp->ang.y, mwp->ang_aim.y, ang + 0x100);
        }
        if (pwp->spd.x < pwp->p.max_x_spd) {
            mag *= pwp->p.run_accel;
        } else {
            mag *= pwp->p.run_accel * 0.39999998f;
        }
        if (pwp->spd.x <= pwp->p.jog_speed) {
            add.x = mag;
        }
    }
    sx = pwp->spd.x;
    dir.x = njSqrt(SA1_FMAC(sx, sx, pwp->spd.z * pwp->spd.z));
    dir.y = pwp->spd.y;
    dir.z = 0.0f;
    if (pwp->spd.x < 0.0f) {
        dir.x = -dir.x;
    }
    if (pwp->spd.x < 4.5999995f) {
        r1 = 0.59999998f;
        r2 = 0.39999998f;
    } else if (pwp->spd.x < 9.199999f) {
        r1 = SA1_FMAC(pwp->spd.x - 4.5999995f, 0.065217391f, 0.59999998f);
        r2 = 1.0f - r1;
    } else {
        r1 = 0.9f;
        r2 = 0.099999996f;
    }
    pwp->spd.x = SA1_FMAC(pwp->spd.x, r1, dir.x * r2);
    pwp->spd.y = SA1_FMAC(pwp->spd.y, r1, dir.y * r2);
    pwp->spd.z = SA1_FMAC(pwp->spd.z, r1, dir.z * r2);
    if (add.x < 0.0f) {
        if (pwp->spd.x > 0.0f) {
            t = acc.x + add.x;
            if (t * add.x < 0.0f) {
                t = 0.0f;
            }
        } else {
            if (acc.y < 0.0f) {
                t = acc.y * pwp->p.lim_frict * mwp->weight;
            } else {
                t = 0.0f;
            }
            t += add.x;
            if (t >= 0.0f) {
                t = 0.0f;
            }
        }
        acc.x = t;
    } else {
        if (acc.y < 0.0f) {
            t = acc.y * pwp->p.lim_frict * mwp->weight;
        } else {
            t = 0.0f;
        }
        acc.x += add.x;
        if (acc.x > 0.0f) {
            if (t > acc.x) {
                acc.x = 0.0f;
            } else {
                acc.x -= t;
            }
        } else if (acc.x < 0.0f) {
            if (acc.x > -t) {
                acc.x = 0.0f;
            } else {
                acc.x += t;
            }
        }
    }
    if (pwp->spd.z == 0.0f) {
        t = acc.y * pwp->p.lim_frict * mwp->weight;
        if (t < 0.0f) {
            t = 0.0f;
        }
        acc.z += add.z;
        if (t > acc.z && acc.z > -t) {
            acc.z = 0.0f;
        }
    } else {
        t = acc.y * pwp->p.grd_frict_z * mwp->weight;
        if (t < 0.0f) {
            t = 0.0f;
        }
        acc.z += add.z;
        if (acc.z > 0.0f) {
            if (t > acc.z) {
                acc.z = 0.0f;
            } else {
                acc.z -= t;
            }
        } else if (acc.z < 0.0f) {
            if (acc.z > -t) {
                acc.z = 0.0f;
            } else {
                acc.z += t;
            }
        }
    }
    pwp->acc = acc;
}
