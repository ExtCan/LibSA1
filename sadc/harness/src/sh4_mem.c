/*
 * sh4_mem.c - guest memory map.
 *
 * Main RAM (16 MB) is one shared memory object mapped (on a 32-bit Windows
 * LAA host) at 0x8C000000 (P1), 0x0C000000-0x0F000000 (P0 and its area-3
 * mirrors) and 0xAC000000 (P2), so guest addresses equal host addresses and
 * writes through any view are coherent.  Natively compiled decompiled C can
 * then dereference game pointers directly.  On other hosts the RAM is mapped
 * anywhere and only the interpreter (through the page table) can use it.
 *
 * The interpreter translates through sh4_page[] (1 MB pages).  Pages without
 * an entry go through the slow path, which classifies the address:
 *   - store queues (0xE0000000-0xE3FFFFFF): SQ buffers, flushed by PREF
 *   - hardware (Holly/PVR/G1/G2 0x005F0000, AICA 0x00700000, TA FIFO
 *     0x10000000-0x13FFFFFF, BIOS/flash, P4 control registers 0xFF000000+):
 *     the io callback (default: log + return 0; P4 registers read back the
 *     last value written)
 *   - host pass-through (32-bit host only, P0 addresses below 0x0C000000 or
 *     outside DC RAM that are readable host memory): pointers that native
 *     code hands to guest functions (its stack, its string literals)
 *   - anything else: SH4_FAULT_MEM
 */
#include "sh4_internal.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

uint8_t *sh4_page[4096];
uint8_t sh4_hookbits[SH4_RAM_SIZE / 16];
int sh4_host_passthrough = 2;
int sh4_null_page_faults;
int sh4_strict_io;
unsigned sh4_null_touched;
int sh4_io_verbose;

static uint8_t *ram;            /* primary view of main RAM */
uint8_t *sh4_ram_base;
uint8_t sh4_ram_dirty[SH4_RAM_SIZE >> 12];
static uint8_t *vram, *aram;
static int ram_identity;        /* views at the DC addresses */
static uint32_t sq_buf[2][8];
static uint32_t qacr[2];

static sh4_io_read_fn io_readfn = sh4_io_default_read;
static sh4_io_write_fn io_writefn = sh4_io_default_write;
static void *io_user;

/* event log */
static sh4_event *ev_buf;
static size_t ev_cap, ev_n;
static int ev_on;

void sh4_evlog_start(sh4_event *buf, size_t cap)
{
    ev_buf = buf;
    ev_cap = cap;
    ev_n = 0;
    ev_on = 1;
}

size_t sh4_evlog_stop(void)
{
    ev_on = 0;
    return ev_n;
}

void sh4_evlog_add(const sh4_event *e)
{
    if (!ev_on)
        return;
    if (ev_n < ev_cap)
        ev_buf[ev_n] = *e;
    ev_n++;
}

static void ev_io(int type, uint32_t addr, uint32_t val, int size)
{
    sh4_event e;
    if (!ev_on)
        return;
    memset(&e, 0, sizeof e);
    e.type = (uint8_t)type;
    e.size = (uint8_t)size;
    e.addr = addr;
    e.val = val;
    e.pc = SH4.cur_pc;
    sh4_evlog_add(&e);
}
/* ------------------------------------------------------------------ */
/* mapping                                                             */

#ifdef _WIN32
static HANDLE ram_section;

static uint8_t *map_view_at(uint32_t base)
{
    void *want = (void *)(uintptr_t)base;
    void *p;
    if (sizeof(void *) != 4)
        return NULL;
    p = MapViewOfFileEx(ram_section, FILE_MAP_ALL_ACCESS, 0, 0, SH4_RAM_SIZE, want);
    return (uint8_t *)p;
}
#endif

static void set_pages(uint32_t guest, uint32_t size, uint8_t *host)
{
    uint32_t i;
    for (i = 0; i < size >> 20; i++)
        sh4_page[(guest >> 20) + i] = host + ((size_t)i << 20);
}

int sh4_mem_init(void)
{
    static const uint32_t mirrors[] = { 0x0D000000u, 0x0E000000u, 0x0F000000u, 0x8D000000u, 0x8E000000u,
                                        0x8F000000u, SH4_RAM_P2, 0xAD000000u, 0xAE000000u, 0xAF000000u };
    uint32_t seg;
    size_t i;

    if (ram)
        return ram_identity ? 1 : 0;
#ifdef _WIN32
    ram_section = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SH4_RAM_SIZE, NULL);
    if (!ram_section) {
        fprintf(stderr, "sh4_mem_init: CreateFileMapping failed (%lu)\n", GetLastError());
        return -1;
    }
    ram = map_view_at(SH4_RAM_P1);
    if (ram) {
        uint8_t *p0 = map_view_at(SH4_RAM_P0);
        ram_identity = p0 != NULL;
        for (i = 0; i < sizeof mirrors / sizeof mirrors[0]; i++)
            map_view_at(mirrors[i]);    /* best effort: native code mostly uses 8C/0C */
    } else {
        ram = (uint8_t *)MapViewOfFile(ram_section, FILE_MAP_ALL_ACCESS, 0, 0, SH4_RAM_SIZE);
    }
    if (!ram) {
        fprintf(stderr, "sh4_mem_init: MapViewOfFile failed (%lu)\n", GetLastError());
        return -1;
    }
#else
    {
        void *want = (sizeof(void *) == 4) ? (void *)(uintptr_t)SH4_RAM_P1 : NULL;
        ram = (uint8_t *)mmap(want, SH4_RAM_SIZE, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ram == (uint8_t *)MAP_FAILED) {
            ram = NULL;
            return -1;
        }
        ram_identity = (uintptr_t)ram == SH4_RAM_P1;
        (void)mirrors;
        (void)i;
    }
#endif
    sh4_ram_base = ram;
    vram = (uint8_t *)calloc(1, SH4_VRAM_SIZE);
    aram = (uint8_t *)calloc(1, SH4_ARAM_SIZE);
    if (!vram || !aram)
        return -1;

    memset(sh4_page, 0, sizeof sh4_page);
    /* main RAM: area 3 and its mirrors, in P0, P1 and P2.  P3 (0xC0000000)
     * would also reach physical memory with the MMU off, but games do not
     * use it and float garbage used as a pointer (-2.0 .. -2^64) lands
     * there, so it is left unmapped (faults like on the native side). */
    for (seg = 0; seg < 3; seg++) {
        uint32_t s = seg == 0 ? 0u : (0x60000000u + seg * 0x20000000u);   /* 0, 80, A0, C0 */
        uint32_t mir;
        for (mir = 0x0C000000u; mir <= 0x0F000000u; mir += 0x01000000u)
            set_pages(s | mir, SH4_RAM_SIZE, ram);
    }
    /* VRAM (64-bit and 32-bit paths share one buffer) and sound RAM, P1/P2 only */
    for (seg = 1; seg < 3; seg++) {
        uint32_t s = 0x60000000u + seg * 0x20000000u;
        set_pages(s | 0x04000000u, SH4_VRAM_SIZE, vram);
        set_pages(s | 0x05000000u, SH4_VRAM_SIZE, vram);
        set_pages(s | 0x00800000u, SH4_ARAM_SIZE, aram);
    }
    return ram_identity ? 1 : 0;
}

uint8_t *sh4_map_extra(uint32_t guest, uint32_t size)
{
    uint8_t *p = NULL;
    if ((guest & 0xFFFFFu) || !size || (size & 0xFFFFFu))
        return NULL;
    if (sh4_page[guest >> 20])
        return sh4_page[guest >> 20];   /* mapped before (a second set-up in one process) */
#ifdef _WIN32
    if (sizeof(void *) == 4)
        p = (uint8_t *)VirtualAlloc((void *)(uintptr_t)guest, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p && (uintptr_t)p != guest) {
        VirtualFree(p, 0, MEM_RELEASE);
        p = NULL;
    }
#else
    if (sizeof(void *) == 4) {
        p = (uint8_t *)mmap((void *)(uintptr_t)guest, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == (uint8_t *)MAP_FAILED || (uintptr_t)p != guest)
            p = NULL;
    }
#endif
    if (!p)
        return NULL;
    set_pages(guest, size, p);
    return p;
}

uint8_t *sh4_ram(void) { return ram; }

void sh4_mem_clear(void)
{
    if (ram)
        memset(ram, 0, SH4_RAM_SIZE);
}

int sh4_is_ram(uint32_t a) { return sh4_in_ram(a); }
uint32_t sh4_ram_offset(uint32_t a) { return a & 0x00FFFFFFu; }

void *sh4_host_ptr(uint32_t a)
{
    uint8_t *p = sh4_page[a >> 20];
    return p ? p + (a & 0xFFFFFu) : NULL;
}

long sh4_load_file(const char *path, uint32_t addr)
{
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *dst;
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    dst = (uint8_t *)sh4_host_ptr(addr);
    if (!dst || !sh4_in_ram(addr) || (uint32_t)size > SH4_RAM_SIZE - (addr & 0x00FFFFFFu)) {
        fclose(f);
        return -1;
    }
    if (fread(dst, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return size;
}

/* ------------------------------------------------------------------ */
/* io                                                                  */

void sh4_set_io(sh4_io_read_fn readfn, sh4_io_write_fn writefn, void *user)
{
    io_readfn = readfn ? readfn : sh4_io_default_read;
    io_writefn = writefn ? writefn : sh4_io_default_write;
    io_user = user;
}

/* small open-addressing table: P4 register latches + per-address log counts */
#define LATCH_N 1024
static struct { uint32_t addr, val, count; uint8_t used; } latch[LATCH_N];

static int latch_slot(uint32_t addr, int create)
{
    uint32_t h = (addr * 2654435761u) >> 22;
    int i;
    for (i = 0; i < LATCH_N; i++) {
        uint32_t k = (h + (uint32_t)i) & (LATCH_N - 1);
        if (latch[k].used && latch[k].addr == addr)
            return (int)k;
        if (!latch[k].used) {
            if (!create)
                return -1;
            latch[k].used = 1;
            latch[k].addr = addr;
            latch[k].val = 0;
            latch[k].count = 0;
            return (int)k;
        }
    }
    return -1;
}

/* reset the emulated hardware state: store queues, QACR, register latches */
void sh4_hw_reset(void)
{
    memset(sq_buf, 0, sizeof sq_buf);
    memset(qacr, 0, sizeof qacr);
    memset(latch, 0, sizeof latch);
}

uint32_t sh4_io_default_read(uint32_t addr, int size, void *user)
{
    int k = latch_slot(addr, 1);
    uint32_t v = 0;
    (void)user;
    if (k >= 0 && addr >= 0xFF000000u)
        v = latch[k].val;
    if (sh4_io_verbose && k >= 0 && latch[k].count++ < 4)
        fprintf(stderr, "io: read%d  %08X -> %08X (pc %08X)\n", size * 8, addr, v, SH4.cur_pc);
    return v;
}

void sh4_io_default_write(uint32_t addr, uint32_t val, int size, void *user)
{
    int k = latch_slot(addr, 1);
    (void)user;
    if (k >= 0)
        latch[k].val = val;
    if (sh4_io_verbose && k >= 0 && latch[k].count++ < 4)
        fprintf(stderr, "io: write%d %08X <- %08X (pc %08X)\n", size * 8, addr, val, SH4.cur_pc);
}
/* ------------------------------------------------------------------ */
/* slow path                                                           */

enum { REG_FAULT, REG_IO, REG_SQ, REG_HOST };

static int host_accessible(uint32_t a, int size, int write)
{
#ifdef _WIN32
    static uintptr_t c_base, c_end;
    static DWORD c_prot;
    static uintptr_t img_lo, img_hi;
    uintptr_t p = (uintptr_t)a;
    DWORD prot;
    if (sizeof(void *) != 4 || !sh4_host_passthrough)
        return 0;
    if (p < 0x10000u)
        return 0;
    if (sh4_host_passthrough == 2) {
        /* safe mode: the current thread's stack (read/write) and the
         * executable image (read only) */
        ULONG_PTR lo = 0, hi = 0;
        GetCurrentThreadStackLimits(&lo, &hi);
        if (p >= lo && p + (uintptr_t)size <= hi)
            return 1;
        if (!img_lo) {
            const uint8_t *base = (const uint8_t *)GetModuleHandleA(NULL);
            uint32_t pe = *(const uint32_t *)(base + 0x3C);
            img_lo = (uintptr_t)base;
            img_hi = img_lo + *(const uint32_t *)(base + pe + 24 + 56);   /* SizeOfImage */
        }
        if (write || p < img_lo || p + (uintptr_t)size > img_hi)
            return 0;
    }
    if (!(p >= c_base && p + (uintptr_t)size <= c_end)) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)p, &mbi, sizeof mbi) != sizeof mbi)
            return 0;
        if (mbi.State != MEM_COMMIT)
            return 0;
        c_base = (uintptr_t)mbi.BaseAddress;
        c_end = c_base + mbi.RegionSize;
        c_prot = mbi.Protect;
        if (p + (uintptr_t)size > c_end)
            return 0;
    }
    prot = c_prot & 0xFF;
    if (c_prot & (PAGE_GUARD | PAGE_NOACCESS))
        return 0;
    if (write)
        return prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_WRITECOPY ||
               prot == PAGE_EXECUTE_WRITECOPY;
    return prot == PAGE_READONLY || prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READ ||
           prot == PAGE_EXECUTE_READWRITE || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_WRITECOPY;
#else
    (void)a; (void)size; (void)write;
    return 0;
#endif
}

int sh4_host_readable(uint32_t a, int size)
{
    return host_accessible(a, size, 0);
}

static int is_hw_phys(uint32_t phys)
{
    if (sh4_strict_io) {
        /* only the register windows and the TA FIFO games really use */
        return (phys >= 0x005F6800u && phys < 0x005FA000u) || (phys >= 0x00700000u && phys < 0x00711000u) ||
               (phys >= 0x10000000u && phys < 0x12000000u);
    }
    if (phys < 0x00200000u)                         /* boot ROM */
        return 1;
    if (phys >= 0x00200000u && phys < 0x00220000u)  /* flash */
        return 1;
    if (phys >= 0x005F0000u && phys < 0x00800000u)  /* Holly/G1/G2/PVR regs, modem, AICA regs, RTC */
        return 1;
    if (phys >= 0x01000000u && phys < 0x04000000u)  /* G2 external area */
        return 1;
    if (phys >= 0x10000000u && phys < 0x14000000u)  /* TA FIFO, YUV, direct texture path */
        return 1;
    return 0;
}

enum { REG_NULL = 10 };

/* SH7750 on-chip register blocks (P4) and the cache/TLB arrays */
static int is_p4_reg(uint32_t a)
{
    static const uint32_t blocks[] = { 0xFF000000u, 0xFF200000u, 0xFF800000u, 0xFF900000u, 0xFFA00000u, 0xFFC00000u,
                                       0xFFC80000u, 0xFFD00000u, 0xFFD80000u, 0xFFE00000u, 0xFFE80000u, 0xFFF00000u };
    size_t i;
    if (a >= 0xF0000000u && a < 0xF8000000u)
        return 1;
    for (i = 0; i < sizeof blocks / sizeof blocks[0]; i++)
        if (a >= blocks[i] && a < blocks[i] + 0x10000u)
            return 1;
    return 0;
}

static int classify(uint32_t a, int size, int write)
{
    if (a >= 0xE0000000u) {
        if (a < 0xE4000000u)
            return REG_SQ;
        return (!sh4_strict_io || is_p4_reg(a)) ? REG_IO : REG_FAULT;
    }
    if (a >= 0x80000000u)
        return is_hw_phys(a & 0x1FFFFFFFu) ? REG_IO : REG_FAULT;
    if (a < 0x10000u)
        return sh4_null_page_faults ? REG_FAULT : REG_NULL;
    if (host_accessible(a, size, write))
        return REG_HOST;
    if (is_hw_phys(a))
        return REG_IO;
    return REG_FAULT;
}

uint32_t sh4_mem_slow_read(sh4_cpu *c, uint32_t a, int size)
{
    uint8_t *p = sh4_page[a >> 20];
    uint32_t v;
    if (a & (uint32_t)(size - 1))
        sh4_fault(c, SH4_FAULT_ALIGN, a, "misaligned read%d at %08X", size * 8, a);
    if (p) {    /* aligned access to a mapped page (only reached via public helpers) */
        p += a & 0xFFFFFu;
        if (size == 1) return *p;
        if (size == 2) { uint16_t w; memcpy(&w, p, 2); return w; }
        memcpy(&v, p, 4);
        return v;
    }
    switch (classify(a, size, 0)) {
    case REG_SQ: {
        uint32_t w = sq_buf[(a >> 5) & 1][(a >> 2) & 7];
        if (size == 4) return w;
        if (size == 2) return (w >> ((a & 2) * 8)) & 0xFFFF;
        return (w >> ((a & 3) * 8)) & 0xFF;
    }
    case REG_IO:
        v = io_readfn(a, size, io_user);
        ev_io(SH4_EV_IO_READ, a, v, size);
        return v;
    case REG_NULL:      /* boot ROM through a NULL pointer: reads 0 */
        sh4_null_touched++;
        return 0;
    case REG_HOST: {
        const uint8_t *h = (const uint8_t *)(uintptr_t)a;
        if (size == 1) return *h;
        if (size == 2) { uint16_t w; memcpy(&w, h, 2); return w; }
        memcpy(&v, h, 4);
        return v;
    }
    default:
        sh4_fault(c, SH4_FAULT_MEM, a, "read%d from unmapped %08X", size * 8, a);
        return 0;
    }
}

void sh4_mem_slow_write(sh4_cpu *c, uint32_t a, uint32_t v, int size)
{
    uint8_t *p = sh4_page[a >> 20];
    if (a & (uint32_t)(size - 1))
        sh4_fault(c, SH4_FAULT_ALIGN, a, "misaligned write%d at %08X", size * 8, a);
    if (p) {
        p += a & 0xFFFFFu;
        if (size == 1) *p = (uint8_t)v;
        else if (size == 2) { uint16_t w = (uint16_t)v; memcpy(p, &w, 2); }
        else memcpy(p, &v, 4);
        return;
    }
    switch (classify(a, size, 1)) {
    case REG_SQ: {
        uint32_t *w = &sq_buf[(a >> 5) & 1][(a >> 2) & 7];
        if (size == 4) *w = v;
        else if (size == 2) { int s = (a & 2) * 8; *w = (*w & ~(0xFFFFu << s)) | (v << s); }
        else { int s = (a & 3) * 8; *w = (*w & ~(0xFFu << s)) | (v << s); }
        return;
    }
    case REG_IO:
        if (a == 0xFF000038u) qacr[0] = v;
        if (a == 0xFF00003Cu) qacr[1] = v;
        ev_io(SH4_EV_IO_WRITE, a, v, size);
        io_writefn(a, v, size, io_user);
        return;
    case REG_NULL:      /* write to the boot ROM area: ignored */
        sh4_null_touched++;
        return;
    case REG_HOST: {
        uint8_t *h = (uint8_t *)(uintptr_t)a;
        if (size == 1) *h = (uint8_t)v;
        else if (size == 2) { uint16_t w = (uint16_t)v; memcpy(h, &w, 2); }
        else memcpy(h, &v, 4);
        return;
    }
    default:
        sh4_fault(c, SH4_FAULT_MEM, a, "write%d to unmapped %08X", size * 8, a);
    }
}

/* PREF @Rn with Rn in the store queue area: burst the 32-byte queue to
 * (QACRn[4:2] << 26) | (Rn & 0x03FFFFE0) */
void sh4_sq_pref(sh4_cpu *c, uint32_t a)
{
    int q = (a >> 5) & 1, i;
    uint32_t dst = (((qacr[q] >> 2) & 7u) << 26) | (a & 0x03FFFFE0u);
    uint8_t *p = sh4_page[(0x80000000u | dst) >> 20];
    (void)c;
    if (p) {
        memcpy(p + ((0x80000000u | dst) & 0xFFFFFu), sq_buf[q], 32);
        sh4_mark_dirty(p + ((0x80000000u | dst) & 0xFFFFFu));
        return;
    }
    for (i = 0; i < 8; i++) {
        ev_io(SH4_EV_SQ, dst + (uint32_t)i * 4, sq_buf[q][i], 4);
        io_writefn(dst + (uint32_t)i * 4, sq_buf[q][i], 4, io_user);
    }
}

/* ------------------------------------------------------------------ */
/* public accessors                                                    */

uint8_t sh4_read8(uint32_t a) { return (uint8_t)sh4_rd8(&SH4, a); }
uint16_t sh4_read16(uint32_t a) { return (uint16_t)sh4_rd16(&SH4, a); }
uint32_t sh4_read32(uint32_t a) { return sh4_rd32(&SH4, a); }
float sh4_readf(uint32_t a) { uint32_t u = sh4_rd32(&SH4, a); float f; memcpy(&f, &u, 4); return f; }
void sh4_write8(uint32_t a, uint8_t v) { sh4_wr8(&SH4, a, v); }
void sh4_write16(uint32_t a, uint16_t v) { sh4_wr16(&SH4, a, v); }
void sh4_write32(uint32_t a, uint32_t v) { sh4_wr32(&SH4, a, v); }
void sh4_writef(uint32_t a, float v) { uint32_t u; memcpy(&u, &v, 4); sh4_wr32(&SH4, a, u); }