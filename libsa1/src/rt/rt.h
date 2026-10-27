/*
 * rt.h - common header of the libsa1 runtime: the clean-room reimplementation
 * of the Ninja / Kamui / Shinobi SDK and of the SHC C runtime that the
 * decompiled game code calls (docs/libsa1_integration.md section 4).  Rules
 * and verification: README.md in this directory.
 *
 * Every runtime file includes this header first.  It gives
 *   - the game dialect: SADC/include/common.h types (Sint32, Float, Angle ...)
 *     and the SDK prototypes of SADC/include/sa1/ninja_funcs.h, so a runtime
 *     definition that disagrees with what the game calls is a compile error;
 *   - SA1_XMTRX, the SH-4 matrix register the SDK keeps state in;
 *   - the SH-4 FPU models (SADC/include/pc/sh4_fpmodel.h) as RT_* helpers.
 *
 * Profiles (selected by the build, nothing to do in runtime code):
 *   harness   TARGET_PC_HARNESS (set by SADC/harness/src/harness_pre.h):
 *             compiled into the 32-bit SH-4 interpreter harness by
 *             "harness/build.py --non-matching --pc --rt" (design profile
 *             ga32, sa1h_rt).  Guest address == host address; SA1_XMTRX is
 *             the interpreter's XF bank (SH4.xf), so native runtime code and
 *             the original SDK code share one XMTRX and can call each other.
 *   libsa1    (default): SA1_XMTRX is the ticking world's matrix
 *             (design 6.1); the world core points sa1_rt_xmtrx at it on
 *             tick entry.  Globals live in the world's arena (pclower).
 */
#ifndef SA1_RT_H
#define SA1_RT_H

#include "common.h"
#include "sa1/ninja_funcs.h"
#include "pc/sh4_fpmodel.h"

/* ------------------------------------------------------------------ */
/* XMTRX                                                               */

/* SA1_XMTRX[k], k = 0..15: XF[k], i.e. float k of the NJS_MATRIX the SDK
 * last loaded (FMOV pair loads put m[k] into XF[k]); FTRV computes
 * out = v * M in Ninja's row-vector convention (translation in m[12..14]). */
#if defined(TARGET_PC_HARNESS)
extern float *const sa1h_xmtrx;         /* SADC/harness/src/sh4_intrin.c: SH4.xf.f */
#define SA1_XMTRX (sa1h_xmtrx)
#else
extern float *sa1_rt_xmtrx;             /* libsa1 world core: the ticking world's XMTRX[16] */
#define SA1_XMTRX (sa1_rt_xmtrx)
#endif

/* XMTRX = m (16 floats, bit-exact: NaN payloads survive like FMOV) */
SA1_INLINE void rt_xmtrx_load(const Float *m)
{
    SH4FP_MEMCPY(SA1_XMTRX, m, 16 * sizeof(Float));
}

/* m = XMTRX */
SA1_INLINE void rt_xmtrx_store(Float *m)
{
    SH4FP_MEMCPY(m, SA1_XMTRX, 16 * sizeof(Float));
}

/* ------------------------------------------------------------------ */
/* SH-4 FPU instructions (the interpreter's models; use these, never   */
/* host libm, and never a different formula for the same instruction)  */

#define RT_FIPR(va, vb)        sh4_fipr((va), (vb))              /* FIPR FVm,FVn: 4-term dot product, Float[4] each */
#define RT_FTRV(v, out)        sh4_ftrv(SA1_XMTRX, (v), (out))   /* FTRV XMTRX,FVn: out[4] = v[4] * XMTRX (may alias) */
#define RT_FTRV_M(m, v, out)   sh4_ftrv((m), (v), (out))         /* the same with a matrix in memory (XF layout) */
#define RT_FSCA(ang, s, c)     sh4_fsca((Uint32)(ang), (s), (c)) /* FSCA: *s = sin, *c = cos of (ang & 0xFFFF) */
#define RT_FSRRA(x)            sh4_fsrra(x)                      /* FSRRA: 1/sqrt(x) */
#define RT_FSQRT(x)            sh4_fsqrt(x)                      /* FSQRT */
#define RT_FMAC(a, b, c)       sh4_fmac((a), (b), (c))           /* FMAC FR0,FRm,FRn: a * b + c, one rounding */
#define RT_FTRC(x)             ((Sint32)sh4_ftrc(x))             /* FTRC: truncate, saturating, NaN -> 0x80000000 */
#define RT_FLOAT(i)            ((Float)(Sint32)(i))              /* FLOAT: rounds in the current mode (RZ) */

/* FADD FSUB FMUL FDIV: plain C operators are exact (same IEEE operation in
 * the same MXCSR mode); a NaN result is x86's 0xFFC00000, the SH-4 gives
 * 0x7FBFFFFF.  Use these where a NaN can come out (0 * inf, inf - inf,
 * 0 / 0 ...), so the result is bit-identical to the original. */
#define RT_FADD(a, b)          sh4_canon((a) + (b))
#define RT_FSUB(a, b)          sh4_canon((a) - (b))
#define RT_FMUL(a, b)          sh4_canon((a) * (b))
#define RT_FDIV(a, b)          sh4_canon((a) / (b))

/* FABS / FNEG are bit operations on the SH-4 (NaNs included) */
#define RT_FABS(x)             sh4_u2f(sh4_f2u(x) & 0x7FFFFFFFu)
#define RT_FNEG(x)             sh4_u2f(sh4_f2u(x) ^ 0x80000000u)

/* bits <-> float */
#define RT_F2U(x)              sh4_f2u(x)
#define RT_U2F(u)              sh4_u2f(u)

#endif /* SA1_RT_H */
