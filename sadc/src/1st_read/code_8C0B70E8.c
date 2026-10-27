/*
 * 1ST_READ 8C0B70E8: Tails helper (chunk 8C0B6120-8C0BA820, function 3/6;
 * func_8C0B6120, TailsMain, TailsModeMachine, func_8C0BA738 and
 * func_8C0BA75A are separate assignments).
 *
 * func_8C0B70E8(twp, pwp): sets twp->mode to 1, clears the idle timer and
 * tail-calls func_8C0BAFC0(twp, pwp) (chunk 8C0BAFC0, Tails action code).
 * Called by TailsModeMachine (twp = its 1st argument, pwp = its 3rd) and
 * by several Tails action functions in chunk 8C0BAFC0 (8C0BBDD6,
 * 8C0BCD00, 8C0BCEC0, 8C0BCF56, 8C0BD336, 8C0BD4CC).
 */
#include "sa1/sa1.h"

extern void func_8C0BAFC0(taskwk *twp, playerwk *pwp);

void func_8C0B70E8(taskwk *twp, playerwk *pwp)
{
    twp->mode = 1;
    pwp->waittimer = 0;
    func_8C0BAFC0(twp, pwp);
}
