#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C107744-8C1077A8 (chunk 8C107700, functions 2-4): management of
 * the single task kept in D_8C5FB8A4 (created by func_8C102EAE, which reads
 * a 0x1C-byte {type, pos, pos2} record; callers: Amy stage objects in
 * code_8C0DDFB4.c, func_8C0A2266, func_8C1032AC).
 *  func_8C107744  creates the task if there is none; 1 if created now.
 *  func_8C10776A  frees the task if there is one; 1 if freed.
 *  func_8C10778E  sets bit 0 of the task's twp->btimer; 1 if the task exists.
 */

/* Argument of func_8C107744/func_8C102EAE (0x1C bytes on the callers'
 * stack, see code_8C0DDFB4.c; func_8C102EAE switches on type 0/1/2 at +0
 * and reads floats at +0x04..+0x18). */
typedef struct {
    Sint32 type;                /* 0x00 */
    NJS_POINT3 pos;             /* 0x04 */
    NJS_POINT3 pos2;            /* 0x10 */
} UNK_8C107744_arg;             /* 0x1C */

SA1_ASSERT_SIZE(UNK_8C107744_arg, 0x1C);

extern task *D_8C5FB8A4;

extern task *func_8C102EAE(UNK_8C107744_arg *arg);

/* two early "return 0"s: "if (!D && (D = f()) != NULL) return 1;" puts
 * the return-1 block first */
Sint32 func_8C107744(UNK_8C107744_arg *arg)
{
    if (D_8C5FB8A4 != NULL) {
        return 0;
    }
    if ((D_8C5FB8A4 = func_8C102EAE(arg)) == NULL) {
        return 0;
    }
    return 1;
}

/* the if/else form gives the original R2/R3 choice (FreeTask in R2) */
Sint32 func_8C10776A(void)
{
    if (!D_8C5FB8A4) {
        return 0;
    } else {
        FreeTask(D_8C5FB8A4);
        D_8C5FB8A4 = NULL;
    }
    return 1;
}

Sint32 func_8C10778E(void)
{
    if (D_8C5FB8A4 == NULL) {
        return 0;
    }
    D_8C5FB8A4->twp->btimer |= 1;
    return 1;
}
