#include "sa1/sa1.h"

/*
 * 1ST_READ 8C040A40-8C040A60 (last 32 bytes of the 8C03FDC0-8C040A60 chunk).
 *
 * func_8C040A40 is not a function: it is SHC's mid-file literal pool
 * (".ALIGN 32; BRA over the pool; NOP; pool; .ALIGN 32"), the same pattern
 * as 8C040020, 8C040380, 8C040660 and 8C040840 in this chunk (see
 * code_8C03FF90.c and code_8C040596.c). The 8 pool bytes at 8C040A44 hold
 * the call targets of func_8C0409EE (the last function before it):
 *   8C040A44  .DATA.L _func_8C605DAC   (JSR at 8C040A16)
 *   8C040A48  .DATA.L _func_8C605E0A   (tail JMP at 8C040A22)
 * The BRA at 8C040A40 is unreachable (func_8C0409EE ends with RTS at
 * 8C040A2A and the 8C040A2E-8C040A40 fill is .ALIGN 32 NOPs); it jumps to
 * the NOP fill at 8C040A4C that runs into 8C040A60.
 *
 * Because the pool is in the mid-file form (BRA + 00 09 NOP fill instead
 * of an end-of-file pool followed by lnk's 00 fill), the translation unit
 * that holds func_8C0409EE most likely continues into the 8C040A60 chunk,
 * i.e. 8C040A60 is not a file boundary. The bytes are produced by the
 * compiler while compiling that unit and cannot be written as C on their
 * own; they stay in the assembly chunk.
 */
