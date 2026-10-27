/*
 * 1ST_READ 8C0B6148: TailsMain, the exec of Tails' player task
 * (chunk 8C0B6120-8C0BA820, function 2/6; created by LoadPlayerTask
 * 8C04F3C0, also called by 8C081AC0 and 8C081CAC).
 *
 * Per frame: ride flags from the collision work, the mode machine
 * (TailsModeMachine 8C0B70F4, skipped in mode 60), the attribute bits of
 * Tails' first CCL_INFO, then a switch on twp->mode (0-63) running the
 * physics steps of the current mode, then motion, the jet anklet / tail
 * parts and welds, display, held object, shadow and the per-frame
 * collision entry.
 *
 * Code generation notes (same situation as SonicMain, code_8C0A77BC.c):
 *  - every local lives on the stack (SHC does that for this very large
 *    function): tp @(24), twp @(20), mwp @(16), pwp @(12); per-case
 *    temporaries are declared in blocks so sibling scopes share slots;
 *  - the cases are written in the original source order (the order of
 *    their code), not in numeric order; SHC cross-jumps the identical
 *    "...; PSetPosition(); func_8C079096(); break;" tails into the last
 *    case that has them;
 *  - the part objects are declared as arrays so the stores are
 *    "base + @(48,Rn)" / "@(4,Rn)" instead of folded "sym+48" pool words;
 *  - modes 10/19 and 18/61 share their case bodies (one label each in the
 *    jump table); 62/63 are empty cases at the end (the table has 64
 *    entries, range check CMP/HS #64); 49 has no case.
 *
 * NON_MATCHING only because of tools/shcdiff.py (same as SonicMain): the
 * instruction stream is identical (1540/1540, every pool value and branch
 * target equal), but the MOVA that addresses the switch jump table is
 * compared against the table's first ".DATA.W Lx-Ly" item, an expression
 * shcdiff cannot evaluate. All 64 jump table entries were checked to reach
 * the same instruction (by index) as in the original. Note also that the
 * original's first literal pool inside TailsMain (8C0B6220) holds
 * func_8C0B6120's pool words (tcrf_LevelSelectVar, B_8C78B451,
 * func_8C08A298): func_8C0B6120 and TailsMain were one source file, so
 * this file alone places the jump table 8 bytes later than the original.
 */
#include "common.h"
#include "sa1/sa1.h"

/* Tails data (extern only) */
extern CCL_INFO D_8C2575AC[];           /* Tails' collision info (4 entries) */
extern NJS_OBJECT *D_8C25767C[];        /* head, eyeL, eyeR (func_8C0BE070) */
extern NJS_OBJECT *D_8C257688[];        /* head, eyeL, eyeR (func_8C0BE0CE) */
extern NJS_OBJECT D_8C25A15C[];         /* nodes whose sibling is a (jet anklet) shoe part */
extern NJS_OBJECT D_8C25AFCC[];
extern NJS_OBJECT D_8C261E90[];
extern NJS_OBJECT D_8C262D00[];
extern NJS_OBJECT D_8C259D40;           /* plain shoes */
extern NJS_OBJECT D_8C25ABB0;
extern NJS_OBJECT D_8C261A74;
extern NJS_OBJECT D_8C2628E4;
extern NJS_OBJECT Tails_Right_Jet_Anklet_Worn;
extern NJS_OBJECT Tails_Left_Jet_Anklet_Worn;
extern NJS_OBJECT D_8C2592C8[];         /* objects whose model is swapped by equipment & 0x10 */
extern NJS_OBJECT D_8C260FFC[];
extern NJS_MODEL D_8C2592A0;            /* plain models (stored just before their objects) */
extern NJS_MODEL D_8C260FD4;
extern NJS_MODEL D_8C28FA8C;            /* model used by both objects with equipment & 0x10 */

/* player common, not declared in sa1/player.h */
void func_8C019368(Sint8 a, Sint8 b);
void func_8C06CA4E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F25C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F386(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07061C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C070814(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07095E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C070EDA(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0711D2(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0725C0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07266E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C078308(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C0784F8(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C078710(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07B694(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* Tails (same chunk, called with JSR) */
void TailsModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0BA738(task *tp);                                   /* dest */
void func_8C0BA75A(taskwk *twp, playerwk *pwp);
/* Tails (other chunks) */
void func_8C0BA820(task *tp);                                   /* disp */
void func_8C0BAFC0(taskwk *twp, playerwk *pwp);
void func_8C0BB11C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0BB22A(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0BB476(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0BB68A(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0BD4CC(taskwk *twp, playerwk *pwp);
void func_8C0BD6B8(taskwk *twp, playerwk *pwp);
Angle func_8C0BD702(playerwk *pwp);
void func_8C0BD8B2(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C0BE070(Sint8 pno, NJS_OBJECT **objs);       /* code_8C0BDC80.c */
Bool func_8C0BE0CE(Sint8 pno, NJS_OBJECT **objs);
void func_8C0BF262(taskwk *twp, playerwk *pwp);
void func_8C0BF402(task *tp);
void func_8C0BF76A(task *tp);
void func_8C0BFCA2(taskwk *twp, Sint32 se);
void func_8C0BFCC0(taskwk *twp);

#ifdef NON_MATCHING
void TailsMain(task *tp)
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
    if (twp->mode != 60) {
        TailsModeMachine(twp, mwp, pwp);
    }
    if (twp->cwp != NULL) {
        Sint32 a;
        Sint32 b;
        CCL_INFO *info;

        info = CWP(twp)->info;
        if ((Sint16)pwp->item & 0x8000) {
            a = 3;
            b = 3;
        } else if (twp->flag & 0x100) {
            a = 1;
            b = 1;
        } else if (twp->mode == 43) {
            a = 0;
            b = 1;
        } else {
            a = 0;
            b = 0;
        }
        info->flag = (info->flag & ~3) | a;
        info->flag = (info->flag & ~0xC) | (b << 2);
    }
    switch (twp->mode) {
    case 0:
        twp->counter.b[1] = SA1_CHR_TAILS;
        PInitialize((Uint8)twp->counter.b[0], tp);
        pwp = PWP(mwp);
        pwp->mj.plactptr = TailsActionList;
        pwp->mj.reqaction = 0;
        CCL_Init(tp, D_8C2575AC, 0, 1, 4);
        CWP(twp)->info->a = pwp->p.rad;
        twp->mode = 1;
        func_8C0BE070(twp->counter.b[0], D_8C25767C);
        func_8C0BE0CE(twp->counter.b[0], D_8C257688);
        func_8C098FAE(2);
        pwp->waittimer = 0;
        pwp->mj.pljvptr = TailsWeldList;
        {
            task *ctp;

            ctp = CreateElementalTask(2, 3, func_8C0BF76A);
            if (ctp != NULL) {
                ctp->twp->counter.b[0] = twp->counter.b[0];
            }
            ctp = CreateElementalTask(3, 3, func_8C0BF402);
            if (ctp != NULL) {
                ctp->twp->counter.b[0] = twp->counter.b[0];
            }
        }
        if (twp->counter.b[0] == 0) {
            func_8C019368(4, 20);
        }
        tp->dest = func_8C0BA738;
        tp->disp = func_8C0BA820;
        break;
    case 1:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0BAFC0(twp, pwp);
        break;
    case 2:
        if (pwp->mj.lastaction == 22 && pwp->mj.action != 22) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            func_8C0BFCA2(twp, 0x469);
            twp->mode = 16;
            pwp->mj.reqaction = 34;
            func_8C07B320(twp);
            pwp->spd.x = -(pwp->spd.x * 0.29999999f);
            pwp->spd.z = -(pwp->spd.z * 0.29999999f);
        } else {
            func_8C079096(twp, mwp, pwp);
            func_8C0BB11C(twp, mwp, pwp);
        }
        break;
    case 3:
        func_8C06F25C(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        func_8C0BB11C(twp, mwp, pwp);
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
        func_8C071656(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 10:
    case 19:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 11:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 12:
        if (pwp->mj.reqaction == 24) {
            func_8C070EDA(twp, mwp, pwp);
        } else {
            func_8C06F386(twp, mwp, pwp);
            func_8C0BFCC0(twp);
            if (pwp->mj.lastaction == 24) {
                func_8C0BFCA2(twp, 0x46C);
            }
        }
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 13:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 14:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 16:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 17:
        func_8C071700(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 36:
    case 37:
        func_8C0784F8(twp, mwp, pwp);
        func_8C0BB22A(twp, mwp, pwp);
        break;
    case 38:
    case 39:
        func_8C078710(twp, mwp, pwp);
        func_8C0BB476(twp, mwp, pwp);
        break;
    case 40:
        if (func_8C05E7D4(twp, mwp, pwp) <= 31) {
            pwp->mj.reqaction = 95;
        }
        break;
    case 41:
        break;
    case 42:
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 20:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 15:
        func_8C071D40(twp, mwp, pwp);
        func_8C0BD8B2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 18:
    case 61:
        break;
    case 21:
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 22:
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 23:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 24:
        func_8C06F386(twp, mwp, pwp);
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 26:
        func_8C06D136(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 25:
        if (pwp->mj.lastaction == 61 && pwp->mj.action != 61) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0BB11C(twp, mwp, pwp);
        break;
    case 27:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 28:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 29:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 30:
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 31:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 32:
        func_8C06F386(twp, mwp, pwp);
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0BFCC0(twp);
        break;
    case 33:
        func_8C071D40(twp, mwp, pwp);
        func_8C06D136(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 34:
        func_8C06CA4E(twp, mwp, pwp);
        break;
    case 35:
        pwp->htp->mwp->acc.x = pwp->htp->mwp->acc.y = pwp->htp->mwp->acc.z
            = pwp->mj.nframe * 0.0099999998f;
        break;
    case 43:
        break;
    case 44:
        func_8C0BD6B8(twp, pwp);
        break;
    case 45: {
        taskwk *htwp;
        Float y;

        htwp = pwp->htp->twp;
        twp->pos.y -= 0.5f;
        y = htwp->pos.y + CWP(htwp)->info->center.y - CWP(htwp)->info->b;
        if (y >= twp->pos.y) {
            twp->pos.y = y;
        }
        func_8C0BD6B8(twp, pwp);
        break;
    }
    case 46:
        twp->ang.y += func_8C0BD702(pwp);
        func_8C0BD6B8(twp, pwp);
        break;
    case 47:
        twp->ang.y -= func_8C0BD702(pwp);
        func_8C0BD6B8(twp, pwp);
        break;
    case 48:
        func_8C0700C2(twp, mwp, pwp, 5.9799992f);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            func_8C0BFCA2(twp, 0x469);
            twp->mode = 54;
            if (VectorAngle(&mwp->spd, &pwp->wall_normal, &pwp->cstsp->tnorm) > 0x8000) {
                pwp->mj.reqaction = 115;
            } else {
                pwp->mj.reqaction = 114;
            }
            func_8C07B320(twp);
            if ((Uint16)GetStageNumber() == 0x802) {
                func_8C0BFCA2(twp, 235);
            }
        } else {
            func_8C079096(twp, mwp, pwp);
            func_8C0BB68A(twp, mwp, pwp);
        }
        func_8C07B694(twp, mwp, pwp);
        break;
    case 50:
        func_8C07095E(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C07B694(twp, mwp, pwp);
        break;
    case 51:
        func_8C071D40(twp, mwp, pwp);
        func_8C07061C(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 52:
        func_8C071D40(twp, mwp, pwp);
        func_8C07061C(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 53:
        func_8C071D40(twp, mwp, pwp);
        func_8C070814(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp)) {
            pwp->mj.reqaction = 117;
        }
        func_8C079096(twp, mwp, pwp);
        break;
    case 54:
        func_8C07095E(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C07B694(twp, mwp, pwp);
        func_8C07B694(twp, mwp, pwp);
        break;
    case 55:
        func_8C071B26(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        if (twp->flag & 1) {
            mwp->spd.y = 0.0f;
        }
        func_8C079096(twp, mwp, pwp);
        break;
    case 56:
        func_8C071D40(twp, mwp, pwp);
        func_8C0714B6(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 57:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 58:
        func_8C04B9E0(twp);
        break;
    case 59:
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
    case 60:
        if (twp->flag & 3) {
            pwp->mj.reqaction = 28;
        }
        func_8C071D40(twp, mwp, pwp);
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 62:
    case 63:
        break;
    }
    PSetMotion(&pwp->mj);
    if (!(twp->flag & 0x100)) {
        if (pwp->equipment & 8) {
            D_8C25A15C->sibling = &Tails_Right_Jet_Anklet_Worn;
            D_8C25AFCC->sibling = &Tails_Left_Jet_Anklet_Worn;
            D_8C261E90->sibling = &Tails_Right_Jet_Anklet_Worn;
            D_8C262D00->sibling = &Tails_Left_Jet_Anklet_Worn;
        } else {
            D_8C25A15C->sibling = &D_8C259D40;
            D_8C25AFCC->sibling = &D_8C25ABB0;
            D_8C261E90->sibling = &D_8C261A74;
            D_8C262D00->sibling = &D_8C2628E4;
        }
        if (pwp->equipment & 0x10) {
            D_8C2592C8->model = &D_8C28FA8C;
            D_8C260FFC->model = &D_8C28FA8C;
        } else {
            D_8C2592C8->model = &D_8C2592A0;
            D_8C260FFC->model = &D_8C260FD4;
        }
        PJoinVertexes(twp, mwp, pwp);
    }
    func_8C0BA75A(twp, pwp);
    func_8C0BA820(tp);
    if (pwp->htp != NULL) {
        func_8C0BD4CC(twp, pwp);
    }
    func_8C0925A0(twp, &pwp->shadow);
    if (!(twp->timer.b[3] & 8)) {
        func_8C0BF262(twp, pwp);
        CCL_Entry(twp);
        CWP(twp)->range = pwp->p.height + njScalor(&pwp->spd);
    }
    func_8C06C6F8(twp, mwp, pwp);
}
#else
ASM_STUB(void TailsMain(task *tp))
#endif
