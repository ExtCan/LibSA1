/*
 * guard_alloc.h - a page-guard host allocator for libsa1's tests (a "page heap": out-of-bounds accesses of libsa1's heap
 * blocks fault at the faulting instruction, with a report and backtraces, instead of corrupting the heap silently).
 *
 *   every block sits on page(s) of its own, its end against a no-access guard page: a read or write past the end faults
 *     (the block start is aligned to `align`, default 16, so up to align - 1 bytes of slack lie between the end and the
 *     guard page; the slack and the unused front of the first page hold a canary that is checked when the block is freed
 *     and at exit: small overruns and underruns are reported there, with the block's allocation backtrace);
 *   freed blocks' pages are decommitted (no access) and their addresses are not reused while they are in quarantine
 *     (cap: quarantine=MB below; the oldest fully freed regions are recycled first): a use after free faults;
 *   a fault in the guard heap prints what was hit (overflow / use after free / which block, its size, the offset), the
 *     faulting thread's backtrace and the block's allocation (and free) backtrace, then ends the process with exit code
 *     139 (GA_EXIT_CODE); faults elsewhere are left to the program's other handlers (libsa1's world fault handler runs
 *     first: game faults inside worlds are unaffected);
 *   double and invalid frees are reported the same way.
 *
 * Selection (off by default: then ga_malloc / ga_free are malloc / free):
 *   environment SA1_GUARD_ALLOC=1, or SA1_GUARD_ALLOC=<options>: comma-separated align=N (1, 2, 4, 8, 16), quarantine=MB,
 *     junk (fill new blocks with 0xCD instead of 0: uninitialised reads become visible), quiet (no summary at exit);
 *   option --guard-alloc[=<options>] on a test's command line (ga_args removes it from argv).
 *
 * Use: sa1_host_defaults(&h); ga_host(&h);  (or ga_malloc / ga_free inside a test's own counting callbacks).
 * Thread-safe (libsa1's preparations allocate on worker threads). Windows (x86, x64; DbgHelp symbols from the PDB when
 * present) and POSIX (mmap / mprotect, SIGSEGV / SIGBUS; glibc backtraces). x86 backtraces are best effort (frame
 * pointers are omitted at -O2): the faulting instruction and the block are always right.
 *
 * Single-translation-unit tests include guard_alloc.c (it includes this header); the include is guarded.
 */
#ifndef SA1_TEST_GUARD_ALLOC_H
#define SA1_TEST_GUARD_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#include "libsa1.h"

#define GA_EXIT_CODE 139

/* 1 when the guard heap is in use (the first call reads SA1_GUARD_ALLOC) */
int ga_enabled(void);
/* turns the guard heap on (with options as SA1_GUARD_ALLOC takes them; NULL: defaults) before the first allocation */
void ga_enable(const char *options);
/* removes --guard-alloc[=<options>] from argv (enabling the guard heap); returns ga_enabled() */
int ga_args(int *argc, char **argv);

/* a guard block when enabled, else malloc (n 0 is taken as 1) */
void *ga_malloc(size_t n);
/* frees a guard block, or (any other pointer) free() */
void ga_free(void *p);

/* sa1_host callbacks, and installing them into a host when enabled (other fields kept) */
void *ga_host_alloc(void *user, size_t n);
void ga_host_free(void *user, void *p);
void ga_host(sa1_host *h);

/* checks the canaries of every live block now (reports each damaged one); returns the number damaged */
uint32_t ga_check(void);

typedef struct ga_stats {
    uint64_t allocs, frees;             /* guard blocks */
    uint64_t live, live_bytes, peak_bytes;
    uint64_t quarantine_bytes;          /* address space of fully freed regions still kept no-access */
    uint64_t recycled;                  /* regions released from the quarantine (their addresses may be reused) */
    uint32_t damaged;                   /* canary damage found so far (frees, ga_check) */
} ga_stats;

void ga_stats_get(ga_stats *out);

#endif
