/*
 * 1ST_READ 8C02B1A0-8C02B620: land collision, segment (ray) cast.
 *
 * 8C02B1A0 casts the segment pos .. pos + vec against the land polygons:
 * first against the object `obj` (if not NULL), then against every other
 * object of the landtable gather around pos (8C0522C0, radius 5). Dynamic
 * objects (with an owner task) are first culled in xz by their bounding
 * circle (model radius times the largest scale, plus |vec.xz| + 1). Returns
 * the first object hit (with the nearest hit within that object), NULL if
 * none. Caller: the stage 05 overlay (downward probe, obj NULL).
 *
 * 8C02B2D6 tests the segment against every polygon of the polygon lists in
 * D_8C02D548 that belong to `obj`: front-facing plane hit for 0 <= t <= 1,
 * then point-in-triangle by the three edge cross products. The nearest hit
 * is stored in the ray (point, normal, slope angles, attribute, t). Returns
 * obj if anything was hit, else NULL.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_funcs.h"
#include "sa1/collision.h"

/* segment cast query and result (the caller fills pos, vec and attr) */
typedef struct LANDRAY {
    NJS_POINT3 pos;             /* 0x00 start */
    NJS_VECTOR vec;             /* 0x0C start to end */
    NJS_POINT3 hit;             /* 0x18 hit point */
    NJS_VECTOR normal;          /* 0x24 polygon normal (not normalised) */
    Angle angx;                 /* 0x30 slope angle (8C066732 2nd argument) */
    Angle angz;                 /* 0x34 slope angle (8C066732 3rd argument) */
    Uint32 attr;                /* 0x38 in: attribute for obj; out: attribute of the object hit */
    Float t;                    /* 0x3C hit parameter along vec (0..1) */
} LANDRAY;                      /* 0x40 */

/* polygon node of the polygon lists (64-byte nodes, see code_8C028BDC.c) */
typedef struct LANDPOLY {
    struct LANDPOLY *next;      /* 0x00 */
    struct LANDPOLY *prev;      /* 0x04 */
    NJS_POINT3 center;          /* 0x08 bounding sphere centre */
    Float rr;                   /* 0x14 bounding sphere radius squared */
    NJS_POINT3 v[3];            /* 0x18 triangle vertices */
} LANDPOLY;

/* polygon list of one land object (list head D_8C02D548, see code_8C028BDC.c) */
typedef struct LANDPOLYLIST {
    struct LANDPOLYLIST *next;  /* 0x00 */
    Sint32 unk04;               /* 0x04 not accessed here */
    NJS_OBJECT *object;         /* 0x08 */
    Sint32 unk0C;               /* 0x0C not accessed here */
    LANDPOLY *head;             /* 0x10 */
} LANDPOLYLIST;

SA1_ASSERT_SIZE(LANDRAY, 0x40);
SA1_ASSERT_OFFSET(LANDRAY, hit, 0x18);
SA1_ASSERT_OFFSET(LANDRAY, attr, 0x38);
SA1_ASSERT_OFFSET(LANDRAY, t, 0x3C);
SA1_ASSERT_OFFSET(LANDPOLY, v, 0x18);
SA1_ASSERT_OFFSET(LANDPOLYLIST, object, 0x08);
SA1_ASSERT_OFFSET(LANDPOLYLIST, head, 0x10);

extern LANDPOLYLIST *D_8C02D548;

/* cross product: *out = *a x *b */
extern void func_8C066660(NJS_VECTOR *a, NJS_VECTOR *b, NJS_VECTOR *out);
/* slope angles of a normal v: *angx = atan2f(v->z, |v.xy|),
 * *angz = -atan2f(v->x, v->y) (func_8C10D0F8 argument order), as Angle */
extern void func_8C066732(NJS_VECTOR *v, Angle *angx, Angle *angz);

NJS_OBJECT *func_8C02B2D6(LANDRAY *ray, NJS_OBJECT *obj, Uint32 attr);

#ifdef NON_MATCHING
/*
 * Functionally complete. Does not match: the original keeps the obj
 * parameter in the stack slot of the loop counter (stored at entry, reloaded
 * for the test and the call; the same variable is then the counter), which
 * moves the register allocation (ret in R5, the three B_8C754EF4[i] field
 * pointers in R8/R9/R10, FR12/FR13 swapped). Declaring the parameter as a
 * Sint32 that is reused as the counter reproduces the entry spill, the
 * stack layout and the float registers with SHC (ret in R5 and the order of
 * the field pointers still differ), but a pointer kept in an integer is
 * forbidden by docs/portability.md, so this body stays portable.
 */
NJS_OBJECT *func_8C02B1A0(LANDRAY *ray, NJS_OBJECT *obj)
{
    NJS_POINT3 d;
    NJS_OBJECT *skip;
    NJS_VECTOR *vec;
    Sint32 i;
    _OBJ_LANDCOLL *lc;
    NJS_POINT3 *pos;
    NJS_OBJECT *o;
    NJS_OBJECT *ret;
    Float r;
    Float s;

    pos = &ray->pos;
    vec = &ray->vec;
    skip = obj;
    if (obj != NULL) {
        if ((ret = func_8C02B2D6(ray, obj, ray->attr)) != NULL) {
            return ret;
        }
    }
    func_8C0522C0(pos->x, pos->y, pos->z, 5.0f);
    if (B_8C754EF0 == 0) {
        return NULL;
    }
    lc = B_8C754EF4;
    for (i = 0; i < B_8C754EF0; i++) {
        o = lc[i].pObject;
        if (o == NULL || o == skip) {
            continue;
        }
        if (lc[i].pTask != NULL) {
            r = njSqrt(SA1_FMAC(vec->x, vec->x, vec->z * vec->z));
            s = o->scl[0];
            if (o->scl[1] > s) {
                s = o->scl[1];
            }
            if (o->scl[2] > s) {
                s = o->scl[2];
            }
            /* SHC: "r += o->model->r * s" gives the original FMAC into r */
            r = SA1_FMAC(o->model->r, s, r);
            d.x = o->pos[0] - pos->x;
            d.z = o->pos[2] - pos->z;
            s = njSqrt(SA1_FMAC(d.x, d.x, d.z * d.z));
            if (s > r + 1.0f) {
                continue;
            }
        }
        if ((ret = func_8C02B2D6(ray, o, lc[i].slAttribute)) != NULL) {
            return ret;
        }
    }
    return NULL;
}
#else
ASM_STUB(NJS_OBJECT *func_8C02B1A0(LANDRAY *ray, NJS_OBJECT *obj))
#endif

NJS_OBJECT *func_8C02B2D6(LANDRAY *ray, NJS_OBJECT *obj, Uint32 attr)
{
    NJS_POINT3 hit;
    NJS_VECTOR w;
    NJS_VECTOR n;
    NJS_VECTOR a;
    NJS_VECTOR b;
    NJS_VECTOR c;
    NJS_OBJECT *ret;
    LANDPOLYLIST *lp;
    LANDPOLY *np;
    NJS_VECTOR *vec;
    NJS_POINT3 *pos;
    NJS_POINT3 *v0;
    NJS_POINT3 *v1;
    NJS_POINT3 *v2;
    Float dn;
    Float t;

    pos = &ray->pos;
    vec = &ray->vec;
    ret = NULL;
    lp = D_8C02D548;
    while (lp != NULL) {
        if (lp->object == obj) {
            for (np = lp->head; np != NULL; np = np->next) {
                v0 = np->v;
                v1 = &np->v[1];
                v2 = &np->v[2];
                a.x = v1->x - v0->x;
                a.y = v1->y - v0->y;
                a.z = v1->z - v0->z;
                b.x = v2->x - v0->x;
                b.y = v2->y - v0->y;
                b.z = v2->z - v0->z;
                n.x = a.y * b.z - a.z * b.y;
                n.y = a.z * b.x - a.x * b.z;
                n.z = a.x * b.y - a.y * b.x;
                /* front faces only */
                if (!(-0.000001f > (dn = njInnerProduct(vec, &n)))) {
                    continue;
                }
                w.x = pos->x - v0->x;
                w.y = pos->y - v0->y;
                w.z = pos->z - v0->z;
                if (!((t = njInnerProduct(&w, &n)) > 0.000001f)) {
                    continue;
                }
                t = -(t / dn);
                if (t > 1.0f) {
                    continue;
                }
                if (0.0f > t) {
                    continue;
                }
                hit.x = SA1_FMAC(t, vec->x, pos->x);
                hit.y = SA1_FMAC(t, vec->y, pos->y);
                hit.z = SA1_FMAC(t, vec->z, pos->z);
                /* inside test: the hit point is left of all three edges */
                a.x = v0->x - hit.x;
                a.y = v0->y - hit.y;
                a.z = v0->z - hit.z;
                b.x = v1->x - hit.x;
                b.y = v1->y - hit.y;
                b.z = v1->z - hit.z;
                func_8C066660(&a, &b, &w);
                if (!(njInnerProduct(&w, &n) > 0.000001f)) {
                    continue;
                }
                c.x = v2->x - hit.x;
                c.y = v2->y - hit.y;
                c.z = v2->z - hit.z;
                func_8C066660(&b, &c, &w);
                if (!(njInnerProduct(&w, &n) > 0.000001f)) {
                    continue;
                }
                func_8C066660(&c, &a, &w);
                if (!(njInnerProduct(&w, &n) > 0.000001f)) {
                    continue;
                }
                if (ret == NULL || ray->t > t) {
                    ret = obj;
                    ray->hit.x = hit.x;
                    ray->hit.y = hit.y;
                    ray->hit.z = hit.z;
                    ray->normal.x = n.x;
                    ray->normal.y = n.y;
                    ray->normal.z = n.z;
                    func_8C066732(&n, &ray->angx, &ray->angz);
                    ray->attr = attr;
                    ray->t = t;
                }
            }
        }
        lp = lp->next;
    }
    return ret;
}
