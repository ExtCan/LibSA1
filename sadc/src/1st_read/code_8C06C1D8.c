#include "common.h"
#include "sa1/ninja_funcs.h"

/*
 * 8C06C1D8: per-frame callback registered by func_8C06C0C0
 * (func_8C64133E(func_8C06C1D8, 0)). func_8C06C0C0 programs TMU channel 1
 * (TCOR1 = 12500000, i.e. a 1 s period at Pphi/4) and starts it; this
 * callback samples the channel 1 counter TCNT1 (0xFFD80018) into
 * B_8C78C604, calls the SDK function func_8C604546 and then the game's
 * mode dispatcher func_8C0108EA (tail call).
 *
 * Matching: the counter is read into a local first, through a non-volatile
 * pointer (a volatile read, or a direct global assignment without the local,
 * keeps the store out of the JSR delay slot).
 *
 * TARGET_PC: the TMU does not exist; the counter is read through the libsa1
 * runtime (sa1_tmu_tcnt1, emulated timer), never through the DC address.
 */

#if defined(TARGET_PC)
Uint32 sa1_tmu_tcnt1(void);
#define TMU_TCNT1 (sa1_tmu_tcnt1())
#else
#define TMU_TCNT1 (*(Uint32 *)0xFFD80018)
#endif

extern Uint32 B_8C78C604;   /* last TMU1 counter sample */

void func_8C0108EA(void);   /* code_8C010400.c: game mode dispatcher */

void func_8C06C1D8(void)
{
    Uint32 count;

    count = TMU_TCNT1;
    B_8C78C604 = count;
    func_8C604546();
    func_8C0108EA();
}
