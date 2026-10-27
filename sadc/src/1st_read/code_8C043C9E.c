/*
 * 1ST_READ 8C043C9E-8C043D66 (chunk 8C043C00): a per-player effect task
 * that lives while player 0 has power-up bit 2 (pwp->item & 2), created
 * by func_8C07D3DC.
 *
 *  func_8C043C9E  init: plays SE 6, sets dest func_8C043FEC and exec
 *                 lbl_8C043CBE, then tail-calls the exec
 *  lbl_8C043CBE   exec: freed when the followed player (playertwp[
 *                 twp->counter.b[0]]) is gone or player 0 lost item bit 2;
 *                 otherwise func_8C043C00(twp, ptwp) and, with probability
 *                 0.3 per frame, a child task (exec func_8C043D66, disp
 *                 func_8C043E70) with random ang.x/ang.y in 0..0xFFFE and
 *                 value.f = 1.0f; then runs the children
 *
 * The init has no RTS: SHC places the tail-called exec right after it and
 * falls through into it (as in code_8C0B2EA0.c and code_8C0CAD20.c), so the
 * original range of func_8C043C9E (8C043C9E-8C043D66, 200 bytes) holds both
 * functions.
 */
#include "common.h"
#include "sa1/sa1.h"

extern void func_8C043D66(task *tp);    /* child exec (8C043C00.src) */
extern void func_8C043E70(task *tp);    /* child disp (8C043C00.src) */
extern void func_8C043FEC(task *tp);    /* dest (8C043C00.src) */

ASM_STUB(void func_8C043C00(taskwk *twp, taskwk *ptwp))

void lbl_8C043CBE(task *tp);

/*
 * NON_MATCHING only for the tools: the code is identical. Compared as one
 * range with lbl_8C043CBE (shcdiff with the compiled lbl_8C043CBE function
 * label ignored) all 100 of 100 instructions match and the layout is the
 * original's (lbl_8C043CBE at +0x20, no BRA).
 * tools/shcdiff.py and tools/splice.py stop the compiled side at the
 * lbl_8C043CBE label, so the function is reported as DIFF and would fail
 * the splice; it stays guarded until the tools learn about fall-through
 * functions. lbl_8C043CBE is not a function of the chunk manifest, so it is
 * not spliced and stays outside the guard.
 */
#ifdef NON_MATCHING
void func_8C043C9E(task *tp)
{
    func_8C0940DA(6, 0, 0, 0);
    tp->dest = func_8C043FEC;
    tp->exec = lbl_8C043CBE;
    lbl_8C043CBE(tp);
}
#else
ASM_STUB(void func_8C043C9E(task *tp))
#endif

void lbl_8C043CBE(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;
    task *ctp;
    taskwk *ctwp;

    twp = tp->twp;
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    if (ptwp == NULL || !(GetPlayerWorkPtr(0)->item & 2)) {
        FreeTask(tp);
        return;
    }
    func_8C043C00(twp, ptwp);
    if ((Float)rand() * 3.0517578125e-05f > 0.7f) {
        if ((ctp = CreateChildTask(2, func_8C043D66, tp)) != NULL) {
            ctwp = ctp->twp;
            ctwp->ang.x = SA1_FTOI((Float)rand() * 2.0f);
            ctwp->ang.y = SA1_FTOI((Float)rand() * 2.0f);
            ctwp->value.f = 1.0f;
            ctp->disp = func_8C043E70;
        }
    }
    func_8C098E98(tp);
}
