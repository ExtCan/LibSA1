#ifndef NINJA_H
#define NINJA_H

#include "common.h"

/*
 * Ninja (Katana SDK graphics library) base types used by the game.
 *
 * PORTABILITY: the model/motion/texture structs below are game DATA. On the
 * PC target they are never compiled in: the runtime ripper deserialises them
 * from the user's 1ST_READ.BIN into these native structs (pointers widened).
 * Pointer fields stay pointer-typed; fields that hold a pointer at run time
 * but are integers in the SDK (NJS_TEXNAME.texaddr) use SA1_UPTR.
 * Ninja function prototypes: include/sa1/ninja_funcs.h; other SDK types
 * (NJS_ARGB, NJS_POINT3COL, NJS_SPRITE, ...): include/sa1/ninja_types.h.
 */

typedef Sint32 Angle;           /* 0x10000 = 360 degrees */

typedef struct {
    Float x, y, z;
} NJS_POINT3, NJS_VECTOR;

typedef struct {
    Angle x, y, z;
} Angle3;

typedef struct {
    Float x, y;
} NJS_POINT2;

typedef Float NJS_MATRIX[16];

typedef union {
    Uint32 color;
    struct {
        Uint8 b, g, r, a;
    } argb;
} NJS_COLOR;

typedef struct {
    Sint16 u, v;
} NJS_TEX;

/* Basic model (SA1 "basic" format) */
typedef struct {
    NJS_COLOR diffuse;
    NJS_COLOR specular;
    Float exponent;
    Uint32 attr_texId;
    Uint32 attrflags;
} NJS_MATERIAL;                 /* 0x14 */

typedef struct {
    Uint16 type_matId;
    Uint16 nbMesh;
    Sint16 *meshes;
    Uint32 *attrs;
    NJS_VECTOR *normals;
    NJS_COLOR *vertcolor;
    NJS_TEX *vertuv;
} NJS_MESHSET;                  /* 0x18 */

typedef struct {
    NJS_POINT3 *points;
    NJS_VECTOR *normals;
    Sint32 nbPoint;
    NJS_MESHSET *meshsets;
    NJS_MATERIAL *mats;
    Uint16 nbMeshset;
    Uint16 nbMat;
    NJS_POINT3 center;
    Float r;
} NJS_MODEL;                    /* 0x28 */

typedef struct njs_object {
    Uint32 evalflags;
    NJS_MODEL *model;
    Float pos[3];
    Angle ang[3];
    Float scl[3];
    struct njs_object *child;
    struct njs_object *sibling;
} NJS_OBJECT;                   /* 0x34 */

typedef struct {
    void *texname;
    Uint32 attr;
    SA1_UPTR texaddr;           /* NJS_TEXMANAGE * once loaded */
} NJS_TEXNAME;                  /* 0x0C */

typedef struct {
    NJS_TEXNAME *textures;
    Uint32 nbTexture;
} NJS_TEXLIST;                  /* 0x08 */

/*
 * Motion. mdata points to nbNode NJS_MDATAn entries (sa1/ninja_types.h),
 * n = number of channels set in type (NJD_MTYPE_*): node motions use
 * NJS_MDATA2/3 (pos, ang[, scl]: 8C057BC0 steps 0x10/0x18 per node),
 * shape motions NJS_MDATA2 (vertex, normal keys: 8C07B080, 8C040200).
 * nbFrame is read as Uint32 (unsigned-to-float fixup in 8C06CA30,
 * 8C0700C2) and as signed by 8C03FDC0 (FLOAT); inp_fn & 0xFF3F is the
 * interpolation, (inp_fn & 0xC0) the link flags (8C040A60).
 */
typedef struct {
    void *mdata;
    Uint32 nbFrame;
    Uint16 type;
    Uint16 inp_fn;
} NJS_MOTION;                   /* 0x0C */

/* NJS_MOTION.type channel bits (SDK NJD_MTYPE_*; 8C03FDC0, 8C079C20) */
#define NJD_MTYPE_POS_0     0x01
#define NJD_MTYPE_ANG_1     0x02
#define NJD_MTYPE_SCL_2     0x04    /* scale channel (NJS_MDATA3) */
#define NJD_MTYPE_VERT_4    0x10
#define NJD_MTYPE_NORM_5    0x20

/* NJS_OBJECT.evalflags (SDK NJD_EVAL_*; tested by 8C057BC0, 8C040840,
 * 8C04196C) */
#define NJD_EVAL_UNIT_POS 0x01
#define NJD_EVAL_UNIT_ANG 0x02
#define NJD_EVAL_UNIT_SCL 0x04
#define NJD_EVAL_HIDE       0x08
#define NJD_EVAL_BREAK      0x10
#define NJD_EVAL_ZXY_ANG    0x20
#define NJD_EVAL_SKIP       0x40
#define NJD_EVAL_SHAPE_SKIP 0x80

typedef struct {
    NJS_OBJECT *object;
    NJS_MOTION *motion;
} NJS_ACTION;                   /* 0x08 */

SA1_ASSERT_SIZE(NJS_POINT3, 0x0C);
SA1_ASSERT_SIZE(Angle3, 0x0C);
SA1_ASSERT_SIZE(NJS_MATRIX, 0x40);
SA1_ASSERT_SIZE(NJS_MATERIAL, 0x14);
SA1_ASSERT_SIZE(NJS_MESHSET, 0x18);
SA1_ASSERT_SIZE(NJS_MODEL, 0x28);
SA1_ASSERT_OFFSET(NJS_MODEL, center, 0x18);
SA1_ASSERT_SIZE(NJS_OBJECT, 0x34);
SA1_ASSERT_OFFSET(NJS_OBJECT, pos, 0x08);
SA1_ASSERT_OFFSET(NJS_OBJECT, ang, 0x14);
SA1_ASSERT_OFFSET(NJS_OBJECT, child, 0x2C);
SA1_ASSERT_SIZE(NJS_TEXNAME, 0x0C);
SA1_ASSERT_SIZE(NJS_TEXLIST, 0x08);
SA1_ASSERT_SIZE(NJS_MOTION, 0x0C);
SA1_ASSERT_OFFSET(NJS_MOTION, nbFrame, 0x04);
SA1_ASSERT_OFFSET(NJS_MOTION, type, 0x08);
SA1_ASSERT_OFFSET(NJS_MOTION, inp_fn, 0x0A);
SA1_ASSERT_OFFSET(NJS_MESHSET, meshes, 0x04);
SA1_ASSERT_OFFSET(NJS_MESHSET, normals, 0x0C);
SA1_ASSERT_OFFSET(NJS_MODEL, nbPoint, 0x08);
SA1_ASSERT_OFFSET(NJS_MODEL, meshsets, 0x0C);
SA1_ASSERT_OFFSET(NJS_MODEL, nbMeshset, 0x14);
SA1_ASSERT_OFFSET(NJS_OBJECT, scl, 0x20);
SA1_ASSERT_OFFSET(NJS_OBJECT, sibling, 0x30);
SA1_ASSERT_OFFSET(NJS_ACTION, motion, 0x04);
SA1_ASSERT_SIZE(NJS_ACTION, 0x08);

#endif