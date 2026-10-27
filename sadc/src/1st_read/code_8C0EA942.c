/*
 * 1ST_READ 8C0EA942-8C0EA9F6 (Big the Cat, fishing):
 *
 *  func_8C0EA942  writes to *pos the world position of a point at offset
 *                 (-0.6, 0.3, 0) in the lure task's frame (B_8C7A40C8:
 *                 rotated by -ang.y, ang.x, ang.z, then translated by the
 *                 lure position); (0, 0, 0) when there is no lure task.
 *                 Called by func_8C0E7720, func_8C0E78C0, func_8C0E943C.
 */
#include "common.h"
#include "sa1/sa1.h"

extern task *B_8C7A40C8;        /* lure task (func_8C0E6DA0's result) */

void func_8C0EA942(NJS_POINT3 *pos)
{
    taskwk *twp;
    NJS_MATRIX m;
    NJS_VECTOR v;

    if (B_8C7A40C8 == NULL) {
        pos->z = 0.0f;
        pos->y = 0.0f;
        pos->x = 0.0f;
        return;
    }
    twp = B_8C7A40C8->twp;
    v.x = -0.59999998f;
    v.y = 0.29999999f;
    v.z = 0.0f;
    njUnitMatrix((NJS_MATRIX_PTR)&m);
    -twp->ang.y ? njRotateY((NJS_MATRIX_PTR)&m, -twp->ang.y) : (void)0;
    twp->ang.x ? njRotateX((NJS_MATRIX_PTR)&m, twp->ang.x) : (void)0;
    twp->ang.z ? njRotateZ((NJS_MATRIX_PTR)&m, twp->ang.z) : (void)0;
    njCalcVector((NJS_MATRIX_PTR)&m, &v, pos);
    pos->x += twp->pos.x;
    pos->y += twp->pos.y;
    pos->z += twp->pos.z;
}
