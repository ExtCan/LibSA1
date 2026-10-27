#include "common.h"
#include "ninja.h"
#include "sa1/ninja_types.h"
#include "sa1/ninja_funcs.h"

/* Number display parameters (passed by pointer; callers build it on the
 * stack or keep it in object data). No pointers: same layout on every host. */
typedef struct {
    Float x, y;                 /* 0x00 screen position of the first digit */
    Float sx, sy;               /* 0x08 sprite scale */
    Float dx, dy;               /* 0x10 step between two digits */
} NUMDISP;                      /* 0x18 */

SA1_ASSERT_SIZE(NUMDISP, 0x18);

extern NJS_TEXLIST D_8C1BF19C;  /* digit texture list */
extern NJS_TEXANIM D_8C19EF64[]; /* cells 0-9 digits, 10 separator */

/* Draws value (clamped to 0..99999 and to 10^(digits+1)-1) as `digits`
 * sprite digits, most significant first, starting at disp->x/y. */
void func_8C057780(NUMDISP *disp, Sint32 value, Sint32 digits)
{
    NUMDISP *nd;
    Sint32 num;
    Sint32 ndig;
    NJS_SPRITE sp;
    Sint32 i;
    Sint32 max;
    volatile Sint32 n;          /* lives on the stack in the original code */

    nd = disp;
    ndig = digits;
    num = value;
    njSetTexture(&D_8C1BF19C);
    sp.tlist = &D_8C1BF19C;
    sp.tanim = D_8C19EF64;
    sp.sx = nd->sx;
    sp.sy = nd->sy;
    sp.ang = 0;
    sp.p.x = nd->x;
    sp.p.y = nd->y;
    if (num >= 100000) {
        num = 99999;
    }
    if (num <= 0) {
        num = 0;
    }
    /* starts at 10, so the limit is one digit larger than ndig (as in the
     * original) */
    max = 10;
    for (i = 0; i < ndig; i++) {
        max *= 10;
    }
    max--;
    if (num > max) {
        num = max;
    }
    if (ndig >= 5) {
        n = num / 10000;
        njDrawSprite2D(&sp, n, -10.0f, 0x20);
        sp.p.x += nd->dx;
        sp.p.y += nd->dy;
    }
    if (ndig >= 4) {
        n = num % 10000 / 1000;
        njDrawSprite2D(&sp, n, -10.0009997f, 0x20);
        sp.p.x += nd->dx;
        sp.p.y += nd->dy;
    }
    if (ndig >= 3) {
        n = num % 1000 / 100;
        njDrawSprite2D(&sp, n, -10.002f, 0x20);
        sp.p.x += nd->dx;
        sp.p.y += nd->dy;
    }
    if (ndig >= 2) {
        n = num % 100 / 10;
        njDrawSprite2D(&sp, n, -10.0029995f, 0x20);
        sp.p.x += nd->dx;
        sp.p.y += nd->dy;
    }
    if (ndig >= 1) {
        n = num % 10;
        njDrawSprite2D(&sp, n, -10.004f, 0x20);
    }
}

/* Draws "aa:bb" (two 2-digit numbers separated by texanim cell 10), e.g. a
 * minutes:seconds time. */
void func_8C057934(NUMDISP *nd, Sint32 a, Sint32 b)
{
    NUMDISP pos;
    NJS_SPRITE sp;

    pos = *nd;
    func_8C057780(&pos, a, 2);
    /* FMAC site (dx * 2 + x, fused on PC) */
    SA1_FMAC_ACC(pos.x, nd->dx, 2.0f);
    njSetTexture(&D_8C1BF19C);
    sp.tlist = &D_8C1BF19C;
    sp.tanim = D_8C19EF64;
    sp.sx = pos.sx;
    sp.sy = pos.sy;
    sp.ang = 0;
    sp.p.x = pos.x;
    sp.p.y = pos.y;
    njDrawSprite2D(&sp, 10, -10.0f, 0x20);
    pos.x += nd->dx;
    func_8C057780(&pos, b, 2);
}
