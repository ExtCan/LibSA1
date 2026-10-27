/*
 * rt_libm.c - the SHC C library maths of the game (code region
 * 8C10C10C-8C111194): acosf, asinf, the two atan2 wrappers, ceilf, floorf,
 * the modff fraction behind "njRandom", and the cores they call
 * (docs/libsa1_integration.md 4.1, 4.2, 4.5; README.md in this directory).
 *
 * Clean-room: written from the observed behaviour of the SDK functions (the
 * operation order, FPU instructions, branch conditions and edge results were
 * read off the original code with tools/funcinfo.py) on top of the shared
 * FPU models; verified bit-exact against the original code in the harness
 * (README.md, section "rt_libm.c").
 *
 * No game bytes: every constant the library loads from its data block
 * D_8C1601F4..D_8C1603BC (the NaN it returns, +-0, +-inf, +-1, +-0.5, pi,
 * pi/2, pi/4, the atan breakpoints and the atan polynomial D_8C1603A4) is
 * read in place through its symbol, i.e. from the user's 1ST_READ image at
 * run time.  The only literals are IEEE bit masks and the powers of two the
 * code carries as instruction immediates / pool words (2^31, 2^32).
 *
 * Behaviour reproduced (all single precision, round toward zero, FTZ/DAZ):
 *
 *   wrappers   func_8C10D008 (acosf), func_8C10D058 (asinf), func_8C10D0A8
 *              and atan2f (identical bodies, core 8C10E6A0), func_8C10D148
 *              (ceilf), func_8C10D198 (floorf), func_8C10EB50 (modff): call
 *              the core, then errno B_8C7AC108 = 0x44C if the result is
 *              +-inf, 0x44D if it is a NaN (otherwise errno is untouched).
 *   trunc      func_8C63A994 (SDK area): x < 0 ? -floorf(|x|) : floorf(|x|)
 *              through the floorf wrapper (so errno as there).
 *   NaN        the library's own NaN result is the word D_8C1601F4
 *              (0x7F800001 in the US 1.005 image), returned by a plain load,
 *              so its bits survive; arithmetic on it gives the SH-4 qNaN
 *              0x7FBFFFFF (acosf of |x| > 1, ceilf/floorf/modff of a NaN).
 *   asin       NaN or |x| > 1 (inf included) -> NaN; +-1 -> +-pi/2; +-0 (and
 *              denormals, DAZ) -> x; else 2 * atan(t) with
 *              t = x / (1 + sqrt(1 - x*x)) for |x| < 0.5,
 *              t = (1 - sqrt(1 - x*x)) / x otherwise; the doubling is the
 *              ldexp core 8C10E7B8.
 *   acos       pi/2 - asin(x) (one FSUB).
 *   atan       NaN -> NaN; |x| >= 2^32 (inf included) -> +-pi/2; +-0 -> x;
 *              +-1 -> +-pi/4; |x| > 1 folds to pi/4 + atan((|x| - 1) /
 *              (|x| + 1)); the reduced argument t in [0, 1) picks one of 8
 *              eighths through the remainder/quotient core 8C10FBB8
 *              (t, 0.125); eighths 1-2, 3-4, 5-6, 7 shift by the breakpoints
 *              0.25, 0.5, 0.75, 1 with the stored atan values, then the odd
 *              polynomial 8C10FB94 (5 FMAC Horner steps over
 *              D_8C1603A4[0..5]); sign last.
 *   atan2      atan(y / x) (one FDIV, NaN for 0/0 and inf/inf, so
 *              atan2(0, 0) is NaN); if x has the sign bit set, pi or -pi
 *              (by the sign bit of y) is added.  No other quadrant logic.
 *   ceil/floor FTRC + FLOAT when the exponent field is below that of 2^32,
 *              sign bit ORed back (so -0.5 -> -0), then +1 / -1 when the
 *              truncation changed a positive (ceil) / negative (floor) x.
 *              FTRC saturation is kept: ceilf(3e9) = 2147483520 (FLOAT of
 *              0x7FFFFFFF, RZ), floorf(-3e9) = -2^31; |x| >= 2^32, inf
 *              and NaN pass through (a positive NaN in ceilf / a negative
 *              one in floorf goes through the FADD -> 0x7FBFFFFF).
 *   modf       integer part as ceil/floor's truncation (bits stored as they
 *              are, NaN payload included), fraction x - ipart (inf ->
 *              ipart inf, fraction NaN).
 *   ldexp      exponent arithmetic on the bits; NaN -> the library NaN,
 *              +-inf / +-0 / denormal -> +-inf / +-0; overflow / underflow
 *              decided by the sign of n (+-inf / +-0), no denormal results.
 *   remainder  8C10FBB8(x, y, &q): x inf/NaN, y NaN or y == 0 -> library
 *              NaN, q = 0; y inf -> x, q = 0; else d = |x| / |y|, td =
 *              trunc(d) as above, q = FTRC(td > 2^31 ? td - 2^32 : td),
 *              result |x| - (q as unsigned) * |y| with the sign bit of x
 *              ORed in.
 *
 * Calling convention: every function here is a plain SHC-ABI C function
 * (FR4/FR5/R4 in, FR0 out, *ip / *q through memory).  The SDK leaves
 * nothing else in registers that a caller reads: the only library callers
 * of the cores (8C10CE9C, 8C10E878 -> ldexp; 8C10FCBC -> 8C10FBB8) take FR0
 * and the memory results only; 8C10FCBC's unreachable default case reads
 * FR5, which the original 8C10FBB8 leaves untouched, as the adapter does.
 * The cores are exported under their own addresses so the harness can test
 * them alone; the wrappers use the static versions.
 *
 * 32-bit x86 note: the i386 C ABI returns a float in the x87 register ST(0);
 * loading a NaN whose x86 quiet bit (bit 22) is clear -- the library NaN
 * 0x7F800001 and the SH-4 qNaN 0x7FBFFFFF both are -- sets that bit
 * (0x7FC00001, 0x7FFFFFFF).  So a NaN *returned* by one of these functions
 * reaches its caller with a different payload on i386 (in the harness: the
 * adapter's FR0); it is still a NaN, which every game caller treats alike
 * (errno test, FPU arithmetic -> 0x7FBFFFFF, FTRC -> 0x80000000).  Inside
 * this file NaN bits never pass through a float return before they are
 * tested or stored (errno, *ip), and the 64-bit builds return them in XMM0
 * unchanged.
 */
#include "rt.h"
#include "sa1/engine.h"         /* the game's prototypes of the libm wrappers */

/* ------------------------------------------------------------------ */
/* SDK state and library constants, used in place                      */

extern Sint32 B_8C7AC108;       /* errno of the SHC library */
extern Float B_8C89033C;        /* njRandom's integer part (modff target) */

extern Float D_8C1601F4;        /* the library's NaN result */
extern Float D_8C1601F8;        /* +0 */
extern Float D_8C1601FC;        /* +inf */
extern Float D_8C160200;        /* 1 */
extern Float D_8C160204;        /* 0.5 */
extern Float D_8C160214;        /* pi */
extern Float D_8C16021C;        /* pi/2 */
extern Float D_8C160220;        /* pi/4 */
extern Float D_8C160228;        /* -0 */
extern Float D_8C16022C;        /* -inf */
extern Float D_8C160230;        /* -1 */
extern Float D_8C160234;        /* -0.5 */
extern Float D_8C160238;        /* -0.25 */
extern Float D_8C160244;        /* -pi */
extern Float D_8C16038C;        /* atan(0.25) */
extern Float D_8C160390;        /* atan(0.5) */
extern Float D_8C160394;        /* atan(0.75) */
extern Float D_8C160398;        /* 0.75 */
extern Float D_8C16039C;        /* -0.75 */
extern Float D_8C1603A0;        /* 0.125: width of the atan eighths */
extern Float D_8C1603A4[6];     /* atan polynomial in z^2, highest power first */

/* the cores are exported at their own addresses (harness: diff them alone) */
Float func_8C10E590(Float x);
Float func_8C10E5AC(Float x);
Float func_8C10E6A0(Float y, Float x);
Float func_8C10E6F8(Float x);
Float func_8C10E758(Float x);
Float func_8C10E7B8(Float x, Sint32 n);
Float func_8C10EB50(Float x, Float *ip);
Float func_8C10EF84(Float x);
Float func_8C10F164(Float x, Float *ip);
Float func_8C10FB94(Float z);
Float func_8C10FBB8(Float x, Float y, Sint32 *q);

#define RT_SIGN     0x80000000u
#define RT_EXP      0x7F800000u
#define RT_MANT     0x007FFFFFu
#define RT_EXP_2_32 0x4F800000u         /* exponent field of 2^32 */

/* the classification the cores start with: sign, zero (FCMP/EQ with 0, so
 * denormals count as zero under DAZ), infinite, NaN */
#define RT_CL_NAN   1u
#define RT_CL_INF   2u
#define RT_CL_ZERO  4u
#define RT_CL_NEG   8u

static Uint32 rt_classify(Float x)
{
    Uint32 u = RT_F2U(x);
    Uint32 cl = (u & RT_SIGN) ? RT_CL_NEG : 0u;

    if (x == 0.0f)
        cl |= RT_CL_ZERO;
    else if ((u & 0x7FFFFFFFu) == RT_EXP)
        cl |= RT_CL_INF;
    else if (!(x == x))
        cl |= RT_CL_NAN;
    return cl;
}

/* ------------------------------------------------------------------ */
/* errno after a wrapper's core                                        */

static void rt_errno_check(Float r)
{
    Uint32 u = RT_F2U(r);

    if ((u & RT_EXP) == RT_EXP) {
        if ((u & RT_MANT) != 0)
            B_8C7AC108 = 0x44D;         /* NaN result */
        else
            B_8C7AC108 = 0x44C;         /* infinite result */
    }
}

/* ------------------------------------------------------------------ */
/* 8C10E7B8: x * 2^n on the exponent field                             */

static Float rt_ldexp(Float x, Sint32 n)
{
    Uint32 u = RT_F2U(x);
    Uint32 e = u & RT_EXP;
    Sint32 k;

    if (e == RT_EXP) {
        if ((u & RT_MANT) != 0)
            return D_8C1601F4;
        return (u & RT_SIGN) ? D_8C16022C : D_8C1601FC;
    }
    if (e == 0)                                         /* +-0, denormal */
        return (u & RT_SIGN) ? D_8C160228 : D_8C1601F8;
    k = (Sint32)((e >> 23) + (Uint32)n);                /* 32-bit wrap like ADD */
    if (k > 0 && k < 255)
        return RT_U2F((u & RT_SIGN) | ((Uint32)k << 23) | (u & RT_MANT));
    if (n >= 0)                                         /* overflow */
        return (u & RT_SIGN) ? D_8C16022C : D_8C1601FC;
    return (u & RT_SIGN) ? D_8C160228 : D_8C1601F8;    /* underflow */
}

/* ------------------------------------------------------------------ */
/* 8C10FB94: z * P(z^2), P of degree 5 by FMAC Horner steps            */

static Float rt_atan_poly(Float z)
{
    const Float *c = D_8C1603A4;
    Float z2 = RT_FMUL(z, z);
    Float p = c[0];
    Sint32 i;

    for (i = 1; i <= 5; i++)
        p = RT_FMAC(z2, p, c[i]);                       /* z2 * p + c[i], one rounding */
    return RT_FMUL(z, p);
}

/* ------------------------------------------------------------------ */
/* truncation toward zero as the library does it: FTRC + FLOAT when the
 * exponent field is below 2^32's (so 2^31 <= |x| < 2^32 saturates), the
 * sign bit of x ORed back in (-0.5 -> -0).  Returned as bits: the result is
 * x itself for |x| >= 2^32, inf and NaN, and a NaN must keep its payload. */
static Uint32 rt_trunc_bits(Float x)
{
    Uint32 u = RT_F2U(x);
    Uint32 t = u;

    if ((u & RT_EXP) < RT_EXP_2_32)
        t = RT_F2U(RT_FLOAT(RT_FTRC(x)));
    return t | (u & RT_SIGN);
}

/* ------------------------------------------------------------------ */
/* 8C10FBB8: remainder of |x| / |y| with the quotient in *q            */

static Float rt_remquo(Float x, Float y, Sint32 *q)
{
    Uint32 ux = RT_F2U(x), uy = RT_F2U(y);
    Float ax, ay, td, tq, r;
    Sint32 k;

    if ((ux & RT_EXP) == RT_EXP
        || ((uy & RT_EXP) == RT_EXP && (uy & RT_MANT) != 0)
        || y == 0.0f) {
        *q = 0;
        return D_8C1601F4;
    }
    if ((uy & RT_EXP) == RT_EXP) {                      /* y infinite */
        *q = 0;
        return x;
    }
    ax = RT_U2F(ux & 0x7FFFFFFFu);
    ay = RT_U2F(uy & 0x7FFFFFFFu);
    td = RT_U2F(rt_trunc_bits(ax / ay));                /* finite: RZ never overflows to inf */
    if (td > 0x1p31f)
        td = td + -0x1p32f;
    k = RT_FTRC(td);
    *q = k;
    tq = RT_FLOAT(k);
    if (k < 0)
        tq = tq + 0x1p32f;                              /* the quotient as unsigned */
    r = ax - tq * ay;
    return RT_U2F(RT_F2U(r) | (ux & RT_SIGN));
}

/* ------------------------------------------------------------------ */
/* 8C10EF84: atanf                                                      */

static Float rt_atan(Float x)
{
    Uint32 cl = rt_classify(x);
    Uint32 neg = cl & RT_CL_NEG;
    Float ax, t, base, z, r;
    Sint32 e, q;

    ax = neg ? RT_FNEG(x) : x;
    if (cl & RT_CL_NAN)
        return D_8C1601F4;
    e = (Sint32)((RT_F2U(ax) & RT_EXP) >> 23) - 127;
    if (e >= 32)                                        /* |x| >= 2^32, inf */
        return neg ? RT_FNEG(D_8C16021C) : D_8C16021C;
    if (cl & RT_CL_ZERO)
        return x;
    if (D_8C160200 > ax) {
        base = D_8C1601F8;
        t = ax;
    } else if (ax == D_8C160200) {
        return neg ? RT_FNEG(D_8C160220) : D_8C160220;
    } else {
        Float den = D_8C160200 + ax;
        base = D_8C160220;
        t = (D_8C160230 + ax) / den;                    /* < 1 under RZ */
    }
    (void)rt_remquo(t, D_8C1603A0, &q);                 /* q = eighth of [0, 1) */
    switch (q) {
    case 0:
        z = t;
        break;
    case 1:
    case 2: {
        Float num = D_8C160238 + t;
        base = base + D_8C16038C;
        z = num / (D_8C160200 + rt_ldexp(t, -2));
        break;
    }
    case 3:
    case 4: {
        Float num = D_8C160234 + t;
        base = base + D_8C160390;
        z = num / (D_8C160200 + rt_ldexp(t, -1));
        break;
    }
    case 5:
    case 6: {
        Float num = t + D_8C16039C;
        Float den = RT_FMAC(D_8C160398, t, D_8C160200);
        base = base + D_8C160394;
        z = num / den;
        break;
    }
    case 7: {
        Float den = D_8C160200 + t;
        Float num = D_8C160230 + t;
        base = base + D_8C160220;
        z = num / den;
        break;
    }
    default:            /* unreachable for t < 1; the original returns FR5 = 0.125 */
        r = D_8C1603A0;
        return neg ? RT_FNEG(r) : r;
    }
    r = base + rt_atan_poly(z);
    return neg ? RT_FNEG(r) : r;
}

/* ------------------------------------------------------------------ */
/* 8C10E5AC: asinf, 8C10E590: acosf                                     */

static Float rt_asin(Float x)
{
    Uint32 cl = rt_classify(x);
    Float ax, t, s;

    if (cl & RT_CL_NAN)
        return D_8C1601F4;
    ax = (cl & RT_CL_NEG) ? RT_FNEG(x) : x;
    if (ax > D_8C160200)                                /* |x| > 1, inf */
        return D_8C1601F4;
    if (ax == D_8C160200)
        return (cl & RT_CL_NEG) ? RT_FNEG(D_8C16021C) : D_8C16021C;
    if (cl & RT_CL_ZERO)
        return x;
    s = RT_FSQRT(D_8C160200 - x * x);
    if (D_8C160204 > ax)
        t = x / (D_8C160200 + s);
    else
        t = (D_8C160200 - s) / x;
    return rt_ldexp(rt_atan(t), 1);
}

static Float rt_acos(Float x)
{
    return RT_FSUB(D_8C16021C, rt_asin(x));
}

/* ------------------------------------------------------------------ */
/* 8C10E6A0: atan2 core                                                 */

static Float rt_atan2(Float y, Float x)
{
    Float r = rt_atan(RT_FDIV(y, x));

    if ((RT_F2U(x) & RT_SIGN) == 0)
        return r;
    if ((RT_F2U(y) & RT_SIGN) == 0)
        return RT_FADD(D_8C160214, r);
    return RT_FADD(D_8C160244, r);
}

/* ------------------------------------------------------------------ */
/* 8C10E6F8: ceilf, 8C10E758: floorf, 8C10F164: modff                  */

static Float rt_ceil(Float x)
{
    Float t = RT_U2F(rt_trunc_bits(x));

    if ((RT_F2U(x) & RT_SIGN) != 0 || t == x)
        return t;
    return RT_FADD(D_8C160200, t);
}

static Float rt_floor(Float x)
{
    Float t = RT_U2F(rt_trunc_bits(x));

    if ((RT_F2U(x) & RT_SIGN) == 0 || t == x)
        return t;
    return RT_FADD(D_8C160230, t);
}

/* *ip = the truncation (stored as bits), returns x - *ip */
static Float rt_modf(Float x, Float *ip)
{
    Uint32 tb = rt_trunc_bits(x);

    SH4FP_MEMCPY(ip, &tb, sizeof tb);
    return RT_FSUB(x, RT_U2F(tb));
}

/* 8C10EB50: the modff wrapper; the core writes a local, the wrapper copies
 * it to *ip before the errno check */
static Float rt_modff(Float x, Float *ip)
{
    Float ipart;
    Float r = rt_modf(x, &ipart);

    SH4FP_MEMCPY(ip, &ipart, sizeof ipart);
    rt_errno_check(r);
    return r;
}

/* ------------------------------------------------------------------ */
/* exported: wrappers                                                   */

/* 8C10D008 acosf */
Float func_8C10D008(Float x)
{
    Float r = rt_acos(x);

    rt_errno_check(r);
    return r;
}

/* 8C10D058 asinf */
Float func_8C10D058(Float x)
{
    Float r = rt_asin(x);

    rt_errno_check(r);
    return r;
}

/* 8C10D0A8 atan2 (y, x): the same body as atan2f */
Float func_8C10D0A8(Float y, Float x)
{
    Float r = rt_atan2(y, x);

    rt_errno_check(r);
    return r;
}

/* 8C10D0F8 */
Float atan2f(Float y, Float x)
{
    Float r = rt_atan2(y, x);

    rt_errno_check(r);
    return r;
}

/* 8C10D148 ceilf */
Float func_8C10D148(Float x)
{
    Float r = rt_ceil(x);

    rt_errno_check(r);
    return r;
}

/* 8C10D198 floorf */
Float func_8C10D198(Float x)
{
    Float r = rt_floor(x);

    rt_errno_check(r);
    return r;
}

/* 8C10EB50 modff */
Float func_8C10EB50(Float x, Float *ip)
{
    return rt_modff(x, ip);
}

/* 8C63A980 "njRandom": a jump to 8C10EB50 with the integer part going to
 * B_8C89033C (FR4 = n untouched); returns the fraction of n */
Float njRandom(Float n)
{
    return rt_modff(n, &B_8C89033C);
}

/* 8C63A994: truncation toward zero (the STG01 ocean's 25-unit grid snap,
 * code_0C909F20.c): FR4 = |x| (FABS in the delay slot of the branch), then
 * the floorf wrapper 8C10D198 (errno included), whose result is negated
 * (FNEG) when 0 > x; a NaN and -0 take the other path (floorf(|x|), a
 * tail call) */
Float func_8C63A994(Float x)
{
    Float a = RT_FABS(x);

    if (0.0f > x) {
        return RT_FNEG(func_8C10D198(a));
    }
    return func_8C10D198(a);
}

/* ------------------------------------------------------------------ */
/* exported: cores (only the library itself calls them)                 */

/* 8C10E590 */
Float func_8C10E590(Float x)
{
    return rt_acos(x);
}

/* 8C10E5AC */
Float func_8C10E5AC(Float x)
{
    return rt_asin(x);
}

/* 8C10E6A0 */
Float func_8C10E6A0(Float y, Float x)
{
    return rt_atan2(y, x);
}

/* 8C10E6F8 */
Float func_8C10E6F8(Float x)
{
    return rt_ceil(x);
}

/* 8C10E758 */
Float func_8C10E758(Float x)
{
    return rt_floor(x);
}

/* 8C10E7B8 */
Float func_8C10E7B8(Float x, Sint32 n)
{
    return rt_ldexp(x, n);
}

/* 8C10EF84 */
Float func_8C10EF84(Float x)
{
    return rt_atan(x);
}

/* 8C10F164 */
Float func_8C10F164(Float x, Float *ip)
{
    return rt_modf(x, ip);
}

/* 8C10FB94 */
Float func_8C10FB94(Float z)
{
    return rt_atan_poly(z);
}

/* 8C10FBB8 */
Float func_8C10FBB8(Float x, Float y, Sint32 *q)
{
    return rt_remquo(x, y, q);
}
