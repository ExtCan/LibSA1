/*
 * libsa1 - a host-agnostic C library for Sonic Adventure (Dreamcast) characters.
 *
 * libsa1 contains no SEGA data.  Every model, motion, weld list, texture and
 * physics table is read at run time from the user's own GD-ROM dump (.gdi) of
 * Sonic Adventure (US, v1.005; 1ST_READ.BIN SHA-1
 * e74c849806db103aa87f84e41a382873bc96b6df).
 *
 * API conventions
 *   - Every function returns an sa1_status (0 = success, < 0 = error, > 0 = warning)
 *     unless documented otherwise.  sa1_last_error() gives a readable message.
 *   - All state lives in an opaque sa1_context; there are no globals.  A context is
 *     not thread-safe; different contexts may be used from different threads.  The
 *     exceptions, callable from host worker threads while the context ticks, are listed
 *     under "Preparation on host threads" (sa1_collision_prepare, sa1_character_prepare).
 *   - Structs that a caller passes in carry a leading `struct_size` field; set it to
 *     sizeof(the struct).  Output structs with struct_size are filled up to the
 *     smaller of the caller's and the library's size.
 *   - Outputs are in HOST space: SA1 coordinates are mapped through a configurable
 *     axis matrix and unit scale (see sa1_coords; default = Source/Hammer units).
 *   - Memory returned by the library (texture pixels, posed geometry, names) is
 *     owned by the context and stays valid as documented per function.
 *
 * Status of this version (0.2 draft):
 *   0.1:             init/shutdown, coordinate system, disc loading + hashing,
 *                    character/action/physics/texture queries, texture atlas,
 *                    character create/destroy/pose (CPU mesh evaluation, welds),
 *                    collision input storage, sound event queue (always empty).
 *   0.2 (draft):     the game's own logic: one "world" per ticking character runs the
 *                    decompiled game code (sa1_character_tick, sa1_character_state[_ex],
 *                    sa1_character_create_ex, sa1_poll_events), grouped static collision
 *                    (sa1_collision_set_static_ex: FAITHFUL and DISC modes), GRID collision
 *                    for host triangle soups (sa1_collision_set_static[_ex]: cells, a window
 *                    per world, kill planes, surface kinds) and moving collision objects
 *                    (sa1_collision_object_*).  A build without the world core or without
 *                    the lowered game code reports SA1_ERR_NOT_IMPLEMENTED from the tick.
 *   0.2 render draft (SA1_API_RENDER, 2026-10-20): what the game draws, as draws in its order
 *                    (sa1_character_render: textures of any of the character's archives,
 *                    material state, blend factors, sprites and lines of effects), interpolated
 *                    between the last two pairs for any host frame rate (sa1_character_render,
 *                    sa1_character_mesh_lerp); sa1_mesh / sa1_texture_info got optional fields.
 *   0.2 prepare draft (SA1_API_PREPARE, 2026-10-21): preparation on host threads
 *                    (sa1_collision_prepare[_tris] -> sa1_collision_set_prepared: the GRID synthesis off
 *                    the thread that ticks, shared by reference by every world; sa1_character_prepare /
 *                    SA1_LOAD_WORLD_IMAGES: the world images ahead), GRID undersides within reach left
 *                    out (sa1_collision_grid.under_reach, SA1_GRID_KEEP_UNDERSIDES), the NEAR-poly debug
 *                    query (sa1_debug_near_polys).
 *   0.3 host objects draft (SA1_API_HOSTOBJ, round 5): proxies of host objects in every world (sa1_hostobj_*,
 *                    sa1_poll_hostobj_events), the ring API (sa1_character_add_rings, start_rings, sa1_state_ex rings /
 *                    lives / score / invulnerability), contacts with moving collision objects (sa1_character_contacts);
 *                    round 5's game side: HOLDABLE and BALLOON kinds, E-102's lock-on events, the HURT / DEATH events,
 *                    sa1_character_damage / set_option / held, sa1_hostobj_release / world_mask.
 *   0.3 round 0.9.1: SA1_EVENT_DEATH for drowning and death zones (SA1_DEATH_DROWN / ZONE, the RESTART fallback),
 *                    SA1_EVENT_TIME_UP, the life counter (sa1_character_set_lives), companions (Amy's bird:
 *                    sa1_character_set_companion), game options (sa1_character_set_game_option: Tails' tail snap).
 *   0.4 screen draft (SA1_API_SCREEN, round 0.9.1): the game's own HUD and the 2D draws of a world's tasks
 *                    (sa1_character_screen; SA1_COPT_HUD_PARTS), the game clock (SA1_COPT_CLOCK, E-102's time limit:
 *                    SA1_COPT_E102_NO_TIME_LIMIT, sa1_character_set_time, sa1_state_ex time fields), CON_REGULAR among
 *                    every character's archives.
 *   0.4 round 0.9.2 (SA1_API_092; sadc docs/research/gsa1_090_plan.md 6.2-6.4): sa1_input.host_flags (the host's own
 *                    water at the character), sa1_state_ex.water_attr / water_level, the land-collision capacities
 *                    SA1_CAPACITY_LAND_QUERY / LAND_HITS, the NEAR-poly list of host worlds 512, GRID objects split by
 *                    orientation (floor / wall / ceiling), an early GRID size check, the surface kinds SNOW / SPEED_PATH
 *                    / FOOTPRINTS, damage requests before the first step held until the player exists.
 *   0.4 round 0.9.3 collision (SA1_API_CEILING): front-only land triangles (solid ceilings over thin roofs), CURVED
 *                    surfaces GRID keeps, SA1_GRID_UNDERSIDES_DROP, grid_front_only / grid_curved counters; the land
 *                    query bound 191 (every object in reach), polygon-budget events only for objects within 40 units.
 *   0.4 Super Sonic (SA1_API_SUPER, round 6a; sadc docs/research/super_sonic.md, gsa1_rounds_plan.md 11.1 / 11.2 / 14.1a):
 *                    SA1_CHAR_SUPER_SONIC ticks (the game's own transformation on the Perfect Chaos stage, the ring drain,
 *                    the revert fix), Sonic turns Super in play in his own world (SA1_CREATE_SUPER_CAPABLE,
 *                    sa1_character_order, SA1_LOAD_SUPER_FORMS), sa1_character_desc.super_flags, SA1_CREATE_WATER_FLOOR, DISC
 *                    collision of level 19, SA1_STATE_SUPER.
 *   0.4 round 0.9.3a (SA1_API_SUPER_GENERAL; sadc docs/research/super_general_design.md with its Review R.4): the General
 *                    form (SA1_SUPER_GENERAL / START / WATER_WHILE_SUPER, SA1_SUPER_START_NORMAL for kind 8),
 *                    sa1_character_set_super_option (SA1_SUPOPT_*), the game options SA1_GOPT_SUPER_IDLE_BY_FORM /
 *                    SUPER_AURA_ANIM, sa1_hostobj_held.drawn / drawn_prev / drawn_yaw / drawn_yaw_prev.
 *   0.4 round 0.9.4 (SA1_API_SET; sadc docs/research/gsa1_094_plan.md 6.2 / 6.3 with its Review): the game's own objects
 *                    placed by the host (sa1_object_*, sa1_set_*, sa1_poll_set_events: rings, ring lines / circles, both item
 *                    boxes, run by the game's SET manager and spawner in every world), SA1_DRAW_OBJECT, the capacities
 *                    SA1_CAPACITY_SET_WINDOW / SET_HEAP, the game option SA1_GOPT_ITEM_ICON_LIMIT, the camera taskwk of
 *                    live worlds at the world's virtual eye.
 *   0.4 round 0.9.4a (SA1_API_SUPER_POLISH; sadc docs/research/super2_plan.md): the General form's own Super Sonic poses
 *                    (SA1_SUPOPT_SUPER_ANIMS), the golden Light Speed charge (SA1_SUPOPT_LSD_GOLD, SA1_DRAW_SUPER_GOLD,
 *                    sa1_texture_variant_get / SA1_TEXTURE_VARIANT_SUPER_GOLD), the golden spin dash dust
 *                    (SA1_SUPER_EFFECTS_DUST); libsa1_audio.h version 5: stage rules;
 *                    the game's event faces (SA1_API_FACE: sa1_character_face_mode / _face / _face_sequence / _face_level,
 *                    sa1_face_info_get) and emotes through the cutscenes' event action queue (SA1_API_EMOTE:
 *                    sa1_character_emote / _emote_stop, sa1_emote_count / sa1_emote_get); sa1_state_ex's face and emote
 *                    fields (sadc docs/research/faces_wheel_plan.md).
 *   0.x round 0.9.4b (SA1_API_SET_RANGE, SA1_API_SET_LIVE, SA1_API_PREVIEW; sadc docs/research/objects2_plan.md): the SET
 *                    objects' spawn range (sa1_set_config.range: the game's own per-type range of the object list, up to
 *                    3,000 units - the world's far clip; owner answer O1, objects2_plan.md 11.0), the window's order beyond
 *                    the game's 400 units (a ring line or circle by its nearest ring: objects2_plan.md 11.1), the windows'
 *                    maxima 200 / 100 with a capture of 2,048 records per pair, the object displays' screen width
 *                    (sa1_set_config.view_tan), the live records (sa1_set_live); object previews (sa1_preview_*): the
 *                    game's own objects without a character, in a private playerless world per kind.
 *                    The emote end mode SA1_EMOTE_HOLD (SA1_API_EMOTE_HOLD, SA1_EMOTE_STATE_HOLDING; objects2_plan.md
 *                    11.2): an emote that ends in a pose keeps its last frame until a cancel, a stop or another emote.
 *   0.x round 0.9.4c (SA1_API_FACE_MOUTH; sadc docs/research/phoneme_plan.md): lip sync with the host's own mouth
 *                    (sa1_character_face_mouth: the face block's two keys and the weight between them, from the host's
 *                    phoneme detection), SA1_FACE_SAME_PAIR (the lip-sync mouth drawn in the pair it is applied: the face
 *                    task's own step-0 blend run before that step's draw), the per-character loudness ladders of
 *                    sa1_face_info (by the measured mouth opening, phoneme_shapes.md 6).
 *   0.3 round 0.9.5 (SA1_VERSION_MINOR 3, SA1_API_BOARD; sadc docs/research/gsa1_095_plan.md): sa1_input.host_cam_pos / host_cam_target +
 *                    SA1_INPUT_HOST_CAMERA, sa1_character_board / _board_state, SA1_STATE_ON_BOARD, SA1_EVENT_BOARD,
 *                    SA1_DRAW_BOARD, SA1_SURFACE_BOARD_SNOW / _SAND, SA1_PREVIEW_BOARD, the board archives (MIW_B001,
 *                    SB_BOARD1, MTX_BOARD0), stubs.tsv 0C900AE0 void, refusals logged at DEBUG.
 */
#ifndef LIBSA1_H
#define LIBSA1_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Versioning and linkage                                                    */
/* ------------------------------------------------------------------------- */

#define SA1_VERSION_MAJOR 0
#define SA1_VERSION_MINOR 3
#define SA1_VERSION_PATCH 0
/* API version passed in sa1_host.api_version.  The library accepts a host whose
 * major version matches and whose minor version is <= the library's. */
#define SA1_API_VERSION ((SA1_VERSION_MAJOR << 16) | SA1_VERSION_MINOR)

#if defined(SA1_STATIC)
#  define SA1_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#  if defined(SA1_BUILD_SHARED)
#    define SA1_API __declspec(dllexport)
#  else
#    define SA1_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define SA1_API __attribute__((visibility("default")))
#else
#  define SA1_API
#endif

/* ------------------------------------------------------------------------- */
/* Status codes                                                              */
/* ------------------------------------------------------------------------- */

typedef int32_t sa1_status;

#define SA1_OK                       0
#define SA1_PARTIAL                  1    /* succeeded, but some optional content is missing */
#define SA1_ERR_INVALID_ARGUMENT    -1
#define SA1_ERR_OUT_OF_MEMORY       -2
#define SA1_ERR_IO                  -3    /* host file callback failed */
#define SA1_ERR_BAD_PATH            -4    /* rejected path (UNC, device, not a bare track name) */
#define SA1_ERR_BAD_FORMAT          -5    /* malformed GDI / ISO9660 / PRS / PVM / PVR / Ninja data */
#define SA1_ERR_BAD_HASH            -6    /* file does not match the supported disc version */
#define SA1_ERR_NOT_FOUND           -7    /* file / character / action / texture does not exist */
#define SA1_ERR_NOT_LOADED          -8    /* no disc loaded, or the asset was not loaded */
#define SA1_ERR_UNSUPPORTED         -9    /* valid data in a variant this version cannot handle */
#define SA1_ERR_NOT_IMPLEMENTED    -10    /* game logic that is not decompiled yet */
#define SA1_ERR_BUFFER_TOO_SMALL   -11    /* caller buffer too small; required size reported */
#define SA1_ERR_VERSION            -12    /* host/library API version mismatch */
#define SA1_ERR_LIMIT              -13    /* a safety limit (size, count, depth) was exceeded */

#define SA1_FAILED(s) ((s) < 0)

/* ------------------------------------------------------------------------- */
/* Host callbacks                                                            */
/* ------------------------------------------------------------------------- */

#define SA1_LOG_DEBUG 0
#define SA1_LOG_INFO  1
#define SA1_LOG_WARN  2
#define SA1_LOG_ERROR 3

typedef struct sa1_host {
    uint32_t struct_size;       /* sizeof(sa1_host) */
    uint32_t api_version;       /* SA1_API_VERSION */
    void    *user;              /* passed to every callback */
    int32_t  log_level;         /* minimum level passed to log() */

    /* Logging (optional; NULL = silent).  `message` is UTF-8 without a newline. */
    void  (*log)(void *user, int32_t level, const char *message);

    /* Memory (optional; both NULL = malloc/free).  alloc must return memory aligned
     * for any C type; free(NULL) must be accepted. */
    void *(*alloc)(void *user, size_t size);
    void  (*free)(void *user, void *ptr);

    /* Read-only file access (optional; all NULL = C stdio with 64-bit offsets).
     * Paths are UTF-8.  libsa1 only asks for the .gdi given to sa1_load_disc and
     * for the track files it names (bare file names resolved next to the .gdi).
     * file_size/file_read return 0 on success; file_read must read exactly `size`
     * bytes. */
    void   *(*file_open)(void *user, const char *path);
    int32_t (*file_size)(void *user, void *file, uint64_t *size);
    int32_t (*file_read)(void *user, void *file, uint64_t offset, void *buffer, size_t size);
    void    (*file_close)(void *user, void *file);
} sa1_host;

typedef struct sa1_context sa1_context;

SA1_API uint32_t    sa1_version(void);            /* (major << 16) | (minor << 8) | patch */
SA1_API const char *sa1_version_string(void);     /* "0.3.0" */
SA1_API const char *sa1_status_string(sa1_status status);

/* Fills `host` with defaults (struct_size, api_version, log level INFO, no callbacks). */
SA1_API void        sa1_host_defaults(sa1_host *host);
/* Creates a context.  `host` may be NULL (defaults for everything). */
SA1_API sa1_status  sa1_init(const sa1_host *host, sa1_context **out_ctx);
/* Destroys the context and everything it owns (assets, characters).  NULL is ok. */
SA1_API void        sa1_shutdown(sa1_context *ctx);
/* Message of the last error/warning on this context ("" if none). */
SA1_API const char *sa1_last_error(const sa1_context *ctx);

/* ------------------------------------------------------------------------- */
/* Coordinate system                                                         */
/* ------------------------------------------------------------------------- */
/*
 * SA1 world/model space: right-handed, +Y up.  Character models face -X in model
 * space; posed characters are turned by 180 degrees about +Y (as the game draws
 * them), so a character with yaw 0 faces SA1 world +X.
 *
 * host = scale * (axis * sa1)        (axis = row-major 3x3, must be orthonormal)
 *
 * Default (SA1_COORDS_PRESET_SOURCE): scale 4.0 Hammer units per SA1 unit and
 *     axis rows [1,0,0] [0,0,-1] [0,1,0], i.e. host = 4 * (x, -z, y):
 *     SA1 +X -> Source +X (forward), SA1 +Y -> Source +Z (up),
 *     SA1 +Z -> Source -Y (right).  det = +1: a proper rotation, no mirroring;
 *     handedness and triangle winding are preserved, and a positive yaw turns
 *     counter-clockwise about Source +Z like a Source yaw angle.
 * SA1_COORDS_PRESET_NATIVE: scale 1, identity (raw SA1 units as stored on the disc).
 *
 * Triangles are emitted counter-clockwise when seen from their front side in a
 * right-handed frame.  Hosts whose front faces are clockwise (Direct3D-style
 * culling) set SA1_COORDS_FLIP_WINDING, which swaps two corners of each triangle.
 */
#define SA1_COORDS_FLIP_WINDING 0x1u

typedef struct sa1_coords {
    uint32_t struct_size;
    float    scale;             /* host units per SA1 unit, > 0 */
    float    axis[9];           /* row-major, orthonormal (det +1 or -1) */
    uint32_t flags;             /* SA1_COORDS_* */
} sa1_coords;

#define SA1_COORDS_PRESET_SOURCE 0
#define SA1_COORDS_PRESET_NATIVE 1

SA1_API sa1_status sa1_coords_preset(int32_t preset, sa1_coords *out);
/* Applies to everything produced or consumed afterwards (poses, collision input). */
SA1_API sa1_status sa1_set_coords(sa1_context *ctx, const sa1_coords *coords);
SA1_API sa1_status sa1_get_coords(const sa1_context *ctx, sa1_coords *out);

/* ------------------------------------------------------------------------- */
/* Disc loading                                                              */
/* ------------------------------------------------------------------------- */

/* Characters.  The values 0-7 are the game's own character ids. */
#define SA1_CHAR_SONIC        0
#define SA1_CHAR_EGGMAN       1
#define SA1_CHAR_TAILS        2
#define SA1_CHAR_KNUCKLES     3
#define SA1_CHAR_TIKAL        4
#define SA1_CHAR_AMY          5
#define SA1_CHAR_GAMMA        6
#define SA1_CHAR_BIG          7
#define SA1_CHAR_SUPER_SONIC  8
#define SA1_CHAR_COUNT        9
#define SA1_CHAR_MASK_ALL     0x1FFu

/* Load flags (sa1_load_options.flags). */
#define SA1_LOAD_BOARDS          0x01u  /* S_SBMOT / M_SBMOT: sand/snow board actions */
#define SA1_LOAD_SUPER           0x02u  /* B_CHAOS7: Super Sonic model, textures, actions */
#define SA1_LOAD_MINICART        0x04u  /* MINICART: one Twinkle Circuit cart action per character */
#define SA1_LOAD_STAGE_ACTIONS   0x08u  /* STG02/04/06/07/08/09: Sonic's stage-specific actions */
#define SA1_LOAD_ACCEPT_MODIFIED 0x100u /* accept overlays/archives whose SHA-1 is not the
                                           expected one (1ST_READ.BIN must always match) */
#define SA1_LOAD_WORLD_IMAGES    0x200u /* also compose the world images of the loaded characters that can tick
                                           (sa1_character_prepare): the first world of each kind is then cheap */
#define SA1_LOAD_SUPER_FORMS     0x400u /* API 0.4 (SA1_API_SUPER): Sonic also loads the SUPERSONIC archive (his
                                           archive 5, after the common ones), the textures of his Super form in a
                                           SA1_CREATE_SUPER_CAPABLE world (needs SA1_LOAD_SUPER; without it such a
                                           world is SA1_ERR_NOT_LOADED). Super Sonic always loads Sonic's archives */
#define SA1_LOAD_DEFAULT        (SA1_LOAD_BOARDS | SA1_LOAD_SUPER)
#define SA1_LOAD_ALL            (SA1_LOAD_BOARDS | SA1_LOAD_SUPER | SA1_LOAD_MINICART | SA1_LOAD_STAGE_ACTIONS)

typedef struct sa1_load_options {
    uint32_t struct_size;
    uint32_t characters;        /* bit mask of (1u << SA1_CHAR_*); 0 = all */
    uint32_t flags;             /* SA1_LOAD_* */
} sa1_load_options;

/*
 * Opens a .gdi (2352- or 2048-byte sectors; track files must be bare file names in
 * the .gdi's directory), reads 1ST_READ.BIN and checks its SHA-1, reads and checks
 * the overlays and texture archives the selected characters need, and converts all
 * assets into native structures.  The disc is closed again before returning; no game
 * data is written anywhere.  UNC (\\server\...), \\?\ and \\.\ device paths are
 * rejected.  Returns SA1_OK, SA1_PARTIAL (some optional overlay/archive missing or
 * rejected; see the log), or an error.  Loading again replaces the previous assets
 * (all characters are destroyed).  `options` may be NULL (all characters,
 * SA1_LOAD_DEFAULT).
 */
SA1_API sa1_status sa1_load_disc(sa1_context *ctx, const char *gdi_path, const sa1_load_options *options);
/* Frees all assets and destroys all characters. */
SA1_API void       sa1_unload_disc(sa1_context *ctx);

/* Files read from the disc during the last load (diagnostics). */
#define SA1_HASH_OK        0   /* SHA-1 matches the supported version */
#define SA1_HASH_MISMATCH  1   /* known file, different content (rejected unless ACCEPT_MODIFIED) */
#define SA1_HASH_UNKNOWN   2   /* file not in the manifest (rejected unless ACCEPT_MODIFIED) */

typedef struct sa1_disc_file_info {
    uint32_t    struct_size;
    const char *name;           /* ISO path, e.g. "SONICADV/S_MOT.PRS" */
    uint32_t    size;           /* bytes on disc */
    uint32_t    unpacked_size;  /* after PRS decompression (= size if not compressed) */
    char        sha1[41];       /* hex SHA-1 of the bytes on disc */
    int32_t     hash_status;    /* SA1_HASH_* */
    int32_t     used;           /* 1 = accepted and used */
    double      read_ms;        /* time to read + hash + decompress */
} sa1_disc_file_info;

SA1_API uint32_t   sa1_disc_file_count(const sa1_context *ctx);
SA1_API sa1_status sa1_disc_file(const sa1_context *ctx, uint32_t index, sa1_disc_file_info *out);

/* ------------------------------------------------------------------------- */
/* Asset queries                                                             */
/* ------------------------------------------------------------------------- */

typedef struct sa1_character_info {
    uint32_t    struct_size;
    int32_t     kind;               /* SA1_CHAR_* */
    const char *name;               /* "sonic", "tails", ... */
    int32_t     loaded;             /* 1 if the character can be created */
    uint32_t    num_nodes;          /* main model: NJS_OBJECT nodes */
    uint32_t    num_vertices;       /* main model: vertices of drawn nodes */
    uint32_t    num_triangles;      /* main model: triangles of drawn nodes */
    uint32_t    max_vertices;       /* max over the main model and every available action object */
    uint32_t    max_triangles;      /* size sa1_mesh buffers for this many triangles */
    uint32_t    num_actions;        /* entries in the action table (PL_ACTION) */
    uint32_t    num_actions_available;
    uint32_t    num_motions;        /* distinct NJS_MOTIONs loaded */
    uint32_t    num_welds;          /* weld entries that resolved against loaded objects */
    uint32_t    num_archives;       /* texture archives; archive 0 textures the character model */
    uint32_t    main_object_addr;   /* SH-4 address of the root NJS_OBJECT */
} sa1_character_info;

SA1_API sa1_status  sa1_character_info_get(const sa1_context *ctx, int32_t kind, sa1_character_info *out);
SA1_API const char *sa1_character_name(int32_t kind);                 /* NULL if out of range */
SA1_API int32_t     sa1_character_kind_from_name(const char *name);   /* -1 if unknown */

#define SA1_ACTION_OK          0
#define SA1_ACTION_EMPTY       1    /* NULL entry in the table */
#define SA1_ACTION_NOT_LOADED  2    /* lives in an overlay that was not loaded */
#define SA1_ACTION_INVALID     3    /* failed validation */

typedef struct sa1_action_info {
    uint32_t    struct_size;
    int32_t     status;             /* SA1_ACTION_* */
    uint32_t    num_frames;         /* NJS_MOTION.nbFrame */
    uint32_t    num_nodes;          /* nodes of the action's object */
    int32_t     mode;               /* PL_ACTION.mtnmode (3 loop, 4 once then `next`, ...) */
    uint32_t    next;               /* PL_ACTION.next */
    float       blend;              /* PL_ACTION.frame (transition rate) */
    float       speed;              /* PL_ACTION.racio (playback speed) */
    uint32_t    motion_type;        /* NJD_MTYPE_* bits (1 POS, 2 ANG, 4 SCL) */
    uint32_t    action_addr;        /* SH-4 addresses */
    uint32_t    object_addr;
    uint32_t    motion_addr;
    const char *image;              /* file holding the NJS_ACTION, e.g. "S_MOT.PRS" ("" if none) */
} sa1_action_info;

SA1_API sa1_status sa1_action_info_get(const sa1_context *ctx, int32_t kind, uint32_t action, sa1_action_info *out);

/* player_parameter (0x84 bytes per character in 1ST_READ.BIN at 0x8C1BDFE4).
 * Values are in SA1 units per 60 Hz frame, exactly as on the disc. */
typedef struct sa1_physics {
    int32_t jump2_timer;
    float pos_error, lim_h_spd, lim_v_spd, max_x_spd, max_psh_spd, jmp_y_spd, nocon_speed,
          slide_speed, jog_speed, run_speed, rush_speed, crash_speed, dash_speed, jmp_addit,
          run_accel, air_accel, slow_down, run_break, air_break, air_resist_air, air_resist,
          air_resist_y, air_resist_z, grd_frict, grd_frict_z, lim_frict, rat_bound, rad,
          height, weight, eyes_height, center_height;
} sa1_physics;

/* Super Sonic has no entry of his own and reports Sonic's (his world's physics task 8C0B32B6 overrides air_resist,
 * air_resist_air, air_accel and center_height while he is Super: see "Super Sonic (API 0.4)"). */
SA1_API sa1_status sa1_physics_get(const sa1_context *ctx, int32_t kind, sa1_physics *out);

/* ------------------------------------------------------------------------- */
/* Textures                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct sa1_texture_info {
    uint32_t       struct_size;
    uint32_t       width, height;
    const uint8_t *rgba;            /* width*height*4 bytes, RGBA8, top row first; NULL if
                                       the texture could not be decoded */
    const char    *name;            /* PVM entry name ("" if none) */
    uint32_t       global_index;    /* GBIX */
    uint32_t       pixel_format;    /* PVR pixel format (0 ARGB1555, 1 RGB565, 2 ARGB4444) */
    uint32_t       data_format;     /* PVR data format (1 twiddled, 2 +mips, 3 VQ, 4 VQ+mips,
                                       9 rectangle, 0x10 small VQ, 0x11 small VQ+mips) */
    /* API 0.2 render draft */
    uint32_t       alpha;           /* SA1_TEXTURE_ALPHA_*: what the alpha channel holds */
} sa1_texture_info;

#define SA1_TEXTURE_ALPHA_NONE   0u /* every texel opaque (RGB565 and most others) */
#define SA1_TEXTURE_ALPHA_BINARY 1u /* only 0 and 255 (ARGB1555 cut-outs): alpha testing suffices */
#define SA1_TEXTURE_ALPHA_BLEND  2u /* graded alpha (ARGB4444): needs blending */

/* Texture archives of a character in the order the game loads them; archive 0
 * holds the textures the character's model uses (material texture id = index). */
SA1_API sa1_status sa1_texture_archive(const sa1_context *ctx, int32_t kind, uint32_t archive,
                                       const char **name, uint32_t *num_textures);
/* Pixel pointers stay valid until sa1_unload_disc / sa1_shutdown. */
SA1_API sa1_status sa1_texture_get(const sa1_context *ctx, int32_t kind, uint32_t archive,
                                   uint32_t index, sa1_texture_info *out);

/* round 0.9.4a (SA1_API_SUPER_POLISH; sadc docs/research/super2_plan.md 5.3): texture variants made by libsa1 */
#define SA1_TEXTURE_VARIANT_SUPER_GOLD 1u /* every texel's HSV value and saturation kept, the hue SA1_SUPER_GOLD_HUE, alpha
                                             kept. Available for SON_EFF lcharge01-04 of Sonic (kind 0) loaded with
                                             SA1_LOAD_SUPER_FORMS; any other texture: SA1_ERR_NOT_FOUND */
#define SA1_SUPER_GOLD_HUE    45        /* degrees: the weighted mean hue (45.1) of the game's s1sonic01-08 and ss_ball_a / _b */
/* a texture variant made by libsa1 from the game's texture (sa1_texture_info as sa1_texture_get fills it; the pixels live
 * as long as sa1_texture_get's). Any other kind, archive, texture or variant, or Sonic loaded without the forms:
 * SA1_ERR_NOT_FOUND */
SA1_API sa1_status sa1_texture_variant_get(const sa1_context *ctx, int32_t kind, uint32_t archive, uint32_t index,
                                           uint32_t variant, sa1_texture_info *out);

typedef struct sa1_atlas_rect {
    uint32_t x, y, w, h;            /* texel rectangle of the texture (without gutter) */
} sa1_atlas_rect;

/*
 * Packs every texture of one archive into a single RGBA8 image (deterministic shelf
 * packing, power-of-two size, 2-texel edge-replicated gutter).  Call with
 * rgba_out == NULL to query the size into *width / *height.  When rgba_out is given,
 * *width / *height must be at least the queried size (rgba_out holds width*height*4
 * bytes; the image is written with that row pitch).  Mesh UVs stay texture-relative
 * (character materials clamp or tile per texel), so a host that renders from the
 * atlas remaps them itself:  u_atlas = (rect.x + u * rect.w) / width.
 * rects_out (optional) receives one rect per texture (undecodable: all zero).
 */
SA1_API sa1_status sa1_texture_atlas(const sa1_context *ctx, int32_t kind, uint32_t archive,
                                     uint8_t *rgba_out, uint32_t *width, uint32_t *height,
                                     sa1_atlas_rect *rects_out, uint32_t rects_capacity);

/* ------------------------------------------------------------------------- */
/* Characters                                                                */
/* ------------------------------------------------------------------------- */

typedef int32_t sa1_character_id;       /* > 0; 0 is never a valid id */

/* Material flags per triangle: the Ninja NJS_MATERIAL attribute word, unchanged. */
#define SA1_MAT_CLAMP_V          0x00008000u
#define SA1_MAT_CLAMP_U          0x00010000u
#define SA1_MAT_FLIP_V           0x00020000u
#define SA1_MAT_FLIP_U           0x00040000u
#define SA1_MAT_IGNORE_SPECULAR  0x00080000u
#define SA1_MAT_USE_ALPHA        0x00100000u
#define SA1_MAT_USE_TEXTURE      0x00200000u
#define SA1_MAT_USE_ENV          0x00400000u   /* sphere-mapped (environment) texture */
#define SA1_MAT_DOUBLE_SIDE      0x00800000u
#define SA1_MAT_USE_FLAT         0x01000000u
#define SA1_MAT_IGNORE_LIGHT     0x02000000u
#define SA1_MAT_FILTER(m)        (((m) >> 13) & 3u)   /* 0 point, 1 bilinear, 2 trilinear */
#define SA1_MAT_SRC_BLEND(m)     (((m) >> 29) & 7u)
#define SA1_MAT_DST_BLEND(m)     (((m) >> 26) & 7u)

/* Per-triangle flags in sa1_geometry.tri_flags. */
#define SA1_TRI_HAS_UV           0x01u
#define SA1_TRI_HAS_VCOLOR       0x02u

/* Attach points from the weld table (hand/foot/user positions). */
#define SA1_ATTACH_RIGHT_HAND  0
#define SA1_ATTACH_LEFT_HAND   1
#define SA1_ATTACH_RIGHT_FOOT  2
#define SA1_ATTACH_LEFT_FOOT   3
#define SA1_ATTACH_USER0       4
#define SA1_ATTACH_USER1       5
#define SA1_ATTACH_COUNT       6

/* Triangle-soup output into CALLER-owned arrays (libsm64 style).  Every array
 * pointer may be NULL to skip that stream; sizes are per triangle. */
typedef struct sa1_mesh {
    uint32_t  struct_size;
    uint32_t  max_triangles;        /* capacity of the arrays below */
    float    *position;             /* 9 floats: 3 corners x (x,y,z), host space */
    float    *normal;               /* 9 floats, unit length, host space */
    float    *uv;                   /* 6 floats: (u,v) per corner; 1.0 = texture size,
                                       origin top-left, v grows downwards */
    uint8_t  *color;                /* 12 bytes: RGBA8 per corner (material diffuse or
                                       vertex colour) */
    int16_t  *texture;              /* 1: texture index in archive `archive` (archive 0 without that stream), -1 =
                                       untextured */
    uint32_t *material;             /* 1: SA1_MAT_* attribute flags */
    uint32_t  num_triangles;        /* out: triangles written */
    uint32_t  triangles_needed;     /* out: triangles of the full pose */
    /* API 0.2 render draft (SA1_API_RENDER): written only when struct_size covers them; NULL = skipped. Without them a
       mesh holds only the triangles of archive 0 (the character's own textures); with `archive` it also holds the
       triangles of effect models (other archives) and of 3D polygons (sprites and lines: sa1_character_render). */
    int16_t  *archive;              /* 1: texture archive of `texture` (sa1_texture_get), -1 = untextured */
    uint32_t *draw_flags;           /* 1: SA1_DRAW_* of the triangle's draw */
    uint32_t *order;                /* 1: the draw's order (sa1_draw.order): blended triangles in this order */
    uint8_t  *blend;                /* 2: SA1_BLEND_* source, destination factor */
} sa1_mesh;

/* Library-owned INDEXED geometry of the last pose of a character (valid until the
 * next pose/tick/destroy of that character, or unload/shutdown).  A ticking character's is
 * every triangle of its last pair (render draft: effect models too, with `textures` -1 where
 * the texture is not in archive 0; sa1_character_render names every archive). */
typedef struct sa1_geometry {
    uint32_t        num_vertices;
    const float    *positions;      /* 3 per vertex, host space */
    const float    *normals;        /* 3 per vertex, unit length */
    uint32_t        num_triangles;
    const uint32_t *indices;        /* 3 per triangle (vertex indices) */
    const float    *uvs;            /* 6 per triangle (per corner) */
    const uint8_t  *colors;         /* 12 per triangle (RGBA8 per corner) */
    const int16_t  *textures;       /* 1 per triangle */
    const uint32_t *materials;      /* 1 per triangle */
    const uint8_t  *tri_flags;      /* 1 per triangle: SA1_TRI_* */
    uint32_t        num_nodes;      /* nodes of the posed object */
    const float    *node_matrices;  /* 12 per node: row-major 3x4 (column vectors),
                                       node -> host space, including the scale */
    uint32_t        attach_valid;   /* bit mask of (1 << SA1_ATTACH_*) */
    float           attach[SA1_ATTACH_COUNT][3];  /* host space (provisional: origin of the
                                                     weld entry's source node) */
    int32_t         action;         /* action that was posed (-1 = bind pose) */
    uint32_t        object_addr;    /* SH-4 address of the posed NJS_OBJECT (as referenced) */
} sa1_geometry;

/* Pose flags */
#define SA1_POSE_NO_WELD      0x1u   /* skip the PL_JOIN_VERTEX welds (raw rigid parts) */
#define SA1_POSE_MODEL_SPACE  0x2u   /* ignore position/yaw and the 180-degree world turn:
                                        output the model as stored (then axis + scale) */
#define SA1_POSE_BIND         0x4u   /* ignore action/frame: bind pose of the main model */

typedef struct sa1_pose {
    uint32_t struct_size;
    int32_t  action;            /* index into the character's action table */
    float    frame;             /* motion frame; wrapped into [0, nbFrame) */
    float    position[3];       /* host space */
    float    yaw;               /* radians, counter-clockwise about the host up axis
                                   (= SA1 +Y); 0 = facing SA1 world +X */
    uint32_t flags;             /* SA1_POSE_* */
} sa1_pose;

/* Controller input for sa1_character_tick. */
#define SA1_BUTTON_JUMP    0x01u    /* A */
#define SA1_BUTTON_ACTION  0x02u    /* B / X: spin dash, attack */
#define SA1_BUTTON_ACTION2 0x04u    /* Y */
#define SA1_BUTTON_L       0x08u
#define SA1_BUTTON_R       0x10u
#define SA1_BUTTON_START   0x20u

typedef struct sa1_input {
    uint32_t struct_size;
    float    stick_x, stick_y;  /* -1..1, +y = forward relative to the camera */
    float    camera_yaw;        /* radians, host space, same convention as sa1_pose.yaw */
    uint32_t buttons;           /* SA1_BUTTON_* held */
    /* round 0.9.2 (SA1_API_092): read only when struct_size covers it; 0 = the host says nothing (the game's own rules) */
    uint32_t host_flags;        /* SA1_INPUT_HOST_* */
    /* round 0.9.5 (SA1_INPUT_HOST_CAMERA; sadc docs/research/gsa1_095_plan.md 5.3, board095_cam.md 5.2): read only when
       struct_size covers host_cam_target AND host_flags has SA1_INPUT_HOST_CAMERA */
    float    host_cam_pos[3];   /* host space: the eye of the view the host shows for this character */
    float    host_cam_target[3];/* host space: a point that view looks at (kept in the world; unused by 0.9.5's step) */
} sa1_input;                    /* 48 bytes (24 before round 0.9.5) */

/* sa1_input.host_flags (round 0.9.2; sadc docs/research/gsa1_090_water.md WTR-A with its Review R4 a-c, R3.3, R3.5).
 * The host's own water at the character for the pairs of this tick: the character's probe point (its feet + 1 SA1 unit,
 * from the state after the previous tick: sa1_state.position, not an interpolated draw position) inside host water (a
 * Source engine host: util.PointContents & (CONTENTS_WATER | CONTENTS_SLIME)). Without KNOWN the world follows the game's
 * own rule (the lowest water surface above the character, 8C0925A0), byte for byte as before. With KNOWN, in every world
 * but E-102's (his main has no water probe; he hovers over water as land):
 *   KNOWN, not IN_WATER  the collision window leaves out every water object that has a water polygon on the vertical line
 *                        above the character's probe point (a dry room or sewer under host water stays dry; water below
 *                        the character - its shadow, landing in it - is unaffected);
 *   KNOWN | IN_WATER     the deep-water window rule W1 without its land-between test (deep water under a submerged ledge
 *                        stays reachable). Where the host's geometry cut the water top away over the character (a dock, a
 *                        walkway on the surface), no water polygon lies above it and it stays dry: the flag cannot create
 *                        water the window does not have.
 * The flag is up to one host tick late on a hitch (the pairs of a tick share it). */
#define SA1_INPUT_HOST_WATER_KNOWN 0x1u
#define SA1_INPUT_HOST_IN_WATER    0x2u
/* ---- sa1_input (round 0.9.5, the host camera; sadc docs/research/gsa1_095_plan.md 5.3, board095_cam.md 5.2) -------------
 * The two fields at the END of sa1_input, read only when struct_size covers host_cam_target AND host_flags has
 * SA1_INPUT_HOST_CAMERA. With the bit, every live step of this tick holds host_cam_pos (converted to SA1 space with the
 * context's coords, inside the tick in the world's FP mode) in the world's camera taskwk (camera_twp, D_8C18B7EC) instead of
 * the virtual eye 40 units behind the character: the game code that reads the camera's POSITION measures against the view
 * the host shows (Big's line alpha 8C0EC60C; the SET spawner's centre 8C091380 while Big's lure is in the water; libsa1's SET
 * window order beyond 400 units). Unchanged: the camera's angles (ang.y follows camera_yaw; ang.x, ang.z, smode 0) and XMTRX
 * (the virtual view the game's visibility tests use). Ignored (the virtual eye) when any of the six floats is not finite or
 * above 1e30 in magnitude, in replays, preview worlds and with set_no_eye. Latched per tick: every step of the tick uses the
 * same camera. */
#define SA1_INPUT_HOST_CAMERA 0x4u

/* sa1_state.flags */
#define SA1_STATE_ON_GROUND  0x01u  /* touching land collision (the game's taskwk.flag & 3) */
#define SA1_STATE_TICKING    0x02u  /* the state comes from the character's world (else: create / last pose) */
#define SA1_STATE_FAULTED    0x04u  /* the world faulted: the state is the last one before the fault */
#define SA1_STATE_SUPER      0x08u  /* API 0.4 (SA1_API_SUPER): player 0 is Super Sonic (playerwk.equipment & 0x8000) */

typedef struct sa1_state {
    uint32_t struct_size;
    float    position[3];       /* host space */
    float    velocity[3];       /* host units per second */
    float    yaw;               /* radians, the sa1_pose.yaw convention (counter-clockwise about the host up
                                   axis, 0 = facing SA1 world +X) */
    int32_t  action;            /* index into the character's action table (the game's current action) */
    float    frame;             /* motion frame of that action */
    uint32_t flags;             /* SA1_STATE_* */
    /* API 0.2 */
    uint32_t surface;           /* SA1_SURFACE_* of the land the character stands on (from the game's
                                   attribute word, sa1_state_ex.attr) */
} sa1_state;

/* Creates a character instance (assets of `kind` must be loaded).  position may be NULL.
 * API 0.2: the instance can also tick; its world (16 MB, the game's own logic, design D2) is built
 * by the first sa1_character_tick, starting at `position` / `yaw` (NULL position: the game's own
 * start position of Emerald Coast act 1).  Characters that are only posed never get a world.
 * sa1_character_create_ex chooses the world, start and stage explicitly. */
SA1_API sa1_status sa1_character_create(sa1_context *ctx, int32_t kind, const float position[3],
                                        float yaw, sa1_character_id *out_id);
SA1_API sa1_status sa1_character_destroy(sa1_context *ctx, sa1_character_id id);

/* Runs whole game "pairs" from an accumulator: dt seconds are added and every full 1/30 s runs two
 * 60 Hz logic steps exactly as the game's frame loop does (input sampled per pair, presses since the
 * last pair delivered to its first step; at most 8 pairs per call, a larger backlog is dropped).
 * The first tick builds the character's world (see sa1_character_create; its errors are returned, a world
 * without land - no static collision - is only logged).  state_out (may be NULL)
 * gets the state after the last step; mesh_out (may be NULL) the triangles the game drew in the last pair
 * (display capture: the character's own textures, archive 0; with sa1_mesh.archive also effect models; not
 * interpolated: sa1_character_render / sa1_character_mesh_lerp), before the first pair the CPU pose.  Returns SA1_OK, SA1_ERR_GAME_FAULT (the world is dead until the character
 * is recreated; sa1_last_error names the address and the game function), SA1_ERR_NOT_IMPLEMENTED (no
 * game code in this build, or the game reached a function libsa1 does not have), SA1_ERR_UNSUPPORTED
 * (this kind cannot tick yet: Eggman, Tikal; or a puppet) or the world creation error. Super Sonic ticks since API 0.4
 * (SA1_API_SUPER: see "Super Sonic (API 0.4)"). */
SA1_API sa1_status sa1_character_tick(sa1_context *ctx, sa1_character_id id, const sa1_input *input,
                                      float dt, sa1_state *state_out, sa1_mesh *mesh_out);

/* Poses a character (remote/puppet path and asset viewer): evaluates `pose->action`
 * at `pose->frame` on the CPU, applies the welds and writes the triangle soup into
 * `mesh_out` (may be NULL).  The indexed form is available via sa1_character_geometry.
 * If mesh_out->max_triangles is too small, nothing is written to mesh_out except
 * triangles_needed and SA1_ERR_BUFFER_TOO_SMALL is returned (the indexed geometry
 * is still updated). */
SA1_API sa1_status sa1_character_pose(sa1_context *ctx, sa1_character_id id, const sa1_pose *pose,
                                      sa1_mesh *mesh_out);
SA1_API sa1_status sa1_character_geometry(const sa1_context *ctx, sa1_character_id id,
                                          const sa1_geometry **out);

/* ------------------------------------------------------------------------- */
/* Render output (API 0.2 draft)                                             */
/* ------------------------------------------------------------------------- */
/*
 * What the game draws for a character, as a list of draws in the game's own order (sa1_character_render):
 *
 *   - triangles of models: the character's body (its renderer 8C037098), effect models drawn with the
 *     character's effect archives (Sonic's spin dash trail with SON_EFF, Knuckles' punch, E-102's beam),
 *     the game's 3D triangle / polygon draws (Tails' tail trail, Amy's hammer whirl);
 *   - sprites (njDrawSprite3D and the game's own 3D sprite draw 8C09E778): camera-facing quads the host
 *     orients itself;
 *   - line segments (njDrawLine3D: E-102's targeting lines);
 *   - the blob shadow (SA1_DRAW_SHADOW): the game's own shadow under the character (a triangle draw).
 *
 * Each draw names its texture by archive and index (sa1_texture_get(kind, archive, index): archive 0 is
 * the model's texture set, archives 1.. the effect sets and the common archives (EFF_REGULAR; OBJ_REGULAR, the
 * blob shadow's) in the game's own load order, sa1_texture_archive),
 * the Ninja material attribute word as the game drew it, and the render state derived from it:
 *
 *   opaque           no SA1_DRAW_BLEND: depth test and write.  Many character materials set
 *                    SA1_MAT_USE_ALPHA without anything translucent (opaque texture, vertex alpha 255);
 *                    the Dreamcast sorts those per pixel, so they are opaque here (SA1_DRAW_USE_ALPHA marks
 *                    them).  Example: Sonic's irises lie 0.05 units in front of his eye whites but are
 *                    drawn before them: a host that draws them as translucent in order hides the irises.
 *   alpha test       SA1_DRAW_ALPHA_TEST: a texture with 1-bit alpha; discard texels below 0.5.
 *   blended          SA1_DRAW_BLEND: colour = src * blend_src + dst * blend_dst (SA1_BLEND_*; additive:
 *                    SA1_DRAW_ADDITIVE); draw after the opaque draws in `order`, depth test without depth
 *                    writes.  Tails' fading tail copies (SA1_DRAW_AFTERIMAGE) and most effects.
 *   culling          front faces are counter-clockwise (SA1_COORDS_FLIP_WINDING: clockwise); cull the
 *                    back faces unless SA1_DRAW_DOUBLE_SIDED.
 *   texture address  SA1_MAT_CLAMP_U / _V clamp, SA1_MAT_FLIP_U / _V mirror (repeat mirrored), else repeat.
 *   lighting         SA1_DRAW_PALETTE_LIT (the character renderer): texture x the host's lighting, vertex RGB
 *                    is white (the game lights from the stage's light palette and ignores the material's
 *                    diffuse colour, which is only informational in sa1_draw.diffuse); SA1_DRAW_UNLIT: no
 *                    lighting; other draws: vertex colour (diffuse or vertex colours) x texture x lighting.
 *   SA1_DRAW_ENV_MAP sphere map: UV = view-space normal.xy * 0.5 + 0.5 (the mesh UVs are not used).
 *
 * Interpolation.  The game draws once per 1/30 s pair.  sa1_character_render(alpha) interpolates every drawn
 * model of the last pair with its draw in the previous pair (the n-th draw of a model with the n-th draw of the
 * same model) as a transform: the rotation turns (quaternion, the shorter way; the way and the number of turns
 * the model's node makes in the game's motion when the action is steady: a spin dash ball turns up to 216
 * degrees per pair; an ambiguous turn of more than 150 degrees without that takes the last pair's rotation),
 * stretch and translation are lerped, and the model's points as the game drew them are transformed - a turning
 * part keeps its shape (a vertex lerp flattened the spin ball).  A model rigidly attached to another drawn model
 * (the eyes, irises and pupils on a head, decals) is interpolated relative to it, so it stays glued to it
 * between the pairs (in a turn, models interpolated each on its own cut different chords: Big's eye whites sank
 * behind his face); the others are interpolated in the drawn root's frame (sa1_render.position / yaw), so a turn of
 * the whole character turns them about the root.  Points the game changed between the pairs
 * (welds, morphs, shape motion) are lerped in the model's space, and welded copies keep following the vertex
 * they copy.  A draw of the last pair only (effects come and go) moves with the character (by sa1_render.position
 * minus the last pair's root; the blob shadow only horizontally: it stays on the ground the game found); sprite and
 * line positions are lerped when the two pairs drew the same ones.  When no
 * draw matches the previous pair (Knuckles' jump draws the ball and the curled body in turn), the whole last pair
 * moves with the root: interpolated 0 with SA1_RENDER_MOVED_WITH_ROOT; a host must not move such a frame again.
 * The character's position and yaw (sa1_render.position / yaw) are always interpolated, and they are the root of
 * the DRAWN step: the game draws a pair on its first 60 Hz step (the character renderer 8C037098 draws on sub step 0
 * only), so the drawn body stands one step behind sa1_state (the state after the pair).  A camera hung on
 * sa1_render.position therefore moves with the body (libsa1 of G-SA1 0.5 and before used the state's position: the
 * body trailed the camera by one step of movement and caught up a pair later on steps, landings, falls and the Light
 * Speed Dash, sadc docs/research/gsa1_vertical_flicker.md).  Game logic, sound and collision use sa1_state.
 * alpha 0 = the previous pair, 1 = the last pair; alpha < 0 =
 * the library's own accumulator (the part of the dt given to sa1_character_tick that did not make a whole
 * pair yet, / (1/30 s)), so a host that ticks every frame renders with -1 and is smooth at any frame rate,
 * one pair (33 ms) behind the game.  The output always lies between the last two pairs the game ran: a tick
 * that runs several pairs (a frame of 33 ms or more) captures the last two of them (libsa1 0.2 before the 0.6.0
 * fixes captured only the last one, so the root and the body lagged up to k - 1 pairs behind after a k-pair tick
 * and caught up with a lurch: sadc docs/research/gsa1_060_pops.md).  Characters that are only posed (puppets) have
 * no previous mesh.
 */
#define SA1_API_RENDER 1

/* blend factors: the Ninja material encoding (SA1_MAT_SRC_BLEND / SA1_MAT_DST_BLEND), PowerVR's */
#define SA1_BLEND_ZERO            0u
#define SA1_BLEND_ONE             1u
#define SA1_BLEND_OTHER_COLOR     2u    /* as source factor: the destination colour; as destination: the source colour */
#define SA1_BLEND_INV_OTHER_COLOR 3u
#define SA1_BLEND_SRC_ALPHA       4u
#define SA1_BLEND_INV_SRC_ALPHA   5u
#define SA1_BLEND_DST_ALPHA       6u
#define SA1_BLEND_INV_DST_ALPHA   7u

/* sa1_draw.prim */
#define SA1_PRIM_TRIANGLES 0u   /* first / count: triangles of sa1_render (positions ... colors) */
#define SA1_PRIM_SPRITES   1u   /* first / count: sa1_render.sprites */
#define SA1_PRIM_LINES     2u   /* first / count: line segments (2 sa1_render.line_vertices each) */

/* sa1_draw.source: the game's draw call */
#define SA1_SOURCE_CHARACTER 0u /* the character renderer 8C037098 / 8C03700C (bodies, effect models of the motion player) */
#define SA1_SOURCE_MODEL     1u /* njDrawModel / njDrawObject / the Ninja motion draws */
#define SA1_SOURCE_SPRITE    2u /* njDrawSprite3D, the game's 3D sprite draw 8C09E778 */
#define SA1_SOURCE_LINE      3u /* njDrawLine3D */
#define SA1_SOURCE_POLYGON   4u /* the 3D triangle / polygon draws (8C63D414 / 8C63D66C) */
#define SA1_SOURCE_POSE      5u /* the CPU pose evaluator (puppets; a ticking character before its first pair) */

/* sa1_draw.flags */
#define SA1_DRAW_BLEND        0x0001u   /* blended: see above */
#define SA1_DRAW_ALPHA_TEST   0x0002u   /* 1-bit texture alpha: discard texels with alpha < 0.5 */
#define SA1_DRAW_DOUBLE_SIDED 0x0004u   /* no back-face culling */
#define SA1_DRAW_ENV_MAP      0x0008u   /* environment (sphere) map */
#define SA1_DRAW_UNLIT        0x0010u   /* no lighting: the vertex colour as it is */
#define SA1_DRAW_PALETTE_LIT  0x0020u   /* lit by the stage's light palette: texture x host lighting, vertex RGB white */
#define SA1_DRAW_EFFECT       0x0040u   /* an effect: another archive than 0 (the effect sets, and the common archives
                                           such as OBJ_REGULAR of the blob shadow, SA1_DRAW_SHADOW), a sprite, a line
                                           or a 3D polygon */
#define SA1_DRAW_NO_TEXTURE   0x0080u   /* textured in the game from an archive libsa1 does not load: untextured here */
#define SA1_DRAW_AFTERIMAGE   0x0100u   /* a fading copy (constant material with an alpha offset) */
#define SA1_DRAW_ADDITIVE     0x0200u   /* blend_dst is SA1_BLEND_ONE */
#define SA1_DRAW_USE_ALPHA    0x0400u   /* the material sets SA1_MAT_USE_ALPHA but nothing is translucent: opaque */
#define SA1_DRAW_SHADOW       0x0800u   /* the game's blob shadow (Character_Shadow_Blob, drawn by the shadow code 8C09247E
                                           / 8C09222C / 8C092124 ...): a flat quad the game put 0.3 units above the
                                           ground it found under the character, tilted to that ground, turned with the
                                           character and shrunk with its height; it disappears where the game finds no
                                           ground in reach. Its texture (OBJ_REGULAR "shadow") is black with the shape
                                           in its alpha, SA/ISA blended with the diffuse alpha 204. It also carries
                                           SA1_DRAW_EFFECT (another archive than the model's), so an sa1_mesh without
                                           the render streams (the v0.1 / v0.2 mesh_out) leaves it out with the effects;
                                           sa1_mesh.draw_flags and sa1_render carry it. A host draws it with its own
                                           switch, whatever it does with effects (libsa1 0.2 and before left it out) */
#define SA1_DRAW_OBJECT       0x1000u   /* round 0.9.4 (SA1_API_SET): a game object - a ring, a ring's sparkle or blob
                                           shadow, an item box - drawn by its own task (the capture's exec: the rings,
                                           the ring groups, the dropped / dug rings, both item boxes); interpolated in
                                           world space, never moved with the character's root; a host draws it with its
                                           object switch, in first person too, whatever it does with SA1_DRAW_EFFECT
                                           (which it also carries). Never merged with a draw without it */
#define SA1_DRAW_SUPER_GOLD   0x2000u   /* round 0.9.4a (SA1_SUPOPT_LSD_GOLD): shown in Super Sonic's gold. Textured: draw the
                                           texture's SA1_TEXTURE_VARIANT_SUPER_GOLD (sa1_texture_variant_get) in place of the
                                           texture (archive / texture stay the game's); untextured: the vertex colours are
                                           gold already. Never merged with a draw without it */
#define SA1_DRAW_BOARD        0x4000u   /* round 0.9.5 (SA1_API_BOARD): the board task's draws (execs 8C0B2F46 / 8C0BF804: its
                                           model and its motion draw 8C60E080) and SA1_PREVIEW_BOARD's: the board under
                                           Sonic's / Tails' feet or left on the ground. They are also SA1_DRAW_EFFECT
                                           (another archive than 0): a host draws them with the character, in first person
                                           too, whatever it does with effects. Never merged with a draw without the bit,
                                           never moved with the root. A board task's draws are left out until it was held
                                           once (its first pairs at the SA1 origin): presentation only */

typedef struct sa1_draw {
    uint32_t prim;                  /* SA1_PRIM_* */
    uint32_t first, count;          /* range in the sa1_render array of the primitive */
    int32_t  archive;               /* texture: sa1_texture_get(kind, archive, texture); -1 = untextured */
    int32_t  texture;
    uint32_t material;              /* the Ninja attribute word as the game drew it (SA1_MAT_*, the constant attribute
                                       masks of the draw applied); sprites / lines / polygons: 0 */
    uint32_t flags;                 /* SA1_DRAW_* */
    uint8_t  blend_src, blend_dst;  /* SA1_BLEND_* (used with SA1_DRAW_BLEND) */
    uint8_t  source;                /* SA1_SOURCE_* */
    uint8_t  reserved;
    uint32_t order;                 /* the game's draw order: increasing along sa1_render.draws */
    uint8_t  diffuse[4];            /* the material's diffuse RGBA as stored (informational) */
} sa1_draw;

typedef struct sa1_sprite {
    float   position[3];            /* the sprite's anchor, host space */
    float   rect[4];                /* x0, y0, x1, y1: the quad in the camera plane around the anchor, host units; +x is
                                       the camera's right, +y screen down (before the rotation). The game's size:
                                       tanim px x sp->sx by tanim px x sp->sy, times the matrix's scale for unscaled
                                       sprites (no NJD_SPRITE_SCALE); zero-size quads are not emitted */
    float   angle;                  /* radians: rotation of the quad about the anchor, clockwise on screen (unscaled
                                       sprites: with the matrix's turn in the game's view plane; a mirroring matrix
                                       flips rect and v) */
    float   uv[4];                  /* u0, v0 at (x0, y0) and u1, v1 at (x1, y1) (flips applied; 1.0 = texture size) */
    uint8_t color[4];               /* RGBA (the constant material with NJD_SPRITE_COLOR, else white) */
} sa1_sprite;

typedef struct sa1_line_vertex {
    float   position[3];            /* host space */
    uint8_t color[4];               /* RGBA */
} sa1_line_vertex;

typedef struct sa1_render {
    uint32_t struct_size;           /* sizeof(sa1_render) */
    uint32_t generation;            /* changes whenever the character drew a new pair or was posed */
    float    alpha;                 /* the interpolation factor used (0 previous pair .. 1 last pair) */
    uint32_t interpolated;          /* 1: geometry interpolated (draws of the last pair only may still have moved with
                                       the root: SA1_RENDER_MOVED_WITH_ROOT); 0 with SA1_RENDER_MOVED_WITH_ROOT: no
                                       draw matched the previous pair, every draw is the last pair's moved along with
                                       `position` (do not move it again); 0 without it: the last pair as it is (no
                                       previous pair, alpha >= 1, or a pose) */
    float    position[3];           /* the character's root at alpha, host space: the root of the drawn step (the
                                       pair's first 60 Hz step, whose display the game shows; SA1_RENDER_DRAWN_ROOT);
                                       sa1_state.position is the state after the pair, one step further.
                                       Interpolated between the last two pairs: a tick that runs several pairs
                                       captures the last two of them */
    float    yaw;                   /* the character's yaw at alpha, the same step (sa1_state.yaw convention) */
    uint32_t num_triangles;
    const float   *positions;       /* 9 per triangle: 3 corners, host space */
    const float   *normals;         /* 9 per triangle, unit length */
    const float   *uvs;             /* 6 per triangle */
    const uint8_t *colors;          /* 12 per triangle: RGBA per corner */
    uint32_t num_draws;
    const sa1_draw *draws;          /* in the game's order */
    uint32_t num_sprites;
    const sa1_sprite *sprites;
    uint32_t num_line_vertices;     /* 2 per segment */
    const sa1_line_vertex *line_vertices;
    /* render draft, 2026-10-21 (sadc docs/research/gsa1_vertical_flicker.md): present when struct_size covers it */
    uint32_t flags;                 /* SA1_RENDER_* */
} sa1_render;

/* sa1_render.flags */
#define SA1_RENDER_MOVED_WITH_ROOT 0x1u /* draws (vertices, sprites or lines) of the last pair that have no counterpart in
                                           the previous pair were moved along with `position` (by position minus the
                                           last pair's root); with interpolated 0: every draw. It counts draws of the
                                           character's own task: SA1_DRAW_OBJECT draws never move with the root (one
                                           without a counterpart is drawn where the game drew it; round 0.9.4) */
#define SA1_RENDER_DRAWN_ROOT      0x2u /* position / yaw are the root of the drawn step (the world's record of the
                                           pair's sub step 0); without it the state's (the CPU pose before the first
                                           pair or while the game's camera does not see the character, a world that
                                           kept its last state after a fault) */

/* The character's draws (see above), library-owned: valid until the next sa1_character_render, tick, pose or destroy
 * of this character (or unload / shutdown). alpha: 0..1, or < 0 for the library's accumulator. A ticking character
 * renders what its world drew (before its first pair: the CPU pose of its start); a puppet its last sa1_character_pose
 * (one draw per texture / material run, SA1_SOURCE_POSE). */
SA1_API sa1_status sa1_character_render(sa1_context *ctx, sa1_character_id id, float alpha, const sa1_render **out);
/* The interpolated triangles of sa1_character_render as a caller-owned triangle soup (sa1_mesh rules: every stream may
 * be NULL, SA1_ERR_BUFFER_TOO_SMALL with triangles_needed). Without sa1_mesh.archive only the triangles of archive 0
 * and untextured ones are written (as sa1_character_tick's mesh_out). */
SA1_API sa1_status sa1_character_mesh_lerp(sa1_context *ctx, sa1_character_id id, float alpha, sa1_mesh *mesh_out);

/* ------------------------------------------------------------------------- */
/* Characters in the game's own logic (API 0.2 draft)                         */
/* ------------------------------------------------------------------------- */
/*
 * Every ticking character has its own "world": a 16 MB image of the Dreamcast's RAM built from the
 * user's disc, in which the decompiled game code runs (sadc docs/libsa1_integration.md section 6).
 * The world is the game at a stage start: the character in player slot 0, a camera, the static
 * collision of the context (sa1_collision_set_static[_ex]: GRID, FAITHFUL or DISC; with no geometry the
 * world has no land) and its moving collision objects, the stage (level / act) the game code believes
 * it is in.
 */
#define SA1_API_CHARACTER_EX 1

/* sa1_character_desc.flags */
#define SA1_CREATE_WORLD        0x01u   /* build the world now (errors are reported here) instead of at the
                                           first tick */
#define SA1_CREATE_PUPPET       0x02u   /* never a world: posed only (sa1_character_tick: SA1_ERR_UNSUPPORTED) */
#define SA1_CREATE_SA1_SPACE    0x04u   /* position is SA1 space, used bit for bit (exactness tests) */
#define SA1_CREATE_ANGLES       0x08u   /* angle[] (the game's BAMS) instead of yaw */
#define SA1_CREATE_START_TABLE  0x10u   /* the game's own start position of the stage (position, yaw and
                                           angle are ignored) */
#define SA1_CREATE_STAGE        0x20u   /* level / act are given (else 1 / 0: Emerald Coast act 1, or the
                                           stage of DISC collision) */
#define SA1_CREATE_SEED         0x40u   /* seed is given (else the game's srand(0xDEAD0CAB)) */
#define SA1_CREATE_UPGRADES     0x80u   /* upgrades[] are given (else none) */
#define SA1_CREATE_STAGE_TASKS  0x100u  /* also the stage's own tasks, as the game's stage start creates them for
                                           level / act, as far as the stage's code is decompiled (Emerald Coast:
                                           the ocean's water plane under the character and the footprints; Red
                                           Mountain: the level task with its kill collision ...) and the stage's
                                           death zones; the world image then holds the stage overlay (STGnn from
                                           the disc, its SHA-1 must be US v1.005's) in every collision mode. For
                                           exactness tests against the game; a host level usually wants none */
#define SA1_CREATE_START_RINGS  0x200u  /* API 0.3 draft (host objects): start_rings is given (else the game's 0) */
/* API 0.4 (SA1_API_SUPER): SA1_CREATE_SUPER 0x400, SA1_CREATE_SUPER_CAPABLE 0x800, SA1_CREATE_WATER_FLOOR 0x1000, in the
 * section "Super Sonic (API 0.4)" at the end of this header */

typedef struct sa1_character_desc {
    uint32_t struct_size;       /* sizeof(sa1_character_desc) */
    int32_t  kind;              /* SA1_CHAR_* */
    uint32_t flags;             /* SA1_CREATE_* */
    float    position[3];       /* host space (SA1_CREATE_SA1_SPACE: SA1 space) */
    float    yaw;               /* radians, the sa1_pose.yaw convention */
    int32_t  angle[3];          /* SA1_CREATE_ANGLES: the game's taskwk.ang (BAMS, 0x10000 = one turn) */
    int32_t  level, act;        /* SA1_CREATE_STAGE: the game's CurrentLevel (0-42) / CurrentAct (0-7) */
    uint32_t seed;              /* SA1_CREATE_SEED: the game's rand() state */
    uint32_t upgrades[4];       /* SA1_CREATE_UPGRADES: event flags 0..127 of the game (bit n of
                                   upgrades[n / 32]); PInitialize derives the character's upgrades from them */
    /* API 0.3 draft (host objects, SA1_API_HOSTOBJ); read when struct_size covers it */
    int32_t  start_rings;       /* SA1_CREATE_START_RINGS: the ring count at the start, set when the world is built
                                   (before its first step) with the game's own 8C08A074 then 8C08A198(n); clamped to
                                   0..99, so a start never gives a 1-up. Without the flag: the game's 0, Super Sonic 50 */
    /* API 0.4 (Super Sonic, SA1_API_SUPER); read when struct_size covers it */
    uint32_t super_flags;       /* SA1_CREATE_SUPER: SA1_SUPER_* (see "Super Sonic (API 0.4)") */
} sa1_character_desc;

/* Creates a character instance like sa1_character_create, with the world described by `desc`.
 * SA1_CREATE_WORLD: the world is built and booted now and its errors are returned; SA1_PARTIAL means
 * the world was built without land (no static collision was set). */
SA1_API sa1_status sa1_character_create_ex(sa1_context *ctx, const sa1_character_desc *desc,
                                           sa1_character_id *out_id);

/* The full state after the last step (sa1_state plus the game's own player state). */
typedef struct sa1_state_ex {
    uint32_t struct_size;       /* sizeof(sa1_state_ex) */
    /* the fields of sa1_state (host space) */
    float    position[3];
    float    velocity[3];
    float    yaw;
    int32_t  action;
    float    frame;
    uint32_t flags;             /* SA1_STATE_* */
    uint32_t surface;
    /* the game's player state, bit for bit what the game stores (SA1 space, units per 60 Hz step;
       all zero without a world) */
    uint32_t step;              /* 60 Hz logic steps the world has run */
    int32_t  mode, smode;       /* taskwk.mode / smode: the character's state machine */
    uint32_t game_flags;        /* taskwk.flag (& 3: on ground) */
    uint32_t player_flags;      /* playerwk.flag */
    int32_t  angle[3];          /* taskwk.ang (BAMS; the low 16 bits count) */
    float    sa1_position[3];   /* taskwk.pos */
    float    speed[3];          /* motionwk.spd: world-space velocity per step */
    float    accel[3];          /* motionwk.acc */
    int32_t  angle_aim[3];      /* motionwk.ang_aim */
    float    player_speed[3];   /* playerwk.spd: player-space speed (x = forward) */
    float    player_accel[3];   /* playerwk.acc */
    float    floor_normal[3];   /* playerwk.floor_normal */
    float    dotp;              /* playerwk.dotp */
    uint32_t attr, last_attr;   /* playerwk.attr / last_attr: COL attribute bits of the land (raw) */
    int32_t  jump_timer, nocon_timer, breath_timer, wait_timer;
    int32_t  req_action, last_action, next_action;     /* playerwk.mj (action / frame: above) */
    uint32_t equipment;         /* playerwk.equipment: upgrade bits (16 bits; 0x8000 = Super Sonic) */
    uint32_t item;              /* playerwk.item: power-up bits (barriers ...) */
    uint32_t land_count;        /* land collision entries the world gathered in the last step */
    uint32_t rand_state;        /* the world's rand() state */
    /* hand / foot / user points of the game's weld evaluation (PJoinVertexes), host space; written when
       the game evaluates the welds (bit set in attach_valid: the point is not all zero) */
    uint32_t attach_valid;      /* bit mask of (1 << SA1_ATTACH_*) */
    float    attach[SA1_ATTACH_COUNT][3];
    float    attach_dir[SA1_ATTACH_COUNT][3];  /* direction vectors (host axes, unscaled) */
    /* API 0.3 draft (host objects, SA1_API_HOSTOBJ): written when struct_size covers them (0 without a world) */
    int32_t  rings;             /* the world's ring count (B_8C752BE4) */
    int32_t  lives;             /* B_8C752BE0 */
    uint32_t score;             /* B_8C752BF4 */
    uint32_t invulnerable_steps;/* taskwk.wtimer: steps of invulnerability left after a hit (the body blinks) */
    uint32_t hurt_steps;        /* 60 Hz steps since the character was last hurt (its wtimer became 120: the game's
                                   damage checks set exactly 120); 0xFFFFFFFF = not hurt since the world was built */
    uint32_t held_object;       /* API 0.3 (round 5): the host object the character holds (playerwk.htp), 0 = none */
    /* round 0.9.1 (the HUD, SA1_API_SCREEN): the game clock as the HUD shows it, written when struct_size covers them
       (0 without a world): B_8C752BE1 / BE2 / BE3 (minutes 0..99, seconds, frames 0..59 of a second: the HUD shows
       frames x 1.6667 as hundredths); clock_running: the clock counts (up; E-102's down) in the world's steps
       (D_8C1BF198 set and not held by SA1_COPT_E102_NO_TIME_LIMIT) */
    int32_t  time_minutes, time_seconds, time_frames;
    uint32_t clock_running;
    /* round 0.9.2 (SA1_API_092; sadc docs/research/gsa1_090_water.md WTR-LOG): the game's water probe (8C0925A0, the
       water above the character: playerwk.shadow), written when struct_size covers them (0 without a live world, also
       after a fault) */
    uint32_t water_attr;        /* shadow.Attr_top: the COL bits of the lowest water surface above the character (& 0x400002:
                                   the game counts the character as in water); 0 = no water found */
    float    water_level;       /* shadow.y_top: that surface's height in host space (the host coordinate along the host's
                                   image of SA1 +Y, e.g. Source z), meaningful when water_attr & 0x400002 */
    /* round 0.9.4a (SA1_API_FACE), written when struct_size covers them. Without a world, or in a world that never had a
       face call: face_mode 0, face_shown 0, face_from / face_to / face_letter -1, face_rate 0, face_activity 0 */
    uint32_t face_mode;         /* SA1_FACE_MODE_* | SA1_FACE_PACE_CUTSCENE as applied */
    uint32_t face_shown;        /* 1: after the pair the EV head is linked into the tree the display draws (it is drawn from the
                                   next pair); 0 while the General look is on or an emote draws another object (review R.5) */
    int32_t  face_from, face_to;/* the keys being blended: the event face block's while it runs, else the gameplay face work's
                                   while a gameplay face blends; -1 none */
    float    face_rate;         /* 0..1 between them */
    int32_t  face_letter;       /* the face animation being played 0..30 ('A'..'e'); -2 a host step (a hold, lip sync); -1 none */
    uint32_t face_activity;     /* SA1_FACE_ACT_* */
    /* round 0.9.4a (SA1_API_EMOTE), written when struct_size covers them (no world / no emote: 0 and -1) */
    uint32_t emote_state;       /* SA1_EMOTE_STATE_* */
    int32_t  emote_item;        /* the item playing (0..count-1; the trailing stand entry: count), -1 none */
    int32_t  emote_row;         /* its action row, -1 for an extra or none */
    int32_t  emote_extra;       /* its extra index (sa1_emote_info.index of an EXTRA entry), -1 for a row or none */
    float    emote_frame;       /* the queue head's frame (eventwk f3C) */
} sa1_state_ex;

/* The state after the last tick (a character without a world: its create / last pose position, yaw,
 * action and frame).  Outputs with struct_size are filled up to the smaller size. */
SA1_API sa1_status sa1_character_state(const sa1_context *ctx, sa1_character_id id, sa1_state *out);
SA1_API sa1_status sa1_character_state_ex(const sa1_context *ctx, sa1_character_id id, sa1_state_ex *out);

/* ------------------------------------------------------------------------- */
/* Collision input (stored for the game logic; host space)                    */
/* ------------------------------------------------------------------------- */

#define SA1_SURFACE_SOLID     0x01u
#define SA1_SURFACE_WATER     0x02u
#define SA1_SURFACE_NO_CLIMB  0x04u
#define SA1_SURFACE_HURT      0x08u
#define SA1_SURFACE_SLIPPERY  0x10u
/* API 0.2 draft (sadc docs/libsa1_integration.md 5.6): further surface kinds.  Each maps to
 * the game's COL attribute bit given in the comment when the collision adapter builds the
 * game's landtable (raw COL bits: SA1_COLGROUP_RAW_FLAGS).  The five above map to COL 0x1
 * (SOLID), 0x2 (WATER), 0x1000 (NO_CLIMB), 0x10000 (HURT) and 0x4 (SLIPPERY). */
#define SA1_SURFACE_HEAVY_WATER 0x0020u  /* COL 0x400000: weight x0.6, air resistance y x2 */
#define SA1_SURFACE_SAND        0x0040u  /* COL 0x10: weight 0.5 */
#define SA1_SURFACE_BOOST       0x0080u  /* COL 0x80: weight 1.5 */
#define SA1_SURFACE_SLIDE       0x0100u  /* COL 0x20000: sliding floor */
#define SA1_SURFACE_STAIRS      0x0200u  /* COL 0x4000: stair mode */
#define SA1_SURFACE_NO_LAND     0x0400u  /* COL 0x40: treated as a wall */
#define SA1_SURFACE_WALL_SLIDE  0x0800u  /* COL 0x200000: no landing, always slides, no climb */
#define SA1_SURFACE_DIG         0x1000u  /* COL 0x100: diggable */
/* API 0.2 draft (GRID, design 5.4-5.6).  KILL and DOUBLE_SIDED are not COL bits: */
#define SA1_SURFACE_KILL        0x2000u  /* kill plane (the game's death zones, DeathZoneTable): the
                                            character dies when it is below the triangle by at most
                                            30 units (it fell through it); only upward-facing kill
                                            triangles are kept.  Not land unless SOLID is set too */
#define SA1_SURFACE_DOUBLE_SIDED 0x4000u /* material 0x00800000 (SA1_MAT_DOUBLE_SIDE): the triangle
                                            collides from both sides (single-sided triangles push a
                                            sphere out to their front side) */
/* round 0.9.2 (SA1_API_092; sadc docs/research/gsa1_090_surfaces.md F1, section 2): three more COL bits */
#define SA1_SURFACE_SNOW        0x8000u  /* COL 0x8: Ice Cap's snow(board) slope: weight 0.15 (a seventh of the
                                            acceleration, normal stopping), the gravity factor of 8C074CF0, board
                                            spray and sounds; of SNOW / SLIPPERY / SAND / BOOST only the first in
                                            that order counts (8C0751C0) */
#define SA1_SURFACE_SPEED_PATH  0x10000u /* COL 0x800000: speed track (Speed Highway, Casinopolis, Final Egg):
                                            slope and high-speed turns in 8C06D254, no head-on stop (8C075416) */
#define SA1_SURFACE_FOOTPRINTS  0x20000u /* COL 0x100000: footprints (Emerald Coast's beach; read only by STG01's
                                            footprint task O_ASIATO, which host worlds do not create) */
/* round 0.9.3 collision (SA1_API_CEILING; sadc docs/research/gsa1_091b_ceiling.md FIX-C1 / C2 and gsa1_091a_loop.md
 * LOOP-L1, with their Reviews). Neither is a COL bit (no game code reads them); GRID input and moving objects: */
#define SA1_SURFACE_CURVED      0x40000u /* part of a curved surface a character runs along (a loop's inside, a pipe):
                                            GRID never leaves it out as an underside within reach (under_reach) and
                                            never makes it front-only: it stays an ordinary single-sided land triangle */
#define SA1_SURFACE_FRONT_ONLY  0x80000u /* front-only (material SA1_MAT_FRONT_ONLY): collides as a single-sided land
                                            triangle, except that a collision sphere whose centre starts the step behind
                                            its plane does not touch it (host worlds only: a PC-only test in the game's
                                            sphere test 8C029B00; the reference worlds keep the game's rule). Meant for
                                            the underside of a slab thinner than a sphere's reach (a thin roof, a plank):
                                            a ceiling from below, absent from above. Ignored with DOUBLE_SIDED or CURVED;
                                            left out with SA1_GRID_UNDERSIDES_DROP */
/* round 0.9.5 (SA1_API_BOARD; sadc docs/research/gsa1_095_plan.md 5.3 with its Review R.1): host-only kinds, never a COL bit
 * of the static segment (sa1ca_surface_to_col leaves them out of every COL word); GRID input and moving objects */
#define SA1_SURFACE_BOARD_SNOW  0x100000u /* COL 0x8 (Ice Cap's snow, weight 0.15) only in the world of a character whose
                                             player 0 rides a board, while he rides; nothing on foot, nothing in any other
                                             world */
#define SA1_SURFACE_BOARD_SAND  0x200000u /* ... COL 0x10 (Sand Hill's sand, weight 0.5) */
/* (review R.1) the game's land attribute is per COL entry, not per triangle: GRID keys its objects by (cell, COL word, the two
 * board bits, orientation) and a moving object its groups by (COL word, board bits), so no object mixes board snow / sand with
 * other land; libsa1 keeps each object's board bits in a side record of its own (no Ninja material bit, no COL bit), and the
 * world's window copy (and a moving object's DynamicCOL word, re-registered at the ride's two edges) ORs 0x8 / 0x10 into that
 * object's attribute while the character rides. A host counts (COL word, board bits) as one surface word against
 * SA1_MAX_OBJECT_GROUPS. Input without these bits gives the GRID of 0.2.0, byte for byte */
#define SA1_MAT_FRONT_ONLY      0x00000040u /* collision only: the material bit of a front-only land polygon (a user bit of
                                            the Ninja material word, 0x7F, that no land material of the game's stages
                                            sets: 9 stage landtables measured). GRID and moving-object models carry it for
                                            SA1_SURFACE_FRONT_ONLY; in sa1_collision_static.materials it marks a GRID
                                            triangle front-only */
/* A triangle with none of SOLID, WATER, HEAVY_WATER and KILL is SOLID (sa1_collision_set_static,
 * GRID groups without SA1_COLGROUP_RAW_FLAGS and moving objects): 0 and the modifier kinds alone
 * (HURT, SLIPPERY, STAIRS ...) are land.  WATER / HEAVY_WATER triangles are water surfaces (the
 * game finds the water level above the character): only their upward-facing triangles are kept,
 * so a closed water volume works. */

typedef struct sa1_collision_tri {
    float    v[3][3];           /* host space */
    uint32_t surface;           /* SA1_SURFACE_* */
} sa1_collision_tri;

#define SA1_MAX_COLLISION_TRIS 4000000u
#define SA1_MAX_COLLISION_OBJECTS 4096u
/* API 0.2 draft: limits of one moving object (sa1_collision_object_add) */
#define SA1_MAX_OBJECT_TRIS     4096u   /* triangles */
#define SA1_MAX_OBJECT_RADIUS   1000.0f /* SA1 units from the object's origin (the game sorts an object's
                                           polygons in 16-unit buckets over +-1024 units) */
#define SA1_MAX_OBJECT_GROUPS   4u      /* distinct collidable surface words per object */

/* Replaces the static world geometry (copied and converted to SA1 space; n == 0 clears).  The
 * triangles are GRID collision (mode SA1_COLLISION_MODE_GRID with the default parameters, see
 * sa1_collision_set_static_ex): worlds created afterwards collide with them. */
SA1_API sa1_status sa1_collision_set_static(sa1_context *ctx, const sa1_collision_tri *tris, uint32_t n);
/* Moving geometry: triangles in object space (host axes and units), placed by a host-space
 * position and a row-major 3x3 rotation (NULL = identity).
 * API 0.2: every world (the ticking characters of the context, GRID or no static collision) carries
 * each object as a game "DynamicCOL" entry (design 5.5): a moving land object that the game's own
 * land collision rebuilds every step and that carries a character standing on it (position and yaw).
 * A move takes effect at the next pair of steps, interpolated over its two steps.  Limits:
 * SA1_MAX_OBJECT_TRIS, SA1_MAX_OBJECT_RADIUS, SA1_MAX_OBJECT_GROUPS (SA1_ERR_LIMIT); 64 objects per
 * world (more are left out and reported with SA1_EVENT_CAPACITY).  Triangles with only KILL are
 * ignored. */
SA1_API sa1_status sa1_collision_object_add(sa1_context *ctx, const sa1_collision_tri *tris, uint32_t n,
                                            const float position[3], const float rotation[9],
                                            uint32_t *out_id);
SA1_API sa1_status sa1_collision_object_move(sa1_context *ctx, uint32_t id, const float position[3],
                                             const float rotation[9]);
SA1_API sa1_status sa1_collision_object_remove(sa1_context *ctx, uint32_t id);
/* Counts (tests/diagnostics).  Any pointer may be NULL.  static_tris counts the triangles of
 * sa1_collision_set_static or sa1_collision_set_static_ex, whichever was called last. */
SA1_API sa1_status sa1_collision_stats(const sa1_context *ctx, uint32_t *static_tris,
                                       uint32_t *objects, uint32_t *object_tris);

/* ------------------------------------------------------------------------- */
/* Grouped static collision (API 0.2 draft)                                  */
/* ------------------------------------------------------------------------- */
/*
 * sa1_collision_set_static_ex replaces the static world geometry like
 * sa1_collision_set_static, with the geometry grouped the way the game stores its
 * stages (sadc docs/libsa1_integration.md section 5): a stage is a list of COL
 * entries, each one game object (an NJS_OBJECT transform and one model of points and
 * triangles) with one 32-bit attribute word for all of its triangles.  The collision
 * adapter turns the groups into that landtable, in the game's own memory layout, and
 * the game's unchanged collision code runs on it.
 *
 * Modes (sa1_collision_static.mode):
 *   GRID      production: the triangles are re-partitioned into cells (design 5.4).
 *             Host geometry in any grouping (one group per surface kind is enough): the
 *             groups are transformed to world space, triangles longer than max_edge are
 *             cut into well-shaped pieces (consecutive triangles of one planar convex face,
 *             a quad or a fan, are joined back into the face first, so send each face's
 *             triangles together), each piece goes to the cell (cube of cell_size) of its centroid
 *             (SA1_GRID_CUT: cut at the cell boundaries instead) and every (cell, attribute
 *             word, orientation class - floor / wall / ceiling, round 0.9.2; without the class
 *             with SA1_GRID_MIXED_ORIENTATIONS) becomes one game object in the static collision
 *             segment (synthesized once per geometry generation, shared by the context's worlds).
 *             Each world keeps a window landtable of at most 128 of these objects, the
 *             nearest to its character first, refreshed before every land collision update
 *             (within the game's limits: 191 land objects, 8192 polygons, 16 objects in
 *             the character's land-collision query); kill planes become the world's death zones.  The
 *             game's collision code runs unchanged on them; results match the original
 *             geometry up to the partition: the game resolves the touches of each object
 *             together (8C029B00 / 8C028100), so which triangles share an object matters, and
 *             a split or cut triangle changes the pushback (the game turns each triangle into a
 *             frame by angles quantised to 1/65536 turn).  On Emerald Coast act 1 (harness
 *             landtable dump, libsa1 tests/grid_test.c) GRID with SA1_GRID_MIXED_ORIENTATIONS
 *             keeps the reference's mode and action sequence in all 5 scripts; two are
 *             bit-identical step by step (also with the default orientation classes, which
 *             keep 4 of the 5 sequences: ec1_turns turns differently from step 156).
 *   FAITHFUL  one game object per group, in group order, with the group's own transform
 *             and bounds (design 5.3).  Meant for exactness tests against the original
 *             game (e.g. a landtable dump of the harness): with the original transforms,
 *             bounds, model-space points, triangle order, material words, COL attributes
 *             and SA1_COLTRI_STRIP_ODD marks, the game's collision polygon cache comes out
 *             bit-identical to the one built from the stage data (verified on Emerald
 *             Coast act 1, libsa1 tests/coladapt_test.c).
 *   DISC      the stage's own landtable from the user's disc (test oracle): no geometry
 *             is passed, only disc_level / disc_act; the world holds the stage overlay.
 *
 * Spaces: points are in the OBJECT space of their group, i.e. the game computes
 *   world = T(position) * Rz(angle[2]) * Ry(angle[1]) * Rx(angle[0]) * S(scale) * point
 * (Ninja order, column vectors).  Without SA1_COLLISION_SA1_SPACE, points, position and
 * the centres/radii are host space and units (converted with the current sa1_coords);
 * angle and scale are always about/along the SA1 axes (hosts that only have a host-space
 * rotation bake it into the points and leave angle 0, scale 1).  With
 * SA1_COLLISION_SA1_SPACE every float is used bit for bit (exactness tests).
 */
#define SA1_API_COLLISION_EX 1

#define SA1_COLLISION_MODE_GRID     0u
#define SA1_COLLISION_MODE_FAITHFUL 1u
#define SA1_COLLISION_MODE_DISC     2u

/* sa1_collision_static.flags */
#define SA1_COLLISION_SA1_SPACE     0x1u    /* every float is SA1 space, copied bit for bit */

/* GRID parameters (API 0.2 draft; sa1_collision_static.grid, NULL = all defaults) */
#define SA1_GRID_CUT                0x1u    /* cut the triangles at the cell boundaries (tight cells; the
                                               pieces change the game's pushback slightly, see GRID above)
                                               instead of giving each to the cell of its centroid */
#define SA1_GRID_DC_LIMITS          0x2u    /* keep the game's own NEAR-poly list (96 polygons per query) in the
                                               worlds of this geometry; by default GRID worlds (and worlds without
                                               static collision) raise it to 512 (256 before round 0.9.2; a PC-only
                                               capacity, design 5.7:
                                               long or layered host geometry fills 96, and the floor under the
                                               character is then never tested).  For comparisons with the game */
#define SA1_GRID_KEEP_UNDERSIDES    0x4u    /* keep the undersides within reach (see under_reach): every polygon of
                                               the input stays land, as in the game's own stages */
#define SA1_GRID_MIXED_ORIENTATIONS 0x8u    /* (round 0.9.2) one object per (cell, attribute word) as before: floors,
                                               walls and ceilings of a cell share objects. By default they are made
                                               into objects of their own (by the triangle's normal in SA1 space:
                                               floor y >= 0.5, wall |y| < 0.5, ceiling y <= -0.5), because the game
                                               averages the push-backs of all touched polygons of one object
                                               (8C029B00): a wall sharing an object with the floor at its foot let a
                                               character creep into it and through, a thin pair cancelled the floor's
                                               lift (sadc docs/research/gsa1_090_collision.md C1 / F2). For
                                               comparisons with the game's own stages (Emerald Coast's landtable in
                                               tests/grid_test.c "ec1": its ec1_turns script keeps the reference's
                                               modes only with this flag) */
#define SA1_GRID_UNDERSIDES_DROP    0x10u   /* (round 0.9.3, SA1_API_CEILING) the undersides within reach are all left
                                               out, as before round 0.9.3 (one-way platforms: a character jumps up
                                               through them), and so is every SA1_SURFACE_FRONT_ONLY / SA1_MAT_FRONT_ONLY
                                               triangle of the input. By default the near-horizontal ones (normal y <=
                                               -0.97 in SA1 space, within 14 degrees of level) become front-only
                                               instead: solid ceilings */
typedef struct sa1_collision_grid {
    uint32_t struct_size;       /* sizeof(sa1_collision_grid) */
    uint32_t flags;             /* SA1_GRID_* */
    float    cell_size;         /* SA1 units, 0 = 256 (16 .. 2048) */
    float    max_edge;          /* SA1 units, 0 = 128 (8 .. 220): longer triangles are split (the game's
                                   probes assume triangles shorter than 220) */
    uint32_t window_objects;    /* static objects in a world's window, 0 = 128 (1 .. 128) */
    uint32_t window_polygons;   /* polygon budget of a world's land collision cache (window plus moving
                                   objects), 0 = 7680 (256 .. 8000; the game's pool holds 8192) */
    /* API 0.2 draft (2026-10-21); a smaller struct_size that ends before it is accepted (the default) */
    float    under_reach;       /* SA1 units, 0 = 12 (0.5 .. 64): undersides within reach are left out. A single-
                                   sided land triangle facing down (normal y < -0.1) that lies, at every point, at
                                   most this far behind a land triangle facing up (normal y >= 0.3, measured along
                                   that triangle's normal) is not land: the game's sphere test touches a single-
                                   sided polygon from behind too and prefers such a touch, so the underside of a
                                   slab thinner than a sphere's reach (planks, thin platforms, a slope's buried end
                                   cap) pushed characters out below the slab (sadc docs/research/
                                   gsa1_ramp_freeze.md, mechanism B). Such slabs became one-way platforms (jump up
                                   through them from below). Round 0.9.3 (SA1_API_CEILING): such an underside that is
                                   near-horizontal (normal y <= -0.97, within 14 degrees of level) stays as a
                                   FRONT_ONLY triangle (a ceiling from below, absent from above: sadc docs/research/
                                   gsa1_091b_ceiling.md FIX-C2), steeper ones are left out, SA1_SURFACE_CURVED ones
                                   are kept as they are (gsa1_091a_loop.md LOOP-L1); SA1_GRID_UNDERSIDES_DROP leaves
                                   them all out. SA1_GRID_KEEP_UNDERSIDES keeps them */
} sa1_collision_grid;

/* sa1_collision_group.flags */
#define SA1_COLGROUP_RAW_FLAGS      0x1u    /* `surface` is the raw COL attribute word (else SA1_SURFACE_*) */
#define SA1_COLGROUP_EVALFLAGS      0x2u    /* `evalflags` is given (else derived from the transform:
                                               0x1 position 0, 0x2 angles 0, 0x4 scale 1) */
#define SA1_COLGROUP_MODEL_BOUNDS   0x4u    /* model_center / model_radius are given (else the
                                               bounding sphere of the group's points) */
#define SA1_COLGROUP_COL_SPHERE     0x8u    /* col_center / col_radius are given (else the model
                                               sphere placed by the transform) */

/* per-triangle flags (sa1_collision_static.tri_flags) */
#define SA1_COLTRI_STRIP_ODD        0x1u    /* exactness only: the triangle sat at an odd (winding-
                                               flipped) position of a triangle strip in the original
                                               model, so the game sums its corners in the order 0, 2, 1
                                               (its centroid and radius round accordingly).  The indices
                                               stay in winding order. */

typedef struct sa1_collision_group {
    uint32_t struct_size;       /* sizeof(sa1_collision_group) of the caller (array stride) */
    uint32_t flags;             /* SA1_COLGROUP_* */
    uint32_t surface;           /* SA1_SURFACE_* of every triangle, or the raw COL attribute
                                   (SA1_COLGROUP_RAW_FLAGS: 0x1 solid, 0x2 water, ... 0x80000000
                                   drawn; only groups with a bit of 0x400003 are collidable) */
    uint32_t evalflags;         /* NJS_OBJECT evalflags (SA1_COLGROUP_EVALFLAGS) */
    float    position[3];       /* object transform */
    int32_t  angle[3];          /* BAMS (0x10000 = one turn), SA1 axes, applied Z, then Y, then X */
    float    scale[3];          /* SA1 axes; 0 0 0 is read as 1 1 1 */
    float    model_center[3];   /* bounding sphere of the model, object space (MODEL_BOUNDS) */
    float    model_radius;
    float    col_center[3];     /* the COL entry's sphere, world space (COL_SPHERE): the game's */
    float    col_radius;        /* per-step gather tests it against the player position */
    float    col_ywidth, col_zwidth;    /* raw COL fields (copied; 0 for host geometry) */
    uint32_t col_blockbit;
    uint32_t first_point, num_points;   /* range of sa1_collision_static.points */
    uint32_t first_tri, num_tris;       /* range of sa1_collision_static.indices (3 per triangle) */
} sa1_collision_group;

typedef struct sa1_collision_static {
    uint32_t struct_size;       /* sizeof(sa1_collision_static) */
    uint32_t mode;              /* SA1_COLLISION_MODE_* */
    uint32_t flags;             /* SA1_COLLISION_* */
    const float    *points;     /* 3 floats per point, object space of its group */
    uint32_t        num_points;
    const uint32_t *indices;    /* 3 per triangle, relative to the group's first_point, in winding
                                   order (counter-clockwise seen from the front in a right-handed
                                   frame, as sa1_mesh emits them) */
    const uint32_t *materials;  /* per triangle (may be NULL = 0): the Ninja material attribute
                                   word; SA1_MAT_DOUBLE_SIDE makes the triangle collide from both
                                   sides, the other bits are passed through */
    const uint8_t  *tri_flags;  /* per triangle (may be NULL = 0): SA1_COLTRI_* */
    uint32_t        num_tris;
    const sa1_collision_group *groups;
    uint32_t        num_groups; /* at most SA1_MAX_COLLISION_GROUPS; 0 clears (GRID / FAITHFUL) */
    int32_t         disc_level; /* MODE_DISC: stage (1 = Emerald Coast ... 10; API 0.4: 19 = Perfect Chaos, B_CHAOS7,
                                   for Super Sonic's worlds) and act (0-based) */
    int32_t         disc_act;
    /* API 0.2 draft (GRID); a smaller struct_size that ends before it is accepted (defaults) */
    const sa1_collision_grid *grid;     /* MODE_GRID parameters (NULL: defaults) */
} sa1_collision_static;

#define SA1_MAX_COLLISION_GROUPS 32767u     /* the landtable's entry count is a signed 16-bit word */

/* SA1_EVENT_CAPACITY ids of the collision adapter (API 0.2 draft; arg = objects left out in the step where the
 * situation began; reported once per episode).  The window takes the nearest objects first; only objects left out
 * within 200 units of the character are reported (the game gathers up to 400). */
#define SA1_CAPACITY_WINDOW_OBJECTS   1u    /* more GRID objects in range than the window holds (128, 191 with the
                                               moving objects in range) */
#define SA1_CAPACITY_WINDOW_POLYGONS  2u    /* the polygon budget of the land collision cache; since round 0.9.3 only
                                               when an object within 40 units of the character was left out even with
                                               the whole pool (arg = those objects): farther objects left out for the
                                               budget are counted, not reported */
#define SA1_CAPACITY_NEAR_OBJECTS     3u    /* (until round 0.9.2) more than 14 land objects within 40 units (box
                                               distance) of the character; replaced by SA1_CAPACITY_LAND_QUERY, no
                                               longer reported */
#define SA1_CAPACITY_MOVING_OBJECTS   4u    /* more moving objects than a world holds (64), or the game's
                                               DynamicCOL list (256) / the world's extension segment is full */
#define SA1_CAPACITY_NEAR_POLYGONS    5u    /* a NEAR-poly query of the land collision (8C028BFE) filled its list
                                               (512 polygons since round 0.9.2, 256 before; 96 with
                                               SA1_GRID_DC_LIMITS): polygons beyond it near the character were not
                                               tested (arg = the queries that filled it; position = the character:
                                               geometry there is too dense or layered) */
#define SA1_CAPACITY_MATRIX_STACK     6u    /* a step began with the game's matrix stack deeper than after boot
                                               (8C029B00 leaves one push behind when 16 polygons of one land object
                                               are touched, as on the DC): restored (arg = slots restored) */
/* round 0.9.2 (SA1_API_092; sadc docs/research/gsa1_090_collision.md C3 / F1 with its Review R.4.1). 8C073160 /
 * 8C0777E0 (the character's land collision) copy every land object of the character's query into 16-entry arrays,
 * touched or not; the query takes every object whose COL sphere reaches the query sphere (8C0522C0: centre the
 * character's position + its collision radius r along its up vector, radius r). */
#define SA1_CAPACITY_LAND_QUERY       8u    /* the window left land objects out so that the character's query takes at
                                               most 15 (minus the moving ones in range): objects whose COL sphere comes
                                               within 2 r + 20 of the character, nearest box first (arg = objects left
                                               out). E-102 counts water objects too (8C073160 takes them for him only).
                                               Since round 0.9.3 the bound is the window's own 191 when the lowered game
                                               code holds 192 per call (every host world of this library): not expected
                                               then; 15 only with SA1_GRID_DC_LIMITS */
#define SA1_CAPACITY_LAND_HITS        9u    /* a land-collision call of the lowered game code gathered more objects than
                                               its PC-only arrays hold (192; the window hands out at most 191): the rest
                                               were left out of that call (arg = objects left out; not expected) */
#define SA1_MAX_GROUP_POINTS     32767u     /* point indices are signed 16-bit words in the game */

/* Replaces the static world geometry (copied; the caller's arrays may be freed afterwards).
 * Worlds pick it up when they (re)build their collision (world creation; teleports later).
 * sa1_collision_set_static and this function replace each other. */
SA1_API sa1_status sa1_collision_set_static_ex(sa1_context *ctx, const sa1_collision_static *desc);

typedef struct sa1_collision_info {
    uint32_t struct_size;
    uint32_t mode;              /* SA1_COLLISION_MODE_* of the current static geometry */
    uint32_t generation;        /* changes with every set_static / set_static_ex */
    uint32_t groups, points, triangles;
    uint32_t collidable_groups;     /* groups whose attribute has a bit of 0x400003 */
    uint32_t collidable_triangles;
    uint32_t odd_triangles;         /* SA1_COLTRI_STRIP_ODD */
    int32_t  disc_level, disc_act;
    /* last synthesis of the game landtable (0 until a world built it) */
    uint32_t landtable;             /* guest address of the _OBJ_LANDTABLE */
    uint32_t bytes;                 /* bytes written into the static collision segment */
    uint32_t meshsets, materials;
    uint32_t builds;                /* syntheses of the current geometry so far */
    /* API 0.2 draft: GRID synthesis (0 until a world built it) and moving objects */
    uint32_t grid_objects;          /* land objects (cell x attribute word) */
    uint32_t grid_kill_objects;     /* kill plane objects (death zones) */
    uint32_t grid_triangles;        /* land + kill triangles after splitting and cutting */
    uint32_t grid_split;            /* input triangles split for max_edge */
    uint32_t grid_cut;              /* triangles cut at cell boundaries */
    uint32_t grid_dropped;          /* degenerate triangles, water / kill triangles not facing up */
    float    grid_cell_size;
    uint32_t moving_objects;        /* sa1_collision_object_add objects */
    /* API 0.2 draft (2026-10-21) */
    uint32_t grid_undersides;       /* downward triangles within reach of an upward one left out (under_reach) */
    uint32_t prepared;              /* 1: the static geometry is a sa1_collision_prepared (sa1_collision_set_prepared) */
    /* round 0.9.3 (SA1_API_CEILING): written when struct_size covers them */
    uint32_t grid_front_only;       /* GRID triangles that are front-only (the input's FRONT_ONLY ones and the undersides
                                       within reach made front-only) */
    uint32_t grid_curved;           /* CURVED GRID triangles that would have been left out as undersides within reach */
} sa1_collision_info;

/* Describes the current static geometry (sa1_collision_set_static: mode GRID, one group). */
SA1_API sa1_status sa1_collision_info_get(const sa1_context *ctx, sa1_collision_info *out);

/* ------------------------------------------------------------------------- */
/* Preparation on host threads (API 0.2 draft, 2026-10-21)                   */
/* ------------------------------------------------------------------------- */
/*
 * What a host can do on its own worker threads so that creating a ticking character costs only its world's set-up on the
 * calling thread (about 5 ms: the 16 MB arena from the kind's image, the boot; G-SA1 request 11):
 *
 *   sa1_collision_prepare / _prepare_tris  the GRID synthesis (0.1-0.4 s for a big map: gm_freespace_13's 51,000
 *                    triangles become 344,000 in 12,700 objects) or the FAITHFUL landtable, into a
 *                    sa1_collision_prepared: immutable, reference-counted, with a collision segment of its own.
 *                    sa1_collision_set_prepared makes it the context's static geometry in O(1) (where
 *                    sa1_collision_set_static[_ex] leave the synthesis to the first world created afterwards, on the
 *                    thread that creates it). Every world created while it is the context's geometry maps that segment
 *                    by reference (first creations, respawns, several characters: nothing is copied) and keeps it alive
 *                    until the world is destroyed, so the host may release its own reference at any time.
 *   sa1_character_prepare   the world images of character kinds: 1ST_READ.BIN and the kind's overlays read from the
 *                    disc, SHA-1 checked and PRS-unpacked (30-60 ms per kind), which the first world of a kind otherwise
 *                    composes on the thread that creates it. SA1_LOAD_WORLD_IMAGES does it at the end of sa1_load_disc.
 *
 * Threads: sa1_collision_prepare[_tris], sa1_collision_prepared_retain / _release / _info_get and sa1_character_prepare
 * may run on any thread, at the same time as each other and as any other call on the same context (ticks, creations,
 * renders, sa1_collision_set_*), except sa1_set_coords, sa1_load_disc / sa1_unload_disc and sa1_shutdown of that context.
 * They call only the context's allocator and file callbacks (which must then be thread-safe; the defaults are), never its
 * log, and never touch sa1_last_error: a failed preparation keeps its message in the prepared object, and
 * sa1_character_prepare returns the status of the first kind that failed. Nothing here enters a world (no game code
 * runs), so a preparation never waits for a tick.
 */
#define SA1_API_PREPARE 1

typedef struct sa1_collision_prepared sa1_collision_prepared;

/* Synthesizes `desc` (as sa1_collision_set_static_ex takes it; mode GRID or FAITHFUL, DISC needs no preparation:
 * SA1_ERR_UNSUPPORTED) with ctx's coordinate system and allocator, on the calling thread. *out receives the prepared
 * geometry with one reference (the caller's) - also when the preparation failed, then holding the failure
 * (sa1_collision_prepared_info_get: status, message), unless no object could be allocated (*out NULL). Release it in
 * either case. The description's arrays may be freed when the call returns. */
SA1_API sa1_status sa1_collision_prepare(const sa1_context *ctx, const sa1_collision_static *desc,
                                         sa1_collision_prepared **out);
/* The same for a flat triangle list (what sa1_collision_set_static takes; host space): GRID with `grid`'s parameters
 * (NULL: the defaults). */
SA1_API sa1_status sa1_collision_prepare_tris(const sa1_context *ctx, const sa1_collision_tri *tris, uint32_t n,
                                              const sa1_collision_grid *grid, sa1_collision_prepared **out);
/* Reference counting (any thread; NULL is ignored). The last release frees the geometry. */
SA1_API void sa1_collision_prepared_retain(sa1_collision_prepared *p);
SA1_API void sa1_collision_prepared_release(sa1_collision_prepared *p);

typedef struct sa1_collision_prepared_info {
    uint32_t struct_size;           /* sizeof(sa1_collision_prepared_info) */
    sa1_status status;              /* SA1_OK, or why the preparation failed */
    char     message[256];          /* the failure ("" when it worked) */
    uint32_t mode;                  /* SA1_COLLISION_MODE_GRID / FAITHFUL */
    uint32_t triangles;             /* triangles of the description */
    uint32_t grid_objects;          /* GRID: land objects; FAITHFUL: the landtable's objects */
    uint32_t grid_kill_objects;
    uint32_t grid_triangles;        /* triangles after splitting and cutting */
    uint32_t grid_split, grid_cut, grid_dropped, grid_undersides;  /* as sa1_collision_info */
    uint32_t bytes;                 /* of its collision segment in use */
    float    grid_cell_size;
    float    ms;                    /* how long the preparation took (on its thread) */
    uint32_t refs;                  /* references held now: the host's, the contexts' and every world's */
    /* round 0.9.3 (SA1_API_CEILING): written when struct_size covers them */
    uint32_t grid_front_only, grid_curved;  /* as sa1_collision_info */
} sa1_collision_prepared_info;

SA1_API sa1_status sa1_collision_prepared_info_get(const sa1_collision_prepared *p, sa1_collision_prepared_info *out);

/* Makes p the context's static geometry (replacing sa1_collision_set_static[_ex]'s, as they replace each other; the
 * context takes a reference, released when the geometry is replaced or the context shuts down). O(1): nothing is
 * synthesized or copied. Worlds created afterwards collide with it; existing worlds keep the geometry they were created
 * with. p NULL clears the static geometry. A failed preparation is refused (SA1_ERR_INVALID_ARGUMENT). p may come from
 * another context of the same process (the same coordinate system is the host's business). */
SA1_API sa1_status sa1_collision_set_prepared(sa1_context *ctx, sa1_collision_prepared *p);

/* Composes the world images of the character kinds in `kinds` (bit mask of 1u << SA1_CHAR_*; 0 = every kind that can
 * tick) now, on the calling thread, from the disc of the last sa1_load_disc; later world creations of these kinds only
 * copy the image. Kinds already composed are skipped (cheap). SA1_ERR_NOT_LOADED without a disc,
 * SA1_ERR_NOT_IMPLEMENTED in builds without the world core or the game code. */
SA1_API sa1_status sa1_character_prepare(sa1_context *ctx, uint32_t kinds);

/* ------------------------------------------------------------------------- */
/* Sound events                                                              */
/* ------------------------------------------------------------------------- */

typedef struct sa1_sound_event {
    sa1_character_id character;
    uint32_t         sound_id;      /* game sound-effect id */
    uint32_t         bank;          /* 3 = player SFX, 6 = voice, 0/2 = common */
    float            position[3];   /* host space */
    float            volume;        /* 0..1 */
    uint32_t         flags;
} sa1_sound_event;

/* Pops up to `capacity` queued events.  Nothing produces events before the game
 * logic exists, so 0.1 always reports 0. */
SA1_API sa1_status sa1_poll_sound_events(sa1_context *ctx, sa1_sound_event *out, uint32_t capacity,
                                         uint32_t *count);

/* ------------------------------------------------------------------------- */
/* World core additions (API 0.2 draft)                                      */
/* ------------------------------------------------------------------------- */
/*
 * Additions for the world core that runs the decompiled game code (GUEST-ARENA design,
 * sadc docs/libsa1_integration.md 6.9 and 7.4).  SA1_API_WORLD_DRAFT tells a host at compile
 * time that they exist (API 0.2: sa1_poll_events and a ticking sa1_character_tick).
 */
#define SA1_API_WORLD_DRAFT 1

/* The game code of a character's world faulted (bad pointer, missing function, fatal
 * state).  The character is dead until it is recreated; sa1_last_error() names the
 * guest address, segment and DC function. */
#define SA1_ERR_GAME_FAULT         -14

/* Event types (sa1_event.type) */
#define SA1_EVENT_SOUND            1    /* play a sound effect: id, bank, position */
#define SA1_EVENT_SOUND_STOP       2    /* stop a sound effect: id (0 = all of the task) */
#define SA1_EVENT_VOICE            3    /* character voice: id */
#define SA1_EVENT_JINGLE           4    /* music jingle (invincibility, speed up, ...): id */
#define SA1_EVENT_RUMBLE           5    /* controller rumble: id = pattern */
#define SA1_EVENT_DEATH            6    /* the character died (arg: cause) */
#define SA1_EVENT_RESTART          7    /* the game asked to restart the act */
#define SA1_EVENT_RESPAWN          8    /* the game asked for the start position */
#define SA1_EVENT_MISSING_ACTION   9    /* an action whose data is not loaded was selected: id = action */
#define SA1_EVENT_UNIMPLEMENTED   10    /* game code reached a function libsa1 does not have: id = DC address */
#define SA1_EVENT_CAPACITY        11    /* a DC capacity limit was reached: id = which, arg = count */
#define SA1_EVENT_TIME_UP         12    /* E-102's timer ran out (worlds built through the character API: pushed in
                                           the step of E-102's time-up death 8C0D23E4, right before its
                                           SA1_EVENT_DEATH; libsa1 worlds hold the game clock stopped unless a host
                                           starts it) */
#define SA1_EVENT_FAULT           13    /* the world faulted: id = guest address, arg = DC function */

typedef struct sa1_event {
    sa1_character_id character;
    uint32_t         type;          /* SA1_EVENT_* */
    uint32_t         id;            /* per type: sound id, action, DC address, ... */
    uint32_t         bank;          /* sounds: 3 = player SFX, 6 = voice, 0/2 = common */
    float            position[3];   /* host space (the world core records SA1 space; the API
                                       converts when it hands events out) */
    uint32_t         arg;           /* per type */
    uint32_t         step;          /* logic step of the character's world when it happened */
} sa1_event;

/* Pops up to `capacity` queued events of all characters (oldest first per character; positions in
 * host space).  Queues hold 256 events per character; older events are dropped when a host does not
 * poll. */
SA1_API sa1_status sa1_poll_events(sa1_context *ctx, sa1_event *out, uint32_t capacity, uint32_t *count);

/* ------------------------------------------------------------------------- */
/* Diagnostics: the NEAR-poly list (API 0.2 draft, 2026-10-21)               */
/* ------------------------------------------------------------------------- */
/*
 * The game's land collision first collects the polygons near the character's collision sphere (8C028BFE, the NEAR-poly
 * list: every polygon of the gathered land objects whose bounding sphere passes |c - p|^2 <= 1.3 (r^2 + R^2)), then
 * tests the sphere against them (8C029B00). A list that fills leaves the rest untested (SA1_CAPACITY_NEAR_POLYGONS).
 * Host worlds (GRID or no static collision) collect into a raised list of 512 (256 before round 0.9.2) in the world's
 * pcdata (a block found by its magic words "NCAP" "8BFE": {u32 magic[2]; s32 cap, full, peak; u32 pool[512]}, guest
 * addresses), the reference worlds
 * (DISC, FAITHFUL, SA1_GRID_DC_LIMITS) into the game's own B_8C73E280[96]. Every query starts the list again, so after a
 * tick it holds the last query of the last step (the character's land collision is the last one in most steps).
 *
 * sa1_debug_near_polys copies that list (outside ticks; a step hook inside a world may call it too): `info` gets the guest
 * addresses (for tools that read the world's memory themselves: sa1_character_world + the world core's readers), the
 * capacity and the counters; `out` (may be NULL) gets up to `capacity` entries, host space, in the list's order, and
 * *count (may be NULL) how many were written. SA1_ERR_NOT_LOADED when the character has no world yet.
 */
typedef struct sa1_near_poly {
    float    v[3][3];               /* the corners (the game's world-space copy, winding order), host space */
    float    center[3];             /* its bounding sphere (the list's sort key is the SA1 z of the centre) */
    float    radius;                /* host units */
    uint32_t attr;                  /* the polygon's attribute (0x800000: collides from both sides) */
    uint32_t object;                /* guest address of the land object's NJS_OBJECT (0: not found) */
    uint32_t guest;                 /* guest address of the polygon */
} sa1_near_poly;

typedef struct sa1_near_info {
    uint32_t struct_size;           /* sizeof(sa1_near_info) */
    uint32_t count;                 /* entries of the last query (B_8C73E27C) */
    uint32_t capacity;              /* 512 (the raised list; 256 before round 0.9.2) or 96 (the game's list) */
    uint32_t list;                  /* guest address of the list the last query filled (guest words, one per entry) */
    uint32_t count_address;         /* guest address of the count: 0x8C73E27C */
    uint32_t block;                 /* guest address of the raised list's pcdata block (0: the lowered code has none) */
    uint32_t full;                  /* queries that filled the list over the world's life (counted by the lowered 8C028BFE
                                       of libsa1 builds in every world, 0 without the block) */
    uint32_t peak;                  /* the fullest list of the last step (host worlds: as the collision adapter took it;
                                       other worlds: since the world began) */
    uint32_t peak_life;             /* the fullest list of the world's life (host worlds) */
} sa1_near_info;

SA1_API sa1_status sa1_debug_near_polys(sa1_context *ctx, sa1_character_id id, sa1_near_info *info, sa1_near_poly *out,
                                        uint32_t capacity, uint32_t *count);

/* ---- host objects (API 0.3) ------------------------------------------------------------------------------------- */
/*
 * SA1-style interaction with the host's world (sadc docs/libsa1_interaction.md 2.1-2.8 and its addendum, section 6).
 * The host registers proxy objects (enemies, targets, breakables, hazards) with shapes in host space; libsa1 mirrors
 * them into every ticking character's world as real game tasks built like the game's own enemies: a level-3 task whose
 * exec is libsa1's (the synthetic code address 0x7F000100), a CCL collision work (id 3 or 4, the game's CCL_INFO bits),
 * the 0x23C enemy work as tp->mwp and a SET-condition stand-in as tp->ocp. The decompiled player code then reacts to
 * them natively: the jump / roll / homing attack, the Light Speed Attack, Knuckles' punches and Maximum Heat, the enemy
 * bounce, E-102's lock-on (reported as events), touch damage with the game's own knock-back, ring loss and
 * invulnerability. Each world reports what happened in it as events (sa1_poll_hostobj_events).
 *
 * Objects are context-wide (like sa1_collision_object_*): up to 1024 per context; every ticking world mirrors the
 * nearest ones (a window of at most 64 proxies, entered within enter_radius of its character and kept until
 * leave_radius, nearest first by (squared distance, id)). Calls take effect at the next pair of a world: a move is
 * interpolated over the pair's two steps (half at sub step 0, all at sub step 1), as moving collision objects are.
 * Registration and moves are inputs latched per pair inside each world (its floating-point mode), so a world's run is
 * deterministic for the same sequence of calls. Without registered objects a world runs exactly as without this layer.
 *
 * What a hit does in the world is the game's: the proxy's exec runs the game's enemy damage check 8C0489E4 (squash,
 * "killed by a player", score +10, the bounce 8C048980 of a balled, airborne Sonic, Tails or Knuckles), then enters a
 * cooldown (cooldown_steps, default 20: defence 3 on every shape, out of the homing / throw target lists and of
 * E-102's lock-on) after which the object is a target again unless the host removed it (an SA1 boss's behaviour).
 * Whether the host object really dies is the host's decision (sa1_hostobj_remove, SA1_HOSTOBJ_REMOVE_KILLED).
 */
#define SA1_API_HOSTOBJ 1

#define SA1_HOSTOBJ_MAX_OBJECTS 1024u   /* per context (SA1_ERR_LIMIT beyond) */
#define SA1_HOSTOBJ_MAX_WINDOW  64u     /* proxies per world */

/* kinds (sa1_hostobj_desc.kind) */
#define SA1_HOSTOBJ_ENEMY      1u   /* CCL id 3: damageable, hurts on touch (touch 1), homing / lock-on target, solid */
#define SA1_HOSTOBJ_TARGET     2u   /* CCL id 3: damageable, never hurts, homing / lock-on target (no E-102 time bonus) */
#define SA1_HOSTOBJ_BREAKABLE  3u   /* CCL id 4: damageable, never hurts, not a target */
#define SA1_HOSTOBJ_HAZARD     4u   /* CCL id 4: not damageable, hurts on touch (touch 2) */
/* SA1_HOSTOBJ_HOLDABLE 5 and SA1_HOSTOBJ_BALLOON 6: below (round 5) */

/* sa1_hostobj_desc.flags */
#define SA1_HOSTOBJ_SOLID        0x01u  /* pushes the character out (CCL_INFO b05 0x01); default for ENEMY */
#define SA1_HOSTOBJ_NOT_SOLID    0x02u  /* overrides the kind's default */
#define SA1_HOSTOBJ_NO_HOMING    0x04u  /* never in the player target lists (homing, LSA, throws, Maximum Heat, Big) */
#define SA1_HOSTOBJ_NO_LOCKON    0x08u  /* E-102 never locks it (enemy work +0x4E 0x1000, shape 0 attr 0x100000) */
#define SA1_HOSTOBJ_NO_BONUS     0x10u  /* E-102 locks it without counting it (attr 0x100000); default for TARGET */
#define SA1_HOSTOBJ_DISABLED     0x20u  /* in the world but every shape off (no hits, no targeting) */
#define SA1_HOSTOBJ_REPORT_TOUCH 0x40u  /* SA1_HOSTOBJ_EVENT_TOUCH when the character starts touching it */

/* shapes: at most 4 per object; shape 0 is the aim point (homing, E-102). Spheres, vertical cylinders and boxes turned
 * about the vertical only: the game compiles the capsule pairs out and the player's sphere has no other partner. */
#define SA1_HOSTOBJ_MAX_SHAPES 4u
#define SA1_SHAPE_SPHERE   0u   /* size[0] radius */
#define SA1_SHAPE_CYLINDER 1u   /* size[0] radius, size[1] half height (along the host up axis = SA1 +Y) */
#define SA1_SHAPE_BOX      4u   /* size[] half extents (host axes before yaw), turned about the up axis by the shape's
                                   yaw plus the object's (interpolated over the pair like the position) */

/* sa1_hostobj_shape.flags */
#define SA1_HSHAPE_NO_DAMAGE 0x1u   /* this shape cannot be out-levelled (no attr 0x80) */
#define SA1_HSHAPE_NO_TOUCH  0x2u   /* this shape does not hurt (attack level 0) */
#define SA1_HSHAPE_OFF       0x4u   /* attr 0x10: the shape is off */

typedef struct sa1_hostobj_shape {
    uint32_t form;              /* SA1_SHAPE_* */
    float    center[3];         /* offset from the object's position, host axes and units (not turned by yaw) */
    float    size[3];           /* host units, > 0, at most 1000 SA1 units (a sphere's CCL b is set to its radius, as in
                                   the players' tables) */
    float    yaw;               /* BOX: radians, the sa1_pose.yaw convention */
    uint32_t flags;             /* SA1_HSHAPE_* */
} sa1_hostobj_shape;

typedef struct sa1_hostobj_desc {
    uint32_t struct_size;       /* sizeof(sa1_hostobj_desc) */
    uint32_t kind;              /* SA1_HOSTOBJ_ENEMY .. SA1_HOSTOBJ_HAZARD */
    uint32_t flags;             /* SA1_HOSTOBJ_* flags */
    float    position[3];       /* host space */
    float    yaw;               /* radians, the sa1_pose.yaw convention (|yaw| <= 10000) */
    int32_t  armor;             /* 0..3 = the defence level of every shape (0: any attack; 1: needs level 2; 2: level 3;
                                   3: invulnerable), -1 = the kind's default (0) */
    int32_t  touch;             /* 0..3 = the attack level against the character, -1 = the kind's default (ENEMY 1,
                                   HAZARD 2, others 0) */
    uint32_t cooldown_steps;    /* steps the object is invulnerable after a hit (0 = 20) */
    uint64_t user;              /* host tag, returned in events (G-SA1: the entity index) */
    uint32_t num_shapes;        /* 1..4 */
    sa1_hostobj_shape shapes[SA1_HOSTOBJ_MAX_SHAPES];
    /* round 5 (read when struct_size covers them; 0 = the default) */
    float    launch_speed;      /* BALLOON: host units/s upward; 0 = the game's ground spring (5 SA1 units per step) */
    float    grab_reach;        /* HOLDABLE: host units added to the body radius for the grab cylinder (0 = 8 SA1 units) */
} sa1_hostobj_desc;

/* Registers an object (copied); *out_id receives its id (never 0). */
SA1_API sa1_status sa1_hostobj_add(sa1_context *ctx, const sa1_hostobj_desc *desc, uint32_t *out_id);
/* Replaces an object's description (a new kind or shape count rebuilds its proxies, other changes apply in place). */
SA1_API sa1_status sa1_hostobj_update(sa1_context *ctx, uint32_t id, const sa1_hostobj_desc *desc);
/* Moves an object (host space; one call per pair is enough: the worlds interpolate over the pair). */
SA1_API sa1_status sa1_hostobj_move(sa1_context *ctx, uint32_t id, const float position[3], float yaw);
#define SA1_HOSTOBJ_REMOVE_SILENT 0u    /* left or deleted: no "killed" bookkeeping */
#define SA1_HOSTOBJ_REMOVE_KILLED 1u    /* destroyed by the character: its proxies end "killed by a player" (enemy work
                                           +0x4E 0x1000, which E-102's hooked destructor reads) */
/* Removes an object: its proxies are destroyed at each world's next pair (FreeTask, the game's DestroyTask). */
SA1_API sa1_status sa1_hostobj_remove(sa1_context *ctx, uint32_t id, uint32_t how);

typedef struct sa1_hostobj_config {
    uint32_t struct_size;       /* sizeof(sa1_hostobj_config) */
    uint32_t window;            /* proxies per world, 1..64 (0 = 64) */
    float    enter_radius;      /* host units from the character (0 = 280 SA1 units; E-102 reaches 250, homing 100) */
    float    leave_radius;      /* host units (0 = 320 SA1 units; at least enter_radius) */
} sa1_hostobj_config;
SA1_API sa1_status sa1_hostobj_configure(sa1_context *ctx, const sa1_hostobj_config *cfg);

/* events (sa1_hostobj_event.type) */
#define SA1_HOSTOBJ_EVENT_HIT       1u  /* the character (or its projectile) damaged the object */
#define SA1_HOSTOBJ_EVENT_LOCKED    2u  /* E-102 locked on (its marker task appeared in the enemy work, +0x40) */
#define SA1_HOSTOBJ_EVENT_TARGETED  3u  /* E-102 fired a shot at it (the marker's shot slot became set) */
#define SA1_HOSTOBJ_EVENT_UNLOCKED  4u  /* the marker was released (a hit by E-102's shot releases it at once, 8C0D5D90) */
#define SA1_HOSTOBJ_EVENT_TOUCH     5u  /* REPORT_TOUCH: the character started touching it */

/* sa1_hostobj_event.flags */
#define SA1_HITF_BOUNCED     0x1u   /* the game bounced the character off it (8C048980: ball, airborne, Sonic / Tails /
                                       Knuckles; the world speed 2 per step upward) */
#define SA1_HITF_PROJECTILE  0x2u   /* the attacker was a list-1 object (E-102's shot, Big's lure, Sonic's sparkles ...) */
#define SA1_HITF_LOCKED      0x4u   /* the object was locked on by E-102 when hit */

/* sa1_hostobj_event.attack: the attack kind, classified from the game's state right after the CCL pass */
#define SA1_ATTACK_OTHER              0u
#define SA1_ATTACK_SPIN               1u    /* the ball: jump, roll (taskwk.flag 0x100) */
#define SA1_ATTACK_SPIN_DASH          2u    /* Sonic's spin dash: its charge and roll (modes 4, 5); his light speed dash
                                               (mode 6) */
#define SA1_ATTACK_HOMING             3u    /* Sonic, mode 14 (Super Sonic 77) */
#define SA1_ATTACK_LIGHT_SPEED_ATTACK 4u    /* Sonic, mode 44 */
#define SA1_ATTACK_TAIL               5u    /* Tails, shapes 1 / 2 */
#define SA1_ATTACK_TAILS_FLIGHT       6u    /* Tails, shape 3 */
#define SA1_ATTACK_PUNCH              7u    /* Knuckles, shape 1 */
#define SA1_ATTACK_MAX_HEAT           8u    /* Knuckles, mode 18 */
#define SA1_ATTACK_DIG                9u    /* Knuckles, shapes 2 / 3 */
#define SA1_ATTACK_HAMMER            10u    /* Amy, shape 1 */
#define SA1_ATTACK_LASER             11u    /* E-102's shot (exec 8C0D3DA0) */
#define SA1_ATTACK_LASER_BLAST       12u    /* its impact sphere (8C0D65BA) */
#define SA1_ATTACK_LURE              13u    /* Big's lure (8C0E943C) */
#define SA1_ATTACK_BIG_ATTACK        14u    /* Big, shape 1 */
#define SA1_ATTACK_SPARKLE           15u    /* Sonic's sparkles (8C0B5994 / 8C0B5B80) */
#define SA1_ATTACK_INVINCIBLE        16u    /* the invincibility item (playerwk.item 0x8000) */
#define SA1_ATTACK_SUPER             17u    /* Super Sonic (playerwk.equipment 0x8000) */

typedef struct sa1_hostobj_event {
    sa1_character_id character; /* the world it happened in */
    uint32_t type;              /* SA1_HOSTOBJ_EVENT_* */
    uint32_t object;            /* host object id */
    uint32_t attack;            /* HIT: SA1_ATTACK_* */
    uint32_t level;             /* HIT: the attacking shape's attack level (1..3) */
    uint32_t damage;            /* HIT: level - armor, at least 1 ("SA1 damage units") */
    uint32_t shape;             /* HIT / TOUCH: the object's shape that was hit */
    uint32_t flags;             /* SA1_HITF_* */
    uint64_t user;              /* the object's sa1_hostobj_desc.user */
    float    position[3];       /* host space: the attacking shape's centre */
    float    direction[3];      /* unit, host axes: the attacker's velocity (zero at rest) */
    float    speed;             /* host units per second */
    uint32_t step;              /* the world's step */
    /* round 5 */
    uint32_t reason;            /* RELEASED: SA1_RELEASE_* */
    uint32_t target;            /* RELEASED (THROW_AIMED): the host object of the nearest target-list entry, 0 = none */
} sa1_hostobj_event;

/* Pops up to `capacity` events of every ticking character (oldest first per character). Each world queues 256; when a
 * host does not poll, newer events are dropped and counted (sa1_hostobj_info.events_dropped). */
SA1_API sa1_status sa1_poll_hostobj_events(sa1_context *ctx, sa1_hostobj_event *out, uint32_t capacity,
                                           uint32_t *count);

/* SA1_EVENT_CAPACITY id: a proxy could not be created (the game's task pool is full, or its heap has less than 4 KB
 * in one block: the game's CCL_Init would wait forever on a failed allocation); arg = objects refused in the pair */
#define SA1_CAPACITY_HOST_OBJECTS 7u

typedef struct sa1_hostobj_info {
    uint32_t struct_size;       /* sizeof(sa1_hostobj_info) */
    uint32_t objects;           /* registered in the context */
    uint32_t proxies;           /* live in the character's world */
    uint32_t created, destroyed;/* proxies over the world's life */
    uint32_t refused;           /* proxies the world could not create (SA1_CAPACITY_HOST_OBJECTS) */
    uint32_t events_dropped;    /* events lost because the world's queue was full */
    uint32_t heap_free;         /* bytes free in the world's game heap (the sum of its free blocks) */
    uint32_t heap_largest;      /* the largest free block */
} sa1_hostobj_info;
/* The context's counts; with a character id (> 0) also its world's (zero before the world exists). */
SA1_API sa1_status sa1_hostobj_info_get(sa1_context *ctx, sa1_character_id id, sa1_hostobj_info *out);

/* Rings: adds n (negative: removes) through the game's own 8C08A198 inside the character's world (built if needed),
 * the 1-up per 100 and its jingle (SA1_EVENT_SOUND 0x2C0) included; the count stays within 0..999. */
SA1_API sa1_status sa1_character_add_rings(sa1_context *ctx, sa1_character_id id, int32_t n);

/* ---- contacts with moving collision objects (API 0.3 draft; pushing physics props) ---- */
/*
 * Which moving collision object (sa1_collision_object_*) the character touched in each 60 Hz step, measured on the
 * game's own result of the step without changing the game: the character's collision sphere where the game's land
 * collision (PSetPosition 8C072B44 / 8C0730D8: radius csts.radius, centre half the height above taskwk.pos along the
 * character's up) left it, against the object's polygons as the world's DynamicCOL entry held them in that step. A
 * contact is within 1 SA1 unit of the sphere's surface. Its type comes from the contact normal (from the object to the
 * character): LANDING when the object carries the character (the game's playerwk.ttp is its task) or the normal points up
 * (y >= 0.47, the game's landing slope 0x2C00), HEAD when it points down (y <= -0.47), else SIDE. A side contact the
 * game's wall response pushed the character back from in that step (playerwk.wall_normal agrees) is SA1_CONTACTF_WALL
 * and reports the game's wall normal; a riding contact reports the game's floor normal.
 * Contacts are recorded only in worlds with GRID or no static collision (the worlds that carry moving objects).
 */
#define SA1_CONTACT_SIDE    1u      /* pushed against it sideways */
#define SA1_CONTACT_LANDING 2u      /* stands / landed on it */
#define SA1_CONTACT_HEAD    3u      /* hit it from below */

#define SA1_CONTACTF_RIDING 0x1u    /* the game carries the character with the object in this step (playerwk.ttp) */
#define SA1_CONTACTF_WALL   0x2u    /* the game's wall response pushed the character back from it in this step */
#define SA1_CONTACTF_FIRST  0x4u    /* no contact with this object in the step before */

typedef struct sa1_contact {
    sa1_character_id character;
    uint32_t object;            /* sa1_collision_object_add id */
    uint32_t type;              /* SA1_CONTACT_* */
    uint32_t flags;             /* SA1_CONTACTF_* */
    uint32_t step;              /* the world's step */
    float    point[3];          /* host space: the object's point nearest to the sphere's centre */
    float    normal[3];         /* unit, host axes: from the object towards the character */
    float    velocity[3];       /* host units/s: the character's world velocity entering the step (motionwk.spd) */
    float    velocity_after[3]; /* host units/s: after the step (the game's push-back applied) */
    float    depth;             /* host units: the sphere's radius minus its distance from the object (> 0: inside) */
} sa1_contact;

/* Pops up to `capacity` contacts of the character recorded since the last call (oldest first; one per object and step;
 * 128 kept per character, older ones dropped). */
SA1_API sa1_status sa1_character_contacts(sa1_context *ctx, sa1_character_id id, sa1_contact *out, uint32_t capacity,
                                          uint32_t *count);

/* ---- host objects: round 5's game side (sadc docs/libsa1_interaction.md 2.5, 2.6 and addendum 6.1-6.6) -------- */
/*
 * Two more kinds. HOLDABLE is a prop the characters lift, carry, put down and throw with the game's own hold code: the
 * proxy is a CCL id 4 task with a motion work, set up by the game's movable-object initialiser 8C0586A0 (taskwk.id = the
 * lift class 4, flag 0x240), whose CCL shapes are two cylinders made from the host's shape 0: a grab cylinder (radius
 * max(15, body radius + grab reach), half height max(6, body half height); the game's lift needs its side contact) and
 * the body (pushes the character unless NOT_SOLID). A sphere body gives two spheres instead: the game never lifts those
 * (a sphere gives no side contact), but they can still be hit (HOLD_HITS). Every playable character lifts class 4; with
 * HEAVY the class is 5, which only Big lifts (the others grab and push it: UNVERIFIED behaviour). While a character holds
 * it the game places it at the hands every step (8C0B1DE0 and the other characters' hold updates) and the host should
 * show the prop at sa1_character_held's pose; a throw, a put-down or a drop is a RELEASED event with the game's speed,
 * after which the proxy keeps the release point until the host moves the object (the host owns the flight).
 * BALLOON is a homing / lock-on target that never hurts: an attack of the character's body launches the character through
 * the game's ground-spring request (8C07C720: next-action order 3, world speed straight up, default the spring's 5 SA1
 * units per step) or, with LAUNCH_BOUNCE, through SetVelocityP alone when the enemy bounce's conditions hold (the ball and
 * the homing chain stay); a projectile only pops it. In both cases the host decides whether the balloon pops (removal).
 */
#define SA1_HOSTOBJ_HOLDABLE   5u   /* CCL id 4, lift class 4 (0x200): lifted, carried, put down and thrown */
#define SA1_HOSTOBJ_BALLOON    6u   /* CCL id 3: homing / lock-on target, never hurts; a hit launches the character */

/* sa1_hostobj_desc.flags, round 5 bits */
#define SA1_HOSTOBJ_HOLD_HITS      0x0080u  /* HOLDABLE: report HIT when an attack out-levels it (a breakable prop) */
#define SA1_HOSTOBJ_HOLD_LOCKED    0x0100u  /* HOLDABLE: held elsewhere / not allowed: taskwk.flag 0x1000 (8C06C880 skips it) */
#define SA1_HOSTOBJ_HEAVY          0x0200u  /* HOLDABLE: lift class 5 instead of 4 (only Big lifts it) */
#define SA1_HOSTOBJ_POP_ON_TOUCH   0x0400u  /* BALLOON: the character's body touching it counts as a hit */
#define SA1_HOSTOBJ_LAUNCH_BOUNCE  0x0800u  /* BALLOON: SetVelocityP only, when the enemy bounce 8C048980's conditions hold
                                               (ball, airborne, Sonic / Tails / Knuckles): the ball and the homing stay */
#define SA1_HOSTOBJ_KEEP_HSPEED    0x1000u  /* BALLOON: keep the character's horizontal speed in the launch */

/* sa1_hostobj_desc.launch_speed / grab_reach and sa1_hostobj_event.reason / target: the round 5 tails of those structs
 * (above) */

/* events (sa1_hostobj_event.type), round 5 */
#define SA1_HOSTOBJ_EVENT_LIFTED    6u  /* a character took a HOLDABLE (its playerwk.htp is the proxy) */
#define SA1_HOSTOBJ_EVENT_RELEASED  7u  /* thrown / put down / dropped: position, direction and speed (host units/s) of the
                                           game's release, reason and (aimed) target */
#define SA1_HITF_LAUNCHED           0x8u    /* HIT on a BALLOON: the character was launched */
/* sa1_hostobj_event.reason (RELEASED) */
#define SA1_RELEASE_THROW           1u      /* a throw at the fixed speed (Sonic's actions 60 / 62 ...) */
#define SA1_RELEASE_THROW_AIMED     2u      /* a throw at the nearest entry of the target list (59 / 61 ...): .target */
#define SA1_RELEASE_PUT_DOWN        3u      /* put down (63 ...; Big's let-go actions 80 / 45) */
#define SA1_RELEASE_DROP            4u      /* the character's own drop: hurt (no speed: the drop's P2G(1, 1, 0) needs a
                                               contact without the hurt flag), or the object lost */
#define SA1_RELEASE_FORCED          5u      /* sa1_hostobj_release, a removal, a kind change or a world mask */

/* The held state of a character after its last step (round 5; a host draws the held prop here, interpolating
 * position_prev -> position with the character's own render alpha). */
#define SA1_HOLD_NONE      0u
#define SA1_HOLD_LIFTING   1u           /* the lift motion is running; the object is not taken yet */
#define SA1_HOLD_CARRYING  2u
#define SA1_HOLD_THROWING  3u           /* a throw / put-down action is running, before its release frame */
typedef struct sa1_hostobj_held {
    uint32_t struct_size;       /* sizeof(sa1_hostobj_held) */
    uint32_t object;            /* host object id, 0 = nothing held or lifted */
    uint32_t phase;             /* SA1_HOLD_* */
    float    position[3];       /* host space: the proxy's position after the last step (the registered reference point) */
    float    position_prev[3];  /* after the step before (= position at the first held step) */
    float    yaw;               /* the holder's facing, radians (the sa1_pose.yaw convention) */
    float    hand[3];           /* host space: the hand anchor of the character's hold update (8C0B1DE0 ...) */
    /* round 0.9.3a (SA1_API_SUPER_GENERAL; written when struct_size covers them; sadc docs/research/super_general_design.md
       4.12): the held proxy at the drawn step (sub step 0) of the last pair and of the pair before, and the holder's yaw
       there (radians, as yaw). The body sa1_character_render draws stands at the drawn steps of its last two pairs:
       interpolate these with the render alpha, not position_prev -> position (the last pair's two steps: a 30 Hz
       sawtooth of 7-15 Hammer units at a run). drawn_prev = drawn at the first drawn step of a hold. */
    float    drawn[3], drawn_prev[3];
    float    drawn_yaw, drawn_yaw_prev;
} sa1_hostobj_held;
/* The object the character holds (or is lifting); object 0 without a world or a held object. */
SA1_API sa1_status sa1_character_held(sa1_context *ctx, sa1_character_id id, sa1_hostobj_held *out);
/* Makes every character holding the HOLDABLE `id` drop it (the character's own drop function, at each world's next pair):
 * RELEASED with SA1_RELEASE_FORCED. SA1_ERR_INVALID_ARGUMENT for another kind. */
SA1_API sa1_status sa1_hostobj_release(sa1_context *ctx, uint32_t id);

/* Which kinds the character's world mirrors (bit SA1_HOSTOBJ_KIND_BIT(kind); 0 = every kind, the default; kept for a world
 * built later, applied at each world's next pair). Followers (round 6b): ENEMY and HAZARD, plus TARGET and BALLOON when
 * they may attack, never HOLDABLE. A held object dropped from the mask is released first (FORCED). */
#define SA1_HOSTOBJ_KIND_BIT(k) (1u << (k))
SA1_API sa1_status sa1_hostobj_world_mask(sa1_context *ctx, sa1_character_id id, uint32_t kind_mask);

/* ---- the character side: damage, options, HURT (design 2.5) ---- */
/* sa1_event of type SA1_EVENT_HURT: the character was hit. id = the credited host object (0: the HURT surface, a host
 * request without an object, or a game object), arg = rings lost (bits 0-15) | SA1_HURT_* << 16, bank = rings left,
 * position = the character. Detected after the step from the game's own result (its damage checks set wtimer to 120). */
#define SA1_EVENT_HURT 14
#define SA1_HURT_BARRIER   0x01u    /* a real barrier absorbed it */
#define SA1_HURT_ABSORBED  0x02u    /* an option absorbed it (SA1_COPT_NO_DEATH / KEEP_RINGS: knock-back, no ring loss) */
#define SA1_HURT_FATAL     0x04u    /* no rings: the death sequence started (SA1_EVENT_DEATH follows in the same step) */
#define SA1_HURT_HOST      0x08u    /* from sa1_character_damage */
#define SA1_HURT_SURFACE   0x10u    /* the HURT surface (SA1_SURFACE_HURT) */
#define SA1_HURT_IGNORED   0x20u    /* a host request that did nothing (invulnerable, invincible, SA1_COPT_NO_DAMAGE) */
/* SA1_EVENT_DEATH (worlds built through the character API): player 0 died; pushed once, in the step in which the game
 * started that death; id = the credited host object (HURT, HOST; else 0), position = the character, arg = the cause.
 * Round 0.9.1 (sadc docs/research/gsa1_070_drown.md E1 with its Review R3.6 / R4.2) covers every way the game dies:
 *   - the game's death request 8C07BBC2 (playerwk.item 0x4000 appeared): HURT, HOST, else OTHER (its other callers: Big's
 *     8C0E0E70, the objects of 8C086F5E, a boss, and E-102's time-up 8C0D23E4, which also pushes SA1_EVENT_TIME_UP right
 *     before the DEATH: the cause stays OTHER, as the host-object oracle's references recorded it);
 *   - drowning: the drowning countdown 8C07BE6E ran out (SetInputP(0, 39): taskwk.smode became 39): DROWN;
 *   - a death zone of the stage or a GRID kill plane (SA1_SURFACE_KILL): the death-zone task (exec 8C07BAC0) got its death
 *     child (8C07BD48, the scream and the death camera): ZONE;
 *   - fallback: the world's first restart request 8C04E386(2) (SA1_EVENT_RESTART with id 2) when no SA1_EVENT_DEATH came
 *     before it: OTHER, pushed in that step right after the RESTART (later requests of a dead world, e.g. E-102's second
 *     one after his time-up, are no new death).
 * The game asks for the restart (SA1_EVENT_RESTART, id 2) after each of them: 121 steps after a death request or a death
 * zone, 240 steps after a drowning (whose own drown action plays in between). E-102 never drowns (his main has no water
 * probe). */
#define SA1_DEATH_OTHER    0u       /* the death request of other game code, E-102's time-up, or the RESTART fallback */
#define SA1_DEATH_HURT     1u       /* a hit with no rings (SA1_EVENT_HURT with SA1_HURT_FATAL) */
#define SA1_DEATH_HOST     2u       /* sa1_character_damage with SA1_DAMAGE_KILL */
#define SA1_DEATH_DROWN    3u       /* drowned: the air timer's countdown ran out (round 0.9.1) */
#define SA1_DEATH_ZONE     4u       /* a death zone of the stage or a GRID kill plane (round 0.9.1) */

#define SA1_DAMAGE_FACE_SOURCE 0x1u /* turn the character to the source first: the game knocks back along the facing, so
                                       away from the source */
#define SA1_DAMAGE_KILL        0x2u /* the game's death request (8C07BBC2), regardless of rings */
typedef struct sa1_damage {
    uint32_t struct_size;       /* sizeof(sa1_damage) */
    uint32_t flags;             /* SA1_DAMAGE_* */
    float    source[3];         /* host space (FACE_SOURCE) */
    uint32_t object;            /* host object credited in the HURT event (0 = none) */
} sa1_damage;
/* A hit on the character, applied before the executor of its next step (at most 4 queued): taskwk.flag 4, exactly what an
 * enemy contact leaves after a CCL pass, so the game's own hurt path does the rest (knock-back, 120 steps of
 * invulnerability, the barrier, the ring scatter, death at 0 rings). During invulnerability it does nothing (HURT with
 * SA1_HURT_IGNORED). SA1_ERR_NOT_LOADED without a world.
 * Round 0.9.2 (SA1_API_092; sadc docs/research/gsa1_090_faults.md F1 with its Review R.3.5): a request made before the
 * character's first step (a character just created or re-created after a respawn) is applied at its first step that has
 * the player work (PInitialize ran: player 0's playerwk exists): it is held while the player task is still in mode 0, and a
 * request that finds the player work missing in any other state is dropped. A held request returns SA1_OK at once and
 * takes effect, with its SA1_EVENT_HURT / SA1_EVENT_DEATH, in that step. */
SA1_API sa1_status sa1_character_damage(sa1_context *ctx, sa1_character_id id, const sa1_damage *d);

/* Options (sa1_character_set_option; value 0 = off, the default; kept for a world built later): without touching game
 * code */
#define SA1_COPT_NO_DEATH    1u     /* a hit with 0 rings knocks back without killing (a phantom barrier) */
#define SA1_COPT_KEEP_RINGS  2u     /* hits knock back without scattering rings (a phantom barrier on every hit) */
#define SA1_COPT_NO_DAMAGE   3u     /* hits are ignored entirely (wtimer kept at 1 while a hit is pending; no blinking) */
SA1_API sa1_status sa1_character_set_option(sa1_context *ctx, sa1_character_id id, uint32_t option, int32_t value);

/* ---- lives, companions, game options (round 0.9.1; sadc docs/research/gsa1_070_drown.md E2, gsa1_070_bird.md F1 and
 *      gsa1_rounds_plan.md 14.1h, with their reviews) --------------------------------------------------------------- */
/*
 * Per-character settings of the world layer. Each is applied to the character's world at once (between ticks) and kept
 * by the context for a world built later (sa1_character_create_ex without SA1_CREATE_WORLD: the first tick builds it;
 * the setting is in place before the world's first step). A puppet stores them only. Without a call a world is the
 * game's: the setting is never touched (the reference worlds never call these).
 */
#define SA1_API_CHAROPT 1

/* Lives: the life counter of the character's world (B_8C752BE0, a signed byte; the HUD shows it). The game gives one per
 * 100 rings (8C08A198) and takes one when it restarts the act after a death; libsa1 worlds never restart the act (the
 * host rebuilds the character after SA1_EVENT_RESTART), so a host that counts lives down creates the new character and
 * sets the old count minus 1 before its first tick (and the game-over count at 0). Clamped to 0..127 (8C08A120's cap).
 * A world starts with the game's 4 (8C08A04A) without a call. SA1_ERR_NOT_FOUND for an unknown id. */
SA1_API sa1_status sa1_character_set_lives(sa1_context *ctx, sa1_character_id id, int32_t lives);

/* Companions: game tasks the story keeps next to a character, made and freed with the game's own calls, against the
 * game's own gate:
 *   SA1_COMPANION_BIRD  Amy's Birdie (1ST_READ 8C0DECDA / 8C0DE964 / 8C0DE8C0, matching C): AmyMain's spawner 8C0DEDC0
 *                       makes it in the world's first step on Amy's story stages only (CurrentCharacter 5, game mode
 *                       4 / 5 / 9, stage 0x0301 0x0302 0x0A00 0x0C00 0x0C01 of D_8C15C808 or an adventure field
 *                       0x1A00-0x22FF with no event): CreateElementalTask(2, 3, 8C0DECDA). Its exec follows player 0, flaps
 *                       (faster when it moves), draws itself (archive 0 of Amy, textures 44-52) and its blob shadow; it
 *                       makes no sound and has no collision. Amy only.
 * on = 1: before the world's first step (create, or a world built later) the task is made only when that gate fails (else
 * the game makes it in step 0); later, when the world has none. on = 0: before the first step, when the gate passes the
 * game's own bird is freed right after the world's first pair (FreeTask 8C098786, before it ever drew); else every bird
 * task is freed at once (DestroyTask removes it at its next exec). Idempotent: never a second bird.
 * SA1_ERR_NOT_FOUND unknown id; SA1_ERR_INVALID_ARGUMENT another kind than Amy or an unknown companion;
 * SA1_ERR_UNSUPPORTED a world whose player 0 is not that Amy (CurrentCharacter or playertwp[0]->counter.b[1] not 5: a
 * follower world, sadc gsa1_rounds_plan.md 12.2); a faulted world's status. */
#define SA1_COMPANION_BIRD 1u
SA1_API sa1_status sa1_character_set_companion(sa1_context *ctx, sa1_character_id id, uint32_t companion, int32_t on);
/* *present = 1 when a task of the companion lives in the character's world now (0 without a world) */
SA1_API sa1_status sa1_character_companion(const sa1_context *ctx, sa1_character_id id, uint32_t companion,
                                           int32_t *present);

/* Game options: PC-only switches inside the lowered game code (SA1_GA blocks of sadc src; a deviation from the game,
 * available on request; value 0 = the game, the default). The switch lives in the world's own copy of the game code's
 * data (pcdata), so the reference worlds, which never set it, run the game's code.
 *   SA1_GOPT_TAILS_SNAP_TAILS  Tails' display 8C0BA820 blends his bent tails into the propeller blades over 100 pairs
 *                              while he speeds up into a run (and back when he slows down; pwp->free.f[6] +-0.01 per pair,
 *                              a vertex blend whose in-between tails are flat ribbons: gsa1_070_tails.md T4). With the
 *                              option the display uses the blend weight snapped to 0 / 1 (1 from 0.5): the tails switch at
 *                              once between the bent tails and the blades, with the spin, the angle between the two tails
 *                              and the tail after-images of the end state. f[6] itself still rises and falls as in the
 *                              game (the switch comes 50 pairs into a run). Any kind may set it (only Tails' code reads
 *                              it). */
#define SA1_GOPT_TAILS_SNAP_TAILS 1u
/* round 0.9.3a (SA1_API_SUPER_GENERAL; sadc docs/research/super_general_design.md 4.8 / 4.9 with its Review RC16):
 *   SA1_GOPT_SUPER_IDLE_BY_FORM  Sonic's idle-voice choice 8C07DCE0 (the SA1_SUPERVOICE block of sadc
 *                              src/1st_read/code_8C07DCE0.c): value 1 = by his form: the level-19 lines (Super Sonic's: 396
 *                              / 388) while player 0's equipment has 0x8000, else those of the world's normal level (its
 *                              own level and act; a world booted on level 19 - kind 8, a 0.9.3-style super-capable Sonic -
 *                              uses level 1 act 0, a plain world's lines: 390 / 227). CurrentLevel itself is never
 *                              written. Any kind may set it (only Sonic's choice reads it).
 *   SA1_GOPT_SUPER_AURA_ANIM     Super Sonic's aura display 0C91D222 (the SA1_AURAANIM block of sadc
 *                              src/overlays/b_chaos7/code_0C91D200.c): 0 the game's (the standing aura reads btimer & 7: 4
 *                              of its 8 textures, a new one each pair), 1 calm (the standing aura reads (btimer >> 1) & 7
 *                              as the dash auras do: all 8 in order), 2 slow (all three read (btimer >> 2) & 7: each
 *                              texture for two pairs). Any value outside 0..2 is SA1_ERR_INVALID_ARGUMENT. Any kind may
 *                              set it (only the aura of a super-capable world reads it). */
#define SA1_GOPT_SUPER_IDLE_BY_FORM 2u
#define SA1_GOPT_SUPER_AURA_ANIM    3u
#define SA1_AURA_ANIM_DREAMCAST 0
#define SA1_AURA_ANIM_CALM      1
#define SA1_AURA_ANIM_SLOW      2
/* round 0.9.4 (SA1_API_SET; sadc docs/research/gsa1_094_items.md 6.6 / F13, gsa1_094_plan.md 6.3 task 5):
 *   SA1_GOPT_ITEM_ICON_LIMIT     the game's item icon list B_8C78C394 (the item pictures that slide in on the screen: 20
 *                              entries, emptied 134 steps after its last append; the SA1_ICONLIMIT block of sadc
 *                              src/1st_read/code_8C05E0F8.c) has no bound: 21 or more items in quick succession (a row
 *                              of item boxes, Big's lure) write past it into the globals behind it (the jump panel list
 *                              B_8C78C4E0, then the player task pointers: about 40 fault the world). Value 1: an icon
 *                              that does not fit is not queued - the item is still given, only its picture is dropped.
 *                              Any kind may set it (the item boxes and Big's lure 8C0E943C append). */
#define SA1_GOPT_ITEM_ICON_LIMIT    4u
SA1_API sa1_status sa1_character_set_game_option(sa1_context *ctx, sa1_character_id id, uint32_t option, int32_t value);

/* ---- screen / HUD (round 0.9.1, API 0.4 draft; sadc docs/research/gsa1_070_hud.md 4.1 with its Review R.5) ---------- */
/*
 * The game's own 2D draws of a character's world, on the Dreamcast's 640 x 480 screen (+x right, +y down; a host scales
 * the screen to its own): the HUD the game draws on every action stage, made by the game's own HUD functions where its
 * frame steps call them (after each pair's two logic steps: 8C08A4BA rings and lives, 8C08A724 the time, or the score on
 * Sand Hill), and the 2D draws of the world's tasks (E-102's "+N" time bonus counter and last-seconds digits, Big's HUD
 * - WEIGHT, rings and lives: his task draws it, the common HUD skips him - and his fishing reel, line-tension gauge and
 * line length, the drowning countdown digits above the character's head, Knuckles' emerald radar where its task runs).
 * Not drawn by libsa1 worlds: the camera's underwater tint (the game's camera task is not in them), Tails' race bar (no
 * race opponent), the pause menu and the results screens.
 *
 * Parts (SA1_COPT_HUD_PARTS, the mask of SA1_HUD_*; 0, the default: no HUD pass and an empty screen):
 *   SA1_HUD_RINGS      the TIME label with the ring icon (one texture cell) and the ring count (red blink at 0 rings).
 *                      Without SA1_HUD_TIME the label is cropped to the game's ring-only cell (D_8C1BF5B4, as the
 *                      adventure fields draw it): no lone "TIME" (display only)
 *   SA1_HUD_LIVES      the character's life icon and the lives
 *   SA1_HUD_TIME       the timer mm:ss:hh (E-102: his countdown, white / yellow under 30 s, red under 15 s), or the score
 *   SA1_HUD_CHARACTER  every 2D draw of the world's tasks (their `source` is the drawing task's exec)
 * The rings / lives / time parts are the game's calls split with the game's own visibility flags (B_8C752BDE lives,
 * B_8C752BDF rings), after the game's own test of its HUD wrapper (not during events, not in Twinkle Circuit, not for
 * Big): the world's state after a pair is the game's whatever the mask. The HUD pass writes display state only (the
 * HUD's sprite, the texlist, the constant material and the ring blink phase: 26 bytes, measured), never in replays.
 *
 * The clock: worlds boot with the game clock stopped (D_8C1BF198 = 0, as the reference simulator): the timer shows
 * 00:00:00 (E-102: 03:00:00). SA1_COPT_CLOCK 1 starts it with the game's 8C08A5AE (0: stops it, 8C08A5B6): it counts up,
 * to 99:59:59 (the HUD then shows 99:59:99), and for E-102 down from 3:00, with the game's rule: at 00:00:00 he dies
 * (SA1_EVENT_TIME_UP, then SA1_EVENT_DEATH, then the RESTART request), and lock-on kills of host objects add seconds.
 * SA1_COPT_E102_NO_TIME_LIMIT 1 (a host option, not the game): E-102's clock is held every step (his time stays where
 * it is, 03:00:00 plus any bonus; game code cannot start it again), so the time never runs out. Other characters ignore
 * it. While a clock is held or stopped, the game's score-chain window B_8C7997A0 is not counted down either (as today).
 *
 * Counters across a respawn (the host builds a new character after SA1_EVENT_RESTART or for a change of upgrades): read
 * sa1_state_ex.time_* / lives of the old one, then sa1_character_set_time / sa1_character_set_lives on the new one before
 * its first tick. E-102's 00:00:00 is his time-up: set on an E-102 it gives the game's restart time 03:00:00 (8C08A580),
 * never a death loop.
 */
#define SA1_API_SCREEN 1

#define SA1_HUD_RINGS     0x01u
#define SA1_HUD_LIVES     0x02u
#define SA1_HUD_TIME      0x04u
#define SA1_HUD_CHARACTER 0x08u
#define SA1_HUD_ALL       0x0Fu

/* more options of sa1_character_set_option (value 0 = off, the default; kept for a world built later, applied before
 * its first step): */
#define SA1_COPT_CLOCK               4u     /* 1: the game clock runs (8C08A5AE), 0: stopped (8C08A5B6); without a call it
                                               stays as the world boots: stopped */
#define SA1_COPT_E102_NO_TIME_LIMIT  5u     /* 1: E-102's clock held (no time-up death); 0: the game's rule */
#define SA1_COPT_HUD_PARTS           6u     /* value: the mask of SA1_HUD_* (0: no HUD pass, an empty screen) */

/* The time of the character's world (B_8C752BE1 / BE2 / BE3 through the game's 8C08AB20): minutes 0..99, seconds 0..59,
 * frames 0..59 (clamped). Kept for a world built later (before its first step). E-102: 00:00:00 gives 03:00:00. */
SA1_API sa1_status sa1_character_set_time(sa1_context *ctx, sa1_character_id id, int32_t minutes, int32_t seconds,
                                          int32_t frames);

typedef struct sa1_screen_sprite {
    float    rect[4];           /* x0, y0, x1, y1 in screen pixels around `pivot`, before `angle`: -cx * sx .. (w - cx) *
                                   sx by -cy * sy .. (h - cy) * sy (the NJS_TEXANIM's size w x h and centre cx, cy, times
                                   the sprite's scale) */
    float    pivot[2];          /* the sprite's position (NJS_SPRITE p.x, p.y) on the 640 x 480 screen */
    float    angle;             /* radians, turning +x towards +y (clockwise on the screen; NJD_SPRITE_ANGLE only: Big's
                                   reel); 0 otherwise */
    float    uv[4];             /* u0, v0 at (x0, y0), u1, v1 at (x1, y1): the NJS_TEXANIM's / 256 (flips applied; 1.0 = the
                                   texture's size) */
    uint8_t  color[4];          /* RGBA: the constant material with NJD_SPRITE_COLOR (the zero-ring blink, E-102's timer
                                   colours, Big's numbers), else white */
    float    priority;          /* the draw's 2D depth (njDrawSprite2D pri): -1 front, more negative behind */
    uint32_t part;              /* SA1_HUD_* */
    uint32_t source;            /* the DC address of the task exec that drew it (SA1_HUD_CHARACTER), 0 for the HUD pass */
} sa1_screen_sprite;

typedef struct sa1_screen_vertex {
    float    x, y;              /* screen pixels */
    float    u, v;              /* textured polygons: NJS_TEX / 256; else 0 */
    uint8_t  color[4];          /* RGBA (white when the game gave no colours) */
} sa1_screen_vertex;

typedef struct sa1_screen_draw {
    uint32_t prim;              /* SA1_PRIM_SPRITES: sprites[first .. first + count); SA1_PRIM_TRIANGLES: vertices[first ..
                                   first + count) as a triangle list (2D polygons, njDrawPolygon2D with fill: Big's
                                   tension bar, a gouraud quad); SA1_PRIM_LINES: vertices[first .. first + count) as
                                   segments (a polygon's outline) */
    uint32_t first, count;
    int32_t  archive, texture;  /* sa1_texture_get(kind, archive, texture) of the character; -1: untextured */
    uint32_t flags;             /* SA1_DRAW_BLEND (with blend_src / blend_dst), SA1_DRAW_ADDITIVE, SA1_DRAW_ALPHA_TEST,
                                   SA1_DRAW_NO_TEXTURE (the game's texture is not one of the character's archives),
                                   SA1_DRAW_UNLIT (always) */
    uint8_t  blend_src, blend_dst;  /* SA1_BLEND_* (njColorBlendingMode at the draw; the HUD: SRC_ALPHA / INV_SRC_ALPHA) */
    uint8_t  reserved[2];
    float    priority;          /* as sa1_screen_sprite.priority */
    uint32_t part;              /* SA1_HUD_* */
    uint32_t source;            /* as sa1_screen_sprite.source */
} sa1_screen_draw;

typedef struct sa1_screen {
    uint32_t struct_size;       /* sizeof(sa1_screen) */
    uint32_t generation;        /* changes whenever the screen changes (a new pair, other parts) */
    float    width, height;     /* 640, 480: the game's screen */
    uint32_t num_draws;
    const sa1_screen_draw *draws;   /* in drawing order: the opaque draws, then the blended ones, each from the most
                                       negative priority (behind) to the least (front), the game's order among equal
                                       priorities (the Dreamcast sorts its 2D layers by depth: Big's opaque tension bar at
                                       -1.82 lies behind the translucent gauge frame at -1.819, whose window shows it) */
    uint32_t num_sprites;
    const sa1_screen_sprite *sprites;
    uint32_t num_vertices;
    const sa1_screen_vertex *vertices;
} sa1_screen;

/* The 2D draws of the character's last pair, library-owned: valid until the next call for this character, its tick,
 * destroy, unload or shutdown. Built from the pair the character's last tick captured (the last pair of a tick that ran
 * several), also when the game's camera did not see the character. Not interpolated: the game draws its HUD once per
 * 1/30 s pair. An empty screen (num_draws 0) without SA1_COPT_HUD_PARTS, before the first tick, or for a puppet. */
SA1_API sa1_status sa1_character_screen(sa1_context *ctx, sa1_character_id id, const sa1_screen **out);

/* ---- round 0.9.2 (sadc docs/research/gsa1_090_plan.md 6.2-6.4: the 0.9.0 report's collision, water, faults) ---------- */
/*
 * SA1_API_092 tells a host at compile time that this header has round 0.9.2's additions, declared with their families:
 *   sa1_input.host_flags (SA1_INPUT_HOST_WATER_KNOWN / IN_WATER)       the host's water at the character
 *   sa1_state_ex.water_attr / water_level                            the game's water probe result
 *   SA1_CAPACITY_LAND_QUERY / SA1_CAPACITY_LAND_HITS                  the land-collision query bound (the window keeps the
 *                                                                     objects of the character's query at <= 15 - since
 *                                                                     round 0.9.3 at <= 191 -; the lowered 8C073160 /
 *                                                                     8C0777E0 hold 192 per call)
 *   SA1_SURFACE_SNOW / SPEED_PATH / FOOTPRINTS                        COL 0x8 / 0x800000 / 0x100000
 *   sa1_character_damage                                              requests before the first step held (see there)
 * Behaviour changes without new names (host worlds only; the reference worlds are unchanged):
 *   - GRID land objects are made per (cell, COL word, orientation): floors (normal y >= 0.5 in SA1 space), walls
 *     (|normal y| < 0.5) and ceilings (normal y <= -0.5) never share an object, so the game's per-object averaging of
 *     push-backs (8C029B00) no longer mixes a wall's or a ceiling's with the floor's (sadc gsa1_090_collision.md C1 / F2);
 *     SA1_GRID_MIXED_ORIENTATIONS (sa1_collision_grid.flags) keeps the old partition for comparisons with the game;
 *   - the NEAR-poly list of host worlds holds 512 (F5);
 *   - a GRID synthesis that cannot fit the 64 MB static collision segment (about 2.75-3.0 million GRID triangles of a real
 *     map) fails before it cuts anything, with the estimate and the largest face in the message (SA1_ERR_LIMIT; the
 *     message of sa1_collision_prepared_info / sa1_last_error).
 */
#define SA1_API_092 1

/* ---- round 0.9.3 collision: solid ceilings, curved surfaces (sadc docs/research/gsa1_091b_ceiling.md FIX-C1 / C2 and
 *      gsa1_091a_loop.md LOOP-L1, with their Reviews) ------------------------------------------------------------------ */
/*
 * SA1_API_CEILING tells a host at compile time that this header has:
 *   SA1_SURFACE_FRONT_ONLY / SA1_MAT_FRONT_ONLY     front-only land triangles: a sphere whose centre starts the step behind
 *                                                   the plane does not touch them (host worlds: GRID and moving objects)
 *   SA1_SURFACE_CURVED                              GRID keeps the triangle as it is (never an underside within reach)
 *   SA1_GRID_UNDERSIDES_DROP                        the one-way behaviour before 0.9.3 (undersides and FRONT_ONLY left out)
 *   sa1_collision_info / _prepared_info .grid_front_only, .grid_curved
 * Behaviour change without a new name (host GRID worlds only; the reference worlds are unchanged): GRID's undersides within
 * reach (sa1_collision_grid.under_reach) that are near-horizontal become front-only instead of being left out, so a thin
 * roof is a ceiling from below (the game's own head contact, 8C0747D2, stops a jump under it) and absent from above (the
 * fix of round 4b2 for falls through thin planks stays); the top of such a slab is not touched from more than a sphere's
 * radius behind by a sphere rising from under it (a slab thinner than a step's rise was passed: the doc's residual 1). The
 * sphere test runs the game's own code; the only PC-only change is those two skips, enabled per host world (a pcdata block of
 * the lowered 8C029B00, SA1_FRONTONLY) and only for the land objects libsa1 makes (the GRID segment and the moving objects'
 * models), never for the game's own.
 * Two more behaviour changes of the window (host GRID worlds; the 0.9.2 owner logs' "query took 15 objects" / "polygon
 * budget" lines, sadc build/research/colltunnel): the query bound is the window's own 191 objects when the lowered game
 * code holds 192 per call (SA1_CAPACITY_LAND_QUERY: 15 only with SA1_GRID_DC_LIMITS), so every land object in reach of the
 * character is in its land collision, as on the DC; SA1_CAPACITY_WINDOW_POLYGONS is reported only when an object within
 * 40 units was left out even with the whole polygon pool.
 */
#define SA1_API_CEILING 1

/* ---- Super Sonic (API 0.4; round 6a: sadc docs/research/super_sonic.md 4 and 6 with its Review R.2-R.4,
 *      gsa1_rounds_plan.md 11.1 / 11.2 and the owner's answer 14.1a) ------------------------------------------------------ */
/*
 * SA1_API_SUPER tells a host at compile time that this header has the Super Sonic additions.
 *
 * SUPER SONIC (SA1_CHAR_SUPER_SONIC) ticks. His world is a Sonic world (game character 0: SonicMain, Sonic's physics entry 0
 * and action table, whose actions 130-141 are the Super ones on B_CHAOS7's models) booted on the Perfect Chaos stage, level
 * 19 act 0 (stage 0x1300: SonicMain switches the 12 Super Sonic welds on only there, super_sonic.md 1.8), with B_CHAOS7 in
 * its image (SA1_LOAD_SUPER). SA1_CREATE_STAGE with another stage is SA1_ERR_INVALID_ARGUMENT, and so is DISC collision of
 * another stage (both need the 0x0C900000 overlay slot). The game turns him Super itself: the world's level task (exec
 * 0x0C900000, asm only in the game) is a stand-in that, in its first step and in the game's order (0C914E36, 0C914DC6),
 * creates
 *   the sea plane   CreateElementalTask(6, 0, 0C91514A): the Perfect Chaos water, an 80 x 80 floor at SA1 y = 0 that stays
 *                   at the spawn for 600 steps and then follows the character; once he is not Super any more it sinks and
 *                   kills him (8C07BBC2: the game's end of Perfect Chaos). Only in a world whose landtable is the stage's
 *                   (DISC or FAITHFUL collision) and only with SA1_SUPER_SEA_PLANE (the default there when the host gives no
 *                   super_flags); never with host collision (GRID or none);
 *   the trigger     CreateChildTask(6, 0C914D60, level task): its second step sends player 0 the game's request 46
 *                   (SetInputP, PL_OP SUPER), which Sonic's own request handler (8C0AFF1A / 8C0B07E4 case 46) carries
 *                   out: equipment |= 0x8000, the aura task 0C900480, the physics task 8C0B32B6 (air_resist -0.001,
 *                   air_resist_air -0.002, air_accel 0.05, center_height 6.4), mode 71, action 130. He is Super from
 *                   step 2 on (the game: step 6, after its stage init);
 * and in every later step it runs its children and then, unless SA1_SUPER_NO_DRAIN, the game's ring drain 0C917088 (the
 * Perfect Chaos boss's, SADX SuperSonic_SubtractRings) while player 0's equipment has 0x8000: one ring every 60 steps, and
 * at 0 rings the game's request 47 (PL_OP NORMAL: back to normal Sonic). The drain is a host composition of game code (the
 * game gates it by the boss's state; the counter is the stand-in's taskwk +0x0E). Left out of the stand-in: the level
 * task's pos.y >= 0 clamp of player 0 (0C900040, 0C914F40, 0C914F86), the boss, the stage's music, fog and camera.
 * Rings: his world starts with 50 (the boss's 8C08A074 + 8C08A198(50)), or start_rings with SA1_CREATE_START_RINGS.
 * Any world of level 19 built with SA1_CREATE_STAGE_TASKS gets the stand-in too, as the stage's own tasks: the sea plane
 * (stage landtable) and the trigger, no drain (the boss is not decompiled) - so a Sonic world replays the harness's
 * Super free-roam references (--super-free) bit for bit, as Super Sonic's does with SA1_SUPER_NO_DRAIN.
 * Archives: SUPERSONIC (archive 0), the common OBJ_REGULAR, EFF_REGULAR, CON_REGULAR (1-3), then Sonic's own SONIC and
 * SON_EFF (4, 5): a Super Sonic who turned back is drawn with Sonic's textures.
 *
 * SONIC TURNS SUPER IN PLAY (gsa1_rounds_plan.md 14.1a): SA1_CREATE_SUPER_CAPABLE builds a Sonic (kind 0) world the same
 * way (level 19 act 0, B_CHAOS7, the level task stand-in with the drain and the revert fix; no trigger, he starts normal).
 * sa1_character_order(SA1_ORDER_SUPER) then turns him Super in his own world through the game's request 46: position,
 * facing, speed, state (on the ground, in the air, rolling ...) and rings stay what they are, nothing is re-created; and
 * SA1_ORDER_NORMAL, or the drain at 0 rings, turns him back the same way (request 47). A kind-8 world is such a world that
 * started Super: after a revert it holds a normal Sonic, whom SA1_ORDER_SUPER turns Super again. Sonic's archives get the
 * SUPERSONIC archive after the others (archive 5) with SA1_LOAD_SUPER_FORMS, which SA1_CREATE_SUPER_CAPABLE needs (a
 * host that never turns Sonic Super loads and paints no extra archive). What level 19 changes for a normal Sonic in such
 * a world: the voice selection 8C07DCE0 (level-19 voice table D_8C1BE7A0[19], voice 396 instead of 390) and the level's
 * effect lists (D_8C1C4140[19]: the D_8C19C078 list once more). Measured (tests/super_test.c "transform"): the in-world
 * request keeps everything (a run at 2.70 units per step goes on at 2.74 as Super Sonic in the next pair, same facing,
 * same rings; back to normal Sonic the same way); re-creating the character as kind 8 at the same position and yaw with
 * its rings loses the speed (2.70 -> 0.03) and the state (the new world starts standing, Super only from its step 2).
 * Drawing a hidden pair (the CPU pose, see sa1_character_render) and the v0.1 sa1_mesh without archive streams use the
 * kind's archive 0, so the other form's body is not textured there (a General world's is: see "THE GENERAL FORM").
 *
 * THE REVERT FIX (a patch of game state, in kind-8 and super-capable worlds unless SA1_SUPER_FAITHFUL_REVERT): the physics
 * task's destructor 8C0B3240 restores the wrong fields (the air resistances it saved go into max_x_spd and run_accel, and
 * air_resist / air_resist_air keep the Super values: a reverted Sonic has max_x_spd -0.008 and run_accel -0.028; measured
 * in tests/super_test.c "drain", a run then reaches 3.07 units per step in 90 pairs instead of 3.71; in the game the sea
 * plane kills him about 100 steps later). When the physics task's exec 8C0B3280 frees the task
 * (player 0's equipment lost 0x8000: the drain at 0 rings, SA1_ORDER_NORMAL, a death), libsa1 puts max_x_spd and
 * run_accel back as they were right before the destructor ran and air_resist / air_resist_air from the task's own saved
 * values (awp +4 / +8), so the speed-up tasks 8C07D2A2 / 8C0B2EA0, which save and restore max_x_spd too, keep working.
 *
 * DRAWS: for kinds 0 and 8 the body's texlist is chosen by Sonic's display rule (8C0AE17A: reqaction - action while
 * mtnmode is 2 - in 130..141: SUPERSONIC 0x0C901EA0, else SONIC), so the body of either form is not SA1_DRAW_EFFECT; the
 * aura (additive SRC_ALPHA / ONE, double-sided, drawn under a mirroring matrix) and the Super effect clones are effects of
 * the SUPERSONIC archive. The aura's whoosh sounds are SA1_EVENT_SOUND 1035 (speed above 7) and 1036 (above 13); they are
 * tones of the Perfect Chaos stage bank (CHAOS7B.MLT, sa1_audio_set_stage 19).
 *
 * SA1_CREATE_WATER_FLOOR (any kind; GRID worlds): the window copy of every GRID water object (COL & 0x400002: WATER and
 * HEAVY_WATER surfaces, which keep only their upward faces) gets the solid bit instead of its water bits, so the
 * character stands and runs on host water; the water above him (W1) is not added and the host's water flags
 * (sa1_input.host_flags, WTR-A) do not take such water out: he runs on it with KNOWN | IN_WATER and with KNOWN alone.
 * The static collision stays the context's (other worlds keep the water). DISC / FAITHFUL worlds and moving objects are
 * unchanged. The reference worlds never set it.
 */
#define SA1_API_SUPER 1

/* sa1_character_desc.flags (API 0.4) */
#define SA1_CREATE_SUPER          0x400u  /* super_flags is given (struct_size must cover it); else the defaults: the drain
                                             on, back to normal Sonic at 0 rings, the revert fix, the sea plane in worlds
                                             with the stage's landtable */
#define SA1_CREATE_SUPER_CAPABLE  0x800u  /* Sonic (kind 0): a world in which he can turn Super in play (see above; Sonic
                                             loaded with SA1_LOAD_SUPER_FORMS, else SA1_ERR_NOT_LOADED); kind 8 always is
                                             one; any other kind: SA1_ERR_INVALID_ARGUMENT. Level 19 act 0 unless
                                             super_flags has SA1_SUPER_GENERAL (the host's stage, default level 1) */
#define SA1_CREATE_WATER_FLOOR    0x1000u /* GRID worlds: host water is a solid floor for this character (see above) */

/* sa1_character_desc.super_flags (with SA1_CREATE_SUPER; kind 8 and super-capable Sonic worlds) */
#define SA1_SUPER_NO_DRAIN        0x1u    /* no ring drain (0C917088): Super until the host orders otherwise */
#define SA1_SUPER_KEEP_AT_ZERO    0x2u    /* the drain stops at 0 rings, so the game code never sees 0: he stays Super (the
                                             game's own end is the revert, and then on the sea plane the death) */
#define SA1_SUPER_FAITHFUL_REVERT 0x4u    /* keep the game's revert bug (no revert fix) */
#define SA1_SUPER_SEA_PLANE       0x8u    /* the sea plane (worlds with the stage's landtable only) */
/* round 0.9.3a (SA1_API_SUPER_GENERAL): see "THE GENERAL FORM" below */
#define SA1_SUPER_GENERAL         0x10u   /* kind 0 with SA1_CREATE_SUPER_CAPABLE: the General form; kind 8:
                                             SA1_ERR_INVALID_ARGUMENT */
#define SA1_SUPER_START           0x20u   /* with SA1_SUPER_GENERAL: Super right after the world's first pair (the first
                                             pair shows normal Sonic, as kind 8's first pairs do); no voice. Without
                                             SA1_SUPER_GENERAL: SA1_ERR_INVALID_ARGUMENT */
#define SA1_SUPER_WATER_WHILE_SUPER 0x40u /* GRID worlds, any super-capable world: SA1_CREATE_WATER_FLOOR's floor copy of
                                             host water only while player 0 is Super and only for a water object whose top
                                             is at most SA1_SUPER_WATER_TOLERANCE x his collision radius above his
                                             position (water above that stays water, W1 included: a transformation deep
                                             in water keeps him swimming); the window follows the form at every step */
#define SA1_SUPER_START_NORMAL    0x80u   /* kind 8 only (review RC1): the world starts with normal Sonic - the game's
                                             trigger is left out (a re-created Vs. Perfect Chaos Super Sonic who had
                                             turned back); SA1_ORDER_SUPER turns him Super as after a revert. Any other
                                             kind: SA1_ERR_INVALID_ARGUMENT */
#define SA1_SUPER_KNOWN_FLAGS     0xFFu
#define SA1_SUPER_WATER_TOLERANCE 1.0f    /* SA1_SUPER_WATER_WHILE_SUPER: the floor rule's reach above his position, in
                                             his collision radii (playerwk.p.height / 2: 5 SA1 units for Sonic) */

/* In-play orders: the game's player request (SetInputP(0, order): the game's PL_OP) given to player 0 of the character's
 * world now (outside ticks). The game takes it at the character's next step in any state whose request handler takes it
 * (almost every state, super_sonic.md 1.2; a state that takes none keeps it pending).
 *   SA1_ORDER_SUPER   request 46: turn Super (a super-capable world: kind 8, or kind 0 with SA1_CREATE_SUPER_CAPABLE);
 *                     SA1_OK without a request while he is Super
 *   SA1_ORDER_NORMAL  request 47: back to normal Sonic (the same worlds); SA1_OK without a request while he is not Super
 * The ring rule (later games: 50 rings and a button) is the host's. A character without a world yet gets it built (as
 * sa1_character_add_rings does). Returns SA1_ERR_NOT_FOUND (no such character), SA1_ERR_UNSUPPORTED (not a super-capable
 * world, or another order), a world creation error, the faulted world's status. */
#define SA1_ORDER_SUPER   46
#define SA1_ORDER_NORMAL  47
SA1_API sa1_status sa1_character_order(sa1_context *ctx, sa1_character_id id, int32_t order);

/*
 * THE GENERAL FORM (round 0.9.3a, SA1_API_SUPER_GENERAL; sadc docs/research/super_general_design.md 4 / 5 with its Review
 * R.4): a Super Sonic who plays as Sonic. A Sonic (kind 0) created with SA1_CREATE_SUPER_CAPABLE and super_flags
 * SA1_SUPER_GENERAL:
 *   - the world is Sonic's own on the host's stage (SA1_CREATE_STAGE's level / act, default level 1 act 0; never forced
 *     to level 19), so normal Sonic keeps his own idle voices (390 / 227 on level 1). Its image holds B_CHAOS7 (Sonic's
 *     optional overlay: the Super models, textures and code); host collision (GRID, FAITHFUL or none: DISC collision of
 *     another stage than 19 and SA1_CREATE_STAGE_TASKS are SA1_ERR_INVALID_ARGUMENT, the stage overlay and B_CHAOS7 share
 *     0x0C900000). The Perfect Chaos level-task stand-in is the world's level task (created directly,
 *     without the level's tables): it runs the drain. Level 19 itself is allowed (SonicMain's mode 0 then sets the Super
 *     welds up itself);
 *   - right after the world's first pair libsa1 sets up the 12 Super Sonic welds with the game's own PJoinVertexes
 *     (jvmode 0 on a list of its own: SonicMain's mode 0 switches them on only on stage 0x13xx) and the Super hand / foot /
 *     head points, and keeps them in Sonic's weld list for the world's life (34 + 5 entries);
 *   - SA1_ORDER_SUPER turns him Super at once (between pairs; before the first pair the order waits for it) by the body
 *     of the game's request 46 without its mode change: equipment |= 0x8000, the aura task 0C900480 (unless
 *     SA1_SUPOPT_NO_AURA), the physics task 8C0B32B6 (then SA1_SUPOPT_SPEED's set and Sonic's center_height), and the
 *     look: Sonic's actions drawn
 *     with the Super body and ball (as the game's own actions 135-141 pair them: a copy of SonicActionList) and the
 *     SUPERSONIC textures in texlist_SONIC, plus SA1_SUPOPT_VOICE's tone. He stays in Sonic's mode machine: spin dash,
 *     homing attack (the game's Super homing), Light Speed Dash with the upgrades, carrying, springs; the bit's rules apply
 *     (a hit only halves his speed; attack / defence 3). Position, speed, state, rings and upgrades are kept;
 *   - SA1_ORDER_NORMAL, the drain at 0 rings (in the step in which the game's drain 0C917088 would send request 47, which
 *     such a world never sends: the game drops it in Sonic's normal modes) and any clear of the bit by the game turn him
 *     back: the bit cleared, the look restored, the aura and physics tasks free themselves, the physics exactly Sonic's
 *     (the revert fix). An order while he is in that form already is SA1_OK and does nothing;
 *   - after each pair the look follows the bit, and while it is on, a drawn action whose object is neither the Super
 *     body nor the Super ball (an action of another overlay: adventure fields, carts, the board) is drawn with Sonic's
 *     textures for that pair (an SA1_LOG_WARN, once per such stretch). A General world whose player 0 reaches the game's
 *     Super modes 71-78 (only the scripted requests 9, 10, 12-17, 19-23 lead there, through mode 20's Super handler; no
 *     libsa1 path sends them) logs one SA1_LOG_WARN;
 *   - the CPU pose of a hidden pair draws the Super body / ball with the SUPERSONIC textures (its draws name that
 *     archive; a v0.1 sa1_mesh without archive streams cannot) while the look is on; sa1_geometry.object_addr names the
 *     action table's object (Sonic's body) all the same.
 * Effects: Sonic's normal modes spawn Sonic's own blue trail, ball after-images and spin dash dust; with
 * SA1_SUPOPT_SUPER_EFFECTS the game's Super clones of the first two (0C9004F2, 0C9004B6, the Super modes' own) run instead
 * while he is Super, and the dust takes the Super after-images' golden textures (the game has no golden dust: libsa1
 * borrows those textures in the dust's own set-up; review R.5). The Light Speed charge's sparkles, streaks
 * and glow have no golden version in the game: SA1_SUPOPT_LSD_GOLD recolours them at presentation (SA1_DRAW_SUPER_GOLD;
 * the Crystal Ring's green sparkles and streaks keep the game's colours: owner answer O3, super2_plan.md 10.1).
 * SA1_SUPOPT_SUPER_ANIMS draws the game's own Super Sonic motions (actions 130-134) for standing and moving.
 * The reference worlds never set any of this.
 *
 * In request-based worlds (kind 8, a 0.9.3-style super-capable Sonic), SA1_SUPOPT_VOICE's tone plays when the game
 * takes the host's SA1_ORDER_SUPER (after the pair in which the bit got set), once per transformation however often the
 * host sent the order (a pending request 46 is not sent again); never for the game's own trigger.
 */
#define SA1_API_SUPER_GENERAL 1

/* Super options (round 0.9.3a): applied to the character's world at once (between ticks) and kept for a world built later
 * (as the game options). SA1_ERR_NOT_FOUND unknown id; SA1_ERR_INVALID_ARGUMENT an unknown option or value;
 * SA1_ERR_UNSUPPORTED an option the character's world cannot take (noted per option); a faulted world's status. */
#define SA1_SUPOPT_SPEED          1u    /* General worlds: SA1_SUPER_SPEED_* (default GAME): the physics task's three values
                                           after its first exec, computed from its own saves of Sonic's values (awp +4 /
                                           +8 / +0xC), so the revert stays exact; applied at once while Super */
#define SA1_SUPOPT_NO_AURA        2u    /* General worlds: 1 = no aura task (freed at once while Super; 0 creates it again
                                           while Super). Kind 8: SA1_ERR_UNSUPPORTED (his aura stays) */
#define SA1_SUPOPT_VOICE          3u    /* any super-capable world: a tone (1..2047) played with the game's sound call
                                           8C0940DA(tone, 0, 0, 0) when the host's SA1_ORDER_SUPER is carried out (General:
                                           with the transformation; kind 8 / 0.9.3-style: when the game takes the
                                           request); 0 = none (the default: the game's own trigger plays none) */
#define SA1_SUPOPT_SUPER_EFFECTS  4u    /* General worlds (review RC14): bits SA1_SUPER_EFFECTS_*, 0..3 (4 and up:
                                           SA1_ERR_INVALID_ARGUMENT). SA1_SUPER_EFFECTS_CLONES: while he is Super, the
                                           first execs of Sonic's trail 8C0B48B4 and ball after-images 8C0B3FC0 run the
                                           game's Super clones 0C9004F2 / 0C9004B6 instead (the same creators: im 2, lists
                                           6 / 5, counter.b[0] the player); SA1_SUPER_EFFECTS_DUST: below; 0 = Sonic's own
                                           (the default) */
#define SA1_SUPER_SPEED_GAME  0         /* air_resist -0.001, air_resist_air -0.002, air_accel 0.05 (the game's Super) */
#define SA1_SUPER_SPEED_RUN   1         /* -0.001 and Sonic's air_resist_air / air_accel: faster on the ground, Sonic's
                                           jumps. The run keeps gaining: equal to Sonic's for the first ~60 pairs, 5.90
                                           units per step at pair 150, 9.38 at 300, 11.49 at 450 (Sonic levels off at
                                           4.46) */
#define SA1_SUPER_SPEED_HALF  2         /* the midpoint of Sonic's and the game's Super value of each of the three */
#define SA1_SUPER_SPEED_SONIC 3         /* Sonic's three values. Every set has Sonic's center_height 5.4 (not the task's 6.4,
                                           which suits the game's own Super motions: Sonic's motions on the Super body at
                                           6.4 stand 1 unit above the ground, measured), restored by the task's own
                                           destructor */
/* round 0.9.4a (SA1_API_SUPER_POLISH; sadc docs/research/super2_plan.md 5.3 with its Review and 10.1) */
#define SA1_API_SUPER_POLISH 1
#define SA1_SUPER_EFFECTS_CLONES  1     /* the trail / ball after-image clones 0C9004F2 / 0C9004B6 (0.9.3a's value 1) */
#define SA1_SUPER_EFFECTS_DUST    2     /* the spin dash dust (8C0B3FA6, set up by 8C0B4040: the SA1_SUPERDUST block of sadc
                                           src/1st_read/code_8C0B4040.c) draws the Super after-images' ss_ball_a / _b
                                           (B_CHAOS7's texlists 0CA6B2D0 / 0CA6B2D8, the ones the Super clone 0C91DB60 sets)
                                           while he is Super; the game's random choice between the two is unchanged. A
                                           build without the block: SA1_ERR_NOT_IMPLEMENTED */
#define SA1_SUPOPT_SUPER_ANIMS    5u    /* General worlds: 1 = the game's own Super Sonic motions for standing and moving:
                                           Sonic's stand, alternate-stand and slope-stand actions (0, 1, 2, 7, 8, 74) draw
                                           action 130 (Stationary), his walk / run actions 9-13 draw 131-134 by the game's
                                           own speed bands (8C0AE6D8's jog / run / rush, the bands of Sonic's own selector),
                                           the four Super motions' root lifted by the physics task's center_height minus
                                           Sonic's (6.4 - 5.4, the placement the game gives them); every other action keeps
                                           Sonic's motion on the Super body - also his idle after standing still (4 -> 5 ->
                                           6, the foot tap: owner answer O1, super2_plan.md 10.1). Motion data only:
                                           position, speed, mode and rings are the same as with 0; the motion chains' timing
                                           and with it the game's rand stream (the bubble task 8C0B3B1A bursts on motion
                                           wraps) are not. The CPU pose of a hidden pair and the render interpolation
                                           follow. 0 = Sonic's motions (0.9.3a; the default). Kind 8 and a Sonic that is not
                                           General: SA1_ERR_UNSUPPORTED */
#define SA1_SUPOPT_LSD_GOLD       6u    /* General worlds: 1 = while the General look is on, the Light Speed charge's
                                           sparkles (8C0B58E0) and streaks (8C0B504A) of the plain charge (lcharge02 /
                                           lcharge01) are marked SA1_DRAW_SUPER_GOLD (draw them with the texture's
                                           SA1_TEXTURE_VARIANT_SUPER_GOLD) and the charged glow (8C0B4F00) is emitted in the
                                           complementary colours of the game's (max + min - c per channel: gold, the game's
                                           pulse kept). The Crystal Ring's green sparkles and streaks (lcharge04 / lcharge03)
                                           keep the game's colours (owner answer O3, super2_plan.md 10.1). The game has these
                                           effects only in Sonic's blue (green with the Crystal Ring); presentation only, no
                                           game state changes. 0 = the game's colours (the default). Kind 8 and a Sonic that
                                           is not General: SA1_ERR_UNSUPPORTED */
SA1_API sa1_status sa1_character_set_super_option(sa1_context *ctx, sa1_character_id id, uint32_t option, int32_t value);

/* ---- SET objects (SA1_API_SET, round 0.9.4; sadc docs/research/gsa1_094_plan.md 6.3 with its Review, v1_arch.md 5.1,
 *      gsa1_090_rings.md 4.1, gsa1_094_items.md) ------------------------------------------------------------------------ */
/*
 * SA1_API_SET tells a host at compile time that this header has the SET objects: the game's own objects (rings, ring
 * lines and circles, the item boxes), placed by the host and run by the game's own code in every character's world.
 *
 * THE CATALOGUE (sa1_object_count, sa1_object_info_get; LIB gen/objtypes.tsv): libsa1's own type ids, stable across
 * versions (never a stage's object-list index): SA1_OBJTYPE_RING 1, SA1_OBJTYPE_O_GRING 2 (a ring line or a ring circle),
 * SA1_OBJTYPE_ITEMBOX 3 (the item box standing on the ground), SA1_OBJTYPE_ITEMBOX_AIR 4 (the floating one). Types are
 * 1 .. sa1_object_count. available is 0 for a type whose code is not in this build (sa1_set_add refuses it).
 *
 * RECORDS (sa1_set_add / update / remove; host space, kept by the context, at most SA1_SET_MAX_RECORDS). params are the
 * SET entry's scl as the game reads it; rotation is a host rotation matrix (row-major, column vectors: local -> host),
 * all zero = none:
 *   RING         params (0, 0, 0) (-1 in params[0] is the game's ground-following ring, which re-probes the ground when
 *                the camera is near); only the yaw of the rotation counts
 *   O_GRING      params[0] = n, an integer 0..15; params[2] 0: a line of n + 1 rings params[1] units apart (SA1 units),
 *                1: a circle of n rings, radius params[1]. Rotation: full (the game's njRotateZ, X, Y). The record's user
 *                bits are its collected rings (bit i = ring i, 8 bits: a circle's ring 8 has no bit and comes back when
 *                the group is re-made - the game's own quirk; lines of up to 8 rings and circles of up to 7 are exact)
 *   ITEMBOX      params[0] = the contents, an integer 0..8 (speed shoes, invincibility, 5 rings, 10 rings, random rings,
 *                barrier, extra life, bomb, magnetic barrier), params[1] the item panel's start angle (degrees); the
 *                position is the box's base on the ground; no rotation (the game draws it upright)
 *   ITEMBOX_AIR  params[0] = the contents 0..8; rotation: full
 * Anything else is SA1_ERR_INVALID_ARGUMENT. kinds: bit (1u << SA1_CHAR_*) per character kind whose worlds run the record,
 * 0 = every kind. flags must be 0 in this version. user is the host's tag, returned in events. A new record is present,
 * not broken, user bits 0.
 *
 * IN EACH WORLD (character worlds of the context; a world without records runs exactly as before: no task, no write):
 *   - the game's SET manager task 8C090FA0 is created at level 8 (list 7: CreateElementalTask(2, 8, ...)), already in
 *     mode 1 (libsa1 does mode 0's work: its object list B_8C79A3CC = a generated list of the four types in the world's
 *     segment 0x18, the act-change flag 0), when the context first has records; each step the game's spawner 8C091380
 *     makes the task of every present record within the range of the character (400 SA1 units, or
 *     sa1_set_config.range) (of the camera taskwk while Big's lure is in the water) - the game's own
 *     CreateElementalTask with the type's exec, its OBJ_CONDITION in the game's
 *     array B_8C79A3E0 and a 0x20-byte SET entry;
 *   - THE WINDOW, at every pair start: the records within (range + margin) SA1 units of the character, nearest first by
 *     (squared distance, id) (above 400: the order of sa1_set_config.range; round 0.9.4b: a ring line or circle is as
 *     far as its nearest ring, so the part of a big circle made of several records that lies nearest the character is
 *     the part that runs), within two budgets: ring tasks (a RING 1, a line n + 1, a circle n; ring_window) and boxes
 *     (box_window); a record that left the window keeps its units while its task still runs (see the touch below), so
 *     the live tasks never exceed the windows. A record outside the window has no condition slot; one that leaves it
 *     has its task freed (FreeTask: silent, as the game's own range-out would; the game's range-out 8C0919CC stays off)
 *     and its slot released once no task points at it. Refusals: SA1_EVENT_CAPACITY SA1_CAPACITY_SET_WINDOW (arg =
 *     records left out; once per episode). A spawn needs a free block of 32 KB in the game heap (CCL_Init never
 *     returns from a failed CAlloc): below that the world holds its spawns and reports SA1_EVENT_CAPACITY
 *     SA1_CAPACITY_SET_HEAP (arg = the largest block, once per episode);
 *   - slots are libsa1's own map; a record entering the window gets its slot written from the context's state (present:
 *     ssCondition 0x8000, broken: | 0x0004, scUserFlag = its user bits);
 *   - libsa1 never frees a record's task between a touch and the game's own end of it (a RING or ring in its sparkle,
 *     an air box in modes 2-4, a ground box breaking): a leave, an update or a forced state waits for the game to write
 *     the condition, so the rings / item are given once and the event is raised;
 *   - a world built later (a character re-created) runs every record with the context's state from its first pair.
 *
 * THE SHARED STATE (sa1_set_state; the host's, e.g. a server's): applied in every world of the context, without sound,
 * rings, item or event:
 *   PRESENT 0   (any type) the condition's bit 15 cleared and a live task freed: the object is gone
 *   PRESENT 1   bit 15 set again once no task points at the slot: the spawner makes it again
 *   BROKEN 1    (ITEMBOX) 0x0004 set and a live intact box freed: the spawner makes the broken box (its base, no debris,
 *               no item, no icon, no sound)
 *   BROKEN 0    (ITEMBOX) 0x0004 cleared, bit 15 from PRESENT, the broken box freed: an intact box grows in (46 steps
 *               without collision)
 *   user bits   (O_GRING) bits the world's group lacks are added and the group re-made without those rings (deferred
 *               while one of its rings sparkles; the world's own bits coming back change nothing)
 * A ground box's broken state is BROKEN, not PRESENT 0: the box stays as its broken base (the game never clears its bit
 * 15). State changes are applied at a world's next pair start (a ground box breaking at that moment: one step later).
 * Every call is applied again, also with the values the record has: a host that restores an object one world took
 * (a server that refused the collection) brings it back there; where it is so already nothing changes.
 *
 * EVENTS (sa1_poll_set_events; per world, 256 queued, older ones dropped):
 *   SPAWNED     the record's task appeared in the world (the spawner made it)
 *   DESPAWNED   it ended (collected, window, forced state, removal)
 *   GONE        the game ended the object for good: its task carries the game's end mark (8C091BA4: exec = DestroyTask)
 *               with bit 15 clear and libsa1 did not free it; once per task (a collected ring; an air box 22 steps after
 *               it broke). E-102's firing clears bit 15 of every box he has locked on before his shot arrives: that alone
 *               raises nothing
 *   USERBITS    a group's collected rings changed (user_bits = the world's bits)
 *   BROKEN      a ground box broke (0x0004 set by the game)
 * Each event: the raw condition word, the type's exec, the world's step, and THE TOUCH that caused it: the last collision
 * hit of the object before its code took it - cause BODY (the character's own body; attacker = the character's position),
 * PROJECTILE (a character's projectile: E-102's laser, Big's lure, Sonic's sparkles; attacker = the projectile's
 * position), MAGNET (a ring the magnetic barrier pulled: the touch is the character's position, attacker the ring's own,
 * which may be far from its place), UNKNOWN (no hit seen). position = the character's position at the touch (UNKNOWN: the
 * object's place); touch_step its step.
 *
 * The reference worlds never have records. A range above 400 and a view_tan are host options; the reference worlds never
 * set them. The camera taskwk of every live world holds the world's virtual eye (the view
 * the game's draws are tested against): the spawner centres on it while Big's lure is in the water.
 */
#define SA1_API_SET 1

#define SA1_SET_MAX_RECORDS 16384u      /* per context (SA1_ERR_LIMIT beyond) */

/* object types (gen/objtypes.tsv) */
#define SA1_OBJTYPE_RING        1u      /* "RING", objexec_RING 8C0869E8 */
#define SA1_OBJTYPE_O_GRING     2u      /* "O GRING", 8C087B80: a ring line or circle */
#define SA1_OBJTYPE_ITEMBOX     3u      /* "O ITEMBOX", objexec_O_ITEMBOX 8C05D10C */
#define SA1_OBJTYPE_ITEMBOX_AIR 4u      /* "O ItemBoxAir", objexec_O_ItemBoxAir 8C05D9F6 */

/* sa1_object_info.cls (v1_arch.md 2.2) */
#define SA1_OBJCLASS_STATIC     1u
#define SA1_OBJCLASS_CONSUMABLE 2u      /* collected / broken once (the four types of this version) */
#define SA1_OBJCLASS_PERSONAL   3u
#define SA1_OBJCLASS_TOGGLE     4u
#define SA1_OBJCLASS_ACTOR      5u
#define SA1_OBJCLASS_ARENA      6u

/* sa1_object_info.rotation_order: how the type's code applies the SET entry's angles */
#define SA1_SET_ROT_NONE 0u             /* not used (the ground box is drawn upright) */
#define SA1_SET_ROT_Y    1u             /* the yaw only */
#define SA1_SET_ROT_ZXY  2u             /* njRotateZ, njRotateX, njRotateY (local vectors turn about Y, then X, then Z) */

/* sa1_object_info.budget */
#define SA1_SET_BUDGET_RING 1u          /* counted in ring tasks (sa1_set_config.ring_window) */
#define SA1_SET_BUDGET_BOX  2u          /* counted in boxes (box_window) */

/* the state word (sa1_set_state, sa1_object_info.states) */
#define SA1_SET_STATE_PRESENT 0x1u      /* the condition's bit 15: the object exists */
#define SA1_SET_STATE_BROKEN  0x2u      /* the condition's 0x0004: used up, stays in place (the ground box's broken base) */

typedef struct sa1_object_info {
    uint32_t    struct_size;
    uint32_t    type;               /* SA1_OBJTYPE_* */
    const char *name;               /* the game's list name ("RING", "O ITEMBOX") */
    uint32_t    exec;               /* DC address of the exec */
    uint8_t     im, level;          /* the list entry: task works, task level */
    uint16_t    list_flags;         /* the list entry's attribute (0: the spawner's default range) */
    float       range;              /* host units: the spawner's range of the context (400 SA1 units, or sa1_set_config.range) */
    uint32_t    cls;                /* SA1_OBJCLASS_* */
    uint32_t    model;              /* guest address of the type's NJS_OBJECT (0 = none) */
    uint32_t    rotation_order;     /* SA1_SET_ROT_* */
    uint32_t    available;          /* 1: the type's code is in this build */
    uint32_t    states;             /* the SA1_SET_STATE_* bits the type uses */
    uint32_t    budget;             /* SA1_SET_BUDGET_* */
} sa1_object_info;
SA1_API uint32_t   sa1_object_count(const sa1_context *ctx);
/* SA1_ERR_NOT_FOUND for an unknown type */
SA1_API sa1_status sa1_object_info_get(const sa1_context *ctx, uint32_t type, sa1_object_info *out);

typedef struct sa1_set_record {
    uint32_t struct_size;
    uint32_t type;                  /* SA1_OBJTYPE_* */
    uint32_t flags;                 /* 0 (reserved) */
    float    position[3];           /* host space */
    float    rotation[9];           /* host rotation matrix (row-major; all zero: none) */
    float    params[3];             /* the SET entry's scl as the game reads it (see above) */
    uint32_t kinds;                 /* 1u << SA1_CHAR_* per kind; 0 = all */
    uint64_t user;                  /* the host's tag, returned in events */
} sa1_set_record;

/* sa1_set_add: *out_id = the record's id (never 0). SA1_ERR_INVALID_ARGUMENT (a bad record), SA1_ERR_UNSUPPORTED (a type
 * not available in this build, flags), SA1_ERR_LIMIT (SA1_SET_MAX_RECORDS). sa1_set_update: a new place / parameters /
 * kinds (the type may change too); a live task is freed and made again at the new place (the state is kept).
 * sa1_set_remove: silent (the task freed without an event of the game). SA1_ERR_NOT_FOUND for an unknown id. */
SA1_API sa1_status sa1_set_add(sa1_context *ctx, const sa1_set_record *r, uint32_t *out_id);
SA1_API sa1_status sa1_set_update(sa1_context *ctx, uint32_t id, const sa1_set_record *r);
SA1_API sa1_status sa1_set_remove(sa1_context *ctx, uint32_t id);
/* The shared state (see above): state = SA1_SET_STATE_* (BROKEN only for SA1_OBJTYPE_ITEMBOX), user_bits only for
 * SA1_OBJTYPE_O_GRING (0..0xFF; else 0). SA1_ERR_NOT_FOUND, SA1_ERR_INVALID_ARGUMENT. */
SA1_API sa1_status sa1_set_state(sa1_context *ctx, uint32_t id, uint32_t state, uint32_t user_bits);

#define SA1_SETLIST_LIBSA1 0u           /* the generated object list (the only list of this version) */
/* The windows' maxima (round 0.9.4b; sadc docs/research/objects2_range.md 1.3 F): the ceiling is the game's own collision
 * lists - CCL list 7 takes 256 entries per step (the rings; a hurt scatter's 20 and a dig fountain's 32 come on top) and list 2
 * 128 (the boxes). Beyond them CCL_Entry silently skips the OLDEST tasks (CreateElementalTask puts new tasks at the head of
 * their list), whose rings then cannot be collected (measured: 300 live ring tasks - none of the 50 nearest taken; 250 - taken
 * normally). The capture holds RT_CAP_MAX records per pair (a ring 1, 2 with its blob shadow; a box about 4.1): 2,048 since
 * round 0.9.4b (512 before). Measured (tests/set_test.c r5, review R.4): 200 ring tasks and 100 boxes in the virtual camera's
 * view with the rings' blob shadows, a hurt scatter and a dig fountain take up to 1,125 records - at 1,024 that scene dropped
 * 101 records and the HUD, at 2,048 none (925 without the shadows); r4: CCL list 7 at most 225 entries a step and list 2 100
 * at these maxima (scatter and fountain included); set_test (10): the HUD now holds with 400 ring tasks in view (the search's
 * bound) and with 100 boxes beside 64 rings. */
#define SA1_SET_RING_WINDOW_MAX 200u
#define SA1_SET_BOX_WINDOW_MAX  100u
typedef struct sa1_set_config {
    uint32_t struct_size;
    uint32_t ring_window;           /* ring tasks per world, 1..SA1_SET_RING_WINDOW_MAX (default 64) */
    uint32_t box_window;            /* item boxes per world, 1..SA1_SET_BOX_WINDOW_MAX (default 32) */
    float    margin;                /* SA1 units beyond the spawner's range, 0..1000 (default 100) */
    uint32_t list;                  /* SA1_SETLIST_LIBSA1 */
    /* round 0.9.4b (SA1_API_SET_RANGE): read when struct_size covers them; 0 = the game's */
    float    range;                 /* SA1 units: how far from the character (from the camera taskwk while Big's lure is in
                                       the water) the game's spawner makes the objects. 0 or SA1_SET_RANGE_MIN: the game's 400
                                       (the generated list's attribute 0; 0.9.4's behaviour exactly). Above it, up to
                                       SA1_SET_RANGE_MAX: every entry of the generated list gets the game's own per-type range,
                                       attribute bit 0 and fRange = range * range, which the spawner 8C091380 reads (the disc's
                                       own lists use it 583 times; Twinkle Circuit gives these four types 2,000); the window's
                                       reach becomes range + margin and its order: the records within 400 units first (nearest
                                       first), then those in front of the plane through the virtual eye perpendicular to the
                                       camera's yaw (nearest first), then the rest; a record keeps tier 1 or 2 for
                                       SA1_SET_TIER_HOLD pairs after it changes between them (one that comes within 400
                                       units is first at once: review R.12). Collection, breaking and every reach of the
                                       game are unchanged (none reaches beyond 400). Applied at a world's next pair start */
    float    view_tan;              /* the object displays' horizontal half view as tan(half angle). 0: the game's 640 x 480
                                       screen (tan 35.0 degrees = 0.700). Above it, up to SA1_SET_VIEW_TAN_MAX: the sphere
                                       test 8C038D00 widens its two sideways tests to that half view while the exec of one of
                                       the four SET types runs (RING; O_GRING, which runs its rings; both item boxes), so
                                       objects at the sides of a wide host screen are drawn. Up / down, depth and the eye stay
                                       the game's (the world's virtual eye); only their drawing changes (their displays'
                                       visible paths only draw); every other caller - the characters' own displays, whose
                                       visible paths run logic for Tails and Knuckles - keeps the game's screen */
} sa1_set_config;
#define SA1_API_SET_RANGE 1
#define SA1_SET_RANGE_MIN    400.0f     /* the game's default range (attribute 0) */
#define SA1_SET_RANGE_MAX    3000.0f    /* the world's far clip B_8C88F614 (owner answer O1, objects2_plan.md 11.0: the host's
                                           slider goes to 12,000 HU); the game's own largest range for these four types is
                                           2,000 (MINICART's list). Beyond about 2,980 the far test hides what spawns at
                                           the edge ahead of the virtual eye; the window maxima still bound what is live */
#define SA1_SET_VIEW_TAN_MAX 4.0f       /* about +-76 degrees */
#define SA1_SET_TIER_HOLD    30u        /* pairs a record keeps window tier 1 or 2 (never delays tier 0: review R.12) */
/* the context's window (every world, at its next pair start); SA1_ERR_INVALID_ARGUMENT outside the ranges: ring_window /
 * box_window 0 or above the maxima, margin outside 0..1000, range outside 0 and SA1_SET_RANGE_MIN..MAX, view_tan outside 0
 * and 0.700..SA1_SET_VIEW_TAN_MAX (a struct_size that ends before range / view_tan leaves them as they are) */
SA1_API sa1_status sa1_set_configure(sa1_context *ctx, const sa1_set_config *cfg);

/* review R.6: which records a character's world runs now. A host must not rebuild this from the events: a world queues 256
 * (the oldest dropped) and a destroyed or re-made world raises no DESPAWNED. */
#define SA1_API_SET_LIVE 1
/* the ids of the records with a live task in character id's world after its last pair (a ring group: its group task),
 * ascending; *count = how many (SA1_ERR_BUFFER_TOO_SMALL with *count when capacity is smaller; records may be NULL with
 * capacity 0). SA1_ERR_NOT_FOUND unknown character; 0 records for a world without SET state, a puppet, SA1_NO_WORLD */
SA1_API sa1_status sa1_set_live(sa1_context *ctx, sa1_character_id id, uint32_t *records, uint32_t capacity,
                                uint32_t *count);

/* SA1_EVENT_CAPACITY ids of the SET layer */
#define SA1_CAPACITY_SET_WINDOW 10u     /* records in the window's reach left out: a budget is full (arg = how many) */
#define SA1_CAPACITY_SET_HEAP   11u     /* spawns held: the game heap's largest free block is below 32 KB (arg = it) */

#define SA1_SET_EVENT_SPAWNED   1u
#define SA1_SET_EVENT_DESPAWNED 2u
#define SA1_SET_EVENT_GONE      3u
#define SA1_SET_EVENT_USERBITS  4u
/* 5 (TOGGLE) and 6 (PASSED) are reserved for 1.0 */
#define SA1_SET_EVENT_BROKEN    7u

#define SA1_SET_CAUSE_UNKNOWN    0u
#define SA1_SET_CAUSE_BODY       1u     /* the character's own collision (CCL id 0) */
#define SA1_SET_CAUSE_PROJECTILE 2u     /* one of the character's projectiles (CCL id 1) */
#define SA1_SET_CAUSE_MAGNET     3u     /* a ring the magnetic barrier pulled (8C08789A) */

typedef struct sa1_set_event {
    sa1_character_id character;     /* the world */
    uint32_t type;                  /* SA1_SET_EVENT_* */
    uint32_t record;                /* the record's id */
    uint64_t user;                  /* its tag */
    uint32_t condition;             /* the raw ssCondition (16 bits) after the step */
    uint32_t user_bits;             /* scUserFlag (8 bits) */
    uint32_t exec;                  /* the type's exec */
    uint32_t step;                  /* the world's step of the event */
    float    position[3];           /* host space: the touch (the character at the hit), or the object's place */
    uint32_t cause;                 /* SA1_SET_CAUSE_* */
    float    attacker[3];           /* host space (see above) */
    uint32_t touch_step;            /* the world's step of the touch (0: none) */
} sa1_set_event;
/* pops up to capacity events of every character (oldest first per character) */
SA1_API sa1_status sa1_poll_set_events(sa1_context *ctx, sa1_set_event *out, uint32_t capacity, uint32_t *count);

/* ---- object previews (SA1_API_PREVIEW, round 0.9.4b; sadc docs/research/objects2_view.md 5, objects2_plan.md) -------- */
/*
 * The game's own objects without a character (a host player without one; spawn-menu pictures). The SHOWCASE of a context and
 * character kind is a private world of that kind's image with NO PLAYER and no SET manager - never in the character table, never
 * attached to the SET layer, its events dropped - that holds one live copy of each preview the host adds: the type's own task
 * made directly (its exec, an OBJ_CONDITION of its own: present, | 0x0004 for SA1_SET_STATE_BROKEN, scUserFlag = user_bits) at
 * a private slot far from every other copy (a 16 x 16 grid of pitch 2,048 SA1 units around the origin: review R.1). Its exec
 * runs under a view that looks at its slot, so the game's visibility test passes and its display draws as the game draws it:
 * the ring spins, a box turns its panel and grows in, a broken box is its base, a ring line lacks its taken rings. Nothing can
 * be collected, broken or hurt (no player, no player collision); no sound, event or capacity leaves the showcase; no character
 * world sees a copy, and SET records never reach a showcase. The kind decides what a character world of that kind would show
 * (the Extra Life panel's face, the archives of the draws: sa1_texture_get(kind, archive, texture)). Compared with a character
 * world's drawing of the same record: no blob shadows (no ground), no game culling, every copy of one description shares one
 * spin phase. The reference worlds never make a showcase.
 * Calls: on the thread that ticks the context's characters. A showcase lives while it has copies (and SA1_PREVIEW_IDLE_PAIRS
 * pairs more); sa1_unload_disc, sa1_load_disc and sa1_shutdown free every showcase (ids then SA1_ERR_NOT_FOUND).
 */
#define SA1_API_PREVIEW 1
#define SA1_PREVIEW_MAX        256u     /* copies per kind */
#define SA1_PREVIEW_IDLE_PAIRS 60u      /* a showcase without copies is freed this many pairs later */
#define SA1_PREVIEW_ROT_NONE   0u       /* drawn upright: the host applies no rotation (ITEMBOX) */
#define SA1_PREVIEW_ROT_OUTER  1u       /* the record's rotation is the host's outer matrix (RING: only its yaw, which is the
                                           ring's spin start; ITEMBOX_AIR: full, the game's Z, X, Y) */
#define SA1_PREVIEW_ROT_INNER  2u       /* the rotation is part of the description (O_GRING: the game's own ring layout) */
/* round 0.9.5 (SA1_API_BOARD; sadc docs/research/gsa1_095_plan.md 5.3, decision 9): the board model alone */
#define SA1_PREVIEW_BOARD 0x100u        /* sa1_preview_desc.type: kind 0 (params[0] = 0 MIW_B001 / 1 SB_BOARD1) or kind 2
                                           (MTX_BOARD0): the game's board model with its texture; object space with origin =
                                           a kept board's model_position and axes = its model_rotation (so a copy drawn at
                                           that pose is that board); rotation SA1_PREVIEW_ROT_OUTER, settle 0; its draws carry
                                           SA1_DRAW_BOARD. Not a SET type: sa1_set_add refuses it; 1.0's generated catalogue
                                           keeps its ids below 0x100 */

typedef struct sa1_preview_type {
    uint32_t struct_size;
    uint32_t type;                  /* SA1_OBJTYPE_* */
    uint32_t available;             /* 1: the type runs in a showcase in this build (the four SET types; measured) */
    uint32_t rotation;              /* SA1_PREVIEW_ROT_* */
    uint32_t settle_steps;          /* 60 Hz steps until a new copy looks settled (ITEMBOX: 47, its mode-0 step and its
                                       46-step grow-in; ITEMBOX_AIR: its own, measured; RING, O_GRING: 0) */
} sa1_preview_type;
/* SA1_ERR_NOT_FOUND for an unknown type */
SA1_API sa1_status sa1_preview_type_get(const sa1_context *ctx, uint32_t type, sa1_preview_type *out);

typedef struct sa1_preview_desc {
    uint32_t struct_size;
    int32_t  kind;                  /* SA1_CHAR_* whose showcase runs it (SA1_ERR_UNSUPPORTED: a kind without one) */
    uint32_t type;                  /* SA1_OBJTYPE_* with sa1_preview_type.available */
    float    rotation[9];           /* SA1_PREVIEW_ROT_INNER types only, as sa1_set_record.rotation (all zero: none); a
                                       rotation on another type: SA1_ERR_INVALID_ARGUMENT */
    float    params[3];             /* as sa1_set_record.params (ITEMBOX: params[1], the panel's start angle, included; RING:
                                       (0, 0, 0) only - the ground-following -1: SA1_ERR_UNSUPPORTED, review R.20) */
    uint32_t state;                 /* SA1_SET_STATE_PRESENT, | SA1_SET_STATE_BROKEN (ITEMBOX) */
    uint32_t user_bits;             /* O_GRING: the rings taken (as sa1_set_state) */
} sa1_preview_desc;

/* a copy; *out_id never 0. The same description twice gives two copies (the host shares them). SA1_ERR_INVALID_ARGUMENT (a
 * bad description, as sa1_set_add), SA1_ERR_UNSUPPORTED (kind, type), SA1_ERR_LIMIT (SA1_PREVIEW_MAX copies of the kind, or
 * the showcase's capture or heap would not hold one more), SA1_ERR_NOT_LOADED (no disc), SA1_ERR_GAME_FAULT (the kind's
 * showcase faulted: see sa1_preview_clear) */
SA1_API sa1_status sa1_preview_add(sa1_context *ctx, const sa1_preview_desc *d, uint32_t *out_id);
SA1_API sa1_status sa1_preview_remove(sa1_context *ctx, uint32_t id);
/* every copy; every showcase freed now (also the way out of a fault) */
SA1_API sa1_status sa1_preview_clear(sa1_context *ctx);
/* the showcases' whole pairs for dt seconds (1/30 s a pair, the character worlds' accumulator rules); never inside a character
 * world's tick. Returns SA1_OK or the first showcase's fault (the others and the character worlds keep running) */
SA1_API sa1_status sa1_preview_tick(sa1_context *ctx, float dt);
/* the copy's draws in OBJECT SPACE: host units and host axes, origin = the object's own position (where the host would put the
 * record), only this copy's draws (never merged with another copy's). alpha: 1 or more = its last pair, 0 = the pair before,
 * between: a vertex lerp when both pairs have the same draws (else the last pair), < 0 = the showcases' accumulator.
 * sa1_render.position (0, 0, 0), yaw 0, flags 0, generation = the copy's pair count. Library-owned until the next tick, render,
 * remove or clear */
SA1_API sa1_status sa1_preview_render(sa1_context *ctx, uint32_t id, float alpha, const sa1_render **out);
typedef struct sa1_preview_info {
    uint32_t struct_size;
    int32_t  kind;
    uint32_t type;
    uint32_t steps;                 /* 60 Hz steps the copy has run */
    uint32_t settled;               /* 1: steps >= its type's settle_steps */
    uint32_t draws, triangles;      /* of its last pair */
    float    bounds[6];             /* object space (host units), its last pair's draws: min x y z, max x y z (zero: none) */
} sa1_preview_info;
SA1_API sa1_status sa1_preview_info_get(const sa1_context *ctx, uint32_t id, sa1_preview_info *out);
/* The places of an O_GRING record's rings by the game's own layout (8C087EEE for a line, i = 0..n; 8C088000 for a circle,
 * i = 1..n, run in the kind's showcase on a scratch task work with the record's params and rotation): object space (host units
 * and axes, origin = the record's position), 3 floats per ring in places, the ring's user bit (1 << i) in bits (may be NULL).
 * *count = the rings; SA1_ERR_BUFFER_TOO_SMALL (with *count) when capacity is smaller. No task is made. A ring of the game's
 * group is a RING task at its place with the angle 0 (D_8C159434): a RING copy drawn there is that ring */
SA1_API sa1_status sa1_preview_ring_places(sa1_context *ctx, int32_t kind, const float params[3], const float rotation[9],
                                           float *places, uint32_t *bits, uint32_t capacity, uint32_t *count);

/* ---- faces (SA1_API_FACE, round 0.9.4a; sadc docs/research/faces_events.md, faces_wheel_plan.md 4.2-4.4) -----------------
 * The game's event faces: the EV head (a second head whose vertices the game blends between shape keys) driven by the game's
 * own face block (eventwk 0x5C-0x6B: EV_SetFace's body 8C04BBCE, the reader 8C04BC20 / 8C04BC70 / 8C04BCC8, EV_ClrFace's body
 * 8C04BD60), drawn by the face task 8C07B106 with the blend 8C07B080 / 8C055EFE. libsa1 takes the place of a cutscene's face
 * script: every call is latched and applied at the start of the character's next pair (before the executor); the last call
 * of a kind before a pair wins. A world that never gets a face call runs exactly as before (the reference worlds never do).
 * Characters: Sonic (21 keys), Tails, Knuckles, Amy (20): all 31 face animations; Big (8 keys, 6 face shapes): the 14 of
 * sa1_face_info.letters; E-102, Eggman, Tikal and Super Sonic (kind 8): SA1_ERR_UNSUPPORTED; Sonic in the General form:
 * accepted, shown only while he is not Super (the game has no Super Sonic event head). A puppet: SA1_ERR_UNSUPPORTED.
 * Which face shows: a running face animation or NOW script > lip sync > a held face > the base (EVENT: the game's idle;
 * GAME: the regular face). While any of these runs a gameplay face (a hit's 'I') waits, as in the game's events, and is
 * dropped (PFaceCancel 8C07B2DC) when it ends. Host-only writes (only in worlds that get these calls): the NOW interrupt
 * (face_next / face_count / face_script), face_seq pointing at a step buffer of the host (face animations, holds, lip sync),
 * the lip-sync writes, the face table's pointers while PACE_CUTSCENE, and PFaceCancel when a host face STARTS over a running
 * gameplay face (the game's face task would show the gameplay face to its end and run the host face unseen; review R.4).
 * Host face calls without activity (GAME mode, lip sync off) leave the game's faces exactly as without a call (review R.6). */
#define SA1_API_FACE 1

#define SA1_FACE_MODE_GAME       0u     /* the default: the regular face, with the game's gameplay faces (hurt 'I', frozen 'e') */
#define SA1_FACE_MODE_EVENT      1u     /* the EV head always, with the game's idle (8C04BC70: neutral 'A' 75 %, blink 'B'
                                           25 %, 8 steps each); leaving it: 8C04BD60 and PFaceCancel */
#define SA1_FACE_PACE_CUTSCENE 0x100u   /* mode flag (EVENT only): the game's face animations at cutscene speed - the world's
                                           face table points at copies with every step time doubled (events run one logic
                                           step per frame); the host's enter / hold frames are not scaled */
SA1_API sa1_status sa1_character_face_mode(sa1_context *ctx, sa1_character_id id, uint32_t mode);

/* The game's EV_SetFace: script = 1..64 of 'A'-'Z', 'a'-'e' (face animations 0-30, faceTbl D_8C19C32C) and '0' (back to
 * the regular face); NULL / "" = stop (EVENT: the idle; GAME: the regular face). GAME mode: a '0' is appended when missing,
 * except with SA1_FACE_LOOP (a looping script ends with the next face call and may not contain '0': SA1_ERR_INVALID_ARGUMENT;
 * review R.3). 'A' in a script is the game's idle choice (8C04BC70 takes n == 0 as the idle: 'A' 75 % / 'B' 25 %, rand()),
 * as in the game's events; a face animation (sa1_character_face_sequence) plays 'A' itself (review R.11).
 * Without SA1_FACE_NOW the game's queue: the script starts when the running animation ends. Errors (nothing applied): a
 * script byte outside 'A'-'Z', 'a'-'e', '0' or more than 64 of them SA1_ERR_INVALID_ARGUMENT; a letter the character cannot
 * show SA1_ERR_UNSUPPORTED. */
#define SA1_FACE_NOW   0x1u    /* start at the next step: the running step ends on the nearer of its two keys */
#define SA1_FACE_LOOP  0x2u    /* repeat the script until the next face call (a one-letter script holds that face) */
SA1_API sa1_status sa1_character_face(sa1_context *ctx, sa1_character_id id, const char *script, uint32_t flags);

/* A face animation made by the host: entries played in order (always NOW), each one of the game's face animations with the
 * host's timing written as the game's own step data: enter = the time of the animation's first step (the blend from the face
 * shown before into its first key; 0 = the game's own, sa1_face_info.first), the animation's other steps with the game's
 * times, then hold more steps on its last key (steps {<= 127, key}). At the end: EVENT the idle, GAME the regular face; with
 * SA1_FACE_HOLD_LAST the last key stays until the next face call. count 0 = stop. Refused as a whole (nothing applied):
 * count > SA1_FACE_SEQ_MAX, anim > 30, enter > 127, hold > SA1_FACE_HOLD_MAX, reserved fields != 0: SA1_ERR_INVALID_ARGUMENT;
 * an animation the character cannot show: SA1_ERR_UNSUPPORTED. Frames are 60 Hz logic steps. */
typedef struct sa1_face_entry {
    uint8_t  anim;              /* 0..30 = 'A'..'Z', 'a'..'e' */
    uint8_t  reserved;          /* 0 */
    uint16_t enter;             /* 0..127 steps; 0 = the game's first step time */
    uint16_t hold;              /* 0..SA1_FACE_HOLD_MAX steps on the last key */
    uint16_t reserved2;         /* 0 */
} sa1_face_entry;
#define SA1_FACE_SEQ_MAX     32u
#define SA1_FACE_HOLD_MAX  3600u
#define SA1_FACE_HOLD_LAST   0x4u
SA1_API sa1_status sa1_character_face_sequence(sa1_context *ctx, sa1_character_id id, const sa1_face_entry *entries,
                                               uint32_t count, uint32_t flags);

/* Lip sync: level 0..1 from the host (its loudness), < 0 = off; the last value before a pair counts; cheap to call every
 * frame. While on (and no face animation runs) the EV head shows, also in GAME mode; off: the mouth closes over 6 steps, then
 * a held face comes back (blended over 8 steps) or the base.
 * style: SA1_FACE_LEVEL_LADDER (round 0.9.4c: | SA1_FACE_SAME_PAIR) or SA1_FACE_LEVEL_TALK; anything else
 * SA1_ERR_INVALID_ARGUMENT. */
#define SA1_FACE_LEVEL_LADDER 0u   /* the mouth on the character's ladder (sa1_face_info.ladder) by the level: the face block's
                                      keys, face_time 64 and face_count written each pair, the reader parked on a held step;
                                      drawn by the game's blend one pair later */
#define SA1_FACE_LEVEL_TALK   1u   /* the game's talk animations C, D, E looped (NOW | LOOP) while the level is >= 0 */
SA1_API sa1_status sa1_character_face_level(sa1_context *ctx, sa1_character_id id, float level, uint32_t style);

/* Lip sync with the host's own mouth (SA1_API_FACE_MOUTH, round 0.9.4c; sadc docs/research/phoneme_plan.md 4.2-4.6): the host
 * picks the face block's two keys and the weight between them (a viseme of its phoneme detection) instead of a position on
 * the ladder. Each pair: face = key_a, face_next = key_b, face_time 64, face_count = round(weight x 64) - 1, face_script NULL,
 * face_seq = the held step {127, key_b} - the writes of SA1_FACE_LEVEL_LADDER - drawn by the game's own blend (8C07B080 /
 * 8C055EFE). It shares the level's latch: the last sa1_character_face_level or sa1_character_face_mouth call before a pair
 * wins, so a host may mix them pair by pair (no close in between). weight < 0 = off, as a level < 0 (the mouth closes over 6
 * steps, then a held face comes back or the base); weight > 1 is 1. Same priority as lip sync (a running face animation or
 * NOW script wins; talking resumes the pair after it ends). Cheap to call every frame: no allocation, no log line (errors
 * only in sa1_last_error). Errors (nothing latched): key_a or key_b >= the character's sa1_face_info.keys (Big 8: a key >= 8
 * would read wrong data, >= 16 fault the world), a NaN weight or unknown flags SA1_ERR_INVALID_ARGUMENT; a kind without a face
 * (E-102, Eggman, Tikal, kind 8) or a puppet SA1_ERR_UNSUPPORTED (a General-form Sonic while Super: accepted, not shown, as the
 * level). A world that never gets the call runs exactly as before; the reference worlds never call it. */
#define SA1_API_FACE_MOUTH 1
#define SA1_FACE_SAME_PAIR 0x200u  /* in sa1_character_face_mouth's flags, or OR'ed into sa1_character_face_level's style with
                                       SA1_FACE_LEVEL_LADDER: the mouth is drawn in the pair it is applied, not one pair
                                       later. At that pair's start libsa1 runs the face task's own step-0 blend before the
                                       step's draw - 8C07B080 with face / face_next / rate = count / time as the step's advance
                                       leaves them (mode 4 of 8C07B106) - and, when the face task is idle, its link of the EV
                                       head; the face task then computes the same points again. Only while no gameplay face
                                       blends (the face work's rate_spd is 0; the face task idle or following the face
                                       block: its modes 1 / 4). Game state is identical with and without it;
                                       only that step's draw differs. Drawn: lerp(key_a, key_b, round(weight x 64) / 64) in the
                                       pair (without it: + 1/64, one pair later). Not with SA1_FACE_LEVEL_TALK */
SA1_API sa1_status sa1_character_face_mouth(sa1_context *ctx, sa1_character_id id, uint32_t key_a, uint32_t key_b,
                                            float weight, uint32_t flags);

#define SA1_FACE_ACT_NONE      0u  /* the base */
#define SA1_FACE_ACT_SEQUENCE  1u  /* a face animation of the host (sa1_character_face_sequence) */
#define SA1_FACE_ACT_SCRIPT    2u  /* a script (sa1_character_face) */
#define SA1_FACE_ACT_HELD      3u  /* HOLD_LAST's last key, or a LOOP script */
#define SA1_FACE_ACT_LIPSYNC   4u
#define SA1_FACE_ACT_GAMEPLAY  5u  /* the game's own gameplay face (PFaceChange: hurt, frozen) */

typedef struct sa1_face_info {
    uint32_t struct_size;
    uint32_t keys;              /* vertex keys of the EV head's shape motion: 21 Sonic, 20 Tails / Knuckles / Amy / Tikal,
                                   8 Big; 0 none (E-102, Eggman, Super Sonic) */
    uint32_t letters;           /* bit n: face animation n uses only keys the character has (0x7FFFFFFF; Big 0x01FC0C1F) */
    uint32_t ladder_len;        /* Sonic 3, Tails 4, Knuckles 4, Amy 5, Big 3, Tikal 5; 0 none */
    uint8_t  ladder[8];         /* the loudness ladder, closed to open by the measured mouth opening (sadc docs/research/
                                   phoneme_shapes.md 6): Sonic 0 4 16, Tails 0 4 7 8, Knuckles 0 4 7 16, Amy 0 7 4 8 16,
                                   Big 0 4 5, Tikal 0 5 4 6 7 (not measured); round 0.9.4a: 0 5 4 6 7 for all but Big */
    uint16_t steps[31];         /* each face animation's length in 60 Hz steps at gameplay pace */
    uint8_t  first[31];         /* its first step's time (sa1_face_entry.enter's default) */
    uint8_t  last_key[31];      /* the key it ends on (what sa1_face_entry.hold holds) */
} sa1_face_info;
/* No world needed: each call reads 1ST_READ.BIN (Tikal: TIKAL_PROG.PRS) from the disc of the last sa1_load_disc and computes
 * the numbers (tens of ms; nothing is kept - sa1_load_disc frees the images it read: call it once per kind and keep the
 * result; review R.2). kind 8: keys 0; SA1_ERR_NOT_LOADED without a disc. */
SA1_API sa1_status sa1_face_info_get(sa1_context *ctx, int32_t kind, sa1_face_info *out);

/* ---- emotes (SA1_API_EMOTE, round 0.9.4a; sadc docs/research/emote_wheel_lib.md, faces_wheel_plan.md 4.5) ----------------
 * Any of a character's animations, played the way the game's cutscenes animate a live player: the event action queue of the
 * player's event work (EV_SetAction's body 8C04B4D2, EV_SetMotion's 8C04B528, EV_ClrAction's 8C04B48C), drawn by the
 * character's own display through 8C04B57C (welds 8C04BF00) instead of the gameplay action while the queue is not empty. The
 * gameplay keeps running underneath (the character stands: gravity, ground, water, damage and the idle timer are the game's);
 * the host writes nothing into the game state. Started at the character's next pair; cancelled before a pair whose input would
 * move him (GetAnalog 8C06CD22, the jump / action presses) and after a pair that left the standing state, hurt him, changed
 * the held object or the form. Only the entries of sa1_emote_get are accepted. A world without an emote call runs exactly as
 * before (the reference worlds never call one). The queue's own code leaks a finished entry when the next one has no blend
 * (8C04B57C), so libsa1 never queues an unblended entry behind another: such items start after a clear (review R.1). The queue
 * moves only on pairs the character is drawn: outside the host camera's view an emote pauses (review R.17). */
#define SA1_API_EMOTE 1

typedef struct sa1_emote_item {
    uint32_t source;            /* SA1_EMOTE_ROW or SA1_EMOTE_EXTRA */
    uint32_t index;             /* the action row, or the extra index (sa1_emote_info.index of an EXTRA entry) */
    int32_t  play;              /* SA1_EMOTE_PLAY_* */
    float    speed;             /* multiplier of the game's rate; 0 = 1 */
} sa1_emote_item;
#define SA1_EMOTE_ROW          0u
#define SA1_EMOTE_EXTRA        1u
#define SA1_EMOTE_PLAY_DEFAULT 0   /* the row's own: loop rows loop, once and hold rows once */
#define SA1_EMOTE_PLAY_ONCE    1
#define SA1_EMOTE_PLAY_LOOP    2
#define SA1_EMOTE_MAX_ITEMS    8u
typedef struct sa1_emote {
    uint32_t       struct_size;
    uint32_t       count;       /* 1..SA1_EMOTE_MAX_ITEMS, played in order; only the last may loop */
    sa1_emote_item items[SA1_EMOTE_MAX_ITEMS];
    int32_t        link;        /* blend frames between items of one object and into the stand; -1 = 8, 0 = none */
    uint32_t       flags;       /* SA1_EMOTE_* */
} sa1_emote;
#define SA1_EMOTE_CHAIN      0x1u  /* a once item: also the game's PL_ACTION.next chain up to its loop (75 > 76 > stand) */
#define SA1_EMOTE_NO_TRAIL   0x2u  /* no trailing stand entry after a once emote (the gameplay stand pops back) */
#define SA1_EMOTE_IN_AIR     0x4u  /* may start when not standing; then cancelled only by input, hurt and the object changes
                                       (the form, the board, the held object; review R.15) */
#define SA1_EMOTE_KEEP_INPUT 0x8u  /* no input cancel (the host cancels) */
#define SA1_EMOTE_NO_PROPS   0x10u /* Amy: do not show the hammer (8C0DE46C) for her attack rows */
#define SA1_EMOTE_HOLD       0x20u /* round 0.9.4b: an emote that ends in a pose (its last entry plays once) keeps that
                                       entry's LAST FRAME, the way the game's own hold rows do (mtnmode 5, PSetMotion
                                       8C079476: frame nbFrame - 1), instead of the trailing stand entry: state HOLDING
                                       until the cancels above (input, the state, hurt, the form, the board, the held
                                       object), a stop (STOP_BLEND blends from the held frame into the stand) or another
                                       emote end it. No effect when the last entry loops (it loops until a cancel or stop) */
#define SA1_API_EMOTE_HOLD 1
/* Errors: SA1_ERR_NOT_FOUND (no character), SA1_ERR_UNSUPPORTED (a puppet, Eggman, Tikal; or not standing without
 * SA1_EMOTE_IN_AIR: nothing queued; sa1_last_error says why in plain words: "in the air", "hurt", "carrying something",
 * "on the board", "moving", "busy"; review R.16), SA1_ERR_INVALID_ARGUMENT (an entry not offered for this kind and form),
 * SA1_ERR_LIMIT ("the game's memory is full": the game heap cannot take the queue entries; review R.7). Round 0.9.5
 * (board095_perf.md 4): a refusal of the game's state (SA1_ERR_UNSUPPORTED) is logged at DEBUG, SA1_ERR_LIMIT at WARN;
 * sa1_last_error is unchanged */
SA1_API sa1_status sa1_character_emote(sa1_context *ctx, sa1_character_id id, const sa1_emote *emote);
#define SA1_EMOTE_STOP_NOW   0u    /* EV_ClrAction at the next pair start: the gameplay pose */
#define SA1_EMOTE_STOP_BLEND 1u    /* the stand blended in after the current item (a loop finishes its cycle) */
SA1_API sa1_status sa1_character_emote_stop(sa1_context *ctx, sa1_character_id id, uint32_t how);
#define SA1_EMOTE_STATE_NONE    0u
#define SA1_EMOTE_STATE_PLAYING 1u
#define SA1_EMOTE_STATE_ENDING  2u /* blending into the stand */
#define SA1_EMOTE_STATE_HOLDING 3u /* SA1_EMOTE_HOLD: on its last entry's last frame (emote_frame = nbFrame - 1) */

/* The catalogue of one character entry: every offered table row (in G-SA1's worlds, not empty, not a stage or MINICART row;
 * kind 8: Sonic's rows on the Super body except the curled-body rows; rows 130-141 only for kind 8 and SA1_EMOTE_FORM_SUPER)
 * and every validated extra motion. No world needed: names, labels, classes, the extras' addresses and frame counts are a
 * generated table of names and numbers; rows' frames and play modes come from the loaded character set (sa1_action_info_get;
 * review R.2). Internal names are not unique (two motions may share one): dup_of compares the drawn object and the motion. */
#define SA1_EMOTE_FORM_NORMAL 0u
#define SA1_EMOTE_FORM_SUPER  1u   /* kind 0: the General form while Super (GT / GA rows and 130-141); kind 8 is always Super */
#define SA1_EMOTE_CLASS_STAND    0u
#define SA1_EMOTE_CLASS_IDLE     1u
#define SA1_EMOTE_CLASS_POSE     2u
#define SA1_EMOTE_CLASS_MOVE     3u
#define SA1_EMOTE_CLASS_AIR      4u
#define SA1_EMOTE_CLASS_BRAKE    5u
#define SA1_EMOTE_CLASS_BALL     6u
#define SA1_EMOTE_CLASS_ATTACK   7u
#define SA1_EMOTE_CLASS_CARRY    8u
#define SA1_EMOTE_CLASS_SETPIECE 9u
#define SA1_EMOTE_CLASS_HURT    10u
#define SA1_EMOTE_CLASS_FISH    11u
#define SA1_EMOTE_CLASS_SWIM    12u
#define SA1_EMOTE_CLASS_BOARD   13u
#define SA1_EMOTE_CLASS_SUPER   14u
#define SA1_EMOTE_F_NEEDS_PROP  0x1u  /* drawn without its object (held object, stage piece, fishing tasks, water) */
#define SA1_EMOTE_F_HOLD        0x2u  /* the game holds its last frame (mtnmode 5 / 7); played once */
#define SA1_EMOTE_F_SUPER_ONLY  0x4u  /* 130-141 */
#define SA1_EMOTE_F_BOARD       0x8u  /* a board motion (the emote layer makes no board task: no board is drawn; a board
                                         task in the world - a kept board - follows the player during such a motion,
                                         8C0B2F46 holds the board whenever his action is a board action) */
#define SA1_EMOTE_F_OTHER_OBJECT 0x10u /* not the main body (ball, curled body, Tails' / Knuckles' flight model): never blended
                                           from an entry of another object */
#define SA1_EMOTE_F_EXTRA       0x20u /* not in the action table (EV_SetMotion on the main body) */
typedef struct sa1_emote_info {
    uint32_t    struct_size;
    uint32_t    source;          /* SA1_EMOTE_ROW / SA1_EMOTE_EXTRA */
    uint32_t    index;           /* the row, or the extra index */
    const char *internal_name;   /* "s_kimepose0": the developers' motion file name (SA Tools labels' file names) */
    const char *label;           /* "S_MOT_Sonic_Level_Clear_Pose" (SA Tools; a seed for custom names later) */
    uint32_t    emote_class;     /* SA1_EMOTE_CLASS_* */
    uint32_t    flags;           /* SA1_EMOTE_F_* */
    uint32_t    frames;          /* NJS_MOTION.nbFrame (from the disc) */
    int32_t     play;            /* SA1_EMOTE_PLAY_LOOP or SA1_EMOTE_PLAY_ONCE: how it plays by default */
    int32_t     dup_of;          /* the list position of the first entry with the same object and motion, -1 none */
} sa1_emote_info;
SA1_API uint32_t   sa1_emote_count(const sa1_context *ctx, int32_t kind, uint32_t form);   /* 0 without a disc */
SA1_API sa1_status sa1_emote_get(const sa1_context *ctx, int32_t kind, uint32_t form, uint32_t i, sa1_emote_info *out);

/* ---- the board (SA1_API_BOARD, round 0.9.5; sadc docs/research/gsa1_095_plan.md 5.3, board095_lib.md 4) -----------------
 * Sonic's and Tails' sand / snow board by the game's own code: what the stage code does (SBOARD 0C9031AE, STG08 0C90EA14):
 * SetInputP(0, 44) and CreateElementalTask(3, 2, 8C0B2EE6 Sonic / 8C0BF7CE Tails) in the world, between ticks; the order
 * handler (8C0AFF1A / 8C0BBDD6 case 44) puts player 0 on the board at its next step; the board task follows him while his
 * action is a board action (Sonic 102-124, Tails 107-127) and is the game's free object (8C05872A) otherwise. Off: SetInputP(0,
 * 24) as Ice Cap's goal 0C90E900 does. Host deviations, each named by its flag or rule: a new board on every ON (the game's
 * re-entry: SA1_BOARD_REUSE_KEPT), the terrain overrides, the board-time surface kinds, the start push (SA1_BOARD_START_PUSH)
 * and again on request (SA1_BOARD_PUSH), Tails' jump restore, a fallen kept board freed, the host's free (SA1_BOARD_FREE), the
 * board not drawn before it was first held, Sonic's board pick (the game's pick by frame parity alternates here: libsa1's
 * frame counter moves by 2 a pair; sa1_world_board.c WB_RANDOM_PICK). The reference worlds never call any of it.
 * Also in this round, in their own sections: SA1_DRAW_BOARD (draws), SA1_SURFACE_BOARD_SNOW / _SAND (surface kinds),
 * SA1_PREVIEW_BOARD (object previews), SA1_INPUT_HOST_CAMERA (sa1_input). */
#define SA1_API_BOARD 1

#define SA1_BOARD_OFF   0   /* sa1_board_desc.on: riding -> order 24; else SA1_OK, nothing posted */
#define SA1_BOARD_ON    1   /* not riding -> order 44 (+ a new board task, rule 4a); riding -> only terrain / flags change */
#define SA1_BOARD_PUSH  2   /* riding on the ground (Sonic modes 62 / 64, Tails 48 / 50): the start push again */
#define SA1_BOARD_FREE  3   /* not riding, a kept board in the world: FreeTask it now (the host's own copy takes over) */

#define SA1_BOARD_TERRAIN_MAP   0u  /* the land's own words (and the SA1_SURFACE_BOARD_* kinds while riding) */
#define SA1_BOARD_TERRAIN_SNOW  1u  /* while riding: every land word of THIS world's collision window and moving objects gets the
                                       class bits 0x9C replaced by 0x8 (Ice Cap's snow, weight 0.15) */
#define SA1_BOARD_TERRAIN_SAND  2u  /* ... by 0x10 (Sand Hill's sand, weight 0.5) */

#define SA1_BOARD_START_PUSH     0x01u  /* at the get-on: STG08's start push 0C90E810 (decompiled) as a world task: players 0 AND
                                           1 spd.x >= 4.0 every step for 102 steps, then FreeTask */
/* (review R.2) the push task belongs to the ride: libsa1 frees it (FreeTask) in the step the board mode ends (any cause) and
   when ON is refused (SA1_BOARD_EV_REFUSED); without that rule an OFF at board pair 20 leaves Sonic on foot held at spd.x
   4.000 with a neutral stick for the rest of the 102 steps (measured) */
#define SA1_BOARD_REMOVE_ON_EXIT 0x02u  /* the kept board is freed in the step the board mode ends (default: it stays) */
/* (review R.5) REMOVE_ON_EXIT is read at ON and at OFF (an OFF with it frees the board in the exit step, EV_FREED arg 0): the
   host decides when the rider gets off */
#define SA1_BOARD_FAITHFUL_EXIT  0x04u  /* Tails: keep the game's jmp_y_spd x1.2 per ride (it compounds); default: restored */
#define SA1_BOARD_KEEP_FALLEN    0x08u  /* a kept board that fell out of the land lives on (the game's); default: freed 128 units
                                           below the last height where it stood */
#define SA1_BOARD_REUSE_KEPT     0x10u  /* ON with a kept board: post 44 only; the game's board task takes the player again
                                           (Ice Cap's re-entry 0C90DFE0 state 7; it snaps back from wherever it lies). Default:
                                           the kept board is freed (SA1_BOARD_EV_FREED arg 2) and a new one is made */
#define SA1_BOARD_KNOWN_FLAGS    0x1Fu

typedef struct sa1_board_desc {
    uint32_t struct_size;           /* must cover flags (the 0.9.5 size); later fields are read only when covered */
    int32_t  on;                    /* SA1_BOARD_OFF / ON / PUSH / FREE */
    uint32_t terrain;               /* SA1_BOARD_TERRAIN_* (ON; in force until the board mode ends) */
    uint32_t flags;                 /* SA1_BOARD_* (ON; OFF reads REMOVE_ON_EXIT only: review R.5) */
} sa1_board_desc;
/* Between ticks, on the ticking thread. SA1_OK; SA1_ERR_UNSUPPORTED with plain words in sa1_last_error, logged at DEBUG (not
 * Sonic 0 / Tails 2 - Super Sonic 8 included -, PUSH not riding on the ground, FREE while riding or without a kept board);
 * SA1_ERR_NOT_LOADED (the world image lacks S_SBMOT / M_SBMOT); SA1_ERR_INVALID_ARGUMENT (struct_size, on, terrain, unknown
 * flags; a terrain other than MAP in a DISC / FAITHFUL world); a faulted world's status. A character without a world gets it
 * built (as sa1_character_order). Whether the game TOOK the order shows after the next pair (SA1_BOARD_EV_ON / _REFUSED). */
SA1_API sa1_status sa1_character_board(sa1_context *ctx, sa1_character_id id, const sa1_board_desc *d);

#define SA1_STATE_ON_BOARD 0x10u    /* sa1_state.flags: player 0 is in a board mode (Sonic 62-68, Tails 48-54) */

#define SA1_EVENT_BOARD 15          /* sa1_event: id = SA1_BOARD_EV_*, position = the character (FREED: the board), arg below */
#define SA1_BOARD_EV_ON      1u     /* the game took order 44 (arg = the board mode) */
#define SA1_BOARD_EV_OFF     2u     /* the board mode ended (arg = the new mode; bank 0 order 24, 1 order 19, 2 other) */
#define SA1_BOARD_EV_REFUSED 3u     /* order 44 was not taken after one pair (arg = the mode that did not take it); the board
                                       task this call made was freed */
#define SA1_BOARD_EV_FREED   4u     /* libsa1 freed a board task (arg 0 REMOVE_ON_EXIT, 1 fell, 2 replaced by a new ON, 3 FREE) */
#define SA1_BOARD_EV_PUSH    5u     /* a start push began (arg 0 at the get-on, 1 SA1_BOARD_PUSH) */

typedef struct sa1_board_state {
    uint32_t struct_size;
    uint32_t flags;                 /* SA1_BSTATE_* */
    uint32_t terrain;               /* in force (MAP when not riding) */
    uint32_t board_task;            /* guest address of the world's board task (0: none) */
    uint32_t push_steps;            /* steps the running start push still has (0: none) */
    float    saved_jmp_y_spd;       /* Tails: the value restored at the end of the ride (0: none saved) */
    int32_t  board_kind;            /* SA1_CHAR_SONIC / SA1_CHAR_TAILS: whose board it is; -1 none */
    uint32_t board_variant;         /* Sonic: 0 MIW_B001, 1 SB_BOARD1 (the game's pick at 8C0B2EE6); Tails: 0 (MTX_BOARD0) */
    float    model_position[3];     /* host space: the origin of the board's model as the game drew it in the last pair
                                       (the task's pos, its Z / X / Y rotation, then 8C0B3068's -5.22 drop when not held);
                                       0 without a task */
    float    model_rotation[9];     /* host space, row-major, host axes: that model's rotation (identity without a task) */
    uint32_t still_steps;           /* steps a kept board has rested (taskwk flag & 1 set and its position unchanged); 0 held */
} sa1_board_state;
#define SA1_BSTATE_ON      0x01u    /* = SA1_STATE_ON_BOARD */
#define SA1_BSTATE_PENDING 0x02u    /* order 44 posted, not answered yet (until the next pair) */
#define SA1_BSTATE_HELD    0x04u    /* the board task follows the player (his action is a board action) */
#define SA1_BSTATE_KEPT    0x08u    /* a board task lives and is not held (left behind) */
#define SA1_BSTATE_SETTLED 0x10u    /* KEPT and still_steps >= SA1_BOARD_SETTLE_STEPS */
#define SA1_BOARD_SETTLE_STEPS 30u
/* filled up to the smaller struct_size; SA1_ERR_NOT_FOUND for an unknown id */
SA1_API sa1_status sa1_character_board_state(const sa1_context *ctx, sa1_character_id id, sa1_board_state *out);

#ifdef __cplusplus
}
#endif
#endif /* LIBSA1_H */