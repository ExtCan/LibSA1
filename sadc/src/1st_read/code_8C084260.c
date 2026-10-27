#include "common.h"
#include "task.h"

/* creation parameters passed in by func_8C083A1E */
typedef struct {
    Sint32 type;                /* 0x00 */
    task *tp4;                  /* 0x04 */
    task *tp8;                  /* 0x08 */
    Sint32 unkC;                /* 0x0C */
    Sint32 unk10;               /* 0x10 */
} UNK_8C084260_arg;

/* 0x9C-byte work attached at task->awp */
typedef struct {
    Sint8 unk0;                 /* 0x00 */
    Sint8 unk1;                 /* 0x01 */
    Sint8 type;                 /* 0x02 */
    Sint8 unk3;                 /* 0x03 */
    Sint32 unk4;                /* 0x04 */
    void *unk8;                 /* 0x08 */
    Sint32 unkC;                /* 0x0C */
    Sint32 unk10;               /* 0x10 */
    Sint32 unk14;               /* 0x14 */
    Sint32 unk18[11];           /* 0x18 */
    task *tp8;                  /* 0x44 */
    taskwk *twp8;               /* 0x48 */
    Sint32 *mwp8;               /* 0x4C */
    Sint32 mwp8_0;              /* 0x50 */
    task *tp4;                  /* 0x54 */
    taskwk *twp4;               /* 0x58 */
    Sint32 *mwp4;               /* 0x5C */
    Sint32 mwp4_0;              /* 0x60 */
    Sint32 unk64[3];            /* 0x64 */
    Float unk70[3];             /* 0x70 */
    Sint32 unk7C;               /* 0x7C */
    Sint16 unk80;               /* 0x80 */
    Sint16 unk82;               /* 0x82 */
    Sint16 unk84;               /* 0x84 */
    Sint16 unk86;               /* 0x86 */
    Sint16 unk88;               /* 0x88 */
    Sint16 unk8A;               /* 0x8A */
    Sint32 unk8C[4];            /* 0x8C */
} UNK_8C084260_work;            /* 0x9C */

extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec);
extern void *CAlloc(Sint32 n, Sint32 size);
extern void func_8C084324(task *tp);
extern void func_8C083EF8(task *tp);
extern void func_8C082F80(task *tp);
extern void func_8C082EB2(task *tp);
extern void func_8C0585DC(Sint32 a);
extern void func_8C048B00(task *tp);
extern Uint8 D_8C1BEFD4[];

task *func_8C084260(UNK_8C084260_arg *arg) {
    task *tp;
    UNK_8C084260_work *wp;
    task *tp8;

    tp8 = arg->tp8;
    tp = CreateElementalTask(2, 0, func_8C084324);
    if (tp == NULL) {
        return NULL;
    }
    wp = CAlloc(1, sizeof(UNK_8C084260_work));
    tp->awp = wp;
    wp->tp8 = tp8;
    wp->twp8 = tp8->twp;
    wp->mwp8 = (Sint32 *)tp8->mwp;
    wp->mwp8_0 = *wp->mwp8;
    wp->tp4 = arg->tp4;
    wp->twp4 = wp->tp4->twp;
    wp->mwp4 = (Sint32 *)wp->tp4->mwp;
    wp->mwp4_0 = *wp->mwp4;
    wp->type = arg->type;
    wp->unk10 = arg->unkC;
    wp->unk14 = arg->unk10;
    wp->unk8 = D_8C1BEFD4;
    wp->unk70[0] = 50.0f;
    wp->unk70[1] = 50.0f;
    wp->unk70[2] = 50.0f;
    wp->unk80 = 120;
    wp->unk82 = 60;
    wp->unk86 = 120;
    wp->unk88 = 1;
    func_8C083EF8(tp);
    tp->disp = func_8C082F80;
    tp->dest = func_8C082EB2;
    func_8C0585DC(0);
    func_8C048B00(tp);
    return tp;
}
