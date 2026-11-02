/*
 * ADVERTISE 8C901400: number of character select slots: 7 (with Super
 * Sonic) when not in Trial mode and everything is open or event flag 0x47
 * is set, else 6 (chunk 8C901400, its only function).
 */
#include "common.h"
#include "sa1/sa1.h"

/* the menu's 18-byte block B_8C78B450..B_8C78B461 (array in the original):
 * [0] every character open, [0xA] Trial mode */
extern Uint8 B_8C78B450[18];

Sint32 func_8C901400(void)
{
    {
        Uint8 *p;

        p = B_8C78B450;
        if (p[10] != 0) {
            return 6;
        }
        if (p[0] != 0) {
            return 7;
        }
    }
    if (func_8C09068E(0x47)) {
        return 7;
    }
    return 6;
}
