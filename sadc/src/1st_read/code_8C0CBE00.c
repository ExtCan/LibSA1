/*
 * 1ST_READ 8C0CBE00: E102Main, the exec of E-102 Gamma's player task
 * (chunk 8C0CBE00-8C0D2080, function 1/28; created by LoadPlayerTask
 * 8C04F3C0 and 8C083F7C).
 *
 * Per frame: frame flags B_8C7A2EA8/A9/C0/C1, ride flags from the collision
 * work, the E-102 work update 8C0D317E, the mode machine (E102ModeMachine
 * 8C0CD0A8), the attack bits of both CCL_INFO shapes and the collision
 * height, then a switch on twp->mode (0-55) running the physics steps of the
 * current mode, then motion, welds, the node matrices of the gun/arm parts,
 * the laser/target code, the ground distance below E-102 and the per-frame
 * collision entry.
 *
 * Code generation notes (same situation as SonicMain, code_8C0A77BC.c):
 *  - every local lives on the stack (SHC does that for this very large
 *    function); slots are shared only between sibling block scopes, so
 *    the temporaries are declared in blocks (flag, a/b/info, w share
 *    @(8,R15)..@(0,R15));
 *  - the cases are in the original source order (the order of their code),
 *    not in numeric order; SHC cross-jumps the identical
 *    "PSetPosition(); func_8C079096(); break;" tails into the last case;
 *  - mode 9 needs the nested "if (lastaction == 11) { if (B_8C7A2EA8)"
 *    (with "&&" pos.y is read through a copied base register), and the
 *    "default: break;" changes the register choice right after the switch
 *    (B_8C7A2EBC in R3, ewk in R2);
 *  - B_8C7A2EAC is one NJS_POINT3COL: its .col is stored with
 *    MOV.L R2,@-R0 from the address of .tex.
 *
 * Status: instruction-identical to the original. All 1944 instructions
 * match (tools/shcdiff.py), every instruction sits at the same offset (the
 * in-function literal pools fall at the same places), and the 56-entry
 * switch jump table at 8C0CC010 has the same halfwords. tools/shcdiff.py
 * still reports two items it cannot evaluate: the MOVA of the jump table
 * (".DATA.W Lcase-Lbase" entries) and the JMP pool word of the
 * out-of-range path (a local label compared byte-wise with code whose
 * pool displacements differ, see below). Hence the NON_MATCHING guard, as
 * for SonicMain (code_8C0A77BC.c) and func_8C0D079A.
 * Byte layout: the last literal pool of E102Main (8C0CD100-8C0CD170) lies
 * inside E102ModeMachine (after func_8C0CD09C and E102ModeMachine's first
 * BRA): SHC deferred it into the next function's pool, so the 34 pool
 * loads after 8C0CCF00 differ by a constant displacement when E102Main is
 * compiled alone. A byte-exact splice needs E102Main, func_8C0CD09C and
 * E102ModeMachine compiled in one translation unit.
 */

#include "common.h"
#include "sa1/sa1.h"

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
 * E-102 work (pwp->free.ptr[0]; CAlloc(1, 0x244) in case 0 below). No
 * shared type yet: this is the full layout merged from the E-102 files
 * (code_8C0CFACE.c, code_8C0D2EC0.c, code_8C0D32A0.c, code_8C0D3C00.c,
 * code_8C0D46CE.c, code_8C0D5A00.c) without padding arrays, so that it has
 * the same native layout as theirs on 64-bit hosts (see the type notes).
 */
typedef struct {
    Sint8 mode;                 /* 0x00 laser/targeting state (E102Main: MOV.B, == 3) */
    Sint8 smode;                /* 0x01 targeting arm state 0..5 */
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
    task *tp;                   /* 0x6C child task (DestroyTask'd by func_8C0CFA6E) */
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
    Float altitude;             /* 0x238 twp->pos.y - ground_y (mode 43 lands below 40.0f) */
    Sint32 unk23C;              /* 0x23C func_8C0D278A(twp); bit 1 tested by 8C0CD0A8 */
    Float step_dist;            /* 0x240 0.0f at init; func_8C0D24E0 adds |spd.x| */
} E102WK;                       /* 0x244 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_OFFSET(E102_LINE, p3c, 0x10);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, timer08, 0x08);
SA1_ASSERT_OFFSET(E102WK, flag, 0x16);
SA1_ASSERT_OFFSET(E102WK, target_tp, 0x30);
SA1_ASSERT_OFFSET(E102WK, tp, 0x6C);
SA1_ASSERT_OFFSET(E102WK, arm_action, 0x80);
SA1_ASSERT_OFFSET(E102WK, spd_y, 0xAC);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);
SA1_ASSERT_OFFSET(E102WK, line1, 0xD0);
SA1_ASSERT_OFFSET(E102WK, lock_tp, 0xF4);
SA1_ASSERT_OFFSET(E102WK, shot_tp, 0x174);
SA1_ASSERT_OFFSET(E102WK, analog, 0x1F4);
SA1_ASSERT_OFFSET(E102WK, ground_y, 0x234);
SA1_ASSERT_OFFSET(E102WK, altitude, 0x238);
SA1_ASSERT_OFFSET(E102WK, unk23C, 0x23C);
SA1_ASSERT_OFFSET(E102WK, step_dist, 0x240);

/* E-102 frame flags (bss 8C7A2EA8-8C7A2EC1, read by the E-102 chunks) */
extern Sint8 B_8C7A2EA8;                /* positional SE enable: !func_8C049A4E() */
extern Sint8 B_8C7A2EA9;                /* set by mode 44; copied to B_8C7A2EC0 */
extern Sint8 B_8C7A2EAA;                /* 1: the task 8C0D6198 was started */
extern NJS_POINT3COL B_8C7A2EAC;        /* 2-point line (bss splits it at +8/+12); drawn at 8C0D3BA2 (chunk 8C0D32A0) with (&B_8C7A2EAC, 1, 64), presumably njDrawLine3D */
extern E102WK *B_8C7A2EBC;              /* E-102 work of this frame */
extern Sint8 B_8C7A2EC0;
extern Sint8 B_8C7A2EC1;                /* func_8C08AB60() */
extern NJS_MATERIAL *B_8C7A3048;        /* material being patched (case 0 leaves D_8C42BD14) */

/* node matrices of E-102's model (func_8C058434) */
extern NJS_MATRIX B_8C7A2EC8;
extern NJS_MATRIX B_8C7A2F08;
extern NJS_MATRIX B_8C7A2F88;
extern NJS_MATRIX B_8C7A2FC8;
extern NJS_MATRIX B_8C7A3008;
/* E-102 data (extern only) */
extern CCL_INFO D_8C422B80[];           /* E-102's collision info (2 entries) */
extern NJS_POINT3 D_8C422C60[];         /* 2 points */
extern NJS_COLOR D_8C422C78[];          /* 2 colours */
extern NJS_MATERIAL D_8C4269AC[];
extern NJS_MATERIAL D_8C4265E8[];
extern NJS_MATERIAL D_8C42616C[];
extern NJS_MATERIAL D_8C42A994[];
extern NJS_MATERIAL D_8C42BD14[];

/* player common, not declared in sa1/player.h */
void func_8C06CA4E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F25C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F386(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0711D2(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0725C0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07266E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C078308(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C078710(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C08AB60(void);
Float func_8C09222C(taskwk *twp, Float r);
/* E-102 (same chunk or the following ones, all reached through JSR) */
void E102ModeMachine(task *tp, motionwk *mwp, playerwk *pwp);
void func_8C0CFA6E(task *tp);                                   /* dest */
void func_8C0CFACE(task *tp);                                   /* disp */
Sint32 func_8C0D06C6(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0D1672(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D1C78(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D1D76(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0D1E38(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0D1FC8(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0D203A(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* -> PGetAcceleration */
void func_8C0D2080(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* -> func_8C06E904 */
void func_8C0D2086(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0D2150(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* -> func_8C070EDA */
void func_8C0D2156(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* -> func_8C071700 */
void func_8C0D2210(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0D23E4(taskwk *twp, playerwk *pwp);
void func_8C0D24E0(taskwk *twp, playerwk *pwp, E102WK *ewk);
void func_8C0D2538(taskwk *twp, E102WK *ewk);
void func_8C0D26E0(taskwk *twp, playerwk *pwp, E102WK *ewk);
Uint32 func_8C0D278A(taskwk *twp);
void func_8C0D2C00(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0D317E(E102WK *ewk, Sint32 pno, Sint32 flag);
void func_8C0D3220(E102WK *ewk, Sint32 pno);
void func_8C0D33A0(taskwk *twp, playerwk *pwp, E102WK *ewk);
void func_8C0D46CE(task *tp, playerwk *pwp, E102WK *ewk);
void func_8C0D5552(E102_LINE *a, E102_LINE *b);
void func_8C0D6198(task *tp);
void func_8C0D68FC(void);
void func_8C0D6A74(void);

#ifdef NON_MATCHING
void E102Main(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    E102WK *ewk;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = PWP(mwp);
    ewk = NULL;
    B_8C7A2EC0 = B_8C7A2EA9;
    B_8C7A2EA9 = 0;
    B_8C7A2EA8 = !func_8C049A4E();
    B_8C7A2EC1 = func_8C08AB60();
    if (mwp != NULL) {
        if ((twp->flag & 0x20) && mwp->spd.y <= 0.0f) {
            twp->flag |= 2;
            twp->ang.x = CWP(twp)->mytask->twp->ang.x;
            twp->ang.z = CWP(twp)->mytask->twp->ang.z;
        } else {
            twp->flag &= ~2;
        }
        if (pwp != NULL) {
            Sint32 flag;

            ewk = (E102WK *)pwp->free.ptr[0];
            flag = ewk->mode == 3;
            func_8C0D317E(ewk, (Uint8)twp->counter.b[0], flag);
            if (D_8C19B468 == 1) {
                twp->mode = 56;
            } else if (twp->mode != 51) {
                E102ModeMachine(tp, mwp, pwp);
                func_8C0D23E4(twp, pwp);
            }
        }
    }
    if (twp->cwp != NULL) {
        Sint32 a;
        Sint32 b;
        CCL_INFO *info;

        info = CWP(twp)->info;
        if (pwp->item & 0x8000) {
            a = 3;
            b = 3;
        } else if (twp->mode == 53) {
            a = 0;
            b = 1;
        } else {
            a = 0;
            b = 0;
        }
        info->flag = (info->flag & ~3) | a;
        info->flag = (info->flag & ~0xC) | (b << 2);
        info[1].flag = (info[1].flag & ~3) | a;
        info[1].flag = (info[1].flag & ~0xC) | (b << 2);
        if (twp->mode >= 31 && twp->mode <= 40) {
            pwp->p.height = 10.0f;
        } else {
            pwp->p.height = 21.0f;
        }
    }
    switch (twp->mode) {
    case 0:
        twp->counter.b[1] = SA1_CHR_E102;
        PInitialize((Uint8)twp->counter.b[0], tp);
        pwp = PWP(mwp);
        pwp->mj.plactptr = GammaActionList;
        pwp->mj.reqaction = 0;
        CCL_Init(tp, D_8C422B80, 0, 1, 2);
        CWP(twp)->info->a = pwp->p.rad;
        func_8C098FAE(6);
        pwp->mj.pljvptr = GammaWeldList;
        tp->dest = func_8C0CFA6E;
        tp->disp = func_8C0CFACE;
        ewk = (E102WK *)CAlloc(1, sizeof(E102WK));
        pwp->free.ptr[0] = ewk;
        pwp->free.w[2] = 0;     /* DC 0x80, E-102 timer (8C0CD0A8); on 64-bit hosts
                                   w[2] overlaps ptr[0]: needs a free.e102 view */
        func_8C0D5552(&ewk->line0, &ewk->line1);
        ewk->step_dist = 0.0f;
        B_8C7A2EAC.p = D_8C422C60;
        B_8C7A2EAC.tex = NULL;
        B_8C7A2EAC.num = 2;
        B_8C7A2EAC.col = D_8C422C78;

        twp->mode = 1;
        func_8C0D68FC();
        B_8C7A3048 = D_8C4269AC;
        B_8C7A3048->attrflags &= 0x03FFFFFF;
        B_8C7A3048->attrflags |= 0x80100000;
        B_8C7A3048 = D_8C4265E8;
        B_8C7A3048->attrflags &= 0x03FFFFFF;
        B_8C7A3048->attrflags |= 0x80100000;
        B_8C7A3048->diffuse.argb.a = 0xFF;
        B_8C7A3048 = D_8C42616C;
        B_8C7A3048->attrflags &= 0x03FFFFFF;
        B_8C7A3048->attrflags |= 0x80100000;
        B_8C7A3048->diffuse.argb.a = 0xFF;
        B_8C7A3048 = D_8C42A994;
        B_8C7A3048->attrflags &= 0x03FFFFFF;
        B_8C7A3048->attrflags |= 0x80100000;
        B_8C7A3048->diffuse.argb.a = 0xFF;
        B_8C7A3048 = D_8C42BD14;
        B_8C7A3048->attrflags &= 0x03FFFFFF;
        B_8C7A3048->attrflags |= 0x80100000;
        B_8C7A3048->diffuse.argb.a = 0xFF;
        B_8C7A2EAA = 0;
        if (twp->id == 1 && func_8C049A4E() != 1
                && ((GetStageNumber() & 0xFF00) >> 8) <= 25) {
            B_8C7A2EAA = 1;
            CreateChildTask(2, func_8C0D6198, tp);
        }
        func_8C0D6A74();
        twp->flag |= 1;
        break;
    case 1:
    case 29:
        func_8C0D2156(twp, mwp, pwp);
        func_8C0D203A(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0D1E38(twp, mwp, pwp);
        break;
    case 2:
        if (pwp->mj.lastaction == 19 && pwp->mj.action != 19) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        func_8C0D203A(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            pwp->spd.x = -pwp->spd.x;
            pwp->spd.z = -pwp->spd.z;
        } else {
            func_8C079096(twp, mwp, pwp);
            func_8C0D1FC8(twp, mwp, pwp);
        }
        func_8C0D24E0(twp, pwp, ewk);
        break;
    case 31:
        if (pwp->mj.lastaction == 60 && pwp->mj.action != 60) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        func_8C0D2086(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            pwp->spd.x = -pwp->spd.x;
            pwp->spd.z = -pwp->spd.z;
        } else {
            func_8C079096(twp, mwp, pwp);
        }
        break;
    case 33:
        if (pwp->mj.lastaction == 19 && pwp->mj.action != 19) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        func_8C0D2086(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            func_8C07B320(twp);
            pwp->spd.x = -pwp->spd.x;
            pwp->spd.z = -pwp->spd.z;
        } else {
            func_8C079096(twp, mwp, pwp);
        }
        pwp->spd.x = (pwp->spd.x < -pwp->p.run_speed) ? -pwp->p.run_speed
                   : (pwp->spd.x > pwp->p.run_speed) ? pwp->p.run_speed : pwp->spd.x;
        break;
    case 3:
        func_8C06F25C(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        func_8C0D1FC8(twp, mwp, pwp);
        break;
    case 4:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C0D2080(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 7;
        }
        break;
    case 35:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C0D2080(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        func_8C0D1D76(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 36:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C0D2080(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        func_8C0D1D76(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 37:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C0D2080(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        if (func_8C0D06C6(twp, mwp, pwp)) {
            twp->mode = 7;
            pwp->mj.reqaction = 8;
        }
        break;
    case 5:
        func_8C0D2080(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 6:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C0D2080(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 7:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C0D2080(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        if (func_8C0D1C78(twp, mwp, pwp) == 1) {
            if (pwp->mj.reqaction != 48 && pwp->mj.reqaction != 49) {
                pwp->mj.reqaction = 48;
            }
        } else {
            pwp->mj.reqaction = 8;
        }
        ((E102WK *)pwp->free.ptr[0])->spd_y = pwp->spd.y;
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 8:
    case 39:
        func_8C0D2150(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 9:
        if (pwp->mj.reqaction == 11) {
            func_8C0D2150(twp, mwp, pwp);
        } else {
            func_8C06F386(twp, mwp, pwp);
            func_8C094330(15, twp, 1, 0, 2);
            if (pwp->mj.lastaction == 11) {
                if (B_8C7A2EA8) {
                    func_8C09413A(0x4A8, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
                }
            }

        }
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 10:
        func_8C071D40(twp, mwp, pwp);
        func_8C0D203A(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 11:
        func_8C0D2150(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 12:
        func_8C0D2150(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 13:
        func_8C0D2156(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 14:
    case 52:
        break;
    case 15:
        func_8C0D2156(twp, mwp, pwp);
        func_8C0D203A(twp, mwp, pwp);
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
        func_8C0D2156(twp, mwp, pwp);
        func_8C0D203A(twp, mwp, pwp);
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
        if (pwp->mj.lastaction == 24 && pwp->mj.action != 24) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        func_8C0D203A(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0D1FC8(twp, mwp, pwp);
        func_8C0D24E0(twp, pwp, ewk);
        break;
    case 32:
        if (pwp->mj.lastaction == 24 && pwp->mj.action != 24) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        func_8C0D203A(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 34:
        if (pwp->mj.lastaction == 24 && pwp->mj.action != 24) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        func_8C0D203A(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 21:
    case 40:
        func_8C0D2150(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 22:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C0D2080(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        func_8C0D1C78(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 23:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C0D2080(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 38:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C0D2080(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 24:
        func_8C0D2080(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 25:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C0D2080(twp, mwp, pwp);
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
        func_8C094330(15, twp, 1, 0, 2);
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
    case 54:
        pwp->htp->mwp->acc.x = pwp->htp->mwp->acc.y = pwp->htp->mwp->acc.z
            = pwp->mj.nframe * 0.01f;
        break;
    case 41:
        pwp->mj.reqaction = 6;
        twp->mode = 42;
        break;
    case 42:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C0D2080(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 43;
            pwp->mj.reqaction = 8;
        }
        break;
    case 43:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C0D2080(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        if (pwp->spd.y < 0.0f && ewk->altitude < 40.0f) {
            pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
            pwp->mj.reqaction = 48;
            twp->mode = 44;
        }
        break;
    case 44:
        func_8C071700(twp, mwp, pwp);
        B_8C7A2EA9 = 1;
        break;
    case 45:
        pwp->mj.reqaction = 3;
        twp->mode = 31;
        func_8C0D2538(twp, ewk);
        break;
    case 46:
        twp->mode = 39;
        pwp->mj.reqaction = 47;
        if (B_8C7A2EA8) {
            func_8C09413A(0x310, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        break;
    case 47:
    case 48:
        func_8C078710(twp, mwp, pwp);
        func_8C0D2210(twp, mwp, pwp);
        break;
    case 53:
        break;
    case 55:
        func_8C04B9E0(twp);
        break;
    case 49:
    case 51:
        func_8C071D40(twp, mwp, pwp);
        func_8C0714B6(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    default:
        break;
    }
    B_8C7A2EBC = ewk;

    PSetMotion(&pwp->mj);
    PJoinVertexes(twp, mwp, pwp);
    {
        E102WK *w;

        w = (E102WK *)pwp->free.ptr[0];
        func_8C0D3220(w, (Uint8)twp->counter.b[0]);
    }
    func_8C058434(54, B_8C7A2EC8);
    func_8C058434(30, B_8C7A2F08);
    func_8C058434(39, B_8C7A2F88);
    njRotateZ(B_8C7A2F88, 0x4000);
    func_8C058434(2, B_8C7A3008);
    func_8C058434(37, B_8C7A2FC8);
    func_8C0D26E0(twp, pwp, ewk);
    func_8C0CFACE(tp);
    func_8C0D33A0(twp, pwp, (E102WK *)pwp->free.ptr[0]);
    func_8C0D46CE(tp, pwp, (E102WK *)pwp->free.ptr[0]);
    if (pwp->htp != NULL) {
        func_8C0D1672(twp, mwp, pwp);
    }
    {
        E102WK *w;

        w = (E102WK *)pwp->free.ptr[0];
        w->ground_y = func_8C09222C(twp, 1.5f);
        w->altitude = twp->pos.y - w->ground_y;
        w->unk23C = func_8C0D278A(twp);
        w->timer08++;
    }
    if ((pwp->mj.reqaction == 5 || pwp->mj.reqaction == 53) && pwp->spd.x > 3.5f) {
        func_8C0D2C00(twp, mwp, pwp);
    }
    if (pwp->mj.action != 67 && pwp->mj.action != 68) {
        CCL_Entry(twp);
    }
    func_8C06C6F8(twp, mwp, pwp);
    func_8C098E98(tp);
}
#else
ASM_STUB(void E102Main(task *tp))
#endif
