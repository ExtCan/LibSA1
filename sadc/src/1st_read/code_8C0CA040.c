/*
 * 1ST_READ 8C0CA040-8C0CA51E (chunk 8C0CA040-8C0CA780, functions 1-6 of 8):
 * Knuckles' punch effects (texlist_KNU_EFF).
 *
 *  func_8C0CA040  exec of the punch trail (created by func_8C0C7786 during
 *                 the trail window of the punch): counts counter.f up by 0.5
 *                 per frame and frees the task at 6, drawing every frame
 *  func_8C0CA094  disp of the trail: sprite D_8C3534BC cell (Uint32)counter.f
 *                 at twp->pos, alpha 0.5 (blending modes 8/10)
 *  func_8C0CA11C  exec of the punch hit effect (func_8C0C7786 at the effect
 *                 frame, scl = direction * 2): one collision shape
 *                 (D_8C35368C, CCL id 4); counter.f falls from 1 by 1/12 per
 *                 frame (freed at 0), pos += scl, shape radius = 8 * counter.f
 *  func_8C0CA232  disp of the hit effect: sprite D_8C353510 at twp->pos,
 *                 alpha 0.8 * counter.f
 *  func_8C0CA2AC  exec of a spark particle (no direct caller found): random
 *                 unit direction * 0.5 in scl, life counter.f 1 -> 0 in 150
 *                 frames, pos += scl, scl *= 0.975 per frame
 *  func_8C0CA454  disp of the spark: billboard (camera ang.y/ang.x) sprite
 *                 D_8C3534F0 scaled by 4 - counter.f, alpha
 *                 0.015 * counter.f + 0.005
 *
 * Effect taskwk use: mode = state, counter.f = timer/life, pos = position,
 * scl = velocity, cwp = colliwk (func_8C0CA11C only).
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST texlist_KNU_EFF;     /* 8C3538DC */
extern NJS_SPRITE D_8C3534BC;           /* punch trail sprite (cells 0..5) */
extern NJS_SPRITE D_8C353510;           /* punch hit sprite */
extern NJS_SPRITE D_8C3534F0;           /* spark sprite */
extern CCL_INFO D_8C35368C[];           /* punch hit collision shape (1 entry) */

void func_8C0CA094(task *tp);
void func_8C0CA232(task *tp);
void func_8C0CA454(task *tp);

void func_8C0CA040(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        twp->counter.f = 0.0f;
        twp->mode = 1;
        tp->disp = func_8C0CA094;
        break;
    case 1:
        if (!((twp->counter.f += 0.5f) < 6.0f)) {
            FreeTask(tp);
            return;
        }
        break;
    }
    func_8C0CA094(tp);
    func_8C0919CC(tp);
}

void func_8C0CA094(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    func_8C043A20(0.5f, 1.0f, 1.0f, 1.0f);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 10);
    njSetTexture(&texlist_KNU_EFF);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    /* counter.f is 0..6 here: the unsigned conversion is in range */
    njDrawSprite3D(&D_8C3534BC, (Uint32)twp->counter.f, 0x32);
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}

void func_8C0CA11C(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        twp->counter.f = 1.0f;
        CCL_Init(tp, D_8C35368C, 4, 1, 1);
        twp->mode = 1;
        tp->disp = func_8C0CA232;
        break;
    case 1:
        if (!((twp->counter.f += -0.083333337f) > 0.0f)) {
            FreeTask(tp);
            return;
        }
        twp->pos.x += twp->scl.x;
        twp->pos.y += twp->scl.y;
        twp->pos.z += twp->scl.z;
        CWP(twp)->info->a = twp->counter.f * 8.0f;
        CCL_Entry(twp);
        break;
    }
    func_8C0CA232(tp);
    func_8C0919CC(tp);
}

void func_8C0CA232(task *tp)
{
    taskwk *twp;
    Float a;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    a = twp->counter.f * 0.79999997f;
    func_8C043A20(a, 1.0f, 1.0f, 1.0f);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 10);
    njSetTexture(&texlist_KNU_EFF);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    njDrawSprite3D(&D_8C353510, 0, 0x32);
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}

void func_8C0CA2AC(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        twp->counter.f = 1.0f;
        twp->scl.x = SA1_FMAC(3.05175785e-05f, (Float)rand(), -0.5f);
        twp->scl.y = SA1_FMAC(3.05175785e-05f, (Float)rand(), -0.5f);
        twp->scl.z = SA1_FMAC(3.05175785e-05f, (Float)rand(), -0.5f);
        if (njScalor(&twp->scl) == 0.0f) {
            twp->scl.x = 1.0f;
            twp->scl.y = twp->scl.z = 0.0f;
        } else {
            njUnitVector(&twp->scl);
        }
        twp->scl.x *= 0.5f;
        twp->scl.y *= 0.5f;
        twp->scl.z *= 0.5f;
        twp->mode = 1;
        tp->disp = func_8C0CA454;
        break;
    case 1:
        if (!((twp->counter.f += -0.0066666665f) > 0.0f)) {
            FreeTask(tp);
            return;
        }
        twp->pos.x += twp->scl.x;
        twp->pos.y += twp->scl.y;
        twp->pos.z += twp->scl.z;
        twp->scl.x *= 0.97499998f;
        twp->scl.y *= 0.97499998f;
        twp->scl.z *= 0.97499998f;
        break;
    }
    func_8C0CA454(tp);
    func_8C0919CC(tp);
}

void func_8C0CA454(task *tp)
{
    taskwk *twp;
    Float a;
    Float s;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    a = SA1_FMAC(0.015f, twp->counter.f, 0.005f);
    s = 4.0f - twp->counter.f * 1.0f;
    func_8C043A20(a, 1.0f, 1.0f, 1.0f);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 10);
    njSetTexture(&texlist_KNU_EFF);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    camera_twp->ang.y ? njRotateY(NULL, camera_twp->ang.y) : (void)0;
    camera_twp->ang.x ? njRotateX(NULL, camera_twp->ang.x) : (void)0;
    njScale(NULL, s, s, s);
    njDrawSprite3D(&D_8C3534F0, 0, 0x2A);
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}
