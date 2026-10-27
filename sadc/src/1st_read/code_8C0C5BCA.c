#include "sa1/sa1.h"

/*
 * Knuckles (1ST_READ chunk 8C0C5680-8C0C71A0, function 8 of 18):
 *
 *  func_8C0C5BCA  Knuckles' "next action" handler (Sonic: 8C0AFF1A,
 *                 Tails: 8C0BBDD6, E-102: 8C0D079A, Amy: 8C0DBC16; SADX
 *                 Knuckles_NAct analogue), called by KnucklesModeMachine
 *                 (8C0C193A) with (twp, mwp, pwp).
 *                 While the event work locks the player (ewp->lock) it runs
 *                 the event-lock helper func_8C0C5B3A and returns 0; an
 *                 event path (ewp->list4C) forces mode 49. Otherwise, when
 *                 an action was requested from outside (TWF_REQUEST, set by
 *                 SetInputP together with twp->smode), it switches on the
 *                 requested action twp->smode (1-50), sets the new mode and
 *                 motion request (pwp->mj.reqaction) and returns 1 (0 for an
 *                 ignored request).
 */

/* Knuckles: glide/dash end (code_8C0C5680.c; BSR from here) */
ASM_STUB(void func_8C0C5792(taskwk *twp, motionwk *mwp, playerwk *pwp))
/* Knuckles event-lock helper (code_8C0C5B3A.c; BSR from here) */
ASM_STUB(Sint32 func_8C0C5B3A(taskwk *twp, motionwk *mwp, playerwk *pwp))

/* Knuckles: back to standing (Sonic func_8C0A9660; other chunk, JSR) */
extern void func_8C0C4F40(taskwk *twp, motionwk *mwp, playerwk *pwp);

#ifdef NON_MATCHING
/*
 * Byte-identical to the original: all 676 instructions match and the 50
 * entries of the switch jump table give the same offsets (label differences
 * of the compiled .src compared entry by entry with the halfwords at
 * 8C0C5C5C). tools/shcdiff.py cannot evaluate the ".DATA.W Lcase-Lbase"
 * jump-table entries behind the MOVA and always reports that one instruction
 * as differing, which would fail the splice step; hence the guard (same
 * situation as func_8C0AFF1A, func_8C0D079A and func_8C0DBC16).
 * Source-shape notes (as in Sonic's version): cases in source order (not
 * numeric); angle resets chained (ang.x = ang.z = 0, z stored first), speed
 * resets chained x = y = z (z stored first); case 38 must be a guarded block
 * ending in break (a "return 1" there is tail-merged with the ewp->list4C
 * exit).
 */
Sint32 func_8C0C5BCA(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (EWP(twp)->lock) {
        func_8C0C5B3A(twp, mwp, pwp);
        return 0;
    }
    if (EWP(twp)->list4C != NULL) {
        twp->mode = 49;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        return 1;
    }
    if (!(twp->flag & TWF_REQUEST)) {
        return 0;
    }
    switch (twp->smode) {
    case 3:
    case 8:
        twp->mode = 7;
        pwp->mj.reqaction = 17;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 6:
        twp->mode = 7;
        pwp->mj.reqaction = 76;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 7:
        twp->mode = 7;
        pwp->mj.reqaction = 78;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 4:
        twp->mode = 8;
        pwp->mj.reqaction = 17;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 11:
        twp->mode = 9;
        pwp->mj.reqaction = 30;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 1:
        twp->mode = 3;
        pwp->mj.reqaction = 11;
        pwp->hpos = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag |= TWF_ONPATH;
        break;
    case 12:
        twp->mode = 16;
        pwp->mj.reqaction = 0;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 13:
        twp->mode = 16;
        pwp->mj.reqaction = 14;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag |= 0x500;
        pwp->work.f = 2.0f;
        func_8C0940DA(0x2D8, 0, 0, 0);
        twp->flag &= ~TWF_ONPATH;
        break;
    case 15:
        twp->mode = 16;
        pwp->mj.reqaction = 23;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 16:
        twp->mode = 16;
        pwp->mj.reqaction = 84;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 17:
        twp->mode = 16;
        pwp->mj.reqaction = 19;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 18:
        twp->mode = 52;
        pwp->mj.reqaction = 36;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 19:
        twp->mode = 16;
        pwp->mj.reqaction = 39;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 20:
        twp->mode = 16;
        pwp->mj.reqaction = 107;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 21:
        twp->mode = 16;
        pwp->mj.reqaction = 109;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 22:
        twp->mode = 16;
        pwp->mj.reqaction = 110;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 9:
        twp->mode = 16;
        pwp->mj.reqaction = 81;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 10:
        twp->mode = 16;
        pwp->mj.reqaction = 82;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 24:
        func_8C0C4F40(twp, mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 26:
        if (twp->mode == 2 || pwp->spd.x > pwp->p.run_speed) {
            pwp->hpos = 0.0f;
            if (twp->flag & 0x100) {
                twp->flag &= ~0x500;
            }
            twp->flag |= TWF_ONPATH;
        }
        return 0;
    case 29:
        twp->mode = 42;
        pwp->mj.reqaction = 101;
        pwp->mj.nframe = 16.0f;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag |= TWF_ONPATH;
        break;
    case 31:
        if (twp->flag & TWF_GROUND) {
            return 0;
        }
        twp->mode = 44;
        pwp->mj.reqaction = 104;
        pwp->mj.nframe = 0.0f;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag |= TWF_ONPATH;
        break;
    case 38:
        if (twp->mode != 48) {
            twp->mode = 48;
            pwp->mj.reqaction = 37;
            pwp->free.w[6] = 300;
            func_8C0B60B8((Uint8)twp->counter.b[0]);
            PClearSpeed(mwp, pwp);
            if (twp->flag & 0x100) {
                twp->flag &= ~0x500;
            }
            twp->flag &= ~TWF_ONPATH;
        }
        break;
    case 35:
        twp->mode = 17;
        pwp->mj.reqaction = 31;
        if (twp->counter.b[0] == 0) {
            func_8C0940DA(0x479, 0, 0, 0);
        }
        func_8C07B320(twp);
        twp->flag &= ~0x2500;
        break;
    case 37:
        twp->mode = 29;
        pwp->mj.reqaction = 74;
        pwp->htp = twp->value.tp;
        pwp->htp->twp->flag |= TWF_REQUEST;
        twp->flag |= TWF_HOLD;
        twp->flag &= ~8;
        twp->flag &= ~0x2500;
        break;
    case 48:
        twp->mode = 11;
        pwp->mj.reqaction = 51;
        func_8C0C5792(twp, mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag |= 0x400;
        pwp->free.b[5] = 0;
        return 1;
    case 39:
        twp->mode = 50;
        pwp->mj.reqaction = 38;
        twp->flag &= ~0x2500;
        break;
    case 50:
        twp->mode = 51;
        pwp->mj.reqaction = 27;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    default:
        return 0;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C0C5BCA(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
