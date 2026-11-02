/*
 * 1ST_READ chunk 8C0491C0 (part): func_8C04A120, the task wrapper of the
 * event action queue (SADX EV_SetAction): queues an NJS_ACTION on the task's
 * taskwk through func_8C04B4D2 (code_8C04B440.c). Used by the character
 * select's 3D task (8C902D64) with speed 0.25 (gsa1_090_charselect.md 2.2).
 */
#include "sa1/sa1.h"

void func_8C04A120(task *tp, NJS_ACTION *action, NJS_TEXLIST *texlist, Float speed, Sint32 mode,
                   Sint32 linkframe)
{
    if (tp != NULL) {
        func_8C04B4D2(tp->twp, action, texlist, speed, mode, linkframe);
    }
}
