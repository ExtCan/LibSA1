/*
 * 1ST_READ 8C029400: six-direction probe of one dynamic ("FD") land
 * collision object (SADX xyyzzxsdwstr layout; FD variant of the SADX
 * GetZxShadowOnFDPolygon / GetShadowPosXYZ family).
 *
 * The object is looked up by its NJS_OBJECT in the FD object list
 * D_8C02D548 (list code 8C02CEA0/8C02CEF4, polygon lists 8C02D060..).
 * Every polygon whose first vertex lies within 220 of xyzp->pos is tested
 * three times: projected along Z, X and Y. When the probe position projects
 * inside the triangle, the triangle plane is intersected with the axis line
 * through the position and the nearest hit on each side is kept in
 * hit[4]/hit[5] (z), hit[0]/hit[1] (x), hit[2]/hit[3] (y). Returns 1 when
 * any hit was stored, 0 otherwise (also 0 when the object is not in the list).
 * The third argument (land list attribute, passed by every caller) is unused.
 * Callers: 8C015820, 8C015938, 8C016C60, 8C091C20, 8C092BE0, 8C103200.
 *
 * NON_MATCHING (SHC 5.1r03): 417 of 773 instructions differ, all register
 * allocation / stack slot order; the instruction stream is otherwise the
 * same (block 1 spills a different value, slots of poly/objatt/z/x/y are
 * permuted, the 12-byte normal copy uses R1+R2 instead of R2 only).
 * Measured with "--flags=-extra=a=800": the whole 8C027360-8C02D980 region
 * was built without -extra bit 0x1000 (it loads 8-bit unsigned offsets
 * such as 0x9C with MOV.W, which a=1800 turns into MOV #-100 / EXTU.B).
 * Matching idioms found: the normal must be a block-scoped local of each
 * test, its reset to (0, 1, 0) is written through a Float view of &n (the
 * original rematerialises R15+40 for every store), the clear loop is a
 * while loop (CMP/PL), and the lower-side tests read "t < hit.onpos".
 */
#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/* one probe result (SADX xssunit); same layout as UNK_8C016C60_unit */
typedef struct xssunit {
    Sint32 findflag;            /* 0x00 1 = polygon found */
    Sint32 objatt;              /* 0x04 attribute of the FD object */
    Angle angx;                 /* 0x08 */
    Angle angz;                 /* 0x0C */
    Float onpos;                /* 0x10 coordinate of the hit on the probe axis */
    NJS_VECTOR normal;          /* 0x14 */
} xssunit;                      /* 0x20 */

/* probe position + six results: +x, -x, +y, -y, +z, -z (SADX xyyzzxsdwstr) */
typedef struct xyyzzxsdwstr {
    NJS_POINT3 pos;             /* 0x00 */
    xssunit hit[6];             /* 0x0C */
} xyyzzxsdwstr;                 /* 0xCC */

SA1_ASSERT_SIZE(xssunit, 0x20);
SA1_ASSERT_OFFSET(xssunit, onpos, 0x10);
SA1_ASSERT_SIZE(xyyzzxsdwstr, 0xCC);
SA1_ASSERT_OFFSET(xyyzzxsdwstr, hit, 0x0C);

/* polygon of an FD object (world-space triangle) */
typedef struct FDPOLY {
    struct FDPOLY *next;        /* 0x00 */
    void *prev;                 /* 0x04 */
    NJS_POINT3 center;          /* 0x08 bounding sphere (read by 8C02D060) */
    Float r;                    /* 0x14 */
    NJS_POINT3 v[3];            /* 0x18 */
} FDPOLY;

/* FD (dynamic) land collision object, list head D_8C02D548 */
typedef struct FDOBJ {
    struct FDOBJ *next;         /* 0x00 */
    void *prev;                 /* 0x04 */
    NJS_OBJECT *object;         /* 0x08 key */
    Sint32 attr;                /* 0x0C */
    FDPOLY *poly;               /* 0x10 polygon list */
} FDOBJ;

SA1_ASSERT_OFFSET(FDPOLY, v, 0x18);
SA1_ASSERT_OFFSET(FDOBJ, poly, 0x10);

extern FDOBJ *D_8C02D548;

/* triangle normal (not normalised): (v1 - v0) x (v0 - v2) */
extern void func_8C027360(NJS_POINT3 *v0, NJS_POINT3 *v1, NJS_POINT3 *v2, NJS_VECTOR *n);

#ifdef NON_MATCHING
Sint32 func_8C029400(xyyzzxsdwstr *xyzp, NJS_OBJECT *object, Uint32 attr)
{
    Sint32 ret;
    FDPOLY *poly;
    Sint32 objatt;
    Float z;
    Float x;
    Float y;
    FDOBJ *fdp;
    NJS_POINT3 *v0;
    NJS_POINT3 *v1;
    NJS_POINT3 *v2;
    xssunit *hp;
    Sint32 i;
    Sint32 found;
    Float c0;
    Float c1;
    Float c2;
    Float d;

    PC_ONLY((void)attr;)
    ret = 0;
    i = 6;
    hp = xyzp->hit;
    while (i > 0) {
        hp->findflag = 0;
        hp++;
        i--;
    }
    xyzp->hit[0].onpos = xyzp->pos.x + 220.0f;
    xyzp->hit[1].onpos = xyzp->pos.x - 220.0f;
    xyzp->hit[2].onpos = xyzp->pos.y + 220.0f;
    xyzp->hit[3].onpos = xyzp->pos.y - 220.0f;
    xyzp->hit[4].onpos = xyzp->pos.z + 220.0f;
    xyzp->hit[5].onpos = xyzp->pos.z - 220.0f;
    found = 0;
    for (fdp = D_8C02D548; fdp != NULL; fdp = fdp->next) {
        if (fdp->object == object) {
            found = 1;
            break;
        }
    }
    if (found == 0) {
        return 0;
    }
    poly = fdp->poly;
    objatt = fdp->attr;
    for (; poly != NULL; poly = poly->next) {
        /*
         * SHC: FMUL dy*dy, FMAC dx*dx, FMAC dz*dz. The PC form fuses both FMACs
         * (SA1_FMAC); the SHC form keeps the original operand order (written
         * with SA1_FMAC, SHC permutes the loads: 422 instead of 417 differ).
         */
        if (
#ifdef TARGET_PC
            SA1_FMAC(poly->v[0].z - xyzp->pos.z, poly->v[0].z - xyzp->pos.z,
                SA1_FMAC(poly->v[0].x - xyzp->pos.x, poly->v[0].x - xyzp->pos.x,
                    (poly->v[0].y - xyzp->pos.y) * (poly->v[0].y - xyzp->pos.y)))
#else
            (poly->v[0].x - xyzp->pos.x) * (poly->v[0].x - xyzp->pos.x)
            + (poly->v[0].y - xyzp->pos.y) * (poly->v[0].y - xyzp->pos.y)
            + (poly->v[0].z - xyzp->pos.z) * (poly->v[0].z - xyzp->pos.z)
#endif
            > 48400.0f) {
            continue;
        }
        v0 = &poly->v[0];
        v1 = &poly->v[1];
        v2 = &poly->v[2];

        /* along Z */
        c0 = (v1->x - v0->x) * (xyzp->pos.y - v0->y) - (v1->y - v0->y) * (xyzp->pos.x - v0->x);
        c1 = (v2->x - v1->x) * (xyzp->pos.y - v1->y) - (v2->y - v1->y) * (xyzp->pos.x - v1->x);
        c2 = (v0->x - v2->x) * (xyzp->pos.y - v2->y) - (v0->y - v2->y) * (xyzp->pos.x - v2->x);
        if (!(c0 * c1 < 0.0f) && !(c1 * c2 < 0.0f) && !(c0 * c2 < 0.0f)) {
            NJS_VECTOR n;

            func_8C027360(v0, v1, v2, &n);
            d = -n.x * v0->x - n.y * v0->y - n.z * v0->z;
            if (!(n.z * n.z < 0.000099999992f)) {
                z = (-n.x * xyzp->pos.x - n.y * xyzp->pos.y - d) / n.z;
                hp = NULL;
                if (xyzp->pos.z > z) {
                    if (xyzp->hit[4].findflag == 0 || z > xyzp->hit[4].onpos) {
                        hp = &xyzp->hit[4];
                    }
                } else {
                    if (xyzp->hit[5].findflag == 0 || z < xyzp->hit[5].onpos) {
                        hp = &xyzp->hit[5];
                    }
                }
                if (hp != NULL) {
                    if (njScalor(&n) == 0.0f) {
                        /* NJS_VECTOR as Float[3] (no padding on any target) */
                        ((Float *)&n)[0] = 0.0f;
                        ((Float *)&n)[1] = 1.0f;
                        ((Float *)&n)[2] = 0.0f;
                    } else {
                        njUnitVector(&n);
                    }
                    ret = 1;
                    hp->findflag = 1;
                    hp->objatt = objatt;
                    hp->onpos = z;
                    hp->normal = n;
                    hp->angz = -SA1_FTOI(atan2f(n.x, n.y) * 10430.381f);
                    hp->angx = SA1_FTOI(func_8C10D058(n.z) * 10430.381f);
                }
            }
        }

        /* along X */
        c0 = (v1->y - v0->y) * (xyzp->pos.z - v0->z) - (v1->z - v0->z) * (xyzp->pos.y - v0->y);
        c1 = (v2->y - v1->y) * (xyzp->pos.z - v1->z) - (v2->z - v1->z) * (xyzp->pos.y - v1->y);
        c2 = (v0->y - v2->y) * (xyzp->pos.z - v2->z) - (v0->z - v2->z) * (xyzp->pos.y - v2->y);
        if (!(c0 * c1 < 0.0f) && !(c1 * c2 < 0.0f) && !(c0 * c2 < 0.0f)) {
            NJS_VECTOR n;

            func_8C027360(v0, v1, v2, &n);
            d = -n.x * v0->x - n.y * v0->y - n.z * v0->z;
            if (!(n.x * n.x < 0.000099999992f)) {
                x = (-n.y * xyzp->pos.y - n.z * xyzp->pos.z - d) / n.x;
                hp = NULL;
                if (xyzp->pos.x > x) {
                    if (xyzp->hit[0].findflag == 0 || x > xyzp->hit[0].onpos) {
                        hp = &xyzp->hit[0];
                    }
                } else {
                    if (xyzp->hit[1].findflag == 0 || x < xyzp->hit[1].onpos) {
                        hp = &xyzp->hit[1];
                    }
                }
                if (hp != NULL) {
                    if (njScalor(&n) == 0.0f) {
                        /* NJS_VECTOR as Float[3] (no padding on any target) */
                        ((Float *)&n)[0] = 0.0f;
                        ((Float *)&n)[1] = 1.0f;
                        ((Float *)&n)[2] = 0.0f;
                    } else {
                        njUnitVector(&n);
                    }
                    ret = 1;
                    hp->findflag = 1;
                    hp->objatt = objatt;
                    hp->onpos = x;
                    hp->normal = n;
                    hp->angz = -SA1_FTOI(atan2f(n.x, n.y) * 10430.381f);
                    hp->angx = SA1_FTOI(func_8C10D058(n.z) * 10430.381f);
                }
            }
        }

        /* along Y */
        c0 = (v1->z - v0->z) * (xyzp->pos.x - v0->x) - (v1->x - v0->x) * (xyzp->pos.z - v0->z);
        c1 = (v2->z - v1->z) * (xyzp->pos.x - v1->x) - (v2->x - v1->x) * (xyzp->pos.z - v1->z);
        c2 = (v0->z - v2->z) * (xyzp->pos.x - v2->x) - (v0->x - v2->x) * (xyzp->pos.z - v2->z);
        if (!(c0 * c1 < 0.0f) && !(c1 * c2 < 0.0f) && !(c0 * c2 < 0.0f)) {
            NJS_VECTOR n;

            func_8C027360(v0, v1, v2, &n);
            d = -n.x * v0->x - n.y * v0->y - n.z * v0->z;
            if (!(n.y * n.y < 0.000099999992f)) {
                y = (-n.z * xyzp->pos.z - n.x * xyzp->pos.x - d) / n.y;
                hp = NULL;
                if (xyzp->pos.y > y) {
                    if (xyzp->hit[2].findflag == 0 || y > xyzp->hit[2].onpos) {
                        hp = &xyzp->hit[2];
                    }
                } else {
                    if (xyzp->hit[3].findflag == 0 || y < xyzp->hit[3].onpos) {
                        hp = &xyzp->hit[3];
                    }
                }
                if (hp != NULL) {
                    if (njScalor(&n) == 0.0f) {
                        /* NJS_VECTOR as Float[3] (no padding on any target) */
                        ((Float *)&n)[0] = 0.0f;
                        ((Float *)&n)[1] = 1.0f;
                        ((Float *)&n)[2] = 0.0f;
                    } else {
                        njUnitVector(&n);
                    }
                    ret = 1;
                    hp->findflag = 1;
                    hp->objatt = objatt;
                    hp->onpos = y;
                    hp->normal = n;
                    hp->angz = -SA1_FTOI(atan2f(n.x, n.y) * 10430.381f);
                    hp->angx = SA1_FTOI(func_8C10D058(n.z) * 10430.381f);
                }
            }
        }
    }
    return ret;
}
#else
ASM_STUB(Sint32 func_8C029400(xyyzzxsdwstr *xyzp, NJS_OBJECT *object, Uint32 attr))
#endif
