#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06FB8C), called by SonicMain 8C0A77BC.
 *  func_8C06FB8C  board-style acceleration: weight pulls on the
 *                 player-space x and y, air resistance on the speed (with a
 *                 higher x limit, max_x_spd * 1.2, for Sonic with
 *                 twp->flag & 0x100), a forward push (run_accel when
 *                 flag & 1 and spd.x < max_x_spd, else run_accel * 0.3)
 *                 kept in mwp->rad, the
 *                 raw stick of the pad record B_8C754C08[pno] (dead zone 3072)
 *                 turned into a camera-relative heading clamped to +-0x800
 *                 (+-0x400 with flag & 0x100), friction limits on x and z
 *                 (the x excess above the limit goes to mwp->height). The
 *                 result goes to pwp->acc. Does nothing under input lock
 *                 (twp->flag & 0x4000).
 *
 * NON_MATCHING: 7 instructions differ, all in the final z section: the
 * original keeps the sum v.z + w.z in FR5 and w.z in FR6, this C swaps them.
 * Everything else matches. Without the "register" declarations SHC splits
 * "lim" into separate live ranges and the z friction section allocates
 * v.y/lim to FR4/FR5 the wrong way round (15 differences); this exact set
 * of register declarations reproduces the original integer allocation
 * (pwp R14, twp R13, mwp R12, ang R11) and the z friction allocation.
 *
 * libsa1: the five air-resistance accumulations (three on v.x, v.y, v.z)
 * are FMAC in the original, hence SA1_FMAC_ACC. clang tail-merges the two
 * "spd.x - limit" branches into one fma, so fmac_audit counts 4 PC fused
 * ops for the 5 FMAC (same results).
 */

/* rotation helpers (8C07061C chunk, R4-R7) */
void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang);
void func_8C0720A0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang);

extern taskwk *D_8C18B7EC;      /* camera task work */

#ifdef NON_MATCHING
void func_8C06FB8C(register taskwk *twp, register motionwk *mwp, register playerwk *pwp)
{
    NJS_VECTOR v;
    NJS_VECTOR w;
    Sint32 r;
    Float fx;
    Float fy;
    register Sint32 ang;
    register Sint32 d;
    register Float lim;
    Float f;

    v = mwp->acc;
    w.x = w.y = w.z = 0.0f;
    if (twp->flag & 0x4000) {
        return;
    }
    v.y += -pwp->p.weight;
    v.x += -pwp->p.weight;
    PConvertVector_G2P(twp, &v);
    r = GetAnalog(twp, NULL, NULL);
    if (!(twp->flag & 1) && pwp->spd.x > pwp->p.max_x_spd) {
        SA1_FMAC_ACC(v.x, pwp->p.air_resist, pwp->spd.x);
    } else if (twp->counter.b[1] == 0 && (twp->flag & 0x100) && pwp->spd.x > pwp->p.max_x_spd * 1.19999996f) {
        SA1_FMAC_ACC(v.x, pwp->p.air_resist, pwp->spd.x - pwp->p.max_x_spd * 1.19999996f);
    } else if (!(twp->flag & 0x100) && pwp->spd.x > pwp->p.max_x_spd) {
        SA1_FMAC_ACC(v.x, pwp->p.air_resist, pwp->spd.x - pwp->p.max_x_spd);
    }
    SA1_FMAC_ACC(v.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(v.z, pwp->p.air_resist_z, pwp->spd.z);
    if ((twp->flag & 1) && pwp->spd.x < pwp->p.max_x_spd) {
        f = pwp->p.run_accel;
    } else {
        f = pwp->p.run_accel * 0.29999999f;
    }
    mwp->rad = f;
    w.x = f;
    if (r == 0) {
        mwp->ang_aim.y = 0;
        func_8C072000(twp, mwp, pwp, 0);
    } else {
        fx = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8);
        fy = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8);
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
        ang = SA1_FTOI(atan2f(fy, fx) * 10430.381f);
        if (D_8C18B7EC->ang.z >= 0x4000) {
            ang -= D_8C18B7EC->ang.y;
        } else {
            ang = -ang - D_8C18B7EC->ang.y + 0x8000;
        }
        d = SubAngle(ang, 0);
        if (d > 0x800) {
            if (twp->flag & 0x100) {
                ang = 0x400;
            } else {
                ang = 0x800;
            }
        } else if (d < -0x800) {
            if (twp->flag & 0x100) {
                ang = -0x400;
            } else {
                ang = -0x800;
            }
        }
        mwp->ang_aim.y = (Uint16)ang;
        func_8C0720A0(twp, mwp, pwp, mwp->ang_aim.y);
    }
    lim = v.y * pwp->p.lim_frict * mwp->weight;
    if (!(twp->flag & 1)) {
        v.x += w.x;
    } else if (lim > 0.0f && w.x > lim) {
        f = w.x + v.x;
        if (f > lim) {
            mwp->height = f - lim;
            f = lim;
        }
        v.x = f;
    } else {
        v.x += w.x;
    }
    if (pwp->spd.z == 0.0f) {
        lim = v.y * pwp->p.lim_frict * mwp->weight;
        if (v.z < lim && v.z > -lim) {
            v.z = 0.0f;
        }
    } else {
        if (v.y < 0.0f) {
            lim = v.y * pwp->p.grd_frict_z * mwp->weight;
        } else {
            lim = 0.0f;
        }
        if (v.z > 0.0f) {
            w.z -= lim;
        } else if (v.z < 0.0f) {
            w.z += lim;
        }
        f = v.z + w.z;
        if (v.z != 0.0f && w.z != 0.0f && f * v.z < 0.0f) {
            f = 0.0f;
        }
        v.z = f;
    }
    pwp->acc = v;
}
#else
ASM_STUB(void func_8C06FB8C(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
