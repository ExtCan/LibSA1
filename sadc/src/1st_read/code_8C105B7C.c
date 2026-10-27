/*
 * 8C105B7C-8C105D62: two helpers of Zero (E-100), whose motion work is the
 * global mtnjvwk B_8C7A5140 (action list D_8C5C69C8; see code_8C102EAE.c).
 *
 * func_8C105B7C: evaluates the object's current motion (node 30) at the
 * task position/yaw and stores the world position of that node in
 * B_8C7A51E4.
 * func_8C105C08: refreshes that node position, aims from it towards
 * B_8C7A51CC (direction B_8C7A51FC scaled by 4, yaw B_8C7A51DC), fills the
 * 16-point trail B_8C7A5208 with it, starts func_8C10689E and moves
 * collision shape 1 there. Returns 1 when the target is at the node.
 */

#include "sa1/sa1.h"

extern mtnjvwk B_8C7A5140;          /* Zero's motion work (mtnmode 2 = link action in actwkptr) */
extern PL_ACTION D_8C5C69C8[];      /* Zero's action list (game data) */
extern NJS_MATRIX B_8C7A5308;       /* node matrix out of func_8C0583A0 */
extern NJS_POINT3 B_8C7A51E4;       /* world position of node 30 */
extern NJS_POINT3 B_8C7A51CC;       /* target position */
extern NJS_POINT3 B_8C7A51F0;       /* copy of B_8C7A51E4 at the start */
extern NJS_VECTOR B_8C7A51FC;       /* B_8C7A51CC - B_8C7A51E4, then * 4.0f */
extern NJS_POINT3 B_8C7A5208[16];   /* trail points */
extern Sint32 B_8C7A51C8;
extern Sint32 B_8C7A51D8;
extern Angle B_8C7A51DC;            /* yaw of B_8C7A51FC */
extern Sint32 B_8C7A51E0;

void func_8C10689E(task *tp);

void func_8C105B7C(task *tp)
{
    taskwk *twp;
    NJS_ACTION *action;
    NJS_POINT3 v;
    NJS_POINT3 out;
    volatile Angle ang;         /* volatile: the original keeps it in a stack slot (MOV.L R2,@R15 / MOV.L @R15,R5) */

    twp = tp->twp;
    njPushMatrix(D_8C67C640);
    v.x = 0.0f;
    v.y = 0.0f;
    v.z = 0.0f;
    njTranslateV(NULL, &twp->pos);
    if ((ang = twp->ang.y) != 0) {
        njRotateY(NULL, ang);
    }
    if (B_8C7A5140.mtnmode == 2) {
        action = B_8C7A5140.actwkptr;
    } else {
        action = D_8C5C69C8[B_8C7A5140.reqaction].actptr;
    }
    func_8C0583A0(NULL, action, B_8C7A5140.nframe, 30, B_8C7A5308);
    njCalcPoint(B_8C7A5308, &v, &out);
    B_8C7A51E4 = out;
    njPopMatrix(1);
}

Sint32 func_8C105C08(task *tp)
{
    taskwk *twp;
    Sint32 i;

    twp = tp->twp;
    func_8C105B7C(tp);
    B_8C7A51F0 = B_8C7A51E4;
    B_8C7A51FC.x = B_8C7A51CC.x - B_8C7A51E4.x;
    B_8C7A51FC.y = B_8C7A51CC.y - B_8C7A51E4.y;
    B_8C7A51FC.z = B_8C7A51CC.z - B_8C7A51E4.z;
    if (njScalor(&B_8C7A51FC) == 0.0f) {
        B_8C7A51C8 = 0;
        return B_8C7A51D8 = 1;
    } else {
        B_8C7A51FC.x *= 4.0f;
        B_8C7A51FC.y *= 4.0f;
        B_8C7A51FC.z *= 4.0f;
        B_8C7A51DC = -SA1_FTOI(atan2f(B_8C7A51FC.z, B_8C7A51FC.x) * 10430.381f);
        B_8C7A51E0 = 0;
        for (i = 0; i < 16; i++) {
            B_8C7A5208[i] = B_8C7A51E4;
        }
        CreateElementalTask(2, 6, func_8C10689E);
        CWP(twp)->info[1].attr &= ~0x10;
        CWP(twp)->info[1].center = B_8C7A51F0;
        B_8C7A51D8 = 0;
        return 0;
    }
}
