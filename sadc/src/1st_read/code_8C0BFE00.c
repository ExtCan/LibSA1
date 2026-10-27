/*
 * 1ST_READ 8C0BFE00-8C0C023E (chunk 8C0BFE00, function 1 of 2): Tails.
 *  func_8C0BFE00  exec of a tail trail segment created by func_8C0BEDA0
 *                 (CreateElementalTask(10, 6, ...)): pos/scl are the last
 *                 and the new tail tip, counter/timer/value and
 *                 awp->work[0..2] the body centre of the last and of this
 *                 frame. Frame 0 pulls awp and counter/timer/value 0.4 of
 *                 the way towards scl and pos and creates the collision
 *                 (D_8C257B00, CCL id 4); later frames push scl and pos
 *                 outwards by 1% and, on frames 9..11, move the collision
 *                 shape to scl + (scl - awp) * 0.6. The segment lives 18
 *                 frames and draws itself as a 4-vertex polygon
 *                 (D_8C257AD0 = { B_8C7A22D0, B_8C7A2300, NULL, 4 }) with
 *                 colours lerped along D_8C257AC0 and faded by 1 - t.
 *
 * Matching notes:
 *  - the "FreeTask(tp); return;" form (not if/else) is what gives the
 *    colour block FR2 instead of FR1 for its temporaries;
 *  - the frame > 0 updates are "+=" on SHC (the original loads the
 *    accumulator a second time; the a * b + c spelling does not) and
 *    SA1_FMAC on the PC (SH_ONLY/PC_ONLY, as in code_8C0D7960.c);
 *  - the 8.999999f/11.999998f/17.999998f/0.05555556f constants are the
 *    original bits (0x410FFFFF, 0x413FFFFE, 0x418FFFFF, 0x3D638E3A),
 *    written in the portable literal window.
 * PC notes: the colour bytes stay within 0..255 (t <= 1.0000001f, so f can
 *    be -1.2e-7 and the product truncates to 0); SA1_FTOI keeps FTRC
 *    semantics anyway.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

extern NJS_COLOR D_8C257AC0[4];         /* trail colours: [0]/[1] outer edge, [2]/[3] inner edge */
extern NJS_POINT3COL D_8C257AD0;        /* { B_8C7A22D0, B_8C7A2300, NULL, 4 } */
extern CCL_INFO D_8C257B00[];           /* one shape */
extern NJS_POINT3 B_8C7A22D0[4];        /* polygon vertices */
extern NJS_COLOR B_8C7A2300[4];         /* polygon colours */

void func_8C0BFE00(task *tp)
{
    taskwk *twp;
    NJS_POINT3 v;
    Float t0;
    Float t1;
    Float f0;
    Float f1;

    twp = tp->twp;
    if (twp->wtimer == 0) {
        AWP(tp)->work[0].f = SA1_FMAC(0.39999998f, twp->scl.x - AWP(tp)->work[0].f, AWP(tp)->work[0].f);
        AWP(tp)->work[1].f = SA1_FMAC(0.39999998f, twp->scl.y - AWP(tp)->work[1].f, AWP(tp)->work[1].f);
        AWP(tp)->work[2].f = SA1_FMAC(0.39999998f, twp->scl.z - AWP(tp)->work[2].f, AWP(tp)->work[2].f);
        twp->counter.f = SA1_FMAC(0.39999998f, twp->pos.x - twp->counter.f, twp->counter.f);
        twp->timer.f = SA1_FMAC(0.39999998f, twp->pos.y - twp->timer.f, twp->timer.f);
        twp->value.f = SA1_FMAC(0.39999998f, twp->pos.z - twp->value.f, twp->value.f);
        CCL_Init(tp, D_8C257B00, 4, 1, 1);
    } else {
        twp->scl.x SH_ONLY(+= (twp->scl.x - AWP(tp)->work[0].f) * 0.01f)
            PC_ONLY(= SA1_FMAC(twp->scl.x - AWP(tp)->work[0].f, 0.01f, twp->scl.x));
        twp->scl.y SH_ONLY(+= (twp->scl.y - AWP(tp)->work[1].f) * 0.01f)
            PC_ONLY(= SA1_FMAC(twp->scl.y - AWP(tp)->work[1].f, 0.01f, twp->scl.y));
        twp->scl.z SH_ONLY(+= (twp->scl.z - AWP(tp)->work[2].f) * 0.01f)
            PC_ONLY(= SA1_FMAC(twp->scl.z - AWP(tp)->work[2].f, 0.01f, twp->scl.z));
        twp->pos.x SH_ONLY(+= (twp->pos.x - twp->counter.f) * 0.01f)
            PC_ONLY(= SA1_FMAC(twp->pos.x - twp->counter.f, 0.01f, twp->pos.x));
        twp->pos.y SH_ONLY(+= (twp->pos.y - twp->timer.f) * 0.01f)
            PC_ONLY(= SA1_FMAC(twp->pos.y - twp->timer.f, 0.01f, twp->pos.y));
        twp->pos.z SH_ONLY(+= (twp->pos.z - twp->value.f) * 0.01f)
            PC_ONLY(= SA1_FMAC(twp->pos.z - twp->value.f, 0.01f, twp->pos.z));
        if (twp->wtimer >= 8.9999992f && twp->wtimer < 11.9999982f) {
            v.x = SA1_FMAC(0.59999998f, twp->scl.x - AWP(tp)->work[0].f, twp->scl.x);
            v.y = SA1_FMAC(0.59999998f, twp->scl.y - AWP(tp)->work[1].f, twp->scl.y);
            v.z = SA1_FMAC(0.59999998f, twp->scl.z - AWP(tp)->work[2].f, twp->scl.z);
            CWP(twp)->info->center = v;
            func_8C02F63C(twp);
        }
    }
    if (++twp->wtimer >= 17.9999985f) {
        FreeTask(tp);
        return;
    }
    t0 = twp->wtimer * 0.05555556f;
    t1 = (twp->wtimer + 1) * 0.05555556f;
    f0 = 1.0f - t0;
    f1 = 1.0f - t1;
    B_8C7A2300[0].argb.r = SA1_FTOI(f1 * SA1_FMAC(f1, D_8C257AC0[0].argb.r, t1 * D_8C257AC0[1].argb.r));
    B_8C7A2300[0].argb.g = SA1_FTOI(f1 * SA1_FMAC(f1, D_8C257AC0[0].argb.g, t1 * D_8C257AC0[1].argb.g));
    B_8C7A2300[0].argb.b = SA1_FTOI(f1 * SA1_FMAC(f1, D_8C257AC0[0].argb.b, t1 * D_8C257AC0[1].argb.b));
    B_8C7A2300[1].argb.r = SA1_FTOI(f0 * SA1_FMAC(f0, D_8C257AC0[0].argb.r, t0 * D_8C257AC0[1].argb.r));
    B_8C7A2300[1].argb.g = SA1_FTOI(f0 * SA1_FMAC(f0, D_8C257AC0[0].argb.g, t0 * D_8C257AC0[1].argb.g));
    B_8C7A2300[1].argb.b = SA1_FTOI(f0 * SA1_FMAC(f0, D_8C257AC0[0].argb.b, t0 * D_8C257AC0[1].argb.b));
    B_8C7A2300[3].argb.r = SA1_FTOI(f1 * SA1_FMAC(f1, D_8C257AC0[2].argb.r, t1 * D_8C257AC0[3].argb.r));
    B_8C7A2300[3].argb.g = SA1_FTOI(f1 * SA1_FMAC(f1, D_8C257AC0[2].argb.g, t1 * D_8C257AC0[3].argb.g));
    B_8C7A2300[3].argb.b = SA1_FTOI(f1 * SA1_FMAC(f1, D_8C257AC0[2].argb.b, t1 * D_8C257AC0[3].argb.b));
    B_8C7A2300[2].argb.r = SA1_FTOI(f0 * SA1_FMAC(f0, D_8C257AC0[2].argb.r, t0 * D_8C257AC0[3].argb.r));
    B_8C7A2300[2].argb.g = SA1_FTOI(f0 * SA1_FMAC(f0, D_8C257AC0[2].argb.g, t0 * D_8C257AC0[3].argb.g));
    B_8C7A2300[2].argb.b = SA1_FTOI(f0 * SA1_FMAC(f0, D_8C257AC0[2].argb.b, t0 * D_8C257AC0[3].argb.b));
    B_8C7A22D0[0] = twp->pos;
    B_8C7A22D0[1] = twp->scl;
    B_8C7A22D0[2].x = AWP(tp)->work[0].f;
    B_8C7A22D0[2].y = AWP(tp)->work[1].f;
    B_8C7A22D0[2].z = AWP(tp)->work[2].f;
    B_8C7A22D0[3].x = twp->counter.f;
    B_8C7A22D0[3].y = twp->timer.f;
    B_8C7A22D0[3].z = twp->value.f;
    njColorBlendingMode(0, 10);
    njColorBlendingMode(1, 10);
    func_8C63D414(&D_8C257AD0, 4, 0x60);
    njColorBlendingMode(0, 1);
    njColorBlendingMode(1, 1);
    func_8C0919CC(tp);
}
