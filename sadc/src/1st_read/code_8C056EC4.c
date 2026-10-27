#include "common.h"
#include "ninja.h"
#include "sa1/ninja_types.h"

/*
 * 1ST_READ 8C056EC4-8C0572D8 (chunk 8C055D20):
 *   8C056EC4   sprite initialisation (njInitSprite-like)
 *   8C056EE4   unsigned -> decimal digits, written backwards
 *   8C056F08   unsigned -> lower-case hex digits, written backwards
 *   8C056F72   unsigned -> upper-case hex digits, written backwards
 *   8C056F94   the game's own small vsprintf: %[-+0][width] s c u d x X
 *              (no precision, no long modifier; an unknown conversion
 *              prints nothing but still consumes one argument)
 *   8C057272   sprintf wrapper
 */

#if defined(TARGET_PC)
#include <stdarg.h>
#include <string.h>
/* 8C10D3DC is the SHC library strcpy */
#define func_8C10D3DC(dst, src) strcpy((dst), (src))
#else
/*
 * SHC <stdarg.h> equivalent (the matching build has no SHC library headers).
 * Every argument slot is 4 bytes, so the misaligned branches are never taken;
 * this is the shape the original va_start/va_arg expansions compile to.
 * SHC passes the last named parameter of a variadic function on the stack,
 * so &fmt + 4 is the first variadic argument.
 */
typedef char *va_list;
#define va_start(ap, param) \
    ((ap) = ((Sint32)((char *)&(param) + sizeof(param)) & 3) ? (char *)&(param) + 8 \
                                                             : (char *)&(param) + sizeof(param))
#define va_arg(ap, type) \
    (*(type *)((ap) = ((Sint32)((ap) + sizeof(type)) & 3) ? (ap) + 8 : (ap) + sizeof(type), \
               ((Sint32)((ap) - sizeof(type)) & 3) ? (ap) - 8 : (ap) - sizeof(type)))
#define va_end(ap)
extern Uint32 strlen(const char *s);                    /* 8C10D3F0 */
extern char *func_8C10D3DC(char *dst, const char *src); /* 8C10D3DC strcpy */
#endif

/* njInitSprite-like: origin, scale 1, no rotation (caller 8C0EFA00 sets up
 * five sprites with texlist D_8C598DF0 and NJS_TEXANIM tables) */
void func_8C056EC4(NJS_SPRITE *sp, NJS_TEXLIST *tlist, NJS_TEXANIM *tanim)
{
    sp->tlist = tlist;
    sp->tanim = tanim;
    sp->ang = 0;
    sp->sx = 1.0f;
    sp->sy = 1.0f;
    sp->p.x = 0.0f;
    sp->p.y = 0.0f;
    sp->p.z = 0.0f;
}

/* unsigned decimal; the digits end just before p, returns the first digit */
char *func_8C056EE4(char *p, Uint32 n)
{
    do {
        *--p = (char)(n % 10 + '0');
        n /= 10;
    } while (n != 0);
    return p;
}

/* lower-case hex; the digits end just before p, returns the first digit */
char *func_8C056F08(char *p, Uint32 n)
{
    Sint32 d;

    do {
        d = (Sint32)(n & 0xF);
        *--p = (char)((d < 10 ? '0' : 'a' - 10) + d);
        n >>= 4;
    } while (n != 0);
    return p;
}

/* upper-case hex; the digits end just before p, returns the first digit */
char *func_8C056F72(char *p, Uint32 n)
{
    Sint32 d;

    do {
        d = (Sint32)(n & 0xF);
        *--p = (char)((d < 10 ? '0' : 'A' - 10) + d);
        n >>= 4;
    } while (n != 0);
    return p;
}

#ifdef NON_MATCHING
/*
 * vsprintf: returns the length written (without the terminator).
 *
 * Status: complete and faithful; differs from the original in two places.
 *  1. Case 's': the two va_arg address temporaries get R4/R5 swapped
 *     (original: ap+4 in R5, ap-4 in R4). Everything else, including the
 *     other five va_arg expansions, is identical.
 *  2. The 'd' case "if (plus)" test is BT in the original but BF+BRA here.
 *     This depends only on where SHC dumps literal pools, i.e. on the
 *     functions before 8C056EC4 in the original translation unit (their
 *     pool is dumped inside 8C056F08 there). Compiled after a dummy function
 *     that recreates that pool state, the only remaining difference is 1.
 *     Difference 1 is probably the same effect: the original has an (empty)
 *     pool dump point right before case 's' (8C05704E), and a dump point is
 *     known to change SHC's register choices next to it (see 8C057272).
 * Source idioms the original needs: the flag loop is written with goto (SHC
 * then stops rotating the while loops of the whole function, giving the
 * original's "BRA test; body; test: Bcc body" loops); pad is a variable
 * separate from width; the zero-fill bound is written inline as
 * end + plus - width (SHC hoists it above the switch, except in 'd' where
 * plus changes).
 * As in the original, a width above 63 with the 0 flag writes below tmp[]
 * (no bounds check).
 */
Sint32 func_8C056F94(char *buf, const char *fmt, va_list ap)
{
    char *out;
    char *p;
    Sint32 c;
    Sint32 minus;
    Sint32 plus;
    Sint32 zero;
    Sint32 width;
    Sint32 len;
    Sint32 pad;
    Sint32 n;
    char *end;
    char tmp[64];

    out = buf;
    end = &tmp[63];
    *end = '\0';
    while ((c = *fmt++) != '\0') {
        if (c == '%') {
            width = 0;
            minus = 0;
            plus = 0;
            zero = 0;
        flags:
            c = *fmt++;
            if (c == '-') {
                minus = 1;
                goto flags;
            }
            if (c == '+') {
                plus = 1;
                goto flags;
            }
            if (c == '0') {
                zero = 1;
                goto flags;
            }
            while (c >= '0' && c <= '9') {
                width = width * 10 + c - '0';
                c = *fmt++;
            }
            switch (c) {
            case 's':
                p = va_arg(ap, char *);
                break;
            case 'c':
                p = end;
                *--p = (char)va_arg(ap, Sint32);
                break;
            case 'u':
                p = func_8C056EE4(end, va_arg(ap, Uint32));
                if (zero) {
                    while (p > end + plus - width) {
                        *--p = '0';
                    }
                }
                if (plus) {
                    *--p = '+';
                }
                break;
            case 'd':
                n = va_arg(ap, Sint32);
                if (n >= 0) {
                    p = func_8C056EE4(end, (Uint32)n);
                } else {
                    plus = 1;
                    p = func_8C056EE4(end, (Uint32)-n);
                }
                if (zero) {
                    while (p > end + plus - width) {
                        *--p = '0';
                    }
                }
                if (plus) {
                    *--p = (n < 0) ? '-' : '+';
                }
                break;
            case 'x':
                p = func_8C056F08(end, va_arg(ap, Uint32));
                if (zero) {
                    while (p > end + plus - width) {
                        *--p = '0';
                    }
                }
                break;
            case 'X':
                p = func_8C056F72(end, va_arg(ap, Uint32));
                if (zero) {
                    while (p > end + plus - width) {
                        *--p = '0';
                    }
                }
                break;
            default:
                p = end;
                (void)va_arg(ap, Sint32);
                break;
            }
            len = (Sint32)strlen(p);
            pad = width - len;
            if (minus) {
                func_8C10D3DC(out, p);
                out += len;
                while (pad > 0) {
                    *out++ = ' ';
                    pad--;
                }
            } else {
                while (pad > 0) {
                    *out++ = ' ';
                    pad--;
                }
                func_8C10D3DC(out, p);
                out += len;
            }
        } else {
            *out++ = (char)c;
        }
    }
    *out = '\0';
    return (Sint32)(out - buf);
}
#else
ASM_STUB(Sint32 func_8C056F94(char *buf, const char *fmt, va_list ap))
#endif

#ifdef NON_MATCHING
/*
 * sprintf. Matches (shcdiff MATCH with -define=NON_MATCHING) when it is
 * compiled after the C body of func_8C056F94: the original dumps
 * func_8C056F94's literal pool (strlen, strcpy) at this function's BRA, and
 * SHC only picks the original registers for the va_start temporaries with
 * that pool split in place. After the ASM_STUB of func_8C056F94 there is no
 * pool there and SHC adds a register copy, so it stays NON_MATCHING until
 * func_8C056F94 matches.
 */
Sint32 func_8C057272(char *buf, const char *fmt, ...)
{
    va_list ap;
    Sint32 ret;

    va_start(ap, fmt);
    ret = func_8C056F94(buf, fmt, ap);
    va_end(ap);
    return ret;
}
#else
ASM_STUB(Sint32 func_8C057272(char *buf, const char *fmt, ...))
#endif
