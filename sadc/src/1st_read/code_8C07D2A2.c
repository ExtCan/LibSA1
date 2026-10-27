/*
 * Speed-up power-up task (8C07D2A2-8C07D360), created by func_8C07D3A0
 * (item box "speed up") with CreateElementalTask(8, 2, func_8C07D2DA).
 *
 *   func_8C07D2DA  first exec: saves playerpwp[pno]->p.run_accel /
 *                  max_x_spd / lim_frict in the any work, doubles them,
 *                  requests sound/music 85 (func_8C107C80) unless the stage
 *                  is 0x0401, then installs exec func_8C07D274 (timer) and
 *                  dest func_8C07D2A2.
 *   func_8C07D2A2  dest: restores the three saved parameters.
 */

#include "common.h"
#include "task.h"
#include "sa1/sa1.h"

/*
 * Any work (task.awp, CAlloc(1, 0x10)): pno at 0x00 (written by
 * func_8C07D3A0), frame counter at 0x02 (func_8C07D274), saved
 * physics parameters at 0x04/0x08/0x0C.
 */
typedef struct {
    Uint8 pno;                  /* 0x00 */
    Uint8 pad01;                /* 0x01 */
    Uint16 timer;               /* 0x02 func_8C07D274 */
    Float run_accel;            /* 0x04 */
    Float max_x_spd;            /* 0x08 */
    Float lim_frict;            /* 0x0C */
} PL_SPDUPWK;

SA1_ASSERT_SIZE(PL_SPDUPWK, 0x10);

void func_8C07D274(task *tp);
void func_8C107C80(Sint32 no);

void func_8C07D2A2(task *tp)
{
    playerwk *pwp;

    pwp = playerpwp[((PL_SPDUPWK *)tp->awp)->pno];
    if (pwp) {
        pwp->p.run_accel = ((PL_SPDUPWK *)tp->awp)->run_accel;
        pwp->p.max_x_spd = ((PL_SPDUPWK *)tp->awp)->max_x_spd;
        pwp->p.lim_frict = ((PL_SPDUPWK *)tp->awp)->lim_frict;
    }
}

void func_8C07D2DA(task *tp)
{
    playerwk *pwp;

    pwp = playerpwp[((PL_SPDUPWK *)tp->awp)->pno];
    if (!pwp) {
        FreeTask(tp);
        return;
    }
    ((PL_SPDUPWK *)tp->awp)->run_accel = pwp->p.run_accel;
    ((PL_SPDUPWK *)tp->awp)->max_x_spd = pwp->p.max_x_spd;
    ((PL_SPDUPWK *)tp->awp)->lim_frict = pwp->p.lim_frict;
    pwp->p.run_accel = pwp->p.run_accel * 2.0f;
    pwp->p.max_x_spd = pwp->p.max_x_spd * 2.0f;
    pwp->p.lim_frict = pwp->p.lim_frict * 2.0f;
    if ((Uint16)GetStageNumber() != 0x401) {
        func_8C107C80(85);
    }
    tp->exec = func_8C07D274;
    tp->dest = func_8C07D2A2;
}
