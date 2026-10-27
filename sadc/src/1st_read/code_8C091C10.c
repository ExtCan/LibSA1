#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * Returns the SET object's user flag (task.ocp->scUserFlag); the setter is
 * func_8C091C00. Used by objexec_O_GRING (8C087B80).
 * With no ocp the original falls through to RTS without loading R0, so it
 * returns whatever R0 held; the PC build returns 0 there.
 */
Sint8 func_8C091C10(task *tp)
{
    if (tp->ocp) {
        return ((OBJ_CONDITION *)tp->ocp)->scUserFlag;
    }
    PC_ONLY(return 0;)
}
