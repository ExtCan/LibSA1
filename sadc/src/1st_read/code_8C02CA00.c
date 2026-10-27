#include "common.h"
#include "ninja.h"
#include "sa1/ninja_funcs.h"
#include "sa1/engine.h"

/*
 * 1ST_READ 8C02CA00-8C02CE80: last two functions of the land collision
 * module 8C027360-8C02CE80.
 *
 * func_8C02CA00: vertical probe of one land object (SADX zxsdwstr layout).
 * Callers: 8C01BD9E, 8C07BAC0 (death zones), 8C0925A0, 8C0C88E6. Same
 * algorithm as func_8C02BC60 without the attribute / squared-range test:
 * the object's matrix is rebuilt from the unit matrix, every polygon of its
 * model is unpacked into a MESHBUF by 8C0274B8 / 8C0275E0 / 8C0273A2, and the
 * vertical line through zssp->pos is tested against each triangle in XZ.
 * The plane height at zssp->pos becomes the lower result (below pos.y,
 * highest wins) or the upper result (at/above pos.y, lowest wins). Returns 1
 * when a result was stored, 0 otherwise (also 0, without popping the matrix,
 * for a meshset of unknown type).
 *
 * func_8C02CE18: forget a land object in the collision object cache: clears
 * the object pointer of its entry in the in-use list D_8C02D548.
 *
 * Compiler: this module was not built with the project compiler (see
 * code_8C028480.c / code_8C028100.c: 16-bit constants are MOV.W pool loads).
 * func_8C02CE18 matches with 5.1r03. func_8C02CA00 is NON_MATCHING: with the
 * project flags 5.1r03 builds its frame / stack offsets with MOV/SHLL8. Its
 * structure (loop inversion: no "continue" in the triangle loop, "?:"
 * rotations for the stack-slot angle temp, switch on an assignment, the
 * for-initialiser order) was worked out with
 *   py -3.12 tools/shcdiff.py func_8C02CA00 src/1st_read/code_8C02CA00.c
 *       --ver=5.0r32 --flags=-define=NON_MATCHING
 * which leaves register-allocation differences only:
 *  - the original keeps `unit` (R13) live across the whole triangle test, so
 *    it gets a register of its own and p1 is spilled, although its NULL store
 *    sits just before the lower/upper selection; this source sets unit = NULL
 *    at the top of the iteration to get the same allocation (the MOV #0 then
 *    moves to the top);
 *  - the original keeps 1.0f/0.0f/10430.381f in FR12/FR13/FR14 and reloads
 *    the 0.0001f epsilon; here the epsilon is hoisted instead of 1.0f, and the
 *    FR numbering of the XZ edge tests differs.
 */

/* mesh work buffer filled by 8C0273A2..8C0275E0, same as in code_8C027360.c
 * (native work struct; the DC layout is checked by the assertions below) */
typedef struct {
    Sint32 type;                /* NJS_MESHSET.type_matId & 0xC000 */
    Sint32 n;                   /* vertices in buf (3, 4, strip length) */
    Sint16 *meshes;             /* cursor in NJS_MESHSET.meshes */
    NJS_MODEL *model;           /* points source */
    NJS_POINT3 buf[0x400];      /* gathered vertices */
} MESHBUF;

/* one probe result (SADX xssunit), as in code_8C02BC60.c */
typedef struct xssunit {
    Sint32 findflag;            /* 0x00 1 = polygon found */
    Sint32 objatt;              /* 0x04 (not written here) */
    Angle angx;                 /* 0x08 */
    Angle angz;                 /* 0x0C */
    Float onpos;                /* 0x10 plane height at pos */
    NJS_VECTOR normal;          /* 0x14 */
} xssunit;                      /* 0x20 */

/* probe position + two results (SADX zxsdwstr), as in code_8C02BC60.c;
 * 76 bytes on the stack of 8C07BAC0 */
typedef struct zxsdwstr {
    NJS_POINT3 pos;             /* 0x00 */
    xssunit lower;              /* 0x0C highest polygon below pos.y */
    xssunit upper;              /* 0x2C lowest polygon at/above pos.y */
} zxsdwstr;                     /* 0x4C */

SA1_ASSERT_SIZE(MESHBUF, 0x3010);
SA1_ASSERT_OFFSET(MESHBUF, model, 0x0C);
SA1_ASSERT_OFFSET(MESHBUF, buf, 0x10);
SA1_ASSERT_SIZE(xssunit, 0x20);
SA1_ASSERT_OFFSET(xssunit, onpos, 0x10);
SA1_ASSERT_SIZE(zxsdwstr, 0x4C);
SA1_ASSERT_OFFSET(zxsdwstr, lower, 0x0C);
SA1_ASSERT_OFFSET(zxsdwstr, upper, 0x2C);

/* collision object cache entry: leading fields of colobj (code_8C028E60.c) */
typedef struct colobj {
    struct colobj *next;        /* 0x00 in-use list D_8C02D548 */
    void *prev;                 /* 0x04 */
    NJS_OBJECT *object;         /* 0x08 NULL: entry free */
} colobj;

extern colobj *D_8C02D548;      /* objects in use (head, kept in the code section) */

#ifdef NON_MATCHING

extern void func_8C027360(Float *a, Float *b, Float *c, Float *nrm);
extern Bool func_8C0273A2(MESHBUF *mb);
extern Bool func_8C0274B8(MESHBUF *mb);
extern Bool func_8C0275E0(MESHBUF *mb, NJS_OBJECT *obj);

Sint32 func_8C02CA00(zxsdwstr *zssp, NJS_OBJECT *obj)
{
    NJS_MODEL model;
    MESHBUF mb;
    NJS_VECTOR normal;
    Sint32 ret;
    Bool flip;
    Sint32 nbMesh;
    Sint32 i;
    NJS_POINT3 *p1;
    NJS_POINT3 *p2;
    NJS_POINT3 *p0;
    NJS_POINT3 *a;
    NJS_POINT3 *b;
    NJS_POINT3 *c;
    xssunit *unit;
    Float c0, c1, c2;
    Float d;
    Float onpos;
    Float nx;

    ret = 0;
    zssp->lower.findflag = 0;
    zssp->upper.findflag = 0;
    model = *obj->model;
    mb.model = &model;
    njPushMatrix(NULL);
    njUnitMatrix(NULL);
    if (!(obj->evalflags & 1)) {
        njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
    }
    if (!(obj->evalflags & 2)) {
        obj->ang[2] ? njRotateZ(NULL, obj->ang[2]) : (void)0;
        obj->ang[1] ? njRotateY(NULL, obj->ang[1]) : (void)0;
        obj->ang[0] ? njRotateX(NULL, obj->ang[0]) : (void)0;
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
        nbMesh = model.meshsets->nbMesh;
        mb.meshes = model.meshsets->meshes;
        for (; nbMesh != 0; nbMesh--) {
            if ((obj->evalflags & 7) == 7) {
                flip = func_8C0274B8(&mb);
            } else if ((obj->evalflags & 6) == 6) {
                flip = func_8C0275E0(&mb, obj);
            } else {
                flip = func_8C0273A2(&mb);
            }
            for (i = 0, p0 = &mb.buf[0], p1 = &mb.buf[1], p2 = &mb.buf[2]; i < mb.n - 2;
                 i++, p0 = p1, p1 = p2, p2++) {
                unit = NULL;
                if (((i + flip) & 1) == 0) {
                    a = p0;
                    b = p1;
                    c = p2;
                } else {
                    a = p0;
                    b = p2;
                    c = p1;
                }
                c0 = (b->z - a->z) * (zssp->pos.x - a->x) - (b->x - a->x) * (zssp->pos.z - a->z);
                c1 = (c->z - b->z) * (zssp->pos.x - b->x) - (c->x - b->x) * (zssp->pos.z - b->z);
                c2 = (a->z - c->z) * (zssp->pos.x - c->x) - (a->x - c->x) * (zssp->pos.z - c->z);
                if (c0 * c1 >= 0.0f && c1 * c2 >= 0.0f && c0 * c2 >= 0.0f) {
                    func_8C027360(&a->x, &b->x, &c->x, &normal.x);
                    nx = normal.x;
                    d = -nx * a->x - normal.y * a->y - normal.z * a->z;
                    if (normal.y * normal.y >= 0.000099999992f) {
                        onpos = (-normal.z * zssp->pos.z - nx * zssp->pos.x - d) / normal.y;
                        if (zssp->pos.y > onpos) {
                            if (zssp->lower.findflag == 0 || onpos > zssp->lower.onpos) {
                                unit = &zssp->lower;
                            }
                        } else {
                            if (zssp->upper.findflag == 0 || zssp->upper.onpos > onpos) {
                                unit = &zssp->upper;
                            }
                        }
                        if (unit != NULL) {
                            if (njScalor(&normal) == 0.0f) {
                                (&normal)->x = 0.0f;
                                (&normal)->y = 1.0f;
                                (&normal)->z = 0.0f;
                            } else {
                                njUnitVector(&normal);
                            }
                            ret = 1;
                            unit->findflag = 1;
                            unit->onpos = onpos;
                            unit->normal = normal;
                            unit->angz = -SA1_FTOI(atan2f(normal.x, normal.y) * 10430.381f);
                            unit->angx = SA1_FTOI(func_8C10D058(normal.z) * 10430.381f);
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
ASM_STUB(Sint32 func_8C02CA00(zxsdwstr *zssp, NJS_OBJECT *obj))
#endif

void func_8C02CE18(NJS_OBJECT *obj)
{
    colobj *co;

    for (co = D_8C02D548; co != NULL; co = co->next) {
        if (co->object == obj) {
            co->object = NULL;
            break;
        }
    }
}
