/*
 * 1ST_READ 8C0DF0D0 (last function of chunk 8C0DEEA0, Amy hammer-target
 * objects; the rest of the chunk is in code_8C0DEEA0.c).
 *  func_8C0DF0D0  clears B_8C7A4098, the remembered hammer-target object
 *                 (called by AmyMain).
 *
 * Matching note: in the original translation unit this function followed
 * func_8C0DF0CA (`return B_8C7A4098;`), and SHC's choice of scratch registers
 * depends on the functions compiled before it in the same file: alone it uses
 * R2/R3, after func_8C0DF0CA it uses R1/R2 like the original. The SHC-only
 * function sa1_regctx_8C0DF0D0 below reproduces that context (same body as
 * func_8C0DF0CA). It has no game address, so the splicer and progress tools
 * ignore it, and it is not compiled for TARGET_PC. Appending func_8C0DF0D0 to
 * code_8C0DEEA0.c instead also matches (verified) and would make it
 * unnecessary.
 */
#include "common.h"
#include "sa1/sa1.h"

extern task *B_8C7A4098;                /* object the player hit while facing it */

#if !defined(TARGET_PC)
task *sa1_regctx_8C0DF0D0(void)
{
    return B_8C7A4098;
}
#endif

void func_8C0DF0D0(void)
{
    B_8C7A4098 = NULL;
}
