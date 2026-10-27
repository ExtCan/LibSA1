/*
 * 1ST_READ 8C04C960-8C04C98E (chunk 8C04C680; game state):
 *
 *  func_8C04C960  level type of CurrentLevel: 1 for the adventure fields
 *                 (levels 26-34) and the levels above 38, 2 otherwise (the
 *                 action stages, bosses, Twinkle Circuit, Sky Chase, Sand
 *                 Hill). The HUD uses it: 1 hides the timer and makes the
 *                 ring / life counters pop up (8C08A4BA, 8C08A198,
 *                 8C08A120, Big's HUD 8C0E6FE0), and the logic step tail
 *                 8C04E714 runs the game clock only when it is not 1.
 */
#include "common.h"
#include "sa1/sa1.h"

Sint32 func_8C04C960(void)
{
    Sint32 ret;

    if (CurrentLevel <= 14) {
        ret = 2;
    } else if (CurrentLevel <= 25) {
        ret = 2;
    } else if (CurrentLevel <= 34) {
        ret = 1;
    } else if (CurrentLevel <= 38) {
        ret = 2;
    } else {
        ret = 1;
    }
    return ret;
}
