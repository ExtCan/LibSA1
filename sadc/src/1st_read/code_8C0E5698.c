#include "sa1/sa1.h"

/*
 * Big (1ST_READ 8C0E5698-8C0E59C0, functions 28-29 of chunk 8C0DFFA0).
 *  func_8C0E5698  (BigMain) keeps the held object (pwp->htp) in Big's hands.
 *                 Without a 0x200 object in contact (func_8C06C880): when
 *                 Big touches something other than the held object, drop
 *                 it (8C0E55E2) and, once nothing is held, stand (8C0E65BE)
 *                 or fall (mode 8, action 14). With one, while the held
 *                 object requests it (TWF_REQUEST): its position becomes
 *                 Big's collision centre plus the midpoint of the two hand
 *                 weld positions (relative to the object's own collision
 *                 centre; y only for class 4), pushed out to the sum of the
 *                 two collision radii while TWF_HOLD, rotated by Big's
 *                 angles (z, x, 0x8000 - y).
 *  func_8C0E58F6  buoyancy: vertical acceleration from the depth below the
 *                 water surface (shadow.y_top) times D_8C525DF8 (x 0.9 when
 *                 the surface is above the collision centre) plus a random
 *                 part, capped at D_8C525DFC.
 */

/* Big: release the held object (8C0E55E2, same chunk, BSR; code_8C0E5166.c) */
ASM_STUB(void func_8C0E55E2(taskwk *twp, motionwk *mwp, playerwk *pwp))
/* Big: back to standing (8C0E65BE, other chunk, JSR) */
void func_8C0E65BE(taskwk *twp, motionwk *mwp, playerwk *pwp);

extern Float D_8C525DF8;       /* buoyancy per unit of depth */
extern Float D_8C525DFC;       /* buoyancy cap */

#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): 268 of 268 instructions,
 * only 8 differ, all temporary registers: the hit test loads twp->cwp,
 * hit_cwp, mytask and mytask->twp into R0, R3, R1, R0 in the original
 * (CMP/EQ R0,R12), R3, R2, R1, R3 here; the htwp->id == 4 block loads
 * htwp->cwp / info into R3 / R2 in the original, R2 / R3 here. Same
 * unresolved R0 chain as the siblings Sonic 8C0B1DE0 (code_8C0B1C56.c),
 * Tails 8C0BD4CC (code_8C0BD4CC.c) and Amy 8C0DC7DE (code_8C0DC7DE.c),
 * whose notes list the forms already tried; a colliwk local (Amy's best)
 * gives R4, R3, R1, R0 but swaps FR13/FR14 in the njSqrt argument (11
 * differ). Stack frame (mwp spilled to @0 and shared with the parked
 * rotation angle, out @4, v @16), control flow, the FMAC and the
 * rotations match. Needed: htwp assigned before info, htwp on the left of
 * the compare, if/else for the stand/fall choice, and the quotient
 * fabsf(c) / d in a Float local (FMOV FR2,FR5 / FMOV FR2,FR4; the inline
 * r * (fabsf(c) / d) divides in place).
 */
void func_8C0E5698(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    NJS_VECTOR out;
    CCL_INFO *info;
    taskwk *htwp;
    Float r;
    Float d;
    Float a;

    htwp = pwp->htp->twp;
    info = CWP(twp)->info;
    if (!(func_8C06C880(twp) & 0x200)) {
        if (CWP(twp)->hit_cwp == NULL) {
            return;
        }
        if (htwp == CWP(twp)->hit_cwp->mytask->twp) {
            return;
        }
        func_8C0E55E2(twp, mwp, pwp);
        if (pwp->htp != NULL) {
            return;
        }
        if (twp->flag & 3) {
            func_8C0E65BE(twp, mwp, pwp);
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 14;
        }
        return;
    }
    if (!(htwp->flag & TWF_REQUEST)) {
        return;
    }
    htwp->pos = info->center;
    v.x = (pwp->righthand_pos.x + pwp->lefthand_pos.x) * 0.5f;
    v.y = (pwp->righthand_pos.y + pwp->lefthand_pos.y) * 0.5f;
    v.z = (pwp->righthand_pos.z + pwp->lefthand_pos.z) * 0.5f;
    v.x -= CWP(htwp)->info->center.x;
    if (htwp->id == 4) {
        v.y -= CWP(htwp)->info->center.y;
    }
    v.z -= CWP(htwp)->info->center.z;
    if (twp->flag & TWF_HOLD) {
        r = info->a + CWP(htwp)->info[1].a;
        d = njSqrt(SA1_FMAC(v.x, v.x, v.y * v.y));
        if (r > d) {
            if (pwp->righthand_pos.y > 9.0f) {
                a = _builtin_fabsf(v.x) / d;
                v.x = -(r * a);
                a = _builtin_fabsf(v.y) / d;
                v.y = r * a;
                if (v.y > 13.0f) {
                    v.y = 13.0f;
                }
            } else {
                v.x = -njSqrt(r * r - v.y * v.y);
            }
        }
    }
    njPushMatrix(D_8C67C640);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    njCalcVector(NULL, &v, &out);
    njPopMatrix(1);
    njAddVector(&htwp->pos, &out);
}
#else
ASM_STUB(void func_8C0E5698(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C0E58F6(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float acc;

    acc = (pwp->shadow.y_top - twp->pos.y) * D_8C525DF8;
    if (pwp->shadow.y_top > CWP(twp)->info->center.y) {
        acc *= 0.9f;
    }
    /* FMAC: SHC fuses "acc += k * x" into acc's register; the SA1_FMAC
     * spelling (k * x + acc) computes into a temporary (one more FMOV) */
    SH_ONLY(acc += 3.0517578e-9f * (Float)rand();)
    PC_ONLY(acc = SA1_FMAC(3.0517578e-9f, (Float)rand(), acc);)
    /* FCMP/GT acc,cap; BT: the cap also replaces a NaN acc */
    if (!(D_8C525DFC > acc)) {
        acc = D_8C525DFC;
    }
    SetAccelerationP(twp->counter.b[0], 0.0f, acc, 0.0f);
}
