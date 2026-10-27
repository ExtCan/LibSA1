#include "common.h"
#include "ninja.h"

/*
 * 8C085A1C-8C085A50: part of the hand-written SH-4 assembly module of small
 * vector helpers (starts at 8C0858C0; see code_8C08593C.c, code_8C0859E4.c).
 * Not SHC output:
 * - starts at a non-32-byte-aligned address (SHC objects are ALIGN=32) and
 *   the two functions are packed back to back inside the module;
 * - both vectors are read straight from @R4+/@R5+ (with ADD #4 skipping y in
 *   func_8C085A36) and the differences are formed directly in FR0-FR2 with
 *   FR4 as scratch and FLDI0 filling the unused lanes, then FIPR FV0,FV0 is
 *   used directly; SHC's fipr() intrinsic always copies both operands
 *   through stack arrays into FV0/FV4 and emits FIPR FV4,FV0, and the plain
 *   C expression compiles to FMUL/FMAC;
 * - FMOV FR3,FR0 sits in the RTS delay slot.
 * The bodies below are C descriptions; the original assembly stays in the
 * build.
 */

#ifdef NON_MATCHING
/* SHC intrinsic (machine.h fipr) */
extern float _builtin_fipr(float vect1[4], float vect2[4]);

/* Squared distance between points a and b: |a - b|^2. */
Float func_8C085A1C(NJS_POINT3 *a, NJS_POINT3 *b) {
    float t[4];

    t[0] = a->x - b->x;
    t[1] = a->y - b->y;
    t[2] = a->z - b->z;
    t[3] = 0.0f;
    return _builtin_fipr(t, t);
}

/* Squared XZ-plane distance between points a and b (y ignored). */
Float func_8C085A36(NJS_POINT3 *a, NJS_POINT3 *b) {
    float t[4];

    t[0] = a->x - b->x;
    t[1] = 0.0f;
    t[2] = a->z - b->z;
    t[3] = 0.0f;
    return _builtin_fipr(t, t);
}
#endif
