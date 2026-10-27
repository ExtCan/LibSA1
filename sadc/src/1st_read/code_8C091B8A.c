#include "sa1/pending.h"

/*
 * 1ST_READ 8C091B8A-8C091BC2: SET object condition helpers.
 * Bit 0 of ocp->ssCondition marks a SET entry whose task is alive
 * (DestroyTask 8C098752 clears it); bit 15 is cleared together with it.
 */

/* Clears bits 15 and 0 of the task's SET condition (ssCondition &= ~0x8001).
 * Called by 8C0D46CE, 8C0F414A, 8C0F4B12. */
void func_8C091B8A(task *tp)
{
    ((OBJ_CONDITION *)tp->ocp)->ssCondition &= ~0x8000;
    ((OBJ_CONDITION *)tp->ocp)->ssCondition &= ~1;
}

/* Same as func_8C091B8A, then makes the task destroy itself on its next
 * execution (exec = DestroyTask). Used by many SET objects (rings, springs,
 * item boxes, balloons, emblems, enemies) when they are collected/killed. */
void func_8C091BA4(task *tp)
{
    ((OBJ_CONDITION *)tp->ocp)->ssCondition &= ~0x8000;
    ((OBJ_CONDITION *)tp->ocp)->ssCondition &= ~1;
    tp->exec = DestroyTask;
}
