/*
 * Player common (1ST_READ 8C078856-8C078E06, last function of the object
 * 8C0777E0-8C078E40): land collision variant called only by SonicMain
 * (8C0A77BC).
 *
 *  func_8C078856  1. CCL hit on a shape whose byte +5 has a low nibble set
 *                    (colliwk.flag 0x80 and 0x8, twp->flag & 4 clear): the
 *                    speed component towards the shape (8C0344DC push vector)
 *                    is cancelled (spd.x/spd.z = 0); status 1 if
 *                    pwp->spd.x < p.run_speed, else 2.
 *                 2. twp->flag & 2: clear flag bits 0/1, move by mwp->spd.x/z.
 *                 3. Otherwise the collision sphere (cstsp) is put above the
 *                    player along its rotated up vector (rotation Z, X, -Y of
 *                    twp->ang; centre at height/2 - pos_error/2, offset ofs =
 *                    height/2) and tested against every gathered land object
 *                    (B_8C754EF4). A hit whose normal2 lies within 0x2C00 of
 *                    +X (sic: the reference vector is (1, 0, 0)) lands the
 *                    player on the push-back position (twp->ang.x/z turn by
 *                    0x1000 towards the polygon slope while moving; moving
 *                    land objects through 8C07506E); any other hit pushes the
 *                    player back (status 1/2 by spd.x vs p.crash_speed when
 *                    moving into the polygon). Without a landing the player
 *                    moves by mwp->spd.
 *                 Returns the status (0 = none).
 *
 * NON_MATCHING: control flow, stack frame contents, constants and calls are
 * those of the original, but SHC allocates registers differently. The
 * original keeps ret/1/0 in R8/R9/R10 and spills rot and the induction
 * pointer of lc (three copies of lc at sp+0/4/28, one of them unused,
 * apparently left over from code SHC removed), so it does not CSE the
 * njCalcVector address. This version CSEs that address into a callee-saved
 * register, which shifts every allocation after it. Also the original literal
 * pool at 8C07890C holds the preceding function's constants (-0.01f, 0.01f,
 * func_8C0897F8: func_8C078710 in code_8C078308.c), so a match also needs
 * that function in the same translation unit (pool placement decides the
 * BF/BRA long-branch forms).
 * Evidence used for the source shape: while loop (CMP/PL guard with i = 0
 * folded), Uint16 i (EXTU.W in the loop test), the |d|^2 test reads d
 * through a pointer hoisted before the loop (R10 = sp+44; values reloaded,
 * FMUL + 2 FMAC, result moved to FR4), ret = 2 on the fall-through path.
 */
#define func_8C029B00 func_8C029B00_hdr
#include "sa1/sa1.h"
#undef func_8C029B00

/* every call site passes (csts *, _OBJ_LANDCOLL.pObject, .slAttribute) */
Sint32 func_8C029B00(csts *cp, NJS_OBJECT *obj, Uint32 attr);
/* CCL hit query: push-out vector from the hit shape (8C0344DC) */
Sint8 func_8C0344DC(taskwk *twp, NJS_POINT3 *v);
/* moving land object: carry the player (code_8C0730D8.c prototype) */
void func_8C07506E(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *ofs);

#define NJM_RAD_ANG(n) SA1_FTOI((n) * 10430.381f)

#ifdef NON_MATCHING
Sint32 func_8C078856(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 ofs;             /* sp+80 */
    NJS_POINT3 cofs;            /* sp+68 */
    NJS_POINT3 v;               /* sp+56 */
    NJS_POINT3 d;               /* sp+44 */
    colliwk *cwp;
    csts *cp;
    _OBJ_LANDCOLL *lc;
    task *tp;
    Sint32 ret;
    Sint32 rot;
    Sint32 wasair;
    Sint32 ang;
    Sint32 angx;
    Sint32 angz;
    Sint32 angx2;
    Sint32 angz2;
    Sint32 diffx;
    Uint16 i;
    Sint16 oldflag;
    Uint8 landed;
    Float f;
    NJS_POINT3 *dp;

    cwp = CWP(twp);
    ret = 0;
    if ((cwp->flag & 0x80) && !(twp->flag & 4) && (cwp->flag & 8)
        && (cwp->hit_cwp->info[cwp->hit_num].b05 & 0xF)) {
        func_8C0344DC(twp, &cofs);
        d = mwp->spd;
        if (njScalor(&d) != 0.0f) {
            njUnitVector(&d);
        }
        f = njInnerProduct(&cofs, &d);
        if (f < 0.0f) {
            mwp->spd.x = 0.0f;
            mwp->spd.z = 0.0f;
        }
        /* SHC: FCMP/GT spd.x,run_speed; BT -> 1 (2 also for NaN) */
        if (pwp->spd.x >= pwp->p.run_speed) {
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
    oldflag = twp->flag;
    twp->flag &= ~3;
    landed = 0;
    lc = B_8C754EF4;
    cp = pwp->cstsp;
    func_8C057B40();
    if ((rot = twp->ang.z) != 0) {
        njRotateZ(NULL, rot);
    }
    if ((rot = twp->ang.x) != 0) {
        njRotateX(NULL, rot);
    }
    if ((rot = -twp->ang.y) != 0) {
        njRotateY(NULL, rot);
    }
    v.x = v.z = 0.0f;
    v.y = 1.0f;
    cp->angx = twp->ang.x;
    cp->angz = twp->ang.z;
    njCalcVector(NULL, &v, &cp->tnorm);
    v.x = v.z = 0.0f;
    v.y = pwp->p.height * 0.5f;
    njCalcVector(NULL, &v, &ofs);
    v.x = v.z = 0.0f;
    v.y = pwp->p.height * 0.5f - pwp->p.pos_error * 0.5f;
    njCalcVector(NULL, &v, &cofs);
    njPopMatrix(1);
    cp->radius = pwp->p.height * 0.5f;
    cp->pos.x = twp->pos.x + cofs.x;
    cp->pos.y = twp->pos.y + cofs.y;
    cp->pos.z = twp->pos.z + cofs.z;
    cp->spd = mwp->spd;
    func_8C028BFE(cp);
    i = 0;
    wasair = !(oldflag & 3);
    dp = &d;
    while (i < B_8C754EF0) {
        if (func_8C029B00(cp, lc[i].pObject, lc[i].slAttribute)) {
            v.x = 1.0f;
            v.y = v.z = 0.0f;
            ang = VectorAngle(&v, &cp->normal2, NULL);
            if (ang <= 0x2C00) {
                if (pwp->spd.y <= 0.0f || (pwp->spd.y > 0.0f && wasair)) {
                    landed = 1;
                    if (mwp->spd.x != 0.0f || mwp->spd.z != 0.0f) {
                        angx = NJM_RAD_ANG(func_8C10D058(cp->normal2.z));
                        angz = -NJM_RAD_ANG(func_8C10D058(cp->normal2.x));
                        if (cp->normal2.y < 0.0f) {
                            angx2 = (Uint16)(0x8000 - angx);
                            angz2 = (Uint16)(0x8000 - angz);
                            diffx = DiffAngle(twp->ang.x, angx2);
                            if (diffx < DiffAngle(twp->ang.z, angz2)) {
                                cp->angx = angx2;
                                cp->angz = angz;
                            } else {
                                cp->angx = angx;
                                cp->angz = angz2;
                            }
                        }
                        twp->ang.x = AdjustAngle(twp->ang.x, cp->angx, 0x1000);
                        twp->ang.z = AdjustAngle(twp->ang.z, cp->angz, 0x1000);
                    }
                    mwp->ang_aim.x = cp->angx;
                    mwp->ang_aim.z = cp->angz;
                    cp->pos.x = cp->pshbk.x + cp->anaspdh.x;
                    cp->pos.y = cp->pshbk.y + cp->anaspdh.y;
                    cp->pos.z = cp->pshbk.z + cp->anaspdh.z;
                    if ((tp = lc[i].pTask) != NULL) {
                        pwp->ttp = tp;
                        func_8C07506E(twp, pwp, cp, &ofs);
                    }
                    v.x = cp->pos.x - ofs.x;
                    v.y = cp->pos.y - ofs.y;
                    v.z = cp->pos.z - ofs.z;
                    dp->x = twp->pos.x - v.x;
                    dp->y = twp->pos.y - v.y;
                    dp->z = twp->pos.z - v.z;
                    /* FMUL y*y, then FMAC +x*x, FMAC +z*z (fused on PC) */
                    f = dp->y * dp->y;
                    SA1_FMAC_ACC(f, dp->x, dp->x);
                    SA1_FMAC_ACC(f, dp->z, dp->z);
                    if (f > 0.0001f) {
                        twp->pos = v;
                    }
                    cp->spd.x = cp->spd.y = cp->spd.z = 0.0f;
                    twp->flag |= 1;
                }
            } else {
                d.x = -mwp->spd.x;
                d.y = -mwp->spd.y;
                d.z = -mwp->spd.z;
                if (njScalor(&d) != 0.0f) {
                    njUnitVector(&d);
                    if (VectorAngle(&d, &cp->normal2, NULL) <= 0x2C00) {
                        /* SHC: FCMP/GT spd.x,crash_speed; BT -> 1 */
                        if (pwp->spd.x >= pwp->p.crash_speed) {
                            ret = 2;
                        } else {
                            ret = 1;
                        }
                    }
                }
                cp->pos = cp->pshbk;
                twp->pos.x = cp->pshbk.x - ofs.x;
                twp->pos.y = cp->pshbk.y - ofs.y;
                twp->pos.z = cp->pshbk.z - ofs.z;
                cp->spd = cp->anaspdh;
            }
        }
        i++;
    }
    if (landed != 1) {
        twp->pos.x += mwp->spd.x;
        twp->pos.y += mwp->spd.y;
        twp->pos.z += mwp->spd.z;
    }
    return ret;
}
#else
ASM_STUB(Sint32 func_8C078856(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
