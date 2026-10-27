#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0D32A0), laser lock-on:
 *
 *  func_8C0D32A0  Bool: the task has a taskwk, a colliwk, and its first
 *                 shape lacks attr 0x100000 (still a valid target).
 *  func_8C0D32C0  tp->dest hook installed on lockable targets by
 *                 func_8C0D332C: when a locked target (flag 0x1000) is
 *                 destroyed while still valid, notifies 8C0D5064 with the
 *                 marker's task; releases the marker (8C0D5D90) and
 *                 chains to the original dest saved in the target work.
 *  func_8C0D332C  hooks tp->dest of every task of a colliwk list
 *                 (B_8C745908/B_8C745B08, called by 8C0D38C6) that has a
 *                 target work and whose first shape is not 0xC-flagged.
 *  func_8C0D33A0  laser state machine (E102Main, args twp, pwp, ewk):
 *                 the laser button (D_8C19E38C) starts the lock-on task
 *                 8C0D38C6 (SE 0x316), fires laser tasks 8C0D3A20 along
 *                 laser_pos/laser_vec while held (180 frames), then
 *                 releases the locked targets (8C0D5DF6); also runs the
 *                 scope frame 0..19.
 *  func_8C0D36C4  squared distance from the laser origin to a colliwk's
 *                 first shape if it is inside the laser's reach (laser_len2)
 *                 and aim: within 50 units 30 degrees (0x1555) around the
 *                 laser yaw, farther 12 degrees (0x888) and at most 60 units
 *                 from the laser line; else 0 (8C0D37F4 picks the nearest).
 *
 * Matching notes: the two counted loops (func_8C0D332C, case 4 of
 * func_8C0D33A0) only get the original's "BRA to the test" shape with
 * "continue"s (a plain for loop gets a guard test); func_8C0D332C also needs
 * the separate counter i = n (SHC then keeps 12 in R13). func_8C0D36C4
 * needs the call argument twp in a temporary and the fabs result in a
 * variable (FMOV FR0,FR4), which also restores the far form of the second
 * early return.
 */

/* E-102 line draw list (0x20 bytes, see code_8C0CFA6E.c); layout only */
typedef struct {
    NJS_POINT3 *src_p;          /* 0x00 */
    NJS_COLOR *src_col;         /* 0x04 */
    Sint16 *idx;                /* 0x08 */
    Sint16 num;                 /* 0x0C */
    Sint16 pad0E;               /* 0x0E */
    NJS_POINT3COL p3c;          /* 0x10 */
} E102_LINE;                    /* 0x20 */

/*
 * E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2: CAlloc(1, 0x244)).
 * Layout merged from code_8C0CFACE.c / code_8C0D24E0.c and this chunk
 * (0x04 nb_lockon, 0x10 laser_timer, 0x4C/0x50 laser length, 0xF4
 * lockon_tp). Local copy until a shared E102WK exists (see type notes).
 */
typedef struct {
    Sint8 mode;                 /* 0x00 laser state (func_8C0D33A0) */
    Sint8 smode;                /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint8 nb_lockon;            /* 0x04 entries in lockon_tp (MOV.B; 8C0D38C6 stops at 32) */
    Sint8 pad05[3];             /* 0x05 */
    Sint16 timer08;             /* 0x08 */
    Sint16 timer0A;             /* 0x0A lock-on counter (8C0D38C6: ++, max 99) */
    Sint16 timer0C;             /* 0x0C */
    Sint16 unk0E;               /* 0x0E */
    Sint16 laser_timer;         /* 0x10 180 at laser start, -- in mode 3 */
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
    Float laser_len;            /* 0x4C 0 at init; 250.0f by the laser task 8C0D3A20 */
    Float laser_len2;           /* 0x50 laser_len squared (8C0D3A20) */
    NJS_POINT3 arm_pos;         /* 0x54 */
    NJS_POINT3 arm_vec;         /* 0x60 */
    task *tp;                   /* 0x6C lock-on task (func_8C0D38C6) */
    Float arm_frame;            /* 0x70 */
    Sint32 unk74;               /* 0x74 */
    Sint32 unk78;               /* 0x78 */
    Sint32 unk7C;               /* 0x7C */
    NJS_ACTION *arm_action;     /* 0x80 */
    Float scope_frame;          /* 0x84 0..19 */
    Angle blink_ang;            /* 0x88 */
    Angle scope_ang;            /* 0x8C */
    Float body_y;               /* 0x90 */
    NJS_POINT3 muzzle[2];       /* 0x94 */
    Sint32 unkAC;               /* 0xAC */
    E102_LINE line0;            /* 0xB0 */
    E102_LINE line1;            /* 0xD0 */
    Sint32 unkF0;               /* 0xF0 */
    task *lockon_tp[32];        /* 0xF4 lock-on marker tasks */
    Sint32 unk174[46];          /* 0x174 */
    Sint32 unk22C;              /* 0x22C */
    Float unk230;               /* 0x230 */
    Float ground_y;             /* 0x234 */
    Float altitude;             /* 0x238 */
    Sint32 unk23C;              /* 0x23C */
    Float step_dist;            /* 0x240 */
} E102WK;                       /* 0x244 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, nb_lockon, 0x04);
SA1_ASSERT_OFFSET(E102WK, timer0A, 0x0A);
SA1_ASSERT_OFFSET(E102WK, laser_timer, 0x10);
SA1_ASSERT_OFFSET(E102WK, lockon_timer, 0x12);
SA1_ASSERT_OFFSET(E102WK, laser_pos, 0x34);
SA1_ASSERT_OFFSET(E102WK, laser_vec, 0x40);
SA1_ASSERT_OFFSET(E102WK, laser_len, 0x4C);
SA1_ASSERT_OFFSET(E102WK, laser_len2, 0x50);
SA1_ASSERT_OFFSET(E102WK, tp, 0x6C);
SA1_ASSERT_OFFSET(E102WK, scope_frame, 0x84);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);
SA1_ASSERT_OFFSET(E102WK, lockon_tp, 0xF4);
SA1_ASSERT_OFFSET(E102WK, unk22C, 0x22C);
SA1_ASSERT_OFFSET(E102WK, step_dist, 0x240);

/* awp of the lock-on task func_8C0D38C6 (CAlloc'd here) */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 pad01[3];             /* 0x01 */
    taskwk *ptwp;               /* 0x04 E-102's taskwk */
    E102WK *ewk;                /* 0x08 */
    Sint32 unk0C;               /* 0x0C */
} E102_LOCKONWK;                /* 0x10 */

/* awp of a lock-on marker task (func_8C0D577C; CAlloc(1, 0x34) by
 * func_8C0D38C6, released by func_8C0D5D90) */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 num;                  /* 0x01 lock-on number (ewk->timer0A) */
    Sint16 unk02;               /* 0x02 */
    Sint16 flag;                /* 0x04 0x200 tested by 8C0D5D90 */
    Sint16 unk06;               /* 0x06 */
    task *target;               /* 0x08 locked-on task */
    taskwk *ptwp;               /* 0x0C E-102's taskwk */
    E102WK *ewk;                /* 0x10 */
    task *unk14;                /* 0x14 (8C0D5D90: twp->flag |= 0x100) */
    Sint32 unk18;               /* 0x18 */
    Float unk1C;                /* 0x1C */
    Sint32 unk20;               /* 0x20 */
    Angle ang24;                /* 0x24 */
    Angle ang28;                /* 0x28 */
    task *unk2C;                /* 0x2C handed to 8C0D5064 by func_8C0D32C0 */
    TaskFuncPtr target_exec;    /* 0x30 */
} E102_MARKERWK;                /* 0x34 */

/* tp->mwp of a task E-102 can lock on (larger than motionwk) */
typedef struct {
    motionwk mw;                /* 0x00 */
    task *marker_tp;            /* 0x40 lock-on marker task, NULL if not locked */
    TaskFuncPtr dest;           /* 0x44 original tp->dest (hooked by func_8C0D332C) */
    Sint32 unk48;               /* 0x48 */
    Sint16 unk4C;               /* 0x4C */
    Uint16 flag;                /* 0x4E 0x1000 tested by func_8C0D32C0, 8C0D5D90 */
} E102_TARGETWK;                /* 0x50 */

SA1_ASSERT_SIZE(E102_LOCKONWK, 0x10);
SA1_ASSERT_OFFSET(E102_LOCKONWK, ptwp, 0x04);
SA1_ASSERT_OFFSET(E102_LOCKONWK, ewk, 0x08);
SA1_ASSERT_SIZE(E102_MARKERWK, 0x34);
SA1_ASSERT_OFFSET(E102_MARKERWK, target, 0x08);
SA1_ASSERT_OFFSET(E102_MARKERWK, unk2C, 0x2C);
SA1_ASSERT_OFFSET(E102_TARGETWK, marker_tp, 0x40);
SA1_ASSERT_OFFSET(E102_TARGETWK, dest, 0x44);
SA1_ASSERT_OFFSET(E102_TARGETWK, flag, 0x4E);

#define ABS(x) ((x) < 0 ? -(x) : (x))

/* positional-SE enable flag (E102Main) */
extern Sint8 B_8C7A2EA8;

void func_8C02FC12(taskwk *twp, CCL_INFO *ci, NJS_POINT3 *out);   /* shape centre */
/*
 * D_8C422B44[twp->mode]: laser allowed in the current mode. Defined with a
 * Sint8 result in code_8C0D1E38.c, but the original caller here treats the
 * result as an int (no EXTS.B after the JSR: probably called without a
 * prototype). The callee code is the same for a Sint32 result (MOV.B
 * sign-extends); the PC build must use the definition's type.
 */
#if defined(TARGET_PC)
Sint8 func_8C0D2032(taskwk *twp);
#else
Sint32 func_8C0D2032(taskwk *twp);
#endif
void func_8C0D38C6(task *tp);                                   /* lock-on task exec */
void func_8C0D3A20(task *tp);                                   /* laser task exec */
void func_8C0D5064(task *tp);
void func_8C0D5D90(task *tp);                                   /* release the marker of a target */
void func_8C0D5DF6(task *tp);

Bool func_8C0D32A0(task *tp)
{
    taskwk *twp;
    colliwk *cwp;

    twp = tp->twp;
    if (twp == NULL) {
        return FALSE;
    }
    cwp = CWP(twp);
    if (cwp == NULL) {
        return FALSE;
    }
    if (cwp->info->attr & 0x100000) {
        return FALSE;
    }
    return TRUE;
}

void func_8C0D32C0(task *tp)
{
    E102_TARGETWK *mwp;
    E102_MARKERWK *awp;
    task *mtp;

    mwp = (E102_TARGETWK *)tp->mwp;
    if (mwp != NULL) {
        mtp = mwp->marker_tp;
        awp = NULL;
        if (mwp->flag & 0x1000) {
            if (mtp != NULL) {
                awp = (E102_MARKERWK *)mtp->awp;
            }
            if (func_8C0D32A0(tp)) {
                if (awp != NULL) {
                    func_8C0D5064(awp->unk2C);
                } else {
                    func_8C0D5064(NULL);
                }
            }
        }
    }
    func_8C0D5D90(tp);
    if (mwp != NULL && mwp->dest != NULL) {
        mwp->dest(tp);
    }
}

void func_8C0D332C(colliwk **list, Sint32 n)
{
    Sint32 i;
    colliwk *cwp;
    task *tp;
    E102_TARGETWK *mwp;

    for (i = n; i > 0; i--) {
        cwp = *list++;
        tp = cwp->mytask;
        mwp = (E102_TARGETWK *)tp->mwp;
        if (mwp == NULL) {
            continue;
        }
        if (tp->dest == func_8C0D32C0) {
            continue;
        }
        if ((cwp->info->flag & 0xC) == 0xC) {
            continue;
        }
        mwp->dest = tp->dest;
        tp->dest = func_8C0D32C0;
    }
}

void func_8C0D33A0(taskwk *twp, playerwk *pwp, E102WK *ewk)
{
    task **pp;
    Sint32 i;
    task *tp;
    taskwk *ctwp;
    E102_LOCKONWK *lwp;
    E102_MARKERWK *mwk;
    Float x;
    Float y;
    Float z;
    Float r;

    switch (ewk->mode) {
    case 0:
        ewk->scope_frame = 0.0f;
        ewk->lockon_timer = 0;
        ewk->laser_len = 0.0f;
        ewk->mode = 1;
        break;
    case 1:
        if ((B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) && func_8C0D2032(twp)) {
            ewk->mode = 2;
        }
        break;
    case 2:
        ewk->tp = CreateElementalTask(0, 6, func_8C0D38C6);
        if (ewk->tp != NULL) {
            lwp = (E102_LOCKONWK *)CAlloc(1, sizeof(E102_LOCKONWK));
            if (lwp != NULL) {
                ewk->tp->awp = lwp;
                lwp->ptwp = twp;
                lwp->ewk = ewk;
                ewk->laser_timer = 180;
                ewk->mode = 3;
            } else {
                DestroyTask(ewk->tp);
            }
        }
        if (B_8C7A2EA8) {
            func_8C09413A(0x316, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        break;
    case 3:
        tp = CreateElementalTask(2, 2, func_8C0D3A20);
        if (tp != NULL) {
            ctwp = tp->twp;
            ctwp->pos = ewk->laser_pos;
            x = ewk->laser_vec.x;
            y = ewk->laser_vec.y;
            z = ewk->laser_vec.z;
            r = njSqrt(SA1_FMAC(x, x, z * z));
            ctwp->ang.y = -SA1_FTOI(atan2f(z, x) * 10430.381f);
            ctwp->ang.z = SA1_FTOI(atan2f(y, r) * 10430.381f);
            ctwp->value.ptr = ewk;
        }
        if (!(B_8C754C08[(Uint8)twp->counter.b[0]].on & D_8C19E38C)) {
            ewk->mode = 5;
            ewk->timer0A = 0;
            ewk->timer0C = 0;
        }
        ewk->laser_timer--;
        if (ewk->laser_timer <= 0 || !func_8C0D2032(twp)) {
            ewk->timer0A = 0;
            ewk->timer0C = 0;
            ewk->mode = 4;
        }
        break;
    case 4:
        for (i = ewk->nb_lockon, pp = ewk->lockon_tp; i > 0; i--, pp++) {
            mwk = (E102_MARKERWK *)(*pp)->awp;
            if (mwk == NULL) {
                continue;
            }
            func_8C0D5DF6(mwk->target);
        }
        ewk->mode = 6;
        ewk->smode = 0;
        break;
    case 5:
    case 6:
        DestroyTask(ewk->tp);
        ewk->tp = NULL;
        ewk->mode = 1;
        func_8C0945A0(0x316);
        if ((B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) && func_8C0D2032(twp)) {
            ewk->mode = 2;
        }
        break;
    }
    if (B_8C754C08[(Uint8)twp->counter.b[0]].on & D_8C19E38C) {
        ewk->scope_frame += 1.0f;
    } else {
        ewk->scope_frame -= 1.0f;
    }
    ewk->scope_frame = (ewk->scope_frame < 0.0f) ? 0.0f
                     : (ewk->scope_frame > 19.0f) ? 19.0f : ewk->scope_frame;
}

Float func_8C0D36C4(colliwk *cwp, E102WK *ewk)
{
    NJS_POINT3 v;
    NJS_LINE line;
    NJS_POINT3 pos;
    Float dist;
    taskwk *twp;
    Float f;
    Sint32 ang0;
    Sint32 ang1;
    Sint32 d;

    if (cwp->mytask == NULL) {
        return 0.0f;
    }
    if (cwp->mytask->twp == NULL) {
        return 0.0f;
    }
    twp = cwp->mytask->twp;
    func_8C02FC12(twp, cwp->info, &pos);
    v = pos;
    njSubVector(&v, &ewk->laser_pos);
    dist = njScalor2(&v);
    if (dist > ewk->laser_len2) {
        return 0.0f;
    }
    ang0 = SA1_FTOI(atan2f(ewk->laser_vec.z, ewk->laser_vec.x) * 10430.381f);
    ang1 = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f);
    d = ABS(SubAngle(ang0, ang1));
    if (dist <= 2500.0f) {
        if (d > 0x1555) {
            return 0.0f;
        }
    } else {
        if (d > 0x888) {
            return 0.0f;
        }
        line.px = ewk->laser_pos.x;
        line.py = ewk->laser_pos.y;
        line.pz = ewk->laser_pos.z;
        line.vx = ewk->laser_vec.x;
        line.vy = ewk->laser_vec.y;
        line.vz = ewk->laser_vec.z;
        f = _builtin_fabsf(func_8C63FF84(&pos, &line, NULL));
        if (f > 60.0f) {
            return 0.0f;
        }
    }
    return dist;
}
