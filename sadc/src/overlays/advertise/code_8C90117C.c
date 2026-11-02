/*
 * ADVERTISE 8C90117C: is character select slot n unlocked? (chunk 8C901080,
 * function 3 of 3; 8C901080 / 8C9010FE build the Trial lists and are not
 * executed by the character select). Everything is open when B_8C78B450 is
 * set; otherwise the slot's event flag: 0x41 Sonic, 0x42 Tails, 0x43
 * Knuckles, 0x44 Amy, 0x46 Big, 0x45 E-102, 0x47 Super Sonic. Another n is
 * returned unchanged.
 */
#include "common.h"
#include "sa1/sa1.h"

extern Uint8 B_8C78B450[18];    /* the menu block; [0] every character open */

Sint32 func_8C90117C(Sint32 n)
{
    if (B_8C78B450[0] != 0) {
        return 1;
    }
    switch (n) {
    case 0:
        n = func_8C09068E(0x41);
        break;
    case 1:
        n = func_8C09068E(0x42);
        break;
    case 2:
        n = func_8C09068E(0x43);
        break;
    case 3:
        n = func_8C09068E(0x44);
        break;
    case 4:
        n = func_8C09068E(0x46);
        break;
    case 5:
        n = func_8C09068E(0x45);
        break;
    case 6:
        n = func_8C09068E(0x47);
        break;
    }
    return n;
}
