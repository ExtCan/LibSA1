#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0CBE00):
 *   func_8C0CD09C  set mode 1 (likely standing): twp->mode = 1, reset the
 *                  idle timer and tail-call the stand/idle handler
 *                  8C0D1E38 (which tests pwp->waittimer > 1800).
 * Callers: E102ModeMachine, 8C0D079A, 8C0D0E8C, 8C0D100A, 8C0D14D6, 8C0D1672.
 */

/* E-102 stand/idle handler (8C0D1E38): R4 twp (kept on the stack),
 * R6 pwp (reads pwp->floor_normal, pwp->mj, pwp->waittimer) */
void func_8C0D1E38(taskwk *twp, motionwk *mwp, playerwk *pwp);

void func_8C0CD09C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    twp->mode = 1;
    pwp->waittimer = 0;
    func_8C0D1E38(twp, mwp, pwp);
}
