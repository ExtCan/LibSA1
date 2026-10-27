#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Player movement along a path (8C078308, 8C0784F8, 8C078710), called by the
 * character mains (SonicMain 8C0A77BC, TailsMain, KnucklesMain, E102Main,
 * AmyMain, BigMain, EggmanMain). twp->value holds the PATHTAG and
 * pwp->hpos the position along it, advanced by the forward speed pwp->spd.x
 * (backwards when the player faces against the path direction).
 *
 * mwp->rad is used as a 0.0f/1.0f "on path" result by 8C0784F8/8C078710.
 *
 * Matching: the rotation angle goes through an unsigned local that SHC keeps
 * in a stack slot (@R15); the conditional rotations are written as ?:
 * expressions (an if statement swaps the R2/R3 scratch registers).
 */

/*
 * Path tag (SADX pathtag; full layout in code_8C08966E.c). Only the two
 * leading scalar fields are read here; it holds pointers (tblhead,
 * pathctrl) after them, so this view stops before those.
 */
typedef struct pathtag {
    Uint8 slattr;               /* 0x00 */
    Uint8 unk1;                 /* 0x01 */
    Uint16 points;              /* 0x02 */
    Float totallen;             /* 0x04 */
} PATHTAG;

/* Path state filled by 8C0897F8 (SADX pathinfo; no pointers). */
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

void func_8C078308(taskwk *twp, motionwk *mwp, playerwk *pwp) {
    PATHINFO info;
    NJS_VECTOR vd;
    NJS_VECTOR vs;
    Uint32 rot;
    Angle ang;

    twp->flag &= ~0x2003;
    info.onpathpos = pwp->hpos;
    if (func_8C0897F8((PATHTAG *)twp->value.ptr, &info)) {
        ang = SA1_FTOI(atan2f(info.front.z, info.front.x) * 10430.381f);
        if (DiffAngle(mwp->ang_aim.y, ang) > 0x4000) {
            pwp->hpos -= pwp->spd.x;
        } else {
            pwp->hpos += pwp->spd.x;
        }
    } else {
        twp->pos.x += mwp->spd.x;
        twp->pos.y += mwp->spd.y;
        twp->pos.z += mwp->spd.z;
        return;
    }
    info.onpathpos = pwp->hpos;
    if (func_8C0897F8((PATHTAG *)twp->value.ptr, &info)) {
        ang = SA1_FTOI(atan2f(info.front.z, info.front.x) * 10430.381f);
        vs.x = vs.z = 0.0f;
        vs.y = 1.0f;
        func_8C057B40();
        (rot = info.angz) ? njRotateZ(NULL, rot) : (void)0;
        (rot = info.angx) ? njRotateX(NULL, rot) : (void)0;
        njCalcVector(NULL, &vs, &vd);
        njPopMatrix(1);
        if (vd.y < 0.0f) {
            ang = (Uint16)(0x8000 - ang);
        }
        if (DiffAngle(mwp->ang_aim.y, ang) > 0x4000) {
            ang += 0x8000;
        }
        mwp->ang_aim.y = ang;
        twp->ang.y = AdjustAngle(twp->ang.y, ang, 0x800);
        twp->ang.x = info.angx;
        twp->ang.z = info.angz;
        twp->pos.x = info.xpos2;
        twp->pos.y = info.ypos2;
        twp->pos.z = info.zpos2;
        twp->flag |= 0x2001;
    } else {
        twp->pos.x += mwp->spd.x;
        twp->pos.y += mwp->spd.y;
        twp->pos.z += mwp->spd.z;
    }
}

Bool func_8C0784F8(taskwk *twp, motionwk *mwp, playerwk *pwp) {
    PATHINFO info;
    NJS_VECTOR vd;
    NJS_VECTOR vs;
    Uint32 rot;
    PATHTAG *tag;
    Angle ang;

    tag = (PATHTAG *)twp->value.ptr;
    mwp->rad = 0.0f;
    info.onpathpos = pwp->hpos;
    if (func_8C0897F8(tag, &info)) {
        ang = SA1_FTOI(atan2f(info.front.z, info.front.x) * 10430.381f);
        if (DiffAngle(mwp->ang_aim.y, ang) > 0x4000) {
            pwp->hpos -= pwp->spd.x;
        } else {
            pwp->hpos += pwp->spd.x;
        }
        mwp->rad = 1.0f;
    } else {
        twp->pos.x += mwp->spd.x;
        twp->pos.y += mwp->spd.y;
        twp->pos.z += mwp->spd.z;
        mwp->rad = 0.0f;
        return FALSE;
    }
    if (tag->points == 1) {
        pwp->hpos = 0.0f;
        mwp->rad = 0.0f;
    } else if (!(tag->totallen > pwp->hpos)) {
        pwp->hpos = tag->totallen - 0.01f;
        mwp->rad = 0.0f;
    } else if (pwp->hpos <= 0.0f) {
        pwp->hpos = 0.01f;
        mwp->rad = 0.0f;
    }
    info.onpathpos = pwp->hpos;
    if (func_8C0897F8(tag, &info)) {
        ang = SA1_FTOI(atan2f(info.front.z, info.front.x) * 10430.381f);
        vs.x = vs.z = 0.0f;
        vs.y = 1.0f;
        func_8C057B40();
        (rot = info.angz) ? njRotateZ(NULL, rot) : (void)0;
        (rot = info.angx) ? njRotateX(NULL, rot) : (void)0;
        njCalcVector(NULL, &vs, &vd);
        njPopMatrix(1);
        if (vd.y < 0.0f) {
            ang = (Uint16)(0x8000 - ang);
        }
        if (DiffAngle(mwp->ang_aim.y, ang) > 0x4000) {
            ang += 0x8000;
        }
        mwp->ang_aim.y = ang;
        twp->ang.y = AdjustAngle(twp->ang.y, ang, 0x800);
        twp->ang.x = info.angx;
        twp->ang.z = info.angz;
        twp->pos.x = info.xpos2;
        twp->pos.y = info.ypos2;
        twp->pos.z = info.zpos2;
        return TRUE;
    }
    return FALSE;
}

Bool func_8C078710(taskwk *twp, motionwk *mwp, playerwk *pwp) {
    PATHINFO info;
    PATHTAG *tag;

    tag = (PATHTAG *)twp->value.ptr;
    mwp->rad = 0.0f;
    info.onpathpos = pwp->hpos;
    if (func_8C0897F8(tag, &info)) {
        pwp->hpos += pwp->spd.x;
        mwp->rad = 1.0f;
    } else {
        twp->pos.x += mwp->spd.x;
        twp->pos.y += mwp->spd.y;
        twp->pos.z += mwp->spd.z;
        mwp->rad = 0.0f;
        return FALSE;
    }
    if (tag->points == 1) {
        pwp->hpos = 0.0f;
        mwp->rad = 0.0f;
    } else if (!(tag->totallen > pwp->hpos)) {
        pwp->hpos = tag->totallen - 0.01f;
        mwp->rad = 0.0f;
    } else if (pwp->hpos <= 0.0f) {
        pwp->hpos = 0.01f;
        mwp->rad = 0.0f;
    }
    info.onpathpos = pwp->hpos;
    if (func_8C0897F8(tag, &info)) {
        twp->pos.x = info.xpos2;
        twp->pos.y = info.ypos2;
        twp->pos.z = info.zpos2;
        return TRUE;
    }
    return FALSE;
}
