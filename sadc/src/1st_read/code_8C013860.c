#include "common.h"
#include "ninja.h"

/* Palette editor debug screen: draws the panels, the selected colour's
 * R/G/B channel bars and the 512-entry gradient of the current palette. */

typedef struct {
    NJS_POINT2 *p;
    Uint32 *col;
    NJS_TEX *tex;
    Uint32 num;
} NJS_POINT2COL;

extern Sint32 B_8C6B6874;
extern Sint32 D_8C161CCC; /* palette number */
extern Sint32 D_8C161CD0;
extern Sint32 D_8C161CD4; /* 0: base, 1: spec */
extern Sint32 D_8C161CD8;
extern Sint32 D_8C161CDC;
extern Sint32 D_8C038F60[][512];
extern char D_8C111570[]; /* "R:" */
extern char D_8C111574[]; /* "G:" */
extern char D_8C111578[]; /* "B:" */
extern char D_8C11157C[]; /* "SPEC" */
extern char D_8C111584[]; /* "BASE" */
extern char D_8C11158C[];
extern char D_8C111590[]; /* "LINIER" */

/* njDrawPolygon2D-like */
extern void func_8C63EBC8(NJS_POINT2COL *p, Sint32 n, Float pri, Uint32 attr);
/* njPrint-like: loc = (column << 16) | row */
extern void njPrintC(Sint32 loc, char *str);
/* njPrintD-like: prints val with the given number of digits */
extern void func_8C640DE2(Sint32 loc, Sint32 val, Sint32 digits);

/* draws one channel bar (shift 16/8/0 = R/G/B) of palette entry idx */
void func_8C013F20(Sint32 idx, Sint32 shift, Uint32 *col);
/* draws a gouraud quad (x0,y0)-(x1,y1) with corner colours col[0..3]
 * (parameter order chosen so the callers evaluate col first) */
void func_8C014084(Float x0, Float y0, Float x1, Float y1, Uint32 *col);

void func_8C013860(void) {
    Uint32 col[4];
    NJS_POINT2COL pc;
    NJS_POINT2 pts[4];
    Uint32 cols[4];
    NJS_TEX tex[4];
    Sint32 c;
    Sint32 b;
    Sint32 g;
    Sint32 r;
    Sint32 i;

    col[0] = 0xFF808080;
    col[1] = 0xFF202020;
    col[2] = 0xFFC0C0C0;
    col[3] = 0xFF404040;
    func_8C014084(110.0f, 450.0f, 626.0f, 368.0f, col);
    col[1] = 0xFF808080;
    col[0] = 0xFF202020;
    col[3] = 0xFFC0C0C0;
    col[2] = 0xFF404040;
    func_8C014084(12.0f, 450.0f, 114.0f, 365.0f, col);
    if (B_8C6B6874) {
        col[1] = 0xFF808080;
        col[0] = 0xFF202020;
        col[3] = 0xFFC0C0C0;
        col[2] = 0xFF404040;
        func_8C014084(28.0f, 368.0f, 116.0f, 348.0f, col);
        col[0] = 0xFF000000;
        col[1] = 0xFF303030;
        col[2] = 0xFF000000;
        col[3] = 0xFF303030;
        func_8C014084(30.0f, 368.0f, 114.0f, 350.0f, col);
    }
    col[0] = 0xFF101010;
    col[1] = 0xFF101010;
    col[2] = 0xFF404040;
    col[3] = 0xFF404040;
    func_8C014084(112.0f, 448.0f, 624.0f, 382.0f, col);
    col[0] = 0xFF101010;
    col[1] = 0xFF101010;
    col[2] = 0xFF404040;
    col[3] = 0xFF404040;
    func_8C014084(112.0f, 380.0f, 624.0f, 370.0f, col);
    col[0] = 0xFF000000;
    col[1] = 0xFF303030;
    col[2] = 0xFF000000;
    col[3] = 0xFF303030;
    func_8C014084(14.0f, 448.0f, 112.0f, 367.0f, col);
    col[2] = 0xFF80FF80;
    col[3] = 0xFF80FF80;
    col[0] = 0xFF008000;
    col[1] = 0xFF008000;
    func_8C014084(D_8C161CDC * 2 + 112.0f, 448.0f, D_8C161CD0 * 2 + 113.0f, 384.0f, col);
    col[0] = 0xFF000000;
    col[1] = 0xFFFF0000;
    col[2] = 0xFF000000;
    col[3] = 0xFFFF0000;
    func_8C013F20(D_8C161CD4, 16, col);
    func_8C014084(16.0f, 382.0f, 110.0f, 397.0f, col);
    col[0] = 0xFF000000;
    col[1] = 0xFF00FF00;
    col[2] = 0xFF000000;
    col[3] = 0xFF00FF00;
    func_8C013F20(D_8C161CD4, 8, col);
    func_8C014084(16.0f, 398.0f, 110.0f, 413.0f, col);
    col[0] = 0xFF000000;
    col[1] = 0xFF0000FF;
    col[2] = 0xFF000000;
    col[3] = 0xFF0000FF;
    func_8C013F20(D_8C161CD4, 0, col);
    func_8C014084(16.0f, 414.0f, 110.0f, 429.0f, col);

    c = D_8C038F60[D_8C161CCC][D_8C161CD0 * 2 + D_8C161CD4];
    r = (c >> 16) & 0xFF;
    g = (c >> 8) & 0xFF;
    b = c & 0xFF;
    njPrintC(0x00030018, D_8C111570);
    func_8C640DE2(0x00050018, r, 2);
    njPrintC(0x00030019, D_8C111574);
    func_8C640DE2(0x00050019, g, 2);
    njPrintC(0x0003001A, D_8C111578);
    func_8C640DE2(0x0005001A, b, 2);
    if (D_8C161CD4) {
        njPrintC(0x0001001B, D_8C11157C);
    } else {
        njPrintC(0x0001001B, D_8C111584);
    }
    func_8C640DE2(0x0005001B, D_8C161CCC, 2);
    if (B_8C6B6874) {
        njPrintC(0x00040016, D_8C11158C);
        func_8C640DE2(0x00020016, D_8C161CDC, 2);
        func_8C640DE2(0x00050016, D_8C161CD0, 2);
    }
    /* switch: the test is CMP/EQ #0,R0, not TST */
    switch (D_8C161CD8) {
    case 0:
        njPrintC(0x00010017, D_8C111590);
        break;
    }

    pc.p = pts;
    pc.col = cols;
    pc.tex = tex;
    pc.num = 4;
    tex[0].u = 0;
    tex[0].v = 0;
    tex[1].u = 0;
    tex[1].v = 0;
    tex[2].u = 0;
    tex[2].v = 0;
    tex[3].u = 0;
    tex[3].v = 0;
    c = D_8C038F60[D_8C161CCC][D_8C161CD4];
    pts[0].x = 112.0f;
    pts[0].y = 382.0f;
    cols[0] = c;
    pts[1].x = 112.0f;
    pts[1].y = 370.0f;
    cols[1] = c;
    /* "i = i + 2" (not "i += 2") gives the original register choice */
    for (i = 2; i < 512; i = i + 2) {
        c = D_8C038F60[D_8C161CCC][D_8C161CD4 + i];
        pts[2].x = i + 112.0f;
        pts[2].y = 382.0f;
        cols[2] = c;
        pts[3].x = i + 112.0f;
        pts[3].y = 370.0f;
        cols[3] = c;
        func_8C63EBC8(&pc, 4, -1.0f, 0x20);
        pts[0].x = pts[2].x;
        pts[0].y = pts[2].y;
        cols[0] = cols[2];
        pts[1].x = pts[3].x;
        pts[1].y = pts[3].y;
        cols[1] = cols[3];
    }
}

ASM_STUB(void func_8C013F20(Sint32 idx, Sint32 shift, Uint32 *col))
ASM_STUB(void func_8C014084(Float x0, Float y0, Float x1, Float y1, Uint32 *col))
