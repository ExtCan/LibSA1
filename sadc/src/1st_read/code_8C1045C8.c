/*
 * 1ST_READ 8C1045C8-8C104CFA: land push-out probes and a short-lived
 * collision probe task (spawned by func_8C104B60, exec func_8C104978).
 *
 *  func_8C1045C8  six axis probes (+x, -x, +y, -y, +z, -z) of pos against
 *                 the land objects gathered within r; for each axis keeps
 *                 the hit within r of the point: even axes the largest,
 *                 odd axes the smallest onpos. Returns 1 if a unit changed.
 *  func_8C104800  pushes twp->pos 12 units out of the walls/floor/ceiling
 *                 found by func_8C1045C8 (flag 0x100 wall, 1 floor).
 *  func_8C104978  exec of the probe task: CCL_Init (D_8C5C6B80), then flies
 *                 along scl for at most 30 frames; dies on a land hit within
 *                 5 of its position or on a CCL hit (B_8C7A52CC = 1 when the
 *                 hit colliwk has id 0, 0 for ids 4-7, timeout or land).
 *  func_8C104B60  line-of-sight test toward player 0: spawns the probe task
 *                 (one at a time, B_8C7A52C8) from twp->pos toward the
 *                 player at 5 units/frame. Returns 0 when func_8C0DDDE6(0)
 *                 is 1, B_8C7A52D8.flag is 1, the player is farther than
 *                 150 or Amy's state (func_8C0DDE1C) is 6; 1 when that state
 *                 is 3, else 1 when the player is in front (DiffAngle(ang.y,
 *                 dir) <= 0x471C) and the last probe reached it (B_8C7A52CC).
 *  func_8C104CCA  squared distance between two points.
 *
 * Matching notes: func_8C1045C8 assigns lc before the pos copy; the odd-axis
 * compare is written "new < old". func_8C104978 needs the id read as
 * "!(id = hit->id)" inside "(hit = ...) != NULL" (Uint16 kept raw in R4,
 * hit in R5; a plain "id = hit->id; if (id == 0)" spills id to the stack).
 * func_8C104B60: "(dist = njScalor(&v)) == 0.0f", "v.y = v.z = 0.0f", the
 * creation folded into one condition (puts ctp in a stack slot) and the last
 * two tests as separate "return 0" statements.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"
#include "sa1/shc_intrinsics.h"
#include "sa1/pending.h"

/* File-local view of B_8C7A52D8 (see code_8C102E80.c). */
typedef struct {
    Sint32 flag;                /* 0x00 */
    NJS_POINT3 vec;             /* 0x04 (B_8C7A52DC) */
} struct_8C7A52D8;

extern struct_8C7A52D8 B_8C7A52D8;
extern Sint32 B_8C7A52C8;       /* probe task alive */
extern Sint32 B_8C7A52CC;       /* probe task reached a CCL object with id 0 */
extern CCL_INFO D_8C5C6B80[];   /* probe task collision shape */

Sint32 func_8C0DDDE6(Sint32 pno);       /* code_8C0DDA00.c */
Sint32 func_8C0DDE1C(Sint32 pno);       /* code_8C0DDA00.c: Amy state */

void func_8C104978(task *tp);

Sint32 func_8C1045C8(xyyzzxsdwstr *xyzp, NJS_POINT3 *pos, Float r)
{
    xyyzzxsdwstr xyz;
    Sint32 i;
    _OBJ_LANDCOLL *lc;
    Sint32 j;
    Sint32 flag;
    Sint32 ret;

    ret = 0;
    func_8C0522C0(pos->x, pos->y, pos->z, r);
    for (j = 0; j < 6; j++) {
        xyzp->hit[j].findflag = 0;
    }
    lc = B_8C754EF4;
    xyz.pos = *pos;
    for (i = B_8C754EF0; i > 0; i--, lc++) {
        if (lc->slAttribute & 0x400002) {
            continue;
        }
        if (!func_8C029400(&xyz, lc->pObject, lc->slAttribute)) {
            continue;
        }
        for (j = 0; j < 6; j++) {
            if (xyz.hit[j].findflag == 0) {
                continue;
            }
            if (_builtin_fabsf((&pos->x)[j >> 1] - xyz.hit[j].onpos) > r) {
                continue;
            }
            if (xyzp->hit[j].findflag == 0) {
                xyzp->hit[j] = xyz.hit[j];
                ret = 1;
            } else {
                flag = 0;
                if (j & 1) {
                    if (xyz.hit[j].onpos < xyzp->hit[j].onpos) {
                        flag = 1;
                    }
                } else {
                    if (xyz.hit[j].onpos > xyzp->hit[j].onpos) {
                        flag = 1;
                    }
                }
                if (flag == 1) {
                    xyzp->hit[j] = xyz.hit[j];
                    ret = 1;
                }
            }
        }
    }
    return ret;
}

void func_8C104800(task *tp)
{
    xyyzzxsdwstr xyz;
    taskwk *twp;
    Float y;

    twp = tp->twp;
    func_8C1045C8(&xyz, &twp->pos, 12.0f);
    twp->flag &= ~0x100;
    if (xyz.hit[0].findflag == 1 && twp->pos.x - xyz.hit[0].onpos < 12.0f) {
        twp->pos.x = xyz.hit[0].onpos + 12.0f;
        twp->flag |= 0x100;
    }
    if (xyz.hit[1].findflag == 1 && xyz.hit[1].onpos - twp->pos.x < 12.0f) {
        twp->pos.x = xyz.hit[1].onpos + -12.0f;
        twp->flag |= 0x100;
    }
    if (xyz.hit[4].findflag == 1 && twp->pos.z - xyz.hit[4].onpos < 12.0f) {
        twp->pos.z = xyz.hit[4].onpos + 12.0f;
        twp->flag |= 0x100;
    }
    if (xyz.hit[5].findflag == 1 && xyz.hit[5].onpos - twp->pos.z < 12.0f) {
        twp->pos.z = xyz.hit[5].onpos + -12.0f;
        twp->flag |= 0x100;
    }
    twp->flag &= ~1;
    if (xyz.hit[2].findflag == 1 && twp->pos.y - xyz.hit[2].onpos < 12.0f) {
        y = twp->pos.y;
        twp->pos.y = xyz.hit[2].onpos + 12.0f;
        twp->flag |= 1;
        if (twp->pos.y - y > 3.0f) {
            twp->scl.y = 1.0f;
        } else {
            twp->scl.y *= -0.5f;
        }
    }
    if (xyz.hit[3].findflag == 1 && xyz.hit[3].onpos - twp->pos.y < 12.0f) {
        twp->pos.y = xyz.hit[3].onpos + -12.0f;
        twp->scl.y = 0.0f;
    }
}

void func_8C104978(task *tp)
{
    _OBJ_LANDCOLL *lc;
    Sint32 i;
    xyyzzxsdwstr xyz;
    taskwk *twp;
    xssunit *hp;
    Float *p;
    Sint32 n;
    colliwk *hit;
    Uint16 id;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        CCL_Init(tp, D_8C5C6B80, 4, 1, 1);
        twp->counter.f = 0.0f;
        twp->mode = 1;
        break;
    case 1:
        if ((twp->counter.f += 5.0f) >= 150.0f) {
            B_8C7A52CC = 0;
            B_8C7A52C8 = 0;
            FreeTask(tp);
            return;
        }
        if ((hit = CWP(twp)->hit_cwp) != NULL) {
            if (!(id = hit->id)) {
                B_8C7A52CC = 1;
                B_8C7A52C8 = 0;
                FreeTask(tp);
                return;
            }
            if (id >= 4 && id <= 7) {
                B_8C7A52CC = 0;
                B_8C7A52C8 = 0;
                FreeTask(tp);
                return;
            }
        }
        func_8C0522C0(twp->pos.x, twp->pos.y, twp->pos.z, 5.0f);
        xyz.pos = twp->pos;
        for (i = B_8C754EF0, lc = B_8C754EF4; i > 0; i--, lc++) {
            if (func_8C029400(&xyz, lc->pObject, lc->slAttribute) == 1) {
                hp = xyz.hit;
                p = &twp->pos.x;
                for (n = 6; n > 0; n--) {
                    if (hp->findflag == 1 && _builtin_fabsf(hp->onpos - *p) < 5.0f) {
                        B_8C7A52CC = 0;
                        B_8C7A52C8 = 0;
                        FreeTask(tp);
                        return;
                    }
                    hp++;
                    if (n & 1) {
                        p++;
                    }
                }
            }
        }
        twp->pos.x += twp->scl.x;
        twp->pos.y += twp->scl.y;
        twp->pos.z += twp->scl.z;
        func_8C02F63C(twp);
        break;
    }
}

Sint32 func_8C104B60(task *tp)
{
    NJS_VECTOR v;
    NJS_POINT3 a;
    NJS_POINT3 b;
    NJS_VECTOR s;
    task *ctp;
    taskwk *twp;
    Float dist;

    twp = tp->twp;
    if (func_8C0DDDE6(0) == 1) {
        return 0;
    }
    if (B_8C7A52D8.flag == 1) {
        return 0;
    }
    a = twp->pos;
    b = playertwp[0]->pos;
    b.y += 5.0f;
    v.x = b.x - a.x;
    v.y = b.y - a.y;
    v.z = b.z - a.z;
    if ((dist = njScalor(&v)) == 0.0f) {
        v.x = 1.0f;
        v.y = v.z = 0.0f;
    } else {
        njUnitVector(&v);
    }
    if (B_8C7A52C8 == 0 && (ctp = CreateElementalTask(2, 6, func_8C104978)) != NULL) {
        B_8C7A52C8 = 1;
        s = v;
        s.x *= 5.0f;
        s.y *= 5.0f;
        s.z *= 5.0f;
        ctp->twp->pos = a;
        ctp->twp->scl = s;
    }
    if (dist > 150.0f) {
        return 0;
    }
    if (func_8C0DDE1C(0) == 6) {
        return 0;
    }
    if (func_8C0DDE1C(0) == 3) {
        return 1;
    }
    if (DiffAngle(twp->ang.y, SA1_FTOI(atan2f(v.z, -v.x) * 10430.381f)) > 0x471C) {
        return 0;
    }
    if (!B_8C7A52CC) {
        return 0;
    }
    return 1;
}

Float func_8C104CCA(NJS_POINT3 *a, NJS_POINT3 *b)
{
    NJS_VECTOR v;

    v = *b;
    njSubVector(&v, a);
    return njScalor2(&v);
}
