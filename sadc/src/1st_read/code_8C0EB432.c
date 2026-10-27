#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * 1ST_READ 8C0EB432-8C0EB472 (chunk 8C0EB244-8C0EBFA0, function 6 of 25).
 *
 *  func_8C0EB432  (Big, func_8C0E42F0) nearest target check: walks the
 *                 player's target list (B_8C73EE38 for player 0,
 *                 B_8C742288 otherwise; ended by a NULL twp) and returns 1
 *                 when the smallest dist is at most 2500 (50 units, dist is
 *                 squared), 0 when it is larger or the list is empty.
 */

Sint32 func_8C0EB432(taskwk *twp)
{
    PL_TARGET *p;
    Float min;

    if (twp->counter.b[0] == 0) {
        p = B_8C73EE38;
    } else {
        p = B_8C742288;
    }
    if (p->twp == NULL) {
        return 0;
    }
    min = p->dist;
    while (p->twp != NULL) {
        if (min > p->dist) {
            min = p->dist;
        }
        p++;
    }
    if (min > 2500.0f) {
        return 0;
    }
    return 1;
}
