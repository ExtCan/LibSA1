#include "common.h"
#include "task.h"

extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec); /* CreateElementalTask */
extern void *CAlloc(Sint32 n, Sint32 size);
extern void DestroyTask(task *tp);
extern Sint32 rand(void); /* rand() */
extern void func_8C07C3F2(Uint8 pno, task *target, Uint32 time); /* canonical: sa1/protos.h */

/* 0x6C-byte work allocated for each spawned object (stored in twp->value) */
typedef struct {
    Sint8 unk0[0x64];
    Sint32 unk64;
    Sint32 unk68;
} UNK_8C015820_work;

/* still assembly; placed first so func_8C0158E6 does not fall through into it */
ASM_STUB(void func_8C01593E(task *tp, Sint32 n))

taskwk *func_8C015820(Float x, Float y, Float z, TaskFuncPtr exec) {
    task *tp;
    UNK_8C015820_work *wk;
    taskwk *twp;

    if (exec != NULL) {
        tp = CreateElementalTask(3, 4, exec);
        wk = CAlloc(1, sizeof(UNK_8C015820_work));
        if (tp != NULL) {
            if (wk != NULL) {
                twp = tp->twp;
                twp->pos.x = x;
                twp->pos.y = y;
                twp->pos.z = z;
                twp->ang.y = (Float)rand() * 2.0f;
                func_8C07C3F2(0, tp, 300);
                twp->value.ptr = wk;
                wk->unk64 = 0;
                return twp;
            } else {
                DestroyTask(tp);
                return NULL;
            }
        }
    }
    return NULL;
}

taskwk *func_8C0158B6(Float x, Float y, Float z, TaskFuncPtr exec) {
    taskwk *twp;

    twp = func_8C015820(x, y, z, exec);
    if (twp != NULL) {
        twp->wtimer |= 0x5000;
    }
    return twp;
}

void func_8C0158D2(task *tp) {
    func_8C01593E(tp, 0);
}

void func_8C0158D6(task *tp) {
    func_8C01593E(tp, 1);
}

void func_8C0158DA(task *tp) {
    func_8C01593E(tp, 4);
}

void func_8C0158DE(task *tp) {
    func_8C01593E(tp, 3);
}

void func_8C0158E2(task *tp) {
    func_8C01593E(tp, 5);
}

void func_8C0158E6(task *tp) {
    func_8C01593E(tp, 13);
}
