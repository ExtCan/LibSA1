#include "sa1/sa1.h"

/*
 * Big (1ST_READ chunk 8C0DFFA0-8C0E59C0), functions 16-21. All are called
 * by BigModeMachine (8C0E0E70) with (twp, mwp, pwp); 8C0E4C16 also by
 * BigMain (8C0DFFA0).
 *
 *  func_8C0E488C  Big input/request check (SADX Big_CheckInput analogue,
 *                 counterpart of Amy's func_8C0DB71A): event lock ->
 *                 func_8C0E43CA; otherwise, while TWF_REQUEST is set,
 *                 switch on the requested state twp->smode and set
 *                 mode/reqaction. Returns 1 when the mode was changed.
 *  func_8C0E4C16  underwater test: equipment 0x1000, the surface above is
 *                 water (shadow.Attr_top & 2) and its height y_top is more
 *                 than 2.0 above the first collision shape's center.y (or
 *                 the player is airborne), while the D_8C19E390 button is
 *                 not held or was just pressed.
 *  func_8C0E4C7C  same test with 5.0 and only "button not held".
 *  func_8C0E4CD0  func_8C0E4C7C -> mode 47, reqaction 92.
 *  func_8C0E4D40  airborne -> mode 8 (reqaction 14); while holding an
 *                 object of class 0x200 (func_8C06C880) mode 22
 *                 (reqaction 39) instead, other held objects are released
 *                 first (func_8C0E55E2).
 *  func_8C0E4DA4  no stick input and no speed: stand (func_8C0E65BE), or
 *                 with a held object mode 18 (reqaction 36) from modes
 *                 10/28/26, else mode 17 (func_8C0E3C20).
 *
 * Matching notes:
 *  - The "button held" tests only match with (Sint32) casts on both
 *    operands (see the note in sa1/input.h); the "pressed" test of
 *    8C0E4C16 matches with the plain Uint32 types.
 *  - func_8C0E3C20 is reached with BSR from func_8C0E488C but with JSR from
 *    func_8C0E4DA4 (8C0E4E00 is 0x11E0 bytes after it: out of BSR range in
 *    the original TU). The matching build therefore declares it extern;
 *    the stub that gives func_8C0E488C its BSR exists only in the
 *    NON_MATCHING build, where func_8C0E488C is compiled.
 */

#ifdef NON_MATCHING
ASM_STUB(void func_8C0E3C20(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(Sint32 func_8C0E43CA(taskwk *twp, motionwk *mwp, playerwk *pwp))
#else
void func_8C0E3C20(taskwk *twp, motionwk *mwp, playerwk *pwp);
#endif
void func_8C0E55E2(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* Big: back to standing (twp->mode = 1, reqaction 0; next chunk) */
void func_8C0E65BE(taskwk *twp, motionwk *mwp, playerwk *pwp);

#ifdef NON_MATCHING
/*
 * Byte-identical to the original: all 360 instructions match and the 50
 * entries of the switch jump table give the same offsets (checked against
 * the compiled listing vs the halfwords at 8C0E4900). tools/shcdiff.py
 * cannot evaluate the ".DATA.W Lcase-Lbase" entries behind the MOVA and
 * always reports that one instruction as differing, which would fail the
 * splice step; hence the guard (same as Amy's func_8C0DB71A).
 * Verify: py -3.12 tools/shcdiff.py func_8C0E488C src/1st_read/code_8C0E488C.c
 *         --flags=-define=NON_MATCHING   (only the MOVA differs)
 * The cases are in source (code) order, not numeric order.
 */
Sint32 func_8C0E488C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (EWP(twp)->lock) {
        func_8C0E43CA(twp, mwp, pwp);
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
        twp->mode = 24;
        pwp->mj.reqaction = 37;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 4:
        twp->mode = 25;
        pwp->mj.reqaction = 37;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 11:
        twp->mode = 22;
        pwp->mj.reqaction = 39;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 1:
        func_8C0E55E2(twp, mwp, pwp);
        twp->mode = 3;
        pwp->mj.reqaction = 9;
        pwp->hpos = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
            twp->flag &= ~0x400;
        }
        twp->flag |= TWF_ONPATH;
        break;
    case 9:
    case 10:
    case 12:
        twp->mode = 14;
        func_8C0E3C20(twp, mwp, pwp);
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 15:
        func_8C0E55E2(twp, mwp, pwp);
        twp->mode = 14;
        pwp->mj.reqaction = 17;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 16:
        twp->mode = 14;
        pwp->mj.reqaction = 32;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 17:
        twp->mode = 14;
        pwp->mj.reqaction = 14;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 19:
        func_8C0E55E2(twp, mwp, pwp);
        twp->mode = 14;
        pwp->mj.reqaction = 48;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 24:
        twp->mode = 17;
        func_8C0E3C20(twp, mwp, pwp);
        twp->flag &= ~TWF_ONPATH;
        break;
    case 26:
        if (twp->mode == 19 || pwp->spd.x > pwp->p.run_speed) {
            pwp->hpos = 0.0f;
            twp->flag |= TWF_ONPATH;
        }
        return 0;
    case 31:
        if (twp->flag & TWF_GROUND) {
            return 0;
        }
        func_8C0E55E2(twp, mwp, pwp);
        func_8C0940DA(34, 0, 0, 0);
        twp->mode = 29;
        pwp->mj.reqaction = 46;
        pwp->mj.nframe = 0.0f;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x400;
        twp->flag |= TWF_ONPATH;
        break;
    case 18:
        twp->mode = 51;
        pwp->mj.reqaction = 96;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~TWF_ONPATH;
        break;
    case 39:
        func_8C0E55E2(twp, mwp, pwp);
        twp->mode = 53;
        pwp->mj.reqaction = 98;
        twp->flag &= ~0x400;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 50:
        func_8C0940DA(23, 0, 0, 0);
        func_8C0E55E2(twp, mwp, pwp);
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
ASM_STUB(Sint32 func_8C0E488C(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

Sint32 func_8C0E4C16(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if ((pwp->equipment & 0x1000)
        && (!((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].on & (Sint32)D_8C19E390)
            || (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E390))
        && (pwp->shadow.Attr_top & 2)
        && (pwp->shadow.y_top > CWP(twp)->info->center.y + 2.0f || !(twp->flag & TWF_GROUND))) {
        return 1;
    }
    return 0;
}

Sint32 func_8C0E4C7C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if ((pwp->equipment & 0x1000)
        && !((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].on & (Sint32)D_8C19E390)
        && (pwp->shadow.Attr_top & 2)
        && (pwp->shadow.y_top > CWP(twp)->info->center.y + 5.0f || !(twp->flag & TWF_GROUND))) {
        return 1;
    }
    return 0;
}

Sint32 func_8C0E4CD0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (func_8C0E4C7C(twp, mwp, pwp)) {
        twp->mode = 47;
        pwp->mj.reqaction = 92;
        return 1;
    }
    return 0;
}

Sint32 func_8C0E4D40(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!((twp->flag | pwp->flag) & TWF_GROUND)) {
        if (twp->flag & TWF_HOLD) {
            if (func_8C06C880(twp) & 0x200) {
                twp->mode = 22;
                pwp->mj.reqaction = 39;
                return 1;
            }
            func_8C0E55E2(twp, mwp, pwp);
        }
        twp->mode = 8;
        pwp->mj.reqaction = 14;
        return 1;
    }
    return 0;
}

Sint32 func_8C0E4DA4(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!GetAnalog(twp, NULL, NULL) && pwp->spd.x == 0.0f) {
        if (twp->flag & TWF_HOLD) {
            if (twp->mode == 10 || twp->mode == 28 || twp->mode == 26) {
                twp->mode = 18;
                pwp->mj.reqaction = 36;
            } else {
                twp->mode = 17;
                func_8C0E3C20(twp, mwp, pwp);
            }
        } else {
            func_8C0E65BE(twp, mwp, pwp);
        }
        return 1;
    }
    return 0;
}

ASM_STUB(void func_8C0E55E2(taskwk *twp, motionwk *mwp, playerwk *pwp))
