#include "common.h"
#include "ninja.h"

/*
 * 8C010340-8C0103C4: hand-written SH-4 assembly (not SHC output).
 *
 * - Both routines use FRCHG to fill the XF bank and FTRV XMTRX,FV0, which
 *   SHC 5.1 cannot generate from C.
 * - The 4-byte constant D_8C01038C (0x3F800000 = 1.0f, the w of the vector)
 *   sits inline in the code, so the symbol func_8C01038E is misplaced: its
 *   first "instruction" (CMP/EQ R8,R15 = 0x3F80) is the upper half of that
 *   float, and the real second routine starts at 8C010390.
 * - The region has no 00 09 pool padding (see docs/compiler.md, region
 *   8C010320-8C010400 listed as assembly).
 *
 * The bodies below are C descriptions of what the assembly does; they are
 * kept under NON_MATCHING only and the original assembly stays in the build.
 */

extern Float *B_8C88F5F8; /* current matrix pointer (Ninja _nj_curr_matrix_-like) */
extern Float D_8C01038C;  /* 1.0f, embedded in the code */

#ifdef NON_MATCHING
/* njCalcPoint-like: dst = m * (src.x, src.y, src.z, 1); m == NULL uses the
 * current matrix. Original loads m into XMTRX and uses FTRV. */
void func_8C010340(Float *m, NJS_POINT3 *src, NJS_POINT3 *dst) {
    Float x;
    Float y;
    Float z;

    x = src->x;
    y = src->y;
    z = src->z;
    if (m == NULL) {
        m = B_8C88F5F8;
    }
    dst->x = m[0] * x + m[1] * y + m[2] * z + m[3] * D_8C01038C;
    dst->y = m[4] * x + m[5] * y + m[6] * z + m[7] * D_8C01038C;
    dst->z = m[8] * x + m[9] * y + m[10] * z + m[11] * D_8C01038C;
}

/* Real entry is 8C010390: loads m (or the current matrix when m == NULL)
 * transposed into the XF bank (XMTRX) for later FTRV use. No C equivalent. */
void func_8C01038E(Float *m) {
    if (m == NULL) {
        m = B_8C88F5F8;
    }
}
#endif
