#include "sa1/sa1.h"

/*
 * Knuckles effect launchers (1ST_READ chunk 8C0CB960), called from
 * func_8C0C450A (Knuckles code):
 *   func_8C0CB960  create the fade task (exec 8C0CB848, disp 8C0CB870)
 *                  with step 1/18, scale 6.0 -> 1.2
 *   func_8C0CB9A4  same task with step 1/24, scale 0.6 -> 4.5
 *   func_8C0CB9E8  reset the Knuckles trail ring count (B_8C7A2784.num)
 *
 * Task work as used by 8C0CB848/8C0CB870 (chunk 8C0CB7A0):
 *   timer.f    progress t, advanced by scl.x each frame by 8C0CB848;
 *              the task is freed once t reaches 1.0 (CAlloc leaves it 0)
 *   value.f    start of the interpolated value (lerp(value.f, counter.f, t))
 *   counter.f  end of the interpolated value
 *   scl.y/z    start/end scale (lerp(scl.y, scl.z, t), FMACs in 8C0CB870)
 */

void func_8C0CB848(task *tp);   /* exec: advance timer.f, FreeTask at 1.0 */
void func_8C0CB870(task *tp);   /* disp */

/* B_8C7A2784: Knuckles ribbon-trail history (count + 16 rings of 8 points),
 * filled and drawn by func_8C0CBA40. Same layout as the copy in
 * code_8C0CBA40.c so both translation units agree on the object's type
 * (candidate for a shared header). */
typedef struct {
    NJS_POINT3 p[8];            /* 0x00 */
} KNU_TRAIL_RING;               /* 0x60 */

typedef struct {
    Sint32 num;                 /* 0x000 */
    KNU_TRAIL_RING ring[16];    /* 0x004 */
} KNU_TRAIL;                    /* 0x604 */

SA1_ASSERT_SIZE(KNU_TRAIL_RING, 0x60);
SA1_ASSERT_SIZE(KNU_TRAIL, 0x604);

extern KNU_TRAIL B_8C7A2784;

void func_8C0CB960(void)
{
    task *tp;
    taskwk *twp;

    if ((tp = CreateElementalTask(2, 6, func_8C0CB848)) != NULL) {
        twp = tp->twp;
        twp->scl.x = 0.05555556f;   /* 0x3D638E3A, 1/18 */
        twp->scl.y = 6.0f;
        twp->scl.z = 1.19999995f;   /* 0x3F999999 */
        twp->value.f = 0.0f;
        twp->counter.f = -1.0f;
        tp->disp = func_8C0CB870;
    }
}

void func_8C0CB9A4(void)
{
    task *tp;
    taskwk *twp;

    if ((tp = CreateElementalTask(2, 6, func_8C0CB848)) != NULL) {
        twp = tp->twp;
        twp->scl.x = 0.041666669f;  /* 0x3D2AAAAB, 1/24 */
        twp->scl.y = 0.59999998f;   /* 0x3F199999 */
        twp->scl.z = 4.5f;
        twp->value.f = 0.0f;
        twp->counter.f = -1.0f;
        tp->disp = func_8C0CB870;
    }
}

void func_8C0CB9E8(void)
{
    B_8C7A2784.num = 0;
}
