#include "common.h"
#include "task.h"
#include "sa1/engine.h"

/*
 * Task system helpers (8C0988E4-8C098998): destroy the tasks of whole level
 * ranges and allocate the per-task work blocks with the same sizes as
 * CreateElementalTask (8C09852E).
 */

/*
 * Force work (task.fwp): CreateElementalTask allocates CAlloc(2, 0x1C) for
 * im & 4. Layout from the SADX analogy (forcewk); not in the shared headers.
 */
typedef struct forcewk {
    void (*call_back)(task *tp, taskwk *twp, struct forcewk *fwp);   /* 0x00 */
    Angle3 ang_spd;             /* 0x04 */
    NJS_POINT3 pos_spd;         /* 0x10 */
} forcewk;                      /* 0x1C */

/* Any work (task.awp): CAlloc(1, 0x10) for im & 8 (SADX anywk). */
typedef struct anywk {
    TaskWkUnion work[4];        /* 0x00 */
} anywk;                        /* 0x10 */

SA1_ASSERT_SIZE(forcewk, 0x1C);
SA1_ASSERT_SIZE(anywk, 0x10);

/* Destroys every task of list level (8 is clamped to 7). */
ASM_STUB(void func_8C098876(Sint32 level))

/* Destroys the tasks of levels 0..6. */
void func_8C0988E4(void)
{
    Sint32 i;

    for (i = 0; i < 7; i++) {
        func_8C098876(i);
    }
}

/* Destroys the tasks of all levels 0..7. */
void func_8C098900(void)
{
    Sint32 i;

    for (i = 0; i < 8; i++) {
        func_8C098876(i);
    }
}

/* Allocates a zeroed taskwk (no callers). */
taskwk *func_8C098960(void)
{
    taskwk *twp;

    twp = CAlloc(1, sizeof(taskwk));
    return twp;
}

/* Allocates a zeroed motionwk (no callers). */
motionwk *func_8C09896E(void)
{
    motionwk *mwp;

    mwp = CAlloc(1, sizeof(motionwk));
    return mwp;
}

/* Allocates the two zeroed forcewk of task.fwp (no callers). */
forcewk *func_8C09897C(void)
{
    forcewk *fwp;

    fwp = CAlloc(2, sizeof(forcewk));
    return fwp;
}

/* Allocates a zeroed anywk (no callers). */
anywk *func_8C09898A(void)
{
    anywk *awp;

    awp = CAlloc(1, sizeof(anywk));
    return awp;
}

ASM_STUB(void *CAlloc(Sint32 n, Sint32 size))
