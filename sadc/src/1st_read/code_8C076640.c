/*
 * Player common (1ST_READ 8C076640-8C076782): turn the player along a wall
 * while being pushed sideways (called by the land collision step 8C074AFC).
 *
 * func_8C076640 runs when the player is airborne/flagged (pwp->flag & 3) or
 * moving faster than jog speed. If the probe vector v is not parallel to the
 * surface normal cp->normal2 and the collision has a horizontal analog speed
 * (cp->anaspdh), the yaw is turned by the angle between the collision speed
 * and that horizontal speed (around cp->tnorm), flipped by 0x8000 when it
 * points away from the aim angle (DiffAngle > 0x6000), and copied into the
 * motion aim angle. When |anaspdh| >= 0.4 the motion speed is redirected
 * along anaspdh, keeping its magnitude (scaled by 0.96 above 0.6).
 */
#include "sa1/sa1.h"

void func_8C076640(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *v)
{
    NJS_POINT3 dir;
    NJS_POINT3 spd;
    NJS_POINT3 hspd;
    motionwk *mwp;
    Float s;

    if ((pwp->flag & 3) || njScalor(&pwp->spd) > pwp->p.jog_speed) {
        if (njOuterProduct(v, &cp->normal2, &dir) == 0.0f) {
            return;
        }
        spd = cp->spd;
        hspd = cp->anaspdh;
        if (njScalor(&hspd) == 0.0f) {
            return;
        }
        njUnitVector(&spd);
        njUnitVector(&hspd);
        twp->ang.y -= VectorAngle(&spd, &hspd, &cp->tnorm);
        if (DiffAngle(playermwp[(Uint8)twp->counter.b[0]]->ang_aim.y, twp->ang.y) > 0x6000) {
            twp->ang.y += 0x8000;
        }
        playermwp[(Uint8)twp->counter.b[0]]->ang_aim.y = twp->ang.y;
        dir = cp->anaspdh;
        if (njScalor(&dir) < 0.39999998f) {
            return;
        }
        mwp = playermwp[(Uint8)twp->counter.b[0]];
        s = njScalor(&mwp->spd);
        if (s > 0.59999997f) {
            s *= 0.96f;
        }
        njUnitVector(&dir);
        mwp->spd.x = dir.x * s;
        mwp->spd.y = dir.y * s;
        mwp->spd.z = dir.z * s;
    }
}
