/*
 * 8C0B54AC-8C0B56D2 (end of chunk asm/1st_read/text/8C0B4F00.src).
 *
 * Polygon strip effect task. The task's mwp slot holds an MAlloc'd
 * NJS_POINT3COL (points, colours, uvs) drawn by func_8C0B504A with
 * func_8C63D66C (num - 2 polygons, attr 0x80000062). taskwk use:
 *   counter.l  maximum number of points
 *   timer.l    colour of the first point (0xFFFFFFFF from func_8C0B5690)
 *   value.ptr  NJS_POINT3 * target followed by the strip
 *   ang.x/y    pitch/yaw from pos to the target
 *   pos        start point
 *   scl        copy of the target position for this frame
 *   func_8C0B54AC  exec: grow the strip towards the target.
 *   func_8C0B5520  first exec: allocate the arrays and start the strip.
 *   func_8C0B5690  create the task (target point, number of points).
 */

#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/* disp: draws the strip */
ASM_STUB(void func_8C0B504A(task *tp))
/* adds points to the strip (mode 1: exec, 2: init) */
ASM_STUB(void func_8C0B50AE(taskwk *twp, NJS_POINT3COL *p3c, Sint32 mode))
void func_8C0B5424(task *tp);   /* dest: frees the arrays */
void func_8C0B5460(task *tp);   /* exec: fade out, then FreeTask */

void func_8C0B54AC(task *tp)
{
    taskwk *twp;
    NJS_POINT3COL *p3c;
    Uint32 num;
    NJS_POINT3 *tgt;

    twp = tp->twp;
    p3c = (NJS_POINT3COL *)tp->mwp;
    tgt = (NJS_POINT3 *)twp->value.ptr;
    twp->scl = *tgt;
    num = p3c->num;
    func_8C0B50AE(twp, p3c, 1);
    func_8C0B504A(tp);
    if (p3c->num == num || p3c->num >= (Uint32)twp->counter.l) {
        tp->exec = func_8C0B5460;
    }
}

/*
 * NON_MATCHING: 3 instructions differ. The constant 1 stored to p3c->num is
 * built in R3 and scheduled one store earlier; the original uses R2
 * (MOV.L R9,@(4,R12); MOV #1,R2; ...). Everything else matches: the int
 * size arithmetic (n * (Sint32)sizeof) makes SHC CSE n * 4 into a spilled
 * temp and spill n (stack frame 20); the Float local k gives the FR15/FR14/
 * FR13 order of the original. Store order, statement order, declaration
 * order, if/else/goto layouts and reuse of n for the count were tried.
 * Also tried (no change or worse): all 24 orders of the four p3c stores
 * (the store order follows the source), nested ifs, goto fail, a flat
 * early-return tail, an explicit size variable for n * 4, Uint32/Int n,
 * local num/max for the final test, casts on the constant, Float v[3],
 * NJS_VECTOR v. Cutting the success block down to the four p3c stores
 * still puts the 1 in R3, so the choice is made by the code before it.
 * Second pass: compiling after code_8C0B4F00.c (the rest of the original
 * TU, as with TU_EXPERIMENT in code_8C0AFB10.c) changes nothing; 1U, ->
 * instead of [0], block-scoped v/k/tgt, sizeof * n, a separate tp->mwp
 * test, goto/else-if failure layouts: no change or worse. The choice follows
 * the scratch registers used by the tex failure path just before it: with
 * MFree hoisted into R8 (four MFree calls there) the four stores come out
 * exactly as in the original (MOV #1,R2 third), with one MFree (R2) the 1 is
 * R2 but scheduled first, with the original two (R2, R3) it is R3.
 */
#ifdef NON_MATCHING
void func_8C0B5520(task *tp)
{
    NJS_POINT3 v;
    Float k;
    taskwk *twp;
    NJS_POINT3COL *p3c;
    NJS_POINT3 *pts;
    NJS_COLOR *col;
    NJS_TEX *tex;
    NJS_POINT3 *tgt;
    Sint32 n;

    twp = tp->twp;
    if ((tp->mwp = (motionwk *)MAlloc(sizeof(NJS_POINT3COL))) == NULL) {
        FreeTask(tp);
        return;
    }
    p3c = (NJS_POINT3COL *)tp->mwp;
    n = twp->counter.l;
    if ((pts = (NJS_POINT3 *)MAlloc(n * (Sint32)sizeof(NJS_POINT3))) == NULL) {
        FreeTask(tp);
        return;
    }
    if ((col = (NJS_COLOR *)MAlloc(n * (Sint32)sizeof(NJS_COLOR))) == NULL) {
        MFree(pts);
        FreeTask(tp);
        return;
    }
    if ((tex = (NJS_TEX *)MAlloc(n * (Sint32)sizeof(NJS_TEX))) == NULL) {
        MFree(col);
        MFree(pts);
    } else {
        p3c->p = pts;
        p3c->col = col;
        p3c->tex = tex;
        p3c->num = 1;
        pts[0].x = twp->pos.x;
        pts[0].y = twp->pos.y;
        pts[0].z = twp->pos.z;
        col[0].color = twp->timer.l;
        tex[0].u = 0;
        tex[0].v = 0;
        tgt = (NJS_POINT3 *)twp->value.ptr;
        twp->scl = *tgt;
        v.x = twp->scl.x - twp->pos.x;
        v.y = twp->scl.y - twp->pos.y;
        v.z = twp->scl.z - twp->pos.z;
        k = 10430.381f;
        twp->ang.x = SA1_FTOI(atan2f(v.y, njSqrt(SA1_FMAC(v.x, v.x, v.z * v.z))) * k);
        twp->ang.y = SA1_FTOI(atan2f(v.x, v.z) * k);
        func_8C0B50AE(twp, p3c, 2);
        tp->exec = func_8C0B54AC;
        tp->disp = func_8C0B504A;
        tp->dest = func_8C0B5424;
        if (p3c->num < 3 || p3c->num >= (Uint32)twp->counter.l) {
            FreeTask(tp);
            tp->disp = NULL;
        } else {
            func_8C0B504A(tp);
        }
        return;
    }
    FreeTask(tp);
}
#else
ASM_STUB(void func_8C0B5520(task *tp))
#endif

void func_8C0B5690(NJS_POINT3 *p, Sint32 n)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 3, func_8C0B5520)) != NULL) {
        tp->twp->value.ptr = p;
        tp->twp->counter.l = n;
        tp->twp->timer.l = -1;
        tp->twp->pos = *p;
    }
}
