/*
 * ADVERTISE 8C900F20: clears the stage / act to start, D_8C9610C8 (chunk
 * 8C900F20, function 1 of 5; the other four count / list the Trial mode
 * entries and are not executed by the character select).
 */
#include "common.h"

typedef struct ADV_STAGEACT {
    Uint8 stage;                /* 0x00 */
    Uint8 act;                  /* 0x01 */
} ADV_STAGEACT;                 /* 0x02 */

extern ADV_STAGEACT D_8C9610C8; /* stage / act to start (func_8C901280) */

void func_8C900F20(void)
{
    D_8C9610C8.stage = 0;
    D_8C9610C8.act = 0;
}
