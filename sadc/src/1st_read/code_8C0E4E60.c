#include "sa1/sa1.h"

/*
 * Big (1ST_READ chunk 8C0DFFA0-8C0E59C0, functions 22-24 of 29): checks of
 * BigModeMachine (8C0E0E70) for grabbing, lifting and carrying objects.
 * Big's versions of Tails' 8C0BCDD0 / 8C0BCEC0 (code_8C0BCBCA.c) and Amy's
 * 8C0DC1E2 / 8C0DC2A0 (code_8C0DC1E2.c) with his own mode and action
 * numbers and SE 0x4B4. All take (twp, mwp, pwp) and return 1 when they
 * changed the player's mode.
 *
 *  func_8C0E4E60  action button pressed (press & D_8C19E3A0) next to an
 *                 object (func_8C06C880 kind) at any angle class 1..4
 *                 (func_8C06CF60): 0x400 objects are grabbed at once
 *                 (mode 18, action 35, pwp->htp = the hit task, flag
 *                 0x800); 0x200 objects start the lift (mode 15, action 78
 *                 for objects of class 4, else 26); 0x800 objects: mode 18,
 *                 action 35 without taking the task.
 *  func_8C0E4F28  lift (mode 15): without the CCL push-back (8C06CB40)
 *                 back to standing (8C0E65BE) with htp cleared and flag
 *                 0x800 off; from frame 8 a 0x200 object straight ahead
 *                 (func_8C06CF60 == 4) is taken: mode 17, SE 0x4B4, the
 *                 hit task becomes pwp->htp (its taskwk flag 0x1000),
 *                 twp->flag 0x800 and ~8; otherwise back to standing.
 *  func_8C0E501C  carrying: the action button throws (in the air: mode 27,
 *                 action 43/44; on the ground mode 20, action 41/42, or a
 *                 put-down, mode 16, action 80 for class-4 objects else 45,
 *                 when slower than jog speed); the action depends on whether
 *                 the target list is empty (func_8C06D118). When the object
 *                 is gone (flag 0x800 clear): standing (8C0E65BE) or
 *                 falling (mode 8, action 14) and the held task is released.
 *
 * Matching notes: 8C0E4E60 needs the signed button test (as Tails'
 * 8C0BCDD0; R2 for the 8C06C880 address) and its own "return 1" in the
 * 0x200 branch (the original leaves "store; BRA; NOP" after action 26).
 * 8C0E4F28 needs the "else return 0" shape of Amy's 8C0DC1E2 (register
 * order of the second back-to-standing block) and has 12 unused bytes in
 * its frame (an unused NJS_POINT3, SHC only).
 */

/* angle class 1..4 towards the hit object (code_8C06CCA0.c); the callers
 * pass mwp as a second argument */
extern Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);
/* Big: back to standing (8C0E65BE, other chunk) */
extern void func_8C0E65BE(taskwk *twp, motionwk *mwp, playerwk *pwp);

Sint32 func_8C0E4E60(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 kind;

    if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].press & (Sint32)D_8C19E3A0) {
        if ((kind = func_8C06C880(twp)) != 0) {
            switch (func_8C06CF60(twp, mwp)) {
            case 4:
            case 3:
            case 2:
            case 1:
                if (kind & 0x400) {
                    twp->mode = 18;
                    pwp->mj.reqaction = 35;
                    pwp->htp = CWP(twp)->hit_cwp->mytask;
                    twp->flag |= 0x800;
                } else if (kind & 0x200) {
                    twp->mode = 15;
                    if (CWP(twp)->hit_cwp->mytask->twp->id == 4) {
                        pwp->mj.reqaction = 78;
                    } else {
                        pwp->mj.reqaction = 26;
                    }
                    return 1;
                } else if (kind & 0x800) {
                    twp->mode = 18;
                    pwp->mj.reqaction = 35;
                } else {
                    break;
                }
                return 1;
            }
        }
    }
    return 0;
}

Sint32 func_8C0E4F28(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    SH_ONLY(NJS_POINT3 v;) /* unused: the original frame has 12 more bytes below kind */
    Sint32 kind;

    if (func_8C06CB40(twp, mwp) == 0) {
        func_8C0E65BE(twp, mwp, pwp);
        pwp->htp = NULL;
        twp->flag &= ~0x800;
        return 0;
    }
    if (pwp->mj.nframe >= 8.0f) {
        if ((kind = func_8C06C880(twp)) != 0 && func_8C06CF60(twp, mwp) == 4) {
            if (kind & 0x200) {
                twp->mode = 17;
                func_8C0940DA(0x4B4, 0, 0, 0);
                pwp->htp = CWP(twp)->hit_cwp->mytask;
                pwp->htp->twp->flag |= 0x1000;
                twp->flag |= 0x800;
                twp->flag &= ~8;
                return 1;
            }
        }
    } else {
        return 0;
    }
    func_8C0E65BE(twp, mwp, pwp);
    pwp->htp = NULL;
    twp->flag &= ~0x800;
    return 0;
}

Sint32 func_8C0E501C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E3A0) {
        if (!(twp->flag & 3)) {
            twp->mode = 27;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 43;
            } else {
                pwp->mj.reqaction = 44;
            }
        } else if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 16;
            if (pwp->htp->twp->id == 4) {
                pwp->mj.reqaction = 80;
            } else {
                pwp->mj.reqaction = 45;
            }
            PClearSpeed(mwp, pwp);
        } else {
            twp->mode = 20;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 41;
            } else {
                pwp->mj.reqaction = 42;
            }
        }
        return 1;
    } else if (!(twp->flag & 0x800)) {
        if (twp->flag & 3) {
            func_8C0E65BE(twp, mwp, pwp);
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 14;
        }
        if (pwp->htp) {
            pwp->htp->twp->flag &= ~0x1000;
            pwp->htp = NULL;
        }
        return 1;
    }
    return 0;
}
