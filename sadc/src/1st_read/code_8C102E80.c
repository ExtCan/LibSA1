/*
 * func_8C102E80 (8C102E80): clears the B_8C7A52D8 record (flag = 0,
 * vector = 0,0,0). Callers: func_8C10313C, func_8C1032AC.
 *
 * B_8C7A52D8 is a 0x10-byte record: Sint32 flag at +0x00 (set to 1 by
 * func_8C102E96) followed by a float vector at +0x04, which other code
 * addresses through the separate symbol B_8C7A52DC (func_8C102E9E copies
 * a 12-byte vector into it). The matched code reaches all four words
 * from one base register, so it is written as one record here.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/* File-local view of B_8C7A52D8 (+0x04 is also the symbol B_8C7A52DC). */
typedef struct {
    Sint32 flag;                /* 0x00 */
    NJS_POINT3 vec;             /* 0x04 (B_8C7A52DC) */
} struct_8C7A52D8;

extern struct_8C7A52D8 B_8C7A52D8;

void func_8C102E80(void)
{
    struct_8C7A52D8 *wk;

    wk = &B_8C7A52D8;
    wk->flag = 0;
    wk->vec.x = 0.0f;
    wk->vec.y = 0.0f;
    wk->vec.z = 0.0f;
}
