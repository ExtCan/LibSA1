#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0B0E94-8C0B11C0: two Sonic input-request handlers (chunk
 * 8C0AEFA0-8C0B11C0, functions 14 and 15). Both are called by
 * SonicModeMachine (8C0A966C) with (twp, mwp, pwp). When SetInputP has
 * posted a request (twp->flag & 0x1000) they switch twp->mode and
 * pwp->mj.reqaction according to the requested mode twp->smode and return
 * 1 when the request was taken, 0 otherwise.
 */

extern void func_8C0B1D60(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern void func_8C0A9660(taskwk *twp, motionwk *mwp, playerwk *pwp);   /* back to standing (code_8C0A9660.c) */

Sint32 func_8C0B0E94(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!(twp->flag & 0x1000)) {
        return 0;
    }
    if (twp->smode == 24) {
        twp->mode = 12;
        pwp->mj.reqaction = 18;
        return 1;
    }
    if (twp->smode == 19) {
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 20;
        pwp->mj.reqaction = 75;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        return 1;
    }
    return 0;
}

Sint32 func_8C0B0F24(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!(twp->flag & 0x1000)) {
        return 0;
    }
    switch (twp->smode) {
    case 3:
        twp->mode = 76;
        pwp->mj.reqaction = 135;
        twp->flag &= ~0x2500;
        break;
    case 4:
        twp->mode = 75;
        pwp->mj.reqaction = 135;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 1:
        twp->mode = 73;
        pwp->mj.reqaction = 133;
        pwp->hpos = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag |= 0x2000;
        break;
    case 14:
        twp->mode = 20;
        pwp->mj.reqaction = 134;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 12:
        twp->mode = 20;
        pwp->mj.reqaction = 130;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 13:
        twp->mode = 20;
        pwp->mj.reqaction = 141;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag |= 0x500;
        twp->flag &= ~0x2000;
        break;
    case 19:
        twp->mode = 20;
        pwp->mj.reqaction = 139;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 24:
        twp->mode = 71;
        pwp->mj.reqaction = 130;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 47:
        pwp->equipment &= ~0x8000;
        func_8C0A9660(twp, mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 26:
        if (twp->mode == 72 || pwp->spd.x > pwp->p.run_speed) {
            twp->flag |= 0x2000;
        }
        return 0;
    case 50:
        pwp->equipment &= ~0x8000;
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
