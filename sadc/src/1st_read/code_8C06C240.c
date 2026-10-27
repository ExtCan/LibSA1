#include "common.h"
#include "sa1/sa1.h"

/*
 * 8C06C240-8C06C2E0: debug display of the 8 counters B_8C78C5E0[]
 * (set to -1 by func_8C06C0C0 in code_8C06C0C0.c), enabled by the
 * developer flag tcrf_SprTaskDisplayMode. Each counter that is > 0 is
 * printed as value + 1 with 4 digits at text location (35, 16 + i)
 * through func_8C640A80 (njPrintD-like).
 *
 * NON_MATCHING: 78 of 80 instructions match. The only difference is a
 * register-allocation choice: the original loads the address of
 * tcrf_SprTaskDisplayMode into R1 (MOV.L @(148,PC),R1; MOV.L @R1,R3),
 * this C gets R2. Writing the location as (i + 16) instead of the
 * separate counter y additionally swaps R4/R5 for the array base and its
 * dead copy, so the separate counter is closer to the original.
 * Tried without success: early return / goto / else forms of the guard,
 * a local copy or pointer to the flag, flag as pointer/array/struct/
 * unsigned, int vs Sint32, register, pointer-walking loops, while loops,
 * a parameter (task *tp), alternative location expressions.
 */

extern Sint32 tcrf_SprTaskDisplayMode; /* 8C1BDFD8 developer flag (TCRF name) */
extern Sint32 B_8C78C5E0[8];           /* debug counters, -1 = unused */

#ifdef NON_MATCHING
void func_8C06C240(void)
{
    Sint32 i;
    Sint32 y;

    if (tcrf_SprTaskDisplayMode != 0) {
        y = 16;
        for (i = 0; i < 8; i++) {
            if (B_8C78C5E0[i] > 0) {
                func_8C640A80(y | 0x230000, B_8C78C5E0[i] + 1, 4);
            }
            y++;
        }
    }
}
#else
ASM_STUB(void func_8C06C240(void))
#endif
