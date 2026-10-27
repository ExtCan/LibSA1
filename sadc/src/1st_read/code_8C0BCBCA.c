/*
 * 1ST_READ 8C0BCBCA-8C0BCF56 (chunk 8C0BAFC0-8C0BDC80, functions 13-18 of
 * 29): Tails mode-machine checks, all called by TailsModeMachine (8C0B70F4);
 * each returns 1 when it changed the player's mode, else 0.
 *
 *  func_8C0BCBCA  (twp, mwp, pwp) requests from outside (twp->flag 0x1000,
 *                 twp->smode): 24 -> mode 10 / action 19 with p.height 9
 *                 and center_height = height / 2; 19 -> mode 62, action
 *                 54/56 by func_8C0818DE, angles and speeds cleared
 *                 (PClearSpeed written out), twp->timer.b[3] |= 8.
 *  func_8C0BCC9A  (twp, pwp) leaving the ground state: flag 0x100 -> mode
 *                 6; holding an object (flag 0x800): 0x200 objects mode 28
 *                 action 82, else release it (func_8C0BD440); then mode 10
 *                 action 19.
 *  func_8C0BCD00  (twp, pwp) no stick input and spd.x == 0: holding a
 *                 0xC00 object -> mode 24 action 79, other held objects
 *                 mode 23 action 62; not holding: back to standing
 *                 (func_8C0B70E8).
 *  func_8C0BCDA0  (twp, pwp) slower than run speed: mode 2 (moving
 *                 forward) or 1, action 36.
 *  func_8C0BCDD0  (twp, mwp, pwp) action button (press & D_8C19E3A0) at an
 *                 object (Sonic's func_8C0B16BC, code_8C0B16BC.c): 0x400
 *                 objects are taken at once (mode 24, action 78, htp), 0x200
 *                 objects start the lift (mode 21, action 58, speed capped
 *                 at jog speed), 0x800 objects mode 24 action 78.
 *  func_8C0BCEC0  (twp, mwp, pwp) lift (Sonic's func_8C0B17AE): from frame
 *                 12 a 0x200 object straight ahead (func_8C06CF60 == 4) is
 *                 taken: mode 23, SE 0x46A, the hit task becomes pwp->htp
 *                 (its flag 0x1000), twp->flag 0x800; otherwise back to
 *                 standing (func_8C0B70E8).
 *
 * Matching notes (all six match alone):
 *  - 8C0BCC9A: the flag 0x100 branch needs its own "return 1" inside an
 *    if/else (BRA + NOP delay slot, R2 for the 0x800 constant).
 *  - 8C0BCDA0: both branches store reqaction 36 (SHC hoists the constant
 *    into R6 before the branch); "spd.x <= 0.0f" picks mode 1 first.
 *  - 8C0BCDD0: the button test needs Sint32 operands (as Sonic's
 *    8C0B16BC and 8C06CEBE).
 */
#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/* angle class 1..4 towards the hit object (code_8C06CCA0.c); the callers
 * pass mwp as a second argument */
extern Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);
/* Tails: back to standing (code_8C0B70E8.c) */
extern void func_8C0B70E8(taskwk *twp, playerwk *pwp);
/* Tails SE: func_8C0940DA(se, 0, 0, 0) unless twp->counter.b[0] == 1 (code_8C0BBA4E.c) */
extern void func_8C0BFCA2(taskwk *twp, Sint32 se);
/* code_8C0817E0.c */
extern Sint32 func_8C0818DE(void);
/* Tails: release the held object (same chunk, defined later: BSR) */
void func_8C0BD440(taskwk *twp, playerwk *pwp);

Sint32 func_8C0BCBCA(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!(twp->flag & 0x1000)) {
        return 0;
    }
    switch (twp->smode) {
    case 24:
        twp->mode = 10;
        pwp->mj.reqaction = 19;
        pwp->p.height = 9.0f;
        pwp->p.center_height = pwp->p.height * 0.5f;
        break;
    case 19:
        twp->mode = 62;
        if (func_8C0818DE() == 1) {
            pwp->mj.reqaction = 54;
        } else {
            pwp->mj.reqaction = 56;
        }
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->timer.b[3] |= 8;
        break;
    }
    return 1;
}

Sint32 func_8C0BCC9A(taskwk *twp, playerwk *pwp)
{
    if (!(twp->flag & 3)) {
        if (twp->flag & 0x100) {
            twp->mode = 6;
            return 1;
        } else {
            if (twp->flag & 0x800) {
                if (func_8C06C880(twp) & 0x200) {
                    twp->mode = 28;
                    pwp->mj.reqaction = 82;
                    return 1;
                }
                func_8C0BD440(twp, pwp);
            }
            twp->mode = 10;
            pwp->mj.reqaction = 19;
        }
        return 1;
    }
    return 0;
}

Sint32 func_8C0BCD00(taskwk *twp, playerwk *pwp)
{
    if (GetAnalog(twp, NULL, NULL) == 0 && pwp->spd.x == 0.0f) {
        if (twp->flag & 0x800) {
            if (func_8C06C880(twp) & 0xC00) {
                twp->mode = 24;
                pwp->mj.reqaction = 79;
            } else {
                twp->mode = 23;
                pwp->mj.reqaction = 62;
            }
        } else {
            func_8C0B70E8(twp, pwp);
        }
        return 1;
    }
    return 0;
}

Sint32 func_8C0BCDA0(taskwk *twp, playerwk *pwp)
{
    if (pwp->spd.x < pwp->p.run_speed) {
        if (pwp->spd.x <= 0.0f) {
            twp->mode = 1;
            pwp->mj.reqaction = 36;
        } else {
            twp->mode = 2;
            pwp->mj.reqaction = 36;
        }
        return 1;
    }
    return 0;
}

Sint32 func_8C0BCDD0(taskwk *twp, motionwk *mwp, playerwk *pwp)
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
                    twp->mode = 24;
                    pwp->mj.reqaction = 78;
                    pwp->htp = CWP(twp)->hit_cwp->mytask;
                    twp->flag |= 0x800;
                } else if (kind & 0x200) {
                    twp->mode = 21;
                    pwp->mj.reqaction = 58;
                    if (pwp->spd.x >= pwp->p.jog_speed) {
                        pwp->spd.x = pwp->p.jog_speed;
                    }
                } else if (kind & 0x800) {
                    twp->mode = 24;
                    pwp->mj.reqaction = 78;
                } else {
                    break;
                }
                return 1;
            }
        }
    }
    return 0;
}

Sint32 func_8C0BCEC0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 kind;

    func_8C06CB40(twp, mwp);
    if (pwp->mj.nframe >= 12.0f) {
        if ((kind = func_8C06C880(twp)) != 0 && func_8C06CF60(twp, mwp) == 4) {
            if (kind & 0x200) {
                twp->mode = 23;
                func_8C0BFCA2(twp, 0x46A);
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
    func_8C0B70E8(twp, pwp);
    return 0;
}

ASM_STUB(void func_8C0BD440(taskwk *twp, playerwk *pwp))
