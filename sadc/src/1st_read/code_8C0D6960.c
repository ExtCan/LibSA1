#include "sa1/sa1.h"

/*
 * E-102 hit sparks (1ST_READ chunk 8C0D6960-8C0D6B40):
 *   func_8C0D6960  creates the shared spark task D_8C4BB1CC (40 particles)
 *                  if it does not exist yet
 *   func_8C0D699A  emits 20 sparks at pos with random speed in [-2, 2)^3
 *   func_8C0D6A74  loads the hit textures (E102Main), then 8C0D6960
 *   func_8C0D6A84  frees the spark task (E-102 destructor 8C0CFA6E)
 *   func_8C0D6AA0  hit effect at pos (called by the E-101 boss and stage
 *                  overlays): a flash sprite task plus 20 sparks
 */

/*
 * Sprite list entry of the effect system 8C0502D8/8C050190/8C050914
 * (stride 0x2C; the list ends at a negative texid). 8C0502D8 fills tex from
 * the texlist entry and, with flag bit 0, the size from the texture's
 * width/height (+0x24/+0x28 of the texaddr target), with bit 1 the centre
 * (size * 0.5). Only the fields 8C0502D8 touches are named.
 */
typedef struct {
    Sint16 texid;               /* 0x00 texlist index, < 0 ends the list */
    Uint8 flag;                 /* 0x02 bit 0: size from texture, bit 1: centre */
    Uint8 pad03;                /* 0x03 */
    Uint32 tex;                 /* 0x04 first word of texlist->textures[texid].texaddr's target
                                   (NJS_TEXMEMLIST.globalIndex by SADX analogy: width/height at
                                   +0x24/+0x28 are texinfo.texsurface.nWidth/nHeight), not a pointer */
    Float sx;                   /* 0x08 */
    Float sy;                   /* 0x0C */
    Float cx;                   /* 0x10 */
    Float cy;                   /* 0x14 */
    Sint32 unk18[5];            /* 0x18 not used here */
} EFF_SPRANIM;                  /* 0x2C */

SA1_ASSERT_SIZE(EFF_SPRANIM, 0x2C);

/*
 * Parameters of the single-sprite effect task 8C050914 (copied by
 * 8C050400 into the task's 0x44-byte work).
 */
typedef struct {
    Sint32 flag;                /* 0x00 -> work+0x00 (nonzero: task gets an exec) */
    NJS_POINT3 ofs;             /* 0x04 offset from pos (8C050400 -> work+0x08) */
    Float unk10;                /* 0x10 -> work+0x20 */
    Float scl;                  /* 0x14 -> work+0x24 */
    Float unk18;                /* 0x18 -> work+0x28 */
    Float unk1C;                /* 0x1C -> work+0x2C */
    EFF_SPRANIM *anim;          /* 0x20 -> work+0x30 */
    Float unk24;                /* 0x24 -> work+0x38 */
} EFF_SPRPARAM;                 /* 0x28 */

SA1_ASSERT_SIZE(EFF_SPRPARAM, 0x28);
SA1_ASSERT_OFFSET(EFF_SPRPARAM, anim, 0x20);

extern task *D_8C4BB1CC;                /* spark particle task */
extern NJS_TEXLIST texlist_E102HIT;
extern EFF_SPRANIM D_8C15C694;          /* spark sprite animation */
extern EFF_SPRANIM D_8C15C3A8;          /* flash sprite animation */

/* effect system (1ST_READ 8C04FEA0-8C050A00) */
void func_8C04FF58(task *tp, NJS_POINT3 *pos, NJS_VECTOR *spd, Float frame, Float scl);
task *func_8C050190(Sint32 num, NJS_POINT2 *accel, EFF_SPRANIM *anim, Sint32 flag);
void func_8C0502D8(NJS_TEXLIST *tlist, EFF_SPRANIM *anim, Sint32 flag);
task *func_8C050914(NJS_POINT3 *pos, EFF_SPRPARAM *param, Sint32 flag);

void func_8C0D6960(void)
{
    NJS_POINT2 accel;

    if (D_8C4BB1CC == NULL) {
        func_8C0502D8(&texlist_E102HIT, &D_8C15C694, 0);
        accel.x = 0.03f;
        accel.y = 0.0f;
        D_8C4BB1CC = func_8C050190(40, &accel, &D_8C15C694, 0);
    }
}

/*
 * SHC unrolls the 20-iteration loop 2x (ADD #-2,R12): the 3 SA1_FMAC below
 * (one FMAC per speed component, rand() * (1/8192) + -2.0f) are the 6
 * FMACs of the asm; the frame (rand() * 0.000457763671875f) is a plain FMUL.
 */
void func_8C0D699A(NJS_POINT3 *pos, Float scl)
{
    NJS_VECTOR spd;
    Sint32 i;

    if (D_8C4BB1CC != NULL) {
        for (i = 0; i < 20; i++) {
            spd.x = SA1_FMAC((Float)rand(), 0.0001220703125f, -2.0f);
            spd.y = SA1_FMAC((Float)rand(), 0.0001220703125f, -2.0f);
            spd.z = SA1_FMAC((Float)rand(), 0.0001220703125f, -2.0f);
            func_8C04FF58(D_8C4BB1CC, pos, &spd, (Float)rand() * 0.000457763671875f, scl);
        }
    }
}

void func_8C0D6A74(void)
{
    func_8C0502D8(&texlist_E102HIT, &D_8C15C3A8, 0);
    func_8C0D6960();
}

void func_8C0D6A84(void)
{
    if (D_8C4BB1CC != NULL) {
        FreeTask(D_8C4BB1CC);
        D_8C4BB1CC = NULL;
    }
}

void func_8C0D6AA0(NJS_POINT3 *pos, Float scl)
{
    EFF_SPRPARAM prm;

    prm.flag = 0;
    prm.ofs.x = prm.ofs.y = prm.ofs.z = 0.0f;
    prm.unk10 = 1.0f;
    prm.scl = scl;
    prm.unk18 = 0.0f;
    prm.unk1C = 0.0f;
    prm.anim = &D_8C15C3A8;
    prm.unk24 = 1.0f;
    func_8C050914(pos, &prm, 1);
    func_8C0D699A(pos, scl);
}
