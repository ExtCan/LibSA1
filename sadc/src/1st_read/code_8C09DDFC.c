/*
 * 1ST_READ 8C09DDFC (chunk 8C09DDA0-8C09E2A0): effect (particle) lists.
 *
 *  func_8C09DDFC  take a particle node from the free list B_8C7A0D18, push it
 *                 on the head of an effect list, set its exec and
 *                 f40 = B_8C7A0D4C; NULL when the list or the pool is empty
 *  func_8C09DE34  release a particle (clear exec; the list update unlinks it)
 *  func_8C09DE3A  create an effect list: CAlloc(1, 0x1C + extra), link it into
 *                 the list of lists B_8C7A0D20, point work at the extra bytes
 *                 that follow it, remember the sprite descriptor
 *
 * The node pool is set up by func_8C09DDA0 (array of 0x4C-byte nodes chained
 * through next, head B_8C7A0D18, count B_8C7A0D1C).
 */
#include "common.h"
#include "sa1/sa1.h"

struct sa1_effect_list;

/* Particle node (partial layout; full 0x48 view in code_8C0D67C0.c, pool
 * stride 0x4C in func_8C09DDA0) */
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

/* Effect list (CAlloc(1, 0x1C + extra) by func_8C09DE3A) */
typedef struct sa1_effect_list {
    struct sa1_effect_list *next; /* DC 0x00 list of lists B_8C7A0D20 */
    SA1_EFFECT *head;           /* DC 0x04 particles */
    void (*exec)(struct sa1_effect_list *list); /* DC 0x08 called by func_8C09DE8C */
    Sint32 count;               /* DC 0x0C number of particles */
    void *desc;                 /* DC 0x10 sprite descriptor */
    Sint32 flag;                /* DC 0x14 1 = paused, 2 = released (8C09DE8C..8C09DF52) */
    void *work;                 /* DC 0x18 extra bytes after the list, or NULL */
} SA1_EFFECT_LIST;              /* DC 0x1C */

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT, 0x48);
SA1_ASSERT_OFFSET(SA1_EFFECT, f40, 0x40);
SA1_ASSERT_SIZE(SA1_EFFECT_LIST, 0x1C);
SA1_ASSERT_OFFSET(SA1_EFFECT_LIST, count, 0x0C);
SA1_ASSERT_OFFSET(SA1_EFFECT_LIST, work, 0x18);
#endif

typedef void (*SA1_EFFECT_EXEC)(SA1_EFFECT *ep, SA1_EFFECT_LIST *list);

extern SA1_EFFECT *B_8C7A0D18;          /* free particle nodes */
extern Sint32 B_8C7A0D1C;               /* number of free nodes */
extern SA1_EFFECT_LIST *B_8C7A0D20;     /* all effect lists */
extern Sint32 B_8C7A0D24;               /* number of effect lists */
extern Float B_8C7A0D4C;

extern void *CAlloc(Sint32 n, Sint32 size);

SA1_EFFECT *func_8C09DDFC(SA1_EFFECT_LIST *list, SA1_EFFECT_EXEC exec)
{
    SA1_EFFECT *ep;

    if (list == NULL) {
        return NULL;
    }
    ep = B_8C7A0D18;
    if (ep == NULL) {
        return NULL;
    }
    B_8C7A0D18 = B_8C7A0D18->next;
    B_8C7A0D1C--;
    ep->next = list->head;
    list->head = ep;
    list->count++;
    ep->exec = exec;
    ep->f40 = B_8C7A0D4C;
    return ep;
}

void func_8C09DE34(SA1_EFFECT *ep)
{
    ep->exec = NULL;
}

SA1_EFFECT_LIST *func_8C09DE3A(void *desc, Uint32 extra)
{
    SA1_EFFECT_LIST *list;

    if (desc == NULL) {
        return NULL;
    }
    list = (SA1_EFFECT_LIST *)CAlloc(1, (Sint32)(sizeof(SA1_EFFECT_LIST) + extra));
    if (list == NULL) {
        return NULL;
    }
    list->next = B_8C7A0D20;
    B_8C7A0D20 = list;
    B_8C7A0D24++;
    if (extra > 0) {
        list->work = list + 1;
    }
    list->desc = desc;
    return list;
}
