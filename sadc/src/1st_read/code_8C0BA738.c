/*
 * 1ST_READ 8C0BA738-8C0BA7C4 (chunk 8C0B6120-8C0BA820, functions 5-6 of 6):
 * Tails' task destructor and collision centre update (same shape as Sonic's
 * func_8C0AE0E0 / func_8C0AE110, code_8C0AE0E0.c).
 *  func_8C0BA738  destructor (tp->dest, pool entry 8C0B6148 in TailsMain):
 *                 resets the tail morph state (func_8C0BDD30), frees the
 *                 player slot, stops the owned sounds, releases resource slot 2
 *  func_8C0BA75A  move collision shape 0 to the body centre (p.center_height
 *                 along the player's up axis) in world space; TailsMain calls
 *                 it with (twp, pwp) (8C0B7076)
 */
#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

extern void func_8C0BDD30(void);    /* Tails: reset tail spin angles and tail morph-node models (code_8C0BDC80.c) */

void func_8C0BA738(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    func_8C0BDD30();
    func_8C06C5CA((Uint8)twp->counter.b[0]);
    func_8C0945D0(twp);
    func_8C098FFC(2);
}

void func_8C0BA75A(taskwk *twp, playerwk *pwp)
{
    CCL_INFO *info;
    NJS_POINT3 v;

    info = CWP(twp)->info;
    v.x = v.z = 0.0f;
    v.y = pwp->p.center_height;
    PConvertVector_P2G(twp, &v);
    info->center = v;
    info->center.x += twp->pos.x;
    info->center.y += twp->pos.y;
    info->center.z += twp->pos.z;
}
