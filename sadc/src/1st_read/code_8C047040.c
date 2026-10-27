/*
 * Enemy common library (1ST_READ 8C047040-8C048B00, sa1_code_map.md):
 * the enemy work initialiser func_8C047040 (SADX EnemyInitialize).
 *
 * Chunk asm/1st_read/text/8C047040.src also holds func_8C047126 (an empty
 * function) and func_8C04712A (player-in-view test over B_8C78C608[0..1]);
 * they stay asm for now.
 */

#include "sa1/sa1.h"

/*
 * Enemy work (SADX enemywk): CAlloc(1, 0x23C) by func_8C047040 into
 * tp->mwp of an enemy task created without a motion work (19 callers: the
 * E_* enemy execs 8C0F5040 ..., the rival target 8C085532). Local view until
 * include/sa1 has the type (type_notes of the staging report); the prefix is
 * the E102_TARGETWK view of code_8C0D5600.c / code_8C0D37F4.c, +0x08 and
 * +0x4E / +0x1A0 are the fields func_8C0489E4 (code_8C0486A0.c) writes.
 * The defaults written below are named by offset: their meaning (SADX
 * view / search / wall-bounce parameters) is not verified on the DC.
 */
typedef struct {
    motionwk mw;                /* 0x000 motionwk2 layout: +0x08 = mw.spd.y (0.7 after a no-bounce hit, 8C0489E4) */
    task *marker_tp;            /* 0x040 E-102 lock-on marker task (8C0D332C / 8C0D32C0); NULL = not locked */
    TaskFuncPtr dest;           /* 0x044 E-102: the task's own dest, saved when hooked (8C0D332C) */
    Sint32 unk48;               /* 0x048 */
    Sint16 unk4C;               /* 0x04C */
    Uint16 flag;                /* 0x04E 0x1000 hit by a player / projectile, 0x800 no-bounce hit, 0x400 cleared by a bounce (8C0489E4) */
    NJS_POINT3 home;            /* 0x050 twp->pos at init (SADX ewp->home) */
    Uint8 pad05C[0xA0 - 0x5C];  /* 0x05C */
    Float fA0;                  /* 0x0A0 init 5.0 */
    Float fA4;                  /* 0x0A4 */
    Float fA8;                  /* 0x0A8 init 10.0 */
    Float fAC;                  /* 0x0AC init 15.0 */
    Float fB0;                  /* 0x0B0 init -1.0 */
    Float fB4;                  /* 0x0B4 init 50.0 */
    Float fB8;                  /* 0x0B8 init 0.7 */
    Float fBC;                  /* 0x0BC init 0.9 */
    Float fC0;                  /* 0x0C0 init 0.8 */
    Float fC4;                  /* 0x0C4 init 0.1 */
    Sint32 lC8;                 /* 0x0C8 init 0x400 */
    Uint8 pad0CC[0x1A0 - 0xCC]; /* 0x0CC */
    Float f1A0;                 /* 0x1A0 squash speed (0.35 on a hit, 8C0489E4; spring 8C0488A2) */
    Sint32 unk1A4;              /* 0x1A4 */
    Sint32 l1A8;                /* 0x1A8 init 0x100 */
    Uint8 pad1AC[0x1D4 - 0x1AC]; /* 0x1AC */
    Angle ang1D4;               /* 0x1D4 init 0x38E3 (80 degrees): view half-angle of 8C04712A (DiffAngle against twp->ang.y) */
    Float f1D8;                 /* 0x1D8 init 22500.0 (150 squared): view range squared of 8C04712A */
    Float f1DC;                 /* 0x1DC init 1225.0 (35 squared): 8C04712A sees a player this close at any angle */
    Uint8 pad1E0[0x23C - 0x1E0]; /* 0x1E0 */
} UNK_enemywk;                  /* 0x23C */

SA1_ASSERT_SIZE(UNK_enemywk, 0x23C);
SA1_ASSERT_OFFSET(UNK_enemywk, marker_tp, 0x40);
SA1_ASSERT_OFFSET(UNK_enemywk, flag, 0x4E);
SA1_ASSERT_OFFSET(UNK_enemywk, home, 0x50);
SA1_ASSERT_OFFSET(UNK_enemywk, fA0, 0xA0);
SA1_ASSERT_OFFSET(UNK_enemywk, lC8, 0xC8);
SA1_ASSERT_OFFSET(UNK_enemywk, f1A0, 0x1A0);
SA1_ASSERT_OFFSET(UNK_enemywk, l1A8, 0x1A8);
SA1_ASSERT_OFFSET(UNK_enemywk, ang1D4, 0x1D4);
SA1_ASSERT_OFFSET(UNK_enemywk, f1DC, 0x1DC);

/*
 * EnemyInitialize: allocate the enemy work of tp (twp = tp->twp at every
 * call site) as its motion work, with the home position and the defaults.
 * Without memory the task is freed and NULL returned (the caller must not
 * touch tp then; the rival target 8C085532 does not check).
 */
UNK_enemywk *func_8C047040(task *tp, taskwk *twp)
{
    UNK_enemywk *ewp;

    ewp = CAlloc(1, sizeof(UNK_enemywk));
    if (ewp == NULL) {
        FreeTask(tp);
        return NULL;
    }
    tp->mwp = &ewp->mw;
    ewp->home = twp->pos;
    ewp->fAC = 15.0f;
    ewp->fA0 = 5.0f;
    ewp->fA8 = 10.0f;
    ewp->fB0 = -1.0f;
    ewp->fB4 = 50.0f;
    ewp->fBC = 0.9f;
    ewp->fC0 = 0.79999997f;
    ewp->fC4 = 0.099999996f;
    ewp->fB8 = 0.7f;
    ewp->lC8 = 0x400;
    ewp->ang1D4 = 0x38E3;
    ewp->f1D8 = 22500.0f;
    ewp->f1DC = 1225.0f;
    ewp->l1A8 = 0x100;
    ewp->flag |= 0x480;
    ewp->flag &= 0xCFFF;
    return ewp;
}
