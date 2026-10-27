#include "common.h"
#include "ninja.h"
#include "sa1/shc_intrinsics.h"

/*
 * 8C02CF70-8C02CFA4: two squared-distance helpers from the hand-written
 * SH-4 assembly module 8C02CE80-8C02D980 (docs/compiler.md lists it among
 * the regions without SHC's 32-byte pool alignment; its neighbours
 * 8C02CF20/8C02CF48 share a BRA and a module-local data block, and
 * 8C02CF68 is a bare "FLDI0 FR7; FIPR FV4,FV4; RTS; FMOV FR7,FR0" tail).
 * Not SHC output:
 * - func_8C02CF8A starts at a 2-byte-aligned address (8C02CF8A); SHC
 *   aligns every function to at least 4 and every object to 32, and a
 *   "#pragma inline_asm" function would add ".ALIGN 4 / RTS / NOP";
 * - both vectors are read straight from @R4+/@R5+ (ADD #4 skips y in
 *   func_8C02CF8A), the differences are formed in place in FV4 with FR0 as
 *   scratch, FLDI0 fills the unused lanes and FIPR FV4,FV4 is used
 *   directly; SHC's fipr() intrinsic always copies both operands through
 *   stack arrays into FV0/FV4 and the plain C expression compiles to
 *   FMUL/FMAC;
 * - FMOV FR7,FR0 sits in the RTS delay slot.
 * Same routines as func_8C085A1C/func_8C085A36 (code_8C085A1C.c). The
 * bodies below are C descriptions (PC implementation); the original
 * assembly stays in the build.
 */

#ifdef NON_MATCHING
/* Squared distance between points a and b: |a - b|^2. */
Float func_8C02CF70(NJS_POINT3 *a, NJS_POINT3 *b) {
    float t[4];

    t[0] = a->x - b->x;
    t[1] = a->y - b->y;
    t[2] = a->z - b->z;
    t[3] = 0.0f;
    return _builtin_fipr(t, t);
}

/* Squared XZ-plane distance between points a and b (y ignored). */
Float func_8C02CF8A(NJS_POINT3 *a, NJS_POINT3 *b) {
    float t[4];

    t[0] = a->x - b->x;
    t[1] = 0.0f;
    t[2] = a->z - b->z;
    t[3] = 0.0f;
    return _builtin_fipr(t, t);
}
#endif
