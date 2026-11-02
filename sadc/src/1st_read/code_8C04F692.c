/*
 * 1ST_READ chunk 8C04D920 (part): two game-state setters run by the menu
 * front end (gsa1_090_charselect.md 2.7, "other").
 *
 *   func_8C04F692  CurrentCharacter = n (the setter in front of
 *                  GetPlayerNumber 8C04F698)
 *   func_8C04F76C  set the level and act (CurrentLevel / CurrentAct),
 *                  keeping the previous pair in B_8C7493B6 / B_8C7493B8
 *                  (callers: the menus 8C08F440 .. 8C090E1E, the TGS select
 *                  8C09A2D8 / 8C09A420)
 *
 * LANDING: GetPlayerNumber (src/1st_read/code_8C04F698.c) is NON_MATCHING
 * only because SHC's scratch-register rotation needs func_8C04F692 in front
 * of it in the same translation unit (see that file). It is repeated here
 * after func_8C04F692, where it MATCHES; landing moves it into this file and
 * deletes code_8C04F698.c (one definition only).
 */
#include "sa1/sa1.h"

extern Sint16 B_8C7493B6;       /* previous CurrentLevel (8C04F76C) */
extern Sint16 B_8C7493B8;       /* previous CurrentAct (8C04F76C) */

void func_8C04F692(Sint32 n)
{
    CurrentCharacter = n;
}

Sint32 GetPlayerNumber(void)
{
    return (Uint16)CurrentCharacter;
}

void func_8C04F76C(Uint8 level, Uint8 act)
{
    B_8C7493B6 = CurrentLevel;
    B_8C7493B8 = CurrentAct;
    CurrentLevel = level;
    CurrentAct = act;
}
