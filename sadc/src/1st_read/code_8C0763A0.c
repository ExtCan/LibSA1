#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C0763A0): wall slide.
 *  func_8C0763A0  called by the wall-hit response 8C0747D2 with the movement
 *                 direction v (only when pwp->flag & 3 or the player is
 *                 faster than p.jog_speed). Intersects the wall plane
 *                 (cp->onpoly, cp->normal2) with the plane through the
 *                 player (cp->pos) whose normal is v x normal2 (cp->tnorm
 *                 when that is too short) using func_8C64016C (a
 *                 njDistancePL2PL-like helper that returns the intersection
 *                 line), points the line direction away from v and redirects
 *                 the analog speed cp->anaspdh (and the motionwk speed when
 *                 last_attr & 0x800000) along it, keeping the magnitude.
 *                 Then turns twp->ang.y by the angle between cp->spd and
 *                 the new speed (about cp->tnorm), flips it by 0x8000 when
 *                 it ends up more than 0x6000 away from mwp->ang_aim.y, and
 *                 copies it to ang_aim.y.
 *
 * Matching: pwp and cp are declared register; without it SHC gives twp R13
 * and pwp R12 (original: cp R14, pwp/mwp R13, twp R12).
 */

void func_8C0763A0(taskwk *twp, register playerwk *pwp, register csts *cp, NJS_POINT3 *v)
{
    NJS_PLANE wall;
    NJS_PLANE side;
    NJS_LINE line;
    NJS_VECTOR nrm;
    NJS_VECTOR dir;
    NJS_VECTOR spd;
    motionwk *mwp;
    NJS_POINT3 *p;
    Float sc;

    if (!(pwp->flag & 3) && !(njScalor(&pwp->spd) > pwp->p.jog_speed)) {
        return;
    }
    if (njOuterProduct(v, &cp->normal2, &nrm) == 0.0f) {
        return;
    }
    if (!(njScalor(&nrm) > 0.149999994f)) {
        nrm = cp->tnorm;
    } else {
        njUnitVector(&nrm);
    }
    wall.px = cp->onpoly.x;
    wall.py = cp->onpoly.y;
    wall.pz = cp->onpoly.z;
    wall.vx = cp->normal2.x;
    wall.vy = cp->normal2.y;
    wall.vz = cp->normal2.z;
    side.px = cp->pos.x;
    side.py = cp->pos.y;
    side.pz = cp->pos.z;
    side.vx = nrm.x;
    side.vy = nrm.y;
    side.vz = nrm.z;
    func_8C64016C(&wall, &side, &line);
    dir = *v;
    if (njScalor(&dir) != 0.0f) {
        njUnitVector(&dir);
    }
    if (njInnerProduct((NJS_VECTOR *)&line.vx, &dir) > 0.0f) {
        line.vx = -line.vx;
        line.vy = -line.vy;
        line.vz = -line.vz;
    }
    if (!(pwp->flag & 3)) {
        p = &cp->anaspdh;
    } else {
        p = &cp->spd;
    }
    sc = njScalor(p);
    cp->anaspdh.x = sc * line.vx;
    cp->anaspdh.y = sc * line.vy;
    cp->anaspdh.z = sc * line.vz;
    if (pwp->last_attr & 0x800000) {
        mwp = playermwp[(Uint8)twp->counter.b[0]];
        sc = njScalor(&mwp->spd);
        mwp->spd.x = sc * line.vx;
        mwp->spd.y = sc * line.vy;
        mwp->spd.z = sc * line.vz;
    }
    dir = cp->spd;
    if (njScalor(&dir) > 0.19999999f) {
        spd = cp->anaspdh;
        njUnitVector(&dir);
        njUnitVector(&spd);
        twp->ang.y -= VectorAngle(&dir, &spd, &cp->tnorm);
    }
    if (DiffAngle(playermwp[(Uint8)twp->counter.b[0]]->ang_aim.y, twp->ang.y) > 0x6000) {
        twp->ang.y += 0x8000;
    }
    playermwp[(Uint8)twp->counter.b[0]]->ang_aim.y = twp->ang.y;
}
