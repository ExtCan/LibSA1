/*
 * hle.c - high-level emulation stubs.
 *
 * hle_install_defaults(): nothing is stubbed by default (the diff tests run
 * the original SDK code; its hardware accesses go to the io log).
 * hle_install_bios(): the boot ROM's syscall vectors (sysinfo, ROM font,
 * flash ROM, GD-ROM/misc, system) that games call through 0x8C0000B0..E0;
 * without a BIOS they are 0.  The stubs log the call and report failure /
 * "completed" so that code polling them terminates.
 */
#include "harness.h"
#include "sh4_internal.h"

#include <stdio.h>

#define BIOS_STUBS 0x8C000100u

static unsigned bios_log_count;
int hle_verbose;

static void hle_bios(sh4_cpu *c, void *u)
{
    int which = (int)(uintptr_t)u;
    static const char *const names[] = { "sysinfo", "romfont", "flashrom", "gdrom/misc", "vector C0", "system" };
    uint32_t r = 0;
    switch (which) {
    case 0:     /* SYSINFO: 0 init, 3 get id */
        r = 0;
        break;
    case 1:     /* ROMFONT: address / lock / unlock */
        r = 0;
        break;
    case 2:     /* FLASHROM: info/read/write/delete: fail */
        r = 0xFFFFFFFFu;
        break;
    case 3:     /* GD-ROM (R6 = -1) or misc (R6 = 0); R7 selects the function */
        if (c->r[6] == 0xFFFFFFFFu) {
            switch (c->r[7]) {
            case 0: r = 1; break;                       /* send command -> request id */
            case 1: r = 2; break;                       /* check command -> completed */
            case 4:                                     /* check drive -> standby, GD-ROM */
                if (sh4_is_ram(c->r[4])) {
                    sh4_write32(c->r[4], 2);
                    sh4_write32(c->r[4] + 4, 0x80);
                }
                r = 0;
                break;
            default: r = 0; break;
            }
        }
        break;
    default:
        r = 0;
        break;
    }
    if (hle_verbose || bios_log_count < 16)
        fprintf(stderr, "hle: BIOS %s R4=%08X R5=%08X R6=%08X R7=%08X -> %08X (from %08X)\n", names[which], c->r[4],
                c->r[5], c->r[6], c->r[7], r, c->pr);
    bios_log_count++;
    c->r[0] = r;
}

void hle_install_bios(void)
{
    static const uint32_t vec[] = { 0x8C0000B0u, 0x8C0000B4u, 0x8C0000B8u, 0x8C0000BCu, 0x8C0000C0u, 0x8C0000E0u };
    int i;
    for (i = 0; i < 6; i++) {
        uint32_t stub = BIOS_STUBS + (uint32_t)i * 0x10;
        sh4_write32(vec[i], stub);
        sh4_write16(stub, 0x000B);      /* RTS; NOP in case the hook is removed */
        sh4_write16(stub + 2, 0x0009);
        sh4_hook_add(stub, hle_bios, (void *)(uintptr_t)i, "bios");
    }
}

void hle_install_defaults(int verbose)
{
    hle_verbose = verbose;
}