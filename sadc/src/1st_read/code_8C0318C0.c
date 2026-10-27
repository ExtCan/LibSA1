#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * CCL (object-vs-object collision) 8C0318C0-8C031CBE: box (form with a/b/c
 * half extents and a Y rotation) helpers and the sphere-vs-box pair test.
 */

extern void func_8C02AE78(Float *p0, Float *p1, Float *p2, Float *n);
extern void func_8C02FC12(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C02FE5C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C02F27C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C02F8A0(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2);
extern void func_8C02FA20(colliwk *cwp1, colliwk *cwp2);

/*
 * 1 when pos lies inside the box shape cwp2->info[cwp2->test_num] (x/z half
 * extents a/c grown by r, rotated by its angy + the owner's ang.y, centred
 * on center): every edge of the rectangle seen from pos turns the same way
 * (func_8C02AE78 normal of each edge triangle has n.y > 0).
 * Matching notes: r1 = ci1->a is dead; SHC removes that load but keeps the
 * ci1 address computation (single-pass dead-code removal), like the
 * original. n1/n2 locals give n2 its stack slot (stored, reloaded). The
 * rotation is a ?: expression (sum in a stack slot, stored in the delay
 * slot). The func_8C02AE78 arguments must be whole-struct pointers: &p.x
 * makes SHC keep &p in a register for the whole function.
 */
Sint32 func_8C0318C0(colliwk *cwp1, NJS_POINT3 *pos, colliwk *cwp2, NJS_POINT3 *center, Float r)
{
    NJS_POINT3 pt[4];
    NJS_POINT3 tmp[4];
    NJS_POINT3 p;
    NJS_VECTOR n;
    taskwk *twp;
    CCL_INFO *ci;
    CCL_INFO *ci1;
    Float r1;
    Float a;
    Float c;
    Uint16 n1;
    Uint16 n2;
    Uint8 i;

    twp = cwp2->mytask->twp;
    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    ci1 = &cwp1->info[n1];
    ci = &cwp2->info[n2];
    r1 = ci1->a;
    PC_ONLY((void)r1;)
    a = ci->a;
    c = ci->c;
    p.x = pos->x;
    p.y = pos->y;
    p.z = pos->z;
    pt[0].x = r + a;
    pt[0].y = p.y;
    pt[0].z = r + c;
    pt[1].x = r + a;
    pt[1].y = p.y;
    pt[1].z = -r - c;
    pt[2].x = -r - a;
    pt[2].y = p.y;
    pt[2].z = -r - c;
    pt[3].x = -r - a;
    pt[3].y = p.y;
    pt[3].z = r + c;
    njPushMatrix(D_8C67C640);
    (ci->angy + twp->ang.y) != 0 ? njRotateY(NULL, ci->angy + twp->ang.y) : (void)0;
    for (i = 0; i < 4; i++) {
        njCalcPoint(NULL, &pt[i], &tmp[i]);
        pt[i].x = center->x + tmp[i].x;
        pt[i].y = p.y;
        pt[i].z = center->z + tmp[i].z;
    }
    njPopMatrix(1);
    for (i = 0; i < 4; i++) {
        func_8C02AE78((Float *)&p, (Float *)&pt[i], (Float *)&pt[(i + 1) % 4], (Float *)&n);
        if (!(n.y > 0.0f)) {
            return 0;
        }
    }
    return 1;
}
/* out = a - b rotated by ang around Y */
void func_8C031A84(NJS_POINT3 *a, NJS_POINT3 *b, Angle ang, NJS_POINT3 *out)
{
    NJS_VECTOR v;
    NJS_VECTOR w;
    Angle t;
    NJS_POINT3 *o;

    (void)&t;   /* memory-resident in the original (stored at sp+0, reloaded) */
    t = ang;
    o = out;
    v.x = a->x - b->x;
    v.y = a->y - b->y;
    v.z = a->z - b->z;
    njPushMatrix(D_8C67C640);
    t != 0 ? njRotateY(NULL, t) : (void)0;
    njCalcVector(NULL, &v, &w);
    njPopMatrix(1);
    o->x = w.x;
    o->y = w.y;
    o->z = w.z;
}

/* v = v rotated by -ang around Y, plus b (inverse of func_8C031A84; a unused) */
void func_8C031B20(NJS_POINT3 *a, NJS_POINT3 *b, Angle ang, NJS_VECTOR *v)
{
    NJS_MATRIX m;
    NJS_VECTOR w;
    Angle t1;
    Angle t2;
    NJS_VECTOR *vp;

    (void)&t2;  /* both angles are memory-resident in the original */
    (void)&t1;
    t1 = ang;
    vp = v;
    njUnitMatrix((NJS_MATRIX_PTR)&m);
    (t2 = -t1) != 0 ? njRotateY((NJS_MATRIX_PTR)&m, t2) : (void)0;
    njCalcVector((NJS_MATRIX_PTR)&m, vp, &w);
    vp->x = w.x + b->x;
    vp->y = w.y + b->y;
    vp->z = w.z + b->z;
}

/*
 * Sphere shape cwp1 (radius a) vs box shape cwp2 (half extents a/b/c,
 * rotated around Y by angy, plus the owner's ang.y unless attr has 0x20 or
 * 0x2000). The Y ranges are compared first; then the sphere centre is moved
 * into the box frame (func_8C031A84 with -ang.y) and the X/Z ranges are
 * tested. On a hit: 8C02F8A0/8C02FA20 record it and cwp1->flag |= 0x4000.
 * Returns 100.0f.
 * Matching notes: ang.z is stored in both branches (SHC tail-merges it,
 * leaving "store; BRA; NOP"); y must be declared before r1 (operand order of
 * y + b2) and only p2.y goes through it.
 */
Float func_8C031B88(colliwk *cwp1, colliwk *cwp2)
{
    NJS_POINT3 p1;
    NJS_POINT3 p2;
    NJS_POINT3 d;
    Angle3 ang;
    CCL_INFO *ci1;
    CCL_INFO *ci2;
    taskwk *twp1;
    taskwk *twp2;
    Float y;
    Float r1;
    Float a2;
    Float b2;
    Float c2;
    Float bot1;
    Float top1;
    Float bot2;
    Float top2;

    ci1 = &cwp1->info[cwp1->test_num];
    ci2 = &cwp2->info[cwp2->test_num];
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    r1 = ci1->a;
    a2 = ci2->a;
    b2 = ci2->b;
    c2 = ci2->c;
    func_8C02FC12(twp1, ci1, &p1);
    func_8C02FE5C(twp2, ci2, &p2);
    top1 = p1.y + r1;
    bot1 = p1.y - r1;
    y = p2.y;
    top2 = y + b2;
    bot2 = y - b2;
    if ((ci2->attr & 0x20) || (ci2->attr & 0x2000)) {
        ang.y = ci2->angy;
        ang.z = ci2->angz;
    } else {
        ang.y = twp2->ang.y + ci2->angy;
        ang.z = ci2->angz;
    }
    if (bot2 < top1 && bot1 < top2) {
        func_8C031A84(&p1, &p2, -ang.y, &d);
        if (tcrf_CollisionDisplayVar >= 0) {
            func_8C02F27C(twp2, ci2, &p2);
        }
        if (-a2 < d.x + r1 && d.x - r1 < a2) {
            if (-c2 < d.z + r1 && d.z - r1 < c2) {
                func_8C02F8A0(twp1, cwp1, twp2, cwp2);
                func_8C02FA20(cwp1, cwp2);
                cwp1->flag |= 0x4000;
            }
        }
    }
    return 100.0f;
}