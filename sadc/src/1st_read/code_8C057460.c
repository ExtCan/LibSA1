#include "common.h"
#include "ninja.h"
/*
 * sa1/ninja_funcs.h declares func_8C63FE54 with three arguments; the routine
 * (njCollisionCheckSS) takes two sphere pointers (R4, R5 only). Keep the
 * header's declaration out of the way until the header is fixed.
 */
#define func_8C63FE54 func_8C63FE54_header_proto
#include "sa1/sa1.h"
#undef func_8C63FE54

/*
 * Per-node draw override for func_8C057460, an array terminated by
 * obj == NULL (game data, e.g. D_8C1B9C3C / D_8C1B9C4C / D_8C1BA83C).
 * Holds pointers: native layout on 64-bit hosts.
 */
typedef struct {
    NJS_OBJECT *obj;                    /* node of the drawn tree */
    void (*func)(NJS_OBJECT *obj);      /* NULL: node not drawn */
} OBJECT_DRAW_FUNC;

SA1_ASSERT_SIZE(OBJECT_DRAW_FUNC, 0x08);
SA1_ASSERT_OFFSET(OBJECT_DRAW_FUNC, func, 0x04);

/* Ninja NJS_SPHERE (njCollisionCheckSS argument); not in sa1/ninja_types.h yet */
typedef struct {
    NJS_POINT3 c;                       /* 0x00 centre */
    Float r;                            /* 0x0C radius */
} NJS_SPHERE_LOCAL;                     /* 0x10 */

SA1_ASSERT_SIZE(NJS_SPHERE_LOCAL, 0x10);

/* njCollisionCheckSS (8C63FE54): 1 when the spheres overlap (dist < r1 + r2) */
Sint32 func_8C63FE54(NJS_SPHERE_LOCAL *s1, NJS_SPHERE_LOCAL *s2);

extern void func_8C03700C(NJS_MODEL *model);

/*
 * Draws an object tree like njDrawObject (ZXY rotation order, no evalflags),
 * but a node with a model that is listed in `list` is drawn by its own
 * function instead of the default transform + func_8C03700C (func == NULL:
 * the node draws nothing). Siblings are walked by tail recursion.
 */
#ifdef NON_MATCHING
/*
 * Instruction-identical except two things that look like a
 * compiler-internal difference:
 *  - the original keeps the angle temporary in a 4-byte stack slot (stored
 *    and reloaded around each njRotateX/Y/Z call) AND turns the sibling call
 *    into a loop (tail recursion: "MOV R13,R4; BRA top; MOV.L @(48,R14),R14").
 *    SHC 5.1r03 only puts a scalar in that slot for `volatile` or
 *    address-taken locals, and both turn off tail-recursion elimination;
 *    a plain local gives the loop but keeps the angle in R5 (no frame);
 *    struct/array locals keep the loop but address the slot through a
 *    callee-saved base register; #pragma inline helpers get one slot each.
 *  - with `volatile`, the scratch registers of the 2nd/3rd angle loads and
 *    of two pool loads are R2/R3 swapped.
 */
void func_8C057460(NJS_OBJECT *obj, OBJECT_DRAW_FUNC *list)
{
    Sint32 i;
    OBJECT_DRAW_FUNC *p;
    volatile Angle ang;

    njPushMatrix(NULL);
    if (obj->model != NULL) {
        for (i = 0; list[i].obj != NULL; i++) {
            if (list[i].obj == obj) {
                break;
            }
        }
        p = &list[i];
        if (p->obj == NULL) {
            njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
            if ((ang = obj->ang[2]) != 0) {
                njRotateZ(NULL, ang);
            }
            if ((ang = obj->ang[0]) != 0) {
                njRotateX(NULL, ang);
            }
            if ((ang = obj->ang[1]) != 0) {
                njRotateY(NULL, ang);
            }
            njScaleV(NULL, (NJS_VECTOR *)obj->scl);
            func_8C03700C(obj->model);
        } else if (p->func != NULL) {
            p->func(obj);
        }
    } else {
        njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
        if ((ang = obj->ang[2]) != 0) {
            njRotateZ(NULL, ang);
        }
        if ((ang = obj->ang[0]) != 0) {
            njRotateX(NULL, ang);
        }
        if ((ang = obj->ang[1]) != 0) {
            njRotateY(NULL, ang);
        }
        njScaleV(NULL, (NJS_VECTOR *)obj->scl);
    }
    if (obj->child != NULL) {
        njPushMatrix(NULL);
        func_8C057460(obj->child, list);
        njPopMatrix(1);
    }
    njPopMatrix(1);
    if (obj->sibling != NULL) {
        func_8C057460(obj->sibling, list);
    }
}
#else
ASM_STUB(void func_8C057460(NJS_OBJECT *obj, OBJECT_DRAW_FUNC *list))
#endif

/* out = from + unit(to - from) * dist (out = from when to == from) */
void func_8C057586(NJS_POINT3 *from, NJS_POINT3 *to, Float dist, NJS_POINT3 *out)
{
    NJS_VECTOR v;

    v.x = to->x - from->x;
    v.y = to->y - from->y;
    v.z = to->z - from->z;
    if (njUnitVector(&v) == 0.0f) {
        *out = *from;
    } else {
        v.x *= dist;
        v.y *= dist;
        v.z *= dist;
        v.x += from->x;
        v.y += from->y;
        v.z += from->z;
        *out = v;
    }
}

/* Sphere/sphere hit test (njCollisionCheckSS) on two centres and radii */
Sint32 func_8C057682(NJS_POINT3 *p1, NJS_POINT3 *p2, Float r1, Float r2)
{
    NJS_SPHERE_LOCAL s[2];

    s[0].c.x = p1->x;
    s[0].c.y = p1->y;
    s[0].c.z = p1->z;
    s[0].r = r1;
    s[1].c.x = p2->x;
    s[1].c.y = p2->y;
    s[1].c.z = p2->z;
    s[1].r = r2;
    return func_8C63FE54(&s[0], &s[1]);
}

/* Unsigned 16-bit angle difference b - a (0..0xFFFF) */
Sint32 func_8C0576CC(Sint32 a, Sint32 b)
{
    Uint32 d;

    d = (Uint16)b;
    d |= 0x10000;
    d -= (Uint16)a;
    return (Uint16)d;
}

/* Absolute 16-bit angle distance (0..0x8000) */
Sint32 func_8C0576DA(Sint32 a, Sint32 b)
{
    Sint32 d;

    d = func_8C0576CC(a, b);
    if (d > 0x8000) {
        d = 0x10000 - d;
    }
    return d;
}

/* Turns ang towards target by at most spd (unsigned 16-bit angles) */
Sint32 func_8C0576F4(Sint32 ang, Sint32 target, Sint32 spd)
{
    Uint32 a;
    Uint32 t;
    Sint32 d;

    a = (Uint16)ang;
    t = (Uint16)target;
    if (a == t) {
        return a;
    }
    d = func_8C0576CC(a, t);
    if (d > 0x8000) {
        a = (Uint16)(a - spd);
        if (func_8C0576CC(a, t) < 0x8000) {
            a = t;
        }
    } else {
        a = (Uint16)(a + spd);
        if (func_8C0576CC(a, t) > 0x8000) {
            a = t;
        }
    }
    return (Uint16)a;
}
