/*
 * 1ST_READ 8C044060-8C044122 (chunk 8C043C00): the barrier task, created by
 * func_8C07D43E with CreateElementalTask(2, 2, func_8C044060).
 *
 *  func_8C044060  init: sound effect 6, dest = func_8C044280 (empty),
 *                 exec = lbl_8C044080, then runs the exec once
 *  lbl_8C044080   exec: freed when the player (playertwp[counter.b[0]]) is
 *                 gone or player 0 lost the barrier (item bit 1); otherwise
 *                 func_8C043C00 follows the player and, with probability
 *                 0.3 per frame, a child spark task (exec func_8C044122,
 *                 disp func_8C044166) gets random ang.x/ang.y (rand() * 2);
 *                 then runs the children (func_8C098E98)
 *
 * The init has no RTS: SHC places the tail-called exec right after it and
 * falls through into it (as in code_8C0B2EA0.c and code_8C0CAD20.c), so the
 * original range of func_8C044060 (8C044060-8C044122, 194 bytes) holds both
 * functions.
 */
#include "common.h"
#include "task.h"
#include "sa1/sa1.h"

void func_8C044122(task *tp);
void func_8C044166(task *tp);
void func_8C044280(task *tp);

ASM_STUB(void func_8C043C00(taskwk *twp, taskwk *ptwp))

void lbl_8C044080(task *tp);

/*
 * NON_MATCHING only for the tools: the code is identical. Compared as one
 * range with lbl_8C044080 (shcdiff with the compiled lbl_8C044080 function
 * label ignored) all 97 of 97 instructions match, literal pool included.
 * tools/shcdiff.py and tools/splice.py stop the compiled side at the
 * lbl_8C044080 label (16 vs 97 instructions), so the function is reported as
 * DIFF and would fail the splice; it stays guarded until the tools learn
 * about fall-through functions. lbl_8C044080 itself is not a function of the
 * chunk manifest, so it is not spliced and stays outside the guard.
 */
#ifdef NON_MATCHING
void func_8C044060(task *tp)
{
    func_8C0940DA(6, 0, 0, 0);
    tp->dest = func_8C044280;
    tp->exec = lbl_8C044080;
    lbl_8C044080(tp);
}
#else
ASM_STUB(void func_8C044060(task *tp))
#endif

void lbl_8C044080(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;
    task *ctp;
    taskwk *ctwp;

    twp = tp->twp;
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    if (ptwp == NULL || !(GetPlayerWorkPtr(0)->item & 1)) {
        FreeTask(tp);
        return;
    }
    func_8C043C00(twp, ptwp);
    if ((Float)rand() * 3.0517578125e-05f > 0.7f) {
        if ((ctp = CreateChildTask(2, func_8C044122, tp)) != NULL) {
            ctwp = ctp->twp;
            ctwp->ang.x = SA1_FTOI((Float)rand() * 2.0f);
            ctwp->ang.y = SA1_FTOI((Float)rand() * 2.0f);
            ctp->disp = func_8C044166;
        }
    }
    func_8C098E98(tp);
}
