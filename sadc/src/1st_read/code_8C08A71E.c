#include "common.h"
#include "ninja.h"

/* NJS_SPRITE-like */
typedef struct {
    Float x;
    Float y;
    Float z;
} UNK_POINT3;

typedef struct {
    UNK_POINT3 p;
    Float sx;
    Float sy;
    Sint32 ang;
    void *tlist;
    void *tanim;
} UNK_SPRITE;

extern Sint32 B_8C752BE8;
extern Sint8 B_8C752BDD;
extern Sint8 B_8C752BDF;
extern Uint8 B_8C78B451;
extern Sint32 B_8C752BF0;
extern Sint8 B_8C752BE1; /* timer minutes */
extern Sint8 B_8C752BE2; /* timer seconds */
extern Sint8 B_8C752BE3; /* timer frames */
extern NJS_TEXLIST D_8C1BF1A4;
extern Uint8 D_8C1BF288[];

extern Sint32 GetPlayerNumber(void);
extern Uint16 GetStageNumber(void);
extern Sint32 func_8C0A1218(void);
extern Sint32 func_8C0A11DE(void);
extern void func_8C0A1120(Sint32 num);
extern void njColorBlendingMode(Sint32 a, Sint32 b);
extern void func_8C605480(Sint32 a);
extern Sint32 njSetTexture(NJS_TEXLIST *texlist);
extern void njDrawSprite2D(UNK_SPRITE *sp, Sint32 n, Float pri, Uint32 attr);
extern Float njSin(Sint32 ang);
extern void func_8C043A20(Float a, Float r, Float g, Float b);
extern float _builtin_fabsf(float x);

static void func_8C08A7E0(Float x, Float y);

#ifdef NON_MATCHING
Sint32 func_8C08A71E(void) {
    return B_8C752BE8;
}
#else
ASM_STUB(Sint32 func_8C08A71E(void))
#endif

#ifdef NON_MATCHING
void func_8C08A724(void) {
    Sint32 lv;

    if (B_8C752BDD >= 0 && B_8C752BDF >= 0 && GetPlayerNumber() != 7) {
        lv = GetStageNumber() >> 8;
        if (lv != 36 && lv != 37 && func_8C0A1218() == 0) {
            if (func_8C0A11DE() != 0 && B_8C78B451 == 1) {
                func_8C0A1120(B_8C752BF0);
            } else {
                func_8C08A7E0(64.0f, 33.0f);
            }
        }
    }
}
#else
ASM_STUB(void func_8C08A724(void))
#endif

/* Draws the game timer (mm:ss:ff) as sprites at (x, y). */
static void func_8C08A7E0(Float x, Float y) {
    UNK_SPRITE sp;
    Float g;
    Float b;
    Sint16 n;
    Uint32 f;

    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    func_8C605480(0);
    njSetTexture(&D_8C1BF1A4);
    sp.tlist = &D_8C1BF1A4;
    sp.tanim = D_8C1BF288;
    sp.sx = 1.0f;
    sp.sy = 1.0f;
    sp.p.x = x;
    sp.p.y = y;
    if (B_8C752BE1 > 99) {
        njDrawSprite2D(&sp, 9, -2.001f, 0x20);
        sp.p.x += 16.0f;
        njDrawSprite2D(&sp, 9, -2.0f, 0x20);
        sp.p.x += 16.0f;
        njDrawSprite2D(&sp, 10, -2.001f, 0x20);
        sp.p.x += 16.0f;
        njDrawSprite2D(&sp, 5, -2.0f, 0x20);
        sp.p.x += 16.0f;
        njDrawSprite2D(&sp, 9, -2.001f, 0x20);
        sp.p.x += 16.0f;
        njDrawSprite2D(&sp, 10, -2.001f, 0x20);
        sp.p.x += 16.0f;
        njDrawSprite2D(&sp, 9, -2.0f, 0x20);
        sp.p.x += 16.0f;
        njDrawSprite2D(&sp, 9, -2.001f, 0x20);
    } else {
        g = 1.0f;
        b = 1.0f;
        if (GetPlayerNumber() == 6 && B_8C752BE1 < 1 && B_8C752BE2 < 30) {
            b = _builtin_fabsf(njSin((Sint32)(B_8C752BE3 * 1092.2667f)));
            if (B_8C752BE2 < 15) {
                g = b;
                b = 0.0f;
            } else {
                g = 1.0f;
            }
        }
        func_8C043A20(1.0f, 1.0f, g, b);
        n = B_8C752BE1 / 10;
        njDrawSprite2D(&sp, n, -2.001f, 0x22);
        sp.p.x += 16.0f;
        n = B_8C752BE1 % 10;
        njDrawSprite2D(&sp, n, -2.0f, 0x22);
        sp.p.x += 16.0f;
        njDrawSprite2D(&sp, 10, -2.001f, 0x22);
        sp.p.x += 16.0f;
        n = B_8C752BE2 / 10;
        njDrawSprite2D(&sp, n, -2.0f, 0x22);
        sp.p.x += 16.0f;
        n = B_8C752BE2 % 10;
        njDrawSprite2D(&sp, n, -2.001f, 0x22);
        sp.p.x += 16.0f;
        njDrawSprite2D(&sp, 10, -2.001f, 0x22);
        f = B_8C752BE3 * 1.6666666f;
        sp.p.x += 16.0f;
        n = f / 10;
        njDrawSprite2D(&sp, n, -2.0f, 0x22);
        sp.p.x += 16.0f;
        n = f % 10;
        njDrawSprite2D(&sp, n, -2.001f, 0x22);
    }
    func_8C605480(1);
}

void func_8C08AB02(Sint8 *min, Sint8 *sec) {
    *sec = B_8C752BE2;
    *min = B_8C752BE1;
}

void func_8C08AB10(Sint8 min, Sint8 sec) {
    B_8C752BE2 = sec;
    B_8C752BE1 = min;
    B_8C752BE3 = 0;
}

void func_8C08AB20(Sint8 min, Sint8 sec, Sint8 fr) {
    B_8C752BE2 = sec;
    B_8C752BE1 = min;
    B_8C752BE3 = fr;
}

void func_8C08AB2E(Sint8 *min, Sint8 *sec, Sint8 *fr) {
    *sec = B_8C752BE2;
    *min = B_8C752BE1;
    *fr = B_8C752BE3;
}

void func_8C08AB42(Sint32 s) {
    for (s += B_8C752BE2; s >= 60; s -= 60) {
        B_8C752BE1++;
    }
    B_8C752BE2 = s;
}

#ifdef NON_MATCHING
Sint32 func_8C08AB60(void) {
    if (B_8C752BE3 == 0 && B_8C752BE2 == 0 && B_8C752BE1 == 0) {
        return 1;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C08AB60(void))
#endif
