#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Player slot accessors 8C07CFA0-8C07D026 (B_8C78C648 = playertp[8],
 * B_8C78C668 = playerpwp[8], filled by PInitialize 8C06C350).
 */

task *func_8C07CFA0(Uint8 pno)
{
    return playertp[pno];
}

/* Character id (twp->counter.b[1]) of a task in a player slot, or -1 (GetCharacterID-like) */
Sint32 func_8C07CFAA(task *tp)
{
    task **pp;
    Sint32 ret;

    ret = -1;
    if (tp == NULL) {
        return -1;
    }
    for (pp = playertp; pp < &playertp[8]; pp++) {
        if (*pp == tp) {
            ret = (Uint8)tp->twp->counter.b[1];
            break;
        }
    }
    return ret;
}

playerwk *func_8C07CFDE(Uint8 pno)
{
    return playerpwp[pno];
}

/* Number of occupied player slots */
Sint32 func_8C07CFE8(void)
{
    Sint32 i;
    Sint32 n;

    n = 0;
    for (i = 0; i < 8; i++) {
        if (playertp[i] != NULL) {
            n++;
        }
    }
    return n;
}
