/*
 * 1ST_READ 8C0EC0A8-8C0EC180 (chunk 8C0EC0A8; Big the Cat, fishing):
 *
 *  func_8C0EC0A8  updates the fishing line (called by the rod/lure task
 *                 func_8C0E943C with the work block AWP(tp)->work[2]):
 *                 pins pt[0] to the rod tip and pt[11] to the lure, then
 *                 runs one solver iteration over the inner points
 *                 pt[1..10]: the length-constraint pass func_8C0EC212
 *                 (with the two segment-length thresholds) and the second
 *                 pass func_8C0EC4E8.  mode 0 uses the thresholds {10, 20}
 *                 (slack line); mode 1 and any other value use {0, 0}
 *                 (every segment is pulled in).
 *
 * The solver loop "for (j = 0; j < 1; j++)" (iteration count 1, probably a
 * #define in the original) is required: SHC keeps the constant 1 in R11 and
 * emits a TST R11,R11 entry check before each unrolled inner loop.
 */
#include "common.h"
#include "sa1/sa1.h"

/* Fishing line work: same layout as FISHLINE in code_8C0EC180.c (point array
 * at +0x00; pt[0] is the rod tip, pt[11] the lure end). */
typedef struct {
    NJS_POINT3 pt[12];          /* 0x00 */
} FISHLINE;

/* Segment-length thresholds read by func_8C0EC212 (squared distance of
 * pt[i] - pt[i-1] is compared with hi * hi first, then with lo * lo).
 * Must be a struct, not Float[2]: an array makes SHC keep &lim in a
 * callee-saved register instead of using R15 directly. */
typedef struct {
    Float lo;                   /* 0x00 */
    Float hi;                   /* 0x04 */
} FISHLINE_LIM;

ASM_STUB(void func_8C0EC212(FISHLINE *lw, FISHLINE_LIM *lim, Sint32 i))
ASM_STUB(void func_8C0EC4E8(FISHLINE *lw, Sint32 i))

void func_8C0EC0A8(FISHLINE *lw, NJS_POINT3 *a, NJS_POINT3 *b, Sint32 mode)
{
    FISHLINE_LIM lim;
    Sint32 i;
    Sint32 j;

    switch (mode) {
    case 0:
        lim.lo = 10.0f;
        lim.hi = 20.0f;
        break;
    case 1:
    default:
        lim.lo = 0.0f;
        lim.hi = 0.0f;
        break;
    }
    lw->pt[0].x = a->x;
    lw->pt[0].y = a->y;
    lw->pt[0].z = a->z;
    lw->pt[11].x = b->x;
    lw->pt[11].y = b->y;
    lw->pt[11].z = b->z;
    for (j = 0; j < 1; j++) {
        for (i = 1; i < 11; i++) {
            func_8C0EC212(lw, &lim, i);
        }
        for (i = 1; i < 11; i++) {
            func_8C0EC4E8(lw, i);
        }
    }
}
