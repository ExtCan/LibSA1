/*
 * 1ST_READ 8C07B320-8C07B500: player effect spawners.
 *
 *   func_8C07B320  spawn the 8C044680 task for the player of twp (copies the
 *                  player number into the new task); called by the mains
 *   func_8C07B346  random effect at a point under the player while it moves
 *                  (character shadow 8C0925A0; it also passes the surface
 *                  angles in R6, the 'ang' parameter this function never reads)
 *   func_8C07B3DA  burst of 20 random effects at the position of twp + SE 0x2CD
 *                  (character shadow 8C0925A0)
 */

#include "common.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"         /* func_8C07B346 */

void func_8C044680(task *tp);                                       /* task exec */
void func_8C046A08(NJS_POINT3 *pos, NJS_POINT3 *spd, Float scl);   /* effect spawner (stores pos, spd, scl * k) */
/* 8C046B60: effect spawner (CreateElementalTask(2, 6, ..), copies pos/spd/scl);
 * the current split labels it as data (D_8C046B60) */
void D_8C046B60(NJS_POINT3 *pos, NJS_POINT3 *spd, Float scl);
#define func_8C046B60 D_8C046B60

void func_8C07B320(taskwk *twp)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 6, func_8C044680)) != NULL) {
        tp->twp->counter.b[0] = twp->counter.b[0];
    }
}

#ifdef NON_MATCHING
/*
 * Instruction-identical except for one register-allocation tie in the last
 * block: the original keeps the 0.0f for v.y in FR1 and spd.y * 0.005f /
 * 0.03f in FR2; SHC here swaps them (FR2 / FR1). Tried: statement and
 * declaration orders, nested ifs, temporaries, double literals, a third
 * (unused) parameter.
 */
void func_8C07B346(Uint8 pno, NJS_POINT3 *pos, Angle *ang)
{
    NJS_POINT3 v;
    taskwk *twp;
    playerwk *pwp;
    Float x;

    twp = playertwp[pno];
    pwp = playerpwp[pno];
    if (pos->y > twp->pos.y + pwp->p.height) {
        return;
    }
    if (njScalor(&pwp->spd) == 0.0f) {
        return;
    }
    if ((Float)rand() * 3.0517579e-05f > 0.84999998f) {
        x = -(pwp->spd.y * 0.005f);
        v.x = x + 0.03f;
        v.y = 0.0f;
        v.z = 0.84999998f;
        func_8C046B60(pos, &v, pwp->p.rad * 0.099999996f);
    }
}
#else
ASM_STUB(void func_8C07B346(Uint8 pno, NJS_POINT3 *pos, Angle *ang))
#endif

void func_8C07B3DA(taskwk *twp)
{
    NJS_POINT3 v;
    Sint32 i;

    v.y = _builtin_fabsf(playerpwp[(Uint8)twp->counter.b[0]]->spd.y) * 0.5f;
    for (i = 0; i < 20; i++) {
        v.x = (0.5f - (Float)rand() * 3.0517579e-05f) * 2.0f;
        v.z = (0.5f - (Float)rand() * 3.0517579e-05f) * 2.0f;
        func_8C046A08(&twp->pos, &v, 0.19999999f);
    }
    func_8C0940DA(0x2CD, 0, 0, 0);
}
