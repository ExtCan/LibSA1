/*
 * sa1_coladapt.h - the collision adapter (sadc docs/libsa1_integration.md section 5). Internal.
 *
 * The decompiled land collision code (cache build 8C028EC2, gathers 8C052518 / 8C0522C0 / 8C052E30,
 * probes, CCL, shadows) runs unchanged on an _OBJ_LANDTABLE in Dreamcast layout. The adapter only
 * produces that landtable:
 *
 *   DISC      the stage's own landtable, used in place: the stage overlay (STGnn.PRS, PRS-unpacked,
 *             SHA-1 checked) is loaded at 0x0C900000 in the world image and B_8C7596F4 points at
 *             D_8C19E9FC[level][act] (EC1: 0x0C981554). No synthesis; this is the test oracle.
 *   FAITHFUL  one NJS_OBJECT per group (= per original COL), in group order, with the group's own
 *             evalflags / pos / ang / scl, one NJS_MODEL with the group's object-space points and
 *             center / r, the triangles as meshsets in the given order, one NJS_MATERIAL per distinct
 *             material word, and a COL entry with the group's sphere, raw fields and attribute.
 *             From a landtable dump (sa1ca_dump_load) the game's polygon cache comes out bit-identical
 *             to DISC's (tests/coladapt_test.c), because every input of the cache build is the same:
 *             the same matrix (T * Rz * Ry * Rx * S of the same floats), the same njCalcPoint inputs,
 *             the same triangle order and the same corner order of the centroid sums.
 *   GRID      production (design 5.4): the host's triangles in world space, longer ones cut into well-shaped
 *             pieces of at most max_edge, each in the cell (cube of cell_size) of its centroid (SA1_GRID_CUT: cut
 *             at the cell boundaries), one object per (cell, COL attribute word, orientation class - floor / wall /
 *             ceiling, round 0.9.2; fuller cells in parts of at most 1024 triangles) in the static collision segment
 *             (sa1ca_build_grid), plus a column index; each world keeps a window
 *             landtable of the objects nearest to its character, refreshed inside every step before the
 *             land collision update (src/world/sa1_coladapt_world.c, sa1ca_world_attach). Kill planes
 *             become the world's death zones (DeathZoneTable + the death zone task 8C07BAC0).
 *   moving    sa1_collision_object_*: per world, an NJS_OBJECT per collidable surface word in the
 *             extension segment, registered as DynamicCOL entries (8C0528AE / 8C052940) of a proxy task
 *             whose force work carries a character standing on it (8C07506E) (design 5.5).
 *
 * Triangle encoding (FAITHFUL). 8C0287A0 computes a triangle's centroid as (v[i] + v[i+1]) + v[i+2]
 * in STRIP order and stores the corners in winding order: (v[i], v[i+1], v[i+2]) when (flip + i) is
 * even, (v[i], v[i+2], v[i+1]) when it is odd. Float addition is not associative under the game's
 * round-toward-zero mode, so a triangle that sat at an odd strip position must be re-emitted at an odd
 * strip position to give the same centroid (and radius) bits. The adapter therefore writes
 *   even triangles (a, b, c)   as type-0 meshsets (3 indices each), and
 *   odd triangles  (a, b, c)   (SA1_COLTRI_STRIP_ODD) as strip meshsets (type 0x8000) of one-triangle
 *                              strips {0x8003, a, c, b}: flip 1, i = 0, so the game emits (a, b, c) and
 *                              sums a + c + b, exactly like the original strip.
 * Every probe that walks models directly (8C02770E, 8C02B620, 8C02BC60, 8C02C100, 8C02CA00) uses the
 * same (i + flip) & 1 rule on the same strip buffer, so they see the same corner order too.
 *
 * Memory: the synthesized data goes wherever an sa1ca_alloc_fn puts it; the world core passes its
 * static collision segment (guest 0x88000000, shared by the worlds of a context:
 * sa1g_colseg_alloc). Nothing here depends on the world core (the src/ C files are plain C99), so the world
 * glue is:
 *     static uint32_t colseg_alloc(void *u, size_t n, size_t a, void **h)
 *     { return sa1g_colseg_alloc((sa1g_shared *)u, n, a, h); }
 *     sa1ca_build(ctx, colseg_alloc, &wctx->shared, &built);      once per generation
 *     sa1g_world_wr32(w, SA1CA_ADDR_LANDTABLE, built.landtable);  per world, then 8C028E60 + one update
 * (D_8C19E978 = 1, D_8C19E96C = 400.0f and D_8C196044 = 0 are the image's own values).
 */
#ifndef SA1_COLADAPT_H
#define SA1_COLADAPT_H

#include "sa1_internal.h"

/* ---- Dreamcast layout of the landtable data (little-endian, 4-byte aligned) ------------------- */
#define SA1CA_LANDTABLE_SIZE 0x24u  /* _OBJ_LANDTABLE: ssCount s16 +0x00, ssMotCount +0x02, ssAttribute +0x04,
                                       ssLoadFlag +0x06, fFarClipping +0x08, pLandEntry +0x0C, pMotLandEntry
                                       +0x10, pPvmFileName +0x14, pTexList +0x18, pBinaryFileName +0x1C,
                                       pLoadBinary +0x20 */
#define SA1CA_COL_SIZE       0x24u  /* _OBJ_LANDENTRY: center +0x00, xWidth (radius) +0x0C, yWidth +0x10,
                                       zWidth +0x14, pObject +0x18, blockbit +0x1C, slAttribute +0x20 */
#define SA1CA_OBJECT_SIZE    0x34u  /* NJS_OBJECT: evalflags, model +0x04, pos +0x08, ang +0x14, scl +0x20,
                                       child +0x2C, sibling +0x30 */
#define SA1CA_MODEL_SIZE     0x28u  /* NJS_MODEL: points, normals +0x04, nbPoint +0x08, meshsets +0x0C,
                                       mats +0x10, nbMeshset u16 +0x14, nbMat u16 +0x16, center +0x18, r +0x24 */
#define SA1CA_MESHSET_SIZE   0x18u  /* NJS_MESHSET: type_matId u16, nbMesh u16 +0x02, meshes +0x04, attrs,
                                       normals, vertcolor, vertuv */
#define SA1CA_MATERIAL_SIZE  0x14u  /* NJS_MATERIAL: diffuse, specular, exponent, attr_texId, attrflags +0x10 */

#define SA1CA_COL_COLLIDABLE 0x400003u  /* COL bits the gathers 8C052518 / 8C052E30 take */
#define SA1CA_MESH_STRIP     0x8000u    /* NJS_MESHSET type: triangle strips */
#define SA1CA_STRIP_FLIP     0x8000u    /* strip header: first triangle has reversed winding */
#define SA1CA_MATID_MAX      0x3FFFu    /* type_matId & 0x3FFF */

/* ---- game globals of the landtable path -------------------------------------------------------- */
#define SA1CA_ADDR_LANDTABLE    0x8C7596F4u  /* B_8C7596F4: current landtable (read by 8C052518) */
#define SA1CA_ADDR_GATHER_ON    0x8C19E978u  /* D_8C19E978: 1 = 8C052518 gathers landtable entries */
#define SA1CA_ADDR_GATHER_RANGE 0x8C19E96Cu  /* D_8C19E96C: gather range of player 1 (400.0f) */
#define SA1CA_ADDR_GATHER_ALL   0x8C196044u  /* D_8C196044: 1 = next update gathers all (8C052E30) */
#define SA1CA_ADDR_LANDTABLES   0x8C19E9FCu  /* D_8C19E9FC[level][8]: landtable of each stage and act */
#define SA1CA_STAGE_LEVELS      12
#define SA1CA_STAGE_ACTS        8
/* round 6a (super_sonic.md Review R.3.2): DISC mode also takes the Perfect Chaos stage, level 19, whose overlay at
   0x0C900000 is B_CHAOS7.PRS (the landtable D_8C19E9FC[19][0] = 0x0C901214 lies in it) */
#define SA1CA_LEVEL_SUPER       19
#define SA1CA_DISC_LEVEL_OK(l)  (((l) >= 0 && (l) < SA1CA_STAGE_LEVELS) || (l) == SA1CA_LEVEL_SUPER)
#define SA1CA_STAGE_SLOT        0x0C900000u  /* load address of STGnn.PRS */

/* ---- GRID (design 5.4) ----------------------------------------------------------------------------- */
#define SA1CA_GRID_MAGIC      0x31445247u   /* "GRD1" */
#define SA1CA_GRID_HDR_SIZE   0x44u
/* GRID header in the static collision segment (little-endian words, read by the world's adapter) */
#define SA1CA_GH_MAGIC        0x00u
#define SA1CA_GH_GENERATION   0x04u
#define SA1CA_GH_TOKEN        0x08u
#define SA1CA_GH_CELL         0x0Cu     /* float cell size */
#define SA1CA_GH_OBJECTS      0x10u     /* land objects (COL entries) */
#define SA1CA_GH_COLS         0x14u     /* guest: COL[objects], sorted by column (ix, iz), then iy, attribute */
#define SA1CA_GH_POLYS        0x18u     /* guest: u32 triangles per COL */
#define SA1CA_GH_HASH         0x1Cu     /* guest: column hash, SA1CA_GRID_HASH_ENTRY bytes per entry */
#define SA1CA_GH_HASH_SIZE    0x20u     /* entries (power of 2; 0 = no land) */
#define SA1CA_GH_MAX_RADIUS   0x24u     /* float: largest COL radius */
#define SA1CA_GH_KILL_ACTS    0x28u     /* guest: DEATHZONE *[8] (every act -> the kill list), 0 = none */
#define SA1CA_GH_KILLS        0x2Cu     /* kill objects */
#define SA1CA_GH_LANDTABLE    0x30u     /* guest: a full landtable of every land object (information) */
#define SA1CA_GH_WIN_OBJECTS  0x34u     /* window objects per world */
#define SA1CA_GH_WIN_POLYS    0x38u     /* polygon budget per world */
#define SA1CA_GH_FLAGS        0x3Cu     /* SA1_GRID_* */
#define SA1CA_GH_BOXES        0x40u     /* guest: float[6] per COL: the object's points' box (min xyz, max xyz) */
#define SA1CA_GRID_HASH_ENTRY 16u       /* {s32 ix, s32 iz, u32 first COL, u32 count}; count 0 = empty */
#define SA1CA_GRID_COL_PAD    16.0f     /* COL radius = model radius + 16 (the per-query gather ignores speed) */
#define SA1CA_DEATHZONE_TABLE 0x8C1BE404u  /* DeathZoneTable[level] -> DEATHZONE *[act] (8C07BAC0) */
#define SA1CA_LEVELS          43u          /* DeathZoneTable entries (levels 0-42) */
#define SA1CA_FN_DEATHZONE    0x8C07BAC0u  /* death zone task exec */
#define SA1CA_COLSEG_BYTES    (64u << 20)  /* the static collision segment (guest 0x88000000-0x8BFFFFFF) */

/* column index hash (the builder and the world's window use the same) */
static inline uint32_t sa1ca_grid_hash(int32_t ix, int32_t iz)
{
    uint32_t h = (uint32_t)ix * 0x9E3779B1u ^ (uint32_t)iz * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

/* GRID parameters (sa1_collision_grid with the defaults applied) */
typedef struct sa1ca_grid_params {
    uint32_t flags;
    float cell, max_edge;
    uint32_t win_objects, win_polys;
    float under_reach;          /* undersides within this reach of an upward face are left out (SA1 units) */
} sa1ca_grid_params;

/* ---- the stored static description (SA1 space; what set_static / set_static_ex keep) ------------ */
typedef struct sa1ca_group {
    uint32_t col_flags;         /* raw COL attribute */
    uint32_t surface;           /* the group's SA1_SURFACE_* (raw = 0) */
    uint32_t raw;               /* SA1_COLGROUP_RAW_FLAGS */
    uint32_t evalflags;
    float pos[3];
    uint32_t ang[3];
    float scl[3];
    float model_center[3];
    float model_r;
    float col_center[3];
    float col_radius, col_ywidth, col_zwidth;
    uint32_t col_blockbit;
    uint32_t first_point, num_points;   /* into sa1ca_static.points (3 floats per point) */
    uint32_t first_tri, num_tris;       /* into sa1ca_static.tris */
} sa1ca_group;

typedef struct sa1ca_tri {
    uint32_t v[3];              /* point indices relative to the group, winding order */
    uint32_t material;          /* NJS_MATERIAL.attrflags */
    uint32_t flags;             /* SA1_COLTRI_* */
} sa1ca_tri;

/* result of one synthesis */
typedef struct sa1ca_built {
    uint32_t landtable;         /* guest address of the _OBJ_LANDTABLE (0: none) */
    uint32_t cols;              /* guest address of the COL array */
    uint32_t base;              /* guest address of the allocation */
    uint32_t bytes;
    uint32_t objects, meshsets, materials, points, triangles, odd_triangles;
    uint32_t generation;        /* of the description it was built from */
    /* GRID */
    uint32_t grid;              /* guest address of the GRID header (0: not GRID) */
    uint32_t token;             /* GRID header token (identifies this build in a collision segment) */
    uint32_t kill_objects, split, cut, dropped, inexact;
    uint32_t undersides;        /* downward faces within reach of an upward face left out (GRID c2) */
    uint32_t front_only;        /* (round 0.9.3) front-only GRID triangles (the input's and c2's) */
    uint32_t curved;            /* (round 0.9.3) CURVED undersides within reach c2 kept */
    /* round 0.9.5 (the board kinds, SA1_SURFACE_BOARD_SNOW / _SAND; gsa1_095_plan.md 5.3 with its Review R.1): the land
       objects' board bits, one byte per COL entry in COL order (1 board snow, 2 board sand), in the collision segment right
       after the GRID allocation's objects; 0 when no input triangle has a board bit (the GRID is then 0.2.0's, byte for
       byte). Read by the world's window (sa1_coladapt_world.c) while its player rides */
    uint32_t board;
    uint32_t board_objects;     /* land objects with a board bit */
} sa1ca_built;
/* a surface's board bits (1 SA1_SURFACE_BOARD_SNOW, 2 SA1_SURFACE_BOARD_SAND): GRID object and moving-object group keys */
static inline uint32_t sa1ca_surface_board(uint32_t surface)
{
    return ((surface & 0x100000u) ? 1u : 0u) | ((surface & 0x200000u) ? 2u : 0u);
}

#define SA1CA_MAGIC 0x41434153u /* "SACA" */

/* ---- prepared static collision (sa1_collision_prepare, G-SA1 request 11) -------------------------------------------
 * The synthesis of a description (GRID cells or the FAITHFUL landtable) made once, on any thread, into a collision
 * segment of its own (guest 0x88000000, sealed read-only), immutable afterwards and reference-counted: the host's
 * reference, the context's (sa1_collision_set_prepared: a store whose `prepared` points here) and one per world created
 * while it was the context's geometry (the world's segments 0x08-0x0B are this segment: nothing is copied). The last
 * release frees the segment (any thread). */
#define SA1CA_PREP_MAGIC 0x50455250u    /* "PREP" */
struct sa1g_shared;
struct sa1_collision_prepared {
    uint32_t magic;
    volatile int32_t refs;
    sa1_host host;                      /* the allocator of the context it was prepared from */
    sa1_status status;                  /* SA1_OK or why the preparation failed */
    char message[256];
    uint32_t mode;                      /* SA1_COLLISION_MODE_GRID / FAITHFUL */
    uint32_t generation;                /* process-unique: the GRID header's generation word */
    /* the description's counts (sa1_collision_info) */
    uint32_t groups, points, triangles, collidable_groups, collidable_tris, odd_tris, flat;
    sa1ca_grid_params grid;
    sa1ca_built built;                  /* the synthesis (guest addresses in seg) */
    float ms;                           /* the preparation's own time */
    struct sa1g_shared *seg;            /* the collision segment (world core); NULL in builds without it */
};

typedef struct sa1ca_static {
    uint32_t magic;
    uint32_t mode;              /* SA1_COLLISION_MODE_* */
    uint32_t generation;
    int32_t disc_level, disc_act;
    uint32_t num_groups, num_points, num_tris;
    sa1ca_group *groups;
    float *points;
    sa1ca_tri *tris;
    uint32_t collidable_groups, collidable_tris, odd_tris;
    sa1ca_built built;          /* last synthesis (sa1ca_build) */
    uint32_t builds;
    sa1ca_grid_params grid;     /* GRID parameters (defaults for sa1_collision_set_static) */
    uint32_t num_flat;          /* sa1_collision_set_static: triangles in flat[] */
    sa1_collision_tri *flat;    /* num_flat triangles (SA1 space) stored right after this struct, in the
                                   same allocation; ctx->static_tris points here */
    struct sa1_collision_prepared *prepared;    /* sa1_collision_set_prepared: the geometry, already synthesized (a
                                                   reference: sa1ca_static_free releases it); no groups / flat here */
} sa1ca_static;

/* Validate and convert a public description (host -> SA1 space unless SA1_COLLISION_SA1_SPACE).
 * The result is sa1_mem_alloc'd (free with sa1ca_static_free). */
sa1_status sa1ca_static_create(sa1_context *ctx, const sa1_collision_static *desc, sa1ca_static **out);
/* A store for n flat triangles (sa1_collision_set_static), mode GRID, flat[] zeroed. */
sa1_status sa1ca_static_create_flat(sa1_context *ctx, uint32_t n, sa1ca_static **out);
void sa1ca_static_free(const sa1_context *ctx, sa1ca_static *s);
/* The context's current description (sa1_collision.c), NULL if none */
sa1ca_static *sa1ca_static_of(const sa1_context *ctx);

/* the prepared geometry of a store (NULL: none) and the reference counting (sa1_collision.c; any thread) */
static inline struct sa1_collision_prepared *sa1ca_prepared_of(const sa1ca_static *s)
{
    return s != NULL ? s->prepared : NULL;
}
void sa1ca_prepared_retain(struct sa1_collision_prepared *p);
void sa1ca_prepared_release(struct sa1_collision_prepared *p);

/* (round 0.9.3; sadc docs/research/gsa1_091b_ceiling.md residual 1) the material bit GRID c2 sets on the top of a slab whose
 * underside it keeps as front-only (SA1_MAT_FRONT_ONLY): the lowered 8C029B00 of a host world does not touch such a top from
 * behind when the sphere starts the step more than its radius behind it and moves towards its front - a sphere rising from
 * under the slab, held one radius below the front-only underside, whose step reached the top's band (a slab thinner than the
 * step's rise: Tails rising out of water, a jump in a low room) and would have been put on the roof; a sphere that dipped
 * into the slab from above (within its radius of the top: mechanism B's rescue) is touched as before. A user bit of the
 * Ninja material word (0x7F) that no land material of the game's stages sets; set by libsa1 only, not a host flag */
#define SA1CA_MAT_THIN_TOP   0x00000020u

/* SA1_SURFACE_* -> COL attribute bits (design 5.6) */
uint32_t sa1ca_surface_to_col(uint32_t surface);
/* the same, with SOLID implied when none of SOLID / WATER / HEAVY_WATER / KILL is set (host triangle soups:
 * sa1_collision_set_static, GRID groups without SA1_COLGROUP_RAW_FLAGS, moving objects) */
uint32_t sa1ca_surface_col(uint32_t surface);
/* material attrflags of a host triangle (SA1_SURFACE_DOUBLE_SIDED -> 0x00800000; round 0.9.3: SA1_SURFACE_FRONT_ONLY ->
 * SA1_MAT_FRONT_ONLY unless DOUBLE_SIDED or CURVED) */
uint32_t sa1ca_surface_material(uint32_t surface);
/* GRID parameters with the defaults applied (g may be NULL) */
void sa1ca_grid_defaults(const sa1_collision_grid *g, sa1ca_grid_params *out);
/* a caller's sa1_collision_grid passes the API's checks (struct_size, flags, finite non-negative sizes) */
int sa1ca_grid_valid(const sa1_collision_grid *gp);

/* ---- synthesis ------------------------------------------------------------------------------------ */
/* Allocate n bytes (zeroed, writable) aligned to `align` in guest memory: returns the guest address and
 * the host pointer, or 0 when out of space. */
typedef uint32_t (*sa1ca_alloc_fn)(void *user, size_t n, size_t align, void **host);

/* Bytes FAITHFUL needs for s (one allocation) */
sa1_status sa1ca_faithful_size(sa1_context *ctx, const sa1ca_static *s, size_t *bytes);
/* FAITHFUL: one landtable for every group of s, in one allocation of `alloc`. */
sa1_status sa1ca_build_faithful(sa1_context *ctx, const sa1ca_static *s, sa1ca_alloc_fn alloc, void *user,
                                sa1ca_built *out);
/* GRID (design 5.4): cells, column index, kill planes and a full landtable in one allocation of `alloc`. */
sa1_status sa1ca_build_grid(sa1_context *ctx, const sa1ca_static *s, sa1ca_alloc_fn alloc, void *user,
                            sa1ca_built *out);
/* The context's current description, by mode (FAITHFUL, GRID; DISC needs no synthesis: it reports
 * SA1_ERR_UNSUPPORTED, use sa1ca_disc_stage / sa1ca_disc_landtable). Records the result for
 * sa1_collision_info_get. */
sa1_status sa1ca_build(sa1_context *ctx, sa1ca_alloc_fn alloc, void *user, sa1ca_built *out);

/* ---- DISC mode ------------------------------------------------------------------------------------ */
typedef struct sa1ca_stage {
    char name[16];              /* "STG01.PRS" */
    uint32_t load;              /* SA1CA_STAGE_SLOT */
    uint8_t *data;              /* unpacked image (sa1_mem_alloc'd; sa1ca_stage_free) */
    uint32_t size;
    char sha1[41];              /* of the file on the disc */
} sa1ca_stage;

/* Read the stage overlay of `level` (STGnn.PRS) from the user's disc: SHA-1 checked (unless
 * SA1_LOAD_ACCEPT_MODIFIED in flags), PRS-unpacked. The world image composition loads it at
 * stage->load in place of B_CHAOS7 (one occupant per overlay slot). */
sa1_status sa1ca_disc_stage(sa1_context *ctx, const char *gdi_path, int32_t level, uint32_t flags,
                            sa1ca_stage *stage);
void sa1ca_stage_free(const sa1_context *ctx, sa1ca_stage *stage);
/* D_8C19E9FC[level][act] from a 1ST_READ.BIN image (loaded at 0x8C010000): the landtable's guest
 * address, 0 if none / out of range. */
uint32_t sa1ca_disc_landtable(const uint8_t *first_read, size_t size, int32_t level, int32_t act);

/* ---- landtable dumps of the harness (--dump-land v2: PREFIX.tri + PREFIX.col) --------------------- */
typedef struct sa1ca_dump {
    sa1_collision_static desc;  /* ready for sa1_collision_set_static_ex: FAITHFUL, SA1 space, raw flags */
    sa1_collision_group *groups;
    float *points;
    uint32_t *indices;
    uint32_t *materials;
    uint8_t *tri_flags;
    float *world;               /* 9 per triangle: the dump's world-space corners (8C028EC2's njCalcPoint
                                   results in the interpreter), for tests */
    uint32_t landtable;         /* guest address of the dumped landtable */
    uint32_t ncol, ntri, nnodes;
    uint32_t strips;            /* strips seen */
    uint32_t parity_deduced;    /* strips whose flip bit follows from neighbouring triangles */
    uint32_t parity_assumed;    /* one-triangle strips: flip 0 assumed (not recorded by dump v2) */
    uint32_t parity_conflicts;  /* strips no flip value explains (cut strips): flip 0 assumed */
    uint32_t odd_triangles;
} sa1ca_dump;

/* Read a v2 dump (host file callbacks). Every COL must be a single node (v2 has no child transforms:
 * SA1_ERR_UNSUPPORTED otherwise). */
sa1_status sa1ca_dump_load(sa1_context *ctx, const char *prefix, sa1ca_dump *out);
void sa1ca_dump_free(const sa1_context *ctx, sa1ca_dump *d);

/* ---- moving objects (design 5.5) -------------------------------------------------------------------- */
/* Every sa1_col_object's triangles (SA1 space, object local, sorted by collidable group) follow this header
 * in the same allocation (sa1_collision.c): o->tris == (sa1_collision_tri *)(objx + 1). */
#define SA1CA_OBJX_MAGIC 0x584F4153u    /* "SAOX" */
typedef struct sa1ca_objx {
    uint32_t magic;
    uint32_t version;           /* changes with every move (the worlds interpolate to it) */
    int32_t ang[3];             /* BAMS: the rotation as Rz(ang[2]) * Ry(ang[1]) * Rx(ang[0]) (Ninja order) */
    float pos[3];               /* SA1 space */
    float radius;               /* largest |point| (object space) */
    uint32_t ntris;
    uint32_t ngroups;           /* collidable groups (distinct COL words) */
    uint32_t group_col[SA1_MAX_OBJECT_GROUPS];
    uint32_t group_first[SA1_MAX_OBJECT_GROUPS], group_count[SA1_MAX_OBJECT_GROUPS];
    /* round 0.9.5 (review R.1): the groups' board bits (sa1ca_surface_board); a group is (COL word, board bits) */
    uint32_t group_board[SA1_MAX_OBJECT_GROUPS];
} sa1ca_objx;

/* the header of an object (NULL if o has none) */
sa1ca_objx *sa1ca_objx_of(const sa1_col_object *o);
/* rotation (row-major, column vectors, SA1 space) -> BAMS angles of Rz * Ry * Rx */
void sa1ca_rotation_to_bams(const float r[9], int32_t ang[3]);

/* ---- the world side (src/world/sa1_coladapt_world.c; builds with the world core only) --------------- */
struct sa1g_world;
/* After a world was booted (sa1w_world_create): for an EMPTY / WINDOW landtable (GRID or no static
 * collision) it synthesizes the GRID cells of the current geometry generation into the world's collision
 * segment (once per generation), keeps the world's adapter state in its extension segment, installs the
 * per-step refresh (window landtable, moving objects) and, with kill planes, the world's death zones.
 * Worlds with the stage's or a FAITHFUL landtable (DISC, FAITHFUL) are left unchanged. notes (may be NULL):
 * SA1W_NOTE_GRID_PENDING is cleared when GRID collision was set up. */
sa1_status sa1ca_world_attach(sa1_context *ctx, struct sa1g_world *w, uint32_t *notes);

/* per-world counters of the adapter (tests, diagnostics); 0 when the world has no adapter */
typedef struct sa1ca_world_stats {
    uint32_t attached;          /* 1: the adapter runs in this world */
    uint32_t grid;              /* guest address of the GRID header (0: none) */
    uint32_t steps;             /* refreshes */
    uint32_t window_objects, window_polygons;   /* last window */
    uint32_t candidates;        /* last step: objects within the gather range */
    uint32_t over_objects, over_polygons, over_near;    /* steps that left objects within 200 units out (by reason) */
    uint32_t over_far;          /* steps that left only objects farther than 200 units out (window full) */
    uint32_t moving, moving_entries;            /* objects / DynamicCOL entries in the world */
    uint32_t over_moving;       /* steps with objects left out (slots, DynamicCOL list, segment) */
    uint32_t dyn_near, dyn_polygons;            /* last step: moving entries in range and their polygons */
    uint32_t kill_objects;
    /* PC-only capacities of host worlds (design 5.7; sadc docs/research/gsa1_ramp_freeze.md fixes 3, 4, 10) */
    uint32_t near_cap;          /* NEAR-poly list of the world's game code (8C028BFE): 512 (round 0.9.2; 256 before),
                                   or 96 = the DC list
                                   (SA1_GRID_DC_LIMITS, or the lowered game code has no raised list) */
    uint32_t near_peak;         /* last step: the fullest NEAR-poly list of a query */
    uint32_t near_max;          /* the fullest NEAR-poly list of the world's life */
    uint32_t over_near_polys;   /* steps in which a NEAR-poly query filled its list (SA1_CAPACITY_NEAR_POLYGONS) */
    uint32_t must_over;         /* steps that left out an object within 40 units of the character even with the whole
                                   polygon pool (the nearest object is never left out for the budget alone) */
    uint32_t matrix_base;       /* the matrix stack depth after boot (B_8C88F69C) */
    uint32_t matrix_restores;   /* steps that began deeper (the stack was restored: SA1_CAPACITY_MATRIX_STACK) */
    uint32_t matrix_slots;      /* slots restored in all */
    /* round 0.9.2 (sadc docs/research/gsa1_090_collision.md C3 / F1 with its Review R.4.1). over_near (above) counts the
       steps in which the query bound left objects out (SA1_CAPACITY_LAND_QUERY; before 0.9.2: the box-distance guard) */
    uint32_t query_radius_x100; /* last step: the character's collision radius r (playerwk.p.height / 2) x 100 */
    uint32_t query_near;        /* last step: land objects in the window whose COL sphere comes within 2 r + 20 of the
                                   character (the ones its next land-collision query can gather), moving ones included */
    uint32_t query_max;         /* the most of the world's life (the bound keeps it <= query_cap) */
    uint32_t hits_cap;          /* the land-collision arrays of the lowered 8C073160 / 8C0777E0 (192; 0: the lowered code
                                   has no SA1_HITCAP block, e.g. a build of the game code before round 0.9.2) */
    uint32_t hits_peak;         /* last step: the most land objects one call of them collected (pass 1) */
    uint32_t hits_max;          /* the most of the world's life (more than 16 would have overflowed the game's arrays) */
    uint32_t hits_dropped;      /* objects a call left out because its arrays were full (SA1_CAPACITY_LAND_HITS) */
    uint32_t hits_canary;       /* steps that found the canary word after the arrays changed (memory corruption: 0) */
    /* round 6a (SA1_CREATE_WATER_FLOOR, src/world/sa1_coladapt_world.c cw_window) */
    uint32_t water_floors;      /* water objects the window copied as solid floors (all refreshes) */
    /* round 0.9.3 (sadc docs/research/gsa1_091b_ceiling.md FIX-C1: SA1_FRONTONLY of the lowered 8C029B00) */
    uint32_t front_only_on;     /* 1: the world's sphere test skips front-only polygons touched from behind */
    uint32_t front_only_skipped;    /* polygons it skipped so far (the sphere started behind them) */
    uint32_t thin_top_skipped;      /* thin slabs' tops (SA1CA_MAT_THIN_TOP) it skipped so far (rising from under them) */
    /* round 0.9.3 (the 0.9.2 owner logs' "the land collision's query took 15 objects"; sadc build/research/colltunnel) */
    uint32_t query_cap;         /* the query bound: 191 (the window's own: the lowered arrays hold 192), or 15 when the
                                   lowered game code has no SA1_HITCAP arrays or the geometry keeps SA1_GRID_DC_LIMITS */
} sa1ca_world_stats;
void sa1ca_world_stats_get(const struct sa1g_world *w, sa1ca_world_stats *out);
/* round 0.9.2 (sadc docs/research/gsa1_090_water.md WTR-LOG, sa1_state_ex.water_attr / water_level): player 0's
 * playerwk.shadow.Attr_top and y_top (the game's water probe 8C0925A0: the lowest water surface above the character, SA1
 * space) in any world; 0 when the world has no player work yet */
int sa1ca_world_player_water(const struct sa1g_world *w, uint32_t *attr, float *y_top);

/* At the start of every step of a world (src/world/sa1_world_tick.c; inside the world): in a host world (the adapter
 * is attached: EMPTY / WINDOW landtable) the Ninja matrix stack goes back to its depth after boot when a push was left
 * behind (8C029B00 leaves one when 16 polygons of one object are touched, as the DC does; the DC re-inits the stack at
 * game-mode changes only, 8C0544E2, which a libsa1 world never makes), counted and reported once per episode as
 * SA1_EVENT_CAPACITY SA1_CAPACITY_MATRIX_STACK. Other worlds (DISC / FAITHFUL references, transplants) are left as the
 * game leaves them. */
void sa1ca_world_step_begin(struct sa1g_world *w);

#endif /* SA1_COLADAPT_H */
