#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0D5D80):
 *
 *  func_8C0D5D80  task kept in tp->twp->value.tp, NULL for a NULL task.
 *                 Both callers (8C083818, 8C085660) pass the task their
 *                 collision hit and compare the result with the task at
 *                 +0x40 of their motion work (the lock-on marker slot,
 *                 E102_TARGETWK.marker_tp in code_8C0D32A0.c).
 */

task *func_8C0D5D80(task *tp)
{
    if (tp == NULL) {
        return NULL;
    }
    return tp->twp->value.tp;
}
