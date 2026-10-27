#include "common.h"
#include "task.h"
#include "sa1/engine.h"

/*
 * Task work freeing helpers and the game heap (MAlloc/CAlloc/MFree).
 *
 * The heap is a 0x90000-byte arena (B_8C7EE8E0) carved into blocks that
 * start with a MEMBLK header. Free blocks are kept in a doubly linked free
 * list headed by B_8C79F03C; every block (free or used) also links to its
 * physical neighbours (end = the block after it, last = the block before).
 * A terminating header sits at the end of the arena (end == NULL).
 * Allocated blocks have next == prev == NULL; the user area follows the
 * header.
 */
typedef struct MEMBLK {
    struct MEMBLK *next;        /* free list next (NULL when allocated) */
    struct MEMBLK *prev;        /* free list previous (NULL when allocated) */
    struct MEMBLK *end;         /* physically next block (NULL: arena end) */
    struct MEMBLK *last;        /* physically previous block */
} MEMBLK;

/* DC layout; on 64-bit hosts the header is native (32 bytes): it only ever
 * lives inside the arena this code manages, never in ripped game data. */
SA1_ASSERT_SIZE(MEMBLK, 0x10);
SA1_ASSERT_OFFSET(MEMBLK, prev, 0x04);
SA1_ASSERT_OFFSET(MEMBLK, end, 0x08);
SA1_ASSERT_OFFSET(MEMBLK, last, 0x0C);

#define HEAP_SIZE 0x90000

extern Uint8 B_8C7EE8E0[];      /* heap arena, HEAP_SIZE bytes + end header */
extern MEMBLK *B_8C79F03C;      /* free list head */
extern Sint32 B_8C79F040;       /* cleared by the heap init, no other user */

extern void func_8C0344A0(taskwk *twp);        /* free twp->cwp (colliwk) */
extern void func_8C04B468(taskwk *twp);        /* free twp->ewp (eventwk) */

void MFree(void *p);

/* Free a task's taskwk together with its collision and event work. */
void func_8C098998(task *tp)
{
    if (tp->twp->cwp != NULL) {
        func_8C0344A0(tp->twp);
    }
    if (tp->twp->ewp != NULL) {
        func_8C04B468(tp->twp);
    }
    MFree(tp->twp);
    tp->twp = NULL;
}

/* Free the motion work. */
void func_8C0989C8(task *tp)
{
    MFree(tp->mwp);
    tp->mwp = NULL;
}

/* Free the force work. */
void func_8C0989E0(task *tp)
{
    MFree(tp->fwp);
    tp->fwp = NULL;
}

/* Free the any work. */
void func_8C0989F8(task *tp)
{
    MFree(tp->awp);
    tp->awp = NULL;
}

/* Heap init: fill the arena with 0xFF, one free block spanning it. */
void func_8C098A10(void)
{
    Uint8 *p;
    MEMBLK *end;
    Uint32 i;

    p = B_8C7EE8E0;
    B_8C79F03C = (MEMBLK *)p;
    end = (MEMBLK *)(p + HEAP_SIZE);
    for (i = 0; i < HEAP_SIZE; i++, p++) {
        *p = 0xFF;
    }
    B_8C79F03C->next = end;
    B_8C79F03C->prev = NULL;
    B_8C79F03C->end = end;
    B_8C79F03C->last = NULL;
    end->next = NULL;
    end->prev = B_8C79F03C;
    end->end = NULL;
    end->last = B_8C79F03C;
    B_8C79F040 = 0;
}

void *MAlloc(Sint32 size)
{
    MEMBLK *head;
    MEMBLK *blk;
    MEMBLK *nblk;
    MEMBLK *next;
    MEMBLK *prev;
    Uint32 sz;
    Uint32 len;

    if (size == 0) {
        return NULL;
    }
    head = B_8C79F03C;
    if (head == NULL || head->end == NULL) {
        return NULL;
    }
    sz = (size + (sizeof(MEMBLK) + 15)) & ~15;
    for (blk = head; blk != NULL && blk->end != NULL; blk = blk->next) {
        len = (Uint32)((Uint8 *)blk->end - (Uint8 *)blk);
        if (len == sz) {
            next = blk->next;
            prev = blk->prev;
            if (next != NULL) {
                next->prev = prev;
            }
            if (prev != NULL) {
                prev->next = next;
            } else {
                B_8C79F03C = next;
            }
            blk->next = NULL;
            blk->prev = NULL;
            blk++;
            return blk;
        } else if (len > sz) {
            next = blk->next;
            prev = blk->prev;
            nblk = (MEMBLK *)((Uint8 *)blk + sz);
            nblk->next = next;
            nblk->prev = prev;
            nblk->end = blk->end;
            nblk->last = blk;
            blk->end->last = nblk;
            blk->end = nblk;
            if (next != NULL) {
                next->prev = nblk;
            }
            if (prev != NULL) {
                prev->next = nblk;
            } else {
                B_8C79F03C = nblk;
            }
            blk->next = NULL;
            blk->prev = NULL;
            blk++;
            return blk;
        }
    }
    return NULL;
}

ASM_STUB(void MFree(void *p))
