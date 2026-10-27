#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0A966C: SonicModeMachine (chunk 8C0A9660-8C0AE760, function 2/8).
 *
 * Sonic's action state machine, called once per frame by SonicMain
 * (8C0A77BC): first the "hurt / invincible" timer on twp->wtimer and the
 * damage check (func_8C0AFB10), then a switch on twp->mode (1..83) that
 * decides the next mode and the requested animation (pwp->mj.reqaction)
 * from the input checks (func_8C0AFF1A, func_8C0B1352, ... return non-zero
 * when they changed the mode themselves).
 *
 * The function is so large that SHC compiles it without register
 * allocation: the three parameters live at @(64/60/56,R15) and every block
 * local in the frame below them; identical tails are cross-jumped.
 *
 * Status: the NON_MATCHING body below compiles to the original's exact
 * instruction stream (8122 instructions, all encodings, branch targets and
 * literal-pool values equal). shcdiff still reports one difference because
 * it cannot evaluate the switch table entries (".DATA.W Lx-Ly" label
 * differences) behind the MOVA at 8C0A978E; all 83 table entries were
 * checked separately and reach the same instructions as the original. The
 * body stays under NON_MATCHING only until shcdiff/splice handle switch
 * tables (the splicer refuses anything shcdiff does not call MATCH).
 *
 * Source idioms that the register state and folding depend on:
 * - "if (c) { ...; break; } else { ...; break; }" and "if (c) { ...; break; }
 *   break;" instead of "if (!c) break; ..." (SHC's temporary-register choice
 *   after a block depends on how the far branch to the end was formed);
 * - ROTATE as "a ? njRotateZ(NULL, a) : (void)0" (expression statements);
 * - "(const Float)(x * 65536.0f) * k": the const-qualified cast stops SHC
 *   from folding the two constant multiplies into one (65536 * k), which the
 *   original keeps as two FMULs. Bit-identical either way (power-of-two
 *   scale), so the PC build is unaffected.
 */

extern Uint32 D_8C19E38C;       /* jump button mask */
extern Uint32 D_8C19E390;       /* action button mask */
extern Uint32 D_8C19E3A0;       /* button mask */
extern Uint32 B_8C754E7C[];     /* per-player input flags (mash detection) */
extern taskwk *D_8C18B7EC;      /* camera taskwk */
extern NJS_MATRIX D_8C67C640;
extern Float *D_8C112048;       /* reads [0x48 / 4] */

/* Sonic action handlers (same chunk family 8C0AEFA0-8C0B1D60) */
extern Sint32 func_8C0AFB10(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern void func_8C0B1D60(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0AFF1A(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B1352(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B11C0(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0AFC98(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B16BC(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B12E0(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B122E(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B14E0(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B1686(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0AFE20(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B0F24(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B07E4(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B17AE(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B1C56(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B184C(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B199A(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C0B0E94(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern taskwk *func_8C0AF5A0(taskwk *twp, playerwk *pwp);
extern void func_8C0AEFA0(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern void func_8C0AF18C(taskwk *twp, motionwk *mwp, playerwk *pwp);
/* same file, reached with JSR (out of BSR range) */
extern void func_8C0AE3D6(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern void func_8C0AE588(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* player common; the call sites pass more arguments than the matched
 * definitions use (code_8C06CCA0.c, code_8C06CFCC.c, code_8C06CA30.c) */
extern Bool func_8C06CE40(taskwk *twp, motionwk *mwp);
extern Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp);
extern Bool func_8C06CEFE(taskwk *twp, playerwk *pwp);
extern Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);
extern Sint32 func_8C06CA30(taskwk *twp, motionwk *mwp, playerwk *pwp);
extern Sint32 func_8C05E6C4(taskwk *twp);
extern Sint32 func_8C05E6E8(taskwk *twp);
extern void func_8C05E788(taskwk *twp);
extern void func_8C07B2DC(Uint8 pno);
extern void func_8C07C9D2(Uint8 pno, Angle angx, Angle angy, Angle angz);
extern void func_8C07BA0A(Sint32 se, Sint8 pno, Sint16 mode);
extern void func_8C086F5E(Sint8 flag);
extern void func_8C0945A0(Sint32 se);
extern void func_8C0A77A0(Sint32 n);
extern void func_8C01AD2C(void (*func)(void), Sint8 adjust, Sint8 direct);
extern void func_8C01AE60(void);
extern void func_8C01FB98(void);
extern void func_8C01FD6C(void);

/*
 * func_8C0A9660 is decompiled in code_8C0A9660.c. The matching build needs
 * its real 12-byte body here (not a large ASM_STUB): SHC emits the BSR from
 * case 13 only while the callee is within BSR range, and the BSR keeps that
 * tail out of the cross-jumping. The splicer skips this copy because of the
 * name below:
 *   ASM_STUB(void func_8C0A9660(taskwk *twp, motionwk *mwp, playerwk *pwp))
 */
#if defined(TARGET_PC)
void func_8C0A9660(taskwk *twp, motionwk *mwp, playerwk *pwp);
#else
void func_8C0A9660(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    twp->mode = 1;
    pwp->waittimer = 0;
    func_8C0AE3D6(twp, mwp, pwp);
}
#endif
void func_8C0AE6D8(taskwk *twp, motionwk *mwp, playerwk *pwp);

#define PER(twp) (B_8C754C08[(Uint8)(twp)->counter.b[0]])

/* "acc += a * b" compiled to FMAC: fused on PC (SA1_FMAC); SA1_FMAC itself
 * (a * b + acc) changes the SHC output here */
#if defined(TARGET_PC)
#define FMAC_ACC(acc, a, b) ((acc) = SA1_FMAC((a), (b), (acc)))
#else
#define FMAC_ACC(acc, a, b) ((acc) += (a) * (b))
#endif

#ifdef NON_MATCHING
void SonicModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp != NULL && (pwp->item & 0x8000)) {
        twp->flag |= 0x8000;
        twp->wtimer = 0;
    } else if (twp->wtimer == 0) {
        if (func_8C0AFB10(twp, mwp, pwp)) {
            twp->flag |= 0x8000;
            if (twp->flag & 0x800) {
                func_8C0B1D60(twp, mwp, pwp);
            }
            twp->flag &= ~0x3B00;
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
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) break;
        if (func_8C0AFC98(twp, mwp, pwp)) break;
        if (func_8C0B16BC(twp, mwp, pwp)) break;
        if (func_8C0B12E0(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            break;
        } else if (pwp->cstsp->tnorm.y < 0.77f && njScalor(&pwp->spd) > pwp->p.slide_speed) {
            twp->mode = 19;
            pwp->mj.reqaction = 31;
        }
        break;

    case 2:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp) || func_8C0AFC98(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0B16BC(twp, mwp, pwp)) break;
        if (func_8C0B12E0(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0B122E(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4)
            && !(CWP(twp)->hit_cwp->info[CWP(twp)->hit_num].attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                twp->mode = 15;
                pwp->mj.reqaction = 21;
                twp->flag &= ~0x2000;
            }
        }
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 13;
            pwp->mj.reqaction = 20;
            func_8C0940DA(18, 0, 0, 0);
            twp->flag &= ~0x2000;
        }
        break;

    case 3:
        twp->flag &= ~0x500;
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp) || func_8C0B11C0(twp, mwp, pwp) || func_8C0AFC98(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0B122E(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
        }
        break;

    case 4:
        if (func_8C0AFF1A(twp, mwp, pwp)) {
            func_8C0945A0(0x2D8);
            func_8C0945A0(0x2D4);
            break;
        }
        if (func_8C0B11C0(twp, mwp, pwp)) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 5.0f;
            twp->flag |= 0x500;
            func_8C0945A0(0x2D8);
            func_8C0945A0(0x2D4);
            break;
        }
        if (func_8C0B14E0(twp, mwp, pwp)) break;
        break;

    case 5:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) {
            pwp->mj.reqaction = 14;
            pwp->work.f = 5.0f;
            twp->flag |= 0x500;
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0AFC98(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0B1686(twp, mwp, pwp)) {
            twp->flag &= ~0x500;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) {
            twp->mode = 2;
            pwp->mj.reqaction = 29;
            twp->flag &= ~0x500;
            break;
        }
        break;

    case 6:
        if (func_8C0AFF1A(twp, mwp, pwp)) {
            pwp->free.w[2] = 0;
            func_8C0945A0(0x2D5);
            if (pwp->spd.x > 2.0f) pwp->spd.x = 2.0f;
            twp->flag &= ~0x400;
            break;
        }
        if (pwp->free.w[2]-- <= 0 || pwp->free.w[3]++ > 360) {
            twp->mode = 13;
            pwp->mj.reqaction = 20;
            if (pwp->spd.x > 2.0f) pwp->spd.x = 2.0f;
            twp->flag &= ~0x400;
            pwp->free.w[2] = 0;
            func_8C0945A0(0x2D5);
            break;
        } else {
            colliwk *hit;
            task *htp;

            hit = CWP(twp)->hit_cwp;
            if (hit != NULL) {
                htp = hit->mytask;
                if (htp != NULL && (Uint8)htp->twp->id == 11) {
                    pwp->free.w[3] = 0;
                }
            }
        }
        {
            NJS_POINT3 pos;

            GetPlayerPosition(twp->counter.b[0], 1, &pos, NULL);
            if (njDistanceP2P(&pos, &twp->pos) <= 1.5f) {
                twp->mode = 13;
                pwp->mj.reqaction = 20;
                if (pwp->spd.x > 2.0f) pwp->spd.x = 2.0f;
                twp->flag &= ~0x400;
                pwp->free.w[2] = 0;
                func_8C0945A0(0x2D5);
                break;
            }
        }
        break;

    case 7:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) {
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

    case 8:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 13;
                pwp->mj.reqaction = 20;
            } else if (func_8C0B122E(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 2;
                } else {
                    pwp->mj.reqaction = 19;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0AE588(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C0AFE20(twp, mwp, pwp)) break;
        if (func_8C06CEBE(twp, pwp)) {
            twp->mode = 14;
            func_8C0AF18C(twp, mwp, pwp);
            break;
        }
        if (func_8C06CEFE(twp, pwp)) {
            FMAC_ACC(pwp->spd.y, pwp->p.jmp_addit, 0.79999997f);
        }
        break;

    case 9:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) break;
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 12;
            if (pwp->mj.reqaction == 39) {
                pwp->mj.reqaction = 40;
            } else if (pwp->mj.reqaction == 41) {
                pwp->mj.reqaction = 42;
            } else {
                pwp->mj.reqaction = 17;
            }
        }
        break;

    case 10:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) break;
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 12;
            pwp->mj.reqaction = 17;
        }
        break;

    case 11:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 13;
                pwp->mj.reqaction = 20;
            } else if (func_8C0B122E(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 2;
                } else {
                    pwp->mj.reqaction = 19;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0AE588(twp, mwp, pwp);
            }
        }
        break;

    case 12:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 13;
                pwp->mj.reqaction = 20;
            } else if (func_8C0B122E(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 1;
                } else {
                    pwp->mj.reqaction = 19;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0AE588(twp, mwp, pwp);
            }
        }
        break;

    case 13:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) break;
        if (func_8C0AFC98(twp, mwp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 2;
            pwp->mj.reqaction = 30;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 20) {
            func_8C0A9660(twp, mwp, pwp);
        }
        break;

    case 14:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        {
            colliwk *hit;

            hit = CWP(twp)->hit_cwp;
            if (pwp->free.w[3]++ >= 360
                || (hit != NULL && (hit->id == 0 || hit->id == 2 || hit->id == 3))) {
                pwp->free.w[2] = -1;
                twp->mode = 8;
                break;
            }
        }
        if (pwp->free.w[2] >= 5.99999964f || ((Sint32)PER(twp).press & (Sint32)D_8C19E38C)) {
            pwp->free.w[2] = 0;
            twp->mode = 12;
            pwp->mj.reqaction = 18;
            twp->flag &= ~0x500;
            break;
        }
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            twp->flag &= ~0x500;
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 13;
                pwp->mj.reqaction = 20;
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
            } else if (func_8C0B122E(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 2;
                } else {
                    pwp->mj.reqaction = 19;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0AE588(twp, mwp, pwp);
            }
        }
        break;

    case 15:
        if (func_8C0AFF1A(twp, mwp, pwp) || func_8C0B1352(twp, mwp, pwp) || func_8C0AFC98(twp, mwp, pwp)
            || func_8C0B11C0(twp, mwp, pwp) || func_8C0B122E(twp, mwp, pwp)) {
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
                    twp->mode = 32;
                    pwp->mj.reqaction = 48;
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 9;
                }
                func_8C0945A0(15);
                break;
            }
            break;
        }
        func_8C0B1D60(twp, mwp, pwp);
        twp->mode = 2;
        pwp->mj.reqaction = 9;
        func_8C0945A0(15);
        break;

    case 16:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 23 && pwp->mj.reqaction != 24) {
            if (func_8C0B122E(twp, mwp, pwp)) {
                func_8C0A9660(twp, mwp, pwp);
                break;
            } else {
                twp->mode = 2;
                func_8C0AE588(twp, mwp, pwp);
                break;
            }
        } else
        if (twp->wtimer <= 0) {
            if (twp->flag & 3) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0AE588(twp, mwp, pwp);
                break;
            } else {
                twp->mode = 12;
                pwp->mj.reqaction = 18;
            }
            break;
        }
        if (twp->wtimer <= 60 && GetAnalog(twp, NULL, NULL)) {
            if (func_8C0AFC98(twp, mwp, pwp)) break;
            if (twp->flag & 3) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0AE588(twp, mwp, pwp);
                break;
            }
            break;
        }
        if (twp->wtimer == 120) break;
        if (twp->flag & 3) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            pwp->mj.reqaction = 24;
        }
        break;

    case 17:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) break;
        if (func_8C0AFC98(twp, mwp, pwp)) break;
        if (func_8C0B122E(twp, mwp, pwp)) break;
        if (!(pwp->spd.x < 0.0f)) {
            twp->mode = 2;
            func_8C0AE588(twp, mwp, pwp);
            break;
        }
        break;

    case 20:
        if (pwp->equipment & 0x8000) {
            if (func_8C0B0F24(twp, mwp, pwp)) break;
        } else if (twp->flag & 0x800) {
            if (func_8C0B07E4(twp, mwp, pwp)) break;
        } else {
            if (func_8C0AFF1A(twp, mwp, pwp)) break;
        }
        break;

    case 18:
        if (pwp->mj.reqaction == 27) break;
        if (func_8C0B11C0(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 28) {
            func_8C0A9660(twp, mwp, pwp);
        }
        break;

    case 19:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) break;
        if (func_8C0AFC98(twp, mwp, pwp)) break;
        if (func_8C0B12E0(twp, mwp, pwp)) break;
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
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp) || func_8C0AFC98(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (twp->flag & 0x2000) {
            Angle ang;

            if (GetAnalog(twp, &ang, NULL)) {
                if (DiffAngle(twp->ang.y, ang) < 0x4000) {
                    pwp->spd.x = 0.375f;
                    if (pwp->mj.reqaction == 68) {
                        pwp->mj.start_frame = 0.0f;
                    } else {
                        pwp->mj.start_frame = 32.0f;
                    }
                } else {
                    pwp->spd.x = -0.375f;
                    if (pwp->mj.reqaction == 68) {
                        pwp->mj.start_frame = 0.0f;
                    } else {
                        pwp->mj.start_frame = 32.0f;
                    }
                }
                twp->mode = 37;
                pwp->mj.reqaction = 66;
                break;
            }
        } else {
            twp->mode = 12;
            pwp->mj.reqaction = 18;
            twp->flag &= ~0x2000;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) {
            twp->mode = 12;
            pwp->mj.reqaction = 18;
            twp->flag &= ~0x2000;
            break;
        }
        break;

    case 37:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp) || func_8C0AFC98(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (twp->flag & 0x2000) {
        } else {
            twp->mode = 12;
            pwp->mj.reqaction = 18;
            twp->flag &= ~0x2000;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) {
            twp->mode = 12;
            pwp->mj.reqaction = 18;
            twp->flag &= ~0x2000;
            break;
        }
        break;

    case 38:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0AFC98(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (twp->flag & 0x2000) {
            Angle ang;

            if (GetAnalog(twp, &ang, NULL)) {
                if (DiffAngle(0xC000 - D_8C18B7EC->ang.y, ang) < 0x4000) {
                    pwp->spd.x = 0.375f;
                    break;
                } else {
                    pwp->spd.x = -0.375f;
                    break;
                }
            }
            break;
        } else {
            twp->mode = 12;
            pwp->mj.reqaction = 18;
            PClearSpeed(mwp, pwp);
            twp->flag &= ~0x2000;
            break;
        }
        break;

    case 39:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0AFC98(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (twp->flag & 0x2000) {
        } else {
            twp->mode = 12;
            pwp->mj.reqaction = 18;
            PClearSpeed(mwp, pwp);
            twp->flag &= ~0x2000;
        }
        break;

    case 40:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C05E6C4(twp)) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            PClearSpeed(mwp, pwp);
            twp->mode = 41;
            pwp->mj.reqaction = 74;
            break;
        }
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            PClearSpeed(mwp, pwp);
            twp->mode = 1;
            pwp->waittimer = 0;
            pwp->mj.reqaction = 2;
        }
        break;

    case 41:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C05E6E8(twp) <= 0) {
            twp->mode = 1;
            pwp->waittimer = 0;
            pwp->mj.reqaction = 2;
            break;
        }
        if (func_8C06CEBE(twp, pwp) == 2) {
            func_8C05E788(twp);
            twp->mode = 40;
            pwp->mj.reqaction = 73;
            func_8C0940DA(17, 0, 0, 0);
        }
        break;

    case 42:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B1352(twp, mwp, pwp)) break;
        if (B_8C754E7C[(Uint8)twp->counter.b[0]] & 0x306FE) {
            pwp->free.w[3] -= 20;
        }
        if (pwp->free.w[3] > 0) break;
        func_8C0A9660(twp, mwp, pwp);
        break;

    case 43:
        if (func_8C0AFF1A(twp, mwp, pwp) || func_8C0B1352(twp, mwp, pwp)) {
            func_8C07B2DC(twp->counter.b[0]);
            func_8C0940DA(16, 0, 0, 0);
            break;
        }
        if (B_8C754E7C[(Uint8)twp->counter.b[0]] & 0x306FE) {
            pwp->free.w[3] -= 20;
        } else {
            pwp->free.w[3] -= 1;
        }
        if (pwp->free.w[3] <= 0) {
            twp->mode = 8;
            pwp->spd.y = pwp->p.jmp_y_spd;
            pwp->mj.reqaction = 14;
            pwp->work.f = 5.0f;
            twp->flag &= ~2;
            twp->flag |= 0x500;
            pwp->jumptimer = 0;
            func_8C0940DA(17, 0, 0, 0);
            func_8C0940DA(16, 0, 0, 0);
            func_8C07B2DC(twp->counter.b[0]);
        }
        break;

    case 79:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (!(pwp->mj.flag & 1)) break;
        func_8C0A9660(twp, mwp, pwp);
        break;

    case 44:
        if (!func_8C0AF5A0(twp, pwp) || ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) || pwp->free.w[3]++ > 360) {
            twp->mode = 12;
            pwp->mj.reqaction = 18;
            twp->flag &= ~0x500;
            {
                Float spd;

                spd = pwp->p.jmp_y_spd;
                mwp->spd.x = Gravity.x * spd;
                mwp->spd.y = Gravity.y * spd;
                mwp->spd.z = Gravity.z * spd;
                pwp->spd.x = 0.0f;
                pwp->spd.y = spd;
                pwp->spd.z = 0.0f;
            }
            func_8C01AE60();
            break;
        }
        if ((CWP(twp)->flag & 2) && (CWP(twp)->hit_cwp->id == 2 || CWP(twp)->hit_cwp->id == 3)) {
        }
        break;

    case 21:
        if (twp->flag & 0x800) {
            if (func_8C0B07E4(twp, mwp, pwp)) break;
        } else {
            if (func_8C0AFF1A(twp, mwp, pwp)) break;
        }
        if (func_8C0B11C0(twp, mwp, pwp)) break;
        if (func_8C0B17AE(twp, mwp, pwp)) break;
        break;

    case 24:
        if (func_8C0B1C56(twp, mwp, pwp)) break;
        if (func_8C0AFC98(twp, mwp, pwp)) break;
        break;

    case 23:
        if (func_8C0B07E4(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) break;
        if (((Sint32)PER(twp).on & (Sint32)D_8C19E3A0) && GetAnalog(twp, NULL, NULL) && (Uint8)pwp->htp->twp->id == 4) {
            twp->mode = 35;
            pwp->mj.reqaction = 50;
            break;
        }
        if (func_8C0B184C(twp, mwp, pwp)) break;
        if (func_8C0AFC98(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 25;
            pwp->mj.reqaction = 38;
        }
        break;

    case 25:
        if (func_8C0B07E4(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp) || func_8C0AFC98(twp, mwp, pwp) || func_8C0B184C(twp, mwp, pwp)
            || func_8C0B122E(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0B184C(twp, mwp, pwp)) break;
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 27;
            pwp->mj.reqaction = 35;
            func_8C0940DA(18, 0, 0, 0);
            twp->flag &= ~0x2000;
            break;
        }
        break;

    case 22:
    case 26:
        if (twp->flag & 0x800) {
            if (func_8C0B07E4(twp, mwp, pwp)) break;
        } else {
            if (func_8C0AFF1A(twp, mwp, pwp)) break;
        }
        if (func_8C0B199A(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 59 && pwp->mj.reqaction != 60 && pwp->mj.reqaction != 63) {
            func_8C0A9660(twp, mwp, pwp);
        }
        break;

    case 27:
        if (func_8C0B07E4(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) break;
        if (func_8C0AFC98(twp, mwp, pwp)) break;
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 25;
            pwp->mj.reqaction = 36;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 35) {
            twp->mode = 23;
            pwp->mj.reqaction = 37;
        }
        break;

    case 28:
        if (func_8C0B07E4(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 27;
                pwp->mj.reqaction = 35;
            } else if (func_8C0B122E(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 37;
                } else {
                    pwp->mj.reqaction = 58;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 25;
                func_8C0AE588(twp, mwp, pwp);
            }
        } else if (func_8C0B184C(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
        }
        break;

    case 29:
        if (func_8C0B07E4(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 27;
                pwp->mj.reqaction = 35;
            } else if (func_8C0B122E(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (mwp->spd.y > -5.0f) {
                    pwp->mj.reqaction = 37;
                } else {
                    pwp->mj.reqaction = 58;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 25;
                func_8C0AE588(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C06CEFE(twp, pwp)) {
            FMAC_ACC(pwp->spd.y, pwp->p.jmp_addit, 0.39999998f);
        }
        if (func_8C0B184C(twp, mwp, pwp)) break;
        if (!(mwp->spd.y > 0.0f)) {
            twp->mode = 28;
            pwp->mj.reqaction = 56;
        }
        break;

    case 30:
        if (!(mwp->spd.y > 0.0f)) {
            twp->mode = 28;
            pwp->mj.reqaction = 56;
        }
        break;

    case 31:
        if (!(mwp->spd.y > 0.0f)) {
            twp->mode = 28;
            pwp->mj.reqaction = 56;
        }
        break;

    case 32:
        if (func_8C0AFF1A(twp, mwp, pwp) || func_8C0AFC98(twp, mwp, pwp) || func_8C0B11C0(twp, mwp, pwp)) {
            func_8C0945A0(15);
            break;
        }
        if (!((Sint32)PER(twp).on & (Sint32)D_8C19E3A0)) {
            func_8C0A9660(twp, mwp, pwp);
            func_8C0B1D60(twp, mwp, pwp);
            func_8C0945A0(15);
        }
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4)
            && !(CWP(twp)->hit_cwp->info[CWP(twp)->hit_num].attr & 0x800)
            && (CWP(twp)->flag & 8)) {
            switch (func_8C06CFCC(twp, mwp)) {
            case 4:
                twp->mode = 15;
                pwp->mj.reqaction = 21;
                func_8C0945A0(15);
                break;
            case 0:
                if (twp->flag & 0x800) {
                    twp->mode = 24;
                    pwp->mj.reqaction = 54;
                    func_8C0945A0(15);
                }
                break;
            default:
                if (twp->flag & 0x800) {
                    twp->mode = 32;
                    pwp->mj.reqaction = 48;
                } else {
                    twp->mode = 2;
                    pwp->mj.reqaction = 9;
                }
                func_8C0945A0(15);
                break;
            }
            break;
        } else {
            func_8C0B1D60(twp, mwp, pwp);
            twp->mode = 2;
            pwp->mj.reqaction = 9;
            func_8C0945A0(15);
            break;
        }
        break;

    case 33:
        if (twp->flag & 0x800) {
            if (func_8C0B07E4(twp, mwp, pwp)) break;
        } else {
            if (func_8C0AFF1A(twp, mwp, pwp)) break;
        }
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 27;
                pwp->mj.reqaction = 35;
            } else if (func_8C0B122E(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (twp->flag & 0x800) {
                    if (mwp->spd.y > -5.0f) {
                        pwp->mj.reqaction = 37;
                    } else {
                        pwp->mj.reqaction = 58;
                    }
                } else if (mwp->spd.y > -5.0f) {
                    func_8C0AE3D6(twp, mwp, pwp);
                } else {
                    pwp->mj.reqaction = 19;
                }
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                if (twp->flag & 0x800) {
                    twp->mode = 25;
                } else {
                    twp->mode = 2;
                }
                func_8C0AE588(twp, mwp, pwp);
            }
        } else {
            if (func_8C0B199A(twp, mwp, pwp)) break;
            if (pwp->mj.reqaction != 61 && pwp->mj.reqaction != 62) {
                twp->mode = 12;
                pwp->mj.reqaction = 18;
            }
        }
        break;

    case 34:
        if (func_8C0B07E4(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) break;
        if (func_8C0AFC98(twp, mwp, pwp)) break;
        if (func_8C06CA30(twp, mwp, pwp)) {
            if (!GetAnalog(twp, NULL, NULL)) {
                twp->mode = 24;
                pwp->mj.reqaction = 54;
            }
            break;
        }
        func_8C0A9660(twp, mwp, pwp);
        func_8C0B1D60(twp, mwp, pwp);
        break;

    case 35:
        if (func_8C0B07E4(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) break;
        if (func_8C0AFC98(twp, mwp, pwp)) break;
        if (pwp->htp == NULL || (Uint8)pwp->htp->twp->id != 4) {
            func_8C0A9660(twp, mwp, pwp);
            func_8C0B1D60(twp, mwp, pwp);
            break;
        }
        if (!GetAnalog(twp, NULL, NULL)) {
            twp->mode = 23;
            pwp->mj.reqaction = 52;
            break;
        }
        break;

    case 45:
        if (twp->flag & 0x1000) {
            if (twp->smode == 28) {
                twp->mode = 9;
                pwp->mj.reqaction = 16;
                twp->flag &= ~0x2500;
            } else if (twp->smode == 50) {
                twp->mode = 82;
                pwp->mj.reqaction = 86;
                twp->flag &= ~0x2500;
                func_8C0940DA(0x570, 0, 0, 0);
                break;
            }
        }
        break;

    case 46:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) {
            twp->mode = 48;
            break;
        }
        if (func_8C0AFC98(twp, mwp, pwp)) {
            pwp->spd.y = pwp->p.jmp_y_spd * 1.05f;
            twp->mode = 49;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) {
            twp->mode = 47;
            pwp->mj.reqaction = 15;
            func_8C0940DA(0x2D9, 0, 0, 0);
            twp->flag |= 0x500;
        }
        break;

    case 47:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) {
            twp->mode = 48;
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C0AFC98(twp, mwp, pwp)) {
            twp->mode = 49;
            pwp->spd.y = pwp->p.jmp_y_spd * 1.05f;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E38C) {
            twp->mode = 46;
            func_8C0AE588(twp, mwp, pwp);
            twp->flag &= ~0x500;
        }
        break;

    case 49:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 46;
            func_8C0AE588(twp, mwp, pwp);
            twp->flag &= ~0x500;
        }
        break;

    case 48:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 46;
            func_8C0AE588(twp, mwp, pwp);
        }
        break;

    case 50:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 46) {
            twp->mode = 46;
            pwp->mj.reqaction = 9;
        }
        break;

    case 51:
        if (func_8C0AFF1A(twp, mwp, pwp) || func_8C0AFC98(twp, mwp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if ((PER(twp).x1 << 8) < -0xC00) {
            twp->mode = 53;
            pwp->mj.reqaction = 90;
            break;
        }
        if ((PER(twp).x1 << 8) > 0xC00) {
            twp->mode = 54;
            pwp->mj.reqaction = 91;
            break;
        }
        if (twp->mode < 51 || twp->mode > 54) {
            pwp->htp = NULL;
        }
        break;

    case 52:
        if (func_8C0AFF1A(twp, mwp, pwp) || func_8C0AFC98(twp, mwp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if (pwp->htp != NULL) {
            taskwk *htwp;

            htwp = pwp->htp->twp;
            if (htwp->pos.y + CWP(htwp)->info->center.y - CWP(htwp)->info->b >= twp->pos.y) {
                twp->mode = 51;
                pwp->mj.reqaction = 93;
                break;
            }
        } else {
            func_8C0A9660(twp, mwp, pwp);
            break;
        }
        if (twp->mode < 51 || twp->mode > 54) {
            pwp->htp = NULL;
        }
        break;

    case 53:
        if (func_8C0AFF1A(twp, mwp, pwp) || func_8C0AFC98(twp, mwp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if ((PER(twp).x1 << 8) > -0xC00) {
            twp->mode = 51;
            pwp->mj.reqaction = 89;
            break;
        }
        if (twp->mode < 51 || twp->mode > 54) {
            pwp->htp = NULL;
        }
        break;

    case 54:
        if (func_8C0AFF1A(twp, mwp, pwp) || func_8C0AFC98(twp, mwp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if ((PER(twp).x1 << 8) < 0xC00) {
            twp->mode = 51;
            pwp->mj.reqaction = 89;
            break;
        }
        if (twp->mode < 51 || twp->mode > 54) {
            pwp->htp = NULL;
        }
        break;

    case 55:
        if (func_8C0AFF1A(twp, mwp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if (func_8C06CEBE(twp, pwp)) {
            twp->mode = 59;
            pwp->mj.reqaction = 96;
            twp->flag &= ~2;
            break;
        }
        if ((PER(twp).x1 << 8) < -0xC00) {
            twp->mode = 57;
            pwp->mj.reqaction = 99;
            break;
        }
        if ((PER(twp).x1 << 8) > 0xC00) {
            twp->mode = 58;
            pwp->mj.reqaction = 100;
            break;
        }
        if (pwp->free.w[3] == 300 || pwp->free.w[3] == 420 || pwp->free.w[3] == 480) {
            twp->mode = 56;
            pwp->mj.reqaction = 95;
        }
        break;

    case 56:
        if (func_8C0AFF1A(twp, mwp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if (func_8C06CEBE(twp, pwp)) {
            twp->mode = 59;
            pwp->mj.reqaction = 96;
            twp->flag &= ~2;
            break;
        }
        if (pwp->mj.reqaction != 95) {
            if (pwp->free.w[3] >= 480) {
                twp->mode = 12;
                pwp->mj.reqaction = 18;
                pwp->free.w[3] = 0;
                pwp->htp = NULL;
                pwp->jumptimer = 0;
                break;
            } else {
                twp->mode = 55;
                pwp->mj.reqaction = 94;
                break;
            }
        }
        break;

    case 57:
        if (func_8C0AFF1A(twp, mwp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if (func_8C06CEBE(twp, pwp)) {
            twp->mode = 59;
            pwp->mj.reqaction = 96;
            twp->flag &= ~2;
            break;
        }
        if (pwp->free.w[3] == 300 || pwp->free.w[3] == 420 || pwp->free.w[3] == 480) {
            twp->mode = 56;
            pwp->mj.reqaction = 95;
            break;
        }
        if (_builtin_fabsf((Float)(PER(twp).x1 << 8)) < 3072.0f) {
            twp->mode = 55;
            pwp->mj.reqaction = 94;
            break;
        }
        break;

    case 58:
        if (func_8C0AFF1A(twp, mwp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if (func_8C06CEBE(twp, pwp)) {
            twp->mode = 59;
            pwp->mj.reqaction = 96;
            twp->flag &= ~2;
            break;
        }
        if (pwp->free.w[3] == 300 || pwp->free.w[3] == 420 || pwp->free.w[3] == 480) {
            twp->mode = 56;
            pwp->mj.reqaction = 95;
            break;
        }
        if (_builtin_fabsf((Float)(PER(twp).x1 << 8)) < 3072.0f) {
            twp->mode = 55;
            pwp->mj.reqaction = 94;
            break;
        }
        break;

    case 59:
        if (func_8C0AFF1A(twp, mwp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if (!(pwp->mj.nframe < (Float)pwp->mj.plactptr[pwp->mj.action].actptr->motion->nbFrame - 2.0f)) {
            twp->mode = 61;
            twp->ang.y += 0x8000;
            mwp->ang_aim.y = twp->ang.y;
            pwp->spd.x = 2.0f;
            pwp->spd.y = 0.5f;
            pwp->spd.z = 0.0f;
            pwp->jumptimer = 0;
            func_8C0940DA(219, 0, 0, 0);
            pwp->htp = NULL;
            twp->smode = 0;
        }
        break;

    case 60:
        if (func_8C0AFF1A(twp, mwp, pwp)) {
            pwp->htp = NULL;
            break;
        }
        if (func_8C06CEBE(twp, pwp)) {
            twp->mode = 59;
            pwp->mj.reqaction = 96;
            twp->flag &= ~2;
            break;
        }
        if (pwp->htp->twp->pos.y <= twp->pos.y + 16.0f) {
            twp->mode = 55;
            pwp->mj.reqaction = 94;
            break;
        }
        if (pwp->free.w[3] == 300 || pwp->free.w[3] == 420 || pwp->free.w[3] == 480) {
            twp->mode = 56;
            pwp->mj.reqaction = 95;
        }
        break;

    case 61:
        if (func_8C0AFF1A(twp, mwp, pwp)) {
            pwp->jumptimer = 0;
            break;
        }
        if (pwp->jumptimer++ > 20) {
            pwp->jumptimer = 0;
            twp->mode = 12;
            pwp->mj.reqaction = 18;
            pwp->htp = NULL;
            break;
        }
        break;

    case 62:
        if (func_8C0B0E94(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) {
            twp->mode = 65;
            pwp->mj.reqaction = 111;
            break;
        }
        if ((Sint32)PER(twp).press & (Sint32)D_8C19E390) {
            twp->mode = 66;
            pwp->mj.reqaction = 106;
            pwp->spd.y = pwp->p.jmp_y_spd;
            if (pwp->spd.x < pwp->p.rush_speed) {
                pwp->spd.x += pwp->p.jog_speed;
            }
            if (pwp->ttp != NULL) {
                sa1_forcewk *fwp;

                fwp = (sa1_forcewk *)pwp->ttp->fwp;
                if (fwp != NULL) {
                    func_8C07CB54(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
                }
                if ((Uint8)pwp->ttp->twp->id == 9) {
                    twp->mode = 67;
                    func_8C0940DA(230, 0, 0, 0);
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
                        v.y = 1.69999996f;
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
                        func_8C07CB54(twp->counter.b[0], p.x, p.y, p.z);
                    }
                    if (pwp->spd.x > 4.1399995f) {
                        if ((Float)rand() * 3.0517578125e-05f > 0.65999998f) {
                            pwp->mj.reqaction = 117;
                            func_8C0940DA(0x575, 0, 0, 0);
                        } else if ((Float)rand() * 3.0517578125e-05f > 0.65999998f) {
                            pwp->mj.reqaction = 118;
                            func_8C0940DA(0x576, 0, 0, 0);
                        } else {
                            pwp->mj.reqaction = 119;
                            func_8C0940DA(0x576, 0, 0, 0);
                        }
                        if ((Float)rand() * 3.0517578125e-05f >= 0.5f) {
                            func_8C01AD2C(func_8C01FB98, 0, 2);
                            D_8C112048[0x48 / 4] = 0.0f;
                        } else {
                            func_8C01AD2C(func_8C01FD6C, 0, 2);
                            D_8C112048[0x48 / 4] = 0.0f;
                        }
                        func_8C0A77A0(100);
                    } else if (pwp->spd.x > 2.7599998f) {
                        pwp->mj.reqaction = 120;
                        func_8C0A77A0(50);
                    } else if (pwp->spd.x > 1.8399998f) {
                        pwp->mj.reqaction = 123;
                        func_8C0A77A0(30);
                    } else {
                        twp->mode = 66;
                        pwp->mj.reqaction = 106;
                        func_8C0A77A0(10);
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

                twp->mode = 64;
                GetAnalog(twp, &ang, NULL);
                if ((ang = SubAngle(twp->ang.y, ang)) > 0) {
                    pwp->mj.reqaction = 103;
                } else {
                    pwp->mj.reqaction = 104;
                }
            }
        }
        break;

    case 64:
        if (func_8C0B0E94(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) {
            twp->mode = 65;
            pwp->mj.reqaction = 111;
            break;
        }
        if ((Sint32)PER(twp).release & (Sint32)D_8C19E390) {
            twp->mode = 66;
            pwp->spd.y = pwp->p.jmp_y_spd;
            if (pwp->spd.x < pwp->p.rush_speed) {
                pwp->spd.x += pwp->p.jog_speed;
            }
            if (pwp->ttp != NULL) {
                sa1_forcewk *fwp;

                fwp = (sa1_forcewk *)pwp->ttp->fwp;
                if (fwp != NULL) {
                    func_8C07CB54(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
                }
            }
            pwp->mj.reqaction = 106;
            twp->flag &= ~2;
            pwp->jumptimer = 0;
            func_8C0940DA(17, 0, 0, 0);
            break;
        }
        if (!func_8C06CE40(twp, mwp) || pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 62;
            pwp->mj.reqaction = 102;
            func_8C0AEFA0(twp, mwp, pwp);
            break;
        }
        {
            Angle ang;

            GetAnalog(twp, &ang, NULL);
            if ((ang = SubAngle(twp->ang.y, ang)) > 0) {
                pwp->mj.reqaction = 103;
            } else {
                pwp->mj.reqaction = 104;
            }
        }
        break;

    case 65:
        if (func_8C0B0E94(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            twp->mode = 62;
            if (pwp->spd.y <= -3.0f) {
                pwp->mj.reqaction = 113;
            } else {
                pwp->mj.reqaction = 102;
                if (pwp->spd.x <= 0.29999999f) {
                    pwp->spd.x = 1.0f;
                }
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0AEFA0(twp, mwp, pwp);
        }
        break;

    case 66:
        if (func_8C0B0E94(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            twp->mode = 62;
            if (pwp->spd.y <= -3.0f) {
                pwp->mj.reqaction = 113;
            } else {
                pwp->mj.reqaction = 102;
                if (pwp->spd.x <= 0.29999999f) {
                    pwp->spd.x = 1.0f;
                }
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0AEFA0(twp, mwp, pwp);
            break;
        }
        break;

    case 67:
        if (func_8C0B0E94(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            twp->mode = 62;
            if (pwp->spd.y <= -3.0f) {
                pwp->mj.reqaction = 113;
            } else {
                pwp->mj.reqaction = 102;
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0AEFA0(twp, mwp, pwp);
            break;
        }
        break;

    case 68:
        if (func_8C0B0E94(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 109 && pwp->mj.reqaction != 110) {
            twp->mode = 62;
            pwp->mj.reqaction = 102;
            func_8C0AEFA0(twp, mwp, pwp);
        }
        break;

    case 69:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) {
            twp->mode = 70;
        }
        break;

    case 70:
        if (func_8C0AFF1A(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            twp->mode = 69;
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
        }
        break;

    case 71:
        if (func_8C0B0F24(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) {
            twp->mode = 74;
            pwp->mj.reqaction = 137;
            break;
        }
        if (func_8C0AFC98(twp, mwp, pwp)) {
            twp->mode = 78;
            pwp->mj.reqaction = 141;
            break;
        }
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 72;
            func_8C0AE6D8(twp, mwp, pwp);
        }
        break;

    case 72:
        if (func_8C0B0F24(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) {
            twp->mode = 74;
            pwp->mj.reqaction = 137;
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0AFC98(twp, mwp, pwp)) {
            twp->mode = 78;
            pwp->mj.reqaction = 141;
            break;
        }
        if (func_8C0B122E(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            twp->mode = 71;
            pwp->mj.reqaction = 130;
        }
        break;

    case 73:
        twp->flag &= ~0x500;
        if (func_8C0B0F24(twp, mwp, pwp)) break;
        if (func_8C0B11C0(twp, mwp, pwp)) {
            twp->mode = 74;
            pwp->mj.reqaction = 137;
            twp->flag &= ~0x2000;
            break;
        }
        if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 72;
            pwp->mj.reqaction = 131;
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0B122E(twp, mwp, pwp)) {
            twp->mode = 71;
            pwp->mj.reqaction = 130;
            twp->flag &= ~0x2000;
        }
        break;

    case 78:
        if (func_8C0B0F24(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (func_8C0B122E(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 71;
                pwp->mj.reqaction = 130;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 72;
                func_8C0AE6D8(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C0AFE20(twp, mwp, pwp)) {
            twp->mode = 74;
            pwp->mj.reqaction = 137;
            break;
        }
        if (func_8C06CEBE(twp, pwp)) {
            twp->mode = 77;
            func_8C0AF18C(twp, mwp, pwp);
            break;
        }
        if (func_8C06CEFE(twp, pwp)) {
            FMAC_ACC(pwp->spd.y, pwp->p.jmp_addit, 0.79999997f);
        }
        break;

    case 76:
        if (func_8C0B0F24(twp, mwp, pwp)) break;
        if (!(mwp->spd.y > 0.0f)) {
            twp->mode = 74;
            pwp->mj.reqaction = 136;
        }
        break;

    case 75:
        if (func_8C0B0F24(twp, mwp, pwp)) break;
        if (!(mwp->spd.y > 0.0f)) {
            twp->mode = 74;
            pwp->mj.reqaction = 136;
        }
        break;

    case 74:
        if (func_8C0B0F24(twp, mwp, pwp)) break;
        if (!(twp->flag & 3)) break;
        if (func_8C0B122E(twp, mwp, pwp)) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 71;
            pwp->mj.reqaction = 130;
        } else {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 72;
            func_8C0AE6D8(twp, mwp, pwp);
        }
        break;

    case 77:
        if (func_8C0B0F24(twp, mwp, pwp)) break;
        {
            colliwk *hit;

            hit = CWP(twp)->hit_cwp;
            if (pwp->free.w[3]++ >= 360
                || (hit != NULL && (hit->id == 0 || hit->id == 2 || hit->id == 3))) {
                pwp->free.w[2] = -1;
                twp->mode = 78;
                break;
            }
        }
        if (pwp->free.w[2] >= 5.9999996f) {
            pwp->free.w[2] = 0;
            twp->mode = 74;
            pwp->mj.reqaction = 137;
            twp->flag &= ~0x500;
            break;
        }
        if (!(twp->flag & 3)) break;
        twp->flag &= ~0x500;
        if (func_8C0B122E(twp, mwp, pwp)) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 71;
            pwp->mj.reqaction = 130;
            break;
        }
        twp->ang.x = mwp->ang_aim.x;
        twp->ang.z = mwp->ang_aim.z;
        twp->mode = 72;
        func_8C0AE6D8(twp, mwp, pwp);
        break;

    case 80:
        if (EWP(twp)->list4C == NULL) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            func_8C0A9660(twp, mwp, pwp);
        }
        break;

    case 83:
        if (PER(twp).press & 4) {
            if ((PER(twp).on & 2) && (Uint16)GetStageNumber() == 0x401) {
                twp->mode = 46;
                pwp->mj.reqaction = 9;
                break;
            }
            if (!(PER(twp).on & 2) && (Uint16)GetStageNumber() == 0x802) {
                twp->mode = 65;
                pwp->mj.reqaction = 105;
                break;
            }
            if (!(PER(twp).on & 2) && (Uint16)GetStageNumber() == 0x2600) {
                twp->mode = 65;
                pwp->mj.reqaction = 105;
                break;
            }
            if ((pwp->equipment & 0x8000) && ((Uint16)GetStageNumber() & 0xFF00) == 0x1300) {
                twp->mode = 71;
                pwp->mj.reqaction = 130;
                break;
            }
            twp->mode = 2;
            pwp->mj.reqaction = 9;
        }
        break;
    }
}
#else
ASM_STUB(void SonicModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

ASM_STUB(void func_8C0AE6D8(taskwk *twp, motionwk *mwp, playerwk *pwp))
