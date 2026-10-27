/*
 * 1ST_READ 8C046A08 (chunk 8C046960-8C046F48, function 2/3).
 *
 *  func_8C046A08  spawn one particle (pos, velocity, scale) in the effect
 *                 list D_8C19C078, animated by func_8C046960 with the
 *                 parameter block D_8C19C07C (caller: 8C07B3DA)
 */
#include "common.h"
#include "sa1/sa1.h"

/* Effect spawn/animation parameters (15 floats; same shape as
 * D_8C4BB178 in code_8C0D67C0.c). D_8C19C07C = 1.0, 0.038, 0.19, 0.115,
 * 0.025, 0 x6, 1.0 x4 (argb at D_8C19C0A8). */
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
 * B_8C7A0D4C). Same layout as code_8C0D67C0.c. */
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
SA1_ASSERT_OFFSET(SA1_EFFECT, argb, 0x30);
SA1_ASSERT_OFFSET(SA1_EFFECT, param, 0x44);
SA1_ASSERT_OFFSET(SA1_EFFECT_PARAM, argb, 0x2C);
#endif

typedef void (*SA1_EFFECT_EXEC)(SA1_EFFECT *ep, SA1_EFFECT_LIST *list);

extern SA1_EFFECT_LIST *D_8C19C078;     /* effect list */
/* parameters; its argb (0x2C) carries the separate data label D_8C19C0A8 */
extern SA1_EFFECT_PARAM D_8C19C07C;

SA1_EFFECT *func_8C09DDFC(SA1_EFFECT_LIST *list, SA1_EFFECT_EXEC exec);
void func_8C046960(SA1_EFFECT *ep, SA1_EFFECT_LIST *list);

void func_8C046A08(NJS_POINT3 *pos, NJS_POINT3 *spd, Float scl)
{
    SA1_EFFECT *ep;

    ep = func_8C09DDFC(D_8C19C078, func_8C046960);
    if (ep != NULL) {
        ep->pos = *pos;
        ep->spd = *spd;
        ep->scl = scl * 0.16999999f;
        ep->frame = (Float)rand() * 1.52587890625e-04f;
        ep->ang = SA1_FTOI(SA1_FMAC((Float)rand(), 0.0048828125f, 100.0f) * 182.04443359375f);
        ep->argb = D_8C19C07C.argb;
        ep->param = &D_8C19C07C;
    }
}
