#include "common.h"
#include "ninja.h"

typedef struct {
    NJS_POINT2 *p;
    NJS_COLOR *col;
    NJS_TEX *tex;
    Uint32 num;
} NJS_POINT2COL;

extern Sint32 D_8C161CCC;
extern Sint32 D_8C038F60[][512];

extern void func_8C63EBC8(NJS_POINT2COL *p, Sint32 n, Float pri, Uint32 attr);
extern void func_8C63E1D4(NJS_POINT2COL *p, Sint32 n, Float pri, Uint32 attr);

void func_8C01413A(Float x0, Float y0, Float x1, Float y1, NJS_COLOR *c);

#ifdef NON_MATCHING
/* Draws a line graph of one byte (selected by shift sh) of 256 entries of
 * D_8C038F60[D_8C161CCC], every 2nd entry from start, in colour c[1].
 *
 * Status: the register allocation is right (sh R8, start R9, table R10,
 * &D_8C161CCC R11, x0 IV R12, divisor 4 R13, x1 IV R14, 112.0f FR14, 448.0f
 * FR15; prev/cur spilled to the stack; 3x unrolled loop with the stack slots
 * rotating) only when cur is assigned inside the call argument, as below.
 * Remaining difference (each unrolled copy): the original computes the table
 * value in R4, stores it to cur, reloads it into R1 for __divls and builds
 * y1 as 448.0f - x (FMOV FR15,FR7; FSUB); this version computes it directly
 * in R1 and builds y1 as -x + 448.0f (FNEG; FADD).  With "cur = ...;" as a
 * separate statement the codegen of that part is right, but cur then wins a
 * callee-saved register over start/sh (spilled instead).  The x0/x1 induction
 * variables come from strength reduction of (i - 1) * 2 and i * 2. */
void func_8C013F20(Sint32 start, Sint32 sh, NJS_COLOR *c) {
    Sint32 i;
    Sint32 prev;
    Sint32 cur;

    prev = (Uint8)(D_8C038F60[D_8C161CCC][start] >> sh);
    for (i = 1; i < 256; i++) {
        func_8C01413A((i - 1) * 2 + 112.0f, 448.0f - prev / 4, i * 2 + 112.0f,
                      448.0f - (cur = (Uint8)(D_8C038F60[D_8C161CCC][start + i * 2] >> sh)) / 4, c);
        prev = cur;
    }
}
#else
ASM_STUB(void func_8C013F20(Sint32 start, Sint32 sh, NJS_COLOR *c))
#endif

/* Draws a gouraud-shaded rectangle (x0,y0)-(x1,y1) with one colour per corner. */
void func_8C014084(Float x0, Float y0, Float x1, Float y1, NJS_COLOR *c) {
    NJS_POINT2COL p2c;
    NJS_POINT2 pts[4];
    NJS_COLOR cols[4];
    NJS_TEX tex[4];

    p2c.p = pts;
    p2c.col = cols;
    p2c.tex = tex;
    p2c.num = 4;
    pts[0].x = x0;
    pts[0].y = y0;
    cols[0] = c[0];
    tex[0].u = 0;
    tex[0].v = 0;
    pts[1].x = x1;
    pts[1].y = y0;
    cols[1] = c[1];
    tex[1].u = 255;
    tex[1].v = 0;
    pts[2].x = x1;
    pts[2].y = y1;
    cols[2] = c[2];
    tex[2].u = 255;
    tex[2].v = 255;
    pts[3].x = x0;
    pts[3].y = y1;
    cols[3] = c[3];
    tex[3].u = 0;
    tex[3].v = 255;
    func_8C63EBC8(&p2c, 4, -1.0f, 0x20);
}

/* Draws a line (x0,y0)-(x1,y1) in colour c[1]. */
void func_8C01413A(Float x0, Float y0, Float x1, Float y1, NJS_COLOR *c) {
    NJS_POINT2COL p2c;
    NJS_POINT2 pts[2];
    NJS_COLOR cols[2];
    NJS_TEX tex[2];

    p2c.p = pts;
    p2c.col = cols;
    p2c.tex = tex;
    p2c.num = 4;
    pts[0].x = x0;
    pts[0].y = y0;
    cols[0] = c[1];
    tex[0].u = 0;
    tex[0].v = 0;
    pts[1].x = x1;
    pts[1].y = y1;
    cols[1] = c[1];
    tex[1].u = 255;
    tex[1].v = 255;
    func_8C63E1D4(&p2c, 1, -0.5f, 0x40);
}
