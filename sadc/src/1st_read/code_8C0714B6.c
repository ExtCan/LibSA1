#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C0714B6-8C071B26): gravity acceleration and the
 * turning helpers of PGetAcceleration and the character mains.
 *  func_8C0714B6  acc = motion acc + gravity * weight, in player space.
 *                 Running faster than run_speed with dotp < 0 multiplies the
 *                 vertical part by -8. On the ground (flag & 3) the analog
 *                 push csts.anaspdv is scaled by
 *                 weight * 5/7 / max_x_spd, converted and added to y/z, and
 *                 cleared. Air resistance per axis (with flag 0x100 and
 *                 dotp <= 0.98 the forward one is a fixed -0.0002 * spd.x).
 *                 Event-locked input (flag & 0x4000) leaves pwp->acc alone.
 *  func_8C071656  acc = motion acc + gravity * weight, in player space.
 *  func_8C071700  rotation from the stick: flag 0x2000 -> func_8C06F530;
 *                 no input: on the ground slide turn (func_8C0718C0), else
 *                 aim = facing; with input on a sliding surface
 *                 (attr 0x20000) the camera-relative stick direction is
 *                 rotated about the floor normal and the aim is the angle
 *                 between the player's forward axis and it; otherwise
 *                 aim = stick angle. Then func_8C071EC0 turns the player.
 *  func_8C0718C0  no-input slope turn: unless no-control time or attr
 *                 0x4000, or moving faster than jog_speed mostly against
 *                 gravity, on a slope (-0.87 < g.y < 0 in player space) the
 *                 aim turns downhill at |g.z| * 3072 (flag 0x100) or 1536
 *                 per frame; else aim = facing. Then func_8C071EC0.
 *  func_8C071A20  same for the stick-input path of PGetAcceleration: only
 *                 when faster than jog_speed and not moving up; the turn
 *                 speed is |g.z| * spd.x * 4096 / jog_speed (flag 0x100) or
 *                 |g.z| * 2048. Then func_8C072000.
 */

extern taskwk *D_8C18B7EC;      /* camera taskwk */

void func_8C06F530(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0718C0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang);
void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang);
void func_8C0720A0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang);

void func_8C0714B6(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR spd;
    NJS_VECTOR vec;
    csts *cp;
    Float r;

    spd = mwp->acc;
    if (twp->flag & 0x4000) {
        return;
    }
    SA1_FMAC_ACC(spd.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(spd.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(spd.z, Gravity.z, pwp->p.weight);
    PConvertVector_G2P(twp, &spd);
    if (pwp->spd.x > pwp->p.run_speed && pwp->dotp < -0.0f) {
        spd.y = -(spd.y * 8.0f);
    }
    if (twp->flag & 3) {
        cp = pwp->cstsp;
        r = pwp->p.max_x_spd / (pwp->p.weight * 0.71428568f);
        vec.x = cp->anaspdv.x / r;
        vec.y = cp->anaspdv.y / r;
        vec.z = cp->anaspdv.z / r;
        cp->anaspdv.y = 0.0f;
        cp->anaspdv.x = 0.0f;
        cp->anaspdv.z = 0.0f;
        PConvertVector_G2P(twp, &vec);
        spd.y += vec.y;
        spd.z += vec.z;
    }
    if (twp->flag & 0x100) {
        if (pwp->dotp > 0.97999997f) {
            SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x);
        } else {
            spd.x += -(pwp->spd.x * 0.0002f);
        }
    } else {
        SA1_FMAC_ACC(spd.x, pwp->p.air_resist, pwp->spd.x);
    }
    SA1_FMAC_ACC(spd.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(spd.z, pwp->p.air_resist_z, pwp->spd.z);
    pwp->acc = spd;
}

void func_8C071656(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR spd;

    spd = mwp->acc;
    if (twp->flag & 0x4000) {
        return;
    }
    SA1_FMAC_ACC(spd.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(spd.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(spd.z, Gravity.z, pwp->p.weight);
    PConvertVector_G2P(twp, &spd);
    pwp->acc = spd;
}

/*
 * NON_MATCHING: same instructions and stack frame; 9 scratch registers
 * differ (the ang_aim = facing store uses R3 for R2, the attr load R2 for R1,
 * and the second camera rotation block swaps R2/R3). The original keeps the
 * camera angle in one memory slot (@(8,R15)) shared by both rotations and
 * spills the stick-record pointer and the stick angle to one slot
 * (@(4,R15)); sibling block-scope structs reproduce the slots (a function
 * scope struct takes a callee-saved base register, a volatile scalar moves
 * to @(0,R15)); a volatile struct gives the same 9 differences. Same idiom
 * as func_8C06DCA2 and func_8C057460 (code_8C057460.c notes the same R2/R3
 * swap with volatile).
 */
#ifdef NON_MATCHING
void func_8C071700(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v1;
    NJS_VECTOR vs;
    NJS_VECTOR v2;
    Angle rot;
    Angle ang;
    Sint32 diff;

    if (twp->flag & 0x4000) {
        return;
    }
    if (twp->flag & 0x2000) {
        func_8C06F530(twp, mwp, pwp);
    } else if (GetAnalog(twp, &ang, NULL) == 0) {
        if (twp->flag & 3) {
            /* both arms are the same call; only the FCMP/GT is left */
            if (pwp->spd.x > 2.0f) {
                func_8C0718C0(twp, mwp, pwp);
            } else {
                func_8C0718C0(twp, mwp, pwp);
            }
            return;
        }
        mwp->ang_aim.y = twp->ang.y;
    } else if (pwp->attr & 0x20000) {
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
    } else {
        mwp->ang_aim.y = ang;
    }
    func_8C071EC0(twp, mwp, pwp, mwp->ang_aim.y);
}
#else
ASM_STUB(void func_8C071700(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C0718C0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR g;
    NJS_VECTOR v;
    Angle target;
    Angle rspd;

    if (pwp->nocontimer != 0) {
        return;
    }
    if (pwp->attr & 0x4000) {
        return;
    }
    g = Gravity;
    v = mwp->spd;
    if (njScalor(&v) > pwp->p.jog_speed) {
        njUnitVector(&v);
        if (njInnerProduct(&Gravity, &v) < -0.85999996f) {
            return;
        }
    }
    PConvertVector_G2P(twp, &g);
    if (g.y < 0.0f && g.y > -0.86999995f) {
        if (g.x < 0.0f) {
            g.x = -g.x;
        }
        target = twp->ang.y + SA1_FTOI(atan2f(g.z, g.x) * 10430.381f);
        if (g.z < 0.0f) {
            g.z = -g.z;
        }
        if (twp->flag & 0x100) {
            rspd = SA1_FTOI(g.z * 3072.0f);
        } else {
            rspd = SA1_FTOI(g.z * 1536.0f);
        }
        mwp->ang_aim.y = AdjustAngle(mwp->ang_aim.y, target, rspd);
    } else {
        mwp->ang_aim.y = twp->ang.y;
    }
    func_8C071EC0(twp, mwp, pwp, mwp->ang_aim.y);
}

void func_8C071A20(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR g;
    NJS_VECTOR v;
    Angle target;
    Angle rspd;

    if (pwp->nocontimer != 0) {
        return;
    }
    g = Gravity;
    v = mwp->spd;
    if (njScalor(&v) > pwp->p.jog_speed) {
        njUnitVector(&v);
        if (njInnerProduct(&Gravity, &v) < -0.85999996f) {
            return;
        }
    } else {
        return;
    }
    PConvertVector_G2P(twp, &g);
    if (g.y > -0.86999995f) {
        if (g.x < 0.0f) {
            g.x = -g.x;
        }
        target = twp->ang.y + SA1_FTOI(atan2f(g.z, g.x) * 10430.381f);
        if (g.z < 0.0f) {
            g.z = -g.z;
        }
        if (twp->flag & 0x100) {
            rspd = SA1_FTOI(g.z * pwp->spd.x * 4096.0f / pwp->p.jog_speed);
        } else {
            rspd = SA1_FTOI(g.z * 2048.0f);
        }
        mwp->ang_aim.y = AdjustAngle(mwp->ang_aim.y, target, rspd);
    } else {
        mwp->ang_aim.y = twp->ang.y;
    }
    func_8C072000(twp, mwp, pwp, mwp->ang_aim.y);
}

ASM_STUB(void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang))
ASM_STUB(void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang))
ASM_STUB(void func_8C0720A0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang))
