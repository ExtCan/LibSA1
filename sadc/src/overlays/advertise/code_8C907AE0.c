/*
 * ADVERTISE 8C907AE0: the "Clear" percentage of select slot n (chunk
 * 8C907AE0, its only function): the value of the last entry of the slot's
 * list D_8C911618[n] whose condition holds (kind 0: event flag 8C09068E,
 * kind 1: 8C049A5A).
 */
#include "common.h"
#include "sa1/sa1.h"

typedef struct ADV_CLEARENT {
    Sint32 value;               /* 0x00 percentage */
    Sint32 kind;                /* 0x04 0 event flag, 1 8C049A5A */
    Sint32 flag;                /* 0x08 */
} ADV_CLEARENT;                 /* 0x0C */

typedef struct ADV_CLEARLIST {
    ADV_CLEARENT *list;         /* 0x00 */
    Sint32 count;               /* 0x04 */
} ADV_CLEARLIST;                /* 0x08 */

extern ADV_CLEARLIST D_8C911618[];

Sint32 func_8C049A5A(Sint32 n);

/*
 * NON_MATCHING: only the two hoisted function addresses swap registers (the
 * original keeps 8C09068E in R9 and 8C049A5A in R10); tried the case
 * order, early-break forms, the declaration order.
 */
#ifdef NON_MATCHING
Sint32 func_8C907AE0(Sint32 n)
{
    ADV_CLEARLIST *t;
    ADV_CLEARENT *e;
    Sint32 i;
    Sint32 r;

    r = 0;
    t = &D_8C911618[n];
    for (i = 0; i < t->count; i++) {
        e = &t->list[i];
        switch (e->kind) {
        case 0:
            if (func_8C09068E(e->flag)) {
                r = e->value;
            }
            break;
        case 1:
            if (func_8C049A5A(e->flag)) {
                r = e->value;
            }
            break;
        }
    }
    return r;
}
#else
ASM_STUB(Sint32 func_8C907AE0(Sint32 n))
#endif
