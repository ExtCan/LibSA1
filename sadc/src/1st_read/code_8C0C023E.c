/*
 * 1ST_READ 8C0C023E-8C0C05F0 (chunk 8C0BFE00, function 2 of 2): Tails.
 *  func_8C0C023E  exec of the upper tail trail quad, created (with
 *                 func_8C0BFE00 for the lower one) by func_8C0BEDA0 with
 *                 pos = last tail tip, scl = new tip, counter/timer/value =
 *                 last centre and awp work[0..2] = new centre.
 *                 Frame 0 (wtimer == 0): the new centre moves 70% towards the
 *                 new tip and the last centre 70% towards the last tip.
 *                 Later frames: both tips move away from their centres by 20%.
 *                 After 12 frames the task frees itself; until then it draws
 *                 the quad D_8C257AF0 = {B_8C7A2310, B_8C7A2340, NULL, 4}
 *                 (last tip, new tip, new centre, last centre) with additive
 *                 blending (njColorBlendingMode 10/10), colours faded from
 *                 D_8C257AE0 (tips c0 -> c1, centres c2 -> c3) by
 *                 t = wtimer / 12 and darkened by 1 - t, then calls
 *                 func_8C0919CC(tp).
 *  Matching notes: the free test is "(Float)++wtimer >= 11.999999" with the
 *  FreeTask tail call first; the colour FMACs take the source colour in FR0
 *  (SA1_FMAC(s, c0, t * c1)); the else-branch FMACs are compound
 *  assignments (FMAC_ACC below).
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

extern NJS_COLOR D_8C257AE0[4];         /* source colours (0x008080FF, 0x000000FF, 0, 0) */
extern NJS_POINT3COL D_8C257AF0;        /* {B_8C7A2310, B_8C7A2340, NULL, 4} */
extern NJS_POINT3 B_8C7A2310[4];        /* quad vertices */
extern NJS_COLOR B_8C7A2340[4];         /* quad vertex colours */

/*
 * x += a * b, fused like SA1_FMAC on PC. The original evaluates the
 * compound assignment (the destination is loaded twice: once for the
 * difference and once as the addend); "x = SA1_FMAC(a, b, x)" makes SHC load
 * it once, so the six FMACs of the else branch are written with this.
 */
#if defined(TARGET_PC)
#define FMAC_ACC(x, a, b) ((x) = SA1_FMAC((a), (b), (x)))
#else
#define FMAC_ACC(x, a, b) ((x) += (a) * (b))
#endif

void func_8C0C023E(task *tp)
{
    taskwk *twp;
    Float t0;
    Float t1;
    Float s0;
    Float s1;

    twp = tp->twp;
    if (twp->wtimer == 0) {
        AWP(tp)->work[0].f = SA1_FMAC(twp->scl.x - AWP(tp)->work[0].f, 0.69999999f, AWP(tp)->work[0].f);
        AWP(tp)->work[1].f = SA1_FMAC(twp->scl.y - AWP(tp)->work[1].f, 0.69999999f, AWP(tp)->work[1].f);
        AWP(tp)->work[2].f = SA1_FMAC(twp->scl.z - AWP(tp)->work[2].f, 0.69999999f, AWP(tp)->work[2].f);
        twp->counter.f = SA1_FMAC(twp->pos.x - twp->counter.f, 0.69999999f, twp->counter.f);
        twp->timer.f = SA1_FMAC(twp->pos.y - twp->timer.f, 0.69999999f, twp->timer.f);
        twp->value.f = SA1_FMAC(twp->pos.z - twp->value.f, 0.69999999f, twp->value.f);
    } else {
        FMAC_ACC(twp->scl.x, twp->scl.x - AWP(tp)->work[0].f, 0.19999999f);
        FMAC_ACC(twp->scl.y, twp->scl.y - AWP(tp)->work[1].f, 0.19999999f);
        FMAC_ACC(twp->scl.z, twp->scl.z - AWP(tp)->work[2].f, 0.19999999f);
        FMAC_ACC(twp->pos.x, twp->pos.x - twp->counter.f, 0.19999999f);
        FMAC_ACC(twp->pos.y, twp->pos.y - twp->timer.f, 0.19999999f);
        FMAC_ACC(twp->pos.z, twp->pos.z - twp->value.f, 0.19999999f);
    }
    if ((Float)++twp->wtimer >= 11.9999992f) {
        FreeTask(tp);
        return;
    }
    t0 = (Float)twp->wtimer * 0.083333336f;
    t1 = (Float)(twp->wtimer + 1) * 0.083333336f;
    s0 = 1.0f - t0;
    s1 = 1.0f - t1;
    B_8C7A2340[0].argb.r = SA1_FTOI(s1 * SA1_FMAC(s1, (Float)D_8C257AE0[0].argb.r, t1 * D_8C257AE0[1].argb.r));
    B_8C7A2340[0].argb.g = SA1_FTOI(s1 * SA1_FMAC(s1, (Float)D_8C257AE0[0].argb.g, t1 * D_8C257AE0[1].argb.g));
    B_8C7A2340[0].argb.b = SA1_FTOI(s1 * SA1_FMAC(s1, (Float)D_8C257AE0[0].argb.b, t1 * D_8C257AE0[1].argb.b));
    B_8C7A2340[1].argb.r = SA1_FTOI(s0 * SA1_FMAC(s0, (Float)D_8C257AE0[0].argb.r, t0 * D_8C257AE0[1].argb.r));
    B_8C7A2340[1].argb.g = SA1_FTOI(s0 * SA1_FMAC(s0, (Float)D_8C257AE0[0].argb.g, t0 * D_8C257AE0[1].argb.g));
    B_8C7A2340[1].argb.b = SA1_FTOI(s0 * SA1_FMAC(s0, (Float)D_8C257AE0[0].argb.b, t0 * D_8C257AE0[1].argb.b));
    B_8C7A2340[3].argb.r = SA1_FTOI(s1 * SA1_FMAC(s1, (Float)D_8C257AE0[2].argb.r, t1 * D_8C257AE0[3].argb.r));
    B_8C7A2340[3].argb.g = SA1_FTOI(s1 * SA1_FMAC(s1, (Float)D_8C257AE0[2].argb.g, t1 * D_8C257AE0[3].argb.g));
    B_8C7A2340[3].argb.b = SA1_FTOI(s1 * SA1_FMAC(s1, (Float)D_8C257AE0[2].argb.b, t1 * D_8C257AE0[3].argb.b));
    B_8C7A2340[2].argb.r = SA1_FTOI(s0 * SA1_FMAC(s0, (Float)D_8C257AE0[2].argb.r, t0 * D_8C257AE0[3].argb.r));
    B_8C7A2340[2].argb.g = SA1_FTOI(s0 * SA1_FMAC(s0, (Float)D_8C257AE0[2].argb.g, t0 * D_8C257AE0[3].argb.g));
    B_8C7A2340[2].argb.b = SA1_FTOI(s0 * SA1_FMAC(s0, (Float)D_8C257AE0[2].argb.b, t0 * D_8C257AE0[3].argb.b));
    B_8C7A2310[0] = twp->pos;
    B_8C7A2310[1] = twp->scl;
    B_8C7A2310[2].x = AWP(tp)->work[0].f;
    B_8C7A2310[2].y = AWP(tp)->work[1].f;
    B_8C7A2310[2].z = AWP(tp)->work[2].f;
    B_8C7A2310[3].x = twp->counter.f;
    B_8C7A2310[3].y = twp->timer.f;
    B_8C7A2310[3].z = twp->value.f;
    njColorBlendingMode(0, 10);
    njColorBlendingMode(1, 10);
    func_8C63D414(&D_8C257AF0, 4, 0x60);
    njColorBlendingMode(0, 1);
    njColorBlendingMode(1, 1);
    func_8C0919CC(tp);
}
