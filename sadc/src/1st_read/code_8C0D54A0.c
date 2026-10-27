#include "sa1/sa1.h"

/*
 * E-102 Gamma line draw lists (1ST_READ chunk 8C0D54A0):
 *   func_8C0D54A0  build p3c of one line list: MAlloc 2*num points/colours
 *                  and gather them from src_p/src_col through idx
 *   func_8C0D5524  free p3c.p and p3c.col of one line list
 *   func_8C0D553A  draw one line list with njDrawLine3D (task disp 8C0D5600..)
 *   func_8C0D5552  init both E-102 line lists from the templates
 *                  D_8C42311C / D_8C42313C (E102Main, code_8C0CBE00.c)
 *   func_8C0D55A6  free both line lists (E-102 destructor 8C0CFA6E)
 */

/*
 * E-102 line draw list (0x20 bytes; local copy, same as code_8C0CFA6E.c,
 * code_8C0D24E0.c; shared type still to be defined).
 */
typedef struct {
    NJS_POINT3 *src_p;          /* 0x00 source points (12-byte stride) */
    NJS_COLOR *src_col;         /* 0x04 source colours (4-byte stride) */
    Sint16 *idx;                /* 0x08 index list (MOV.W @R11+) */
    Sint16 num;                 /* 0x0C index count / 2 */
    Sint16 pad0E;               /* 0x0E */
    NJS_POINT3COL p3c;          /* 0x10 p/col MAlloc'd, tex = NULL, num = 2 * num */
} E102_LINE;                    /* 0x20 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_OFFSET(E102_LINE, idx, 0x08);
SA1_ASSERT_OFFSET(E102_LINE, num, 0x0C);
SA1_ASSERT_OFFSET(E102_LINE, p3c, 0x10);

extern E102_LINE D_8C42311C;    /* template of line list 0 */
extern E102_LINE D_8C42313C;    /* template of line list 1 */

void func_8C0D54A0(E102_LINE *l)
{
    NJS_POINT3COL *p3c;
    NJS_POINT3 *src_p;
    NJS_COLOR *src_col;
    Sint16 *idx;
    Sint32 n;
    NJS_POINT3 *p;
    NJS_COLOR *col;
    Sint32 i;
    Sint32 k;

    p3c = &l->p3c;
    idx = l->idx;
    src_p = l->src_p;
    src_col = l->src_col;
    n = l->num * 2;
    p = p3c->p = MAlloc(n * sizeof(NJS_POINT3));
    col = p3c->col = MAlloc(n * sizeof(NJS_COLOR));
    p3c->tex = NULL;
    p3c->num = n;
    for (i = 0; i < n; i++) {
        k = *idx++;
        *p++ = src_p[k];
        *col++ = src_col[k];
    }
}

void func_8C0D5524(E102_LINE *l)
{
    MFree(l->p3c.p);
    MFree(l->p3c.col);
}

void func_8C0D553A(E102_LINE *l, Uint32 attr)
{
    njDrawLine3D(&l->p3c, l->num, attr);
}

void func_8C0D5552(E102_LINE *a, E102_LINE *b)
{
    *a = D_8C42311C;
    func_8C0D54A0(a);
    *b = D_8C42313C;
    func_8C0D54A0(b);
}

void func_8C0D55A6(E102_LINE *a, E102_LINE *b)
{
    E102_LINE *l;

    l = a;
    MFree(l->p3c.p);
    MFree(l->p3c.col);
    l = b;
    MFree(l->p3c.p);
    MFree(l->p3c.col);
}
