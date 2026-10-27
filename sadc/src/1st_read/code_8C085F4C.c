#include "common.h"

/*
 * 8C085F4C-8C085F68: part of a hand-written SH-4 assembly math module
 * (starts at 8C085A50, whose atan2-like routine has several entry points and
 * a BRAF into shared tails). Evidence that this is not SHC 5.1r03 output:
 * the function starts right after the 8C085F14 pool without the 32-byte
 * object alignment, the module uses little-endian 09 00 / 00 00 alignment
 * fill (SHC 5.1r03 -code=machinecode writes 00 09), the FPUL round trip goes
 * through R0 and FR4 (the parameter) is overwritten in place, and the branch
 * is a plain BT to a separate RTS; NOP. SHC always produces FLDI0 into a
 * scratch register, R2/R3 for the FTRC temp and BT/S or BF + RTS.
 * The body below is a C description only; the original assembly stays in
 * the build.
 *
 * Original:
 *     FLDI0 FR0; FCMP/EQ FR5,FR0; BT ret
 *     FMOV FR4,FR0; FDIV FR5,FR4; FTRC FR4,FPUL; STS FPUL,R0; LDS R0,FPUL
 *     FLOAT FPUL,FR4; FMUL FR5,FR4; RTS; FSUB FR4,FR0
 *   ret: RTS; NOP
 */

#ifdef NON_MATCHING
/* fmodf-like: a - trunc(a / b) * b, or 0 when b is 0 */
Float func_8C085F4C(Float a, Float b) {
    if (b == 0.0f) {
        return 0.0f;
    }
    return a - (Float)(Sint32)(a / b) * b;
}
#endif
