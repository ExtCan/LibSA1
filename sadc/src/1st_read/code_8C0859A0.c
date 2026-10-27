#include "common.h"
#include "ninja.h"

/*
 * 8C0859A0-8C0859BE: part of the hand-written SH-4 assembly module of small
 * vector helpers (see code_8C08594C.c / code_8C085970.c). Not SHC output:
 * - the vector is loaded straight into FV0 (FMOV.S @R4+ with ADD #4,R4
 *   skipping y), FLDI0 FR1/FR3 fill the unused lanes and FIPR FV0,FV0 is
 *   used directly; SHC's _builtin_fipr always copies two stack arrays into
 *   FV0/FV4 (FIPR FV4,FV0) with a frame and saved registers;
 * - FSRRA FR3 (left as .DATA.W H'F37D by the disassembler) is not accepted by
 *   the SHC toolchain's assembler (ILLEGAL MNEMONIC for _builtin_fsrra output);
 * - the stores use ADD #12,R5 / FMOV.S @-R5 with ADD #-4,R5 to skip y, and
 *   FMOV FR3,FR0 sits in the RTS delay slot;
 * - alignment fill after the function is "09 00" (asmsh style).
 * The original assembly stays in the build.
 */

/* SHC intrinsics (machine.h fipr / sqrtf) */
extern float _builtin_fipr(float vect1[4], float vect2[4]);
extern float _builtin_sqrtf(float x);

#ifdef NON_MATCHING
/*
 * Normalizes a vector in the XZ plane: out->x = v->x * r, out->z = v->z * r
 * with r = 1 / sqrtf(x*x + z*z) (the original uses FSRRA, an approximate
 * reciprocal square root). out->y is left untouched. Returns r.
 */
Float func_8C0859A0(NJS_VECTOR *v, NJS_VECTOR *out) {
    float t[4];
    float r;

    t[0] = v->x;
    t[1] = 0.0f;
    t[2] = v->z;
    t[3] = 0.0f;
    r = 1.0f / _builtin_sqrtf(_builtin_fipr(t, t));
    out->z = t[2] * r;
    out->x = t[0] * r;
    return r;
}
#endif
