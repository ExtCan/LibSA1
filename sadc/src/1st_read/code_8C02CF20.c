#include "common.h"
#include "ninja.h"

/*
 * 8C02CF20-8C02CF68: hand-written SH-4 assembly (not SHC output).
 *
 * Part of the land collision list module 8C02CE80-8C02D54C (free-list and
 * list helpers 8C02CE80, 8C02CEA0, 8C02CECA, 8C02CEF4, 8C02CF20, 8C02CF48,
 * see src/1st_read/code_8C02CEF4.c and code_8C028E60.c):
 * - zero-displacement accesses are encoded MOV.L @(0,R4),R2 (0x5240),
 *   MOV.L R2,@(0,R3) (0x1320), MOV.L R0,@(0,R4) (0x1400) and
 *   MOV.L R0,@(0,R3) (0x1300); SHC always emits MOV.L @Rm,Rn (0x6nm2) and
 *   MOV.L Rm,@Rn (0x2nm2).
 * - func_8C02CF20 keeps the NOP in the RTS delay slot although the last
 *   store could fill it, and both routines use R0/R1 as scratch registers
 *   (MOV.L @R1,R0 right after loading R1 from the pool).
 * The C below is the same algorithm; the original assembly stays in the
 * build (bodies only under NON_MATCHING, used by libsa1).
 *
 * func_8C02CF20: unlinks an object entry from the doubly linked in-use list
 *   D_8C02D548 and pushes it on the free list D_8C02D544. The prev link of
 *   the first in-use entry is the negated address of the list head
 *   (func_8C02CEF4); entries are 0x8C... addresses, negative as Sint32, so
 *   CMP/PZ tells the head marker (>= 0) from an entry pointer and the next
 *   link is then stored through -prev. The entry's own prev is left stale.
 * func_8C02CF48: frees an object entry: gives its polygon chain
 *   (co->polys .. co->last, only when both are set) back to the polygon free
 *   list D_8C02D540, clears co->object, then tail-calls (BRA)
 *   func_8C02CF20. co->polys/co->last are not cleared.
 *   Caller: func_8C028EC2 (code_8C028E60.c).
 */

/* polygon buffer entry ("RANGE-poly", B_8C6BE27C); same layout as in
 * code_8C028E60.c (should move to sa1/collision.h) */
typedef struct colpoly {
    struct colpoly *next;       /* 0x00 free list / per-object list */
    struct colpoly *prev;       /* 0x04 previous polygon of the object (NULL: first) */
    Sint32 unk08;               /* 0x08 */
    Sint32 unk0C;               /* 0x0C */
    Sint32 unk10;               /* 0x10 */
    Float unk14;                /* 0x14 */
    Sint32 unk18[10];           /* 0x18 */
} colpoly;                      /* 0x40 */

/* object buffer entry ("OBJECT buf", B_8C6BB27C); same layout as in
 * code_8C028E60.c (should move to sa1/collision.h) */
typedef struct colobj {
    struct colobj *next;        /* 0x00 free list D_8C02D544 / in-use list D_8C02D548 */
    void *prev;                 /* 0x04 previous in-use entry, or the list-head marker */
    NJS_OBJECT *object;         /* 0x08 NULL: entry free */
    Uint32 attr;                /* 0x0C */
    colpoly *polys;             /* 0x10 first polygon */
    colpoly *last;              /* 0x14 last polygon */
    Sint32 unk18;               /* 0x18 */
    void *unk1C;                /* 0x1C */
    Sint32 unk20;               /* 0x20 */
    Sint32 unk24;               /* 0x24 */
    Float unk28;                /* 0x28 */
    Float unk2C;                /* 0x2C */
    NJS_POINT3 center;          /* 0x30 */
    Sint32 unk3C;               /* 0x3C */
} colobj;                       /* 0x40 */

SA1_ASSERT_SIZE(colpoly, 0x40);
SA1_ASSERT_SIZE(colobj, 0x40);
SA1_ASSERT_OFFSET(colobj, prev, 0x04);
SA1_ASSERT_OFFSET(colobj, object, 0x08);
SA1_ASSERT_OFFSET(colobj, polys, 0x10);
SA1_ASSERT_OFFSET(colobj, last, 0x14);

extern colpoly *D_8C02D540;     /* free polygons (head); data word in the code section */
extern colobj *D_8C02D544;      /* free objects (head); data word in the code section */
extern colobj *D_8C02D548;      /* objects in use (head); data word in the code section */

/*
 * prev link of the first in-use entry (set by func_8C02CEF4): the negated
 * address of the list head on every target (COLOBJ_HEAD_LINK in
 * code_8C02CEF4.c). DC: recognised as Sint32 >= 0 (entries are 0x8C...
 * addresses), the next link is stored through -prev.
 * PC: the sign test only works with 0x8C... addresses (on 64-bit hosts
 * user-space pointers are positive), so the head link is recognised by
 * value; the head is then D_8C02D548. In the harness guest addresses are
 * host addresses, so the value is exactly the one the original stores.
 */
#if defined(TARGET_PC)
#define COLOBJ_DC_HEAD_LINK         ((void *)(0 - (SA1_UPTR)&D_8C02D548))
#define COLOBJ_IS_HEAD_LINK(link)   ((link) == COLOBJ_DC_HEAD_LINK)
#define COLOBJ_HEAD_OF_LINK(link)   (&D_8C02D548)
#else
#define COLOBJ_IS_HEAD_LINK(link)   ((Sint32)(SA1_UPTR)(link) >= 0)
#define COLOBJ_HEAD_OF_LINK(link)   ((colobj **)(0 - (SA1_UPTR)(link)))
#endif

#ifdef NON_MATCHING
void func_8C02CF20(colobj *co)
{
    colobj *next;
    colobj *freehead;
    void *prev;

    next = co->next;
    prev = co->prev;
    if (next != NULL) {
        next->prev = prev;
    }
    if (COLOBJ_IS_HEAD_LINK(prev)) {
        *COLOBJ_HEAD_OF_LINK(prev) = next;
    } else if (prev != NULL) {
        ((colobj *)prev)->next = next;
    }
    /* prev == NULL (never stored by func_8C02CEF4) only reaches here on
     * PC: the DC code takes it for a head link (0 >= 0) and stores through
     * -0, i.e. to address 0 (boot ROM, no effect), so nothing is stored */
    /* store order of the original: free head first, then co->next */
    freehead = D_8C02D544;
    D_8C02D544 = co;
    co->next = freehead;
}
#endif

#ifdef NON_MATCHING
void func_8C02CF48(colobj *co)
{
    colpoly *first;
    colpoly *last;
    colpoly *freehead;

    first = co->polys;
    last = co->last;
    if (first != NULL && last != NULL) {
        /* store order of the original: free head first, then last->next */
        freehead = D_8C02D540;
        D_8C02D540 = first;
        last->next = freehead;
    }
    co->object = NULL;
    func_8C02CF20(co);
}
#endif
