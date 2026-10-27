#include "common.h"
#include "task.h"
#include "sa1/ccl.h"            /* CCL_Init, CCL_INFO */

/* creation parameters (see func_8C083A1E) */
typedef struct {
    Sint32 type;                /* 0x00 */
    task *tp4;                  /* 0x04 */
    task *tp8;                  /* 0x08 */
    Sint32 unkC;                /* 0x0C */
    TaskFuncPtr unk10;          /* 0x10 destructor callback */
} UNK_8C082D60_arg;

/* 0x9C-byte work (task->awp) */
typedef struct {
    Sint8 unk0;                 /* 0x00 */
    Sint8 unk1;                 /* 0x01 */
    Sint8 type;                 /* 0x02 */
    Sint8 unk3;                 /* 0x03 */
    Sint32 unk4;                /* 0x04 */
    Uint8 *unk8;                /* 0x08 */
    Sint32 unkC;                /* 0x0C */
    Sint32 unk10;               /* 0x10 */
    TaskFuncPtr unk14;          /* 0x14 called by func_8C082EB2 */
    Sint8 pad18[0x44 - 0x18];
    task *tp44;                 /* 0x44 */
    taskwk *twp48;              /* 0x48 */
    Sint32 *unk4C;              /* 0x4C */
    Sint32 unk50;               /* 0x50 */
    task *tp54;                 /* 0x54 */
    taskwk *twp58;              /* 0x58 */
    Sint32 *unk5C;              /* 0x5C */
    Sint32 unk60;               /* 0x60 */
    task *tp64;                 /* 0x64 */
    void *unk68;                /* 0x68 */
    void *unk6C;                /* 0x6C */
    Float unk70[3];             /* 0x70 */
    Sint32 unk7C;               /* 0x7C */
    Sint16 unk80;               /* 0x80 */
    Sint16 unk82;               /* 0x82 */
    Sint16 unk84;               /* 0x84 */
    Sint16 unk86;               /* 0x86 */
    Sint16 unk88;               /* 0x88 */
    Sint8 pad8A[0x9C - 0x8A];
} UNK_8C082D60_work;

extern Sint8 B_8C7A2EAA;
extern Sint32 B_8C798938;
extern Sint8 B_8C752BDE;
extern Sint8 B_8C752BDF;
extern Sint32 B_8C754EC8;
extern Uint8 D_8C1BEF68[];
extern Uint8 D_8C1BEF6B[];
extern CCL_INFO D_8C1BEF70[];   /* 1 shape, CCL_Init id 2 */
extern Sint32 D_8C1BEFA4;

extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec); /* CreateElementalTask */
extern void *CAlloc(Sint32 n, Sint32 size);
extern void FreeTask(void *p);
extern void DestroyTask(task *tp);
extern void func_8C082FD4(task *tp);
extern void func_8C083818(task *tp);
extern void func_8C083EF8(task *tp);
extern void *func_8C047040(task *tp, taskwk *twp);     /* returns the enemy work (code_8C047040.c), unused here */
extern void func_8C0585DC(Sint32 n);
extern void func_8C048B00(task *tp);
extern void func_8C084200(Sint32 n, void *p);

void func_8C082F80(task *tp);
void func_8C082FB2(task *tp);

task *func_8C082D60(UNK_8C082D60_arg *arg) {
    task *tp;
    UNK_8C082D60_work *wk;
    task *obj;

    B_8C7A2EAA = 0;
    obj = arg->tp8;
    tp = CreateElementalTask(2, 0, func_8C082FD4);
    if (tp == NULL) {
        return NULL;
    }
    wk = CAlloc(1, sizeof(UNK_8C082D60_work));
    tp->awp = wk;
    wk->tp44 = obj;
    wk->twp48 = wk->tp44->twp;
    wk->unk4C = (Sint32 *)wk->tp44->mwp;
    wk->unk50 = *wk->unk4C;
    wk->tp54 = arg->tp4;
    wk->twp58 = wk->tp54->twp;
    wk->unk5C = (Sint32 *)wk->tp54->mwp;
    wk->unk60 = *wk->unk5C;
    wk->type = arg->type;
    wk->unk10 = arg->unkC;
    wk->unk14 = arg->unk10;
    wk->unk70[0] = 70.0f;
    wk->unk70[1] = 50.0f;
    wk->unk70[2] = 100.0f;
    wk->unk80 = 0;
    wk->unk82 = 60;
    wk->unk86 = 180;
    wk->unk88 = 1;
    switch (wk->type) {
    case 0:
        wk->unk84 = 0;
        wk->unk8 = D_8C1BEF68;
        break;
    case 1:
        wk->unk84 = 30;
        wk->unk82 = 30;
        wk->unk8 = D_8C1BEF6B;
        break;
    }
    func_8C083EF8(tp);
    if (wk->type == 1) {
        wk->tp64 = CreateElementalTask(7, 3, func_8C083818);
        func_8C047040(wk->tp64, wk->tp64->twp);
        CCL_Init(wk->tp64, D_8C1BEF70, 2, 1, 1);
        wk->tp64->twp->value.ptr = wk->tp44;
        B_8C7A2EAA = 0;
    }
    tp->disp = func_8C082F80;
    tp->dest = func_8C082FB2;
    func_8C0585DC(0);
    func_8C048B00(tp);
    return tp;
}

/* Same body as func_8C083AA6 (later in this unit). SHC (-speed) inlines this
 * call into func_8C082EB2 and leaves the dead parameter copy "MOV R13,R4" in
 * the delay slot of the final BT/S, which is what the original has. */
static void setB_8C798938(Sint32 n) {
    B_8C798938 = n;
}

void func_8C082EB2(task *tp) {
    UNK_8C082D60_work *wk;

    wk = tp->awp;
    if (wk->unk68 != NULL) {
        FreeTask(wk->unk68);
        wk->unk68 = NULL;
    }
    if (wk->unk6C != NULL) {
        FreeTask(wk->unk6C);
        wk->unk6C = NULL;
    }
    setB_8C798938(0);
    if (wk->unk14 != NULL) {
        wk->unk14(tp);
    }
}

void func_8C082F80(task *tp) {
    UNK_8C082D60_work *wk;
    Sint32 n;

    wk = tp->awp;
    B_8C752BDF = 1;
    B_8C752BDE = 1;
    if (B_8C754EC8 == 0 && wk != NULL) {
        n = 3 - wk->unk3;
        if (n < 0) {
            n = 0;
        }
        func_8C084200(n, &D_8C1BEFA4);
    }
}

void func_8C082FB2(task *tp) {
    UNK_8C082D60_work *wk;

    wk = tp->awp;
    if (wk->tp64 != NULL) {
        DestroyTask(wk->tp64);
    }
    func_8C082EB2(tp);
}
