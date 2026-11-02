/*
 * ADVERTISE 8C901200-8C901246 (chunk 8C901200).
 *
 *  func_8C901200  everything open (B_8C78B450[0]), else 1ST_READ 8C10A4E0
 *                 (not executed by the character select)
 *  func_8C901216  has the story of select slot n been started? (event flags
 *                 D_8C90EC78[n] = 0x48..0x4E); chooses dialog 6 / 7
 *
 * SPLIT NOTE: "func_8C901212" (RTS / NOP at 8C901212, no callers) is not a
 * function: it is the dead epilogue SHC emits after func_8C901200's tail
 * call (the C below compiles to exactly 8C901200-8C901216, 11 instructions).
 * The split found it as a gap seed. Until the split absorbs it into
 * func_8C901200, func_8C901200 stays under NON_MATCHING (shcdiff compares it
 * against 9 instructions).
 */
#include "common.h"
#include "sa1/sa1.h"

/* the menu's 18-byte block B_8C78B450..B_8C78B461 (cleared by 8C058646);
 * an array in the original (MOV.L / MOV.B register choice): [0] every
 * character open, [1] B_8C78B451, [0xA] Trial mode, [0xB] previous slot,
 * [0xD] selected select slot */
extern Uint8 B_8C78B450[18];
extern Sint32 D_8C90EC78[];     /* story-started event flag per slot */

Sint32 func_8C10A4E0(void);

#ifdef NON_MATCHING
Sint32 func_8C901200(void)
{
    if (B_8C78B450[0] != 0) {
        return 1;
    }
    return func_8C10A4E0();
}
#else
ASM_STUB(Sint32 func_8C901200(void))
#endif

Sint32 func_8C901216(Sint32 n)
{
    if (B_8C78B450[0] != 0) {
        return 1;
    }
    if (func_8C09068E(D_8C90EC78[n]) != 0) {
        return 1;
    }
    return 0;
}
