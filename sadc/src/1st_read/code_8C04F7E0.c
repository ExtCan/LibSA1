/*
 * 1ST_READ 8C04F7E0: GetStageNumber (chunk 8C04D920-8C04FEA0, function 35/47).
 *
 * Returns the current stage as (CurrentLevel << 8) | CurrentAct. The return
 * type is Uint16: the value is not extended here and the callers zero-extend
 * it (EXTU.W R0).
 *
 * NON_MATCHING: register allocation only. The instructions, their order and
 * the literal-pool loads are identical, but SHC gives
 *     orig: level address R2, act address R1, act value R3
 *     mine: level address R3, act address R1, act value R2
 * The siblings func_8C04F7EE (B_8C7493B6/B_8C7493B8) and func_8C04F840 use
 * the same R2/R1/R3 pattern, so the original idiom produces it every time.
 * Tried without success (about 120 variants): operand order, | vs +, << 8 vs
 * * 256, casts on each operand, Sint32/Sint16/Uint16/Uint32/int/short locals
 * for the level, the act, both, or the result (the GetPlayerNumber idiom
 * "n = global; return n"), pointer locals, register, Sint32/Sint16 return
 * types, redundant masks, absolute addresses, defined/static globals,
 * swapped declaration order, a preceding func_8C04F7C2 in the file, extra
 * declarations (label numbering) and -nospeed/-size. The closest variant,
 * "Sint16 act; act = CurrentAct; return (CurrentLevel << 8) | act;", gets
 * the level address into R2 but puts act in R3/R4 (3 instructions differ).
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/engine.h"

#ifdef NON_MATCHING
Uint16 GetStageNumber(void)
{
    return (Uint16)((CurrentLevel << 8) | CurrentAct);
}
#else
ASM_STUB(Uint16 GetStageNumber(void))
#endif
