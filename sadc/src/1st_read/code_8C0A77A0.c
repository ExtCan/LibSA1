/*
 * 1ST_READ 8C0A77A0: Sonic bonus helper (chunk 8C0A77A0-8C0A9660, function 1/2;
 * SonicMain at 8C0A77BC is a separate assignment).
 *
 * func_8C0A77A0(n): called from SonicModeMachine (8C0A966C) with n = 50 / 10.
 * When tcrf_LevelSelectVar == 9 and B_8C78B451[0] == 1 it adds n to the
 * counter through func_8C08A298 (B_8C752BF0 += n, code_8C08A198.c).
 */
#include "common.h"

extern Sint32 tcrf_LevelSelectVar;
extern Uint8 B_8C78B451[];

extern void func_8C08A298(Sint32 n);

void func_8C0A77A0(Sint32 n)
{
    if (tcrf_LevelSelectVar == 9 && B_8C78B451[0] == 1) {
        func_8C08A298(n);
    }
}
