#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * 1ST_READ 8C0EB5B6-8C0EBA4E (chunk 8C0EB244-8C0EBFA0, functions 14-15 of 25).
 *
 *  func_8C0EB5B6  (Big, only from func_8C0EB70E) stage 0x803 special case:
 *                 when B_8C7A40F0 is within 70 of one of the three points
 *                 D_8C565FD8[0..2], the sphere (pos + spd, radius r) is kept
 *                 inside the 80x80 square (x/z) around that point: spd.x/z
 *                 is clipped so the sphere touches the wall and ang->x/ang->z
 *                 are set to the wall tilt (0x4000/0xC000). Returns 1 when
 *                 something was clipped, else 0.
 *
 *  func_8C0EB70E  (Big, func_8C0E8A4A) sphere (radius r at pos, moving by
 *                 spd) against every gathered land object (B_8C754EF4) that
 *                 lacks attribute 2. The up vector is (0, 1, 0) rotated by
 *                 ang (Z, X, Y) when ang is given. Of the hits the one with
 *                 the smallest push-back distance is kept: pos becomes its
 *                 pshbk, spd its anaspdh, and with ang the slope angles
 *                 (normal2 below the horizon: the variant nearer to the
 *                 current ang) plus the owner's forcewk ang_spd. A moving
 *                 owner (forcewk) also carries pshbk by its pos_spd.
 *                 Returns 1 when something was hit.
 */

extern NJS_POINT3 D_8C565FD8[3];        /* stage 0x803 square centres */
extern NJS_POINT3 B_8C7A40F0;           /* position tested against them */

Sint32 func_8C0EB5B6(NJS_POINT3 *pos, NJS_VECTOR *spd, Angle3 *ang, Float r)
{
    Sint32 i;
    Sint32 ret;
    Float xmin;
    Float xmax;
    Float zmin;
    Float zmax;

    if (GetStageNumber() != 0x803) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        if (njDistanceP2P(&D_8C565FD8[i], &B_8C7A40F0) < 70.0f) {
            break;
        }
    }
    if (i == 3) {
        return 0;
    }
    xmin = D_8C565FD8[i].x + -40.0f;
    xmax = D_8C565FD8[i].x + 40.0f;
    zmin = D_8C565FD8[i].z + -40.0f;
    zmax = D_8C565FD8[i].z + 40.0f;
    ret = 0;
    if (pos->x + spd->x - r <= xmin) {
        ret = 1;
        spd->x = xmin - pos->x + r;
        ang->z = 0xC000;
    }
    if (pos->x + spd->x + r >= xmax) {
        ret = 1;
        spd->x = xmax - pos->x - r;
        ang->z = 0x4000;
    }
    if (pos->z + spd->z - r <= zmin) {
        ret = 1;
        spd->z = zmin - pos->z + r;
        ang->x = 0x4000;
    }
    if (pos->z + spd->z + r >= zmax) {
        ret = 1;
        spd->z = zmax - pos->z - r;
        ang->x = 0xC000;
    }
    return ret;
}

/*
 * NON_MATCHING only because of the symbol map (same situation as
 * code_8C0742D4.c, code_8C0CBA40.c, PInitialize in code_8C06C300.c): the two
 * csts copies (best = cs, 0x84 bytes) call SHC's __quick_mvn, which
 * config/symbols.1st_read.txt maps to 8C10CD38, while the game code calls
 * 8C10CDDC. Otherwise the body compiles to the original instruction for
 * instruction (shcdiff: 2 differing words, both __quick_mvn pool entries).
 * Matching details: cs is reached through cp (R14), best through bp only
 * after the loop; rs/v and up/vy are block-scoped locals that share the
 * stack slots 0x38/0x2C (SHC hoists &rs into R13); the rotation uses the
 * "a ? njRotateZ(NULL, a) : (void)0" form (stack temporary, see
 * code_8C056A04.c); the continue + if(call) loop shape and the separate
 * dang/rs zero stores fix the register and spill-slot assignment.
 */
#ifdef NON_MATCHING
Sint32 func_8C0EB70E(NJS_POINT3 *pos, NJS_VECTOR *spd, Angle3 *ang, Float r)
{
    csts cs;
    csts best;
    csts *cp;
    Angle3 dang;
    Float min;
    Float d;
    Uint16 i;
    Sint32 hit;
    Angle angx;
    Angle angz;
    Angle angx2;
    Angle angz2;
    Sint32 d1;
    Sint32 d2;
    _OBJ_LANDCOLL *lp;
    csts *bp;
    task *tp;
    forcewk *fwp;

    hit = 0;
    PC_ONLY(min = 0.0f;) /* only read once hit != 0, i.e. after it was set */
    if (func_8C0EB5B6(pos, spd, ang, r)) {
        return 1;
    }
    lp = B_8C754EF4;
    cp = &cs;
    dang.x = 0;
    dang.y = 0;
    dang.z = 0;
    if (ang == NULL) {
        cp->tnorm.x = 0.0f;
        cp->tnorm.y = 1.0f;
        cp->tnorm.z = 0.0f;
    } else {
        NJS_VECTOR up;
        NJS_VECTOR vy;

        vy.x = 0.0f;
        vy.y = 1.0f;
        vy.z = 0.0f;
        njPushMatrix(D_8C67C640);
        ang->z ? njRotateZ(NULL, ang->z) : (void)0;
        ang->x ? njRotateX(NULL, ang->x) : (void)0;
        ang->y ? njRotateY(NULL, ang->y) : (void)0;
        njCalcVector(NULL, &vy, &up);
        njPopMatrix(1);
        cp->tnorm = up;
    }
    cp->pos = *pos;
    cp->radius = r;
    cp->spd = *spd;
    for (i = 0; i < B_8C754EF0; i++) {
        Angle3 rs;
        NJS_VECTOR v;

        if (lp[i].slAttribute & 2) {
            continue;
        }
        if (func_8C029B00(cp, lp[i].pObject, lp[i].slAttribute)) {
            if (ang != NULL && cp->normal2.y < 0.0f) {
                angx = SA1_FTOI(func_8C10D058(cp->normal2.z) * 10430.381f);
                angz = -SA1_FTOI(func_8C10D058(cp->normal2.x) * 10430.381f);
                angx2 = (Uint16)(0x8000 - angx);
                angz2 = (Uint16)(0x8000 - angz);
                d1 = DiffAngle(ang->x, angx2);
                d2 = DiffAngle(ang->z, angz2);
                if (d1 < d2) {
                    cp->angx = angx2;
                    cp->angz = angz;
                } else {
                    cp->angx = angx;
                    cp->angz = angz2;
                }
            }
            tp = lp[i].pTask;
            if (tp != NULL && tp->fwp != NULL) {
                fwp = FWP(tp);
                cp->pshbk.x += fwp->pos_spd.x;
                cp->pshbk.y += fwp->pos_spd.y;
                cp->pshbk.z += fwp->pos_spd.z;
                rs = fwp->ang_spd;
            } else {
                rs.x = 0;
                rs.y = 0;
                rs.z = 0;
            }
            v.x = cp->pshbk.x - pos->x;
            v.y = cp->pshbk.y - pos->y;
            v.z = cp->pshbk.z - pos->z;
            if (hit == 0) {
                best = cs;
                min = njScalor(&v);
                dang = rs;
            } else {
                d = njScalor(&v);
                if (min > d) {
                    best = cs;
                    min = d;
                    dang = rs;
                }
            }
            hit = 1;
        }
    }
    if (hit == 1) {
        bp = &best;
        *pos = bp->pshbk;
        *spd = bp->anaspdh;
        if (ang != NULL) {
            ang->x = bp->angx + dang.x;
            ang->y += dang.y;
            ang->z = bp->angz + dang.z;
        }
    }
    return hit;
}
#else
ASM_STUB(Sint32 func_8C0EB70E(NJS_POINT3 *pos, NJS_VECTOR *spd, Angle3 *ang, Float r))
#endif
