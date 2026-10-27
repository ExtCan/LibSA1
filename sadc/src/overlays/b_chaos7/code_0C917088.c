/*
 * B_CHAOS7 0C916EE0 (Perfect Chaos boss): chunk 0C916EE0-0C917F00,
 * function 4 of 16 (the others, the boss main 0C917300 among them, are
 * still assembly).
 *
 *  func_0C917088  [SADX SuperSonic_SubtractRings] the Super Sonic ring
 *                 drain. Called (BSR) only from the boss main 0C917300 at
 *                 0C917650 with R4 = the boss task's taskwk (R14 =
 *                 tp->twp, 0C917318), in one fight state while the float at
 *                 +0x70 of a saved pointer is > 0 (0C917640-0C91764E).
 *                 While player 0 has rings it counts the calls in
 *                 twp->timer.w[1] (+0x0E) and takes one ring every 60th
 *                 call (func_8C08A198(-1)); at 0 rings it clears the ring
 *                 count (func_8C08A074) and sends request 47 to player 0
 *                 (back to normal Sonic: code_8C0A9660.c clears
 *                 equipment & 0x8000). super_sonic.md 1.6 and Review R.1.
 *
 * Code generation: the counter is incremented as its own statement (the
 * asm stores it and loads it again for the compare); both calls of the
 * revert path and the -1 call are tail calls (JMP).
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"

/* 1ST_READ functions not declared in the sa1 headers */
Sint16 func_8C08A2BC(void);             /* ring count B_8C752BE4 (code_8C08A198.c) */
void func_8C08A074(void);               /* ring count = 0 (code_8C089F20.c) */

/* [SADX SuperSonic_SubtractRings] */
void func_0C917088(taskwk *twp)
{
    if (func_8C08A2BC() > 0) {
        twp->timer.w[1]++;
        if (twp->timer.w[1] >= 60) {
            twp->timer.w[1] = 0;
            func_8C08A198(-1);
        }
    } else {
        func_8C08A074();
        SetInputP(0, 47);
    }
}
