/*
 * Player common (1ST_READ 8C076782): Knuckles' wall / ceiling grab step
 * (called by KnucklesMain 8C0C0640 with its taskwk, motionwk and playerwk).
 *
 * func_8C076782 first carries the player with the moving land object stood
 * on (pwp->ttp force work), returns 1/2 (slower/faster than run speed) when
 * the CCL hit object has a "climbable" shape flag, and on twp->flag & 2 only
 * moves the player. Otherwise it probes the land collision (csts
 * pwp->cstsp, 8C028BFE + 8C029B00 over the gathered list B_8C754EF4):
 *   1. a sphere of radius 0.75 * height ahead of the player along the
 *      player's up vector: a ceiling (normal2.y < -0.86) or a steep wall
 *      facing away from up pushes the player out by half the normal (unless
 *      the stick is held, GetAnalog) and ends the step;
 *   2. when falling, a probe lower down: a floor (normal2.y >= 0.86) is
 *      copied into cp->normal2 and ends the step;
 *   3. a probe 0.3 along up: of all touched walls (-0.86 <= ny <= 0.71,
 *      up . normal2 > 0.259) the best one (nearest the probe direction, or
 *      the one ahead) becomes the climbing wall: the player's angles are
 *      rebuilt from the halfway vector between up and the wall normal,
 *      the position snaps to the touch point (plus the wall object's force
 *      work) and pwp->ttp / pwp->lclop are set; else, when rising, a probe
 *      higher up copies a floor into cp->normal2.
 * Returns 0, or 1/2 from the CCL check.
 *
 * NON_MATCHING:
 *  - The original object starts before 8C076782: the literal pool that SHC
 *    dumps at 8C076878 holds func_8C076640's constants, and it decides the
 *    long-branch form (BF/S + BRA) at 8C0767F0. Compile together with
 *    code_8C076640.c to reproduce it:
 *      py -3.12 tools/shcdiff.py func_8C076782 src/1st_read/code_8C076782.c
 *          --flags=-define=TU_EXPERIMENT,NON_MATCHING
 *  - Then 37 words differ: the three "best = ti->hit" struct copies call
 *    SHC's __quick_mvn, which config/symbols.1st_read.txt maps to 8C10CD38
 *    while the game calls 8C10CDDC (same situation as code_8C0742D4.c);
 *    the two push-out blocks get R4/R5 swapped (pos pointer in R4 instead
 *    of R5) and two JSR address registers are R3 instead of R2
 *    (register-allocation ties; every instruction is otherwise identical).
 */
/* the shared header declares 8C029B00 as (csts *, Uint32, Uint32); R5 is the
 * land object (_OBJ_LANDCOLL.pObject) and R6 its attribute (slAttribute) */
#define func_8C029B00 func_8C029B00_hdr
#include "sa1/sa1.h"
#undef func_8C029B00

#ifdef TU_EXPERIMENT
#include "1st_read/code_8C076640.c"
#endif

/* force work (task.fwp, CreateElementalTask im & 4: CAlloc(2, 0x1C));
 * same local definition as code_8C056A04.c */
typedef struct forcewk {
    void (*call_back)(task *tp, taskwk *twp, struct forcewk *fwp);   /* 0x00 */
    Angle3 ang_spd;             /* 0x04 */
    NJS_POINT3 pos_spd;         /* 0x10 */
} forcewk;                      /* 0x1C */

/* first 0x50 bytes of a touch (copied as a whole: struct copy of 80 bytes) */
typedef struct {
    Angle angx;                 /* 0x00 */
    Angle angz;                 /* 0x04 */
    NJS_VECTOR normal;          /* 0x08 */
    NJS_VECTOR normal2;         /* 0x14 */
    NJS_POINT3 onpoly;          /* 0x20 */
    NJS_POINT3 pshbk;           /* 0x2C */
    NJS_VECTOR anaspdh;         /* 0x38 */
    NJS_VECTOR anaspdv;         /* 0x44 */
} TOUCHHIT;                     /* 0x50 */

/* one touch of 8C029B00 (B_8C6BACF0[], TOUCHINFO of code_8C029B00.c) */
typedef struct {
    TOUCHHIT hit;               /* 0x00 */
    Float dist;                 /* 0x50 */
    Float dist2;                /* 0x54 */
} TOUCHINFO;                    /* 0x58 */

SA1_ASSERT_SIZE(forcewk, 0x1C);
SA1_ASSERT_OFFSET(forcewk, pos_spd, 0x10);
SA1_ASSERT_SIZE(TOUCHHIT, 0x50);
SA1_ASSERT_OFFSET(TOUCHHIT, normal2, 0x14);
SA1_ASSERT_OFFSET(TOUCHHIT, onpoly, 0x20);
SA1_ASSERT_SIZE(TOUCHINFO, 0x58);

extern TOUCHINFO B_8C6BACF0[];          /* touches of the last 8C029B00 call (cp->find_count) */

Sint32 func_8C029B00(csts *cp, NJS_OBJECT *object, Uint32 attr);
Sint8 func_8C0344DC(taskwk *twp, NJS_POINT3 *v);

#define NJM_RAD_ANG(n) SA1_FTOI((n) * 10430.381f)

/*
 * FMAC sites (19 in the original): "c + a * b" and "a * b - c". For SHC they
 * expand to the original expression trees (keeps the TU_EXPERIMENT result);
 * on PC they are fused (fmaf), like SA1_FMAC. The two push-out blocks use
 * SA1_FMAC_ACC; clang merges the two identical blocks, so the libsa1 object
 * has 16 fused ops for the 19 FMACs.
 */
#if defined(TARGET_PC)
#define FMAC_ADD(c, a, b) SA1_FMAC((a), (b), (c))
#define FMAC_SUB(a, b, c) SA1_FMAC((a), (b), -(c))
#else
#define FMAC_ADD(c, a, b) ((c) + (a) * (b))
#define FMAC_SUB(a, b, c) ((a) * (b) - (c))
#endif

#ifdef NON_MATCHING
Sint32 func_8C076782(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR up;
    TOUCHHIT best;
    NJS_VECTOR dir;
    NJS_VECTOR v1;
    NJS_VECTOR v2;
    NJS_VECTOR v3;
    NJS_VECTOR v4;
    csts *cp;
    Float *q;
    Float *p;
    forcewk *fwp;
    _OBJ_LANDCOLL *lc;
    _OBJ_LANDCOLL *bestlc;
    TOUCHINFO *ti;
    task *tp;
    Sint32 n;
    Sint16 k;
    Sint32 found;
    Angle angz;
    Angle angy;
    Angle angx;
    Sint32 ret;
    Float ip;

    ret = 0;
    if (pwp->ttp != NULL) {
        fwp = (forcewk *)pwp->ttp->fwp;
        if (fwp != NULL) {
            twp->pos.x += fwp->pos_spd.x;
            twp->pos.y += fwp->pos_spd.y;
            twp->pos.z += fwp->pos_spd.z;
        }
    }
    if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && (CWP(twp)->flag & 8)
        && (CWP(twp)->hit_cwp->info[CWP(twp)->hit_num].b05 & 0xF)) {
        func_8C0344DC(twp, &dir);
        v1 = mwp->spd;
        if (njScalor(&v1) != 0.0f) {
            njUnitVector(&v1);
        }
        ip = njInnerProduct(&dir, &v1);
        if (ip < 0.0f) {
            mwp->spd.x = 0.0f;
            mwp->spd.z = 0.0f;
        }
        if (!(pwp->spd.x < pwp->p.run_speed)) {
            ret = 2;
        } else {
            ret = 1;
        }
    }
    if (twp->flag & 2) {
        twp->flag &= ~3;
        twp->pos.x += mwp->spd.x;
        twp->pos.z += mwp->spd.z;
        return ret;
    }
    twp->flag &= ~3;
    cp = pwp->cstsp;
    up.x = up.z = 0.0f;
    up.y = 1.0f;
    PConvertVector_P2G(twp, &up);
    cp->radius = pwp->p.height * 0.75f;
    cp->angx = twp->ang.x;
    cp->angz = twp->ang.z;
    cp->spd = mwp->spd;
    func_8C028BFE(cp);
    cp->pos.x = FMAC_ADD(twp->pos.x + mwp->spd.x, cp->radius, up.x);
    cp->pos.y = FMAC_ADD(twp->pos.y + mwp->spd.y, cp->radius, up.y);
    cp->pos.z = FMAC_ADD(twp->pos.z + mwp->spd.z, cp->radius, up.z);
    for (n = B_8C754EF0, lc = B_8C754EF4; n > 0; n--, lc++) {
        if (func_8C029B00(cp, lc->pObject, lc->slAttribute) == 1) {
            if (lc->slAttribute & 0x201000) {
                return ret;
            }
            for (ti = B_8C6BACF0, k = cp->find_count; k > 0; k--, ti++) {
                if (ti->hit.normal2.y < -0.85999997f) {
                    if (!GetAnalog(twp, NULL, NULL)) {
                        q = &ti->hit.normal2.x;
                        p = &twp->pos.x;
                        SA1_FMAC_ACC(*p, *q, 0.5f);
                        p++;
                        q++;
                        SA1_FMAC_ACC(*p, *q, 0.5f);
                        p++;
                        q++;
                        SA1_FMAC_ACC(*p, *q, 0.5f);
                    }
                    return ret;
                }
                if (ti->hit.normal2.y < 0.85999997f && !(njInnerProduct(&up, &ti->hit.normal2) > 0.25899998f)) {
                    if (!GetAnalog(twp, NULL, NULL)) {
                        q = &ti->hit.normal2.x;
                        p = &twp->pos.x;
                        SA1_FMAC_ACC(*p, *q, 0.5f);
                        p++;
                        q++;
                        SA1_FMAC_ACC(*p, *q, 0.5f);
                        p++;
                        q++;
                        SA1_FMAC_ACC(*p, *q, 0.5f);
                    }
                    return ret;
                }
            }
        }
    }
    if (mwp->spd.y < -0.01f) {
        cp->pos.x = FMAC_ADD(twp->pos.x + mwp->spd.x, pwp->p.height * up.x, 0.25f);
        cp->pos.y = FMAC_ADD(twp->pos.y + mwp->spd.y, FMAC_SUB(up.y, 0.25f, 0.5f), pwp->p.height);
        cp->pos.z = FMAC_ADD(twp->pos.z + mwp->spd.z, pwp->p.height * up.z, 0.25f);
        for (n = B_8C754EF0, lc = B_8C754EF4; n > 0; n--, lc++) {
            if (func_8C029B00(cp, lc->pObject, lc->slAttribute) == 1) {
                for (ti = B_8C6BACF0, k = cp->find_count; k > 0; k--, ti++) {
                    if (!(ti->hit.normal2.y < 0.85999997f)) {
                        cp->normal2.x = up.x;
                        cp->normal2.z = up.z;
                        cp->normal2.y = ti->hit.normal2.y;
                        return ret;
                    }
                }
            }
        }
    }
    found = 0;
    PC_ONLY(bestlc = NULL;) /* only read when found == 1 */
    cp->pos.x = FMAC_ADD(twp->pos.x + mwp->spd.x, up.x, 0.29999999f);
    cp->pos.y = FMAC_ADD(twp->pos.y + mwp->spd.y, up.y, 0.29999999f);
    cp->pos.z = FMAC_ADD(twp->pos.z + mwp->spd.z, up.z, 0.29999999f);
    for (n = B_8C754EF0, lc = B_8C754EF4; n > 0; n--, lc++) {
        if (func_8C029B00(cp, lc->pObject, lc->slAttribute) == 1) {
            if (lc->slAttribute & 0x201000) {
                continue;
            }
            for (ti = B_8C6BACF0, k = cp->find_count; k > 0; k--, ti++) {
                if (ti->hit.normal2.y < -0.85999997f || ti->hit.normal2.y > 0.71f) {
                } else if (njInnerProduct(&up, &ti->hit.normal2) > 0.25899998f) {
                    if (found == 0) {
                        found = 1;
                        best = ti->hit;
                        bestlc = lc;
                    } else {
                        dir.x = ti->hit.onpoly.x - best.onpoly.x;
                        dir.y = ti->hit.onpoly.y - best.onpoly.y;
                        dir.z = ti->hit.onpoly.z - best.onpoly.z;
                        if (njScalor(&dir) < 0.0001f) {
                            v1.x = up.x - best.normal2.x;
                            v1.y = up.y - best.normal2.y;
                            v1.z = up.z - best.normal2.z;
                            v2.x = up.x - ti->hit.normal2.x;
                            v2.y = up.y - ti->hit.normal2.y;
                            v2.z = up.z - ti->hit.normal2.z;
                            if (njScalor2(&v1) < njScalor2(&v2)) {
                                best = ti->hit;
                                bestlc = lc;
                            }
                        } else if (!(njInnerProduct(&up, &dir) < 0.0f)) {
                            best = ti->hit;
                            bestlc = lc;
                        }
                    }
                }
            }
        }
    }
    if (found == 1) {
        dir.x = (up.x + best.normal2.x) * 0.5f;
        dir.y = (up.y + best.normal2.y) * 0.5f;
        dir.z = (up.z + best.normal2.z) * 0.5f;
        if (njScalor(&dir) == 0.0f) {
            dir.x = 1.0f;
            dir.y = dir.z = 0.0f;
        } else {
            njUnitVector(&dir);
        }
        angx = NJM_RAD_ANG(func_8C10D058(dir.z));
        angz = 0 - NJM_RAD_ANG(atan2f(dir.x, dir.y));
        v1.x = 1.0f;
        v1.y = v1.z = 0.0f;
        func_8C057B40();
        if (angz != 0) {
            njRotateZ(NULL, angz);
        }
        if (angx != 0) {
            njRotateX(NULL, angx);
        }
        njCalcVector(NULL, &v1, &v2);
        njPopMatrix(1);
        if (dir.y * dir.y < 0.0001f) {
            v3.x = v3.z = 0.0f;
            v3.y = 1.0f;
        } else {
            if (dir.y > 0.0f) {
                v3.x = -dir.x;
                v3.y = SA1_FMAC(dir.x, dir.x, dir.z * dir.z) / dir.y;
                v3.z = -dir.z;
            } else {
                v3.x = dir.x;
                v3.y = (-(dir.x * dir.x) - dir.z * dir.z) / dir.y;
                v3.z = dir.z;
            }
            njUnitVector(&v3);
        }
        if (_builtin_fabsf(njOuterProduct(&v3, &v2, &v4)) < 0.01f) {
            if (njInnerProduct(&v2, &v3) > 0.0f) {
                angy = 0;
            } else {
                angy = 0x8000;
            }
        } else {
            njUnitVector(&v4);
            if (!(njInnerProduct(&dir, &v4) < 0.0f)) {
                angy = NJM_RAD_ANG(func_8C10D008(njInnerProduct(&v2, &v3)));
            } else {
                angy = -NJM_RAD_ANG(func_8C10D008(njInnerProduct(&v2, &v3)));
            }
        }
        angx = (Uint16)angx;
        angy = (Uint16)angy;
        angz = (Uint16)angz;
        twp->ang.x = angx;
        twp->ang.y = angy;
        twp->ang.z = angz;
        mwp->ang_aim.x = twp->ang.x;
        mwp->ang_aim.z = twp->ang.z;
        cp->pos = best.onpoly;
        tp = bestlc->pTask;
        if (tp != NULL) {
            njAddVector(&cp->pos, &((forcewk *)tp->fwp)->pos_spd);
            twp->ang.x += ((forcewk *)tp->fwp)->ang_spd.x;
            twp->ang.y += ((forcewk *)tp->fwp)->ang_spd.y;
            twp->ang.z += ((forcewk *)tp->fwp)->ang_spd.z;
        }
        pwp->ttp = bestlc->pTask;
        if (njDistanceP2P(&twp->pos, &cp->pos) > 0.29999999f) {
            twp->pos = cp->pos;
        } else {
            pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
            mwp->spd.x = mwp->spd.y = mwp->spd.z = 0.0f;
        }
        cp->spd.x = cp->spd.y = cp->spd.z = 0.0f;
        twp->flag |= 1;
        pwp->lclop = bestlc->pObject;
    } else if (mwp->spd.y > 0.01f) {
        cp->pos.x = twp->pos.x + mwp->spd.x - pwp->p.height * up.x * 0.25f;
        cp->pos.y = FMAC_ADD(twp->pos.y + mwp->spd.y, SA1_FMAC(up.y, 0.25f, 0.5f), pwp->p.height);
        cp->pos.z = twp->pos.z + mwp->spd.z - pwp->p.height * up.z * 0.25f;
        for (n = B_8C754EF0, lc = B_8C754EF4; n > 0; n--, lc++) {
            if (func_8C029B00(cp, lc->pObject, lc->slAttribute) == 1) {
                for (ti = B_8C6BACF0, k = cp->find_count; k > 0; k--, ti++) {
                    if (!(ti->hit.normal2.y < 0.85999997f)) {
                        cp->normal2.x = up.x;
                        cp->normal2.z = up.z;
                        cp->normal2.y = ti->hit.normal2.y;
                        return ret;
                    }
                }
            }
        }
    }
    return ret;
}
#else
ASM_STUB(Sint32 func_8C076782(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
