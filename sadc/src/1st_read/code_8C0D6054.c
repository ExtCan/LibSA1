/*
 * func_8C0D6054: spawns the func_8C0D5E80 object task at the position of
 * twp and hands it a parameter in value.w[0] (passed as a 32-bit int,
 * stored with MOV.W; read back by func_8C0D5E80 with MOV.W @(16,R13)).
 */
#include "common.h"
#include "task.h"
#include "sa1/engine.h"

extern void func_8C0D5E80(task *tp);

void func_8C0D6054(taskwk *twp, Sint32 n)
{
    task *tp;

    tp = CreateElementalTask(2, 2, func_8C0D5E80);
    if (tp) {
        tp->twp->pos = twp->pos;
        tp->twp->value.w[0] = (Sint16)n;
    }
}
