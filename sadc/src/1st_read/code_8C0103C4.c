#include "common.h"
#include "ninja.h"

/*
 * 8C0103C4-8C0103E4: hand-written SH-4 assembly (not SHC output).
 *
 * - The routine runs FTRV XMTRX,FV0 on a matrix that is assumed to be
 *   already loaded in the XF bank (by the FRCHG loader at 8C010390, see
 *   code_8C010340.c); SHC 5.1 cannot generate this from C.
 * - The constant D_8C0103E0 (0x3F800000 = 1.0f, the w of the vector) lives
 *   in the P section right after the code; the 7 zero words that follow it
 *   are the linker gap up to the next 32-byte object at 8C010400.
 * - The pool is only 4-byte aligned with 00 00 fill (SHC with
 *   -extra=a=1800 aligns pools to 32 bytes with 00 09 fill); the region
 *   8C010320-8C010400 is listed as assembly in docs/compiler.md.
 *
 * The body below is a C description of what the assembly does; it is kept
 * under NON_MATCHING only and the original assembly stays in the build.
 */

extern Float D_8C0103E0; /* 1.0f, embedded after the code */

#ifdef NON_MATCHING
/* njCalcPoint-like with the matrix already in XMTRX (xm[16], column-major
 * as loaded by the FRCHG loader): dst = XMTRX * (src.x, src.y, src.z, 1).
 * The original stores z, y, x through a pre-decremented dst + 3. */
extern Float xm[16]; /* stands for the XF register bank, not a real symbol */

void func_8C0103C4(NJS_POINT3 *src, NJS_POINT3 *dst) {
    Float x;
    Float y;
    Float z;

    x = src->x;
    y = src->y;
    z = src->z;
    dst->x = xm[0] * x + xm[4] * y + xm[8] * z + xm[12] * D_8C0103E0;
    dst->y = xm[1] * x + xm[5] * y + xm[9] * z + xm[13] * D_8C0103E0;
    dst->z = xm[2] * x + xm[6] * y + xm[10] * z + xm[14] * D_8C0103E0;
}
#endif
