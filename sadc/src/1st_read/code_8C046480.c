/*
 * 1ST_READ 8C046480 (chunk 8C046480-8C0465E0): dust particle execs.
 *
 *  func_8C046480  exec of a dust puff behind a running player (spawned by
 *                 8C07B500 and 8C0484CC into the dust effect list
 *                 D_8C19BF48): animation cell from the frame counter; past
 *                 the sprite descriptor's last cell the particle is released
 *                 (func_8C09DE34); otherwise the frame advances 0.19 per step
 *                 below cell 6 and 0.57 after, the scale grows 0.0057 and the
 *                 puff drifts up 0.3 and along z 0.1 (no velocity)
 *  func_8C0464EE  exec of a dust particle (spawned by func_8C0465E0,
 *                 8C04666A, 8C0466E8 and 8C064636 with the parameter block
 *                 D_8C19BF5C): same end test; frame + param->dframe below
 *                 cell 6, twice that after; velocity drag + rise, position
 *                 and 0.15 * param->dscl scale step (same scheme as the E-102
 *                 smoke func_8C0D67C0 in code_8C0D67C0.c)
 *
 * libsa1 carried a hand-written copy of both in LIB/src/rt/rt_effect.c; that
 * file must go when this one lands (pclower would see two definitions).
 */
#include "common.h"
#include "sa1/sa1.h"

/* Sprite descriptor of an effect list (same layout as code_8C0D67C0.c) */
typedef struct {
    NJS_TEXLIST *texlist;       /* DC 0x00 */
    void *cells;                /* DC 0x04 animation cell table */
    Sint32 ncell;               /* DC 0x08 number of cells (the execs read
                                 * its low half) */
    Sint32 w0C;                 /* DC 0x0C */
    Sint32 w10;                 /* DC 0x10 */
} SA1_EFFECT_DESC;              /* DC 0x14 */

/* Effect spawn/animation parameters (15 floats; D_8C19BF5C, same shape as
 * D_8C4BB178 in code_8C0D67C0.c) */
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

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT_PARAM, 0x3C);
SA1_ASSERT_SIZE(SA1_EFFECT, 0x48);
SA1_ASSERT_OFFSET(SA1_EFFECT, cell, 0x0A);
SA1_ASSERT_OFFSET(SA1_EFFECT, frame, 0x10);
SA1_ASSERT_OFFSET(SA1_EFFECT, spd, 0x24);
SA1_ASSERT_OFFSET(SA1_EFFECT, param, 0x44);
SA1_ASSERT_OFFSET(SA1_EFFECT_LIST, desc, 0x10);
SA1_ASSERT_OFFSET(SA1_EFFECT_DESC, ncell, 0x08);
SA1_ASSERT_OFFSET(SA1_EFFECT_PARAM, drag, 0x0C);
#endif

void func_8C09DE34(SA1_EFFECT *ep);

void func_8C046480(SA1_EFFECT *ep, SA1_EFFECT_LIST *list)
{
    ep->cell = (Sint16)SA1_FTOI(ep->frame);
    if (ep->cell >= (Sint16)list->desc->ncell) {
        ep->cell = (Sint16)(list->desc->ncell - 1);
        func_8C09DE34(ep);
        return;
    }
    if (ep->frame < 6.0f) {
        ep->frame += 0.19f;
    } else {
        ep->frame += 0.57f;
    }
    ep->scl += 0.0056999992f;
    ep->pos.y += 0.29999999f;
    ep->pos.z += 0.099999995f;
}

void func_8C0464EE(SA1_EFFECT *ep, SA1_EFFECT_LIST *list)
{
    SA1_EFFECT_PARAM *param;
    Float drag;

    param = ep->param;
    ep->cell = (Sint16)SA1_FTOI(ep->frame);
    if (ep->cell >= (Sint16)list->desc->ncell) {
        ep->cell = (Sint16)(list->desc->ncell - 1);
        func_8C09DE34(ep);
        return;
    }
    if (ep->frame < 6.0f) {
        ep->frame += param->dframe;
    } else {
        SA1_FMAC_ACC(ep->frame, 2.0f, param->dframe);
    }
    drag = param->drag;
    ep->spd.x += -(ep->spd.x * drag);
    ep->spd.y += -(ep->spd.y * drag) + param->rise;
    ep->spd.z += -(ep->spd.z * drag);
    ep->pos.x += ep->spd.x;
    ep->pos.y += ep->spd.y;
    ep->pos.z += ep->spd.z;
    SA1_FMAC_ACC(ep->scl, 0.149999992f, param->dscl);
}
