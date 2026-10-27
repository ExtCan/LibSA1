#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/collision.h"

/*
 * 8C028480-8C028620: part of the land collision module 8C027360-8C02CE80.
 *
 * This module was not built like the rest of the game. Its 16-bit constants
 * (stack frame sizes, stack offsets > 127, 0x2000, 0xC0) are MOV.W pool
 * loads, and float constants are reloaded at every use instead of being
 * kept in FR12-FR15. Both functions below match byte-exactly with SHC
 * 5.0r31 or 5.0r32 and the standard game flags:
 *
 *   py -3.12 tools/shcdiff.py func_8C028480 src/1st_read/code_8C028480.c
 *       --ver=5.0r32 --flags=-define=NON_MATCHING
 *
 * SHC 5.1r03 with -extra=a=1800 builds those constants with MOV #imm/SHLL8,
 * and it keeps +-100000.0f in FR14/FR15 in func_8C028480. Until the build can
 * select the compiler version per file, the bodies stay under NON_MATCHING
 * and the original assembly stays in the build.
 */

#ifdef NON_MATCHING

/*
 * Tests one land object against the probe in cp (cp->pos). On a hit it
 * returns 1 with the polygon height in cp->yb and its slope in
 * cp->angx/angz. 8C02B620, JSR callee.
 */
extern Sint32 func_8C02B620(csts *cp, NJS_OBJECT *obj, Uint32 attr);

/*
 * Free-list builders for 64-byte nodes (hand-written asm, DT loops):
 * link n nodes starting at pool through their first word and store the
 * head in D_8C02D540 (8C02CE80) or D_8C02D544 (8C02CECA, which also clears
 * D_8C02D548).
 */
extern void func_8C02CE80(void *pool, Sint32 n);
extern void func_8C02CECA(void *pool, Sint32 n);

extern Uint8 B_8C6BE27C[];      /* node pool: 0x2000 nodes of 64 bytes on DC */
extern Uint8 B_8C6BB27C[];      /* node pool: 0xC0 nodes of 64 bytes (0x3000 bytes, ends at B_8C6BE27C) */

/*
 * Vertical probe at pos: gathers the land objects within r of pos, then
 * finds the nearest polygon above pos (*top) and below it (*bottom). The
 * slope angles of the hit polygons are optional outputs (NULL to skip).
 * When nothing is found above, *top is pos->y - 100000; when nothing is
 * found below, *bottom is pos->y + 100000.
 */
void func_8C028480(NJS_POINT3 *pos, Float r, Float *top, Angle *top_angx, Angle *top_angz,
                   Float *bottom, Angle *bottom_angx, Angle *bottom_angz)
{
    csts cs;
    Sint32 found_top;
    Sint32 found_bottom;
    _OBJ_LANDCOLL *lc;
    Sint32 i;

    *top = pos->y + 100000.0f;
    *bottom = pos->y + -100000.0f;
    found_top = 0;
    found_bottom = 0;
    lc = B_8C754EF4;
    func_8C0522C0(pos->x, pos->y, pos->z, r);
    cs.pos.x = pos->x;
    cs.pos.y = pos->y;
    cs.pos.z = pos->z;
    for (i = 0; i < B_8C754EF0; i++) {
        if (func_8C02B620(&cs, lc[i].pObject, lc[i].slAttribute) == 1) {
            if (cs.yb > pos->y) {
                if (*top > cs.yb) {
                    *top = cs.yb;
                    if (top_angx) {
                        *top_angx = cs.angx;
                    }
                    if (top_angz) {
                        *top_angz = cs.angz;
                    }
                    found_top = 1;
                }
            } else if (cs.yb > *bottom) {
                *bottom = cs.yb;
                if (bottom_angx) {
                    *bottom_angx = cs.angx;
                }
                if (bottom_angz) {
                    *bottom_angz = cs.angz;
                }
                found_bottom = 1;
            }
        }
    }
    if (!found_top) {
        *top = pos->y + -100000.0f;
    }
    if (!found_bottom) {
        *bottom = pos->y + 100000.0f;
    }
}

/* Initialises the two 64-byte node pools of the land collision module. */
void func_8C0285D2(void)
{
    func_8C02CE80(B_8C6BE27C, 0x2000);
    func_8C02CECA(B_8C6BB27C, 0xC0);
}

#endif /* NON_MATCHING */
