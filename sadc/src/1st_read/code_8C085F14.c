#include "common.h"
#include "ninja.h"

/*
 * 8C085F14-8C085F4C: part of the hand-written SH-4 assembly module of small
 * vector helpers (starts at 8C0858C0; see code_8C08593C.c, code_8C085A1C.c,
 * code_8C085ADE.c). Not SHC output:
 * - starts at a non-32-byte-aligned address (SHC objects are ALIGN=32),
 *   right after the atan2 table of func_8C085A50, and the fill in front of
 *   it is "09 00" (asmsh style; SHC 5.1r03 -code=machinecode pads "00 09");
 * - x and y are read with two FMOV.S @R4+ loads although the pointer is dead
 *   afterwards (SHC drops the dead second increment: "@R4+ / @R4");
 * - every pool address is loaded into R1 right before its use and FR0 is the
 *   product scratch; SHC hoists the pool loads into R1-R3 and reorders the
 *   products as (scale * y) * r;
 * - RTS has an unfilled NOP delay slot although the last FMOV.S FR0,@-R5
 *   could fill it (SHC always moves the final store into the delay slot).
 * Best attempts with SHC differ in 20+ of 21 instructions. The body below is
 * a C description (perspective projection of a view-space point:
 * r = B_8C88F5E4 / z, out = (B_8C88F618 * r * x, B_8C88F61C * r * y, z));
 * the original assembly stays in the build.
 */

#ifdef NON_MATCHING
extern Float B_8C88F5E4;
extern Float B_8C88F618;
extern Float B_8C88F61C;

void func_8C085F14(NJS_POINT3 *src, NJS_POINT3 *dst) {
    Float r;

    dst->z = src->z;
    r = B_8C88F5E4 / src->z;
    dst->y = B_8C88F61C * r * src->y;
    dst->x = B_8C88F618 * r * src->x;
}
#endif
