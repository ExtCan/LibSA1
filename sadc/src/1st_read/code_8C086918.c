#include "common.h"

/*
 * 8C086918-8C08695C: part of the hand-written SH-4 assembly module of float
 * colour / vector helpers (8C0867B0 uses FTRV/FIPR, 8C086824, 8C08684C and
 * 8C08695C use the same unscheduled FR0/R0/R1 + FPUL style, 8C08695C uses
 * FMAC).
 *
 * func_8C086918 unpacks a packed 0xAARRGGBB colour into four floats
 * {a, r, g, b}, each channel scaled by 1/255 (0x3B808080).
 * The original is not SHC output: it has no instruction scheduling
 * (LDS/FLOAT/FMUL/FMOV.S back to back for each channel), keeps 1/255 in FR3
 * and 0xFF in R1 and uses R0/FR0 as temporaries. SHC 5.1r03 (-speed)
 * interleaves the integer and float work, uses EXTU.B for & 0xFF (or R3/R6
 * when the mask is a variable) and FR3/FR4 for the float values.
 * The body below is a C description only; the original assembly stays in
 * the build.
 */

#ifdef NON_MATCHING
void func_8C086918(Uint32 color, Float *argb) {
    Float scale;
    Uint32 mask;

    scale = 1.0f / 255.0f;
    mask = 0xFF;
    argb += 4;
    *--argb = (Float)(Sint32)(color & mask) * scale;
    color >>= 8;
    *--argb = (Float)(Sint32)(color & mask) * scale;
    color >>= 8;
    *--argb = (Float)(Sint32)(color & mask) * scale;
    color >>= 8;
    *--argb = (Float)(Sint32)color * scale;
}
#endif
