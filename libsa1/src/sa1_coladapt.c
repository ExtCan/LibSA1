/*
 * sa1_coladapt.c - the collision adapter: the game's landtable, in Dreamcast layout, from the host's
 * static geometry (sadc docs/libsa1_integration.md section 5; overview and conventions in
 * sa1_coladapt.h).
 *
 *   1. the stored description (sa1_collision_set_static_ex input, validated, in SA1 space)
 *   2. FAITHFUL synthesis (design 5.3)
 *   3. GRID synthesis (design 5.4)
 *   4. DISC mode: the stage overlay and its landtable from the user's disc
 *   5. landtable dumps of the harness (--dump-land v2), for FAITHFUL tests and replays
 *   6. moving objects: the header of each object's triangles, rotation -> BAMS
 * The world side (window landtable, DynamicCOL entries, kill planes) is src/world/sa1_coladapt_world.c.
 *
 * No game data is held here: the only constants are DC addresses, structure layouts and the SHA-1 of
 * the stage file DISC mode reads (metadata, like sa1_manifest.c).
 */
#include "sa1_coladapt.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------------------------------ */
/* helpers                                                                                          */
/* ------------------------------------------------------------------------------------------------ */

static void ca_wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void ca_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* floats are written bit for bit (no conversion, -0.0 and every payload survive) */
static void ca_wrf(uint8_t *p, float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    ca_wr32(p, u);
}

static int ca_coord_ok(float f)
{
    return sa1_finite(f) && fabsf(f) <= 1e9f;
}

uint32_t sa1ca_surface_to_col(uint32_t surface)
{
    static const uint32_t map[][2] = {
        { SA1_SURFACE_SOLID, 0x1u },          { SA1_SURFACE_WATER, 0x2u },
        { SA1_SURFACE_NO_CLIMB, 0x1000u },    { SA1_SURFACE_HURT, 0x10000u },
        { SA1_SURFACE_SLIPPERY, 0x4u },       { SA1_SURFACE_HEAVY_WATER, 0x400000u },
        { SA1_SURFACE_SAND, 0x10u },          { SA1_SURFACE_BOOST, 0x80u },
        { SA1_SURFACE_SLIDE, 0x20000u },      { SA1_SURFACE_STAIRS, 0x4000u },
        { SA1_SURFACE_NO_LAND, 0x40u },       { SA1_SURFACE_WALL_SLIDE, 0x200000u },
        { SA1_SURFACE_DIG, 0x100u },
        /* round 0.9.2 (sadc docs/research/gsa1_090_surfaces.md F1) */
        { SA1_SURFACE_SNOW, 0x8u },           { SA1_SURFACE_SPEED_PATH, 0x800000u },
        { SA1_SURFACE_FOOTPRINTS, 0x100000u },
    };
    uint32_t col = 0, i;
    for (i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (surface & map[i][0]) {
            col |= map[i][1];
        }
    }
    return col;
}

uint32_t sa1ca_surface_col(uint32_t surface)
{
    uint32_t col = sa1ca_surface_to_col(surface);
    if (!(surface & (SA1_SURFACE_SOLID | SA1_SURFACE_WATER | SA1_SURFACE_HEAVY_WATER | SA1_SURFACE_KILL))) {
        col |= 0x1u;        /* land unless the triangle says it is something else */
    }
    return col;
}

uint32_t sa1ca_surface_material(uint32_t surface)
{
    if (surface & SA1_SURFACE_DOUBLE_SIDED) {
        return SA1_MAT_DOUBLE_SIDE;
    }
    /* round 0.9.3 (SA1_API_CEILING): front-only, unless the triangle is part of a curved surface (LOOP-L1 / ceiling
       Review R.4.8: a loop's faces stay plain single-sided) */
    return (surface & (SA1_SURFACE_FRONT_ONLY | SA1_SURFACE_CURVED)) == SA1_SURFACE_FRONT_ONLY ? SA1_MAT_FRONT_ONLY : 0u;
}

/* a GRID piece's material word: front-only only for a single-sided land triangle that is not part of a curved surface;
 * SA1CA_MAT_THIN_TOP is GRID c2's own (never the host's) */
static uint32_t ca_mat_norm(uint32_t mat, int curved)
{
    mat &= ~(uint32_t)SA1CA_MAT_THIN_TOP;
    return (mat & SA1_MAT_DOUBLE_SIDE) || curved ? (mat & ~(uint32_t)SA1_MAT_FRONT_ONLY) : mat;
}

/* ------------------------------------------------------------------------------------------------ */
/* 1. the stored description                                                                        */
/* ------------------------------------------------------------------------------------------------ */

void sa1ca_static_free(const sa1_context *ctx, sa1ca_static *s)
{
    if (s == NULL) {
        return;
    }
    if (s->prepared != NULL) {
        sa1ca_prepared_release(s->prepared);
        s->prepared = NULL;
    }
    sa1_mem_free(ctx, s->groups);
    sa1_mem_free(ctx, s->points);
    sa1_mem_free(ctx, s->tris);
    s->magic = 0;
    sa1_mem_free(ctx, s);
}

static sa1ca_static *ca_store_alloc(const sa1_context *ctx, uint32_t nflat)
{
    size_t size = sizeof(sa1ca_static) + (size_t)nflat * sizeof(sa1_collision_tri);
    sa1ca_static *s = (sa1ca_static *)sa1_mem_alloc(ctx, size);
    if (s != NULL) {
        s->magic = SA1CA_MAGIC;
        s->num_flat = nflat;
        s->flat = (sa1_collision_tri *)(void *)(s + 1);
    }
    return s;
}

sa1_status sa1ca_static_create_flat(sa1_context *ctx, uint32_t n, sa1ca_static **out)
{
    sa1ca_static *s;
    *out = NULL;
    if (n > SA1_MAX_COLLISION_TRIS) {
        return SA1_ERR_LIMIT;
    }
    s = ca_store_alloc(ctx, n);
    if (s == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    s->mode = SA1_COLLISION_MODE_GRID;
    sa1ca_grid_defaults(NULL, &s->grid);
    *out = s;
    return SA1_OK;
}

/* a host-space radius / width -> SA1 units */
static float ca_len_from_host(const sa1_context *ctx, float r)
{
    return r / ctx->coords.scale;
}

/* world = T * Rz * Ry * Rx * S * p for the computed COL sphere (double; never on an exact path) */
static void ca_transform_point(const sa1ca_group *g, const float p[3], double out[3])
{
    const double k = 6.283185307179586476925 / 65536.0;
    double x = (double)p[0] * g->scl[0], y = (double)p[1] * g->scl[1], z = (double)p[2] * g->scl[2], t, c, s;
    c = cos((double)(g->ang[0] & 0xFFFFu) * k);
    s = sin((double)(g->ang[0] & 0xFFFFu) * k);
    t = y * c - z * s;
    z = y * s + z * c;
    y = t;
    c = cos((double)(g->ang[1] & 0xFFFFu) * k);
    s = sin((double)(g->ang[1] & 0xFFFFu) * k);
    t = x * c + z * s;
    z = -x * s + z * c;
    x = t;
    c = cos((double)(g->ang[2] & 0xFFFFu) * k);
    s = sin((double)(g->ang[2] & 0xFFFFu) * k);
    t = x * c - y * s;
    y = x * s + y * c;
    x = t;
    out[0] = x + g->pos[0];
    out[1] = y + g->pos[1];
    out[2] = z + g->pos[2];
}

/* a radius that is never below the exact one after rounding to float */
static float ca_radius_up(double r)
{
    float f = (float)r;
    return f + 1e-4f * (f > 1.0f ? f : 1.0f);
}

static void ca_model_bounds(const sa1ca_static *s, sa1ca_group *g)
{
    const float *p = s->points + 3u * (size_t)g->first_point;
    float lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
    double c[3], best = 0.0;
    uint32_t i, k;
    for (i = 0; i < g->num_points; i++) {
        for (k = 0; k < 3; k++) {
            float v = p[3u * i + k];
            if (i == 0 || v < lo[k]) {
                lo[k] = v;
            }
            if (i == 0 || v > hi[k]) {
                hi[k] = v;
            }
        }
    }
    for (k = 0; k < 3; k++) {
        c[k] = 0.5 * ((double)lo[k] + (double)hi[k]);
        g->model_center[k] = (float)c[k];
    }
    for (i = 0; i < g->num_points; i++) {
        double d = 0.0;
        for (k = 0; k < 3; k++) {
            double e = (double)p[3u * i + k] - (double)g->model_center[k];
            d += e * e;
        }
        if (d > best) {
            best = d;
        }
    }
    g->model_r = g->num_points ? ca_radius_up(sqrt(best)) : 0.0f;
}

static void ca_col_sphere(sa1ca_group *g)
{
    double c[3];
    float m = fabsf(g->scl[0]);
    uint32_t k;
    if (fabsf(g->scl[1]) > m) {
        m = fabsf(g->scl[1]);
    }
    if (fabsf(g->scl[2]) > m) {
        m = fabsf(g->scl[2]);
    }
    ca_transform_point(g, g->model_center, c);
    for (k = 0; k < 3; k++) {
        g->col_center[k] = (float)c[k];
    }
    g->col_radius = ca_radius_up((double)g->model_r * (double)m);
}

static sa1_status ca_bad(sa1_context *ctx, sa1ca_static *s, sa1_status st, const char *what, uint32_t i)
{
    sa1ca_static_free(ctx, s);
    return sa1_fail(ctx, st, "sa1_collision_set_static_ex: %s (%u)", what, (unsigned)i);
}

#define CA_GROUP(d, stride, i) ((const sa1_collision_group *)(const void *)((const uint8_t *)(d)->groups + (size_t)(stride) * (i)))

sa1_status sa1ca_static_create(sa1_context *ctx, const sa1_collision_static *d, sa1ca_static **out)
{
    sa1ca_static *s;
    uint32_t stride = 0, i, k, raw;
    int sa1_space;
    *out = NULL;
    if (d == NULL || d->struct_size < offsetof(sa1_collision_static, grid)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_collision_set_static_ex: NULL or old description");
    }
    if (d->mode > SA1_COLLISION_MODE_DISC || (d->flags & ~(uint32_t)SA1_COLLISION_SA1_SPACE) != 0) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_collision_set_static_ex: mode %u / flags %X",
                        (unsigned)d->mode, (unsigned)d->flags);
    }
    s = ca_store_alloc(ctx, 0);
    if (s == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    s->mode = d->mode;
    s->disc_level = -1;
    s->disc_act = -1;
    sa1ca_grid_defaults(NULL, &s->grid);
    if (d->struct_size >= offsetof(sa1_collision_static, grid) + sizeof(d->grid) && d->grid != NULL) {
        if (!sa1ca_grid_valid(d->grid)) {
            return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "sa1_collision_grid: struct_size, flags or sizes",
                          d->grid->flags);
        }
        sa1ca_grid_defaults(d->grid, &s->grid);
    }
    if (d->mode == SA1_COLLISION_MODE_DISC) {
        if (!SA1CA_DISC_LEVEL_OK(d->disc_level) || d->disc_act < 0 || d->disc_act >= SA1CA_STAGE_ACTS) {
            return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "DISC stage / act out of range",
                          (uint32_t)d->disc_level);
        }
        s->disc_level = d->disc_level;
        s->disc_act = d->disc_act;
        *out = s;
        return SA1_OK;
    }
    if (d->num_groups > SA1_MAX_COLLISION_GROUPS) {
        return ca_bad(ctx, s, SA1_ERR_LIMIT, "too many groups", d->num_groups);
    }
    if (d->num_tris > SA1_MAX_COLLISION_TRIS || d->num_points > 3u * SA1_MAX_COLLISION_TRIS) {
        return ca_bad(ctx, s, SA1_ERR_LIMIT, "too many triangles or points", d->num_tris);
    }
    if ((d->num_groups && d->groups == NULL) || (d->num_points && d->points == NULL)
        || (d->num_tris && d->indices == NULL)) {
        return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "missing array", 0);
    }
    if (d->num_groups) {
        stride = d->groups[0].struct_size;
        if (stride < sizeof(sa1_collision_group) || (stride & 3u) != 0) {
            return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "sa1_collision_group.struct_size", stride);
        }
    }
    sa1_space = (d->flags & SA1_COLLISION_SA1_SPACE) != 0;
    s->num_groups = d->num_groups;
    s->num_points = d->num_points;
    s->num_tris = d->num_tris;
    s->groups = (sa1ca_group *)sa1_mem_array(ctx, d->num_groups ? d->num_groups : 1u, sizeof(sa1ca_group));
    s->points = (float *)sa1_mem_array(ctx, d->num_points ? 3u * (size_t)d->num_points : 1u, sizeof(float));
    s->tris = (sa1ca_tri *)sa1_mem_array(ctx, d->num_tris ? d->num_tris : 1u, sizeof(sa1ca_tri));
    if (s->groups == NULL || s->points == NULL || s->tris == NULL) {
        return ca_bad(ctx, s, SA1_ERR_OUT_OF_MEMORY, "out of memory", 0);
    }
    for (i = 0; i < d->num_points; i++) {
        const float *p = d->points + 3u * (size_t)i;
        if (!ca_coord_ok(p[0]) || !ca_coord_ok(p[1]) || !ca_coord_ok(p[2])) {
            return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "point is not finite", i);
        }
        if (sa1_space) {
            memcpy(s->points + 3u * (size_t)i, p, 3 * sizeof(float));
        } else {
            sa1_from_host_point(ctx, p, s->points + 3u * (size_t)i);
        }
    }
    for (i = 0; i < d->num_tris; i++) {
        sa1ca_tri *t = &s->tris[i];
        for (k = 0; k < 3; k++) {
            t->v[k] = d->indices[3u * (size_t)i + k];
        }
        t->material = d->materials ? d->materials[i] : 0u;
        t->flags = d->tri_flags ? d->tri_flags[i] : 0u;
        if (t->flags & ~(uint32_t)SA1_COLTRI_STRIP_ODD) {
            return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "unknown triangle flags", i);
        }
    }
    for (i = 0; i < d->num_groups; i++) {
        const sa1_collision_group *gi = CA_GROUP(d, stride, i);
        sa1ca_group *g = &s->groups[i];
        float pos[3], mc[3], cc[3], sc[3];
        int collidable;
        if (gi->struct_size != stride) {
            return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "group struct_size differs from group 0", i);
        }
        if (gi->flags & ~(uint32_t)(SA1_COLGROUP_RAW_FLAGS | SA1_COLGROUP_EVALFLAGS | SA1_COLGROUP_MODEL_BOUNDS
                                    | SA1_COLGROUP_COL_SPHERE)) {
            return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "unknown group flags", i);
        }
        if (gi->first_point > d->num_points || gi->num_points > d->num_points - gi->first_point
            || gi->first_tri > d->num_tris || gi->num_tris > d->num_tris - gi->first_tri) {
            return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "group range outside the arrays", i);
        }
        if (d->mode == SA1_COLLISION_MODE_FAITHFUL && gi->num_points > SA1_MAX_GROUP_POINTS) {
            return ca_bad(ctx, s, SA1_ERR_LIMIT, "FAITHFUL group with more than 32767 points", i);
        }
        for (k = 0; k < 3; k++) {
            pos[k] = gi->position[k];
            mc[k] = gi->model_center[k];
            cc[k] = gi->col_center[k];
            sc[k] = gi->scale[k];
        }
        for (k = 0; k < 3; k++) {
            if (!ca_coord_ok(pos[k]) || !ca_coord_ok(mc[k]) || !ca_coord_ok(cc[k]) || !ca_coord_ok(sc[k])) {
                return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "group transform / bounds not finite", i);
            }
        }
        if (!ca_coord_ok(gi->model_radius) || !ca_coord_ok(gi->col_radius) || !ca_coord_ok(gi->col_ywidth)
            || !ca_coord_ok(gi->col_zwidth)) {
            return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "group radius not finite", i);
        }
        raw = gi->flags & SA1_COLGROUP_RAW_FLAGS;
        g->raw = raw != 0;
        g->surface = raw ? 0u : gi->surface;
        g->col_flags = raw ? gi->surface
                           : (d->mode == SA1_COLLISION_MODE_GRID ? sa1ca_surface_col(gi->surface)
                                                                  : sa1ca_surface_to_col(gi->surface));
        if (sa1_space) {
            memcpy(g->pos, pos, sizeof pos);
            memcpy(g->model_center, mc, sizeof mc);
            memcpy(g->col_center, cc, sizeof cc);
            g->model_r = gi->model_radius;
            g->col_radius = gi->col_radius;
            g->col_ywidth = gi->col_ywidth;
            g->col_zwidth = gi->col_zwidth;
        } else {
            sa1_from_host_point(ctx, pos, g->pos);
            sa1_from_host_point(ctx, mc, g->model_center);
            sa1_from_host_point(ctx, cc, g->col_center);
            g->model_r = ca_len_from_host(ctx, gi->model_radius);
            g->col_radius = ca_len_from_host(ctx, gi->col_radius);
            g->col_ywidth = ca_len_from_host(ctx, gi->col_ywidth);
            g->col_zwidth = ca_len_from_host(ctx, gi->col_zwidth);
        }
        for (k = 0; k < 3; k++) {
            g->ang[k] = (uint32_t)gi->angle[k];
        }
        if (sc[0] == 0.0f && sc[1] == 0.0f && sc[2] == 0.0f && !signbit(sc[0]) && !signbit(sc[1])
            && !signbit(sc[2])) {
            sc[0] = sc[1] = sc[2] = 1.0f;
        }
        memcpy(g->scl, sc, sizeof sc);
        g->col_blockbit = gi->col_blockbit;
        g->first_point = gi->first_point;
        g->num_points = gi->num_points;
        g->first_tri = gi->first_tri;
        g->num_tris = gi->num_tris;
        if (gi->flags & SA1_COLGROUP_EVALFLAGS) {
            g->evalflags = gi->evalflags;
        } else {
            g->evalflags = (g->pos[0] == 0.0f && g->pos[1] == 0.0f && g->pos[2] == 0.0f ? 0x1u : 0u)
                           | ((g->ang[0] | g->ang[1] | g->ang[2]) == 0 ? 0x2u : 0u)
                           | (g->scl[0] == 1.0f && g->scl[1] == 1.0f && g->scl[2] == 1.0f ? 0x4u : 0u);
        }
        for (k = 0; k < g->num_tris; k++) {
            const sa1ca_tri *t = &s->tris[g->first_tri + k];
            if (t->v[0] >= g->num_points || t->v[1] >= g->num_points || t->v[2] >= g->num_points) {
                return ca_bad(ctx, s, SA1_ERR_INVALID_ARGUMENT, "triangle index outside its group", g->first_tri + k);
            }
        }
        if (!(gi->flags & SA1_COLGROUP_MODEL_BOUNDS)) {
            ca_model_bounds(s, g);
        }
        if (!(gi->flags & SA1_COLGROUP_COL_SPHERE)) {
            ca_col_sphere(g);
        }
        collidable = (g->col_flags & SA1CA_COL_COLLIDABLE) != 0;
        s->collidable_groups += (uint32_t)collidable;
        if (collidable) {
            s->collidable_tris += g->num_tris;
        }
        for (k = 0; k < g->num_tris; k++) {
            s->odd_tris += (s->tris[g->first_tri + k].flags & SA1_COLTRI_STRIP_ODD) != 0;
        }
    }
    *out = s;
    return SA1_OK;
}

/* ------------------------------------------------------------------------------------------------ */
/* 2. FAITHFUL synthesis (design 5.3)                                                               */
/* ------------------------------------------------------------------------------------------------ */
/*
 * One allocation:
 *   _OBJ_LANDTABLE (0x24)
 *   COL[num_groups] (0x24 each)
 *   per group: NJS_OBJECT (0x34), NJS_MODEL (0x28), NJS_MATERIAL[nmat] (0x14), NJS_MESHSET[nms] (0x18),
 *              points (12 each), meshes (Sint16, padded to 4 bytes)
 * Materials: one per distinct material word, in order of first use. Meshsets: one per run of
 * triangles with the same (material, even/odd) in the given order; even triangles as type-0
 * polygons {a, b, c}, odd ones as one-triangle strips {0x8003, a, c, b} (sa1_coladapt.h).
 */
#define CA_MATS_CAP (SA1CA_MATID_MAX + 1u)

typedef struct ca_plan {
    uint32_t nmat, nms, nshorts;
} ca_plan;

typedef struct ca_out {
    uint8_t *ms;                /* meshset array (host) */
    uint8_t *mesh;              /* Sint16 meshes (host) */
    uint32_t mesh_guest;
} ca_out;

static uint32_t ca_align4(uint32_t n)
{
    return (n + 3u) & ~3u;
}

/* Walks the triangles of a group: materials (mats[], first-use order), meshset runs and mesh words.
 * With o == NULL it only counts (the plan); with o it writes the meshsets and the meshes. */
static sa1_status ca_group_meshes(const sa1ca_static *s, const sa1ca_group *g, uint32_t *mats, ca_plan *p,
                                  const ca_out *o)
{
    uint32_t k, n = 0, nms = 0, nsh = 0, run = 0, key = 0, last = 0;
    int have = 0;
    uint8_t *cur = NULL;
    for (k = 0; k < g->num_tris; k++) {
        const sa1ca_tri *t = &s->tris[g->first_tri + k];
        uint32_t j, odd = (t->flags & SA1_COLTRI_STRIP_ODD) != 0, kk;
        if (have && mats[last] == t->material) {
            j = last;
        } else {
            for (j = 0; j < n && mats[j] != t->material; j++) {
            }
            if (j == n) {
                if (n >= CA_MATS_CAP) {
                    return SA1_ERR_LIMIT;
                }
                mats[n++] = t->material;
            }
            last = j;
            have = 1;
        }
        kk = (odd ? SA1CA_MESH_STRIP : 0u) | j;
        if (run == 0 || kk != key || run == 0xFFFFu) {
            if (o != NULL) {
                cur = o->ms + (size_t)nms * SA1CA_MESHSET_SIZE;
                ca_wr16(cur, kk);
                ca_wr32(cur + 4, o->mesh_guest + 2u * nsh);
            }
            nms++;
            run = 0;
            key = kk;
        }
        run++;
        if (o != NULL) {
            uint8_t *m = o->mesh + 2u * (size_t)nsh;
            ca_wr16(cur + 2, run);
            if (odd) {
                ca_wr16(m, SA1CA_STRIP_FLIP | 3u);
                ca_wr16(m + 2, t->v[0]);
                ca_wr16(m + 4, t->v[2]);
                ca_wr16(m + 6, t->v[1]);
            } else {
                ca_wr16(m, t->v[0]);
                ca_wr16(m + 2, t->v[1]);
                ca_wr16(m + 4, t->v[2]);
            }
        }
        nsh += odd ? 4u : 3u;
    }
    p->nmat = n;
    p->nms = nms;
    p->nshorts = nsh;
    return SA1_OK;
}

static uint32_t ca_group_bytes(const sa1ca_group *g, const ca_plan *p)
{
    return SA1CA_OBJECT_SIZE + SA1CA_MODEL_SIZE + SA1CA_MATERIAL_SIZE * p->nmat + SA1CA_MESHSET_SIZE * p->nms
           + 12u * g->num_points + ca_align4(2u * p->nshorts);
}

/* plans of every group and the total size (plans may be NULL) */
static sa1_status ca_faithful_plan(sa1_context *ctx, const sa1ca_static *s, uint32_t *mats, ca_plan *plans,
                                   size_t *bytes)
{
    size_t total = SA1CA_LANDTABLE_SIZE + (size_t)SA1CA_COL_SIZE * s->num_groups;
    uint32_t i;
    for (i = 0; i < s->num_groups; i++) {
        ca_plan p;
        sa1_status st = ca_group_meshes(s, &s->groups[i], mats, &p, NULL);
        if (st != SA1_OK) {
            return sa1_fail(ctx, st, "collision adapter: group %u has more than %u materials", (unsigned)i,
                            (unsigned)CA_MATS_CAP);
        }
        if (plans != NULL) {
            plans[i] = p;
        }
        total += ca_group_bytes(&s->groups[i], &p);
        if (total > (size_t)0x7FFFFFFF) {
            return sa1_fail(ctx, SA1_ERR_LIMIT, "collision adapter: landtable larger than 2 GB");
        }
    }
    *bytes = total;
    return SA1_OK;
}

sa1_status sa1ca_faithful_size(sa1_context *ctx, const sa1ca_static *s, size_t *bytes)
{
    uint32_t *mats;
    sa1_status st;
    *bytes = 0;
    if (s == NULL || s->magic != SA1CA_MAGIC) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    mats = (uint32_t *)sa1_mem_array(ctx, CA_MATS_CAP, sizeof(uint32_t));
    if (mats == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    st = ca_faithful_plan(ctx, s, mats, NULL, bytes);
    sa1_mem_free(ctx, mats);
    return st;
}

sa1_status sa1ca_build_faithful(sa1_context *ctx, const sa1ca_static *s, sa1ca_alloc_fn alloc, void *user,
                                sa1ca_built *out)
{
    uint32_t *mats = NULL, base, i, k, off;
    ca_plan *plans = NULL;
    size_t total = 0;
    uint8_t *host = NULL;
    sa1_status st;
    memset(out, 0, sizeof *out);
    if (s == NULL || s->magic != SA1CA_MAGIC || alloc == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    mats = (uint32_t *)sa1_mem_array(ctx, CA_MATS_CAP, sizeof(uint32_t));
    plans = (ca_plan *)sa1_mem_array(ctx, s->num_groups ? s->num_groups : 1u, sizeof(ca_plan));
    if (mats == NULL || plans == NULL) {
        st = SA1_ERR_OUT_OF_MEMORY;
        goto done;
    }
    st = ca_faithful_plan(ctx, s, mats, plans, &total);
    if (st != SA1_OK) {
        goto done;
    }
    base = alloc(user, total, 16, (void **)&host);
    if (base == 0 || host == NULL) {
        st = sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "collision adapter: no room for %u bytes of landtable",
                      (unsigned)total);
        goto done;
    }
    memset(host, 0, total);
    /* _OBJ_LANDTABLE: no motion entries, no textures, no binary file (ssAttribute 0) */
    ca_wr16(host + 0x00, s->num_groups);
    ca_wr32(host + 0x0C, base + SA1CA_LANDTABLE_SIZE);
    off = SA1CA_LANDTABLE_SIZE + SA1CA_COL_SIZE * s->num_groups;
    for (i = 0; i < s->num_groups; i++) {
        const sa1ca_group *g = &s->groups[i];
        const ca_plan *p = &plans[i];
        uint8_t *col = host + SA1CA_LANDTABLE_SIZE + (size_t)SA1CA_COL_SIZE * i;
        uint32_t o_obj = off, o_model = o_obj + SA1CA_OBJECT_SIZE, o_mats = o_model + SA1CA_MODEL_SIZE;
        uint32_t o_ms = o_mats + SA1CA_MATERIAL_SIZE * p->nmat, o_pts = o_ms + SA1CA_MESHSET_SIZE * p->nms;
        uint32_t o_mesh = o_pts + 12u * g->num_points;
        uint8_t *obj = host + o_obj, *model = host + o_model;
        ca_plan check;
        ca_out o;
        /* COL entry */
        ca_wrf(col + 0x00, g->col_center[0]);
        ca_wrf(col + 0x04, g->col_center[1]);
        ca_wrf(col + 0x08, g->col_center[2]);
        ca_wrf(col + 0x0C, g->col_radius);
        ca_wrf(col + 0x10, g->col_ywidth);
        ca_wrf(col + 0x14, g->col_zwidth);
        ca_wr32(col + 0x18, base + o_obj);
        ca_wr32(col + 0x1C, g->col_blockbit);
        ca_wr32(col + 0x20, g->col_flags);
        /* NJS_OBJECT */
        ca_wr32(obj + 0x00, g->evalflags);
        ca_wr32(obj + 0x04, base + o_model);
        for (k = 0; k < 3; k++) {
            ca_wrf(obj + 0x08 + 4 * k, g->pos[k]);
            ca_wr32(obj + 0x14 + 4 * k, g->ang[k]);
            ca_wrf(obj + 0x20 + 4 * k, g->scl[k]);
        }
        /* NJS_MODEL */
        ca_wr32(model + 0x00, g->num_points ? base + o_pts : 0u);
        ca_wr32(model + 0x08, g->num_points);
        ca_wr32(model + 0x0C, p->nms ? base + o_ms : 0u);
        ca_wr32(model + 0x10, p->nmat ? base + o_mats : 0u);
        ca_wr16(model + 0x14, p->nms);
        ca_wr16(model + 0x16, p->nmat);
        for (k = 0; k < 3; k++) {
            ca_wrf(model + 0x18 + 4 * k, g->model_center[k]);
        }
        ca_wrf(model + 0x24, g->model_r);
        /* points, bit for bit */
        for (k = 0; k < 3u * g->num_points; k++) {
            ca_wrf(host + o_pts + 4u * k, s->points[3u * (size_t)g->first_point + k]);
        }
        /* meshsets + meshes, then the materials found on the way */
        o.ms = host + o_ms;
        o.mesh = host + o_mesh;
        o.mesh_guest = base + o_mesh;
        st = ca_group_meshes(s, g, mats, &check, &o);
        if (st != SA1_OK || check.nmat != p->nmat || check.nms != p->nms || check.nshorts != p->nshorts) {
            st = sa1_fail(ctx, SA1_ERR_LIMIT, "collision adapter: group %u changed between passes", (unsigned)i);
            goto done;
        }
        for (k = 0; k < p->nmat; k++) {
            ca_wr32(host + o_mats + SA1CA_MATERIAL_SIZE * k + 0x10, mats[k]);
        }
        off += ca_group_bytes(g, p);
        out->meshsets += p->nms;
        out->materials += p->nmat;
        out->points += g->num_points;
        out->triangles += g->num_tris;
        for (k = 0; k < g->num_tris; k++) {
            out->odd_triangles += (s->tris[g->first_tri + k].flags & SA1_COLTRI_STRIP_ODD) != 0;
        }
    }
    if ((size_t)off != total) {
        st = sa1_fail(ctx, SA1_ERR_LIMIT, "collision adapter: layout %u != planned %u", (unsigned)off,
                      (unsigned)total);
        goto done;
    }
    out->landtable = base;
    out->cols = base + SA1CA_LANDTABLE_SIZE;
    out->base = base;
    out->bytes = (uint32_t)total;
    out->objects = s->num_groups;
    out->generation = s->generation;
    sa1_log(ctx, SA1_LOG_DEBUG,
            "collision adapter: FAITHFUL landtable %08X: %u COLs, %u meshsets, %u materials, %u points, %u triangles "
            "(%u odd), %u bytes",
            (unsigned)base, (unsigned)s->num_groups, (unsigned)out->meshsets, (unsigned)out->materials,
            (unsigned)out->points, (unsigned)out->triangles, (unsigned)out->odd_triangles, (unsigned)total);
done:
    if (st != SA1_OK) {
        memset(out, 0, sizeof *out);
    }
    sa1_mem_free(ctx, plans);
    sa1_mem_free(ctx, mats);
    return st;
}

/* ------------------------------------------------------------------------------------------------ */
/* 3. GRID synthesis (design 5.4)                                                                   */
/* ------------------------------------------------------------------------------------------------ */
/*
 * Input: set_static's flat triangles (one COL word per triangle from its surface kinds) or set_static_ex's
 * GRID groups (transformed to world space in double; raw COL words or surface kinds; per-triangle material
 * words and SA1_COLTRI_STRIP_ODD marks).
 *
 *   0. (round 0.9.2, GRID-CAP) a counting pass over a-c without storing anything (ca_grid_count): more pieces than
 *      CA_FRAG_MAX or an estimated layout over the 64 MB segment fail here, with the estimate and the longest face;
 *   a. drop non-finite and degenerate triangles (sine of the smallest angle below 1e-6); water and kill
 *      triangles of host surfaces keep their upward-facing triangles only (the game's water probe 8C0925A0
 *      and death-zone probe 8C07BAC0 look for the lowest surface above the character, so a bottom face
 *      would count as a surface);
 *   b. cut triangles longer than max_edge into well-shaped pieces (8C029400 / 8C02A680 / 8C02D2E0 assume < 220
 *      units; 8C029B00's angle quantisation errs by edge length x 9.6e-5): consecutive triangles of one planar convex
 *      face (a quad or fan, one surface word) are merged back into the face first (ca_grid_merge), then a grid in the
 *      face's plane along its longest edge, cells at most max_edge across their diagonal, each cell fan-triangulated
 *      (ca_grid_poly; it replaced longest-edge halving, which turned long narrow faces into needles that filled the
 *      NEAR-poly list, docs/research/gsa1_ramp_freeze.md), and give each piece to the cell (cube of cell_size) of
 *      its centroid:
 *      whole triangles keep the input's corners and order, so an object holds exactly the original polygons and
 *      SA1_COLTRI_STRIP_ODD triangles are emitted as one-triangle strips (as FAITHFUL); objects overhang their cell
 *      by at most 2/3 of max_edge;
 *   c. or, with SA1_GRID_CUT, cut each triangle / face at the cell boundaries first (Sutherland-Hodgman per cell of
 *      its bounding box, the cut points computed once per edge and plane so that neighbouring pieces share them),
 *      then cut the pieces as in b: tight cells, more and smaller polygons;
 *   c2. leave out the undersides within reach (ca_grid_undersides, docs/research/gsa1_ramp_freeze.md mechanism B): a
 *      single-sided land piece that faces down (unit normal y < -0.1) and lies, at every one of 7 sample points, at most
 *      under_reach (default 12 units) behind an upward-facing land piece (normal y >= 0.3, measured along that piece's
 *      normal, the point's projection inside it) is dropped, unless SA1_GRID_KEEP_UNDERSIDES. The game's TOUCH test
 *      8C029B00 takes a single-sided polygon within the sphere's radius from behind too and, per land object, selects
 *      the touch with the lowest height after the move: a touch from behind always has a negative height, so the
 *      underside of a slab thinner than the sphere's reach (a plank, a thin platform, a slope's buried end cap) won
 *      over the slab's top whenever the sphere came within reach of it (the probe sits up to 3.6 units low, and an entry
 *      at speed dips 5-10 units into a slope), and its push-back and its downward normal sent the character under the
 *      slab (ramp_diag slab / under / case). Without the underside the top alone pushes a sphere that went into the
 *      slab back out above it. Cost: such a slab was a one-way platform (a character could jump up through it from below,
 *      as G-SA1's one-way thin platforms). Round 0.9.3 (sadc docs/research/gsa1_091b_ceiling.md FIX-C2 and
 *      gsa1_091a_loop.md LOOP-L1, with their Reviews): such an underside that is near-horizontal (normal y <= -0.97, within 14 degrees of level) stays,
 *      as a FRONT-ONLY piece (material SA1_MAT_FRONT_ONLY: the lowered 8C029B00 of a host world does not touch it from
 *      behind, SA1_FRONTONLY), so it is a ceiling from below and absent from above; steeper ones (a slope's buried end cap,
 *      touched from its front by a runner at dash speed) are dropped as before; a piece of a curved surface
 *      (SA1_SURFACE_CURVED: a loop, whose ceiling lies within reach of its outer face) is kept as it is.
 *      SA1_GRID_UNDERSIDES_DROP drops every underside within reach and every FRONT_ONLY piece of the input (before 0.9.3);
 *   d. one object per (cell, COL word, orientation class: floor / wall / ceiling by the piece's normal in SA1 space, round
 *      0.9.2 F2: the game averages the push-backs of one object's touched polygons, so a wall must not share an object
 *      with the floor at its foot, nor a low ceiling with its floor); a cell with more than CA_OBJ_SPLIT_TRIS triangles
 *      of one COL word and class becomes
 *      several objects of about equal size, the triangles in Z order of their centroids inside the cell (compact
 *      sub-cells: the world's window never has to leave out the object under the character because it alone
 *      exceeds the polygon budget); kill planes: whole triangles per cell of 8 x 8 x 1 cells by centroid, at most
 *      CA_OBJ_MAX_TRIS per object (the death zone task probes them all every step). The triangles of a land
 *      object are stored in descending order of their centroids' z (input order with SA1_GRID_DC_LIMITS): the
 *      cache build 8C028EC2 pushes each polygon onto the head of its 16-unit z bucket (8C02CFC0) and chains the
 *      buckets in ascending order, and the NEAR query 8C028BFE stops at the first polygon past the query's z range,
 *      so polygons out of z order inside a bucket made it stop early; near the high-z end of an object (where it
 *      walks back from the tail) it could find nothing at all and the character fell through (a cell split into
 *      parts has many such ends):
 *        NJS_OBJECT evalflags 6 (the probes take the translate-only path 8C0275E0), pos = the centre of
 *        the object's bounding box rounded to a multiple of the largest ulp of its coordinates, so that
 *        pos + local reproduces every world-space corner exactly when it can; ang 0, scl 1;
 *        NJS_MODEL with the deduplicated points relative to pos, the triangles in input order as type-0
 *        meshsets (uncut SA1_COLTRI_STRIP_ODD triangles as one-triangle strips, as FAITHFUL), one
 *        material per distinct material word, center (0,0,0), r = max |p| (rounded up);
 *        COL center = pos, radius = r + 16, the COL word (land objects only);
 *   e. the land objects sorted by column (ix, iz), then iy and COL word, with an open-addressing column
 *      index and each object's box (the window ranks objects by the character's distance to it) for the world's
 *      window (sa1_coladapt_world.c); the kill objects as a DEATHZONE list behind an
 *      8-entry act table; a full landtable of every land object (information, ssCount capped at 32767).
 * Static cells never get 0x08000000 / 0x10000000 (raw COL words are passed through unchanged).
 */
#define CA_GT_ODD           0x1u        /* SA1_COLTRI_STRIP_ODD of an uncut triangle */
#define CA_GT_KILL          0x2u        /* a kill plane triangle (death zone object) */
#define CA_OBJ_MAX_TRIS     8192u       /* triangles per synthesized object (kill planes) */
#define CA_OBJ_SPLIT_TRIS   1024u       /* land triangles per synthesized object: a fuller cell is split (d) */
#define CA_OBJ_MAX_POINTS   32767u      /* point indices are Sint16 */
#define CA_LAYOUT_HASH      (1u << 16)  /* point hash of ca_grid_layout_object: 6 slots per triangle of an object */
typedef char ca_layout_hash_fits[(6u * CA_OBJ_MAX_TRIS <= CA_LAYOUT_HASH && 6u * CA_OBJ_SPLIT_TRIS <= CA_LAYOUT_HASH)
                                 ? 1 : -1];
#define CA_FRAG_MAX         (4u << 20)  /* triangles of one synthesis after cutting (76 bytes each while building: 319 MB
                                           of host memory at the limit). The binding limit is the 64 MB segment, which
                                           holds about 2.75-3.0 million GRID triangles of a real map (22.5-24.3 bytes
                                           each, sadc docs/research/gsa1_090_water.md 6.1 with its Review R2.5): the
                                           counting pass (ca_grid_count) refuses both before the cut */
#define CA_CLIP_MAX_CELLS   4096u       /* cells of one triangle's bounding box before it is split first */
#define CA_POLY_MAX         24          /* vertices of a clipped polygon (a merged face of 8 by 6 cell planes and 4 grid
                                           planes: 18) */
#define CA_KILL_CELLS       8.0         /* kill plane objects: cells of 8 x cell_size in x and z */
#define CA_UNDER_REACH      12.0f       /* default under_reach (SA1 units): a slab thinner than the reach of the largest
                                           spheres behind a dip (Knuckles 5.7 + a 10-unit entry dip; c2) */
#define CA_UNDER_FACE_NY    (-0.1)      /* c2: a piece faces down (unit normal y below this) */
#define CA_UNDER_COVER_NY   0.3         /* c2: a covering piece faces up (unit normal y at least this) */
#define CA_UNDER_TOL        0.05        /* c2: plane / edge tolerance (units) */
#define CA_UNDER_CELL       128.0       /* c2: x / z cells of the covering pieces' index (a piece, at most about 90 wide, in
                                           1-4 of them) */
#define CA_GT_DROP          0x4u        /* c2: an underside within reach, removed before the objects are made */
#define CA_GT_CURVED        0x8u        /* (round 0.9.3, LOOP-L1) a piece of a SA1_SURFACE_CURVED triangle (split / cut pieces
                                           inherit it): c2 keeps it as it is */
#define CA_GT_BSNOW         0x10u       /* (round 0.9.5, review R.1) SA1_SURFACE_BOARD_SNOW: part of the object key */
#define CA_GT_BSAND         0x20u       /* (round 0.9.5) SA1_SURFACE_BOARD_SAND */
#define CA_GT_BOARD         (CA_GT_BSNOW | CA_GT_BSAND)
#define CA_UNDER_FLAT_NY    (-0.97)     /* (round 0.9.3, FIX-C2) c2: an underside within reach this flat (unit normal y at most
                                           this: within 14 degrees of level) becomes front-only instead of being dropped. The doc's
                                           -0.9 (25.8 degrees) let a runner at dash-panel speed dip under thin plank ramps of 20-25
                                           degrees at their foot (ramp_diag slab: the slab's top shares a GRID object with the floor
                                           there, so its lift is averaged, and the front-only underside, now in front of the
                                           sphere, pushed it 11 units down; dropped, as before, nothing pushes back) */

static float ca_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int sa1ca_grid_valid(const sa1_collision_grid *gp)
{
    return gp != NULL && gp->struct_size >= offsetof(sa1_collision_grid, under_reach)
           && (gp->flags & ~(uint32_t)(SA1_GRID_CUT | SA1_GRID_DC_LIMITS | SA1_GRID_KEEP_UNDERSIDES
                                       | SA1_GRID_MIXED_ORIENTATIONS | SA1_GRID_UNDERSIDES_DROP)) == 0
           && sa1_finite(gp->cell_size) && gp->cell_size >= 0.0f && sa1_finite(gp->max_edge) && gp->max_edge >= 0.0f
           && (gp->struct_size < offsetof(sa1_collision_grid, under_reach) + sizeof gp->under_reach
               || (sa1_finite(gp->under_reach) && gp->under_reach >= 0.0f));
}

void sa1ca_grid_defaults(const sa1_collision_grid *g, sa1ca_grid_params *out)
{
    out->flags = 0;
    out->cell = 256.0f;
    out->max_edge = 128.0f;
    out->win_objects = 128u;
    out->win_polys = 7680u;
    out->under_reach = CA_UNDER_REACH;
    if (g == NULL) {
        return;
    }
    out->flags = g->flags & (SA1_GRID_CUT | SA1_GRID_DC_LIMITS | SA1_GRID_KEEP_UNDERSIDES | SA1_GRID_MIXED_ORIENTATIONS
                             | SA1_GRID_UNDERSIDES_DROP);
    if (g->struct_size >= offsetof(sa1_collision_grid, under_reach) + sizeof g->under_reach && g->under_reach > 0.0f) {
        out->under_reach = ca_clampf(g->under_reach, 0.5f, 64.0f);
    }
    if (g->cell_size > 0.0f) {
        out->cell = ca_clampf(g->cell_size, 16.0f, 2048.0f);
    }
    if (g->max_edge > 0.0f) {
        out->max_edge = ca_clampf(g->max_edge, 8.0f, 220.0f);
    }
    if (g->window_objects != 0) {
        out->win_objects = g->window_objects > 128u ? 128u : g->window_objects;
    }
    if (g->window_polygons != 0) {
        out->win_polys = g->window_polygons < 256u ? 256u : (g->window_polygons > 8000u ? 8000u : g->window_polygons);
    }
}

typedef struct ca_frag {
    float v[9];                 /* world-space corners, winding order */
    int32_t key[3];             /* cell ix, iy, iz */
    uint32_t col, mat, flags;   /* COL word, material word, CA_GT_* */
    uint32_t seq;               /* input order */
    uint32_t sub;               /* Z-order key of the centroid inside the cell (cells split into several objects) */
    uint16_t idx[3];            /* point indices in the object (layout pass) */
    uint16_t ori;               /* CA_ORI_*: the orientation class, part of the object key (d; land pieces only) */
} ca_frag;

/* F2 (round 0.9.2, sadc docs/research/gsa1_090_collision.md C1 / F2 with its Review R.3.4): a land object holds the pieces
 * of one orientation class, in SA1 space. 8C029B00 (TOUCH) averages the push-backs of every polygon of one land object the
 * sphere touches, so a wall (or a thin pair) sharing an object with the floor at its foot got part of its push-back and the
 * landing slid the character into it (or through the floor); a low ceiling averaged with its floor */
#define CA_ORI_FLOOR    0u      /* unit normal y >= 0.5 */
#define CA_ORI_WALL     1u      /* |y| < 0.5 */
#define CA_ORI_CEILING  2u      /* y <= -0.5 */

/* GRID-CAP (round 0.9.2, sadc docs/research/gsa1_090_water.md 6.4 / GRID-CAP with its Review R2.5): a counting pass over the
 * input before the fragments are allocated: every piece the cut makes (the same code, ca_frag_push counting instead of
 * storing), the distinct objects and points of the layout estimated with HyperLogLog sketches */
#define CA_HLL_BITS     16
#define CA_HLL_M        (1u << CA_HLL_BITS)
#define CA_CAP_MARGIN   0.97    /* refuse early only when 97 % of the estimate is over the segment (the sketch's error is
                                   about 0.4 %; undersides left out later (c2) and split parts make the layout differ a
                                   little): nearer than that the synthesis runs and the layout pass decides exactly */
#define CA_OBJ_OVERHEAD (SA1CA_OBJECT_SIZE + SA1CA_MODEL_SIZE + SA1CA_MATERIAL_SIZE + SA1CA_MESHSET_SIZE + SA1CA_COL_SIZE \
                         + 4u + 24u + 2u * SA1CA_GRID_HASH_ENTRY)    /* per land object: at least one material and meshset;
                                                                          COL, triangle count, box, column hash share */

typedef struct ca_gobj {
    uint32_t first, count;      /* fragments */
    uint32_t pfirst, npts;      /* points (world space) in ca_gb.pts */
    uint32_t mfirst, nmat;      /* material words in ca_gb.mats */
    uint32_t nms, nshorts;
    uint32_t col, kill;
    float pos[3], r;
    float lo[3], hi[3];         /* box of the points (world space, rounded outward) */
    uint32_t board;             /* round 0.9.5: the pieces' board bits (CA_GT_BOARD >> 4: 1 snow, 2 sand) */
} ca_gobj;

typedef struct ca_gb {
    sa1_context *ctx;
    const sa1ca_grid_params *gp;
    double cell, edge2;
    ca_frag *fr;
    size_t nfr, capfr;
    uint32_t seq, split, cut, dropped, inexact, split_objects, merged, undersides;
    uint32_t front_marked;      /* (round 0.9.3) c2: undersides within reach made front-only */
    uint32_t curved;            /* (round 0.9.3) c2: CURVED undersides within reach kept */
    sa1_status st;
    /* layout */
    ca_gobj *obj;
    size_t nobj, capobj;
    float *pts;
    size_t npts, cappts;
    uint32_t *mats;
    size_t nmats, capmats;
    /* GRID-CAP counting pass (count_only: ca_frag_push counts) */
    int count_only;
    uint64_t cnt_land, cnt_kill, cnt_odd;
    uint8_t *hll_obj, *hll_pts;             /* CA_HLL_M registers each */
    double big_len2;                        /* the face with the longest edge seen (squared, SA1 units) */
    float big_at[3];                        /* its centroid (SA1 space) */
    uint64_t big_pieces;                    /* the pieces it was cut into */
} ca_gb;

static int ca_grow(sa1_context *ctx, void **p, size_t *cap, size_t need, size_t elem, size_t used)
{
    size_t ncap = *cap ? *cap : 256u;
    void *q;
    if (need <= *cap) {
        return 1;
    }
    while (ncap < need) {
        if (ncap > ((size_t)-1) / 4u / elem) {
            return 0;
        }
        ncap *= 2u;
    }
    q = sa1_mem_array(ctx, ncap, elem);
    if (q == NULL) {
        return 0;
    }
    if (*p != NULL && used != 0) {
        memcpy(q, *p, used * elem);
    }
    sa1_mem_free(ctx, *p);
    *p = q;
    *cap = ncap;
    return 1;
}

/* the orientation class of a piece (F2): its normal in SA1 space (y up) */
static uint16_t ca_orientation(const double v[9])
{
    double e1[3], e2[3], n[3], nn;
    int k;
    for (k = 0; k < 3; k++) {
        e1[k] = v[3 + k] - v[k];
        e2[k] = v[6 + k] - v[k];
    }
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    nn = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!(nn > 0.0)) {
        return CA_ORI_FLOOR;
    }
    return n[1] >= 0.5 * nn ? CA_ORI_FLOOR : (n[1] <= -0.5 * nn ? CA_ORI_CEILING : CA_ORI_WALL);
}

/* ---- GRID-CAP: HyperLogLog sketches of the counting pass ---- */
static uint64_t ca_mix64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

static void ca_hll_add(uint8_t *reg, uint64_t h)
{
    uint32_t j = (uint32_t)(h >> (64 - CA_HLL_BITS));
    uint64_t w = h << CA_HLL_BITS;
    uint8_t r;
#if defined(__GNUC__) || defined(__clang__)
    r = w ? (uint8_t)(__builtin_clzll(w) + 1) : (uint8_t)(64 - CA_HLL_BITS + 1);
#else
    r = 1;
    while (r <= 64 - CA_HLL_BITS && !(w & 0x8000000000000000ull)) {
        w <<= 1;
        r++;
    }
#endif
    if (r > reg[j]) {
        reg[j] = r;
    }
}

static double ca_hll_count(const uint8_t *reg)
{
    double m = (double)CA_HLL_M, sum = 0.0, e;
    uint32_t j, zeros = 0;
    for (j = 0; j < CA_HLL_M; j++) {
        sum += ldexp(1.0, -(int)reg[j]);
        zeros += reg[j] == 0;
    }
    e = (0.7213 / (1.0 + 1.079 / m)) * m * m / sum;
    if (e <= 2.5 * m && zeros != 0) {
        e = m * log(m / (double)zeros);     /* small range: linear counting */
    }
    return e;
}

/* the bytes the layout pass will need, from the counting pass (sa1ca_build_grid's layout, see ca_gobj_bytes) */
static double ca_cap_bytes(const ca_gb *b, double *objects, double *points)
{
    *objects = b->cnt_land ? ca_hll_count(b->hll_obj) : 0.0;
    *points = b->cnt_land ? ca_hll_count(b->hll_pts) : 0.0;
    return (double)SA1CA_GRID_HDR_SIZE + (double)SA1CA_LANDTABLE_SIZE + *objects * (double)CA_OBJ_OVERHEAD
           + 12.0 * *points + 6.0 * (double)b->cnt_land + 2.0 * (double)b->cnt_odd
           + (12.0 * 3.0 + 6.0 + 8.0) * (double)b->cnt_kill;
}

static void ca_frag_count(ca_gb *b, const double v[9], const int32_t key[3], uint32_t col, uint32_t flags)
{
    uint64_t h;
    uint32_t k;
    if (flags & CA_GT_KILL) {
        b->cnt_kill++;
        return;
    }
    if (++b->cnt_land > CA_FRAG_MAX) {
        b->st = SA1_ERR_LIMIT;      /* exactly what the synthesis would meet in ca_frag_push (sa1ca_build_grid words it) */
        return;
    }
    b->cnt_odd += (flags & CA_GT_ODD) != 0;
    h = ca_mix64((((uint64_t)(uint32_t)key[0] << 32) ^ (uint32_t)key[2]) * 0x9E3779B97F4A7C15ull
                 ^ (((uint64_t)(uint32_t)key[1] << 32) ^ col) * 0xC2B2AE3D27D4EB4Full
                 ^ ((b->gp->flags & SA1_GRID_MIXED_ORIENTATIONS) ? 0u : (uint64_t)ca_orientation(v) << 61));
    ca_hll_add(b->hll_obj, h);
    for (k = 0; k < 3; k++) {
        float x = (float)v[3 * k], y = (float)v[3 * k + 1], z = (float)v[3 * k + 2];
        uint32_t ux, uy, uz;
        memcpy(&ux, &x, 4);
        memcpy(&uy, &y, 4);
        memcpy(&uz, &z, 4);
        ca_hll_add(b->hll_pts, ca_mix64(h ^ (((uint64_t)ux << 32) | uy) * 0x9E3779B97F4A7C15ull
                                        ^ (uint64_t)uz * 0x85EBCA77C2B2AE63ull));
    }
}

static void ca_frag_push(ca_gb *b, const double v[9], const int32_t key[3], uint32_t col, uint32_t mat,
                         uint32_t flags)
{
    ca_frag *f;
    uint32_t k;
    if (b->st != SA1_OK) {
        return;
    }
    if (b->count_only) {
        ca_frag_count(b, v, key, col, flags);
        return;
    }
    if (b->nfr >= CA_FRAG_MAX) {
        b->st = sa1_fail(b->ctx, SA1_ERR_LIMIT, "collision adapter: GRID: more than %u triangles after cutting",
                         (unsigned)CA_FRAG_MAX);
        return;
    }
    if (!ca_grow(b->ctx, (void **)&b->fr, &b->capfr, b->nfr + 1u, sizeof *b->fr, b->nfr)) {
        b->st = SA1_ERR_OUT_OF_MEMORY;
        return;
    }
    f = &b->fr[b->nfr++];
    memset(f, 0, sizeof *f);
    for (k = 0; k < 9; k++) {
        f->v[k] = (float)v[k];
    }
    f->key[0] = key[0];
    f->key[1] = key[1];
    f->key[2] = key[2];
    f->col = col;
    f->mat = mat;
    f->flags = flags;
    f->seq = b->seq++;
    f->ori = (flags & CA_GT_KILL) || (b->gp->flags & SA1_GRID_MIXED_ORIENTATIONS) ? CA_ORI_FLOOR : ca_orientation(v);
}

/* twice the area and the longest squared edge */
static void ca_tri_metrics(const double v[9], double n[3], double *nn, double *l2)
{
    double e1[3], e2[3], e3[3], a, c, d;
    int k;
    for (k = 0; k < 3; k++) {
        e1[k] = v[3 + k] - v[k];
        e2[k] = v[6 + k] - v[k];
        e3[k] = v[6 + k] - v[3 + k];
    }
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    *nn = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    a = e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2];
    c = e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2];
    d = e3[0] * e3[0] + e3[1] * e3[1] + e3[2] * e3[2];
    *l2 = a > c ? (a > d ? a : d) : (c > d ? c : d);
}

/* |n| / l2 is twice the sine of the smallest angle times (shortest / longest edge): below 2e-6 the
 * triangle is a sliver the game cannot collide with meaningfully */
static int ca_degenerate(const double v[9])
{
    double n[3], nn, l2;
    ca_tri_metrics(v, n, &nn, &l2);
    return !(l2 > 0.0) || !(nn > 2e-6 * l2) || !isfinite(nn);
}

static int32_t ca_cell_of(double x, double cell)
{
    double q = floor(x / cell);
    return q < -2147483647.0 ? -2147483647 : (q > 2147483647.0 ? 2147483647 : (int32_t)q);
}

/* the dot product the cuts use (one expression, so that a vertex is classified the same way by every cut) */
static double ca_dot(const double d[3], const double *v)
{
    return d[0] * v[0] + d[1] * v[1] + d[2] * v[2];
}

/* the point of segment p-q on the plane dot(d, x) = t (p and q on either side), the same bits for (p, q) and
 * (q, p): neighbouring pieces cut from the same edge share their cut points */
static void ca_cut_dir(const double *p, const double *q, const double d[3], double t, double *out)
{
    const double *a = p, *b = q;
    double sa, sb, f;
    int k;
    if (q[0] < p[0] || (q[0] == p[0] && (q[1] < p[1] || (q[1] == p[1] && q[2] < p[2])))) {
        a = q;
        b = p;
    }
    sa = ca_dot(d, a) - t;
    sb = ca_dot(d, b) - t;
    f = sa / (sa - sb);
    for (k = 0; k < 3; k++) {
        out[k] = a[k] + f * (b[k] - a[k]);
    }
}

/* keep the part of convex polygon in[n] with dot(d, x) >= t (ge) or <= t; returns the vertex count (consecutive
 * duplicates, which a vertex on the plane gives, removed) */
static int ca_clip_dir(const double *in, int n, double *out, const double d[3], double t, int ge)
{
    int i, m = 0;
    for (i = 0; i < n && m < CA_POLY_MAX - 1; i++) {
        const double *a = in + 3 * i, *c = in + 3 * ((i + 1) % n);
        double da = ca_dot(d, a), dc = ca_dot(d, c);
        int ina = ge ? da >= t : da <= t;
        int inc = ge ? dc >= t : dc <= t;
        if (ina) {
            memcpy(out + 3 * m, a, 3 * sizeof(double));
            m++;
        }
        if (ina != inc) {
            ca_cut_dir(a, c, d, t, out + 3 * m);
            if (m == 0 || memcmp(out + 3 * m, out + 3 * (m - 1), 3 * sizeof(double)) != 0) {
                m++;
            }
        }
    }
    while (m > 1 && memcmp(out, out + 3 * (m - 1), 3 * sizeof(double)) == 0) {
        m--;
    }
    return m;
}

static double ca_dist2(const double *a, const double *b)
{
    double x = b[0] - a[0], y = b[1] - a[1], z = b[2] - a[2];
    return x * x + y * y + z * z;
}

/* unit normal of a planar polygon p[n] (Newell; the direction of (p1 - p0) x (p2 - p0) for a triangle); 0 if none */
static int ca_poly_normal(const double *p, int n, double out[3])
{
    double x = 0.0, y = 0.0, z = 0.0, l;
    int i;
    for (i = 0; i < n; i++) {
        const double *a = p + 3 * i, *c = p + 3 * ((i + 1) % n);
        x += (a[1] - c[1]) * (a[2] + c[2]);
        y += (a[2] - c[2]) * (a[0] + c[0]);
        z += (a[0] - c[0]) * (a[1] + c[1]);
    }
    l = sqrt(x * x + y * y + z * z);
    if (!(l > 0.0) || !isfinite(l)) {
        return 0;
    }
    out[0] = x / l;
    out[1] = y / l;
    out[2] = z / l;
    return 1;
}

/* one piece: dropped when degenerate (slivers on the cut lines: the surface is kept), else stored in cell key
 * (NULL: the cell of its centroid) */
static void ca_piece(ca_gb *b, const double v[9], const int32_t key[3], uint32_t col, uint32_t mat, uint32_t flags)
{
    int32_t own[3];
    int k;
    if (ca_degenerate(v)) {
        return;
    }
    if (key == NULL) {
        for (k = 0; k < 3; k++) {
            own[k] = ca_cell_of((v[k] + v[3 + k] + v[6 + k]) / 3.0, b->cell);
        }
        key = own;
    }
    ca_frag_push(b, v, key, col, mat, flags);
}

/* a convex polygon p[n] (winding order) as a fan from the vertex whose longest diagonal is the shortest (a quad is cut
 * along its shorter diagonal) */
static void ca_fan(ca_gb *b, const double *p, int n, const int32_t key[3], uint32_t col, uint32_t mat, uint32_t flags)
{
    double t[9], best = -1.0;
    int r = 0, i, k;
    if (n < 3) {
        return;
    }
    if (n > 3) {
        for (i = 0; i < n; i++) {
            double worst = 0.0;
            for (k = 2; k < n - 1; k++) {
                double d = ca_dist2(p + 3 * i, p + 3 * ((i + k) % n));
                worst = d > worst ? d : worst;
            }
            if (best < 0.0 || worst < best) {
                best = worst;
                r = i;
            }
        }
    }
    for (i = 1; i + 1 < n; i++) {
        memcpy(t, p + 3 * r, 3 * sizeof(double));
        memcpy(t + 3, p + 3 * ((r + i) % n), 3 * sizeof(double));
        memcpy(t + 6, p + 3 * ((r + i + 1) % n), 3 * sizeof(double));
        ca_piece(b, t, key, col, mat, flags);
    }
}

/* the number of equal parts of an extent so that each is at most lim long: the smallest power of two for an extent that
 * is a power of two (binary cells, exact normals), else the smallest count (at most 2^24) */
static uint32_t ca_cut_count(double ext, double lim)
{
    uint32_t n = 1u;
    int e = 0;
    if (!(ext > lim)) {
        return 1u;
    }
    if (frexp(ext, &e) == 0.5) {
        while (ext / (double)n > lim && n < (1u << 24)) {
            n <<= 1;
        }
        return n;
    }
    return ext / lim >= (double)(1u << 24) ? (1u << 24) : (uint32_t)ceil(ext / lim);
}

/*
 * Well-shaped subdivision (docs/research/gsa1_ramp_freeze.md, fix 1) of a convex planar polygon p[n] (an input triangle,
 * a merged face (ca_grid_input) or, with SA1_GRID_CUT, a piece of either clipped to its cell).
 *   A polygon whose vertices are all within max_edge of each other is stored as is: a triangle whole (its corners and
 * order kept: an object holds exactly the original polygons, SA1_COLTRI_STRIP_ODD included), a larger polygon as a fan.
 *   A larger one is cut along a grid laid in its own plane: d1 along its longest edge (oriented from the edge's
 * lexicographically smaller end, so that both faces that share the edge use the same direction), d2 across it. The
 * columns across d1 are at most c = max_edge / sqrt 2 wide, the slabs along d1 as long as the column width allows with a
 * cell diagonal of at most max_edge, in equal parts; an extent that is a power of two is cut into a power of two of parts,
 * so that a face with binary sizes (a power-of-two floor) gives binary cells and exact normals (the game normalises them
 * with an approximate square root: an inexact normal tilts the frame of 8C029B00 by a few angle steps, and a character
 * stands up to 0.03 units off a flat floor, as on most of the game's own polygons). Every cell is clipped out of the
 * original polygon (slab planes, then column planes:
 * neighbouring pieces compute their shared cut points from the same segments) and fan-triangulated, the winding kept.
 *   Longest-edge halving (before) turned a long narrow face (GMod planks, rails, kerbs, road strips: 1328 x 59 SA1 units
 * on gm_freespace_13) into hundreds of side-by-side needles, each with a bounding sphere of about 2/3 of max_edge; the
 * NEAR query 8C028BFE takes every polygon whose centre lies within sqrt(1.3 (r^2 + R^2)) of the probe, so a few stacked
 * layers of needles filled its 96-entry list and the floor under the character was never tested. The grid gives one
 * piece pair per slab for such a face (32 instead of about 500, none narrower than the face), and for wide faces about
 * as many pieces as halving did, with bounding spheres of at most about max_edge / 2.
 */
static void ca_grid_poly_cut(ca_gb *b, const double *p, int n, const int32_t key[3], uint32_t col, uint32_t mat,
                             uint32_t flags);

static void ca_grid_poly(ca_gb *b, const double *p, int n, const int32_t key[3], uint32_t col, uint32_t mat,
                         uint32_t flags)
{
    if (b->count_only && n >= 3) {
        /* GRID-CAP: the face with the longest edge and the pieces it makes, for the message */
        uint64_t before = b->cnt_land;
        double l2 = 0.0;
        int k, j;
        for (k = 0; k < n; k++) {
            double d = ca_dist2(p + 3 * k, p + 3 * ((k + 1) % n));
            l2 = d > l2 ? d : l2;
        }
        ca_grid_poly_cut(b, p, n, key, col, mat, flags);
        if (l2 > b->big_len2) {
            b->big_len2 = l2;
            b->big_pieces = b->cnt_land - before;
            for (j = 0; j < 3; j++) {
                double s = 0.0;
                for (k = 0; k < n; k++) {
                    s += p[3 * k + j];
                }
                b->big_at[j] = (float)(s / (double)n);
            }
        }
        return;
    }
    ca_grid_poly_cut(b, p, n, key, col, mat, flags);
}

static void ca_grid_poly_cut(ca_gb *b, const double *p, int n, const int32_t key[3], uint32_t col, uint32_t mat,
                             uint32_t flags)
{
    double d1[3], d2[3], nrm[3], len2 = -1.0, maxd = 0.0, len, mn1, mx1, mn2, mx2, e, c, w2, l1max, ws, ls;
    double slab[3 * CA_POLY_MAX], tmp[3 * CA_POLY_MAX], cell[3 * CA_POLY_MAX];
    const double *a0 = p, *b0 = p;
    uint32_t n1, n2, i, j;
    int k, q;
    if (b->st != SA1_OK || n < 3) {
        return;
    }
    for (k = 0; k < n; k++) {
        for (q = k + 1; q < n; q++) {
            double d = ca_dist2(p + 3 * k, p + 3 * q);
            maxd = d > maxd ? d : maxd;
        }
    }
    if (!(maxd > b->edge2)) {
        if (n == 3) {
            if (ca_degenerate(p)) {
                b->dropped++;
                return;
            }
            ca_piece(b, p, key, col, mat, flags);
        } else {
            ca_fan(b, p, n, key, col, mat, flags & ~CA_GT_ODD);
        }
        return;
    }
    flags &= ~CA_GT_ODD;
    for (k = 0; k < n; k++) {
        double d = ca_dist2(p + 3 * k, p + 3 * ((k + 1) % n));
        if (d > len2) {
            len2 = d;
            a0 = p + 3 * k;
            b0 = p + 3 * ((k + 1) % n);
        }
    }
    if (b0[0] < a0[0] || (b0[0] == a0[0] && (b0[1] < a0[1] || (b0[1] == a0[1] && b0[2] < a0[2])))) {
        const double *s = a0;
        a0 = b0;
        b0 = s;
    }
    len = sqrt(len2);
    if (!(len > 0.0) || !ca_poly_normal(p, n, nrm)) {
        b->dropped++;
        return;
    }
    for (k = 0; k < 3; k++) {
        d1[k] = (b0[k] - a0[k]) / len;
    }
    d2[0] = nrm[1] * d1[2] - nrm[2] * d1[1];
    d2[1] = nrm[2] * d1[0] - nrm[0] * d1[2];
    d2[2] = nrm[0] * d1[1] - nrm[1] * d1[0];
    mn1 = mx1 = ca_dot(d1, p);
    mn2 = mx2 = ca_dot(d2, p);
    for (k = 1; k < n; k++) {
        double u = ca_dot(d1, p + 3 * k), w = ca_dot(d2, p + 3 * k);
        mn1 = u < mn1 ? u : mn1;
        mx1 = u > mx1 ? u : mx1;
        mn2 = w < mn2 ? w : mn2;
        mx2 = w > mx2 ? w : mx2;
    }
    /* sizes (0.999: rounding never lets a piece edge exceed max_edge) */
    e = sqrt(b->edge2) * 0.999;
    c = e * 0.70710678118654752;
    ws = mx2 - mn2;
    ls = mx1 - mn1;
    n2 = ca_cut_count(ws, c);
    w2 = ws / (double)n2;
    l1max = sqrt(e * e - w2 * w2);
    n1 = ca_cut_count(ls, l1max);
    if ((double)n1 * (double)n2 > 2.0 * (double)CA_FRAG_MAX) {
        b->st = sa1_fail(b->ctx, SA1_ERR_LIMIT, "collision adapter: GRID: a face %.0f x %.0f units needs more than %u "
                         "pieces", ls, ws, (unsigned)CA_FRAG_MAX);
        return;
    }
    for (i = 0; i < n1 && b->st == SA1_OK; i++) {
        double lo2, hi2;
        uint32_t j0, j1;
        int m = n;
        memcpy(slab, p, (size_t)n * 3u * sizeof(double));
        if (i > 0) {
            m = ca_clip_dir(slab, m, tmp, d1, mn1 + ls * (double)i / (double)n1, 1);
            memcpy(slab, tmp, (size_t)m * 3u * sizeof(double));
        }
        if (m >= 3 && i + 1 < n1) {
            m = ca_clip_dir(slab, m, tmp, d1, mn1 + ls * (double)(i + 1) / (double)n1, 0);
            memcpy(slab, tmp, (size_t)m * 3u * sizeof(double));
        }
        if (m < 3) {
            continue;
        }
        if (n2 == 1) {
            ca_fan(b, slab, m, key, col, mat, flags);
            continue;
        }
        lo2 = hi2 = ca_dot(d2, slab);
        for (k = 1; k < m; k++) {
            double w = ca_dot(d2, slab + 3 * k);
            lo2 = w < lo2 ? w : lo2;
            hi2 = w > hi2 ? w : hi2;
        }
        j0 = lo2 > mn2 ? (uint32_t)floor((lo2 - mn2) / w2) : 0u;
        j1 = hi2 > mn2 ? (uint32_t)floor((hi2 - mn2) / w2) : 0u;
        j0 = j0 > 0u ? j0 - 1u : 0u;
        j1 = j1 + 1u < n2 ? j1 + 1u : n2 - 1u;
        for (j = j0; j <= j1 && b->st == SA1_OK; j++) {
            int mm = m;
            memcpy(cell, slab, (size_t)m * 3u * sizeof(double));
            if (j > 0) {
                mm = ca_clip_dir(cell, mm, tmp, d2, mn2 + ws * (double)j / (double)n2, 1);
                memcpy(cell, tmp, (size_t)mm * 3u * sizeof(double));
            }
            if (mm >= 3 && j + 1 < n2) {
                mm = ca_clip_dir(cell, mm, tmp, d2, mn2 + ws * (double)(j + 1) / (double)n2, 0);
                memcpy(cell, tmp, (size_t)mm * 3u * sizeof(double));
            }
            ca_fan(b, cell, mm, key, col, mat, flags);
        }
    }
}

/* ---- merging the triangles of one face (ca_grid_input) ---- */
#define CA_MERGE_MAX        8           /* vertices of a merged face */
#define CA_MERGE_PLANE      2e-3        /* largest distance of a merged vertex from the face's plane (SA1 units) */

/* triangle t, which follows a face p[*n] (convex, winding order, unit normal nrm) in the input, shares one of its edges
 * (p[k], p[k + 1]) as (t[j + 1], t[j]) bit for bit: t's third vertex goes in after p[k] when the face stays planar and
 * convex. 1 = merged. */
static int ca_poly_merge(double *p, int *n, const double t[9], const double nrm[3])
{
    double tn[3], q[3 * CA_MERGE_MAX];
    int k, j, i, m = *n;
    if (m >= CA_MERGE_MAX || !ca_poly_normal(t, 3, tn) || ca_dot(tn, nrm) < 0.99999) {
        return 0;
    }
    for (k = 0; k < m; k++) {
        const double *pa = p + 3 * k, *pb = p + 3 * ((k + 1) % m);
        for (j = 0; j < 3; j++) {
            const double *ta = t + 3 * j, *tb = t + 3 * ((j + 1) % 3), *tc = t + 3 * ((j + 2) % 3);
            double d[3];
            if (memcmp(pa, tb, 3 * sizeof(double)) != 0 || memcmp(pb, ta, 3 * sizeof(double)) != 0) {
                continue;
            }
            for (i = 0; i < 3; i++) {
                d[i] = tc[i] - p[i];
            }
            if (!(fabs(ca_dot(nrm, d)) <= CA_MERGE_PLANE)) {
                return 0;
            }
            memcpy(q, p, (size_t)(k + 1) * 3u * sizeof(double));
            memcpy(q + 3 * (k + 1), tc, 3 * sizeof(double));
            memcpy(q + 3 * (k + 2), p + 3 * (k + 1), (size_t)(m - k - 1) * 3u * sizeof(double));
            /* convex: every corner turns the same way as the normal (straight corners allowed) */
            for (i = 0; i <= m; i++) {
                const double *u = q + 3 * ((i + m) % (m + 1)), *v = q + 3 * i, *w = q + 3 * ((i + 1) % (m + 1));
                double e1[3], e2[3], cr[3], l1, l2;
                int a;
                for (a = 0; a < 3; a++) {
                    e1[a] = v[a] - u[a];
                    e2[a] = w[a] - v[a];
                }
                l1 = sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
                l2 = sqrt(e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2]);
                cr[0] = e1[1] * e2[2] - e1[2] * e2[1];
                cr[1] = e1[2] * e2[0] - e1[0] * e2[2];
                cr[2] = e1[0] * e2[1] - e1[1] * e2[0];
                if (!(l1 > 0.0) || !(l2 > 0.0) || ca_dot(nrm, cr) < -1e-9 * l1 * l2) {
                    return 0;
                }
            }
            memcpy(p, q, (size_t)(m + 1) * 3u * sizeof(double));
            *n = m + 1;
            return 1;
        }
    }
    return 0;
}

/* the point of segment p-q on the plane x[axis] = bound, the same for (p, q) and (q, p) */
static void ca_cut_point(const double *p, const double *q, int axis, double bound, double *out)
{
    const double *a = p, *b = q;
    double t;
    int k;
    if (q[0] < p[0] || (q[0] == p[0] && (q[1] < p[1] || (q[1] == p[1] && q[2] < p[2])))) {
        a = q;
        b = p;
    }
    t = (bound - a[axis]) / (b[axis] - a[axis]);
    for (k = 0; k < 3; k++) {
        out[k] = a[k] + t * (b[k] - a[k]);
    }
    out[axis] = bound;
}

/* keep the part of polygon in[n] with x[axis] >= bound (ge) or <= bound; returns the vertex count */
static int ca_clip_plane(const double *in, int n, double *out, int axis, double bound, int ge)
{
    int i, m = 0;
    for (i = 0; i < n && m < CA_POLY_MAX - 1; i++) {
        const double *a = in + 3 * i, *c = in + 3 * ((i + 1) % n);
        int ina = ge ? a[axis] >= bound : a[axis] <= bound;
        int inc = ge ? c[axis] >= bound : c[axis] <= bound;
        if (ina) {
            memcpy(out + 3 * m, a, 3 * sizeof(double));
            m++;
        }
        if (ina != inc) {
            ca_cut_point(a, c, axis, bound, out + 3 * m);
            m++;
        }
    }
    return m;
}

static void ca_grid_place(ca_gb *b, const double *v, int nv, uint32_t col, uint32_t mat, uint32_t flags, int depth)
{
    double lo[3], hi[3], cell = b->cell;
    int32_t c0[3], c1[3], key[3];
    uint64_t ncell;
    int k, i;
    if (!(b->gp->flags & SA1_GRID_CUT)) {
        ca_grid_poly(b, v, nv, NULL, col, mat, flags);     /* each piece to the cell of its centroid */
        return;
    }
    for (k = 0; k < 3; k++) {
        lo[k] = hi[k] = v[k];
        for (i = 1; i < nv; i++) {
            lo[k] = v[3 * i + k] < lo[k] ? v[3 * i + k] : lo[k];
            hi[k] = v[3 * i + k] > hi[k] ? v[3 * i + k] : hi[k];
        }
        c0[k] = ca_cell_of(lo[k], cell);
        c1[k] = ca_cell_of(hi[k], cell);
        if (c1[k] > c0[k] && hi[k] == (double)c1[k] * cell) {
            c1[k]--;        /* a box that ends exactly on a boundary does not enter the next cell */
        }
    }
    if (c0[0] == c1[0] && c0[1] == c1[1] && c0[2] == c1[2]) {
        ca_grid_poly(b, v, nv, c0, col, mat, flags);
        return;
    }
    ncell = (uint64_t)(c1[0] - c0[0] + 1) * (uint64_t)(c1[1] - c0[1] + 1) * (uint64_t)(c1[2] - c0[2] + 1);
    if (ncell > CA_CLIP_MAX_CELLS && depth < 40) {
        /* a large face: halve it first so that the cells of each half's box are mostly hit (a merged face: its fan) */
        double l[3], a[9], c[9];
        int e, i0, i1, i2;
        if (nv > 3) {
            for (i = 1; i + 1 < nv; i++) {
                memcpy(a, v, 3 * sizeof(double));
                memcpy(a + 3, v + 3 * i, 3 * sizeof(double));
                memcpy(a + 6, v + 3 * (i + 1), 3 * sizeof(double));
                ca_grid_place(b, a, 3, col, mat, flags & ~CA_GT_ODD, depth + 1);
            }
            return;
        }
        for (e = 0; e < 3; e++) {
            l[e] = ca_dist2(v + 3 * e, v + 3 * ((e + 1) % 3));
        }
        e = l[0] >= l[1] ? (l[0] >= l[2] ? 0 : 2) : (l[1] >= l[2] ? 1 : 2);
        i0 = e;
        i1 = (e + 1) % 3;
        i2 = (e + 2) % 3;
        for (k = 0; k < 3; k++) {
            double m = 0.5 * (v[3 * i0 + k] + v[3 * i1 + k]);
            a[k] = v[3 * i0 + k];
            a[3 + k] = m;
            a[6 + k] = v[3 * i2 + k];
            c[k] = m;
            c[3 + k] = v[3 * i1 + k];
            c[6 + k] = v[3 * i2 + k];
        }
        ca_grid_place(b, a, 3, col, mat, flags & ~CA_GT_ODD, depth + 1);
        ca_grid_place(b, c, 3, col, mat, flags & ~CA_GT_ODD, depth + 1);
        return;
    }
    b->cut++;
    for (key[0] = c0[0]; key[0] <= c1[0]; key[0]++) {
        for (key[1] = c0[1]; key[1] <= c1[1]; key[1]++) {
            for (key[2] = c0[2]; key[2] <= c1[2]; key[2]++) {
                double pa[3 * CA_POLY_MAX], pb[3 * CA_POLY_MAX];
                int n = nv, ax;
                memcpy(pa, v, (size_t)nv * 3u * sizeof(double));
                for (ax = 0; ax < 3 && n >= 3; ax++) {
                    n = ca_clip_plane(pa, n, pb, ax, (double)key[ax] * cell, 1);
                    if (n >= 3) {
                        n = ca_clip_plane(pb, n, pa, ax, (double)(key[ax] + 1) * cell, 0);
                    }
                }
                if (n >= 3) {
                    ca_grid_poly(b, pa, n, key, col, mat, flags & ~CA_GT_ODD);
                }
            }
        }
    }
}

/* one input triangle (world space) of surface class: land (col), kill, or both */
static void ca_grid_tri(ca_gb *b, const double v[9], uint32_t col, uint32_t mat, uint32_t flags, int kill,
                        int top_only)
{
    double n[3], nn, l2;
    int k, up;
    for (k = 0; k < 9; k++) {
        if (!isfinite(v[k]) || fabs(v[k]) > 1e9) {
            b->dropped++;
            return;
        }
    }
    ca_tri_metrics(v, n, &nn, &l2);
    if (!(l2 > 0.0) || !(nn > 2e-6 * l2)) {
        b->dropped++;
        return;
    }
    up = n[1] > 1e-3 * nn;
    if ((col & SA1CA_COL_COLLIDABLE) != 0) {
        if (top_only && (col & 0x400002u) != 0 && !(col & 0x1u) && !up) {
            b->dropped++;
        } else {
            ca_grid_place(b, v, 3, col, mat, flags, 0);
        }
    }
    if (kill) {
        if (up) {
            /* kill planes are probed whole by the death zone task every step (8C02CA00 walks every polygon of
             * every zone object): no cutting, coarse cells (8 x 8 cells in x / z, one cell in y, so that stacked
             * kill planes stay in different objects: a zone object kills only with nothing of it below) */
            int32_t key[3];
            double kc = b->cell * CA_KILL_CELLS;
            key[0] = ca_cell_of((v[0] + v[3] + v[6]) / 3.0, kc);
            key[1] = ca_cell_of((v[1] + v[4] + v[7]) / 3.0, b->cell);
            key[2] = ca_cell_of((v[2] + v[5] + v[8]) / 3.0, kc);
            ca_frag_push(b, v, key, 0u, 0u, CA_GT_KILL);
        } else {
            b->dropped++;
        }
    }
    if (l2 > b->edge2) {
        b->split++;
    }
}

/* the bits of x (10) spread to every third bit */
static uint32_t ca_spread10(uint32_t x)
{
    x &= 0x3FFu;
    x = (x | (x << 16)) & 0x030000FFu;
    x = (x | (x << 8)) & 0x0300F00Fu;
    x = (x | (x << 4)) & 0x030C30C3u;
    x = (x | (x << 2)) & 0x09249249u;
    return x;
}

/* Z-order key (30 bits) of the centroid of f inside its cell */
static uint32_t ca_morton(const ca_frag *f, double cell)
{
    uint32_t m = 0;
    int k;
    for (k = 0; k < 3; k++) {
        double c = ((double)f->v[k] + (double)f->v[3 + k] + (double)f->v[6 + k]) / 3.0;
        double u = (c / cell - (double)f->key[k]) * 1024.0;
        uint32_t q = u <= 0.0 ? 0u : (u >= 1023.0 ? 1023u : (uint32_t)u);
        m |= ca_spread10(q) << k;
    }
    return m;
}

static int ca_frag_cmp_sub(const void *pa, const void *pb)
{
    const ca_frag *a = (const ca_frag *)pa, *c = (const ca_frag *)pb;
    if (a->sub != c->sub) {
        return a->sub < c->sub ? -1 : 1;
    }
    return a->seq < c->seq ? -1 : (a->seq > c->seq ? 1 : 0);
}

/* descending centroid z, then input order: the game's polygon list of an object comes out sorted by z (see d) */
static int ca_frag_cmp_zdesc(const void *pa, const void *pb)
{
    const ca_frag *a = (const ca_frag *)pa, *c = (const ca_frag *)pb;
    double za = (double)a->v[2] + (double)a->v[5] + (double)a->v[8];
    double zc = (double)c->v[2] + (double)c->v[5] + (double)c->v[8];
    if (za != zc) {
        return za > zc ? -1 : 1;
    }
    return a->seq < c->seq ? -1 : (a->seq > c->seq ? 1 : 0);
}

static int ca_frag_cmp(const void *pa, const void *pb)
{
    const ca_frag *a = (const ca_frag *)pa, *c = (const ca_frag *)pb;
    uint32_t ka = a->flags & CA_GT_KILL, kc = c->flags & CA_GT_KILL;
    if (ka != kc) {
        return ka < kc ? -1 : 1;
    }
    if (a->key[0] != c->key[0]) {
        return a->key[0] < c->key[0] ? -1 : 1;
    }
    if (a->key[2] != c->key[2]) {
        return a->key[2] < c->key[2] ? -1 : 1;
    }
    if (a->key[1] != c->key[1]) {
        return a->key[1] < c->key[1] ? -1 : 1;
    }
    if (a->col != c->col) {
        return a->col < c->col ? -1 : 1;
    }
    if (a->ori != c->ori) {
        return a->ori < c->ori ? -1 : 1;    /* F2: floor, wall, ceiling objects of one cell and word */
    }
    if ((a->flags & CA_GT_BOARD) != (c->flags & CA_GT_BOARD)) {
        /* round 0.9.5 (review R.1): board snow / sand never share an object with other land */
        return (a->flags & CA_GT_BOARD) < (c->flags & CA_GT_BOARD) ? -1 : 1;
    }
    return a->seq < c->seq ? -1 : (a->seq > c->seq ? 1 : 0);
}

static int ca_same_object(const ca_frag *a, const ca_frag *c)
{
    return (a->flags & CA_GT_KILL) == (c->flags & CA_GT_KILL) && a->key[0] == c->key[0] && a->key[1] == c->key[1]
           && a->key[2] == c->key[2] && a->col == c->col && a->ori == c->ori
           && (a->flags & CA_GT_BOARD) == (c->flags & CA_GT_BOARD);
}

/* the largest float ulp of |x| over a range */
static double ca_ulp(double ax)
{
    int e = 0;
    if (!(ax > 0.0)) {
        return ldexp(1.0, -126);
    }
    (void)frexp(ax, &e);       /* ax = m * 2^e, 0.5 <= m < 1: the float ulp is 2^(e - 24) */
    return ldexp(1.0, e - 24 < -149 ? -149 : e - 24);
}

static uint32_t ca_fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/* points, materials, meshset runs and pos / r of object o (fragments o->first .. o->first + o->count) */
static sa1_status ca_grid_layout_object(ca_gb *b, ca_gobj *o, uint32_t *hash)
{
    double lo[3], hi[3], c[3], u[3], best = 0.0;
    uint32_t i, k, j, run = 0, key = 0, hsize = 16u;
    int have = 0;
    while (hsize < 6u * o->count) {
        hsize *= 2u;
    }
    for (i = 0; i < hsize; i++) {
        hash[i] = 0;
    }
    o->pfirst = (uint32_t)b->npts;
    o->npts = 0;
    o->mfirst = (uint32_t)b->nmats;
    o->nmat = 0;
    o->nms = 0;
    o->nshorts = 0;
    for (i = 0; i < o->count; i++) {
        ca_frag *f = &b->fr[o->first + i];
        uint32_t odd = (f->flags & CA_GT_ODD) != 0, kk, mi;
        for (k = 0; k < 3; k++) {
            uint32_t h = ca_fbits(f->v[3 * k]) * 0x9E3779B1u ^ ca_fbits(f->v[3 * k + 1]) * 0x85EBCA77u
                         ^ ca_fbits(f->v[3 * k + 2]) * 0xC2B2AE3Du;
            h ^= h >> 15;
            for (j = h & (hsize - 1u);; j = (j + 1u) & (hsize - 1u)) {
                if (hash[j] == 0) {
                    if (o->npts >= CA_OBJ_MAX_POINTS) {
                        return SA1_ERR_LIMIT;
                    }
                    if (!ca_grow(b->ctx, (void **)&b->pts, &b->cappts, b->npts + 1u, 3u * sizeof(float), b->npts)) {
                        return SA1_ERR_OUT_OF_MEMORY;
                    }
                    memcpy(b->pts + 3u * b->npts, f->v + 3 * k, 3 * sizeof(float));
                    b->npts++;
                    hash[j] = ++o->npts;
                    break;
                }
                if (memcmp(b->pts + 3u * (o->pfirst + hash[j] - 1u), f->v + 3 * k, 3 * sizeof(float)) == 0) {
                    break;
                }
            }
            f->idx[k] = (uint16_t)(hash[j] - 1u);
        }
        for (mi = 0; mi < o->nmat && b->mats[o->mfirst + mi] != f->mat; mi++) {
        }
        if (mi == o->nmat) {
            if (o->nmat > SA1CA_MATID_MAX) {
                return SA1_ERR_LIMIT;
            }
            if (!ca_grow(b->ctx, (void **)&b->mats, &b->capmats, b->nmats + 1u, sizeof(uint32_t), b->nmats)) {
                return SA1_ERR_OUT_OF_MEMORY;
            }
            b->mats[b->nmats++] = f->mat;
            o->nmat++;
        }
        kk = (odd ? SA1CA_MESH_STRIP : 0u) | mi;
        if (!have || kk != key || run == 0xFFFFu) {
            o->nms++;
            run = 0;
            key = kk;
            have = 1;
        }
        run++;
        o->nshorts += odd ? 4u : 3u;
    }
    /* pos: the box centre on a grid of the largest ulp, so that pos + (p - pos) == p exactly */
    for (k = 0; k < 3; k++) {
        lo[k] = hi[k] = b->pts[3u * o->pfirst + k];
    }
    for (i = 0; i < o->npts; i++) {
        for (k = 0; k < 3; k++) {
            double x = b->pts[3u * (o->pfirst + i) + k];
            lo[k] = x < lo[k] ? x : lo[k];
            hi[k] = x > hi[k] ? x : hi[k];
        }
    }
    for (k = 0; k < 3; k++) {
        double m = fabs(lo[k]) > fabs(hi[k]) ? fabs(lo[k]) : fabs(hi[k]);
        o->lo[k] = (float)lo[k] - 1e-3f * (1.0f + (float)m * 1e-4f);
        o->hi[k] = (float)hi[k] + 1e-3f * (1.0f + (float)m * 1e-4f);
        u[k] = ca_ulp(m);
        c[k] = floor(0.5 * (lo[k] + hi[k]) / u[k] + 0.5) * u[k];
        o->pos[k] = (float)c[k];
    }
    for (i = 0; i < o->npts; i++) {
        double d = 0.0;
        for (k = 0; k < 3; k++) {
            float p = b->pts[3u * (o->pfirst + i) + k];
            double e = (double)p - (double)o->pos[k];
            float l = (float)e;
            if ((double)l != e || o->pos[k] + l != p) {
                b->inexact++;
            }
            d += e * e;
        }
        best = d > best ? d : best;
    }
    o->r = ca_radius_up(sqrt(best));
    return SA1_OK;
}

static uint32_t ca_gobj_bytes(const ca_gobj *o)
{
    return SA1CA_OBJECT_SIZE + SA1CA_MODEL_SIZE + SA1CA_MATERIAL_SIZE * o->nmat + SA1CA_MESHSET_SIZE * o->nms
           + 12u * o->npts + ca_align4(2u * o->nshorts);
}

/* NJS_OBJECT + NJS_MODEL + materials + meshsets + points + meshes of o at host + off (guest base + off) */
static void ca_grid_write_object(const ca_gb *b, const ca_gobj *o, uint8_t *host, uint32_t base, uint32_t off)
{
    uint32_t o_model = off + SA1CA_OBJECT_SIZE, o_mats = o_model + SA1CA_MODEL_SIZE;
    uint32_t o_ms = o_mats + SA1CA_MATERIAL_SIZE * o->nmat, o_pts = o_ms + SA1CA_MESHSET_SIZE * o->nms;
    uint32_t o_mesh = o_pts + 12u * o->npts, i, k, nms = 0, nsh = 0, run = 0, key = 0;
    uint8_t *obj = host + off, *model = host + o_model, *cur = NULL;
    int have = 0;
    ca_wr32(obj + 0x00, 6u);                    /* evalflags: no rotation, no scale */
    ca_wr32(obj + 0x04, base + o_model);
    for (k = 0; k < 3; k++) {
        ca_wrf(obj + 0x08 + 4 * k, o->pos[k]);
        ca_wrf(obj + 0x20 + 4 * k, 1.0f);
    }
    ca_wr32(model + 0x00, base + o_pts);
    ca_wr32(model + 0x08, o->npts);
    ca_wr32(model + 0x0C, base + o_ms);
    ca_wr32(model + 0x10, base + o_mats);
    ca_wr16(model + 0x14, o->nms);
    ca_wr16(model + 0x16, o->nmat);
    ca_wrf(model + 0x24, o->r);
    for (i = 0; i < o->nmat; i++) {
        ca_wr32(host + o_mats + SA1CA_MATERIAL_SIZE * i + 0x10, b->mats[o->mfirst + i]);
    }
    for (i = 0; i < o->npts; i++) {
        for (k = 0; k < 3; k++) {
            float p = b->pts[3u * (o->pfirst + i) + k];
            ca_wrf(host + o_pts + 12u * i + 4u * k, (float)((double)p - (double)o->pos[k]));
        }
    }
    for (i = 0; i < o->count; i++) {
        const ca_frag *f = &b->fr[o->first + i];
        uint32_t odd = (f->flags & CA_GT_ODD) != 0, kk, mi;
        uint8_t *m = host + o_mesh + 2u * nsh;
        for (mi = 0; mi < o->nmat && b->mats[o->mfirst + mi] != f->mat; mi++) {
        }
        kk = (odd ? SA1CA_MESH_STRIP : 0u) | mi;
        if (!have || kk != key || run == 0xFFFFu) {
            cur = host + o_ms + (size_t)SA1CA_MESHSET_SIZE * nms;
            ca_wr16(cur, kk);
            ca_wr32(cur + 4, base + o_mesh + 2u * nsh);
            nms++;
            run = 0;
            key = kk;
            have = 1;
        }
        run++;
        ca_wr16(cur + 2, run);
        if (odd) {
            ca_wr16(m, SA1CA_STRIP_FLIP | 3u);
            ca_wr16(m + 2, f->idx[0]);
            ca_wr16(m + 4, f->idx[2]);
            ca_wr16(m + 6, f->idx[1]);
            nsh += 4u;
        } else {
            ca_wr16(m, f->idx[0]);
            ca_wr16(m + 2, f->idx[1]);
            ca_wr16(m + 4, f->idx[2]);
            nsh += 3u;
        }
    }
}

/* the stored description's triangles: set_static's flat list (g NULL) or one group's, in world space */
typedef struct ca_src {
    const sa1ca_static *s;
    const sa1ca_group *g;
    int ident;                  /* the group's transform is the identity */
    uint32_t gmat;              /* material bits of the group's surface */
} ca_src;

/* triangle i of src in world space; *same: what must be equal for two triangles to join one face (the surface word of a
 * flat triangle, the material word of a group's) */
static void ca_src_tri(const ca_src *src, uint32_t i, double v[9], uint32_t *same)
{
    uint32_t k;
    if (src->g == NULL) {
        const sa1_collision_tri *ft = &src->s->flat[i];
        for (k = 0; k < 9; k++) {
            v[k] = (double)ft->v[k / 3][k % 3];
        }
        *same = ft->surface;
        return;
    }
    {
        const sa1ca_tri *tr = &src->s->tris[src->g->first_tri + i];
        for (k = 0; k < 3; k++) {
            const float *p = src->s->points + 3u * ((size_t)src->g->first_point + tr->v[k]);
            if (src->ident) {
                v[3 * k] = p[0];
                v[3 * k + 1] = p[1];
                v[3 * k + 2] = p[2];
            } else {
                ca_transform_point(src->g, p, v + 3 * k);
            }
        }
        *same = tr->material | src->gmat;
    }
}

/* finite, inside the coordinate range and not degenerate: may start or join a merged face */
static int ca_tri_ok(const double v[9])
{
    int k;
    for (k = 0; k < 9; k++) {
        if (!isfinite(v[k]) || fabs(v[k]) > 1e9) {
            return 0;
        }
    }
    return !ca_degenerate(v);
}

static int ca_tri_long(const ca_gb *b, const double v[9])
{
    double n[3], nn, l2;
    ca_tri_metrics(v, n, &nn, &l2);
    return l2 > b->edge2;
}

/* The triangles first, first + 1 .. of src that form one convex planar face (consecutive in the input, the same
 * surface / material, each sharing an edge with the face so far: a quad or a fan as a host or the BSP conversion
 * triangulates its faces) when one of them is longer than max_edge: the face is cut as one polygon (a rectangle into
 * rectangular cells instead of two triangles cut along the diagonal: half the pieces, no slivers along it). Land faces
 * only (kill triangles stay whole). Returns the number of triangles taken (0: none, place triangle first alone). */
static uint32_t ca_grid_merge(ca_gb *b, const ca_src *src, uint32_t first, uint32_t end, uint32_t col, uint32_t mat,
                              uint32_t flags, int top_only)
{
    double p[3 * CA_MERGE_MAX], v[9], nrm[3];
    uint32_t i, same0, same, longs;
    int n = 3;
    ca_src_tri(src, first, p, &same0);
    if (first + 1 >= end || !ca_tri_ok(p) || !ca_poly_normal(p, 3, nrm)) {
        return 0;
    }
    longs = (uint32_t)ca_tri_long(b, p);
    for (i = first + 1; i < end; i++) {
        ca_src_tri(src, i, v, &same);
        if (same != same0 || !ca_tri_ok(v) || !ca_poly_merge(p, &n, v, nrm)) {
            break;
        }
        longs += (uint32_t)ca_tri_long(b, v);
    }
    if (i == first + 1 || longs == 0) {
        return 0;
    }
    if (top_only && (col & 0x400002u) != 0 && !(col & 0x1u) && !(nrm[1] > 1e-3)) {
        b->dropped += i - first;    /* a water surface keeps its upward faces only (ca_grid_tri) */
        return i - first;
    }
    ca_grid_place(b, p, n, col, mat, flags & (CA_GT_CURVED | CA_GT_BOARD), 0);
    b->split += longs;
    b->merged += i - first;
    return i - first;
}

/* GRID-CAP counting pass: a merged face (ca_grid_merge) spans at most CA_MERGE_MAX - 2 triangles and is made only when
 * one of them is longer than max_edge, so a run starting at i without such a triangle among the next CA_MERGE_MAX - 2
 * merges nothing: the counting pass skips the attempt (the pieces stay exactly the same). *nl: no long triangle in
 * [i, *nl) of this source. */
static int ca_src_long(const ca_gb *b, const ca_src *src, uint32_t i)
{
    double v[9];
    uint32_t same;
    ca_src_tri(src, i, v, &same);
    return ca_dist2(v, v + 3) > b->edge2 || ca_dist2(v + 3, v + 6) > b->edge2 || ca_dist2(v + 6, v) > b->edge2;
}

static int ca_merge_possible(const ca_gb *b, const ca_src *src, uint32_t i, uint32_t end, uint32_t *nl)
{
    uint32_t lim = end - i > CA_MERGE_MAX - 2u ? i + (CA_MERGE_MAX - 2u) : end;
    if (!b->count_only) {
        return 1;
    }
    if (*nl < i) {
        *nl = i;
    }
    while (*nl < lim && !ca_src_long(b, src, *nl)) {
        (*nl)++;
    }
    return *nl < lim;
}

/* feed the stored description into the partition */
static void ca_grid_input(ca_gb *b, const sa1ca_static *s)
{
    ca_src src;
    double v[9];
    uint32_t i, t, same, nl = 0, gt;
    memset(&src, 0, sizeof src);
    src.s = s;
    if (s->num_flat != 0) {
        for (i = 0; i < s->num_flat && b->st == SA1_OK;) {
            const sa1_collision_tri *ft = &s->flat[i];
            uint32_t col = sa1ca_surface_col(ft->surface), mat = sa1ca_surface_material(ft->surface), used = 0;
            int kill = (ft->surface & SA1_SURFACE_KILL) != 0;
            gt = (ft->surface & SA1_SURFACE_CURVED) ? CA_GT_CURVED : 0u;    /* a merged face: one surface word */
            gt |= sa1ca_surface_board(ft->surface) << 4;                    /* round 0.9.5: the board kinds */
            if (!kill && (col & SA1CA_COL_COLLIDABLE) != 0 && ca_merge_possible(b, &src, i, s->num_flat, &nl)) {
                used = ca_grid_merge(b, &src, i, s->num_flat, col, mat, gt, 1);
            }
            if (used != 0) {
                i += used;
                continue;
            }
            ca_src_tri(&src, i, v, &same);
            ca_grid_tri(b, v, col, mat, gt, kill, 1);
            i++;
        }
        return;
    }
    for (i = 0; i < s->num_groups && b->st == SA1_OK; i++) {
        const sa1ca_group *g = &s->groups[i];
        int kill = !g->raw && (g->surface & SA1_SURFACE_KILL) != 0;
        src.g = g;
        src.ident = g->pos[0] == 0.0f && g->pos[1] == 0.0f && g->pos[2] == 0.0f && (g->ang[0] & 0xFFFFu) == 0
                    && (g->ang[1] & 0xFFFFu) == 0 && (g->ang[2] & 0xFFFFu) == 0 && g->scl[0] == 1.0f
                    && g->scl[1] == 1.0f && g->scl[2] == 1.0f;
        src.gmat = g->raw ? 0u : sa1ca_surface_material(g->surface);
        gt = !g->raw && (g->surface & SA1_SURFACE_CURVED) ? CA_GT_CURVED : 0u;
        gt |= !g->raw ? sa1ca_surface_board(g->surface) << 4 : 0u;     /* round 0.9.5: the board kinds */
        if ((g->col_flags & SA1CA_COL_COLLIDABLE) == 0 && !kill) {
            continue;
        }
        nl = 0;
        for (t = 0; t < g->num_tris && b->st == SA1_OK;) {
            const sa1ca_tri *tr = &s->tris[g->first_tri + t];
            uint32_t used = 0, mat = ca_mat_norm(tr->material | src.gmat, gt != 0);
            if (!kill && (g->col_flags & SA1CA_COL_COLLIDABLE) != 0 && ca_merge_possible(b, &src, t, g->num_tris, &nl)) {
                used = ca_grid_merge(b, &src, t, g->num_tris, g->col_flags, mat, gt, !g->raw);
            }
            if (used != 0) {
                t += used;
                continue;
            }
            ca_src_tri(&src, t, v, &same);
            ca_grid_tri(b, v, g->col_flags, mat, ((tr->flags & SA1_COLTRI_STRIP_ODD) ? CA_GT_ODD : 0u) | gt, kill,
                        !g->raw);
            t++;
        }
    }
}

/* ---- c2: undersides within reach ---- */
typedef struct ca_cover {
    double v[9];                /* corners (world space, winding order) */
    double n[3];                /* unit normal */
    size_t frag;                /* its piece (round 0.9.3: marked SA1CA_MAT_THIN_TOP when it covers a front-only underside) */
} ca_cover;

typedef struct ca_cell_ent {
    int32_t cx, cz;
    uint32_t idx;               /* into the covers */
} ca_cell_ent;

static int32_t ca_under_cell(double x)
{
    double q = floor(x / CA_UNDER_CELL);
    return q < -2147483647.0 ? -2147483647 : (q > 2147483647.0 ? 2147483647 : (int32_t)q);
}

static uint32_t ca_under_hash(int32_t cx, int32_t cz, uint32_t mask)
{
    return sa1ca_grid_hash(cx, cz) & mask;
}

/* the unit normal of a piece (0: degenerate) */
static int ca_frag_normal(const ca_frag *f, double n[3])
{
    double v[9];
    int k;
    for (k = 0; k < 9; k++) {
        v[k] = f->v[k];
    }
    return ca_poly_normal(v, 3, n);
}

/* a land piece that pushes a sphere out (solid; not water, not a kill plane) */
static int ca_under_land(const ca_frag *f)
{
    return !(f->flags & (CA_GT_KILL | CA_GT_DROP)) && (f->col & 0x1u) != 0;
}

/* is p at most reach behind cover c (along c's normal; on its plane counts) with its projection inside c? */
static int ca_under_covers(const ca_cover *c, const double p[3], double reach)
{
    double d = c->n[0] * (p[0] - c->v[0]) + c->n[1] * (p[1] - c->v[1]) + c->n[2] * (p[2] - c->v[2]), q[3];
    int e, k;
    if (!(d <= CA_UNDER_TOL) || !(d >= -reach)) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        q[k] = p[k] - d * c->n[k];
    }
    for (e = 0; e < 3; e++) {
        const double *a = c->v + 3 * e, *b = c->v + 3 * ((e + 1) % 3);
        double ab[3], aq[3], cr[3], l;
        for (k = 0; k < 3; k++) {
            ab[k] = b[k] - a[k];
            aq[k] = q[k] - a[k];
        }
        cr[0] = ab[1] * aq[2] - ab[2] * aq[1];
        cr[1] = ab[2] * aq[0] - ab[0] * aq[2];
        cr[2] = ab[0] * aq[1] - ab[1] * aq[0];
        l = sqrt(ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2]);
        if (c->n[0] * cr[0] + c->n[1] * cr[1] + c->n[2] * cr[2] < -CA_UNDER_TOL * l) {
            return 0;
        }
    }
    return 1;
}

/*
 * c2 (see the overview above): the downward-facing single-sided land pieces whose every sample point (the centroid, the
 * corners and the edge midpoints, each moved 2% towards the centroid) lies at most reach behind an upward-facing land
 * piece are marked CA_GT_DROP and removed. The covering pieces are indexed by the x / z cells (CA_UNDER_CELL) their
 * triangle reaches when moved by up to reach along its normal (an open-addressing table of cells, counted first, then
 * filled: no sorting). Runs on the synthesis's own data only (worker-safe: sa1_collision_prepare).
 * Round 0.9.3: of those, CURVED pieces are kept as they are (LOOP-L1) and, unless `drop` (SA1_GRID_UNDERSIDES_DROP), the
 * near-horizontal ones (normal y <= CA_UNDER_FLAT_NY, -0.97) get SA1_MAT_FRONT_ONLY and stay (FIX-C2); only the others go.
 */
static void ca_grid_undersides(ca_gb *b, double reach, int drop)
{
    ca_cover *cov = NULL;
    ca_cell_ent *ent = NULL;
    uint32_t *start = NULL, *fill = NULL, ncov = 0, nent = 0, mask, hsize = 1u, i, j, out;
    size_t f;
    if (b->st != SA1_OK || b->nfr == 0 || !(reach > 0.0)) {
        return;
    }
    /* the covering pieces */
    for (f = 0; f < b->nfr; f++) {
        double n[3];
        if (ca_under_land(&b->fr[f]) && ca_frag_normal(&b->fr[f], n) && n[1] >= CA_UNDER_COVER_NY) {
            ncov++;
        }
    }
    if (ncov == 0) {
        return;
    }
    cov = (ca_cover *)sa1_mem_array(b->ctx, ncov, sizeof *cov);
    if (cov == NULL) {
        b->st = SA1_ERR_OUT_OF_MEMORY;
        return;
    }
    ncov = 0;
    for (f = 0; f < b->nfr; f++) {
        ca_cover *c;
        double n[3];
        int k;
        /* the normal goes to a local first: once the last cover is stored, cov[ncov] is past the end */
        if (!ca_under_land(&b->fr[f]) || !ca_frag_normal(&b->fr[f], n) || n[1] < CA_UNDER_COVER_NY) {
            continue;
        }
        c = &cov[ncov];
        for (k = 0; k < 3; k++) {
            c->n[k] = n[k];
        }
        for (k = 0; k < 9; k++) {
            c->v[k] = b->fr[f].v[k];
        }
        c->frag = f;
        ncov++;
    }
    /* cells of each cover: its x / z box grown by reach x |n.xz| (where points behind it can lie) */
    for (i = 0; i < ncov; i++) {
        const ca_cover *c = &cov[i];
        double g = reach * sqrt(c->n[0] * c->n[0] + c->n[2] * c->n[2]) + CA_UNDER_TOL, lo[2], hi[2];
        lo[0] = hi[0] = c->v[0];
        lo[1] = hi[1] = c->v[2];
        for (j = 1; j < 3; j++) {
            lo[0] = c->v[3 * j] < lo[0] ? c->v[3 * j] : lo[0];
            hi[0] = c->v[3 * j] > hi[0] ? c->v[3 * j] : hi[0];
            lo[1] = c->v[3 * j + 2] < lo[1] ? c->v[3 * j + 2] : lo[1];
            hi[1] = c->v[3 * j + 2] > hi[1] ? c->v[3 * j + 2] : hi[1];
        }
        nent += (uint32_t)((ca_under_cell(hi[0] + g) - ca_under_cell(lo[0] - g) + 1)
                           * (ca_under_cell(hi[1] + g) - ca_under_cell(lo[1] - g) + 1));
        if (nent > (1u << 26)) {
            b->st = sa1_fail(b->ctx, SA1_ERR_LIMIT, "collision adapter: GRID: too many cells of upward faces");
            goto done;
        }
    }
    while (hsize < ncov) {
        hsize <<= 1;        /* slots shared by several cells are fine: an entry keeps its cell */
    }
    mask = hsize - 1u;
    ent = (ca_cell_ent *)sa1_mem_array(b->ctx, nent, sizeof *ent);
    start = (uint32_t *)sa1_mem_array(b->ctx, (size_t)hsize + 1u, sizeof *start);
    fill = (uint32_t *)sa1_mem_array(b->ctx, hsize, sizeof *fill);
    if (ent == NULL || start == NULL || fill == NULL) {
        b->st = SA1_ERR_OUT_OF_MEMORY;
        goto done;
    }
    /* two passes: count per hash slot, then place (an entry keeps its cell: slots are shared by several cells) */
    for (j = 0; j < 2; j++) {
        if (j == 1) {
            uint32_t acc = 0;
            for (i = 0; i < hsize; i++) {
                uint32_t c = start[i];
                start[i] = acc;
                fill[i] = acc;
                acc += c;
            }
            start[hsize] = acc;
            if (acc != nent) {
                /* the placement pass would write past ent[nent - 1]: the cell ranges must equal the first loop's */
                b->st = sa1_fail(b->ctx, SA1_ERR_LIMIT, "collision adapter: GRID: underside cells %u != %u counted",
                                 (unsigned)acc, (unsigned)nent);
                goto done;
            }
        }
        for (i = 0; i < ncov; i++) {
            const ca_cover *c = &cov[i];
            double g = reach * sqrt(c->n[0] * c->n[0] + c->n[2] * c->n[2]) + CA_UNDER_TOL, lo[2], hi[2];
            int32_t x, z, x0, x1, z0, z1;
            uint32_t k;
            lo[0] = hi[0] = c->v[0];
            lo[1] = hi[1] = c->v[2];
            for (k = 1; k < 3; k++) {
                lo[0] = c->v[3 * k] < lo[0] ? c->v[3 * k] : lo[0];
                hi[0] = c->v[3 * k] > hi[0] ? c->v[3 * k] : hi[0];
                lo[1] = c->v[3 * k + 2] < lo[1] ? c->v[3 * k + 2] : lo[1];
                hi[1] = c->v[3 * k + 2] > hi[1] ? c->v[3 * k + 2] : hi[1];
            }
            x0 = ca_under_cell(lo[0] - g);
            x1 = ca_under_cell(hi[0] + g);
            z0 = ca_under_cell(lo[1] - g);
            z1 = ca_under_cell(hi[1] + g);
            for (x = x0; x <= x1; x++) {
                for (z = z0; z <= z1; z++) {
                    uint32_t h = ca_under_hash(x, z, mask);
                    if (j == 0) {
                        start[h]++;
                    } else {
                        ca_cell_ent *e = &ent[fill[h]++];
                        e->cx = x;
                        e->cz = z;
                        e->idx = i;
                    }
                }
            }
        }
    }
    /* the downward-facing single-sided land pieces */
    for (f = 0; f < b->nfr; f++) {
        ca_frag *fr = &b->fr[f];
        double n[3], ce[3], smp[7][3];
        uint32_t covered_by[7] = { 0, 0, 0, 0, 0, 0, 0 };
        int k, si, ok = 1;
        if (!ca_under_land(fr) || (fr->mat & SA1_MAT_DOUBLE_SIDE) || !ca_frag_normal(fr, n) || !(n[1] < CA_UNDER_FACE_NY)) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            ce[k] = ((double)fr->v[k] + (double)fr->v[3 + k] + (double)fr->v[6 + k]) / 3.0;
        }
        for (si = 0; si < 3; si++) {
            for (k = 0; k < 3; k++) {
                double a = fr->v[3 * si + k], c = fr->v[3 * ((si + 1) % 3) + k];
                smp[si][k] = a + 0.02 * (ce[k] - a);
                smp[3 + si][k] = 0.5 * (a + c) + 0.02 * (ce[k] - 0.5 * (a + c));
            }
        }
        memcpy(smp[6], ce, sizeof ce);
        for (si = 0; si < 7 && ok; si++) {
            int32_t cx = ca_under_cell(smp[si][0]), cz = ca_under_cell(smp[si][2]);
            uint32_t h = ca_under_hash(cx, cz, mask), e;
            ok = 0;
            for (e = start[h]; e < start[h + 1u] && !ok; e++) {
                ok = ent[e].cx == cx && ent[e].cz == cz && ca_under_covers(&cov[ent[e].idx], smp[si], reach);
                if (ok) {
                    covered_by[si] = ent[e].idx;
                }
            }
        }
        if (ok) {
            if (fr->flags & CA_GT_CURVED) {
                b->curved++;            /* LOOP-L1: part of a curved surface (a loop's ceiling): kept as it is */
            } else if (!drop && n[1] <= CA_UNDER_FLAT_NY) {
                b->front_marked += !(fr->mat & SA1_MAT_FRONT_ONLY);
                fr->mat |= SA1_MAT_FRONT_ONLY;      /* FIX-C2: a ceiling from below, absent from above */
                /* the slab's top over it: not touched from deep behind by a sphere rising from below (SA1CA_MAT_THIN_TOP) */
                for (si = 0; si < 7; si++) {
                    b->fr[cov[covered_by[si]].frag].mat |= SA1CA_MAT_THIN_TOP;
                }
            } else {
                fr->flags |= CA_GT_DROP;
                b->undersides++;
            }
        }
    }
    /* remove them (the input order of the others kept) */
    for (f = 0, out = 0; f < b->nfr; f++) {
        if (!(b->fr[f].flags & CA_GT_DROP)) {
            b->fr[out++] = b->fr[f];
        }
    }
    b->nfr = out;
done:
    sa1_mem_free(b->ctx, cov);
    sa1_mem_free(b->ctx, ent);
    sa1_mem_free(b->ctx, start);
    sa1_mem_free(b->ctx, fill);
}

/* SA1_GRID_UNDERSIDES_DROP (round 0.9.3; ceiling Review R.5.2): the input's FRONT_ONLY land pieces are left out too, so that
 * the option's "off" is the one-way behaviour before 0.9.3 (counted with the undersides) */
static void ca_grid_drop_front_only(ca_gb *b)
{
    size_t f, out;
    for (f = 0, out = 0; f < b->nfr; f++) {
        const ca_frag *fr = &b->fr[f];
        if (!(fr->flags & CA_GT_KILL) && (fr->mat & SA1_MAT_FRONT_ONLY) && !(fr->mat & SA1_MAT_DOUBLE_SIDE)) {
            b->undersides++;
            continue;
        }
        b->fr[out++] = b->fr[f];
    }
    b->nfr = out;
}

/* a token per synthesis (any thread: sa1_collision_prepare) */
static uint32_t ca_token_next(uint32_t generation)
{
    static volatile uint32_t counter;
    uint32_t c, t;
#if defined(_MSC_VER) && !defined(__clang__)
    c = (uint32_t)_InterlockedIncrement((volatile long *)&counter);
#else
    c = __atomic_add_fetch(&counter, 1u, __ATOMIC_RELAXED);
#endif
    t = (c * 0x9E3779B9u) ^ (generation * 0x85EBCA6Bu) ^ 0x5A17C0DEu;
    return t ? t : 1u;
}

/*
 * GRID-CAP (round 0.9.2; sadc docs/research/gsa1_090_water.md 6.1 / 6.4 / GRID-CAP with its Review R2.5): before the
 * fragments are allocated (76 bytes each while building), the input goes through the same partition in a counting mode:
 * every piece is counted (exactly the pieces ca_frag_push would store, before c2 leaves undersides out) and its object
 * (cell, COL word, orientation) and corners go into HyperLogLog sketches, which estimate the layout's objects and points
 * (ca_gobj_bytes: 12 bytes per point, 6 per triangle, about 230 per object). The synthesis fails here, early and with a
 * readable message, when there are more than CA_FRAG_MAX pieces (the count is exact) or when CA_CAP_MARGIN of the estimate
 * is over the static collision segment (64 MB: about 2.75-3.0 million GRID triangles of a real map, 22.5-24.3 bytes each;
 * a geometry nearer the limit goes on, and the layout pass decides exactly as before). Returns SA1_OK to go on.
 */
static sa1_status ca_grid_count(sa1_context *ctx, const sa1ca_static *s)
{
    ca_gb b;
    sa1_status st = SA1_OK;
    double objects = 0.0, points = 0.0, bytes;
    float at[3], len;
    memset(&b, 0, sizeof b);
    b.ctx = ctx;
    b.gp = &s->grid;
    b.cell = (double)s->grid.cell;
    b.edge2 = (double)s->grid.max_edge * (double)s->grid.max_edge;
    b.count_only = 1;
    b.hll_obj = (uint8_t *)sa1_mem_array(ctx, 2u * CA_HLL_M, 1u);
    if (b.hll_obj == NULL) {
        return SA1_OK;      /* no room for the sketches: the synthesis decides as before */
    }
    memset(b.hll_obj, 0, 2u * CA_HLL_M);
    b.hll_pts = b.hll_obj + CA_HLL_M;
    ca_grid_input(&b, s);
    sa1_to_host_point(ctx, b.big_at, at);
    len = (float)sqrt(b.big_len2) * ctx->coords.scale;
    if (b.st == SA1_ERR_LIMIT && b.cnt_land > CA_FRAG_MAX) {
        st = sa1_fail(ctx, SA1_ERR_LIMIT, "collision adapter: GRID: more than %u triangles after cutting (counted before "
                      "the cut; the %u MB collision segment holds about 2.75-3.0 M); longest face %.0f units at (%.0f %.0f "
                      "%.0f), %u pieces", (unsigned)CA_FRAG_MAX, (unsigned)(SA1CA_COLSEG_BYTES >> 20), (double)len,
                      (double)at[0], (double)at[1], (double)at[2],
                      (unsigned)(b.big_pieces > 0xFFFFFFFFull ? 0xFFFFFFFFull : b.big_pieces));
    } else if (b.st != SA1_OK) {
        st = b.st;          /* a face cut into too many pieces (ca_grid_poly_cut has said so), out of memory */
    } else {
        bytes = ca_cap_bytes(&b, &objects, &points);
        if (bytes * CA_CAP_MARGIN > (double)SA1CA_COLSEG_BYTES) {
            st = sa1_fail(ctx, SA1_ERR_LIMIT, "collision adapter: GRID needs about %.1f MB, more than the %u MB collision "
                          "segment (about 2.75-3.0 M GRID triangles of a real map): %u GRID triangles, about %.0f points, %.0f "
                          "objects; longest face %.0f units at (%.0f %.0f %.0f), %u pieces", bytes / 1048576.0,
                          (unsigned)(SA1CA_COLSEG_BYTES >> 20), (unsigned)(b.cnt_land + b.cnt_kill), points, objects,
                          (double)len, (double)at[0], (double)at[1], (double)at[2],
                          (unsigned)(b.big_pieces > 0xFFFFFFFFull ? 0xFFFFFFFFull : b.big_pieces));
        }
    }
    sa1_mem_free(ctx, b.hll_obj);
    return st;
}

sa1_status sa1ca_build_grid(sa1_context *ctx, const sa1ca_static *s, sa1ca_alloc_fn alloc, void *user,
                            sa1ca_built *out)
{
    ca_gb b;
    uint32_t *hash = NULL, i, k, nland = 0, nkill = 0, ncolumns = 0, hsize = 0;
    uint32_t base, off, o_lt, o_cols, o_polys, o_boxes, o_hash, o_acts = 0, o_dz = 0, o_objs, mesh_ms = 0, mesh_mat = 0;
    size_t total, j, bsize = 0;
    uint32_t nboard = 0;      /* round 0.9.5: land objects with board bits */
    uint8_t *host = NULL;
    float maxr = 0.0f;
    sa1_status st = SA1_OK;
    memset(out, 0, sizeof *out);
    memset(&b, 0, sizeof b);
    if (s == NULL || s->magic != SA1CA_MAGIC || alloc == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    b.ctx = ctx;
    b.gp = &s->grid;
    b.cell = (double)s->grid.cell;
    b.edge2 = (double)s->grid.max_edge * (double)s->grid.max_edge;
    st = ca_grid_count(ctx, s);     /* GRID-CAP: a geometry the segment cannot hold fails here, before the cut */
    if (st != SA1_OK) {
        goto done;
    }
    ca_grid_input(&b, s);
    if (b.st == SA1_OK && !(s->grid.flags & SA1_GRID_KEEP_UNDERSIDES)) {
        int drop = (s->grid.flags & SA1_GRID_UNDERSIDES_DROP) != 0;
        if (drop) {
            ca_grid_drop_front_only(&b);
        }
        ca_grid_undersides(&b, (double)s->grid.under_reach, drop);
    }
    if (b.st != SA1_OK) {
        st = b.st;
        goto done;
    }
    if (b.nfr > 1u) {
        qsort(b.fr, b.nfr, sizeof *b.fr, ca_frag_cmp);
    }
    /* objects: runs of the same (kill, cell, COL word); a land run longer than CA_OBJ_SPLIT_TRIS in Z order of the
     * centroids and cut into parts of about equal size, kill runs into chunks of at most CA_OBJ_MAX_TRIS */
    hash = (uint32_t *)sa1_mem_array(ctx, CA_LAYOUT_HASH, sizeof(uint32_t));
    if (hash == NULL) {
        st = SA1_ERR_OUT_OF_MEMORY;
        goto done;
    }
    for (j = 0; j < b.nfr;) {
        size_t r = j + 1u, part, nparts, limit;
        while (r < b.nfr && ca_same_object(&b.fr[j], &b.fr[r])) {
            r++;
        }
        limit = (b.fr[j].flags & CA_GT_KILL) ? CA_OBJ_MAX_TRIS : CA_OBJ_SPLIT_TRIS;
        if (!(b.fr[j].flags & CA_GT_KILL) && r - j > limit) {
            size_t q;
            for (q = j; q < r; q++) {
                b.fr[q].sub = ca_morton(&b.fr[q], b.cell);
            }
            qsort(b.fr + j, r - j, sizeof *b.fr, ca_frag_cmp_sub);
            b.split_objects++;
        }
        nparts = (r - j + limit - 1u) / limit;
        for (part = 0; part < nparts; part++) {
            size_t a = j + (r - j) * part / nparts, e = j + (r - j) * (part + 1u) / nparts;
            ca_gobj *o;
            if (!(b.fr[a].flags & CA_GT_KILL) && !(s->grid.flags & SA1_GRID_DC_LIMITS) && e - a > 1u) {
                qsort(b.fr + a, e - a, sizeof *b.fr, ca_frag_cmp_zdesc);
            }
            if (!ca_grow(ctx, (void **)&b.obj, &b.capobj, b.nobj + 1u, sizeof *b.obj, b.nobj)) {
                st = SA1_ERR_OUT_OF_MEMORY;
                goto done;
            }
            o = &b.obj[b.nobj++];
            memset(o, 0, sizeof *o);
            o->first = (uint32_t)a;
            o->count = (uint32_t)(e - a);
            o->col = b.fr[a].col;
            o->kill = (b.fr[a].flags & CA_GT_KILL) != 0;
            o->board = o->kill ? 0u : (b.fr[a].flags & CA_GT_BOARD) >> 4;
            st = ca_grid_layout_object(&b, o, hash);
            if (st != SA1_OK) {
                st = sa1_fail(ctx, st, "collision adapter: GRID object with too many points or materials");
                goto done;
            }
            if (o->kill) {
                nkill++;
            } else {
                nland++;
                if (nland == 1u || b.fr[a].key[0] != b.fr[b.obj[b.nobj - 2u].first].key[0]
                    || b.fr[a].key[2] != b.fr[b.obj[b.nobj - 2u].first].key[2]) {
                    ncolumns++;
                }
                if (o->r + SA1CA_GRID_COL_PAD > maxr) {
                    maxr = o->r + SA1CA_GRID_COL_PAD;
                }
            }
        }
        j = r;
    }
    if (nland > 0x00FFFFFFu || nkill > 0x00FFFFFFu) {
        st = sa1_fail(ctx, SA1_ERR_LIMIT, "collision adapter: GRID: too many objects");
        goto done;
    }
    for (hsize = 16u; hsize < 2u * ncolumns; hsize *= 2u) {
    }
    if (nland == 0) {
        hsize = 0;
    }
    /* layout: header, landtable, COL[], polys[], column index, kill act table + DEATHZONE list, objects */
    o_lt = SA1CA_GRID_HDR_SIZE;
    o_cols = o_lt + SA1CA_LANDTABLE_SIZE;
    o_polys = o_cols + SA1CA_COL_SIZE * nland;
    o_boxes = o_polys + 4u * nland;
    o_hash = o_boxes + 24u * nland;
    off = o_hash + SA1CA_GRID_HASH_ENTRY * hsize;
    if (nkill) {
        o_acts = off;
        o_dz = o_acts + 4u * SA1CA_STAGE_ACTS;
        off = o_dz + 8u * (nkill + 1u);
    }
    o_objs = off;
    total = o_objs;
    for (i = 0; i < b.nobj; i++) {
        total += ca_gobj_bytes(&b.obj[i]);
        if (total > (size_t)SA1CA_COLSEG_BYTES) {
            st = sa1_fail(ctx, SA1_ERR_LIMIT, "collision adapter: GRID needs more than %u MB",
                          (unsigned)(SA1CA_COLSEG_BYTES >> 20));
            goto done;
        }
    }
    /* round 0.9.5 (review R.1): the land objects' board bits, one byte per COL entry after the objects, only when some
       object has them (else the allocation and every byte of it are 0.2.0's) */
    nboard = 0;
    for (i = 0; i < b.nobj; i++) {
        nboard += !b.obj[i].kill && b.obj[i].board != 0;
    }
    bsize = nboard ? (((size_t)nland + 3u) & ~(size_t)3u) : 0u;
    base = alloc(user, total + bsize, 16, (void **)&host);
    if (base == 0 || host == NULL) {
        st = sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "collision adapter: no room for %u bytes of GRID collision",
                      (unsigned)(total + bsize));
        goto done;
    }
    memset(host, 0, total + bsize);
    /* objects, COL entries, polygon counts, DEATHZONE list */
    off = o_objs;
    {
        uint32_t li = 0, ki = 0;
        for (i = 0; i < b.nobj; i++) {
            const ca_gobj *o = &b.obj[i];
            ca_grid_write_object(&b, o, host, base, off);
            if (o->kill) {
                ca_wr32(host + o_dz + 8u * ki, 0xFFFFFFFFu);
                ca_wr32(host + o_dz + 8u * ki + 4u, base + off);
                ki++;
            } else {
                uint8_t *col = host + o_cols + (size_t)SA1CA_COL_SIZE * li;
                for (k = 0; k < 3; k++) {
                    ca_wrf(col + 4 * k, o->pos[k]);
                }
                ca_wrf(col + 0x0C, o->r + SA1CA_GRID_COL_PAD);
                ca_wr32(col + 0x18, base + off);
                ca_wr32(col + 0x20, o->col);
                if (bsize) {
                    host[total + li] = (uint8_t)o->board;
                }
                ca_wr32(host + o_polys + 4u * li, o->count);
                for (k = 0; k < 3; k++) {
                    ca_wrf(host + o_boxes + 24u * li + 4u * k, o->lo[k]);
                    ca_wrf(host + o_boxes + 24u * li + 12u + 4u * k, o->hi[k]);
                }
                li++;
            }
            mesh_ms += o->nms;
            mesh_mat += o->nmat;
            off += ca_gobj_bytes(o);
        }
    }
    if ((size_t)off != total) {
        st = sa1_fail(ctx, SA1_ERR_LIMIT, "collision adapter: GRID layout %u != planned %u", (unsigned)off,
                      (unsigned)total);
        goto done;
    }
    /* column index over the land objects (sorted by ix, iz) */
    if (hsize) {
        uint32_t li = 0;
        i = 0;
        while (i < b.nobj) {
            const ca_frag *f = &b.fr[b.obj[i].first];
            uint32_t h, first = li, cnt = 0;
            if (b.obj[i].kill) {
                i++;
                continue;
            }
            /* the land objects of one column are consecutive */
            while (i < b.nobj && !b.obj[i].kill && b.fr[b.obj[i].first].key[0] == f->key[0]
                   && b.fr[b.obj[i].first].key[2] == f->key[2]) {
                i++;
                cnt++;
            }
            li += cnt;
            for (h = sa1ca_grid_hash(f->key[0], f->key[2]) & (hsize - 1u);; h = (h + 1u) & (hsize - 1u)) {
                uint8_t *he = host + o_hash + SA1CA_GRID_HASH_ENTRY * h;
                if (sa1_rd32(he + 12) == 0) {
                    ca_wr32(he, (uint32_t)f->key[0]);
                    ca_wr32(he + 4, (uint32_t)f->key[2]);
                    ca_wr32(he + 8, first);
                    ca_wr32(he + 12, cnt);
                    break;
                }
            }
        }
    }
    if (nkill) {
        for (k = 0; k < SA1CA_STAGE_ACTS; k++) {
            ca_wr32(host + o_acts + 4u * k, base + o_dz);
        }
    }
    /* the full landtable (information) */
    ca_wr16(host + o_lt, nland > 32767u ? 32767u : nland);
    ca_wr32(host + o_lt + 0x0C, nland ? base + o_cols : 0u);
    out->board = bsize ? base + (uint32_t)total : 0u;
    out->board_objects = nboard;
    /* header */
    out->token = ca_token_next(s->generation);
    ca_wr32(host + SA1CA_GH_MAGIC, SA1CA_GRID_MAGIC);
    ca_wr32(host + SA1CA_GH_GENERATION, s->generation);
    ca_wr32(host + SA1CA_GH_TOKEN, out->token);
    ca_wrf(host + SA1CA_GH_CELL, s->grid.cell);
    ca_wr32(host + SA1CA_GH_OBJECTS, nland);
    ca_wr32(host + SA1CA_GH_COLS, nland ? base + o_cols : 0u);
    ca_wr32(host + SA1CA_GH_POLYS, nland ? base + o_polys : 0u);
    ca_wr32(host + SA1CA_GH_HASH, hsize ? base + o_hash : 0u);
    ca_wr32(host + SA1CA_GH_HASH_SIZE, hsize);
    ca_wrf(host + SA1CA_GH_MAX_RADIUS, maxr);
    ca_wr32(host + SA1CA_GH_KILL_ACTS, nkill ? base + o_acts : 0u);
    ca_wr32(host + SA1CA_GH_KILLS, nkill);
    ca_wr32(host + SA1CA_GH_LANDTABLE, base + o_lt);
    ca_wr32(host + SA1CA_GH_WIN_OBJECTS, s->grid.win_objects);
    ca_wr32(host + SA1CA_GH_WIN_POLYS, s->grid.win_polys);
    ca_wr32(host + SA1CA_GH_FLAGS, s->grid.flags);
    ca_wr32(host + SA1CA_GH_BOXES, nland ? base + o_boxes : 0u);
    out->landtable = base + o_lt;
    out->cols = nland ? base + o_cols : 0u;
    out->base = base;
    out->bytes = (uint32_t)total;
    out->objects = nland;
    out->kill_objects = nkill;
    out->meshsets = mesh_ms;
    out->materials = mesh_mat;
    out->points = (uint32_t)b.npts;
    out->triangles = (uint32_t)b.nfr;
    for (j = 0; j < b.nfr; j++) {
        out->odd_triangles += (b.fr[j].flags & CA_GT_ODD) != 0;
        out->front_only += !(b.fr[j].flags & CA_GT_KILL) && (b.fr[j].mat & SA1_MAT_FRONT_ONLY)
                           && !(b.fr[j].mat & SA1_MAT_DOUBLE_SIDE);
    }
    out->curved = b.curved;
    out->grid = base;
    out->split = b.split;
    out->cut = b.cut;
    out->dropped = b.dropped;
    out->undersides = b.undersides;
    out->inexact = b.inexact;
    out->generation = s->generation;
    sa1_log(ctx, SA1_LOG_DEBUG,
            "collision adapter: GRID %08X: cell %g, %u land objects in %u columns (%u cells split), %u kill objects, "
            "%u triangles (%u split, %u merged into faces, %u cut, %u dropped, %u undersides within reach left out, %u "
            "made front-only, %u curved kept; %u front-only in all), %u points (%u not exact), %u bytes",
            (unsigned)base, (double)s->grid.cell, (unsigned)nland, (unsigned)ncolumns, (unsigned)b.split_objects,
            (unsigned)nkill, (unsigned)b.nfr, (unsigned)b.split, (unsigned)b.merged, (unsigned)b.cut,
            (unsigned)b.dropped, (unsigned)b.undersides, (unsigned)b.front_marked, (unsigned)b.curved,
            (unsigned)out->front_only, (unsigned)b.npts, (unsigned)b.inexact, (unsigned)total);
done:
    if (st != SA1_OK) {
        memset(out, 0, sizeof *out);
    }
    sa1_mem_free(ctx, hash);
    sa1_mem_free(ctx, b.fr);
    sa1_mem_free(ctx, b.obj);
    sa1_mem_free(ctx, b.pts);
    sa1_mem_free(ctx, b.mats);
    return st;
}

sa1_status sa1ca_build(sa1_context *ctx, sa1ca_alloc_fn alloc, void *user, sa1ca_built *out)
{
    sa1ca_static *s;
    sa1_status st;
    if (ctx == NULL || out == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof *out);
    s = sa1ca_static_of(ctx);
    if (s == NULL || (s->mode != SA1_COLLISION_MODE_DISC && s->num_groups == 0 && s->num_flat == 0)) {
        return SA1_OK;  /* no static geometry: B_8C7596F4 stays NULL (8C052518 checks it) */
    }
    switch (s->mode) {
    case SA1_COLLISION_MODE_FAITHFUL:
        st = sa1ca_build_faithful(ctx, s, alloc, user, out);
        break;
    case SA1_COLLISION_MODE_GRID:
        st = sa1ca_build_grid(ctx, s, alloc, user, out);
        break;
    default:
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED,
                        "collision adapter: DISC mode uses the stage's own landtable (world image with "
                        "sa1ca_disc_stage, B_8C7596F4 = sa1ca_disc_landtable)");
    }
    if (st == SA1_OK) {
        out->generation = s->generation;
        s->built = *out;
        s->builds++;
    }
    return st;
}

/* ------------------------------------------------------------------------------------------------ */
/* 4. DISC mode                                                                                     */
/* ------------------------------------------------------------------------------------------------ */

/* stage files DISC mode may read that sa1_manifest.c does not list (metadata: name, size, SHA-1) */
static const struct {
    const char *name;
    uint32_t size;
    const char *sha1;
} k_stage_files[] = {
    { "STG01.PRS", 1300946u, "7a7f716143e1bf537b6b1db9cf79da66918828bf" },   /* Emerald Coast */
    { "STG03.PRS", 647178u, "60643604446d72acbb9cdcbf7e48a2d340d6e7e2" },    /* Twinkle Park */
    { "STG05.PRS", 1256539u, "0b5fbddcdbf6d22087ee3afc3a34fa113b66d012" },   /* Red Mountain */
    { "STG10.PRS", 1100822u, "a19f0c6d0bab3fbb14e332a49c5775db3a9a173f" },   /* Final Egg */
};

void sa1ca_stage_free(const sa1_context *ctx, sa1ca_stage *stage)
{
    if (stage != NULL) {
        sa1_mem_free(ctx, stage->data);
        memset(stage, 0, sizeof *stage);
    }
}

sa1_status sa1ca_disc_stage(sa1_context *ctx, const char *gdi_path, int32_t level, uint32_t flags,
                            sa1ca_stage *stage)
{
    const sa1_file_desc *fd;
    const char *want = NULL;
    uint32_t want_size = 0, n = 0, i;
    char iso[48];
    uint8_t *raw = NULL;
    sa1_disc *disc;
    sa1_status st;
    if (ctx == NULL || gdi_path == NULL || stage == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    memset(stage, 0, sizeof *stage);
    if ((level < 0 || level > 10) && level != SA1CA_LEVEL_SUPER) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "collision adapter: no stage overlay for level %d",
                        (int)level);
    }
    if (level == SA1CA_LEVEL_SUPER) {
        /* round 6a: the Perfect Chaos stage's overlay is the boss overlay B_CHAOS7 (in the manifest) */
        snprintf(stage->name, sizeof stage->name, "B_CHAOS7.PRS");
    } else {
        snprintf(stage->name, sizeof stage->name, "STG%02d.PRS", (int)level);
    }
    snprintf(iso, sizeof iso, "SONICADV/%s", stage->name);
    fd = sa1_manifest_file(stage->name);
    if (fd != NULL) {
        want = fd->sha1;
        want_size = fd->size;
    }
    for (i = 0; i < sizeof k_stage_files / sizeof k_stage_files[0]; i++) {
        if (strcmp(k_stage_files[i].name, stage->name) == 0) {
            want = k_stage_files[i].sha1;
            want_size = k_stage_files[i].size;
        }
    }
    disc = (sa1_disc *)sa1_mem_alloc(ctx, sizeof *disc);
    if (disc == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    st = sa1_disc_open(ctx, gdi_path, disc);
    if (st == SA1_OK) {
        st = sa1_disc_read_file(disc, iso, &raw, &n);
        sa1_disc_close(disc);
    }
    sa1_mem_free(ctx, disc);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, raw);
        return sa1_fail(ctx, st, "collision adapter: cannot read %s (%s)", iso, sa1_status_string(st));
    }
    sa1_sha1_hex(raw, n, stage->sha1);
    if (want == NULL || want_size != n || strcmp(want, stage->sha1) != 0) {
        if (!(flags & SA1_LOAD_ACCEPT_MODIFIED)) {
            sa1_mem_free(ctx, raw);
            return sa1_fail(ctx, SA1_ERR_BAD_HASH, "collision adapter: %s SHA-1 %s (%u bytes) is not US v1.005's",
                            iso, stage->sha1, (unsigned)n);
        }
        sa1_log(ctx, SA1_LOG_WARN, "collision adapter: %s has an unknown SHA-1 %s (accepted)", iso, stage->sha1);
    }
    st = sa1_prs_decompress(ctx, raw, n, SA1_MAX_UNPACKED_SIZE, &stage->data, &stage->size);
    sa1_mem_free(ctx, raw);
    if (st != SA1_OK) {
        memset(stage, 0, sizeof *stage);
        return sa1_fail(ctx, st, "collision adapter: %s: PRS decompression failed", iso);
    }
    if (stage->size > 0x0D000000u - SA1CA_STAGE_SLOT) {
        sa1ca_stage_free(ctx, stage);
        return sa1_fail(ctx, SA1_ERR_BAD_FORMAT, "collision adapter: %s does not fit DC RAM", iso);
    }
    stage->load = SA1CA_STAGE_SLOT;
    return SA1_OK;
}

uint32_t sa1ca_disc_landtable(const uint8_t *first_read, size_t size, int32_t level, int32_t act)
{
    size_t off;
    if (first_read == NULL || !SA1CA_DISC_LEVEL_OK(level) || act < 0 || act >= SA1CA_STAGE_ACTS) {
        return 0;
    }
    off = (size_t)(SA1CA_ADDR_LANDTABLES - SA1_FIRST_READ_LOAD) + 4u * ((size_t)level * SA1CA_STAGE_ACTS + (size_t)act);
    if (off + 4u > size) {
        return 0;
    }
    return sa1_rd32(first_read + off);
}

/* ------------------------------------------------------------------------------------------------ */
/* 5. landtable dumps (harness --dump-land v2, sadc harness/src/sa1_trace.h)                        */
/* ------------------------------------------------------------------------------------------------ */
#define CA_LAND_MAGIC    0x444E414Cu    /* "LAND" */
#define CA_LANDCOL_MAGIC 0x4C4F434Cu    /* "LCOL" */
#define CA_LAND_HDR      32u
#define CA_LAND_TRI      108u           /* sa1_land_tri */
#define CA_LAND_COL      136u           /* sa1_land_col */
#define CA_DUMP_MAX      (512u << 20)

static sa1_status ca_read_file(sa1_context *ctx, const char *path, uint8_t **data, uint32_t *size)
{
    void *f;
    uint64_t n = 0;
    sa1_status st;
    *data = NULL;
    *size = 0;
    f = sa1_file_open(ctx, path);
    if (f == NULL) {
        return sa1_fail(ctx, SA1_ERR_IO, "collision adapter: cannot open %s", path);
    }
    st = sa1_file_size(ctx, f, &n);
    if (st == SA1_OK && (n < CA_LAND_HDR || n > CA_DUMP_MAX)) {
        st = SA1_ERR_BAD_FORMAT;
    }
    if (st == SA1_OK) {
        *data = (uint8_t *)sa1_mem_alloc(ctx, (size_t)n);
        st = *data ? sa1_file_read(ctx, f, 0, *data, (size_t)n) : SA1_ERR_OUT_OF_MEMORY;
    }
    sa1_file_close(ctx, f);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, *data);
        *data = NULL;
        return sa1_fail(ctx, st, "collision adapter: cannot read %s", path);
    }
    *size = (uint32_t)n;
    return SA1_OK;
}

void sa1ca_dump_free(const sa1_context *ctx, sa1ca_dump *d)
{
    if (d == NULL) {
        return;
    }
    sa1_mem_free(ctx, d->groups);
    sa1_mem_free(ctx, d->points);
    sa1_mem_free(ctx, d->indices);
    sa1_mem_free(ctx, d->materials);
    sa1_mem_free(ctx, d->tri_flags);
    sa1_mem_free(ctx, d->world);
    memset(d, 0, sizeof *d);
}

/* one strip of the dump: triangles t0..t0+m-1 (tri index 0..m-1). Can flip bit f explain the corner
 * orders? Triangle i emits (s[i], s[i+1], s[i+2]) when (f + i) is even, else (s[i], s[i+2], s[i+1]). */
static int ca_strip_consistent(const uint8_t *tri, uint32_t t0, uint32_t m, uint32_t f, uint32_t *s)
{
    uint32_t i, k;
    for (i = 0; i < m + 2u; i++) {
        s[i] = 0xFFFFFFFFu;
    }
    for (i = 0; i < m; i++) {
        const uint8_t *r = tri + CA_LAND_HDR + (size_t)CA_LAND_TRI * (t0 + i);
        uint32_t e0 = sa1_rd16(r + 100), e1 = sa1_rd16(r + 102), e2 = sa1_rd16(r + 104);
        uint32_t want[3], odd = (f + i) & 1u;
        want[0] = e0;
        want[1] = odd ? e2 : e1;
        want[2] = odd ? e1 : e2;
        for (k = 0; k < 3; k++) {
            if (s[i + k] != 0xFFFFFFFFu && s[i + k] != want[k]) {
                return 0;
            }
            s[i + k] = want[k];
        }
    }
    return 1;
}

/* odd flags (SA1_COLTRI_STRIP_ODD) of the triangles of one COL, from the meshset types and the
 * corner orders (dump v2 does not record a strip's flip bit) */
static sa1_status ca_dump_parity(sa1_context *ctx, const uint8_t *tri, uint32_t t0, uint32_t count, uint8_t *flags,
                                 sa1ca_dump *d)
{
    uint32_t i = 0, *s;
    s = (uint32_t *)sa1_mem_array(ctx, (size_t)count + 2u, sizeof(uint32_t));
    if (s == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    while (i < count) {
        const uint8_t *r = tri + CA_LAND_HDR + (size_t)CA_LAND_TRI * (t0 + i);
        uint32_t type = sa1_rd16(r + 106) & 0xC000u, mset = sa1_rd16(r + 94), mesh = sa1_rd16(r + 96), m = 1, k;
        uint32_t c0, c1, f;
        if (sa1_rd16(r + 98) != 0) {
            sa1_mem_free(ctx, s);
            return sa1_fail(ctx, SA1_ERR_BAD_FORMAT, "collision adapter: dump triangle %u starts a polygon at "
                            "index %u", (unsigned)(t0 + i), (unsigned)sa1_rd16(r + 98));
        }
        while (i + m < count) {
            const uint8_t *q = tri + CA_LAND_HDR + (size_t)CA_LAND_TRI * (t0 + i + m);
            if (sa1_rd16(q + 94) != mset || sa1_rd16(q + 96) != mesh || sa1_rd16(q + 98) != m) {
                break;
            }
            m++;
        }
        if (type == 0x0000u) {
            f = 0;      /* polygons: one triangle each, even */
        } else if (type == 0x4000u) {
            f = 0;      /* quads: two-triangle strips without the flip bit (8C028666), the second is odd */
        } else {
            d->strips++;
            c0 = (uint32_t)ca_strip_consistent(tri, t0 + i, m, 0, s);
            c1 = (uint32_t)ca_strip_consistent(tri, t0 + i, m, 1, s);
            if (c0 && !c1) {
                f = 0;
                d->parity_deduced++;
            } else if (c1 && !c0) {
                f = 1;
                d->parity_deduced++;
            } else {
                f = 0;  /* one-triangle strip: SEGA's stripifier wrote them unflipped (EC1: all 402) */
                if (c0 && c1) {
                    d->parity_assumed++;
                } else {
                    d->parity_conflicts++;
                }
            }
        }
        for (k = 0; k < m; k++) {
            uint32_t odd = (f + k) & 1u;
            flags[i + k] = odd ? (uint8_t)SA1_COLTRI_STRIP_ODD : 0u;
            d->odd_triangles += odd;
        }
        i += m;
    }
    sa1_mem_free(ctx, s);
    return SA1_OK;
}

sa1_status sa1ca_dump_load(sa1_context *ctx, const char *prefix, sa1ca_dump *out)
{
    char path[1100];
    uint8_t *tri = NULL, *col = NULL, *set = NULL;
    uint32_t ntsize = 0, ncsize = 0, ntri, ncol, i, k, npts = 0;
    sa1ca_dump d;
    sa1_status st;
    if (ctx == NULL || prefix == NULL || out == NULL || strlen(prefix) + 5 >= sizeof path) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof *out);
    memset(&d, 0, sizeof d);
    snprintf(path, sizeof path, "%s.tri", prefix);
    st = ca_read_file(ctx, path, &tri, &ntsize);
    if (st == SA1_OK) {
        snprintf(path, sizeof path, "%s.col", prefix);
        st = ca_read_file(ctx, path, &col, &ncsize);
    }
    if (st != SA1_OK) {
        goto fail;
    }
    ntri = sa1_rd32(tri + 8);
    ncol = sa1_rd32(tri + 12);
    if (sa1_rd32(tri) != CA_LAND_MAGIC || sa1_rd32(tri + 4) != 2 || sa1_rd32(tri + 16) != CA_LAND_TRI
        || sa1_rd32(tri + 20) != CA_LAND_COL || (uint64_t)CA_LAND_HDR + (uint64_t)ntri * CA_LAND_TRI != ntsize
        || sa1_rd32(col) != CA_LANDCOL_MAGIC || sa1_rd32(col + 4) != 2 || sa1_rd32(col + 8) != ncol
        || sa1_rd32(col + 12) != CA_LAND_COL || sa1_rd32(col + 20) != ntri
        || (uint64_t)CA_LAND_HDR + (uint64_t)ncol * CA_LAND_COL != ncsize || ncol > SA1_MAX_COLLISION_GROUPS
        || ntri > SA1_MAX_COLLISION_TRIS) {
        st = sa1_fail(ctx, SA1_ERR_BAD_FORMAT, "collision adapter: %s.tri/.col is not a version-2 landtable dump",
                      prefix);
        goto fail;
    }
    d.landtable = sa1_rd32(tri + 24);
    d.nnodes = sa1_rd32(tri + 28);
    d.ncol = ncol;
    d.ntri = ntri;
    /* points: each COL's root model has npoints; the dump gives every point a triangle uses */
    for (i = 0; i < ncol; i++) {
        const uint8_t *c = col + CA_LAND_HDR + (size_t)CA_LAND_COL * i;
        uint32_t np = sa1_rd32(c + 124);
        if (np > SA1_MAX_GROUP_POINTS || npts + np < npts) {
            st = sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "collision adapter: dump COL %u has %u points", (unsigned)i,
                          (unsigned)np);
            goto fail;
        }
        npts += np;
    }
    d.groups = (sa1_collision_group *)sa1_mem_array(ctx, ncol ? ncol : 1u, sizeof(sa1_collision_group));
    d.points = (float *)sa1_mem_array(ctx, npts ? 3u * (size_t)npts : 1u, sizeof(float));
    set = (uint8_t *)sa1_mem_alloc(ctx, npts ? npts : 1u);
    d.indices = (uint32_t *)sa1_mem_array(ctx, ntri ? 3u * (size_t)ntri : 1u, sizeof(uint32_t));
    d.materials = (uint32_t *)sa1_mem_array(ctx, ntri ? ntri : 1u, sizeof(uint32_t));
    d.tri_flags = (uint8_t *)sa1_mem_alloc(ctx, ntri ? ntri : 1u);
    d.world = (float *)sa1_mem_array(ctx, ntri ? 9u * (size_t)ntri : 1u, sizeof(float));
    if (!d.groups || !d.points || !set || !d.indices || !d.materials || !d.tri_flags || !d.world) {
        st = SA1_ERR_OUT_OF_MEMORY;
        goto fail;
    }
    npts = 0;
    for (i = 0; i < ncol; i++) {
        const uint8_t *c = col + CA_LAND_HDR + (size_t)CA_LAND_COL * i;
        sa1_collision_group *g = &d.groups[i];
        uint32_t t0 = sa1_rd32(c + 112), tn = sa1_rd32(c + 116), np = sa1_rd32(c + 124), t;
        if (sa1_rd32(c) != i || t0 > ntri || tn > ntri - t0 || (sa1_rd32(c + 80) == 0 && tn != 0)) {
            st = sa1_fail(ctx, SA1_ERR_BAD_FORMAT, "collision adapter: dump COL %u: bad triangle range", (unsigned)i);
            goto fail;
        }
        if (sa1_rd32(c + 120) > 1u) {
            st = sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "collision adapter: dump COL %u has %u nodes: dump v2 has no "
                          "child transforms", (unsigned)i, (unsigned)sa1_rd32(c + 120));
            goto fail;
        }
        g->struct_size = sizeof *g;
        g->flags = SA1_COLGROUP_RAW_FLAGS | SA1_COLGROUP_EVALFLAGS | SA1_COLGROUP_MODEL_BOUNDS | SA1_COLGROUP_COL_SPHERE;
        g->surface = sa1_rd32(c + 4);
        for (k = 0; k < 3; k++) {
            memcpy(&g->col_center[k], c + 8 + 4 * k, 4);
            memcpy(&g->position[k], c + 44 + 4 * k, 4);
            g->angle[k] = (int32_t)sa1_rd32(c + 56 + 4 * k);
            memcpy(&g->scale[k], c + 68 + 4 * k, 4);
            memcpy(&g->model_center[k], c + 84 + 4 * k, 4);
        }
        memcpy(&g->col_radius, c + 20, 4);
        memcpy(&g->col_ywidth, c + 24, 4);
        memcpy(&g->col_zwidth, c + 28, 4);
        g->col_blockbit = sa1_rd32(c + 32);
        g->evalflags = sa1_rd32(c + 40);
        memcpy(&g->model_radius, c + 96, 4);
        g->first_point = npts;
        g->num_points = np;
        g->first_tri = t0;
        g->num_tris = tn;
        for (t = t0; t < t0 + tn; t++) {
            const uint8_t *r = tri + CA_LAND_HDR + (size_t)CA_LAND_TRI * t;
            if (sa1_rd32(r + 40) != i || sa1_rd16(r + 92) != 0) {
                st = sa1_fail(ctx, SA1_ERR_BAD_FORMAT, "collision adapter: dump triangle %u is not COL %u's root node",
                              (unsigned)t, (unsigned)i);
                goto fail;
            }
            for (k = 0; k < 3; k++) {
                uint32_t idx = sa1_rd16(r + 100 + 2 * k), pk = npts + idx;
                if (idx >= np) {
                    st = sa1_fail(ctx, SA1_ERR_BAD_FORMAT, "collision adapter: dump triangle %u: point %u of %u",
                                  (unsigned)t, (unsigned)idx, (unsigned)np);
                    goto fail;
                }
                if (set[pk] && memcmp(&d.points[3u * (size_t)pk], r + 44 + 12 * k, 12) != 0) {
                    st = sa1_fail(ctx, SA1_ERR_BAD_FORMAT, "collision adapter: dump COL %u point %u differs between "
                                  "triangles", (unsigned)i, (unsigned)idx);
                    goto fail;
                }
                memcpy(&d.points[3u * (size_t)pk], r + 44 + 12 * k, 12);
                set[pk] = 1;
                d.indices[3u * (size_t)t + k] = idx;
            }
            memcpy(&d.world[9u * (size_t)t], r, 36);
            d.materials[t] = sa1_rd32(r + 80);
        }
        st = ca_dump_parity(ctx, tri, t0, tn, d.tri_flags + t0, &d);
        if (st != SA1_OK) {
            goto fail;
        }
        npts += np;
    }
    d.desc.struct_size = sizeof d.desc;
    d.desc.mode = SA1_COLLISION_MODE_FAITHFUL;
    d.desc.flags = SA1_COLLISION_SA1_SPACE;
    d.desc.points = d.points;
    d.desc.num_points = npts;
    d.desc.indices = d.indices;
    d.desc.materials = d.materials;
    d.desc.tri_flags = d.tri_flags;
    d.desc.num_tris = ntri;
    d.desc.groups = d.groups;
    d.desc.num_groups = ncol;
    d.desc.disc_level = -1;
    d.desc.disc_act = -1;
    sa1_mem_free(ctx, tri);
    sa1_mem_free(ctx, col);
    sa1_mem_free(ctx, set);
    *out = d;
    sa1_log(ctx, SA1_LOG_DEBUG, "collision adapter: dump %s: landtable %08X, %u COLs, %u triangles, %u strips "
            "(%u flip bits deduced, %u assumed 0, %u conflicts), %u odd triangles", prefix, (unsigned)d.landtable,
            (unsigned)ncol, (unsigned)ntri, (unsigned)d.strips, (unsigned)d.parity_deduced,
            (unsigned)d.parity_assumed, (unsigned)d.parity_conflicts, (unsigned)d.odd_triangles);
    return SA1_OK;
fail:
    sa1_mem_free(ctx, tri);
    sa1_mem_free(ctx, col);
    sa1_mem_free(ctx, set);
    sa1ca_dump_free(ctx, &d);
    return st;
}

/* ------------------------------------------------------------------------------------------------ */
/* 6. moving objects                                                                                */
/* ------------------------------------------------------------------------------------------------ */

sa1ca_objx *sa1ca_objx_of(const sa1_col_object *o)
{
    sa1ca_objx *x;
    if (o == NULL || o->tris == NULL) {
        return NULL;
    }
    x = (sa1ca_objx *)(void *)o->tris - 1;
    return x->magic == SA1CA_OBJX_MAGIC ? x : NULL;
}

static int32_t ca_bams(double rad)
{
    double t = floor(rad * (65536.0 / 6.283185307179586476925) + 0.5);
    return (int32_t)((int64_t)t & 0xFFFF);
}

/* R = Rz(c) * Ry(b) * Rx(a) (standard right-handed rotations, as njRotateZ / Y / X build them):
 * R[2][0] = -sin b, R[2][1] = cos b sin a, R[2][2] = cos b cos a, R[1][0] = sin c cos b, R[0][0] = cos c cos b */
void sa1ca_rotation_to_bams(const float r[9], int32_t ang[3])
{
    double sb = -(double)r[6], a, b, c;
    sb = sb > 1.0 ? 1.0 : (sb < -1.0 ? -1.0 : sb);
    b = asin(sb);
    if (fabs(sb) < 0.99999988) {
        a = atan2((double)r[7], (double)r[8]);
        c = atan2((double)r[3], (double)r[0]);
    } else {
        a = 0.0;                        /* gimbal lock: Rz and Rx turn about the same axis */
        c = atan2(-(double)r[1], (double)r[4]);
    }
    ang[0] = ca_bams(a);
    ang[1] = ca_bams(b);
    ang[2] = ca_bams(c);
}
