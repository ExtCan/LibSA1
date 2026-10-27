#include "sa1/sa1.h"

/*
 * Knuckles (1ST_READ chunk 8C0C5680-8C0C71A0, function 9 of 18):
 *
 *  func_8C0C61F4  Knuckles' second "next action" handler, called by
 *                 KnucklesModeMachine (8C0C193A) with (twp, mwp, pwp).
 *                 While the event work locks the player (ewp->lock) it runs
 *                 the event-lock helper func_8C0C5B3A and returns 0.
 *                 Otherwise, when an action was requested from outside
 *                 (TWF_REQUEST, set by SetInputP together with twp->smode),
 *                 it switches on the requested action twp->smode (1-50),
 *                 usually drops the held object first (func_8C0C6DE0),
 *                 sets the new mode and motion request (pwp->mj.reqaction)
 *                 and returns 1 (0 for an ignored request; request 26 only
 *                 clears hpos and sets TWF_ONPATH when in mode 31 or faster
 *                 than run_speed, and returns 0).
 */

/* Same-chunk callees reached with BSR (stubs at the end of this file):
 * glide/dash end (code_8C0C5680.c), the event-lock helper (code_8C0C5B3A.c)
 * and "drop the held object" (8C0C6DE0). */
void func_8C0C5792(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0C5B3A(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0C6DE0(taskwk *twp, motionwk *mwp, playerwk *pwp);

#ifdef NON_MATCHING
/*
 * Byte-identical to the original: all 600 instructions match and the 50
 * entries of the switch jump table give the same offsets (checked entry by
 * entry: label differences in the compiled listing vs the halfwords at
 * 8C0C6280). tools/shcdiff.py cannot evaluate the ".DATA.W Lcase-Lbase"
 * jump-table entries behind the MOVA and always reports that one
 * instruction as differing, which would fail the splice step; hence the
 * guard (same as Sonic's func_8C0AFF1A, Amy's func_8C0DBC16, Big's
 * func_8C0E488C).
 * Verify: py -3.12 tools/shcdiff.py func_8C0C61F4 src/1st_read/code_8C0C61F4.c
 *         --flags=-define=NON_MATCHING   (only the MOVA differs)
 * Matching notes: the cases are in source (code) order, not numeric order;
 * cases 9, 10 and 12 are separate identical bodies; requests 9, 10, 12,
 * 13, 15-17, 19, 29 and 31 clear mwp->spd and pwp->spd inline (chained
 * "x = y = z = 0.0f", z stored first) while 18, 20 and 38 call PClearSpeed;
 * case 38 is a guarded block ending in break.
 * Knuckles' playerwk.free use seen here: free.b[5] (0x81) cleared by request
 * 48, free.w[6] (0x88) = 300 by request 38.
 */
Sint32 func_8C0C61F4(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (EWP(twp)->lock) {
        func_8C0C5B3A(twp, mwp, pwp);
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
        twp->mode = 36;
        pwp->mj.reqaction = 92;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 4:
        twp->mode = 37;
        pwp->mj.reqaction = 92;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 11:
        twp->mode = 34;
        pwp->mj.reqaction = 94;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 1:
        func_8C0C6DE0(twp, mwp, pwp);
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
        pwp->mj.reqaction = 74;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 13:
        func_8C0C6DE0(twp, mwp, pwp);
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
        func_8C0C6DE0(twp, mwp, pwp);
        twp->mode = 16;
        pwp->mj.reqaction = 23;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 16:
        twp->mode = 16;
        pwp->mj.reqaction = 84;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 17:
        twp->mode = 16;
        pwp->mj.reqaction = 19;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 18:
        twp->mode = 52;
        pwp->mj.reqaction = 36;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~TWF_ONPATH;
        break;
    case 19:
        func_8C0C6DE0(twp, mwp, pwp);
        twp->mode = 16;
        pwp->mj.reqaction = 39;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 20:
        func_8C0C6DE0(twp, mwp, pwp);
        twp->mode = 16;
        pwp->mj.reqaction = 107;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 9:
        twp->mode = 16;
        pwp->mj.reqaction = 74;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 10:
        twp->mode = 16;
        pwp->mj.reqaction = 74;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 24:
        twp->mode = 29;
        pwp->mj.reqaction = 74;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 26:
        if (twp->mode == 31 || pwp->spd.x > pwp->p.run_speed) {
            pwp->hpos = 0.0f;
            twp->flag |= TWF_ONPATH;
        }
        return 0;
    case 29:
        func_8C0C6DE0(twp, mwp, pwp);
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
        if (twp->flag & 3) {
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
            func_8C0C6DE0(twp, mwp, pwp);
            twp->mode = 48;
            pwp->mj.reqaction = 37;
            pwp->free.w[6] = 300;
            PClearSpeed(mwp, pwp);
            func_8C0B60B8((Uint8)twp->counter.b[0]);
            if (twp->flag & 0x100) {
                twp->flag &= ~0x500;
            }
            twp->flag &= ~TWF_ONPATH;
        }
        break;
    case 35:
        func_8C0C6DE0(twp, mwp, pwp);
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
        break;
    case 39:
        func_8C0C6DE0(twp, mwp, pwp);
        twp->mode = 50;
        pwp->mj.reqaction = 38;
        twp->flag &= ~0x2500;
        break;
    case 50:
        func_8C0C6DE0(twp, mwp, pwp);
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
ASM_STUB(Sint32 func_8C0C61F4(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

ASM_STUB(void func_8C0C5792(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(Sint32 func_8C0C5B3A(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C0C6DE0(taskwk *twp, motionwk *mwp, playerwk *pwp))
