#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * CCL (object-vs-object collision) 8C032406: swept player shape (cwp1:
 * a, b = half height, c) vs "rectangle 2" (cwp2: a box with half extents
 * a/b/c, rotated around Y by angy (+ the owner's ang.y unless attr has
 * 0x20/0x2000) and around Z by angz). Like func_8C031F88, the player is moved
 * from its previous position (GetPlayerPosition history frame 1) to the
 * current one in steps of 2.5 units. The Z angle is folded by quarter turns
 * ((angz + 0x2000) & 0xFFFF picks the quadrant; a and b of the box are swapped
 * for the odd ones); the upper and lower faces are slopes of the remaining
 * angle (njTan), the sides are resolved in the box's local frame and pushed
 * out by 0.3 of the face distance (debug "RECTANGLE2 UPPER/LOWER/SIDE!!",
 * "FACE 1-4"). Every hit ends with SetPlayerPosition and returns.
 * Called from 8C033A10.
 *
 * Matching notes (NON_MATCHING; 927 of the 1061 original instructions align
 * (difflib on masked encodings), same frame 0x104 and control flow):
 *  - reproduced: struct slots (p @248 .. o @128), int registers (R8 njPrintC,
 *    R9 ci2, R10 &d, R12 &ang), the start loads (a1, a2, b1, b2 order as in
 *    func_8C032D98), the quadrant fold ("ang.z = ang.z - K" gives the single
 *    ang.z load in the BF/S delay slot), the length chain (FMUL d.y*d.y, FMAC
 *    d.x, FMAC d.z with d.x/d.y spilled to sp+124/120; see SUMSQ3 below), the
 *    d zeroing order, the dead store of cwp1->mytask to sp+116 (the player
 *    number is read through cwp1->mytask->twp again, as in func_8C032D98),
 *    ci1 @4. D_8C67C640 must be written &D_8C67C640 so its address is hoisted
 *    into three spill slots; "x = q.x" as a variable keeps a1 in FR12 (plain
 *    q.x spills a1); t is memory-resident in the original (sp+36: stored in
 *    both rotation tests, reloaded only in the first), hence "(void)&t".
 *  - still different: a2/b1 are FR13/FR14 (orig FR14/FR13, 0.3f in FR14);
 *    t @8 and n @36 (orig n @8, t @36); yup/ylo @24/@20 (orig @20/@24); the
 *    hoisted loop-invariant temps (sp+64..112) are in another order; the
 *    ang.y if/else store is tail-merged (orig: store, BRA, NOP); x = q.x is
 *    CSE'd with the q.x/r0.x loads of ylo (orig reloads both after the ylo
 *    store and keeps r0.x in FR4 / q.z in FR5 through the condition chains);
 *    the final rotation reloads ang.z/ang.y (orig keeps each in R11, i.e. a
 *    callee-saved register although nothing is live across the calls); the
 *    side test "a1 + b2 + -4.0f > s.y" loads s.y first (orig the constant);
 *    the 0.3 pushes load pos before q (orig q first, through R1).
 *  - findings for a next attempt (each alone reshuffles FP registers and
 *    spill slots, so the count drops; they look like the original forms):
 *    "n--; if (n > 0)" (func_8C031F88/func_8C032D98 style) and the push as
 *    "pos.x SH_ONLY(+= q.x * 0.3f) PC_ONLY(= SA1_FMAC(...))" (the compound
 *    lvalue sets R0 = 32 first, which is why the original reaches q through
 *    R1); with both, the 0.3 pushes match except registers. Without
 *    "(void)&t" a2/b1 get the original FR14/FR13 but ci2/njPrintC swap
 *    R8/R9 and the length chain changes; a dead "if (0) { t = ci2->angy; }"
 *    (SHC counts references before dead-code removal) gives ci2 R9 again but
 *    flips a2/b1 back. A variable for the final rotation angle gets a
 *    scratch register (R5), never R11. "Angle ang[3]", else-if or ternary
 *    ang.y forms and plain "ang.z" rotation arguments are worse.
 */

extern void func_8C02FE5C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C02F27C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C02F8A0(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2);
extern void func_8C02FA20(colliwk *cwp1, colliwk *cwp2);
extern Sint8 func_8C02FB3A(colliwk *cwp1, CCL_INFO *ccl1, colliwk *cwp2, CCL_INFO *ccl2);

extern const char D_8C112640[];         /* "RECTANGLE2 UPPER!!" */
extern const char D_8C112654[];         /* "RECTANGLE2 LOWER!!" */
extern const char D_8C112668[];         /* "RECTANGLE2 SIDE!!" */
extern const char D_8C11267C[];         /* "FACE 1" */
extern const char D_8C112684[];         /* "FACE 2" */
extern const char D_8C11268C[];         /* "FACE 3" */
extern const char D_8C112694[];         /* "FACE 4" */

/*
 * |d|^2. SHC reassociates the sum: this SHC spelling is the one that compiles
 * to the original FMUL d.y*d.y, FMAC d.x*d.x, FMAC d.z*d.z, i.e. the SH-4
 * computes fma(z, z, fma(x, x, y * y)); the PC spelling is that order.
 */
#if defined(TARGET_PC)
#define SUMSQ3(v) SA1_FMAC((v).z, (v).z, SA1_FMAC((v).x, (v).x, (v).y * (v).y))
#else
#define SUMSQ3(v) SA1_FMAC((v).x, (v).x, SA1_FMAC((v).y, (v).y, (v).z * (v).z))
#endif

/* 8C031A84 (same chunk, BSR): out = a - b rotated by ang around Y */
ASM_STUB(void func_8C031A84(NJS_POINT3 *a, NJS_POINT3 *b, Angle ang, NJS_POINT3 *out))

#ifdef NON_MATCHING
void func_8C032406(colliwk *cwp1, colliwk *cwp2)
{
    NJS_POINT3 p;
    NJS_POINT3 p2;
    Angle3 ang;
    NJS_POINT3 old;
    NJS_VECTOR d;
    NJS_POINT3 q;
    NJS_POINT3 r0;
    NJS_POINT3 r1;
    NJS_POINT3 w;
    NJS_VECTOR s;
    NJS_VECTOR o;
    CCL_INFO *ci1;
    CCL_INFO *ci2;
    taskwk *twp1;
    taskwk *twp2;
    Angle t;
    Float a1;
    Float b1;
    Float a2;
    Float c1;
    Float c2;
    Float b2;
    Float len;
    Float top1;
    Float bot1;
    Float yup;
    Float ylo;
    Float e0;
    Float e1;
    Float x;
    Sint32 n;

    (void)&t;   /* memory-resident in the original (sp+36) */
    ci1 = &cwp1->info[cwp1->test_num];
    ci2 = &cwp2->info[cwp2->test_num];
    a1 = ci1->a;
    a2 = ci2->a;
    b1 = ci1->b;
    b2 = ci2->b;
    c1 = ci1->c;
    c2 = ci2->c;
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    GetPlayerPosition(cwp1->mytask->twp->counter.b[0], 1, &old, NULL);
    d.x = twp1->pos.x - old.x;
    d.y = twp1->pos.y - old.y;
    d.z = twp1->pos.z - old.z;
    len = njSqrt(SUMSQ3(d));
    if (len > 0.0f) {
        d.x /= len;
        d.y /= len;
        d.z /= len;
        d.x *= 2.5f;
        d.y *= 2.5f;
        d.z *= 2.5f;
    } else {
        len = 0.0099999998f;
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
    if ((ci2->attr & 0x20) || (ci2->attr & 0x2000)) {
        ang.y = ci2->angy;
    } else {
        ang.y = twp2->ang.y + ci2->angy;
    }
    ang.z = ci2->angz;
    ang.x = (Uint16)(ang.z + 0x2000);
    if (ang.x >= 0x4000 && ang.x < 0x8000) {
        a2 = ci2->b;
        b2 = ci2->a;
        ang.z = ang.z - 0x4000;
    } else if (ang.x >= 0x8000 && ang.x < 0xC000) {
        ang.z = ang.z - 0x8000;
    } else if (ang.x >= 0xC000 && ang.x <= 0xFFFF) {
        a2 = ci2->b;
        b2 = ci2->a;
        ang.z = ang.z - 0xC000;
    }
    while (n > 0) {
        if (--n > 0) {
            p.x += d.x;
            p.y += d.y;
            p.z += d.z;
        } else {
            p.x = twp1->pos.x;
            p.y = twp1->pos.y + b1;
            p.z = twp1->pos.z;
        }
        func_8C031A84(&p, &p2, -ang.y, &q);
        if (q.z > -a1 - c2 && a1 + c2 > q.z) {
            njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
            if ((t = ang.z) != 0) {
                njRotateZ(NULL, t);
            }
            w.x = -a2 - c1;
            w.y = b2;
            w.z = 0.0f;
            njCalcPoint(NULL, &w, &r0);
            w.x = a2 + c1;
            w.y = b2;
            w.z = 0.0f;
            njCalcPoint(NULL, &w, &r1);
            njPopMatrix(1);
            top1 = q.y + b1;
            bot1 = q.y - b1;
            yup = SA1_FMAC(njTan(ang.z), q.x - r0.x, r0.y);
            ylo = njTan(ang.z) * (q.x + r0.x) - r0.y;
            x = q.x;
            if (q.z > -c1 - c2 && c1 + c2 > q.z && x > r0.x && r1.x > x
                && bot1 > yup + -3.0f && bot1 <= yup) {
                if (tcrf_CollisionDisplayVar >= 0) {
                    njPrintC(0x0014000F, D_8C112640);
                }
                if (!(cwp1->flag & 0x4000)) {
                    func_8C02F8A0(twp1, cwp1, twp2, cwp2);
                }
                func_8C02FA20(cwp1, cwp2);
                twp2->flag |= 0x10;
                cwp1->flag |= 0x10;
                cwp2->flag |= 0x4;
                if (func_8C02FB3A(cwp1, ci1, cwp2, ci2)) {
                    twp1->pos.y += yup - q.y + b1;
                    twp1->ang.x = 0;
                    twp1->ang.z = 0;
                    twp1->flag &= ~0x8;
                    twp1->flag |= 0x20;
                }
                SetPlayerPosition(cwp1->mytask->twp->counter.b[0], 0, &twp1->pos, NULL);
                return;
            } else if (x > -r1.x && -r0.x > x && ylo <= top1 && top1 <= ylo + 3.0f) {
                if (tcrf_CollisionDisplayVar >= 0) {
                    njPrintC(0x0014000F, D_8C112654);
                }
                if (!(cwp1->flag & 0x4000)) {
                    func_8C02F8A0(twp1, cwp1, twp2, cwp2);
                }
                func_8C02FA20(cwp1, cwp2);
                twp2->flag |= 0x20;
                cwp1->flag |= 0x4;
                cwp2->flag |= 0x10;
                if (func_8C02FB3A(cwp1, ci1, cwp2, ci2)) {
                    if (!(twp1->flag & 0x1)) {
                        twp1->pos.y += ylo - q.y - b1;
                    }
                    twp1->flag &= ~0x8;
                    twp1->flag |= 0x10;
                }
                SetPlayerPosition(cwp1->mytask->twp->counter.b[0], 0, &twp1->pos, NULL);
                return;
            } else {
                njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
                if ((t = -ang.z) != 0) {
                    njRotateZ(NULL, t);
                }
                njCalcVector(NULL, &q, &s);
                njPopMatrix(1);
                if (s.x > -a1 - a2 && a1 + a2 > s.x && a1 + b2 + -4.0f > s.y && s.y > 2.0f - a1 - b2) {
                    func_8C02F8A0(twp1, cwp1, twp2, cwp2);
                    func_8C02FA20(cwp1, cwp2);
                    cwp1->flag |= 0x4000;
                    cwp1->flag |= 0x8;
                    cwp2->flag |= 0x8;
                    if (func_8C02FB3A(cwp1, ci1, cwp2, ci2)) {
                        e0 = (a1 + a2) * s.z - (a1 + c2) * s.x;
                        e1 = (a1 + a2) * s.z + (a1 + c2) * s.x;
                        if (tcrf_CollisionDisplayVar >= 0) {
                            njPrintC(0x0014000F, D_8C112668);
                        }
                        if (e0 > 0.0f) {
                            if (e1 > 0.0f) {
                                o.x = 0.0f;
                                o.y = 0.0f;
                                o.z = (a1 + c2) * 0.99899997f - s.z;
                                if (tcrf_CollisionDisplayVar >= 0) {
                                    njPrintC(0x00140010, D_8C11267C);
                                }
                            } else {
                                o.x = (-a1 - a2) * 0.99899997f - s.x;
                                o.y = 0.0f;
                                o.z = 0.0f;
                                if (tcrf_CollisionDisplayVar >= 0) {
                                    njPrintC(0x00140010, D_8C112684);
                                }
                            }
                        } else if (0.0f > e1) {
                            o.x = 0.0f;
                            o.y = 0.0f;
                            o.z = (-a1 - c2) * 0.99899997f - s.z;
                            if (tcrf_CollisionDisplayVar >= 0) {
                                njPrintC(0x00140010, D_8C11268C);
                            }
                        } else {
                            o.x = (a1 + a2) * 0.99899997f - s.x;
                            o.y = 0.0f;
                            o.z = 0.0f;
                            if (tcrf_CollisionDisplayVar >= 0) {
                                njPrintC(0x00140010, D_8C112694);
                            }
                        }
                        njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
                        if (ang.z != 0) {
                            njRotateZ(NULL, ang.z);
                        }
                        if (ang.y != 0) {
                            njRotateY(NULL, ang.y);
                        }
                        njCalcVector(NULL, &o, &q);
                        njPopMatrix(1);
                        twp1->pos.x = SA1_FMAC(q.x, 0.29999999f, twp1->pos.x);
                        if (!(twp1->flag & 0x1)) {
                            twp1->pos.y = SA1_FMAC(q.y, 0.29999999f, twp1->pos.y);
                        }
                        twp1->pos.z = SA1_FMAC(q.z, 0.29999999f, twp1->pos.z);
                    }
                    SetPlayerPosition(cwp1->mytask->twp->counter.b[0], 0, &twp1->pos, NULL);
                    return;
                }
            }
        }
    }
}
#else
ASM_STUB(void func_8C032406(colliwk *cwp1, colliwk *cwp2))
#endif /* NON_MATCHING */
