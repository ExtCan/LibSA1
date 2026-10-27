/*
 * 1ST_READ 8C09EE44-8C09F080: near-plane clipping of one sprite quad for the
 * sprite renderer func_8C09E778 -> func_8C09EB08 (the only caller).
 *
 * func_8C09EB08 transforms the four corners of a sprite (FTRV), projects
 * them (sx = cx + X * dist * aspx / Z, z field = 1/Z) and sets one bit per
 * corner that lies in front of the near plane B_8C88F610 (corner 0 = bit 3
 * ... corner 3 = bit 0). When some but not all corners are clipped it calls
 * this function, which:
 *   - recovers the view-space corners from the screen ones
 *     (X = (sx - cx) / (dist * aspx) / iz, Z = 1 / iz);
 *   - clips the two triangles (0,1,2) and (1,2,3) of the strip against
 *     Z = near, driven by the per-triangle action byte D_8C66C30C[mask]
 *     (mask = clip >> 1 for the first triangle, clip & 7 for the second):
 *     for each edge prev->cur (prev starts at the triangle's last corner)
 *     bit 2k = "emit the edge's intersection with the near plane",
 *     bit 2k+1 = "emit corner cur", bit 7 = the clipped polygon is a quad;
 *   - writes each result as a 32-byte PVR vertex into the store queue and
 *     flushes it (PREF): 0xE0000000 = vertex, 0xF0000000 = end of strip.
 *     A quad p0 p1 p2 p3 is sent as the strip p0 p1 p3 p2 (the third vertex
 *     is kept back and sent last as the end of the strip).
 * Table bytes (D_8C66C30C, asm data): 2A 9B B6 07 AD 1C 31 00.
 *
 * HAND-WRITTEN SH-4 ASSEMBLY (not SHC output; the whole region
 * 8C09E760-8C09F080 is a non-standard module, docs/compiler.md), so it
 * cannot match:
 *   - the store-queue pointer is passed in R2 and returned advanced in R2
 *     (func_8C09EB08 also takes it in R2 from func_8C09E778); clip mask in
 *     R4, corner array in R5 (R6/R7 are not arguments);
 *   - callee-saved R8 and FR12-FR15 are clobbered without being saved,
 *     R0/R1/R3 are used as loop/state registers, PREF @R2 after every vertex;
 *   - hand-scheduled pre-decrement stores and a frame set up as R6 = R15
 *     before the stack adjustment.
 * The C below describes what the assembly does; it is kept under
 * NON_MATCHING only and the original assembly stays in the build. The PC
 * prototype passes the store-queue pointer as the third argument and returns
 * it.
 *
 * Quirk of the original reproduced below (marked "sic"): FR13 holds 1/near,
 * the z of every intersection point, but sending a kept-back vertex loads its
 * z into FR13; when that vertex was an original corner, the intersection
 * points of the second triangle get that corner's z instead of 1/near.
 * The offset-intensity word (+0x1C) of each PVR vertex is never written.
 */
#include "common.h"
#include "ninja.h"

/* sprite corner as func_8C09EB08 builds it on its stack (stride 0x20) */
typedef struct {
    Uint32 pad00;               /* 0x00 not used */
    Float sx;                   /* 0x04 screen x */
    Float sy;                   /* 0x08 screen y */
    Float iz;                   /* 0x0C 1 / view z */
    Float u;                    /* 0x10 texture u */
    Float v;                    /* 0x14 texture v */
    Uint32 pad18[2];            /* 0x18 not used */
} SPRVTX;                       /* 0x20 */

/* PVR textured intensity vertex written through the store queue */
typedef struct {
    Uint32 cmd;                 /* 0x00 parameter control word */
    Float x;                    /* 0x04 */
    Float y;                    /* 0x08 */
    Float z;                    /* 0x0C 1/w */
    Float u;                    /* 0x10 */
    Float v;                    /* 0x14 */
    Float base;                 /* 0x18 base intensity (1.0f) */
    Float offset;               /* 0x1C offset intensity (not written) */
} SQVTX;                        /* 0x20 */

SA1_ASSERT_SIZE(SPRVTX, 0x20);
SA1_ASSERT_OFFSET(SPRVTX, iz, 0x0C);
SA1_ASSERT_OFFSET(SPRVTX, v, 0x14);
SA1_ASSERT_SIZE(SQVTX, 0x20);
SA1_ASSERT_OFFSET(SQVTX, base, 0x18);

#define SQ_CMD_VERTEX   0xE0000000u
#define SQ_CMD_EOS      0xF0000000u

extern Float B_8C88F5E4;        /* Ninja screen distance */
extern Float B_8C88F5F0;        /* screen centre x */
extern Float B_8C88F5F4;        /* screen centre y */
extern Float B_8C88F610;        /* near clip z */
extern Float B_8C88F618;        /* aspect x */
extern Float B_8C88F61C;        /* aspect y */
extern Sint8 D_8C66C30C[8];     /* clip actions per 3-bit triangle mask */

#ifdef NON_MATCHING
/* one PVR vertex into the store queue, then PREF (send it to the TA) */
static SQVTX *func_8C09EE44_put(SQVTX *sq, Uint32 cmd, Float x, Float y,
                                Float z, Float u, Float v)
{
    sq->base = 1.0f;
    sq->v = v;
    sq->u = u;
    sq->z = z;
    sq->y = y;
    sq->x = x;
    sq->cmd = cmd;
    return sq + 1;
}

SQVTX *func_8C09EE44(Uint32 clip, SPRVTX *vtx, SQVTX *sq)
{
    NJS_POINT3 view[4];         /* corners in view space (Z = 1 / iz) */
    SPRVTX tmp;                 /* kept-back intersection point */
    SPRVTX *defer;              /* R14: kept-back third vertex */
    SPRVTX *cur;                /* R13 */
    SPRVTX *prev;               /* R12 */
    NJS_POINT3 *vc;             /* R11 */
    NJS_POINT3 *vp;             /* R10 */
    Float clipz;
    Float cx;
    Float cy;
    Float kx;
    Float ky;
    Float ikx;
    Float iky;
    Float iz;                   /* FR13: 1 / near */
    Float w;
    Float t;
    Float sx;
    Float sy;
    Float u;
    Float v;
    Sint32 code;
    Sint32 n;
    Sint32 tri;
    Sint32 k;
    Sint32 i;
    Uint32 cmd;

    defer = &tmp;               /* always set before use (table entries with bit 7) */
    clipz = B_8C88F610;
    cx = B_8C88F5F0;
    cy = B_8C88F5F4;
    iz = 1.0f / clipz;
    kx = B_8C88F618 * B_8C88F5E4;
    ky = B_8C88F61C * B_8C88F5E4;
    ikx = 1.0f / kx;
    iky = 1.0f / ky;
    kx = kx * iz;
    ky = ky * iz;

    for (i = 3; i >= 0; i--) {
        w = 1.0f / vtx[i].iz;
        view[i].x = (vtx[i].sx - cx) * ikx * w;
        view[i].y = (vtx[i].sy - cy) * iky * w;
        view[i].z = w;
    }

    for (tri = 0; tri < 2; tri++) {
        if (tri == 0) {
            code = D_8C66C30C[clip >> 1];
            cur = &vtx[0];
            vc = &view[0];
        } else {
            code = D_8C66C30C[clip & 7];
            cur = &vtx[1];
            vc = &view[1];
        }
        prev = cur + 2;
        vp = vc + 2;
        cmd = SQ_CMD_VERTEX;
        n = 0;

        for (k = 0; k < 3; k++) {
            if (code & 1) {
                /* intersection of the edge prev -> cur with Z = near */
                t = (clipz - vp->z) / (vc->z - vp->z);
                sx = ((vc->x - vp->x) * t + vp->x) * kx + cx;
                sy = ((vc->y - vp->y) * t + vp->y) * ky + cy;
                u = (cur->u - prev->u) * t + prev->u;
                v = (cur->v - prev->v) * t + prev->v;
                n++;
                if (n == 3 && code < 0) {
                    tmp.sx = sx;
                    tmp.sy = sy;
                    tmp.iz = iz;
                    tmp.u = u;
                    tmp.v = v;
                    defer = &tmp;
                } else {
                    if (n == 3) {
                        cmd = SQ_CMD_EOS;
                    }
                    sq = func_8C09EE44_put(sq, cmd, sx, sy, iz, u, v);
                }
            }
            code >>= 1;
            if (code & 1) {
                /* corner cur itself */
                n++;
                if (n == 3 && code < 0) {
                    defer = cur;
                } else {
                    if (n == 3) {
                        cmd = SQ_CMD_EOS;
                    }
                    sq = func_8C09EE44_put(sq, cmd, cur->sx, cur->sy, cur->iz,
                                           cur->u, cur->v);
                }
            }
            code >>= 1;
            vp = vc;
            vc++;
            prev = cur;
            cur++;
        }

        if (code < 0) {
            /* quad: the kept-back third vertex ends the strip */
            iz = defer->iz;     /* sic: FR13 is reloaded with its z */
            sq = func_8C09EE44_put(sq, SQ_CMD_EOS, defer->sx, defer->sy, iz,
                                   defer->u, defer->v);
        }
    }
    return sq;
}
#else
ASM_STUB(SQVTX *func_8C09EE44(Uint32 clip, SPRVTX *vtx, SQVTX *sq))
#endif
