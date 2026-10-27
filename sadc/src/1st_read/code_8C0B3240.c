/*
 * 8C0B3240-8C0B33A0 (chunk asm/1st_read/text/8C0B3240.src).
 *
 * A small helper task that temporarily changes a player's physics
 * parameters while the equipment bit 0x8000 is set:
 *   func_8C0B32B6  init (exec of the new task, created by func_8C0AFF1A):
 *                  saves three parameters into the task's any-work,
 *                  overrides four, then installs exec/dest.
 *   func_8C0B3280  exec: destroys itself when the player is gone or the
 *                  equipment bit 0x8000 is cleared.
 *   func_8C0B3240  dest: restores the saved parameters.
 * Note: the original saves air_resist/air_resist_air/air_accel but
 * restores into max_x_spd/run_accel/air_accel (kept as in the game).
 *
 *   func_8C0B332E  player mode helper: when twp->flag & 0x100, switch to
 *                  mode 2, request action 29 and clear flag bits 0x500.
 */

#include "common.h"
#include "task.h"
#include "sa1/player.h"
#include "sa1/engine.h"

/* any-work (task->awp, 0x10 bytes) of the parameter task */
typedef struct {
    Sint8 pno;                  /* 0x00 player number */
    Float save0;                /* 0x04 saved p.air_resist (restored into p.max_x_spd) */
    Float save1;                /* 0x08 saved p.air_resist_air (restored into p.run_accel) */
    Float save2;                /* 0x0C saved p.air_accel */
} PARAMWK_8C0B32B6;

void func_8C0B3240(task *tp)
{
    PARAMWK_8C0B32B6 *wp;
    playerwk *pwp;

    wp = (PARAMWK_8C0B32B6 *)tp->awp;
    pwp = GetPlayerWorkPtr(wp->pno);
    if (pwp) {
        pwp->p.max_x_spd = wp->save0;
        pwp->p.run_accel = wp->save1;
        pwp->p.air_accel = wp->save2;
        pwp->p.center_height = 5.3999997f;     /* 5.4f (Sonic default) */
    }
}

void func_8C0B3280(task *tp)
{
    playerwk *pwp;

    pwp = GetPlayerWorkPtr(((PARAMWK_8C0B32B6 *)tp->awp)->pno);
    if (!pwp) {
        DestroyTask(tp);
        return;
    }
    /* read as a signed halfword in the original (no EXTU.W) */
    if (!((Sint16)pwp->equipment & 0x8000)) {
        DestroyTask(tp);
        return;
    }
}

void func_8C0B32B6(task *tp)
{
    PARAMWK_8C0B32B6 *wp;
    playerwk *pwp;

    wp = (PARAMWK_8C0B32B6 *)tp->awp;
    pwp = GetPlayerWorkPtr(wp->pno);
    if (!pwp) {
        DestroyTask(tp);
        return;
    }
    wp->save0 = pwp->p.air_resist;
    wp->save1 = pwp->p.air_resist_air;
    wp->save2 = pwp->p.air_accel;
    pwp->p.air_resist = -0.00099999996f;  /* -0.001f */
    pwp->p.air_resist_air = -0.0019999999f; /* -0.002f */
    pwp->p.air_accel = 0.049999998f;       /* 0.05f */
    pwp->p.center_height = 6.3999997f;     /* 6.4f */
    tp->dest = func_8C0B3240;
    tp->exec = func_8C0B3280;
}

void func_8C0B332E(taskwk *twp, playerwk *pwp)
{
    if (twp->flag & 0x100) {
        twp->mode = 2;
        pwp->mj.reqaction = 29;
        twp->flag &= ~0x500;
    }
}
