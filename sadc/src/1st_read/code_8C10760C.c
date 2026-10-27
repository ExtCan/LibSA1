/*
 * func_8C10760C, func_8C10762C (8C10760C-8C1076AC): motion helpers of an
 * object whose taskwk.scl holds its velocity. Callers: func_8C1032AC,
 * func_8C107102.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/* 12-byte vector added to the position each frame (the vector part of the
 * B_8C7A52D8 record, see code_8C102E80.c; set by func_8C102E9E). */
extern NJS_POINT3 B_8C7A52DC;

/* Gravity: scl.y -= 0.1 per frame, clamped at -3.0. */
void func_8C10760C(task *tp)
{
    if ((tp->twp->scl.y += -0.099999996f) < -3.0f) {
        tp->twp->scl.y = -3.0f;
    }
}

/* Snap tiny velocity components to 0, move forward along ang.y by scl.x,
 * add scl.y to the height and add the B_8C7A52DC drift vector. */
void func_8C10762C(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (_builtin_fabsf(twp->scl.x) < 0.01f) {
        twp->scl.x = 0.0f;
    }
    if (_builtin_fabsf(twp->scl.y) < 0.01f) {
        twp->scl.y = 0.0f;
    }
    if (_builtin_fabsf(twp->scl.z) < 0.01f) {
        twp->scl.z = 0.0f;
    }
    twp->pos.x -= twp->scl.x * njCos(twp->ang.y);
    SA1_FMAC_ACC(twp->pos.z, twp->scl.x, njSin(twp->ang.y));
    twp->pos.y += twp->scl.y;
    njAddVector(&twp->pos, &B_8C7A52DC);
}
