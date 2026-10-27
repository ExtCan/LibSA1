#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"     /* func_8C0DEFB6 (Bool), func_8C06CA30..func_8C06CFCC */

/*
 * 1ST_READ 8C0D7960: AmyModeMachine (chunk 8C0D6B40-8C0DCA6C, function 2/32).
 *
 * Amy's action state machine, called once per frame by AmyMain (8C0D6B40,
 * BSR) with (twp, mwp, pwp): first the hurt / invincibility timer on
 * twp->wtimer and the damage check (func_8C0DB000), then a switch on
 * twp->mode (1..49) that decides the next mode and the requested motion
 * (pwp->mj.reqaction) from the input checks of the chunk (func_8C0DB71A,
 * func_8C0DC9BE, ... return non-zero when they changed the mode themselves).
 * Same programmer and shape as SonicModeMachine (code_8C0A966C.c) and
 * E102ModeMachine (code_8C0CD0A8.c). Amy specials: the hammer jump
 * (modes 19-21, free.f[6]/f[7] hammer morph weights, effect tasks
 * func_8C0DD03C/func_8C0DF2A0), the spinning hammer (mode 20, reqaction
 * 85-88, free.w[4] spin counter, free.b[1]/b[3] stick history), the
 * hammer rebound (modes 17/18, free.f[5]) and the Warrior Feather jump
 * (equipment 0x100, AmyActionList[82].next).
 *
 * Status: instruction-identical to the original. With the two SHC-only
 * translation-unit stand-ins below, tools/shcdiff.py
 * --flags=-define=NON_MATCHING matches 4234 of the 4235 instructions, with
 * every pool value and branch target equal; the one item it reports is the
 * MOVA of the 49-entry switch jump table (".DATA.W Lcase-Lbase" items it
 * cannot evaluate). All 49 entries were checked to reach the same
 * instruction (by index) as the original table at 8C0D7A74. Hence the
 * NON_MATCHING guard, as for E102ModeMachine and AmyMain: a byte-exact
 * splice needs AmyMain, this function and func_8C0DA106..func_8C0DA8DA in
 * one translation unit. With the stand-ins every instruction also sits at
 * its original offset from the function start (the first literal pool
 * holds one extra word of tu_AmyMain_tail, absorbed by its 32-byte
 * alignment padding).
 *
 * Code generation notes. The function is so large that SHC compiles it
 * without register allocation: twp/mwp/pwp live at @(40/36/32,R15) and every
 * block local in @(0..28,R15); R8/FR12 only hold temporaries across calls.
 * The cases are in the original source order (the order of their code),
 * not in numeric order. What the C has to reproduce, besides the
 * expressions:
 *  - SHC picks the R1-R3 scratch registers from a state that runs through
 *    the source: code after "if (c) { ...; break; }" continues from the
 *    then-block, an else-block from the condition. Hence the "break;"
 *    closing the then-blocks of cases 12, 17, 37, 44 and of case 21/89,
 *    the nested ifs of cases 20 and 21/default, the "<= 1.0f" block of
 *    case 18 and the "nframe >= f - 2.0f" block of case 21/89; even the
 *    first registers of the function depend on the end of AmyMain (see
 *    tu_AmyMain_tail);
 *  - identical tails that end in a jump to the end are cross-jumped into
 *    the last one: "func_8C0DA6E0(); break;" of cases 19, 20 (spin end) and
 *    18 into case 44's. Calls in an else arm fall through to the join and
 *    are never merged: hence the if/else of cases 21/90, 30 and 35 (their
 *    func_8C0DA8DA tails would otherwise merge with each other and with
 *    case 12's);
 *  - func_8C0DA6E0/6E8/8DA are BSR only from cases 31, 34, 35, 37 and 45
 *    (SHC's distance estimate; see tu_gap_8C0DA106 at the end);
 *  - the ten FMACs are the SA1_FMAC uses and the SH_ONLY/PC_ONLY "+=" forms
 *    (the a * b + c spelling loads the accumulator later and changes the
 *    code on SHC).
 *
 * PC notes: AmyActionList[82].next is written with a halfword store
 * (MOV.W at 8C0D885C, as in func_8C0DCB22): the header has PL_ACTION.next
 * as Uint8 + pad07, so the store goes through a Sint16 pointer (little
 * endian, -fno-strict-aliasing: next = 83, pad07 = 0).
 */

extern Uint32 B_8C754E7C[];     /* per-player input flags (mash detection) */

/* Amy (same chunk, reached with JSR: out of BSR range) */
Sint32 func_8C0DB000(taskwk *twp, motionwk *mwp, playerwk *pwp);       /* damage check */
Sint32 func_8C0DB08C(taskwk *twp, motionwk *mwp, playerwk *pwp);       /* jump check */
Sint32 func_8C0DB230(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DB28E(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DB380(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DB3BA(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DB5E4(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DB71A(taskwk *twp, motionwk *mwp, playerwk *pwp);       /* input/request check */
Sint32 func_8C0DBC16(taskwk *twp, motionwk *mwp, playerwk *pwp);       /* request check while holding */
Sint32 func_8C0DC120(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DC1E2(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DC2A0(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DC3C0(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DC662(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0DC75E(taskwk *twp, motionwk *mwp, playerwk *pwp);         /* release the held object */
Sint32 func_8C0DC9BE(taskwk *twp, motionwk *mwp, playerwk *pwp);
/* Amy (chunks 8C0DCA6C-8C0DFFA0) */
Sint32 func_8C0DCA6C(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DCAE0(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DCB22(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0DCBA0(taskwk *twp, playerwk *pwp);
Sint32 func_8C0DCC1A(taskwk *twp, playerwk *pwp);
Sint32 func_8C0DCE94(taskwk *twp, playerwk *pwp);
void func_8C0DD03C(task *tp);                                           /* hammer effect task */
void func_8C0DF2A0(NJS_POINT3 *pos, Angle angx, Angle angz);
void func_8C0DF90C(task *tp);

/* player common; the call sites pass more arguments than the matched
 * definitions use (code_8C06CCA0.c, code_8C06CFCC.c, code_8C06CA30.c) */
extern Sint32 func_8C06CA30(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Bool func_8C06CE40(taskwk *twp, motionwk *mwp);
extern Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp);
extern Bool func_8C06CEFE(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);
extern Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);
extern void func_8C086F5E(Sint8 flag);
extern void func_8C109EAC(Sint32 pno, Sint32 a, Sint32 b, Sint32 c);  /* pad vibration */

/* Amy (same chunk, reached with BSR from the last cases): defined after the
 * TU layout model at the end of the file */
void func_8C0DA6E0(taskwk *twp, motionwk *mwp, playerwk *pwp);         /* back to standing */
void func_8C0DA6E8(taskwk *twp, motionwk *mwp, playerwk *pwp);         /* stand action selector */
void func_8C0DA8DA(taskwk *twp, motionwk *mwp, playerwk *pwp);         /* run action selector */

/* button state of the player's pad */
#define PER(twp) (B_8C754C08[(Uint8)(twp)->counter.b[0]])

/* hit shape of the collision partner */
#define AMY_HITINFO(twp) (CWP(twp)->hit_cwp->info[CWP(twp)->hit_num])

#if defined(NON_MATCHING) && !defined(TARGET_PC)
/*
 * Translation-unit layout model, part 1 (SHC check only, never spliced).
 * In the original TU AmyMain (8C0D6B40) is compiled just before this
 * function, and SHC's choice of the first scratch registers here (R1 for
 * the pwp != NULL test, R3 for the reload of pwp) continues from the end
 * of AmyMain ("...; func_8C0DF0D0(); pwp->free.b[6] = 0;"):
 * tu_AmyMain_tail leaves the same state (without it the prologue uses R2).
 * tu_AmyMain_align only makes this function start on a 32-byte boundary,
 * as at 8C0D7960: with both, every instruction of AmyModeMachine sits at
 * the same offset from the function start as in the game.
 */
Sint32 tu_AmyMain_align(Sint32 a)
{
    __asm_stub_sink();
    __asm_stub_sink();
    __asm_stub_sink();
    return a;
}

void tu_AmyMain_tail(task *tp, taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    __asm_stub_sink();
    __asm_stub_sink();
    pwp->free.b[6] = 0;
}
#endif

#ifdef NON_MATCHING
void AmyModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp != NULL && (pwp->item & 0x8000)) {
        twp->flag |= 0x8000;
        twp->wtimer = 0;
    } else if (twp->wtimer == 0) {
        if (func_8C0DB000(twp, mwp, pwp)) {
            twp->flag |= 0x8000;
            if (twp->flag & TWF_HOLD) {
                func_8C0DC75E(twp, mwp, pwp);
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
    } else {
        twp->wtimer--;
        twp->flag &= ~4;
    }

    switch (twp->mode) {
    case 1:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (func_8C0DCC1A(twp, pwp)) break;
        if (func_8C0DCBA0(twp, pwp)) break;
        if (func_8C0DB08C(twp, mwp, pwp)) break;
        if (func_8C0DC120(twp, mwp, pwp)) break;
        if (func_8C0DB28E(twp, mwp, pwp)) break;
        if (func_8C0DB3BA(twp, mwp, pwp)) break;
        if (func_8C0DCB22(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL) || pwp->spd.x != 0.0f) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
        }
        break;

    case 2:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (func_8C0DCC1A(twp, pwp)) break;
        if (func_8C0DB08C(twp, mwp, pwp)) break;
        if (pwp->spd.x > 1.19999996f) {
            if (func_8C0DCAE0(twp, mwp, pwp)) break;
        } else {
            if (func_8C0DCBA0(twp, pwp)) break;
            if (func_8C0DB28E(twp, mwp, pwp)) break;
            if (func_8C0DCB22(twp, mwp, pwp)) break;
            if (func_8C0DB3BA(twp, mwp, pwp)) break;
        }
        if (func_8C0DCA6C(twp, mwp, pwp)) break;
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && (CWP(twp)->flag & 8)
            && (AMY_HITINFO(twp).b05 & 0xF)) {
            switch (func_8C06CF60(twp, mwp)) {
            case 4:
                twp->mode = 10;
                pwp->mj.reqaction = 22;
            }
        }
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 9;
            pwp->mj.reqaction = 20;
            func_8C0940DA(18, 0, 0, 0);
            break;
        }
        break;

    case 3:
        twp->flag &= ~0x500;
        if (func_8C0DB71A(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0DC9BE(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0DB08C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0DCA6C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 4:
        if (func_8C0DB71A(twp, mwp, pwp)) {
            twp->flag &= ~0x500;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0DC9BE(twp, mwp, pwp)) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 1.19999996f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (pwp->spd.x < 1.84999993f) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 1.19999996f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 5:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 9;
                pwp->mj.reqaction = 20;
            } else if (func_8C0DCA6C(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 1;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0DA8DA(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C0DB230(twp, mwp, pwp)) break;
        if (func_8C06CEFE(twp, pwp)) {
            pwp->spd.y SH_ONLY(+= pwp->p.jmp_addit * 0.79999996f)
                PC_ONLY(= SA1_FMAC(pwp->p.jmp_addit, 0.79999996f, pwp->spd.y));
        }
        if (pwp->mj.reqaction == 14 && mwp->spd.y <= 0.0f) {
            pwp->mj.reqaction = 17;
        }
        break;

    case 6:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 8;
            pwp->mj.reqaction = 17;
        }
        break;

    case 7:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 8;
            pwp->mj.reqaction = 17;
        }
        break;

    case 8:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 9;
                pwp->mj.reqaction = 20;
            } else if (func_8C0DCA6C(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 1;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0DA8DA(twp, mwp, pwp);
            }
        }
        if (func_8C0DB230(twp, mwp, pwp)) break;
        break;

    case 9:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (func_8C0DB08C(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 20) {
            func_8C0DA6E0(twp, mwp, pwp);
        }
        break;

    case 10:
        if (func_8C0DB71A(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0DB08C(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0DC9BE(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0DCA6C(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && !(AMY_HITINFO(twp).attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                break;
            case 3:
                if (!(twp->flag & TWF_HOLD)) {
                    twp->mode = 2;
                    pwp->mj.reqaction = 9;
                    func_8C0945A0(15);
                }
                break;
            default:
                if (twp->flag & TWF_HOLD) {
                    twp->mode = 34;
                    pwp->mj.reqaction = 56;
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 9;
                }
                func_8C0945A0(15);
                break;
            }
            break;
        }
        func_8C0DC75E(twp, mwp, pwp);
        twp->mode = 2;
        pwp->mj.reqaction = 9;
        func_8C0945A0(15);
        break;

    case 11:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (twp->wtimer != 120 && (twp->flag & TWF_GROUND)) {
            if (func_8C0DCA6C(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 25;
                break;
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 2;
            func_8C0DA8DA(twp, mwp, pwp);
        }
        break;

    case 12:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (func_8C0DB08C(twp, mwp, pwp)) break;
        if (func_8C0DCA6C(twp, mwp, pwp)) break;
        if (pwp->spd.x >= 0.0f) {
            twp->mode = 2;
            func_8C0DA8DA(twp, mwp, pwp);
            break;
        }
        break;

    case 13:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0DBC16(twp, mwp, pwp)) break;
        } else {
            if (func_8C0DB71A(twp, mwp, pwp)) break;
        }
        break;

    case 14:
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 30 && pwp->mj.reqaction != 31) {
            func_8C0DA6E0(twp, mwp, pwp);
        }
        break;

    case 15:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (func_8C0DB380(twp, mwp, pwp)) break;
        break;

    case 16:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (func_8C0DB08C(twp, mwp, pwp)) break;
        if (func_8C0DB5E4(twp, mwp, pwp)) break;
        break;

    case 19:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction == 83) {
            pwp->free.f[6] = 0.0f;
        } else {
            pwp->free.f[6] = 1.0f;
        }
        if (pwp->mj.reqaction == 82 && pwp->mj.nframe >= 3.0f && pwp->mj.nframe < 4.0f) {
            task *tp;
            NJS_POINT3 v;
            NJS_POINT3 out;

            tp = CreateElementalTask(2, 6, func_8C0DD03C);
            if (tp != NULL) {
                v.x = 15.0f;
                v.y = v.z = 0.0f;
                njPushMatrix(D_8C67C640);
                twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
                twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
                -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
                njCalcVector(NULL, &v, &out);
                njPopMatrix(1);
                tp->twp->pos.x = twp->pos.x + out.x;
                tp->twp->pos.y = twp->pos.y + out.y;
                tp->twp->pos.z = twp->pos.z + out.z;
                tp->twp->ang.x = twp->ang.x;
                tp->twp->ang.z = twp->ang.z;
                tp->twp->wtimer = 10;
                tp->twp->btimer = 1;
                func_8C0DF2A0(&tp->twp->pos, twp->ang.x, twp->ang.z);
            }
            if (pwp->equipment & 0x80) {
                func_8C0940DA(0x2F1, 0, 0, 0);
            } else {
                func_8C0940DA(0x2F0, 0, 0, 0);
            }
            func_8C109EAC((Uint8)twp->counter.b[0], 3, 15, 0);
        }
        if (pwp->mj.reqaction == 0) {
            func_8C0DA6E0(twp, mwp, pwp);
            break;
        }
        if (pwp->mj.reqaction == 84) {
            pwp->free.b[0] = 0;
            twp->mode = 20;
            break;
        }
        if (!(pwp->equipment & 0x100) || !(PER(twp).on & D_8C19E38C)) {
            *(Sint16 *)&AmyActionList[82].next = 83;
        }
        if (pwp->mj.reqaction >= 83) {
            if (func_8C0DCB22(twp, mwp, pwp)) break;
        }
        break;

    case 20:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            /* nothing (SonicModeMachine form) */
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 18;
            break;
        }
        if (pwp->mj.reqaction != 87 && pwp->mj.reqaction != 88) {
            if (!((Sint32)PER(twp).on & (Sint32)D_8C19E38C)) {
                func_8C0DA6E0(twp, mwp, pwp);
                break;
            }
        }
        switch (pwp->mj.reqaction) {
        case 84:
            pwp->free.f[6] = 1.0f;
            break;
        case 85:
            {
                Float f;

                f = SA1_FMAC(0.017857142f, (Float)pwp->free.b[1], 1.0f);
                pwp->free.f[6] = f;
            }
            pwp->work.f = SA1_FMAC(0.06f, _builtin_fabsf((Float)pwp->free.b[3]), 0.099999996f);
            if (func_8C0DCE94(twp, pwp)) {
                if (pwp->free.b[3] > 0) {
                    pwp->work.f = -1.0f;
                } else {
                    pwp->work.f = 1.0f;
                }
                pwp->free.w[4] = 0;
                pwp->mj.reqaction = 86;
                func_8C0940DA(0x2F2, 0, 0, 0);
            }
            break;
        case 86:
            pwp->free.f[6] = 1.5f;
            if (++pwp->free.w[4] > 300.0f) {
                pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
                pwp->free.w[4] = 0;
                pwp->mj.reqaction = 87;
                func_8C0940DA(0x49C, 0, 0, 0);
                CreateElementalTask(2, 6, func_8C0DF90C);
            }
            if (!(pwp->free.w[4] & 7)) {
                task *tp;

                tp = CreateElementalTask(2, 6, func_8C0DD03C);
                if (tp != NULL) {
                    tp->twp->pos = twp->pos;
                    tp->twp->pos.y SH_ONLY(+= pwp->p.height * 0.5f)
                        PC_ONLY(= SA1_FMAC(pwp->p.height, 0.5f, tp->twp->pos.y));
                    tp->twp->ang = twp->ang;
                    tp->twp->wtimer = 4;
                    tp->twp->btimer = 0;
                }
                func_8C109EAC((Uint8)twp->counter.b[0], 1, 15, 0);
            }
            break;
        case 87:
        case 88:
            {
                Float f;
                Angle ang;

                pwp->free.f[6] = 0.0f;
                if (pwp->free.w[4] < 150.0f
                    && (((Sint32)PER(twp).press & (Sint32)D_8C19E390) || ((Sint32)PER(twp).press & (Sint32)D_8C19E38C))) {
                    pwp->free.w[4] += 18;
                }
                if (++pwp->free.w[4] > 300.0f) {
                    func_8C0DA6E0(twp, mwp, pwp);
                    break;
                }
                f = 1.0f - pwp->free.w[4] * 0.0033333332f;
                ang = pwp->free.w[4] << 9;
                if (pwp->spd.x > 0.16999999f) {
                    pwp->spd.x -= SA1_FMAC(0.5f, f, 0.5f) * pwp->spd.x * _builtin_fabsf(njSin(ang)) * 0.79999996f;
                    pwp->spd.z = SA1_FMAC(0.5f, f, 0.5f) * njCos(ang) * 0.29999999f;
                }
                if (_builtin_fabsf(pwp->spd.x) > 0.0f) {
                    pwp->mj.reqaction = 88;
                    break;
                }
                AmyActionList[87].racio = SA1_FMAC(0.5f, f, 0.29999999f);
                pwp->mj.reqaction = 87;
            }
            break;
        }
        break;

    case 21:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        switch (pwp->mj.reqaction) {
        case 89:
            {
                Float f;
                task *tp;
                NJS_POINT3 v;
                NJS_POINT3 out;

                pwp->free.f[6] = 1.0f;
                f = (Float)AmyActionList[89].actptr->motion->nbFrame;
                if (pwp->mj.nframe >= f - 2.0f) {
                    tp = CreateElementalTask(2, 6, func_8C0DD03C);
                    if (tp != NULL) {
                        v.x = 15.0f;
                        v.y = v.z = 0.0f;
                        njPushMatrix(D_8C67C640);
                        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
                        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
                        -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
                        njCalcVector(NULL, &v, &out);
                        njPopMatrix(1);
                        tp->twp->pos.x = twp->pos.x + out.x;
                        tp->twp->pos.y = twp->pos.y + out.y;
                        tp->twp->pos.z = twp->pos.z + out.z;
                        tp->twp->ang = twp->ang;
                        tp->twp->wtimer = 10;
                        tp->twp->btimer = 1;
                    }
                    if (pwp->equipment & 0x80) {
                        func_8C0940DA(0x2F1, 0, 0, 0);
                    } else {
                        func_8C0940DA(0x2F0, 0, 0, 0);
                    }
                    func_8C109EAC((Uint8)twp->counter.b[0], 6, 15, 0);
                    pwp->spd.y = pwp->p.jmp_y_spd * 1.9f;
                    break;
                }
            }
            break;
        case 90:
        case 91:
        case 92:
        case 93:
            pwp->free.f[6] = 0.0f;
            if (twp->flag & TWF_GROUND) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (func_8C0DCA6C(twp, mwp, pwp)) {
                    pwp->mj.reqaction = 1;
                } else {
                    twp->mode = 2;
                    func_8C0DA8DA(twp, mwp, pwp);
                }
                break;
            }
            if (func_8C0DB230(twp, mwp, pwp)) break;
            break;
        default:
            pwp->free.f[6] = 0.0f;
            pwp->free.f[7] = 0.0f;
            if (pwp->mj.reqaction != 94) {
                if (twp->flag & TWF_GROUND) {
                    twp->mode = 1;
                } else {
                    twp->mode = 5;
                    pwp->jumptimer = pwp->p.jump2_timer;
                    pwp->mj.reqaction = 18;
                }
            }
            break;
        }
        break;

    case 22:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction == 96) {
            if (twp->flag & TWF_GROUND) {
                twp->mode = 1;
            } else {
                twp->mode = 8;
            }
            break;
        }
        if (twp->flag & TWF_GROUND) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            pwp->mj.reqaction = 97;
            twp->mode = 1;
            break;
        }
        if (pwp->mj.reqaction == 95
            && pwp->mj.nframe < (Float)AmyActionList[95].actptr->motion->nbFrame - 2.0f) {
            pwp->free.f[6] = 1.0f;
            pwp->free.f[7] = 1.0f;
            break;
        }
        pwp->free.f[6] = 0.0f;
        pwp->free.f[7] = 0.0f;
        break;

    case 17:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction == 0) {
            func_8C0DA6E0(twp, mwp, pwp);
            pwp->free.b[4] = 1;
            break;
        }
        break;

    case 18:
        pwp->free.f[5] -= 1.0f;
        pwp->mj.nframe = pwp->free.f[5];
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (pwp->free.f[5] <= 1.0f) {
            NJS_POINT3 pos;

            pos = twp->pos;
            pos.x SH_ONLY(+= njCos(twp->ang.y) * 7.5f)
                PC_ONLY(= SA1_FMAC(njCos(twp->ang.y), 7.5f, pos.x));
            pos.z SH_ONLY(+= njSin(twp->ang.y) * 7.5f)
                PC_ONLY(= SA1_FMAC(njSin(twp->ang.y), 7.5f, pos.z));
            func_8C0DEFB6(&pos);
            func_8C0DA6E0(twp, mwp, pwp);
            break;
        }
        break;

    case 38:
        if (func_8C0DB71A(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0DB08C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (twp->flag & TWF_ONPATH) {
            Angle ang;

            if (GetAnalog(twp, &ang, NULL)) {
                if (DiffAngle(twp->ang.y, ang) < 0x4000) {
                    pwp->spd.x = 0.1875f;
                    twp->mode = 39;
                    break;
                }
            }
            break;
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 18;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 39:
        if (func_8C0DB71A(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0DB08C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (twp->flag & TWF_ONPATH) {
            /* nothing (SonicModeMachine form) */
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 18;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 40:
        if (func_8C0DB71A(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0DB08C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (twp->flag & TWF_ONPATH) {
            Angle ang;

            if (GetAnalog(twp, &ang, NULL)) {
                if (DiffAngle(0xC000 - camera_twp->ang.y, ang) < 0x4000) {
                    pwp->spd.x = 0.1875f;
                    break;
                } else {
                    pwp->spd.x = -0.1875f;
                    break;
                }
            }
            break;
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 18;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 41:
        if (func_8C0DB71A(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0DB08C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (twp->flag & TWF_ONPATH) {
            /* nothing (SonicModeMachine form) */
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 18;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 42:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
            mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
            if (func_8C05E6E8(twp)) {
                twp->mode = 43;
                pwp->mj.reqaction = 74;
                break;
            }
            twp->mode = 1;
            pwp->mj.reqaction = 2;
            break;
        }
        break;

    case 43:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (!func_8C05E6E8(twp)) {
            twp->mode = 1;
            pwp->mj.reqaction = 2;
            break;
        }
        if (func_8C06CEBE(twp, pwp) == 2) {
            func_8C05E788(twp);
            twp->mode = 42;
            pwp->mj.reqaction = 73;
        }
        break;

    case 44:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (B_8C754E7C[(Uint8)twp->counter.b[0]] & 0x306FE) {
            pwp->free.w[5] -= 20;
        }
        if (pwp->free.w[5] <= 0) {
            func_8C0DA6E0(twp, mwp, pwp);
            break;
        }
        break;

    case 23:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0DBC16(twp, mwp, pwp)) break;
        } else {
            if (func_8C0DB71A(twp, mwp, pwp)) break;
        }
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (func_8C0DC1E2(twp, mwp, pwp)) break;
        break;

    case 26:
        if (func_8C0DC662(twp, mwp, pwp)) break;
        if (func_8C0DB08C(twp, mwp, pwp)) break;
        break;

    case 25:
        if (func_8C0DBC16(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (((Sint32)PER(twp).on & (Sint32)D_8C19E3A0) && GetAnalog(twp, NULL, NULL)
            && pwp->htp->twp->id == 4) {
            twp->mode = 37;
            pwp->mj.reqaction = 58;
            break;
        }
        if (func_8C0DC2A0(twp, mwp, pwp)) break;
        if (func_8C0DB08C(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 27;
            pwp->mj.reqaction = 48;
        }
        break;

    case 27:
        if (func_8C0DBC16(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp) || func_8C0DB08C(twp, mwp, pwp) || func_8C0DC2A0(twp, mwp, pwp)
            || func_8C0DCA6C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 29;
            pwp->mj.reqaction = 45;
            func_8C0940DA(18, 0, 0, 0);
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 24:
    case 28:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0DBC16(twp, mwp, pwp)) break;
        } else {
            if (func_8C0DB71A(twp, mwp, pwp)) break;
        }
        if (func_8C0DC3C0(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 67 && pwp->mj.reqaction != 68 && pwp->mj.reqaction != 71) {
            func_8C0DA6E0(twp, mwp, pwp);
        }
        break;

    case 29:
        if (func_8C0DBC16(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (func_8C0DB08C(twp, mwp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 27;
            pwp->mj.reqaction = 46;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 45) {
            twp->mode = 25;
            pwp->mj.reqaction = 47;
        }
        break;

    case 30:
        if (func_8C0DBC16(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 29;
                pwp->mj.reqaction = 45;
                break;
            }
            if (func_8C0DCA6C(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 47;
                } else {
                    pwp->mj.reqaction = 66;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 27;
                func_8C0DA8DA(twp, mwp, pwp);
            }
            break;
        }
        if (func_8C0DC2A0(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 31:
        if (func_8C0DBC16(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 29;
                pwp->mj.reqaction = 45;
            } else if (func_8C0DCA6C(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 47;
                } else {
                    pwp->mj.reqaction = 66;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 27;
                func_8C0DA8DA(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C06CEFE(twp, pwp)) {
            pwp->spd.y SH_ONLY(+= pwp->p.jmp_addit * 0.39999998f)
                PC_ONLY(= SA1_FMAC(pwp->p.jmp_addit, 0.39999998f, pwp->spd.y));
        }
        if (func_8C0DC2A0(twp, mwp, pwp)) break;
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 30;
            pwp->mj.reqaction = 64;
        }
        break;

    case 32:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 30;
            pwp->mj.reqaction = 64;
        }
        break;

    case 33:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 30;
            pwp->mj.reqaction = 64;
        }
        break;

    case 34:
        if (func_8C0DB71A(twp, mwp, pwp)) break;
        if (func_8C0DB08C(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (!((Sint32)PER(twp).on & (Sint32)D_8C19E3A0)) {
            func_8C0DA6E0(twp, mwp, pwp);
            func_8C0DC75E(twp, mwp, pwp);
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && !(AMY_HITINFO(twp).attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                twp->mode = 10;
                pwp->mj.reqaction = 22;
                break;
            case 0:
                if (twp->flag & TWF_HOLD) {
                    twp->mode = 26;
                    pwp->mj.reqaction = 62;
                }
                break;
            default:
                if (twp->flag & TWF_HOLD) {
                    twp->mode = 34;
                    pwp->mj.reqaction = 56;
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 9;
                }
                break;
            }
            break;
        } else {
            func_8C0DC75E(twp, mwp, pwp);
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            break;
        }
        break;

    case 35:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0DBC16(twp, mwp, pwp)) break;
        } else {
            if (func_8C0DB71A(twp, mwp, pwp)) break;
        }
        if (twp->flag & TWF_GROUND) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 29;
                pwp->mj.reqaction = 45;
                break;
            }
            if (func_8C0DCA6C(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (twp->flag & TWF_HOLD) {
                    if (mwp->spd.y > -5.0f) {
                        pwp->mj.reqaction = 47;
                    } else {
                        pwp->mj.reqaction = 66;
                    }
                } else if (mwp->spd.y > -5.0f) {
                    func_8C0DA6E8(twp, mwp, pwp);
                } else {
                    pwp->mj.reqaction = 19;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (twp->flag & TWF_HOLD) {
                    twp->mode = 27;
                } else {
                    twp->mode = 2;
                }
                func_8C0DA8DA(twp, mwp, pwp);
            }
            break;
        }
        if (func_8C0DC3C0(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 69 && pwp->mj.reqaction != 70) {
            twp->mode = 8;
            pwp->mj.reqaction = 18;
        }
        break;

    case 36:
        if (func_8C0DBC16(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (func_8C0DB08C(twp, mwp, pwp)) break;
        if (!func_8C06CA30(twp, mwp, pwp)) {
            func_8C0DA6E0(twp, mwp, pwp);
            func_8C0DC75E(twp, mwp, pwp);
            break;
        }
        if (!GetAnalog(twp, NULL, NULL)) {
            twp->mode = 26;
            pwp->mj.reqaction = 62;
        }
        break;

    case 37:
        if (func_8C0DBC16(twp, mwp, pwp)) break;
        if (func_8C0DC9BE(twp, mwp, pwp)) break;
        if (func_8C0DB08C(twp, mwp, pwp)) break;
        if (pwp->htp == NULL || pwp->htp->twp->id != 4) {
            func_8C0DA6E0(twp, mwp, pwp);
            func_8C0DC75E(twp, mwp, pwp);
            break;
        }
        if (!GetAnalog(twp, NULL, NULL)) {
            twp->mode = 25;
            pwp->mj.reqaction = 60;
            break;
        }
        break;

    case 45:
        if (EWP(twp)->list4C == NULL) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0DA6E0(twp, mwp, pwp);
        }
        break;

    case 48:
        if ((twp->flag & TWF_REQUEST) && twp->smode == 28) {
            twp->mode = 6;
            pwp->mj.reqaction = 16;
            twp->flag &= ~0x500;
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 49:
        if (B_8C754C08[0].press & 4) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
        }
        break;
    }
}
#else
ASM_STUB(void AmyModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

#if defined(NON_MATCHING) && !defined(TARGET_PC)
/*
 * Translation-unit layout model, part 2 (SHC check only, never spliced: the
 * gap has no address in the image). In the original TU func_8C0DA106..
 * func_8C0DA662 (1498 bytes) lie between this function and func_8C0DA6E0,
 * and func_8C0DA6E8 (498 bytes) before func_8C0DA8DA, so SHC calls the
 * three with BSR only from cases 31, 34, 35, 37 and 45 (JSR from case
 * 24/28 at 8C0D98D2 and case 30 at 8C0D9A6E). Calibrated: 384 to 512 sink
 * calls give exactly the original BSR/JSR choice (fewer: cases 44 and
 * 24/28 also use BSR; 640 or more: case 31 uses JSR).
 */
#define TU_GAP4  __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink();
#define TU_GAP16 TU_GAP4 TU_GAP4 TU_GAP4 TU_GAP4
#define TU_GAP64 TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP16
void tu_gap_8C0DA106(void)
{
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64
}
#endif

ASM_STUB(void func_8C0DA6E0(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C0DA6E8(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C0DA8DA(taskwk *twp, motionwk *mwp, playerwk *pwp))
