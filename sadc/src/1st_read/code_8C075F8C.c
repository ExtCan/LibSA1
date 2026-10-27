/*
 * Player land collision (1ST_READ 8C075F8C-8C0763A0): wall slide along the
 * touched polygons (called by the land collision core 8C0777E0).
 *
 * For every touch found by the sphere/polygon test 8C029B00
 * (cp->find_count entries of B_8C6BACF0) it measures the distance from the
 * pushed-back centre to cp->pos. A touch whose normal is within 0x2C00 of
 * the floor normal cp->tnorm, or points more upward than it, is floor-like
 * (flag 2). For the others that are not nearly opposite (angle < 0x7000),
 * the movement direction faces the wall (dot < 0) and either faces it
 * steeply (dot < -cos(0x600)) or the speed into it exceeds 4.5, the touch
 * is a wall hit (flag 8) and the nearest one is kept. If a wall was hit,
 * the move is longer than the distance to it and the player is not on
 * 0x4000 ground: when cp->anaspdh is zero it is cleared and 0 returned;
 * otherwise cp->anaspdv is added to cp->anaspdh, the wall normal is stored
 * in pwp->wall_normal and (unless mwp->spd is zero) mwp->spd is redirected,
 * keeping its length, along the line where the wall plane meets the plane
 * through the pushed-back centre spanned by the wall normal and anaspdh
 * (func_8C64016C, plane/plane intersection); the new speed is copied to
 * cp->anaspdh. The flag array is only written (never read back).
 *
 * Return value: 0 when nothing was redirected. The redirect path has no
 * return statement: R0 still holds 12 (the offset of the last FMOV.S into
 * mwp->spd), and 8C0777E0 passes that value on as its own result, so the
 * PC build returns 12 explicitly. The caller (8C0777E0) pushes a fifth
 * stack argument that this function never reads.
 */

#include "sa1/sa1.h"

/* one touch (B_8C6BACF0[], stride 0x58); same layout as TOUCHINFO in
 * code_8C029B00.c / code_8C075860.c / code_8C075A40.c, to be moved to
 * sa1/collision.h */
typedef struct {
    Angle angx;                 /* 0x00 */
    Angle angz;                 /* 0x04 */
    NJS_VECTOR normal;          /* 0x08 triangle normal */
    NJS_VECTOR normal2;         /* 0x14 normal facing the sphere */
    NJS_POINT3 onpoly;          /* 0x20 centre projected on the plane */
    NJS_POINT3 pshbk;           /* 0x2C centre pushed out to the radius */
    NJS_VECTOR anaspdh;         /* 0x38 */
    NJS_VECTOR anaspdv;         /* 0x44 */
    Float dist;                 /* 0x50 */
    Float dist2;                /* 0x54 */
} TOUCHINFO;                    /* 0x58 */

SA1_ASSERT_SIZE(TOUCHINFO, 0x58);
SA1_ASSERT_OFFSET(TOUCHINFO, normal2, 0x14);
SA1_ASSERT_OFFSET(TOUCHINFO, onpoly, 0x20);
SA1_ASSERT_OFFSET(TOUCHINFO, pshbk, 0x2C);

extern TOUCHINFO B_8C6BACF0[];          /* touches found by 8C029B00 */

#ifdef NON_MATCHING
/*
 * Not matching, for two reasons:
 *  1. Literal pool. 8C075F8C shares its translation unit with 8C075A40
 *     (before) and 8C0763A0 (after). The pending pool entries of 8C075A40
 *     make SHC dump the shared pool right after the BRA at 8C07601E; with
 *     no preceding function (or only an ASM_STUB of it) the pool lands
 *     after 8C076150 instead, and the loop back-edge and the early returns
 *     become BF instead of BT/BRA. Compiled in one file after the C of
 *     func_8C075A40 (code_8C075A40.c) the pool, the stack frame, the loop
 *     shape and the rest of the function are identical to the original.
 *  2. In that one-file build exactly one spot remains: the original puts
 *     the store of "flags[i] |= 2" in the delay slot of the BRA to the loop
 *     increment (8C07601E/20) and the following v = cp->spd copy uses
 *     R1/R0/R3; here the store stays before the BRA (NOP in the slot) and
 *     the copy uses R2/R3/R1 (448 vs 447 instructions). The loop shape (BRA
 *     to the test at the bottom, i*4 kept in R13, mwp spilled to @R15)
 *     needs a "continue"; every continue/goto/while/for variant tried gives
 *     the NOP, while a plain if/else without continue fills the slot but
 *     guards the loop instead. The "else" around the anaspdh additions and
 *     the "continue" for ang >= 0x7000 are what the register allocation of
 *     the original needs (FR1/FR2 for the additions, FR3 afterwards).
 */
Sint32 func_8C075F8C(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp)
{
    NJS_PLANE wall;
    NJS_PLANE move;
    NJS_LINE cross;
    NJS_VECTOR nrm;
    NJS_VECTOR back;
    Float dist[16];
    Uint32 flags[16];
    NJS_VECTOR v;
    Sint32 best;
    Sint32 count;
    Angle ang;
    Float mindist;
    Float d;
    Float d2;
    Float s;
    Sint32 i;
    TOUCHINFO *tp;

    count = 0;
    best = -1;
    mindist = 1000.0f;
    for (i = 0; i < cp->find_count; i++) {
        tp = &B_8C6BACF0[i];
        dist[i] = njDistanceP2P(&tp->pshbk, &cp->pos);
        flags[i] = 0;
        ang = VectorAngle(&cp->tnorm, &tp->normal2, 0);
        if (ang <= 0x2C00 || cp->tnorm.y < tp->normal2.y) {
            flags[i] |= 2;
            continue;
        }
        v = cp->spd;
        if (njScalor(&v) != 0.0f) {
            njUnitVector(&v);
        }
        if (ang >= 0x7000) {
            continue;
        }
        if ((d = njInnerProduct(&v, &tp->normal2)) < 0.0f) {
            d2 = njInnerProduct(&cp->spd, &tp->normal2);
            if (d < -njCos(0x600) || d2 < -4.5f) {
                flags[i] |= 8;
                count++;
                if (dist[i] < mindist) {
                    mindist = dist[i];
                    best = i;
                }
            }
        }
    }
    if (count <= 0) {
        return 0;
    }
    /* count > 0 with best == -1 needs every wall hit 1000 or more away; the
     * original then reads dist[-1] (the stack slot below dist). */
    PC_ONLY(if (best < 0) { return 0; })
    s = njScalor(&cp->spd);
    if (!(s > dist[best])) {
        return 0;
    }
    if (pwp->last_attr & 0x4000) {
        return 0;
    }
    if (njScalor(&cp->anaspdh) == 0.0f) {
        cp->anaspdh.x = cp->anaspdh.y = cp->anaspdh.z = 0.0f;
        return 0;
    } else {
        cp->anaspdh.x += cp->anaspdv.x;
        cp->anaspdh.y += cp->anaspdv.y;
        cp->anaspdh.z += cp->anaspdv.z;
    }
    tp = &B_8C6BACF0[best];
    pwp->wall_normal = tp->normal2;
    back.x = -mwp->spd.x;
    back.y = -mwp->spd.y;
    back.z = -mwp->spd.z;
    if (njScalor(&back) == 0.0f) {
        return 0;
    }
    njOuterProduct(&cp->anaspdh, &pwp->wall_normal, &nrm);
    if (njScalor(&nrm) == 0.0f) {
        nrm = cp->tnorm;
    } else {
        njUnitVector(&nrm);
    }
    wall.px = tp->onpoly.x;
    wall.py = tp->onpoly.y;
    wall.pz = tp->onpoly.z;
    wall.vx = tp->normal2.x;
    wall.vy = tp->normal2.y;
    wall.vz = tp->normal2.z;
    move.px = cp->pshbk.x;
    move.py = cp->pshbk.y;
    move.pz = cp->pshbk.z;
    move.vx = nrm.x;
    move.vy = nrm.y;
    move.vz = nrm.z;
    func_8C64016C(&wall, &move, &cross);
    v = cp->anaspdh;
    if (njScalor(&v) != 0.0f) {
        njUnitVector(&v);
    }
    if (njInnerProduct((NJS_VECTOR *)&cross.vx, &v) < 0.0f) {
        cross.vx = -cross.vx;
        cross.vy = -cross.vy;
        cross.vz = -cross.vz;
    }
    s = njScalor(&mwp->spd);
    mwp->spd.x = cross.vx * s;
    mwp->spd.y = cross.vy * s;
    mwp->spd.z = cross.vz * s;
    cp->anaspdh = mwp->spd;
    PC_ONLY(return 12;)
}
#else
ASM_STUB(Sint32 func_8C075F8C(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp))
#endif
