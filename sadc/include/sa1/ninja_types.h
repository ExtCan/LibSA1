#ifndef SA1_NINJA_TYPES_H
#define SA1_NINJA_TYPES_H

/*
 * Ninja SDK types used by the engine/player code that are not in ninja.h.
 * Declared from the SDK interface (documentation + the layouts the game's
 * data and matched code use); no SDK code. Some older src/ files define their
 * own copies of a few of these (NJS_ARGB, NJS_POINT3COL, ...): new files
 * include this header instead.
 */

#include "ninja.h"

typedef Float *NJS_MATRIX_PTR;  /* NULL = current matrix of the Ninja stack */

/* njSetConstantMaterial argument (8C60EDF0) */
typedef struct {
    Float a;                    /* 0x00 */
    Float r;                    /* 0x04 */
    Float g;                    /* 0x08 */
    Float b;                    /* 0x0C */
} NJS_ARGB;                     /* 0x10 */

/* njDrawLine3D (8C63CE24) point list; colours/uvs may be NULL */
typedef struct {
    NJS_POINT3 *p;              /* 0x00 */
    NJS_COLOR *col;             /* 0x04 */
    NJS_TEX *tex;               /* 0x08 */
    Uint32 num;                 /* 0x0C */
} NJS_POINT3COL;                /* 0x10 */

typedef struct {
    NJS_POINT2 *p;              /* 0x00 */
    NJS_COLOR *col;             /* 0x04 */
    NJS_TEX *tex;               /* 0x08 */
    Uint32 num;                 /* 0x0C */
} NJS_POINT2COL;                /* 0x10 */

/* Sprite animation cell. Verified on D_8C1BE9C8: 10 halfwords per entry
 * (16,16,0,0,0,0,255,255,0,0x20 / 64,64,34,32,0,0,255,255,1,0x20). */
typedef struct {
    Sint16 sx, sy;              /* 0x00 size */
    Sint16 cx, cy;              /* 0x04 centre */
    Sint16 u1, v1;              /* 0x08 upper left */
    Sint16 u2, v2;              /* 0x0C lower right */
    Sint16 texid;               /* 0x10 */
    Sint16 attr;                /* 0x12 */
} NJS_TEXANIM;                  /* 0x14 */

/* njDrawSprite2D/3D (8C63EEB4/8C63EF74); layout as used by matched code
 * (code_8C014480.c, code_8C0817E0.c) */
typedef struct {
    NJS_POINT3 p;               /* 0x00 */
    Float sx, sy;               /* 0x0C */
    Angle ang;                  /* 0x14 */
    NJS_TEXLIST *tlist;         /* 0x18 */
    NJS_TEXANIM *tanim;         /* 0x1C */
} NJS_SPRITE;                   /* 0x20 */

/* njDistanceP2L (8C640080): point + direction */
typedef struct {
    Float px, py, pz;           /* 0x00 */
    Float vx, vy, vz;           /* 0x0C */
} NJS_LINE, NJS_PLANE;          /* 0x18 */

/* Sphere (SDK NJS_SPHERE): njCollisionCheckSS 8C63FE54 (code_8C057460.c
 * reads c at +0x00..+0x08 and r at +0x0C of both arguments) */
typedef struct {
    NJS_POINT3 c;               /* 0x00 centre */
    Float r;                    /* 0x0C radius */
} NJS_SPHERE;                   /* 0x10 */

/*
 * Motion key frames and per-node motion data (SDK NJS_MKEY_* / NJS_MDATAn).
 * Canonical names (NJS_MKEY_F, NJS_MDATA2, ...) are still defined locally
 * by unmigrated src files, so the shared definitions are named sa1_<name>
 * here and sa1/pending.h maps the SDK names to them.
 *  - NJS_MKEY_F / NJS_MKEY_A: stride 16, keyframe at +0 (compared unsigned,
 *    CMP/HS in 8C03FEB8; converted signed, FLOAT in 8C03FF90), 3 values at
 *    +4..+0xC (8C057BC0, 8C03FDC0, 8C079C20).
 *  - NJS_MKEY_P: stride 8, keyframe + pointer to nbPoint vertices or
 *    normals (shape motions: 8C040200, 8C03FEF2, 8C07B080).
 *  - NJS_MDATAn: n key-list pointers then n key counts; nb starts at
 *    +4*n and the entry is 8*n bytes (8C0405D2/8C040612 step 8/16,
 *    8C057BC0 steps 0x10/0x18, PSetMotion CAlloc(111, 0x18)).
 * PORTABILITY: game data (pointers): rebuilt natively by the ripper.
 */
typedef struct {
    Uint32 keyframe;            /* 0x00 */
    Float key[3];               /* 0x04 */
} sa1_NJS_MKEY_F;               /* 0x10 */

typedef struct {
    Uint32 keyframe;            /* 0x00 */
    Angle key[3];               /* 0x04 */
} sa1_NJS_MKEY_A;               /* 0x10 */

typedef struct {
    Uint32 keyframe;            /* 0x00 */
    NJS_POINT3 *key;            /* 0x04 vertex or normal array of this key */
} sa1_NJS_MKEY_P;               /* 0x08 */

typedef struct {
    void *p[1];                 /* 0x00 */
    Uint32 nb[1];               /* 0x04 */
} sa1_NJS_MDATA1;               /* 0x08 */

typedef struct {
    void *p[2];                 /* 0x00 node: pos, ang keys; shape: vertex, normal keys */
    Uint32 nb[2];               /* 0x08 */
} sa1_NJS_MDATA2;               /* 0x10 */

typedef struct {
    void *p[3];                 /* 0x00 pos, ang, scl keys */
    Uint32 nb[3];               /* 0x0C */
} sa1_NJS_MDATA3;               /* 0x18 */

typedef struct {
    void *p[4];                 /* 0x00 */
    Uint32 nb[4];               /* 0x10 */
} sa1_NJS_MDATA4;               /* 0x20 */

/* Linked (blended) motion (SDK NJS_MOTION_LINK; layout confirmed by
 * 8C040AF4/8C040B90: motion[n] at n*4, frame[n] at 8+n*4; 8C041C92 reads
 * NJS_ACTION_LINK object/motionlink). PSetMotion 8C079476 does not use it:
 * it builds a plain NJS_ACTION + NJS_MOTION (nbFrame 2, type 3, inp_fn 2,
 * mdata CAlloc(111, 0x18)). */
typedef struct {
    NJS_MOTION *motion[2];      /* 0x00 */
    Float frame[2];             /* 0x08 */
} NJS_MOTION_LINK;              /* 0x10 */

typedef struct {
    NJS_OBJECT *object;         /* 0x00 */
    NJS_MOTION_LINK *motionlink; /* 0x04 */
} NJS_ACTION_LINK;              /* 0x08 */

SA1_ASSERT_SIZE(NJS_ARGB, 0x10);
SA1_ASSERT_SIZE(NJS_POINT3COL, 0x10);
SA1_ASSERT_SIZE(NJS_POINT2COL, 0x10);
SA1_ASSERT_SIZE(NJS_TEXANIM, 0x14);
SA1_ASSERT_SIZE(NJS_SPRITE, 0x20);
SA1_ASSERT_OFFSET(NJS_SPRITE, tlist, 0x18);
SA1_ASSERT_SIZE(NJS_LINE, 0x18);
SA1_ASSERT_SIZE(NJS_SPHERE, 0x10);
SA1_ASSERT_OFFSET(NJS_SPHERE, r, 0x0C);
SA1_ASSERT_SIZE(sa1_NJS_MKEY_F, 0x10);
SA1_ASSERT_OFFSET(sa1_NJS_MKEY_F, key, 0x04);
SA1_ASSERT_SIZE(sa1_NJS_MKEY_A, 0x10);
SA1_ASSERT_OFFSET(sa1_NJS_MKEY_A, key, 0x04);
SA1_ASSERT_SIZE(sa1_NJS_MKEY_P, 0x08);
SA1_ASSERT_OFFSET(sa1_NJS_MKEY_P, key, 0x04);
SA1_ASSERT_SIZE(sa1_NJS_MDATA1, 0x08);
SA1_ASSERT_OFFSET(sa1_NJS_MDATA1, nb, 0x04);
SA1_ASSERT_SIZE(sa1_NJS_MDATA2, 0x10);
SA1_ASSERT_OFFSET(sa1_NJS_MDATA2, nb, 0x08);
SA1_ASSERT_SIZE(sa1_NJS_MDATA3, 0x18);
SA1_ASSERT_OFFSET(sa1_NJS_MDATA3, nb, 0x0C);
SA1_ASSERT_SIZE(sa1_NJS_MDATA4, 0x20);
SA1_ASSERT_OFFSET(sa1_NJS_MDATA4, nb, 0x10);
SA1_ASSERT_SIZE(NJS_MOTION_LINK, 0x10);
SA1_ASSERT_OFFSET(NJS_MOTION_LINK, frame, 0x08);
SA1_ASSERT_SIZE(NJS_ACTION_LINK, 0x08);

#endif