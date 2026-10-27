#include "common.h"
#include "ninja.h"
#include "sa1/ninja_types.h"
#include "sa1/shc_intrinsics.h"

/*
 * 8C09E880-8C09EB08: hand-written SH-4 assembly (not SHC output), part of
 * the sprite module 8C09E760-8C09F080 that docs/compiler.md lists among the
 * regions without SHC's 32-byte "00 09" pool padding. Caller: the sprite
 * renderer func_8C09E778 (also hand-written), which picks this routine when
 * attr has bit 0x10 (NJD_SPRITE_SCALE: perspective-scaled 3D sprite) and
 * func_8C09EB08 otherwise.
 *
 * Why it cannot be C:
 * - private register convention: besides R4 (sprite), R5 (texanim index)
 *   and R6 (attr) it takes the store-queue vertex cursor in R2 and returns
 *   it in R2 (advanced by 4 * 32 bytes when the quad is emitted) next to the
 *   status in R0; SHC never reads R2 as an input;
 * - FTRV XMTRX,FV4 on the matrix the caller left in the XF bank, and FSCA
 *   FPUL,DR10 emitted as ".DATA.W H'FAFD";
 * - outcode packing with FCMP/GT + ROTCL into registers seeded from pool
 *   words 0x00000000 / 0xFFFFFFFF (SHC would use MOV #0 / MOV #-1);
 * - vertices are written with pre-decrement stores and flushed with PREF
 *   (PVR store queue), UVs are spilled through R8 used as a frame pointer;
 * - the first pool sits inside the code after a BRA, 4-byte aligned.
 *
 * Quirk kept in the description: the y scale register FR5 is loaded from
 * offset 12 like FR4 (the MOV #16,R0 meant for sp->sy comes after the
 * load), so sp->sx scales both axes and sp->sy is never read.
 *
 * The body below is a C description of what the assembly does; it is kept
 * under NON_MATCHING only and the original assembly stays in the build.
 * libsa1 replaces the whole sprite path (8C09E778) with a no-op
 * (docs/libsa1_integration.md 4.4), so this body is documentation.
 */

#ifdef NON_MATCHING
/* PVR vertex parameter, textured, intensity mode, as written to the store
 * queue (32 bytes) */
typedef struct {
    Uint32 cmd;                 /* 0x00 0xE0000000 vertex, 0xF0000000 end of strip */
    Float x, y;                 /* 0x04 screen position */
    Float z;                    /* 0x0C 1/w */
    Float u, v;                 /* 0x10 */
    Float base;                 /* 0x18 base intensity (1.0f) */
    Float offset;               /* 0x1C offset intensity (not written) */
} SPRITE_SQ_VTX;                /* 0x20 */

extern Float D_8C66C314;        /* depth bias added before the clip test and 1/w */
extern Float B_8C88F5E4;        /* screen distance (projection scale) */
extern Float B_8C88F5F0;        /* screen centre x (read as B_8C88F5E4 + 12) */
extern Float B_8C88F5F4;        /* screen centre y (read as B_8C88F5E4 + 16) */
extern Float B_8C88F600[4];     /* screen clip rectangle: xmin, ymin, xmax, ymax */
extern Float B_8C88F610;        /* near clip (lower bound of z + bias) */
extern Float B_8C88F614;        /* far clip (upper bound of z + bias) */
extern Float B_8C88F618;        /* aspect x */
extern Float B_8C88F61C;        /* aspect y */

/* outcode of a screen point against B_8C88F600 (4 FCMP/GT + ROTCL) */
#define SPRITE_OUTCODE(px, py, r)                       \
    ((((py) > (r)[3]) << 3) | (((r)[1] > (py)) << 2) |  \
     (((px) > (r)[2]) << 1) | ((r)[0] > (px)))

/* vpp stands for R2 (store-queue vertex cursor, in/out); returns 1 when the
 * quad was emitted, 0 when it is outside the depth range or off screen */
Sint32 func_8C09E880(NJS_SPRITE *sp, Sint32 n, Uint32 attr, SPRITE_SQ_VTX **vpp) {
    NJS_TEXANIM *ta;
    SPRITE_SQ_VTX *vp;
    Float *clip;
    Float u1;
    Float v1;
    Float u2;
    Float v2;
    Float t;
    Float x0;
    Float y0;
    Float x1;
    Float y1;
    Float k;
    Float rz;
    Float oow;
    Float cx;
    Float cy;
    Float s;
    Float c;
    Float qx[4];
    Float qy[4];
    float vec[4];
    Uint32 code;
    Sint32 i;

    ta = &sp->tanim[n];
    u1 = (Float)ta->u1 * 0.00390625f;
    v1 = (Float)ta->v1 * 0.00390625f;
    u2 = (Float)ta->u2 * 0.00390625f;
    v2 = (Float)ta->v2 * 0.00390625f;
    if (attr & 4) {             /* NJD_SPRITE_HFLIP */
        t = u1;
        u1 = u2;
        u2 = t;
    }
    if (attr & 8) {             /* NJD_SPRITE_VFLIP */
        t = v1;
        v1 = v2;
        v2 = t;
    }

    /* quad corners around the texanim centre; sp->sx for both axes (see
     * the quirk above) */
    x0 = (Float)(-ta->cx) * sp->sx;
    y0 = (Float)(-ta->cy) * sp->sx;
    x1 = (Float)(ta->sx - ta->cx) * sp->sx;
    y1 = (Float)(ta->sy - ta->cy) * sp->sx;

    vec[0] = sp->p.x;
    vec[1] = sp->p.y;
    vec[2] = sp->p.z;
    vec[3] = 1.0f;
    _builtin_ftrv(vec, vec);
    rz = 1.0f / vec[2];
    oow = vec[2] + D_8C66C314;
    if (oow > B_8C88F614 || B_8C88F610 > oow) {
        return 0;
    }
    oow = 1.0f / oow;

    k = B_8C88F5E4 * rz;
    x0 = x0 * k;
    y0 = y0 * k;
    x1 = x1 * k;
    y1 = y1 * k;
    cx = vec[0] * (B_8C88F618 * k) + B_8C88F5F0;
    cy = vec[1] * (B_8C88F61C * k) + B_8C88F5F4;

    /* vertex order: (x0,y0) (x0,y1) (x1,y0) (x1,y1) */
    if (attr & 1) {             /* NJD_SPRITE_ANGLE */
        _builtin_fsca(sp->ang & 0xFFFF, &s, &c);
        qx[0] = x0 * c - y0 * s;
        qy[0] = x0 * s + y0 * c;
        qx[1] = x0 * c - y1 * s;
        qy[1] = x0 * s + y1 * c;
        qx[2] = x1 * c - y0 * s;
        qy[2] = x1 * s + y0 * c;
        qx[3] = x1 * c - y1 * s;
        qy[3] = x1 * s + y1 * c;
    } else {
        qx[0] = x0;
        qy[0] = y0;
        qx[1] = x0;
        qy[1] = y1;
        qx[2] = x1;
        qy[2] = y0;
        qx[3] = x1;
        qy[3] = y1;
    }
    for (i = 0; i < 4; i++) {
        qx[i] = qx[i] + cx;
        qy[i] = qy[i] + cy;
    }

    /* trivial reject: all four corners outside the same screen edge */
    clip = B_8C88F600;
    code = 0xFFFFFFFF;
    for (i = 0; i < 4; i++) {
        code &= (Uint32)SPRITE_OUTCODE(qx[i], qy[i], clip);
    }
    if (code != 0) {
        return 0;
    }

    /* one strip of 4 vertices, each flushed with PREF */
    vp = *vpp;
    for (i = 0; i < 4; i++) {
        vp[i].cmd = (i == 3) ? 0xF0000000 : 0xE0000000;
        vp[i].x = qx[i];
        vp[i].y = qy[i];
        vp[i].z = oow;
        vp[i].u = (i < 2) ? u1 : u2;
        vp[i].v = (i & 1) ? v2 : v1;
        vp[i].base = 1.0f;
    }
    *vpp = vp + 4;
    return 1;
}
#endif
