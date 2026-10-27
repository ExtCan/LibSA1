/*
 * 1ST_READ 8C0BEDA0-8C0BF262 (chunk 8C0BEDA0, function 1 of 18): Tails.
 *  func_8C0BEDA0  tail trail effect, called by TailsModeMachine 8C0B70F4
 *                 with (twp, mwp, pwp). Builds the player matrix (center of
 *                 the first collision shape, rotations Z, X, 0x8000 - Y),
 *                 evaluates the current action for nodes 66 and 62 (the two
 *                 tails) and gets the tail tip points (7, 0, 1) of both.
 *                 When last frame's tips (pwp->free.f[0..2], f[3..5]) are
 *                 all set, each tail spawns two trail tasks (CreateElementalTask
 *                 (10, 6, ...): exec 8C0BFE00 and 8C0C023E, the second 0.5
 *                 higher) with pos = last tip, scl = new tip, counter/timer/
 *                 value = center - mwp->spd (center of last frame), awp
 *                 work[0..2] = center. Then stores the new tips.
 *
 * Matching notes: the matrices are passed as (NJS_MATRIX_PTR)&B_x (address
 * of the whole array): plain B_x is CSE'd into a callee-saved register, the
 * original reloads every use from the pool. The rotations are the
 * "c ? f(m, c) : (void)0" form (CSE temporary in the stack slot @R15, temps
 * R2/R3/R3 as in the original). The trail spawn is skipped with "|| ...
 * goto store": an "if (... && ...) { }" block gives R2 instead of R3 for
 * the &pwp->free temporary at the start of the block.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

extern NJS_MATRIX B_8C7A2190;       /* player base matrix */
extern NJS_MATRIX B_8C7A21D0;       /* node matrix out of func_8C0583A0 */
extern NJS_MATRIX B_8C7A2210;       /* node 66 (tail 1) */
extern NJS_MATRIX B_8C7A2250;       /* node 62 (tail 2) */

void func_8C0BFE00(task *tp);       /* trail task exec */
void func_8C0C023E(task *tp);       /* trail task exec (upper) */

void func_8C0BEDA0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 p1;
    NJS_POINT3 p2;
    NJS_POINT3 v;
    NJS_POINT3 pos;
    task *tp;
    Float ofs;

    njUnitMatrix((NJS_MATRIX_PTR)&B_8C7A2190);
    njTranslateV((NJS_MATRIX_PTR)&B_8C7A2190, &CWP(twp)->info->center);
    twp->ang.z ? njRotateZ((NJS_MATRIX_PTR)&B_8C7A2190, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX((NJS_MATRIX_PTR)&B_8C7A2190, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY((NJS_MATRIX_PTR)&B_8C7A2190, 0x8000 - twp->ang.y) : (void)0;
    if (!func_8C0583A0((NJS_MATRIX_PTR)&B_8C7A2190, TailsActionList[pwp->mj.reqaction].actptr, pwp->mj.nframe, 66, (NJS_MATRIX_PTR)&B_8C7A21D0)) {
        return;
    }
    njSetMatrix((NJS_MATRIX_PTR)&B_8C7A2210, (NJS_MATRIX_PTR)&B_8C7A21D0);
    if (!func_8C058434(62, (NJS_MATRIX_PTR)&B_8C7A21D0)) {
        return;
    }
    njSetMatrix((NJS_MATRIX_PTR)&B_8C7A2250, (NJS_MATRIX_PTR)&B_8C7A21D0);
    v.x = 7.0f;
    v.y = 0.0f;
    v.z = 1.0f;
    njCalcPoint((NJS_MATRIX_PTR)&B_8C7A2210, &v, &p1);
    njCalcPoint((NJS_MATRIX_PTR)&B_8C7A2250, &v, &p2);
    if (pwp->free.f[0] == 0.0f || pwp->free.f[1] == 0.0f || pwp->free.f[2] == 0.0f
        || pwp->free.f[3] == 0.0f || pwp->free.f[4] == 0.0f || pwp->free.f[5] == 0.0f) {
        goto store;
    }
    pos.x = pwp->free.f[0];
    pos.y = pwp->free.f[1];
    pos.z = pwp->free.f[2];
    if ((tp = CreateElementalTask(10, 6, func_8C0BFE00)) != NULL) {
        tp->twp->pos.x = pos.x;
        tp->twp->pos.y = pos.y;
        tp->twp->pos.z = pos.z;
        tp->twp->scl.x = p1.x;
        tp->twp->scl.y = p1.y;
        tp->twp->scl.z = p1.z;
        tp->twp->counter.f = CWP(twp)->info->center.x - mwp->spd.x;
        tp->twp->timer.f = CWP(twp)->info->center.y - mwp->spd.y;
        tp->twp->value.f = CWP(twp)->info->center.z - mwp->spd.z;
        AWP(tp)->work[0].f = CWP(twp)->info->center.x;
        AWP(tp)->work[1].f = CWP(twp)->info->center.y;
        AWP(tp)->work[2].f = CWP(twp)->info->center.z;
    }
    ofs = 0.5f;
    if ((tp = CreateElementalTask(10, 6, func_8C0C023E)) != NULL) {
        tp->twp->pos.x = pos.x;
        tp->twp->pos.y = pos.y + ofs;
        tp->twp->pos.z = pos.z;
        tp->twp->scl.x = p1.x;
        tp->twp->scl.y = p1.y + ofs;
        tp->twp->scl.z = p1.z;
        tp->twp->counter.f = CWP(twp)->info->center.x - mwp->spd.x;
        tp->twp->timer.f = CWP(twp)->info->center.y - mwp->spd.y + ofs;
        tp->twp->value.f = CWP(twp)->info->center.z - mwp->spd.z;
        AWP(tp)->work[0].f = CWP(twp)->info->center.x;
        AWP(tp)->work[1].f = CWP(twp)->info->center.y + ofs;
        AWP(tp)->work[2].f = CWP(twp)->info->center.z;
    }
    pos.x = pwp->free.f[3];
    pos.y = pwp->free.f[4];
    pos.z = pwp->free.f[5];
    if ((tp = CreateElementalTask(10, 6, func_8C0BFE00)) != NULL) {
        tp->twp->pos.x = pos.x;
        tp->twp->pos.y = pos.y;
        tp->twp->pos.z = pos.z;
        tp->twp->scl.x = p2.x;
        tp->twp->scl.y = p2.y;
        tp->twp->scl.z = p2.z;
        tp->twp->counter.f = CWP(twp)->info->center.x - mwp->spd.x;
        tp->twp->timer.f = CWP(twp)->info->center.y - mwp->spd.y;
        tp->twp->value.f = CWP(twp)->info->center.z - mwp->spd.z;
        AWP(tp)->work[0].f = CWP(twp)->info->center.x;
        AWP(tp)->work[1].f = CWP(twp)->info->center.y;
        AWP(tp)->work[2].f = CWP(twp)->info->center.z;
    }
    ofs = 0.5f;
    if ((tp = CreateElementalTask(10, 6, func_8C0C023E)) != NULL) {
        tp->twp->pos.x = pos.x;
        tp->twp->pos.y = pos.y + ofs;
        tp->twp->pos.z = pos.z;
        tp->twp->scl.x = p2.x;
        tp->twp->scl.y = p2.y + ofs;
        tp->twp->scl.z = p2.z;
        tp->twp->counter.f = CWP(twp)->info->center.x - mwp->spd.x;
        tp->twp->timer.f = CWP(twp)->info->center.y - mwp->spd.y + ofs;
        tp->twp->value.f = CWP(twp)->info->center.z - mwp->spd.z;
        AWP(tp)->work[0].f = CWP(twp)->info->center.x;
        AWP(tp)->work[1].f = CWP(twp)->info->center.y + ofs;
        AWP(tp)->work[2].f = CWP(twp)->info->center.z;
    }
store:
    pwp->free.f[0] = p1.x;
    pwp->free.f[1] = p1.y;
    pwp->free.f[2] = p1.z;
    pwp->free.f[3] = p2.x;
    pwp->free.f[4] = p2.y;
    pwp->free.f[5] = p2.z;
}
