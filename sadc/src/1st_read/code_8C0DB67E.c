#include "sa1/sa1.h"

/*
 * Amy (1ST_READ chunk 8C0D6B40-8C0DCA6C), functions 22-23:
 *
 *  func_8C0DB67E  rotation helper (Amy counterpart of Sonic's func_8C0AFE72
 *                 and E-102's func_8C0D06F4): event lock 1..4 sets
 *                 twp->flag 0x4000; lock 6 / 5 rotate the player towards
 *                 the event angle (absolute / relative to ang.y) via
 *                 8C07C298; any other lock clears 0x4000 and returns 0.
 *                 Called (BSR) by func_8C0DB71A and func_8C0DBC16 with
 *                 (twp, mwp, pwp); only twp is used.
 *  func_8C0DB71A  Amy input/request check (SADX Amy_NextAction analogue,
 *                 counterpart of SonicCheckInput func_8C0AFF1A and E-102's
 *                 func_8C0D079A): event lock -> func_8C0DB67E; event path
 *                 queued -> mode 45; otherwise, while TWF_REQUEST is set,
 *                 switch on the requested state twp->smode and set
 *                 mode/reqaction. Returns 1 when the mode was changed.
 *                 Called by AmyModeMachine 8C0D7960.
 */

/* Amy: back to standing (twp->mode = 1, pwp->waittimer = 0; 8C0DA6E0) */
void func_8C0DA6E0(taskwk *twp, motionwk *mwp, playerwk *pwp);

Sint32 func_8C0DB67E(taskwk *twp, motionwk *mwp, playerwk *pwp)
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
 * Byte-identical to the original: all 534 instructions match and the 50
 * entries of the switch jump table give the same offsets (checked against the
 * compiled .src, label differences vs the halfwords at 8C0DB7B8).
 * tools/shcdiff.py cannot evaluate the ".DATA.W Lcase-Lbase" jump-table
 * entries behind the MOVA and always reports that one instruction as
 * differing, which would fail the splice step; hence the guard (same
 * situation as SonicCheckInput func_8C0AFF1A and E-102's func_8C0D079A).
 * Matching notes: the cases are in source order (not numeric); case 33 ends
 * with break after the SE call (a "return 1" there changes the register
 * choice of case 39); case 39 must end with break (return 1 changes case 50).
 * The block of case 1 (8C0DB87E) is reached only through the jump table.
 */
Sint32 func_8C0DB71A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (EWP(twp)->lock) {
        func_8C0DB67E(twp, mwp, pwp);
        return 0;
    }
    if (EWP(twp)->list4C != NULL) {
        twp->mode = 45;
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
        twp->mode = 6;
        pwp->mj.reqaction = 16;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 6:
        twp->mode = 6;
        pwp->mj.reqaction = 49;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 7:
        twp->mode = 6;
        pwp->mj.reqaction = 51;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 4:
        twp->mode = 7;
        pwp->mj.reqaction = 16;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 11:
        twp->mode = 8;
        pwp->mj.reqaction = 29;
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
        twp->mode = 13;
        pwp->mj.reqaction = 0;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 15:
        twp->mode = 13;
        pwp->mj.reqaction = 22;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 16:
        twp->mode = 13;
        pwp->mj.reqaction = 55;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 17:
        twp->mode = 13;
        pwp->mj.reqaction = 18;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 18:
        twp->mode = 48;
        pwp->mj.reqaction = 76;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 19:
        twp->mode = 13;
        if (GetStageNumber() < 0xF00) {
            pwp->mj.reqaction = 32;
        } else {
            pwp->mj.reqaction = 42;
        }
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 20:
        twp->mode = 13;
        pwp->mj.reqaction = 77;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 21:
        twp->mode = 13;
        pwp->mj.reqaction = 79;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 22:
        twp->mode = 13;
        pwp->mj.reqaction = 80;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 9:
        twp->mode = 13;
        pwp->mj.reqaction = 54;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~TWF_ONPATH;
        break;
    case 24:
        func_8C0DA6E0(twp, mwp, pwp);
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
        twp->mode = 46;
        pwp->mj.reqaction = 37;
        break;
    case 50:
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
ASM_STUB(Sint32 func_8C0DB71A(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
