#include "common.h"
#include "ninja.h"

/*
 * Global flag word (bits set/cleared by func_8C056986/func_8C056990) and its
 * saved copy (saved by func_8C056972, restored by func_8C05697C).
 * B_8C788C40 is a single Uint32; it is declared as a 1-element array because
 * only the B_8C788C40[0] spelling reproduces SHC's register allocation here.
 */
extern Uint32 B_8C88F62C;
extern Uint32 B_8C788C40[1];

/*
 * func_8C056A04: sphere (pos, radius r) vs. collision list sweep along v,
 * optionally aligning rotation ang; entries whose attribute matches mask are skipped.
 * Returns hit flags (1 = side hit, 2 = floor-like hit).
 */
ASM_STUB(Sint32 func_8C056A04(NJS_POINT3 *pos, NJS_POINT3 *v, Angle3 *ang, Uint32 mask, Float r))

void func_8C05697C(void) {
    B_8C88F62C = B_8C788C40[0];
}

void func_8C056986(Uint32 bits) {
    B_8C88F62C |= bits;
}

void func_8C056990(Uint32 bits) {
    B_8C88F62C &= ~bits;
}

Bool func_8C0569E0(NJS_POINT3 *pos, NJS_POINT3 *v, Angle3 *ang, Float r) {
    return func_8C056A04(pos, v, ang, 0, r) != 0;
}

Bool func_8C0569F0(NJS_POINT3 *pos, NJS_POINT3 *v, Angle3 *ang, Uint32 mask, Float r) {
    return func_8C056A04(pos, v, ang, mask, r) != 0;
}

Sint32 func_8C056A00(NJS_POINT3 *pos, NJS_POINT3 *v, Angle3 *ang, Float r) {
    return func_8C056A04(pos, v, ang, 0, r);
}
