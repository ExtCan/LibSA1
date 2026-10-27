#include "common.h"

extern Sint8 B_8C752BDD;
extern Sint8 B_8C752BDE;
extern Sint8 B_8C752BDF;
extern Sint8 B_8C752BE0;    /* life count */
extern Sint8 B_8C752BE1;    /* timer minutes */
extern Sint8 B_8C752BE2;    /* timer seconds */
extern Sint8 B_8C752BE3;    /* timer frames */
extern Sint32 B_8C752BE8;
extern Sint32 B_8C752BEC;
extern Sint32 D_8C1BF198;
extern Sint32 B_8C754EC4;
extern Sint8 B_8C7997B0;
extern Sint32 B_8C7997A0;

extern Sint32 func_8C0A1218(void);
extern Sint32 GetPlayerNumber(void);
extern Sint32 func_8C04C960(void);
extern void func_8C0940DA(Sint32 se, Sint32 a, Sint32 b, Sint32 c);

ASM_STUB(void func_8C08A2C2(void))

/* The common HUD wrapper (rings, lives; called by the frame steps 8C04E95A /
 * 8C04EA4A / 8C04EB7E after the logic steps): not while an event runs
 * (B_8C752BDD < 0), not in Twinkle Circuit (8C0A1218), not for Big (player
 * 7: his HUD task 8C0EB160 draws his own). It then copies BDD into BDE and
 * BDF (-1 in adventure fields, 8C04C960() == 1). */
void func_8C08A4BA(void) {
    if (B_8C752BDD < 0) {
        return;
    }
    if (func_8C0A1218() != 0) {
        return;
    }
    if (GetPlayerNumber() == 7) {
        return;
    }
    func_8C08A2C2();
    if (func_8C04C960() == 1) {
        B_8C752BDF = -1;
        B_8C752BDE = -1;
    } else {
        B_8C752BDF = B_8C752BDE = B_8C752BDD;
    }
}

/* lives; Sint16: every caller extends the result with EXTS.W R0 (8C04C9DA,
 * 8C04CD66, 8C04CF62, 8C04D19A, 8C0E755A) */
Sint16 func_8C08A572(void) {
    return B_8C752BE0;
}

void func_8C08A580(void);

#ifdef NON_MATCHING
void func_8C08A578(void) {
    B_8C752BE8 = 0;
    func_8C08A580();
}
#else
ASM_STUB(void func_8C08A578(void))
#endif

void func_8C08A580(void) {
    B_8C752BE3 = 0;
    B_8C752BE2 = 0;
    B_8C752BE1 = 0;
    if (GetPlayerNumber() == 6) {
        B_8C752BE1 = 3;
        B_8C752BE2 = 0;
    }
    B_8C752BEC = 0;
}

void func_8C08A5AE(void) {
    D_8C1BF198 = 1;
}

void func_8C08A5B6(void) {
    D_8C1BF198 = 0;
}

void func_8C08A5BE(void) {
    B_8C752BE8 += B_8C754EC4;
}

#ifdef NON_MATCHING
void func_8C08A5CC(void) {
    Sint32 t;

    if (D_8C1BF198 != 0) {
        if (GetPlayerNumber() == 6) {
            if (--B_8C752BE3 < 0) {
                B_8C752BE3 = 59;
                if (--B_8C752BE2 < 0) {
                    B_8C752BE2 = 59;
                    if (--B_8C752BE1 < 0) {
                        B_8C752BE1 = 0;
                        B_8C752BE2 = 0;
                        B_8C752BE3 = 0;
                    }
                }
            }
            t = B_8C752BE1 * 60 + B_8C752BE2;
            if (t < 10 && t > 0) {
                B_8C7997B0--;
                if (B_8C7997B0 <= 0) {
                    func_8C0940DA(0x30B, 0, 0, 0);
                    if (t > 5) {
                        B_8C7997B0 = 120;
                    } else {
                        B_8C7997B0 = 60;
                    }
                }
            }
        } else {
            if (++B_8C752BE3 >= 60) {
                B_8C752BE3 = 0;
                if (++B_8C752BE2 >= 60) {
                    B_8C752BE2 = 0;
                    if (++B_8C752BE1 >= 100) {
                        B_8C752BE1 = 99;
                        B_8C752BE2 = 59;
                        B_8C752BE3 = 59;
                    }
                }
            }
        }
        if (B_8C7997A0-- < 0) {
            B_8C7997A0 = 0;
        }
    }
    B_8C752BEC++;
}
#else
ASM_STUB(void func_8C08A5CC(void))
#endif
