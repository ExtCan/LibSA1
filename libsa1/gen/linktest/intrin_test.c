/*
 * intrin_test.c - src/rt/rt_intrin.h (the SHC intrinsics inlined into lowered code) against the world core's
 * out-of-line definitions of the same names (src/world/sa1_world_intrin.c): every result bit must agree, under
 * the game's MXCSR (0xFFC0: round toward zero, FTZ, DAZ) and under the host default (round to nearest).
 *
 * Built by LIB/build.py with the game flags (sadc tools/pc_cflags.txt) and -include src/rt/rt_intrin.h, so
 * "sa1g_fipr(a, b)" is the inline model and "(sa1g_fipr)(a, b)" (no macro expansion) the world's function.
 *
 * 32-bit x86: a float RETURNED by a function travels through the x87 stack (cdecl), and loading a signalling
 * NaN there quiets it: the SH-4 default NaN 0x7FBFFFFF (FSQRT / FSRRA / FIPR of bad input) comes back from the
 * out-of-line functions as 0x7FFFFFFF. The inline versions keep the model's bits; such pairs are counted as
 * "x87-quieted NaNs" (a known property of every float-returning function on that ABI), not as differences.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xmmintrin.h>

static uint32_t rng = 0x12345678u;

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint32_t f2u(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static unsigned quieted;

/* inline result a, out-of-line result b */
static int differ(float a, float b)
{
    uint32_t ua = f2u(a), ub = f2u(b);
    if (ua == ub) {
        return 0;
    }
#if defined(__i386__)
    if ((ua & 0x7F800000u) == 0x7F800000u && (ua & 0x7FFFFFu) != 0u && ub == (ua | 0x00400000u)) {
        quieted++;
        return 0;
    }
#endif
    return 1;
}

static float rnd(void)
{
    static const uint32_t special[] = { 0x00000000u, 0x80000000u, 0x3F800000u, 0xBF800000u, 0x7F800000u,
                                        0xFF800000u, 0x7FC00000u, 0x7FBFFFFFu, 0x00000001u, 0x807FFFFFu,
                                        0x7F7FFFFFu, 0x34000000u };
    uint32_t r = next();
    float f;
    if ((r & 15u) == 0u) {
        r = special[(r >> 4) % (sizeof special / sizeof special[0])];
    } else if ((r & 15u) < 8u) {
        r = (r & 0x80000000u) | ((0x70u + ((r >> 4) & 0x1Fu)) << 23) | (next() & 0x7FFFFFu);  /* |x| ~ 2^-15..2^16 */
    } else {
        r = next();
    }
    memcpy(&f, &r, 4);
    return f;
}

int main(void)
{
    static const unsigned modes[2] = { 0xFFC0u, 0x1F80u };
    float xm[16], v[4], a[4], o1[4], o2[4];
    unsigned m, bad = 0, n = 0, per[2][6];
    int i, k;
    uint32_t saved = _mm_getcsr();
    sa1_rt_xmtrx = xm;
    memset(per, 0, sizeof per);
    for (m = 0; m < 2; m++) {
        _mm_setcsr(modes[m]);
        for (i = 0; i < 200000; i++) {
            float x = rnd();
            for (k = 0; k < 4; k++) {
                v[k] = rnd();
                a[k] = rnd();
            }
            for (k = 0; k < 16; k++) {
                xm[k] = rnd();
            }
            per[m][0] += differ(sa1g_fabsf(x), (sa1g_fabsf)(x));
            per[m][1] += differ(sa1g_sqrtf(x), (sa1g_sqrtf)(x));
            per[m][2] += differ(sa1g_fsrra(x), (sa1g_fsrra)(x));
            per[m][3] += differ(sa1g_fipr(v, a), (sa1g_fipr)(v, a));
            sa1g_ftrv(v, o1);
            (sa1g_ftrv)(v, o2);
            per[m][4] += memcmp(o1, o2, sizeof o1) != 0;
            n += 5;
        }
        for (i = 0; i < 0x20000; i++) {
            float s1, c1, s2, c2;
            sa1g_fsca(i - 0x10000, &s1, &c1);
            (sa1g_fsca)(i - 0x10000, &s2, &c2);
            per[m][5] += f2u(s1) != f2u(s2) || f2u(c1) != f2u(c2);
            n++;
        }
    }
    _mm_setcsr(saved);
    sa1_rt_xmtrx = NULL;
    for (m = 0; m < 2; m++) {
        for (k = 0; k < 6; k++) {
            bad += per[m][k];
        }
        if (per[m][0] | per[m][1] | per[m][2] | per[m][3] | per[m][4] | per[m][5]) {
            printf("MXCSR %04X: fabsf %u, sqrtf %u, fsrra %u, fipr %u, ftrv %u, fsca %u differ\n", modes[m], per[m][0],
                   per[m][1], per[m][2], per[m][3], per[m][4], per[m][5]);
        }
    }
    printf("intrinsics: %u comparisons, %u differ (inline rt_intrin.h vs world sa1_world_intrin.c, MXCSR FFC0 and "
           "1F80); %u NaN results quieted by the x87 return of the out-of-line versions (i386 only)\n", n, bad, quieted);
    return bad != 0;
}
