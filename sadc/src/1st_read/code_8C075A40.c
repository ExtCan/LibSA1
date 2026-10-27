/*
 * Player common (1ST_READ 8C075A40-8C075F8C): wall touches of the
 * PSetPosition core 8C0730D8 (called for an entry of the gathered land
 * collision list, with the entry's slAttribute as the 5th argument).
 *
 * func_8C075A40 walks the touches found by the sphere/polygon test 8C029B00
 * (B_8C6BACF0[cp->find_count]). A touch is a wall candidate when its normal
 * is more than 0x2C00 away from the floor normal (cp->tnorm), it is not more
 * "floor-like" with respect to gravity than tnorm and the land object has no
 * 0x4000 attribute; a candidate is counted (flag 8) when the move direction
 * is within 0x600 (8.4 deg) of head-on into it or the speed into it exceeds
 * 4.5, and the nearest counted one (distance of its push-back point from the
 * sphere centre) is kept. When the speed is longer than that distance and
 * the last surface had no 0x4000 attribute, cp->anaspdh is rescaled to the
 * distance, the touch normal is stored in pwp->wall_normal and:
 *  - head-on (VectorAngle(-mwp->spd, wall) <= 0x1000): at crash speed or
 *    more cp->anaspdh is cleared and 2 is returned; otherwise mwp->spd is
 *    cut to 0.2 * jog speed when faster than the jog speed and 1 is returned;
 *  - else mwp->spd is redirected (same length) along the line where the wall
 *    plane meets the plane through cp->pshbk spanned by cp->anaspdh and the
 *    wall normal (njDistancePL2PL-like 8C64016C), cp->anaspdh = mwp->spd and
 *    twp->ang.y / mwp->ang_aim.y follow it. This path has no return
 *    statement: the original returns whatever R0 holds (see the end).
 * Returns 0 when nothing was done.
 *
 * Matching: the two SH_ONLY self-assignments in the loop only steer SHC's
 * FP register allocation (mindist FR15, d FR14, e FR13, constant 0.0f FR12);
 * without them the code is identical except that those four registers are
 * permuted. They generate no code and are absent on PC.
 */
#include "sa1/sa1.h"

/* one touch of the sphere/polygon test (B_8C6BACF0[], stride 0x58); same
 * layout as TOUCHINFO in code_8C029B00.c (not in a shared header yet) */
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

extern TOUCHINFO B_8C6BACF0[];  /* touches of the last 8C029B00 call (16) */

Sint32 func_8C075A40(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp, Uint32 attr)
{
    NJS_PLANE wall;
    NJS_PLANE side;
    NJS_LINE cross;
    NJS_VECTOR nrm;
    NJS_VECTOR nspd;
    NJS_VECTOR sum;
    NJS_VECTOR sum2;
    Float dist[16];
    Sint32 flags[16];
    NJS_VECTOR v;
    Sint32 best;
    Uint32 rot;
    Sint32 count;
    TOUCHINFO *tp;
    Sint32 i;
    Angle ang;
    Float mindist;
    Float d;
    Float e;
    Float s;
    PC_ONLY(Sint32 ret;)

    count = 0;
    best = -1;
    mindist = 1000.0f;
    sum.x = sum.y = sum.z = 0.0f;
    sum2 = sum;                 /* dead copy, as in the original */
    rot = attr & 0x4000;
    for (i = 0; i < cp->find_count; i++) {
        tp = &B_8C6BACF0[i];
        dist[i] = njDistanceP2P(&tp->pshbk, &cp->pos);
        flags[i] = 0;           /* flags[] is never read back */
        ang = VectorAngle(&cp->tnorm, &tp->normal2, NULL);
        if (ang <= 0x2C00
            || njInnerProduct(&Gravity, &cp->tnorm) > njInnerProduct(&Gravity, &tp->normal2)
            || rot) {
            flags[i] |= 2;
        } else {
            v = cp->spd;
            if (njScalor(&v) != 0.0f) {
                njUnitVector(&v);
            }
            if ((d = njInnerProduct(&v, &tp->normal2)) < 0.0f) {
                SH_ONLY(d = d;)         /* register allocation, see header */
                e = njInnerProduct(&cp->spd, &tp->normal2);
                if (d < -njCos(0x600) || e < -4.5f) {
                    flags[i] |= 8;
                    count++;
                    SH_ONLY(mindist = mindist;) /* register allocation */
                    if (dist[i] < mindist) {
                        mindist = dist[i];
                        best = i;
                    }
                }
                njAddVector(&sum, &tp->normal2);
            }
        }
    }
    if (count <= 0) {
        return 0;
    }
    s = njScalor(&cp->spd);
    if (!(s > dist[best]) || (pwp->last_attr & 0x4000)) {
        return 0;
    }
    if (njScalor(&cp->anaspdh) == 0.0f) {
        cp->anaspdh.x = cp->anaspdh.y = cp->anaspdh.z = 0.0f;
        return 0;
    } else {
        njUnitVector(&cp->anaspdh);
    }
    cp->anaspdh.x *= dist[best];
    cp->anaspdh.y *= dist[best];
    cp->anaspdh.z *= dist[best];
    tp = &B_8C6BACF0[best];
    pwp->wall_normal = tp->normal2;
    nspd.x = -mwp->spd.x;
    nspd.y = -mwp->spd.y;
    nspd.z = -mwp->spd.z;
    if (njScalor(&nspd) == 0.0f) {
        return 0;
    }
    njUnitVector(&nspd);
    ang = VectorAngle(&nspd, &pwp->wall_normal, NULL);
    if (ang <= 0x1000) {
        if (pwp->spd.x >= pwp->p.crash_speed) {
            cp->anaspdh.x = cp->anaspdh.y = cp->anaspdh.z = 0.0f;
            return 2;
        }
        if (pwp->spd.x > pwp->p.jog_speed) {
            s = pwp->p.jog_speed * 0.19999999f;
            njUnitVector(&mwp->spd);
            mwp->spd.x *= s;
            mwp->spd.y *= s;
            mwp->spd.z *= s;
        }
        return 1;
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
    wall.vx = pwp->wall_normal.x;
    wall.vy = pwp->wall_normal.y;
    wall.vz = pwp->wall_normal.z;
    side.px = cp->pshbk.x;
    side.py = cp->pshbk.y;
    side.pz = cp->pshbk.z;
    side.vx = nrm.x;
    side.vy = nrm.y;
    side.vz = nrm.z;
    func_8C64016C(&wall, &side, &cross);
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
    twp->ang.y = SA1_FTOI(atan2f(cross.vz, cross.vx) * 10430.381f) & 0xFFFF;
    if ((PC_ONLY(ret =) DiffAngle(mwp->ang_aim.y, twp->ang.y)) > 0x6000) {
        twp->ang.y += 0x8000;
        PC_ONLY(ret = twp->ang.y;)
    }
    mwp->ang_aim.y = twp->ang.y;
    /* no return statement: the original returns whatever R0 holds (the
     * DiffAngle result, or the new twp->ang.y); the caller tests it for 0 */
    PC_ONLY(return ret;)
}
