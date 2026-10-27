#include "sa1/sa1.h"

/*
 * Homing sprite object (8C0B58E0-8C0B5B80), created by func_8C0B5B80:
 * a camera-facing sprite that flies towards the collision centre of a
 * player (twp->counter.b[0] = player number), scaled by its remaining life
 * (twp->timer.l), and freed when the life runs out.
 *  twp->scl:    target position (centre of the player's first CCL shape)
 *  twp->btimer: sprite index into D_8C1C6AD0
 * Matching: the rotations are "?:" expressions (SHC keeps the angle in a
 * stack slot at @R15, as the original does; volatile locals give an R2/R3
 * swap). The two aim angles are plain locals (spilled by SHC).
 */

extern taskwk *D_8C18B7EC;          /* camera taskwk */
extern NJS_MATRIX D_8C67C640;
extern NJS_SPRITE D_8C1C6AD0[];

/* display: billboard sprite at twp->pos, size timer / 32 */
void func_8C0B58E0(task *tp)
{
    taskwk *twp;
    Float s;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 10);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    s = (Float)twp->timer.l * 0.03125f;
    D_8C18B7EC->ang.y ? njRotateY(NULL, D_8C18B7EC->ang.y) : (void)0;
    D_8C18B7EC->ang.x ? njRotateX(NULL, D_8C18B7EC->ang.x) : (void)0;
    njScale(NULL, s, s, s);
    func_8C63FAE8();
    njDrawSprite3D(&D_8C1C6AD0[(Uint8)twp->btimer], 0, 32);
    func_8C63FB18();
    njPopMatrix(1);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
}

/* exec: turn towards the target and move at most 3 units per frame */
void func_8C0B5994(task *tp)
{
    NJS_VECTOR v;
    NJS_VECTOR p;
    taskwk *twp;
    taskwk *ptwp;
    Float dist;
    Angle angx;
    Angle angy;

    twp = tp->twp;
    if (twp->timer.l-- > 0) {
        ptwp = B_8C78C608[(Uint8)twp->counter.b[0]];
        if (ptwp != NULL) {
            twp->scl = ((colliwk *)ptwp->cwp)->info->center;
        }
        v.x = twp->scl.x - twp->pos.x;
        v.y = twp->scl.y - twp->pos.y;
        v.z = twp->scl.z - twp->pos.z;
        dist = njScalor(&v);
        if (dist == 0.0f) {
            return;
        }
        /* func_8C10D058: asinf-like (elevation of the target) */
        angx = -SA1_FTOI(func_8C10D058(v.y / dist) * 10430.381f);
        angy = SA1_FTOI(atan2f(v.x, v.z) * 10430.381f);
        twp->ang.x = AdjustAngle(twp->ang.x, angx, 0x400);
        twp->ang.y = AdjustAngle(twp->ang.y, angy, 0x700);
        njPushMatrix(D_8C67C640);
        twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        if (dist > 3.0f) {
            dist = 3.0f;
        }
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = dist;
        njCalcPoint(NULL, &v, &p);
        twp->pos.x += p.x;
        twp->pos.y += p.y;
        twp->pos.z += p.z;
        njPopMatrix(1);
        func_8C0B58E0(tp);
        func_8C02F63C(twp);
    } else {
        FreeTask(tp);
    }
}
