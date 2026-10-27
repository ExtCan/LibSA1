#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"     /* func_8C06CA30/CE40/CEBE/CFCC, func_8C07C9D2, func_8C07BA0A, func_8C01AD2C */

/*
 * 1ST_READ 8C0B70F4: TailsModeMachine (chunk 8C0B6120-8C0BA820, function
 * 4/6; func_8C0B6120, TailsMain, func_8C0B70E8, func_8C0BA738 and
 * func_8C0BA75A are separate assignments).
 *
 * Tails' action state machine, called once per frame by TailsMain
 * (8C0B6148, JSR) with (twp, mwp, pwp) unless twp->mode is 60: first the
 * hurt / invincibility timer on twp->wtimer and the damage check
 * (func_8C0BBAE2), then a switch on twp->mode (1..62) that decides the next
 * mode and the requested motion (pwp->mj.reqaction) from the input checks
 * of the Tails action chunk 8C0BAFC0 (func_8C0BBDD6, func_8C0BCC9A, ...
 * return non-zero when they changed the mode themselves). Same programmer
 * and shape as SonicModeMachine (code_8C0A966C.c). Tails specifics: mode 15
 * (entered from modes 6/10 through func_8C06CEBE: reqaction 37,
 * twp->flag 0x400, twp->timer.b[2] bit 0, camera func_8C023360 for player 0
 * with twp->timer.b[3] bit 1), pwp->free.f[6]/f[7], the mash counter
 * twp->counter.w[1] (mode 42), the snowboard modes 48-54 (func_8C0B6120
 * score bonus, SE through func_8C0BFCA2).
 *
 * Code generation notes. The function is so large that SHC compiles it
 * without register allocation: twp/mwp/pwp live at @(64/60/56,R15) and every
 * block local in @(0..52,R15). The cases are in the original source order
 * (the order of their code), not in numeric order. The SonicModeMachine
 * idioms decide SHC's scratch-register choice and its cross-jumping:
 * "if (c) { ...; break; } break;" forms, (Sint32) casts on the pad tests,
 * (Uint8) on the id compares, the ternary njRotate statements and the
 * (const Float) cast that keeps the two angle FMULs. A call followed
 * directly by "break;" is cross-jumped into the last identical "call;
 * break;" (func_8C0BFD40 of case 32, func_8C0B70E8 of case 45,
 * func_8C0BBA4E of case 53, func_8C0BFCA2 of case 50); a call that ends an
 * if-block before the "break;" keeps its own copy (cases 16, 22/26, 51, 54).
 *
 * Status: NON_MATCHING. With the translation-unit model below, this body
 * compiles to the original's 5826 instructions; tools/shcdiff.py
 * --flags=-define=NON_MATCHING reports 9 items: the MOVA of the 62-entry
 * switch table (shcdiff cannot evaluate ".DATA.W Lx-Ly"; all 62 entries were
 * checked to reach the same instruction as the table at 8C0B725C) and, in
 * the two identical mode-15 blocks of cases 6 and 10 (8C0B7A54-8C0B7A64,
 * 8C0B7E52-8C0B7E5E), the scratch register holding the addresses of
 * func_8C0673E0 and GetStageNumber (R2/R2 in the original, R3/R1 here).
 * Every other encoding, literal-pool value and branch target is equal.
 * FMAC: the two "spd.y += jmp_addit * k" (FMAC_ACC: SA1_FMAC on PC).
 */

extern Uint32 B_8C754E7C[];     /* per-player input flags (mash detection) */
extern Float *D_8C112048;       /* reads [0x48 / 4] */

/* Tails action checks (chunk 8C0BAFC0; reached with JSR, return 1 when
 * they changed twp->mode) */
extern Sint32 func_8C0BBAE2(taskwk *twp, playerwk *pwp);                /* damage check */
extern void func_8C0BD440(taskwk *twp, playerwk *pwp);                  /* release the held object */
extern Sint32 func_8C0BBDD6(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0BCC9A(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C0BBBAA(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C0BCDD0(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0BD83C(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C0BD800(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C0BCD00(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C0BCDA0(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C0BBCEA(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C0BC61C(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0BCEC0(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0BD336(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0BCF56(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0BD098(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C0BCBCA(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern void func_8C0BAFC0(taskwk *twp, playerwk *pwp);                  /* stand action selector */
extern void func_8C0BB11C(taskwk *twp, motionwk *mwp, playerwk *pwp);   /* run action selector */
extern void func_8C0BBA4E(taskwk *twp, playerwk *pwp);                  /* run effect / sound task (8C07BA0A) */
extern void func_8C0BEBE0(taskwk *twp, playerwk *pwp);
extern void func_8C0BEDA0(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern void func_8C0BFCA2(taskwk *twp, Sint32 se);                      /* Tails SE */
extern void func_8C0BFD40(taskwk *twp);
extern Sint32 func_8C0B6120(Sint32 n);                                  /* snowboard trick bonus (code_8C0B6120.c) */

/* player common (func_8C06CA30, func_8C06CE40, func_8C06CEBE, func_8C06CFCC,
 * func_8C07C9D2 and func_8C07BA0A: sa1/protos.h) */
extern Bool func_8C06CEFE(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C05E6C4(taskwk *twp);
extern Sint32 func_8C0673E0(void);
extern void func_8C086F5E(Sint8 flag);
/* camera (func_8C01AD2C: sa1/protos.h) */
extern void func_8C01AE60(void);
extern void func_8C01FB98(void);
extern void func_8C01FD6C(void);
extern void func_8C023360(void);

#if defined(NON_MATCHING) && !defined(TARGET_PC)
/*
 * Translation-unit layout model (SHC check only, never spliced). In the
 * original TU TailsMain ends right before func_8C0B70E8 with 13 literal
 * pool words still pending (D_8C2592C8 ... func_8C06C6F8): SHC dumps them,
 * with func_8C0B70E8's func_8C0BAFC0, into the first pool of this function
 * (8C0B71C0-8C0B7207), so the branches around that pool are longer. The
 * model leaves 13 pending words and starts TailsModeMachine at the original
 * address modulo 32 (0x14). func_8C0B70E8 is its real 12-byte body (BSR
 * from case 11). The splicer skips both because of
 * ASM_STUB(void tu_tail_TailsMain(void **v, Sint32 *w)) and
 * ASM_STUB(void func_8C0B70E8(taskwk *twp, playerwk *pwp)).
 */
extern Sint32 tu_pool0, tu_pool1, tu_pool2, tu_pool3, tu_pool4, tu_pool5, tu_pool6;
extern Sint32 tu_pool7, tu_pool8, tu_pool9, tu_pool10, tu_pool11, tu_pool12;
void tu_tail_TailsMain(void **v, Sint32 *w)
{
    w[0] = 1;
    w[1] = 2;
    w[2] = 3;
    w[4]++;
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
    v[10] = &tu_pool10;
    v[11] = &tu_pool11;
    v[12] = &tu_pool12;
}

/* Tails return to standing (code_8C0B70E8.c), reached with BSR from case 11 */
void func_8C0B70E8(taskwk *twp, playerwk *pwp)
{
    twp->mode = 1;
    pwp->waittimer = 0;
    func_8C0BAFC0(twp, pwp);
}
#else
void func_8C0B70E8(taskwk *twp, playerwk *pwp);
#endif

/* button state of the player's pad */
#define PER(twp) (B_8C754C08[(Uint8)(twp)->counter.b[0]])

/* hit shape of the collision partner */
#define HITINFO(twp) (CWP(twp)->hit_cwp->info[CWP(twp)->hit_num])

/* "acc += a * b" compiled to FMAC: fused on PC (SA1_FMAC); SA1_FMAC itself
 * (a * b + acc) changes the SHC output here */
#if defined(TARGET_PC)
#define FMAC_ACC(acc, a, b) ((acc) = SA1_FMAC((a), (b), (acc)))
#else
#define FMAC_ACC(acc, a, b) ((acc) += (a) * (b))
#endif

#ifdef NON_MATCHING
void TailsModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp != NULL && (pwp->item & 0x8000)) {
        twp->flag |= 0x8000;
        twp->wtimer = 0;
    } else if (twp->wtimer == 0) {
        if (func_8C0BBAE2(twp, pwp)) {
            twp->flag |= 0x8000;
            if (twp->flag & 0x800) {
                func_8C0BD440(twp, pwp);
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
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) break;
        if (func_8C0BCDD0(twp, mwp, pwp)) break;
        if (func_8C0BD83C(twp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            break;
        } else if (pwp->cstsp->tnorm.y < 0.77f && njScalor(&pwp->spd) > pwp->p.slide_speed) {
            twp->mode = 17;
            pwp->mj.reqaction = 23;
        }
        break;

    case 2:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp) || func_8C0BBBAA(twp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (pwp->spd.x > 2.0f) {
            if (func_8C0BD800(twp, pwp)) break;
        } else {
            if (func_8C0BD83C(twp, pwp)) break;
        }
        if (func_8C0BCDD0(twp, mwp, pwp)) break;
        if (func_8C0BD83C(twp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0BCD00(twp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4)
            && !(HITINFO(twp).attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                twp->mode = 12;
                pwp->mj.reqaction = 24;
                twp->flag &= ~0x2000;
            }
        }
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 11;
            pwp->mj.reqaction = 21;
            func_8C0BFCA2(twp, 18);
            twp->flag &= ~0x2000;
        }
        break;
    case 3:
        twp->flag &= ~0x500;
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0BBBAA(twp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0BCD00(twp, pwp)) {
            twp->flag &= ~0x2000;
        }
        break;

    case 4:
        if (func_8C0BBDD6(twp, mwp, pwp)) {
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C0BCC9A(twp, pwp)) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 2.0f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            break;
        }
        if (func_8C0BBBAA(twp, pwp)) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 2.0f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            break;
        }
        if (func_8C0BCDA0(twp, pwp)) {
            twp->flag &= ~0x500;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) {
            twp->mode = 2;
            pwp->mj.reqaction = 36;
            twp->flag &= ~0x500;
            break;
        }
        break;

    case 5:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 5.0f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            twp->flag &= ~0x2000;
            break;
        }
        if (pwp->spd.x < 1.84999993f) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 5.0f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            twp->flag &= ~0x2000;
        }
        break;

    case 6:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            func_8C0BFCA2(twp, 33);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 11;
                pwp->mj.reqaction = 21;
            } else if (func_8C0BCD00(twp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 2;
                } else {
                    pwp->mj.reqaction = 20;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0BB11C(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C06CEBE(twp, pwp)) {
            twp->flag &= ~0x500;
            twp->mode = 15;
            pwp->mj.reqaction = 37;
            pwp->free.f[7] = 0.0f;
            twp->flag &= ~0x500;
            twp->flag |= 0x400;
            twp->timer.b[2] |= 1;
            twp->timer.b[3] &= ~1;
            if (!twp->counter.b[0] && func_8C0673E0()
                && (GetStageNumber() & 0xFF00) != 0x900) {
                twp->timer.b[3] |= 2;
                func_8C01AD2C(func_8C023360, 20, 2);
            }
            func_8C0BFCA2(twp, 0x46D);
            break;
        }
        if (func_8C0BBCEA(twp, pwp)) break;
        if (func_8C06CEFE(twp, pwp)) {
            FMAC_ACC(pwp->spd.y, pwp->p.jmp_addit, 0.79999997f);
        }
        break;

    case 7:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 10;
            if (pwp->mj.reqaction == 64) {
                pwp->mj.reqaction = 65;
            } else if (pwp->mj.reqaction == 66) {
                pwp->mj.reqaction = 67;
            } else {
                pwp->mj.reqaction = 18;
            }
        }
        break;

    case 8:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 10;
            pwp->mj.reqaction = 18;
        }
        break;

    case 9:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            func_8C0BFCA2(twp, 33);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 11;
                pwp->mj.reqaction = 21;
            } else if (func_8C0BCD00(twp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 2;
                } else {
                    pwp->mj.reqaction = 20;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0BB11C(twp, mwp, pwp);
            }
        }
        break;

    case 10:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->timer.b[3] & 2) {
                func_8C01AE60();
            }
            twp->timer.b[2] &= ~1;
            func_8C0BFCA2(twp, 33);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 11;
                pwp->mj.reqaction = 21;
            } else if (func_8C0BCD00(twp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 1;
                } else {
                    pwp->mj.reqaction = 20;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0BB11C(twp, mwp, pwp);
            }
            break;
        }
        if (func_8C06CEBE(twp, pwp)) {
            twp->flag &= ~0x500;
            twp->mode = 15;
            pwp->mj.reqaction = 37;
            pwp->free.f[7] = 0.0f;
            twp->flag &= ~0x500;
            twp->flag |= 0x400;
            twp->timer.b[2] |= 1;
            twp->timer.b[3] &= ~1;
            if (!twp->counter.b[0] && func_8C0673E0()
                && (GetStageNumber() & 0xFF00) != 0x900) {
                twp->timer.b[3] |= 2;
                func_8C01AD2C(func_8C023360, 20, 2);
            }
            func_8C0BFCA2(twp, 0x46D);
            break;
        }
        break;

    case 19:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->timer.b[3] & 2) {
                func_8C01AE60();
            }
            twp->timer.b[2] &= ~1;
            func_8C0BFCA2(twp, 33);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 11;
                pwp->mj.reqaction = 21;
            } else if (func_8C0BCD00(twp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 1;
                } else {
                    pwp->mj.reqaction = 20;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0BB11C(twp, mwp, pwp);
            }
        }
        break;

    case 11:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 2;
            pwp->mj.reqaction = 22;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 21) {
            func_8C0B70E8(twp, pwp);
        }
        break;

    case 12:
        if (func_8C0BBDD6(twp, mwp, pwp)) {
            func_8C0BFD40(twp);
            break;
        }
        if (func_8C0BBBAA(twp, pwp)) {
            func_8C0BFD40(twp);
            break;
        }
        if (func_8C0BCC9A(twp, pwp)) {
            func_8C0BFD40(twp);
            break;
        }
        if (func_8C0BCD00(twp, pwp)) {
            func_8C0BFD40(twp);
            break;
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4)
            && !(HITINFO(twp).attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                break;
            case 3:
                if (!(twp->flag & 0x800)) {
                    twp->mode = 2;
                    pwp->mj.reqaction = 9;
                    func_8C0BFD40(twp);
                }
                break;
            default:
                if (twp->flag & 0x800) {
                    twp->mode = 32;
                    pwp->mj.reqaction = 73;
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 9;
                }
                func_8C0BFD40(twp);
                break;
            }
            break;
        }
        func_8C0BD440(twp, pwp);
        twp->mode = 2;
        pwp->mj.reqaction = 9;
        func_8C0BFD40(twp);
        break;
    case 13:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 26 && pwp->mj.reqaction != 27) {
            if (func_8C0BCD00(twp, pwp)) {
                func_8C0B70E8(twp, pwp);
                break;
            } else {
                twp->mode = 2;
                func_8C0BB11C(twp, mwp, pwp);
                break;
            }
            break;
        }
        if (twp->wtimer <= 0) {
            if (twp->flag & 3) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0BB11C(twp, mwp, pwp);
                break;
            } else {
                twp->mode = 10;
                pwp->mj.reqaction = 19;
            }
            break;
        }
        if (twp->wtimer <= 60 && GetAnalog(twp, NULL, NULL)) {
            if (func_8C0BBBAA(twp, pwp)) break;
            if (twp->flag & 3) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0BB11C(twp, mwp, pwp);
                break;
            }
            break;
        }
        if (twp->wtimer == 120) break;
        if (twp->flag & 3) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            pwp->mj.reqaction = 27;
        }
        break;

    case 14:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) break;
        if (func_8C0BCD00(twp, pwp)) break;
        if (pwp->spd.x >= 0.0f) {
            twp->mode = 2;
            func_8C0BB11C(twp, mwp, pwp);
            break;
        }
        break;

    case 18:
        if (twp->flag & 0x800) {
            if (func_8C0BC61C(twp, mwp, pwp)) break;
        } else {
            if (func_8C0BBDD6(twp, mwp, pwp)) break;
        }
        break;

    case 16:
        if (pwp->mj.reqaction == 34) break;
        if (func_8C0BCC9A(twp, pwp)) break;
        if (pwp->mj.reqaction != 35) {
            func_8C0B70E8(twp, pwp);
        }
        break;

    case 17:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) break;
        if (func_8C0BD83C(twp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            break;
        }
        if (njScalor(&mwp->spd) < pwp->p.slide_speed || pwp->cstsp->tnorm.y > 0.97999997f) {
            twp->mode = 1;
            pwp->waittimer = 0;
            pwp->mj.reqaction = 2;
        }
        break;

    case 36:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (twp->flag & 0x2000) {
            Angle ang;

            if (GetAnalog(twp, &ang, NULL)) {
                if (DiffAngle(twp->ang.y, ang) < 0x4000) {
                    pwp->spd.x = 0.375f;
                    if (pwp->mj.reqaction == 92) {
                        pwp->mj.start_frame = 0.0f;
                    } else {
                        pwp->mj.start_frame = 32.0f;
                    }
                } else {
                    pwp->spd.x = -0.375f;
                    if (pwp->mj.reqaction == 92) {
                        pwp->mj.start_frame = 0.0f;
                    } else {
                        pwp->mj.start_frame = 32.0f;
                    }
                }
                twp->mode = 37;
                pwp->mj.reqaction = 90;
                break;
            }
        } else {
            twp->mode = 10;
            pwp->mj.reqaction = 19;
            twp->flag &= ~0x2000;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) {
            twp->mode = 10;
            pwp->mj.reqaction = 19;
            twp->flag &= ~0x2000;
            break;
        }
        break;

    case 37:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (twp->flag & 0x2000) {
        } else {
            twp->mode = 10;
            pwp->mj.reqaction = 19;
            twp->flag &= ~0x2000;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) {
            twp->mode = 10;
            pwp->mj.reqaction = 19;
            twp->flag &= ~0x2000;
            break;
        }
        break;

    case 38:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (twp->flag & 0x2000) {
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
            twp->mode = 10;
            pwp->mj.reqaction = 19;
            PClearSpeed(mwp, pwp);
            twp->flag &= ~0x2000;
            break;
        }
        break;

    case 39:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (twp->flag & 0x2000) {
        } else {
            twp->mode = 10;
            pwp->mj.reqaction = 19;
            PClearSpeed(mwp, pwp);
            twp->flag &= ~0x2000;
        }
        break;

    case 40:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C05E6C4(twp)) {
            func_8C0BFCA2(twp, 33);
            PClearSpeed(mwp, pwp);
            twp->mode = 41;
            pwp->mj.reqaction = 96;
            break;
        }
        if (twp->flag & 3) {
            func_8C0BFCA2(twp, 33);
            PClearSpeed(mwp, pwp);
            twp->mode = 1;
            pwp->waittimer = 0;
            pwp->mj.reqaction = 2;
        }
        break;

    case 41:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C05E6E8(twp) <= 0) {
            twp->mode = 1;
            pwp->waittimer = 0;
            pwp->mj.reqaction = 2;
            break;
        }
        if (func_8C06CEBE(twp, pwp) == 2) {
            func_8C05E788(twp);
            twp->mode = 40;
            pwp->mj.reqaction = 95;
        }
        break;

    case 42:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (B_8C754E7C[(Uint8)twp->counter.b[0]] & 0x306FE) {
            twp->counter.w[1] -= 20;
        }
        if ((Uint16)twp->counter.w[1] > 0) break;
        func_8C0B70E8(twp, pwp);
        break;

    case 57:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (!(pwp->mj.flag & 1)) break;
        func_8C0B70E8(twp, pwp);
        break;

    case 15:
        if (func_8C0BBDD6(twp, mwp, pwp)) {
            if (twp->timer.b[3] & 2) {
                func_8C01AE60();
            }
            twp->timer.b[2] &= ~1;
            break;
        }
        if (twp->timer.b[3] & 1) {
            twp->mode = 10;
            PClearSpeed(NULL, pwp);
            pwp->mj.reqaction = 19;
            twp->flag &= ~0x400;
            twp->timer.b[2] &= ~1;
        }
        if (twp->flag & 3) {
            if (twp->timer.b[3] & 2) {
                func_8C01AE60();
            }
            twp->timer.b[2] &= ~1;
            func_8C0BFCA2(twp, 33);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 11;
                pwp->mj.reqaction = 21;
                pwp->free.f[6] = 0.79999997f;
                twp->flag &= ~0x400;
            } else if (func_8C0BCD00(twp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 1;
                pwp->free.f[6] = 0.79999997f;
                twp->flag &= ~0x400;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0BB11C(twp, mwp, pwp);
                pwp->free.f[6] = 0.79999997f;
                twp->flag &= ~0x400;
            }
        }
        switch (pwp->mj.reqaction) {
        case 37:
            if (_builtin_fabsf(pwp->spd.y) > 2.0f) {
                pwp->mj.reqaction = 40;
            }
            break;
        case 40:
            if (_builtin_fabsf(pwp->spd.y) < 1.5f) {
                pwp->mj.reqaction = 37;
            }
            break;
        }
        break;

    case 20:
        if (func_8C0BBDD6(twp, mwp, pwp)) {
            twp->timer.b[2] &= ~2;
            break;
        }
        if (pwp->mj.reqaction == 0) {
            twp->mode = 1;
            pwp->mj.reqaction = 0;
            twp->timer.b[2] &= ~2;
            break;
        }
        func_8C0BEBE0(twp, pwp);
        func_8C0BEDA0(twp, mwp, pwp);
        break;

    case 21:
        if (twp->flag & 0x800) {
            if (func_8C0BC61C(twp, mwp, pwp)) break;
        } else {
            if (func_8C0BBDD6(twp, mwp, pwp)) break;
        }
        if (func_8C0BCC9A(twp, pwp)) break;
        if (func_8C0BCEC0(twp, mwp, pwp)) break;
        break;

    case 24:
        if (func_8C0BD336(twp, mwp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) break;
        break;

    case 23:
        if (func_8C0BC61C(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) break;
        if (((Sint32)PER(twp).on & (Sint32)D_8C19E3A0) && GetAnalog(twp, NULL, NULL)
            && pwp->htp->twp->id == 4) {
            twp->mode = 35;
            pwp->mj.reqaction = 75;
            break;
        }
        if (func_8C0BCF56(twp, mwp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 25;
            pwp->mj.reqaction = 63;
        }
        break;

    case 25:
        if (func_8C0BC61C(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp) || func_8C0BBBAA(twp, pwp)
            || func_8C0BCF56(twp, mwp, pwp) || func_8C0BCD00(twp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0BCF56(twp, mwp, pwp)) break;
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 27;
            pwp->mj.reqaction = 60;
            func_8C0BFCA2(twp, 18);
            twp->flag &= ~0x2000;
            break;
        }
        break;

    case 22:
    case 26:
        if (twp->flag & 0x800) {
            if (func_8C0BC61C(twp, mwp, pwp)) break;
        } else {
            if (func_8C0BBDD6(twp, mwp, pwp)) break;
        }
        if (func_8C0BD098(twp, pwp)) break;
        if (pwp->mj.reqaction != 84 && pwp->mj.reqaction != 85 && pwp->mj.reqaction != 88) {
            func_8C0B70E8(twp, pwp);
        }
        break;

    case 27:
        if (func_8C0BC61C(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 25;
            pwp->mj.reqaction = 61;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 60) {
            twp->mode = 23;
            pwp->mj.reqaction = 62;
        }
        break;
    case 28:
        if (func_8C0BC61C(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            func_8C0BFCA2(twp, 33);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 27;
                pwp->mj.reqaction = 60;
            } else if (func_8C0BCD00(twp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 62;
                } else {
                    pwp->mj.reqaction = 83;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 25;
                func_8C0BB11C(twp, mwp, pwp);
            }
            break;
        }
        if (func_8C0BCF56(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
        }
        break;

    case 29:
        if (func_8C0BC61C(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            func_8C0BFCA2(twp, 33);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 27;
                pwp->mj.reqaction = 60;
            } else if (func_8C0BCD00(twp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 62;
                } else {
                    pwp->mj.reqaction = 83;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 25;
                func_8C0BB11C(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C06CEFE(twp, pwp)) {
            FMAC_ACC(pwp->spd.y, pwp->p.jmp_addit, 0.39999998f);
        }
        if (func_8C0BCF56(twp, mwp, pwp)) break;
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 28;
            pwp->mj.reqaction = 81;
        }
        break;

    case 30:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 28;
            pwp->mj.reqaction = 81;
        }
        break;

    case 31:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 28;
            pwp->mj.reqaction = 81;
        }
        break;

    case 32:
        if (func_8C0BBDD6(twp, mwp, pwp)) {
            func_8C0BFD40(twp);
            break;
        }
        if (func_8C0BBBAA(twp, pwp)) {
            func_8C0BFD40(twp);
            break;
        }
        if (func_8C0BCC9A(twp, pwp)) {
            func_8C0BFD40(twp);
            break;
        }
        if (!((Sint32)PER(twp).on & (Sint32)D_8C19E3A0)) {
            func_8C0B70E8(twp, pwp);

            func_8C0BD440(twp, pwp);
            func_8C0BFD40(twp);
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4)
            && !(HITINFO(twp).attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                twp->mode = 12;
                pwp->mj.reqaction = 24;
                func_8C0BFD40(twp);
                break;
            case 0:
                if (twp->flag & 0x800) {
                    twp->mode = 24;
                    pwp->mj.reqaction = 79;
                    func_8C0BFD40(twp);
                }
                break;
            default:
                if (twp->flag & 0x800) {
                    twp->mode = 32;
                    pwp->mj.reqaction = 73;
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 9;
                }
                func_8C0BFD40(twp);
                break;
            }
            break;
        } else {
            func_8C0BD440(twp, pwp);
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            func_8C0BFD40(twp);
            break;
        }
        break;

    case 33:
        if (twp->flag & 0x800) {
            if (func_8C0BC61C(twp, mwp, pwp)) break;
        } else {
            if (func_8C0BBDD6(twp, mwp, pwp)) break;
        }
        if (twp->flag & 3) {
            func_8C0BFCA2(twp, 33);
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 27;
                pwp->mj.reqaction = 60;
            } else if (func_8C0BCD00(twp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (twp->flag & 0x800) {
                    if (mwp->spd.y > -5.0f) {
                        pwp->mj.reqaction = 62;
                    } else {
                        pwp->mj.reqaction = 83;
                    }
                } else if (mwp->spd.y > -5.0f) {
                    func_8C0BAFC0(twp, pwp);
                } else {
                    pwp->mj.reqaction = 20;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (twp->flag & 0x800) {
                    twp->mode = 25;
                } else {
                    twp->mode = 2;
                }
                func_8C0BB11C(twp, mwp, pwp);
            }
            break;
        }
        if (func_8C0BD098(twp, pwp)) break;
        if (pwp->mj.reqaction != 86 && pwp->mj.reqaction != 87) {
            twp->mode = 10;
            pwp->mj.reqaction = 19;
        }
        break;

    case 34:
        if (func_8C0BC61C(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) break;
        if (func_8C06CA30(twp, mwp, pwp)) {
            if (!GetAnalog(twp, NULL, NULL)) {
                twp->mode = 24;
                pwp->mj.reqaction = 79;
                break;
            }
            break;
        }
        func_8C0B70E8(twp, pwp);
        func_8C0BD440(twp, pwp);
        break;

    case 35:
        if (func_8C0BC61C(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) break;
        if (func_8C0BBBAA(twp, pwp)) break;
        if (pwp->htp == NULL || pwp->htp->twp->id != 4) {
            func_8C0B70E8(twp, pwp);
            func_8C0BD440(twp, pwp);
            break;
        }
        if (!GetAnalog(twp, NULL, NULL)) {
            twp->mode = 23;
            pwp->mj.reqaction = 77;
            break;
        }
        break;

    case 43:
        if ((twp->flag & 0x1000) && twp->smode == 28) {
            twp->mode = 7;
            pwp->mj.reqaction = 17;
            twp->flag &= ~0x2500;
        }
        break;

    case 44:
        if (func_8C0BBDD6(twp, mwp, pwp) || func_8C0BBBAA(twp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if ((PER(twp).x1 << 8) < -0xC00) {
            twp->mode = 46;
            pwp->mj.reqaction = 103;
            break;
        }
        if ((PER(twp).x1 << 8) > 0xC00) {
            twp->mode = 47;
            pwp->mj.reqaction = 104;
            break;
        }
        if (twp->mode < 44 || twp->mode > 47) {
            pwp->htp = NULL;
        }
        break;

    case 45:
        if (func_8C0BBDD6(twp, mwp, pwp) || func_8C0BBBAA(twp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if (pwp->htp != NULL) {
            taskwk *htwp;

            htwp = pwp->htp->twp;
            if (htwp->pos.y + CWP(htwp)->info->center.y - CWP(htwp)->info->b >= twp->pos.y) {
                twp->mode = 44;
                pwp->mj.reqaction = 106;
                break;
            }
        } else {
            func_8C0B70E8(twp, pwp);
            break;
        }
        if (twp->mode < 44 || twp->mode > 47) {
            pwp->htp = NULL;
        }
        break;

    case 46:
        if (func_8C0BBDD6(twp, mwp, pwp) || func_8C0BBBAA(twp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if ((PER(twp).x1 << 8) > -0xC00) {
            twp->mode = 44;
            pwp->mj.reqaction = 102;
            break;
        }
        if (twp->mode < 44 || twp->mode > 47) {
            pwp->htp = NULL;
        }
        break;

    case 47:
        if (func_8C0BBDD6(twp, mwp, pwp) || func_8C0BBBAA(twp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if ((PER(twp).x1 << 8) < 0xC00) {
            twp->mode = 44;
            pwp->mj.reqaction = 102;
            break;
        }
        if (twp->mode < 44 || twp->mode > 47) {
            pwp->htp = NULL;
        }
        break;
    case 48:
        if (func_8C0BCBCA(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) {
            twp->mode = 51;
            pwp->mj.reqaction = 116;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E390) {
            twp->mode = 52;
            pwp->mj.reqaction = 111;
            pwp->spd.y = pwp->p.jmp_y_spd;
            if (pwp->spd.x < pwp->p.rush_speed) {
                pwp->spd.x += pwp->p.jog_speed;
            }
            if (pwp->ttp != NULL) {
                sa1_forcewk *fwp;

                fwp = (sa1_forcewk *)pwp->ttp->fwp;
                if (fwp != NULL) {
                    SetAccelerationP(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
                }
                if ((Uint8)pwp->ttp->twp->id == 9) {
                    twp->mode = 53;
                    func_8C0BFCA2(twp, 230);
                    {
                        NJS_VECTOR v;
                        NJS_VECTOR p;
                        taskwk *ttwp;
                        Angle angx;
                        Angle angy;
                        Angle angz;
                        NJS_VECTOR q;

                        ttwp = pwp->ttp->twp;
                        v.x = 0.0f;
                        v.y = 1.4f;
                        v.z = 3.0f;
                        njPushMatrix(D_8C67C640);
                        ttwp->ang.z ? njRotateZ(NULL, ttwp->ang.z) : (void)0;
                        ttwp->ang.y ? njRotateY(NULL, ttwp->ang.y) : (void)0;
                        ttwp->ang.x ? njRotateX(NULL, ttwp->ang.x) : (void)0;
                        njRotateY(NULL, 0x8000);
                        njCalcVector(NULL, &v, &p);
                        v.z = 0.0f;
                        v.x = 0.0f;
                        v.y = 1.0f;
                        njCalcVector(NULL, &v, &q);
                        angy = SA1_FTOI((const Float)(atan2f(p.x, p.z) * 65536.0f) * 0.159154985f);
                        angx = SA1_FTOI((const Float)(func_8C10D058(q.z) * 65536.0f) * 0.159154985f);
                        angz = -SA1_FTOI((const Float)(func_8C10D058(q.x) * 65536.0f) * 0.159154985f);
                        func_8C07C9D2(twp->counter.b[0], angx, angy, angz);
                        njPopMatrix(1);
                        SetAccelerationP(twp->counter.b[0], p.x, p.y, p.z);
                    }
                    if (pwp->spd.x > 4.1399995f) {
                        if ((Float)rand() * 3.0517578125e-05f > 0.65999998f) {
                            pwp->mj.reqaction = 121;
                            func_8C0BFCA2(twp, 0x542);
                        } else {
                            pwp->mj.reqaction = 122;
                            func_8C0BFCA2(twp, 0x543);
                        }
                        if ((Float)rand() * 3.0517578125e-05f >= 0.5f) {
                            func_8C01AD2C(func_8C01FB98, 0, 2);
                            D_8C112048[0x48 / 4] = 0.0f;
                        } else {
                            func_8C01AD2C(func_8C01FD6C, 0, 2);
                            D_8C112048[0x48 / 4] = 0.0f;
                        }
                        func_8C0B6120(100);
                    } else if (pwp->spd.x > 2.7599998f) {
                        pwp->mj.reqaction = 123;
                        func_8C0BFCA2(twp, 0x550);
                        func_8C0B6120(50);
                    } else if (pwp->spd.x > 1.8399998f) {
                        pwp->mj.reqaction = 126;
                        func_8C0BFCA2(twp, 0x551);
                        func_8C0B6120(30);
                    } else {
                        twp->mode = 52;
                        pwp->mj.reqaction = 111;
                        func_8C0B6120(10);
                    }
                }
            }
            twp->flag &= ~2;
            pwp->jumptimer = 0;
            func_8C07BA0A(231, twp->counter.b[0], twp->mode);
            break;
        }
        if (func_8C06CE40(twp, mwp) && !(pwp->spd.x < pwp->p.jog_speed)) {
            if (!(pwp->spd.x < pwp->p.jog_speed)) {
                Angle ang;

                twp->mode = 50;
                GetAnalog(twp, &ang, NULL);
                if ((ang = SubAngle(twp->ang.y, ang)) > 0) {
                    pwp->mj.reqaction = 108;
                } else {
                    pwp->mj.reqaction = 109;
                }
            }
        }
        break;

    case 50:
        if (func_8C0BCBCA(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) {
            twp->mode = 51;
            pwp->mj.reqaction = 116;
            break;
        }
        if ((Sint32)PER(twp).release & (Sint32)D_8C19E390) {
            twp->mode = 52;
            pwp->spd.y = pwp->p.jmp_y_spd;
            if (pwp->spd.x < pwp->p.rush_speed) {
                pwp->spd.x += pwp->p.jog_speed;
            }
            if (pwp->ttp != NULL) {
                sa1_forcewk *fwp;

                fwp = (sa1_forcewk *)pwp->ttp->fwp;
                if (fwp != NULL) {
                    SetAccelerationP(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
                }
            }
            pwp->mj.reqaction = 111;
            twp->flag &= ~2;
            pwp->jumptimer = 0;
            func_8C0BFCA2(twp, 17);
            break;
        }
        if (!func_8C06CE40(twp, mwp) || pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 48;
            pwp->mj.reqaction = 107;
            func_8C0BBA4E(twp, pwp);
            break;
        }
        {
            Angle ang;

            GetAnalog(twp, &ang, NULL);
            if ((ang = SubAngle(twp->ang.y, ang)) > 0) {
                pwp->mj.reqaction = 108;
            } else {
                pwp->mj.reqaction = 109;
            }
        }
        break;

    case 51:
        if (func_8C0BCBCA(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            func_8C0BFCA2(twp, 33);
            twp->mode = 48;
            if (pwp->spd.y <= -3.0f) {
                pwp->mj.reqaction = 118;
            } else {
                pwp->mj.reqaction = 107;
                if (pwp->spd.x <= 0.29999999f) {
                    pwp->spd.x = 1.0f;
                }
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0BBA4E(twp, pwp);
        }
        break;

    case 52:
        if (func_8C0BCBCA(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            twp->mode = 48;
            if (pwp->spd.y <= -3.0f) {
                pwp->mj.reqaction = 118;
            } else {
                pwp->mj.reqaction = 107;
                if (pwp->spd.x <= 0.29999999f) {
                    pwp->spd.x = 1.0f;
                }
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0BBA4E(twp, pwp);
            break;
        }
        break;

    case 53:
        if (func_8C0BCBCA(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            twp->mode = 48;
            if (pwp->spd.y <= -3.0f) {
                pwp->mj.reqaction = 118;
            } else {
                pwp->mj.reqaction = 107;
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0BBA4E(twp, pwp);
            break;
        }
        break;

    case 54:
        if (func_8C0BCBCA(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 114 && pwp->mj.reqaction != 115) {
            twp->mode = 48;
            pwp->mj.reqaction = 107;
            func_8C0BBA4E(twp, pwp);
        }
        break;

    case 55:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (func_8C0BCC9A(twp, pwp)) {
            twp->mode = 56;
        }
        break;

    case 56:
        if (func_8C0BBDD6(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            func_8C0BFCA2(twp, 33);
            twp->mode = 55;
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            break;
        }
        break;

    case 61:
        if (twp->flag & 0x800) {
            if (func_8C0BC61C(twp, mwp, pwp)) break;
        } else {
            if (func_8C0BBDD6(twp, mwp, pwp)) break;
        }
        break;

    case 58:
        if (EWP(twp)->list4C == NULL) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0B70E8(twp, pwp);
        }
        break;

    case 62:
        break;
    }
}
#else
ASM_STUB(void TailsModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif