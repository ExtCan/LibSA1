/*
 * 1ST_READ 8C0B6120: Tails bonus helper (chunk 8C0B6120-8C0BA820, function 1/6;
 * TailsMain, func_8C0B70E8, TailsModeMachine, func_8C0BA738 and func_8C0BA75A
 * are separate assignments).
 *
 * func_8C0B6120(n): called from TailsModeMachine (n = 100 / 30 / 10; the
 * return value is ignored there). In game mode 9 (tcrf_LevelSelectVar) with B_8C78B451[0] == 1
 * it adds n to the counter through func_8C08A298 (B_8C752BF0 += n,
 * code_8C08A198.c) and returns 1; otherwise it returns 0.
 * Same pattern as func_8C0A77A0 (Sonic), which returns nothing.
 */
#include "common.h"
#include "sa1/engine.h"

extern Uint8 B_8C78B451[];

extern void func_8C08A298(Sint32 n);

Sint32 func_8C0B6120(Sint32 n)
{
    if (tcrf_LevelSelectVar == 9 && B_8C78B451[0] == 1) {
        func_8C08A298(n);
        return 1;
    }
    return 0;
}
