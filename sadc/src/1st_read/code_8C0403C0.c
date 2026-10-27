/*
 * Shape motion (vertex / normal morph) of basic models, 1ST_READ 8C0403C0-8C040596
 * (part of the motion playback module 8C03FDC0-8C040A60).
 *  func_8C0403C0  interpolate the vertex key pair into the scratch buffer, model->points = buffer
 *  func_8C040404  interpolate (and normalise) the normal key pair, model->normals = buffer
 *  func_8C040448  same, normals stored after the nbPoint vertices of the buffer
 *  func_8C040498  draw one node with its shape keys applied, then restore points/normals
 *  func_8C04057A  per-node translate callback without motion keys (njTranslate obj->pos)
 *  func_8C040588  per-node rotate callback without motion keys (njRotateXYZ obj->ang)
 *
 * Matching status: only func_8C04057A matches. The other functions are complete C but
 * this module does not compile to the original with the project's SHC 5.1r03 flags:
 *  - 8C0403C0/8C040404/8C040448 keep the Float rate on the stack inside the loop
 *    (5.1r03 puts it in FR15; SHC 5.0r31/5.0r32 reproduce the original allocation and
 *    stack layout exactly, differing only in instruction order);
 *  - 8C040498 and 8C040588 differ only in instruction scheduling (argument setup order
 *    R6 before R5, pool load placement), the same pattern seen in the neighbouring
 *    files of this module.
 */
#include "common.h"
#include "ninja.h"
#include "sa1/ninja_funcs.h"

/* Motion playback context (B_8C88FE40 / B_8C88FE5C); B_8C88FE5C drives the shape motion */
typedef struct {
    void *mdata;                /* 0x00 current NJS_MDATAn entry */
    void **p;                   /* 0x04 key lists of the current entry */
    Uint32 *nb;                 /* 0x08 key counts of the current entry */
    Float frame;                /* 0x0C */
    Uint32 nbFrame;             /* 0x10 */
    Uint32 idx;                 /* 0x14 element index inside the entry (B_8C88FE70) */
    Sint32 unk18;               /* 0x18 */
} MOTION_CTX;                   /* 0x1C */

SA1_ASSERT_SIZE(MOTION_CTX, 0x1C);
SA1_ASSERT_OFFSET(MOTION_CTX, idx, 0x14);

/* scratch buffer for the interpolated shape: nbPoint vertices, then nbPoint normals */
extern NJS_POINT3 *B_8C89002C;
/* shape motion channels: 1 = vertices, 2 = normals, 3 = both */
extern Uint16 B_8C749104;
extern MOTION_CTX B_8C88FE5C;

/* basic model draw (8C037098) */
extern void func_8C037098(NJS_MODEL *model);

/* out = normalize(p0 * (1 - t) + p1 * t) */
ASM_STUB(void func_8C040240(NJS_VECTOR *p0, NJS_VECTOR *p1, NJS_VECTOR *out, Float t))
/* out = p0 * (1 - t) + p1 * t */
ASM_STUB(void func_8C0402A4(NJS_POINT3 *p0, NJS_POINT3 *p1, NJS_POINT3 *out, Float t))
/* vertex key pair and rate of the current element; *count += 1 when it has keys */
ASM_STUB(void func_8C0402D0(Sint32 *count, Float *rate, NJS_POINT3 **out))
/* normal key pair and rate of the current element; *count += 2 when it has keys */
ASM_STUB(void func_8C04031E(Sint32 *count, Float *rate, NJS_VECTOR **out))

#ifdef NON_MATCHING
void func_8C0403C0(NJS_MODEL *model, Float rate, Sint32 n, NJS_POINT3 **src)
{
    NJS_POINT3 *buf;
    NJS_POINT3 *dst;
    NJS_POINT3 *p0;
    NJS_POINT3 *p1;
    Sint32 i;

    dst = B_8C89002C;
    buf = dst;
    p0 = src[0];
    p1 = src[1];
    i = n;
    do {
        func_8C0402A4(p0, p1, dst, rate);
        p0++;
        p1++;
        dst++;
    } while (--i != 0);
    model->points = buf;
}
#else
ASM_STUB(void func_8C0403C0(NJS_MODEL *model, Float rate, Sint32 n, NJS_POINT3 **src))
#endif

#ifdef NON_MATCHING
void func_8C040404(NJS_MODEL *model, Float rate, Sint32 n, NJS_VECTOR **src)
{
    NJS_VECTOR *buf;
    NJS_VECTOR *dst;
    NJS_VECTOR *p0;
    NJS_VECTOR *p1;
    Sint32 i;

    dst = B_8C89002C;
    buf = dst;
    p0 = src[0];
    p1 = src[1];
    i = n;
    do {
        func_8C040240(p0, p1, dst, rate);
        p0++;
        p1++;
        dst++;
    } while (--i != 0);
    model->normals = buf;
}
#else
ASM_STUB(void func_8C040404(NJS_MODEL *model, Float rate, Sint32 n, NJS_VECTOR **src))
#endif

#ifdef NON_MATCHING
void func_8C040448(NJS_MODEL *model, Float rate, Sint32 n, NJS_VECTOR **src)
{
    NJS_VECTOR *buf;
    NJS_VECTOR *dst;
    NJS_VECTOR *p0;
    NJS_VECTOR *p1;
    Sint32 i;

    dst = &B_8C89002C[n];
    buf = dst;
    p0 = src[0];
    p1 = src[1];
    i = n;
    do {
        func_8C040240(p0, p1, dst, rate);
        p0++;
        p1++;
        dst++;
    } while (--i != 0);
    model->normals = buf;
}
#else
ASM_STUB(void func_8C040448(NJS_MODEL *model, Float rate, Sint32 n, NJS_VECTOR **src))
#endif

#ifdef NON_MATCHING
void func_8C040498(NJS_OBJECT *obj)
{
    NJS_POINT3 *srcv[2];
    NJS_VECTOR *srcn[2];
    NJS_POINT3 *points;
    NJS_VECTOR *normals;
    Float ratev;
    Float raten;
    Sint32 count;
    NJS_MODEL *model;
    Sint32 n;

    model = obj->model;
    if (model == NULL || (obj->evalflags & 8)) {    /* NJD_EVAL_HIDE */
        return;
    }
    points = model->points;
    normals = model->normals;
    n = model->nbPoint;
    count = 0;
    /* the original leaves the rates unset; count only selects a channel whose rate was written */
    PC_ONLY(ratev = 0.0f; raten = 0.0f;)
    switch (B_8C749104) {
    case 1:
        func_8C0402D0(&count, &ratev, srcv);
        break;
    case 2:
        func_8C04031E(&count, &raten, srcn);
        break;
    case 3:
        func_8C0402D0(&count, &ratev, srcv);
        B_8C88FE5C.idx++;
        func_8C04031E(&count, &raten, srcn);
        break;
    }
    switch (count) {
    case 1:
        func_8C0403C0(model, ratev, n, srcv);
        break;
    case 2:
        func_8C040404(model, raten, n, srcn);
        break;
    case 3:
        func_8C0403C0(model, ratev, n, srcv);
        func_8C040448(model, raten, n, srcn);
        break;
    }
    func_8C037098(model);
    model->points = points;
    model->normals = normals;
}
#else
ASM_STUB(void func_8C040498(NJS_OBJECT *obj))
#endif

void func_8C04057A(NJS_OBJECT *obj)
{
    Float *p;
    Float z;
    Float y;
    Float x;

    p = obj->pos;
    x = *p;
    p++;
    y = *p;
    p++;
    z = *p;
    njTranslate(NULL, x, y, z);
}

#ifdef NON_MATCHING
void func_8C040588(NJS_OBJECT *obj)
{
    Angle *a;
    Angle z;
    Angle y;
    Angle x;

    a = obj->ang;
    y = a[1];
    z = a[2];
    x = a[0];
    njRotateXYZ(NULL, x, y, z);
}
#else
ASM_STUB(void func_8C040588(NJS_OBJECT *obj))
#endif
