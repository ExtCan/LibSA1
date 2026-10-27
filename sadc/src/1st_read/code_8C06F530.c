#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Player movement along a path (8C06F530), called by PGetAcceleration
 * 8C06D254, 8C06DCA2 and 8C071700 when the player follows a path.
 * twp->value holds the PATHTAG; pwp->hpos is the position along it,
 * advanced by the forward speed pwp->spd.x (boosted to dash_speed while the
 * stick is held). The player's yaw is turned to follow the path; if the
 * path ends (8C0897F8 fails) or the direction is degenerate, bit 0x2000 of
 * twp->flag (path mode) is cleared.
 */

/*
 * Path tag (SADX pathtag; full layout in code_8C08966E.c). Only passed
 * through here, so it stays incomplete (it holds pointers: tblhead,
 * pathctrl). Path state filled by 8C0897F8 (SADX pathinfo; no pointers).
 */
typedef struct pathtag PATHTAG;

typedef struct {
    Angle slangx;               /* 0x00 */
    Angle slangz;               /* 0x04 */
    Angle angx;                 /* 0x08 */
    Angle angz;                 /* 0x0C */
    Float onpathpos;            /* 0x10 */
    Float xpos;                 /* 0x14 */
    Float ypos;                 /* 0x18 */
    Float zpos;                 /* 0x1C */
    Float xpos2;                /* 0x20 */
    Float ypos2;                /* 0x24 */
    Float zpos2;                /* 0x28 */
    NJS_VECTOR normal;          /* 0x2C */
    NJS_VECTOR normala;         /* 0x38 */
    NJS_VECTOR front;           /* 0x44 */
} PATHINFO;                     /* 0x50 */

/* path evaluation (code_8C08966E.c) */
extern Bool func_8C0897F8(PATHTAG *tag, PATHINFO *info);

void func_8C06F530(taskwk *twp, motionwk *mwp, playerwk *pwp) {
    PATHINFO info;
    NJS_VECTOR v0;
    NJS_VECTOR v1;
    NJS_POINT3 p;
    NJS_LINE line;
    Angle ang;

    if (pwp->spd.x < pwp->p.dash_speed && GetAnalog(twp, NULL, NULL)) {
        pwp->spd.x = pwp->p.dash_speed;
    }
    pwp->hpos += pwp->spd.x;
    info.onpathpos = pwp->hpos;
    if (func_8C0897F8((PATHTAG *)twp->value.ptr, &info)) {
        line.vx = pwp->cstsp->tnorm.x;
        line.vy = pwp->cstsp->tnorm.y;
        line.vz = pwp->cstsp->tnorm.z;
        line.px = line.py = line.pz = 0.0f;
        p.x = info.xpos2 - twp->pos.x;
        p.y = info.ypos2 - twp->pos.y;
        p.z = info.zpos2 - twp->pos.z;
        njDistanceP2L(&p, &line, &v0);
        p = mwp->spd;
        njDistanceP2L(&p, &line, &v1);
        if (njScalor(&v1) == 0.0f || njScalor(&v0) == 0.0f) {
            twp->flag &= ~0x2000;
            return;
        } else {
            njUnitVector(&v0);
            njUnitVector(&v1);
            ang = VectorAngle(&v0, &v1, &pwp->cstsp->tnorm);
            ang = (Uint16)(twp->ang.y + ang);
            if (DiffAngle(ang, mwp->ang_aim.y) > 0x4000) {
                ang = (Uint16)(0x8000 - ang);
            }
            mwp->ang_aim.y = ang;
            twp->ang.y = ang;
        }
    } else {
        twp->flag &= ~0x2000;
    }
}
