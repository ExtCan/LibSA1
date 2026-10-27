#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Vector -> angle helper and two pairs of global flag words
 * (chunk 8C055D20, functions 8C056860-8C05697C).
 */

/* 32-bit flag words: OR-set by func_8C05694A, cleared by func_8C05695C,
 * saved to/restored from B_8C788C38/B_8C788C3C by func_8C056926/func_8C056938 */
extern Uint32 B_8C88F660;
extern Uint32 B_8C88F664;
extern Uint32 B_8C788C38;
extern Uint32 B_8C788C3C;
extern Sint32 B_8C88F62C;
extern Sint32 B_8C788C40;

/*
 * Direction vector -> angles. ang is an Angle3 viewed as Angle[3] (the
 * indexed form is what makes SHC keep &ang[1]/&ang[2] in stack temps):
 *   ang[1] = atan2f(v->x, -v->z)      (yaw)
 *   ang[2] = atan2f(-v->y, |v.xz|)    (pitch, stored in the z slot)
 * With base != NULL the angles of base are subtracted. ang[0] is not written.
 */
void func_8C056860(NJS_VECTOR *v, NJS_VECTOR *base, Angle *ang) {
    Float d;

    d = njSqrt(SA1_FMAC(v->x, v->x, v->z * v->z));
    ang[1] = SA1_FTOI(atan2f(v->x, -v->z) * 10430.381f);
    ang[2] = SA1_FTOI(atan2f(-v->y, d) * 10430.381f);
    if (base != NULL) {
        d = njSqrt(SA1_FMAC(base->x, base->x, base->z * base->z));
        ang[1] -= SA1_FTOI(atan2f(base->x, -base->z) * 10430.381f);
        ang[2] -= SA1_FTOI(atan2f(-base->y, d) * 10430.381f);
    }
}

void func_8C056926(void) {
    B_8C788C38 = B_8C88F660;
    B_8C788C3C = B_8C88F664;
}

void func_8C056938(void) {
    B_8C88F660 = B_8C788C38;
    B_8C88F664 = B_8C788C3C;
}

void func_8C05694A(Uint32 a, Uint32 b) {
    B_8C88F660 |= a;
    B_8C88F664 |= b;
}

void func_8C05695C(Uint32 a, Uint32 b) {
    B_8C88F660 &= ~a;
    B_8C88F664 &= ~b;
}

void func_8C056972(void) {
    B_8C788C40 = B_8C88F62C;
}
