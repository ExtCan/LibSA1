#include "common.h"
#include "ninja.h"

/*
 * 8C02CFA4-8C02CFC0: hand-written SH-4 assembly (not SHC output).
 *
 * Part of the land collision list module 8C02CE80-8C02D54C (see
 * src/1st_read/code_8C02CEF4.c, code_8C02CF20.c and code_8C028E60.c):
 * - the loop count 128 is a literal-pool word (.DATA.L H'00000080); SHC
 *   always builds 128 as MOV #-128,Rn / EXTU.B Rn,Rn.
 * - zero, pointer and counter live in R0, R1 and R7; SHC allocates a leaf's
 *   temporaries from R4..R6 (it gives MOV #-128,R6 / MOV.L ..,R5 / MOV #0,R4)
 *   and schedules the stores after the DT.
 * The C below is the same algorithm; the original assembly stays in the
 * build (body only under NON_MATCHING, used by libsa1).
 *
 * Clears the 128 z buckets D_8C02D560 (a data block in the code section,
 * after the list heads D_8C02D540/44/48) of the land collision polygon
 * sorter. func_8C02CFC0 takes a polygon from the free list D_8C02D540 and
 * pushes it on bucket ((Sint32)z >> 4) + 64, clamped to 0..127; the tail
 * is set only when the bucket was empty.
 * Caller: func_8C028EC2 (code_8C028E60.c).
 */

struct colpoly;

/* one z bucket of polygons (8 bytes, D_8C02D560[128]) */
typedef struct colbucket {
    struct colpoly *head;       /* 0x00 first polygon (func_8C02CFC0 pushes here) */
    struct colpoly *tail;       /* 0x04 last polygon (set when the bucket was empty) */
} colbucket;

SA1_ASSERT_OFFSET(colbucket, tail, 0x04);

#define COLBUCKET_MAX 128

extern colbucket D_8C02D560[COLBUCKET_MAX];   /* data block in the code section */

#ifdef NON_MATCHING
void func_8C02CFA4(void)
{
    colbucket *b;
    Sint32 i;

    b = D_8C02D560;
    i = COLBUCKET_MAX;
    do {
        b->head = NULL;
        b->tail = NULL;
        b++;
    } while (--i != 0);
}
#endif
