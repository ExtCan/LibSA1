#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C071B26-8C072174): airborne motion and turning
 * helpers called by the character mains and the acceleration code.
 *  func_8C071B26  airborne motion (skipped while pwp->nocontimer runs):
 *                 gravity, projected onto the floor plane when twp->flag & 1,
 *                 times p.weight * 0.4 plus mwp->acc into mwp->spd; air_resist
 *                 damping; player-space copy in pwp->spd; pwp->eff added;
 *                 func_8C0723E0 / func_8C072510; heading (twp->ang.y and
 *                 mwp->ang_aim.y) from the horizontal world speed.
 *  func_8C071D40  rotate twp->ang.x/z towards the user gravity angles
 *                 (B_8C754BE8/BEC) keeping the player-space speed, unless the
 *                 speed exceeds p.dash_speed; equipment bit 0x8000 (SADX
 *                 Upgrades_SuperSonic) rotates at any speed.
 *  func_8C071DD8  same towards (0, 0xC000), no equipment override.
 *  func_8C071EC0  PAdjustAngleY-like: turn twp->ang.y towards ang (0x800 per
 *                 frame above 0x2000, else diff / 4 or diff / 8) keeping the
 *                 world speed, then blend the old player-space speed with the
 *                 rotated one (factor 0.1 in the air, 0.05 / 0.99 / 0.5
 *                 scaled by mwp->weight on the ground).
 *  func_8C072000  PAdjustAngleYQ-like: turn by up to 0x2000, keep the speed.
 *  func_8C0720A0  PAdjustAngleYS-like: turn by 0x100, less above dash_speed,
 *                 then blend like func_8C071EC0 (0.99 / 0.5, no weight).
 *
 * Matching notes: each blend branch of func_8C071EC0 is written out (SHC
 * cross-jumps the identical tails); in func_8C0720A0 the blend is shared and
 * "s = 1.0f - r" sits in each branch. "spd -= ..." (not an FTRC wrapper) is
 * needed so that SHC keeps spd = 0x100 in a register; the value cannot leave
 * the Sint32 range for finite speeds. "+ v.z * v.z" is an FMAC whose product
 * comes second, which SA1_FMAC cannot express without changing the output
 * (FMAC_ADD below).
 */

void func_8C0723E0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C072510(taskwk *twp, motionwk *mwp);

/*
 * c + a * b where the original has FMAC but SHC needs the product second
 * (func_8C071D40 / func_8C071DD8: FR3 = y*y; FMAC x*x; FMAC z*z). Neither
 * SA1_FMAC nor SA1_FMAC_ACC keeps those functions matching.
 */
#if defined(TARGET_PC)
#define FMAC_ADD(c, a, b) SA1_FMAC((a), (b), (c))
#else
#define FMAC_ADD(c, a, b) ((c) + (a) * (b))
#endif

void func_8C071B26(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    NJS_LINE line;
    Angle ang;

    if (pwp->nocontimer != 0) {
        return;
    }
    v = Gravity;
    if (twp->flag & 1) {
        line.px = line.py = line.pz = 0.0f;
        line.vx = pwp->floor_normal.x;
        line.vy = pwp->floor_normal.y;
        line.vz = pwp->floor_normal.z;
        njDistanceP2L(&Gravity, &line, &v);
        if (njScalor(&v) != 0.0f) {
            njUnitVector(&v);
        } else {
            v = Gravity;
        }
    }
    v.x *= pwp->p.weight * 0.39999998f;
    v.y *= pwp->p.weight * 0.39999998f;
    v.z *= pwp->p.weight * 0.39999998f;
    njAddVector(&v, &mwp->acc);
    njAddVector(&mwp->spd, &v);
    v = mwp->spd;
    v.x *= pwp->p.air_resist;
    v.y *= pwp->p.air_resist;
    v.z *= pwp->p.air_resist;
    njAddVector(&mwp->spd, &v);
    v = mwp->spd;
    PConvertVector_G2P(twp, &v);
    pwp->spd = v;
    njAddVector(&mwp->spd, &pwp->eff);
    func_8C0723E0(twp, mwp, pwp);
    func_8C072510(twp, mwp);
    v = mwp->spd;
    if (njScalor(&v) != 0.0f) {
        njUnitVector(&v);
        ang = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f);
        mwp->ang_aim.y = ang;
        twp->ang.y = ang;
    }
}

void func_8C071D40(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    Float sq;

    v = pwp->spd;
    if (!((Sint16)pwp->equipment & 0x8000)) {
        sq = FMAC_ADD(SA1_FMAC(v.x, v.x, v.y * v.y), v.z, v.z);
        if (sq > pwp->p.dash_speed * pwp->p.dash_speed) {
            return;
        }
    }
    PConvertVector_P2G(twp, &v);
    twp->ang.x = AdjustAngle(twp->ang.x, B_8C754BE8, 0x800);
    twp->ang.z = AdjustAngle(twp->ang.z, B_8C754BEC, 0x800);
    PConvertVector_G2P(twp, &v);
    pwp->spd = v;
}

void func_8C071DD8(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    Float sq;

    v = pwp->spd;
    sq = FMAC_ADD(SA1_FMAC(v.x, v.x, v.y * v.y), v.z, v.z);
    if (sq > pwp->p.dash_speed * pwp->p.dash_speed) {
        return;
    }
    PConvertVector_P2G(twp, &v);
    twp->ang.x = AdjustAngle(twp->ang.x, 0, 0x800);
    twp->ang.z = AdjustAngle(twp->ang.z, 0xC000, 0x800);
    PConvertVector_G2P(twp, &v);
    pwp->spd = v;
}

void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang)
{
    NJS_VECTOR v;
    Sint32 diff;
    Float r;

    v = pwp->spd;
    PConvertVector_P2G(twp, &v);
    diff = DiffAngle(twp->ang.y, ang);
    if (diff > 0x2000) {
        twp->ang.y = AdjustAngle(twp->ang.y, ang, 0x800);
    } else if (diff > 0x1000) {
        diff >>= 2;
        twp->ang.y = AdjustAngle(twp->ang.y, ang, diff);
    } else {
        diff >>= 3;
        twp->ang.y = AdjustAngle(twp->ang.y, ang, diff);
    }
    PConvertVector_G2P(twp, &v);
    if (!(twp->flag & 3)) {
        pwp->spd.x = SA1_FMAC(pwp->spd.x, 0.099999996f, v.x * 0.9f);
        pwp->spd.y = SA1_FMAC(pwp->spd.y, 0.099999996f, v.y * 0.9f);
        pwp->spd.z = SA1_FMAC(pwp->spd.z, 0.099999996f, v.z * 0.9f);
    } else if (!GetAnalog(twp, NULL, NULL)) {
        r = 0.049999998f;
        if (mwp->weight < 1.0f) {
            r *= mwp->weight;
        }
        pwp->spd.x = SA1_FMAC(pwp->spd.x, r, v.x * (1.0f - r));
        pwp->spd.y = SA1_FMAC(pwp->spd.y, r, v.y * (1.0f - r));
        pwp->spd.z = SA1_FMAC(pwp->spd.z, r, v.z * (1.0f - r));
    } else if (pwp->dotp > 0.39999998f) {
        r = 0.98999996f;
        if (mwp->weight < 1.0f) {
            r *= mwp->weight;
        }
        pwp->spd.x = SA1_FMAC(pwp->spd.x, r, v.x * (1.0f - r));
        pwp->spd.y = SA1_FMAC(pwp->spd.y, r, v.y * (1.0f - r));
        pwp->spd.z = SA1_FMAC(pwp->spd.z, r, v.z * (1.0f - r));
    } else {
        r = 0.5f;
        if (mwp->weight < 1.0f) {
            r *= mwp->weight;
        }
        pwp->spd.x = SA1_FMAC(pwp->spd.x, r, v.x * (1.0f - r));
        pwp->spd.y = SA1_FMAC(pwp->spd.y, r, v.y * (1.0f - r));
        pwp->spd.z = SA1_FMAC(pwp->spd.z, r, v.z * (1.0f - r));
    }
}

void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang)
{
    NJS_VECTOR v;

    v = pwp->spd;
    PConvertVector_P2G(twp, &v);
    twp->ang.y = AdjustAngle(twp->ang.y, ang, 0x2000);
    PConvertVector_G2P(twp, &v);
    pwp->spd = v;
}

void func_8C0720A0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang)
{
    NJS_VECTOR v;
    Sint32 spd;
    Float r;
    Float s;

    v = pwp->spd;
    PConvertVector_P2G(twp, &v);
    spd = 0x100;
    if (pwp->spd.x > pwp->p.dash_speed) {
        spd -= njSqrt((pwp->spd.x - pwp->p.dash_speed) * 0.0625f) * 256.0f;
    }
    twp->ang.y = AdjustAngle(twp->ang.y, ang, spd);
    PConvertVector_G2P(twp, &v);
    if (pwp->dotp > 0.39999998f) {
        r = 0.98999996f;
        s = 1.0f - r;
    } else {
        r = 0.5f;
        s = 1.0f - r;
    }
    pwp->spd.x = SA1_FMAC(pwp->spd.x, r, v.x * s);
    pwp->spd.y = SA1_FMAC(pwp->spd.y, r, v.y * s);
    pwp->spd.z = SA1_FMAC(pwp->spd.z, r, v.z * s);
}

ASM_STUB(void func_8C0723E0(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C072510(taskwk *twp, motionwk *mwp))
