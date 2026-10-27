/*
 * spike_test.c - GUEST-ARENA world tests (sadc docs/libsa1_integration.md sections 2, 6, 11).
 *
 * One call sequence (k_steps), two builds of this file:
 *
 *   -DSPIKE_REF  32-bit, linked with the objects of a private sa1h build (LIB/tests/build_spike.py):
 *                runs the ORIGINAL functions of the user's baserom/1ST_READ.BIN in the SH-4
 *                interpreter on fresh RAM and writes the 16 MB guest RAM after the exit sequence
 *                (<prefix>_exit.bin) and after the extended sequence (<prefix>_ext.bin), plus the
 *                return values (<prefix>.res); with -DSPIKE_GA32 also the same sequence through the
 *                ga32 lowered code.
 *   default      the libsa1 world tests (LIB/build.py builds them for x86_64-windows, x86-windows and
 *                x86_64-linux, linked with libsa1.a):
 *                - world image: composed from the user's disc when SA1_TEST_GDI=<game.gdi> is set
 *                  (Sonic: 1ST_READ + S_MOT + S_SBMOT + B_CHAOS7, the action placeholder patches;
 *                  every other kind's image too), else 1ST_READ.BIN from the command line;
 *                - with lowered game code linked: heap init 8C098A10, task init 8C09844A,
 *                  CreateElementalTask(7, 1, 8C0A77BC) and the extended sequence through the code
 *                  map in the world; the arena must equal the interpreter reference byte for byte
 *                  (the interpreter's guest stack 8C00C000-8C00F400, overlay spans and patched words
 *                  excepted, those are checked against the image);
 *                - two worlds in one process running the sequence interleaved;
 *                - guest-address machinery and faults: NULL page, unmapped segments, segment 0x13,
 *                  guards, extension and collision segments, host-stack window, encode, code map,
 *                  fatal stubs, sa1g_fatal, dead worlds;
 *                - the tick contract: pairing (B_8C754EC4 = 2, B_8C754EC8 = 0/1), presses ORed,
 *                  accumulator, events, MXCSR 0xFFC0 inside and the host value restored after
 *                  100,000 pairs and after faults.
 *
 *   spike_ref  <1ST_READ.BIN> <out prefix>
 *   spike_test <1ST_READ.BIN | -> <ref prefix | ->        (SA1_TEST_GDI=<game.gdi> optional)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_SIZE 0x01000000u
#define ROM_SIZE 6735488u
#define STACK_LO 0x00C000u          /* interpreter guest stack (SH4_DEFAULT_STACK 8C00F400 down) */
#define STACK_HI 0x00F400u

typedef struct spike_step {
    const char *what;
    uint32_t addr;                  /* DC address of the function */
    int nargs;
    uint32_t a[3];
    unsigned res_mask;              /* bit k: a[k] is the index of an earlier step (its result) */
    int ret;                        /* the function returns a value */
    int dump;                       /* 1: exit-test snapshot, 2: extended-sequence snapshot */
} spike_step;

static const spike_step k_steps[] = {
    /* ---- the phase 1 exit test */
    { "heap init 8C098A10", 0x8C098A10u, 0, {0, 0, 0}, 0, 0, 0 },
    { "task init 8C09844A", 0x8C09844Au, 0, {0, 0, 0}, 0, 0, 0 },
    { "CreateElementalTask(7, 1, 8C0A77BC)", 0x8C09852Eu, 3, {7, 1, 0x8C0A77BCu}, 0, 1, 1 },
    /* ---- extended sequence (beyond the exit test) */
    { "CreateElementalTask(15, 3, NULL)", 0x8C09852Eu, 3, {15, 3, 0}, 0, 1, 0 },             /* 3 */
    { "CreateChildTask(2, 8C0A77BC, #2)", 0x8C098CA0u, 3, {2, 0x8C0A77BCu, 2}, 4, 1, 0 },     /* 4 */
    { "CreateChildTask(3, NULL, #2)", 0x8C098CA0u, 3, {3, 0, 2}, 4, 1, 0 },                   /* 5 */
    { "MAlloc(0x100)", 0x8C098A80u, 1, {0x100, 0, 0}, 0, 1, 0 },                              /* 6 */
    { "CAlloc(3, 0x21)", 0x8C098B0Cu, 2, {3, 0x21, 0}, 0, 1, 0 },                             /* 7 */
    { "MAlloc(0x40)", 0x8C098A80u, 1, {0x40, 0, 0}, 0, 1, 0 },                                /* 8 */
    { "MFree(#6)", 0x8C098B42u, 1, {6, 0, 0}, 1, 0, 0 },                                      /* 9 */
    { "MFree(#8)", 0x8C098B42u, 1, {8, 0, 0}, 1, 0, 0 },                                      /* 10 */
    { "DestroyTask(#4)", 0x8C09865Cu, 1, {4, 0, 0}, 1, 0, 0 },                                /* 11 */
    { "MFree(#7)", 0x8C098B42u, 1, {7, 0, 0}, 1, 0, 0 },                                      /* 12 */
    { "DestroyTask(#3)", 0x8C09865Cu, 1, {3, 0, 0}, 1, 0, 0 },                                /* 13 */
    { "FreeTask(#2)", 0x8C098786u, 1, {2, 0, 0}, 1, 0, 0 },                                   /* 14 */
    { "func_8C098C2C(8C000F00, 8C000F04)", 0x8C098C2Cu, 2, {0x8C000F00u, 0x8C000F04u, 0}, 0, 0, 0 },
    { "DestroyTask(#2)", 0x8C09865Cu, 1, {2, 0, 0}, 1, 0, 0 },                                /* 16 */
    { "MAlloc(0x90000)", 0x8C098A80u, 1, {0x90000, 0, 0}, 0, 1, 0 },                          /* 17 */
    { "MAlloc(0)", 0x8C098A80u, 1, {0, 0, 0}, 0, 1, 0 },                                      /* 18 */
    { "CreateElementalTask(2, 8, 8C0A77BC)", 0x8C09852Eu, 3, {2, 8, 0x8C0A77BCu}, 0, 1, 0 },  /* 19 */
    { "func_8C098444()", 0x8C098444u, 0, {0, 0, 0}, 0, 1, 0 },                                /* 20 */
    { "func_8C098C2C(8C000F08, 8C000F0C)", 0x8C098C2Cu, 2, {0x8C000F08u, 0x8C000F0Cu, 0}, 0, 0, 2 },
};
#define NSTEPS ((int)(sizeof k_steps / sizeof k_steps[0]))

static uint32_t g_res[NSTEPS];

static void step_args(int i, uint32_t *a)
{
    int k;
    for (k = 0; k < 3; k++) {
        a[k] = (k_steps[i].res_mask & (1u << k)) ? g_res[k_steps[i].a[k]] : k_steps[i].a[k];
    }
}

static unsigned char *read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long sz;
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (unsigned char *)malloc(sz > 0 ? (size_t)sz : 1u);
    if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *n = (size_t)sz;
    return b;
}

#ifdef SPIKE_REF
static int write_file(const char *path, const void *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    if (fwrite(p, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    return fclose(f);
}
#endif

#if !defined(SPIKE_REF) || defined(SPIKE_GA32)
/* ---- shared by the world tests (profile ga64) and the ga32 pass of the reference ---------------- */
#include "sa1/guest.h"

/* each step through the generated code map: DC address -> native lowered function */
static uint32_t call_lowered(int i, const uint32_t *a)
{
    void *f = sa1_fn(SA1_GFN_C(k_steps[i].addr), 0u);
    switch (k_steps[i].addr) {
    case 0x8C098A10u:
    case 0x8C09844Au:
        ((void (*)(void))f)();
        return 0;
    case 0x8C09852Eu:
        return sa1_encode(((void *(*)(uint16_t, int32_t, sa1_gfn))f)((uint16_t)a[0], (int32_t)a[1], SA1_GFN_C(a[2])));
    case 0x8C098CA0u:
        return sa1_encode(((void *(*)(int32_t, sa1_gfn, void *))f)((int32_t)a[0], SA1_GFN_C(a[1]), sa1_decode(a[2])));
    case 0x8C09865Cu:
    case 0x8C098786u:
    case 0x8C098B42u:
        ((void (*)(void *))f)(sa1_decode(a[0]));
        return 0;
    case 0x8C098A80u:
        return sa1_encode(((void *(*)(int32_t))f)((int32_t)a[0]));
    case 0x8C098B0Cu:
        return sa1_encode(((void *(*)(int32_t, int32_t))f)((int32_t)a[0], (int32_t)a[1]));
    case 0x8C098C2Cu:
        ((void (*)(uint32_t *, uint32_t *))f)((uint32_t *)sa1_decode(a[0]), (uint32_t *)sa1_decode(a[1]));
        return 0;
    case 0x8C098444u:
        return ((sa1_gfn (*)(void))f)().v;
    }
    printf("no dispatch for %08X\n", k_steps[i].addr);
    exit(2);
}

static uint32_t rd32(const unsigned char *ram, uint32_t g)
{
    uint32_t v;
    memcpy(&v, ram + (g & 0xFFFFFFu), 4);
    return v;
}

static int region_eq(const unsigned char *a, const unsigned char *b, uint32_t g, uint32_t n)
{
    return memcmp(a + (g & 0xFFFFFFu), b + (g & 0xFFFFFFu), n) == 0;
}
#endif

#if defined(SPIKE_REF) && defined(SPIKE_GA32)
/* whole-arena comparison (stack window excepted); returns the number of differing bytes */
static size_t compare_ram(const char *what, const unsigned char *mine, const unsigned char *ref)
{
    size_t off, ndiff = 0;
    int shown = 0;
    for (off = 0; off < RAM_SIZE; off++) {
        if (off >= STACK_LO && off < STACK_HI) {
            continue;
        }
        if (mine[off] != ref[off]) {
            if (shown < 12) {
                uint32_t w = (uint32_t)off & ~3u;
                printf("    %s: %08X lowered %08X reference %08X\n", what, 0x8C000000u | w, rd32(mine, w), rd32(ref, w));
                shown++;
            }
            ndiff++;
        }
    }
    return ndiff;
}
#endif

#ifdef SPIKE_REF
/* =============================== reference: the original code in the interpreter ============== */
#include "sh4.h"

#ifdef SPIKE_GA32
/* world-fault entry points of the ga32 profile (the harness would route them to its own fault path) */
void sa1g_fault_encode(const void *host)
{
    fprintf(stderr, "ga32: encode fault %p\n", host);
    exit(1);
}

void sa1g_fault_fn(uint32_t g, uint32_t sig)
{
    fprintf(stderr, "ga32: code-map fault %08X sig %08X\n", (unsigned)g, (unsigned)sig);
    exit(1);
}

void sa1g_unimpl(uint32_t g, const char *name)
{
    fprintf(stderr, "ga32: unimplemented %s %08X\n", name, (unsigned)g);
    exit(1);
}

/* the same sequence through the lowered C (profile ga32) on the interpreter's RAM at 0x8C000000 */
static int ga32_pass(const char *rom, const char *prefix, int mem_at_dc)
{
    static uint32_t ref_res[NSTEPS];
    char path[1024];
    unsigned char *ref;
    size_t n;
    int i, fails = 0;
    if (mem_at_dc <= 0) {
        printf("ga32: guest RAM is not mapped at its DC addresses (LARGE_ADDRESS_AWARE?): pass skipped\n");
        return 1;
    }
    memcpy(ref_res, g_res, sizeof ref_res);
    sh4_mem_clear();
    if (sh4_load_file(rom, SH4_IMAGE_BASE) != (long)ROM_SIZE) {
        return 1;
    }
    sa1g_pcd_init();                /* ga32: R4 pcdata once, before any lowered code runs */
    for (i = 0; i < NSTEPS; i++) {
        uint32_t a[3], r;
        step_args(i, a);
        r = call_lowered(i, a);
        g_res[i] = k_steps[i].ret ? r : 0u;
        if (g_res[i] != ref_res[i]) {
            printf("ga32: step %d %s: lowered %08X interpreter %08X\n", i, k_steps[i].what, (unsigned)g_res[i],
                   (unsigned)ref_res[i]);
            fails++;
        }
        if (k_steps[i].dump) {
            size_t d;
            snprintf(path, sizeof path, "%s_%s.bin", prefix, k_steps[i].dump == 1 ? "exit" : "ext");
            ref = read_file(path, &n);
            if (!ref || n != RAM_SIZE) {
                return 1;
            }
            d = compare_ram("ga32", sh4_ram(), ref);
            printf("ga32: %s snapshot: %zu bytes differ from the interpreter run\n",
                   k_steps[i].dump == 1 ? "exit-test" : "extended", d);
            fails += d != 0;
            free(ref);
        }
    }
    printf("ga32: lowered text (profile ga32, native on the harness RAM) %s the interpreter on %d steps\n",
           fails ? "DIFFERS FROM" : "matches", NSTEPS);
    return fails ? 1 : 0;
}
#endif

int main(int argc, char **argv)
{
    char path[1024];
    FILE *rf;
    long n;
    int i, k, mem_at_dc;
    if (argc < 3) {
        fprintf(stderr, "usage: spike_ref <1ST_READ.BIN> <out prefix>\n");
        return 2;
    }
    mem_at_dc = sh4_mem_init();
    if (mem_at_dc < 0) {
        fprintf(stderr, "spike_ref: guest memory init failed\n");
        return 1;
    }
    sh4_reset(&SH4);
    sh4_host_fp_game_mode();
    sh4_native_init();
    n = sh4_load_file(argv[1], SH4_IMAGE_BASE);
    if (n != (long)ROM_SIZE) {
        fprintf(stderr, "spike_ref: %s: %ld bytes (expected %u)\n", argv[1], n, ROM_SIZE);
        return 1;
    }
    for (i = 0; i < NSTEPS; i++) {
        uint32_t a[3], r0 = 0;
        float f0 = 0.0f;
        sh4_arg A[3];
        step_args(i, a);
        for (k = 0; k < k_steps[i].nargs; k++) {
            A[k] = sh4_ai(a[k]);
        }
        k = sh4_invoke(k_steps[i].addr, A, k_steps[i].nargs, &r0, &f0);
        if (k != SH4_OK) {
            printf("step %2d %-40s FAULT %s at pc=%08X addr=%08X: %s\n", i, k_steps[i].what,
                   sh4_fault_name(SH4.fault.kind), SH4.fault.pc, SH4.fault.addr, SH4.fault.msg);
            return 1;
        }
        g_res[i] = k_steps[i].ret ? r0 : 0;
        printf("step %2d %-40s -> %08X\n", i, k_steps[i].what, g_res[i]);
        if (k_steps[i].dump) {
            snprintf(path, sizeof path, "%s_%s.bin", argv[2], k_steps[i].dump == 1 ? "exit" : "ext");
            if (write_file(path, sh4_ram(), RAM_SIZE) != 0) {
                fprintf(stderr, "spike_ref: cannot write %s\n", path);
                return 1;
            }
        }
    }
    snprintf(path, sizeof path, "%s.res", argv[2]);
    rf = fopen(path, "w");
    if (!rf) {
        return 1;
    }
    for (i = 0; i < NSTEPS; i++) {
        fprintf(rf, "%08X\n", g_res[i]);
    }
    fclose(rf);
    printf("spike_ref: %d steps, %llu instructions\n", NSTEPS, (unsigned long long)SH4.icount);
#ifdef SPIKE_GA32
    return ga32_pass(argv[1], argv[2], mem_at_dc);
#else
    (void)mem_at_dc;
    return 0;
#endif
}
#else
/* =============================== the libsa1 world tests ========================================== */
#include "libsa1.h"
#include "sa1_world.h"
#include "sa1g_text.h"
#include <xmmintrin.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* the page-guard allocator (tests/guard_alloc.h): SA1_GUARD_ALLOC=1 puts libsa1's heap blocks against no-access pages */
#include "guard_alloc.c"
#define sa1_host_defaults(h) (sa1_host_defaults(h), ga_host(h))

static int g_fail, g_skip;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok    ");                                                \
        } else {                                                               \
            printf("  FAIL  ");                                                \
            g_fail++;                                                          \
        }                                                                      \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
    } while (0)

#define SKIP(...)                                                              \
    do {                                                                       \
        printf("  skip  ");                                                    \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
        g_skip++;                                                              \
    } while (0)

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN) {
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : "warn", msg);
    }
}

/* ---- probes: in the game-text section, so the fault handlers accept their faults ----------------- */
typedef struct probe {
    uint32_t g;         /* guest word */
    uint32_t v;         /* value read / to write; code map: signature id */
    uintptr_t p;        /* host address */
    uint32_t r;         /* result word */
    int ok;
} probe;

SA1G_GAME_TEXT static void probe_read(sa1g_world *w, void *u)
{
    probe *p = (probe *)u;
    (void)w;
    p->v = *(volatile uint32_t *)sa1_decode(p->g);
}

SA1G_GAME_TEXT static void probe_write(sa1g_world *w, void *u)
{
    probe *p = (probe *)u;
    (void)w;
    *(volatile uint32_t *)sa1_decode(p->g) = p->v;
}

SA1G_GAME_TEXT static void probe_read_host(sa1g_world *w, void *u)
{
    probe *p = (probe *)u;
    (void)w;
    p->v = *(volatile uint32_t *)p->p;
}

SA1G_GAME_TEXT static void probe_null_field(sa1g_world *w, void *u)
{
    probe *p = (probe *)u;
    volatile uint32_t g = p->g;        /* 0 at run time */
    (void)w;
    p->v = *(volatile uint32_t *)((volatile char *)sa1_decode(g) + 0x20);
}

SA1G_GAME_TEXT static void probe_encode(sa1g_world *w, void *u)
{
    probe *p = (probe *)u;
    (void)w;
    p->r = sa1_encode((const void *)p->p);
}

SA1G_GAME_TEXT static void probe_call(sa1g_world *w, void *u)
{
    probe *p = (probe *)u;
    (void)w;
    ((void (*)(void))sa1_fn(SA1_GFN_C(p->g), p->v))();
}

/* a lowered game function handed a trap pointer (DestroyTask(0 - 8C02D548)) */
SA1G_GAME_TEXT static void probe_game_fault(sa1g_world *w, void *u)
{
    probe *p = (probe *)u;
    (void)w;
    ((void (*)(void *))sa1_fn(SA1_GFN_C(0x8C09865Cu), 0u))(sa1_decode(p->g));
}

/* A function that is a FATAL boundary stub in this build, for the fatal-stub probe (0: none known).
 * Whole game (pclower --all): 8C011D46 (VMU save code, policy "fatal" in LIB/gen/policy/stubs.tsv; replace it
 * here if that line ever changes). The phase 1 spike (tests/build_spike.py, three TUs): 8C098440, an
 * unresolved callee there (no stubs.tsv line, so fatal); in the whole game it is decompiled game code. */
static uint32_t fatal_probe_addr(void)
{
    static const uint32_t cand[] = { 0x8C011D46u, 0x8C098440u };
    size_t i;
    for (i = 0; i < sizeof cand / sizeof cand[0]; i++) {
        if (sa1_fn_name(cand[i]) != NULL) {
            return cand[i];
        }
    }
    return 0u;
}

SA1G_GAME_TEXT static void probe_fatal(sa1g_world *w, void *u)
{
    (void)w;
    (void)u;
    sa1g_fatal(0x8C098B42u, "MFree: double free");
}

/* the host-stack window: a local of game code stored into guest memory and read back */
SA1G_GAME_TEXT static void probe_stack(sa1g_world *w, void *u)
{
    probe *p = (probe *)u;
    volatile uint32_t local = 0x12345678u;
    uint32_t g = sa1_encode((const void *)&local);
    (void)w;
    p->g = g;
    p->p = (uintptr_t)&local;
    p->ok = (uintptr_t)sa1_decode(g) == (uintptr_t)&local;
    *(volatile uint32_t *)SA1_GV(0x8C000F20u) = g;
    p->v = *(volatile uint32_t *)sa1_decode(*(volatile uint32_t *)SA1_GV(0x8C000F20u));
}

/* is a host address unmapped (an access faults)? */
static int host_unmapped(uintptr_t a)
{
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((const void *)a, &mbi, sizeof mbi) == 0) {
        return 0;
    }
    return mbi.State == MEM_FREE || mbi.State == MEM_RESERVE;
#else
    (void)a;
    return 0;
#endif
}

/* a stack-window word whose host address lies below the live stack */
SA1G_GAME_TEXT static void probe_stack_low(sa1g_world *w, void *u)
{
    probe *p = (probe *)u;
    uintptr_t a = (uintptr_t)sa1_decode(p->g);
    (void)w;
    p->p = a;
    p->ok = host_unmapped(a);
    if (p->ok) {
        p->v = *(volatile uint32_t *)a;
    }
}

/* inside-the-world properties (MXCSR, decode/encode, SA1_GV) */
typedef struct props {
    uint32_t mxcsr;
    int null_ok, alias_ok, encode_ok, gv_ok;
} props;

static void probe_props(sa1g_world *w, void *u)
{
    props *q = (props *)u;
    q->mxcsr = _mm_getcsr();
    q->null_ok = sa1_decode(0) == NULL && sa1_encode(NULL) == 0;
    q->alias_ok = sa1_decode(0x8C123456u) == w->ram + 0x123456 && sa1_decode(0x0C123456u) == w->ram + 0x123456
                  && sa1_decode(0xAC123456u) == w->ram + 0x123456;
    q->encode_ok = sa1_encode(w->ram + 0x123456) == 0x8C123456u && sa1_encode(w->ram + 0x950000) == 0x0C950000u;
    q->gv_ok = SA1_GV(0x8C79F044u) == (void *)(w->ram + 0x79F044);
}

/* run one probe in the world; returns the fault kind (0 = none) and clears the fault */
static int run_probe(sa1g_world *w, sa1g_world_fn fn, probe *p, sa1g_fault *out)
{
    sa1_status st = sa1g_world_call(w, fn, p);
    int kind = st == SA1_OK ? 0 : w->fault.kind;
    if (out) {
        *out = w->fault;
    }
    if (st != SA1_OK) {
        sa1_status again = sa1g_world_call(w, fn, p);   /* a dead world refuses to run */
        if (again != st) {
            printf("    dead world ran again (%d)\n", (int)again);
            g_fail++;
        }
        sa1g_world_clear_fault(w);
    }
    return kind;
}

/* ---- the lowered call sequence in a world -------------------------------------------------------- */
typedef struct seq_ctx {
    int i;
    uint32_t a[3];
    uint32_t r;
} seq_ctx;

static void seq_step_cb(sa1g_world *w, void *u)
{
    seq_ctx *c = (seq_ctx *)u;
    (void)w;
    c->r = call_lowered(c->i, c->a);
}

static int game_has_sequence(void)
{
    int i;
    if (!sa1g_game_linked()) {
        return 0;
    }
    for (i = 0; i < NSTEPS; i++) {
        if (sa1_fn_name(k_steps[i].addr) == NULL) {
            return 0;
        }
    }
    return 1;
}

/* step i of the sequence in world w; res[] holds that world's results */
static sa1_status seq_step(sa1g_world *w, int i, uint32_t *res)
{
    seq_ctx c;
    int k;
    sa1_status st;
    c.i = i;
    for (k = 0; k < 3; k++) {
        c.a[k] = (k_steps[i].res_mask & (1u << k)) ? res[k_steps[i].a[k]] : k_steps[i].a[k];
    }
    c.r = 0;
    st = sa1g_world_call(w, seq_step_cb, &c);
    res[i] = k_steps[i].ret ? c.r : 0u;
    return st;
}

/* byte ranges excluded from the reference comparison (overlay spans: the reference has none) */
typedef struct range {
    uint32_t lo, hi;
} range;

static size_t compare_excl(const char *what, const unsigned char *mine, const unsigned char *ref, const range *ex,
                           int nex, const sa1g_image *img)
{
    size_t off, ndiff = 0;
    int shown = 0, k;
    for (off = 0; off < RAM_SIZE; off++) {
        int skip = off >= STACK_LO && off < STACK_HI;
        for (k = 0; k < nex && !skip; k++) {
            skip = off >= ex[k].lo && off < ex[k].hi;
        }
        if (!skip && img != NULL) {
            uint32_t pi;
            for (pi = 0; pi < img->num_patches && !skip; pi++) {
                uint32_t po = img->patches[pi].guest & 0xFFFFFFu;
                skip = off >= po && off < po + 4u;
            }
        }
        if (skip || mine[off] == ref[off]) {
            continue;
        }
        if (shown < 12) {
            uint32_t wd = (uint32_t)off & ~3u;
            printf("    %s: %08X world %08X reference %08X\n", what, 0x8C000000u | wd, rd32(mine, wd), rd32(ref, wd));
            shown++;
        }
        ndiff++;
    }
    return ndiff;
}

static void exit_regions(const unsigned char *mine, const unsigned char *ref, uint32_t tp)
{
    uint32_t twp = rd32(ref, tp + 0x20), mwp = rd32(ref, tp + 0x24), fwp = rd32(ref, tp + 0x28);
    CHECK(region_eq(mine, ref, 0x8C79F03Cu, 8), "heap globals B_8C79F03C (free head %08X) / B_8C79F040",
          rd32(mine, 0x8C79F03Cu));
    CHECK(region_eq(mine, ref, 0x8C79F044u, 0x28), "task globals B_8C79F044 (pool %08X), B_8C79F048 (free %08X), "
          "B_8C79F04C[8] (level 1 head %08X)", rd32(mine, 0x8C79F044u), rd32(mine, 0x8C79F048u),
          rd32(mine, 0x8C79F050u));
    CHECK(region_eq(mine, ref, 0x8C7EE8E0u, 0x90010), "heap arena 8C7EE8E0-8C87E8F0 (blocks, headers, 0xFF fill)");
    CHECK(region_eq(mine, ref, rd32(ref, 0x8C79F044u), 0x30000), "task pool %08X (0x1000 tasks x 0x30, free list)",
          rd32(ref, 0x8C79F044u));
    CHECK(region_eq(mine, ref, tp, 0x30), "task %08X (exec %08X, twp %08X, mwp %08X, fwp %08X)", tp,
          rd32(mine, tp + 0x10), rd32(mine, tp + 0x20), rd32(mine, tp + 0x24), rd32(mine, tp + 0x28));
    CHECK(twp && region_eq(mine, ref, twp - 0x10, 0x50), "taskwk %08X (+ heap header)", twp);
    CHECK(mwp && region_eq(mine, ref, mwp - 0x10, 0x50), "motionwk %08X (+ heap header)", mwp);
    CHECK(fwp && region_eq(mine, ref, fwp - 0x10, 0x48), "forcewk[2] %08X (+ heap header)", fwp);
}

/* ---- tick contract hooks ---------------------------------------------------------------------------- */
typedef struct tick_rec {
    uint64_t n;
    uint32_t ec4_bad, ec8_bad, mxcsr_bad;
    uint32_t on[2];
    int write_press, push_event, fault;
} tick_rec;

static void rec_step(sa1g_world *w, int32_t sub, void *user)
{
    tick_rec *r = (tick_rec *)user;
    uint32_t ec4 = 0, ec8 = 0;
    sa1g_world_rd32(w, SA1G_ADDR_STEPS_PER_FRAME, &ec4);
    sa1g_world_rd32(w, SA1G_ADDR_SUB_STEP, &ec8);
    r->ec4_bad += ec4 != 2u;
    r->ec8_bad += ec8 != (uint32_t)sub || (uint64_t)sub != (r->n & 1u);
    r->mxcsr_bad += _mm_getcsr() != 0xFFC0u;
    r->on[sub & 1] = w->pad[0].on;
    if (r->write_press) {
        /* what the pad update leaves: per[0] = a record, press bits of this step */
        sa1g_world_wr32(w, SA1G_ADDR_PAD_PTRS, 0x8C000F40u);
        sa1g_world_wr32(w, 0x8C000F50u, sub == 0 ? 0x4u : 0x200u);
    }
    if (r->push_event) {
        float pos[3] = { 1.0f, 2.0f, 3.0f };
        sa1g_event_push(SA1_EVENT_SOUND, 0x1234u, 3u, pos, (uint32_t)sub);
    }
    if (r->fault && sub == 1) {
        probe p;
        memset(&p, 0, sizeof p);
        p.g = 0x73FD2AB8u;
        probe_read(w, &p);          /* faults: segment 0x13 */
    }
    r->n++;
}

/* ---- world image ------------------------------------------------------------------------------------ */
static unsigned char *g_snap[3];
static uint32_t g_res2[NSTEPS], g_res3[NSTEPS];

static const sa1g_span *span_named(const sa1g_image *img, const char *name)
{
    uint32_t i;
    for (i = 0; i < img->num_spans; i++) {
        if (strcmp(img->spans[i].name, name) == 0) {
            return &img->spans[i];
        }
    }
    return NULL;
}

static int test_image(sa1_context *ctx, const char *gdi, const char *rom_path, sa1g_image *img)
{
    sa1_status st;
    printf("world image:\n");
    if (gdi != NULL && *gdi) {
        static const struct {
            int32_t kind;
            uint32_t placeholders;
        } kinds[] = {
            { SA1_CHAR_TAILS, 1 }, { SA1_CHAR_KNUCKLES, 1 }, { SA1_CHAR_AMY, 1 }, { SA1_CHAR_GAMMA, 1 },
            { SA1_CHAR_BIG, 8 }, { SA1_CHAR_TIKAL, 0 }, { SA1_CHAR_SUPER_SONIC, 23 },
        };
        const sa1g_span *s1, *s2, *s3, *s4;
        sa1g_image tmp;
        uint8_t dummy[16];
        size_t k;
        st = sa1g_image_compose(ctx, gdi, SA1_CHAR_SONIC, 0, img);
        CHECK(st == SA1_OK, "Sonic image composed from %s (%s)", gdi, st == SA1_OK ? "ok" : sa1_last_error(ctx));
        if (st != SA1_OK) {
            return -1;
        }
        s1 = span_named(img, "1ST_READ.BIN");
        s2 = span_named(img, "S_MOT.PRS");
        s3 = span_named(img, "S_SBMOT.PRS");
        s4 = span_named(img, "B_CHAOS7.PRS");
        CHECK(img->num_spans == 4 && s1 && s1->guest == 0x8C010000u && s1->size == ROM_SIZE && s2
              && s2->guest == 0x0CC00000u && s3 && s3->guest == 0x0CB08000u && s4 && s4->guest == 0x0C900000u,
              "spans: 1ST_READ.BIN 8C010000 (%u B), S_MOT 0CC00000 (%u B), S_SBMOT 0CB08000 (%u B), B_CHAOS7 "
              "0C900000 (%u B)", s1 ? (unsigned)s1->size : 0u, s2 ? (unsigned)s2->size : 0u,
              s3 ? (unsigned)s3->size : 0u, s4 ? (unsigned)s4->size : 0u);
        CHECK(sa1g_image_count_patches(img, SA1G_PATCH_ACTION_PLACEHOLDER) == 23,
              "Sonic: %u action placeholders (23 = stage-overlay actions 40, 42, 44-46, 88-101, 126-129)",
              (unsigned)sa1g_image_count_patches(img, SA1G_PATCH_ACTION_PLACEHOLDER));
        for (k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
            st = sa1g_image_compose(ctx, gdi, kinds[k].kind, 0, &tmp);
            CHECK(st == SA1_OK && sa1g_image_count_patches(&tmp, SA1G_PATCH_ACTION_PLACEHOLDER)
                  == kinds[k].placeholders, "%s image: %u spans, %u action placeholders (expected %u)",
                  sa1_character_name(kinds[k].kind), (unsigned)tmp.num_spans,
                  (unsigned)sa1g_image_count_patches(&tmp, SA1G_PATCH_ACTION_PLACEHOLDER),
                  (unsigned)kinds[k].placeholders);
            sa1g_image_free(&tmp);
        }
        st = sa1g_image_compose(ctx, gdi, SA1_CHAR_SONIC, SA1G_IMAGE_NO_OPTIONAL, &tmp);
        CHECK(st == SA1_OK && tmp.num_spans == 2 && sa1g_image_count_patches(&tmp, SA1G_PATCH_ACTION_PLACEHOLDER) > 23,
              "Sonic without the optional overlays: %u spans, %u placeholders", (unsigned)tmp.num_spans,
              (unsigned)sa1g_image_count_patches(&tmp, SA1G_PATCH_ACTION_PLACEHOLDER));
        sa1g_image_free(&tmp);
        memset(dummy, 0, sizeof dummy);
        st = sa1g_image_compose(ctx, gdi, SA1_CHAR_SONIC, 0, &tmp);
        if (st == SA1_OK) {
            st = sa1g_image_add_span(&tmp, "STG02.PRS", 0x0C900000u, dummy, sizeof dummy);
            CHECK(st == SA1_ERR_UNSUPPORTED, "STG02 at 0C900000 next to B_CHAOS7: one occupant per slot (%s)",
                  sa1_last_error(ctx));
            sa1g_image_free(&tmp);
        }
        st = sa1g_image_compose(ctx, "no_such_disc.gdi", SA1_CHAR_SONIC, 0, &tmp);
        CHECK(st < 0, "a missing .gdi is an error (%s)", sa1g_status_string(st));
    } else {
        unsigned char *rom;
        size_t n = 0;
        sa1g_image tmp;
        rom = read_file(rom_path, &n);
        if (rom == NULL) {
            printf("FAIL: cannot read %s (and SA1_TEST_GDI is not set)\n", rom_path);
            return -1;
        }
        st = sa1g_image_from_memory(ctx, SA1G_KIND_BARE, rom, n, img);
        CHECK(st == SA1_OK, "bare image from %s (%s)", rom_path, st == SA1_OK ? "SHA-1 ok" : sa1_last_error(ctx));
        if (st == SA1_OK) {
            st = sa1g_image_finalize(img);
        }
        if (n > 100) {
            rom[100] ^= 1u;
            CHECK(sa1g_image_from_memory(ctx, SA1G_KIND_BARE, rom, n, &tmp) == SA1_ERR_BAD_HASH,
                  "a modified 1ST_READ.BIN is refused (%s)", sa1_last_error(ctx));
            rom[100] ^= 1u;
        }
        free(rom);
        SKIP("composition from the disc (set SA1_TEST_GDI=<game.gdi>)");
        if (st != SA1_OK) {
            return -1;
        }
    }
    return 0;
}

static void test_world_image(const sa1g_world *w, const sa1g_image *img)
{
    uint32_t i, k, v = 0;
    int same = 1, zero = 1;
    for (i = 0; i < img->num_spans && same; i++) {
        const sa1g_span *s = &img->spans[i];
        const unsigned char *a = w->ram + (s->guest & 0xFFFFFFu);
        uint32_t off;
        for (off = 0; off < s->size; off++) {
            int patched = 0;
            if (a[off] == s->data[off]) {
                continue;
            }
            for (k = 0; k < img->num_patches && !patched; k++) {
                uint32_t po = img->patches[k].guest & 0xFFFFFFu, so = (s->guest & 0xFFFFFFu) + off;
                patched = so >= po && so < po + 4u;
            }
            if (!patched) {
                same = 0;
                break;
            }
        }
    }
    for (i = SA1G_BSS_START & 0xFFFFFFu; i < (SA1G_BSS_END & 0xFFFFFFu); i++) {
        if (w->ram[i] != 0) {
            zero = 0;
            break;
        }
    }
    CHECK(same, "arena holds every span at its load address (%u spans; patched words excepted)",
          (unsigned)img->num_spans);
    CHECK(zero, "BSS 8C67C680-8C900000 is zero");
    if (img->num_patches > 0) {
        uint32_t obj = 0;
        sa1g_world_rd32(w, img->patches[0].guest, &v);
        CHECK(v == w->placeholder_action && (v >> 24) == 0x98u && w->patches_applied == img->num_patches,
              "%u patches applied; action %u now points at the placeholder %08X in segment 0x18",
              (unsigned)w->patches_applied, (unsigned)img->patches[0].arg, (unsigned)v);
        if (sa1g_world_ptr(w, v) != NULL) {
            memcpy(&obj, sa1g_world_ptr(w, v), 4);
        }
        CHECK(obj == img->main_object && ((w->missing_actions[img->patches[0].arg >> 5] >> (img->patches[0].arg & 31u)) & 1u),
              "placeholder NJS_ACTION object = root object %08X; missing-action bit set", (unsigned)obj);
    }
}

/* ---- the lowered sequence in world w, compared with the interpreter reference ------------------------ */
static void test_sequence(sa1g_world *w, const sa1g_image *img, const char *ref_prefix)
{
    char path[1024];
    unsigned char *ref;
    size_t n;
    range ex[SA1G_IMAGE_MAX_SPANS];
    int nex = 0, i;
    uint32_t k;
    printf("lowered call sequence in the world (%d steps, game: %s):\n", NSTEPS, sa1g_game_info());
    for (i = 0; i < NSTEPS; i++) {
        sa1_status st = seq_step(w, i, g_res);
        if (st != SA1_OK) {
            printf("  FAIL  step %d %s: %s: %s\n", i, k_steps[i].what, sa1g_status_string(st), w->fault.msg);
            g_fail++;
            return;
        }
        if (k_steps[i].dump) {
            g_snap[k_steps[i].dump] = (unsigned char *)malloc(RAM_SIZE);
            memcpy(g_snap[k_steps[i].dump], w->ram, RAM_SIZE);
        }
    }
    for (i = 0; i < NSTEPS; i++) {
        printf("    step %2d %-40s -> %08X\n", i, k_steps[i].what, g_res[i]);
    }
    for (k = 0; k < img->num_spans; k++) {
        if (img->spans[k].guest != SA1G_FIRST_READ_ADDR) {
            ex[nex].lo = img->spans[k].guest & 0xFFFFFFu;
            ex[nex].hi = ex[nex].lo + img->spans[k].size;
            nex++;
        }
    }
    if (strcmp(ref_prefix, "-") == 0) {
        SKIP("reference comparison (no reference prefix)");
        return;
    }
    snprintf(path, sizeof path, "%s.res", ref_prefix);
    {
        FILE *rf = fopen(path, "r");
        int same = rf != NULL;
        if (rf == NULL) {
            SKIP("reference comparison: %s missing (run tests/build_spike.py)", path);
            return;
        }
        for (i = 0; i < NSTEPS; i++) {
            unsigned v;
            if (fscanf(rf, "%x", &v) != 1 || v != g_res[i]) {
                printf("    step %d: world %08X reference %08X\n", i, g_res[i], v);
                same = 0;
            }
        }
        fclose(rf);
        CHECK(same, "return values of all %d steps equal the interpreter's (%s)", NSTEPS, path);
    }
    printf("  exit test (phase 1):\n");
    snprintf(path, sizeof path, "%s_exit.bin", ref_prefix);
    ref = read_file(path, &n);
    if (ref == NULL || n != RAM_SIZE) {
        CHECK(0, "reference %s", path);
        free(ref);
        return;
    }
    exit_regions(g_snap[1], ref, g_res[2]);
    {
        size_t d = compare_excl("exit", g_snap[1], ref, ex, nex, img);
        CHECK(d == 0, "whole arena equal (guest stack 8C00C000-8C00F400, %d overlay span(s), %u patched word(s) "
              "excepted): %zu bytes differ", nex, (unsigned)img->num_patches, d);
    }
    free(ref);
    snprintf(path, sizeof path, "%s_ext.bin", ref_prefix);
    ref = read_file(path, &n);
    if (ref == NULL || n != RAM_SIZE) {
        CHECK(0, "reference %s", path);
        free(ref);
        return;
    }
    {
        size_t d = compare_excl("ext", g_snap[2], ref, ex, nex, img);
        CHECK(d == 0, "whole arena equal after the extended sequence: %zu bytes differ (free %08X, largest %08X)", d,
              rd32(g_snap[2], 0x8C000F08u), rd32(g_snap[2], 0x8C000F0Cu));
    }
    free(ref);
}

static void test_two_worlds(sa1g_world *w2, sa1g_world *w3, sa1g_shared *shared, int have_seq)
{
    probe p;
    int i, ok = 1;
    printf("two worlds in one process:\n");
    CHECK(w2->seg[SA1G_SEG_ARENA] != w3->seg[SA1G_SEG_ARENA] && w2->seg[SA1G_SEG_EXT] != w3->seg[SA1G_SEG_EXT]
          && w2->seg[SA1G_SEG_COL0] == (uintptr_t)shared->col && w3->seg[SA1G_SEG_COL0 + 3] == (uintptr_t)shared->col
          + 3u * SA1G_SEG_SIZE, "own arena and extension segment each; the collision segment 0x08-0x0B is shared");
    if (have_seq) {
        for (i = 0; i < NSTEPS && ok; i++) {
            ok = seq_step(w2, i, g_res2) == SA1_OK && seq_step(w3, i, g_res3) == SA1_OK;
        }
        CHECK(ok && memcmp(g_res2, g_res, sizeof g_res) == 0 && memcmp(g_res3, g_res, sizeof g_res) == 0,
              "the sequence interleaved step by step in two worlds gives the single-world results");
        CHECK(memcmp(w2->ram, w3->ram, RAM_SIZE) == 0 && g_snap[2] && memcmp(w2->ram, g_snap[2], RAM_SIZE) == 0,
              "both arenas equal the single-world arena byte for byte");
    } else {
        SKIP("interleaved sequence (no lowered game code linked)");
    }
    memset(&p, 0, sizeof p);
    p.g = 0x8C000F30u;
    p.v = 0xA5A5A5A5u;
    ok = run_probe(w2, probe_write, &p, NULL) == 0;
    p.v = 0;
    ok = ok && run_probe(w3, probe_read, &p, NULL) == 0;
    CHECK(ok && p.v == 0 && *(uint32_t *)(void *)(w2->ram + 0xF30) == 0xA5A5A5A5u,
          "a store in one world is invisible in the other (read %08X)", (unsigned)p.v);
}

/* ---- guest addresses and faults ---------------------------------------------------------------------- */
static void test_faults(sa1g_world *w, sa1g_shared *shared)
{
    static const struct {
        uint32_t g;
        int kind;
        int seg;
        const char *what;
    } traps[] = {
        { 0x73FD2AB8u, SA1G_FAULT_NEGTRAP, 0x13, "COLOBJ_HEAD_LINK word 0 - 8C02D548" },
        { 0xF3FD2AB8u, SA1G_FAULT_NEGTRAP, 0x13, "the same word with bit 31 set" },
        { 0x3F800000u, SA1G_FAULT_TRAP, 0x1F, "1.0f used as a pointer" },
        { 0x00000010u, SA1G_FAULT_TRAP, 0x00, "small integer used as a pointer" },
        { 0x91000000u, SA1G_FAULT_TRAP, 0x11, "word of an unmapped segment" },
    };
    props q;
    probe p;
    sa1g_fault f;
    size_t k;
    int kind, local = 0;
    void *host = NULL;
    uint32_t g;
    printf("guest address machinery and faults:\n");
    memset(&q, 0, sizeof q);
    CHECK(sa1g_world_call(w, probe_props, &q) == SA1_OK && q.mxcsr == 0xFFC0u,
          "inside the world MXCSR = %08X (0xFFC0: round toward zero, FTZ, DAZ, masked)", (unsigned)q.mxcsr);
    CHECK(q.null_ok && q.alias_ok && q.encode_ok && q.gv_ok, "NULL stays NULL; P0/P1/P2 aliases; encode P1 below "
          "0x900000, P0 above; SA1_GV(global) = ram + (a & 0xFFFFFF)");
    CHECK(sa1g_text_contains((uintptr_t)probe_read) && !sa1g_text_contains((uintptr_t)test_faults),
          "game-text section: probes inside, test code outside (the fault handlers act only for game text)");
    for (k = 0; k < sizeof traps / sizeof traps[0]; k++) {
        memset(&p, 0, sizeof p);
        p.g = traps[k].g;
        kind = run_probe(w, probe_read, &p, &f);
        CHECK(kind == traps[k].kind && (sizeof(void *) == 4 || f.seg == traps[k].seg)
              && (f.guest & 0xFFFFFFu) == (traps[k].g & 0xFFFFFFu) && f.text_off != 0,
              "%s (%08X): '%s', segment %d, guest %08X", traps[k].what, (unsigned)traps[k].g, sa1g_fault_name(kind),
              (int)f.seg, (unsigned)f.guest);
        if (k == 0) {
            printf("        %s\n", f.msg);
        }
    }
    memset(&p, 0, sizeof p);
    p.p = 0x20;
    kind = run_probe(w, probe_read_host, &p, &f);
    CHECK(kind == SA1G_FAULT_NULL && f.guest == 0x20u, "host NULL + 0x20: '%s'", sa1g_fault_name(kind));
    printf("        %s\n", f.msg);
    memset(&p, 0, sizeof p);
    kind = run_probe(w, probe_null_field, &p, &f);
    CHECK(kind == SA1G_FAULT_NULL || (kind == SA1G_FAULT_TRAP && (f.guest & 0xFFFFFFu) == 0x20u),
          "decode(0) + 0x20: '%s' (the compiler may fold the NULL select into segment 0's trap)", sa1g_fault_name(kind));
    p.p = (uintptr_t)w->ram + RAM_SIZE + 0x20;
    kind = run_probe(w, probe_read_host, &p, &f);
    CHECK(kind == SA1G_FAULT_GUARD && f.guest == 0x8D000020u, "just past the arena: '%s' at %08X",
          sa1g_fault_name(kind), (unsigned)f.guest);
    p.p = (uintptr_t)w->ram - 0x10;
    kind = run_probe(w, probe_read_host, &p, &f);
    CHECK(kind == SA1G_FAULT_GUARD, "just before the arena: '%s'", sa1g_fault_name(kind));
    /* extension segment */
    g = sa1g_world_ext_alloc(w, 16, 4, &host);
    memset(&p, 0, sizeof p);
    p.g = g;
    p.v = 0x5EC70018u;
    kind = run_probe(w, probe_write, &p, NULL);
    CHECK(g >> 24 == 0x98u && kind == 0 && host && *(uint32_t *)host == 0x5EC70018u,
          "extension segment: %08X allocated, written through decode", (unsigned)g);
    p.g = SA1G_EXT_GUEST | 0x800000u;
    kind = run_probe(w, probe_read, &p, &f);
    CHECK(kind == SA1G_FAULT_EXT && f.seg == SA1G_SEG_EXT, "extension segment beyond the allocation: '%s' at %08X",
          sa1g_fault_name(kind), (unsigned)f.guest);
    /* collision segment */
    g = sa1g_colseg_alloc(shared, 64, 16, &host);
    if (g != 0 && host != NULL) {
        *(uint32_t *)host = 0xC0115E61u;
    }
    CHECK(g >> 24 == 0x88u && sa1g_colseg_seal(shared, 1) == 0, "collision segment: %08X allocated, sealed",
          (unsigned)g);
    memset(&p, 0, sizeof p);
    p.g = g;
    kind = run_probe(w, probe_read, &p, NULL);
    CHECK(kind == 0 && p.v == 0xC0115E61u, "sealed collision data reads through decode (%08X)", (unsigned)p.v);
    p.v = 1;
    kind = run_probe(w, probe_write, &p, &f);
    CHECK(kind == SA1G_FAULT_COLSEG && f.access == SA1G_ACCESS_WRITE, "a store into the sealed segment: '%s' (write)",
          sa1g_fault_name(kind));
    p.g = 0x8A000010u;
    kind = run_probe(w, probe_read, &p, &f);
    CHECK(kind == SA1G_FAULT_COLSEG && f.seg == 0x0A, "uncommitted collision segment 0x0A: '%s', segment %d",
          sa1g_fault_name(kind), (int)f.seg);
    sa1g_colseg_seal(shared, 0);
    /* host-stack window */
    memset(&p, 0, sizeof p);
    kind = run_probe(w, probe_stack, &p, NULL);
    CHECK(kind == 0 && (p.g >> 24) == 0x9Du && p.ok && p.v == 0x12345678u,
          "a game-code local encodes into the stack window (%08X), decodes back, survives a store/load through guest "
          "memory", (unsigned)p.g);
    CHECK(w->stack_top == 0 && w->seg[SA1G_SEG_STACK] != (uintptr_t)p.p - (p.g & 0xFFFFFFu),
          "the window is closed when the world call returns");
    memset(&p, 0, sizeof p);
    p.g = SA1G_STACK_GUEST | 0x1000u;
    kind = run_probe(w, probe_stack_low, &p, &f);
    if (p.ok) {
        CHECK(kind == SA1G_FAULT_STACK && f.seg == SA1G_SEG_STACK, "stack-window word below the live stack (%p, "
              "unmapped): '%s' at %08X", (void *)p.p, sa1g_fault_name(kind), (unsigned)f.guest);
    } else {
        SKIP("stack-window fault: %p (window base + 0x1000) is mapped on this host or not checkable", (void *)p.p);
    }
    memset(&p, 0, sizeof p);
    p.p = (uintptr_t)&local;
    kind = run_probe(w, probe_encode, &p, &f);
    CHECK(kind == SA1G_FAULT_ENCODE, "encode of a host stack address above the window (caller frame): '%s'",
          sa1g_fault_name(kind));
    p.p = (uintptr_t)g_snap;
    kind = run_probe(w, probe_encode, &p, &f);
    CHECK(kind == SA1G_FAULT_ENCODE, "encode of a host data address: '%s'", sa1g_fault_name(kind));
    printf("        %s\n", f.msg);
    /* code map, fatal stubs, sa1g_fatal */
    memset(&p, 0, sizeof p);
    p.g = 0x8C123456u;
    kind = run_probe(w, probe_call, &p, &f);
    CHECK(kind == SA1G_FAULT_CODEMAP && sa1g_fault_status(&f) == SA1_ERR_GAME_FAULT,
          "indirect call of an address with no code: '%s' (%s)", sa1g_fault_name(kind), f.msg);
    if (sa1g_game_linked() && fatal_probe_addr() != 0u) {
        uint32_t fa = fatal_probe_addr();
        p.g = fa;
        kind = run_probe(w, probe_call, &p, &f);
        CHECK(kind == SA1G_FAULT_UNIMPL && sa1g_fault_status(&f) == SA1_ERR_NOT_IMPLEMENTED && f.dc_func == fa,
              "code-map entry of an unresolved function is a fatal stub (%08X %s): %s", (unsigned)fa, sa1_fn_name(fa),
              f.msg);
        p.g = 0x8C09865Cu;
        p.v = 0x12345u;
        kind = run_probe(w, probe_call, &p, &f);
        CHECK(kind == SA1G_FAULT_CODEMAP, "indirect call with a wrong signature id: %s", f.msg);
        CHECK(sa1_fn_addr(sa1_fn(SA1_GFN_C(0x0C09852Eu), 0u)) == 0x8C09852Eu,
              "code map: P0 alias resolves; reverse map gives 8C09852E (%s)", sa1_fn_name(0x8C09852Eu));
        memset(&p, 0, sizeof p);
        p.g = 0x73FD2AB8u;
        kind = run_probe(w, probe_game_fault, &p, &f);
        CHECK(kind == SA1G_FAULT_NEGTRAP && (sizeof(void *) == 4 || f.dc_func == 0x8C09865Cu),
              "a fault inside lowered game code names the DC function where the host can unwind (win64): %s",
              f.msg);
    } else {
        SKIP("fatal stub / signature checks (no lowered game code linked)");
    }
    kind = run_probe(w, probe_fatal, &p, &f);
    CHECK(kind == SA1G_FAULT_FATAL && f.dc_func == 0x8C098B42u, "sa1g_fatal: '%s' (%s)", sa1g_fault_name(kind), f.msg);
    CHECK(w->faults >= 15, "%u faults recorded; each left the world dead until cleared", (unsigned)w->faults);
}

/* ---- the tick contract and MXCSR ---------------------------------------------------------------------- */
static void test_tick(sa1g_world *w)
{
    tick_rec r;
    sa1g_pad pad;
    sa1_event ev[SA1G_EVENT_QUEUE];
    sa1_input in;
    uint32_t n = 0, v = 0, i, host, custom, bad = 0;
    sa1_status st;
    printf("tick contract:\n");
    st = sa1g_world_pair(w);
    if (sa1g_game_ready()) {
        CHECK(st == SA1_ERR_NOT_LOADED, "default step of a world that was not booted: %s (refused: no heap, tasks or "
              "landtable yet)", sa1g_status_string(st));
    } else {
        CHECK(st == SA1_ERR_NOT_IMPLEMENTED, "default step without the executor/CCL/land code linked: %s "
              "(sa1_character_tick reports NOT_IMPLEMENTED too)", sa1g_status_string(st));
    }
    st = sa1g_world_tick(w, 1.0f / 30.0f, 0, &n);
    CHECK(st == (sa1g_game_ready() ? SA1_ERR_NOT_LOADED : SA1_ERR_NOT_IMPLEMENTED), "sa1g_world_tick likewise: %s",
          sa1g_status_string(st));
    memset(&r, 0, sizeof r);
    w->step = rec_step;
    w->user = &r;
    w->acc = 0.0;
    st = sa1g_world_pair(w);
    CHECK(st == SA1_OK && r.n == 2 && r.ec4_bad == 0 && r.ec8_bad == 0 && r.mxcsr_bad == 0 && w->pairs == 1,
          "a pair: 2 steps, B_8C754EC4 = 2, B_8C754EC8 = 0 then 1, MXCSR 0xFFC0 in both");
    memset(&pad, 0, sizeof pad);
    pad.connected = 1;
    pad.on = SA1G_PAD_A;
    sa1g_world_set_pad(w, 0, &pad);
    pad.on = 0;
    sa1g_world_set_pad(w, 0, &pad);
    st = sa1g_world_pair(w);
    CHECK(st == SA1_OK && r.on[0] == SA1G_PAD_A && r.on[1] == 0,
          "A pressed and released between two pairs reaches sub 0 (on %X) only (sub 1 on %X)", (unsigned)r.on[0],
          (unsigned)r.on[1]);
    pad.on = SA1G_PAD_A | SA1G_PAD_B;
    sa1g_world_set_pad(w, 0, &pad);
    st = sa1g_world_pair(w);
    st = st == SA1_OK ? sa1g_world_pair(w) : st;
    CHECK(st == SA1_OK && r.on[0] == (SA1G_PAD_A | SA1G_PAD_B) && r.on[1] == r.on[0], "held buttons in both steps of "
          "every pair (%X / %X)", (unsigned)r.on[0], (unsigned)r.on[1]);
    r.write_press = 1;
    st = sa1g_world_pair(w);
    r.write_press = 0;
    sa1g_world_rd32(w, 0x8C000F50u, &v);
    CHECK(st == SA1_OK && v == 0x204u, "per[0].press ORed across the pair and written back: %X (0x4 | 0x200)",
          (unsigned)v);
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_y = 1.0f;
    in.stick_x = -0.5f;
    in.buttons = SA1_BUTTON_JUMP | SA1_BUTTON_ACTION2;
    sa1g_pad_from_input(&in, &pad);
    CHECK(pad.connected && pad.on == (SA1G_PAD_A | SA1G_PAD_Y) && pad.y1 == -127 && pad.x1 == -64,
          "sa1_input -> pad: buttons %X, stick (%d, %d) (y < 0 = forward)", (unsigned)pad.on, pad.x1, pad.y1);
    /* accumulator */
    w->acc = 0.0;
    sa1g_world_tick(w, 1.0f / 60.0f, 0, &n);
    i = n;
    sa1g_world_tick(w, 1.0f / 60.0f, 0, &n);
    CHECK(i == 0 && n == 1, "accumulator: 1/60 s runs no pair, the next 1/60 s one pair");
    w->backlog_drops = 0;
    st = sa1g_world_tick(w, 0.5f, 8, &n);
    CHECK(st == SA1_OK && n == 8 && w->backlog_drops == 7 && w->acc < SA1G_PAIR_SECONDS,
          "0.5 s with at most 8 pairs: %u pairs run, %u dropped, %.6f s carried", (unsigned)n,
          (unsigned)w->backlog_drops, w->acc);
    CHECK(sa1g_world_tick(w, -1.0f, 0, &n) == SA1_ERR_INVALID_ARGUMENT, "a negative dt is refused");
    /* replay steps carry their own sub */
    r.n = 1;
    i = r.ec8_bad;
    st = sa1g_world_step(w, 1);
    CHECK(st == SA1_OK && r.ec8_bad == i && sa1g_world_step(w, 2) == SA1_ERR_INVALID_ARGUMENT,
          "single replay step with sub 1 (B_8C754EC8 = 1)");
    r.n = 0;
    /* events */
    sa1g_world_poll_events(w, ev, SA1G_EVENT_QUEUE);
    r.push_event = 1;
    st = sa1g_world_pair(w);
    n = sa1g_world_poll_events(w, ev, 8);
    CHECK(st == SA1_OK && n == 2 && ev[0].type == SA1_EVENT_SOUND && ev[0].id == 0x1234u && ev[0].bank == 3
          && ev[0].position[2] == 3.0f && ev[0].arg == 0 && ev[1].arg == 1 && ev[1].step == ev[0].step + 1,
          "events pushed inside a pair are polled after it (%u: sound %X bank %u, subs %u/%u)", (unsigned)n,
          (unsigned)ev[0].id, (unsigned)ev[0].bank, (unsigned)ev[0].arg, (unsigned)ev[1].arg);
    w->events.dropped = 0;
    for (i = 0; i < 130; i++) {
        sa1g_world_pair(w);
    }
    n = sa1g_world_poll_events(w, ev, SA1G_EVENT_QUEUE);
    CHECK(n == SA1G_EVENT_QUEUE && w->events.dropped == 4, "queue of %u: 260 pushes keep %u, %u dropped",
          (unsigned)SA1G_EVENT_QUEUE, (unsigned)n, (unsigned)w->events.dropped);
    r.push_event = 0;
    /* MXCSR: 100,000 pairs */
    host = _mm_getcsr();
    custom = (host & ~0x6000u) | 0x2000u;        /* round down: a value the world never uses */
    _mm_setcsr(custom);
    r.mxcsr_bad = 0;
    for (i = 0; i < 100000u; i++) {
        if (sa1g_world_pair(w) != SA1_OK) {
            bad++;
        }
        if (_mm_getcsr() != custom) {
            bad++;
        }
    }
    v = _mm_getcsr();
    _mm_setcsr(host);
    CHECK(bad == 0 && r.mxcsr_bad == 0 && v == custom, "100,000 pairs: 0xFFC0 inside every step, the host MXCSR "
          "%08X back after every pair (%u bad)", (unsigned)custom, (unsigned)bad);
    /* a fault inside a pair */
    _mm_setcsr(custom);
    r.fault = 1;
    st = sa1g_world_pair(w);
    v = _mm_getcsr();
    _mm_setcsr(host);
    r.fault = 0;
    n = sa1g_world_poll_events(w, ev, 8);
    CHECK(st == SA1_ERR_GAME_FAULT && w->dead && w->fault.kind == SA1G_FAULT_NEGTRAP && v == custom
          && n == 1 && ev[0].type == SA1_EVENT_FAULT && ev[0].id == 0x73FD2AB8u,
          "a fault in sub 1: %s, world dead, MXCSR %08X restored, SA1_EVENT_FAULT queued", sa1g_status_string(st),
          (unsigned)v);
    CHECK(sa1g_world_pair(w) == SA1_ERR_GAME_FAULT && sa1g_world_tick(w, 0.1f, 0, &n) == SA1_ERR_GAME_FAULT,
          "a dead world does not tick");
    sa1g_world_clear_fault(w);
    w->step = sa1g_step_game;
    w->user = NULL;
}

/* ---- the API sketch: sa1_character_* over worlds ----------------------------------------------------- */
static void test_api(sa1_context *ctx, const char *gdi)
{
    static sa1g_wctx c;
    sa1g_world *w = NULL, *w2 = NULL;
    sa1_input in;
    sa1_state stt;
    sa1_event ev[4];
    uint32_t n = 0;
    sa1_status st;
    float pos[3] = { 36.0f, 64.0f, 16.0f };     /* host space (Source preset: 4 * (x, -z, y)) */
    float sa1pos[3] = { 1.0f, 2.0f, 3.0f };
    printf("API sketch (sa1_character_* over worlds):\n");
    if (gdi == NULL || *gdi == 0) {
        SKIP("character worlds (set SA1_TEST_GDI=<game.gdi>)");
        return;
    }
    st = sa1g_wctx_init(&c, ctx, gdi);
    if (st == SA1_OK) {
        st = sa1g_character_world_create(&c, SA1_CHAR_KNUCKLES, pos, 0.0f, &w);
    }
    CHECK(st == SA1_OK && w && w->kind == SA1_CHAR_KNUCKLES && c.composed[SA1_CHAR_KNUCKLES] && w->start_pos[0] == 9.0f
          && w->start_pos[1] == 4.0f && w->start_pos[2] == -16.0f,
          "Knuckles world from the disc; start (%g, %g, %g) in SA1 space", w ? w->start_pos[0] : 0.0f,
          w ? w->start_pos[1] : 0.0f, w ? w->start_pos[2] : 0.0f);
    st = sa1g_character_world_create(&c, SA1_CHAR_KNUCKLES, NULL, 0.0f, &w2);
    CHECK(st == SA1_OK && w2 && w && w2->image == w->image && w2->ram != w->ram,
          "a second Knuckles world reuses the pristine image (one per kind and context), own arena");
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    memset(&stt, 0xAB, sizeof stt);
    stt.struct_size = sizeof stt;
    st = w ? sa1g_character_world_tick(w, &in, 1.0f / 30.0f, &stt) : SA1_ERR_INVALID_ARGUMENT;
    if (sa1g_game_ready()) {
        /* one pair from the start: step 0 is the main's mode 0 (PInitialize), no ground under Knuckles */
        float d0 = stt.position[0] - pos[0], d1 = stt.position[1] - pos[1], d2 = stt.position[2] - pos[2];
        CHECK(st == SA1_OK && stt.struct_size == sizeof stt && d0 * d0 + d1 * d1 + d2 * d2 < 16.0f * 16.0f,
              "tick (game code linked): %s, one pair, Knuckles at (%g, %g, %g) in host space, start (%g, %g, %g)",
              sa1g_status_string(st), stt.position[0], stt.position[1], stt.position[2], pos[0], pos[1], pos[2]);
    } else {
        CHECK(st == SA1_ERR_NOT_IMPLEMENTED && stt.struct_size == sizeof stt && stt.position[0] == 0.0f,
              "tick: %s without the step's game code (state cleared)", sa1g_status_string(st));
    }
    if (w != NULL) {
        sa1g_world_event_push(w, SA1_EVENT_DEATH, 0, 0, sa1pos, 7u);
        st = sa1g_character_world_poll_events(&c, w, ev, 4, &n);
        CHECK(st == SA1_OK && n == 1 && ev[0].type == SA1_EVENT_DEATH && ev[0].arg == 7u && ev[0].position[0] == 4.0f
              && ev[0].position[1] == -12.0f && ev[0].position[2] == 8.0f,
              "events in host space: death at SA1 (1, 2, 3) -> (%g, %g, %g)", ev[0].position[0], ev[0].position[1],
              ev[0].position[2]);
    }
    {
        sa1g_world *we = NULL;
        st = sa1g_character_world_create(&c, SA1_CHAR_EGGMAN, NULL, 0.0f, &we);
    }
    CHECK(st == SA1_ERR_UNSUPPORTED, "Eggman worlds are out of scope (D14): %s", sa1g_status_string(st));
    sa1g_character_world_destroy(&c, w);
    sa1g_character_world_destroy(&c, w2);
    sa1g_wctx_free(&c);
}

int main(int argc, char **argv)
{
    static sa1g_shared shared;
    static sa1g_world w, w2, w3;
    static sa1g_image img;
    sa1_context *ctx = NULL;
    sa1_host host;
    const char *gdi = getenv("SA1_TEST_GDI");
    const char *rom_path = argc > 1 ? argv[1] : "-";
    const char *ref_prefix = argc > 2 ? argv[2] : "-";
    int have_seq, i;
    probe p;
    sa1_status st;
    (void)step_args;
    sa1_host_defaults(&host);
    host.log = log_cb;
    host.log_level = SA1_LOG_WARN;
    if (sa1_init(&host, &ctx) != SA1_OK) {
        printf("FAIL: sa1_init\n");
        return 1;
    }
    printf("libsa1 world tests: %d-bit, game code: %s\n", (int)(sizeof(void *) * 8), sa1g_game_info());
    if (test_image(ctx, gdi, rom_path, &img) != 0) {
        sa1_shutdown(ctx);
        return 1;
    }
    if (sa1g_shared_create(&shared) != 0 || sa1g_world_create(&w, &shared) != 0
        || sa1g_world_apply_image(&w, &img) != SA1_OK) {
        printf("FAIL: world creation\n");
        return 1;
    }
    printf("world: arena %p (16 MB, guards %u KB), ext %p, collision %p, seg[0x13] %p, seg[0x1F] %p\n", (void *)w.ram,
           SA1G_GUARD_SIZE / 1024u, (void *)w.ext, (void *)shared.col, (void *)w.seg[0x13], (void *)w.seg[0x1F]);
    test_world_image(&w, &img);

    have_seq = game_has_sequence();
    if (have_seq) {
        test_sequence(&w, &img, ref_prefix);
    } else {
        SKIP("lowered call sequence (game code: %s)", sa1g_game_info());
    }

    if (sa1g_world_create(&w2, &shared) != 0 || sa1g_world_create(&w3, &shared) != 0
        || sa1g_world_apply_image(&w2, &img) != SA1_OK || sa1g_world_apply_image(&w3, &img) != SA1_OK) {
        printf("FAIL: second / third world\n");
        return 1;
    }
    test_two_worlds(&w2, &w3, &shared, have_seq && g_snap[2] != NULL);
    test_faults(&w3, &shared);
    test_tick(&w3);
    test_api(ctx, gdi);
    st = sa1g_world_apply_image(&w3, &img);
    CHECK(st == SA1_OK && !w3.dead && w3.steps == 0 && *(uint32_t *)(void *)(w3.ram + 0xF20) == 0u,
          "re-applying the image rebuilds a used world");

    sa1g_world_destroy(&w);
    sa1g_world_destroy(&w2);
    sa1g_world_destroy(&w3);
    sa1g_shared_destroy(&shared);
    /* the process state (trap reservations, fault handler) is released with the last world and
     * comes back with the next one */
    i = sa1g_world_create(&w, NULL);
    memset(&p, 0, sizeof p);
    p.g = 0x73FD2AB8u;
    CHECK(i == 0 && run_probe(&w, probe_read, &p, NULL) == SA1G_FAULT_NEGTRAP && w.seg[SA1G_SEG_COL0] != 0,
          "after the last world was destroyed a new world gets traps and the fault handler again");
    sa1g_world_destroy(&w);
    for (i = 0; i < 3; i++) {
        free(g_snap[i]);
    }
    sa1g_image_free(&img);
    sa1_shutdown(ctx);
    printf("%s: %d failure(s), %d skipped\n", g_fail ? "FAIL" : "PASS", g_fail, g_skip);
    return g_fail ? 1 : 0;
}
#endif
