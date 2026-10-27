/*
 * 1ST_READ 8C0EAEC0-8C0EB110 (chunk 8C0EAEC0).
 *
 *  func_8C0EAEC0  moves *dst towards it along the segment pos -> dst only as far
 *                 as a radius-4 sphere sweep (func_8C0569E0) gets: if the sweep
 *                 hits, dst = pos + (clipped move)
 *  func_8C0EAF28  Big fishing camera (only caller func_8C0E943C): target = midpoint
 *                 of the two points in B_8C6B6E68; the camera keeps its direction
 *                 from the target at twice their distance, moving at most 20 units
 *                 per frame; it never sinks below the target height, and while it
 *                 is less than 5 units above the water level D_8C565F6C it is
 *                 pulled back out with a sweep from the camera task position.
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * Camera control work (*D_8C112048), SADX _camcontwk analogue. Only the
 * fields used here are known; +0x48 is a Float (code_8C0A966C.c,
 * code_8C0B70F4.c).
 */
typedef struct camcontwk_l {
    Sint32 cammode;             /* 0x00 (SADX analogy, unverified) */
    Sint32 camnextmode;         /* 0x04 (SADX analogy, unverified) */
    Sint32 camreturnmode;       /* 0x08 (SADX analogy, unverified) */
    NJS_POINT3 pos;             /* 0x0C camera position */
    NJS_POINT3 tgt;             /* 0x18 look-at target */
} camcontwk_l;

extern camcontwk_l *D_8C112048;
extern NJS_POINT3 B_8C6B6E68[2];
extern Float D_8C565F6C;

Bool func_8C0569E0(NJS_POINT3 *pos, NJS_POINT3 *v, Angle3 *ang, Float r);

void func_8C0EAEC0(NJS_POINT3 *pos, NJS_POINT3 *dst)
{
    NJS_VECTOR v;

    v.x = dst->x - pos->x;
    v.y = dst->y - pos->y;
    v.z = dst->z - pos->z;
    if (func_8C0569E0(pos, &v, NULL, 4.0f)) {
        dst->x = pos->x + v.x;
        dst->y = pos->y + v.y;
        dst->z = pos->z + v.z;
    }
}

void func_8C0EAF28(void)
{
    NJS_POINT3 p0;
    NJS_POINT3 p1;
    NJS_POINT3 cpos;
    NJS_VECTOR mv;
    NJS_VECTOR d;
    Float f;

    p0 = B_8C6B6E68[0];
    p1 = B_8C6B6E68[1];
    cpos = camera_twp->pos;
    D_8C112048->tgt.x = (p0.x + p1.x) * 0.5f;
    D_8C112048->tgt.y = (p0.y + p1.y) * 0.5f;
    D_8C112048->tgt.z = (p0.z + p1.z) * 0.5f;
    mv.x = D_8C112048->pos.x - D_8C112048->tgt.x;
    mv.y = D_8C112048->pos.y - D_8C112048->tgt.y;
    mv.z = D_8C112048->pos.z - D_8C112048->tgt.z;
    d.x = p0.x - p1.x;
    d.y = p0.y - p1.y;
    d.z = p0.z - p1.z;
    f = njScalor(&d) * 2.0f;
    njUnitVector(&mv);
    mv.x = SA1_FMAC(f, mv.x, D_8C112048->tgt.x) - D_8C112048->pos.x;
    mv.y = SA1_FMAC(f, mv.y, D_8C112048->tgt.y) - D_8C112048->pos.y;
    mv.z = SA1_FMAC(f, mv.z, D_8C112048->tgt.z) - D_8C112048->pos.z;
    f = 20.0f;
    if (f > njScalor(&mv)) {
        D_8C112048->pos.x += mv.x;
        D_8C112048->pos.y += mv.y;
        D_8C112048->pos.z += mv.z;
    } else {
        njUnitVector(&mv);
        SA1_FMAC_ACC(D_8C112048->pos.x, mv.x, f);
        SA1_FMAC_ACC(D_8C112048->pos.y, mv.y, f);
        SA1_FMAC_ACC(D_8C112048->pos.z, mv.z, f);
    }
    if (D_8C112048->pos.y < D_8C112048->tgt.y) {
        D_8C112048->pos.y = D_8C112048->tgt.y;
    }
    if (D_8C565F6C + -5.0f > D_8C112048->pos.y) {
        func_8C0EAEC0(&cpos, &D_8C112048->pos);
    }
}
