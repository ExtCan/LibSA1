/*
 * 1ST_READ 8C0EBFA8-8C0EC0A8 (chunk 8C0EBFA8; Big the Cat, fishing):
 *
 *  func_8C0EBFA8  fishing line reset (only caller: the lure exec
 *                 func_8C0E943C, mode 2 when the cast starts, with the any
 *                 work block work[2]): each of the 12 line points gets a
 *                 zero velocity and a zero force and is placed at a; the
 *                 step towards b is (b - a) * 0 (FLDI0 in the asm), so every
 *                 point ends up at a. The original probably meant "/ 11"
 *                 and lost it to an integer division 1 / 11.
 *                 NON_MATCHING: complete and functionally faithful (0 FMAC).
 *                 14 of 128 instructions differ, all one register choice:
 *                 the i * 12 index temp is R7 here, R1 in the original.
 *                 See the notes at the function.
 *
 * The TU starts at 8C0EBFA0 (32-byte aligned, zero fill before it): the
 * 8 bytes 8C0EBFA0-8C0EBFA7 (chunk 8C0EBFA0: R6 = R5 + 12, push R5, pop,
 * no RTS) fall through into this function and have no callers.
 */
#include "common.h"
#include "sa1/sa1.h"

/* Fishing line work (same block as FISHLINE in code_8C0EC180.c, which only
 * knows pt[]): the arrays at +0x90 and +0x120 are cleared here; the
 * relaxation code (func_8C0EC0A8 and its callees) presumably uses them as
 * per-point velocity and force. */
typedef struct {
    NJS_POINT3 pt[12];          /* 0x000 */
    NJS_POINT3 spd[12];         /* 0x090 */
    NJS_POINT3 acc[12];         /* 0x120 */
} FISHLINE;                     /* 0x1B0 */

SA1_ASSERT_OFFSET(FISHLINE, spd, 0x90);
SA1_ASSERT_OFFSET(FISHLINE, acc, 0x120);

/*
 * Matching notes (about 70 attempts):
 * - The original reaches d and p with @(R0,R15) at every access and reloads
 *   R0 each time. Only "volatile" locals give that: plain NJS_POINT3 locals
 *   (or Float arrays, a wrapper struct, NJS_POINT3[2]) make SHC keep &d and
 *   &p in registers, and volatile Float scalars get another stack layout.
 * - A for/while loop over 12 is unrolled 4x by -speed; a do-while loop (or
 *   a bound held in a variable) is not.
 * - "register" on i and on the bound n gives the original R5/R6. The index
 *   temp i * 12 then still takes R7 (an optimizer CSE variable); the
 *   original has it in R1, which SHC only does when R4-R7 are all live
 *   (checked with a 4-parameter test), yet the original never touches R7.
 *   Tried without effect: i++ in the if / in the while test / in both
 *   branches, continue, goto loop, Int/Uint32/Sint16 i, a block-local index
 *   copy, pointer arithmetic, Float[12][3] or flat Float[36] arrays,
 *   NJS_POINT3 (*)[12], a 4th parameter, extra register variables,
 *   volatile on the line, comma-joined stores, "i != n - 1".
 */
#ifdef NON_MATCHING
void func_8C0EBFA8(FISHLINE *lw, NJS_POINT3 *a, NJS_POINT3 *b)
{
    volatile NJS_POINT3 d;
    volatile NJS_POINT3 p;
    register Sint32 i;
    register Sint32 n;

    d.x = b->x - a->x;
    d.y = b->y - a->y;
    d.z = b->z - a->z;
    d.x *= 0.0f;
    d.y *= 0.0f;
    d.z *= 0.0f;
    p.x = a->x;
    p.y = a->y;
    p.z = a->z;
    n = 12;
    i = 0;
    do {
        lw->acc[i].x = 0.0f;
        lw->acc[i].y = 0.0f;
        lw->acc[i].z = 0.0f;
        lw->spd[i].x = 0.0f;
        lw->spd[i].y = 0.0f;
        lw->spd[i].z = 0.0f;
        lw->pt[i].x = p.x;
        lw->pt[i].y = p.y;
        lw->pt[i].z = p.z;
        if (i != 11) {
            p.x += d.x;
            p.y += d.y;
            p.z += d.z;
        }
        i++;
    } while (i < n);
}
#else
ASM_STUB(void func_8C0EBFA8(FISHLINE *lw, NJS_POINT3 *a, NJS_POINT3 *b))
#endif
