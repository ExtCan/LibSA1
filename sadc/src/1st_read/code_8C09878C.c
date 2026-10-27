#include "common.h"
#include "task.h"
#include "sa1/engine.h"

/*
 * Task list walkers (8C09878C-8C0988E4).
 * B_8C79F04C[8] holds the head of each task level list; level 8 is an
 * alias of level 7 (it is run first by the "all levels" walkers).
 * D_8C1C1D40 holds the exec function that is running (0 otherwise).
 */

extern task *B_8C79F04C[8];
extern TaskFuncPtr D_8C1C1D40;

ASM_STUB(void DestroyTask(task *tp))

void func_8C098842(task *tp);

/* Run the exec function of every task in a level list (SADX ExecTask-like). */
void func_8C09878C(Sint32 level)
{
    task *tp;
    task *next;
    TaskFuncPtr exec;

    if (level == 8) {
        level = 7;
    }
    tp = B_8C79F04C[level];
    if (tp != NULL) {
        do {
            exec = tp->exec;
            next = tp->next;
            D_8C1C1D40 = exec;
            if (exec != NULL) {
                exec(tp);
            }
            D_8C1C1D40 = NULL;
            tp = next;
        } while (tp != NULL);
    }
}

/* Run the display function of every task in a level list and of its children. */
void func_8C098800(Sint32 level)
{
    task *tp;
    task *next;
    TaskFuncPtr disp;

    if (level == 8) {
        level = 7;
    }
    for (tp = B_8C79F04C[level]; tp != NULL; tp = next) {
        disp = tp->disp;
        next = tp->next;
        if (disp != NULL) {
            disp(tp);
        }
        if (tp->ctp != NULL) {
            func_8C098842(tp);
        }
    }
}

/* Run the display function of every child of tp, recursively. */
void func_8C098842(task *tp)
{
    task *ctp;
    task *next;
    TaskFuncPtr disp;

    for (ctp = tp->ctp; ctp != NULL; ctp = next) {
        disp = ctp->disp;
        next = ctp->next;
        if (disp != NULL) {
            disp(ctp);
        }
        if (ctp->ctp != NULL) {
            func_8C098842(ctp);
        }
    }
}

/* Destroy every task of a level list that has an exec function. */
void func_8C098876(Sint32 level)
{
    task *tp;
    task *next;
    TaskFuncPtr exec;

    if (level == 8) {
        level = 7;
    }
    tp = B_8C79F04C[level];
    if (tp != NULL) {
        do {
            exec = tp->exec;
            next = tp->next;
            if (exec != NULL) {
                DestroyTask(tp);
            }
            tp = next;
        } while (tp != NULL);
    }
}

/* Run all task levels (level 8 = 7 first, then 0..6). */
void func_8C0988A4(void)
{
    Sint32 i;

    func_8C09878C(8);
    for (i = 0; i < 7; i++) {
        func_8C09878C(i);
    }
}

/* Display all task levels (level 8 = 7 first, then 0..6). */
void func_8C0988C4(void)
{
    Sint32 i;

    func_8C098800(8);
    for (i = 0; i < 7; i++) {
        func_8C098800(i);
    }
}
