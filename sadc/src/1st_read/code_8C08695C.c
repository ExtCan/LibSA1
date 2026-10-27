#include "common.h"

/*
 * 8C08695C-8C0869A4: part of a hand-written SH-4 assembly module of float
 * colour helpers (8C0867B0-8C0869E0: neighbours 8C08684C/8C086918 use the
 * same unscheduled FR0/FR1 + FPUL style, 8C0869A4/8C0869B0 are DT-loop
 * memset routines).
 *
 * func_8C08695C converts a float colour {a, r, g, b} in [0,1] to a packed
 * 32-bit ARGB value: each channel is FTRC(c * 255.0f + 0.5f) (one FMAC).
 * The original is not SHC output: it has no instruction scheduling (each
 * channel is load/FMAC/FTRC/STS/shift/OR in strict sequence), keeps the
 * loaded value in FR0 for FMAC, and post-increments R4 on the last (dead)
 * load. SHC 5.1r03 interleaves the four channels, puts the 255.0f constant
 * in FR0, and emits FMOV.S @R4 for the last component.
 * The body below is a C description only; the original assembly stays in
 * the build.
 */

#ifdef NON_MATCHING
Uint32 func_8C08695C(Float *argb) {
    Uint32 col;

    col = (Uint32)SA1_FTOI(SA1_FMAC(*argb++, 255.0f, 0.5f)) << 24;
    col |= (Uint32)SA1_FTOI(SA1_FMAC(*argb++, 255.0f, 0.5f)) << 16;
    col |= (Uint32)SA1_FTOI(SA1_FMAC(*argb++, 255.0f, 0.5f)) << 8;
    col |= (Uint32)SA1_FTOI(SA1_FMAC(*argb++, 255.0f, 0.5f));
    return col;
}
#endif
