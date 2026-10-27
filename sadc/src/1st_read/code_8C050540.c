/*
 * Sprite effect drawing (1ST_READ chunk 8C0502D8-8C0509A0, functions 6-10):
 *   func_8C050540  draw one animated sprite (EFF_SPRANIM key list) at a
 *                  view-transformed position as a 4-vertex njDrawTexture
 *                  quad; the key size/centre/colour are interpolated between
 *                  the current key and the next one of the same texture
 *   func_8C050862  draw the sprite of a single-sprite work at its position
 *   func_8C050876  same at another position
 *   func_8C05088E  disp of the single-sprite task (8C050914)
 *   func_8C050892  default exec of the single-sprite task: draw, step
 *                  (8C0504DA) and free the task when the animation ended
 *
 * Callers of func_8C050540: the particle pool display func_8C050018
 * (code_8C04FEA0.c, mv = the 0x30-byte particle, aw = its animation state)
 * and the single-sprite work below.
 *
 * Matching notes (func_8C050540): the body is nested in the two draw
 * conditions (early returns load njCalcPoint into R3 instead of R2); in the
 * interpolating branch mv->scl is read before t is computed (else the vertex
 * pointers swap R4/R5); the 8 FMACs are written addend first (FMAC_ADD:
 * with "t * d + key" the blue channel is computed in FR2 instead of FR4).
 * func_8C050892 calls the disp function func_8C05088E, which SHC inlines.
 */
#include "sa1/sa1.h"

/*
 * Sprite animation key (stride 0x2C; a negative texid ends the list).
 * Defined with fewer names in code_8C0D6960.c (EFF_SPRANIM) and
 * code_8C0502A0.c (ANIMKEY).
 */
typedef struct EFF_SPRANIM {
    Sint16 texid;               /* 0x00 key list ends at < 0; interpolation only between keys of the same texid */
    Uint8 flag;                 /* 0x02 bit 2: additive blending (SRCALPHA, ONE) */
    Uint8 pad03;                /* 0x03 */
    Uint32 tex;                 /* 0x04 global texture index (njDrawTexture tex) */
    Float sx;                   /* 0x08 size (scaled by EFF_MOVE.scl) */
    Float sy;                   /* 0x0C */
    Float cx;                   /* 0x10 centre */
    Float cy;                   /* 0x14 */
    Float time;                 /* 0x18 cumulative key time */
    Float a;                    /* 0x1C colour, 0..1 */
    Float r;                    /* 0x20 */
    Float g;                    /* 0x24 */
    Float b;                    /* 0x28 */
} EFF_SPRANIM;                  /* 0x2C */

/* Sprite animation state (0x10): EFF_ANIMWK in code_8C04FEA0.c */
typedef struct {
    Float frame;                /* 0x00 */
    Float speed;                /* 0x04 */
    Sint32 no;                  /* 0x08 current key */
    Uint32 loop;                /* 0x0C */
} EFF_ANIMWK;

/*
 * Sprite motion (0x20): EFF_MOVE in code_8C04FEA0.c, which calls +0x18
 * unk18 and +0x1C scl; here +0x18 scales the key size/centre and +0x1C is
 * added to the view-space z (near-clip test and the vertices' 1/z).
 */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 */
    NJS_VECTOR spd;             /* 0x0C */
    Float scl;                  /* 0x18 sprite size factor (1.0f by default) */
    Float zofs;                 /* 0x1C added to the view-space depth */
} EFF_MOVE;

/* Work of the single-sprite task 8C050914 (MAlloc(0x44) in task.awp, filled by 8C050400) */
typedef struct EFF_SPRWK {
    Sint32 (*func)(struct EFF_SPRWK *wk); /* 0x00 optional step function (exec 8C0508E8); -1 frees the task */
    NJS_POINT3 *pos;            /* 0x04 drawing position (&mv.pos or the caller's point) */
    EFF_MOVE mv;                /* 0x08 */
    NJS_POINT2 accel;           /* 0x28 for 8C04FEF6 */
    EFF_SPRANIM *anim;          /* 0x30 */
    EFF_ANIMWK aw;              /* 0x34 */
} EFF_SPRWK;                    /* 0x44 */

/* njDrawTexture vertex (SDK NJS_TEXTURE_VTX) */
typedef struct {
    Float x;                    /* 0x00 screen x */
    Float y;                    /* 0x04 screen y */
    Float z;                    /* 0x08 1/z */
    Float u;                    /* 0x0C */
    Float v;                    /* 0x10 */
    Uint32 col;                 /* 0x14 ARGB */
} NJS_TEXTURE_VTX;              /* 0x18 */

SA1_ASSERT_SIZE(EFF_SPRANIM, 0x2C);
SA1_ASSERT_OFFSET(EFF_SPRANIM, time, 0x18);
SA1_ASSERT_SIZE(EFF_ANIMWK, 0x10);
SA1_ASSERT_SIZE(EFF_MOVE, 0x20);
SA1_ASSERT_SIZE(EFF_SPRWK, 0x44);
SA1_ASSERT_OFFSET(EFF_SPRWK, mv, 0x08);
SA1_ASSERT_OFFSET(EFF_SPRWK, anim, 0x30);
SA1_ASSERT_OFFSET(EFF_SPRWK, aw, 0x34);
SA1_ASSERT_SIZE(NJS_TEXTURE_VTX, 0x18);

extern Sint32 B_8C754EC8;       /* nonzero: skip drawing */
extern Float B_8C88F610;        /* near clip depth (Ninja) */

void func_8C085F14(NJS_POINT3 *src, NJS_POINT3 *dst);  /* perspective projection (hand-written asm, code_8C085F14.c) */
void func_8C63ED08(NJS_TEXTURE_VTX *vtx, Int count, Int tex, Int flag);  /* njDrawTexture (R4-R7) */

ASM_STUB(Sint32 func_8C0504DA(EFF_SPRWK *wk))

/*
 * c + a * b at an FMAC site with the addend evaluated first (the key value of
 * the interpolation); fused like SA1_FMAC on PC. SA1_FMAC's "a * b + c" order
 * changes SHC's register choice for the last colour channel.
 */
#if defined(TARGET_PC)
#define FMAC_ADD(c, a, b) SA1_FMAC((a), (b), (c))
#else
#define FMAC_ADD(c, a, b) ((c) + (a) * (b))
#endif

void func_8C050540(NJS_POINT3 *pos, EFF_MOVE *mv, EFF_ANIMWK *aw, EFF_SPRANIM *anim)
{
    NJS_POINT3 calc;
    NJS_POINT3 proj;
    NJS_POINT3 size;
    NJS_POINT3 center;
    NJS_TEXTURE_VTX vtx[4];
    NJS_POINT3 ssize;
    NJS_POINT3 scenter;
    EFF_SPRANIM *key;
    EFF_SPRANIM *next;
    Uint32 color;
    Float t;
    Float a;
    Float r;
    Float g;
    Float b;
    Float rz;
    Float scl;

    key = &anim[aw->no];
    next = key + 1;
    if (B_8C754EC8 == 0) {
        njCalcPoint(NULL, pos, &calc);
        if (!(B_8C88F610 > calc.z + mv->zofs)) {
            func_8C085F14(&calc, &proj);
            if (key->flag & 4) {
                njColorBlendingMode(0, 8);
                njColorBlendingMode(1, 10);
            } else {
                njColorBlendingMode(0, 1);
            }
            if (aw->frame == key->time || key->texid != next->texid) {
                scl = mv->scl;
                size.x = key->sx * scl;
                size.y = key->sy * scl;
                center.x = key->cx * scl;
                center.y = key->cy * scl;
                color = ((Uint32)SA1_FTOI(key->a * 255.0f) << 24)
                      | ((Uint32)SA1_FTOI(key->r * 255.0f) << 16)
                      | ((Uint32)SA1_FTOI(key->g * 255.0f) << 8)
                      | (Uint32)SA1_FTOI(key->b * 255.0f);
            } else {
                scl = mv->scl;
                t = (aw->frame - key->time) / (next->time - key->time);
                size.x = FMAC_ADD(key->sx, t, next->sx - key->sx);
                size.y = FMAC_ADD(key->sy, t, next->sy - key->sy);
                center.x = FMAC_ADD(key->cx, t, next->cx - key->cx);
                center.y = FMAC_ADD(key->cy, t, next->cy - key->cy);
                size.x *= scl;
                size.y *= scl;
                center.x *= scl;
                center.y *= scl;
                a = FMAC_ADD(key->a, t, next->a - key->a);
                r = FMAC_ADD(key->r, t, next->r - key->r);
                g = FMAC_ADD(key->g, t, next->g - key->g);
                b = FMAC_ADD(key->b, t, next->b - key->b);
                color = ((Uint32)SA1_FTOI(a * 255.0f) << 24)
                      | ((Uint32)SA1_FTOI(r * 255.0f) << 16)
                      | ((Uint32)SA1_FTOI(g * 255.0f) << 8)
                      | (Uint32)SA1_FTOI(b * 255.0f);
            }
            size.z = proj.z;
            center.z = proj.z;
            func_8C085F14(&size, &ssize);
            func_8C085F14(&center, &scenter);
            vtx[0].u = vtx[1].u = 0.0f;
            vtx[2].u = vtx[3].u = 1.0f;
            vtx[0].v = vtx[2].v = 0.0f;
            vtx[1].v = vtx[3].v = 1.0f;
            vtx[0].col = vtx[1].col = vtx[2].col = vtx[3].col = color;
            rz = 1.0f / (proj.z + mv->zofs);
            vtx[0].z = vtx[1].z = vtx[2].z = vtx[3].z = rz;
            vtx[0].x = vtx[1].x = proj.x - scenter.x + 320.0f;
            vtx[2].x = vtx[3].x = proj.x - scenter.x + ssize.x + 320.0f;
            vtx[0].y = vtx[2].y = proj.y - scenter.y + 240.0f;
            vtx[1].y = vtx[3].y = proj.y - scenter.y + ssize.y + 240.0f;
            func_8C63ED08(vtx, 4, (Int)key->tex, 1);
        }
    }
}

void func_8C050862(EFF_SPRWK *wk)
{
    func_8C050540(wk->pos, &wk->mv, &wk->aw, wk->anim);
}

void func_8C050876(EFF_SPRWK *wk, NJS_POINT3 *pos)
{
    func_8C050540(pos, &wk->mv, &wk->aw, wk->anim);
}

void func_8C05088E(task *tp)
{
    EFF_SPRWK *wk;

    wk = tp->awp;
    func_8C050862(wk);
}

void func_8C050892(task *tp)
{
    EFF_SPRWK *wk;

    wk = tp->awp;
    func_8C05088E(tp);
    if (func_8C0504DA(wk) == -1) {
        FreeTask(tp);
    }
}
