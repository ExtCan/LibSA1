/*
 * Character instances (create / destroy / pose / tick / state) and asset queries.
 *
 * API 0.2: a character that ticks has its own world (sadc docs/libsa1_integration.md D2, 6): the
 * decompiled game code in a 16 MB image of the Dreamcast's RAM, built and booted by the bridge
 * src/world/sa1_api_world.c (sa1_api_world.h) either at sa1_character_create_ex(SA1_CREATE_WORLD) or at
 * the first sa1_character_tick. A character that is only posed (the puppets of 7.3) never gets one.
 * The state of a ticking character is decoded from the game's own player blobs (7.1).
 *
 * Its mesh is what the game draws (7.2, sa1_capture.h): every sa1_character_tick holds the world's capture window
 * open, so the game's own displays (SonicMain's 8C0AE17A ...) record their draws; after every tick that ran pairs
 * they are emitted (models read live from the arena: welds, morphs, part swaps as the game drew them; effects:
 * sprites, lines, polygons) into a double buffer of draws (render output, SA1_API_RENDER): sa1_character_render
 * interpolates between the last two pairs, mesh_out is the last pair's triangles. The CPU pose evaluator stays the
 * puppet path (sa1_character_pose) and the fallback until a world has captured a pair.
 */
#include "sa1_api_world.h"
#include "sa1_capture.h"
#include "sa1_coladapt.h"
#include "sa1_hostobj.h"
#include "sa1_screen.h"
#include "sa1_preview.h"    /* round 0.9.4b: sa1pv_destroy_all (the object showcases) */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static const char *const k_names[SA1_CHAR_COUNT] = { "sonic", "eggman", "tails", "knuckles", "tikal",
                                                     "amy", "gamma", "big", "supersonic" };

#define SA1_TWO_PI 6.28318530717958647692

const char *sa1_character_name(int32_t kind)
{
    return (kind >= 0 && kind < SA1_CHAR_COUNT) ? k_names[kind] : NULL;
}

int32_t sa1_character_kind_from_name(const char *name)
{
    int32_t k;
    if (!name)
        return -1;
    for (k = 0; k < SA1_CHAR_COUNT; k++)
        if (strcmp(name, k_names[k]) == 0)
            return k;
    if (strcmp(name, "e102") == 0 || strcmp(name, "e-102") == 0)
        return SA1_CHAR_GAMMA;
    if (strcmp(name, "miles") == 0)
        return SA1_CHAR_TAILS;
    if (strcmp(name, "super_sonic") == 0)
        return SA1_CHAR_SUPER_SONIC;
    return -1;
}

static const sa1_charset *loaded_set(const sa1_context *ctx, int32_t kind)
{
    if (!ctx || kind < 0 || kind >= SA1_CHAR_COUNT || !ctx->chars[kind].loaded)
        return NULL;
    return &ctx->chars[kind];
}

/* ------------------------------------------------------------------------- */
/* Queries                                                                   */
/* ------------------------------------------------------------------------- */

sa1_status sa1_character_info_get(const sa1_context *ctx, int32_t kind, sa1_character_info *out)
{
    sa1_character_info info;
    const sa1_charset *cs;
    if (!ctx || !out || out->struct_size < sizeof(uint32_t) || kind < 0 || kind >= SA1_CHAR_COUNT)
        return SA1_ERR_INVALID_ARGUMENT;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.kind = kind;
    info.name = k_names[kind];
    cs = &ctx->chars[kind];
    if (cs->loaded) {
        const sa1_object *mo = &SA1_VEC(cs->objects, sa1_object)[cs->main_object];
        info.loaded = 1;
        info.num_nodes = mo->num_nodes;
        info.num_vertices = mo->drawn_vertices;
        info.num_triangles = mo->drawn_tris;
        info.max_vertices = cs->max_vertices;
        info.max_triangles = cs->max_triangles;
        info.num_actions = cs->num_actions;
        info.num_actions_available = cs->num_available;
        info.num_motions = cs->motions.count;
        info.num_welds = cs->num_welds;
        info.num_archives = cs->num_archives;
        info.main_object_addr = cs->main_object_addr;
    }
    sa1_copy_out(out, &info, sizeof info);
    return cs->loaded ? SA1_OK : SA1_ERR_NOT_LOADED;
}

sa1_status sa1_action_info_get(const sa1_context *ctx, int32_t kind, uint32_t action, sa1_action_info *out)
{
    sa1_action_info info;
    const sa1_charset *cs = loaded_set(ctx, kind);
    const sa1_action *a;
    if (!out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    if (!cs)
        return SA1_ERR_NOT_LOADED;
    if (action >= cs->num_actions)
        return SA1_ERR_NOT_FOUND;
    a = &cs->actions[action];
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.status = a->status;
    info.mode = a->mode;
    info.next = a->next;
    info.blend = a->blend;
    info.speed = a->speed;
    info.action_addr = a->addr;
    info.object_addr = a->object_addr;
    info.motion_addr = a->motion_addr;
    info.image = (a->image >= 0 && (uint32_t)a->image < ctx->num_image_names) ? ctx->image_names[a->image] : "";
    if (a->status == SA1_ACTION_OK) {
        const sa1_motion *m = &SA1_VEC(cs->motions, sa1_motion)[a->motion];
        info.num_frames = m->num_frames;
        info.num_nodes = SA1_VEC(cs->objects, sa1_object)[a->object].num_nodes;
        info.motion_type = m->type;
    }
    sa1_copy_out(out, &info, sizeof info);
    return SA1_OK;
}

sa1_status sa1_physics_get(const sa1_context *ctx, int32_t kind, sa1_physics *out)
{
    const sa1_charset *cs = loaded_set(ctx, kind);
    if (!out)
        return SA1_ERR_INVALID_ARGUMENT;
    if (!cs || !cs->has_physics)
        return SA1_ERR_NOT_LOADED;
    *out = cs->physics;
    return SA1_OK;
}

uint32_t sa1_disc_file_count(const sa1_context *ctx)
{
    return ctx ? ctx->files.count : 0u;
}

sa1_status sa1_disc_file(const sa1_context *ctx, uint32_t index, sa1_disc_file_info *out)
{
    sa1_disc_file_info info;
    const sa1_file_record *r;
    if (!ctx || !out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    if (index >= ctx->files.count)
        return SA1_ERR_NOT_FOUND;
    r = &SA1_VEC(ctx->files, sa1_file_record)[index];
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.name = r->name;
    info.size = r->size;
    info.unpacked_size = r->unpacked_size;
    memcpy(info.sha1, r->sha1, sizeof info.sha1);
    info.hash_status = r->hash_status;
    info.used = r->used;
    info.read_ms = r->read_ms;
    sa1_copy_out(out, &info, sizeof info);
    return SA1_OK;
}

/* ------------------------------------------------------------------------- */
/* Instances                                                                 */
/* ------------------------------------------------------------------------- */

/* The interpolation plan of one drawn model between the last two pairs (render_build): */
#define RN_VERTEX 0                 /* world-space vertex lerp (no transform pair: singular, mirrored on one side) */
#define RN_RIGID  1                 /* its model-local points did not change: the interpolated transform of them */
#define RN_LOCAL  2                 /* they changed (welded, morphed, shape motion): the interpolated transform of the
                                       lerped local points */
typedef struct sa1_rnode {
    sa1_xlerp x;
    int32_t mode;                   /* RN_* */
    int32_t prev;                   /* its draw in the pair before (-1: drawn in the last pair only) */
    /* RN_RIGID / RN_LOCAL (sadc docs/research/gsa1_060_render.md 3.6): what x interpolates. anchor >= 0: the model's
       transform relative to that drawn model's (A^-1 * M; A is interpolated first: the plan's order), a model it is
       rigidly attached to (eyes, irises, pupils on a head); -1: relative to the drawn root's frame (F^-1 * M, F the
       translation sa1_render.position and the turn sa1_render.yaw) */
    int32_t anchor;
    double turn;                    /* the motion's hint of how far the model's node turns (sa1_pose_node_turn), -1 */
    /* round 0.9.1 (sadc docs/research/gsa1_070_tails.md F-T1 with its Review R.4.1, gsa1_070_bird.md Review R.4 F5): */
    uint32_t group;                 /* the drawn models anchored to this one, directly or through others */
    int32_t world;                  /* anchor -1 only: 1 = x interpolates the model -> SA1 world transform itself (a
                                       companion whose motion does not follow the character's turn), 0 = F^-1 * M */
} sa1_rnode;

/* A vertex whose local point changed but which lies on an unchanged vertex of another draw at both pairs (a welded
 * copy: PJoinVertexes writes the source vertex into the destination model): it keeps following that vertex. */
typedef struct sa1_rfollow {
    uint32_t v, r;
    float off0[3], off1[3];         /* v - r at both pairs (host space) */
    int32_t nrm;                    /* the normals agree at both pairs too: v takes r's */
} sa1_rfollow;

/* the render output of a character (sa1_character_render), library-owned */
typedef struct sa1_rout {
    float *pos, *nrm, *uv;          /* 9 / 9 / 6 per triangle */
    uint8_t *col;                   /* 12 per triangle */
    uint32_t cap_t;
    sa1_draw *draws;
    uint32_t cap_d;
    sa1_sprite *spr;
    uint32_t cap_s;
    sa1_line_vertex *lv;
    uint32_t cap_l;
    sa1_render r;
    /* interpolation between the last two pairs */
    float *vpos, *vnrm;             /* 3 per vertex of the last pair */
    uint8_t *vstat;                 /* 1 per vertex: follows its draw's transform exactly (local point unchanged) */
    uint32_t *pidx;                 /* 1 per vertex: the same vertex in the pair before (PIDX_NONE: none) */
    uint32_t cap_v;
    sa1_rnode *plan;                /* 1 per drawn model */
    uint32_t cap_plan;
    uint32_t *order;                /* the matched models in the order they are interpolated (matrix stack depth, stable):
                                       an anchor comes before the models anchored to it */
    uint32_t norder;
    double *nm;                     /* 12 per drawn model: its interpolated model -> SA1 world matrix (render_interp) */
    double *inv;                    /* 24 per drawn model: plan_build's inverses of its matrices in both pairs */
    uint8_t *inv_ok;                /* 1 per drawn model: both inverses exist */
    sa1_rfollow *fol;
    uint32_t nfol, cap_fol;
    uint32_t *hash;
    uint32_t cap_hash;
    uint32_t plan_gen;              /* rgen the plan was made for */
    int32_t plan_ok;
    sa1_lerp_stats stats;
    /* round 0.9.4 (game OBJECT draws, sadc docs/research/gsa1_094_plan.md 6.2 task 3; see obj_match): */
    int32_t *omatch;                /* 1 per drawn model of the last pair (cap_plan): an OBJECT model's draw in the pair
                                       before, matched by place (-1: none) */
    uint8_t *vobj;                  /* 1 per vertex of the last pair (cap_v): it belongs to an SA1_DRAW_OBJECT draw */
    struct sa1_ocand *ocand;        /* match candidates (models in plan_build, sprites in render_build) */
    uint32_t cap_ocand;
    uint8_t *otaken;                /* 1 per model / sprite of the pair before: used by a match */
    uint32_t cap_otaken;
    uint8_t *sobj;                  /* 2 x cap_sobj: OBJECT sprites of the last pair, then of the pair before */
    int32_t *smatch;                /* 1 per sprite of the last pair: its OBJECT sprite in the pair before (-1: none) */
    uint32_t cap_sobj;
    uint32_t nsobj;                 /* OBJECT sprites of the last pair (plan_build; sobj / smatch valid when nonzero) */
    struct sa1_okey *okey;          /* the OBJECT models of the pair before, sorted (obj_match) */
    uint32_t cap_okey;
} sa1_rout;

/* a candidate pair of the R-F1 / R-F3 matching of game OBJECT draws: draw k of the last pair, j of the pair before, d
 * their distance in the SA1 world (units) */
typedef struct sa1_ocand {
    uint32_t k, j;
    double d;
} sa1_ocand;

/* an OBJECT model of the pair before for the R-F1 search: its model, its world x (node_wm[3]) and its index */
typedef struct sa1_okey {
    uint32_t model;
    double x;
    uint32_t j;
} sa1_okey;

/* What the game did between two pairs for the motion hint of the interpolation (sa1_pose_node_turn). */
typedef struct sa1_rinfo {
    int32_t valid;                  /* a steady action (mj.reqaction == mj.action, not linking) */
    int32_t action;
    float frame, spd;               /* mj.nframe, playerwk.spd.x after the pair */
    int32_t dvalid;                 /* df: frames the motion advanced since the previous pair (same steady action) */
    double df;
    /* round 0.9.4a (super2_plan.md 6.1 task 4, review R.1): what the display drew for `action` (general_drawn): eff = the
       action whose mtnmode / racio the drawn motion plays with (a GA row: the game's Super action), dobj / dmot = the
       drawn object and motion (-1: the action's own) */
    int32_t eff;
    int32_t dobj, dmot;
    /* round 0.9.4a (the owner's 0.9.4 note "the run animations break a little bit when moving at extreme speed"): the
       motion frame the character renderer drew - the drawn step's (sa1w_pstate drawn_frame: after the pair's sub step 0)
       when the world latched it for this action, else `frame`. frame_advance measures between drawn frames and the
       hint's stretch of the motion starts at the previous pair's drawn frame: from `frame`, one 60 Hz step later, the
       stretch was off by up to 6.7 frames of Sonic's 33-frame sprint at Super speeds (super_test general_anims_extreme) */
    float dframe;
} sa1_rinfo;

/* tests only (sa1w_test_hint_legacy): pair_info leaves the drawn map out, as 0.9.4 did */
static int g_hint_legacy;

/* An instance: the posing state (sa1_instance, sa1_pose.c) and the character's world. */
typedef struct sa1_char {
    sa1_instance inst;              /* first: ctx->inst[slot] points here */
    uint32_t create_flags;          /* SA1_CREATE_* */
    sa1w_create cw;                 /* how the world is built (SA1 space, BAMS) */
    struct sa1g_world *world;       /* NULL: none (yet) */
    sa1_status world_status;        /* the world could not be built: reported by every tick */
    int32_t world_failed;
    sa1w_pstate last;               /* the state after the last step (kept when the world faults) */
    int32_t have_last;
    sa1_capture *cap;               /* the world's display capture (NULL: none / not attached yet) */
    int32_t cap_failed;             /* the world has no capture block: the mesh is the CPU pose */
    /* render output (SA1_API_RENDER): the draws of the last two pairs */
    sa1_meshbuf rb[2];              /* rb[rcur]: the last pair, rb[rcur ^ 1]: the one before (rhave == 2) */
    int32_t rcur, rhave;            /* rhave: pairs held (0: the CPU pose is the output) */
    float rpos[2][3], ryaw[2];      /* the character's root (host) at each of them: the drawn step's (render_capture) */
    int32_t rdrawn[2];              /* rpos / ryaw are the drawn step's root (else the state after the pair) */
    uint32_t rgen;                  /* sa1_render.generation */
    sa1_rinfo rinfo[2];             /* per rb */
    double acc;                     /* seconds of dt that did not make a whole pair yet (alpha < 0) */
    sa1_meshbuf rpose;              /* draws of the CPU pose geometry */
    sa1_meshbuf rshadow;            /* the blob shadow of a hidden pair, drawn with the CPU pose (rshadow_on) */
    int32_t pose_archive;           /* round 0.9.3a: the archive of the CPU pose's textures (SUPERSONIC for a General
                                       world's Super look, else 0) */
    int32_t rshadow_on;
    sa1_rout rout;                  /* the expanded / interpolated output */
    int32_t start_rings;            /* API 0.3 (SA1_CREATE_START_RINGS): rings set when the world is built, -1 = none */
} sa1_char;

static sa1_char *get_char(const sa1_context *ctx, sa1_character_id id)
{
    uint32_t slot, gen;
    if (!ctx || id <= 0)
        return NULL;
    slot = ((uint32_t)id & 0xFFFFu) - 1u;
    gen = ((uint32_t)id >> 16) & 0x7FFFu;
    if (slot >= SA1_MAX_INSTANCES || !ctx->inst[slot] || ctx->inst_gen[slot] != gen)
        return NULL;
    return (sa1_char *)(void *)ctx->inst[slot];
}

static sa1_instance *get_inst(const sa1_context *ctx, sa1_character_id id)
{
    sa1_char *ch = get_char(ctx, id);
    return ch ? &ch->inst : NULL;
}

/* host yaw (radians, counter-clockwise about the up axis, 0 = SA1 +X) <-> the game's taskwk.ang.y: the
 * game moves along (cos a, 0, sin a) for ang.y = a (BAMS), a clockwise turn seen from +Y, so a = -yaw */
static uint32_t yaw_to_bams(float yaw)
{
    double t = -(double)yaw / SA1_TWO_PI * 65536.0;
    t = floor(t + 0.5);
    t = fmod(t, 65536.0);
    if (t < 0.0)
        t += 65536.0;
    return (uint32_t)t & 0xFFFFu;
}

static float bams_to_yaw(uint32_t a)
{
    return (float)(-(double)(int16_t)(uint16_t)(a & 0xFFFFu) * (SA1_TWO_PI / 65536.0));
}

static void world_destroy(sa1_context *ctx, sa1_char *ch)
{
    /* the capture reads the world's memory: it goes first */
    sa1_capture_free(ctx, ch->cap);
    ch->cap = NULL;
    ch->cap_failed = 0;
    ch->rhave = 0;
    ch->rshadow_on = 0;
#if !defined(SA1_NO_WORLD)
    if (ch->world && sa1_api_ext_of(ctx)->world)
        sa1w_world_destroy(sa1_api_ext_of(ctx)->world, ch->world);
#endif
    ch->world = NULL;
}

static void rout_free(const sa1_context *ctx, sa1_rout *o);

static void char_free(sa1_context *ctx, sa1_char *ch)
{
    world_destroy(ctx, ch);
    /* round 0.9.4: the exec arrays of the capture's emits (render_capture: exec_arrays) first */
    sa1_meshbuf_free_exec(ctx, &ch->rb[0]);
    sa1_meshbuf_free_exec(ctx, &ch->rb[1]);
    sa1_meshbuf_free_exec(ctx, &ch->rshadow);
    sa1_meshbuf_free(ctx, &ch->rb[0]);
    sa1_meshbuf_free(ctx, &ch->rb[1]);
    sa1_meshbuf_free(ctx, &ch->rpose);
    sa1_meshbuf_free(ctx, &ch->rshadow);
    rout_free(ctx, &ch->rout);
    sa1_instance_free(ctx, &ch->inst);
    sa1_mem_free(ctx, ch);
}

/* builds the character's world (once); SA1_PARTIAL: built without land */
static sa1_status ensure_world(sa1_context *ctx, sa1_char *ch, sa1_character_id id)
{
#if defined(SA1_NO_WORLD)
    (void)ch;
    (void)id;
    snprintf(ctx->error, sizeof ctx->error, "this libsa1 build has no world core (SA1_NO_WORLD): no game logic");
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    sa1_api_ext *x = sa1_api_ext_of(ctx);
    uint32_t notes = 0;
    sa1_status st;
    if (ch->world)
        return SA1_OK;
    if (ch->create_flags & SA1_CREATE_PUPPET)
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "character %d is a puppet (SA1_CREATE_PUPPET): it does not tick",
                        (int)id);
    if (ch->world_failed)
        return ch->world_status;
    if (!sa1w_ready()) {
        /* recorded quietly: hosts may call this every frame */
        snprintf(ctx->error, sizeof ctx->error, "sa1_character_tick needs the lowered game code (not in this build)");
        return SA1_ERR_NOT_IMPLEMENTED;
    }
    if (!ctx->disc_loaded || !x->gdi[0])
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "no disc loaded: a world is built from the disc of sa1_load_disc");
    {
        struct sa1w_ctx *wc = NULL;
        st = sa1w_ctx_get(ctx, &wc);    /* under the table lock: sa1_character_prepare may create it on another thread */
        if (st != SA1_OK)
            return sa1_fail(ctx, st, "world: cannot set up the worlds of this context (collision segment, %s)",
                            sa1_status_string(st));
    }
    st = sa1w_world_create(x->world, &ch->cw, &ch->world, &notes);
    if (st == SA1_OK) {
        /* API 0.3: the host-object hooks and the start rings (sa1_world_hostobj.c), before the world's first step */
        st = sa1ho_world_attach(ctx, ch->world, ch->start_rings);
        if (st == SA1_OK) {
            /* round 0.9.1 HUD (src/sa1_screen.c): the HUD parts, clock and time the host gave before the world was built */
            st = sa1_screen_world_attach(ctx, id, ch->world);
        }
        if (st != SA1_OK) {
            sa1_fail(ctx, st, "character %d: the start rings: %s", (int)id, sa1w_world_fault(ch->world));
            sa1w_world_destroy(x->world, ch->world);
        }
    }
    if (st != SA1_OK) {
        ch->world = NULL;
        ch->world_failed = 1;
        ch->world_status = st;
        return st;
    }
    if (notes & SA1W_NOTE_GRID_PENDING)
        return sa1_fail(ctx, SA1_PARTIAL, "character %d: the static collision is GRID geometry, whose synthesis is "
                        "not implemented yet: the world has no land", (int)id);
    if (notes & SA1W_NOTE_NO_GEOMETRY)
        return sa1_fail(ctx, SA1_PARTIAL, "character %d: no static collision was set: the world has no land", (int)id);
    return SA1_OK;
#endif
}

sa1_status sa1_character_create_ex(sa1_context *ctx, const sa1_character_desc *desc_in, sa1_character_id *out_id)
{
    static const uint32_t known = SA1_CREATE_WORLD | SA1_CREATE_PUPPET | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES |
                                  SA1_CREATE_START_TABLE | SA1_CREATE_STAGE | SA1_CREATE_SEED | SA1_CREATE_UPGRADES |
                                  SA1_CREATE_STAGE_TASKS | SA1_CREATE_START_RINGS | SA1_CREATE_SUPER |
                                  SA1_CREATE_SUPER_CAPABLE | SA1_CREATE_WATER_FLOOR;
    sa1_character_desc d;
    const sa1_charset *cs;
    sa1_char *ch;
    uint32_t slot, k;
    sa1_character_id id;
    sa1_status st;
    if (!ctx || !out_id || !desc_in || desc_in->struct_size < offsetof(sa1_character_desc, position))
        return SA1_ERR_INVALID_ARGUMENT;
    *out_id = 0;
    memset(&d, 0, sizeof d);
    memcpy(&d, desc_in, desc_in->struct_size < sizeof d ? desc_in->struct_size : sizeof d);
    if (d.kind < 0 || d.kind >= SA1_CHAR_COUNT)
        return SA1_ERR_INVALID_ARGUMENT;
    if (d.flags & ~known)
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "unknown character flags 0x%X", (unsigned)(d.flags & ~known));
    if ((d.flags & SA1_CREATE_PUPPET) && (d.flags & SA1_CREATE_WORLD))
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "a puppet has no world (SA1_CREATE_PUPPET with SA1_CREATE_WORLD)");
    if (!(sa1_finite(d.position[0]) && sa1_finite(d.position[1]) && sa1_finite(d.position[2])) || !sa1_finite(d.yaw))
        return SA1_ERR_INVALID_ARGUMENT;
    if ((d.flags & SA1_CREATE_STAGE) && (d.level < 0 || d.level > 42 || d.act < 0 || d.act > 7))
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "stage %d act %d does not exist (level 0-42, act 0-7)",
                        (int)d.level, (int)d.act);
    if ((d.flags & SA1_CREATE_START_RINGS) &&
        desc_in->struct_size < offsetof(sa1_character_desc, start_rings) + sizeof d.start_rings)
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_CREATE_START_RINGS: sa1_character_desc.struct_size %u does not "
                        "cover start_rings", (unsigned)desc_in->struct_size);
    /* round 6a (Super Sonic, API 0.4) */
    if ((d.flags & SA1_CREATE_SUPER) &&
        desc_in->struct_size < offsetof(sa1_character_desc, super_flags) + sizeof d.super_flags)
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_CREATE_SUPER: sa1_character_desc.struct_size %u does not cover "
                        "super_flags", (unsigned)desc_in->struct_size);
    if ((d.flags & SA1_CREATE_SUPER) && (d.super_flags & ~(uint32_t)SA1_SUPER_KNOWN_FLAGS))
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "unknown super_flags 0x%X",
                        (unsigned)(d.super_flags & ~(uint32_t)SA1_SUPER_KNOWN_FLAGS));
    if ((d.flags & SA1_CREATE_SUPER_CAPABLE) && d.kind != SA1_CHAR_SONIC && d.kind != SA1_CHAR_SUPER_SONIC)
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_CREATE_SUPER_CAPABLE: only Sonic turns Super");
    if ((d.flags & SA1_CREATE_SUPER) && !(d.flags & SA1_CREATE_SUPER_CAPABLE) && d.kind != SA1_CHAR_SUPER_SONIC)
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_CREATE_SUPER: super_flags are for Super Sonic and "
                        "SA1_CREATE_SUPER_CAPABLE");
    /* round 0.9.3a (the General form; sadc docs/research/super_general_design.md 4.1 / 5 with its Review RC1) */
    {
        uint32_t sf = (d.flags & SA1_CREATE_SUPER) ? d.super_flags : 0u;
        int general = (sf & SA1_SUPER_GENERAL) != 0;
        if (general && (d.kind != SA1_CHAR_SONIC || !(d.flags & SA1_CREATE_SUPER_CAPABLE)))
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_SUPER_GENERAL: the General form is a Sonic (kind 0) created "
                            "with SA1_CREATE_SUPER_CAPABLE (kind %d)", (int)d.kind);
        if ((sf & SA1_SUPER_START) && !general)
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_SUPER_START goes with SA1_SUPER_GENERAL");
        if ((sf & SA1_SUPER_START_NORMAL) && d.kind != SA1_CHAR_SUPER_SONIC)
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_SUPER_START_NORMAL is for Super Sonic (kind 8) only");
        if (general && (d.flags & SA1_CREATE_STAGE_TASKS))
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_SUPER_GENERAL with SA1_CREATE_STAGE_TASKS: the stage "
                            "overlay and B_CHAOS7 share 0x0C900000");
        if (!general && (d.flags & SA1_CREATE_STAGE) &&
            (d.kind == SA1_CHAR_SUPER_SONIC || (d.flags & SA1_CREATE_SUPER_CAPABLE)) && (d.level != 19 || d.act != 0))
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "Super Sonic's world is the Perfect Chaos stage (level 19 act "
                            "0), not stage %d act %d (a General world takes any stage)", (int)d.level, (int)d.act);
    }
    cs = loaded_set(ctx, d.kind);
    if (!cs)
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "character %s is not loaded", k_names[d.kind]);
    if ((d.flags & SA1_CREATE_SUPER_CAPABLE) && d.kind == SA1_CHAR_SONIC) {
        /* round 6a: his Super body's textures (sa1_loader.c load_other_body) */
        for (k = 0; k < cs->num_archives && strcmp(cs->archives[k].name, "SUPERSONIC") != 0; k++)
            ;
        if (k == cs->num_archives)
            return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SUPER_CAPABLE: Sonic was loaded without his Super form's "
                            "textures (sa1_load_disc with SA1_LOAD_SUPER_FORMS and SA1_LOAD_SUPER)");
    }
    for (slot = 0; slot < SA1_MAX_INSTANCES && ctx->inst[slot]; slot++)
        ;
    if (slot == SA1_MAX_INSTANCES)
        return sa1_fail(ctx, SA1_ERR_LIMIT, "too many characters (max %u)", (unsigned)SA1_MAX_INSTANCES);
    ch = (sa1_char *)sa1_mem_alloc(ctx, sizeof *ch);
    if (!ch)
        return SA1_ERR_OUT_OF_MEMORY;
    ch->inst.kind = d.kind;
    ch->create_flags = d.flags;
    ch->start_rings = (d.flags & SA1_CREATE_START_RINGS) ? (d.start_rings < 0 ? 0 : d.start_rings) : -1;
    if (!(d.flags & SA1_CREATE_START_RINGS) && d.kind == SA1_CHAR_SUPER_SONIC)
        ch->start_rings = 50;   /* round 6a: Perfect Chaos starts with 50 (the boss: 8C08A074, 8C08A198(50)) */
    /* the world's start (SA1 space, BAMS) and the posing state (host space) */
    ch->cw.kind = d.kind;
    ch->cw.level = (d.flags & SA1_CREATE_STAGE) ? d.level : -1;     /* -1: the bridge's default */
    ch->cw.act = (d.flags & SA1_CREATE_STAGE) ? d.act : -1;
    ch->cw.seed = (d.flags & SA1_CREATE_SEED) ? d.seed : 0xDEAD0CABu;
    if (d.flags & SA1_CREATE_UPGRADES)
        memcpy(ch->cw.event_flags, d.upgrades, sizeof ch->cw.event_flags);
    if (d.flags & SA1_CREATE_START_TABLE)
        ch->cw.flags |= SA1W_START_TABLE;
    if (d.flags & SA1_CREATE_STAGE_TASKS)
        ch->cw.flags |= SA1W_STAGE_TASKS;
    if (d.flags & SA1_CREATE_SUPER_CAPABLE)
        ch->cw.flags |= SA1W_SUPER_CAPABLE;
    if (d.flags & SA1_CREATE_SUPER) {
        ch->cw.flags |= SA1W_SUPER_GIVEN;
        ch->cw.super_flags = d.super_flags;
    }
    if (d.flags & SA1_CREATE_WATER_FLOOR)
        ch->cw.flags |= SA1W_WATER_FLOOR;
    if (d.flags & SA1_CREATE_SA1_SPACE) {
        memcpy(ch->cw.pos, d.position, sizeof ch->cw.pos);
        sa1_to_host_point(ctx, d.position, ch->inst.position);
    } else {
        sa1_from_host_point(ctx, d.position, ch->cw.pos);
        memcpy(ch->inst.position, d.position, sizeof ch->inst.position);
    }
    if (d.flags & SA1_CREATE_ANGLES) {
        for (k = 0; k < 3; k++)
            ch->cw.ang[k] = (uint32_t)d.angle[k];
        ch->inst.yaw = bams_to_yaw(ch->cw.ang[1]);
    } else {
        ch->cw.ang[1] = yaw_to_bams(d.yaw);
        ch->inst.yaw = d.yaw;
    }
    ch->inst.action = cs->num_available ? 0 : -1;
    st = sa1_instance_alloc(ctx, cs, &ch->inst);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, ch);
        return st;
    }
    ch->inst.geom.action = -1;
    ctx->inst[slot] = &ch->inst;
    ctx->inst_gen[slot] = (uint16_t)((ctx->inst_gen[slot] + 1u) & 0x7FFFu);
    id = (sa1_character_id)(((uint32_t)ctx->inst_gen[slot] << 16) | (slot + 1u));
    st = SA1_OK;
    if (d.flags & SA1_CREATE_WORLD) {
        st = ensure_world(ctx, ch, id);
        if (SA1_FAILED(st)) {
            ctx->inst[slot] = NULL;
            char_free(ctx, ch);
            return st;
        }
    }
    *out_id = id;
    return st;
}

sa1_status sa1_character_create(sa1_context *ctx, int32_t kind, const float position[3], float yaw,
                                sa1_character_id *out_id)
{
    sa1_character_desc d;
    if (!ctx || !out_id || kind < 0 || kind >= SA1_CHAR_COUNT)
        return SA1_ERR_INVALID_ARGUMENT;
    *out_id = 0;
    if (!loaded_set(ctx, kind))
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "character %s is not loaded", k_names[kind]);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.yaw = yaw;
    if (position)
        memcpy(d.position, position, sizeof d.position);
    else
        d.flags |= SA1_CREATE_START_TABLE;
    return sa1_character_create_ex(ctx, &d, out_id);
}

/* the character kinds whose worlds tick (sa1g_character_main: Sonic, Tails, Knuckles, Amy, E-102, Big; round 6a: Super
 * Sonic, whose world is Sonic's) */
#define SA1_TICKING_KINDS ((1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | \
                           (1u << SA1_CHAR_AMY) | (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG) | \
                           (1u << SA1_CHAR_SUPER_SONIC))

sa1_status sa1_character_prepare(sa1_context *ctx, uint32_t kinds)
{
#if defined(SA1_NO_WORLD)
    (void)kinds;
    return ctx ? SA1_ERR_NOT_IMPLEMENTED : SA1_ERR_INVALID_ARGUMENT;
#else
    sa1_api_ext *x;
    if (!ctx || (kinds & ~(uint32_t)SA1_CHAR_MASK_ALL))
        return SA1_ERR_INVALID_ARGUMENT;
    if (!sa1w_ready())
        return SA1_ERR_NOT_IMPLEMENTED;
    x = sa1_api_ext_of(ctx);
    if (!ctx->disc_loaded || !x->gdi[0])
        return SA1_ERR_NOT_LOADED;
    /* no sa1_fail / sa1_log: any thread (libsa1.h, "Preparation on host threads") */
    return sa1w_prepare_images(ctx, (kinds ? kinds : SA1_CHAR_MASK_ALL) & SA1_TICKING_KINDS);
#endif
}

sa1_status sa1_character_destroy(sa1_context *ctx, sa1_character_id id)
{
    sa1_char *ch = get_char(ctx, id);
    uint32_t slot;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    slot = ((uint32_t)id & 0xFFFFu) - 1u;
    ctx->inst[slot] = NULL;
    char_free(ctx, ch);
    return SA1_OK;
}

void sa1_characters_destroy_all(sa1_context *ctx)
{
    sa1_api_ext *x = sa1_api_ext_of(ctx);
    uint32_t i;
    for (i = 0; i < SA1_MAX_INSTANCES; i++) {
        if (ctx->inst[i]) {
            sa1_char *ch = (sa1_char *)(void *)ctx->inst[i];
            ctx->inst[i] = NULL;
            char_free(ctx, ch);
        }
    }
#if !defined(SA1_NO_WORLD)
    /* round 0.9.4b (sadc docs/research/objects2_plan.md 6.2 task 1, review R.2): the object showcases (src/sa1_preview.c)
     * were made from the image cache and shared segments that sa1w_ctx_destroy frees below: they go first */
    sa1pv_destroy_all(ctx);
#endif
    sa1_screen_free_all(ctx);   /* round 0.9.1 HUD: the screen settings and outputs of the characters (src/sa1_screen.c) */
    /* the worlds' images come from the disc that is being unloaded: the next world composes them from the
     * disc of the next sa1_load_disc, whose .gdi is the next file opened */
#if !defined(SA1_NO_WORLD)
    if (x->world)
        sa1w_ctx_destroy(x->world);
#endif
    x->world = NULL;
    x->gdi[0] = 0;
    x->gdi_capture = 1;
}

sa1_status sa1_debug_near_polys(sa1_context *ctx, sa1_character_id id, sa1_near_info *info, sa1_near_poly *out,
                                uint32_t capacity, uint32_t *count)
{
    sa1_char *ch = get_char(ctx, id);
    if (count)
        *count = 0;
    if (!ch || (info && info->struct_size < sizeof(uint32_t)) || (out && capacity == 0))
        return ch ? SA1_ERR_INVALID_ARGUMENT : SA1_ERR_NOT_FOUND;
#if defined(SA1_NO_WORLD)
    (void)info;
    (void)out;
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    if (!ch->world)
        return SA1_ERR_NOT_LOADED;
    return sa1w_world_near(ctx, ch->world, info, out, capacity, count);
#endif
}

struct sa1g_world *sa1_character_world(const sa1_context *ctx, sa1_character_id id)
{
    sa1_char *ch = get_char(ctx, id);
    return ch ? ch->world : NULL;
}

sa1_capture *sa1_character_capture(const sa1_context *ctx, sa1_character_id id)
{
    sa1_char *ch = get_char(ctx, id);
    return ch ? ch->cap : NULL;
}

/* ------------------------------------------------------------------------- */
/* State (design 7.1)                                                        */
/* ------------------------------------------------------------------------- */

/* the game's COL attribute bits -> SA1_SURFACE_* (the inverse of the adapter's map, design 5.6) */
static uint32_t surface_of_attr(uint32_t attr)
{
    static const uint32_t kinds[] = { SA1_SURFACE_SOLID, SA1_SURFACE_WATER, SA1_SURFACE_NO_CLIMB, SA1_SURFACE_HURT,
                                      SA1_SURFACE_SLIPPERY, SA1_SURFACE_HEAVY_WATER, SA1_SURFACE_SAND,
                                      SA1_SURFACE_BOOST, SA1_SURFACE_SLIDE, SA1_SURFACE_STAIRS, SA1_SURFACE_NO_LAND,
                                      SA1_SURFACE_WALL_SLIDE, SA1_SURFACE_DIG,
                                      /* round 0.9.2 (sadc docs/research/gsa1_090_surfaces.md F1) */
                                      SA1_SURFACE_SNOW, SA1_SURFACE_SPEED_PATH, SA1_SURFACE_FOOTPRINTS };
    uint32_t s = 0, i;
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        uint32_t col = sa1ca_surface_to_col(kinds[i]);
        if (col && (attr & col) == col)
            s |= kinds[i];
    }
    return s;
}

/* the player state of a character: live from its world, or the last one before the world faulted */
static int pstate_of(const sa1_char *ch, sa1w_pstate *p, int *faulted)
{
    *faulted = 0;
#if !defined(SA1_NO_WORLD)
    if (ch->world && !sa1w_world_fault(ch->world)[0]) {
        sa1w_world_pstate(ch->world, p);
        return p->valid;
    }
    *faulted = ch->world != NULL;
#endif
    *p = ch->last;
    return ch->have_last && p->valid;
}

/* API 0.3 tail (host objects): rings, lives, score, invulnerability, steps since the last hurt; from the world as soon as
 * it exists (the start rings are set before its first step, when player 0 has no playerwk yet) */
static void state_tail(const sa1_char *ch, sa1_state_ex *x)
{
#if !defined(SA1_NO_WORLD)
    if (ch->world) {
        sa1ho_tail t;
        sa1ho_world_tail(ch->world, &t);
        x->rings = t.rings;
        x->lives = t.lives;
        x->score = t.score;
        x->invulnerable_steps = t.invulnerable_steps;
        x->hurt_steps = t.hurt_steps;
        x->held_object = t.held_object;     /* round 5 */
        sa1_screen_state_tail(ch->world, x);    /* round 0.9.1 HUD: the game clock (src/sa1_screen.c) */
    }
#else
    (void)ch;
    (void)x;
#endif
}

/* round 0.9.4a (faces_wheel_plan.md 5.4, review R.12): the twelve face / emote fields - from the world's state (the live
 * one, or the last one before a fault), else the "none" values */
static void state_lanes(sa1_state_ex *x, const sa1w_pstate *p)
{
    x->face_mode = p ? p->face_mode : 0u;
    x->face_shown = p ? p->face_shown : 0u;
    x->face_from = p ? p->face_from : -1;
    x->face_to = p ? p->face_to : -1;
    x->face_rate = p ? p->face_rate : 0.0f;
    x->face_letter = p ? p->face_letter : -1;
    x->face_activity = p ? p->face_activity : 0u;
    x->emote_state = p ? p->emote_state : 0u;
    x->emote_item = p ? p->emote_item : -1;
    x->emote_row = p ? p->emote_row : -1;
    x->emote_extra = p ? p->emote_extra : -1;
    x->emote_frame = p ? p->emote_frame : 0.0f;
}

static void state_of(const sa1_context *ctx, const sa1_char *ch, sa1_state_ex *x)
{
    sa1w_pstate ps;
    const sa1w_pstate *p = &ps;
    float v[3];
    uint32_t k;
    int faulted, live;
    memset(x, 0, sizeof *x);
    x->struct_size = sizeof *x;
    state_tail(ch, x);
    live = pstate_of(ch, &ps, &faulted);
    /* ps came from a world (live, or the last state before a fault) also when it has no player: its lane fields count */
    state_lanes(x, (ch->world != NULL && !faulted) || ch->have_last ? &ps : NULL);
    if (!live) {
        /* no world (yet): where create / the last pose put the character */
        memcpy(x->position, ch->inst.position, sizeof x->position);
        x->yaw = ch->inst.yaw;
        x->action = ch->inst.action;
        x->frame = ch->inst.frame;
        x->flags = (ch->world ? SA1_STATE_TICKING : 0u) | (faulted ? SA1_STATE_FAULTED : 0u);
        return;
    }
    sa1_to_host_point(ctx, p->pos, x->position);
    for (k = 0; k < 3; k++)
        v[k] = p->mspd[k] * 60.0f;
    sa1_to_host_point(ctx, v, x->velocity);
    x->yaw = bams_to_yaw(p->ang[1]);
    x->action = (int32_t)p->action;
    x->frame = p->nframe;
    x->flags = SA1_STATE_TICKING | ((p->twp_flag & 3u) ? SA1_STATE_ON_GROUND : 0u) | (faulted ? SA1_STATE_FAULTED : 0u) |
               ((p->equipment & 0x8000u) ? SA1_STATE_SUPER : 0u);
    /* round 0.9.5 (SA1_API_BOARD): player 0 in a board mode (Sonic 62-68, Tails 48-54) */
    if ((ch->inst.kind == SA1_CHAR_SONIC && p->mode >= 62 && p->mode <= 68) ||
        (ch->inst.kind == SA1_CHAR_TAILS && p->mode >= 48 && p->mode <= 54))
        x->flags |= SA1_STATE_ON_BOARD;
    x->surface = surface_of_attr(p->attr);
    x->step = (uint32_t)p->steps;
    x->mode = p->mode;
    x->smode = p->smode;
    x->game_flags = p->twp_flag;
    x->player_flags = p->pwp_flag;
    for (k = 0; k < 3; k++) {
        x->angle[k] = (int32_t)p->ang[k];
        x->angle_aim[k] = (int32_t)p->ang_aim[k];
    }
    memcpy(x->sa1_position, p->pos, sizeof x->sa1_position);
    memcpy(x->speed, p->mspd, sizeof x->speed);
    memcpy(x->accel, p->macc, sizeof x->accel);
    memcpy(x->player_speed, p->pspd, sizeof x->player_speed);
    memcpy(x->player_accel, p->pacc, sizeof x->player_accel);
    memcpy(x->floor_normal, p->floor_normal, sizeof x->floor_normal);
    x->dotp = p->dotp;
    x->attr = p->attr;
    x->last_attr = p->last_attr;
    x->jump_timer = p->jumptimer;
    x->nocon_timer = p->nocontimer;
    x->breath_timer = p->breathtimer;
    x->wait_timer = p->waittimer;
    x->req_action = (int32_t)p->reqaction;
    x->last_action = (int32_t)p->lastaction;
    x->next_action = (int32_t)p->nextaction;
    x->equipment = p->equipment;
    x->item = p->item;
    x->land_count = p->land_count;
    x->rand_state = p->rand_state;
    for (k = 0; k < SA1_ATTACH_COUNT; k++) {
        const float *ap = p->attach_pos[k], *av = p->attach_vec[k];
        if (ap[0] != 0.0f || ap[1] != 0.0f || ap[2] != 0.0f)
            x->attach_valid |= 1u << k;
        sa1_to_host_point(ctx, ap, x->attach[k]);
        sa1_to_host_dir(ctx, av, x->attach_dir[k]);
    }
#if !defined(SA1_NO_WORLD)
    /* round 0.9.2 (WTR-LOG): the game's water probe result, from a live world only (0 after a fault) */
    if (!faulted && ch->world) {
        uint32_t wattr = 0;
        float wy = 0.0f, up[3] = { 0.0f, 1.0f, 0.0f }, hu[3], pt[3] = { 0.0f, 0.0f, 0.0f }, hp[3];
        if (sa1ca_world_player_water(ch->world, &wattr, &wy)) {
            pt[1] = wy;
            sa1_to_host_dir(ctx, up, hu);       /* the host's image of SA1 +Y (unit) */
            sa1_to_host_point(ctx, pt, hp);     /* scale * y_top along it */
            x->water_attr = wattr;
            x->water_level = hp[0] * hu[0] + hp[1] * hu[1] + hp[2] * hu[2];
        }
    }
#endif
}

/* the state into a caller struct of either type (struct_size decides how much) */
static void state_out(const sa1_context *ctx, const sa1_char *ch, void *out)
{
    sa1_state_ex x;
    uint32_t size;
    memcpy(&size, out, sizeof size);
    if (size < sizeof(uint32_t))
        return;
    state_of(ctx, ch, &x);
    sa1_copy_out(out, &x, sizeof x);
}

sa1_status sa1_character_state(const sa1_context *ctx, sa1_character_id id, sa1_state *out)
{
    const sa1_char *ch = get_char(ctx, id);
    if (!out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    /* sa1_state is the head of sa1_state_ex (same fields, same order) */
    state_out(ctx, ch, out);
    return SA1_OK;
}

sa1_status sa1_character_state_ex(const sa1_context *ctx, sa1_character_id id, sa1_state_ex *out)
{
    const sa1_char *ch = get_char(ctx, id);
    if (!out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    state_out(ctx, ch, out);
    return SA1_OK;
}

/* API 0.3 (host objects): rings through the game's own 8C08A198 inside the character's world (built if needed) */
sa1_status sa1_character_add_rings(sa1_context *ctx, sa1_character_id id, int32_t n)
{
    sa1_char *ch = get_char(ctx, id);
    sa1_status st;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    st = ensure_world(ctx, ch, id);
    if (SA1_FAILED(st))
        return st;
#if defined(SA1_NO_WORLD)
    (void)n;
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    st = sa1ho_world_add_rings(ch->world, n);
    if (st != SA1_OK)
        return sa1_fail(ctx, st, "character %d: sa1_character_add_rings: %s", (int)id, sa1w_world_fault(ch->world));
    return SA1_OK;
#endif
}

/* round 6a (Super Sonic, API 0.4): the game's request 46 / 47 to player 0 of a super-capable world
 * (src/world/sa1_world_super.c) */
sa1_status sa1_character_order(sa1_context *ctx, sa1_character_id id, int32_t order)
{
    sa1_char *ch = get_char(ctx, id);
    sa1_status st;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    if (order != SA1_ORDER_SUPER && order != SA1_ORDER_NORMAL)
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "sa1_character_order: order %d is not SA1_ORDER_SUPER / NORMAL",
                        (int)order);
    if (ch->inst.kind != SA1_CHAR_SUPER_SONIC && !(ch->create_flags & SA1_CREATE_SUPER_CAPABLE))
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "character %d: only Super Sonic and a Sonic created with "
                        "SA1_CREATE_SUPER_CAPABLE take Super orders", (int)id);
    st = ensure_world(ctx, ch, id);
    if (SA1_FAILED(st))
        return st;
#if defined(SA1_NO_WORLD)
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    st = sa1w_world_order(ch->world, order);
    if (st != SA1_OK)
        return sa1_fail(ctx, st, "character %d: sa1_character_order(%d): %s", (int)id, (int)order,
                        st == SA1_ERR_NOT_LOADED ? "the world has no player" : sa1w_world_fault(ch->world));
    return SA1_OK;
#endif
}

/* round 0.9.3a (include/libsa1.h "THE GENERAL FORM"; sadc docs/research/super_general_design.md 5): the host's Super options,
 * kept by character slot with the other per-character settings (sa1_api_ext.charopt) and handed to the world at once */
sa1_status sa1_character_set_super_option(sa1_context *ctx, sa1_character_id id, uint32_t option, int32_t value)
{
    sa1_char *ch = get_char(ctx, id);
    uint32_t slot = ((uint32_t)id & 0xFFFFu) - 1u, sf;
    int capable, general;
    sa1w_charopt *o;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    if (option < SA1_SUPOPT_SPEED || option > SA1_SUPOPT_LSD_GOLD)
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_character_set_super_option: unknown option %u", (unsigned)option);
    /* round 0.9.4a (super2_plan.md 6.1 task 1c): SUPER_EFFECTS takes the SA1_SUPER_EFFECTS_* bits 0..3; SUPER_ANIMS and
       LSD_GOLD 0 / 1 (General worlds, as every option but the voice) */
    if ((option == SA1_SUPOPT_SPEED && (value < SA1_SUPER_SPEED_GAME || value > SA1_SUPER_SPEED_SONIC)) ||
        ((option == SA1_SUPOPT_NO_AURA || option == SA1_SUPOPT_SUPER_ANIMS || option == SA1_SUPOPT_LSD_GOLD) &&
         (value < 0 || value > 1)) ||
        (option == SA1_SUPOPT_SUPER_EFFECTS &&
         (value < 0 || value > (int32_t)(SA1_SUPER_EFFECTS_CLONES | SA1_SUPER_EFFECTS_DUST))) ||
        (option == SA1_SUPOPT_VOICE && (value < 0 || value > 2047)))
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_character_set_super_option: option %u, value %d out of range",
                        (unsigned)option, (int)value);
    sf = (ch->cw.flags & SA1W_SUPER_GIVEN) ? ch->cw.super_flags : 0u;
    capable = ch->inst.kind == SA1_CHAR_SUPER_SONIC || (ch->create_flags & SA1_CREATE_SUPER_CAPABLE);
    general = ch->inst.kind == SA1_CHAR_SONIC && (ch->create_flags & SA1_CREATE_SUPER_CAPABLE) && (sf & SA1_SUPER_GENERAL);
    if (!(ch->create_flags & SA1_CREATE_PUPPET)) {
        if (option == SA1_SUPOPT_VOICE && !capable)
            return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "character %d: SA1_SUPOPT_VOICE is for a super-capable world", (int)id);
        if (option != SA1_SUPOPT_VOICE && !general)
            return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "character %d: Super option %u is for the General form "
                            "(SA1_SUPER_GENERAL)", (int)id, (unsigned)option);
    }
    o = &sa1_api_ext_of(ctx)->charopt[slot];
    if (o->id != id) {
        memset(o, 0, sizeof *o);
        o->id = id;
    }
    o->supopts |= 1u << option;
    o->supopt_value[option] = value;
    o->set |= SA1W_SET_SUPOPTS;
#if defined(SA1_NO_WORLD)
    return SA1_OK;
#else
    if (ch->world) {
        sa1_status st = sa1w_world_supopt(ch->world, option, value);
        if (st == SA1_ERR_NOT_IMPLEMENTED)
            return sa1_fail(ctx, st, "character %d: Super option %u: the lowered game code of this build has no "
                            "SA1_SUPERDUST block (sadc src/1st_read/code_8C0B4040.c)", (int)id, (unsigned)option);
        return st;
    }
    return SA1_OK;
#endif
}

/* round 0.9.4a (sa1_api_world.h; faces_wheel_plan.md 5.4 with its Review R.12): the character's world for the face / emote
 * lanes, built if needed. SA1_PARTIAL (built without land) is success with *out set */
sa1_status sa1w_character_world_ensure(sa1_context *ctx, sa1_character_id id, struct sa1g_world **out)
{
    sa1_char *ch = get_char(ctx, id);
    if (out != NULL)
        *out = NULL;
    if (ctx == NULL || out == NULL)
        return SA1_ERR_INVALID_ARGUMENT;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    if (ch->create_flags & SA1_CREATE_PUPPET)
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "character %d is a puppet (SA1_CREATE_PUPPET): it has no world", (int)id);
#if defined(SA1_NO_WORLD)
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no game logic");
#else
    {
        sa1_status st = ensure_world(ctx, ch, id);
        if (SA1_FAILED(st) || ch->world == NULL)
            return SA1_FAILED(st) ? st : SA1_ERR_NOT_LOADED;
    }
    *out = ch->world;
    return SA1_OK;
#endif
}

/* round 0.9.5 (src/sa1_board.c): kind, create flags and world, nothing built */
sa1_status sa1w_character_peek(const sa1_context *ctx, sa1_character_id id, int32_t *kind, uint32_t *create_flags,
                               struct sa1g_world **world)
{
    const sa1_char *ch = get_char(ctx, id);
    if (kind != NULL)
        *kind = -1;
    if (create_flags != NULL)
        *create_flags = 0;
    if (world != NULL)
        *world = NULL;
    if (ctx == NULL)
        return SA1_ERR_INVALID_ARGUMENT;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    if (kind != NULL)
        *kind = ch->inst.kind;
    if (create_flags != NULL)
        *create_flags = ch->create_flags;
#if !defined(SA1_NO_WORLD)
    if (world != NULL)
        *world = ch->world;
#endif
    return SA1_OK;
}

/* sa1_state's fields must stay the head of sa1_state_ex */
typedef char sa1_state_head_check[(offsetof(sa1_state_ex, surface) == offsetof(sa1_state, surface) &&
                                   offsetof(sa1_state_ex, flags) == offsetof(sa1_state, flags) &&
                                   sizeof(sa1_state) == offsetof(sa1_state_ex, step)) ? 1 : -1];

/* ------------------------------------------------------------------------- */
/* Tick and pose                                                             */
/* ------------------------------------------------------------------------- */

/* Triangles + draws as a caller-owned sa1_mesh. The triangles of a SA1_PRIM_TRIANGLES draw are contiguous and the
 * draws are in order. idx != NULL: pos / nrm per vertex, else per corner (9 floats per triangle). A mesh without the
 * render streams (struct_size before them, or archive == NULL) gets only the triangles of draws that are not effects
 * (the v0.1 / v0.2 mesh_out: the character's own textures, archive 0). */
typedef struct mesh_src {
    const uint32_t *idx;
    const float *pos, *nrm;
    const float *uv;
    const uint8_t *col;
    const sa1_draw *draws;
    uint32_t ndraw;
} mesh_src;

#define MESH_V1_SIZE offsetof(sa1_mesh, archive)

static sa1_status write_mesh_src(const mesh_src *s, sa1_mesh *m)
{
    uint32_t need = 0, d, o = 0, k;
    int full;
    if (m->struct_size < MESH_V1_SIZE)
        return SA1_ERR_INVALID_ARGUMENT;
    full = m->struct_size >= offsetof(sa1_mesh, blend) + sizeof(m->blend) && m->archive != NULL;
    for (d = 0; d < s->ndraw; d++) {
        const sa1_draw *dr = &s->draws[d];
        if (dr->prim == SA1_PRIM_TRIANGLES && (full || !(dr->flags & SA1_DRAW_EFFECT)))
            need += dr->count;
    }
    m->triangles_needed = need;
    m->num_triangles = 0;
    if (need > m->max_triangles)
        return SA1_ERR_BUFFER_TOO_SMALL;
    for (d = 0; d < s->ndraw; d++) {
        const sa1_draw *dr = &s->draws[d];
        uint32_t t;
        if (dr->prim != SA1_PRIM_TRIANGLES || (!full && (dr->flags & SA1_DRAW_EFFECT)))
            continue;
        for (t = dr->first; t < dr->first + dr->count; t++, o++) {
            for (k = 0; k < 3; k++) {
                const float *p, *n;
                if (s->idx) {
                    uint32_t v = s->idx[3u * t + k];
                    p = s->pos + 3u * v;
                    n = s->nrm + 3u * v;
                } else {
                    p = s->pos + 9u * t + 3u * k;
                    n = s->nrm + 9u * t + 3u * k;
                }
                if (m->position)
                    memcpy(m->position + 9u * o + 3u * k, p, 3 * sizeof(float));
                if (m->normal)
                    memcpy(m->normal + 9u * o + 3u * k, n, 3 * sizeof(float));
            }
            if (m->uv)
                memcpy(m->uv + 6u * o, s->uv + 6u * t, 6 * sizeof(float));
            if (m->color)
                memcpy(m->color + 12u * o, s->col + 12u * t, 12);
            if (m->texture)
                m->texture[o] = (int16_t)(full ? dr->texture : (dr->archive == 0 ? dr->texture : -1));
            if (m->material)
                m->material[o] = dr->material;
            if (full) {
                m->archive[o] = (int16_t)dr->archive;
                if (m->draw_flags)
                    m->draw_flags[o] = dr->flags;
                if (m->order)
                    m->order[o] = dr->order;
                if (m->blend) {
                    m->blend[2u * o] = dr->blend_src;
                    m->blend[2u * o + 1u] = dr->blend_dst;
                }
            }
        }
    }
    m->num_triangles = need;
    return SA1_OK;
}

/* the draws of the character's posed geometry (CPU evaluator) into rpose; the mesh source of it */
static sa1_status pose_src(sa1_context *ctx, sa1_char *ch, mesh_src *s)
{
    const sa1_charset *cs = loaded_set(ctx, ch->inst.kind);
    const sa1_geometry *g = &ch->inst.geom;
    sa1_status st = sa1_meshbuf_draws_of_geometry_ex(ctx, &ch->rpose, g, cs, ch->pose_archive);
    if (st != SA1_OK)
        return st;
    memset(s, 0, sizeof *s);
    s->idx = g->indices;
    s->pos = g->positions;
    s->nrm = g->normals;
    s->uv = g->uvs;
    s->col = g->colors;
    s->draws = ch->rpose.draws;
    s->ndraw = ch->rpose.ndraw;
    return SA1_OK;
}

static void buf_src(const sa1_meshbuf *b, mesh_src *s)
{
    memset(s, 0, sizeof *s);
    s->idx = b->idx;
    s->pos = b->pos;
    s->nrm = b->nrm;
    s->uv = b->uv;
    s->col = b->col;
    s->draws = b->draws;
    s->ndraw = b->ndraw;
}

/* the character at its current position, yaw, action and frame by the CPU evaluator (the puppet path of design 7.3,
 * and the fallback of mesh_current); an action that is not loaded: the bind pose */
/* round 0.9.3a (sadc docs/research/super_general_design.md 4.3, review RC15): while a General world's look is on, the CPU
 * pose (hidden pairs, the v0.1 mesh before the first capture) draws what the game draws: Sonic's body as the Super body
 * (the object of the game's actions 130-140), the ball and the curled body as the Super ball (action 141, the curled body
 * with its spin-ball motion), textured from the SUPERSONIC archive.
 * Round 0.9.4a (super2_plan.md 6.1 task 4 with its Review R.1 / R.8): the one map of a drawn action, shared by the CPU pose
 * and the render interpolation's hint (pair_info, frame_advance, hint_node), read from the state after the last pair
 * (last.super_look, last.super_anims: the table the next pair draws with). A GA row (SA1_SUPOPT_SUPER_ANIMS;
 * sa1w_super_anims_row) draws the game's Super action s: cs->actions[s]'s object and motion (130-134 are in Sonic's set
 * loaded with SA1_LOAD_SUPER_FORMS), played with s's mtnmode and racio (*eff = s), its root lifted by last.super_lift
 * (*lift); every other row (GT) draws its own motion on the Super body / ball as above (*eff = a). Returns 1 when it
 * substituted (*archive: the SUPERSONIC archive, 0 if absent), 0 otherwise (*object / *motion untouched). */
static int general_drawn(const sa1_charset *cs, const sa1_char *ch, int32_t a, int32_t *eff, int32_t *object,
                         int32_t *motion, float *lift, int32_t *archive)
{
    static const uint32_t k_body = 0x8C1CE76Cu, k_ball = 0x8C1DBB2Cu, k_curled = 0x8C1D630Cu;
    static const uint32_t k_super_body = 0x0C909D2Cu, k_super_ball = 0x0C90BD04u;
    int32_t sbody = -1, sball = -1, sball_mot = -1;
    uint32_t k, oa, sa;
    *eff = a;
    *lift = 0.0f;
    *archive = 0;
    if (ch->inst.kind != SA1_CHAR_SONIC || !ch->have_last || !ch->last.valid || !ch->last.super_look || a < 0 ||
        (uint32_t)a >= cs->num_actions || cs->actions[a].status != SA1_ACTION_OK)
        return 0;
    for (k = 0; k < cs->num_archives; k++) {
        if (strcmp(cs->archives[k].name, "SUPERSONIC") == 0) {
            *archive = (int32_t)k;
            break;
        }
    }
    sa = ch->last.super_anims ? sa1w_super_anims_row((uint32_t)a) : 0u;
    if (sa != 0 && sa < cs->num_actions && cs->actions[sa].status == SA1_ACTION_OK && cs->actions[sa].object >= 0 &&
        cs->actions[sa].motion >= 0) {
        *eff = (int32_t)sa;
        *object = cs->actions[sa].object;
        *motion = cs->actions[sa].motion;
        *lift = ch->last.super_lift;
        return 1;
    }
    for (k = 0; k < cs->num_actions; k++) {
        const sa1_action *x = &cs->actions[k];
        if (x->status != SA1_ACTION_OK)
            continue;
        if (sbody < 0 && (x->object_addr & 0x1FFFFFFFu) == (k_super_body & 0x1FFFFFFFu))
            sbody = x->object;
        if (sball < 0 && (x->object_addr & 0x1FFFFFFFu) == (k_super_ball & 0x1FFFFFFFu)) {
            sball = x->object;
            sball_mot = x->motion;
        }
    }
    oa = cs->actions[a].object_addr & 0x1FFFFFFFu;
    if (oa == (k_body & 0x1FFFFFFFu) && sbody >= 0) {
        *object = sbody;
    } else if (oa == (k_ball & 0x1FFFFFFFu) && sball >= 0) {
        *object = sball;
    } else if (oa == (k_curled & 0x1FFFFFFFu) && sball >= 0) {
        *object = sball;
        *motion = sball_mot;
    } else {
        *archive = 0;
        return 0;
    }
    return 1;
}

static sa1_status pose_eval_current(sa1_context *ctx, sa1_char *ch)
{
    const sa1_charset *cs = loaded_set(ctx, ch->inst.kind);
    int32_t object, motion = -1, action = ch->inst.action;
    float lift = 0.0f;              /* round 0.9.4a: GA's lift of the game's Super motions (general_drawn) */
    sa1_status st;
    if (!cs)
        return SA1_ERR_NOT_LOADED;
    object = cs->main_object;
    ch->pose_archive = 0;
    if (action >= 0 && (uint32_t)action < cs->num_actions && cs->actions[action].status == SA1_ACTION_OK) {
        int32_t eff, arch;
        object = cs->actions[action].object;
        motion = cs->actions[action].motion;
        if (general_drawn(cs, ch, action, &eff, &object, &motion, &lift, &arch))
            ch->pose_archive = arch;
    } else {
        action = -1;
    }
    if (cs->has_physics) {
        /* the game's displays draw the model's root above twp->pos: Sonic's and E-102's at the collision centre,
         * center_height up (8C0AE110 / 8C0AE17A, 8C0CFACE), Tails', Knuckles' and Amy's at half their height
         * (8C0BA820, 8C0C46AC, 8C0DA106); the capture shows it (tests/capture_test.c) */
        float up[3] = { 0.0f, 0.0f, 0.0f }, hup[3], keep[3];
        uint32_t k;
        int32_t kind = ch->inst.kind;
        up[1] = (kind == SA1_CHAR_TAILS || kind == SA1_CHAR_KNUCKLES || kind == SA1_CHAR_AMY)
                    ? cs->physics.height * 0.5f : cs->physics.center_height;
        up[1] += lift;  /* round 0.9.4a (review R.8): the lift along the world up center_height uses, as the world's keys */
        sa1_to_host_point(ctx, up, hup);
        memcpy(keep, ch->inst.position, sizeof keep);
        for (k = 0; k < 3; k++)
            ch->inst.position[k] += hup[k];
        st = sa1_pose_evaluate(ctx, cs, &ch->inst, object, motion, ch->inst.frame, 0u);
        memcpy(ch->inst.position, keep, sizeof keep);
    } else {
        st = sa1_pose_evaluate(ctx, cs, &ch->inst, object, motion, ch->inst.frame, 0u);
    }
    if (st != SA1_OK)
        return st;
    ch->inst.geom.action = action;
    ch->rgen++;
    return SA1_OK;
}

static sa1_status pose_current(sa1_context *ctx, sa1_char *ch, sa1_mesh *mesh)
{
    mesh_src s;
    sa1_status st = pose_eval_current(ctx, ch);
    if (st != SA1_OK)
        return st;
    st = pose_src(ctx, ch, &s);
    if (st != SA1_OK)
        return st;
    return write_mesh_src(&s, mesh);
}

/* render buffers of a character: the draws of the last two pairs (double buffer) and the expanded output */
#define RB_DRAWS   512u
#define RB_SPRITES 256u
#define RB_LINES   1024u

#if !defined(SA1_NO_WORLD)
/* the world's display capture (7.2), once per world; the emitter's buffers are sized here, so later ticks do not
 * allocate. A world without a capture block (no lowered runtime) keeps the CPU pose. */
static void capture_attach(sa1_context *ctx, sa1_char *ch)
{
    const sa1_charset *cs;
    uint32_t k;
    if (ch->cap || ch->cap_failed || !ch->world)
        return;
    if (sa1_capture_attach(ctx, ch->world, &ch->cap) != SA1_OK) {
        ch->cap = NULL;
        ch->cap_failed = 1;
        sa1_log(ctx, SA1_LOG_WARN, "the character's world has no display capture block: its mesh is the CPU pose");
        return;
    }
    cs = loaded_set(ctx, ch->inst.kind);
    if (cs) {
        sa1_capture_set_archives(ch->cap, cs);
        sa1_capture_reserve(ctx, ch->cap, 2u * cs->max_vertices + 1024u, 2u * cs->max_triangles + 1024u,
                            2u * cs->max_nodes + 64u);
        for (k = 0; k < 2; k++) {
            sa1_meshbuf_reserve(ctx, &ch->rb[k], 4u * cs->max_vertices + 2048u, 4u * cs->max_triangles + 2048u,
                                4u * cs->max_nodes + 128u);
            sa1_meshbuf_reserve_extra(ctx, &ch->rb[k], RB_DRAWS, RB_SPRITES, RB_LINES);
            sa1_meshbuf_reserve_scratch(ctx, &ch->rb[k], 4096u, 2048u);
            sa1_meshbuf_reserve_exec(ctx, &ch->rb[k]);      /* round 0.9.4: node_exec / spr_exec */
        }
        /* the blob shadow of hidden pairs (4 points, 2 triangles per draw; a few draws) */
        sa1_meshbuf_reserve(ctx, &ch->rshadow, 64u, 64u, 16u);
        sa1_meshbuf_reserve_extra(ctx, &ch->rshadow, 16u, 0u, 0u);
        sa1_meshbuf_reserve_scratch(ctx, &ch->rshadow, 64u, 64u);
        sa1_meshbuf_reserve_exec(ctx, &ch->rshadow);
    }
}
#endif

/* Draws farther than this from the character are left out of its mesh (SA1 units): the game parks models it does
 * not want to see far away (E-102 draws one at y = -1000000), beyond the far clip of any stage. */
#define SA1_MESH_CLIP_RADIUS 16384.0f

/* The draws of a ticking character's last pair (7.2, render output): every draw of the display capture - the models
 * the game drew in the last pair (sub step 0 for the motion player's renderer 8C037098, both sub steps for Ninja
 * draws) with their textures resolved into the character's archives, effects (sprites, lines, 3D polygons) - in host
 * space, materials and colours through the draw state the renderer applied (constant attribute masks, the constant
 * material: Tails' fading tail copies), the blob shadow marked SA1_DRAW_SHADOW (sadc docs/research/gsa1_060_shadow.md
 * L3-L5: the game's own shadow, drawn by the host with its own switch; before the 0.6.0 fixes it was left out and hosts
 * were to draw their own); left out: repeated draws of an unmoved model under a fading constant material over an
 * opaque draw of it (Tails' body copies), models parked beyond SA1_MESH_CLIP_RADIUS. Emitted into the other buffer of
 * the double buffer, which
 * becomes the last pair; the indexed geometry (sa1_character_geometry) points at it. An empty capture is the game
 * drawing nothing (Sonic's damage blink skips the draw on frames with twp->wtimer & 2). */
#if !defined(SA1_NO_WORLD)
/* Frames the motion of a steady action advanced from pair a to pair b: (b - a) modulo the motion's length, the
 * representative nearest the game's per-step advance (PSetMotion 8C079476: modes 3, 4, 5, 8 add racio, 6 and 7
 * subtract it, 9 adds racio * playerwk.spd.x; two steps lie between the drawn frames) or, for the other modes, nearest
 * the previous pair's advance (the spin dash's rev-up speeds up a little per pair), else the shortest. 0 if unknown. */
static int frame_advance(const sa1_charset *cs, const sa1_rinfo *a, const sa1_rinfo *b, double *df)
{
    const sa1_action *ac;
    double n, d, e = 0.0;
    int known = 1;
    int32_t mot;
    /* round 0.9.4a (super2_plan.md 6.1 task 4): the steadiness test keeps the game's action numbers; the motion and its
       play mode are the drawn ones (pair_info: eff, dmot), the same in both pairs */
    if (!cs || !a->valid || !b->valid || a->action != b->action || a->eff != b->eff || a->dmot != b->dmot ||
        b->eff < 0 || (uint32_t)b->eff >= cs->num_actions)
        return 0;
    ac = &cs->actions[b->eff];
    mot = b->dmot >= 0 ? b->dmot : ac->motion;
    if (ac->status != SA1_ACTION_OK || mot < 0 || (uint32_t)mot >= cs->motions.count)
        return 0;
    n = (double)SA1_VEC(cs->motions, sa1_motion)[mot].num_frames;
    if (!(n > 0.0) || !sa1_finite(a->dframe) || !sa1_finite(b->dframe))
        return 0;
    switch (ac->mode) {
    case 3: case 4: case 5: case 8:
        e = 2.0 * (double)ac->speed;
        break;
    case 6: case 7:
        e = -2.0 * (double)ac->speed;
        break;
    case 9:
        e = (double)ac->speed * ((double)a->spd + (double)b->spd);
        break;
    default:
        known = 0;
        break;
    }
    if (!known && a->dvalid) {
        e = a->df;
        known = 1;
    }
    d = fmod((double)b->dframe - (double)a->dframe, n);   /* round 0.9.4a: between the drawn frames */
    if (d < 0.0)
        d += n;
    if (known && e == e && fabs(e) < 64.0 * n)
        d += floor((e - d) / n + 0.5) * n;
    else if (d > 0.5 * n)
        d -= n;
    *df = d;
    return 1;
}

/* the motion hint of pair `slot` (just captured; the other slot holds the previous pair while rhave > 0) */
static void pair_info(const sa1_charset *cs, sa1_char *ch, int32_t slot)
{
    sa1_rinfo *ri = &ch->rinfo[slot];
    const sa1_rinfo *pi = &ch->rinfo[slot ^ 1];
    memset(ri, 0, sizeof *ri);
    if (!ch->have_last || !ch->last.valid || ch->last.action != ch->last.reqaction)
        return;
    ri->valid = 1;
    ri->action = (int32_t)ch->last.action;
    ri->eff = ri->action;
    ri->dobj = -1;
    ri->dmot = -1;
    if (cs && !g_hint_legacy) {
        /* round 0.9.4a (review R.1): the object and motion the display drew (a General world's look) */
        int32_t eff, obj = -1, mot = -1, arch;
        float lift;
        if (ri->action >= 0 && (uint32_t)ri->action < cs->num_actions) {
            obj = cs->actions[ri->action].object;
            mot = cs->actions[ri->action].motion;
        }
        if (general_drawn(cs, ch, ri->action, &eff, &obj, &mot, &lift, &arch)) {
            ri->eff = eff;
            ri->dobj = obj;
            ri->dmot = mot;
        }
    }
    ri->frame = ch->last.nframe;
    ri->dframe = (!g_hint_legacy && ch->last.drawn_valid && ch->last.drawn_frame_valid &&
                  (int32_t)ch->last.drawn_action == ri->action && sa1_finite(ch->last.drawn_frame))
                     ? ch->last.drawn_frame : ri->frame;
    ri->spd = ch->last.pspd[0];
    if (ch->rhave > 0)
        ri->dvalid = frame_advance(cs, pi, ri, &ri->df);
}

/* The texlist of the character's body draws (the others are effects: SA1_DRAW_EFFECT), archive 0's. Round 6a: Sonic and
 * Super Sonic draw either body in their world (sa1_character_order, the drain's revert); the body's texlist follows
 * Sonic's display rule (8C0AE17A, sadc code_8C0AE0E0.c: reqaction, or action while mtnmode is 2, in 130..141 ->
 * TEXLIST_SUPERSONIC 0x0C901EA0, else texlist_SONIC), found among the kind's archives by name (load_other_body,
 * sa1_loader.c) */
static uint32_t body_texlist(const sa1_char *ch, const sa1_charset *cs)
{
    uint32_t i, a;
    const char *want;
    if (!cs->num_archives)
        return 0u;
    if ((ch->inst.kind != SA1_CHAR_SONIC && ch->inst.kind != SA1_CHAR_SUPER_SONIC) || !ch->have_last || !ch->last.valid)
        return cs->archives[0].texlist_addr;
    a = ch->last.mtnmode == 2 ? ch->last.action : ch->last.reqaction;
    want = (a >= 130u && a <= 141u) ? "SUPERSONIC" : "SONIC";
    for (i = 0; i < cs->num_archives; i++)
        if (strcmp(cs->archives[i].name, want) == 0)
            return cs->archives[i].texlist_addr;
    return cs->archives[0].texlist_addr;
}

static sa1_status render_capture(sa1_context *ctx, sa1_char *ch)
{
    const sa1_charset *cs = loaded_set(ctx, ch->inst.kind);
    sa1_geometry *g = &ch->inst.geom;
    sa1_capture_emit_opts o;
    sa1_state_ex x;
    sa1_status st;
    int32_t nxt = ch->rhave ? (ch->rcur ^ 1) : 0;
    if (!cs)
        return SA1_ERR_NOT_LOADED;
    state_of(ctx, ch, &x);
    memset(&o, 0, sizeof o);
    o.texlist0 = body_texlist(ch, cs);
    o.shadow_model = sa1_capture_shadow_model(ch->cap);
    o.clip = 1;
    memcpy(o.clip_center, x.sa1_position, sizeof o.clip_center);
    o.clip_radius = SA1_MESH_CLIP_RADIUS;
    o.cs = cs;
    o.effects = 1;
    o.dedupe = 1;
    o.exec_arrays = 1;      /* round 0.9.4: who drew each model / sprite (sa1_meshbuf.node_exec / spr_exec) */
#if !defined(SA1_NO_WORLD)
    /* round 0.9.5 (the board): its draws left out until it was held once; a kept board in world space */
    o.board_hide = ch->world != NULL && sa1wb_draw_hidden(ch->world);
    o.board_world = ch->world != NULL && sa1wb_kept(ch->world);
#endif
    if (sa1_capture_hidden(ch->cap)) {
        /* the game's camera did not see the character in the last pair: its display returned before drawing (game
         * logic: the visibility test 8C038D00, rt_draw.c section 6). Hosts draw the character whether the game's
         * camera sees it or not: the render output is the CPU pose of the game's action and frame (the posing state
         * follows the game, sa1_character_tick) until a pair draws again. The blob shadow does not ask the test: the
         * game drew it, and it stays with the body (gsa1_060_shadow.md L4): its draws are kept in rshadow and
         * appended to the CPU pose (render_build). */
        sa1_status ps = pose_eval_current(ctx, ch);
        sa1_geometry sg;
        if (ps != SA1_OK)
            return ps;
        ch->rhave = 0;
        ch->rshadow_on = 0;
        if (o.shadow_model) {
            o.only_model = o.shadow_model;
            o.effects = 0;
            st = sa1_capture_emit_into(ctx, ch->cap, &o, &ch->rshadow, &sg);
            if (st == SA1_ERR_OUT_OF_MEMORY)
                return st;
            ch->rshadow_on = st == SA1_OK && ch->rshadow.ndraw > 0;
        }
        return SA1_OK;
    }
    ch->rshadow_on = 0;
    st = sa1_capture_emit_into(ctx, ch->cap, &o, &ch->rb[nxt], g);
    if (st != SA1_OK)
        return st;
    pair_info(cs, ch, nxt);
    ch->rcur = nxt;
    if (ch->rhave < 2)
        ch->rhave++;
    {
        /* the root of the drawn body: the character renderer 8C037098 draws on sub step 0 only (sa1_capture.h), so the
         * body stands where the character was after the pair's FIRST step, one 60 Hz step behind sa1_state (the state
         * after the pair). Rooting the render output (sa1_render.position / yaw, the move of draws of the last pair
         * only) at the state put the host's camera one step ahead of the body: the body sank / trailed by one step of
         * movement and caught up a pair later on steps, landings, falls and the Light Speed release (sadc
         * docs/research/gsa1_vertical_flicker.md cause B, fix B). Without the world's record (a world that ran no whole
         * pair, a faulted world's kept state) the state's root as before. The CPU pose (hidden pairs, before the first
         * pair) stands at the state's root: the first captured pair after it starts one step back. */
        sa1w_pstate dp;
        int dfault;
        if (pstate_of(ch, &dp, &dfault) && dp.drawn_valid && !dfault) {
            sa1_to_host_point(ctx, dp.drawn_pos, ch->rpos[nxt]);
            ch->ryaw[nxt] = bams_to_yaw(dp.drawn_ang[1]);
            ch->rdrawn[nxt] = 1;
        } else {
            memcpy(ch->rpos[nxt], x.position, sizeof ch->rpos[nxt]);
            ch->ryaw[nxt] = x.yaw;
            ch->rdrawn[nxt] = 0;
        }
    }
    ch->rgen++;
    g->attach_valid = x.attach_valid;
    memcpy(g->attach, x.attach, sizeof g->attach);
    g->action = x.action;
    g->object_addr = (x.action >= 0 && (uint32_t)x.action < cs->num_actions) ? cs->actions[x.action].object_addr
                                                                               : cs->main_object_addr;
    return SA1_OK;
}
#endif

/* The mesh of a ticking character: the last pair's draws (render_capture), the CPU pose of the game's action and frame
 * until the world has captured a pair (or when it has no capture). */
static sa1_status mesh_current(sa1_context *ctx, sa1_char *ch, sa1_mesh *mesh)
{
    mesh_src s;
    if (!ch->rhave)
        return pose_current(ctx, ch, mesh);
    buf_src(&ch->rb[ch->rcur], &s);
    return write_mesh_src(&s, mesh);
}

sa1_status sa1_character_tick(sa1_context *ctx, sa1_character_id id, const sa1_input *input, float dt,
                              sa1_state *state_out_p, sa1_mesh *mesh_out)
{
    sa1_char *ch = get_char(ctx, id);
    sa1_status st, cst, rs1 = SA1_OK;
    uint32_t pairs = 0, p2 = 0;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    if (mesh_out && mesh_out->struct_size >= sizeof(uint32_t)) {
        mesh_out->num_triangles = 0;
        mesh_out->triangles_needed = 0;
    }
    if (!(dt >= 0.0f) || dt > 3600.0f)
        return SA1_ERR_INVALID_ARGUMENT;
    cst = ensure_world(ctx, ch, id);
    if (SA1_FAILED(cst)) {
        if (state_out_p && state_out_p->struct_size >= sizeof(uint32_t))
            state_out(ctx, ch, state_out_p);
        return cst;
    }
#if defined(SA1_NO_WORLD)
    (void)input;
    (void)pairs;
    (void)p2;
    (void)rs1;
    st = SA1_ERR_NOT_IMPLEMENTED;
#else
    /* the game's displays draw into the capture while the window is open (7.2): every live tick, whether the host asks
     * for the mesh or not, so the world runs the same code either way */
    capture_attach(ctx, ch);
    sa1w_world_input_ex(ch->world, input, &ctx->coords);   /* round 0.9.5: with the host camera (SA1_INPUT_HOST_CAMERA) */
    {
        /* A tick of k >= 2 pairs (a frame of 33 ms or more) captures every pair it runs, as k one-pair ticks do, so
         * that the render output interpolates between the last two pairs (sadc docs/research/gsa1_060_pops.md F1 and
         * its Review R.3.1). With one capture per tick the previous capture was k pairs old: sa1_render.position and the
         * body lagged up to (k - 1)(1 - alpha) pairs behind the game and caught up at 3-5x speed in the next frames.
         * Every pair but the last runs in a window of its own (sa1w_world_run keeps the rest of the accumulator: the
         * first run adds dt, the others 0, the last drops a backlog beyond SA1G_MAX_PAIRS_PER_TICK as one run would: the
         * world makes the same decisions and runs the same steps; the input is set once) and is captured with the state
         * of that pair (ch->last for the motion hint pair_info, ch->inst for the CPU pose of a hidden pair: the Review's
         * refresh). Capturing each one, not only the one before the last, keeps the motion hints' chain (pair_info's
         * frame advance of the pair before) what one-pair ticks have: a k-pair tick renders bit for bit what k one-pair
         * ticks render (tests/pairs_test.c). Cost: k - 1 more captures after a long frame only. */
        uint32_t due = sa1w_world_pairs_due(ch->world, dt), p1 = 0, j;
        st = SA1_OK;
        for (j = 1; j < due && st == SA1_OK; j++) {
            uint32_t pj = 0;
            sa1_capture_open(ch->cap);
            st = sa1w_world_run(ch->world, j == 1u ? dt : 0.0f, 1u, 1, &pj);
            sa1_capture_close(ch->cap);
            p1 += pj;
            if (st == SA1_OK && pj > 0) {
                sa1_state_ex x;
                sa1w_world_pstate(ch->world, &ch->last);
                ch->have_last = 1;
                state_of(ctx, ch, &x);
                memcpy(ch->inst.position, x.position, sizeof ch->inst.position);
                ch->inst.yaw = x.yaw;
                ch->inst.action = x.action;
                ch->inst.frame = x.frame;
                if (rs1 == SA1_OK && sa1_capture_valid(ch->cap))
                    rs1 = render_capture(ctx, ch);
            }
        }
        if (st == SA1_OK) {
            sa1_capture_open(ch->cap);
            st = sa1w_world_run(ch->world, due >= 2u ? 0.0f : dt, due >= 2u ? 1u : 0u, 0, &p2);
            sa1_capture_close(ch->cap);
        }
        pairs = p1 + p2;
    }
    if (st == SA1_OK) {
        /* round 0.9.3a: the General form's warnings (the world cannot log from inside a pair) */
        uint32_t sw = sa1w_world_super_warnings(ch->world);
        if (sw & SA1W_SUPER_WARN_GUARD)
            sa1_log(ctx, SA1_LOG_WARN, "character %d: the General Super look drew an action without a Super object: "
                    "Sonic's textures for it (review RC7)", (int)id);
        if (sw & SA1W_SUPER_WARN_LEAK)
            sa1_log(ctx, SA1_LOG_WARN, "character %d: the General Super Sonic is in the game's Super modes 71-78 (a "
                    "scripted request 9, 10, 12-17 or 19-23 through mode 20: design 4.4)", (int)id);
        if (sw & SA1W_SUPER_WARN_WELDS)
            sa1_log(ctx, SA1_LOG_WARN, "character %d: the General form's weld set-up did not run: the Super body has no "
                    "welds", (int)id);
        if (sw & SA1W_SUPER_WARN_GA)
            sa1_log(ctx, SA1_LOG_WARN, "character %d: the General form's Super poses (SA1_SUPOPT_SUPER_ANIMS) could not be "
                    "set up: Sonic's motions on the Super body stay", (int)id);
        /* kept for sa1_character_state after a fault (the arena then holds a half-run step) */
        sa1w_world_pstate(ch->world, &ch->last);
        ch->have_last = 1;
        /* the render accumulator mirrors the world's: dt that did not make a whole pair yet */
        ch->acc += (double)dt - (double)pairs / 30.0;
        if (ch->acc < 0.0)
            ch->acc = 0.0;
        if (ch->acc >= 1.0 / 30.0)
            ch->acc = fmod(ch->acc, 1.0 / 30.0);   /* a backlog of more than 8 pairs was dropped */
    } else {
        sa1_fail(ctx, st, "character %d: %s", (int)id, sa1w_world_fault(ch->world));
    }
#endif
    if (ch->world) {
        /* the posing state follows the game */
        sa1_state_ex x;
        state_of(ctx, ch, &x);
        memcpy(ch->inst.position, x.position, sizeof ch->inst.position);
        ch->inst.yaw = x.yaw;
        ch->inst.action = x.action;
        ch->inst.frame = x.frame;
    }
#if !defined(SA1_NO_WORLD)
    if (st == SA1_OK && p2 > 0 && sa1_capture_valid(ch->cap)) {
        sa1_status rs = render_capture(ctx, ch);
        if (rs != SA1_OK)
            return rs;
    }
    if (rs1 != SA1_OK)
        return rs1;     /* the capture of an earlier pair of the tick failed (the world ran on) */
#endif
    if (state_out_p && state_out_p->struct_size >= sizeof(uint32_t))
        state_out(ctx, ch, state_out_p);
    if (st == SA1_OK && mesh_out) {
        sa1_status ms = mesh_current(ctx, ch, mesh_out);
        if (ms != SA1_OK)
            return ms;
    }
    /* a world built here without land (cst == SA1_PARTIAL) was logged; the tick itself succeeded */
    return st;
}

sa1_status sa1_character_pose(sa1_context *ctx, sa1_character_id id, const sa1_pose *pose_in, sa1_mesh *mesh_out)
{
    sa1_char *ch = get_char(ctx, id);
    sa1_instance *inst = ch ? &ch->inst : NULL;
    const sa1_charset *cs;
    sa1_pose pose;
    int32_t object, motion = -1;
    sa1_status st;
    mesh_src s;
    if (!inst)
        return SA1_ERR_NOT_FOUND;
    if (!pose_in || pose_in->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    memset(&pose, 0, sizeof pose);
    memcpy(&pose, pose_in, pose_in->struct_size < sizeof pose ? pose_in->struct_size : sizeof pose);
    if (!sa1_finite(pose.frame) || !sa1_finite(pose.yaw) || !sa1_finite(pose.position[0]) ||
        !sa1_finite(pose.position[1]) || !sa1_finite(pose.position[2]))
        return SA1_ERR_INVALID_ARGUMENT;
    cs = loaded_set(ctx, inst->kind);
    if (!cs)
        return SA1_ERR_NOT_LOADED;
    object = cs->main_object;
    if (!(pose.flags & SA1_POSE_BIND)) {
        const sa1_action *a;
        if (pose.action < 0 || (uint32_t)pose.action >= cs->num_actions)
            return sa1_fail(ctx, SA1_ERR_NOT_FOUND, "action %d does not exist", (int)pose.action);
        a = &cs->actions[pose.action];
        if (a->status != SA1_ACTION_OK)
            return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "action %d is not available", (int)pose.action);
        object = a->object;
        motion = a->motion;
    }
    memcpy(inst->position, pose.position, sizeof inst->position);
    inst->yaw = pose.yaw;
    inst->action = (pose.flags & SA1_POSE_BIND) ? -1 : pose.action;
    inst->frame = pose.frame;
    st = sa1_pose_evaluate(ctx, cs, inst, object, motion, pose.frame, pose.flags);
    if (st != SA1_OK)
        return st;
    inst->geom.action = inst->action;
    ch->rgen++;
    ch->rhave = 0;  /* the render output shows this pose (a ticking character: until its next pair) */
    ch->rshadow_on = 0;
    if (mesh_out) {
        st = pose_src(ctx, ch, &s);
        if (st != SA1_OK)
            return st;
        return write_mesh_src(&s, mesh_out);
    }
    return SA1_OK;
}

sa1_status sa1_character_geometry(const sa1_context *ctx, sa1_character_id id, const sa1_geometry **out)
{
    sa1_instance *inst = get_inst(ctx, id);
    if (!out)
        return SA1_ERR_INVALID_ARGUMENT;
    *out = NULL;
    if (!inst)
        return SA1_ERR_NOT_FOUND;
    *out = &inst->geom;
    return SA1_OK;
}

/* ------------------------------------------------------------------------- */
/* Render output (SA1_API_RENDER)                                            */
/* ------------------------------------------------------------------------- */

static void rout_free(const sa1_context *ctx, sa1_rout *o)
{
    sa1_mem_free(ctx, o->pos);
    sa1_mem_free(ctx, o->nrm);
    sa1_mem_free(ctx, o->uv);
    sa1_mem_free(ctx, o->col);
    sa1_mem_free(ctx, o->draws);
    sa1_mem_free(ctx, o->spr);
    sa1_mem_free(ctx, o->lv);
    sa1_mem_free(ctx, o->vpos);
    sa1_mem_free(ctx, o->vnrm);
    sa1_mem_free(ctx, o->vstat);
    sa1_mem_free(ctx, o->pidx);
    sa1_mem_free(ctx, o->plan);
    sa1_mem_free(ctx, o->order);
    sa1_mem_free(ctx, o->nm);
    sa1_mem_free(ctx, o->inv);
    sa1_mem_free(ctx, o->inv_ok);
    sa1_mem_free(ctx, o->fol);
    sa1_mem_free(ctx, o->hash);
    sa1_mem_free(ctx, o->omatch);
    sa1_mem_free(ctx, o->vobj);
    sa1_mem_free(ctx, o->ocand);
    sa1_mem_free(ctx, o->otaken);
    sa1_mem_free(ctx, o->sobj);
    sa1_mem_free(ctx, o->smatch);
    sa1_mem_free(ctx, o->okey);
    memset(o, 0, sizeof *o);
}

/* the plan's arrays for nn drawn models and nv vertices */
static sa1_status plan_reserve(const sa1_context *ctx, sa1_rout *o, uint32_t nn, uint32_t nv)
{
    if (nv > o->cap_v) {
        uint32_t c = nv + nv / 2u + 256u;
        float *p = (float *)sa1_mem_array(ctx, 3u * (size_t)c, sizeof(float));
        float *n = (float *)sa1_mem_array(ctx, 3u * (size_t)c, sizeof(float));
        uint8_t *s = (uint8_t *)sa1_mem_array(ctx, c, 1);
        uint32_t *px = (uint32_t *)sa1_mem_array(ctx, c, sizeof(uint32_t));
        sa1_rfollow *f = (sa1_rfollow *)sa1_mem_array(ctx, c, sizeof(sa1_rfollow));
        uint8_t *vo = (uint8_t *)sa1_mem_array(ctx, c, 1);
        uint32_t hc = 64u, *h;
        while (hc < 2u * c)
            hc *= 2u;
        h = (uint32_t *)sa1_mem_array(ctx, hc, sizeof(uint32_t));
        if (!p || !n || !s || !px || !f || !h || !vo) {
            sa1_mem_free(ctx, p);
            sa1_mem_free(ctx, n);
            sa1_mem_free(ctx, s);
            sa1_mem_free(ctx, px);
            sa1_mem_free(ctx, f);
            sa1_mem_free(ctx, h);
            sa1_mem_free(ctx, vo);
            return SA1_ERR_OUT_OF_MEMORY;
        }
        sa1_mem_free(ctx, o->vpos);
        sa1_mem_free(ctx, o->vnrm);
        sa1_mem_free(ctx, o->vstat);
        sa1_mem_free(ctx, o->pidx);
        sa1_mem_free(ctx, o->vobj);
        o->pidx = px;
        o->vobj = vo;
        sa1_mem_free(ctx, o->fol);
        sa1_mem_free(ctx, o->hash);
        o->vpos = p;
        o->vnrm = n;
        o->vstat = s;
        o->fol = f;
        o->cap_fol = c;
        o->hash = h;
        o->cap_hash = hc;
        o->cap_v = c;
    }
    if (nn > o->cap_plan) {
        uint32_t c = nn + nn / 2u + 32u;
        sa1_rnode *p = (sa1_rnode *)sa1_mem_array(ctx, c, sizeof(sa1_rnode));
        uint32_t *ord = (uint32_t *)sa1_mem_array(ctx, c, sizeof(uint32_t));
        double *nm = (double *)sa1_mem_array(ctx, 12u * (size_t)c, sizeof(double));
        double *inv = (double *)sa1_mem_array(ctx, 24u * (size_t)c, sizeof(double));
        uint8_t *iok = (uint8_t *)sa1_mem_array(ctx, c, 1);
        int32_t *om = (int32_t *)sa1_mem_array(ctx, c, sizeof(int32_t));
        if (!p || !ord || !nm || !inv || !iok || !om) {
            sa1_mem_free(ctx, p);
            sa1_mem_free(ctx, ord);
            sa1_mem_free(ctx, nm);
            sa1_mem_free(ctx, inv);
            sa1_mem_free(ctx, iok);
            sa1_mem_free(ctx, om);
            return SA1_ERR_OUT_OF_MEMORY;
        }
        sa1_mem_free(ctx, o->plan);
        sa1_mem_free(ctx, o->order);
        sa1_mem_free(ctx, o->nm);
        sa1_mem_free(ctx, o->inv);
        sa1_mem_free(ctx, o->inv_ok);
        sa1_mem_free(ctx, o->omatch);
        o->plan = p;
        o->order = ord;
        o->nm = nm;
        o->inv = inv;
        o->inv_ok = iok;
        o->omatch = om;
        o->cap_plan = c;
    }
    return SA1_OK;
}

/* round 0.9.4: room for nc match candidates and nt "taken" flags of the pair before (scratch: contents not kept) */
static sa1_status ocand_reserve(const sa1_context *ctx, sa1_rout *o, uint32_t nc, uint32_t nt)
{
    if (nc > o->cap_ocand) {
        uint32_t c = nc + nc / 2u + 64u;
        sa1_ocand *p = (sa1_ocand *)sa1_mem_array(ctx, c, sizeof(sa1_ocand));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        sa1_mem_free(ctx, o->ocand);
        o->ocand = p;
        o->cap_ocand = c;
    }
    if (nt > o->cap_otaken) {
        uint32_t c = nt + nt / 2u + 64u;
        uint8_t *p = (uint8_t *)sa1_mem_array(ctx, c, 1);
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        sa1_mem_free(ctx, o->otaken);
        o->otaken = p;
        o->cap_otaken = c;
    }
    return SA1_OK;
}

/* round 0.9.4: room for the sprite classification of both pairs (ns sprites each) */
static sa1_status sobj_reserve(const sa1_context *ctx, sa1_rout *o, uint32_t ns)
{
    if (ns > o->cap_sobj) {
        uint32_t c = ns + ns / 2u + 32u;
        uint8_t *p = (uint8_t *)sa1_mem_array(ctx, 2u * (size_t)c, 1);
        int32_t *m = (int32_t *)sa1_mem_array(ctx, c, sizeof(int32_t));
        if (!p || !m) {
            sa1_mem_free(ctx, p);
            sa1_mem_free(ctx, m);
            return SA1_ERR_OUT_OF_MEMORY;
        }
        sa1_mem_free(ctx, o->sobj);
        sa1_mem_free(ctx, o->smatch);
        o->sobj = p;
        o->smatch = m;
        o->cap_sobj = c;
    }
    return SA1_OK;
}

static sa1_status rout_reserve(const sa1_context *ctx, sa1_rout *o, uint32_t nt, uint32_t nd, uint32_t ns, uint32_t nl)
{
    if (nt > o->cap_t) {
        uint32_t c = nt + nt / 2u + 256u;
        float *p = (float *)sa1_mem_array(ctx, 9u * (size_t)c, sizeof(float));
        float *n = (float *)sa1_mem_array(ctx, 9u * (size_t)c, sizeof(float));
        float *u = (float *)sa1_mem_array(ctx, 6u * (size_t)c, sizeof(float));
        uint8_t *k = (uint8_t *)sa1_mem_array(ctx, 12u * (size_t)c, 1);
        if (!p || !n || !u || !k) {
            sa1_mem_free(ctx, p);
            sa1_mem_free(ctx, n);
            sa1_mem_free(ctx, u);
            sa1_mem_free(ctx, k);
            return SA1_ERR_OUT_OF_MEMORY;
        }
        sa1_mem_free(ctx, o->pos);
        sa1_mem_free(ctx, o->nrm);
        sa1_mem_free(ctx, o->uv);
        sa1_mem_free(ctx, o->col);
        o->pos = p;
        o->nrm = n;
        o->uv = u;
        o->col = k;
        o->cap_t = c;
    }
    if (nd > o->cap_d) {
        uint32_t c = nd + nd / 2u + 32u;
        sa1_draw *p = (sa1_draw *)sa1_mem_array(ctx, c, sizeof(sa1_draw));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        sa1_mem_free(ctx, o->draws);
        o->draws = p;
        o->cap_d = c;
    }
    if (ns > o->cap_s) {
        uint32_t c = ns + ns / 2u + 32u;
        sa1_sprite *p = (sa1_sprite *)sa1_mem_array(ctx, c, sizeof(sa1_sprite));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        sa1_mem_free(ctx, o->spr);
        o->spr = p;
        o->cap_s = c;
    }
    if (nl > o->cap_l) {
        uint32_t c = nl + nl / 2u + 64u;
        sa1_line_vertex *p = (sa1_line_vertex *)sa1_mem_array(ctx, c, sizeof(sa1_line_vertex));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        sa1_mem_free(ctx, o->lv);
        o->lv = p;
        o->cap_l = c;
    }
    return SA1_OK;
}

static float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static void lerp3(const float *a, const float *b, float t, float *o)
{
    o[0] = lerpf(a[0], b[0], t);
    o[1] = lerpf(a[1], b[1], t);
    o[2] = lerpf(a[2], b[2], t);
}

static void nlerp3(const float *a, const float *b, float t, float *o)
{
    float l;
    lerp3(a, b, t, o);
    l = sqrtf(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
    if (l > 1e-20f) {
        o[0] /= l;
        o[1] /= l;
        o[2] /= l;
    } else {
        memcpy(o, b, 3 * sizeof(float));
    }
}

static int same_topology(const sa1_meshbuf *a, const sa1_meshbuf *b)
{
    return a->sig == b->sig && a->nv == b->nv && a->nt == b->nt && a->ndraw == b->ndraw && a->nspr == b->nspr &&
           a->nlv == b->nlv && a->nn == b->nn;
}

/* Turns of more than this between two pairs are ambiguous (which way round, how many times) without the motion's hint:
 * the draw then takes the last pair's rotation ("accept the last pair") */
#define RENDER_MAX_ANGLE 2.6179938779914944    /* 150 degrees */
/* welded copies of a vertex lie within this of it at both pairs (SA1 units; measured: 1e-4) */
#define FOLLOW_EPS 0.01
#define PIDX_NONE 0xFFFFFFFFu

static void cell_of(const float *p, double cs, int64_t c[3])
{
    c[0] = (int64_t)floor((double)p[0] / cs);
    c[1] = (int64_t)floor((double)p[1] / cs);
    c[2] = (int64_t)floor((double)p[2] / cs);
}

static uint32_t cell_hash(const int64_t c[3])
{
    uint64_t h = (uint64_t)c[0] * 73856093ull ^ (uint64_t)c[1] * 19349663ull ^ (uint64_t)c[2] * 83492791ull;
    return (uint32_t)(h ^ (h >> 29));
}

static int near3(const float *a, const float *b, double eps)
{
    double dx = (double)a[0] - b[0], dy = (double)a[1] - b[1], dz = (double)a[2] - b[2];
    return dx * dx + dy * dy + dz * dz <= eps * eps;
}

/* The node of the model drawn as `model` for the motion hint: in the steady action's object, else in the object of an
 * action whose motion has as many frames (a display may draw another action at the steady one's frame: SonicDisplay
 * 8C0AE17A draws the spin ball, action 32, at the frame of action 14 / 15). -1 if none. Round 0.9.4a (review R.1): the
 * object and motion the display drew (pair_info's dobj / dmot: a General world's look) in place of the action's own, so a
 * Super body model is found in the Super body with the drawn motion, not in a Super action of the same frame count */
static int32_t hint_node(const sa1_charset *cs, const sa1_rinfo *ri, uint32_t model, int32_t *object, int32_t *motion)
{
    const sa1_action *ac = &cs->actions[ri->action];
    uint32_t nf, a;
    int32_t node, aobj, amot;
    if (ac->status != SA1_ACTION_OK)
        return -1;
    aobj = ri->dobj >= 0 ? ri->dobj : ac->object;
    amot = ri->dobj >= 0 ? ri->dmot : ac->motion;
    if (aobj < 0 || amot < 0 || (uint32_t)amot >= cs->motions.count)
        return -1;
    node = sa1_pose_node_of_model(cs, aobj, model);
    if (node >= 0) {
        *object = aobj;
        *motion = amot;
        return node;
    }
    nf = SA1_VEC(cs->motions, sa1_motion)[amot].num_frames;
    for (a = 0; a < cs->num_actions; a++) {
        const sa1_action *b = &cs->actions[a];
        if (b->status != SA1_ACTION_OK || b->object < 0 || b->object == aobj || b->motion < 0 ||
            (uint32_t)b->motion >= cs->motions.count || SA1_VEC(cs->motions, sa1_motion)[b->motion].num_frames != nf)
            continue;
        node = sa1_pose_node_of_model(cs, b->object, model);
        if (node >= 0) {
            *object = b->object;
            *motion = b->motion;
            return node;
        }
    }
    return -1;
}

/* ---- anchored / root-frame interpolation (sadc docs/research/gsa1_060_render.md 3.6 and its Review R4) ----------
 * Interpolating every drawn model's world matrix on its own cuts a straight chord for each model's origin while the
 * character turns, so two rigidly attached models drift apart between the pairs by about |a - b| (1 - cos(theta / 2)):
 * Big's eye whites, 13.7 HU from his head's origin and 0.04 HU in front of his face, sank 0.2-2 HU behind it on the
 * frames between the pairs of a turn (a 30 Hz shimmer that looked like z-fighting); Sonic's irises, Tails', Knuckles'
 * and E-102's eyes did the same with smaller margins. Now the models are interpolated in the order of their capture
 * records' matrix stack depth; each takes as its anchor the model, among those already planned with the same texlist,
 * whose relative transform A^-1 * M changed least between the pairs (the largest element change, translations weighted
 * ANCHOR_TWEIGHT per SA1 unit, below ANCHOR_MAX: rigidly attached), and interpolates that relative transform; a model
 * without one is interpolated in the drawn root's frame (F = translation sa1_render.position, turn sa1_render.yaw),
 * with the motion's hint of its node's turn as before. Prototype (render_probe.c method 3), failing pairs of the
 * eye / face decal criterion: Big 0 (was 12 and 4), Tails 0 (41), E-102 0 (9), Amy 0 (1), Sonic 2 (23), Knuckles 7
 * (33, all in action 22 whose own pose already intersects). The blob shadow takes no anchor and is none.
 * Round 0.9.1: (1) the root of a rigid group - a model without an anchor that at least SCREW_MIN_GROUP drawn models are
 * anchored to, directly or through others - moves along the screw motion between its two transforms (xlerp_screw_at,
 * sadc docs/research/gsa1_070_tails.md 5-6.1 and its Review R.4.1): Tails' curled body, which the game draws inside the
 * spin ball (opaque, or as the translucent copies of the tail-blur loop), turns up to half a turn per pair about the
 * ball's centre, 1.36 units off its top model's origin; on the straight chord of that origin the body swung out of the
 * ball between the pairs (copy tails up to 7.6 HU outside it, the opaque body's centroid up to 9.6 HU off its line). The
 * screw keeps it on its circle. Limbs keep the chord: the screw arcs them about centres that are not their joints (feet
 * through the floor: the doc's prototype, screw for every model without an anchor, failed the review's T-limbs check).
 * A draw matched with one of the other fading state (SA1_NODE_FADE: the opaque curled body of a body pair and the
 * translucent copy of the ball pair before it, which the game draws at poses whose relative turn does not follow the
 * motion's) takes no motion hint and the shorter way, never SNAP: with the hint it turned 193-267 degrees the wrong way
 * round between those pairs (Review R.3.3; p71 / p73 / p75 of tests/ball_test.c's roll).
 * (2) A model without an anchor that is not one of the character's own models (charset_model: a companion, an effect
 * left in the world) and whose translation changed much less in SA1 world space than in the drawn root's frame
 * (WORLD_RATIO, WORLD_MARGIN) is interpolated in world space (gsa1_070_bird.md Review R.4 F5): Amy's bird follows her
 * position, not her turn, and in the root frame it swung in to her axis and out again on the frames between the pairs
 * of a sharp turn (up to 5.25 units off its straight path). The character's own models keep its frame: a running
 * Knuckles' hand that swings back can move less in the world than in his frame, and Tails' curled body turning about
 * the ball's centre moves more in his frame than a slow ball does in the world (its tails then left the ball). */
#define ANCHOR_MAX     0.25
#define ANCHOR_TWEIGHT 0.1
#define SCREW_MIN_GROUP 4u
#define WORLD_RATIO    0.5      /* world-space translation change below this share of the root-frame one ... */
#define WORLD_MARGIN   1.0      /* ... and smaller by at least this many SA1 units */

/* o = a^-1 (3x4, column vectors, row-major); 0 when singular */
static int inv34(const double a[12], double o[12])
{
    double a0 = a[0], a1 = a[1], a2 = a[2], b0 = a[4], b1 = a[5], b2 = a[6], c0 = a[8], c1 = a[9], c2 = a[10];
    double det = a0 * (b1 * c2 - b2 * c1) - a1 * (b0 * c2 - b2 * c0) + a2 * (b0 * c1 - b1 * c0), id;
    int r;
    if (!(fabs(det) > 1e-20) || det != det)
        return 0;
    id = 1.0 / det;
    o[0] = (b1 * c2 - b2 * c1) * id;
    o[1] = (a2 * c1 - a1 * c2) * id;
    o[2] = (a1 * b2 - a2 * b1) * id;
    o[4] = (b2 * c0 - b0 * c2) * id;
    o[5] = (a0 * c2 - a2 * c0) * id;
    o[6] = (a2 * b0 - a0 * b2) * id;
    o[8] = (b0 * c1 - b1 * c0) * id;
    o[9] = (a1 * c0 - a0 * c1) * id;
    o[10] = (a0 * b1 - a1 * b0) * id;
    for (r = 0; r < 3; r++)
        o[4 * r + 3] = -(o[4 * r] * a[3] + o[4 * r + 1] * a[7] + o[4 * r + 2] * a[11]);
    return 1;
}

/* o = a * b (3x4 affine, column vectors) */
static void mul34(const double a[12], const double b[12], double o[12])
{
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 4; j++)
            o[4 * i + j] = a[4 * i] * b[j] + a[4 * i + 1] * b[4 + j] + a[4 * i + 2] * b[8 + j] + (j == 3 ? a[4 * i + 3] : 0.0);
}

/* v rotated by `ang` radians about the unit axis u (Rodrigues) */
static void rot_axis(const double u[3], double ang, const double v[3], double o[3])
{
    double c = cos(ang), s = sin(ang), d = u[0] * v[0] + u[1] * v[1] + u[2] * v[2], cr[3];
    int i;
    cr[0] = u[1] * v[2] - u[2] * v[1];
    cr[1] = u[2] * v[0] - u[0] * v[2];
    cr[2] = u[0] * v[1] - u[1] * v[0];
    for (i = 0; i < 3; i++)
        o[i] = v[i] * c + cr[i] * s + u[i] * d * (1.0 - c);
}

/* sa1_xlerp_at with the translation along the screw motion that takes the first transform to the second (see above):
 * the points turn about the rotation's own axis by alpha * phi (the hinted angle) and slide along it, instead of the
 * origin cutting a straight chord. Rotation and stretch as sa1_xlerp_at; alpha 0 and 1 give both transforms exactly
 * (up to rounding). SNAP and tiny turns: as sa1_xlerp_at. */
static void xlerp_screw_at(const sa1_xlerp *x, double alpha, double m[12])
{
    double rt0[3], d[3], dpar, dperp[3], ucd[3], c[3], t0c[3], r[3], cotv;
    const double *u = x->axis;
    int i;
    sa1_xlerp_at(x, alpha, m);
    if (x->kind != SA1_XLERP_SLERP || x->theta < 1e-7)
        return;
    rot_axis(u, x->theta, x->t0, rt0);
    for (i = 0; i < 3; i++)
        d[i] = x->t1[i] - rt0[i];
    dpar = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
    for (i = 0; i < 3; i++)
        dperp[i] = d[i] - dpar * u[i];
    ucd[0] = u[1] * dperp[2] - u[2] * dperp[1];
    ucd[1] = u[2] * dperp[0] - u[0] * dperp[2];
    ucd[2] = u[0] * dperp[1] - u[1] * dperp[0];
    cotv = cos(0.5 * x->theta) / sin(0.5 * x->theta);
    for (i = 0; i < 3; i++) {
        c[i] = 0.5 * (dperp[i] + cotv * ucd[i]);    /* a point of the screw axis */
        t0c[i] = x->t0[i] - c[i];
    }
    rot_axis(u, alpha * x->phi, t0c, r);
    for (i = 0; i < 3; i++)
        m[4 * i + 3] = c[i] + r[i] + alpha * dpar * u[i];
}

/* whether `model` (guest address) is one of the character's own models: a node model of the objects of its actions
 * (the charset's model pool). Not one: a companion (Amy's bird), an effect, a buffer the game draws in a node's place. */
static int charset_model(const sa1_charset *cs, uint32_t model)
{
    const sa1_model *m;
    uint32_t i, n;
    if (!cs)
        return 1;
    m = SA1_VEC(cs->models, sa1_model);
    n = (uint32_t)cs->models.count;
    for (i = 0; i < n; i++)
        if ((m[i].addr & 0x1FFFFFFFu) == (model & 0x1FFFFFFFu))
            return 1;
    return 0;
}

/* F5: whether a model without an anchor is interpolated in SA1 world space (w0 -> w1) rather than in the drawn root's
 * frame (q0 -> q1): its translation changed much less there */
static int prefer_world(const double w0[12], const double w1[12], const double q0[12], const double q1[12])
{
    double dw = 0.0, dr = 0.0;
    int i;
    for (i = 3; i < 12; i += 4) {
        dw += (w1[i] - w0[i]) * (w1[i] - w0[i]);
        dr += (q1[i] - q0[i]) * (q1[i] - q0[i]);
    }
    dw = sqrt(dw);
    dr = sqrt(dr);
    return dw < WORLD_RATIO * dr && dr - dw > WORLD_MARGIN;
}

/* the drawn root's frame in SA1 space: translation `host` (host space), turned by `yaw` about the up axis (the
 * sa1_state.yaw convention: the model's +X axis along the game's forward (cos a, 0, sin a) for ang.y = a = -yaw) */
static void root_frame(const sa1_context *ctx, const float host[3], double yaw, double f[12])
{
    float p[3];
    double c = cos(yaw), s = sin(yaw);
    sa1_from_host_point(ctx, host, p);
    f[0] = c;
    f[1] = 0.0;
    f[2] = s;
    f[3] = p[0];
    f[4] = 0.0;
    f[5] = 1.0;
    f[6] = 0.0;
    f[7] = p[1];
    f[8] = -s;
    f[9] = 0.0;
    f[10] = c;
    f[11] = p[2];
}

/* the drawn root's yaw at alpha (sa1_render.yaw: the shorter way from the pair before) */
static double root_yaw_at(const sa1_char *ch, float alpha)
{
    int32_t pi = ch->rcur ^ 1, ci = ch->rcur;
    double dy = (double)ch->ryaw[ci] - ch->ryaw[pi];
    while (dy > 3.14159265358979323846)
        dy -= 6.28318530717958647692;
    while (dy < -3.14159265358979323846)
        dy += 6.28318530717958647692;
    return (double)ch->ryaw[pi] + dy * (double)alpha;
}

/* ---- game OBJECT draws between the pairs (round 0.9.4) --------------------------------------------------------------
 * sadc docs/research/gsa1_094_plan.md decision 3 and 6.2 task 3 (R-F1 / R-F2 / R-F3 of gsa1_091b_visuals.md 4.2 with its
 * Review, for game objects only). A draw is an OBJECT draw when its capture record's exec is a game object's
 * (sa1_capture_object_exec, sa1_capture.c: rings, ring groups, dropped / dug rings, both item boxes): it carries
 * SA1_DRAW_OBJECT, its model SA1_NODE_OBJECT. Before, the n-th draw of a model was matched with the n-th draw of it in the
 * pair before; every ring draws one model, so collecting ring j made every ring drawn after it slide from its
 * neighbour's place (up to 130 HU in an owner-like scene: the owner's 0.9.1 note "rings visually glitch on collection",
 * gsa1_091b_visuals.md 2 and Review R.1.2), and a ring's blob shadow could be matched with the character's own.
 *   R-F1 (plan_build): an OBJECT model is matched with the OBJECT draw of the same model in the pair before (the same
 *        vertex count, the same flags except SA1_NODE_FADE) whose model -> world translation lies nearest, at most
 *        OBJ_GATE SA1 units away, each used once, greedily by distance (ties: the draw order of the last pair, then of the
 *        pair before), also when the two pairs have the same topology; the occurrence rule of every other draw counts
 *        only draws that are not OBJECT draws, in both pairs. A matched OBJECT model is interpolated in SA1 world space
 *        (it stays where its task put it: no anchor, no motion hint), and no other draw takes one as its anchor.
 *   R-F2 (render_interp): an OBJECT draw new in the last pair is drawn where the game drew it (no root shift; `moved`
 *        counts only the shifted draws).
 *   R-F3 (render_build): each OBJECT sprite lerps from the nearest OBJECT sprite of the pair before, at most
 *        OBJ_SPRITE_GATE SA1 units away (compared in SA1 units, not host units: gsa1_091b_visuals.md Review R.3.9), each
 *        used once, greedily; the other OBJECT sprites are drawn as they are.
 * The player's own draws and the effects of other tasks (the barrier family, Super Sonic's auras, particles) keep the
 * rules of 0.9.3a; the general "every other task" classification with the review's two-pass follower rule
 * (gsa1_091b_visuals.md Review R.4.2 (b)) stays in the backlog (gsa1_094_plan.md Review R.18). */
#define OBJ_GATE        16.0    /* SA1 units: a ring line's rings lie 10 apart; the dug rings' flight stays below it */
#define OBJ_SPRITE_GATE 8.0     /* SA1 units: the sparkle's own drift is 0.24 units per pair */

static int ocand_cmp(const void *a, const void *b)
{
    const sa1_ocand *x = (const sa1_ocand *)a, *y = (const sa1_ocand *)b;
    if (x->d != y->d)
        return x->d < y->d ? -1 : 1;
    if (x->k != y->k)
        return x->k < y->k ? -1 : 1;
    return x->j < y->j ? -1 : (x->j > y->j ? 1 : 0);
}

/* the greedy assignment of the sorted candidates: match[k] = j (match[] -1 = free), taken[j] */
static void ocand_assign(sa1_rout *o, uint32_t nc, int32_t *match, uint8_t *taken)
{
    uint32_t i;
    qsort(o->ocand, nc, sizeof(sa1_ocand), ocand_cmp);
    for (i = 0; i < nc; i++) {
        const sa1_ocand *c = &o->ocand[i];
        if (match[c->k] >= 0 || taken[c->j])
            continue;
        match[c->k] = (int32_t)c->j;
        taken[c->j] = 1;
    }
}

/* R-F1: whether model k of cur and model j of prev may be matched, and their distance (SA1 units) */
static int obj_pair(const sa1_meshbuf *prev, uint32_t j, const sa1_meshbuf *cur, uint32_t k, double *d)
{
    const double *a = cur->node_wm + 12u * k, *b = prev->node_wm + 12u * j;
    double dx, dy, dz;
    if (!(prev->node_fl[j] & SA1_NODE_OBJECT) || prev->node_model[j] != cur->node_model[k] ||
        prev->node_v[2u * j + 1u] != cur->node_v[2u * k + 1u] ||
        ((prev->node_fl[j] ^ cur->node_fl[k]) & (uint8_t)~SA1_NODE_FADE))
        return 0;
    dx = a[3] - b[3];
    dy = a[7] - b[7];
    dz = a[11] - b[11];
    *d = sqrt(dx * dx + dy * dy + dz * dz);
    return *d <= OBJ_GATE;
}

/* R-F1: the OBJECT models of prev sorted by (model, world x) for the search of obj_match: candidates of a model k of cur
 * are the keys of its model with x within OBJ_GATE of its own (keys and a binary search; no context for qsort needed) */
static int okey_cmp(const void *a, const void *b)
{
    const sa1_okey *x = (const sa1_okey *)a, *y = (const sa1_okey *)b;
    if (x->model != y->model)
        return x->model < y->model ? -1 : 1;
    if (x->x != y->x)
        return x->x < y->x ? -1 : 1;
    return x->j < y->j ? -1 : (x->j > y->j ? 1 : 0);
}

/* the first key of o->okey[0, n) not before (model, x) */
static uint32_t okey_lower(const sa1_rout *o, uint32_t n, uint32_t model, double x)
{
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const sa1_okey *e = &o->okey[mid];
        if (e->model < model || (e->model == model && e->x < x))
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

/* R-F1: the candidates of every OBJECT model of cur (out: NULL counts them); returns the count */
static uint32_t obj_candidates(sa1_rout *o, uint32_t nkey, const sa1_meshbuf *prev, const sa1_meshbuf *cur,
                               sa1_ocand *out)
{
    uint32_t k, i, nc = 0;
    double d;
    for (k = 0; k < cur->nn; k++) {
        double x = cur->node_wm[12u * k + 3u];
        if (!(cur->node_fl[k] & SA1_NODE_OBJECT))
            continue;
        for (i = okey_lower(o, nkey, cur->node_model[k], x - OBJ_GATE);
             i < nkey && o->okey[i].model == cur->node_model[k] && o->okey[i].x <= x + OBJ_GATE; i++) {
            if (!obj_pair(prev, o->okey[i].j, cur, k, &d))
                continue;
            if (out) {
                out[nc].k = k;
                out[nc].j = o->okey[i].j;
                out[nc].d = d;
            }
            nc++;
        }
    }
    return nc;
}

/* R-F1: o->omatch[k] for every drawn model of cur: its OBJECT draw in prev, matched by place (-1: none, or not an
 * OBJECT model) */
static sa1_status obj_match(const sa1_context *ctx, sa1_rout *o, const sa1_meshbuf *prev, const sa1_meshbuf *cur)
{
    uint32_t k, j, nc, nobj = 0, nkey = 0;
    sa1_status st;
    for (k = 0; k < cur->nn; k++) {
        o->omatch[k] = -1;
        nobj += (cur->node_fl[k] & SA1_NODE_OBJECT) != 0;
    }
    if (!nobj || !prev->nn)
        return SA1_OK;
    if (prev->nn > o->cap_okey) {
        uint32_t c = prev->nn + prev->nn / 2u + 64u;
        sa1_okey *p = (sa1_okey *)sa1_mem_array(ctx, c, sizeof(sa1_okey));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        sa1_mem_free(ctx, o->okey);
        o->okey = p;
        o->cap_okey = c;
    }
    for (j = 0; j < prev->nn; j++) {
        if (!(prev->node_fl[j] & SA1_NODE_OBJECT))
            continue;
        o->okey[nkey].model = prev->node_model[j];
        o->okey[nkey].x = prev->node_wm[12u * j + 3u];
        o->okey[nkey].j = j;
        nkey++;
    }
    if (!nkey)
        return SA1_OK;
    qsort(o->okey, nkey, sizeof(sa1_okey), okey_cmp);
    nc = obj_candidates(o, nkey, prev, cur, NULL);
    st = ocand_reserve(ctx, o, nc, prev->nn);
    if (st != SA1_OK)
        return st;
    nc = obj_candidates(o, nkey, prev, cur, o->ocand);
    memset(o->otaken, 0, prev->nn);
    ocand_assign(o, nc, o->omatch, o->otaken);
    return SA1_OK;
}

/* R-F2: o->vobj[v] for the vertices of the last pair's SA1_DRAW_OBJECT triangle draws (models and 3D polygons) */
static void obj_vertices(sa1_rout *o, const sa1_meshbuf *cur)
{
    uint32_t d, t, i;
    memset(o->vobj, 0, cur->nv);
    for (d = 0; d < cur->ndraw; d++) {
        const sa1_draw *dr = &cur->draws[d];
        if (dr->prim != SA1_PRIM_TRIANGLES || !(dr->flags & SA1_DRAW_OBJECT) || dr->first > cur->nt ||
            dr->count > cur->nt - dr->first)
            continue;
        for (t = dr->first; t < dr->first + dr->count; t++)
            for (i = 0; i < 3; i++)
                if (cur->idx[3u * t + i] < cur->nv)
                    o->vobj[cur->idx[3u * t + i]] = 1;
    }
}

/* R-F3: the OBJECT sprites of a pair (sobj[i] = 1 for the sprites of SA1_DRAW_OBJECT sprite draws) */
static uint32_t obj_sprites(const sa1_meshbuf *b, uint8_t *sobj)
{
    uint32_t d, s, n = 0;
    memset(sobj, 0, b->nspr);
    for (d = 0; d < b->ndraw; d++) {
        const sa1_draw *dr = &b->draws[d];
        if (dr->prim != SA1_PRIM_SPRITES || !(dr->flags & SA1_DRAW_OBJECT) || dr->first > b->nspr ||
            dr->count > b->nspr - dr->first)
            continue;
        for (s = dr->first; s < dr->first + dr->count; s++) {
            n += !sobj[s];
            sobj[s] = 1;
        }
    }
    return n;
}

/* R-F3: o->smatch[i] for the sprites of cur: the nearest OBJECT sprite of prev (SA1 units, at most OBJ_SPRITE_GATE),
 * greedily (-1: none, or not an OBJECT sprite). sc / sp: OBJECT flags of cur's / prev's sprites */
static sa1_status obj_sprite_match(const sa1_context *ctx, sa1_rout *o, const sa1_meshbuf *prev, const sa1_meshbuf *cur,
                                   const uint8_t *sc, const uint8_t *sp)
{
    uint32_t i, j, nc = 0, pass;
    sa1_status st;
    for (i = 0; i < cur->nspr; i++)
        o->smatch[i] = -1;
    for (pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            st = ocand_reserve(ctx, o, nc, prev->nspr);
            if (st != SA1_OK)
                return st;
            nc = 0;
        }
        for (i = 0; i < cur->nspr; i++) {
            float a[3];
            if (!sc[i])
                continue;
            sa1_from_host_point(ctx, cur->spr[i].position, a);
            for (j = 0; j < prev->nspr; j++) {
                float b[3];
                double dx, dy, dz, d;
                if (!sp[j])
                    continue;
                sa1_from_host_point(ctx, prev->spr[j].position, b);
                dx = (double)a[0] - b[0];
                dy = (double)a[1] - b[1];
                dz = (double)a[2] - b[2];
                d = sqrt(dx * dx + dy * dy + dz * dz);
                if (!(d <= OBJ_SPRITE_GATE))
                    continue;
                if (pass == 1) {
                    o->ocand[nc].k = i;
                    o->ocand[nc].j = j;
                    o->ocand[nc].d = d;
                }
                nc++;
            }
        }
    }
    memset(o->otaken, 0, prev->nspr);
    ocand_assign(o, nc, o->smatch, o->otaken);
    return SA1_OK;
}

/* The interpolation plan of the last two pairs: every drawn model of the last pair matched with its draw in the pair
 * before (the n-th draw of a model with the n-th draw of the same model: effects come and go and change their order,
 * afterimage copies of a model grow in number; a draw of the last pair only moves with the character),
 * its transform pair (sa1_xlerp: relative to its anchor, or in the drawn root's frame with the motion's hint of how far
 * the model's node turns when the drawn action is steady; see above) and whether its local points changed; the welded
 * copies among the changed vertices (they keep following the vertex they copy). */
/* round 0.9.4a tests (sa1_api_world.h): the motion hint of the last captured pair */
void sa1w_test_hint_legacy(int on)
{
    g_hint_legacy = on != 0;
}

sa1_status sa1w_test_hint(const sa1_context *ctx, sa1_character_id id, sa1w_hint_info *out)
{
    const sa1_char *ch = get_char(ctx, id);
    const sa1_charset *cs;
    const sa1_rinfo *ri;
    const sa1_meshbuf *cur;
    uint32_t k;
    if (out == NULL)
        return SA1_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    out->action = out->eff = -1;
    out->node = -1;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    cs = loaded_set(ctx, ch->inst.kind);
    if (!cs || ch->rhave == 0)
        return SA1_ERR_NOT_LOADED;
    ri = &ch->rinfo[ch->rcur];
    cur = &ch->rb[ch->rcur];
    out->valid = ri->valid;
    out->dvalid = ri->dvalid;
    out->action = ri->action;
    out->eff = ri->eff;
    out->df = ri->df;
    if (ri->dobj >= 0 && (uint32_t)ri->dobj < cs->objects.count)
        out->dobj = SA1_VEC(cs->objects, sa1_object)[ri->dobj].addr & 0x1FFFFFFFu;
    if (ri->dmot >= 0 && (uint32_t)ri->dmot < cs->motions.count)
        out->dmot = SA1_VEC(cs->motions, sa1_motion)[ri->dmot].addr & 0x1FFFFFFFu;
    if (!ri->valid || ri->action < 0 || (uint32_t)ri->action >= cs->num_actions)
        return SA1_OK;
    for (k = 0; k < cur->nn; k++) {
        int32_t o = -1, m = -1, node;
        if (cur->node_fl[k] & SA1_NODE_OBJECT)
            continue;
        node = hint_node(cs, ri, cur->node_model[k], &o, &m);
        if (node >= 0 && o >= 0 && m >= 0) {
            out->model = cur->node_model[k];
            out->node = node;
            out->hobj = SA1_VEC(cs->objects, sa1_object)[o].addr & 0x1FFFFFFFu;
            out->hmot = SA1_VEC(cs->motions, sa1_motion)[m].addr & 0x1FFFFFFFu;
            break;
        }
    }
    /* the plan's turns against the drawn motion's (the owner's 0.9.4 note on runs at extreme speed) */
#if !defined(SA1_NO_WORLD)
    if (ch->rout.plan_ok && ch->rout.plan_gen == ch->rgen && ch->rhave > 1) {
        const sa1_rinfo *pi = &ch->rinfo[ch->rcur ^ 1];
        const sa1_rout *ro = &ch->rout;
        sa1_rinfo tb = *ri, ta = *pi;
        sa1w_pstate dp;
        int dfault;
        int32_t eff, obj = cs->actions[ri->action].object, mot = cs->actions[ri->action].motion, arch;
        float lift;
        double tdf = 0.0;
        uint32_t a;
        tb.eff = ri->action;
        tb.dobj = tb.dmot = -1;
        if (general_drawn(cs, ch, ri->action, &eff, &obj, &mot, &lift, &arch)) {
            tb.eff = eff;
            tb.dobj = obj;
            tb.dmot = mot;
        }
        ta.eff = tb.eff;
        ta.dobj = tb.dobj;
        ta.dmot = tb.dmot;
        /* the drawn frames whatever sa1w_test_hint_legacy says: this pair's from the world, the pair before's stays in
           its rinfo (legacy: its pair-end frame, as 0.9.4 used) - then estimated one step back by the drawn advance */
        if (pstate_of(ch, &dp, &dfault) && !dfault && dp.drawn_valid && dp.drawn_frame_valid &&
            (int32_t)dp.drawn_action == ri->action)
            tb.dframe = dp.drawn_frame;
        if (g_hint_legacy && tb.dframe != tb.frame)
            ta.dframe = pi->frame - (tb.frame - tb.dframe);
        if (!pi->valid || pi->action != ri->action || !frame_advance(cs, &ta, &tb, &tdf))
            return SA1_OK;
        out->tdf = tdf;
        for (a = 0; a < ro->norder; a++) {
            uint32_t kk = ro->order[a];
            const sa1_rnode *pn = &ro->plan[kk];
            int32_t oo = -1, mm = -1, node;
            double t, th, e;
            if (pn->prev < 0 || pn->mode == RN_VERTEX || pn->anchor >= 0 || pn->world ||
                (cur->node_fl[kk] & (SA1_NODE_OBJECT | SA1_NODE_SHADOW)))
                continue;
            node = hint_node(cs, &tb, cur->node_model[kk], &oo, &mm);
            if (node < 0)
                continue;
            t = sa1_pose_node_turn(cs, oo, mm, node, (double)ta.dframe, tdf);
            th = sa1_pose_node_turn(cs, oo, mm, node, (double)ta.dframe, 0.5 * tdf);
            if (t < 0.0 || th < 0.0)
                continue;
            out->checked++;
            if (pn->x.kind == SA1_XLERP_SNAP) {
                out->snapped++;
                out->wrong++;
                continue;
            }
            e = fabs(fabs(pn->x.phi) - t);
            if (e > out->worst)
                out->worst = e;
            if (e > 0.3490658503988659)     /* 20 degrees */
                out->wrong++;
            e = fabs(0.5 * fabs(pn->x.phi) - th);
            if (e > out->worst_mid)
                out->worst_mid = e;
        }
    }
#endif
    return SA1_OK;
}

static sa1_status plan_build(sa1_context *ctx, sa1_char *ch)
{
    sa1_rout *o = &ch->rout;
    const sa1_meshbuf *cur = &ch->rb[ch->rcur], *prev = &ch->rb[ch->rcur ^ 1];
    const sa1_charset *cs = loaded_set(ctx, ch->inst.kind);
    const sa1_rinfo *ci = &ch->rinfo[ch->rcur], *pi = &ch->rinfo[ch->rcur ^ 1];
    int hint = 0, same = same_topology(prev, cur);
    uint32_t k, v, nv = cur->nv, nn = cur->nn, nstat = 0, mask, a, b2;
    double eps = FOLLOW_EPS * (ctx->coords.scale > 0.0f ? (double)ctx->coords.scale : 1.0), csz = 2.0 * eps;
    double fr0[12], fr1[12], fi0[12], fi1[12];
    sa1_status st = plan_reserve(ctx, o, nn, nv);
    if (st != SA1_OK)
        return st;
    root_frame(ctx, ch->rpos[ch->rcur ^ 1], (double)ch->ryaw[ch->rcur ^ 1], fr0);
    root_frame(ctx, ch->rpos[ch->rcur], (double)ch->ryaw[ch->rcur], fr1);
    inv34(fr0, fi0);
    inv34(fr1, fi1);
    o->norder = 0;
    memset(&o->stats, 0, sizeof o->stats);
    o->stats.struct_size = sizeof o->stats;
    o->stats.models = nn;
    o->stats.same_topology = (uint32_t)same;
    if (cs && ci->dvalid && pi->valid && ci->action == pi->action && ci->action >= 0 &&
        (uint32_t)ci->action < cs->num_actions) {
        hint = 1;
        o->stats.frame_advance = (float)ci->df;
    }
    for (v = 0; v < nv; v++)
        o->pidx[v] = same ? v : PIDX_NONE;
    memset(o->vstat, 0, nv);
    /* round 0.9.4: game OBJECT draws matched by place (R-F1), their vertices marked (R-F2) */
    st = obj_match(ctx, o, prev, cur);
    if (st != SA1_OK)
        return st;
    obj_vertices(o, cur);
    for (k = 0; k < nn; k++) {
        sa1_rnode *pn = &o->plan[k];
        uint32_t v0 = cur->node_v[2u * k], n = cur->node_v[2u * k + 1u], pv0, i;
        int32_t pj = -1;
        int isobj = (cur->node_fl[k] & SA1_NODE_OBJECT) != 0;
        double turn = -1.0;
        pn->mode = RN_VERTEX;
        pn->prev = -1;
        pn->anchor = -1;
        pn->turn = -1.0;
        pn->group = 0;
        pn->world = 0;
        if (v0 > nv || n > nv - v0)
            continue;
        if (isobj) {
            /* R-F1: its draw in the pair before by place (also with the same topology), or none: then the last pair's
               draw as it is (R-F2), not the n-th draw of its model */
            o->stats.objects++;
            pj = o->omatch[k];
            if (pj < 0 || prev->node_v[2u * (uint32_t)pj] > prev->nv || n > prev->nv - prev->node_v[2u * (uint32_t)pj]) {
                for (i = 0; i < n; i++)
                    o->pidx[v0 + i] = PIDX_NONE;
                continue;
            }
            o->stats.object_matched++;
        } else if (same) {
            pj = (int32_t)k;
        } else {
            /* the n-th draw of its model with the n-th; round 0.9.4: counting only draws that are not OBJECT draws, in
               both pairs (a ring's blob shadow is no longer counted with the character's own) */
            uint32_t jj, occ = 0;
            for (jj = 0; jj < k; jj++)
                occ += cur->node_model[jj] == cur->node_model[k] && !(cur->node_fl[jj] & SA1_NODE_OBJECT);
            for (jj = 0; jj < prev->nn; jj++) {
                if (prev->node_model[jj] != cur->node_model[k] || (prev->node_fl[jj] & SA1_NODE_OBJECT))
                    continue;
                if (occ-- == 0) {
                    if (prev->node_v[2u * jj + 1u] == n &&
                        (prev->node_fl[jj] & ~SA1_NODE_FADE) == (cur->node_fl[k] & ~SA1_NODE_FADE))
                        pj = (int32_t)jj;
                    break;
                }
            }
            if (pj < 0)
                continue;
        }
        pv0 = prev->node_v[2u * (uint32_t)pj];
        if (pv0 > prev->nv || n > prev->nv - pv0)
            continue;
        pn->prev = pj;
        o->stats.matched++;
        if (!same || isobj)
            for (i = 0; i < n; i++)
                o->pidx[v0 + i] = pv0 + i;
        /* a draw matched across a fading constant material (Tails' opaque curled body after the translucent copy of a
           ball pair, or the copy after the body): the two are drawn at poses whose relative turn does not follow the
           motion's (60-155 degrees off; gsa1_070_tails.md Review R.3.3), so its hint would turn the body the long way
           round (260 degrees backwards): no hint, the shorter way. Round 0.9.4: an OBJECT draw has none (it is not one
           of the character's models) */
        if (hint && !isobj && !((prev->node_fl[pj] ^ cur->node_fl[k]) & SA1_NODE_FADE)) {
            int32_t object = -1, motion = -1, node = hint_node(cs, ci, cur->node_model[k], &object, &motion);
            if (node >= 0)
                turn = sa1_pose_node_turn(cs, object, motion, node, (double)pi->dframe, ci->df);
        }
        pn->turn = turn;
        o->order[o->norder++] = k;
    }
    /* the order of interpolation: matrix stack depth (stable), so a model's anchor candidates come before it */
    for (a = 1; a < o->norder; a++) {
        uint32_t w = o->order[a];
        int16_t dw = cur->node_depth[w];
        for (b2 = a; b2 > 0 && cur->node_depth[o->order[b2 - 1u]] > dw; b2--)
            o->order[b2] = o->order[b2 - 1u];
        o->order[b2] = w;
    }
    for (a = 0; a < o->norder; a++) {
        k = o->order[a];
        o->inv_ok[k] = (uint8_t)(inv34(prev->node_wm + 12u * (uint32_t)o->plan[k].prev, o->inv + 24u * k) &&
                                 inv34(cur->node_wm + 12u * k, o->inv + 24u * k + 12u));
    }
    for (a = 0; a < o->norder; a++) {
        sa1_rnode *pn;
        uint32_t v0, n, pv0, i;
        int32_t pj, best = -1;
        const double *w0, *w1;
        double bestsc = ANCHOR_MAX, l0[12], l1[12], bl0[12], bl1[12];
        k = o->order[a];
        pn = &o->plan[k];
        pj = pn->prev;
        v0 = cur->node_v[2u * k];
        n = cur->node_v[2u * k + 1u];
        pv0 = prev->node_v[2u * (uint32_t)pj];
        w0 = prev->node_wm + 12u * (uint32_t)pj;
        w1 = cur->node_wm + 12u * k;
        if (cur->node_fl[k] & SA1_NODE_OBJECT) {
            /* round 0.9.4 (R-F1): a game object's draw is interpolated in SA1 world space, where its task put it (no
               anchor, no hint: it does not move with the character) */
            pn->anchor = -1;
            pn->world = 1;
            if (sa1_xlerp_plan(w0, w1, -1.0, RENDER_MAX_ANGLE, &pn->x) == SA1_XLERP_NONE) {
                pn->world = 0;
                o->stats.vertex_lerp++;
                continue;
            }
            o->stats.world++;
        } else if (!(cur->node_fl[k] & SA1_NODE_SHADOW)) {
            for (b2 = 0; b2 < a; b2++) {
                uint32_t c2 = o->order[b2];
                double sc = 0.0;
                int e;
                /* round 0.9.4: never anchored to a game object's draw */
                if (o->plan[c2].mode == RN_VERTEX || !o->inv_ok[c2] || cur->node_tl[c2] != cur->node_tl[k] ||
                    (cur->node_fl[c2] & (SA1_NODE_SHADOW | SA1_NODE_OBJECT)))
                    continue;
                mul34(o->inv + 24u * c2, w0, l0);
                mul34(o->inv + 24u * c2 + 12u, w1, l1);
                for (e = 0; e < 12 && sc < bestsc; e++) {
                    double d = fabs(l0[e] - l1[e]) * ((e & 3) == 3 ? ANCHOR_TWEIGHT : 1.0);
                    if (d > sc)
                        sc = d;
                }
                if (sc < bestsc) {
                    bestsc = sc;
                    best = (int32_t)c2;
                    memcpy(bl0, l0, sizeof bl0);
                    memcpy(bl1, l1, sizeof bl1);
                }
            }
        }
        if (cur->node_fl[k] & SA1_NODE_OBJECT) {
            /* round 0.9.4: planned above, in SA1 world space */
        } else if (best >= 0 && sa1_xlerp_plan(bl0, bl1, -1.0, RENDER_MAX_ANGLE, &pn->x) != SA1_XLERP_NONE) {
            /* rigidly attached to `best`: its transform relative to it */
            pn->anchor = best;
            o->stats.anchored++;
        } else {
            /* in the drawn root's frame, with the motion's hint (relative to the object's root, which is what F^-1 * M
               measures once the character's turn is factored out) */
            double q0[12], q1[12];
            pn->anchor = -1;
            mul34(fi0, w0, q0);
            mul34(fi1, w1, q1);
            if (!(cur->node_fl[k] & SA1_NODE_SHADOW) && prefer_world(w0, w1, q0, q1) &&
                !charset_model(cs, cur->node_model[k])) {
                /* F5: in SA1 world space (no motion hint: the node's turn is relative to the character) */
                pn->world = 1;
                sa1_xlerp_plan(w0, w1, -1.0, RENDER_MAX_ANGLE, &pn->x);
            } else {
                /* across a fading material: the shorter way up to half a turn, never SNAP (whose chord would move a body
                   turning about a centre off its origin out of place) */
                double maxa = ((prev->node_fl[pj] ^ cur->node_fl[k]) & SA1_NODE_FADE) ? 3.14159265358979323846
                                                                                      : RENDER_MAX_ANGLE;
                sa1_xlerp_plan(q0, q1, pn->turn, maxa, &pn->x);
            }
            if (pn->x.kind == SA1_XLERP_NONE) {
                pn->world = 0;
                o->stats.vertex_lerp++;
                continue;
            }
            if (pn->world) {
                o->stats.world++;
            } else {
                o->stats.root_frame++;
            }
            if (!pn->world && pn->turn >= 0.0) {
                o->stats.hinted++;
                if (fabs(pn->x.phi) > 3.14159265358979323846)
                    o->stats.over_half_turn++;
            }
        }
        if (pn->x.kind == SA1_XLERP_SNAP)
            o->stats.snapped++;
        if (prev->node_key[pj] == cur->node_key[k] &&
            memcmp(prev->lpos + 3u * pv0, cur->lpos + 3u * v0, 12u * (size_t)n) == 0 &&
            memcmp(prev->lnrm + 3u * pv0, cur->lnrm + 3u * v0, 12u * (size_t)n) == 0) {
            pn->mode = RN_RIGID;
            o->stats.rigid++;
            memset(o->vstat + v0, 1, n);
            nstat += n;
        } else {
            pn->mode = RN_LOCAL;
            o->stats.local++;
            for (i = 0; i < n; i++) {
                if (memcmp(prev->lpos + 3u * (pv0 + i), cur->lpos + 3u * (v0 + i), 12) == 0 &&
                    memcmp(prev->lnrm + 3u * (pv0 + i), cur->lnrm + 3u * (v0 + i), 12) == 0) {
                    o->vstat[v0 + i] = 1;
                    nstat++;
                }
            }
        }
    }
    /* the rigid groups: how many drawn models hang on each model through their anchors (an anchor comes earlier in the
       order, so the chains have no cycles); the roots of the larger ones in the drawn root's frame move along the screw */
    for (a = 0; a < o->norder; a++) {
        int32_t j = o->plan[o->order[a]].anchor, guard = 0;
        while (j >= 0 && (uint32_t)j < nn && guard++ < 256) {
            o->plan[j].group++;
            j = o->plan[j].anchor;
        }
    }
    for (a = 0; a < o->norder; a++) {
        const sa1_rnode *pn = &o->plan[o->order[a]];
        if (pn->anchor < 0 && !pn->world && pn->mode != RN_VERTEX && pn->x.kind == SA1_XLERP_SLERP &&
            pn->group >= SCREW_MIN_GROUP)
            o->stats.screw++;
    }
    /* welded copies: the unchanged vertices in a hash of their cells, every other matched vertex of a draw looked up */
    o->nfol = 0;
    mask = o->cap_hash - 1u;
    memset(o->hash, 0, (size_t)o->cap_hash * sizeof(uint32_t));
    if (nstat) {
        for (v = 0; v < nv; v++) {
            int64_t c[3];
            uint32_t h;
            if (!o->vstat[v])
                continue;
            cell_of(cur->pos + 3u * v, csz, c);
            h = cell_hash(c) & mask;
            while (o->hash[h])
                h = (h + 1u) & mask;
            o->hash[h] = v + 1u;
        }
        for (k = 0; k < nn; k++) {
            uint32_t v0 = cur->node_v[2u * k], n = cur->node_v[2u * k + 1u];
            if (o->plan[k].prev < 0)
                continue;
            for (v = v0; v < v0 + n; v++) {
                int64_t c[3], q[3];
                int dx, dy, dz, found = 0;
                if (o->vstat[v] || o->pidx[v] == PIDX_NONE || o->nfol >= o->cap_fol)
                    continue;
                cell_of(cur->pos + 3u * v, csz, c);
                for (dx = -1; dx <= 1 && !found; dx++) {
                    for (dy = -1; dy <= 1 && !found; dy++) {
                        for (dz = -1; dz <= 1 && !found; dz++) {
                            uint32_t h;
                            q[0] = c[0] + dx;
                            q[1] = c[1] + dy;
                            q[2] = c[2] + dz;
                            h = cell_hash(q) & mask;
                            while (o->hash[h] && !found) {
                                uint32_t r = o->hash[h] - 1u, pv = o->pidx[v], pr = o->pidx[r];
                                int64_t rc[3];
                                cell_of(cur->pos + 3u * r, csz, rc);
                                if (rc[0] == q[0] && rc[1] == q[1] && rc[2] == q[2] && pr != PIDX_NONE &&
                                    near3(cur->pos + 3u * v, cur->pos + 3u * r, eps) &&
                                    near3(prev->pos + 3u * pv, prev->pos + 3u * pr, eps)) {
                                    sa1_rfollow *f = &o->fol[o->nfol++];
                                    int m;
                                    f->v = v;
                                    f->r = r;
                                    for (m = 0; m < 3; m++) {
                                        f->off0[m] = prev->pos[3u * pv + m] - prev->pos[3u * pr + m];
                                        f->off1[m] = cur->pos[3u * v + m] - cur->pos[3u * r + m];
                                    }
                                    f->nrm = near3(cur->nrm + 3u * v, cur->nrm + 3u * r, 1e-3) &&
                                             near3(prev->nrm + 3u * pv, prev->nrm + 3u * pr, 1e-3);
                                    found = 1;
                                }
                                h = (h + 1u) & mask;
                            }
                        }
                    }
                }
            }
        }
    }
    o->stats.followers = o->nfol;
    /* round 0.9.4 (R-F3): the game objects' sprites matched by place (render_build lerps them) */
    o->nsobj = 0;
    if (cur->nspr) {
        st = sobj_reserve(ctx, o, cur->nspr > prev->nspr ? cur->nspr : prev->nspr);
        if (st != SA1_OK)
            return st;
        o->nsobj = obj_sprites(cur, o->sobj);
        if (o->nsobj) {
            obj_sprites(prev, o->sobj + o->cap_sobj);
            st = obj_sprite_match(ctx, o, prev, cur, o->sobj, o->sobj + o->cap_sobj);
            if (st != SA1_OK)
                return st;
        }
    }
    o->plan_gen = ch->rgen;
    o->plan_ok = 1;
    return SA1_OK;
}

/* how far the character's interpolated position at alpha lies from its position in the last pair (host space): what
 * a draw of the last pair only moves by, so that it stays with the character */
static void root_shift(const sa1_char *ch, float alpha, float out[3])
{
    int32_t pi = ch->rcur ^ 1, ci = ch->rcur, k;
    for (k = 0; k < 3; k++)
        out[k] = ch->rhave > 1 ? (ch->rpos[pi][k] - ch->rpos[ci][k]) * (1.0f - alpha) : 0.0f;
}

/* every vertex of the last pair at alpha into o->vpos / vnrm (plan_build first): matched vertices from the pair before
 * (alpha 0: exactly) towards the last pair, the others as the last pair drew them, moved with the character; returns
 * the number of those moved with the character (sa1_render.flags SA1_RENDER_MOVED_WITH_ROOT) */
static uint32_t render_interp(sa1_context *ctx, sa1_char *ch, float alpha)
{
    sa1_rout *o = &ch->rout;
    const sa1_meshbuf *cur = &ch->rb[ch->rcur], *prev = &ch->rb[ch->rcur ^ 1];
    uint32_t k, v, i, a, nv = cur->nv, moved = 0;
    float shift[3], fa_pos[3];
    double fa[12];
    root_shift(ch, alpha, shift);
    for (v = 0; v < nv; v++) {
        uint32_t p = o->pidx[v];
        if (p == PIDX_NONE && o->vobj[v]) {
            /* round 0.9.4 (R-F2): a game object's draw new in the last pair stays where the game drew it */
            memcpy(o->vpos + 3u * v, cur->pos + 3u * v, 3 * sizeof(float));
            memcpy(o->vnrm + 3u * v, cur->nrm + 3u * v, 3 * sizeof(float));
        } else if (p == PIDX_NONE) {
            moved++;
            o->vpos[3u * v] = cur->pos[3u * v] + shift[0];
            o->vpos[3u * v + 1u] = cur->pos[3u * v + 1u] + shift[1];
            o->vpos[3u * v + 2u] = cur->pos[3u * v + 2u] + shift[2];
            memcpy(o->vnrm + 3u * v, cur->nrm + 3u * v, 3 * sizeof(float));
        } else if (alpha <= 0.0f) {
            memcpy(o->vpos + 3u * v, prev->pos + 3u * p, 3 * sizeof(float));
            memcpy(o->vnrm + 3u * v, prev->nrm + 3u * p, 3 * sizeof(float));
        } else {
            lerp3(prev->pos + 3u * p, cur->pos + 3u * v, alpha, o->vpos + 3u * v);
            nlerp3(prev->nrm + 3u * p, cur->nrm + 3u * v, alpha, o->vnrm + 3u * v);
        }
    }
    {
        /* a blob shadow of the last pair only (a spawn, the ground coming back into the probe's reach while falling)
         * moves with the character horizontally only: the game put it on the ground it found, and the root's vertical
         * move would hold it up to one pair's fall above or below that ground (gsa1_060_shadow.md Review R.3.4) */
        float up0[3] = { 0.0f, 1.0f, 0.0f }, up[3], d, sh[3];
        sa1_to_host_dir(ctx, up0, up);
        d = shift[0] * up[0] + shift[1] * up[1] + shift[2] * up[2];
        sh[0] = shift[0] - d * up[0];
        sh[1] = shift[1] - d * up[1];
        sh[2] = shift[2] - d * up[2];
        for (k = 0; k < cur->nn; k++) {
            uint32_t v0 = cur->node_v[2u * k], n = cur->node_v[2u * k + 1u];
            if (!(cur->node_fl[k] & SA1_NODE_SHADOW) || (cur->node_fl[k] & SA1_NODE_OBJECT) || o->plan[k].prev >= 0 ||
                v0 > nv || n > nv - v0)
                continue;       /* round 0.9.4: a game object's blob shadow stays where the game drew it (R-F2) */
            for (v = v0; v < v0 + n; v++) {
                if (o->pidx[v] != PIDX_NONE)
                    continue;
                o->vpos[3u * v] = cur->pos[3u * v] + sh[0];
                o->vpos[3u * v + 1u] = cur->pos[3u * v + 1u] + sh[1];
                o->vpos[3u * v + 2u] = cur->pos[3u * v + 2u] + sh[2];
            }
        }
    }
    if (alpha <= 0.0f)
        return moved;
    /* the drawn root's frame at alpha (sa1_render.position / yaw) */
    lerp3(ch->rpos[ch->rcur ^ 1], ch->rpos[ch->rcur], alpha, fa_pos);
    root_frame(ctx, fa_pos, root_yaw_at(ch, alpha), fa);
    /* the models in the plan's order: an anchor's matrix at alpha is there before the models anchored to it */
    for (a = 0; a < o->norder; a++) {
        const sa1_rnode *pn;
        uint32_t v0, n, pv0;
        int normals;
        double rel[12], *m;
        k = o->order[a];
        pn = &o->plan[k];
        if (pn->prev < 0 || pn->mode == RN_VERTEX)
            continue;
        v0 = cur->node_v[2u * k];
        n = cur->node_v[2u * k + 1u];
        normals = (cur->node_fl[k] & SA1_NODE_NORMALS) != 0;
        m = o->nm + 12u * k;
        pv0 = prev->node_v[2u * (uint32_t)pn->prev];
        if (pn->anchor < 0 && !pn->world && pn->group >= SCREW_MIN_GROUP)
            xlerp_screw_at(&pn->x, (double)alpha, rel);     /* a rigid group's root in the drawn root's frame */
        else
            sa1_xlerp_at(&pn->x, (double)alpha, rel);
        if (pn->world)
            memcpy(m, rel, sizeof rel);                     /* F5: in SA1 world space */
        else
            mul34(pn->anchor >= 0 ? o->nm + 12u * (uint32_t)pn->anchor : fa, rel, m);
        for (v = v0; v < v0 + n; v++) {
            float lp[3], ln[3], w[3], nw[3];
            if (pn->mode == RN_RIGID || o->vstat[v]) {
                memcpy(lp, cur->lpos + 3u * v, sizeof lp);
                memcpy(ln, cur->lnrm + 3u * v, sizeof ln);
            } else {
                lerp3(prev->lpos + 3u * (pv0 + v - v0), cur->lpos + 3u * v, alpha, lp);
                lerp3(prev->lnrm + 3u * (pv0 + v - v0), cur->lnrm + 3u * v, alpha, ln);
            }
            for (i = 0; i < 3; i++)
                w[i] = (float)(m[4 * i] * lp[0] + m[4 * i + 1] * lp[1] + m[4 * i + 2] * lp[2] + m[4 * i + 3]);
            sa1_to_host_point(ctx, w, o->vpos + 3u * v);
            nw[0] = 0.0f;
            nw[1] = 1.0f;
            nw[2] = 0.0f;
            if (normals) {
                double ox = m[0] * ln[0] + m[1] * ln[1] + m[2] * ln[2], oy = m[4] * ln[0] + m[5] * ln[1] + m[6] * ln[2];
                double oz = m[8] * ln[0] + m[9] * ln[1] + m[10] * ln[2], l = sqrt(ox * ox + oy * oy + oz * oz);
                if (l > 1e-20 && l == l) {
                    nw[0] = (float)(ox / l);
                    nw[1] = (float)(oy / l);
                    nw[2] = (float)(oz / l);
                }
            }
            sa1_to_host_dir(ctx, nw, o->vnrm + 3u * v);
        }
    }
    for (i = 0; i < o->nfol; i++) {
        const sa1_rfollow *f = &o->fol[i];
        float d[3];
        lerp3(f->off0, f->off1, alpha, d);
        o->vpos[3u * f->v] = o->vpos[3u * f->r] + d[0];
        o->vpos[3u * f->v + 1u] = o->vpos[3u * f->r + 1u] + d[1];
        o->vpos[3u * f->v + 2u] = o->vpos[3u * f->r + 2u] + d[2];
        if (f->nrm)
            memcpy(o->vnrm + 3u * f->v, o->vnrm + 3u * f->r, 3 * sizeof(float));
    }
    return moved;
}

static sa1_status render_build(sa1_context *ctx, sa1_char *ch, float alpha, const sa1_render **out)
{
    sa1_rout *o = &ch->rout;
    sa1_render *r = &o->r;
    const sa1_meshbuf *cur = NULL, *prev = NULL;
    const sa1_meshbuf *xs = NULL;   /* the blob shadow of a hidden pair, appended to the CPU pose */
    mesh_src src;
    uint32_t nt, nd, ns, nl, t, k, i, moved = 0, xt = 0, xd = 0;
    int interp = 0, lerp_ok = 0, same = 0;
    sa1_status st;
    if (!(alpha == alpha))
        alpha = 1.0f;
    if (alpha < 0.0f)
        alpha = (float)(ch->acc * 30.0);
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    if (ch->rhave) {
        cur = &ch->rb[ch->rcur];
        if (ch->rhave > 1)
            prev = &ch->rb[ch->rcur ^ 1];
        buf_src(cur, &src);
        lerp_ok = prev != NULL && alpha < 1.0f;
        same = lerp_ok && same_topology(prev, cur);
        ns = cur->nspr;
        nl = cur->nlv;
    } else {
        if (ch->inst.geom.num_triangles == 0 && ch->inst.geom.positions == NULL) {
            st = pose_eval_current(ctx, ch);
            if (st != SA1_OK)
                return st;
        }
        st = pose_src(ctx, ch, &src);
        if (st != SA1_OK)
            return st;
        ns = 0;
        nl = 0;
        if (ch->rshadow_on) {
            xs = &ch->rshadow;
            xt = xs->nt;
            xd = xs->ndraw;
        }
    }
    nd = src.ndraw;
    nt = 0;
    for (i = 0; i < nd; i++)
        if (src.draws[i].prim == SA1_PRIM_TRIANGLES && src.draws[i].first + src.draws[i].count > nt)
            nt = src.draws[i].first + src.draws[i].count;
    st = rout_reserve(ctx, o, nt + xt, nd + xd, ns, nl);
    if (st != SA1_OK)
        return st;
    if (lerp_ok) {
        /* per drawn model: its transform interpolated (the model-local points as drawn), not its vertices */
        if (!o->plan_ok || o->plan_gen != ch->rgen) {
            st = plan_build(ctx, ch);
            if (st != SA1_OK)
                return st;
        }
        moved = render_interp(ctx, ch, alpha);
        interp = same || o->stats.matched > 0;
        for (t = 0; t < nt; t++) {
            for (k = 0; k < 3; k++) {
                uint32_t v = src.idx[3u * t + k];
                memcpy(o->pos + 9u * t + 3u * k, o->vpos + 3u * v, 3 * sizeof(float));
                memcpy(o->nrm + 9u * t + 3u * k, o->vnrm + 3u * v, 3 * sizeof(float));
            }
        }
    } else {
        for (t = 0; t < nt; t++) {
            for (k = 0; k < 3; k++) {
                uint32_t v = src.idx[3u * t + k];
                memcpy(o->pos + 9u * t + 3u * k, src.pos + 3u * v, 3 * sizeof(float));
                memcpy(o->nrm + 9u * t + 3u * k, src.nrm + 3u * v, 3 * sizeof(float));
            }
        }
    }
    if (nt) {
        memcpy(o->uv, src.uv, 6u * sizeof(float) * nt);
        memcpy(o->col, src.col, 12u * (size_t)nt);
    }
    if (nd)
        memcpy(o->draws, src.draws, nd * sizeof(sa1_draw));
    if (xs) {
        /* a hidden pair: the CPU pose, then the blob shadow the game drew (render_capture), in that order */
        uint32_t ord = 0;
        for (i = 0; i < nd; i++)
            if (o->draws[i].order >= ord)
                ord = o->draws[i].order + 1u;
        for (t = 0; t < xt; t++) {
            for (k = 0; k < 3; k++) {
                uint32_t v = xs->idx[3u * t + k];
                memcpy(o->pos + 9u * (nt + t) + 3u * k, xs->pos + 3u * v, 3 * sizeof(float));
                memcpy(o->nrm + 9u * (nt + t) + 3u * k, xs->nrm + 3u * v, 3 * sizeof(float));
            }
        }
        if (xt) {
            memcpy(o->uv + 6u * nt, xs->uv, 6u * sizeof(float) * xt);
            memcpy(o->col + 12u * nt, xs->col, 12u * (size_t)xt);
        }
        for (i = 0; i < xd; i++) {
            o->draws[nd + i] = xs->draws[i];
            o->draws[nd + i].first += nt;
            o->draws[nd + i].order = ord + i;
        }
        nt += xt;
        nd += xd;
    }
    {
        /* sprites and line segments: lerped when the pairs drew the same ones, else as drawn, moved with the character.
           Round 0.9.4 (R-F3): a game object's sprite lerps from its OBJECT sprite of the pair before matched by place
           (plan_build), or is drawn where the game drew it */
        float shift[3] = { 0.0f, 0.0f, 0.0f };
        const uint8_t *sobj = lerp_ok && o->nsobj ? o->sobj : NULL;
        if (lerp_ok && !same) {
            root_shift(ch, alpha, shift);
            moved += ns - (sobj ? o->nsobj : 0u) + nl;
        }
        for (i = 0; i < ns; i++) {
            o->spr[i] = cur->spr[i];
            if (sobj && sobj[i]) {
                if (o->smatch[i] >= 0)
                    lerp3(prev->spr[o->smatch[i]].position, cur->spr[i].position, alpha, o->spr[i].position);
            } else if (same) {
                lerp3(prev->spr[i].position, cur->spr[i].position, alpha, o->spr[i].position);
            } else {
                o->spr[i].position[0] += shift[0];
                o->spr[i].position[1] += shift[1];
                o->spr[i].position[2] += shift[2];
            }
        }
        for (i = 0; i < nl; i++) {
            o->lv[i] = cur->lv[i];
            if (same) {
                lerp3(prev->lv[i].position, cur->lv[i].position, alpha, o->lv[i].position);
            } else {
                o->lv[i].position[0] += shift[0];
                o->lv[i].position[1] += shift[1];
                o->lv[i].position[2] += shift[2];
            }
        }
    }
    memset(r, 0, sizeof *r);
    r->struct_size = sizeof *r;
    r->generation = ch->rgen;
    r->alpha = alpha;
    r->interpolated = (uint32_t)interp;
    r->flags = moved ? SA1_RENDER_MOVED_WITH_ROOT : 0u;
    if (ch->rhave > 1 && alpha < 1.0f) {
        int32_t pi = ch->rcur ^ 1, ci = ch->rcur;
        double dy = (double)ch->ryaw[ci] - ch->ryaw[pi];
        if (ch->rdrawn[pi] && ch->rdrawn[ci])
            r->flags |= SA1_RENDER_DRAWN_ROOT;
        while (dy > 3.14159265358979323846)
            dy -= 6.28318530717958647692;
        while (dy < -3.14159265358979323846)
            dy += 6.28318530717958647692;
        lerp3(ch->rpos[pi], ch->rpos[ci], alpha, r->position);
        r->yaw = (float)(ch->ryaw[pi] + dy * alpha);
    } else if (ch->rhave) {
        memcpy(r->position, ch->rpos[ch->rcur], sizeof r->position);
        r->yaw = ch->ryaw[ch->rcur];
        if (ch->rdrawn[ch->rcur])
            r->flags |= SA1_RENDER_DRAWN_ROOT;
    } else {
        memcpy(r->position, ch->inst.position, sizeof r->position);
        r->yaw = ch->inst.yaw;
    }
    r->num_triangles = nt;
    r->positions = o->pos;
    r->normals = o->nrm;
    r->uvs = o->uv;
    r->colors = o->col;
    r->num_draws = nd;
    r->draws = o->draws;
    r->num_sprites = ns;
    r->sprites = o->spr;
    r->num_line_vertices = nl;
    r->line_vertices = o->lv;
    *out = r;
    return SA1_OK;
}
sa1_status sa1_character_lerp_stats(const sa1_context *ctx, sa1_character_id id, sa1_lerp_stats *out)
{
    sa1_char *ch = get_char(ctx, id);
    if (!out)
        return SA1_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    out->struct_size = sizeof *out;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    if (ch->rout.plan_ok && ch->rout.plan_gen == ch->rgen)
        *out = ch->rout.stats;
    return SA1_OK;
}

sa1_status sa1_character_render(sa1_context *ctx, sa1_character_id id, float alpha, const sa1_render **out)
{
    sa1_char *ch = get_char(ctx, id);
    if (!out)
        return SA1_ERR_INVALID_ARGUMENT;
    *out = NULL;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    return render_build(ctx, ch, alpha, out);
}

sa1_status sa1_character_mesh_lerp(sa1_context *ctx, sa1_character_id id, float alpha, sa1_mesh *mesh_out)
{
    sa1_char *ch = get_char(ctx, id);
    const sa1_render *r = NULL;
    mesh_src s;
    sa1_status st;
    if (!mesh_out || mesh_out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    if (!ch)
        return SA1_ERR_NOT_FOUND;
    st = render_build(ctx, ch, alpha, &r);
    if (st != SA1_OK)
        return st;
    memset(&s, 0, sizeof s);
    s.pos = r->positions;
    s.nrm = r->normals;
    s.uv = r->uvs;
    s.col = r->colors;
    s.draws = r->draws;
    s.ndraw = r->num_draws;
    return write_mesh_src(&s, mesh_out);
}
/* ------------------------------------------------------------------------- */
/* Events (design 7.4)                                                       */
/* ------------------------------------------------------------------------- */

#if !defined(SA1_NO_WORLD)
/* event types whose position, when the game gave none, is the character's */
static int event_at_character(uint32_t type)
{
    return type == SA1_EVENT_SOUND || type == SA1_EVENT_SOUND_STOP || type == SA1_EVENT_VOICE;
}
#endif

/* The events of every ticking character (7.4): the boundary stubs (gen/policy/stubs.tsv "event" lines: sound,
 * voice, jingle, rumble, restart, respawn) and the world core (faults, unimplemented calls) queue them per world
 * with SA1-space positions; here they get the character id and host-space positions. */
sa1_status sa1_poll_events(sa1_context *ctx, sa1_event *out, uint32_t capacity, uint32_t *count)
{
    uint32_t n = 0, i;
    if (!ctx || !count || (capacity && !out))
        return SA1_ERR_INVALID_ARGUMENT;
#if !defined(SA1_NO_WORLD)
    for (i = 0; i < SA1_MAX_INSTANCES && n < capacity; i++) {
        sa1_char *ch = ctx->inst[i] ? (sa1_char *)(void *)ctx->inst[i] : NULL;
        uint32_t got, k;
        if (!ch || !ch->world)
            continue;
        got = sa1w_world_events(ch->world, out + n, capacity - n);
        for (k = 0; k < got; k++) {
            sa1_event *e = &out[n + k];
            float p[3];
            memcpy(p, e->position, sizeof p);
            if (p[0] == 0.0f && p[1] == 0.0f && p[2] == 0.0f && event_at_character(e->type)) {
                /* a sound without a position of its own (8C0940DA play SE, 8C0945A0 stop by id, voices): at the
                 * character (its state after the tick that queued it) */
                sa1w_pstate ps;
                int faulted;
                if (pstate_of(ch, &ps, &faulted))
                    memcpy(p, ps.pos, sizeof p);
            }
            sa1_to_host_point(ctx, p, e->position);
            e->character = (sa1_character_id)(((uint32_t)ctx->inst_gen[i] << 16) | (i + 1u));
        }
        n += got;
    }
#else
    (void)i;
#endif
    *count = n;
    return SA1_OK;
}
