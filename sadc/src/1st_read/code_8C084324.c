#include "common.h"
#include "ninja.h"
#include "task.h"

/* Controller state (PDS_PERIPHERAL-like) */
typedef struct {
    Uint32 id;          /* 0x00 */
    Uint32 support;     /* 0x04 */
    Uint32 on;          /* 0x08 */
    Uint32 off;         /* 0x0C */
    Uint32 press;       /* 0x10 */
    Uint32 release;     /* 0x14 */
    Uint16 r;           /* 0x18 */
    Uint16 l;           /* 0x1A */
    Sint16 x1;          /* 0x1C */
    Sint16 y1;          /* 0x1E */
} UNK_PERIPHERAL;

/* Analog stick input (angle + magnitude) */
typedef struct {
    Angle ang;
    Float mag;
} UNK_STICK;

typedef struct {
    Uint16 id;
    Sint16 flag;
} UNK_CWK;

typedef struct {
    Sint32 unk00[14];
    Float spd;          /* 0x38 */
} UNK_PWK;

typedef struct {
    UNK_PWK *pwp;       /* 0x00 */
    NJS_POINT3 v;       /* 0x04 */
} UNK_MWK;

typedef struct {
    Sint8 mode;         /* 0x00 */
    Sint8 smode;        /* 0x01 */
    Sint8 id;           /* 0x02 */
    Sint8 btimer;       /* 0x03 */
    Sint16 timer;       /* 0x04 */
    Sint16 count;       /* 0x06 */
    Sint8 *modetbl;     /* 0x08 */
    Sint32 flag;        /* 0x0C */
    Sint32 unk10;       /* 0x10 */
    Sint32 unk14;       /* 0x14 */
    Sint32 unk18[3];    /* 0x18 */
    Angle ang24;        /* 0x24 */
    Sint32 unk28;       /* 0x28 */
    Angle ang2C;        /* 0x2C */
    Float mag30;        /* 0x30 */
    Sint32 unk34;       /* 0x34 */
    Sint32 unk38;       /* 0x38 */
    Uint32 on;          /* 0x3C */
    Uint32 press;       /* 0x40 */
    task *tp44;         /* 0x44 */
    taskwk *twp48;      /* 0x48 */
    UNK_MWK *mwp4C;     /* 0x4C */
    UNK_PWK *pwp50;     /* 0x50 */
    task *tp54;         /* 0x54 */
    taskwk *twp58;      /* 0x58 */
    UNK_MWK *mwp5C;     /* 0x5C */
    UNK_PWK *pwp60;     /* 0x60 */
    Sint32 unk64[3];    /* 0x64 */
    Float range[3];     /* 0x70 */
    Sint32 unk7C;       /* 0x7C */
    Sint16 unk80;       /* 0x80 */
    Sint16 unk82;       /* 0x82 */
    Sint16 unk84;       /* 0x84 */
    Sint16 unk86;       /* 0x86 */
    Sint16 unk88;       /* 0x88 */
    Sint16 unk8A;       /* 0x8A */
    Sint32 unk8C;       /* 0x8C */
    NJS_POINT3 pos90;   /* 0x90 */
} UNK_WORK;             /* 0x9C */

extern UNK_PERIPHERAL B_8C754C3C[];
extern UNK_STICK B_8C754DD4;
extern Sint32 B_8C754C00;
extern Uint32 D_8C19E38C;
extern Uint32 D_8C19E390;

extern Sint32 func_8C049A4E(void);
extern void func_8C04917A(void);
/* func_8C0509DA is 'void func_8C0509DA(Uint8 n)' (sa1/input.h: the callee
 * does EXTU.B R4), and the PC build uses that. SHC keeps an int parameter:
 * the call passes R4 = 1 either way, but with a Uint8 parameter SHC swaps
 * the registers of 0.0f and 0.5f (FR14/FR13) in func_8C084324, which is
 * byte-identical with the int one. */
#if !defined(TARGET_PC)
extern void func_8C0509DA(Sint32 n);
#else
extern void func_8C0509DA(Uint8 n);
#endif
extern Sint32 func_8C084118(UNK_WORK *wk);
extern Angle func_8C0826F4(NJS_POINT3 *a, NJS_POINT3 *b, Float *dist, Float *distxz);
extern void func_8C107D80(void);
extern void func_8C107AC6(Sint32 a);
extern void SetInputP(Uint8 pno, Sint32 mode);
extern Sint32 func_8C0839EC(task *tp);
extern void func_8C0585DC(Sint32 a);
extern void FreeTask(task *tp);
extern void func_8C107BA0(Sint32 se);
extern Sint32 func_8C0C8E32(Sint32 a);
extern Angle func_8C085A50(Float z, Float x);
extern void func_8C08273C(NJS_POINT3 *v, Float *mag, Angle *ang);
extern Sint32 func_8C0827C4(task *a, task *b);
extern void func_8C084074(Uint8 pno1, Uint8 pno2, Float f);
extern void func_8C109EAC(Sint32 a, Sint32 b, Sint32 c, Sint32 d);
extern void func_8C082F80(task *tp);
extern void func_8C098E98(task *tp);

#ifdef NON_MATCHING
/*
 * Tutorial/demo driver that feeds simulated pad input (buttons + analog stick)
 * to the player. Byte-identical to the original: every instruction, every pool
 * constant and the 16-entry jump table (0028 0040 004A 006A 0076 0158 01D4
 * 01E4 01E8 021C 037C 03CA 046A 049E 04DA 0558, checked against the .lis).
 * tools/shcdiff.py cannot evaluate the ".DATA.W Lcase-Lbase" jump-table
 * entries behind the MOVA and always reports that one instruction as
 * differing, so it is kept here until the tool handles switch tables.
 */
void func_8C084324(task *tp)
{
    Float dist;
    Angle ang;
    Float mag;
    UNK_WORK *wk;
    Uint32 press;
    Uint32 on;
    Sint32 r;

    wk = tp->awp;
    if (func_8C049A4E() == 1) {
        return;
    }
    func_8C04917A();
    wk->pwp50 = wk->mwp4C->pwp;
    wk->pwp60 = wk->mwp5C->pwp;
    if (wk->pwp50 == NULL) {
        return;
    }
    if (wk->pwp60 == NULL) {
        return;
    }
    if (wk->twp58->counter.b[1] == 0) {
        ((UNK_CWK *)wk->twp48->cwp)->flag |= 0x800;
    }
    func_8C0509DA(1);
    ang = 0;
    mag = 0.0f;
    on = B_8C754C3C[0].on;
    press = B_8C754C3C[0].press;
    r = func_8C084118(wk);
    if (r & 1) {
        wk->mode = 6;
        wk->twp48->pos = wk->pos90;
    } else if (r & 2) {
    }
    switch (wk->mode) {
    case 0:
        wk->twp48->id = 3;
        wk->timer = 0;
        wk->mode = 1;
        func_8C107D80();
        break;
    case 1:
        func_8C107AC6(10);
        wk->mode = 2;
        break;
    case 2:
        wk->timer++;
        if (wk->timer > wk->unk80) {
            wk->timer = 0;
            wk->mode = 3;
        }
        break;
    case 3:
        SetInputP(1, 34);
        wk->mode = 4;
        break;
    case 4:
        if (func_8C0839EC(tp) == 0) {
            func_8C0585DC(2);
            FreeTask(tp);
            return;
        }
        if (wk->btimer >= 3) {
            wk->mode = 6;
            break;
        }
        wk->flag &= ~1;
        if (wk->pwp50->spd == 0.0f) {
            ang = func_8C0826F4(&wk->twp48->pos, &wk->twp58->pos, NULL, NULL);
            mag = 0.005f;
        }
        wk->timer++;
        if (wk->timer > wk->unk82 && (wk->twp48->flag & 3)) {
            wk->timer = 0;
            if (wk->btimer == 1) {
                if (wk->count & 1) {
                    wk->mode = 8;
                } else {
                    wk->mode = 10;
                }
                wk->count++;
            } else {
                wk->mode = wk->modetbl[wk->btimer];
                wk->flag |= 1;
            }
        }
        break;
    case 5:
        if (wk->btimer < 3) {
            ang = func_8C0826F4(&wk->twp48->pos, &wk->twp58->pos, NULL, &dist);
            mag = 1.0f;
            ang += 0x8000;
        }
        if (!(wk->twp48->flag & 0x8000)) {
            wk->mode = 3;
            if (wk->btimer == 2) {
                if (wk->id == 2) {
                    func_8C107BA0(0xA6);
                } else if (wk->id == 3) {
                    if (B_8C754C00 == 0) {
                        func_8C107BA0(0xA8);
                    } else {
                        func_8C107BA0(0xA6);
                    }
                }
            }
        }
        break;
    case 6:
        func_8C0585DC(1);
        FreeTask(tp);
        return;
    case 7:
        break;
    case 8:
        ang = func_8C0826F4(&wk->twp48->pos, &wk->twp58->pos, NULL, &dist);
        mag = 1.0f;
        if (dist < wk->range[0]) {
            wk->smode = 0;
            wk->mode = 9;
        }
        break;
    case 9:
        ang = func_8C0826F4(&wk->twp48->pos, &wk->twp58->pos, NULL, &dist);
        mag = 1.0f;
        if (wk->pwp50->spd > 2.0f) {
            wk->pwp50->spd = 1.9f;
        }
        switch (wk->smode) {
        case 0:
            if (wk->pwp50->spd < 0.5f) {
                wk->pwp50->spd = 0.5f;
            }
            press |= D_8C19E38C;
            wk->smode = 1;
            break;
        case 1:
            if (wk->timer++ > 8) {
                press |= D_8C19E38C;
                wk->timer = 0;
                wk->smode = 2;
            }
            break;
        case 2:
            if (wk->timer++ > 8) {
                if (wk->twp58->flag & 3) {
                }
                press |= D_8C19E38C;
                wk->timer = 0;
                wk->smode = 3;
            }
            break;
        case 3:
            mag = 1.0f;
            if (func_8C0C8E32(1) != 4) {
                wk->timer = 0;
                wk->pwp50->spd *= 0.5f;
                ang = func_8C085A50(wk->mwp4C->v.z, wk->mwp4C->v.x) + 0x800;
                mag = 1.0f;
                wk->smode = 4;
            }
            break;
        case 4:
            mag = 0.0f;
            if (!(wk->pwp50->spd > 0.02f) || wk->timer++ > 120) {
                wk->timer = 0;
                wk->smode = 0;
                wk->mode = 3;
            }
            break;
        }
        break;
    case 10:
        ang = func_8C0826F4(&wk->twp48->pos, &wk->twp58->pos, NULL, &dist);
        mag = 1.0f;
        if (dist < wk->range[1]) {
            press = D_8C19E38C;
            if (!(wk->pwp50->spd > 2.0f)) {
                wk->pwp50->spd = 2.01f;
            }
            wk->smode = 0;
            wk->mode = 11;
        }
        break;
    case 11:
        ang = func_8C0826F4(&wk->twp48->pos, &wk->twp58->pos, NULL, &dist);
        if (!(wk->timer & wk->unk88)) {
            mag = 0.0f;
        } else {
            mag = 1.0f;
        }
        wk->timer++;
        if (wk->timer > wk->unk86) {
            press |= D_8C19E38C;
            func_8C08273C(&wk->mwp4C->v, &mag, &ang);
            mag = 1.0f;
            wk->timer = 0;
            wk->mode = 3;
        } else if ((wk->twp48->flag & 0x500) != 0x500) {
            wk->timer = 0;
            wk->mode = 3;
        }
        break;
    case 12:
        ang = func_8C0826F4(&wk->twp48->pos, &wk->twp58->pos, NULL, NULL);
        mag = 0.005f;
        if (wk->twp48->flag & 3) {
            press |= D_8C19E390;
            wk->mode = 13;
        }
        break;
    case 13:
        ang = func_8C0826F4(&wk->twp48->pos, &wk->twp58->pos, NULL, NULL);
        mag = 1.0f;
        if (wk->mwp4C->v.y > 0.0f) {
            on |= D_8C19E390;
        } else {
            press |= D_8C19E390;
            wk->timer = 0;
            wk->mode = 14;
        }
        break;
    case 14:
        ang = func_8C0826F4(&wk->twp48->pos, &wk->twp58->pos, NULL, NULL);
        wk->timer++;
        mag = 0.5f;
        if (wk->twp48->flag & 3) {
            wk->timer = 0;
            if (func_8C0C8E32(1) == 5) {
                wk->mode = 15;
            } else {
                wk->mode = 3;
            }
        } else {
            on |= D_8C19E390;
        }
        break;
    case 15:
        mag = 1.0f;
        B_8C754C3C[0].y1 = -127;
        if (func_8C0C8E32(1) != 5 && func_8C0C8E32(1) != 6) {
            wk->mode = 3;
        }
        break;
    }
    if (func_8C0827C4(wk->tp44, wk->tp54)) {
        wk->mode = 2;
        wk->smode = 0;
        wk->timer = 0;
        wk->tp44->twp->flag &= ~4;
        wk->tp54->twp->flag &= ~4;
        func_8C084074(0, 1, 1.0f);
        func_8C109EAC(0, 3, 50, 2);
    } else if (!(wk->twp48->flag & 0x8000) && (wk->twp48->flag & 4)) {
        wk->btimer++;
        wk->timer = 0;
        wk->smode = 0;
        wk->mode = 5;
        func_8C107BA0(0xA4);
    }
    B_8C754C3C[0].on = on;
    B_8C754C3C[0].press = press;
    B_8C754DD4.ang = ang;
    B_8C754DD4.mag = mag;
    wk->ang24 = ang;
    wk->ang2C = ang;
    wk->mag30 = mag;
    wk->on = on;
    wk->press = press;
    func_8C082F80(tp);
    func_8C098E98(tp);
}
#endif
