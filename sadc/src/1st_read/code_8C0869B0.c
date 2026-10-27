#include "common.h"

/*
 * 8C0869B0-8C0869D0: hand-written SH-4 assembly (not SHC output).
 *
 * - A NOP at 8C0869B6 pads the loop head to an 8-byte boundary (8C0869B8);
 *   SHC never emits alignment NOPs inside a function.
 * - The unsigned shift n >> 5 is MOV #-5,R0; SHLD R0,R6. SHC 5.1 (-speed)
 *   emits SHLR2/SHLR2/SHLR for an unsigned >> 5 and MOV #-5,R3; SHAD for a
 *   signed one, never through R0.
 * - DT sits after the seventh store, right before BF/S; SHC schedules it
 *   directly after the first store.
 * - The surrounding block (8C0867B0-8C0869E0) has 4-byte-aligned pools with
 *   no 32-byte .ALIGN, unlike code built with -extra=a=1800.
 *
 * The body below is a C description of what the assembly does (a 32-byte
 * unrolled backwards memset of 32-bit words). It stays under NON_MATCHING,
 * and the original assembly stays in the build.
 */

#ifdef NON_MATCHING
/* Fills n bytes (a nonzero multiple of 32) at dst with the word c, backwards from dst + n. */
void func_8C0869B0(Uint32 *dst, Uint32 c, Uint32 n) {
    dst = (Uint32 *)((Uint8 *)dst + n);
    n >>= 5;
    do {
        *--dst = c;
        *--dst = c;
        *--dst = c;
        *--dst = c;
        *--dst = c;
        *--dst = c;
        *--dst = c;
        *--dst = c;
    } while (--n != 0);
}
#endif
