#include "common.h"
#include "ninja.h"

/*
 * 8C085A50-8C085ADE: hand-written SH-4 assembly (not SHC output).
 * (8C085ADE-8C085AEA, the last two tails, are in code_8C085ADE.c.)
 *
 * func_8C085A50 is an atan2 routine (njArcTan2-like: FR4 = y, FR5 = x,
 * returns an Angle in R0, 0x10000 = 360 degrees; (0, 0) gives 0x80000000).
 * It folds (x, y) into the first octant while building an octant code in R1
 * (+8 when x <= 0, +4 when y <= 0, +2 when |y| >= |x| and the two are
 * swapped), computes t = min/max * 512, interpolates the angle between
 * entries [(int)t] and [(int)t + 1] of a 513-entry Sint16 table at 8C085B10
 * (atan(i / 512) in Angle units, 0x0000 .. 0x2000), rounds with +0.5f, then
 * dispatches through the .DATA.W byte-offset table at 8C085AF8 with
 * "BRAF R1; ADD R3,R2". The other labels in this chunk are only the BRAF
 * targets (per-octant fix-up tails) and read the angle from R2, a register
 * SHC never uses for arguments:
 *
 *   octant code   offset  target          result
 *        0         0x04   func_8C085ABA   r2
 *        2         0x08   func_8C085ABE   0x4000 - r2
 *        4         0x30   func_8C085AE6   -r2
 *        6         0x28   func_8C085ADE   r2 - 0x4000
 *        8         0x14   func_8C085ACA   0x8000 - r2
 *       10         0x0E   func_8C085AC4   0x4000 + r2
 *       12         0x1A   func_8C085AD0   r2 - 0x8000
 *       14         0x22   func_8C085AD8   0xFFFFC000 - r2
 *
 * func_8C085AB6 (RTS; MOV #0,R0) is not referenced by the offset table
 * (offset 0 is never used): dead code inside the assembly module, not a
 * function of its own.
 *
 * Evidence that this is not SHC 5.1r03 output: the module starts at a
 * non-32-byte-aligned address (SHC objects are ALIGN=32); the pool fill at
 * 8C085AEA is 00 00 and the fill after the table (8C085F12) is "09 00"
 * (asmsh style; SHC -code=machinecode pads with "00 09"); the constant
 * 0x80000000 is built with MOV #1 + ROTR; 0x4000/0x8000/0xFFFFC000 are
 * MOV.L pool loads (SHC with -extra=a=1800 builds 16-bit constants with
 * MOV #imm + SHLL8); tables are addressed with MOVA
 * and the BRAF jumps into the tails of separately exported labels that take
 * their input in R2. The original assembly stays in the build. The bodies
 * below only describe the behaviour (with the R2 input shown as a parameter)
 * and are kept under NON_MATCHING.
 */

#ifdef NON_MATCHING
/* atan(i / 512) * 0x10000 / (2 * pi), i = 0..512 (0x0000 .. 0x2000) */
extern const Sint16 D_8C085B10[513];

/* njArcTan2-like: angle of (x, y), y first. */
Angle func_8C085A50(Float y, Float x) {
    Sint32 oct;
    Float t;
    Sint32 i;
    Sint32 a;
    Sint32 ang;

    if (y == 0.0f && x == 0.0f) {
        return (Angle)0x80000000;
    }
    oct = 0;
    if (!(x > 0.0f)) {
        x = -x;
        oct = 8;
    }
    if (!(y > 0.0f)) {
        y = -y;
        oct += 4;
    }
    if (!(x > y)) {
        t = y;
        y = x;
        x = t;
        oct += 2;
    }
    t = (y / x) * 512.0f;
    i = (Sint32)t;
    t -= (Float)i;
    a = D_8C085B10[i];
    ang = a + (Sint32)((Float)(D_8C085B10[i + 1] - a) * t + 0.5f);
    switch (oct) {
    case 0:
        return ang;
    case 2:
        return 0x4000 - ang;
    case 4:
        return -ang;
    case 6:
        return ang - 0x4000;
    case 8:
        return 0x8000 - ang;
    case 10:
        return 0x4000 + ang;
    case 12:
        return ang - 0x8000;
    default:
        return -0x4000 - ang;
    }
}

/* Unreferenced: RTS; MOV #0,R0. */
Sint32 func_8C085AB6(void) {
    return 0;
}

/* Octant fix-up tail of func_8C085A50: angle (input in R2). */
Sint32 func_8C085ABA(Sint32 ang) {
    return ang;
}

/* Octant fix-up tail of func_8C085A50: 0x4000 - angle (input in R2). */
Sint32 func_8C085ABE(Sint32 ang) {
    return 0x4000 - ang;
}

/* Octant fix-up tail of func_8C085A50: 0x4000 + angle (input in R2). */
Sint32 func_8C085AC4(Sint32 ang) {
    return 0x4000 + ang;
}

/* Octant fix-up tail of func_8C085A50: 0x8000 - angle (input in R2). */
Sint32 func_8C085ACA(Sint32 ang) {
    return 0x8000 - ang;
}

/* Octant fix-up tail of func_8C085A50: angle - 0x8000 (input in R2). */
Sint32 func_8C085AD0(Sint32 ang) {
    return ang - 0x8000;
}

/* Octant fix-up tail of func_8C085A50: 0xFFFFC000 - angle (input in R2). */
Sint32 func_8C085AD8(Sint32 ang) {
    return -0x4000 - ang;
}
#endif
