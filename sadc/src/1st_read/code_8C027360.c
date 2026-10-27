/*
 * 1ST_READ 8C027360-8C027700: vertex gathering for the land collision
 * (module 8C027360-8C02CE80, see code_8C02770E.c / code_8C028100.c).
 *
 * MESHBUF is the strip buffer that func_8C02770E builds on its stack
 * (sp+0xB4) and fills one polygon / strip at a time from a basic-model
 * meshset:
 *   type   = NJS_MESHSET.type_matId & 0xC000 (0 tris, 0x4000 quads,
 *            0x8000 / 0xC000 strips)
 *   n      = vertex count of the current polygon (3 or 4, set by the caller)
 *            or of the current strip (strip header & 0x7FFF, read here)
 *   meshes = read cursor in NJS_MESHSET.meshes (vertex indices)
 *   model  = model whose points[] are indexed
 *   buf    = gathered vertices (at most 0x400: "WARNING: STRIP buf is FULL")
 * Bit 15 of a strip header is the strip's winding flag, returned to the
 * caller.
 *
 * Compiler: like 8C028100, this module was built with SHC 5.0r31/5.0r32
 * (or 5.1r01 with -extra=a=800): 16-bit constants (0x4000, 0x7FFF, 0x400)
 * are MOV.W pool loads, which the project flags (5.1r03 -extra=a=1800) turn
 * into MOV/SHLL8. Everything is therefore kept under NON_MATCHING until the
 * build supports per-file compiler versions:
 *   py -3.12 tools/shcdiff.py <func> src/1st_read/code_8C027360.c
 *       --ver 5.0r32 --flags=-define=NON_MATCHING
 *   func_8C0273A2  MATCH
 *   func_8C0274B8  MATCH
 *   func_8C0275E0  2 instructions differ (two loads in swapped order)
 *   func_8C027360  10 instructions differ (FR4/FR6 swapped between the
 *                  x and y components of a - c; no source form found)
 *
 * The original reads x/y/z through post-incremented pointers (FMOV.S @Rn+);
 * SHC only produces that from explicit Float pointer walks, hence the
 * Float * parameters of func_8C027360 and the Float *s in func_8C0275E0.
 * NJS_POINT3 is three packed Floats on every target.
 */
#include "common.h"
#include "ninja.h"
#include "sa1/ninja_funcs.h"

typedef struct {
    Sint32 type;
    Sint32 n;
    Sint16 *meshes;
    NJS_MODEL *model;
    NJS_POINT3 buf[0x400];
} MESHBUF;

SA1_ASSERT_SIZE(MESHBUF, 0x3010);
SA1_ASSERT_OFFSET(MESHBUF, n, 0x04);
SA1_ASSERT_OFFSET(MESHBUF, meshes, 0x08);
SA1_ASSERT_OFFSET(MESHBUF, model, 0x0C);
SA1_ASSERT_OFFSET(MESHBUF, buf, 0x10);

extern Sint8 tcrf_CollisionDisplayVar;   /* 8C752BDC, debug display level */
extern const char D_8C112470[];          /* "WARNING: STRIP buf is FULL" */

#ifdef NON_MATCHING
/* Face normal (not normalised) of the triangle a, b, c (Float[3] each):
 * nrm = (b - a) x (a - c). */
void func_8C027360(Float *a, Float *b, Float *c, Float *nrm)
{
    Float x1, y1, z1;
    Float x2, y2, z2;

    x2 = *a++;
    y2 = *a++;
    z2 = *a;
    x1 = *b++ - x2;
    y1 = *b++ - y2;
    z1 = *b - z2;
    x2 -= *c++;
    y2 -= *c++;
    z2 -= *c;
    nrm[0] = z1 * y2 - y1 * z2;
    nrm[1] = x1 * z2 - z1 * x2;
    nrm[2] = y1 * x2 - x1 * y2;
}
#else
ASM_STUB(void func_8C027360(Float *a, Float *b, Float *c, Float *nrm))
#endif

#ifdef NON_MATCHING
/* Gather the next polygon / strip of mb->meshes into mb->buf through the
 * current matrix. Returns the strip winding flag (FALSE for tris/quads). */
Bool func_8C0273A2(MESHBUF *mb)
{
    Bool flip;
    NJS_POINT3 *pts;
    Sint16 *idx;
    NJS_POINT3 *dst;
    Sint32 i;

    flip = FALSE;
    switch (mb->type) {
    case 0:
    case 0x4000:
        i = mb->n;
        idx = mb->meshes;
        pts = mb->model->points;
        dst = mb->buf;
        for (; i > 0; i--) {
            njCalcPoint(NULL, &pts[*idx], dst);
            idx++;
            dst++;
        }
        mb->meshes = idx;
        break;
    case 0x8000:
    case 0xC000:
        mb->n = *mb->meshes & 0x7FFF;
        if (*mb->meshes & 0x8000) {
            flip = TRUE;
        }
        if (mb->n > 0x400) {
            mb->n = 0x400;
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0x00020012, D_8C112470);
            }
        }
        mb->meshes++;
        idx = mb->meshes;
        pts = mb->model->points;
        i = mb->n;
        dst = mb->buf;
        for (; i > 0; i--) {
            njCalcPoint(NULL, &pts[*idx], dst);
            idx++;
            dst++;
        }
        mb->meshes = idx;
        break;
    }
    return flip;
}
#else
ASM_STUB(Bool func_8C0273A2(MESHBUF *mb))
#endif

#ifdef NON_MATCHING
/* Same as func_8C0273A2, untransformed (model space). */
Bool func_8C0274B8(MESHBUF *mb)
{
    Bool flip;
    NJS_POINT3 *pts;
    Sint16 *idx;
    NJS_POINT3 *dst;
    Sint32 i;

    flip = FALSE;
    switch (mb->type) {
    case 0:
    case 0x4000:
        i = mb->n;
        idx = mb->meshes;
        pts = mb->model->points;
        dst = mb->buf;
        for (; i > 0; i--) {
            *dst = pts[*idx];
            idx++;
            dst++;
        }
        mb->meshes = idx;
        break;
    case 0x8000:
    case 0xC000:
        mb->n = *mb->meshes & 0x7FFF;
        if (*mb->meshes & 0x8000) {
            flip = TRUE;
        }
        if (mb->n > 0x400) {
            mb->n = 0x400;
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0x00020012, D_8C112470);
            }
        }
        mb->meshes++;
        idx = mb->meshes;
        pts = mb->model->points;
        i = mb->n;
        dst = mb->buf;
        for (; i > 0; i--) {
            *dst = pts[*idx];
            idx++;
            dst++;
        }
        mb->meshes = idx;
        break;
    }
    return flip;
}
#else
ASM_STUB(Bool func_8C0274B8(MESHBUF *mb))
#endif

#ifdef NON_MATCHING
/* Same as func_8C0274B8, translated by the node position obj->pos (no
 * rotation/scale). */
Bool func_8C0275E0(MESHBUF *mb, NJS_OBJECT *obj)
{
    Bool flip;
    NJS_POINT3 *pts;
    Sint16 *idx;
    NJS_POINT3 *dst;
    Sint32 i;
    Float *s;
    Float px, py, pz;

    flip = FALSE;
    switch (mb->type) {
    case 0:
    case 0x4000:
        i = mb->n;
        idx = mb->meshes;
        px = obj->pos[0];
        py = obj->pos[1];
        pz = obj->pos[2];
        pts = mb->model->points;
        dst = mb->buf;
        for (; i > 0; i--) {
            s = &pts[*idx].x;
            idx++;
            dst->x = *s++ + px;
            dst->y = *s++ + py;
            dst->z = *s + pz;
            dst++;
        }
        mb->meshes = idx;
        break;
    case 0x8000:
    case 0xC000:
        mb->n = *mb->meshes & 0x7FFF;
        if (*mb->meshes & 0x8000) {
            flip = TRUE;
        }
        if (mb->n > 0x400) {
            mb->n = 0x400;
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0x00020012, D_8C112470);
            }
        }
        mb->meshes++;
        i = mb->n;
        pts = mb->model->points;
        px = obj->pos[0];
        py = obj->pos[1];
        pz = obj->pos[2];
        idx = mb->meshes;
        dst = mb->buf;
        for (; i > 0; i--) {
            s = &pts[*idx].x;
            idx++;
            dst->x = *s++ + px;
            dst->y = *s++ + py;
            dst->z = *s + pz;
            dst++;
        }
        mb->meshes = idx;
        break;
    }
    return flip;
}
#else
ASM_STUB(Bool func_8C0275E0(MESHBUF *mb, NJS_OBJECT *obj))
#endif
