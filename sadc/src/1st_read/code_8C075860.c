/*
 * Player land collision (1ST_READ 8C075860-8C075A40): touch selection.
 *
 * Called by the land collision cores 8C0730D8 and 8C0777E0 after the sphere
 * vs polygon pass (8C029B00) with (twp, mwp, pwp, cp); only cp is used.
 * For every touch found (cp->find_count entries of B_8C6BACF0) it measures
 * the distance from the pushed-back centre to cp->pos. A touch whose normal
 * is within 0x2C00 of the current floor normal (cp->tnorm), or whose normal
 * points more against gravity than the floor normal, counts as "floor-like"
 * (flag 2) and the nearest one is kept; the others are marked as walls hit
 * head-on (flag 8) when the movement direction faces them (dot < -0.2).
 * If any floor-like touch exists and the move is longer than the distance to
 * the nearest one, cp->spd is clamped to that distance and 1 is returned.
 * The flag array is only written (never read back). Unused: twp, mwp, pwp.
 */

#include "sa1/sa1.h"

/* one touch (B_8C6BACF0[], stride 0x58); same layout as TOUCHINFO in
 * code_8C029B00.c, to be moved to sa1/collision.h */
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
SA1_ASSERT_OFFSET(TOUCHINFO, pshbk, 0x2C);

extern TOUCHINFO B_8C6BACF0[];          /* touches found by 8C029B00 */

Sint32 func_8C075860(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp)
{
    Float dist[16];
    Uint32 flags[16];
    NJS_VECTOR v;
    Sint32 best;
    Sint32 count;
    Angle ang;
    Float mindist;
    Float limit;
    Float len;
    Sint32 i;
    TOUCHINFO *tp;

    count = 0;
    best = -1;
    mindist = 1000.0f;
    limit = -0.19999999f;
    for (i = 0; i < cp->find_count; i++) {
        tp = &B_8C6BACF0[i];
        dist[i] = njDistanceP2P(&tp->pshbk, &cp->pos);
        flags[i] = 0;
        ang = VectorAngle(&cp->tnorm, &tp->normal2, NULL);
        if (ang <= 0x2C00
            || njInnerProduct(&Gravity, &cp->tnorm) > njInnerProduct(&Gravity, &tp->normal2)) {
            flags[i] |= 2;
            count++;
            if (dist[i] < mindist) {
                mindist = dist[i];
                best = i;
            }
        } else {
            v = cp->spd;
            if (njScalor(&v) != 0.0f) {
                njUnitVector(&v);
            }
            if (njInnerProduct(&v, &tp->normal2) < limit) {
                flags[i] |= 8;
            }
        }
    }
    if (count > 0) {
        /* best stays -1 when every floor-like touch is 1000 or more away;
         * the original then reads dist[-1] (flags[15] or stack garbage) */
        PC_ONLY(if (best < 0) { return 0; })
        len = njScalor(&cp->spd);
        if (len > dist[best]) {
            if (njScalor(&cp->spd) == 0.0f) {
                cp->spd.x = cp->spd.y = cp->spd.z = 0.0f;
            } else {
                njUnitVector(&cp->spd);
                cp->spd.x *= dist[best];
                cp->spd.y *= dist[best];
                cp->spd.z *= dist[best];
            }
            return 1;
        }
    }
    return 0;
}
