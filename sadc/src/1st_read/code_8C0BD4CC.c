/*
 * 1ST_READ 8C0BD4CC-8C0BD8B2 (chunk 8C0BAFC0-8C0BDC80, functions 23-27/29):
 * Tails helpers for carrying an object and for the flight start.
 *
 *  func_8C0BD4CC  (TailsMain) held object (pwp->htp): while the collision
 *                 class query (func_8C06C880) has bit 0x200 the held task is
 *                 placed at Tails' first CCL shape centre plus the hand
 *                 offset (righthand weld + 2.5 * hand vector for actions
 *                 84-87, else the middle of both hands) rotated by
 *                 twp->ang (z, x, 0x8000 - y). Otherwise, when Tails' shape
 *                 hit something that is not the held task, the object is
 *                 dropped (func_8C0BD440) and Tails returns to standing
 *                 (func_8C0B70E8 on the ground, else mode 10 / action 19).
 *  func_8C0BD6B8  (TailsMain) put Tails behind the held task: pos.x/z =
 *                 held pos - (cos, sin)(ang.y) * held mwp->rad (30 without
 *                 a motionwk).
 *  func_8C0BD702  (TailsMain) turn speed while pushing/rotating the held
 *                 object, from the frame number of the motion: 0 outside
 *                 frames 4..28 (tested first: the original places that
 *                 exit before the ramps), ramps 4..14 and 18..28 by
 *                 0x10000 / (rad * 10.47197) (0x100 without radius).
 *  func_8C0BD800  (TailsModeMachine) action button D_8C19E38C: mode 4,
 *                 action 15, flag |= 0x500, work.f = spd.x; returns 1.
 *  func_8C0BD83C  (TailsModeMachine) action button, not on a path: flight
 *                 start (mode 20, action 43, timer.b[2] |= 2, free.f[0..5]
 *                 cleared, tail objects' ang[0] reset, SE 0x2DE); returns 1.
 */
#include "sa1/sa1.h"

/* Tails: back to standing (twp->mode = 1, waittimer = 0; code_8C0B70E8.c) */
extern void func_8C0B70E8(taskwk *twp, playerwk *pwp);
/* Tails SE: func_8C0940DA(se, 0, 0, 0) unless twp->counter.b[0] == 1 (code_8C0BBA4E.c) */
extern void func_8C0BFCA2(taskwk *twp, Sint32 se);

/* flight tails (code_8C0BA820.c). Arrays: a plain struct extern makes SHC
 * fold sym+0x14 into the literal pool (the original loads the base). */
extern NJS_OBJECT D_8C2606A8[];
extern NJS_OBJECT D_8C260028[];

/* drop the held object (same chunk, BSR) */
ASM_STUB(void func_8C0BD440(taskwk *twp, playerwk *pwp))

#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): 218 of 218 instructions,
 * only 7 differ, all temporary registers of the hit test: the original
 * loads twp->cwp / hit_cwp / mytask / mytask->twp into R0, R3, R1, R0
 * (CMP/EQ R0,R12) and pwp->htp after the drop into R3; this gives R3, R1,
 * R0, R3 and R2. Stack frame (info parked at @0 and shared with the
 * rotation angle, out @4, v @16), control flow, the 3 FMACs, the
 * reqaction test and the rotations match. Same unresolved pattern as the
 * siblings 8C0DC7DE (code_8C0DC7DE.c) and 8C0B1DE0 (code_8C0B1C56.c).
 * Needed: htwp assigned before info (scheduling of the prologue loads),
 * nested positive ifs for the drop path (a combined && CSEs twp->cwp and
 * spills it), an || chain on pwp->mj.reqaction (a switch or a local gives
 * a jump table / extra copies). Tried without effect on the R0 chain:
 * early returns, mixed return/nesting, do/while(0) breaks, swapped compare
 * operands, a #pragma inline hit-task helper (R4/R5), locals for htp/cwp,
 * declaration orders, == 0 mask test, -size/-macsave=1/-extra variants and
 * SHC 5.1r01..r13.
 */
void func_8C0BD4CC(taskwk *twp, playerwk *pwp)
{
    NJS_VECTOR v;
    NJS_VECTOR out;
    CCL_INFO *info;
    taskwk *htwp;

    htwp = pwp->htp->twp;
    info = CWP(twp)->info;
    if (!(func_8C06C880(twp) & 0x200)) {
        if (CWP(twp)->hit_cwp != NULL) {
            if (htwp != CWP(twp)->hit_cwp->mytask->twp) {
                func_8C0BD440(twp, pwp);
                if (pwp->htp == NULL) {
                    if (twp->flag & 3) {
                        func_8C0B70E8(twp, pwp);
                    } else {
                        twp->mode = 10;
                        pwp->mj.reqaction = 19;
                    }
                }
            }
        }
        return;
    }
    htwp->pos = info->center;
    if (pwp->mj.reqaction == 84 || pwp->mj.reqaction == 85 || pwp->mj.reqaction == 86 || pwp->mj.reqaction == 87) {
        v.x = SA1_FMAC(2.5f, pwp->righthand_vec.x, pwp->righthand_pos.x);
        v.y = SA1_FMAC(2.5f, pwp->righthand_vec.y, pwp->righthand_pos.y);
        v.z = SA1_FMAC(2.5f, pwp->righthand_vec.z, pwp->righthand_pos.z);
    } else {
        v.x = (pwp->righthand_pos.x + pwp->lefthand_pos.x) * 0.5f;
        v.y = (pwp->righthand_pos.y + pwp->lefthand_pos.y) * 0.5f;
        v.z = (pwp->righthand_pos.z + pwp->lefthand_pos.z) * 0.5f;
    }
    v.x -= CWP(htwp)->info->center.x;
    v.y -= CWP(htwp)->info->center.y;
    v.z -= CWP(htwp)->info->center.z;
    njPushMatrix(D_8C67C640);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    njCalcVector(NULL, &v, &out);
    njPopMatrix(1);
    njAddVector(&htwp->pos, &out);
}
#else
ASM_STUB(void func_8C0BD4CC(taskwk *twp, playerwk *pwp))
#endif

void func_8C0BD6B8(taskwk *twp, playerwk *pwp)
{
    taskwk *htwp;
    motionwk *hmwp;
    Float dist;

    htwp = pwp->htp->twp;
    hmwp = pwp->htp->mwp;
    if (hmwp == NULL) {
        dist = 30.0f;
    } else {
        dist = hmwp->rad;
    }
    twp->pos.x = htwp->pos.x - njCos(twp->ang.y) * dist;
    twp->pos.z = htwp->pos.z - njSin(twp->ang.y) * dist;
}

Angle func_8C0BD702(playerwk *pwp)
{
    motionwk *hmwp;
    Float frame;
    Angle ang;

    hmwp = pwp->htp->mwp;
    frame = pwp->mj.nframe;
    if (hmwp == NULL || hmwp->rad == 0.0f) {
        ang = 0x100;
    } else {
        ang = SA1_FTOI(65536.0f / (hmwp->rad * 10.47197f));
    }
    if (frame >= 28.0f || frame <= 4.0f) {
        ang = 0;
    } else if (frame >= 18.0f) {
        ang = SA1_FTOI((28.0f - frame) * (Float)ang * 0.099999996f);
    } else if (frame <= 14.0f) {
        ang = SA1_FTOI((frame - 4.0f) * (Float)ang * 0.099999996f);
    }
    return ang;
}

Sint32 func_8C0BD800(taskwk *twp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        twp->mode = 4;
        pwp->mj.reqaction = 15;
        twp->flag |= 0x500;
        pwp->work.f = pwp->spd.x;
        return 1;
    }
    return 0;
}

Sint32 func_8C0BD83C(taskwk *twp, playerwk *pwp)
{
    NJS_OBJECT *obj;

    if ((B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) && !(twp->flag & 0x2000)) {
        twp->mode = 20;
        pwp->mj.reqaction = 43;
        twp->timer.b[2] |= 2;
        pwp->free.f[0] = 0.0f;
        pwp->free.f[1] = 0.0f;
        pwp->free.f[2] = 0.0f;
        pwp->free.f[3] = 0.0f;
        pwp->free.f[4] = 0.0f;
        pwp->free.f[5] = 0.0f;
        /* one pointer variable for both: the pool address is loaded into
         * the same register right before each store */
        obj = D_8C2606A8;
        obj->ang[0] = 0;
        obj = D_8C260028;
        obj->ang[0] = 0;
        func_8C0BFCA2(twp, 0x2DE);
        return 1;
    }
    return 0;
}
