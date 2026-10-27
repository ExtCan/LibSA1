#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * 1ST_READ 8C0C8F20-8C0C905A (chunk 8C0C8F20-8C0C9420, functions 1-2 of 4):
 * Knuckles helpers.
 *
 *  func_8C0C8F20  (KnucklesMain mode 18) nearest entry of the player's
 *                 target list (B_8C73EE38 / B_8C742288) that lies in the
 *                 front half (DiffAngle(ang.y, yaw to it) < 0x8000) and is
 *                 nearer than 400 (dist is squared: 160000); turns ang.y
 *                 towards it (AdjustAngle, 0x1000 per frame) and updates the
 *                 trail (func_8C0CBA40). When the best squared distance is
 *                 at most 225 (15 units) the dirt particles are spawned at
 *                 the player (func_8C0CA780). Returns the target's taskwk or
 *                 NULL.
 *  func_8C0C9000  (KnucklesMain, KnucklesModeMachine) stick to the polygon
 *                 under the player: flag |= 1, spd.x = spd.z = 0, ang.x/z and
 *                 pos from the collision status (cstsp angx/angz/onpoly),
 *                 flag &= ~0x400, mode 24, action 52.
 *
 * Matching notes: func_8C0C8F20's loop is entered with a BRA to the test at
 * the bottom (no guard copy of the test) only when the body leaves through
 * "continue"; a plain while/for gives the guarded form. min must be
 * stored before best (the store order follows from it). The two zero
 * stores of func_8C0C9000 need the chained assignment (FR3, x first).
 */

/* trail of twp (code_8C0CBA40.c) */
extern void func_8C0CBA40(taskwk *twp);
/* dirt particles at pos (code_8C0CA780.c defines it with pos only; the only
 * caller, func_8C0C8F20, passes 0 in R5: the original has a second, unused
 * argument) */
extern void func_8C0CA780(NJS_POINT3 *pos, Sint32 n);

taskwk *func_8C0C8F20(taskwk *twp)
{
    NJS_VECTOR v;
    PL_TARGET *p;
    taskwk *best;
    CCL_INFO *info;
    Float min;
    Angle ang;
    Angle bestang;

    PC_ONLY(bestang = 0;)
    if (twp->counter.b[0] == 0) {
        p = B_8C73EE38;
    } else {
        p = B_8C742288;
    }
    min = 160000.0f;
    best = NULL;
    for (; p->twp != NULL; p++) {
        info = CWP(p->twp)->info;
        v = info->center;
        if ((info->attr & 0x20) == 0) {
            njAddVector(&v, &p->twp->pos);
        }
        njSubVector(&v, &twp->pos);
        ang = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f);
        if (DiffAngle(twp->ang.y, ang) > 0x7FFF) {
            continue;
        }
        if (min > p->dist) {
            min = p->dist;
            best = p->twp;
            bestang = ang;
        }
    }
    if (best != NULL) {
        twp->ang.y = AdjustAngle(twp->ang.y, bestang, 0x1000);
        func_8C0CBA40(twp);
    }
    if (min <= 225.0f) {
        func_8C0CA780(&twp->pos, 0);
    }
    return best;
}

void func_8C0C9000(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    twp->flag |= 1;
    pwp->spd.z = pwp->spd.x = 0.0f;
    twp->ang.x = pwp->cstsp->angx;
    twp->ang.z = pwp->cstsp->angz;
    twp->pos.x = pwp->cstsp->onpoly.x;
    twp->pos.y = pwp->cstsp->onpoly.y;
    twp->pos.z = pwp->cstsp->onpoly.z;
    twp->flag &= ~0x400;
    twp->mode = 24;
    pwp->mj.reqaction = 52;
}
