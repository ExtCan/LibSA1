#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06D254-8C06DCA2): PGetAcceleration, the ground
 * acceleration of a player in player space (x forward, y up, z sideways).
 * Called by every character main (Sonic, Tails, Knuckles, Amy, Big, Eggman,
 * 8C0AF280, 8C0AF6C4, 8C0D203A).
 *
 *  - event-locked players (twp->flag & 0x4000) and two special surfaces
 *    (attr 0x20000: 8C06DCA2, attr 0x4000: 8C06E4E0) use other routines;
 *  - gravity * weight is converted to player space and scaled by the slope
 *    (pwp->dotp) and by |floor normal x speed|;
 *  - air resistance; stick stroke turned into a forward drive (vd.x) whose
 *    strength depends on the speed band (jog/run/rush/max), with the turning
 *    helpers 8C072000 / 8C071EC0 / 8C0720A0 chosen by speed and angle change;
 *  - ground friction clamps for x (lim_frict / grd_frict) and z (grd_frict_z).
 * motionwk.rad receives the drive and motionwk.height the friction excess
 * (scratch use of these fields, as in SADX).
 *
 * NON_MATCHING. With the C below the instruction stream matches except:
 *  1. func_8C071EC0 is called from three places. SHC keeps the address of an
 *     extern callee used three times in a callee-saved register (an extra
 *     R9 save shifts the whole allocation); the original loads it from the
 *     pool at every call, which is what SHC does for a callee defined in the
 *     same translation unit but out of BSR range. The original TU therefore
 *     also contains func_8C071EC0 (8C071EC0, ~19 KB further on). The
 *     NON_MATCHING SHC compile below emulates that with a stub in another
 *     section; shcdiff then reports those two pool loads as "data differs"
 *     (local symbol) although the code is the original's.
 *  2. the hoisted constants 0.8f and 0.5f get FR14/FR13 instead of FR13/FR14
 *     (9 instructions); this register choice reacts to unrelated changes
 *     anywhere in the function and was not reproduced.
 * Verify: py -3.12 tools/shcdiff.py PGetAcceleration src/1st_read/code_8C06D254.c
 *         --flags=-define=NON_MATCHING   (11 differing: items 1 and 2)
 */

void func_8C06EAC0(taskwk *twp, motionwk *mwp, playerwk *pwp);   /* event-locked player */
void func_8C06E4E0(taskwk *twp, motionwk *mwp, playerwk *pwp);   /* surface attr 0x4000 */
void func_8C0718C0(taskwk *twp, motionwk *mwp, playerwk *pwp);   /* no input on a steep slope */
void func_8C06F530(taskwk *twp, motionwk *mwp, playerwk *pwp);   /* twp->flag & 0x2000: sets mwp->ang_aim.y */
void func_8C071A20(taskwk *twp, motionwk *mwp, playerwk *pwp);   /* attr 0x800000 on a slope */
void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* turn while standing */
void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* turn while running */
void func_8C0720A0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* turn at high speed */

ASM_STUB(Sint32 func_8C06CD22(taskwk *twp, Angle *ang, Float *mag))
void func_8C06DCA2(taskwk *twp, motionwk *mwp, playerwk *pwp);

#ifdef NON_MATCHING
void PGetAcceleration(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR acc;
    NJS_VECTOR vd;
    NJS_VECTOR vn;
    Angle ang;
    Float mag;
    Sint32 analog;
    Sint32 diff;
    Float work;
    Float frict;
    Float over;

    if (twp->flag & 0x4000) {
        func_8C06EAC0(twp, mwp, pwp);
        return;
    } else if (pwp->attr & 0x20000) {
        func_8C06DCA2(twp, mwp, pwp);
        return;
    } else if (pwp->attr & 0x4000) {
        func_8C06E4E0(twp, mwp, pwp);
        return;
    }
    acc = mwp->acc;
    vd.x = vd.y = vd.z = 0.0f;
    /* FMAC sites: "acc += a * b" (SA1_FMAC's a * b + c order changes SHC's code) */
    SA1_FMAC_ACC(acc.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(acc.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(acc.z, Gravity.z, pwp->p.weight);
    PConvertVector_G2P(twp, &acc);
    analog = GetAnalog(twp, &ang, &mag);
    njOuterProduct(&pwp->cstsp->tnorm, &mwp->spd, &vn);

    if (pwp->dotp < 0.099999995f && _builtin_fabsf(vn.y) > 0.59999997f && pwp->spd.x > 1.16f) {
        acc.x = acc.y = acc.z = 0.0f;
        acc.y -= pwp->p.weight * 1.0f;
    } else if (pwp->dotp < -0.39999998f && pwp->spd.x > 1.16f) {
        acc.y -= pwp->p.weight * 5.0f;
    } else if (pwp->dotp < -0.29999999f && pwp->spd.x > 1.16f) {
        acc.y -= pwp->p.weight * 0.79999996f;
    } else if (pwp->dotp < -0.099999995f && pwp->spd.x > 1.16f) {
        acc.y -= pwp->p.weight * 0.39999998f;
    } else if (pwp->dotp < 0.5f && pwp->spd.x < pwp->p.run_speed && pwp->spd.x > -pwp->p.run_speed) {
        acc.x *= 4.225f;
        acc.z *= 4.225f;
    } else if (pwp->dotp < 0.7f && pwp->spd.x < pwp->p.run_speed && pwp->spd.x > -pwp->p.run_speed) {
        acc.x *= 1.0f;
        acc.z *= 2.0f;
    } else if (pwp->dotp < 0.86999995f && pwp->spd.x < pwp->p.jog_speed && pwp->spd.x > -pwp->p.run_speed) {
        acc.x *= 1.0f;
        acc.z *= 1.4f;
    }

    /* FMAC: acc += a * b; case 2 fuses (air_resist * (spd.x - max_x_spd)) * 1.7 */
    if (!analog && pwp->spd.x > pwp->p.run_speed) {
        SA1_FMAC_ACC(acc.x, pwp->p.air_resist, pwp->spd.x);
    } else if (analog && pwp->spd.x > pwp->p.max_x_spd && pwp->dotp > 0.96f) {
        SA1_FMAC_ACC(acc.x, pwp->p.air_resist * (pwp->spd.x - pwp->p.max_x_spd), 1.69999994f);
    } else if (pwp->spd.x > pwp->p.max_x_spd) {
        SA1_FMAC_ACC(acc.x, pwp->p.air_resist, pwp->spd.x - pwp->p.max_x_spd);
    } else if (pwp->spd.x < 0.0f) {
        SA1_FMAC_ACC(acc.x, pwp->p.air_resist, pwp->spd.x);
    }
    SA1_FMAC_ACC(acc.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(acc.z, pwp->p.air_resist_z, pwp->spd.z);

    if (!analog) {
        if (pwp->dotp < 0.71f) {
            func_8C0718C0(twp, mwp, pwp);
        } else if (pwp->spd.x > 0.0f) {
            vd.x = pwp->p.slow_down;
        } else if (pwp->spd.x < 0.0f) {
            vd.x = -pwp->p.slow_down;
        }
        mwp->rad = 0.0f;
    } else {
        if (twp->flag & 0x2000) {
            func_8C06F530(twp, mwp, pwp);
            ang = mwp->ang_aim.y;
            vd.y -= 0.79999996f;
        } else if (pwp->dotp < 0.94f && (pwp->attr & 0x800000)) {
            func_8C071A20(twp, mwp, pwp);
        }
        if (pwp->spd.x < pwp->p.max_x_spd) {
            if (pwp->spd.x < pwp->p.jog_speed) {
                if (mag > 0.5f) {
                    mag *= pwp->p.run_accel;
                } else if (pwp->spd.x < pwp->p.jog_speed * 0.39999998f) {
                    mag *= pwp->p.run_accel;
                } else {
                    mag = 0.0f;
                }
            } else if (pwp->spd.x < pwp->p.run_speed) {
                if (mag > 0.7f) {
                    mag *= pwp->p.run_accel;
                } else if (pwp->spd.x < (pwp->p.run_speed + pwp->p.run_speed) * 0.5f) {
                    mag *= pwp->p.run_accel;
                } else {
                    mag = 0.0f;
                }
            } else if (pwp->spd.x < pwp->p.rush_speed) {
                if (mag > 0.9f) {
                    mag *= pwp->p.run_accel;
                } else {
                    mag *= pwp->p.run_accel * 0.29999999f;
                }
            } else {
                mag *= pwp->p.run_accel;
            }
        } else if (pwp->spd.x >= pwp->p.max_x_spd && pwp->dotp < 0.0f) {
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
        } else if (pwp->spd.x >= pwp->p.dash_speed && (pwp->attr & 0x800000)) {
            mwp->ang_aim.y = ang;
            func_8C0720A0(twp, mwp, pwp, mwp->ang_aim.y);
        } else if (pwp->spd.x >= pwp->p.jog_speed && pwp->spd.x <= pwp->p.rush_speed && diff > 0x2000) {
            mwp->ang_aim.y = ang;
            mag *= 0.79999996f;
            func_8C071EC0(twp, mwp, pwp, mwp->ang_aim.y);
        } else {
            mwp->ang_aim.y = ang;
            func_8C071EC0(twp, mwp, pwp, mwp->ang_aim.y);
        }
        mwp->rad = mag;
        vd.x = mag;
    }

    /* forward (x) friction */
    if (pwp->dotp < 0.71f && pwp->spd.x < pwp->p.jog_speed && pwp->spd.x > -pwp->p.jog_speed && !analog) {
        acc.x = acc.x * 10.0f;
        acc.z *= 10.0f;
    } else if (pwp->spd.x == 0.0f) {
        frict = acc.y * pwp->p.lim_frict * mwp->weight;
        acc.x += vd.x;
        if (!analog && acc.x < frict && acc.x > -frict) {
            acc.x = 0.0f;
        }
        if (!analog && acc.x < 0.051f && acc.x > -0.051f) {
            acc.x = 0.0f;
        }
    } else if (vd.x < 0.0f) {
        if (pwp->attr & 4) {
            if (acc.y < 0.0f) {
                frict = acc.y * pwp->p.grd_frict * mwp->weight;
            } else {
                frict = 0.0f;
            }
            if (frict > 0.0f && -vd.x > frict) {
                work = vd.x + acc.x;
                over = work - frict;
                if (work < 0.0f && work < -frict) {
                    mwp->height = over;
                    work = -frict;
                } else if (work > 0.0f && work > frict) {
                    mwp->height = over;
                    work = frict;
                }
                acc.x = work;
            } else if (!analog && pwp->spd.x <= pwp->p.jog_speed && acc.x < 0.051f && acc.x > -0.051f) {
                acc.x = 0.0f;
            } else {
                acc.x += vd.x;
            }
        } else {
            if (pwp->spd.x > 0.0f) {
                work = acc.x + vd.x;
                if (work * vd.x < 0.0f) {
                    work = 0.0f;
                }
            } else if (!analog && pwp->spd.x <= pwp->p.jog_speed && acc.x < 0.051f && acc.x > -0.051f) {
                work = 0.0f;
            } else {
                work = acc.y * pwp->p.lim_frict * mwp->weight;
                work += vd.x;
                if (work >= 0.0f) {
                    work = 0.0f;
                }
            }
            acc.x = work;
        }
    } else {
        frict = acc.y * pwp->p.lim_frict * mwp->weight;
        if (pwp->spd.x < 0.0f && acc.x < 0.051f && acc.x > -0.051f) {
            acc.x += vd.x;
            if (acc.x * vd.x < 0.0f) {
                acc.x = 0.0f;
            }
        } else if (frict > 0.0f && vd.x > frict) {
            work = vd.x + acc.x;
            if (work > frict) {
                mwp->height = work - frict;
                work = frict;
            }
            acc.x = work;
        } else if (!analog && pwp->spd.x <= pwp->p.jog_speed && acc.x < 0.051f && acc.x > -0.051f) {
            acc.x = 0.0f;
        } else {
            acc.x += vd.x;
        }
    }

    /* sideways (z) friction */
    if (pwp->spd.z == 0.0f) {
        frict = acc.y * pwp->p.lim_frict * mwp->weight;
        if (acc.z < frict && acc.z > -frict) {
            acc.z = 0.0f;
        }
    } else {
        if (acc.y < 0.0f) {
            frict = acc.y * pwp->p.grd_frict_z * mwp->weight;
        } else {
            frict = 0.0f;
        }
        if (pwp->attr & 4) {
            work = vd.z + acc.z;
            if (work > 0.0f) {
                work -= frict;
                if (work < 0.0f) {
                    work = 0.0f;
                }
            } else if (work < 0.0f) {
                work += frict;
                if (work > 0.0f) {
                    work = 0.0f;
                }
            }
            acc.z = work;
        } else {
            if (acc.z > 0.0f) {
                vd.z -= frict;
            } else if (acc.z < 0.0f) {
                vd.z += frict;
            }
            work = acc.z + vd.z;
            if (acc.z != 0.0f && vd.z != 0.0f && work * acc.z < 0.0f) {
                work = 0.0f;
            }
            acc.z = work;
        }
    }
    acc.y += vd.y;
    pwp->acc = acc;
}
#else
ASM_STUB(void PGetAcceleration(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

ASM_STUB(void func_8C06DCA2(taskwk *twp, motionwk *mwp, playerwk *pwp))

#if defined(NON_MATCHING) && !defined(TARGET_PC)
/* Test aid only (see item 1 above): a same-TU func_8C071EC0 out of BSR
 * range, so SHC loads its address at each call like the original. */
#pragma section STUB
ASM_STUB(void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang))
#pragma section
#endif
