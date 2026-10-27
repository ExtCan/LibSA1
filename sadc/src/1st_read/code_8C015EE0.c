#include "common.h"
#include "task.h"

/* Collision work (taskwk.cwp) fields used here */
typedef struct UNK_8C015EE0_colli {
    Uint16 id;                          /* 0x00 */
    Uint16 flag;                        /* 0x02 */
    Sint8 unk4[0x14];
    struct UNK_8C015EE0_colli *hit_cwp; /* 0x18 */
} UNK_8C015EE0_colli;

typedef struct {
    Uint16 unk0;                        /* 0x00 */
    Sint8 unk2[0xA];
    Float unkC;                         /* 0x0C */
} UNK_8C015EE0_param;

/* 0x6C-byte work allocated by func_8C015820 (twp->value.ptr) */
typedef struct UNK_8C015EE0_work {
    Uint8 unk0;                         /* 0x00 */
    Sint8 unk1[3];
    Float unk4;                         /* 0x04 */
    NJS_POINT3 spd;                     /* 0x08 */
    NJS_POINT3 acc;                     /* 0x14 */
    Sint8 unk20[0x34];                  /* 0x20 */
    NJS_POINT3 oldpos;                  /* 0x54 */
    void (*exec)(struct UNK_8C015EE0_work *wk, taskwk *twp); /* 0x60 */
    Sint32 unk64;                       /* 0x64 */
    UNK_8C015EE0_param *param;          /* 0x68 */
} UNK_8C015EE0_work;

extern void func_8C01798A(void *p, Sint32 n);
extern void func_8C0940DA(Sint32 se, Sint32 a, Sint32 b, Sint32 c);
extern void func_8C017C82(NJS_POINT3 *pos, Sint32 n);
extern void func_8C017B20(NJS_POINT3 *pos, Sint32 n, Sint32 flag);
extern void func_8C026D40(Sint8 n);
extern void func_8C017828(UNK_8C015EE0_work *wk, taskwk *twp);
extern Float atan2f(Float y, Float x);

/* sign-extends a 16-bit angle difference (the argument is evaluated three times) */
#define SHORT_ANGLE(a) (((a) & 0x8000) ? ((a) | 0xFFFF0000) : ((a) & 0xFFFF))

void func_8C01622A(UNK_8C015EE0_work *wk, taskwk *twp);
void func_8C016C60(UNK_8C015EE0_work *wk, taskwk *twp);

void func_8C015EE0(UNK_8C015EE0_work *wk, taskwk *twp) {
    switch (twp->mode) {
    case 0:
        if (twp->wtimer & 0x100) {
            if (!(twp->wtimer & 0x40)) {
                twp->mode = 2;
            }
        } else if (twp->timer.w[0] > 600) {
            twp->mode = 1;
        }
        break;
    case 1:
        break;
    }
}

void func_8C015F1C(UNK_8C015EE0_work *wk, taskwk *twp, task *tp) {
    if (twp->timer.w[0] > 30 && (((UNK_8C015EE0_colli *)twp->cwp)->flag & 2)) {
        if (((UNK_8C015EE0_colli *)twp->cwp)->hit_cwp->id == 0) {
            func_8C0940DA(28, 0, 0, 0);
            if (twp->wtimer & 0x40) {
                func_8C017C82(&twp->pos, wk->param->unk0);
            }
            func_8C017B20(&twp->pos, wk->param->unk0, twp->wtimer & 8);
            if (wk->unk0 < 15) {
                func_8C026D40(wk->unk0);
            }
            twp->wtimer |= 0x8000;
        } else {
            if (((UNK_8C015EE0_colli *)twp->cwp)->flag & 8) {
                func_8C01798A(wk->unk20, 1);
            } else {
                func_8C01798A(wk->unk20, 6);
            }
        }
    }
}

void func_8C016006(UNK_8C015EE0_work *wk, taskwk *twp) {
    if (!(twp->wtimer & 0x4000)) {
        if (twp->timer.w[0] == 180) {
            twp->wtimer &= 0xFFBF;
            twp->wtimer |= 0x80;
            func_8C017C82(&twp->pos, wk->param->unk0);
        }
        if (twp->timer.w[0] == 181) {
            twp->wtimer &= 0xFF7F;
        }
    }
    if (twp->wtimer & 0x40) {
        func_8C017828(wk, twp);
    }
}

void func_8C016076(UNK_8C015EE0_work *wk, taskwk *twp) {
    NJS_VECTOR v;
    Sint32 ang;
    Float d;

    (void)&v; /* address-taken: keeps SHC from caching &v in a register */
    func_8C01622A(wk, twp);
    d = SA1_FMAC(wk->spd.x, wk->spd.x, wk->spd.z * wk->spd.z);
    if (d > 0.00039999997f) {
        ang = SA1_FTOI(atan2f(wk->spd.x, wk->spd.z) * 10430.381f);
        twp->ang.y += (Sint32)SHORT_ANGLE(ang - twp->ang.y) >> 2;
    }
    wk->exec(wk, twp);
    func_8C016C60(wk, twp);
    if (twp->wtimer & 0x200) {
        wk->acc.x = 0.0f;
        wk->acc.z = 0.0f;
        wk->spd.x = 0.0f;
        wk->spd.z = 0.0f;
    }
    twp->wtimer &= 0xFDFF;
    wk->oldpos.x = twp->pos.x;
    wk->oldpos.y = twp->pos.y;
    wk->oldpos.z = twp->pos.z;
    twp->pos.x += wk->spd.x;
    twp->pos.y += wk->spd.y;
    twp->pos.z += wk->spd.z;
    wk->spd.x += wk->acc.x;
    wk->spd.y += wk->acc.y;
    wk->spd.z += wk->acc.z;
    v.x = wk->spd.x * 0.33587f;
    v.y = wk->spd.y * 0.33587f;
    v.z = wk->spd.z * 0.33587f;
    wk->spd.x -= v.x;
    wk->spd.y -= v.y;
    wk->spd.z -= v.z;
    wk->acc.x = 0.0f;
    wk->acc.y = 0.0f;
    wk->acc.z = 0.0f;
}

ASM_STUB(void func_8C01622A(UNK_8C015EE0_work *wk, taskwk *twp))
ASM_STUB(void func_8C016C60(UNK_8C015EE0_work *wk, taskwk *twp))
