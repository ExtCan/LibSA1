/*
 * ho_lib_glue.c - libsa1's side of the round-5 host-object oracle (gsa1_rounds_plan.md 9.4, libsa1_interaction.md 4.3).
 *
 * libsa1's own host-object layer is compiled into the harness unchanged (harness/build.py, every 32-bit Windows build):
 * LIB src/sa1_hostobj.c (the registry and the public calls) and src/world/sa1_world_hostobj.c (the proxies, the
 * LIBSA1_HOST exec 0x7F000100, the pre / post hooks of the step), with -DSA1_GA32 (guest == host), -DSA1_STATIC and
 * libsa1's world FP flags, and three renames that keep them off the GUEST-ARENA glue of --ga builds:
 * sa1_fn -> sa1h_ho_fn, sa1g_sqrtf -> sa1h_ho_sqrtf, sa1g_fsca -> sa1h_ho_fsca. It runs on the ORIGINAL game code in the
 * interpreter (or the routed native code of --route): the oracle of libsa1_interaction.md 4.3.
 *
 * This file gives the layer what a libsa1 character world gives it:
 *   - one world (sa1g_world): the arena is the harness's DC RAM (guest 0x8C000000 at host 0x8C000000), the extension
 *     segment 0x18 is 1 MB of host memory mapped at its guest address 0x98000000 (sh4_map_extra), where the layer puts
 *     its slot block with the OBJ_CONDITION stand-ins and the CCL templates the game code reads (in libsa1 too segment
 *     0x18 lies outside the arena: libsa1_interaction.md 6.4 row 2), booted, attached with sa1ho_world_attach;
 *   - one context (sa1_context): libsa1's default coordinates (SA1_COORDS_PRESET_SOURCE), the character as id 1;
 *   - copies of the small libsa1 functions the two files call: sa1_world_intrin.c sa1g_sqrtf / sa1g_fsca (the FP
 *     models of include/pc/sh4_fpmodel.h, as libsa1), sa1_world_tick.c sa1g_world_event_push / sa1g_world_poll_events,
 *     sa1_core.c sa1_fail / sa1_copy_out / sa1_to_host_dir / sa1_to_host_point, and the world calls
 *     (sa1g_world_current / sa1g_world_call / sa1g_world_ext_alloc / sa1_character_world) as one world needs them;
 *   - sa1h_ho_fn: a game function's typed wrapper (ho_oracle.c hoo_game_fn), which runs it with sh4_invoke.
 * The harness process runs in the game's FP mode (RZ, FTZ, DAZ: sh4_host_fp_game_mode), the mode of libsa1's ticks.
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_hostobj.h"
#include "pc/sh4_fpmodel.h"
#include "ho_glue.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HO_EXEC_ADDR 0x7F000100u

static sa1g_world g_w;
static sa1_context g_ctx;
static int g_init;
static uint8_t g_inst_dummy[16];

/* ---- copies of libsa1 functions --------------------------------------------------------------------------------- */
float sa1g_sqrtf(float x)                               /* sa1_world_intrin.c (renamed sa1h_ho_sqrtf) */
{
    return sh4_fsqrt(x);
}

void sa1g_fsca(int32_t ang, float *s, float *c)         /* sa1_world_intrin.c (renamed sa1h_ho_fsca) */
{
    sh4_fsca((uint32_t)ang, s, c);
}

int sa1g_world_event_push(sa1g_world *w, uint32_t type, uint32_t id, uint32_t bank, const float pos[3], uint32_t arg)
{
    sa1g_events *q;
    sa1_event *e;
    if (w == NULL) {
        return -1;
    }
    q = &w->events;
    if (q->count >= SA1G_EVENT_QUEUE) {
        q->dropped++;
        return -1;
    }
    e = &q->ev[(q->head + q->count) % SA1G_EVENT_QUEUE];
    q->count++;
    memset(e, 0, sizeof *e);
    e->type = type;
    e->id = id;
    e->bank = bank;
    if (pos != NULL) {
        e->position[0] = pos[0];
        e->position[1] = pos[1];
        e->position[2] = pos[2];
    }
    e->arg = arg;
    e->step = (uint32_t)w->steps;
    return 0;
}

sa1_status sa1_fail(sa1_context *ctx, sa1_status st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->error, sizeof ctx->error, fmt, ap);
    va_end(ap);
    fprintf(stderr, "hostobj: libsa1 %s: %s\n", st < 0 ? "error" : "warning", ctx->error);
    return st;
}

void sa1_copy_out(void *dst, const void *src, size_t src_size)
{
    uint32_t dsz;
    size_t n;
    memcpy(&dsz, dst, sizeof dsz);
    n = dsz < src_size ? dsz : src_size;
    memcpy(dst, src, n);
    memcpy(dst, &dsz, sizeof dsz);
}

void sa1_to_host_dir(const sa1_context *ctx, const float in[3], float out[3])
{
    const float *a = ctx->coords.axis;
    float x = in[0], y = in[1], z = in[2];
    out[0] = a[0] * x + a[1] * y + a[2] * z;
    out[1] = a[3] * x + a[4] * y + a[5] * z;
    out[2] = a[6] * x + a[7] * y + a[8] * z;
}

void sa1_to_host_point(const sa1_context *ctx, const float in[3], float out[3])
{
    float s = ctx->coords.scale;
    sa1_to_host_dir(ctx, in, out);
    out[0] *= s;
    out[1] *= s;
    out[2] *= s;
}

void *sa1_mem_alloc(const sa1_context *ctx, size_t size)
{
    (void)ctx;
    return calloc(1, size ? size : 1);
}

void sa1_mem_free(const sa1_context *ctx, void *p)
{
    (void)ctx;
    free(p);
}

/* ---- the world ------------------------------------------------------------------------------------------------- */
sa1g_world *sa1g_world_current(void)
{
    return g_init ? &g_w : NULL;
}

sa1_status sa1g_world_call(sa1g_world *w, sa1g_world_fn fn, void *user)
{
    if (w == NULL || fn == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    fn(w, user);                /* between two steps, in the game's FP mode (the harness's) */
    return SA1_OK;
}

uint32_t sa1g_world_ext_alloc(sa1g_world *w, size_t n, size_t align, void **host)
{
    size_t off;
    if (host != NULL) {
        *host = NULL;
    }
    if (w == NULL || w->ext == NULL || align == 0) {
        return 0;
    }
    off = (w->ext_used + align - 1u) / align * align;
    if (off + n > w->ext_committed) {
        return 0;
    }
    w->ext_used = off + n;
    if (host != NULL) {
        *host = w->ext + off;
    }
    return SA1G_EXT_GUEST + (uint32_t)off;
}

sa1_status sa1g_world_start_rings(sa1g_world *w, int32_t n)
{
    (void)w;
    (void)n;
    return SA1_ERR_NOT_IMPLEMENTED;     /* the oracle starts with the game's rings; scripts use "rings N" */
}

struct sa1g_world *sa1_character_world(const sa1_context *ctx, sa1_character_id id)
{
    return g_init && ctx == &g_ctx && id == 1 ? &g_w : NULL;
}

/* ---- the code map: game functions through ho_oracle.c's wrappers, the proxies' exec ------------------------------ */
void *sa1h_ho_fn(sa1_gfn g, uint32_t sig)
{
    void *f;
    (void)sig;
    if ((g.v & 0x1FFFFFFFu) == (HO_EXEC_ADDR & 0x1FFFFFFFu)) {
        return (void *)(uintptr_t)hoo_lib_exec_native();
    }
    f = hoo_game_fn(g.v);
    if (f == NULL) {
        hoo_fatal("the host-object layer called game function %08X, which the oracle has no wrapper for",
                  (unsigned)g.v);
    }
    return f;
}

/* ---- the interface of ho_glue.h ---------------------------------------------------------------------------------- */
int hoo_lib_init(uint8_t *ram, uint8_t *ext, uint32_t ext_guest, uint32_t ext_size)
{
    static const float axis[9] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
    if (g_init) {
        return 0;
    }
    if (ram == NULL || ext == NULL || ext_guest != SA1G_EXT_GUEST) {
        return -1;
    }
    memset(&g_ctx, 0, sizeof g_ctx);
    g_ctx.coords.struct_size = sizeof g_ctx.coords;
    g_ctx.coords.scale = 4.0f;          /* SA1_COORDS_PRESET_SOURCE: host = 4 * (x, -z, y) */
    memcpy(g_ctx.coords.axis, axis, sizeof axis);
    g_ctx.inst[0] = (sa1_instance *)(void *)g_inst_dummy;   /* the character of id 1 (slot 0, generation 0) */
    g_ctx.inst_gen[0] = 0;
    memset(&g_w, 0, sizeof g_w);
    g_w.ram = ram;
    g_w.ext = ext;
    g_w.ext_used = 0;
    g_w.ext_committed = ext_size;
    g_w.booted = 1;
    g_w.kind = -1;
    g_w.chr = -1;
    g_w.ho_hurt_steps = 0xFFFFFFFFu;
    g_init = 1;
    if (sa1ho_world_attach(&g_ctx, &g_w, -1) != SA1_OK) {
        g_init = 0;
        return -1;
    }
    return 0;
}

sa1_context *hoo_lib_ctx(void)
{
    return &g_ctx;
}

sa1_character_id hoo_lib_chr(void)
{
    return 1;
}

void hoo_lib_pre(int32_t sub)
{
    if (g_init && g_w.hostobj_pre != NULL) {
        g_w.hostobj_pre(&g_w, sub, g_w.user);
    }
}

void hoo_lib_post(int32_t sub)
{
    if (g_init && g_w.hostobj_post != NULL) {
        g_w.hostobj_post(&g_w, sub, g_w.user);
    }
}

void hoo_lib_step_done(void)
{
    g_w.steps++;
}

const void *hoo_lib_exec_native(void)
{
    const sa1g_fnent *e = sa1g_hostobj_image.lookup(HO_EXEC_ADDR);
    return e != NULL ? (const void *)e->fn : NULL;
}

void hoo_lib_exec(uint32_t tp)
{
    void (*f)(void *) = (void (*)(void *))hoo_lib_exec_native();
    if (f != NULL) {
        f(sa1_decode(tp));
    }
}

uint32_t hoo_lib_world_events(sa1_event *out, uint32_t cap)
{
    sa1g_events *q = &g_w.events;
    uint32_t n = 0;
    while (n < cap && q->count > 0) {
        out[n++] = q->ev[q->head];
        q->head = (q->head + 1u) % SA1G_EVENT_QUEUE;
        q->count--;
    }
    return n;
}

uint32_t hoo_lib_proxy_twp(uint32_t object)
{
    return g_init ? sa1ho_world_proxy_twp(&g_w, object) : 0u;
}

sa1_status hoo_lib_add_rings(int32_t n)
{
    return g_init ? sa1ho_world_add_rings(&g_w, n) : SA1_ERR_NOT_LOADED;
}
