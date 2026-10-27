/*
 * CCL (object-vs-object collision) 8C033DAE-8C03431C: the per-frame pair
 * loops over the per-id colliwk lists filled by CCL_Entry (func_8C02F63C)
 * and the entry point that runs them.
 *
 * Literal pool layout. The original translation unit is the whole chunk
 * 8C0318C0-8C0344A0. In it func_8C033DAE gets a mid-function pool after the
 * BRA of case 1 (8C033E6C), which turns its early returns and the outer loop
 * entry test into "BF/BT over BRA end" pairs. Three stubs placed before
 * func_8C033DAE leave a pending __asm_stub_sink pool entry that SHC dumps at
 * that same BRA (two to four stubs work; none or one gives plain BT/BF, five
 * or more dump it too early); the other stubs are at the end of the file.
 * The splicer ignores the stub bodies and keeps the original pools.
 */
#include "common.h"
#include "ninja.h"
#include "sa1/pending.h"

/* same chunk (BSR targets), still assembly: ASM_STUB bodies below */
Sint32 func_8C0332A0(colliwk *cwp1, colliwk *cwp2);
Sint32 func_8C03357A(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2);
Float func_8C0335D4(colliwk *cwp1, colliwk *cwp2);
Float func_8C033800(colliwk *cwp1, colliwk *cwp2);
void func_8C033A10(colliwk *cwp1, colliwk *cwp2);
void func_8C033CDC(colliwk *cwp1, colliwk *cwp2);

/* shape-pair tests (JSR) */
extern Float func_8C02FF7E(colliwk *cwp1, colliwk *cwp2);  /* sphere - sphere */
extern Float func_8C0301A0(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C030F20(colliwk *cwp1, colliwk *cwp2);
extern void func_8C031D00(colliwk *cwp1, colliwk *cwp2);
extern void func_8C02F81A(void);                           /* reset of the CCL counters */

/* per-id entry lists filled by func_8C02F63C (BSS; counts are MOV.W/EXTU.W) */
extern Uint16 B_8C7456D8;               /* id 0 count (max 8) */
extern Uint16 B_8C7456DA;               /* id 1 count (max 128) */
extern Uint16 B_8C7456DC;               /* id 4 count (max 512) */
extern Uint16 B_8C7456DE;               /* id 5 count (max 128) */
extern Uint16 B_8C7456E0;               /* id 6 count (max 256) */
extern Uint16 B_8C7456E2;               /* id 7 count (max 256) */
extern Uint16 B_8C7456E4;               /* id 8 count (max 256) */
extern Uint16 B_8C752BD8;               /* id 2 count (max 128) */
extern Uint16 B_8C752BDA;               /* id 3 count (max 256) */
extern colliwk *B_8C7456E8[];           /* id 0 list [8] */
extern colliwk *B_8C745708[];           /* id 1 list [128] */
extern colliwk *B_8C745908[];           /* id 2 list [128] */
extern colliwk *B_8C745B08[];           /* id 3 list [256] */
extern colliwk *B_8C745F08[];           /* id 4 list [512] */
extern colliwk *B_8C746708[];           /* id 5 list [128] */
extern colliwk *B_8C746908[];           /* id 6 list [256] */
extern colliwk *B_8C746D08[];           /* id 7 list [256] */
extern colliwk *B_8C747108[];           /* id 8 list [256] */

/* target lists of the players (filled by func_8C03339E) */
extern PL_TARGET B_8C741280[];          /* light speed dash targets, player 0 */
extern PL_TARGET B_8C7446D0[];          /* light speed dash targets, player 1 */
extern Uint16 B_8C747708;               /* entries in B_8C73EE38 */
extern Uint16 B_8C74770A;               /* entries in B_8C741280 */
extern Uint16 B_8C74770C;               /* entries in B_8C742288 */
extern Uint16 B_8C74770E;               /* entries in B_8C7446D0 */

/* pool-layout stubs (see the top of the file) */
ASM_STUB(Sint32 func_8C0332A0(colliwk *cwp1, colliwk *cwp2))
ASM_STUB(Sint32 func_8C03357A(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2))
ASM_STUB(Float func_8C0335D4(colliwk *cwp1, colliwk *cwp2))

/*
 * Every shape pair of cwp1 x cwp2 (cwp1 any form, cwp2 forms 0, 1, 3, 4)
 * with the pair function of cwp2's form. Used for id 3 against ids 4/5
 * (func_8C03415E).
 *
 * NON_MATCHING: complete and faithful; 5 instructions differ. SHC keeps one
 * loop constant out of a register (R8-R14 are all taken): the original keeps
 * 0 in R8 (MOV #0,R8; MOV R8,R11 / MOV R8,R12 for the i and j inits) and
 * masks the form with AND #15,R0, this source keeps 15 in R8 (AND R8,R0) and
 * initialises i and j with MOV #0. Everything else (registers, the info1/info2
 * and twp1/twp2 temporaries, the mid-function literal pool after case 1, the
 * "default: break" that gives EXTU.W R12,R3 in the inner loop test) is
 * identical. Tried without effect: switch/if chains, casts of the form value,
 * a form local, explicit == 0 / != 0 tests, nested ifs instead of continue,
 * declaration orders, register on params/locals, i = j = 0, ++i, goto loops,
 * "% 16", NULL written as 0, == FALSE, the 8C033800 shape (one if around the
 * switch), case orders, dead zero assignments (removed before allocation),
 * while/goto loops. Adding a real store of 0 inside the inner loop does hoist
 * 0 (so the original has some zero use the allocator counted that left no
 * code). func_8C033A10 (code_8C033800.c) shows the same shared 0 in R8.
 */
#ifdef NON_MATCHING
void func_8C033DAE(colliwk *cwp1, colliwk *cwp2)
{
    CCL_INFO *info1;
    CCL_INFO *info2;
    taskwk *twp1;
    taskwk *twp2;
    Uint16 i;
    Uint16 j;

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
    for (i = 0; i < cwp1->nbInfo; i++) {
        for (j = 0; j < cwp2->nbInfo; j++) {
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
            case 3:
                func_8C030F20(cwp1, cwp2);
                break;
            case 4:
                func_8C031D00(cwp1, cwp2);
                break;
            default:
                break;
            }
        }
    }
}
#else
ASM_STUB(void func_8C033DAE(colliwk *cwp1, colliwk *cwp2))
#endif

/*
 * Pair loops of the id 0 (players) and id 1 lists against the other lists.
 * The list ends are computed once per loop, as in the original. "register"
 * on the four pointers is needed:
 * without it SHC keeps five list addresses in R8-R12 and spills pend/qend.
 */
void func_8C033F00(void)
{
    register colliwk **p;
    register colliwk **pend;
    register colliwk **q;
    register colliwk **qend;

    pend = &B_8C7456E8[B_8C7456D8];
    for (p = B_8C7456E8; p < pend; p++) {
        for (q = p + 1; q < pend; q++) {
            func_8C0335D4(*p, *q);
        }
        qend = &B_8C745708[B_8C7456DA];
        for (q = B_8C745708; q < qend; q++) {
            func_8C0335D4(*p, *q);
        }
        qend = &B_8C745908[B_8C752BD8];
        for (q = B_8C745908; q < qend; q++) {
            func_8C033800(*p, *q);
        }
        qend = &B_8C745B08[B_8C752BDA];
        for (q = B_8C745B08; q < qend; q++) {
            func_8C033800(*p, *q);
        }
        qend = &B_8C745F08[B_8C7456DC];
        for (q = B_8C745F08; q < qend; q++) {
            func_8C033A10(*p, *q);
        }
        qend = &B_8C746708[B_8C7456DE];
        for (q = B_8C746708; q < qend; q++) {
            func_8C033A10(*p, *q);
        }
        qend = &B_8C746908[B_8C7456E0];
        for (q = B_8C746908; q < qend; q++) {
            func_8C0335D4(*p, *q);
        }
        qend = &B_8C746D08[B_8C7456E2];
        for (q = B_8C746D08; q < qend; q++) {
            func_8C0335D4(*p, *q);
        }
        qend = &B_8C747108[B_8C7456E4];
        for (q = B_8C747108; q < qend; q++) {
            func_8C033CDC(*p, *q);
        }
    }
    pend = &B_8C745708[B_8C7456DA];
    for (p = B_8C745708; p < pend; p++) {
        qend = &B_8C745908[B_8C752BD8];
        for (q = B_8C745908; q < qend; q++) {
            func_8C0335D4(*p, *q);
        }
        qend = &B_8C745B08[B_8C752BDA];
        for (q = B_8C745B08; q < qend; q++) {
            func_8C0335D4(*p, *q);
        }
        qend = &B_8C745F08[B_8C7456DC];
        for (q = B_8C745F08; q < qend; q++) {
            func_8C0335D4(*p, *q);
        }
        qend = &B_8C746708[B_8C7456DE];
        for (q = B_8C746708; q < qend; q++) {
            func_8C0335D4(*p, *q);
        }
        qend = &B_8C746908[B_8C7456E0];
        for (q = B_8C746908; q < qend; q++) {
            func_8C0335D4(*p, *q);
        }
        qend = &B_8C746D08[B_8C7456E2];
        for (q = B_8C746D08; q < qend; q++) {
            func_8C0335D4(*p, *q);
        }
    }
}

/*
 * id 3 list against itself, then against the id 4 and id 5 lists.
 */
void func_8C03415E(void)
{
    Uint16 i;
    Uint16 j;

    for (i = 0; i < B_8C752BDA - 1; i++) {
        for (j = i + 1; j < B_8C752BDA; j++) {
            func_8C0335D4(B_8C745B08[i], B_8C745B08[j]);
        }
    }
    for (i = 0; i < B_8C752BDA; i++) {
        for (j = 0; j < B_8C7456DC; j++) {
            func_8C033DAE(B_8C745B08[i], B_8C745F08[j]);
        }
    }
    for (i = 0; i < B_8C752BDA; i++) {
        for (j = 0; j < B_8C7456DE; j++) {
            func_8C033DAE(B_8C745B08[i], B_8C746708[j]);
        }
    }
}

/*
 * id 5 list against itself.
 */
void func_8C034250(void)
{
    Sint32 i;
    Sint32 j;

    for (i = 0; i < B_8C7456DE - 1; i++) {
        for (j = i + 1; j < B_8C7456DE; j++) {
            func_8C0335D4(B_8C746708[i], B_8C746708[j]);
        }
    }
}

/*
 * Per-frame CCL pass: unless D_8C19B468 is set, empty the player target
 * lists (count 0, first entry NULL) and run the pair loops; then reset the
 * per-id lists (func_8C02F81A, tail call).
 *
 * NON_MATCHING: complete and faithful; 2 instructions differ: the tail call
 * loads func_8C02F81A into R3 (original R2). Chained assignments
 * ("B_8C74770A = B_8C747708 = 0;" ...) move the tail call to R2 but always
 * disturb the order of the target-list stores (all 256 contiguous chain
 * groupings of the eight stores tried; best 2 instructions). Also tried:
 * other store orders (the scheduler keeps the source order), else branch
 * with its own tail call, empty then-branch, goto, 0 for NULL, "->twp",
 * an unprototyped callee. The body temporaries rotate R3, R1, R2 exactly as
 * in the original; only the tail-block temporary differs (6 stores give R2,
 * 7-9 give R3).
 */
#ifdef NON_MATCHING
void func_8C0342E0(void)
{
    if (D_8C19B468 == 0) {
        B_8C747708 = 0;
        B_8C74770A = 0;
        B_8C74770C = 0;
        B_8C74770E = 0;
        B_8C73EE38[0].twp = NULL;
        B_8C741280[0].twp = NULL;
        B_8C742288[0].twp = NULL;
        B_8C7446D0[0].twp = NULL;
        func_8C033F00();
        func_8C03415E();
        func_8C034250();
    }
    func_8C02F81A();
}
#else
ASM_STUB(void func_8C0342E0(void))
#endif

/* same chunk, still assembly (BSR targets) */
ASM_STUB(Float func_8C033800(colliwk *cwp1, colliwk *cwp2))
ASM_STUB(void func_8C033A10(colliwk *cwp1, colliwk *cwp2))
ASM_STUB(void func_8C033CDC(colliwk *cwp1, colliwk *cwp2))
