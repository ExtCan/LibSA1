#ifndef SA1_CCL_H
#define SA1_CCL_H

/*
 * CCL: object-vs-object collision (8C02E820-8C034940). taskwk.cwp points to a
 * colliwk created by CCL_Init (8C02F4B0); the per-frame entry is 8C02F63C.
 * SA1 DC layouts differ from SADX (docs/matching_tips.md, engine facts).
 * CCL_INFO tables (Sonic 8C1C5E80, Tails 8C2575AC, ...) are game DATA:
 * extern only, ripped at run time on the PC target.
 */

#include "task.h"

/*
 * Collision shape (0x34 bytes). CCL_Init copies the caller's table into a
 * CAlloc(nbInfo, 0x34) block with 8C604500 (nbInfo * 0x34 bytes: on the PC
 * target this must be nbInfo * sizeof(CCL_INFO)).
 */
struct colliwk;

typedef struct {
    Uint32 attr;                /* 0x00 low 4 bits = form (8C02F400 treats 3 specially); 0x100 cleared each frame (8C02F5E0) */
    Uint8 flag;                 /* 0x04 bits 0x10/0x20 (8C02F5E0, 8C081D00); bits 0-1 and 2-3: two 2-bit attack/damage levels written every frame by SonicMain (8C0A78FA-8C0A791E: 3/3 for item/equipment 0x8000, 2/3 mode 44, 1/3 mode 6, 1/1 twp->flag 0x100, 0/1 mode 45) and 8C0B13EE (light speed dash); compared by 8C072B44 (SADX: damage) */
    Uint8 b05;                  /* 0x05 written by 8C0C967A; read as hit_cwp->info[hit_num].b05 & 0xF (MOV.B + EXTU.B) by 8C078856/8C0771C4, gated by colliwk.flag 0x80/0x08 */
    Uint8 pad06[2];
    NJS_POINT3 center;          /* 0x08 read 8C01FB98, 8C02FD32, 8C05E7D4, 8C06CB40.. */
    Float a;                    /* 0x14 size (radius); written by the mains */
    Float b;                    /* 0x18 */
    Float c;                    /* 0x1C */
    Angle angx;                 /* 0x20 shape rotation (debug draw 8C02E980 -> njRotateX) */
    Angle angy;                 /* 0x24 (8C02EA00 MOV.L @(36) -> njRotateY) */
    Angle angz;                 /* 0x28 (8C02EA10 MOV.L @(40) -> njRotateZ) */
    Sint16 hit_info;            /* 0x2C index of the hit shape in hit_cwp->info (8C02F944); -1 each frame (8C02F5E0) */
    Sint16 pad2E;
    struct colliwk *hit_cwp;    /* 0x30 colliwk this shape hit (8C02F93E, same value as cwp->hit_cwp); 0 each frame (8C02F5E0).
                                 * POINTER: CCL_INFO is 0x34 bytes only on 32-bit targets; ripped tables must be converted */
} CCL_INFO;                     /* 0x34 */

/* Collision work (CAlloc(1, 0x1C) by CCL_Init 8C02F4B0) */
typedef struct colliwk {
    Uint16 id;                  /* 0x00 CCL_Init argument 3 */
    Uint16 flag;                /* 0x02 |= 0x20 at init, |= 0x800 for id 2/3/7; matched code tests 2 (hit), 0x100 with 2 (8C06C880, 8C06C7DE), 0x400, 0x800, 0x80/0x08 (8C078856); &= 0xFF61 when collision is switched off (8C0A95A8) */
    Uint16 nbInfo;              /* 0x04 */
    Uint16 test_num;            /* 0x06 index of the shape being tested (set in the pair loop 8C0335D4; read with MULU.W #52 by 8C02F8A0, 8C02FF98, 8C031BA2) */
    Float range;                /* 0x08 25.0f at init (id 0/1), else computed by 8C02F400; players: p.height + njScalor(&pwp->spd) (8C0A77BC) */
    CCL_INFO *info;             /* 0x0C */
    struct task *mytask;        /* 0x10 owner task (read through hit_cwp->mytask->twp by 8C06C880, 8C079234, 8C05E6E8). NOTE: SonicMain copies cwp->mytask->twp->ang.x/z when twp->flag & 0x20 (8C0A77F4), which SADX does from the collided task: verify before relying on "owner" */
    Sint16 my_num;              /* 0x14 -1 each frame (8C02F5E0) */
    Sint16 hit_num;             /* 0x16 index into hit_cwp->info, -1 each frame */
    struct colliwk *hit_cwp;    /* 0x18 */
} colliwk;                      /* 0x1C */

SA1_ASSERT_SIZE(CCL_INFO, 0x34);
SA1_ASSERT_OFFSET(CCL_INFO, center, 0x08);
SA1_ASSERT_OFFSET(CCL_INFO, a, 0x14);
SA1_ASSERT_OFFSET(CCL_INFO, hit_info, 0x2C);
SA1_ASSERT_SIZE(colliwk, 0x1C);
SA1_ASSERT_OFFSET(colliwk, info, 0x0C);
SA1_ASSERT_OFFSET(colliwk, mytask, 0x10);
SA1_ASSERT_OFFSET(colliwk, hit_num, 0x16);
SA1_ASSERT_OFFSET(colliwk, hit_cwp, 0x18);

SA1_ASSERT_OFFSET(CCL_INFO, flag, 0x04);
SA1_ASSERT_OFFSET(CCL_INFO, b05, 0x05);
SA1_ASSERT_OFFSET(CCL_INFO, angx, 0x20);
SA1_ASSERT_OFFSET(CCL_INFO, angz, 0x28);
SA1_ASSERT_OFFSET(colliwk, test_num, 0x06);
SA1_ASSERT_OFFSET(CCL_INFO, hit_cwp, 0x30);
SA1_ASSERT_OFFSET(colliwk, flag, 0x02);
SA1_ASSERT_OFFSET(colliwk, nbInfo, 0x04);
SA1_ASSERT_OFFSET(colliwk, range, 0x08);
SA1_ASSERT_OFFSET(colliwk, my_num, 0x14);

#define CWP(twp) ((colliwk *)(twp)->cwp)

/* CCL_INFO tables (game data; extern only) */
extern CCL_INFO D_8C19C1C4[];           /* one shape, CCL_Init id 4 (event model 8C04BD70) */

/*
 * CCL_Init. SA1 DC argument order (asm 8C02F4B0): R6 = id (a char
 * parameter: spilled with MOV.B, stored to cwp->id as a byte), R7 = unused,
 * 5th argument (stack) = nbInfo (read with MOV.B). Matched code calls it as
 * (tp, info, id, 1, n), e.g. code_8C0486A0.c; the definition is matched
 * code_8C02F27C.c (build/verify/canonical_protos.json).
 */
void CCL_Init(task *tp, CCL_INFO *info, Uint8 id, Sint32 unused, Uint8 nbInfo);
void func_8C02F63C(taskwk *twp);        /* per-frame entry (83 callers) */
void func_8C02F400(colliwk *cwp);       /* recompute range */
void func_8C02F5E0(taskwk *twp, colliwk *cwp); /* per-frame reset of hit state */
void func_8C0344A0(taskwk *twp);        /* free twp->cwp (task destructor 8C098998) */
Sint8 func_8C0344DC(taskwk *twp, NJS_POINT3 *v); /* push-out vector of the hit shape (reads cwp, hit_num, info, hit_cwp); callers EXTS.B R0 (8C06CA30, 8C06CCA0, 8C06CFCC, 8C06EFCE, 8C07266E, 8C078856, 8C076782, 8C0771C4) */
#define CCL_Entry func_8C02F63C

#endif