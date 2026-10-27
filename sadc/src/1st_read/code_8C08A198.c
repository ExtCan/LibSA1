#include "common.h"
#include "ninja.h"
#include "task.h"

/* NJS_SPRITE-like */
typedef struct {
    NJS_POINT3 p;
    Float sx;
    Float sy;
    Angle ang;
    void *tlist;
    void *tanim;
} UNK_SPRITE;

extern Sint16 B_8C752BE4;   /* ring count */
extern Sint8 B_8C752BE0;    /* life count */
extern Sint8 B_8C752BDE;
extern Sint8 B_8C752BDF;
extern Sint32 B_8C752BF0;
extern Sint32 B_8C752BF4;
extern Sint32 B_8C7997A0;
extern Sint32 B_8C7997A4;
extern Sint32 B_8C7997AC;
extern Uint8 B_8C78B451;
extern taskwk *B_8C78C608[];
extern UNK_SPRITE D_8C1BF4E0;
extern UNK_SPRITE D_8C1BF5B4;

extern Sint32 func_8C04C960(void);
extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec); /* CreateElementalTask */
extern void FreeTask(task *tp); /* FreeTask */
extern void func_8C08A178(task *tp);
extern void njColorBlendingMode(Sint32 a, Sint32 b);
extern void func_8C605480(Sint32 a);
extern void njDrawSprite2D(UNK_SPRITE *sp, Sint32 n, Float pri, Uint32 attr);
extern void func_8C043A20(Float a, Float r, Float g, Float b);
extern Sint32 func_8C0A11DE(void);
extern void func_8C0A11BE(void);
extern Uint16 GetStageNumber(void);
extern Sint32 func_8C04FE40(void);
extern Float njSin(Sint32 ang);
extern Sint32 GetPlayerNumber(void);

ASM_STUB(void func_8C089F20(UNK_SPRITE *sp, Sint32 n, Uint8 digits))
ASM_STUB(void func_8C08A120(Sint16 n))

void func_8C08A198(Sint16 n)
{
    Sint16 old;

    old = B_8C752BE4;
    B_8C752BE4 += n;
    {
        Sint32 a;
        Sint32 b;

        a = old / 100;
        b = B_8C752BE4 / 100;
        if (a < b) {
            func_8C08A120(b - a);
        }
    }
    if (func_8C04C960() == 1) {
        CreateElementalTask(8, 6, func_8C08A178);
    }
}

void func_8C08A1EA(task *tp)
{
    FreeTask(tp);
}

#ifdef NON_MATCHING
void func_8C08A1F0(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        if (B_8C78C608[0]->flag & 3) {
            twp->mode = 1;
            B_8C7997A0 = 0;
            B_8C7997A4 = 0;
        } else {
            B_8C7997A0 = 120;
        }
        break;
    case 1:
        FreeTask(tp);
        break;
    }
}
#else
ASM_STUB(void func_8C08A1F0(task *tp))
#endif

void func_8C08A272(Sint32 n)
{
    if (B_8C7997A0 > 0) {
        n += B_8C7997A4;
        if (n > 500) {
            n = 500;
        }
        B_8C7997A4 = n;
    } else {
        B_8C7997A4 = 0;
    }
    B_8C7997A0 = 120;
}

void func_8C08A298(Sint32 n)
{
    B_8C752BF0 += n;
}

void func_8C08A2A2(Sint32 n)
{
    func_8C08A272(n);
    B_8C752BF4 += n;
}

Sint16 func_8C08A2BC(void)
{
    return B_8C752BE4;
}

#ifdef NON_MATCHING
void func_8C08A2C2(void)
{
    UNK_SPRITE *sp;
    Float c;

    sp = &D_8C1BF4E0;
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    func_8C605480(0);
    sp->sx = sp->sy = 1.0f;
    if (B_8C752BDF >= 0) {
        if (func_8C0A11DE() && B_8C78B451 == 1) {
            func_8C0A11BE();
        } else {
            switch ((Sint32)GetStageNumber() >> 8) {
            case 26:
            case 27:
            case 28:
            case 29:
            case 30:
            case 31:
            case 32:
            case 33:
            case 34:
                njDrawSprite2D(&D_8C1BF5B4, 0, -2.0f, 0x20);
                break;
            default:
                sp->p.x = 16.0f;
                sp->p.y = 32.0f;
                njDrawSprite2D(sp, 11, -2.0f, 0x20);
                break;
            }
        }
        sp->p.x = 46.0f;
        sp->p.y = 52.0f;
        if (B_8C752BE4 == 0) {
            if (!func_8C04FE40()) {
                B_8C7997AC += 0x400;
            }
            c = njSin(B_8C7997AC);
            c = 1.0f - c * c;
        } else {
            c = 1.0f;
        }
        func_8C043A20(1.0f, 1.0f, c, c);
        func_8C089F20(sp, B_8C752BE4, 3);
        func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
    }
    if (B_8C752BDE >= 0) {
        sp->p.x = 16.0f;
        sp->p.y = 400.0f;
        njDrawSprite2D(sp, GetPlayerNumber() + 12, -2.0f, 0x20);
        sp->p.x = 49.0f;
        sp->p.y = 416.0f;
        func_8C043A20(1.0f, 1.0f, 1.0f, 1.0f);
        func_8C089F20(sp, B_8C752BE0, 2);
        func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
    }
    func_8C605480(1);
}
#else
ASM_STUB(void func_8C08A2C2(void))
#endif
