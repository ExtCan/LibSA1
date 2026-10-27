/*
 * Discriminator d01 (no original address; see t04 IsStage1 for the real case).
 * "(int)unsigned_short >> 8": the promoted value is known non-negative, but
 *   SHC 5.1r03, 5.1r04, 5.1r08 emit a signed shift sequence
 *       EXTU.W R0,R0 / CMP/PZ R0 / BF Lneg / BRA Ldone / SHLR8 R0 / Lneg: NOT/SHLR8/NOT
 *   SHC <= 5.1r01 and 5.1r11/5.1r13 emit just EXTU.W + SHLR8.
 * 1ST_READ.BIN game code contains 54 EXTU.x Rn + CMP/PZ Rn sequences of this kind
 * (e.g. 8C01494A, 8C02685C, 8C0673E8), so only 5.1r03/r04/r08 qualify.
 * Flags: SA1 flags (see docs/compiler.md).
 */
extern unsigned short GetStageAct_8C04F7E0(void);
extern void sink(int);

void d01(void)
{
    int n = GetStageAct_8C04F7E0() >> 8;
    sink(n);
}
