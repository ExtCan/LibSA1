#include "sa1/pending.h"
#include "sa1/protos.h"

/*
 * 1ST_READ 8C0ED880-8C0EEAA0: Dr. Eggman's player code. The game creates this
 * task only from event overlays (EggmanMain: 14 EV*.PRS), never as the stage's
 * player. A reduced copy of Sonic's code, the counterpart of Tikal's
 * (src/overlays/tikal_prog/code_0CB00000.c, code_0CB00780.c).
 *  EggmanMain      exec: ride flags, the mode machine unless mode 16, a switch
 *                  on twp->mode (0-17), motion, welds, display, button 0x400
 *                  (mode 17), shadow, collision entry, per-frame update (tail)
 *  func_8C0EDD46   mode machine (modes 1-17)
 *  func_8C0EE420   dest (tp->dest): free the player slot, release resource 1
 *  func_8C0EE43C   display: called from the exec, tp->disp is never set
 *  func_8C0EE668   run action by speed (actions 4-8, work.f)
 *  func_8C0EE6D2   free movement (debug mode 17); the same code as Amy's
 *                  func_8C0DAE96 and Knuckles' func_8C0C5680
 *  func_8C0EE82E   jump check (mode 5, action 9, sound 17)
 *  func_8C0EE8B6   event lock helper
 *  func_8C0EE96C   request check: lock, path queue (mode 15); any TWF_REQUEST
 *                  just answers 1: there is no switch on smode, so no hurt state
 *  func_8C0EE9C0   airborne -> mode 5 (0x100 set) or 8, action 13
 *  func_8C0EEA20   no stick input and no speed -> stand (mode 1, action 0)
 *
 * What the game leaves broken stays: no hurt, no drown, no action button, no
 * jump pose. The second 0x400 test in EggmanMain (position 100, 0, -600) can
 * never fire: the first has just set mode 17.
 *
 * Status (round 0.9.9): seven functions MATCH. Guarded by NON_MATCHING, each
 * complete and proven by the strict references eggman_ec1, eggman_debug_ec1
 * (harness trace --cast, --route --check):
 *  - EggmanMain, func_8C0EDD46: instruction-identical (482, 719 instructions,
 *    every encoding, branch target and literal value equal). tools/shcdiff.py
 *    still reports the MOVA of the jump table (".DATA.W Lcase-Lbase" items it
 *    cannot evaluate), hence the guard, as for SonicMain and TikalMain.
 *  - func_8C0EE6D2: MATCHes in the whole translation unit (shcdiff with
 *    --flags=-define=NON_MATCHING); with the stubs above it the literal pools
 *    fall elsewhere and the long-branch forms change (Amy's func_8C0DAE96
 *    has the same note).
 *  - func_8C0EE9C0: identical but one register (R1 for the original's R3 in
 *    "MOV #13"): SHC's choice; if/else, goto, return and switch forms tried.
 *
 * Code generation notes: EggmanMain has no "default: break;" (with it PSetMotion
 * and PJoinVertexes get R2/R3 instead of R3/R2; TikalMain needs the opposite);
 * the mode machine keeps one "twp->flag &= ~0x2000; break;" per check and SHC
 * cross-jumps them; "if (c) { ...; break; } break;" as in SonicModeMachine.
 * Eggman differs from Tikal where it looks alike: flag &= ~0x3100 at the 0x400
 * press (Tikal 0x3000), the display's body centre is p.height * 0.5 (Tikal
 * p.center_height), lock 5's angle is ang[1] + ang.y, buttons come from pad 0.
 */

extern CCL_INFO D_8C57021C[];           /* Eggman's collision info (1 entry) */
extern NJS_TEXLIST D_8C570508;          /* Eggman's texlist */

/* player common, not declared in sa1/player.h (func_8C06CE40, func_8C06CEBE, func_8C06CF60, func_8C06CFCC: sa1/protos.h) */
Bool func_8C06CEFE(taskwk *twp, playerwk *pwp);
void func_8C06F25C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C070EDA(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0711D2(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0725C0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07266E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C078308(taskwk *twp, motionwk *mwp, playerwk *pwp);
/* reset the player to the stage start position (Sonic: code_8C0AEFA0.c) */
void func_8C04F8FA(taskwk *twp);

void func_8C0EDD46(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0EE420(task *tp);
void func_8C0EE43C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0EE668(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0EE6D2(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0EE82E(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0EE8B6(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0EE96C(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0EE9C0(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0EEA20(taskwk *twp, motionwk *mwp, playerwk *pwp);

#ifdef NON_MATCHING
void EggmanMain(task *tp)
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
    }
    if (twp->mode != 16) {
        func_8C0EDD46(twp, mwp, pwp);
    }
    switch (twp->mode) {
    case 0:
        twp->counter.b[1] = SA1_CHR_EGGMAN;
        PInitialize((Uint8)twp->counter.b[0], tp);
        pwp = PWP(mwp);
        pwp->mj.plactptr = EggmanActionList;
        pwp->mj.reqaction = 0;
        CCL_Init(tp, D_8C57021C, 0, 1, 1);
        CWP(twp)->info->a = pwp->p.rad;
        twp->mode = 1;
        func_8C098FAE(1);
        pwp->mj.pljvptr = EggmanWeldList;
        tp->dest = func_8C0EE420;
        break;
    case 1:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 2:
        if (pwp->mj.lastaction == 16 && pwp->mj.action != 16) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            twp->mode = 14;
            pwp->mj.reqaction = 22;
            func_8C07B320(twp);
            pwp->spd.x = -pwp->spd.x;
            pwp->spd.z = -pwp->spd.z;
            break;
        }
        func_8C079096(twp, mwp, pwp);
        func_8C0EE668(twp, mwp, pwp);
        break;
    case 3:
        func_8C06F25C(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        func_8C0EE668(twp, mwp, pwp);
        break;
    case 4:
        func_8C0714B6(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        break;
    case 5:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
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
            pwp->mj.reqaction = 16;
            func_8C071700(twp, mwp, pwp);
            twp->ang.y = mwp->ang_aim.y;
            break;
        }
        break;
    case 10:
        PGetAcceleration(twp, mwp, pwp);
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
        func_8C04B9E0(twp);
        break;
    case 16:
        func_8C071D40(twp, mwp, pwp);
        func_8C0714B6(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 17:
        func_8C0EE6D2(twp, mwp, pwp);
        break;
    }
    PSetMotion(&pwp->mj);
    PJoinVertexes(twp, mwp, pwp);
    func_8C0EE43C(twp, mwp, pwp);
    if ((B_8C754C08[0].press & 0x400) && twp->mode != 17) {
        twp->mode = 17;
        twp->flag &= ~0x3100;
    }
    if ((B_8C754C08[0].press & 0x400) && twp->mode != 17) {
        twp->mode = 17;
        twp->pos.x = 100.0f;
        twp->pos.y = 0.0f;
        twp->pos.z = -600.0f;
    }
    func_8C0925A0(twp, &pwp->shadow);
    CCL_Entry(twp);
    func_8C06C6F8(twp, mwp, pwp);
}
#else
ASM_STUB(void EggmanMain(task *tp))
#endif

#ifdef NON_MATCHING
void func_8C0EDD46(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (twp->wtimer == 0) {
        twp->flag &= 0x7FFF;
    } else {
        twp->wtimer--;
        twp->flag &= ~4;
    }
    switch (twp->mode) {
    case 1:
        if (func_8C0EE96C(twp, mwp, pwp)) break;
        if (func_8C0EE9C0(twp, mwp, pwp)) break;
        if (func_8C0EE82E(twp, mwp, pwp)) break;
        if (GetAnalog(twp, NULL, NULL) || pwp->spd.x != 0.0f) {
            twp->mode = 2;
            pwp->mj.reqaction = 4;
            break;
        }
        break;
    case 2:
        if (func_8C0EE96C(twp, mwp, pwp)) break;
        if (func_8C0EE9C0(twp, mwp, pwp)) break;
        if (func_8C0EE82E(twp, mwp, pwp)) break;
        if (func_8C0EEA20(twp, mwp, pwp)) break;
        if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && (CWP(twp)->flag & 8)
            && (CWP(twp)->hit_cwp->info[CWP(twp)->hit_num].b05 & 0xF)) {
            switch (func_8C06CF60(twp, mwp)) {
            case 4:
                twp->mode = 10;
                pwp->mj.reqaction = 17;
            }
        }
        if (func_8C06CE40(twp, mwp) && pwp->spd.x >= pwp->p.jog_speed) {
            twp->mode = 9;
            pwp->mj.reqaction = 15;
            break;
        }
        break;
    case 3:
        twp->flag &= ~0x500;
        if (func_8C0EE96C(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0EE9C0(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0EE82E(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 2;
            pwp->mj.reqaction = 4;
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0EEA20(twp, mwp, pwp)) {
            twp->flag &= ~0x2000;
            break;
        }
        break;
    case 4:
        if (func_8C0EE96C(twp, mwp, pwp)) {
            twp->flag &= ~0x500;
            twp->flag &= ~0x2000;
            break;
        }
        if (func_8C0EE9C0(twp, mwp, pwp)) {
            pwp->mj.reqaction = 9;
            pwp->work.f = 2.0f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            twp->flag &= ~0x2000;
            break;
        }
        if (pwp->spd.x < 1.84999993f) {
            pwp->mj.reqaction = 9;
            pwp->work.f = 2.0f;
            twp->ang.x = twp->ang.z = 0;
            twp->flag |= 0x500;
            twp->flag &= ~0x2000;
            break;
        }
        break;
    case 5:
        if (func_8C0EE96C(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 9;
                pwp->mj.reqaction = 15;
            } else if (func_8C0EEA20(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 1;
            } else {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                twp->mode = 2;
                func_8C0EE668(twp, mwp, pwp);
            }
            twp->flag &= ~0x500;
            break;
        }
        if (func_8C06CEFE(twp, pwp)) {
            SA1_FMAC_ACC(pwp->spd.y, pwp->p.jmp_addit, 0.79999997f);
        }
        if (pwp->mj.reqaction == 9 && mwp->spd.y <= 0.0f) {
            pwp->mj.reqaction = 12;
        }
        break;
    case 6:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 8;
            pwp->mj.reqaction = 12;
        }
        break;
    case 7:
        if (mwp->spd.y <= 0.0f) {
            twp->mode = 8;
            pwp->mj.reqaction = 12;
        }
        break;
    case 8:
        if (func_8C0EE96C(twp, mwp, pwp)) break;
        if (twp->flag & 3) {
            if (func_8C06CE40(twp, mwp) && pwp->spd.x > 0.0f) {
                twp->mode = 9;
                pwp->mj.reqaction = 15;
                break;
            }
            if (func_8C0EEA20(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 1;
                break;
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 2;
            func_8C0EE668(twp, mwp, pwp);
        }
        break;
    case 9:
        if (func_8C0EE96C(twp, mwp, pwp)) break;
        if (func_8C0EE9C0(twp, mwp, pwp)) break;
        if (func_8C0EE82E(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction != 15) {
            twp->mode = 1;
            pwp->mj.reqaction = 0;
        }
        break;
    case 10:
        if (func_8C0EE96C(twp, mwp, pwp)) break;
        if (func_8C0EE82E(twp, mwp, pwp)) break;
        if (func_8C0EE9C0(twp, mwp, pwp)) break;
        if (func_8C0EEA20(twp, mwp, pwp)) break;
        switch (func_8C06CFCC(twp, mwp)) {
        case 4:
            break;
        case 3:
            if (!(twp->flag & TWF_HOLD)) {
                twp->mode = 2;
                pwp->mj.reqaction = 4;
            }
            break;
        default:
            twp->mode = 2;
            pwp->mj.reqaction = 4;
            break;
        }
        break;
    case 11:
        if (func_8C0EE96C(twp, mwp, pwp)) break;
        if (twp->wtimer != 120 && (twp->flag & 3)) {
            if (func_8C0EEA20(twp, mwp, pwp)) {
                twp->ang.x = mwp->ang_aim.x;
                twp->ang.z = mwp->ang_aim.z;
                pwp->mj.reqaction = 1;
                break;
            }
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 2;
            func_8C0EE668(twp, mwp, pwp);
        }
        break;
    case 12:
        if (func_8C0EE96C(twp, mwp, pwp)) break;
        if (func_8C0EE9C0(twp, mwp, pwp)) break;
        if (func_8C0EE82E(twp, mwp, pwp)) break;
        if (func_8C0EEA20(twp, mwp, pwp)) break;
        if (pwp->spd.x >= 0.0f) {
            twp->mode = 2;
            func_8C0EE668(twp, mwp, pwp);
        }
        break;
    case 13:
        if (func_8C0EE96C(twp, mwp, pwp)) break;
        break;
    case 14:
        if (func_8C0EE9C0(twp, mwp, pwp)) break;
        if (pwp->mj.reqaction == 22) {
            break;
        }
        if (pwp->mj.reqaction != 23) {
            twp->mode = 1;
            pwp->mj.reqaction = 0;
        }
        break;
    case 15:
        if (EWP(twp)->list4C == NULL) {
            twp->ang.x = mwp->ang_aim.x;
            twp->ang.z = mwp->ang_aim.z;
            twp->mode = 1;
            pwp->mj.reqaction = 0;
        }
        break;
    case 17:
        if (B_8C754C08[0].press & 4) {
            twp->mode = 2;
            pwp->mj.reqaction = 4;
        }
        break;
    }
}
#else
ASM_STUB(void func_8C0EDD46(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C0EE420(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    func_8C06C5CA((Uint8)twp->counter.b[0]);
    func_8C098FFC(1);
}

/* The rotations are "?:" expressions (angle kept in @R15), as in Tikal's
 * display func_0CB008A0 and Sonic's func_8C0AE17A. */
void func_8C0EE43C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    CCL_INFO *info;
    Uint32 action;
    NJS_POINT3 v;
    NJS_POINT3 c;

    action = pwp->mj.reqaction;
    if (pwp->mj.mtnmode == 2) {
        action = pwp->mj.action;
    }
    if (!(twp->wtimer & 1)) {
        func_8C038CEC(2);
        njSetTexture(&D_8C570508);
        njPushMatrix(NULL);
        v.x = v.z = 0.0f;
        v.y = pwp->p.height * 0.5f;
        njPushMatrix(D_8C67C640);
        twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
        twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
        -twp->ang.y ? (njRotateY(NULL, -twp->ang.y), 0) : 0;
        njCalcVector(NULL, &v, &c);
        njPopMatrix(1);
        c.x += twp->pos.x;
        c.y += twp->pos.y;
        c.z += twp->pos.z;
        njTranslateV(NULL, &c);
        info = CWP(twp)->info;
        info->center.x = c.x;
        info->center.y = c.y;
        info->center.z = c.z;
        twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
        twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
        (0x8000 - twp->ang.y) ? (njRotateY(NULL, 0x8000 - twp->ang.y), 0) : 0;
        if (EWP(twp)->list40 != NULL) {
            func_8C04B57C(twp);
        } else if ((B_8C754C08[0].on & 0x400) && twp->mode == 17) {
            func_8C036F12((void *)pwp->mj.plactptr[pwp->mj.reqaction].actptr->object);
        } else if (pwp->mj.mtnmode != 2) {
            func_8C040942(pwp->mj.plactptr[action].actptr, pwp->mj.nframe);
        } else {
            func_8C040942(pwp->mj.actwkptr, pwp->mj.nframe);
        }
        njPopMatrix(1);
        func_8C038CEC(0);
    }
}

void func_8C0EE668(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float spd;

    spd = SA1_FMAC(mwp->height, 5.0f, pwp->spd.x);
    if (spd > pwp->p.dash_speed) {
        pwp->mj.reqaction = 8;
    } else if (spd > pwp->p.rush_speed) {
        pwp->mj.reqaction = 7;
    } else if (spd > pwp->p.run_speed) {
        pwp->mj.reqaction = 6;
    } else if (spd > pwp->p.jog_speed) {
        pwp->mj.reqaction = 5;
    } else {
        pwp->mj.reqaction = 4;
    }
    pwp->work.f = SA1_FMAC(mwp->height, 20.0f, pwp->spd.x);
}

/* The same code as Amy's func_8C0DAE96 (code_8C0DAC46.c) and Knuckles' func_8C0C5680:
 * the buttons of pad 0, the stick of the player's pad. */
#ifdef NON_MATCHING
void func_8C0EE6D2(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float fx;
    Float fy;
    Angle ang;

    if (B_8C754C08[0].press & 2) {
        func_8C04F8FA(twp);
    }
    if ((B_8C754C08[0].on & 0x400) ? TRUE : FALSE) {
        fx = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8);
        fy = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8);
        if (fx > 3072.0f || fx < -3072.0f || fy > 3072.0f || fy < -3072.0f) {
            if (fy != 0.0f) {
                fx = fy / njSqrt(fy * fy);
            } else {
                fx = 0.0f;
            }
            twp->pos.y -= fx * 5.0f;
        }
    } else {
        fx = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8);
        fy = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8);
        if (fx > 3072.0f || fx < -3072.0f || fy > 3072.0f || fy < -3072.0f) {
            ang = SA1_FTOI(atan2f(fy, fx) * 10430.381f) - camera_twp->ang.y;
            twp->pos.x SH_ONLY(+= 5.0f * njCos(ang)) PC_ONLY(= SA1_FMAC(5.0f, njCos(ang), twp->pos.x));
            twp->pos.z SH_ONLY(+= 5.0f * njSin(ang)) PC_ONLY(= SA1_FMAC(5.0f, njSin(ang), twp->pos.z));
        }
    }
}
#else
ASM_STUB(void func_8C0EE6D2(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

Sint32 func_8C0EE82E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    forcewk *fwp;

    if (func_8C06CEBE(twp, pwp)) {
        twp->mode = 5;
        if (pwp->ttp != NULL) {
            fwp = FWP(pwp->ttp);
            if (fwp != NULL) {
                SetAccelerationP(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
            }
        }
        pwp->spd.y = pwp->p.jmp_y_spd;
        pwp->mj.reqaction = 9;
        pwp->work.f = 2.0f;
        twp->flag &= ~2;
        twp->flag |= 0x500;
        pwp->jumptimer = 0;
        func_8C0940DA(17, 0, 0, 0);
        return 1;
    }
    return 0;
}

Sint32 func_8C0EE8B6(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    eventwk *ewp;

    ewp = EWP(twp);
    switch ((Uint8)ewp->lock) {
    case 2:
    case 3:
    case 4:
    case 1:
        twp->flag |= TWF_EVENTLOCK;
        break;
    case 6:
        func_8C07C298(twp->counter.b[0], ewp->ang[1]);
        break;
    case 5:
        func_8C07C298(twp->counter.b[0], ewp->ang[1] + twp->ang.y);
        break;
    default:
        twp->flag &= ~TWF_EVENTLOCK;
        return 0;
    }
    return 1;
}

Sint32 func_8C0EE96C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (EWP(twp)->lock) {
        func_8C0EE8B6(twp, mwp, pwp);
        return 0;
    }
    if (EWP(twp)->list4C != NULL) {
        twp->mode = 15;
        twp->flag &= ~0x500;
        twp->flag &= ~TWF_ONPATH;
        return 1;
    }
    if (!(twp->flag & TWF_REQUEST)) {
        return 0;
    }
    return 1;
}

#ifdef NON_MATCHING
Sint32 func_8C0EE9C0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!(twp->flag & TWF_GROUND)) {
        if (twp->flag & 0x100) {
            twp->mode = 5;
            return TRUE;
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 13;
            return TRUE;
        }
    }
    return FALSE;
}
#else
ASM_STUB(Sint32 func_8C0EE9C0(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

Sint32 func_8C0EEA20(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!GetAnalog(twp, NULL, NULL) && pwp->spd.x == 0.0f) {
        twp->mode = 1;
        pwp->mj.reqaction = 0;
        return 1;
    }
    return 0;
}
