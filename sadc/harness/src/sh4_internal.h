/* sh4_internal.h - fast paths shared by the interpreter modules (not public API). */
#ifndef SH4_INTERNAL_H
#define SH4_INTERNAL_H

#include "sh4.h"
#include <string.h>

#if defined(__GNUC__)
#define SH4_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SH4_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define SH4_NORETURN    __attribute__((noreturn))
#define SH4_NOINLINE    __attribute__((noinline))
#else
#define SH4_LIKELY(x)   (x)
#define SH4_UNLIKELY(x) (x)
#define SH4_NORETURN
#define SH4_NOINLINE
#endif

/* one bit per halfword of main RAM: a hook is installed at that address */
extern uint8_t sh4_hookbits[SH4_RAM_SIZE / 16];

/* main RAM (area 3 and its mirrors in P0, P1, P2; P3 is left unmapped) */
static inline int sh4_in_ram(uint32_t a)
{
    return (a & 0x1C000000u) == 0x0C000000u && a < 0xC0000000u;
}

/* called on every taken branch whose target is a hook, the sentinel, or
 * outside main RAM (native function pointers) */
void sh4_branch_special(sh4_cpu *c);

static inline void sh4_branch(sh4_cpu *c, uint32_t t)
{
    c->pc = t;
    if (SH4_LIKELY(sh4_in_ram(t))) {
        uint32_t off = (t & 0x00FFFFFFu) >> 1;
        if (SH4_LIKELY(!(sh4_hookbits[off >> 3] & (1u << (off & 7)))))
            return;
    }
    sh4_branch_special(c);
}

/* dirty map of main RAM (4 KB blocks) for writes made by the interpreter */
extern uint8_t *sh4_ram_base;
extern uint8_t sh4_ram_dirty[SH4_RAM_SIZE >> 12];

static inline void sh4_mark_dirty(const uint8_t *h)
{
    size_t d = (size_t)(h - sh4_ram_base);
    if (d < SH4_RAM_SIZE)
        sh4_ram_dirty[d >> 12] = 1;
}

static inline uint32_t sh4_rd32(sh4_cpu *c, uint32_t a)
{
    uint8_t *p = sh4_page[a >> 20];
    if (SH4_LIKELY(p != NULL && (a & 3) == 0)) {
        uint32_t v;
        memcpy(&v, p + (a & 0xFFFFFu), 4);
        return v;
    }
    return sh4_mem_slow_read(c, a, 4);
}
static inline uint32_t sh4_rd16(sh4_cpu *c, uint32_t a)
{
    uint8_t *p = sh4_page[a >> 20];
    if (SH4_LIKELY(p != NULL && (a & 1) == 0)) {
        uint16_t v;
        memcpy(&v, p + (a & 0xFFFFFu), 2);
        return v;
    }
    return sh4_mem_slow_read(c, a, 2);
}
static inline uint32_t sh4_rd8(sh4_cpu *c, uint32_t a)
{
    uint8_t *p = sh4_page[a >> 20];
    if (SH4_LIKELY(p != NULL))
        return p[a & 0xFFFFFu];
    return sh4_mem_slow_read(c, a, 1);
}
static inline void sh4_wr32(sh4_cpu *c, uint32_t a, uint32_t v)
{
    uint8_t *p = sh4_page[a >> 20];
    if (SH4_LIKELY(p != NULL && (a & 3) == 0)) {
        memcpy(p + (a & 0xFFFFFu), &v, 4);
        sh4_mark_dirty(p + (a & 0xFFFFFu));
        return;
    }
    sh4_mem_slow_write(c, a, v, 4);
}
static inline void sh4_wr16(sh4_cpu *c, uint32_t a, uint32_t v)
{
    uint8_t *p = sh4_page[a >> 20];
    if (SH4_LIKELY(p != NULL && (a & 1) == 0)) {
        uint16_t w = (uint16_t)v;
        memcpy(p + (a & 0xFFFFFu), &w, 2);
        sh4_mark_dirty(p + (a & 0xFFFFFu));
        return;
    }
    sh4_mem_slow_write(c, a, v & 0xFFFFu, 2);
}
static inline void sh4_wr8(sh4_cpu *c, uint32_t a, uint32_t v)
{
    uint8_t *p = sh4_page[a >> 20];
    if (SH4_LIKELY(p != NULL)) {
        p[a & 0xFFFFFu] = (uint8_t)v;
        sh4_mark_dirty(p + (a & 0xFFFFFu));
        return;
    }
    sh4_mem_slow_write(c, a, v & 0xFFu, 1);
}

/* guard stack (sh4_call.c) */
extern sh4_guard *sh4_guard_top;

#endif