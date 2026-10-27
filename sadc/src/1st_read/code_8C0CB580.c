/*
 * 1ST_READ 8C0CB580-8C0CB7A0 (chunk 8C0CB580): Knuckles effects.
 *  func_8C0CB580  spawn one orbiting sparkle (exec func_8C0CB44E, disp
 *                 func_8C0CB3F6) around player 0; rate 0..1 scales the
 *                 orbit speed and the alpha (called by func_8C0C7600)
 *  func_8C0CB628  draw the punch effect model at pos/ang, alpha from the
 *                 motion frame (called by func_8C0C7786)
 *
 * Matching notes: the ptwp local makes SHC load B_8C78C608[0] into R4;
 * "spd += 16" as its own statement gives spd R13 (rand's address R12);
 * the "* 0.15f" written in both branches is what hoists the constant into
 * FR5 before the compare and merges the FMUL after the join.
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST texlist_KNU_EFF;     /* 8C3538DC */
extern NJS_MODEL Knuckles_Punch_Effect; /* 8C38D268 */

extern void func_8C0CB3F6(task *tp);
extern void func_8C0CB44E(task *tp);

void func_8C0CB580(Float rate)
{
    Sint32 spd;
    task *tp;
    taskwk *ptwp;

    ptwp = B_8C78C608[0];
    if (ptwp == NULL) {
        return;
    }
    if ((tp = CreateElementalTask(2, 6, func_8C0CB44E)) == NULL) {
        return;
    }
    spd = SA1_FTOI(rate * 4096.0f);
    spd += 16;
    if ((Float)rand() * 0.000030517578125f < 0.5f) {
        spd = -spd;
    }
    tp->twp->scl.x = SA1_FMAC((Float)rand(), 0.000244140625f, 1.0f);
    tp->twp->scl.y = SA1_FMAC((Float)rand(), 0.0000762939453125f, 2.5f);
    tp->twp->ang.x = SA1_FTOI((Float)rand() * 2.0f);
    tp->twp->ang.y = spd;
    tp->twp->value.f = rate;
    tp->disp = func_8C0CB3F6;
}

void func_8C0CB628(NJS_POINT3 *pos, Angle3 *ang, Float frame)
{
    Float a;

    if (frame < 25.0f) {
        a = (frame - 20.0f) * 0.149999995f;
    } else {
        a = (30.0f - frame) * 0.149999995f;
    }
    if (B_8C754EC8 != 0) {
        return;
    }
    func_8C043A20(a, 1.0f, 1.0f, 1.0f);
    njPushMatrix(NULL);
    njTranslateV(NULL, pos);
    ang->z ? njRotateZ(NULL, ang->z) : (void)0;
    ang->x ? njRotateX(NULL, ang->x) : (void)0;
    0x8000 - ang->y ? njRotateY(NULL, 0x8000 - ang->y) : (void)0;
    SA1_FTOI(frame * 6553.5997f) ? njRotateX(NULL, SA1_FTOI(frame * 6553.5997f)) : (void)0;
    njScale(NULL, 1.0f, 2.0f, 2.0f);
    njSetTexture(&texlist_KNU_EFF);
    func_8C036F12(&Knuckles_Punch_Effect);
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}
