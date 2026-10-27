#include "common.h"

/*
 * 8C09E76C-8C09E778: float getter of D_8C66C314 (the setter is
 * func_8C09E760; func_8C09E880 reads the variable directly and adds it to
 * a coordinate before a range test).
 *
 * Not SHC 5.1r03 output with the game flags: the module 8C09E760-8C09F080
 * is listed in docs/compiler.md as an assembly / differently built region
 * (no "00 09" 32-byte pool padding). Each function here has its own
 * literal pool right after the RTS, padded with .DATA.W H'0000, and the
 * pool address is loaded into R0 (MOV.L @(4,PC),R0; RTS; FMOV.S @R0,FR0).
 * SHC (5.0r26 .. 5.1r13, -optimize=0/1, -speed/-size; plain, array,
 * struct and local-pointer forms) always picks R3 for this load and keeps
 * one pool per file. The original assembly stays in the build; the body
 * below is the C description used by libsa1.
 */
#ifdef NON_MATCHING
extern Float D_8C66C314;

Float func_8C09E76C(void) {
    return D_8C66C314;
}
#endif
