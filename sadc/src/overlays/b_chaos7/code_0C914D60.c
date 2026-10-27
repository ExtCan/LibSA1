/*
 * B_CHAOS7 0C914D60 (Perfect Chaos stage): chunk 0C914D60-0C9150A0,
 * function 1 of 9 (func_0C914D8E.. are still assembly).
 *
 *  func_0C914D60  the Super Sonic trigger. A child task of the stage init
 *                 0C914DC6 (CreateChildTask(6, func_0C914D60, tp) at
 *                 0C914DF4-0C914DFC). It counts its executions in
 *                 twp->timer.l; on the second one it sends request 46 to
 *                 player 0 (SetInputP: the player's request handler
 *                 func_8C0AFF1A / func_8C0B07E4 case 46 sets
 *                 equipment |= 0x8000, creates the aura task 0C900480 and
 *                 the physics task 8C0B32B6, mode 71 / action 130) and
 *                 destroys itself (DestroyTask tail call).
 *                 super_sonic.md 1.1 and Review R.1.
 */
#include "common.h"
#include "sa1/sa1.h"

void func_0C914D60(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (++twp->timer.l > 1) {
        SetInputP(0, 46);
        DestroyTask(tp);
    }
}
