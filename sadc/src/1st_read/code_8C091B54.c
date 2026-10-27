/*
 * 1ST_READ 8C091B54 and 8C091B68 (chunk asm/1st_read/text/8C090FA0.src, the
 * SET manager unit): two SET condition helpers next to func_8C091B5E
 * (code_8C091B5E.c). Bit 2 (0x0004) of ocp->ssCondition marks a SET object
 * that was used up but stays in place (v1_arch.md review item 3). Callers:
 * the ground item box (code_8C05D0A0.c: objexec_O_ITEMBOX's mode 0 and
 * func_8C05D4CE), CMN_KUSA (8C05A940 chunk), the 8C064E80 chunk (O_Save_Point)
 * and objects of the ADV03 and STG10 overlays. The bit lives in the condition
 * array B_8C79A3E0, so it survives the object leaving and re-entering the
 * SET range. Staged by
 * dc-itembox-ground with the ground item box (both MATCH): without C they are
 * new stubs for libsa1 (not in LIB's code map).
 */
#include "sa1/pending.h"

/* Sets bit 2 (0x0004) of the task's SET condition (ocp->ssCondition |= 4).
 * Called by the ground item box's break (func_8C05D4CE). */
void func_8C091B54(task *tp)
{
    ((OBJ_CONDITION *)tp->ocp)->ssCondition |= 4;
}

/* Returns bit 2 of the task's SET condition (ocp->ssCondition & 4): 4 when
 * the object was used up before. */
Sint32 func_8C091B68(task *tp)
{
    return ((OBJ_CONDITION *)tp->ocp)->ssCondition & 4;
}