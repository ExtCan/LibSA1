/*
 * Input module: soft-reset combo detection and per-port button/stick
 * digital state with key repeat (1ST_READ 8C050A40-8C050BE4).
 * Both are called by the pad update func_8C050BE4.
 */
#include "common.h"
#include "sa1/input.h"

extern Uint8 D_8C19E3A8;        /* pad reading enabled (code_8C0509A0.c) */
extern Sint8 D_8C19E3E0;        /* reset combo fired this frame (read by func_8C050A06) */
extern Sint16 D_8C19E3E2;       /* reset combo frame counter */
extern Uint32 B_8C754E4C[4];    /* per-port held: stick directions (0xF0) | pad 'on' */
extern Uint32 B_8C754E5C[4];    /* per-port pressed: new stick directions | pad 'press' | repeat */
extern Uint32 B_8C754E6C[4];    /* B_8C754E4C, or 0 while pad reading is off */
extern Uint32 B_8C754E7C[4];    /* B_8C754E5C, or 0 while pad reading is off */
extern Sint32 B_8C754EAC[4];    /* per-port direction key-repeat timer */

/* Soft reset: A+B+X+Y+Start (0x60E) held on any port, one of them pressed this frame */
void func_8C050A40(void) {
    Sint8 flag;

    flag = 0;
    if ((B_8C754DA8[0]->on & 0x60E) == 0x60E && (B_8C754DA8[0]->press & 0x60E)) {
        flag = 1;
    }
    if ((B_8C754DA8[1]->on & 0x60E) == 0x60E && (B_8C754DA8[1]->press & 0x60E)) {
        flag = 1;
    }
    if ((B_8C754DA8[2]->on & 0x60E) == 0x60E && (B_8C754DA8[2]->press & 0x60E)) {
        flag = 1;
    }
    if ((B_8C754DA8[3]->on & 0x60E) == 0x60E && (B_8C754DA8[3]->press & 0x60E)) {
        flag = 1;
    }
    if (flag == 1) {
        D_8C19E3E2++;
    } else {
        D_8C19E3E2 = 0;
    }
    if (D_8C19E3E2 >= 1) {
        D_8C19E3E0 = 1;
        D_8C19E3E2 = 0;
    } else {
        D_8C19E3E0 = 0;
    }
}

/* Stick -> digital directions (0x10 up, 0x20 down, 0x40 left, 0x80 right) merged with the buttons */
void func_8C050ACE(Sint32 n) {
    Uint32 dir;

    dir = 0;
    if (B_8C754DA8[n] == NULL) {
        return;
    }
    if (B_8C754DA8[n]->y1 > 78) {
        dir = 0x20;
    }
    if (B_8C754DA8[n]->y1 < -78) {
        dir |= 0x10;
    }
    if (B_8C754DA8[n]->x1 > 78) {
        dir |= 0x80;
    }
    if (B_8C754DA8[n]->x1 < -78) {
        dir |= 0x40;
    }
    B_8C754E5C[n] = ~B_8C754E4C[n] & dir;
    B_8C754E4C[n] = dir;
    B_8C754E4C[n] |= B_8C754DA8[n]->on;
    B_8C754E5C[n] |= B_8C754DA8[n]->press;
    if (B_8C754E5C[n] & 0xF0) {
        B_8C754EAC[n] = 0;
    } else if (B_8C754E4C[n] & 0xF0) {
        if (++B_8C754EAC[n] > 30) {
            B_8C754EAC[n] -= 6;
            B_8C754E5C[n] |= B_8C754E4C[n] & 0xF0;
        }
    } else {
        B_8C754EAC[n] = 0;
    }
    if (D_8C19E3A8 == 0) {
        B_8C754E6C[n] = 0;
        B_8C754E7C[n] = 0;
    } else {
        B_8C754E6C[n] = B_8C754E4C[n];
        B_8C754E7C[n] = B_8C754E5C[n];
    }
}
