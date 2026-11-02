/*
 * 1ST_READ chunk 8C099A00-8C099AA8: the event coroutine scheduler (the
 * event / menu "threads": each coroutine has its own stack; the main loop
 * 8C04E714 and the event code switch to the next one with 8C099A50).
 * Coroutines form a tree: siblings in a circular list (next / prev), each
 * with a parent and a first child (create / destroy: chunk 8C099AAC).
 *
 *   func_8C099A00  init: the root coroutine B_8C7A0A0C (cleared, alone in
 *                  its sibling ring) becomes the current one B_8C7A0A08
 *   func_8C099A1C  pick the next coroutine in pre-order (first child, else
 *                  the next sibling of the nearest ancestor that is not its
 *                  parent's first child) into B_8C7A0A08
 *   func_8C099A50  yield: save R8-R14, MACH/MACL, GBR, PR and FR12-FR15 on
 *                  the current stack, store R15 in the current coroutine,
 *                  pick the next one and resume it from its saved R15
 *
 * The chunk ends in H'0000 alignment fill and its own .END, the style of
 * the hand-written modules (SHC fills with H'0009); 8C099A50 switches
 * stacks and cannot be written in C (see the comment at it). libsa1 does not
 * run the coroutines (sa1_world_tick.c skips 8C099A50).
 */
#include "sa1/sa1.h"

/* Event coroutine (0x24 bytes; 8C099AB2 frees it with MFree) */
typedef struct ev_coro {
    struct ev_coro *next;       /* 0x00 sibling ring */
    struct ev_coro *prev;       /* 0x04 */
    struct ev_coro *parent;     /* 0x08 */
    struct ev_coro *child;      /* 0x0C first child */
    void *sp;                   /* 0x10 saved R15 (8C099A50) */
    void *unk14;                /* 0x14 */
    void (*dest)(void);         /* 0x18 called once by the destroy 8C099AB2 */
    Uint32 unk1C;               /* 0x1C */
    Uint32 unk20;               /* 0x20 */
} EV_CORO;                      /* 0x24 */

SA1_ASSERT_SIZE(EV_CORO, 0x24);

extern EV_CORO *B_8C7A0A08;     /* the current coroutine */
extern EV_CORO B_8C7A0A0C;      /* the root coroutine (the main loop) */

/* NON_MATCHING: complete and faithful; only the order of the first two loads differs (R6 = 36 before R4 = &B_8C7A0A0C) */
#ifdef NON_MATCHING
void func_8C099A00(void)
{
    memset(&B_8C7A0A0C, 0, sizeof(EV_CORO));
    B_8C7A0A08 = B_8C7A0A0C.next = B_8C7A0A0C.prev = &B_8C7A0A0C;
}
#else
ASM_STUB(void func_8C099A00(void))
#endif

void func_8C099A1C(void)
{
    EV_CORO *c;
    EV_CORO *n;

    c = B_8C7A0A08;
    if (c->child != NULL) {
        n = c->child;
    } else {
        n = c->next;
        goto test;
        do {
            n = n->parent->next;
test:
            ;
        } while (n->parent != NULL && n->parent->child == n);
    }
    B_8C7A0A08 = n;
}

/*
 * func_8C099A50 (82 bytes, hand-written; not in C): yield to the next event
 * coroutine. On the current stack it pushes PR twice, GBR, MACH, MACL,
 * R14-R8, R4 and FR15-FR12, stores R15 in B_8C7A0A08->sp, calls
 * func_8C099A1C, loads R15 from the new B_8C7A0A08->sp and pops the same
 * registers, returning through the saved PR of the coroutine it resumes (the
 * second PR is the resume address a new coroutine's stack is primed with,
 * 8C099B2C). A stack switch has no C equivalent. When no event coroutine
 * exists (only the root B_8C7A0A0C: it is its own ring, without parent or
 * child) func_8C099A1C picks the root again and the call returns to its
 * caller unchanged, so libsa1 handles it with a 'void' policy (LANDING.md);
 * the character select runs never create a coroutine (8C099AB2 / 8C099B2C
 * are not in their coverage).
 */
