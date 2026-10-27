#include "common.h"
#include "task.h"

/* Runs the exec function of every child of tp (children may destroy themselves). */
void func_8C098E98(task *tp)
{
    task *ctp;
    task *next;

    ctp = tp->ctp;
    while (ctp != NULL) {
        next = ctp->next;
        ctp->exec(ctp);
        ctp = next;
    }
}
