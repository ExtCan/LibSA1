#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0B2E30-8C0B2E4C (chunk 8C0B2D40-8C0B2EA0, function 7 of 7):
 * destructor of Sonic's temporary speed-limit task.
 *
 * func_8C0B2EA0(twp, ?, pwp) (called from Sonic's code func_8C0B14E0)
 * creates a task with CreateElementalTask(8, 0, func_8C0B2DFE), stores
 * {twp, pwp, pwp->p.max_x_spd} in its any-work, sets pwp->p.max_x_spd to
 * 0.5f and installs this function as the destructor. The executor
 * func_8C0B2DFE frees the task once twp->flag & 0x200 is clear (or player 1
 * is gone); this destructor then puts the saved max_x_spd back, as long as
 * player 1 still exists.
 */

/* task.awp of the speed-limit task (CAlloc(1, 0x10), 0x0C used) */
typedef struct {
    taskwk *twp;                /* 0x00 player taskwk (func_8C0B2DFE tests flag & 0x200) */
    playerwk *pwp;              /* 0x04 */
    Float max_x_spd;            /* 0x08 saved pwp->p.max_x_spd */
} SPDLIMITWK;

void func_8C0B2E30(task *tp)
{
    playerwk *pwp;

    pwp = ((SPDLIMITWK *)tp->awp)->pwp;
    if (playertwp[0]) {
        pwp->p.max_x_spd = ((SPDLIMITWK *)tp->awp)->max_x_spd;
    }
}
