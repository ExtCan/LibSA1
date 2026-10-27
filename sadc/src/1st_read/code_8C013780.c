#include "common.h"

/* Linear interpolation between two ARGB colours; alpha is taken from c1. */
Uint32 func_8C013780(Sint32 unused, Sint32 c1, Sint32 c2, Float t) {
    Sint32 r1;
    Sint32 g1;
    Sint32 b1;
    Sint32 g2;
    Sint32 r2;
    Sint32 b2;
    Sint32 a;
    Float fr;
    Float fg;
    Float fb;
    Sint32 r;
    Sint32 g;
    Sint32 b;
    Uint32 c;

    r1 = (c1 >> 16) & 0xFF;
    g1 = (c1 >> 8) & 0xFF;
    b1 = c1 & 0xFF;
    r2 = (c2 >> 16) & 0xFF;
    g2 = (c2 >> 8) & 0xFF;
    b2 = c2 & 0xFF;
    a = (c1 >> 24) & 0xFF;
    /* FMAC x3 in the original */
    fr = SA1_FMAC(t, (Float)(r2 - r1), (Float)r1);
    fg = SA1_FMAC(t, (Float)(g2 - g1), (Float)g1);
    fb = SA1_FMAC(t, (Float)(b2 - b1), (Float)b1);
    r = SA1_FTOI(fr);
    g = SA1_FTOI(fg);
    b = SA1_FTOI(fb);
    c = (a << 24) | (r << 16) | (g << 8) | b;
    return c;
}