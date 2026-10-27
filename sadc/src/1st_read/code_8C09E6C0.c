/*
 * 1ST_READ 8C09E6C0 (chunk 8C09E6C0-8C09E760): a simple one-shot sprite
 * particle in the effect list D_8C1C44C4.
 *
 *  func_8C09E6C0  exec of the particle: animation cell from the frame
 *                 counter; past cell 7 the particle is released
 *                 (func_8C09DE34), otherwise the frame advances by 1
 *  func_8C09E6E6  spawn one such particle at pos with scale scl * 0.15,
 *                 frame 0, angle 0, white opaque colour
 */
#include "common.h"
#include "sa1/sa1.h"

/* Particle node of the effect lists (func_8C09DDFC takes one from the free
 * list, links it into the list and sets exec). Local view (same layout as
 * code_8C0D67C0.c / code_8C0465E0.c) until a shared type exists. */
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
    void *param;                /* DC 0x44 */
} SA1_EFFECT;                   /* DC 0x48 */

typedef struct sa1_effect_list SA1_EFFECT_LIST;   /* opaque here */

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT, 0x48);
SA1_ASSERT_OFFSET(SA1_EFFECT, cell, 0x0A);
SA1_ASSERT_OFFSET(SA1_EFFECT, ang, 0x0C);
SA1_ASSERT_OFFSET(SA1_EFFECT, frame, 0x10);
SA1_ASSERT_OFFSET(SA1_EFFECT, pos, 0x18);
SA1_ASSERT_OFFSET(SA1_EFFECT, argb, 0x30);
#endif

typedef void (*SA1_EFFECT_EXEC)(SA1_EFFECT *ep, SA1_EFFECT_LIST *list);

extern SA1_EFFECT_LIST *D_8C1C44C4;     /* effect list of this particle */

SA1_EFFECT *func_8C09DDFC(SA1_EFFECT_LIST *list, SA1_EFFECT_EXEC exec);
void func_8C09DE34(SA1_EFFECT *ep);

void func_8C09E6C0(SA1_EFFECT *ep, SA1_EFFECT_LIST *list)
{
    ep->cell = (Sint16)SA1_FTOI(ep->frame);
    if (ep->cell >= 8) {
        ep->cell = 7;
        func_8C09DE34(ep);
        return;
    }
    ep->frame += 1.0f;
}

void func_8C09E6E6(NJS_POINT3 *pos, Float scl)
{
    SA1_EFFECT *ep;
    SA1_EFFECT_LIST *list;

    list = D_8C1C44C4;
    ep = func_8C09DDFC(list, func_8C09E6C0);
    if (ep != NULL) {
        ep->pos = *pos;
        ep->scl = scl * 0.149999992f;
        ep->frame = 0.0f;
        ep->ang = 0;
        ep->argb.a = 1.0f;
        ep->argb.r = 1.0f;
        ep->argb.g = 1.0f;
        ep->argb.b = 1.0f;
    }
}
