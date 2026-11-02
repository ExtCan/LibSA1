/*
 * 1ST_READ chunk 8C043120 (part): func_8C043998, run by the menu init
 * (gsa1_090_charselect.md review R.4 / RC5): B_8C74913C = *p. Callers: the
 * game init 8C04CA40 / 8C04DA20 and the menus 8C08F210 / 8C08F594. What the
 * word means is UNVERIFIED (read by 8C042F30's display code with
 * njColorBlendingMode nearby).
 */
#include "sa1/sa1.h"

extern Sint32 B_8C74913C;

void func_8C043998(Sint32 *p)
{
    B_8C74913C = *p;
}
