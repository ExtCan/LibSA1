#include "sa1/sa1.h"

/*
 * E-102 Gamma targeting arm / shot (1ST_READ chunk 8C0D3C00-8C0D4B00,
 * functions 11-13 of 13; the first ten are still assembly):
 *
 *  func_8C0D46CE  per-frame targeting arm state machine (E102Main 8C0CBE00
 *                 with (tp, pwp, ewk)), on ewk->smode:
 *                 0 init, 1 arm follows the nearest locked target
 *                 (8C0D3C00 / 8C0D454A pick one of the six arm actions
 *                 D_8C422D90); when targets are locked and ewk->mode is 1:
 *                 2 creates the shot group task 8C0D520A, marks every
 *                 lock-on marker's target (clears its SET condition bits
 *                 8C091B8A, counts it with 8C0D32A0) and stores the group
 *                 task in the marker; 3 re-aims; 4 after 15 frames fires
 *                 at the nearest target: the shot task 8C0D3DA0 at the arm
 *                 point / direction, plus the muzzle effects 8C0D49B8;
 *                 5 recoil of the body (body_y) and back to 3.
 *                 Every frame the lock-on counts are cleared.
 *  func_8C0D4996  releases a shot task: twp->mode 2, no target, optional
 *                 new speed (callers 8C083818, 8C085660, 8C0D5944).
 *  func_8C0D49B8  muzzle flash tasks 8C0D439E / 8C0D43DA at the arm point,
 *                 two smoke effects 8C0D6870 at the muzzle points with a
 *                 speed of 1 along the reversed facing, and pad vibration
 *                 for player 1 outside events (also E102ModeMachine).
 */

/*
 * E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2: CAlloc(1, 0x244)).
 * Head only, up to the lock-on list used here (field names follow
 * code_8C0CFACE.c; the shared type is still to be defined, see the
 * type notes). Only reached through a pointer.
 */
typedef struct {
    Sint8 mode;                 /* 0x00 1: shooting allowed (MOV.B + CMP/EQ #1) */
    Sint8 smode;                /* 0x01 targeting arm state 0..5 (this file) */
    Sint8 nb_target;            /* 0x02 valid markers (func_8C0D3C00) */
    Sint8 unk03;                /* 0x03 */
    Sint8 nb_lock;              /* 0x04 lock-on markers registered this frame (MOV.B, cleared every frame) */
    Sint8 nb_shot;              /* 0x05 entries of a shot list at 0x174 (code_8C0D3C00.c), cleared every frame */
    Sint8 arm_idx;              /* 0x06 index into D_8C422D90 (MOV.B, 8C0D454A result) */
    Sint8 unk07;                /* 0x07 */
    Sint16 timer08;             /* 0x08 */
    Sint16 timer0A;             /* 0x0A */
    Sint16 timer0C;             /* 0x0C */
    Sint16 unk0E;               /* 0x0E */
    Sint16 unk10;               /* 0x10 */
    Sint16 lockon_timer;        /* 0x12 */
    Sint16 unk14;               /* 0x14 */
    Sint16 flag;                /* 0x16 */
    Float hover_y;              /* 0x18 */
    Sint32 unk1C;               /* 0x1C */
    Angle3 jet_ang;             /* 0x20 */
    Angle jet_phase;            /* 0x2C */
    task *target_tp;            /* 0x30 */
    NJS_POINT3 laser_pos;       /* 0x34 */
    NJS_POINT3 laser_vec;       /* 0x40 */
    Float range;                /* 0x4C */
    Float range2;               /* 0x50 */
    NJS_POINT3 arm_pos;         /* 0x54 shot start point */
    NJS_POINT3 arm_vec;         /* 0x60 shot speed (copied to the shot's mwp->spd) */
    task *tp;                   /* 0x6C child task */
    Float arm_frame;            /* 0x70 arm frame / state timer */
    Sint32 unk74;               /* 0x74 */
    Sint32 unk78;               /* 0x78 */
    Sint32 unk7C;               /* 0x7C */
    NJS_ACTION *arm_action;     /* 0x80 targeting arm action (D_8C422D90[arm_idx]) */
    Float scope_frame;          /* 0x84 */
    Angle blink_ang;            /* 0x88 */
    Angle scope_ang;            /* 0x8C */
    Float body_y;               /* 0x90 recoil offset (0 or 1 - (frame - 15) / 15) */
    NJS_POINT3 muzzle[2];       /* 0x94 muzzle points (smoke effects) */
    Float spd_y;                /* 0xAC */
    Sint32 line0[8];            /* 0xB0 E102_LINE (code_8C0CFA6E.c), not used here */
    Sint32 line1[8];            /* 0xD0 E102_LINE */
    Sint32 unkF0;               /* 0xF0 */
    task *lock_tp[32];          /* 0xF4 lock-on marker tasks (func_8C0D577C), nb_lock entries (< 32: 8C0D38F6) */
} E102WK;                       /* head only; the real work is 0x244 bytes */

SA1_ASSERT_OFFSET(E102WK, nb_lock, 0x04);
SA1_ASSERT_OFFSET(E102WK, arm_idx, 0x06);
SA1_ASSERT_OFFSET(E102WK, arm_pos, 0x54);
SA1_ASSERT_OFFSET(E102WK, arm_vec, 0x60);
SA1_ASSERT_OFFSET(E102WK, arm_frame, 0x70);
SA1_ASSERT_OFFSET(E102WK, arm_action, 0x80);
SA1_ASSERT_OFFSET(E102WK, body_y, 0x90);
SA1_ASSERT_OFFSET(E102WK, muzzle, 0x94);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);
SA1_ASSERT_OFFSET(E102WK, lock_tp, 0xF4);

/* awp of a lock-on marker task func_8C0D577C (CAlloc(1, 0x34), code_8C0D37F4.c) */
typedef struct {
    Sint8 mode;                 /* 0x00 1: target marked for the shot */
    Sint8 count;                /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint16 equipment;           /* 0x04 */
    Sint16 unk06;               /* 0x06 */
    task *target_tp;            /* 0x08 */
    taskwk *ptwp;               /* 0x0C */
    E102WK *ewk;                /* 0x10 */
    task *shot_tp;              /* 0x14 shot task 8C0D3DA0 fired at this target (0 at creation) */
    Sint32 unk18[5];            /* 0x18 */
    task *group_tp;             /* 0x2C shot group task 8C0D520A */
    TaskFuncPtr target_exec;    /* 0x30 */
} E102_LOCKWK;                  /* 0x34 */

SA1_ASSERT_SIZE(E102_LOCKWK, 0x34);
SA1_ASSERT_OFFSET(E102_LOCKWK, target_tp, 0x08);
SA1_ASSERT_OFFSET(E102_LOCKWK, shot_tp, 0x14);
SA1_ASSERT_OFFSET(E102_LOCKWK, group_tp, 0x2C);

extern NJS_ACTION *D_8C422D90[];        /* six targeting arm actions */
extern NJS_POINT3 D_8C15C24C;           /* { 0, 0, 0 } (initialiser constant) */

void func_8C0D3DA0(task *tp);           /* shot exec (same chunk) */
void func_8C0D439E(task *tp);           /* muzzle flash exec (same chunk) */
void func_8C0D43DA(task *tp);           /* muzzle flash exec (same chunk) */
void func_8C0D520A(task *tp);           /* shot group exec */
Sint32 func_8C0D32A0(task *tp);         /* lock-on count of a target */
void func_8C091B8A(task *tp);           /* tp->ocp->ssCondition &= ~0x8001 */
void func_8C0D6870(NJS_POINT3 *pos, NJS_POINT3 *vec, Float scl);   /* smoke effect */
void func_8C109EAC(Sint32 pno, Sint32 a, Sint32 b, Sint32 c);      /* pad vibration */

/*
 * Same chunk, still assembly. func_8C0D3C00 and func_8C0D454A are BSR
 * callees; func_8C0D3DA0/439E/43DA are only stubbed for the literal pool
 * layout: in the original TU func_8C0D454A leaves a pending literal
 * (0x0000E000, 8C0D468E) that SHC dumps after the first BRA of
 * func_8C0D46CE (pool 8C0D4740); the next pool then lands after case 4
 * (8C0D4900), which is what turns four case-4 branches to the common exit
 * into BF/BRA long-branch pairs. Five stubs in front of func_8C0D46CE
 * (their ___asm_stub_sink literal plays the part of 0x0000E000) reproduce
 * both pool positions; 0-4 or 6-7 stubs do not.
 */
ASM_STUB(task *func_8C0D3C00(taskwk *twp, E102WK *ewk, Sint32 flag))
ASM_STUB(void func_8C0D3DA0(task *tp))
ASM_STUB(void func_8C0D439E(task *tp))
ASM_STUB(void func_8C0D43DA(task *tp))
ASM_STUB(Sint32 func_8C0D454A(taskwk *twp, taskwk *ttwp))

void func_8C0D49B8(task *tp, playerwk *pwp, E102WK *ewk);

/*
 * Matching notes: case 4 needs "+=" (the reload of arm_frame then shares
 * FR3 with the sum) and "if (ntp != NULL) {...}"; the lock-on loop needs
 * "if (!func_8C0D32A0()) continue;" and case 5 the ">= 24" block with its
 * own break. The usual "< x: break" spellings compile to the same
 * instructions with R2/R3 and FR2/FR3 swapped.
 */
void func_8C0D46CE(task *tp, playerwk *pwp, E102WK *ewk)
{
    taskwk *twp;
    task *ctp;
    task *ntp;
    E102_LOCKWK *lwp;
    Sint32 i;
    task **pp;
    Sint32 cnt;

    twp = tp->twp;
    switch (ewk->smode) {
    case 0:
        ewk->body_y = 0.0f;
        ewk->smode = 1;
        /* fall through */
    case 1:
        ctp = func_8C0D3C00(twp, ewk, 0);
        ewk->arm_idx = (ctp != NULL) ? func_8C0D454A(twp, ctp->twp) : 0;
        ewk->arm_action = D_8C422D90[ewk->arm_idx];
        if (ctp == NULL || ewk->nb_lock <= 0 || ewk->mode != 1) {
            break;
        }
        ewk->smode = 2;
        /* fall through */
    case 2:
        ewk->arm_frame = 14.0f;
        cnt = 0;
        pp = ewk->lock_tp;
        ntp = CreateElementalTask(2, 2, func_8C0D520A);
        if (ntp == NULL) {
            break;
        }
        for (i = ewk->nb_lock; i > 0; i--) {
            ctp = *pp++;
            lwp = (E102_LOCKWK *)ctp->awp;
            if (lwp->shot_tp != NULL) {
                continue;
            }
            if (lwp->target_tp == NULL) {
                continue;
            }
            if (lwp->target_tp->ocp != NULL) {
                func_8C091B8A(lwp->target_tp);
            } else if (lwp->target_tp->ptp->ocp != NULL) {
                func_8C091B8A(lwp->target_tp->ptp);
            }
            if (lwp->target_tp->twp == NULL) {
                continue;
            }
            lwp->mode = 1;
            if (!func_8C0D32A0(lwp->target_tp)) {
                continue;
            }
            cnt++;
            lwp->group_tp = ntp;
        }
        ntp->twp->timer.b[0] = cnt;
        /* fall through */
    case 3:
        ewk->body_y = 0.0f;
        ctp = func_8C0D3C00(twp, ewk, 1);
        if (ctp != NULL) {
            ewk->arm_idx = func_8C0D454A(twp, ctp->twp);
            ewk->arm_action = D_8C422D90[ewk->arm_idx];
            ewk->smode = 4;
        } else {
            ewk->smode = 0;
            break;
        }
        /* fall through */
    case 4:
        ewk->arm_frame += 1.0f;
        if (ewk->arm_frame < 15.0f) {
            break;
        }
        ewk->smode = 5;
        ctp = func_8C0D3C00(twp, ewk, 1);
        if (ctp == NULL) {
            break;
        }
        lwp = (E102_LOCKWK *)ctp->awp;
        if (lwp->target_tp == NULL) {
            break;
        }
        if (lwp->target_tp->exec == NULL) {
            break;
        }
        ntp = CreateElementalTask(3, 6, func_8C0D3DA0);
        if (ntp != NULL) {
            ntp->twp->counter.b[0] = tp->twp->counter.b[0];
            ntp->twp->pos = ewk->arm_pos;
            ntp->mwp->spd = ewk->arm_vec;
            ntp->twp->value.tp = ctp;
            lwp->shot_tp = ntp;
            func_8C0D49B8(tp, pwp, ewk);
        }
        break;
    case 5:
        ewk->arm_frame = ewk->arm_frame + 1.0f;
        ewk->body_y = 1.0f - (ewk->arm_frame - 15.0f) * 0.066666664f;
        if (func_8C0D3C00(twp, ewk, 1) != NULL) {
            if (ewk->arm_frame >= 24.0f) {
                ewk->arm_frame = 10.0f;
                ewk->smode = 3;
                break;
            }
            break;
        }
        if (ewk->arm_frame < 30.0f) {
            break;
        }
        ewk->arm_frame = 0.0f;
        ewk->smode = 3;
        break;
    }
    ewk->nb_shot = 0;
    ewk->nb_lock = 0;
}

void func_8C0D4996(task *tp, NJS_POINT3 *v)
{
    tp->twp->mode = 2;
    tp->twp->value.tp = NULL;
    if (v != NULL) {
        tp->mwp->spd = *v;
    }
}

void func_8C0D49B8(task *tp, playerwk *pwp, E102WK *ewk)
{
    taskwk *twp;
    task *ctp;
    Angle ang;
    Float spd;
    NJS_POINT3 v;

    v = D_8C15C24C;
    twp = tp->twp;
    ctp = CreateElementalTask(2, 3, func_8C0D439E);
    if (ctp != NULL) {
        ctp->twp->pos = ewk->arm_pos;
        ctp->twp->timer.b[0] = tp->twp->counter.b[0];
        ctp->twp->wtimer = pwp->equipment;
    }
    ang = 0x8000 - twp->ang.y;
    ctp = CreateElementalTask(2, 3, func_8C0D43DA);
    spd = 1.0f;
    if (ctp != NULL) {
        ctp->twp->pos = ewk->arm_pos;
        ctp->twp->ang.y = ang;
    }
    v.x = njCos(ang) * spd;
    v.z = njSin(ang) * spd;
    func_8C0D6870(&ewk->muzzle[0], &v, 0.19999999f);
    func_8C0D6870(&ewk->muzzle[1], &v, 0.19999999f);
    if (twp->id == 1 && func_8C049A4E() != 1) {
        func_8C109EAC(0, 4, 15, 0);
    }
}
