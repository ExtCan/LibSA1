#include "common.h"
#include "sa1/shc_intrinsics.h"

/*
 * 1ST_READ 8C02CF68-8C02CF70: squared length of the vector (x, y, z),
 * x*x + y*y + z*z, taken as three Float arguments in FR4-FR6.
 *
 * Hand-written SH-4 assembly, not SHC output (same style as the FIPR
 * distance helpers that follow it at 8C02CF70/8C02CF8A): the arguments
 * already sit in FV4, FLDI0 FR7 clears the w lane, FIPR FV4,FV4 does the
 * dot product and FMOV FR7,FR0 is in the RTS delay slot.
 *   - Plain C (x * x + y * y + z * z) compiles to FMUL/FMAC.
 *   - SHC's _builtin_fipr always copies two stack arrays into FV0/FV4.
 *   - "#pragma inline_asm" reproduces the body but SHC appends its own
 *     RTS/NOP, giving 12 bytes instead of 8.
 * The original assembly stays in the build; the C below is the
 * NON_MATCHING / TARGET_PC implementation.
 */

#ifdef NON_MATCHING
Float func_8C02CF68(Float x, Float y, Float z) {
    float t[4];

    t[0] = x;
    t[1] = y;
    t[2] = z;
    t[3] = 0.0f;
    return _builtin_fipr(t, t);
}
#endif
