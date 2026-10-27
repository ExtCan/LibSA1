/*
 * 1ST_READ 8C0DF2A0-8C0DF540 (chunk 8C0DF2A0): spawns a ring of eight Amy
 * effect sprites (exec func_8C0DF1A0, disp func_8C0DF100; code_8C0DF100.c)
 * around pos; called by AmyModeMachine (code_8C0D7960.c). The ring is
 * tilted by angz then angx; sprite k starts 5 units out along its direction
 * d_k (0.4358, 0.9, 0) rotated by (k + 1) * 0x2000 around Y, moves along d_k
 * (twp->scl = velocity), with ang.y = k * 0x2000 and ang.z = 0x7000.
 *
 * Matching notes:
 *  - func_8C0DF100/func_8C0DF1A0 are defined before func_8C0DF2A0 in the
 *    original translation unit. SHC does not hoist the address of a function
 *    defined in the same TU into a callee-saved register, so they are
 *    declared with ASM_STUB here (the stubs are never spliced; on PC they are
 *    plain prototypes and code_8C0DF100.c defines them). As extern
 *    prototypes, func_8C0DF1A0 is hoisted into R10 and pos is spilled.
 *  - SHC unrolls the 8-iteration for loop 4x (the 3 FMAC_ACC below are the
 *    12 FMACs of the asm).
 *  - "if ((tp = ...) != NULL)" (TST R0,R0 with MOV R0,R5 in the delay slot)
 *    and the "?:" rotate spellings are needed.
 */
#include "common.h"
#include "sa1/sa1.h"

/* "acc += a * b" compiled to FMAC: fused on PC (SA1_FMAC); SA1_FMAC itself
 * (a * b + acc) gives a different register allocation in SHC (same macro as
 * code_8C0DB230.c/code_8C0DE8C0.c) */
#if defined(TARGET_PC)
#define FMAC_ACC(acc, a, b) ((acc) = SA1_FMAC((a), (b), (acc)))
#else
#define FMAC_ACC(acc, a, b) ((acc) += (a) * (b))
#endif

ASM_STUB(void func_8C0DF100(task *tp))
ASM_STUB(void func_8C0DF1A0(task *tp))

void func_8C0DF2A0(NJS_POINT3 *pos, Angle angx, Angle angz)
{
    NJS_POINT3 src;
    NJS_POINT3 v;
    task *tp;
    taskwk *twp;
    Sint32 i;

    njPushMatrix(D_8C67C640);
    angz ? njRotateZ(NULL, angz) : (void)0;
    angx ? njRotateX(NULL, angx) : (void)0;
    src.x = 0.4358f;
    src.y = 0.9f;
    src.z = 0.0f;
    for (i = 0; i < 0x10000; i += 0x2000) {
        njRotateY(NULL, 0x2000);
        njCalcPoint(NULL, &src, &v);
        if ((tp = CreateElementalTask(2, 6, func_8C0DF1A0)) != NULL) {
            twp = tp->twp;
            twp->pos = *pos;
            FMAC_ACC(twp->pos.x, v.x, 5.0f);
            FMAC_ACC(twp->pos.y, v.y, 5.0f);
            FMAC_ACC(twp->pos.z, v.z, 5.0f);
            twp->scl = v;
            twp->ang.y = i;
            twp->ang.z = 0x7000;
            tp->disp = func_8C0DF100;
        }
    }
    njPopMatrix(1);
}
