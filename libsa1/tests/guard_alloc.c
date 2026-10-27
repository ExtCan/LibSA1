/*
 * guard_alloc.c - the page-guard host allocator of libsa1's tests (interface and behaviour: guard_alloc.h). The single-
 * translation-unit tests include this file; the include is guarded, so a test and a file it includes may both include it.
 *
 * Layout: regions of reserved address space (16 MB on 64-bit, 4 MB on 32-bit hosts; a block of more than a quarter of that
 * gets a region of its own) are handed out front to back, never reused while the region lives: each block takes its data
 * pages, committed read-write, then one guard page that stays reserved (no access). The block ends `align`-rounded at the
 * guard page; the front of its first page and the tail slack hold the canary GA_PAT. Freeing decommits the data pages.
 * A region without live blocks that is no longer handed out goes to the quarantine (FIFO); past the quarantine cap the
 * oldest region is released and its records are recycled. The book-keeping (records with backtraces, a pointer hash, the
 * region table) lives in malloc memory under one lock.
 */
#ifndef SA1_TEST_GUARD_ALLOC_C
#define SA1_TEST_GUARD_ALLOC_C

#include "guard_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dbghelp.h>
/* the including test's own identifiers of these names (windows.h defines them empty) */
#  undef near
#  undef far
#else
#  include <pthread.h>
#  include <signal.h>
#  include <sys/mman.h>
#  include <unistd.h>
#  if defined(__GLIBC__)
#    include <execinfo.h>
#    define GA_HAVE_BACKTRACE 1
#  endif
#  if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#    define MAP_ANONYMOUS MAP_ANON
#  endif
#  if !defined(MAP_ANONYMOUS) && defined(__linux__)
#    define MAP_ANONYMOUS 0x20
#  endif
#  ifndef MAP_NORESERVE
#    define MAP_NORESERVE 0
#  endif
#endif

#define GA_BT       16              /* frames of an allocation backtrace */
#define GA_FBT      12              /* frames of a free's backtrace */
#define GA_WALK     32              /* frames of a faulting thread */
#define GA_PAT      0xFDu           /* canary byte */
#define GA_JUNK     0xCDu           /* fill of new blocks with the junk option */
#define GA_NONE     0xFFFFFFFFu
#define GA_TOMB     0xFFFFFFFEu

enum { GA_UNUSED = 0, GA_LIVE = 1, GA_FREED = 2 };
enum { GA_R_FREE = 0, GA_R_OPEN = 1, GA_R_CLOSED = 2, GA_R_QUAR = 3 };

typedef struct ga_rec {
    uintptr_t user;                 /* the block */
    size_t size;
    uintptr_t base;                 /* its first data page */
    size_t pages;                   /* data pages (the guard page follows them) */
    uint32_t region, next;          /* the region and its next record (GA_NONE ends; free list when unused) */
    uint32_t state;
    uint32_t nabt, nfbt;
    uint64_t serial, tid, ftid;
    void *abt[GA_BT];
    void *fbt[GA_FBT];
} ga_rec;

typedef struct ga_region {
    uintptr_t base;
    size_t size;                    /* reserved bytes */
    size_t used;                    /* handed out (bytes from base) */
    uint32_t live;                  /* live blocks */
    uint32_t first;                 /* its records, newest first */
    uint32_t state;
    uint32_t qnext;                 /* quarantine FIFO, or the free list of region slots */
} ga_region;

static struct {
    int state;                      /* -1: SA1_GUARD_ALLOC not read yet, 0 off, 1 on */
    size_t align, page, chunk;
    uint64_t qcap;
    int junk, quiet;
    ga_rec *rec;
    uint32_t nrec, caprec, freerec;
    ga_region *reg;
    uint32_t nreg, capreg, open, qhead, qtail, freereg;
    uint32_t *hash;
    uint32_t hcap, hused, hlive;
    uint64_t serial;
    ga_stats st;
} ga_s = { .state = -1, .align = 16, .freerec = GA_NONE, .open = GA_NONE, .qhead = GA_NONE, .qtail = GA_NONE,
        .freereg = GA_NONE };

static void ga_fatal_exit(void);

/* ---- platform: lock, pages, backtraces, symbols ------------------------------------------------------------------- */
#if defined(_WIN32)
static SRWLOCK ga_lock_srw = SRWLOCK_INIT;

static void ga_lock(void)
{
    AcquireSRWLockExclusive(&ga_lock_srw);
}

static void ga_unlock(void)
{
    ReleaseSRWLockExclusive(&ga_lock_srw);
}

static int ga_trylock(void)
{
    return TryAcquireSRWLockExclusive(&ga_lock_srw) != 0;
}

static uint64_t ga_tid(void)
{
    return (uint64_t)GetCurrentThreadId();
}

static void *ga_vm_reserve(size_t n)
{
    return VirtualAlloc(NULL, n, MEM_RESERVE, PAGE_NOACCESS);
}

static int ga_vm_commit(uintptr_t p, size_t n)
{
    return VirtualAlloc((void *)p, n, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

static void ga_vm_decommit(uintptr_t p, size_t n)
{
    VirtualFree((void *)p, n, MEM_DECOMMIT);
}

static void ga_vm_release(uintptr_t p, size_t n)
{
    (void)n;
    VirtualFree((void *)p, 0, MEM_RELEASE);
}

static size_t ga_page_size(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize ? (size_t)si.dwPageSize : 4096u;
}

typedef USHORT(WINAPI *ga_capture_t)(ULONG, ULONG, PVOID *, PULONG);
typedef BOOL(WINAPI *ga_syminit_t)(HANDLE, PCSTR, BOOL);
typedef DWORD(WINAPI *ga_symopt_t)(DWORD);
typedef BOOL(WINAPI *ga_symaddr_t)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
typedef BOOL(WINAPI *ga_symline_t)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
typedef BOOL(WINAPI *ga_walk_t)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64,
                                PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64,
                                PTRANSLATE_ADDRESS_ROUTINE64);

static ga_capture_t ga_capture_fn;
static ga_syminit_t ga_syminit_fn;
static ga_symopt_t ga_symopt_fn;
static ga_symaddr_t ga_symaddr_fn;
static ga_symline_t ga_symline_fn;
static ga_walk_t ga_walk_fn;
static PFUNCTION_TABLE_ACCESS_ROUTINE64 ga_ftab_fn;
static PGET_MODULE_BASE_ROUTINE64 ga_modbase_fn;
static int ga_sym_ready;
static PVOID ga_veh_handle;

static void ga_proc(HMODULE m, const char *name, void *dst, size_t n)
{
    FARPROC f = m ? GetProcAddress(m, name) : NULL;
    memset(dst, 0, n);
    if (f != NULL && n == sizeof f) {
        memcpy(dst, &f, n);
    }
}

static unsigned ga_backtrace(void **out, unsigned max, unsigned skip)
{
    if (ga_capture_fn == NULL) {
        return 0;
    }
    return (unsigned)ga_capture_fn((ULONG)skip, (ULONG)max, out, NULL);
}

/* DbgHelp (loaded at the first report: symbols come from the PDB next to the executable) */
static void ga_sym_init(void)
{
    HMODULE dh;
    if (ga_sym_ready) {
        return;
    }
    ga_sym_ready = 1;
    dh = LoadLibraryA("dbghelp.dll");
    ga_proc(dh, "SymInitialize", &ga_syminit_fn, sizeof ga_syminit_fn);
    ga_proc(dh, "SymSetOptions", &ga_symopt_fn, sizeof ga_symopt_fn);
    ga_proc(dh, "SymFromAddr", &ga_symaddr_fn, sizeof ga_symaddr_fn);
    ga_proc(dh, "SymGetLineFromAddr64", &ga_symline_fn, sizeof ga_symline_fn);
    ga_proc(dh, "StackWalk64", &ga_walk_fn, sizeof ga_walk_fn);
    ga_proc(dh, "SymFunctionTableAccess64", &ga_ftab_fn, sizeof ga_ftab_fn);
    ga_proc(dh, "SymGetModuleBase64", &ga_modbase_fn, sizeof ga_modbase_fn);
    if (ga_symopt_fn != NULL) {
        ga_symopt_fn(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
    }
    {
        /* the executable's directory first (the PDB lies next to it; the image may name it relative to the build's cwd) */
        char sp[MAX_PATH + 8];
        DWORD n = GetModuleFileNameA(NULL, sp, MAX_PATH);
        char *s = n > 0 && n < MAX_PATH ? strrchr(sp, '\\') : NULL;
        if (s != NULL) {
            strcpy(s, ";.");
        } else {
            strcpy(sp, ".");
        }
        if (ga_syminit_fn != NULL && !ga_syminit_fn(GetCurrentProcess(), sp, TRUE)) {
            ga_syminit_fn = NULL;
        }
    }
    if (ga_syminit_fn == NULL) {
        ga_symaddr_fn = NULL;
        ga_symline_fn = NULL;
        ga_walk_fn = NULL;
    }
}

/* one frame: function+offset, file:line (or module+offset); ret: a return address (looked up at ret - 1) */
static void ga_sym_print(FILE *f, unsigned i, void *pc, int ret)
{
    DWORD64 a = (DWORD64)(uintptr_t)pc - (ret ? 1u : 0u), disp = 0;
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *si = (SYMBOL_INFO *)(void *)buf;
    IMAGEHLP_LINE64 ln;
    DWORD ld = 0;
    HMODULE mod = NULL;
    char path[MAX_PATH];
    fprintf(f, "    #%-2u %p", i, pc);
    memset(buf, 0, sizeof buf);
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen = 255;
    if (ga_symaddr_fn != NULL && ga_symaddr_fn(GetCurrentProcess(), a, &disp, si)) {
        fprintf(f, " %s+0x%llx", si->Name, (unsigned long long)(disp + (ret ? 1u : 0u)));
        memset(&ln, 0, sizeof ln);
        ln.SizeOfStruct = sizeof ln;
        if (ga_symline_fn != NULL && ga_symline_fn(GetCurrentProcess(), a, &ld, &ln)) {
            fprintf(f, "  %s:%lu", ln.FileName, (unsigned long)ln.LineNumber);
        }
    } else if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCSTR)pc, &mod) && GetModuleFileNameA(mod, path, sizeof path)) {
        const char *b = strrchr(path, '\\');
        fprintf(f, " %s+0x%llx", b ? b + 1 : path, (unsigned long long)((uintptr_t)pc - (uintptr_t)mod));
    }
    fprintf(f, "\n");
}

static void ga_print_bt(FILE *f, void *const *bt, unsigned n)
{
    unsigned i;
    ga_sym_init();
    for (i = 0; i < n; i++) {
        ga_sym_print(f, i, bt[i], 1);
    }
    if (n == 0) {
        fprintf(f, "    (no backtrace)\n");
    }
}

/* the faulting thread from its context */
static void ga_print_context(FILE *f, const CONTEXT *cx0)
{
    CONTEXT cx = *cx0;
    STACKFRAME64 sf;
    DWORD machine;
    unsigned depth;
    ga_sym_init();
    memset(&sf, 0, sizeof sf);
#if defined(_M_X64) || defined(__x86_64__)
    machine = IMAGE_FILE_MACHINE_AMD64;
    sf.AddrPC.Offset = cx.Rip;
    sf.AddrFrame.Offset = cx.Rbp;
    sf.AddrStack.Offset = cx.Rsp;
#elif defined(_M_IX86) || defined(__i386__)
    machine = IMAGE_FILE_MACHINE_I386;
    sf.AddrPC.Offset = cx.Eip;
    sf.AddrFrame.Offset = cx.Ebp;
    sf.AddrStack.Offset = cx.Esp;
#else
    machine = 0;
#endif
    sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
    if (machine == 0 || ga_walk_fn == NULL) {
        ga_sym_print(f, 0, (void *)(uintptr_t)sf.AddrPC.Offset, 0);
        return;
    }
    for (depth = 0; depth < GA_WALK; depth++) {
        if (!ga_walk_fn(machine, GetCurrentProcess(), GetCurrentThread(), &sf, &cx, NULL, ga_ftab_fn, ga_modbase_fn, NULL)
            || sf.AddrPC.Offset == 0) {
            break;
        }
        ga_sym_print(f, depth, (void *)(uintptr_t)sf.AddrPC.Offset, depth > 0);
    }
}

static void ga_terminate(void)
{
    fflush(stdout);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), GA_EXIT_CODE);
}
#else
static pthread_mutex_t ga_mtx = PTHREAD_MUTEX_INITIALIZER;
#if defined(SA_SIGINFO)
static struct sigaction ga_old_segv, ga_old_bus;
#endif

static void ga_lock(void)
{
    pthread_mutex_lock(&ga_mtx);
}

static void ga_unlock(void)
{
    pthread_mutex_unlock(&ga_mtx);
}

static int ga_trylock(void)
{
    return pthread_mutex_trylock(&ga_mtx) == 0;
}

static uint64_t ga_tid(void)
{
    return (uint64_t)(uintptr_t)pthread_self();
}

static void *ga_vm_reserve(size_t n)
{
    void *p = mmap(NULL, n, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static int ga_vm_commit(uintptr_t p, size_t n)
{
    return mprotect((void *)p, n, PROT_READ | PROT_WRITE) == 0;
}

/* a fresh no-access mapping over the pages: no access, and the memory goes back to the system */
static void ga_vm_decommit(uintptr_t p, size_t n)
{
    if (mmap((void *)p, n, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) == MAP_FAILED) {
        mprotect((void *)p, n, PROT_NONE);
    }
}

static void ga_vm_release(uintptr_t p, size_t n)
{
    munmap((void *)p, n);
}

static size_t ga_page_size(void)
{
    long n = sysconf(_SC_PAGESIZE);
    return n > 0 ? (size_t)n : 4096u;
}

static unsigned ga_backtrace(void **out, unsigned max, unsigned skip)
{
#if defined(GA_HAVE_BACKTRACE)
    void *tmp[GA_WALK + 8];
    int n = backtrace(tmp, (int)(max + skip < GA_WALK + 8 ? max + skip : GA_WALK + 8)), i, k = 0;
    for (i = (int)skip; i < n && k < (int)max; i++) {
        out[k++] = tmp[i];
    }
    return (unsigned)k;
#else
    (void)out;
    (void)max;
    (void)skip;
    return 0;
#endif
}

static void ga_print_bt(FILE *f, void *const *bt, unsigned n)
{
    fflush(f);
#if defined(GA_HAVE_BACKTRACE)
    if (n != 0) {
        backtrace_symbols_fd(bt, (int)n, fileno(f));
        return;
    }
#else
    (void)bt;
#endif
    if (n == 0) {
        fprintf(f, "    (no backtrace)\n");
    }
}

static void ga_terminate(void)
{
    fflush(stdout);
    fflush(stderr);
    _exit(GA_EXIT_CODE);
}
#endif

/* ---- book-keeping ------------------------------------------------------------------------------------------------- */
static size_t ga_round(size_t n, size_t a)
{
    return (n + a - 1u) / a * a;
}

static uint32_t ga_hash_of(uintptr_t p, uint32_t cap)
{
    uint64_t h = (uint64_t)p * 0x9E3779B97F4A7C15ull;
    return (uint32_t)(h >> 32) & (cap - 1u);
}

static uint32_t ga_hash_find(uintptr_t p)
{
    uint32_t h, n;
    if (ga_s.hcap == 0) {
        return GA_NONE;
    }
    for (h = ga_hash_of(p, ga_s.hcap), n = 0; n < ga_s.hcap; h = (h + 1u) & (ga_s.hcap - 1u), n++) {
        uint32_t r = ga_s.hash[h];
        if (r == GA_NONE) {
            return GA_NONE;
        }
        if (r != GA_TOMB && ga_s.rec[r].user == p) {
            return h;
        }
    }
    return GA_NONE;
}

static int ga_hash_insert(uint32_t r)
{
    uint32_t h;
    if ((uint64_t)(ga_s.hused + 1u) * 2u > ga_s.hcap) {
        uint32_t ncap = 1024u, i, *old = ga_s.hash, ocap = ga_s.hcap;
        while ((uint64_t)ncap < (uint64_t)(ga_s.hlive + 1u) * 4u) {
            ncap *= 2u;
        }
        ga_s.hash = (uint32_t *)malloc((size_t)ncap * sizeof *ga_s.hash);
        if (ga_s.hash == NULL) {
            ga_s.hash = old;
            return 0;
        }
        memset(ga_s.hash, 0xFF, (size_t)ncap * sizeof *ga_s.hash);
        ga_s.hcap = ncap;
        ga_s.hused = 0;
        for (i = 0; i < ocap; i++) {
            if (old[i] != GA_NONE && old[i] != GA_TOMB) {
                for (h = ga_hash_of(ga_s.rec[old[i]].user, ncap); ga_s.hash[h] != GA_NONE; h = (h + 1u) & (ncap - 1u)) {
                }
                ga_s.hash[h] = old[i];
                ga_s.hused++;
            }
        }
        free(old);
    }
    for (h = ga_hash_of(ga_s.rec[r].user, ga_s.hcap); ga_s.hash[h] != GA_NONE && ga_s.hash[h] != GA_TOMB; h = (h + 1u) & (ga_s.hcap - 1u)) {
    }
    if (ga_s.hash[h] == GA_NONE) {
        ga_s.hused++;
    }
    ga_s.hash[h] = r;
    ga_s.hlive++;
    return 1;
}

static uint32_t ga_rec_new(void)
{
    uint32_t r;
    if (ga_s.freerec != GA_NONE) {
        r = ga_s.freerec;
        ga_s.freerec = ga_s.rec[r].next;
        return r;
    }
    if (ga_s.nrec == ga_s.caprec) {
        uint32_t ncap = ga_s.caprec ? 2u * ga_s.caprec : 4096u;
        ga_rec *n = (ga_rec *)realloc(ga_s.rec, (size_t)ncap * sizeof *n);
        if (n == NULL || ncap < ga_s.caprec) {
            return GA_NONE;
        }
        ga_s.rec = n;
        ga_s.caprec = ncap;
    }
    return ga_s.nrec++;
}

/* releases a quarantined region: its addresses may be reused, its records are recycled */
static void ga_region_evict(uint32_t i)
{
    ga_region *g = &ga_s.reg[i];
    uint32_t r = g->first;
    ga_vm_release(g->base, g->size);
    while (r != GA_NONE) {
        uint32_t nx = ga_s.rec[r].next;
        ga_s.rec[r].state = GA_UNUSED;
        ga_s.rec[r].next = ga_s.freerec;
        ga_s.freerec = r;
        r = nx;
    }
    ga_s.st.quarantine_bytes -= g->size;
    ga_s.st.recycled++;
    memset(g, 0, sizeof *g);
    g->state = GA_R_FREE;
    g->first = GA_NONE;
    g->qnext = ga_s.freereg;
    ga_s.freereg = i;
}

static void ga_quarantine_trim(uint64_t cap)
{
    while (ga_s.qhead != GA_NONE && ga_s.st.quarantine_bytes > cap) {
        uint32_t i = ga_s.qhead;
        ga_s.qhead = ga_s.reg[i].qnext;
        if (ga_s.qhead == GA_NONE) {
            ga_s.qtail = GA_NONE;
        }
        ga_region_evict(i);
    }
}

/* a region without live blocks that is no longer handed out */
static void ga_region_retire(uint32_t i)
{
    ga_region *g = &ga_s.reg[i];
    g->state = GA_R_QUAR;
    g->qnext = GA_NONE;
    if (ga_s.qtail != GA_NONE) {
        ga_s.reg[ga_s.qtail].qnext = i;
    } else {
        ga_s.qhead = i;
    }
    ga_s.qtail = i;
    ga_s.st.quarantine_bytes += g->size;
    ga_quarantine_trim(ga_s.qcap);
}

static uint32_t ga_region_new(size_t size)
{
    void *p = ga_vm_reserve(size);
    uint32_t i;
    ga_region *g;
    if (p == NULL) {
        ga_quarantine_trim(0);          /* out of address space: give the quarantine back first */
        p = ga_vm_reserve(size);
        if (p == NULL) {
            return GA_NONE;
        }
    }
    if (ga_s.freereg != GA_NONE) {
        i = ga_s.freereg;
        ga_s.freereg = ga_s.reg[i].qnext;
    } else {
        if (ga_s.nreg == ga_s.capreg) {
            uint32_t ncap = ga_s.capreg ? 2u * ga_s.capreg : 256u;
            ga_region *n = (ga_region *)realloc(ga_s.reg, (size_t)ncap * sizeof *n);
            if (n == NULL) {
                ga_vm_release((uintptr_t)p, size);
                return GA_NONE;
            }
            ga_s.reg = n;
            ga_s.capreg = ncap;
        }
        i = ga_s.nreg++;
    }
    g = &ga_s.reg[i];
    memset(g, 0, sizeof *g);
    g->base = (uintptr_t)p;
    g->size = size;
    g->first = GA_NONE;
    g->qnext = GA_NONE;
    g->state = GA_R_CLOSED;
    return i;
}

/* the region of a block of `pages` data pages and the block's first page */
static uint32_t ga_region_take(size_t pages, uintptr_t *base)
{
    size_t need = (pages + 1u) * ga_s.page;
    uint32_t i;
    if (need > ga_s.chunk / 4u) {
        i = ga_region_new(need);        /* a region of its own */
        if (i == GA_NONE) {
            return GA_NONE;
        }
        ga_s.reg[i].used = need;
        *base = ga_s.reg[i].base;
        return i;
    }
    if (ga_s.open != GA_NONE && ga_s.reg[ga_s.open].size - ga_s.reg[ga_s.open].used < need) {
        uint32_t o = ga_s.open;
        ga_s.open = GA_NONE;
        ga_s.reg[o].state = GA_R_CLOSED;
        if (ga_s.reg[o].live == 0) {
            ga_region_retire(o);
        }
    }
    if (ga_s.open == GA_NONE) {
        i = ga_region_new(ga_s.chunk);
        if (i == GA_NONE) {
            return GA_NONE;
        }
        ga_s.reg[i].state = GA_R_OPEN;
        ga_s.open = i;
    }
    i = ga_s.open;
    *base = ga_s.reg[i].base + ga_s.reg[i].used;
    ga_s.reg[i].used += need;
    return i;
}

/* the region holding address a (live or quarantined), GA_NONE when a is not guard heap */
static uint32_t ga_region_of(uintptr_t a)
{
    uint32_t i;
    for (i = 0; i < ga_s.nreg; i++) {
        const ga_region *g = &ga_s.reg[i];
        if (g->state != GA_R_FREE && a - g->base < g->size) {
            return i;
        }
    }
    return GA_NONE;
}

/* the record whose pages or guard page hold a; else the nearest block below a in its region (*exact 0) */
static uint32_t ga_rec_at(uint32_t region, uintptr_t a, int *exact)
{
    uint32_t r, best = GA_NONE;
    *exact = 0;
    for (r = ga_s.reg[region].first; r != GA_NONE; r = ga_s.rec[r].next) {
        const ga_rec *q = &ga_s.rec[r];
        if (a - q->base < (q->pages + 1u) * ga_s.page) {
            *exact = 1;
            return r;
        }
        if (q->base <= a && (best == GA_NONE || q->base > ga_s.rec[best].base)) {
            best = r;
        }
    }
    return best;
}

/* ---- canaries ----------------------------------------------------------------------------------------------------- */
static int ga_pat_ok(uintptr_t p, size_t n, size_t *bad)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        if (b[i] != GA_PAT) {
            *bad = i;
            return 0;
        }
    }
    return 1;
}

/* 0 = intact; else prints what is damaged (front: before the block, tail: after its end) */
static int ga_canary_check(const ga_rec *q, FILE *f)
{
    size_t front = q->user - q->base, tail = q->pages * ga_s.page - front - q->size, bad = 0;
    int damaged = 0;
    if (!ga_pat_ok(q->base, front, &bad)) {
        size_t last = bad, i;
        for (i = bad; i < front; i++) {
            if (((const uint8_t *)q->base)[i] != GA_PAT) {
                last = i;
            }
        }
        if (f != NULL) {
            fprintf(f, "==guard_alloc== HEAP BUFFER UNDERFLOW (found by the canary): bytes %ld .. %ld before the start of "
                    "a %llu-byte block at %p were written\n", -(long)(front - bad), -(long)(front - last),
                    (unsigned long long)q->size, (void *)q->user);
        }
        damaged = 1;
    }
    if (!ga_pat_ok(q->user + q->size, tail, &bad)) {
        if (f != NULL) {
            fprintf(f, "==guard_alloc== HEAP BUFFER OVERFLOW (found by the canary): %llu byte(s) past the end of a %llu-byte "
                    "block at %p were written (the first at offset +%llu = %llu past the end)\n",
                    (unsigned long long)(tail - bad), (unsigned long long)q->size, (void *)q->user,
                    (unsigned long long)(q->size + bad), (unsigned long long)bad);
        }
        damaged = 1;
    }
    return damaged;
}

static void ga_print_rec(FILE *f, const ga_rec *q)
{
    fprintf(f, "==guard_alloc== the block: %llu bytes at %p (allocation #%llu, thread %llu), allocated at:\n",
            (unsigned long long)q->size, (void *)q->user, (unsigned long long)q->serial, (unsigned long long)q->tid);
    ga_print_bt(f, q->abt, q->nabt);
    if (q->state == GA_FREED) {
        fprintf(f, "==guard_alloc== freed by thread %llu at:\n", (unsigned long long)q->ftid);
        ga_print_bt(f, q->fbt, q->nfbt);
    }
}

/* ---- set-up ------------------------------------------------------------------------------------------------------- */
static void ga_atexit(void);

static void ga_parse(const char *s)
{
    char tok[64];
    size_t n;
    while (s != NULL && *s) {
        n = strcspn(s, ",");
        if (n < sizeof tok) {
            memcpy(tok, s, n);
            tok[n] = 0;
            if (!strncmp(tok, "align=", 6)) {
                long a = strtol(tok + 6, NULL, 10);
                if (a == 1 || a == 2 || a == 4 || a == 8 || a == 16) {
                    ga_s.align = (size_t)a;
                }
            } else if (!strncmp(tok, "quarantine=", 11)) {
                ga_s.qcap = (uint64_t)strtoull(tok + 11, NULL, 10) << 20;
            } else if (!strcmp(tok, "junk")) {
                ga_s.junk = 1;
            } else if (!strcmp(tok, "quiet")) {
                ga_s.quiet = 1;
            }
        }
        s += n;
        if (*s == ',') {
            s++;
        }
    }
}

#if defined(_WIN32)
static LONG CALLBACK ga_veh(EXCEPTION_POINTERS *ep);
#elif defined(SA_SIGINFO)
static void ga_sig(int sig, siginfo_t *si, void *uc);
#endif

void ga_enable(const char *options)
{
    if (ga_s.state == 1) {
        return;
    }
    ga_s.state = 1;
    ga_s.align = 16;
    ga_s.page = ga_page_size();
    ga_s.chunk = sizeof(void *) >= 8 ? ((size_t)16u << 20) : ((size_t)4u << 20);
    ga_s.qcap = sizeof(void *) >= 8 ? ((uint64_t)32u << 30) : ((uint64_t)256u << 20);
    ga_parse(options);
#if defined(_WIN32)
    {
        HMODULE k = GetModuleHandleA("kernel32.dll");
        ga_proc(k, "RtlCaptureStackBackTrace", &ga_capture_fn, sizeof ga_capture_fn);
        if (ga_capture_fn == NULL) {
            ga_proc(GetModuleHandleA("ntdll.dll"), "RtlCaptureStackBackTrace", &ga_capture_fn, sizeof ga_capture_fn);
        }
        ga_veh_handle = AddVectoredExceptionHandler(0, ga_veh);   /* last: libsa1's world handler goes first */
    }
#elif defined(SA_SIGINFO)
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = ga_sig;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, &ga_old_segv);
        sigaction(SIGBUS, &sa, &ga_old_bus);
    }
#endif
    atexit(ga_atexit);
    if (!ga_s.quiet) {
        fprintf(stderr, "guard_alloc: on (every libsa1 heap block against a no-access page; align %u, quarantine %llu MB%s)\n",
                (unsigned)ga_s.align, (unsigned long long)(ga_s.qcap >> 20), ga_s.junk ? ", junk fill" : "");
    }
}

int ga_enabled(void)
{
    if (ga_s.state < 0) {
        const char *e = getenv("SA1_GUARD_ALLOC");
        ga_s.state = 0;
        if (e != NULL && *e && strcmp(e, "0") != 0 && strcmp(e, "off") != 0) {
            ga_enable(e);
        }
    }
    return ga_s.state == 1;
}

int ga_args(int *argc, char **argv)
{
    int i, o = 1;
    for (i = 1; i < *argc; i++) {
        if (!strcmp(argv[i], "--guard-alloc")) {
            ga_enable(NULL);
        } else if (!strncmp(argv[i], "--guard-alloc=", 14)) {
            ga_enable(argv[i] + 14);
        } else {
            argv[o++] = argv[i];
        }
    }
    if (o < *argc) {
        argv[o] = NULL;
    }
    *argc = o;
    return ga_enabled();
}

/* ---- allocation --------------------------------------------------------------------------------------------------- */
void *ga_malloc(size_t n)
{
    void *bt[GA_BT];
    unsigned nbt;
    size_t an, pages;
    uintptr_t base, user;
    uint32_t reg, r;
    ga_rec *q;
    if (!ga_enabled()) {
        return malloc(n ? n : 1u);
    }
    if (n == 0) {
        n = 1;
    }
    if (n > ((size_t)-1) / 2u) {
        return NULL;
    }
    an = ga_round(n, ga_s.align);
    pages = (an + ga_s.page - 1u) / ga_s.page;
    nbt = ga_backtrace(bt, GA_BT, 1);
    ga_lock();
    reg = ga_region_take(pages, &base);
    if (reg == GA_NONE) {
        ga_unlock();
        return NULL;
    }
    if (!ga_vm_commit(base, pages * ga_s.page)) {
        ga_quarantine_trim(0);
        if (!ga_vm_commit(base, pages * ga_s.page)) {
            if (ga_s.reg[reg].state == GA_R_CLOSED && ga_s.reg[reg].live == 0) {
                ga_region_retire(reg);
            }
            ga_unlock();
            return NULL;
        }
    }
    r = ga_rec_new();
    if (r == GA_NONE) {
        ga_vm_decommit(base, pages * ga_s.page);
        ga_unlock();
        return NULL;
    }
    user = base + pages * ga_s.page - an;
    q = &ga_s.rec[r];
    memset(q, 0, sizeof *q);
    q->user = user;
    q->size = n;
    q->base = base;
    q->pages = pages;
    q->region = reg;
    q->state = GA_LIVE;
    q->serial = ++ga_s.serial;
    q->tid = ga_tid();
    q->nabt = nbt;
    memcpy(q->abt, bt, nbt * sizeof bt[0]);
    if (!ga_hash_insert(r)) {
        q->state = GA_UNUSED;
        q->next = ga_s.freerec;
        ga_s.freerec = r;
        ga_vm_decommit(base, pages * ga_s.page);
        ga_unlock();
        return NULL;
    }
    q->next = ga_s.reg[reg].first;
    ga_s.reg[reg].first = r;
    ga_s.reg[reg].live++;
    memset((void *)base, GA_PAT, user - base);
    memset((void *)(user + n), GA_PAT, an - n);
    if (ga_s.junk) {
        memset((void *)user, GA_JUNK, n);
    }
    ga_s.st.allocs++;
    ga_s.st.live++;
    ga_s.st.live_bytes += n;
    if (ga_s.st.live_bytes > ga_s.st.peak_bytes) {
        ga_s.st.peak_bytes = ga_s.st.live_bytes;
    }
    ga_unlock();
    return (void *)user;
}

void ga_free(void *p)
{
    void *bt[GA_FBT];
    unsigned nbt;
    uint32_t h, r, reg;
    ga_rec *q;
    if (p == NULL) {
        return;
    }
    if (ga_s.state != 1) {
        free(p);
        return;
    }
    nbt = ga_backtrace(bt, GA_FBT, 1);
    ga_lock();
    h = ga_hash_find((uintptr_t)p);
    if (h == GA_NONE) {
        int exact = 0;
        reg = ga_region_of((uintptr_t)p);
        if (reg == GA_NONE) {
            ga_unlock();
            free(p);            /* not a guard block (malloc memory of the test) */
            return;
        }
        fflush(stdout);
        r = ga_rec_at(reg, (uintptr_t)p, &exact);
        fprintf(stderr, "==guard_alloc== %s: free(%p) by thread %llu\n",
                exact && r != GA_NONE && ga_s.rec[r].user == (uintptr_t)p && ga_s.rec[r].state == GA_FREED ? "DOUBLE FREE"
                                                                                                     : "INVALID FREE",
                p, (unsigned long long)ga_tid());
        ga_print_bt(stderr, bt, nbt);
        if (exact && r != GA_NONE) {
            ga_print_rec(stderr, &ga_s.rec[r]);
        }
        ga_terminate();
    }
    r = ga_s.hash[h];
    q = &ga_s.rec[r];
    if (ga_canary_check(q, NULL)) {
        fflush(stdout);
        ga_canary_check(q, stderr);
        fprintf(stderr, "==guard_alloc== found when thread %llu freed it at:\n", (unsigned long long)ga_tid());
        ga_print_bt(stderr, bt, nbt);
        ga_print_rec(stderr, q);
        ga_terminate();
    }
    ga_s.hash[h] = GA_TOMB;
    ga_s.hlive--;
    ga_vm_decommit(q->base, q->pages * ga_s.page);
    q->state = GA_FREED;
    q->ftid = ga_tid();
    q->nfbt = nbt;
    memcpy(q->fbt, bt, nbt * sizeof bt[0]);
    ga_s.st.frees++;
    ga_s.st.live--;
    ga_s.st.live_bytes -= q->size;
    reg = q->region;
    ga_s.reg[reg].live--;
    if (ga_s.reg[reg].live == 0 && ga_s.reg[reg].state == GA_R_CLOSED) {
        ga_region_retire(reg);
    }
    ga_unlock();
}

void *ga_host_alloc(void *user, size_t n)
{
    (void)user;
    return ga_malloc(n);
}

void ga_host_free(void *user, void *p)
{
    (void)user;
    ga_free(p);
}

void ga_host(sa1_host *h)
{
    if (h != NULL && ga_enabled()) {
        h->alloc = ga_host_alloc;
        h->free = ga_host_free;
    }
}

uint32_t ga_check(void)
{
    uint32_t r, n = 0;
    if (ga_s.state != 1) {
        return 0;
    }
    ga_lock();
    for (r = 0; r < ga_s.nrec; r++) {
        if (ga_s.rec[r].state == GA_LIVE && ga_canary_check(&ga_s.rec[r], NULL)) {
            fflush(stdout);
            ga_canary_check(&ga_s.rec[r], stderr);
            ga_print_rec(stderr, &ga_s.rec[r]);
            n++;
        }
    }
    ga_s.st.damaged += n;
    ga_unlock();
    return n;
}

void ga_stats_get(ga_stats *out)
{
    ga_lock();
    *out = ga_s.st;
    ga_unlock();
}

static void ga_atexit(void)
{
    uint32_t bad = ga_check();
    if (!ga_s.quiet || bad) {
        fflush(stdout);
        fprintf(stderr, "guard_alloc: %llu blocks allocated, %llu freed, %llu live at exit; peak %.1f MB; quarantine %.1f MB, "
                "%llu regions recycled; canaries %s\n", (unsigned long long)ga_s.st.allocs, (unsigned long long)ga_s.st.frees,
                (unsigned long long)ga_s.st.live, (double)ga_s.st.peak_bytes / 1048576.0,
                (double)ga_s.st.quarantine_bytes / 1048576.0, (unsigned long long)ga_s.st.recycled,
                bad ? "DAMAGED" : "intact");
        fflush(stderr);
    }
    if (bad) {
        ga_terminate();
    }
}

/* ---- faults ------------------------------------------------------------------------------------------------------- */
static void ga_fatal_exit(void)
{
    ga_terminate();
}

/* what address a is to the guard heap; 1 = reported (the process is ending) */
static int ga_report_fault(uintptr_t a, const char *access)
{
    uint32_t reg, r;
    int exact = 0, locked;
    const ga_rec *q;
    if (ga_s.state != 1) {
        return 0;
    }
    locked = ga_trylock();      /* a fault inside the allocator itself still reports, without the lock */
    reg = ga_region_of(a);
    if (reg == GA_NONE) {
        if (locked) {
            ga_unlock();
        }
        return 0;
    }
    fflush(stdout);
    r = ga_rec_at(reg, a, &exact);
    q = r != GA_NONE ? &ga_s.rec[r] : NULL;
    if (q != NULL && exact && a >= q->base + q->pages * ga_s.page) {
        fprintf(stderr, "==guard_alloc== HEAP BUFFER OVERFLOW: %s at %p, %llu byte(s) past the end of a %s %llu-byte block "
                "at %p (offset +%llu), thread %llu\n", access, (void *)a, (unsigned long long)(a - (q->user + q->size)),
                q->state == GA_LIVE ? "live" : "freed", (unsigned long long)q->size, (void *)q->user,
                (unsigned long long)(a - q->user), (unsigned long long)ga_tid());
    } else if (q != NULL && exact && q->state == GA_FREED) {
        fprintf(stderr, "==guard_alloc== USE AFTER FREE: %s at %p, offset %+lld of a freed %llu-byte block at %p, thread %llu\n",
                access, (void *)a, (long long)((intptr_t)a - (intptr_t)q->user), (unsigned long long)q->size,
                (void *)q->user, (unsigned long long)ga_tid());
    } else if (q != NULL && exact) {
        fprintf(stderr, "==guard_alloc== FAULT in a live block: %s at %p, offset %+lld of a %llu-byte block at %p, thread %llu\n",
                access, (void *)a, (long long)((intptr_t)a - (intptr_t)q->user), (unsigned long long)q->size,
                (void *)q->user, (unsigned long long)ga_tid());
    } else {
        fprintf(stderr, "==guard_alloc== WILD ACCESS into the guard heap: %s at %p (no block there%s), thread %llu\n", access,
                (void *)a, q != NULL ? "; the nearest block below is shown" : "", (unsigned long long)ga_tid());
    }
    fprintf(stderr, "==guard_alloc== faulting thread:\n");
    return 1;
}

#if defined(_WIN32)
static LONG CALLBACK ga_veh(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    uintptr_t a;
    uint32_t reg, r;
    int exact = 0;
    const char *access;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < 2) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    a = (uintptr_t)er->ExceptionInformation[1];
    access = er->ExceptionInformation[0] == 1 ? "write" : er->ExceptionInformation[0] == 8 ? "execute" : "read";
    if (!ga_report_fault(a, access)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    ga_print_context(stderr, ep->ContextRecord);
    reg = ga_region_of(a);
    r = reg != GA_NONE ? ga_rec_at(reg, a, &exact) : GA_NONE;
    if (r != GA_NONE) {
        ga_print_rec(stderr, &ga_s.rec[r]);
    }
    ga_fatal_exit();
    return EXCEPTION_CONTINUE_SEARCH;
}
#elif defined(SA_SIGINFO)
static void ga_sig_chain(int sig, siginfo_t *si, void *uc)
{
    struct sigaction *o = sig == SIGBUS ? &ga_old_bus : &ga_old_segv;
    if ((o->sa_flags & SA_SIGINFO) && o->sa_sigaction != NULL) {
        o->sa_sigaction(sig, si, uc);
        return;
    }
    if (!(o->sa_flags & SA_SIGINFO) && o->sa_handler != SIG_DFL && o->sa_handler != SIG_IGN) {
        o->sa_handler(sig);
        return;
    }
    {
        struct sigaction d;
        memset(&d, 0, sizeof d);
        d.sa_handler = SIG_DFL;
        sigemptyset(&d.sa_mask);
        sigaction(sig, &d, NULL);       /* the instruction faults again and the default action ends the process */
    }
}

static void ga_sig(int sig, siginfo_t *si, void *uc)
{
    uintptr_t a = (uintptr_t)si->si_addr;
    uint32_t reg, r;
    int exact = 0;
    if (!ga_report_fault(a, "access")) {
        ga_sig_chain(sig, si, uc);
        return;
    }
    {
        void *bt[GA_WALK];
        unsigned n = ga_backtrace(bt, GA_WALK, 0);
        ga_print_bt(stderr, bt, n);
    }
    reg = ga_region_of(a);
    r = reg != GA_NONE ? ga_rec_at(reg, a, &exact) : GA_NONE;
    if (r != GA_NONE) {
        ga_print_rec(stderr, &ga_s.rec[r]);
    }
    ga_fatal_exit();
}
#endif

#endif
