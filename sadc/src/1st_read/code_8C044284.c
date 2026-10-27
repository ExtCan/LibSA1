/*
 * func_8C044284: init routine of the invincibility task created by
 * func_8C07D4A0 (CreateElementalTask(2, 2, func_8C044284)).
 * Sets dest = func_8C044280 (empty), exec = func_8C04430A and runs exec once.
 *
 * The original loads &func_8C044290 (the drawing routine, which
 * func_8C04430A calls itself with BSR) into R2 and never stores it: a dead
 * store that SHC eliminated after the constant load. Writing it as an
 * overwritten tp->exec store reproduces that; tp->disp is NOT set.
 */

#include "common.h"
#include "task.h"
#include "sa1/sa1.h"

void func_8C044280(task *tp);
void func_8C044290(task *tp);
ASM_STUB(void func_8C04430A(task *tp))

void func_8C044284(task *tp)
{
    tp->dest = func_8C044280;
    tp->exec = func_8C044290;
    tp->exec = func_8C04430A;
    func_8C04430A(tp);
}
