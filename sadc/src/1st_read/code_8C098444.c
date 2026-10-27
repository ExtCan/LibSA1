#include "common.h"
#include "task.h"

/*
 * Task system core (8C098444-8C09878C): task pool initialisation, task
 * creation and destruction (task.c of the original TU 8C098440-8C098EE0).
 *
 * The original takes an unsigned short im (MOV.W R4,@R15 ... MOV.W @R15,R13;
 * EXTU.W); sa1/engine.h declares it that way since the canonical-prototype
 * pass (build/verify/canonical_protos.json). The prototypes this file needs
 * are its own definitions and the same-file stubs below.
 *
 * B_8C79F044: base of the task pool (MAlloc'd, 0x1000 tasks).
 * B_8C79F048: head of the free task list (linked through next/last).
 * B_8C79F04C[8]: head of each task level list (level 8 is an alias of 7;
 *     level 7 tasks created by func_8C0985EC are not linked into any list,
 *     CreateChildTask links them into the parent's child list).
 * D_8C1C1D40: exec function that is running (set by func_8C09878C).
 */

extern task *B_8C79F044;
extern task *B_8C79F048;
extern task *B_8C79F04C[8];
extern TaskFuncPtr D_8C1C1D40;

/*
 * The SA1 DC task is 0x30 bytes: the pool is MAlloc(0x30000) for 0x1000
 * tasks and is linked with a 48-byte stride, and every task function only
 * touches fields 0x00-0x2C. include/task.h still declares the SADX field thp
 * at 0x30 (sizeof(task) == 0x34), so the SHC build uses the DC size
 * explicitly until the header drops thp; the PC pool uses the native size.
 */
#define TASK_POOL_NUM 0x1000
#if defined(TARGET_PC)
#define TASK_SIZE ((Sint32)sizeof(task))
#define TASK_NEXT(tp) ((tp) + 1)
#else
#define TASK_SIZE 0x30
#define TASK_NEXT(tp) ((task *)((Uint8 *)(tp) + TASK_SIZE))
#endif

/* Object condition (task.ocp, set-data entry); only the head is used here. */
typedef struct OBJ_CONDITION {
    Sint8 scCount;              /* 0x00 */
    Sint8 scUserFlag;           /* 0x01 */
    Sint16 ssCondition;         /* 0x02 bit 0 cleared by DestroyTask */
} OBJ_CONDITION;

/*
 * Force work (task.fwp, im & 4: CAlloc(2, 0x1C)) and any work (task.awp,
 * im & 8: CAlloc(1, 0x10)); SADX layouts, not in the shared headers yet
 * (same definitions as code_8C0988E4.c).
 */
typedef struct forcewk {
    void (*call_back)(task *tp, taskwk *twp, struct forcewk *fwp);   /* 0x00 */
    Angle3 ang_spd;             /* 0x04 */
    NJS_POINT3 pos_spd;         /* 0x10 */
} forcewk;                      /* 0x1C */

typedef struct anywk {
    TaskWkUnion work[4];        /* 0x00 */
} anywk;                        /* 0x10 */

SA1_ASSERT_SIZE(forcewk, 0x1C);
SA1_ASSERT_SIZE(anywk, 0x10);

/* Empty exec function (code_8C098440.c), used when exec is NULL. */
extern void func_8C098440(task *tp);

/* Same-file callees (BSR) that are still assembly: stubs at the end. */
void *MAlloc(Sint32 size);
void *CAlloc(Sint32 n, Sint32 size);
void MFree(void *p);
void func_8C098998(task *tp);
void func_8C098E60(task *tp);

task *func_8C0985EC(Sint32 level, TaskFuncPtr exec);
void DestroyTask(task *tp);

/*
 * The original TU defines small helpers that SHC -speed inlined into
 * CreateElementalTask (func_8C098960 taskwk, func_8C09896E motionwk,
 * func_8C09897C forcewk, func_8C09898A anywk) and into DestroyTask
 * (func_8C0989C8/8C0989E0/8C0989F8 free mwp/fwp/awp); none of them has a
 * caller left. They are decompiled in code_8C0988E4.c and code_8C098998.c;
 * the inlining (and with it the register allocation of the callers) only
 * happens with a body in this file, so file-local copies are used.
 */
static taskwk *AllocTaskwk(void);
static motionwk *AllocMotionwk(void);
static forcewk *AllocForcewk(void);
static anywk *AllocAnywk(void);

/* Returns the exec function that is running. */
TaskFuncPtr func_8C098444(void)
{
    return D_8C1C1D40;
}

/* Allocates the task pool and links every task into the free list. */
void func_8C09844A(void)
{
    task *tp;
    task *next;
    task *prev;
    Sint32 i;

    B_8C79F044 = MAlloc(TASK_POOL_NUM * TASK_SIZE);
    B_8C79F048 = B_8C79F044;
    if (B_8C79F048 == NULL) {
        return;
    }
    for (i = 0; i < 8; i++) {
        B_8C79F04C[i] = NULL;
    }
    tp = B_8C79F048;
    prev = NULL;
    next = tp;
    for (i = 0; i < TASK_POOL_NUM; i++) {
        next = TASK_NEXT(next);
        if (i >= TASK_POOL_NUM - 1) {
            next = NULL;
        }
        tp->next = next;
        tp->last = prev;
        tp->ocp = NULL;
        tp->twp = NULL;
        tp->mwp = NULL;
        tp->fwp = NULL;
        tp->awp = NULL;
        tp->ctp = NULL;
        tp->ptp = NULL;
        tp->exec = NULL;
        tp->disp = NULL;
        tp->dest = NULL;
        prev = tp;
        tp = next;
    }
}

/*
 * Creates a task in list level with the work blocks selected by im:
 * 2 = taskwk, 4 = 2 forcewk, 1 = motionwk, 8 = anywk. Returns NULL (and
 * destroys the half-built task) when an allocation fails.
 */
task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec)
{
    task *tp;
    taskwk *twp;
    motionwk *mwp;
    forcewk *fwp;
    anywk *awp;

    tp = func_8C0985EC(level, exec);
    if (tp == NULL) {
        return NULL;
    }
    if (im & 2) {
        twp = AllocTaskwk();
        if (twp == NULL) {
            DestroyTask(tp);
            return NULL;
        }
        tp->twp = twp;
    }
    if (im & 4) {
        fwp = AllocForcewk();
        if (fwp == NULL) {
            DestroyTask(tp);
            return NULL;
        }
        tp->fwp = fwp;
    }
    if (im & 1) {
        mwp = AllocMotionwk();
        if (mwp == NULL) {
            DestroyTask(tp);
            return NULL;
        }
        tp->mwp = mwp;
    }
    if (im & 8) {
        awp = AllocAnywk();
        if (awp == NULL) {
            DestroyTask(tp);
            return NULL;
        }
        tp->awp = awp;
    }
    return tp;
}

/*
 * Takes a task from the free list, sets its exec function (the empty
 * func_8C098440 when exec is NULL) and links it at the head of list level
 * (level 7 tasks are not linked; level 8 goes to list 7).
 */
task *func_8C0985EC(Sint32 level, TaskFuncPtr exec)
{
    task *tp;
    task *next;
    task *head;

    if (B_8C79F048 == NULL) {
        return NULL;
    }
    tp = B_8C79F048;
    next = tp->next;
    if (next != NULL) {
        next->last = NULL;
    }
    B_8C79F048 = next;
    if (exec != NULL) {
        tp->exec = exec;
    } else {
        tp->exec = func_8C098440;
    }
    if (level != 7) {
        if (level == 8) {
            level = 7;
        }
        if (B_8C79F04C[level] == NULL) {
            B_8C79F04C[level] = tp;
            tp->next = NULL;
            tp->last = NULL;
        } else {
            head = B_8C79F04C[level];
            tp->last = NULL;
            tp->next = head;
            head->last = tp;
            B_8C79F04C[level] = tp;
        }
    }
    tp->ocp = NULL;
    tp->twp = NULL;
    tp->mwp = NULL;
    tp->fwp = NULL;
    tp->awp = NULL;
    tp->disp = NULL;
    tp->dest = NULL;
    tp->ctp = NULL;
    tp->ptp = NULL;
    return tp;
}

#ifdef NON_MATCHING
/* Inlined copies of func_8C0989C8, func_8C0989E0, func_8C0989F8. */
static void FreeMotionwk(task *tp)
{
    MFree(tp->mwp);
    tp->mwp = NULL;
}

static void FreeForcewk(task *tp)
{
    MFree(tp->fwp);
    tp->fwp = NULL;
}

static void FreeAnywk(task *tp)
{
    MFree(tp->awp);
    tp->awp = NULL;
}

/*
 * Calls the destructor, destroys the children, unlinks the task from its
 * list (sibling list, parent's child list or level list), frees its work
 * blocks, releases its set-data entry and returns it to the free list.
 *
 * NON_MATCHING: 7 instructions differ, all in the level-list search loop:
 * SHC keeps the list base in R7 and the end pointer in R6, the original
 * the other way round (base R6, end R7). Everything else, including the
 * inlined work-freeing helpers, is identical.
 */
void DestroyTask(task *tp)
{
    task *next;
    task *last;
    Sint32 i;

    if (tp->dest != NULL) {
        tp->dest(tp);
    }
    tp->exec = NULL;
    tp->disp = NULL;
    tp->dest = NULL;
    next = tp->next;
    last = tp->last;
    if (tp->ctp != NULL) {
        func_8C098E60(tp);
    }
    if (next != NULL && last != NULL) {
        next->last = last;
        last->next = next;
    } else if (next == NULL && last != NULL) {
        last->next = NULL;
    } else if (tp->ptp != NULL) {
        if (next != NULL) {
            next->last = NULL;
        }
        tp->ptp->ctp = next;
    } else {
        if (next != NULL) {
            next->last = NULL;
        }
        for (i = 0; i < 8; i++) {
            if (tp == B_8C79F04C[i]) {
                B_8C79F04C[i] = next;
                break;
            }
        }
    }
    if (tp->twp != NULL) {
        func_8C098998(tp);
    }
    if (tp->fwp != NULL) {
        FreeForcewk(tp);
    }
    if (tp->mwp != NULL) {
        FreeMotionwk(tp);
    }
    if (tp->awp != NULL) {
        FreeAnywk(tp);
    }
    if (tp->ocp != NULL) {
        ((OBJ_CONDITION *)tp->ocp)->ssCondition &= ~1;
        tp->ocp = NULL;
    }
    if (B_8C79F048 == NULL) {
        B_8C79F048 = tp;
        tp->last = NULL;
        tp->next = NULL;
    } else {
        next = B_8C79F048;
        next->last = tp;
        tp->next = next;
        tp->last = NULL;
        B_8C79F048 = tp;
    }
    tp->ctp = NULL;
    tp->ptp = NULL;
}
#else
ASM_STUB(void DestroyTask(task *tp))
#endif

/* Marks a task for destruction: its next exec is DestroyTask. */
void FreeTask(task *tp)
{
    tp->exec = DestroyTask;
}

/* Inlined copies of func_8C098960, func_8C09896E, func_8C09897C, func_8C09898A. */
static taskwk *AllocTaskwk(void)
{
    return CAlloc(1, sizeof(taskwk));
}

static motionwk *AllocMotionwk(void)
{
    return CAlloc(1, sizeof(motionwk));
}

static forcewk *AllocForcewk(void)
{
    return CAlloc(2, sizeof(forcewk));
}

static anywk *AllocAnywk(void)
{
    return CAlloc(1, sizeof(anywk));
}

ASM_STUB(void *CAlloc(Sint32 n, Sint32 size))
ASM_STUB(void *MAlloc(Sint32 size))
ASM_STUB(void MFree(void *p))
ASM_STUB(void func_8C098998(task *tp))
ASM_STUB(void func_8C098E60(task *tp))
