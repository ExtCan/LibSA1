/*
 * 1ST_READ 8C09E0B8 (chunk 8C09DDA0-8C09E2A0): effect (particle) lists,
 * continued from code_8C09DDFC.c.
 *
 *  func_8C09E0B8  draw every live particle of every effect list as a 3D
 *                 sprite (shared NJS_SPRITE B_8C7A0D2C), count them in
 *                 B_8C7A0D28
 *  func_8C09E200  reset the particle pool and free all effect lists
 *  func_8C09E236  return the particles of every list to the free pool
 *  func_8C09E27A  set the f40 value given to new particles (B_8C7A0D4C)
 */
#include "common.h"
#include "sa1/sa1.h"

struct sa1_effect_list;

/* Particle node (same layout as code_8C09DDFC.c; pool stride 0x4C in
 * func_8C09DDA0) */
typedef struct sa1_effect {
    struct sa1_effect *next;    /* DC 0x00 */
    void (*exec)(struct sa1_effect *ep, struct sa1_effect_list *list); /* DC 0x04 */
    Sint16 w08;                 /* DC 0x08 */
    Sint16 cell;                /* DC 0x0A animation cell */
    Angle ang;                  /* DC 0x0C */
    Float frame;                /* DC 0x10 */
    Float scl;                  /* DC 0x14 */
    NJS_POINT3 pos;             /* DC 0x18 */
    NJS_POINT3 spd;             /* DC 0x24 */
    NJS_ARGB argb;              /* DC 0x30 */
    Float f40;                  /* DC 0x40 B_8C7A0D4C at allocation */
    void *param;                /* DC 0x44 */
} SA1_EFFECT;                   /* DC 0x48 */

/* Sprite descriptor of an effect list (list->desc) */
typedef struct {
    NJS_TEXLIST *tlist;         /* DC 0x00 */
    NJS_TEXANIM *tanim;         /* DC 0x04 */
    Sint32 w08;                 /* DC 0x08 */
    Int src;                    /* DC 0x0C njColorBlendingMode(0, src) */
    Int dst;                    /* DC 0x10 njColorBlendingMode(1, dst) */
} SA1_EFFECT_DESC;

/* Effect list (CAlloc(1, 0x1C + extra) by func_8C09DE3A) */
typedef struct sa1_effect_list {
    struct sa1_effect_list *next; /* DC 0x00 list of lists B_8C7A0D20 */
    SA1_EFFECT *head;           /* DC 0x04 particles */
    void (*exec)(struct sa1_effect_list *list); /* DC 0x08 */
    Sint32 count;               /* DC 0x0C number of particles */
    SA1_EFFECT_DESC *desc;      /* DC 0x10 sprite descriptor */
    Sint32 flag;                /* DC 0x14 */
    void *work;                 /* DC 0x18 */
} SA1_EFFECT_LIST;              /* DC 0x1C */

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT, 0x48);
SA1_ASSERT_OFFSET(SA1_EFFECT, argb, 0x30);
SA1_ASSERT_OFFSET(SA1_EFFECT, f40, 0x40);
SA1_ASSERT_SIZE(SA1_EFFECT_DESC, 0x14);
SA1_ASSERT_SIZE(SA1_EFFECT_LIST, 0x1C);
SA1_ASSERT_OFFSET(SA1_EFFECT_LIST, desc, 0x10);
#endif

extern SA1_EFFECT *B_8C7A0D10;          /* particle pool */
extern Uint32 B_8C7A0D14;               /* pool size */
extern SA1_EFFECT *B_8C7A0D18;          /* free particle nodes */
extern Sint32 B_8C7A0D1C;               /* number of free nodes */
extern SA1_EFFECT_LIST *B_8C7A0D20;     /* all effect lists */
extern Sint32 B_8C7A0D24;               /* number of effect lists */
extern Sint32 B_8C7A0D28;               /* particles drawn last frame */
extern NJS_SPRITE B_8C7A0D2C;           /* sprite used to draw particles */
extern Float B_8C7A0D4C;
extern NJS_ARGB D_8C1C40B4;             /* default constant material */

extern void func_8C09E760(Float f);
extern void func_8C09E778(NJS_SPRITE *sp, Sint32 n, Uint32 attr);

ASM_STUB(void func_8C09DDA0(SA1_EFFECT *pool, Uint32 n))

void func_8C09E0B8(void)
{
    SA1_EFFECT_LIST *list;
    SA1_EFFECT_DESC *desc;
    SA1_EFFECT *ep;
    NJS_SPRITE *sp;
    Sint32 n;

    n = 0;
    func_8C63FAE8();
    for (list = B_8C7A0D20; list != NULL; list = list->next) {
        desc = list->desc;
        sp = &B_8C7A0D2C;
        ep = list->head;
        sp->tlist = desc->tlist;
        sp->tanim = desc->tanim;
        njColorBlendingMode(0, desc->src);
        njColorBlendingMode(1, desc->dst);
        for (; ep != NULL; ep = ep->next) {
            if (ep->exec != NULL) {
                njSetConstantMaterial(&ep->argb);
                sp->p = ep->pos;
                sp->sx = sp->sy = ep->scl;
                sp->ang = ep->ang;
                func_8C09E760(ep->f40);
                func_8C09E778(sp, ep->cell, 51);
                n++;
            }
        }
    }
    njSetConstantMaterial(&D_8C1C40B4);
    func_8C63FB18();
    B_8C7A0D28 = n;
}

void func_8C09E200(void)
{
    SA1_EFFECT_LIST *list;
    SA1_EFFECT_LIST *next;

    func_8C09DDA0(B_8C7A0D10, B_8C7A0D14);
    list = B_8C7A0D20;
    while (list != NULL) {
        next = list->next;
        MFree(list);
        list = next;
    }
    B_8C7A0D20 = NULL;
    B_8C7A0D24 = 0;
}

void func_8C09E236(void)
{
    SA1_EFFECT_LIST *list;
    SA1_EFFECT *ep;

    for (list = B_8C7A0D20; list != NULL; list = list->next) {
        if (list->head != NULL) {
            ep = list->head;
            while (ep->next != NULL) {
                ep->exec = NULL;
                ep = ep->next;
            }
            ep->next = B_8C7A0D18;
            B_8C7A0D18 = list->head;
            B_8C7A0D1C += list->count;
            list->head = NULL;
            list->count = 0;
        }
    }
}

void func_8C09E27A(Float f)
{
    B_8C7A0D4C = f;
}
