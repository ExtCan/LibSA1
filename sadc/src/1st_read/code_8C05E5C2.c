/*
 * 1ST_READ 8C05E5C2 (chunk 8C05D9C0-8C05E680, item box code; dc-itembox-a,
 * staged for G-SA1 0.9.2; gsa1_090_faults.md F2b): the bomb sphere test.
 *
 *  func_8C05E5C2  1 when pos is inside the bomb sphere (centre B_8C78C384,
 *                 radius B_8C78C390, < 0 = off); the enemy test 8C0486A0
 *                 destroys an enemy for which it returns 1
 *
 * The sphere is started by the bomb item (func_8C05D930 -> func_8C05E582)
 * and grown / ended only by the item icon list task (func_8C05E14E ->
 * func_8C05E59A, code_8C05E0CE.c). LANDING: let libsa1 lower this function
 * only together with a world boot that creates the icon task (as the stage
 * start 8C058E48 does); without the task a bomb leaves the radius at 10
 * for the rest of the world (gsa1_090_faults.md review R.3.2). A lowered
 * definition overrides LIB's "ret 0" policy line (gen_stubs.py: a defined
 * function is never stubbed): see LANDING.md.
 *
 * SPLIT: the manifest has no function at 8C05E612 (the debug print, BRA
 * target of objexec_O_ItemBoxAir), so tools/shcdiff.py counts
 * 8C05E612-8C05E63A as part of this function; with func_8C05E612 =
 * 0x8C05E612 added to config/symbols.1st_read.txt (LANDING.md) it matches.
 */
#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

extern NJS_POINT3 B_8C78C384;           /* bomb sphere centre */
extern Float B_8C78C390;                /* bomb sphere radius, < 0 = off */

Sint32 func_8C05E5C2(NJS_POINT3 *pos)
{
    NJS_POINT3 v;

    if (B_8C78C390 >= 0.0f) {
        v.x = pos->x - B_8C78C384.x;
        v.y = pos->y - B_8C78C384.y;
        v.z = pos->z - B_8C78C384.z;
        if (B_8C78C390 > njScalor(&v)) {
            return 1;
        }
    }
    return 0;
}
