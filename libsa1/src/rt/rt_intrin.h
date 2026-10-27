/*
 * rt_intrin.h - the SHC intrinsics of the lowered game code as static inlines (libsa1 profile ga64;
 * sadc docs/libsa1_integration.md 4.3 and 8.2).
 *
 * pclower (R7) renames the SHC intrinsics of the game sources to the rt names of sa1/guest.h:
 *
 *   _builtin_fabsf -> sa1g_fabsf     FABS   bit operation (NaNs included)
 *   _builtin_sqrtf -> sa1g_sqrtf     FSQRT  sqrtss, SH-4 NaN
 *   _builtin_fsrra -> sa1g_fsrra     FSRRA  1/sqrt in double, the interpreter's special cases
 *   _builtin_fipr  -> sa1g_fipr      FIPR   4 products summed in double, rounded once
 *   _builtin_ftrv  -> sa1g_ftrv      FTRV   the same per row, on the ticking world's XMTRX
 *   _builtin_fsca  -> sa1g_fsca      FSCA   the generated quarter-wave table (rt_fsca_q.c)
 *
 * The models are the ones the reference traces were recorded with: include/pc/sh4_fpmodel.h of sadc,
 * shared with the interpreter harness (decision D6). The world core keeps out-of-line definitions of
 * the same names (src/world/sa1_world_intrin.c, on the same header) for callers that need an address;
 * this header turns every CALL in a lowered translation unit into the inline model, so the hot paths
 * (sa1g_fabsf has 109 uses, sa1g_fipr 40) cost no call. Both are the same functions of the same
 * header, so inlining changes no result bit.
 *
 * Usage: LIB/build.py force-includes it into every lowered game / runtime object, after
 * src/world/sa1g_text.h (so the inline copies land in the game-text section too):
 *
 *     -DSA1G_TEXT_PRAGMA -include src/world/sa1g_text.h -include src/rt/rt_intrin.h
 *
 * How: sa1/guest.h is included first (its extern prototypes of sa1g_fabsf ... stay as they are; the
 * lowered file's own #include "sa1/guest.h" is then a no-op), then function-like macros map each call
 * sa1g_X(...) to the static inline sa1g_i_X(...). A redeclaration "float sa1g_fabsf(float x);" in a
 * lowered text becomes a redeclaration of the static inline (it inherits internal linkage: valid C).
 *
 * Lowered runtime files (rt_*.c) contain the PREPROCESSED text of sh4_fpmodel.h (pclower expands project
 * headers), i.e. their own static definitions of sh4_fipr, sh4_f2u ... and no include guard. This header
 * therefore includes sh4_fpmodel.h under private names (sa1g_m_*): the force-included copy can never
 * collide with the copy inside a lowered file. Only the data symbol sh4_fsca_q keeps its name.
 *
 * Requirements: the game flags (sadc tools/pc_cflags.txt: scalar SSE on x86, -ffp-contract=off,
 * -frounding-math) and the world's MXCSR (round toward zero, FTZ, DAZ) while the code runs.
 */
#ifndef SA1_RT_INTRIN_H
#define SA1_RT_INTRIN_H

#include <stdint.h>

#include "sa1/guest.h"

/* ---- the FP models under private names ------------------------------------------------------- */
#if defined(SH4_FPMODEL_H)
#error "rt_intrin.h must be force-included before anything that includes pc/sh4_fpmodel.h"
#endif
#if defined(SH4_FPMODEL_EXTERN)
#error "rt_intrin.h needs the static inline models (SH4_FPMODEL_EXTERN is set)"
#endif
#define sh4_f2u sa1g_m_f2u
#define sh4_u2f sa1g_m_u2f
#define sh4_canon sa1g_m_canon
#define sh4_canond sa1g_m_canond
#define sh4_fsrra sa1g_m_fsrra
#define sh4_fipr sa1g_m_fipr
#define sh4_fmac sa1g_m_fmac
#define sh4_fsqrt sa1g_m_fsqrt
#define sh4_ftrv sa1g_m_ftrv
#define sh4_ftrc sa1g_m_ftrc
#define sh4_fsca_sin16 sa1g_m_fsca_sin16
#define sh4_fsca sa1g_m_fsca
#include "pc/sh4_fpmodel.h"
#undef sh4_f2u
#undef sh4_u2f
#undef sh4_canon
#undef sh4_canond
#undef sh4_fsrra
#undef sh4_fipr
#undef sh4_fmac
#undef sh4_fsqrt
#undef sh4_ftrv
#undef sh4_ftrc
#undef sh4_fsca_sin16
#undef sh4_fsca
/* the guard goes: a lowered text that includes the header for real gets its own (public) copy */
#undef SH4_FPMODEL_H

#if defined(__GNUC__) || defined(__clang__)
#define SA1G_RT_INLINE static inline __attribute__((always_inline, unused))
#else
#define SA1G_RT_INLINE static inline
#endif

/* ---- the intrinsics ----------------------------------------------------------------------------- */
/* FABS: a bit operation (clears the sign of NaNs too, no canonicalisation) */
SA1G_RT_INLINE float sa1g_i_fabsf(float x)
{
    return sa1g_m_u2f(sa1g_m_f2u(x) & 0x7FFFFFFFu);
}

/* FSQRT */
SA1G_RT_INLINE float sa1g_i_sqrtf(float x)
{
    return sa1g_m_fsqrt(x);
}

/* FSRRA */
SA1G_RT_INLINE float sa1g_i_fsrra(float x)
{
    return sa1g_m_fsrra(x);
}

/* FIPR FVm,FVn: a and b are 4-float vectors (host pointers into the arena or the stack) */
SA1G_RT_INLINE float sa1g_i_fipr(float *a, float *b)
{
    return sa1g_m_fipr(a, b);
}

/* FTRV XMTRX,FVn: out = v * XMTRX of the ticking world (sa1_rt_xmtrx, set on world entry); out may
 * alias v */
SA1G_RT_INLINE void sa1g_i_ftrv(float *v, float *out)
{
    sa1g_m_ftrv(sa1_rt_xmtrx, v, out);
}

/* FSCA FPUL,DRn: sine and cosine of (ang & 0xFFFF) * 2pi / 65536 */
SA1G_RT_INLINE void sa1g_i_fsca(int32_t ang, float *s, float *c)
{
    sa1g_m_fsca((uint32_t)ang, s, c);
}

/* every call in the lowered text goes to the inline model */
#define sa1g_fabsf(x) sa1g_i_fabsf(x)
#define sa1g_sqrtf(x) sa1g_i_sqrtf(x)
#define sa1g_fsrra(x) sa1g_i_fsrra(x)
#define sa1g_fipr(a, b) sa1g_i_fipr(a, b)
#define sa1g_ftrv(v, out) sa1g_i_ftrv(v, out)
#define sa1g_fsca(ang, s, c) sa1g_i_fsca(ang, s, c)

#endif /* SA1_RT_INTRIN_H */
