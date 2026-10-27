/*
 * 1ST_READ 8C0C7D4C-8C0C80D4: Knuckles' climb/glide wall checks
 * (chunk 8C0C7600-8C0C80D4, functions 6-7 of 7).
 *
 * func_8C0C7D4C (KnucklesMain 8C0C0640, KnucklesModeMachine 8C0C193A):
 * when the collision work has flag 0x80 and an id other than 0-3, mode 9 /
 * action 19 and 1. Otherwise a sphere of half the player height, moved from
 * the collision centre by its radius along twp->ang.y and swept by
 * mwp->spd, is tested against the gathered land objects: 3 when one with
 * attribute 0x400002 is touched; else for the first other hit, 1 after
 * func_8C0C7C26 (wall normal: csts.normal2 for attribute 0x08201000, else
 * the first touch record with |normal2.y| >= 0.71 and normal2.y >= 0) or
 * after moving twp->pos by pshbk - centre (normal2.y <= -0.71); when hits
 * had only steep normals, PSetPosition decides 1 or 2; 0 without a hit.
 *
 * func_8C0C7FB0 (KnucklesModeMachine): 3x3 grid (step 4.0) of spheres of the
 * player height around the collision centre; 1 when any land object is hit.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * c + a * b compiled to FMAC (FMAC FR0,FR3,FR12 at 8C0C7E02/8C0C7E2C). SHC
 * loads c before the call in b and keeps it in FR12, which the operand
 * order of SA1_FMAC(a, b, c) = a * b + c does not reproduce; fused on PC.
 */
#if defined(TARGET_PC)
#define FMAC_ADD(c, a, b) SA1_FMAC((a), (b), (c))
#else
#define FMAC_ADD(c, a, b) ((c) + (a) * (b))
#endif

/* same chunk, earlier (BSR): turn to face a wall normal */
ASM_STUB(void func_8C0C7C26(taskwk *twp, motionwk *mwp, playerwk *pwp, NJS_VECTOR *normal))

Sint32 func_8C0C7D4C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    csts cs;
    NJS_VECTOR v;
    Sint32 flag;
    Sint32 i;
    _OBJ_LANDCOLL *lp;
    Sint16 j;
    TOUCHINFO *tp;

    if (twp->cwp != NULL && (CWP(twp)->flag & 0x80)) {
        if (CWP(twp)->id == 0 || CWP(twp)->id == 1 || CWP(twp)->id == 2 || CWP(twp)->id == 3) {
            return 0;
        }
        twp->flag &= ~0x400;
        twp->flag &= ~0x500;
        twp->mode = 9;
        pwp->mj.reqaction = 19;
        return 1;
    }
    cs.radius = pwp->p.height * 0.5f;
    cs.angx = twp->ang.x;
    cs.angz = twp->ang.z;
    cs.pos.x = FMAC_ADD(CWP(twp)->info->center.x, cs.radius, njCos(twp->ang.y));
    cs.pos.y = CWP(twp)->info->center.y;
    cs.pos.z = FMAC_ADD(CWP(twp)->info->center.z, cs.radius, njSin(twp->ang.y));
    cs.spd = mwp->spd;
    func_8C028BFE(&cs);
    for (i = B_8C754EF0, lp = B_8C754EF4; i > 0; i--, lp++) {
        if ((lp->slAttribute & 0x400002) && func_8C029B00(&cs, lp->pObject, lp->slAttribute) == 1) {
            return 3;
        }
    }
    flag = 0;
    /* the explicit continue gives the original loop shape (BRA to the test
     * at the bottom instead of a duplicated test at the top) */
    for (i = B_8C754EF0, lp = B_8C754EF4; i > 0; i--, lp++) {
        if (lp->slAttribute & 0x400002) {
            continue;
        }
        if (func_8C029B00(&cs, lp->pObject, lp->slAttribute) == 1) {
            flag = 1;
            if (lp->slAttribute & 0x08201000) {
                func_8C0C7C26(twp, mwp, pwp, &cs.normal2);
                return 1;
            }
            tp = B_8C6BACF0;
            for (j = cs.find_count; j > 0; j--, tp++) {
                if (tp->normal2.y <= -0.71f || tp->normal2.y >= 0.71f) {
                    if (tp->normal2.y >= 0.0f) {
                        func_8C0C7C26(twp, mwp, pwp, &tp->normal2);
                    } else {
                        v = tp->pshbk;
                        njSubVector(&v, &CWP(twp)->info->center);
                        njAddVector(&twp->pos, &v);
                    }
                    return 1;
                }
            }
        }
    }
    if (flag == 1) {
        if (PSetPosition(twp, mwp, pwp)) {
            return 2;
        }
        return 1;
    }
    return 0;
}

Sint32 func_8C0C7FB0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    csts cs;
    Float z;
    Float x;
    Sint32 i;
    _OBJ_LANDCOLL *lp;

    cs.radius = pwp->p.height;
    cs.angx = twp->ang.x;
    cs.angz = twp->ang.z;
    cs.pos = CWP(twp)->info->center;
    cs.spd.x = cs.spd.y = cs.spd.z = 0.0f;
    func_8C028BFE(&cs);
    for (z = -4.0f; z <= 4.0f; z += 4.0f) {
        for (x = -4.0f; x <= 4.0f; x += 4.0f) {
            cs.pos = CWP(twp)->info->center;
            cs.pos.x += x;
            cs.pos.z += z;
            for (i = B_8C754EF0, lp = B_8C754EF4; i > 0; i--, lp++) {
                if (func_8C029B00(&cs, lp->pObject, lp->slAttribute) == 1) {
                    return 1;
                }
            }
        }
    }
    return 0;
}
