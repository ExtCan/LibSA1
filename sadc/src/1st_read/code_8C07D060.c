/*
 * Player slot helpers (8C07D060-8C07D2A2):
 *   8C07D060 index of the player nearest to a point
 *   8C07D18E slot of the rival player (taskwk.id 3 <-> 1), -1 if none
 *   8C07D1D0 is player pno standing on (pwp->ttp) the task tp
 *   8C07D226 / 8C07D250 suspend / resume a player task (exec + disp)
 *   8C07D274 exec of the speed-up task (frees itself after 600 frames)
 *
 * 8C07D2A2 and 8C07D2DA (same chunk) are not part of this file yet.
 */

#include "common.h"
#include "task.h"
#include "sa1/sa1.h"

/*
 * Any work (task.awp, CAlloc(1, 0x10)) of the speed-up task (exec
 * func_8C07D274, dest func_8C07D2A2, set up by func_8C07D2DA, spawned by
 * func_8C07D3A0): pno at 0x00, frame counter at 0x02 (func_8C07D274),
 * saved playerpwp[pno]->p.run_accel/max_x_spd/lim_frict at 0x04/0x08/0x0C.
 */
typedef struct {
    Uint8 pno;                  /* 0x00 */
    Uint8 pad01;                /* 0x01 */
    Uint16 timer;               /* 0x02 */
    Float run_accel;            /* 0x04 */
    Float max_x_spd;            /* 0x08 */
    Float lim_frict;            /* 0x0C */
} PL_SPDUPWK;

SA1_ASSERT_SIZE(PL_SPDUPWK, 0x10);

/* saved exec/disp of the 8 player tasks (func_8C07D226 / func_8C07D250) */
extern TaskFuncPtr B_8C798798[8];
extern TaskFuncPtr B_8C7987B8[8];

/* index of the player nearest to pos (0 if no slot is used) */
Sint32 func_8C07D060(NJS_POINT3 *pos)
{
    Sint32 i;
    Sint32 nearest;
    Float min;
    Float dist;
    NJS_VECTOR v;

    nearest = 0;
    min = 10000000.0f;
    for (i = 0; i < 8; i++) {
        if (playertwp[i] != NULL) {
            v = *pos;
            njSubVector(&v, &playertwp[i]->pos);
            dist = njScalor(&v);
            if (dist < min) {
                min = dist;
                nearest = i;
            }
        }
    }
    return nearest;
}

/* slot of the rival player (taskwk.id 3 <-> 1), -1 if none; the nested
 * ifs keep the second playertwp[i] load of the original */
Sint32 func_8C07D18E(Uint8 pno)
{
    Sint32 i;
    Sint32 id;

    if ((Uint8)playertwp[pno]->id == 3) {
        id = 1;
    } else {
        id = 3;
    }
    for (i = 0; i < 8; i++) {
        if (playertwp[i] != NULL) {
            if ((Uint8)playertwp[i]->id == id) {
                return i;
            }
        }
    }
    return -1;
}

Bool func_8C07D1D0(Uint8 pno, task *tp)
{
    taskwk *twp;
    playerwk *pwp;

    twp = playertwp[pno];
    pwp = playerpwp[pno];
    if (pwp != NULL && twp != NULL && (twp->flag & 1) && pwp->ttp == tp) {
        return 1;
    }
    return 0;
}

void func_8C07D226(Uint8 pno)
{
    task *tp;

    tp = playertp[pno];
    if (tp != NULL) {
        B_8C798798[pno] = tp->exec;
        B_8C7987B8[pno] = tp->disp;
        tp->exec = NULL;
        tp->disp = NULL;
    }
}

void func_8C07D250(Uint8 pno)
{
    task *tp;

    tp = playertp[pno];
    if (tp != NULL) {
        tp->exec = B_8C798798[pno];
        tp->disp = B_8C7987B8[pno];
    }
}

/*
 * Exec of the speed-up task: frees the task (dest func_8C07D2A2 restores the
 * saved parameters) when the player is gone or after 600 frames. tp->awp is
 * read through the cast twice on purpose: only the CSE'd expression (not a
 * PL_SPDUPWK * local) gives the original register allocation.
 */
void func_8C07D274(task *tp)
{
    playerwk *pwp;

    pwp = playerpwp[((PL_SPDUPWK *)tp->awp)->pno];
    if (pwp == NULL || ((PL_SPDUPWK *)tp->awp)->timer++ >= 600) {
        FreeTask(tp);
    }
}
