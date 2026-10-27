#include "sa1/sa1.h"

/*
 * Angle and vector utilities (1ST_READ 8C055D20-8C057460, first part:
 * AdjustAngle .. func_8C055E40).
 */

extern taskwk *D_8C18B7EC;      /* camera taskwk (pos at +0x20; also code_8C010400.c, code_8C0869E0.c) */

/*
 * Turn ang towards target by at most spd (16-bit BAMS). Returns target when
 * it is within spd, else ang +/- spd, as an unsigned 16-bit angle.
 */
Angle AdjustAngle(Angle ang, Angle target, Angle spd) {
    Sint16 diff;

    target = (Uint16)target;
    ang = (Uint16)ang;
    diff = target - ang;
    if (diff <= spd && diff >= -spd) {
        return target;
    }
    if (diff & 0x8000) {
        target = ang - spd;
    } else {
        target = ang + spd;
    }
    return (Uint16)target;
}

/*
 * The original inlines SubAngle into func_8C055D70 (the expansion keeps the
 * argument copies MOV R5,R7 / EXTU.W R4,R6); AdjustAngle and DiffAngle do not
 * call it.
 */
#ifndef TARGET_PC
#pragma inline(SubAngle)
#endif
/* Signed 16-bit difference b - a. */
Sint32 SubAngle(Sint32 a, Sint32 b) {
    Sint32 diff;

    a = (Uint16)a;
    b = (Uint16)b;
    diff = b - a;
    return (Sint16)diff;
}

/* Absolute 16-bit difference |b - a| (0..0x8000). */
Sint32 DiffAngle(Sint32 a, Sint32 b) {
    Sint32 diff;

    diff = (Uint16)b - (Uint16)a;
    if ((Sint16)diff < 0) {
        diff = -diff;
    }
    return (Uint16)diff;
}

/*
 * Moves target by whole turns (0x10000) towards ang, in the direction of the
 * 16-bit difference: if target is ahead of ang it is raised until it is
 * >= ang, if behind it is lowered until it is <= ang; equal angles give ang.
 */
Angle func_8C055D70(Angle ang, Angle target) {
    Sint32 diff;
    Angle res;

    diff = SubAngle(ang, target);
    if (diff > 0) {
        res = target;
        while (ang > res) {
            res += 0x10000;
        }
    } else if (diff < 0) {
        res = target;
        while (ang < res) {
            res -= 0x10000;
        }
    } else {
        res = ang;
    }
    return res;
}

/*
 * Angle between two unit vectors a and b (acos of the inner product, in
 * BAMS). With a reference vector c, the angle is mirrored to 0x10000 - ang
 * when (a x b) points away from c.
 */
Angle VectorAngle(void *a, void *b, void *c) {
    NJS_VECTOR ov;
    Angle ang;

    ang = SA1_FTOI(func_8C10D008(njInnerProduct(a, b)) * 10430.381f);
    njOuterProduct(a, b, &ov);
    if (c != NULL) {
        if (njInnerProduct(c, &ov) < 0.0f) {
            ang = 0x10000 - ang;
        }
    }
    return ang;
}

/*
 * On-screen test for twp->pos: projects it with the current matrix and
 * checks it against the 640x480 screen widened by a radius r = 3000 / camera
 * distance (x in [-r, 640 + r], |240 - y| < 240 + r).
 */
Sint32 func_8C055E40(taskwk *twp) {
    NJS_POINT2 scr;
    Float dist;
    Float r;
    Float t;

    /* The original leaves r unset when dist == 0 (FR4 then holds whatever
     * njDistanceP2P left there); PC uses 0. */
    PC_ONLY(r = 0.0f;)
    njPushMatrix(NULL);
    njProjectScreen(NULL, &twp->pos, &scr);
    njPopMatrix(1);
    dist = njDistanceP2P(&D_8C18B7EC->pos, &twp->pos);
    if (dist != 0.0f) {
        r = 3000.0f / dist;
    }
    t = (scr.x + r) / SA1_FMAC(2.0f, r, 640.0f);
    if (t < 0.0f || t > 1.0f) {
        return 0;
    }
    if ((240.0f - scr.y) * (240.0f - scr.y) < (r + 240.0f) * (r + 240.0f)) {
        return 1;
    }
    return 0;
}
