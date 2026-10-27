/*
 * pc_runtime.c - the few libsa1 runtime pieces that TARGET_PC decompiled code
 * references (harness --pc builds). The libsa1 implementations live in
 * libsa1/src; these are the minimal versions the oracle needs.
 */
#include <stdint.h>

#include "sh4.h"

/* emulated TMU channel 1 (code_8C06C0C0.c / code_8C06C1D8.c under TARGET_PC);
 * the simulator HLEs the frame-time reads, so a counter that never moves is
 * what the original sees there too */
typedef struct {
    uint8_t tstr;
    uint16_t tcr1;
    uint32_t tcor1;
    uint32_t tcnt1;
} SA1_TMU;
SA1_TMU sa1_tmu;

uint32_t sa1_tmu_tcnt1(void)
{
    return sa1_tmu.tcnt1;
}

/* fraction of n: n - (float)(int)n */
float njFraction(float n)
{
    return n - (float)(int32_t)n;
}

/* libsa1's game-detected fatal state (LIB/src/world/sa1_world.h): TARGET_PC code calls it where the
 * original would hang, e.g. PC_ONLY(sa1g_fatal(...)) in CCL_Init's endless "CANT GET COLLI_WORK" loop
 * (src/1st_read/code_8C02F27C.c). The harness reports it as a fault of the running call, as the lowered
 * code's fatal stubs do (harness/tools/gen_ga.py sa1g_unimpl); it never returns. */
void sa1g_fatal(uint32_t dc_func, const char *what)
{
    sh4_fault(&SH4, SH4_FAULT_HOOK, dc_func, "native fatal at %08X: %s", dc_func, what);
    for (;;) {
    }
}
