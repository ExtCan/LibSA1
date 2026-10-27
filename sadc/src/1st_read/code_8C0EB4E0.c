#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0EB4E0-8C0EB548 (chunk 8C0EB244-8C0EBFA0, function 8 of 25).
 *
 *  func_8C0EB4E0  (Big, called from func_8C0E943C) weighted random pick
 *                 from the lot table D_8C5661B4: r = rand() / 32768, then
 *                 the rates of entries 1, 2, ... are summed until the sum
 *                 reaches r. The chosen entry's callback (if any) is run
 *                 and its kind is returned (the caller passes a positive
 *                 kind to func_8C05E0F8).
 *
 * D_8C5661B4 (data 8C561194.src), 12-byte entries:
 *   [0] {0.0,  -1, NULL}            (never chosen: the loop starts at 1)
 *   [1] {0.1,   2, func_8C0EB548}
 *   [2] {0.05,  3, func_8C0EB55E}
 *   [3] {0.03,  4, func_8C0EB574}
 *   [4] {0.01,  6, func_8C0EB5AA}
 *   [5] {9.99, -1, NULL}            (catch-all: nothing)
 *   [6] {0, 0, NULL}
 */

/* file-local view of the D_8C5661B4 entries (no shared type yet) */
typedef struct {
    Float rate;             /* 0x00 probability weight */
    Sint32 kind;            /* 0x04 returned value (-1 = nothing) */
    void (*func)(void);     /* 0x08 optional callback, NULL = none */
} BIG_LOT;
SA1_ASSERT_SIZE(BIG_LOT, 0xC);

extern BIG_LOT D_8C5661B4[];

Sint32 func_8C0EB4E0(void)
{
    Sint32 i;
    Float sum;
    Float r;

    i = 0;
    sum = 0.0f;
    r = (Float)rand() * 3.0517578125e-05f;
    do {
        i++;
        sum += D_8C5661B4[i].rate;
    } while (sum < r);
    if (D_8C5661B4[i].func != NULL) {
        D_8C5661B4[i].func();
    }
    return D_8C5661B4[i].kind;
}
