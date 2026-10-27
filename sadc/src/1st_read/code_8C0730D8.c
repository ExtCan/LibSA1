/*
 * Player common (1ST_READ 8C0730D8-8C0742D4): the land collision core of
 * PSetPosition (8C072B44, which calls func_8C0730D8 with BSR and uses the
 * returned status).
 *
 *  func_8C0730D8  dispatcher: stage 0x902/0x903 (GetStageNumber, Uint16)
 *                 uses 8C0777E0, every other stage the land collision below
 *                 (tail call by BRA).
 *  func_8C073160  land collision. The collision sphere (pwp->cstsp, radius
 *                 half the height) is put at the player position plus half
 *                 the height along the player's up vector (along the floor
 *                 normal when last_attr & 0x4000) and swept by mwp->spd
 *                 against every gathered land object (B_8C754EF4, count
 *                 B_8C754EF0) with 8C029B00. Pass 1 collects every object
 *                 (skipping water 0x400002 unless E-102, and dynamic objects
 *                 whose task lacks flag 0x100), touched or not: the sphere
 *                 test only decides whether 8C075416 (on the ground) or
 *                 8C075860 (in the air) is asked to accept it. The accepted
 *                 one goes first, the
 *                 object stood on last frame (pwp->lclop) last, the others
 *                 in collection order (reversed when not riding a moving
 *                 object). Pass 2 re-tests each in that order: a wall (steep
 *                 normal, too fast into it, ...) goes to 8C0747D2; a landing
 *                 runs 8C075A40, sets the slope angles from the polygon
 *                 normal (cp->angx/angz -> mwp->ang_aim), projects the
 *                 sphere back onto the touched polygon (njDistanceP2L along
 *                 tnorm), follows moving land (8C07506E) and moves twp->pos.
 *                 With no landing, the player is moved by the push-back
 *                 speed (or half a step back when standing still on flat
 *                 ground without input). Returns the status of
 *                 8C075A40/8C0747D2 (0 = nothing).
 *
 * The code at 8C073160 has no symbol in config/symbols.1st_read.txt (it is
 * only reached by func_8C0730D8's BRA), so tools/shcdiff.py and the splicer
 * see 8C0730D8-8C0742D4 as one function. Name and prototype are ours; a
 * symbol "func_8C073160 = 0x8C073160" in the symbol config would split it.
 *
 * NON_MATCHING. With that split simulated (tools/shcdiff.compare against a
 * manifest with 8C073160 added):
 *  - func_8C0730D8 matches exactly (35/35 instructions);
 *  - func_8C073160 has the same frame (0x230), callee-saved register and
 *    constant registers (R8 = 1, R9 = 0, FR12 = 0.5, FR13 = 1, FR14 =
 *    10430.381, FR15 = 0), literal pools and control flow, but SHC assigns
 *    the spilled scalars to different stack slots (the original shares
 *    slots e.g. lc/i, foundidx/flags/angz2, i1/last/tp), hoists
 *    &sorted[last] above the ttp test in the sort (the original computes it
 *    in each branch) and cross-jumps the first 8C0747D2 call into the outer
 *    else instead of the inner one; 1999 vs 2006 instructions.
 * Source idioms that the original needs (verified): the dead "zero != 0"
 * test (TST R9,R9 on the cached zero register), the comma expression for the
 * 0.52 wall test (the two 8C0747D2 calls cross-jumped), separate temps for
 * pwp->spd.x (spilled), and the landing-block locals (line, cpos, tmp).
 *
 * SA1_GA (libsa1's lowered game code only; not the SHC build, the harness's
 * SHC / TARGET_PC builds or their traces): a PC-only capacity like
 * SA1_NEARCAP in code_8C028BDC.c (sadc docs/research/gsa1_090_collision.md
 * C3 / F1b with its Review R.4.1). Pass 1 copies every gathered land object
 * into hits[16] / sorted[16] without a bound check. The game's stages never
 * gather more than 16 for one query; host worlds can (GRID objects of one
 * 256-unit cell all overlap the character's query sphere), and the copy then
 * overwrote the locals behind the arrays (a host crash in njDistanceP2L on
 * rp_downtown_v4c). Under SA1_GA the arrays hold SA1_HITCAP_MAX (192: the
 * land object cache of 8C028EC2 hands out at most 191 objects, so a gather
 * cannot fill them) in a pcdata block (segment 0x18 in libsa1: one per
 * world) that the host world's collision adapter (LIB
 * src/world/sa1_coladapt_world.c) finds by its magic words, and pass 1 stops
 * at that size (an object beyond it is left out of the call and counted).
 * The adapter reads the block after every step (the most objects one call
 * collected, the objects left out) and checks a canary word after the
 * arrays. With at most 16 objects per call, the only case the game itself
 * makes, the function does what the original does.
 */
#define func_8C029B00 func_8C029B00_hdr
#define func_8C0730D8 func_8C0730D8_hdr
#include "sa1/sa1.h"
#undef func_8C029B00
#undef func_8C0730D8

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
extern Sint32 D_8C1BE84C;               /* set while a hit object has attribute 0x4000 */
extern Sint32 D_8C1BDFE0;               /* cleared every frame by func_8C06C6F8 */

/* every call site passes (csts *, _OBJ_LANDCOLL.pObject, .slAttribute) */
Sint32 func_8C029B00(csts *cp, NJS_OBJECT *obj, Uint32 attr);

Sint32 func_8C0777E0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C079000(NJS_POINT3 *nrm, NJS_POINT3 *v, Angle angy);
void func_8C074CF0(motionwk *mwp, playerwk *pwp, csts *cp, NJS_POINT3 *v);
Sint32 func_8C075416(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp, Uint32 attr);
Sint32 func_8C075860(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp);
void func_8C075214(taskwk *twp, motionwk *mwp, playerwk *pwp, Uint32 attr);
Sint32 func_8C075A40(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp, Uint32 attr);
void func_8C074EE4(taskwk *twp, playerwk *pwp, csts *cp);
void func_8C07506E(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *ofs);
void func_8C0751C0(taskwk *twp, motionwk *mwp, csts *cp);

Sint32 func_8C073160(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0742D4(taskwk *twp, csts *cp, _OBJ_LANDCOLL *lc);
Sint32 func_8C0747D2(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp,
                     NJS_POINT3 *pos, NJS_POINT3 *ofs, Sint32 ret, Uint32 attr);

#define NJM_RAD_ANG(n) SA1_FTOI((n) * 10430.381f)

#if defined(SA1_GA)
#define SA1_HITCAP_MAGIC0 0x50435448    /* "HTCP" (sa1_coladapt_world.c CW_HITCAP_*) */
#define SA1_HITCAP_MAGIC1 0x30363137    /* "7160" (code_8C0777E0.c: "77E0") */
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
Sint32 func_8C0730D8(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if ((Uint16)GetStageNumber() == 0x902 || (Uint16)GetStageNumber() == 0x903) {
        return func_8C0777E0(twp, mwp, pwp);
    }
    return func_8C073160(twp, mwp, pwp);
}

Sint32 func_8C073160(taskwk *twp, motionwk *mwp, playerwk *pwp)
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
    csts *cp;
    _OBJ_LANDCOLL *lc;
    _OBJ_LANDCOLL *dst;
    _OBJ_LANDCOLL *p;
    _OBJ_LANDCOLL *list;
    task *tp;
    NJS_OBJECT *lastobj;
    Sint32 ret;
    Sint32 i;
    Sint32 k;
    Sint32 j;
    Sint32 n;
    Sint32 last;
    Sint32 found;
    Sint32 foundidx;
    Sint32 flags;
    Sint32 ang;
    Sint32 oldflag;
    Sint32 wasair;
    Sint32 landed;
    Sint32 r;
    Sint32 angx;
    Sint32 angz;
    Sint32 angx2;
    Sint32 angz2;
    Sint32 diffx;
    Sint32 diffz;
    Sint32 zero = 0;            /* never changed: the anaspdh clamp is dead code */
    Float dist;
    Float ip;
    Float s;
    Float scl;
    Float t1;                   /* pwp->spd.x copies (spilled in the original) */
    Float t2;
    Float mag;

#if defined(SA1_GA)
    hits = sa1_hitcap.hits;
    sorted = sa1_hitcap.sorted;
    sa1_all = 0;
#endif
    cp = pwp->cstsp;
    ret = 0;
    D_8C1BE84C = 0;
    oldflag = pwp->flag;
    pwp->flag = twp->flag;
    twp->flag &= ~1;
    cp->tnorm.x = cp->tnorm.z = 0.0f;
    cp->tnorm.y = 1.0f;
    cp->angx = twp->ang.x;
    cp->angz = twp->ang.z;
    ofs.x = ofs.z = 0.0f;
    ofs.y = pwp->p.height * 0.5f;
    cofs = ofs;
    if (pwp->last_attr & 0x4000) {
        func_8C079000(&pwp->floor_normal, &cp->tnorm, twp->ang.y);
        func_8C079000(&pwp->floor_normal, &ofs, twp->ang.y);
        func_8C074CF0(mwp, pwp, cp, &cofs);
        func_8C079000(&pwp->floor_normal, &cofs, twp->ang.y);
    } else {
        PConvertVector_P2G(twp, &cp->tnorm);
        PConvertVector_P2G(twp, &ofs);
        func_8C074CF0(mwp, pwp, cp, &cofs);
        PConvertVector_P2G(twp, &cofs);
    }
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

    /* collect the hit land objects */
    landed = 0;
    found = 0;
    lc = B_8C754EF4;
    lastobj = NULL;
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
                if (func_8C075416(twp, mwp, pwp, cp, lc->slAttribute) == 1) {
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
        /* every object that passed the filters is collected, touched or
         * not (TST R0,R0 / BT L8C0733F6): the sphere test only gates the
         * accept test above */
        hits[n] = *lc;
        n++;
        if (lc->slAttribute & 0x4000) {
            D_8C1BE84C = 1;
        }
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
    if (func_8C049A4E()) {
        D_8C1BE84C = 0;
    }

    /* sort: the accepted hit first, the object stood on last frame last */
    j = 0;
    if (found == 1) {
        sorted[0] = hits[foundidx];
        j++;
    }
    dst = &sorted[j];
    last = n - 1;
    if (pwp->ttp != NULL) {
        for (k = 0; k < n; k++) {
            if (found == 1 && foundidx == k) {
                found = 0;
            } else {
                p = &hits[k];
                if (pwp->lclop == p->pObject) {
                    sorted[last] = *p;
                } else {
                    *dst++ = *p;
                }
            }
        }
    } else {
        for (k = last; k >= 0; k--) {
            if (found == 1 && foundidx == k) {
                found = 0;
            } else {
                p = &hits[k];
                if (pwp->lclop == p->pObject) {
                    sorted[last] = *p;
                } else {
                    *dst++ = *p;
                }
            }
        }
    }
    list = sorted;

    cp->pos.x = twp->pos.x + cofs.x;
    cp->pos.y = twp->pos.y + cofs.y;
    cp->pos.z = twp->pos.z + cofs.z;
    cp->spd = mwp->spd;
    pwp->ttp = NULL;
    wasair = !(oldflag & 3);
    for (i = 0; i < n; i++) {
        lc = &list[i];
        tp = lc->pTask;
        if (!func_8C029B00(cp, lc->pObject, lc->slAttribute)) {
            continue;
        }
        flags = 1;
        if ((twp->flag & 1) && lastobj != lc->pObject) {
            if (D_8C1BE84C == 1) {
                v.x = cp->onpoly.x + cp->anaspdh.x - twp->pos.x;
                v.y = cp->onpoly.y + cp->anaspdh.y - twp->pos.y;
                v.z = cp->onpoly.z + cp->anaspdh.z - twp->pos.z;
            } else {
                v.x = cp->pshbk.x + cp->anaspdh.x - ofs.x - twp->pos.x;
                v.y = cp->pshbk.y + cp->anaspdh.y - ofs.y - twp->pos.y;
                v.z = cp->pshbk.z + cp->anaspdh.z - ofs.z - twp->pos.z;
            }
            dist = njScalor(&v);
            if (dist != 0.0f) {
                njUnitVector(&v);
                ip = njInnerProduct(&v, &cp->normal2);
                if (ip < -0.19999999f || dist > pwp->p.height) {
                    flags = 0;
                }
            }
        }
        flags |= 4;
        if ((twp->flag & 3) && !(twp->flag & 0x2000)) {
            NJS_POINT3 dir;
            NJS_POINT3 side;

            dir.x = cp->onpoly.x + cp->anaspdh.x - cp->pos.x;
            if (D_8C1BE84C) {
                dir.y = 0.0f;
            } else {
                dir.y = cp->onpoly.y + cp->anaspdh.y - cp->pos.y;
            }
            dir.z = cp->onpoly.z + cp->anaspdh.z - cp->pos.z;
            if (njScalor(&dir) != 0.0f) {
                njUnitVector(&dir);
                if (!(pwp->spd.x < 0.0f)) {
                    side.x = 1.0f;
                } else {
                    side.x = -1.0f;
                }
                side.y = 0.0f;
                side.z = 0.0f;
                PConvertVector_P2G(twp, &side);
                if (!D_8C1BE84C || _builtin_fabsf(side.y) != 1.0f) {
                    njUnitVector(&side);
                    if (njInnerProduct(&dir, &side) < -0.39999998f) {
                        flags &= ~4;
                    }
                }
            }
        }
        ang = VectorAngle(&cp->tnorm, &cp->normal2, NULL);
        if (ang > 0x2C00) {
            func_8C0742D4(twp, cp, lc);
            ang = VectorAngle(&cp->tnorm, &cp->normal2, NULL);
            flags |= 2;
        }
        if (!(twp->flag & 2) && !(lc->slAttribute & 0x200040)
            && ((ang <= 0x2C00 && (flags & 5) == 5)
                || njInnerProduct(&Gravity, &cp->tnorm) > njInnerProduct(&Gravity, &cp->normal2)
                || ((lc->slAttribute & 0x4000) && (flags & 5) == 5))
            && (wasair || (pwp->flag & 3))
            && (!(njInnerProduct(&Gravity, &cp->normal2) > 0.0f) || (pwp->flag & 3))
            && ((flags & 2) || func_8C0742D4(twp, cp, lc))
            && !((Uint8)twp->counter.b[1] == SA1_CHR_BIG && twp->mode == 48)) {
            if (!(njScalor(&mwp->spd) < pwp->p.run_speed) && !(lc->slAttribute & 0x4000)
                && (v = mwp->spd, njUnitVector(&v), njInnerProduct(&v, &cp->normal2) > 0.52f)) {
                ret = func_8C0747D2(twp, mwp, pwp, cp, &oldpos, &ofs, ret, lc->slAttribute);
                continue;
            }
            if (!(pwp->spd.y > 0.0f) || (pwp->spd.y > 0.0f && !(pwp->flag & 3))) {
                NJS_LINE line;
                NJS_POINT3 cpos;
                _OBJ_LANDCOLL tmp;
                TOUCHINFO *ti;
                Float d;

                /* landing */
                landed = 1;
                lastobj = lc->pObject;
                r = func_8C075A40(twp, mwp, pwp, cp, lc->slAttribute);
                if (r) {
                    if (ret == 0) {
                        ret = r;
                    } else if (r == 2) {
                        ret = r;
                    }
                }
                if (lc->slAttribute & 0x4000) {
                    twp->ang.x = twp->ang.z = 0;
                } else if ((lc->slAttribute & 0x2001C) || (pwp->flag & 0x2000)) {
                    cp->angz = -NJM_RAD_ANG(atan2f(cp->normal2.x, cp->normal2.y));
                    cp->angx = NJM_RAD_ANG(func_8C10D058(cp->normal2.z));
                    if (mwp->spd.x != 0.0f || mwp->spd.z != 0.0f || !(pwp->flag & 1)
                        || njInnerProduct(&Gravity, &cp->normal2) < njInnerProduct(&Gravity, &cp->tnorm)) {
                        func_8C074EE4(twp, pwp, cp);
                    }
                } else {
                    cp->angx = NJM_RAD_ANG(func_8C10D058(cp->normal2.z));
                    if (!(cp->normal2.y < 0.0f)) {
                        cp->angz = -NJM_RAD_ANG(func_8C10D058(cp->normal2.x));
                    } else {
                        cp->angz = NJM_RAD_ANG(func_8C10D058(cp->normal2.x)) + 0x8000;
                    }
                    if (mwp->spd.x != 0.0f || mwp->spd.z != 0.0f || !(pwp->flag & 1)
                        || njInnerProduct(&Gravity, &cp->normal2) < njInnerProduct(&Gravity, &cp->tnorm)) {
                        angx = NJM_RAD_ANG(func_8C10D058(cp->normal2.z));
                        angz = -NJM_RAD_ANG(func_8C10D058(cp->normal2.x));
                        if (cp->normal2.y < 0.0f) {
                            angx2 = 0x8000 - angx;
                            angz2 = 0x8000 - angz;
                            diffx = DiffAngle(twp->ang.x, angx2);
                            diffz = DiffAngle(twp->ang.z, angz2);
                            if (diffx < diffz) {
                                cp->angx = angx2;
                                cp->angz = angz;
                            } else {
                                cp->angx = angx;
                                cp->angz = angz2;
                            }
                        }
                        func_8C074EE4(twp, pwp, cp);
                    }
                }
                if (ret == 0) {
                    cp->tnorm.x = cp->tnorm.z = 0.0f;
                    cp->tnorm.y = 1.0f;
                    cp->angx = twp->ang.x;
                    cp->angz = twp->ang.z;
                    if (lc->slAttribute & 0x4000) {
                        func_8C079000(&cp->normal2, &cp->tnorm, twp->ang.y);
                    } else {
                        PConvertVector_P2G(twp, &cp->tnorm);
                    }
                    ofs.x = ofs.z = 0.0f;
                    ofs.y = pwp->p.height * 0.5f;
                    cofs = ofs;
                    if (lc->slAttribute & 0x4000) {
                        func_8C079000(&cp->normal2, &ofs, twp->ang.y);
                    } else {
                        PConvertVector_P2G(twp, &ofs);
                    }
                    func_8C074CF0(mwp, pwp, cp, &cofs);
                    if (lc->slAttribute & 0x4000) {
                        func_8C079000(&cp->normal2, &cofs, twp->ang.y);
                    } else {
                        PConvertVector_P2G(twp, &cofs);
                    }
                }
                mwp->ang_aim.x = cp->angx;
                mwp->ang_aim.z = cp->angz;
                pwp->floor_normal = cp->normal2;
                if (lc->slAttribute & 0x4000) {
                    v = cp->anaspdh;
                    if (njScalor(&v) != 0.0f) {
                        scl = njScalor(&cp->spd);
                        njUnitVector(&v);
                        cp->anaspdh.x = v.x * scl;
                        cp->anaspdh.y = v.y * scl;
                        cp->anaspdh.z = v.z * scl;
                    }
                }
                if (zero != 0 && (s = njScalor(&cp->anaspdh)) > 2.1f) {
                    Float f;

                    njUnitVector(&cp->anaspdh);
                    s = njScalor(&cp->spd);
                    f = s - 2.1f;
                    cp->spd.x = cp->anaspdh.x * f;
                    cp->spd.y = cp->anaspdh.y * f;
                    cp->spd.z = cp->anaspdh.z * f;
                    s = 2.1f;
                    cp->anaspdh.x *= s;
                    cp->anaspdh.y *= s;
                    cp->anaspdh.z *= s;
                    tmp = list[last];
                    list[last] = *lc;
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
                if ((tp != NULL || pwp->spd.x != 0.0f || pwp->spd.z != 0.0f || d > 1.0f || D_8C1BDFE0)
                    && (d > 0.0001f || D_8C1BDFE0)) {
                    twp->pos = v;
                }
                if (!((t1 = pwp->spd.x) < 1.84999992f && t1 > -1.84999992f
                      && !(njInnerProduct(&Gravity, &cp->tnorm) < -0.33999998f)
                      && !(lc->slAttribute & 0x4000)
                      && (((t2 = pwp->spd.x) < 1.16f && t2 > -1.16f)
                          || !(njInnerProduct(&Gravity, &cp->tnorm) < -0.33999998f)))) {
                    twp->flag |= 1;
                    func_8C075214(twp, mwp, pwp, lc->slAttribute);
                } else {
                    func_8C075214(twp, mwp, pwp, 0);
                    if (pwp->spd.x > 0.0f) {
                        func_8C0751C0(twp, mwp, cp);
                    }
                }
            } else {
                ret = func_8C0747D2(twp, mwp, pwp, cp, &oldpos, &ofs, ret, lc->slAttribute);
            }
            continue;
        }
        ret = func_8C0747D2(twp, mwp, pwp, cp, &oldpos, &ofs, ret, lc->slAttribute);
    }

    if (landed != 1) {
        mag = 0.0f;
        if ((pwp->flag & 1) && !(pwp->flag & 0x4000)
            && (!GetAnalog(twp, NULL, &mag) || (!(mag > 0.89999998f) && pwp->nocontimer == 0))
            && !(pwp->spd.y > 0.0f) && !(pwp->spd.x > pwp->p.run_speed)
            && !(pwp->last_attr & 0x1C) && pwp->dotp > 0.69999999f) {
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
#else
ASM_STUB(Sint32 func_8C0730D8(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

ASM_STUB(Sint32 func_8C0742D4(taskwk *twp, csts *cp, _OBJ_LANDCOLL *lc))

ASM_STUB(Sint32 func_8C0747D2(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp,
                              NJS_POINT3 *pos, NJS_POINT3 *ofs, Sint32 ret, Uint32 attr))
