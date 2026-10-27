#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/*
 * E-100 Zero shockwave ring (exec of the shockwave task, set up by
 * func_8C1032AC). The ring expands for 35 frames: its radius (scl.x) grows
 * by 2.828571 per frame, the texture alternates between 21 and 22 and a
 * single collision sphere is kept on the ring at the point nearest to
 * player 0 (direction from the ring centre to the player in the XZ plane).
 */

extern CCL_INFO D_8C5C6D1C[];           /* shockwave collision shape (game data) */
extern NJS_OBJECT E_100_Zero_Shockwave; /* shockwave model (game data) */
extern void func_8C109EAC(Sint32 pno, Sint32 a, Sint32 b, Sint32 c); /* pad vibration */

ASM_STUB(void func_8C106B3E(task *tp))

void func_8C106BCA(task *tp)
{
    taskwk *twp;
    NJS_VECTOR v;

    twp = tp->twp;
    if (twp->mode == 0) {
        twp->scl.x = 1.0f;
        twp->scl.y = 5.65714276f;        /* 0x40B50750 = 198/35 (twice the growth step) */
        CCL_Init(tp, D_8C5C6D1C, 4, 1, 1);
        CWP(twp)->range = 105.0f;
        twp->mode = 1;
        tp->disp = func_8C106B3E;
    }
    if ((Uint32)++twp->counter.l >= 35) {
        FreeTask(tp);
        return;
    }
    twp->scl.x += 2.82857138f;       /* 0x40350750 = 99/35: 1.0 -> 100.0 in 35 frames */
    twp->scl.z = twp->scl.x;
    if (B_8C754EC8 == 0) {
        twp->ang.y += 0x1000;
        if (++twp->btimer & 1) {
            E_100_Zero_Shockwave.model->mats->attr_texId = 21;
        } else {
            E_100_Zero_Shockwave.model->mats->attr_texId = 22;
        }
        func_8C106B3E(tp);
    }
    v.x = playertwp[0]->pos.x - twp->pos.x;
    v.y = 0.0f;
    v.z = playertwp[0]->pos.z - twp->pos.z;
    if (njScalor(&v) == 0.0f) {
        v.x = 1.0f;
        v.z = 0.0f;
        v.y = 0.0f;
    } else {
        njUnitVector(&v);
    }
    v.x *= twp->scl.x;
    v.z *= twp->scl.x;
    v.x += twp->pos.x;
    v.y = twp->pos.y;
    v.z += twp->pos.z;
    CWP(twp)->info->center = v;
    func_8C02F63C(twp);
    func_8C109EAC(0, 1, 15, 0);
}
