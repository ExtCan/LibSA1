#include "common.h"
#include "task.h"

/* GetStageNumber returns (level << 8) | act. Its prototype is
 * 'Uint16 GetStageNumber(void)' (sa1/engine.h: callers EXTU.W R0), but the
 * matched code below shifts the raw R0 without EXTU.W, which SHC only emits
 * for an int result, so SHC keeps the Sint32 declaration
 * (build/verify/canonical_protos.json). Same value: (Uint8)(x >> 8) only
 * uses bits 8-15. */
#if !defined(TARGET_PC)
extern Sint32 GetStageNumber(void);
#else
extern Uint16 GetStageNumber(void);
#endif
extern Sint32 rand(void); /* rand() */
extern taskwk *func_8C015820(Float x, Float y, Float z, TaskFuncPtr exec);

extern Sint8 D_8C1623D0[][5];
extern Sint8 D_8C1623D4[][5];
extern TaskFuncPtr D_8C162394[]; /* animal task functions (func_8C015820 exec) */

void func_8C015720(Float x, Float y, Float z) {
    Uint16 level;
    Uint16 n;

    level = (Uint8)(GetStageNumber() >> 8);
    if ((Float)rand() * 3.0517579e-05f < 0.1f) {
        n = 4;
    } else {
        n = (Float)rand() * 1.2176513e-4f;
    }
    func_8C015820(x, y, z, D_8C162394[D_8C1623D0[level][n]]);
}

void func_8C015798(Float x, Float y, Float z) {
    Uint16 level;

    level = (Uint8)(GetStageNumber() >> 8);
    func_8C015820(x, y, z, D_8C162394[D_8C1623D4[level][0]]);
}
