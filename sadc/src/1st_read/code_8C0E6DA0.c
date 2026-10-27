/*
 * 1ST_READ 8C0E6DA0-8C0E6F04 (Big the Cat, fishing):
 *
 *  func_8C0E6DA0  creates the lure task (CreateElementalTask(11, 2,
 *                 func_8C0E943C): twp + mwp + awp; caller BigMain 8C0DFFA0
 *                 stores it in B_8C7A40C8 for player 0). The any work gets a
 *                 0x1B4-byte block (work[2], stored before the NULL check,
 *                 as in the original) and a 5 x 4-byte block (work[3]); the
 *                 new task starts at the creator's position and angle. Clears
 *                 the fishing flags B_8C7A40E8 if a lure task already exists
 *                 and D_8C565F80. Returns the new task.
 *  func_8C0E6E80  counts the set event flags 0x1DE..0x1E1 (Big's fishing
 *                 upgrades), stores the count in B_8C78C5D8 and returns it.
 *  func_8C0E6ED4  lure task end (called by func_8C0E943C): plays SE 800,
 *                 frees the work[3] block (not the work[2] one) and clears
 *                 B_8C7A40C8.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

extern task *B_8C7A40C8;        /* lure task (func_8C0E6DA0's result) */
extern Sint16 B_8C7A40E8;       /* Big fishing state bits (MOV.W store) */
extern Sint32 D_8C565F80;
extern Sint32 B_8C78C5D8;       /* number of fishing upgrades (func_8C0E6E80) */

void func_8C0E943C(task *tp);

task *func_8C0E6DA0(task *tp)
{
    task *ntp;
    void *p;

    ntp = CreateElementalTask(11, 2, func_8C0E943C);
    p = CAlloc(1, 0x1B4);
    AWP(ntp)->work[2].ptr = p;
    p = CAlloc(5, 4);
    if (ntp != NULL && p != NULL) {
        ntp->twp->pos = tp->twp->pos;
        ntp->twp->ang = tp->twp->ang;
        AWP(ntp)->work[3].ptr = p;
        if (B_8C7A40C8 != NULL) {
            B_8C7A40E8 = 0;
        }
        D_8C565F80 = 0;
    }
    return ntp;
}

Sint32 func_8C0E6E80(void)
{
    Sint32 n;

    n = 0;
    if (func_8C09068E(0x1DE)) {
        n++;
    }
    if (func_8C09068E(0x1DF)) {
        n++;
    }
    if (func_8C09068E(0x1E0)) {
        n++;
    }
    if (func_8C09068E(0x1E1)) {
        n++;
    }
    B_8C78C5D8 = n;
    return n;
}

void func_8C0E6ED4(task *tp)
{
    anywk *awp;
    void *p;

    awp = AWP(tp);
    func_8C0945A0(800);
    p = awp->work[3].ptr;
    if (p != NULL) {
        MFree(p);
    }
    B_8C7A40C8 = NULL;
}
