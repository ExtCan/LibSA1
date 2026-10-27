#include "common.h"
#include "task.h"

typedef struct {
    Sint32 unk0;
    Sint32 unk4;
    Sint32 unk8;
    Sint32 unkC;
    Sint32 unk10;
    Sint32 unk14;
    Sint32 unk18;
    Sint32 unk1C;
    Sint32 unk20;
    Sint32 *unk24;
} UNK_8C084AE0_obj;

typedef struct {
    Sint32 unk0;
    UNK_8C084AE0_obj *unk4;
    UNK_8C084AE0_obj *unk8;
    Sint32 unkC;
    Sint32 unk10;
} UNK_8C084AE0_arg;

typedef struct {
    Sint8 unk0;
    Sint8 unk1;
    Sint8 unk2;
    Sint8 unk3;
    Sint32 unk4;
    void *unk8;
    Sint32 unkC;
    Sint32 unk10;
    Sint32 unk14;
    Sint8 pad18[0x44 - 0x18];
    UNK_8C084AE0_obj *unk44;
    Sint32 unk48;
    Sint32 *unk4C;
    Sint32 unk50;
    UNK_8C084AE0_obj *unk54;
    Sint32 unk58;
    Sint32 *unk5C;
    Sint32 unk60;
    Sint8 pad64[0x70 - 0x64];
    Float unk70[2];             /* 0x70 */
    Sint8 pad78[0x80 - 0x78];
    Sint16 unk80;
    Sint16 unk82;
    Sint16 unk84;
    Sint8 pad86[0x9C - 0x86];
} UNK_8C084AE0_work;

extern Sint8 B_8C7A2EAA;
extern Sint32 D_8C1BEFD8;

extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec);
extern void *CAlloc(Sint32 n, Sint32 size);
extern void func_8C084BA0(task *tp);
extern void func_8C083EF8(task *tp);
extern void func_8C082F80(task *tp);
extern void func_8C082EB2(task *tp);
extern void func_8C0585DC(Sint32 n);
extern void func_8C048B00(task *tp);

task *func_8C084AE0(UNK_8C084AE0_arg *arg) {
    task *tp;
    UNK_8C084AE0_work *wk;
    UNK_8C084AE0_obj *obj;

    B_8C7A2EAA = 0;
    obj = arg->unk8;
    tp = CreateElementalTask(2, 0, func_8C084BA0);
    if (tp == NULL) {
        return NULL;
    }
    wk = CAlloc(1, sizeof(UNK_8C084AE0_work));
    tp->awp = wk;
    wk->unk44 = obj;
    wk->unk48 = obj->unk20;
    wk->unk4C = obj->unk24;
    wk->unk50 = *wk->unk4C;
    wk->unk54 = arg->unk4;
    wk->unk58 = wk->unk54->unk20;
    wk->unk5C = wk->unk54->unk24;
    wk->unk60 = *wk->unk5C;
    wk->unk2 = arg->unk0;
    wk->unk10 = arg->unkC;
    wk->unk14 = arg->unk10;
    wk->unk8 = &D_8C1BEFD8;
    wk->unk70[0] = 100.0f;
    wk->unk70[1] = 100.0f;
    wk->unk80 = 0;
    wk->unk82 = 60;
    wk->unk84 = 60;
    func_8C083EF8(tp);
    tp->disp = func_8C082F80;
    tp->dest = func_8C082EB2;
    func_8C0585DC(0);
    func_8C048B00(tp);
    return tp;
}
