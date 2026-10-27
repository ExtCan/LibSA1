#ifndef SA1_INPUT_H
#define SA1_INPUT_H

/*
 * Pad state as seen by the game (input module 8C0509A0-8C051760).
 * pdGetPeripheral (8C601874, sa1/ninja_funcs.h) returns the Katana
 * peripheral record for a port; the pad update 8C050BE4 copies it (13 words)
 * into per-port records and the analog conversion 8C05117E turns the stick
 * into a camera-relative angle + magnitude per player (atan2f-like
 * 8C10D0F8). libsa1 fills these from the host's input each tick.
 */

#include "common.h"
#include "ninja.h"

/*
 * Katana PDS_PERIPHERAL (0x34 on DC; layout as used by matched code
 * code_8C0130F8.c and code_8C081D00.c). Holds pointers (name, extend, info):
 * native layout on 64-bit hosts.
 */
typedef struct {
    Uint32 id;                  /* 0x00 */
    Uint32 support;             /* 0x04 capability bits; 8C050BE4 tests & 0xC000E */
    Uint32 on;                  /* 0x08 held buttons (26 readers of B_8C754C08+8) */
    Uint32 off;                 /* 0x0C */
    Uint32 press;               /* 0x10 pressed this frame (OR'ed across logic steps by 8C04E95A) */
    Uint32 release;             /* 0x14 */
    Uint16 r;                   /* 0x18 analog trigger */
    Uint16 l;                   /* 0x1A */
    Sint16 x1;                  /* 0x1C stick */
    Sint16 y1;                  /* 0x1E */
    Sint16 x2;                  /* 0x20 */
    Sint16 y2;                  /* 0x22 */
    char *name;                 /* 0x24 */
    void *extend;               /* 0x28 */
    Uint32 old;                 /* 0x2C previous 'on' (8C050BE4 reset path) */
    void *info;                 /* 0x30 */
} PDS_PERIPHERAL;               /* 0x34 */

SA1_ASSERT_SIZE(PDS_PERIPHERAL, 0x34);
SA1_ASSERT_OFFSET(PDS_PERIPHERAL, on, 0x08);
SA1_ASSERT_OFFSET(PDS_PERIPHERAL, press, 0x10);
SA1_ASSERT_OFFSET(PDS_PERIPHERAL, x1, 0x1C);
SA1_ASSERT_OFFSET(PDS_PERIPHERAL, old, 0x2C);

/* Stick as angle + magnitude (one per player, stride 8; 8C05117E writes,
 * GetAnalog 8C06CD22 reads) */
typedef struct {
    Angle ang;                  /* 0x00 camera-relative */
    Float stroke;               /* 0x04 0..1 */
} PL_ANALOG;                    /* 0x08 */

SA1_ASSERT_SIZE(PL_ANALOG, 0x08);

/* B_8C754C08..B_8C754DA8 is exactly 8 records of 0x34 (B_8C754C3C = [1]) */
extern PDS_PERIPHERAL B_8C754C08[8];
extern PDS_PERIPHERAL *B_8C754DA8[4];   /* pdGetPeripheral results for ports A..D (8C050BE4) */
extern PL_ANALOG B_8C754DCC[8];         /* per-player analog (8C05117E -> GetAnalog) */
extern PL_ANALOG B_8C754E0C[8];         /* second analog set written by 8C05117E (role unverified) */
extern Uint32 D_8C19E38C;               /* button assignment mask (50 readers) */
extern Uint32 D_8C19E390;               /* button assignment mask (35 readers; jump in the AI code) */
extern Uint32 D_8C19E3A0;               /* button assignment mask (31 readers) */
extern Sint32 D_8C19E3A4;               /* button assignment mask; must be Sint32 (code_8C06C300.c func_8C06C32C only matches with it) */
extern Sint8 B_8C754BF8;                /* pad flag (MOV.B + TST at 8C06C340; also chunks 8C04C680, 8C099DA0) */
/*
 * NOTE (unresolved): the game probably declared PDS_PERIPHERAL.on/press and
 * the D_8C19E38C/390/3A0 masks signed: 8C06CEBE, 8C0B14E0, 8C0B1C56 and
 * code_8C084BA0.c only match with (Sint32) casts on both operands, while
 * 8C06CEFE, 8C0B12E0 and 8C0B1352 match with the Uint32 types. Kept Uint32
 * (matched code depends on it); cast where needed.
 * B_8C754C10 / B_8C754C18 in the original pools are &B_8C754C08[0].on /
 * .press: write B_8C754C08[i].on/.press (shcdiff compares the values).
 */

void func_8C0509A0(void);               /* pad reading on (D_8C19E3A8), 13 callers */
void func_8C0509A8(void);               /* pad reading off */
void func_8C0509B8(Uint8 n);            /* per-port enable (matched code_8C081D00 prototype) */
void func_8C0509DA(Uint8 n);            /* per-port disable */
void func_8C050BE4(void);               /* pad update: pdGetPeripheral per port, copy records (10 callers) */
void func_8C05117E(void);               /* stick -> camera-relative angle/stroke per player (1 caller) */
void func_8C0517F6(Sint32 n);           /* pad refresh between logic steps (8C04E95A) */

#endif