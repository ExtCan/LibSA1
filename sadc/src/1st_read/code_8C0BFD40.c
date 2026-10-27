/*
 * 1ST_READ 8C0BFD40-8C0BFDDE (chunk 8C0BEDA0, functions 14-18 of 18): Tails.
 *  func_8C0BFD40  called by TailsModeMachine 8C0B70F4 with the player twp:
 *                 plays sound 15 unless the player number is 1.
 *  func_8C0BFD52  sets bit 2 of byte 0x0F (timer.b[3]) of player pno's taskwk;
 *  func_8C0BFD6A  identical copy of func_8C0BFD52. No direct callers.
 *  func_8C0BFD82  creates a timer task (CreateElementalTask(8, 1, func_8C0BFDAC))
 *                 with awp->work[0] = 6 frames and awp->work[1] = value.
 *  func_8C0BFDAC  its exec: counts work[0] down; at 0 passes work[1] to
 *                 func_8C107BA0 and destroys itself.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

extern void func_8C107BA0(Sint32 a);

void func_8C0BFDAC(task *tp);

void func_8C0BFD40(taskwk *twp)
{
    if ((Uint8)twp->counter.b[0] != 1) {
        func_8C0945A0(15);
    }
}

Sint8 func_8C0BFD52(Sint32 pno)
{
    taskwk *twp;

    twp = playertwp[pno];
    if (twp == NULL) {
        return 0;
    }
    return twp->timer.b[3] |= 4;
}

Sint8 func_8C0BFD6A(Sint32 pno)
{
    taskwk *twp;

    twp = playertwp[pno];
    if (twp == NULL) {
        return 0;
    }
    return twp->timer.b[3] |= 4;
}

void func_8C0BFD82(Sint32 a)
{
    task *tp;

    if ((tp = CreateElementalTask(8, 1, func_8C0BFDAC)) != NULL) {
        AWP(tp)->work[0].l = 6;
        AWP(tp)->work[1].l = a;
    }
}

void func_8C0BFDAC(task *tp)
{
    anywk *awp;

    awp = AWP(tp);
    awp->work[0].l--;
    if (awp->work[0].l <= 0) {
        func_8C107BA0(awp->work[1].l);
        DestroyTask(tp);
    }
}
