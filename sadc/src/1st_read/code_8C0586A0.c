#include "sa1/sa1.h"

/*
 * 1ST_READ chunk 8C0586A0 (part): the movable-object helpers (code map
 * "ObjectMovable*").
 *
 *   8C0586A0  movable-object setup (SADX ObjectMovableInitialize analogue):
 *             by lift class n 4..7 it sets the object's radius
 *             (motionwk.rad) and fall weight (motionwk.weight), ORs the
 *             movable flags into taskwk.flag and stores n as the object class
 *             taskwk.id (4 = liftable by everyone, 5/6 = heavy, 7 = class 7);
 *             returns 0 without a motion work or for another n. Callers: the
 *             O_SAMPLE_S/M/L/SW objects 8C064980.., the animals 8C01593E,
 *             8C0FBB00; libsa1's HOLDABLE host object (libsa1_interaction.md
 *             6.1.2 / 6.7).
 *   8C05872A  the movable-object step (exec helper): gravity (acc.y =
 *             -weight off the ground), friction (x0.996 in the air, x0.1 and
 *             a stop below 1.0 on the ground), fall speed clamped to +-6;
 *             then the object sphere (r 2.5, centre 2.48 above pos along the
 *             object's own up axis) is moved with spd + acc against the land
 *             collision (8C056A04, attribute mask 2). A floor hit (or any hit
 *             while not flag 2) stands the object on the ground with the
 *             floor's angles; a wall hit while flag 2 slides it and slows it;
 *             no hit moves it freely with spd. Last, unless held (flag
 *             0x1000), the collision entry (CCL_Entry) and the clear of the
 *             acceleration (8C058D3C). Callers: O_SAMPLE_S's exec 8C0649CC,
 *             the snowboard tasks 8C0B2F46 (Sonic) and 8C0BF804 (Tails)
 *             (gsa1_060_board.md F1 / Review R.2.2).
 *   8C058A1A  the heavy variant of the step (O_SAMPLE_M/L 8C064B20 /
 *             8C064C04, ADV02): gravity without acc (on the ground spd.y = 0),
 *             air friction x0.98, no move on the ground in sub step
 *             B_8C754EC8 != 0, sphere r 5.0 at 4.9 above pos, no slow-down on
 *             walls, and the acceleration is cleared even while held.
 *   8C058D20  the held-object step: CCL_Entry, then 8C058D3C (falls through
 *             into it in the original; caller 8C064D50, O_SAMPLE_SW)
 *   8C058D3C  clears taskwk.flag 0x38 and motionwk.acc.
 *   8C058D5A  rotates a vector by the inverse of the object's angles (-Y, -X,
 *             -Z), in place (no direct caller).
 *   8C058DD4  rotates a vector by the object's angles (Z, X, Y), in place.
 *
 * Not here (still assembly): 8C058E48.. (functions 8-29 of the chunk).
 */

Sint32 func_8C0586A0(taskwk *twp, motionwk *mwp, Sint32 n)
{
    if (!mwp) {
        return 0;
    }
    switch (n) {
    case 4:
        mwp->rad = 5.0f;
        mwp->weight = 0.02f;
        twp->flag |= 0x240;
        break;
    case 5:
        mwp->rad = 20.0f;
        mwp->weight = 0.099999996f;
        twp->flag |= 0x640;
        break;
    case 6:
        mwp->rad = 30.0f;
        mwp->weight = 0.19999999f;
        twp->flag |= 0x640;
        break;
    case 7:
        mwp->rad = 5.0f;
        mwp->weight = 1.0f;
        twp->flag |= 0x840;
        break;
    default:
        return 0;
    }
    twp->id = n;
    return 1;
}

void func_8C05872A(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    Sint32 res;
    NJS_POINT3 spd;
    NJS_VECTOR v;
    NJS_POINT3 pos;
    Angle3 ang;

    twp = tp->twp;
    mwp = tp->mwp;
    if (!(twp->flag & 0x1000)) {
        if (twp->flag & 1) {
            mwp->acc.y = 0.0f;
            mwp->spd.y = 0.0f;
        } else {
            mwp->acc.y = -mwp->weight;
        }
        if (!(twp->flag & 1)) {
            mwp->spd.x *= 0.996f;
            mwp->spd.z *= 0.996f;
        } else {
            mwp->spd.x *= 0.099999996f;
            mwp->spd.z *= 0.099999996f;
            if (_builtin_fabsf(mwp->spd.x) <= 1.0f) {
                mwp->spd.x = 0.0f;
            }
            if (_builtin_fabsf(mwp->spd.z) <= 1.0f) {
                mwp->spd.z = 0.0f;
            }
        }
        mwp->spd.y += mwp->acc.y;
        if (mwp->spd.y > 6.0f) {
            mwp->spd.y = 6.0f;
        } else if (mwp->spd.y < -6.0f) {
            mwp->spd.y = -6.0f;
        }
        pos = twp->pos;
        spd.x = mwp->spd.x + mwp->acc.x;
        spd.y = mwp->spd.y + mwp->acc.y;
        spd.z = mwp->spd.z + mwp->acc.z;
        ang = twp->ang;
        v.x = 0.0f;
        v.y = 2.4799998f;
        v.z = 0.0f;
        func_8C058DD4(twp, &v);
        njAddVector(&pos, &v);
        if (!func_8C042DC4(tp)) {
            res = func_8C056A04(&pos, &spd, &ang, 2, 2.5f);
            /* 2U: the same unsigned constant as the attribute mask above (Uint32 parameter); a plain 2 gives
             * SHC another register allocation */
            if (((twp->flag & 2U) && (res & 1)) || (!(twp->flag & 2U) && res)) {
                twp->pos.x = pos.x + spd.x;
                twp->pos.y = pos.y + spd.y;
                twp->pos.z = pos.z + spd.z;
                twp->ang = ang;
                v.x = 0.0f;
                v.y = 2.5f;
                v.z = 0.0f;
                func_8C058DD4(twp, &v);
                njSubVector(&twp->pos, &v);
                twp->flag |= 1;
            } else {
                if ((twp->flag & 2) && (res & 2)) {
                    twp->pos.x = pos.x + spd.x;
                    twp->pos.y = pos.y + spd.y;
                    twp->pos.z = pos.z + spd.z;
                    v.x = 0.0f;
                    v.y = 2.5f;
                    v.z = 0.0f;
                    func_8C058DD4(twp, &v);
                    njSubVector(&twp->pos, &v);
                    mwp->spd.x *= 0.099999996f;
                    mwp->spd.z *= 0.099999996f;
                } else {
                    twp->pos.x += mwp->spd.x;
                    twp->pos.y += mwp->spd.y;
                    twp->pos.z += mwp->spd.z;
                }
                twp->ang.x = 0;
                twp->ang.z = 0;
                twp->flag &= ~1;
            }
        }
    } else {
        twp->flag &= ~1;
    }
    if (!(twp->flag & 0x1000) && !func_8C042DC4(tp)) {
        CCL_Entry(twp);
        func_8C058D3C(twp, mwp);
    }
}

void func_8C058A1A(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    Float g;
    Sint32 res;
    NJS_POINT3 spd;
    NJS_VECTOR v;
    NJS_POINT3 pos;
    Angle3 ang;

    twp = tp->twp;
    mwp = tp->mwp;
    if (!(twp->flag & 0x1000)) {
        if (twp->flag & 1) {
            g = 0.0f;
            mwp->spd.y = g;
        } else {
            g = -mwp->weight;
        }
        if (!(twp->flag & 1)) {
            mwp->spd.x *= 0.97999997f;
            mwp->spd.z *= 0.97999997f;
        } else {
            mwp->spd.x *= 0.099999996f;
            mwp->spd.z *= 0.099999996f;
            if (_builtin_fabsf(mwp->spd.x) <= 1.0f) {
                mwp->spd.x = 0.0f;
            }
            if (_builtin_fabsf(mwp->spd.z) <= 1.0f) {
                mwp->spd.z = 0.0f;
            }
        }
        mwp->spd.y += g;
        if (mwp->spd.y > 6.0f) {
            mwp->spd.y = 6.0f;
        } else if (mwp->spd.y < -6.0f) {
            mwp->spd.y = -6.0f;
        }
        if (!(twp->flag & 1) || !B_8C754EC8) {
            pos = twp->pos;
            spd.x = mwp->spd.x + mwp->acc.x;
            spd.y = mwp->spd.y + mwp->acc.y;
            spd.z = mwp->spd.z + mwp->acc.z;
            ang = twp->ang;
            v.x = 0.0f;
            v.y = 4.8999997f;
            v.z = 0.0f;
            func_8C058DD4(twp, &v);
            njAddVector(&pos, &v);
            if (!func_8C042DC4(tp)) {
                res = func_8C056A04(&pos, &spd, &ang, 2, 5.0f);
                if (((twp->flag & 2U) && (res & 1)) || (!(twp->flag & 2U) && res)) {
                    twp->pos.x = pos.x + spd.x;
                    twp->pos.y = pos.y + spd.y;
                    twp->pos.z = pos.z + spd.z;
                    twp->ang = ang;
                    v.x = 0.0f;
                    v.y = 5.0f;
                    v.z = 0.0f;
                    func_8C058DD4(twp, &v);
                    njSubVector(&twp->pos, &v);
                    twp->flag |= 1;
                } else {
                    if ((twp->flag & 2) && (res & 2)) {
                        twp->pos.x = pos.x + spd.x;
                        twp->pos.y = pos.y + spd.y;
                        twp->pos.z = pos.z + spd.z;
                        v.x = 0.0f;
                        v.y = 5.0f;
                        v.z = 0.0f;
                        func_8C058DD4(twp, &v);
                        njSubVector(&twp->pos, &v);
                    } else {
                        twp->pos.x += mwp->spd.x;
                        twp->pos.y += mwp->spd.y;
                        twp->pos.z += mwp->spd.z;
                    }
                    twp->ang.x = 0;
                    twp->ang.z = 0;
                    twp->flag &= ~1;
                }
            }
        }
    } else {
        twp->flag &= ~1;
    }
    if (!(twp->flag & 0x1000) && !func_8C042DC4(tp)) {
        CCL_Entry(twp);
    }
    func_8C058D3C(twp, mwp);
}

void func_8C058D20(task *tp)
{
    taskwk *twp;
    motionwk *mwp;

    twp = tp->twp;
    mwp = tp->mwp;
    CCL_Entry(twp);
    func_8C058D3C(twp, mwp);
}

void func_8C058D3C(taskwk *twp, motionwk *mwp)
{
    twp->flag &= ~0x38;
    if (mwp) {
        mwp->acc.x = mwp->acc.y = mwp->acc.z = 0.0f;
    }
}

void func_8C058D5A(taskwk *twp, NJS_VECTOR *v)
{
    NJS_VECTOR vs;

    vs.x = v->x;
    vs.y = v->y;
    vs.z = v->z;
    njPushMatrix(D_8C67C640);
    -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
    -twp->ang.x ? njRotateX(NULL, -twp->ang.x) : (void)0;
    -twp->ang.z ? njRotateZ(NULL, -twp->ang.z) : (void)0;
    njCalcVector(NULL, &vs, v);
    njPopMatrix(1);
}

void func_8C058DD4(taskwk *twp, NJS_VECTOR *v)
{
    NJS_VECTOR vs;

    vs.x = v->x;
    vs.y = v->y;
    vs.z = v->z;
    njPushMatrix(D_8C67C640);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    njCalcVector(NULL, &vs, v);
    njPopMatrix(1);
}
