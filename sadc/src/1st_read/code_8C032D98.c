#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * CCL (object-vs-object collision) 8C032D98-8C03339E: swept cylinder
 * (player shape) vs box pair test, player kept inside a cylinder, and the
 * shape attribute filter of the pair dispatch.
 */

extern void func_8C02FE5C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C02F27C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);

/*
 * Translation-unit layout model. func_8C031A84 and func_8C031B20 belong to
 * the same original TU (8C0318C0-8C0344A0) but lie about 5 KB before the
 * call sites, out of BSR range: SHC calls in-TU functions out of range with
 * JSR through the pool and, unlike extern callees, never hoists their
 * addresses into callee-saved registers. tu_gap_8C031B88 stands in for the
 * code between 8C031B88 and 8C032D98. tu_tail_8C032406 stands in for the end
 * of func_8C032406, whose literal pool entries are still pending when
 * func_8C032D98 starts: SHC keeps one pool per file, and the original dumps
 * that leftover pool after func_8C032D98's first BRA (8C032E3C), which puts
 * the second pool after the BRA at 8C03305C and turns four BF into
 * BT/BRA/NOP. A tail of 128 calls (one pending entry referenced 0x200 bytes
 * before the function) reproduces that placement. On SHC the stubs, the gap
 * and the tail are never spliced (ASM_STUB bodies are ignored, the stand-ins
 * have no address).
 */
/* out = a - b rotated by ang around Y */
ASM_STUB(void func_8C031A84(NJS_POINT3 *a, NJS_POINT3 *b, Angle ang, NJS_POINT3 *out))
/* v = v rotated by -ang around Y, plus b */
ASM_STUB(void func_8C031B20(NJS_POINT3 *a, NJS_POINT3 *b, Angle ang, NJS_VECTOR *v))
#if !defined(TARGET_PC)
#define TU_GAP4  __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink();
#define TU_GAP16 TU_GAP4 TU_GAP4 TU_GAP4 TU_GAP4
#define TU_GAP64 TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP16
void tu_gap_8C031B88(void)
{
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64
}

void tu_tail_8C032406(void)
{
    TU_GAP64 TU_GAP64
}
#endif

/*
 * Swept cylinder (cwp1, a player shape: radius a, height b) vs box (cwp2,
 * half sizes a/b, rotated by angy around Y; absolute angle when attr has
 * 0x20 or 0x2000). The cylinder centre is moved from the player's previous
 * position (GetPlayerPosition history frame 1) to the current one in steps of
 * 2.5 units; at the first step that overlaps the box in local x and y:
 *  - 0 <= z - a1 < 50 (local z): moved 6% of the way towards z = a1 + 50,
 *  - -50 < z < a1: put at z = a1 + 0.1 and also written to the position
 *    history (SetPlayerPosition frame 0);
 * only pos.x/pos.z are written. Called from 8C033CDC.
 */
void func_8C032D98(colliwk *cwp1, colliwk *cwp2)
{
    NJS_POINT3 p;
    NJS_POINT3 p2;
    Angle3 ang;
    NJS_POINT3 old;
    NJS_VECTOR d;
    NJS_POINT3 l;
    CCL_INFO *ci1;
    CCL_INFO *ci2;
    taskwk *twp1;
    taskwk *twp2;
    Float a1;
    Float b1;
    Float a2;
    Float b2;
    Float len;
    Sint32 n;

    ci1 = &cwp1->info[cwp1->test_num];
    ci2 = &cwp2->info[cwp2->test_num];
    a1 = ci1->a;
    a2 = ci2->a;
    b1 = ci1->b;
    b2 = ci2->b;
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    GetPlayerPosition(cwp1->mytask->twp->counter.b[0], 1, &old, NULL);
    d.x = twp1->pos.x - old.x;
    d.y = twp1->pos.y - old.y;
    d.z = twp1->pos.z - old.z;
    if ((ci2->attr & 0x20) || (ci2->attr & 0x2000)) {
        ang.y = ci2->angy;
    } else {
        ang.y = twp2->ang.y + ci2->angy;
    }
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
        len = 0.01f;
        d.x = 0.0f;
        d.y = 0.0f;
        d.z = 0.0f;
    }
    n = SA1_FTOI(func_8C10D148(len * 0.39999998f));
    p.x = old.x;
    p.y = old.y + b1;
    p.z = old.z;
    func_8C02FE5C(twp2, ci2, &p2);
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp2, ci2, &p2);
    }
    while (n >= 0) {
        n--;
        if (n > 0) {
            p.x += d.x;
            p.y += d.y;
            p.z += d.z;
        } else {
            p.x = twp1->pos.x;
            p.y = twp1->pos.y + b1;
            p.z = twp1->pos.z;
        }
        func_8C031A84(&p, &p2, -ang.y, &l);
        if (l.y + b1 > -b2 && b2 > l.y - b1 && l.x + a1 > -a2 && a2 > l.x - a1) {
            if (l.z - a1 < 50.0f && l.z - a1 >= 0.0f) {
                l.z SH_ONLY(+= (a1 - l.z + 50.0f) * 0.06f)
                    PC_ONLY(= SA1_FMAC(a1 - l.z + 50.0f, 0.06f, l.z));
                func_8C031B20(&p, &p2, -ang.y, &l);
                l.y -= b1;
                twp1->pos.x = l.x;
                twp1->pos.z = l.z;
                return;
            }
            if (l.z - a1 < 0.0f && l.z > -50.0f) {
                l.z = a1 + 0.099999996f;
                func_8C031B20(&p, &p2, -ang.y, &l);
                l.y -= b1;
                twp1->pos.x = l.x;
                twp1->pos.z = l.z;
                SetPlayerPosition(cwp1->mytask->twp->counter.b[0], 0, &twp1->pos, NULL);
                return;
            }
        }
    }
}

/*
 * Player cylinder (cwp1, radius a, height b) inside cylinder cwp2: when the
 * player's circle leaves cwp2's (horizontal distance + a1 > a2), pull the
 * player back towards cwp2's axis by 70% of the overshoot (+0.1).
 * The dead block at the end stands in for code compiled out of the original:
 * it owns the frame's otherwise unused vector (sp+12) and raises ci2's SHC
 * register priority (counted before dead code is removed) so that ci2, twp1
 * and &pos get R14, R13, R12 as in the original.
 */
void func_8C033122(colliwk *cwp1, colliwk *cwp2)
{
    NJS_POINT3 pos;
    NJS_POINT3 p2;
    NJS_POINT3 dbg;
    NJS_VECTOR v;
    CCL_INFO *ci1;
    CCL_INFO *ci2;
    taskwk *twp1;
    taskwk *twp2;
    Float b1;
    Float f;

    twp2 = cwp2->mytask->twp;
    ci1 = &cwp1->info[cwp1->test_num];
    twp1 = cwp1->mytask->twp;
    ci2 = &cwp2->info[cwp2->test_num];
    b1 = ci1->b;
    pos = twp1->pos;
    func_8C02FE5C(twp2, ci2, &p2);
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C02F27C(twp2, ci2, &p2);
    }
    pos.x = twp1->pos.x;
    pos.y = twp1->pos.y + b1;
    pos.z = twp1->pos.z;
    v.x = pos.x - p2.x;
    v.y = 0.0f;
    v.z = pos.z - p2.z;
    f = njScalor(&v);
    f += ci1->a;
    f -= ci2->a;
    if (f > 0.0f) {
        if (njScalor(&v) == 0.0f) {
            v.y = 0.0f;
            v.x = 0.0f;
            v.z = 0.099999996f;
        } else {
            njUnitVector(&v);
        }
        f = -0.099999996f - f;
        v.x = v.x * f * 0.7f;
        v.z = v.z * f * 0.7f;
        twp1->pos.x += v.x;
        twp1->pos.z += v.z;
    }
    if (0) {
        dbg.x = ci2->center.x;
        dbg.y = ci2->center.y + ci2->b;
        dbg.z = ci2->center.z;
    }
}

/*
 * 1 when either shape of the pair has the attribute bit that excludes the
 * other one's collision id: ids 0/1 -> 0x10000, 2/3 -> 0x20000,
 * 4/5 -> 0x40000, 6/7 -> 0x80000.
 */
Sint32 func_8C0332A0(colliwk *cwp1, colliwk *cwp2)
{
    CCL_INFO *ci1;
    CCL_INFO *ci2;

    ci1 = &cwp1->info[cwp1->test_num];
    ci2 = &cwp2->info[cwp2->test_num];
    switch (cwp1->id) {
    case 0:
    case 1:
        if (ci2->attr & 0x10000) {
            return 1;
        }
        break;
    case 2:
    case 3:
        if (ci2->attr & 0x20000) {
            return 1;
        }
        break;
    case 4:
    case 5:
        if (ci2->attr & 0x40000) {
            return 1;
        }
        break;
    case 6:
    case 7:
        if (ci2->attr & 0x80000) {
            return 1;
        }
        break;
    }
    switch (cwp2->id) {
    case 0:
    case 1:
        if (ci1->attr & 0x10000) {
            return 1;
        }
        break;
    case 2:
    case 3:
        if (ci1->attr & 0x20000) {
            return 1;
        }
        break;
    case 4:
    case 5:
        if (ci1->attr & 0x40000) {
            return 1;
        }
        break;
    case 6:
    case 7:
        if (ci1->attr & 0x80000) {
            return 1;
        }
        break;
    }
    return 0;
}
