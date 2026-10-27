/*
 * CCL pair test 8C030F20: sphere (cwp1) against capsule (cwp2)
 * (object-vs-object collision, include/sa1/ccl.h). Reached through the
 * shape-pair dispatch of 8C0335D4/8C033800 with the two colliwk being
 * tested (colliwk.test_num selects the shape). Sibling of the matched
 * cylinder/cylinder test 8C0309E0 (code_8C0309E0.c), written the same way.
 *
 * The capsule axis segment is center2 +/- b2 * axis; the distance from the
 * sphere centre to the axis line decides the hit. When the closest point lies
 * between the two ends the side is hit (cwp2->flag |= 8), otherwise the
 * sphere is tested against the upper (|= 4) or lower (|= 0x10) end cap. On a
 * hit the shapes are pushed apart along the separation vector (only shapes
 * for which 8C02FB3A agrees). Returns the point-line distance.
 *
 * NON_MATCHING: complete and functionally faithful (same operations in the
 * same order, 7 FMACs as in the original). Every instruction sequence is the
 * original one; only the FP register allocation differs:
 *  - original: FR12 = 0.1f and FR13 = -0.1f hoisted at the top of the hit
 *    branch, FR14 = dist, FR15 = len / r1 + r2 / push depth; r1, r2, d2 and
 *    d2b spilled to @8, @4, @12, @16. Here r2 wins FR13 (and shares it with
 *    d2/d2b), so only -0.1f is hoisted (into FR12), 0.1f is reloaded with
 *    MOVA at each use and the frame is 12 bytes smaller (all sp offsets
 *    shift, hence the large raw diff count).
 * Findings (this source vs. alternatives):
 *  - the side push depth is one expression, as in 8C0309E0. SHC sorts the
 *    operands of a float +/- chain by declaration order, so dist must be
 *    declared before r2: then r1 - dist + r2 stays (r1 - dist) + r2 like the
 *    original; with dist declared after r2 it becomes (r1 + r2) - dist (CSE
 *    with the compare) and r1/r2 die early. Other declaration orders and
 *    operand spellings (r2 + r1, dist < r1 + r2, + -0.1f, 0.1 double,
 *    integer 0) compile identically.
 *  - the previous version split it (scl = r1 - dist; scl = scl + r2 - 0.1f):
 *    that spills r1/r2 and keeps the original frame (fewer raw diffs), but
 *    computes r2 + (r1 - dist) (r2 as destination) and hoists 0.1f/0.0f
 *    instead of 0.1f/-0.1f; it is the operand order that lowers r2's
 *    priority there, not the original's form.
 *  - the allocation reacts to code far away: dropping the second debug draw
 *    at the end, a dead duplicate "r2 = ccl2->a;" or separate zero stores
 *    each flip r2 out of FR13, but then 0.0f gets hoisted or dist moves.
 *    A reduced probe with the same shape (no other code) gives exactly the
 *    original pattern (r1/r2 spilled, 0.1f/-0.1f hoisted, FLDI0 reused for
 *    the zero stores) except FR12/FR13 swapped.
 *  - tried without success: rsum or the constants as variables, scl reused
 *    for r1 + r2, one variable for d2/d2b, volatile radii (kills the r1 + r2
 *    CSE), do/while(0) wrappers, a #pragma inline normalise helper (keeps a
 *    pointer register), dead constant assignments, sequential debug ifs.
 */
#include "common.h"
#include "task.h"
#include "sa1/sa1.h"

extern char D_8C112604[];               /* "HIT_CAPSULE_SIDE" */
extern char D_8C112618[];               /* "HIT_CAPSULE_UPPER" */
extern char D_8C11262C[];               /* "HIT_CAPSULE_LOWER" */

/* 8C030850: unit axis of the shape ((0,1,0) through the shape and task
 * rotations); same chunk (BSR) */
ASM_STUB(void func_8C030850(taskwk *twp, CCL_INFO *ccl, NJS_VECTOR *axis))

/* 8C02FC12: sphere centre in world space */
extern void func_8C02FC12(taskwk *twp, CCL_INFO *ccl, NJS_POINT3 *pos);
/* 8C02FD32: shape centre in world space */
extern void func_8C02FD32(taskwk *twp, CCL_INFO *ccl, NJS_POINT3 *pos);
/* 8C02F27C: debug display of a shape */
extern void func_8C02F27C(taskwk *twp, CCL_INFO *ccl, NJS_POINT3 *pos);
/* 8C02F8A0: record the hit in both colliwk */
extern void func_8C02F8A0(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2);
/* 8C02FA20: hit bookkeeping for the pair */
extern void func_8C02FA20(colliwk *cwp1, colliwk *cwp2);
/* 8C02FB3A: nonzero when shape 1 is pushed out by shape 2 */
extern Sint8 func_8C02FB3A(colliwk *cwp1, CCL_INFO *ccl1, colliwk *cwp2, CCL_INFO *ccl2);

#ifdef NON_MATCHING
Float func_8C030F20(colliwk *cwp1, colliwk *cwp2)
{
    NJS_POINT3 pos1;
    NJS_POINT3 pos2;
    NJS_POINT3 seg[2];
    NJS_VECTOR axis;
    NJS_LINE line;
    NJS_POINT3 cp;
    NJS_VECTOR v0;
    NJS_VECTOR v1;
    NJS_VECTOR vec;
    Uint16 n1;
    Uint16 n2;
    CCL_INFO *ccl1;
    CCL_INFO *ccl2;
    taskwk *twp1;
    taskwk *twp2;
    Float dist;
    Float r1;
    Float r2;
    Float len;
    Float dx;
    Float dy;
    Float dz;
    Float d2;
    Float d2b;
    Float scl;

    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    ccl1 = &cwp1->info[n1];
    ccl2 = &cwp2->info[n2];
    r1 = ccl1->a;
    r2 = ccl2->a;
    len = ccl2->b;
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;

    func_8C02FC12(twp1, ccl1, &pos1);
    func_8C030850(twp2, ccl2, &axis);
    func_8C02FD32(twp2, ccl2, &pos2);
    seg[0].x = SA1_FMAC(len, axis.x, pos2.x);
    seg[0].y = SA1_FMAC(len, axis.y, pos2.y);
    seg[0].z = SA1_FMAC(len, axis.z, pos2.z);
    seg[1].x = pos2.x - len * axis.x;
    seg[1].y = pos2.y - len * axis.y;
    seg[1].z = pos2.z - len * axis.z;

    line.px = seg[1].x;
    line.py = seg[1].y;
    line.pz = seg[1].z;
    line.vx = axis.x;
    line.vy = axis.y;
    line.vz = axis.z;
    dist = func_8C63FF84(&pos1, &line, &cp);

    v0.x = seg[0].x - cp.x;
    v0.y = seg[0].y - cp.y;
    v0.z = seg[0].z - cp.z;
    v1.x = seg[1].x - cp.x;
    v1.y = seg[1].y - cp.y;
    v1.z = seg[1].z - cp.z;

    if (r1 + r2 > dist) {
        if (njInnerProduct(&v0, &v1) < 0.0f) {
            if (tcrf_CollisionDisplayVar >= 0) {
                func_8C043A40(pos1.x, pos1.y, pos1.z, cp.x, cp.y, cp.z);
            }
            func_8C02F8A0(twp1, cwp1, twp2, cwp2);
            func_8C02FA20(cwp1, cwp2);
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0xA0011, D_8C112604);
            }
            cwp2->flag |= 8;
            vec.x = pos1.x - cp.x;
            vec.y = pos1.y - cp.y;
            vec.z = pos1.z - cp.z;
            scl = r1 - dist + r2 - 0.099999996f;
            if (njScalor(&vec) == 0.0f) {
                vec.x = vec.y = 0.0f;
                vec.z = 0.099999996f;
            } else {
                njUnitVector(&vec);
            }
            vec.x *= scl;
            vec.y *= scl;
            vec.z *= scl;
            if (func_8C02FB3A(cwp1, ccl1, cwp2, ccl2)) {
                twp1->pos.x += vec.x;
                twp1->pos.y += vec.y;
                twp1->pos.z += vec.z;
            }
            if (func_8C02FB3A(cwp2, ccl2, cwp1, ccl1)) {
                twp2->pos.x -= vec.x;
                twp2->pos.y -= vec.y;
                twp2->pos.z -= vec.z;
            }
        } else {
            dx = pos1.x - seg[0].x;
            dy = pos1.y - seg[0].y;
            dz = pos1.z - seg[0].z;
            d2 = SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy));
            if (d2 < (r1 + r2) * (r1 + r2)) {
                func_8C02F8A0(twp1, cwp1, twp2, cwp2);
                func_8C02FA20(cwp1, cwp2);
                if (func_8C02FB3A(cwp1, ccl1, cwp2, ccl2)) {
                    vec.x = pos1.x - seg[0].x;
                    vec.y = pos1.y - seg[0].y;
                    vec.z = pos1.z - seg[0].z;
                    scl = r1 + r2 - njSqrt(d2) - 0.099999996f;
                    if (njScalor(&vec) == 0.0f) {
                        vec.x = vec.y = 0.0f;
                        vec.z = 0.099999996f;
                    } else {
                        njUnitVector(&vec);
                    }
                    if (tcrf_CollisionDisplayVar >= 0) {
                        njPrintC(0xA0011, D_8C112618);
                    }
                    cwp2->flag |= 4;
                    vec.x *= scl;
                    vec.y *= scl;
                    vec.z *= scl;
                    twp1->pos.x += vec.x;
                    twp1->pos.y += vec.y;
                    twp1->pos.z += vec.z;
                }
            } else {
                dx = pos1.x - seg[1].x;
                dy = pos1.y - seg[1].y;
                dz = pos1.z - seg[1].z;
                d2b = SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy));
                if (d2b < (r1 + r2) * (r1 + r2)) {
                    func_8C02F8A0(twp1, cwp1, twp2, cwp2);
                    func_8C02FA20(cwp1, cwp2);
                    if (func_8C02FB3A(cwp1, ccl1, cwp2, ccl2)) {
                        vec.x = pos1.x - seg[1].x;
                        vec.y = pos1.y - seg[1].y;
                        vec.z = pos1.z - seg[1].z;
                        scl = r1 + r2 - njSqrt(d2b) - 0.099999996f;
                        if (njScalor(&vec) == 0.0f) {
                            vec.x = vec.y = 0.0f;
                            vec.z = 0.099999996f;
                        } else {
                            njUnitVector(&vec);
                        }
                        if (tcrf_CollisionDisplayVar >= 0) {
                            njPrintC(0xA0011, D_8C11262C);
                        }
                        cwp2->flag |= 0x10;
                        vec.x *= scl;
                        vec.y *= scl;
                        vec.z *= scl;
                        twp1->pos.x += vec.x;
                        twp1->pos.y += vec.y;
                        twp1->pos.z += vec.z;
                    }
                }
            }
        }
    }

    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp1, ccl1, &pos1);
        if (tcrf_CollisionDisplayVar >= 0) {
            func_8C02F27C(twp2, ccl2, &pos2);
        }
    }
    return dist;
}
#else
ASM_STUB(Float func_8C030F20(colliwk *cwp1, colliwk *cwp2))
#endif
