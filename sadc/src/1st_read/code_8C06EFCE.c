#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06EFCE-8C06F530): acceleration under an event
 * lock and the simple accelerations used by the character mains.
 *  func_8C06EFCE  air acceleration while the input is locked
 *                 (twp->flag & 0x4000; called by func_8C06E904, 8C07061C,
 *                 8C070814). The event work (twp->ewp) steers the player:
 *                 lock 2 runs towards the point ewp+0x28, lock 3 runs along
 *                 the direction ewp+0x28 with the speed given by its length,
 *                 lock 4 pushes along the vector ewp+0x28. On arrival (or a
 *                 zero vector) the speed is cleared and lock becomes 1; any
 *                 lock value other than 1-4 releases the input lock.
 *  func_8C06F25C  plain acceleration (gravity, air_resist, stick input
 *                 scaled by run_accel or slow_down, static friction).
 *  func_8C06F386  acceleration while pushing against an object: faces the
 *                 push-out direction of the CCL hit (func_8C0344DC); the
 *                 stick direction class from func_8C06CFCC selects push
 *                 (acc.x = +a), pull (-a) or braking against the speed.
 */

/* rotate twp->ang.y towards ang (AdjustAngle step 0x2000), keeping the
 * player-space speed pwp->spd (8C072000; mwp unused) */
void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang);

/* stick direction vs. push-out direction of the CCL hit, 1..4
 * (src/1st_read/code_8C06CFCC.c). Callers pass twp and mwp (R5 is set at
 * the call in 8C06F386), the definition only reads twp. */
Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);

/* CCL hit query: push-out vector from the hit shape (8C0344DC) */
Sint8 func_8C0344DC(taskwk *twp, NJS_POINT3 *v);

/* eventwk +0x28 holds a point/vector (floats) for locks 2-4 and angles for
 * locks 5/6 (8C0AFE72); include/sa1/engine.h declares it as Angle ang[3]. */
#define EWP_VEC(ewp) (*(NJS_POINT3 *)(ewp)->ang)

#ifdef NON_MATCHING
/*
 * Same instruction sequence as the original except for register allocation
 * of the hoisted constants: the original keeps atan2f in R9 and the lock
 * value 1 in R8 (PClearSpeed is loaded at its single, merged call site);
 * this C gets PClearSpeed in R9 and atan2f in R8 (other spellings tried give
 * {atan2f, 1} but then swap FR13/FR14 or stop SHC from merging the three
 * "clear speed, lock = 1" blocks into the one in case 4). The sibling
 * 8C06EAC0 (same switch, same allocation) may show the original spelling.
 * The first literal pool also lands elsewhere, so the switch branches to
 * case 4 and to the merged block use the near forms.
 */
void func_8C06EFCE(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR add;
    NJS_VECTOR acc;
    NJS_VECTOR v;
    eventwk *ewp;
    Float dist;

    ewp = (eventwk *)twp->ewp;
    acc = mwp->acc;
    add.x = add.y = add.z = 0.0f;
    SA1_FMAC_ACC(acc.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(acc.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(acc.z, Gravity.z, pwp->p.weight);
    switch ((Uint8)ewp->lock) {
    case 2:
        v = EWP_VEC(ewp);
        njSubVector(&v, &twp->pos);
        v.y = 0.0f;
        dist = njScalor(&v);
        if (!(dist > 1.0f)) {
            PClearSpeed(NULL, pwp);
            ewp->lock = 1;
            break;
        }
        mwp->ang_aim.y = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f) & 0xFFFF;
        add.x = pwp->p.run_speed - pwp->spd.x;
        if (add.x > 0.0f) {
            if (add.x > pwp->p.run_accel) {
                add.x = pwp->p.run_accel;
            }
        } else {
            add.x = 0.0f;
        }
        break;
    case 3:
        v = EWP_VEC(ewp);
        v.y = 0.0f;
        dist = njScalor(&v);
        if (dist == 0.0f) {
            PClearSpeed(NULL, pwp);
            ewp->lock = 1;
            break;
        }
        mwp->ang_aim.y = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f) & 0xFFFF;
        if ((add.x = dist - pwp->spd.x) > 0.0f) {
            if (add.x > pwp->p.run_accel) {
                add.x = pwp->p.run_accel;
            }
        } else {
            add.x = 0.0f;
        }
        break;
    case 4:
        v = EWP_VEC(ewp);
        v.y = 0.0f;
        dist = njScalor(&v);
        if (dist == 0.0f) {
            PClearSpeed(NULL, pwp);
            ewp->lock = 1;
            break;
        }
        mwp->ang_aim.y = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f) & 0xFFFF;
        njAddVector(&acc, &v);
        break;
    case 1:
        break;
    default:
        twp->flag &= ~0x4000;
        break;
    }
    PConvertVector_G2P(twp, &acc);
    SA1_FMAC_ACC(acc.x, pwp->p.air_resist_air, pwp->spd.x);
    SA1_FMAC_ACC(acc.y, pwp->p.air_resist_y, pwp->spd.y);
    SA1_FMAC_ACC(acc.z, pwp->p.air_resist_z, pwp->spd.z);
    func_8C072000(twp, mwp, pwp, mwp->ang_aim.y);
    acc.x += add.x;
    pwp->acc = acc;
}
#else
ASM_STUB(void func_8C06EFCE(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

#ifdef NON_MATCHING
/*
 * Instruction-for-instruction the original, but the match depends on where
 * SHC dumps the shared literal pool: in the original the pool that holds
 * func_8C06EFCE's later entries lands inside this function (8C06F300), which
 * turns the early-return branch into the far form (BT/S; BRA). That only
 * happens when func_8C06EFCE is real C with the original code size; with
 * func_8C06EFCE as ASM_STUB the pool moves into func_8C06F386 and the branch
 * becomes BF/S. Verified MATCH with an earlier func_8C06EFCE draft in front.
 */
void func_8C06F25C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    Float mag;
    Sint32 on;
    Float lim;

    if (twp->flag & 0x4000) {
        return;
    }
    v.x = v.y = v.z = 0.0f;
    SA1_FMAC_ACC(v.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(v.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(v.z, Gravity.z, pwp->p.weight);
    PConvertVector_G2P(twp, &v);
    SA1_FMAC_ACC(v.x, pwp->p.air_resist, pwp->spd.x);
    if ((on = GetAnalog(twp, NULL, &mag)) == 0) {
        mag = pwp->p.slow_down;
    } else {
        mag = pwp->p.run_accel * mag;
    }
    v.x += mag;
    if (pwp->spd.x == 0.0f) {
        lim = v.y * pwp->p.lim_frict;
        if (on == 0 && lim > v.x && v.x > -lim) {
            v.x = 0.0f;
        }
    }
    pwp->acc = v;
}
#else
ASM_STUB(void func_8C06F25C(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C06F386(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    Float mag;
    Sint32 dir;
    Sint32 res;
    Float lim;

    if (twp->flag & 0x4000) {
        return;
    }
    res = func_8C06CFCC(twp, mwp);
    if (res >= 4) {
        dir = 1;
    } else if (res == 1) {
        dir = -1;
    } else {
        dir = 0;
    }
    func_8C0344DC(twp, &v);
    mwp->ang_aim.y = SA1_FTOI(atan2f(-v.z, -v.x) * 10430.381f) & 0xFFFF;
    twp->ang.y = mwp->ang_aim.y;
    v.x = v.y = v.z = 0.0f;
    SA1_FMAC_ACC(v.x, Gravity.x, pwp->p.weight);
    SA1_FMAC_ACC(v.y, Gravity.y, pwp->p.weight);
    SA1_FMAC_ACC(v.z, Gravity.z, pwp->p.weight);
    PConvertVector_G2P(twp, &v);
    SA1_FMAC_ACC(v.x, pwp->p.air_resist, pwp->spd.x);
    if (dir == 0) {
        mag = pwp->p.slow_down;
    } else {
        GetAnalog(twp, NULL, &mag);
        mag = pwp->p.run_accel * mag;
    }
    v.x += mag;
    if (pwp->spd.x == 0.0f) {
        lim = v.y * pwp->p.lim_frict;
        if (dir == 0 && lim > v.x && v.x > -lim) {
            v.x = 0.0f;
        }
    }
    /* only acc.x is written */
    if (dir > 0) {
        pwp->acc.x = v.x;
    } else if (dir < 0) {
        pwp->acc.x = -v.x;
    } else {
        v.x = _builtin_fabsf(v.x);
        if (pwp->spd.x > 0.0f) {
            pwp->acc.x = -v.x;
        } else if (pwp->spd.x < 0.0f) {
            pwp->acc.x = v.x;
        }
    }
}
