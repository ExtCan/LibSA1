/*
 * Player common (1ST_READ 8C0777E0-8C078308): land collision core used on
 * stages 0x902/0x903 (func_8C0730D8 dispatches here; every other stage uses
 * func_8C073160 in code_8C0730D8.c).
 *
 *  func_8C0777E0  same structure as func_8C073160: the collision sphere
 *                 (cstsp) is put at the player position plus half the
 *                 height along the player's up vector (built here with the
 *                 twp->ang rotation matrix instead of PConvertVector_P2G),
 *                 tested against every gathered land object (B_8C754EF4),
 *                 the hits are sorted (the one 8C075686/8C075860 accepts
 *                 first, the object stood on last frame last) and resolved:
 *                 landing (8C075F8C, slope angles from the polygon normal,
 *                 sphere pushed onto the polygon plane, moving land objects
 *                 8C07506E) or wall handling (8C074AFC). Differences from
 *                 8C073160: every candidate of the gather is kept (even
 *                 without a touch), objects with attribute 0x200000 are
 *                 floors only in act 3 when nearly flat (normal.y > 0.99),
 *                 a push-back longer than 4.1 is cut to 4.1 and the rest is
 *                 carried into cp->spd while the hit swaps places with the
 *                 last list entry and the list is walked again, and there is
 *                 no 0x4000 (loop/ceiling) handling.
 *                 Returns the landing/wall status (0 = none).
 *
 * NON_MATCHING: 1241 of 1273 instructions match (shcdiff, 32 differ). What
 * is left are allocation tie-breaks that no source change tried so far
 * moved: (1) the prologue schedules `MOV R10,R8` (ret = 0) after the copy of
 * mwp to R11 instead of before it; (2) landed and lastobj get stack slots
 * 0x24/0x20 instead of 0x20/0x24; (3) the two float constants hoisted out of
 * the second loop are swapped (10430.381 in FR12 and 4.1 in FR13 instead of
 * FR13/FR12); (4) R2/R3 are swapped in the second func_8C074AFC call and in
 * the loop increment. Declaration order, statement order, names and
 * TU-context (a dummy function before this one) were tried without effect.
 * Codegen notes kept in the C below: the dead `dst = sorted` before the
 * found test makes SHC keep &sorted in R6 through the sort; `list` is the
 * pointer the second loop indexes (spilled at 0x10); the stood-on slot is
 * written as `sorted[n - 1]` (a CSE temporary, spilled at 0x08) - a `last`
 * variable is allocated differently; the hit copies go through `src`.
 *
 * SA1_GA (libsa1's lowered game code only): the same PC-only capacity as
 * func_8C073160 in code_8C0730D8.c (its own pcdata block, magic "HTCP"
 * "77E0"; sadc docs/research/gsa1_090_collision.md F1b): hits / sorted hold
 * SA1_HITCAP_MAX objects and the collection stops at that size.
 */
#define func_8C029B00 func_8C029B00_hdr
#include "sa1/sa1.h"
#undef func_8C029B00

/* one touch of the sphere test (B_8C6BACF0[]; same layout as TOUCHINFO in
 * code_8C029B00.c, which fills it; csts.selected_nmb indexes it) */
typedef struct {
    Angle angx;                 /* 0x00 */
    Angle angz;                 /* 0x04 */
    NJS_VECTOR normal;          /* 0x08 */
    NJS_VECTOR normal2;         /* 0x14 */
    NJS_POINT3 onpoly;          /* 0x20 read here */
    NJS_POINT3 pshbk;           /* 0x2C */
    NJS_VECTOR anaspdh;         /* 0x38 */
    NJS_VECTOR anaspdv;         /* 0x44 */
    Float dist;                 /* 0x50 */
    Float dist2;                /* 0x54 */
} TOUCHINFO;                    /* 0x58 */

SA1_ASSERT_SIZE(TOUCHINFO, 0x58);
SA1_ASSERT_OFFSET(TOUCHINFO, onpoly, 0x20);

extern TOUCHINFO B_8C6BACF0[];          /* touches (code_8C029B00.c) */

/* every call site passes (csts *, _OBJ_LANDCOLL.pObject, .slAttribute) */
Sint32 func_8C029B00(csts *cp, NJS_OBJECT *obj, Uint32 attr);

void func_8C074CF0(motionwk *mwp, playerwk *pwp, csts *cp, NJS_POINT3 *v);
void func_8C074EE4(taskwk *twp, playerwk *pwp, csts *cp);
void func_8C07506E(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *ofs);
void func_8C075214(taskwk *twp, motionwk *mwp, playerwk *pwp, Uint32 attr);
Sint32 func_8C075686(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp, Uint32 attr);
Sint32 func_8C075860(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp);
Sint32 func_8C074AFC(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp,
                     NJS_POINT3 *pos, NJS_POINT3 *ofs, Sint32 ret, Uint32 attr);

/* The call here pushes a fifth argument (the object's attributes) that
 * func_8C075F8C never reads; code_8C075F8C.c defines it with four. */
#ifdef TARGET_PC
Sint32 func_8C075F8C(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp);
#define LAND_8C075F8C(twp, mwp, pwp, cp, attr) func_8C075F8C(twp, mwp, pwp, cp)
#else
Sint32 func_8C075F8C(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp, Uint32 attr);
#define LAND_8C075F8C(twp, mwp, pwp, cp, attr) func_8C075F8C(twp, mwp, pwp, cp, attr)
#endif

#if defined(SA1_GA)
#define SA1_HITCAP_MAGIC0 0x50435448    /* "HTCP" (sa1_coladapt_world.c CW_HITCAP_*) */
#define SA1_HITCAP_MAGIC1 0x30453737    /* "77E0" (code_8C0730D8.c: "7160") */
#define SA1_HITCAP_MAX 192
typedef struct SA1_HITCAP {
    Uint32 magic[2];                        /* 0x00 SA1_HITCAP_MAGIC0, SA1_HITCAP_MAGIC1 */
    Sint32 cap;                             /* 0x08 SA1_HITCAP_MAX */
    Sint32 peak;                            /* 0x0C the most objects one call collected (before the bound) since
                                               the adapter cleared it */
    Sint32 dropped;                         /* 0x10 objects left out of a call: the arrays were full */
    Sint32 calls;                           /* 0x14 calls */
    _OBJ_LANDCOLL hits[SA1_HITCAP_MAX];     /* 0x18 */
    _OBJ_LANDCOLL sorted[SA1_HITCAP_MAX];   /* 0x918 */
    Uint32 canary;                          /* 0x1218 written by the adapter, never by the game code */
} SA1_HITCAP;
SA1_ASSERT_OFFSET(SA1_HITCAP, hits, 0x18);
SA1_ASSERT_OFFSET(SA1_HITCAP, sorted, 0x918);
SA1_ASSERT_OFFSET(SA1_HITCAP, canary, 0x1218);
static SA1_HITCAP sa1_hitcap = { { SA1_HITCAP_MAGIC0, SA1_HITCAP_MAGIC1 }, SA1_HITCAP_MAX, 0, 0, 0 };
#endif

#ifdef NON_MATCHING
#define NJM_RAD_ANG(n) SA1_FTOI((n) * 10430.381f)

Sint32 func_8C0777E0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 ofs;
    NJS_POINT3 cofs;
    NJS_POINT3 v;
    NJS_POINT3 oldpos;
#if defined(SA1_GA)
    _OBJ_LANDCOLL *hits;
    _OBJ_LANDCOLL *sorted;
    Sint32 sa1_all;
#else
    _OBJ_LANDCOLL hits[16];
    _OBJ_LANDCOLL sorted[16];
#endif
    NJS_LINE line;
    NJS_POINT3 cpos;
    _OBJ_LANDCOLL tmp;
    Sint32 landed;
    NJS_OBJECT *lastobj;
    Float mag;
    csts *cp;
    _OBJ_LANDCOLL *lc;
    _OBJ_LANDCOLL *dst;
    _OBJ_LANDCOLL *list;
    _OBJ_LANDCOLL *src;
    TOUCHINFO *ti;
    task *tp;
    Sint32 ret;
    Sint32 i;
    Sint32 j;
    Sint32 k;
    Sint32 n;
    Sint32 found;
    Sint32 foundidx;
    Sint32 oldflag;
    Sint32 ang;
    Sint32 r;
    Float s;
    Float d;

#if defined(SA1_GA)
    hits = sa1_hitcap.hits;
    sorted = sa1_hitcap.sorted;
    sa1_all = 0;
#endif
    cp = pwp->cstsp;
    oldflag = pwp->flag;
    ret = 0;
    pwp->flag = twp->flag;
    twp->flag &= ~1;
    func_8C057B40();
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
    v.x = v.z = 0.0f;
    v.y = 1.0f;
    cp->angx = twp->ang.x;
    cp->angz = twp->ang.z;
    njCalcVector(NULL, &v, &cp->tnorm);
    v.x = v.z = 0.0f;
    v.y = pwp->p.height * 0.5f;
    njCalcVector(NULL, &v, &ofs);
    func_8C074CF0(mwp, pwp, cp, &v);
    njCalcVector(NULL, &v, &cofs);
    njPopMatrix(1);
    cp->pos.x = twp->pos.x + cofs.x;
    cp->pos.y = twp->pos.y + cofs.y;
    cp->pos.z = twp->pos.z + cofs.z;
    cp->radius = pwp->p.height * 0.5f;
    cp->spd = mwp->spd;
    oldpos = twp->pos;
    func_8C028BFE(cp);
    if (B_8C754EF0 == 0) {
        twp->pos.x += mwp->spd.x;
        twp->pos.y += mwp->spd.y;
        twp->pos.z += mwp->spd.z;
        func_8C075214(twp, mwp, pwp, 0);
        pwp->ttp = NULL;
        pwp->lclop = NULL;
        return 0;
    }

    /* collect the gathered land objects (all of them, touched or not) */
    landed = 0;
    lc = B_8C754EF4;
    lastobj = NULL;
    found = 0;
    foundidx = 0;
    n = 0;
    for (i = 0; i < B_8C754EF0; i++, lc++) {
        if ((lc->slAttribute & 0x400002) && (Uint8)twp->counter.b[1] != SA1_CHR_E102) {
            continue;
        }
        tp = lc->pTask;
        if (tp != NULL && !(tp->twp->flag & 0x100)) {
            continue;
        }
#if defined(SA1_GA)
        if (++sa1_all > SA1_HITCAP_MAX) {
            sa1_hitcap.dropped++;   /* the arrays are full: left out of this call */
            continue;
        }
#endif
        if (func_8C029B00(cp, lc->pObject, lc->slAttribute)) {
            if (pwp->flag & 3) {
                if (func_8C075686(twp, mwp, pwp, cp, lc->slAttribute) == 1) {
                    found = 1;
                    foundidx = n;
                }
            } else {
                if (func_8C075860(twp, mwp, pwp, cp) == 1) {
                    found = 1;
                    foundidx = n;
                }
            }
        }
        hits[n] = *lc;
        n++;
    }
#if defined(SA1_GA)
    sa1_hitcap.calls++;
    if (sa1_all > sa1_hitcap.peak) {
        sa1_hitcap.peak = sa1_all;
    }
#endif
    if (n == 0) {
        twp->pos.x += mwp->spd.x;
        twp->pos.y += mwp->spd.y;
        twp->pos.z += mwp->spd.z;
        func_8C075214(twp, mwp, pwp, 0);
        pwp->ttp = NULL;
        pwp->lclop = NULL;
        return 0;
    }

    /* sort: the accepted hit first, the object stood on last frame last */
    j = 0;
    dst = sorted;
    if (found == 1) {
        sorted[0] = hits[foundidx];
        j++;
    }
    if (pwp->ttp != NULL) {
        for (k = 0, dst = &sorted[j]; k < n; k++) {
            if (found == 1 && k == foundidx) {
                found = 0;
            } else {
                src = &hits[k];
                if (pwp->lclop == src->pObject) {
                    sorted[n - 1] = *src;
                } else {
                    *dst = *src;
                    dst++;
                }
            }
        }
    } else {
        for (k = n - 1, dst = &sorted[j]; k >= 0; k--) {
            if (found == 1 && k == foundidx) {
                found = 0;
            } else {
                src = &hits[k];
                if (pwp->lclop == src->pObject) {
                    sorted[n - 1] = *src;
                } else {
                    *dst = *src;
                    dst++;
                }
            }
        }
    }

    cp->pos.x = twp->pos.x + cofs.x;
    cp->pos.y = twp->pos.y + cofs.y;
    cp->pos.z = twp->pos.z + cofs.z;
    cp->spd = mwp->spd;
    pwp->ttp = NULL;
    list = sorted;
    for (i = 0; i < n; i++) {
        lc = &list[i];
        tp = lc->pTask;
        if (!func_8C029B00(cp, lc->pObject, lc->slAttribute)) {
            continue;
        }
        ang = VectorAngle(&cp->tnorm, &cp->normal2, NULL);
        if (!(twp->flag & 2)
            && (!(lc->slAttribute & 0x200040)
                || ((lc->slAttribute & 0x200000) && (Uint8)GetStageNumber() == 3
                    && 0.98999996f < cp->normal2.y))
            && (ang <= 0x2C00 || cp->tnorm.y < cp->normal2.y)
            && (!(oldflag & 3) || (pwp->flag & 3))
            && (njInnerProduct(&Gravity, &cp->normal2) <= 0.0f || (pwp->flag & 3))) {
            if (pwp->spd.y <= 0.0f || (pwp->spd.y > 0.0f && !(pwp->flag & 3))) {
                /* landing */
                landed = 1;
                lastobj = lc->pObject;
                r = LAND_8C075F8C(twp, mwp, pwp, cp, lc->slAttribute);
                if (r) {
                    if (ret == 0) {
                        ret = r;
                    } else if (r == 2) {
                        ret = r;
                    }
                }
                cp->angz = -NJM_RAD_ANG(atan2f(cp->normal2.x, cp->normal2.y));
                cp->angx = NJM_RAD_ANG(func_8C10D058(cp->normal2.z));
                if (mwp->spd.x != 0.0f || mwp->spd.z != 0.0f || !(pwp->flag & 1)
                    || njInnerProduct(&Gravity, &cp->normal2) < njInnerProduct(&Gravity, &cp->tnorm)) {
                    func_8C074EE4(twp, pwp, cp);
                }
                if (ret == 0) {
                    func_8C057B40();
                    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
                    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
                    -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
                    v.x = v.z = 0.0f;
                    v.y = 1.0f;
                    cp->angx = twp->ang.x;
                    cp->angz = twp->ang.z;
                    njCalcVector(NULL, &v, &cp->tnorm);
                    v.x = v.z = 0.0f;
                    v.y = pwp->p.height * 0.5f;
                    njCalcVector(NULL, &v, &ofs);
                    func_8C074CF0(mwp, pwp, cp, &v);
                    njCalcVector(NULL, &v, &cofs);
                    njPopMatrix(1);
                }
                mwp->ang_aim.x = cp->angx;
                mwp->ang_aim.z = cp->angz;
                pwp->floor_normal = cp->normal2;
                if (njScalor(&cp->anaspdh) > 4.1f) {
                    /* keep 4.1 of the push-back, carry the rest over */
                    njUnitVector(&cp->anaspdh);
                    s = njScalor(&cp->spd) - 4.1f;
                    cp->spd.x = cp->anaspdh.x * s;
                    cp->spd.y = cp->anaspdh.y * s;
                    cp->spd.z = cp->anaspdh.z * s;
                    s = 4.1f;
                    cp->anaspdh.x *= s;
                    cp->anaspdh.y *= s;
                    cp->anaspdh.z *= s;
                    tmp = list[n - 1];
                    list[n - 1] = *lc;
                    *lc = tmp;
                    i = -1;
                } else {
                    cp->spd.x = cp->spd.y = cp->spd.z = 0.0f;
                }
                cp->pos.x = cp->pshbk.x + cp->anaspdh.x;
                cp->pos.y = cp->pshbk.y + cp->anaspdh.y;
                cp->pos.z = cp->pshbk.z + cp->anaspdh.z;
                ti = &B_8C6BACF0[cp->selected_nmb];
                line.vx = cp->tnorm.x;
                line.vy = cp->tnorm.y;
                line.vz = cp->tnorm.z;
                line.px = ti->onpoly.x;
                line.py = ti->onpoly.y;
                line.pz = ti->onpoly.z;
                njDistanceP2L(&cp->pos, &line, &cpos);
                cp->pos.x = ofs.x + cpos.x;
                cp->pos.y = ofs.y + cpos.y;
                cp->pos.z = ofs.z + cpos.z;
                if (tp != NULL) {
                    pwp->ttp = tp;
                    func_8C07506E(twp, pwp, cp, &ofs);
                } else {
                    pwp->ttp = NULL;
                }
                v.x = cp->pos.x - ofs.x;
                v.y = cp->pos.y - ofs.y;
                v.z = cp->pos.z - ofs.z;
                d = njDistanceP2P(&twp->pos, &v);
                if ((tp != NULL || pwp->spd.x != 0.0f || pwp->spd.z != 0.0f || d > 1.0f)
                    && d > 0.0001f) {
                    twp->pos = v;
                }
                twp->flag |= 1;
                func_8C075214(twp, mwp, pwp, lc->slAttribute);
            } else {
                ret = func_8C074AFC(twp, mwp, pwp, cp, &oldpos, &ofs, ret, lc->slAttribute);
            }
        } else {
            ret = func_8C074AFC(twp, mwp, pwp, cp, &oldpos, &ofs, ret, lc->slAttribute);
        }
    }

    if (landed != 1) {
        mag = 0.0f;
        if ((pwp->flag & 1) && !(pwp->flag & 0x4000)
            && (!GetAnalog(twp, NULL, &mag) || (mag <= 0.89999998f && pwp->nocontimer == 0))
            && pwp->spd.y <= 0.0f && pwp->spd.x <= pwp->p.run_speed
            && pwp->dotp > 0.69999999f) {
            twp->pos.x -= mwp->spd.x * 0.5f;
            twp->pos.y -= mwp->spd.y * 0.5f;
            twp->pos.z -= mwp->spd.z * 0.5f;
            mwp->spd.x = 0.0f;
            mwp->spd.y = 0.0f;
            mwp->spd.z = 0.0f;
        } else {
            twp->pos.x += cp->spd.x;
            twp->pos.y += cp->spd.y;
            twp->pos.z += cp->spd.z;
        }
        func_8C075214(twp, mwp, pwp, 0);
    } else {
        pwp->lclop = lastobj;
        twp->pos.x += cp->spd.x;
        twp->pos.y += cp->spd.y;
        twp->pos.z += cp->spd.z;
    }
    return ret;
}
#endif
