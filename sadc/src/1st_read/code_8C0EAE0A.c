/*
 * 1ST_READ 8C0EAE0A-8C0EAE7A (chunk 8C0EAA80, Big the Cat, fishing).
 *
 *  func_8C0EAE0A  puts the camera point at +0x0C of B_8C6B6E68 30 units from
 *                 player 0 towards tp's task on the horizontal plane (same
 *                 height as player 0). Tail-called by func_8C0E91E4 and
 *                 func_8C0E943C after func_8C0239C2(..., 0, 2).
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * B_8C6B6E68: two NJS_POINT3 (camera position/target-like): 8C0EAEC0 copies
 * +0x00..+0x0B and +0x0C..+0x17 as 12-byte struct copies; 8C022CDE reads
 * +0x00/+0x04/+0x08 as floats. Fields from +0x18 on are not needed here.
 */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 */
    NJS_POINT3 tgt;             /* 0x0C */
} CAM_POINTS_8C6B6E68;

extern CAM_POINTS_8C6B6E68 B_8C6B6E68;

void func_8C0EAE0A(task *tp)
{
    taskwk *ptwp;
    NJS_VECTOR v;

    ptwp = B_8C78C608[0];
    v.x = tp->twp->pos.x - ptwp->pos.x;
    v.y = 0.0f;
    v.z = tp->twp->pos.z - ptwp->pos.z;
    njUnitVector(&v);
    v.x *= 30.0f;
    v.z *= 30.0f;
    B_8C6B6E68.tgt.x = ptwp->pos.x + v.x;
    B_8C6B6E68.tgt.y = ptwp->pos.y;
    B_8C6B6E68.tgt.z = ptwp->pos.z + v.z;
}
