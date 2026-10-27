#include "sa1/sa1.h"

/*
 * Sonic: steering towards CCL targets (1ST_READ 8C0AF5A0-8C0AFB10; the two
 * steps are called from SonicMain 8C0A77BC).
 *  func_8C0AF5A0  homing target: nearest entry of the player's target list
 *                 whose cwp->id is 2 or 3, or the rival player when nearer
 *                 (also called by 8C021700, SonicModeMachine, 8C0B1352)
 *  func_8C0AF6C4  homing attack step: no target -> damp spd.x and run the
 *                 normal acceleration/speed; else steer mwp->spd towards the
 *                 target (speed 10) and return 1
 *  func_8C0AF90A  dash step over the second target list (light speed dash?):
 *                 nearest entry within a 0x3000 cone ahead, turn towards it
 *                 and move at sqrt(dist) clamped to 2..16
 */

/* Target list entry (same layout as code_8C06CFCC.c): task work + squared
 * distance, terminated by twp == NULL; filled by the CCL code 8C0334E8 */
typedef struct {
    taskwk *twp;                /* 0x00 */
    Float dist;                 /* 0x04 */
} PL_TARGET;

extern PL_TARGET B_8C73EE38[];  /* homing targets, player 0 */
extern PL_TARGET B_8C742288[];  /* homing targets, other players */
extern PL_TARGET B_8C741280[];  /* light speed dash targets, player 0 */
extern PL_TARGET B_8C7446D0[];  /* light speed dash targets, other players */

Sint32 func_8C07D18E(Uint8 pno);                                /* rival player slot (taskwk.id 3 <-> 1), -1 if none */
void func_8C0714B6(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* acceleration (gravity, input) */

taskwk *func_8C0AF5A0(taskwk *twp, playerwk *pwp)
{
    NJS_VECTOR v;
    PL_TARGET *p;
    taskwk *ttwp;
    Float dist;
    Sint32 n;
    colliwk *cwp;

    if (twp->counter.b[0] == 0) {
        p = B_8C73EE38;
    } else {
        p = B_8C742288;
    }
    pwp->free.w[3] = 0;
    if (p->twp == NULL) {
        return NULL;
    }
    n = func_8C07D18E(twp->counter.b[0]);
    if (n != -1) {
        ttwp = playertwp[n];
        v.x = twp->pos.x - ttwp->pos.x;
        v.y = twp->pos.y - ttwp->pos.y;
        v.z = twp->pos.z - ttwp->pos.z;
        dist = njScalor2(&v);
    } else {
        dist = 160000.0f;
        ttwp = NULL;
    }
    while (p->twp != NULL) {
        cwp = CWP(p->twp);
        if ((cwp->id == 2 || cwp->id == 3) && dist > p->dist) {
            dist = p->dist;
            ttwp = p->twp;
        }
        p++;
    }
    return ttwp;
}

Sint32 func_8C0AF6C4(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    NJS_VECTOR w;
    taskwk *ttwp;
    CCL_INFO *info;
    Float f;
    Float g;

    if ((ttwp = func_8C0AF5A0(twp, pwp)) == NULL) {
        pwp->spd.x *= 0.9f;
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        return 0;
    } else if (pwp->free.w[1] < 2) {
        v = pwp->spd;
        PConvertVector_P2G(twp, &v);
        mwp->spd = v;
    } else {
        info = CWP(ttwp)->info;
        v = info->center;
        if ((info->attr & 0x20) == 0) {
            njAddVector(&v, &ttwp->pos);
        }
        njSubVector(&v, &twp->pos);
        if (njScalor(&v) == 0.0f) {
            v.x = -Gravity.x;
            v.y = -Gravity.y;
            v.z = -Gravity.z;
        } else {
            njUnitVector(&v);
        }
        w = mwp->spd;
        if (njScalor(&w) == 0.0f) {
            w = v;
        } else {
            njUnitVector(&w);
        }
        g = 8.0f;
        f = SA1_FMAC((Float)(pwp->free.w[1] & 31), 0.0625f, 2.0f);
        v.x = SA1_FMAC(w.x, g, v.x * f);
        v.y = SA1_FMAC(w.y, g, v.y * f);
        v.z = SA1_FMAC(w.z, g, v.z * f);
        if (njScalor(&v) == 0.0f) {
            v = w;
        } else {
            njUnitVector(&v);
        }
        v.x *= 10.0f;
        v.y *= 10.0f;
        v.z *= 10.0f;
        mwp->spd = v;
        PConvertVector_G2P(twp, &v);
        pwp->spd = v;
    }
    return 1;
}

void func_8C0AF90A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR fwd;
    NJS_VECTOR v;
    CCL_INFO *info;
    PL_TARGET *p;
    taskwk *ttwp;
    taskwk *best;
    Float dist;
    Angle ang;

    if (twp->counter.b[0] == 0) {
        p = B_8C741280;
    } else {
        p = B_8C7446D0;
    }
    if (p->twp == NULL) {
        func_8C0714B6(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        return;
    }
    fwd.x = 1.0f;
    fwd.y = 0.0f;
    fwd.z = 0.0f;
    PConvertVector_P2G(twp, &fwd);
    njUnitVector(&fwd);
    best = NULL;
    PC_ONLY(dist = 0.0f;)
    while (p->twp != NULL) {
        ttwp = p->twp;
        info = CWP(ttwp)->info;
        v = ttwp->pos;
        njAddVector(&v, &info->center);
        njSubVector(&v, &twp->pos);
        njUnitVector(&v);
        ang = VectorAngle(&fwd, &v, NULL);
        if (ang <= 0x3000) {
            if (best == NULL || dist > p->dist) {
                best = ttwp;
                dist = p->dist;
            }
        }
        p++;
    }
    if (best == NULL) {
        return;
    }
    if (pwp->free.w[2] < 5) {
        pwp->free.w[2] = 5;
    }
    info = CWP(best)->info;
    v = best->pos;
    njAddVector(&v, &info->center);
    njSubVector(&v, &twp->pos);
    twp->ang.y = AdjustAngle(twp->ang.y, SA1_FTOI(atan2f(v.z, v.x) * 10430.381f), 0x1800);
    mwp->ang_aim.y = twp->ang.y;
    dist = njSqrt(dist);
    if (dist > 16.0f) {
        dist = 16.0f;
    } else if (dist < 2.0f) {
        dist = 2.0f;
    }
    if (njScalor(&v) <= 1.0f) {
        v = mwp->spd;
    }
    njUnitVector(&v);
    mwp->spd.x = v.x * dist;
    mwp->spd.y = v.y * dist;
    mwp->spd.z = v.z * dist;
    PConvertVector_G2P(twp, &v);
    pwp->spd = v;
}
