/*
 * 1ST_READ 8C029B00-8C02A680 (land collision TU 8C027360-8C02CE80):
 * TOUCH-poly search.
 *
 * func_8C029B00 tests the collision sphere of a csts (cp->pos, cp->radius,
 * moving by cp->spd) against the NEAR-poly list of one land object (built
 * by 8C028BFE; the object is looked up by its NJS_OBJECT in D_8C02D548).
 * For every candidate triangle the frame is rotated so that the triangle
 * normal is +Y, and the sphere centre is swept from pos to pos + spd in
 * steps of radius / 2; the first step whose height above the plane is
 * within the radius and which projects inside the triangle (XZ) is a touch.
 * Polygons with attr 0x800000 are touched from both sides. Touches go into
 * B_8C73E404 (16 entries, count B_8C73E400) and are copied to B_8C6BACF0;
 * the touch with the lowest height after the move is selected into the
 * csts, onpoly/pshbk are averaged over all touches. Returns 1 when at least
 * one polygon was touched. R6 (the land object attribute) is unused.
 * Callers: PSetPosition family (8C0730D8, 8C0777E0, 8C078856, ...),
 * 8C056A04, 8C0C7D4C, 8C0EB70E, ...
 *
 * NON_MATCHING. This TU was not built with the project compiler (MOV.W
 * constants, see docs/compiler.md); the closest available compiler is SHC
 * 5.0r31/5.0r32 (shcdiff --ver=5.0r32 --flags=-define=NON_MATCHING): same
 * frame (0xF4), same callee-saved registers (R8-R14, FR12-FR15) and the same
 * code for the list search, the per-polygon setup, the display block and the
 * whole tail. Remaining differences are compiler-internal: the original
 * loads the object key into R6 before the list search, initialises its
 * loop-invariant registers (R11 = njCalcPoint, R14 = &info, FR14 = 10430.381,
 * FR15 = 0) after the polygon-count test instead of before it, numbers the
 * scratch FP registers of the vertex block and of the triangle test
 * differently and orders its spill slots differently.
 *
 * SA1_GA (libsa1's lowered game code only; not the SHC build, the harness's
 * SHC / TARGET_PC builds or their traces): a PC-only option like SA1_NEARCAP
 * in code_8C028BDC.c (sadc docs/research/gsa1_091b_ceiling.md FIX-C1 with its
 * Review R.5.3). A host world keeps the underside of a slab thinner than a
 * sphere's reach (a thin roof) as a FRONT-ONLY polygon: a sphere whose centre
 * starts the step behind such a polygon does not touch it (as if it were left
 * out: the touch from behind won the selection and pushed the sphere out on
 * the far side of the slab), one that starts in front of it touches it as any
 * single-sided polygon (a ceiling: the game's head contact). The top of such a
 * slab (libsa1's SA1CA_MAT_THIN_TOP) is not touched from behind by a sphere
 * that starts more than its radius behind it and moves towards its front: a
 * sphere rising from under the slab, held one radius below the front-only
 * underside, whose step reaches the top (a slab thinner than the step's
 * rise) would otherwise be put on the roof; a sphere that dipped into the
 * slab from above (within its radius of the top) is touched as before. The
 * rule lives in
 * a pcdata block (segment 0x18 in libsa1: one per world) that the host world's
 * collision adapter (LIB src/world/sa1_coladapt_world.c) finds by its magic
 * words and enables: the material bits of a front-only polygon (COLPOLY.attr,
 * libsa1's SA1_MAT_FRONT_ONLY) and the guest address ranges of the land
 * objects libsa1 makes (its GRID segment, its moving objects' models): the
 * game's own land objects are never affected. While `on` is 0, the initial
 * value, the function is the original (the reference / exactness worlds).
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_funcs.h"
#include "sa1/shc_intrinsics.h"
#include "sa1/engine.h"
/* the shared header declares 8C029B00 as (csts *, Uint32, Uint32); R5 is the
 * land object (_OBJ_LANDCOLL.pObject, compared with the list key), R6 its
 * attribute (_OBJ_LANDCOLL.slAttribute, unused): see the caller 8C0777E0 */
#define func_8C029B00 func_8C029B00_hdr
#include "sa1/collision.h"
#undef func_8C029B00

/* collision triangle (64 bytes; COLPOLY of code_8C028620.c) */
typedef struct colpoly {
    struct colpoly *next;       /* 0x00 */
    struct colpoly *prev;       /* 0x04 */
    NJS_POINT3 center;          /* 0x08 */
    Float r;                    /* 0x14 */
    NJS_POINT3 v[3];            /* 0x18 vertices (world) */
    Uint32 attr;                /* 0x3C 0x800000: touched from both sides */
} COLPOLY;                      /* 0x40 */

/* polygon list of one land object (list head D_8C02D548; code_8C028BDC.c,
 * code_8C029400.c) */
typedef struct NEARLIST {
    struct NEARLIST *next;      /* 0x00 */
    void *prev;                 /* 0x04 */
    NJS_OBJECT *object;         /* 0x08 compared with _OBJ_LANDCOLL.pObject */
    Sint32 attr;                /* 0x0C */
    COLPOLY *head;              /* 0x10 */
    COLPOLY *tail;              /* 0x14 */
    Sint32 unk18;               /* 0x18 not accessed here */
    COLPOLY **near;             /* 0x1C first entry in the NEAR-poly buffer */
    Sint32 unk20;               /* 0x20 not accessed here */
    Sint32 near_num;            /* 0x24 number of NEAR-poly entries */
} NEARLIST;

/* one touch (B_8C6BACF0[]; copied to the csts fields of the same name) */
typedef struct {
    Angle angx;                 /* 0x00 */
    Angle angz;                 /* 0x04 */
    NJS_VECTOR normal;          /* 0x08 triangle normal */
    NJS_VECTOR normal2;         /* 0x14 normal facing the sphere */
    NJS_POINT3 onpoly;          /* 0x20 centre projected on the plane */
    NJS_POINT3 pshbk;           /* 0x2C centre pushed out to the radius */
    NJS_VECTOR anaspdh;         /* 0x38 speed along the plane */
    NJS_VECTOR anaspdv;         /* 0x44 speed along the normal */
    Float dist;                 /* 0x50 height of the centre above the plane */
    Float dist2;                /* 0x54 height after the move (selection key) */
} TOUCHINFO;                    /* 0x58 */

/* TOUCH-poly buffer entry */
typedef struct {
    Sint32 unk00;               /* 0x00 not accessed */
    COLPOLY poly;               /* 0x04 copy of the triangle */
    TOUCHINFO info;             /* 0x44 */
} TOUCHPOLY;                    /* 0x9C */

SA1_ASSERT_SIZE(COLPOLY, 0x40);
SA1_ASSERT_OFFSET(COLPOLY, v, 0x18);
SA1_ASSERT_OFFSET(COLPOLY, attr, 0x3C);
SA1_ASSERT_OFFSET(NEARLIST, object, 0x08);
SA1_ASSERT_OFFSET(NEARLIST, near, 0x1C);
SA1_ASSERT_OFFSET(NEARLIST, near_num, 0x24);
SA1_ASSERT_SIZE(TOUCHINFO, 0x58);
SA1_ASSERT_OFFSET(TOUCHINFO, onpoly, 0x20);
SA1_ASSERT_OFFSET(TOUCHINFO, dist2, 0x54);
SA1_ASSERT_SIZE(TOUCHPOLY, 0x9C);
SA1_ASSERT_OFFSET(TOUCHPOLY, info, 0x44);

extern NEARLIST *D_8C02D548;            /* polygon lists */
extern Sint32 B_8C73E400;               /* TOUCH-poly count */
extern TOUCHPOLY B_8C73E404[16];        /* TOUCH-poly buffer */
#if defined(TARGET_PC)
#define B_8C73E448 (&B_8C73E404[0].info)
#else
extern TOUCHINFO B_8C73E448[];          /* &B_8C73E404[0].info (own pool symbol) */
#endif
extern TOUCHINFO B_8C6BACF0[];          /* touches, read by the player code */
extern NJS_POINT3 B_8C73EDC8[3];        /* debug display triangle */
extern NJS_MATRIX B_8C73EDF8;           /* debug display: saved matrix */
extern NJS_MATRIX D_8C67C640;           /* base matrix */
extern NJS_POINT3COL D_8C196034;        /* debug display triangle (B_8C73EDC8) */
extern Sint8 tcrf_CollisionDisplayVar;  /* 8C752BDC */
extern const char D_8C112508[];         /* "WARNING: TOUCH-poly buf is FULL" */

/* triangle normal (not normalised) */
extern void func_8C027360(NJS_POINT3 *v0, NJS_POINT3 *v1, NJS_POINT3 *v2, NJS_VECTOR *n);

#if defined(SA1_GA)
#define SA1_FRONTONLY_MAGIC0 0x4C4E4F46 /* "FONL" (sa1_coladapt_world.c CW_FRONTONLY_*) */
#define SA1_FRONTONLY_MAGIC1 0x30304239 /* "9B00" */
typedef struct SA1_FRONTONLY {
    Uint32 magic[2];            /* 0x00 SA1_FRONTONLY_MAGIC0, SA1_FRONTONLY_MAGIC1 */
    Uint32 on;                  /* 0x08 0: the game's rule */
    Uint32 mask;                /* 0x0C COLPOLY.attr bits of a front-only polygon */
    Uint32 lo[2];               /* 0x10 guest address ranges [lo, hi) of the land objects (NJS_OBJECT) it applies to */
    Uint32 hi[2];               /* 0x18 */
    Uint32 skipped;             /* 0x20 front-only polygons not touched: the sphere started behind them */
    Uint32 topmask;             /* 0x24 COLPOLY.attr bits of a thin slab's top over a front-only underside */
    Uint32 topskipped;          /* 0x28 such tops not touched: the sphere started more than its radius behind them,
                                   moving towards their front (rising from under the slab) */
} SA1_FRONTONLY;
SA1_ASSERT_OFFSET(SA1_FRONTONLY, lo, 0x10);
SA1_ASSERT_OFFSET(SA1_FRONTONLY, skipped, 0x20);
SA1_ASSERT_OFFSET(SA1_FRONTONLY, topskipped, 0x28);
static SA1_FRONTONLY sa1_frontonly = { { SA1_FRONTONLY_MAGIC0, SA1_FRONTONLY_MAGIC1 }, 0, 0, { 0, 0 }, { 0, 0 }, 0, 0, 0 };
#endif

#define NJM_RAD_ANG(n) SA1_FTOI((n) * 10430.381f)
#define ROTATEX(m, a) if ((a) != 0) njRotateX((m), (a))
#define ROTATEY(m, a) if ((a) != 0) njRotateY((m), (a))
#define VEC_SET(v, a, b, c) ((v)->x = (a), (v)->y = (b), (v)->z = (c))
#define VEC_ZERO(v) ((v)->x = (v)->y = (v)->z = 0.0f)
/* side of q relative to the edge a-b in the XZ plane (cross product y) */
#define SIDE_XZ(a, b, q) \
    (((b)->z - (a)->z) * ((q)->x - (a)->x) - ((b)->x - (a)->x) * ((q)->z - (a)->z))

#ifdef NON_MATCHING
Sint32 func_8C029B00(csts *cp, NJS_OBJECT *object, Uint32 attr)
{
    NJS_POINT3 v[3];
    NJS_POINT3 rel;
    NJS_VECTOR spd;
    NJS_VECTOR n;
    NJS_VECTOR dir;
    NJS_POINT3 p;
    TOUCHINFO info;
    NJS_VECTOR tmp;
    NEARLIST *lp;
    COLPOLY **pp;
    TOUCHPOLY *tp;
    TOUCHINFO *ti;
    NJS_POINT3 *pv;
    Float *a;
    Float *b;
    Float *c;
    Float *d;
    Sint32 i;
    Sint32 found;
    Sint32 hit;
    Sint32 cont;
    Uint32 twoside;
    Sint16 sel;
    Float spdlen;
    Float step;
    Float r;
    Float t;
    Float c0;
    Float c1;
    Float c2;
    Float min;
    Float inv;
    Float x0;
    Float y0;
    Float z0;
    Float x;
    Float y;
    Float z;
#if defined(SA1_GA)
    Uint32 fomask;      /* the front-only material bits for this land object (0: the game's rule) */
    Uint32 fotop;       /* the thin-top material bits for this land object */
#endif

    if (!(cp->radius > 0.0f)) {
        return 0;
    }
    B_8C73E400 = 0;
    spdlen = njScalor(&cp->spd);
    step = cp->radius * 0.5f;
    tp = B_8C73E404;
    found = FALSE;
    for (lp = D_8C02D548; lp != NULL; lp = lp->next) {
        if (lp->object == object) {
            found = TRUE;
            break;
        }
    }
    if (!found) {
        return 0;
    }
#if defined(SA1_GA)
    fomask = 0;
    fotop = 0;
    if (sa1_frontonly.on) {
        Uint32 g = (Uint32)object;      /* the land object's guest address */
        if ((g >= sa1_frontonly.lo[0] && g < sa1_frontonly.hi[0]) || (g >= sa1_frontonly.lo[1] && g < sa1_frontonly.hi[1])) {
            fomask = sa1_frontonly.mask;
            fotop = sa1_frontonly.topmask;
        }
    }
#endif
    pp = lp->near;
    for (i = lp->near_num; i > 0; i--, pp++) {
        /* triangle relative to its first vertex */
        VEC_ZERO(&v[0]);
        pv = (*pp)->v;
        a = &pv[0].x;
        b = &pv[1].x;
        c = &pv[2].x;
        x0 = *a++;
        x = *b++ - x0;
        y0 = *a++;
        y = *b++ - y0;
        z0 = *a;
        z = *b - z0;
        d = &v[1].x + 3;
        *--d = z;
        *--d = y;
        *--d = x;
        x = *c++ - x0;
        y = *c++ - y0;
        z = *c - z0;
        d = &v[2].x + 3;
        *--d = z;
        *--d = y;
        *--d = x;
        rel.x = cp->pos.x - x0;
        rel.y = cp->pos.y - y0;
        rel.z = cp->pos.z - z0;
        spd = cp->spd;

        /* rotate the frame so that the triangle normal is +Y */
        njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
        func_8C027360(&v[0], &v[1], &v[2], &n);
        if (njScalor(&n) == 0.0f) {
            VEC_SET(&n, 0.0f, 1.0f, 0.0f);
        } else {
            njUnitVector(&n);
        }
        ROTATEX(NULL, -NJM_RAD_ANG(func_8C10D008(n.y)));
        ROTATEY(NULL, -NJM_RAD_ANG(atan2f(n.x, n.z)));
        njCalcPoint(NULL, &v[1], &v[1]);
        njCalcPoint(NULL, &v[2], &v[2]);
        njCalcPoint(NULL, &rel, &rel);
        njCalcPoint(NULL, &spd, &spd);
        r = cp->radius;
        twoside = (*pp)->attr & 0x800000;
        if (twoside && rel.y < 0.0f) {
            r *= -1.0f;
        }
#if defined(SA1_GA)
        /* a front-only polygon of a host world (see the header) is not touched by a sphere that starts behind it; the top
           of a thin slab over one is not touched by a sphere that starts more than its radius behind it moving towards
           its front (rising from under the slab: the slab's own underside holds it) */
        if (fomask != 0 && !twoside && rel.y < 0.0f) {
            Uint32 at = (*pp)->attr;
            if ((at & fomask) != 0) {
                sa1_frontonly.skipped++;
                njPopMatrix(1);         /* the push above; the loop's own pop below is skipped */
                continue;
            }
            if ((at & fotop) != 0 && rel.y < -cp->radius && spd.y > 0.0f) {
                sa1_frontonly.topskipped++;
                njPopMatrix(1);
                continue;
            }
        }
#endif

        /* sweep the centre from rel to rel + spd in steps of radius / 2 */
        hit = FALSE;
        dir = spd;
        if (njScalor(&dir) == 0.0f) {
            VEC_ZERO(&dir);
        } else {
            njUnitVector(&dir);
        }
        dir.x *= step;
        dir.y *= step;
        dir.z *= step;
        t = 0.0f;
        p = rel;
        for (;;) {
            cont = TRUE;
            if (!(spdlen > t)) {
                cont = FALSE;
                p = rel;
                p.x = rel.x + spd.x;
                p.y = rel.y + spd.y;
                p.z = rel.z + spd.z;
            }
            if (!(_builtin_fabsf(p.y) > cp->radius)) {
                c0 = SIDE_XZ(&v[0], &v[1], &p);
                c1 = SIDE_XZ(&v[1], &v[2], &p);
                c2 = SIDE_XZ(&v[2], &v[0], &p);
                if (!(0.0f > c0 * c1) && !(0.0f > c1 * c2) && !(0.0f > c0 * c2)) {
                    hit = TRUE;
                    break;
                }
            }
            if (!cont) {
                break;
            }
            p.x += dir.x;
            p.y += dir.y;
            p.z += dir.z;
            t += step;
        }

        if (hit) {
            if (tcrf_CollisionDisplayVar >= 0) {
                B_8C73EDC8[0] = (*pp)->v[0];
                B_8C73EDC8[1] = (*pp)->v[1];
                B_8C73EDC8[2] = (*pp)->v[2];
                B_8C73EDC8[0].x += n.x;
                B_8C73EDC8[1].x += n.x;
                B_8C73EDC8[2].x += n.x;
                B_8C73EDC8[0].y += n.y;
                B_8C73EDC8[1].y += n.y;
                B_8C73EDC8[2].y += n.y;
                B_8C73EDC8[0].z += n.z;
                B_8C73EDC8[1].z += n.z;
                B_8C73EDC8[2].z += n.z;
                njGetMatrix((NJS_MATRIX_PTR)&B_8C73EDF8);
                njPopMatrix(1);
                func_8C63D66C(&D_8C196034, 3, 0x20);
                njPushMatrix((NJS_MATRIX_PTR)&B_8C73EDF8);
            }
            info.dist = rel.y;
            info.dist2 = rel.y + spd.y;
            info.normal = n;
            if (twoside && rel.y < 0.0f) {
                info.normal2.x = -n.x;
                info.normal2.y = -n.y;
                info.normal2.z = -n.z;
            } else {
                info.normal2 = n;
            }
            info.angz = -NJM_RAD_ANG(atan2f(info.normal2.x, info.normal2.y));
            info.angx = NJM_RAD_ANG(func_8C10D058(info.normal2.z));
            /* back to world space */
            njInvertMatrix(NULL);
            dir.x = rel.x;
            dir.y = 0.0f;
            dir.z = rel.z;
            njCalcPoint(NULL, &dir, &p);
            info.onpoly.x = p.x + (*pp)->v[0].x;
            info.onpoly.y = p.y + (*pp)->v[0].y;
            info.onpoly.z = p.z + (*pp)->v[0].z;
            dir.y = r;
            njCalcPoint(NULL, &dir, &p);
            info.pshbk.x = p.x + (*pp)->v[0].x;
            info.pshbk.y = p.y + (*pp)->v[0].y;
            info.pshbk.z = p.z + (*pp)->v[0].z;
            /* the flipped tmp.y is overwritten below (dead stores in the
             * original too) */
            tmp = spd;
            if (twoside) {
                if (rel.y * spd.y < 0.0f) {
                    tmp.y = -spd.y;
                }
            } else {
                if (r * spd.y < 0.0f) {
                    tmp.y = -spd.y;
                }
            }
            tmp.x = spd.x;
            tmp.y = 0.0f;
            tmp.z = spd.z;
            njCalcPoint(NULL, &tmp, &p);
            info.anaspdh = p;
            tmp.x = 0.0f;
            tmp.y = spd.y;
            tmp.z = 0.0f;
            njCalcPoint(NULL, &tmp, &p);
            info.anaspdv = p;
            tp->poly = **pp;
            tp->info = info;
            tp++;
            if (++B_8C73E400 == 16) {
                if (tcrf_CollisionDisplayVar >= 0) {
                    njPrintC(0x00020016, D_8C112508);
                }
                /* leaves the loop without popping the matrix (as the original) */
                break;
            }
        }
        njPopMatrix(1);
    }
    if (B_8C73E400 < 1) {
        return 0;
    }

    /* select the touch with the lowest height after the move */
    min = B_8C73E404[0].info.dist2;
    sel = 0;
    for (i = 1, tp = &B_8C73E404[1]; i < B_8C73E400; i++, tp++) {
        if (min > tp->info.dist2) {
            min = tp->info.dist2;
            sel = (Sint16)i;
        }
    }
    for (i = B_8C73E400, tp = B_8C73E404, ti = B_8C6BACF0; i > 0; i--, tp++, ti++) {
        *ti = tp->info;
    }
    cp->find_count = (Uint16)B_8C73E400;
    cp->selected_nmb = (Uint16)sel;
    /* &B_8C73E404[sel].info, with a 16-bit byte offset (MULS.W + EXTS.W) */
    ti = (TOUCHINFO *)((Sint8 *)B_8C73E448 + (Sint16)(sel * sizeof(TOUCHPOLY)));
    cp->angx = ti->angx;
    cp->angz = ti->angz;
    cp->normal = ti->normal;
    cp->normal2 = ti->normal2;
    cp->anaspdh = ti->anaspdh;
    cp->anaspdv = ti->anaspdv;
    if (B_8C73E400 == 1) {
        cp->onpoly = ti->onpoly;
        cp->pshbk = ti->pshbk;
    } else {
        tp = B_8C73E404;
        cp->onpoly = tp->info.onpoly;
        cp->pshbk = tp->info.pshbk;
        tp++;
        for (i = B_8C73E400 - 1; i > 0; i--, tp++) {
            cp->onpoly.x += tp->info.onpoly.x;
            cp->onpoly.y += tp->info.onpoly.y;
            cp->onpoly.z += tp->info.onpoly.z;
            cp->pshbk.x += tp->info.pshbk.x;
            cp->pshbk.y += tp->info.pshbk.y;
            cp->pshbk.z += tp->info.pshbk.z;
        }
        inv = 1.0f / (Float)B_8C73E400;
        cp->onpoly.x *= inv;
        cp->onpoly.y *= inv;
        cp->onpoly.z *= inv;
        cp->pshbk.x *= inv;
        cp->pshbk.y *= inv;
        cp->pshbk.z *= inv;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C029B00(csts *cp, NJS_OBJECT *object, Uint32 attr))
#endif
