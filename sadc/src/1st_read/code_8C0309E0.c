#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * CCL 8C0309E0: cylinder (cwp1's current shape) against cylinder (cwp2's
 * current shape, "CYLINDER2"). Debug strings "HIT CYLINDER2 SIDE" /
 * "HIT CYLINDER2 FLAT". Called from the pair loop 8C033A10.
 *
 * Shape fields used: info1->a = radius, info1->b = half height (summed with
 * info2->b), info1->c = flat-hit radius, info2->a = radius, info2->b = half
 * height of the axis segment.
 *
 * Matching notes (SHC 5.1r03):
 *  - b1 + b2 is written out at every use: SHC keeps it as one common
 *    subexpression in FR15 (a named variable would be coalesced with b1).
 *  - SHC orders the operands of commutative float chains by declaration
 *    order (r1 + r2, r1 - dist + r2, b1 + b2, r2 + r1c) and breaks
 *    register-priority ties the same way. b2 must still win FR14 over b1, so
 *    it needs one more reference than b1: the repeated read of info2->b
 *    (dropped by the optimizer, no code).
 *  - The two ifs around the hit test must stay nested (not &&), otherwise
 *    the dx/dy/dz temporaries get other registers.
 */

extern char D_8C1125D0[];               /* "HIT CYLINDER2 SIDE" */
extern NJS_VECTOR D_8C1125E4;           /* {0.0f, 1.0f, 0.0f} up vector */
extern char D_8C1125F0[];               /* "HIT CYLINDER2 FLAT" */

void func_8C02F27C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);       /* debug draw of a shape */
void func_8C02F8A0(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2); /* record the hit */
void func_8C02FA20(colliwk *cwp1, colliwk *cwp2);
Sint8 func_8C02FB3A(colliwk *cwp1, CCL_INFO *info1, colliwk *cwp2, CCL_INFO *info2); /* may cwp1 be pushed by cwp2 */

ASM_STUB(void func_8C02FC12(taskwk *twp, CCL_INFO *info, NJS_POINT3 *out))
ASM_STUB(void func_8C02FD32(taskwk *twp, CCL_INFO *info, NJS_POINT3 *out))
ASM_STUB(void func_8C030850(taskwk *twp, CCL_INFO *info, NJS_VECTOR *out))

Float func_8C0309E0(colliwk *cwp1, colliwk *cwp2)
{
    NJS_POINT3 c1;
    NJS_POINT3 c2;
    NJS_POINT3 seg[2];
    NJS_VECTOR axis;
    NJS_LINE line;
    NJS_POINT3 cp;
    NJS_VECTOR d0;
    NJS_VECTOR d1;
    NJS_VECTOR vec;
    NJS_VECTOR up;
    Uint16 n1;
    Uint16 n2;
    CCL_INFO *info1;
    CCL_INFO *info2;
    taskwk *twp1;
    taskwk *twp2;
    Float dist;
    Float r1;
    Float r2;
    Float b1;
    Float b2;
    Float r1c;
    Float dx;
    Float dy;
    Float dz;
    Float h2;
    Float push;

    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    info1 = &cwp1->info[n1];
    info2 = &cwp2->info[n2];
    r1 = info1->a;
    r2 = info2->a;
    b1 = info1->b;
    b2 = info2->b;
    b2 = info2->b;
    r1c = info1->c;
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    func_8C02FC12(twp1, info1, &c1);
    func_8C030850(twp2, info2, &axis);
    func_8C02FD32(twp2, info2, &c2);

    /* axis segment of shape 2, lengthened by shape 1's half height */
    seg[0].x = SH_ONLY(c2.x + (b1 + b2) * axis.x) PC_ONLY(SA1_FMAC(b1 + b2, axis.x, c2.x));
    seg[0].y = SA1_FMAC(b1 + b2, axis.y, c2.y);
    seg[0].z = SA1_FMAC(b1 + b2, axis.z, c2.z);
    seg[1].x = c2.x - (b1 + b2) * axis.x;
    seg[1].y = c2.y - (b1 + b2) * axis.y;
    seg[1].z = c2.z - (b1 + b2) * axis.z;
    line.px = seg[1].x;
    line.py = seg[1].y;
    line.pz = seg[1].z;
    line.vx = axis.x;
    line.vy = axis.y;
    line.vz = axis.z;
    dist = func_8C63FF84(&c1, &line, &cp);
    d0.x = seg[0].x - cp.x;
    d0.y = seg[0].y - cp.y;
    d0.z = seg[0].z - cp.z;
    d1.x = seg[1].x - cp.x;
    d1.y = seg[1].y - cp.y;
    d1.z = seg[1].z - cp.z;

    if (r1 + r2 > dist) {
        /* closest point strictly between the segment ends */
        if (njInnerProduct(&d0, &d1) < 0.0f) {
            dx = c2.x - cp.x;
            dy = c2.y - cp.y;
            dz = c2.z - cp.z;
            h2 = SH_ONLY(dx * dx + dy * dy + dz * dz) PC_ONLY(SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy)));
            if (tcrf_CollisionDisplayVar >= 0) {
                func_8C043A40(c1.x, c1.y, c1.z, cp.x, cp.y, cp.z);
            }
            func_8C02F8A0(twp1, cwp1, twp2, cwp2);
            func_8C02FA20(cwp1, cwp2);
            cwp2->flag |= 8;
            if (((b1 + b2) - 3.0f) * ((b1 + b2) - 3.0f) > h2) {
                /* side hit: push the shapes apart horizontally */
                vec.x = c1.x - cp.x;
                vec.y = c1.y - cp.y;
                vec.z = c1.z - cp.z;
                push = r1 - dist + r2 - 0.099999996f;
                if (njScalor(&vec) == 0.0f) {
                    vec.x = vec.y = 0.0f;
                    vec.z = 0.099999996f;
                } else {
                    njUnitVector(&vec);
                }
                vec.x *= push;
                vec.y *= push;
                vec.z *= push;
                if (func_8C02FB3A(cwp1, info1, cwp2, info2)) {
                    twp1->pos.x += vec.x;
                    twp1->pos.y += vec.y;
                    twp1->pos.z += vec.z;
                }
                if (tcrf_CollisionDisplayVar >= 0) {
                    njPrintC(0x000A0011, D_8C1125D0);
                }
                if (func_8C02FB3A(cwp2, info2, cwp1, info1)) {
                    twp2->pos.x -= vec.x;
                    twp2->pos.y -= vec.y;
                    twp2->pos.z -= vec.z;
                }
            } else if (r2 + r1c > dist) {
                /* flat hit: push shape 1 out along the axis direction */
                up = D_8C1125E4;
                vec = cp;
                njSubVector(&vec, &c2);
                if (njScalor(&vec) == 0.0f) {
                    vec.x = vec.y = 0.0f;
                    vec.z = 0.099999996f;
                } else {
                    njUnitVector(&vec);
                }
                if (njInnerProduct(&up, &vec) > 0.65f) {
                    twp1->flag |= 0x20;
                    twp2->flag |= 0x10;
                    cwp1->flag |= 0x10;
                    cwp2->flag |= 4;
                }
                cwp1->flag |= 0x40;
                cwp2->flag |= 0x40;
                vec.x *= (b1 + b2) - njSqrt(h2);
                vec.y *= (b1 + b2) - njSqrt(h2);
                vec.z *= (b1 + b2) - njSqrt(h2);
                if (tcrf_CollisionDisplayVar >= 0) {
                    njPrintC(0x000A0011, D_8C1125F0);
                }
                if (func_8C02FB3A(cwp1, info1, cwp2, info2)) {
                    njAddVector(&twp1->pos, &vec);
                    twp1->flag &= ~8;
                }
            }
        }
    }
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp1, info1, &c1);
    }
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp2, info2, &c2);
    }
    return dist;
}
