#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"         /* func_8C07B2DC, func_8C06CA30 ... */

/*
 * 1ST_READ 8C0C193A: KnucklesModeMachine (chunk 8C0C0640-8C0C5220,
 * function 2/9; KnucklesMain, func_8C0C450A, func_8C0C4690, func_8C0C46AC,
 * func_8C0C4C2E, func_8C0C4F40, func_8C0C4F48 and func_8C0C513A are
 * separate assignments).
 *
 * Knuckles' action state machine, called once per frame by KnucklesMain
 * (8C0C0640) with (twp, mwp, pwp): first the hurt / invincibility timer on
 * twp->wtimer (0x7FF0 = held) and the damage check (func_8C0C57C8), then a
 * switch on twp->mode (1..53) that decides the next mode and the requested
 * motion (pwp->mj.reqaction) from the input checks of the Knuckles action
 * chunks (func_8C0C5BCA, func_8C0C7080, ... return non-zero when they
 * changed the mode themselves). Same programmer and shape as
 * SonicModeMachine (code_8C0A966C.c) and TailsModeMachine
 * (code_8C0B70F4.c): glide (modes 6/11/12, pwp->free.b[5] glide time in
 * the adventure fields, timer.w[0]), climbing (modes 42-45, TWF_ONPATH),
 * digging (modes 18-21, twp->value.f depth, dirt tasks func_8C0CA9B0),
 * the drill dive (modes 24-26, pwp->free.w[3]), carrying (modes 27-41,
 * TWF_HOLD) and the hold-and-shake escape (mode 48, pwp->free.w[6]).
 *
 * Code generation notes. The function is so large that SHC compiles it
 * without register allocation: twp/mwp/pwp live at @(32/28/24,R15) and the
 * block locals of each case in @(0..23,R15) (allocated from the top, first
 * declaration highest). The cases are in the original source order (the
 * order of their code), not in numeric order. Identical tails are
 * cross-jumped (case 9/21 jumps into the glide start of case 11, case 40
 * into the "drop" tail of case 41, the tails "func_8C0945A0(15); break;"
 * of case 13). The first literal pool of the original (8C0C19EA-8C0C1A3F)
 * also holds KnucklesMain's deferred pool words and the last pool word
 * (case 53, &B_8C754C08[0].press) sits in func_8C0C450A's pool: KnucklesMain,
 * this function and the following helpers were one source file.
 *
 * Status: with the translation-unit model below, the NON_MATCHING body
 * compiles to the original's exact instruction stream (4688 instructions;
 * every encoding, literal-pool value and branch target equal).
 * tools/shcdiff.py --flags=-define=NON_MATCHING still reports one item, the
 * MOVA of the 53-entry switch table at 8C0C1A7A, because it cannot evaluate
 * the table's ".DATA.W Lx-Ly" label differences; all 53 entries were checked
 * separately (resolved through the listing) and reach the same instruction
 * as the original table at 8C0C1A88. The body stays under NON_MATCHING only
 * until shcdiff/splice handle switch tables (as SonicModeMachine).
 *
 * Source idioms the temporary-register choice depends on: the empty
 * "else if (twp->wtimer == 0x7FF0) { } else { ... }" of the hurt timer (a
 * "!= 0x7FF0" test swaps R1/R2 in the decrement), and in case 38 the
 * "if (c) { switch ...; break; } else { drop; ...; break; } break;" form
 * (a plain fall-through drop tail swaps R1/R2 at the start of case 39).
 * FMAC: the six "acc += a * b" (FMAC_ACC: SA1_FMAC on PC); writing them as
 * "acc = a * b + acc" for SHC changes the code completely.
 */

extern Uint32 B_8C754E7C[];     /* per-player input flags (mash detection) */
extern NJS_POINT3 B_8C7A2614;   /* position where Knuckles came out of the ground */

/* Knuckles action checks (chunks 8C0C5680-8C0CA9B0; reached with JSR,
 * return non-zero when they changed twp->mode) */
extern Sint32 func_8C0C57C8(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* damage / wall bounce check */
extern void func_8C0C6DE0(taskwk *twp, motionwk *mwp, playerwk *pwp);    /* drop the held object */
extern Sint32 func_8C0C5BCA(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* next action (requests, event path) */
extern Sint32 func_8C0C7080(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C8330(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C5888(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* jump button */
extern Sint32 func_8C0C67BC(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C71DC(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C71A0(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C70EE(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C7222(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern void func_8C0C5792(taskwk *twp, motionwk *mwp, playerwk *pwp);    /* end of a glide/dash */
extern Sint32 func_8C0C59B6(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C7D4C(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* 2: grabbed a wall */
extern void func_8C0C9000(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C88E6(taskwk *twp);
extern Sint32 func_8C0C61F4(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* next action while carrying */
extern void func_8C0C8534(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C5A20(taskwk *twp, playerwk *pwp);                 /* jump off a wall */
extern Sint32 func_8C0C7FB0(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern void func_8C0C80D4(taskwk *twp, playerwk *pwp);
extern void func_8C0C769E(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C68A0(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C6D36(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C693E(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0C6A7A(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern void func_8C0CA9B0(task *tp);                                      /* dirt particle task */
extern void func_8C0CAE60(NJS_POINT3 *pos, NJS_POINT3 *spd);              /* dirt effect */

/* player common; the call sites pass more arguments than the matched
 * definitions use (code_8C06CCA0.c, code_8C06CFCC.c, code_8C06CA30.c) */
extern Sint32 func_8C06CA30(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Bool func_8C06CE40(taskwk *twp, motionwk *mwp);
extern Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp);
extern Bool func_8C06CEFE(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);
extern Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);
extern void func_8C086F5E(Sint8 flag);
extern void func_8C01ABDC(Sint32 mode, Sint32 a);
extern void func_8C01AE60(void);
extern void func_8C109EAC(Sint32 pno, Sint32 a, Sint32 b, Sint32 c);    /* vibration */

/* same chunk, after this function: reached with BSR from the last cases */
void func_8C0C4F40(taskwk *twp, motionwk *mwp, playerwk *pwp);           /* back to standing */
void func_8C0C4F48(taskwk *twp, motionwk *mwp, playerwk *pwp);           /* stand action selector */
void func_8C0C513A(taskwk *twp, motionwk *mwp, playerwk *pwp);           /* run action selector */

/* button state of the player's pad */
#define PER(twp) (B_8C754C08[(Uint8)(twp)->counter.b[0]])

/* "acc += a * b" compiled to FMAC: fused on PC (SA1_FMAC) */
#if defined(TARGET_PC)
#define FMAC_ACC(acc, a, b) ((acc) = SA1_FMAC((a), (b), (acc)))
#else
#define FMAC_ACC(acc, a, b) ((acc) += (a) * (b))
#endif
#if defined(NON_MATCHING) && !defined(TARGET_PC)
/*
 * Translation-unit layout model (SHC check only, never spliced). In the
 * original TU KnucklesMain ends right before this function with 12 literal
 * pool words still pending (func_8C0C88E6 ... func_8C06C6F8): SHC dumps them
 * into the first pool of this function (8C0C1A00-8C0C1A2F), and the pools
 * are aligned to 32 bytes, so the code after them sits at the original
 * addresses only when the function starts at the original address modulo
 * 32 (0x1A). tu_tail_KnucklesMain leaves 12 pending words and has that
 * size; tu_gap_8C0C450A and tu_gap_8C0C4F48 stand in for the code up to
 * func_8C0C4F40 and func_8C0C513A, which SHC reaches with BSR only from the
 * last cases (in-TU callees out of range are called with JSR). The splicer
 * skips all of them:
 *   ASM_STUB(void tu_tail_KnucklesMain(void **v, Sint32 *w))
 *   ASM_STUB(void tu_gap_8C0C450A(void))
 *   ASM_STUB(void tu_gap_8C0C4F48(void))
 */
extern Sint32 tu_pool0, tu_pool1, tu_pool2, tu_pool3, tu_pool4, tu_pool5;
extern Sint32 tu_pool6, tu_pool7, tu_pool8, tu_pool9, tu_pool10, tu_pool11;
void tu_tail_KnucklesMain(void **v, Sint32 *w)
{
    w[0] = 1;
    w[1] = 2;
    w[2] = 3;
    w[3] = 4;
    w[4] = 5;
    w[5] = 6;
    w[6]++;
    v[0] = &tu_pool0;
    v[1] = &tu_pool1;
    v[2] = &tu_pool2;
    v[3] = &tu_pool3;
    v[4] = &tu_pool4;
    v[5] = &tu_pool5;
    v[6] = &tu_pool6;
    v[7] = &tu_pool7;
    v[8] = &tu_pool8;
    v[9] = &tu_pool9;
    __asm_stub_sink();
    v[10] = &tu_pool10;
}
#endif

#ifdef NON_MATCHING
void KnucklesModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp != NULL && (pwp->item & 0x8000)) {
        twp->flag |= 0x8000;
        twp->wtimer = 0;
    } else if (twp->wtimer == 0) {
        if (func_8C0C57C8(twp, mwp, pwp)) {
            twp->flag |= 0x8000;
            if (twp->flag & 0x800) {
                func_8C0C6DE0(twp, mwp, pwp);
            }
            twp->flag &= ~0x3900;
            if (pwp->item & 3) {
                pwp->item &= ~3;
            } else {
                func_8C086F5E(twp->counter.b[0]);
            }
        } else {
            twp->flag &= ~0x8000;
        }
    } else if (twp->wtimer == 0x7FF0) {
        /* held */
    } else {
        twp->wtimer--;
        twp->flag &= ~4;
    }

    switch (twp->mode) {
    case 1:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (func_8C0C8330(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) break;
        if (func_8C0C67BC(twp, mwp, pwp)) break;
        if (func_8C0C71DC(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL) || pwp->spd.x != 0.0f) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
        }
        break;

    case 2:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (pwp->spd.x > 2.0f) {
            if (func_8C0C5888(twp, mwp, pwp)) break;
            if (func_8C0C71A0(twp, mwp, pwp)) break;
        } else {
            if (func_8C0C8330(twp, mwp, pwp)) break;
            if (func_8C0C5888(twp, mwp, pwp)) break;
            if (func_8C0C71DC(twp, mwp, pwp)) break;
        }
        if (func_8C0C70EE(twp, mwp, pwp)) break;
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && (CWP(twp)->flag & 8)
            && (CWP(twp)->hit_cwp->info[CWP(twp)->hit_num].b05 & 0xF)) {
            switch (func_8C06CF60(twp, mwp)) {
            case 4:
                twp->mode = 13;
                pwp->mj.reqaction = 23;
            }
        }
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 10;
            pwp->mj.reqaction = 21;
            func_8C0940DA(18, 0, 0, 0);
        }
        break;

    case 3:
        twp->flag &= ~0x500;
        if (func_8C0C5BCA(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0C7080(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0C5888(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0C70EE(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 4:
        if (func_8C0C5BCA(twp, mwp, pwp)) {
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C0C7080(twp, mwp, pwp)) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 2.0f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            break;
        }
        if (func_8C0C5888(twp, mwp, pwp)) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 2.0f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            break;
        }
        if (func_8C0C7222(twp, mwp, pwp)) {
            twp->flag &= ~0x500;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) {
            twp->mode = 2;
            pwp->mj.reqaction = 33;
            twp->flag &= ~0x500;
            break;
        }
        break;

    case 5:
        if (func_8C0C5BCA(twp, mwp, pwp)) {
            twp->flag &= ~0x500;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0C7080(twp, mwp, pwp)) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 2.0f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (pwp->spd.x < 1.84999993f) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 2.0f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 6:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 10;
                pwp->mj.reqaction = 21;
            } else if (func_8C0C70EE(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 1;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0C513A(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (pwp->mj.reqaction == 14) {
            if (func_8C06CEBE(twp, pwp)) {
                twp->mode = 11;
                pwp->mj.reqaction = 51;
                func_8C0C5792(twp, mwp, pwp);
                twp->flag &= ~0x500;
                twp->flag |= 0x400;
                pwp->free.b[5] = 0;
                break;
            }
        } else if (pwp->mj.reqaction == 34) {
            if (((Sint32)PER(twp).on & (Sint32)D_8C19E390)
                && pwp->mj.nframe > (Float)(KnucklesActionList[34].actptr->motion->nbFrame - 2)) {
                twp->mode = 11;
                pwp->mj.reqaction = 51;
                func_8C0C5792(twp, mwp, pwp);
                twp->flag &= ~0x500;
                twp->flag |= 0x400;
                pwp->free.b[5] = 0;
                break;
            }
        }
        if (func_8C0C59B6(twp, mwp, pwp)) break;
        if (func_8C06CEFE(twp, pwp)) {
            FMAC_ACC(pwp->spd.y, pwp->p.jmp_addit, 0.79999997f);
            break;
        }
        if (mwp->spd.y > 0.0f) break;
        if (func_8C0C7D4C(twp, mwp, pwp) == 2) {
            twp->flag &= ~0x500;
            func_8C0C9000(twp, mwp, pwp);
            break;
        }
        break;

    case 7:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 9;
            pwp->mj.reqaction = 18;
        }
        break;

    case 8:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 9;
            pwp->mj.reqaction = 18;
        }
        break;

    case 9:
    case 21:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 10;
                pwp->mj.reqaction = 21;
            } else if (func_8C0C70EE(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 1;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0C513A(twp, mwp, pwp);
            }
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E390) {
            twp->mode = 6;
            pwp->mj.reqaction = 34;
            pwp->work.f = 2.0f;
            twp->flag |= 0x500;
            pwp->jumptimer = 0;
            func_8C0945A0(0x2E1);
            break;
        }
        break;

    case 10:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 21) {
            func_8C0C4F40(twp, mwp, pwp);
        }
        break;
    case 11:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (func_8C0C88E6(twp)) {
            if (GetStageNumber() < 0xF00) {
                func_8C01ABDC(68, 0);
            }
            mwp->ang_aim.y = twp->ang.y + 0x8000;
            twp->timer.w[0] = 30;
            twp->mode = 12;
            break;
        }
        if (twp->flag & 3) {
            twp->flag &= ~0x500;
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 10;
                pwp->mj.reqaction = 53;
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
            } else if (func_8C0C70EE(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 53;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0C513A(twp, mwp, pwp);
            }
            func_8C0945A0(0x2E1);
            break;
        }
        if (!((Sint32)PER(twp).on & (Sint32)D_8C19E390)) {
            twp->mode = 6;
            pwp->mj.reqaction = 34;
            pwp->work.f = 2.0f;
            twp->flag |= 0x500;
            pwp->jumptimer = 0;
            func_8C0945A0(0x2E1);
            break;
        }
        {
            Uint16 stg;

            if ((stg = GetStageNumber()) >= 0x1A00 && stg < 0x2300) {
                if (pwp->free.b[5] >= 60) {
                    twp->flag &= ~0x400;
                    twp->mode = 9;
                    pwp->mj.reqaction = 19;
                    break;
                } else if (--pwp->free.b[5] < 0) {
                    pwp->free.b[5] = 0;
                }
            }
        }
        if (!(++twp->timer.w[0] & 63)) {
            func_8C0940DA(0x2E1, 0, 0, 0);
        }
        break;

    case 12:
        if (!func_8C0C88E6(twp)) {
            if (--twp->timer.w[0] <= 0) {
                if (GetStageNumber() < 0xF00) {
                    func_8C01AE60();
                }
                twp->mode = 11;
                pwp->free.b[5] = 0;
                break;
            }
            break;
        }
        if (twp->timer.w[0] != 30) {
            twp->timer.w[0] = 30;
        }
        break;

    case 13:
        if (func_8C0C5BCA(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0C5888(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0C7080(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0C70EE(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4)
            && !(CWP(twp)->hit_cwp->info[CWP(twp)->hit_num].attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                break;
            case 3:
                if (!(twp->flag & 0x800)) {
                    twp->mode = 2;
                    pwp->mj.reqaction = 9;
                    func_8C0945A0(15);
                }
                break;
            default:
                if (twp->flag & 0x800) {
                    twp->mode = 38;
                    pwp->mj.reqaction = 85;
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 9;
                }
                func_8C0945A0(15);
                break;
            }
            break;
        }
        func_8C0C6DE0(twp, mwp, pwp);
        twp->mode = 2;
        pwp->mj.reqaction = 9;
        func_8C0945A0(15);
        break;

    case 14:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (twp->wtimer == 120) break;
        if (twp->flag & 3) {
            if (func_8C0C70EE(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 26;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0C513A(twp, mwp, pwp);
            }
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E390) {
            twp->mode = 6;
            pwp->mj.reqaction = 34;
            pwp->work.f = 2.0f;
            twp->flag |= 0x500;
            pwp->jumptimer = 0;
            break;
        }
        break;

    case 15:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) break;
        if (func_8C0C70EE(twp, mwp, pwp)) break;
        if (pwp->spd.x >= 0.0f) {
            twp->mode = 2;
            func_8C0C513A(twp, mwp, pwp);
            break;
        }
        break;

    case 16:
        if (twp->flag & 0x800) {
            if (func_8C0C61F4(twp, mwp, pwp)) break;
        } else {
            if (func_8C0C5BCA(twp, mwp, pwp)) break;
        }
        break;

    case 17:
        if (pwp->mj.reqaction == 31) break;
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 32) {
            func_8C0C4F40(twp, mwp, pwp);
        }
        break;

    case 18:
        if (func_8C0C5BCA(twp, mwp, pwp)) {
            if (GetStageNumber() < 0xF00) {
                func_8C01AE60();
            }
            break;
        }
        {
            Sint32 found;
            Sint32 i;
            NJS_POINT3 pos;

            found = 0;
            if (twp->timer.w[1] < 150) {
                for (i = 3; i > 0; i--) {
                    GetPlayerPosition(twp->counter.b[0], i, &pos, NULL);
                    if (njDistanceP2P(&twp->pos, &pos) > 0.99999995f) break;
                }
                if (i == 0) {
                    found = 1;
                }
            }
            if (found == 1 || --twp->timer.w[1] <= 0) {
                if (GetStageNumber() < 0xF00) {
                    func_8C01AE60();
                }
                twp->flag &= ~0x400;
                twp->mode = 9;
                pwp->mj.reqaction = 19;
                break;
            }
        }
        break;

    case 19:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        twp->timer.w[0]++;
        if (twp->timer.w[0] > 25 && !(twp->timer.w[0] & 7)) {
            Sint32 ang;
            task *tp;
            NJS_POINT3 v;

            for (ang = SA1_FTOI((Float)rand() * 0.0625f); ang < 0x10000; ang += 0x800) {
                tp = CreateElementalTask(2, 6, func_8C0CA9B0);
                if (tp != NULL) {
                    tp->twp->pos = twp->pos;
                    tp->twp->scl.x = (Float)rand() * njSin(ang) * 3.0517578125e-05f;
                    tp->twp->scl.y = (Float)rand() * 3.0517578125e-05f;
                    tp->twp->scl.z = (Float)rand() * njCos(ang) * 3.0517578125e-05f;
                }
            }
            v.x = v.z = 0.0f;
            v.y = 1.0f;
            func_8C0CAE60(&twp->pos, &v);
        }
        if (twp->timer.w[0] < 40) break;
        twp->value.f -= 0.39999998f;
        if (twp->value.f < -20.0f) {
            func_8C0C8534(twp, mwp, pwp);
            twp->mode = 20;
            pwp->mj.reqaction = 43;
            func_8C0945A0(0x2E6);
        }
        if (!(twp->timer.w[0] & 7)) {
            func_8C0945A0(0x2E6);
            func_8C0940DA(0x2E6, 0, 0, 0);
            func_8C109EAC((Uint8)twp->counter.b[0], 2, 15, 0);
        }
        break;

    case 20:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        twp->value.f += 2.0f;
        if (twp->value.f >= 0.0f) {
            B_8C7A2614 = twp->pos;
            pwp->spd.y = 2.0f;
            twp->mode = 21;
            pwp->mj.reqaction = 44;
            break;
        }
        if (!(++twp->timer.w[0] & 7)) {
            Sint32 ang;
            task *tp;
            NJS_POINT3 v;

            for (ang = SA1_FTOI((Float)rand() * 0.25f); ang < 0x10000; ang += 0x2000) {
                tp = CreateElementalTask(2, 6, func_8C0CA9B0);
                if (tp != NULL) {
                    tp->twp->pos = twp->pos;
                    tp->twp->scl.x = (Float)rand() * njSin(ang) * 3.0517578125e-05f;
                    tp->twp->scl.y = (Float)rand() * 0.0001068115234375f;
                    tp->twp->scl.z = (Float)rand() * njCos(ang) * 3.0517578125e-05f;
                }
            }
            v.x = v.z = 0.0f;
            v.y = 2.0f;
            func_8C0CAE60(&twp->pos, &v);
        }
        break;

    case 22:
    case 23:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (twp->mode != 23) break;
        if (func_8C0C5888(twp, mwp, pwp)) break;
        if (func_8C0C67BC(twp, mwp, pwp)) break;
        if (func_8C0C71DC(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL) || pwp->spd.x != 0.0f) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
        }
        break;
    case 24:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        {
            NJS_POINT3 p;
            NJS_POINT3 v;

            if (twp->flag & 0x22) {
                pwp->spd.x = pwp->spd.y = pwp->spd.z = mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
                FMAC_ACC(twp->pos.x, pwp->p.height * pwp->cstsp->normal2.x, 0.29999999f);
                FMAC_ACC(twp->pos.z, pwp->p.height * pwp->cstsp->normal2.z, 0.29999999f);
                func_8C0C4F40(twp, mwp, pwp);
                break;
            }
            if (mwp->spd.y >= 0.0f) {
                if (pwp->cstsp->normal2.y >= 0.85999996f) {
                    p.x = p.z = 0.0f;
                    p.y = -1.0f;
                    PConvertVector_P2G(twp, &p);
                    mwp->spd.x = p.x * pwp->p.height * 0.01f;
                    mwp->spd.z = p.z * pwp->p.height * 0.01f;
                    mwp->spd.y = pwp->p.height * 0.0279999992f;
                    twp->mode = 25;
                    pwp->mj.reqaction = 50;
                    break;
                }
            } else if (pwp->cstsp->normal2.y >= 0.85999996f) {
                pwp->spd.x = pwp->spd.y = pwp->spd.z = mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
                FMAC_ACC(twp->pos.x, pwp->p.height * pwp->cstsp->normal2.x, 0.29999999f);
                FMAC_ACC(twp->pos.z, pwp->p.height * pwp->cstsp->normal2.z, 0.29999999f);
                func_8C0C4F40(twp, mwp, pwp);
                break;
            }
            if (func_8C0C5A20(twp, pwp)) break;
            if (!func_8C0C7FB0(twp, mwp, pwp)) {
                if (--pwp->free.w[3] <= 0) {
                    twp->mode = 9;
                    pwp->mj.reqaction = 19;
                    break;
                }
            } else {
                pwp->free.w[3] = 6;
            }
            switch (pwp->mj.reqaction) {
            case 46:
            case 47:
            case 48:
                if ((pwp->mj.nframe >= 0.0f && pwp->mj.nframe < 1.5f)
                    || (pwp->mj.nframe >= 16.0f && pwp->mj.nframe < 17.5f)) {
                    p = twp->pos;
                    p.y += 4.0f;
                    v.x = v.y = v.z = 0.0f;
                    func_8C0CAE60(&p, &v);
                    func_8C0940DA(0x2EA, 0, 0, 0);
                }
                break;
            }
        }
        func_8C0C80D4(twp, pwp);
        break;

    case 26:
        if (func_8C0C5BCA(twp, mwp, pwp) || pwp->mj.reqaction == 0) {
            CWP(twp)->info[1].attr |= 0x10;
            func_8C0C4F40(twp, mwp, pwp);
            twp->flag &= ~0x400;
            break;
        }
        if ((pwp->mj.reqaction == 54 || pwp->mj.reqaction == 59) && func_8C0C8330(twp, mwp, pwp)) {
            twp->flag &= ~0x400;
            break;
        }
        func_8C0C769E(twp, mwp, pwp);
        if (!((Sint32)PER(twp).on & (Sint32)D_8C19E38C)) {
            pwp->free.w[5] = -1;
        }
        break;

    case 25:
        if (pwp->mj.reqaction == 0) {
            twp->mode = 1;
        }
        break;

    case 42:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        {
            Angle ang;

            if (twp->flag & TWF_ONPATH) {
                if (GetAnalog(twp, &ang, NULL)) {
                    if (DiffAngle(twp->ang.y, ang) < 0x4000) {
                        pwp->spd.x = 0.375f;
                        if (pwp->mj.reqaction == 103) {
                            pwp->mj.start_frame = 0.0f;
                        } else {
                            pwp->mj.start_frame = 32.0f;
                        }
                    } else {
                        pwp->spd.x = -0.375f;
                        if (pwp->mj.reqaction == 103) {
                            pwp->mj.start_frame = 0.0f;
                        } else {
                            pwp->mj.start_frame = 32.0f;
                        }
                    }
                    twp->mode = 43;
                    pwp->mj.reqaction = 101;
                    break;
                }
            } else {
                twp->mode = 9;
                pwp->mj.reqaction = 19;
                twp->flag &= ~TWF_ONPATH;
                break;
            }
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) {
            twp->mode = 9;
            pwp->mj.reqaction = 19;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 43:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (twp->flag & TWF_ONPATH) {
        } else {
            twp->mode = 9;
            pwp->mj.reqaction = 19;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) {
            twp->mode = 9;
            pwp->mj.reqaction = 19;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 44:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (twp->flag & TWF_ONPATH) {
            Angle ang;

            if (GetAnalog(twp, &ang, NULL)) {
                if (DiffAngle(0xC000 - camera_twp->ang.y, ang) < 0x4000) {
                    pwp->spd.x = 0.375f;
                    break;
                } else {
                    pwp->spd.x = -0.375f;
                    break;
                }
            }
            break;
        } else {
            twp->mode = 9;
            pwp->mj.reqaction = 19;
            PClearSpeed(mwp, pwp);
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 45:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (twp->flag & TWF_ONPATH) {
        } else {
            twp->mode = 9;
            pwp->mj.reqaction = 19;
            PClearSpeed(mwp, pwp);
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 46:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
            mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
            if (func_8C05E6E8(twp)) {
                twp->mode = 47;
                pwp->mj.reqaction = 106;
            } else {
                twp->mode = 1;
                pwp->mj.reqaction = 2;
            }
        }
        break;

    case 47:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (!func_8C05E6E8(twp)) {
            twp->mode = 1;
            pwp->mj.reqaction = 2;
            break;
        }
        if (func_8C06CEBE(twp, pwp) == 2) {
            func_8C05E788(twp);
            twp->mode = 46;
            pwp->mj.reqaction = 105;
        }
        break;

    case 48:
        if (func_8C0C5BCA(twp, mwp, pwp)) {
            func_8C07B2DC(twp->counter.b[0]);
            func_8C0940DA(16, 0, 0, 0);
            break;
        }
        if (B_8C754E7C[(Uint8)twp->counter.b[0]] & 0x306FE) {
            pwp->free.w[6] -= 20;
        } else {
            pwp->free.w[6]--;
        }
        if (pwp->free.w[6] <= 0) {
            twp->mode = 6;
            pwp->spd.y = pwp->p.jmp_y_spd;
            pwp->mj.reqaction = 14;
            pwp->work.f = 2.0f;
            twp->flag &= ~2;
            twp->flag |= 0x500;
            pwp->jumptimer = 0;
            func_8C0940DA(17, 0, 0, 0);
            func_8C0940DA(16, 0, 0, 0);
            func_8C07B2DC(twp->counter.b[0]);
            break;
        }
        break;

    case 27:
        if (twp->flag & 0x800) {
            if (func_8C0C61F4(twp, mwp, pwp)) break;
        } else {
            if (func_8C0C5BCA(twp, mwp, pwp)) break;
        }
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (func_8C0C68A0(twp, mwp, pwp)) break;
        break;

    case 30:
        if (func_8C0C6D36(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) break;
        break;

    case 29:
        if (func_8C0C61F4(twp, mwp, pwp)) break;
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (((Sint32)PER(twp).on & (Sint32)D_8C19E3A0) && GetAnalog(twp, NULL, NULL)
            && pwp->htp->twp->id == 4) {
            twp->mode = 41;
            pwp->mj.reqaction = 87;
            break;
        }
        if (func_8C0C693E(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 31;
            pwp->mj.reqaction = 75;
        }
        break;

    case 31:
        if (func_8C0C61F4(twp, mwp, pwp)) break;
        if (func_8C0C7080(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0C5888(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0C693E(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0C70EE(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 33;
            pwp->mj.reqaction = 72;
            func_8C0940DA(18, 0, 0, 0);
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;
    case 28:
    case 32:
        if (twp->flag & 0x800) {
            if (func_8C0C61F4(twp, mwp, pwp)) break;
        } else {
            if (func_8C0C5BCA(twp, mwp, pwp)) break;
        }
        if (func_8C0C6A7A(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 96 && pwp->mj.reqaction != 97 && pwp->mj.reqaction != 100) {
            func_8C0C4F40(twp, mwp, pwp);
        }
        break;

    case 33:
        if (func_8C0C61F4(twp, mwp, pwp)) break;
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 31;
            pwp->mj.reqaction = 73;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 72) {
            twp->mode = 29;
            pwp->mj.reqaction = 74;
        }
        break;

    case 34:
        if (func_8C0C61F4(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 33;
                pwp->mj.reqaction = 72;
            } else if (func_8C0C70EE(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 74;
                } else {
                    pwp->mj.reqaction = 95;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 31;
                func_8C0C513A(twp, mwp, pwp);
            }
        } else if (func_8C0C693E(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 35:
        if (func_8C0C61F4(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 33;
                pwp->mj.reqaction = 72;
            } else if (func_8C0C70EE(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 74;
                } else {
                    pwp->mj.reqaction = 95;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 31;
                func_8C0C513A(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C06CEFE(twp, pwp)) {
            FMAC_ACC(pwp->spd.y, pwp->p.jmp_addit, 0.39999998f);
        }
        if (func_8C0C693E(twp, mwp, pwp)) break;
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 34;
            pwp->mj.reqaction = 93;
        }
        break;

    case 36:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 34;
            pwp->mj.reqaction = 93;
        }
        break;

    case 37:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 34;
            pwp->mj.reqaction = 93;
        }
        break;

    case 38:
        if (func_8C0C5BCA(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) break;
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (!((Sint32)PER(twp).on & (Sint32)D_8C19E3A0)) {
            func_8C0C4F40(twp, mwp, pwp);
            func_8C0C6DE0(twp, mwp, pwp);
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4)
            && !(CWP(twp)->hit_cwp->info[CWP(twp)->hit_num].attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                twp->mode = 13;
                pwp->mj.reqaction = 23;
                break;
            case 0:
                if (twp->flag & 0x800) {
                    twp->mode = 30;
                    pwp->mj.reqaction = 91;
                }
                break;
            default:
                if (twp->flag & 0x800) {
                    twp->mode = 38;
                    pwp->mj.reqaction = 85;
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 9;
                }
                break;
            }
            break;
        } else {
            func_8C0C6DE0(twp, mwp, pwp);
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            break;
        }
        break;

    case 39:
        if (twp->flag & 0x800) {
            if (func_8C0C61F4(twp, mwp, pwp)) break;
        } else {
            if (func_8C0C5BCA(twp, mwp, pwp)) break;
        }
        if (twp->flag & 3) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 33;
                pwp->mj.reqaction = 72;
            } else if (func_8C0C70EE(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (twp->flag & 0x800) {
                    if (mwp->spd.y > -5.0f) {
                        pwp->mj.reqaction = 74;
                    } else {
                        pwp->mj.reqaction = 95;
                    }
                } else {
                    if (mwp->spd.y > -5.0f) {
                        func_8C0C4F48(twp, mwp, pwp);
                    } else {
                        pwp->mj.reqaction = 20;
                    }
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (twp->flag & 0x800) {
                    twp->mode = 31;
                } else {
                    twp->mode = 2;
                }
                func_8C0C513A(twp, mwp, pwp);
            }
        } else if (!func_8C0C6A7A(twp, mwp, pwp) && pwp->mj.reqaction != 98 && pwp->mj.reqaction != 99) {
            twp->mode = 9;
            pwp->mj.reqaction = 19;
        }
        break;

    case 40:
        if (func_8C0C61F4(twp, mwp, pwp)) break;
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) break;
        if (!func_8C06CA30(twp, mwp, pwp)) {
            func_8C0C4F40(twp, mwp, pwp);
            func_8C0C6DE0(twp, mwp, pwp);
            break;
        }
        if (!GetAnalog(twp, NULL, NULL)) {
            twp->mode = 30;
            pwp->mj.reqaction = 91;
        }
        break;

    case 41:
        if (func_8C0C61F4(twp, mwp, pwp)) break;
        if (func_8C0C7080(twp, mwp, pwp)) break;
        if (func_8C0C5888(twp, mwp, pwp)) break;
        if (pwp->htp == NULL || pwp->htp->twp->id != 4) {
            func_8C0C4F40(twp, mwp, pwp);
            func_8C0C6DE0(twp, mwp, pwp);
            break;
        }
        if (!GetAnalog(twp, NULL, NULL)) {
            twp->mode = 29;
            pwp->mj.reqaction = 89;
            break;
        }
        break;

    case 49:
        if (EWP(twp)->list4C == NULL) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0C4F40(twp, mwp, pwp);
        }
        break;

    case 52:
        if ((twp->flag & TWF_REQUEST) && twp->smode == 28) {
            twp->mode = 7;
            pwp->mj.reqaction = 17;
            twp->flag &= ~0x500;
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 53:
        if (B_8C754C08[0].press & 4) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
        }
        break;
    }
}
#else
ASM_STUB(void KnucklesModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

#if defined(NON_MATCHING) && !defined(TARGET_PC)
#define TU_GAP4  __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink();
#define TU_GAP16 TU_GAP4 TU_GAP4 TU_GAP4 TU_GAP4
#define TU_GAP64 TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP16
void tu_gap_8C0C450A(void)
{
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP16 TU_GAP16
}
#endif

/* same chunk, after this function (code_8C0C4C2E.c); stubs only so that
 * SHC reaches them with BSR from the last cases */
ASM_STUB(void func_8C0C4F40(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C0C4F48(taskwk *twp, motionwk *mwp, playerwk *pwp))

#if defined(NON_MATCHING) && !defined(TARGET_PC)
void tu_gap_8C0C4F48(void)
{
    TU_GAP64 TU_GAP16 TU_GAP4 TU_GAP4 TU_GAP4
}
#endif

ASM_STUB(void func_8C0C513A(taskwk *twp, motionwk *mwp, playerwk *pwp))
