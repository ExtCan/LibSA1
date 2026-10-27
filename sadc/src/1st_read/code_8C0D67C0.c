/*
 * 1ST_READ 8C0D67C0 (chunk 8C0D67C0-8C0D6960): E-102 smoke particles.
 *
 *  func_8C0D67C0  exec of a smoke particle: animation cell from the frame
 *                 counter; at the last cell of the sprite descriptor the
 *                 particle is released (func_8C09DE34); otherwise frame,
 *                 velocity (drag + rise), position and scale advance by the
 *                 parameter block D_8C4BB178
 *  func_8C0D6870  spawn one smoke particle (pos, velocity, scale) in the
 *                 E-102 effect list D_8C4BB1C0 (muzzle smoke, 8C0D46CE)
 *  func_8C0D68FC  create that effect list (called by E102Main)
 */
#include "common.h"
#include "sa1/sa1.h"

/* Sprite descriptor of an effect list (D_8C4BB164: texlist_E102EFFECT,
 * cell table D_8C4BB0C4, 8, 8, 10) */
typedef struct {
    NJS_TEXLIST *texlist;       /* DC 0x00 */
    void *cells;                /* DC 0x04 animation cell table */
    Sint32 ncell;               /* DC 0x08 number of cells */
    Sint32 w0C;                 /* DC 0x0C */
    Sint32 w10;                 /* DC 0x10 */
} SA1_EFFECT_DESC;              /* DC 0x14 */

/* Effect spawn/animation parameters (15 floats; same shape as
 * D_8C1BE858 in code_8C07B500.c). D_8C4BB178 = 1.0, 0.015, 0.25, 0.06,
 * 0.01, 0 x6, 1.0 x4. */
typedef struct {
    Float scl;                  /* 0x00 */
    Float dscl;                 /* 0x04 scale step per frame */
    Float dframe;               /* 0x08 animation step per frame */
    Float drag;                 /* 0x0C velocity damping */
    Float rise;                 /* 0x10 added to spd.y per frame */
    NJS_POINT3 pos;             /* 0x14 */
    NJS_POINT3 spd;             /* 0x20 */
    NJS_ARGB argb;              /* 0x2C */
} SA1_EFFECT_PARAM;             /* 0x3C */

/* Particle node of the effect lists (func_8C09DDFC takes one from the free
 * list B_8C7A0D18, links it into the list, sets exec and f40 =
 * B_8C7A0D4C; func_8C09DE34 releases it by clearing exec) */
struct sa1_effect_list;
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
    SA1_EFFECT_PARAM *param;    /* DC 0x44 */
} SA1_EFFECT;                   /* DC 0x48 */

/* Effect list (CAlloc(1, 0x1C + extra) by func_8C09DE3A) */
typedef struct sa1_effect_list {
    struct sa1_effect_list *next; /* DC 0x00 list of lists B_8C7A0D20 */
    SA1_EFFECT *head;           /* DC 0x04 */
    Sint32 w08;                 /* DC 0x08 */
    Sint32 count;               /* DC 0x0C */
    SA1_EFFECT_DESC *desc;      /* DC 0x10 */
} SA1_EFFECT_LIST;

/* D_8C4BB1B4: {&D_8C4BB164, 0, 0}; the first word is passed on */
typedef struct {
    SA1_EFFECT_DESC *desc;      /* DC 0x00 */
    Sint32 w04;                 /* DC 0x04 */
    Sint32 w08;                 /* DC 0x08 */
} SA1_EFFECT_DEF;

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT_PARAM, 0x3C);
SA1_ASSERT_SIZE(SA1_EFFECT, 0x48);
SA1_ASSERT_OFFSET(SA1_EFFECT, param, 0x44);
SA1_ASSERT_OFFSET(SA1_EFFECT_LIST, desc, 0x10);
SA1_ASSERT_OFFSET(SA1_EFFECT_DESC, ncell, 0x08);
#endif

typedef void (*SA1_EFFECT_EXEC)(SA1_EFFECT *ep, SA1_EFFECT_LIST *list);

extern SA1_EFFECT_PARAM D_8C4BB178;     /* smoke parameters */
extern SA1_EFFECT_DEF D_8C4BB1B4;       /* smoke sprite definition */
extern SA1_EFFECT_LIST *D_8C4BB1C0;     /* E-102 smoke effect list */

SA1_EFFECT *func_8C09DDFC(SA1_EFFECT_LIST *list, SA1_EFFECT_EXEC exec);
void func_8C09DE34(SA1_EFFECT *ep);
SA1_EFFECT_LIST *func_8C09DE3A(SA1_EFFECT_DESC *desc, Uint32 extra);

void func_8C0D67C0(SA1_EFFECT *ep, SA1_EFFECT_LIST *list)
{
    SA1_EFFECT_PARAM *param;
    Float drag;

    param = ep->param;
    ep->cell = (Sint16)SA1_FTOI(ep->frame);
    if (ep->cell >= (Sint16)list->desc->ncell) {
        ep->cell = list->desc->ncell - 1;
        func_8C09DE34(ep);
        return;
    }
    ep->frame += param->dframe;
    drag = param->drag;
    ep->spd.x += -(ep->spd.x * drag);
    ep->spd.y += -(ep->spd.y * drag) + param->rise;
    ep->spd.z += -(ep->spd.z * drag);
    ep->pos.x += ep->spd.x;
    ep->pos.y += ep->spd.y;
    ep->pos.z += ep->spd.z;
    ep->scl += param->dscl;
}

void func_8C0D6870(NJS_POINT3 *pos, NJS_POINT3 *vec, Float scl)
{
    SA1_EFFECT *ep;

    ep = func_8C09DDFC(D_8C4BB1C0, func_8C0D67C0);
    if (ep != NULL) {
        ep->pos = *pos;
        ep->spd = *vec;
        ep->scl = scl;
        ep->frame = (Float)rand() * 1.220703125e-04f;
        ep->ang = 0x2000 - SA1_FTOI((Float)rand() * 0.125f);
        ep->argb.a = 1.0f;
        ep->argb.r = 1.0f;
        ep->argb.g = 1.0f;
        ep->argb.b = 1.0f;
        ep->param = &D_8C4BB178;
    }
}

void func_8C0D68FC(void)
{
    D_8C4BB1C0 = func_8C09DE3A(D_8C4BB1B4.desc, 0);
}
