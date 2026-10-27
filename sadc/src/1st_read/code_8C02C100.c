/*
 * 1ST_READ 8C02C100 (land collision TU 8C027360-8C02CE80): six-direction
 * probe of one land object (SADX xyyzzxsdwstr layout), the landtable (model)
 * counterpart of 8C029400, which probes the dynamic "FD" objects. No direct
 * caller in 1ST_READ (reached through a pointer or from an overlay).
 *
 * obj's meshsets are unpacked one polygon / strip at a time into a MESHBUF
 * (8C0273A2..8C0275E0) under obj's local transform. Every triangle is tested
 * three times: projected along Z, X and Y. When the probe position projects
 * inside the triangle, the plane is intersected with the axis line through
 * the position and the nearest hit on each side is kept in hit[4]/hit[5]
 * (z), hit[0]/hit[1] (x), hit[2]/hit[3] (y). Unlike 8C029400 it does not
 * preset onpos nor write objatt. Returns 1 when any hit was stored, 0
 * otherwise (also 0, without popping the matrix, for a meshset of unknown
 * type).
 *
 * NON_MATCHING. This TU was built with SHC 5.0r31/5.0r32 (MOV.W constants,
 * see code_8C028480.c); test with --ver=5.0r32 -define=NON_MATCHING. With
 * that compiler the control flow, the stack frame (0x3084), the switch, the
 * post-increment distance test and the n.x CSE across each plane test (it
 * needs the block-scoped NJS_VECTOR n) agree; 926 of 944 instructions, the
 * rest is register allocation and spilling: the original keeps b/c in
 * R12/R11 and the constant 1 in R10 (here R12, b/c in R11/R10, hp of the Y
 * test in R13), 0.0001f/1.0f in FR12/FR13 (swapped here), spills the
 * rotation angle into p1's stack slot (here kept in R5; "volatile" gives
 * the spill but a slot of its own), spills other FP temporaries and orders
 * the spilled scalars differently (obj sp+16, p0 sp+12, x/z/y sp+0..8).
 * 5.1r03 additionally builds the 16-bit constants with MOV/SHLL8.
 */
#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/* one probe result (SADX xssunit); same layout as in code_8C029400.c */
typedef struct xssunit {
    Sint32 findflag;            /* 0x00 1 = polygon found */
    Sint32 objatt;              /* 0x04 (not written here) */
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

/* Polygon / strip vertex buffer, same as in code_8C027360.c (local at sp+0x4C) */
typedef struct {
    Sint32 type;                /* 0x00 meshset type (type_matId & 0xC000) */
    Sint32 n;                   /* 0x04 vertex count */
    Sint16 *meshes;             /* 0x08 read cursor in NJS_MESHSET.meshes */
    NJS_MODEL *model;           /* 0x0C points source */
    NJS_POINT3 buf[0x400];      /* 0x10 gathered vertices */
} MESHBUF;                      /* 0x3010 */

SA1_ASSERT_SIZE(MESHBUF, 0x3010);

extern Float D_8C112460[];      /* squared ranges, indexed by attr bits (see code_8C028100.c) */

/*
 * The distance pre-test of the original reads three Floats starting at &p0,
 * the pointer variable itself (a bug: p0 was meant). On the DC those are the
 * stack slots of p0, obj and p2, i.e. 0x8Cxxxxxx bit patterns, which read as
 * Floats between -2^-103 and -2^-101; the test is therefore in effect
 * |pos|^2 > range. It is not exactly pos - 0: under round-to-zero a negative
 * coordinate minus such a value moves one ulp towards zero (a positive one
 * is unchanged, and the square of the difference at pos = 0 flushes to 0).
 * Every 0x8Cxxxxxx value gives the same differences for any coordinate of
 * magnitude above 2^-77, so the PC build uses -2^-103 (0x8C000000) for all
 * three words.
 */
#ifdef TARGET_PC
#define DC_PTR_FLOAT            sa1_f32(0x8C000000u)
#define DC_STACK_FLOATS(pp)     ((Float[3]){ DC_PTR_FLOAT, DC_PTR_FLOAT, DC_PTR_FLOAT })
#else
#define DC_STACK_FLOATS(pp)     ((Float *)(pp))
#endif

/* triangle normal (not normalised), see code_8C027360.c */
extern void func_8C027360(NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *c, NJS_VECTOR *nrm);
extern Bool func_8C0273A2(MESHBUF *mb);
extern Bool func_8C0274B8(MESHBUF *mb);
extern Bool func_8C0275E0(MESHBUF *mb, NJS_OBJECT *obj);

#ifdef NON_MATCHING
Sint32 func_8C02C100(xyyzzxsdwstr *xyzp, NJS_OBJECT *obj, Uint32 attr)
{
    NJS_MODEL model;
    MESHBUF mb;
    NJS_VECTOR *np;
    Sint32 ret;
    Float range;
    Sint32 cnt;
    Bool flip;
    Sint32 i;
    NJS_POINT3 *p0;
    NJS_POINT3 *p1;
    NJS_POINT3 *p2;
    NJS_POINT3 *a;
    NJS_POINT3 *b;
    NJS_POINT3 *c;
    xssunit *hp;
    Float *s;
    Float *v;
    Float dx, dy, dz;
    Float c0, c1, c2;
    Float d;
    Float x, y, z;

    ret = 0;
    hp = xyzp->hit;
    i = 6;
    while (i > 0) {
        hp->findflag = 0;
        hp++;
        i--;
    }
    model = *obj->model;
    mb.model = &model;
    range = D_8C112460[(attr & 0x60000000) >> 13];
    njPushMatrix(NULL);
    njUnitMatrix(NULL);
    if (!(obj->evalflags & 1)) {
        njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
    }
    if (!(obj->evalflags & 2)) {
        Angle ang;

        if ((ang = obj->ang[2]) != 0) {
            njRotateZ(NULL, ang);
        }
        if ((ang = obj->ang[1]) != 0) {
            njRotateY(NULL, ang);
        }
        if ((ang = obj->ang[0]) != 0) {
            njRotateX(NULL, ang);
        }
    }
    if (!(obj->evalflags & 4)) {
        njScaleV(NULL, (NJS_VECTOR *)obj->scl);
    }
    for (; model.nbMeshset != 0; model.meshsets++, model.nbMeshset--) {
        switch (mb.type = model.meshsets->type_matId & 0xC000) {
        case 0x0000:
            mb.n = 3;
            break;
        case 0x4000:
            mb.n = 4;
            break;
        case 0x8000:
        case 0xC000:
            break;
        default:
            return 0;
        }
        cnt = model.meshsets->nbMesh;
        mb.meshes = model.meshsets->meshes;
        for (; cnt != 0; cnt--) {
            if ((obj->evalflags & 7) == 7) {
                flip = func_8C0274B8(&mb);
            } else if ((obj->evalflags & 6) == 6) {
                flip = func_8C0275E0(&mb, obj);
            } else {
                flip = func_8C0273A2(&mb);
            }
            p0 = &mb.buf[0];
            p1 = &mb.buf[1];
            p2 = &mb.buf[2];
            for (i = 0; i < mb.n - 2; i++, p0 = p1, p1 = p2, p2++) {
                s = &xyzp->pos.x;
                v = DC_STACK_FLOATS(&p0);
                dx = *s++ - *v++;
                dy = *s++ - *v++;
                dz = *s - *v;
                /* FMUL dy*dy, then FMAC dx*dx and FMAC dz*dz (fused) */
                if (SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy)) > range) {
                    continue;
                }
                if (((i + flip) & 1) == 0) {
                    a = p0;
                    b = p1;
                    c = p2;
                } else {
                    a = p0;
                    b = p2;
                    c = p1;
                }

                /*
                 * The edge and threshold tests are written !(p < k): the
                 * SH-4 code only skips on k > p, so a NaN product passes.
                 */

                /* along Z */
                c0 = (b->x - a->x) * (xyzp->pos.y - a->y) - (b->y - a->y) * (xyzp->pos.x - a->x);
                c1 = (c->x - b->x) * (xyzp->pos.y - b->y) - (c->y - b->y) * (xyzp->pos.x - b->x);
                c2 = (a->x - c->x) * (xyzp->pos.y - c->y) - (a->y - c->y) * (xyzp->pos.x - c->x);
                if (!(c0 * c1 < 0.0f) && !(c1 * c2 < 0.0f) && !(c0 * c2 < 0.0f)) {
                    NJS_VECTOR n;

                    func_8C027360(a, b, c, &n);
                    d = -n.x * a->x - n.y * a->y - n.z * a->z;
                    if (!(n.z * n.z < 0.0001f)) {
                        z = (-n.x * xyzp->pos.x - n.y * xyzp->pos.y - d) / n.z;
                        hp = NULL;
                        if (xyzp->pos.z > z) {
                            if (xyzp->hit[4].findflag == 0 || z > xyzp->hit[4].onpos) {
                                hp = &xyzp->hit[4];
                            }
                        } else {
                            if (xyzp->hit[5].findflag == 0 || xyzp->hit[5].onpos > z) {
                                hp = &xyzp->hit[5];
                            }
                        }
                        if (hp != NULL) {
                            if (njScalor(&n) == 0.0f) {
                                np = &n;
                                np->x = 0.0f;
                                np->y = 1.0f;
                                np->z = 0.0f;
                            } else {
                                njUnitVector(&n);
                            }
                            ret = 1;
                            hp->findflag = 1;
                            hp->onpos = z;
                            hp->normal = n;
                            hp->angz = -SA1_FTOI(atan2f(n.x, n.y) * 10430.381f);
                            hp->angx = SA1_FTOI(func_8C10D058(n.z) * 10430.381f);
                        }
                    }
                }

                /* along X */
                c0 = (b->y - a->y) * (xyzp->pos.z - a->z) - (b->z - a->z) * (xyzp->pos.y - a->y);
                c1 = (c->y - b->y) * (xyzp->pos.z - b->z) - (c->z - b->z) * (xyzp->pos.y - b->y);
                c2 = (a->y - c->y) * (xyzp->pos.z - c->z) - (a->z - c->z) * (xyzp->pos.y - c->y);
                if (!(c0 * c1 < 0.0f) && !(c1 * c2 < 0.0f) && !(c0 * c2 < 0.0f)) {
                    NJS_VECTOR n;

                    func_8C027360(a, b, c, &n);
                    d = -n.x * a->x - n.y * a->y - n.z * a->z;
                    if (!(n.x * n.x < 0.0001f)) {
                        x = (-n.y * xyzp->pos.y - n.z * xyzp->pos.z - d) / n.x;
                        hp = NULL;
                        if (xyzp->pos.x > x) {
                            if (xyzp->hit[0].findflag == 0 || x > xyzp->hit[0].onpos) {
                                hp = &xyzp->hit[0];
                            }
                        } else {
                            if (xyzp->hit[1].findflag == 0 || xyzp->hit[1].onpos > x) {
                                hp = &xyzp->hit[1];
                            }
                        }
                        if (hp != NULL) {
                            if (njScalor(&n) == 0.0f) {
                                np = &n;
                                np->x = 0.0f;
                                np->y = 1.0f;
                                np->z = 0.0f;
                            } else {
                                njUnitVector(&n);
                            }
                            ret = 1;
                            hp->findflag = 1;
                            hp->onpos = x;
                            hp->normal = n;
                            hp->angz = -SA1_FTOI(atan2f(n.x, n.y) * 10430.381f);
                            hp->angx = SA1_FTOI(func_8C10D058(n.z) * 10430.381f);
                        }
                    }
                }

                /* along Y */
                c0 = (b->z - a->z) * (xyzp->pos.x - a->x) - (b->x - a->x) * (xyzp->pos.z - a->z);
                c1 = (c->z - b->z) * (xyzp->pos.x - b->x) - (c->x - b->x) * (xyzp->pos.z - b->z);
                c2 = (a->z - c->z) * (xyzp->pos.x - c->x) - (a->x - c->x) * (xyzp->pos.z - c->z);
                if (!(c0 * c1 < 0.0f) && !(c1 * c2 < 0.0f) && !(c0 * c2 < 0.0f)) {
                    NJS_VECTOR n;

                    func_8C027360(a, b, c, &n);
                    d = -n.x * a->x - n.y * a->y - n.z * a->z;
                    if (!(n.y * n.y < 0.0001f)) {
                        y = (-n.z * xyzp->pos.z - n.x * xyzp->pos.x - d) / n.y;
                        hp = NULL;
                        if (xyzp->pos.y > y) {
                            if (xyzp->hit[2].findflag == 0 || y > xyzp->hit[2].onpos) {
                                hp = &xyzp->hit[2];
                            }
                        } else {
                            if (xyzp->hit[3].findflag == 0 || xyzp->hit[3].onpos > y) {
                                hp = &xyzp->hit[3];
                            }
                        }
                        if (hp != NULL) {
                            if (njScalor(&n) == 0.0f) {
                                np = &n;
                                np->x = 0.0f;
                                np->y = 1.0f;
                                np->z = 0.0f;
                            } else {
                                njUnitVector(&n);
                            }
                            ret = 1;
                            hp->findflag = 1;
                            hp->onpos = y;
                            hp->normal = n;
                            hp->angz = -SA1_FTOI(atan2f(n.x, n.y) * 10430.381f);
                            hp->angx = SA1_FTOI(func_8C10D058(n.z) * 10430.381f);
                        }
                    }
                }
            }
        }
    }
    njPopMatrix(1);
    return ret;
}
#else
ASM_STUB(Sint32 func_8C02C100(xyyzzxsdwstr *xyzp, NJS_OBJECT *obj, Uint32 attr))
#endif
