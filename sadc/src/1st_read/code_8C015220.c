#include "common.h"
#include "ninja.h"

/* Ninja ARGB float color (njSetConstantMaterial argument) */
typedef struct {
    Float a;
    Float r;
    Float g;
    Float b;
} NJS_ARGB;

/* 28-byte sprite descriptor table (see code_8C014E42.c) */
typedef struct {
    Sint32 w;
    Sint32 h;
    Uint32 col[4];
    Sint32 tex;
} SPRITE_DEF_t;

extern void njSetConstantMaterial(NJS_ARGB *argb);

extern SPRITE_DEF_t D_8C1621E8[];
extern NJS_COLOR D_8C162370[2];
extern NJS_POINT3 D_8C162380;
extern NJS_POINT2 D_8C16238C;
extern NJS_ARGB D_8C1115E8;
extern NJS_ARGB D_8C1115F8;

ASM_STUB(void func_8C014E42(Sint32 idx, NJS_POINT3 *pos, NJS_POINT2 *scale))

#ifdef NON_MATCHING
/*
 * Menu background: 16x12 grid of sprites with a vertical color gradient
 * between D_8C162370[0] and D_8C162370[1]; the four corner tiles use
 * sprite 7 (mirrored with negative scale), the rest sprite 8.
 * Control flow, loops (counters kept in memory), color math and register
 * roles are close to the original, but the original hoists each of the
 * 20 color reads in the inner loop as a separate &col[k] temporary (all
 * spilled to @(0..76,R15), locals start at @80). SHC here shares the
 * address within each branch (10 temporaries), which shifts the frame.
 */
#define LERP8(c0, c1, t) \
    ((Uint8)SA1_FTOI(SA1_FMAC((Float)((c1) - (c0)), (t), (Float)(c0))))

void func_8C015220(void)
{
    struct {
        Sint16 x;
        Sint16 y;
    } p;
    NJS_COLOR col[4];
    NJS_POINT3 pos;
    NJS_POINT2 scale;
    Float t;

    for (p.y = 0; p.y < 12; p.y++) {
        /* 0x3DAAAAAA (1/12 truncated); the difference of the two colors is
         * an integer, then one FMAC c0 + diff * t and FTRC */
        t = p.y * 0.08333333f;
        col[0].argb.a = LERP8(D_8C162370[0].argb.a, D_8C162370[1].argb.a, t);
        col[0].argb.r = LERP8(D_8C162370[0].argb.r, D_8C162370[1].argb.r, t);
        col[0].argb.g = LERP8(D_8C162370[0].argb.g, D_8C162370[1].argb.g, t);
        col[0].argb.b = LERP8(D_8C162370[0].argb.b, D_8C162370[1].argb.b, t);
        t = (p.y + 1) * 0.08333333f;
        col[1].argb.a = LERP8(D_8C162370[0].argb.a, D_8C162370[1].argb.a, t);
        col[1].argb.r = LERP8(D_8C162370[0].argb.r, D_8C162370[1].argb.r, t);
        col[1].argb.g = LERP8(D_8C162370[0].argb.g, D_8C162370[1].argb.g, t);
        col[1].argb.b = LERP8(D_8C162370[0].argb.b, D_8C162370[1].argb.b, t);
        p.x = 0;
        do {
            pos.x = (p.x - 8) * 16 + 320;
            pos.y = (p.y - 6) * 16 + 240;
            pos.z = 10.0f;
            if (p.x == 0 && p.y == 0) {
                scale.x = 1.0f;
                scale.y = 1.0f;
                D_8C1621E8[7].col[0] = col[0].color;
                D_8C1621E8[7].col[1] = col[1].color;
                D_8C1621E8[7].col[2] = col[0].color;
                D_8C1621E8[7].col[3] = col[1].color;
                func_8C014E42(7, &pos, &scale);
            } else if (p.x == 0 && p.y == 11) {
                scale.x = 1.0f;
                scale.y = -1.0f;
                D_8C1621E8[7].col[0] = col[1].color;
                D_8C1621E8[7].col[1] = col[0].color;
                D_8C1621E8[7].col[2] = col[1].color;
                D_8C1621E8[7].col[3] = col[0].color;
                func_8C014E42(7, &pos, &scale);
            } else if (p.x == 15 && p.y == 0) {
                scale.x = -1.0f;
                scale.y = 1.0f;
                D_8C1621E8[7].col[0] = col[0].color;
                D_8C1621E8[7].col[1] = col[1].color;
                D_8C1621E8[7].col[2] = col[0].color;
                D_8C1621E8[7].col[3] = col[1].color;
                func_8C014E42(7, &pos, &scale);
            } else if (p.x == 15 && p.y == 11) {
                scale.x = -1.0f;
                scale.y = -1.0f;
                D_8C1621E8[7].col[0] = col[1].color;
                D_8C1621E8[7].col[1] = col[0].color;
                D_8C1621E8[7].col[2] = col[1].color;
                D_8C1621E8[7].col[3] = col[0].color;
                func_8C014E42(7, &pos, &scale);
            } else {
                scale.x = 1.0f;
                scale.y = 1.0f;
                D_8C1621E8[8].col[0] = col[0].color;
                D_8C1621E8[8].col[1] = col[1].color;
                D_8C1621E8[8].col[2] = col[0].color;
                D_8C1621E8[8].col[3] = col[1].color;
                func_8C014E42(8, &pos, &scale);
            }
            p.x++;
        } while (p.x < 16);
    }
}

#undef LERP8

#endif

#ifdef NON_MATCHING
void func_8C015566(void)
{
    NJS_ARGB argb;

    argb = D_8C1115E8;
    njSetConstantMaterial(&argb);
    func_8C014E42(3, &D_8C162380, &D_8C16238C);
}
#else
ASM_STUB(void func_8C015566(void))
#endif

void func_8C015594(Sint32 n, Sint32 x, Sint32 sel, Float f)
{
    NJS_POINT3 pos;
    NJS_POINT2 scale;
    NJS_ARGB argb;

    if (sel) {
        scale.x = 1.5f;
        scale.y = 1.5f;
    } else {
        scale.x = 1.25f;
        scale.y = 1.0f;
    }
    argb = D_8C1115F8;
    njSetConstantMaterial(&argb);
    pos.x = 320.0f;
    pos.y = (Float)(x + 240) + 8.0f;
    pos.z = 12.0f;
    func_8C014E42(1, &pos, &scale);
    pos.x = f + 312.0f;
    pos.y = (Float)(x + 240) + f;
    pos.z = 13.0f;
    func_8C014E42(0, &pos, &scale);
    if (sel) {
        scale.x = 1.5f;
        scale.y = 1.5f;
    } else {
        scale.x = 1.0f;
        scale.y = 1.0f;
    }
    pos.x = f + 312.0f;
    pos.y = (Float)(x + 240) + f + 2.0f;
    pos.z = 14.0f;
    func_8C014E42(n, &pos, &scale);
    if (sel) {
        pos.x = f + 312.0f;
        pos.y = (Float)(x + 240) + f;
        pos.z = 15.0f;
        func_8C014E42(2, &pos, &scale);
    }
}
