/*
 * 1ST_READ 8C0B6072-8C0B60DC (chunk 8C0B5D60-8C0B6120, functions 7-8/8).
 *
 * Sonic's ice model effect: func_8C0B60B8 creates the task for player pno,
 * func_8C0B6072 is its first exec. It copies the player's requested action
 * into wtimer, sets unit scale, installs exec func_8C0B6018, display
 * func_8C0B5D60 (njSetTexture(value.ptr), draws counter.ptr at pos/scl)
 * and an empty destructor func_8C0B5DCA, and keeps the model Sonic_Ice and
 * its texture list D_8C25658C in counter.ptr/value.ptr.
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_MODEL Sonic_Ice;
extern NJS_TEXLIST D_8C25658C;

void func_8C0B5D60(task *tp);
void func_8C0B6018(task *tp);
void func_8C0B5DCA(task *tp);

void func_8C0B6072(task *tp)
{
    taskwk *twp;
    playerwk *pwp;

    twp = tp->twp;
    pwp = func_8C07CFDE(twp->btimer);
    twp->wtimer = pwp->mj.reqaction;
    twp->scl.x = twp->scl.y = twp->scl.z = 1.0f;
    tp->disp = func_8C0B5D60;
    tp->exec = func_8C0B6018;
    tp->dest = func_8C0B5DCA;
    twp->counter.ptr = &Sonic_Ice;
    twp->value.ptr = &D_8C25658C;
}

void func_8C0B60B8(Sint32 pno)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 3, func_8C0B6072)) != NULL) {
        tp->twp->btimer = pno;
    }
}
