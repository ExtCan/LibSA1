/*
 * libsa1 core: context lifetime, memory, logging, errors, coordinate system.
 *
 * The context carries the API's own state behind the sa1_context (sa1_api_ext, sa1_api_world.h): the
 * per-context world state of the ticking characters and the .gdi of the last sa1_load_disc, which the
 * worlds compose their memory images from (sadc docs/libsa1_integration.md 2.5, D2).
 */
#include "sa1_api_world.h"

#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------- */
/* Version / status                                                          */
/* ------------------------------------------------------------------------- */

uint32_t sa1_version(void)
{
    return ((uint32_t)SA1_VERSION_MAJOR << 16) | ((uint32_t)SA1_VERSION_MINOR << 8) | SA1_VERSION_PATCH;
}

#define SA1_STR2(x) #x
#define SA1_STR(x) SA1_STR2(x)

const char *sa1_version_string(void)
{
    return SA1_STR(SA1_VERSION_MAJOR) "." SA1_STR(SA1_VERSION_MINOR) "." SA1_STR(SA1_VERSION_PATCH);
}

const char *sa1_status_string(sa1_status status)
{
    switch (status) {
    case SA1_OK:                   return "ok";
    case SA1_PARTIAL:              return "partially loaded";
    case SA1_ERR_INVALID_ARGUMENT: return "invalid argument";
    case SA1_ERR_OUT_OF_MEMORY:    return "out of memory";
    case SA1_ERR_IO:               return "i/o error";
    case SA1_ERR_BAD_PATH:         return "rejected path";
    case SA1_ERR_BAD_FORMAT:       return "malformed data";
    case SA1_ERR_BAD_HASH:         return "unsupported disc version (hash mismatch)";
    case SA1_ERR_NOT_FOUND:        return "not found";
    case SA1_ERR_NOT_LOADED:       return "not loaded";
    case SA1_ERR_UNSUPPORTED:      return "unsupported";
    case SA1_ERR_NOT_IMPLEMENTED:  return "not implemented";
    case SA1_ERR_BUFFER_TOO_SMALL: return "buffer too small";
    case SA1_ERR_VERSION:          return "api version mismatch";
    case SA1_ERR_LIMIT:            return "safety limit exceeded";
    case SA1_ERR_GAME_FAULT:       return "game code fault";
    default:                       return "unknown status";
    }
}

/* ------------------------------------------------------------------------- */
/* Memory                                                                    */
/* ------------------------------------------------------------------------- */

static void *host_alloc(const sa1_host *h, size_t size)
{
    if (size == 0)
        size = 1;
    return h->alloc ? h->alloc(h->user, size) : malloc(size);
}

static void host_free(const sa1_host *h, void *p)
{
    if (!p)
        return;
    if (h->free)
        h->free(h->user, p);
    else
        free(p);
}

void *sa1_mem_alloc(const sa1_context *ctx, size_t size)
{
    void *p = host_alloc(&ctx->host, size);
    if (p)
        memset(p, 0, size ? size : 1);
    return p;
}

void *sa1_mem_array(const sa1_context *ctx, size_t n, size_t elem)
{
    if (elem && n > ((size_t)-1) / elem)
        return NULL;
    return sa1_mem_alloc(ctx, n * elem);
}

void sa1_mem_free(const sa1_context *ctx, void *p)
{
    host_free(&ctx->host, p);
}

void *sa1_host_mem_alloc(const sa1_host *h, size_t size)
{
    void *p = host_alloc(h, size);
    if (p)
        memset(p, 0, size ? size : 1);
    return p;
}

void sa1_host_mem_free(const sa1_host *h, void *p)
{
    host_free(h, p);
}

struct sa1_arena_block {
    sa1_arena_block *next;
    size_t           size;
    size_t           used;
};

#define ARENA_HDR   ((sizeof(sa1_arena_block) + 15u) & ~(size_t)15u)
#define ARENA_BLOCK ((size_t)256u << 10)

void *sa1_arena_alloc(sa1_arena *a, size_t size)
{
    sa1_arena_block *b = a->head;
    uint8_t *p;
    size = (size + 15u) & ~(size_t)15u;
    if (size == 0)
        size = 16;
    if (!b || b->size - b->used < size) {
        size_t bs = size > ARENA_BLOCK ? size : ARENA_BLOCK;
        if (bs > ((size_t)-1) - ARENA_HDR)
            return NULL;
        b = (sa1_arena_block *)host_alloc(&a->ctx->host, ARENA_HDR + bs);
        if (!b)
            return NULL;
        b->size = bs;
        b->used = 0;
        b->next = a->head;
        a->head = b;
        a->total += ARENA_HDR + bs;
    }
    p = (uint8_t *)b + ARENA_HDR + b->used;
    b->used += size;
    memset(p, 0, size);
    return p;
}

void *sa1_arena_array(sa1_arena *a, size_t n, size_t elem)
{
    if (elem && n > ((size_t)-1) / elem)
        return NULL;
    return sa1_arena_alloc(a, n * elem);
}

void sa1_arena_release(sa1_arena *a)
{
    sa1_arena_block *b = a->head;
    while (b) {
        sa1_arena_block *n = b->next;
        host_free(&a->ctx->host, b);
        b = n;
    }
    a->head = NULL;
    a->total = 0;
}

void *sa1_vec_push(const sa1_context *ctx, sa1_vec *v)
{
    uint8_t *p;
    if (v->count == v->cap) {
        uint32_t ncap = v->cap ? v->cap * 2u : 16u;
        void *nd;
        if (ncap < v->cap || ncap > (1u << 26))
            return NULL;
        nd = sa1_mem_array(ctx, ncap, v->elem);
        if (!nd)
            return NULL;
        if (v->data)
            memcpy(nd, v->data, (size_t)v->count * v->elem);
        sa1_mem_free(ctx, v->data);
        v->data = nd;
        v->cap = ncap;
    }
    p = (uint8_t *)v->data + (size_t)v->count * v->elem;
    memset(p, 0, v->elem);
    v->count++;
    return p;
}

void sa1_vec_free(const sa1_context *ctx, sa1_vec *v)
{
    sa1_mem_free(ctx, v->data);
    v->data = NULL;
    v->count = v->cap = 0;
}

/* ------------------------------------------------------------------------- */
/* Logging and errors                                                        */
/* ------------------------------------------------------------------------- */

static void vlog(const sa1_context *ctx, int32_t level, const char *fmt, va_list ap)
{
    char buf[1024];
    if (!ctx || !ctx->host.log || level < ctx->host.log_level)
        return;
    vsnprintf(buf, sizeof buf, fmt, ap);
    ctx->host.log(ctx->host.user, level, buf);
}

void sa1_log(const sa1_context *ctx, int32_t level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(ctx, level, fmt, ap);
    va_end(ap);
}

sa1_status sa1_fail(sa1_context *ctx, sa1_status st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->error, sizeof ctx->error, fmt, ap);
    va_end(ap);
    sa1_log(ctx, st < 0 ? SA1_LOG_ERROR : SA1_LOG_WARN, "%s", ctx->error);
    return st;
}

/* round 0.9.5 (board095_perf.md 4): ctx->error exactly as sa1_fail writes it, logged at the caller's level */
sa1_status sa1_refuse(sa1_context *ctx, sa1_status st, int32_t level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->error, sizeof ctx->error, fmt, ap);
    va_end(ap);
    sa1_log(ctx, level, "%s", ctx->error);
    return st;
}

const char *sa1_last_error(const sa1_context *ctx)
{
    return ctx ? ctx->error : "no context";
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

/* ------------------------------------------------------------------------- */
/* Host file wrappers                                                        */
/* ------------------------------------------------------------------------- */

void *sa1_file_open(const sa1_context *ctx, const char *path)
{
    void *f = ctx->host.file_open(ctx->host.user, path);
    sa1_api_ext *x = sa1_api_ext_of(ctx);
    /* sa1_load_disc opens its .gdi first (after sa1_characters_destroy_all armed the capture): the worlds
     * compose their memory images from the same disc */
    if (f && x->gdi_capture && path && strlen(path) < sizeof x->gdi) {
        memcpy(x->gdi, path, strlen(path) + 1);
        x->gdi_capture = 0;
    }
    return f;
}

sa1_status sa1_file_size(const sa1_context *ctx, void *f, uint64_t *size)
{
    return ctx->host.file_size(ctx->host.user, f, size) == 0 ? SA1_OK : SA1_ERR_IO;
}

sa1_status sa1_file_read(const sa1_context *ctx, void *f, uint64_t off, void *buf, size_t n)
{
    return ctx->host.file_read(ctx->host.user, f, off, buf, n) == 0 ? SA1_OK : SA1_ERR_IO;
}

void sa1_file_close(const sa1_context *ctx, void *f)
{
    if (f)
        ctx->host.file_close(ctx->host.user, f);
}

/* ------------------------------------------------------------------------- */
/* Context                                                                   */
/* ------------------------------------------------------------------------- */

/* every sa1_context is allocated by sa1_init together with the API's own state */
typedef struct sa1_core_ctx {
    sa1_context ctx;            /* first: a sa1_context pointer is a sa1_core_ctx pointer */
    sa1_api_ext ext;
} sa1_core_ctx;

sa1_api_ext *sa1_api_ext_of(const sa1_context *ctx)
{
    /* the extension is not part of the caller's view of a const context: it is the API's bookkeeping */
    return &((sa1_core_ctx *)(uintptr_t)ctx)->ext;
}

void sa1_host_defaults(sa1_host *host)
{
    if (!host)
        return;
    memset(host, 0, sizeof *host);
    host->struct_size = sizeof *host;
    host->api_version = SA1_API_VERSION;
    host->log_level = SA1_LOG_INFO;
}

sa1_status sa1_init(const sa1_host *host_in, sa1_context **out_ctx)
{
    sa1_host h;
    sa1_context *ctx;
    int nfile;

    if (!out_ctx)
        return SA1_ERR_INVALID_ARGUMENT;
    *out_ctx = NULL;
    sa1_host_defaults(&h);
    if (host_in) {
        size_t n = host_in->struct_size < sizeof h ? host_in->struct_size : sizeof h;
        if (host_in->struct_size < 2 * sizeof(uint32_t))
            return SA1_ERR_INVALID_ARGUMENT;
        memcpy(&h, host_in, n);
        h.struct_size = sizeof h;
        if ((h.api_version >> 16) != SA1_VERSION_MAJOR || (h.api_version & 0xFFFFu) > SA1_VERSION_MINOR)
            return SA1_ERR_VERSION;
    }
    if ((h.alloc == NULL) != (h.free == NULL))
        return SA1_ERR_INVALID_ARGUMENT;
    nfile = (h.file_open != NULL) + (h.file_size != NULL) + (h.file_read != NULL) + (h.file_close != NULL);
    if (nfile != 0 && nfile != 4)
        return SA1_ERR_INVALID_ARGUMENT;
    if (nfile == 0) {
        h.file_open = sa1_stdio_open;
        h.file_size = sa1_stdio_size;
        h.file_read = sa1_stdio_read;
        h.file_close = sa1_stdio_close;
    }

    ctx = (sa1_context *)host_alloc(&h, sizeof(sa1_core_ctx));
    if (!ctx)
        return SA1_ERR_OUT_OF_MEMORY;
    memset(ctx, 0, sizeof(sa1_core_ctx));
    ctx->host = h;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &ctx->coords);
    ctx->files.elem = sizeof(sa1_file_record);
    ctx->col_objects.elem = sizeof(sa1_col_object);
    ctx->next_col_id = 1;
    *out_ctx = ctx;
    sa1_log(ctx, SA1_LOG_DEBUG, "libsa1 %s context created", sa1_version_string());
    return SA1_OK;
}

sa1_context *sa1_worker_ctx_create(const sa1_context *ctx)
{
    sa1_core_ctx *c;
    if (!ctx)
        return NULL;
    c = (sa1_core_ctx *)host_alloc(&ctx->host, sizeof *c);
    if (!c)
        return NULL;
    memset(c, 0, sizeof *c);
    c->ctx.host = ctx->host;
    c->ctx.host.log = NULL;             /* workers never call the host's log */
    c->ctx.coords = ctx->coords;
    c->ctx.files.elem = sizeof(sa1_file_record);
    c->ctx.col_objects.elem = sizeof(sa1_col_object);
    c->ctx.next_col_id = 1;
    return &c->ctx;
}

void sa1_worker_ctx_destroy(sa1_context *wctx)
{
    sa1_host h;
    if (!wctx)
        return;
    sa1_vec_free(wctx, &wctx->files);
    h = wctx->host;
    host_free(&h, wctx);
}

void sa1_shutdown(sa1_context *ctx)
{
    sa1_host h;
    if (!ctx)
        return;
    sa1_unload_disc(ctx);
    sa1_collision_free(ctx);
    sa1_vec_free(ctx, &ctx->files);
    h = ctx->host;
    host_free(&h, ctx);
}

/* ------------------------------------------------------------------------- */
/* Coordinate system                                                         */
/* ------------------------------------------------------------------------- */

sa1_status sa1_coords_preset(int32_t preset, sa1_coords *out)
{
    static const float source_axis[9] = { 1, 0, 0, 0, 0, -1, 0, 1, 0 };
    static const float ident[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    if (!out)
        return SA1_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    out->struct_size = sizeof *out;
    switch (preset) {
    case SA1_COORDS_PRESET_SOURCE:
        out->scale = 4.0f;
        memcpy(out->axis, source_axis, sizeof source_axis);
        return SA1_OK;
    case SA1_COORDS_PRESET_NATIVE:
        out->scale = 1.0f;
        memcpy(out->axis, ident, sizeof ident);
        return SA1_OK;
    default:
        return SA1_ERR_INVALID_ARGUMENT;
    }
}

sa1_status sa1_set_coords(sa1_context *ctx, const sa1_coords *in)
{
    sa1_coords c;
    int i, j;
    if (!ctx || !in || in->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    memset(&c, 0, sizeof c);
    memcpy(&c, in, in->struct_size < sizeof c ? in->struct_size : sizeof c);
    c.struct_size = sizeof c;
    if (!sa1_finite(c.scale) || !(c.scale > 1e-6f) || !(c.scale < 1e6f))
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "coordinate scale must be finite and > 0");
    for (i = 0; i < 9; i++)
        if (!sa1_finite(c.axis[i]))
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "coordinate axis matrix is not finite");
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            float d = c.axis[3 * i] * c.axis[3 * j] + c.axis[3 * i + 1] * c.axis[3 * j + 1] +
                      c.axis[3 * i + 2] * c.axis[3 * j + 2];
            float want = i == j ? 1.0f : 0.0f;
            if (fabsf(d - want) > 1e-4f)
                return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "coordinate axis matrix is not orthonormal");
        }
    if (c.flags & ~SA1_COORDS_FLIP_WINDING)
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "unknown coordinate flags 0x%X", (unsigned)c.flags);
    ctx->coords = c;
    return SA1_OK;
}

sa1_status sa1_get_coords(const sa1_context *ctx, sa1_coords *out)
{
    if (!ctx || !out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    sa1_copy_out(out, &ctx->coords, sizeof ctx->coords);
    return SA1_OK;
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

void sa1_from_host_dir(const sa1_context *ctx, const float in[3], float out[3])
{
    const float *a = ctx->coords.axis;  /* inverse of an orthonormal matrix = transpose */
    float x = in[0], y = in[1], z = in[2];
    out[0] = a[0] * x + a[3] * y + a[6] * z;
    out[1] = a[1] * x + a[4] * y + a[7] * z;
    out[2] = a[2] * x + a[5] * y + a[8] * z;
}

void sa1_from_host_point(const sa1_context *ctx, const float in[3], float out[3])
{
    float s = 1.0f / ctx->coords.scale;
    sa1_from_host_dir(ctx, in, out);
    out[0] *= s;
    out[1] *= s;
    out[2] *= s;
}

/* ------------------------------------------------------------------------- */
/* Sound events                                                              */
/* ------------------------------------------------------------------------- */

sa1_status sa1_poll_sound_events(sa1_context *ctx, sa1_sound_event *out, uint32_t capacity, uint32_t *count)
{
    uint32_t n = 0;
    if (!ctx || !count || (capacity && !out))
        return SA1_ERR_INVALID_ARGUMENT;
    while (n < capacity && ctx->sound_count) {
        out[n++] = ctx->sounds[ctx->sound_head];
        ctx->sound_head = (ctx->sound_head + 1u) % SA1_SOUND_QUEUE;
        ctx->sound_count--;
    }
    *count = n;
    return SA1_OK;
}