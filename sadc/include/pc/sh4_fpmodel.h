/*
 * sh4_fpmodel.h - the SH-4 FPU models shared by the interpreter harness and
 * the libsa1 runtime (docs/libsa1_integration.md 8.2, decision D6).
 *
 * Users:
 *   SADC/harness/src/sh4_cpu.c      the interpreter (FIPR, FTRV, FSRRA, FSCA,
 *                                   FMAC, FSQRT, FTRC and NaN results)
 *   SADC/harness/src/sh4_intrin.c   the SHC intrinsics (_builtin_fipr ...) of
 *                                   natively compiled decompiled code
 *   libsa1/src/rt (via rt.h)        the reimplemented SDK (njSin, njUnitVector,
 *                                   the matrix code ...)
 * The reference traces (SADC/build/traces/ref) were recorded with exactly these
 * models, so the runtime must use them and nothing else.
 *
 * The functions were moved verbatim from harness/src/sh4_cpu.c (2026-10-20);
 * the only changes: the "sh4_" prefix on the helpers (f2u, u2f, canon,
 * canond), memcpy -> __builtin_memcpy and sqrt -> the SSE sqrtsd/sqrtss
 * builtins (the instructions the harness build already emitted for them),
 * so that the header needs no C library and gives the same results under any
 * -fno-builtin / -fmath-errno setting. FSCA reads the generated quarter-wave
 * table sh4_fsca_q[] (libsa1/src/rt/rt_fsca_q.c) instead of a table that the
 * harness built at start-up with the host's sin().
 *
 * Requirements: scalar SSE math (-msse2 -mfpmath=sse on 32-bit x86). Results
 * depend on the host rounding mode: the game runs with MXCSR = round toward
 * zero + FTZ + DAZ (SH-4 FPSCR 0x00040001), which the harness keeps all the
 * time and libsa1 sets for the duration of a tick (design 8.1).
 *
 * Linkage: everything is static inline, except that a file which defines
 * SH4_FPMODEL_EXTERN before including this header gets external definitions
 * of the public models (sh4_fsrra, sh4_fipr, sh4_fmac, sh4_fsqrt, sh4_ftrv,
 * sh4_ftrc, sh4_fsca). harness/src/sh4_cpu.c does that, so the declarations in
 * harness/src/sh4.h keep linking for the other harness files. A file that
 * includes both this header and sh4.h must include this header first.
 */
#ifndef SH4_FPMODEL_H
#define SH4_FPMODEL_H

#include <stdint.h>

#if defined(_MSC_VER) && !defined(__clang__)
#define SH4FP_INL static __inline
#else
#define SH4FP_INL static inline
#endif
#if defined(SH4_FPMODEL_EXTERN)
#define SH4FP_API
#else
#define SH4FP_API SH4FP_INL
#endif

#if defined(__GNUC__) || defined(__clang__)
#define SH4FP_MEMCPY(d, s, n) __builtin_memcpy((d), (s), (n))
#else
#include <string.h>
#define SH4FP_MEMCPY(d, s, n) memcpy((d), (s), (n))
#endif

/* The models must never be contracted into FMA, whatever -ffp-contract the
 * including file uses (the harness builds without --pc compile decompiled
 * code with -ffp-contract=on). */
#if defined(__clang__)
#define SH4FP_NOCONTRACT _Pragma("STDC FP_CONTRACT OFF")
#else
#define SH4FP_NOCONTRACT
#endif

/* Square roots: the SSE instructions sqrtss / sqrtsd, rounded in the current
 * MXCSR mode (what the harness build emitted for __builtin_sqrtf / sqrt).
 * GCC/clang use the compiler builtins behind _mm_sqrt_ss/_mm_sqrt_sd, so no
 * system header with inline functions is needed (harness/tools/gen_native.py
 * scans every function body the file sees). */
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__SSE2__) || defined(__x86_64__))
typedef float sh4fp_v4sf __attribute__((__vector_size__(16)));
typedef double sh4fp_v2df __attribute__((__vector_size__(16)));
#define SH4FP_SQRTF(x) (__builtin_ia32_sqrtss((sh4fp_v4sf){ (x), 0.0f, 0.0f, 0.0f })[0])
#define SH4FP_SQRTD(x) (__builtin_ia32_sqrtsd((sh4fp_v2df){ (x), 0.0 })[0])
#elif defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define SH4FP_SQRTF(x) _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)))
#define SH4FP_SQRTD(x) _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(x)))
#else
#define SH4FP_SQRTF(x) __builtin_sqrtf(x)     /* non-x86 hosts: compile with -fno-math-errno */
#define SH4FP_SQRTD(x) __builtin_sqrt(x)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* bits                                                                */

SH4FP_INL uint32_t sh4_f2u(float f) { uint32_t u; SH4FP_MEMCPY(&u, &f, 4); return u; }
SH4FP_INL float sh4_u2f(uint32_t u) { float f; SH4FP_MEMCPY(&f, &u, 4); return f; }

/* NaN canonicalisation: every NaN result of an FPU operation is the SH-4
 * default qNaN 0x7FBFFFFF (x86 would produce 0xFFC00000) */
SH4FP_INL float sh4_canon(float f)
{
    uint32_t u = sh4_f2u(f);
    if ((u & 0x7FFFFFFFu) > 0x7F800000u)
        return sh4_u2f(0x7FBFFFFFu);
    return f;
}

SH4FP_INL double sh4_canond(double d)
{
    uint64_t u;
    SH4FP_MEMCPY(&u, &d, 8);
    if ((u & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull) {
        u = 0x7FF7FFFFFFFFFFFFull;
        SH4FP_MEMCPY(&d, &u, 8);
    }
    return d;
}

/* ------------------------------------------------------------------ */
/* models                                                              */

/* FSRRA: 1/sqrt(x).  Hardware error bound 2^-21; we return the double
 * result rounded under the current mode.  APPROXIMATE. */
SH4FP_API float sh4_fsrra(float x)
{
    SH4FP_NOCONTRACT
    uint32_t u = sh4_f2u(x);
    if ((u & 0x7FFFFFFFu) > 0x7F800000u)
        return sh4_u2f(0x7FBFFFFFu);
    if (u & 0x80000000u) {
        if ((u & 0x7FFFFFFFu) == 0)            /* -0 -> -inf */
            return sh4_u2f(0xFF800000u);
        return sh4_u2f(0x7FBFFFFFu);            /* negative -> qNaN */
    }
    if ((u & 0x7F800000u) == 0)                 /* +0 / denormal (DN) -> +inf */
        return sh4_u2f(0x7F800000u);
    if (u == 0x7F800000u)
        return 0.0f;
    return (float)(1.0 / SH4FP_SQRTD((double)x));
}

/* FIPR: sum of 4 products, computed in double, rounded once.  APPROXIMATE
 * (the hardware's internal precision differs in the last bits). */
SH4FP_API float sh4_fipr(const float *va, const float *vb)
{
    SH4FP_NOCONTRACT
    double s = (double)va[0] * vb[0];
    s += (double)va[1] * vb[1];
    s += (double)va[2] * vb[2];
    s += (double)va[3] * vb[3];
    return sh4_canon((float)s);
}

/* FMAC: FR0*FRm + FRn fused.  The product of two floats is exact in double;
 * the double sum truncated (RZ) and then truncated to float equals the
 * directly truncated exact result, so this is exact under RZ.  Under RN the
 * double rounding can differ in rare ties. */
SH4FP_API float sh4_fmac(float x, float y, float z)
{
    SH4FP_NOCONTRACT
    double r = (double)x * (double)y + (double)z;
    return sh4_canon((float)r);
}

/* FSQRT (single precision), SH-4 NaN */
SH4FP_API float sh4_fsqrt(float x)
{
    return sh4_canon(SH4FP_SQRTF(x));
}

/* FTRV: out[i] = sum_j XF[i + 4j] * v[j], products and sum in double,
 * rounded once.  APPROXIMATE like FIPR.  xm is XMTRX as the XF bank holds
 * it: XF[k] = float k of the NJS_MATRIX the SDK loaded (FMOV pair loads), so
 * out = v * M in Ninja's row-vector convention.  out may alias v. */
SH4FP_API void sh4_ftrv(const float *xm, const float *v, float *out)
{
    SH4FP_NOCONTRACT
    float r[4];
    int i;
    for (i = 0; i < 4; i++) {
        double s = (double)xm[i] * v[0];
        s += (double)xm[i + 4] * v[1];
        s += (double)xm[i + 8] * v[2];
        s += (double)xm[i + 12] * v[3];
        r[i] = sh4_canon((float)s);
    }
    for (i = 0; i < 4; i++)
        out[i] = r[i];
}

/* FTRC: truncation with SH-4 saturation (>= 2^31 -> 0x7FFFFFFF,
 * < -2^31 -> 0x80000000, NaN -> 0x80000000) */
SH4FP_API uint32_t sh4_ftrc(float f)
{
    uint32_t u = sh4_f2u(f);
    if ((u & 0x7FFFFFFFu) > 0x7F800000u)
        return 0x80000000u;                     /* NaN */
    if (f >= 2147483648.0f)
        return 0x7FFFFFFFu;
    if (f < -2147483648.0f)
        return 0x80000000u;
    return (uint32_t)(int32_t)f;
}

/* ------------------------------------------------------------------ */
/* FSCA                                                                */

/* Quarter-wave table: sh4_fsca_q[i] = float bits of sin(i * 2pi/65536),
 * i = 0..0x4000, exactly 0 at i = 0 and 1 at i = 0x4000.  Generated from the
 * harness's fsca_build() (harness/src/sh4_fsca_ref.c: host sin() rounded to
 * nearest) by harness/tools/dump_fsca.py into libsa1/src/rt/rt_fsca_q.c.
 * The hardware uses an internal table (documented error bound ~2^-21), so
 * this is APPROXIMATE with respect to a real Dreamcast and exact with
 * respect to the reference traces. */
extern const uint32_t sh4_fsca_q[0x4001];

/* sine of (angle & 0xFFFF) * 2pi/65536 with the interpreter's quadrant
 * rules: q[a] in the first quadrant, q[0x8000 - a] in the second, the same
 * negated in the third and fourth; +0 (never -0) at 0 and 0x8000 */
SH4FP_INL float sh4_fsca_sin16(uint32_t angle)
{
    uint32_t a = angle & 0x7FFFu;
    uint32_t i = a <= 0x4000u ? a : 0x8000u - a;
    uint32_t u = sh4_fsca_q[i];
    if ((angle & 0x8000u) != 0 && i != 0)
        u ^= 0x80000000u;
    return sh4_u2f(u);
}

/* FSCA FPUL,DRn: *s = FR[n] = sin, *c = FR[n+1] = cos = sin(angle + 0x4000);
 * only the low 16 bits of the angle count */
SH4FP_API void sh4_fsca(uint32_t angle, float *s, float *c)
{
    float sv = sh4_fsca_sin16(angle);
    float cv = sh4_fsca_sin16(angle + 0x4000u);
    *s = sv;
    *c = cv;
}

#ifdef __cplusplus
}
#endif

#endif /* SH4_FPMODEL_H */
