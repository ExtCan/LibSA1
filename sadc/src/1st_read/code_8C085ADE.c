#include "common.h"

/*
 * 8C085ADE-8C085AEA: not real functions. Hand-written SH-4 assembly (not SHC
 * output).
 *
 * func_8C085A50 is a hand-written atan2-style routine (njArcTan2-like:
 * FR4 = y, FR5 = x, returns an Angle in R0). It folds the arguments into the
 * first octant, builds a 3-bit octant code in R1 (+8 x<0, +4 y<0, +2 swap),
 * looks up a byte offset in the .DATA.W table at 8C085AF8, interpolates the
 * angle from the 0x2000-entry table at 8C085B10 into R2 and then does
 * "BRAF R1; ADD R3,R2". The labels func_8C085AB6 .. func_8C085AE6 are just
 * the BRAF targets (the per-octant fix-up tails), each of which reads its
 * input from R2 -- a register SHC never uses for arguments:
 *
 *   func_8C085ADE:  MOV.L #0x4000,R0; SUB R0,R2; RTS; MOV R2,R0   -> r2 - 0x4000
 *   func_8C085AE6:  RTS; NEG R2,R0                                 -> -r2
 *
 * They share one literal pool with func_8C085A50 and use MOVA/BRAF tricks
 * SHC 5.1 does not generate, so they cannot be produced from C. The original
 * assembly stays in the build. The bodies below only describe the behaviour
 * (with the R2 input shown as a parameter) and are kept under NON_MATCHING.
 */

#ifdef NON_MATCHING
/* Octant fix-up tail of func_8C085A50: angle - 0x4000 (input in R2). */
Sint32 func_8C085ADE(Sint32 ang) {
    return ang - 0x4000;
}

/* Octant fix-up tail of func_8C085A50: -angle (input in R2). */
Sint32 func_8C085AE6(Sint32 ang) {
    return -ang;
}
#endif
