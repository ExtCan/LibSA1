/*
 * 1ST_READ 8C0C0640: KnucklesMain, the exec of Knuckles' player task
 * (chunk 8C0C0640-8C0C5220, function 1/9; created by LoadPlayerTask
 * 8C04F3C0, also called by 8C083F7C).
 *
 * Per frame: ride flags from the collision work, the Maximum Heat charge
 * (func_8C0C450A), the mode machine (KnucklesModeMachine 8C0C193A, JSR),
 * then a switch on twp->mode (0-53) running the physics steps of the current
 * mode, then (Speed Highway, Red Mountain, Casinopolis) the push-out along
 * the position history while func_8C0C88E6 reports a contact, motion, welds,
 * display, held object, shadow and the per-frame collision entry.
 *
 * Code generation notes (same situation as SonicMain, code_8C0A77BC.c, and
 * AmyMain, code_8C0D6B40.c):
 *  - every local lives on the stack (SHC does that for this very large
 *    function): tp @(36), twp @(32), mwp @(28), pwp @(24); block locals of
 *    sibling scopes share the slots below;
 *  - the cases are written in the original source order (the order of
 *    their code), not in numeric order; SHC cross-jumps the identical
 *    "PGetSpeed; PSetPosition; func_8C079096; break;" tails (8C0C0D9E,
 *    8C0C128E, 8C0C172E), the func_8C0C513A tail of case 3 into case 31 and
 *    the func_8C109EAC call of case 11/2 into case 18;
 *  - case 22 needs the "break" inside its if (else case 23 swaps R1/R2),
 *    cases 22/23 need the compound assignment inside the test (no reload),
 *    case 26 needs the nested if (constant 0.2f reused from FR3);
 *  - the post-switch block keeps n (Uint8) @(23), dx @(16), dz @(12) and
 *    the history position @(0); GetPlayerPosition gets (Sint8)n (MOV.B, no
 *    EXTU.B).
 *
 * Status: instruction-identical to the original. All 1925 instructions
 * match (tools/shcdiff.py), and the 54-entry switch jump table at 8C0C0718
 * assembles to the original halfwords. tools/shcdiff.py still reports the
 * MOVA of the jump table (a ".DATA.W Lcase-Lbase" item it cannot evaluate),
 * hence the NON_MATCHING guard, as for SonicMain and AmyMain. Byte layout:
 * in the game SHC deferred the last literal pool (8C0C1A00-8C0C1A2C) into
 * KnucklesModeMachine (next in the translation unit); compiled alone the
 * pool follows KnucklesMain, so the 12 pool loads from 8C0C1894 on have
 * smaller displacements (same values).
 * Re-verified: shcdiff --flags=-define=NON_MATCHING reports only the MOVA
 * (1924/1925); the 54 ".DATA.W" entries at listing L1005 equal the
 * halfwords at 8C0C0718-8C0C0783; 4 FMACs in the asm, 4 SA1_FMAC/FMAC_ACC
 * here; tools/pc_check.py is clean.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"     /* func_8C06CE40, func_8C076782, func_8C0C5680, func_8C0C896C */

/* rand() scaled to [0, 1) (inline in the game code: rand() * 1/32768) */
#define KNUCKLES_RAND() ((Float)rand() * 3.0517578125e-05f)

/* "acc += a * b" compiled to FMAC (see code_8C0C450A.c): fused on PC */
#if defined(TARGET_PC)
#define FMAC_ACC(acc, a, b) ((acc) = SA1_FMAC((a), (b), (acc)))
#else
#define FMAC_ACC(acc, a, b) ((acc) += (a) * (b))
#endif

/* Knuckles data (extern only) */
extern CCL_INFO D_8C352FBC[];           /* Knuckles' collision info (4 entries) */
extern NJS_OBJECT *D_8C35308C[];        /* head / eyeL / eyeR objects passed to func_8C0C896C (cf. Sonic's D_8C1C5F08) */
extern Float B_8C7A2614;                /* cleared by case 0 */
extern Float B_8C7A2618;
extern Float B_8C7A261C;
extern Sint32 B_8C7A25F0;               /* cleared every frame */

/* player common, not declared in sa1/player.h (func_8C06CE40 and
 * func_8C076782: sa1/protos.h) */
void func_8C01AE60(void);
void func_8C06CA4E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F25C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F386(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F9A2(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06FB04(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C070EDA(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0711D2(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0725C0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07266E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C072806(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0728A0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C078308(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C0784F8(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C078710(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C109EAC(Sint32 pno, Sint32 a, Sint32 b, Sint32 c);      /* pad vibration */

/* Knuckles (same chunk and other chunks; all called with JSR; func_8C0C5680
 * and func_8C0C896C, the look task setup: sa1/protos.h) */
void KnucklesModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0C450A(taskwk *twp, playerwk *pwp);                 /* Maximum Heat charge */
void func_8C0C4690(task *tp);                                   /* dest */
void func_8C0C46AC(task *tp);                                   /* disp */
void func_8C0C4C2E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0C4F40(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0C4F48(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* stand action selector */
void func_8C0C513A(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* run action selector */
void func_8C0C5220(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0C543E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0C6E80(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0C726E(task *tp);
void func_8C0C7694(task *tp);
void func_8C0C7786(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0C7D4C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0C813E(taskwk *twp, playerwk *pwp);
void func_8C0C876C(taskwk *twp);
Sint32 func_8C0C88E6(taskwk *twp);
void func_8C0C8E8E(task *tp);
taskwk *func_8C0C8F20(taskwk *twp);                             /* nearest target (B_8C73EE38/B_8C742288 list) */
void func_8C0C9000(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0C99E4(task *tp);

#ifdef NON_MATCHING
void KnucklesMain(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = PWP(mwp);
    if ((twp->flag & 0x20) && mwp->spd.y <= 0.0f) {
        twp->flag |= 2;
        twp->ang.x = CWP(twp)->mytask->twp->ang.x;
        twp->ang.z = CWP(twp)->mytask->twp->ang.z;
    } else {
        twp->flag &= ~2;
    }
    func_8C0C450A(twp, pwp);
    if (D_8C19B468 == 1) {
        twp->mode = 53;
    } else if (twp->mode != 51) {
        KnucklesModeMachine(twp, mwp, pwp);
    }
    switch (twp->mode) {
    case 0:
        twp->counter.b[1] = SA1_CHR_KNUCKLES;
        PInitialize((Uint8)twp->counter.b[0], tp);
        pwp = PWP(mwp);
        pwp->mj.reqaction = 0;
        pwp->mj.plactptr = KnucklesActionList;
        CCL_Init(tp, D_8C352FBC, 0, 1, 4);
        CWP(twp)->info->a = pwp->p.rad;
        twp->mode = 1;
        func_8C098FAE(3);
        pwp->mj.pljvptr = KnucklesWeldList;
        pwp->free.w[5] = -1;
        func_8C0C896C(twp->counter.b[0], D_8C35308C);
        {
            task *ctp;

            ctp = CreateElementalTask(2, 3, func_8C0C8E8E);
            if (ctp != NULL) {
                ctp->twp->counter.b[0] = twp->counter.b[0];
            }
            ctp = CreateElementalTask(3, 3, func_8C0C726E);
            if (ctp != NULL) {
                ctp->twp->counter.b[0] = twp->counter.b[0];
            }
            tp->dest = func_8C0C4690;
            tp->disp = func_8C0C46AC;
            if (func_8C049A4E() != 1 && (tcrf_LevelSelectVar == 4 || tcrf_LevelSelectVar == 9)) {
                CreateElementalTask(2, 3, func_8C0C99E4);
            }
            ctp = CreateElementalTask(2, 3, func_8C0C7694);
            if (ctp != NULL) {
                ctp->twp->counter.b[0] = twp->counter.b[0];
                ctp->twp->value.ptr = &pwp->free.w[5];
            }
        }
        B_8C7A2614 = B_8C7A2618 = B_8C7A261C = 0.0f;
        break;
    case 1:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0C4F48(twp, mwp, pwp);
        break;
    case 2:
        if (pwp->mj.lastaction == 22 && pwp->mj.action != 22) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            twp->mode = 17;
            pwp->mj.reqaction = 31;
            func_8C07B320(twp);
            pwp->spd.x = -pwp->spd.x;
            pwp->spd.z = -pwp->spd.z;
        } else {
            func_8C079096(twp, mwp, pwp);
            func_8C0C513A(twp, mwp, pwp);
        }
        break;
    case 3:
        func_8C06F25C(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        func_8C0C513A(twp, mwp, pwp);
        break;
    case 4:
        func_8C071700(twp, mwp, pwp);
        func_8C0714B6(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 5:
        func_8C0714B6(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        break;
    case 6:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 7:
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 8:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 9:
    case 21:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 10:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 2;
            pwp->mj.reqaction = 22;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
        }
        break;
    case 11:
        func_8C071D40(twp, mwp, pwp);
        if (mwp->spd.y > 0.0f) {
            func_8C06E904(twp, mwp, pwp);
            PGetSpeed(twp, mwp, pwp);
        } else {
            func_8C06F9A2(twp, mwp, pwp);
            func_8C0728A0(twp, mwp, pwp);
        }
        switch (func_8C0C7D4C(twp, mwp, pwp)) {
        case 1:
            func_8C079096(twp, mwp, pwp);
            func_8C109EAC((Uint8)twp->counter.b[0], 3, 15, 0);
            pwp->free.b[5] += 20;
            break;
        case 2:
            func_8C0C9000(twp, mwp, pwp);
            func_8C109EAC((Uint8)twp->counter.b[0], 3, 15, 0);
            break;
        case 3:
            twp->flag &= ~0x500;
            twp->mode = 10;
            pwp->mj.reqaction = 53;
            break;
        default:
            PSetPosition(twp, mwp, pwp);
            func_8C079096(twp, mwp, pwp);
            break;
        }
        break;
    case 12:
        func_8C071D40(twp, mwp, pwp);
        if (twp->timer.w[0] == 30) {
            twp->ang.y += 0x400;
        }
        if (mwp->spd.y > 0.0f) {
            PGetSpeed(twp, mwp, pwp);
        } else {
            func_8C0728A0(twp, mwp, pwp);
        }
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 13:
        if (pwp->mj.reqaction == 23) {
            func_8C070EDA(twp, mwp, pwp);
        } else {
            func_8C06F386(twp, mwp, pwp);
            if (func_8C06C880(twp) & 0x400) {
                func_8C094330(15, twp, 1, 0, 2);
            }
            if (pwp->mj.lastaction == 23) {
                func_8C0940DA(0x47D, 0, 0, 0);
            }
        }
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 14:
        func_8C071D40(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 15:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 16:
        break;
    case 17:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 18:
        twp->flag |= 0x400;
        {
            taskwk *ctwp;
            NJS_POINT3 v;

            ctwp = func_8C0C8F20(twp);
            if (ctwp == NULL) {
                v.x = v.y = v.z = 0.0f;
                pwp->spd = v;
                PConvertVector_P2G(twp, &v);
                mwp->spd = v;
                if (GetStageNumber() < 0x0F00) {
                    func_8C01AE60();
                }
                twp->flag &= ~0x400;
                twp->mode = 9;
                pwp->mj.reqaction = 18;
                break;
            }
            v.x = 5.0f;
            v.y = SA1_FMAC(ctwp->pos.y - twp->pos.y, 0.049999998f, pwp->spd.y);
            if (v.y < -5.0f) {
                v.y = -5.0f;
            } else if (v.y > 5.0f) {
                v.y = 5.0f;
            }
            v.z = 0.0f;
            pwp->spd = v;
            PConvertVector_P2G(twp, &v);
            mwp->spd = v;
            PSetPosition(twp, mwp, pwp);
            func_8C079096(twp, mwp, pwp);
            if (!(twp->timer.w[1] & 8)) {
                func_8C0940DA(0x2E5, 0, 0, 0);
            }
            func_8C109EAC((Uint8)twp->counter.b[0], 2, 30, 0);
        }
        break;
    case 19:
    case 20:
        break;
    case 22:
        if ((pwp->mj.nframe += 0.7f) > 10.0f) {
            twp->mode = 23;
            break;      /* needed: without it case 23 swaps R1/R2 */
        }
        break;
    case 23:
        if ((pwp->mj.nframe -= 0.5f) < 0.5f) {
            func_8C0C4F40(twp, mwp, pwp);
        }
        break;
    case 24:
        if (pwp->mj.reqaction != 52 && pwp->mj.reqaction != 50) {
            func_8C06FB04(twp, mwp, pwp);
            func_8C072806(twp, mwp, pwp);
            func_8C076782(twp, mwp, pwp);
        }
        func_8C079096(twp, mwp, pwp);
        break;
    case 25:
        twp->pos.x += mwp->spd.x;
        twp->pos.y += mwp->spd.y;
        twp->pos.z += mwp->spd.z;
        break;
    case 26:
        func_8C0C7786(twp, mwp, pwp);
        func_8C071700(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        /* both tests compare with 54 in the original (sic) */
        if (pwp->mj.reqaction == 54 || pwp->mj.reqaction == 54) {
            /* nested if: with "&&" the 0.2f of the test is not reused */
            if (pwp->spd.x > 0.19999999f) {
                FMAC_ACC(pwp->spd.x, 0.19999999f - pwp->spd.x, 0.5f);
            }
        }
        if (PSetPosition(twp, mwp, pwp)) {
            NJS_POINT3 v;

            GetPlayerPosition(twp->counter.b[0], 1, &v, NULL);
            twp->pos = v;
            pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
            mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        }
        func_8C079096(twp, mwp, pwp);
        break;
    case 42:
    case 43:
        func_8C0784F8(twp, mwp, pwp);
        func_8C0C5220(twp, mwp, pwp);
        break;
    case 44:
    case 45:
        func_8C078710(twp, mwp, pwp);
        func_8C0C543E(twp, mwp, pwp);
        break;
    case 46:
        twp->flag &= ~2;
        func_8C05E7D4(twp, mwp, pwp);
        break;
    case 47:
        break;
    case 48:
        {
            Float mag;
            Angle ang;

            func_8C07B2B8(0, 30);
            func_8C0711D2(twp, mwp, pwp);
            PGetSpeed(twp, mwp, pwp);
            if (GetAnalog(twp, &ang, &mag)) {
                mwp->spd.x = mag * (0.5f - KNUCKLES_RAND());
                if (twp->flag & 3) {
                    if (KNUCKLES_RAND() > 0.79999996f) {
                        mwp->spd.y = SA1_FMAC(KNUCKLES_RAND(), 0.19999999f, 0.29999999f);
                    }
                }
                mwp->spd.z = mag * (0.5f - KNUCKLES_RAND());
            }
        }
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 27:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 28:
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 29:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 30:
        func_8C06F386(twp, mwp, pwp);
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 32:
        func_8C06D136(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 31:
        if (pwp->mj.lastaction == 73 && pwp->mj.action != 73) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0C513A(twp, mwp, pwp);
        break;
    case 33:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 34:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 35:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 36:
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 37:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 38:
        func_8C06F386(twp, mwp, pwp);
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 39:
        func_8C071D40(twp, mwp, pwp);
        func_8C06D136(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 40:
        func_8C06CA4E(twp, mwp, pwp);
        break;
    case 41:
        pwp->htp->mwp->acc.x = pwp->htp->mwp->acc.y = pwp->htp->mwp->acc.z
            = pwp->mj.nframe * 0.01f;
        break;
    case 49:
        func_8C04B9E0(twp);
        break;
    case 50:
        if ((pwp->mj.flag & 1) && pwp->shadow.y_top > CWP(twp)->info->center.y
                && (pwp->shadow.Attr_top & 2)) {
            pwp->spd.y = pwp->shadow.y_top - CWP(twp)->info->center.y;
            if (pwp->spd.y > 0.39999998f) {
                pwp->spd.y = 0.39999998f;
            }
        } else {
            pwp->spd.y = 0.0f;
        }
        func_8C071D40(twp, mwp, pwp);
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 51:
        if (twp->flag & 3) {
            pwp->mj.reqaction = 28;
        }
        func_8C071D40(twp, mwp, pwp);
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 52:
        break;
    case 53:
        func_8C0C5680(twp, mwp, pwp);
        break;
    }
    if (twp->mode != 11 && twp->mode != 12
            && (tcrf_LevelSelectVar == 4 || tcrf_LevelSelectVar == 5 || tcrf_LevelSelectVar == 9)
            && func_8C0C88E6(twp) == 1) {
        Uint8 n;
        Float dx;
        Float dz;
        NJS_POINT3 v;

        /* the loop body always runs (n = 1): PC-only init silences
         * -Wconditional-uninitialized */
        PC_ONLY(dx = 0.0f; dz = 0.0f;)
        /* step back through the position history to the first point at
         * least 0.1 away (horizontally) */
        for (n = 1; n < 128; n++) {
            GetPlayerPosition(twp->counter.b[0], (Sint8)n, &v, NULL);
            dx = v.x - twp->pos.x;
            dz = v.z - twp->pos.z;
            if (SA1_FMAC(dx, dx, dz * dz) > 0.009999998f) {
                break;
            }
        }
        /* then push out by that step while func_8C0C88E6 still reports a
         * contact (at most 128 more steps) */
        n = 128;
        twp->pos.x += dx;
        twp->pos.z += dz;
        while (n > 0 && func_8C0C88E6(twp) == 1) {
            n--;
            twp->pos.x += dx;
            twp->pos.z += dz;
        }
    }
    PSetMotion(&pwp->mj);
    func_8C0C4C2E(twp, mwp, pwp);
    if (!(twp->flag & 0x100)) {
        PJoinVertexes(twp, mwp, pwp);
    }
    func_8C0C46AC(tp);
    if (pwp->htp != NULL) {
        func_8C0C6E80(twp, mwp, pwp);
    }
    func_8C0925A0(twp, &pwp->shadow);
    func_8C0C813E(twp, pwp);
    func_8C0C876C(twp);
    B_8C7A25F0 = 0;
    if (pwp->mj.reqaction != 39 && pwp->mj.reqaction != 40) {
        CCL_Entry(twp);
    }
    func_8C06C6F8(twp, mwp, pwp);
}
#else
ASM_STUB(void KnucklesMain(task *tp))
#endif
