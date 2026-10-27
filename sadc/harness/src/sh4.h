/*
 * sh4.h - SH-4 interpreter harness for the Sonic Adventure (DC) decompilation.
 *
 * Runs original SH-4 code from the user's own 1ST_READ.BIN (and overlays) in
 * guest memory that is mapped at the Dreamcast addresses, so that natively
 * compiled decompiled C (32-bit x86, same ILP32 little-endian layouts) and the
 * interpreted original share one memory image.
 *
 * Main pieces:
 *   memory   sh4_mem_init / sh4_load_*      (sh4_mem.c)
 *   cpu      sh4_step / sh4_run             (sh4_cpu.c)
 *   calls    sh4_call / sh4_invoke / hooks  (sh4_call.c)
 *
 * The harness is single-threaded; there is one CPU instance, SH4.
 */
#ifndef SH4_H
#define SH4_H

#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* memory map                                                          */

#define SH4_RAM_SIZE     0x01000000u   /* 16 MB main RAM (area 3)            */
#define SH4_RAM_P1       0x8C000000u   /* cached view, used by the game       */
#define SH4_RAM_P0       0x0C000000u   /* physical / P0 view (overlays)       */
#define SH4_RAM_P2       0xAC000000u   /* uncached view                       */
#define SH4_VRAM_SIZE    0x00800000u
#define SH4_ARAM_SIZE    0x00200000u
#define SH4_IMAGE_BASE   0x8C010000u   /* 1ST_READ.BIN load address           */

#define SH4_SENTINEL     0xDEADBEE0u   /* PR value meaning "return to host"   */
#define SH4_DEFAULT_STACK 0x8C00F400u  /* the game's own stack top (8C00C000-8C00F400) */

/* SR bits */
#define SR_T     0x00000001u
#define SR_S     0x00000002u
#define SR_IMASK 0x000000F0u
#define SR_Q     0x00000100u
#define SR_M     0x00000200u
#define SR_FD    0x00008000u
#define SR_BL    0x10000000u
#define SR_RB    0x20000000u
#define SR_MD    0x40000000u
#define SR_MASK  0x700083F3u

/* FPSCR bits */
#define FPSCR_RM    0x00000003u
#define FPSCR_FLAG  0x0000007Cu
#define FPSCR_EN    0x00000F80u
#define FPSCR_CAUSE 0x0003F000u
#define FPSCR_DN    0x00040000u
#define FPSCR_PR    0x00080000u
#define FPSCR_SZ    0x00100000u
#define FPSCR_FR    0x00200000u
#define FPSCR_MASK  0x003FFFFFu
#define SH4_DEFAULT_FPSCR 0x00040001u  /* DN=1, RM=round to zero (Katana default) */

/* ------------------------------------------------------------------ */
/* CPU state                                                           */

enum sh4_fault_kind {
    SH4_OK = 0,
    SH4_FAULT_ILLEGAL,     /* undefined instruction                         */
    SH4_FAULT_SLOT,        /* instruction not allowed in a delay slot       */
    SH4_FAULT_MEM,         /* access to an unmapped address                 */
    SH4_FAULT_ALIGN,       /* misaligned access                             */
    SH4_FAULT_BRANCH,      /* branch to a non-executable address            */
    SH4_FAULT_TIMEOUT,     /* instruction budget exhausted                  */
    SH4_FAULT_TRAP,        /* TRAPA without a trap handler                  */
    SH4_FAULT_SLEEP,       /* SLEEP without a sleep handler                 */
    SH4_FAULT_UNDEF_FPU,   /* FPU op undefined in the current PR/SZ mode    */
    SH4_FAULT_NATIVE,      /* access violation in native (x86) code         */
    SH4_FAULT_HOOK,        /* raised by an HLE hook                         */
    SH4_FAULT_COUNT
};

typedef struct sh4_fault_info {
    int kind;
    uint32_t pc;          /* guest PC of the faulting instruction (0 for native) */
    uint32_t addr;        /* data address / branch target / host EIP              */
    uint16_t op;
    char msg[200];
} sh4_fault_info;

typedef struct sh4_cpu {
    uint32_t r[16];
    uint32_t rbank[8];    /* the inactive bank of R0-R7 (SR.RB)               */
    uint32_t sr, gbr, vbr, ssr, spc, sgr, dbr;
    uint32_t mach, macl, pr, pc;
    uint32_t fpscr, fpul;
    union { float f[16]; uint32_t u[16]; } fr, xf;   /* FR bank (FPSCR.FR selects) and XF bank */

    /* bookkeeping */
    uint64_t icount;      /* instructions executed                            */
    uint64_t ilimit;      /* SH4_FAULT_TIMEOUT when icount reaches it (0=off) */
    int depth;            /* sh4_invoke nesting depth                         */
    int stop;             /* set when the current run reaches SH4_SENTINEL    */
    int in_slot;          /* executing a delay slot                           */
    int trace;            /* print every executed instruction to stderr       */
    uint32_t stack_top;   /* R15 for depth-0 calls                            */
    uint32_t cur_pc;      /* PC of the instruction being executed             */
    uint16_t cur_op;
    sh4_fault_info fault;      /* last fault                                       */
} sh4_cpu;

extern sh4_cpu SH4;

/* SHC-ABI argument (for sh4_invoke): 'i' integer/pointer, 'f' float */
typedef struct sh4_arg { char kind; uint32_t v; } sh4_arg;

static inline sh4_arg sh4_ai(uint32_t v) { sh4_arg a; a.kind = 'i'; a.v = v; return a; }
static inline sh4_arg sh4_af(float f) { sh4_arg a; union { float f; uint32_t u; } x; x.f = f; a.kind = 'f'; a.v = x.u; return a; }

/* ------------------------------------------------------------------ */
/* memory (sh4_mem.c)                                                  */

/* page table: guest address >> 20 -> host pointer of that 1 MB page, or NULL */
extern uint8_t *sh4_page[4096];

int  sh4_mem_init(void);                    /* map RAM (aliased views), VRAM, ARAM */
void sh4_mem_clear(void);                   /* zero main RAM */
uint8_t *sh4_ram(void);                     /* host pointer to main RAM offset 0  */
long sh4_load_file(const char *path, uint32_t addr);  /* returns size or -1 */
extern int sh4_host_passthrough;           /* 0 off, 1 any host memory, 2 (default) host stack + image */
extern int sh4_null_page_faults;           /* guest accesses to 0-0xFFFF fault (else read 0) */
extern unsigned sh4_null_touched;          /* count of guest accesses to 0-0xFFFF       */
extern int sh4_strict_io;                   /* only real register windows are hardware   */

uint8_t  sh4_read8(uint32_t a);
uint16_t sh4_read16(uint32_t a);
uint32_t sh4_read32(uint32_t a);
float    sh4_readf(uint32_t a);
void     sh4_write8(uint32_t a, uint8_t v);
void     sh4_write16(uint32_t a, uint16_t v);
void     sh4_write32(uint32_t a, uint32_t v);
void     sh4_writef(uint32_t a, float v);
void    *sh4_host_ptr(uint32_t a);          /* host pointer for plain memory, else NULL */
int      sh4_is_ram(uint32_t a);            /* inside main RAM (any mirror)            */
int      sh4_host_readable(uint32_t a, int size);  /* host pass-through memory (32-bit host)  */
uint32_t sh4_ram_offset(uint32_t a);        /* offset within main RAM                  */

/* slow paths used by the CPU (unmapped page / misaligned / hardware) */
uint32_t sh4_mem_slow_read(sh4_cpu *c, uint32_t a, int size);
void     sh4_mem_slow_write(sh4_cpu *c, uint32_t a, uint32_t v, int size);
void     sh4_sq_pref(sh4_cpu *c, uint32_t a);   /* PREF on the store-queue area */

/* hardware access callbacks (Holly/AICA/P4 registers, TA FIFO, BIOS area) */
typedef uint32_t (*sh4_io_read_fn)(uint32_t addr, int size, void *user);
typedef void     (*sh4_io_write_fn)(uint32_t addr, uint32_t val, int size, void *user);
void sh4_set_io(sh4_io_read_fn readfn, sh4_io_write_fn writefn, void *user);
void sh4_hw_reset(void);                    /* clear store queues, QACR, register latches */
uint32_t sh4_io_default_read(uint32_t addr, int size, void *user);
void     sh4_io_default_write(uint32_t addr, uint32_t val, int size, void *user);
extern int sh4_io_verbose;                  /* print io accesses (first N per address) */

/* ------------------------------------------------------------------ */
/* event log (hardware accesses and hook calls), used by diff tests     */

enum { SH4_EV_IO_READ = 1, SH4_EV_IO_WRITE, SH4_EV_HOOK, SH4_EV_SQ };
typedef struct sh4_event {
    uint8_t type, size;
    uint32_t addr;        /* io address / hook address / SQ target  */
    uint32_t val;         /* io value / hook R4                      */
    uint32_t a[3];        /* hook R5-R7                              */
    uint32_t f[4];        /* hook FR4-FR7 bits                       */
    uint32_t pc;          /* guest PC of the access                  */
} sh4_event;
void sh4_evlog_start(sh4_event *buf, size_t cap);
size_t sh4_evlog_stop(void);                /* returns number of events recorded */
void sh4_evlog_add(const sh4_event *e);

/* ------------------------------------------------------------------ */
/* CPU (sh4_cpu.c)                                                     */

void sh4_reset(sh4_cpu *c);
void sh4_step(sh4_cpu *c);                  /* execute one instruction        */
void sh4_run(sh4_cpu *c);                   /* run until c->stop              */
void sh4_set_fpscr(sh4_cpu *c, uint32_t v); /* bank swap + host FP mode sync  */
void sh4_set_sr(sh4_cpu *c, uint32_t v);    /* register bank swap             */
void sh4_fpu_sync_host(sh4_cpu *c);         /* MXCSR from FPSCR.RM/DN         */
const char *sh4_op_name(uint16_t op);       /* normalised instruction form    */
int  sh4_op_valid(uint16_t op);
void sh4_fault(sh4_cpu *c, int kind, uint32_t addr, const char *fmt, ...);
const char *sh4_fault_name(int kind);

/* host FP environment: the harness runs host SSE math in the game's mode
 * (round to zero, flush-to-zero, denormals-are-zero). */
void sh4_host_fp_game_mode(void);
uint32_t sh4_host_mxcsr_get(void);
void sh4_host_mxcsr_set(uint32_t v);

/* SH-4 FPU semantics shared by the interpreter and native intrinsics */
float sh4_fsca_sin(uint32_t angle);
float sh4_fsca_cos(uint32_t angle);
float sh4_fsrra(float x);
float sh4_fipr(const float *a, const float *b);
float sh4_fmac(float a, float b, float c);  /* a*b+c, fused */
uint32_t sh4_ftrc(float f);
float sh4_fsqrt(float x);                  /* FSQRT (single), SH-4 NaN */
void sh4_ftrv(const float *xmtrx, const float *v, float *out);   /* out = XMTRX * v */

/* ------------------------------------------------------------------ */
/* calls, hooks, guards (sh4_call.c)                                   */

/* Call a guest function per the SHC ABI: args[0..3] -> R4-R7, fargs[0..7] ->
 * FR4-FR11 (either may be NULL).  Returns R0; FR0 is in SH4.fr.f[0].
 * On a fault the call is abandoned and SH4.fault is set (returns 0). */
uint32_t sh4_call(uint32_t addr, const uint32_t args[4], const float fargs[8]);
float    sh4_call_f(uint32_t addr, const uint32_t args[4], const float fargs[8]);
/* General form: arguments in declaration order; ints beyond R7 and floats
 * beyond FR11 go to the stack in declaration order (SHC convention).
 * Returns SH4_OK or the fault kind. */
int sh4_invoke(uint32_t addr, const sh4_arg *args, int nargs, uint32_t *r0, float *fr0);
/* Variadic SHC convention: every argument on the stack. */
int sh4_invoke_varargs(uint32_t addr, const uint32_t *words, int nwords, uint32_t *r0);
/* Run with the register state as set by the caller (non-ABI helpers such as
 * __divls: R1/R0).  PR and (at depth 0) R15 are set by the call. */
int sh4_call_regs(uint32_t addr);

/* Fault guards.  SH4_TRY establishes a recovery point that catches guest
 * faults at any nesting depth and access violations in native code. */
typedef struct sh4_guard {
    jmp_buf jb;
    struct sh4_guard *prev;
    int depth;
    uint32_t r15, pr, pc;
} sh4_guard;
void sh4_guard_push(sh4_guard *g);
void sh4_guard_pop(sh4_guard *g);
void *sh4_guard_top_get(void);              /* innermost active guard (debugging) */
#define SH4_TRY(g) (sh4_guard_push(&(g)), setjmp((g).jb) == 0)
/* after SH4_TRY returned false: call sh4_guard_pop(&g) and inspect SH4.fault */

/* HLE hooks: when guest control reaches `addr` (call, jump or tail call),
 * fn(cpu,user) runs instead and execution continues at PR. */
typedef void (*sh4_hook_fn)(sh4_cpu *c, void *user);
int  sh4_hook_add(uint32_t addr, sh4_hook_fn fn, void *user, const char *name);
void sh4_hook_remove(uint32_t addr);
void sh4_hook_clear(void);
int  sh4_hook_find(uint32_t addr, sh4_hook_fn *fn, void **user, const char **name);
extern int sh4_hook_log;                    /* record hook calls in the event log */
/* Special hooks at synthetic code addresses outside main RAM (round 5: libsa1's LIBSA1_HOST image, 0x7F000100 = the
 * host-object proxies' exec, ho_oracle.c): an interpreted branch to the address runs fn like a RAM hook; native code that
 * calls the address through a guest function pointer (32-bit Windows: a DEP fault on it) jumps to `native`, a cdecl
 * function with the call's own prototype.  At most 8; addr is compared as addr & 0x1FFFFFFF. */
int  sh4_special_hook_add(uint32_t addr, sh4_hook_fn fn, void *user, const void *native, const char *name);
/* Maps size bytes (1 MB multiples) of host memory at the host address == guest address `guest` (32-bit hosts) and into
 * the interpreter's page table, so interpreted and native code share it: e.g. libsa1's extension segment 0x98000000
 * (the host-object oracle's slot block). Returns the host pointer, NULL when the address range is not free. */
uint8_t *sh4_map_extra(uint32_t guest, uint32_t size);
void sh4_hle_ret0(sh4_cpu *c, void *u);     /* R0 = 0, FR0 = 0                 */
void sh4_hle_ret1(sh4_cpu *c, void *u);     /* R0 = 1                          */
void sh4_hle_retu(sh4_cpu *c, void *u);     /* R0 = (uint32_t)(uintptr_t)u     */
uint32_t sh4_stack_arg(sh4_cpu *c, int i);  /* i-th stack argument at hook entry */

/* optional shadow call stack (debugging: who called the faulting code).
 * BSR/BSRF/JSR push, RTS pops to the matching return address. */
typedef struct sh4_frame { uint32_t target, ret; } sh4_frame;
#define SH4_CSTACK_MAX 256
extern sh4_frame sh4_cstack[SH4_CSTACK_MAX];
extern int sh4_cstack_depth;
extern int sh4_cstack_on;
extern uint8_t *sh4_cov;        /* coverage: bit (pc & 0xFFFFFF) >> 1 set when pc executes; 1 MB, NULL = off */
void sh4_cstack_push(uint32_t target, uint32_t ret);
void sh4_cstack_ret(uint32_t to);

/* trap / sleep handlers (TRAPA #imm, SLEEP) */
extern void (*sh4_trap_handler)(sh4_cpu *c, uint32_t imm);
extern void (*sh4_sleep_handler)(sh4_cpu *c);

/* ------------------------------------------------------------------ */
/* native code registry (generated by tools/gen_native.py)             */

typedef struct sh4_native_fn {
    uint32_t guest;                  /* original address (0 if unknown)   */
    void *host;                      /* native function                   */
    void (*adapter)(sh4_cpu *c);     /* guest regs -> native call -> R0/FR0 */
    const char *name;
    const char *sig;                 /* "ret:arg,arg" (v i8 u8 i16 u16 i32 u32 p f32 f64 s ...) */
    const char *file;                /* decompiled source file */
} sh4_native_fn;

/* game data referenced by the native code (for input randomisation) */
typedef struct sh4_native_data {
    const char *name;
    uint32_t addr;
    uint32_t gap;                    /* distance to the next symbol (capped) */
    const char *file;
} sh4_native_data;
extern const sh4_native_data sh4_native_datas[];
extern const int sh4_native_data_count;

typedef struct sh4_thunk_info {
    uint32_t guest;                  /* original address called by the thunk */
    void *host;                      /* the thunk (native -> interpreter)    */
    const char *name;
    const char *sig;
} sh4_thunk_info;

extern const sh4_native_fn sh4_native_fns[];
extern const int sh4_native_fn_count;
extern const sh4_thunk_info sh4_thunks[];
extern const int sh4_thunk_count;

/* route guest calls of functions that have a native implementation into
 * the native code (scaffolding mode).  Off by default (pure original). */
void sh4_route_native(int on);
int  sh4_route_native_enabled(void);
const sh4_native_fn *sh4_native_by_guest(uint32_t guest);
const sh4_native_fn *sh4_native_by_host(const void *host);
const sh4_thunk_info *sh4_thunk_by_host(const void *host);
const sh4_thunk_info *sh4_thunk_by_guest(uint32_t guest);
uint32_t sh4_canon_code_ptr(uint32_t v);    /* native fn/thunk address -> guest address (else v) */

/* signature helpers: parse "ret:arg,arg", extend a value per type */
int sh4_sig_parse(const char *sig, char ret[8], char params[24][8]);
uint32_t sh4_extend(const char *t, uint32_t v);

/* host exception handling (Windows x86: native calls through guest
 * function pointers, access violations inside guards) */
void sh4_native_init(void);

/* symbol lookup (generated table of build/1ST_READ.symbols.txt names used
 * by the harness; plus a loader for the full file) */
int  sh4_symbols_load(const char *path);
uint32_t sh4_sym_addr(const char *name);     /* 0 if unknown */
const char *sh4_sym_name(uint32_t addr);     /* exact match or NULL */
const char *sh4_sym_near(uint32_t addr, uint32_t *off);   /* closest symbol at or below addr */

#ifdef __cplusplus
}
#endif
#endif