/*
 * 1ST_READ 8C09DDA0 (chunk 8C09DDA0-8C09E2A0): effect (particle) node pool.
 *
 *  func_8C09DDA0  chain an array of n particle nodes into the free list
 *                 B_8C7A0D18 (count B_8C7A0D1C), last next = NULL
 *  func_8C09DDDA  initialise the effect system: remember the pool
 *                 (B_8C7A0D10/B_8C7A0D14), build the free list, no effect
 *                 lists yet (B_8C7A0D20/B_8C7A0D24), B_8C7A0D4C = 0
 *
 * func_8C04E660 calls func_8C09DDDA(B_8C7493D4, 0x200): B_8C7493D4 is
 * 38912 = 512 * 0x4C bytes, so a pool node is 0x4C bytes.
 */
#include "common.h"
#include "sa1/sa1.h"

struct sa1_effect_list;

/* Particle node (same layout as code_8C09DDFC.c / code_8C0D67C0.c, plus the
 * word at 0x48 that makes the pool stride 0x4C) */
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
    Sint32 w48;                 /* DC 0x48 unknown (pool stride only) */
} SA1_EFFECT;                   /* DC 0x4C */

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT, 0x4C);
SA1_ASSERT_OFFSET(SA1_EFFECT, f40, 0x40);
SA1_ASSERT_OFFSET(SA1_EFFECT, w48, 0x48);
#endif

extern SA1_EFFECT *B_8C7A0D10;          /* node pool */
extern Uint32 B_8C7A0D14;               /* number of nodes in the pool */
extern SA1_EFFECT *B_8C7A0D18;          /* free particle nodes */
extern Uint32 B_8C7A0D1C;               /* number of free nodes */
extern struct sa1_effect_list *B_8C7A0D20; /* all effect lists */
extern Sint32 B_8C7A0D24;               /* number of effect lists */
extern Float B_8C7A0D4C;

void func_8C09DDA0(SA1_EFFECT *pool, Uint32 n)
{
    Uint32 i;

    B_8C7A0D18 = pool;
    B_8C7A0D1C = n;
    for (i = 0; i < n - 1; i++) {
        pool[i].next = &pool[i + 1];
    }
    pool[n - 1].next = NULL;
}

void func_8C09DDDA(SA1_EFFECT *pool, Uint32 n)
{
    B_8C7A0D10 = pool;
    B_8C7A0D14 = n;
    func_8C09DDA0(B_8C7A0D10, n);
    B_8C7A0D20 = NULL;
    B_8C7A0D24 = 0;
    B_8C7A0D4C = 0.0f;
}
