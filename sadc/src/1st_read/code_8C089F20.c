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
extern Sint8 B_8C752BDE;
extern Sint8 B_8C752BDF;
#ifdef U1
extern Uint8 B_8C752BE0;
extern Uint16 B_8C752BE4;
#else
extern Sint8 B_8C752BE0;
extern Sint16 B_8C752BE4;
#endif
extern Sint32 B_8C752BF0;
extern Sint32 B_8C752BF4;
extern Sint32 B_8C752BF8;
extern Sint32 B_8C752C00;
extern Sint32 B_8C7997A0;
extern Sint32 B_8C7997A4;

#ifdef VKR
extern void njDrawSprite2D();
#else
extern void njDrawSprite2D(UNK_SPRITE *sp, Sint32 n, Float pri, Uint32 attr); /* njDrawSprite2D-like */
#endif
extern void FreeTask(task *tp); /* FreeTask */
extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec); /* CreateElementalTask */
extern Sint32 func_8C04C960(void);
extern void func_8C0940DA(Sint32 a, Sint32 b, Sint32 c, Sint32 d);

#ifndef SKIP1
/* Draws a number of up to 5 digits with the digit sprite. */
void func_8C089F20(UNK_SPRITE *sp, Sint32 value, Uint8 digits) {
    Sint32 max;
    Sint32 i;
    Sint32 n;
    volatile Sint32 d;

    if (B_8C752BDD < 0) {
        return;
    }
    if (value >= 100000) {
        value = 99999;
    }
    max = 1;
    n = digits;
    for (i = 0; i < n; i++) {
        max *= 10;
    }
    max--;
    if (value > max) {
        value = max;
    }
    if (n >= 5) {
        d = value / 10000;
        njDrawSprite2D(sp, d, -1.5f, 34);
        sp->p.x += 16.0f;
    }
    if (n >= 4) {
        d = value % 10000 / 1000;
        njDrawSprite2D(sp, d, -1.501f, 34);
        sp->p.x += 16.0f;
    }
    if (n >= 3) {
        d = value % 1000 / 100;
        njDrawSprite2D(sp, d, -1.502f, 34);
        sp->p.x += 16.0f;
    }
    if (n >= 2) {
        d = value % 100 / 10;
        njDrawSprite2D(sp, d, -1.503f, 34);
        sp->p.x += 16.0f;
    }
    if (n >= 1) {
        d = value % 10;
        njDrawSprite2D(sp, d, -1.504f, 34);
    }
}

#endif

#ifdef DA
void dummy_c(void) {
    B_8C752BE4 = 1;
}
#endif
#ifdef DB
void dummy_d(void) {
    B_8C752BE0 = 1;
}
#endif
#ifdef DC
void dummy_e(Sint32 x) {
    B_8C752BF8 = x;
}
#endif
#ifdef DD
void dummy_h(void) {
    B_8C752BF0 = 1;
    B_8C752BF4 = 2;
}
#endif
#ifdef DE
void dummy_i(void) {
    B_8C752BF0 = 1;
    B_8C752BF4 = 2;
    B_8C7997A0 = 3;
}
#endif
#ifdef DF
void dummy_j(void) {
    B_8C752BE0 = 5;
}
#endif

#ifdef NON_MATCHING
void func_8C08A04A(void) {
    B_8C752BE4 = 0;
    B_8C752BE0 = 4;
    B_8C752BF8 = 0;
}
#else
ASM_STUB(void func_8C08A04A(void))
#endif

#ifdef NON_MATCHING
void func_8C08A05C(void) {
    B_8C752BF0 = 0;
    B_8C752BF4 = 0;
    B_8C7997A0 = 0;
    B_8C7997A4 = 0;
    B_8C752C00 = 0;
}
#else
ASM_STUB(void func_8C08A05C(void))
#endif

#ifdef DUMMY
void dummy_a(void) {
    B_8C752BE4 = 1;
}
#endif
#ifdef DUMMY2
void dummy_b(void) {
    B_8C752BE4 = 1;
}
#endif
#ifdef DUMMY3
void dummy_f(void) {
    B_8C752BF0 = 1;
}
#endif
#ifdef DUMMY4
void dummy_g(void) {
}
#endif

#ifdef NON_MATCHING
void func_8C08A074(void) {
#if defined(T1)
    B_8C752BE4 = (Sint16)0;
#elif defined(T2)
    *(Sint16 *)&B_8C752BE4 = 0;
#elif defined(T3)
    B_8C752BE4 = 0L;
#elif defined(T4)
    Sint16 *p = &B_8C752BE4;
    *p = 0;
#elif defined(T5)
    B_8C752BE4 = 0.0f;
#else
    B_8C752BE4 = 0;
#endif
}
#else
ASM_STUB(void func_8C08A074(void))
#endif

#ifdef NON_MATCHING
void func_8C08A07C(void) {
    B_8C752BE0 = 4;
}
#else
ASM_STUB(void func_8C08A07C(void))
#endif

#ifdef NON_MATCHING
void func_8C08A084(task *tp) {
    if ((*(Sint32 *)tp->awp)++ > 180) {
        FreeTask(tp);
    } else {
        B_8C752BDE = 0;
    }
}
#else
ASM_STUB(void func_8C08A084(task *tp))
#endif

void func_8C08A120(Sint16 n) {
    if (n > 0) {
        func_8C0940DA(0x2C0, 0, 0, 0);
    }
    B_8C752BE0 += (Sint8)n;
    if (B_8C752BE0 < 0 && n > 0) {
        B_8C752BE0 = 127;
    }
    if (func_8C04C960() == 1) {
        CreateElementalTask(8, 6, func_8C08A084);
    }
}

void func_8C08A178(task *tp) {
    if ((*(Sint32 *)tp->awp)++ > 180) {
        FreeTask(tp);
    } else {
        B_8C752BDF = 0;
    }
}
