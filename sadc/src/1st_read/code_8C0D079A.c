#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0CBE00):
 *   func_8C0D079A  E-102 input/request check (SADX E102CheckInput analogue):
 *                  event lock -> 8C0D06F4; event path queued -> mode 55;
 *                  otherwise, while TWF_REQUEST is set, switch on the
 *                  requested state twp->smode and set mode/reqaction.
 *                  Returns 1 when the mode was changed. Called by
 *                  E102ModeMachine.
 */

/* E-102 event-lock handler (8C0D06F4, BSR from here; returns 0/1). */
ASM_STUB(Sint32 func_8C0D06F4(taskwk *twp, motionwk *mwp, playerwk *pwp))

/* E-102 return to standing (src/1st_read/code_8C0CD09C.c) */
void func_8C0CD09C(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* byte flag read only by the E-102 chunk (MOV.B + TST); here it gates the positional SE 34 */
extern Sint8 B_8C7A2EA8;

#ifdef NON_MATCHING
/*
 * Byte-identical to the original: all 347 instructions match and the 50
 * entries of the switch jump table give the same offsets (checked against the
 * compiled .src, label differences vs the halfwords at 8C0D083C).
 * tools/shcdiff.py cannot evaluate the ".DATA.W Lcase-Lbase" jump-table
 * entries behind the MOVA and always reports that one instruction as
 * differing, which would fail the splice step; hence the guard (same
 * situation as SonicCheckInput func_8C0AFF1A in code_8C0AFF1A.c).
 */
Sint32 func_8C0D079A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (EWP(twp)->lock) {
        func_8C0D06F4(twp, mwp, pwp);
        return 0;
    }
    if (EWP(twp)->list4C != NULL) {
        twp->mode = 55;
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
        pwp->mj.reqaction = 6;
        twp->ang.x = twp->ang.z = 0;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 4:
        twp->mode = 6;
        pwp->mj.reqaction = 6;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 11:
        twp->mode = 7;
        pwp->mj.reqaction = 16;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 1:
    case 2:
        twp->mode = 3;
        pwp->mj.reqaction = 3;
        pwp->hpos = 0.0f;
        twp->flag &= ~0x400;
        twp->flag |= TWF_ONPATH;
        break;
    case 9:
    case 10:
    case 12:
    case 13:
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
        pwp->mj.reqaction = 11;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 24:
    case 25:
        func_8C0CD09C(twp, mwp, pwp);
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 26:
        if (twp->mode == 2 || pwp->spd.x > pwp->p.run_speed) {
            pwp->hpos = 0.0f;
            twp->flag &= ~0x400;
            twp->flag |= TWF_ONPATH;
        }
        return 0;
    case 31:
        if (twp->flag & TWF_GROUND) {
            return 0;
        }
        if (B_8C7A2EA8) {
            func_8C09413A(34, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        twp->mode = 47;
        pwp->mj.reqaction = 66;
        pwp->mj.nframe = 0.0f;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag |= TWF_ONPATH;
        break;
    case 18:
        twp->mode = 53;
        pwp->mj.reqaction = 72;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~TWF_ONPATH;
        break;
    case 19:
        twp->mode = 52;
        pwp->mj.reqaction = 67;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 50:
        if (twp->mode == 51) {
            return 1;
        }
        switch (twp->mode) {
        case 31:
            pwp->mj.reqaction = 70;
            break;
        case 33:
            pwp->mj.reqaction = 71;
            break;
        default:
            pwp->mj.reqaction = 69;
            break;
        }
        twp->mode = 51;
        break;
    case 37:
        twp->mode = 17;
        pwp->mj.reqaction = 25;
        pwp->htp = twp->value.tp;
        pwp->htp->twp->flag |= TWF_REQUEST;
        twp->flag |= TWF_HOLD;
        twp->flag &= ~8;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    default:
        return 0;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C0D079A(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
