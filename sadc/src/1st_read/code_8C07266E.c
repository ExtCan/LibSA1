#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C07266E-8C072B44): speed integration variants
 * and the wall slide.
 *  func_8C07266E  pushing an object (8C06C880 & 0x400): X speed integration
 *                 with stop snapping (as func_8C0725C0) clamped to
 *                 +-max_psh_spd, Y added, Z snapped like X; converted to
 *                 global space into mwp->spd and copied (Y = 0) into the
 *                 pushed task's mwp->acc. Otherwise PClearSpeed.
 *  func_8C072806  pwp->spd = pwp->acc, converted into mwp->spd (Knuckles).
 *  func_8C0728A0  pwp->spd += pwp->acc with X kept in [0.15, 3.5] (Knuckles).
 *  func_8C072946  wall slide (PSetPosition): on the ground, moving at least
 *                 1.0 and touching a CCL wall (cwp->flag & 0x80, & 8),
 *                 redirects mwp->spd along the line where the wall plane
 *                 meets the floor plane and turns twp->ang.y with it.
 */

/* object classification of the carried/touched object (code_8C06C880.c) */
Sint32 func_8C06C880(taskwk *twp);
/* CCL hit query: push-out vector from the hit shape (code_8C06CA30.c) */
Sint8 func_8C0344DC(taskwk *twp, NJS_POINT3 *v);

void func_8C0723E0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07256A(motionwk *mwp, playerwk *pwp);

void func_8C07266E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 spd;
    motionwk *hmwp;
    task *tp;
    Float s;

    if (pwp->htp == NULL) {
        tp = CWP(twp)->hit_cwp->mytask;
    } else {
        tp = pwp->htp;
    }
    hmwp = tp->mwp;
    if (!(func_8C06C880(twp) & 0x400)) {
        PClearSpeed(mwp, pwp);
        return;
    }
    s = pwp->spd.x;
    pwp->spd.x += pwp->acc.x;
    if (s > 0.0f) {
        if (pwp->spd.x < 0.0f) {
            pwp->spd.x = 0.0f;
        } else if (s >= 0.099999996f && pwp->spd.x < 0.099999996f) {
            pwp->spd.x = 0.0f;
        }
    } else if (s < 0.0f) {
        if (pwp->spd.x > -0.0f) {
            pwp->spd.x = 0.0f;
        } else if (s <= -0.099999996f && pwp->spd.x > -0.099999996f) {
            pwp->spd.x = 0.0f;
        }
    }
    if (pwp->p.max_psh_spd < pwp->spd.x) {
        pwp->spd.x = pwp->p.max_psh_spd;
    }
    if (pwp->spd.x < -pwp->p.max_psh_spd) {
        pwp->spd.x = -pwp->p.max_psh_spd;
    }
    pwp->spd.y += pwp->acc.y;
    s = pwp->spd.z;
    pwp->spd.z += pwp->acc.z;
    if (s > 0.0f) {
        if (pwp->spd.z < 0.0f) {
            pwp->spd.z = 0.0f;
        } else if (s >= 0.099999996f && pwp->spd.z < 0.099999996f) {
            pwp->spd.z = 0.0f;
        }
    } else if (s < 0.0f) {
        if (pwp->spd.z > -0.0f) {
            pwp->spd.z = 0.0f;
        } else if (s <= -0.099999996f && pwp->spd.z > -0.099999996f) {
            pwp->spd.z = 0.0f;
        }
    }
    spd = pwp->spd;
    PConvertVector_P2G(twp, &spd);
    mwp->spd = spd;
    if (hmwp) {
        hmwp->acc.x = spd.x;
        hmwp->acc.y = 0.0f;
        hmwp->acc.z = spd.z;
    }
    func_8C0723E0(twp, mwp, pwp);
}

void func_8C072806(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 spd;

    pwp->spd = pwp->acc;
    spd = pwp->spd;
    PConvertVector_P2G(twp, &spd);
    mwp->spd = spd;
    func_8C0723E0(twp, mwp, pwp);
}

void func_8C0728A0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 spd;

    pwp->spd.x += pwp->acc.x;
    pwp->spd.y += pwp->acc.y;
    pwp->spd.z += pwp->acc.z;
    if (pwp->spd.x < 1.5f) {
        if (pwp->spd.x < 0.0f) {
            pwp->spd.x = 0.0f;
        }
        pwp->spd.x += 0.149999995f;
    }
    if (pwp->spd.x > 3.5f) {
        pwp->spd.x = 3.5f;
    }
    spd = pwp->spd;
    PConvertVector_P2G(twp, &spd);
    mwp->spd = spd;
    func_8C0723E0(twp, mwp, pwp);
}

void func_8C072946(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 nrm;
    NJS_PLANE wall;
    NJS_PLANE floor;
    NJS_LINE cross;
    NJS_POINT3 v0;
    NJS_POINT3 v1;
    colliwk *cwp;
    Uint32 flag;
    Float s;

    if (!(twp->flag & 3)) {
        return;
    }
    if (njScalor(&mwp->spd) < 1.0f) {
        return;
    }
    cwp = CWP(twp);
    flag = cwp->flag;
    if (!(flag & 0x80) || (twp->flag & 4) || !(flag & 8)) {
        return;
    }
    func_8C0344DC(twp, &nrm);
    pwp->wall_normal = nrm;
    if (njScalor(&nrm) == 0.0f) {
        nrm.x = 0.0f;
        nrm.y = 0.0f;
        nrm.z = 0.0f;
    } else {
        njUnitVector(&nrm);
    }
    wall.px = 0.0f;
    wall.py = 0.0f;
    wall.pz = 0.0f;
    wall.vx = pwp->wall_normal.x;
    wall.vy = pwp->wall_normal.y;
    wall.vz = pwp->wall_normal.z;
    floor.px = 0.0f;
    floor.py = 0.0f;
    floor.pz = 0.0f;
    floor.vx = pwp->floor_normal.x;
    floor.vy = pwp->floor_normal.y;
    floor.vz = pwp->floor_normal.z;
    func_8C64016C(&wall, &floor, &cross);
    v0 = mwp->spd;
    if (njScalor(&v0) != 0.0f) {
        njUnitVector(&v0);
    }
    if (njInnerProduct((NJS_VECTOR *)&cross.vx, &v0) < 0.0f) {
        cross.vx = -cross.vx;
        cross.vy = -cross.vy;
        cross.vz = -cross.vz;
    }
    s = njScalor(&mwp->spd);
    v0 = mwp->spd;
    mwp->spd.x = s * cross.vx;
    mwp->spd.y = s * cross.vy;
    mwp->spd.z = s * cross.vz;
    v1 = mwp->spd;
    njUnitVector(&v0);
    njUnitVector(&v1);
    twp->ang.y -= VectorAngle(&v0, &v1, &pwp->floor_normal);
    if (DiffAngle(mwp->ang_aim.y, twp->ang.y) > 0x6000) {
        twp->ang.y += 0x8000;
    }
    mwp->ang_aim.y = twp->ang.y;
}

ASM_STUB(void func_8C0723E0(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C07256A(motionwk *mwp, playerwk *pwp))
