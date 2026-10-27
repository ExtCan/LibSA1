/*
 * sa1_set.c - SET objects, the context side (API SA1_API_SET, round 0.9.4; sadc docs/research/gsa1_094_plan.md 6.3 tasks
 * 1-2 with its Review, v1_arch.md 5.1 / 5.6 with its Review, gsa1_090_rings.md 4.1, gsa1_094_items.md 1, 2 and 6.8): the
 * catalogue (gen/objtypes.tsv), the registry of up to SA1_SET_MAX_RECORDS records per context, the records' checks, the
 * public calls that change it, and the event poll that reads the worlds. The worlds run the records themselves
 * (src/world/sa1_world_set.c reads the registry at every pair start).
 *
 * Where the registry lives: a process table keyed by the context (a short spin lock guards the table, never a world), as
 * the host objects' registry (sa1_hostobj.c), created by the first sa1_set_add / sa1_set_configure and freed by
 * sa1_collision_free at sa1_shutdown, so the context's own layout does not change. Records are kept in host space as given:
 * the conversion to SA1 space is the worlds' (their floating-point mode).
 *
 * Ids: generation << SA1SET_IDX_BITS (15) | (entry + 1), never 0; a freed entry goes to the back of a FIFO and is reused only once every
 * entry has been used once, so a removed record's id stays unknown to the registry for as long as possible.
 *
 * Every sa1_set_state call is a new state version, also when the values do not change: a host that restores a record
 * (a server refused a world's collection) makes every world apply it again; a world in which it is already so changes
 * nothing.
 *
 * Threading: like sa1_hostobj_*, these calls belong to the thread that ticks the context's characters.
 */
#include "sa1_internal.h"
#include "sa1_api_world.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define SA1SET_TABLE 64u                    /* contexts with SET records per process */
#define SA1SET_MAX_SA1 100000.0f            /* SA1 units: the farthest record from the origin */
#define SA1SET_GEN_MASK ((1u << (32u - SA1SET_IDX_BITS)) - 1u)

/* ---- the catalogue (gen/objtypes.tsv: keep the two in step; tests/set_test.c compares them) ----------------------- */
/* range: the spawner 8C091380's default for list attribute 0 (pool L8C0914AC = 160,000 = 400^2; the condition gets
 * 160,100). Models: the ring's NJS_OBJECT 0x8C1A2204 (its model D_8C1A21DC; a ring group's children draw it too), Item_Box
 * 0x8C1AC96C, Item_Box_Air 0x8C1B87BC. Rotation: objexec_RING turns its model about Y only (code_8C0869E0.c func_8C086D00);
 * O_GRING places its rings with njRotateZ / X / Y (code_8C087B80.c func_8C087EEE / func_8C088000); the ground box draws
 * upright (staged code_8C05D0A0.c func_8C05D56E); the air box with njRotateZ / X / Y (code_8C05DC60.c). */
static const sa1set_type k_types[SA1SET_NTYPES] = {
    { SA1_OBJTYPE_RING, "RING", 0x8C0869E8u, 2u, 3u, 0u, 400.0f, SA1_OBJCLASS_CONSUMABLE, 0x8C1A2204u, SA1_SET_ROT_Y,
      SA1_SET_STATE_PRESENT, SA1_SET_BUDGET_RING },
    { SA1_OBJTYPE_O_GRING, "O GRING", 0x8C087B80u, 2u, 3u, 0u, 400.0f, SA1_OBJCLASS_CONSUMABLE, 0x8C1A2204u,
      SA1_SET_ROT_ZXY, SA1_SET_STATE_PRESENT, SA1_SET_BUDGET_RING },
    { SA1_OBJTYPE_ITEMBOX, "O ITEMBOX", 0x8C05D10Cu, 2u, 3u, 0u, 400.0f, SA1_OBJCLASS_CONSUMABLE, 0x8C1AC96Cu,
      SA1_SET_ROT_NONE, SA1_SET_STATE_PRESENT | SA1_SET_STATE_BROKEN, SA1_SET_BUDGET_BOX },
    { SA1_OBJTYPE_ITEMBOX_AIR, "O ItemBoxAir", 0x8C05D9F6u, 15u, 3u, 0u, 400.0f, SA1_OBJCLASS_CONSUMABLE, 0x8C1B87BCu,
      SA1_SET_ROT_ZXY, SA1_SET_STATE_PRESENT, SA1_SET_BUDGET_BOX },
};

const sa1set_type *sa1set_type_of(uint32_t type)
{
    return type >= 1u && type <= SA1SET_NTYPES ? &k_types[type - 1u] : NULL;
}

/* ---- the process table ------------------------------------------------------------------------------------------- */
static struct {
    const sa1_context *ctx;
    sa1set_registry *r;
} g_tab[SA1SET_TABLE];
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

sa1set_registry *sa1set_registry_of(const sa1_context *ctx)
{
    sa1set_registry *r = NULL;
    uint32_t i;
    if (ctx == NULL) {
        return NULL;
    }
    tab_lock();
    for (i = 0; i < SA1SET_TABLE; i++) {
        if (g_tab[i].ctx == ctx) {
            r = g_tab[i].r;
            break;
        }
    }
    tab_unlock();
    return r;
}

static sa1_status registry_get(sa1_context *ctx, sa1set_registry **out)
{
    sa1set_registry *r = sa1set_registry_of(ctx);
    uint32_t i, slot = SA1SET_TABLE;
    *out = r;
    if (r != NULL) {
        return SA1_OK;
    }
    r = (sa1set_registry *)sa1_mem_alloc(ctx, sizeof *r);
    if (r == NULL) {
        return sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "SET objects: cannot allocate the registry (%u bytes)",
                        (unsigned)sizeof *r);
    }
    r->magic = SA1SET_MAGIC;
    r->ctx = ctx;
    r->ring_window = 64u;
    r->box_window = 32u;
    r->margin = 100.0f;
    r->config_version = 1u;
    r->range = 0.0f;                    /* round 0.9.4b: the game's 400 */
    r->view_tan = 0.0f;                 /* the game's screen */
    r->tier_hold = -1;                  /* SA1_SET_TIER_HOLD */
    tab_lock();
    for (i = 0; i < SA1SET_TABLE; i++) {
        if (g_tab[i].ctx == NULL && slot == SA1SET_TABLE) {
            slot = i;
        }
    }
    if (slot < SA1SET_TABLE) {
        g_tab[slot].ctx = ctx;
        g_tab[slot].r = r;
    }
    tab_unlock();
    if (slot == SA1SET_TABLE) {
        sa1_mem_free(ctx, r);
        return sa1_fail(ctx, SA1_ERR_LIMIT, "SET objects: more than %u contexts with records in this process",
                        (unsigned)SA1SET_TABLE);
    }
    *out = r;
    return SA1_OK;
}

void sa1set_context_free(sa1_context *ctx)
{
    sa1set_registry *r = NULL;
    uint32_t i;
    if (ctx == NULL) {
        return;
    }
    tab_lock();
    for (i = 0; i < SA1SET_TABLE; i++) {
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

/* ---- the catalogue API --------------------------------------------------------------------------------------------- */
static int type_available(const sa1set_registry *r, const sa1set_type *t)
{
    if (r != NULL && (r->unavailable & (1u << t->type))) {
        return 0;
    }
#if defined(SA1_NO_WORLD)
    return 0;
#else
    return sa1w_set_exec_native(t->exec);
#endif
}

uint32_t sa1_object_count(const sa1_context *ctx)
{
    (void)ctx;
    return SA1SET_NTYPES;
}

sa1_status sa1_object_info_get(const sa1_context *ctx, uint32_t type, sa1_object_info *out)
{
    const sa1set_type *t = sa1set_type_of(type);
    sa1_object_info info;
    if (ctx == NULL || out == NULL || out->struct_size < sizeof(uint32_t)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (t == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.type = t->type;
    info.name = t->name;
    info.exec = t->exec;
    info.im = t->im;
    info.level = t->level;
    info.list_flags = t->attr;
    {
        /* round 0.9.4b: the context's spawner range (sa1_set_config.range above the game's 400) */
        const sa1set_registry *R = sa1set_registry_of(ctx);
        info.range = (R != NULL && R->range > t->range ? R->range : t->range) * ctx->coords.scale;
    }
    info.cls = t->cls;
    info.model = t->model;
    info.rotation_order = t->rotation_order;
    info.available = (uint32_t)type_available(sa1set_registry_of(ctx), t);
    info.states = t->states;
    info.budget = t->budget;
    sa1_copy_out(out, &info, sizeof info);
    return SA1_OK;
}

/* ---- records ------------------------------------------------------------------------------------------------------- */
static int fin(float f)
{
    return sa1_finite(f);
}

static int is_int(float f, float lo, float hi)
{
    return fin(f) && f >= lo && f <= hi && (float)(int32_t)f == f;
}

uint32_t sa1set_cost(const sa1_set_record *r)
{
    const sa1set_type *t = r != NULL ? sa1set_type_of(r->type) : NULL;
    int32_t n;
    if (t == NULL) {
        return 0;
    }
    if (r->type != SA1_OBJTYPE_O_GRING) {
        return 1u;
    }
    /* objexec_O_GRING mode 0: i = 0 .. (Sint32)scl.x; a circle (scl.z 1) skips i = 0 */
    n = fin(r->params[0]) ? (int32_t)r->params[0] : 0;
    n = n < 0 ? 0 : n;
    return r->params[2] == 1.0f ? (uint32_t)n : (uint32_t)n + 1u;
}

/* the caller's record, completed to this library's size and checked; SA1_OK or an error recorded with sa1_fail */
static sa1_status rec_take(sa1_context *ctx, const sa1set_registry *R, const sa1_set_record *in, sa1_set_record *r)
{
    static const float ident[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    const sa1set_type *t;
    float lim;
    uint32_t i, j, zero = 1;
    if (in == NULL || in->struct_size < offsetof(sa1_set_record, user)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_record: struct_size %u (at least %u)",
                        in != NULL ? (unsigned)in->struct_size : 0u, (unsigned)offsetof(sa1_set_record, user));
    }
    memset(r, 0, sizeof *r);
    memcpy(r, in, in->struct_size < sizeof *r ? in->struct_size : sizeof *r);
    r->struct_size = sizeof *r;
    t = sa1set_type_of(r->type);
    if (t == NULL) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_record: unknown type %u (1..%u)", (unsigned)r->type,
                        (unsigned)SA1SET_NTYPES);
    }
    if (r->flags != 0) {
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "sa1_set_record: flags 0x%X (none in this version)", (unsigned)r->flags);
    }
    if (!type_available(R, t)) {
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "sa1_set_record: type %u (%s): its code (%08X) is not in this build",
                        (unsigned)t->type, t->name, (unsigned)t->exec);
    }
    lim = SA1SET_MAX_SA1 * ctx->coords.scale;
    for (i = 0; i < 3; i++) {
        if (!fin(r->position[i]) || fabsf(r->position[i]) > lim) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_record: position[%u] %g (finite, within %g host units)",
                            (unsigned)i, (double)r->position[i], (double)lim);
        }
        if (!fin(r->params[i]) || fabsf(r->params[i]) > 1000000.0f) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_record: params[%u] %g", (unsigned)i,
                            (double)r->params[i]);
        }
    }
    for (i = 0; i < 9; i++) {
        if (!fin(r->rotation[i])) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_record: rotation[%u] is not finite", (unsigned)i);
        }
        zero &= r->rotation[i] == 0.0f;
    }
    if (zero) {
        memcpy(r->rotation, ident, sizeof ident);
    } else {
        /* a rotation: R R^T = I within 0.01 */
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++) {
                const float *a = &r->rotation[3 * i], *b = &r->rotation[3 * j];
                float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] - (i == j ? 1.0f : 0.0f);
                if (fabsf(d) > 0.01f) {
                    return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_record: rotation is not a rotation matrix "
                                    "(rows %u / %u: %g)", (unsigned)i, (unsigned)j, (double)d);
                }
            }
        }
    }
    switch (r->type) {
    case SA1_OBJTYPE_O_GRING:
        if (!is_int(r->params[0], 0.0f, 15.0f) || !(r->params[2] == 0.0f || r->params[2] == 1.0f) ||
            fabsf(r->params[1]) > 1000.0f) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "O_GRING: params (%g, %g, %g): an integer count 0..15, a "
                            "spacing / radius within 1000 SA1 units, 0 (line) or 1 (circle)", (double)r->params[0],
                            (double)r->params[1], (double)r->params[2]);
        }
        break;
    case SA1_OBJTYPE_ITEMBOX:
    case SA1_OBJTYPE_ITEMBOX_AIR:
        if (!is_int(r->params[0], 0.0f, 8.0f)) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: contents %g (an integer 0..8)", t->name,
                            (double)r->params[0]);
        }
        break;
    default:
        break;
    }
    return SA1_OK;
}

sa1_status sa1_set_add(sa1_context *ctx, const sa1_set_record *rec, uint32_t *out_id)
{
    sa1set_registry *R;
    sa1_set_record r;
    sa1set_entry *e;
    uint32_t idx;
    sa1_status st;
    if (out_id != NULL) {
        *out_id = 0;
    }
    if (ctx == NULL || out_id == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = rec_take(ctx, sa1set_registry_of(ctx), rec, &r);
    if (st != SA1_OK) {
        return st;
    }
    st = registry_get(ctx, &R);
    if (st != SA1_OK) {
        return st;
    }
    if (R->hiwater < SA1_SET_MAX_RECORDS) {
        idx = R->hiwater++;
    } else if (R->free_count != 0) {
        idx = R->freeq[R->free_head];
        R->free_head = (R->free_head + 1u) % SA1_SET_MAX_RECORDS;
        R->free_count--;
    } else {
        return sa1_fail(ctx, SA1_ERR_LIMIT, "SET objects: %u records (SA1_SET_MAX_RECORDS)",
                        (unsigned)SA1_SET_MAX_RECORDS);
    }
    e = &R->ent[idx];
    e->gen = (e->gen + 1u) & SA1SET_GEN_MASK;
    if (e->gen == 0) {
        e->gen = 1;
    }
    e->id = (e->gen << SA1SET_IDX_BITS) | (idx + 1u);
    e->desc_version++;
    if (e->desc_version == 0) {
        e->desc_version = 1;
    }
    e->state_version++;
    e->state = SA1_SET_STATE_PRESENT;
    e->user_bits = 0;
    e->rec = r;
    R->count++;
    *out_id = e->id;
    return SA1_OK;
}

static sa1set_entry *entry_mut(sa1set_registry *R, uint32_t id)
{
    return (sa1set_entry *)(uintptr_t)sa1set_entry_of(R, id);
}

sa1_status sa1_set_update(sa1_context *ctx, uint32_t id, const sa1_set_record *rec)
{
    sa1set_registry *R;
    sa1set_entry *e;
    sa1_set_record r;
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    R = sa1set_registry_of(ctx);
    e = entry_mut(R, id);
    if (e == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    st = rec_take(ctx, R, rec, &r);
    if (st != SA1_OK) {
        return st;
    }
    if (r.type != SA1_OBJTYPE_ITEMBOX) {
        e->state &= ~SA1_SET_STATE_BROKEN;      /* a type without the broken state */
    }
    if (r.type != SA1_OBJTYPE_O_GRING) {
        e->user_bits = 0;
    }
    e->rec = r;
    e->desc_version++;
    if (e->desc_version == 0) {
        e->desc_version = 1;
    }
    return SA1_OK;
}

sa1_status sa1_set_remove(sa1_context *ctx, uint32_t id)
{
    sa1set_registry *R;
    sa1set_entry *e;
    uint32_t idx;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    R = sa1set_registry_of(ctx);
    e = entry_mut(R, id);
    if (e == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    idx = (id & SA1SET_IDX_MASK) - 1u;
    e->id = 0;
    R->count--;
    R->freeq[(R->free_head + R->free_count) % SA1_SET_MAX_RECORDS] = (uint16_t)idx;
    R->free_count++;
    return SA1_OK;
}

sa1_status sa1_set_state(sa1_context *ctx, uint32_t id, uint32_t state, uint32_t user_bits)
{
    sa1set_registry *R;
    sa1set_entry *e;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    R = sa1set_registry_of(ctx);
    e = entry_mut(R, id);
    if (e == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    if (state & ~(SA1_SET_STATE_PRESENT | SA1_SET_STATE_BROKEN)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_state: state 0x%X", (unsigned)state);
    }
    if ((state & SA1_SET_STATE_BROKEN) && e->rec.type != SA1_OBJTYPE_ITEMBOX) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_state: BROKEN on a %s (only the ground item box)",
                        sa1set_type_of(e->rec.type)->name);
    }
    if (user_bits != 0 && (e->rec.type != SA1_OBJTYPE_O_GRING || user_bits > 0xFFu)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_state: user bits 0x%X (O_GRING: 0..0xFF; else 0)",
                        (unsigned)user_bits);
    }
    e->state = state;
    e->user_bits = user_bits;
    e->state_version++;
    return SA1_OK;
}

sa1_status sa1_set_configure(sa1_context *ctx, const sa1_set_config *in)
{
    sa1_set_config c;
    sa1set_registry *R;
    sa1_status st;
    int has_range, has_view;
    if (ctx == NULL || in == NULL || in->struct_size < offsetof(sa1_set_config, list)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    memset(&c, 0, sizeof c);
    memcpy(&c, in, in->struct_size < sizeof c ? in->struct_size : sizeof c);
    /* round 0.9.4b (SA1_API_SET_RANGE): range and view_tan when struct_size covers them (else they stay as they are) */
    has_range = in->struct_size >= offsetof(sa1_set_config, range) + sizeof c.range;
    has_view = in->struct_size >= offsetof(sa1_set_config, view_tan) + sizeof c.view_tan;
    if (has_range && !(c.range == 0.0f || (fin(c.range) && c.range >= SA1_SET_RANGE_MIN && c.range <= SA1_SET_RANGE_MAX))) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_configure: range %g (0, or %g..%g SA1 units)",
                        (double)c.range, (double)SA1_SET_RANGE_MIN, (double)SA1_SET_RANGE_MAX);
    }
    if (has_view && !(c.view_tan == 0.0f || (fin(c.view_tan) && c.view_tan >= 0.700f &&
                                              c.view_tan <= SA1_SET_VIEW_TAN_MAX))) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_configure: view_tan %g (0, or 0.700..%g)",
                        (double)c.view_tan, (double)SA1_SET_VIEW_TAN_MAX);
    }
    if (c.ring_window < 1u || c.ring_window > SA1_SET_RING_WINDOW_MAX || c.box_window < 1u ||
        c.box_window > SA1_SET_BOX_WINDOW_MAX || !fin(c.margin) || c.margin < 0.0f || c.margin > 1000.0f ||
        c.list != SA1_SETLIST_LIBSA1) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_set_configure: ring_window %u (1..%u), box_window %u (1..%u), "
                        "margin %g (0..1000), list %u (SA1_SETLIST_LIBSA1)", (unsigned)c.ring_window,
                        (unsigned)SA1_SET_RING_WINDOW_MAX, (unsigned)c.box_window, (unsigned)SA1_SET_BOX_WINDOW_MAX,
                        (double)c.margin, (unsigned)c.list);
    }
    st = registry_get(ctx, &R);
    if (st != SA1_OK) {
        return st;
    }
    R->ring_window = c.ring_window;
    R->box_window = c.box_window;
    R->margin = c.margin;
    if (has_range) {
        R->range = c.range;
    }
    if (has_view) {
        R->view_tan = c.view_tan;
    }
    R->config_version++;
    return SA1_OK;
}

/* round 0.9.4b (SA1_API_SET_LIVE, review R.6): the records whose task lives in a character's world after its last pair */
sa1_status sa1_set_live(sa1_context *ctx, sa1_character_id id, uint32_t *records, uint32_t capacity, uint32_t *count)
{
    uint32_t slot, gen, n = 0;
    if (count != NULL) {
        *count = 0;
    }
    if (ctx == NULL || count == NULL || (capacity != 0 && records == NULL)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    slot = ((uint32_t)id & 0xFFFFu) - 1u;
    gen = ((uint32_t)id >> 16) & 0x7FFFu;
    if (id <= 0 || slot >= SA1_MAX_INSTANCES || ctx->inst[slot] == NULL || ctx->inst_gen[slot] != gen) {
        return SA1_ERR_NOT_FOUND;
    }
#if !defined(SA1_NO_WORLD)
    {
        struct sa1g_world *w = sa1_character_world(ctx, id);
        if (w != NULL) {
            n = sa1w_set_world_live(w, records, capacity);
        }
    }
#endif
    *count = n;
    return n > capacity ? SA1_ERR_BUFFER_TOO_SMALL : SA1_OK;
}

sa1_status sa1set_test_unavailable(sa1_context *ctx, uint32_t type, int32_t unavailable)
{
    sa1set_registry *R;
    sa1_status st;
    if (sa1set_type_of(type) == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    st = registry_get(ctx, &R);
    if (st != SA1_OK) {
        return st;
    }
    R->unavailable = unavailable ? (R->unavailable | (1u << type)) : (R->unavailable & ~(1u << type));
    return SA1_OK;
}

/* ---- events -------------------------------------------------------------------------------------------------------- */
sa1_status sa1_poll_set_events(sa1_context *ctx, sa1_set_event *out, uint32_t capacity, uint32_t *count)
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
        got = sa1w_set_world_poll(ctx, w, out + n, capacity - n);
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
