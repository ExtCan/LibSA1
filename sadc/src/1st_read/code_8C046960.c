/*
 * 1ST_READ 8C046960 (chunk 8C046960-8C046F48, functions 1/3 and 3/3; the
 * spawner func_8C046A08, function 2/3, is in code_8C046A08.c).
 *
 *  func_8C046960  exec of a water splash particle of the effect list
 *                 D_8C19C078 (spawned by func_8C046A08 and func_8C046A9C):
 *                 animation cell from the frame counter; past cell 7 the
 *                 particle is released (func_8C09DE34; the list's cell count
 *                 is not read); otherwise the frame advances
 *                 1.4 * param->dframe below cell 6 and 2.8 * after, the
 *                 velocity is scaled by 1 - param->drag with param->rise
 *                 added to spd.y, the position moves by the new velocity and
 *                 the scale grows 0.255 * param->dscl
 *  func_8C046A9C  spawn one splash particle from a full parameter block
 *                 (position, velocity, scale and colour) with a random start
 *                 frame and angle; the block stays the particle's parameter
 *                 block. Caller: B_CHAOS7 0C91D200 (Super Sonic's aura on the
 *                 water plane), which fills D_0CA6B418 and tail-calls it
 *
 * libsa1 carried a hand-written copy of func_8C046960 in
 * LIB/src/rt/rt_effect.c; that file must go when this one lands (pclower
 * would see two definitions).
 */
#include "common.h"
#include "sa1/sa1.h"

/* Effect spawn/animation parameters (15 floats; D_8C19C07C, D_0CA6B418,
 * same shape as D_8C4BB178 in code_8C0D67C0.c) */
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

/* Effect list (CAlloc(1, 0x1C + extra) by func_8C09DE3A); opaque here */
typedef struct sa1_effect_list {
    struct sa1_effect_list *next; /* DC 0x00 list of lists B_8C7A0D20 */
    SA1_EFFECT *head;           /* DC 0x04 */
    Sint32 w08;                 /* DC 0x08 */
    Sint32 count;               /* DC 0x0C */
    void *desc;                 /* DC 0x10 sprite descriptor */
} SA1_EFFECT_LIST;

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT_PARAM, 0x3C);
SA1_ASSERT_SIZE(SA1_EFFECT, 0x48);
SA1_ASSERT_OFFSET(SA1_EFFECT, cell, 0x0A);
SA1_ASSERT_OFFSET(SA1_EFFECT, ang, 0x0C);
SA1_ASSERT_OFFSET(SA1_EFFECT, pos, 0x18);
SA1_ASSERT_OFFSET(SA1_EFFECT, spd, 0x24);
SA1_ASSERT_OFFSET(SA1_EFFECT, argb, 0x30);
SA1_ASSERT_OFFSET(SA1_EFFECT, param, 0x44);
SA1_ASSERT_OFFSET(SA1_EFFECT_PARAM, pos, 0x14);
SA1_ASSERT_OFFSET(SA1_EFFECT_PARAM, argb, 0x2C);
#endif

typedef void (*SA1_EFFECT_EXEC)(SA1_EFFECT *ep, SA1_EFFECT_LIST *list);

extern SA1_EFFECT_LIST *D_8C19C078;     /* water splash effect list */

SA1_EFFECT *func_8C09DDFC(SA1_EFFECT_LIST *list, SA1_EFFECT_EXEC exec);
void func_8C09DE34(SA1_EFFECT *ep);

void func_8C046960(SA1_EFFECT *ep, SA1_EFFECT_LIST *list)
{
    SA1_EFFECT_PARAM *param;
    Float k;

    (void)list;
    param = ep->param;
    ep->cell = (Sint16)SA1_FTOI(ep->frame);
    if (ep->cell > 7) {
        ep->cell = 7;
        func_8C09DE34(ep);
        return;
    }
    /* one FMAC in the asm: SHC merges the two branch tails and keeps only
     * the constant load per branch (fmac_audit counts 4 PC FMAs against 3
     * SH-4 FMACs here; every path still runs exactly one fused FMAC) */
    if (ep->frame < 6.0f) {
        SA1_FMAC_ACC(ep->frame, 1.4f, param->dframe);
    } else {
        SA1_FMAC_ACC(ep->frame, 2.8f, param->dframe);
    }
    /* written as plain assignments: "*=" / "+=" let SHC keep &ep->spd in
     * R1 and load in another order */
    k = -param->drag + 1.0f;
    ep->spd.x = ep->spd.x * k;
    ep->spd.y = SA1_FMAC(ep->spd.y, k, param->rise);
    ep->spd.z = ep->spd.z * k;
    ep->pos.x = ep->pos.x + ep->spd.x;
    ep->pos.y = ep->pos.y + ep->spd.y;
    ep->pos.z = ep->pos.z + ep->spd.z;
    SA1_FMAC_ACC(ep->scl, 0.25499997f, param->dscl);
}

void func_8C046A9C(SA1_EFFECT_PARAM *param)
{
    SA1_EFFECT *ep;
    SA1_EFFECT_LIST *list;

    list = D_8C19C078;
    ep = func_8C09DDFC(list, func_8C046960);
    if (ep != NULL) {
        ep->pos = param->pos;
        ep->spd = param->spd;
        ep->scl = param->scl;
        ep->frame = (Float)rand() * 1.52587890625e-04f;
        ep->ang = SA1_FTOI(SA1_FMAC((Float)rand(), 0.0048828125f, 100.0f) * 182.04443359375f);
        ep->argb = param->argb;
        ep->param = param;
    }
}
