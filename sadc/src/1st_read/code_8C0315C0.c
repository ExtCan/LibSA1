/*
 * CCL pair tests 8C0315C0 (cylinder cwp1, form 1, vs capsule cwp2, form 3)
 * and 8C031726 (capsule vs capsule, form 3 x 3) (object-vs-object collision,
 * include/sa1/ccl.h). Both are reached through the shape-pair dispatch of
 * 8C0335D4/8C033800/8C033A10 (e.g. 8C03372C, 8C033762, 8C03376E) with the
 * two colliwk being tested (colliwk.test_num selects the shape; the form is
 * CCL_INFO.attr & 0xF). They build the axis segment of each
 * shape (center +/- b * axis), draw the shapes when the collision display is
 * on, and return 100000.0f: the actual segment test is missing (the two
 * functions compile to identical code; the segments are computed but never
 * used, the test was presumably compiled out of the release build).
 *
 * Matching notes: the shape indices go through Uint16 locals (gives the
 * MOV R0,Rn copies before MULU.W), and twp2/len2 are assigned before
 * twp1/len1 (gives ccl1 = R13, ccl2 = R14 and the mytask load order).
 *
 * libsa1: the segments are never read, so clang drops them together with
 * their FMACs (SHC keeps the stores). SEG_QUAL makes them volatile on PC only,
 * which keeps the computation as in the original (and the 6 fused
 * multiply-adds per function the FMAC audit expects); SHC sees no change.
 */
#include "common.h"
#include "task.h"
#include "sa1/sa1.h"

#if defined(TARGET_PC)
#define SEG_QUAL volatile
#else
#define SEG_QUAL
#endif

/* 8C030850: unit axis of the shape ((0,1,0) through the shape and task
 * rotations); same chunk (BSR) */
ASM_STUB(void func_8C030850(taskwk *twp, CCL_INFO *ccl, NJS_VECTOR *axis))

/* 8C02FD32: shape centre in world space */
extern void func_8C02FD32(taskwk *twp, CCL_INFO *ccl, NJS_POINT3 *pos);
/* 8C02F27C: debug display of a shape */
extern void func_8C02F27C(taskwk *twp, CCL_INFO *ccl, NJS_POINT3 *pos);

Float func_8C0315C0(colliwk *cwp1, colliwk *cwp2)
{
    taskwk *twp1;
    taskwk *twp2;
    CCL_INFO *ccl1;
    CCL_INFO *ccl2;
    Float len1;
    Float len2;
    NJS_POINT3 pos1;
    NJS_POINT3 pos2;
    SEG_QUAL NJS_POINT3 seg1[2];
    SEG_QUAL NJS_POINT3 seg2[2];
    NJS_VECTOR axis1;
    NJS_VECTOR axis2;
    SEG_QUAL NJS_POINT3 *p;
    Uint16 n1;
    Uint16 n2;

    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    ccl1 = &cwp1->info[n1];
    ccl2 = &cwp2->info[n2];
    twp2 = cwp2->mytask->twp;
    twp1 = cwp1->mytask->twp;
    len2 = ccl2->b;
    len1 = ccl1->b;

    func_8C030850(twp1, ccl1, &axis1);
    func_8C02FD32(twp1, ccl1, &pos1);
    p = &seg1[0];
    p->x = SA1_FMAC(len1, axis1.x, pos1.x);
    p->y = SA1_FMAC(len1, axis1.y, pos1.y);
    p->z = SA1_FMAC(len1, axis1.z, pos1.z);
    p = &seg1[1];
    p->x = pos1.x - len1 * axis1.x;
    p->y = pos1.y - len1 * axis1.y;
    p->z = pos1.z - len1 * axis1.z;

    func_8C030850(twp2, ccl2, &axis2);
    func_8C02FD32(twp2, ccl2, &pos2);
    p = &seg2[0];
    p->x = SA1_FMAC(len2, axis2.x, pos2.x);
    p->y = SA1_FMAC(len2, axis2.y, pos2.y);
    p->z = SA1_FMAC(len2, axis2.z, pos2.z);
    p = &seg2[1];
    p->x = pos2.x - len2 * axis2.x;
    p->y = pos2.y - len2 * axis2.y;
    p->z = pos2.z - len2 * axis2.z;

    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp1, ccl1, &pos1);
        if (tcrf_CollisionDisplayVar >= 0) {
            func_8C02F27C(twp2, ccl2, &pos2);
        }
    }
    return 100000.0f;
}

/* 8C031726: capsule vs capsule; identical code to 8C0315C0 (same
 * unfinished segment test). */
Float func_8C031726(colliwk *cwp1, colliwk *cwp2)
{
    taskwk *twp1;
    taskwk *twp2;
    CCL_INFO *ccl1;
    CCL_INFO *ccl2;
    Float len1;
    Float len2;
    NJS_POINT3 pos1;
    NJS_POINT3 pos2;
    SEG_QUAL NJS_POINT3 seg1[2];
    SEG_QUAL NJS_POINT3 seg2[2];
    NJS_VECTOR axis1;
    NJS_VECTOR axis2;
    SEG_QUAL NJS_POINT3 *p;
    Uint16 n1;
    Uint16 n2;

    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    ccl1 = &cwp1->info[n1];
    ccl2 = &cwp2->info[n2];
    twp2 = cwp2->mytask->twp;
    twp1 = cwp1->mytask->twp;
    len2 = ccl2->b;
    len1 = ccl1->b;

    func_8C030850(twp1, ccl1, &axis1);
    func_8C02FD32(twp1, ccl1, &pos1);
    p = &seg1[0];
    p->x = SA1_FMAC(len1, axis1.x, pos1.x);
    p->y = SA1_FMAC(len1, axis1.y, pos1.y);
    p->z = SA1_FMAC(len1, axis1.z, pos1.z);
    p = &seg1[1];
    p->x = pos1.x - len1 * axis1.x;
    p->y = pos1.y - len1 * axis1.y;
    p->z = pos1.z - len1 * axis1.z;

    func_8C030850(twp2, ccl2, &axis2);
    func_8C02FD32(twp2, ccl2, &pos2);
    p = &seg2[0];
    p->x = SA1_FMAC(len2, axis2.x, pos2.x);
    p->y = SA1_FMAC(len2, axis2.y, pos2.y);
    p->z = SA1_FMAC(len2, axis2.z, pos2.z);
    p = &seg2[1];
    p->x = pos2.x - len2 * axis2.x;
    p->y = pos2.y - len2 * axis2.y;
    p->z = pos2.z - len2 * axis2.z;

    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp1, ccl1, &pos1);
        if (tcrf_CollisionDisplayVar >= 0) {
            func_8C02F27C(twp2, ccl2, &pos2);
        }
    }
    return 100000.0f;
}
