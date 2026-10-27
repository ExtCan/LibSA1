#include "common.h"
#include "ninja.h"
#include "task.h"

/* Controller state (Katana PDS_PERIPHERAL layout) */
typedef struct {
    Uint32 id;                  /* 0x00 */
    Uint32 support;             /* 0x04 */
    Uint32 on;                  /* 0x08 */
    Uint32 off;                 /* 0x0C */
    Uint32 press;               /* 0x10 */
    Uint32 release;             /* 0x14 */
    Uint16 r;                   /* 0x18 */
    Uint16 l;                   /* 0x1A */
    Sint16 x1;                  /* 0x1C */
    Sint16 y1;                  /* 0x1E */
} UNK_per;

/* Collision shape entry (0x34 bytes in SA1 DC) */
typedef struct {
    Uint32 attr;                /* 0x00 */
    Uint8 flag4;                /* 0x04 */
    Uint8 pad05[0x2F];
} UNK_ccl;                      /* 0x34 */

/* Collision work (SA1 DC layout) */
typedef struct {
    Uint16 id;                  /* 0x00 */
    Uint16 pad02;
    Uint16 nbInfo;              /* 0x04 */
    Uint16 pad06;
    Sint32 pad08;
    UNK_ccl *info;              /* 0x0C */
} UNK_cwk;

/* Player motion work (B_8C78C628[]) */
typedef struct {
    Float unk0;                 /* 0x00 */
    NJS_POINT3 spd;             /* 0x04 */
} UNK_mwk;

/* Player work (B_8C78C668[], see func_8C07CFDE) */
typedef struct {
    Uint8 pad00[0x38];
    NJS_POINT3 spd;             /* 0x38 */
} UNK_pwk;

extern taskwk *B_8C78C608[];    /* playertwp[] */
extern taskwk *B_8C78C60C[];    /* = &B_8C78C608[1] (separate symbol: shcdiff cannot parse sym+4 pool entries) */
extern UNK_mwk *B_8C78C628[];
extern UNK_per *B_8C754DAC[];  /* controller pointer (read as array: address is hoisted, not the load) */
extern UNK_per B_8C754C3C[];   /* controller state (button masks at +8 on, +0x10 press) */
extern Uint32 D_8C19E390;
extern Sint32 B_8C78C24C;

extern UNK_pwk *func_8C07CFDE(Uint8 pno);
extern void SetInputP(Uint8 pno, Sint32 mode);
extern task *func_8C07CFA0(Uint8 pno);
extern void func_8C0509B8(Uint8 n);
extern void func_8C0509DA(Uint8 n);
extern Sint32 func_8C07CEF2(Uint8 pno, Sint32 frame, NJS_POINT3 *pos, Angle3 *ang);
extern Sint32 func_8C049A4E(void);
extern void func_8C049160(NJS_POINT3 *pos);

ASM_STUB(Sint32 func_8C081B8A(void))
ASM_STUB(Sint32 func_8C081C36(void))

void func_8C082160(task *tp);
Sint32 func_8C082570(task *tp, taskwk *p0, taskwk *p1);
void func_8C0826B0(taskwk *twp, NJS_POINT3 *pos, Float f);
Sint32 func_8C082820(void);

void func_8C081D00(Uint8 pno)
{
    UNK_mwk *mwp;
    UNK_pwk *pwp;

    mwp = B_8C78C628[pno];
    pwp = func_8C07CFDE(pno);
    if (mwp != NULL && pwp != NULL) {
        mwp->spd.z = 0.0f;
        mwp->spd.y = 0.0f;
        mwp->spd.x = 0.0f;
        pwp->spd.z = 0.0f;
        pwp->spd.y = 0.0f;
        pwp->spd.x = 0.0f;
        SetInputP(pno, 48);
    }
}

void func_8C081D4E(task *tp)
{
    taskwk *twp;
    taskwk *p0;
    taskwk *p1;
    task *t0;
    task *t1;
    Sint32 i;
    Sint32 ret;
    NJS_POINT3 unused;          /* never referenced; reproduces the 20-byte frame */

    twp = tp->twp;
    p0 = B_8C78C608[0];
    p1 = B_8C78C60C[0];
    t0 = func_8C07CFA0(0);
    t1 = func_8C07CFA0(1);
    if (t0 == NULL) {
        twp->mode = 0;
    }
    if (t1 == NULL || p0 == NULL || p1 == NULL) {
        return;
    }
    if (func_8C082820() == 0) {
        SetInputP(1, 12);
        func_8C0509B8(1);
        twp->mode = 6;
    }
    if (p1 != NULL) {
        for (i = 0; i < ((UNK_cwk *)p1->cwp)->nbInfo; i++) {
            ((UNK_cwk *)p1->cwp)->info[i].flag4 &= ~0x20;
        }
    }
    switch (twp->mode) {
    case 0:
        func_8C0509DA(1);
        twp->timer.w[0] = 0;
        func_8C0826B0(p0, &p1->pos, 10.0f);
        func_8C07CEF2(1, 0, &B_8C78C60C[0]->pos, NULL);
        twp->mode = 4;
        twp->smode = 0;
        break;
    case 1:
        p1->pos = p0->pos;
        p1->pos.y += 1000000.0f;
        func_8C07CEF2(1, 0, &t1->twp->pos, NULL);
        if ((B_8C754C3C[0].press & 8) && func_8C049A4E() != 1 && func_8C082820() != 0) {
            func_8C081C36();
        }
        break;
    case 2:
        func_8C0509DA(1);
        func_8C049160(&p1->pos);
        p1->pos.y += 50.0f;
        func_8C07CEF2(1, 0, &t1->twp->pos, NULL);
        func_8C081D00(1);
        B_8C754C3C[0].on |= D_8C19E390;
        B_8C754C3C[0].press |= D_8C19E390;
        twp->smode = 3;
        twp->mode = 4;
        break;
    case 3:
        func_8C0509DA(1);
        func_8C081D00(1);
        B_8C754C3C[0].on |= D_8C19E390;
        B_8C754C3C[0].press |= D_8C19E390;
        twp->smode = 3;
        break;
    case 4:
        if ((B_8C754DAC[0]->on & 0x306F6) || (Float)(B_8C754DAC[0]->x1 << 8) != 0.0f || (Float)(B_8C754DAC[0]->y1 << 8) != 0.0f) {
            func_8C0509B8(1);
            twp->mode = 5;
        } else {
            func_8C0509DA(1);
        }
        func_8C082160(tp);
        ret = func_8C082570(tp, p0, p1);
        switch (ret) {
        case 1:
            func_8C081C36();
            break;
        case 2:
            twp->mode = 3;
            break;
        default:
            break;
        }
        break;
    case 5:
        if (!(B_8C754DAC[0]->on & 0x306FE) && (Float)(B_8C754DAC[0]->x1 << 8) == 0.0f && (Float)(B_8C754DAC[0]->y1 << 8) == 0.0f) {
            twp->timer.w[0]++;
            if (twp->timer.w[0] > 300) {
                func_8C0509DA(1);
                twp->mode = 4;
            }
        } else {
            func_8C0509B8(1);
            twp->timer.w[0] = 0;
        }
        ret = func_8C082570(tp, p0, p1);
        switch (ret) {
        case 1:
            func_8C081C36();
            break;
        case 2:
            twp->mode = 3;
            break;
        default:
            break;
        }
        break;
    case 6:
        p1->pos = p0->pos;
        p1->pos.y += 1000000.0f;
        func_8C07CEF2(1, 0, &t1->twp->pos, NULL);
        if (func_8C082820() == 1) {
            func_8C081B8A();
            return;
        }
        break;
    case 7:
        p1->pos = p0->pos;
        p1->pos.y += 1000000.0f;
        func_8C07CEF2(1, 0, &t1->twp->pos, NULL);
        if (twp->timer.w[0]++ > 3) {
            SetInputP(1, 12);
            twp->mode = 8;
        }
        break;
    case 8:
        p1->pos = p0->pos;
        p1->pos.y += 1000000.0f;
        func_8C07CEF2(1, 0, &t1->twp->pos, NULL);
        if (B_8C78C24C == 0) {
            SetInputP(1, 24);
            func_8C0509DA(1);
            twp->mode = 2;
        }
        break;
    }
}

ASM_STUB(void func_8C082160(task *tp))
ASM_STUB(Sint32 func_8C082570(task *tp, taskwk *p0, taskwk *p1))
ASM_STUB(void func_8C0826B0(taskwk *twp, NJS_POINT3 *pos, Float f))
ASM_STUB(Sint32 func_8C082820(void))
