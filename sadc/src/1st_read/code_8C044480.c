/*
 * 1ST_READ 8C044480-8C04454E (chunk 8C044480): the invincibility spark,
 * a child task created by the invincibility exec func_8C04430A
 * (code_8C043C00.c: CreateChildTask(2, func_8C044480, tp), disp
 * func_8C0444C4, random ang.x / ang.y). The original translation unit is
 * the one of chunk 8C043C00 (func_8C04430A's pool reloads func_8C0444C4 in
 * its loop only when these two are defined in the same file: see the
 * ASM_STUBs at the end of code_8C043C00.c); the split put them in a chunk
 * of their own, so they have their own file here.
 *
 *  func_8C044480  exec: 32 frames (btimer), follows the parent's position,
 *                 size = parent scl.y * 0.2, draws itself
 *  func_8C0444C4  disp: model D_8C1A0910 with texture (btimer / 2) & 7 of
 *                 D_8C19B6C8
 *
 * PORTABILITY: func_8C621574 is a Ninja-internal model draw (see
 * code_8C043C00.c); libsa1 needs an rt entry for it (LANDING_b.md).
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST D_8C19B6C8[];        /* invincibility spark textures (8) */
extern NJS_MODEL D_8C1A0910;            /* invincibility spark model */

void func_8C621574(NJS_MODEL *model);   /* Ninja model draw (8C60E31C's node draw) */

void func_8C0444C4(task *tp);

void func_8C044480(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;

    twp = tp->twp;
    ptwp = tp->ptp->twp;
    if (twp->btimer >= 32) {
        FreeTask(tp);
        return;
    }
    twp->pos = ptwp->pos;
    twp->scl.x = ptwp->scl.y * 0.19999999f;
    func_8C0444C4(tp);
    twp->btimer++;
}

void func_8C0444C4(task *tp)
{
    taskwk *twp;
    Sint32 n;

    twp = tp->twp;
    if (B_8C754EC8 == 0) {
        njPushMatrix(NULL);
        n = (twp->btimer >> 1) & 7;
        njSetTexture(&D_8C19B6C8[n]);
        njTranslateV(NULL, &twp->pos);
        twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        njScale(NULL, twp->scl.x, twp->scl.x, twp->scl.x);
        func_8C621574(&D_8C1A0910);
        njPopMatrix(1);
    }
}
