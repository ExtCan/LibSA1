/*
 * CCL (object-vs-object collision) 8C033800-8C033DAE: the shape-pair
 * dispatch variants called by the per-frame pair loop 8C033F00.
 */
#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"

/* same chunk (BSR) */
ASM_STUB(void func_8C032D98(colliwk *cwp1, colliwk *cwp2))
ASM_STUB(void func_8C033122(colliwk *cwp1, colliwk *cwp2))
ASM_STUB(Sint32 func_8C0332A0(colliwk *cwp1, colliwk *cwp2))
ASM_STUB(Sint32 func_8C03357A(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2))

/* shape-pair tests (JSR); they return the squared distance of the shapes */
extern Float func_8C02FF7E(colliwk *cwp1, colliwk *cwp2);  /* sphere - sphere */
extern Float func_8C0301A0(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C030540(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C030634(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C0309E0(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C030F20(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C0315C0(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C031726(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C031B88(colliwk *cwp1, colliwk *cwp2);  /* sphere - box */
/* func_8C031F88 and func_8C032406 (no result): sa1/protos.h */

/*
 * Variant of 8C0335D4: tests every shape pair of cwp1 x cwp2 (forms 0, 1, 3,
 * 4) and returns the result of the last pair test that set it (1000000.0f
 * when none did). The empty "default:" of the form-4 switch is needed for
 * the match (it moves a scratch register of the loop test).
 */
Float func_8C033800(colliwk *cwp1, colliwk *cwp2)
{
    Float ret;
    taskwk *twp1;
    taskwk *twp2;
    Uint16 i;
    Uint16 j;

    ret = 1000000.0f;
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    if (func_8C03357A(twp1, cwp1, twp2, cwp2)) {
        for (i = 0; i < cwp1->nbInfo; i++) {
            for (j = 0; j < cwp2->nbInfo; j++) {
                cwp1->test_num = i;
                cwp2->test_num = j;
                if (func_8C0332A0(cwp1, cwp2)) {
                    continue;
                }
                if (!(cwp1->info[i].attr & 0x10) && !(cwp2->info[j].attr & 0x10)) {
                    if (!(cwp1->info[i].attr & 0xF)) {
                        switch (cwp2->info[j].attr & 0xF) {
                        case 0:
                            ret = func_8C02FF7E(cwp1, cwp2);
                            break;
                        case 1:
                            ret = func_8C030634(cwp1, cwp2);
                            break;
                        case 3:
                            ret = func_8C030F20(cwp1, cwp2);
                            break;
                        case 4:
                            func_8C031B88(cwp1, cwp2);
                            break;
                        }
                    } else if ((cwp1->info[i].attr & 0xF) == 1) {
                        switch (cwp2->info[j].attr & 0xF) {
                        case 0:
                            ret = func_8C030540(cwp2, cwp1);
                            break;
                        case 1:
                            ret = func_8C030634(cwp1, cwp2);
                            break;
                        case 3:
                            ret = func_8C0315C0(cwp1, cwp2);
                            break;
                        case 4:
                            break;
                        }
                    } else if ((cwp1->info[i].attr & 0xF) == 3) {
                        switch (cwp2->info[j].attr & 0xF) {
                        case 0:
                            ret = func_8C030F20(cwp2, cwp1);
                            break;
                        case 1:
                            ret = func_8C0315C0(cwp2, cwp1);
                            break;
                        case 3:
                            ret = func_8C031726(cwp1, cwp2);
                            break;
                        case 4:
                            break;
                        }
                    } else if ((cwp1->info[i].attr & 0xF) == 4) {
                        switch (cwp2->info[j].attr & 0xF) {
                        case 0:
                            break;
                        case 1:
                            break;
                        case 4:
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
        }
    }
    return ret;
}

/*
 * Pair dispatch with cwp1's first shape against all shapes of cwp2 (forms
 * 0-5), then cwp1's other shapes against cwp2 (forms 0, 1, 3, 4).
 * NON_MATCHING: complete and faithful; opcodes and control flow are those of
 * the original, only the register allocation differs (about 36 instructions
 * once aligned). The original keeps a shared constant 0 in R8 (MOV #0,R8;
 * MOV R8,R10 / MOV R8,R9 for j and i of the first loop, MOV R8,R9 for the
 * inner j = 0 of the second loop), 16 in R11 and j / i*52 in R10; SHC here
 * keeps the address of func_8C030F20 in R8 (JSR @R8, three call sites),
 * rematerialises 0 and swaps 16 and j / i*52 (R10/R11). The same
 * "callee address instead of a constant in R8" difference is seen in
 * func_8C033DAE and func_8C0AF280. As in func_8C033CDC (which matches with
 * it), the first loop stores i twice: without that SHC spills the first
 * loop's i to the stack too (77 aligned differences instead of 36).
 * Tried without effect: a separate variable for the first loop's 0, literal
 * 0 stores, unused call results, extra j = 0 / i = 0 assignments,
 * duplicate stores in the second loop, switch default variants.
 */
#ifdef NON_MATCHING
void func_8C033A10(colliwk *cwp1, colliwk *cwp2)
{
    Uint16 i;
    Uint16 j;
    CCL_INFO *info1;
    CCL_INFO *info2;
    taskwk *twp1;
    taskwk *twp2;

    info1 = cwp1->info;
    info2 = cwp2->info;
    if (info1 == NULL || info2 == NULL) {
        return;
    }
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    if (!func_8C03357A(twp1, cwp1, twp2, cwp2)) {
        return;
    }
    i = 0;
    for (j = 0; j < cwp2->nbInfo; j++) {
        cwp1->test_num = i;
        cwp1->test_num = i;
        cwp2->test_num = j;
        if (func_8C0332A0(cwp1, cwp2)) {
            continue;
        }
        if ((cwp1->info[i].attr & 0x10) || (cwp2->info[j].attr & 0x10)) {
            continue;
        }
        switch (cwp2->info[j].attr & 0xF) {
        case 0:
            func_8C02FF7E(cwp1, cwp2);
            break;
        case 1:
            func_8C0301A0(cwp1, cwp2);
            break;
        case 2:
            func_8C0309E0(cwp1, cwp2);
            break;
        case 3:
            func_8C030F20(cwp1, cwp2);
            break;
        case 4:
            func_8C031F88(cwp1, cwp2);
            break;
        case 5:
            func_8C032406(cwp1, cwp2);
            break;
        }
    }
    for (i = 1; i < cwp1->nbInfo; i++) {
        for (j = 0; j < cwp2->nbInfo; j++) {
            cwp1->test_num = i;
            cwp2->test_num = j;
            if (func_8C0332A0(cwp1, cwp2)) {
                continue;
            }
            if ((cwp1->info[i].attr & 0x10) || (cwp2->info[j].attr & 0x10)) {
                continue;
            }
            if ((cwp1->info[i].attr & 0xF) == 0) {
                switch (cwp2->info[j].attr & 0xF) {
                case 0:
                    func_8C02FF7E(cwp1, cwp2);
                    break;
                case 1:
                    func_8C030540(cwp1, cwp2);
                    break;
                case 3:
                    func_8C030F20(cwp1, cwp2);
                    break;
                case 4:
                    func_8C031B88(cwp1, cwp2);
                    break;
                }
            } else if ((cwp1->info[i].attr & 0xF) == 1) {
                switch (cwp2->info[j].attr & 0xF) {
                case 0:
                    func_8C030540(cwp2, cwp1);
                    break;
                case 1:
                    func_8C030634(cwp1, cwp2);
                    break;
                case 3:
                    func_8C0315C0(cwp1, cwp2);
                    break;
                case 4:
                    break;
                }
            } else if ((cwp1->info[i].attr & 0xF) == 3) {
                switch (cwp2->info[j].attr & 0xF) {
                case 0:
                    func_8C030F20(cwp2, cwp1);
                    break;
                case 1:
                    func_8C0315C0(cwp2, cwp1);
                    break;
                case 3:
                    func_8C031726(cwp1, cwp2);
                    break;
                case 4:
                    break;
                }
            } else if ((cwp1->info[i].attr & 0xF) == 4) {
                switch (cwp2->info[j].attr & 0xF) {
                case 0:
                    break;
                case 1:
                    break;
                case 4:
                    break;
                default:
                    break;
                }
            }
        }
    }
}
#else
ASM_STUB(void func_8C033A10(colliwk *cwp1, colliwk *cwp2))
#endif

/*
 * cwp1's first shape against the shapes of cwp2 of forms 6 (8C032D98) and
 * 7 (8C033122).
 * i must be a Uint16 variable (a literal 0 or a 32-bit/8-bit i is
 * constant-folded) and the store of i is written twice: the redundant store
 * is removed by SHC, but the extra reference raises i's register priority,
 * which gives it R10 above the form mask 0xF (R9) as in the original.
 */
void func_8C033CDC(colliwk *cwp1, colliwk *cwp2)
{
    Uint16 i;
    Uint16 j;
    CCL_INFO *info1;
    CCL_INFO *info2;

    info1 = cwp1->info;
    info2 = cwp2->info;
    if (info1 == NULL || info2 == NULL) {
        return;
    }
    i = 0;
    for (j = 0; j < cwp2->nbInfo; j++) {
        cwp1->test_num = i;
        cwp1->test_num = i;
        cwp2->test_num = j;
        if (func_8C0332A0(cwp1, cwp2)) {
            continue;
        }
        if ((cwp1->info[i].attr & 0x10) || (cwp2->info[j].attr & 0x10)) {
            continue;
        }
        switch (cwp2->info[j].attr & 0xF) {
        case 6:
            func_8C032D98(cwp1, cwp2);
            break;
        case 7:
            func_8C033122(cwp1, cwp2);
            break;
        }
    }
}
