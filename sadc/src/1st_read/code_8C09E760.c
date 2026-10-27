/*
 * Float setter (1ST_READ 8C09E760-8C09E76C); the matching getter is
 * func_8C09E76C. Called from func_8C09E0B8 with a per-entry Float (+0x40).
 *
 * NON_MATCHING: the original keeps the literal-pool address in R0
 *   MOV.L L,R0 ; RTS ; FMOV.S FR4,@R0
 * SHC 5.1r03 (and every other SHC version in tools/shc, with -size/-speed,
 * and with pointer/array/return-type variants) allocates R3 (or R4) instead.
 * The address range 8C09E760-8C09F080 is one of the non-standard modules in
 * docs/compiler.md (no 32-byte pool alignment; the pool at 8C09E768 directly
 * follows the code): hand-written assembly or a differently built object.
 * The C below is complete and functionally identical.
 */
#include "common.h"

extern Float D_8C66C314;

#ifdef NON_MATCHING
void func_8C09E760(Float f) {
    D_8C66C314 = f;
}
#else
ASM_STUB(void func_8C09E760(Float f))
#endif
