#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ccl.h"            /* CCL_Init, CCL_INFO */

/* per-kind parameters (0x14 bytes) */
typedef struct {
    Sint8 unk0[0x0C];
    Float speed;                /* 0x0C frame step */
    Sint8 unk10[4];
} UNK_8C111E34;

/* 0x6C-byte work stored in twp->value (allocated by func_8C015820) */
typedef struct {
    Uint8 kind;                 /* 0x00 */
    Uint8 unk1;                 /* 0x01 */
    Uint8 unk2;                 /* 0x02 */
    Sint8 unk3;                 /* 0x03 */
    Float frame;                /* 0x04 */
    NJS_POINT3 unk8;            /* 0x08 */
    NJS_POINT3 unk14;           /* 0x14 */
    NJS_POINT3 scl;             /* 0x20 */
    Sint8 unk2C[0x0C];          /* 0x2C */
    Angle unk38;                /* 0x38 */
    Float unk3C;                /* 0x3C */
    Sint8 unk40[8];             /* 0x40 */
    NJS_POINT3 unk48;           /* 0x48 */
    NJS_POINT3 unk54;           /* 0x54 */
    void (*unk60)();            /* 0x60 */
    void (*unk64)(task *tp);    /* 0x64 */
    UNK_8C111E34 *unk68;        /* 0x68 */
} UNK_8C01593E_work;

typedef struct {
    Uint16 id;                  /* 0x00 */
    Uint16 flag;                /* 0x02 */
} UNK_cwk_head;

extern taskwk *B_8C78C608[];    /* playertwp[] */
extern Sint32 B_8C754EC8;
extern CCL_INFO D_8C111DCC[];   /* 2 shapes, CCL_Init id 3 */
extern CCL_INFO D_8C111D64[];   /* 2 shapes, CCL_Init id 6 */
extern UNK_8C111E34 D_8C111E34[];
extern NJS_ACTION D_8C1659FC;
extern NJS_ACTION D_8C1688F8;
extern NJS_ACTION D_8C16AF38;
extern NJS_ACTION D_8C16D854;
extern NJS_ACTION D_8C170088;
extern NJS_ACTION D_8C172E3C;
extern NJS_ACTION D_8C176244;
extern NJS_ACTION D_8C178E34;
extern NJS_ACTION D_8C17BBE4;
extern NJS_ACTION D_8C17DFC4;
extern NJS_ACTION D_8C1811CC;
extern NJS_ACTION D_8C184428;
extern NJS_ACTION D_8C1861AC;
extern NJS_ACTION D_8C1887A8;
extern NJS_ACTION D_8C18B7AC;

extern void DestroyTask(task *tp);
extern Uint16 GetStageNumber(void);
extern Sint32 func_8C0586A0(taskwk *twp, motionwk *mwp, Sint32 n); /* movable-object setup (code_8C0586A0.c); result unused here */
extern Sint32 func_8C0919CC(task *tp);
extern void func_8C017648(UNK_8C01593E_work *wk, taskwk *twp);
extern void func_8C017360(UNK_8C01593E_work *wk, taskwk *twp, task *tp, task *tp2, ...);
extern void MFree(void *p);
extern void FreeTask(task *tp);
extern Sint32 rand(void); /* rand() */
extern Sint32 func_8C07CE40(Uint8 pno);        /* canonical: sa1/protos.h */
extern void func_8C09413A(Sint32 a, Sint32 b, Sint32 c, Sint32 d, Float x, Float y, Float z);
extern void func_8C02F63C(taskwk *twp);
extern void func_8C016536();
extern void func_8C016754();
extern void func_8C016800();
extern void func_8C0168A8();
extern void func_8C016A70();

void func_8C01593E(task *tp, Sint32 kind);
void func_8C015A42(task *tp);
void func_8C015AA8(task *tp);
void func_8C015AD0(task *tp);
void func_8C015AD8(task *tp);
Sint32 func_8C015B80(UNK_8C01593E_work *wk, taskwk *twp, Sint32 kind);
void func_8C015DC4(UNK_8C01593E_work *wk, taskwk *twp, task *tp);
void func_8C015EE0(UNK_8C01593E_work *wk, taskwk *twp);
void func_8C015F1C(UNK_8C01593E_work *wk, taskwk *twp, task *tp);
void func_8C016006(UNK_8C01593E_work *wk, taskwk *twp);
void func_8C016076(UNK_8C01593E_work *wk, taskwk *twp);

/* falls through into func_8C01593E */
void func_8C01593C(task *tp)
{
    func_8C01593E(tp, 2);
}

#ifdef NON_MATCHING
void func_8C01593E(task *tp, Sint32 kind)
{
    taskwk *twp;
    UNK_8C01593E_work *wk;
    Uint16 level;

    twp = tp->twp;
    wk = twp->value.ptr;
    if (B_8C78C608[0] == NULL || func_8C015B80(wk, twp, kind) == 0) {
        DestroyTask(tp);
        return;
    }
    level = (Uint8)(GetStageNumber() >> 8);
    if (level == 39 || level == 40 || level == 41) {
        if (wk->kind >= 15) {
            DestroyTask(tp);
            return;
        }
        CCL_Init(tp, D_8C111DCC, 3, 1, 2);
        tp->exec = func_8C015AA8;
        func_8C0586A0(twp, tp->mwp, 4);
        twp->wtimer &= ~0x40;
    } else {
        CCL_Init(tp, D_8C111D64, 6, 1, 2);
        tp->exec = func_8C015A42;
    }
    twp->mode = 0;
    tp->disp = func_8C015AD0;
    tp->dest = func_8C015AD8;
    ((UNK_cwk_head *)twp->cwp)->flag &= ~0x800;
}
#else
ASM_STUB(void func_8C01593E(task *tp, Sint32 kind))
#endif

void func_8C015A42(task *tp)
{
    taskwk *twp;
    UNK_8C01593E_work *wk;

    twp = tp->twp;
    wk = twp->value.ptr;
    if (func_8C0919CC(tp)) {
        if (twp->wtimer > 1500) {
            twp->wtimer |= 0x8000;
        } else {
            return;
        }
    }
    if (twp->wtimer & 0x8000) {
        DestroyTask(tp);
        return;
    }
    func_8C015DC4(wk, twp, tp);
    func_8C017648(wk, twp);
}

#ifdef NON_MATCHING
void func_8C015AA8(task *tp)
{
    taskwk *twp;
    UNK_8C01593E_work *wk;

    twp = tp->twp;
    wk = twp->value.ptr;
    func_8C017360(wk, twp, tp, tp);
    func_8C017648(wk, twp);
}
#else
ASM_STUB(void func_8C015AA8(task *tp))
#endif

void func_8C015AD0(task *tp)
{
    taskwk *twp;
    UNK_8C01593E_work *wk;

    twp = tp->twp;
    wk = twp->value.ptr;
    func_8C017648(wk, twp);
}

void func_8C015AD8(task *tp)
{
    taskwk *twp;
    UNK_8C01593E_work *wk;

    twp = tp->twp;
    wk = twp->value.ptr;
    if (wk != NULL) {
        if (twp->wtimer & 0x2000) {
            MFree(wk->unk68);
        }
        if (wk->unk64 != NULL) {
            wk->unk64(tp);
        }
        MFree(wk);
    }
    FreeTask(tp);
}

#ifdef NON_MATCHING
Sint32 func_8C015B80(UNK_8C01593E_work *wk, taskwk *twp, Sint32 kind)
{
    wk->kind = kind;
    if (!(twp->wtimer & 0x2000)) {
        wk->unk68 = &D_8C111E34[kind];
    }
    switch (kind) {
    case 0:
        twp->counter.ptr = &D_8C1659FC;
        wk->unk60 = func_8C016536;
        break;
    case 1:
        twp->counter.ptr = &D_8C1688F8;
        wk->unk60 = func_8C016754;
        break;
    case 4:
        twp->counter.ptr = &D_8C16AF38;
        wk->unk60 = func_8C016A70;
        break;
    case 3:
        twp->counter.ptr = &D_8C16D854;
        wk->unk60 = func_8C016A70;
        break;
    case 5:
        twp->counter.ptr = &D_8C170088;
        wk->unk60 = func_8C016A70;
        break;
    case 13:
        twp->counter.ptr = &D_8C172E3C;
        wk->unk60 = func_8C016536;
        break;
    case 8:
        twp->counter.ptr = &D_8C176244;
        wk->unk60 = func_8C0168A8;
        break;
    case 6:
        twp->counter.ptr = &D_8C178E34;
        wk->unk60 = func_8C016800;
        break;
    case 7:
        twp->counter.ptr = &D_8C17BBE4;
        wk->unk60 = func_8C0168A8;
        break;
    case 12:
        twp->counter.ptr = &D_8C17DFC4;
        wk->unk60 = func_8C016536;
        break;
    case 11:
        twp->counter.ptr = &D_8C1811CC;
        wk->unk60 = func_8C016536;
        break;
    case 10:
        twp->counter.ptr = &D_8C184428;
        wk->unk60 = func_8C016800;
        break;
    case 9:
        twp->counter.ptr = &D_8C1861AC;
        wk->unk60 = func_8C016536;
        break;
    case 14:
        twp->counter.ptr = &D_8C1887A8;
        wk->unk60 = func_8C016754;
        break;
    case 2:
        twp->counter.ptr = &D_8C18B7AC;
        wk->unk60 = func_8C016536;
        break;
    case 15:
    default:
        wk->unk60 = func_8C016A70;
        break;
    }
    wk->unk2 = 0;
    if (twp->counter.ptr != NULL) {
        wk->frame = (Float)rand() * (Float)((NJS_ACTION *)twp->counter.ptr)->motion->nbFrame * 3.0517579e-05f;
    }
    wk->unk1 = SA1_FTOI(SA1_FMAC((Float)rand(), 0.0077819825f, 0.0f));
    wk->unk3C = 10.0f;
    wk->unk38 = SA1_FTOI((Float)rand() * 2.0f);
    wk->unk54.x = twp->pos.x;
    wk->unk54.y = twp->pos.y;
    wk->unk54.z = twp->pos.z;
    twp->timer.w[0] = 0;
    wk->scl.x = 0.84999998f;
    wk->scl.y = 1.3f;
    wk->scl.z = 0.84999998f;
    twp->wtimer |= 0x40;
    if (twp->wtimer & 0x1000) {
        wk->unk48.x = twp->pos.x;
        wk->unk48.y = twp->pos.y;
        wk->unk48.z = twp->pos.z;
    }
    if (func_8C07CE40(0) == 6) {
        twp->wtimer |= 0x100;
    }
    wk->unk14.x = 0.0f;
    wk->unk14.y = 0.0f;
    wk->unk14.z = 0.0f;
    wk->unk8.x = 0.0f;
    wk->unk8.y = 0.0f;
    wk->unk8.z = 0.0f;
    return 1;
}
#else
ASM_STUB(Sint32 func_8C015B80(UNK_8C01593E_work *wk, taskwk *twp, Sint32 kind))
#endif

void func_8C015DC4(UNK_8C01593E_work *wk, taskwk *twp, task *tp)
{
    twp->timer.w[0]++;
    if (twp->timer.w[0] > 0x3FFF) {
        twp->timer.w[0] = 0x3FFF;
    } else if (twp->timer.w[0] == 15) {
        func_8C09413A(24, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
    }
    if (twp->counter.ptr != NULL) {
        wk->frame += wk->unk68->speed;
        if (wk->frame > (Float)((NJS_ACTION *)twp->counter.ptr)->motion->nbFrame) {
            wk->frame -= (Float)((NJS_ACTION *)twp->counter.ptr)->motion->nbFrame;
        }
    }
    func_8C015EE0(wk, twp);
    func_8C015F1C(wk, twp, tp);
    func_8C016006(wk, twp);
    if (B_8C754EC8 == 0) {
        func_8C016076(wk, twp);
    }
    func_8C02F63C(twp);
}

ASM_STUB(void func_8C015EE0(UNK_8C01593E_work *wk, taskwk *twp))
ASM_STUB(void func_8C015F1C(UNK_8C01593E_work *wk, taskwk *twp, task *tp))
ASM_STUB(void func_8C016006(UNK_8C01593E_work *wk, taskwk *twp))
ASM_STUB(void func_8C016076(UNK_8C01593E_work *wk, taskwk *twp))
ASM_STUB(void func_8C016536())
ASM_STUB(void func_8C016754())
ASM_STUB(void func_8C016800())
ASM_STUB(void func_8C0168A8())
ASM_STUB(void func_8C016A70())
