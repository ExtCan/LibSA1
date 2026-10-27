/*
 * Input module: read the soft-reset flag D_8C19E3E0 (1ST_READ 8C050A06).
 * D_8C19E3E0 is set once per frame by func_8C050A40 (code_8C050A40.c) when
 * A+B+X+Y+Start is held on a port. Called by func_8C04CA40 (through a pointer
 * in its literal pool), func_8C04DA20, func_8C08F28A and func_8C08F780; the
 * result is tested against zero.
 *
 * NON_MATCHING only because of translation-unit context: the original object
 * is 8C0509A0-8C050A40 (all of code_8C0509A0.c plus this function, sharing
 * one literal pool). SHC continues its scratch-register rotation from the
 * previous function of the same file: compiled alone this body loads the
 * address into R3; after a preceding getter (func_8C0509B0/func_8C0509FC
 * shape) it uses R2 exactly as the original does, after a setter R1.
 * Source-only variants (pointer local, casts, return types, volatile, dead
 * loads, #pragma inline helpers, extra headers) all stay on R3.
 * Appending this function to code_8C0509A0.c after func_8C0509FC gives a
 * MATCH (verified with shcdiff).
 */
#include "common.h"

extern Sint8 D_8C19E3E0;        /* signed byte: MOV.B with no EXTU.B in the getter */

#ifdef NON_MATCHING
Sint32 func_8C050A06(void) {
    return D_8C19E3E0;
}
#else
ASM_STUB(Sint32 func_8C050A06(void))
#endif
