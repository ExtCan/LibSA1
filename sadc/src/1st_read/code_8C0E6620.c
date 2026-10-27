/*
 * 1ST_READ 8C0E6620-8C0E66C4 (Big the Cat; caller BigMain 8C0DFFA0):
 *
 *  func_8C0E6620  creates a child task (CreateElementalTask(11, 2,
 *                 func_8C0E6CC0): twp + mwp + awp) at the creator's
 *                 position and angle, stores the creator's ang.y (low 16
 *                 bits) and a radius of 20 in the any work, and moves the
 *                 new task 20 units along its facing direction (cos/sin of
 *                 ang.y in x/z) and 20 units up. Returns the new task (or
 *                 NULL).
 */
#include "common.h"
#include "sa1/sa1.h"

/* Any work (task.awp, 0x10 bytes) of the task created here; also used by
 * its exec func_8C0E6CC0 (+0x04 Float, +0x0C/+0x0E Sint16). No pointers. */
typedef struct {
    Float rad;                  /* 0x00 distance from the creator (20.0f) */
    Float y;                    /* 0x04 written by func_8C0E6CC0 (pos.y) */
    Sint16 angy;                /* 0x08 creator's ang.y (MOV.W @(24)) */
    Sint16 pad0A;
    Sint16 w0C;                 /* 0x0C cleared by func_8C0E6CC0 */
    Sint16 w0E;                 /* 0x0E cleared by func_8C0E6CC0 */
} BIGCHILDWK;                   /* 0x10 */

extern void func_8C0E6CC0(task *tp);

task *func_8C0E6620(task *tp)
{
    task *ntp;

    ntp = CreateElementalTask(11, 2, func_8C0E6CC0);
    if (ntp != NULL) {
        ntp->twp->pos = tp->twp->pos;
        ntp->twp->ang = tp->twp->ang;
        ((BIGCHILDWK *)ntp->awp)->angy = (Sint16)tp->twp->ang.y;
        ((BIGCHILDWK *)ntp->awp)->rad = 20.0f;
        SA1_FMAC_ACC(ntp->twp->pos.x, njCos(ntp->twp->ang.y), ((BIGCHILDWK *)ntp->awp)->rad);
        ntp->twp->pos.y += 20.0f;
        SA1_FMAC_ACC(ntp->twp->pos.z, njSin(ntp->twp->ang.y), ((BIGCHILDWK *)ntp->awp)->rad);
    }
    return ntp;
}
