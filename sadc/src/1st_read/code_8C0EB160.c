/*
 * 1ST_READ 8C0EB160-8C0EB184 (chunk 8C0EB160; Big the Cat, fishing HUD task):
 *
 *  func_8C0EB160  exec of Big's HUD task (created by BigMain 8C0DFFA0, never
 *                 freed). Mode 0: install the display func_8C0E70E4 (run only
 *                 by the pause display pass 8C0988C4) and go to mode 1.
 *                 Mode 1: every frame, the in-play HUD func_8C0E6FE0
 *                 (code_8C0E6FE0.c: WEIGHT label, ring icon, life icon and
 *                 the three numbers).
 */
#include "common.h"
#include "sa1/sa1.h"

void func_8C0E6FE0(task *tp);   /* code_8C0E6FE0.c */
void func_8C0E70E4(task *tp);   /* code_8C0E6FE0.c */

void func_8C0EB160(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        tp->disp = func_8C0E70E4;
        twp->mode = 1;
        break;
    case 1:
        func_8C0E6FE0(tp);
        break;
    }
}
