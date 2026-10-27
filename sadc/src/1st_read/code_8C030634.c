/*
 * CCL pair test 8C030634 (cylinder vs cylinder, vertical axes) and the shape
 * axis helper 8C030850 (object-vs-object collision, include/sa1/ccl.h).
 *
 * func_8C030634 is reached through the shape-pair dispatch of
 * 8C0335D4/8C033800/8C033A10 with the two colliwk being tested
 * (colliwk.test_num selects the shape). It returns the squared horizontal
 * distance of the two shape centres; on an overlap it records the hit
 * (8C02F8A0, 8C02FA20) and pushes the tasks apart horizontally.
 */
#include "common.h"
#include "task.h"
#include "sa1/sa1.h"

/* same chunk, reached with BSR */
ASM_STUB(void func_8C02F8A0(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2))
ASM_STUB(void func_8C02FA20(colliwk *cwp1, colliwk *cwp2))
ASM_STUB(Sint8 func_8C02FB3A(colliwk *cwp1, CCL_INFO *ccl1, colliwk *cwp2, CCL_INFO *ccl2))
/* 8C02FD32: shape centre in world space */
ASM_STUB(void func_8C02FD32(taskwk *twp, CCL_INFO *ccl, NJS_POINT3 *pos))

/* 8C02F27C: debug display of a shape (JSR) */
extern void func_8C02F27C(taskwk *twp, CCL_INFO *ccl, NJS_POINT3 *pos);

/*
 * 8C030634: cylinder (cwp1: centre from 8C02FD32, radius a, half height b)
 * against cylinder (cwp2, same). Hit when the horizontal centre distance is
 * below a1 + a2 and the vertical extents overlap; the tasks are then pushed
 * apart horizontally by 0.4 * (a1 + a2 - dist - 0.001) each (only the ones
 * 8C02FB3A agrees to move). Returns the squared horizontal distance.
 *
 * NON_MATCHING: complete and functionally faithful (1 FMAC = 1 SA1_FMAC).
 * Same instructions, stack frame, callee-saved registers (a1 FR14, b1 FR13,
 * a2 FR12, b2 spilled, dist2 FR15) and integer registers as the original;
 * 17 differing lines, all caller-saved FP temporaries: dx/dz are in FR5/FR4
 * instead of FR4/FR5 (so the FMAC is scheduled after the p1.y load) and
 * top1/bot1 are in FR7/FR6 instead of FR6/FR7.
 * Findings: "dist2 < (a1 + a2) * (a1 + a2)" with the sum left to CSE (also
 * reused by d) is needed for the R9/R10 and FR13/FR14 assignment; an explicit
 * r = a1 + a2 local reshuffles all FP registers. dz must be a named local:
 * with (p1.z - p2.z) inline, dx/dz get FR4/FR5 as in the original but a2 is
 * spilled instead of b2 (SHC ranks the callee-saved candidates by use count).
 * Tried without effect: declaration orders and names, load orders, top/bot
 * statement orders and incremental forms, compare spellings (< / >), nested
 * ifs, operand orders, y1/y2 locals, register, split dist2 statements.
 */
#ifdef NON_MATCHING
Float func_8C030634(colliwk *cwp1, colliwk *cwp2)
{
    Uint16 n1;
    Uint16 n2;
    CCL_INFO *info1;
    CCL_INFO *info2;
    taskwk *twp1;
    taskwk *twp2;
    Float a1;
    Float a2;
    Float b1;
    Float b2;
    Float dx;
    Float dz;
    Float dist2;
    Float top1;
    Float bot1;
    Float top2;
    Float bot2;
    Float d;
    NJS_POINT3 p1;
    NJS_POINT3 p2;
    NJS_VECTOR v;

    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    info1 = &cwp1->info[n1];
    info2 = &cwp2->info[n2];
    a1 = info1->a;
    a2 = info2->a;
    b1 = info1->b;
    b2 = info2->b;
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    func_8C02FD32(twp1, info1, &p1);
    func_8C02FD32(twp2, info2, &p2);
    dx = p1.x - p2.x;
    dz = p1.z - p2.z;
    dist2 = SA1_FMAC(dx, dx, dz * dz);
    bot1 = p1.y - b1;
    top1 = p1.y + b1;
    top2 = p2.y + b2;
    bot2 = p2.y - b2;
    if (dist2 < (a1 + a2) * (a1 + a2) && top2 > bot1 && top1 > bot2) {
        func_8C02F8A0(twp1, cwp1, twp2, cwp2);
        func_8C02FA20(cwp1, cwp2);
        v.x = p1.x - p2.x;
        v.y = 0.0f;
        v.z = p1.z - p2.z;
        d = a1 + a2 - njSqrt(dist2) - 0.00099999996f;
        if (njScalor(&v) == 0.0f) {
            v.x = v.y = 0.0f;
            v.z = 0.099999996f;
        } else {
            njUnitVector(&v);
        }
        v.x = v.x * d * 0.39999998f;
        v.y = 0.0f;
        v.z = v.z * d * 0.39999998f;
        cwp1->flag |= 8;
        cwp2->flag |= 8;
        if (func_8C02FB3A(cwp1, info1, cwp2, info2)) {
            twp1->pos.x += v.x;
            twp1->pos.z += v.z;
        }
        if (func_8C02FB3A(cwp2, info2, cwp1, info1)) {
            twp2->pos.x -= v.x;
            twp2->pos.z -= v.z;
        }
    }
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp1, info1, &p1);
    }
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp2, info2, &p2);
    }
    return dist2;
}
#else
ASM_STUB(Float func_8C030634(colliwk *cwp1, colliwk *cwp2))
#endif

/*
 * Unit axis of a shape: (0, 1, 0) rotated by the task angles (unless
 * attr & 0x20 or 0x2000) and the shape angles; attr & 0x40 selects Y-Z-X
 * order instead of Z-X-Y. Used by the capsule/cylinder pair tests
 * 8C0309E0, 8C030F20, 8C0315C0, 8C031726.
 */
void func_8C030850(taskwk *twp, CCL_INFO *ccl, NJS_VECTOR *axis)
{
    NJS_VECTOR up;

    up.x = 0.0f;
    up.y = 1.0f;
    up.z = 0.0f;
    njPushMatrix(D_8C67C640);
    njTranslateV(NULL, &twp->pos);
    if (ccl->attr & 0x40) {
        if (!(ccl->attr & 0x20) && !(ccl->attr & 0x2000)) {
            twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        }
        ccl->angy ? njRotateY(NULL, ccl->angy) : (void)0;
        ccl->angz ? njRotateZ(NULL, ccl->angz) : (void)0;
        ccl->angx ? njRotateX(NULL, ccl->angx) : (void)0;
    } else {
        if (!(ccl->attr & 0x20) && !(ccl->attr & 0x2000)) {
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
            twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        }
        ccl->angz ? njRotateZ(NULL, ccl->angz) : (void)0;
        ccl->angx ? njRotateX(NULL, ccl->angx) : (void)0;
        ccl->angy ? njRotateY(NULL, ccl->angy) : (void)0;
    }
    njCalcVector(NULL, &up, axis);
    njPopMatrix(1);
}
