/*
 * sa1_skin.h - a worn skin: Metal Sonic over Sonic's world (SA1_API_SKIN, round 0.9.9f; sadc docs/research/metal099e.md with its review RM.1-16).
 * Internal: not part of the public API. A create flag on a kind 0 world (or sa1_frontend_set_skin): the world is byte for byte Sonic's, only the render output differs.
 *
 * Game thread (sa1k_load, the first create with the flag or the first sa1_frontend_set_skin) or the character's own tick (the rest): no FP state, no process static, no game state.
 * The model bytes and the textures are read from the disc with their SHA-1 at run time, held in memory only and freed with the disc. Nothing here writes the world's memory: the walk
 * of the world's tree (RM.1) only reads, through the capture.
 */
#ifndef SA1_SKIN_H
#define SA1_SKIN_H

#include "sa1_internal.h"
#include "sa1_capture.h"

#define SA1K_PARTS_MAX 32u
#define SA1K_NODES     80u

struct sa1k_state;
typedef struct sa1k_char sa1k_char;

/* What the world's display drew in the last pair (render_capture); row -1: nothing known */
typedef struct sa1k_drive {
    int32_t row;            /* the action row the display drew */
    int32_t steady;         /* not linking between rows: frame is the row's motion frame */
    int32_t linking;        /* round 0.9.9g: not steady, an emote excepted: the game's own blend between two of Sonic's rows (mtnmode 2, or a row asked for) */
    float speed;            /* round 0.9.9g: playerwk.spd.x */
    float frame;
    int32_t have_root;      /* pos, ang hold the drawn step */
    float pos[3];           /* SA1 space */
    uint32_t ang[3];        /* BAMS */
    float center_height;
} sa1k_drive;

/* the model and the textures of `skin`, once per disc and skin (round 1.0.0-beta1: up to SA1_SKINS_MAX, known by their identity; a loaded one is answered as it is): *out its state.
   SA1_ERR_NOT_LOADED with the reason in sa1_last_error without the disc's files, SA1_ERR_LIMIT past SA1_SKINS_MAX, SA1_ERR_UNSUPPORTED for a base_kind other than 0 */
sa1_status sa1k_load(sa1_context *ctx, const sa1_skin_desc *skin, struct sa1k_state **out);
void sa1k_free(sa1_context *ctx);
/* what sa1_texture_get and sa1_texture_archive see as (kind 0, archive): SA1_ARCHIVE_SKIN_FIRST + slot, or SA1_ARCHIVE_SKIN for slot 0; NULL: no such skin loaded */
const sa1_archive *sa1k_archive(const sa1_context *ctx, uint32_t archive);
/* round 1.0.0-beta1: the trail variant of Sonic's (archive, index) for the skin in `slot` (sa1_texture_variant_get); NULL: not one of its four textures, or no trail */
const uint8_t *sa1k_trail_rgba(const sa1_context *ctx, uint32_t slot, uint32_t archive, uint32_t index);

/* the per-character state: outside the arena, wearing q (sa1k_load's). anim: SA1_SKIN_ANIM_* at the start */
sa1_status sa1k_char_new(sa1_context *ctx, const struct sa1k_state *q, uint32_t anim, sa1k_char **out);
/* round 1.0.0-beta1: the archive its parts draw from (SA1_ARCHIVE_SKIN_FIRST + slot); 1: its trails are marked (a trail colour and SA1_SKIN_ANIM_TRAIL); the state it wears */
uint32_t sa1k_char_archive(const sa1k_char *sc);
int sa1k_trail_on(const sa1k_char *sc);
const struct sa1k_state *sa1k_char_state(const sa1k_char *sc);
void sa1k_char_free(sa1_context *ctx, sa1k_char *sc);
void sa1k_set_anim(sa1k_char *sc, uint32_t anim);

/* round 0.9.9g: a draw of the game's aura this pair, with its draw state: the shells take its colours */
void sa1k_note_glow(sa1k_char *sc, const sa1_draw_state *ds);
/* sa1_capture_emit_into: at the top of an emit, the world's own tree -> the lookup (RM.1, RM.2) */
void sa1k_note_begin(sa1k_char *sc, sa1_capture *c, const sa1_charset *cs);
/* 1: this model is the spin ball */
int sa1k_note_ball(const sa1k_char *sc, uint32_t model);
/* a draw of an own model (never the ball): its matrix by Sonic's node, the draw state of the torso. Fading draws and models that are no node are only dropped */
void sa1k_note_draw(sa1k_char *sc, uint32_t model, const double m[12], const sa1_draw_state *ds);
/* the spin ball's draw info: reader over the capture, its one texture from the skin's archive. bc lives as long as the emit */
typedef struct sa1k_ball_ctx {
    sa1_capture *cap;
    const struct sa1k_state *q;
} sa1k_ball_ctx;
void sa1k_ball_info(const sa1k_char *sc, sa1_capture *c, sa1k_ball_ctx *bc, sa1_emit_info *ei);

/* a hidden pair or the time before the first pair: the table from the CPU pose. node_mtx: object's 12 per node, t: what to add to their translations (SA1 space) */
void sa1k_note_pose(sa1k_char *sc, const sa1_charset *cs, int32_t object, const float *node_mtx, const float t[3]);

/* once per captured pair: the fades. Not in sa1_character_render: photo mode's freeze holds them */
void sa1k_step(sa1k_char *sc, const sa1_charset *cs, const sa1k_drive *d);
/* appends the parts to b, if the table has a torso. The caller refreshes the geometry */
sa1_status sa1k_append(sa1_context *ctx, sa1k_char *sc, const sa1_charset *cs, sa1_meshbuf *b);

/* the interpolation's questions (sa1_character.c): Sonic's mesh node a part follows, -1: no part; flying: no motion hint (RM.8) */
int32_t sa1k_part_mesh(const sa1k_char *sc, uint32_t model);
int32_t sa1k_flying(const sa1k_char *sc);

/* src/sa1_character.c */
sa1k_char *sa1k_char_of(const sa1_context *ctx, sa1_character_id id);
/* src/sa1_loader.c: the model's file unpacked (*overlay, freed by the caller) and of tex_file's archive only `keep`, by position, into cs's archive 0 */
sa1_status sa1_loader_skin(sa1_context *ctx, const char *gdi_path, sa1_charset *cs, const char *model_file, uint8_t **overlay, uint32_t *overlay_size,
                           const char *tex_file, const uint16_t *keep, uint32_t num_keep);

/* ---- tests (tests/skin_test.c) ---- */
typedef struct sa1k_test_info {
    uint32_t num_parts;
    int32_t drew;                       /* the last append made parts */
    float w, x;
    int32_t cur, prev;
    double phase, rate;
    double r[SA1K_PARTS_MAX][9];        /* the last append's rotation of each part */
    double p[SA1K_PARTS_MAX][3];        /* ... its origin, SA1 space */
    double scale;
    double align_deg[SA1K_PARTS_MAX];   /* the angle of A(i) */
    uint8_t have[SA1K_NODES];           /* the last pair's table */
    double mtx[SA1K_NODES][12];
    double rest[SA1K_NODES][9];
    double fly_root[12];
    uint32_t lookups;
    uint32_t model_bytes;               /* held */
    int32_t drive_row, has_fix52;
    /* round 0.9.9g */
    float live_mix;                     /* how much of Sonic's own pose of this pair is in the parts, 0 to 1 */
    int32_t have_flown;                 /* flown_r, src_r hold this pair's */
    double flown_r[SA1K_PARTS_MAX][9];  /* the flown pose */
    double src_r[SA1K_PARTS_MAX][9];    /* what it fades from, against the root */
    int32_t glow_passes;
} sa1k_test_info;
void sa1k_test_get(const sa1k_char *sc, sa1k_test_info *out);
uint32_t sa1k_test_glow_bytes(const sa1k_char *sc);
/* the sa1_frontend's state (NULL: no skin), for the same */
sa1k_char *sa1fe_test_skin(const sa1_frontend *fe);

#endif /* SA1_SKIN_H */
