#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06CFCC-8C06D254): direction tests against the
 * object the player collided with, and turning towards the nearest target.
 *  func_8C06CFCC  stick direction vs. push-out direction of the CCL hit,
 *                 classified 1 (opposite) .. 4 (same direction); mwp is
 *                 passed by the callers (8C06F386, 8C0B1C78) but not read
 *                 (prototype: sa1/protos.h)
 *  func_8C06D080  movement direction (-mwp->spd) vs. push-out direction,
 *                 classified the same way
 *  func_8C06D118  target list of player pno is not empty
 *  func_8C06D136  turn towards the nearest entry of the target list, or
 *                 rotate from input when the list is empty
 */

/* CCL hit query: push-out vector from the hit shape (8C0344DC; returns a
 * byte, non-zero when there is a hit) */
Sint8 func_8C0344DC(taskwk *twp, NJS_POINT3 *v);

/* Target list entry: task work + distance (bss 8C73EE38 / 8C742288, 9288
 * bytes = 1161 entries each, terminated by twp == NULL; filled by
 * 8C0334E8 in the CCL code) */
typedef struct {
    taskwk *twp;                /* 0x00 */
    Float dist;                 /* 0x04 */
} PL_TARGET;

extern PL_TARGET B_8C73EE38[];  /* player 0 */
extern PL_TARGET B_8C742288[];  /* other players */

ASM_STUB(Sint32 func_8C06CD22(taskwk *twp, Angle *ang, Float *mag))

Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp)
{
    NJS_VECTOR v;
    Angle ang;
    Sint32 diff;

    if (twp->flag & 0x4000) {
        return 0;
    }
    if (GetAnalog(twp, &ang, NULL) == 0) {
        return 0;
    }
    func_8C0344DC(twp, &v);
    diff = DiffAngle(ang, SA1_FTOI(atan2f(-v.z, -v.x) * 10430.381f));
    if (diff > 0x6000) {
        return 1;
    }
    if (diff > 0x3400) {
        return 2;
    }
    if (diff > 0x1400) {
        return 3;
    }
    return 4;
}

Sint32 func_8C06D080(taskwk *twp, motionwk *mwp)
{
    NJS_VECTOR v;
    NJS_VECTOR dir;
    Sint32 ang;

    func_8C0344DC(twp, &v);
    dir.x = -mwp->spd.x;
    dir.y = -mwp->spd.y;
    dir.z = -mwp->spd.z;
    if (njScalor(&dir) != 0.0f) {
        njUnitVector(&dir);
    }
    ang = SA1_FTOI(func_8C10D008(njInnerProduct(&v, &dir)) * 10430.381f);
    if (ang > 0x6000) {
        return 1;
    }
    if (ang > 0x3400) {
        return 2;
    }
    if (ang > 0x1400) {
        return 3;
    }
    return 4;
}

Sint32 func_8C06D118(Uint8 pno)
{
    PL_TARGET *p;

    if (pno == 0) {
        p = B_8C73EE38;
    } else {
        p = B_8C742288;
    }
    if (p->twp == NULL) {
        return 0;
    }
    return 1;
}

void func_8C06D136(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    PL_TARGET *p;
    taskwk *ttwp;
    Float dist;
    CCL_INFO *info;
    CCL_INFO *tinfo;
    Angle ang;

    if (twp->counter.b[0] == 0) {
        p = B_8C73EE38;
    } else {
        p = B_8C742288;
    }
    if (p->twp == NULL) {
        func_8C071700(twp, mwp, pwp);
        return;
    }
    ttwp = p->twp;
    dist = p->dist;
    while (p->twp != NULL) {
        if (dist > p->dist) {
            dist = p->dist;
            ttwp = p->twp;
        }
        p++;
    }
    tinfo = CWP(ttwp)->info;
    info = CWP(twp)->info;
    v.x = ttwp->pos.x + tinfo->center.x - info->center.x;
    v.y = ttwp->pos.y + tinfo->center.y - info->center.y;
    v.z = ttwp->pos.z + tinfo->center.z - info->center.z;
    ang = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f) & 0xFFFF;
    mwp->ang_aim.y = ang;
    twp->ang.y = AdjustAngle(twp->ang.y, ang, 0x1000);
}
