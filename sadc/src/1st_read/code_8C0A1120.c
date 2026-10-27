#include "common.h"

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

extern void njColorBlendingMode(Sint32 a, Sint32 b);
extern void func_8C605480(Sint32 a);
extern void njSetConstantMaterial(void *p);
extern void njDrawSprite2D(UNK_SPRITE *sp, Sint32 n, Float pri, Uint32 attr);
extern Uint16 GetStageNumber(void);

extern Uint8 D_8C15B1A4[];
extern Uint8 D_8C15B1B4[];
extern Float D_8C15B19C[];
extern UNK_SPRITE D_8C15B1D8;
extern UNK_SPRITE D_8C15B20C;
extern Sint16 D_8C1C454C;
extern UNK_SPRITE D_8C1C4550;
extern Sint32 tcrf_LevelSelectVar;

Float func_8C0A123C(Float x, Float y, Float pri, Sint32 n);

void func_8C0A1120(Sint32 num) {
    Float x;
    Float y;
    Sint32 d;
    Sint32 i;
    Sint32 n;

    njColorBlendingMode(0, 1);
    func_8C605480(0);
    njSetConstantMaterial(D_8C15B1A4);
    x = 92.0f;
    y = 33.0f;
    n = num;
    if (n > 99999) {
        n = 99999;
    }
    d = 10000;
    i = 0;
    do {
        x += func_8C0A123C(x, y, D_8C15B19C[i], n / d);
        n %= d;
        d /= 10;
        i ^= 1;
    } while (d > 0);
    njSetConstantMaterial(D_8C15B1B4);
    func_8C605480(1);
}

void func_8C0A11BE(void) {
    njDrawSprite2D(&D_8C15B1D8, 0, -2.001f, 0x20);
    njDrawSprite2D(&D_8C15B20C, 0, -2.0f, 0x20);
}

Sint32 func_8C0A11DE(void) {
    Sint32 r;
    Sint32 stg;
    Uint8 lvl;

    r = 0;
    stg = GetStageNumber();
    lvl = (stg & 0xFF00) >> 8;
    if (lvl == 38 || (lvl == 8 && (Uint8)stg == 2 && tcrf_LevelSelectVar == 9)) {
        r = 1;
    }
    return r;
}

Sint32 func_8C0A1218(void) {
    Sint32 r;
    Sint32 stg;
    Uint8 lvl;

    r = 0;
    stg = GetStageNumber();
    lvl = (stg & 0xFF00) >> 8;
    if (lvl == 35) {
        r = 1;
    }
    return r;
}

Float func_8C0A123C(Float x, Float y, Float pri, Sint32 n) {
    D_8C1C454C = n;
    D_8C1C4550.p.x = x;
    D_8C1C4550.p.y = y;
    njDrawSprite2D(&D_8C1C4550, 0, pri, 0x20);
    return 16.0f;
}
