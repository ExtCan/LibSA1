/*
 * 1ST_READ 8C0673E0: two small helpers from chunk 8C0670B4-8C067480
 * (functions 10-11 of 12; the others are separate assignments).
 *
 * func_8C0673E0(): 1 when the current level (high byte of GetStageNumber())
 * is below 15 (the action stages), else 0. Called by TailsModeMachine.
 *
 * func_8C067414(v): normalises v in place and returns its length before
 * normalisation; a vector shorter than 1e-6 is replaced by (0, 1, 0) first.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

extern void func_8C066320(const NJS_VECTOR *src, NJS_VECTOR *dst);   /* vector copy (chunk 8C066320) */

Sint32 func_8C0673E0(void)
{
    Uint16 level;

    level = (Uint8)((Sint32)GetStageNumber() >> 8);
    if (level >= 0 && level < 15) {
        return 1;
    }
    return 0;
}

Float func_8C067414(NJS_VECTOR *v)
{
    NJS_VECTOR tmp;
    Float len;

    func_8C066320(v, &tmp);
    len = njScalor(&tmp);
    if (len < 0.000001f) {
        v->x = 0.0f;
        v->y = 1.0f;
        v->z = 0.0f;
    }
    njUnitVector(v);
    return len;
}
