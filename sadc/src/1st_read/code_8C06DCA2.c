#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06DCA2-8C06E4A2): acceleration on a sliding
 * floor. Only caller: PGetAcceleration (8C06D254) when pwp->attr & 0x20000.
 *  func_8C06DCA2  acceleration = motion acc - floor normal * weight (in
 *                 player space), air resistance per axis, the stick stroke
 *                 turned into forward acceleration (kept in mwp->rad) and a
 *                 turn towards the stick direction rotated into the floor
 *                 plane (camera yaw/pitch, then around the floor normal),
 *                 then floor friction (attr & 4: frictionless floor); result
 *                 in pwp->acc. Twin of func_8C06E4E0 (normal floor) and of
 *                 the slide branch of func_8C071700 (code_8C0714B6.c).
 */

extern taskwk *D_8C18B7EC;      /* camera taskwk */

void func_8C06F530(taskwk *twp, motionwk *mwp, playerwk *pwp);           /* flag 0x2000 turn (code_8C06F530.c) */
void func_8C0718C0(taskwk *twp, motionwk *mwp, playerwk *pwp);           /* no-input slope turn (code_8C0714B6.c) */
void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleYQ-like (turn on the spot) */
void func_8C0720A0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleYS-like */

/*
 * NON_MATCHING (SHC 5.1r03): instruction-identical and same stack frame
 * except for two things that do not come from this function's source:
 *  - three branches are emitted short (BF) where the original has the long
 *    form (BT; BRA; NOP): 8C06DEA6, 8C06E1F4, 8C06E1FC. SHC places literal
 *    pools by counting from the last pool of the translation unit; in the
 *    original TU the count starts in PGetAcceleration (pool 8C06DBC0, then
 *    0xC2 bytes of pool-free code), so the first pool lands at 8C06DD4E and
 *    the later pools between those branches and their targets. Verified:
 *    with 0x11A..0x23A bytes of literal-free code in front of this function
 *    in the same file, all six pools land as in the original.
 *  - the second camera rotation (8C06E07E) uses R3/R2 where the original
 *    uses R2/R3 (same R2/R3 swap as func_8C071700 in code_8C0714B6.c and
 *    func_8C057460). The camera angle lives in a 4-byte stack slot
 *    (@(12,R15)) in the original; sibling block-scope structs reproduce the
 *    slot (an unsigned scalar does too, with the same swap).
 * Matching idioms found: the 0.4 / 0.5 factors are locals (SHC keeps them in
 * FR5 / FR4; plain constants are reloaded at each use); `(x + x) * half` is
 * only CSE'd outside an || operand, so the run-speed test is an else-if;
 * `spd.x += a * b` (FMAC) evaluates b first, SA1_FMAC(a, b, spd.x) loads
 * spd.x early; the B_8C754C08 record address is a CSE temp spilled to
 * @(8,R15), sharing the slot with `rot`.
 */
#ifdef NON_MATCHING
void func_8C06DCA2(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR spd;
    NJS_VECTOR vec;
    NJS_VECTOR v1;
    NJS_VECTOR vs;
    NJS_VECTOR v2;
    Angle rot;
    Angle ang;
    Float mag;
    Sint32 analog;
    Sint32 diff;
    Float slow;                 /* 0.4: low-speed / over-speed factor (FR5) */
    Float half;                 /* 0.5 (FR4) */
    Float lim;
    Float s;
    Float excess;

    spd = mwp->acc;
    vec.z = 0.0f;
    vec.y = 0.0f;
    vec.x = 0.0f;
    spd.x -= pwp->floor_normal.x * pwp->p.weight;
    spd.y -= pwp->floor_normal.y * pwp->p.weight;
    spd.z -= pwp->floor_normal.z * pwp->p.weight;
    PConvertVector_G2P(twp, &spd);
    analog = GetAnalog(twp, &ang, &mag);
    /* air resistance (SHC emits FMAC for `x += a * b`; the first three
     * spd.x statements share one FMAC at 8C06DDE0 by tail merging, hence
     * 4 FMACs in the original for 6 fused statements) */
    if (analog == 0 && pwp->spd.x > pwp->p.run_speed) {
        SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x);
    } else if (analog != 0 && pwp->spd.x > pwp->p.max_x_spd && pwp->dotp > 0.96f) {
        SA1_FMAC_ACC(spd.x, 1.69999996f, pwp->p.air_resist * (pwp->spd.x - pwp->p.max_x_spd));
    } else if (pwp->spd.x > pwp->p.max_x_spd) {
        SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x - pwp->p.max_x_spd);
    } else if (pwp->spd.x < 0.0f) {
        SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x);
    }
    SA1_FMAC_ACC(spd.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(spd.z, pwp->p.air_resist_z, pwp->spd.z);
    if (analog == 0) {
        if (pwp->dotp < 0.71f) {
            func_8C0718C0(twp, mwp, pwp);
        } else if (pwp->spd.x > 0.0f) {
            vec.x = pwp->p.slow_down;
        } else if (pwp->spd.x < 0.0f) {
            vec.x = -pwp->p.slow_down;
        }
        mwp->rad = 0.0f;
    } else {
        if (twp->flag & 0x2000) {
            func_8C06F530(twp, mwp, pwp);
            ang = mwp->ang_aim.y;
            vec.y -= 0.79999997f;
        }
        slow = 0.39999998f;
        if (pwp->spd.x < pwp->p.max_x_spd) {
            half = 0.5f;
            if (pwp->spd.x < pwp->p.jog_speed) {
                if (mag > half || pwp->spd.x < pwp->p.jog_speed * slow) {
                    mag *= pwp->p.run_accel;
                } else {
                    mag = 0.0f;
                }
            } else if (pwp->spd.x < pwp->p.run_speed) {
                if (mag > 0.7f) {
                    mag *= pwp->p.run_accel;
                } else if (pwp->spd.x < (pwp->p.run_speed + pwp->p.run_speed) * half) {
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
        } else {
            if (pwp->spd.x >= pwp->p.max_x_spd && pwp->dotp < 0.0f) {
                mag *= pwp->p.run_accel;
            } else {
                mag *= pwp->p.run_accel * slow;
            }
        }
        diff = DiffAngle(twp->ang.y, ang);
        if (pwp->spd.x == 0.0f && diff > 0x1000) {
            mwp->ang_aim.y = ang;
            mag = 0.0f;
            func_8C072000(twp, mwp, pwp, mwp->ang_aim.y);
        } else {
            /* stick direction (raw pad record), turned by the camera and
             * then around the floor normal */
            rot = SA1_FTOI(atan2f((Float)(B_8C754C08[(Uint16)(Uint8)twp->counter.b[0]].x1 << 8),
                                  (Float)(B_8C754C08[(Uint16)(Uint8)twp->counter.b[0]].y1 << 8)) * 10430.381f);
            vs.x = 0.0f;
            vs.y = 0.0f;
            vs.z = -1.0f;
            func_8C057B40();
            {
                struct { Angle a; } t;
                if ((t.a = D_8C18B7EC->ang.y) != 0) {
                    njRotateY(NULL, t.a);
                }
            }
            {
                struct { Angle a; } t;
                if ((t.a = D_8C18B7EC->ang.x) != 0) {
                    njRotateX(NULL, t.a);
                }
            }
            njCalcVector(NULL, &vs, &v1);
            njPopMatrix(1);
            func_8C057B40();
            njRotate(NULL, &pwp->floor_normal, rot);
            njCalcVector(NULL, &v1, &v2);
            njPopMatrix(1);
            vs.x = 1.0f;
            vs.y = 0.0f;
            vs.z = 0.0f;
            PConvertVector_P2G(twp, &vs);
            diff = VectorAngle(&vs, &v2, &pwp->floor_normal);
            mwp->ang_aim.y = (Uint16)(twp->ang.y + diff);
            func_8C0720A0(twp, mwp, pwp, mwp->ang_aim.y);
        }
        mwp->rad = mag;         /* forward acceleration of this frame */
        vec.x = mag;
    }
    /* floor friction; mwp->height receives the part of the push that the
     * frictionless-floor clamp removed */
    if (pwp->spd.x == 0.0f) {
        lim = spd.y * pwp->p.lim_frict * mwp->weight;
        spd.x += vec.x;
        if (analog == 0 && lim > spd.x && spd.x > -lim) {
            spd.x = 0.0f;
        }
        if (analog == 0 && spd.x < 0.051f && spd.x > -0.051f) {
            spd.x = 0.0f;
        }
    } else if (vec.x < 0.0f) {
        if (pwp->attr & 4) {
            if (spd.y < 0.0f) {
                lim = spd.y * pwp->p.grd_frict * mwp->weight;
            } else {
                lim = 0.0f;
            }
            if (lim > 0.0f && -vec.x > lim) {
                s = vec.x + spd.x;
                excess = s - lim;
                if (s < 0.0f && s < -lim) {
                    mwp->height = excess;
                    s = -lim;
                } else if (s > 0.0f && s > lim) {
                    mwp->height = excess;
                    s = lim;
                }
                spd.x = s;
            } else if (analog == 0 && pwp->spd.x <= pwp->p.jog_speed && spd.x < 0.051f && spd.x > -0.051f) {
                spd.x = 0.0f;
            } else {
                spd.x += vec.x;
            }
        } else {
            if (pwp->spd.x > 0.0f) {
                s = spd.x + vec.x;
                if (s * vec.x < 0.0f) {
                    s = 0.0f;
                }
            } else if (analog == 0 && pwp->spd.x <= pwp->p.jog_speed && spd.x < 0.051f && spd.x > -0.051f) {
                s = 0.0f;
            } else {
                lim = spd.y * pwp->p.lim_frict * mwp->weight;
                s = lim + vec.x;
                if (!(s < 0.0f)) {
                    s = 0.0f;
                }
            }
            spd.x = s;
        }
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
        } else if (analog == 0 && pwp->spd.x <= pwp->p.jog_speed && spd.x < 0.051f && spd.x > -0.051f) {
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
            lim = spd.y * pwp->p.grd_frict_z * mwp->weight;
        } else {
            lim = 0.0f;
        }
        if (pwp->attr & 4) {
            s = vec.z + spd.z;
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
            spd.z = s;
        } else {
            if (spd.z > 0.0f) {
                vec.z -= lim;
            } else if (spd.z < 0.0f) {
                vec.z += lim;
            }
            s = spd.z + vec.z;
            if (spd.z != 0.0f && vec.z != 0.0f && s * spd.z < 0.0f) {
                s = 0.0f;
            }
            spd.z = s;
        }
    }
    spd.y += vec.y;
    pwp->acc = spd;
}
#else
ASM_STUB(void func_8C06DCA2(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
