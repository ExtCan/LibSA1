/*
 * talkref_drv.c - the test-time driver of harness/tests/sim/talkref/talk_pages.py (G-SA1 1.0.0, prep lane p-refs-T;
 * sadc docs/research/gsa1_100_npc.md 9 and 15.2, test T1).
 *
 * It is the harness's SH-4 interpreter (harness/src: sh4_cpu.c, sh4_mem.c, sh4_call.c, ...) with a small command loop in
 * place of main.c: the ORIGINAL code of the owner's 1ST_READ.BIN typesets one talk page per command into the game's
 * 512 x 64 message buffer, and the buffer goes back to the caller through the pipe. Nothing is written to a file: the
 * caller (talk_pages.py) hashes each buffer in memory and keeps numbers and SHA-1 only.
 *
 * Commands on stdin, one per line; every answer on stdout is binary: int32 status, uint32 payload length, the payload.
 *   R <path>                 load 1ST_READ.BIN at 8C010000 and keep that RAM as the base state   (status: bytes or -1)
 *   F <hexaddr> <path>       base state, then the talk file at its load address (0CB46000 / 0CB4A000, as the game's
 *                            loaders 0C90F2C0 / 0C911D60 / 0C902F00 put it); that RAM is the page state (status: bytes)
 *   T <row> <hexmsgc> <hexptr> <budget>
 *                            page state; B_8C754BFC (the language: the font table row) = row; the game's
 *                            8C055794(msgc) (NH_MSG_Open 70, 384, 500, 52, texture 100, buffer B_8C87E900, the first-page
 *                            path of the runner's 8C05F70A), then 8C054F2E(msgc, ptr) (the text routine).
 *                            status 0 or the fault kind; payload: uint64 instructions, the 0x3C bytes of the window
 *                            structure, 4 pad bytes, then 0x14000 bytes from the buffer (0x10000 = 512 x 64 x 2, plus
 *                            0x4000 past it: a third line writes there, sadc docs/research/v1_npcs.md review R2.4)
 *   Q                        quit
 */
#include "sh4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define LANG_ADDR    0x8C754BFCu    /* B_8C754BFC: the language, 0 J 1 E 2 F 3 S 4 G (READ asm 8C054F2E) */
#define OPEN_FN      0x8C055794u    /* NH_MSG_Open with the runner's numbers (READ asm 8C055794 -> 8C055B80) */
#define TEXT_FN      0x8C054F2Eu    /* the text routine (READ asm 8C054F2E: 0x09 -> 8C055050, 0x07 -> 8C0550FC) */
#define BUF_SPAN     0x14000u
#define MSGC_SIZE    0x3Cu

static uint8_t *snap_base, *snap_page;

static void answer(int32_t status, const void *p, uint32_t n)
{
    fwrite(&status, 4, 1, stdout);
    fwrite(&n, 4, 1, stdout);
    if (n)
        fwrite(p, 1, n, stdout);
    fflush(stdout);
}

static void ram_restore(const uint8_t *snap)
{
    uint8_t *ram = sh4_ram();
    uint32_t off;
    for (off = 0; off < SH4_RAM_SIZE; off += 4096)
        if (memcmp(ram + off, snap + off, 4096))
            memcpy(ram + off, snap + off, 4096);
}

static void cpu_reset(void)
{
    sh4_reset(&SH4);
    sh4_set_fpscr(&SH4, SH4_DEFAULT_FPSCR);
}

int main(void)
{
    char line[2048];
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    if (sh4_mem_init() < 0) {
        fprintf(stderr, "talkref_drv: memory init failed\n");
        return 1;
    }
    cpu_reset();
    sh4_host_fp_game_mode();
    sh4_native_init();
    snap_base = (uint8_t *)malloc(SH4_RAM_SIZE);
    snap_page = (uint8_t *)malloc(SH4_RAM_SIZE);
    if (!snap_base || !snap_page)
        return 1;
    while (fgets(line, sizeof line, stdin)) {
        char *s = line, path[1800];
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (s[0] == 'Q')
            break;
        if (s[0] == 'R' && s[1] == ' ') {
            long k;
            sh4_mem_clear();
            k = sh4_load_file(s + 2, SH4_IMAGE_BASE);
            memcpy(snap_base, sh4_ram(), SH4_RAM_SIZE);
            memcpy(snap_page, snap_base, SH4_RAM_SIZE);
            answer((int32_t)k, NULL, 0);
        } else if (s[0] == 'F' && s[1] == ' ') {
            unsigned addr = 0;
            long k = -1;
            if (sscanf(s + 2, "%x %1799[^\n]", &addr, path) == 2) {
                ram_restore(snap_base);
                k = sh4_load_file(path, addr);
                memcpy(snap_page, sh4_ram(), SH4_RAM_SIZE);
            }
            answer((int32_t)k, NULL, 0);
        } else if (s[0] == 'T' && s[1] == ' ') {
            static uint8_t out[8 + MSGC_SIZE + 4 + BUF_SPAN];
            int row = 0, k;
            unsigned msgc = 0, ptr = 0, budget = 0;
            uint64_t ic0;
            uint32_t r0, buf;
            float f0;
            sh4_arg a[2];
            if (sscanf(s + 2, "%d %x %x %u", &row, &msgc, &ptr, &budget) != 4) {
                answer(-2, NULL, 0);
                continue;
            }
            ram_restore(snap_page);
            cpu_reset();
            sh4_write32(LANG_ADDR, (uint32_t)row);
            ic0 = SH4.icount;
            SH4.ilimit = budget ? SH4.icount + budget : 0;
            a[0] = sh4_ai(msgc);
            k = sh4_invoke(OPEN_FN, a, 1, &r0, &f0);
            if (k == SH4_OK) {
                a[1] = sh4_ai(ptr);
                k = sh4_invoke(TEXT_FN, a, 2, &r0, &f0);
            }
            SH4.ilimit = 0;
            memset(out, 0, sizeof out);
            {
                uint64_t ic = SH4.icount - ic0;
                uint32_t i;
                memcpy(out, &ic, 8);
                for (i = 0; i < MSGC_SIZE; i++)
                    out[8 + i] = sh4_read8(msgc + i);
                buf = sh4_read32(msgc + 0x20);
                if (!sh4_is_ram(buf) || !sh4_is_ram(buf + BUF_SPAN - 1))
                    buf = 0x8C87E900u;
                memcpy(out + 8 + MSGC_SIZE + 4, sh4_ram() + sh4_ram_offset(buf), BUF_SPAN);
            }
            if (k != SH4_OK)
                fprintf(stderr, "talkref_drv: page %08X: %s at pc=%08X addr=%08X: %s\n", ptr, sh4_fault_name(k),
                        SH4.fault.pc, SH4.fault.addr, SH4.fault.msg);
            answer(k, out, (uint32_t)sizeof out);
        } else {
            answer(-3, NULL, 0);
        }
    }
    return 0;
}