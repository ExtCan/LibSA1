#include "sa1/pending.h"

/*
 * 1ST_READ 8C091B5E: SET object condition helper.
 * Called by the item boxes (objexec_O_ITEMBOX 8C05D10C,
 * objexec_O_ItemBoxAir 8C05D9F6) and the save point
 * (objexec_O_Save_Point 8C064F40) on their own task.
 */

/* Sets bit 4 (0x10) of the task's SET condition (ocp->ssCondition). */
void func_8C091B5E(task *tp)
{
    ((OBJ_CONDITION *)tp->ocp)->ssCondition |= 0x10;
}
