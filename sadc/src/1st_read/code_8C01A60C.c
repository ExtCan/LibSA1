/*
 * 1ST_READ 8C01A60C-8C01A7A4 (chunk 8C019400): the three conversion kinds
 * of the camera mode table (byte 5 of a mode row; 8C019F4A runs the one of
 * the current camera after its mode function, and the adjusts call kind 3
 * again: func_8C025010). Staged by dc-fishcam for 0.9.6
 * (gsa1_090_bigfish.md 2.1, review R3.3; v1_triggers.md 1.2 / 1.4).
 *
 *  func_8C01A60C  kind 3: the camera work's position and angles are the
 *                 camera taskwk's.
 *  func_8C01A680  kind 2 (FISHING, the lure camera, KNUCKLES, ...): position
 *                 from the camera work; yaw and pitch from the position to
 *                 the target (atan2f, BAMS); roll from the work.
 *  func_8C01A718  kind 1: the camera on a sphere around the target, radius
 *                 the work's distance, at the work's pitch and yaw.
 *
 * Matching note: kind 1 loads the target coordinate before the njSin /
 * njCos call (SHC keeps it in FR12), so the SHC form is "tgt + r * f()";
 * the PC form fuses like the original FMAC.
 */
#include "sa1/sa1.h"

/* camera work *D_8C112048 (v1_triggers.md 1.2: written by the mode
 * functions, converted into the camera taskwk). No pointers. */
typedef struct {
    Sint32 w00;                 /* 0x00 */
    Sint32 w04;                 /* 0x04 */
    Float dist;                 /* 0x08 distance from the target (conversion kind 1) */
    NJS_POINT3 pos;             /* 0x0C camera position */
    NJS_POINT3 tgt;             /* 0x18 camera target */
    Angle angx;                 /* 0x24 */
    Angle angy;                 /* 0x28 */
    Angle angz;                 /* 0x2C */
} CAMWK_FC;

extern CAMWK_FC *D_8C112048;

void func_8C01A60C(taskwk *twp)
{
    twp->pos.x = D_8C112048->pos.x;
    twp->pos.y = D_8C112048->pos.y;
    twp->pos.z = D_8C112048->pos.z;
    twp->ang.x = D_8C112048->angx;
    twp->ang.y = D_8C112048->angy;
    twp->ang.z = D_8C112048->angz;
}

void func_8C01A680(taskwk *twp)
{
    Float dx;
    Float dz;

    twp->pos.x = D_8C112048->pos.x;
    twp->pos.y = D_8C112048->pos.y;
    twp->pos.z = D_8C112048->pos.z;
    dx = D_8C112048->pos.x - D_8C112048->tgt.x;
    dz = D_8C112048->pos.z - D_8C112048->tgt.z;
    twp->ang.y = SA1_FTOI(atan2f(dx, dz) * 10430.381f);
    twp->ang.x = SA1_FTOI(atan2f(D_8C112048->tgt.y - D_8C112048->pos.y, njSqrt(SA1_FMAC(dx, dx, dz * dz))) * 10430.381f);
    twp->ang.z = D_8C112048->angz;
}

void func_8C01A718(taskwk *twp)
{
    Float r;

    r = D_8C112048->dist * njCos(D_8C112048->angx);
    twp->pos.y = D_8C112048->tgt.y - D_8C112048->dist * njSin(D_8C112048->angx);
    twp->pos.x = SH_ONLY(D_8C112048->tgt.x + r * njSin(D_8C112048->angy))
                 PC_ONLY(SA1_FMAC(r, njSin(D_8C112048->angy), D_8C112048->tgt.x));
    twp->pos.z = SH_ONLY(D_8C112048->tgt.z + r * njCos(D_8C112048->angy))
                 PC_ONLY(SA1_FMAC(r, njCos(D_8C112048->angy), D_8C112048->tgt.z));
    twp->ang.x = D_8C112048->angx;
    twp->ang.y = D_8C112048->angy;
    twp->ang.z = D_8C112048->angz;
}
