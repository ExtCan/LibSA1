#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C078E40-8C0792DC): vector space conversions.
 *  func_8C078E40  PConvertVector_G2P: global -> player space (rotate by
 *                 ang.y, -ang.x, -ang.z on a pushed unit matrix)
 *  func_8C078EB0  PConvertVector_P2G: player -> global space (ang.z, ang.x,
 *                 -ang.y)
 *  func_8C078F1E  global -> player space along a floor normal (angles
 *                 from atan2f(n.x, n.y) and func_8C10D058(n.z), plus angy)
 *  func_8C079000  player -> global space along a floor normal (inverse of
 *                 func_8C078F1E)
 *  func_8C079096  mwp->spd -> pwp->spd (player space), snapping small X/Z
 *                 components to 0, Y cleared on the ground; wraps the
 *                 angles to 16 bits and sets pwp->dotp
 *  func_8C079234  moves the object hit by the player (cwp->hit_cwp) by
 *                 mwp->spd, then mwp->spd -> pwp->spd (no caller found)
 *
 * Matching notes: the input vector is copied through a walking Float
 * pointer (FMOV.S @R4+ with v kept on the stack); "a ? njRotateN(NULL, a) :
 * (void)0" gives the stack temporary of the rotations; 8C079096 needs the
 * nested if for the Z snap of the airborne path (FR5/FR6 allocation);
 * 8C079234 reads the hit task before copying mwp->spd.
 */

void func_8C078F1E(NJS_POINT3 *nrm, NJS_POINT3 *v, Angle angy);
void func_8C079000(NJS_POINT3 *nrm, NJS_POINT3 *v, Angle angy);
void func_8C079234(taskwk *twp, motionwk *mwp, playerwk *pwp);

void func_8C078E40(taskwk *twp, NJS_POINT3 *v)
{
    NJS_POINT3 vs;
    Float *p;

    p = &v->x;
    vs.x = *p++;
    vs.y = *p++;
    vs.z = *p;
    func_8C057B40();
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    -twp->ang.x ? njRotateX(NULL, -twp->ang.x) : (void)0;
    -twp->ang.z ? njRotateZ(NULL, -twp->ang.z) : (void)0;
    njCalcVector(NULL, &vs, v);
    njPopMatrix(1);
}

void func_8C078EB0(taskwk *twp, NJS_POINT3 *v)
{
    NJS_POINT3 vs;
    Float *p;

    p = &v->x;
    vs.x = *p++;
    vs.y = *p++;
    vs.z = *p;
    func_8C057B40();
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
    njCalcVector(NULL, &vs, v);
    njPopMatrix(1);
}

void func_8C078F1E(NJS_POINT3 *nrm, NJS_POINT3 *v, Angle angy)
{
    NJS_POINT3 vs;
    Angle angx;
    Angle angz;
    Float *p;

    p = &v->x;
    vs.x = *p++;
    vs.y = *p++;
    vs.z = *p;
    angz = -SA1_FTOI(atan2f(nrm->x, nrm->y) * 10430.381f);
    angx = SA1_FTOI(func_8C10D058(nrm->z) * 10430.381f);
    func_8C057B40();
    angy ? njRotateY(NULL, angy) : (void)0;
    -angx ? njRotateX(NULL, -angx) : (void)0;
    -angz ? njRotateZ(NULL, -angz) : (void)0;
    njCalcVector(NULL, &vs, v);
    njPopMatrix(1);
}

void func_8C079000(NJS_POINT3 *nrm, NJS_POINT3 *v, Angle angy)
{
    NJS_POINT3 vs;
    Angle angx;
    Angle angz;
    Float *p;

    p = &v->x;
    vs.x = *p++;
    vs.y = *p++;
    vs.z = *p;
    angz = -SA1_FTOI(atan2f(nrm->x, nrm->y) * 10430.381f);
    angx = SA1_FTOI(func_8C10D058(nrm->z) * 10430.381f);
    func_8C057B40();
    angz ? njRotateZ(NULL, angz) : (void)0;
    angx ? njRotateX(NULL, angx) : (void)0;
    -angy ? njRotateY(NULL, -angy) : (void)0;
    njCalcVector(NULL, &vs, v);
    njPopMatrix(1);
}

void func_8C079096(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    Float lim;

    v = mwp->spd;
    if ((twp->flag & 3) && (pwp->attr & 0x4000)) {
        func_8C078F1E(&pwp->floor_normal, &v, twp->ang.y);
    } else {
        PConvertVector_G2P(twp, &v);
    }
    if (twp->flag & 3) {
        v.y = 0.0f;
    }
    if (twp->flag & 3) {
        if (!(pwp->last_attr & 0xC)) {
            lim = pwp->p.run_accel * 0.89999998f;
            if (v.x < 0.0099999998f && v.x > -0.0099999998f) {
                v.x = 0.0f;
            }
            if (v.z <= lim && v.z >= -lim) {
                v.z = 0.0f;
            }
        }
    } else {
        if (v.x <= 0.00099999996f && v.x >= -0.00099999996f) {
            v.x = 0.0f;
        }
        if (v.z <= 0.00099999996f) {
            if (v.z >= -0.00099999996f) {
                v.z = 0.0f;
            }
        }
    }
    pwp->spd = v;
    twp->ang.x &= 0xFFFF;
    twp->ang.y &= 0xFFFF;
    twp->ang.z &= 0xFFFF;
    mwp->ang_aim.x &= 0xFFFF;
    mwp->ang_aim.y &= 0xFFFF;
    mwp->ang_aim.z &= 0xFFFF;
    if ((twp->flag & 3) && !(pwp->attr & 0x4000)) {
        pwp->dotp = -njInnerProduct(&Gravity, &pwp->floor_normal);
    } else {
        pwp->dotp = 1.0f;
    }
}

void func_8C079234(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    taskwk *htwp;

    htwp = CWP(twp)->hit_cwp->mytask->twp;
    v = mwp->spd;
    htwp->pos.x += v.x;
    htwp->pos.y += v.y;
    htwp->pos.z += v.z;
    PConvertVector_G2P(twp, &v);
    if (v.x <= 0.00099999996f && v.x >= -0.00099999996f) {
        v.x = 0.0f;
    }
    if (v.z <= 0.00099999996f && v.z >= -0.00099999996f) {
        v.z = 0.0f;
    }
    if (twp->flag & 3) {
        v.y = 0.0f;
    }
    pwp->spd = v;
}
