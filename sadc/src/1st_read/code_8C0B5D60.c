#include "sa1/sa1.h"

/*
 * Sonic's ice block effect (8C0B5D60-8C0B6120): a block of ice drawn at a
 * player's collision centre while that player's requested action stays the
 * same (twp->wtimer), which then shatters into 16 falling particles.
 *  twp->btimer:      player number
 *  twp->counter.ptr: NJS_MODEL to draw (Sonic_Ice / Sonic_Ice_Particles)
 *  twp->value.ptr:   NJS_TEXLIST (D_8C25658C)
 * Particles: scl.y = vertical speed, ang.x/ang.z spin, scl.x/scl.z size.
 */

extern NJS_MODEL Sonic_Ice;
extern NJS_MODEL Sonic_Ice_Particles;
extern NJS_TEXLIST D_8C25658C;

void func_8C0B5F60(taskwk *twp);

/* display: model twp->counter.ptr at pos, rotated Z-X-Y, scaled by scl */
void func_8C0B5D60(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    njSetTexture(twp->value.ptr);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    njScaleV(NULL, &twp->scl);
    func_8C036F12(twp->counter.ptr);
    njPopMatrix(1);
}

/* destructor: nothing to free */
void func_8C0B5DCA(task *tp)
{
}

/* particle exec: fly outwards along ang.y, fall (scl.y), spin, draw */
void func_8C0B5DCE(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (func_8C0919CC(tp) == 0) {
        SA1_FMAC_ACC(twp->pos.x, njSin(twp->ang.y), 2.0f);
        SA1_FMAC_ACC(twp->pos.z, njCos(twp->ang.y), 2.0f);
        twp->pos.y += twp->scl.y;
        twp->scl.y -= 0.099999996f;
        if (twp->scl.y < -6.0f) {
            twp->scl.y = -6.0f;
        }
        twp->ang.x += 0x100;
        func_8C0B5D60(tp);
    }
}

/* particle init: random size and spin, then install the particle callbacks */
void func_8C0B5E64(task *tp)
{
    taskwk *twp;
    Float f;

    twp = tp->twp;
    func_8C07CFDE(twp->btimer);
    f = (Float)rand() * 3.0517578125e-05f;
    twp->scl.x = f + 0.5f;
    twp->ang.x = SA1_FTOI(f * 65536.0f);
    f = (Float)rand() * 3.0517578125e-05f;
    twp->scl.z = f + 0.5f;
    twp->ang.z = SA1_FTOI(f * 65536.0f);
    tp->disp = func_8C0B5D60;
    tp->exec = func_8C0B5DCE;
    tp->dest = func_8C0B5DCA;
    twp->counter.ptr = &Sonic_Ice_Particles;
    twp->value.ptr = &D_8C25658C;
}

/* shatter: spawn 16 particles at twp->pos */
void func_8C0B5F60(taskwk *twp)
{
    task *tp;
    Sint32 i;

    for (i = 0; i < 16; i++) {
        tp = CreateElementalTask(2, 6, func_8C0B5E64);
        tp->twp->pos = twp->pos;
        tp->twp->ang.y = SA1_FTOI((Float)rand() * 2.0f);
        tp->twp->scl.y = (Float)rand() * 6.103515625e-05f;
        tp->twp->btimer = twp->btimer;
    }
}

/* ice block exec: follow the player until its requested action changes */
void func_8C0B6018(task *tp)
{
    taskwk *twp;
    playerwk *pwp;
    Uint8 pno;

    twp = tp->twp;
    pno = twp->btimer;
    pwp = func_8C07CFDE(pno);
    if (pwp->mj.reqaction != twp->wtimer) {
        func_8C0B5F60(twp);
        FreeTask(tp);
        return;
    }
    twp->pos = ((colliwk *)B_8C78C608[pno]->cwp)->info->center;
    func_8C0B5D60(tp);
}
