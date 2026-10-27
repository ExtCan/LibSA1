#include "common.h"
#include "ninja.h"
#include "sa1/shc_intrinsics.h"

/*
 * 8C028BC0-8C028BDC: squared distance |a - b|^2 between two points.
 *
 * The original is an SHC "#pragma inline_asm" function: SHC copies the
 * assembly body verbatim and appends ".ALIGN 4 / RTS / NOP" (hence the NOP
 * in the delay slot, unlike compiled code), and still emits an out-of-line,
 * exported copy that the rest of the object (8C028BDC...) follows directly.
 * The difference vector (dx, dy, dz, 0) is built in FV4 and dotted with
 * itself with FIPR FV4,FV4; SHC's _builtin_fipr always goes through two
 * stack arrays and cannot produce this. TARGET_PC uses the C equivalent.
 */

#ifdef TARGET_PC
Float func_8C028BC0(NJS_POINT3 *a, NJS_POINT3 *b) {
    float t[4];

    t[0] = a->x - b->x;
    t[1] = a->y - b->y;
    t[2] = a->z - b->z;
    t[3] = 0.0f;
    return _builtin_fipr(t, t);
}
#else
/* The SHC build takes this function's original code from the asm split from your own disc (the public sources carry no
 * assembly copied from the game: 2026-10-27, the repo audit). The original is an SHC "#pragma inline_asm" function. */
ASM_STUB(Float func_8C028BC0(NJS_POINT3 *a, NJS_POINT3 *b))
#endif
