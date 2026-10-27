/*
 * 1ST_READ 8C09E520 (chunk 8C09E520-8C09E6C0): generic smoke/dust particles
 * of the effect list D_8C1C43F8 (parameter block D_8C1C43B0).
 *
 *  func_8C09E520  exec of a particle: animation cell from the frame counter;
 *                 after cell 7 the particle is released (func_8C09DE34);
 *                 otherwise frame, velocity (damping + rise), position and
 *                 scale advance by the particle's parameter block
 *  func_8C09E59A  spawn one particle at the origin with scale scl, random
 *                 start frame and angle, white opaque colour, parameters
 *                 D_8C1C43B0 (called from the stg04 overlay)
 *  func_8C09E612  spawn one particle from a parameter block (position,
 *                 velocity, scale, colour), random angle (stg10 overlay)
 */
#include "common.h"
#include "sa1/sa1.h"

/* Effect spawn/animation parameters (15 floats; same shape as
 * SA1_EFFECT_PARAM in code_8C0D67C0.c / code_8C0465E0.c).
 * Local view until a shared type exists. */
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
 * list, links it into the list and sets exec; func_8C09DE34 releases it).
 * Local view (same layout as code_8C0D67C0.c) until a shared type exists. */
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
    Float f40;                  /* DC 0x40 */
    SA1_EFFECT_PARAM *param;    /* DC 0x44 */
} SA1_EFFECT;                   /* DC 0x48 */

typedef struct sa1_effect_list SA1_EFFECT_LIST;   /* opaque here */

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT_PARAM, 0x3C);
SA1_ASSERT_OFFSET(SA1_EFFECT_PARAM, argb, 0x2C);
SA1_ASSERT_SIZE(SA1_EFFECT, 0x48);
SA1_ASSERT_OFFSET(SA1_EFFECT, ang, 0x0C);
SA1_ASSERT_OFFSET(SA1_EFFECT, pos, 0x18);
SA1_ASSERT_OFFSET(SA1_EFFECT, argb, 0x30);
SA1_ASSERT_OFFSET(SA1_EFFECT, param, 0x44);
#endif

typedef void (*SA1_EFFECT_EXEC)(SA1_EFFECT *ep, SA1_EFFECT_LIST *list);

extern SA1_EFFECT_LIST *D_8C1C43F8;     /* effect list */
extern SA1_EFFECT_PARAM D_8C1C43B0;     /* default parameters */

SA1_EFFECT *func_8C09DDFC(SA1_EFFECT_LIST *list, SA1_EFFECT_EXEC exec);
void func_8C09DE34(SA1_EFFECT *ep);

void func_8C09E520(SA1_EFFECT *ep, SA1_EFFECT_LIST *list)
{
    SA1_EFFECT_PARAM *param;
    Float *p;
    Float x;
    Float y;
    Float z;
    Float px;
    Float py;
    Float pz;

    param = ep->param;
    ep->cell = (Sint16)SA1_FTOI(ep->frame);
    if (ep->cell >= 8) {
        ep->cell = 7;
        func_8C09DE34(ep);
        return;
    }
    ep->frame += param->dframe;
    /* spd *= 1 - drag; spd.y += rise (the original reads and writes the
     * vectors as float triples, like a vector macro: post-increment loads,
     * pre-decrement stores, the factor expanded per component) */
    p = &ep->spd.x;
    x = *p++;
    y = *p++;
    z = *p++;
    x *= -param->drag + 1.0f;
    y *= -param->drag + 1.0f;
    z *= -param->drag + 1.0f;
    y += param->rise;
    *--p = z;
    *--p = y;
    *--p = x;
    /* pos += spd */
    p = &ep->pos.x;
    px = *p++;
    py = *p++;
    pz = *p++;
    px += x;
    py += y;
    pz += z;
    *--p = pz;
    *--p = py;
    *--p = px;
    ep->scl += param->dscl;
}

void func_8C09E59A(Float scl)
{
    SA1_EFFECT *ep;
    SA1_EFFECT_LIST *list;
    Float *p;
    Float x;
    Float y;
    Float z;

    list = D_8C1C43F8;
    ep = func_8C09DDFC(list, func_8C09E520);
    if (ep != NULL) {
        /* pos = 0 (pre-decrement stores, as in func_8C09E520) */
        x = y = z = 0.0f;
        p = &ep->pos.x + 3;
        *--p = z;
        *--p = y;
        *--p = x;
        ep->scl = scl;
        ep->frame = (Float)rand() * 1.220703125e-04f;
        ep->ang = 0x2000 - SA1_FTOI((Float)rand() * 0.125f);
        ep->argb.a = 1.0f;
        ep->argb.r = 1.0f;
        ep->argb.g = 1.0f;
        ep->argb.b = 1.0f;
        ep->param = &D_8C1C43B0;
    }
}

void func_8C09E612(SA1_EFFECT_PARAM *param)
{
    SA1_EFFECT *ep;
    SA1_EFFECT_LIST *list;

    list = D_8C1C43F8;
    ep = func_8C09DDFC(list, func_8C09E520);
    if (ep != NULL) {
        ep->pos = param->pos;
        ep->spd = param->spd;
        ep->scl = param->scl;
        ep->frame = 0.0f;
        ep->ang = 0x2000 - SA1_FTOI((Float)rand() * 0.125f);
        ep->argb = param->argb;
        ep->param = param;
    }
}
