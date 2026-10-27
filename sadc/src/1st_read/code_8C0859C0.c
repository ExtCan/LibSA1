#include "common.h"
#include "ninja.h"

/*
 * 8C0859C0-8C0859E4: part of the hand-written SH-4 assembly module of small
 * vector helpers (see code_8C08594C.c / code_8C085980.c). Not SHC output:
 * - it uses FSRRA FR7 / FSRRA FR11 (F77D / FB7D, left as .DATA.W by the
 *   disassembler), an instruction SHC 5.1 has no intrinsic for and never emits;
 * - both vectors are loaded straight from @R4+ / @R5+ into FV4 / FV8 with
 *   FLDI0 for the w lanes and FIPR FV4,FV4 / FV8,FV8 / FV4,FV8 are used
 *   directly (SHC's fipr() intrinsic always copies both operands through
 *   stack arrays into FV0/FV4);
 * - the last FMUL sits in the RTS delay slot and the next routine
 *   (8C0859E4) follows with no 32-byte alignment (SHC objects are ALIGN=32).
 * The original assembly stays in the build.
 */

/* SHC intrinsics (machine.h fipr / sqrtf) */
extern float _builtin_fipr(float vect1[4], float vect2[4]);
extern float _builtin_sqrtf(float x);

#ifdef NON_MATCHING
/* Cosine of the angle between two vectors: (a . b) / (|a| * |b|). */
Float func_8C0859C0(NJS_VECTOR *a, NJS_VECTOR *b) {
    float va[4];
    float vb[4];
    float r;

    va[0] = a->x;
    va[1] = a->y;
    va[2] = a->z;
    va[3] = 0.0f;
    vb[0] = b->x;
    vb[1] = b->y;
    vb[2] = b->z;
    vb[3] = 0.0f;
    r = 1.0f / _builtin_sqrtf(_builtin_fipr(va, va));
    r *= 1.0f / _builtin_sqrtf(_builtin_fipr(vb, vb));
    return r * _builtin_fipr(va, vb);
}
#endif
