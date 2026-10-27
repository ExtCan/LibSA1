#include "common.h"
#include "ninja.h"
#include "task.h"

/* Ninja ARGB float color (njSetConstantMaterial argument) */
typedef struct {
    Float a;
    Float r;
    Float g;
    Float b;
} NJS_ARGB;

/* Ninja colored 3D point list (njDrawLine3D argument) */
typedef struct {
    NJS_POINT3 *p;
    NJS_COLOR *col;
    NJS_TEX *tex;
    Uint32 num;
} NJS_POINT3COL;

extern void njSetConstantMaterial(NJS_ARGB *argb);
extern void njDrawLine3D(NJS_POINT3COL *p, Sint32 n, Uint32 attr);
extern Sint32 func_8C07CE40(Uint8 pno);    /* canonical: sa1/protos.h */
extern Sint32 func_8C07CD16(NJS_POINT3 *pos, Float r);

void func_8C043A20(Float a, Float r, Float g, Float b)
{
    NJS_ARGB argb;

    argb.a = a;
    argb.r = r;
    argb.g = g;
    argb.b = b;
    njSetConstantMaterial(&argb);
}

void func_8C043A40(Float x1, Float y1, Float z1, Float x2, Float y2, Float z2)
{
    NJS_POINT3COL p3c;
    NJS_POINT3 pos[2];
    NJS_COLOR col[2];

    pos[0].x = x1;
    pos[0].y = y1;
    pos[0].z = z1;
    pos[1].x = x2;
    pos[1].y = y2;
    pos[1].z = z2;
    col[0].argb.a = 0xFF;
    col[0].argb.r = 0xFF;
    col[0].argb.g = 0xFF;
    col[0].argb.b = 0xFF;
    col[1].argb.a = 0xFF;
    col[1].argb.r = 0x88;
    col[1].argb.g = 0x88;
    col[1].argb.b = 0xBB;
    p3c.p = pos;
    p3c.col = col;
    p3c.tex = 0;
    p3c.num = 2;
    njDrawLine3D(&p3c, 1, 0);
}

void func_8C043AA4(Float x1, Float y1, Float z1, Float x2, Float y2, Float z2,
                   NJS_COLOR *col1, NJS_COLOR *col2)
{
    NJS_POINT3COL p3c;
    NJS_POINT3 pos[2];
    NJS_COLOR col[2];

    pos[0].x = x1;
    pos[0].y = y1;
    pos[0].z = z1;
    pos[1].x = x2;
    pos[1].y = y2;
    pos[1].z = z2;
    col[0].color = col1->color;
    col[1].color = col2->color;
    p3c.p = pos;
    p3c.col = col;
    p3c.tex = 0;
    p3c.num = 2;
    njDrawLine3D(&p3c, 1, 0);
}

void func_8C043AEE(NJS_POINT3 *v1, NJS_POINT3 *v2)
{
    NJS_POINT3COL p3c;
    NJS_POINT3 pos[2];
    NJS_COLOR col[2];

    pos[0].x = v1->x;
    pos[0].y = v1->y;
    pos[0].z = v1->z;
    pos[1].x = v2->x;
    pos[1].y = v2->y;
    pos[1].z = v2->z;
    col[0].argb.a = 0xFF;
    col[0].argb.r = 0xFF;
    col[0].argb.g = 0xFF;
    col[0].argb.b = 0xFF;
    col[1].argb.a = 0xFF;
    col[1].argb.r = 0x88;
    col[1].argb.g = 0x88;
    col[1].argb.b = 0xBB;
    p3c.p = pos;
    p3c.col = col;
    p3c.tex = 0;
    p3c.num = 2;
    njDrawLine3D(&p3c, 1, 0);
}

void func_8C043B60(taskwk *twp, Sint16 mask)
{
    NJS_ARGB argb;
    Sint16 bit;

    bit = 1 << func_8C07CE40(0);
    if (mask & bit) {
        if (func_8C07CD16(&twp->pos, 20.0f)) {
            argb.a = 0.0f;
            argb.r = 0.5f;
            argb.g = 0.5f;
            argb.b = 0.5f;
        } else {
            argb.a = 0.0f;
            argb.r = 0.0f;
            argb.g = 0.0f;
            argb.b = 0.0f;
        }
        njSetConstantMaterial(&argb);
    }
}
