/*
 * 1ST_READ 8C0E9072-8C0E9142 (chunk 8C0E6620).
 *
 *  func_8C0E9072  places a task at *pos facing like player 0 (twp->ang.y and
 *                 mwp->ang_aim.y = player 0's ang.y), then swings ang.x
 *                 (clamped to 0x5800..0xA800, step 0x200) and ang.z (clamped
 *                 to 0x2800..0x5800, step 0x80) back and forth between two
 *                 turning points kept in mwp->ang_aim.x/z (only caller:
 *                 func_8C0E943C)
 */
#include "common.h"
#include "sa1/sa1.h"

void func_8C0E9072(task *tp, NJS_POINT3 *pos)
{
    taskwk *twp;
    taskwk *ptwp;
    Angle ang;
    Angle aim;

    twp = tp->twp;
    ptwp = B_8C78C608[0];
    twp->pos = *pos;
    twp->ang.y = tp->mwp->ang_aim.y = ptwp->ang.y;

    ang = tp->twp->ang.x;
    aim = tp->mwp->ang_aim.x;
    if (ang < 0x5800 || ang > 0xA800) {
        ang = 0x5800;
    }
    if (ang < 0x6000 && ang > 0x5000) {
        aim = 0xA000;
    }
    if (ang > 0xA000 && ang < 0xB000) {
        aim = 0x6000;
    }
    if (aim < 0x8000) {
        ang = (Uint16)(ang + 0xFE00);
    } else {
        ang = (Uint16)(ang + 0x200);
    }
    tp->mwp->ang_aim.x = aim;
    tp->twp->ang.x = ang;

    ang = tp->twp->ang.z;
    aim = tp->mwp->ang_aim.z;
    if (ang < 0x2800 || ang > 0x5800) {
        ang = 0x5800;
    }
    if (ang < 0x3000 && ang > 0x2000) {
        aim = 0x5000;
    }
    if (ang > 0x5000 && ang < 0x6000) {
        aim = 0x3000;
    }
    if (aim < 0x4000) {
        ang = (Uint16)(ang + 0xFF80);
    } else {
        ang = (Uint16)(ang + 0x80);
    }
    tp->mwp->ang_aim.z = aim;
    tp->twp->ang.z = ang;
}
