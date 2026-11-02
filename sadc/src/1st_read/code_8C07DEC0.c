/*
 * 1ST_READ chunk 8C07DEC0-8C07F560 (part): the menus' 2D library
 * (staged for the character select front end, gsa1_090_charselect.md 2.7 /
 * CS3, agent dc-charsel-1a).
 *
 * A textured quad B_8C7987D8 (4 x NJS_TEXTURE_VTX: x, y, 1/pri, u, v, ARGB)
 * is filled and handed to njDrawTexture (8C63ED08) with the texture's global
 * index (GBIX) taken from the current texlist (D_8C673480):
 *
 *   func_8C07DEC0  reset: white vertices, default UVs, colour / flags
 *   func_8C07DF2A  the global colour B_8C79883C
 *   func_8C07DF30  one vertex colour (modulated by the global colour)
 *   func_8C07DF6E  four vertex colours (modulated)
 *   func_8C07DFDC  read back the four vertex colours
 *   func_8C07E040  set the global colour and modulate the current ones
 *   func_8C07E0A8  force the blend flag B_8C798838
 *   func_8C07E0AE  draw texture n at (x, y) in its own size
 *   func_8C07E480  draw texture n at (x, y) with a given width / height
 *   func_8C07E642  draw texture n at (x, y) scaled by (sx, sy)
 *   func_8C07EB60  draw a list of {n, dx, dy} at (x, y)
 *   func_8C07EBBA  the quad's UVs for a flip mode
 *   func_8C07EC90  the default blending (src 8, dst 6)
 *   func_8C07ECE0  colour lerp (component-wise, 8C0867F0 / 8C086824)
 *   func_8C07EF42  a texture's width and height
 *   func_8C07F18C  the menu camera task: view from the task's angles / position
 *   func_8C07F1D2  grey ramp into palette bank n
 *   func_8C07F21C  grey ramps into banks 0-7
 *   func_8C07F2A0  the window frame (ADV_WINDOW texlist D_8C1BE928)
 *   func_8C07F42A  the title bar (gradient polygon, njDrawPolygon 8C63EC58)
 *
 * The draw attribute word of 8C07E0AE / 8C07E480 / 8C07E642:
 *   bits 30-31 (0xC0000000) flip: 0x40000000 vertical, 0x80000000 horizontal
 *   bits 24-27 (0x0F000000) anchor of (x, y): 0 top left, 1 top centre,
 *              2 top right, 3 middle left, 4 centre, 5 middle right,
 *              6 bottom left, 7 bottom centre, 8 bottom right
 *   the rest   (0x30FFFFFF) the texture's index in the current texlist
 * Translucency: the quad blends when a vertex alpha is not 0xFF
 * (B_8C798838) or when the texture is ARGB1555 / ARGB4444 (pixel format
 * & 0x38000000 == 0 or 0x10000000).
 */
#include "sa1/sa1.h"

/* njDrawTexture vertex (Ninja NJS_TEXTURE_VTX) */
typedef struct {
    Float x;                    /* 0x00 */
    Float y;                    /* 0x04 */
    Float z;                    /* 0x08 1 / pri */
    Float u;                    /* 0x0C */
    Float v;                    /* 0x10 */
    Uint32 col;                 /* 0x14 ARGB8888 */
} NJS_TEXTURE_VTX;              /* 0x18 */

/* njDrawPolygon vertex (Ninja NJS_POLYGON_VTX) */
typedef struct {
    Float x;                    /* 0x00 */
    Float y;                    /* 0x04 */
    Float z;                    /* 0x08 1 / pri */
    Uint32 col;                 /* 0x0C ARGB8888 */
} NJS_POLYGON_VTX;              /* 0x10 */

/* Loaded texture (Ninja NJS_TEXMEMLIST; NJS_TEXNAME.texaddr points to it once
 * the texlist is loaded; same layout as code_8C0502D8.c) */
typedef struct {
    Uint32 Type;                /* 0x00 */
    Uint32 BitDepth;            /* 0x04 */
    Uint32 PixelFormat;         /* 0x08 bits 27-29: 0 ARGB1555, 1 RGB565, 2 ARGB4444, ... */
    Uint32 nWidth;              /* 0x0C */
    Uint32 nHeight;             /* 0x10 */
    Uint32 TextureSize;         /* 0x14 */
    Uint32 fSurfaceFlags;       /* 0x18 */
    Uint32 *pSurface;           /* 0x1C */
    Uint32 *pVirtual;           /* 0x20 */
    Uint32 *pPhysical;          /* 0x24 */
} NJS_TEXSURFACE;               /* 0x28 */

typedef struct {
    void *texaddr;              /* 0x00 */
    NJS_TEXSURFACE texsurface;  /* 0x04 */
} NJS_TEXINFO;                  /* 0x2C */

typedef struct {
    Uint32 globalIndex;         /* 0x00 GBIX */
    void *bank;                 /* 0x04 NJS_TEXPALETTE * */
    Uint32 tspparambuffer;      /* 0x08 */
    Uint32 texparambuffer;      /* 0x0C */
    Uint32 texaddr;             /* 0x10 */
    NJS_TEXINFO texinfo;        /* 0x14 */
    Uint16 count;               /* 0x40 */
    Uint16 dummy;               /* 0x42 */
} NJS_TEXMEMLIST;               /* 0x44 */

SA1_ASSERT_SIZE(NJS_TEXTURE_VTX, 0x18);
SA1_ASSERT_SIZE(NJS_POLYGON_VTX, 0x10);
SA1_ASSERT_SIZE(NJS_TEXSURFACE, 0x28);
SA1_ASSERT_SIZE(NJS_TEXINFO, 0x2C);
SA1_ASSERT_SIZE(NJS_TEXMEMLIST, 0x44);
SA1_ASSERT_OFFSET(NJS_TEXMEMLIST, texinfo.texsurface.PixelFormat, 0x20);
SA1_ASSERT_OFFSET(NJS_TEXMEMLIST, texinfo.texsurface.nWidth, 0x24);

/* Entry of the list drawn by 8C07EB60 (a count, then {n, dx, dy} 6 bytes each) */
typedef struct {
    Sint16 n;                   /* 0x00 texture (attribute word, low 16 bits) */
    Sint16 dx;                  /* 0x02 */
    Sint16 dy;                  /* 0x04 */
} MENU2D_PART;                  /* 0x06 */

typedef struct {
    Sint16 count;               /* 0x00 */
    MENU2D_PART part[1];        /* 0x02 count entries */
} MENU2D_LIST;

/* Ninja view (njInitView 8C6403B0 ... njSetView 8C640540). Only its size is
 * known (the camera task reserves 0x78 bytes); the layout is the SDK's
 * (UNVERIFIED). */
typedef struct {
    Float w[30];                /* 0x00 */
} MENU_VIEW;                    /* 0x78 */

extern NJS_TEXTURE_VTX B_8C7987D8[4];   /* the quad */
extern Sint32 B_8C798838;               /* 1: a vertex alpha is not 0xFF (the quad blends) */
extern Uint32 B_8C79883C;               /* global colour (modulates every vertex colour) */
extern Uint32 B_8C798840;               /* fade colour of 8C07F0E6 (8C07F090 | 0xFF000000) */
extern Sint32 B_8C798844;               /* 8C07F0E6 draws the fade while non-zero */
extern Float B_8C798848;                /* reset to 1.0 by 8C07DEC0 */
extern Sint32 D_8C1BE8F0;               /* reset to 1 by 8C07DEC0 */
extern NJS_TEXLIST *D_8C673480;         /* Ninja's current texlist (njSetTexture) */
extern NJS_TEXLIST D_8C1BE928;          /* ADV_WINDOW */
extern Sint32 D_8C038F60[][512];        /* palette banks (2 words per entry) */

extern Uint32 func_8C08684C(Uint32 a, Uint32 b);        /* modulate two ARGB colours */
extern void func_8C0867F0(Uint32 color, Float *argb);   /* ARGB -> 4 floats */
extern Uint32 func_8C086824(Float *argb);               /* 4 floats -> ARGB */
extern void func_8C63ED08(NJS_TEXTURE_VTX *vtx, Sint32 n, Sint32 gbix, Sint32 trans); /* njDrawTexture */
extern void func_8C63EC58(NJS_POLYGON_VTX *vtx, Sint32 n, Sint32 trans);              /* njDrawPolygon */
extern void func_8C60551E(Sint32 mode);                     /* SDK, called with 3 before the window frame (UNVERIFIED: texture filter / draw mode) */
extern void func_8C6403B0(MENU_VIEW *v);                /* njInitView (UNVERIFIED name) */
extern void func_8C640454(MENU_VIEW *v, Angle ang);     /* njRotateViewX (UNVERIFIED name) */
extern void func_8C64048C(MENU_VIEW *v, Angle ang);     /* njRotateViewY (UNVERIFIED name) */
extern void func_8C6404E4(MENU_VIEW *v, Angle ang);     /* njRotateViewZ (UNVERIFIED name) */
extern void func_8C640684(MENU_VIEW *v, Float x, Float y, Float z); /* njTranslateView (UNVERIFIED name) */
extern void func_8C640540(MENU_VIEW *v);                /* njSetView (UNVERIFIED name) */

void func_8C07EBBA(Uint32 flip);

void func_8C07DEC0(void)
{
    Sint32 i;

    for (i = 0; i < 4; i++) {
        B_8C7987D8[i].col = 0xFFFFFFFF;
    }
    B_8C7987D8[0].u = 0.0f;
    B_8C7987D8[0].v = 0.0f;
    B_8C7987D8[1].u = 0.0f;
    B_8C7987D8[1].v = 1.0f;
    B_8C7987D8[2].u = 1.0f;
    B_8C7987D8[2].v = 0.0f;
    B_8C7987D8[3].u = 1.0f;
    B_8C7987D8[3].v = 1.0f;
    B_8C79883C = 0xFFFFFFFF;
    B_8C798838 = 0;
    D_8C1BE8F0 = 1;
    B_8C798840 = 0;
    B_8C798848 = 1.0f;
    B_8C798844 = 0;
}

Uint32 func_8C07DF2A(void)
{
    return B_8C79883C;
}

void func_8C07DF30(Uint32 col)
{
    Sint32 i;
    Uint32 c;

    c = func_8C08684C(col, B_8C79883C);
    for (i = 0; i < 4; i++) {
        B_8C7987D8[i].col = c;
    }
    if ((c & 0xFF000000) == 0xFF000000) {
        B_8C798838 = 0;
    } else {
        B_8C798838 = 1;
    }
}

void func_8C07DF6E(Uint32 c0, Uint32 c1, Uint32 c2, Uint32 c3)
{
    c0 = func_8C08684C(c0, B_8C79883C);
    c1 = func_8C08684C(c1, B_8C79883C);
    c2 = func_8C08684C(c2, B_8C79883C);
    c3 = func_8C08684C(c3, B_8C79883C);
    B_8C7987D8[0].col = c0;
    B_8C7987D8[1].col = c1;
    B_8C7987D8[2].col = c2;
    B_8C7987D8[3].col = c3;
    if ((c0 & c1 & c2 & c3 & 0xFF000000) == 0xFF000000) {
        B_8C798838 = 0;
    } else {
        B_8C798838 = 1;
    }
}

Uint32 func_8C07DFDC(Uint32 *out)
{
    Uint32 c;

    if (out != NULL) {
        c = out[0] = B_8C7987D8[0].col;
        out[1] = B_8C7987D8[1].col;
        out[2] = B_8C7987D8[2].col;
        out[3] = B_8C7987D8[3].col;
    } else {
        c = B_8C7987D8[0].col;
    }
    return c;
}

void func_8C07E040(Uint32 col)
{
    Sint32 i;
    Uint32 m;

    B_8C79883C = col;
    m = 0xFF000000;
    for (i = 0; i < 4; i++) {
        B_8C7987D8[i].col = func_8C08684C(B_8C7987D8[i].col, col);
        m &= B_8C7987D8[i].col;
    }
    if ((m & 0xFF000000) == 0xFF000000) {
        B_8C798838 = 0;
    } else {
        B_8C798838 = 1;
    }
}

void func_8C07E0A8(Sint32 trans)
{
    B_8C798838 = trans;
}

/* NON_MATCHING: complete and faithful (routed in the real character select: identical draws); SHC allocates the four corner variables to other registers of FR4-FR7 and schedules the cases differently (193 of 382 instructions differ; the order of the assignments inside each case was tuned for the allocation); anchors 9-15 leave the corners unset in the original (PC: 0) */
#ifdef NON_MATCHING
void func_8C07E0AE(Uint32 attr, Float x, Float y, Float pri)
{
    Uint32 anchor;
    Uint32 flip;
    NJS_TEXMEMLIST *tm;
    NJS_TEXINFO *ti;
    NJS_TEXSURFACE *ts;
    Sint32 trans;
    Float left;
    Float top;
    Float right;
    Float bottom;
    Float hw;
    Float hh;
    Float iz;

    flip = attr & 0xC0000000;
    anchor = attr & 0x0F000000;
    attr &= 0x30FFFFFF;
    tm = (NJS_TEXMEMLIST *)D_8C673480->textures[attr].texaddr;
    ti = &tm->texinfo;
    ts = &ti->texsurface;
    func_8C07EBBA(flip);
    PC_ONLY(left = top = right = bottom = 0.0f;)   /* anchors 9-15: the original draws with whatever the FPU registers held */
    switch (anchor) {
    case 0x00000000:
        left = x;
        top = y;
        right = x + (Float)ts->nWidth;
        bottom = y + (Float)ts->nHeight;
        break;
    case 0x01000000:
        hw = (Float)(ts->nWidth >> 1);
        right = x + hw;
        top = y;
        left = x - hw;
        bottom = y + (Float)ts->nHeight;
        break;
    case 0x02000000:
        left = x - (Float)ts->nWidth;
        right = x;
        top = y;
        bottom = y + (Float)ts->nHeight;
        break;
    case 0x03000000:
        hh = (Float)(ts->nHeight >> 1);
        left = x;
        top = y - hh;
        right = x + (Float)ts->nWidth;
        bottom = y + hh;
        break;
    case 0x04000000:
        hw = (Float)(ts->nWidth >> 1);
        hh = (Float)(ts->nHeight >> 1);
        right = x + hw;
        bottom = y + hh;
        top = y - hh;
        left = x - hw;
        break;
    case 0x05000000:
        hh = (Float)(ts->nHeight >> 1);
        top = y - hh;
        left = x - (Float)ts->nWidth;
        right = x;
        bottom = y + hh;
        break;
    case 0x06000000:
        left = x;
        top = y - (Float)ts->nHeight;
        right = x + (Float)ts->nWidth;
        bottom = y;
        break;
    case 0x07000000:
        hw = (Float)(ts->nWidth >> 1);
        left = x - hw;
        top = y - (Float)ts->nHeight;
        right = x + hw;
        bottom = y;
        break;
    case 0x08000000:
        left = x - (Float)ts->nWidth;
        top = y - (Float)ts->nHeight;
        right = x;
        bottom = y;
        break;
    }
    iz = 1.0f / pri;
    B_8C7987D8[0].x = left;
    B_8C7987D8[0].y = top;
    B_8C7987D8[0].z = iz;
    B_8C7987D8[1].x = left;
    B_8C7987D8[1].y = bottom;
    B_8C7987D8[1].z = iz;
    B_8C7987D8[2].x = right;
    B_8C7987D8[2].y = top;
    B_8C7987D8[2].z = iz;
    B_8C7987D8[3].x = right;
    B_8C7987D8[3].y = bottom;
    B_8C7987D8[3].z = iz;
    trans = B_8C798838;
    if ((ts->PixelFormat & 0x38000000) == 0 || (ts->PixelFormat & 0x38000000) == 0x10000000) {
        trans = 1;
    }
    func_8C63ED08(B_8C7987D8, 4, tm->globalIndex, trans);
}
#else
ASM_STUB(void func_8C07E0AE(Uint32 attr, Float x, Float y, Float pri))
#endif

/* NON_MATCHING: complete and faithful (routed in the real character select: identical draws); register allocation of the corners, of w / h and of the hoisted x +- w / y +- h differs (162 of 185 instructions) */
#ifdef NON_MATCHING
void func_8C07E480(Uint32 attr, Float x, Float y, Float pri, Float w, Float h)
{
    Uint32 anchor;
    Uint32 flip;
    NJS_TEXMEMLIST *tm;
    NJS_TEXSURFACE *ts;
    Sint32 trans;
    Float left;
    Float top;
    Float right;
    Float bottom;
    Float hw;
    Float hh;
    Float x2;
    Float y2;
    Float x0;
    Float y0;
    Float iz;

    flip = attr & 0xC0000000;
    anchor = attr & 0x0F000000;
    attr &= 0x30FFFFFF;
    tm = (NJS_TEXMEMLIST *)D_8C673480->textures[attr].texaddr;
    ts = &tm->texinfo.texsurface;
    func_8C07EBBA(flip);
    hh = h * 0.5f;
    hw = w * 0.5f;
    y2 = y + h;
    x2 = x + w;
    x0 = x - w;
    y0 = y - h;
    PC_ONLY(left = top = right = bottom = 0.0f;)   /* anchors 9-15: the original draws with whatever the FPU registers held */
    switch (anchor) {
    case 0x00000000:
        left = x;
        top = y;
        bottom = y2;
        right = x2;
        break;
    case 0x01000000:
        left = x - hw;
        right = x + hw;
        top = y;
        bottom = y2;
        break;
    case 0x02000000:
        left = x0;
        top = y;
        right = x;
        bottom = y2;
        break;
    case 0x03000000:
        top = y - hh;
        left = x;
        right = x2;
        bottom = y + hh;
        break;
    case 0x04000000:
        top = y - hh;
        left = x - hw;
        right = x + hw;
        bottom = y + hh;
        break;
    case 0x05000000:
        top = y - hh;
        left = x0;
        right = x;
        bottom = y + hh;
        break;
    case 0x06000000:
        top = y0;
        left = x;
        right = x2;
        bottom = y;
        break;
    case 0x07000000:
        left = x - hw;
        right = x + hw;
        top = y0;
        bottom = y;
        break;
    case 0x08000000:
        left = x0;
        right = x;
        top = y0;
        bottom = y;
        break;
    }
    iz = 1.0f / pri;
    B_8C7987D8[0].x = left;
    B_8C7987D8[0].y = top;
    B_8C7987D8[0].z = iz;
    B_8C7987D8[1].x = left;
    B_8C7987D8[1].y = bottom;
    B_8C7987D8[1].z = iz;
    B_8C7987D8[2].x = right;
    B_8C7987D8[2].y = top;
    B_8C7987D8[2].z = iz;
    B_8C7987D8[3].x = right;
    B_8C7987D8[3].y = bottom;
    B_8C7987D8[3].z = iz;
    trans = B_8C798838;
    if ((ts->PixelFormat & 0x38000000) == 0 || (ts->PixelFormat & 0x38000000) == 0x10000000) {
        trans = 1;
    }
    func_8C63ED08(B_8C7987D8, 4, tm->globalIndex, trans);
}
#else
ASM_STUB(void func_8C07E480(Uint32 attr, Float x, Float y, Float pri, Float w, Float h))
#endif

/* NON_MATCHING: complete and faithful (routed in the real character select: identical draws); register allocation of the corners differs (FMAC tails merged as in the original; 215 of 388 instructions) */
#ifdef NON_MATCHING
void func_8C07E642(Uint32 attr, Float x, Float y, Float pri, Float sx, Float sy)
{
    Uint32 flip;
    Uint32 anchor;
    NJS_TEXMEMLIST *tm;
    NJS_TEXSURFACE *ts;
    Sint32 trans;
    Float left;
    Float top;
    Float right;
    Float bottom;
    Float hw;
    Float hh;
    Float iz;

    flip = attr & 0xC0000000;
    anchor = attr & 0x0F000000;
    attr &= 0x30FFFFFF;
    tm = (NJS_TEXMEMLIST *)D_8C673480->textures[attr].texaddr;
    ts = &tm->texinfo.texsurface;
    func_8C07EBBA(flip);
    PC_ONLY(left = top = right = bottom = 0.0f;)   /* anchors 9-15: the original draws with whatever the FPU registers held */
    switch (anchor) {
    case 0x00000000:
        right = SA1_FMAC((Float)ts->nWidth, sx, x);
        top = y;
        bottom = SA1_FMAC((Float)ts->nHeight, sy, y);
        left = x;
        break;
    case 0x01000000:
        hw = (Float)(ts->nWidth >> 1) * sx;
        left = x - hw;
        top = y;
        right = x + hw;
        bottom = SA1_FMAC((Float)ts->nHeight, sy, y);
        break;
    case 0x02000000:
        hw = (Float)ts->nWidth * sx;
        left = x - hw;
        top = y;
        right = x;
        bottom = SA1_FMAC((Float)ts->nHeight, sy, y);
        break;
    case 0x03000000:
        hh = (Float)(ts->nHeight >> 1) * sy;
        top = y - hh;
        left = x;
        right = SA1_FMAC((Float)ts->nWidth, sx, x);
        bottom = y + hh;
        break;
    case 0x04000000:
        hw = (Float)(ts->nWidth >> 1) * sx;
        hh = (Float)(ts->nHeight >> 1) * sy;
        top = y - hh;
        left = x - hw;
        right = x + hw;
        bottom = y + hh;
        break;
    case 0x05000000:
        hh = (Float)(ts->nHeight >> 1) * sy;
        hw = (Float)ts->nWidth * sx;
        top = y - hh;
        left = x - hw;
        right = x;
        bottom = y + hh;
        break;
    case 0x06000000:
        top = y - (Float)ts->nHeight * sy;
        right = SA1_FMAC((Float)ts->nWidth, sx, x);
        left = x;
        bottom = y;
        break;
    case 0x07000000:
        hw = (Float)(ts->nWidth >> 1) * sx;
        top = y - (Float)ts->nHeight * sy;
        left = x - hw;
        right = x + hw;
        bottom = y;
        break;
    case 0x08000000:
        left = x - (Float)ts->nWidth * sx;
        top = y - (Float)ts->nHeight * sy;
        right = x;
        bottom = y;
        break;
    }
    iz = 1.0f / pri;
    B_8C7987D8[0].x = left;
    B_8C7987D8[0].y = top;
    B_8C7987D8[0].z = iz;
    B_8C7987D8[1].x = left;
    B_8C7987D8[1].y = bottom;
    B_8C7987D8[1].z = iz;
    B_8C7987D8[2].x = right;
    B_8C7987D8[2].y = top;
    B_8C7987D8[2].z = iz;
    B_8C7987D8[3].x = right;
    B_8C7987D8[3].y = bottom;
    B_8C7987D8[3].z = iz;
    trans = B_8C798838;
    if ((ts->PixelFormat & 0x38000000) == 0 || (ts->PixelFormat & 0x38000000) == 0x10000000) {
        trans = 1;
    }
    func_8C63ED08(B_8C7987D8, 4, tm->globalIndex, trans);
}
#else
ASM_STUB(void func_8C07E642(Uint32 attr, Float x, Float y, Float pri, Float sx, Float sy))
#endif

void func_8C07EB60(MENU2D_LIST *list, Float x, Float y, Float pri)
{
    MENU2D_PART *p;
    Sint32 i;

    p = list->part;
    for (i = 0; i < list->count; i++, p++) {
        func_8C07E0AE(p->n, p->dx + x, p->dy + y, pri);
    }
}

void func_8C07EBBA(Uint32 flip)
{
    switch (flip) {
    case 0x00000000:
    default:
        B_8C7987D8[0].u = 0.0f;
        B_8C7987D8[0].v = 0.0f;
        B_8C7987D8[1].u = 0.0f;
        B_8C7987D8[1].v = 1.0f;
        B_8C7987D8[2].u = 1.0f;
        B_8C7987D8[2].v = 0.0f;
        B_8C7987D8[3].u = 1.0f;
        B_8C7987D8[3].v = 1.0f;
        break;
    case 0x40000000:
        B_8C7987D8[0].u = 0.0f;
        B_8C7987D8[0].v = 1.0f;
        B_8C7987D8[1].u = 0.0f;
        B_8C7987D8[1].v = 0.0f;
        B_8C7987D8[2].u = 1.0f;
        B_8C7987D8[2].v = 1.0f;
        B_8C7987D8[3].u = 1.0f;
        B_8C7987D8[3].v = 0.0f;
        break;
    case 0x80000000:
        B_8C7987D8[0].u = 1.0f;
        B_8C7987D8[0].v = 0.0f;
        B_8C7987D8[1].u = 1.0f;
        B_8C7987D8[1].v = 1.0f;
        B_8C7987D8[2].u = 0.0f;
        B_8C7987D8[2].v = 0.0f;
        B_8C7987D8[3].u = 0.0f;
        B_8C7987D8[3].v = 1.0f;
        break;
    case 0xC0000000:
        B_8C7987D8[0].u = 1.0f;
        B_8C7987D8[0].v = 1.0f;
        B_8C7987D8[1].u = 1.0f;
        B_8C7987D8[1].v = 0.0f;
        B_8C7987D8[2].u = 0.0f;
        B_8C7987D8[2].v = 1.0f;
        B_8C7987D8[3].u = 0.0f;
        B_8C7987D8[3].v = 0.0f;
        break;
    }
}

void func_8C07EC90(void)
{
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
}

/* NON_MATCHING: complete and faithful; the first argument is never used (the original only loads it once, dead; reproduced with an empty switch), stack homes of the arguments differ */
#ifdef NON_MATCHING
Uint32 func_8C07ECE0(Uint32 unused, Uint32 c0, Uint32 c1, Float t)
{
    Float c[3][4];
    Float u;

    func_8C0867F0(c0, c[0]);
    func_8C0867F0(c1, c[1]);
    u = 1.0f - t;
    switch (unused) {
    }
    c[2][0] = SA1_FMAC(u, c[0][0], c[1][0] * t);
    c[2][1] = SA1_FMAC(u, c[0][1], c[1][1] * t);
    c[2][2] = SA1_FMAC(u, c[0][2], c[1][2] * t);
    c[2][3] = SA1_FMAC(u, c[0][3], c[1][3] * t);
    return func_8C086824(c[2]);
}
#else
ASM_STUB(Uint32 func_8C07ECE0(Uint32 unused, Uint32 c0, Uint32 c1, Float t))
#endif

void func_8C07EF42(NJS_TEXLIST *tl, Sint32 n, Uint32 *w, Uint32 *h)
{
    NJS_TEXMEMLIST *tm;
    NJS_TEXSURFACE *ts;

    if (tl == NULL) {
        tl = D_8C673480;
    }
    tm = (NJS_TEXMEMLIST *)tl->textures[n].texaddr;
    ts = &tm->texinfo.texsurface;
    if (w != NULL) {
        *w = ts->nWidth;
    }
    if (h != NULL) {
        *h = ts->nHeight;
    }
}

void func_8C07F18C(task *tp)
{
    taskwk *twp;
    MENU_VIEW view;

    twp = tp->twp;
    func_8C6403B0(&view);
    func_8C6404E4(&view, twp->ang.z);
    func_8C640454(&view, twp->ang.x);
    func_8C64048C(&view, twp->ang.y);
    func_8C640684(&view, twp->pos.x, twp->pos.y, twp->pos.z);
    func_8C640540(&view);
}

void func_8C07F1D2(Sint32 bank)
{
    Sint32 i;
    Sint32 c;
    Sint32 col;

    c = 255;
    for (i = 0; i < 256; i++) {
        col = (c << 16) | (c << 8) | c;
        D_8C038F60[bank][i * 2] = col | 0xFF000000;
        c--;
    }
}

void func_8C07F21C(void)
{
    Sint32 i;

    for (i = 0; i < 8; i++) {
        func_8C07F1D2(i);
    }
}

/* NON_MATCHING: complete and faithful; needs the whole chunk in C: the original reaches 8C07DFDC / 8C07DF30 / 8C07DF6E / 8C07E0AE with JSR (out of BSR range in the full translation unit) and this file lacks 8C07E9E6, 8C07ED5E, 8C07EDAE, 8C07EF6E, 8C07EF88, 8C07F090, 8C07F0E0, 8C07F0E6 */
#ifdef NON_MATCHING
void func_8C07F2A0(Float x, Float y, Float pri, Float w, Float h)
{
    NJS_COLOR save[4];
    NJS_COLOR c[4];
    Float yb;
    Float hm;
    Float ym;
    Float xr;

    func_8C07DFDC(&save[0].color);
    func_8C60551E(3);
    njSetTexture(&D_8C1BE928);
    func_8C07DF30(0xC0808080);
    c[0].color = save[0].color;
    c[1].color = func_8C07ECE0(0, save[0].color, save[1].color, 16.0f / h);
    c[2].color = func_8C07ECE0(0, save[0].color, save[1].color, (h - 16.0f) / h);
    c[3].color = save[1].color;
    func_8C07DF6E(c[0].color, c[1].color, c[0].color, c[1].color);
    func_8C07E0AE(0x00000000, x, y, pri);
    xr = x + w;
    func_8C07E0AE(0x82000000, xr, y, pri);
    func_8C07DF6E(c[2].color, c[3].color, c[2].color, c[3].color);
    yb = y + h;
    func_8C07E0AE(0x46000000, x, yb, pri);
    func_8C07E0AE(0xC8000000, xr, yb, pri);
    func_8C07DF6E(c[1].color, c[2].color, c[1].color, c[2].color);
    hm = h - 32.0f;
    ym = y + 16.0f;
    func_8C07E480(1, x, ym, pri, 16.0f, hm);
    func_8C07E480(1, xr - 16.0f, ym, pri, 16.0f, hm);
    func_8C07DF6E(c[0].color, c[3].color, c[0].color, c[3].color);
    func_8C07E480(1, x + 16.0f, y, pri, w - 32.0f, h);
}
#else
ASM_STUB(void func_8C07F2A0(Float x, Float y, Float pri, Float w, Float h))
#endif

/* NON_MATCHING: complete and faithful; stack homes of pri / w / h and the register choice for the vertex pointer differ */
#ifdef NON_MATCHING
void func_8C07F42A(Float x, Float y, Float pri, Float w, Float h)
{
    NJS_POLYGON_VTX v[4];
    Uint32 c[2];
    Float iz;

    c[0] = func_8C08684C(0xFF007014, B_8C79883C);
    c[1] = func_8C08684C(0x00007014, B_8C79883C);
    iz = 1.0f / pri;
    v[0].x = x;
    v[0].y = y;
    v[0].z = iz;
    v[0].col = c[0];
    v[1].x = x;
    v[1].y = y + h;
    v[1].z = iz;
    v[1].col = c[0];
    v[2].x = x + w;
    v[2].y = y;
    v[2].z = iz;
    v[2].col = c[1];
    v[3].x = x + w;
    v[3].y = y + h;
    v[3].z = iz;
    v[3].col = c[1];
    njColorBlendingMode(0, 9);
    njColorBlendingMode(1, 5);
    func_8C63EC58(v, 4, 1);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
}
#else
ASM_STUB(void func_8C07F42A(Float x, Float y, Float pri, Float w, Float h))
#endif
