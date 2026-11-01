/*
 * Collision input from the host, stored in SA1 space (converted with the current coordinate
 * system).  The static geometry lives in one store (sa1_coladapt.h: sa1ca_static) that the
 * collision adapter turns into the game's landtable when a world builds its collision
 * (sadc docs/libsa1_integration.md section 5):
 *   sa1_collision_set_static     flat triangles (mode GRID), kept in the store's flat[] array
 *   sa1_collision_set_static_ex  grouped geometry (GRID / FAITHFUL) or a DISC stage reference
 * ctx->static_tris points at the store's flat triangles, which follow the store in the same
 * allocation (none for set_static_ex), so the store is found from the context without a field of
 * its own; ctx->num_static_tris counts them.
 *   sa1_collision_set_prepared   a sa1_collision_prepared (sa1_collision_prepare[_tris], any thread: the synthesis
 *                                done in a collision segment of its own); the store only holds a reference
 * Moving objects are stored per object: SA1-space object-local triangles sorted by collidable surface word
 * behind a header (sa1ca_objx: the BAMS rotation, a version that every move changes, the groups); every
 * world turns them into DynamicCOL entries (design 5.5, src/world/sa1_coladapt_world.c), which also records which of
 * them each character touched in every step (sa1_character_contacts, API 0.3: pushing physics props).
 */
#include "sa1_coladapt.h"
#include "sa1_api_world.h"
#include "sa1_hostobj.h"

#include <stdio.h>

static int tri_ok(const sa1_collision_tri *t)
{
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            if (!sa1_finite(t->v[i][j]) || fabsf(t->v[i][j]) > 1e9f)
                return 0;
    return 1;
}

static int rot_ok(const float r[9])
{
    int i, j;
    if (!r)
        return 1;
    for (i = 0; i < 9; i++)
        if (!sa1_finite(r[i]))
            return 0;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            float d = r[3 * i] * r[3 * j] + r[3 * i + 1] * r[3 * j + 1] + r[3 * i + 2] * r[3 * j + 2];
            if (fabsf(d - (i == j ? 1.0f : 0.0f)) > 1e-3f)
                return 0;
        }
    return 1;
}

static sa1_status copy_tris(sa1_context *ctx, const sa1_collision_tri *in, uint32_t n, int points,
                            sa1_collision_tri **out)
{
    uint32_t i, k;
    sa1_collision_tri *t;
    *out = NULL;
    if (n == 0)
        return SA1_OK;
    if (!in || n > SA1_MAX_COLLISION_TRIS)
        return n > SA1_MAX_COLLISION_TRIS ? SA1_ERR_LIMIT : SA1_ERR_INVALID_ARGUMENT;
    t = (sa1_collision_tri *)sa1_mem_array(ctx, n, sizeof *t);
    if (!t)
        return SA1_ERR_OUT_OF_MEMORY;
    for (i = 0; i < n; i++) {
        if (!tri_ok(&in[i])) {
            sa1_mem_free(ctx, t);
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "collision triangle %u is not finite", (unsigned)i);
        }
        for (k = 0; k < 3; k++) {
            if (points)
                sa1_from_host_point(ctx, in[i].v[k], t[i].v[k]);
            else
                memcpy(t[i].v[k], in[i].v[k], sizeof t[i].v[k]);
        }
        t[i].surface = in[i].surface;
    }
    *out = t;
    return SA1_OK;
}

/* ---- the static store --------------------------------------------------------------------- */

sa1ca_static *sa1ca_static_of(const sa1_context *ctx)
{
    sa1ca_static *s;
    if (!ctx || !ctx->static_tris)
        return NULL;
    s = (sa1ca_static *)(void *)ctx->static_tris - 1;   /* the triangles follow the store */
    return s->magic == SA1CA_MAGIC && s->flat == ctx->static_tris ? s : NULL;
}

static uint32_t next_generation(const sa1_context *ctx)
{
    const sa1ca_static *old = sa1ca_static_of(ctx);
    uint32_t g = old ? old->generation + 1u : 1u;
    return g ? g : 1u;
}

/* replaces the store (s may be NULL: no static geometry) */
static void store_install(sa1_context *ctx, sa1ca_static *s)
{
    if (s)
        s->generation = next_generation(ctx);
    sa1ca_static_free(ctx, sa1ca_static_of(ctx));
    ctx->static_tris = s ? s->flat : NULL;
    ctx->num_static_tris = s ? s->num_flat : 0;
}

sa1_status sa1_collision_set_static(sa1_context *ctx, const sa1_collision_tri *tris, uint32_t n)
{
    sa1_collision_tri *t;
    sa1ca_static *s;
    sa1_status st;
    if (!ctx)
        return SA1_ERR_INVALID_ARGUMENT;
    st = copy_tris(ctx, tris, n, 1, &t);
    if (st != SA1_OK)
        return st;
    st = sa1ca_static_create_flat(ctx, n, &s);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, t);
        return st;
    }
    if (n)
        memcpy(s->flat, t, (size_t)n * sizeof *t);
    sa1_mem_free(ctx, t);
    store_install(ctx, s);
    return SA1_OK;
}

sa1_status sa1_collision_set_static_ex(sa1_context *ctx, const sa1_collision_static *desc)
{
    sa1ca_static *s;
    sa1_status st;
    if (!ctx)
        return SA1_ERR_INVALID_ARGUMENT;
    st = sa1ca_static_create(ctx, desc, &s);
    if (st != SA1_OK)
        return st;
    store_install(ctx, s);
    return SA1_OK;
}

/* ---- prepared geometry (any thread) ----------------------------------------------------------- */

static volatile int32_t g_prep_serial;

void sa1ca_prepared_retain(struct sa1_collision_prepared *p)
{
    if (p)
        sa1_atomic_add32(&p->refs, 1);
}

void sa1ca_prepared_release(struct sa1_collision_prepared *p)
{
    sa1_host h;
    if (!p || sa1_atomic_add32(&p->refs, -1) != 0)
        return;
#if !defined(SA1_NO_WORLD)
    sa1w_prepare_free(p);
#endif
    h = p->host;
    p->magic = 0;
    sa1_host_mem_free(&h, p);
}

static int prepared_ok(const sa1_collision_prepared *p)
{
    return p && p->magic == SA1CA_PREP_MAGIC;
}

void sa1_collision_prepared_retain(sa1_collision_prepared *p)
{
    if (prepared_ok(p))
        sa1ca_prepared_retain(p);
}

void sa1_collision_prepared_release(sa1_collision_prepared *p)
{
    if (prepared_ok(p))
        sa1ca_prepared_release(p);
}

/* the work of both entry points on a worker context: the description (desc) or the flat triangles, then the synthesis */
static sa1_status prepare_run(sa1_context *w, const sa1_collision_static *desc, const sa1_collision_tri *tris, uint32_t n,
                              const sa1_collision_grid *grid, sa1_collision_prepared *p)
{
    sa1ca_static *s = NULL;
    sa1_status st;
    if (desc) {
        st = sa1ca_static_create(w, desc, &s);
    } else {
        sa1_collision_tri *t = NULL;
        if (grid && !sa1ca_grid_valid(grid))
            return sa1_fail(w, SA1_ERR_INVALID_ARGUMENT, "sa1_collision_prepare_tris: sa1_collision_grid: struct_size, flags or sizes");
        st = copy_tris(w, tris, n, 1, &t);
        if (st == SA1_OK)
            st = sa1ca_static_create_flat(w, n, &s);
        if (st == SA1_OK) {
            if (n)
                memcpy(s->flat, t, (size_t)n * sizeof *t);
            if (grid)
                sa1ca_grid_defaults(grid, &s->grid);
        }
        sa1_mem_free(w, t);
    }
    if (st != SA1_OK)
        return st;
    p->mode = s->mode;
    p->groups = s->num_groups;
    p->points = s->num_points;
    p->triangles = s->num_flat ? s->num_flat : s->num_tris;
    p->flat = s->num_flat;
    p->collidable_groups = s->collidable_groups;
    p->collidable_tris = s->collidable_tris;
    p->odd_tris = s->odd_tris;
    p->grid = s->grid;
    if (s->num_flat) {
        uint32_t i;
        p->groups = 1;
        p->points = 3u * s->num_flat;
        p->collidable_tris = 0;
        for (i = 0; i < s->num_flat; i++)
            p->collidable_tris += (sa1ca_surface_col(s->flat[i].surface) & SA1CA_COL_COLLIDABLE) != 0;
        p->collidable_groups = p->collidable_tris != 0;
    }
    if (s->mode == SA1_COLLISION_MODE_DISC) {
        st = sa1_fail(w, SA1_ERR_UNSUPPORTED, "sa1_collision_prepare: DISC collision needs no preparation "
                      "(sa1_collision_set_static_ex)");
    } else {
        s->generation = p->generation;
#if defined(SA1_NO_WORLD)
        st = sa1_fail(w, SA1_ERR_NOT_IMPLEMENTED, "sa1_collision_prepare: this libsa1 build has no world core");
#else
        st = sa1w_prepare_build(w, s, p);
#endif
    }
    sa1ca_static_free(w, s);
    return st;
}

static sa1_status prepare(const sa1_context *ctx, const sa1_collision_static *desc, const sa1_collision_tri *tris,
                          uint32_t n, const sa1_collision_grid *grid, sa1_collision_prepared **out)
{
    sa1_collision_prepared *p;
    sa1_context *w;
    sa1_status st;
    double t0 = sa1_now_ms();
    if (!out)
        return SA1_ERR_INVALID_ARGUMENT;
    *out = NULL;
    if (!ctx)
        return SA1_ERR_INVALID_ARGUMENT;
    p = (sa1_collision_prepared *)sa1_host_mem_alloc(&ctx->host, sizeof *p);
    if (!p)
        return SA1_ERR_OUT_OF_MEMORY;
    p->magic = SA1CA_PREP_MAGIC;
    p->refs = 1;
    p->host = ctx->host;
    /* the GRID header's generation word: process-unique, so that a header always names its own preparation */
    p->generation = 0x40000000u | ((uint32_t)sa1_atomic_add32(&g_prep_serial, 1) & 0x3FFFFFFFu);
    w = sa1_worker_ctx_create(ctx);
    if (!w) {
        st = SA1_ERR_OUT_OF_MEMORY;
        snprintf(p->message, sizeof p->message, "out of memory");
    } else {
        st = prepare_run(w, desc, tris, n, grid, p);
        if (st != SA1_OK)
            snprintf(p->message, sizeof p->message, "%s", w->error[0] ? w->error : sa1_status_string(st));
        sa1_worker_ctx_destroy(w);
    }
    p->status = st;
    p->ms = (float)(sa1_now_ms() - t0);
    *out = p;
    return st;
}

sa1_status sa1_collision_prepare(const sa1_context *ctx, const sa1_collision_static *desc, sa1_collision_prepared **out)
{
    if (!desc) {
        if (out)
            *out = NULL;
        return SA1_ERR_INVALID_ARGUMENT;
    }
    return prepare(ctx, desc, NULL, 0, NULL, out);
}

sa1_status sa1_collision_prepare_tris(const sa1_context *ctx, const sa1_collision_tri *tris, uint32_t n,
                                      const sa1_collision_grid *grid, sa1_collision_prepared **out)
{
    return prepare(ctx, NULL, tris, n, grid, out);
}

sa1_status sa1_collision_prepared_info_get(const sa1_collision_prepared *p, sa1_collision_prepared_info *out)
{
    sa1_collision_prepared_info i;
    if (!prepared_ok(p) || !out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    memset(&i, 0, sizeof i);
    i.struct_size = sizeof i;
    i.status = p->status;
    memcpy(i.message, p->message, sizeof i.message);
    i.mode = p->mode;
    i.triangles = p->triangles;
    i.grid_objects = p->built.objects;
    i.grid_kill_objects = p->built.kill_objects;
    i.grid_triangles = p->built.triangles;
    i.grid_split = p->built.split;
    i.grid_cut = p->built.cut;
    i.grid_dropped = p->built.dropped;
    i.grid_undersides = p->built.undersides;
    i.grid_front_only = p->built.front_only;    /* round 0.9.3 (SA1_API_CEILING) */
    i.grid_curved = p->built.curved;
    i.bytes = p->built.bytes;
    i.grid_cell_size = p->mode == SA1_COLLISION_MODE_GRID ? p->grid.cell : 0.0f;
    i.ms = p->ms;
    i.refs = (uint32_t)sa1_atomic_load32(&((sa1_collision_prepared *)(uintptr_t)p)->refs);
    sa1_copy_out(out, &i, sizeof i);
    return SA1_OK;
}

sa1_status sa1_collision_set_prepared(sa1_context *ctx, sa1_collision_prepared *p)
{
    sa1ca_static *s;
    sa1_status st;
    if (!ctx)
        return SA1_ERR_INVALID_ARGUMENT;
    if (!p) {
        store_install(ctx, NULL);
        return SA1_OK;
    }
    if (!prepared_ok(p))
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_collision_set_prepared: not a prepared geometry");
    if (p->status != SA1_OK)
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_collision_set_prepared: the preparation failed: %s",
                        p->message);
    st = sa1ca_static_create_flat(ctx, 0, &s);      /* an empty store that only refers to p */
    if (st != SA1_OK)
        return st;
    s->mode = p->mode;
    s->grid = p->grid;
    s->built = p->built;
    s->builds = 1;
    sa1ca_prepared_retain(p);
    s->prepared = p;
    store_install(ctx, s);
    s->built.generation = s->generation;
    sa1_log(ctx, SA1_LOG_DEBUG, "collision: prepared geometry set (%s, %u triangles -> %u in %u objects, %u bytes, prepared "
            "in %.1f ms)", p->mode == SA1_COLLISION_MODE_GRID ? "GRID" : "FAITHFUL", (unsigned)p->triangles,
            (unsigned)p->built.triangles, (unsigned)p->built.objects, (unsigned)p->built.bytes, (double)p->ms);
    return SA1_OK;
}

sa1_status sa1_collision_info_get(const sa1_context *ctx, sa1_collision_info *out)
{
    const sa1ca_static *s;
    uint32_t has_built;
    sa1_collision_info info;
    uint32_t i;
    if (!ctx || !out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.disc_level = -1;
    info.disc_act = -1;
    s = sa1ca_static_of(ctx);
    if (s && s->prepared) {
        const sa1_collision_prepared *p = s->prepared;
        info.mode = p->mode;
        info.generation = s->generation;
        info.groups = p->groups;
        info.points = p->points;
        info.triangles = p->triangles;
        info.collidable_groups = p->collidable_groups;
        info.collidable_triangles = p->collidable_tris;
        info.odd_triangles = p->odd_tris;
        info.landtable = p->built.landtable;
        info.bytes = p->built.bytes;
        info.meshsets = p->built.meshsets;
        info.materials = p->built.materials;
        info.builds = 1;
        info.grid_objects = p->mode == SA1_COLLISION_MODE_GRID ? p->built.objects : 0u;
        info.grid_kill_objects = p->built.kill_objects;
        info.grid_triangles = p->mode == SA1_COLLISION_MODE_GRID ? p->built.triangles : 0u;
        info.grid_split = p->built.split;
        info.grid_cut = p->built.cut;
        info.grid_dropped = p->built.dropped;
        info.grid_undersides = p->built.undersides;
        info.grid_front_only = p->built.front_only;     /* round 0.9.3 (SA1_API_CEILING) */
        info.grid_curved = p->built.curved;
        info.grid_cell_size = p->mode == SA1_COLLISION_MODE_GRID ? p->grid.cell : 0.0f;
        info.prepared = 1;
    } else if (s) {
        info.mode = s->mode;
        info.generation = s->generation;
        info.groups = s->num_groups;
        info.points = s->num_points;
        info.triangles = s->num_flat ? s->num_flat : s->num_tris;
        info.collidable_groups = s->collidable_groups;
        info.collidable_triangles = s->collidable_tris;
        info.odd_triangles = s->odd_tris;
        info.disc_level = s->disc_level;
        info.disc_act = s->disc_act;
        info.landtable = s->built.landtable;
        info.bytes = s->built.bytes;
        info.meshsets = s->built.meshsets;
        info.materials = s->built.materials;
        info.builds = s->builds;
        has_built = s->built.generation == s->generation && s->built.grid != 0;
        info.grid_objects = has_built ? s->built.objects : 0u;
        info.grid_kill_objects = has_built ? s->built.kill_objects : 0u;
        info.grid_triangles = has_built ? s->built.triangles : 0u;
        info.grid_split = has_built ? s->built.split : 0u;
        info.grid_cut = has_built ? s->built.cut : 0u;
        info.grid_dropped = has_built ? s->built.dropped : 0u;
        info.grid_undersides = has_built ? s->built.undersides : 0u;
        info.grid_front_only = has_built ? s->built.front_only : 0u;  /* round 0.9.3 (SA1_API_CEILING) */
        info.grid_curved = has_built ? s->built.curved : 0u;
        info.grid_cell_size = s->mode == SA1_COLLISION_MODE_GRID ? s->grid.cell : 0.0f;
        if (s->num_flat) {
            /* set_static: one GRID group of flat triangles; collidable if its surface maps to 0x400003 */
            info.groups = 1;
            info.points = 3u * s->num_flat;
            for (i = 0; i < s->num_flat; i++)
                info.collidable_triangles += (sa1ca_surface_col(s->flat[i].surface) & SA1CA_COL_COLLIDABLE) != 0;
            info.collidable_groups = info.collidable_triangles != 0;
        }
    }
    info.moving_objects = ctx->col_objects.count;
    sa1_copy_out(out, &info, sizeof info);   /* keeps the caller's struct_size */
    return SA1_OK;
}

static sa1_col_object *find_obj(sa1_context *ctx, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < ctx->col_objects.count; i++)
        if (SA1_VEC(ctx->col_objects, sa1_col_object)[i].id == id)
            return &SA1_VEC(ctx->col_objects, sa1_col_object)[i];
    return NULL;
}

/* the triangles of a moving object in SA1 space (object local), sorted by collidable group, behind their
 * header (sa1ca_objx) in one allocation */
static sa1_status obj_create(sa1_context *ctx, const sa1_collision_tri *in, uint32_t n, sa1ca_objx **out)
{
    sa1_collision_tri *conv, *dst;
    sa1ca_objx *x;
    uint32_t i, k, g, ng = 0, cols[SA1_MAX_OBJECT_GROUPS], bds[SA1_MAX_OBJECT_GROUPS], pos[SA1_MAX_OBJECT_GROUPS + 1u];
    double best = 0.0;
    sa1_status st;
    *out = NULL;
    if (n > SA1_MAX_OBJECT_TRIS)
        return sa1_fail(ctx, SA1_ERR_LIMIT, "collision object: %u triangles (at most %u)", (unsigned)n,
                        (unsigned)SA1_MAX_OBJECT_TRIS);
    st = copy_tris(ctx, in, n, 1, &conv);
    if (st != SA1_OK)
        return st;
    for (i = 0; i < n; i++) {
        uint32_t col = sa1ca_surface_col(conv[i].surface);
        for (k = 0; k < 3; k++) {
            double d = (double)conv[i].v[k][0] * conv[i].v[k][0] + (double)conv[i].v[k][1] * conv[i].v[k][1]
                       + (double)conv[i].v[k][2] * conv[i].v[k][2];
            best = d > best ? d : best;
        }
        uint32_t bd = sa1ca_surface_board(conv[i].surface);
        if (!(col & SA1CA_COL_COLLIDABLE))
            continue;
        /* round 0.9.5 (review R.1): a group is one (COL word, board bits): the world ORs the riding bit into a group */
        for (g = 0; g < ng && !(cols[g] == col && bds[g] == bd); g++) {
        }
        if (g == ng) {
            if (ng == SA1_MAX_OBJECT_GROUPS) {
                sa1_mem_free(ctx, conv);
                return sa1_fail(ctx, SA1_ERR_LIMIT, "collision object: more than %u collidable surface kinds",
                                (unsigned)SA1_MAX_OBJECT_GROUPS);
            }
            bds[ng] = bd;
            cols[ng++] = col;
        }
    }
    if (sqrt(best) > (double)SA1_MAX_OBJECT_RADIUS) {
        sa1_mem_free(ctx, conv);
        return sa1_fail(ctx, SA1_ERR_LIMIT, "collision object: a point %.1f SA1 units from its origin (at most %.0f)",
                        sqrt(best), (double)SA1_MAX_OBJECT_RADIUS);
    }
    x = (sa1ca_objx *)sa1_mem_alloc(ctx, sizeof *x + (size_t)n * sizeof(sa1_collision_tri));
    if (!x) {
        sa1_mem_free(ctx, conv);
        return SA1_ERR_OUT_OF_MEMORY;
    }
    x->magic = SA1CA_OBJX_MAGIC;
    x->ntris = n;
    x->ngroups = ng;
    x->radius = (float)(sqrt(best) * 1.0001 + 1e-3);
    dst = (sa1_collision_tri *)(void *)(x + 1);
    /* stable counting sort: group 0, 1, ..., then the triangles that are not land (ignored by the worlds) */
    memset(pos, 0, sizeof pos);
    for (i = 0; i < n; i++) {
        uint32_t col = sa1ca_surface_col(conv[i].surface), bd = sa1ca_surface_board(conv[i].surface);
        for (g = 0; g < ng && !((col & SA1CA_COL_COLLIDABLE) && cols[g] == col && bds[g] == bd); g++) {
        }
        pos[g]++;
    }
    for (g = 0, k = 0; g <= ng; g++) {
        uint32_t c = pos[g];
        if (g < ng) {
            x->group_col[g] = cols[g];
            x->group_board[g] = bds[g];
            x->group_first[g] = k;
            x->group_count[g] = c;
        }
        pos[g] = k;
        k += c;
    }
    for (i = 0; i < n; i++) {
        uint32_t col = sa1ca_surface_col(conv[i].surface), bd = sa1ca_surface_board(conv[i].surface);
        for (g = 0; g < ng && !((col & SA1CA_COL_COLLIDABLE) && cols[g] == col && bds[g] == bd); g++) {
        }
        dst[pos[g]++] = conv[i];
    }
    sa1_mem_free(ctx, conv);
    *out = x;
    return SA1_OK;
}

static void obj_free(const sa1_context *ctx, sa1_col_object *o)
{
    sa1ca_objx *x = sa1ca_objx_of(o);
    sa1_mem_free(ctx, x ? (void *)x : (void *)o->tris);
    o->tris = NULL;
}

/* host rotation R_h acts on host vectors; in SA1 space: R_s = A^T * R_h * A */
static void place(sa1_context *ctx, sa1_col_object *o, const float position[3], const float rotation[9])
{
    sa1ca_objx *x;
    int c;
    if (position)
        sa1_from_host_point(ctx, position, o->position);
    else
        o->position[0] = o->position[1] = o->position[2] = 0.0f;
    for (c = 0; c < 3; c++) {
        float e[3] = { 0, 0, 0 }, h[3], rh[3], s[3];
        e[c] = 1.0f;
        sa1_to_host_dir(ctx, e, h);
        if (rotation) {
            rh[0] = rotation[0] * h[0] + rotation[1] * h[1] + rotation[2] * h[2];
            rh[1] = rotation[3] * h[0] + rotation[4] * h[1] + rotation[5] * h[2];
            rh[2] = rotation[6] * h[0] + rotation[7] * h[1] + rotation[8] * h[2];
        } else {
            memcpy(rh, h, sizeof rh);
        }
        sa1_from_host_dir(ctx, rh, s);
        o->rotation[c] = s[0];
        o->rotation[3 + c] = s[1];
        o->rotation[6 + c] = s[2];
    }
    x = sa1ca_objx_of(o);
    if (x) {
        memcpy(x->pos, o->position, sizeof x->pos);
        sa1ca_rotation_to_bams(o->rotation, x->ang);
        x->version++;
    }
}

sa1_status sa1_collision_object_add(sa1_context *ctx, const sa1_collision_tri *tris, uint32_t n,
                                    const float position[3], const float rotation[9], uint32_t *out_id)
{
    sa1_col_object *o;
    sa1ca_objx *x;
    sa1_status st;
    if (!ctx || !out_id || !tris || n == 0)
        return SA1_ERR_INVALID_ARGUMENT;
    *out_id = 0;
    if (ctx->col_objects.count >= SA1_MAX_COLLISION_OBJECTS)
        return SA1_ERR_LIMIT;
    if (position && !(sa1_finite(position[0]) && sa1_finite(position[1]) && sa1_finite(position[2])))
        return SA1_ERR_INVALID_ARGUMENT;
    if (!rot_ok(rotation))
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "collision object rotation is not orthonormal");
    st = obj_create(ctx, tris, n, &x);
    if (st != SA1_OK)
        return st;
    o = (sa1_col_object *)sa1_vec_push(ctx, &ctx->col_objects);
    if (!o) {
        sa1_mem_free(ctx, x);
        return SA1_ERR_OUT_OF_MEMORY;
    }
    o->id = ctx->next_col_id++;
    if (ctx->next_col_id == 0)
        ctx->next_col_id = 1;
    o->tris = (sa1_collision_tri *)(void *)(x + 1);
    o->num_tris = n;
    place(ctx, o, position, rotation);
    *out_id = o->id;
    return SA1_OK;
}

sa1_status sa1_collision_object_move(sa1_context *ctx, uint32_t id, const float position[3], const float rotation[9])
{
    sa1_col_object *o;
    if (!ctx)
        return SA1_ERR_INVALID_ARGUMENT;
    o = find_obj(ctx, id);
    if (!o)
        return SA1_ERR_NOT_FOUND;
    if (position && !(sa1_finite(position[0]) && sa1_finite(position[1]) && sa1_finite(position[2])))
        return SA1_ERR_INVALID_ARGUMENT;
    if (!rot_ok(rotation))
        return SA1_ERR_INVALID_ARGUMENT;
    place(ctx, o, position, rotation);
    return SA1_OK;
}

sa1_status sa1_collision_object_remove(sa1_context *ctx, uint32_t id)
{
    sa1_col_object *o;
    uint32_t idx;
    if (!ctx)
        return SA1_ERR_INVALID_ARGUMENT;
    o = find_obj(ctx, id);
    if (!o)
        return SA1_ERR_NOT_FOUND;
    obj_free(ctx, o);
    idx = (uint32_t)(o - SA1_VEC(ctx->col_objects, sa1_col_object));
    if (idx + 1u < ctx->col_objects.count)
        SA1_VEC(ctx->col_objects, sa1_col_object)[idx] = SA1_VEC(ctx->col_objects, sa1_col_object)[ctx->col_objects.count - 1u];
    ctx->col_objects.count--;
    return SA1_OK;
}

sa1_status sa1_collision_stats(const sa1_context *ctx, uint32_t *static_tris, uint32_t *objects, uint32_t *object_tris)
{
    const sa1ca_static *s;
    uint32_t i, n = 0;
    if (!ctx)
        return SA1_ERR_INVALID_ARGUMENT;
    for (i = 0; i < ctx->col_objects.count; i++)
        n += SA1_VEC(ctx->col_objects, sa1_col_object)[i].num_tris;
    s = sa1ca_static_of(ctx);
    if (static_tris)
        *static_tris = s ? (s->prepared ? s->prepared->triangles : (s->num_flat ? s->num_flat : s->num_tris)) : 0;
    if (objects)
        *objects = ctx->col_objects.count;
    if (object_tris)
        *object_tris = n;
    return SA1_OK;
}

/* API 0.3: the contacts of a character with the moving objects (sa1_coladapt_world.c records them every step) */
sa1_status sa1_character_contacts(sa1_context *ctx, sa1_character_id id, sa1_contact *out, uint32_t capacity,
                                  uint32_t *count)
{
    uint32_t slot = ((uint32_t)id & 0xFFFFu) - 1u, gen = ((uint32_t)id >> 16) & 0x7FFFu;
    if (count)
        *count = 0;
    if (!ctx || !count || (capacity && !out))
        return SA1_ERR_INVALID_ARGUMENT;
    if (id <= 0 || slot >= SA1_MAX_INSTANCES || !ctx->inst[slot] || ctx->inst_gen[slot] != gen)
        return SA1_ERR_NOT_FOUND;
#if defined(SA1_NO_WORLD)
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    {
        struct sa1g_world *w = sa1_character_world(ctx, id);
        uint32_t n = w ? sa1ca_world_contacts(ctx, w, out, capacity) : 0u, k;
        for (k = 0; k < n; k++)
            out[k].character = id;
        *count = n;
        return SA1_OK;
    }
#endif
}

void sa1_collision_free(sa1_context *ctx)
{
    uint32_t i;
    for (i = 0; i < ctx->col_objects.count; i++)
        obj_free(ctx, &SA1_VEC(ctx->col_objects, sa1_col_object)[i]);
    sa1_vec_free(ctx, &ctx->col_objects);
    store_install(ctx, NULL);
    sa1ho_context_free(ctx);    /* API 0.3: the host objects' registry (sa1_hostobj.c), at sa1_shutdown */
    sa1set_context_free(ctx);   /* round 0.9.4: the SET objects' registry (sa1_set.c), at sa1_shutdown */
}