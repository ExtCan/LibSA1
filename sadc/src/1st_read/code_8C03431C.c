#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

extern void func_8C031A84(NJS_POINT3 *a, NJS_POINT3 *b, Angle ang, NJS_POINT3 *out);
extern void func_8C031B20(NJS_POINT3 *a, NJS_POINT3 *b, Angle ang, NJS_VECTOR *v);
extern const char D_8C11269C[];         /* "PUSHOUT" */

/*
 * Push-out of shape cwp1->info[test_num] (at p1) from the box-like shape
 * cwp2->info[test_num] (at p2, half sizes a along x and c along z in the
 * shape's local frame): p1 is moved into the shape's frame, clamped onto the
 * nearest side (0.999 of the combined size), moved back, and the owner's
 * x/z position of cwp1 is updated. Callers: func_8C031D00, func_8C031F88.
 * NOTE: the third side uses r * 0.999 + a2 (one FMAC), unlike the other
 * three sides, which scale (r + size) by 0.999; kept as in the original.
 */
void func_8C03431C(colliwk *cwp1, NJS_POINT3 *p1, colliwk *cwp2, NJS_POINT3 *p2)
{
    NJS_VECTOR v;
    Angle ang;
    Uint16 n1;                  /* Uint16 locals: MULU.W #52 and twp2 spilled to the stack */
    Uint16 n2;
    Float r;
    Float a2;
    Float c2;
    Float zz;
    Float xx;
    Float d1;
    Float d2;
    Float dx;
    Float dz;
    CCL_INFO *ci1;
    CCL_INFO *ci2;
    taskwk *twp2;
    taskwk *twp1;

    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    ci1 = &cwp1->info[n1];
    ci2 = &cwp2->info[n2];
    r = ci1->a;
    a2 = ci2->a;
    c2 = ci2->c;

    if ((ci2->attr & 0x20) || (ci2->attr & 0x2000)) {
        ang = -ci2->angy;
    } else {
        ang = -twp2->ang.y - ci2->angy;
    }
    func_8C031A84(p1, p2, ang, &v);
    /* zz/xx must be locals: as inline subexpressions SHC commutes zz + xx */
    zz = (r + a2) * v.z;
    xx = (r + c2) * v.x;
    d1 = zz - xx;
    d2 = zz + xx;
    if (d1 > 0.0f) {
        if (d2 > 0.0f) {
            v.z = (r + c2) * 0.99899997f;
        } else {
            v.x = (-r - a2) * 0.99899997f;
        }
    } else {
        if (d2 > 0.0f) {
            v.x = SA1_FMAC(r, 0.99899997f, a2);
        } else {
            v.z = (-r - c2) * 0.99899997f;
        }
    }
    func_8C031B20(p1, p2, ang, &v);
    if (tcrf_CollisionDisplayVar >= 0) {
        njPrintC(0x000A000F, D_8C11269C);
    }
    dx = v.x - p1->x;
    dz = v.z - p1->z;
    twp1->pos.x = p1->x + dx;
    twp1->pos.z = p1->z + dz;
}
