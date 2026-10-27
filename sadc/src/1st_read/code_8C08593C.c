#include "common.h"
#include "ninja.h"

/*
 * 8C08593C-8C08594C: hand-written SH-4 assembly (not SHC output).
 *
 * - Vector length via FIPR FV0,FV0 on (x, y, z, 0): the operand is loaded
 *   straight from @R4+ into FR0-FR2 with FLDI0 FR3 interleaved, and the
 *   FSQRT sits in the RTS delay slot. SHC 5.1's _builtin_fipr/_builtin_sqrtf
 *   always copy both operands into two different vector registers
 *   (FIPR FV4,FV0, FSQRT FR3; FMOV.S FR3,FR0) and never produce this form.
 * - The routine starts at a non-32-byte-aligned address (SHC objects are
 *   ALIGN=32) and belongs to a block of similar hand-written vector routines
 *   (8C08594C, 8C085960, 8C085970, 8C085980 ...; the latter uses FSRRA,
 *   which the disassembler leaves as .DATA.W).
 *
 * The body below is a C description of what the assembly does (njScalor-like);
 * it is kept under NON_MATCHING only and the original assembly stays in the build.
 */

#ifdef NON_MATCHING
extern float _builtin_fipr(float vect1[4], float vect2[4]);
extern float _builtin_sqrtf(float x);

/* Returns |v| = sqrt(x*x + y*y + z*z). */
Float func_8C08593C(NJS_VECTOR *v) {
    float t[4];

    t[0] = v->x;
    t[1] = v->y;
    t[2] = v->z;
    t[3] = 0.0f;
    return _builtin_sqrtf(_builtin_fipr(t, t));
}
#endif
