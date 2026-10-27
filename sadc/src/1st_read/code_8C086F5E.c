#include "common.h"
#include "ninja.h"
#include "task.h"

/*
 * func_8C086F5E: scatter the player's rings when hit (flag == 0 only).
 * Up to 20 rings are spawned with func_8C0870EA as exec, spread evenly over
 * 360 degrees with a random start angle; with no rings the player is killed
 * (func_8C07BBC2) and a death sound is played (0x4AD for character 6).
 *
 * NON_MATCHING, for two reasons:
 *
 * 1. Translation-unit boundary: the original has a literal pool dumped in the
 *    middle of this function (8C086FC4-8C08705F, after the BRA in the n == 0
 *    branch) that starts with the pool entries of the preceding function
 *    func_8C086DE6 (func_8C04FE40, B_8C754EC8, func_8C043A20, ...). SHC only
 *    does that when an earlier function of the same .c file left pool entries
 *    pending, so this function was compiled in the same file as the
 *    functions before it (chunk 8C0869E0) and cannot match on its own: the
 *    pool pushes the end of the function out of BT range, giving
 *    "BT +6; BRA end" instead of "BF end". Verified with a test file that
 *    puts a dummy function in front to force the pool to the same place:
 *    everything then matches except point 2.
 *
 * 2. Register allocation: func_8C0940DA (R10 in the original) and
 *    &B_8C78C608 (R9) are swapped here. Tried: Sint16 count, i * 360 / n,
 *    while/do-while loops, early return, local player pointer (struct and
 *    component copies), (*B_8C78C608)->pos, temporaries for the division and
 *    the float, ternary/variable for the sound id, declaration orders;
 *    only extra references to func_8C0940DA or a float component copy flip
 *    the two registers, and both change the code elsewhere.
 */

extern Sint32 rand(void);                                     /* rand() */
extern Sint16 func_8C08A2BC(void);                                     /* ring count */
extern void func_8C07BBC2(Uint8 pno);
extern Sint32 GetPlayerNumber(void);                                     /* character id */
extern void func_8C0940DA(Sint32 se, Sint32 a, Sint32 b, Sint32 c);  /* sound */
extern void func_8C08A074(void);
extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec); /* CreateElementalTask */
extern void func_8C0870EA(task *tp);
extern taskwk *B_8C78C608[];                                           /* playertwp[] */

#ifdef NON_MATCHING
void func_8C086F5E(Sint8 flag)
{
    Float f;
    Sint32 n;
    Sint32 i;
    Sint32 ang;
    task *tp;
    taskwk *twp;

    f = (Float)rand() * 0.010986328125f; /* rand() * 360 / 32768 */
    n = func_8C08A2BC();
    if (flag == 0) {
        if (n > 20) {
            n = 20;
        } else if (n == 0) {
            func_8C07BBC2(0);
            if (GetPlayerNumber() == 6) {
                func_8C0940DA(0x4AD, 0, 0, 0);
            } else {
                func_8C0940DA(23, 0, 0, 0);
            }
            return;
        }
        func_8C08A074();
        for (i = 0, ang = 0; i < n; i++, ang += 360) {
            tp = CreateElementalTask(10, 2, func_8C0870EA);
            if (tp) {
                twp = tp->twp;
                twp->pos = B_8C78C608[0]->pos;
                twp->ang.y = ((Float)(ang / n) + f) * 182.04444f; /* NJM_DEG_ANG */
            }
        }
        func_8C0940DA(0, 0, 0, 0);
    }
}
#endif
