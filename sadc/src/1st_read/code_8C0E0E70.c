#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0E0E70: BigModeMachine (chunk 8C0DFFA0-8C0E59C0, function 2/29).
 *
 * Big's action state machine, called once per frame by BigMain (8C0DFFA0)
 * with (twp, mwp, pwp): first the hurt / invincibility timer on twp->wtimer
 * and the damage check (func_8C0E40CE), then a switch on twp->mode (1..52,
 * no case 46) that decides the next mode and the requested motion
 * (pwp->mj.reqaction) from the input checks of the chunk (func_8C0E4432,
 * func_8C0E4CD0, ... return non-zero when they changed the mode themselves).
 * Same programmer and shape as SonicModeMachine (code_8C0A966C.c),
 * E102ModeMachine (code_8C0CD0A8.c) and AmyModeMachine (code_8C0D7960.c).
 * Big specials: the fishing modes 32-45 (rod cast 32/40, reel 33-36 and
 * 41-44 with the stick-angle reel actions 55-59 / 69-73, catch 37/45; the
 * fishing state is the flag word B_8C7A40E8 and pwp->free.l[0]).
 *
 * Code generation notes. The function is so large that SHC compiles it
 * without register allocation: twp/mwp/pwp live at @(20/16/12,R15) and the
 * block locals in @(0..11,R15) (blocks share the slots; each block
 * allocates from the top, first declared highest). The cases are in the
 * original source order (the order of their code), not in numeric order.
 * As in E102ModeMachine the statement structure decides the rest:
 *  - SHC cross-jumps identical tails made of a call and a jump to the end
 *    of the switch: an explicit "break;" after the call, or the jump over
 *    an else when the if is followed by "break;". A call that falls into a
 *    join (end of an if without else, of an else, of an else-if chain) is
 *    not merged. Hence the shared "func_8C0E65BE(); break;" of case 47
 *    (also reached from cases 11, 13, 31, 33, 36, 38), the shared
 *    "func_8C0945A0(15); break;" of case 26 (case 10), func_8C0E6446 /
 *    func_8C0E6500 of case 44 (cases 35, 36, 43) and func_8C0E3C20 of case
 *    23 (case 22), while the func_8C0E3DE0 calls of cases 4, 7, 8, 11, 22
 *    and 23 are written so that they fall into a join;
 *  - the choice among R1-R3 for the temporaries follows the statement
 *    structure: code after "if (c) { ...; break; }" continues from the
 *    register state of the then-block, code in an else from the state
 *    after the condition (cases 22, 23, 27, 35, 36, 43); a far break
 *    (MOV.L pool,Rn; JMP @Rn) at the end of a then-block keeps a NOP in its
 *    delay slot only when the then-block ends with its own "break;" (cases
 *    30, 33, 47 to 51);
 *  - the two "spd.y += jmp_addit * 0.8f" (cases 4 and 23) are FMACs; the
 *    SA1_FMAC spelling (a * b + c) loads spd.y first and changes the code,
 *    hence SH_ONLY/PC_ONLY as in AmyModeMachine.
 *
 * Status: instruction-identical to the original. tools/shcdiff.py
 * --flags=-define=NON_MATCHING matches 4448 of the 4449 instructions, with
 * every pool value and every branch target equal; the one item it reports
 * is the MOVA of the 52-entry switch jump table, whose ".DATA.W
 * Lcase-Lbase" entries it cannot evaluate. All 52 entries were checked to
 * reach the same instruction (by index) as the original table at
 * 8C0E0FB4, and the literal pools fall at the same offsets. The identity
 * needs the translation-unit models of this file (the end of BigMain
 * before this function, func_8C0E386E and the gaps up to func_8C0E416E
 * after it), compiled only for this SHC check. The splice (splice.py uses
 * the same comparison) is blocked by the jump-table MOVA, hence the
 * NON_MATCHING guard, as for E102ModeMachine.
 *
 * PC notes: pwp->free.l[0] holds Big's fishing bits (1, 2, 4, 8, set and
 * cleared by modes 32-45); no pointer is kept in free.
 */

/* Big's fishing state (rod / lure; written by the fishing code 8C0E6620-) */
extern task *B_8C7A40C8;        /* lure task (func_8C0EAD6A ignores R4) */
extern Sint32 B_8C7A40CC;       /* fish on the hook */
extern Uint16 B_8C7A40E8;       /* fishing flags: 1, 2, 4, 0x20, 0x400, 0x800 */
extern NJS_POINT3 D_8C15CA98;   /* catch offsets (cases 35, 36, 43, 44) */
extern NJS_POINT3 D_8C15CAA4;
extern NJS_POINT3 D_8C15CAB0;
extern NJS_POINT3 D_8C15CABC;

/* Big (same chunk, reached with JSR) */
Sint32 func_8C0E40CE(taskwk *twp, motionwk *mwp, playerwk *pwp);       /* damage check */
Sint32 func_8C0E428E(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E42F0(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E4380(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E4432(taskwk *twp, motionwk *mwp, playerwk *pwp);       /* input/request check */
Sint32 func_8C0E488C(taskwk *twp, motionwk *mwp, playerwk *pwp);       /* request check while holding */
Sint32 func_8C0E4C16(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E4CD0(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E4D40(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E4DA4(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E4E60(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E4F28(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E501C(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E5166(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E54E4(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0E55E2(taskwk *twp, motionwk *mwp, playerwk *pwp);         /* release the held object */
/* Big (chunks 8C0E63C0-8C0E6620, 8C0EAA80) */
void func_8C0E6446(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0E6500(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0E6586(void);
void func_8C0E65A8(void);
void func_8C0E65BE(taskwk *twp, motionwk *mwp, playerwk *pwp);         /* back to standing: mode 1, reqaction 0, waittimer 0 */
void func_8C0EAD6A(task *tp);

/* player common / engine; the call sites pass more arguments than the
 * matched definitions use (code_8C06CCA0.c, code_8C06CFCC.c, code_8C06CA30.c) */
extern Sint32 func_8C06CA30(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Bool func_8C06CE40(taskwk *twp, motionwk *mwp);
extern Bool func_8C06CEFE(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);
extern void func_8C086F5E(Sint8 flag);
extern void func_8C07BBC2(Uint8 pno);
extern void func_8C064EF6(NJS_POINT3 *pos, NJS_POINT3 *ofs);
extern void func_8C01AE60(void);
extern void func_8C04EFE0(void);

/* Big (same chunk, reached with BSR from the last cases): defined after the
 * TU layout model at the end of the file */
void func_8C0E3C20(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0E3DE0(taskwk *twp, motionwk *mwp, playerwk *pwp);         /* run action selector */
Sint32 func_8C0E416E(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* button state of the player's pad */
#define PER(twp) (B_8C754C08[(Uint8)(twp)->counter.b[0]])

/* hit shape of the collision partner */
#define BIG_HITINFO(twp) (CWP(twp)->hit_cwp->info[CWP(twp)->hit_num])

#if defined(NON_MATCHING) && !defined(TARGET_PC)
/*
 * Translation-unit layout model of the end of BigMain (8C0DFFA0; SHC check
 * only, never spliced: it has no address in the image). In the original TU
 * BigMain's last literals (its model swap and its per-frame calls) are
 * still pending when this function starts and are dumped in its first pool
 * (8C0E0F40-8C0E0F64), and this function starts at an address 16 mod 32
 * (the filler stores): both change SHC's branch-length estimates (BT + BRA
 * at 8C0E0EA6) and the offsets of every later pool.
 */
extern NJS_OBJECT D_8C52AD1C[];
extern NJS_MODEL D_8C52ACF4;
extern NJS_OBJECT D_8C52A5E8[];
void func_8C0E38AA(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0E3A40(task *tp);
void func_8C0E5698(taskwk *twp, motionwk *mwp, playerwk *pwp);
void tu_model_BigMain(task *tp, taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    twp->ang.x = twp->ang.y;
    twp->ang.z = twp->ang.y;
    twp->pos.x = twp->scl.x;
    twp->pos.y = 0.0f;
    D_8C52AD1C[0].model = &D_8C52ACF4;
    D_8C52AD1C[0].child = D_8C52A5E8;
    PJoinVertexes(twp, mwp, pwp);
    func_8C0E38AA(twp, mwp, pwp);
    func_8C0E3A40(tp);
    if (pwp->htp != NULL) {
        func_8C0E5698(twp, mwp, pwp);
    }
    func_8C0925A0(twp, &pwp->shadow);
    if (pwp->mj.action != 48) {
        func_8C02F63C(twp);
    }
    func_8C06C6F8(twp, mwp, pwp);
}
#endif

#ifdef NON_MATCHING
void BigModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp != NULL) {
        if (pwp->item & 0x8000) {
            twp->flag |= 0x8000;
            twp->wtimer = 0;
        } else if (twp->wtimer == 0) {
            if (func_8C0E40CE(twp, mwp, pwp)) {
                twp->flag |= 0x8000;
                if (twp->flag & TWF_HOLD) {
                    func_8C0E55E2(twp, mwp, pwp);
                }
                twp->flag &= ~0x3100;
                if (B_8C7A40E8 & 1) {
                    B_8C7A40E8 |= 4;
                }
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
    }

    switch (twp->mode) {
    case 1:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (func_8C0E4CD0(twp, mwp, pwp)) break;
        if (func_8C0E4D40(twp, mwp, pwp)) break;
        if (func_8C0E416E(twp, mwp, pwp)) break;
        if (func_8C0E4E60(twp, mwp, pwp)) break;
        if (func_8C0E42F0(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 2;
            pwp->mj.reqaction = 7;
            break;
        } else if (pwp->cstsp->tnorm.y < 0.77f && njScalor(&pwp->spd) > pwp->p.slide_speed) {
            twp->mode = 13;
            pwp->mj.reqaction = 25;
            break;
        }
        break;

    case 2:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (func_8C0E4CD0(twp, mwp, pwp) || func_8C0E4D40(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E416E(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E42F0(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E4DA4(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && !(BIG_HITINFO(twp).attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                twp->mode = 10;
                pwp->mj.reqaction = 17;
                twp->flag &= ~TWF_ONPATH;
            }
        }
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 9;
            pwp->mj.reqaction = 16;
            func_8C0940DA(18, 0, 0, 0);
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 3:
        if (func_8C0E4432(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E4D40(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E416E(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 2;
            pwp->mj.reqaction = 7;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E4DA4(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 4:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (func_8C0E4CD0(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            func_8C0940DA(33, 0, 0, 0);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 9;
                pwp->mj.reqaction = 16;
            } else if (func_8C0E4DA4(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 6;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0E3DE0(twp, mwp, pwp);
            }
        } else {
            if (mwp->spd.y <= 0.0f) {
                pwp->mj.reqaction = 14;
            }
            if (func_8C06CEFE(twp, pwp)) {
                pwp->spd.y SH_ONLY(+= pwp->p.jmp_addit * 0.79999997f)
                    PC_ONLY(= SA1_FMAC(pwp->p.jmp_addit, 0.79999997f, pwp->spd.y));
            }
        }
        break;

    case 5:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (func_8C0E4CD0(twp, mwp, pwp)) break;
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 8;
            pwp->mj.reqaction = 13;
        }
        break;

    case 6:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (func_8C0E4CD0(twp, mwp, pwp)) break;
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 8;
            pwp->mj.reqaction = 13;
        }
        break;

    case 7:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (func_8C0E4CD0(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            func_8C0940DA(33, 0, 0, 0);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 9;
                pwp->mj.reqaction = 16;
                break;
            }
            if (func_8C0E4DA4(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 0;
                break;
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 2;
            func_8C0E3DE0(twp, mwp, pwp);
        }
        break;

    case 8:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (func_8C0E4CD0(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            func_8C0940DA(33, 0, 0, 0);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 9;
                pwp->mj.reqaction = 16;
                break;
            }
            if (func_8C0E4DA4(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 6;
                break;
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 2;
            func_8C0E3DE0(twp, mwp, pwp);
        }
        break;

    case 9:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (func_8C0E4CD0(twp, mwp, pwp)) break;
        if (func_8C0E4D40(twp, mwp, pwp)) break;
        if (func_8C0E416E(twp, mwp, pwp)) break;
        if (func_8C0E42F0(twp, mwp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 2;
            pwp->mj.reqaction = 24;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 16) {
            func_8C0E65BE(twp, mwp, pwp);
        }
        break;

    case 10:
        if (func_8C0E4432(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0E4CD0(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0E416E(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0E4D40(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0E4DA4(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && !(BIG_HITINFO(twp).attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                break;
            case 3:
                if (!(twp->flag & TWF_HOLD)) {
                    twp->mode = 2;
                    pwp->mj.reqaction = 7;
                    func_8C0945A0(15);
                }
                break;
            default:
                if (twp->flag & TWF_HOLD) {
                    twp->mode = 26;
                    pwp->mj.reqaction = 33;
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 7;
                }
                func_8C0945A0(15);
                break;
            }
            break;
        } else {
            func_8C0E55E2(twp, mwp, pwp);
            twp->mode = 2;
            pwp->mj.reqaction = 7;
            func_8C0945A0(15);
            break;
        }
        break;
    case 11:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 19 && pwp->mj.reqaction != 20) {
            if (func_8C0E4DA4(twp, mwp, pwp)) {
                func_8C0E65BE(twp, mwp, pwp);
                break;
            } else {
                twp->mode = 2;
                func_8C0E3DE0(twp, mwp, pwp);
            }
        } else if (twp->wtimer != 120 && (twp->flag & TWF_GROUND)) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            pwp->mj.reqaction = 20;
        }
        break;

    case 12:
        if (func_8C0E4CD0(twp, mwp, pwp)) break;
        if (func_8C0E4D40(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 22 && pwp->mj.reqaction != 23) {
            func_8C0E65BE(twp, mwp, pwp);
        }
        break;

    case 13:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (func_8C0E4CD0(twp, mwp, pwp)) break;
        if (func_8C0E4D40(twp, mwp, pwp)) break;
        if (func_8C0E416E(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 2;
            pwp->mj.reqaction = 7;
            break;
        }
        if (njScalor(&mwp->spd) < pwp->p.slide_speed || pwp->cstsp->tnorm.y > 0.97999997f) {
            func_8C0E65BE(twp, mwp, pwp);
            break;
        }
        break;

    case 31:
        if (func_8C0E4432(twp, mwp, pwp)) {
            twp->flag &= ~0x400;
            break;
        }
        if (pwp->mj.reqaction != 49) {
            twp->flag &= ~0x400;
            func_8C0E65BE(twp, mwp, pwp);
            break;
        }
        break;

    case 14:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0E488C(twp, mwp, pwp)) break;
        } else {
            if (func_8C0E4432(twp, mwp, pwp)) break;
        }
        break;

    case 29:
        if (func_8C0E4432(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E416E(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (twp->flag & TWF_ONPATH) {
            Angle ang;

            if (GetAnalog(twp, &ang, NULL)) {
                if (DiffAngle(0xC000 - camera_twp->ang.y, ang) < 0x4000) {
                    pwp->spd.x = 0.75f;
                    break;
                } else {
                    pwp->spd.x = -0.75f;
                    break;
                }
            }
            break;
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 14;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 30:
        if (func_8C0E4432(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E416E(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (twp->flag & TWF_ONPATH) {
            /* nothing (SonicModeMachine form) */
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 14;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 32:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if ((Sint32)PER(twp).release & (Sint32)D_8C19E38C) {
            pwp->free.l[0] |= 8;
        }
        if (pwp->mj.flag & 1) {
            if (pwp->free.l[0] & 8) {
                pwp->mj.reqaction = 54;
                twp->mode = 34;
                twp->flag |= 0x400;
            } else {
                pwp->free.l[0] |= 1;
                twp->mode = 33;
                func_8C0E6586();
            }
            pwp->free.l[0] &= ~8;
        }
        break;

    case 33:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (!func_8C0E428E(twp, mwp, pwp)) {
            func_8C0E65A8();
            func_8C0E65BE(twp, mwp, pwp);
            break;
        }
        if ((Sint32)PER(twp).release & (Sint32)D_8C19E38C) {
            pwp->mj.reqaction = 53;
            twp->mode = 35;
            pwp->free.l[0] |= 2;
            pwp->free.l[0] &= ~4;
            break;
        }
        break;

    case 34:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 54) {
            twp->mode = 1;
            twp->flag &= ~0x400;
            pwp->waittimer = 0;
        }
        break;

    case 35:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (!func_8C0E428E(twp, mwp, pwp)) {
            pwp->free.l[0] |= 4;
            if (B_8C7A40CC && (B_8C7A40E8 & 1)) {
                B_8C7A40E8 |= 4;
            }
        }
        if (pwp->free.l[0] & 4) {
            if (B_8C7A40E8 & 2) {
                if (B_8C7A40E8 & 1) {
                    NJS_POINT3 ofs;

                    ofs = D_8C15CA98;
                    func_8C064EF6(&twp->pos, &ofs);
                    twp->mode = 37;
                    twp->btimer = 120;
                    func_8C0E6446(twp, mwp, pwp);
                    break;
                }
            }
            func_8C0E65A8();
            func_8C0E65BE(twp, mwp, pwp);
            pwp->free.l[0] &= ~4;
            break;
        }
        if (B_8C7A40E8 & 0x800) {
            func_8C0E6500(twp, mwp, pwp);
            break;
        }
        if (pwp->mj.nframe < (Float)pwp->mj.plactptr->actptr->motion->nbFrame - 3.0f) break;
        {
            Sint16 act;
            Angle ang;

            if (GetAnalog(twp, &ang, NULL)) {
                if (camera_twp != NULL) {
                    ang = (Uint16)(ang + camera_twp->ang.y + 0x4000);
                } else {
                    ang = 0;
                }
                if (ang > 0x4000) {
                    if (ang <= 0x5800) {
                        act = 58;
                    } else if (ang <= 0x7000) {
                        act = 56;
                    } else if (ang <= 0x9000) {
                        act = 55;
                    } else if (ang <= 0xA800) {
                        act = 57;
                    } else if (ang <= 0xC000) {
                        act = 59;
                    } else {
                        break;
                    }
                    twp->mode = 36;
                    pwp->mj.reqaction = act;
                }
            }
        }
        break;

    case 36:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (pwp->free.l[0] & 4) {
            if (B_8C7A40E8 & 2) {
                if (B_8C7A40E8 & 1) {
                    NJS_POINT3 ofs;

                    ofs = D_8C15CAA4;
                    func_8C064EF6(&twp->pos, &ofs);
                    twp->mode = 37;
                    twp->btimer = 120;
                    func_8C0E6446(twp, mwp, pwp);
                    break;
                }
            }
            func_8C0E65A8();
            pwp->free.l[0] &= ~4;
            func_8C0E65BE(twp, mwp, pwp);
            break;
        }
        if (B_8C7A40E8 & 0x800) {
            func_8C0E6500(twp, mwp, pwp);
            break;
        }
        pwp->mj.start_frame = (Float)pwp->mj.plactptr[51].actptr->motion->nbFrame - 1.0f;
        if (pwp->mj.nframe >= 10.0f && pwp->mj.nframe <= 30.0f) {
            B_8C7A40E8 |= 0x20;
        } else {
            B_8C7A40E8 &= 0xFFDF;
        }
        if (pwp->mj.reqaction < 55 || pwp->mj.reqaction > 59) {
            twp->mode = 35;
            B_8C7A40E8 &= 0xFFDF;
        }
        break;

    case 37:
        if (B_8C7A40C8 != NULL) {
            func_8C0EAD6A(B_8C7A40C8);
        }
        if (((Sint32)PER(twp).press & (Sint32)D_8C19E38C) || --twp->btimer <= 0) {
            twp->mode = 1;
            B_8C7A40CC = 0;
            func_8C01AE60();
            if (B_8C7A40E8 & 0x400) {
                func_8C04EFE0();
            } else {
                pwp->mj.reqaction = 0;
                pwp->free.l[0] &= ~4;
            }
            B_8C7A40E8 = 0;
        }
        pwp->waittimer = 0;
        if (func_8C0E4432(twp, mwp, pwp)) break;
        break;

    case 38:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (pwp->mj.flag & 1) {
            func_8C0E65BE(twp, mwp, pwp);
            break;
        }
        break;

    case 39:
        if (pwp->mj.reqaction != 63) {
            func_8C07BBC2(twp->counter.b[0]);
        }
        break;

    case 40:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if ((Sint32)PER(twp).release & (Sint32)D_8C19E38C) {
            pwp->free.l[0] |= 8;
        }
        if (pwp->mj.flag & 1) {
            if (pwp->free.l[0] & 8) {
                pwp->mj.reqaction = 68;
                twp->mode = 42;
                twp->flag |= 0x400;
            } else {
                pwp->free.l[0] |= 1;
                twp->mode = 41;
                func_8C0E6586();
            }
            pwp->free.l[0] &= ~8;
        }
        break;

    case 41:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if ((Sint32)PER(twp).release & (Sint32)D_8C19E38C) {
            pwp->mj.reqaction = 67;
            twp->mode = 43;
            pwp->free.l[0] |= 2;
            pwp->free.l[0] &= ~4;
        }
        break;

    case 42:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 67) {
            twp->mode = 1;
            twp->flag &= ~0x400;
            pwp->waittimer = 0;
        }
        break;
    case 43:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (pwp->free.l[0] & 4) {
            if (B_8C7A40E8 & 2) {
                if (B_8C7A40E8 & 1) {
                    NJS_POINT3 ofs;

                    ofs = D_8C15CAB0;
                    func_8C064EF6(&twp->pos, &ofs);
                    twp->mode = 45;
                    twp->btimer = 120;
                    func_8C0E6446(twp, mwp, pwp);
                    break;
                }
            }
            func_8C0E65A8();
            twp->mode = 47;
            pwp->mj.reqaction = 92;
            pwp->free.l[0] &= ~4;
            break;
        }
        if (B_8C7A40E8 & 0x800) {
            func_8C0E6500(twp, mwp, pwp);
            break;
        }
        if (pwp->mj.nframe < (Float)pwp->mj.plactptr->actptr->motion->nbFrame - 3.0f) break;
        {
            Sint16 act;
            Angle ang;

            if (GetAnalog(twp, &ang, NULL)) {
                if (camera_twp != NULL) {
                    ang = (Uint16)(ang + camera_twp->ang.y + 0x4000);
                } else {
                    ang = 0;
                }
                if (ang > 0x4000) {
                    if (ang <= 0x5800) {
                        act = 72;
                    } else if (ang <= 0x7000) {
                        act = 70;
                    } else if (ang <= 0x9000) {
                        act = 69;
                    } else if (ang <= 0xA800) {
                        act = 71;
                    } else if (ang <= 0xC000) {
                        act = 73;
                    } else {
                        break;
                    }
                    twp->mode = 44;
                    pwp->mj.reqaction = act;
                }
            }
        }
        break;

    case 44:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (pwp->free.l[0] & 4) {
            if (B_8C7A40E8 & 2) {
                if (B_8C7A40E8 & 1) {
                    NJS_POINT3 ofs;

                    ofs = D_8C15CABC;
                    func_8C064EF6(&twp->pos, &ofs);
                    twp->mode = 45;
                    twp->btimer = 120;
                    func_8C0E6446(twp, mwp, pwp);
                    break;
                }
            }
            func_8C0E65A8();
            twp->mode = 47;
            pwp->mj.reqaction = 92;
            pwp->free.l[0] &= ~4;
            break;
        }
        if (B_8C7A40E8 & 0x800) {
            func_8C0E6500(twp, mwp, pwp);
            break;
        }
        pwp->mj.start_frame = (Float)pwp->mj.plactptr[68].actptr->motion->nbFrame - 1.0f;
        if (pwp->mj.nframe >= 10.0f && pwp->mj.nframe <= 20.0f) {
            B_8C7A40E8 |= 0x20;
        } else {
            B_8C7A40E8 &= 0xFFDF;
        }
        if (pwp->mj.reqaction < 69 || pwp->mj.reqaction > 73) {
            twp->mode = 43;
            B_8C7A40E8 &= 0xFFDF;
        }
        break;

    case 45:
        if (B_8C7A40C8 != NULL) {
            func_8C0EAD6A(B_8C7A40C8);
        }
        if (((Sint32)PER(twp).press & (Sint32)D_8C19E38C) || --twp->btimer <= 0) {
            twp->mode = 47;
            B_8C7A40CC = 0;
            func_8C01AE60();
            if (B_8C7A40E8 & 0x400) {
                func_8C04EFE0();
            } else {
                pwp->mj.reqaction = 92;
                pwp->free.l[0] &= ~4;
            }
            B_8C7A40E8 = 0;
        }
        if (func_8C0E4432(twp, mwp, pwp)) break;
        break;

    case 47:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (!func_8C0E4C16(twp, mwp, pwp)) {
            func_8C0E65BE(twp, mwp, pwp);
            break;
        }
        if (func_8C0E4380(twp, mwp, pwp)) break;
        if (CWP(twp)->hit_cwp != NULL && CWP(twp)->hit_cwp->mytask->twp->id == 13
            && func_8C0E416E(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 48;
            pwp->mj.reqaction = 86;
            break;
        }
        if ((Sint32)PER(twp).on & (Sint32)D_8C19E390) {
            twp->mode = 49;
            pwp->mj.reqaction = 88;
            break;
        }
        break;

    case 48:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (!func_8C0E4C16(twp, mwp, pwp)) {
            twp->mode = 2;
            pwp->mj.reqaction = 7;
            break;
        }
        if (func_8C0E4380(twp, mwp, pwp)) break;
        if (CWP(twp)->hit_cwp != NULL && CWP(twp)->hit_cwp->mytask->twp->id == 13
            && func_8C0E416E(twp, mwp, pwp)) break;
        if (!GetAnalog(twp, NULL, NULL)) {
            twp->mode = 47;
            pwp->mj.reqaction = 90;
            break;
        }
        if ((Sint32)PER(twp).on & (Sint32)D_8C19E390) {
            twp->mode = 49;
            pwp->mj.reqaction = 88;
            break;
        }
        break;

    case 49:
        if (func_8C0E4432(twp, mwp, pwp)) break;
        if (func_8C0E4C16(twp, mwp, pwp)) {
            twp->mode = 47;
            pwp->mj.reqaction = 90;
            break;
        }
        if (twp->flag & TWF_GROUND) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 9;
                pwp->mj.reqaction = 16;
            } else if (func_8C0E4DA4(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 6;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0E3DE0(twp, mwp, pwp);
            }
        }
        if (!((Sint32)PER(twp).on & (Sint32)D_8C19E390)) {
            twp->mode = 47;
            pwp->mj.reqaction = 90;
            break;
        }
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 48;
            pwp->mj.reqaction = 86;
            break;
        }
        break;

    case 15:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0E488C(twp, mwp, pwp)) break;
        } else {
            if (func_8C0E4432(twp, mwp, pwp)) break;
        }
        if (func_8C0E4D40(twp, mwp, pwp)) break;
        if (func_8C0E4F28(twp, mwp, pwp)) break;
        break;

    case 18:
        if (func_8C0E54E4(twp, mwp, pwp)) break;
        if (func_8C0E416E(twp, mwp, pwp)) break;
        break;

    case 17:
        if (func_8C0E488C(twp, mwp, pwp)) break;
        if (func_8C0E4D40(twp, mwp, pwp)) break;
        if (((Sint32)PER(twp).on & (Sint32)D_8C19E3A0) && GetAnalog(twp, NULL, NULL)
            && pwp->htp->twp->id == 4) {
            twp->mode = 50;
            pwp->mj.reqaction = 93;
            break;
        }
        if (func_8C0E501C(twp, mwp, pwp)) break;
        if (func_8C0E416E(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 19;
            pwp->mj.reqaction = 31;
        }
        break;

    case 19:
        if (func_8C0E488C(twp, mwp, pwp)) break;
        if (func_8C0E4D40(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E416E(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E501C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E4DA4(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0E501C(twp, mwp, pwp)) break;
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 21;
            pwp->mj.reqaction = 28;
            func_8C0940DA(18, 0, 0, 0);
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 16:
    case 20:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0E488C(twp, mwp, pwp)) break;
        } else {
            if (func_8C0E4432(twp, mwp, pwp)) break;
        }
        if (func_8C0E5166(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 41 && pwp->mj.reqaction != 42 && pwp->mj.reqaction != 45
            && pwp->mj.reqaction != 80) {
            func_8C0E65BE(twp, mwp, pwp);
        }
        break;

    case 21:
        if (func_8C0E488C(twp, mwp, pwp)) break;
        if (func_8C0E4D40(twp, mwp, pwp)) break;
        if (func_8C0E416E(twp, mwp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 19;
            pwp->mj.reqaction = 29;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 28) {
            twp->mode = 17;
            func_8C0E3C20(twp, mwp, pwp);
        }
        break;
    case 22:
        if (func_8C0E488C(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            func_8C0940DA(33, 0, 0, 0);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 21;
                pwp->mj.reqaction = 28;
            } else {
                if (func_8C0E4DA4(twp, mwp, pwp)) {
                    twp->ang.x = mwp->ang_aim.x;
                    twp->ang.z = mwp->ang_aim.z;
                    if (mwp->spd.y > -5.0f) {
                        func_8C0E3C20(twp, mwp, pwp);
                        break;
                    }
                    pwp->mj.reqaction = 40;
                    break;
                }
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 19;
                func_8C0E3DE0(twp, mwp, pwp);
            }
        } else if (func_8C0E501C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 23:
        if (func_8C0E488C(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            func_8C0940DA(33, 0, 0, 0);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 21;
                pwp->mj.reqaction = 28;
            } else {
                if (func_8C0E4DA4(twp, mwp, pwp)) {
                    twp->ang.x = mwp->ang_aim.x;
                    twp->ang.z = mwp->ang_aim.z;
                    if (mwp->spd.y > -5.0f) {
                        func_8C0E3C20(twp, mwp, pwp);
                        break;
                    }
                    pwp->mj.reqaction = 40;
                    break;
                }
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 19;
                func_8C0E3DE0(twp, mwp, pwp);
            }
        } else {
            if (func_8C06CEFE(twp, pwp)) {
                pwp->spd.y SH_ONLY(+= pwp->p.jmp_addit * 0.79999997f)
                    PC_ONLY(= SA1_FMAC(pwp->p.jmp_addit, 0.79999997f, pwp->spd.y));
            }
            if (func_8C0E501C(twp, mwp, pwp)) break;
            if (mwp->spd.y <= 0.0f) {
                twp->mode = 22;
                pwp->mj.reqaction = 38;
            }
        }
        break;

    case 24:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 22;
            pwp->mj.reqaction = 38;
        }
        break;

    case 25:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 22;
            pwp->mj.reqaction = 38;
        }
        break;

    case 26:
        if (func_8C0E4432(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0E416E(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0E4D40(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (!((Sint32)PER(twp).on & (Sint32)D_8C19E3A0)) {
            func_8C0E65BE(twp, mwp, pwp);
            func_8C0E55E2(twp, mwp, pwp);
            func_8C0945A0(15);
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && !(BIG_HITINFO(twp).attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                twp->mode = 10;
                pwp->mj.reqaction = 17;
                func_8C0945A0(15);
                break;
            case 0:
                if (twp->flag & TWF_HOLD) {
                    twp->mode = 18;
                    pwp->mj.reqaction = 36;
                    func_8C0945A0(15);
                }
                break;
            default:
                if (twp->flag & TWF_HOLD) {
                    twp->mode = 26;
                    pwp->mj.reqaction = 33;
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 7;
                }
                func_8C0945A0(15);
                break;
            }
            break;
        } else {
            func_8C0E55E2(twp, mwp, pwp);
            twp->mode = 2;
            pwp->mj.reqaction = 7;
            func_8C0945A0(15);
            break;
        }
        break;

    case 27:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0E488C(twp, mwp, pwp)) break;
        } else {
            if (func_8C0E4432(twp, mwp, pwp)) break;
        }
        if (twp->flag & TWF_GROUND) {
            func_8C0940DA(33, 0, 0, 0);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 21;
                pwp->mj.reqaction = 28;
                break;
            }
            if (func_8C0E4DA4(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (twp->flag & TWF_HOLD) {
                    if (mwp->spd.y > -5.0f) {
                        func_8C0E3C20(twp, mwp, pwp);
                    } else {
                        pwp->mj.reqaction = 40;
                    }
                } else {
                    if (mwp->spd.y > -5.0f) {
                        pwp->mj.reqaction = 0;
                    } else {
                        pwp->mj.reqaction = 6;
                    }
                }
                break;
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            if (twp->flag & TWF_HOLD) {
                twp->mode = 19;
            } else {
                twp->mode = 2;
            }
            func_8C0E3DE0(twp, mwp, pwp);
            break;
        }
        if (func_8C0E5166(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 43 && pwp->mj.reqaction != 44) {
            twp->mode = 8;
            pwp->mj.reqaction = 14;
        }
        break;

    case 28:
        if (func_8C0E488C(twp, mwp, pwp)) break;
        if (func_8C0E4D40(twp, mwp, pwp)) break;
        if (func_8C0E416E(twp, mwp, pwp)) break;
        if (!func_8C06CA30(twp, mwp, pwp)) {
            func_8C0E65BE(twp, mwp, pwp);
            func_8C0E55E2(twp, mwp, pwp);
            break;
        }
        if (!GetAnalog(twp, NULL, NULL)) {
            twp->mode = 18;
            pwp->mj.reqaction = 36;
        }
        break;

    case 50:
        if (func_8C0E488C(twp, mwp, pwp)) break;
        if (func_8C0E4D40(twp, mwp, pwp)) break;
        if (func_8C0E416E(twp, mwp, pwp)) break;
        if (pwp->htp == NULL || pwp->htp->twp->id != 4) {
            func_8C0E65BE(twp, mwp, pwp);
            func_8C0E55E2(twp, mwp, pwp);
            break;
        }
        if (!GetAnalog(twp, NULL, NULL)) {
            twp->mode = 17;
            pwp->mj.reqaction = 95;
            break;
        }
        break;

    case 51:
        if ((twp->flag & TWF_REQUEST) && twp->smode == 28) {
            twp->mode = 5;
            pwp->mj.reqaction = 12;
            twp->flag &= ~0x400;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 52:
        if (EWP(twp)->list4C == NULL) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0E65BE(twp, mwp, pwp);
        }
        break;
    }
}
#else
ASM_STUB(void BigModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

#if defined(NON_MATCHING) && !defined(TARGET_PC)
/*
 * Translation-unit layout model of func_8C0E386E (Big's destructor, SHC
 * check only): with it this function's last literal (func_8C0E65BE, case
 * 52) is not dumped after its RTS but deferred, as in the original
 * (8C0E39E0), which keeps the far break of case 48 (8C0E2B00) a MOV.L/JMP.
 */
extern NJS_TEXLIST texlist_BIG;

void tu_model_8C0E386E(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    func_8C06C5CA((Uint8)twp->counter.b[0]);
    func_8C0945D0(twp);
    if ((GetStageNumber() & 0xFF00) == 0x1300) {
        func_8C6088BC(&texlist_BIG);
    } else {
        func_8C098FFC(7);
    }
}
#endif

#if defined(NON_MATCHING) && !defined(TARGET_PC)
/*
 * Translation-unit layout model (SHC check only, never spliced: the gaps
 * have no address in the image). In the original TU func_8C0E386E,
 * func_8C0E38AA and func_8C0E3A40 (946 bytes) lie between this function and
 * func_8C0E3C20 (448 bytes), then func_8C0E3DE0 (224 bytes), func_8C0E3EC0
 * and func_8C0E40CE (686 bytes) come before func_8C0E416E, so SHC calls
 * func_8C0E3C20, func_8C0E3DE0 and func_8C0E416E with BSR only from the
 * last cases and with JSR from the earlier ones. The gaps below put the
 * three functions within 0x40 bytes of their original offsets.
 */
#define TU_GAP1  __asm_stub_sink();
#define TU_GAP4  TU_GAP1 TU_GAP1 TU_GAP1 TU_GAP1
#define TU_GAP16 TU_GAP4 TU_GAP4 TU_GAP4 TU_GAP4
#define TU_GAP64 TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP16
void tu_gap_8C0E386E(void)
{
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP4 TU_GAP1 TU_GAP1 TU_GAP1
}
#endif

ASM_STUB(void func_8C0E3C20(taskwk *twp, motionwk *mwp, playerwk *pwp))

#if defined(NON_MATCHING) && !defined(TARGET_PC)
void tu_gap_8C0E3C20(void)
{
    TU_GAP64 TU_GAP16 TU_GAP4 TU_GAP4 TU_GAP4 TU_GAP1 TU_GAP1
}
#endif

ASM_STUB(void func_8C0E3DE0(taskwk *twp, motionwk *mwp, playerwk *pwp))

#if defined(NON_MATCHING) && !defined(TARGET_PC)
void tu_gap_8C0E3DE0(void)
{
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP4 TU_GAP4
}
#endif

ASM_STUB(Sint32 func_8C0E416E(taskwk *twp, motionwk *mwp, playerwk *pwp))