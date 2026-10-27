/*
 * 1ST_READ 8C0DF540-8C0DF760 (chunk 8C0DF540): Amy's hammer hit spark
 * (texlist_AMY_EFF), created by func_8C0DCCA8 through func_8C0DF630.
 *  func_8C0DF540  disp: draws sprite D_8C4BF078 at twp->pos with alpha
 *                 (0.25 - (age - 0.5)^2) * 2 (fades in and out; blending
 *                 modes 8/10)
 *  func_8C0DF5CC  exec: ages the effect by 1/15 per frame (frees it at 1),
 *                 damps the velocity twp->scl by 0.9, moves it, draws it
 *  func_8C0DF630  creates the effect at pos with a random unit direction
 *                 times 0.25 as velocity
 *
 * Effect taskwk use: counter.f = age 0..1, scl = velocity.
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST texlist_AMY_EFF;     /* 8C4BF3E4 */
extern NJS_SPRITE D_8C4BF078;           /* effect sprite */

void func_8C0DF540(task *tp)
{
    taskwk *twp;
    Float f;
    Float c;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    njSetTexture(&texlist_AMY_EFF);
    f = twp->counter.f - 0.5f;
    f = (0.25f - f * f) * 2.0f;
    c = 1.0f;
    func_8C043A20(f, c, c, c);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 10);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    njDrawSprite3D(&D_8C4BF078, 0, 0x32);
    njPopMatrix(1);
    c = 0.0f;
    func_8C043A20(c, c, c, c);
}

void func_8C0DF5CC(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (!((twp->counter.f += 0.066666665f) < 1.0f)) {
        FreeTask(tp);
        return;
    }
    twp->scl.x *= 0.9f;
    twp->scl.y *= 0.9f;
    twp->scl.z *= 0.9f;
    twp->pos.x += twp->scl.x;
    twp->pos.y += twp->scl.y;
    twp->pos.z += twp->scl.z;
    func_8C0DF540(tp);
}

void func_8C0DF630(NJS_POINT3 *pos)
{
    task *tp;
    NJS_VECTOR v;

    if ((tp = CreateElementalTask(2, 6, func_8C0DF5CC)) == NULL) {
        return;
    }
    v.x = SA1_FMAC(3.0517578125e-05f, (Float)rand(), -0.5f);
    v.y = SA1_FMAC(3.0517578125e-05f, (Float)rand(), -0.5f);
    v.z = SA1_FMAC(3.0517578125e-05f, (Float)rand(), -0.5f);
    if (njScalor(&v) == 0.0f) {
        v.x = 1.0f;
        v.y = v.z = 0.0f;
    } else {
        njUnitVector(&v);
    }
    tp->twp->scl.x = v.x * 0.25f;
    tp->twp->scl.y = v.y * 0.25f;
    tp->twp->scl.z = v.z * 0.25f;
    tp->disp = func_8C0DF540;
    tp->twp->pos = *pos;
}
