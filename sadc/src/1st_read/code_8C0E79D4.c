/*
 * 1ST_READ 8C0E79D4-8C0E80A6 (Big the Cat fishing: rod / lure helpers;
 * lure task exec func_8C0E943C, created by func_8C0E6DA0). All six match.
 *
 *  func_8C0E79D4  rod tip position: player 0's centre (p.center_height up)
 *                 plus user0_pos - user0_vec * 16 (17 with equipment bit
 *                 0x800) and a small offset, both rotated by the player's
 *                 ang (y as 0x8000 - ang.y), plus player pos, added into
 *                 *pos. tp is unused (callers pass their task).
 *  func_8C0E7BA8  lure falling: spd.y -= 0.02, sweep test from twp->pos
 *                 along spd (func_8C0569E0, r = 0.5); TRUE on a hit, else
 *                 moves by spd and returns the colliwk flag bit 2.
 *  func_8C0E7C34  reel the lure towards the rod tip (step 10 in mode 7,
 *                 else 2); TRUE once within +-5 of the tip on every axis.
 *  func_8C0E7D5A  TRUE when the horizontal distance a-b is below 20 (in
 *                 stage 0x803 also b->y above D_8C565F6C - 10).
 *  func_8C0E7E00  lure idle in the water: damp spd.x/spd.z towards 0,
 *                 spd.y from the fishing bits B_8C7A40E8 (bit 0: random
 *                 bob, bit 4: 0.1; else sink by 0.005 with a limit), then
 *                 func_8C0945A0(800) (SE 800, the reel sound).
 *  func_8C0E7F60  while B_8C7A40C8 exists: D_8C565F74 -= 0.1, reel SE 800
 *                 every 0x8000 of the phase D_8C565F80; when D_8C565F74
 *                 drops to D_8C565F70 falls back to func_8C0E7E00; else pulls
 *                 the lure horizontally towards *p (speed 0.08 scaled by
 *                 5000 / (5000 + weight) of the hooked task B_8C7A40CC when
 *                 bit 0 is set), func_8C0E91E4 when close (func_8C0E7D5A),
 *                 and button D_8C19E38C sets bit 0x1000.
 *
 * Matching notes: the 1.8 offset is 0x3FE66665 (one ulp below 1.8f; probably
 * a folded 0.6f * 3), written 1.79999985f. The button test needs (Sint32)
 * casts on both operands (as noted in sa1/input.h); without them SHC uses
 * R0 instead of R1 for the B_8C7A40E8 |= 0x1000.
 */
#include "common.h"
#include "sa1/sa1.h"

/* fishing state (Big; also used by chunk 8C0DFFA0 BigMain) */
extern task *B_8C7A40C8;        /* fishing task; only tested here (BigMain passes it to func_8C0EAD6A) */
extern task *B_8C7A40CC;        /* hooked task: B_8C7A40CC->mwp->weight read at 8C0E7F96 */
extern Uint16 B_8C7A40E8;       /* fishing bits (MOV.W + EXTU.W): 0x1, 0x10, 0x800, 0x1000 */
extern Float D_8C565F6C;        /* height limit for stage 0x803 (func_8C0E7D5A) */
extern Float D_8C565F70;        /* lower limit of D_8C565F74 */
extern Float D_8C565F74;        /* -0.1 per frame while pulling */
extern Sint32 D_8C565F80;       /* reel sound phase (+0x71C per frame) */

extern Bool func_8C0569E0(NJS_POINT3 *pos, NJS_POINT3 *v, Angle3 *ang, Float r); /* code_8C05697C.c */
extern void func_8C0E91E4(task *tp);

void func_8C0E79D4(task *tp, NJS_POINT3 *pos)
{
    NJS_VECTOR v;
    NJS_VECTOR out;
    playerwk *pwp;
    taskwk *twp;
    Float s;

    pwp = playermwp[0]->work.pwp;
    twp = playertwp[0];
    njPushMatrix(D_8C67C640);
    v.x = v.z = 0.0f;
    v.y = pwp->p.center_height;
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    njCalcVector(NULL, &v, pos);
    if (pwp->equipment & 0x800) {
        s = 17.0f;
    } else {
        s = 16.0f;
    }
    v.x = pwp->user0_pos.x - s * pwp->user0_vec.x;
    v.y = pwp->user0_pos.y - s * pwp->user0_vec.y;
    v.z = pwp->user0_pos.z - s * pwp->user0_vec.z;
    if (pwp->equipment & 0x800) {
        v.y -= 1.79999985f;
        v.z += 0.59999998f;
    } else {
        v.y -= 0.59999998f;
    }
    njCalcVector(NULL, &v, &out);
    pos->x += twp->pos.x + out.x;
    pos->y += twp->pos.y + out.y;
    pos->z += twp->pos.z + out.z;
    njPopMatrix(1);
}

Bool func_8C0E7BA8(task *tp)
{
    NJS_POINT3 pos;
    Angle3 ang;
    motionwk *mwp;
    taskwk *twp;

    twp = tp->twp;
    mwp = tp->mwp;
    ang.x = ang.y = ang.z = 0;
    mwp->spd.y -= 0.02f;
    pos = twp->pos;
    if (func_8C0569E0(&pos, &mwp->spd, &ang, 0.5f)) {
        return TRUE;
    }
    twp->pos.x += mwp->spd.x;
    twp->pos.y += mwp->spd.y;
    twp->pos.z += mwp->spd.z;
    if (CWP(twp)->flag & 2) {
        return TRUE;
    }
    return FALSE;
}

Bool func_8C0E7C34(task *tp)
{
    NJS_POINT3 pos;
    taskwk *twp;
    motionwk *mwp;
    Float s;

    twp = tp->twp;
    mwp = tp->mwp;
    func_8C0E79D4(tp, &pos);
    mwp->spd.x = pos.x - twp->pos.x;
    mwp->spd.y = pos.y - twp->pos.y;
    mwp->spd.z = pos.z - twp->pos.z;
    njUnitVector(&mwp->spd);
    if (twp->mode == 7) {
        s = 10.0f;
    } else {
        s = 2.0f;
    }
    SA1_FMAC_ACC(twp->pos.x, s, mwp->spd.x);
    SA1_FMAC_ACC(twp->pos.y, s, mwp->spd.y);
    SA1_FMAC_ACC(twp->pos.z, s, mwp->spd.z);
    if (twp->pos.x + 5.0f > pos.x && pos.x > twp->pos.x + -5.0f
        && twp->pos.y + 5.0f > pos.y && pos.y > twp->pos.y + -5.0f
        && twp->pos.z + 5.0f > pos.z && pos.z > twp->pos.z + -5.0f) {
        return TRUE;
    }
    return FALSE;
}

Bool func_8C0E7D5A(NJS_POINT3 *a, NJS_POINT3 *b)
{
    NJS_VECTOR v;

    v.x = a->x - b->x;
    v.z = a->z - b->z;
    v.y = 0.0f;
    if (GetStageNumber() == 0x803) {
        if (njScalor(&v) < 20.0f && b->y > D_8C565F6C + -10.0f) {
            return TRUE;
        }
    } else if (njScalor(&v) < 20.0f) {
        return TRUE;
    }
    return FALSE;
}

void func_8C0E7E00(task *tp)
{
    motionwk *mwp;

    mwp = tp->mwp;
    if (mwp->spd.x > 0.0f) {
        mwp->spd.x -= 0.049999998f;
    }
    if (mwp->spd.x < 0.0f) {
        mwp->spd.x += 0.049999998f;
    }
    if (mwp->spd.x < 0.099999996f && mwp->spd.x > -0.099999996f) {
        mwp->spd.x = 0.0f;
    }
    if (mwp->spd.z > 0.0f) {
        mwp->spd.z -= 0.049999998f;
    }
    if (mwp->spd.z < 0.0f) {
        mwp->spd.z += 0.049999998f;
    }
    if (mwp->spd.z < 0.099999996f && mwp->spd.z > -0.099999996f) {
        mwp->spd.z = 0.0f;
    }
    if (B_8C7A40E8 & 1) {
        if (B_8C7A40E8 & 0x10) {
            mwp->spd.y = 0.099999996f;
        } else if ((Float)rand() * 0.000030517578125f < 0.79999997f) {
            mwp->spd.y = -0.049999998f;
        } else {
            mwp->spd.y = 0.049999998f;
        }
    } else {
        mwp->spd.y -= 0.005f;
        if (GetStageNumber() == 0x803) {
            if (mwp->spd.y < -0.099999996f) {
                mwp->spd.y = -0.099999996f;
            }
        } else if (mwp->spd.y < -0.049999998f) {
            mwp->spd.y = -0.049999998f;
        }
    }
    if (B_8C7A40E8 & 0x10) {
        mwp->spd.y = 0.0f;
    }
    func_8C0945A0(800);
}

void func_8C0E7F60(task *tp, NJS_POINT3 *p)
{
    taskwk *twp;
    motionwk *mwp;
    Float rate;
    Sint32 old;

    twp = tp->twp;
    mwp = tp->mwp;
    if (B_8C7A40C8 == NULL) {
        return;
    }
    if (B_8C7A40CC != NULL && (B_8C7A40E8 & 1)) {
        rate = 5000.0f / (B_8C7A40CC->mwp->weight + 5000.0f);
    } else {
        rate = 1.0f;
    }
    D_8C565F74 -= 0.099999996f;
    old = D_8C565F80;
    D_8C565F80 += 0x71C;
    if ((old ^ D_8C565F80) & 0x8000) {
        func_8C0945A0(800);
        func_8C0940DA(800, 0, 0, 0);
    }
    if (!(D_8C565F70 > D_8C565F74)) {
        func_8C0E7E00(tp);
        return;
    }
    mwp->spd.x = p->x - twp->pos.x;
    mwp->spd.z = p->z - twp->pos.z;
    if (func_8C0E7D5A(p, &twp->pos)) {
        func_8C0E91E4(tp);
    }
    njUnitVector(&mwp->spd);
    mwp->spd.x *= rate * 0.08f;
    mwp->spd.z *= rate * 0.08f;
    if (GetStageNumber() == 0x803) {
        mwp->spd.y = 0.06f;
    } else {
        mwp->spd.y = 0.02f;
    }
    twp->ang.z = tp->mwp->ang_aim.z + 0x2000;
    if ((Sint32)B_8C754C08[0].press & (Sint32)D_8C19E38C) {
        B_8C7A40E8 |= 0x1000;
    }
}
