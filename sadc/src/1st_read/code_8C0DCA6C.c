/*
 * 1ST_READ 8C0DCA6C-8C0DCBA0: Amy mode helpers (all called by
 * AmyModeMachine 8C0D7960 with (twp, mwp, pwp); they return 1 when they
 * changed the player's mode, else 0).
 *
 *  func_8C0DCA6C  no stick input (GetAnalog) and no forward speed: while
 *                 holding an object (twp->flag 0x800) put it down (object
 *                 class & 0xC00: mode 26, action 62; else mode 25, action
 *                 47), otherwise back to standing (8C0DA6E0)
 *  func_8C0DCAE0  button D_8C19E38C pressed: mode 21, action 89, SE 0x49B
 *  func_8C0DCB22  button D_8C19E38C pressed: action 82 chains to action 84
 *                 (AmyModeMachine sets 83 at 8C0D885C), speed x/z cleared,
 *                 free.b[1] = 0, mode 19, action 81, SE 0x49A
 */
#include "common.h"
#include "sa1/sa1.h"

/* Amy: back to standing (twp->mode = 1, pwp->waittimer = 0; 8C0DA6E0) */
extern void func_8C0DA6E0(taskwk *twp, motionwk *mwp, playerwk *pwp);

Sint32 func_8C0DCA6C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (GetAnalog(twp, NULL, NULL) == 0 && pwp->spd.x == 0.0f) {
        if (twp->flag & 0x800) {
            if (func_8C06C880(twp) & 0xC00) {
                twp->mode = 26;
                pwp->mj.reqaction = 62;
            } else {
                twp->mode = 25;
                pwp->mj.reqaction = 47;
            }
        } else {
            func_8C0DA6E0(twp, mwp, pwp);
        }
        return 1;
    }
    return 0;
}

Sint32 func_8C0DCAE0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        twp->mode = 21;
        pwp->mj.reqaction = 89;
        func_8C0940DA(0x49B, 0, 0, 0);
        return 1;
    }
    return 0;
}

/*
 * The action-list store has to be evaluated inside the spd.x assignment
 * (comma expression): written as its own statement (in any statement order
 * that keeps the original store order) SHC allocates the scratch registers
 * differently (pool pointer R1, 0 in R3, 19 in R2). The original pool holds
 * D_8C4BB6F0 = &AmyActionList[82] and stores a halfword at +6 (MOV.W: next
 * and the following pad byte; the header has PL_ACTION.next as Uint8
 * because PSetMotion reads it with MOV.B); on PC (little endian,
 * -fno-strict-aliasing) the same store sets next = 84 and pad07 = 0.
 */
Sint32 func_8C0DCB22(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        pwp->spd.x = (*(Sint16 *)&AmyActionList[82].next = 84, 0.0f);
        pwp->spd.z = 0.0f;
        pwp->free.b[1] = 0;
        twp->mode = 19;
        pwp->mj.reqaction = 81;
        func_8C0940DA(0x49A, 0, 0, 0);
        return 1;
    }
    return 0;
}
