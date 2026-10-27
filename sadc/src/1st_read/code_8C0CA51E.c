/*
 * 1ST_READ 8C0CA51E-8C0CA704 (chunk 8C0CA040, functions 7-8): a small
 * Knuckles effect particle (texlist_KNU_EFF) that drifts along a velocity
 * which decays towards zero, fading out over 30 frames.
 *  func_8C0CA51E  exec: frees the task after 30 frames (wtimer); adds to
 *                 the velocity twp->scl a drag of -0.05 along the direction
 *                 of scl + (0, 0.1, 0) (drag components below 0.001 snap to
 *                 0), moves twp->pos by it, draws, then calls the SET
 *                 manager query func_8C0919CC
 *  func_8C0CA680  disp: sprite D_8C3534F0 at twp->pos with alpha
 *                 (30 - wtimer) / 30, blending modes 8/10
 *
 * Effect taskwk use: wtimer = age in frames, scl = velocity.
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST texlist_KNU_EFF;     /* 8C3538DC */
extern NJS_SPRITE D_8C3534F0;           /* effect sprite */

void func_8C0CA680(task *tp);

void func_8C0CA51E(task *tp)
{
    taskwk *twp;
    NJS_POINT3 oldpos;
    NJS_VECTOR v;

    twp = tp->twp;
    if (twp->mode == 0) {
        twp->mode = 1;
    }
    if (++twp->wtimer > 30) {
        FreeTask(tp);
        return;
    }
    oldpos = twp->pos;                  /* dead copy (kept: the original stores it) */
    v.x = twp->scl.x;
    v.y = twp->scl.y + 0.099999996f;
    v.z = twp->scl.z;
    if (njScalor(&v) == 0.0f) {
        v.x = 1.0f;
        v.z = 0.0f;
        v.y = 0.0f;
    } else {
        njUnitVector(&v);
    }
    /* the assignments inside the conditions make SHC reload -0.05 for each
     * component while 0.001 stays in a register, as in the original */
    if (_builtin_fabsf(v.x *= -0.049999998f) < 0.00099999995f) {
        v.x = 0.0f;
    }
    if (_builtin_fabsf(v.y *= -0.049999998f) < 0.00099999995f) {
        v.y = 0.0f;
    }
    if (_builtin_fabsf(v.z *= -0.049999998f) < 0.00099999995f) {
        v.z = 0.0f;
    }
    njAddVector(&twp->scl, &v);
    njAddVector(&twp->pos, &twp->scl);
    func_8C0CA680(tp);
    func_8C0919CC(tp);
}

void func_8C0CA680(task *tp)
{
    taskwk *twp;
    Float a;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    a = (Float)(30 - twp->wtimer) * 0.033333332f;  /* separate statement: SHC's scheduling */
    func_8C043A20(a, 1.0f, 1.0f, 1.0f);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 10);
    njSetTexture(&texlist_KNU_EFF);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    njDrawSprite3D(&D_8C3534F0, 0, 0x32);
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}
