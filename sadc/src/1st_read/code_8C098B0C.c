#include "common.h"
#include "task.h"
#include "sa1/ninja_types.h"
#include "sa1/ninja_funcs.h"
#include "sa1/engine.h"

/*
 * Game heap (MAlloc 8C098A80 / CAlloc / MFree) and child-task helpers.
 *
 * Every heap block starts with a 16-byte header (on the DC); the user pointer
 * is the address right after it. Free blocks are kept in a doubly linked list
 * (B_8C79F03C = head); next/prev are both NULL for a block in use.
 * nextphys/prevphys chain all blocks in address order; the size of a block is
 * nextphys - block, and the terminator block has nextphys == NULL.
 */
typedef struct memblk {
    struct memblk *next;        /* 0x00 free list */
    struct memblk *prev;        /* 0x04 free list */
    struct memblk *nextphys;    /* 0x08 following block in memory */
    struct memblk *prevphys;    /* 0x0C preceding block in memory */
} MEMBLK;                       /* 0x10 */

SA1_ASSERT_SIZE(MEMBLK, 0x10);

extern MEMBLK *B_8C79F03C;      /* free list head */

/*
 * Child task description (array terminated by exec == NULL),
 * e.g. D_8C1B9778 (CMN_KUSA, 8C05A940).
 */
typedef struct {
    TaskFuncPtr exec;           /* 0x00 */
    Sint16 im;                  /* 0x04 CreateElementalTask im */
    Sint16 flag;                /* 0x06 -> twp->flag */
    Angle3 ang;                 /* 0x08 added to the parent's angles */
    NJS_POINT3 pos;             /* 0x14 offset in the parent's frame */
    TaskWkUnion counter;        /* 0x20 -> twp->counter */
} CHILDTASKINFO;                /* 0x24 */

SA1_ASSERT_SIZE(CHILDTASKINFO, 0x24);
SA1_ASSERT_OFFSET(CHILDTASKINFO, ang, 0x08);
SA1_ASSERT_OFFSET(CHILDTASKINFO, pos, 0x14);
SA1_ASSERT_OFFSET(CHILDTASKINFO, counter, 0x20);

extern NJS_MATRIX D_8C67C640;   /* unit matrix */

void func_8C098E98(task *tp);

ASM_STUB(task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec))
ASM_STUB(void DestroyTask(task *tp))
ASM_STUB(void *MAlloc(Sint32 size))

/* MAlloc of n elements of size bytes (size rounded up to 4), zero-filled. */
void *CAlloc(Sint32 n, Sint32 size)
{
    Uint32 total;
    Uint32 i;
    Sint32 *p;
    Sint32 *q;

    total = ((size + 3) & ~3) * n;
    p = MAlloc(total);
    if (p != NULL) {
        q = p;
        for (i = total >> 2; i != 0; i--) {
            *q++ = 0;
        }
    }
    return p;
}

void MFree(void *p)
{
    MEMBLK *blk;
    MEMBLK *prv;
    MEMBLK *nxt;
    MEMBLK *n;
    MEMBLK *l;

    if (p != NULL) {
        blk = (MEMBLK *)p - 1;
        if (blk->next != NULL && blk->prev != NULL) {
            for (;;) {
            }
        }
        prv = blk->prevphys;
        nxt = blk->nextphys;
        if (prv != NULL && (prv->next != NULL || prv->prev != NULL)
            && (nxt->next != NULL || nxt->prev != NULL) && nxt->nextphys != NULL) {
            /* both neighbours free: merge all three into prv */
            nxt->prevphys = prv;
            prv->nextphys = nxt;
            n = nxt->next;
            l = nxt->prev;
            if (n != NULL) {
                n->prev = l;
            }
            if (l != NULL) {
                l->next = n;
            } else {
                B_8C79F03C = n;
            }
            nxt->nextphys->prevphys = prv;
            prv->nextphys = nxt->nextphys;
        } else if (prv != NULL && (prv->next != NULL || prv->prev != NULL)) {
            /* previous block free: grow it */
            nxt->prevphys = prv;
            prv->nextphys = nxt;
        } else if ((nxt->next != NULL || nxt->prev != NULL) && nxt->nextphys != NULL) {
            /* next block free: blk takes its place in the free list */
            n = nxt->next;
            l = nxt->prev;
            blk->next = n;
            blk->prev = l;
            if (n != NULL) {
                nxt->next->prev = blk;
            }
            if (l != NULL) {
                nxt->prev->next = blk;
            } else {
                B_8C79F03C = blk;
            }
            blk->nextphys = nxt->nextphys;
            nxt->nextphys->prevphys = blk;
        } else {
            n = B_8C79F03C;
            B_8C79F03C = blk;
            if (n != NULL) {
                n->prev = blk;
            }
            blk->next = n;
            blk->prev = NULL;
        }
    }
}

/* Free heap statistics: total free bytes and largest free block. */
void func_8C098C2C(Uint32 *total, Uint32 *largest)
{
    MEMBLK *blk;
    Uint32 size;
    Uint32 max;
    Uint32 sum;

    sum = 0;
    max = 0;
    for (blk = B_8C79F03C; blk != NULL; blk = blk->next) {
        if (blk->nextphys != NULL) {
            size = (Uint32)((Uint8 *)blk->nextphys - (Uint8 *)blk);
            sum += size;
            if (size > max) {
                max = size;
            }
        }
    }
    if (total != NULL) {
        *total = sum;
    }
    if (largest != NULL) {
        *largest = max;
    }
}

/* Creates a level-7 task as the first child of parent; copies its position/angles. */
task *CreateChildTask(Uint16 im, TaskFuncPtr exec, task *parent)
{
    task *ctp;
    taskwk *ctwp;
    taskwk *ptwp;

    ctp = CreateElementalTask(im, 7, exec);
    if (ctp != NULL) {
        ctp->next = parent->ctp;
        if (parent->ctp != NULL) {
            parent->ctp->last = ctp;
        }
        parent->ctp = ctp;
        ctp->last = NULL;
        ctp->ptp = parent;
        ctwp = ctp->twp;
        ptwp = parent->twp;
        if (ctwp != NULL && ptwp != NULL) {
            ctwp->pos = ptwp->pos;
            ctwp->ang = ptwp->ang;
        }
    } else {
        return NULL;
    }
    return ctp;
}

/*
 * Creates one child of parent per entry of info (until exec == NULL), placed
 * relative to the parent's position and angles; returns the first one.
 */
task *func_8C098D06(CHILDTASKINFO *info, task *parent)
{
    task *ctp;
    task *first;
    task *last;
    taskwk *ptwp;
    taskwk *ctwp;
    Sint32 im;
    NJS_POINT3 v;

    ptwp = parent->twp;
    last = NULL;
    first = NULL;
    njPushMatrix(D_8C67C640);
    if (ptwp != NULL) {
        njTranslateV(NULL, &ptwp->pos);
        ptwp->ang.z ? njRotateZ(NULL, ptwp->ang.z) : (void)0;
        ptwp->ang.x ? njRotateX(NULL, ptwp->ang.x) : (void)0;
        ptwp->ang.y ? njRotateY(NULL, ptwp->ang.y) : (void)0;
    }
    for (; info->exec != NULL; info++) {
        im = info->im;
        ctp = CreateElementalTask(im, 7, info->exec);
        if (ctp != NULL) {
            ctp->last = last;
            if (last != NULL) {
                last->next = ctp;
            } else {
                first = ctp;
            }
            ctp->next = NULL;
            ctp->ptp = parent;
            ctwp = ctp->twp;
            if (ctwp != NULL) {
                ctwp->flag = info->flag;
                njCalcVector(NULL, &info->pos, &v);
                njAddVector(&v, &ptwp->pos);
                njAddVector(&ctwp->pos, &v);
                ctwp->ang.x = ptwp->ang.x + info->ang.x;
                ctwp->ang.y = ptwp->ang.y + info->ang.y;
                ctwp->ang.z = ptwp->ang.z + info->ang.z;
                ctwp->counter = info->counter;
            }
            last = ctp;
        }
    }
    njPopMatrix(1);
    if (first != NULL) {
        if (parent->ctp != NULL) {
            parent->ctp->last = last;
            last->next = parent->ctp;
        }
        parent->ctp = first;
    }
    return first;
}

/* exec: runs the children; once they are all gone, destroys itself. */
void func_8C098E60(task *tp)
{
    if (tp->exec == func_8C098E60) {
        func_8C098E98(tp);
        if (tp->ctp == NULL) {
            tp->exec = DestroyTask;
        }
        return;
    }
    while (tp->ctp != NULL) {
        DestroyTask(tp->ctp);
    }
}

ASM_STUB(void func_8C098E98(task *tp))
