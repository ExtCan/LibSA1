#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0C450A-8C0C46AC (chunk 8C0C0640-8C0C5220, functions 3-4 of 9):
 * Knuckles' Maximum Heat charge and his task destructor.
 *  func_8C0C450A  called by KnucklesMain with (twp, pwp). With the upgrade
 *                 bit pwp->equipment & 0x20, holding the D_8C19E38C button
 *                 on the ground in modes 1, 2 or 10 counts pwp->free.w[5]
 *                 up to 120 (charge SE 0x2ED while charging; at 120 the
 *                 flash effect func_8C0CB960 and SE 21) and damps
 *                 pwp->spd.x towards 0.5. Releasing it at full charge
 *                 starts the attack: camera 0x44 (stages below 0x0F00),
 *                 effect func_8C0CB9A4, trail reset, twp->timer.w[1] = 180,
 *                 twp->mode = 18, action 35. Releasing always resets the
 *                 counter to -1, stops twp's sounds and (unless mode 19)
 *                 plays SE 0x2EE. Without the upgrade the counter is -1.
 *  func_8C0C4690  destructor (tp->dest, set by KnucklesMain 8C0C0830).
 */

/* Knuckles effect launchers (code_8C0CB960.c) */
extern void func_8C0CB960(void);
extern void func_8C0CB9A4(void);
extern void func_8C0CB9E8(void);
/* camera mode request (camera module 8C019360-8C025960) */
extern void func_8C01ABDC(Sint32 mode, Sint32 a);
/* SE owned by a task (sound module 8C092FA0; returns -1 when no entry) */
extern Sint32 func_8C0941FC(Sint32 se, taskwk *twp, Sint32 a, Sint32 b);

void func_8C0C450A(taskwk *twp, playerwk *pwp)
{
    Sint16 old;

    if (pwp == NULL) {
        return;
    }
    if (!(pwp->equipment & 0x20)) {
        pwp->free.w[5] = -1;
        return;
    }
    if (twp->mode == 26 || twp->mode == 18 || pwp->free.w[5] < 0) {
        return;
    }
    if ((B_8C754C08[(Uint8)twp->counter.b[0]].on & D_8C19E38C) && (twp->flag & 3)
        && (twp->mode == 1 || twp->mode == 2 || twp->mode == 10)) {
        old = pwp->free.w[5];
        if (++pwp->free.w[5] >= 120) {
            pwp->free.w[5] = 120;
            if (old != 120) {
                func_8C0CB960();
                func_8C0945D0(twp);
                func_8C0940DA(21, 0, 0, 0);
            }
        } else {
            func_8C0941FC(0x2ED, twp, 1, 0);
        }
        if (pwp->spd.x > 0.5f) {
            /* FMAC FR0,FR3,FR2 (8C0C45E6): spd.x += (0.5 - spd.x) * 0.5, fused;
             * SA1_FMAC(a, b, spd.x) would load spd.x once and break matching */
            SA1_FMAC_ACC(pwp->spd.x, 0.5f - pwp->spd.x, 0.5f);
        }
    } else {
        if (pwp->free.w[5] == 120) {
            if (GetStageNumber() < 0xF00) {
                func_8C01ABDC(0x44, 0);
            }
            func_8C0CB9A4();
            func_8C0CB9E8();
            twp->timer.w[1] = 180;
            twp->mode = 18;
            pwp->mj.reqaction = 35;
        }
        pwp->free.w[5] = -1;
        func_8C0945D0(twp);
        if (twp->mode != 19) {
            func_8C0940DA(0x2EE, 0, 0, 0);
        }
    }
}

void func_8C0C4690(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    func_8C06C5CA((Uint8)twp->counter.b[0]);
    func_8C098FFC(3);
}
