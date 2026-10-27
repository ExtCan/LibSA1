/*
 * TIKAL_PROG 0CB00000: TikalMain (func_0CB00000), the exec of Tikal's player
 * task, and TikalModeMachine (func_0CB003A0) (chunk 0CB00000-0CB00F60,
 * functions 1-2 of 14). Tikal's code is a reduced copy of Sonic's
 * (SonicMain code_8C0A77BC.c, SonicModeMachine code_8C0A966C.c).
 *
 * TikalMain: ride flags from the collision work, the mode machine (BSR)
 * unless mode 10, a switch on twp->mode (0-11) running the physics steps of
 * the current mode, then motion, welds, func_0CB007A0, the display
 * func_0CB008A0 (also stored as tp->disp in mode 0), button 0x400 (mode 11),
 * shadow, collision entry and the per-frame player update (tail call).
 * TikalModeMachine: mode/action transitions for modes 1-11.
 *
 * Code generation notes:
 *  - TikalMain case 1 ends with the inlined static TikalStandAction (Sonic's
 *    func_8C0AE3D6 there): the inliner leaves the parameter copy
 *    "MOV R13,R4" and stores reqaction through R4. SHC drops the static
 *    function itself, as in the original.
 *  - TikalMain needs "default: break;" (as E102Main, code_8C0CBE00.c):
 *    without it PSetMotion/PJoinVertexes get R3/R2 instead of R2/R3.
 *  - TikalModeMachine follows SonicModeMachine's idioms: "if (c) { ...;
 *    break; } break;", the else-if chains of cases 3/6 (only the reqaction
 *    store is cross-jumped with case 1, at 0CB00656), one
 *    "twp->flag &= ~0x2000" per check in case 2 (SHC then keeps ~0x2000 in
 *    R10 and cross-jumps the four copies), "if (c) {...}" in cases 4/5 (with
 *    "if (!c) break;" the reqaction stores are merged across cases).
 *
 * Status (both NON_MATCHING):
 *  - TikalMain: instruction-identical (358 instructions, every encoding,
 *    branch target and literal value equal; the 12 jump-table entries reach
 *    the same instructions). tools/shcdiff.py still reports the MOVA of the
 *    jump table (".DATA.W Lcase-Lbase" items it cannot evaluate), hence the
 *    guard, as for SonicMain/AmyMain/E102Main.
 *  - TikalModeMachine: identical except one branch form: at 0CB006AA (case
 *    7, after func_0CB00B64) the original has "BT +6; BRA end; NOP" where
 *    SHC now emits "BF end" (distance 188 bytes, in range). Everything else
 *    up to there matches, and after it only the displacements shift by 4
 *    bytes. Tried: ||-chains, else-if chains, explicit breaks, case 8/9/11
 *    rewrites, default/case 10 labels, a real func_0CB00780 body after it,
 *    and inlined stand helpers in cases 7/9 (those flip the branch but leave
 *    "MOV R13,R4" copies and change the R8/R9 or constant registers). An
 *    explicit case 10 or default block before case 11 also flips it (so the
 *    original's estimate saw a few more bytes between 0CB006AA and the end).
 *    Also the MOVA of the jump table, as above.
 *  - The last literal of TikalModeMachine (B_8C754C08[0].press) lives in the
 *    pool after func_0CB007A0 (0CB00860) in the game; shcdiff compares the
 *    value only.
 */
#include "common.h"
#include "sa1/sa1.h"

/* Tikal data (TIKAL_PROG, extern only) */
extern PL_ACTION D_0CB01400[];                  /* Tikal's action list (9 entries) */
extern NJS_OBJECT *D_0CB01490[];                /* 3 objects passed to func_0CB00F60 */
extern CCL_INFO D_0CB0149C[];                   /* Tikal's collision info (1 entry) */
extern PL_JOIN_VERTEX TIKAL_PROG_TikalWeldList[];

/* player common, not declared in sa1/player.h */
Bool func_8C06CE40(taskwk *twp, motionwk *mwp);
Bool func_8C06CEFE(taskwk *twp, playerwk *pwp);
void func_8C070EDA(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0711D2(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* Tikal, other chunk (JSR) */
Bool func_0CB00F60(Sint8 pno, NJS_OBJECT **objs);

/* Tikal, this chunk (BSR/BRA; ASM_STUB bodies below) */
void func_0CB003A0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_0CB00780(task *tp);                                   /* dest */
void func_0CB007A0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_0CB008A0(task *tp);                                   /* disp */
void func_0CB00994(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_0CB00A40(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_0CB00B64(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_0CB00C20(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_0CB00CD2(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_0CB00E74(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_0CB00E90(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_0CB00ED4(task *tp);

#ifdef NON_MATCHING
/* Inlined by SHC (-speed) and absent from the binary; cf. Sonic's
 * func_8C0AE3D6 (stand action selector) and func_8C0A9660 (back to standing). */
static void TikalStandAction(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    pwp->mj.reqaction = 0;
}

void func_0CB00000(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    task *ctp;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = PWP(mwp);
    if ((twp->flag & 0x20) && mwp->spd.y <= 0.0f) {
        twp->flag |= 2;
        twp->ang.x = CWP(twp)->mytask->twp->ang.x;
        twp->ang.z = CWP(twp)->mytask->twp->ang.z;
    }
    if (twp->mode != 10) {
        func_0CB003A0(twp, mwp, pwp);
    }
    switch (twp->mode) {
    case 0:
        twp->counter.b[1] = SA1_CHR_TIKAL;
        PInitialize((Uint8)twp->counter.b[0], tp);
        pwp = PWP(mwp);
        pwp->mj.plactptr = D_0CB01400;
        pwp->mj.reqaction = 0;
        CCL_Init(tp, D_0CB0149C, 0, 1, 1);
        CWP(twp)->info->a = pwp->p.rad;
        twp->mode = 1;
        func_8C098FAE(4);
        func_0CB00F60(twp->counter.b[0], D_0CB01490);
        pwp->mj.pljvptr = TIKAL_PROG_TikalWeldList;
        ctp = CreateElementalTask(2, 3, func_0CB00ED4);
        if (ctp != NULL) {
            ctp->twp->counter.b[0] = twp->counter.b[0];
        }
        tp->dest = func_0CB00780;
        tp->disp = func_0CB008A0;
        break;
    case 1:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        TikalStandAction(twp, mwp, pwp);
        break;
    case 2:
        if (pwp->mj.lastaction == 8 && pwp->mj.action != 8) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_0CB00994(twp, mwp, pwp);
        break;
    case 3:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 4:
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 5:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
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
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 8:
        break;
    case 9:
        func_8C04B9E0(twp);
        break;
    case 10:
        func_8C071D40(twp, mwp, pwp);
        func_8C0714B6(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 11:
        func_0CB00A40(twp, mwp, pwp);
        break;
    default:
        break;
    }
    PSetMotion(&pwp->mj);
    PJoinVertexes(twp, mwp, pwp);
    func_0CB007A0(twp, mwp, pwp);
    func_0CB008A0(tp);
    if ((B_8C754C08[(Uint8)twp->counter.b[0]].press & 0x400) && twp->mode != 11) {
        twp->mode = 11;
        twp->flag &= ~0x3000;
    }
    func_8C0925A0(twp, &pwp->shadow);
    CCL_Entry(twp);
    func_8C06C6F8(twp, mwp, pwp);
}
#else
ASM_STUB(void func_0CB00000(task *tp))
#endif

#ifdef NON_MATCHING
void func_0CB003A0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    switch (twp->mode) {
    case 1:
        if (func_0CB00CD2(twp, mwp, pwp)) break;
        if (func_0CB00E74(twp, mwp, pwp)) break;
        if (func_0CB00B64(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL)) {
            twp->mode = 2;
            pwp->mj.reqaction = 1;
            break;
        }
        break;
    case 2:
        if (func_0CB00CD2(twp, mwp, pwp)) {
            break;
        }
        if (func_0CB00E74(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_0CB00B64(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_0CB00E90(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (!func_8C06CE40(twp, mwp)) {
            break;
        }
        if (pwp->spd.x < pwp->p.jog_speed) {
            break;
        }
        twp->mode = 7;
        pwp->mj.reqaction = 7;
        func_8C0940DA(18, 0, 0, 0);
        twp->flag &= ~0x2000;
        break;
    case 3:
        if (func_0CB00CD2(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 7;
                pwp->mj.reqaction = 7;
            } else if (func_0CB00E90(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 6;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_0CB00994(twp, mwp, pwp);
            }
            break;
        }
        if (func_0CB00C20(twp, mwp, pwp)) break;
        if (func_8C06CEFE(twp, pwp)) {
            SA1_FMAC_ACC(pwp->spd.y, pwp->p.jmp_addit, 0.79999997f);
        }
        break;
    case 4:
        if (func_0CB00CD2(twp, mwp, pwp)) {
            break;
        }
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 6;
            pwp->mj.reqaction = 4;
        }
        break;
    case 5:
        if (func_0CB00CD2(twp, mwp, pwp)) {
            break;
        }
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 6;
            pwp->mj.reqaction = 4;
        }
        break;
    case 6:
        if (func_0CB00CD2(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(33, 0, 0, 0);
            }
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 7;
                pwp->mj.reqaction = 7;
            } else if (func_0CB00E90(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 6;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_0CB00994(twp, mwp, pwp);
            }
        }
        break;
    case 7:
        if (func_0CB00CD2(twp, mwp, pwp)) {
            break;
        }
        if (func_0CB00E74(twp, mwp, pwp)) {
            break;
        }
        if (func_0CB00B64(twp, mwp, pwp)) {
            break;
        }
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 2;
            pwp->mj.reqaction = 8;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        if (pwp->mj.reqaction != 7) {
            twp->mode = 1;
            pwp->mj.reqaction = 0;
        }
        break;
    case 8:
        if (func_0CB00CD2(twp, mwp, pwp)) {
            break;
        }
        break;
    case 9:
        if (EWP(twp)->list4C == NULL) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 1;
            pwp->mj.reqaction = 0;
        }
        break;
    case 11:
        if (B_8C754C08[(Uint8)twp->counter.b[0]].press & 4) {
            twp->mode = 2;
            pwp->mj.reqaction = 1;
        }
        break;
    }
}
#else
ASM_STUB(void func_0CB003A0(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

ASM_STUB(void func_0CB00780(task *tp))
ASM_STUB(void func_0CB007A0(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_0CB008A0(task *tp))
ASM_STUB(void func_0CB00994(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_0CB00A40(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(Sint32 func_0CB00B64(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(Sint32 func_0CB00C20(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(Sint32 func_0CB00CD2(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(Sint32 func_0CB00E74(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(Sint32 func_0CB00E90(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_0CB00ED4(task *tp))
