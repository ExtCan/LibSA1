/* t_tools.c - call / list / bench commands */
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double now_s(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
#else
static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#endif

int cmd_call(int argc, char **argv)
{
    sh4_arg args[24];
    int n = 0, i, k, route = 0;
    uint32_t addr, r0;
    float f0;
    uint64_t ic;
    double t;
    if (harness_load_rom())
        return 1;
    for (i = 0; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++)
        if (!strcmp(argv[i], "--route"))
            route = 1;
    if (i >= argc) {
        fprintf(stderr, "usage: call [--route] <func|addr> [args]\n");
        return 2;
    }
    addr = harness_parse_addr(argv[i++]);
    if (!addr)
        return 2;
    for (; i < argc && n < 24; i++) {
        const char *s = argv[i];
        size_t len = strlen(s);
        if (len && (s[len - 1] == 'f' || strchr(s, '.')) && !(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')))
            args[n++] = sh4_af((float)atof(s));
        else
            args[n++] = sh4_ai((uint32_t)strtoul(s, NULL, 0));
    }
    hle_install_defaults(0);
    sh4_route_native(route);
    ic = SH4.icount;
    t = now_s();
    k = sh4_invoke(addr, args, n, &r0, &f0);
    t = now_s() - t;
    if (k) {
        harness_print_fault("call");
        return 1;
    }
    printf("%08X returned R0=%08X (%d) FR0=%.9g (%08X)  [%llu instructions, %.3f ms]\n", addr, r0, (int32_t)r0,
           (double)f0, SH4.fr.u[0], (unsigned long long)(SH4.icount - ic), t * 1000.0);
    return 0;
}

int cmd_list(int argc, char **argv)
{
    int i;
    (void)argc; (void)argv;
    printf("%d native functions:\n", sh4_native_fn_count);
    for (i = 0; i < sh4_native_fn_count; i++)
        printf("  %08X %-28s %-24s %s\n", sh4_native_fns[i].guest, sh4_native_fns[i].name, sh4_native_fns[i].sig,
               sh4_native_fns[i].adapter ? "" : "(no adapter)");
    printf("%d thunks (native -> original):\n", sh4_thunk_count);
    for (i = 0; i < sh4_thunk_count; i++)
        printf("  %08X %-28s %s\n", sh4_thunks[i].guest, sh4_thunks[i].name, sh4_thunks[i].sig);
    return 0;
}

#define BENCH_CODE 0x8C7F0000u

int cmd_bench(int argc, char **argv)
{
    /* 1: register-only loop: 8 instructions per iteration */
    static const uint16_t loop[] = {
        0x300C | (1 << 8) | (2 << 4),   /* ADD R2,R1   */
        0x200A | (3 << 8) | (1 << 4),   /* XOR R1,R3   */
        0x4000 | (3 << 8),              /* SHLL R3     */
        0x7001 | (2 << 8),              /* ADD #1,R2   */
        0x2009 | (5 << 8) | (3 << 4),   /* AND R3,R5   */
        0x4010 | (4 << 8),              /* DT R4       */
        0x8FF8,                         /* BF/S loop (-8 -> index 0) */
        0x6013 | (6 << 8) | (1 << 4),   /* MOV R1,R6 (slot) */
        0x000B, 0x0009,                 /* RTS; NOP */
    };
    /* 2: memory + FPU loop */
    static const uint16_t floop[] = {
        0xF008 | (1 << 8) | (7 << 4),   /* FMOV.S @R7,FR1 */
        0xF00E | (2 << 8) | (1 << 4),   /* FMAC FR0,FR1,FR2 */
        0xF002 | (3 << 8) | (1 << 4),   /* FMUL FR1,FR3 */
        0xF00A | (7 << 8) | (2 << 4),   /* FMOV.S FR2,@R7 */
        0x6002 | (1 << 8) | (7 << 4),   /* MOV.L @R7,R1 */
        0x4010 | (4 << 8),              /* DT R4 */
        0x8FF8,                         /* BF/S loop */
        0x0009,                         /* NOP */
        0x000B, 0x0009,
    };
    uint32_t i;
    uint64_t ic;
    double t;
    int iters = argc > 0 ? atoi(argv[0]) : 20000000;
    (void)argv;
    for (i = 0; i < sizeof loop / 2; i++)
        sh4_write16(BENCH_CODE + i * 2, loop[i]);
    SH4.r[4] = (uint32_t)iters;
    ic = SH4.icount;
    t = now_s();
    sh4_call_regs(BENCH_CODE);
    t = now_s() - t;
    printf("integer loop : %llu instructions in %.3f s = %.1f MIPS\n", (unsigned long long)(SH4.icount - ic), t,
           (double)(SH4.icount - ic) / t / 1e6);
    for (i = 0; i < sizeof floop / 2; i++)
        sh4_write16(BENCH_CODE + 0x100 + i * 2, floop[i]);
    sh4_writef(BENCH_CODE + 0x800, 1.0f);
    SH4.r[4] = (uint32_t)iters / 2;
    SH4.r[7] = BENCH_CODE + 0x800;
    SH4.fr.f[0] = 0.5f;
    ic = SH4.icount;
    t = now_s();
    sh4_call_regs(BENCH_CODE + 0x100);
    t = now_s() - t;
    printf("mem/FPU loop : %llu instructions in %.3f s = %.1f MIPS\n", (unsigned long long)(SH4.icount - ic), t,
           (double)(SH4.icount - ic) / t / 1e6);
    if (harness_load_rom() == 0) {
        uint32_t divls = sh4_sym_addr("_divls");
        int n = 200000;
        ic = SH4.icount;
        t = now_s();
        for (i = 0; i < (uint32_t)n; i++) {
            SH4.r[1] = 0x12345678u + i;
            SH4.r[0] = 1000u + i;
            sh4_call_regs(divls);
        }
        t = now_s() - t;
        printf("__divls      : %d calls in %.3f s = %.2f us/call, %.1f MIPS (incl. call overhead)\n", n, t,
               t / n * 1e6, (double)(SH4.icount - ic) / t / 1e6);
        {
            uint32_t njsin = 0x8C63A970u;
            uint32_t a[4] = { 0, 0, 0, 0 };
            ic = SH4.icount;
            t = now_s();
            for (i = 0; i < (uint32_t)n; i++) {
                a[0] = i * 37u;
                sh4_call(njsin, a, NULL);
            }
            t = now_s() - t;
            printf("func_8C63A970: %d calls in %.3f s = %.2f us/call (%llu instructions)\n", n, t, t / n * 1e6,
                   (unsigned long long)(SH4.icount - ic));
        }
    }
    return 0;
}
/* ------------------------------------------------------------------ */
/* nativetest: the native <-> guest glue                               */

#define NT_CODE 0x8C7E0000u

typedef int32_t (*fn_ii_i)(int32_t, int32_t);
typedef float (*fn_i_f)(int32_t);

int cmd_nativetest(int argc, char **argv)
{
    int fails = 0, i;
    (void)argc; (void)argv;
#if defined(_WIN32) && defined(__i386__)
    {
        /* guest: R0 = R4 + R5 (MOV R4,R0; RTS; ADD R5,R0) */
        static const uint16_t add[] = { 0x6043, 0x000B, 0x305C };
        fn_ii_i f;
        int32_t r;
        for (i = 0; i < 3; i++)
            sh4_write16(NT_CODE + 2u * (uint32_t)i, add[i]);
        f = (fn_ii_i)(uintptr_t)NT_CODE;    /* native call through a guest code pointer */
        r = f(40, 2);
        printf("native -> guest pointer call (DEP trampoline): 40+2 = %d %s\n", r, r == 42 ? "ok" : "FAIL");
        fails += r != 42;
    }
    if (harness_load_rom() == 0) {
        /* 8C63A970: FSCA, returns FR1 = cos (njCos; docs/research calls it njSin) */
        const sh4_thunk_info *ti = sh4_thunk_by_guest(0x8C63A970u);
        fn_i_f viaptr = (fn_i_f)(uintptr_t)0x8C63A970u;
        float a = viaptr(0x1000), want = sh4_fsca_cos(0x1000);
        printf("native -> guest pointer call, float return (signature from the thunk table): 8C63A970(0x1000) = %.9g %s\n",
               (double)a, a == want ? "ok" : "FAIL");
        fails += a != want;
        if (ti) {
            fn_i_f thunk = (fn_i_f)ti->host;
            double t;
            int n = 200000;
            float s = 0.0f;
            a = thunk(0x1000);
            printf("thunk %s(0x1000) = %.9g %s\n", ti->name, (double)a, a == want ? "ok" : "FAIL");
            fails += a != want;
            if (a != want) {
                harness_print_fault("  thunk");
                printf("  host=%p depth=%d guard=%p icount=%llu\n", ti->host, SH4.depth, (void *)sh4_guard_top_get(),
                       (unsigned long long)SH4.icount);
            }
            t = now_s();
            for (i = 0; i < n; i++)
                s += thunk(i);
            t = now_s() - t;
            printf("thunk call overhead: %d calls in %.3f s = %.2f us/call (guest body 5 instructions)\n", n, t,
                   t / n * 1e6);
            (void)s;
        }
        /* guest -> native: a natively compiled function reached by a guest JSR
         * (routing) and through a native function pointer stored in guest RAM */
        {
            const sh4_native_fn *nf = sh4_native_by_guest(0x8C052A1Eu);  /* pool alloc, p: */
            if (nf && nf->adapter) {
                /* guest caller: STS.L PR,@-R15; JSR @R4; NOP; LDS.L @R15+,PR; RTS; NOP */
                static const uint16_t caller[] = { 0x4F22, 0x440B, 0x0009, 0x4F26, 0x000B, 0x0009 };
                uint32_t args[4] = { 0, 0, 0, 0 };
                uint32_t r_orig, r_route, r_ptr;
                uint8_t *save = (uint8_t *)malloc(SH4_RAM_SIZE);
                sh4_event ev[64];
                size_t nev;
                for (i = 0; i < 6; i++)
                    sh4_write16(NT_CODE + 0x100 + 2u * (uint32_t)i, caller[i]);
                memcpy(save, sh4_ram(), SH4_RAM_SIZE);
                args[0] = nf->guest;
                r_orig = sh4_call(NT_CODE + 0x100, args, NULL);
                memcpy(sh4_ram(), save, SH4_RAM_SIZE);
                sh4_route_native(1);
                sh4_hook_log = 1;
                sh4_evlog_start(ev, 64);
                r_route = sh4_call(NT_CODE + 0x100, args, NULL);
                nev = sh4_evlog_stop();
                if (SH4.fault.kind)
                    harness_print_fault("  routing");
                sh4_hook_log = 0;
                printf("guest JSR -> native %s (routing): %08X, original %08X, native calls %u %s\n", nf->name,
                       r_route, r_orig, (unsigned)nev, (r_route == r_orig && nev == 1) ? "ok" : "FAIL");
                fails += !(r_route == r_orig && nev == 1);
                sh4_route_native(0);
                memcpy(sh4_ram(), save, SH4_RAM_SIZE);
                args[0] = (uint32_t)(uintptr_t)nf->host;   /* native pointer in a guest register */
                sh4_route_native(1);
                r_ptr = sh4_call(NT_CODE + 0x100, args, NULL);
                sh4_route_native(0);
                printf("guest JSR to a native function pointer: %08X %s\n", r_ptr, r_ptr == r_orig ? "ok" : "FAIL");
                fails += r_ptr != r_orig;
                memcpy(sh4_ram(), save, SH4_RAM_SIZE);
                free(save);
            } else {
                printf("(func_8C052A1E is not compiled natively in this build: routing test skipped)\n");
            }
        }
    }
#else
    printf("nativetest needs the 32-bit Windows build (guest RAM at its DC addresses)\n");
#endif
    printf("nativetest: %s\n", fails ? "FAILURES" : "all ok");
    return fails ? 1 : 0;
}