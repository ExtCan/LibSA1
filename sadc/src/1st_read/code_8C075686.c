#include "sa1/sa1.h"

/*
 * Player land collision (1ST_READ 8C075686-8C075860): wall touch selection
 * of the alternate PSetPosition core 8C0777E0 (called there as
 * (twp, mwp, pwp, cp, attr); only cp is used).
 *
 * For every touch found by 8C029B00 (cp->find_count entries of B_8C6BACF0)
 * it measures the distance from the pushed-back centre to cp->pos. A touch
 * whose normal is within 0x2C00 of the floor normal (cp->tnorm), or points
 * more against gravity than the floor normal, is floor-like (flag 2) and
 * skipped. The others count as walls hit (flag 8) when their normal is less
 * than 0x7000 away from the floor normal and the movement direction faces
 * them (dir . normal2 < 0); the nearest of those is kept. If a wall was hit
 * and the move is longer than the distance to it, cp->spd is clamped to that
 * distance and 1 is returned, else 0. The flag array is only written.
 *
 * Original translation unit: this function follows func_8C075416
 * (code_8C0751C0.c). SHC dumps 8C075416's pending literal pool (njUnitVector,
 * njInnerProduct, 0x800000, njCos, njAddVector) after this function's first
 * BRA (8C0756B8); that does not change the instructions compared here.
 */

/* one touch (B_8C6BACF0[], stride 0x58); same layout as TOUCHINFO in
 * code_8C029B00.c / code_8C0751C0.c / code_8C075860.c, to be moved to
 * sa1/collision.h. No pointers: DC layout on every host. */
typedef struct {
    Angle angx;                 /* 0x00 */
    Angle angz;                 /* 0x04 */
    NJS_VECTOR normal;          /* 0x08 triangle normal */
    NJS_VECTOR normal2;         /* 0x14 normal facing the sphere (read here) */
    NJS_POINT3 onpoly;          /* 0x20 centre projected on the plane */
    NJS_POINT3 pshbk;           /* 0x2C centre pushed out to the radius (read here) */
    NJS_VECTOR anaspdh;         /* 0x38 */
    NJS_VECTOR anaspdv;         /* 0x44 */
    Float dist;                 /* 0x50 */
    Float dist2;                /* 0x54 */
} TOUCHINFO;                    /* 0x58 */

SA1_ASSERT_SIZE(TOUCHINFO, 0x58);
SA1_ASSERT_OFFSET(TOUCHINFO, normal2, 0x14);
SA1_ASSERT_OFFSET(TOUCHINFO, pshbk, 0x2C);

extern TOUCHINFO B_8C6BACF0[];          /* touches found by 8C029B00 (16) */

#ifdef NON_MATCHING
/*
 * Status: complete and functionally faithful; 2 spots differ (shcdiff with
 * -define=NON_MATCHING: 199 vs 198 instructions because of one NOP, all
 * others identical apart from the shift it causes).
 *  - The original loop is not inverted (entry BRA to the test at the bottom,
 *    no guard). SHC 5.1 only produces that shape here with a `continue` in
 *    the loop (a plain for/while loop gets a guard test; for(;;)+break and
 *    goto change the register allocation). With the continues the shape,
 *    registers and pool placement match, but:
 *  - the flag-2 store `MOV.L R2,@(R0,R13)` stays before the BRA to the loop
 *    increment with a NOP in the delay slot (original: in the slot), and
 *  - the 12-byte copy v = cp->spd uses R3/R2/R1 (original: R0 as the
 *    destination, R1/R3 as data, njScalor's address loaded into R2 between
 *    the stores).
 *    Without any continue (plain for loop) both of these match, so they are a
 *    side effect of how SHC compiles loops with continue; the same pattern is
 *    unresolved in func_8C075F8C (code_8C075F8C.c). Tried: |= 2 in an
 *    if/else with continue elsewhere, a trailing continue, two separate ifs,
 *    goto, for(;;)+break, while, unsigned test, block-scoped v, field-wise
 *    copy, signed flags, `flags[i] = flags[i] | 2` (fills the slot but uses
 *    a pointer and OR #2,R0), and compiling after the real 8C0751C0-8C075416
 *    bodies (reproduces the original pool placement, not these two spots).
 */
Sint32 func_8C075686(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp, Uint32 attr)
{
    Float dist[16];
    Uint32 flags[16];
    NJS_VECTOR v;
    Sint32 sel;
    Sint32 count;
    TOUCHINFO *ti;
    Float min;
    Float len;
    Sint32 ang;
    Sint32 i;

    PC_ONLY((void)twp; (void)mwp; (void)pwp; (void)attr;)
    count = 0;
    sel = -1;
    min = 1000.0f;
    for (i = 0; i < cp->find_count; i++) {
        ti = &B_8C6BACF0[i];
        dist[i] = njDistanceP2P(&ti->pshbk, &cp->pos);
        flags[i] = 0;
        ang = VectorAngle(&cp->tnorm, &ti->normal2, NULL);
        if (ang <= 0x2C00 ||
            njInnerProduct(&Gravity, &cp->tnorm) > njInnerProduct(&Gravity, &ti->normal2)) {
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
        if (njInnerProduct(&v, &ti->normal2) < 0.0f) {
            flags[i] |= 8;
            count++;
            if (dist[i] < min) {
                min = dist[i];
                sel = i;
            }
        }
    }
    if (count > 0) {
        /* sel stays -1 when every wall hit is 1000 or more away; the original
         * then reads dist[-1] (the bits of flags[15] or stack garbage) */
        PC_ONLY(if (sel < 0) { return 0; })
        len = njScalor(&cp->spd);
        if (len <= dist[sel]) {
            return 0;
        }
        if (njScalor(&cp->spd) == 0.0f) {
            cp->spd.x = cp->spd.y = cp->spd.z = 0.0f;
        } else {
            njUnitVector(&cp->spd);
            cp->spd.x *= dist[sel];
            cp->spd.y *= dist[sel];
            cp->spd.z *= dist[sel];
        }
        return 1;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C075686(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp, Uint32 attr))
#endif
