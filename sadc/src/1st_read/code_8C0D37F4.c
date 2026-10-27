#include "sa1/sa1.h"

/*
 * E-102 Gamma target lock-on (1ST_READ chunk 8C0D32A0-8C0D3C00, functions
 * 6-8 of 8; the first five are still assembly, reached with BSR):
 *
 *  func_8C0D37F4  nearest lockable target in one CCL entry list: for every
 *                 colliwk of the list, the distance from the E-102 laser
 *                 (func_8C0D36C4, > 0 when in range) of its owner task,
 *                 skipping owners without an extended motion work, with
 *                 work +0x40 set, with both CCL_INFO attack bits 0xC or
 *                 with work flag 0x1000; returns the owner task and the
 *                 distance (99999992.0f when none).
 *  func_8C0D38C6  exec of the lock-on search task (created by 8C0D33A0
 *                 with awp = {mode, player twp, E-102 work}): while the
 *                 lock-on timer of the E-102 work is 0 and fewer than 32
 *                 targets are held, picks the nearer target of the CCL
 *                 lists id 2 (B_8C745908) and id 3 (B_8C745B08), creates a
 *                 lock-on marker task (func_8C0D577C) at its position with
 *                 a 0x34-byte work, counts targets with func_8C0D32A0 (max
 *                 99) and restarts the timer (8); every frame it hooks the
 *                 dest of the listed tasks (func_8C0D332C).
 *  func_8C0D3A20  display of the laser task: a line along +x of length
 *                 twp->timer.f (250.0f) from twp->pos rotated by ang.y/z,
 *                 additive blending, blue/red colour from the lock-on
 *                 counter (60 - work +0x10) and the flash table
 *                 D_8C422D38 (the task ends at its 0 terminator).
 */

/*
 * E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2: CAlloc(1, 0x244)).
 * Head only, up to the fields used here (more in code_8C0CFACE.c,
 * code_8C0CFA6E.c, code_8C0D04A0.c).
 */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 unk01;                /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint8 nb_lock;              /* 0x04 MOV.B, < 32 to search for a new target (8C0D38F6) */
    Sint8 unk05;                /* 0x05 */
    Sint16 unk06;               /* 0x06 */
    Sint16 unk08;               /* 0x08 */
    Sint16 timer0A;             /* 0x0A lock-on count, ++ (max 99) per lockable target */
    Sint16 timer0C;             /* 0x0C */
    Sint16 unk0E;               /* 0x0E */
    Sint16 unk10;               /* 0x10 laser fade (colour 60 - unk10, 8C0D3AC6) */
    Sint16 lockon_timer;        /* 0x12 > 0: no new search; 8 after a lock */
    Sint16 unk14;               /* 0x14 */
    Sint16 flag;                /* 0x16 */
    Float hover_y;              /* 0x18 */
    Sint32 unk1C;               /* 0x1C */
    Angle3 jet_ang;             /* 0x20 */
    Angle jet_phase;            /* 0x2C */
    task *lock_tp;              /* 0x30 last lock-on marker task (8C0D39C6) */
    NJS_POINT3 laser_pos;       /* 0x34 */
    NJS_POINT3 laser_vec;       /* 0x40 */
    Float range;                /* 0x4C laser length, 250.0f (8C0D3A52) */
    Float range2;               /* 0x50 range * range */
} E102WK;                       /* head only; the real work is 0x244 bytes */

SA1_ASSERT_OFFSET(E102WK, nb_lock, 0x04);
SA1_ASSERT_OFFSET(E102WK, timer0A, 0x0A);
SA1_ASSERT_OFFSET(E102WK, unk10, 0x10);
SA1_ASSERT_OFFSET(E102WK, lockon_timer, 0x12);
SA1_ASSERT_OFFSET(E102WK, lock_tp, 0x30);
SA1_ASSERT_OFFSET(E102WK, laser_pos, 0x34);
SA1_ASSERT_OFFSET(E102WK, range, 0x4C);
SA1_ASSERT_OFFSET(E102WK, range2, 0x50);

/*
 * Extended motion work of lockable objects (enemies): owner->mwp of a
 * colliwk in the id 2/3 lists. Only the fields read by this chunk.
 */
typedef struct {
    motionwk mw;                /* 0x00 */
    void *unk40;                /* 0x40 must be NULL to be targeted; pointer (8C0D32C0 reads its +0x2C) */
    TaskFuncPtr dest;           /* 0x44 dest saved by 8C0D332C (restored by 8C0D32C0) */
    Uint8 unk48[6];             /* 0x48 */
    Uint16 flag;                /* 0x4E 0x1000: not targetable */
} E102_TARGETWK;

SA1_ASSERT_OFFSET(E102_TARGETWK, unk40, 0x40);
SA1_ASSERT_OFFSET(E102_TARGETWK, dest, 0x44);
SA1_ASSERT_OFFSET(E102_TARGETWK, flag, 0x4E);

/* awp of the lock-on search task (CAlloc(1, 0x10) by 8C0D33A0) */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 pad01[3];             /* 0x01 */
    taskwk *ptwp;               /* 0x04 player taskwk */
    E102WK *ewk;                /* 0x08 */
    Sint32 unk0C;               /* 0x0C */
} E102_SEARCHWK;

SA1_ASSERT_OFFSET(E102_SEARCHWK, ptwp, 0x04);
SA1_ASSERT_OFFSET(E102_SEARCHWK, ewk, 0x08);

/* awp of the lock-on marker task func_8C0D577C (CAlloc(1, 0x34) here) */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 count;                /* 0x01 E-102 work timer0A (low byte) */
    Sint16 unk02;               /* 0x02 */
    Sint16 equipment;           /* 0x04 player equipment */
    Sint16 unk06;               /* 0x06 */
    task *target_tp;            /* 0x08 */
    taskwk *ptwp;               /* 0x0C player taskwk */
    E102WK *ewk;                /* 0x10 */
    Sint32 unk14;               /* 0x14 0 */
    Sint32 unk18[6];            /* 0x18 */
    TaskFuncPtr target_exec;    /* 0x30 target_tp->exec */
} E102_LOCKWK;                  /* 0x34 */

SA1_ASSERT_SIZE(E102_LOCKWK, 0x34);
SA1_ASSERT_OFFSET(E102_LOCKWK, equipment, 0x04);
SA1_ASSERT_OFFSET(E102_LOCKWK, target_tp, 0x08);
SA1_ASSERT_OFFSET(E102_LOCKWK, unk14, 0x14);
SA1_ASSERT_OFFSET(E102_LOCKWK, target_exec, 0x30);

/* CCL entry lists filled by CCL_Entry (code_8C02F27C.c) */
extern Uint16 B_8C752BD8;               /* id 2 count (max 128) */
extern Uint16 B_8C752BDA;               /* id 3 count (max 256) */
extern colliwk *B_8C745908[];           /* id 2 list [128] */
extern colliwk *B_8C745B08[];           /* id 3 list [256] */

/* laser line (code_8C0CBE00.c sets B_8C7A2EAC = {D_8C422C60, D_8C422C78, NULL, 2}) */
extern NJS_POINT3COL B_8C7A2EAC;
extern NJS_POINT3 D_8C422C60[];         /* 2 points */
extern NJS_COLOR D_8C422C78[];          /* 2 colours */
extern Uint8 D_8C422D38[];              /* laser flash table, 0-terminated */

void func_8C0D577C(task *tp);           /* lock-on marker exec */

ASM_STUB(Sint32 func_8C0D32A0(task *tp))
ASM_STUB(void func_8C0D332C(colliwk **list, Sint32 n))
ASM_STUB(Float func_8C0D36C4(colliwk *cwp, E102WK *ewk))

task *func_8C0D37F4(E102WK *ewk, colliwk **list, Sint32 n, Float *dist)
{
    task *found;
    Float min;
    colliwk *cwp;
    Float d;
    E102_TARGETWK *mwp;
    task *tp;
    Sint32 i;

    found = NULL;
    min = 99999994.0f;
    for (i = n; i > 0; i--) {
        cwp = *list++;
        d = func_8C0D36C4(cwp, ewk);
        if (!(d > 0.0f)) {
            continue;
        }
        mwp = (E102_TARGETWK *)cwp->mytask->mwp;
        if (mwp == NULL) {
            continue;
        } else {
            tp = cwp->mytask;
            if (mwp->unk40 != NULL) {
                continue;
            }
        }
        if ((cwp->info->flag & 0xC) == 0xC) {
            continue;
        }
        if (mwp->flag & 0x1000) {
            continue;
        }
        if (d < min) {
            found = tp;
            min = d;
        }
    }
    *dist = min;
    return found;
}

void func_8C0D38C6(task *tp)
{
    E102_SEARCHWK *awp;
    taskwk *ptwp;
    E102WK *ewk;
    task *target;
    task *target2;
    task *ntp;
    E102_LOCKWK *lwp;
    Sint32 pno;
    Float dist;
    Float dist2;

    awp = (E102_SEARCHWK *)tp->awp;
    ptwp = awp->ptwp;
    ewk = awp->ewk;
    switch (awp->mode) {
    case 0:
        awp->mode = 1;
    case 1:
        if (ewk->lockon_timer > 0) {
            break;
        }
        if (ewk->nb_lock >= 32) {
            break;
        }
        target = func_8C0D37F4(ewk, B_8C745908, B_8C752BD8, &dist);
        target2 = func_8C0D37F4(ewk, B_8C745B08, B_8C752BDA, &dist2);
        if (target == NULL) {
            target = target2;
        } else if (target2 != NULL && dist > dist2) {
            target = target2;
        }
        if (target != NULL) {
            ntp = CreateElementalTask(2, 5, func_8C0D577C);
            if (ntp != NULL) {
                lwp = (E102_LOCKWK *)CAlloc(1, sizeof(E102_LOCKWK));
                if (lwp == NULL) {
                    DestroyTask(ntp);
                } else {
                    pno = (Uint8)ptwp->counter.b[0];
                    if (func_8C0D32A0(target)) {
                        ewk->timer0A++;
                        ewk->timer0A = (ewk->timer0A < 99) ? ewk->timer0A : 99;
                    }
                    ewk->lock_tp = ntp;
                    ewk->lockon_timer = 8;
                    ntp->twp->pos = target->twp->pos;
                    ntp->awp = lwp;
                    lwp->count = (Sint8)ewk->timer0A;
                    lwp->target_tp = target;
                    lwp->ptwp = ptwp;
                    lwp->ewk = ewk;
                    lwp->unk14 = 0;
                    lwp->target_exec = target->exec;
                    lwp->equipment = B_8C78C628[pno]->work.pwp->equipment;
                }
            }
        }
        break;
    }
    func_8C0D332C(B_8C745908, B_8C752BD8);
    func_8C0D332C(B_8C745B08, B_8C752BDA);
}

void func_8C0D3A20(task *tp)
{
    taskwk *twp;
    E102WK *ewk;
    Uint8 c;
    Sint32 n;
    Float f;
    Float x;
    Sint32 r;
    Sint32 b;

    PC_ONLY(c = 0;)
    twp = tp->twp;
    ewk = (E102WK *)twp->value.ptr;
    switch (twp->mode) {
    case 0:
        twp->timer.f = 250.0f;
        ewk->range = twp->timer.f;
        ewk->range2 = ewk->range * ewk->range;
        twp->counter.b[0] = 0;
        twp->mode = 1;
    case 1:
        c = D_8C422D38[(Uint8)twp->counter.b[0]++];
        if (c == 0) {
            DestroyTask(tp);
            return;
        }
        break;
    }
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    njColorBlendingMode(0, 10);
    njColorBlendingMode(1, 10);
    n = 60 - ewk->unk10;
    f = (Float)(n > 0 ? n : 0);
    f *= 0.016666666f;
    r = SA1_FTOI((1.0f - f) * (Float)c);
    b = SA1_FTOI((Float)c * f);
    D_8C422C78[0].argb.r = (Uint8)r;
    D_8C422C78[1].argb.r = (Uint8)r;
    D_8C422C78[0].argb.b = (Uint8)b;
    D_8C422C78[1].argb.b = (Uint8)b;
    x = 0.0f;
    do {
        D_8C422C60[0].x = x;
        x = (twp->timer.f > x + 7.5f) ? twp->timer.f : x + 7.5f;
        D_8C422C60[1].x = x;
        njDrawLine3D(&B_8C7A2EAC, 1, 64);
    } while (twp->timer.f > x);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    njPopMatrix(1);
}
