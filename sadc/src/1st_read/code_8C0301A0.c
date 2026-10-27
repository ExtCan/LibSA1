/*
 * CCL pair tests 8C0301A0 and 8C030540 (object-vs-object collision,
 * include/sa1/ccl.h). Both are reached through the shape-pair dispatch of
 * 8C0335D4/8C033800/8C033A10 with the two colliwk being tested
 * (colliwk.test_num selects the shape) and return the squared horizontal
 * distance of the two shape centres.
 */
#include "common.h"
#include "task.h"
#include "sa1/sa1.h"

/* same chunk (BSR) */
ASM_STUB(void func_8C02F8A0(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2))
ASM_STUB(void func_8C02FA20(colliwk *cwp1, colliwk *cwp2))
ASM_STUB(Sint8 func_8C02FB3A(colliwk *cwp1, CCL_INFO *ccl1, colliwk *cwp2, CCL_INFO *ccl2))
ASM_STUB(void func_8C02FC12(taskwk *twp, CCL_INFO *ccl, NJS_POINT3 *pos))
ASM_STUB(void func_8C02FD32(taskwk *twp, CCL_INFO *ccl, NJS_POINT3 *pos))
ASM_STUB(void func_8C02FF60(taskwk *twp, CCL_INFO *ccl, NJS_POINT3 *pos))

/* 8C02F27C: debug display of a shape */
extern void func_8C02F27C(taskwk *twp, CCL_INFO *ccl, NJS_POINT3 *pos);

/*
 * 8C0301A0: shape 1 (centre from 8C02FF60 = twp->pos raised by b; radius a,
 * half height b, c added to the radius of shape 2 for the landing test)
 * against the cylinder of shape 2 (centre from 8C02FD32, radius a, half
 * height b): landing on top ("UP": twp1 is put on the top surface and its
 * x/z angles cleared), hitting it from below ("DOWN") or being pushed out
 * sideways ("SIDE"). Returns the squared horizontal centre distance.
 * The b1/b2 load order below is needed for the FR12/FR13 scheduling.
 */
Float func_8C0301A0(colliwk *cwp1, colliwk *cwp2)
{
    Uint16 n1;
    Uint16 n2;
    CCL_INFO *info1;
    CCL_INFO *info2;
    taskwk *twp1;
    taskwk *twp2;
    Float b1;
    Float b2;
    Float dist2;
    Float top1;
    Float bot1;
    Float top2;
    Float bot2;
    Float d;
    NJS_POINT3 p1;
    NJS_POINT3 p2;
    NJS_VECTOR v;
    Float a1;
    Float a2;
    Float c1;

    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    info1 = &cwp1->info[n1];
    info2 = &cwp2->info[n2];
    a1 = info1->a;
    a2 = info2->a;
    b1 = info1->b;
    b2 = info2->b;
    c1 = info1->c;
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    func_8C02FF60(twp1, info1, &p1);
    func_8C02FD32(twp2, info2, &p2);
    dist2 = SA1_FMAC(p1.x - p2.x, p1.x - p2.x, (p1.z - p2.z) * (p1.z - p2.z));
    top1 = p1.y + b1;
    bot1 = p1.y;
    bot1 -= b1;
    top2 = p2.y + b2;
    bot2 = p2.y - b2;
    if (top2 > bot1 && bot1 > top2 - 3.5f && (a2 + c1) * (a2 + c1) > dist2) {
        if (tcrf_CollisionDisplayVar >= 0) {
            njPrintC(0x000A000F, "UP");
        }
        if (cwp1->hit_cwp == NULL) {
            func_8C02F8A0(twp1, cwp1, twp2, cwp2);
            func_8C02FA20(cwp1, cwp2);
            twp2->flag |= 0x10;
            cwp2->flag |= 4;
        }
        cwp1->flag |= 0x10;
        if (func_8C02FB3A(cwp1, info1, cwp2, info2)) {
            twp1->flag &= ~8;
            twp1->flag |= 0x20;
            twp1->pos.y = top2 - 0.01f;
            twp1->ang.x = 0;
            twp1->ang.z = 0;
        }
        return dist2;
    }
    if ((a1 + a2) * (a1 + a2) > dist2) {
        if (top1 > bot2 && bot2 + 2.0f > top1) {
            func_8C02F8A0(twp1, cwp1, twp2, cwp2);
            func_8C02FA20(cwp1, cwp2);
            twp2->flag |= 0x20;
            cwp2->flag |= 0x10;
            cwp1->flag |= 4;
            if (func_8C02FB3A(cwp1, info1, cwp2, info2)) {
                twp1->flag |= 0x10;
                twp1->flag &= ~8;
                twp1->pos.y = bot2 - b1 * 2.0f + 0.01f;
            }
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0x000A000F, "DOWN");
            }
        } else if (bot1 + 1.5f < top2 - 3.5f && top1 > bot2) {
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0x000A000F, "SIDE");
            }
            func_8C02F8A0(twp1, cwp1, twp2, cwp2);
            func_8C02FA20(cwp1, cwp2);
            cwp1->flag |= 8;
            cwp2->flag |= 8;
            if (func_8C02FB3A(cwp1, info1, cwp2, info2)) {
                v.x = p1.x - p2.x;
                v.y = 0.0f;
                v.z = p1.z - p2.z;
                d = njScalor(&v);
                d -= info1->a;
                d -= info2->a;
                if (d < 0.0f) {
                    if (njScalor(&v) == 0.0f) {
                        v.x = v.y = 0.0f;
                        v.z = 0.099999996f;
                    } else {
                        njUnitVector(&v);
                    }
                    d = -0.099999996f - d;
                    v.x = v.x * d * 0.7f;
                    v.z = v.z * d * 0.7f;
                    twp1->pos.x += v.x;
                    twp1->pos.z += v.z;
                }
            }
        } else {
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0x000A000F, "NONE");
            }
        }
    }
    p1 = info1->center;
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp1, info1, &p1);
    }
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp2, info2, &p2);
    }
    return dist2;
}

/*
 * 8C030540: shape 1 (centre from 8C02FC12, radius a) against the cylinder of
 * shape 2 (centre from 8C02FD32, radius a, half height b) grown by the
 * radius of shape 1: records the hit (8C02F8A0, 8C02FA20) when the centre of
 * shape 1 is inside. Returns the squared horizontal centre distance.
 *
 * NON_MATCHING: complete and functionally faithful (1 FMAC = 1 SA1_FMAC).
 * Same instruction sequence as the original; shcdiff differs only in two
 * register-allocation swaps: (1) info1/info2 are in R11/R12 instead of
 * R12/R11, (2) the temporaries top/bot are in FR5/FR6 instead of FR6/FR5.
 * Tried: n1/n2 inline or partly inline, every order of the a1/a2/b2 loads and
 * of the twp loads, top/bot/r statement orders and incremental forms, r*r
 * inline or in a variable, p2.y/p1.y temporaries, comparison spellings
 * (<, !(>=)), && instead of nested ifs, dx/dz locals, FMAC operand order,
 * declaration order and names (no effect), dead template loads (b1/c1/c2 as
 * in 8C0301A0: they move info1 above cwp1/cwp2 or reshuffle cwp1/cwp2).
 * The two display ifs may be nested or separate (SHC threads the jump).
 */
#ifdef NON_MATCHING
Float func_8C030540(colliwk *cwp1, colliwk *cwp2)
{
    Uint16 n1;
    Uint16 n2;
    CCL_INFO *info1;
    CCL_INFO *info2;
    taskwk *twp1;
    taskwk *twp2;
    Float a1;
    Float a2;
    Float b2;
    Float dist2;
    Float r;
    Float top;
    Float bot;
    NJS_POINT3 p1;
    NJS_POINT3 p2;

    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    info1 = &cwp1->info[n1];
    info2 = &cwp2->info[n2];
    a1 = info1->a;
    a2 = info2->a;
    b2 = info2->b;
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    func_8C02FC12(twp1, info1, &p1);
    func_8C02FD32(twp2, info2, &p2);
    dist2 = SA1_FMAC(p1.x - p2.x, p1.x - p2.x, (p1.z - p2.z) * (p1.z - p2.z));
    top = p2.y + b2 + a1;
    bot = p2.y - b2 - a1;
    r = a1 + a2;
    if (r * r > dist2) {
        if (top > p1.y && p1.y > bot) {
            func_8C02F8A0(twp1, cwp1, twp2, cwp2);
            func_8C02FA20(cwp1, cwp2);
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
ASM_STUB(Float func_8C030540(colliwk *cwp1, colliwk *cwp2))
#endif
