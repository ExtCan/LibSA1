#include "common.h"
#include "ninja.h"
#include "task.h"

/*
 * Mission card / pause-menu helpers. Same translation unit as
 * code_8C014480.c (the original object spans 8C014480-8C015720).
 */

/*
 * TU_EXPERIMENT compiles this file together with code_8C014480.c, as in the
 * original object; then func_8C014A78 matches too:
 *   tools/shcdiff.py func_8C014A78 src/1st_read/code_8C014A78.c
 *       --flags=-define=TU_EXPERIMENT,NON_MATCHING
 */
#ifdef TU_EXPERIMENT
#include "1st_read/code_8C014480.c"
#define NJS_ARGB UNK_ARGB
#define NJS_SPRITE UNK_SPRITE
#else

/* Ninja ARGB float color (njSetConstantMaterial argument) */
typedef struct {
    Float a;
    Float r;
    Float g;
    Float b;
} NJS_ARGB;

/* Ninja sprite (njDrawSprite2D argument) */
typedef struct {
    NJS_POINT3 p;
    Float sx;
    Float sy;
    Angle ang;
    NJS_TEXLIST *tlist;
    void *tanim;
} NJS_SPRITE;
#endif

extern Uint16 GetStageNumber(void);
extern Sint32 GetPlayerNumber(void);
extern Sint32 func_8C608C82(NJS_TEXLIST *tlist);
extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec); /* CreateElementalTask */
extern void func_8C0147D0(task *tp);
extern void func_8C014936(task *tp);
extern Float njSin(Angle ang); /* 8C63A9B8 (FSCA sine) */
extern Sint32 njSetTexture(NJS_TEXLIST *tlist);
extern void njColorBlendingMode(Sint32 a, Sint32 b);
extern void njSetConstantMaterial(NJS_ARGB *argb);
extern void njDrawSprite2D(NJS_SPRITE *sp, Sint32 n, Float pri, Uint32 attr);
extern Sint32 func_8C6088BC(NJS_TEXLIST *tlist);

extern Sint32 B_8C754BFC;
extern NJS_TEXLIST B_8C6B6878;
extern NJS_TEXNAME D_8C161CF0[];
extern NJS_TEXNAME D_8C161F54[];
extern char D_8C161CE8[];
extern Sint32 B_8C754EC8;
extern NJS_TEXLIST D_8C1BFDA0;
extern Uint8 D_8C1621CC[];
extern NJS_TEXLIST D_8C1621E0;
extern NJS_ARGB D_8C1115B8;
extern NJS_ARGB D_8C1115C8;
extern NJS_ARGB D_8C1115D8;

#ifndef TU_EXPERIMENT
ASM_STUB(Sint32 func_8C014480(Sint32 chr, Sint32 level))
#endif
void func_8C015220(void);
void func_8C015566(void);
void func_8C015594(Sint32 n, Sint32 x, Sint32 sel, Float f);

#ifdef NON_MATCHING
/*
 * Matches only when compiled in one file with code_8C014480.c (verified by
 * #including it): the literal pool shared with func_8C01493E is dumped in the
 * middle of this function, which turns the first two early returns into
 * BF/BRA far branches, and the first call address goes to R2 instead of R3.
 */
void func_8C014A78(void)
{
    Sint32 level;
    Sint32 chr;
    Sint32 n;
    char *s;
    task *tp;

    level = GetStageNumber() >> 8;
    chr = GetPlayerNumber();
    if (level == 0) {
        return;
    }
    if (level > 14) {
        return;
    }
    n = func_8C014480(chr, level);
    if (n < 0) {
        return;
    }
    if (B_8C754BFC == 0) {
        B_8C6B6878.textures = &D_8C161CF0[n];
    } else {
        B_8C6B6878.textures = &D_8C161F54[n];
        s = B_8C6B6878.textures->texname;
        while (*s) {
            s++;
        }
        *--s = D_8C161CE8[B_8C754BFC];
    }
    B_8C6B6878.nbTexture = 1;
    func_8C608C82(&B_8C6B6878);
    tp = CreateElementalTask(2, 1, func_8C0147D0);
    tp->twp->mode = 0;
    tp->twp->counter.b[1] = chr;
    tp->twp->wtimer = 60;
    tp->twp->pos.x = 320.0f;
    tp->twp->pos.y = 240.0f;
    tp->dest = func_8C014936;
}
#endif

void func_8C014B86(void)
{
}

void func_8C014B8A(task *tp)
{
    NJS_SPRITE sp;
    NJS_ARGB argb;
    taskwk *twp;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    sp.tlist = &D_8C1BFDA0;
    sp.tanim = D_8C1621CC;
    sp.sx = 1.0f;
    sp.sy = 1.0f;
    sp.p.x = 496.0f;
    sp.p.y = 48.0f;
    twp->wtimer += 0x400;
    twp->scl.z = SA1_FMAC(njSin(twp->wtimer), 0.5f, 0.5f);
    njSetTexture(&D_8C1BFDA0);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    argb.a = twp->scl.z;
    argb.r = 1.0f;
    argb.g = 1.0f;
    argb.b = 1.0f;
    njSetConstantMaterial(&argb);
    njDrawSprite2D(&sp, 0, -1.09999993f, 0x22); /* 0xBF8CCCCC */
    argb.a = 0.0f;
    argb.r = 0.0f;
    argb.g = 0.0f;
    argb.b = 0.0f;
    njSetConstantMaterial(&argb);
}

void func_8C014C42(void)
{
    func_8C6088BC(&D_8C1BFDA0);
}

void func_8C014C4A(Sint32 sel)
{
    NJS_ARGB argb;

    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    njSetTexture(&D_8C1621E0);
    func_8C015220();
    func_8C015566();
    if (B_8C754BFC == 0) {
        func_8C015594(5, -48, sel == 0, 0.0f);
        func_8C015594(4, 0, sel == 1, 0.0f);
        func_8C015594(6, 48, sel == 2, 0.0f);
    } else {
        func_8C015594(12, -48, sel == 0, 0.0f);
        func_8C015594(11, 0, sel == 1, 0.0f);
        func_8C015594(13, 48, sel == 2, 0.0f);
    }
    argb = D_8C1115B8;
    njSetConstantMaterial(&argb);
}

void func_8C014D6E(Sint32 sel)
{
    NJS_ARGB argb;

    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    njSetTexture(&D_8C1621E0);
    func_8C015220();
    func_8C015566();
    if (B_8C754BFC == 0) {
        func_8C015594(5, -32, sel == 0, 0.0f);
        func_8C015594(6, 32, sel == 1, 0.0f);
    } else {
        func_8C015594(12, -32, sel == 0, 0.0f);
        func_8C015594(13, 32, sel == 1, 0.0f);
    }
    argb = D_8C1115C8;
    njSetConstantMaterial(&argb);
}

void func_8C014DFC(void)
{
    NJS_ARGB argb;

    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    njSetTexture(&D_8C1621E0);
    func_8C015220();
    func_8C015566();
    argb = D_8C1115D8;
    njSetConstantMaterial(&argb);
}

ASM_STUB(void func_8C015220(void))
ASM_STUB(void func_8C015566(void))
ASM_STUB(void func_8C015594(Sint32 n, Sint32 x, Sint32 sel, Float f))
