/*
 * 1ST_READ 8C02D060-8C02D528: shadow/ground probes against one dynamic ("FD")
 * land collision object (SADX zxsdwstr / xssunit; SADX analogue of the
 * GetZxShadowOnFDPolygon family). The object is looked up by its NJS_OBJECT
 * in the FD object list D_8C02D548 (see code_8C028E60.c, code_8C029400.c);
 * every polygon whose bounding sphere is near the probe position and whose
 * XZ projection contains the probe position is intersected with the vertical
 * line through it.
 *
 *   8C02D060  lower probe only (callers 8C091D74, 8C091F02, 8C092000, 8C0D278A)
 *   8C02D170  its per-polygon test
 *   8C02D278  normalise FR4-FR6 (FIPR + FSRRA)
 *   8C02D286  triangle normal (v1 - v0) x (v2 - v0)
 *   8C02D2B2  XZ edge function
 *   8C02D2E0  lower + upper probe (callers 8C069220, 8C091C20, 8C0925A0,
 *             8C0E9D24, 8C0EA040)
 *   8C02D410  its per-polygon test
 *
 * All callers of 8C02D060 and 8C02D2E0 also pass the land table entry's
 * attribute (slAttribute) in R6, which the probes never read; it is the third
 * parameter (slattr) of their canonical prototypes (sa1/protos.h).
 *
 * HAND-WRITTEN SH-4 ASSEMBLY (not SHC output), so nothing here can match:
 * - private register conventions: 8C02D170/8C02D410 take the vertex pointer
 *   in R12, the probe in R14, the attribute in R13 and the probe position in
 *   FR8-FR10 (left there by their callers); 8C02D286 takes the triangle in
 *   FR0-FR8 and returns the normal in FR9-FR11; 8C02D278 returns in FR4-FR6;
 *   8C02D2B2 returns its pointer arguments advanced by one vertex (R4/R5 +12)
 *   and the callers rely on it (ADD #-36,R5 to wrap back to v[0]);
 * - callee-saved FR12/FR15 are used as scratch by the helpers and FR6-FR11
 *   are saved by the callee (8C02D170/8C02D410) for the caller;
 * - FIPR on hand-built vectors and FSRRA (left as .DATA.W H'F77D by the
 *   disassembler), "00 09" alignment fill up to the 32-byte aligned 8C02D2E0;
 * - the list heads and constants live in the code section right after it
 *   (D_8C02D540..D_8C02D96C, chunk 8C02D528).
 * The bodies below are C descriptions of what the assembly does; they are
 * kept under NON_MATCHING only and the original assembly stays in the build.
 *
 * Quirks of the original reproduced below (marked "sic"):
 * - 8C02D060's skip loop tests the z of the polygon before the step, so the
 *   first polygon with center.z >= zlo is skipped as well (8C02D2E0 does not);
 * - 8C02D170 compares the new hit with the previous one using n.y (FR9 was
 *   overwritten by the normal) instead of the probe y, and makes only one of
 *   the two differences absolute (FABS FR0 twice).
 */
#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/* one probe result (SADX xssunit; same layout as in code_8C029400.c) */
typedef struct xssunit {
    Sint32 findflag;            /* 0x00 1 = polygon found */
    Sint32 objatt;              /* 0x04 attribute of the FD object */
    Angle angx;                 /* 0x08 */
    Angle angz;                 /* 0x0C */
    Float onpos;                /* 0x10 y of the hit */
    NJS_VECTOR normal;          /* 0x14 unit polygon normal */
} xssunit;                      /* 0x20 */

/* probe position + lower/upper result (SADX zxsdwstr) */
typedef struct zxsdwstr {
    NJS_POINT3 pos;             /* 0x00 */
    xssunit lower;              /* 0x0C */
    xssunit upper;              /* 0x2C (8C02D2E0 only) */
} zxsdwstr;                     /* 0x4C */

SA1_ASSERT_SIZE(xssunit, 0x20);
SA1_ASSERT_OFFSET(xssunit, onpos, 0x10);
SA1_ASSERT_OFFSET(xssunit, normal, 0x14);
SA1_ASSERT_SIZE(zxsdwstr, 0x4C);
SA1_ASSERT_OFFSET(zxsdwstr, lower, 0x0C);
SA1_ASSERT_OFFSET(zxsdwstr, upper, 0x2C);

/* polygon of an FD object (world-space triangle), sorted by center.z */
typedef struct FDPOLY {
    struct FDPOLY *next;        /* 0x00 increasing center.z */
    struct FDPOLY *prev;        /* 0x04 */
    NJS_POINT3 center;          /* 0x08 bounding sphere centre */
    Float rr;                   /* 0x14 compared with squared distances */
    NJS_POINT3 v[3];            /* 0x18 */
} FDPOLY;

/* FD (dynamic) land collision object, list head D_8C02D548 */
typedef struct FDOBJ {
    struct FDOBJ *next;         /* 0x00 */
    void *prev;                 /* 0x04 */
    NJS_OBJECT *object;         /* 0x08 key */
    Sint32 attr;                /* 0x0C copied to xssunit.objatt */
    FDPOLY *head;               /* 0x10 lowest center.z */
    FDPOLY *tail;               /* 0x14 highest center.z */
    Sint32 unk18;               /* 0x18 not accessed here */
    void *unk1C;                /* 0x1C not accessed here */
    Sint32 unk20;               /* 0x20 not accessed here */
    Sint32 unk24;               /* 0x24 not accessed here */
    Float zr;                   /* 0x28 half extent in z */
} FDOBJ;

SA1_ASSERT_OFFSET(FDPOLY, rr, 0x14);
SA1_ASSERT_OFFSET(FDPOLY, v, 0x18);
SA1_ASSERT_OFFSET(FDOBJ, attr, 0x0C);
SA1_ASSERT_OFFSET(FDOBJ, head, 0x10);
SA1_ASSERT_OFFSET(FDOBJ, tail, 0x14);
SA1_ASSERT_OFFSET(FDOBJ, zr, 0x28);

/* in the code section after this module (chunk 8C02D528) */
extern FDOBJ *D_8C02D548;               /* FD objects in use (head) */
extern Sint32 D_8C02D54C;               /* return value: 1 = a polygon was found */
extern Float D_8C02D960;                /* 900.0f added to FDPOLY.rr */
extern Float D_8C02D964;                /* 10430.0f radians to Angle */
extern Float D_8C02D968;                /* 0.0001f minimum n.y * n.y */
extern Float D_8C02D96C;                /* 220.0f initial onpos range */

/* atan2-like libm wrapper (FR4 y, FR5 x -> FR0): calls the core 8C10E6A0,
 * which divides FR4 by FR5; same declaration as in code_8C02D410.c */
extern Float func_8C10D0A8(Float y, Float x);

#ifdef NON_MATCHING
Sint32 func_8C02D060(zxsdwstr *zssp, NJS_OBJECT *object, Uint32 slattr);
void func_8C02D170(NJS_POINT3 *v, zxsdwstr *zssp, Sint32 attr);
/*
 * The three BSR helpers have private register conventions that no C
 * prototype can express (8C02D278 returns a vector in FR4-FR6, 8C02D286 takes
 * FR0-FR8 and returns FR9-FR11, 8C02D2B2 also returns R4/R5 advanced by 12),
 * so their C versions are file-local helpers of func_8C02D170 with ordinary
 * pointer parameters, not replacements for the guest entry points (the
 * original callers 8C02D170/8C02D410 are assembly that relies on those
 * registers; code_8C02D410.c inlines the same computations). Their behaviour
 * is checked through func_8C02D060 -> func_8C02D170.
 */
static void func_8C02D278(NJS_VECTOR *v);
static void func_8C02D286(NJS_POINT3 *v, NJS_VECTOR *n);
static Float func_8C02D2B2(NJS_POINT3 *a, NJS_POINT3 *b, Float px, Float pz);
Sint32 func_8C02D2E0(zxsdwstr *zssp, NJS_OBJECT *object, Uint32 slattr);
void func_8C02D410(NJS_POINT3 *v, zxsdwstr *zssp, Sint32 attr);

/*
 * Lower probe: finds the polygons of the FD object `object` under/over
 * zssp->pos and keeps one hit in zssp->lower. Returns 1 if a polygon was
 * taken, 0 otherwise (also when the object is not in the FD list).
 */
Sint32 func_8C02D060(zxsdwstr *zssp, NJS_OBJECT *object, Uint32 slattr)
{
    FDOBJ *fdp;
    FDPOLY *poly;
    FDPOLY *tail;
    Sint32 attr;
    Float zr;
    Float zlo;
    Float zhi;
    Float z;
    Float px;
    Float py;
    Float pz;
    Float lim;
    Float t;
    Float d;

    zssp->lower.findflag = 0;
    for (fdp = D_8C02D548; fdp != NULL; fdp = fdp->next) {
        if (fdp->object == object) {
            break;
        }
    }
    if (fdp == NULL) {
        return 0;
    }
    D_8C02D54C = 0;
    poly = fdp->head;
    tail = fdp->tail;
    attr = fdp->attr;
    zr = fdp->zr;
    zlo = zssp->pos.z - zr;
    zhi = zssp->pos.z + zr;
    px = zssp->pos.x;
    py = zssp->pos.y;
    pz = zssp->pos.z;
    if (zlo > poly->center.z) {
        if (!(tail->center.z > zhi)) {
            /* walk backwards from the tail */
            for (poly = tail; poly != NULL; poly = poly->prev) {
                if (zlo > poly->center.z) {
                    break;
                }
                lim = poly->rr + D_8C02D960;
                t = poly->center.x - px;
                d = t * t;
                if (d > lim) {
                    continue;
                }
                t = poly->center.y - py;
                d = t * t + d;
                if (d > lim) {
                    continue;
                }
                t = poly->center.z - pz;
                d = t * t + d;
                if (d > lim) {
                    continue;
                }
                func_8C02D170(poly->v, zssp, attr);
            }
            return D_8C02D54C;
        }
        /* sic: tests the polygon before the step (skips one in-range polygon).
         * The original does not check for NULL here and below (it would read
         * near address 0 when no polygon follows); the C stops instead. */
        do {
            z = poly->center.z;
            poly = poly->next;
        } while (zlo > z && poly != NULL);
    }
    /* walk forwards */
    for (; poly != NULL; poly = poly->next) {
        if (!(zhi > poly->center.z)) {
            break;
        }
        lim = poly->rr + D_8C02D960;
        t = poly->center.x - px;
        d = t * t;
        if (d > lim) {
            continue;
        }
        t = poly->center.y - py;
        d = t * t + d;
        if (d > lim) {
            continue;
        }
        t = poly->center.z - pz;
        d = t * t + d;
        if (d > lim) {
            continue;
        }
        func_8C02D170(poly->v, zssp, attr);
    }
    return D_8C02D54C;
}

/*
 * Per-polygon test of 8C02D060 (asm: v = &poly->v[0] in R12, zssp in R14,
 * attr in R13). If (pos.x, pos.z) lies inside the XZ projection of the
 * triangle and the triangle is not vertical, the plane height y at that
 * point replaces zssp->lower when there is no hit yet or when it is "nearer".
 */
void func_8C02D170(NJS_POINT3 *v, zxsdwstr *zssp, Sint32 attr)
{
    Float e0;
    Float e1;
    Float e2;
    Float d;
    Float y;
    NJS_VECTOR n;
    Float a[4];
    Float b[4];

    e0 = func_8C02D2B2(&v[0], &v[1], zssp->pos.x, zssp->pos.z);
    e1 = func_8C02D2B2(&v[1], &v[2], zssp->pos.x, zssp->pos.z);
    if (!(e0 * e1 > 0.0f)) {
        return;
    }
    e2 = func_8C02D2B2(&v[2], &v[0], zssp->pos.x, zssp->pos.z);
    if (!(e1 * e2 > 0.0f)) {
        return;
    }
    func_8C02D286(v, &n);
    if (!(n.y * n.y > D_8C02D968)) {
        return;
    }
    a[0] = v[0].x;
    a[1] = v[0].y;
    a[2] = v[0].z;
    a[3] = 0.0f;
    b[0] = n.x;
    b[1] = n.y;
    b[2] = n.z;
    b[3] = 0.0f;
    d = -_builtin_fipr(a, b);
    y = -(zssp->pos.x * n.x + zssp->pos.z * n.z + d) / n.y;
    if (zssp->lower.findflag != 0) {
        /* sic: n.y instead of pos.y, and only y - n.y is made absolute */
        if (!(zssp->lower.onpos - n.y > _builtin_fabsf(y - n.y))) {
            return;
        }
    }
    D_8C02D54C = 1;
    zssp->lower.findflag = 1;
    zssp->lower.objatt = attr;
    zssp->lower.onpos = y;
    func_8C02D278(&n);
    zssp->lower.normal.z = n.z;
    zssp->lower.normal.y = n.y;
    zssp->lower.normal.x = n.x;
    zssp->lower.angz = SA1_FTOI(-(func_8C10D0A8(n.x, n.y) * D_8C02D964));
    zssp->lower.angx = SA1_FTOI(func_8C10D058(zssp->lower.normal.z) * D_8C02D964);
}

/* v = v / |v| with FSRRA (asm: vector in and out in FR4-FR6, clobbers FR7) */
static void func_8C02D278(NJS_VECTOR *v)
{
    Float t[4];
    Float r;

    t[0] = v->x;
    t[1] = v->y;
    t[2] = v->z;
    t[3] = 0.0f;
    r = _builtin_fsrra(_builtin_fipr(t, t));
    v->z = t[2] * r;
    v->y = t[1] * r;
    v->x = t[0] * r;
}

/* n = (v[1] - v[0]) x (v[2] - v[0]), not normalised
 * (asm: v[0..2] in FR0-FR8, n out in FR9-FR11, clobbers FR15) */
static void func_8C02D286(NJS_POINT3 *v, NJS_VECTOR *n)
{
    Float ax;
    Float ay;
    Float az;
    Float bx;
    Float by;
    Float bz;

    ax = v[1].x - v[0].x;
    ay = v[1].y - v[0].y;
    az = v[1].z - v[0].z;
    bx = v[2].x - v[0].x;
    by = v[2].y - v[0].y;
    bz = v[2].z - v[0].z;
    n->x = ay * bz - az * by;
    n->y = az * bx - ax * bz;
    n->z = ax * by - ay * bx;
}

/* XZ edge function: (px - a.x)(b.z - a.z) - (pz - a.z)(b.x - a.x)
 * (asm: a in R4, b in R5, px/pz in FR4/FR5; returns with R4 = a + 1 and
 * R5 = b + 1, which the callers use to step to the next edge) */
static Float func_8C02D2B2(NJS_POINT3 *a, NJS_POINT3 *b, Float px, Float pz)
{
    return (px - a->x) * (b->z - a->z) - (pz - a->z) * (b->x - a->x);
}

/*
 * Lower + upper probe: like 8C02D060, but a hit above zssp->pos.y goes to
 * zssp->upper (lowest one kept) and one at or below it to zssp->lower
 * (highest one kept). Both onpos are first set to pos.y -/+ 220.
 */
Sint32 func_8C02D2E0(zxsdwstr *zssp, NJS_OBJECT *object, Uint32 slattr)
{
    FDOBJ *fdp;
    FDPOLY *poly;
    FDPOLY *tail;
    Sint32 attr;
    Float zr;
    Float zlo;
    Float zhi;
    Float px;
    Float py;
    Float pz;
    Float lim;
    Float t;
    Float d;

    zssp->lower.findflag = 0;
    zssp->upper.findflag = 0;
    for (fdp = D_8C02D548; fdp != NULL; fdp = fdp->next) {
        if (fdp->object == object) {
            break;
        }
    }
    if (fdp == NULL) {
        return 0;
    }
    D_8C02D54C = 0;
    poly = fdp->head;
    tail = fdp->tail;
    attr = fdp->attr;
    zr = fdp->zr;
    zlo = zssp->pos.z - zr;
    zhi = zssp->pos.z + zr;
    px = zssp->pos.x;
    py = zssp->pos.y;
    pz = zssp->pos.z;
    zssp->lower.onpos = py - D_8C02D96C;
    zssp->upper.onpos = py + D_8C02D96C;
    if (zlo > poly->center.z) {
        if (!(tail->center.z > zhi)) {
            /* walk backwards from the tail */
            for (poly = tail; poly != NULL; poly = poly->prev) {
                if (zlo > poly->center.z) {
                    break;
                }
                lim = poly->rr + D_8C02D960;
                t = poly->center.x - px;
                d = t * t;
                if (d > lim) {
                    continue;
                }
                t = poly->center.y - py;
                d = t * t + d;
                if (d > lim) {
                    continue;
                }
                t = poly->center.z - pz;
                d = t * t + d;
                if (d > lim) {
                    continue;
                }
                func_8C02D410(poly->v, zssp, attr);
            }
            return D_8C02D54C;
        }
        /* the tail is beyond zhi >= zlo, so this stops at the tail at the latest */
        do {
            poly = poly->next;
        } while (zlo > poly->center.z);
    }
    /* walk forwards */
    for (; poly != NULL; poly = poly->next) {
        if (!(zhi > poly->center.z)) {
            break;
        }
        lim = poly->rr + D_8C02D960;
        t = poly->center.x - px;
        d = t * t;
        if (d > lim) {
            continue;
        }
        t = poly->center.y - py;
        d = t * t + d;
        if (d > lim) {
            continue;
        }
        t = poly->center.z - pz;
        d = t * t + d;
        if (d > lim) {
            continue;
        }
        func_8C02D410(poly->v, zssp, attr);
    }
    return D_8C02D54C;
}

/* func_8C02D410 (per-polygon test of 8C02D2E0): code_8C02D410.c */
#endif
