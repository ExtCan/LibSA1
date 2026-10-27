#include "common.h"

/*
 * 8C010320-8C010340: part of a hand-written SH-4 assembly module
 * (8C010320-8C010400, see docs/compiler.md: no 00 09 pool padding; the pool
 * of func_8C01032E is preceded by a 00 00 fill halfword, which SHC 5.1r03
 * never emits). Small CPU helpers: set SR, set/get the stack pointer, and
 * cache enable.
 *
 * func_8C010320 is byte-identical when written with the SHC intrinsic
 * set_cr() (machine.h: _builtin_set_cr). The other three cannot be produced
 * by SHC from C; their bodies below are C descriptions only, kept under
 * NON_MATCHING, and the original assembly stays in the build.
 */

/* SHC intrinsic (machine.h set_cr): LDC Rn,SR */
extern void _builtin_set_cr(int cr);

/* set_cr(sr): LDC R4,SR; RTS; NOP */
void func_8C010320(int sr) {
    _builtin_set_cr(sr);
}

#ifdef NON_MATCHING
/* Sets the stack pointer: RTS; MOV R4,R15. No C equivalent (SHC has no
 * intrinsic that writes R15). */
void func_8C010326(void *sp) {
}

/* Returns the current stack pointer: RTS; MOV R15,R0. No C equivalent; the
 * address of a local is the closest approximation. */
void *func_8C01032A(void) {
    int sp;

    return (void *)&sp;
}

/* Cache enable: CCR (0xFF00001C) = 0x105 (ICE | CB | OCE). The original loads
 * both constants from the pool into R0/R1; SHC builds 0x105 with
 * MOV #1/SHLL8/ADD #5 in R3 and uses R2 for the address. */
void func_8C01032E(void) {
    *(volatile Uint32 *)0xFF00001C = 0x105;
}
#endif
