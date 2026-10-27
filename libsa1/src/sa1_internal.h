/*
 * libsa1 internal declarations.  Not part of the public API.
 *
 * Conventions:
 *   - All disc data is untrusted.  Every read from a game image goes through
 *     sa1_view_get(), which bounds-checks the SH-4 address range against the loaded
 *     images; nothing ever casts raw image memory to a C struct.
 *   - Native asset structures are built in a per-character arena and refer to each
 *     other by index, never by SH-4 pointer.
 */
#ifndef SA1_INTERNAL_H
#define SA1_INTERNAL_H

#include "libsa1.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Limits (safety caps for untrusted data)                                   */
/* ------------------------------------------------------------------------- */

#define SA1_MAX_FILE_SIZE      (32u << 20)  /* any single file read from the disc */
#define SA1_MAX_UNPACKED_SIZE  (16u << 20)  /* PRS output (the Dreamcast has 16 MB RAM) */
#define SA1_MAX_GDI_SIZE       (64u << 10)
#define SA1_MAX_TRACKS         99u
#define SA1_MAX_DIR_SIZE       (4u << 20)
#define SA1_MAX_NODES          1024u        /* nodes per NJS_OBJECT tree */
#define SA1_MAX_DEPTH          128u         /* nesting depth of a tree */
#define SA1_MAX_POINTS         65535u       /* vertices per NJS_MODEL (u16 indices) */
#define SA1_MAX_MESHSETS       1024u
#define SA1_MAX_MATERIALS      1024u
#define SA1_MAX_CORNERS        (1u << 20)   /* polygon corners per meshset */
#define SA1_MAX_FRAMES         1000000u
#define SA1_MAX_KEYS           1000000u     /* keys per track */
#define SA1_MAX_FACTORS        8u
#define SA1_MAX_TEXTURES       4096u        /* per archive */
#define SA1_MAX_TEX_DIM        1024u
#define SA1_MAX_WELDS          256u
#define SA1_MAX_ARCHIVES       12u          /* per character set: its own archives (E-102: 5) and the common ones
                                               (OBJ_REGULAR, EFF_REGULAR, CON_REGULAR: E-102 8 of 8 before round 0.9.1,
                                               sadc docs/research/gsa1_070_hud.md Review R.4.1) */
#define SA1_MAX_ACTIONS        1024u
#define SA1_MAX_IMAGES         32u
#define SA1_MAX_INSTANCES      256u
#define SA1_VIEW_MAX           8u

/* ------------------------------------------------------------------------- */
/* Memory                                                                    */
/* ------------------------------------------------------------------------- */

void *sa1_mem_alloc(const sa1_context *ctx, size_t size);           /* zeroed; NULL on failure */
void *sa1_mem_array(const sa1_context *ctx, size_t n, size_t elem); /* overflow-checked, zeroed */
void  sa1_mem_free(const sa1_context *ctx, void *p);

typedef struct sa1_arena_block sa1_arena_block;
typedef struct sa1_arena {
    const sa1_context *ctx;
    sa1_arena_block   *head;
    size_t             total;
} sa1_arena;

void *sa1_arena_alloc(sa1_arena *a, size_t size);                   /* zeroed, 16-aligned */
void *sa1_arena_array(sa1_arena *a, size_t n, size_t elem);
void  sa1_arena_release(sa1_arena *a);

/* Growable array of fixed-size elements.  Elements move on growth: refer by index. */
typedef struct sa1_vec {
    void    *data;
    uint32_t count, cap, elem;
} sa1_vec;

void *sa1_vec_push(const sa1_context *ctx, sa1_vec *v);             /* zeroed new element */
void  sa1_vec_free(const sa1_context *ctx, sa1_vec *v);
#define SA1_VEC(v, T) ((T *)(v).data)

/* the host's allocator without a context (objects that may outlive the context that made them: sa1_collision_prepared) */
void *sa1_host_mem_alloc(const sa1_host *h, size_t size);           /* zeroed; NULL on failure */
void  sa1_host_mem_free(const sa1_host *h, void *p);

/* A worker context (sa1_collision_prepare, sa1_character_prepare): a private sa1_context of the same host allocator, file
 * callbacks and coordinate system as ctx, without its log (workers never log) and with its own error text, files list and
 * API state, so that code written for a context runs on another thread without touching ctx's mutable state. Only ctx's
 * host struct and coordinates are read (sa1_set_coords must not run at the same time). */
sa1_context *sa1_worker_ctx_create(const sa1_context *ctx);
void         sa1_worker_ctx_destroy(sa1_context *wctx);

/* atomic 32-bit counters (reference counts) */
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
static inline int32_t sa1_atomic_add32(volatile int32_t *p, int32_t d)
{
    return (int32_t)_InterlockedExchangeAdd((volatile long *)p, (long)d) + d;
}
#else
static inline int32_t sa1_atomic_add32(volatile int32_t *p, int32_t d)
{
    return __atomic_add_fetch(p, d, __ATOMIC_ACQ_REL);
}
#endif
static inline int32_t sa1_atomic_load32(volatile int32_t *p)
{
    return sa1_atomic_add32(p, 0);
}

/* ------------------------------------------------------------------------- */
/* Logging, errors, time                                                     */
/* ------------------------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#  define SA1_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#  define SA1_PRINTF(a, b)
#endif

void       sa1_log(const sa1_context *ctx, int32_t level, const char *fmt, ...) SA1_PRINTF(3, 4);
/* Records `fmt` as the last error, logs it (ERROR for st < 0, WARN otherwise), returns st. */
sa1_status sa1_fail(sa1_context *ctx, sa1_status st, const char *fmt, ...) SA1_PRINTF(3, 4);
double     sa1_now_ms(void);

/* Host file access (defaults to stdio when the host gives no callbacks). */
void      *sa1_file_open(const sa1_context *ctx, const char *path);
sa1_status sa1_file_size(const sa1_context *ctx, void *f, uint64_t *size);
sa1_status sa1_file_read(const sa1_context *ctx, void *f, uint64_t off, void *buf, size_t n);
void       sa1_file_close(const sa1_context *ctx, void *f);

void      *sa1_stdio_open(void *user, const char *path);
int32_t    sa1_stdio_size(void *user, void *file, uint64_t *size);
int32_t    sa1_stdio_read(void *user, void *file, uint64_t offset, void *buffer, size_t size);
void       sa1_stdio_close(void *user, void *file);

/* ------------------------------------------------------------------------- */
/* Little-endian readers (game data is little-endian like x86, but we never rely  */
/* on host byte order or alignment)                                          */
/* ------------------------------------------------------------------------- */

static inline uint16_t sa1_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t sa1_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline int32_t sa1_rds32(const uint8_t *p) { return (int32_t)sa1_rd32(p); }
static inline float sa1_rdf32(const uint8_t *p)
{
    uint32_t u = sa1_rd32(p);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}
static inline int sa1_finite(float f) { return isfinite(f) ? 1 : 0; }

/* ------------------------------------------------------------------------- */
/* SHA-1                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct sa1_sha1 {
    uint32_t h[5];
    uint64_t length;
    uint8_t  block[64];
    uint32_t used;
} sa1_sha1;

void sa1_sha1_init(sa1_sha1 *s);
void sa1_sha1_update(sa1_sha1 *s, const void *data, size_t len);
void sa1_sha1_final(sa1_sha1 *s, uint8_t digest[20]);
void sa1_sha1_hex(const void *data, size_t len, char hex[41]);

/* ------------------------------------------------------------------------- */
/* PRS                                                                       */
/* ------------------------------------------------------------------------- */

/* Decompresses a PRS stream into a new buffer (sa1_mem_free it).  Fails on truncated
 * input, copies before the start of the output, or output larger than max_out. */
sa1_status sa1_prs_decompress(const sa1_context *ctx, const uint8_t *src, size_t src_len, size_t max_out,
                              uint8_t **out, uint32_t *out_len);

/* ------------------------------------------------------------------------- */
/* Paths, GDI, ISO9660                                                       */
/* ------------------------------------------------------------------------- */

/* Rejects UNC (\\server\share, //server), \\?\ and \\.\ paths and DOS device names. */
sa1_status sa1_path_check_host(const char *path);
/* Accepts only a bare file name: no separators, no drive, not "."/"..", no control
 * characters, no reserved device name, no trailing dot/space. */
sa1_status sa1_path_check_bare(const char *name);

typedef struct sa1_gdi_track {
    uint32_t number;
    uint32_t lba;
    uint32_t type;          /* 0 audio, 4 data */
    uint32_t sector_size;   /* 2048 or 2352 */
    char     name[256];
} sa1_gdi_track;

sa1_status sa1_gdi_parse(const char *text, size_t len, sa1_gdi_track *tracks, uint32_t max_tracks,
                         uint32_t *num_tracks);

typedef struct sa1_disc_track {
    sa1_gdi_track info;
    uint32_t      num_sectors;
    void         *file;
} sa1_disc_track;

typedef struct sa1_disc {
    const sa1_context *ctx;
    sa1_disc_track     tracks[SA1_MAX_TRACKS];
    uint32_t           num_tracks;
    uint32_t           hd;              /* index of the last data track (ISO9660 lives here) */
    uint32_t           root_lba, root_size;
    uint8_t           *raw;             /* scratch for raw sector reads */
    uint8_t           *dir;             /* one cached directory extent */
    uint32_t           dir_lba, dir_size;
} sa1_disc;

sa1_status sa1_disc_open(sa1_context *ctx, const char *gdi_path, sa1_disc *disc);
void       sa1_disc_close(sa1_disc *disc);
sa1_status sa1_disc_read_sectors(sa1_disc *disc, uint32_t lba, uint32_t count, uint8_t *out);
/* Reads one file by ISO path ("1ST_READ.BIN", "SONICADV/S_MOT.PRS"); data is
 * allocated with sa1_mem_alloc. */
sa1_status sa1_disc_read_file(sa1_disc *disc, const char *path, uint8_t **data, uint32_t *size);

/* ------------------------------------------------------------------------- */
/* Memory images and address views                                          */
/* ------------------------------------------------------------------------- */

typedef struct sa1_image {
    char     name[32];      /* "1ST_READ.BIN", "S_MOT.PRS", ... */
    uint32_t load_addr;     /* as spelled in pointers (0x8C010000, 0x0CC00000, ...) */
    uint32_t phys;          /* load_addr & 0x1FFFFFFF */
    uint32_t size;
    uint8_t *data;
    int32_t  id;            /* index into sa1_context.image_names */
} sa1_image;

/* An ordered set of images used to resolve SH-4 pointers.  0x8Cxxxxxx, 0xACxxxxxx
 * and 0x0Cxxxxxx alias (physical address = addr & 0x1FFFFFFF); the first image that
 * contains the whole range wins. */
typedef struct sa1_view {
    const sa1_image *img[SA1_VIEW_MAX];
    uint32_t         n;
} sa1_view;

/* Returns a pointer to `len` bytes at `addr`, or NULL (addr == 0 or not fully inside
 * one image of the view). */
const uint8_t *sa1_view_get(const sa1_view *v, uint32_t addr, uint32_t len, const sa1_image **which);
int            sa1_images_overlap(const sa1_image *a, const sa1_image *b);

/* ------------------------------------------------------------------------- */
/* Manifest (US v1.005)                                                      */
/* ------------------------------------------------------------------------- */

#define SA1_FIRST_READ_NAME   "1ST_READ.BIN"
#define SA1_FIRST_READ_SHA1   "e74c849806db103aa87f84e41a382873bc96b6df"
#define SA1_FIRST_READ_LOAD   0x8C010000u
#define SA1_ADDR_PHYSICS      0x8C1BDFE4u   /* player_parameter[8], 0x84 each */
#define SA1_PHYSICS_STRIDE    0x84u
#define SA1_ADDR_PVM_LISTS    0x8C1C2584u   /* 8 pointers to {char *name; NJS_TEXLIST *list}[] */
#define SA1_ADDR_PVM_COMMON   0x8C1C2450u   /* {char *name; NJS_TEXLIST *list}[] of the stage start, every character:
                                               OBJ_REGULAR, EFF_REGULAR, CON_REGULAR, EXTRA, HINT, EME_KIRAN */

typedef struct sa1_file_desc {
    const char *name;       /* bare name; 1ST_READ.BIN is in the root, the rest in SONICADV/ */
    uint32_t    size;
    const char *sha1;
    uint32_t    load_addr;  /* memory image load address, 0 = not an image (texture archive) */
} sa1_file_desc;

typedef struct sa1_action_override {
    uint16_t    first, last;    /* action index range */
    const char *image;          /* overlay that holds these actions */
    uint32_t    load_flag;      /* SA1_LOAD_* that enables the overlay */
} sa1_action_override;

typedef struct sa1_char_desc {
    const char *name;
    int32_t     game_id;            /* character id: physics index and PVM list index */
    uint32_t    main_object;
    uint32_t    action_table;       /* PL_ACTION[] (0 = none) */
    uint32_t    num_actions;
    uint32_t    weld_table;         /* PL_JOIN_VERTEX[] (0 = none) */
    const char *images[4];          /* overlays mapped besides 1ST_READ.BIN */
    uint32_t    image_flags[4];     /* SA1_LOAD_* flag needed (0 = always) */
    const sa1_action_override *overrides;
    uint32_t    num_overrides;
    const uint32_t (*subst)[2];     /* object substitutions {from, to} (Super Sonic) */
    uint32_t    num_subst;
    const char *archive;            /* explicit texture archive instead of the PVM list */
    uint32_t    archive_texlist;
} sa1_char_desc;

const sa1_file_desc *sa1_manifest_file(const char *name);
const sa1_char_desc *sa1_manifest_char(int32_t kind);

/* ------------------------------------------------------------------------- */
/* Native assets                                                             */
/* ------------------------------------------------------------------------- */

/* NJS_OBJECT eval flags */
#define SA1_EVAL_UNIT_POS 0x01u
#define SA1_EVAL_UNIT_ANG 0x02u
#define SA1_EVAL_UNIT_SCL 0x04u
#define SA1_EVAL_HIDE     0x08u
#define SA1_EVAL_BREAK    0x10u
#define SA1_EVAL_ZXY_ANG  0x20u

/* NJS_MOTION type bits */
#define SA1_MTYPE_POS 0x1u
#define SA1_MTYPE_ANG 0x2u
#define SA1_MTYPE_SCL 0x4u

typedef struct sa1_material {
    uint32_t diffuse;       /* ARGB */
    uint32_t specular;      /* ARGB */
    float    exponent;
    uint32_t tex_id;        /* attr_texId & 0x3FFFFFFF */
    uint32_t attr;          /* NJD_FLAG_* attribute word */
} sa1_material;

typedef struct sa1_meshset {
    uint8_t   poly_type;    /* 0 triangles, 1 quads, 2 N-gons, 3 strips */
    uint16_t  mat_id;
    uint32_t  num_polys;
    uint32_t  num_corners;
    uint16_t *corner_vtx;   /* model vertex index per corner (poly stream order) */
    int16_t  *uv;           /* 2 per corner (NJS_TEX, 255 = 1.0), or NULL */
    uint32_t *vcolor;       /* 1 per corner (ARGB), or NULL */
    uint32_t  num_tris;
    uint32_t *tris;         /* 3 corner indices per triangle (strip rules applied) */
} sa1_meshset;

typedef struct sa1_model {
    uint32_t     addr;
    int32_t      image;
    uint32_t     num_points;
    float       *points;    /* 3 per point */
    float       *normals;   /* 3 per point, or NULL */
    uint32_t     num_meshsets;
    sa1_meshset *meshsets;
    uint32_t     num_mats;
    sa1_material *mats;
    float        center[3];
    float        radius;
    uint32_t     num_tris;
} sa1_model;

typedef struct sa1_node {
    uint32_t addr;
    uint32_t flags;
    int32_t  model;         /* index into sa1_charset.models, -1 = none */
    float    pos[3];
    int32_t  ang[3];        /* BAMS */
    float    scl[3];
    int32_t  parent, child, sibling;
    uint8_t  drawn;         /* visited by the draw traversal and not hidden */
    uint32_t vbase;         /* first vertex in the per-instance work buffer, or UINT32_MAX */
} sa1_node;

typedef struct sa1_object {
    uint32_t  addr;         /* physical (addr & 0x1FFFFFFF), used for deduplication */
    uint32_t  ptr;          /* the pointer value as first referenced */
    int32_t   image;
    uint32_t  num_nodes;
    sa1_node *nodes;        /* Ninja traversal order: node, children, then siblings */
    uint32_t  work_vertices;    /* vertices of every node with a model (weld work buffer) */
    uint32_t  drawn_vertices;   /* vertices of drawn nodes */
    uint32_t  drawn_tris;       /* triangles of drawn nodes */
} sa1_object;

typedef struct sa1_key3 { uint32_t frame; float v[3]; } sa1_key3;
typedef struct sa1_keya { uint32_t frame; int32_t v[3]; } sa1_keya;

typedef struct sa1_motion_node {
    uint32_t  num_pos, num_ang, num_scl;
    sa1_key3 *pos;
    sa1_keya *ang;
    sa1_key3 *scl;
} sa1_motion_node;

typedef struct sa1_motion {
    uint32_t         addr;
    int32_t          image;
    uint32_t         num_frames;
    uint16_t         type;
    uint16_t         inp_fn;
    uint32_t         num_nodes;
    sa1_motion_node *nodes;
    uint32_t         num_keys;
} sa1_motion;

typedef struct sa1_action {
    int32_t  status;        /* SA1_ACTION_* */
    int32_t  object;        /* index into objects, -1 */
    int32_t  motion;        /* index into motions, -1 */
    uint32_t addr, object_addr, motion_addr;
    int32_t  image;         /* image id holding the NJS_ACTION, -1 */
    uint8_t  objnum;
    int8_t   mode;
    uint16_t next;
    float    blend, speed;
} sa1_action;

typedef struct sa1_weld {
    int32_t   object;       /* base object index */
    int32_t   src_node, dst_node;
    uint8_t   type, src_depth, dst_depth, count;
    uint32_t  num_pairs;
    uint16_t *pairs;        /* (src vertex, dst vertex) per pair */
} sa1_weld;

typedef struct sa1_texture {
    char     name[32];
    uint32_t gbix;
    uint32_t width, height;
    uint8_t  pixel_format, data_format;
    uint8_t *rgba;          /* NULL if undecodable */
    uint8_t *rgba_gold;     /* round 0.9.4a: SA1_TEXTURE_VARIANT_SUPER_GOLD's pixels (sa1_texture.c), in the charset's
                               arena (freed with it); NULL: no variant */
} sa1_texture;

typedef struct sa1_archive {
    char         name[32];
    uint32_t     texlist_addr;
    uint32_t     texlist_count;
    uint32_t     num_textures;
    sa1_texture *tex;
} sa1_archive;

typedef struct sa1_charset {
    int32_t      loaded;
    sa1_arena    arena;
    sa1_vec      models;    /* sa1_model */
    sa1_vec      objects;   /* sa1_object */
    sa1_vec      motions;   /* sa1_motion */
    int32_t      main_object;
    uint32_t     main_object_addr;
    uint32_t     num_actions;
    sa1_action  *actions;
    uint32_t     num_available;
    uint32_t     num_welds;
    sa1_weld    *welds;
    uint32_t     num_archives;
    sa1_archive  archives[SA1_MAX_ARCHIVES];
    int32_t      has_physics;
    sa1_physics  physics;
    uint32_t     max_nodes, max_work_vertices, max_vertices, max_triangles;
} sa1_charset;

/* Ninja deserializers (sa1_ninja.c).  Return the pool index (deduplicated by image
 * and address) or -1 with *st set. */
int32_t sa1_ninja_object(sa1_context *ctx, sa1_charset *cs, const sa1_view *v, uint32_t addr, sa1_status *st);
int32_t sa1_ninja_motion(sa1_context *ctx, sa1_charset *cs, const sa1_view *v, uint32_t addr,
                         uint32_t num_nodes, sa1_status *st);
sa1_status sa1_ninja_welds(sa1_context *ctx, sa1_charset *cs, const sa1_view *v, uint32_t table);

/* ------------------------------------------------------------------------- */
/* PVM / PVR                                                                 */
/* ------------------------------------------------------------------------- */

typedef struct sa1_pvm_entry {
    char     name[32];
    uint32_t gbix;
    uint32_t offset, length;    /* PVRT chunk inside the archive (from the magic) */
} sa1_pvm_entry;

sa1_status sa1_pvm_parse(const uint8_t *data, uint32_t size, sa1_pvm_entry *entries, uint32_t max_entries,
                         uint32_t *count);
sa1_status sa1_pvr_header(const uint8_t *chunk, uint32_t len, uint32_t *w, uint32_t *h, uint32_t *pf,
                          uint32_t *df);
/* Decodes the top mip level into rgba (w*h*4 bytes). */
sa1_status sa1_pvr_decode(const uint8_t *chunk, uint32_t len, uint8_t *rgba, size_t rgba_size);

/* ------------------------------------------------------------------------- */
/* Character instances and posing                                           */
/* ------------------------------------------------------------------------- */

typedef struct sa1_instance {
    int32_t      kind;
    float        position[3];
    float        yaw;
    int32_t      action;
    float        frame;
    uint32_t     cap_nodes, cap_work, cap_vertices, cap_tris;
    float       *node_mtx;      /* 12 per node, SA1 world space */
    float       *wpos, *wnrm;   /* 3 per work vertex, SA1 world space */
    uint32_t    *remap;         /* work vertex -> output vertex */
    float       *out_pos, *out_nrm, *out_uv, *out_node_mtx;
    uint32_t    *out_idx, *out_mat;
    uint8_t     *out_col, *out_flags;
    int16_t     *out_tex;
    sa1_geometry geom;
} sa1_instance;

sa1_status sa1_instance_alloc(sa1_context *ctx, const sa1_charset *cs, sa1_instance *inst);
void       sa1_instance_free(sa1_context *ctx, sa1_instance *inst);
/* Evaluates object `object` with motion `motion` (-1 = none) at `frame` into the
 * instance's geometry. */
sa1_status sa1_pose_evaluate(sa1_context *ctx, const sa1_charset *cs, sa1_instance *inst, int32_t object,
                             int32_t motion, float frame, uint32_t flags);

/* ------------------------------------------------------------------------- */
/* Context                                                                   */
/* ------------------------------------------------------------------------- */

typedef struct sa1_file_record {
    char     name[48];
    uint32_t size, unpacked_size;
    char     sha1[41];
    int32_t  hash_status;
    int32_t  used;
    double   read_ms;
} sa1_file_record;

typedef struct sa1_col_object {
    uint32_t           id;
    uint32_t           num_tris;
    sa1_collision_tri *tris;        /* SA1 space, object local */
    float              position[3]; /* SA1 space */
    float              rotation[9]; /* SA1 space */
} sa1_col_object;

#define SA1_SOUND_QUEUE 64u

struct sa1_context {
    sa1_host           host;
    sa1_coords         coords;
    char               error[512];
    int32_t            disc_loaded;
    sa1_charset        chars[SA1_CHAR_COUNT];
    char               image_names[SA1_MAX_IMAGES][32];
    uint32_t           num_image_names;
    sa1_vec            files;           /* sa1_file_record */
    sa1_instance      *inst[SA1_MAX_INSTANCES];
    uint16_t           inst_gen[SA1_MAX_INSTANCES];
    sa1_collision_tri *static_tris;     /* SA1 space */
    uint32_t           num_static_tris;
    sa1_vec            col_objects;     /* sa1_col_object */
    uint32_t           next_col_id;
    sa1_sound_event    sounds[SA1_SOUND_QUEUE];
    uint32_t           sound_head, sound_count;
};

void sa1_charset_free(sa1_context *ctx, sa1_charset *cs);
void sa1_characters_destroy_all(sa1_context *ctx);
void sa1_collision_free(sa1_context *ctx);
/* host <-> SA1 conversions with the current coordinate system */
void sa1_to_host_point(const sa1_context *ctx, const float in[3], float out[3]);
void sa1_to_host_dir(const sa1_context *ctx, const float in[3], float out[3]);
void sa1_from_host_point(const sa1_context *ctx, const float in[3], float out[3]);
void sa1_from_host_dir(const sa1_context *ctx, const float in[3], float out[3]);
/* Copies min(sizes) bytes of a library-filled output struct that starts with struct_size. */
void sa1_copy_out(void *dst, const void *src, size_t src_size);

#endif /* SA1_INTERNAL_H */