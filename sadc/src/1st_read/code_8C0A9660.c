#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0A9660: Sonic "return to standing" helper (chunk 8C0A9660-8C0AE760,
 * function 1/8; SonicModeMachine and the stand selectors are separate assignments).
 *
 * func_8C0A9660 puts the player back into mode 1 (standing), clears the idle
 * timer and tail-calls the stand-action selector func_8C0AE3D6. Called from
 * SonicModeMachine and several Sonic action handlers (8C0AFF1A, 8C0B0F24, ...).
 */

extern void func_8C0AE3D6(taskwk *twp, motionwk *mwp, playerwk *pwp);

void func_8C0A9660(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    twp->mode = 1;
    pwp->waittimer = 0;
    func_8C0AE3D6(twp, mwp, pwp);
}
