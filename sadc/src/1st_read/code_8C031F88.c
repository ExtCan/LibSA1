#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * CCL (object-vs-object collision) 8C031F88: swept sphere (cwp1, a player
 * shape) vs box (cwp2) pair test. Same landing ("UP") / ceiling ("DOWN") /
 * side ("SIDE") resolution as func_8C031D00, but the sphere is moved from the
 * player's previous position (GetPlayerPosition, history frame 1) to its
 * current one in steps of 2.5 units, so that a fast player does not pass
 * through a thin box. On a hit the corrected position is written back into
 * the position history (SetPlayerPosition frame 0). Reached from the shape
 * pair dispatch 8C033A10 (case 4).
 */

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
 * NON_MATCHING (SHC: 16 of 478 instructions differ, all FP register choices;
 * code, stack frame and integer registers match):
 *  - the sqrt operands d.x/d.z get FR13/FR12 instead of FR12/FR13, and the
 *    loop's bot1/top1 get FR14/FR13 instead of FR13/FR14. Not changed by
 *    declaration order or scope, names, comparison spelling, else-if
 *    chains, sum order (other orders change the FMUL/FMAC sequence) or dead
 *    copies; "register" and goto-merged tails change far more.
 *    Also unchanged by: the order of the start loads (all 180 orders of
 *    ci1/ci2/r1/b2/twp1/twp2 tried; b2 before r1 fixes the sqrt pair but
 *    reschedules the prologue and the hoisted-temp slots), dead stores to
 *    b2/top1, a "y = p.y" temp, "--n", "0 < n", break instead of return,
 *    preceding functions in the file, d as Float[3] or through a pointer.
 *    The two pairs are coupled: with no callee-saved FMAC temp in the sqrt
 *    (e.g. njSqrt(d.x * d.x)) the loop gets the original FR13/FR14.
 *  - The 7 literals at the start of the first pool (8C032164-8C03217C, not
 *    used here) belong to the SIDE branch of func_8C031D00, which lies after
 *    its own pool: both were one translation unit; shcdiff does not compare
 *    pool layout.
 *  - Also tried without effect on the two pairs (second pass): top1
 *    computed before bot1, "d.y*d.y + d.x*d.x + ..." and
 *    "d.x*d.x + (d.y*d.y + d.z*d.z)", "register" on either parameter or on
 *    any single Float (rewrites everything), all 512 combinations of
 *    "a op= b" vs "a = a op b" for the d /= len, d *= 2.5f and p += d
 *    statements, "x = x & ~k" flag spellings, dropping a2 or c2, a dead
 *    load of ci1->b. These only move other values: the {len, bot2} chain
 *    between FR12/FR13 and the order of the hoisted slots sp+16..24. In
 *    every variant bot1 shares b2's register (orig: top1 does, bot1 shares
 *    the 0.01f constant's). A memory-resident "y = d.y" ((void)&y) gives
 *    the original loop structure but moves the d.y slot from sp+32 to sp+16
 *    and swaps b2/0.01 between FR13/FR14. Spill slots are assigned in
 *    allocation-priority order, so the ties look like an allocator sort
 *    tie-break that depends on the IR numbering.
 *  - Found on the way: the stack layout needs "n--; if (n > 0)" (not
 *    "if (--n > 0)"); the dead MOV.L R13,@(28,R15) needs the start to read
 *    the player number through cwp1->mytask->twp again; R8/R9 (ci2 vs the
 *    hoisted 0x000A000F) need a dead load of another ci2 extent (the
 *    original very likely loaded a2/b2/c2 like func_8C031B88; SHC counts the
 *    reference before removing the load); "d.x = d.y = d.z = 0" stores in
 *    the wrong order.
 */
#ifdef NON_MATCHING
void func_8C031F88(colliwk *cwp1, colliwk *cwp2)
{
    NJS_POINT3 p;
    NJS_POINT3 p2;
    NJS_POINT3 old;
    NJS_VECTOR d;
    CCL_INFO *ci1;
    CCL_INFO *ci2;
    taskwk *twp1;
    taskwk *twp2;
    Float r1;
    SH_ONLY(Float a2;)
    Float b2;
    SH_ONLY(Float c2;)
    Float len;
    Float bot1;
    Float top1;
    Float bot2;
    Float top2;
    Sint32 n;

    ci1 = &cwp1->info[cwp1->test_num];
    ci2 = &cwp2->info[cwp2->test_num];
    r1 = ci1->a;
    SH_ONLY(a2 = ci2->a;)
    b2 = ci2->b;
    SH_ONLY(c2 = ci2->c;)
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    GetPlayerPosition(cwp1->mytask->twp->counter.b[0], 1, &old, NULL);
    d.x = twp1->pos.x - old.x;
    d.y = twp1->pos.y - old.y;
    d.z = twp1->pos.z - old.z;
    /*
     * SH-4: FMUL d.y*d.y, FMAC d.x, FMAC d.z. SHC emits that order only for
     * the left-nested sum (it rotates the SA1_FMAC spelling), so SHC gets the
     * plain sum and the PC the fused form of the original.
     */
    len = njSqrt(SH_ONLY(d.x * d.x + d.y * d.y + d.z * d.z)
                 PC_ONLY(SA1_FMAC(d.z, d.z, SA1_FMAC(d.x, d.x, d.y * d.y))));
    if (len > 0.0f) {
        d.x /= len;
        d.y /= len;
        d.z /= len;
        d.x *= 2.5f;
        d.y *= 2.5f;
        d.z *= 2.5f;
    } else {
        d.x = 0.0f;
        d.y = 0.0f;
        d.z = 0.0f;
        len = 0.0099999998f;
    }
    /* number of 2.5-unit steps (func_8C10D148: libm wrapper, rounds up) */
    n = SA1_FTOI(func_8C10D148(len * 0.39999998f));
    p.x = old.x;
    p.y = old.y + r1;
    p.z = old.z;
    func_8C02FE5C(twp2, ci2, &p2);
    top2 = p2.y + b2;
    bot2 = p2.y - b2;
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp2, ci2, &p2);
    }
    while (n > 0) {
        n--;
        if (n > 0) {
            p.x += d.x;
            p.y += d.y;
            p.z += d.z;
        } else {
            p.x = twp1->pos.x;
            p.y = twp1->pos.y + r1;
            p.z = twp1->pos.z;
        }
        bot1 = p.y - r1;
        top1 = p.y + r1;
        if (top2 > bot1 && bot1 > top2 + -4.0f && func_8C0318C0(cwp1, &p, cwp2, &p2, r1)) {
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0x000A000F, D_8C1125B4);
            }
            if (!(cwp1->flag & 0x4000)) {
                func_8C02F8A0(twp1, cwp1, twp2, cwp2);
            }
            func_8C02FA20(cwp1, cwp2);
            twp2->flag |= 0x10;
            cwp1->flag |= 0x10;
            cwp2->flag |= 0x4;
            if (func_8C02FB3A(cwp1, ci1, cwp2, ci2)) {
                twp1->pos.y = top2 + -0.0099999998f;
                twp1->ang.x = 0;
                twp1->ang.z = 0;
                twp1->flag |= 0x20;
                twp1->flag &= ~0x8;
            }
            SetPlayerPosition(cwp1->mytask->twp->counter.b[0], 0, &twp1->pos, NULL);
            return;
        }
        if (func_8C0318C0(cwp1, &p, cwp2, &p2, r1)) {
            if (top1 > bot2 && bot2 + 2.0f > top1) {
                func_8C02F8A0(twp1, cwp1, twp2, cwp2);
                func_8C02FA20(cwp1, cwp2);
                twp2->flag |= 0x20;
                cwp1->flag |= 0x4;
                cwp2->flag |= 0x10;
                if (func_8C02FB3A(cwp1, ci1, cwp2, ci2)) {
                    twp1->pos.y = bot2 - r1 * 2.0f + 0.0099999998f;
                    twp1->flag &= ~0x8;
                    twp1->flag |= 0x10;
                }
                if (tcrf_CollisionDisplayVar >= 0) {
                    njPrintC(0x000A000F, D_8C1125B8);
                }
                SetPlayerPosition(cwp1->mytask->twp->counter.b[0], 0, &twp1->pos, NULL);
                return;
            }
            if (top2 + -4.0f > bot1 && top1 > bot2) {
                func_8C02F8A0(twp1, cwp1, twp2, cwp2);
                func_8C02FA20(cwp1, cwp2);
                cwp1->flag |= 0x4000;
                cwp1->flag |= 0x8;
                cwp2->flag |= 0x8;
                if (func_8C02FB3A(cwp1, ci1, cwp2, ci2)) {
                    func_8C03431C(cwp1, &p, cwp2, &p2);
                }
                if (tcrf_CollisionDisplayVar >= 0) {
                    njPrintC(0x000A000F, D_8C1125C0);
                }
                SetPlayerPosition(cwp1->mytask->twp->counter.b[0], 0, &twp1->pos, NULL);
                return;
            }
        }
    }
}
#else
ASM_STUB(void func_8C031F88(colliwk *cwp1, colliwk *cwp2))
#endif
