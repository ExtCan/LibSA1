/*
 * 1ST_READ 8C036F60-8C036FD8 (its own chunk): set-up of the hand-written
 * basic-model renderer (8C036FFC-8C038D00, the renderer that libsa1's rt
 * replaces: LIB src/rt/rt_draw.c section 5, 8C03700C / 8C037098 / 8C038CEC).
 *
 * The renderer keeps its parameters in the work area B_8C8FFE00 and reaches
 * them GBR-relative; func_8C036F60 points GBR at that area, copies seven
 * screen / projection values of the Ninja library into its head and sets the
 * palette bank pointer (GBR + 0x58, bank 0 = D_8C038F60; 8C038CEC changes the
 * bank) and the table pointer GBR + 0x38 (D_8C03D760). Callers: the landtable
 * manager 8C0519C0 / its display 8C051E00, 8C0544E2 (menu / stage start) and
 * 8C10B840.
 *
 * NON_MATCHING only: the original is hand-written assembly (no scheduling,
 * FR0 / R1 / R2 as temporaries, a pointer walked with ADD #4, a PR save with
 * no call, H'0000 alignment fill, its own .END), not SHC output. The body is
 * a C description of what it does; the original assembly stays in the build.
 * libsa1: a 'void' policy is exact for the worlds (the rt renderer does not
 * read this work area); the C below is the faithful alternative.
 */
#include "sa1/sa1.h"

/* The renderer's GBR work area (only the words written here) */
typedef struct {
    Float f00;                  /* 0x00 = B_8C88F5F0 */
    Float f04;                  /* 0x04 = B_8C88F5F4 */
    Float f08;                  /* 0x08 = B_8C88F5E4 */
    Float f0C;                  /* 0x0C = B_8C88F61C */
    Float f10;                  /* 0x10 = B_8C88F610 */
    Float f14;                  /* 0x14 = B_8C88F618 * B_8C88F5E4 */
    Float f18;                  /* 0x18 = B_8C88F61C * B_8C88F5E4 */
    Uint8 pad1C[0x38 - 0x1C];
    void *tab;                  /* 0x38 GBR + 56: D_8C03D760 */
    Uint8 pad3C[0x58 - 0x3C];
    void *pal;                  /* 0x58 GBR + 88: the palette bank (D_8C038F60 + bank * 0x800) */
} RENDER_GBR;

extern RENDER_GBR B_8C8FFE00;
extern Float B_8C88F5E4;
extern Float B_8C88F5F0;
extern Float B_8C88F5F4;
extern Float B_8C88F610;
extern Float B_8C88F618;
extern Float B_8C88F61C;
extern Sint32 D_8C038F60[][512];        /* palette banks */
extern Uint8 D_8C03D760[];
extern void _builtin_set_gbr(void *base);

#ifdef NON_MATCHING
void func_8C036F60(void)
{
    RENDER_GBR *w;
    Float s;

    _builtin_set_gbr(&B_8C8FFE00);
    w = &B_8C8FFE00;
    w->f00 = B_8C88F5F0;
    w->f04 = B_8C88F5F4;
    s = B_8C88F5E4;
    w->f08 = s;
    w->f0C = B_8C88F61C;
    w->f10 = B_8C88F610;
    w->f14 = B_8C88F618 * s;
    w->f18 = B_8C88F61C * s;
    w->pal = D_8C038F60[0];
    w->tab = D_8C03D760;
}
#endif
