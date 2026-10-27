#include "common.h"

/*
 * 8C08684C-8C08690A: hand-written SH-4 assembly (not SHC output), part of the
 * assembly module of float colour helpers that also holds 8C0867B0, 8C0867F0
 * and 8C086824 (the routine starts right after 8C086824, at an address that is
 * not 32-byte aligned, with no gap).
 * - The alignment fill after the RTS delay slot is "09 00" (H'0009, asmsh
 *   .ALIGN style); SHC 5.1r03 pads with "00 09" (see docs/compiler.md).
 * - The 0xFF mask is built once in R3 (MOV #-1 / EXTU.B) and applied with
 *   MOV R3,Rn / AND; SHC compiles "& 0xFF" to EXTU.B Rm,Rn.
 * - All four constants are loaded into FR0/FR3/FR4/FR5 up front and the code
 *   is completely unscheduled; SHC interleaves the channels, uses FMAC for
 *   "f * 255.0f + 0.5f" and folds (x * k) * (y * k) into (x * y) * (k * k).
 * The body below is a C description of what the assembly does; it is kept
 * under NON_MATCHING only and the original assembly stays in the build.
 */

#ifdef NON_MATCHING
/* Modulates two packed 8:8:8:8 colours channel by channel:
 * each channel = min(ca / 255 * cb / 255, 1) * 255 + 0.5. */
Uint32 func_8C08684C(Uint32 a, Uint32 b) {
    Float fa;
    Float fb;
    Uint32 r;

    fa = (Float)(Sint32)(a & 0xFF) * (1.0f / 255.0f);
    fb = (Float)(Sint32)(b & 0xFF) * (1.0f / 255.0f);
    fa *= fb;
    if (fa >= 1.0f) {
        fa = 1.0f;
    }
    r = (Sint32)(fa * 255.0f + 0.5f);
    a >>= 8;
    b >>= 8;
    fa = (Float)(Sint32)(a & 0xFF) * (1.0f / 255.0f);
    fb = (Float)(Sint32)(b & 0xFF) * (1.0f / 255.0f);
    fa *= fb;
    if (fa >= 1.0f) {
        fa = 1.0f;
    }
    r |= (Sint32)(fa * 255.0f + 0.5f) << 8;
    a >>= 8;
    b >>= 8;
    fa = (Float)(Sint32)(a & 0xFF) * (1.0f / 255.0f);
    fb = (Float)(Sint32)(b & 0xFF) * (1.0f / 255.0f);
    fa *= fb;
    if (fa >= 1.0f) {
        fa = 1.0f;
    }
    r |= (Sint32)(fa * 255.0f + 0.5f) << 16;
    a >>= 8;
    b >>= 8;
    fa = (Float)(Sint32)(a & 0xFF) * (1.0f / 255.0f);
    fb = (Float)(Sint32)(b & 0xFF) * (1.0f / 255.0f);
    fa *= fb;
    if (fa >= 1.0f) {
        fa = 1.0f;
    }
    r |= (Sint32)(fa * 255.0f + 0.5f) << 24;
    return r;
}
#endif
