#include "common.h"

/* 52-byte pool entry (256 entries at B_8C75D310, in-use flags at B_8C760710) */
typedef struct {
    Sint32 unk0;
    Sint32 unk4;
    Sint32 unk8[9];
    Sint32 unk2C;
    Sint32 unk30;
} UNK_8C75D310_t;

extern Uint8 B_8C760710[256];
extern UNK_8C75D310_t B_8C75D310[256];
extern Float B_8C754BDC[3];
extern Sint32 B_8C754BE8;
extern Sint32 B_8C754BEC;

extern Float njCos(Sint32 ang);
extern Float njSin(Sint32 ang);
extern Float func_8C10D058(Float x);
extern Float atan2f(Float y, Float x);

void func_8C052A00(void) {
    Sint16 i;

    for (i = 255; i >= 0; i--) {
        B_8C760710[i] = 0;
    }
}

UNK_8C75D310_t *func_8C052A1E(void) {
    Sint16 i;

    for (i = 255; i >= 0; i--) {
        if (B_8C760710[i] == 0) {
            break;
        }
    }
    if (i >= 0) {
        B_8C760710[i] = 1;
        B_8C75D310[i].unk4 = 0;
        B_8C75D310[i].unk2C = 0;
        B_8C75D310[i].unk30 = 0;
        return &B_8C75D310[i];
    }
    return NULL;
}

void func_8C052A5E(UNK_8C75D310_t *p) {
    Sint16 i;

    for (i = 255; i >= 0; i--) {
        if (p == &B_8C75D310[i]) {
            break;
        }
    }
    if (i >= 0) {
        B_8C760710[i] = 0;
    }
}

void func_8C052A8A(void) {
    B_8C754BDC[0] = 0.0f;
    B_8C754BDC[1] = -1.0f;
    B_8C754BDC[2] = 0.0f;
    B_8C754BE8 = 0;
    B_8C754BEC = 0;
}

/* Sets the vector at B_8C754BDC from two angles (rotation of (0,-1,0)) and
 * stores the angles recovered from it (asin/atan2 in radians -> Angle). */
void func_8C052AA8(Sint32 a, Sint32 b) {
    Float *v;
    Float x;
    Float y;
    Float z;

    v = B_8C754BDC;
    x = -(njCos(-a) * njSin(-b));
    y = -(njCos(-a) * njCos(-b));
    z = njSin(-a);
    v += 3;
    *--v = z;
    *--v = y;
    *--v = x;
    B_8C754BE8 = func_8C10D058(-z) * 10430.381f;
    B_8C754BEC = -(Sint32)(atan2f(-x, -y) * 10430.381f);
}
