/*
 * 1ST_READ 8C0B5B80 (chunk 8C0B5B80-8C0B5D60, function 1/1).
 *
 * Spawns one sparkle around Sonic (called three times by SonicMain 8C0A77BC
 * when equipment bit 2 is set): a task (exec func_8C0B5994, disp
 * func_8C0B58E0, collision D_8C1C6B10) placed at a random point on a shell
 * of radius 25..40 around the centre of the player's first collision shape,
 * plus a companion task (func_8C0B5520) that follows its position.
 */
#include "common.h"
#include "sa1/sa1.h"

void func_8C0B5994(task *tp);
void func_8C0B58E0(task *tp);
void func_8C0B5520(task *tp);

extern CCL_INFO D_8C1C6B10[];

/*
 * NON_MATCHING: register allocation only (199/199 instructions, 9 differ,
 * all in 8C0B5C6A-8C0B5C88). The original computes 0x8000 - twp->ang.y in
 * R2/R3, loads CreateElementalTask for the second call into R2 right after
 * the ADD (above the ang.y store) and uses R1 for n + 1. Written with the
 * Angle local below, SHC puts the value in R4 and the constant in R3 (n + 1
 * in R2); everything else matches, including JSR @R2 and the dtwp block.
 * Without the local (ttwp->ang.y = 0x8000 - twp->ang.y) the ang block matches
 * but the call address gets R3 and the dtwp block (8C0B5C96-8C0B5CB6) swaps
 * R2/R3 (18 differ). The sibling func_8C0B5760 (code_8C0B5720.c) has the
 * same JSR @R2 vs @R3 difference at its second CreateElementalTask.
 * Tried without effect on the call register (beyond the earlier attempt's
 * list: statement/declaration orders, expression forms, flag/n types,
 * if/else/goto shapes, function-pointer local, SHC 5.1r01/r04/r08/r11):
 * random declaration orders (120), all six orders of the ang.y/timer/btimer
 * stores (the stores are never reordered), a different symbol for either
 * call, CreateElementalTask prototypes with Uint16/Uint8/Sint16 parameters,
 * void * result or no prototype, casts/&/!= 0 forms of the second call,
 * explicit Angle locals for the FSCA arguments, FMAC operand orders, locals
 * for n + 1 / flag / twp / ttwp, a volatile copy of n, and a
 * #pragma inline helper for the companion task. The C is complete and
 * functionally faithful.
 */
#ifdef NON_MATCHING
void func_8C0B5B80(taskwk *twp, Sint32 n)
{
    playerwk *pwp;
    Sint32 flag;
    task *tp;
    task *dtp;
    taskwk *ttwp;
    taskwk *dtwp;
    NJS_POINT3 *center;
    Float dist;
    Float angx;
    Float angy;
    Angle ang;

    pwp = func_8C07CFDE(twp->counter.b[0]);
    if (pwp != NULL && (pwp->equipment & 1)) {
        flag = 1;
    } else {
        flag = 0;
    }
    if ((tp = CreateElementalTask(2, 2, func_8C0B5994)) == NULL) {
        return;
    }
    ttwp = tp->twp;
    center = &CWP(twp)->info->center;
    ttwp->counter.b[0] = twp->counter.b[0];
    ttwp->scl = *center;
    dist = SA1_FMAC((Float)rand(), 0.000457763671875f, 25.0f);
    angx = (Float)(SA1_FTOI((Float)rand() * 0.375f) - 0x800);
    angy = (Float)SA1_FTOI((Float)rand() * 2.0f);
#ifdef TARGET_PC
    /*
     * Same values; each angle is converted once, as in the original (FTRC of
     * angy kept on the stack, of angx in R8). Repeating SA1_FTOI makes clang
     * duplicate the pos.z FMA over its saturation branch (5 fused ops for 4
     * FMACs in tools/fmac_audit.py). Explicit locals here cost SHC 160
     * differing instructions, hence PC only.
     */
    {
        Sint32 iangy = SA1_FTOI(angy);
        Sint32 iangx = SA1_FTOI(angx);

        ttwp->pos.x = SA1_FMAC(dist * njSin(iangy), njCos(iangx), center->x);
        ttwp->pos.z = SA1_FMAC(dist * njCos(iangy), njCos(iangx), center->z);
        ttwp->pos.y = SA1_FMAC(dist, njSin(iangx), center->y);
    }
#else
    ttwp->pos.x = SA1_FMAC(dist * njSin(SA1_FTOI(angy)), njCos(SA1_FTOI(angx)), center->x);
    ttwp->pos.z = SA1_FMAC(dist * njCos(SA1_FTOI(angy)), njCos(SA1_FTOI(angx)), center->z);
    ttwp->pos.y = SA1_FMAC(dist, njSin(SA1_FTOI(angx)), center->y);
#endif
    ang = 0x8000 - twp->ang.y;
    ttwp->ang.y = ang;
    ttwp->timer.l = n + 1;
    ttwp->btimer = flag;
    if ((dtp = CreateElementalTask(2, 3, func_8C0B5520)) != NULL) {
        dtwp = dtp->twp;
        dtwp->value.ptr = &tp->twp->pos;
        dtwp->counter.l = n;
        dtwp->timer.l = -1;
        dtwp->pos = tp->twp->pos;
        dtwp->btimer = flag;
    } else {
        FreeTask(tp);
        return;
    }
    CCL_Init(tp, D_8C1C6B10, 1, 1, 1);
    tp->disp = func_8C0B58E0;
}
#else
ASM_STUB(void func_8C0B5B80(taskwk *twp, Sint32 n))
#endif