/*
 * sa1_hostobj.c - host objects, the context side (API 0.3; sadc docs/libsa1_interaction.md 2.2 H1 with the addendum,
 * section 6): the registry of up to 1024 objects per context, the descriptions' checks, the public calls that change it,
 * and the calls that read the worlds (events, counts). The proxies live in each character's world
 * (src/world/sa1_world_hostobj.c), which reads the registry at every pair start.
 *
 * Where the registry lives: a process table keyed by the context (a short spin lock guards the table, never a world),
 * created by the first sa1_hostobj_add / sa1_hostobj_configure and freed by sa1_collision_free at sa1_shutdown, so the
 * context's own layout (sa1_internal.h) does not change. The registry keeps every description in host space as given:
 * the conversion to SA1 space is the worlds' (their floating-point mode; the asset layer is not built with the world's
 * FP flags, and on x86 it may use the x87 unit).
 *
 * Ids: generation << 11 | (entry + 1), never 0; a freed entry goes to the back of a FIFO, so the id of a removed object
 * stays recognisable (entry.removed_id / removed_how) for as long as possible: a world that still holds a proxy of it
 * learns at its next pair whether it was removed KILLED or SILENT.
 *
 * Threading: like sa1_collision_object_*, these calls belong to the thread that ticks the context's characters.
 */
#include "sa1_hostobj.h"
#include "sa1_api_world.h"
#include "sa1_screen.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define SA1HO_TABLE 64u                     /* contexts with host objects per process */
#define SA1HO_MAX_SA1 1000.0f               /* SA1 units: sizes and offsets (design 2.2) */
#define SA1HO_MAX_YAW 10000.0f
#define SA1HO_DAMAGE_QUEUE 4u               /* sa1_world.h SA1G_HO_DAMAGE_MAX (the asset layer has no world headers) */

static struct {
    const sa1_context *ctx;
    sa1ho_registry *r;
} g_tab[SA1HO_TABLE];
static volatile int32_t g_lock;

static void tab_lock(void)
{
    while (sa1_atomic_add32(&g_lock, 1) != 1) {
        sa1_atomic_add32(&g_lock, -1);
    }
}

static void tab_unlock(void)
{
    sa1_atomic_add32(&g_lock, -1);
}

sa1ho_registry *sa1ho_registry_of(const sa1_context *ctx)
{
    sa1ho_registry *r = NULL;
    uint32_t i;
    if (ctx == NULL) {
        return NULL;
    }
    tab_lock();
    for (i = 0; i < SA1HO_TABLE; i++) {
        if (g_tab[i].ctx == ctx) {
            r = g_tab[i].r;
            break;
        }
    }
    tab_unlock();
    return r;
}

/* the registry of ctx, created on first use */
static sa1_status registry_get(sa1_context *ctx, sa1ho_registry **out)
{
    sa1ho_registry *r = sa1ho_registry_of(ctx);
    uint32_t i, slot = SA1HO_TABLE;
    *out = r;
    if (r != NULL) {
        return SA1_OK;
    }
    r = (sa1ho_registry *)sa1_mem_alloc(ctx, sizeof *r);
    if (r == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    r->magic = SA1HO_MAGIC;
    r->ctx = ctx;
    r->window = SA1_HOSTOBJ_MAX_WINDOW;
    tab_lock();
    for (i = 0; i < SA1HO_TABLE; i++) {
        if (g_tab[i].ctx == NULL && slot == SA1HO_TABLE) {
            slot = i;
        }
    }
    if (slot < SA1HO_TABLE) {
        g_tab[slot].ctx = ctx;
        g_tab[slot].r = r;
    }
    tab_unlock();
    if (slot == SA1HO_TABLE) {
        sa1_mem_free(ctx, r);
        return sa1_fail(ctx, SA1_ERR_LIMIT, "host objects: more than %u contexts with host objects in this process",
                        (unsigned)SA1HO_TABLE);
    }
    *out = r;
    return SA1_OK;
}

void sa1ho_context_free(sa1_context *ctx)
{
    sa1ho_registry *r = NULL;
    uint32_t i;
    if (ctx == NULL) {
        return;
    }
    tab_lock();
    for (i = 0; i < SA1HO_TABLE; i++) {
        if (g_tab[i].ctx == ctx) {
            r = g_tab[i].r;
            g_tab[i].ctx = NULL;
            g_tab[i].r = NULL;
        }
    }
    tab_unlock();
    if (r != NULL) {
        r->magic = 0;
        sa1_mem_free(ctx, r);
    }
}

/* ---- checks ----------------------------------------------------------------------------------------------------- */
static int fin3(const float v[3])
{
    return sa1_finite(v[0]) && sa1_finite(v[1]) && sa1_finite(v[2]);
}

/* a host length in SA1 units is at most SA1HO_MAX_SA1 */
static int len_ok(const sa1_context *ctx, float v)
{
    return sa1_finite(v) && fabsf(v) <= SA1HO_MAX_SA1 * ctx->coords.scale;
}

/* the caller's description, completed to this library's size; SA1_OK or an error recorded with sa1_fail */
static sa1_status desc_take(sa1_context *ctx, const sa1_hostobj_desc *in, sa1_hostobj_desc *d)
{
    static const uint32_t known = SA1_HOSTOBJ_SOLID | SA1_HOSTOBJ_NOT_SOLID | SA1_HOSTOBJ_NO_HOMING |
                                  SA1_HOSTOBJ_NO_LOCKON | SA1_HOSTOBJ_NO_BONUS | SA1_HOSTOBJ_DISABLED |
                                  SA1_HOSTOBJ_REPORT_TOUCH | SA1_HOSTOBJ_HOLD_HITS | SA1_HOSTOBJ_HOLD_LOCKED |
                                  SA1_HOSTOBJ_HEAVY | SA1_HOSTOBJ_POP_ON_TOUCH | SA1_HOSTOBJ_LAUNCH_BOUNCE |
                                  SA1_HOSTOBJ_KEEP_HSPEED;
    uint32_t i, k;
    if (in == NULL || in->struct_size < SA1HO_DESC_MIN) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: struct_size %u (at least %u)",
                        in ? (unsigned)in->struct_size : 0u, (unsigned)SA1HO_DESC_MIN);
    }
    memset(d, 0, sizeof *d);
    memcpy(d, in, in->struct_size < sizeof *d ? in->struct_size : sizeof *d);
    d->struct_size = sizeof *d;
    if (d->kind < SA1_HOSTOBJ_ENEMY || d->kind > SA1_HOSTOBJ_BALLOON) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: unknown kind %u", (unsigned)d->kind);
    }
    if (d->flags & ~known) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: unknown flags 0x%X",
                        (unsigned)(d->flags & ~known));
    }
    if (!fin3(d->position) || fabsf(d->position[0]) > 1e9f || fabsf(d->position[1]) > 1e9f ||
        fabsf(d->position[2]) > 1e9f || !sa1_finite(d->yaw) || fabsf(d->yaw) > SA1HO_MAX_YAW) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: position or yaw not finite (or too large)");
    }
    if (d->armor < -1 || d->armor > 3 || d->touch < -1 || d->touch > 3) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: armor %d / touch %d (-1..3)", (int)d->armor,
                        (int)d->touch);
    }
    if (d->cooldown_steps > 3600u) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: cooldown_steps %u (at most 3600)",
                        (unsigned)d->cooldown_steps);
    }
    if (d->num_shapes < 1u || d->num_shapes > SA1_HOSTOBJ_MAX_SHAPES) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: %u shapes (1..%u)", (unsigned)d->num_shapes,
                        (unsigned)SA1_HOSTOBJ_MAX_SHAPES);
    }
    for (i = 0; i < d->num_shapes; i++) {
        const sa1_hostobj_shape *s = &d->shapes[i];
        uint32_t need = s->form == SA1_SHAPE_SPHERE ? 1u : s->form == SA1_SHAPE_CYLINDER ? 2u : 3u;
        if (s->form != SA1_SHAPE_SPHERE && s->form != SA1_SHAPE_CYLINDER && s->form != SA1_SHAPE_BOX) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: shape %u: form %u (sphere 0, cylinder 1, "
                            "box 4)", (unsigned)i, (unsigned)s->form);
        }
        if (s->flags & ~(SA1_HSHAPE_NO_DAMAGE | SA1_HSHAPE_NO_TOUCH | SA1_HSHAPE_OFF)) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: shape %u: unknown flags 0x%X", (unsigned)i,
                            (unsigned)s->flags);
        }
        for (k = 0; k < 3; k++) {
            if (!len_ok(ctx, s->center[k]) || !len_ok(ctx, s->size[k]) || (k < need && !(s->size[k] > 0.0f))) {
                return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: shape %u: centre / size not finite, "
                                "not positive or beyond %.0f SA1 units", (unsigned)i, (double)SA1HO_MAX_SA1);
            }
        }
        if (!sa1_finite(s->yaw) || fabsf(s->yaw) > SA1HO_MAX_YAW) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: shape %u: yaw", (unsigned)i);
        }
    }
    for (; i < SA1_HOSTOBJ_MAX_SHAPES; i++) {
        memset(&d->shapes[i], 0, sizeof d->shapes[i]);
    }
    /* round 5 tails (zero when the caller's struct is the 0.3 core) */
    if (!sa1_finite(d->launch_speed) || d->launch_speed < 0.0f || !len_ok(ctx, d->launch_speed / 60.0f) ||
        !sa1_finite(d->grab_reach) || d->grab_reach < 0.0f || !len_ok(ctx, d->grab_reach)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: launch_speed / grab_reach not finite, negative "
                        "or too large");
    }
    if (d->kind == SA1_HOSTOBJ_HOLDABLE) {
        /* the body from shape 0 (6.1.2): its horizontal radius at most 32 SA1 units (128 HU in the Source preset) */
        const sa1_hostobj_shape *s = &d->shapes[0];
        double r2 = 0.0, lim = 32.0 * (double)ctx->coords.scale;
        if (s->form == SA1_SHAPE_BOX) {
            const float *a = ctx->coords.axis;
            uint32_t j;
            double e[3];
            for (j = 0; j < 3; j++) {   /* the half extents along the SA1 axes (host units) */
                e[j] = fabs((double)a[j]) * s->size[0] + fabs((double)a[3 + j]) * s->size[1] +
                       fabs((double)a[6 + j]) * s->size[2];
            }
            r2 = e[0] * e[0] + e[2] * e[2];
        } else {
            r2 = (double)s->size[0] * (double)s->size[0];
        }
        if (r2 > lim * lim) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_desc: a HOLDABLE body wider than 32 SA1 units");
        }
    }
    return SA1_OK;
}

static sa1ho_entry *entry_mut(sa1ho_registry *r, uint32_t id)
{
    return (sa1ho_entry *)(uintptr_t)sa1ho_entry_of(r, id);
}

/* ---- the public calls --------------------------------------------------------------------------------------------- */
sa1_status sa1_hostobj_add(sa1_context *ctx, const sa1_hostobj_desc *desc, uint32_t *out_id)
{
    sa1ho_registry *r;
    sa1ho_entry *e;
    sa1_hostobj_desc d;
    uint32_t idx;
    sa1_status st;
    if (out_id != NULL) {
        *out_id = 0;
    }
    if (ctx == NULL || out_id == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = desc_take(ctx, desc, &d);
    if (st != SA1_OK) {
        return st;
    }
    st = registry_get(ctx, &r);
    if (st != SA1_OK) {
        return st;
    }
    if (r->count >= SA1HO_MAX) {
        return sa1_fail(ctx, SA1_ERR_LIMIT, "host objects: %u per context", (unsigned)SA1HO_MAX);
    }
    if (r->hiwater < SA1HO_MAX) {
        idx = r->hiwater++;
    } else {
        idx = r->freeq[r->free_head];
        r->free_head = (r->free_head + 1u) % SA1HO_MAX;
        r->free_count--;
    }
    e = &r->ent[idx];
    e->gen = (e->gen + 1u) & ((1u << (32u - SA1HO_IDX_BITS)) - 1u);
    if (e->gen == 0) {
        e->gen = 1;
    }
    e->id = (e->gen << SA1HO_IDX_BITS) | (idx + 1u);
    e->desc = d;
    e->desc_version++;
    e->pose_version++;
    r->count++;
    *out_id = e->id;
    return SA1_OK;
}

sa1_status sa1_hostobj_update(sa1_context *ctx, uint32_t id, const sa1_hostobj_desc *desc)
{
    sa1ho_entry *e;
    sa1_hostobj_desc d;
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    e = entry_mut(sa1ho_registry_of(ctx), id);
    if (e == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    st = desc_take(ctx, desc, &d);
    if (st != SA1_OK) {
        return st;
    }
    e->desc = d;
    e->desc_version++;
    e->pose_version++;
    return SA1_OK;
}

sa1_status sa1_hostobj_move(sa1_context *ctx, uint32_t id, const float position[3], float yaw)
{
    sa1ho_entry *e;
    if (ctx == NULL || position == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    e = entry_mut(sa1ho_registry_of(ctx), id);
    if (e == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    if (!fin3(position) || fabsf(position[0]) > 1e9f || fabsf(position[1]) > 1e9f || fabsf(position[2]) > 1e9f ||
        !sa1_finite(yaw) || fabsf(yaw) > SA1HO_MAX_YAW) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (memcmp(e->desc.position, position, sizeof e->desc.position) != 0 || e->desc.yaw != yaw) {
        memcpy(e->desc.position, position, sizeof e->desc.position);
        e->desc.yaw = yaw;
        e->pose_version++;
    }
    return SA1_OK;
}

sa1_status sa1_hostobj_remove(sa1_context *ctx, uint32_t id, uint32_t how)
{
    sa1ho_registry *r;
    sa1ho_entry *e;
    uint32_t idx;
    if (ctx == NULL || (how != SA1_HOSTOBJ_REMOVE_SILENT && how != SA1_HOSTOBJ_REMOVE_KILLED)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    r = sa1ho_registry_of(ctx);
    e = entry_mut(r, id);
    if (e == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    idx = (uint32_t)(e - r->ent);
    e->removed_id = id;
    e->removed_how = how;
    e->id = 0;
    r->count--;
    r->freeq[(r->free_head + r->free_count) % SA1HO_MAX] = (uint16_t)idx;
    r->free_count++;
    return SA1_OK;
}

sa1_status sa1_hostobj_configure(sa1_context *ctx, const sa1_hostobj_config *cfg)
{
    sa1_hostobj_config c;
    sa1ho_registry *r;
    sa1_status st;
    if (ctx == NULL || cfg == NULL || cfg->struct_size < sizeof(uint32_t)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    memset(&c, 0, sizeof c);
    memcpy(&c, cfg, cfg->struct_size < sizeof c ? cfg->struct_size : sizeof c);
    if (c.window > SA1_HOSTOBJ_MAX_WINDOW || !sa1_finite(c.enter_radius) || !sa1_finite(c.leave_radius) ||
        c.enter_radius < 0.0f || c.leave_radius < 0.0f || c.enter_radius > 100000.0f * ctx->coords.scale ||
        c.leave_radius > 100000.0f * ctx->coords.scale) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_config: window 0..%u, radii finite, >= 0",
                        (unsigned)SA1_HOSTOBJ_MAX_WINDOW);
    }
    st = registry_get(ctx, &r);
    if (st != SA1_OK) {
        return st;
    }
    r->window = c.window ? c.window : SA1_HOSTOBJ_MAX_WINDOW;
    r->enter_radius = c.enter_radius;
    r->leave_radius = c.leave_radius;
    r->config_version++;
    return SA1_OK;
}

/* ---- round 5: release, world mask, damage, options, held ----------------------------------------------------------- */
sa1_status sa1_hostobj_release(sa1_context *ctx, uint32_t id)
{
    sa1ho_entry *e;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    e = entry_mut(sa1ho_registry_of(ctx), id);
    if (e == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    if (e->desc.kind != SA1_HOSTOBJ_HOLDABLE) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_release: object %u is not a HOLDABLE", (unsigned)id);
    }
    e->release_seq++;
    return SA1_OK;
}

static int character_exists(const sa1_context *ctx, sa1_character_id id);

/* the context's record of a character's settings (created with the registry) */
static sa1_status charset_of(sa1_context *ctx, sa1_character_id id, sa1ho_charset **out)
{
    sa1ho_registry *r;
    sa1ho_charset *cs;
    sa1_status st = registry_get(ctx, &r);
    *out = NULL;
    if (st != SA1_OK) {
        return st;
    }
    cs = &r->chars[((uint32_t)id & 0xFFFFu) - 1u];
    if (cs->id != id) {
        memset(cs, 0, sizeof *cs);
        cs->id = id;
    }
    *out = cs;
    return SA1_OK;
}

sa1_status sa1_hostobj_world_mask(sa1_context *ctx, sa1_character_id id, uint32_t kind_mask)
{
    static const uint32_t kinds = SA1_HOSTOBJ_KIND_BIT(SA1_HOSTOBJ_ENEMY) | SA1_HOSTOBJ_KIND_BIT(SA1_HOSTOBJ_TARGET) |
                                  SA1_HOSTOBJ_KIND_BIT(SA1_HOSTOBJ_BREAKABLE) | SA1_HOSTOBJ_KIND_BIT(SA1_HOSTOBJ_HAZARD) |
                                  SA1_HOSTOBJ_KIND_BIT(SA1_HOSTOBJ_HOLDABLE) | SA1_HOSTOBJ_KIND_BIT(SA1_HOSTOBJ_BALLOON);
    sa1ho_charset *cs;
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (!character_exists(ctx, id)) {
        return SA1_ERR_NOT_FOUND;
    }
    if (kind_mask & ~kinds) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_world_mask: unknown kind bits 0x%X",
                        (unsigned)(kind_mask & ~kinds));
    }
    st = charset_of(ctx, id, &cs);
    if (st != SA1_OK) {
        return st;
    }
    cs->set |= SA1HO_SET_MASK;
    cs->mask = kind_mask;
#if !defined(SA1_NO_WORLD)
    sa1ho_world_settings(sa1_character_world(ctx, id), SA1HO_SET_MASK, 0, kind_mask);
#endif
    return SA1_OK;
}

sa1_status sa1_character_set_option(sa1_context *ctx, sa1_character_id id, uint32_t option, int32_t value)
{
    sa1ho_charset *cs;
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (!character_exists(ctx, id)) {
        return SA1_ERR_NOT_FOUND;
    }
#if !defined(SA1H_HOSTOBJ)
    /* round 0.9.1: the clock and HUD options (src/sa1_screen.c). Not in sadc's harness, whose host-object oracle compiles
       this file and src/world/sa1_world_hostobj.c without the screen layer (harness/build.py LIB_HOSTOBJ_SOURCES) */
    if (option >= SA1_COPT_CLOCK && option <= SA1_COPT_HUD_PARTS) {
        return sa1_screen_set_option(ctx, id, option, value);
    }
#endif
    if (option < SA1_COPT_NO_DEATH || option > SA1_COPT_NO_DAMAGE) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_character_set_option: unknown option %u", (unsigned)option);
    }
    st = charset_of(ctx, id, &cs);
    if (st != SA1_OK) {
        return st;
    }
    cs->set |= SA1HO_SET_OPTS;
    cs->opts = value ? (cs->opts | (1u << option)) : (cs->opts & ~(1u << option));
#if !defined(SA1_NO_WORLD)
    sa1ho_world_settings(sa1_character_world(ctx, id), SA1HO_SET_OPTS, cs->opts, 0);
#endif
    return SA1_OK;
}

sa1_status sa1_character_damage(sa1_context *ctx, sa1_character_id id, const sa1_damage *d)
{
    sa1_damage x;
    if (ctx == NULL || d == NULL || d->struct_size < offsetof(sa1_damage, source)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (!character_exists(ctx, id)) {
        return SA1_ERR_NOT_FOUND;
    }
    memset(&x, 0, sizeof x);
    memcpy(&x, d, d->struct_size < sizeof x ? d->struct_size : sizeof x);
    if (x.flags & ~(SA1_DAMAGE_FACE_SOURCE | SA1_DAMAGE_KILL)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_damage: unknown flags 0x%X",
                        (unsigned)(x.flags & ~(SA1_DAMAGE_FACE_SOURCE | SA1_DAMAGE_KILL)));
    }
    if (!fin3(x.source) || fabsf(x.source[0]) > 1e9f || fabsf(x.source[1]) > 1e9f || fabsf(x.source[2]) > 1e9f) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_damage: source not finite (or too large)");
    }
#if !defined(SA1_NO_WORLD)
    {
        struct sa1g_world *w = sa1_character_world(ctx, id);
        sa1_status st;
        if (w == NULL) {
            return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "sa1_character_damage: character %d has no world yet", (int)id);
        }
        st = sa1ho_world_damage(w, &x);
        if (st != SA1_OK) {
            return sa1_fail(ctx, st, "sa1_character_damage: %u requests are pending for character %d",
                            (unsigned)SA1HO_DAMAGE_QUEUE, (int)id);
        }
        return SA1_OK;
    }
#else
    return SA1_ERR_NOT_IMPLEMENTED;
#endif
}

sa1_status sa1_character_held(sa1_context *ctx, sa1_character_id id, sa1_hostobj_held *out)
{
    sa1_hostobj_held h;
    if (ctx == NULL || out == NULL || out->struct_size < sizeof(uint32_t)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (!character_exists(ctx, id)) {
        return SA1_ERR_NOT_FOUND;
    }
    memset(&h, 0, sizeof h);
    h.struct_size = sizeof h;
#if !defined(SA1_NO_WORLD)
    sa1ho_world_held(ctx, sa1_character_world(ctx, id), &h);
#endif
    sa1_copy_out(out, &h, sizeof h);
    return SA1_OK;
}

/* the character of an id (NULL: none), without the private sa1_char of sa1_character.c */
static int character_exists(const sa1_context *ctx, sa1_character_id id)
{
    uint32_t slot = ((uint32_t)id & 0xFFFFu) - 1u, gen = ((uint32_t)id >> 16) & 0x7FFFu;
    return id > 0 && slot < SA1_MAX_INSTANCES && ctx->inst[slot] != NULL && ctx->inst_gen[slot] == gen;
}

sa1_status sa1_poll_hostobj_events(sa1_context *ctx, sa1_hostobj_event *out, uint32_t capacity, uint32_t *count)
{
    uint32_t n = 0, i;
    if (ctx == NULL || count == NULL || (capacity != 0 && out == NULL)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
#if !defined(SA1_NO_WORLD)
    for (i = 0; i < SA1_MAX_INSTANCES && n < capacity; i++) {
        sa1_character_id id;
        struct sa1g_world *w;
        uint32_t got, k;
        if (ctx->inst[i] == NULL) {
            continue;
        }
        id = (sa1_character_id)(((uint32_t)ctx->inst_gen[i] << 16) | (i + 1u));
        w = sa1_character_world(ctx, id);
        if (w == NULL) {
            continue;
        }
        got = sa1ho_world_poll(ctx, w, out + n, capacity - n);
        for (k = 0; k < got; k++) {
            out[n + k].character = id;
        }
        n += got;
    }
#else
    (void)i;
#endif
    *count = n;
    return SA1_OK;
}

sa1_status sa1_hostobj_info_get(sa1_context *ctx, sa1_character_id id, sa1_hostobj_info *out)
{
    sa1_hostobj_info info;
    const sa1ho_registry *r;
    if (ctx == NULL || out == NULL || out->struct_size < sizeof(uint32_t) || id < 0) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    if (id > 0) {
        if (!character_exists(ctx, id)) {
            return SA1_ERR_NOT_FOUND;
        }
#if !defined(SA1_NO_WORLD)
        {
            struct sa1g_world *w = sa1_character_world(ctx, id);
            if (w != NULL) {
                sa1ho_world_info(w, &info);
            }
        }
#endif
    }
    r = sa1ho_registry_of(ctx);
    info.objects = r ? r->count : 0u;
    sa1_copy_out(out, &info, sizeof info);
    return SA1_OK;
}
