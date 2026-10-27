#include "common.h"
#include "ninja.h"
#include "sa1/ninja_funcs.h"
#include "sa1/shc_intrinsics.h"

/*
 * 8C028620-8C028BC0: land collision polygon builder. Turns the meshsets of
 * a land object tree (NJS_OBJECT) into world-space collision triangles
 * (COLPOLY, 0x40 bytes) that func_8C02CFC0 hands out from a free list kept
 * in 128 buckets sorted by z. func_8C028EC2 calls func_8C028B00 for each
 * entry of the land collision list (B_8C754EF4).
 *
 * COMPILER FLAGS: this object was built with -extra=a=800, not the
 * project's -extra=a=1800. Its 16-bit constants (0x2000, 0x4000, 0x7FFF,
 * 0x400, the 0x3040 frame size) are MOV.W pool loads; bit 0x1000 of
 * a=1800 would build them as MOV #imm / SHLL8. The 32-byte pool alignment
 * (bit 0x800) is present. With the build's single flag set none of these
 * functions can match, so all four are guarded by NON_MATCHING and the
 * build keeps the original assembly. func_8C028620 and func_8C028666 are
 * byte-identical with
 *   py -3.12 tools/shcdiff.py <func> src/1st_read/code_8C028620.c
 *       --flags="-define=NON_MATCHING -extra=a=800"
 * and can be unguarded once the build supports per-file flags.
 */

/* Collision triangle (pool B_8C6BE27C, 0x2000 entries, r cleared by
 * func_8C028E60) */
typedef struct colpoly {
    struct colpoly *next;       /* 0x00 bucket list, increasing center.z (func_8C02CFC0) */
    struct colpoly *prev;       /* 0x04 */
    NJS_POINT3 center;          /* 0x08 centroid */
    Float r;                    /* 0x14 bounding radius squared (0 = free slot) */
    NJS_POINT3 v[3];            /* 0x18 vertices (world space) */
    Uint32 attr;                /* 0x3C NJS_MATERIAL.attrflags of the meshset */
} COLPOLY;                      /* 0x40 */

SA1_ASSERT_SIZE(COLPOLY, 0x40);
SA1_ASSERT_OFFSET(COLPOLY, center, 0x08);
SA1_ASSERT_OFFSET(COLPOLY, r, 0x14);
SA1_ASSERT_OFFSET(COLPOLY, v, 0x18);
SA1_ASSERT_OFFSET(COLPOLY, attr, 0x3C);

#define STRIP_BUF_MAX 0x400

/* Vertices of one polygon or strip of a meshset (on the stack of
 * func_8C0287A0; same layout as MESHBUF in code_8C027360.c) */
typedef struct {
    Sint32 type;                /* 0x00 NJS_MESHSET.type_matId & 0xC000 */
    Sint32 n;                   /* 0x04 vertex count (3, 4 or the strip length) */
    Sint16 *meshes;             /* 0x08 cursor into NJS_MESHSET.meshes */
    NJS_MODEL *model;           /* 0x0C */
    NJS_POINT3 v[STRIP_BUF_MAX]; /* 0x10 transformed vertices */
} STRIPBUF;                     /* 0x3010 */

SA1_ASSERT_SIZE(STRIPBUF, 0x3010);
SA1_ASSERT_OFFSET(STRIPBUF, model, 0x0C);
SA1_ASSERT_OFFSET(STRIPBUF, v, 0x10);

extern COLPOLY *B_8C6BB270;     /* free-slot cursor (only func_8C028620 uses it) */
extern Sint32 B_8C6BB274;       /* index of B_8C6BB270 */
extern Float B_8C73EDEC;        /* z origin of the bucket sort */
extern Float B_8C73EDF0;        /* largest z extent of a collision triangle */
extern Sint8 tcrf_CollisionDisplayVar;  /* 8C752BDC */
extern char D_8C112470[];       /* "WARNING: STRIP buf is FULL" */

extern Float func_8C02CF68(Float x, Float y, Float z);  /* x*x + y*y + z*z (FIPR) */
extern COLPOLY *func_8C02CFC0(Float z);                  /* takes a free COLPOLY into the bucket of z */

Sint32 func_8C028666(STRIPBUF *sb);
Sint32 func_8C0287A0(NJS_OBJECT *obj, Uint32 attr, void *grp);
NJS_OBJECT *func_8C028B00(NJS_OBJECT *obj, Uint32 attr, void *grp);

#ifdef NON_MATCHING
/* MATCH with -extra=a=800 (see the file comment).
 * Advances B_8C6BB270 to the next free COLPOLY (r == 0); NULL once all
 * 0x2000 have been looked at. No callers. */
void func_8C028620(void)
{
    while (B_8C6BB274 < 0x2000 && B_8C6BB270->r != 0.0f) {
        B_8C6BB274++;
        B_8C6BB270++;
    }
    if (B_8C6BB274 >= 0x2000) {
        B_8C6BB270 = NULL;
    }
}
#else
ASM_STUB(void func_8C028620(void))
#endif

#ifdef NON_MATCHING
/* MATCH with -extra=a=800 (see the file comment).
 * Transforms the next polygon (triangle/quad) or strip of sb->meshes by the
 * current matrix into sb->v. Returns 1 for a strip whose header has the
 * flip bit (0x8000), i.e. whose first triangle has reversed winding. */
Sint32 func_8C028666(STRIPBUF *sb)
{
    NJS_POINT3 *v;
    Sint32 i;
    Sint32 flip;

    flip = 0;
    switch (sb->type) {
    case 0x0000:
    case 0x4000:
        v = sb->v;
        for (i = sb->n; i > 0; i--) {
            njCalcPoint(NULL, &sb->model->points[*sb->meshes], v);
            sb->meshes++;
            v++;
        }
        break;
    case 0x8000:
    case 0xC000:
        sb->n = *sb->meshes & 0x7FFF;
        if (*sb->meshes & 0x8000) {
            flip = 1;
        }
        if (sb->n > STRIP_BUF_MAX) {
            sb->n = STRIP_BUF_MAX;
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0x00020012, D_8C112470);
            }
        }
        sb->meshes++;
        v = sb->v;
        for (i = sb->n; i > 0; i--) {
            njCalcPoint(NULL, &sb->model->points[*sb->meshes], v);
            sb->meshes++;
            v++;
        }
        break;
    }
    return flip;
}
#else
ASM_STUB(Sint32 func_8C028666(STRIPBUF *sb))
#endif

#ifdef NON_MATCHING
/*
 * Complete; differs from the original only in float/int register
 * allocation (with -extra=a=800): the original keeps y2, z0, z1, z2 in
 * FR12/FR14/FR13/FR15 and spills cx, cy, cz, x0..x2, y0, y1 to
 * sp+24/28/8/20/16/12/4/0; SHC's choice reacts to statement order and to
 * "x -= c" vs "x = x - c" in ways no tried variant reproduced.
 *
 * Builds the collision triangles of obj->model under the current matrix:
 * every triangle, quad (as 2 triangles) and strip of every meshset becomes
 * one COLPOLY with its centroid, radius^2 and material attribute; strip
 * winding alternates (flip). Also records the largest z extent in
 * B_8C73EDF0. Returns 0 when the COLPOLY pool is exhausted, else 1.
 * attr and grp are passed through by func_8C028B00 but not used.
 */
Sint32 func_8C0287A0(NJS_OBJECT *obj, Uint32 attr, void *grp)
{
    STRIPBUF sb;
    Sint32 flip;
    Sint32 nbMeshset;
    Sint32 nbMesh;
    NJS_MESHSET *meshset;
    NJS_MATERIAL *mats;
    NJS_POINT3 *v;
    COLPOLY *poly;
    Sint32 i;
    Float x0, y0, z0;
    Float x1, y1, z1;
    Float x2, y2, z2;
    Float cx, cy, cz;
    Float d, e, f, r;

    PC_ONLY((void)attr; (void)grp;)
    sb.model = obj->model;
    nbMeshset = sb.model->nbMeshset;
    meshset = sb.model->meshsets;
    for (; nbMeshset > 0; nbMeshset--, meshset++) {
        switch (sb.type = meshset->type_matId & 0xC000) {
        case 0x0000:
            sb.n = 3;
            break;
        case 0x4000:
            sb.n = 4;
            break;
        case 0x8000:
        case 0xC000:
            break;
        default:
            continue;
        }
        nbMesh = meshset->nbMesh;
        sb.meshes = meshset->meshes;
        for (; nbMesh > 0; nbMesh--) {
            flip = func_8C028666(&sb);
            for (i = 0, v = sb.v; i < sb.n - 2; i++, v++) {
                x0 = v[0].x;
                x1 = v[1].x;
                x2 = v[2].x;
                y0 = v[0].y;
                y1 = v[1].y;
                y2 = v[2].y;
                z2 = v[2].z;
                z1 = v[1].z;
                z0 = v[0].z;
                cx = (x0 + x1 + x2) * 0.3333f;
                cy = (y0 + y1 + y2) * 0.3333f;
                cz = (z0 + z1 + z2) * 0.3333f;
                if ((poly = func_8C02CFC0(cz - B_8C73EDEC)) != NULL) {
                    if (((flip + i) & 1) == 0) {
                        poly->v[0].x = x0;
                        poly->v[0].y = y0;
                        poly->v[0].z = z0;
                        poly->v[1].x = x1;
                        poly->v[1].y = y1;
                        poly->v[1].z = z1;
                        poly->v[2].x = x2;
                        poly->v[2].y = y2;
                        poly->v[2].z = z2;
                    } else {
                        poly->v[0].x = x0;
                        poly->v[0].y = y0;
                        poly->v[0].z = z0;
                        poly->v[1].x = x2;
                        poly->v[1].y = y2;
                        poly->v[1].z = z2;
                        poly->v[2].x = x1;
                        poly->v[2].y = y1;
                        poly->v[2].z = z1;
                    }
                    d = _builtin_fabsf(z1 - z0);
                    e = _builtin_fabsf(z2 - z1);
                    f = _builtin_fabsf(z0 - z2);
                    if (e > d) {
                        d = e;
                    }
                    if (f > d) {
                        d = f;
                    }
                    if (d > B_8C73EDF0) {
                        B_8C73EDF0 = d;
                    }
                    x0 -= cx;
                    y0 -= cy;
                    z0 -= cz;
                    x1 -= cx;
                    y1 -= cy;
                    z1 -= cz;
                    x2 -= cx;
                    y2 -= cy;
                    z2 -= cz;
                    poly->center.x = cx;
                    poly->center.y = cy;
                    poly->center.z = cz;
                    r = func_8C02CF68(x0, y0, z0);
                    d = func_8C02CF68(x1, y1, z1);
                    if (d > r) {
                        r = d;
                    }
                    d = func_8C02CF68(x2, y2, z2);
                    if (d > r) {
                        r = d;
                    }
                    poly->r = r;
                    mats = obj->model->mats;
                    if (mats != NULL) {
                        poly->attr = mats[meshset->type_matId & ~0xC000].attrflags;
                    } else {
                        poly->attr = 0;
                    }
                } else {
                    return 0;
                }
            }
        }
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C0287A0(NJS_OBJECT *obj, Uint32 attr, void *grp))
#endif

#ifdef NON_MATCHING
/*
 * Complete; with -extra=a=800 only temp registers differ (10 of 78
 * instructions): the original loads the 2nd/3rd angle and the njScaleV
 * address into R3, this compiles to R2. The angle goes through an unsigned
 * local, which SHC keeps in a stack slot (same idiom in func_8C028EC2 and
 * code_8C0876F4.c).
 *
 * Walks an object tree: builds the collision triangles of every node under
 * its accumulated transform (children first, then the node itself).
 * Returns obj->sibling, or NULL when the COLPOLY pool is exhausted (which
 * also ends the caller's sibling loop).
 */
NJS_OBJECT *func_8C028B00(NJS_OBJECT *obj, Uint32 attr, void *grp)
{
    Uint32 ang;
    NJS_OBJECT *child;

    njPushMatrix(NULL);
    njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
    if ((ang = (Uint32)obj->ang[2]) != 0) {
        njRotateZ(NULL, (Angle)ang);
    }
    if ((ang = (Uint32)obj->ang[1]) != 0) {
        njRotateY(NULL, (Angle)ang);
    }
    if ((ang = (Uint32)obj->ang[0]) != 0) {
        njRotateX(NULL, (Angle)ang);
    }
    njScaleV(NULL, (NJS_VECTOR *)obj->scl);
    child = obj->child;
    while (child != NULL) {
        child = func_8C028B00(child, attr, grp);
    }
    if (!func_8C0287A0(obj, attr, grp)) {
        njPopMatrix(1);
        return NULL;
    } else {
        njPopMatrix(1);
        return obj->sibling;
    }
}
#else
ASM_STUB(NJS_OBJECT *func_8C028B00(NJS_OBJECT *obj, Uint32 attr, void *grp))
#endif
