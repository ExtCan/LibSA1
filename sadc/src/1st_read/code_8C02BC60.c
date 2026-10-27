/*
 * 1ST_READ 8C02BC60 (land collision TU 8C027360-8C02CE80): vertical probe of
 * one land-collision object (SADX zxsdwstr / xssunit layout). Called by
 * func_8C02AD22 for every gathered land object; returns 1 when a polygon was
 * found above or below zssp->pos.
 *
 * The object's matrix is rebuilt from the unit matrix, every polygon of its
 * model is unpacked into a MESHBUF by 8C0274B8 (no transform) / 8C0275E0
 * (translation only) / 8C0273A2 (full matrix) and the vertical line through
 * zssp->pos is tested against each triangle in XZ. The plane height at the
 * probe position becomes zssp->lower (plane below pos.y, highest wins) or
 * zssp->upper (plane above, lowest wins), with the unit normal and its angles.
 *
 * Original quirks kept as is:
 *  - the range index is (attr & 0x60000000) >> 13 (as in 8C02833A), i.e. 0
 *    for every attribute without those bits, far out of the 4-entry table
 *    otherwise;
 *  - the per-triangle range test does not read a vertex: it reads the three
 *    stack slots of the vertex pointers (v0, v2, v1, in this order on the DC
 *    stack) as a NJS_POINT3. As floats these 0x8Cxxxxxx addresses are about
 *    -1e-31, so the test is |zssp->pos|^2 > range^2 (distance from the world
 *    origin). Not exactly: under round-to-zero pos - (-1e-31) moves a negative
 *    component one ulp toward zero. The PC build subtracts -2^-103 (the float
 *    of 0x8C000000), which gives the same squares for every input (the exact
 *    slot value only matters when |pos| < 2^-79, whose square vanishes);
 *  - an unknown meshset type returns 0 without njPopMatrix.
 *
 * NON_MATCHING: this TU was built with an SHC older than the project's 5.1r03
 * (MOV.W pool constants for 16-bit values, see docs/compiler.md and
 * code_8C02B0B4.c), so 5.1r03 cannot reproduce the 0x3064-byte frame offsets.
 * With shcdiff --ver=5.0r32 the remaining difference is register allocation:
 * the original keeps the triangle pointers a/b/c in R12/R11/R10 and unit in
 * R13 (so i, v0..v2, flip, nbMesh, ret, range2 and ang live on the stack,
 * ang sharing v1's slot), while this source gets b/c coalesced into R5/R6
 * and keeps i/v1/v2 in registers. Reproduced: the pointer-walk range test
 * (post-increment loads), the (&normal)-> stores (base + offset addressing),
 * the 0x38D1B716 threshold, and a declaration order that yields the
 * original stack order once those variables are on the stack (v0 0, v2 4,
 * v1 8, i 12, nbMesh 16, range2 20, flip 24, ret 28, normal 32, mb 44,
 * model 0x303C).
 */
#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Mesh work buffer (a local of this function, never game data), same
 * definition as MESHBUF in code_8C027360.c whose helpers fill it; DC offsets
 * in the asserts below.
 */
typedef struct {
    Sint32 type;                /* NJS_MESHSET.type_matId & 0xC000 */
    Sint32 n;                   /* vertices in buf (3, 4, strip length) */
    Sint16 *meshes;             /* cursor in NJS_MESHSET.meshes */
    NJS_MODEL *model;           /* points source */
    NJS_POINT3 buf[0x400];      /* gathered vertices */
} MESHBUF;

/* one probe result (SADX xssunit), as in code_8C02AD22.c */
typedef struct xssunit {
    Sint32 findflag;            /* 0x00 1 = polygon found */
    Sint32 objatt;              /* 0x04 (not written here) */
    Angle angx;                 /* 0x08 */
    Angle angz;                 /* 0x0C */
    Float onpos;                /* 0x10 plane height at pos.x/pos.z */
    NJS_VECTOR normal;          /* 0x14 unit normal */
} xssunit;                      /* 0x20 */

/* probe position + two results (SADX zxsdwstr), as in code_8C02AD22.c */
typedef struct zxsdwstr {
    NJS_POINT3 pos;             /* 0x00 */
    xssunit lower;              /* 0x0C nearest plane below pos.y */
    xssunit upper;              /* 0x2C nearest plane above pos.y */
} zxsdwstr;                     /* 0x4C */

SA1_ASSERT_SIZE(MESHBUF, 0x3010);
SA1_ASSERT_OFFSET(MESHBUF, n, 0x04);
SA1_ASSERT_OFFSET(MESHBUF, meshes, 0x08);
SA1_ASSERT_OFFSET(MESHBUF, model, 0x0C);
SA1_ASSERT_OFFSET(MESHBUF, buf, 0x10);
SA1_ASSERT_SIZE(xssunit, 0x20);
SA1_ASSERT_OFFSET(xssunit, onpos, 0x10);
SA1_ASSERT_SIZE(zxsdwstr, 0x4C);
SA1_ASSERT_OFFSET(zxsdwstr, lower, 0x0C);
SA1_ASSERT_OFFSET(zxsdwstr, upper, 0x2C);

extern Float D_8C112460[];      /* squared ranges (4 entries) */

/* code_8C027360.c */
extern void func_8C027360(Float *a, Float *b, Float *c, Float *nrm);
extern Bool func_8C0273A2(MESHBUF *mb);
extern Bool func_8C0274B8(MESHBUF *mb);
extern Bool func_8C0275E0(MESHBUF *mb, NJS_OBJECT *obj);

#ifdef NON_MATCHING
Sint32 func_8C02BC60(zxsdwstr *zssp, NJS_OBJECT *obj, Uint32 attr)
{
    NJS_MODEL model;
    MESHBUF mb;
    NJS_VECTOR normal;
    Sint32 ret;
    Bool flip;
    Float range2;
    Sint32 nbMesh;
    Sint32 i;
    NJS_POINT3 *v1;
    NJS_POINT3 *v2;
    NJS_POINT3 *v0;
    NJS_POINT3 *a;
    NJS_POINT3 *b;
    NJS_POINT3 *c;
    xssunit *unit;
    Angle ang;
    Float dx, dy, dz;
    Float c0, c1, c2;
    Float d;
    Float onpos;
    Float *p;
    Float *q;
#ifdef TARGET_PC
    Float org[3];
#endif

    ret = 0;
    zssp->lower.findflag = 0;
    zssp->upper.findflag = 0;
    model = *obj->model;
    mb.model = &model;
    range2 = D_8C112460[(attr & 0x60000000) >> 13];
#ifdef TARGET_PC
    org[0] = org[1] = org[2] = sa1_f32(0x8C000000); /* a DC stack slot as float */
#endif
    njPushMatrix(NULL);
    njUnitMatrix(NULL);
    if (!(obj->evalflags & 1)) {
        njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
    }
    if (!(obj->evalflags & 2)) {
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
        mb.type = model.meshsets->type_matId & 0xC000;
        switch (mb.type) {
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
            return 0;           /* matrix stays pushed (original) */
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
            for (i = 0, v0 = &mb.buf[0], v1 = &mb.buf[1], v2 = &mb.buf[2]; i < mb.n - 2;
                 i++, v0 = v1, v1 = v2, v2++) {
                /* range test against the pointer slots, see the header */
#ifdef TARGET_PC
                q = org;
#else
                q = (Float *)&v0;
#endif
                p = &zssp->pos.x;
                dx = *p++ - *q++;
                dy = *p++ - *q++;
                dz = *p - *q;
                if (SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy)) > range2) {   /* FMUL dy*dy, FMAC dx, FMAC dz */
                    continue;
                }
                if (((i + flip) & 1) == 0) {
                    a = v0;
                    b = v1;
                    c = v2;
                } else {
                    a = v0;
                    b = v2;
                    c = v1;
                }
                /* vertical line through pos inside the triangle (XZ) */
                c0 = (b->z - a->z) * (zssp->pos.x - a->x) - (b->x - a->x) * (zssp->pos.z - a->z);
                c1 = (c->z - b->z) * (zssp->pos.x - b->x) - (c->x - b->x) * (zssp->pos.z - b->z);
                c2 = (a->z - c->z) * (zssp->pos.x - c->x) - (a->x - c->x) * (zssp->pos.z - c->z);
                if (c0 * c1 < 0.0f) {
                    continue;
                }
                if (c1 * c2 < 0.0f) {
                    continue;
                }
                if (c0 * c2 < 0.0f) {
                    continue;
                }
                func_8C027360(&a->x, &b->x, &c->x, &normal.x);
                d = -normal.x * a->x - normal.y * a->y - normal.z * a->z;
                if (normal.y * normal.y < 0.000099999992f) {
                    continue;   /* (nearly) vertical polygon */
                }
                onpos = (-normal.z * zssp->pos.z - normal.x * zssp->pos.x - d) / normal.y;
                unit = NULL;
                if (zssp->pos.y > onpos) {
                    if (zssp->lower.findflag == 0 || onpos > zssp->lower.onpos) {
                        unit = &zssp->lower;
                    }
                } else {
                    if (zssp->upper.findflag == 0 || zssp->upper.onpos > onpos) {
                        unit = &zssp->upper;
                    }
                }
                if (unit == NULL) {
                    continue;
                }
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
    njPopMatrix(1);
    return ret;
}
#else
ASM_STUB(Sint32 func_8C02BC60(zxsdwstr *zssp, NJS_OBJECT *obj, Uint32 attr))
#endif
