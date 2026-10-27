#include "common.h"

/*
 * 8C0867F0-8C086824: hand-written SH-4 assembly (not SHC output).
 *
 * - The alignment fill after the RTS is the little-endian NOP "09 00"
 *   (H'0009). SHC 5.1r03 objects always pad with "00 09" (H'0900, see
 *   docs/compiler.md); "09 00" is assembler output.
 * - The routine starts at a non-32-byte-aligned address (SHC objects are
 *   ALIGN=32) and sits in a block of hand-written routines: 8C0867B0 (FIPR
 *   matrix/vector code) before it and 8C086824 (the inverse conversion,
 *   floats -> packed color, same FR0-only style) after it.
 * - Every conversion goes through FR0 with no instruction interleaving and the
 *   0xFF mask is built once in R1 (MOV #-1 / EXTU.B) and ANDed via R0; SHC
 *   compiles "& 0xFF" to EXTU.B and schedules the FLOAT/FMOV chains with
 *   FR3/FR4, so this form cannot be reproduced from C.
 *
 * The body below is a C description of what the assembly does; it is kept
 * under NON_MATCHING only and the original assembly stays in the build.
 */

#ifdef NON_MATCHING
/* Unpacks a 0xAARRGGBB color into four floats {a, r, g, b} (NJS_ARGB-like, 0..255). */
void func_8C0867F0(Uint32 color, Float *argb) {
    argb += 4;
    *--argb = (Float)(Sint32)(color & 0xFF);
    color >>= 8;
    *--argb = (Float)(Sint32)(color & 0xFF);
    color >>= 8;
    *--argb = (Float)(Sint32)(color & 0xFF);
    color >>= 8;
    *--argb = (Float)(Sint32)color;
}
#endif
