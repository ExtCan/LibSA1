#include "common.h"
#include "sa1/ninja_funcs.h"

/*
 * 8C06C0C0-8C06C1D8: frame timing on SH-4 TMU channel 1.
 *
 * func_8C06C0C0 stops TMU1, loads TCOR1/TCNT1 with 12500000 (a 1 s period at
 * Pphi/4 = 12.5 MHz), restarts it and registers the per-frame callback
 * func_8C06C1D8 (code_8C06C1D8.c, samples TCNT1 into B_8C78C604) through
 * func_8C64133E. The other functions turn counter values into time; TCNT1
 * counts down from 12500000, so "12500000 - TCNT1" is the count elapsed in
 * the current period. One count is 0.0048 units (= 60000 / 12500000: time in
 * 1/60000 s, i.e. frames * 1000 at 60 Hz).
 *
 * Matching notes: the float result goes through a local (r) in every
 * function; without it SHC computes straight into FR0 and drops the
 * FMOV FRn,FR4 / FMOV FR4,FR0 pair. func_8C06C12E re-reads B_8C78C600 after
 * storing it (SHC forwards the value with a MOV copy). func_8C06C0C0 keeps
 * the TSTR value read at the start and writes "tstr | 2" back (no re-read).
 *
 * TARGET_PC: the TMU does not exist. The registers are the libsa1 runtime's
 * emulated TMU channel 1 (sa1_tmu); TCNT1 is read through sa1_tmu_tcnt1()
 * like code_8C06C1D8.c, never through the DC address.
 */

#if defined(TARGET_PC)
typedef struct {
    Uint8 tstr;     /* TSTR: bit 1 runs channel 1 */
    Uint16 tcr1;    /* TCR1 */
    Uint32 tcor1;   /* TCOR1: reload value */
    Uint32 tcnt1;   /* TCNT1: written here, read through sa1_tmu_tcnt1() */
} SA1_TMU;
extern SA1_TMU sa1_tmu;
Uint32 sa1_tmu_tcnt1(void);
#define TMU_TSTR    (sa1_tmu.tstr)
#define TMU_TCR1    (sa1_tmu.tcr1)
#define TMU_TCOR1   (sa1_tmu.tcor1)
#define TMU_TCNT1_W (sa1_tmu.tcnt1)
#define TMU_TCNT1   (sa1_tmu_tcnt1())
#else
#define TMU_TSTR    (*(Uint8 *)0xFFD80004)
#define TMU_TCR1    (*(Uint16 *)0xFFD8001C)
#define TMU_TCOR1   (*(Uint32 *)0xFFD80014)
#define TMU_TCNT1_W (*(Uint32 *)0xFFD80018)
#define TMU_TCNT1   (*(Uint32 *)0xFFD80018)
#endif

#define TMU_PERIOD  12500000        /* TMU1 counts per second (Pphi/4) */

extern Uint32 B_8C78C600;           /* TCNT1 at the last stopwatch reset */
extern Uint32 B_8C78C604;           /* TCNT1 sampled by func_8C06C1D8 each frame */
extern Sint32 B_8C78C5E0[8];        /* reset to -1; func_8C06C240 prints entries > 0 */
extern Sint32 D_8C03F760[20];       /* cleared here */

void func_8C06C1D8(void);           /* code_8C06C1D8.c: per-frame TMU1 callback */

/* Program TMU1 for a 1 s period, register the frame callback, reset tables. */
void func_8C06C0C0(void)
{
    Sint32 i;
    Uint8 tstr;

    tstr = TMU_TSTR;
    TMU_TSTR = tstr & ~2;
    B_8C78C600 = TMU_PERIOD;
    TMU_TCR1 = 0;
    TMU_TCOR1 = B_8C78C600;
    TMU_TCNT1_W = B_8C78C600;
    TMU_TSTR = tstr | 2;
    func_8C64133E((void *)func_8C06C1D8, 0);
    for (i = 0; i < 8; i++) {
        B_8C78C5E0[i] = -1;
    }
    for (i = 0; i < 20; i++) {
        D_8C03F760[i] = 0;
    }
}

/* Reset the stopwatch; return the time elapsed in the current TMU1 period. */
Float func_8C06C12E(void)
{
    Uint32 c;
    Float r;

    c = TMU_TCNT1;
    B_8C78C600 = c;
    r = (12500000.0f - (Float)B_8C78C600) * 0.0047999993f;
    return r;
}

/* Time since the last stopwatch reset; resets the stopwatch. */
Float func_8C06C15A(void)
{
    Uint32 c;
    Uint32 d;
    Float r;

    c = TMU_TCNT1;
    d = B_8C78C600 - c;
    B_8C78C600 = c;
    r = (Float)d * 0.0047999993f;
    return r;
}

/* Reset the stopwatch. */
void func_8C06C182(void)
{
    Uint32 c;

    c = TMU_TCNT1;
    B_8C78C600 = c;
}

/* Time elapsed in the TMU1 period at the last frame callback. */
Float func_8C06C18C(void)
{
    Float r;

    r = (12500000.0f - (Float)B_8C78C604) * 0.0047999993f;
    return r;
}

/* Time elapsed in the current TMU1 period. */
Float func_8C06C1B2(void)
{
    Uint32 c;
    Float r;

    c = TMU_TCNT1;
    r = (12500000.0f - (Float)c) * 0.0047999993f;
    return r;
}
