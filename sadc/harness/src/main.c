/*
 * main.c - sa1h: SH-4 interpreter harness command line.
 *
 *   sa1h selftest                       instruction-level interpreter tests (no ROM)
 *   sa1h rtlib  [iters]                  SHC runtime helpers (__divls ...) vs C semantics
 *   sa1h leafsmoke [count] [seed]        original leaf functions on random inputs
 *   sa1h call <func|addr> [args...]      call a guest function (args: 12, 0x10, 1.5f)
 *   sa1h diff <func|all> [iters] [seed]  differential test: original vs native decompiled C
 *   sa1h list                            native functions and thunks linked in
 *   sa1h bench                           interpreter speed
 *   sa1h nativetest                      native <-> guest glue checks
 *   sa1h boot [frames]                   headless boot experiment (task system)
 *   sa1h trace [options]                 reference simulator: stage + player, per-step trace
 *   sa1h menu [options]                  the ADVERTISE menu overlay (character select), logged
 *
 * Options (before the command): --root DIR, --overlay NAME (load baserom/overlays/NAME.BIN at its
 * load address, repeatable), --trace (every instruction), --calls (guest call/return trace),
 * --io-verbose, --no-passthrough
 */
#include "harness.h"
#include "sh4_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

char g_root[1024];
int g_rom_loaded;
static const char *overlay_names[16];
static int n_overlays;
static uint32_t rng_state = 0x9E3779B9u;

uint32_t harness_rand(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

void harness_srand(uint32_t seed)
{
    rng_state = seed ? seed : 0x9E3779B9u;
}

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

static void find_root(void)
{
    char buf[1024], probe[1100];
    const char *env = getenv("SADC_ROOT");
    int i;
    if (env && *env) {
        snprintf(g_root, sizeof g_root, "%s", env);
        return;
    }
#ifdef _WIN32
    {
        DWORD n = GetModuleFileNameA(NULL, buf, sizeof buf);
        if (n > 0 && n < sizeof buf) {
            /* exe lives in harness/build/: walk up looking for baserom/ */
            for (i = 0; i < 4; i++) {
                char *s = strrchr(buf, '\\');
                if (!s)
                    break;
                *s = 0;
                snprintf(probe, sizeof probe, "%s\\baserom", buf);
                snprintf(probe, sizeof probe, "%s\\configure.py", buf);
                if (file_exists(probe)) {
                    snprintf(g_root, sizeof g_root, "%s", buf);
                    return;
                }
            }
        }
    }
#endif
    (void)buf; (void)probe; (void)i;
    snprintf(g_root, sizeof g_root, ".");
}

int harness_load_rom(void)
{
    char path[1200];
    long n;
    if (g_rom_loaded)
        return 0;
    snprintf(path, sizeof path, "%s/baserom/1ST_READ.BIN", g_root);
    n = sh4_load_file(path, SH4_IMAGE_BASE);
    if (n < 0) {
        fprintf(stderr, "cannot load %s (set --root or SADC_ROOT)\n", path);
        return -1;
    }
    if (n != 6735488)
        fprintf(stderr, "warning: %s is %ld bytes, expected 6735488 (US 1.005)\n", path, n);
    snprintf(path, sizeof path, "%s/build/1ST_READ.symbols.txt", g_root);
    if (sh4_symbols_load(path) < 0)
        fprintf(stderr, "warning: no symbol file %s (names unavailable)\n", path);
    g_rom_loaded = 1;
    {
        int i;
        for (i = 0; i < n_overlays; i++)
            if (harness_load_overlay(overlay_names[i]))
                return -1;
    }
    return 0;
}

int harness_load_overlay(const char *name)
{
    char path[1200];
    int i;
    for (i = 0; i < sh4_overlay_count; i++) {
        if (!strcmp(sh4_overlays[i].name, name)) {
            snprintf(path, sizeof path, "%s/%s", g_root, sh4_overlays[i].bin);
            if (sh4_load_file(path, sh4_overlays[i].load) < 0) {
                fprintf(stderr, "cannot load overlay %s\n", path);
                return -1;
            }
            return 0;
        }
    }
    fprintf(stderr, "unknown overlay %s\n", name);
    return -1;
}

/* restore main RAM from a snapshot, copying only the 4 KB blocks that differ */
size_t harness_ram_restore(const uint8_t *snap)
{
    uint8_t *ram = sh4_ram();
    size_t off, n = 0;
    for (off = 0; off < SH4_RAM_SIZE; off += 4096) {
        if (memcmp(ram + off, snap + off, 4096) != 0) {
            memcpy(ram + off, snap + off, 4096);
            n += 4096;
        }
    }
    return n;
}

/* restore only the blocks the interpreter wrote since the last call (valid
 * when no native code wrote to guest RAM) */
size_t harness_ram_restore_dirty(const uint8_t *snap)
{
    uint8_t *ram = sh4_ram();
    size_t i, n = 0;
    for (i = 0; i < sizeof sh4_ram_dirty; i++) {
        if (sh4_ram_dirty[i]) {
            memcpy(ram + (i << 12), snap + (i << 12), 4096);
            sh4_ram_dirty[i] = 0;
            n += 4096;
        }
    }
    return n;
}

uint32_t harness_parse_addr(const char *s)
{
    uint32_t a = sh4_sym_addr(s);
    char *end;
    if (a)
        return a;
    a = (uint32_t)strtoul(s, &end, 16);
    if (*end) {
        fprintf(stderr, "unknown symbol or address: %s\n", s);
        return 0;
    }
    return a;
}

void harness_print_fault(const char *what)
{
    printf("%s: %s at pc=%08X op=%04X addr=%08X: %s\n", what, sh4_fault_name(SH4.fault.kind), SH4.fault.pc,
           SH4.fault.op, SH4.fault.addr, SH4.fault.msg);
}

static int cmd_help(void)
{
    puts("usage: sa1h [--root DIR] [--overlay NAME]... [--trace] [--calls] [--io-verbose] [--no-passthrough]\n"
         "            <command> [args]\n"
         "  selftest                       instruction-level interpreter tests\n"
         "  rtlib [iters]                  SHC runtime helpers vs C semantics\n"
         "  leafsmoke [count] [seed]       run original leaf functions on random inputs\n"
         "  call <func|addr> [args...]     call a guest function (12, 0x10, 1.5f)\n"
         "  diff <func|all> [iters] [seed] original vs native decompiled C\n"
         "  list                           native functions / thunks in this build\n"
         "  bench                          interpreter speed\n"
         "  nativetest                     native <-> guest glue (thunks, trampolines, routing)\n"
         "  boot [frames] [chr] [level]    headless boot experiment (see t_boot.c)\n"
         "  trace [--level N --act N --input SCRIPT --out PREFIX ...]\n"
         "                                 reference simulator: real stage + player under scripted input,\n"
         "                                 per-step trace (t_sim.c, docs/harness_simulator.md)\n"
         "  menu [--frames N --input SCRIPT --log FILE --log-2d FILE ...]\n"
         "                                 the ADVERTISE menu overlay (character select) under scripted input:\n"
         "                                 state / sound log and the 2D draws (t_sim.c cmd_menu)");
    return 2;
}

int main(int argc, char **argv)
{
    int i = 1, r;
    setvbuf(stdout, NULL, _IONBF, 0);
    find_root();
    for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++) {
        if (!strcmp(argv[i], "--root") && i + 1 < argc)
            snprintf(g_root, sizeof g_root, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--trace"))
            SH4.trace = 1;
        else if (!strcmp(argv[i], "--overlay") && i + 1 < argc && n_overlays < 16)
            overlay_names[n_overlays++] = argv[++i];
        else if (!strcmp(argv[i], "--calls"))
            sh4_cstack_on = 2;
        else if (!strcmp(argv[i], "--io-verbose"))
            sh4_io_verbose = 1;
        else if (!strcmp(argv[i], "--no-passthrough"))
            sh4_host_passthrough = 0;
        else
            return cmd_help();
    }
    if (i >= argc)
        return cmd_help();
    r = sh4_mem_init();
    if (r < 0) {
        fprintf(stderr, "memory init failed\n");
        return 1;
    }
    if (r == 0 && sh4_native_fn_count > 0)
        fprintf(stderr, "warning: guest RAM is not at its DC addresses; native code cannot run\n");
    {
        int trace = SH4.trace;
        sh4_reset(&SH4);
        SH4.trace = trace;
    }
    sh4_host_fp_game_mode();
    sh4_native_init();
    if (!strcmp(argv[i], "selftest")) return cmd_selftest(argc - i - 1, argv + i + 1);
    if (!strcmp(argv[i], "rtlib")) return cmd_rtlib(argc - i - 1, argv + i + 1);
    if (!strcmp(argv[i], "leafsmoke")) return cmd_leafsmoke(argc - i - 1, argv + i + 1);
    if (!strcmp(argv[i], "call")) return cmd_call(argc - i - 1, argv + i + 1);
    if (!strcmp(argv[i], "diff")) return cmd_diff(argc - i - 1, argv + i + 1);
    if (!strcmp(argv[i], "list")) return cmd_list(argc - i - 1, argv + i + 1);
    if (!strcmp(argv[i], "bench")) return cmd_bench(argc - i - 1, argv + i + 1);
    if (!strcmp(argv[i], "boot")) return cmd_boot(argc - i - 1, argv + i + 1);
    if (!strcmp(argv[i], "nativetest")) return cmd_nativetest(argc - i - 1, argv + i + 1);
    if (!strcmp(argv[i], "trace")) return cmd_trace(argc - i - 1, argv + i + 1);
    if (!strcmp(argv[i], "menu")) return cmd_menu(argc - i - 1, argv + i + 1);
    return cmd_help();
}