#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0CBE00), mode checks called by
 * E102ModeMachine (all return 1 when they changed twp->mode):
 *   func_8C0D0B16  input/request check while holding an object (same shape
 *                  as func_8C0D079A): event lock -> 8C0D06F4; while
 *                  TWF_REQUEST is set, switch on the requested state
 *                  twp->smode, release the held object (8C0D15C0) where
 *                  needed and set mode/reqaction.
 *   func_8C0D0DF4  in the air: mode 7 (fall), or mode 22 when holding an
 *                  object of class 0x200.
 *   func_8C0D0E8C  stopped with no stick input: stand (8C0CD09C) or, when
 *                  holding, mode 17/18 by the object class.
 *   func_8C0D0F0C  grab button: pick up (class 0x400/0x800, mode 18) or
 *                  push (class 0x200, mode 15) the hit object.
 *   func_8C0D100A  lifting: after frame 12 take the hit object of class
 *                  0x200 facing it (mode 17), else stand.
 */

/* E-102 event-lock handler (8C0D06F4, BSR from here; returns 0/1). */
ASM_STUB(Sint32 func_8C0D06F4(taskwk *twp, motionwk *mwp, playerwk *pwp))
/* E-102 release of the held object (8C0D15C0, BSR from here; later in the
 * chunk, code_8C0D10BC.c). */
void func_8C0D15C0(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* E-102 return to standing (src/1st_read/code_8C0CD09C.c) */
void func_8C0CD09C(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* angle class 1..4 towards the hit object (code_8C06CCA0.c defines it with
 * (taskwk *)); the E-102 callers pass mwp in R5 as well */
extern Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);

/* byte flag, only used by the E-102 chunk: positional SE enable */
extern Sint8 B_8C7A2EA8;

#ifdef NON_MATCHING
/*
 * Byte-identical to the original: all 288 instructions, the pool and the
 * 50-entry jump table (case targets checked entry by entry against the
 * original table at 8C0D0B9C). tools/shcdiff.py cannot evaluate the
 * ".DATA.W Lcase-Lbase" jump-table entries behind the MOVA and always
 * reports that one instruction as differing, so it is kept here until the
 * tool handles switch tables (as code_8C084324.c). Without this body the
 * layout model after the stub below stands in for its size (see there).
 */
Sint32 func_8C0D0B16(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (EWP(twp)->lock != 0) {
        func_8C0D06F4(twp, mwp, pwp);
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
        pwp->mj.reqaction = 31;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 4:
        twp->mode = 25;
        pwp->mj.reqaction = 31;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 11:
        twp->mode = 22;
        pwp->mj.reqaction = 33;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 1:
        func_8C0D15C0(twp, mwp, pwp);
        twp->mode = 3;
        pwp->mj.reqaction = 3;
        pwp->hpos = 0.0f;
        twp->flag &= ~0x400;
        twp->flag |= TWF_ONPATH;
        break;
    case 9:
    case 10:
    case 12:
        twp->mode = 14;
        pwp->mj.reqaction = 25;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 13:
    case 15:
        func_8C0D15C0(twp, mwp, pwp);
        twp->mode = 14;
        pwp->mj.reqaction = 11;
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 24:
        twp->mode = 17;
        pwp->mj.reqaction = 25;
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
        func_8C0D15C0(twp, mwp, pwp);
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
        func_8C0D15C0(twp, mwp, pwp);
        twp->mode = 52;
        pwp->mj.reqaction = 67;
        twp->ang.x = twp->ang.z = 0;
        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 50:
        twp->mode = 51;
        pwp->mj.reqaction = 69;
        return 1;
    default:
        return 0;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C0D0B16(taskwk *twp, motionwk *mwp, playerwk *pwp))
#if !defined(TARGET_PC)
/*
 * Translation-unit layout model (SHC default build only; the PC build and
 * the NON_MATCHING build, which has the real body above, do not need it).
 * SHC shares literal pools between the functions of a file and dumps them
 * after an unconditional branch once the pending entries get old enough.
 * In the original, func_8C0D0B16's last entries are dumped inside
 * func_8C0D0DF4, so func_8C0D0E8C's entries start a new pool that is dumped
 * inside func_8C0D0F0C, between its far branches to the "return 0" exit:
 * that gives the far form "BF/S; BRA; NOP" of the button test. With only
 * the 44-byte stub in front, SHC keeps the pool open until func_8C0D0E8C
 * and the test becomes a short BT/S (97 instead of 99 instructions).
 * tu_gap_8C0D0B16 stands in for the missing code of func_8C0D0B16 so the
 * pools fall as in the original; it has no address in the image and is
 * never spliced. Any gap of 37..128 stub calls works (checked); 80 is the
 * middle of that window.
 */
#define TU_GAP4  __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink();
#define TU_GAP16 TU_GAP4 TU_GAP4 TU_GAP4 TU_GAP4
void tu_gap_8C0D0B16(void)
{
    TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP16
}
#endif
#endif

Sint32 func_8C0D0DF4(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!(twp->flag & TWF_GROUND)) {
        if (twp->flag & TWF_HOLD) {
            if (func_8C06C880(twp) & 0x200) {
                twp->mode = 22;
                pwp->mj.reqaction = 33;
                return 1;
            }
            func_8C0D15C0(twp, mwp, pwp);
        }
        twp->mode = 7;
        pwp->mj.reqaction = 8;
        return 1;
    }
    return 0;
}

Sint32 func_8C0D0E8C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp->mj.reqaction != 9 && !GetAnalog(twp, NULL, NULL) && pwp->spd.x == 0.0f) {
        if (twp->flag & TWF_HOLD) {
            if (func_8C06C880(twp) & 0xC00) {
                twp->mode = 18;
                pwp->mj.reqaction = 30;
            } else {
                twp->mode = 17;
                pwp->mj.reqaction = 25;
            }
        } else {
            func_8C0CD09C(twp, mwp, pwp);
        }
        return 1;
    }
    return 0;
}

/*
 * The original dumps the literal pool that holds func_8C0D0E8C's entries
 * (func_8C06CD22, func_8C06C880, func_8C0CD09C) inside this function (pool
 * at 8C0D0F88), which turns the button test into the far form
 * "BF/S; BRA; NOP". This depends on the pool state left by
 * func_8C0D0B16: it matches after the C body of
 * func_8C0D0B16 (NON_MATCHING build) and after the stub plus the layout
 * model tu_gap_8C0D0B16 (default build).
 * The button test needs Sint32 operands (as Sonic's 8C0B16BC) for SHC's
 * register choice of the func_8C06C880 address (R2).
 */
Sint32 func_8C0D0F0C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 kind;

    if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].press & (Sint32)D_8C19E3A0) {
        if ((kind = func_8C06C880(twp)) != 0) {
            switch (func_8C06CF60(twp, mwp)) {
            case 4:
            case 3:
            case 2:
            case 1:
                if (kind & 0x400) {
                    twp->mode = 18;
                    pwp->mj.reqaction = 29;
                    pwp->htp = CWP(twp)->hit_cwp->mytask;
                    twp->flag |= TWF_HOLD;
                } else if (kind & 0x200) {
                    twp->mode = 15;
                    pwp->mj.reqaction = 21;
                    if (pwp->spd.x >= pwp->p.jog_speed) {
                        pwp->spd.x = pwp->p.jog_speed;
                    }
                } else if (kind & 0x800) {
                    twp->mode = 18;
                    pwp->mj.reqaction = 29;
                } else {
                    break;
                }
                return 1;
            }
        }
    }
    return 0;
}

/* the angle class test is a one-case switch with an explicit break (an
 * "== 4 &&" condition allocates R1-R3 differently) */
Sint32 func_8C0D100A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 kind;

    func_8C06CB40(twp, mwp);
    if (pwp->mj.nframe >= 12.0f) {
        if ((kind = func_8C06C880(twp)) != 0) {
            switch (func_8C06CF60(twp, mwp)) {
            case 4:
                if (kind & 0x200) {
                    twp->mode = 17;
                    if (B_8C7A2EA8) {
                        func_8C09413A(0x4A5, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
                    }
                    pwp->htp = CWP(twp)->hit_cwp->mytask;
                    pwp->htp->twp->flag |= TWF_REQUEST;
                    twp->flag |= TWF_HOLD;
                    twp->flag &= ~8;
                    return 1;
                }
                break;
            }
            func_8C0CD09C(twp, mwp, pwp);
        }
    }
    return 0;
}

ASM_STUB(void func_8C0D15C0(taskwk *twp, motionwk *mwp, playerwk *pwp))
