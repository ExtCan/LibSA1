#include "common.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0CD0A8: E102ModeMachine (chunk 8C0CBE00-8C0D2080, function 3/28).
 *
 * E-102 Gamma's action state machine, called once per frame by E102Main
 * (8C0CBE00) with (tp, mwp, pwp): first the hurt / invincibility timer on
 * twp->wtimer and the damage check (func_8C0D04A0), then a switch on
 * twp->mode (1..55) that decides the next mode and the requested animation
 * (pwp->mj.reqaction) from the input checks of the chunk (func_8C0D079A,
 * func_8C0D0B16, ... return non-zero when they changed the mode themselves).
 * Same programmer and shape as SonicModeMachine (code_8C0A966C.c).
 *
 * Code generation notes. The function is so large that SHC compiles it
 * without register allocation: tp/mwp/pwp live at @(20/16/12,R15), twp and
 * the E-102 work at @(8/4,R15) and every block local at @(0,R15); only the
 * "register" local of case 29 gets R14. The cases are in the original
 * source order (the order of their code), not in numeric order. Identical
 * tails ending in an explicit break are cross-jumped (lbl_8C0CF236,
 * lbl_8C0CF4FA, lbl_8C0CEDEE, 8C0CF834). What is left is SHC's choice among
 * R1-R3 for the expression temporaries and whether a far "break" (MOV.L
 * pool,Rn; JMP @Rn) gets its delay slot filled. Both follow the statement
 * structure, not just the expression:
 *  - code after "if (c) { ...; break; }" continues from the register state
 *    of the then-block, code in the else of such an if from the state after
 *    the condition. Hence the "} else if" in case 1, the "break;" closing
 *    the then-blocks of cases 10, 26 (last else), 33, 34, 38 and 53, and the
 *    SonicModeMachine forms "if (ONPATH) { ... } else { ... }" (cases 47,
 *    48) and "switch (func_8C06CFCC(twp, mwp)) { case 4: ... }" (case 2);
 *  - the button test is "(Sint32)PER.on & (Sint32)D_8C19E3A0" (cases 17, 26),
 *    as in SonicModeMachine;
 *  - the six "spd.y += jmp_addit * k" are FMACs; the SA1_FMAC spelling
 *    (a * b + c) loads spd.y later and changes the code, hence SH_ONLY/PC_ONLY.
 *
 * Status: instruction-identical to the original (re-verified 2026-10-12).
 * tools/shcdiff.py --flags=-define=NON_MATCHING matches 4444 of the 4445
 * instructions, with every pool value and every branch target equal; the
 * one item it reports is the MOVA of the 55-entry switch jump table, whose
 * ".DATA.W Lcase-Lbase" entries it cannot evaluate. All 55 entries were
 * checked to reach the same instruction (by index) as the original table at
 * 8C0CD274. The byte layout differs only in where the literal pools fall:
 * in the original, SHC dumped a pool right after the first BRA
 * (8C0CD0E6-8C0CD180) holding E102Main's deferred pool words
 * (8C0CD100-8C0CD174, see code_8C0CBE00.c) plus this function's 0x8000;
 * compiled alone, nothing is pending there, 0x8000 goes to the next pool
 * and the code from 8C0CD180 on sits 0xA4 bytes earlier. Hence the
 * NON_MATCHING guard, as for E102Main, SonicMain and func_8C0AFF1A: a
 * byte-exact splice needs E102Main, func_8C0CD09C and this function in one
 * translation unit. The BSR/JSR choice for func_8C0D0528 needs the
 * translation-unit model at the end of the file (2746 bytes away, see
 * there).
 *
 * PC notes: the run/hover timer free.w[2] is accessed through E102_TIMER
 * (below) so that it does not overlap the work pointer free.ptr[0] on
 * 64-bit hosts (E102Main still clears free.w[2] at init: same header fix
 * needed there). The original reads pwp->free.ptr[0] before testing
 * pwp != NULL; kept, pwp is never NULL (E102Main passes PWP(mwp)).
 */

/*
 * E-102 line draw list (0x20 bytes; init 8C0D5552, see code_8C0CFA6E.c).
 */
typedef struct {
    NJS_POINT3 *src_p;          /* 0x00 */
    NJS_COLOR *src_col;         /* 0x04 */
    Sint16 *idx;                /* 0x08 */
    Sint16 num;                 /* 0x0C */
    Sint16 pad0E;               /* 0x0E */
    NJS_POINT3COL p3c;          /* 0x10 */
} E102_LINE;                    /* 0x20 */

/*
 * E-102 work (pwp->free.ptr[0]; CAlloc(1, 0x244) by E102Main). No shared
 * type yet: same full layout as code_8C0CBE00.c (merged from the E-102
 * files, no padding arrays, so the native layout agrees on 64-bit hosts).
 * Read here: smode (cleared when hit), spd_y (landing test), altitude and
 * unk23C bit 1 (hover landing), flag through func_8C0D2538/func_8C0D257E.
 */
typedef struct {
    Sint8 mode;                 /* 0x00 laser/targeting state (E102Main: MOV.B, == 3) */
    Sint8 smode;                /* 0x01 targeting arm state 0..5; cleared when E-102 is hit */
    Sint8 nb_target;            /* 0x02 valid markers (func_8C0D3C00) */
    Sint8 unk03;                /* 0x03 */
    Sint8 nb_lock;              /* 0x04 entries of lock_tp[] */
    Sint8 nb_shot;              /* 0x05 entries of shot_tp[] */
    Sint8 arm_idx;              /* 0x06 index into D_8C422D90 */
    Sint8 unk07;                /* 0x07 */
    Sint16 timer08;             /* 0x08 frame counter (++ every frame by E102Main) */
    Sint16 timer0A;             /* 0x0A lock-on count; cleared by func_8C0D04A0 */
    Sint16 timer0C;             /* 0x0C cleared by func_8C0D04A0 */
    Sint16 unk0E;               /* 0x0E */
    Sint16 laser_timer;         /* 0x10 */
    Sint16 lockon_timer;        /* 0x12 */
    Sint16 unk14;               /* 0x14 */
    Sint16 flag;                /* 0x16 bit 0: hover SE playing (8C0D257E, 8C0D2538) */
    Float hover_y;              /* 0x18 */
    Angle hover_phase;          /* 0x1C */
    Angle3 jet_ang;             /* 0x20 */
    Angle jet_phase;            /* 0x2C */
    task *target_tp;            /* 0x30 */
    NJS_POINT3 laser_pos;       /* 0x34 */
    NJS_POINT3 laser_vec;       /* 0x40 */
    Float range;                /* 0x4C laser length */
    Float range2;               /* 0x50 range squared */
    NJS_POINT3 arm_pos;         /* 0x54 */
    NJS_POINT3 arm_vec;         /* 0x60 */
    task *tp;                   /* 0x6C child task (destroyed by func_8C0CFA6E) */
    Float arm_frame;            /* 0x70 */
    Sint32 unk74;               /* 0x74 */
    Sint32 unk78;               /* 0x78 */
    Sint32 unk7C;               /* 0x7C */
    NJS_ACTION *arm_action;     /* 0x80 */
    Float scope_frame;          /* 0x84 */
    Angle blink_ang;            /* 0x88 */
    Angle scope_ang;            /* 0x8C */
    Float body_y;               /* 0x90 */
    NJS_POINT3 muzzle[2];       /* 0x94 */
    Float spd_y;                /* 0xAC pwp->spd.y copy (mode 7, FMOV.S); landing test < -2.0f in 8C0CD0A8 */
    E102_LINE line0;            /* 0xB0 */
    E102_LINE line1;            /* 0xD0 */
    Sint32 unkF0;               /* 0xF0 */
    task *lock_tp[32];          /* 0xF4 lock-on marker tasks */
    task *shot_tp[32];          /* 0x174 laser shot tasks */
    PL_ANALOG analog[8];        /* 0x1F4 stick history, [7] newest (8C0D317E, 8C0D3220) */
    Float ground_y;             /* 0x234 func_8C09222C(twp, 1.5f): height of the ground below */
    Float altitude;             /* 0x238 twp->pos.y - ground_y (mode 7: < 100.0f with unk23C & 2 -> mode 35) */
    Sint32 unk23C;              /* 0x23C func_8C0D278A(twp); bit 1 tested by 8C0CD0A8 */
    Float step_dist;            /* 0x240 0.0f at init; func_8C0D24E0 adds |spd.x| */
} E102WK;                       /* 0x244 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_OFFSET(E102_LINE, p3c, 0x10);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, smode, 0x01);
SA1_ASSERT_OFFSET(E102WK, flag, 0x16);
SA1_ASSERT_OFFSET(E102WK, tp, 0x6C);
SA1_ASSERT_OFFSET(E102WK, spd_y, 0xAC);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);
SA1_ASSERT_OFFSET(E102WK, analog, 0x1F4);
SA1_ASSERT_OFFSET(E102WK, altitude, 0x238);
SA1_ASSERT_OFFSET(E102WK, unk23C, 0x23C);

/* E-102 frame flag (bss 8C7A2EA8, set by E102Main) */
extern Sint8 B_8C7A2EA8;                /* positional SE enable: !func_8C049A4E() */

/* E-102 mode checks (same chunk; reached with JSR, return 1 when they changed twp->mode) */
Sint32 func_8C0D04A0(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D06C6(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D079A(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D0B16(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D0DF4(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D0E8C(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D0F0C(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D100A(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D10BC(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D11DC(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D14D6(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0D15C0(taskwk *twp, motionwk *mwp, playerwk *pwp);     /* drop the held object */
void func_8C0D1E38(taskwk *twp, motionwk *mwp, playerwk *pwp);     /* stand/idle action selector */
void func_8C0D1FC8(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0D2156(taskwk *twp, motionwk *mwp, playerwk *pwp);     /* JMP func_8C071700 */
void func_8C0D2538(taskwk *twp, E102WK *ewk);                      /* tests ewk->flag & 1 */
void func_8C0D257E(taskwk *twp, E102WK *ewk);                      /* clears ewk->flag bit 0 (SE 0x312/0x310) */
void func_8C0D2EC0(taskwk *twp, motionwk *mwp);
void func_8C0D49B8(task *tp, playerwk *pwp, E102WK *ewk);

/* E-102 jump check (code_8C0D04A0.c): an in-TU callee, BSR from cases 28
 * and 54, JSR from cases 21, 40 and 26; defined (ASM_STUB) after the TU
 * layout model at the end of the file */
Sint32 func_8C0D0528(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* player common; the call sites pass more arguments than the matched
 * definitions use (code_8C06CCA0.c, code_8C06CFCC.c, code_8C06CA30.c) */
extern Bool func_8C06CE40(taskwk *twp, motionwk *mwp);
extern Bool func_8C06CEFE(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);
extern Sint32 func_8C06CA30(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern void func_8C086F5E(Sint8 flag);
extern void func_8C07BBC2(Uint8 pno);

/* E-102 return to standing (code_8C0CD09C.c), reached with BSR from case 8 */
ASM_STUB(void func_8C0CD09C(taskwk *twp, motionwk *mwp, playerwk *pwp))

/* button state of the player's pad */
#define PER(twp) (B_8C754C08[(Uint8)(twp)->counter.b[0]])

/* positional SE at the player position */
#define E102_SE(twp, se) func_8C09413A((se), 0, 0, 0, (twp)->pos.x, (twp)->pos.y, (twp)->pos.z)

/* hit shape of the collision partner (CWP(twp)->hit_cwp->info[hit_num]) */
#define E102_HITINFO(twp) (CWP(twp)->hit_cwp->info[CWP(twp)->hit_num])

/*
 * E-102 run/hover timer, DC free.w[2] (pwp+0x80), the halfword right after
 * the work pointer free.ptr[0] (pwp+0x7C). Indexed with sizeof(void *) / 2
 * (2 under SHC, same code) so that on 64-bit hosts it stays right after the
 * 8-byte pointer instead of overlapping its high half: the offset of the
 * proposed header view free.e102 { void *wk; Sint16 timer; }.
 */
#define E102_TIMER(pwp) ((pwp)->free.w[sizeof(void *) / 2])

#ifdef NON_MATCHING
void E102ModeMachine(task *tp, motionwk *mwp, playerwk *pwp)
{
    taskwk *twp;
    E102WK *ewk;

    twp = tp->twp;
    ewk = (E102WK *)pwp->free.ptr[0];      /* read before the pwp != NULL test, as in the original */
    if (pwp != NULL && (pwp->item & 0x8000)) {
        twp->flag |= 0x8000;
        twp->wtimer = 0;
    } else if (twp->wtimer == 0) {
        if (twp->mode != 56) {
            if (func_8C0D04A0(twp, mwp, pwp)) {
                twp->flag |= 0x8000;
                if (twp->flag & TWF_HOLD) {
                    func_8C0D15C0(twp, mwp, pwp);
                }
                twp->flag &= ~0x3900;
                if (pwp->item & 3) {
                    pwp->item &= ~3;
                } else {
                    func_8C086F5E(twp->counter.b[0]);
                }
                ewk->smode = 0;
            } else {
                twp->flag &= ~0x8000;
            }
        }
    } else {
        twp->wtimer--;
        twp->flag &= ~4;
    }

    switch (twp->mode) {
    case 1:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        if (func_8C0D0F0C(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 2;
            pwp->mj.reqaction = 1;
            break;
        } else if (pwp->cstsp->tnorm.y < 0.77f && njScalor(&pwp->spd) > pwp->p.slide_speed) {
            twp->mode = 13;
            pwp->mj.reqaction = 20;
            break;
        }
        if ((pwp->attr & 2) && pwp->mj.reqaction != 44) {
            twp->mode = 33;
            pwp->mj.reqaction = 44;
            func_8C0D257E(twp, ewk);
            break;
        }
        break;

    case 2:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D0528(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D0E8C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && !(E102_HITINFO(twp).attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                twp->mode = 9;
                pwp->mj.reqaction = 11;
                twp->flag &= ~TWF_ONPATH;
            }
        }
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 8;
            pwp->mj.reqaction = 10;
            if (B_8C7A2EA8) {
                E102_SE(twp, 0x310);
            }
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (pwp->mj.reqaction == 19) break;
        if (pwp->spd.x >= pwp->p.run_speed) {
            pwp->spd.x = pwp->p.run_speed;
            E102_TIMER(pwp)++;
            if (E102_TIMER(pwp) > 131.99999f) {
                E102_TIMER(pwp) = 0;
                twp->mode = 31;
                pwp->mj.reqaction = 3;
                if (B_8C7A2EA8) {
                    E102_SE(twp, 0x4AA);
                }
                break;
            }
        } else {
            E102_TIMER(pwp) = 0;
        }
        if ((pwp->attr & 2) && pwp->mj.reqaction != 44) {
            twp->mode = 33;
            pwp->mj.reqaction = 44;
            func_8C0D257E(twp, ewk);
            if (B_8C7A2EA8) {
                E102_SE(twp, 0x4AA);
            }
            break;
        }
        break;

    case 31:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) {
            func_8C0D257E(twp, ewk);
            twp->flag &= ~TWF_ONPATH;
            twp->mode = 37;
            pwp->mj.reqaction = 5;
            break;
        }
        if (func_8C0D0528(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (_builtin_fabsf(pwp->spd.x) > 0.5f) {
            func_8C0D2538(twp, ewk);
        } else {
            func_8C0D257E(twp, ewk);
        }
        if ((pwp->attr & 2) && pwp->mj.reqaction == 5) {
            twp->mode = 33;
            pwp->mj.reqaction = 45;
            func_8C0D257E(twp, ewk);
            break;
        }
        break;

    case 33:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (!(pwp->attr & 2) && pwp->mj.reqaction == 46) {
            E102_TIMER(pwp)++;
            if (E102_TIMER(pwp) > 15) {
                E102_TIMER(pwp) = 0;
                twp->mode = 31;
                pwp->mj.reqaction = 58;
                func_8C0D2538(twp, ewk);
            }
            break;
        } else {
            E102_TIMER(pwp) = 0;
        }
        break;

    case 3:
        if (func_8C0D079A(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D0DF4(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D0528(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 2;
            pwp->mj.reqaction = 1;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D0E8C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 29:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (pwp->mj.action == 6 || pwp->mj.action == 31) {
            sa1_forcewk *fwp;
            register Sint8 mode;

            pwp->spd.y = pwp->p.jmp_y_spd;
            if (pwp->ttp != NULL) {
                fwp = (sa1_forcewk *)pwp->ttp->fwp;
                if (fwp != NULL) {
                    SetAccelerationP(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
                }
            }
            twp->flag &= ~2;
            pwp->jumptimer = 0;
            if (B_8C7A2EA8) {
                E102_SE(twp, 0x30F);
            }
            if (pwp->mj.action == 31) {
                mode = 23;
            } else {
                mode = 4;
            }
            twp->mode = mode;
        }
        break;

    case 4:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 8;
                pwp->mj.reqaction = 10;
            } else if (func_8C0D0E8C(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 0;
                } else {
                    pwp->mj.reqaction = 9;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0D1FC8(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C0D06C6(twp, mwp, pwp)) {
            twp->mode = 7;
            pwp->mj.reqaction = 8;
            break;
        }
        if (func_8C06CEFE(twp, pwp)) {
            pwp->spd.y SH_ONLY(+= pwp->p.jmp_addit * 0.79999997f)
                PC_ONLY(= SA1_FMAC(pwp->p.jmp_addit, 0.79999997f, pwp->spd.y));
        }
        break;

    case 35:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            E102WK *ewk2;

            ewk2 = (E102WK *)pwp->free.ptr[0];
            if (ewk2->unk23C & 2) {
                twp->mode = 33;
                func_8C0D257E(twp, ewk2);
            } else {
                twp->mode = 31;
                pwp->mj.reqaction = 58;
                func_8C0D2538(twp, ewk2);
            }
            break;
        }
        if (func_8C0D06C6(twp, mwp, pwp)) break;
        if (func_8C06CEFE(twp, pwp)) {
            pwp->spd.y SH_ONLY(+= pwp->p.jmp_addit * 0.79999997f)
                PC_ONLY(= SA1_FMAC(pwp->p.jmp_addit, 0.79999997f, pwp->spd.y));
        }
        break;

    case 37:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            E102WK *ewk2;

            ewk2 = (E102WK *)pwp->free.ptr[0];
            if (ewk2->unk23C & 2) {
                twp->mode = 33;
                pwp->mj.reqaction = 45;
                func_8C0D257E(twp, ewk2);
            } else {
                twp->mode = 31;
                func_8C0D2538(twp, ewk2);
            }
            break;
        }
        if (func_8C0D06C6(twp, mwp, pwp)) break;
        if (func_8C06CEFE(twp, pwp)) {
            pwp->spd.y SH_ONLY(+= pwp->p.jmp_addit * 0.79999997f)
                PC_ONLY(= SA1_FMAC(pwp->p.jmp_addit, 0.79999997f, pwp->spd.y));
        }
        break;

    case 5:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 7;
            pwp->mj.reqaction = 7;
        }
        break;

    case 6:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 7;
            pwp->mj.reqaction = 7;
        }
        break;

    case 7:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        {
            E102WK *ewk2;

            ewk2 = (E102WK *)pwp->free.ptr[0];
            if (ewk2->altitude < 100.0f && (ewk2->unk23C & 2)) {
                twp->mode = 35;
                pwp->mj.reqaction = 44;
                break;
            }
        }
        if (twp->flag & TWF_GROUND) {
            func_8C0945A0(0x315);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 8;
                pwp->mj.reqaction = 10;
                break;
            }
            twp->mode = 2;
            if (((E102WK *)pwp->free.ptr[0])->spd_y < -2.0f) {
                pwp->mj.reqaction = 9;
                if (B_8C7A2EA8) {
                    E102_SE(twp, 0x311);
                }
                func_8C0D2EC0(twp, mwp);
            } else {
                pwp->mj.reqaction = 10;
                if (B_8C7A2EA8) {
                    E102_SE(twp, 0x30E);
                }
            }
        }
        break;

    case 8:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 2;
            pwp->mj.reqaction = 19;
            func_8C0D2156(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 10) {
            func_8C0CD09C(twp, mwp, pwp);
        }
        break;

    case 39:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 2;
            pwp->mj.reqaction = 19;
            func_8C0D2156(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 47) {
            twp->mode = 1;
            pwp->mj.reqaction = 56;
        }
        break;

    case 9:
        if (func_8C0D079A(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0D0528(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0D0DF4(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0D0E8C(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && !(E102_HITINFO(twp).attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                break;
            case 3:
                if (!(twp->flag & TWF_HOLD)) {
                    twp->mode = 2;
                    pwp->mj.reqaction = 1;
                    func_8C0945A0(15);
                }
                break;
            default:
                if (twp->flag & TWF_HOLD) {
                    twp->mode = 26;
                    pwp->mj.reqaction = 27;
                    if (B_8C7A2EA8) {
                        E102_SE(twp, 0x4A9);
                    }
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 1;
                }
                func_8C0945A0(15);
                break;
            }
            break;
        }
        func_8C0D15C0(twp, mwp, pwp);
        twp->mode = 2;
        pwp->mj.reqaction = 1;
        func_8C0945A0(15);
        break;

    case 10:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (twp->wtimer != 120 && (twp->flag & TWF_GROUND)) {
            if (func_8C0D0E8C(twp, mwp, pwp)) {
                func_8C0CD09C(twp, mwp, pwp);
                break;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                pwp->mj.reqaction = 14;
            }
        }
        break;

    case 11:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        if (func_8C0D0E8C(twp, mwp, pwp)) break;
        if (pwp->spd.x >= 0.0f) {
            twp->mode = 2;
            func_8C0D1FC8(twp, mwp, pwp);
        }
        break;

    case 52:
        if (pwp->mj.reqaction == 67 && pwp->mj.nframe >= 53.0f) {
            E102WK *ewk2;

            ewk2 = (E102WK *)pwp->free.ptr[0];
            func_8C0D49B8(tp, pwp, ewk2);
            twp->mode = 14;
        }
        /* fall through */
    case 14:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0D0B16(twp, mwp, pwp)) break;
        } else {
            if (func_8C0D079A(twp, mwp, pwp)) break;
        }
        break;

    case 12:
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 17 && pwp->mj.reqaction != 18) {
            func_8C0CD09C(twp, mwp, pwp);
        }
        break;

    case 13:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 2;
            pwp->mj.reqaction = 1;
            break;
        }
        if (njScalor(&mwp->spd) < pwp->p.slide_speed || pwp->cstsp->tnorm.y > 0.97999997f) {
            twp->mode = 1;
            pwp->mj.reqaction = 42;
        }
        break;

    case 47:
        if (func_8C0D079A(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D0528(twp, mwp, pwp)) {
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
            twp->mode = 7;
            pwp->mj.reqaction = 8;
            PClearSpeed(mwp, pwp);
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 48:
        if (func_8C0D079A(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D0528(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (twp->flag & TWF_ONPATH) {
            /* nothing (SonicModeMachine form; see the top comment) */
        } else {
            twp->mode = 7;
            pwp->mj.reqaction = 8;
            PClearSpeed(mwp, pwp);
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 49:
        if (pwp->mj.reqaction == 69 && pwp->mj.nframe >= 78.0f) {
            func_8C07BBC2(twp->counter.b[0]);
            twp->mode = 51;
        }
        break;

    case 15:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0D0B16(twp, mwp, pwp)) break;
        } else {
            if (func_8C0D079A(twp, mwp, pwp)) break;
        }
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (func_8C0D100A(twp, mwp, pwp)) break;
        break;

    case 18:
        if (func_8C0D14D6(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        break;

    case 17:
        if (func_8C0D0B16(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (func_8C0D10BC(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        if (((Sint32)PER(twp).on & (Sint32)D_8C19E3A0) && GetAnalog(twp, NULL, NULL)
            && (Uint8)pwp->htp->twp->id == 4) {
            twp->mode = 54;
            pwp->mj.reqaction = 82;
            break;
        }
        if (func_8C0D10BC(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 19;
            pwp->mj.reqaction = 26;
        }
        break;

    case 19:
        if (func_8C0D0B16(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D0528(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D10BC(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D0E8C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 21;
            pwp->mj.reqaction = 23;
            if (B_8C7A2EA8) {
                E102_SE(twp, 0x310);
            }
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (pwp->spd.x >= pwp->p.rush_speed) {
            pwp->spd.x = pwp->p.rush_speed;
            E102_TIMER(pwp)++;
            if (E102_TIMER(pwp) > 131.99999f) {
                E102_TIMER(pwp) = 0;
                twp->mode = 32;
                pwp->mj.reqaction = 54;
                break;
            }
        } else {
            E102_TIMER(pwp) = 0;
        }
        if ((pwp->attr & 2) && pwp->mj.reqaction != 44) {
            twp->mode = 34;
            pwp->mj.reqaction = 50;
            break;
        }
        break;

    case 32:
        if (func_8C0D0B16(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D0528(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D10BC(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C0D0E8C(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 40;
            pwp->mj.reqaction = 23;
            if (B_8C7A2EA8) {
                E102_SE(twp, 0x310);
            }
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if ((pwp->attr & 2) && pwp->mj.reqaction == 55) {
            twp->mode = 34;
            pwp->mj.reqaction = 51;
            break;
        }
        break;

    case 34:
        if (func_8C0D0B16(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        if (!(pwp->attr & 2) && pwp->mj.reqaction == 52) {
            E102_TIMER(pwp)++;
            if (E102_TIMER(pwp) > 15) {
                E102_TIMER(pwp) = 0;
                twp->mode = 32;
                pwp->mj.reqaction = 59;
                break;
            }
            break;
        }
        E102_TIMER(pwp) = 0;
        break;

    case 16:
    case 20:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0D0B16(twp, mwp, pwp)) break;
        } else {
            if (func_8C0D079A(twp, mwp, pwp)) break;
        }
        if (func_8C0D11DC(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 35 && pwp->mj.reqaction != 36 && pwp->mj.reqaction != 39) {
            func_8C0CD09C(twp, mwp, pwp);
        }
        break;

    case 21:
        if (func_8C0D0B16(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 19;
            pwp->mj.reqaction = 61;
            func_8C0D2156(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 23) {
            twp->mode = 17;
            pwp->mj.reqaction = 57;
        }
        break;

    case 40:
        if (func_8C0D0B16(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 32;
            pwp->mj.reqaction = 24;
            func_8C0D2156(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 23) {
            twp->mode = 17;
            pwp->mj.reqaction = 57;
        }
        break;

    case 22:
        if (func_8C0D0B16(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            func_8C0945A0(0x315);
            if (pwp->attr & 2) {
                twp->mode = 34;
                pwp->mj.reqaction = 50;
                break;
            }
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 21;
                pwp->mj.reqaction = 23;
                if (B_8C7A2EA8) {
                    E102_SE(twp, 0x30E);
                }
                break;
            }
            if (func_8C0D0E8C(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 25;
                    if (B_8C7A2EA8) {
                        E102_SE(twp, 0x30E);
                    }
                } else {
                    pwp->mj.reqaction = 34;
                    if (B_8C7A2EA8) {
                        E102_SE(twp, 0x311);
                    }
                }
                break;
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 19;
            pwp->mj.reqaction = 26;
            if (B_8C7A2EA8) {
                E102_SE(twp, 0x30E);
            }
            break;
        }
        if (func_8C0D10BC(twp, mwp, pwp)) {
            twp->flag &= ~TWF_ONPATH;
        }
        break;

    case 23:
        if (func_8C0D0B16(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            if (pwp->attr & 2) {
                twp->mode = 34;
                pwp->mj.reqaction = 50;
                break;
            }
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 21;
                pwp->mj.reqaction = 23;
            } else if (func_8C0D0E8C(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 25;
                } else {
                    pwp->mj.reqaction = 34;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 19;
                func_8C0D1FC8(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C06CEFE(twp, pwp)) {
            pwp->spd.y SH_ONLY(+= pwp->p.jmp_addit * 0.39999998f)
                PC_ONLY(= SA1_FMAC(pwp->p.jmp_addit, 0.39999998f, pwp->spd.y));
        }
        if (func_8C0D10BC(twp, mwp, pwp)) break;
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 22;
            pwp->mj.reqaction = 32;
        }
        break;

    case 38:
        if (func_8C0D0B16(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            if (pwp->attr & 2) {
                twp->mode = 34;
                pwp->mj.reqaction = 50;
                break;
            } else {
                twp->mode = 32;
            }
            break;
        }
        if (func_8C06CEFE(twp, pwp)) {
            pwp->spd.y SH_ONLY(+= pwp->p.jmp_addit * 0.39999998f)
                PC_ONLY(= SA1_FMAC(pwp->p.jmp_addit, 0.39999998f, pwp->spd.y));
        }
        if (func_8C0D10BC(twp, mwp, pwp)) break;
        break;

    case 36:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            E102WK *ewk2;

            ewk2 = (E102WK *)pwp->free.ptr[0];
            if (ewk2->unk23C & 2) {
                twp->mode = 34;
                func_8C0D257E(twp, ewk2);
            } else {
                twp->mode = 32;
                pwp->mj.reqaction = 58;
                func_8C0D2538(twp, ewk2);
            }
            break;
        }
        if (func_8C0D06C6(twp, mwp, pwp)) break;
        if (func_8C06CEFE(twp, pwp)) {
            pwp->spd.y SH_ONLY(+= pwp->p.jmp_addit * 0.79999997f)
                PC_ONLY(= SA1_FMAC(pwp->p.jmp_addit, 0.79999997f, pwp->spd.y));
        }
        break;

    case 24:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 22;
            pwp->mj.reqaction = 32;
        }
        break;

    case 25:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 22;
            pwp->mj.reqaction = 32;
        }
        break;

    case 26:
        if (func_8C0D079A(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0D0528(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (func_8C0D0DF4(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (!((Sint32)PER(twp).on & (Sint32)D_8C19E3A0)) {
            func_8C0CD09C(twp, mwp, pwp);
            func_8C0D15C0(twp, mwp, pwp);
            func_8C0945A0(15);
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && !(E102_HITINFO(twp).attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                twp->mode = 9;
                pwp->mj.reqaction = 11;
                break;
            case 0:
                if (twp->flag & TWF_HOLD) {
                    twp->mode = 18;
                    pwp->mj.reqaction = 30;
                    func_8C0945A0(15);
                }
                break;
            default:
                if (twp->flag & TWF_HOLD) {
                    twp->mode = 26;
                    pwp->mj.reqaction = 27;
                    if (B_8C7A2EA8) {
                        E102_SE(twp, 0x4A9);
                    }
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 1;
                }
                func_8C0945A0(15);
                break;
            }
            break;
        } else {
            func_8C0D15C0(twp, mwp, pwp);
            twp->mode = 2;
            pwp->mj.reqaction = 1;
            func_8C0945A0(15);
            break;
        }
        break;

    case 27:
        if (twp->flag & TWF_HOLD) {
            if (func_8C0D0B16(twp, mwp, pwp)) break;
        } else {
            if (func_8C0D079A(twp, mwp, pwp)) break;
        }
        if (twp->flag & TWF_GROUND) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 21;
                pwp->mj.reqaction = 23;
                break;
            }
            if (func_8C0D0E8C(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (twp->flag & TWF_HOLD) {
                    if (mwp->spd.y > -5.0f) {
                        pwp->mj.reqaction = 25;
                        if (B_8C7A2EA8) {
                            E102_SE(twp, 0x30E);
                        }
                    } else {
                        pwp->mj.reqaction = 34;
                        if (B_8C7A2EA8) {
                            E102_SE(twp, 0x311);
                        }
                    }
                } else if (mwp->spd.y > -5.0f) {
                    func_8C0D1E38(twp, mwp, pwp);
                    if (B_8C7A2EA8) {
                        E102_SE(twp, 0x30E);
                    }
                } else {
                    pwp->mj.reqaction = 9;
                    if (B_8C7A2EA8) {
                        E102_SE(twp, 0x311);
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
            func_8C0D1FC8(twp, mwp, pwp);
            break;
        }
        if (func_8C0D11DC(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 37 && pwp->mj.reqaction != 38) {
            twp->mode = 7;
            pwp->mj.reqaction = 8;
        }
        break;

    case 28:
        if (func_8C0D0B16(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        if (!func_8C06CA30(twp, mwp, pwp)) {
            func_8C0CD09C(twp, mwp, pwp);
            func_8C0D15C0(twp, mwp, pwp);
            break;
        }
        if (!GetAnalog(twp, NULL, NULL)) {
            twp->mode = 18;
            pwp->mj.reqaction = 30;
        }
        break;

    case 54:
        if (func_8C0D0B16(twp, mwp, pwp)) break;
        if (func_8C0D0DF4(twp, mwp, pwp)) break;
        if (func_8C0D0528(twp, mwp, pwp)) break;
        if (pwp->htp == NULL || pwp->htp->twp->id != 4) {
            func_8C0CD09C(twp, mwp, pwp);
            func_8C0D15C0(twp, mwp, pwp);
            break;
        }
        if (!GetAnalog(twp, NULL, NULL)) {
            twp->mode = 17;
            pwp->mj.reqaction = 84;
        }
        break;

    case 42:
        break;

    case 43:
        if (func_8C0D079A(twp, mwp, pwp)) break;
        if (twp->flag & TWF_GROUND) {
            func_8C0945A0(0x315);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 8;
                pwp->mj.reqaction = 10;
                break;
            }
            if (func_8C0D0E8C(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 10;
                if (B_8C7A2EA8) {
                    E102_SE(twp, 0x30E);
                }
                break;
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            if (B_8C7A2EA8) {
                E102_SE(twp, 0x311);
            }
            func_8C0D2EC0(twp, mwp);
        }
        break;

    case 53:
        if ((twp->flag & TWF_REQUEST) && twp->smode == 28) {
            twp->mode = 5;
            pwp->mj.reqaction = 6;
            twp->flag &= ~0x500;
            twp->flag &= ~TWF_ONPATH;
            break;
        }
        break;

    case 55:
        if (EWP(twp)->list4C == NULL) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0CD09C(twp, mwp, pwp);
        }
        break;
    }
}
#else
ASM_STUB(void E102ModeMachine(task *tp, motionwk *mwp, playerwk *pwp))
#endif

#if defined(NON_MATCHING) && !defined(TARGET_PC)
/*
 * Translation-unit layout model (SHC check only, never spliced: the gap has
 * no address in the image and func_8C0D0528 is an ASM_STUB). In the original
 * TU func_8C0CFA6E, func_8C0CFACE and func_8C0D04A0 (2746 bytes) lie between
 * this function and func_8C0D0528, so SHC calls func_8C0D0528 with BSR from
 * cases 28 and 54 (within 4 KB) but with JSR from cases 21, 40 and 26.
 */
#define TU_GAP4  __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink();
#define TU_GAP16 TU_GAP4 TU_GAP4 TU_GAP4 TU_GAP4
#define TU_GAP64 TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP16
void tu_gap_8C0CFA6E(void)
{
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
}
#endif

/* E-102 jump check (code_8C0D04A0.c), reached with BSR from cases 28 and 54 */
ASM_STUB(Sint32 func_8C0D0528(taskwk *twp, motionwk *mwp, playerwk *pwp))
