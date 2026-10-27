#include "common.h"
#include "ninja.h"
#include "task.h"

extern task *B_8C78C248;
extern Sint32 B_8C78C24C;

extern Uint16 GetStageNumber(void);
extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec); /* CreateElementalTask */
extern void FreeTask(task *tp); /* FreeTask */
extern void TailsMain(task *tp);
extern task *func_8C07CFA0(Uint8 pno); /* GetPlayerTaskPointer (sa1/player.h) */
extern void SetInputP(Uint8 pno, Sint32 mode); /* sa1/player.h */
extern void func_8C0509B8(Uint8 n); /* sa1/input.h */
extern void func_8C0509DA(Uint8 n); /* sa1/input.h */

void func_8C081D4E(task *tp);
Sint32 func_8C082820(void);
void func_8C0826B0(taskwk *twp, NJS_POINT3 *pos, Float dist);

void func_8C081B66(task *tp);

task *func_8C081AC0(task *ptp)
{
    task *tp;
    task *tp2;
    Sint32 level;

    if (B_8C78C248 != NULL) {
        return B_8C78C248;
    }
    level = ((Sint32)GetStageNumber() >> 8) & 0xF;
    if (level == 9 || func_8C082820()) {
        tp = CreateElementalTask(2, 0, func_8C081D4E);
        B_8C78C248 = tp;
        if (tp != NULL) {
            tp->twp->counter.b[1] = 2;
            tp->twp->counter.b[0] = 1;
            tp->dest = func_8C081B66;
            goto next;
        }
    }
    return NULL;
next:
    tp2 = CreateElementalTask(7, 1, TailsMain);
    if (tp2 != NULL) {
        tp2->twp->counter.b[1] = 2;
        tp2->twp->counter.b[0] = 1;
        func_8C0826B0(ptp->twp, &tp2->twp->pos, 10.0f);
    }
    tp->twp->mode = 0;
    B_8C78C24C = 0;
    return tp;
}

void func_8C081B66(task *tp)
{
    B_8C78C248 = NULL;
}

void func_8C081B6E(void)
{
    if (B_8C78C248 != NULL) {
        FreeTask(B_8C78C248);
        B_8C78C248 = NULL;
    }
}

Sint32 func_8C081B8A(void)
{
    task *tp;

    tp = B_8C78C248;
    if (tp == NULL) {
        return 0;
    }
    tp->twp->mode = 0;
    return 1;
}

Sint32 func_8C081C00(void)
{
    task *tp;

    tp = B_8C78C248;
    func_8C07CFA0(1);
    if (tp == NULL) {
        return 0;
    }
    SetInputP(1, 49);
    func_8C0509B8(1);
    tp->twp->mode = 1;
    return 1;
}

Sint32 func_8C081C36(void)
{
    task *tp;

    tp = B_8C78C248;
    if (tp == NULL) {
        return 0;
    }
    SetInputP(1, 24);
    func_8C0509DA(1);
    tp->twp->mode = 2;
    return 1;
}

Sint32 func_8C081C66(void)
{
    task *tp;
    taskwk *twp;

    if (B_8C78C248 == NULL) {
        return 1;
    }
    tp = func_8C07CFA0(1);
    if (tp == NULL) {
        return 0;
    }
    if ((Uint8)(twp = tp->twp)->counter.b[1] == 2 && (Uint8)twp->counter.b[0] == 1) {
        FreeTask(tp);
        return 1;
    }
    return 0;
}

task *func_8C081CAC(void)
{
    task *tp;

    if (B_8C78C248 == NULL) {
        return NULL;
    }
    tp = CreateElementalTask(7, 1, TailsMain);
    if (tp != NULL) {
        tp->twp->counter.b[1] = 2;
        tp->twp->counter.b[0] = 1;
    }
    return tp;
}

ASM_STUB(void func_8C0826B0(taskwk *twp, NJS_POINT3 *pos, Float dist))
ASM_STUB(Sint32 func_8C082820(void))
