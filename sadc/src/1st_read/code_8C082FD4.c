#include "common.h"
#include "ninja.h"
#include "task.h"

/* collision work (taskwk.cwp) */
typedef struct {
    Uint16 id;          /* 0x00 */
    Sint16 flag;        /* 0x02 */
} UNK_CWP;

typedef struct {
    Sint32 unk00;       /* 0x00 */
    Sint16 flag;        /* 0x04 */
    Sint16 unk06;       /* 0x06 */
    Sint32 unk08[12];   /* 0x08 */
    Float unk38;        /* 0x38 */
} UNK_8C082FD4_50;

typedef struct {
    UNK_8C082FD4_50 *wk; /* 0x00 */
    NJS_POINT3 pos;     /* 0x04 */
} UNK_8C082FD4_4C;

typedef struct {
    Sint8 mode;         /* 0x00 */
    Sint8 smode;        /* 0x01 */
    Sint8 id;           /* 0x02 */
    Sint8 btimer;       /* 0x03 */
    Sint16 timer;       /* 0x04 */
    Sint16 unk06;       /* 0x06 */
    Sint8 *modetbl;     /* 0x08 */
    Uint32 flag;        /* 0x0C */
    Sint32 unk10;       /* 0x10 */
    Sint32 unk14;       /* 0x14 */
    NJS_POINT3 tgt;     /* 0x18 */
    Sint32 ang;         /* 0x24 */
    Float dist;         /* 0x28 */
    Sint32 ang2;        /* 0x2C */
    Float spd;          /* 0x30 */
    Sint32 unk34;       /* 0x34 */
    Sint32 unk38;       /* 0x38 */
    Uint32 unk3C;       /* 0x3C */
    Uint32 unk40;       /* 0x40 */
    task *tp1;          /* 0x44 */
    taskwk *twp1;       /* 0x48 */
    UNK_8C082FD4_4C *pw1; /* 0x4C */
    UNK_8C082FD4_50 *wk1; /* 0x50 */
    task *tp2;          /* 0x54 */
    taskwk *twp2;       /* 0x58 */
    UNK_8C082FD4_4C *pw2; /* 0x5C */
    UNK_8C082FD4_50 *wk2; /* 0x60 */
    Sint32 unk64[3];    /* 0x64 */
    Float unk70[1];     /* 0x70 */
    Float unk74;        /* 0x74 */
    Float unk78;        /* 0x78 */
    Sint32 unk7C;       /* 0x7C */
    Sint16 unk80;       /* 0x80 */
    Sint16 unk82;       /* 0x82 */
    Sint16 unk84;       /* 0x84 */
    Sint16 unk86;       /* 0x86 */
    Sint16 unk88;       /* 0x88 */
    Sint16 unk8A;       /* 0x8A */
    Sint32 unk8C;       /* 0x8C */
    NJS_POINT3 unk90;   /* 0x90 */
} UNK_8C082FD4_AWP;


typedef struct {
    Sint32 ang;
    Float spd;
} UNK_8C754DD4;

typedef struct {
    Uint32 id;
    Uint32 support;
    Uint32 on;
    Uint32 off;
    Uint32 press;
} UNK_PER;
extern UNK_PER B_8C754C3C;
extern UNK_8C754DD4 B_8C754DD4;
extern Sint32 D_8C19E38C;
extern Sint32 D_8C19E390;

extern Sint32 func_8C049A4E(void);
extern void func_8C04917A(void);
extern void func_8C0509DA(Uint8 n); /* sa1/input.h */
extern Sint32 func_8C084118(void *awp); /* sa1/protos.h: flag word of the AI work */
extern Sint32 func_8C0826F4(NJS_POINT3 *a, NJS_POINT3 *b, Float *c, Float *d);
extern void func_8C107D80(void);
extern void func_8C107AC6(Sint32 a);
extern void func_8C107BA0(Sint32 a);
extern void func_8C0585DC(Sint32 a);
extern void FreeTask(task *tp);
extern void func_8C098E98(task *tp);
extern Sint32 func_8C085A50(Float y, Float x);
extern void func_8C08273C(NJS_POINT3 *p, Float *spd, Sint32 *ang);
extern Sint32 func_8C0827C4(task *a, task *b);
extern void func_8C109EAC(Sint32 a, Sint32 b, Sint32 c, Sint32 d);

ASM_STUB(void func_8C082F80(task *tp))
Sint32 func_8C0839EC(task *tp);
void func_8C084074(Uint8 a, Uint8 b, Float f);

#ifdef NON_MATCHING
void func_8C082FD4(task *tp)
{

    Sint32 ang;
    Float dist;
    Float spd;
    UNK_8C082FD4_AWP *awp;
    Uint32 flg1;
    Uint32 flg2;
    Uint32 ret;
    UNK_PER *per;

    Float f;
    taskwk *twp;
    Sint32 flag;

    awp = tp->awp;
    if (func_8C049A4E() == 1) {
        return;
    }
    func_8C04917A();
    awp->wk1 = awp->pw1->wk;
    awp->wk2 = awp->pw2->wk;
    if (awp->wk1 == NULL) {
        return;
    }
    if (awp->wk2 == NULL) {
        return;
    }
    ((UNK_CWP *)awp->twp2->cwp)->flag |= 0x800;
    func_8C0509DA(1);
    ang = 0;
    spd = 0.0f;
    per = &B_8C754C3C;
    flg1 = per->on;
    flg2 = per->press;

    awp->wk1->flag |= 2;
    ret = func_8C084118(awp);
    if (ret & 1) {
        awp->mode = 6;
        awp->twp1->pos = awp->unk90;
    } else if (ret & 2) {
    }
    switch (awp->mode) {
    case 0:
        awp->twp1->id = 3;
        awp->timer = 0;
        awp->mode = 1;
        func_8C107D80();
        break;
    case 1:
        func_8C107AC6(10);
        awp->mode = 2;
        break;
    case 2:
        awp->timer++;
        if (awp->timer > awp->unk80) {
            awp->timer = 0;
            awp->mode = 3;
        }
        break;
    case 3:
        if (!func_8C0839EC(tp)) {
            func_8C0585DC(2);
            FreeTask(tp);
            return;
        }
        if (awp->btimer >= 3) {
            awp->mode = 6;
            break;
        }
        if (awp->wk1->unk38 == 0.0f) {
            ang = func_8C0826F4(&awp->twp1->pos, &awp->twp2->pos, NULL, NULL);
            spd = 0.005f;
        }
        awp->timer++;
        if (awp->timer > awp->unk82) {
            awp->timer = 0;
            awp->mode = 4;
        }
        awp->flag &= ~1;
        break;
    case 4:
        if (awp->timer++ > 20) {
            awp->timer = 0;
            awp->mode = awp->modetbl[awp->btimer];
            awp->flag |= 1;
        }
        break;
    case 5:
        if (awp->btimer < 3) {
            ang = func_8C0826F4(&awp->twp1->pos, &awp->twp2->pos, NULL, &dist);
            spd = 1.0f;
            ang += 0x8000;
        }
        if (!(awp->twp1->flag & 0x8000)) {
            awp->mode = 3;
            if (awp->btimer == 2) {
                if (awp->id == 0) {
                    func_8C107BA0(0xA7);
                } else if (awp->id == 1) {
                    func_8C107BA0(0xAA);
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
        ang = func_8C0826F4(&awp->twp1->pos, &awp->twp2->pos, NULL, &dist);
        spd = 1.0f;
        if (dist < awp->unk70[0] && (awp->twp1->flag & 3)) {
            {
                awp->tgt = awp->twp2->pos;

                f = dist * 0.016666667f;
                if (f > 1.0f) {
                    f = 1.0f;
                } else if (f < 0.5f) {
                    f = 0.5f;
                }
                awp->wk1->unk38 = f;
                flg2 |= D_8C19E390;
                awp->mode = 9;
            }
        }
        break;
    case 9:
        if (awp->twp1->flag & 3) {
            awp->timer = 0;
            awp->mode = 3;
            break;
        }
        ang = func_8C0826F4(&awp->twp1->pos, &awp->tgt, NULL, &dist);
        if (dist > 30.0f) {
            flg1 |= D_8C19E390;
            spd = 1.0f;
        } else if (dist > 0.1f) {
            spd = dist * 0.033333333f;
        } else {
            spd = 0.0f;
        }
        break;
    case 10:
        ang = func_8C0826F4(&awp->twp1->pos, &awp->twp2->pos, NULL, &dist);
        spd = 1.0f;
        if (dist < awp->unk74) {
            spd = 0.0f;
            flg2 |= D_8C19E38C;
            flg1 |= D_8C19E38C;
            awp->timer = 0;
            awp->mode = 12;
        }
        break;
    case 11:
        if (awp->twp1->flag & 0x100) {
            awp->timer = 0;
            awp->mode = 12;
        } else {
            spd = 0.0f;
            flg2 |= D_8C19E38C;
            flg1 |= D_8C19E38C;
        }
        break;
    case 12:
        ang = func_8C0826F4(&awp->twp1->pos, &awp->twp2->pos, NULL, &dist);
        spd = 1.0f;
        awp->timer++;
        if (awp->timer < awp->unk84) {
            flg1 |= D_8C19E38C;
        } else {
            awp->timer = 0;
            switch (awp->id) {
            case 0:
                awp->mode = 13;
                break;
            case 1:
                awp->mode = 19;
                awp->tgt = awp->twp2->pos;
                break;
            }
        }
        break;
    case 13:
        ang = func_8C0826F4(&awp->twp1->pos, &awp->twp2->pos, NULL, &dist);
        if (!(awp->timer & awp->unk88)) {
            spd = 1.0f;
        } else {
            spd = 0.0f;
        }
        awp->timer++;
        if (awp->timer > awp->unk86) {
            awp->timer = 0;
            if (!(awp->twp2->flag & 3) && dist < awp->unk70[0]) {

                flg2 |= D_8C19E390;
                awp->mode = 14;
            } else {
                ang = -func_8C085A50(awp->pw1->pos.z, awp->pw1->pos.x);

                spd = 1.0f;
                flg2 |= D_8C19E38C;
                awp->mode = 3;
            }
        } else if ((awp->twp1->flag & 0x500) != 0x500) {
            awp->timer = 0;
            awp->mode = 3;
        }
        break;
    case 14:
        if (awp->twp1->flag & 3) {
            awp->timer = 0;
            awp->mode = 3;
        } else {
            flg1 |= D_8C19E390;
            ang = func_8C0826F4(&awp->twp1->pos, &awp->twp2->pos, NULL, &dist);
            spd = 1.0f;
        }
        break;
    case 15:
        ang = func_8C0826F4(&awp->twp1->pos, &awp->twp2->pos, NULL, &dist);
        if (dist < awp->unk78) {
            if (awp->twp1->flag & 3) {
                flg2 |= D_8C19E390;
                flg1 |= D_8C19E390;
                awp->timer = 0;
                awp->mode = 16;

            }
        } else {
            spd = 1.0f;
        }
        break;
    case 16:
        ang = func_8C0826F4(&awp->twp1->pos, &awp->twp2->pos, &dist, NULL);
        spd = 1.0f;
        if (!(awp->twp1->flag & 3)) {
            if (awp->twp1->pos.y - awp->twp2->pos.y < 20.0f) {
                flg1 |= D_8C19E390;
            } else {
                awp->dist = dist;
                if (awp->unk78 * awp->unk78 > awp->dist) {
                    ((UNK_CWP *)awp->twp2->cwp)->flag |= 0x800;
                    flg2 |= D_8C19E390;
                    awp->mode = 17;
                }
            }
        } else {
            awp->mode = 3;
        }
        break;
    case 17:
        if ((awp->twp1->flag & 0x500) == 0x500) {
            ((UNK_CWP *)awp->twp2->cwp)->flag |= 0x800;
        } else {
            ((UNK_CWP *)awp->twp2->cwp)->flag &= 0xF7FF;
            awp->mode = 3;
        }
        break;
    case 18:
        awp->tgt = awp->twp2->pos;
        flg2 |= D_8C19E38C;
        flg1 |= D_8C19E38C;
        awp->timer = 0;
        awp->mode = 12;

        break;
    case 19:
        ang = func_8C0826F4(&awp->twp1->pos, &awp->tgt, NULL, &dist);
        if (dist < 0.1f) {
            spd = 0.0f;
        } else {
            spd = 1.0f;
        }
        if (dist < 10.0f) {
            awp->timer = 0;
            if (awp->twp2->flag & 3) {
                flg2 |= D_8C19E38C;
                func_8C08273C(&awp->pw1->pos, &spd, &ang);
                awp->mode = 3;
            } else {
                flg2 |= D_8C19E390;
                awp->mode = 14;
            }
        } else if ((awp->twp1->flag & 0x500) != 0x500) {
            awp->timer = 0;
            awp->mode = 3;
        }
        break;
    }
    if (func_8C0827C4(awp->tp1, awp->tp2)) {
        awp->mode = 2;
        awp->smode = 0;
        awp->timer = 0;
        awp->tp1->twp->flag &= ~4;
        awp->tp2->twp->flag &= ~4;
        func_8C084074(0, 1, 1.0f);
        func_8C109EAC(0, 3, 50, 2);
    } else {
        twp = awp->twp1;
        flag = twp->flag;
        if (!(flag & 0x8000) && (flag & 4)) {
            awp->btimer++;
            awp->timer = 0;
            awp->smode = 0;
            awp->mode = 5;
            func_8C107BA0(0xA5);
        }
    }
    per = &B_8C754C3C;
    per->on = flg1;
    per->press = flg2;

    B_8C754DD4.ang = ang;
    B_8C754DD4.spd = spd;
    awp->ang = ang;
    awp->ang2 = ang;
    awp->spd = spd;
    awp->unk3C = flg1;
    awp->unk40 = flg2;
    func_8C082F80(tp);
    func_8C098E98(tp);
}
#else
ASM_STUB(void func_8C082FD4(task *tp))
#endif

ASM_STUB(Sint32 func_8C0839EC(task *tp))
ASM_STUB(void func_8C084074(Uint8 a, Uint8 b, Float f))
