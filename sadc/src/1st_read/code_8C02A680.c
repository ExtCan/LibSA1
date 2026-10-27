#include "sa1/sa1.h"

/*
 * 1ST_READ 8C02A680: line (ray) vs land collision. Casts lc->vec from lc->pos
 * against the cached polygons of the land objects gathered around lc->pos by
 * func_8C0522C0 (ListGroundForCollision) and keeps the nearest hit within
 * lc->len. Returns 1 if any polygon was hit. First function of the object
 * 8C02A680-8C02AE78 (second: func_8C02AD22, code_8C02AD22.c), part of the
 * land collision module 8C027360-8C02CE80. Callers: 8C021B60, 8C105036.
 *
 * Per polygon: reject it when all three vertices lie behind the ray origin,
 * build the plane normal (func_8C027360) of the triangle translated to
 * vertex 0, reject back faces unless attr 0x00800000 is set, reject planes
 * farther than len + 220, rotate everything into the plane's frame
 * (normal -> +Y), intersect the ray with y = 0, do a 2D point-in-triangle
 * test in XZ, then rotate the hit point back.
 *
 * Matching notes (NON_MATCHING):
 *  - Like the rest of the collision module (code_8C028480.c, code_8C02AD22.c)
 *    this object was built with SHC 5.0r31/5.0r32, not 5.1r03. The tell-tale
 *    here: (&local)->field in the DOT/SIDE/VSET macros is not folded into
 *    @(R0,R15) by 5.0r3x (the original rematerializes R15+off for every
 *    access), and the 12-byte struct copies use one temporary register.
 *  - With --ver=5.0r32 the body below is instruction-for-instruction the
 *    original except for register allocation: the original keeps -0.01f in
 *    FR12 (loaded before the loop) and spills dist to @(4,R15), and hoists
 *    &D_8C67C640 into a spill slot @(16,R15); this compile keeps dist in FR12,
 *    reloads -0.01f and D_8C67C640 at their uses, so the frame is 8 bytes
 *    smaller and R9/R10 (&v[0] / njCalcPoint) swap. Not reproduced with
 *    variables for -0.01f and D_8C67C640 (the variable for D_8C67C640 does
 *    give the @(16,R15) slot, but -0.01f still loses FR12 to dist), block or
 *    merged declarations of dist, or "register" (moves it to FR15).
 *  - Sibling blocks are required: d0..d2 and v[]/rel/... share the top of
 *    the frame (d0 at @(104,R15) is v[2]'s slot).
 *  - SHC 5.0r32 hits "4099 Internal error" when the dot products are written
 *    z-first (SA1_FMAC(z, z, SA1_FMAC(x, x, y * y))) or with (Float *) casts,
 *    so their SHC form stays the plain left-to-right sum (SHC fuses it into
 *    FMUL, FMAC, FMAC); the TARGET_PC form of DOT/DOTV spells out the same
 *    fusion with SA1_FMAC for libsa1.
 */

/* Polygon of the land collision polygon cache (built by the 8C028E60 code). */
typedef struct LCPOLY {
    struct LCPOLY *next;        /* 0x00 */
    Sint32 unk04;               /* 0x04 not used here */
    NJS_POINT3 center;          /* 0x08 bounding sphere centre (read by 8C028BFE) */
    Float radius;               /* 0x14 bounding sphere radius (read by 8C028BFE) */
    NJS_POINT3 vtx[3];          /* 0x18 triangle, world space */
    Uint32 attr;                /* 0x3C ColFlags; 0x00800000: no back-face rejection here */
} LCPOLY;

/* Land object with its cached polygons (list head D_8C02D548). */
typedef struct LCOBJ {
    struct LCOBJ *next;         /* 0x00 */
    Sint32 unk04;               /* 0x04 not used here */
    NJS_OBJECT *object;         /* 0x08 compared with _OBJ_LANDCOLL.pObject */
    Sint32 unk0C;               /* 0x0C not used here */
    LCPOLY *polys;              /* 0x10 */
} LCOBJ;

/* Query/result block passed in R4. */
typedef struct LINECOLLI {
    Float len;                  /* 0x00 ray length; forced to 400 outside [0.01, 400] */
    NJS_POINT3 pos;             /* 0x04 ray origin */
    NJS_VECTOR vec;             /* 0x10 ray direction */
    Float dist;                 /* 0x1C distance of the nearest hit (out) */
    NJS_POINT3 hitpos;          /* 0x20 hit point (out) */
    Sint32 hit;                 /* 0x2C 1 when hit (out; cleared on entry) */
    Uint32 attr;                /* 0x30 slAttribute of the hit object (out) */
    Angle angx;                 /* 0x34 asin-like of normal.z (out) */
    Angle angz;                 /* 0x38 -atan2(normal.x, normal.y) (out) */
    Sint32 unk3C;               /* 0x3C not written here */
    NJS_VECTOR normal;          /* 0x40 unit polygon normal (out) */
} LINECOLLI;                    /* 0x4C */

SA1_ASSERT_OFFSET(LCPOLY, vtx, 0x18);
SA1_ASSERT_OFFSET(LCPOLY, attr, 0x3C);
SA1_ASSERT_OFFSET(LCOBJ, polys, 0x10);
SA1_ASSERT_OFFSET(LINECOLLI, normal, 0x40);
SA1_ASSERT_SIZE(LINECOLLI, 0x4C);

extern LCOBJ *D_8C02D548;           /* polygon cache: list of land objects */
extern NJS_MATRIX D_8C67C640;       /* unit matrix (pushed as the base) */
/* triangle normal (b - a) x (c - a) into n (8C027360, not normalised) */
extern void func_8C027360(NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *c, NJS_VECTOR *n);

#ifdef NON_MATCHING

#define VSET(v, a, b, c) ((v)->x = (a), (v)->y = (b), (v)->z = (c))
/*
 * Dot products: the original computes y * y' with FMUL, then FMACs x * x' and
 * z * z' onto it (5 dot products, 10 FMACs). libsa1 fuses them the same way;
 * the SHC form stays the plain left-to-right sum (see the matching notes).
 */
#ifdef TARGET_PC
#define DOT(a, b) SA1_FMAC((a)->z, (b)->z, SA1_FMAC((a)->x, (b)->x, (a)->y * (b)->y))
#define DOTV(a, b) SA1_FMAC((a).z, (b).z, SA1_FMAC((a).x, (b).x, (a).y * (b).y))
#else
#define DOT(a, b) ((a)->x * (b)->x + (a)->y * (b)->y + (a)->z * (b)->z)
#define DOTV(a, b) ((a).x * (b).x + (a).y * (b).y + (a).z * (b).z)
#endif
#define SIDE(a, b, p) (((b)->z - (a)->z) * ((p)->x - (a)->x) - ((b)->x - (a)->x) * ((p)->z - (a)->z))

Sint32 func_8C02A680(LINECOLLI *lc)
{
    Float r2;
    Float d;
    Float c0;
    Float c1;
    Float c2;
    Float dist;
    Sint32 ret;
    Sint32 n;
    Sint32 found;
    LCOBJ *obj;
    LCPOLY *poly;
    NJS_POINT3 *p0;
    NJS_POINT3 *p1;
    NJS_POINT3 *p2;
    _OBJ_LANDCOLL *lp;

    ret = 0;
    lc->hit = 0;
    if (lc->len < 0.01f || lc->len > 400.0f) {
        lc->len = 400.0f;
    }
    r2 = lc->len + 220.0f;
    r2 *= r2;
    func_8C0522C0(lc->pos.x, lc->pos.y, lc->pos.z, lc->len);
    for (n = B_8C754EF0, lp = B_8C754EF4; n > 0; n--, lp++) {
        found = 0;
        for (obj = D_8C02D548; obj; obj = obj->next) {
            if (obj->object == lp->pObject) {
                found = 1;
                break;
            }
        }
        if (!found) {
            continue;
        }
        for (poly = obj->polys; poly; poly = poly->next) {
            p0 = poly->vtx;
            p1 = p0 + 1;
            p2 = p0 + 2;
            {
                NJS_POINT3 d0;
                NJS_POINT3 d1;
                NJS_POINT3 d2;

                d0.x = p0->x - lc->pos.x;
                d0.y = p0->y - lc->pos.y;
                d0.z = p0->z - lc->pos.z;
                d1.x = p1->x - lc->pos.x;
                d1.y = p1->y - lc->pos.y;
                d1.z = p1->z - lc->pos.z;
                d2.x = p2->x - lc->pos.x;
                d2.y = p2->y - lc->pos.y;
                d2.z = p2->z - lc->pos.z;
                if (DOT(&d0, &lc->vec) < 0.0f && DOT(&d1, &lc->vec) < 0.0f && DOT(&d2, &lc->vec) < 0.0f) {
                    continue;
                }
            }
            {
                NJS_POINT3 v[3];
                NJS_POINT3 rel;
                NJS_VECTOR dir;
                NJS_VECTOR nrm;
                NJS_POINT3 pt;
                NJS_POINT3 out;

                v[0].x = v[0].y = v[0].z = 0.0f;
                v[1] = *p1;
                v[2] = *p2;
                v[1].x -= p0->x;
                v[2].x -= p0->x;
                v[1].y -= p0->y;
                v[2].y -= p0->y;
                v[1].z -= p0->z;
                v[2].z -= p0->z;
                rel.x = lc->pos.x - p0->x;
                rel.y = lc->pos.y - p0->y;
                rel.z = lc->pos.z - p0->z;
                dir = lc->vec;
                func_8C027360(&v[0], &v[1], &v[2], &nrm);
                if (!(poly->attr & 0x00800000) && DOT(&nrm, &dir) > -0.01f) {
                    continue;
                }
                if (njScalor(&nrm) == 0.0f) {
                    VSET(&nrm, 0.0f, 1.0f, 0.0f);
                } else {
                    njUnitVector(&nrm);
                }
                d = DOTV(nrm, rel);
                d *= d;
                if (d > r2) {
                    continue;
                }
                njPushMatrix(D_8C67C640);
                if (-SA1_FTOI(func_8C10D008(nrm.y) * 10430.381f)) {
                    njRotateX(NULL, -SA1_FTOI(func_8C10D008(nrm.y) * 10430.381f));
                }
                if (-SA1_FTOI(atan2f(nrm.x, nrm.z) * 10430.381f)) {
                    njRotateY(NULL, -SA1_FTOI(atan2f(nrm.x, nrm.z) * 10430.381f));
                }
                njCalcPoint(NULL, &v[1], &pt);
                v[1] = pt;
                njCalcPoint(NULL, &v[2], &pt);
                v[2] = pt;
                njCalcPoint(NULL, &rel, &pt);
                rel = pt;
                njCalcPoint(NULL, &dir, &pt);
                dir = pt;
                if (!(rel.y < 0.0f) ? !(dir.y > -0.01f) : !(dir.y < 0.01f)) {
                    pt.x = -(dir.x * rel.y / dir.y) + rel.x;
                    pt.z = -(dir.z * rel.y / dir.y) + rel.z;
                    pt.y = 0.0f;
                    c0 = SIDE(&v[0], &v[1], &pt);
                    c1 = SIDE(&v[1], &v[2], &pt);
                    c2 = SIDE(&v[2], &v[0], &pt);
                    if (!(c0 * c1 < 0.0f) && !(c1 * c2 < 0.0f) && !(c0 * c2 < 0.0f)) {
                        dist = njDistanceP2P(&rel, &pt);
                        if (!(dist > lc->len)) {
                            njInvertMatrix(NULL);
                            njCalcPoint(NULL, &pt, &out);
                            pt.x = out.x + p0->x;
                            pt.y = out.y + p0->y;
                            pt.z = out.z + p0->z;
                            if (!ret || lc->dist > dist) {
                                ret = 1;
                                lc->dist = dist;
                                lc->hitpos = pt;
                                lc->hit = 1;
                                lc->attr = lp->slAttribute;
                                lc->angx = SA1_FTOI(func_8C10D058(nrm.z) * 10430.381f);
                                lc->angz = -SA1_FTOI(atan2f(nrm.x, nrm.y) * 10430.381f);
                                lc->normal = nrm;
                                r2 = lc->len + 220.0f;
                                r2 *= r2;
                            }
                        }
                    }
                }
                njPopMatrix(1);
            }
        }
    }
    return ret;
}

#endif /* NON_MATCHING */
