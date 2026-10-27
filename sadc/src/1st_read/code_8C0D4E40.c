#include "sa1/sa1.h"

/*
 * HUD counter tasks (1ST_READ chunk 8C0D4B00-8C0D54A0, part):
 *
 *  func_8C0D4E40  exec of the "collect" counter (created by 8C0D4B54):
 *                 awp = CNTWK (CAlloc(1, 8)); display 8C0D4D08, destroy
 *                 8C0D4DE6. When awp->req is set it pops a sprite in at
 *                 (540, 120) and moves awp->count into awp->total (one
 *                 every 2 frames) for 15 frames, then waits 240 frames (a
 *                 new request restarts the count), then flies off (-14, -3
 *                 per frame, SE 22) while
 *                 shrinking; at scale 0 (or on a new request) it spawns
 *                 8C0D4C30 with timer.w[0] = total and returns to idle.
 *  func_8C0D5028  bump timer.b[1] of the singleton task D_8C423048
 *                 (8C0D520A, created on demand; on failure 8C08AB42(1)
 *                 and SE 4).
 *  func_8C0D5064  same; with tp == NULL it uses/creates the singleton and
 *                 also bumps timer.b[0].
 *  func_8C0D50AC  display of 8C0D520A: pulsing sprite D_8C423024 scaled by
 *                 the total (1.0/1.25/1.5/1.75, x1.15 at 100), then the
 *                 digits (8C0D4B70).
 *  func_8C0D51FC  destructor of 8C0D520A: clears the singleton pointer and
 *                 sets B_8C7A2EAA.
 */

/* task.awp of 8C0D4E40 (CAlloc(1, 8)) */
typedef struct {
    Sint8 req;                  /* 0x00 request (> 0) */
    Sint8 count;                /* 0x01 items still to count */
    Sint16 total;               /* 0x02 counted items */
    Sint16 timer;               /* 0x04 */
    Sint16 wait;                /* 0x06 */
} CNTWK;                        /* 0x08 */

/* task.awp of 8C0D520A (CAlloc(1, 20)) */
typedef struct {
    Sint32 timer;               /* 0x00 pulse phase (njSin(timer * 0x8000 / 15)) */
    Sint32 wait;                /* 0x04 */
    Sint32 frame;               /* 0x08 */
    Sint32 add;                 /* 0x0C */
    Sint32 total;               /* 0x10 */
} CNT2WK;                       /* 0x14 */

SA1_ASSERT_SIZE(CNTWK, 0x08);
SA1_ASSERT_SIZE(CNT2WK, 0x14);

/*
 * Same-chunk neighbours. 8C0D4B70 and 8C0D4D08 are reached with BRA, so
 * they are ASM_STUBs, defined at the end of the file: stubs placed before
 * func_8C0D4E40 would leave their sink address pending in the literal pool
 * and move the pool dumps (the original pools sit inside 4E40 and 50AC).
 */
void func_8C0D4B70(Sint32 n, NJS_POINT3 *pos, Float scl);  /* digits of the total */
void func_8C0D4C30(task *tp);
void func_8C0D4D08(task *tp);
void func_8C0D4DE6(task *tp);
void func_8C0D520A(task *tp);

void func_8C08AB42(Sint32 n);

extern Sint8 B_8C7A2EAA;                    /* 1 once the 8C0D520A task is gone */
extern Sint8 B_8C752BDD;
extern NJS_SPRITE D_8C423024;
extern task *D_8C423048;                    /* the 8C0D520A task (singleton) */

void func_8C0D4E40(task *tp)
{
    taskwk *twp;
    CNTWK *awp;
    task *ctp;

    twp = tp->twp;
    awp = (CNTWK *)tp->awp;
    twp->flag &= ~0x100;
    switch (twp->mode) {
    case 0:
        awp = (CNTWK *)CAlloc(1, sizeof(CNTWK));
        tp->awp = awp;
        twp->counter.l = 0;
        tp->disp = func_8C0D4D08;
        tp->dest = func_8C0D4DE6;
        twp->scl.x = 1.15f;
        twp->mode = 1;
    case 1:
        if (awp->req > 0) {
            B_8C7A2EAA = 0;
            awp->total = 0;
            twp->mode = 2;
            twp->pos.x = 540.0f;
            twp->pos.y = 120.0f;
        }
        break;
    case 2:
        awp->timer = 15;
        twp->scl.x = 1.15f;
        awp->wait = 0;
        twp->mode = 3;
    case 3:
        if (awp->count > 0) {
            awp->wait--;
            if (awp->wait <= 0) {
                awp->wait = 2;
                awp->count--;
                awp->total++;
            }
        }
        awp->timer--;
        if (awp->timer <= 0) {
            awp->req = 0;
            awp->wait = 240;
            twp->mode = 4;
        }
        break;
    case 4:
        if (awp->req > 0) {
            twp->mode = 2;
            break;
        }
        awp->wait--;
        if (awp->wait <= 0) {
            func_8C0940DA(22, 0, 0, 0);
            twp->scl.y = -(twp->scl.x * 0.0333333323f);
            twp->mode = 5;
        }
        break;
    case 5:
        twp->pos.x += -14.0f;
        twp->pos.y += -3.0f;
        twp->scl.x += twp->scl.y;
        if (twp->scl.x <= 0.0f || awp->req > 0) {
            ctp = CreateElementalTask(2, 1, func_8C0D4C30);
            ctp->twp->timer.w[0] = awp->total;
            twp->mode = 1;
        }
        break;
    }
    if (twp->mode > 1 && B_8C752BDD >= 0) {
        twp->flag |= 0x100;
        func_8C0D4D08(tp);
    }
}

void func_8C0D5028(void)
{
    task *tp;

    if (D_8C423048 == NULL) {
        tp = CreateElementalTask(2, 2, func_8C0D520A);
        if (tp == NULL) {
            func_8C08AB42(1);
            func_8C0940DA(4, 0, 0, 0);
            return;
        }
    } else {
        tp = D_8C423048;
    }
    tp->twp->timer.b[1]++;
}

void func_8C0D5064(task *tp)
{
    if (tp == NULL) {
        if (D_8C423048 == NULL) {
            tp = CreateElementalTask(2, 2, func_8C0D520A);
            if (tp == NULL) {
                func_8C08AB42(1);
                func_8C0940DA(4, 0, 0, 0);
                return;
            }
        } else {
            tp = D_8C423048;
        }
        tp->twp->timer.b[0]++;
    }
    tp->twp->timer.b[1]++;
}

void func_8C0D50AC(task *tp)
{
    taskwk *twp;
    CNT2WK *awp;
    Float s;
    Float sz;

    twp = tp->twp;
    awp = (CNT2WK *)tp->awp;
    if (B_8C754EC8 != 0) {
        return;
    }
    if (!(twp->flag & 0x100)) {
        return;
    }
    if (awp->total >= 25) {
        s = 1.75f;
    } else if (awp->total >= 15) {
        s = 1.5f;
    } else if (awp->total >= 6) {
        s = 1.25f;
    } else {
        s = 1.0f;
    }
    s = SA1_FMAC(s * njSin(awp->timer * 0x8000 / 15), 0.5f, s);
    s *= twp->scl.x;
    sz = s;
    if (awp->total >= 100) {
        sz *= 1.15f;
    }
    D_8C423024.sy = sz;
    D_8C423024.sx = sz;
    D_8C423024.p.x = twp->pos.x;
    D_8C423024.p.y = twp->pos.y;
    func_8C043A20(1.0f, 1.0f, 1.0f, 1.0f);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    njDrawSprite2D(&D_8C423024, 11, -3.0f, 34);
    func_8C0D4B70(awp->total, &twp->pos, s);
}

void func_8C0D51FC(task *tp)
{
    D_8C423048 = NULL;
    B_8C7A2EAA = 1;
}

ASM_STUB(void func_8C0D4B70(Sint32 n, NJS_POINT3 *pos, Float scl))
ASM_STUB(void func_8C0D4D08(task *tp))
