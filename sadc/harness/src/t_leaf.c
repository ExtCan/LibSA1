/*
 * t_leaf.c - smoke test of the decoder/executor on original code: every
 * function of 1ST_READ.BIN that calls no other function (per
 * build/1ST_READ.xrefs.json) is run on random inputs.
 *
 * Inputs: R4-R7 are pointers into a scratch area filled with random words
 * (themselves small integers, scratch pointers or floats) or small integers;
 * FR4-FR11 random floats.  RAM is restored before every run.  Host
 * pass-through is disabled, so wild pointers fault instead of touching host
 * memory.  Checked: no illegal instruction / illegal slot / undefined FPU
 * fault, and the SHC ABI (R8-R14, R15, FR12-FR15 preserved) on return.
 */
#include "harness.h"
#include "sh4_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH 0x8CF00000u
#define SCRATCH_SIZE 0x10000u

static uint8_t *snapshot;

static void fill_scratch(void)
{
    uint32_t a;
    for (a = 0; a < SCRATCH_SIZE; a += 4) {
        uint32_t r = harness_rand(), v;
        switch (r & 7) {
        case 0: case 1: v = SCRATCH + ((harness_rand() % SCRATCH_SIZE) & ~3u); break;
        case 2: v = 0; break;
        case 3: v = (r >> 8) & 0xFF; break;
        case 4: { float f = (float)((int32_t)(harness_rand() % 2000) - 1000) / 10.0f; memcpy(&v, &f, 4); break; }
        case 5: v = 1; break;
        default: v = harness_rand() & 0xFFFF; break;
        }
        sh4_write32(SCRATCH + a, v);
    }
}

int cmd_leafsmoke(int argc, char **argv)
{
    int limit = argc > 0 ? atoi(argv[0]) : 100000;
    uint32_t seed = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 1;
    int trials = argc > 2 ? atoi(argv[2]) : 3;
    int counts[SH4_FAULT_COUNT + 1];
    int i, t, nfun = 0, abi_bad = 0, ok_funcs = 0, genuine = 0;
    int verbose = getenv("LEAF_VERBOSE") != NULL;
    uint64_t ic0;
    if (harness_load_rom())
        return 1;
    memset(counts, 0, sizeof counts);
    snapshot = (uint8_t *)malloc(SH4_RAM_SIZE);
    memcpy(snapshot, sh4_ram(), SH4_RAM_SIZE);
    memset(sh4_ram_dirty, 0, sizeof sh4_ram_dirty);
    {
        int save_pt = sh4_host_passthrough;
        (void)save_pt;
    }
    sh4_host_passthrough = 0;
    harness_srand(seed);
    ic0 = SH4.icount;
    for (i = 0; i < sh4_function_count && nfun < limit; i++) {
        const sh4_funcinfo *fi = &sh4_functions[i];
        int any_ok = 0;
        if (!fi->leaf || fi->size < 2)
            continue;
        nfun++;
        for (t = 0; t < trials; t++) {
            uint32_t saved[16], fsaved[4];
            int k, r;
            harness_ram_restore_dirty(snapshot);
            fill_scratch();
            sh4_reset(&SH4);
            for (r = 0; r < 4; r++) {
                uint32_t x = harness_rand();
                SH4.r[4 + r] = (x & 3) ? SCRATCH + ((harness_rand() % (SCRATCH_SIZE - 0x400)) & ~3u) : (x >> 8) % 64;
            }
            for (r = 0; r < 8; r++)
                SH4.fr.f[4 + r] = (float)((int32_t)(harness_rand() % 20000) - 10000) / 100.0f;
            for (r = 8; r < 15; r++)
                SH4.r[r] = 0x5EED0000u + (uint32_t)r;
            for (r = 0; r < 4; r++)
                SH4.fr.u[12 + r] = 0x3F800000u + (uint32_t)r;
            memcpy(saved, SH4.r, sizeof saved);
            memcpy(fsaved, &SH4.fr.u[12], sizeof fsaved);
            SH4.ilimit = SH4.icount + 2000000;
            k = sh4_call_regs(fi->addr);
            SH4.ilimit = 0;
            if (k == SH4_OK) {
                int bad = 0;
                for (r = 8; r < 16; r++)
                    if (SH4.r[r] != saved[r])
                        bad = 1;
                if (memcmp(fsaved, &SH4.fr.u[12], sizeof fsaved) != 0)
                    bad = 1;
                if (bad) {
                    abi_bad++;
                    k = SH4_FAULT_COUNT;
                    if (verbose || !t) {
                        printf("  ABI: %s (%08X) changed", fi->name, fi->addr);
                        for (r = 8; r < 16; r++)
                            if (SH4.r[r] != saved[r])
                                printf(" R%d", r);
                        for (r = 0; r < 4; r++)
                            if (SH4.fr.u[12 + r] != fsaved[r])
                                printf(" FR%d", 12 + r);
                        printf("\n");
                    }
                } else {
                    any_ok = 1;
                }
            } else if (k == SH4_FAULT_ILLEGAL || k == SH4_FAULT_SLOT || k == SH4_FAULT_UNDEF_FPU) {
                /* genuine only if the faulting word is unmodified ROM code */
                uint32_t pc = SH4.fault.pc;
                int in_rom = sh4_is_ram(pc) && (pc & 0x00FFFFFFu) >= 0x00010000u && (pc & 0x00FFFFFFu) < 0x0067C680u;
                uint16_t orig = 0;
                if (in_rom)
                    memcpy(&orig, snapshot + (pc & 0x00FFFFFFu), 2);
                if (in_rom && orig == SH4.fault.op) {
                    genuine++;
                    printf("  GENUINE %s: %s (%08X) at %08X op %04X: %s\n", sh4_fault_name(k), fi->name, fi->addr,
                           pc, SH4.fault.op, SH4.fault.msg);
                } else if (verbose) {
                    printf("  %s (%08X): %s at %08X op %04X (%s)\n", fi->name, fi->addr, sh4_fault_name(k), pc,
                           SH4.fault.op, in_rom ? "code overwritten by the function" : "jumped outside the image");
                }
            }
            counts[k]++;
        }
        ok_funcs += any_ok;
    }
    memcpy(sh4_ram(), snapshot, SH4_RAM_SIZE);
    sh4_host_passthrough = 2;
    printf("leafsmoke: %d leaf functions x %d trials, %llu instructions\n", nfun, trials,
           (unsigned long long)(SH4.icount - ic0));
    printf("  returned normally      : %d (functions with >=1 clean run: %d)\n", counts[SH4_OK], ok_funcs);
    printf("  unmapped memory access : %d\n", counts[SH4_FAULT_MEM]);
    printf("  misaligned access      : %d\n", counts[SH4_FAULT_ALIGN]);
    printf("  budget (2M insns)      : %d\n", counts[SH4_FAULT_TIMEOUT]);
    printf("  bad branch target      : %d\n", counts[SH4_FAULT_BRANCH]);
    printf("  illegal instruction    : %d (in unmodified original code: %d)\n", counts[SH4_FAULT_ILLEGAL], genuine);
    printf("  illegal slot insn      : %d\n", counts[SH4_FAULT_SLOT]);
    printf("  undefined FPU op       : %d\n", counts[SH4_FAULT_UNDEF_FPU]);
    printf("  TRAPA/SLEEP/other      : %d\n", counts[SH4_FAULT_TRAP] + counts[SH4_FAULT_SLEEP] + counts[SH4_FAULT_HOOK] +
           counts[SH4_FAULT_NATIVE]);
    printf("  ABI violations         : %d\n", abi_bad);
    free(snapshot);
    return genuine ? 1 : 0;
}