#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C07095E), called by SonicMain 8C0A77BC and
 * TailsMain 8C0B6148: acceleration of a player riding a board (Sonic's
 * snowboard / Tails' sand board). Same skeleton as the neighbouring
 * acceleration helpers (8C07061C, 8C070EDA): motion acc minus p.weight on
 * the global Y axis, converted to player space, air_resist damping (1.5x /
 * 2x on Z, 1.5x on X when sliding backwards), then
 *  - the board heading follows the horizontal player-space speed (turn at
 *    most 0x200 per frame);
 *  - no stick input: turn (0x400) towards the downhill direction (the
 *    player's local down vector seen from above) and blend the speed
 *    0.8 old / 0.2 new;
 *  - stick input: the stick magnitude (x1.5 below 2.76, x0.1 above) is kept
 *    in mwp->rad and leans the speed vector: rotate it by -(mag * 1024) on Y
 *    and back by the slope angles, blend 0.5 / 0.5; the forward
 *    acceleration is -run_accel * mag (0.4x above max_x_spd), i.e. braking;
 *  - X/Z friction tail (lim_frict, grd_frict_z) like the ground code.
 * Result in pwp->acc. Event-locked input (twp->flag & 0x4000) leaves
 * pwp->acc unchanged.
 */

extern taskwk *D_8C18B7EC;      /* camera taskwk (pos read by code_8C010400.c, ang by code_8C0869E0.c) */

#ifdef NON_MATCHING
/*
 * Complete and functionally faithful. With the three calls marked (*)
 * aimed at differently named externs, SHC 5.1r03 produces this function
 * instruction for instruction (585/585) except for an R2/R3 swap in the
 * njRotateX/njRotateZ block and the two pool loads after it. What is left
 * is a compiler-internal difference: SHC 5.1r03 keeps the addresses of
 * PConvertVector_G2P (4 calls) and PConvertVector_P2G (3 calls) in callee
 * saved registers (R10/R9, "JSR @R10"), while the original reloads them
 * from the literal pool before every call and uses only R10-R14 (R10 for
 * the Y rotation angle). No source form tried (declarations, casts,
 * inline helpers, block scopes, volatile, register, -extra bits, other SHC
 * versions) suppresses that CSE; the same missing CSE is seen in
 * 8C08AC2C (code_8C08ABC0.c). Matching notes for whoever retries:
 *  - the rotation angle temporary is a memory variable (stored to sp+4 at
 *    every def, the call argument forwarded from the register):
 *    "(void)&tmp" at the top reproduces that and, through SHC's slot
 *    ordering (reference count, ties broken by first reference), the frame
 *    layout mag @0, tmp @4, ang @8, r @12;
 *  - the 0.5/0.5 blend needs k / k2 = 1 - k variables (dead copy
 *    "FMOV FR5,FR4"), the 0.8/0.2 blend plain literals (0.2 = 1 - 0.8
 *    folded, 0x3E4CCCD0);
 *  - operand order "air_resist * spd" puts spd in FR0 for FMAC.
 */
void func_8C07095E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR acc;
    NJS_VECTOR vec;
    NJS_VECTOR v;
    NJS_VECTOR v2;
    Sint32 r;
    Angle ang;
    Angle tmp;
    Float mag;
    Angle roty;
    Float s;
    Float lim;
    Float k;
    Float k2;

    (void)&tmp;
    acc = mwp->acc;
    vec.z = 0.0f;
    vec.y = 0.0f;
    vec.x = 0.0f;
    if (twp->flag & 0x4000) {
        return;
    }
    acc.y += -pwp->p.weight;
    PConvertVector_G2P(twp, &acc);
    r = GetAnalog(twp, &ang, &mag);
    /* all five are FMAC in the original (the 1.5 / 2.0 factor is the FR0
       operand of the fused multiply, air_resist * spd an FMUL before it) */
    if (pwp->spd.x > 8.279999f) {
        SA1_FMAC_ACC(acc.x, pwp->p.air_resist, pwp->spd.x);
        SA1_FMAC_ACC(acc.z, pwp->p.air_resist * pwp->spd.z, 1.5f);
    } else if (pwp->spd.x < 0.0f) {
        SA1_FMAC_ACC(acc.x, pwp->p.air_resist * pwp->spd.x, 1.5f);
        SA1_FMAC_ACC(acc.z, pwp->p.air_resist * pwp->spd.z, 2.0f);
    }
    SA1_FMAC_ACC(acc.y, pwp->p.air_resist_y, pwp->spd.y);
    /* downhill heading: the player's down vector seen from above */
    v.x = v.z = 0.0f;
    v.y = -1.0f;
    PConvertVector_G2P(twp, &v);
    tmp = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f) + twp->ang.y;
    /* turn the board towards its own horizontal speed */
    v = pwp->spd;
    PConvertVector_P2G(twp, &v);
    mwp->ang_aim.y = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f) & 0xFFFF;
    twp->ang.y = AdjustAngle(twp->ang.y, mwp->ang_aim.y, 0x200);
    PConvertVector_G2P(twp, &v);                /* (*) */
    pwp->spd = v;
    if (r == 0) {
        v = pwp->spd;
        PConvertVector_P2G(twp, &v);
        mwp->ang_aim.y = tmp;
        twp->ang.y = AdjustAngle(twp->ang.y, mwp->ang_aim.y, 0x400);
        PConvertVector_G2P(twp, &v);            /* (*) */
        pwp->spd.x = SA1_FMAC(pwp->spd.x, 0.79999996f, v.x * 0.20000005f);
        pwp->spd.y = SA1_FMAC(pwp->spd.y, 0.79999996f, v.y * 0.20000005f);
        pwp->spd.z = SA1_FMAC(pwp->spd.z, 0.79999996f, v.z * 0.20000005f);
    } else {
        SubAngle(0xC000 - D_8C18B7EC->ang.y, ang);  /* result unused in the original */
        if (pwp->spd.x < 2.7599998f) {
            s = mag * 1.5f;
        } else {
            s = mag * 0.099999996f;
        }
        mwp->rad = s;
        v = pwp->spd;
        PConvertVector_P2G(twp, &v);            /* (*) */
        roty = twp->ang.y - SA1_FTOI(s * 1024.0f);
        v2.x = v.x;
        v2.y = v.y;
        v2.z = v.z;
        func_8C057B40();
        if (roty) {
            njRotateY(NULL, roty);
        }
        if ((tmp = -twp->ang.x) != 0) {
            njRotateX(NULL, tmp);
        }
        if ((tmp = -twp->ang.z) != 0) {
            njRotateZ(NULL, tmp);
        }
        njCalcVector(NULL, &v2, &v);
        njPopMatrix(1);
        k = 0.5f;
        k2 = 1.0f - k;
        pwp->spd.x = SA1_FMAC(pwp->spd.x, k, v.x * k2);
        pwp->spd.y = SA1_FMAC(pwp->spd.y, k, v.y * k2);
        pwp->spd.z = SA1_FMAC(pwp->spd.z, k, v.z * k2);
        if (pwp->spd.x < pwp->p.max_x_spd) {
            mag *= -pwp->p.run_accel;
        } else {
            mag *= -(pwp->p.run_accel * 0.39999998f);
        }
        vec.x = mag;
    }
    if (vec.x < 0.0f) {
        if (pwp->spd.x > 0.0f) {
            s = acc.x + vec.x;
            if (s * vec.x < 0.0f) {
                s = 0.0f;
            }
        } else {
            if (acc.y < 0.0f) {
                lim = acc.y * pwp->p.lim_frict * mwp->weight;
            } else {
                lim = 0.0f;
            }
            s = lim + vec.x;
            if (!(s < 0.0f)) {
                s = 0.0f;
            }
        }
        acc.x = s;
    } else {
        if (acc.y < 0.0f) {
            lim = acc.y * pwp->p.lim_frict * mwp->weight;
        } else {
            lim = 0.0f;
        }
        acc.x += vec.x;
        if (acc.x > 0.0f) {
            if (acc.x < lim) {
                acc.x = 0.0f;
            } else {
                acc.x -= lim;
            }
        } else if (acc.x < 0.0f) {
            if (acc.x > -lim) {
                acc.x = 0.0f;
            } else {
                acc.x += lim;
            }
        }
    }
    if (pwp->spd.z == 0.0f) {
        lim = acc.y * pwp->p.lim_frict * mwp->weight;
        if (lim < 0.0f) {
            lim = 0.0f;
        }
        acc.z += vec.z;
        if (acc.z < lim && acc.z > -lim) {
            acc.z = 0.0f;
        }
    } else {
        lim = acc.y * pwp->p.grd_frict_z * mwp->weight;
        if (lim < 0.0f) {
            lim = 0.0f;
        }
        acc.z += vec.z;
        if (acc.z > 0.0f) {
            if (acc.z < lim) {
                acc.z = 0.0f;
            } else {
                acc.z -= lim;
            }
        } else if (acc.z < 0.0f) {
            if (acc.z > -lim) {
                acc.z = 0.0f;
            } else {
                acc.z += lim;
            }
        }
    }
    pwp->acc = acc;
}
#else
ASM_STUB(void func_8C07095E(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
