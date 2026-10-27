/*
 * 1ST_READ 8C08EE98-8C08F152 (chunk 8C08E1E0; HUD number draw):
 *
 *  func_8C08EE98  draws a number with the digit sprite D_8C1BF4E0 (its
 *                 tlist / tanim as set by the caller): one njDrawSprite2D
 *                 per digit, least significant first, stepping left by
 *                 16 * sx. Request (HUDNUM):
 *                 flags 0x01 scale (sx = sy = scale, else 1), 0x08 width
 *                 from max (and value clamped to max), 0x20 draw the colon
 *                 cell 10 in every position, 0x40 leading zeros;
 *                 unk1 bits 0-2: priority -1.1 - 0.1 * n; vertical anchor
 *                 0x10 top / 0x20 bottom / centre; horizontal 0x80 x is
 *                 the first digit / 0x40 one digit / centre;
 *                 colour ARGB8888 through 8C043A20 (attr 0x22: alpha +
 *                 colour). Skipped on logic step != 0 (B_8C754EC8).
 *                 Users: the results / time displays (8C08AC2C, 8C08AD04,
 *                 8C08C858..), Big's HUD (8C0E74FC) and line readout
 *                 (8C0E746C), the item box (8C07BDA0), Zero (8C0EBAB2).
 */
#include "common.h"
#include "sa1/sa1.h"

/* The request: sa1_NUMDISP (sa1/engine.h) plus the colour word at +0x1C
 * that this function reads (the header's struct stops at 0x1C; once it has
 * the colour, this file uses NUMDISP from sa1/pending.h). */
typedef struct {
    Uint8 flags;                /* 0x00 1 scale, 8 width from max, 0x20 colon cell, 0x40 leading zeros */
    Uint8 unk1;                 /* 0x01 bits 0-2 priority step, 0x10/0x20 vertical, 0x40/0x80 horizontal anchor */
    Uint16 unk2;                /* 0x02 */
    Uint32 max;                 /* 0x04 */
    Uint32 value;               /* 0x08 */
    Float x;                    /* 0x0C */
    Float y;                    /* 0x10 */
    Float unk14;                /* 0x14 */
    Float scale;                /* 0x18 */
    Uint32 color;               /* 0x1C ARGB8888 */
} HUDNUM;                       /* 0x20 */
SA1_ASSERT_SIZE(HUDNUM, 0x20);
SA1_ASSERT_OFFSET(HUDNUM, value, 0x08);
SA1_ASSERT_OFFSET(HUDNUM, color, 0x1C);

extern NJS_SPRITE D_8C1BF4E0;           /* the HUD's digit sprite (code_8C08A198.c) */
extern void func_8C605480(Sint32 a);    /* Kamui draw state (0 before, 1 after 2D draws) */

#ifdef NON_MATCHING
/* Complete and faithful; register allocation still differs from the original
 * (nd and value swapped between R11 and R12, so the digit lands in R5 instead
 * of R11, and "flags & 8" is not kept in R4). Closest form found (55 of 280
 * instructions differ); functionally verified with the harness (diff --warm). */
void func_8C08EE98(HUDNUM *nd)
{
    Uint8 flags = nd->flags;
    Sint32 align = nd->unk1;
    NJS_SPRITE *sp = &D_8C1BF4E0;
    Uint32 value;
    Uint32 max;
    Sint32 t;
    Sint32 n;
    Sint32 draw;
    Sint32 digit;
    Uint32 c;
    Float pri;
    Float ox;
    Float oy;
    Float a;
    Float r;
    Float g;
    Float b;

    if (B_8C754EC8 != 0) {
        return;
    }
    /* flag 8: the width comes from max, and the value is clamped to it */
    value = nd->value;
    if (flags & 8) {
        max = nd->max;
        if (value > max) {
            value = max;
        }
    }
    n = 1;
    if (flags & 8) {
        t = nd->max;
    } else {
        t = nd->value;
    }
    while (t >= 10) {           /* signed (CMP/GE, __divls) */
        t /= 10;
        n++;
    }
    if (flags & 1) {
        sp->sx = sp->sy = nd->scale;
    } else {
        sp->sx = sp->sy = 1.0f;
    }
    if (sp->sx > 1.0f) {
        func_8C605480(1);
    } else {
        func_8C605480(0);
    }
    pri = -1.09999993f - (Float)(align & 7) * 0.099999996f;
    if (align & 0x10) {
        oy = 0.0f;
    } else if (align & 0x20) {
        oy = -(sp->sy * 16.0f);
    } else {
        oy = -(sp->sy * 8.0f);
    }
    if (align & 0x80) {
        ox = sp->sx * (Float)(n - 1) * 16.0f;
    } else if (align & 0x40) {
        ox = sp->sx * 16.0f;
    } else {
        ox = sp->sx * (Float)(n - 1) * 8.0f;
    }
    c = nd->color;
    b = (Float)(c & 0xFF) * 0.0039215685f;
    c >>= 8;
    g = (Float)(c & 0xFF) * 0.0039215685f;
    c >>= 8;
    r = (Float)(c & 0xFF) * 0.0039215685f;
    c >>= 8;
    a = (Float)(c & 0xFF) * 0.0039215685f;
    func_8C043A20(a, r, g, b);
    sp->p.x = nd->x + ox;
    sp->p.y = nd->y + oy;
    draw = 1;
    for (; n > 0; n--) {
        if (flags & 0x20) {
            digit = 10;         /* the colon cell */
        } else {
            digit = value % 10;
            value /= 10;
        }
        if (draw == 1) {
            njDrawSprite2D(sp, digit, pri, 0x22);
        }
        /* without leading zeros, nothing more is drawn once the rest is 0 */
        if ((flags & 0x40) || value != 0) {
            draw = 1;
        } else {
            draw = 0;
        }
        sp->p.x -= sp->sx * 16.0f;
    }
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
    func_8C605480(1);
}
#else
ASM_STUB(void func_8C08EE98(HUDNUM *nd))
#endif
