#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C074CF0-8C075162), helpers of the PSetPosition
 * core 8C0730D8 (also called by 8C0742D4, 8C0777E0, 8C0771C4, 8C078856):
 *  func_8C074CF0  gravity step: v->x = v->z = 0, v->y -= g * k, with
 *                 g = p.pos_error and k chosen by the surface (last_attr
 *                 0x4000 / 0x20000 / 8), the slope (-gravity . csts.tnorm),
 *                 the speed and pwp->flag (0x2000, 3); nothing when the
 *                 player moves up (spd.y > 0) without flag 0x2000
 *  func_8C074EE4  tilt the player (ang.x/ang.z) towards the floor angles
 *                 csts.angx/angz, limited by the speed class
 *  func_8C07506E  moving ground: csts.pos = twp->pos + off (standing still),
 *                 run the force work callback of the object stood on
 *                 (pwp->ttp->fwp[pno > 0]) at that position, then add its
 *                 pos_spd/ang_spd
 *
 * Matching notes: in 8C074CF0 the zero stores are one chained assignment
 * (0.0f then lives in FR13, 0.3f in FR14); the last_attr & 8 case tests
 * flag & 3 with both arms identical (SHC keeps the dead TST #3 at 8C074D84);
 * 8C074EE4 returns early after copying the floor angles (an if/else gives
 * the struct copy's address registers swapped).
 */

/*
 * Force work (task.fwp): CreateElementalTask allocates CAlloc(2, 0x1C) for
 * im & 4, one per player side (fwp[0] player 0, fwp[1] the others).
 * Layout from the SADX analogy (forcewk); not in the shared headers (same
 * local definition as code_8C0988E4.c and code_8C056A04.c). DC layout:
 * call_back 0x00, ang_spd 0x04, pos_spd 0x10, size 0x1C (asserted below).
 */
typedef struct forcewk {
    void (*call_back)(task *tp, taskwk *twp, struct forcewk *fwp);
    Angle3 ang_spd;             /* added to twp->ang (16-bit wrap) */
    NJS_POINT3 pos_spd;         /* added to csts.pos */
} forcewk;

SA1_ASSERT_SIZE(forcewk, 0x1C);
SA1_ASSERT_OFFSET(forcewk, ang_spd, 0x04);
SA1_ASSERT_OFFSET(forcewk, pos_spd, 0x10);

/*
 * The previous function of the original translation unit (decompiled in
 * code_8C0747D2.c). Its pending literal-pool entry (_func_8C076640) made SHC
 * dump the shared pool early, after 8C074DC4 BRA inside func_8C074CF0; without
 * a preceding function the pool lands between 8C074E32 BF and its target and
 * SHC emits BT/BRA instead. The splicer keeps the original assembly for it.
 */
ASM_STUB(Sint32 func_8C074AFC(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp,
                              NJS_POINT3 *pos, NJS_POINT3 *ofs, Sint32 ret, Uint32 attr))

void func_8C074CF0(motionwk *mwp, playerwk *pwp, csts *cp, NJS_POINT3 *v)
{
    Float g;
    Float d;
    NJS_VECTOR n;

    g = pwp->p.pos_error;
    v->x = v->z = 0.0f;
    if ((pwp->last_attr & 0x4000) && mwp->spd.y <= 0.0f) {
        v->y -= g * 0.59999998f;
    } else if ((pwp->last_attr & 0x4000) && mwp->spd.y > 0.0f) {
        v->y -= g * 0.19999999f;
    } else if (pwp->last_attr & 0x20000) {
        v->y -= g;
    } else if (pwp->spd.y <= 0.0f) {
        if (pwp->last_attr & 8) {
            if (pwp->flag & 3) {
                g *= 0.29999999f;
            } else {
                g *= 0.29999999f;
            }
        } else {
            d = -njInnerProduct(&Gravity, &cp->tnorm);
            if (pwp->flag & 0x2000) {
                if (d <= -0.59999998f) {
                    g *= 1.8f;
                } else if (d <= 0.049999998f) {
                    g *= 1.65f;
                } else {
                    g *= 1.4f;
                }
            } else if (pwp->spd.x < pwp->p.dash_speed || !(pwp->flag & 3)) {
                if (d <= -0.099999996f) {
                } else if (d > -0.099999996f && d < 0.19999999f) {
                    n = mwp->spd;
                    if (njScalor(&n) == 0.0f) {
                        n.y = 0.0f;
                    } else {
                        njUnitVector(&n);
                    }
                    if (_builtin_fabsf(n.y) < 0.25f) {
                        g *= 1.5f;
                    } else {
                        g *= 0.29999999f;
                    }
                } else if (pwp->spd.x <= pwp->p.run_speed) {
                    g *= 0.5f;
                } else if (pwp->flag & 3) {
                    g *= pwp->spd.x / pwp->p.dash_speed;
                }
            }
        }
        v->y -= g;
    } else {
        if (pwp->flag & 0x2000) {
            v->y -= g;
        }
    }
}

void func_8C074EE4(taskwk *twp, playerwk *pwp, csts *cp)
{
    Float z, x;
    NJS_POINT3 v;
    Angle spd;

    if (njInnerProduct(&Gravity, &cp->tnorm) > njInnerProduct(&Gravity, &cp->normal2) || !(pwp->flag & 1) || (pwp->flag & 0x2000)) {
        twp->ang.x = cp->angx;
        twp->ang.z = cp->angz;
        return;
    }
    v = cp->normal2;
    PConvertVector_G2P(twp, &v);
    x = v.x;
    z = v.z;
    v.x = njSqrt(SA1_FMAC(z, z, x * x));
    v.z = 0.0f;
    spd = SA1_FTOI(atan2f(v.x, v.y) * 10430.381f) & 0xFFFF;
    if (spd > 0x8000) {
        spd = 0x10000 - spd;
    }
    if (!(twp->flag & 0x2000) && pwp->spd.x < pwp->p.dash_speed) {
        if (pwp->spd.x >= pwp->p.rush_speed) {
            if (spd > 0x2000) {
                spd = 0x2000;
            }
        } else if (pwp->spd.x >= pwp->p.run_speed) {
            if (spd > 0x1500) {
                spd = 0x1500;
            }
        } else if (pwp->spd.x >= pwp->p.jog_speed) {
            if (spd > 0x1000) {
                spd = 0x1000;
            }
        } else {
            spd = 0x400;
        }
    }
    twp->ang.x = AdjustAngle(twp->ang.x, cp->angx, spd);
    twp->ang.z = AdjustAngle(twp->ang.z, cp->angz, spd);
}

void func_8C07506E(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *off)
{
    forcewk *fwp;
    NJS_POINT3 pos;

    fwp = pwp->ttp->fwp;
    if (fwp != NULL) {
        if (twp->counter.b[0]) {
            fwp++;
        }
        if (njScalor(&pwp->spd) == 0.0f) {
            cp->pos.x = twp->pos.x + off->x;
            cp->pos.y = twp->pos.y + off->y;
            cp->pos.z = twp->pos.z + off->z;
        }
        if (fwp->call_back) {
            pos = twp->pos;
            twp->pos = cp->pos;
            fwp->call_back(playertp[(Uint8)twp->counter.b[0]], twp, fwp);
            twp->pos = pos;
        }
        cp->pos.x += fwp->pos_spd.x;
        cp->pos.y += fwp->pos_spd.y;
        cp->pos.z += fwp->pos_spd.z;
        twp->ang.x = (Uint16)(twp->ang.x + fwp->ang_spd.x);
        twp->ang.y = (Uint16)(twp->ang.y + fwp->ang_spd.y);
        twp->ang.z = (Uint16)(twp->ang.z + fwp->ang_spd.z);
    }
}
