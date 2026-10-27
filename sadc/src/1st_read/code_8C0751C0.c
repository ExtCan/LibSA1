/*
 * Player common (1ST_READ 8C0751C0-8C075686): land collision helpers used by
 * the PSetPosition family (8C07061C chunk, 8C0730D8).
 *
 * func_8C0751C0  push the player out along the collision normal
 * func_8C075214  surface attributes: physics scaling and acceleration factor
 * func_8C075416  filter the touched polygons of one land object
 */
#include "sa1/sa1.h"

/* one touch (B_8C6BACF0[], stride 0x58; same layout as TOUCHINFO in
 * code_8C029B00.c, which fills it). No pointers: DC layout on every host. */
typedef struct {
    Angle angx;                 /* 0x00 */
    Angle angz;                 /* 0x04 */
    NJS_VECTOR normal;          /* 0x08 triangle normal */
    NJS_VECTOR normal2;         /* 0x14 normal facing the sphere (read here) */
    NJS_POINT3 onpoly;          /* 0x20 */
    NJS_POINT3 pshbk;           /* 0x2C sphere centre pushed out (read here) */
    NJS_VECTOR anaspdh;         /* 0x38 */
    NJS_VECTOR anaspdv;         /* 0x44 */
    Float dist;                 /* 0x50 */
    Float dist2;                /* 0x54 */
} TOUCHINFO;                    /* 0x58 */

SA1_ASSERT_SIZE(TOUCHINFO, 0x58);
SA1_ASSERT_OFFSET(TOUCHINFO, normal2, 0x14);
SA1_ASSERT_OFFSET(TOUCHINFO, pshbk, 0x2C);

extern TOUCHINFO B_8C6BACF0[];          /* touches of the last 8C029B00 query (16) */

/* Pushes the player 3 units out along the collision normal and adds twice
 * the normal to the horizontal speed. */
void func_8C0751C0(taskwk *twp, motionwk *mwp, csts *cp)
{
    /* five FMAC sites (fused on PC via SA1_FMAC_ACC) */
    SA1_FMAC_ACC(twp->pos.x, cp->normal.x, 3.0f);
    SA1_FMAC_ACC(twp->pos.y, cp->normal.y, 3.0f);
    SA1_FMAC_ACC(twp->pos.z, cp->normal.z, 3.0f);
    SA1_FMAC_ACC(mwp->spd.x, cp->normal.x, 2.0f);
    SA1_FMAC_ACC(mwp->spd.z, cp->normal.z, 2.0f);
}

/* Records the surface attribute and scales the physics for water (shadow
 * attribute 2) and 0x400000 surfaces; mwp->weight is the acceleration
 * factor of the surface (8: 0.15, 4: 0.1, 0x10: 0.5, 0x80: 1.5).
 * Water: weight * 0.5 (Amy) or 0.45, air_resist_y * 1.5, run_accel * 0.65;
 * 0x400000: weight * 0.6, air_resist_y * 2, run_accel * 0.45. */
void func_8C075214(taskwk *twp, motionwk *mwp, playerwk *pwp, Uint32 attr)
{
    pwp->attr |= attr;
    if (pwp->shadow.Attr_top & 2) {
        if ((Uint8)twp->counter.b[1] == SA1_CHR_AMY) {
            pwp->p.weight = PhysicsTable[(Uint8)twp->counter.b[1]].weight * 0.5f;
        } else {
            pwp->p.weight = PhysicsTable[(Uint8)twp->counter.b[1]].weight * 0.45f;
        }
        pwp->p.air_resist_y = PhysicsTable[(Uint8)twp->counter.b[1]].air_resist_y * 1.5f;
        pwp->p.run_accel = PhysicsTable[(Uint8)twp->counter.b[1]].run_accel * 0.65f;
    } else if (pwp->shadow.Attr_top & 0x400000) {
        pwp->p.weight = PhysicsTable[(Uint8)twp->counter.b[1]].weight * 0.59999998f;
        pwp->p.air_resist_y = PhysicsTable[(Uint8)twp->counter.b[1]].air_resist_y * 2.0f;
        pwp->p.run_accel = PhysicsTable[(Uint8)twp->counter.b[1]].run_accel * 0.45f;
    } else if (!(pwp->shadow.Attr_top & 2)) {
        pwp->p.weight = PhysicsTable[(Uint8)twp->counter.b[1]].weight;
        pwp->p.air_resist_y = PhysicsTable[(Uint8)twp->counter.b[1]].air_resist_y;
        pwp->p.run_accel = PhysicsTable[(Uint8)twp->counter.b[1]].run_accel;
    }
    if (attr & 8) {
        mwp->weight = 0.149999995f;
    } else if (attr & 4) {
        mwp->weight = 0.099999996f;
    } else if (attr & 0x10) {
        mwp->weight = 0.5f;
    } else if (attr & 0x80) {
        mwp->weight = 1.5f;
    } else {
        mwp->weight = 1.0f;
    }
}

/* Walks the touches of the last polygon query (cp->find_count) for one land
 * object (attr = its _OBJ_LANDCOLL.slAttribute). Returns 1 when the speed was
 * clamped to the distance of the nearest usable touch. A touch is skipped
 * (flag 2) when its normal is within 0x2C00 of cp->tnorm or faces gravity
 * more than tnorm does; a usable touch (flag 8) faces against the speed.
 * twp is unused; flags[] and the normal sum are computed but never read
 * (the original keeps the stores and the njAddVector call). */
Sint32 func_8C075416(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp, Uint32 attr)
{
    Float dist[16];
    Uint32 flags[16];
    NJS_VECTOR sum;
    NJS_VECTOR dir;
    TOUCHINFO *ti;
    Sint32 i;
    Sint32 count;
    Sint32 best;
    Angle ang;
    Float min;
    Float ip;
    Float d;
    Float lim;

    count = 0;
    best = -1;
    min = 1000.0f;
    sum.x = sum.y = sum.z = 0.0f;
    for (i = 0; i < cp->find_count; i++) {
        ti = &B_8C6BACF0[i];
        dist[i] = njDistanceP2P(&ti->pshbk, &cp->pos);
        flags[i] = 0;
        ang = VectorAngle(&cp->tnorm, &ti->normal2, NULL);
        if (ang <= 0x2C00 || njInnerProduct(&Gravity, &cp->tnorm) > njInnerProduct(&Gravity, &ti->normal2)) {
            flags[i] |= 2;
        } else {
            dir = cp->spd;
            if (njScalor(&dir) != 0.0f) {
                njUnitVector(&dir);
            }
            if ((ip = njInnerProduct(&dir, &ti->normal2)) < 0.0f) {
                flags[i] |= 8;
                count++;
                if (dist[i] < min) {
                    min = dist[i];
                    best = i;
                }
                if (!(attr & 0x800000)) {
                    if (ip < -njCos(0x600)) {
                        mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
                    }
                    d = njInnerProduct(&cp->spd, &ti->normal2);
                    if (d < -4.5f) {
                        lim = pwp->p.run_speed;
                        if (njScalor(&mwp->spd) > lim) {
                            njUnitVector(&mwp->spd);
                            mwp->spd.x *= lim;
                            mwp->spd.y *= lim;
                            mwp->spd.z *= lim;
                        }
                    }
                }
                njAddVector(&sum, &ti->normal2);
            }
        }
    }
    if (count > 0) {
        d = njScalor(&cp->spd);
        if (d > dist[best]) {
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
