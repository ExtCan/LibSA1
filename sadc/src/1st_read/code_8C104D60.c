#include "sa1/sa1.h"

/*
 * 1ST_READ chunk 8C103200:
 *   func_8C104D60  squared distance from pos to player 1's position raised
 *                  by 5.0 (callers func_8C1032AC, func_8C105036,
 *                  func_8C10622C).
 */

Float func_8C104D60(NJS_POINT3 *pos)
{
    NJS_POINT3 p;
    NJS_POINT3 v;

    p = playertwp[0]->pos;
    p.y += 5.0f;
    v.x = p.x - pos->x;
    v.y = p.y - pos->y;
    v.z = p.z - pos->z;
    return SH_ONLY(v.x * v.x + v.y * v.y + v.z * v.z) PC_ONLY(SA1_FMAC(v.z, v.z, SA1_FMAC(v.x, v.x, v.y * v.y)));
}
