/*
 * t_rtlib.c - the SHC runtime helpers in 1ST_READ.BIN are original code:
 * run them in the interpreter and compare with C semantics.
 *
 *   __divls 8C10CA5C  R0 = (int)R1 / (int)R0
 *   __divlu 8C10CB10  R0 = R1 / R0 (unsigned)
 *   __modls 8C10CBB8  R0 = (int)R1 % (int)R0
 *   __modlu 8C10CC80  R0 = R1 % R0 (unsigned)
 *   (divisor 0: R0 = 0 and the error word B_8C7AC108 = 0x44E)
 *   __quick_mvn 8C10CD38 / func_8C10CDDC: copy R0 bytes from @R2 to @R1
 */
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERRWORD 0x8C7AC108u

typedef struct {
    const char *name;
    uint32_t addr;
    int kind;   /* 0 divls 1 divlu 2 modls 3 modlu */
} helper;

static uint32_t interesting(void)
{
    static const uint32_t edge[] = { 0, 1, 2, 3, 7, 10, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFEu,
                                     0x80000001u, 0x0000FFFFu, 0x00010000u, 0xFFFF0000u, 100, 0xFFFFFF9Cu };
    uint32_t r = harness_rand();
    switch (r & 7) {
    case 0: case 1: return edge[(r >> 3) % (sizeof edge / sizeof edge[0])];
    case 2: return (r >> 3) & 0xFF;
    case 3: return (uint32_t)-(int32_t)((r >> 3) & 0xFFFF);
    default: return harness_rand();
    }
}

static int expect(int kind, uint32_t a, uint32_t b, uint32_t *out)
{
    if (b == 0) {
        *out = 0;
        return 1;
    }
    switch (kind) {
    case 0:
        if (a == 0x80000000u && b == 0xFFFFFFFFu) return 0;     /* overflow: C undefined */
        *out = (uint32_t)((int32_t)a / (int32_t)b);
        return 1;
    case 1: *out = a / b; return 1;
    case 2:
        if (a == 0x80000000u && b == 0xFFFFFFFFu) return 0;
        *out = (uint32_t)((int32_t)a % (int32_t)b);
        return 1;
    default: *out = a % b; return 1;
    }
}

int cmd_rtlib(int argc, char **argv)
{
    static const helper hs[] = {
        { "__divls", 0x8C10CA5Cu, 0 }, { "__divlu", 0x8C10CB10u, 1 },
        { "__modls", 0x8C10CBB8u, 2 }, { "__modlu", 0x8C10CC80u, 3 },
    };
    int iters = argc > 0 ? atoi(argv[0]) : 200000;
    int h, i, r, total_fail = 0;
    if (harness_load_rom())
        return 1;
    harness_srand(12345);
    for (h = 0; h < 4; h++) {
        int fails = 0, faults = 0, ovf = 0;
        uint32_t clobber = 0, ovf_val = 0;
        for (i = 0; i < iters; i++) {
            uint32_t a = interesting(), b = interesting(), want, before[16];
            int k, have;
            if (i == 0) { a = 0x80000000u; b = 0xFFFFFFFFu; }
            if (i == 1) { a = 12345; b = 0; }
            for (r = 2; r < 15; r++)
                SH4.r[r] = 0xA5A50000u + (uint32_t)r;
            SH4.r[1] = a;
            SH4.r[0] = b;
            sh4_write32(ERRWORD, 0);
            memcpy(before, SH4.r, sizeof before);
            k = sh4_call_regs(hs[h].addr);
            if (k) {
                faults++;
                continue;
            }
            for (r = 1; r < 15; r++)
                if (SH4.r[r] != before[r])
                    clobber |= 1u << r;
            have = expect(hs[h].kind, a, b, &want);
            if (!have) {
                ovf++;
                ovf_val = SH4.r[0];
                continue;
            }
            if (SH4.r[0] != want || (b == 0 && sh4_read32(ERRWORD) != 0x44Eu)) {
                if (fails < 5)
                    printf("  %s(%08X, %08X) = %08X, C gives %08X\n", hs[h].name, a, b, SH4.r[0], want);
                fails++;
            }
        }
        printf("%-8s %d cases: %d mismatches, %d faults; clobbers:", hs[h].name, iters, fails, faults);
        for (r = 1; r < 15; r++)
            if (clobber & (1u << r))
                printf(" R%d", r);
        if (ovf)
            printf("; INT_MIN/-1 -> %08X", ovf_val);
        printf("\n");
        total_fail += fails + faults;
    }
    /* block moves */
    {
        /* __quick_mvn: even word counts (last word preloaded into R0),
         * func_8C10CD44: odd word counts (into R3), func_8C10CDDC: any */
        static const struct { const char *name; uint32_t addr; int first, max, step; } mv[] = {
            { "__quick_mvn", 0x8C10CD38u, 8, 64, 8 }, { "func_8C10CD44", 0x8C10CD44u, 4, 60, 8 },
            { "func_8C10CDDC", 0x8C10CDDCu, 4, 512, 4 },
        };
        const uint32_t src = 0x8C7F1000u, dst = 0x8C7F2000u;
        int m;
        for (m = 0; m < 3; m++) {
            int fails = 0, n = 0, size;
            for (size = mv[m].first; size <= mv[m].max; size += mv[m].step) {
                uint32_t j;
                int k;
                for (j = 0; j < 0x400; j += 4) {
                    sh4_write32(src + j, harness_rand());
                    sh4_write32(dst + j, 0xDDDDDDDDu);
                }
                SH4.r[0] = (uint32_t)size;
                SH4.r[1] = dst;
                SH4.r[2] = src;
                SH4.r[3] = 0x33333333u;
                k = sh4_call_regs(mv[m].addr);
                n++;
                if (k || memcmp(sh4_host_ptr(dst), sh4_host_ptr(src), (size_t)size) != 0 ||
                    sh4_read32(dst + (uint32_t)size) != 0xDDDDDDDDu || SH4.r[3] != 0x33333333u) {
                    if (fails < 3)
                        printf("  %s size %d failed (fault %d)\n", mv[m].name, size, k);
                    fails++;
                }
            }
            printf("%-14s %d sizes: %d failures\n", mv[m].name, n, fails);
            total_fail += fails;
        }
    }
    printf("rtlib: %s\n", total_fail ? "FAILURES" : "all helpers match C semantics");
    return total_fail ? 1 : 0;
}