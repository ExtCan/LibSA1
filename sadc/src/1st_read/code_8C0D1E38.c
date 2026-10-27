#include "sa1/pending.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0CBE00-8C0D2080, functions 25-28 of 28):
 *
 *  func_8C0D1E38  stand selector (E102Main, func_8C0CD09C, E102ModeMachine):
 *                 on level ground (cos(0x11C7), about 25 degrees) requests
 *                 0/42/43/73/74/75 become the idle action 73 (with a voice,
 *                 8C07DCE0) after 1800 frames, any other request is reset
 *                 to 0; on steeper ground (up to 45 degrees) without
 *                 attr 0x4000 the slope stand action 85 or 86 is picked by
 *                 the angle between E-102's forward axis and gravity, both
 *                 projected onto the ground plane. Same code as Sonic's
 *                 func_8C0AE3D6 (code_8C0AE0E0.c) with E-102's action numbers.
 *  func_8C0D1FC8  walk action selector (E102Main, E102ModeMachine): leaves
 *                 19, 24 and 9 alone; speed spd.x + 5 * mwp->height against
 *                 jog_speed picks 22/26 while holding an object
 *                 (twp->flag & 0x800), else 2/1.
 *  func_8C0D2032  per-mode flag: D_8C422B44[twp->mode] (func_8C0D33A0).
 *  func_8C0D203A  tail call to PGetAcceleration (E102Main).
 */

/* character voice (code_8C07DCE0.c) */
extern void func_8C07DCE0(taskwk *twp);

/* per-mode byte flags (0/1), indexed by taskwk.mode */
extern Sint8 D_8C422B44[];

/*
 * The four functions that precede func_8C0D1E38 in the original translation
 * unit (decompiled in code_8C0D1672.c and code_8C0D1916.c). They are not
 * called from here, but their pending literal-pool entries make SHC dump the
 * pool in the middle of func_8C0D1E38 (8C0D1EBA, after the idle-voice branch),
 * which turns the waittimer test into a BT/BRA far branch, as in the
 * original. With fewer stubs the pool lands elsewhere and the function does
 * not match (same mechanism as code_8C0AE588.c).
 */
ASM_STUB(void func_8C0D18B8(task *tp))
ASM_STUB(void func_8C0D1916(task *tp))
ASM_STUB(Sint32 func_8C0D1C78(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(Sint32 func_8C0D1D76(taskwk *twp, motionwk *mwp, playerwk *pwp))

void func_8C0D1E38(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_LINE line;
    NJS_POINT3 v;
    NJS_POINT3 b;
    NJS_POINT3 a;
    Float dot;
    Angle ang;

    dot = -njInnerProduct(&Gravity, &pwp->floor_normal);
    if (dot > njCos(0x11C7)) {
        if (pwp->mj.reqaction != 0 && pwp->mj.reqaction != 42 && pwp->mj.reqaction != 43
            && pwp->mj.reqaction != 73 && pwp->mj.reqaction != 74 && pwp->mj.reqaction != 75) {
            pwp->mj.reqaction = 0;
        } else if (pwp->waittimer > 1800) {
            pwp->waittimer = 0;
            pwp->mj.reqaction = 73;
            func_8C07DCE0(twp);
        }
    } else if (!(pwp->attr & 0x4000)) {
        if (dot > njCos(0x2000)) {
            line.vx = pwp->floor_normal.x;
            line.vy = pwp->floor_normal.y;
            line.vz = pwp->floor_normal.z;
            line.px = line.py = line.pz = 0.0f;
            v.x = 1.0f;
            v.y = v.z = 0.0f;
            PConvertVector_P2G(twp, &v);
            njDistanceP2L(&v, &line, &a);
            njUnitVector(&a);
            njDistanceP2L(&Gravity, &line, &b);
            njUnitVector(&b);
            ang = VectorAngle(&a, &b, &pwp->floor_normal);
            if (ang > 0xC000 || ang < 0x4000) {
                pwp->mj.reqaction = 86;
            } else {
                pwp->mj.reqaction = 85;
            }
        }
    }
    pwp->waittimer++;
}

void func_8C0D1FC8(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float spd;

    spd = SA1_FMAC(5.0f, mwp->height, pwp->spd.x);
    if (pwp->mj.reqaction == 19 || pwp->mj.reqaction == 24 || pwp->mj.reqaction == 9) {
        return;
    }
    if (twp->flag & 0x800) {
        if (spd > pwp->p.jog_speed) {
            pwp->mj.reqaction = 22;
        } else {
            pwp->mj.reqaction = 26;
        }
    } else if (spd > pwp->p.jog_speed) {
        pwp->mj.reqaction = 2;
    } else {
        pwp->mj.reqaction = 1;
    }
}

Sint8 func_8C0D2032(taskwk *twp)
{
    return D_8C422B44[twp->mode];
}

void func_8C0D203A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    PGetAcceleration(twp, mwp, pwp);
}
