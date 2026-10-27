#include "sa1/sa1.h"

/*
 * Big the Cat (1ST_READ chunk 8C0DFFA0-8C0E59C0, functions 14-15 of 29):
 *
 *  func_8C0E43CA  event lock helper (Big counterpart of Sonic's
 *                 func_8C0AFE72 and Amy's func_8C0DB67E): event lock 1..4
 *                 sets twp->flag 0x4000; lock 6 / 5 rotate the player
 *                 towards the event angle (absolute / relative to ang.y)
 *                 via 8C07C298; any other lock clears 0x4000 and returns 0.
 *                 Called (BSR) by func_8C0E4432 and func_8C0E488C with
 *                 (twp, mwp, pwp); only twp is used.
 *  func_8C0E4432  Big input/request check (counterpart of SonicCheckInput
 *                 func_8C0AFF1A and Amy's func_8C0DB71A): event lock ->
 *                 func_8C0E43CA; event path queued -> mode 52; otherwise,
 *                 while TWF_REQUEST is set, switch on the requested state
 *                 twp->smode and set mode/reqaction. Returns 1 when the
 *                 mode was changed. Called by BigModeMachine 8C0E0E70.
 */

/* Big: stand selector (same chunk, BSR; code_8C0E386E.c) */
ASM_STUB(void func_8C0E3C20(taskwk *twp, motionwk *mwp, playerwk *pwp))
/* Big: back to standing (mode 1, reqaction 0, waittimer 0; 8C0E65BE, other chunk) */
void func_8C0E65BE(taskwk *twp, motionwk *mwp, playerwk *pwp);
/* Big: 1 in modes 35-37 and 43-45 (8C0E6352, other chunk, JSR) */
Sint32 func_8C0E6352(taskwk *twp);

Sint32 func_8C0E43CA(taskwk *twp, motionwk *mwp, playerwk *pwp)
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

#ifdef NON_MATCHING
/*
 * Byte-identical to the original: all 445 instructions match and the 50
 * entries of the switch jump table give the same offsets (checked against
 * the halfwords at 8C0E44F8 with the compiled listing). tools/shcdiff.py
 * cannot evaluate the ".DATA.W Lcase-Lbase" jump-table entries behind the
 * MOVA and always reports that one instruction as differing, which would
 * fail the splice step; hence the guard (same situation as SonicCheckInput
 * func_8C0AFF1A and Amy's func_8C0DB71A).
 * Matching notes: the cases are in source order (not numeric); case 36 must
 * leave with break after twp->smode = 0 ("return 1" there swaps R2/R3 in
 * the mode 38 / reqaction 97 stores).
 */
Sint32 func_8C0E4432(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (EWP(twp)->lock) {
        func_8C0E43CA(twp, mwp, pwp);
        return 0;
    }
    if (EWP(twp)->list4C != NULL) {
        twp->mode = 52;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        return 1;
    }
    if (!(twp->flag & TWF_REQUEST)) {
        return 0;
    }
    switch (twp->smode) {
    case 3:
    case 6:
    case 7:
    case 8:
        twp->mode = 5;
        pwp->mj.reqaction = 12;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x400;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 4:
        twp->mode = 6;
        pwp->mj.reqaction = 12;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x400;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 5:
        twp->mode = 7;
        pwp->mj.reqaction = 47;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 11:
        twp->mode = 8;
        pwp->mj.reqaction = 21;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 1:
        twp->mode = 3;
        pwp->mj.reqaction = 9;
        pwp->hpos = 0.0f;
        twp->flag &= ~0x400;
        twp->flag |= TWF_ONPATH;
        break;
    case 9:
    case 10:
    case 12:
        twp->mode = 14;
        pwp->mj.reqaction = 0;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 15:
        twp->mode = 14;
        pwp->mj.reqaction = 17;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 16:
        twp->mode = 14;
        pwp->mj.reqaction = 32;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 17:
        twp->mode = 14;
        pwp->mj.reqaction = 14;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 19:
        twp->mode = 14;
        pwp->mj.reqaction = 48;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 24:
        func_8C0E65BE(twp, mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x400;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 26:
        if (twp->mode == 2 || pwp->spd.x > pwp->p.run_speed) {
            pwp->hpos = 0.0f;
            twp->flag |= TWF_ONPATH;
        }
        return 0;
    case 31:
        if (twp->flag & TWF_GROUND) {
            return 0;
        }
        func_8C0940DA(34, 0, 0, 0);
        twp->mode = 29;
        pwp->mj.reqaction = 46;
        pwp->mj.nframe = 0.0f;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x400;
        twp->flag |= TWF_ONPATH;
        break;
    case 36:
        pwp->breathtimer = 0;
        if (func_8C0E6352(twp)) {
            twp->smode = 0;
            break;
        }
        twp->mode = 38;
        pwp->mj.reqaction = 97;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 37:
        pwp->htp = twp->value.tp;
        pwp->htp->twp->flag |= TWF_REQUEST;
        twp->mode = 17;
        twp->flag |= TWF_HOLD;
        twp->flag &= ~8;
        func_8C0E3C20(twp, mwp, pwp);
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 18:
        twp->mode = 51;
        pwp->mj.reqaction = 96;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 39:
        twp->mode = 53;
        pwp->mj.reqaction = 98;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 50:
        twp->mode = 54;
        pwp->mj.reqaction = 99;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    default:
        return 0;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C0E4432(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
