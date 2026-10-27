/*
 * 1ST_READ 8C0CB380-8C0CB580 (chunk 8C0CB380): Knuckles sparkle effect
 * (sprite D_8C353530, texlist_KNU_EFF).
 *  func_8C0CB380  draw one additive sprite at pos with alpha a
 *  func_8C0CB3F6  trail sparkle disp (tasks created by func_8C0CB44E)
 *  func_8C0CB414  trail sparkle exec: fades out over 6 frames
 *  func_8C0CB44E  orbiting sparkle exec (tasks created by func_8C0CB580):
 *                 circles player 0 for 18 frames and leaves a trail
 *                 sparkle every frame
 *
 * taskwk use: timer.f = age (0..1), value.f = alpha scale,
 * ang.x = orbit angle, ang.y = orbit angular speed, scl.x = height above
 * the player, scl.y = orbit radius.
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST texlist_KNU_EFF;     /* 8C3538DC */
extern NJS_SPRITE D_8C353530;           /* sparkle sprite (3D) */

void func_8C0CB380(NJS_POINT3 *pos, Float a)
{
    if (B_8C754EC8 != 0) {
        return;
    }
    njSetTexture(&texlist_KNU_EFF);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 10);
    func_8C043A20(a, 1.0f, 1.0f, 1.0f);
    njPushMatrix(NULL);
    njTranslateV(NULL, pos);
    njDrawSprite3D(&D_8C353530, 0, 50);
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}

void func_8C0CB3F6(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    func_8C0CB380(&twp->pos, (1.0f - twp->timer.f) * twp->value.f);
}

void func_8C0CB414(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    twp->timer.f += 0.166666675f;
    if (twp->timer.f >= 1.0f) {
        FreeTask(tp);
        return;
    }
    func_8C0CB380(&twp->pos, (1.0f - twp->timer.f) * twp->value.f);
}

void func_8C0CB44E(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;
    task *ctp;
    Float a;

    twp = tp->twp;
    twp->timer.f += 0.05555556f;
    if (twp->timer.f >= 1.0f) {
        FreeTask(tp);
        return;
    }
    ptwp = B_8C78C608[0];
    if (ptwp == NULL) {
        FreeTask(tp);
        return;
    }
    twp->ang.x += twp->ang.y;
    twp->pos.x = SA1_FMAC(twp->scl.y, njCos(twp->ang.x), ptwp->pos.x);
    twp->pos.y = twp->scl.x + ptwp->pos.y;
    twp->pos.z = SA1_FMAC(twp->scl.y, njSin(twp->ang.x), ptwp->pos.z);
    a = twp->value.f * (1.0f - twp->timer.f);
    func_8C0CB380(&twp->pos, a);
    if ((ctp = CreateElementalTask(2, 6, func_8C0CB414)) != NULL) {
        ctp->twp->pos = twp->pos;
        ctp->twp->value.f = a;
        ctp->disp = func_8C0CB3F6;
    }
}
