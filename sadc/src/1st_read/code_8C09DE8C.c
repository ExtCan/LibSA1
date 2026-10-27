/*
 * 1ST_READ 8C09DE8C (chunk 8C09DDA0-8C09E2A0): effect (particle) lists,
 * the "SPR TASK" system.
 *
 *  func_8C09DE8C  run a list's own exec unless it is paused (1) or
 *                 released (2); 1 when it ran
 *  func_8C09DEF4  mark a list released (freed by the update once empty)
 *  func_8C09DF06  release a list and give all its particles back to the
 *                 free pool at once
 *  func_8C09DF4A  pause a list
 *  func_8C09DF52  resume a list (unless released)
 *  func_8C09DF64  per-frame update: free released empty lists, run every
 *                 particle's exec, recycle particles whose exec was cleared,
 *                 draw (func_8C09E0B8) and the debug timing display
 *
 * See code_8C09DDFC.c for allocation (func_8C09DDFC, func_8C09DE3A).
 *
 * Matching notes: func_8C09DE8C needs the two flag tests as separate ifs
 * (with "||" SHC loads the flag once). func_8C09DF64's list loop gets the
 * original shape (entry BRA to the test, separate "prev = list" blocks
 * before the particle loop, the released-but-not-empty one jumping over
 * the literal pool) only with "continue" after MFree and "prev = list" in
 * an explicit else of the head test as well as in the outer else; a single
 * "prev = list" after the if (or "&&" in the test) merges the two blocks
 * and changes the register allocation.
 */
#include "common.h"
#include "sa1/sa1.h"

struct sa1_effect_list;

/* Particle node (partial layout; same view as code_8C09DDFC.c, full 0x48
 * view in code_8C0D67C0.c, pool stride 0x4C in func_8C09DDA0) */
typedef struct sa1_effect {
    struct sa1_effect *next;    /* DC 0x00 */
    void (*exec)(struct sa1_effect *ep, struct sa1_effect_list *list); /* DC 0x04 NULL = free it */
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
    Sint32 flag;                /* DC 0x14 1 = paused, 2 = released */
    void *work;                 /* DC 0x18 extra bytes after the list, or NULL */
} SA1_EFFECT_LIST;              /* DC 0x1C */

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT, 0x48);
SA1_ASSERT_OFFSET(SA1_EFFECT, f40, 0x40);
SA1_ASSERT_SIZE(SA1_EFFECT_LIST, 0x1C);
SA1_ASSERT_OFFSET(SA1_EFFECT_LIST, count, 0x0C);
SA1_ASSERT_OFFSET(SA1_EFFECT_LIST, flag, 0x14);
SA1_ASSERT_OFFSET(SA1_EFFECT_LIST, work, 0x18);
#endif

#define EFFECT_LIST_PAUSED   1
#define EFFECT_LIST_RELEASED 2

extern SA1_EFFECT *B_8C7A0D18;          /* free particle nodes */
extern Sint32 B_8C7A0D1C;               /* number of free nodes */
extern Sint32 B_8C7A0D14;               /* "MAX:" in the debug display */
extern SA1_EFFECT_LIST *B_8C7A0D20;     /* all effect lists */
extern Sint32 B_8C7A0D24;               /* number of effect lists */
extern Sint32 B_8C7A0D28;               /* "NUM:" in the debug display */

extern const char D_8C15B0A4[];         /* "SPR TASK" */
extern const char D_8C15B0B0[];         /* "MAX:" */
extern const char D_8C15B0B8[];         /* "NUM:" */
extern const char D_8C15B0C0[];         /* "EXEC:" */
extern const char D_8C15B0C8[];         /* "DRAW:" */

/* TMU1 stopwatch (code_8C06C0C0.c) */
Float func_8C06C15A(void);              /* time since the last reset; resets */
void func_8C06C182(void);               /* reset */

void func_8C09E0B8(void);               /* draw all lists (BSR, below) */

Sint32 func_8C09DE8C(SA1_EFFECT_LIST *list)
{
    if (list->flag & EFFECT_LIST_RELEASED) {
        return 0;
    }
    if (list->flag & EFFECT_LIST_PAUSED) {
        return 0;
    }
    if (list->exec != NULL) {
        list->exec(list);
    }
    return 1;
}

void func_8C09DEF4(SA1_EFFECT_LIST *list)
{
    if (!(list->flag & EFFECT_LIST_RELEASED)) {
        list->flag |= EFFECT_LIST_RELEASED;
    }
}

void func_8C09DF06(SA1_EFFECT_LIST *list)
{
    SA1_EFFECT *ep;

    if (!(list->flag & EFFECT_LIST_RELEASED)) {
        list->flag |= EFFECT_LIST_RELEASED;
        if (list->head != NULL) {
            for (ep = list->head; ep->next != NULL; ep = ep->next) {
                ep->exec = NULL;
            }
            ep->next = B_8C7A0D18;
            B_8C7A0D18 = list->head;
            B_8C7A0D1C += list->count;
            list->head = NULL;
            list->count = 0;
        }
    }
}

void func_8C09DF4A(SA1_EFFECT_LIST *list)
{
    list->flag |= EFFECT_LIST_PAUSED;
}

void func_8C09DF52(SA1_EFFECT_LIST *list)
{
    if (!(list->flag & EFFECT_LIST_RELEASED)) {
        list->flag &= ~EFFECT_LIST_PAUSED;
    }
}

void func_8C09DF64(void)
{
    SA1_EFFECT_LIST *list;
    SA1_EFFECT_LIST *next;
    SA1_EFFECT_LIST *prev;
    SA1_EFFECT *ep;
    SA1_EFFECT *epnext;
    SA1_EFFECT *epprev;
    Float exec_time;
    Float draw_time;

    func_8C06C182();
    prev = NULL;
    for (list = B_8C7A0D20; list != NULL; list = next) {
        next = list->next;
        if (list->flag & EFFECT_LIST_RELEASED) {
            if (list->head == NULL) {
                /* released and empty: unlink and free the list */
                if (prev == NULL) {
                    B_8C7A0D20 = next;
                } else {
                    prev->next = next;
                }
                B_8C7A0D24--;
                MFree(list);
                continue;
            } else {
                prev = list;
            }
        } else {
            prev = list;
        }
        /* run the particles; recycle those whose exec was cleared */
        epprev = NULL;
        for (ep = list->head; ep != NULL; ep = epnext) {
            epnext = ep->next;
            if (ep->exec != NULL) {
                ep->exec(ep, list);
                epprev = ep;
            } else {
                if (epprev == NULL) {
                    list->head = epnext;
                } else {
                    epprev->next = epnext;
                }
                list->count--;
                ep->next = B_8C7A0D18;
                B_8C7A0D18 = ep;
                B_8C7A0D1C++;
            }
        }
    }
    exec_time = func_8C06C15A();
    func_8C06C182();
    if (B_8C754EC8 == 0) {
        func_8C09E0B8();
    }
    draw_time = func_8C06C15A();
    if (tcrf_SprTaskDisplayMode != 0) {
        njPrintC(0x001E000A, D_8C15B0A4);
        njPrintC(0x001E000B, D_8C15B0B0);
        func_8C640A80(0x0023000B, B_8C7A0D14, 4);
        njPrintC(0x001E000C, D_8C15B0B8);
        func_8C640A80(0x0023000C, B_8C7A0D28, 4);
        njPrintC(0x001E000D, D_8C15B0C0);
        func_8C640A80(0x0023000D, SA1_FTOI(exec_time), 4);
        njPrintC(0x001E000E, D_8C15B0C8);
        func_8C640A80(0x0023000E, SA1_FTOI(draw_time), 4);
    }
}

ASM_STUB(void func_8C09E0B8(void))
