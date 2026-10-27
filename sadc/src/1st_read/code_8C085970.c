#include "common.h"
#include "ninja.h"

/*
 * 8C085970-8C085980: part of the hand-written SH-4 assembly module of small
 * vector helpers (see code_8C08594C.c). Not SHC output: the vector is loaded
 * straight into FV0 (FMOV.S @R4+ with ADD #4,R4 skipping y), FLDI0 fills the
 * unused lanes, FIPR FV0,FV0 is used directly and FMOV FR3,FR0 sits in the
 * RTS delay slot. SHC's fipr() intrinsic always goes through two memory
 * arrays on the stack (FMOV.S @R3+/@R2+ into FV0/FV4, FIPR FV4,FV0), and the
 * plain C expression compiles to FMUL/FMAC, so the code cannot be produced
 * from C. The original assembly stays in the build.
 */

/* SHC intrinsic (machine.h fipr) */
extern float _builtin_fipr(float vect1[4], float vect2[4]);

#ifdef NON_MATCHING
/* Squared XZ-plane length of a vector: x*x + z*z (y ignored). */
Float func_8C085970(NJS_VECTOR *v) {
    float t[4];

    t[0] = v->x;
    t[1] = 0.0f;
    t[2] = v->z;
    t[3] = 0.0f;
    return _builtin_fipr(t, t);
}
#endif
