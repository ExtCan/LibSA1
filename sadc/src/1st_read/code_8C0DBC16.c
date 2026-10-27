#include "sa1/sa1.h"

/*
 * Amy (1ST_READ 8C0DBC16-8C0DC1E2, chunk 8C0D6B40, functions 24-25), called
 * by AmyModeMachine (8C0D7960) with (twp, mwp, pwp); both return 1 when they
 * changed the player's mode, else 0.
 *  func_8C0DBC16  request handler (Sonic: 8C0AFF1A, E-102: 8C0D079A): while
 *                 the event work locks the player (ewp->lock) it runs the
 *                 event helper 8C0DB67E and returns 0; otherwise, when an
 *                 action was requested from outside (TWF_REQUEST, set by
 *                 SetInputP together with twp->smode), it switches on the
 *                 requested action twp->smode (1-50), drops a held object
 *                 where needed (8C0DC75E) and sets the new mode and motion.
 *  func_8C0DC120  action button pressed while touching an object in front
 *                 (Sonic: 8C0B16BC): 0x400 objects are taken at once (mode
 *                 26, action 61, the hit task becomes pwp->htp, flag 0x800);
 *                 0x200 objects start the lift (mode 23, action 43, speed
 *                 capped at jog speed); 0x800 objects mode 26, action 61.
 */

/* Amy event-lock handler (8C0DB67E, BSR from here; earlier in the chunk) */
ASM_STUB(Sint32 func_8C0DB67E(taskwk *twp, motionwk *mwp, playerwk *pwp))
/* Amy: release the held object (8C0DC75E, BSR from here; later in the chunk;
 * Sonic 8C0B1D60) */
void func_8C0DC75E(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* object classification of the touched object (code_8C06C880.c) */
extern Sint32 func_8C06C880(taskwk *twp);
/* angle class 1..4 towards the hit object (code_8C06CCA0.c defines it with
 * (taskwk *)); the player callers pass mwp in R5 as well */
extern Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);

#ifdef NON_MATCHING
/*
 * Byte-identical to the original: all 509 instructions match and the 50
 * entries of the switch jump table give the same offsets (label differences
 * of the compiled .src compared entry by entry with the halfwords at
 * 8C0DBC9C). tools/shcdiff.py cannot evaluate the ".DATA.W Lcase-Lbase"
 * jump-table entries behind the MOVA and always reports that one instruction
 * as differing, which would fail the splice step; hence the guard (same
 * situation as func_8C0AFF1A and func_8C0D079A).
 * Source-shape notes: cases in source order (not numeric; 9, 10 and 12 are
 * separate identical blocks, as the original keeps three copies); the angle
 * resets are chained (twp->ang.x = twp->ang.z = 0, z stored first) and the
 * speed resets chained x = y = z (z stored first).
 */
Sint32 func_8C0DBC16(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (EWP(twp)->lock) {
        func_8C0DB67E(twp, mwp, pwp);
        return 0;
    }
    if (!(twp->flag & TWF_REQUEST)) {
        return 0;
    }
    switch (twp->smode) {
    case 3:
    case 6:
    case 7:
    case 8:
        twp->mode = 32;
        pwp->mj.reqaction = 63;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 4:
        twp->mode = 33;
        pwp->mj.reqaction = 63;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 11:
        twp->mode = 30;
        pwp->mj.reqaction = 65;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 1:
    case 2:
        func_8C0DC75E(twp, mwp, pwp);
        twp->mode = 3;
        pwp->mj.reqaction = 11;
        pwp->hpos = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag |= TWF_ONPATH;
        break;
    case 12:
        twp->mode = 13;
        pwp->mj.reqaction = 47;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 13:
    case 15:
        func_8C0DC75E(twp, mwp, pwp);
        twp->mode = 13;
        pwp->mj.reqaction = 22;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 16:
        twp->mode = 13;
        pwp->mj.reqaction = 55;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 17:
        twp->mode = 13;
        pwp->mj.reqaction = 18;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 18:
        twp->mode = 48;
        pwp->mj.reqaction = 76;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~TWF_ONPATH;
        break;
    case 19:
        func_8C0DC75E(twp, mwp, pwp);
        twp->mode = 13;
        if (GetStageNumber() < 0xF00) {
            pwp->mj.reqaction = 32;
        } else {
            pwp->mj.reqaction = 42;
        }
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 20:
        func_8C0DC75E(twp, mwp, pwp);
        twp->mode = 13;
        pwp->mj.reqaction = 77;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 23:
        func_8C0DC75E(twp, mwp, pwp);
        twp->mode = 13;
        pwp->mj.reqaction = 32;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 9:
        twp->mode = 13;
        pwp->mj.reqaction = 47;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 10:
        twp->mode = 13;
        pwp->mj.reqaction = 47;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 24:
    case 25:
    case 43:
        twp->mode = 25;
        pwp->mj.reqaction = 47;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 26:
        if (twp->mode == 27 || pwp->spd.x > pwp->p.run_speed) {
            pwp->hpos = 0.0f;
            twp->flag |= TWF_ONPATH;
        }
        return 0;
    case 31:
        if (twp->flag & TWF_GROUND) {
            return 0;
        }
        twp->mode = 40;
        pwp->mj.reqaction = 72;
        pwp->mj.nframe = 0.0f;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag |= TWF_ONPATH;
        break;
    case 37:
        twp->mode = 25;
        pwp->mj.reqaction = 47;
        pwp->htp = twp->value.tp;
        pwp->htp->twp->flag |= TWF_REQUEST;
        twp->flag |= TWF_HOLD;
        twp->flag &= ~8;
        twp->flag &= ~0x500;
        break;
    case 33:
        func_8C0DC75E(twp, mwp, pwp);
        twp->mode = 44;
        pwp->mj.reqaction = 75;
        pwp->free.w[5] = 180;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        func_8C0940DA(0x503, 0, 0, 0);
        break;
    case 39:
        func_8C0DC75E(twp, mwp, pwp);
        twp->mode = 46;
        pwp->mj.reqaction = 37;
        break;
    case 50:
        func_8C0DC75E(twp, mwp, pwp);
        twp->mode = 47;
        pwp->mj.reqaction = 26;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    default:
        return 0;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C0DBC16(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

Sint32 func_8C0DC120(taskwk *twp, motionwk *mwp, playerwk *pwp)
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
                    twp->mode = 26;
                    pwp->mj.reqaction = 61;
                    pwp->htp = CWP(twp)->hit_cwp->mytask;
                    twp->flag |= TWF_HOLD;
                } else if (kind & 0x200) {
                    twp->mode = 23;
                    pwp->mj.reqaction = 43;
                    if (pwp->spd.x >= pwp->p.jog_speed) {
                        pwp->spd.x = pwp->p.jog_speed;
                    }
                } else if (kind & 0x800) {
                    twp->mode = 26;
                    pwp->mj.reqaction = 61;
                } else {
                    break;
                }
                return 1;
            }
        }
    }
    return 0;
}

ASM_STUB(void func_8C0DC75E(taskwk *twp, motionwk *mwp, playerwk *pwp))
