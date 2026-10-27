/*
 * t_boot.c - "headless boot" experiment: bring up the game's own heap and
 * task system, create a character task with the game's player loader and run
 * the task executor for some frames, with no hardware.
 *
 *   sa1h boot [frames] [character] [level] [--init] [--disp] [--tasks-only] [--quiet]
 *             [--stick X,Y] [--buttons MASK]      (pdGetPeripheral HLE for port 0)
 *
 *   per frame: the logic step 8C04E714 (input, task executor 8C0988A4,
 *   collision, ...) or with --tasks-only just the task executor; --disp also
 *   runs the display pass 8C0988C4; --init first runs the game's init 8C053AC0.
 *
 * Every step runs under an auto-stub loop: when the original code faults
 * (unmapped access, budget exhausted in a hardware wait loop, jump to a
 * missing overlay...), the innermost library (SDK) function on the shadow
 * call stack -- or else the innermost function -- gets an HLE "return 0"
 * hook, RAM is restored and the step is retried.  The list of stubs it
 * needed is the report: what a reference simulator has to provide.
 */
#include "harness.h"
#include "sh4_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEAP_INIT      0x8C098A10u
#define TASK_INIT      0x8C09844Au
#define TASK_EXEC_ALL  0x8C0988A4u
#define TASK_DISP_ALL  0x8C0988C4u
#define LOGIC_STEP     0x8C04E714u     /* one 60 Hz logic step: input, task exec, collision */
#define PLAYER_LOAD    0x8C04F3C0u
#define CUR_CHAR       0x8C7493C8u
#define CUR_LEVEL      0x8C7493BAu
#define CUR_ACT        0x8C7493BCu
#define PLAYER_TWP     0x8C78C608u
#define PLAYER_MWP     0x8C78C628u
#define LIB_START      0x8C5FBB60u
#define GAME_INIT      0x8C053AC0u     /* SDK + game init called by main 8C053940 */
#define VIEW_INIT      0x8C0544E2u     /* njInitMatrix(8C7EC8E0, 128, 0) + screen/view setup */

typedef struct { uint32_t addr; char why[200]; } stub_rec;
static stub_rec stubs[256];
static int nstubs;
static uint8_t *snap;
static int quiet;

/* io statistics */
#define IOSTAT_N 512
static struct { uint32_t addr; uint32_t reads, writes; } iostat[IOSTAT_N];
static int niostat;
static uint64_t sq_words;

static void io_count(uint32_t addr, int write)
{
    int i;
    if ((addr & 0xFC000000u) == 0x10000000u || (addr & 0xFC000000u) == 0x0C000000u)
        addr &= 0xFF000000u;            /* TA FIFO / SQ bursts: one bucket */
    for (i = 0; i < niostat; i++)
        if (iostat[i].addr == addr)
            break;
    if (i == niostat) {
        if (niostat == IOSTAT_N)
            return;
        iostat[niostat].addr = addr;
        niostat++;
    }
    if (write) iostat[i].writes++;
    else iostat[i].reads++;
}

static uint32_t boot_io_read(uint32_t addr, int size, void *user)
{
    io_count(addr, 0);
    return sh4_io_default_read(addr, size, user);
}

static void boot_io_write(uint32_t addr, uint32_t val, int size, void *user)
{
    if ((addr & 0xFC000000u) == 0x10000000u)
        sq_words++;
    io_count(addr, 1);
    sh4_io_default_write(addr, val, size, user);
}

static const sh4_funcinfo *func_of(uint32_t pc)
{
    int lo = 0, hi = sh4_function_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const sh4_funcinfo *f = &sh4_functions[mid];
        if (pc < f->addr)
            hi = mid - 1;
        else if (pc >= f->addr + (f->size ? f->size : 2))
            lo = mid + 1;
        else
            return f;
    }
    return NULL;
}

static const char *fname(uint32_t a)
{
    static char buf[4][64];
    static int k;
    const char *n = sh4_sym_name(a);
    k = (k + 1) & 3;
    if (n)
        snprintf(buf[k], sizeof buf[k], "%s", n);
    else
        snprintf(buf[k], sizeof buf[k], "%08X", a);
    return buf[k];
}

static int is_stubbed(uint32_t a)
{
    int i;
    for (i = 0; i < nstubs; i++)
        if (stubs[i].addr == a)
            return 1;
    return 0;
}

static uint32_t choose_stub(void)
{
    int d = sh4_cstack_depth < SH4_CSTACK_MAX ? sh4_cstack_depth : SH4_CSTACK_MAX;
    int i;
    /* innermost library function */
    for (i = d - 1; i >= 1; i--)
        if (sh4_cstack[i].target >= LIB_START && sh4_in_ram(sh4_cstack[i].target) && !is_stubbed(sh4_cstack[i].target))
            return sh4_cstack[i].target;
    /* else the innermost function that is not the step itself */
    for (i = d - 1; i >= 1; i--)
        if (sh4_in_ram(sh4_cstack[i].target) && !is_stubbed(sh4_cstack[i].target))
            return sh4_cstack[i].target;
    return 0;
}

static int run_step(const char *what, uint32_t addr, const sh4_arg *args, int nargs, uint32_t *r0, uint64_t budget)
{
    sh4_cpu save;
    int attempt;
    memcpy(snap, sh4_ram(), SH4_RAM_SIZE);
    memset(sh4_ram_dirty, 0, sizeof sh4_ram_dirty);
    save = SH4;
    for (attempt = 0; attempt < 64; attempt++) {
        uint32_t target, r = 0;
        float f;
        int k, i, d;
        sh4_cstack_depth = 0;
        SH4.ilimit = SH4.icount + budget;
        k = sh4_invoke(addr, args, nargs, &r, &f);
        SH4.ilimit = 0;
        if (k == SH4_OK) {
            if (r0)
                *r0 = r;
            return 0;
        }
        d = sh4_cstack_depth < SH4_CSTACK_MAX ? sh4_cstack_depth : SH4_CSTACK_MAX;
        target = choose_stub();
        if (!quiet) {
            const sh4_funcinfo *fi = func_of(SH4.fault.pc);
            printf("  [%s] fault: %s at %08X (in %s): %s\n", what, sh4_fault_name(k), SH4.fault.pc,
                   fi ? fi->name : "?", SH4.fault.msg);
            printf("        call stack:");
            for (i = 0; i < d; i++)
                printf(" %s", fname(sh4_cstack[i].target));
            printf("\n");
        }
        if (!target || nstubs >= 256) {
            printf("  [%s] giving up: nothing left to stub\n", what);
            return k;
        }
        stubs[nstubs].addr = target;
        snprintf(stubs[nstubs].why, sizeof stubs[nstubs].why, "%s: %s", sh4_fault_name(k), SH4.fault.msg);
        nstubs++;
        sh4_hook_add(target, sh4_hle_ret0, NULL, "autostub");
        if (!quiet)
            printf("        -> stub %s (returns 0)\n", fname(target));
        harness_ram_restore_dirty(snap);
        {
            uint64_t ic = SH4.icount;
            SH4 = save;
            SH4.icount = ic;
        }
    }
    return -1;
}

static void print_player(int frame)
{
    uint32_t twp = sh4_read32(PLAYER_TWP);
    if (!sh4_is_ram(twp)) {
        printf("  frame %3d: no player task work\n", frame);
        return;
    }
    printf("  frame %3d: mode %3d smode %3d flag %04X pos (%10.4f %10.4f %10.4f) ang (%04X %04X %04X)\n", frame,
           (int8_t)sh4_read8(twp), (int8_t)sh4_read8(twp + 1), sh4_read16(twp + 4), (double)sh4_readf(twp + 0x20),
           (double)sh4_readf(twp + 0x24), (double)sh4_readf(twp + 0x28), sh4_read32(twp + 0x14) & 0xFFFF,
           sh4_read32(twp + 0x18) & 0xFFFF, sh4_read32(twp + 0x1C) & 0xFFFF);
}

/* pdGetPeripheral(port) HLE: a standard controller on port 0 whose analog
 * stick and buttons come from the command line, nothing on the others */
#define PDS_PORT0 0x8C000400u
#define PDS_NONE  0x8C000440u
static int16_t pad_x, pad_y;
static uint32_t pad_buttons;

static void hle_get_peripheral(sh4_cpu *c, void *u)
{
    (void)u;
    if (c->r[4] == 0) {
        uint32_t old = sh4_read32(PDS_PORT0 + 8);
        sh4_write32(PDS_PORT0 + 0, 0x00000001u);           /* id: controller */
        sh4_write32(PDS_PORT0 + 4, 0x000F06FEu);           /* support: digital + analog */
        sh4_write32(PDS_PORT0 + 8, pad_buttons);           /* on */
        sh4_write32(PDS_PORT0 + 12, ~pad_buttons);         /* off */
        sh4_write32(PDS_PORT0 + 16, pad_buttons & ~old);   /* press */
        sh4_write32(PDS_PORT0 + 20, old & ~pad_buttons);   /* release */
        sh4_write16(PDS_PORT0 + 28, (uint16_t)pad_x);
        sh4_write16(PDS_PORT0 + 30, (uint16_t)pad_y);
        c->r[0] = PDS_PORT0;
    } else {
        c->r[0] = PDS_NONE;
    }
}

static void sleep_nop(sh4_cpu *c) { (void)c; }
static void trap_nop(sh4_cpu *c, uint32_t imm) { (void)c; printf("  TRAPA #%u at %08X\n", imm, c->cur_pc); }

/* Bring the game into a "warm" state for diff baselines: matrix/view init,
 * heap, task system, player loader (character 0, level 1) and a few logic
 * frames, with the same auto-stubbing.  Returns 0 on success. */
int boot_warm_state(int frames)
{
    sh4_arg none[1], a0 = sh4_ai(0);
    int f;
    quiet = 1;
    if (!snap)
        snap = (uint8_t *)malloc(SH4_RAM_SIZE);
    sh4_cstack_on = 1;
    sh4_sleep_handler = sleep_nop;
    sh4_trap_handler = trap_nop;
    hle_install_bios();
    sh4_hook_add(0x8C601F5Au, hle_get_peripheral, NULL, "pdGetPeripheral");
    if (run_step("view", VIEW_INIT, &a0, 1, NULL, 5000000) || run_step("heap", HEAP_INIT, none, 0, NULL, 50000000) ||
        run_step("tasks", TASK_INIT, none, 0, NULL, 50000000))
        return -1;
    sh4_write8(CUR_CHAR, 0);
    sh4_write16(CUR_LEVEL, 1);
    sh4_write16(CUR_ACT, 0);
    if (run_step("loader", PLAYER_LOAD, none, 0, NULL, 50000000))
        return -1;
    for (f = 0; f < frames; f++)
        if (run_step("frame", LOGIC_STEP, none, 0, NULL, 20000000))
            return -1;
    sh4_cstack_on = 0;
    sh4_cstack_depth = 0;
    return 0;
}

int boot_stub_count(void) { return nstubs; }

int cmd_boot(int argc, char **argv)
{
    int frames = 60, chr = 0, level = 1, disp = 0, init = 0, logic_only = 0, pad = 0, i, k, f, npos = 0;
    uint64_t ic0, icf;
    sh4_arg none[1];
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--disp")) disp = 1;
        else if (!strcmp(argv[i], "--init")) init = 1;
        else if (!strcmp(argv[i], "--tasks-only")) logic_only = 1;
        else if (!strcmp(argv[i], "--stick") && i + 1 < argc) {
            int x = 0, y = 0;
            sscanf(argv[++i], "%d,%d", &x, &y);
            pad_x = (int16_t)x;
            pad_y = (int16_t)y;
            pad = 1;
        } else if (!strcmp(argv[i], "--buttons") && i + 1 < argc) {
            pad_buttons = (uint32_t)strtoul(argv[++i], NULL, 0);
            pad = 1;
        }
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (npos == 0) { frames = atoi(argv[i]); npos++; }
        else if (npos == 1) { chr = atoi(argv[i]); npos++; }
        else if (npos == 2) { level = atoi(argv[i]); npos++; }
    }
    if (harness_load_rom())
        return 1;
    snap = (uint8_t *)malloc(SH4_RAM_SIZE);
    sh4_set_io(boot_io_read, boot_io_write, NULL);
    sh4_cstack_on = 1;
    sh4_sleep_handler = sleep_nop;
    sh4_trap_handler = trap_nop;
    hle_install_bios();
    if (pad) {
        sh4_hook_add(0x8C601F5Au, hle_get_peripheral, NULL, "pdGetPeripheral");
        printf("boot: pad on port 0: stick (%d, %d), buttons %08X\n", pad_x, pad_y, pad_buttons);
    }
    ic0 = SH4.icount;

    sh4_strict_io = 1;
    if (init) {
        printf("boot: game init %s (SDK init, GD-ROM, sound, ...)\n", fname(GAME_INIT));
        k = run_step("init", GAME_INIT, none, 0, NULL, 200000000);
        printf("boot: game init %s (%llu instructions)\n", k ? "FAILED" : "ok", (unsigned long long)(SH4.icount - ic0));
    }
    printf("boot: matrix stack / view init %s\n", fname(VIEW_INIT));
    {
        sh4_arg a0 = sh4_ai(0);
        if (run_step("view", VIEW_INIT, &a0, 1, NULL, 5000000)) return 1;
    }
    printf("boot: heap init %s\n", fname(HEAP_INIT));
    if (run_step("heap", HEAP_INIT, none, 0, NULL, 50000000)) return 1;
    printf("boot: task system init %s\n", fname(TASK_INIT));
    if (run_step("tasks", TASK_INIT, none, 0, NULL, 50000000)) return 1;
    sh4_write8(CUR_CHAR, (uint8_t)chr);
    sh4_write16(CUR_LEVEL, (uint16_t)level);
    sh4_write16(CUR_ACT, 0);
    printf("boot: player loader %s (character %d, level %d)\n", fname(PLAYER_LOAD), chr, level);
    k = run_step("loader", PLAYER_LOAD, none, 0, NULL, 50000000);
    printf("boot: loader %s, player task work at %08X, motion work at %08X (%llu instructions so far)\n",
           k ? "FAILED" : "ok", sh4_read32(PLAYER_TWP), sh4_read32(PLAYER_MWP),
           (unsigned long long)(SH4.icount - ic0));
    print_player(0);
    icf = SH4.icount;
    for (f = 1; f <= frames; f++) {
        char what[32];
        snprintf(what, sizeof what, "exec f%d", f);
        if (run_step(what, logic_only ? TASK_EXEC_ALL : LOGIC_STEP, none, 0, NULL, 20000000))
            break;
        if (disp) {
            snprintf(what, sizeof what, "disp f%d", f);
            if (run_step(what, TASK_DISP_ALL, none, 0, NULL, 20000000))
                break;
        }

        if (f <= 5 || f % 10 == 0 || f == frames)
            print_player(f);
    }
    printf("boot: %d frames, %.0f instructions/frame\n", f - 1,
           f > 1 ? (double)(SH4.icount - icf) / (double)(f - 1) : 0.0);
    printf("boot: %d functions auto-stubbed:\n", nstubs);
    for (i = 0; i < nstubs; i++) {
        const sh4_funcinfo *fi = func_of(stubs[i].addr);
        printf("  %-28s %s%s\n", fname(stubs[i].addr), stubs[i].addr >= LIB_START ? "[SDK] " : "[game] ", stubs[i].why);
        (void)fi;
    }
    printf("boot: hardware accesses (%d addresses, %llu TA FIFO words):\n", niostat, (unsigned long long)sq_words);
    for (i = 0; i < niostat && i < 40; i++)
        printf("  %08X  reads %6u  writes %6u\n", iostat[i].addr, iostat[i].reads, iostat[i].writes);
    free(snap);
    return 0;
}