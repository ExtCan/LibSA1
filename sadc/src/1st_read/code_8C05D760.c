/*
 * 1ST_READ 8C05D760 (chunk asm/1st_read/text/8C05D760.src): the dest of the
 * ground item box O_ITEMBOX (set by func_8C05D0A0 and objexec_O_ITEMBOX's
 * mode 0, code_8C05D0A0.c). It does nothing: the box's collision and its
 * 0x23C-byte lock-on block are freed by the task system. Staged by
 * dc-itembox-ground (MATCH).
 */
#include "common.h"
#include "task.h"

void func_8C05D760(task *tp)
{
}