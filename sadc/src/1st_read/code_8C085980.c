#include "common.h"
#include "ninja.h"

/*
 * 8C085980-8C08599E: part of the hand-written SH-4 assembly module of small
 * vector helpers (see code_8C08594C.c / code_8C085970.c). Not SHC output:
 * - it uses FSRRA FR3 (F37D, left as .DATA.W by the disassembler), an
 *   instruction SHC 5.1 has no intrinsic for and never emits;
 * - the vector is loaded straight from @R4+ into FV0 with FLDI0 FR3
 *   interleaved and FIPR FV0,FV0 is used directly (SHC's fipr() intrinsic
 *   always copies both operands through stack arrays into FV0/FV4);
 * - the result is stored with ADD #12,R5 / FMOV.S @-R5 and FMOV FR3,FR0 sits
 *   in the RTS delay slot.
 * The original assembly stays in the build.
 */

/* SHC intrinsics (machine.h fipr / sqrtf) */
extern float _builtin_fipr(float vect1[4], float vect2[4]);
extern float _builtin_sqrtf(float x);

#ifdef NON_MATCHING
/* njUnitVector-like: out = v / |v|; returns 1 / |v| (FSRRA of x*x+y*y+z*z). */
Float func_8C085980(NJS_VECTOR *v, NJS_VECTOR *out) {
    float t[4];
    float r;
    float *o;

    t[0] = v->x;
    t[1] = v->y;
    t[2] = v->z;
    t[3] = 0.0f;
    r = 1.0f / _builtin_sqrtf(_builtin_fipr(t, t));
    o = &out->x;
    o += 3;
    *--o = t[2] * r;
    *--o = t[1] * r;
    *--o = t[0] * r;
    return r;
}
#endif
