/*
 * sh4_fsca_ref.c - the FSCA reference table: fsca_build() (moved verbatim
 * from sh4_cpu.c, where it used to run at start-up) and the check that the
 * table the interpreter now loads (sh4_fsca_q[], libsa1/src/rt/rt_fsca_q.c,
 * compiled into the harness) is exactly what fsca_build() computes.
 *
 * The same file is also compiled stand-alone by harness/tools/dump_fsca.py
 * (-DSH4_FSCA_REF_MAIN, same target and flags as the harness) to generate
 * rt_fsca_q.c: fsca_build() depends on the host C library's sin(), which is
 * why libsa1 uses the dumped table and never an independent derivation
 * (docs/libsa1_integration.md 8.2).
 */
#include "pc/sh4_fpmodel.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#define HAVE_MXCSR 1
#endif

#define MXCSR_MASKALL 0x1F80u

#ifdef SH4_FSCA_REF_MAIN
static uint32_t sh4_host_mxcsr_get(void) { return _mm_getcsr(); }
static void sh4_host_mxcsr_set(uint32_t v) { _mm_setcsr(v); }
#else
#include "sh4.h"
#endif

/* FSCA: sin/cos of (angle & 0xFFFF) * 2pi/65536.  The hardware uses an
 * internal table (documented error bound ~2^-21); we use the correctly
 * rounded (to nearest) value of the true function, with exact 0/+-1 at the
 * quadrant points and exact quadrant symmetry.  APPROXIMATE.
 * fsca_tab[a] is the sine of angle a; the cosine is fsca_tab[(a + 0x4000) & 0xFFFF]. */
static void fsca_build(float *fsca_tab)
{
    uint32_t save = sh4_host_mxcsr_get();
    int i;
#ifdef HAVE_MXCSR
    sh4_host_mxcsr_set(MXCSR_MASKALL);      /* round to nearest while building */
#endif
    for (i = 0; i <= 0x4000; i++) {
        double ang = (double)i * (6.283185307179586476925286766559 / 65536.0);
        float s = (float)sin(ang);
        if (i == 0)
            s = 0.0f;
        if (i == 0x4000)
            s = 1.0f;
        fsca_tab[i] = s;
        fsca_tab[0x8000 - i] = s;
        fsca_tab[(0x8000 + i) & 0xFFFF] = -s;
        fsca_tab[(0x10000 - i) & 0xFFFF] = -s;
    }
    fsca_tab[0] = 0.0f;
    fsca_tab[0x8000] = 0.0f;
    sh4_host_mxcsr_set(save);
}

#ifdef SH4_FSCA_REF_MAIN

/* the generator never links the table it generates */
const uint32_t sh4_fsca_q[0x4001];

/* dump_fsca.exe: the 16,385 quarter-wave entries, one hex word per line */
int main(void)
{
    static float tab[0x10000];
    int i;
    fsca_build(tab);
    for (i = 0; i <= 0x4000; i++)
        printf("%08X\n", (unsigned)sh4_f2u(tab[i]));
    return 0;
}

#else

/* Compares the loaded table with fsca_build(): the raw quarter-wave entries
 * and sh4_fsca() (the interpreter's FSCA) for all 65,536 angles, sine and
 * cosine, plus angles with garbage above bit 15.  Returns the number of
 * mismatches; *first_bad = the first mismatching angle (or table index). */
int sh4_fsca_selfcheck(uint32_t *first_bad)
{
    static float ref[0x10000];
    int bad = 0;
    uint32_t a;
    fsca_build(ref);
    for (a = 0; a <= 0x4000u; a++) {
        if (sh4_fsca_q[a] != sh4_f2u(ref[a])) {
            if (!bad++)
                *first_bad = a;
        }
    }
    for (a = 0; a < 0x30000u; a++) {
        float s, c;
        uint32_t ang = a < 0x10000u ? a : (a * 0x9E3779B9u);
        sh4_fsca(ang, &s, &c);
        if (sh4_f2u(s) != sh4_f2u(ref[ang & 0xFFFFu]) ||
            sh4_f2u(c) != sh4_f2u(ref[(ang + 0x4000u) & 0xFFFFu])) {
            if (!bad++)
                *first_bad = ang;
        }
    }
    return bad;
}

#endif
