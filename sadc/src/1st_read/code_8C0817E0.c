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

extern Sint8 B_8C752BDD;
extern Sint32 B_8C754EC8;
extern Sint32 B_8C798850;
extern Sint32 B_8C79884C;
extern UNK_SPRITE B_8C798880;
extern NJS_TEXLIST D_8C1BE9C0;
extern Uint8 D_8C1BE9C8[];

extern Sint32 njSetTexture(NJS_TEXLIST *texlist);  /* sa1/ninja_funcs.h */
extern void njColorBlendingMode(Sint32 a, Sint32 b);     /* njColorBlendingMode-like */
extern void njDrawSprite2D(UNK_SPRITE *sp, Sint32 n, Float pri, Uint32 attr); /* njDrawSprite2D-like */
extern Uint16 GetStageNumber(void);
extern void func_8C0663CA(task *tp);

void func_8C0819D2(task *tp);

/* Draws two horizontal gauges (value / max scaled to 255 pixels). */
void func_8C0817E0(Float a, Float amax, Float b, Float bmax) {
    UNK_SPRITE *sp;
    Float x1;
    Float x0;

    x0 = a;
    x1 = b;
    if (B_8C752BDD >= 0 && B_8C754EC8 != 0) {
        x0 = 255.0f / amax * x0;
        x1 = 255.0f / bmax * x1;
        njSetTexture(&D_8C1BE9C0);
        njColorBlendingMode(0, 8);
        njColorBlendingMode(1, 6);
        sp = &B_8C798880;
        sp->tlist = &D_8C1BE9C0;
        sp->tanim = D_8C1BE9C8;
        sp->sy = 1.0f;
        sp->sx = 16.0f;
        sp->p.x = 184.0f;
        sp->p.y = 424.0f;
        njDrawSprite2D(sp, 0, -2.004f, 0x20);
        sp->sx = 1.0f;
        sp->p.x = x1 + 184.0f;
        sp->p.y = 416.0f;
        if ((Uint8)(GetStageNumber() >> 8) == 4) {
            njDrawSprite2D(&B_8C798880, 11, -2.001f, 0x20);
        } else {
            njDrawSprite2D(&B_8C798880, 1, -2.001f, 0x20);
        }
        sp->p.x = x0 + 184.0f;
        sp->p.y = 416.0f;
        njDrawSprite2D(&B_8C798880, 2, -2.0f, 0x20);
    }
}

void func_8C0818C8(Sint32 x) {
    B_8C798850 = x;
}

void func_8C0818CE(void) {
    B_8C79884C = 1;
}

void func_8C0818D6(void) {
    B_8C79884C = 2;
}

Sint32 func_8C0818DE(void) {
    return B_8C79884C;
}

void func_8C0818E4(task *tp) {
    taskwk *twp;
    Float s;

    twp = tp->twp;
    s = twp->value.f;
    switch (twp->mode) {
    case 0:
        twp->mode = 1;
        if (B_8C79884C == 1) {
            twp->timer.f = 3.0f;
        } else {
            twp->timer.f = 7.0f;
        }
        twp->value.f = 0.5f;
        tp->disp = func_8C0819D2;
        break;
    case 1:
        s += 0.25f;
        if (s >= 4.0f) {
            twp->mode = 2;
        }
        twp->value.f = s;
        break;
    case 2:
        s -= 0.25f;
        if (s <= 2.0f) {
            twp->mode = 3;
            s = 2.0f;
        }
        twp->value.f = s;
        break;
    case 3:
        break;
    }
    func_8C0663CA(tp);
}

void func_8C0819D2(task *tp) {
    taskwk *twp;
    Float s;
    Sint32 n;
    Sint32 i;

    twp = tp->twp;
    s = twp->value.f;
    if (B_8C754EC8 != 0) {
        njSetTexture(&D_8C1BE9C0);
        njColorBlendingMode(0, 8);
        njColorBlendingMode(1, 6);
        B_8C798880.tlist = &D_8C1BE9C0;
        B_8C798880.tanim = D_8C1BE9C8;
        B_8C798880.sx = s;
        B_8C798880.sy = s;
        B_8C798880.p.x = 320.0f;
        B_8C798880.p.y = 240.0f;
        n = twp->timer.f;
        for (i = 0; i < 4; i++) {
            njDrawSprite2D(&B_8C798880, n++, -2.004f, 0x20);
        }
    }
}
