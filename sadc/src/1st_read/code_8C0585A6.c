#include "common.h"
#include "task.h"

/*
 * "NPC / AI hooks" 8C0585A6-8C05867A: tiny getters, setters and tail-call
 * wrappers around the 1.5P follower AI (code_8C081AC0.c, code_8C082A60.c),
 * the boss-fight AI players (code_8C082D60.c, code_8C083E5C.c) and the
 * B_8C78B450 menu / trial block. Two chunks: 8C0584A0.src (8C0585A6-8C0585E8)
 * and 8C058640.src (8C058640-8C058664). configure.py splices per function, so
 * one file covers both.
 *
 * Not here: the dead debug selector 8C0584A0 / 8C0584F8 ("NPC [%s]" on
 * controller 2, calls the SDK print 8C641110); it stays assembly
 * (follower_ai.md Review R.4 F0).
 *
 * The B_8C78B450 block (18 bytes, cleared by func_8C058646) holds single-byte
 * globals with their own symbols: B_8C78B451, B_8C78B459 (save slot index for
 * B_8C78B468), B_8C78B45A (1.5P flag, func_8C0829E6), B_8C78B45D. Its writers
 * are the ADVERTISE (menu) overlay. libsa1 keeps globals at their DC addresses
 * (guest-address model), so the 18-byte clear reaches them on PC as well.
 */

/*
 * Boss-fight AI creation request (B_8C79A39C, filled by func_8C09072C):
 * DC layout +0x00 Sint32 kind 0-5, +0x04 task * player task
 * (func_8C07CFA0(0)), +0x08 task * the AI player task (stored by
 * func_8C083F7C), +0x0C Sint32 (cleared by func_8C083A1E), +0x10 TaskFuncPtr
 * callback (func_8C090800). The full definitions live in code_8C082D60.c
 * (UNK_8C082D60_arg), code_8C084260.c and code_8C083E5C.c (UNK_8C083F7C_t);
 * the wrappers below only pass the pointer on, so the type stays opaque here.
 */
typedef struct UNK_8C0585CA_arg UNK_8C0585CA_arg;

/* one 0x4A0-byte record of B_8C78B468 (3 records, up to B_8C78C248): the
 * size of the save data block (SAVEDATA in code_8C0886F4.c: CRC at +0, the
 * flag bit arrays at +0x260 / +0x2A0), so probably the three save slots */
typedef struct {
    Uint8 data[0x4A0];
} UNK_8C78B468_t;

extern Uint8 B_8C78B450[18];
extern Uint8 B_8C78B459;
extern Uint8 B_8C78B45A;
extern Uint8 B_8C78B45D;
extern UNK_8C78B468_t B_8C78B468[3];
extern task *B_8C78C248;        /* 1.5P AI task (func_8C081AC0) */
extern Sint32 B_8C78C24C;       /* 1.5P hold flag (func_8C082820 returns 0 while set) */
extern Sint32 B_8C78C250;       /* boss AI result: 0 running, 1 done, 2 aborted */

extern Sint32 func_8C081C66(void);
extern task *func_8C081CAC(void);
extern Sint32 func_8C082A8C(Sint32 time);
extern Sint32 func_8C082C5A(void);
extern task *func_8C083A1E(UNK_8C0585CA_arg *p);
extern task *func_8C083F7C(UNK_8C0585CA_arg *p);
extern void func_8C08403E(UNK_8C0585CA_arg *p);
extern void func_8C0869A4(Uint8 *dst, Uint8 c, Sint32 n);

/* 8C0585A6: getter of B_8C78B45D (callers: the SUMMARY overlay, chunks
 * 8C900040, 8C900B60, 8C901BE0; none in 1ST_READ). Matching note:
 * the Uint8 local gives MOV.B @R3,R4 / EXTU.B R4,R0; a plain
 * "return B_8C78B45D;" loads into R0 and also shifts SHC's register choice
 * in the next four functions (R2 instead of R3 in 8C0585AE / 8C0585B4,
 * R1/R2 and R3 in 8C0585BA / 8C0585C2) */
Sint32 func_8C0585A6(void)
{
    Uint8 v;

    v = B_8C78B45D;
    return v;
}

/* 8C0585AE: remove the slot-1 follower Tails (event system 8C049692) */
Sint32 func_8C0585AE(void)
{
    return func_8C081C66();
}

/* 8C0585B4: re-create the slot-1 follower Tails (8C049692 / 8C049744) */
task *func_8C0585B4(void)
{
    return func_8C081CAC();
}

/* 8C0585BA: set the 1.5P hold flag (func_8C082820 on Twinkle Park act 0;
 * STG03 0C907CC8 / 0C907DA8) */
void func_8C0585BA(void)
{
    B_8C78C24C = 1;
}

/* 8C0585C2: clear the 1.5P hold flag (STG03 0C907CDC, L0C908004) */
void func_8C0585C2(void)
{
    B_8C78C24C = 0;
}

/* 8C0585CA: create a boss-fight AI player (func_8C09072C) */
task *func_8C0585CA(UNK_8C0585CA_arg *p)
{
    return func_8C083A1E(p);
}

/* 8C0585D0: create the boss-fight AI's player task (func_8C09072C) */
task *func_8C0585D0(UNK_8C0585CA_arg *p)
{
    return func_8C083F7C(p);
}

/* 8C0585D6: free the boss-fight AI's player task (func_8C090800) */
void func_8C0585D6(UNK_8C0585CA_arg *p)
{
    func_8C08403E(p);
}

/* 8C0585DC: set the boss AI result (boss AI tasks 8C082D60 .. 8C084BA0) */
void func_8C0585DC(Sint32 a)
{
    B_8C78C250 = a;
}

/* 8C0585E2: get the boss AI result (func_8C090800 compares it with 1) */
Sint32 func_8C0585E2(void)
{
    return B_8C78C250;
}

/* 8C0585E8: stage title card wrapper (8C04CA40, 8C04DA20) */
Sint32 func_8C0585E8(Sint32 time)
{
    return func_8C082A8C(time);
}

/* 8C058640: stage title card display wrapper (8C04CA40, 8C04DA20) */
Sint32 func_8C058640(void)
{
    return func_8C082C5A();
}

/* 8C058646: reset on game-mode change (game loop 8C053B60): clears the
 * 18-byte block B_8C78B450 (including the 1.5P flag B_8C78B45A) and forgets
 * the 1.5P AI task pointer */
void func_8C058646(void)
{
    func_8C0869A4(B_8C78B450, 0, 18);
    B_8C78C248 = NULL;
}

/* 8C05865C: getter of the 1.5P flag B_8C78B45A (no caller in any image) */
Sint32 func_8C05865C(void)
{
    return B_8C78B45A;
}

/* 8C058664: current record of B_8C78B468 (save slot B_8C78B459; no
 * reference in the 1ST_READ or overlay asm) */
UNK_8C78B468_t *func_8C058664(void)
{
    return &B_8C78B468[B_8C78B459];
}
