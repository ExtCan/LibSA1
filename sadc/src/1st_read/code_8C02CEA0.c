#include "common.h"
#include "ninja.h"

/*
 * 8C02CEA0-8C02CEF4: hand-written SH-4 assembly (not SHC output).
 *
 * Part of the land collision list module 8C02CE80-8C02D54C (free-list and
 * list helpers, see src/1st_read/code_8C02CE80.c and code_8C028E60.c):
 * - zero-displacement accesses are encoded MOV.L @(0,Rn),Rm (0x5mn0) and
 *   MOV.L Rm,@(0,Rn) (0x1nm0); SHC always emits MOV.L @Rn,Rm (0x6mn2) and
 *   MOV.L Rm,@Rn (0x2nm2).
 * - func_8C02CECA keeps the NOP in the RTS delay slot after storing
 *   D_8C02D548 (unscheduled), and both use R0/R1 as scratch registers.
 * The C below is the same algorithm; the original assembly stays in the
 * build (bodies only under NON_MATCHING, used by libsa1).
 *
 * func_8C02CEA0: takes a polygon from the free list D_8C02D540 and pushes
 *   it at the front of an object's polygon list (co->polys/co->last).
 *   The free-list head itself is never handed out when it is the last
 *   node (the test is on head->next), so one node always stays free.
 * func_8C02CECA: builds the free list of the object buffer (B_8C6BB27C,
 *   64-byte entries; called from 8C0285D2 and 8C028E60) into D_8C02D544
 *   and clears the in-use list D_8C02D548. The DT loop runs n-1 times;
 *   n <= 1 is not guarded (the counter wraps).
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
    void *prev;                 /* 0x04 previous entry, or the negated address of the list head */
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
SA1_ASSERT_OFFSET(colobj, polys, 0x10);
SA1_ASSERT_OFFSET(colobj, last, 0x14);

extern colpoly *D_8C02D540;     /* free polygons (head); data word in the code section */
extern colobj *D_8C02D544;      /* free objects (head); data word in the code section */
extern colobj *D_8C02D548;      /* objects in use (head); data word in the code section */

#ifdef NON_MATCHING
colpoly *func_8C02CEA0(colobj *co)
{
    colpoly *poly;
    colpoly *next;
    colpoly *first;

    poly = D_8C02D540;
    next = poly->next;
    if (next == NULL) {
        return NULL;
    }
    D_8C02D540 = next;
    first = co->polys;
    poly->next = first;
    if (first != NULL) {
        first->prev = poly;
    } else {
        co->last = poly;
    }
    co->polys = poly;
    poly->prev = NULL;
    return poly;
}
#endif

#ifdef NON_MATCHING
void func_8C02CECA(colobj *buf, Sint32 n)
{
    colobj *next;

    D_8C02D544 = buf;
    next = buf + 1;
    n--;
    do {
        buf->next = next;
        buf++;
        next++;
    } while (--n != 0);
    buf->next = NULL;
    D_8C02D548 = NULL;
}
#endif
