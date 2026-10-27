/*
 * 1ST_READ 8C0BD8B2-8C0BDC22 (chunk 8C0BAFC0-8C0BDC80, functions 28-29/29):
 * Tails flight.
 *
 *  func_8C0BD8B2  flight physics of TailsMain mode 15 (before PGetSpeed):
 *                 ground probe under the player (func_8C02AD22) to brake the
 *                 fall near the ground, then jump held -> climb while the
 *                 flight power pwp->free.f[7] rises to 1.0 (exhausted: fall
 *                 action, mode 19 / action 42), action held -> descend
 *                 faster; then the air acceleration func_8C06F6C0 and the
 *                 tail spin counter twp->timer.b[0] (0..63).
 *                 twp->timer.b[1] is the climb state: 1 climbing, -1
 *                 descending, 0 otherwise.
 *  func_8C0BDB72  spins and stretches the two flight tail models
 *                 (D_8C2606A8 / D_8C260028, ang[0] and scl) from the climb
 *                 state; called by the Tails display 8C0BA820.
 *
 * Literal pool layout. The original translation unit is the whole chunk
 * 8C0BAFC0-8C0BDC80. In it the pool entries still pending from the functions
 * before (func_8C0BD83C's D_8C19E38C .. func_8C0BFCA2) make SHC dump a pool
 * right after the first BRA of func_8C0BD8B2 (8C0BD920) and the next one
 * after the descend block (8C0BDAB0), so the "jump held" test stays a short
 * BT. Without pending entries SHC dumps a single pool before the descend
 * block and emits BF + BRA there instead. The stubs of the five preceding
 * functions of that unit (code_8C0BD4CC.c) leave a pending __asm_stub_sink
 * entry that reproduces the original dump points (fewer stubs do not); the
 * splicer ignores stub bodies and keeps the original pools.
 */
#include "sa1/pending.h"

/* Tails SE: func_8C0940DA(se, 0, 0, 0) unless twp->counter.b[0] == 1 (code_8C0BBA4E.c) */
void func_8C0BFCA2(taskwk *twp, Sint32 se);
/* air acceleration, weak stick variant (code_8C06F6C0.c) */
void func_8C06F6C0(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* flight tails (code_8C0BA820.c). Arrays: a plain struct extern makes SHC
 * fold sym+off into the literal pool (the original loads the base). */
extern NJS_OBJECT D_8C2606A8[];    /* flight tail A */
extern NJS_OBJECT D_8C260028[];    /* flight tail B */

/* preceding functions of the original unit (code_8C0BD4CC.c), stubbed only
 * for the literal pool state described above */
ASM_STUB(void func_8C0BD4CC(taskwk *twp, playerwk *pwp))
ASM_STUB(void func_8C0BD6B8(taskwk *twp, playerwk *pwp))
ASM_STUB(Angle func_8C0BD702(playerwk *pwp))
ASM_STUB(Sint32 func_8C0BD800(taskwk *twp, playerwk *pwp))
ASM_STUB(Sint32 func_8C0BD83C(taskwk *twp, playerwk *pwp))

void func_8C0BD8B2(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    zxsdwstr zss;
    Float h;
    Float pow;
    Sint8 n;

    /* near the ground: brake the fall (to -3.0 from 50 down to 25 units,
     * to -0.3 below 25) */
    zss.pos = twp->pos;
    func_8C02AD22(&zss, 20.0f);
    if (zss.lower.findflag == 1) {
        h = twp->pos.y - zss.lower.onpos;
        /* "+=" form: the original computes &pwp->spd.y into R1 here */
        if (pwp->spd.y < -3.0f && h >= 25.0f && h < 50.0f) {
            mwp->acc.y SH_ONLY(+= 0.25f * (-3.0f - pwp->spd.y))
                PC_ONLY(= SA1_FMAC(0.25f, -3.0f - pwp->spd.y, mwp->acc.y));
        } else if (pwp->spd.y < -0.29999999f && h > 0.0f && h < 25.0f) {
            mwp->acc.y SH_ONLY(+= 0.25f * (-0.29999999f - pwp->spd.y))
                PC_ONLY(= SA1_FMAC(0.25f, -0.29999999f - pwp->spd.y, mwp->acc.y));
        }
    }
    twp->timer.b[1] = 0;
    if (B_8C754C08[(Uint8)twp->counter.b[0]].on & D_8C19E390) {
        /* jump held: climb while the flight power lasts (1/120 per frame) */
        pow = pwp->free.f[7] + 0.008333333f;
        if (pow > 1.0f) {
            pow = 1.0f;
        }
        pwp->free.f[7] = pow;
        if (pow < 1.0f) {
            twp->timer.b[1] = 1;
            if (pwp->spd.y < 0.9f) {
                mwp->acc.y += 0.099999996f;
            } else {
                mwp->acc.y += 0.99999996f - pwp->spd.y;
            }
        } else if (pwp->spd.y < 0.0f) {
            /* exhausted: slow the fall, or drop (mode 19, action 42) */
            if (pwp->spd.y > -2.6499999f) {
                mwp->acc.y -= pwp->spd.y * 0.02f;
            } else {
                twp->mode = 19;
                pwp->mj.reqaction = 42;
                twp->flag &= ~0x400;
                twp->timer.b[2] &= ~1;
                func_8C0BFCA2(twp, 0x556);
            }
        }
    } else if (B_8C754C08[(Uint8)twp->counter.b[0]].on & D_8C19E38C) {
        /* action held: descend, up to the vertical speed limit */
        if (pwp->spd.y > -pwp->p.lim_v_spd) {
            mwp->acc.y += -0.099999996f;
        } else {
            mwp->acc.y += -pwp->p.lim_v_spd - pwp->spd.y + -0.099999996f;
        }
        twp->timer.b[1] = -1;
    } else if (pwp->spd.y < -2.0f) {
        mwp->acc.y SH_ONLY(+= 0.099999996f * (-2.0f - pwp->spd.y))
            PC_ONLY(= SA1_FMAC(0.099999996f, -2.0f - pwp->spd.y, mwp->acc.y));
    }
    func_8C06F6C0(twp, mwp, pwp);
    /* tail spin counter: 8 (4 once the power is past 0.4) per frame,
     * 1.5x climbing, 0.5x descending */
    if (pwp->free.f[7] < 0.39999998f) {
        n = 8;
    } else {
        n = 4;
    }
    n += n * twp->timer.b[1] / 2;
    if ((twp->timer.b[0] += n) >= 64) {
        twp->timer.b[0] = 0;
    }
}

void func_8C0BDB72(taskwk *twp, playerwk *pwp)
{
    NJS_OBJECT *obj;
    Sint32 ang;
    Sint32 spd;
    Uint16 step;
    Float sx;
    Float sy;

    /* spin both tails: a step per frame (doubled when the logic ran two
     * steps), halved once the flight power is past 0.4, kept on 0x800 */
    obj = D_8C2606A8;
    ang = obj->ang[0];
    if (B_8C754EC4 == 2) {
        spd = 0x1000;
    } else {
        spd = 0x800;
    }
    if (pwp->free.f[7] < 0.39999998f) {
        ang += spd;
    } else {
        /* the shift is done in place and the halved step is a Uint16
         * local (SHAR R7; EXTU.W R7,R5): otherwise SHC merges both adds */
        spd >>= 1;
        step = spd;
        ang += step;
    }
    ang &= 0xF800;
    obj->ang[0] = ang;
    obj = D_8C260028;
    obj->ang[0] = ang & 0xF800;
    /* stretch while descending, squash back otherwise */
    obj = D_8C2606A8;
    if (twp->timer.b[1] == -1) {
        obj->scl[0] += 0.02f;
        obj->scl[1] -= 0.02f;
    } else {
        obj->scl[0] -= 0.02f;
        obj->scl[1] += 0.02f;
    }
    if (obj->scl[0] < 1.0f) {
        obj->scl[0] = 1.0f;
        obj->scl[1] = 1.0f;
    }
    if (obj->scl[0] > 1.3f) {
        obj->scl[0] = 1.3f;
        obj->scl[1] = 0.7f;
    }
    sx = obj->scl[0];
    sy = obj->scl[1];
    obj->scl[2] = sx;
    obj = D_8C260028;
    obj->scl[0] = sx;
    obj->scl[1] = sy;
    obj->scl[2] = sx;
}
