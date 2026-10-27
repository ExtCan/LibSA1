#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_funcs.h"
#include "sa1/shc_intrinsics.h"
#include "sa1/engine.h"
#include "sa1/collision.h"

/*
 * 1ST_READ 8C02B620 (land collision module 8C027360-8C02CE80): vertical probe
 * of one land object, called by 8C028480 for every entry of the gathered land
 * list (B_8C754EF4) with (probe, entry.pObject, entry.slAttribute).
 *
 * obj->model is copied to the stack and its meshsets are unpacked one
 * polygon / strip at a time into a MESHBUF (8C0273A2 transformed by obj's
 * local matrix, 8C0274B8 untransformed, 8C0275E0 offset by obj->pos). For
 * every triangle that passes a bounding test and whose xz projection contains
 * cp->pos, the plane height at cp->pos.xz is computed; the hit nearest to
 * cp->pos.y is stored in cp->yb, cp->normal, cp->angz / cp->angx. Returns 1
 * on a hit, else 0. A meshset of unknown type returns 0 at once, without
 * njPopMatrix (original behaviour, the matrix stack is left pushed).
 *
 * DC bug, reproduced: the bounding test passes the addresses of the vertex
 * pointer variables (&p0, &p1, &p2) where the vertices were meant, so the
 * SH-4 reads the stack words [p2, p1, p0, cnt, d.x] (sp+0..sp+16) as floats.
 * The pointers (0x8Cxxxxxx) are tiny negative normals (about -1e-31), cnt is
 * a denormal (FPSCR.DN = 1: zero) and d.x = p0->x - p2->x at that point, so
 * the test really measures cp->pos against (~0, 0, d.x), (~0, ~0, 0) and
 * (~0, ~0, ~0), i.e. against the world origin. TARGET_PC rebuilds those words in pcw[]
 * (DC_SLOT); any 0x8Cxxxxxx pattern gives the same result under the SH-4
 * round-to-zero mode (px - tiny is px, or one ulp towards zero when px < 0).
 *
 * NON_MATCHING with the project compiler SHC 5.1r03: like the rest of this
 * module (see code_8C028480.c, code_8C02B0B4.c) it was built with an older
 * SHC (16-bit constants such as the 0x3060 frame and the 0x3038/0x3044/0x304C
 * stack offsets are MOV.W pool loads; 5.1r03 builds them with MOV/SHLL8).
 * With --ver=5.0r32 (or 5.0r31) the body below has the original instruction
 * count and schedule (673 instructions); the remaining differences are only
 * register numbering: ret/i in R10/R11 swapped, the second and third angle
 * temporaries R2 instead of R3, and FR4-FR11/FR15 permuted in the xz edge
 * test and the plane computation:
 *
 *   py -3.12 tools/shcdiff.py func_8C02B620 src/1st_read/code_8C02B620.c
 *       --ver=5.0r32 --flags=-define=NON_MATCHING      (69 differing)
 */

/* Polygon / strip vertex buffer filled by 8C0273A2/8C0274B8/8C0275E0; same
 * layout as MESHBUF in code_8C027360.c (STRIPBUF in code_8C028620.c,
 * CL_STRIPBUF in code_8C02770E.c). */
typedef struct {
    Sint32 type;                /* DC +0x00 meshset type (type_matId & 0xC000) */
    Sint32 n;                   /* DC +0x04 vertex count (3, 4 or strip length) */
    Sint16 *meshes;             /* DC +0x08 read cursor in NJS_MESHSET.meshes */
    NJS_MODEL *model;           /* DC +0x0C points source */
    NJS_POINT3 buf[0x400];      /* DC +0x10 gathered vertices */
} MESHBUF;                      /* DC size 0x3010; a stack work buffer, native layout on PC */

SA1_ASSERT_SIZE(MESHBUF, 0x3010);
SA1_ASSERT_OFFSET(MESHBUF, model, 0x0C);
SA1_ASSERT_OFFSET(MESHBUF, buf, 0x10);

/* face normal (not normalised) of the triangle a, b, c */
extern void func_8C027360(Float *a, Float *b, Float *c, Float *nrm);
/* next polygon / strip of mb->meshes into mb->buf; TRUE when the strip
 * starts flipped */
extern Bool func_8C0273A2(MESHBUF *mb);
extern Bool func_8C0274B8(MESHBUF *mb);
extern Bool func_8C0275E0(MESHBUF *mb, NJS_OBJECT *obj);

/* squared length through a vector pointer (the original recomputes the
 * stack address for every component). Every squared length below is
 * FMUL y*y, FMAC x*x, FMAC z*z (15 FMACs: three edges, the speed + radius
 * sum and three corner distances); libsa1 fuses the same way, the SHC form
 * keeps the source shape of the 5.0r32 comparison. */
#if defined(TARGET_PC)
#define LEN2(x, y, z) SA1_FMAC((z), (z), SA1_FMAC((x), (x), (y) * (y)))
#else
#define LEN2(x, y, z) ((x) * (x) + (y) * (y) + (z) * (z))
#endif
#define SQ(v) LEN2((v)->x, (v)->y, (v)->z)

/* the DC bug above: stack word k of [p2, p1, p0, cnt, d.x] */
#if defined(TARGET_PC)
#define DC_SLOT(var, k) (&pcw[k])
#define DC_PTR_BITS 0x8C0F0000u /* any DC stack address */
#else
#define DC_SLOT(var, k) ((Float *)&(var))
#endif

#ifdef NON_MATCHING
Sint32 func_8C02B620(csts *cp, NJS_OBJECT *obj, Uint32 attr)
{
    NJS_MODEL model;
    MESHBUF mb;
    NJS_VECTOR nrm;
    NJS_VECTOR d;
    volatile Sint32 cnt;        /* sp+12: also the rotation angle temporary */
    NJS_POINT3 *p0;
    NJS_POINT3 *p1;
    NJS_POINT3 *p2;
    Sint32 ret;
    Sint32 i;
    Bool flip;
    NJS_MESHSET *ms;
    NJS_MODEL *mdl;
    Float *s;
    Float r2;
    Float t;
    Float px, py, pz;
    Float dx, dy, dz;
    Float e1, e2, e3;
    Float dd;
    Float nx;
    Float y;
    PC_ONLY(Float pcw[5];)

    PC_ONLY((void)attr;)
    ret = 0;
    mdl = obj->model;
    model = *mdl;
    mb.model = &model;
    njPushMatrix(NULL);
    njUnitMatrix(NULL);
    if (!(obj->evalflags & 1)) {
        njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
    }
    if (!(obj->evalflags & 2)) {
        if ((cnt = obj->ang[2]) != 0) {
            njRotateZ(NULL, cnt);
        }
        if ((cnt = obj->ang[1]) != 0) {
            njRotateY(NULL, cnt);
        }
        if ((cnt = obj->ang[0]) != 0) {
            njRotateX(NULL, cnt);
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
        ms = model.meshsets;
        cnt = ms->nbMesh;
        mb.meshes = ms->meshes;
        if (cnt != 0) do {
            if ((obj->evalflags & 7) == 7) {
                flip = func_8C0274B8(&mb);
            } else if ((obj->evalflags & 6) == 6) {
                flip = func_8C0275E0(&mb, obj);
            } else {
                flip = func_8C0273A2(&mb);
            }
            for (i = 0, p0 = &mb.buf[0], p1 = &mb.buf[1], p2 = &mb.buf[2]; i < mb.n - 2;
                 i++, p0 = p1, p1 = p2, p2++) {
                /* bounding radius: longest edge + speed + sphere radius */
                d.x = p1->x - p0->x;
                d.y = p1->y - p0->y;
                d.z = p1->z - p0->z;
                r2 = SQ(&d);
                d.x = p2->x - p1->x;
                d.y = p2->y - p1->y;
                d.z = p2->z - p1->z;
                if ((t = SQ(&d)) > r2) {
                    r2 = t;
                }
                d.x = p0->x - p2->x;
                d.y = p0->y - p2->y;
                d.z = p0->z - p2->z;
                if ((t = SQ(&d)) > r2) {
                    r2 = t;
                }
                PC_ONLY(pcw[0] = pcw[1] = pcw[2] = sa1_f32(DC_PTR_BITS); pcw[3] = 0.0f; pcw[4] = d.x;)
                s = &cp->pos.x;
#if defined(TARGET_PC)
                r2 += SA1_FMAC(cp->radius, cp->radius, LEN2(cp->spd.x, cp->spd.y, cp->spd.z));
#else
                r2 += cp->spd.x * cp->spd.x + cp->spd.y * cp->spd.y + cp->spd.z * cp->spd.z
                    + cp->radius * cp->radius;
#endif
                px = *s++;
                py = *s++;
                pz = *s;
                s = DC_SLOT(p0, 2);
                dx = px - *s++;
                dy = py - *s++;
                dz = pz - *s;
                if (LEN2(dx, dy, dz) > r2) {
                    continue;
                }
                s = DC_SLOT(p1, 1);
                dx = px - *s++;
                dy = py - *s++;
                dz = pz - *s;
                if (LEN2(dx, dy, dz) > r2) {
                    continue;
                }
                s = DC_SLOT(p2, 0);
                dx = px - *s++;
                dy = py - *s++;
                dz = pz - *s;
                if (LEN2(dx, dy, dz) > r2) {
                    continue;
                }
                {
                    NJS_POINT3 *a;
                    NJS_POINT3 *b;
                    NJS_POINT3 *c;

                    /* strip winding alternates */
                    if (((i + flip) & 1) == 0) {
                        a = p0;
                        b = p1;
                        c = p2;
                    } else {
                        a = p0;
                        b = p2;
                        c = p1;
                    }
                    /* cp->pos inside the triangle in xz */
                    e1 = (b->z - a->z) * (cp->pos.x - a->x) - (b->x - a->x) * (cp->pos.z - a->z);
                    e2 = (c->z - b->z) * (cp->pos.x - b->x) - (c->x - b->x) * (cp->pos.z - b->z);
                    e3 = (a->z - c->z) * (cp->pos.x - c->x) - (a->x - c->x) * (cp->pos.z - c->z);
                    if (e1 * e2 < 0.0f || e2 * e3 < 0.0f || e1 * e3 < 0.0f) {
                        continue;
                    }
                    /* plane height at cp->pos.xz */
                    func_8C027360((Float *)a, (Float *)b, (Float *)c, &nrm.x);
                    dd = -(nx = nrm.x) * a->x - nrm.y * a->y - nrm.z * a->z;
                    if (nrm.y == 0.0f) {
                        continue;
                    }
                    y = (-nx * cp->pos.x - nrm.z * cp->pos.z - dd) / nrm.y;
                    if (njScalor(&nrm) == 0.0f) {
                        (&nrm)->x = 0.0f;
                        (&nrm)->y = 1.0f;
                        (&nrm)->z = 0.0f;
                    } else {
                        njUnitVector(&nrm);
                    }
                    /* keep the hit nearest to cp->pos.y */
                    if (ret == 0
                        || (ret == 1 && _builtin_fabsf(y - cp->pos.y) < _builtin_fabsf(cp->yb - cp->pos.y))) {
                        ret = 1;
                        cp->yb = y;
                        cp->normal = nrm;
                        cp->angz = -SA1_FTOI(atan2f(nrm.x, nrm.y) * 10430.381f);
                        cp->angx = SA1_FTOI(func_8C10D058(nrm.z) * 10430.381f);
                    }
                }
            }
        } while (--cnt != 0);
    }
    njPopMatrix(1);
    return ret;
}
#endif
