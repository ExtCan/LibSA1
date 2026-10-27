/*
 * Compiler probe t08: NEAR MISSES (structure matches, source not yet exact).
 * Kept as documentation of open problems; they do not discriminate versions.
 *   8C01B5E0-8C01B636 IsOddSub  switch -> BRAF jump table (.DATA.W offsets) with the
 *                               literal pool placed between BRAF and the table (no
 *                               .ALIGN 32 there).  Table/branch layout identical; the
 *                               original keeps a dead copy of the switch value in R4
 *                               (MOV.L @R0,R4 / MOV.B @(9,R4),R0 / MOV R0,R4).
 *   8C034D0A-8C034D34 A         original allocates the 4-byte frame with ADD #-4,R15
 *                               and stores R4 separately; 5.1r03 compiles this source
 *                               to MOV.L R4,@-R15 (the split form appears when the slot
 *                               belongs to a *local* variable, cf. t07 Disp1).
 *   8C015798-8C0157D6 Voice4    float args spilled in a different slot order
 *   (function mode diff counts with SA1 flags on 5.1r03: IsOddSub suffers from table
 *    words being decoded as code; use shcprobe.py --exact with a @tu file instead)
 * Flags: -cpu=sh4 -division=cpu -fpu=single -endian=little -optimize=1 -speed
 *        -macsave=0 -extra=a=1800 -round=zero   (SHC 5.1r03)
 */
typedef struct {
    char pad0[9];
    char sub;   /* +9 */
} GameStat;
extern GameStat *gs_8C11204C;
extern void f_8C639C70(int);
extern void f_8C63A804(int, int);
extern void f_8C63CE24(char *, int, int);
extern void f_8C639B98(int);
extern char d_8C19A7D4[];
extern int GetStageAct_8C04F7E0(void);
extern char tbl_8C1623D0[][5];
extern int voice_8C162394[];
extern void PlayVoice_8C015820(int, float, float, float);

/* @match IsOddSub 8C01B5E0-8C01B636 */
int IsOddSub(void)
{
    switch (gs_8C11204C->sub) {
    case 5: case 7: case 9: case 11: case 13: case 15:
    case 17: case 19: case 21: case 23: case 25: case 26:
        return 1;
    }
    return 0;
}

/* @match A 8C034D0A-8C034D34 */
void A(int p)
{
    f_8C639C70(0);
    f_8C63A804(0, p + 32);
    f_8C63CE24(d_8C19A7D4, 3, 0);
    f_8C639B98(1);
}

/* @match Voice4 8C015798-8C0157D6 */
void Voice4(float x, float y, float z)
{
    unsigned short stg;

    stg = (unsigned char)(GetStageAct_8C04F7E0() >> 8);
    PlayVoice_8C015820(voice_8C162394[tbl_8C1623D0[stg][4]], x, y, z);
}
