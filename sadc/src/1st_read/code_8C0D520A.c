#include "sa1/sa1.h"

/*
 * 1ST_READ chunk 8C0D4B00-8C0D54A0, last function.
 *
 *  func_8C0D520A  exec of a 2D counter/bonus task created by 8C0D5028 and
 *                 8C0D5064 (CreateElementalTask(2, 2, func_8C0D520A) while
 *                 D_8C423048 is NULL; otherwise they bump twp->timer.b[1]
 *                 (and b[0]) of the running task). Mode machine on
 *                 twp->mode:
 *                   0  init: D_8C423048 = tp, CAlloc the work, disp
 *                      8C0D50AC, dest 8C0D51FC, pos (540, 120)
 *                   2  wait: when timer.b[0] > 0 and timer.b[1] > 0 go
 *                      to 1; after 300 frames go to 4
 *                   1  consume one unit of both counters, step = min(step
 *                      + 1, 5), total += step, scale 1.15, go to 3
 *                   3  15-frame pop, back to 2 (or 4 if timer.b[0] <= 0)
 *                   4  shrink speed = -scl.x / 30, 60-frame hold (5)
 *                   6  move by (-14, -3) per frame and shrink; at scale 0
 *                      add the remaining min(timer.b[0], 6) steps, spawn
 *                      8C0D4C30 with timer.w[0] = total, destroy self
 *                 Draws itself (8C0D50AC) unless B_8C752BDD < 0.
 *                 8C0D4E40 is a sibling task with the same structure.
 */

/* task.awp of this task (CAlloc(1, 0x14) in mode 0; 8C0D50AC reads the
 * same fields: total selects the sprite scale, timer0 the pulse) */
typedef struct {
    Sint32 timer0;              /* 0x00 mode 3 frames (15) */
    Sint32 timer1;              /* 0x04 mode 5 frames (60) */
    Sint32 count;               /* 0x08 frames waited in mode 2 (300 -> mode 4) */
    Sint32 step;                /* 0x0C increment, saturates at 5 */
    Sint32 total;               /* 0x10 sum of the steps; handed to 8C0D4C30 as timer.w[0] */
} COUNTWK;                      /* 0x14 */

SA1_ASSERT_SIZE(COUNTWK, 0x14);
SA1_ASSERT_OFFSET(COUNTWK, step, 0x0C);
SA1_ASSERT_OFFSET(COUNTWK, total, 0x10);

/* the running instance of this task (cleared by the dest 8C0D51FC) */
extern task *D_8C423048;
/* cleared every frame here, set to 1 by the dest 8C0D51FC */
extern Sint8 B_8C7A2EAA;
/* < 0: do not draw */
extern Sint8 B_8C752BDD;

/* exec of the task spawned at the end (gets timer.w[0] = total) */
void func_8C0D4C30(task *tp);

/*
 * Same-object neighbours that precede this function in the original
 * translation unit (8C0D4B00-8C0D54A0), in their original order. 8C0D50AC
 * is the tail-call (BRA) target. They also stand for the code in front of
 * func_8C0D520A: the original dumps a literal pool (entries pending from
 * 8C0D50AC/8C0D51FC) right after this function's switch dispatch, which
 * pushes cases 3-6 out of BT range ("BF; BRA case; NOP"). With these five
 * stubs in front, the stub sink stays pending and SHC dumps its pool at the
 * same place, so the dispatch (and the whole function) comes out identical.
 */
ASM_STUB(void func_8C0D4E40(task *tp))
ASM_STUB(void func_8C0D5028(void))
ASM_STUB(void func_8C0D5064(task *tp))
ASM_STUB(void func_8C0D50AC(task *tp))
ASM_STUB(void func_8C0D51FC(task *tp))

void func_8C0D520A(task *tp)
{
    COUNTWK *wk;
    taskwk *twp;
    Sint32 n;
    task *ntp;

    twp = tp->twp;
    wk = (COUNTWK *)tp->awp;
    B_8C7A2EAA = 0;
    switch (twp->mode) {
    case 0:
        D_8C423048 = tp;
        tp->awp = CAlloc(1, sizeof(COUNTWK));
        if (tp->awp == NULL) {
            DestroyTask(tp);
            return;
        }
        wk = (COUNTWK *)tp->awp;
        tp->disp = func_8C0D50AC;
        tp->dest = func_8C0D51FC;
        twp->pos.x = 540.0f;
        twp->pos.y = 120.0f;
        wk->count = 0;
        twp->mode = 2;
        /* fall through */
    case 2:
        if (twp->timer.b[0] > 0 && twp->timer.b[1] > 0) {
            twp->mode = 1;
        }
        wk->count++;
        if (wk->count >= 300) {
            twp->mode = 4;
        }
        break;
    case 1:
        twp->flag |= 0x100;
        twp->timer.b[1]--;
        twp->timer.b[0]--;
        wk->count = 0;
        wk->step = (wk->step + 1 < 5) ? wk->step + 1 : 5;
        wk->total += wk->step;
        wk->timer0 = 15;
        wk->timer1 = 0;
        twp->scl.x = 1.15f;
        twp->mode = 3;
        /* fall through */
    case 3:
        wk->timer0--;
        if (wk->timer0 <= 0) {
            if (twp->timer.b[0] > 0) {
                twp->mode = 2;
            } else {
                twp->mode = 4;
            }
        }
        break;
    case 4:
        wk->timer1 = 60;
        twp->scl.y = -(twp->scl.x * 0.0333333321f); /* 0x3D088888: 1/30 */
        twp->mode = 5;
        /* fall through */
    case 5:
        wk->timer1--;
        if (wk->timer1 <= 0) {
            twp->mode = 6;
        }
        break;
    case 6:
        twp->pos.x += -14.0f;
        twp->pos.y += -3.0f;
        twp->scl.x += twp->scl.y;
        if (twp->scl.x <= 0.0f) {
            n = (twp->timer.b[0] < 6) ? twp->timer.b[0] : 6;
            while (n > 0) {
                n--;
                wk->step = (wk->step + 1 < 5) ? wk->step + 1 : 5;
                wk->total += wk->step;
            }
            ntp = CreateElementalTask(2, 1, func_8C0D4C30);
            ntp->twp->timer.w[0] = (Sint16)wk->total;
            DestroyTask(tp);
            return;
        }
        break;
    }
    if (B_8C752BDD >= 0) {
        func_8C0D50AC(tp);
    }
}
