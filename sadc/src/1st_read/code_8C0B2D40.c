#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0B2D40 (chunk 8C0B2D40-8C0B2EA0): Sonic helper-task spawners.
 *
 * func_8C0B2D40..func_8C0B2DD8 each create a child task
 * (CreateElementalTask(2, 5, exec): taskwk only, level 5) and copy the
 * player number (counter.b[0]) of the calling player's taskwk into it.
 * Three of the exec functions live in 1ST_READ (8C0B3FA6, 8C0B3FC0,
 * 8C0B3FDA); the other two (0C9004B6, 0C9004D4) are the Super Sonic
 * variants in the B_CHAOS7 overlay (B_CHAOS7.BIN, load address 0x0C900000;
 * symbols func_0C9004B6/func_0C9004D4 in build/B_CHAOS7.symbols.txt).
 *
 * func_8C0B2DFE is the exec of the task created by func_8C0B2EA0 (which
 * temporarily lowers the player's max_x_spd): the task frees itself (and its
 * destructor restores the speed) when player 1 is gone or the owner's
 * twp->flag & 0x200 is clear, or when the assigned button (D_8C19E38C) is
 * released on pad 0.
 */

/* any work (task.awp, CAlloc(1, 0x10)) of the task created by func_8C0B2EA0:
 * the owner's taskwk and playerwk; func_8C0B2EA0 saves pwp->p.max_x_spd
 * (pwp+0xAC) in 'max_x_spd' and sets it to 0.5f, the destructor
 * func_8C0B2E30 restores it */
typedef struct {
    taskwk *twp;                /* 0x00 */
    playerwk *pwp;              /* 0x04 */
    Float max_x_spd;            /* 0x08 */
} SONIC_8C0B2EA0_AWP;

/* exec functions of the spawned tasks (1ST_READ, chunk 8C0B33A0) */
extern void func_8C0B3FA6(task *tp);
extern void func_8C0B3FC0(task *tp);
extern void func_8C0B3FDA(task *tp);

/* Super Sonic variants in the B_CHAOS7 overlay (absolute addresses on DC).
 * PC: provided by the overlay loader. */
#if defined(TARGET_PC)
extern void func_0C9004B6(task *tp);
extern void func_0C9004D4(task *tp);
#define B_CHAOS7_EXEC_0C9004B6 func_0C9004B6
#define B_CHAOS7_EXEC_0C9004D4 func_0C9004D4
#else
#define B_CHAOS7_EXEC_0C9004B6 ((TaskFuncPtr)0x0C9004B6)
#define B_CHAOS7_EXEC_0C9004D4 ((TaskFuncPtr)0x0C9004D4)
#endif

void func_8C0B2D40(taskwk *twp)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 5, func_8C0B3FA6)) != NULL) {
        tp->twp->counter.b[0] = twp->counter.b[0];
    }
}

void func_8C0B2D66(taskwk *twp)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 5, func_8C0B3FC0)) != NULL) {
        tp->twp->counter.b[0] = twp->counter.b[0];
    }
}

void func_8C0B2D8C(taskwk *twp)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 5, B_CHAOS7_EXEC_0C9004B6)) != NULL) {
        tp->twp->counter.b[0] = twp->counter.b[0];
    }
}

void func_8C0B2DB2(taskwk *twp)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 5, func_8C0B3FDA)) != NULL) {
        tp->twp->counter.b[0] = twp->counter.b[0];
    }
}

void func_8C0B2DD8(taskwk *twp)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 5, B_CHAOS7_EXEC_0C9004D4)) != NULL) {
        tp->twp->counter.b[0] = twp->counter.b[0];
    }
}

void func_8C0B2DFE(task *tp)
{
    taskwk *twp;

    twp = ((SONIC_8C0B2EA0_AWP *)tp->awp)->twp;
    if (playertwp[0] && (twp->flag & 0x200)) {
        if (B_8C754C08[0].release & D_8C19E38C) {
            FreeTask(tp);
        }
    } else {
        FreeTask(tp);
    }
}
