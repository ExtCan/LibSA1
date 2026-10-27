#include "sa1/sa1.h"

/*
 * Knuckles effect (1ST_READ chunk 8C0CBA40): a ribbon trail made of rings of
 * 8 points (radius 5, centred 7.5 above the task position, rotated by the
 * task angles). Each call pushes a new ring at the head of a 16-ring history
 * and draws the band between consecutive rings with a red colour whose alpha
 * ramps up over the first 20 % of the trail and fades out afterwards.
 */

/* one ring of the trail (0x60 bytes: copied with the block-move helper) */
typedef struct {
    NJS_POINT3 p[8];            /* 0x00 */
} KNU_TRAIL_RING;               /* 0x60 */

/* B_8C7A2784: ring count + history (ring[0] = newest). The count is cleared
 * by func_8C0CB9E8 (code_8C0CB960.c declares the same layout; candidate for
 * a shared header). */
typedef struct {
    Sint32 num;                 /* 0x000 */
    KNU_TRAIL_RING ring[16];    /* 0x004 */
} KNU_TRAIL;                    /* 0x604 */

SA1_ASSERT_SIZE(KNU_TRAIL_RING, 0x60);
SA1_ASSERT_SIZE(KNU_TRAIL, 0x604);

extern KNU_TRAIL B_8C7A2784;
extern NJS_POINT3 B_8C7A2D88[18];   /* band vertices (D_8C3536C0.p) */
extern NJS_COLOR B_8C7A2E60[18];    /* band colours (D_8C3536C0.col) */
extern NJS_POINT3COL D_8C3536C0;    /* { B_8C7A2D88, B_8C7A2E60, NULL, 16 } */

/*
 * func_8C0CBA40 (caller func_8C0C8F20): update and draw the trail of twp.
 * NON_MATCHING only because of the symbol map (same situation as
 * PInitialize in code_8C06C300.c and code_8C0742D4.c): the ring shift
 * (ring[i] = ring[i - 1], 0x60 bytes) calls SHC's __quick_mvn, which
 * config/symbols.1st_read.txt maps to 8C10CD38, while the game code calls
 * 8C10CDDC. Otherwise the body compiles to the original instruction for
 * instruction (shcdiff: 1 differing word, the __quick_mvn pool entry).
 * The "a ? njRotateZ(NULL, a) : (void)0" form reproduces the stack temporary
 * the original keeps the angles in (see code_8C056A04.c).
 */
#ifdef NON_MATCHING
void func_8C0CBA40(taskwk *twp)
{
    Sint32 i;
    Sint32 j;
    Float f;
    NJS_POINT3 *pp;
    NJS_COLOR *cp;
    Angle ang;

    i = B_8C7A2784.num;
    if (i > 15) {
        i = 15;
    }
    for (; i > 0; i--) {
        B_8C7A2784.ring[i] = B_8C7A2784.ring[i - 1];
    }

    {
        NJS_VECTOR src;
        NJS_VECTOR v;

        njPushMatrix(D_8C67C640);
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        (0x8000 - twp->ang.y) ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
        for (i = 0, ang = 0; i < 8; i++, ang += 0x2000) {
            src.x = -7.5f;
            src.y = njSin(ang) * 5.0f;
            src.z = njCos(ang) * 5.0f;
            njCalcVector(NULL, &src, &v);
            njAddVector(&v, &twp->pos);
            v.y += 7.5f;
            B_8C7A2784.ring[0].p[i] = v;
        }
        njPopMatrix(1);
    }

    if (++B_8C7A2784.num > 16) {
        B_8C7A2784.num = 16;
    }
    if (B_8C754EC8 != 0) {
        return;
    }

    {
        NJS_COLOR col;

        njColorBlendingMode(0, 8);
        njColorBlendingMode(1, 10);
        for (i = 0; i < B_8C7A2784.num - 1; i++) {
            f = (Float)i / (Float)B_8C7A2784.num;
            if (f < 0.19999999f) {
                f *= 2.5f;
            } else {
                f = (1.0f - f) * 0.62499995f;
            }
            col.argb.a = (Uint8)SA1_FTOI(f * 256.0f);  /* f in [0, 0.5] */
            col.argb.r = 0xFF;
            col.argb.g = 0;
            col.argb.b = 0;
            pp = B_8C7A2D88;
            cp = B_8C7A2E60;
            for (j = 0; j < 8; j++) {
                *pp++ = B_8C7A2784.ring[i].p[j];
                *pp++ = B_8C7A2784.ring[i + 1].p[j];
                *cp++ = col;
                *cp++ = col;
            }
            *pp++ = B_8C7A2784.ring[i].p[0];
            *pp = B_8C7A2784.ring[i + 1].p[0];
            *cp++ = col;
            *cp = col;
            func_8C63D66C(&D_8C3536C0, 16, 0x62);
        }
        njColorBlendingMode(0, 1);
        njColorBlendingMode(1, 1);
    }
}
#else
ASM_STUB(void func_8C0CBA40(taskwk *twp))
#endif
