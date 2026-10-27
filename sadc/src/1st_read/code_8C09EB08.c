#include "common.h"
#include "ninja.h"
#include "sa1/ninja_types.h"
#include "sa1/shc_intrinsics.h"

/*
 * 8C09EB08-8C09EE44: hand-written SH-4 assembly (not SHC output), part of
 * the sprite module 8C09E760-8C09F080 that docs/compiler.md lists among the
 * regions without SHC's 32-byte "00 09" pool padding. Caller: the sprite
 * renderer func_8C09E778 (also hand-written), which calls it through a
 * pointer when attr lacks bit 0x10 (func_8C09E880 otherwise). It draws cell
 * n of sp->tanim as an unscaled 3D sprite: the four corners are offset from
 * sp->p in the local x/y plane (optionally rotated by sp->ang), each one is
 * transformed by the current matrix, projected and written as one 4-vertex
 * PVR strip; strips crossing the near plane go to the clipper
 * func_8C09EE44 instead.
 *
 * Why it cannot be C:
 * - private register convention: besides R4 (sprite), R5 (texanim index)
 *   and R6 (attr) it takes the store-queue vertex cursor in R2 and returns
 *   it in R2 (advanced by 4 * 32 bytes when the strip is emitted) next to
 *   the status in R0, and passes R2 on to func_8C09EE44 the same way;
 * - FTRV XMTRX,FV0/FV4/FV8/FV12 on the matrix the caller left in the XF
 *   bank, and FSCA FPUL,DR10 emitted as ".DATA.W H'FAFD";
 * - near/far and screen outcodes packed with FCMP/GT + ROTCL into
 *   registers seeded from pool words 0x00000000 / 0xFFFFFFFF (SHC would
 *   use MOV #0 / MOV #-1); the 1/256 UV scale is read with MOVA from a pool
 *   inside the code after a BRA;
 * - R8 is a frame pointer for the spilled UVs, the clipper's vertex records
 *   are built with pre-decrement stores (with uninitialised gaps), and the
 *   direct path writes the store queue with pre-decrement stores + PREF.
 *
 * The body below is a C description of what the assembly does; it is kept
 * under NON_MATCHING only and the original assembly stays in the build.
 * libsa1 replaces the whole sprite path (8C09E778) with a no-op
 * (docs/libsa1_integration.md 4.4), so this body is documentation.
 */

#ifdef NON_MATCHING
/* PVR vertex parameter, textured, intensity mode, as written to the store
 * queue (32 bytes); same layout as the clipper's input records */
typedef struct {
    Uint32 cmd;                 /* 0x00 0xE0000000 vertex, 0xF0000000 end of strip */
    Float x, y;                 /* 0x04 screen position */
    Float z;                    /* 0x0C 1/w */
    Float u, v;                 /* 0x10 */
    Float base;                 /* 0x18 base intensity (1.0f) */
    Float offset;               /* 0x1C offset intensity (not written) */
} SPRITE_SQ_VTX;                /* 0x20 */

extern Float B_8C88F5E4;        /* screen distance (projection scale) */
extern Float B_8C88F5F0;        /* screen centre x (read as B_8C88F5E4 + 12) */
extern Float B_8C88F5F4;        /* screen centre y (read as B_8C88F5E4 + 16) */
extern Float B_8C88F600[4];     /* screen clip rectangle: xmin, ymin, xmax, ymax */
extern Float B_8C88F610;        /* near clip (lower bound of view z) */
extern Float B_8C88F614;        /* far clip (upper bound of view z) */
extern Float B_8C88F618;        /* aspect x */
extern Float B_8C88F61C;        /* aspect y */

/* near-plane clipper of the same module (hand-written): near outcode in
 * R4 (bit 3 = vertex 0 .. bit 0 = vertex 3), the four projected vertex
 * records (x, y, 1/w, u, v used) in R5, store-queue cursor in R2 and out
 * (as code_8C09EE44.c models it: third argument and return value) */
SPRITE_SQ_VTX *func_8C09EE44(Uint32 nearcode, SPRITE_SQ_VTX *v, SPRITE_SQ_VTX *vp);  /* returns the cursor (R2) */

/* outcode of a screen point against B_8C88F600 (4 FCMP/GT + ROTCL) */
#define SPRITE_OUTCODE(px, py, r)                       \
    ((((py) > (r)[3]) << 3) | (((r)[1] > (py)) << 2) |  \
     (((px) > (r)[2]) << 1) | ((r)[0] > (px)))

/* vpp stands for R2 (store-queue vertex cursor, in/out); returns 1 when the
 * strip was emitted (directly or through the near clipper), 0 when all four
 * corners are beyond the far plane, all four are in front of the near
 * plane, or all four are outside the same screen edge */
Sint32 func_8C09EB08(NJS_SPRITE *sp, Sint32 n, Uint32 attr, SPRITE_SQ_VTX **vpp) {
    NJS_TEXANIM *ta;
    SPRITE_SQ_VTX *vp;
    SPRITE_SQ_VTX rec[4];
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
    Float s;
    Float c;
    Float kx;
    Float ky;
    Float qx[4];
    Float qy[4];
    float vec[4];
    float out[4];
    Uint32 farcode;
    Uint32 nearcode;
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

    /* quad corners around the texanim centre, in sprite units */
    x0 = (Float)(-ta->cx) * sp->sx;
    x1 = (Float)(ta->sx - ta->cx) * sp->sx;
    y0 = (Float)(-ta->cy) * sp->sy;
    y1 = (Float)(ta->sy - ta->cy) * sp->sy;

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

    /* view transform of each corner (w row of FTRV unused), 1/z and the
     * far/near outcodes (bit 3 = vertex 0 .. bit 0 = vertex 3) */
    farcode = 0;
    nearcode = 0;
    for (i = 0; i < 4; i++) {
        vec[0] = qx[i] + sp->p.x;
        vec[1] = qy[i] + sp->p.y;
        vec[2] = sp->p.z;
        vec[3] = 1.0f;
        _builtin_ftrv(vec, out);
        rec[i].x = out[0];
        rec[i].y = out[1];
        rec[i].z = 1.0f / out[2];
        farcode = (farcode << 1) | (out[2] > B_8C88F614);
        nearcode = (nearcode << 1) | (B_8C88F610 > out[2]);
    }
    if (farcode == 15 || nearcode == 15) {
        return 0;
    }

    /* projection and trivial reject: all four corners outside the same
     * screen edge */
    kx = B_8C88F5E4 * B_8C88F618;
    ky = B_8C88F5E4 * B_8C88F61C;
    clip = B_8C88F600;
    code = 0xFFFFFFFF;
    for (i = 0; i < 4; i++) {
        rec[i].x = rec[i].x * rec[i].z * kx + B_8C88F5F0;
        rec[i].y = rec[i].y * rec[i].z * ky + B_8C88F5F4;
        code &= (Uint32)SPRITE_OUTCODE(rec[i].x, rec[i].y, clip);
    }
    if (code != 0) {
        return 0;
    }

    rec[0].u = u1;
    rec[0].v = v1;
    rec[1].u = u1;
    rec[1].v = v2;
    rec[2].u = u2;
    rec[2].v = v1;
    rec[3].u = u2;
    rec[3].v = v2;
    if (nearcode != 0) {
        /* records on the stack; cmd, base and offset are left unset */
        *vpp = func_8C09EE44(nearcode, rec, *vpp);
        return 1;
    }

    /* one strip of 4 vertices, each flushed with PREF */
    vp = *vpp;
    for (i = 0; i < 4; i++) {
        vp[i].cmd = (i == 3) ? 0xF0000000 : 0xE0000000;
        vp[i].x = rec[i].x;
        vp[i].y = rec[i].y;
        vp[i].z = rec[i].z;
        vp[i].u = rec[i].u;
        vp[i].v = rec[i].v;
        vp[i].base = 1.0f;
    }
    *vpp = vp + 4;
    return 1;
}
#endif
