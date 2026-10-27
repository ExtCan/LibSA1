#include "common.h"
#include "ninja.h"

/*
 * 8C02CEF4-8C02CF20: hand-written SH-4 assembly (not SHC output).
 *
 * Part of the land collision list module 8C02CE80-8C02D54C (free-list and
 * list helpers 8C02CE80, 8C02CEA0, 8C02CECA, 8C02CEF4, 8C02CF20, 8C02CF48,
 * see src/1st_read/code_8C02CE80.c and code_8C028E60.c):
 * - zero-displacement accesses are encoded MOV.L @(0,R0),R3 (0x5300) and
 *   MOV.L R2,@(0,R0) (0x1020); SHC always emits MOV.L @Rm,Rn / MOV.L Rm,@Rn.
 * - the in-use list head's address is stored negated (NEG R4,R4) in the
 *   first entry's prev link as a list-head marker; func_8C02CF20 tells it
 *   from an entry pointer with CMP/PZ (entries are 0x8C... addresses,
 *   negative as Sint32) and then stores through the negated value.
 * - R0/R1 used as scratch, MOV.L @(0,R0) right after loading R0 from the
 *   pool: hand-scheduled.
 * The C below is the PC implementation; the original assembly stays in the
 * matching build.
 *
 * Takes an object entry from the free list D_8C02D544 and pushes it on
 * the doubly linked in-use list D_8C02D548. Returns NULL when the free
 * head has no successor: the last free entry is never handed out (with the
 * 192-entry buffer of func_8C02CECA at most 191 objects are in use).
 * Caller: func_8C028EC2 (does not check for NULL).
 */

/* collision object cache entry (prefix of colobj, code_8C028E60.c) */
typedef struct colobj {
    struct colobj *next;        /* 0x00 free list D_8C02D544 / in-use list D_8C02D548 */
    void *prev;                 /* 0x04 previous in-use entry, or the list-head marker */
    NJS_OBJECT *object;         /* 0x08 NULL: entry free */
} colobj;

SA1_ASSERT_OFFSET(colobj, prev, 0x04);

extern colobj *D_8C02D544;      /* free objects (head); data word in the code section */
extern colobj *D_8C02D548;      /* objects in use (head); data word in the code section */

/*
 * prev link of the first in-use entry: the negated address of the list head
 * on every target, as the original stores it (NEG R4,R4; MOV.L R4,@(4,R0)).
 * The DC func_8C02CF20 tests prev >= 0 as Sint32 and stores the next link
 * through -prev; libsa1 must keep the same value so that the in-use list
 * stays exchangeable with the original code (harness routing). A PC
 * func_8C02CF20 recognises it portably by comparing with
 * COLOBJ_HEAD_LINK(&D_8C02D548) (on 64-bit hosts the sign test of the DC
 * code is reversed: user-space pointers are positive there) and recovers the
 * head as (colobj **)(0 - (SA1_UPTR)prev).
 */
#define COLOBJ_HEAD_LINK(head) ((void *)(0 - (SA1_UPTR)(head)))

#ifdef NON_MATCHING
colobj *func_8C02CEF4(void)
{
    colobj *co;
    colobj *next;
    colobj *head;

    co = D_8C02D544;
    next = co->next;
    if (next == NULL) {
        return NULL;
    }
    D_8C02D544 = next;
    head = D_8C02D548;
    co->next = head;
    if (head != NULL) {
        head->prev = co;
    }
    D_8C02D548 = co;
    co->prev = COLOBJ_HEAD_LINK(&D_8C02D548);
    return co;
}
#endif
