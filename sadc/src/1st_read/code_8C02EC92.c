/*
 * CCL debug display, part 2 (8C02EC92-8C02F27C): draws a collision shape
 * (CCL_INFO) with the debug models. Called per shape form by
 * func_8C02F27C(twp, info, pos) (form = info->attr & 0xF).
 *  func_8C02EC92  form 3: capsule (cylinder + two caps), full rotation
 *  func_8C02EE9E  form 6: box with a direction arrow (y rotation only)
 *  func_8C02F0A0  form 7: circle of radius info->a around twp->pos
 *                 (32 debug line segments)
 */
#include "sa1/sa1.h"

/* declared as an array: SHC keeps the address of an array in a register
 * (R8 in func_8C02EE9E) but reloads &scalar from the pool at every use */
extern NJS_MODEL Debug_Cube_1[];            /* 8C199B6C */
extern NJS_MODEL Debug_Capsule_Cap;         /* 8C19A4BC */
extern NJS_MODEL Debug_Capsule_Cylinder;    /* 8C19A704 */

/* debug line between two points (code_8C043A20.c) */
extern void func_8C043AEE(NJS_POINT3 *v1, NJS_POINT3 *v2);

/* set the debug draw color from info->flag & 3 (code_8C02E820.c) */
ASM_STUB(void func_8C02E820(CCL_INFO *info))

/* form 3: capsule */
void func_8C02EC92(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos)
{
    Float r;
    Float h;

    r = info->a * 0.099999996f;
    h = info->b * 0.099999996f;
    njPushMatrix(NULL);
    njTranslate(NULL, pos->x, pos->y, pos->z);
    if (info->attr & 0x40) {
        if (!(info->attr & 0x20) && !(info->attr & 0x2000)) {
            twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        }
        info->angy ? njRotateY(NULL, info->angy) : (void)0;
        info->angz ? njRotateZ(NULL, info->angz) : (void)0;
        info->angx ? njRotateX(NULL, info->angx) : (void)0;
    } else {
        if (!(info->attr & 0x20) && !(info->attr & 0x2000)) {
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
            twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        }
        info->angz ? njRotateZ(NULL, info->angz) : (void)0;
        info->angx ? njRotateX(NULL, info->angx) : (void)0;
        info->angy ? njRotateY(NULL, info->angy) : (void)0;
    }
    func_8C02E820(info);
    njPushMatrix(NULL);
    njScale(NULL, r, h, r);
    func_8C6215D6(&Debug_Capsule_Cylinder);
    njPopMatrix(1);
    njPushMatrix(NULL);
    njTranslate(NULL, 0.0f, info->b, 0.0f);
    njScale(NULL, r, r, r);
    func_8C6215D6(&Debug_Capsule_Cap);
    njPopMatrix(1);
    njPushMatrix(NULL);
    njTranslate(NULL, 0.0f, -info->b, 0.0f);
    njScale(NULL, r, r, r);
    njRotateX(NULL, 0x8000);
    func_8C6215D6(&Debug_Capsule_Cap);
    njPopMatrix(1);
    njPopMatrix(1);
}

/* form 6: box with a direction arrow */
void func_8C02EE9E(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos)
{
    Float r;
    Float h;
    Float two;

    r = info->a * 0.099999996f;
    h = info->b * 0.099999996f;
    njPushMatrix(NULL);
    njTranslate(NULL, pos->x, pos->y, pos->z);
    if ((info->attr & 0x20) || (info->attr & 0x2000)) {
        info->angy ? njRotateY(NULL, info->angy) : (void)0;
    } else {
        (twp->ang.y + info->angy) ? njRotateY(NULL, twp->ang.y + info->angy) : (void)0;
    }
    njPushMatrix(NULL);
    njTranslate(NULL, 0.0f, 0.0f, 10.0f);
    two = 2.0f;
    njScale(NULL, 0.099999996f, 0.099999996f, two);
    func_8C02E820(info);
    func_8C6215D6(Debug_Cube_1);
    njPopMatrix(1);
    njPushMatrix(NULL);
    njTranslate(NULL, 0.0f, 0.0f, 20.0f);
    njRotateX(NULL, 0x2000);
    njTranslate(NULL, 0.0f, 0.0f, -3.0f);
    njScale(NULL, 0.099999996f, 0.099999996f, 0.7f);
    func_8C02E820(info);
    func_8C6215D6(Debug_Cube_1);
    njPopMatrix(1);
    njPushMatrix(NULL);
    njTranslate(NULL, 0.0f, 0.0f, 20.0f);
    njRotateX(NULL, -0x2000);
    njTranslate(NULL, 0.0f, 0.0f, -3.0f);
    njScale(NULL, 0.099999996f, 0.099999996f, 0.7f);
    func_8C02E820(info);
    func_8C6215D6(Debug_Cube_1);
    njPopMatrix(1);
    njScale(NULL, r * two, h * two, 0.099999996f);
    func_8C02E820(info);
    func_8C6215D6(Debug_Cube_1);
    njPopMatrix(1);
}

/* form 7: circle around twp->pos */
/*
 * NON_MATCHING: 9 differing instructions, all in the scheduling of the
 * prologue (order of the hoisted pool loads, of "a1 = 0x800" (MOV R13,R12)
 * and of "a0 = 0"); registers, frame, loop body (unrolled 4x) and literal
 * pool are identical. Notes from the search:
 *  - the radius must go through a Float variable: v.x = info->a directly
 *    puts it in FR3 and lets FLDI0 FR4 move to the top of the function;
 *  - "a0 = a0 + 0x800" (not "a0 += 0x800") and the cast spelling of the
 *    unit matrix (hoisted into the two spill slots @(4,R15)/@(8,R15));
 *  - the assignment of njCalcPoint/njTranslateV/njPopMatrix/njPushMatrix to
 *    R8-R11 depends on the order of the initial statements: only a0 = 0
 *    first and a1 = 0x800 last gives the original R8-R11; putting a1 = 0x800
 *    before a0 = 0 (or v.y/v.z first) gives the original schedule but swaps
 *    R8/R11 or the store order of v.
 */
#ifdef NON_MATCHING
void func_8C02F0A0(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos)
{
    NJS_POINT3 v;
    NJS_POINT3 p0;
    NJS_POINT3 p1;
    Angle a0;
    Angle a1;
    Float r;

    a0 = 0;
    r = info->a;
    v.x = r;
    v.y = 0.0f;
    v.z = 0.0f;
    a1 = 0x800;
    while (a0 < 0x10000) {
        njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
        njTranslateV(NULL, &twp->pos);
        a0 ? njRotateY(NULL, a0) : (void)0;
        njCalcPoint(NULL, &v, &p0);
        njPopMatrix(1);
        njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
        njTranslateV(NULL, &twp->pos);
        a1 ? njRotateY(NULL, a1) : (void)0;
        njCalcPoint(NULL, &v, &p1);
        njPopMatrix(1);
        func_8C043AEE(&p0, &p1);
        a1 += 0x800;
        a0 = a0 + 0x800;
    }
}
#else
ASM_STUB(void func_8C02F0A0(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos))
#endif
