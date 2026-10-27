/*
 * sa1_api_world.c - the world side of the public character API (src/sa1_api_world.h; sadc
 * docs/libsa1_integration.md D2, 2.5, 5.3, 6.2-6.7, 7.1).
 *
 * Per context (struct sa1w_ctx): the pristine world image of each character kind, composed once from the
 * .gdi of the last sa1_load_disc (sa1g_wctx), DISC-mode images (the kind's image with the stage overlay
 * STGnn at 0x0C900000 instead of B_CHAOS7), and the static collision segment (guest 0x88000000, shared by
 * the context's worlds) holding the FAITHFUL landtable of the current geometry generation.
 *
 * Per ticking character: one world (D2) booted like the game's stage start (6.2, 6.3): heap, tasks,
 * collision, the tick-contract globals, the camera adapter, the landtable, player 0 at the given start
 * (or the game's start table), no extra land update (the cache is built at the end of step 0, as in the
 * game and in the reference traces). The landtable comes from the context's static collision:
 *   FAITHFUL  synthesized by the collision adapter (sa1ca_build) into the collision segment, sealed
 *             read-only, used in place like a stage's own landtable (design 5.3);
 *   DISC      the stage's own landtable D_8C19E9FC[level][act] in the DISC image, with the stage's death
 *             zone task 8C07BAC0 (its DeathZoneTable is in that image);
 *   none, GRID an empty landtable (GRID synthesis, design 5.4, is not implemented yet).
 * SA1W_STAGE_TASKS (SA1_CREATE_STAGE_TASKS) builds the world from the stage image (the kind + STGnn of the world's
 * level, as DISC mode does) in every mode and boots it with the stage's own tasks (sa1_world_stage.c) and its death
 * zone task; the landtable stays the mode's.
 * Prepared geometry (sa1_collision_set_prepared, G-SA1 request 11): the store's sa1_collision_prepared already holds the
 * synthesis in a collision segment of its own; the world is created on that segment (its segments 0x08-0x0B, the fault
 * classifier and encode use w->shared = the prepared segment), FAITHFUL uses the prepared landtable in place, GRID
 * attaches the prepared cells (sa1ca_world_attach), and the world holds a reference until it is destroyed.
 * Images: composed once per kind (on first use, or ahead by sa1_character_prepare on any thread); the table of composed
 * images is read and filled under the world core's table lock, composing runs outside it.
 * A tick is live mode (6.4, 6.6, 6.7): the host input on port A, the host camera yaw on the camera adapter,
 * whole pairs from the accumulator.
 * Round 6a (Super Sonic, include/libsa1.h "Super Sonic (API 0.4)"): kind 8 and a Sonic created with
 * SA1_CREATE_SUPER_CAPABLE get a Sonic world (character 0) on the Perfect Chaos stage, level 19 act 0, whose image holds
 * B_CHAOS7 (DISC mode: level 19's overlay is B_CHAOS7 itself, never added twice), booted with the Perfect Chaos level task
 * stand-in (sa1_world_stage.c) and the Super pieces of sa1_world_super.c (cfg.super: SA1G_SUPER_*), and the water floor
 * (SA1_CREATE_WATER_FLOOR) for any kind. Round 0.9.3a: a General world (SA1_SUPER_GENERAL, sadc
 * docs/research/super_general_design.md 4.1) is the same Sonic world on the host's stage (default level 1), the stand-in
 * made directly (sa1_world_boot.c / sa1_world_stage.c sa1g_stage_pc_create); kind 8 with SA1_SUPER_START_NORMAL has no
 * trigger.
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_coladapt.h"
#include "sa1_api_world.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define SA1W_DISC_IMAGES 4u                 /* stage images (DISC mode, stage tasks) per context */
#define SA1W_TWO_PI 6.28318530717958647692

struct sa1w_ctx {
    sa1g_wctx wc;                           /* images per kind, the static collision segment */
    struct {
        int32_t used, kind, level;
        sa1g_image img;
    } disc[SA1W_DISC_IMAGES];               /* DISC mode: kind + stage overlay */
    uint32_t built_generation;              /* static geometry synthesized into the collision segment (0: none) */
    uint32_t built_landtable;
};

int sa1w_ready(void)
{
    return sa1g_game_ready();
}

/* the world state without sa1_fail (any thread) */
static sa1_status ctx_create_quiet(sa1_context *ctx, const char *gdi, struct sa1w_ctx **out)
{
    struct sa1w_ctx *wc;
    sa1_status st;
    *out = NULL;
    wc = (struct sa1w_ctx *)sa1_mem_alloc(ctx, sizeof *wc);
    if (wc == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    st = sa1g_wctx_init(&wc->wc, ctx, gdi);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, wc);
        return st;
    }
    *out = wc;
    return SA1_OK;
}

sa1_status sa1w_ctx_get(sa1_context *ctx, struct sa1w_ctx **out)
{
    sa1_api_ext *x = sa1_api_ext_of(ctx);
    sa1_status st = SA1_OK;
    sa1g_table_lock();
    if (x->world == NULL) {
        st = x->gdi[0] ? ctx_create_quiet(ctx, x->gdi, &x->world) : SA1_ERR_NOT_LOADED;
    }
    *out = x->world;
    sa1g_table_unlock();
    return st;
}

/* round 0.9.4b (objects2_plan.md 5.4, review R.3): the image cache and shared segments for a world that is not a
 * character's (the object showcase, src/world/sa1_world_preview.c) */
struct sa1g_wctx *sa1w_ctx_wc(struct sa1w_ctx *wc)
{
    return wc ? &wc->wc : NULL;
}

sa1_status sa1w_ctx_create(sa1_context *ctx, const char *gdi, struct sa1w_ctx **out)
{
    struct sa1w_ctx *wc;
    sa1_status st;
    *out = NULL;
    wc = (struct sa1w_ctx *)sa1_mem_alloc(ctx, sizeof *wc);
    if (wc == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    st = sa1g_wctx_init(&wc->wc, ctx, gdi);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, wc);
        return sa1_fail(ctx, st, "world: cannot set up the worlds of this context (collision segment, %s)",
                        sa1g_status_string(st));
    }
    *out = wc;
    return SA1_OK;
}

void sa1w_ctx_destroy(struct sa1w_ctx *wc)
{
    sa1_context *ctx;
    uint32_t i;
    if (wc == NULL) {
        return;
    }
    ctx = wc->wc.ctx;
    for (i = 0; i < SA1W_DISC_IMAGES; i++) {
        if (wc->disc[i].used) {
            sa1g_image_free(&wc->disc[i].img);
        }
    }
    sa1g_wctx_free(&wc->wc);
    sa1_mem_free(ctx, wc);
}

/* the image holds the overlay file `name` (e.g. "B_CHAOS7.PRS") */
static int image_has(const sa1g_image *img, const char *name)
{
    uint32_t i;
    for (i = 0; img != NULL && i < img->num_spans; i++) {
        if (strcmp(img->spans[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* round 6a: the Perfect Chaos stage (B_CHAOS7 at 0x0C900000) and the game character of a kind (Super Sonic's world is
 * Sonic's) */
#define SA1W_LEVEL_SUPER 19
static int32_t world_chr(int32_t kind)
{
    return kind == SA1_CHAR_SUPER_SONIC ? SA1_CHAR_SONIC : kind;
}

/* the stage image of a kind (B_CHAOS7 and S_SBMOT left out, STGnn of level at 0x0C900000), composed once: DISC mode
 * and stage tasks. Level 19's overlay is B_CHAOS7 itself (sa1ca_disc_stage): a kind whose image keeps it (Super Sonic:
 * not optional there) is not given a second occupant of 0x0C900000 */
static sa1_status disc_image(struct sa1w_ctx *wc, int32_t kind, int32_t level, const sa1g_image **out)
{
    sa1_context *ctx = wc->wc.ctx;
    sa1_status st;
    uint32_t i, freeslot = SA1W_DISC_IMAGES;
    *out = NULL;
    for (i = 0; i < SA1W_DISC_IMAGES; i++) {
        if (wc->disc[i].used && wc->disc[i].kind == kind && wc->disc[i].level == level) {
            *out = &wc->disc[i].img;
            return SA1_OK;
        }
        if (!wc->disc[i].used && freeslot == SA1W_DISC_IMAGES) {
            freeslot = i;
        }
    }
    if (freeslot == SA1W_DISC_IMAGES) {
        return sa1_fail(ctx, SA1_ERR_LIMIT, "world: more than %u DISC-mode images in one context",
                        (unsigned)SA1W_DISC_IMAGES);
    }
    /* B_CHAOS7 (optional) leaves the slot 0x0C900000 to the stage overlay */
    st = sa1g_image_compose(ctx, wc->wc.gdi, kind, SA1G_IMAGE_NO_OPTIONAL, &wc->disc[freeslot].img);
    if (st == SA1_OK && !(level == SA1W_LEVEL_SUPER && image_has(&wc->disc[freeslot].img, "B_CHAOS7.PRS"))) {
        st = sa1g_image_add_stage(&wc->disc[freeslot].img, wc->wc.gdi, level, 0);
    }
    if (st != SA1_OK) {
        sa1g_image_free(&wc->disc[freeslot].img);
        memset(&wc->disc[freeslot].img, 0, sizeof wc->disc[freeslot].img);
        return st;
    }
    wc->disc[freeslot].used = 1;
    wc->disc[freeslot].kind = kind;
    wc->disc[freeslot].level = level;
    *out = &wc->disc[freeslot].img;
    return SA1_OK;
}

static uint32_t colseg_alloc(void *user, size_t n, size_t align, void **host)
{
    return sa1g_colseg_alloc((sa1g_shared *)user, n, align, host);
}

/* ---- the composed images of a context (shared with sa1_character_prepare on any thread) ------------------------- */
static void wc_install(struct sa1w_ctx *wc, int32_t kind, sa1g_image *img)
{
    int dup;
    sa1g_table_lock();
    dup = wc->wc.composed[kind];
    if (!dup) {
        wc->wc.images[kind] = *img;
        wc->wc.composed[kind] = 1;
    }
    sa1g_table_unlock();
    if (dup) {
        sa1g_image_free(img);   /* another thread was first */
    }
}

static int wc_composed(struct sa1w_ctx *wc, int32_t kind)
{
    int have;
    sa1g_table_lock();
    have = wc->wc.composed[kind];
    sa1g_table_unlock();
    return have;
}

/* the pristine image of a kind (the thread that creates the world: composed here when no one prepared it) */
static sa1_status wc_image(struct sa1w_ctx *wc, int32_t kind, const sa1g_image **out)
{
    sa1g_image img;
    sa1_status st;
    *out = NULL;
    if (kind < 0 || kind >= SA1_CHAR_COUNT) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (!wc_composed(wc, kind)) {
        st = sa1g_image_compose(wc->wc.ctx, wc->wc.gdi, kind, 0, &img);
        if (st != SA1_OK) {
            return st;
        }
        wc_install(wc, kind, &img);
    }
    *out = &wc->wc.images[kind];    /* never changes once installed (until the world state is destroyed) */
    return SA1_OK;
}

sa1_status sa1w_prepare_images(sa1_context *ctx, uint32_t kinds)
{
    struct sa1w_ctx *wc = NULL;
    sa1_status st, first = SA1_OK;
    int32_t k;
    st = sa1w_ctx_get(ctx, &wc);
    if (st != SA1_OK) {
        return st;
    }
    for (k = 0; k < SA1_CHAR_COUNT; k++) {
        sa1_context *wx;
        sa1g_image img;
        if (!(kinds & (1u << k)) || sa1g_character_main(world_chr(k)) == 0 || wc_composed(wc, k)) {
            continue;
        }
        wx = sa1_worker_ctx_create(ctx);
        if (wx == NULL) {
            st = SA1_ERR_OUT_OF_MEMORY;
        } else {
            /* the worker context reads the same disc through the same file callbacks and allocator; the image then
               belongs to ctx (frees and the patch log of world creation go through it) */
            st = sa1g_image_compose(wx, wc->wc.gdi, k, 0, &img);
            if (st == SA1_OK) {
                img.ctx = ctx;
                wc_install(wc, k, &img);
            }
            sa1_worker_ctx_destroy(wx);
        }
        if (st != SA1_OK && first == SA1_OK) {
            first = st;
        }
    }
    return first;
}

/* ---- prepared static collision (any thread) ------------------------------------------------------------------- */
sa1_status sa1w_prepare_build(sa1_context *wctx, struct sa1ca_static *s, struct sa1_collision_prepared *p)
{
    sa1g_shared *seg;
    sa1_status st;
    seg = (sa1g_shared *)sa1_mem_alloc(wctx, sizeof *seg);
    if (seg == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    if (sa1g_shared_create(seg) != 0) {
        sa1_mem_free(wctx, seg);
        return sa1_fail(wctx, SA1_ERR_OUT_OF_MEMORY, "collision: cannot reserve a collision segment (%u MB)",
                        (unsigned)(SA1G_COL_SIZE >> 20));
    }
    p->seg = seg;
    memset(&p->built, 0, sizeof p->built);
    if (s->mode == SA1_COLLISION_MODE_FAITHFUL) {
        st = s->num_groups != 0 ? sa1ca_build_faithful(wctx, s, colseg_alloc, seg, &p->built) : SA1_OK;
    } else {
        st = (s->num_flat != 0 || s->num_groups != 0) ? sa1ca_build_grid(wctx, s, colseg_alloc, seg, &p->built) : SA1_OK;
    }
    p->built.generation = s->generation;
    if (sa1g_colseg_seal(seg, 1) != 0 && st == SA1_OK) {
        st = sa1_fail(wctx, SA1_ERR_OUT_OF_MEMORY, "collision: the prepared collision segment cannot be sealed");
    }
    return st;
}

void sa1w_prepare_free(struct sa1_collision_prepared *p)
{
    if (p != NULL && p->seg != NULL) {
        sa1g_shared_destroy(p->seg);
        sa1_host_mem_free(&p->host, p->seg);
        p->seg = NULL;
    }
}

/* the FAITHFUL landtable of the current geometry generation, synthesized once into the collision segment,
 * which is read-only afterwards (worlds of older generations keep their landtables: nothing is freed) */
static sa1_status faithful_landtable(struct sa1w_ctx *wc, const sa1ca_static *s, uint32_t *lt)
{
    sa1_context *ctx = wc->wc.ctx;
    sa1ca_built b;
    sa1_status st;
    if (wc->built_generation == s->generation && wc->built_landtable != 0) {
        *lt = wc->built_landtable;
        return SA1_OK;
    }
    if (sa1g_colseg_seal(&wc->wc.shared, 0) != 0) {
        return sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "world: the collision segment cannot be made writable");
    }
    memset(&b, 0, sizeof b);
    st = sa1ca_build(ctx, colseg_alloc, &wc->wc.shared, &b);
    if (sa1g_colseg_seal(&wc->wc.shared, 1) != 0 && st == SA1_OK) {
        st = sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "world: the collision segment cannot be sealed");
    }
    if (st != SA1_OK) {
        return st;
    }
    wc->built_generation = s->generation;
    wc->built_landtable = b.landtable;
    *lt = b.landtable;
    return SA1_OK;
}

sa1_status sa1w_world_create(struct sa1w_ctx *wc, const sa1w_create *c, struct sa1g_world **out, uint32_t *notes)
{
    sa1_context *ctx = wc->wc.ctx;
    const sa1ca_static *s = sa1ca_static_of(ctx);
    struct sa1_collision_prepared *prep = sa1ca_prepared_of(s);
    sa1g_shared *seg = prep != NULL ? prep->seg : &wc->wc.shared;
    const sa1g_image *img = NULL;
    sa1g_boot_cfg cfg;
    sa1g_world *w;
    sa1_status st;
    uint32_t k;
    /* round 6a: Super Sonic (kind 8) and a super-capable Sonic: a Sonic world on the Perfect Chaos stage (libsa1.h
     * "Super Sonic (API 0.4)") */
    int is_super = c->kind == SA1_CHAR_SUPER_SONIC || (c->flags & SA1W_SUPER_CAPABLE) != 0;
    /* round 0.9.3a: the General form (a Sonic world on the host's stage: sadc docs/research/super_general_design.md 4.1) */
    int general = is_super && c->kind == SA1_CHAR_SONIC && (c->flags & SA1W_SUPER_GIVEN) &&
                  (c->super_flags & SA1_SUPER_GENERAL) != 0;
    *out = NULL;
    *notes = 0;
    if (!sa1g_game_ready()) {
        return SA1_ERR_NOT_IMPLEMENTED;
    }
    if (c->kind < 0 || c->kind > SA1_CHAR_SUPER_SONIC || sa1g_character_main(world_chr(c->kind)) == 0) {
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "%s cannot tick yet (design 6.3a, D14)",
                        sa1_character_name(c->kind) ? sa1_character_name(c->kind) : "this character");
    }
    if (is_super && world_chr(c->kind) != SA1_CHAR_SONIC) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "only Sonic can turn Super (SA1_CREATE_SUPER_CAPABLE)");
    }
    if (is_super && !general && c->level >= 0 && (c->level != SA1W_LEVEL_SUPER || c->act != 0)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "Super Sonic's world is the Perfect Chaos stage (level 19 act 0), "
                        "not stage %d act %d (his 12 welds need it: super_sonic.md 1.8)", (int)c->level, (int)c->act);
    }
    sa1g_boot_defaults(&cfg);
    cfg.chr = world_chr(c->kind);
    cfg.level = (int16_t)(c->level >= 0 ? c->level : (is_super && !general ? SA1W_LEVEL_SUPER : 1));
    cfg.act = (int16_t)(c->act >= 0 ? c->act : 0);
    cfg.seed = c->seed;
    memcpy(cfg.event_flags, c->event_flags, sizeof cfg.event_flags);
    /* the stage start: the land collision cache is first built at the end of step 0 */
    cfg.flags = SA1G_BOOT_NO_WORLD_UPDATE;
    if (!(c->flags & SA1W_START_TABLE)) {
        cfg.flags |= SA1G_BOOT_POS;
        for (k = 0; k < 3; k++) {
            cfg.pos[k] = c->pos[k];
            cfg.ang[k] = c->ang[k];
        }
    }
    if (prep != NULL && seg == NULL) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "world: the prepared collision has no collision segment");
    }
    if (s != NULL && prep == NULL && s->mode == SA1_COLLISION_MODE_DISC && s->num_flat == 0) {
        const sa1g_span *fr;
        uint32_t lt;
        if (is_super && (s->disc_level != SA1W_LEVEL_SUPER || s->disc_act != 0)) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "Super Sonic's world needs the Perfect Chaos stage: DISC "
                            "collision of stage %d act %d cannot hold B_CHAOS7", (int)s->disc_level, (int)s->disc_act);
        }
        if (c->level < 0) {
            cfg.level = (int16_t)s->disc_level;
            cfg.act = (int16_t)s->disc_act;
        }
        st = disc_image(wc, c->kind, s->disc_level, &img);
        if (st != SA1_OK) {
            return st;
        }
        fr = sa1g_image_span_at(img, SA1G_FIRST_READ_ADDR, SA1G_FIRST_READ_SIZE);
        lt = fr != NULL ? sa1ca_disc_landtable(fr->data, fr->size, s->disc_level, s->disc_act) : 0u;
        if (lt == 0) {
            return sa1_fail(ctx, SA1_ERR_NOT_FOUND, "world: stage %d act %d has no landtable", (int)s->disc_level,
                            (int)s->disc_act);
        }
        cfg.land_mode = SA1G_LAND_DISC;
        cfg.landtable = lt;
        cfg.flags |= SA1G_BOOT_DEATHZONE;
    } else {
        if (c->flags & SA1W_STAGE_TASKS) {
            st = disc_image(wc, c->kind, cfg.level, &img);
        } else {
            st = wc_image(wc, c->kind, &img);
        }
        if (st != SA1_OK) {
            return st;
        }
        if (prep != NULL && prep->mode == SA1_COLLISION_MODE_FAITHFUL && prep->built.landtable != 0) {
            cfg.landtable = prep->built.landtable;     /* in the prepared segment, used in place */
            cfg.land_mode = SA1G_LAND_DISC;
        } else if (prep != NULL) {
            cfg.land_mode = SA1G_LAND_EMPTY;            /* GRID: sa1ca_world_attach puts the prepared cells in */
            if (prep->built.grid == 0) {
                *notes |= SA1W_NOTE_NO_GEOMETRY;
            }
        } else if (s != NULL && s->mode == SA1_COLLISION_MODE_FAITHFUL && s->num_flat == 0 && s->num_groups != 0) {
            st = faithful_landtable(wc, s, &cfg.landtable);
            if (st != SA1_OK) {
                return st;
            }
            cfg.land_mode = SA1G_LAND_DISC;
        } else {
            cfg.land_mode = SA1G_LAND_EMPTY;
            *notes |= (s != NULL && (s->num_flat != 0 || s->num_groups != 0)) ? SA1W_NOTE_GRID_PENDING
                                                                             : SA1W_NOTE_NO_GEOMETRY;
        }
    }
    if (c->flags & SA1W_STAGE_TASKS) {
        cfg.flags |= SA1G_BOOT_STAGE_TASKS | SA1G_BOOT_DEATHZONE;
    }
    /* round 6a: what the Perfect Chaos level task stand-in does (sa1_world_stage.c sa1g_stage_pc_level; it exists only in
     * level-19 worlds with B_CHAOS7's code map), the revert fix and the drain (sa1_world_super.c), the water floor */
    if (is_super && !image_has(img, "B_CHAOS7.PRS")) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "Super Sonic needs B_CHAOS7.PRS from the disc in the world image "
                        "(SA1_LOAD_SUPER)");
    }
    {
        int stage_land = cfg.land_mode == SA1G_LAND_DISC;  /* DISC or FAITHFUL: the stage's own landtable */
        uint32_t sf = (c->flags & SA1W_SUPER_GIVEN) ? c->super_flags : (stage_land ? (uint32_t)SA1_SUPER_SEA_PLANE : 0u);
        if (is_super) {
            cfg.super |= SA1G_SUPER_WORLD;
            if (c->kind == SA1_CHAR_SUPER_SONIC && !(sf & SA1_SUPER_START_NORMAL)) {
                cfg.super |= SA1G_SUPER_TRIGGER;    /* round 0.9.3a (RC1): START_NORMAL leaves the game's trigger out */
            }
            if (general) {
                cfg.super |= SA1G_SUPER_GENERAL | ((sf & SA1_SUPER_START) ? SA1G_SUPER_START : 0u);
            }
            if (sf & SA1_SUPER_WATER_WHILE_SUPER) {
                cfg.super |= SA1G_SUPER_WATER;
            }
            if (stage_land && (sf & SA1_SUPER_SEA_PLANE)) {
                cfg.super |= SA1G_SUPER_PLANE;
            }
            cfg.super |= ((sf & SA1_SUPER_NO_DRAIN) ? SA1G_SUPER_NO_DRAIN : 0u) |
                         ((sf & SA1_SUPER_KEEP_AT_ZERO) ? SA1G_SUPER_KEEP : 0u) |
                         ((sf & SA1_SUPER_FAITHFUL_REVERT) ? SA1G_SUPER_FAITHFUL : 0u);
        }
        if (c->flags & SA1W_STAGE_TASKS) {
            /* the stage's own tasks: the game's trigger, and its sea plane with the stage's landtable (a super-capable
             * world: as super_flags say) */
            cfg.super |= SA1G_SUPER_TRIGGER;
            if (stage_land && (!is_super || (sf & SA1_SUPER_SEA_PLANE))) {
                cfg.super |= SA1G_SUPER_PLANE;
            }
        }
        if (c->flags & SA1W_WATER_FLOOR) {
            cfg.super |= SA1G_WATER_FLOOR;
        }
    }
    w = (sa1g_world *)sa1_mem_alloc(ctx, sizeof *w);
    if (w == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    if (sa1g_world_create(w, seg) != 0) {
        sa1_mem_free(ctx, w);
        return sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "world: cannot reserve the 16 MB arena");
    }
    if (prep != NULL) {
        sa1ca_prepared_retain(prep);    /* the world maps its segment until it is destroyed */
        w->prepared = prep;
    }
    w->fresh = 1;                       /* nothing wrote the arena yet: apply_image need not zero it */
    st = sa1g_world_apply_image(w, img);
    if (st == SA1_OK) {
        w->kind = c->kind;
        memcpy(w->start_pos, c->pos, sizeof w->start_pos);
        w->start_ang_y = c->ang[1] & 0xFFFFu;
        st = sa1g_world_boot(w, &cfg);
        if (st == SA1_OK) {
            /* the collision adapter (design 5.4-5.6; EMPTY landtables only): GRID window, moving objects, kill planes */
            st = sa1ca_world_attach(ctx, w, notes);
        }
    }
    if (st != SA1_OK) {
        struct sa1_collision_prepared *held = w->prepared;
        sa1_fail(ctx, st, "world: boot of %s failed: %s", sa1_character_name(c->kind),
                 w->dead ? w->fault.msg : sa1g_status_string(st));
        sa1g_world_destroy(w);
        sa1_mem_free(ctx, w);
        sa1ca_prepared_release(held);
        return st;
    }
    /* round 0.9.1 (src/world/sa1_world_charopt.c): the character's lives, companions and game options (attach: the
       settings given before the world was built; pre: a companion freed after the first pair) */
    w->charopt_attach = sa1g_charopt_attach;
    w->charopt_pre = sa1g_charopt_pre;
    *out = w;
    return SA1_OK;
}

void sa1w_world_destroy(struct sa1w_ctx *wc, struct sa1g_world *w)
{
    struct sa1_collision_prepared *held;
    if (wc == NULL || w == NULL) {
        return;
    }
    held = w->prepared;
    sa1g_world_destroy(w);
    sa1_mem_free(wc->wc.ctx, w);
    sa1ca_prepared_release(held);   /* after the world: its segments pointed into the prepared segment */
}

sa1_status sa1w_world_tick(struct sa1g_world *w, const sa1_input *in, float dt, uint32_t *pairs)
{
    sa1w_world_input(w, in);
    return sa1g_world_tick(w, dt, 0, pairs);
}

/* ---- sa1w_world_tick in its parts (sa1_api_world.h; sadc docs/research/gsa1_060_pops.md F1) ---- */
uint32_t sa1w_world_pairs_due(const struct sa1g_world *w, float dt)
{
    double acc;
    uint32_t n = 0;
    if (w == NULL || !(dt >= 0.0f) || dt > 3600.0f) {
        return 0u;
    }
    /* sa1g_world_tick's loop (sa1_world_tick.c), the same double arithmetic: repeated subtraction */
    acc = w->acc + (double)dt;
    while (acc + 1e-9 >= SA1G_PAIR_SECONDS && n < SA1G_MAX_PAIRS_PER_TICK) {
        acc -= SA1G_PAIR_SECONDS;
        n++;
    }
    return n;
}

/* sa1g_world_tick's checks before it touches the accumulator (sa1_world_tick.c sa1g_pre_run, static there; keep the
 * two in step): the step is set, the game code is linked, the world is booted, the world is alive. Without them the
 * keep_backlog path took dt into the accumulator of a world that cannot run and, below a whole pair, said SA1_OK. */
static sa1_status run_check(const sa1g_world *w)
{
    if (w->reserve == NULL || w->step == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    if (w->step == sa1g_step_game && !sa1g_game_ready()) {
        return SA1_ERR_NOT_IMPLEMENTED;
    }
    if (w->step == sa1g_step_game && !w->booted) {
        return SA1_ERR_NOT_LOADED;
    }
    return SA1_OK;
}

sa1_status sa1w_world_run(struct sa1g_world *w, float dt, uint32_t max_pairs, int keep_backlog, uint32_t *pairs)
{
    sa1_status st = SA1_OK;
    uint32_t n = 0;
    if (!keep_backlog) {
        return sa1g_world_tick(w, dt, max_pairs, pairs);
    }
    if (pairs != NULL) {
        *pairs = 0;
    }
    if (w == NULL || !(dt >= 0.0f) || dt > 3600.0f) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = run_check(w);
    if (st != SA1_OK) {
        return st;
    }
    if (max_pairs == 0) {
        max_pairs = SA1G_MAX_PAIRS_PER_TICK;
    }
    /* sa1g_world_tick's accumulator loop without its backlog drop and clamp (the time left stays for the next run,
     * which continues with exactly this accumulator: its dt 0 adds nothing) */
    w->acc += (double)dt;
    while (w->acc + 1e-9 >= SA1G_PAIR_SECONDS && n < max_pairs) {
        st = sa1g_world_pair(w);
        if (st != SA1_OK) {
            break;
        }
        w->acc -= SA1G_PAIR_SECONDS;
        n++;
    }
    if (pairs != NULL) {
        *pairs = n;
    }
    return st;
}

void sa1w_world_input(struct sa1g_world *w, const sa1_input *in)
{
    sa1g_pad pad;
    w->replay = 0;
    sa1g_pad_from_input(in, &pad);
    sa1g_world_set_pad(w, 0, &pad);
    sa1g_world_host_flags(w, in);   /* round 0.9.2 (WTR-A): sa1_input.host_flags for the pairs of this tick */
    if (in != NULL && in->struct_size >= offsetof(sa1_input, camera_yaw) + sizeof(float) && isfinite(in->camera_yaw)) {
        /* 8C05117E: analog ang = atan2(stick y, x) - camera ang.y, and forward (y < 0) is -0x4000, while the
         * player moves along (cos a, 0, sin a) for ang.y = a: a forward stick follows the host camera yaw
         * (counter-clockwise, the sa1_pose.yaw convention: ang.y = -yaw) when camera ang.y = yaw - 0x4000 */
        double t = floor((double)in->camera_yaw / SA1W_TWO_PI * 65536.0 + 0.5);
        int64_t yb = (int64_t)fmod(t, 65536.0);
        w->camera.ang[1] = (uint32_t)(yb - 0x4000) & 0xFFFFu;
    }
}

/* arena bytes (zero outside the arena) */
static void arena_copy(const sa1g_world *w, void *dst, uint32_t g, uint32_t n)
{
    if (g == 0 || ((g >> 24) & 31u) != SA1G_SEG_ARENA || (g & 0xFFFFFFu) > SA1G_ARENA_SIZE - n) {
        memset(dst, 0, n);
        return;
    }
    memcpy(dst, w->ram + (g & 0xFFFFFFu), n);
}

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint16_t rd16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

static float rdf(const uint8_t *p)
{
    float v;
    memcpy(&v, p, 4);
    return v;
}

/* the decoded fields of a trace record (harness fill_record, tests/world_replay.c wr_record) */
/* round 0.9.4a (faces_wheel_plan.md 5.4 with its Review R.12): the face / emote lanes' fills, last (also without a player:
   the fills handle that) */
static void pstate_lanes(const struct sa1g_world *w, sa1w_pstate *o)
{
    if (w->face.fill != NULL) {
        w->face.fill(w, o);
    }
    if (w->emote.fill != NULL) {
        w->emote.fill(w, o);
    }
}

void sa1w_world_pstate(const struct sa1g_world *w, sa1w_pstate *o)
{
    sa1g_player_ptrs pp;
    uint8_t twp[0x40], mwp[0x40], pwp[0x200];
    uint32_t k;
    memset(o, 0, sizeof *o);
    /* round 0.9.4a: the face / emote "none" values (the lanes' fills overwrite them) */
    o->face_from = -1;
    o->face_to = -1;
    o->face_letter = -1;
    o->emote_item = -1;
    o->emote_row = -1;
    o->emote_extra = -1;
    if (w == NULL || w->ram == NULL) {
        return;
    }
    o->steps = w->steps;
    sa1g_world_rd32(w, SA1G_ADDR_LAND_COUNT, &o->land_count);
    sa1g_world_rd32(w, SA1G_ADDR_RAND, &o->rand_state);
    sa1g_world_player(w, &pp);
    if (pp.twp == 0 || pp.mwp == 0 || pp.pwp == 0) {
        pstate_lanes(w, o);
        return;
    }
    arena_copy(w, twp, pp.twp, sizeof twp);
    arena_copy(w, mwp, pp.mwp, sizeof mwp);
    arena_copy(w, pwp, pp.pwp, sizeof pwp);
    o->valid = 1;
    o->mode = (int8_t)twp[0];
    o->smode = (int8_t)twp[1];
    o->twp_flag = rd16(twp + 4);
    for (k = 0; k < 3; k++) {
        o->ang[k] = rd32(twp + 0x14 + 4 * k);
        o->pos[k] = rdf(twp + 0x20 + 4 * k);
        o->mspd[k] = rdf(mwp + 0x04 + 4 * k);
        o->macc[k] = rdf(mwp + 0x10 + 4 * k);
        o->ang_aim[k] = rd32(mwp + 0x1C + 4 * k);
        o->pacc[k] = rdf(pwp + 0x2C + 4 * k);
        o->pspd[k] = rdf(pwp + 0x38 + 4 * k);
        o->floor_normal[k] = rdf(pwp + 0x50 + 4 * k);
    }
    o->dotp = rdf(pwp + 0x1C);
    o->attr = rd32(pwp + 0x5C);
    o->last_attr = rd32(pwp + 0x60);
    o->equipment = rd16(pwp + 0x04);
    o->item = rd16(pwp + 0x06);
    o->jumptimer = (int16_t)rd16(pwp + 0x08);
    o->nocontimer = (int16_t)rd16(pwp + 0x0A);
    o->breathtimer = (int16_t)rd16(pwp + 0x0C);
    o->waittimer = (int16_t)rd16(pwp + 0x0E);
    o->pwp_flag = rd16(pwp + 0x10);
    o->reqaction = rd16(pwp + 0x124);
    o->action = rd16(pwp + 0x126);
    o->lastaction = rd16(pwp + 0x128);
    o->nextaction = rd16(pwp + 0x12A);
    o->nframe = rdf(pwp + 0x130);
    o->mtnmode = (int16_t)rd16(pwp + 0x120);    /* round 6a: playerwk.mj.mtnmode */
    o->super_look = w->sg.look;                 /* round 0.9.3a: the General form's look (sa1_world_super.c) */
    /* round 0.9.4a (super2_plan.md 6.1 task 3, review R.8): the table the next pair draws with, and GA's lift */
    o->super_anims = (w->sg.look && w->sg.anims && w->sg.ga != 0) ? 1 : 0;
    o->super_lift = o->super_anims ? w->sg.lift : 0.0f;
    for (k = 0; k < 6; k++) {
        uint32_t j;
        for (j = 0; j < 3; j++) {
            o->attach_pos[k][j] = rdf(pwp + 0x14C + 12 * k + 4 * j);
            o->attach_vec[k][j] = rdf(pwp + 0x194 + 12 * k + 4 * j);
        }
    }
    /* the drawn step (gsa1_vertical_flicker.md fix B): the last pair's sub step 0, when the world's last step was that
       pair's sub step 1 */
    if (w->drawn_valid && w->drawn_step + 1u == w->steps) {
        o->drawn_valid = 1;
        memcpy(o->drawn_pos, w->drawn_pos, sizeof o->drawn_pos);
        memcpy(o->drawn_ang, w->drawn_ang, sizeof o->drawn_ang);
        /* round 0.9.4a: the motion frame / action the character renderer drew (the render hint's start) */
        o->drawn_frame_valid = w->drawn_frame_valid;
        o->drawn_frame = w->drawn_frame;
        o->drawn_action = w->drawn_action;
    }
    pstate_lanes(w, o);
}

uint32_t sa1w_world_events(struct sa1g_world *w, sa1_event *out, uint32_t cap)
{
    return w != NULL ? sa1g_world_poll_events(w, out, cap) : 0u;
}

const char *sa1w_world_fault(const struct sa1g_world *w)
{
    if (w == NULL) {
        return "no world";
    }
    return w->dead ? (w->fault.msg[0] ? w->fault.msg : sa1g_status_string(w->dead_status)) : "";
}
