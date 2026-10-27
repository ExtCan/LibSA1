/*
 * t_diff.c - differential test: original function (interpreted from the
 * user's 1ST_READ.BIN) vs. the natively compiled decompiled C, on identical
 * memory and arguments.
 *
 *   sa1h diff <func|file.c|all> [iters] [seed] [--args a,b,..] [--ulps N] [--budget N] [-v]
 *
 *   --args gives fixed arguments (ints/pointers as numbers, floats as 1.5f);
 *   the scratch area and BSS inputs still vary per iteration.
 *
 * Per iteration: restore the baseline RAM image, write the inputs (random
 * structured data in a scratch area, random values in the BSS variables the
 * function's source file references, random arguments per the signature),
 * run the original with sh4_invoke, record R0/FR0, the fault status, the
 * hardware/store-queue event log and the RAM image; restore the inputs; run
 * the native function through its generated adapter (same guest registers);
 * compare.  Callees the native code reaches through thunks run the same
 * original code in the interpreter, so their effects are compared as well.
 *
 * Ignored differences: the guest stack area (frames differ by design) and
 * words where the native side stored the native address of a function whose
 * guest address the original stored.  Float words/results that differ by at
 * most --ulps units in the last place (default 4) count as "approx".
 */
#include "harness.h"
#include "sh4_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH 0x8CF00000u
#define SCRATCH_SIZE 0x10000u
#define STACK_LO 0x8C00B000u
#define STACK_HI 0x8C010000u
#define BSS_START 0x8C67C680u
#define EVCAP 8192
#define MAXARGS 24

typedef struct {
    int fault;
    unsigned null_touched;
    uint32_t r0, f0;
    size_t nev;
    char msg[200];
} run_res;

typedef struct {
    int iters, exact, approx, bothfault, mismatch, faultdiff;
    char first[600];
} fn_stats;

static uint8_t *S, *INP, *A;
static sh4_event evA[EVCAP], evB[EVCAP];
static int verbose;
static int ulps_tol = 4;
static const char *given_args;     /* --args a,b,c: fixed arguments (12, 0x8CF00000, 1.5f) */
static int warm;                   /* --warm: baseline after the game's own init (t_boot.c) */
static uint64_t LIMIT = 4000000;

static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static uint32_t name_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static uint32_t struct_word(void)
{
    uint32_t r = harness_rand(), v;
    switch (r & 7) {
    case 0: case 1: v = SCRATCH + ((harness_rand() % SCRATCH_SIZE) & ~3u); break;
    case 2: v = 0; break;
    case 3: v = (r >> 8) & 0xFF; break;
    case 4: { float f = (float)((int32_t)(harness_rand() % 20000) - 10000) / 10.0f; memcpy(&v, &f, 4); break; }
    case 5: v = (r >> 8) & 7; break;
    default: v = harness_rand() & 0xFFFF; break;
    }
    return v;
}

static float rnd_float(void)
{
    static const float sp[] = { 0.0f, 1.0f, -1.0f, 0.5f, 100.0f, -100.0f, 2.0f, 10.0f };
    uint32_t r = harness_rand();
    if ((r & 7) == 0)
        return sp[(r >> 3) & 7];
    return (float)((int32_t)(harness_rand() % 2000000) - 1000000) / 1000.0f;
}

static uint32_t rnd_arg(const char *t)
{
    uint32_t r = harness_rand();
    if (!strcmp(t, "p"))
        return (r % 10 == 0) ? 0 : SCRATCH + ((harness_rand() % (SCRATCH_SIZE - 0x1000)) & ~3u);
    if (!strcmp(t, "f32") || !strcmp(t, "f64"))
        return f2u(rnd_float());
    if (!strcmp(t, "i8") || !strcmp(t, "u8"))
        return sh4_extend(t, harness_rand());
    switch (r & 7) {
    case 0: case 1: case 2: return (harness_rand() & 15);
    case 3: return sh4_extend(t, harness_rand());
    case 4: case 5: return sh4_extend(t, (uint32_t)((int32_t)(harness_rand() % 200) - 100));
    default: return sh4_extend(t, harness_rand() & 0xFF);
    }
}

/* writes the inputs of iteration `it` (deterministic) and returns the args */
static int apply_inputs(const sh4_native_fn *nf, int it, uint32_t seed, char params[MAXARGS][8], int np,
                        sh4_arg *args)
{
    int i;
    uint32_t a;
    harness_srand(seed * 7919u + (uint32_t)it * 104729u + name_hash(nf->name));
    for (a = 0; a < SCRATCH_SIZE; a += 4)
        sh4_write32(SCRATCH + a, struct_word());
    if (it % 2 == 1) {
        for (i = 0; i < sh4_native_data_count; i++) {
            const sh4_native_data *d = &sh4_native_datas[i];
            uint32_t k, gap = d->gap > 256 ? 256 : d->gap;
            if (strcmp(d->file, nf->file) || d->addr < BSS_START || !sh4_is_ram(d->addr))
                continue;
            for (k = 0; k + 4 <= gap && ((d->addr + k) & 3) == 0; k += 4)
                sh4_write32(d->addr + k, (harness_rand() & 3) ? (harness_rand() & 63) : struct_word());
            for (; k < gap; k++)
                sh4_write8(d->addr + k, (uint8_t)(harness_rand() & 7));
        }
    }
    for (i = 0; i < np; i++) {
        args[i].kind = (!strcmp(params[i], "f32") || !strcmp(params[i], "f64")) ? 'f' : 'i';
        args[i].v = rnd_arg(params[i]);
    }
    if (given_args) {
        const char *s = given_args;
        for (i = 0; i < np && *s; i++) {
            char tok[64];
            size_t l = strcspn(s, ",");
            if (l >= sizeof tok)
                l = sizeof tok - 1;
            memcpy(tok, s, l);
            tok[l] = 0;
            s += l + (s[l] == ',');
            if (args[i].kind == 'f')
                args[i].v = f2u((float)atof(tok));
            else
                args[i].v = sh4_extend(params[i], (uint32_t)strtoul(tok, NULL, 0));
        }
    }
    return np;
}

static void run_original(const sh4_native_fn *nf, const sh4_arg *args, int n, run_res *r)
{
    float f0 = 0.0f;
    sh4_evlog_start(evA, EVCAP);
    SH4.ilimit = SH4.icount + LIMIT;
    r->null_touched = sh4_null_touched;
    r->fault = sh4_invoke(nf->guest, args, n, &r->r0, &f0);
    r->null_touched = sh4_null_touched - r->null_touched;
    SH4.ilimit = 0;
    r->nev = sh4_evlog_stop();
    r->f0 = f2u(f0);
    snprintf(r->msg, sizeof r->msg, "%s", r->fault ? SH4.fault.msg : "");
}

/* Zero the host stack below the caller: the original runs on a guest stack
 * that starts out zeroed (restored baseline), so a native local that the
 * decompiled code reads uninitialised -- as the original does -- reads 0
 * here too instead of garbage from earlier tests. */
static __attribute__((noinline)) void scrub_native_stack(void)
{
    volatile uint32_t buf[16384];
    size_t i;
    for (i = 0; i < sizeof buf / sizeof buf[0]; i++)
        buf[i] = 0;
}

static void run_native(const sh4_native_fn *nf, const sh4_arg *args, int n, run_res *r)
{
    sh4_guard g;
    int ni = 0, nfl = 0, ns = 0, i;
    uint32_t stackv[MAXARGS];
    for (i = 0; i < n; i++) {
        if (args[i].kind == 'f') {
            if (nfl < 8) SH4.fr.u[4 + nfl++] = args[i].v;
            else stackv[ns++] = args[i].v;
        } else {
            if (ni < 4) SH4.r[4 + ni++] = args[i].v;
            else stackv[ns++] = args[i].v;
        }
    }
    SH4.r[15] = SH4.stack_top - 4u * (uint32_t)ns;
    for (i = 0; i < ns; i++)
        sh4_write32(SH4.r[15] + 4u * (uint32_t)i, stackv[i]);
    sh4_evlog_start(evB, EVCAP);
    SH4.ilimit = SH4.icount + LIMIT;
    sh4_fpu_sync_host(&SH4);
    r->fault = 0;
    r->msg[0] = 0;
    scrub_native_stack();
    if (SH4_TRY(g)) {
        nf->adapter(&SH4);
        sh4_guard_pop(&g);
    } else {
        sh4_guard_pop(&g);
        r->fault = SH4.fault.kind;
        snprintf(r->msg, sizeof r->msg, "%s", SH4.fault.msg);
    }
    sh4_host_fp_game_mode();
    SH4.ilimit = 0;
    r->nev = sh4_evlog_stop();
    r->r0 = SH4.r[0];
    r->f0 = SH4.fr.u[0];
    SH4.r[15] = SH4.stack_top;
}

static int64_t ulp_dist(uint32_t a, uint32_t b)
{
    int64_t x = (a & 0x80000000u) ? -(int64_t)(a & 0x7FFFFFFFu) : (int64_t)a;
    int64_t y = (b & 0x80000000u) ? -(int64_t)(b & 0x7FFFFFFFu) : (int64_t)b;
    return x > y ? x - y : y - x;
}

/* a normal float that is not a guest RAM pointer */
static int floatish(uint32_t v)
{
    uint32_t e = (v >> 23) & 0xFF;
    return v == 0 || (e != 0 && e != 255 && !sh4_is_ram(v));
}

static void describe(char *buf, size_t cap, uint32_t addr)
{
    uint32_t off = 0;
    const char *n = sh4_sym_near(addr, &off);
    if (n && off < 0x10000)
        snprintf(buf, cap, "%08X (%s+0x%X)", addr, n, off);
    else
        snprintf(buf, cap, "%08X", addr);
}

/* 0 exact, 1 approx, 2 mismatch */
static int compare(const sh4_native_fn *nf, const char *ret, const run_res *ra, const run_res *rb, char *why, size_t cap)
{
    uint8_t *ram = sh4_ram();
    size_t off;
    int approx = 0, diffs = 0;
    why[0] = 0;
    if (!strcmp(ret, "f32") || !strcmp(ret, "f64")) {
        if (ra->f0 != rb->f0) {
            if (ulp_dist(ra->f0, rb->f0) <= ulps_tol ||
                ((ra->f0 & 0x7FFFFFFFu) > 0x7F800000u && (rb->f0 & 0x7FFFFFFFu) > 0x7F800000u)) {
                approx = 1;
            } else {
                snprintf(why, cap, "FR0 %08X (%g) vs native %08X (%g)", ra->f0, (double)u2f(ra->f0), rb->f0,
                         (double)u2f(rb->f0));
                return 2;
            }
        }
    } else if (strcmp(ret, "v")) {
        uint32_t x = sh4_extend(ret, ra->r0), y = sh4_extend(ret, rb->r0);
        if (!strcmp(ret, "p"))
            y = sh4_canon_code_ptr(y);
        if (x != y) {
            snprintf(why, cap, "R0 %08X vs native %08X", x, y);
            return 2;
        }
    }
    for (off = 0; off < SH4_RAM_SIZE; off += 4096) {
        size_t w;
        if (!memcmp(ram + off, A + off, 4096))
            continue;
        for (w = 0; w < 4096; w += 4) {
            uint32_t a, b, addr = SH4_RAM_P1 + (uint32_t)(off + w);
            memcpy(&a, A + off + w, 4);
            memcpy(&b, ram + off + w, 4);
            if (a == b || (addr >= STACK_LO && addr < STACK_HI))
                continue;
            if (a >= STACK_LO && a < STACK_HI && b >= STACK_LO && b < STACK_HI)
                continue;       /* a saved guest stack address: depth differs by design */
            if (sh4_canon_code_ptr(b) == a)
                continue;
            /* a saved return address: native callers reach guest code with
             * PR = SH4_SENTINEL */
            if (b == SH4_SENTINEL && ((a >= 0x8C010000u && a < 0x8C111194u) || (a >= 0x8C5FBB60u && a < 0x8C66C2A8u)))
                continue;
            /* NaN encodings differ (SH-4 default qNaN 7FBFFFFF, x86 FFC00000) */
            if ((a & 0x7FFFFFFFu) > 0x7F800000u && (b & 0x7FFFFFFFu) > 0x7F800000u) {
                approx = 1;
                continue;
            }
            /* the harness's own return sentinel, stored by the original as a
             * caller return address (no equivalent on the native side) */
            if (a == SH4_SENTINEL)
                continue;
            if (floatish(a) && floatish(b) && ulp_dist(a, b) <= ulps_tol) {
                approx = 1;
                continue;
            }
            if (diffs++ == 0) {
                char d[96];
                describe(d, sizeof d, addr);
                snprintf(why, cap, "RAM %s: original %08X, native %08X", d, a, b);
            }
        }
    }
    if (diffs) {
        size_t l = strlen(why);
        snprintf(why + l, cap - l, " (%d words differ)", diffs);
        return 2;
    }
    if (ra->nev != rb->nev) {
        snprintf(why, cap, "hardware event count %u vs native %u", (unsigned)ra->nev, (unsigned)rb->nev);
        return 2;
    } else {
        size_t i, n = ra->nev < EVCAP ? ra->nev : EVCAP;
        for (i = 0; i < n; i++) {
            if (evA[i].type != evB[i].type || evA[i].addr != evB[i].addr || evA[i].val != evB[i].val) {
                if (getenv("DIFF_EVENTS")) {
                    size_t j;
                    for (j = 0; j < n; j++)
                        printf("      ev %3u: %d %08X=%08X pc %08X | %d %08X=%08X pc %08X%s\n", (unsigned)j, evA[j].type,
                               evA[j].addr, evA[j].val, evA[j].pc, evB[j].type, evB[j].addr, evB[j].val, evB[j].pc,
                               (evA[j].val != evB[j].val || evA[j].addr != evB[j].addr) ? "  <==" : "");
                }
                snprintf(why, cap, "hardware event %u: %d %08X=%08X vs native %d %08X=%08X", (unsigned)i,
                         evA[i].type, evA[i].addr, evA[i].val, evB[i].type, evB[i].addr, evB[i].val);
                return 2;
            }
        }
    }
    (void)nf;
    return approx;
}
/* ---------------------------------------------------------------- */
static uint8_t in_set[SH4_RAM_SIZE >> 12], a_set[SH4_RAM_SIZE >> 12];
static sh4_cpu tmpl;

static void cpu_fresh(void)
{
    uint64_t ic = SH4.icount;
    int trace = SH4.trace;
    memcpy(&SH4, &tmpl, sizeof SH4);
    sh4_hw_reset();
    SH4.icount = ic;
    SH4.trace = trace;
}

static void restore_baseline(void)
{
    size_t p;
    uint8_t *ram = sh4_ram();
    for (p = 0; p < sizeof a_set; p++) {
        if (in_set[p] || a_set[p] || sh4_ram_dirty[p] || memcmp(ram + (p << 12), S + (p << 12), 4096))
            memcpy(ram + (p << 12), S + (p << 12), 4096);
        in_set[p] = a_set[p] = 0;
        sh4_ram_dirty[p] = 0;
    }
}

/* expected memory after the original: A where the original (or the inputs)
 * touched a page, else the baseline */
static int ram_matches_expected(void)
{
    size_t p;
    uint8_t *ram = sh4_ram();
    for (p = 0; p < sizeof a_set; p++) {
        const uint8_t *exp = a_set[p] ? A + (p << 12) : S + (p << 12);
        if (memcmp(ram + (p << 12), exp, 4096))
            return 0;
    }
    return 1;
}

static void format_args(char *buf, size_t cap, const sh4_arg *args, int n)
{
    int i;
    size_t l = 0;
    buf[0] = 0;
    for (i = 0; i < n && l + 24 < cap; i++) {
        if (args[i].kind == 'f')
            l += (size_t)snprintf(buf + l, cap - l, "%s%gf", i ? ", " : "", (double)u2f(args[i].v));
        else
            l += (size_t)snprintf(buf + l, cap - l, "%s0x%X", i ? ", " : "", args[i].v);
    }
}

static void test_fn(const sh4_native_fn *nf, int iters, uint32_t seed, fn_stats *st)
{
    char ret[8], params[MAXARGS][8];
    int np = sh4_sig_parse(nf->sig, ret, params);
    sh4_arg args[MAXARGS];
    int it;
    memset(st, 0, sizeof *st);
    if (np < 0)
        np = 0;
    restore_baseline();
    for (it = 0; it < iters; it++) {
        run_res ra, rb;
        char why[400], av[200];
        int n, k;
        size_t p;
        uint8_t *ram = sh4_ram();

        cpu_fresh();
        n = apply_inputs(nf, it, seed, params, np, args);
        for (p = 0; p < sizeof in_set; p++) {
            if (sh4_ram_dirty[p]) {
                memcpy(INP + (p << 12), ram + (p << 12), 4096);
                in_set[p] = 1;
                sh4_ram_dirty[p] = 0;
            }
        }
        run_original(nf, args, n, &ra);
        for (p = 0; p < sizeof a_set; p++) {
            if (sh4_ram_dirty[p] || in_set[p]) {
                memcpy(A + (p << 12), ram + (p << 12), 4096);
                a_set[p] = 1;
            }
            if (sh4_ram_dirty[p]) {
                memcpy(ram + (p << 12), (in_set[p] ? INP : S) + (p << 12), 4096);
                sh4_ram_dirty[p] = 0;
            }
        }
        cpu_fresh();
        run_native(nf, args, n, &rb);
        st->iters++;
        format_args(av, sizeof av, args, n);
        if (ra.fault || rb.fault) {
            /* inconclusive: both faulted; the original faulted on a wild
             * pointer / ran out of budget; the native code dereferenced
             * NULL where the original read the (zero) boot ROM area */
            int incon = (ra.fault && rb.fault) ||
                        (ra.fault && !rb.fault && (ra.fault == SH4_FAULT_MEM || ra.fault == SH4_FAULT_ALIGN ||
                                                   ra.fault == SH4_FAULT_TIMEOUT)) ||
                        (rb.fault == SH4_FAULT_NATIVE && SH4.fault.addr < 0x10000u && ra.null_touched) ||
                        (rb.fault == SH4_FAULT_NATIVE && !ra.fault && ra.nev > 0 && SH4.fault.addr >= 0x10000u &&
                         !sh4_is_ram(SH4.fault.addr));
            if (incon) {
                st->bothfault++;
            } else {
                st->faultdiff++;
                if (!st->first[0])
                    snprintf(st->first, sizeof st->first, "iter %d (%s): original %s%s%s / native %s%s%s", it, av,
                             ra.fault ? sh4_fault_name(ra.fault) : "ok", ra.fault ? ": " : "", ra.msg,
                             rb.fault ? sh4_fault_name(rb.fault) : "ok", rb.fault ? ": " : "", rb.msg);
            }
            if (verbose)
                printf("    iter %2d: original %s, native %s\n", it, sh4_fault_name(ra.fault), sh4_fault_name(rb.fault));
            /* both sides spin in a hardware wait loop: further iterations
             * would only burn the budget again */
            if (ra.fault == SH4_FAULT_TIMEOUT && rb.fault == SH4_FAULT_TIMEOUT && st->bothfault == st->iters &&
                st->iters >= 2) {
                restore_baseline();
                break;
            }
        } else {
            k = ram_matches_expected() ? -1 : 0;
            {
                /* build the full expected image in A for the detailed compare */
                for (p = 0; p < sizeof a_set; p++)
                    if (!a_set[p])
                        memcpy(A + (p << 12), S + (p << 12), 4096);
                k = compare(nf, ret, &ra, &rb, why, sizeof why);
            }
            if (k == 0)
                st->exact++;
            else if (k == 1)
                st->approx++;
            else {
                st->mismatch++;
                if (!st->first[0])
                    snprintf(st->first, sizeof st->first, "iter %d (%s): %s", it, av, why);
            }
            if (verbose)
                printf("    iter %2d (%s): %s %s\n", it, av, k == 0 ? "exact" : k == 1 ? "approx" : "MISMATCH",
                       k == 2 ? why : "");
        }
        restore_baseline();
    }
}

static int match_target(const sh4_native_fn *nf, const char *t)
{
    if (!strcmp(t, "all"))
        return 1;
    if (!strcmp(t, nf->name) || !strcmp(t, nf->file))
        return 1;
    if (sh4_sym_addr(t) && sh4_sym_addr(t) == nf->guest)
        return 1;
    return 0;
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* 'diff all': one child process per source file, so that a native function
 * that corrupts host memory (wild pointers from random inputs, a struct
 * size mismatch that makes the original write past a native stack object)
 * only takes down its own file's run. */
static char reported[512][64];
static int nreported;
/* native code has no instruction budget: a decompiled function that loops
 * forever on random inputs would hang `diff all`, so each child gets a
 * wall-clock limit (--child-timeout SECONDS, default 120) */
static unsigned long child_timeout_ms = 120000;
#define CHILD_TIMEOUT_CODE 0x7130E0u

/* run one child: `sa1h diff <target> ...`, echo its lines, add up its summary;
 * returns the child's exit code */
static unsigned long run_child(const char *exe, const char *tmp, const char *target, int iters, uint32_t seed, int argc,
                               char **argv, int tot[5])
{
    char cmd[4096], line[1024];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    HANDLE h;
    DWORD code = 99;
    FILE *f;
    int j;
    snprintf(cmd, sizeof cmd, "\"%s\" --root \"%s\" diff %s %d %u --inproc", exe, g_root, target, iters, seed);
    for (j = 0; j < argc; j++) {
        if (!strcmp(argv[j], "-v") || !strcmp(argv[j], "--ulps") || !strcmp(argv[j], "--budget") ||
            !strcmp(argv[j], "--warm") ||
            (j > 0 && (!strcmp(argv[j - 1], "--ulps") || !strcmp(argv[j - 1], "--budget")))) {
            strncat(cmd, " ", sizeof cmd - strlen(cmd) - 1);
            strncat(cmd, argv[j], sizeof cmd - strlen(cmd) - 1);
        }
    }
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    h = CreateFileA(tmp, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h;
    si.hStdError = h;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    if (CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        if (WaitForSingleObject(pi.hProcess, child_timeout_ms) == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, CHILD_TIMEOUT_CODE);
            WaitForSingleObject(pi.hProcess, 5000);
        }
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    CloseHandle(h);
    f = fopen(tmp, "r");
    while (f && fgets(line, sizeof line, f)) {
        int a, b, c2, d, e;
        char verdict[16], name[64];
        if (sscanf(line, "diff: %d pass, %d approx, %d fail, %d always-faulting, %d skipped", &a, &b, &c2, &d, &e) == 5) {
            tot[0] += a; tot[1] += b; tot[2] += c2; tot[3] += d; tot[4] += e;
            continue;
        }
        if (!strncmp(line, "diff: warm", 10))
            continue;
        if (sscanf(line, "%15s %63s", verdict, name) == 2 && nreported < 512 &&
            (!strcmp(verdict, "PASS") || !strcmp(verdict, "FAIL") || !strcmp(verdict, "APPROX") ||
             !strcmp(verdict, "FAULT") || !strcmp(verdict, "SKIP")))
            snprintf(reported[nreported++], 64, "%s", name);
        fputs(line, stdout);
    }
    if (f)
        fclose(f);
    return code;
}

static int was_reported(const char *name)
{
    int i;
    for (i = 0; i < nreported; i++)
        if (!strcmp(reported[i], name))
            return 1;
    return 0;
}

static int diff_all_isolated(int iters, uint32_t seed, int argc, char **argv)
{
    char exe[1024], tmp[1024];
    int i, j, tot[5] = { 0, 0, 0, 0, 0 }, crashes = 0;
    GetModuleFileNameA(NULL, exe, sizeof exe);
    GetTempPathA(sizeof tmp, tmp);
    {
        char nm[64];
        snprintf(nm, sizeof nm, "sa1h_diff_%lu.txt", (unsigned long)GetCurrentProcessId());
        strncat(tmp, nm, sizeof tmp - strlen(tmp) - 1);
    }
    for (i = 0; i < sh4_native_fn_count; i++) {
        const char *file = sh4_native_fns[i].file;
        unsigned long code;
        int seen = 0;
        for (j = 0; j < i; j++)
            if (!strcmp(sh4_native_fns[j].file, file))
                seen = 1;
        if (seen)
            continue;
        code = run_child(exe, tmp, file, iters, seed, argc, argv, tot);
        if (code > 1) {
            /* the file's child died: finish its functions one process each */
            if (code == CHILD_TIMEOUT_CODE)
                printf("TIMEOUT %s: child still running after %lu s (native code does not stop at the instruction "
                       "budget); testing its remaining functions one by one\n", file, child_timeout_ms / 1000);
            else
                printf("CRASH   %s: child exited with code 0x%lX; testing its remaining functions one by one\n", file,
                       code);
            for (j = 0; j < sh4_native_fn_count; j++) {
                const sh4_native_fn *nf = &sh4_native_fns[j];
                unsigned long c2;
                if (strcmp(nf->file, file) || was_reported(nf->name) || !nf->adapter || !nf->guest)
                    continue;
                c2 = run_child(exe, tmp, nf->name, iters, seed, argc, argv, tot);
                if (c2 == CHILD_TIMEOUT_CODE) {
                    printf("HANG    %-26s %-18s %-22s the native code did not return within %lu s\n", nf->name,
                           nf->file, nf->sig, child_timeout_ms / 1000);
                    crashes++;
                } else if (c2 > 1) {
                    printf("CRASH   %-26s %-18s %-22s the native code crashed the harness (exit code 0x%lX)\n",
                           nf->name, nf->file, nf->sig, c2);
                    crashes++;
                }
            }
        }
        fflush(stdout);
    }
    printf("diff: %d pass, %d approx, %d fail, %d always-faulting, %d skipped, %d crashed/hung functions\n", tot[0], tot[1], tot[2],
           tot[3], tot[4], crashes);
    return (tot[2] || crashes) ? 1 : 0;
}
#endif

int cmd_diff(int argc, char **argv)
{
    const char *target = NULL;
    int iters = 20, i, npos = 0;
    uint32_t seed = 1;
    int npass = 0, napprox = 0, nfail = 0, nfault = 0, nskip = 0;
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-v"))
            verbose = 1;
        else if (!strcmp(argv[i], "--inproc"))
            ;
        else if (!strcmp(argv[i], "--warm"))
            warm = 1;
        else if (!strcmp(argv[i], "--args") && i + 1 < argc)
            given_args = argv[++i];
        else if (!strcmp(argv[i], "--budget") && i + 1 < argc)
            LIMIT = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--ulps") && i + 1 < argc)
            ulps_tol = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--child-timeout") && i + 1 < argc) {
#ifdef _WIN32
            child_timeout_ms = (unsigned long)atoi(argv[i + 1]) * 1000u;
#endif
            i++;
        }
        else if (npos == 0) { target = argv[i]; npos++; }
        else if (npos == 1) { iters = atoi(argv[i]); npos++; }
        else if (npos == 2) { seed = (uint32_t)strtoul(argv[i], NULL, 0); npos++; }
    }
#ifdef _WIN32
    if (target && !strcmp(target, "all")) {
        int inproc = 0;
        for (i = 0; i < argc; i++)
            if (!strcmp(argv[i], "--inproc"))
                inproc = 1;
        if (!inproc)
            return diff_all_isolated(iters, seed, argc, argv);
    }
#endif
    if (!target) {
        fprintf(stderr, "usage: diff <func|file.c|all> [iters] [seed] [--warm] [--args a,b,..] [--ulps N] [--budget insns]\n"
                        "            [--child-timeout SECONDS] [-v]\n");
        return 2;
    }
    if (harness_load_rom())
        return 1;
    hle_install_defaults(0);
    sh4_route_native(0);
    sh4_strict_io = 1;            /* wild pointers fault instead of reading 'hardware' */
    sh4_null_page_faults = 0;     /* NULL+n reads 0 (boot ROM), counted in sh4_null_touched */
    if (warm) {
        if (boot_warm_state(2)) {
            fprintf(stderr, "diff: warm-up (game init) failed\n");
            return 1;
        }
        sh4_strict_io = 1;
        /* the guest stack is dead memory now: clear what the warm-up left */
        memset(sh4_host_ptr(STACK_LO), 0, SH4.stack_top - STACK_LO);
        if (!strcmp(target, "all") || verbose)
            printf("diff: warm baseline (view/heap/task init, player loader, 2 logic frames; %d auto-stubs)\n",
                   boot_stub_count());
    }
    S = (uint8_t *)malloc(SH4_RAM_SIZE);
    INP = (uint8_t *)malloc(SH4_RAM_SIZE);
    A = (uint8_t *)malloc(SH4_RAM_SIZE);
    memcpy(S, sh4_ram(), SH4_RAM_SIZE);
    memset(sh4_ram_dirty, 0, sizeof sh4_ram_dirty);
    tmpl = SH4;
    for (i = 0; i < sh4_native_fn_count; i++) {
        const sh4_native_fn *nf = &sh4_native_fns[i];
        fn_stats st;
        const char *verdict;
        if (!match_target(nf, target))
            continue;
        if (!nf->adapter || !nf->guest) {
            printf("SKIP    %-26s %-18s %s (%s)\n", nf->name, nf->file, nf->sig,
                   nf->guest ? "signature not supported" : "no guest address");
            nskip++;
            continue;
        }
        if (verbose)
            printf("%s %s %s\n", nf->name, nf->file, nf->sig);
        test_fn(nf, iters, seed, &st);
        if (st.mismatch || st.faultdiff) {
            verdict = "FAIL";
            nfail++;
        } else if (st.exact + st.approx == 0) {
            verdict = "FAULT";
            nfault++;
        } else if (st.approx) {
            verdict = "APPROX";
            napprox++;
        } else {
            verdict = "PASS";
            npass++;
        }
        printf("%-7s %-26s %-18s %-22s exact %2d approx %2d inconclusive %2d mismatch %2d\n", verdict, nf->name, nf->file,
               nf->sig, st.exact, st.approx, st.bothfault, st.mismatch + st.faultdiff);
        if (st.first[0])
            printf("        first difference: %s\n", st.first);
    }
    printf("diff: %d pass, %d approx, %d fail, %d always-faulting, %d skipped\n", npass, napprox, nfail, nfault, nskip);
    memcpy(sh4_ram(), S, SH4_RAM_SIZE);
    free(S);
    free(INP);
    free(A);
    return nfail ? 1 : 0;
}