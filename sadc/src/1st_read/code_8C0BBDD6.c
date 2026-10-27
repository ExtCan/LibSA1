/*
 * 1ST_READ 8C0BBDD6-8C0BC61C (chunk 8C0BAFC0-8C0BDC80, function 11/29):
 * Tails' "next action" handler (= Sonic's func_8C0AFF1A, code_8C0AFF1A.c;
 * SADX Tails_NAct analogue), called from TailsModeMachine.
 *
 * While the event work locks the player (ewp->lock) it defers to the
 * rotation helper func_8C0BBD3C and returns 0. An event path (ewp->list4C)
 * forces mode 58. Otherwise, when an action was requested from outside
 * (twp->flag & 0x1000, set by SetInputP together with twp->smode), it
 * switches on the requested action (twp->smode 1-50), sets the new mode and
 * motion request (pwp->mj.reqaction) and returns 1 (0 for an ignored
 * request). Tails SE through func_8C0BFCA2(twp, se).
 *
 * Matching notes: the cases are in source order (not numeric), as in Sonic's
 * version; the layout and all 883 instructions are otherwise identical.
 */
#include "sa1/sa1.h"

/* Tails SE: func_8C0940DA(se, 0, 0, 0) unless twp->counter.b[0] == 1 (code_8C0BBA4E.c) */
void func_8C0BFCA2(taskwk *twp, Sint32 se);
/* twp->mode = 1, waittimer = 0, then the Tails action code (code_8C0B70E8.c) */
void func_8C0B70E8(taskwk *twp, playerwk *pwp);
/* code_8C0817E0.c */
Sint32 func_8C0818DE(void);

/* event-lock rotation helper (code_8C0BBA4E.c), same chunk: BSR */
ASM_STUB(Sint32 func_8C0BBD3C(taskwk *twp))

#ifdef NON_MATCHING
/*
 * NON_MATCHING: identical instruction stream except for one register-allocation
 * tie: the original keeps the hoisted constant 3 (mode 3, flag & 3, reqaction 3)
 * in R6 and 11 (reqaction 11) in R7; SHC gives them the other way round from
 * this source (9 instructions differ only by R6<->R7). Not reproduced after
 * ~35 attempts (statement/case order, case 26/31/32 forms, literal types and
 * casts, union views): the choice among the tied constants 3/7/11 is not
 * transitive and also depends on their values. Additionally tools/shcdiff.py
 * cannot evaluate the ".DATA.W Lcase-Lbase" jump-table entries behind the MOVA
 * and always reports that instruction as differing (see code_8C0AFF1A.c).
 */
Sint32 func_8C0BBDD6(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    taskwk *htwp;
    CCL_INFO *info;

    if (EWP(twp)->lock) {
        func_8C0BBD3C(twp);
        return 0;
    }
    if (EWP(twp)->list4C != NULL) {
        twp->mode = 58;
        twp->flag &= ~0x2500;
        return 1;
    }
    if (!(twp->flag & 0x1000)) {
        return 0;
    }
    switch (twp->smode) {
    case 3:
    case 8:
        twp->mode = 7;
        pwp->mj.reqaction = 17;
        twp->flag &= ~0x2500;
        break;
    case 6:
        twp->mode = 7;
        pwp->mj.reqaction = 64;
        twp->flag &= ~0x2500;
        break;
    case 7:
        twp->mode = 7;
        pwp->mj.reqaction = 66;
        twp->flag &= ~0x2500;
        break;
    case 4:
        twp->mode = 8;
        pwp->mj.reqaction = 17;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 5:
        twp->mode = 9;
        pwp->mj.reqaction = 94;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 11:
        twp->mode = 10;
        pwp->mj.reqaction = 33;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 1:
        twp->mode = 3;
        pwp->mj.reqaction = 11;
        pwp->hpos = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag |= 0x2000;
        break;
    case 2:
        twp->mode = 5;
        pwp->mj.reqaction = 15;
        pwp->hpos = 0.0f;
        twp->flag |= 0x500;
        twp->flag |= 0x2000;
        break;
    case 12:
        twp->mode = 18;
        pwp->mj.reqaction = 0;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 14:
        twp->mode = 18;
        pwp->mj.reqaction = 11;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 13:
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
        twp->mode = 18;
        pwp->mj.reqaction = 24;
        twp->ang.x = twp->ang.z = 0;
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
        twp->flag &= ~0x2500;
        break;
    case 17:
        twp->mode = 18;
        pwp->mj.reqaction = 19;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 18:
        twp->mode = 43;
        pwp->mj.reqaction = 89;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 19:
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
        twp->mode = 18;
        pwp->mj.reqaction = 97;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 21:
        twp->mode = 18;
        pwp->mj.reqaction = 99;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 22:
        twp->mode = 18;
        pwp->mj.reqaction = 100;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 9:
        twp->mode = 18;
        pwp->mj.reqaction = 69;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 10:
        twp->mode = 18;
        pwp->mj.reqaction = 70;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 24:
        func_8C0B70E8(twp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 26:
        if (twp->mode == 2 || pwp->spd.x > pwp->p.run_speed) {
            twp->flag |= 0x2000;
        }
        if (twp->mode != 20 || !(pwp->spd.x > pwp->p.run_speed)) {
            return 0;
        }
        twp->flag |= 0x2000;
        twp->mode = 2;
        pwp->mj.reqaction = 11;
        twp->timer.b[2] &= ~2;
        break;
    case 44:
        twp->mode = 51;
        pwp->mj.reqaction = 110;
        pwp->p.height += 2.0f;
        pwp->p.center_height = pwp->p.height * 0.5f;
        pwp->p.jmp_y_spd *= 1.19999996f; /* 0x3F999999 */
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 29:
        func_8C0BFCA2(twp, 34);
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
        func_8C0BFCA2(twp, 34);
        twp->mode = 38;
        pwp->mj.reqaction = 93;
        pwp->mj.nframe = 0.0f;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag |= 0x2000;
        break;
    case 32:
        if (twp->mode >= 44 && twp->mode <= 47) {
            twp->smode = 0;
            return 0;
        }
        htwp = pwp->htp->twp;
        info = CWP(htwp)->info;
        if (twp->pos.y > htwp->pos.y + info->center.y - info->b) {
            twp->mode = 45;
            pwp->mj.reqaction = 105;
        } else {
            twp->mode = 44;
            pwp->mj.reqaction = 102;
        }
        func_8C0BFCA2(twp, 34);
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        twp->counter.w[1] = 0;
        break;
    case 45:
        twp->mode = 55;
        pwp->mj.reqaction = 128;
        twp->flag |= 0x500;
        twp->flag &= ~0x2000;
        break;
    case 38:
        if (twp->mode != 13) {
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
        twp->mode = 16;
        pwp->mj.reqaction = 34;
        func_8C0BFCA2(twp, 0x469);
        func_8C07B320(twp);
        twp->flag &= ~0x2500;
        break;
    case 34:
        twp->mode = 57;
        pwp->mj.reqaction = 3;
        twp->flag &= ~0x2500;
        break;
    case 40:
        twp->mode = 18;
        pwp->mj.reqaction = 129;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 41:
        twp->mode = 57;
        pwp->mj.reqaction = 131;
        break;
    case 42:
        twp->mode = 57;
        pwp->mj.reqaction = 132;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 36:
        twp->mode = 57;
        pwp->mj.reqaction = 30;
        pwp->breathtimer = 0;
        twp->flag &= ~0x2500;
        break;
    case 37:
        twp->mode = 23;
        pwp->mj.reqaction = 62;
        pwp->htp = twp->value.tp;
        pwp->htp->twp->flag |= 0x1000;
        twp->flag |= 0x800;
        twp->flag &= ~8;
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
        twp->mode = 59;
        pwp->mj.reqaction = 31;
        twp->flag &= ~0x2500;
        break;
    case 50:
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
ASM_STUB(Sint32 func_8C0BBDD6(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
