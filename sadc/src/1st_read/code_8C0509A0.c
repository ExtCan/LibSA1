/*
 * Input module: pad reading on/off and per-port enable flags
 * (1ST_READ 8C0509A0-8C050A06).
 */
#include "common.h"
#include "sa1/input.h"

extern Uint8 D_8C19E3A8;        /* pad reading enabled */
extern Uint8 D_8C19E3A9[2];     /* per-port enable, ports 0 and 1 (pool symbol D_8C19E3AA is &D_8C19E3A9[1]) */

void func_8C0509A0(void) {
    D_8C19E3A8 = 1;
}

void func_8C0509A8(void) {
    D_8C19E3A8 = 0;
}

Sint32 func_8C0509B0(void) {
    return D_8C19E3A8;
}

void func_8C0509B8(Uint8 n) {
    if (n != 0 && n != 1) {
        D_8C19E3A9[0] = 1;
        D_8C19E3A9[1] = 1;
        return;
    }
    D_8C19E3A9[n] = 1;
}

void func_8C0509DA(Uint8 n) {
    if (n != 0 && n != 1) {
        D_8C19E3A9[0] = 0;
        D_8C19E3A9[1] = 0;
        return;
    }
    D_8C19E3A9[n] = 0;
}

Sint32 func_8C0509FC(Uint8 n) {
    return D_8C19E3A9[n];
}
