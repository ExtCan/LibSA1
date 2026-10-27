#include "common.h"
#include "ninja.h"

/*
 * 8C085A00-8C085A1C: part of the hand-written SH-4 assembly module of small
 * vector helpers (see code_8C08594C.c / code_8C0859E4.c). Not SHC output:
 * - both points are read straight from @R4+/@R5+ (skipping y with ADD #4)
 *   and the differences are formed in FR0/FR2 with FLDI0 FR1/FR3 for the
 *   unused lanes, then FIPR FV0,FV0 is used directly (SHC's fipr()
 *   intrinsic always copies both operands through stack arrays into FV0/FV4
 *   and emits FIPR FV4,FV0);
 * - the z difference is computed as b->z - a->z (FSUB FR4,FR2 with FR2 =
 *   b->z) while x is a->x - b->x;
 * - FMOV FR3,FR0 is scheduled before the RTS and FSQRT FR0 sits in the
 *   delay slot.
 * The body below is a C description (XZ-plane distance between two points);
 * the original assembly stays in the build.
 */

#ifdef NON_MATCHING
/* SHC intrinsics (machine.h fipr / sqrtf) */
extern float _builtin_fipr(float vect1[4], float vect2[4]);
extern float _builtin_sqrtf(float x);

/* Returns the XZ-plane distance between points a and b (y ignored). */
Float func_8C085A00(NJS_POINT3 *a, NJS_POINT3 *b) {
    float t[4];

    t[0] = a->x - b->x;
    t[1] = 0.0f;
    t[2] = b->z - a->z;
    t[3] = 0.0f;
    return _builtin_sqrtf(_builtin_fipr(t, t));
}
#endif
