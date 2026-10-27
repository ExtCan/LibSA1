#include "common.h"

/*
 * func_8C085F68: table-based arcsine, returns an Angle (0x4000 = 90 degrees).
 * D_8C0861A0 is the centre of a 513-entry Sint16 table (index -256..256,
 * values -0x4000..0x4000) that lives inline in the code section right after
 * this function's literal pool.
 *
 * HAND-WRITTEN ASSEMBLY, not SHC output: a second, undetected entry point at
 * 8C0863A4 (between the arcsine table and an arccosine table) does
 *     MOVA  <acos table centre 8C0865AC>,R0 ; BRA 8C085F6A ; MOV R0,R5
 * i.e. it jumps into the middle of this function (just after the table
 * address is loaded into R5) to share the interpolation code. The body also
 * has a pointless FMOV FR4,FR3 copy. Left as assembly; the C below is a
 * functional equivalent only.
 */

extern Sint16 D_8C0861A0[];

#ifdef NON_MATCHING
Sint32 func_8C085F68(Float x) {
    Float f;
    Sint32 i;
    Sint16 *tbl;

    tbl = D_8C0861A0;
    f = 256.0f * x;
    i = (Sint32)f;
    f -= (Float)i;
    return (Sint32)((Float)(tbl[i + 1] - tbl[i]) * f) + tbl[i];
}
#endif
