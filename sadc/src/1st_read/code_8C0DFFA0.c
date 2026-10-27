/*
 * 1ST_READ 8C0DFFA0: BigMain, the exec of Big the Cat's player task
 * (chunk 8C0DFFA0-8C0E59C0, function 1/29; created by LoadPlayerTask
 * 8C04F3C0, also called by 8C0E7720 and 8C0E78C0).
 *
 * Per frame: ride flags from the collision work (not in modes 47/48), the
 * mode machine (BigModeMachine 8C0E0E70, skipped in mode 54), the requests
 * of free.l[0] (bit 0: start the task 8C0E6620, bit 1: set bit 15 of
 * B_8C7A40E8), the attack bits of Big's CCL_INFO shapes, then a switch on
 * twp->mode (0-54) running the physics steps of the current mode, then
 * motion, the Life Belt morph (equipment 0x1000) and the Power Rod model
 * (equipment 0x800), welds, display, held object, shadow and the
 * per-frame collision entry.
 *
 * Code generation notes (same situation as AmyMain, code_8C0D6B40.c):
 *  - every local lives on the stack (SHC does that for this very large
 *    function): tp @(16), twp @(12), mwp @(8), pwp @(4); the block-scope
 *    temporaries (child task, CCL_INFO pointer) share @(0);
 *  - the cases are written in the original source order (the order of
 *    their code), not in numeric order;
 *  - BigModeMachine is called with JSR in the original although it follows
 *    BigMain in the chunk, so it is only declared here;
 *  - case 0 needs "pwp->free.ptr[2] = B_8C7A40C8 = ctp;" (one load of ctp
 *    for both stores) and case 47 "y_top < pos.y + eyes_height" (pwp is
 *    loaded once and copied, MOV R2,R1);
 *  - the Life Belt morph calls need t as 4th parameter (BIG_MORPH below).
 *
 * Status: instruction-identical to the original. All 1462 instructions
 * match (tools/shcdiff.py), every instruction sits at the same offset and
 * the 55-entry switch jump table at 8C0E0158 has the same halfwords.
 * tools/shcdiff.py still reports the MOVA of the jump table (a ".DATA.W
 * Lcase-Lbase" item it cannot evaluate), hence the NON_MATCHING guard, as
 * for SonicMain, TailsMain, E102Main and AmyMain.
 */
#include "common.h"
#if !defined(TARGET_PC)
#define func_8C055EFE func_8C055EFE_engine_h    /* hide the shared prototype, see BIG_MORPH */
#endif
#include "sa1/sa1.h"
#if !defined(TARGET_PC)
#undef func_8C055EFE
#endif

/* Big data (extern only) */
extern CCL_INFO D_8C525BB4[];           /* Big's collision info (2 entries) */
extern NJS_OBJECT *D_8C525C1C[];        /* head, eyeL, eyeR (func_8C0E5D20) */
extern NJS_TEXLIST texlist_BIG;         /* 8C525FB0 */
extern char D_8C15CA94[];               /* "BIG" */
extern NJS_OBJECT D_8C529588[];         /* node whose sibling is the (Life Belt) part */
extern NJS_OBJECT Big_Life_Belt_Worn[]; /* 8C53232C */
extern NJS_OBJECT D_8C526E8C;           /* plain part */
extern NJS_POINT3 D_8C530C20[];         /* Life Belt morph: point arrays (a, b, dst) */
extern NJS_POINT3 D_8C531010[];
extern NJS_POINT3 D_8C531BB4[];
extern NJS_POINT3 D_8C530BD8[];
extern NJS_POINT3 D_8C530FC8[];
extern NJS_POINT3 D_8C531440[];
extern NJS_OBJECT D_8C52AD1C[];         /* object whose model/child are swapped by equipment & 0x800 */
extern NJS_MODEL D_8C532FD8;
extern NJS_OBJECT D_8C5328CC;
extern NJS_MODEL D_8C52ACF4;
extern NJS_OBJECT D_8C52A5E8;
extern task *B_8C7A40C8;                /* task of func_8C0E6DA0 (player 0 only) */
extern Sint32 B_8C7A40D0;
extern Sint32 B_8C7A40D4;
extern Sint16 B_8C7A40E8;

/* player common, not declared in sa1/player.h */
void func_8C06CA4E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F25C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F386(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C070EDA(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0711D2(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0725C0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07266E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C078308(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C078710(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C099750(char *name, NJS_TEXLIST *texlist);         /* texture load by name */

/*
 * BIG_MORPH: func_8C055EFE, the point array blend dst[i] = a[i] * (1 - t)
 * + b[i] * t of code_8C055ECE.c, declared in sa1/engine.h as
 * (Float t, a, b, dst, n). SHC evaluates call arguments right to left, and
 * BigMain's schedule of the two calls (FR4 loaded before the R7 chain) is
 * reproduced only when t is the 4th parameter: the original declaration
 * was (a, b, dst, t, n) (same registers: R4-R7 and FR4). Until the shared
 * prototype is changed, the SHC build declares that order here; the PC
 * build calls the header's prototype.
 */
#if !defined(TARGET_PC)
void func_8C055EFE(NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *dst, Float t, Sint32 n);
#define BIG_MORPH(t, a, b, dst, n) func_8C055EFE((a), (b), (dst), (t), (n))
#else
#define BIG_MORPH(t, a, b, dst, n) func_8C055EFE((t), (a), (b), (dst), (n))
#endif

/* Big (same chunk, called with JSR) */
void BigModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0E386E(task *tp);                                   /* dest */
void func_8C0E38AA(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0E3A40(task *tp);                                   /* disp */
void func_8C0E3C20(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0E3DE0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0E3EC0(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0E4C16(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0E5698(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0E58F6(taskwk *twp, motionwk *mwp, playerwk *pwp);
/* Big (other chunks) */
void func_8C0E59C0(task *tp);
void func_8C0E59FE(task *tp);
Sint32 func_8C0E5D20(Sint8 pno, NJS_OBJECT **objs);            /* look task setup (cf. Tails' func_8C0BE070) */
task *func_8C0E6620(task *tp);
task *func_8C0E6DA0(task *tp);
void func_8C0EB160(task *tp);

#ifdef NON_MATCHING
void BigMain(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = PWP(mwp);
    if ((twp->flag & 0x20) && mwp->spd.y <= 0.0f && twp->mode != 47 && twp->mode != 48) {
        twp->flag |= 2;
        twp->ang.x = CWP(twp)->mytask->twp->ang.x;
        twp->ang.z = CWP(twp)->mytask->twp->ang.z;
    } else {
        twp->flag &= ~2;
    }
    if (twp->mode != 54) {
        BigModeMachine(twp, mwp, pwp);
    }
    if (pwp != NULL && (pwp->free.l[0] & 1)) {
        task *ctp;

        ctp = func_8C0E6620(tp);
        pwp->free.ptr[1] = ctp;
        pwp->free.l[0] &= ~1;
    }
    if (pwp != NULL && (pwp->free.l[0] & 2)) {
        B_8C7A40E8 |= 0x8000;
        pwp->free.l[0] &= ~2;
    }
    if (twp->cwp != NULL) {
        CCL_INFO *info;

        info = CWP(twp)->info;
        if (pwp->item & 0x8000) {
            info->flag = (info->flag & ~3) | 3;
            info->flag = (info->flag & ~0xC) | (3 << 2);
        } else if (twp->mode == 51) {
            info->flag = (info->flag & ~3) | 0;
            info->flag = (info->flag & ~0xC) | (1 << 2);
        } else if (twp->flag & 0x400) {
            info[1].attr &= ~0x10;
            info->flag = (info->flag & ~3) | 0;
            info->flag = (info->flag & ~0xC) | (0 << 2);
        } else {
            info[1].attr |= 0x10;
            info->flag = (info->flag & ~3) | 0;
            info->flag = (info->flag & ~0xC) | (0 << 2);
        }
    }
    switch (twp->mode) {
    case 0:
        twp->counter.b[1] = SA1_CHR_BIG;
        PInitialize((Uint8)twp->counter.b[0], tp);
        pwp = PWP(mwp);
        pwp->mj.plactptr = BigActionList;
        pwp->mj.reqaction = 0;
        pwp->free.l[0] = 0;
        CCL_Init(tp, D_8C525BB4, 0, 1, 2);
        CWP(twp)->info->a = pwp->p.rad;
        twp->mode = 1;
        pwp->waittimer = 0;
        if ((GetStageNumber() & 0xFF00) == 0x1300) {
            func_8C099750(D_8C15CA94, &texlist_BIG);
        } else {
            func_8C098FAE(7);
        }
        func_8C0E5D20(twp->counter.b[0], D_8C525C1C);
        pwp->mj.pljvptr = BigWeldList;
        {
            task *ctp;

            ctp = CreateElementalTask(2, 3, func_8C0E59C0);
            if (ctp != NULL) {
                ctp->twp->counter.b[0] = twp->counter.b[0];
            }
            ctp = CreateElementalTask(3, 3, func_8C0E59FE);
            if (ctp != NULL) {
                ctp->twp->counter.b[0] = twp->counter.b[0];
            }
            if (twp->counter.b[0] == 0) {
                ctp = func_8C0E6DA0(tp);
                pwp->free.ptr[2] = B_8C7A40C8 = ctp;
                if (twp->counter.b[0] == 0) {
                    ctp = CreateElementalTask(2, 3, func_8C0EB160);
                }
            }
        }
        B_8C7A40D0 = 0;
        B_8C7A40D4 = 0;
        B_8C7A40E8 = 0;
        tp->dest = func_8C0E386E;
        tp->disp = func_8C0E3A40;
        break;
    case 1:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0E3C20(twp, mwp, pwp);
        break;
    case 2:
        if (pwp->mj.lastaction == 24 && pwp->mj.action != 24) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            func_8C0940DA(0x4B2, 0, 0, 0);
            twp->mode = 12;
            pwp->mj.reqaction = 22;
            func_8C07B320(twp);
            pwp->spd.x = -pwp->spd.x;
            pwp->spd.z = -pwp->spd.z;
        } else {
            func_8C079096(twp, mwp, pwp);
            func_8C0E3DE0(twp, mwp, pwp);
        }
        break;
    case 3:
        func_8C06F25C(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        func_8C0E3DE0(twp, mwp, pwp);
        break;
    case 4:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 5:
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 6:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 7:
        func_8C071656(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 8:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 9:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 10:
        if (pwp->mj.reqaction == 17) {
            func_8C070EDA(twp, mwp, pwp);
        } else {
            func_8C06F386(twp, mwp, pwp);
            if (func_8C06C880(twp) & 0x400) {
                func_8C094330(15, twp, 1, 0, 2);
            }
            if (pwp->mj.lastaction == 17) {
                func_8C0940DA(0x4B7, 0, 0, 0);
            }
        }
        func_8C07266E(twp, mwp, pwp);
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
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 13:
        func_8C071700(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 29:
    case 30:
        func_8C078710(twp, mwp, pwp);
        func_8C0E3EC0(twp, mwp, pwp);
        break;
    case 31:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 14:
        break;
    case 32:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 33:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 35:
    case 36:
    case 37:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 40:
        func_8C0E58F6(twp, mwp, pwp);
        func_8C071700(twp, mwp, pwp);
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
        func_8C0E58F6(twp, mwp, pwp);
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 47:
        func_8C0E58F6(twp, mwp, pwp);
        func_8C071700(twp, mwp, pwp);
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        if (pwp->shadow.y_top < twp->pos.y + pwp->p.eyes_height) {
            pwp->mj.reqaction = 92;
        }
        break;
    case 48:
        func_8C0E58F6(twp, mwp, pwp);
        func_8C071D40(twp, mwp, pwp);
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 49:
        func_8C071D40(twp, mwp, pwp);
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 15:
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 16:
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 17:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 18:
        func_8C06F386(twp, mwp, pwp);
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 20:
        func_8C06D136(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 19:
        if (pwp->mj.lastaction == 29 && pwp->mj.action != 29) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0E3DE0(twp, mwp, pwp);
        break;
    case 21:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 22:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 23:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 24:
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 25:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 26:
        func_8C06F386(twp, mwp, pwp);
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        if (func_8C06C880(twp) & 0x400) {
            func_8C094330(15, twp, 1, 0, 2);
        }
        break;
    case 27:
        func_8C071D40(twp, mwp, pwp);
        func_8C06D136(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 28:
        func_8C06CA4E(twp, mwp, pwp);
        break;
    case 50:
        pwp->htp->mwp->acc.x = pwp->htp->mwp->acc.y = pwp->htp->mwp->acc.z
            = pwp->mj.nframe * 0.01f;
        break;
    case 51:
        break;
    case 38:
        func_8C071700(twp, mwp, pwp);
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 52:
        func_8C04B9E0(twp);
        break;
    case 53:
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
    case 54:
        if (twp->flag & 3) {
            pwp->mj.reqaction = 100;
        }
        func_8C071D40(twp, mwp, pwp);
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    }
    PSetMotion(&pwp->mj);
    if (pwp->equipment & 0x1000) {
        D_8C529588->sibling = Big_Life_Belt_Worn;
        if (func_8C0E4C16(twp, mwp, pwp)) {
            if (pwp->free.f[7] < 1.0f) {
                pwp->free.f[7] += 0.049999998f;
            } else {
                pwp->free.f[7] = 1.0f;
            }
        } else {
            if (pwp->free.f[7] > 0.0f) {
                pwp->free.f[7] -= 0.049999998f;
            } else {
                pwp->free.f[7] = 0.0f;
            }
        }
        BIG_MORPH(pwp->free.f[7], D_8C530C20, D_8C531010, D_8C531BB4,
                  Big_Life_Belt_Worn->model->nbPoint);
        BIG_MORPH(pwp->free.f[7], D_8C530BD8, D_8C530FC8, D_8C531440,
                  Big_Life_Belt_Worn->child->model->nbPoint);
    } else {
        D_8C529588->sibling = &D_8C526E8C;
    }
    if (pwp->equipment & 0x800) {
        D_8C52AD1C->model = &D_8C532FD8;
        D_8C52AD1C->child = &D_8C5328CC;
    } else {
        D_8C52AD1C->model = &D_8C52ACF4;
        D_8C52AD1C->child = &D_8C52A5E8;
    }
    PJoinVertexes(twp, mwp, pwp);
    func_8C0E38AA(twp, mwp, pwp);
    func_8C0E3A40(tp);
    if (pwp->htp != NULL) {
        func_8C0E5698(twp, mwp, pwp);
    }
    func_8C0925A0(twp, &pwp->shadow);
    if (pwp->mj.action != 48) {
        CCL_Entry(twp);
    }
    func_8C06C6F8(twp, mwp, pwp);
}
#else
ASM_STUB(void BigMain(task *tp))
#endif
