#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * CCL (object-vs-object collision) 8C031D00: sphere (cwp1) vs box (cwp2)
 * pair test with landing ("UP"), ceiling ("DOWN") and side ("SIDE")
 * resolution. Static version of func_8C031F88 (no sweep from the previous
 * position). Reached from the shape pair dispatch 8C033DAE.
 */

extern void func_8C02FC12(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C02FE5C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C02F27C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C02F8A0(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2);
extern void func_8C02FA20(colliwk *cwp1, colliwk *cwp2);
extern Sint8 func_8C02FB3A(colliwk *cwp1, CCL_INFO *ccl1, colliwk *cwp2, CCL_INFO *ccl2);
extern void func_8C03431C(colliwk *cwp1, NJS_POINT3 *p1, colliwk *cwp2, NJS_POINT3 *p2);

extern const char D_8C1125B4[];         /* "UP" */
extern const char D_8C1125B8[];         /* "DOWN" */
extern const char D_8C1125C0[];         /* "SIDE" */

/* 8C0318C0 (same chunk, BSR): pos inside the box shape of cwp2 grown by r */
ASM_STUB(Sint32 func_8C0318C0(colliwk *cwp1, NJS_POINT3 *pos, colliwk *cwp2, NJS_POINT3 *center, Float r))

/*
 * Matching notes (SHC):
 *  - SH_ONLY dead loads of ci2->a/ci2->c (as in func_8C031F88): ci2 R11
 *    above twp2 R10;
 *  - "register" on both parameters and on top2 only: cwp1/cwp2 R14/R13,
 *    top2 FR15, r1 FR14, and b2/bot2 spilled to @(4,R15)/@(0,R15)
 *    instead of top1;
 *  - top1 computed before bot1 (top1 FR13, bot1 FR12);
 *  - the "DOWN" landing height copies r1 before the multiply
 *    (FMOV FR14,FR2; FMUL FR3,FR2), i.e. r1 was still live after it. The
 *    dead "if (0)" block after the DOWN debug print reproduces that: SHC
 *    keeps it (and the use of r1) through register allocation and drops
 *    it afterwards, so it emits no code. Most likely the original had some
 *    disabled (debug) code there; a dead store or "r1;" is removed too early
 *    and an empty "if (r1 ...) {}" leaves its FCMP behind.
 */
void func_8C031D00(register colliwk *cwp1, register colliwk *cwp2)
{
    CCL_INFO *ci1;
    CCL_INFO *ci2;
    taskwk *twp1;
    taskwk *twp2;
    NJS_POINT3 p1;
    NJS_POINT3 p2;
    register Float top2;
    Float r1;
    Float top1;
    Float bot1;
    SH_ONLY(Float a2;)
    Float b2;
    SH_ONLY(Float c2;)
    Float bot2;

    ci1 = &cwp1->info[cwp1->test_num];
    ci2 = &cwp2->info[cwp2->test_num];
    r1 = ci1->a;
    SH_ONLY(a2 = ci2->a;)
    b2 = ci2->b;
    SH_ONLY(c2 = ci2->c;)
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    func_8C02FC12(twp1, ci1, &p1);
    func_8C02FE5C(twp2, ci2, &p2);
    top1 = p1.y + r1;
    bot1 = p1.y - r1;
    top2 = p2.y + b2;
    bot2 = p2.y - b2;
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp2, ci2, &p2);
    }
    if (top2 > bot1 && bot1 > top2 + -4.0f && func_8C0318C0(cwp1, &p1, cwp2, &p2, r1)) {
        if (tcrf_CollisionDisplayVar >= 0) {
            njPrintC(0x000A000F, D_8C1125B4);
        }
        if (!(cwp1->flag & 0x4000)) {
            func_8C02F8A0(twp1, cwp1, twp2, cwp2);
        }
        func_8C02FA20(cwp1, cwp2);
        twp1->flag |= 0x20;
        twp2->flag |= 0x10;
        cwp1->flag |= 0x10;
        cwp2->flag |= 0x4;
        if (func_8C02FB3A(cwp1, ci1, cwp2, ci2)) {
            twp1->pos.y = top2 + -0.0099999998f;
            twp1->ang.x = 0;
            twp1->ang.z = 0;
        }
    } else if (func_8C0318C0(cwp1, &p1, cwp2, &p2, r1)) {
        if (top1 > bot2 && bot2 + 2.0f > top1) {
            func_8C02F8A0(twp1, cwp1, twp2, cwp2);
            func_8C02FA20(cwp1, cwp2);
            twp1->flag |= 0x10;
            twp2->flag |= 0x20;
            cwp1->flag |= 0x4;
            cwp2->flag |= 0x10;
            if (func_8C02FB3A(cwp1, ci1, cwp2, ci2)) {
                twp1->pos.y = bot2 - r1 * 2.0f + 0.0099999998f;
            }
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0x000A000F, D_8C1125B8);
            }
            if (0) {
                twp1->pos.x = r1;   /* dead: see the matching notes above */
            }
        } else if (top2 + -4.0f > bot1 && top1 > bot2) {
            func_8C02F8A0(twp1, cwp1, twp2, cwp2);
            func_8C02FA20(cwp1, cwp2);
            cwp1->flag |= 0x4000;
            cwp1->flag |= 0x8;
            cwp2->flag |= 0x8;
            if (func_8C02FB3A(cwp1, ci1, cwp2, ci2)) {
                func_8C03431C(cwp1, &p1, cwp2, &p2);
            }
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0x000A0010, D_8C1125C0);
            }
        }
    }
}
