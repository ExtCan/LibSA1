/*
 * 1ST_READ 8C0DF100-8C0DF2A0 (chunk 8C0DF100): a fading sprite effect of
 * Amy (texlist_AMY_EFF), created by func_8C0DF2A0.
 *  func_8C0DF100  disp: draws sprite D_8C4BF078 at twp->pos, rotated by
 *                 ang.y/ang.z, with alpha 1 - twp->counter.f (blending
 *                 modes 8/10)
 *  func_8C0DF1A0  exec: ages the effect by 1/30 per frame (frees it at 1),
 *                 moves it by twp->scl (velocity, with gravity on scl.y),
 *                 spins it, then installs the disp function
 *
 * Effect taskwk use: counter.f = age 0..1, scl = velocity, ang.x = spin
 * speed of ang.z.
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST texlist_AMY_EFF;     /* 8C4BF3E4 */
extern NJS_SPRITE D_8C4BF078;           /* effect sprite */

void func_8C0DF100(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    njSetTexture(&texlist_AMY_EFF);
    func_8C043A20(-twp->counter.f + 1.0f, 1.0f, 1.0f, 1.0f);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 10);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    njDrawSprite3D(&D_8C4BF078, 0, 0x22); /* NJD_SPRITE_ALPHA | NJD_SPRITE_COLOR */
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}

void func_8C0DF1A0(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (!((twp->counter.f += 0.033333332f) < 1.0f)) {
        FreeTask(tp);
        return;
    }
    twp->scl.y += -0.049999998f;
    twp->pos.x += twp->scl.x;
    twp->pos.y += twp->scl.y;
    twp->pos.z += twp->scl.z;
    twp->ang.y += 0x400;
    if ((Uint16)twp->ang.z > 0x8000) {
        twp->ang.x -= 0x100;
    } else {
        twp->ang.x += 0x200;
    }
    twp->ang.z += twp->ang.x;
    tp->disp = func_8C0DF100;
}
