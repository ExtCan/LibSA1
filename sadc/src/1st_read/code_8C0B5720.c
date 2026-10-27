#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/engine.h"

/*
 * Small particle burst: a mover task (func_8C0B5720) that drifts its
 * position by a random velocity for a number of frames, plus a strip task
 * (func_8C0B5520, code_8C0B54AC.c) that follows the mover's position with a
 * random colour.
 */

void func_8C0B5520(task *tp);

/* mover: pos += scl (velocity) while counter lasts, then free the task */
void func_8C0B5720(task *tp)
{
    taskwk *twp = tp->twp;

    if (!((Uint32)twp->counter.l-- > 0)) {
        FreeTask(tp);
        return;
    }
    twp->pos.x += twp->scl.x;
    twp->pos.y += twp->scl.y;
    twp->pos.z += twp->scl.z;
}

#ifdef NON_MATCHING
/*
 * Spawn the burst at ptwp->pos for time frames: a mover with a random
 * velocity in [-0.5, 0.5) per axis and a strip following the mover's pos
 * with a random colour (each channel 127..254, alpha 0xFF).
 *
 * NON_MATCHING: 2 instructions differ, register choice only. The original
 * loads CreateElementalTask for the second call into R2 (MOV.L @(200,PC),R2 /
 * JSR @R2 at 8C0B57DC/8C0B57EA); SHC gives R3 here. Everything else matches.
 * Notes from the attempts:
 *  - "if ((dtp = ...) != NULL) { body } else { FreeTask(tp); }" (nested
 *    if/else) lowers &tp->twp->pos with an extra copy (MOV R2,R1; ADD #32,R1),
 *    which pushes the first BT/S out of range (BF/S + BRA) and shifts R1/R2/R3
 *    in the body. The "goto fail" form below lays the blocks out the same way
 *    as the original (body falls through, FreeTask tail call after it) and
 *    allocates the body exactly (R1/R2).
 *  - "(Uint8 *)tp->twp + 0x20" in the nested form avoids the copy but leaves
 *    the body one register off (R2/R3) and is a raw offset.
 *  - No effect on the R2/R3 choice: declaration order, register/unused locals,
 *    block-scoped dtp, rand/CreateElementalTask parameter/return types,
 *    float expression forms, do/while(0)+break or while (those add loop
 *    allocation), splitting the CreateElementalTask address CSE.
 *  - Also no effect (second session): a local for the rand() result or the
 *    scl.z value, a pointer local for &twp->scl / &twp->pos, the colour
 *    written as (rand() * k) * 127.0f + 127.0f (SHC folds it to the same
 *    constant), Uint8 casts / 0xFF, Uint32 time, assignment and NULL test
 *    split into two statements (worse), nested if/else for one or both
 *    tasks (worse), the trail creation or the random value as
 *    #pragma inline helpers (worse), extra functions or externs before the
 *    function, the flags -aggressive=2/-macsave=1/-loop/-inline/-case=table.
 *    Removing any single statement still leaves the address in R3. The
 *    same JSR @R2 vs @R3 difference at the second CreateElementalTask is
 *    seen in the sibling func_8C0B5B80 (code_8C0B5B80.c).
 */
void func_8C0B5760(taskwk *ptwp, Sint32 time)
{
    task *tp;
    task *dtp;

    if ((tp = CreateElementalTask(2, 2, func_8C0B5720)) == NULL) {
        return;
    }
    tp->twp->pos = ptwp->pos;
    tp->twp->counter.l = time;
    tp->twp->scl.x = SA1_FMAC((Float)rand(), 3.0517579e-05f, -0.5f) * 1.0f;
    tp->twp->scl.y = SA1_FMAC((Float)rand(), 3.0517579e-05f, -0.5f) * 1.0f;
    tp->twp->scl.z = SA1_FMAC((Float)rand(), 3.0517579e-05f, -0.5f) * 1.0f;

    if ((dtp = CreateElementalTask(2, 3, func_8C0B5520)) == NULL) {
        goto fail;
    }
    dtp->twp->value.ptr = &tp->twp->pos;
    dtp->twp->counter.l = time;
    dtp->twp->timer.b[0] = (Sint8)SA1_FTOI(SA1_FMAC((Float)rand(), 0.003875732421875f, 127.0f));
    dtp->twp->timer.b[1] = (Sint8)SA1_FTOI(SA1_FMAC((Float)rand(), 0.003875732421875f, 127.0f));
    dtp->twp->timer.b[2] = (Sint8)SA1_FTOI(SA1_FMAC((Float)rand(), 0.003875732421875f, 127.0f));
    dtp->twp->timer.b[3] = -1;
    dtp->twp->pos = tp->twp->pos;
    return;

fail:
    FreeTask(tp);
}
#else
ASM_STUB(void func_8C0B5760(taskwk *ptwp, Sint32 time))
#endif
