/*
 * parallel_test.c - round 0.9.8 (098-lib-threads; sadc docs/research/gsa1_098_plan.md 6.4, gsa1_096_plan.md 6.3): the parallel-tick
 * contract SA1_API_PARALLEL (libsa1.h) and the race it fixes.
 *
 *   parallel_test [--only PART]... [--worlds N] [--frames F] [--threads a,b,c] [--contexts C] [--seconds S] [--scene flat|city]
 *                 [--stack-kb K] GDI
 *
 * Parts (default: hash, errors, fault, and follow when the library has followers):
 *   hash    N Knuckles and N Tails of ONE context (distinct characters, N = 99 on 64-bit, 32 on x86: review R.10), ticked by 1, 2,
 *           4, 8 and 20 threads, a frame = one job per world taken from an atomic index (tick, state_ex, render), joined after
 *           every frame: every world's hash (sa1_state_ex after every tick + the render's positions) equal to the 1-thread run.
 *           SPEED lines give the wall time against 1 thread (T2's bound: 8 threads at most 0.25 x); DETERMINISM lines, over the
 *           first 32 worlds, must be equal on x86 and x64. Then 32 worlds of the six kinds that tick, in turn.
 *   errors  sa1_last_error per thread: 8 threads writing their own messages (each reads its own); a thread that wrote none reads
 *           the newest of any thread; one thread alone behaves as 0.3 did, a direct "quiet" write included.
 *   fault   20 characters on 20 threads, fixed owners; on thread 3 one world is driven into a fatal game error (A sa1g_fatal, B the
 *           real fatal stub 8C011D46, C an access violation in game text on a trap address): only that world reports it, its
 *           event is FAULT / UNIMPLEMENTED, the 19 others' hashes equal their solo runs.
 *   race    C contexts on C threads with N / C worlds in all (the probe's configuration; run by tests/build_parallel.py --race,
 *           one process per run, because the failure is an access violation in sa1_fn_name outside any world: 3 of 63 and 10 of
 *           95 runs in the research, 4 of 20 and 2 of 20 on the 0.3.0 archive in this round).
 *   tight   the discriminating race: thread B calls sa1_character_prepare in a loop (1e6 calls take under 1 s: it goes on) while thread A
 *           ticks one world, for --seconds S (default 10). On the round-start archive it must fault at least once in 5 runs, after L4 never. With a
 *           library that has the front end, the main thread also opens and closes a front-end world (recheck098 4.5 item 7).
 *   follow  (needs SA1_API_FOLLOWER and a library whose followers are built; step 3): 99 Knuckles followers of one leader (one chain
 *           level; 32 on x86) and a chain of 3 levels, chain level by chain level on 1, 2, 4, 8, 20 threads: hashes equal to 1 thread.
 *           32 host-fed followers (sa1_follower_feed in the job). FT-T1: 32 followers on worker threads while the main thread ticks the leader and moves a host object between frames:
 *           identical to the serial run, no deadlock, the main thread's longest wait reported.
 *
 * Built by tests/build_parallel.py (a snapshot of the archive and its headers, or --base DIR for r098/base/lib030). Against 0.3.0's
 * headers only race and tight exist: its contract forbids the rest. With -DSA1PT_REPLAY the same file builds parallel_replay: 20
 * other worlds tick on other threads while tests/replay_test.c runs its strict replays (the outputs are diffed by the script).
 *
 * API notes for hosts (the full contract is libsa1.h's SA1_API_PARALLEL): nothing here calls anything but the six parallel calls
 * during a phase; sa1_poll_events, sa1_character_create_ex, sa1_character_destroy and the collision and host-object calls run between
 * phases, on one thread. A world exists before it is ticked in parallel (SA1_CREATE_WORLD, or one tick before).
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#endif

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_world.h"
#include "sa1g_text.h"

/* ---- threads -------------------------------------------------------------------------------------------------------- */
#ifdef _WIN32
typedef HANDLE pt_thread;
typedef DWORD pt_ret;
#define PT_API WINAPI
static size_t g_stack_bytes;            /* --stack-kb: the workers' stack (a host's pool may have small ones); 0 the default */
static int pt_start(pt_thread *t, pt_ret (PT_API *fn)(void *), void *arg)
{
    *t = CreateThread(NULL, g_stack_bytes, fn, arg, g_stack_bytes ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0, NULL);
    return *t ? 0 : -1;
}
static void pt_join(pt_thread t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
static void pt_yield(void)
{
    SwitchToThread();
}
static double pt_now_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) {
        QueryPerformanceFrequency(&f);
    }
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}
#else
typedef pthread_t pt_thread;
typedef void *pt_ret;
#define PT_API
static size_t g_stack_bytes;
static int pt_start(pt_thread *t, pt_ret (*fn)(void *), void *arg)
{
    pthread_attr_t a;
    int rc;
    pthread_attr_init(&a);
    if (g_stack_bytes) {
        pthread_attr_setstacksize(&a, g_stack_bytes);
    }
    rc = pthread_create(t, &a, fn, arg);
    pthread_attr_destroy(&a);
    return rc;
}
static void pt_join(pt_thread t)
{
    pthread_join(t, NULL);
}
static void pt_yield(void)
{
    sched_yield();
}
static double pt_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
#endif

/* a barrier that spins a little, then yields (a frame is 1-5 ms; other processes may want the cores) */
typedef struct pt_barrier {
    volatile int32_t count, gen;
    int32_t n;
} pt_barrier;

static void pt_barrier_wait(pt_barrier *b)
{
    int32_t g = __atomic_load_n(&b->gen, __ATOMIC_ACQUIRE);
    if (__atomic_add_fetch(&b->count, 1, __ATOMIC_ACQ_REL) == b->n) {
        __atomic_store_n(&b->count, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&b->gen, g + 1, __ATOMIC_RELEASE);
        return;
    }
    {
        int spins = 0;
        while (__atomic_load_n(&b->gen, __ATOMIC_ACQUIRE) == g) {
            if (++spins > 200) {
                pt_yield();
            } else {
                __asm__ volatile("pause");
            }
        }
    }
}

/* ---- checks ---------------------------------------------------------------------------------------------------------- */
static int g_fails;

static void pt_log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

#define CHECK(c, ...) do { \
        if (c) { \
            pt_log_line("  ok   "); \
        } else { \
            pt_log_line("  FAIL (line %d) ", __LINE__); \
            __atomic_add_fetch(&g_fails, 1, __ATOMIC_ACQ_REL); \
        } \
        pt_log_line(__VA_ARGS__); \
        pt_log_line("\n"); \
    } while (0)

/* ---- the scene and the scripts (followscale.c's: SA1 space, native coordinates) ----------------------------------------- */
typedef struct tbuf {
    sa1_collision_tri *t;
    uint32_t n, cap;
} tbuf;

static void tb_tri(tbuf *b, const float a[3], const float c[3], const float d[3])
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        b->cap = b->cap ? 2u * b->cap : 1024u;
        b->t = (sa1_collision_tri *)realloc(b->t, (size_t)b->cap * sizeof *b->t);
    }
    t = &b->t[b->n++];
    memcpy(t->v[0], a, 12);
    memcpy(t->v[1], c, 12);
    memcpy(t->v[2], d, 12);
    t->surface = SA1_SURFACE_SOLID;
}

static void tb_quad(tbuf *b, const float p[4][3], const float n[3])
{
    float e1[3], e2[3], c[3];
    int k;
    for (k = 0; k < 3; k++) {
        e1[k] = p[1][k] - p[0][k];
        e2[k] = p[2][k] - p[0][k];
    }
    c[0] = e1[1] * e2[2] - e1[2] * e2[1];
    c[1] = e1[2] * e2[0] - e1[0] * e2[2];
    c[2] = e1[0] * e2[1] - e1[1] * e2[0];
    if (c[0] * n[0] + c[1] * n[1] + c[2] * n[2] >= 0.0f) {
        tb_tri(b, p[0], p[1], p[2]);
        tb_tri(b, p[0], p[2], p[3]);
    } else {
        tb_tri(b, p[0], p[2], p[1]);
        tb_tri(b, p[0], p[3], p[2]);
    }
}

static void tb_floor(tbuf *b, float x0, float x1, float z0, float z1, float y, float up)
{
    float p[4][3] = { { x0, y, z0 }, { x1, y, z0 }, { x1, y, z1 }, { x0, y, z1 } };
    float n[3] = { 0.0f, up, 0.0f };
    tb_quad(b, p, n);
}

static void tb_box(tbuf *b, float x0, float x1, float y0, float y1, float z0, float z1)
{
    tb_floor(b, x0, x1, z0, z1, y1, 1.0f);
    { float p[4][3] = { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } }; float n[3] = { -1, 0, 0 }; tb_quad(b, p, n); }
    { float p[4][3] = { { x1, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, { x1, y1, z0 } }; float n[3] = { 1, 0, 0 }; tb_quad(b, p, n); }
    { float p[4][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 } }; float n[3] = { 0, 0, -1 }; tb_quad(b, p, n); }
    { float p[4][3] = { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } }; float n[3] = { 0, 0, 1 }; tb_quad(b, p, n); }
}

static void tb_ramp(tbuf *b, float x0, float x1, float h, float z0, float z1)
{
    float p[4][3] = { { x0, 0.0f, z0 }, { x1, h, z0 }, { x1, h, z1 }, { x0, 0.0f, z1 } };
    float n[3] = { -h, x1 - x0, 0.0f };
    tb_quad(b, p, n);
}

static void build_scene(tbuf *b, int city)
{
    const float ext = 6000.0f, cell = 250.0f;
    float x, z;
    int i, j;
    memset(b, 0, sizeof *b);
    for (x = -ext; x < ext; x += cell) {
        for (z = -ext; z < ext; z += cell) {
            tb_floor(b, x, x + cell, z, z + cell, 0.0f, 1.0f);
        }
    }
    if (city) {
        for (i = 0; i < 17; i++) {
            for (j = 0; j < 17; j++) {
                float cx = -5600.0f + 700.0f * (float)i + 350.0f, cz = -5600.0f + 700.0f * (float)j + 350.0f;
                float h = 60.0f + (float)((i * 7 + j * 13) % 7) * 60.0f;
                tb_box(b, cx - 60.0f, cx + 60.0f, 0.0f, h, cz - 60.0f, cz + 60.0f);
                if (((i + j) & 3) == 0) {
                    tb_ramp(b, cx - 300.0f, cx - 60.0f, h, cz - 40.0f, cz + 40.0f);
                }
            }
        }
    }
}

static void script(int gi, int f, sa1_input *in)
{
    double a = (double)gi * 2.39996 + (double)f * 0.004;
    int k = (f + gi * 13) % 150, s = (f + gi * 29) % 600, q = (f + gi * 7) % 400;
    memset(in, 0, sizeof *in);
    in->struct_size = sizeof *in;
    if (s < 540) {
        in->stick_x = (float)sin(a);
        in->stick_y = (float)cos(a);
    }
    if (k < 8 || (k >= 14 && k < 44)) {
        in->buttons |= SA1_BUTTON_JUMP;
    }
    if (q >= 200 && q < 206) {
        in->buttons |= SA1_BUTTON_ACTION;
    }
}

/* ---- context and worlds ----------------------------------------------------------------------------------------------- */
static const char *g_gdi;
static tbuf g_scene;
static volatile int32_t g_warnings, g_errors;

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    (void)msg;
    if (level >= SA1_LOG_ERROR) {
        __atomic_add_fetch(&g_errors, 1, __ATOMIC_RELAXED);
    } else if (level >= SA1_LOG_WARN) {
        __atomic_add_fetch(&g_warnings, 1, __ATOMIC_RELAXED);
    }
}

static sa1_context *ctx_make(uint32_t kinds)
{
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_context *ctx = NULL;
    sa1_status st;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    st = sa1_init(&h, &ctx);
    if (st != SA1_OK) {
        printf("sa1_init: %s\n", sa1_status_string(st));
        exit(2);
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = kinds;
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_WORLD_IMAGES;
    st = sa1_load_disc(ctx, g_gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        exit(2);
    }
    st = sa1_collision_set_static(ctx, g_scene.t, g_scene.n);
    if (SA1_FAILED(st)) {
        printf("sa1_collision_set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        exit(2);
    }
    return ctx;
}

typedef struct pt_world {
    int gi;
    sa1_context *ctx;
    sa1_character_id id;
    uint64_t hash;
    sa1_status st;
    int faulted;
    double wait_max;
} pt_world;

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

static sa1_status world_make(pt_world *w, sa1_context *ctx, int kind, int gi)
{
    sa1_character_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_SEED;
    d.position[0] = ((float)(gi % 10) - 4.5f) * 400.0f + 25.0f;
    d.position[1] = 5.0f;
    d.position[2] = ((float)(gi / 10) - 4.5f) * 400.0f + 25.0f;
    d.angle[1] = (gi * 0x2F1B) & 0xFFFF;
    d.seed = 0x1234567u + (uint32_t)gi * 2654435761u;
    w->gi = gi;
    w->ctx = ctx;
    w->hash = 1469598103934665603ull;
    w->faulted = 0;
    w->st = sa1_character_create_ex(ctx, &d, &w->id);
    if (SA1_FAILED(w->st)) {
        printf("world %d: sa1_character_create_ex: %s: %s\n", gi, sa1_status_string(w->st), sa1_last_error(ctx));
    }
    return w->st;
}

/* the hash covers what a host reads; with a follower's look at the struct in the same way */
static void hash_state_render(pt_world *w)
{
    sa1_state_ex x;
    const sa1_render *r = NULL;
    sa1_status st;
    memset(&x, 0, sizeof x);
    x.struct_size = sizeof x;
    sa1_character_state_ex(w->ctx, w->id, &x);
    w->hash = fnv(w->hash, &x, sizeof x);
    st = sa1_character_render(w->ctx, w->id, -1.0f, &r);
    if (st == SA1_OK && r != NULL) {
        if (r->positions != NULL) {
            w->hash = fnv(w->hash, r->positions, (size_t)r->num_triangles * 9u * sizeof(float));
        }
        w->hash = fnv(w->hash, &r->num_draws, sizeof r->num_draws);
    }
}

/* one host frame of one world */
static void world_frame(pt_world *w, int f)
{
    sa1_input in;
    sa1_status st;
    if (w->faulted) {
        return;
    }
    script(w->gi, f, &in);
    st = sa1_character_tick(w->ctx, w->id, &in, 1.0f / 60.0f, NULL, NULL);
    if (SA1_FAILED(st)) {
        w->st = st;
        w->faulted = 1;
        return;
    }
    hash_state_render(w);
}

/* ---- context-per-thread race (the probe's configuration) --------------------------------------------------------------- */
typedef struct race_group {
    sa1_context *ctx;
    pt_world *w;
    int nw, kind, frames;
    pt_barrier *ready, *frame;
} race_group;

static pt_ret PT_API race_main(void *arg)
{
    race_group *g = (race_group *)arg;
    int i, f;
    g->ctx = ctx_make(1u << g->kind);
    for (i = 0; i < g->nw; i++) {
        world_make(&g->w[i], g->ctx, g->kind, g->w[i].gi);
    }
    pt_barrier_wait(g->ready);
    for (f = 0; f < g->frames; f++) {
        for (i = 0; i < g->nw; i++) {
            world_frame(&g->w[i], f);
        }
        pt_barrier_wait(g->frame);
    }
    return (pt_ret)0;
}

static int part_race(int n, int contexts, int frames, int kind)
{
    pt_world *W = (pt_world *)calloc((size_t)n, sizeof *W);
    race_group *G = (race_group *)calloc((size_t)contexts, sizeof *G);
    pt_thread *T = (pt_thread *)calloc((size_t)contexts, sizeof *T);
    pt_barrier ready, fb;
    uint64_t all = 1469598103934665603ull;
    int i, bad = 0;
    double t0;
    memset(&ready, 0, sizeof ready);
    memset(&fb, 0, sizeof fb);
    ready.n = contexts + 1;
    fb.n = contexts;
    for (i = 0; i < n; i++) {
        W[i].gi = i;
    }
    for (i = 0; i < contexts; i++) {
        int a = i * n / contexts, b = (i + 1) * n / contexts;
        G[i].w = W + a;
        G[i].nw = b - a;
        G[i].kind = kind;
        G[i].frames = frames;
        G[i].ready = &ready;
        G[i].frame = &fb;
        pt_start(&T[i], race_main, &G[i]);
    }
    pt_barrier_wait(&ready);
    t0 = pt_now_ms();
    for (i = 0; i < contexts; i++) {
        pt_join(T[i]);
    }
    for (i = 0; i < n; i++) {
        all = fnv(all, &W[i].hash, sizeof W[i].hash);
        bad += W[i].faulted;
    }
    printf("RACE contexts=%d worlds=%d frames=%d wall_ms=%.1f faults=%d hash=%016llx\n", contexts, n, frames, pt_now_ms() - t0, bad,
           (unsigned long long)all);
    return bad != 0;
}

/* ---- the tight loop ---------------------------------------------------------------------------------------------------- */
typedef struct tight_b {
    sa1_context *ctx;
    volatile int32_t *stop;
    uint64_t calls;
    int32_t bad;
} tight_b;

static pt_ret PT_API tight_prepare(void *arg)
{
    tight_b *b = (tight_b *)arg;
    while (!__atomic_load_n(b->stop, __ATOMIC_ACQUIRE)) {
        sa1_status st = sa1_character_prepare(b->ctx, 1u << SA1_CHAR_KNUCKLES);
        if (st != SA1_OK) {
            b->bad++;
        }
        b->calls++;
    }
    return (pt_ret)0;
}

#if defined(SA1_API_FRONTEND)
#define PT_HAS_FRONTEND 1
#else
#define PT_HAS_FRONTEND 0
#endif

static int part_tight(double seconds, int want_frontend)
{
    sa1_context *ctx = ctx_make(1u << SA1_CHAR_KNUCKLES);
    pt_world w;
    volatile int32_t stop = 0;
    tight_b b;
    pt_thread t;
    int f = 0;
    double t0;
    uint32_t front_opens = 0;
    memset(&w, 0, sizeof w);
    memset(&b, 0, sizeof b);
    if (world_make(&w, ctx, SA1_CHAR_KNUCKLES, 0) != SA1_OK) {
        return 1;
    }
    b.ctx = ctx;
    b.stop = &stop;
    pt_start(&t, tight_prepare, &b);
    t0 = pt_now_ms();
    /* every pair enters and leaves the world: the race's window (dt 1/30: one pair per tick) */
    while (pt_now_ms() - t0 < seconds * 1000.0 && !__atomic_load_n(&w.faulted, __ATOMIC_ACQUIRE)) {
        sa1_input in;
        script(0, f++, &in);
        w.st = sa1_character_tick(ctx, w.id, &in, 1.0f / 30.0f, NULL, NULL);
        if (SA1_FAILED(w.st)) {
            printf("tick: %s: %s\n", sa1_status_string(w.st), sa1_last_error(ctx));
            w.faulted = 1;
        }
#if PT_HAS_FRONTEND
        if (want_frontend && (f % 4096) == 0) {
            sa1_frontend *fe = NULL;
            sa1_frontend_desc fd;
            memset(&fd, 0, sizeof fd);
            fd.struct_size = sizeof fd;
            fd.language = 1;
            fd.open_slots = ~0u;
            if (sa1_frontend_open(ctx, &fd, &fe) == SA1_OK && fe != NULL) {
                sa1_frontend_close(fe);
                front_opens++;
            }
        }
#endif
    }
    (void)want_frontend;
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    pt_join(t);
    printf("TIGHT seconds=%.1f ticks=%d prepare_calls=%llu bad=%d faulted=%d frontend_opens=%u\n", (pt_now_ms() - t0) / 1000.0, f,
           (unsigned long long)b.calls, (int)b.bad, w.faulted, (unsigned)front_opens);
    sa1_shutdown(ctx);
    return b.bad != 0 || w.faulted;
}

#if defined(SA1_API_PARALLEL) && !defined(SA1PT_REPLAY)
/* ---- the job pool of the test ------------------------------------------------------------------------------------------- */
typedef struct pool {
    pt_world *w;
    int n, frames, threads;
    volatile int32_t next;
    volatile int32_t stop;
    int f;
    pt_barrier go, done;
    pt_thread t[64];
    void (*job)(struct pool *p, int i);
} pool;

static void pool_drain(pool *p)
{
    for (;;) {
        int i = __atomic_fetch_add(&p->next, 1, __ATOMIC_ACQ_REL);
        if (i >= p->n) {
            return;
        }
        p->job(p, i);
    }
}

static pt_ret PT_API pool_main(void *arg)
{
    pool *p = (pool *)arg;
    for (;;) {
        pt_barrier_wait(&p->go);
        if (__atomic_load_n(&p->stop, __ATOMIC_ACQUIRE)) {
            return (pt_ret)0;
        }
        pool_drain(p);
        pt_barrier_wait(&p->done);
    }
}

static void pool_start(pool *p, int threads)
{
    int i;
    memset(&p->go, 0, sizeof p->go);
    memset(&p->done, 0, sizeof p->done);
    p->threads = threads;
    p->go.n = threads;
    p->done.n = threads;
    p->stop = 0;
    for (i = 0; i < threads - 1; i++) {
        pt_start(&p->t[i], pool_main, p);
    }
}

/* one phase: the jobs 0..n-1 on every thread, this one included, joined */
static void pool_phase(pool *p)
{
    __atomic_store_n(&p->next, 0, __ATOMIC_RELEASE);
    if (p->threads > 1) {
        pt_barrier_wait(&p->go);
    }
    pool_drain(p);
    if (p->threads > 1) {
        pt_barrier_wait(&p->done);
    }
}

static void pool_stop(pool *p)
{
    int i;
    __atomic_store_n(&p->stop, 1, __ATOMIC_RELEASE);
    if (p->threads > 1) {
        pt_barrier_wait(&p->go);
    }
    for (i = 0; i < p->threads - 1; i++) {
        pt_join(p->t[i]);
    }
}

static void job_frame(pool *p, int i)
{
    world_frame(&p->w[i], p->f);
}

/* ---- hash ----------------------------------------------------------------------------------------------------------------- */
static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void part_hash_kind(sa1_context *ctx, int kind, const char *name, int n, int frames, const int *threads, int nthreads)
{
    uint64_t *ref = (uint64_t *)calloc((size_t)n, sizeof *ref);
    pt_world *W = (pt_world *)calloc((size_t)n, sizeof *W);
    double wall1 = 0.0;
    int t, i;
    for (t = 0; t < nthreads; t++) {
        pool P;
        int f, diff = 0, faults = 0, first = -1;
        double t0, wall;
        memset(&P, 0, sizeof P);
        for (i = 0; i < n; i++) {
            /* kind < 0: the six kinds that tick, in turn */
            static const int8_t mix[6] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA, SA1_CHAR_BIG };
            if (world_make(&W[i], ctx, kind < 0 ? mix[i % 6] : kind, i) != SA1_OK) {
                g_fails++;
                return;
            }
        }
        P.w = W;
        P.n = n;
        P.frames = frames;
        P.job = job_frame;
        pool_start(&P, threads[t]);
        t0 = pt_now_ms();
        for (f = 0; f < frames; f++) {
            P.f = f;
            pool_phase(&P);
        }
        wall = pt_now_ms() - t0;
        pool_stop(&P);
        for (i = 0; i < n; i++) {
            if (W[i].faulted) {
                faults++;
            }
            if (t == 0) {
                ref[i] = W[i].hash;
            } else if (ref[i] != W[i].hash) {
                if (first < 0) {
                    first = i;
                }
                diff++;
            }
            sa1_character_destroy(ctx, W[i].id);
        }
        if (t == 0) {
            wall1 = wall;
        }
        printf("SPEED %s n=%d threads=%d wall_ms=%.1f per_game_second_ms=%.1f speedup=%.2f ratio=%.3f\n", name, n, threads[t], wall,
               wall * 60.0 / (double)frames, wall1 / wall, wall / wall1);
        CHECK(faults == 0, "%s, %d worlds, %d threads: no fault (%d)", name, n, threads[t], faults);
        if (t != 0) {
            CHECK(diff == 0, "%s, %d worlds, %d threads: every world's hash equals the 1-thread run (%d differ, first world %d)", name, n,
                  threads[t], diff, first);
        } else {
            uint64_t d = 1469598103934665603ull;
            for (i = 0; i < n && i < 32; i++) {
                d = fnv(d, &ref[i], sizeof ref[i]);
            }
            printf("DETERMINISM hash %s frames=%d first32=%016llx\n", name, frames, (unsigned long long)d);
        }
    }
    free(ref);
    free(W);
    (void)cmp_double;
}

static void part_hash(int n, int frames, const int *threads, int nthreads)
{
    sa1_context *ctx = ctx_make((1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) |
                                (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG));
    part_hash_kind(ctx, SA1_CHAR_KNUCKLES, "knuckles", n, frames, threads, nthreads);
    part_hash_kind(ctx, SA1_CHAR_TAILS, "tails", n, frames, threads, nthreads);
    part_hash_kind(ctx, -1, "six_kinds", n < 32 ? n : 32, frames, threads, nthreads);
    sa1_shutdown(ctx);
}

/* ---- errors --------------------------------------------------------------------------------------------------------------- */
typedef struct err_thread {
    sa1_context *ctx;
    int index, iters, wrong;
    pt_barrier *start;
} err_thread;

static pt_ret PT_API err_main(void *arg)
{
    err_thread *e = (err_thread *)arg;
    char want[64];
    int i;
    pt_barrier_wait(e->start);
    for (i = 0; i < e->iters; i++) {
        snprintf(want, sizeof want, "thread %d message %d", e->index, i);
        sa1_refuse(e->ctx, SA1_ERR_INVALID_ARGUMENT, SA1_LOG_DEBUG, "%s", want);
        if (strcmp(sa1_last_error(e->ctx), want) != 0) {
            e->wrong++;
        }
    }
    return (pt_ret)0;
}

static pt_ret PT_API err_one(void *arg)
{
    sa1_context *ctx = (sa1_context *)arg;
    sa1_refuse(ctx, SA1_ERR_INVALID_ARGUMENT, SA1_LOG_DEBUG, "from another thread");
    return (pt_ret)0;
}

static void part_errors(void)
{
    sa1_host h;
    sa1_context *ctx = NULL, *other = NULL;
    err_thread E[8];
    pt_thread T[8];
    pt_barrier start;
    int i, wrong = 0;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK || sa1_init(&h, &other) != SA1_OK) {
        CHECK(0, "sa1_init");
        return;
    }
    CHECK(sa1_last_error(NULL) != NULL && !strcmp(sa1_last_error(NULL), "no context"), "no context: the old text");
    CHECK(sa1_last_error(ctx)[0] == 0, "a new context: no message");
    /* one thread alone: 0.3's behaviour */
    sa1_refuse(ctx, SA1_ERR_INVALID_ARGUMENT, SA1_LOG_DEBUG, "first");
    CHECK(!strcmp(sa1_last_error(ctx), "first"), "one thread: its message (%s)", sa1_last_error(ctx));
    CHECK(!strcmp(ctx->error, "first"), "beside ctx->error");
    snprintf(ctx->error, sizeof ctx->error, "a quiet writer");  /* sa1_face.c's quiet(), sa1_preview.c's, sa1_character.c:403 */
    CHECK(!strcmp(sa1_last_error(ctx), "a quiet writer"), "one thread: a direct write of ctx->error is newer (%s)", sa1_last_error(ctx));
    ctx->error[0] = 0;                                          /* sa1_load_disc clears it */
    CHECK(sa1_last_error(ctx)[0] == 0, "one thread: a cleared message stays cleared (%s)", sa1_last_error(ctx));
    sa1_refuse(ctx, SA1_ERR_INVALID_ARGUMENT, SA1_LOG_DEBUG, "second");
    sa1_refuse(other, SA1_ERR_INVALID_ARGUMENT, SA1_LOG_DEBUG, "other context");
    CHECK(!strcmp(sa1_last_error(ctx), "second") && !strcmp(sa1_last_error(other), "other context"),
          "two contexts on one thread: each its own (%s / %s)", sa1_last_error(ctx), sa1_last_error(other));
    /* a thread that wrote none reads the newest of any thread */
    {
        sa1_context *fresh = NULL;
        pt_thread t;
        sa1_init(&h, &fresh);
        pt_start(&t, err_one, fresh);
        pt_join(t);
        CHECK(!strcmp(sa1_last_error(fresh), "from another thread"), "a thread that wrote none reads the newest of any thread (%s)",
              sa1_last_error(fresh));
        sa1_shutdown(fresh);
    }
    /* 8 threads */
    memset(&start, 0, sizeof start);
    start.n = 8;
    for (i = 0; i < 8; i++) {
        E[i].ctx = ctx;
        E[i].index = i;
        E[i].iters = 20000;
        E[i].wrong = 0;
        E[i].start = &start;
        pt_start(&T[i], err_main, &E[i]);
    }
    for (i = 0; i < 8; i++) {
        pt_join(T[i]);
        wrong += E[i].wrong;
    }
    CHECK(wrong == 0, "8 threads, 20000 messages each: every thread reads its own newest (%d wrong)", wrong);
    sa1_shutdown(ctx);
    sa1_shutdown(other);
}

/* ---- fault isolation ------------------------------------------------------------------------------------------------------ */
static int g_fault_variant;

SA1G_GAME_TEXT static void fault_pre(sa1g_world *w, int32_t sub, void *user)
{
    (void)user;
    (void)sub;
    if (g_fault_variant == 0) {
        sa1g_fatal(0x8C098B42u, "parallel_test: MFree: double free");
    } else if (g_fault_variant == 1) {
        void (*fn)(void) = (void (*)(void))sa1_fn(SA1_GFN_C(0x8C011D46u), 0u);    /* policy fatal in gen/policy/stubs.tsv */
        fn();
    } else {
        (void)w;
        *(volatile uint32_t *)sa1_decode(0x70000040u) = 1u;         /* segment 0x10: a trap reservation */
    }
}

typedef struct fault_thread {
    pt_world *w;
    int frames, index, inject_frame;
    pt_barrier *frame;
    sa1_status fail_status;
    int fail_frame;
    char message[512];
} fault_thread;

static pt_ret PT_API fault_main(void *arg)
{
    fault_thread *t = (fault_thread *)arg;
    int f;
    t->fail_frame = -1;
    for (f = 0; f < t->frames; f++) {
        if (f == t->inject_frame) {
            sa1g_world *w = sa1_character_world(t->w->ctx, t->w->id);
            w->emote.pre = fault_pre;
        }
        world_frame(t->w, f);
        if (t->w->faulted && t->fail_frame < 0) {
            t->fail_frame = f;
            t->fail_status = t->w->st;
            snprintf(t->message, sizeof t->message, "%s", sa1_last_error(t->w->ctx));
        }
        pt_barrier_wait(t->frame);
    }
    return (pt_ret)0;
}

static void part_fault(int frames)
{
    enum { N = 20 };
    sa1_context *ctx = ctx_make(1u << SA1_CHAR_KNUCKLES);
    static pt_world W[N];
    static fault_thread T[N];
    pt_thread H[N];
    uint64_t solo[N];
    pt_barrier fb;
    int v, i, f;
    /* the solo runs: one thread, no fault */
    for (i = 0; i < N; i++) {
        world_make(&W[i], ctx, SA1_CHAR_KNUCKLES, i);
    }
    for (f = 0; f < frames; f++) {
        for (i = 0; i < N; i++) {
            world_frame(&W[i], f);
        }
    }
    for (i = 0; i < N; i++) {
        solo[i] = W[i].hash;
        sa1_character_destroy(ctx, W[i].id);
    }
    for (v = 0; v < 3; v++) {
        static const char *const names[3] = { "sa1g_fatal", "the fatal stub 8C011D46", "an access violation on a trap" };
        sa1_event ev[64];
        uint32_t got = 0, faults = 0, unimpl = 0, k;
        int diff = 0, dead = 0;
        g_fault_variant = v;
        memset(&fb, 0, sizeof fb);
        fb.n = N;
        for (i = 0; i < N; i++) {
            world_make(&W[i], ctx, SA1_CHAR_KNUCKLES, i);
        }
        for (i = 0; i < N; i++) {
            memset(&T[i], 0, sizeof T[i]);
            T[i].w = &W[i];
            T[i].frames = frames;
            T[i].index = i;
            T[i].inject_frame = i == 3 ? frames / 3 : -1;
            T[i].frame = &fb;
            pt_start(&H[i], fault_main, &T[i]);
        }
        for (i = 0; i < N; i++) {
            pt_join(H[i]);
        }
        for (i = 0; i < N; i++) {
            if (i != 3 && W[i].hash != solo[i]) {
                diff++;
            }
            dead += W[i].faulted;
        }
        CHECK(dead == 1 && W[3].faulted, "%s on thread 3: only its world stopped (%d dead)", names[v], dead);
        CHECK(diff == 0, "%s: the 19 others' hashes equal their solo runs (%d differ)", names[v], diff);
        if (v == 1) {
            CHECK(T[3].fail_status == SA1_ERR_NOT_IMPLEMENTED, "%s: its tick says %s", names[v], sa1_status_string(T[3].fail_status));
        } else {
            CHECK(T[3].fail_status == SA1_ERR_GAME_FAULT, "%s: its tick says %s", names[v], sa1_status_string(T[3].fail_status));
        }
        printf("  info %s: on thread 3 sa1_last_error: %s\n", names[v], T[3].message);
        CHECK(T[3].message[0] != 0 && strstr(T[3].message, "character") != NULL, "%s: thread 3's sa1_last_error names the fault", names[v]);
        for (;;) {
            sa1_status st = sa1_poll_events(ctx, ev, 64, &got);
            if (st != SA1_OK || got == 0) {
                break;
            }
            for (k = 0; k < got; k++) {
                faults += ev[k].type == SA1_EVENT_FAULT;
                unimpl += ev[k].type == SA1_EVENT_UNIMPLEMENTED;
            }
        }
        if (v == 1) {
            CHECK(unimpl == 1 && faults == 0, "%s: one SA1_EVENT_UNIMPLEMENTED, no FAULT (%u, %u)", names[v], (unsigned)unimpl, (unsigned)faults);
        } else {
            CHECK(faults == 1 && unimpl == 0, "%s: one SA1_EVENT_FAULT (%u, %u)", names[v], (unsigned)faults, (unsigned)unimpl);
        }
        for (i = 0; i < N; i++) {
            sa1_character_destroy(ctx, W[i].id);
        }
    }
    sa1_shutdown(ctx);
}

#if defined(SA1_API_FOLLOWER)
/* ---- followers (step 3, after lanes/lib_follow_done.txt) --------------------------------------------------------------------- */
static sa1_status fol_make(pt_world *f, sa1_context *ctx, int kind, sa1_character_id leader, int gi)
{
    sa1_follower_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.leader = leader;
    d.flags = SA1_FOLLOW_SEED;
    d.seed = 0x7654321u + (uint32_t)gi * 2654435761u;
    f->gi = gi;
    f->ctx = ctx;
    f->hash = 1469598103934665603ull;
    f->faulted = 0;
    f->st = sa1_follower_create(ctx, &d, &f->id);
    if (SA1_FAILED(f->st)) {
        printf("follower %d: sa1_follower_create: %s: %s\n", gi, sa1_status_string(f->st), sa1_last_error(ctx));
    }
    return f->st;
}

/* one host frame of a follower: only the AI moves it. A job: tick, state_ex, render, info (the six parallel calls but feed) */
static void fol_frame(pt_world *w, int f)
{
    sa1_input in;
    sa1_follower_info info;
    sa1_status st;
    (void)f;
    if (w->faulted) {
        return;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    st = sa1_character_tick(w->ctx, w->id, &in, 1.0f / 60.0f, NULL, NULL);
    if (SA1_FAILED(st)) {
        w->st = st;
        w->faulted = 1;
        return;
    }
    hash_state_render(w);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    if (sa1_follower_info_get(w->ctx, w->id, &info) == SA1_OK) {
        info.leader = 0;        /* an id: its generation differs from run to run */
        w->hash = fnv(w->hash, &info, sizeof info);
    }
}

static void job_fol(pool *p, int i)
{
    fol_frame(&p->w[i], p->f);
}

/* levels of followers; level k follows level k - 1 (level 1: the leader), ticked level by level: one parallel phase each */
typedef struct fol_run {
    pt_world leader;
    pt_world *f;
    int n, levels, frames;
    double wall_ms;
    int faults;
} fol_run;

static int fol_run_once(sa1_context *ctx, fol_run *R, int kind, int threads)
{
    pool P;
    int per = R->n / R->levels, i, l, fr;
    double t0;
    memset(&P, 0, sizeof P);
    if (world_make(&R->leader, ctx, SA1_CHAR_KNUCKLES, 0) != SA1_OK) {
        return -1;
    }
    for (l = 0; l < R->levels; l++) {
        for (i = 0; i < per; i++) {
            sa1_character_id lead = l == 0 ? R->leader.id : R->f[(l - 1) * per + i].id;
            if (fol_make(&R->f[l * per + i], ctx, kind, lead, 1000 + l * 100 + i) != SA1_OK) {
                return -1;
            }
        }
    }
    P.job = job_fol;
    pool_start(&P, threads);
    t0 = pt_now_ms();
    for (fr = 0; fr < R->frames; fr++) {
        world_frame(&R->leader, fr);
        for (l = 0; l < R->levels; l++) {
            P.w = R->f + l * per;
            P.n = per;
            P.f = fr;
            pool_phase(&P);
        }
    }
    R->wall_ms = pt_now_ms() - t0;
    pool_stop(&P);
    R->faults = R->leader.faulted;
    for (i = 0; i < per * R->levels; i++) {
        R->faults += R->f[i].faulted;
    }
    return 0;
}

static void fol_destroy(sa1_context *ctx, fol_run *R)
{
    int per = R->n / R->levels, i;
    for (i = per * R->levels - 1; i >= 0; i--) {
        sa1_character_destroy(ctx, R->f[i].id);
    }
    sa1_character_destroy(ctx, R->leader.id);
}

static void part_follow_case(sa1_context *ctx, const char *name, int kind, int n, int levels, int frames, const int *threads, int nthreads)
{
    fol_run R;
    uint64_t *ref = (uint64_t *)calloc((size_t)n, sizeof *ref), refl = 0;
    int per = n / levels, t, i;
    double wall1 = 0.0;
    memset(&R, 0, sizeof R);
    R.f = (pt_world *)calloc((size_t)n, sizeof *R.f);
    R.n = n;
    R.levels = levels;
    R.frames = frames;
    for (t = 0; t < nthreads; t++) {
        int diff = 0;
        if (fol_run_once(ctx, &R, kind, threads[t]) != 0) {
            CHECK(0, "%s: the worlds could not be made", name);
            break;
        }
        for (i = 0; i < per * levels; i++) {
            if (t == 0) {
                ref[i] = R.f[i].hash;
            } else if (ref[i] != R.f[i].hash) {
                diff++;
            }
        }
        if (t == 0) {
            wall1 = R.wall_ms;
            refl = R.leader.hash;
        } else if (refl != R.leader.hash) {
            diff++;
        }
        printf("SPEED %s n=%d levels=%d threads=%d wall_ms=%.1f per_game_second_ms=%.1f speedup=%.2f ratio=%.3f\n", name, per * levels, levels,
               threads[t], R.wall_ms, R.wall_ms * 60.0 / (double)frames, wall1 / R.wall_ms, R.wall_ms / wall1);
        CHECK(R.faults == 0, "%s, %d threads: no fault (%d)", name, threads[t], R.faults);
        if (t != 0) {
            CHECK(diff == 0, "%s, %d threads: every world's hash equals the 1-thread run (%d differ)", name, threads[t], diff);
        } else {
            /* the same worlds on x86 (32 followers) and x64 (99): the first 32 of one level, the first 10 of each of three */
            int each = levels == 1 ? 32 : 10, l;
            uint64_t d = refl;
            for (l = 0; l < levels; l++) {
                for (i = 0; i < per && i < each; i++) {
                    d = fnv(d, &ref[l * per + i], sizeof ref[0]);
                }
            }
            printf("DETERMINISM follow %s levels=%d frames=%d each=%d hash=%016llx\n", name, levels, frames, each, (unsigned long long)d);
        }
        fol_destroy(ctx, &R);
    }
    free(ref);
    free(R.f);
}

/* host-fed followers: sa1_follower_feed is a parallel call too: a job = feed, tick, state_ex, render, info */
static void job_fed(pool *p, int i)
{
    pt_world *w = &p->w[i];
    sa1_leader_state l;
    float a = (float)p->f * 0.01f + (float)w->gi;
    memset(&l, 0, sizeof l);
    l.struct_size = sizeof l;
    l.position[0] = 300.0f * (float)cos(a);
    l.position[1] = 8.0f;
    l.position[2] = 300.0f * (float)sin(a);
    l.velocity[0] = -180.0f * (float)sin(a);
    l.velocity[2] = 180.0f * (float)cos(a);
    l.yaw = a;
    l.flags = SA1_LEADER_ON_GROUND;
    l.kind = SA1_CHAR_SONIC;
    if (!w->faulted && sa1_follower_feed(w->ctx, w->id, &l) != SA1_OK) {
        w->faulted = 1;
    }
    fol_frame(w, p->f);
}

static void part_fed(sa1_context *ctx, int frames, const int *threads, int nthreads)
{
    enum { N = 32 };
    static pt_world F[N];
    uint64_t ref[N];
    int t, i, fr;
    for (t = 0; t < nthreads; t++) {
        pool P;
        int diff = 0, faults = 0;
        memset(&P, 0, sizeof P);
        for (i = 0; i < N; i++) {
            sa1_follower_desc d;
            memset(&d, 0, sizeof d);
            d.struct_size = sizeof d;
            d.kind = SA1_CHAR_KNUCKLES;
            d.flags = SA1_FOLLOW_SEED;
            d.seed = 0x1357u + (uint32_t)i * 2654435761u;
            d.host_leader.struct_size = sizeof d.host_leader;
            d.host_leader.position[0] = 300.0f;
            d.host_leader.position[1] = 8.0f;
            d.host_leader.flags = SA1_LEADER_ON_GROUND;
            d.host_leader.kind = SA1_CHAR_SONIC;
            F[i].gi = i;
            F[i].ctx = ctx;
            F[i].hash = 1469598103934665603ull;
            F[i].faulted = 0;
            F[i].st = sa1_follower_create(ctx, &d, &F[i].id);
            if (SA1_FAILED(F[i].st)) {
                CHECK(0, "host-fed follower %d: %s", i, sa1_last_error(ctx));
                return;
            }
        }
        P.job = job_fed;
        P.w = F;
        P.n = N;
        pool_start(&P, threads[t]);
        for (fr = 0; fr < frames; fr++) {
            P.f = fr;
            pool_phase(&P);
        }
        pool_stop(&P);
        for (i = 0; i < N; i++) {
            faults += F[i].faulted;
            if (t == 0) {
                ref[i] = F[i].hash;
            } else {
                diff += ref[i] != F[i].hash;
            }
            sa1_character_destroy(ctx, F[i].id);
        }
        CHECK(faults == 0, "32 host-fed followers, %d threads: no fault (%d)", threads[t], faults);
        if (t != 0) {
            CHECK(diff == 0, "32 host-fed followers, %d threads: every hash equals the 1-thread run (%d differ)", threads[t], diff);
        }
    }
}

/* FT-T1 (follow096_lib.md 5.12): followers on worker threads while the main thread ticks a world of its own in the same phase (the player's, whose
 * followers are another's: distinct characters), and moves a host object and a moving collision object between frames */
typedef struct ft_result {
    uint64_t own, leader, f[32];
    double own_max_ms, wall_ms;
    int faults;
} ft_result;

static int part_ft_run(sa1_context *ctx, int threads, int frames, ft_result *o)
{
    enum { N = 32 };
    pool P;
    pt_world own, leader;
    static pt_world F[N];
    sa1_hostobj_desc hd;
    static const sa1_collision_tri box[2] = {
        { { { -40.0f, 0.0f, -40.0f }, { 40.0f, 0.0f, 40.0f }, { 40.0f, 0.0f, -40.0f } }, SA1_SURFACE_SOLID },
        { { { -40.0f, 0.0f, -40.0f }, { -40.0f, 0.0f, 40.0f }, { 40.0f, 0.0f, 40.0f } }, SA1_SURFACE_SOLID },
    };
    uint32_t ho = 0, co = 0;
    int i, fr;
    double t0;
    memset(&P, 0, sizeof P);
    memset(o, 0, sizeof *o);
    if (world_make(&leader, ctx, SA1_CHAR_KNUCKLES, 0) != SA1_OK || world_make(&own, ctx, SA1_CHAR_KNUCKLES, 500) != SA1_OK) {
        return -1;
    }
    for (i = 0; i < N; i++) {
        if (fol_make(&F[i], ctx, SA1_CHAR_KNUCKLES, leader.id, 2000 + i) != SA1_OK) {
            return -1;
        }
    }
    memset(&hd, 0, sizeof hd);
    hd.struct_size = sizeof hd;
    hd.kind = SA1_HOSTOBJ_ENEMY;
    hd.position[0] = 100.0f;
    hd.position[1] = 8.0f;
    hd.armor = -1;
    hd.touch = -1;
    hd.num_shapes = 1;
    hd.shapes[0].form = SA1_SHAPE_SPHERE;
    hd.shapes[0].size[0] = 18.0f;
    if (sa1_hostobj_add(ctx, &hd, &ho) != SA1_OK) {
        printf("sa1_hostobj_add: %s\n", sa1_last_error(ctx));
        return -1;
    }
    {
        float pos[3] = { 0.0f, 20.0f, 0.0f };
        if (sa1_collision_object_add(ctx, box, 2, pos, NULL, &co) != SA1_OK) {
            printf("sa1_collision_object_add: %s\n", sa1_last_error(ctx));
            return -1;
        }
    }
    P.job = job_fol;
    P.w = F;
    P.n = N;
    pool_start(&P, threads);
    t0 = pt_now_ms();
    for (fr = 0; fr < frames; fr++) {
        float a = (float)fr * 0.02f, p[3];
        double t1;
        /* between phases, on this thread: the moves, then the leader */
        p[0] = 100.0f + 60.0f * (float)sin(a);
        p[1] = 8.0f;
        p[2] = 60.0f * (float)cos(a);
        sa1_hostobj_move(ctx, ho, p, a);
        p[0] = 200.0f * (float)cos(a);
        p[1] = 20.0f;
        p[2] = 200.0f * (float)sin(a);
        sa1_collision_object_move(ctx, co, p, NULL);
        world_frame(&leader, fr);
        /* the phase: the followers on the workers, this thread's own world beside them, then this thread takes jobs too */
        P.f = fr;
        __atomic_store_n(&P.next, 0, __ATOMIC_RELEASE);
        if (P.threads > 1) {
            pt_barrier_wait(&P.go);
            t1 = pt_now_ms();
            world_frame(&own, fr);
            t1 = pt_now_ms() - t1;
            pool_drain(&P);
            pt_barrier_wait(&P.done);
        } else {
            t1 = pt_now_ms();
            world_frame(&own, fr);
            t1 = pt_now_ms() - t1;
            pool_drain(&P);
        }
        if (t1 > o->own_max_ms) {
            o->own_max_ms = t1;
        }
    }
    o->wall_ms = pt_now_ms() - t0;
    pool_stop(&P);
    o->own = own.hash;
    o->leader = leader.hash;
    o->faults = own.faulted + leader.faulted;
    for (i = 0; i < N; i++) {
        o->f[i] = F[i].hash;
        o->faults += F[i].faulted;
        sa1_character_destroy(ctx, F[i].id);
    }
    sa1_character_destroy(ctx, own.id);
    sa1_character_destroy(ctx, leader.id);
    sa1_hostobj_remove(ctx, ho, 0u);
    sa1_collision_object_remove(ctx, co);
    return 0;
}

static void part_follow(int n, int frames, const int *threads, int nthreads)
{
    sa1_context *ctx;
    ft_result ser, par;
    static const int fth[2] = { 2, 8 };
    int i, c;
    if (!(sa1_follower_kinds() & (1u << SA1_CHAR_KNUCKLES))) {
        printf("SKIP behaviour: follower is not built yet: %s\n",
               sa1_follower_kind_reason(SA1_CHAR_KNUCKLES) ? sa1_follower_kind_reason(SA1_CHAR_KNUCKLES) : "");
        return;
    }
    ctx = ctx_make((1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_TAILS));
    part_follow_case(ctx, "knuckles_followers", SA1_CHAR_KNUCKLES, n, 1, frames, threads, nthreads);
    part_follow_case(ctx, "chain_3_levels", SA1_CHAR_KNUCKLES, n - n % 3, 3, frames, threads, nthreads);
    if (sa1_follower_kinds() & (1u << SA1_CHAR_TAILS)) {
        part_follow_case(ctx, "tails_followers", SA1_CHAR_TAILS, 32, 1, frames, threads, nthreads);
    }
    part_fed(ctx, frames, threads, nthreads);
    /* FT-T1 */
    if (part_ft_run(ctx, 1, frames, &ser) != 0) {
        CHECK(0, "FT-T1: the serial run could not be made");
        sa1_shutdown(ctx);
        return;
    }
    printf("FT-T1 serial: wall %.1f ms, the main thread's own world: longest tick %.3f ms\n", ser.wall_ms, ser.own_max_ms);
    CHECK(ser.faults == 0, "FT-T1 serial: no fault (%d)", ser.faults);
    for (c = 0; c < 2; c++) {
        int diff = 0;
        if (part_ft_run(ctx, fth[c], frames, &par) != 0) {
            CHECK(0, "FT-T1: the run on %d threads could not be made", fth[c]);
            break;
        }
        for (i = 0; i < 32; i++) {
            diff += ser.f[i] != par.f[i];
        }
        diff += ser.own != par.own;
        diff += ser.leader != par.leader;
        printf("FT-T1 %d threads: wall %.1f ms, the main thread's own world beside 32 followers: longest tick %.3f ms (serial %.3f)\n", fth[c],
               par.wall_ms, par.own_max_ms, ser.own_max_ms);
        CHECK(par.faults == 0, "FT-T1, %d threads: no fault (%d), no deadlock", fth[c], par.faults);
        CHECK(diff == 0, "FT-T1, %d threads: identical to the serial run (%d of 34 hashes differ)", fth[c], diff);
    }
    sa1_shutdown(ctx);
}
#endif /* SA1_API_FOLLOWER */
#endif /* SA1_API_PARALLEL */

/* ---- main ------------------------------------------------------------------------------------------------------------------ */
#if defined(SA1PT_REPLAY)
/* parallel_replay: tests/replay_test.c's strict replays while 20 other worlds tick on other threads, in a context of their own */
#define main replay_test_main
#define log_cb replay_test_log_cb
#include "replay_test.c"
#undef main
#undef log_cb

typedef struct load_thread {
    sa1_context *ctx;
    pt_world w[1];
    volatile int32_t *stop;
    uint64_t frames;
} load_thread;

static pt_ret PT_API load_main(void *arg)
{
    load_thread *l = (load_thread *)arg;
    int f = 0;
    while (!__atomic_load_n(l->stop, __ATOMIC_ACQUIRE) && !l->w[0].faulted) {
        world_frame(&l->w[0], f++);
        l->frames++;
    }
    return (pt_ret)0;
}

int main(int argc, char **argv)
{
    enum { N = 20 };
    static load_thread L[N];
    pt_thread T[N];
    volatile int32_t stop = 0;
    sa1_context *ctx;
    int i, rc, dead = 0;
    uint64_t frames = 0;
    g_gdi = getenv("SA1_TEST_GDI");
    if (g_gdi == NULL) {
        printf("parallel_replay: set SA1_TEST_GDI\n");
        return 2;
    }
    build_scene(&g_scene, 0);
    ctx = ctx_make(1u << SA1_CHAR_KNUCKLES);
    for (i = 0; i < N; i++) {
        L[i].ctx = ctx;
        L[i].stop = &stop;
        if (world_make(&L[i].w[0], ctx, SA1_CHAR_KNUCKLES, i) != SA1_OK) {
            return 2;
        }
    }
    /* parallel ticks on one context only under SA1_API_PARALLEL; else the world exists, one context each is the contract */
    for (i = 0; i < N; i++) {
        pt_start(&T[i], load_main, &L[i]);
    }
    rc = replay_test_main(argc, argv);
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    for (i = 0; i < N; i++) {
        pt_join(T[i]);
        dead += L[i].w[0].faulted;
        frames += L[i].frames;
    }
    printf("LOAD %d worlds ticked %llu frames beside the replays, %d faulted\n", N, (unsigned long long)frames, dead);
    return rc != 0 || dead != 0;
}
#else
int main(int argc, char **argv)
{
    const char *only[16];
    int nonly = 0, i;
#if UINTPTR_MAX > 0xFFFFFFFFu
    int n = 99;
#else
    int n = 32;
#endif
    int frames = 600, contexts = 2, city = 1, nthreads = 5, nworlds_given = 0;
    int threads[16] = { 1, 2, 4, 8, 20 };
    double seconds = 10.0;
    int rc;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
        if (!strcmp(a, "--only") && nonly < 16) { only[nonly++] = v; i++; }
        else if (!strcmp(a, "--worlds")) { n = atoi(v); nworlds_given = 1; i++; }
        else if (!strcmp(a, "--frames")) { frames = atoi(v); i++; }
        else if (!strcmp(a, "--contexts")) { contexts = atoi(v); i++; }
        else if (!strcmp(a, "--seconds")) { seconds = atof(v); i++; }
        else if (!strcmp(a, "--scene")) { city = !strcmp(v, "city"); i++; }
        else if (!strcmp(a, "--stack-kb")) { g_stack_bytes = (size_t)atoi(v) * 1024u; i++; }
        else if (!strcmp(a, "--threads")) {
            const char *p = v;
            nthreads = 0;
            while (*p && nthreads < 16) {
                threads[nthreads++] = atoi(p);
                p = strchr(p, ',');
                p = p ? p + 1 : "";
            }
            i++;
        }
        else if (a[0] == '-') { printf("unknown argument %s\n", a); return 2; }
        else { g_gdi = a; }
    }
    (void)nworlds_given;
    if (g_gdi == NULL) {
        g_gdi = getenv("SA1_TEST_GDI");
    }
    if (g_gdi == NULL) {
        printf("usage: parallel_test [options] GDI (or SA1_TEST_GDI)\n");
        return 2;
    }
    build_scene(&g_scene, city);
    rc = 0;
    if (nonly) {
        for (i = 0; i < nonly; i++) {
            if (!strcmp(only[i], "race")) {
                rc |= part_race(n, contexts, frames, SA1_CHAR_KNUCKLES);
            } else if (!strcmp(only[i], "tight")) {
                rc |= part_tight(seconds, 1);
#if defined(SA1_API_PARALLEL)
            } else if (!strcmp(only[i], "hash")) {
                printf("== hash\n");
                part_hash(n, frames, threads, nthreads);
            } else if (!strcmp(only[i], "errors")) {
                printf("== errors\n");
                part_errors();
            } else if (!strcmp(only[i], "fault")) {
                printf("== fault\n");
                part_fault(300);
#if defined(SA1_API_FOLLOWER)
            } else if (!strcmp(only[i], "follow")) {
                printf("== follow\n");
                part_follow(n, frames, threads, nthreads);
#endif
#endif
            } else {
                printf("unknown or absent part %s\n", only[i]);
                return 2;
            }
        }
    } else {
#if defined(SA1_API_PARALLEL)
        printf("== errors\n");
        part_errors();
        printf("== hash\n");
        part_hash(n, frames, threads, nthreads);
        printf("== fault\n");
        part_fault(300);
#if defined(SA1_API_FOLLOWER)
        printf("== follow\n");
        part_follow(n, frames, threads, nthreads);
#endif
#else
        printf("this library has no SA1_API_PARALLEL: only --only race and --only tight\n");
        return 2;
#endif
    }
    printf("parallel_test: %d failure(s)\n", g_fails);
    return (rc != 0 || g_fails != 0) ? 1 : 0;
}
#endif /* SA1PT_REPLAY */
