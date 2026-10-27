/*
 * 1ST_READ 8C066732: slope angles of a (normal) vector.
 *
 * *angz = -atan2f(v->x, v->y) and *angx = atan2f(v->z, |v.xy|), both
 * converted from radians to Angle (65536 / 2pi, bits 0x4622F986).
 * Callers: 8C02B2D6 (land ray cast), 8C066922, 8C081380.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

void func_8C066732(NJS_VECTOR *v, Angle *angx, Angle *angz)
{
    *angz = -(Angle)SA1_FTOI(atan2f(v->x, v->y) * 10430.381f);
    *angx = (Angle)SA1_FTOI(atan2f(v->z, njSqrt(SA1_FMAC(v->x, v->x, v->y * v->y))) * 10430.381f);
}
