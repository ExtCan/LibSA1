/*
 * Amy (1ST_READ chunk 8C0DCBA0-8C0DDA00, functions 7-9 of 12): the skirt
 * task created by AmyMain (8C0D6B40, case 0: CreateElementalTask(3, 3,
 * func_8C0DD16E)) and the dest functions of the two part tasks.
 *
 *  func_8C0DD166  dest of the skirt task: put the rest model D_8C4C72D4 back
 *                 into the skirt node D_8C4C72FC.
 *  func_8C0DD16E  exec: while Amy's current action uses her model tree
 *                 (AmyActionList[reqaction].actptr->object == &Amy), swings
 *                 the skirt: the swing speed (mwp->spd.y) follows the
 *                 player's speed (2 * |spd|, at most 6), decays by 0.99 per
 *                 frame, the phase (mwp->ang_spd.z) advances 0x800 per frame
 *                 and twp->ang.z = cos(phase) * swing * 819.2. The vertices
 *                 of D_8C4C72D4 are rotated by that angle about two pivots
 *                 into amy_z, which D_8C4C72FC then displays.
 *  func_8C0DD488  dest of the second part task (func_8C0DD490): put
 *                 D_8C4BFEA4 back into the node D_8C4BFECC.
 */
#include "common.h"
#include "sa1/sa1.h"

/* Amy data (extern only) */
extern NJS_OBJECT Amy;                  /* 8C4C7C3C root of Amy's model tree */
extern NJS_MODEL amy_z[];               /* 8C4CAF6C skirt model drawn (vertices rewritten here) */
extern NJS_MODEL D_8C4C72D4[];          /* skirt rest model (source vertices) */
extern NJS_OBJECT D_8C4C72FC[];         /* skirt node of the tree (model = D_8C4C72D4 / amy_z) */
extern NJS_MODEL D_8C4BFEA4;            /* rest model of the second part (func_8C0DD490) */
extern NJS_OBJECT D_8C4BFECC[];         /* node of the second part */

/* bss */
extern NJS_MATRIX B_8C7A4018;           /* skirt rotation (twp->ang.z) */
extern NJS_MATRIX B_8C7A4058;           /* half rotation (twp->ang.z >> 1); built but unused here */

void func_8C0DD166(task *tp)
{
    D_8C4C72FC->model = D_8C4C72D4;
}

/*
 * Matching notes (func_8C0DD16E, NON_MATCHING; the C is complete):
 *  - Literal pool sharing: SHC shares one literal pool across the functions
 *    of a translation unit. The original TU also held func_8C0DD03C and
 *    func_8C0DD0DE (code_8C0DCBA0.c); their pending pool entries
 *    (D_8C15C7F4 .. AdjustAngle) make SHC dump the first pool right after
 *    the switch's default BRA (8C0DD1EC), which pushes the second pool to
 *    8C0DD400 and makes the loop exit an out-of-range BF/S + BRA pair. In
 *    this file the pools land elsewhere, so the branch layout cannot match
 *    until these functions share a file with the preceding ones. With a
 *    stand-in function placed before func_8C0DD166 that has the same pool
 *    pressure, all but 14 instructions match.
 *  - Remaining differences (also seen in the sister func_8C0DD490):
 *    the first switch BT is not given a delay slot in the original (here
 *    "MOV.L @R4,R14" is moved into BT/S); case 0 gets R3 = func, R2 = 1
 *    (here R2/R3 swapped); the phase step keeps 0x800 in R4 and the value in
 *    R5 (here swapped). Tried: statement orders, if/ternary clamps,
 *    0x800 + x, a split add, default-first and case-order changes, a mode
 *    local, pno-free prelude, an enclosing if for the action test.
 *  - "i = 0" before the loop (not in the for) gives the original preheader
 *    schedule. Both njCalcVector calls use B_8C7A4018 (two registers, R10
 *    and R9, both holding it): the half-angle matrix B_8C7A4058 is built
 *    but never used, apparently a slip in the original.
 */
#ifdef NON_MATCHING
void func_8C0DD16E(task *tp)
{
    Sint32 a;
    NJS_VECTOR v;
    taskwk *twp;
    motionwk *mwp;
    motionwk *pmwp;
    playerwk *pwp;
    NJS_POINT3 *src;
    NJS_POINT3 *dst;
    Float *m1;
    Float *m2;
    Uint32 n;
    Uint32 i;
    Uint8 pno;
    Float s;

    twp = tp->twp;
    mwp = tp->mwp;
    if (playertwp[(Uint8)twp->counter.b[0]] == NULL) {
        DestroyTask(tp);
        return;
    }
    pno = twp->counter.b[0];
    pmwp = playermwp[pno];
    pwp = GetPlayerWorkPtr(pno);
    dst = amy_z->points;
    if (AmyActionList[pwp->mj.reqaction].actptr->object != &Amy) {
        return;
    }
    n = D_8C4C72D4->nbPoint;
    src = D_8C4C72D4->points;
    switch (twp->mode) {
    case 0:
        twp->mode = 1;
        tp->dest = func_8C0DD166;
        break;
    case 1:
        s = njSqrt(SH_ONLY(pmwp->spd.x * pmwp->spd.x + pmwp->spd.y * pmwp->spd.y + pmwp->spd.z * pmwp->spd.z)
                   PC_ONLY(SA1_FMAC(pmwp->spd.z, pmwp->spd.z, SA1_FMAC(pmwp->spd.x, pmwp->spd.x, pmwp->spd.y * pmwp->spd.y)))) * 2.0f;
        if (s > _builtin_fabsf(mwp->spd.y)) {
            if (s < 0.0f) {
                s = -s;
            }
            if (s > 6.0f) {
                s = 6.0f;
            }
            mwp->spd.y = s;
        }
        if (mwp->spd.y >= 0.19999999f || mwp->spd.y <= -0.19999999f) {
            mwp->spd.y = 0.98999996f * mwp->spd.y;
        } else {
            mwp->spd.y = 0.0f;
        }
        a = SA1_FTOI(_builtin_fabsf(mwp->spd.y * 512.0f)) + 0x800;
        if (a > 0x800) {
            a = 0x800;
        }
        mwp->ang_spd.z += a;
        s = njCos(mwp->ang_spd.z);
        twp->ang.z = SA1_FTOI(s * mwp->spd.y * 819.19997f);
        njUnitMatrix(B_8C7A4018);
        twp->ang.z ? njRotateZ(B_8C7A4018, twp->ang.z) : (void)0;
        njUnitMatrix(B_8C7A4058);
        twp->ang.z >> 1 ? njRotateZ(B_8C7A4058, twp->ang.z >> 1) : (void)0;
        i = 0;
        m1 = (Float *)&B_8C7A4018;
        m2 = (Float *)&B_8C7A4018;
        for (; i < n; i++, src++, dst++) {
            if (!(src->y < 0.19999999f)) {
                v.x = src->x + 1.0f;
                v.y = src->y + 3.0f;
                v.z = src->z;
                njCalcVector(m1, &v, dst);
                dst->x -= 1.0f;
                dst->y -= 3.0f;
            } else if (src->y < -3.1999999f && _builtin_fabsf(src->z) > 1.69999996f) {
                v.x = src->x + 0.29999999f;
                v.y = src->y + 1.19999996f;
                v.z = src->z;
                njCalcVector(m2, &v, dst);
                dst->x -= 0.29999999f;
                dst->y -= 1.19999996f;
            }
        }
        break;
    default:
        DestroyTask(tp);
        break;
    }
    D_8C4C72FC->model = amy_z;
}
#else
ASM_STUB(void func_8C0DD16E(task *tp))
#endif

void func_8C0DD488(task *tp)
{
    D_8C4BFECC->model = &D_8C4BFEA4;
}
