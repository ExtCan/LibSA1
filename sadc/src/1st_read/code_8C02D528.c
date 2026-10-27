#include "common.h"
#include "ninja.h"
#include "sa1/shc_intrinsics.h"

/*
 * 1ST_READ 8C02D528-8C02D53C: transforms the point p in place by the matrix
 * currently loaded in the SH-4 XMTRX register bank, p = XMTRX * (x, y, z, 1)
 * (w result dropped). Used by 8C0955C0 on a stack NJS_POINT3 copy.
 *
 * Last routine of the hand-written assembly module 8C02CE80-8C02D980 (see
 * code_8C02CE80.c / code_8C02CF70.c); it is written as an SHC
 * "#pragma inline_asm" function like func_8C028BC0: SHC copies the body
 * and appends RTS / NOP (the NOP in the delay slot), and still emits an
 * out-of-line, exported copy. The point is read with @R4+, FLDI1 fills the
 * w lane, FTRV XMTRX,FV0 does the transform and the result is stored back
 * with pre-decrement stores; SHC's _builtin_ftrv always goes through two
 * stack arrays and cannot produce this. The data words after the routine
 * (D_8C02D540 free-poly head, D_8C02D544, D_8C02D548 polygon lists,
 * D_8C02D560 table, float constants at 8C02D960-8C02D96C) belong to the
 * module and are not defined here.
 *
 * TARGET_PC uses the C equivalent with the runtime's emulated XMTRX.
 */

#ifdef TARGET_PC
void func_8C02D528(NJS_POINT3 *p) {
    float v[4];
    float r[4];

    v[0] = p->x;
    v[1] = p->y;
    v[2] = p->z;
    v[3] = 1.0f;
    _builtin_ftrv(v, r);
    p->x = r[0];
    p->y = r[1];
    p->z = r[2];
}
#else
/* The SHC build takes this function's original code from the asm split from your own disc (the public sources carry no
 * assembly copied from the game: 2026-10-27, the repo audit). The original is an SHC "#pragma inline_asm" function. */
ASM_STUB(void func_8C02D528(NJS_POINT3 *p))
#endif
