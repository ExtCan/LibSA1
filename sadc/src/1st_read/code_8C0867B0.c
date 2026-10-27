#include "common.h"
#include "ninja.h"

/*
 * 8C0867B0-8C0867EE: hand-written SH-4 assembly (not SHC output). It sits
 * in an assembly module right after the lookup table at 8C085F68 and is
 * followed by more hand-written helpers (8C0867F0).
 * - Matrix elements are read with "MOV #disp,R0; FMOV.S @(R0,R4),FRn",
 *   including "MOV #0,R0" for element 0 (SHC would emit FMOV.S @R4,FRn);
 * - the m == NULL path runs FTRV XMTRX,FV0 on whatever matrix is currently
 *   loaded in the XF bank, without loading it;
 * - the other path uses FIPR FV0,FV4 directly with FR6 never loaded (it is
 *   multiplied by the 0.0 in FR2); SHC's fipr() intrinsic always copies
 *   both operands through stack arrays;
 * - the branch target is aligned with a "09 00" NOP fill (asmsh .ALIGN
 *   style; SHC pads with "00 09").
 * The body below is a C description (a 2D njCalcPoint: dst = M * (x, y, 0, 1),
 * keeping x and y); the original assembly stays in the build.
 */

#ifdef NON_MATCHING
/* SHC intrinsic (machine.h ftrv): vec2 = XMTRX * vec1 */
extern void _builtin_ftrv(float vec1[4], float vec2[4]);

/* Transforms the 2D point src by m (translation at m[12], m[13]) into dst.
 * m == NULL uses the matrix currently loaded in XMTRX. */
void func_8C0867B0(Float *m, NJS_POINT2 *src, NJS_POINT2 *dst) {
    float v[4];
    float r[4];

    v[0] = src->x;
    v[1] = src->y;
    v[2] = 0.0f;
    v[3] = 1.0f;
    if (m == NULL) {
        _builtin_ftrv(v, r);
        dst->x = r[0];
        dst->y = r[1];
        return;
    }
    dst->x = v[0] * m[0] + v[1] * m[4] + m[12];
    dst->y = v[0] * m[1] + v[1] * m[5] + m[13];
}
#endif
