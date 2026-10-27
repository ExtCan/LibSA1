#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * CCL (object-vs-object collision) 8C0344A0-8C03490C: release of the
 * collision work and the push-out direction of the shape that was hit.
 */

extern void func_8C02FC12(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C02FD32(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C02FE5C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
extern void func_8C031A84(NJS_POINT3 *a, NJS_POINT3 *b, Angle ang, NJS_POINT3 *out);

extern const char D_8C1126A4[];         /* "side1" */
extern const char D_8C1126AC[];         /* "side2" */
extern const char D_8C1126B4[];         /* "side3" */
extern const char D_8C1126BC[];         /* "side4" */

/* free twp->cwp (and its CCL_INFO copy when flag 0x20 says it owns one) */
void func_8C0344A0(taskwk *twp)
{
    colliwk *cwp;

    cwp = CWP(twp);
    if (cwp != NULL) {
        if ((cwp->flag & 0x20) && cwp->info != NULL) {
            MFree(cwp->info);
            cwp->info = NULL;
        }
        MFree(cwp);
        twp->cwp = NULL;
    }
}

/*
 * Unit push-out direction from the shape hit by twp's first shape
 * (cwp->hit_cwp->info[cwp->hit_num]) towards twp's shape: forms 0 (sphere)
 * and 5 in 3D, forms 1-3 in the XZ plane, form 4 (box) one of the four
 * side normals of the box ("side1".."side4"). Returns 0 when there is no
 * valid hit.
 *
 * NON_MATCHING: complete and functionally faithful (12 FMAC = 12 SA1_FMAC on
 * PC: 2 per squared length x 5 forms + 2 in the debug line; the original
 * wrote the squared lengths as x*x + y*y + z*z and the debug line as
 * p2.x + v->x * 15, which SA1_FMAC cannot spell, hence SH_ONLY/PC_ONLY).
 * Same instructions as the original except register allocation:
 *  - SHC keeps the address of func_8C02FD32 (4 calls) in R8 (extra
 *    push/pop) where the original reloads it from the pool for every call.
 *    It is the lowest-priority register candidate: any extra value live from
 *    the first call through the switch (e.g. a dead "if (0) njPrintC(..)"
 *    after the switch, which hoists njPrintC's address to the switch head)
 *    makes SHC reload it exactly like the original, but no such source was
 *    found that leaves no other trace. Not it: casts/(*f)() on some calls
 *    (a cast call forms its own CSE class, but 2-call classes in the cases
 *    are still kept in R8), (void) calls, nested ifs / merged early returns,
 *    block-scope or extra locals, register keywords, local pointer to
 *    tcrf_CollisionDisplayVar, case/default order, inline helpers, dead
 *    blocks with otherwise unused functions or variables.
 *  - With the four calls renamed to distinct externs (no CSE, as in the
 *    original) the rest matches except: EXTS.W R6,R2 instead of R6,R3 for
 *    the hit_num test; form 0/1 load v->x/y/z into a different permutation
 *    of FR4-FR6; form 4 puts side/njPrintC/0x320032 in R13/R9/R10 instead of
 *    R9/R10/R13. These look like allocator ordering effects that may follow
 *    from the func_8C02FD32 decision.
 * Matching aids kept from that analysis: a1 declared and loaded before a2
 * (FR13/FR12 and the prologue load order); f1/f2 locals for zz -/+ xx (zz
 * kept in FR4, FLDI0 after the multiplies); the dead "if (0)" use of a2,
 * which SHC keeps through register allocation and then drops (as in
 * func_8C031D00), so a1 + a2 goes to a new register instead of in place in
 * FR12; the rotation written as the ternary (no temporary: SHC itself keeps
 * side - ang in the stack slot at @(0,R15)).
 */
#ifdef NON_MATCHING
Sint8 func_8C0344DC(taskwk *twp, NJS_POINT3 *v)
{
    NJS_POINT3 p1;
    NJS_POINT3 p2;
    NJS_VECTOR d;
    colliwk *cwp;
    colliwk *hcwp;
    CCL_INFO *info;
    CCL_INFO *hinfo;
    taskwk *htwp;
    Float a1;
    Float a2;
    Float c2;
    Float f1;
    Float f2;
    Float zz;
    Float xx;
    Angle ang;
    Angle side;
    Sint16 num;

    cwp = CWP(twp);
    hcwp = cwp->hit_cwp;
    num = cwp->hit_num;
    info = cwp->info;
    if (hcwp == NULL) {
        return 0;
    }
    if (num > hcwp->nbInfo) {
        return 0;
    }
    hinfo = &hcwp->info[num];
    if (info == NULL) {
        return 0;
    }
    /* PC: the original tests the address info + num * 52 itself; clang folds
     * "&p[i] == NULL" to "p == NULL", so compare the address as an integer */
    if (SH_ONLY(hinfo == NULL) PC_ONLY((SA1_UPTR)hcwp->info + (SA1_UPTR)(num * (Sint32)sizeof(CCL_INFO)) == 0)) {
        return 0;
    }
    a1 = info->a;
    a2 = hinfo->a;
    c2 = hinfo->c;
    htwp = hcwp->mytask->twp;
    func_8C02FD32(twp, info, &p1);
    switch (hinfo->attr & 0xF) {
    case 0:
        func_8C02FD32(htwp, hinfo, &p2);
        v->x = p1.x - p2.x;
        v->y = p1.y - p2.y;
        v->z = p1.z - p2.z;
        if (SH_ONLY(v->x * v->x + v->y * v->y + v->z * v->z) PC_ONLY(SA1_FMAC(v->z, v->z, SA1_FMAC(v->x, v->x, v->y * v->y))) != 0.0f) {
            njUnitVector(v);
        } else {
            v->x = 0.0f;
            v->y = 0.0f;
            v->z = 1.0f;
        }
        break;
    case 1:
        func_8C02FD32(htwp, hinfo, &p2);
        v->x = p1.x - p2.x;
        v->y = 0.0f;
        v->z = p1.z - p2.z;
        if (SH_ONLY(v->x * v->x + v->y * v->y + v->z * v->z) PC_ONLY(SA1_FMAC(v->z, v->z, SA1_FMAC(v->x, v->x, v->y * v->y))) != 0.0f) {
            njUnitVector(v);
        } else {
            v->x = 0.0f;
            v->y = 0.0f;
            v->z = 1.0f;
        }
        break;
    case 2:
        func_8C02FC12(htwp, hinfo, &p2);
        v->x = p1.x - p2.x;
        v->y = 0.0f;
        v->z = p1.z - p2.z;
        if (SH_ONLY(v->x * v->x + v->y * v->y + v->z * v->z) PC_ONLY(SA1_FMAC(v->z, v->z, SA1_FMAC(v->x, v->x, v->y * v->y))) != 0.0f) {
            njUnitVector(v);
        } else {
            v->x = 0.0f;
            v->y = 0.0f;
            v->z = 1.0f;
        }
        break;
    case 3:
        func_8C02FC12(htwp, hinfo, &p2);
        v->x = p1.x - p2.x;
        v->y = 0.0f;
        v->z = p1.z - p2.z;
        if (SH_ONLY(v->x * v->x + v->y * v->y + v->z * v->z) PC_ONLY(SA1_FMAC(v->z, v->z, SA1_FMAC(v->x, v->x, v->y * v->y))) != 0.0f) {
            njUnitVector(v);
        } else {
            v->x = 0.0f;
            v->y = 0.0f;
            v->z = 1.0f;
        }
        break;
    case 4:
        p1.x = twp->pos.x;
        p1.y = twp->pos.y;
        p1.z = twp->pos.z;
        func_8C02FE5C(htwp, hinfo, &p2);
        if ((hinfo->attr & 0x20) || (hinfo->attr & 0x2000)) {
            ang = -hinfo->angy;
        } else {
            ang = -htwp->ang.y - hinfo->angy;
        }
        func_8C031A84(&p1, &p2, ang, &d);
        zz = (a1 + a2) * d.z;
        if (0) {
            v->x = a2;  /* dead: keeps a2 (FR12) live past the sum, see above */
        }
        xx = (a1 + c2) * d.x;
        f1 = zz - xx;
        f2 = zz + xx;
        if (f1 > 0.0f) {
            if (f2 > 0.0f) {
                side = 0;
                if (tcrf_CollisionDisplayVar >= 0) {
                    njPrintC(0x320032, D_8C1126A4);
                }
            } else {
                side = 0xC000;
                if (tcrf_CollisionDisplayVar >= 0) {
                    njPrintC(0x320032, D_8C1126AC);
                }
            }
        } else {
            if (f2 > 0.0f) {
                side = 0x4000;
                if (tcrf_CollisionDisplayVar >= 0) {
                    njPrintC(0x320032, D_8C1126B4);
                }
            } else {
                side = 0x8000;
                if (tcrf_CollisionDisplayVar >= 0) {
                    njPrintC(0x320032, D_8C1126BC);
                }
            }
        }
        d.x = 0.0f;
        d.y = 0.0f;
        d.z = 1.0f;
        njPushMatrix(D_8C67C640);
        (side - ang) ? njRotateY(NULL, side - ang) : (void)0;
        njCalcVector(NULL, &d, v);
        njPopMatrix(1);
        break;
    case 5:
        func_8C02FD32(htwp, hinfo, &p2);
        v->x = p1.x - p2.x;
        v->y = 0.0f;
        v->z = p1.z - p2.z;
        if (SH_ONLY(v->x * v->x + v->y * v->y + v->z * v->z) PC_ONLY(SA1_FMAC(v->z, v->z, SA1_FMAC(v->x, v->x, v->y * v->y))) != 0.0f) {
            njUnitVector(v);
        } else {
            v->x = 0.0f;
            v->y = 0.0f;
            v->z = 1.0f;
        }
        break;
    default:
        return 0;
    }
    if (tcrf_CollisionDisplayVar >= 0) {
        func_8C043A40(p2.x, p2.y, p2.z,
                      SH_ONLY(p2.x + v->x * 15.0f) PC_ONLY(SA1_FMAC(v->x, 15.0f, p2.x)), p2.y,
                      SH_ONLY(p2.z + v->z * 15.0f) PC_ONLY(SA1_FMAC(v->z, 15.0f, p2.z)));
    }
    return 1;
}
#else
ASM_STUB(Sint8 func_8C0344DC(taskwk *twp, NJS_POINT3 *v))
#endif
