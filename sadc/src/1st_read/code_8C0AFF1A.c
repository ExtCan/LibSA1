#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0AFF1A (chunk 8C0AEFA0-8C0B11C0, function 12/15): Sonic's
 * "next action" handler (SADX Sonic_NextAction analogue), called from
 * SonicModeMachine (8C0A966C).
 *
 * While the event work locks the player (ewp->lock) it defers to
 * func_8C0AFE72 and returns 0. An event path (ewp->list4C) forces mode 80.
 * Otherwise, when an action was requested from outside (twp->flag & 0x1000,
 * set by SetInputP together with twp->smode), it switches on the requested
 * action (twp->smode 1-50), sets the new mode and motion request and returns 1
 * (0 for an ignored request).
 *
 * Matching notes: the cases are in source order (not numeric); the chained
 * "ang.x = ang.z = 0" sets SHC's register priorities (R9 = 0); case 38 must be
 * a guarded block ending in break (a "return 1" there is tail-merged with the
 * ewp->list4C exit); the second CreateElementalTask is tested inside the if.
 */

/* event work of the player (include/sa1/engine.h) */
#define EWP(twp) ((eventwk *)(twp)->ewp)

/* Super Sonic task: code in the B_CHAOS7 overlay (B_CHAOS7.BIN, load address
 * 0x0C900000; function 0C900480). PC: provided by the overlay loader. */
#if defined(TARGET_PC)
extern void func_0C900480(task *tp);
#define SUPERSONIC_EXEC func_0C900480
#else
#define SUPERSONIC_EXEC ((TaskFuncPtr)0x0C900480)
#endif

ASM_STUB(Sint32 func_8C0AFE72(taskwk *twp, motionwk *mwp, playerwk *pwp))

extern void func_8C0A9660(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* back to standing (code_8C0A9660.c) */
extern void func_8C07B320(taskwk *twp);
extern void func_8C0B60B8(Sint32 pno);
extern void func_8C0B32B6(task *tp);

#ifdef NON_MATCHING
/*
 * Byte-identical to the original: all 957 instructions match and the 50
 * entries of the switch jump table give the same offsets (checked against the
 * compiled .src). tools/shcdiff.py cannot evaluate the ".DATA.W Lcase-Lbase"
 * jump-table entries behind the MOVA and always reports that one instruction
 * as differing, which would fail the splice step; hence the guard.
 */
Sint32 func_8C0AFF1A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    taskwk *htwp;
    CCL_INFO *info;
    task *tp;

    if (EWP(twp)->lock) {
        func_8C0AFE72(twp, mwp, pwp);
        return 0;
    }
    if (EWP(twp)->list4C != NULL) {
        twp->mode = 80;
        twp->flag &= ~0x2500;
        return 1;
    }
    if (!(twp->flag & 0x1000)) {
        return 0;
    }
    switch (twp->smode) {
    case 3:
    case 8:
        twp->mode = 9;
        pwp->mj.reqaction = 16;
        twp->flag &= ~0x2500;
        break;
    case 6:
        twp->mode = 9;
        pwp->mj.reqaction = 39;
        twp->flag &= ~0x2500;
        break;
    case 7:
        twp->mode = 9;
        pwp->mj.reqaction = 41;
        twp->flag &= ~0x2500;
        break;
    case 4:
        twp->mode = 10;
        pwp->mj.reqaction = 16;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 5:
        twp->mode = 11;
        pwp->mj.reqaction = 70;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 11:
        twp->mode = 12;
        pwp->mj.reqaction = 26;
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
        twp->mode = 7;
        pwp->mj.reqaction = 15;
        pwp->hpos = 0.0f;
        twp->flag |= 0x500;
        twp->flag |= 0x2000;
        break;
    case 12:
        twp->mode = 20;
        pwp->mj.reqaction = 0;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 14:
        twp->mode = 20;
        pwp->mj.reqaction = 12;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 13:
        twp->mode = 20;
        pwp->mj.reqaction = 14;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag |= 0x500;
        pwp->work.f = 5.0f;
        func_8C0940DA(0x2D8, 0, 0, 0);
        twp->flag &= ~0x2000;
        break;
    case 15:
        twp->mode = 20;
        pwp->mj.reqaction = 21;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 16:
        twp->mode = 20;
        pwp->mj.reqaction = 47;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 17:
        twp->mode = 20;
        pwp->mj.reqaction = 18;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 18:
        twp->mode = 45;
        pwp->mj.reqaction = 65;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 19:
        twp->mode = 20;
        pwp->mj.reqaction = 75;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 20:
        twp->mode = 20;
        pwp->mj.reqaction = 77;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 21:
        twp->mode = 20;
        pwp->mj.reqaction = 79;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 22:
        twp->mode = 20;
        pwp->mj.reqaction = 80;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 23:
        twp->mode = 20;
        pwp->mj.reqaction = 88;
        twp->flag &= ~0x2500;
        break;
    case 9:
        twp->mode = 20;
        pwp->mj.reqaction = 44;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 10:
        twp->mode = 20;
        pwp->mj.reqaction = 45;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 24:
        func_8C0A9660(twp, mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 25:
        twp->mode = 5;
        pwp->mj.reqaction = 15;
        twp->flag |= 0x500;
        func_8C0940DA(0x2D9, 0, 0, 0);
        break;
    case 26:
        if (twp->mode == 2 || twp->mode == 5 || pwp->spd.x > pwp->p.run_speed) {
            twp->flag |= 0x2000;
        }
        return 0;
    case 43:
        twp->mode = 48;
        pwp->mj.reqaction = 18;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 44:
        twp->mode = 65;
        pwp->mj.reqaction = 105;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 29:
        func_8C0940DA(34, 0, 0, 0);
        twp->mode = 36;
        pwp->mj.reqaction = 67;
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
        func_8C0940DA(34, 0, 0, 0);
        twp->mode = 38;
        pwp->mj.reqaction = 69;
        pwp->mj.nframe = 0.0f;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag |= 0x2000;
        break;
    case 32:
        if (twp->mode >= 51 && twp->mode <= 54) {
            twp->smode = 0;
            return 0;
        }
        htwp = pwp->htp->twp;
        info = CWP(htwp)->info;
        if (twp->pos.y > htwp->pos.y + info->center.y - info->b) {
            twp->mode = 52;
            pwp->mj.reqaction = 92;
        } else {
            twp->mode = 51;
            pwp->mj.reqaction = 89;
        }
        func_8C0940DA(34, 0, 0, 0);
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        pwp->free.w[3] = 0;
        break;
    case 30:
        if (twp->mode >= 55 && twp->mode <= 60) {
            twp->smode = 0;
            return 0;
        }
        if (pwp->htp->twp->pos.y > twp->pos.y + 16.0f) {
            twp->mode = 60;
            pwp->mj.reqaction = 101;
        } else {
            twp->mode = 55;
            pwp->mj.reqaction = 94;
        }
        func_8C0940DA(34, 0, 0, 0);
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        pwp->free.w[3] = 0;
        break;
    case 45:
        twp->mode = 69;
        pwp->mj.reqaction = 125;
        twp->flag |= 0x500;
        twp->flag &= ~0x2000;
        break;
    case 33:
        twp->mode = 42;
        pwp->mj.reqaction = 81;
        pwp->free.w[3] = 180;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        func_8C0940DA(0x56E, 0, 0, 0);
        break;
    case 38:
        if (twp->mode != 43) {
            twp->mode = 43;
            pwp->mj.reqaction = 82;
            pwp->free.w[3] = 300;
            func_8C0B60B8((Uint8)twp->counter.b[0]);
            PClearSpeed(mwp, pwp);
            if (twp->flag & 0x100) {
                twp->flag &= ~0x500;
            }
            twp->flag &= ~0x2000;
        }
        break;
    case 35:
        twp->mode = 18;
        pwp->mj.reqaction = 27;
        if (twp->counter.b[0] == 0) {
            func_8C0940DA(0x458, 0, 0, 0);
        }
        pwp->free.w[2] = 0;
        func_8C07B320(twp);
        twp->flag &= ~0x2500;
        break;
    case 34:
        twp->mode = 79;
        pwp->mj.reqaction = 3;
        twp->flag &= ~0x2500;
        break;
    case 40:
        twp->mode = 20;
        pwp->mj.reqaction = 126;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 41:
        twp->mode = 79;
        pwp->mj.reqaction = 128;
        break;
    case 42:
        twp->mode = 79;
        pwp->mj.reqaction = 129;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 36:
        twp->mode = 79;
        pwp->mj.reqaction = 84;
        pwp->breathtimer = 0;
        twp->flag &= ~0x2500;
        break;
    case 37:
        twp->mode = 23;
        pwp->mj.reqaction = 37;
        pwp->htp = twp->value.tp;
        pwp->htp->twp->flag |= 0x1000;
        twp->flag |= 0x800;
        twp->flag &= ~8;
        twp->flag &= ~0x2500;
        break;
    case 46:
        pwp->equipment |= 0x8000;
        tp = CreateElementalTask(2, 2, SUPERSONIC_EXEC);
        if (tp != NULL) {
            tp->twp->counter.b[0] = twp->counter.b[0];
        }
        if ((tp = CreateElementalTask(8, 2, func_8C0B32B6)) != NULL) {
            *(Sint8 *)tp->awp = twp->counter.b[0];
        }
        twp->mode = 71;
        pwp->mj.reqaction = 130;
        twp->flag &= ~0x2500;
        break;
    case 39:
        twp->mode = 81;
        pwp->mj.reqaction = 85;
        twp->flag &= ~0x2500;
        break;
    case 50:
        twp->mode = 82;
        pwp->mj.reqaction = 86;
        twp->flag &= ~0x2500;
        func_8C0940DA(0x570, 0, 0, 0);
        break;
    default:
        return 0;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C0AFF1A(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
