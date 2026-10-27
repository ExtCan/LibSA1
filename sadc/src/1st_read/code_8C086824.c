#include "common.h"

/*
 * 8C086824-8C08684C: part of a hand-written SH-4 assembly module of float
 * colour / vector helpers (neighbours 8C0867B0 uses FTRV XMTRX / FIPR,
 * 8C0867F0 and 8C08684C use the same unscheduled FR0/FR1 + FPUL style).
 *
 * func_8C086824 packs four floats {a, r, g, b} into a 32-bit ARGB colour.
 * The original is not SHC output: it has no instruction scheduling
 * (FTRC immediately followed by STS FPUL), uses FR0/R1 as temporaries, and
 * post-increments R4 on the last (dead) load. SHC 5.1r03 schedules the
 * loads, uses FR3/R3/R5 and emits FMOV.S @R4 for the last component.
 * The body below is a C description only; the original assembly stays in
 * the build.
 */

#ifdef NON_MATCHING
Uint32 func_8C086824(Float *c) {
    Uint32 col;

    col = (Sint32)*c++ << 24;
    col |= (Sint32)*c++ << 16;
    col |= (Sint32)*c++ << 8;
    return col | (Sint32)*c++;
}
#endif
