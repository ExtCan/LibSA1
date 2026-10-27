#include "sa1/pending.h"

/*
 * 1ST_READ 8C091C00: SET object condition helper.
 *
 * In the original binary 8C091C00 and its getter 8C091C10 are the last two
 * functions of the translation unit that starts at 8C090FA0 (the pool at
 * 8C091BE0 is a mid-file pool dump followed by .ALIGN 32; the unit ends with
 * the 00 fill at 8C091C1E). SHC's choice of scratch register at the start of
 * a function depends on the end of the preceding function: compiled with
 * nothing in front, this function uses R3 for both ocp loads; after a leaf
 * whose last temporary is R3 (as func_8C091BC2 at 8C091BC2-8C091BD2:
 * MOV #-128,R3 / SHLL8 / NOT / AND R3,R0) it uses R2, as in the game.
 */

#if !defined(TARGET_PC)
/*
 * Translation-unit context model (SHC check only; never spliced: the
 * function has no address in the image, and it does not exist on PC).
 * Stands in for func_8C091BC2, the function compiled just before
 * func_8C091C00 in the original unit: the SET entry id
 * (*(Sint16 *)ocp->pObjEditEntry) without bit 15.
 */
Sint32 tu_tail_8C091BC2(task *tp)
{
    return *(Sint16 *)((OBJ_CONDITION *)tp->ocp)->pObjEditEntry & 0x7FFF;
}
#endif

/* Stores a value in the task's SET condition user flag byte
 * (ocp->scUserFlag), if the task has a SET entry. The Gold Ring Circle
 * (func_8C087EAC) keeps its collected-ring bit mask there; the matching
 * getter is func_8C091C10. */
void func_8C091C00(task *tp, Sint8 flag)
{
    if (tp->ocp) {
        ((OBJ_CONDITION *)tp->ocp)->scUserFlag = flag;
    }
}
