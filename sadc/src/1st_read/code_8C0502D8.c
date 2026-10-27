/*
 * Sprite effect system, part 2 (1ST_READ chunk 8C0502D8-8C0509A0):
 *   func_8C0502D8  resolves the texture of every key of a sprite animation
 *                  (optionally loading the texlist first) and fills the
 *                  sprite size/centre from the texture surface
 *   func_8C0503B6  resets an animation state (frame 0, speed 1, key 0)
 *   func_8C050400  initialises the 0x44-byte work of the single-sprite
 *                  effect task 8C050914 from its parameter block
 *   func_8C050470  steps an animation state along its key list
 *   func_8C0504DA  per-frame update of the single-sprite work (custom exec
 *                  or move particle + step animation)
 * The particle helpers 8C04FEA0/8C04FEF6 are in chunk 8C04FEA0.
 */
#include "sa1/sa1.h"

/*
 * Loaded texture (Ninja NJS_TEXMEMLIST, which NJS_TEXNAME.texaddr points to
 * once the texlist is loaded). 8C0502D8 reads globalIndex (+0x00) and the
 * surface size texinfo.texsurface.nWidth/nHeight (+0x24/+0x28, Uint32:
 * unsigned-to-float fixup with 4294967296.0f).
 */
typedef struct {
    Uint32 Type;                /* 0x00 */
    Uint32 BitDepth;            /* 0x04 */
    Uint32 PixelFormat;         /* 0x08 */
    Uint32 nWidth;              /* 0x0C */
    Uint32 nHeight;             /* 0x10 */
    Uint32 TextureSize;         /* 0x14 */
    Uint32 fSurfaceFlags;       /* 0x18 */
    Uint32 *pSurface;           /* 0x1C */
    Uint32 *pVirtual;           /* 0x20 */
    Uint32 *pPhysical;          /* 0x24 */
} NJS_TEXSURFACE;               /* 0x28 */

typedef struct {
    void *texaddr;              /* 0x00 */
    NJS_TEXSURFACE texsurface;  /* 0x04 */
} NJS_TEXINFO;                  /* 0x2C */

typedef struct {
    Uint32 globalIndex;         /* 0x00 */
    void *bank;                 /* 0x04 NJS_TEXPALETTE * */
    Uint32 tspparambuffer;      /* 0x08 */
    Uint32 texparambuffer;      /* 0x0C */
    Uint32 texaddr;             /* 0x10 */
    NJS_TEXINFO texinfo;        /* 0x14 */
    Uint16 count;               /* 0x40 */
    Uint16 dummy;               /* 0x42 */
} NJS_TEXMEMLIST;               /* 0x44 */

SA1_ASSERT_SIZE(NJS_TEXSURFACE, 0x28);
SA1_ASSERT_SIZE(NJS_TEXINFO, 0x2C);
SA1_ASSERT_SIZE(NJS_TEXMEMLIST, 0x44);
SA1_ASSERT_OFFSET(NJS_TEXMEMLIST, texinfo.texsurface.nWidth, 0x24);
SA1_ASSERT_OFFSET(NJS_TEXMEMLIST, texinfo.texsurface.nHeight, 0x28);

/*
 * Sprite animation key (stride 0x2C). A list ends at an entry with a negative
 * texid: -1 stops the animation, -2 loops it (tex = loop count).
 * Same layout as EFF_SPRANIM of code_8C0D6960.c.
 */
typedef struct {
    Sint16 texid;               /* 0x00 texlist index; -1 end, -2 loop */
    Uint8 flag;                 /* 0x02 bit 0: size from texture, bit 1: centre = size / 2 */
    Uint8 pad03;                /* 0x03 */
    Uint32 tex;                 /* 0x04 NJS_TEXMEMLIST.globalIndex (8C0502D8); loop count of a -2 entry (8C050470, unsigned compare) */
    Float sx;                   /* 0x08 sprite width */
    Float sy;                   /* 0x0C sprite height */
    Float cx;                   /* 0x10 centre x */
    Float cy;                   /* 0x14 centre y */
    Float time;                 /* 0x18 key frame */
    Float a, r, g, b;           /* 0x1C colour, 0..1 (8C050540 scales by 255) */
} EFF_SPRANIM;                  /* 0x2C */

SA1_ASSERT_SIZE(EFF_SPRANIM, 0x2C);
SA1_ASSERT_OFFSET(EFF_SPRANIM, time, 0x18);

/* Animation state (8C0503B6 resets it, 8C050470 steps it) */
typedef struct {
    Float frame;                /* 0x00 */
    Float speed;                /* 0x04 frames per update */
    Sint32 key;                 /* 0x08 current key index */
    Sint32 loop;                /* 0x0C loops done (-2 end entry) */
} EFF_ANIMST;                   /* 0x10 */

SA1_ASSERT_SIZE(EFF_ANIMST, 0x10);

/* Particle (8C04FEA0 initialises it, 8C04FEF6 moves it) */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 */
    NJS_VECTOR spd;             /* 0x0C */
    Float scl;                  /* 0x18 sprite scale (8C050540), 1.0 by default */
    Float depth;                /* 0x1C added to the view z for the near test (8C050540) */
} EFF_PTCL;                     /* 0x20 */

SA1_ASSERT_SIZE(EFF_PTCL, 0x20);

typedef struct eff_sprwk EFF_SPRWK;

/* Parameters of the single-sprite effect task 8C050914 (EFF_SPRPARAM of
 * code_8C0D6960.c; 0x00 is a function pointer there typed Sint32 flag). */
typedef struct {
    Sint32 (*exec)(EFF_SPRWK *wk); /* 0x00 custom update, -1 ends the task (8C0508E8) */
    NJS_VECTOR spd;             /* 0x04 -> work ptcl.spd */
    Float scl;                  /* 0x10 -> work ptcl.scl */
    Float depth;                /* 0x14 -> work ptcl.depth */
    NJS_POINT2 accel;           /* 0x18 drag, gravity (8C04FEF6) */
    EFF_SPRANIM *anim;          /* 0x20 */
    Float speed;                /* 0x24 animation speed */
} EFF_SPRPARAM;                 /* 0x28 */

SA1_ASSERT_SIZE(EFF_SPRPARAM, 0x28);
SA1_ASSERT_OFFSET(EFF_SPRPARAM, anim, 0x20);

/* Work of the single-sprite effect task (MAlloc(0x44) in 8C050914) */
struct eff_sprwk {
    Sint32 (*exec)(EFF_SPRWK *wk); /* 0x00 */
    NJS_POINT3 *pos;            /* 0x04 &ptcl.pos or the caller's position */
    EFF_PTCL ptcl;              /* 0x08 */
    NJS_POINT2 accel;           /* 0x28 */
    EFF_SPRANIM *anim;          /* 0x30 */
    EFF_ANIMST st;              /* 0x34 */
};                              /* 0x44 */

SA1_ASSERT_SIZE(EFF_SPRWK, 0x44);
SA1_ASSERT_OFFSET(EFF_SPRWK, ptcl, 0x08);
SA1_ASSERT_OFFSET(EFF_SPRWK, accel, 0x28);
SA1_ASSERT_OFFSET(EFF_SPRWK, st, 0x34);

void func_8C04FEA0(EFF_PTCL *ptcl, NJS_POINT3 *pos, NJS_VECTOR *spd);
void func_8C04FEF6(EFF_PTCL *ptcl, NJS_POINT2 *accel);

void func_8C0502D8(NJS_TEXLIST *tlist, EFF_SPRANIM *anim, Sint32 flag)
{
    if (flag & 1) {
        func_8C608C82(tlist);
    }
    for (; anim->texid >= 0; anim++) {
        if (tlist->textures[anim->texid].texaddr != 0) {
            anim->tex = ((NJS_TEXMEMLIST *)tlist->textures[anim->texid].texaddr)->globalIndex;
            if (anim->flag & 1) {
                anim->sx = (Float)((NJS_TEXMEMLIST *)tlist->textures[anim->texid].texaddr)->texinfo.texsurface.nWidth;
                anim->sy = (Float)((NJS_TEXMEMLIST *)tlist->textures[anim->texid].texaddr)->texinfo.texsurface.nHeight;
            }
            if (anim->flag & 2) {
                anim->cx = anim->sx * 0.5f;
                anim->cy = anim->sy * 0.5f;
            }
        }
    }
}

void func_8C0503B6(EFF_ANIMST *st)
{
    st->frame = 0.0f;
    st->speed = 1.0f;
    st->key = 0;
    st->loop = 0;
}

void func_8C050400(EFF_SPRWK *wk, NJS_POINT3 *pos, EFF_SPRPARAM *prm, Sint32 flag)
{
    wk->exec = prm->exec;
    func_8C04FEA0(&wk->ptcl, pos, &prm->spd);
    if (flag & 1) {
        wk->pos = &wk->ptcl.pos;
    } else {
        wk->pos = pos;
    }
    wk->ptcl.scl = prm->scl;
    wk->ptcl.depth = prm->depth;
    wk->accel.x = prm->accel.x;
    wk->accel.y = prm->accel.y;
    wk->anim = prm->anim;
    func_8C0503B6(&wk->st);
    wk->st.speed = prm->speed;
}

/*
 * Steps the animation by st->speed. key is the entry after the current one
 * (its time ends the current key): returns -1 when the list has ended,
 * 0 while running, -2 when it looped back to key 0. At an -1 end entry, or
 * when a -2 entry's loop count is used up, the frame stops at the last key.
 *
 * NON_MATCHING: register allocation only. The -2 case gives
 * MOV.L @(12,R4),R3; ADD #1,R3; MOV R3,R1; ...; MOV.L @(4,R5),R2 and the
 * default case R3, where the original has R2, copy R3, count in R7 and R2
 * (tried: ++/+= forms, Uint32/Sint32 operand types, locals for the count or
 * the incremented value, if/else chain, declaration order).
 */
#ifdef NON_MATCHING
Sint32 func_8C050470(EFF_ANIMST *st, EFF_SPRANIM *anim)
{
    EFF_SPRANIM *key;
    EFF_SPRANIM *prev;

    key = &anim[st->key];
    key++;
    if (key->texid >= 0) {
        if (key->time <= (st->frame += st->speed)) {
            prev = key;
            key++;
            switch (key->texid) {
            case -1:
                st->frame = prev->time;
                return -1;
            case -2:
                st->loop++;
                if (st->loop < key->tex) {
                    st->frame -= prev->time;
                    st->key = 0;
                    return -2;
                }
                st->frame = prev->time;
                return -1;
            default:
                st->key++;
                break;
            }
        }
        return 0;
    }
    return -1;
}
#else
ASM_STUB(Sint32 func_8C050470(EFF_ANIMST *st, EFF_SPRANIM *anim))
#endif

Sint32 func_8C0504DA(EFF_SPRWK *wk)
{
    Sint32 ret;

    if (wk->exec != NULL) {
        ret = wk->exec(wk);
    } else {
        func_8C04FEF6(&wk->ptcl, &wk->accel);
        ret = func_8C050470(&wk->st, wk->anim);
    }
    return ret;
}
