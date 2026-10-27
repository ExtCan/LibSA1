#include "common.h"
#include "ninja.h"
#include "task.h"

/* Ninja ARGB float color (njSetConstantMaterial argument) */
typedef struct {
    Float a;
    Float r;
    Float g;
    Float b;
} NJS_ARGB;

/* NJS_SPRITE-like */
typedef struct {
    NJS_POINT3 p;
    Float sx;
    Float sy;
    Angle ang;
    void *tlist;
    void *tanim;
} UNK_SPRITE;

/* Number display request passed to func_8C08EE98 */
typedef struct {
    Uint8 flags;                /* 0x00 */
    Uint8 unk1;                 /* 0x01 */
    Uint16 unk2;                /* 0x02 */
    Uint32 max;                 /* 0x04 */
    Uint32 value;               /* 0x08 */
    Float x;                    /* 0x0C */
    Float y;                    /* 0x10 */
    Float unk14;                /* 0x14 */
    Float scale;                /* 0x18 */
} UNK_NUMDISP;

/* Time (minutes, seconds, frames) and screen position, passed by value */
typedef struct {
    Sint32 min;
    Sint32 sec;
    Uint32 frame;
    Float x;
    Float y;
} UNK_TIMEPOS;

extern Sint8 B_8C752BE1;        /* minutes */
extern Sint8 B_8C752BE2;        /* seconds */
extern Sint8 B_8C752BE3;        /* frames */
extern Sint16 B_8C752BE4;
extern Sint32 B_8C752BF0;
extern Sint32 B_8C752C00;
extern Sint32 B_8C754EC8;
extern Sint16 B_8C7A40DC;
extern Sint32 tcrf_LevelSelectVar;
extern UNK_NUMDISP D_8C1BF5F4;
extern NJS_TEXLIST D_8C1C3470;
extern Uint8 D_8C1BF614[];
extern Sint16 D_8C1BF650[];
extern Sint32 D_8C1BF660[4][13];

extern Sint32 GetPlayerNumber(void);
extern void njSetConstantMaterial(NJS_ARGB *argb);
extern void njColorBlendingMode(Sint32 a, Sint32 b);
extern void njDrawSprite2D(UNK_SPRITE *sp, Sint32 n, Float pri, Uint32 attr);
extern Sint32 func_8C08E158(Sint32 a, Sint32 b);
extern Sint32 func_8C08EC10(Sint32 a, Sint32 b);

Float func_8C08ABC0(void) {
    Float t;

    t = 5000.0f;
    if (GetPlayerNumber() == 6) {
        if (B_8C752BE1 < 3) {
            t = (B_8C752BE1 * 60 + B_8C752BE2) * 25.0f;
        }
    } else {
        Float d;

        d = (B_8C752BE1 * 60 + B_8C752BE2) * 10.0f;
        if (B_8C752BE1 != 0) {
            t -= d;
            if (t < 0.0f) {
                t = 0.0f;
            }
        }
    }
    return t;
}

#ifdef NON_MATCHING
void func_8C08AC2C(Float x, Float y) {
    Float w;
    Float w2;

    D_8C1BF5F4.max = 99;
    w = D_8C1BF5F4.scale * 16.0f;
    D_8C1BF5F4.value = B_8C752BE1;
    D_8C1BF5F4.x = x;
    D_8C1BF5F4.y = y;
    func_8C08EE98(&D_8C1BF5F4);
    w2 = w * 2.0f;
    D_8C1BF5F4.flags |= 0x20;
    D_8C1BF5F4.max = 9;
    x += w2;
    D_8C1BF5F4.x = x;
    func_8C08EE98(&D_8C1BF5F4);
    x += w;
    D_8C1BF5F4.flags |= 0x40;
    D_8C1BF5F4.flags &= ~0x20;
    D_8C1BF5F4.max = 59;
    D_8C1BF5F4.value = B_8C752BE2;
    D_8C1BF5F4.x = x;
    func_8C08EE98(&D_8C1BF5F4);
    x += w2;
    D_8C1BF5F4.flags |= 0x20;
    D_8C1BF5F4.max = 9;
    D_8C1BF5F4.x = x;
    func_8C08EE98(&D_8C1BF5F4);
    D_8C1BF5F4.flags &= ~0x20;
    D_8C1BF5F4.max = 99;
    D_8C1BF5F4.value = B_8C752BE3 * 1.6666666f;
    x += w;
    D_8C1BF5F4.x = x;
    func_8C08EE98(&D_8C1BF5F4);
    D_8C1BF5F4.flags &= ~0x40;
}
#else
ASM_STUB(void func_8C08AC2C(Float x, Float y))
#endif

#ifdef NON_MATCHING
void func_8C08AD04(UNK_TIMEPOS t) {
    Float x;
    Float w;
    Float w2;

    x = t.x;
    D_8C1BF5F4.max = 99;
    w = D_8C1BF5F4.scale * 16.0f;
    D_8C1BF5F4.value = t.min;
    D_8C1BF5F4.x = x;
    D_8C1BF5F4.y = t.y;
    func_8C08EE98(&D_8C1BF5F4);
    w2 = w * 2.0f;
    D_8C1BF5F4.flags |= 0x20;
    D_8C1BF5F4.max = 9;
    x += w2;
    D_8C1BF5F4.x = x;
    func_8C08EE98(&D_8C1BF5F4);
    x += w;
    D_8C1BF5F4.flags |= 0x40;
    D_8C1BF5F4.flags &= ~0x20;
    D_8C1BF5F4.max = 59;
    D_8C1BF5F4.value = t.sec;
    D_8C1BF5F4.x = x;
    func_8C08EE98(&D_8C1BF5F4);
    x += w2;
    D_8C1BF5F4.flags |= 0x20;
    D_8C1BF5F4.max = 9;
    D_8C1BF5F4.x = x;
    func_8C08EE98(&D_8C1BF5F4);
    x += w;
    D_8C1BF5F4.flags &= ~0x20;
    D_8C1BF5F4.max = 99;
    D_8C1BF5F4.value = t.frame;
    D_8C1BF5F4.x = x;
    func_8C08EE98(&D_8C1BF5F4);
    D_8C1BF5F4.flags &= ~0x40;
}
#else
ASM_STUB(void func_8C08AD04(UNK_TIMEPOS t))
#endif

#ifdef NON_MATCHING
void func_8C08AE00(Float x, Float y, Float w, Float h) {
    NJS_ARGB col;
    UNK_SPRITE sp;
    Float hh;
    Float y1;
    Float x0;
    Float y0;
    Float x1;
    Float ww;
    Float dx;
    Float dy;

    if (B_8C754EC8 != 0) {
        return;
    }
    dx = (w + 1.0f) * 16.0f;
    dy = (h + 1.0f) * 16.0f;
    x0 = x - dx;
    x1 = x + dx;
    y0 = y - dy;
    y1 = y + dy;
    hh = h * 4.0f;
    ww = w * 4.0f;
    col.a = -1.0f;
    col.r = col.g = col.b = 1.0f;
    njSetConstantMaterial(&col);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    sp.p.x = x;
    sp.p.y = y;
    sp.p.z = 0.0f;
    sp.sx = ww;
    sp.sy = hh;
    sp.ang = 0;
    sp.tlist = &D_8C1C3470;
    sp.tanim = D_8C1BF614;
    njDrawSprite2D(&sp, 0, -1.5f, 0x21);
    sp.p.x = x1;
    sp.p.y = y1;
    sp.sx = 1.0f;
    sp.sy = 1.0f;
    njDrawSprite2D(&sp, 1, -1.5f, 0x21);
    sp.p.x = x0;
    sp.ang = 0x4000;
    njDrawSprite2D(&sp, 1, -1.5f, 0x21);
    sp.p.y = y0;
    sp.ang = 0x8000;
    njDrawSprite2D(&sp, 1, -1.5f, 0x21);
    sp.p.x = x1;
    sp.ang = 0xC000;
    njDrawSprite2D(&sp, 1, -1.5f, 0x21);
    sp.p.x = x;
    sp.p.y = y1;
    sp.sx = w;
    sp.ang = 0;
    njDrawSprite2D(&sp, 2, -1.5f, 0x21);
    sp.p.x = x0;
    sp.p.y = y;
    sp.sx = h;
    sp.ang = 0x4000;
    njDrawSprite2D(&sp, 2, -1.5f, 0x21);
    sp.p.x = x;
    sp.p.y = y0;
    sp.sx = w;
    sp.ang = 0x8000;
    njDrawSprite2D(&sp, 2, -1.5f, 0x21);
    sp.p.x = x1;
    sp.p.y = y;
    sp.sx = h;
    sp.ang = 0xC000;
    njDrawSprite2D(&sp, 2, -1.5f, 0x21);
    col.a = col.r = col.g = col.b = 0.0f;
    njSetConstantMaterial(&col);
}
#else
ASM_STUB(void func_8C08AE00(Float x, Float y, Float w, Float h))
#endif

#ifdef NON_MATCHING
Sint32 func_8C08AFFA(Sint32 n, Sint32 bit) {
    Sint32 mask;
    Sint32 i;
    Sint32 count;
    Uint16 v;

    mask = 0xFFFF >> (15 - bit);
    count = 0;
    for (i = 0; i < n + 1; i++) {
        if (i == n) {
            v = mask & D_8C1BF650[i];
        } else {
            v = D_8C1BF650[i];
        }
        while (v != 0) {
            if (v & 1) {
                count++;
            }
            v >>= 1;
        }
    }
    return count - 1;
}
#else
ASM_STUB(Sint32 func_8C08AFFA(Sint32 n, Sint32 bit))
#endif

#ifdef NON_MATCHING
Sint32 func_8C08B0A0(Sint32 a, Sint32 b, Sint32 c) {
    Sint16 score;
    Sint32 time;

    score = B_8C7A40DC;
    switch (c) {
    case 0:
        time = B_8C752BE1 * 3600 + B_8C752BE2 * 60 + B_8C752BE3;
        switch (a) {
        case 0:
        default:
            if (time >= D_8C1BF660[0][b]) {
                return 0;
            }
            return 1;
        case 2:
            return 1;
        case 3:
            if (time > D_8C1BF660[1][b]) {
                return 0;
            }
            return 1;
        case 5:
            if (time > D_8C1BF660[2][b]) {
                return 0;
            }
            return 1;
        case 6:
            if (time > D_8C1BF660[3][b]) {
                return 1;
            }
            return 0;
        case 7:
            if (score >= 2000) {
                return 1;
            }
            return 0;
        }
    case 1:
        if (a == 7) {
            if (score >= 1000) {
                return 1;
            }
            return 0;
        }
        if (a == 3) {
            if (B_8C752C00 != 0) {
                return 0;
            }
            return 1;
        }
        if (B_8C752BE4 >= 50) {
            return 1;
        }
        return 0;
    case 2:
        return 1;
    default:
        return 0;
    }
}
#else
ASM_STUB(Sint32 func_8C08B0A0(Sint32 a, Sint32 b, Sint32 c))
#endif

#ifdef NON_MATCHING
Sint32 func_8C08B1BA(Sint32 a, Sint32 b, Sint32 c) {
    Sint32 lim;

    if (b == 8) {
        if (tcrf_LevelSelectVar == 9) {
            return func_8C08E158(a, c);
        }
    } else if (b == 38) {
        return func_8C08E158(a, c);
    } else if (b == 35) {
        return func_8C08EC10(a, c);
    }
    switch (c) {
    case 0:
        switch (b) {
        case 0:
            lim = 3000;
            break;
        case 36:
            lim = 8000;
            break;
        case 37:
            lim = 20000;
            break;
        default:
            lim = 9000;
            break;
        }
        break;
    case 1:
        lim = 0;
        break;
    default:
        lim = 999999;
        break;
    }
    if (lim > B_8C752BF0) {
        return 0;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C08B1BA(Sint32 a, Sint32 b, Sint32 c))
#endif
