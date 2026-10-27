#include "sa1/sa1.h"

/*
 * Knuckles (1ST_READ chunk 8C0C5680-8C0C71A0, function 7 of 18):
 *
 *  func_8C0C5B3A  event lock helper (Knuckles counterpart of Sonic's
 *                 func_8C0AFE72, E-102's func_8C0D06F4, Amy's func_8C0DB67E
 *                 and Big's func_8C0E43CA): event lock 1..4 sets twp->flag
 *                 0x4000; lock 6 / 5 rotate the player towards the event
 *                 angle (absolute / relative to ang.y) via 8C07C298; any
 *                 other lock clears 0x4000 and returns 0.
 *                 Called (BSR) by func_8C0C5BCA and func_8C0C61F4 with
 *                 (twp, mwp, pwp); only twp is used.
 */

Sint32 func_8C0C5B3A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    eventwk *ewp;

    ewp = EWP(twp);
    switch ((Uint8)ewp->lock) {
    case 2:
    case 3:
    case 4:
    case 1:
        twp->flag |= TWF_EVENTLOCK;
        break;
    case 6:
        func_8C07C298(twp->counter.b[0], ewp->ang[1]);
        break;
    case 5:
        func_8C07C298(twp->counter.b[0], ewp->ang[1] + twp->ang.y);
        break;
    default:
        twp->flag &= ~TWF_EVENTLOCK;
        return 0;
    }
    return 1;
}
