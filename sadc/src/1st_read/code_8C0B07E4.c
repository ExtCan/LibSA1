#include "sa1/sa1.h"

/*
 * Sonic: action request handler (1ST_READ 8C0B07E4-8C0B0E94, chunk
 * 8C0AEFA0 function 13/15; called from SonicModeMachine 8C0A966C).
 * SADX analogue: Sonic_NAct. While the event work holds the player
 * (ewp->lock) it only runs the event helper 8C0AFE72 and returns 0.
 * Otherwise, when an action was requested (twp->flag & 0x1000, set by
 * SetInputP), it switches on the requested action twp->smode, sets the new
 * mode and motion (pwp->mj.reqaction) and returns 1 when it took the
 * request. twp->flag bits used: 0x2000 (cleared by most requests),
 * 0x0500 (set/cleared together), 0x0100 (tested), 0x0003 (request 31).
 * pwp->equipment 0x8000 is set by request 46 (Super Sonic: also starts the
 * B_CHAOS7.BIN overlay task and func_8C0B32B6).
 *
 * Matching status (NON_MATCHING): with -define=NON_MATCHING the body
 * compiles to the original instructions except for one line that
 * tools/shcdiff.py cannot compare: the MOVA of the switch jump table
 * (orig value 0x00C8008E = first two table entries; SHC emits the table
 * as .DATA.W label differences, which shcdiff reports as "expr"). The 50
 * jump-table entries were compared byte by byte from the listing and are
 * identical. This needs the translation-unit model at the end of the file
 * (SHC only, NON_MATCHING only): in the original TU func_8C0B1D60 is
 * defined about 3.8 KB after this function (out of BSR range), so SHC
 * calls it with JSR through the pool *without* hoisting its address into
 * a callee-saved register (in-TU callees are BSR candidates during
 * register allocation). Declared extern, SHC hoists it and the whole
 * register allocation changes (about 600 differing instructions).
 * Source-shape findings: the angle resets are chained assignments
 * (twp->ang.x = twp->ang.z = 0: one constant use each, which ranks the
 * hoisted constants 0xDFFF mask > PClearSpeed > 0 > 20 as in R11..R8);
 * request 38 is an if block around the body; the second
 * CreateElementalTask of request 46 is assigned inside the condition;
 * func_8C0B60B8 gets (Uint8)pno (EXTU.B).
 */

/* any work (task.awp, CAlloc(1, 0x10)) of the Super Sonic helper task
 * func_8C0B32B6: the player number at +0 (written by request 46) */
typedef struct {
    Sint8 pno;                  /* 0x00 */
} SONIC_SUPER_AWP;

/* Super Sonic task in the B_CHAOS7.BIN overlay at the absolute address
 * 0x0C900480 (same overlay as the Super Sonic texlist of code_8C0AE0E0.c;
 * C: src/overlays/b_chaos7/code_0C900480.c). PC: the base stub of that
 * address dispatches to the world's B_CHAOS7 image. */
#if defined(TARGET_PC)
extern void func_0C900480(task *tp);
#define SUPERSONIC_EXEC func_0C900480
#else
#define SUPERSONIC_EXEC ((TaskFuncPtr)0x0C900480)
#endif

void func_8C0B1D60(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* Sonic: release the held object (twp->flag & 0x800, pwp->htp) */
void func_8C07B320(taskwk *twp);                                /* starts task 8C044680 for player twp->counter.b[0] */
void func_8C0B60B8(Sint32 pno);                                 /* starts a task, stores pno in its twp->btimer */
void func_8C0B32B6(task *tp);                                   /* Super Sonic helper task (exec) */

ASM_STUB(Sint32 func_8C0AFE72(taskwk *twp, motionwk *mwp, playerwk *pwp))

#ifdef NON_MATCHING
Sint32 func_8C0B07E4(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    task *tp;

    if (((eventwk *)twp->ewp)->lock != 0) {
        func_8C0AFE72(twp, mwp, pwp);
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
        pwp->mj.reqaction = 55;
        twp->flag &= ~0x2000;
        break;
    case 4:
        twp->mode = 31;
        pwp->mj.reqaction = 55;
        twp->flag &= ~0x2000;
        break;
    case 11:
        twp->mode = 28;
        pwp->mj.reqaction = 57;
        twp->flag &= ~0x2000;
        break;
    case 1:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 3;
        pwp->mj.reqaction = 11;
        pwp->hpos = 0.0f;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag |= 0x2000;
        break;
    case 2:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 7;
        pwp->mj.reqaction = 15;
        pwp->hpos = 0.0f;
        twp->flag |= 0x500;
        twp->flag |= 0x2000;
        break;
    case 12:
        twp->mode = 20;
        pwp->mj.reqaction = 37;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 13:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 20;
        pwp->mj.reqaction = 14;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag |= 0x500;
        pwp->work.f = 5.0f;
        func_8C0940DA(728, 0, 0, 0);
        twp->flag &= ~0x2000;
        break;
    case 15:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 20;
        pwp->mj.reqaction = 21;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 14:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 20;
        pwp->mj.reqaction = 12;
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
        twp->flag &= ~0x2000;
        break;
    case 17:
        twp->mode = 20;
        pwp->mj.reqaction = 18;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 18:
        twp->mode = 45;
        pwp->mj.reqaction = 65;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 19:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 20;
        pwp->mj.reqaction = 75;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 20:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 20;
        pwp->mj.reqaction = 77;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2500;
        break;
    case 23:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 20;
        pwp->mj.reqaction = 88;
        twp->flag &= ~0x2500;
        break;
    case 9:
        twp->mode = 20;
        pwp->mj.reqaction = 37;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 10:
        twp->mode = 20;
        pwp->mj.reqaction = 37;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x2000;
        break;
    case 24:
        twp->mode = 23;
        pwp->mj.reqaction = 37;
        twp->flag &= ~0x2000;
        break;
    case 25:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 5;
        pwp->mj.reqaction = 15;
        twp->flag |= 0x500;
        func_8C0940DA(729, 0, 0, 0);
        break;
    case 43:
        twp->mode = 48;
        pwp->mj.reqaction = 18;
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 26:
        if (twp->mode == 25 || pwp->spd.x > pwp->p.run_speed) {
            twp->flag |= 0x2000;
        }
        return 0;
    case 29:
        func_8C0940DA(34, 0, 0, 0);
        func_8C0B1D60(twp, mwp, pwp);
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
        func_8C0B1D60(twp, mwp, pwp);
        func_8C0940DA(34, 0, 0, 0);
        twp->mode = 38;
        pwp->mj.reqaction = 69;
        pwp->mj.nframe = 0.0f;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~0x500;
        twp->flag |= 0x2000;
        break;
    case 33:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 42;
        pwp->mj.reqaction = 81;
        pwp->free.w[3] = 180;
        PClearSpeed(mwp, pwp);
        if (twp->flag & 0x100) {
            twp->flag &= ~0x500;
        }
        twp->flag &= ~0x2000;
        break;
    case 38:
        if (twp->mode != 43) {
            func_8C0B1D60(twp, mwp, pwp);
            twp->mode = 43;
            pwp->mj.reqaction = 82;
            pwp->free.w[3] = 300;
            PClearSpeed(mwp, pwp);
            func_8C0B60B8((Uint8)twp->counter.b[0]);
            if (twp->flag & 0x100) {
                twp->flag &= ~0x500;
            }
            twp->flag &= ~0x2000;
        }
        break;
    case 35:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 18;
        pwp->mj.reqaction = 27;
        if (twp->counter.b[0] == 0) {
            func_8C0940DA(1112, 0, 0, 0);
        }
        pwp->free.w[2] = 0;
        func_8C07B320(twp);
        twp->flag &= ~0x2500;
        break;
    case 34:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 79;
        pwp->mj.reqaction = 3;
        twp->flag &= ~0x2500;
        break;
    case 46:
        func_8C0B1D60(twp, mwp, pwp);
        pwp->equipment |= 0x8000;
        tp = CreateElementalTask(2, 2, SUPERSONIC_EXEC);
        if (tp != NULL) {
            tp->twp->counter.b[0] = twp->counter.b[0];
        }
        if ((tp = CreateElementalTask(8, 2, func_8C0B32B6)) != NULL) {
            ((SONIC_SUPER_AWP *)tp->awp)->pno = twp->counter.b[0];
        }
        twp->mode = 71;
        pwp->mj.reqaction = 130;
        twp->flag &= ~0x2500;
        break;
    case 39:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 81;
        pwp->mj.reqaction = 85;
        twp->flag &= ~0x2500;
        break;
    case 50:
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 82;
        pwp->mj.reqaction = 86;
        twp->flag &= ~0x2500;
        func_8C0940DA(1392, 0, 0, 0);
        break;
    default:
        return 0;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C0B07E4(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

#if defined(NON_MATCHING) && !defined(TARGET_PC)
/*
 * Translation-unit layout model (SHC check only; see the top comment).
 * tu_gap_8C0B0E94 stands in for the code between 8C0B0E94 and 8C0B1D60
 * (about 3.8 KB) so that func_8C0B1D60 is an in-TU function out of BSR
 * range. Neither function is spliced into the game (the gap has no address
 * in the image, func_8C0B1D60 is an ASM_STUB).
 */
#define TU_GAP4  __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink();
#define TU_GAP16 TU_GAP4 TU_GAP4 TU_GAP4 TU_GAP4
#define TU_GAP64 TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP16
void tu_gap_8C0B0E94(void)
{
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
}

ASM_STUB(void func_8C0B1D60(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
