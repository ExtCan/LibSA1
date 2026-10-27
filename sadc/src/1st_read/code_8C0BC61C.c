/*
 * 1ST_READ 8C0BC61C-8C0BCBCA (chunk 8C0BAFC0-8C0BDC80, function 12/29):
 * a second Tails "next action" handler (variant of func_8C0BBDD6,
 * code_8C0BBDD6.c), called from TailsModeMachine (8C0B70F4).
 *
 * While the event work locks the player (ewp->lock) it defers to the
 * rotation helper func_8C0BBD3C and returns 0. Otherwise, when an action was
 * requested from outside (twp->flag & 0x1000, set by SetInputP together with
 * twp->smode), it switches on the requested action (twp->smode 1-50); most
 * cases first call func_8C0BD440(twp, pwp) (Tails cleanup helper), then set
 * the new mode and motion request (pwp->mj.reqaction) and return 1 (0 for an
 * ignored request). Tails SE through func_8C0BFCA2(twp, se).
 *
 * Matching notes: the cases are in the order of their code in the binary
 * (not numeric); SHC itself tail-merges cases 3/6/7/8 into case 4 and
 * case 11 into the end of case 12; case 38 must be a guarded block ending
 * in break (its flag store is not merged into the shared one).
 */
#include "sa1/sa1.h"

/* Tails SE: func_8C0940DA(se, 0, 0, 0) unless twp->counter.b[0] == 1 (code_8C0BBA4E.c) */
void func_8C0BFCA2(taskwk *twp, Sint32 se);
/* code_8C0817E0.c */
Sint32 func_8C0818DE(void);

/* event-lock rotation helper (code_8C0BBA4E.c), same chunk: BSR */
ASM_STUB(Sint32 func_8C0BBD3C(taskwk *twp))
/* same chunk, defined later: BSR */
ASM_STUB(void func_8C0BD440(taskwk *twp, playerwk *pwp))

#ifdef NON_MATCHING
/*
 * Byte-identical to the original: all 596 instructions match and the 50
 * entries of the switch jump table give the same offsets (checked against
 * the compiled listing). tools/shcdiff.py cannot evaluate the
 * ".DATA.W Lcase-Lbase" jump-table entries behind the MOVA and always
 * reports that one instruction as differing, which would fail the splice
 * step; hence the guard (same situation as code_8C0AFF1A.c).
 */
Sint32 func_8C0BC61C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (EWP(twp)->lock) {
        func_8C0BBD3C(twp);
        return 0;
    }
    if (!(twp->flag & 0x1000)) {
        return 0;
    }
    switch (twp->smode) {
    case 3:
    case 6:
    case 7:
    case 8:
        twp->mode = 30;
        pwp->mj.reqaction = 80;
        twp->flag &= ~0x2000;
        break;
    case 4:
        twp->mode = 31;
        pwp->mj.reqaction = 80;
        twp->flag &= ~0x2000;
        break;
    case 11:
        twp->mode = 28;
        pwp->mj.reqaction = 82;
        twp->flag &= ~0x2000;
        break;
    case 1:
        func_8C0BD440(twp, pwp);
        twp->mode = 3;
        pwp->mj.reqaction = 11;
        pwp->hpos = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag |= 0x2000;
        break;
    case 2:
        func_8C0BD440(twp, pwp);
        twp->mode = 5;
        pwp->mj.reqaction = 15;
        pwp->hpos = 0.0f;
        twp->flag |= 0x500;
        twp->flag |= 0x2000;
        break;
    case 12:
        twp->mode = 18;
        pwp->mj.reqaction = 62;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 13:
        func_8C0BD440(twp, pwp);
        twp->mode = 18;
        pwp->mj.reqaction = 14;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag |= 0x500;
        pwp->work.f = 2.0f;
        func_8C0BFCA2(twp, 0x2D8);
        twp->flag &= ~0x2000;
        break;
    case 15:
        func_8C0BD440(twp, pwp);
        twp->mode = 18;
        pwp->mj.reqaction = 24;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 14:
        func_8C0BD440(twp, pwp);
        twp->mode = 18;
        pwp->mj.reqaction = 11;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 16:
        twp->mode = 18;
        pwp->mj.reqaction = 72;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 17:
        twp->mode = 18;
        pwp->mj.reqaction = 19;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 18:
        twp->mode = 43;
        pwp->mj.reqaction = 89;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 19:
        func_8C0BD440(twp, pwp);
        twp->mode = 62;
        if (func_8C0818DE() == 1) {
            pwp->mj.reqaction = 54;
        } else {
            pwp->mj.reqaction = 56;
        }
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        twp->timer.b[3] |= 8;
        break;
    case 20:
        func_8C0BD440(twp, pwp);
        twp->mode = 18;
        pwp->mj.reqaction = 97;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 9:
        twp->mode = 18;
        pwp->mj.reqaction = 62;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 10:
        twp->mode = 18;
        pwp->mj.reqaction = 62;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 24:
        twp->mode = 23;
        pwp->mj.reqaction = 62;
        twp->flag &= ~0x2000;
        break;
    case 26:
        if (twp->mode == 25 || pwp->spd.x > pwp->p.run_speed) {
            twp->flag |= 0x2000;
        }
        return 0;
    case 29:
        func_8C0BFCA2(twp, 34);
        func_8C0BD440(twp, pwp);
        twp->mode = 36;
        pwp->mj.reqaction = 91;
        pwp->mj.nframe = 0.0f;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag |= 0x2000;
        break;
    case 31:
        if (twp->flag & 3) {
            return 0;
        }
        func_8C0BD440(twp, pwp);
        func_8C0BFCA2(twp, 34);
        twp->mode = 38;
        pwp->mj.reqaction = 93;
        pwp->mj.nframe = 0.0f;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag |= 0x2000;
        break;
    case 38:
        if (twp->mode != 13) {
            func_8C0BD440(twp, pwp);
            twp->flag &= ~4;
            func_8C0BFCA2(twp, 0x473);
            twp->mode = 13;
            pwp->mj.reqaction = 26;
            func_8C07B2B8(0, 8);
            pwp->spd.y = pwp->p.jmp_y_spd;
            pwp->spd.x = -pwp->p.run_speed;
            twp->flag &= ~0x500;
        }
        break;
    case 35:
        func_8C0BD440(twp, pwp);
        twp->mode = 16;
        pwp->mj.reqaction = 34;
        func_8C0BFCA2(twp, 0x469);
        func_8C07B320(twp);
        twp->flag &= ~0x2500;
        break;
    case 34:
        func_8C0BD440(twp, pwp);
        twp->mode = 57;
        pwp->mj.reqaction = 3;
        twp->flag &= ~0x2500;
        break;
    case 48:
        twp->mode = 15;
        pwp->mj.reqaction = 37;
        pwp->free.f[7] = 0.0f;
        twp->flag &= ~0x500;
        twp->flag |= 0x400;
        twp->timer.b[2] |= 1;
        twp->timer.b[3] &= ~1;
        func_8C0BFCA2(twp, 0x46D);
        break;
    case 49:
        twp->mode = 61;
        pwp->mj.reqaction = 41;
        pwp->free.f[7] = 0.0f;
        twp->flag &= ~0x500;
        twp->flag |= 0x400;
        twp->timer.b[2] |= 1;
        twp->timer.b[3] &= ~1;
        break;
    case 39:
        func_8C0BD440(twp, pwp);
        twp->mode = 59;
        pwp->mj.reqaction = 31;
        twp->flag &= ~0x2500;
        break;
    case 50:
        func_8C0BD440(twp, pwp);
        twp->mode = 60;
        pwp->mj.reqaction = 28;
        twp->flag &= ~0x2500;
        break;
    default:
        return 0;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C0BC61C(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
