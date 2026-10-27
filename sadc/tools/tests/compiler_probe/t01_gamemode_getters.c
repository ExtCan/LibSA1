/*
 * Compiler probe t01: tiny getters/setters around the game-status pointer.
 * Original: 1ST_READ.BIN 0x8C01B35E-0x8C01B388 (US 1.005)
 *   8C01B35E isMode2  (MOVT kept *before* RTS, NOP in delay slot)
 *   8C01B36C isMode3
 *   8C01B37A getVar
 *   8C01B380 clrVar
 * Matches (function mode, SA1 flags): isMode2/isMode3 need SHC >= 5.0r31
 *   (5.0r26/r28 emit BF/RTS/MOV #1 instead of MOVT for the if/return form);
 *   getVar/clrVar match every version.
 * Flags: -cpu=sh4 -division=cpu -fpu=single -endian=little -optimize=1 -speed
 *        -macsave=0 -extra=a=1800 -round=zero   (SHC 5.1r03)
 * Note: "return x == 2;" does NOT match on any version (MOVT goes into the
 * delay slot); the original source used the if/return form.
 */
typedef struct {
    char pad0[8];
    char mode;  /* +8 */
    char sub;   /* +9 */
} GameStat;

extern GameStat *gs_8C11204C;
extern int g_8C6B7078;

/* @match isMode2 8C01B35E-8C01B36C */
int isMode2(void)
{
    if (gs_8C11204C->mode == 2) return 1;
    return 0;
}

/* @match isMode3 8C01B36C-8C01B37A */
int isMode3(void)
{
    if (gs_8C11204C->mode == 3) return 1;
    return 0;
}

/* @match getVar 8C01B37A-8C01B380 */
int getVar(void)
{
    return g_8C6B7078;
}

/* @match clrVar 8C01B380-8C01B388 */
void clrVar(void)
{
    g_8C6B7078 = 0;
}
