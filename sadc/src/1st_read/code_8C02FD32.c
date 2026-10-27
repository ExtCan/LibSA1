#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * CCL (object-vs-object collision) 8C02FD32-8C030164: world position of a
 * collision shape and the sphere-vs-sphere test/resolution of a shape pair.
 */

ASM_STUB(void func_8C02F27C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos))
ASM_STUB(void func_8C02F8A0(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2))
ASM_STUB(void func_8C02FA20(colliwk *cwp1, colliwk *cwp2))
ASM_STUB(Sint8 func_8C02FB3A(colliwk *cwp1, CCL_INFO *info1, colliwk *cwp2, CCL_INFO *info2))
ASM_STUB(void func_8C02FC12(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos))

/*
 * World position of the shape centre: attr 0x20 = absolute, 0x2000 = offset
 * from twp->pos without rotation, else rotated by twp->ang (0x40 selects the
 * Y-Z-X order instead of Z-X-Y). Same code as func_8C02FC12.
 *
 * NON_MATCHING only because of the translation-unit context: the C below is
 * complete and produces the original instructions except for one branch.
 * In the original object the pending literal pool of the preceding function
 * func_8C02FC12 (code_8C02F81A.c; 8 entries D_8C67C640 .. njPopMatrix, the
 * first 6 reused here) is dumped inside this function (8C02FDBA, after the
 * BRA that ends the attr 0x40 case), which
 * makes SHC treat the "attr & 0x2000" branch to the add case as out of range
 * and emit "BT +6; BRA 8C02FE2C; NOP" instead of "BF 8C02FE2C". Here the only
 * pending pool entry before this function is the stubs' __asm_stub_sink, the
 * pool at that point has 7 entries instead of 8 and SHC emits BF. Verified (2026-10-12) with
 * test copies of this file that add exactly one pending pool entry in front
 * of the function (a non-game function with one extern call or one store to
 * an extern global, placed before or after the stubs): shcdiff then reports
 * MATCH for all four functions. A bigger pending pool (e.g. 2 more entries)
 * is dumped earlier, at the BRA of the attr 0x20 case, and does not match.
 * The ternary form of the rotations is needed for the R2/R3 allocation, the
 * unsigned ang for the stack slot (MOV.L Rn,@R15 / MOV.L @R15,R5), and the
 * pointer walk for the @R4+ / @-R4 accesses of the 0x2000 case.
 */
#ifdef NON_MATCHING
void func_8C02FD32(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos)
{
    Uint32 ang;
    Float *p;
    Float x, y, z;
    Float cx, cy, cz;

    if (info->attr & 0x20) {
        pos->x = info->center.x;
        pos->y = info->center.y;
        pos->z = info->center.z;
    } else if (!(info->attr & 0x2000)) {
        njPushMatrix(D_8C67C640);
        njTranslateV(NULL, &twp->pos);
        if (info->attr & 0x40) {
            (ang = twp->ang.y) ? njRotateY(NULL, ang) : (void)0;
            (ang = twp->ang.z) ? njRotateZ(NULL, ang) : (void)0;
            (ang = twp->ang.x) ? njRotateX(NULL, ang) : (void)0;
        } else {
            (ang = twp->ang.z) ? njRotateZ(NULL, ang) : (void)0;
            (ang = twp->ang.x) ? njRotateX(NULL, ang) : (void)0;
            (ang = twp->ang.y) ? njRotateY(NULL, ang) : (void)0;
        }
        njCalcPoint(NULL, &info->center, pos);
        njPopMatrix(1);
    } else {
        p = &twp->pos.x;
        x = *p++;
        y = *p++;
        z = *p;
        p = &info->center.x;
        cx = *p++;
        cy = *p++;
        cz = *p;
        x += cx;
        y += cy;
        z += cz;
        p = &pos->x + 3;
        *--p = z;
        *--p = y;
        *--p = x;
    }
}
#else
ASM_STUB(void func_8C02FD32(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos))
#endif

/* Same as func_8C02FD32, but attr 0x2000 only skips the rotation. */
void func_8C02FE5C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos)
{
    Uint32 ang;

    if (info->attr & 0x20) {
        pos->x = info->center.x;
        pos->y = info->center.y;
        pos->z = info->center.z;
    } else {
        njPushMatrix(D_8C67C640);
        njTranslateV(NULL, &twp->pos);
        if (!(info->attr & 0x2000)) {
            if (info->attr & 0x40) {
                (ang = twp->ang.y) ? njRotateY(NULL, ang) : (void)0;
                (ang = twp->ang.z) ? njRotateZ(NULL, ang) : (void)0;
                (ang = twp->ang.x) ? njRotateX(NULL, ang) : (void)0;
            } else {
                (ang = twp->ang.z) ? njRotateZ(NULL, ang) : (void)0;
                (ang = twp->ang.x) ? njRotateX(NULL, ang) : (void)0;
                (ang = twp->ang.y) ? njRotateY(NULL, ang) : (void)0;
            }
        }
        njCalcPoint(NULL, &info->center, pos);
        njPopMatrix(1);
    }
}

/* Shape centre = twp->pos raised by info->b. */
void func_8C02FF60(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos)
{
    pos->x = twp->pos.x;
    pos->y = twp->pos.y + info->b;
    pos->z = twp->pos.z;
}

/*
 * Sphere test of the current shapes (test_num) of two colliwks. On overlap:
 * records the hit, flags both (0x02) and pushes the owners apart along the
 * centre line by the overlap depth (y ignored when both ids are 0).
 * Returns the squared centre distance.
 *
 * Matching notes: the comparison must use fresh loads of info1->a and
 * info2->a (no shared temporary) and r1 must be read between the two info
 * pointers and the two task pointers; with other orders or a variable for
 * the radius sum SHC swaps the twp2 / r1 loads and computes depth in place
 * instead of FADD FR14,FR13 / FMOV FR13,FR14. The Uint16 n1/n2 locals give
 * the MOV R0,R4 / MOV R0,R5 copies before the MULU.W.
 * d2 has 2 FMACs (FMUL dy*dy, FMAC dx*dx, FMAC dz*dz): SHC contracts the
 * plain sum that way, while the equivalent SA1_FMAC nesting compiles
 * differently, so SHC gets the plain sum and the PC build the SA1_FMACs.
 */
Float func_8C02FF7E(colliwk *cwp1, colliwk *cwp2)
{
    NJS_POINT3 p1;
    NJS_POINT3 p2;
    NJS_VECTOR v;
    CCL_INFO *info1;
    Uint16 n1;
    Uint16 n2;
    CCL_INFO *info2;
    taskwk *twp1;
    taskwk *twp2;
    Float r1;
    Float r2;
    Float d2;
    Float depth;

    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    info1 = &cwp1->info[n1];
    info2 = &cwp2->info[n2];
    r1 = info1->a;
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    r2 = info2->a;
    func_8C02FC12(twp1, info1, &p1);
    func_8C02FC12(twp2, info2, &p2);
    d2 = SH_ONLY((p1.x - p2.x) * (p1.x - p2.x) + (p1.y - p2.y) * (p1.y - p2.y)
                 + (p1.z - p2.z) * (p1.z - p2.z))
         PC_ONLY(SA1_FMAC(p1.z - p2.z, p1.z - p2.z,
                          SA1_FMAC(p1.x - p2.x, p1.x - p2.x, (p1.y - p2.y) * (p1.y - p2.y))));
    if ((info1->a + info2->a) * (info1->a + info2->a) > d2) {
        func_8C02F8A0(twp1, cwp1, twp2, cwp2);
        cwp1->flag |= 2;
        cwp2->flag |= 2;
        func_8C02FA20(cwp1, cwp2);
        v.x = p1.x - p2.x;
        if (cwp1->id == 0 && cwp2->id == 0) {
            v.y = 0.0f;
        } else {
            v.y = p1.y - p2.y;
        }
        v.z = p1.z - p2.z;
        depth = (r1 + r2) - njSqrt(d2);
        if (njScalor(&v) == 0.0f) {
            v.x = v.y = 0.0f;
            v.z = 0.099999996f;
        } else {
            njUnitVector(&v);
        }
        v.x *= depth;
        v.y *= depth;
        v.z *= depth;
        if (func_8C02FB3A(cwp1, info1, cwp2, info2)) {
            twp1->pos.x += v.x;
            twp1->pos.y += v.y;
            twp1->pos.z += v.z;
        }
        if (func_8C02FB3A(cwp2, info2, cwp1, info1)) {
            twp2->pos.x -= v.x;
            twp2->pos.y -= v.y;
            twp2->pos.z -= v.z;
        }
    }
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp1, info1, &p1);
    }
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp2, info2, &p2);
    }
    return d2;
}
