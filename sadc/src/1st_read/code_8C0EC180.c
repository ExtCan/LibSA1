/*
 * 1ST_READ 8C0EC180-8C0EC212 (chunk 8C0EC0A8; Big the Cat, fishing):
 *
 *  func_8C0EC180  draws the fishing line (called by the lure display
 *                 func_8C0E7720 / func_8C0E78C0 with the any work block
 *                 work[2]): a connected 3D line strip (njDrawLine3D,
 *                 11 segments, attr 0x42) through the 12 line points,
 *                 each vertex white with the alpha that func_8C0EC60C
 *                 computes from the point's squared distance to the
 *                 camera (255 - d2 / 4, clamped to [0x40, 0xE0]).
 */
#include "common.h"
#include "sa1/sa1.h"

/* Fishing line work (allocated by the lure code; func_8C0EC0A8 sets pt[0]
 * and pt[11] and relaxes pt[1..10] with func_8C0EC212/func_8C0EC4E8).
 * Only the point array at +0x00 is known; the block may be larger.
 * The struct form is required: p3c.p = lw->pt is a different expression
 * from lw, which makes SHC keep a separate copy (MOV R4,R3) for the store. */
typedef struct {
    NJS_POINT3 pt[12];          /* 0x00 */
} FISHLINE;

ASM_STUB(Sint32 func_8C0EC60C(FISHLINE *lw, Sint32 i))

void func_8C0EC180(FISHLINE *lw)
{
    NJS_POINT3COL p3c;
    NJS_COLOR col[12];
    Sint32 i;

    p3c.p = lw->pt;
    p3c.col = col;
    p3c.tex = NULL;
    p3c.num = 12;
    for (i = 0; i < 12; i++) {
        col[i].argb.a = func_8C0EC60C(lw, i);
        col[i].argb.r = 0xFF;
        col[i].argb.g = 0xFF;
        col[i].argb.b = 0xFF;
    }
    njDrawLine3D(&p3c, 11, 0x42);
}
