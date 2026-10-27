#include "common.h"
#include "ninja.h"
#include "task.h"

/* Partner AI (drives a second player's virtual controller). */

typedef struct {
    Sint16 unk0;
    Sint16 flag;                /* 0x02 */
} UNK_CWP;

typedef struct {
    Uint8 pad0[4];
    Sint8 unk4;                 /* 0x04 */
} UNK_7C;

typedef struct {
    Uint8 pad0[4];
    Uint16 flag;                /* 0x04 */
    Uint8 pad6[0x38 - 0x06];
    Float spd;                  /* 0x38 */
    Uint8 pad3C[0x7C - 0x3C];
    UNK_7C *unk7C[1];           /* 0x7C */
} UNK_PWP;

typedef struct {
    UNK_PWP *pwp;               /* 0x00 */
    NJS_VECTOR spd;             /* 0x04 */
} UNK_MWP;

typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 smode;                /* 0x01 */
    Sint8 unk2;                 /* 0x02 */
    Sint8 idx;                  /* 0x03 */
    Sint16 timer;               /* 0x04 */
    Sint16 unk6;                /* 0x06 */
    Sint8 *modetbl;             /* 0x08 */
    Uint32 flag;                /* 0x0C */
    Uint32 unk10;               /* 0x10 */
    Uint32 unk14;               /* 0x14 */
    Uint32 unk18;               /* 0x18 */
    Uint32 unk1C;               /* 0x1C */
    Uint32 unk20;               /* 0x20 */
    Sint32 ang24;               /* 0x24 */
    Sint32 unk28;               /* 0x28 */
    Sint32 ang2C;               /* 0x2C */
    Float stroke;               /* 0x30 */
    Uint32 unk34;               /* 0x34 */
    Uint32 unk38;               /* 0x38 */
    Uint32 on;                  /* 0x3C */
    Uint32 press;               /* 0x40 */
    task *tp;                   /* 0x44 */
    taskwk *twp;                /* 0x48 */
    UNK_MWP *mwp;               /* 0x4C */
    UNK_PWP *pwp;               /* 0x50 */
    task *tp2;                  /* 0x54 */
    taskwk *twp2;               /* 0x58 */
    UNK_MWP *mwp2;              /* 0x5C */
    UNK_PWP *pwp2;              /* 0x60 */
    Uint32 unk64;               /* 0x64 */
    Uint32 unk68;               /* 0x68 */
    Uint32 unk6C;               /* 0x6C */
    Float dist[2];              /* 0x70 */
    Uint32 unk78;               /* 0x78 */
    Uint32 unk7C;               /* 0x7C */
    Sint16 time80;              /* 0x80 */
    Sint16 time82;              /* 0x82 */
    Sint16 time84;              /* 0x84 */
    Sint16 unk86;               /* 0x86 */
    Sint16 count88;             /* 0x88 */
    Sint16 unk8A;               /* 0x8A */
    Uint32 unk8C;               /* 0x8C */
    NJS_POINT3 pos;             /* 0x90 */
} UNK_AIWK;

typedef struct {
    Uint32 id;
    Uint32 support;
    Uint32 on;                  /* 0x08 */
    Uint32 off;
    Uint32 press;               /* 0x10 */
} UNK_PER;

typedef struct {
    Sint32 angle;
    Float stroke;
} UNK_INPUT;

extern Uint8 B_8C7A2EAA;
extern UNK_PER B_8C754C3C[];
extern UNK_INPUT B_8C754DD4;
extern Sint32 D_8C19E38C;
extern Sint32 D_8C19E390;

extern Sint32 func_8C049A4E(void);
extern void func_8C04917A(void);
extern void func_8C0509DA(Uint8 n);
extern Sint32 func_8C084118(void *awp);
extern void func_8C107D80(void);
extern void func_8C107AC6(Sint32 a);
extern void SetInputP(Uint8 pno, Sint32 mode);
extern Sint32 func_8C0839EC(task *tp);
extern void func_8C0585DC(Sint32 a);
extern void FreeTask(task *tp);
extern void func_8C107BA0(Sint32 a);
extern Sint32 func_8C0826F4(NJS_POINT3 *a, NJS_POINT3 *b, Float *c, Float *d);
extern void func_8C08273C(NJS_VECTOR *v, Float *stroke, Sint32 *ang);
extern Float func_8C085980(NJS_VECTOR *v, NJS_VECTOR *out);
extern void func_8C0D2674(task *tp);
extern Sint32 func_8C0D323E(task *tp);
extern void func_8C082F80(task *tp);
extern void func_8C098E98(task *tp);

task *func_8C085532(task *parent, Sint32 mode, NJS_POINT3 *pos);

#ifdef NON_MATCHING
/*
 * Byte-identical to the original: all 1066 instructions match, and so does the
 * 22-entry jump table (checked byte for byte against 8C084CFC). It stays
 * NON_MATCHING only because tools/shcdiff.py cannot evaluate the
 * ".DATA.W Lcase-Lbase" jump-table entries behind the MOVA, so it always
 * reports that one instruction as different, which would fail the splice step.
 * Matching notes: D_8C19E38C/D_8C19E390 must be Sint32 (as Uint32, SHC hoists
 * the D_8C19E390 load to the switch head); wk->dist and pwp->unk7C are arrays
 * (the original forms their address with ADD #imm instead of using R0).
 */
void func_8C084BA0(task *tp)
{
    UNK_AIWK *wk;
    Uint32 on;
    Uint32 ret;
    NJS_VECTOR v;
    Float tmp;
    UNK_7C *pp;
    Uint32 press;
    Float dist;
    Float stroke;
    Sint32 ang;

    wk = (UNK_AIWK *)tp->awp;
    B_8C7A2EAA = 0;
    if (func_8C049A4E() == 1) {
        return;
    }
    func_8C04917A();
    wk->pwp = wk->mwp->pwp;
    wk->pwp2 = wk->mwp2->pwp;
    if (wk->pwp == NULL) {
        return;
    }
    if (wk->pwp2 == NULL) {
        return;
    }
    pp = wk->pwp->unk7C[0];
    if (wk->twp2->counter.b[1] == 0) {
        ((UNK_CWP *)wk->twp->cwp)->flag |= 0x800;
    }
    func_8C0509DA(1);
    ang = 0;
    stroke = 0.0f;
    on = B_8C754C3C[0].on;
    press = B_8C754C3C[0].press;
    wk->pwp->flag |= 0x400;
    ret = func_8C084118(wk);
    if (ret & 1) {
        wk->mode = 6;
        wk->twp->pos = wk->pos;
    } else if (ret & 2) {
    }
    switch (wk->mode) {
    case 0:
        wk->twp->id = 3;
        wk->timer = 0;
        wk->smode = 0;
        wk->mode = 1;
        func_8C107D80();
        break;
    case 1:
        func_8C107AC6(10);
        wk->mode = 2;
        break;
    case 2:
        wk->timer++;
        if (wk->timer > wk->time80) {
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
        if (wk->idx >= 3) {
            wk->mode = 6;
            break;
        }
        wk->flag &= ~1;
        if (wk->pwp->spd == 0.0f) {
            ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, NULL);
            stroke = 0.005f;
        }
        wk->timer++;
        if (wk->timer > wk->time82 && (wk->twp->flag & 3)) {
            wk->timer = 0;
            wk->mode = wk->modetbl[wk->idx];
            wk->flag |= 1;
        }
        break;
    case 5:
        if (wk->idx < 3) {
            ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, &tmp);
            stroke = 1.0f;
            ang += 0x8000;
        }
        if (!(wk->twp->flag & 0x8000)) {
            wk->mode = 3;
            if (wk->idx == 2) {
                func_8C107BA0(0xA9);
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
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, &dist);
        stroke = 1.0f;
        if (wk->pwp->spd > 1.0f) {
            wk->pwp->spd = 1.0f;
        }
        if (dist < wk->dist[0]) {
            wk->ang2C = ang;
            func_8C08273C(&wk->mwp->spd, &stroke, &ang);
            press |= D_8C19E38C;
            wk->mode = 9;
            wk->flag |= 1;
        }
        break;
    case 9:
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, &dist);
        stroke = 0.1f;
        on |= D_8C19E38C;
        wk->timer++;
        if ((wk->timer & 15) == 0) {
            func_8C085980(&wk->mwp2->spd, &v);
            v.x = v.x * wk->pwp2->spd * 20.0f;
            v.y = v.y * wk->pwp2->spd * 20.0f;
            v.z = v.z * wk->pwp2->spd * 20.0f;
            v.x = wk->twp2->pos.x + v.x;
            v.y = wk->twp2->pos.y + v.y;
            v.z = wk->twp2->pos.z + v.z;
            func_8C085532(tp, 0, &v);
        }
        if (wk->timer > wk->time84) {
            wk->timer = 0;
            wk->mode = 3;
            wk->flag &= ~1;
        }
        break;
    case 10:
        press |= D_8C19E390;
        wk->mode = 12;
        break;
    case 12:
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, NULL);
        stroke = 1.0f;
        on |= D_8C19E390;
        if (wk->twp->flag & 3) {
            wk->mode = 3;
        }
        if (wk->mwp->spd.y < 0.0f) {
            press |= D_8C19E38C;
            wk->flag |= 1;
            wk->mode = 13;
            func_8C085532(tp, 0, &wk->twp2->pos);
        }
        break;
    case 13:
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, NULL);
        stroke = 1.0f;
        on |= D_8C19E38C | D_8C19E390;
        if (pp->unk4 > 0) {
            on &= ~D_8C19E38C;
            wk->flag &= ~1;
            wk->mode = 11;
        }
        if (wk->twp->flag & 3) {
            wk->timer = 0;
            on &= ~D_8C19E38C;
            wk->flag &= ~1;
            wk->mode = 3;
        }
        break;
    case 11:
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, NULL);
        stroke = 1.0f;
        on |= D_8C19E390;
        press |= D_8C19E38C;
        wk->mode = 13;
        wk->flag |= 1;
        func_8C085532(tp, 0, &wk->twp2->pos);
        break;
    case 14:
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, &dist);
        stroke = 0.1f;
        func_8C0D2674(wk->tp);
        wk->mode = 15;
        wk->timer = 0;
        break;
    case 15:
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, &tmp, &dist);
        stroke = 1.0f;
        if (tmp < 100.0f) {
            press |= D_8C19E38C;
            wk->flag |= 1;
            func_8C085532(tp, 0, NULL);
            wk->mode = 16;
        }
        break;
    case 16:
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, &dist);
        stroke = 1.0f;
        if (wk->pwp->spd > 2.0f) {
            wk->pwp->spd = 2.0f;
        }
        on |= D_8C19E38C;
        wk->timer++;
        if (pp->unk4 > 0 || wk->timer > 300) {
            on &= ~D_8C19E38C;
            wk->flag &= ~1;
            wk->mode = 3;
            wk->timer = 0;
        }
        break;
    case 17:
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, &dist);
        stroke = 1.0f;
        if (wk->pwp->spd > 1.0f) {
            wk->pwp->spd = 1.0f;
        }
        if (dist < wk->dist[0]) {
            wk->ang2C = ang;
            func_8C08273C(&wk->mwp->spd, &stroke, &ang);
            press |= D_8C19E38C;
            wk->mode = 18;
            wk->flag |= 1;
            func_8C085980(&wk->mwp2->spd, &v);
            v.x = v.x * wk->pwp2->spd * 20.0f;
            v.y = v.y * wk->pwp2->spd * 20.0f;
            v.z = v.z * wk->pwp2->spd * 20.0f;
            v.x = wk->twp2->pos.x + v.x;
            v.y = wk->twp2->pos.y + v.y;
            v.z = wk->twp2->pos.z + v.z;
            func_8C085532(tp, 0, &v);
        }
        break;
    case 18:
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, &dist);
        stroke = 0.1f;
        on |= D_8C19E38C;
        wk->timer++;
        if (pp->unk4 > 0) {
            on &= ~D_8C19E38C;
            wk->count88++;
            if (wk->count88 > 3) {
                wk->mode = 3;
            } else {
                wk->mode = 17;
            }
            wk->flag &= ~1;
        }
        if (wk->timer > 300) {
            wk->mode = 17;
            wk->flag &= ~1;
        }
        break;
    case 19:
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, &dist);
        stroke = 1.0f;
        if (dist < wk->dist[0]) {
            wk->ang2C = ang;
            press |= D_8C19E38C | D_8C19E390;
            wk->mode = 20;
            wk->flag |= 1;
        }
        break;
    case 20:
        ang = func_8C0826F4(&wk->twp->pos, &wk->twp2->pos, NULL, NULL);
        stroke = 1.0f;
        on |= D_8C19E38C | D_8C19E390;
        wk->timer++;
        if ((wk->timer & 15) == 0 && dist < wk->dist[0]) {
            func_8C085532(tp, 0, &wk->twp2->pos);
        }
        if (wk->twp->flag & 3) {
            wk->timer = 0;
            wk->mode = 3;
            on &= ~D_8C19E38C;
            wk->flag &= ~1;
        } else if (pp->unk4 >= 3 || wk->timer > 600 ||
                   (wk->timer > 60 && func_8C0D323E(wk->tp) == 0)) {
            wk->timer = 0;
            wk->mode = 21;
            on &= ~D_8C19E38C;
            wk->flag &= ~1;
        }
        break;
    case 21:
        on |= D_8C19E390;
        if (wk->twp->flag & 3) {
            wk->mode = 3;
        }
        break;
    }
    if (!(wk->twp->flag & 0x8000) && (wk->twp->flag & 4)) {
        wk->idx++;
        wk->timer = 0;
        wk->smode = 0;
        wk->mode = 5;
        wk->flag &= ~1;
    }
    B_8C754C3C[0].on = on;
    B_8C754C3C[0].press = press;
    B_8C754DD4.angle = ang;
    B_8C754DD4.stroke = stroke;
    wk->ang24 = ang;
    wk->ang2C = ang;
    wk->stroke = stroke;
    wk->on = on;
    wk->press = press;
    func_8C082F80(tp);
    func_8C098E98(tp);
}
#endif

ASM_STUB(task *func_8C085532(task *parent, Sint32 mode, NJS_POINT3 *pos))
