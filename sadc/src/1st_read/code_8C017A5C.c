#include "common.h"
#include "task.h"

/* spawn parameters */
typedef struct {
    NJS_POINT3 pos;     /* 0x00 */
    Float unkC;         /* 0x0C */
    Sint32 unk10;       /* 0x10 */
    Sint32 unk14;       /* 0x14 */
} UNK_8C017A5C_param;

/* 0x14-byte work allocated per object */
typedef struct {
    Sint16 unk0;        /* 0x00 */
    Sint16 unk2;        /* 0x02 */
    Sint32 unk4;        /* 0x04 */
    Float unk8;         /* 0x08 */
    Float unkC;         /* 0x0C */
    Sint32 unk10;       /* 0x10 */
} UNK_8C017A5C_work;

/* 0x6C-byte work hung on taskwk->value.ptr by func_8C015820 */
typedef struct {
    Uint8 pad0[0x64];
    Sint32 unk64;                   /* 0x64 */
    UNK_8C017A5C_work *unk68;       /* 0x68 */
} UNK_8C015820_work;

extern taskwk *func_8C015820(Float x, Float y, Float z, TaskFuncPtr exec);
extern void *CAlloc(Sint32 n, Sint32 size);
extern Sint32 rand(void); /* rand() */
extern void func_8C01593E(task *tp, Sint32 n);

void func_8C017AF0(task *tp);

taskwk *func_8C017A5C(UNK_8C017A5C_param *p, Sint32 arg) {
    taskwk *twp;
    UNK_8C015820_work *ow;
    UNK_8C017A5C_work *wk;

    if (p != NULL) {
        twp = func_8C015820(p->pos.x, p->pos.y, p->pos.z, func_8C017AF0);
        if (twp != NULL) {
            ow = twp->value.ptr;
            wk = CAlloc(1, sizeof(UNK_8C017A5C_work));
            twp->counter.l = p->unk10;
            twp->wtimer |= 0x7000;
            if (wk != NULL) {
                ow->unk68 = wk;
                ow->unk64 = arg;
                wk->unk0 = SA1_FTOI(SA1_FMAC((Float)rand(), 0.000244140625f, 0.0f));
                wk->unk2 = 1;
                wk->unk4 = 0;
                wk->unk8 = 1.0f;
                wk->unkC = p->unkC;
                wk->unk10 = p->unk14;
                return twp;
            }
        }
    }
    return NULL;
}

void func_8C017AF0(task *tp) {
    func_8C01593E(tp, 15);
}
