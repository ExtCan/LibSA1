/*
 * 1ST_READ 8C09E340 (chunk 8C09E340-8C09E520): generic smoke/spark particles
 * of the effect list D_8C1C42F0 (same scheme as the E-102 smoke of
 * code_8C0D67C0.c).
 *
 *  func_8C09E340  exec of a particle: animation cell from the frame counter;
 *                 at cell 8 the particle is released (func_8C09DE34);
 *                 otherwise frame, velocity (drag + rise), position and scale
 *                 advance by the particle's parameter block
 *  func_8C09E3E8  spawn one particle (pos, velocity, scale) with the default
 *                 parameters D_8C1C42A8 and white colour (break/spark effects,
 *                 8C05D6AC, 8C05DF18, 8C1032AC)
 *  func_8C09E474  spawn one particle from a full parameter block (position,
 *                 velocity, scale and colour; board spray/sand, 8C07B694)
 */
#include "common.h"
#include "sa1/sa1.h"

/* Effect spawn/animation parameters (15 floats; D_8C1C42A8, D_8C1BE858,
 * D_8C1BE894, D_8C4BB178) */
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
    struct sa1_effect_list *next; /* DC 0x00 */
    SA1_EFFECT *head;           /* DC 0x04 */
    Sint32 w08;                 /* DC 0x08 */
    Sint32 count;               /* DC 0x0C */
    void *desc;                 /* DC 0x10 */
} SA1_EFFECT_LIST;

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT_PARAM, 0x3C);
SA1_ASSERT_SIZE(SA1_EFFECT, 0x48);
SA1_ASSERT_OFFSET(SA1_EFFECT, spd, 0x24);
SA1_ASSERT_OFFSET(SA1_EFFECT, param, 0x44);
#endif

typedef void (*SA1_EFFECT_EXEC)(SA1_EFFECT *ep, SA1_EFFECT_LIST *list);

extern SA1_EFFECT_LIST *D_8C1C42F0;     /* effect list */
extern SA1_EFFECT_PARAM D_8C1C42A8;     /* default parameters */

SA1_EFFECT *func_8C09DDFC(SA1_EFFECT_LIST *list, SA1_EFFECT_EXEC exec);
void func_8C09DE34(SA1_EFFECT *ep);

void func_8C09E340(SA1_EFFECT *ep, SA1_EFFECT_LIST *list)
{
    SA1_EFFECT_PARAM *param;
    Float drag;

    (void)list;
    param = ep->param;
    ep->cell = (Sint16)SA1_FTOI(ep->frame);
    if (ep->cell >= 8) {
        ep->cell = 7;
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

void func_8C09E3E8(NJS_POINT3 *pos, NJS_POINT3 *vec, Float scl)
{
    SA1_EFFECT *ep;

    ep = func_8C09DDFC(D_8C1C42F0, func_8C09E340);
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
        ep->param = &D_8C1C42A8;
    }
}

void func_8C09E474(SA1_EFFECT_PARAM *param)
{
    SA1_EFFECT *ep;
    SA1_EFFECT_LIST *list;

    /* the list is read into a local first (loads func_8C09E340 before
     * D_8C1C42F0, unlike func_8C09E3E8) */
    list = D_8C1C42F0;
    ep = func_8C09DDFC(list, func_8C09E340);
    if (ep != NULL) {
        ep->pos = param->pos;
        ep->spd = param->spd;
        ep->scl = param->scl;
        ep->frame = (Float)rand() * 1.220703125e-04f;
        ep->ang = 0x2000 - SA1_FTOI((Float)rand() * 0.125f);
        ep->argb = param->argb;
        ep->param = param;
    }
}
