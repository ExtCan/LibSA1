/*
 * 1ST_READ 8C0D6B40: AmyMain, the exec of Amy's player task
 * (chunk 8C0D6B40-8C0DCA6C, function 1/32; created by LoadPlayerTask
 * 8C04F3C0).
 *
 * Per frame: ride flags from the collision work, the mode machine
 * (AmyModeMachine 8C0D7960, BSR), then a switch on twp->mode (0-49) running
 * the physics steps of the current mode, then motion, the hammer part
 * (func_8C0DA662), welds, display, held object, shadow and the per-frame
 * collision entry.
 *
 * Code generation notes (same situation as SonicMain, code_8C0A77BC.c, and
 * E102Main, code_8C0CBE00.c):
 *  - every local lives on the stack (SHC does that for this very large
 *    function): tp @(16), twp @(12), mwp @(8), pwp @(4); the case 0 child
 *    task and the case 15 angle share @(0) (sibling block scopes);
 *  - the cases are written in the original source order (the order of
 *    their code), not in numeric order; SHC cross-jumps the identical
 *    "...; PSetPosition(); func_8C079096(); break;" tails, but not tails
 *    inside if/else arms: case 21 repeats PGetSpeed/PSetPosition/
 *    func_8C079096 in both arms (SHC merges them into one join that is not
 *    cross-jumped with case 47);
 *  - case 5/22 needs the nested "if (b[7] == 0 && reqaction == 95)
 *    { if (cwp && hit_cwp) ... }" (one && chain loads hit_cwp into R0
 *    instead of R1);
 *  - case 15 needs the empty else: with a plain if-then the FMOV.S store is
 *    not moved into the delay slot of the BRA, and with an early
 *    "if (... != 1) break;" the temporaries swap R2/R3.
 *
 * Status: instruction-identical to the original. All 1363 instructions
 * match (tools/shcdiff.py), every instruction up to 8C0D77C8 sits at the
 * same offset and the 50-entry switch jump table at 8C0D6C0C has the same
 * halfwords. tools/shcdiff.py still reports the MOVA of the jump table (a
 * ".DATA.W Lcase-Lbase" item it cannot evaluate), hence the NON_MATCHING
 * guard, as for SonicMain and E102Main. Byte layout: compiled with the
 * ASM_STUB of AmyModeMachine after it, SHC defers AmyMain's last literal
 * pool (8C0D7900-8C0D7944) past the stub, so the 21 pool loads from
 * 8C0D77CA on have larger displacements; with the real (large) AmyModeMachine
 * in the same translation unit the pool lands at 8C0D7900 as in the game.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"     /* func_8C0DD8DC, func_8C0DEDC0, func_8C0DF060 (canonical prototypes) */

/* Amy data (extern only) */
extern CCL_INFO D_8C4BB7F0[];           /* Amy's collision info (2 entries) */
extern NJS_OBJECT *D_8C4BB858[];        /* head / eyeL / eyeR objects passed to func_8C0DD8DC (cf. Sonic's D_8C1C5F08) */

/* player common, not declared in sa1/player.h */
void func_8C06CA4E(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C06CE40(taskwk *twp, motionwk *mwp);
void func_8C06F25C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F386(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C070EDA(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0711D2(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0725C0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07266E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C078308(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C0784F8(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C078710(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C107700(void);

/* Amy (same chunk: BSR to the mode machine, JSR to the others) */
void AmyModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0DA106(task *tp);                                   /* dest */
void func_8C0DA122(task *tp);                                   /* disp */
void func_8C0DA560(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0DA662(playerwk *pwp);                              /* hammer part by equipment & 0x80 */
void func_8C0DA6E8(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0DA8DA(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0DA9A0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0DAC46(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0DAE96(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0DB430(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0DC7DE(taskwk *twp, motionwk *mwp, playerwk *pwp);
/* Amy (other chunks) */
void func_8C0DCCA8(taskwk *twp, playerwk *pwp);
void func_8C0DD0DE(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0DD16E(task *tp);
void func_8C0DD490(task *tp);
void func_8C0DDDB2(task *tp);
void func_8C0DE022(void);
void func_8C0DE22E(void);
void func_8C0DF0D0(void);
Sint32 func_8C0DFE86(Angle *ang);                               /* 1: *ang = stick angle (atan2f) */

#ifdef NON_MATCHING
void AmyMain(task *tp)
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
    if (D_8C19B468 == 1) {
        twp->mode = 49;
    } else if (twp->mode != 47) {
        AmyModeMachine(twp, mwp, pwp);
    }
    switch (twp->mode) {
    case 0:
        twp->counter.b[1] = SA1_CHR_AMY;
        PInitialize((Uint8)twp->counter.b[0], tp);
        pwp = PWP(mwp);
        pwp->mj.plactptr = AmyActionList;
        pwp->mj.reqaction = 0;
        CCL_Init(tp, D_8C4BB7F0, 0, 1, 2);
        CWP(twp)->info->a = pwp->p.rad;
        twp->mode = 1;
        func_8C098FAE(5);
        pwp->mj.pljvptr = AmyWeldList;
        func_8C0DD8DC(twp->counter.b[0], D_8C4BB858);
        {
            task *ctp;

            ctp = CreateElementalTask(2, 3, func_8C0DDDB2);
            if (ctp != NULL) {
                ctp->twp->counter.b[0] = twp->counter.b[0];
            }
            ctp = CreateElementalTask(3, 3, func_8C0DD16E);
            if (ctp != NULL) {
                ctp->twp->counter.b[0] = twp->counter.b[0];
            }
            ctp = CreateElementalTask(3, 3, func_8C0DD490);
            if (ctp != NULL) {
                ctp->twp->counter.b[0] = twp->counter.b[0];
            }
        }
        pwp->free.f[6] = 0.0f;
        pwp->free.f[7] = 0.0f;
        pwp->free.b[5] = 0;
        pwp->free.b[6] = 0;
        tp->dest = func_8C0DA106;
        tp->disp = func_8C0DA122;
        func_8C0DEDC0();
        func_8C107700();
        func_8C0DE022();
        pwp->free.b[4] = 0;
        func_8C0DF060();
        func_8C0DE22E();
        break;
    case 1:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0DA6E8(twp, mwp, pwp);
        break;
    case 2:
        if (pwp->mj.lastaction == 21 && pwp->mj.action != 21) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            twp->mode = 14;
            pwp->mj.reqaction = 30;
            func_8C07B320(twp);
            pwp->spd.x = -pwp->spd.x;
            pwp->spd.z = -pwp->spd.z;
        } else {
            func_8C079096(twp, mwp, pwp);
            func_8C0DA8DA(twp, mwp, pwp);
        }
        break;
    case 3:
        func_8C06F25C(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        func_8C0DA8DA(twp, mwp, pwp);
        break;
    case 4:
        func_8C0714B6(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        break;
    case 5:
    case 22:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        if (pwp->free.b[7] == 0 && pwp->mj.reqaction == 95) {
            if (twp->cwp != NULL && CWP(twp)->hit_cwp != NULL) {
                if (pwp->equipment & 0x80) {
                    func_8C0940DA(0x2F1, 0, 0, 0);
                } else {
                    func_8C0940DA(0x2F0, 0, 0, 0);
                }
                pwp->free.b[7] = 1;
            }
        }
        break;
    case 6:
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 7:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C06E904(twp, mwp, pwp);
        }
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
        if (pwp->spd.x <= 0.0f && func_8C06CE40(twp, mwp)) {
            twp->mode = 2;
            pwp->mj.reqaction = 21;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
        }
        break;
    case 10:
        if (pwp->mj.reqaction == 22) {
            func_8C070EDA(twp, mwp, pwp);
        } else {
            func_8C06F386(twp, mwp, pwp);
            if (func_8C06C880(twp) & 0x400) {
                func_8C094330(15, twp, 1, 0, 2);
            }
            if (pwp->mj.lastaction == 22) {
                func_8C0940DA(0x498, 0, 0, 0);
            }
        }
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 11:
        func_8C071D40(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
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
        break;
    case 14:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 15:
        {
            Angle ang;

            if (func_8C0DFE86(&ang) == 1) {
                pwp->mj.nframe = (Float)(Uint16)ang * 0.00048828125f;
            } else {
                ;       /* empty else: needed for the FMOV.S in the BRA delay slot */
            }
        }
        break;
    case 16:
        func_8C0DB430(twp, mwp, pwp);
        break;
    case 19:
        func_8C071700(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 20:
        if (pwp->mj.reqaction == 87 || pwp->mj.reqaction == 88) {
            func_8C0DD0DE(twp, mwp, pwp);
            PGetAcceleration(twp, mwp, pwp);
            PGetSpeed(twp, mwp, pwp);
        } else if (pwp->mj.reqaction == 84 || pwp->mj.reqaction == 85) {
            func_8C071700(twp, mwp, pwp);
        }
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C094330(0x2F2, twp, 1, 0, 2);
        break;
    case 21:
        if (pwp->mj.reqaction >= 90 && pwp->mj.reqaction <= 93) {
            func_8C071D40(twp, mwp, pwp);
            if (!func_8C07D7B4(twp, mwp, pwp)) {
                func_8C06E904(twp, mwp, pwp);
            }
            PGetSpeed(twp, mwp, pwp);
            PSetPosition(twp, mwp, pwp);
            func_8C079096(twp, mwp, pwp);
        } else {
            func_8C071700(twp, mwp, pwp);
            PGetAcceleration(twp, mwp, pwp);
            PGetSpeed(twp, mwp, pwp);
            PSetPosition(twp, mwp, pwp);
            func_8C079096(twp, mwp, pwp);
        }
        break;
    case 17:
    case 18:
        break;
    case 38:
    case 39:
        func_8C0784F8(twp, mwp, pwp);
        func_8C0DA9A0(twp, mwp, pwp);
        break;
    case 40:
    case 41:
        func_8C078710(twp, mwp, pwp);
        func_8C0DAC46(twp, mwp, pwp);
        break;
    case 42:
        twp->flag &= ~2;
        func_8C05E7D4(twp, mwp, pwp);
        break;
    case 43:
        break;
    case 44:
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
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 25:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 26:
        func_8C06F386(twp, mwp, pwp);
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 28:
        func_8C06D136(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 27:
        if (pwp->mj.lastaction == 46 && pwp->mj.action != 46) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0DA8DA(twp, mwp, pwp);
        break;
    case 29:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 30:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 31:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 32:
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 33:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 34:
        func_8C06F386(twp, mwp, pwp);
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 35:
        func_8C071D40(twp, mwp, pwp);
        func_8C06D136(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 36:
        func_8C06CA4E(twp, mwp, pwp);
        break;
    case 37:
        pwp->htp->mwp->acc.x = pwp->htp->mwp->acc.y = pwp->htp->mwp->acc.z
            = pwp->mj.nframe * 0.01f;
        break;
    case 45:
        func_8C04B9E0(twp);
        break;
    case 46:
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
    case 47:
        if (twp->flag & 3) {
            pwp->mj.reqaction = 27;
        }
        func_8C071D40(twp, mwp, pwp);
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 48:
        break;
    case 49:
        func_8C0DAE96(twp, mwp, pwp);
        break;
    }
    PSetMotion(&pwp->mj);
    func_8C0DA662(pwp);
    PJoinVertexes(twp, mwp, pwp);
    func_8C0DA122(tp);
    func_8C0DA560(twp, mwp, pwp);
    if (pwp->htp != NULL) {
        func_8C0DC7DE(twp, mwp, pwp);
    }
    func_8C0925A0(twp, &pwp->shadow);
    func_8C0DCCA8(twp, pwp);
    if (pwp->mj.reqaction != 42) {
        CCL_Entry(twp);
    }
    func_8C06C6F8(twp, mwp, pwp);
    func_8C0DF0D0();
    pwp->free.b[6] = 0;
}
#else
ASM_STUB(void AmyMain(task *tp))
#endif

ASM_STUB(void AmyModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp))
