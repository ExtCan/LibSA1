/*
 * rt_misc.c - the pieces of the SHC C runtime that the game calls as functions
 * (docs/libsa1_integration.md 4.1, 4.2, 10.2): rand / srand on the arena state
 * D_8C5FBB08, memset, strlen, the strcpy 8C10D3DC and the byte copy 8C604500;
 * plus how the SHC-ABI helpers (__divls ..., __quick_mvn) are covered, and
 * (libsa1 profile only) the emulated TMU channel 1 of TARGET_PC game code.
 *
 * Clean-room: written from the observed behaviour of the original library
 * code (the exact access order included, which matters when buffers overlap),
 * verified bit-exact against it in the harness (README.md):
 *     sa1h_<tag>.exe diff func_8C10D334 2000 1 --warm --ulps 0   (and the others)
 *
 * Names.  The C library names rand / srand / memset / strlen / strcpy must
 * never bind to the host C library (design 2.3 rule R7):
 *   harness profile  every function carries the name of its DC address
 *                    (func_8C10D334 ...): gen_native registers it there (diff,
 *                    --route) and the harness exe defines no host CRT symbol.
 *                    The game's own calls of rand / memset / strlen keep going
 *                    through gen_native's rand__game / memset__game /
 *                    strlen__game thunks, which the routed hook at the address
 *                    then runs natively.
 *   libsa1 profile   the R7 names of SADC/include/sa1/guest.h (sa1g_rand,
 *                    sa1g_srand, sa1g_memset, sa1g_strlen, sa1g_strcpy), which
 *                    pclower gives the game's calls, with guest.h's prototypes;
 *                    the address names 8C10D39C (srand) and 8C10D3DC (strcpy),
 *                    which game code calls by address, exist in both profiles.
 *                    sa1g_memcpy (also in guest.h) has no SHC original and no
 *                    caller in the decompiled code: not provided.
 *
 * SHC-ABI helpers (no C entry point here, on purpose):
 *   __divls 8C10CA5C, __divlu 8C10CB10, __modls 8C10CBB8, __modlu 8C10CC80
 *       R0 = R1 / R0 (or %), signed / unsigned, R1 clobbered; SHC
 *       (-division=cpu) emits a call for every non-constant / and %.  The
 *       decompiled C writes the operator and, on PC, SA1_DIV / SA1_UDIV /
 *       SA1_MOD / SA1_UMOD (SADC/include/common.h; pclower rule R6 gives
 *       sa1g_div_s32 ... of guest.h, the same code), which give the helpers'
 *       results: divisor 0 -> 0, INT_MIN / -1 -> INT_MIN (wraps), x % -1 -> 0.
 *       Evidence: harness "rtlib" runs the original helpers on 200,000 operand
 *       pairs each (edge values included) against C semantics with exactly
 *       these rules (0 mismatches; INT_MIN / -1 -> 80000000, INT_MIN % -1 -> 0).
 *       Not reproduced: on a zero divisor the helpers also store the SHC errno
 *       word B_8C7AC108 = 0x44E; only C-library code (8C10B9A0-8C1109BC)
 *       touches that word, no game code.
 *   __quick_mvn 8C10CD38 / 8C10CD44, 8C10CDDC
 *       the block moves SHC emits for a struct assignment: R0 = byte count (a
 *       multiple of 4), R1 = destination, R2 = source.  8C10CD38 / 8C10CD44
 *       (up to 64 bytes) and 8C10CDDC up to 64 bytes copy longwords from the
 *       last one down to offset 0 through a jump table; 8C10CDDC above 64
 *       bytes copies forward, reading one longword past the end.  The
 *       decompiled C writes these as struct assignments (*a = *b,
 *       ring[i] = ring[i - 1]), which the PC compiler copies itself (pclower
 *       R2 keeps DC-layout structs a byte image).  The copy direction cannot
 *       show: C struct assignment between partially overlapping objects is
 *       undefined (SHC emits these helpers for nothing else), and an exact
 *       self-copy gives the same bytes in any order.  A C function named
 *       func_8C10CDDC must not exist: gen_native would register it with an
 *       R4-R6 C-ABI adapter and --route would then break every struct copy of
 *       the interpreted game.
 */
#include "rt.h"

extern Uint32 D_8C5FBB08;               /* rand state: the C library's seed word (per world in the arena) */

#if defined(TARGET_PC_HARNESS)
#define RT_RAND     func_8C10D334
#define RT_MEMSET   func_8C10D31C
#define RT_STRLEN   func_8C10D3F0
#define RT_SIZE_T   Uint32
#define RT_MEMSET_C Sint32
#else
#define RT_RAND     sa1g_rand
#define RT_MEMSET   sa1g_memset
#define RT_STRLEN   sa1g_strlen
#define RT_SIZE_T   size_t
#define RT_MEMSET_C int
#endif

Sint32 RT_RAND(void);
void func_8C10D39C(Uint32 seed);
void *RT_MEMSET(void *dst, RT_MEMSET_C c, RT_SIZE_T n);
RT_SIZE_T RT_STRLEN(const char *s);
char *func_8C10D3DC(char *dst, const char *src);

/* ------------------------------------------------------------------ */
/* rand / srand                                                        */

/* 8C10D334 rand: x = x * 0x41C64E6D + 0x3039 (mod 2^32), returns bits 16..30
 * of the new state (0..0x7FFF) */
Sint32 RT_RAND(void)
{
    Uint32 x = D_8C5FBB08 * 0x41C64E6Du + 0x3039u;

    D_8C5FBB08 = x;
    return (Sint32)((x >> 16) & 0x7FFFu);
}

/* 8C10D39C srand: the state is the seed itself */
void func_8C10D39C(Uint32 seed)
{
    D_8C5FBB08 = seed;
}

/* ------------------------------------------------------------------ */
/* memory and strings                                                  */

/* 8C10D31C memset: the low byte of c into n bytes, forward, one at a time
 * (n = 0 stores nothing); returns dst */
void *RT_MEMSET(void *dst, RT_MEMSET_C c, RT_SIZE_T n)
{
    Uint8 *d = (Uint8 *)dst;
    RT_SIZE_T i;

    for (i = 0; i < n; i++) {
        d[i] = (Uint8)c;
    }
    return dst;
}

/* 8C10D3F0 strlen: bytes before the first 0 (32-bit count) */
RT_SIZE_T RT_STRLEN(const char *s)
{
    Uint32 n = 0;

    while (*s++ != 0) {
        n++;
    }
    return n;
}

/* a longword holds a zero byte (CMP/STR against 0) */
#define RT_HAS_ZERO_BYTE(w)                                                                       \
    (((w) & 0x000000FFu) == 0 || ((w) & 0x0000FF00u) == 0 || ((w) & 0x00FF0000u) == 0 ||          \
     ((w) & 0xFF000000u) == 0)

/* 8C10D3DC strcpy (core 8C10DACC / 8C10EF20), returns dst.
 * Both pointers longword aligned: whole longwords are copied while the
 * longword holds no zero byte, the next one always read before the current
 * one is stored; the longword with the terminator is then re-read from memory
 * and copied byte by byte, at most 4 bytes, up to and including the first 0
 * (the 4th byte is copied unconditionally).  Otherwise one byte at a time,
 * each byte read just before it is stored.  The writes are the string and its
 * terminator either way; the order only shows when the buffers overlap. */
char *func_8C10D3DC(char *dst, const char *src)
{
    char *d = dst;
    const char *s = src;
    char c;

    if ((((SA1_UPTR)d | (SA1_UPTR)s) & 3u) == 0) {
        Uint32 w = *(const Uint32 *)s;

        s += 4;
        while (!RT_HAS_ZERO_BYTE(w)) {
            Uint32 next = *(const Uint32 *)s;

            s += 4;
            *(Uint32 *)d = w;
            d += 4;
            w = next;
        }
        s -= 4;
        c = s[0];
        d[0] = c;
        if (c != 0) {
            c = s[1];
            d[1] = c;
            if (c != 0) {
                c = s[2];
                d[2] = c;
                if (c != 0) {
                    d[3] = s[3];
                }
            }
        }
        return dst;
    }
    do {
        c = *s++;
        *d++ = c;
    } while (c != 0);
    return dst;
}

#if !defined(TARGET_PC_HARNESS)
/* libsa1 entry points under the R7 names of SADC/include/sa1/guest.h */
void sa1g_srand(unsigned int seed)
{
    func_8C10D39C(seed);
}

char *sa1g_strcpy(char *dst, const char *src)
{
    return func_8C10D3DC(dst, src);
}
#endif

/* ------------------------------------------------------------------ */
/* block copy                                                          */

/* 8C604500: copies n bytes forward, one at a time (read, then store), as a
 * do/while on DT: n = 0 copies 2^32 bytes, exactly like the original (a fault
 * in any world; the callers pass sizes such as CCL_Init's nbInfo * 0x30).
 * Forward overlap smears exactly like the original. */
void func_8C604500(void *dst, const void *src, Uint32 n)
{
    Uint8 *d = (Uint8 *)dst;
    const Uint8 *s = (const Uint8 *)src;

    do {
        Uint8 c = *s++;

        *d++ = c;
    } while (--n != 0);
}

/* ------------------------------------------------------------------ */
/* emulated TMU channel 1 (libsa1 profile)                             */

#if !defined(TARGET_PC_HARNESS)
/* TARGET_PC game code (code_8C06C0C0.c, code_8C06C1D8.c) programs and reads
 * TMU channel 1 through these instead of the SH-4 registers at 0xFFD8xxxx.
 * The layout is the PC-only struct of code_8C06C0C0.c (no DC address: pclower
 * places this definition in the world's pcdata, design 2.3 R4).  The counter
 * never moves: its readers, the frame-time check 8C06C1B2 and the debug timer
 * 8C06C182, are outside libsa1's tick (design 6.4), and the vblank handler
 * 8C06C1D8 is never registered (rt_draw.c, 8C64133E).  The harness has its
 * own copy (SADC/harness/src/pc_runtime.c). */
typedef struct {
    Uint8 tstr;     /* TSTR: bit 1 runs channel 1 */
    Uint16 tcr1;    /* TCR1 */
    Uint32 tcor1;   /* TCOR1: reload value */
    Uint32 tcnt1;   /* TCNT1 */
} SA1_TMU;

SA1_TMU sa1_tmu;

Uint32 sa1_tmu_tcnt1(void)
{
    return sa1_tmu.tcnt1;
}
#endif
