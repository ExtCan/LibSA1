#ifndef COMMON_H
#define COMMON_H

/*
 * Base header for every decompiled file. One source, two targets
 * (rules: docs/portability.md):
 *
 *   default      SHC 5.1r03 matching build. Every type and macro below expands
 *                to exactly what the matched code was written against; nothing
 *                in this header may change the generated SH-4 code.
 *   TARGET_PC    libsa1 (x86 / x86-64, GCC/Clang/MSVC). Fixed-size types and
 *                helpers that reproduce SH-4 semantics. Compile with
 *                -DTARGET_PC -DNON_MATCHING -fwrapv -fno-strict-aliasing
 *                -ffp-contract=off -fsigned-char (and -msse2 -mfpmath=sse on
 *                32-bit x86); never -ffast-math.
 *
 * SA1_PTR32 is 1 when pointers are 32 bits (SHC, x86-32). Only then do the
 * structs have their DC layout, and only then are the SA1_ASSERT_* layout
 * checks active. On 64-bit hosts the structs use the native layout and every
 * access must go through typed fields (never raw offsets).
 *
 * SA1_GA (with TARGET_PC; pclower and the libsa1 guest arena,
 * docs/libsa1_integration.md): SA1_UPTR is Uint32 (raw guest word) and
 * SA1_FMAC is the double form below; include/task.h drops task.thp.
 * Nothing changes for SHC or for TARGET_PC without SA1_GA.
 */

#if defined(TARGET_PC)
#include <stddef.h>
#include <stdint.h>
#if UINTPTR_MAX == 0xFFFFFFFFu
#define SA1_PTR32 1
#else
#define SA1_PTR32 0
#endif
#else
#define TARGET_SH4 1
#define SA1_PTR32 1
#endif

/* Basic types (Katana / Ninja naming conventions) */
#if defined(TARGET_PC)
typedef int8_t             Sint8;
typedef uint8_t            Uint8;
typedef int16_t            Sint16;
typedef uint16_t           Uint16;
typedef int32_t            Sint32;     /* 32 bits on every host (long is 64-bit on LP64) */
typedef uint32_t           Uint32;
typedef float              Float;
typedef float              Double;     /* SHC -fpu=single: double is single precision */
typedef int                Int;
typedef unsigned int       Uint;
typedef void               Void;
typedef Sint32             Bool;
#if defined(SA1_GA)
typedef Uint32             SA1_UPTR;   /* SA1_GA: the raw 32-bit guest word (pclower R6b) */
#else
typedef uintptr_t          SA1_UPTR;   /* integer that can hold a pointer */
#endif
#else
typedef signed char        Sint8;
typedef unsigned char      Uint8;
typedef signed short       Sint16;
typedef unsigned short     Uint16;
typedef signed long        Sint32;
typedef unsigned long      Uint32;
typedef float              Float;
typedef double             Double;
typedef int                Int;
typedef unsigned int       Uint;
typedef void               Void;
typedef Sint32             Bool;
typedef Uint32             SA1_UPTR;
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif
#define TRUE  1
#define FALSE 0

/*
 * PC_ONLY(x) / SH_ONLY(x): code that exists on one target only, e.g. an
 * initialisation the original never did (PC_ONLY(v = 0;)) or a matching-only
 * idiom. The other target sees nothing.
 */
#if defined(TARGET_PC)
#define PC_ONLY(x) x
#define SH_ONLY(x)
#else
#define PC_ONLY(x)
#define SH_ONLY(x) x
#endif

/*
 * Layout checks (use at file scope, followed by ';'):
 *   SA1_ASSERT_SIZE(type, size)            type must be a single identifier
 *   SA1_ASSERT_OFFSET(type, member, off)   member may be a designator (mj.spdp)
 * SHC: sizes are checked by the compiler itself (negative array size);
 * SHC cannot fold offsetof into a constant expression, so offsets are checked
 * by "py -3.12 tools/pc_check.py --layout" (SHC data table) and by every
 * 32-bit PC compile (_Static_assert). 64-bit PC: no layout checks (native
 * layout by design).
 */
#define SA1_PASTE3_(a, b, c) a##b##_##c
#define SA1_PASTE3(a, b, c) SA1_PASTE3_(a, b, c)
#if defined(TARGET_PC) && SA1_PTR32
#define SA1_ASSERT_SIZE(type, size) \
    _Static_assert(sizeof(type) == (size), "sizeof(" #type ") != " #size)
#define SA1_ASSERT_OFFSET(type, member, off) \
    _Static_assert(offsetof(type, member) == (off), "offsetof(" #type ", " #member ") != " #off)
#elif defined(TARGET_PC)
#define SA1_ASSERT_SIZE(type, size) typedef int SA1_PASTE3(sa1_size_, type, __LINE__)
#define SA1_ASSERT_OFFSET(type, member, off) typedef int SA1_PASTE3(sa1_off_, type, __LINE__)
#else
/* No-op form is a typedef on purpose: an extern declaration would shift
 * SHC's internal label numbers (harmless, but the .src would differ). */
#define SA1_ASSERT_SIZE(type, size) \
    typedef char sa1_size_##type[(sizeof(type) == (size)) ? 1 : -1]
#define SA1_ASSERT_OFFSET(type, member, off) typedef int SA1_PASTE3(sa1_off_, type, __LINE__)
#endif

/*
 * SH-4 semantics helpers. On SHC each one expands to the plain C expression
 * the matching code needs; on PC it reproduces what the SH-4 does where C
 * (or x86) differs. See docs/portability.md for when each is required.
 *
 *   SA1_DIV/SA1_MOD (signed), SA1_UDIV/SA1_UMOD (unsigned): integer division
 *       by a value that can be zero. SHC's __divls/__divlu/__modls/__modlu
 *       (8C10CA5C..8C10CC80) return 0 for a zero divisor (and set errno
 *       B_8C7AC108 = 0x44E) instead of trapping; x86 idiv raises #DE.
 *       INT_MIN / -1 also traps on x86; the DIV1 loop wraps (to confirm in the
 *       SH-4 interpreter harness).
 *   SA1_FTOI(f): float -> Sint32 with FTRC semantics (truncate; saturate to
 *       0x7FFFFFFF / 0x80000000 when out of range; NaN -> 0x80000000).
 *       C leaves out-of-range conversions undefined; cvttss2si gives
 *       0x80000000 for every out-of-range value.
 *   SA1_FMAC(a, b, c): a * b + c exactly where SHC emitted FMAC. SH-4 FMAC rounds
 *       once (fused); verified by the interpreter harness: natively compiled
 *       code is bit-exact only with fusion at FMAC sites and without it elsewhere
 *       (e.g. the FMUL/FMUL/FSUB cross product in func_8C027360). On PC it is
 *       fmaf(); libsa1 builds with -ffp-contract=off so nothing else is fused.
 *       Under SA1_GA (pclower / libsa1 guest arena, docs/libsa1_integration.md
 *       8.1) it is the double form (float)((double)a * b + c), i.e. sh4_fmac:
 *       the product of two floats is exact in double, and under round-to-zero
 *       rounding the sum to double and then to float equals one rounding to
 *       float; no libm call.
 */
#if defined(TARGET_PC)
#if defined(_MSC_VER) && !defined(__clang__)
#define SA1_INLINE static __inline
#else
#define SA1_INLINE static inline
#endif

SA1_INLINE Sint32 sa1_divls(Sint32 a, Sint32 b)
{
    if (b == 0) {
        return 0;
    }
    if (b == -1) {
        return (Sint32)(0u - (Uint32)a);
    }
    return a / b;
}

SA1_INLINE Sint32 sa1_modls(Sint32 a, Sint32 b)
{
    if (b == 0 || b == -1) {
        return 0;
    }
    return a % b;
}

SA1_INLINE Uint32 sa1_divlu(Uint32 a, Uint32 b)
{
    return b != 0 ? a / b : 0;
}

SA1_INLINE Uint32 sa1_modlu(Uint32 a, Uint32 b)
{
    return b != 0 ? a % b : 0;
}

SA1_INLINE Sint32 sa1_ftrc(Float f)
{
    if (f != f) {
        return -2147483647 - 1;
    }
    if (f >= 2147483648.0f) {
        return 2147483647;
    }
    if (f <= -2147483648.0f) {
        return -2147483647 - 1;
    }
    return (Sint32)f;
}

/* bit pattern -> float, for PC-only code (constants in matched code use the
 * literal window rule of docs/portability.md instead) */
SA1_INLINE Float sa1_f32(Uint32 bits)
{
    union {
        Uint32 u;
        Float f;
    } v;
    v.u = bits;
    return v.f;
}

#define SA1_DIV(a, b)  sa1_divls((Sint32)(a), (Sint32)(b))
#define SA1_MOD(a, b)  sa1_modls((Sint32)(a), (Sint32)(b))
#define SA1_UDIV(a, b) sa1_divlu((Uint32)(a), (Uint32)(b))
#define SA1_UMOD(a, b) sa1_modlu((Uint32)(a), (Uint32)(b))
#define SA1_FTOI(f)    sa1_ftrc(f)
#else
#define SA1_DIV(a, b)  ((a) / (b))
#define SA1_MOD(a, b)  ((a) % (b))
#define SA1_UDIV(a, b) ((a) / (b))
#define SA1_UMOD(a, b) ((a) % (b))
#define SA1_FTOI(f)    ((Sint32)(f))
#endif
#if defined(TARGET_PC)
#include <math.h>
/* __builtin_fmaf: one FMA instruction with -mfma, otherwise the C library's
 * (correctly rounded) fmaf; also immune to -fno-builtin */
#if defined(SA1_GA)
#define SA1_FMAC(a, b, c) ((Float)((double)(a) * (double)(b) + (double)(c)))
#elif defined(__GNUC__) || defined(__clang__)
#define SA1_FMAC(a, b, c) __builtin_fmaf((a), (b), (c))
#else
#define SA1_FMAC(a, b, c) fmaf((a), (b), (c))
#endif
#else
#define SA1_FMAC(a, b, c) ((a) * (b) + (c))
#endif

/*
 * SA1_FMAC_ACC(acc, a, b): "acc += a * b" where the original has FMAC. For SHC it
 * is exactly the accumulate statement (writing SA1_FMAC(a, b, acc) instead can
 * change SHC's load order and break matching); on PC it is fused like FMAC.
 */
#if defined(TARGET_PC)
#define SA1_FMAC_ACC(acc, a, b) ((acc) = SA1_FMAC((a), (b), (acc)))
#else
#define SA1_FMAC_ACC(acc, a, b) ((acc) += (a) * (b))
#endif

/*
 * ASM_STUB(prototype)
 *
 * Defines a function that is still assembly in this translation unit.
 * SHC only emits BSR/BRA (instead of JSR through the literal pool) for
 * callees defined in the same file, so neighbours that a decompiled
 * function reaches with BSR/BRA must be declared this way (prototype first if
 * the callee comes later in the file). The build's
 * splicer ignores these bodies and keeps the original assembly.
 *
 * The body must not be inlinable (with -speed SHC inlines or deletes small
 * same-file functions) and must clobber registers like a real function, so it
 * calls an external sink several times. It is never linked into the game.
 *
 * TARGET_PC: only the prototype; the PC build links the function from its
 * own decompiled C (or the harness), never from this stub.
 */
#if defined(TARGET_PC)
#define ASM_STUB(proto) proto;
#else
extern void __asm_stub_sink(void);
#define ASM_STUB(proto) proto { __asm_stub_sink(); __asm_stub_sink(); \
    __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); \
    __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); }
#endif

#endif