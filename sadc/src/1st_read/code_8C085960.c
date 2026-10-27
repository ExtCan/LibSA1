#include "common.h"
#include "ninja.h"

/*
 * 8C085960-8C08596E: part of a hand-written SH-4 assembly module of small
 * vector helpers (starts at 8C0858C0). Not SHC output: the vector is loaded
 * straight into FV0 with FLDI0 FR3 for the w lane and FIPR FV0,FV0, and the
 * alignment fill after the function is "09 00" (asmsh style; SHC 5.1r03
 * -code=machinecode pads with "00 09"). SHC's fipr() intrinsic always reads
 * two memory arrays (FMOV.S @R3+/@R2+ into FV0/FV4), so this cannot be
 * produced from C. The original assembly stays in the build.
 */

/* SHC intrinsic (machine.h fipr) */
extern float _builtin_fipr(float vect1[4], float vect2[4]);

#ifdef NON_MATCHING
/* Squared length of a vector: x*x + y*y + z*z. */
Float func_8C085960(NJS_VECTOR *v) {
    float t[4];

    t[0] = v->x;
    t[1] = v->y;
    t[2] = v->z;
    t[3] = 0.0f;
    return _builtin_fipr(t, t);
}
#endif
