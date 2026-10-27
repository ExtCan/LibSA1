#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 8C02770E: land collision, sphere (csts) against the polygons of one
 * NJS_OBJECT node. Called for every node of an object tree by 8C028280
 * (recursive, current matrix = node matrix) and 8C02833A (root), which then
 * reduces the hit list with 8C028100 into the csts.
 *
 * Every meshset is unpacked polygon by polygon into a strip buffer by
 * 8C0273A2 ("WARNING: STRIP buf is FULL", vertices through the current
 * matrix). For each triangle of the strip: bounding-sphere reject, degenerate
 * reject, then the triangle is moved to v[0] and rotated so that its normal
 * is +Y, and the sphere centre is swept along csts.spd in steps of
 * hl->step (2D point-in-triangle test in XZ while |y| <= radius). Hits are
 * appended to hl->list (max 16 entries) in world space.
 *
 * NON_MATCHING: the TU 8C027360-8C02CE80 was built by a different compiler
 * configuration than the rest of the game (docs/compiler.md: MOV.W constants,
 * i.e. -extra=a=800, and 09 00 alignment fill). It also selects post-increment
 * addressing for struct fields on its own (FMOV.S @Rn+ then ADD #-12,Rn to
 * reuse the base, e.g. 8C027860..8C0278C4) and keeps p[0] in registers across
 * the stores into tri[]; SHC 5.0r26..5.1r13 never do that for p->x/p->y/p->z
 * with any flag tried (-speed/-size/-optimize, -extra bits, -loop, -inline),
 * so the code cannot be reproduced with the toolchain in tools/shc.
 * The C below is complete; control flow, statement order, stack layout of
 * the aggregates and every operation follow the original.
 */

/* strip buffer filled by 8C0273A2 (local of 8C02770E at sp+0xB4) */
typedef struct {
    Sint32 type;                /* 0x00 meshset type (type_matId & 0xC000) */
    Sint32 nbVertex;            /* 0x04 vertices in v[] (3 tri, 4 quad, strip length) */
    Sint16 *meshes;             /* 0x08 read cursor in NJS_MESHSET.meshes */
    NJS_MODEL *model;           /* 0x0C points source */
    NJS_POINT3 v[1024];         /* 0x10 vertices through the current matrix */
} CL_STRIPBUF;                  /* 0x3010 */

/* one hit polygon (B_8C6BACF0[16], stride 0x58); reduced by 8C028100 */
typedef struct {
    Angle angx;                 /* 0x00 -> csts.angx */
    Angle angz;                 /* 0x04 -> csts.angz */
    NJS_POINT3 normal;          /* 0x08 -> csts.normal */
    NJS_POINT3 normal2;         /* 0x14 -> csts.normal2 (flipped towards the sphere) */
    NJS_POINT3 onpoly;          /* 0x20 sphere centre projected on the plane */
    NJS_POINT3 onpoly2;         /* 0x2C same point lifted by the (signed) radius */
    NJS_POINT3 anaspdh;         /* 0x38 -> csts.anaspdh (spd parallel to the plane) */
    NJS_POINT3 anaspdv;         /* 0x44 -> csts.anaspdv (spd along the normal) */
    Float height;               /* 0x50 plane distance before the move (8C028100 picks min |height|) */
    Float height2;              /* 0x54 plane distance after the move */
} CL_HITPOLY;                   /* 0x58 */

/* hit list header (local of 8C02833A at sp+4) */
typedef struct {
    Sint32 hit;                 /* 0x00 set when a polygon was hit */
    Sint32 count;               /* 0x04 entries in list (max 16) */
    Sint32 tested;              /* 0x08 polygons that passed the bounding test */
    Float unk0C;                /* 0x0C D_8C112460[] selected by the attribute */
    Float len;                  /* 0x10 njScalor(&cp->spd) */
    Float step;                 /* 0x14 cp->radius * 0.5f, sweep step */
    CL_HITPOLY *list;           /* 0x18 B_8C6BACF0 */
} CL_HITLIST;                   /* 0x1C */

SA1_ASSERT_SIZE(CL_STRIPBUF, 0x3010);
SA1_ASSERT_SIZE(CL_HITPOLY, 0x58);
SA1_ASSERT_SIZE(CL_HITLIST, 0x1C);

/* material attrflags bit that makes a polygon collide from both sides
 * (NJD_FLAG_DOUBLE_SIDE position in the Ninja headers, to verify) */
#define CL_ATTR_BOTHSIDE 0x00800000

/* x*x + y*y + z*z as the SH-4 evaluates it: FMUL y*y, FMAC x*x, FMAC z*z */
#define CL_LEN2(x, y, z) SA1_FMAC((z), (z), SA1_FMAC((x), (x), (y) * (y)))

extern NJS_MATRIX D_8C67C640;   /* unit matrix */

/* (b - a) x (a - c) into n, not normalised */
ASM_STUB(void func_8C027360(NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *c, NJS_POINT3 *n))
/* unpack the next polygon of sb->meshes into sb->v; returns the strip's
 * winding flag (bit 15 of the strip length) */
ASM_STUB(Sint32 func_8C0273A2(CL_STRIPBUF *sb))

#ifdef NON_MATCHING
void func_8C02770E(csts *cp, NJS_OBJECT *obj, CL_HITLIST *hl)
{
    CL_STRIPBUF strip;          /* sp+0xB4 */
    NJS_POINT3 tri[3];          /* sp+0x90 triangle relative to p[0] (tri[0] = origin) */
    NJS_POINT3 pos;             /* sp+0x84 sphere centre relative to p[0] */
    NJS_POINT3 spd;             /* sp+0x78 */
    NJS_POINT3 v1;              /* sp+0x6C */
    NJS_POINT3 normal;          /* sp+0x60 */
    NJS_POINT3 calc;            /* sp+0x54 njCalcPoint output */
    NJS_POINT3 v2;              /* sp+0x48 */
    NJS_POINT3 step;            /* sp+0x3C */
    NJS_POINT3 pt;              /* sp+0x30 swept point */
    NJS_MODEL *model;
    NJS_MESHSET *meshset;
    NJS_POINT3 *p;
    CL_HITPOLY *hp;
    Sint32 nbMeshset;
    Sint32 nbMesh;
    Sint32 i;
    Sint32 flip;
    Sint32 hit;
    Sint32 cont;
    Uint32 bothside;
    Angle ang;
    Angle angx;
    Angle angy;
    Float max;
    Float d;
    Float dx, dy, dz;
    Float r;
    Float t;
    Float c0, c1, c2;

    model = obj->model;
    strip.model = model;
    for (nbMeshset = model->nbMeshset, meshset = model->meshsets;
         nbMeshset > 0 && hl->count < 16; nbMeshset--, meshset++) {
        strip.type = meshset->type_matId & 0xC000;
        switch (strip.type) {
        case 0x0000:
            strip.nbVertex = 3;
            break;
        case 0x4000:
            strip.nbVertex = 4;
            break;
        case 0x8000:
        case 0xC000:
            break;
        default:
            continue;
        }
        nbMesh = meshset->nbMesh;
        strip.meshes = meshset->meshes;
        while (nbMesh != 0 && hl->count < 16) {
            flip = func_8C0273A2(&strip);
            tri[0].x = tri[0].y = tri[0].z = 0.0f;
            for (i = 0, p = strip.v; i < strip.nbVertex - 2; i++, p++) {
                /* bounding test: longest edge + |spd| + radius */
                dx = p[1].x - p[0].x;
                dy = p[1].y - p[0].y;
                dz = p[1].z - p[0].z;
                max = CL_LEN2(dx, dy, dz);
                dx = p[2].x - p[1].x;
                dy = p[2].y - p[1].y;
                dz = p[2].z - p[1].z;
                d = CL_LEN2(dx, dy, dz);
                if (d > max) {
                    max = d;
                }
                dx = p[0].x - p[2].x;
                dy = p[0].y - p[2].y;
                dz = p[0].z - p[2].z;
                d = CL_LEN2(dx, dy, dz);
                if (d > max) {
                    max = d;
                }
                max += SA1_FMAC(cp->radius, cp->radius, CL_LEN2(cp->spd.x, cp->spd.y, cp->spd.z));
                dx = cp->pos.x - p[0].x;
                dy = cp->pos.y - p[0].y;
                dz = cp->pos.z - p[0].z;
                if (CL_LEN2(dx, dy, dz) > max) {
                    continue;
                }
                dx = cp->pos.x - p[1].x;
                dy = cp->pos.y - p[1].y;
                dz = cp->pos.z - p[1].z;
                if (CL_LEN2(dx, dy, dz) > max) {
                    continue;
                }
                dx = cp->pos.x - p[2].x;
                dy = cp->pos.y - p[2].y;
                dz = cp->pos.z - p[2].z;
                if (CL_LEN2(dx, dy, dz) > max) {
                    continue;
                }
                hl->tested++;
                /* strip triangles alternate their winding */
                if (((i + flip) & 1) == 0) {
                    tri[1].x = p[1].x - p[0].x;
                    tri[1].y = p[1].y - p[0].y;
                    tri[1].z = p[1].z - p[0].z;
                    tri[2].x = p[2].x - p[0].x;
                    tri[2].y = p[2].y - p[0].y;
                    tri[2].z = p[2].z - p[0].z;
                } else {
                    tri[1].x = p[2].x - p[0].x;
                    tri[1].y = p[2].y - p[0].y;
                    tri[1].z = p[2].z - p[0].z;
                    tri[2].x = p[1].x - p[0].x;
                    tri[2].y = p[1].y - p[0].y;
                    tri[2].z = p[1].z - p[0].z;
                }
                if (tri[0].x == tri[1].x && tri[0].y == tri[1].y && tri[0].z == tri[1].z) {
                    continue;
                }
                if (tri[0].x == tri[2].x && tri[0].y == tri[2].y && tri[0].z == tri[2].z) {
                    continue;
                }
                if (tri[1].x == tri[2].x && tri[1].y == tri[2].y && tri[1].z == tri[2].z) {
                    continue;
                }
                pos.x = cp->pos.x - p[0].x;
                pos.y = cp->pos.y - p[0].y;
                pos.z = cp->pos.z - p[0].z;
                spd.x = cp->spd.x;
                spd.y = cp->spd.y;
                spd.z = cp->spd.z;
                func_8C027360(&tri[0], &tri[1], &tri[2], &normal);
                if (njScalor(&normal) == 0.0f) {
                    normal.x = 0.0f;
                    normal.y = 1.0f;
                    normal.z = 0.0f;
                } else {
                    njUnitVector(&normal);
                }
                /* polygon space: normal along +Y */
                angx = SA1_FTOI(func_8C10D008(normal.y) * 10430.381f);
                angy = SA1_FTOI(atan2f(normal.x, normal.z) * 10430.381f);
                njPushMatrix(D_8C67C640);
                if ((ang = -angx) != 0) {
                    njRotateX(0, ang);
                }
                if ((ang = -angy) != 0) {
                    njRotateY(0, ang);
                }
                njCalcPoint(0, &tri[1], &calc);
                tri[1] = calc;
                njCalcPoint(0, &tri[2], &calc);
                tri[2] = calc;
                njCalcPoint(0, &pos, &calc);
                pos = calc;
                njCalcPoint(0, &spd, &calc);
                spd = calc;
                bothside = strip.model->mats[meshset->type_matId & ~0xC000].attrflags & CL_ATTR_BOTHSIDE;
                r = cp->radius;
                if (bothside) {
                    if (0.0f > pos.y) {
                        r *= -1.0f;
                    }
                }
                step = spd;
                hit = 0;
                if (njScalor(&step) == 0.0f) {
                    step.x = step.y = step.z = 0.0f;
                } else {
                    njUnitVector(&step);
                }
                step.x *= hl->step;
                step.y *= hl->step;
                step.z *= hl->step;
                t = 0.0f;
                pt = pos;
                for (;;) {
                    cont = 1;
                    if (!(hl->len > t)) {
                        /* last step: end of the move */
                        pt.x = pos.x + spd.x;
                        pt.y = pos.y + spd.y;
                        pt.z = pos.z + spd.z;
                        cont = 0;
                    }
                    if (!(_builtin_fabsf(pt.y) > cp->radius)) {
                        c0 = (tri[1].z - tri[0].z) * (pt.x - tri[0].x) - (tri[1].x - tri[0].x) * (pt.z - tri[0].z);
                        c1 = (tri[2].z - tri[1].z) * (pt.x - tri[1].x) - (tri[2].x - tri[1].x) * (pt.z - tri[1].z);
                        c2 = (tri[0].z - tri[2].z) * (pt.x - tri[2].x) - (tri[0].x - tri[2].x) * (pt.z - tri[2].z);
                        if (!(0.0f > c0 * c1) && !(0.0f > c1 * c2) && !(0.0f > c0 * c2)) {
                            hit = 1;
                            break;
                        }
                    }
                    if (!cont) {
                        break;
                    }
                    pt.x += step.x;
                    pt.y += step.y;
                    pt.z += step.z;
                    t += hl->step;
                }
                if (!hit) {
                    njPopMatrix(1);
                    continue;
                }
                hl->hit = 1;
                hp = &hl->list[hl->count];
                hp->height = pos.y;
                hp->height2 = pos.y + spd.y;
                hp->normal = normal;
                if (bothside) {
                    if (0.0f > pos.y) {
                        hp->normal2.x = -normal.x;
                        hp->normal2.y = -normal.y;
                        hp->normal2.z = -normal.z;
                    } else {
                        hp->normal2 = normal;
                    }
                } else {
                    hp->normal2 = normal;
                }
                hp->angz = -SA1_FTOI(atan2f(hp->normal2.x, hp->normal2.y) * 10430.381f);
                hp->angx = SA1_FTOI(func_8C10D058(hp->normal2.z) * 10430.381f);
                /* back to world space (still relative to p[0]) */
                njInvertMatrix(0);
                v1.x = pos.x;
                v1.y = 0.0f;
                v1.z = pos.z;
                njCalcPoint(0, &v1, &calc);
                hp->onpoly.x = calc.x + p->x;
                hp->onpoly.y = calc.y + p->y;
                hp->onpoly.z = calc.z + p->z;
                v1.y = r;
                njCalcPoint(0, &v1, &calc);
                hp->onpoly2.x = calc.x + p->x;
                hp->onpoly2.y = calc.y + p->y;
                hp->onpoly2.z = calc.z + p->z;
                v2 = spd;
                /* the original flips v2.y here and overwrites it right below
                 * (dead store, kept as in the binary) */
                if (bothside) {
                    if (0.0f > pos.y * spd.y) {
                        v2.y = -spd.y;
                    }
                } else if (0.0f > r * spd.y) {
                    v2.y = -spd.y;
                }
                v2.x = spd.x;
                v2.y = 0.0f;
                v2.z = spd.z;
                njCalcPoint(0, &v2, &calc);
                hp->anaspdh = calc;
                v2.x = 0.0f;
                v2.y = spd.y;
                v2.z = 0.0f;
                njCalcPoint(0, &v2, &calc);
                hp->anaspdv = calc;
                njPopMatrix(1);
                hl->count++;
            }
            nbMesh--;
        }
    }
}
#else
ASM_STUB(void func_8C02770E(csts *cp, NJS_OBJECT *obj, CL_HITLIST *hl))
#endif
