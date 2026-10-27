/*
 * sa1_world.c - world arena, guest segment table, host-stack window, extension and collision
 * segments, and world faults (Windows VEH / Linux SIGSEGV). See sa1_world.h and sadc
 * docs/libsa1_integration.md 2.1, 2.2, 6.1, 6.9, 8.1.
 */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE                     /* REG_RIP / REG_ERR in <ucontext.h> */
#endif

#include "sa1_world.h"
#include "sa1g_text.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#endif

#define SA1G_MXCSR_GAME 0xFFC0u         /* round toward zero, FTZ, DAZ, all exceptions masked (8.1) */
#define SA1G_TRAP_RESERVE (SA1G_SEG_SIZE + SA1G_GUARD_SIZE)    /* + slack for field offsets */
#define SA1G_COMMIT_CHUNK 0x10000u

#if UINTPTR_MAX > 0xFFFFFFFFu
#define SA1G_TRAP_PER_SEGMENT 1         /* 64-bit: one trap reservation per unmapped segment */
#else
#define SA1G_TRAP_PER_SEGMENT 0         /* 32-bit: one shared trap reservation */
#endif

/* ---- process globals read by the lowered code (sa1/guest.h, profile ga64) and by rt ------------- */
uintptr_t sa1g_seg[32];
uint8_t *sa1g_ram_base;
float *sa1_rt_xmtrx;                    /* rt.h: the ticking world's XMTRX */
const sa1g_fnimage *sa1g_fn_active[SA1G_FN_ACTIVE_MAX + 1];    /* the code map's overlay images (2.5) */
const sa1g_pad *sa1_rt_pad;             /* rt_input.c: the ticking world's RT_PAD[4] (same layout) */

_Static_assert(sizeof(sa1g_pad) == 20, "sa1g_pad must keep rt_input.c's RT_PAD layout");
_Static_assert(offsetof(sa1g_pad, x1) == 12, "sa1g_pad must keep rt_input.c's RT_PAD layout");

static sa1g_world *volatile sa1g_cur;   /* the entered world */
static volatile int sa1g_armed;         /* an SA1G_WORLD_TRY of sa1g_cur is active */
static volatile int sa1g_in_handler;    /* the fault handler is running (no nesting) */
static int sa1g_refs;                   /* live worlds (process state exists while > 0) */
static uint8_t *sa1g_trap[32];          /* trap reservation per segment (32-bit: all the same) */
static uint8_t *sa1g_negtrap;           /* segment 0x13 */
static uintptr_t sa1g_idle_seg[32];     /* segment table outside worlds: traps only */

#ifdef _WIN32
static SRWLOCK sa1g_tick_srw = SRWLOCK_INIT;
static SRWLOCK sa1g_proc_srw = SRWLOCK_INIT;
static SRWLOCK sa1g_table_srw = SRWLOCK_INIT;
static volatile DWORD sa1g_tid;
static PVOID sa1g_veh_handle;
#else
static pthread_mutex_t sa1g_tick_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t sa1g_proc_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t sa1g_table_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_t sa1g_tid;
static struct sigaction sa1g_old_segv;
static int sa1g_sig_installed;
#endif

/* ---- virtual memory ------------------------------------------------------------------------------ */
#ifdef _WIN32
static void *sa1g_vm_reserve(size_t n)
{
    return VirtualAlloc(NULL, n, MEM_RESERVE, PAGE_NOACCESS);
}

static int sa1g_vm_commit(void *p, size_t n)
{
    return VirtualAlloc(p, n, MEM_COMMIT, PAGE_READWRITE) == p ? 0 : -1;
}

static int sa1g_vm_protect(void *p, size_t n, int writable)
{
    DWORD old;
    return VirtualProtect(p, n, writable ? PAGE_READWRITE : PAGE_READONLY, &old) ? 0 : -1;
}

static void sa1g_vm_release(void *p, size_t n)
{
    (void)n;
    VirtualFree(p, 0, MEM_RELEASE);
}
#else
static void *sa1g_vm_reserve(size_t n)
{
    void *p = mmap(NULL, n, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

static int sa1g_vm_commit(void *p, size_t n)
{
    return mprotect(p, n, PROT_READ | PROT_WRITE);
}

static int sa1g_vm_protect(void *p, size_t n, int writable)
{
    return mprotect(p, n, writable ? PROT_READ | PROT_WRITE : PROT_READ);
}

static void sa1g_vm_release(void *p, size_t n)
{
    munmap(p, n);
}
#endif

/* ---- locks --------------------------------------------------------------------------------------- */
void sa1g_tick_lock(void)
{
#ifdef _WIN32
    AcquireSRWLockExclusive(&sa1g_tick_srw);
#else
    pthread_mutex_lock(&sa1g_tick_mtx);
#endif
}

void sa1g_tick_unlock(void)
{
#ifdef _WIN32
    ReleaseSRWLockExclusive(&sa1g_tick_srw);
#else
    pthread_mutex_unlock(&sa1g_tick_mtx);
#endif
}

void sa1g_table_lock(void)
{
#if defined(_WIN32)
    AcquireSRWLockExclusive(&sa1g_table_srw);
#else
    pthread_mutex_lock(&sa1g_table_mtx);
#endif
}

void sa1g_table_unlock(void)
{
#if defined(_WIN32)
    ReleaseSRWLockExclusive(&sa1g_table_srw);
#else
    pthread_mutex_unlock(&sa1g_table_mtx);
#endif
}

static void sa1g_proc_lock(void)
{
#ifdef _WIN32
    AcquireSRWLockExclusive(&sa1g_proc_srw);
#else
    pthread_mutex_lock(&sa1g_proc_mtx);
#endif
}

static void sa1g_proc_unlock(void)
{
#ifdef _WIN32
    ReleaseSRWLockExclusive(&sa1g_proc_srw);
#else
    pthread_mutex_unlock(&sa1g_proc_mtx);
#endif
}

static int sa1g_on_world_thread(void)
{
#ifdef _WIN32
    return GetCurrentThreadId() == sa1g_tid;
#else
    return pthread_equal(pthread_self(), sa1g_tid) != 0;
#endif
}

/* ---- game text bounds (sa1g_text.h) ------------------------------------------------------------- */
#if defined(_WIN32)
/* different bodies so identical-code folding can never merge the two markers */
__attribute__((section(".text$sa1g_a"), used, noinline)) int sa1g_text_begin_marker(void);
__attribute__((section(".text$sa1g_z"), used, noinline)) int sa1g_text_end_marker(void);
__attribute__((section(".text$sa1g_a"), used, noinline)) int sa1g_text_begin_marker(void)
{
    __asm__ volatile("");
    return 0x5A1A;
}

__attribute__((section(".text$sa1g_z"), used, noinline)) int sa1g_text_end_marker(void)
{
    __asm__ volatile("");
    return 0x5A1F;
}

void sa1g_text_bounds(uintptr_t *begin, uintptr_t *end)
{
    *begin = (uintptr_t)sa1g_text_begin_marker;
    *end = (uintptr_t)sa1g_text_end_marker;
}
#else
extern char __start_sa1g_text[] __attribute__((weak, visibility("hidden")));
extern char __stop_sa1g_text[] __attribute__((weak, visibility("hidden")));

void sa1g_text_bounds(uintptr_t *begin, uintptr_t *end)
{
    *begin = (uintptr_t)__start_sa1g_text;
    *end = (uintptr_t)__stop_sa1g_text;
}
#endif

int sa1g_text_contains(uintptr_t pc)
{
    uintptr_t b, e;
    sa1g_text_bounds(&b, &e);
    return b != 0 && pc >= b && pc < e;
}

static int sa1g_trap_contains(uintptr_t pc)
{
    int s;
    if (sa1g_negtrap && pc - (uintptr_t)sa1g_negtrap < SA1G_TRAP_RESERVE) {
        return 1;
    }
    for (s = 0; s < 32; s++) {
        if (sa1g_trap[s] && pc - (uintptr_t)sa1g_trap[s] < SA1G_TRAP_RESERVE) {
            return 1;
        }
    }
    return 0;
}

/* ---- names --------------------------------------------------------------------------------------- */
const char *sa1g_fault_name(int32_t kind)
{
    switch (kind) {
    case SA1G_FAULT_NONE: return "none";
    case SA1G_FAULT_TRAP: return "unmapped segment";
    case SA1G_FAULT_NEGTRAP: return "negated-pointer segment 0x13";
    case SA1G_FAULT_NULL: return "NULL page";
    case SA1G_FAULT_GUARD: return "guard";
    case SA1G_FAULT_STACK: return "stack window";
    case SA1G_FAULT_COLSEG: return "collision segment";
    case SA1G_FAULT_EXT: return "extension segment";
    case SA1G_FAULT_ENCODE: return "encode";
    case SA1G_FAULT_CODEMAP: return "code map";
    case SA1G_FAULT_UNIMPL: return "unimplemented";
    case SA1G_FAULT_FATAL: return "fatal";
    }
    return "?";
}

const char *sa1g_status_string(sa1_status st)
{
    if (st == SA1_ERR_GAME_FAULT) {
        return "game code fault";
    }
    return sa1_status_string(st);
}

sa1_status sa1g_fault_status(const sa1g_fault *f)
{
    switch (f->kind) {
    case SA1G_FAULT_NONE: return SA1_OK;
    case SA1G_FAULT_UNIMPL: return SA1_ERR_NOT_IMPLEMENTED;
    default: return SA1_ERR_GAME_FAULT;
    }
}

/* ---- fault records -------------------------------------------------------------------------------- */
typedef struct sa1g_where {
    int32_t kind;
    int32_t seg;
    uint32_t guest;
} sa1g_where;

/* what a host address is to the world (access violations) */
static void sa1g_classify(const sa1g_world *w, uintptr_t a, int access, sa1g_where *o)
{
    int s;
    o->kind = SA1G_FAULT_NONE;
    o->seg = -1;
    o->guest = 0;
    if (a < 0x10000u) {
        o->kind = SA1G_FAULT_NULL;
        o->seg = 0;
        o->guest = (uint32_t)a;
        return;
    }
    if (sa1g_negtrap && a - (uintptr_t)sa1g_negtrap < SA1G_TRAP_RESERVE) {
        o->kind = SA1G_FAULT_NEGTRAP;
        o->seg = SA1G_SEG_NEGTRAP;
        o->guest = 0x73000000u | (uint32_t)((a - (uintptr_t)sa1g_negtrap) & 0xFFFFFFu);
        return;
    }
    for (s = 0; s < 32; s++) {
        if (sa1g_trap[s] && a - (uintptr_t)sa1g_trap[s] < SA1G_TRAP_RESERVE) {
            o->kind = SA1G_FAULT_TRAP;
            o->seg = SA1G_TRAP_PER_SEGMENT ? s : -1;
            o->guest = (SA1G_TRAP_PER_SEGMENT ? (uint32_t)s << 24 : 0u)
                       | (uint32_t)((a - (uintptr_t)sa1g_trap[s]) & 0xFFFFFFu);
            return;
        }
    }
    if (w == NULL) {
        return;
    }
    if (w->reserve && a - (uintptr_t)w->reserve < w->reserve_size) {
        o->kind = SA1G_FAULT_GUARD;
        o->seg = SA1G_SEG_ARENA;
        o->guest = 0x8C000000u + (uint32_t)(a - (uintptr_t)w->ram);
        return;
    }
    if (w->ext_reserve && a - (uintptr_t)w->ext_reserve < w->ext_reserve_size) {
        o->kind = a - (uintptr_t)w->ext < SA1G_EXT_SIZE ? SA1G_FAULT_EXT : SA1G_FAULT_GUARD;
        o->seg = SA1G_SEG_EXT;
        o->guest = SA1G_EXT_GUEST + (uint32_t)(a - (uintptr_t)w->ext);
        return;
    }
    if (w->shared && w->shared->col_reserve && a - (uintptr_t)w->shared->col_reserve < w->shared->col_reserve_size) {
        o->kind = SA1G_FAULT_COLSEG;
        o->guest = SA1G_COL_GUEST + (uint32_t)(a - (uintptr_t)w->shared->col);
        o->seg = (int32_t)((o->guest >> 24) & 31u);
        (void)access;
        return;
    }
    if (w->stack_top && a - (w->stack_top - SA1G_STACK_WINDOW) < SA1G_STACK_WINDOW) {
        o->kind = SA1G_FAULT_STACK;
        o->seg = SA1G_SEG_STACK;
        o->guest = SA1G_STACK_GUEST | (uint32_t)(a - (w->stack_top - SA1G_STACK_WINDOW));
        return;
    }
}

static void sa1g_fault_set(sa1g_world *w, int32_t kind, int32_t seg, uint32_t guest, uint32_t sig, uintptr_t host,
                           uintptr_t pc, int32_t access, uint32_t dc_func, const char *fmt, ...)
{
    sa1g_fault *f = &w->fault;
    uintptr_t tb, te;
    const char *name;
    va_list ap;
    memset(f, 0, sizeof *f);
    f->kind = kind;
    f->seg = seg;
    f->guest = guest;
    f->sig = sig;
    f->host = host;
    f->pc = pc;
    f->access = access;
    f->dc_func = dc_func;
    f->step = w->steps;
    sa1g_text_bounds(&tb, &te);
    if (pc && sa1g_text_contains(pc)) {
        f->text_off = pc - tb;
    }
    name = dc_func ? sa1_fn_name(dc_func) : NULL;
    if (name) {
        snprintf(f->func, sizeof f->func, "%s", name);
    } else if (dc_func) {
        snprintf(f->func, sizeof f->func, "func_%08X", (unsigned)dc_func);
    }
    va_start(ap, fmt);
    vsnprintf(f->msg, sizeof f->msg, fmt, ap);
    va_end(ap);
    w->dead = 1;
    w->dead_status = sa1g_fault_status(f);
    w->faults++;
}

SA1G_NORETURN static void sa1g_unwind(void)
{
    sa1g_world *w = sa1g_cur;
    if (w == NULL || !sa1g_armed) {
        fprintf(stderr, "sa1: world fault outside a world call: %s\n", w ? w->fault.msg : "(no world)");
        abort();
    }
    sa1g_armed = 0;
    longjmp(w->jb, 1);
}

static sa1g_world *sa1g_soft_world(const char *what)
{
    sa1g_world *w = sa1g_cur;
    if (w == NULL || !sa1g_armed) {
        fprintf(stderr, "sa1: %s outside a world call\n", what);
        abort();
    }
    return w;
}

static uint32_t sa1g_caller_dc(void);

void sa1g_fault_encode(const void *host)
{
    sa1g_world *w = sa1g_soft_world("encode fault");
    uint32_t dc = sa1g_caller_dc();
    sa1g_fault_set(w, SA1G_FAULT_ENCODE, -1, 0, 0, (uintptr_t)host, 0, SA1G_ACCESS_WRITE, dc,
                   "encode: host address %p is in no guest segment (arena, 0x08-0x0B, 0x18, stack window)%s%s",
                   host, dc ? " in " : "", dc ? (sa1_fn_name(dc) ? sa1_fn_name(dc) : "?") : "");
    sa1g_unwind();
}

void sa1g_fault_fn(uint32_t g, uint32_t sig)
{
    sa1g_world *w = sa1g_soft_world("code-map fault");
    const char *name = sa1_fn_name(g);
    if (name != NULL) {
        sa1g_fault_set(w, SA1G_FAULT_CODEMAP, (int32_t)((g >> 24) & 31u), g, sig, 0, 0, SA1G_ACCESS_EXEC, g,
                       "code map: indirect call of %08X (%s) with signature %08X: signature mismatch",
                       (unsigned)g, name, (unsigned)sig);
    } else {
        sa1g_fault_set(w, SA1G_FAULT_CODEMAP, (int32_t)((g >> 24) & 31u), g, sig, 0, 0, SA1G_ACCESS_EXEC, 0,
                       "code map: indirect call of %08X: no native function in this build", (unsigned)g);
    }
    sa1g_unwind();
}

void sa1g_unimpl(uint32_t g, const char *name)
{
    sa1g_world *w = sa1g_soft_world("unimplemented function");
    sa1g_fault_set(w, SA1G_FAULT_UNIMPL, (int32_t)((g >> 24) & 31u), g, 0, 0, 0, SA1G_ACCESS_EXEC, g,
                   "unimplemented: %s (%08X) has no implementation in this build (fatal stub)", name,
                   (unsigned)g);
    sa1g_unwind();
}

void sa1g_fatal(uint32_t dc_func, const char *what)
{
    sa1g_world *w = sa1g_soft_world("fatal game state");
    sa1g_fault_set(w, SA1G_FAULT_FATAL, -1, 0, 0, 0, 0, 0, dc_func, "fatal: %s (in %08X)", what,
                   (unsigned)dc_func);
    sa1g_unwind();
}

/* encode of an address outside the arena (guest.h fast path): extension, collision, stack window, and
 * the raw words of segment 0x13 */
uint32_t sa1g_encode_slow(const void *p)
{
    sa1g_world *w = sa1g_cur;
    uint32_t g;
    /* Segment 0x13 (2.1, R6b): a negated arena address (COLOBJ_HEAD_LINK = 0 - &D_8C02D548) that lowered
     * code copied through a pointer local, i.e. decoded into the trap reservation and stored again
     * (func_8C02CF20: next->prev = co->prev). Only the raw word comes back; any access through it still
     * faults. 0 - (P1 arena address) = 0x73xxxxxx (offset > 0x700000), 0 - (P0 overlay address) =
     * 0xF3xxxxxx, the inverse of the arena's P1 / P0 convention. */
    if (sa1g_negtrap != NULL && (uintptr_t)p - (uintptr_t)sa1g_negtrap < SA1G_SEG_SIZE) {
        uint32_t off = (uint32_t)((uintptr_t)p - (uintptr_t)sa1g_negtrap);
        return (off > 0x700000u ? 0x73000000u : 0xF3000000u) | off;
    }
    if (w != NULL) {
        /* sa1/guest.h's canonical encoding of the non-arena segments */
        g = sa1g_encode_segment(p, SA1G_SEG_EXT, (uintptr_t)w->ext, w->ext ? SA1G_EXT_SIZE : 0u);
        if (g == 0 && w->shared != NULL && w->shared->col != NULL) {
            g = sa1g_encode_segment(p, SA1G_SEG_COL0, (uintptr_t)w->shared->col, SA1G_COL_SIZE);
        }
        if (g == 0 && w->stack_top != 0) {
            g = sa1g_encode_segment(p, SA1G_SEG_STACK, w->stack_top - SA1G_STACK_WINDOW, SA1G_STACK_WINDOW);
        }
        if (g != 0) {
            return g;
        }
    }
    sa1g_fault_encode(p);
}

/* ---- access violations (6.9) ---------------------------------------------------------------------- */
SA1G_NORETURN static void sa1g_fault_trampoline(void)
{
    sa1g_unwind();
}

/*
 * An access violation while the world runs. It is a world fault only when the faulting instruction is
 * game/rt text (or a trap address: a jump into a trap), or (win64) a caller within 4 frames is, and the
 * address is one of the world's: NULL page, a trap, a guard, the extension / collision segment or the
 * stack window. Fills the fault record; 1 = redirect to the trampoline.
 */
static int sa1g_hw_fault(sa1g_world *w, uintptr_t addr, uintptr_t pc, int32_t access, int game_frame,
                         uint32_t dc_func)
{
    sa1g_where wh;
    char where[64], func[80];
    const char *name;
    if (!(sa1g_text_contains(pc) || sa1g_trap_contains(pc) || game_frame)) {
        return 0;
    }
    sa1g_classify(w, addr, access, &wh);
    if (wh.kind == SA1G_FAULT_NONE) {
        return 0;
    }
    if (wh.seg >= 0) {
        if (wh.kind == SA1G_FAULT_NEGTRAP) {
            snprintf(where, sizeof where, "segment 0x13 = 0 - %08X", (unsigned)(0u - wh.guest));
        } else {
            snprintf(where, sizeof where, "segment 0x%02X", (unsigned)wh.seg);
        }
    } else {
        snprintf(where, sizeof where, "segment unknown (shared trap)");
    }
    name = dc_func ? sa1_fn_name(dc_func) : NULL;
    if (name) {
        snprintf(func, sizeof func, "in %s (%08X)", name, (unsigned)dc_func);
    } else if (sa1g_text_contains(pc)) {
        uintptr_t tb, te;
        sa1g_text_bounds(&tb, &te);
        snprintf(func, sizeof func, "in game text +0x%lX", (unsigned long)(pc - tb));
    } else {
        snprintf(func, sizeof func, "called from game text");
    }
    sa1g_fault_set(w, wh.kind, wh.seg, wh.guest, 0, addr, pc, access, dc_func,
                   "access violation (%s) at guest %08X (%s, %s) %s; host %p, pc %p",
                   access == SA1G_ACCESS_WRITE ? "write" : access == SA1G_ACCESS_EXEC ? "execute" : "read",
                   (unsigned)wh.guest, sa1g_fault_name(wh.kind), where, func, (void *)addr, (void *)pc);
    return 1;
}

#ifdef _WIN32
#if defined(_WIN64)
/* Walk up to 12 frames: is a frame within the first 4 in game text, and which code-map function is the
 * innermost game frame? (diagnostics: the DC function of the fault) */
static void sa1g_win64_walk(const CONTEXT *cx0, int *game_frame, uint32_t *dc_func)
{
    CONTEXT cx = *cx0;
    int depth;
    for (depth = 0; depth < 12 && cx.Rip != 0; depth++) {
        DWORD64 base = 0;
        PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(cx.Rip, &base, NULL);
        if (sa1g_text_contains((uintptr_t)cx.Rip)) {
            if (depth < 4) {
                *game_frame = 1;
            }
            if (rf != NULL) {
                uint32_t dc = sa1_fn_addr((const void *)(uintptr_t)(base + rf->BeginAddress));
                if (dc != 0) {
                    *dc_func = dc;
                    return;
                }
            }
        }
        if (rf == NULL) {
            cx.Rip = *(const DWORD64 *)(uintptr_t)cx.Rsp;      /* leaf: return address on top */
            cx.Rsp += 8;
        } else {
            PVOID hd = NULL;
            DWORD64 ef = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, cx.Rip, rf, &cx, &hd, &ef, NULL);
        }
    }
}

/* the innermost code-map function on the current stack (software faults: encode) */
static uint32_t sa1g_caller_dc(void)
{
    CONTEXT cx;
    int game_frame = 0;
    uint32_t dc = 0;
    RtlCaptureContext(&cx);
    sa1g_win64_walk(&cx, &game_frame, &dc);
    return dc;
}
#endif

static LONG CALLBACK sa1g_veh(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *cx = ep->ContextRecord;
    sa1g_world *w = sa1g_cur;
    uintptr_t addr, pc;
    int32_t access;
    int handled, game_frame = 0;
    uint32_t dc = 0;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < 2) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (w == NULL || !w->entered || !sa1g_armed || sa1g_in_handler || !sa1g_on_world_thread()) {
        return EXCEPTION_CONTINUE_SEARCH;       /* not ours: the host's crash handling */
    }
    addr = (uintptr_t)er->ExceptionInformation[1];
    pc = (uintptr_t)er->ExceptionAddress;
    access = er->ExceptionInformation[0] == 1 ? SA1G_ACCESS_WRITE
             : er->ExceptionInformation[0] == 8 ? SA1G_ACCESS_EXEC : SA1G_ACCESS_READ;
    sa1g_in_handler = 1;
#if defined(_WIN64)
    sa1g_win64_walk(cx, &game_frame, &dc);
#endif
    handled = sa1g_hw_fault(w, addr, pc, access, game_frame, dc);
    sa1g_in_handler = 0;
    if (!handled) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    /* resume in a trampoline that longjmps to the SA1G_WORLD_TRY (register restore, no SEH unwinding) */
#if defined(_M_X64) || defined(__x86_64__)
    cx->Rsp = ((cx->Rsp - 512) & ~(DWORD64)15) - 8;
    cx->Rip = (DWORD64)(uintptr_t)sa1g_fault_trampoline;
#else
    cx->Esp = ((cx->Esp - 512) & ~(DWORD)15) - 4;
    cx->Eip = (DWORD)(uintptr_t)sa1g_fault_trampoline;
#endif
    return EXCEPTION_CONTINUE_EXECUTION;
}

#if !defined(_WIN64)
static uint32_t sa1g_caller_dc(void)
{
    return 0;
}
#endif

static int sa1g_handlers_install(void)
{
    sa1g_veh_handle = AddVectoredExceptionHandler(1, sa1g_veh);
    return sa1g_veh_handle != NULL ? 0 : -1;
}

static void sa1g_handlers_remove(void)
{
    if (sa1g_veh_handle != NULL) {
        RemoveVectoredExceptionHandler(sa1g_veh_handle);
        sa1g_veh_handle = NULL;
    }
}
#else
static void sa1g_sig_chain(int sig, siginfo_t *si, void *uc)
{
    struct sigaction *o = &sa1g_old_segv;
    if ((o->sa_flags & SA_SIGINFO) && o->sa_sigaction != NULL) {
        o->sa_sigaction(sig, si, uc);
        return;
    }
    if (!(o->sa_flags & SA_SIGINFO) && o->sa_handler != SIG_DFL && o->sa_handler != SIG_IGN) {
        o->sa_handler(sig);
        return;
    }
    /* default action: reinstall it and return; the instruction faults again and the process ends */
    {
        struct sigaction d;
        memset(&d, 0, sizeof d);
        d.sa_handler = SIG_DFL;
        sigemptyset(&d.sa_mask);
        sigaction(sig, &d, NULL);
    }
}

static void sa1g_sig_handler(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = (ucontext_t *)ucv;
    sa1g_world *w = sa1g_cur;
    uintptr_t addr = (uintptr_t)si->si_addr, pc = 0;
    int32_t access = SA1G_ACCESS_READ;
    int handled = 0;
#if defined(__x86_64__)
    pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    if (uc->uc_mcontext.gregs[REG_ERR] & 0x10) {
        access = SA1G_ACCESS_EXEC;
    } else if (uc->uc_mcontext.gregs[REG_ERR] & 0x2) {
        access = SA1G_ACCESS_WRITE;
    }
#elif defined(__i386__)
    pc = (uintptr_t)uc->uc_mcontext.gregs[REG_EIP];
    if (uc->uc_mcontext.gregs[REG_ERR] & 0x2) {
        access = SA1G_ACCESS_WRITE;
    }
#endif
    if (w != NULL && w->entered && sa1g_armed && !sa1g_in_handler && sa1g_on_world_thread()) {
        sa1g_in_handler = 1;
        handled = sa1g_hw_fault(w, addr, pc, access, 0, 0);
        sa1g_in_handler = 0;
    }
    if (handled) {
        /* return from the handler into the trampoline (the kernel restores the signal mask) */
#if defined(__x86_64__)
        uc->uc_mcontext.gregs[REG_RSP] =
            (greg_t)((((uintptr_t)uc->uc_mcontext.gregs[REG_RSP] - 512u) & ~(uintptr_t)15) - 8u);
        uc->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)sa1g_fault_trampoline;
        return;
#elif defined(__i386__)
        uc->uc_mcontext.gregs[REG_ESP] =
            (greg_t)((((uintptr_t)uc->uc_mcontext.gregs[REG_ESP] - 512u) & ~(uintptr_t)15) - 4u);
        uc->uc_mcontext.gregs[REG_EIP] = (greg_t)(uintptr_t)sa1g_fault_trampoline;
        return;
#endif
    }
    sa1g_sig_chain(sig, si, ucv);
}

static uint32_t sa1g_caller_dc(void)
{
    return 0;
}

static int sa1g_handlers_install(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = sa1g_sig_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, &sa1g_old_segv) != 0) {
        return -1;
    }
    sa1g_sig_installed = 1;
    return 0;
}

static void sa1g_handlers_remove(void)
{
    if (sa1g_sig_installed) {
        sigaction(SIGSEGV, &sa1g_old_segv, NULL);
        sa1g_sig_installed = 0;
    }
}
#endif

/* ---- process state: trap reservations and handlers (refcounted by the live worlds) -------------- */
static void sa1g_process_release(void)
{
    int s, k;
    sa1g_handlers_remove();
    for (s = 0; s < 32; s++) {
        uint8_t *t = sa1g_trap[s];
        if (t == NULL) {
            continue;
        }
        for (k = 0; k < 32; k++) {
            if (sa1g_trap[k] == t) {
                sa1g_trap[k] = NULL;
            }
        }
        sa1g_vm_release(t, SA1G_TRAP_RESERVE);
    }
    if (sa1g_negtrap) {
        sa1g_vm_release(sa1g_negtrap, SA1G_TRAP_RESERVE);
        sa1g_negtrap = NULL;
    }
    memset(sa1g_idle_seg, 0, sizeof sa1g_idle_seg);
    memset(sa1g_seg, 0, sizeof sa1g_seg);
    sa1g_ram_base = NULL;
}

static int sa1g_process_ref(void)
{
    int s, rc = 0;
    sa1g_proc_lock();
    if (sa1g_refs == 0) {
        uint8_t *shared = NULL;
        sa1g_negtrap = (uint8_t *)sa1g_vm_reserve(SA1G_TRAP_RESERVE);
        rc = sa1g_negtrap ? 0 : -1;
        for (s = 0; s < 32 && rc == 0; s++) {
            if (s == SA1G_SEG_NEGTRAP) {
                sa1g_idle_seg[s] = (uintptr_t)sa1g_negtrap;
                continue;
            }
            if (!SA1G_TRAP_PER_SEGMENT && shared != NULL) {
                sa1g_trap[s] = shared;
            } else {
                sa1g_trap[s] = (uint8_t *)sa1g_vm_reserve(SA1G_TRAP_RESERVE);
                shared = sa1g_trap[s];
                rc = shared ? 0 : -1;
            }
            sa1g_idle_seg[s] = (uintptr_t)sa1g_trap[s];
        }
        if (rc == 0) {
            rc = sa1g_handlers_install();
        }
        if (rc != 0) {
            sa1g_process_release();
        } else {
            memcpy(sa1g_seg, sa1g_idle_seg, sizeof sa1g_seg);
            sa1g_ram_base = (uint8_t *)sa1g_idle_seg[SA1G_SEG_ARENA];
        }
    }
    if (rc == 0) {
        sa1g_refs++;
    }
    sa1g_proc_unlock();
    return rc;
}

static void sa1g_process_unref(void)
{
    sa1g_proc_lock();
    if (sa1g_refs > 0 && --sa1g_refs == 0) {
        sa1g_process_release();
    }
    sa1g_proc_unlock();
}

/* ---- context-wide collision segment --------------------------------------------------------------- */
int sa1g_shared_create(sa1g_shared *s)
{
    memset(s, 0, sizeof *s);
    s->col_reserve_size = (size_t)SA1G_COL_SIZE + SA1G_GUARD_SIZE;
    s->col_reserve = (uint8_t *)sa1g_vm_reserve(s->col_reserve_size);
    if (s->col_reserve == NULL) {
        return -1;
    }
    s->col = s->col_reserve;
    return 0;
}

void sa1g_shared_destroy(sa1g_shared *s)
{
    if (s->col_reserve) {
        sa1g_vm_release(s->col_reserve, s->col_reserve_size);
    }
    memset(s, 0, sizeof *s);
}

/* bump allocation in a reserved region, committing 64 KB chunks */
static size_t sa1g_bump(uint8_t *base, size_t limit, size_t *used, size_t *committed, size_t n, size_t align)
{
    size_t start, end;
    if (base == NULL || align == 0 || (align & (align - 1)) != 0 || n > limit) {
        return (size_t)-1;
    }
    start = (*used + align - 1) & ~(align - 1);
    if (start > limit - n) {
        return (size_t)-1;
    }
    end = start + n;
    if (end > *committed) {
        size_t need = (end + SA1G_COMMIT_CHUNK - 1) & ~(size_t)(SA1G_COMMIT_CHUNK - 1);
        if (sa1g_vm_commit(base + *committed, need - *committed) != 0) {
            return (size_t)-1;
        }
        *committed = need;
    }
    *used = end;
    return start;
}

uint32_t sa1g_colseg_alloc(sa1g_shared *s, size_t n, size_t align, void **host)
{
    size_t off;
    if (s == NULL || s->col_sealed) {
        return 0;
    }
    off = sa1g_bump(s->col, SA1G_COL_SIZE, &s->col_used, &s->col_committed, n, align);
    if (off == (size_t)-1) {
        return 0;
    }
    if (host) {
        *host = s->col + off;
    }
    return SA1G_COL_GUEST + (uint32_t)off;
}

int sa1g_colseg_seal(sa1g_shared *s, int sealed)
{
    if (s == NULL || s->col == NULL) {
        return -1;
    }
    if (s->col_committed && sa1g_vm_protect(s->col, s->col_committed, !sealed) != 0) {
        return -1;
    }
    s->col_sealed = sealed != 0;
    return 0;
}

/* ---- worlds --------------------------------------------------------------------------------------- */
static void sa1g_world_fill_segments(sa1g_world *w)
{
    uint32_t k;
    memcpy(w->seg, sa1g_idle_seg, sizeof w->seg);
    w->seg[SA1G_SEG_ARENA] = (uintptr_t)w->ram;
    w->seg[SA1G_SEG_EXT] = (uintptr_t)w->ext;
    if (w->shared && w->shared->col) {
        for (k = 0; k < SA1G_SEG_COL_COUNT; k++) {
            w->seg[SA1G_SEG_COL0 + k] = (uintptr_t)w->shared->col + (uintptr_t)k * SA1G_SEG_SIZE;
        }
    }
    if (w->stack_top) {
        w->seg[SA1G_SEG_STACK] = w->stack_top - SA1G_STACK_WINDOW;
    }
}

int sa1g_world_create(sa1g_world *w, sa1g_shared *shared)
{
    memset(w, 0, sizeof *w);
    if (sa1g_process_ref() != 0) {
        return -1;
    }
    w->reserve_size = (size_t)SA1G_ARENA_SIZE + 2u * SA1G_GUARD_SIZE;
    w->reserve = (uint8_t *)sa1g_vm_reserve(w->reserve_size);
    w->ext_reserve_size = (size_t)SA1G_EXT_SIZE + SA1G_GUARD_SIZE;
    w->ext_reserve = (uint8_t *)sa1g_vm_reserve(w->ext_reserve_size);
    if (w->reserve == NULL || w->ext_reserve == NULL
        || sa1g_vm_commit(w->reserve + SA1G_GUARD_SIZE, SA1G_ARENA_SIZE) != 0) {
        if (w->reserve) {
            sa1g_vm_release(w->reserve, w->reserve_size);
        }
        if (w->ext_reserve) {
            sa1g_vm_release(w->ext_reserve, w->ext_reserve_size);
        }
        memset(w, 0, sizeof *w);
        sa1g_process_unref();
        return -1;
    }
    w->ram = w->reserve + SA1G_GUARD_SIZE;      /* fresh committed pages are zero */
    w->ext = w->ext_reserve;
    w->shared = shared;
    sa1g_world_fill_segments(w);
    w->step = sa1g_step_game;
    w->display_fn = sa1g_display_game;
    w->dead_status = SA1_OK;
    w->kind = -1;
    w->chr = -1;
    return 0;
}

void sa1g_world_destroy(sa1g_world *w)
{
    if (w->reserve == NULL) {
        return;
    }
    if (sa1g_cur == w) {
        sa1g_world_leave(w);
    }
    sa1g_vm_release(w->reserve, w->reserve_size);
    sa1g_vm_release(w->ext_reserve, w->ext_reserve_size);
    memset(w, 0, sizeof *w);
    sa1g_process_unref();
}

int sa1g_world_load(sa1g_world *w, uint32_t guest, const void *data, size_t n)
{
    uint32_t off = guest & 0xFFFFFFu;
    if (((guest >> 24) & 31u) != SA1G_SEG_ARENA || n > SA1G_ARENA_SIZE - off) {
        return -1;
    }
    memcpy(w->ram + off, data, n);
    return 0;
}

void *sa1g_world_ptr(const sa1g_world *w, uint32_t guest)
{
    return guest ? (void *)(w->seg[(guest >> 24) & 31u] + (guest & 0xFFFFFFu)) : NULL;
}

int sa1g_world_rd32(const sa1g_world *w, uint32_t guest, uint32_t *v)
{
    uint32_t off = guest & 0xFFFFFFu;
    if (((guest >> 24) & 31u) != SA1G_SEG_ARENA || off > SA1G_ARENA_SIZE - 4u) {
        *v = 0;
        return -1;
    }
    memcpy(v, w->ram + off, 4);
    return 0;
}

int sa1g_world_wr32(sa1g_world *w, uint32_t guest, uint32_t v)
{
    uint32_t off = guest & 0xFFFFFFu;
    if (((guest >> 24) & 31u) != SA1G_SEG_ARENA || off > SA1G_ARENA_SIZE - 4u) {
        return -1;
    }
    memcpy(w->ram + off, &v, 4);
    return 0;
}

uint32_t sa1g_world_ext_alloc(sa1g_world *w, size_t n, size_t align, void **host)
{
    size_t off = sa1g_bump(w->ext, SA1G_EXT_SIZE, &w->ext_used, &w->ext_committed, n, align);
    if (off == (size_t)-1) {
        return 0;
    }
    if (host) {
        *host = w->ext + off;
    }
    return SA1G_EXT_GUEST | (uint32_t)off;
}

/* reset the extension segment (apply_image): committed pages zeroed, allocations forgotten */
void sa1g_world_ext_reset(sa1g_world *w)
{
    if (w->ext_committed) {
        memset(w->ext, 0, w->ext_committed);
    }
    w->ext_used = 0;
}

void sa1g_world_clear_fault(sa1g_world *w)
{
    memset(&w->fault, 0, sizeof w->fault);
    w->dead = 0;
    w->dead_status = SA1_OK;
}

sa1g_world *sa1g_world_current(void)
{
    return sa1g_cur;
}

/* ---- entering a world ----------------------------------------------------------------------------- */
void sa1g_world_enter(sa1g_world *w)
{
    if (sa1g_cur == w && w->entered) {
        return;     /* re-entering after a fault: keep the host MXCSR saved at the first enter */
    }
    if (sa1g_cur != NULL) {
        fprintf(stderr, "sa1: world %p entered while world %p is active\n", (void *)w, (void *)sa1g_cur);
        abort();
    }
    memcpy(sa1g_seg, w->seg, sizeof sa1g_seg);
    sa1g_ram_base = w->ram;
    memcpy((void *)sa1g_fn_active, w->fn_images, sizeof sa1g_fn_active);
    sa1_rt_xmtrx = w->xmtrx;
    sa1_rt_pad = w->pad;                /* replay mode latches the record's raw input there */
#ifdef _WIN32
    sa1g_tid = GetCurrentThreadId();
#else
    sa1g_tid = pthread_self();
#endif
    w->saved_mxcsr = _mm_getcsr();
    w->entered = 1;
    sa1g_cur = w;
    _mm_setcsr(SA1G_MXCSR_GAME);
}

void sa1g_world_leave(sa1g_world *w)
{
    if (sa1g_cur != w || !w->entered) {
        return;
    }
    _mm_setcsr(w->saved_mxcsr);
    w->entered = 0;
    sa1g_armed = 0;
    sa1g_cur = NULL;
    sa1_rt_xmtrx = NULL;
    sa1_rt_pad = NULL;
    memset((void *)sa1g_fn_active, 0, sizeof sa1g_fn_active);
    memcpy(sa1g_seg, sa1g_idle_seg, sizeof sa1g_seg);
    sa1g_ram_base = (uint8_t *)sa1g_idle_seg[SA1G_SEG_ARENA];
}

/* host-stack window (2.1): [top - 16 MB, top) is guest 0x9D000000-0x9DFFFFFF while the world runs */
void sa1g_world_stack_open(sa1g_world *w, uintptr_t top)
{
    w->stack_top = top;
    w->seg[SA1G_SEG_STACK] = top - SA1G_STACK_WINDOW;
    if (sa1g_cur == w) {
        sa1g_seg[SA1G_SEG_STACK] = w->seg[SA1G_SEG_STACK];
    }
}

void sa1g_world_stack_close(sa1g_world *w)
{
    w->stack_top = 0;
    w->seg[SA1G_SEG_STACK] = sa1g_idle_seg[SA1G_SEG_STACK];
    if (sa1g_cur == w) {
        sa1g_seg[SA1G_SEG_STACK] = w->seg[SA1G_SEG_STACK];
    }
}

/* SA1G_WORLD_TRY arms the recovery point (used by the macro through this hook) */
int sa1g_world_arm(sa1g_world *w)
{
    sa1g_armed = (sa1g_cur == w);
    return sa1g_armed;
}

void sa1g_world_disarm(void)
{
    sa1g_armed = 0;
}

sa1_status sa1g_world_call(sa1g_world *w, sa1g_world_fn fn, void *user)
{
    volatile sa1_status st = SA1_OK;
    if (w == NULL || fn == NULL || w->reserve == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    sa1g_tick_lock();
    sa1g_world_enter(w);
    sa1g_world_stack_open(w, (uintptr_t)__builtin_frame_address(0));
    if (SA1G_WORLD_TRY(w)) {
        fn(w, user);
        sa1g_world_disarm();
    } else {
        st = w->dead_status;
    }
    sa1g_world_stack_close(w);
    sa1g_world_leave(w);
    sa1g_tick_unlock();
    return st;
}
