#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06CA30-8C06CCA0): reactions to the object the
 * player's collision hit (colliwk.hit_cwp).
 *   func_8C06CA30  the hit object's taskwk id is 7
 *   func_8C06CA4E  push the hit object (id 7) away: writes its motionwk.acc,
 *                  scaled by the progress through the current animation
 *   func_8C06CB40  turn towards a hit object of id 4/5/6 and push the player
 *                  back out of it
 *
 * Translation unit: the original object starts before 8C06CA30 (no pool or
 * file gap between 8C06CA2E and 8C06CA30) and continues after 8C06CCA0 (the
 * pool at 8C06CC60 is followed by 32-byte 00 09 fill, i.e. a mid-file pool).
 * func_8C06CA4E and func_8C06CB40 match only when compiled after
 * code_8C06C880.c, as in the original object: the literal pool that SHC dumps
 * at 8C06CABE (inside func_8C06CA4E) depends on the preceding code, and it
 * decides the long-branch forms (BF/BRA) of both functions. Check with
 *   py -3.12 tools/shcdiff.py <func> src/1st_read/code_8C06CA30.c
 *       --flags=-define=TU_EXPERIMENT,NON_MATCHING
 */
#ifdef TU_EXPERIMENT
#include "1st_read/code_8C06C880.c"
#endif

/* CCL hit query (8C0344DC): fills *v with the push vector between the
 * player's and the hit object's shapes; returns non-zero (a byte) on a hit */
Sint8 func_8C0344DC(taskwk *twp, NJS_POINT3 *v);

extern Sint32 D_8C1BDFE0;               /* cleared every frame by 8C06C6F8 */

/* Non-zero when the object hit this frame is a task of id 7. mwp and pwp are
 * not read; every caller passes them (R5/R6). */
Sint32 func_8C06CA30(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    colliwk *hit;

    hit = CWP(twp)->hit_cwp;
    if (hit == NULL || (Uint8)hit->mytask->twp->id != 7) {
        return 0;
    }
    return 1;
}

#ifdef NON_MATCHING
/*
 * Matches only in the original translation unit (TU_EXPERIMENT, see above).
 * Push the hit object (id 7) away from the player: its motionwk.acc is set to
 * the negated horizontal push direction, scaled 0 -> 1 -> 0 over the
 * player's current action (peak at mid-animation).
 */
void func_8C06CA4E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    mtnjvwk *mjp;
    Float frames;
    Float half;
    Float r;

    if (func_8C06CA30(twp, mwp, pwp) == 0) {
        return;
    }
    if (func_8C0344DC(twp, &v) == 0) {
        return;
    }
    v.y = 0.0f;
    if (njScalor(&v) != 0.0f) {
        njUnitVector(&v);
    }
    mjp = &pwp->mj;
    frames = (Float)mjp->plactptr[mjp->action].actptr->motion->nbFrame;
    half = frames * 0.5f;
    if (mjp->nframe <= half) {
        r = mjp->nframe / half;
    } else {
        r = (frames - mjp->nframe) / half;
    }
    v.x = -(v.x * r);
    v.y = -(v.y * r);
    v.z = -(v.z * r);
    CWP(twp)->hit_cwp->mytask->mwp->acc = v;
}
#else
ASM_STUB(void func_8C06CA4E(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

#ifdef NON_MATCHING
/*
 * Matches only in the original translation unit (TU_EXPERIMENT, see above).
 * On a hit: turn the player towards the push direction (ang_aim.y by 0x800,
 * ang.y by 0xC00 per frame). Then, for a hit object of id 4/5/6, move the
 * player back by the hit vector (and set D_8C1BDFE0) when the horizontal
 * distance between the object and the player's first collision shape is at
 * least the object's radius (2.22, 7.5, 27.5) plus that shape's radius.
 * Returns 1 for objects 4/5/6, else 0.
 * The two "register" keywords are needed for SHC's register allocation.
 */
Sint32 func_8C06CB40(register taskwk *twp, motionwk *mwp)
{
    NJS_VECTOR v;
    NJS_VECTOR d;
    colliwk *hit;
    register taskwk *htwp;
    NJS_POINT3 *c;
    Float r;

    if (func_8C0344DC(twp, &v) != 0) {
        mwp->ang_aim.y = AdjustAngle(mwp->ang_aim.y, SA1_FTOI(atan2f(-v.z, -v.x) * 10430.381f), 0x800);
        twp->ang.y = AdjustAngle(twp->ang.y, mwp->ang_aim.y, 0xC00);
        hit = CWP(twp)->hit_cwp;
        if (hit != NULL) {
            htwp = hit->mytask->twp;
            d = htwp->pos;
            c = &CWP(twp)->info->center;
            d.x -= c->x;
            d.y = 0.0f;
            d.z -= c->z;
            switch ((Uint8)htwp->id) {
            case 4:
                r = 2.21999985f;     /* 0x400E147A (2.22f truncated) */
                break;
            case 5:
                r = 7.5f;
                break;
            case 6:
                r = 27.5f;
                break;
            default:
                return 0;
            }
            r += CWP(twp)->info->a;
            if (njScalor(&d) >= r) {
                twp->pos.x -= v.x;
                twp->pos.y -= v.y;
                twp->pos.z -= v.z;
                D_8C1BDFE0 = 1;
            }
            return 1;
        }
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C06CB40(taskwk *twp, motionwk *mwp))
#endif
