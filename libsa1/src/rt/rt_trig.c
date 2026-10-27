/*
 * rt_trig.c - Ninja trigonometry: njSin, njCos, njTan (FSCA) and njSqrt
 * (FSQRT), docs/libsa1_integration.md 4.2.
 *
 * Clean-room: written from the documented behaviour and the observed results
 * of the SDK functions, on top of the shared FPU models; verified bit-exact
 * against the original SDK code in the harness (README.md):
 *     sa1h_<tag>.exe diff njSin 2000 1 --ulps 0     (njCos, njTan, njSqrt alike)
 *
 * Behaviour reproduced:
 *   njSin, njCos  FSCA of the BAMS angle, only its low 16 bits count.  (The
 *                 SDK also leaves the other value in FR1 / FR0: caller-saved
 *                 registers, which no SHC-compiled caller reads.)
 *   njTan         sin / cos of one FSCA, a single FDIV: +inf at 0x4000, -inf
 *                 at 0xC000 (the cosine is +0 there), never NaN.
 *   njSqrt        signed square root: n < 0 gives -sqrt(|n|), anything else
 *                 sqrt(n), so -0 stays -0 and a NaN gives the SH-4 qNaN.
 */
#include "rt.h"

/* 8C63A9B8 */
Float njSin(Angle n)
{
    Float s, c;

    RT_FSCA(n, &s, &c);
    return s;
}

/* 8C63A970 */
Float njCos(Angle n)
{
    Float s, c;

    RT_FSCA(n, &s, &c);
    return c;
}

/* 8C63A9F4 */
Float njTan(Angle n)
{
    Float s, c;

    RT_FSCA(n, &s, &c);
    return RT_FDIV(s, c);
}

/* 8C63A9C4 */
Float njSqrt(Float n)
{
    if (0.0f > n) {
        return RT_FNEG(RT_FSQRT(RT_FABS(n)));
    }
    return RT_FSQRT(n);
}
