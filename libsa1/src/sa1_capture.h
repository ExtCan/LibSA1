/*
 * sa1_capture.h - the character mesh from the game's own draws (sadc docs/libsa1_integration.md 7.2, D10).
 * Internal: not part of the public API.
 *
 * In a character's world the game draws the character itself: SonicMain 8C0A77BC ends every step with its
 * display 8C0AE17A (motion player 8C040942 -> 8C040784 -> the hand-written renderer 8C037098 per node, which
 * draws on sub step 0 only, i.e. once per pair), effects draw through njDrawModel / njDrawObject / the Ninja
 * motion draws 8C60E080 / 8C60E49A. libsa1's runtime (src/rt/rt_draw.c, section 5) records those draws into a
 * per-world buffer while a capture window is open: the model's guest address, XMTRX, the matrix stack's slot 0
 * (the base the outermost push saved), the texlist and the draw state. This module is the host side:
 *
 *   attach   find the world's capture block (rt_draw.c's R4 pcdata in segment 0x18, by its magic) once per world;
 *   open     around sa1_character_tick's world tick: capture on (outside a window every rt draw entry does what
 *            its boundary stub did, so replays and tests that step the world directly are unaffected; the sphere
 *            visibility test 8C038D00 answers the same either way: it is the original's test against the world's
 *            screen and view, game logic, rt_draw.c section 6);
 *   close    capture off; snapshot the records of the last pair (the frame counter B_8C752BE8 tells which);
 *   emit     every model draw of the snapshot as triangles: the model's materials, points and normals as the
 *            draw read them (v3 payload), its meshsets, UVs and vertex colours read from the arena (static data),
 *            transformed by XMTRX * slot0^-1 (model -> SA1 world) and emitted by sa1_pose.c's emitter (host
 *            space, winding, colours, UVs, texture ids of the character's texlist).
 *
 * The layouts below mirror rt_draw.c's RT_CAP_REC / RT_CAP_HDR (DC layout, guest pointers as 32-bit words):
 * change both together (both sides check the sizes).
 *
 * Version 2 (2026-10-20, render output API SA1_API_RENDER): records carry njColorBlendingMode's modes and a payload
 * (snapshots of sprite and point-list draws, whose buffers the game reuses), the game's own 3D sprite draw 8C09E778 is
 * recorded, and the emitter produces draws (sa1_draw: archive / texture / material / blend / order) with sprites and
 * line segments next to the triangles. Texture ids are resolved through the draw's texlist: the NJS_TEXNAME it points
 * at lies in the texture array of one of the character's archives (effects draw with small texlists into SON_EFF ...).
 *
 * Version 3 (2026-10-20): a model draw's payload is a snapshot of the model's materials, points and normals at the
 * draw (header: u32 points, u32 materials, u32 SA1_CAP_MSNAP_* flags, u32 0; then 20 bytes per material, 12 per point,
 * 12 per normal). Version 2 read them when the host emitted the records, after the pair, and the game rewrites them in
 * between: PJoinVertexes welds the vertices for sub step 1's pose (whose display does not draw), Tails' display bends
 * its tail models (8C0BDC80) and blends their morph (8C055ECE) after drawing them, on both sub steps (the emitted
 * tails were two bends ahead of the drawn ones), the shape motion draws point model->points at a scratch buffer for
 * the draw only, and effects set a material's texture id / alpha just before drawing (Knuckles' Maximum Heat draws one
 * model with textures 1 and 2 in a pair, both emitted with the last one). A record without payload (a larger model, a
 * full pool: SA1_CAPF_PAY_LOST) is read live as in version 2.
 *
 * Version 4 (2026-10-20, the screen of sadc docs/research/visibility_fidelity.md): the visibility test 8C038D00 no
 * longer answers "visible" inside a window (and 0 outside); it is the original's test, game logic, and the header
 * counts its sub-step-0 answers of the pair (vis_frame / vis_shown / vis_hidden). A display the game's camera does
 * not see returns before it draws; hosts draw the character all the same: a snapshot whose pair asked the test and
 * got "not visible" every time is HIDDEN (sa1_capture_hidden), and sa1_character_tick then shows the CPU pose of the
 * game's action and frame instead of what was drawn (the blob shadow and Big's lure, which do not ask the test, are
 * drawn either way). A snapshot without draws whose pair was not hidden is still the game drawing nothing: Sonic's
 * damage blink asks the test first and then skips the draw.
 *
 * Round 4c (2026-10-21, sadc docs/research/lightspeed_visuals.md 4 and its Review; the record layout, size 0xD0 and
 * version 4 are unchanged): records of Ninja's action draw 8C60E49A carry SA1_CAPF_NJ_ACTION and get Ninja's colour
 * rules (F2(a)); a model's material id beyond nbMat is read from model->mats like Ninja does (F3, host only:
 * sa1_pose.c); 3D sprites have the game's size (F1: sp->sx / sp->sy always, times the matrix's axis lengths when
 * unscaled; zero extents left out; UV / 256); the 3D polygon draws follow the SDK's topology (fill / outline, the
 * zig-zag of njDrawPolygon3D, the strip / fan / stride-2 fan / separate triangles of njDrawTriangle3D) and a
 * one-texture texlist's texture 0 (F4 phase 1); njDrawLine3D's lines follow 8C63CE24's stride rule (F5).
 *
 * 0.6.0 fixes (2026-10-21; the record layout and version 4 are unchanged): the blob shadow is emitted and marked
 * (SA1_DRAW_SHADOW, sadc docs/research/gsa1_060_shadow.md L3-L5) instead of left out, also in hidden pairs (only_model:
 * appended to the CPU pose); every drawn model carries its record's matrix stack depth and texlist (node_depth /
 * node_tl) for the anchored interpolation of sa1_character.c (gsa1_060_render.md 3.6); a fading repeat of a draw is
 * left out only when an earlier draw of the same model and matrices is not fading itself (the dedupe kept one of the
 * game's two translucent copies of Tails' curled body on ball pairs: gsa1_060_render.md 4.3.1 and Review R3.6).
 *
 * Version 5 (round 0.9.4, sadc docs/research/gsa1_094_plan.md 6.2 tasks 1-2 with its Review R.1 / R.2; gsa1_091b_visuals.md
 * 4.2 R-F4 with its Review R.3.7 / R.3.8): every record carries the running exec D_8C1C1D40 (sa1_cap_rec.exec, record
 * 0xD4; the raw guest word, 0 outside the task executor). A draw whose record's exec is one of the game OBJECT execs
 * (sa1_capture_object_exec: the rings, the ring groups, the dropped / dug rings and both item boxes) is emitted with
 * SA1_DRAW_OBJECT in addition to its other flags, set before the emitters' merge tests (add_tri_draw in sa1_pose.c,
 * emit_sprite here), so an OBJECT draw never merges with another one (a ring's blob shadow with the character's), and its
 * drawn model carries SA1_NODE_OBJECT: sa1_character.c matches OBJECT draws between the pairs by their place in the world
 * (R-F1 / R-F2 / R-F3 for objects). exec 0 is never an OBJECT draw.
 *
 * Round 0.9.4a (sadc docs/research/super2_plan.md 6.2 with its Review R.4 / R.7 and owner answers O3 / O4; the record layout
 * and version 5 are unchanged, no rt change): the golden Light Speed charge (SA1_SUPOPT_LSD_GOLD). sa1_capture_close records
 * once per snapshot whether its pair was drawn with the General Super look on and the option set (sa1w_world_lsd_gold); the
 * emit then marks the plain charge's sparkles (lcharge02) and streaks (lcharge01) SA1_DRAW_SUPER_GOLD - the host draws the
 * textures' SA1_TEXTURE_VARIANT_SUPER_GOLD, which sa1_texture.c makes at load - and writes the charged glow's vertex colours
 * as the complement of the game's (sa1_capture.c, the comment above cap_gold_texlist). The Crystal Ring's green sparkles and
 * streaks keep the game's colours. Presentation only: no world memory is written.
 */
#ifndef SA1_CAPTURE_H
#define SA1_CAPTURE_H

#include "sa1_internal.h"

struct sa1g_world;

/* record kinds (rt_draw.c RT_CAP_*) */
#define SA1_CAP_SPRITE2D   1u
#define SA1_CAP_SPRITE3D   2u
#define SA1_CAP_LINE3D     3u   /* 8C63CE24 njDrawLine3D: n lines */
#define SA1_CAP_TRI3D      4u   /* 8C63D414 njDrawPolygon3D: n points, one convex polygon (the old name: rt_draw.c) */
#define SA1_CAP_POLY3D     5u   /* 8C63D66C njDrawTriangle3D: n triangles (strip / fan / separate by attr & 3) */
#define SA1_CAP_PRINT      6u
#define SA1_CAP_RENDER     7u   /* 8C037098 hand-written renderer (sub step 0 only) */
#define SA1_CAP_RENDER2    8u   /* 8C03700C */
#define SA1_CAP_NJMODEL    9u   /* njDrawModel */
#define SA1_CAP_NJOBJECT  10u   /* a node model of njDrawObject */
#define SA1_CAP_NJMOTION  11u   /* a node model of 8C60E080 / 8C60E49A */
#define SA1_CAP_DEBUG     12u   /* 8C6215D6 collision / debug display object */
#define SA1_CAP_POLY2D    13u   /* round 0.9.1: 8C63EBC8 njDrawPolygon2D (rt_draw.c rt_cap_poly2d) */
#define SA1_CAP_KINDS     16u
/* Round 0.9.1 (the HUD, sadc docs/research/gsa1_070_hud.md F2 with its Review R.5.3; record layout 0xD0 and version 4
 * unchanged): the 2D payloads (rt_draw.c RT_CAP_SPRITE2D_PAY / RT_CAP_POLY2D_HEAD).
 *   SA1_CAP_SPRITE2D  0x40 bytes: +0x00 NJS_SPRITE, +0x20 NJS_TEXANIM cell n, +0x34 pri (float bits; Ninja's 2D depth:
 *                     -1 front, more negative behind), +0x38 the running exec D_8C1C1D40 (a DC code address, 0 outside
 *                     the task executor: the HUD pass's draws), +0x3C D_8C673480 before the draw. A 0x34-byte reader
 *                     (the 3D sprites' layout) still reads its part.
 *   SA1_CAP_POLY2D    +0x00 u32 points kept (the call's n, at most 512), +0x04 u32 has colours, +0x08 u32 has uvs, +0x0C
 *                     u32 p->num, +0x10 pri, +0x14 exec, +0x18 D_8C673480, +0x1C the points (float x, y), then the
 *                     NJS_COLORs (ARGB8888), then the NJS_TEXs (s16 u, v x 1/256). attr bits (Ghidra 8C63EBC8.c): 0x20
 *                     filled (else the outline), 0x40 translucent, 0x80000000 textured (the current texture). */
#define SA1_CAP_SPRITE2D_PAY 0x40u
#define SA1_CAP_POLY2D_HEAD  0x1Cu
/* the kinds that draw a model (emitted) */
#define SA1_CAP_IS_MODEL(k) ((k) >= SA1_CAP_RENDER && (k) <= SA1_CAP_NJMOTION)

/* Character_Shadow_Blob (1ST_READ data): the blob shadow object every character's shadow code draws with
 * njDrawObject / njDrawModel after the shadow texture setup 8C099500 (8C09247E, 8C09222C, 8C092124 ..., 8C0877A0).
 * Since the 0.6.0 fixes (sadc docs/research/gsa1_060_shadow.md L3-L5 and its Review R.4.1) the render output keeps its
 * draws, marked SA1_DRAW_SHADOW (and SA1_DRAW_EFFECT, as before: the v0.1 / v0.2 mesh_out leaves it out with the
 * effects): sa1_capture_emit_opts.shadow_model. Its texture is the game's once the world sets the stage-start texlist
 * the setup reads (B_8C79F0F0 = OBJ_REGULAR) and OBJ_REGULAR is loaded; until then it is untextured
 * (SA1_DRAW_NO_TEXTURE). */
#define SA1_CAP_SHADOW_OBJECT 0x8C1ACA78u

#define SA1_CAP_MAGIC0   0x31414153u
#define SA1_CAP_MAGIC1   0x50414352u
#define SA1_CAP_MAGIC2   0x7E3C9A5Du
#define SA1_CAP_MAGIC3   0xC0DE7202u
#define SA1_CAP_VERSION  5u         /* v5 (round 0.9.4): sa1_cap_rec.exec, record 0xD4 */
#define SA1_CAP_MAX      512u
#define SA1_CAP_PAY      0x40000u   /* payload pool bytes (rt_draw.c RT_CAP_PAY) */
#define SA1_CAP_MSNAP_NORMALS 0x1u  /* v3 model snapshot: the normals follow the points */
#define SA1_CAP_NOPAY    0xFFFFFFFFu
#define SA1_CAPF_GAME_SPRITE 0x1u   /* drawn by the game's 3D sprite draw 8C09E778 */
#define SA1_CAPF_PAY_LOST    0x2u   /* the payload did not fit */
#define SA1_CAPF_NJ_ACTION   0x4u   /* made inside Ninja's action draw 8C60E49A (rt_draw.c RT_CAPF_NJ_ACTION): the
                                       original draws these nodes through 8C60E31C -> 8C621574 -> 8C6209A8, Ninja's
                                       material colour rules (sa1_capture_draw_state) */

typedef struct sa1_cap_rec {
    uint32_t kind;              /* SA1_CAP_* */
    int32_t sub;                /* B_8C754EC8 at the draw */
    int32_t frame;              /* B_8C752BE8 */
    int32_t depth;              /* matrix stack depth */
    uint32_t obj;               /* guest: NJS_MODEL * (model kinds), NJS_OBJECT * (debug), sprite / points */
    uint32_t texlist;           /* guest NJS_TEXLIST * (D_8C673480) */
    uint32_t ctl3d;             /* B_8C88F62C */
    uint32_t attr_and, attr_or; /* B_8C88F660 / 664 */
    uint32_t cmat[4];           /* B_8C88F668 */
    uint32_t n, attr;           /* sprite / line / polygon arguments */
    int32_t bank;               /* palette bank */
    float m[16];                /* XMTRX (Ninja row-vector convention: p' = p * m, translation in m[12..14]) */
    float base[16];             /* matrix stack slot 0 */
    uint32_t blend;             /* v2: njColorBlendingMode: source mode | destination mode << 8 (Ninja modes 0-11) */
    uint32_t pay, pay_len;      /* v2: payload offset in the pair's pool (SA1_CAP_NOPAY: none) and bytes */
    uint32_t flags;             /* v2: SA1_CAPF_* */
    uint32_t exec;              /* v5: the running exec D_8C1C1D40 at the draw (guest code address, raw; 0 outside the
                                   task executor: the display walker, the HUD pass). A ring group's children are run
                                   by the group's exec through 8C098E98, which does not set it: their draws carry the
                                   group's exec 8C087B80 */
} sa1_cap_rec;

typedef struct sa1_cap_hdr {
    uint32_t magic[4];
    uint32_t version, hdr_size, rec_size, capacity;
    uint32_t rec;               /* guest address of the records */
    uint32_t on;                /* capture window open */
    uint32_t frame, count, dropped, total;
    int32_t bank;
    uint32_t ninja, vis_calls;
    uint32_t calls[SA1_CAP_KINDS];
    uint32_t blend;             /* v2: current njColorBlendingMode modes */
    uint32_t pay;               /* v2: guest address of the payload pool */
    uint32_t pay_cap, pay_used;
    uint32_t vis_frame;         /* v4: frame counter of the pair vis_shown / vis_hidden count */
    uint32_t vis_shown;         /* v4: sub-step-0 visibility tests of that pair inside windows that answered "visible" */
    uint32_t vis_hidden;        /* v4: ... that answered "not visible" */
} sa1_cap_hdr;

typedef char sa1_cap_rec_size_check[sizeof(sa1_cap_rec) == 0xD4 ? 1 : -1];

/* ---- game OBJECT draws (round 0.9.4; sa1_capture.c, the comment of version 5 above) -------------------------------- */
/* The draw flag of a game object's draws. libsa1.h declares it (094-lib-set, the header's only editor in round 0.9.4);
 * until then it is defined here with the value the plan gives it (the lowest SA1_DRAW_* bit free in 0.9.3a's header). */
#ifndef SA1_DRAW_OBJECT
#define SA1_DRAW_OBJECT 0x1000u
#endif
/* 1 when exec (a capture record's sa1_cap_rec.exec; compared physically) is the exec of a game object whose draws are
 * SA1_DRAW_OBJECT: objexec_RING 8C0869E8, objexec_O_GRING 8C087B80 (its children's draws), the dropped / dug ring
 * 8C0870EA, objexec_O_ITEMBOX 8C05D10C and objexec_O_ItemBoxAir 8C05D9F6 (the table and its evidence: sa1_capture.c).
 * 0 is never one. */
int sa1_capture_object_exec(uint32_t exec);
/* the table (for tests and reports): *execs gets the addresses, *names (may be NULL) their names; returns the count */
uint32_t sa1_capture_object_execs(const uint32_t **execs, const char *const **names);
typedef char sa1_cap_hdr_size_check[sizeof(sa1_cap_hdr) == 0xA0 ? 1 : -1];

/* ---- the emitter (sa1_pose.c): a geometry built from models drawn by the game ------------------------------- */
/* a bounded reader of guest memory: a host pointer to n bytes at guest address g, or NULL */
typedef const uint8_t *(*sa1_guest_read_fn)(void *user, uint32_t g, uint32_t n);

typedef struct sa1_meshbuf {
    float *pos, *nrm;           /* 3 per vertex, host space */
    uint32_t nv, cap_v;
    uint32_t *idx;              /* 3 per triangle */
    float *uv;                  /* 6 per triangle */
    uint8_t *col;               /* 12 per triangle */
    int16_t *tex;
    uint32_t *mat;
    uint8_t *fl;
    uint32_t nt, cap_t;
    float *node_mtx;            /* 12 per drawn model: model -> host space (scale included) */
    uint32_t nn, cap_n;
    uint16_t *corner;           /* scratch: vertex index per polygon corner of one meshset */
    uint32_t *tri;              /* scratch: 3 corner indices per triangle of one meshset */
    uint32_t cap_corner, cap_tri;
    uint32_t bad_models;        /* draws whose model could not be read (bad pointer / count) */
    uint32_t mirrored;          /* draws whose matrix mirrors (negative determinant): winding swapped */
    /* render output (SA1_API_RENDER): the draws in the game's order; triangles of a PRIM_TRIANGLES draw are
       contiguous; sprites and line segments in their own arrays */
    sa1_draw *draws;
    uint32_t ndraw, cap_draw;
    sa1_sprite *spr;
    uint32_t nspr, cap_spr;
    sa1_line_vertex *lv;        /* 2 per segment */
    uint32_t nlv, cap_lv;
    int16_t *tarc;              /* 1 per triangle: texture archive (-1 untextured) */
    int16_t *tidx;              /* 1 per triangle: texture index in that archive (-1 untextured); `tex` keeps the
                                   sa1_geometry meaning: the index for archive 0, -1 otherwise */
    uint32_t order;             /* the next draw order */
    uint64_t sig;               /* topology: models, meshsets, triangle / sprite / line counts in order (lerp) */
    /* per drawn model (node: the same index as node_mtx), for the render interpolation of sa1_character.c: */
    uint32_t *node_v;           /* 2 per node: first vertex, vertex count */
    double *node_wm;            /* 12 per node: model -> SA1 world (3x4, column vectors), as the vertices used it */
    uint32_t *node_model;       /* guest address of the model */
    uint64_t *node_key;         /* hash of the model-local points and normals the draw read */
    uint8_t *node_fl;           /* SA1_NODE_* */
    float *lpos, *lnrm;         /* 3 per vertex of a node: its model-local point / normal as the draw read it ((0, 1, 0)
                                   without normals); vertices of 3D polygons (no node) leave them unset */
    /* per drawn model, for the anchored interpolation (sadc docs/research/gsa1_060_render.md 3.6): the capture record's
       matrix stack depth and texlist (sa1_emit_info.depth / texlist; 0 for draws that are not capture records) */
    int16_t *node_depth;
    uint32_t *node_tl;
    /* round 0.9.4 (capture version 5): who drew each drawn model (node_exec, the same index as node_mtx) and each sprite
       (spr_exec, the index of spr): the record's sa1_cap_rec.exec. Filled by sa1_capture_emit_into only when its options
       ask for it (sa1_capture_emit_opts.exec_arrays; the arrays stay NULL otherwise). They belong to sa1_capture.c, which
       grows them (cap_exec_n / cap_exec_spr); free them with sa1_meshbuf_free_exec BEFORE sa1_meshbuf_free (which clears
       the whole struct). Draws that are not capture records (the CPU pose) have none. */
    uint32_t *node_exec;
    uint32_t *spr_exec;
    uint32_t cap_exec_n, cap_exec_spr;
} sa1_meshbuf;

#define SA1_NODE_NORMALS 0x1u   /* the model has normals (else every vertex normal is the SA1-space up axis) */
#define SA1_NODE_SHADOW  0x2u   /* the blob shadow (emitted with SA1_DRAW_SHADOW) */
#define SA1_NODE_FADE    0x4u   /* drawn under a fading constant material (emitted with SA1_DRAW_AFTERIMAGE; round 0.9.1:
                                   the render plan matches draws regardless of it, but drops the motion hint across it) */
#define SA1_NODE_OBJECT  0x8u   /* round 0.9.4: drawn by a game object's task (emitted with SA1_DRAW_OBJECT; set by
                                   sa1_capture_emit_into after the emitter wrote the node's other flags): the render plan
                                   matches it by its place in the world, never with a draw that is not one (R-F1) */

/* ---- render interpolation of one drawn model between two pairs (sa1_pose.c) ---------------------------------- */
/* The two model -> world matrices of a draw interpolated as transforms (sa1_character_render): A = s * P * Q (or
 * s * Q * P) with Q the polar rotation, P the symmetric stretch on the side where it changes least (a squash applied
 * in the parent's frame, like the spin-dash ball's, stays where it is while the ball turns inside it), s = -1 for
 * mirroring matrices; Q turns about the axis of the relative rotation by alpha * phi, stretch and translation are
 * lerped. */
#define SA1_XLERP_NONE   0      /* not one affine transform pair (singular, mirrored on one side only): vertex lerp */
#define SA1_XLERP_SLERP  1      /* rotation phi about axis */
#define SA1_XLERP_SNAP   2      /* the rotation between them is ambiguous (near or above half a turn, no motion
                                   hint): the second's rotation and stretch, the translation lerped */
typedef struct sa1_xlerp {
    int32_t kind;               /* SA1_XLERP_* */
    int32_t left;               /* 1: A = s P Q (stretch in the parent's frame), 0: A = s Q P (in the model's) */
    double sign;
    double q0[4];               /* Q of the first (w, x, y, z) */
    double axis[3], phi;        /* the rotation from the first to the second: phi radians about axis */
    double theta;               /* the shortest-path angle between them (radians, 0..pi) */
    double p0[9], p1[9];        /* stretch of both (row-major) */
    double a1[9];               /* the second's linear part (SNAP) */
    double t0[3], t1[3];
} sa1_xlerp;
/* Plans the interpolation from m0 to m1 (3x4 column-vector matrices). turn >= 0: how far (radians) the draw's node
 * turns between them by its motion (sa1_pose_node_turn): picks which way round the rotation goes (a spin dash ball
 * turns up to 216 degrees per pair); turn < 0: unknown: the shortest path up to max_angle, SNAP beyond it. Returns
 * kind. */
int32_t sa1_xlerp_plan(const double m0[12], const double m1[12], double turn, double max_angle, sa1_xlerp *x);
/* the interpolated matrix at alpha (0..1) */
void sa1_xlerp_at(const sa1_xlerp *x, double alpha, double m[12]);
/* The node of object `object` of cs whose model lies at guest address `model` (physical compare); -1 if none. */
int32_t sa1_pose_node_of_model(const sa1_charset *cs, int32_t object, uint32_t model);
/* How far node `node` of object `object` turns relative to the object's root under motion `motion` from frame f0 over
 * df frames (df < 0: backwards; wrapping at the motion's length): the net angle (radians, 0 .. 2 pi) of its rotation
 * composed along the way at quarter frames - above pi when the motion turns it the long way round. -1 if the object /
 * motion / node is not there. */
double sa1_pose_node_turn(const sa1_charset *cs, int32_t object, int32_t motion, int32_t node, double f0, double df);

/* The draw state a renderer applied to every material of a draw (Ninja's control-3D flags B_8C88F62C as the
 * hand-written renderer's set-up 8C037294 reads them): NJD_CONTROL_3D_CONSTANT_ATTR (0x4) gives
 * attr = (attr & and) | or (B_8C88F660 / 664: Sonic's display ORs SA1_MAT_IGNORE_SPECULAR in, the fading copies
 * SA1_MAT_USE_ALPHA); NJD_CONTROL_3D_OFFSET_MATERIAL (0x20) adds the constant material B_8C88F668 (a, r, g, b in
 * [-1, 1]), NJD_CONTROL_3D_CONSTANT_MATERIAL (0x10) replaces the colour with it. The hand-written renderer
 * (8C037872) applies it to the alpha only (Tails' tail copies fade with offsets of -1/3 and -2/3) and gives
 * SA1_MAT_IGNORE_LIGHT materials white vertices: the records of 8C037098 / 8C03700C, and the node draws of the Ninja
 * motion draw 8C60E080 (the snowboards), which libsa1 walks with that renderer (rt_draw.c 5b; the original's
 * njDrawModel 8C6108B0 takes its textured RGB from a source that is UNVERIFIED, so those keep these rules: a
 * documented deviation, lightspeed_visuals.md Review R.2.2 / G5). Ninja's action draw 8C60E49A (the Light Speed
 * aura; SA1_CAPF_NJ_ACTION records) colours with 8C6209A8: the diffuse bytes / 255 as a, r, g, b; a textured
 * material (SA1_MAT_USE_TEXTURE) under control-3D bit 0x200 has RGB 1.0 and the diffuse alpha (8C620A94-8C620AB8);
 * 0x10 then replaces all four channels with the constant material (8C620B32-8C620B5C) or else 0x20 adds it to all
 * four (8C620B8C-8C620BC2); not palette-lit; IGNORE_LIGHT unlit. The final clamping and rounding of those channels
 * (libsa1: ds_channel, sa1_pose.c) is UNVERIFIED. */
typedef struct sa1_draw_state {
    int32_t attr_masks;         /* 1: attr = (attr & attr_and) | attr_or */
    uint32_t attr_and, attr_or;
    int32_t material;           /* 0: material / vertex colours; -1: + cmat; 1: cmat */
    int32_t hand;               /* the hand-written renderer: cmat on the alpha only, unlit materials white */
    float cmat[4];              /* a, r, g, b */
    int32_t tex_white;          /* 8C6209A8 with control-3D bit 0x200: a textured material's RGB is 1.0 (the texture
                                   alone), its alpha the diffuse alpha (before the constant material) */
} sa1_draw_state;

/* the draw state of a record's control-3D flags, masks and constant material */
void sa1_capture_draw_state(const sa1_cap_rec *r, sa1_draw_state *ds);

/* ---- the draws (render output) ----------------------------------------------------------------------------- */
/* alpha content of an RGBA8 texture: 0 none (all 255), 1 binary (only 0 / 255), 2 graded */
#define SA1_TEXALPHA_NONE   0u
#define SA1_TEXALPHA_BINARY 1u
#define SA1_TEXALPHA_BLEND  2u
uint32_t sa1_texture_alpha_kind(const uint8_t *rgba, uint32_t w, uint32_t h);
/* the alpha kind of texture `index` of archive `archive` of cs (cached per character set: sa1_texture.c) */
uint32_t sa1_charset_texture_alpha(const sa1_context *ctx, const sa1_charset *cs, int32_t archive, int32_t index);
/* round 0.9.4a (sadc docs/research/super2_plan.md 6.2 task 1; sa1_texture.c): the texture variants of a character set just
 * loaded (sa1_loader.c build_character): SA1_TEXTURE_VARIANT_SUPER_GOLD of SON_EFF lcharge01-04 when kind is Sonic and the
 * load flags have SA1_LOAD_SUPER_FORMS (sa1_texture.rgba_gold, in the set's arena); returns the number made */
uint32_t sa1_texture_make_variants(sa1_context *ctx, sa1_charset *cs, int32_t kind, uint32_t load_flags);
/* one texel through SA1_TEXTURE_VARIANT_SUPER_GOLD's rule (RGBA8 in, RGBA8 out: HSV value and saturation kept, the hue
 * SA1_SUPER_GOLD_HUE, alpha kept; sa1_texture.c gives the formula) */
void sa1_texture_gold_texel(const uint8_t in[4], uint8_t out[4]);

/* SA1_DRAW_* flags and blend factors of a triangle draw with the final attribute word attr, from the character
 * renderer (hand) or another draw, a texture of alpha kind tex_alpha (SA1_TEXALPHA_*, NONE when untextured) and the
 * smallest vertex alpha of its corners */
uint32_t sa1_draw_classify(uint32_t attr, int hand, uint32_t tex_alpha, uint32_t min_alpha, uint8_t *bsrc, uint8_t *bdst);
/* Ninja's njColorBlendingMode mode (0-11) as an SA1_BLEND_* factor of the source (dst = 0) or destination (dst = 1) */
uint8_t sa1_blend_of_ninja(uint32_t mode, int dst);

/* What one model draw is (sa1_pose_emit_model_ex): how its texture ids resolve, the draw state, the order */
typedef struct sa1_emit_info {
    sa1_guest_read_fn reader;
    void *user;
    const sa1_draw_state *ds;   /* NULL: materials and colours as stored */
    const sa1_context *ctx;     /* with cs: texture alpha kinds (classification); NULL: none */
    const sa1_charset *cs;
    /* texture ids: resolve != NULL: resolve(user, texlist, id, &archive) gives the index (-1: not found); else
       `textured` != 0 means the ids are indices of archive 0 (the character's texlist), 0 untextured */
    int32_t (*resolve)(void *user, uint32_t texlist, uint32_t id, int32_t *archive);
    uint32_t texlist;
    int32_t textured;
    uint8_t source;             /* SA1_SOURCE_* */
    uint32_t extra_flags;       /* SA1_DRAW_* added to every draw (AFTERIMAGE, EFFECT) */
    /* the alpha kind of a texture (SA1_TEXALPHA_*); NULL: sa1_charset_texture_alpha with ctx / cs */
    uint32_t (*tex_alpha)(void *user, int32_t archive, int32_t index);
    /* v3: what the draw read from the model (a capture record's snapshot, sa1_capture_model_snapshot): used in place
       of the model's arrays in guest memory; NULL / 0: read those */
    const uint8_t *snap_mats;   /* snap_nmat materials (20 bytes each, DC layout) */
    const uint8_t *snap_pts;    /* snap_np points (12 bytes each) */
    const uint8_t *snap_nrm;    /* snap_np normals, NULL: the draw had none */
    uint32_t snap_np, snap_nmat;
    int32_t snap;               /* 1: the snap_* fields hold a snapshot */
    int32_t depth;              /* the capture record's matrix stack depth (sa1_meshbuf.node_depth; 0: none) */
} sa1_emit_info;

void sa1_meshbuf_reset(sa1_meshbuf *b);
void sa1_meshbuf_free(const sa1_context *ctx, sa1_meshbuf *b);
/* room for at least nv vertices, nt triangles and nn nodes in total (allocation; outside ticks) */
sa1_status sa1_meshbuf_reserve(const sa1_context *ctx, sa1_meshbuf *b, uint32_t nv, uint32_t nt, uint32_t nn);
/* Emits the NJS_MODEL at guest address `model`, read through `reader` (points / normals / meshsets / materials /
 * UVs / vertex colours, all live), with the model -> SA1 world matrix m (3x4, column vectors, row-major, double):
 * vertices to host space (sa1_to_host_point / _dir), the strip / quad / N-gon rules of the loader, winding swapped
 * for SA1_COORDS_FLIP_WINDING and for a mirroring m; texture = the material's texture id when `textured` (the draw
 * used the character's texlist of archive 0), else -1; materials and colours through ds (NULL: as stored, like the
 * evaluator). SA1_ERR_BAD_FORMAT: the model could not be read (nothing emitted, counted in bad_models). */
sa1_status sa1_pose_emit_model(sa1_context *ctx, sa1_meshbuf *b, sa1_guest_read_fn reader, void *user, uint32_t model,
                               const double m[12], int textured, const sa1_draw_state *ds);
/* The same with the draw described by ei: texture ids resolved through the draw's texlist into the character's
 * archives, and one sa1_draw per meshset (merged with the previous draw when everything matches). For a draw of the
 * character renderer (ds->hand) the vertex colours are white with the material's alpha: the renderer lights from the
 * stage's light palette and uses only the material alpha (8C037350 / 8C037872), SA1_DRAW_PALETTE_LIT. */
sa1_status sa1_pose_emit_model_ex(sa1_context *ctx, sa1_meshbuf *b, const sa1_emit_info *ei, uint32_t model,
                                  const double m[12]);
/* room for nd more draws, ns sprites, nl line vertices */
sa1_status sa1_meshbuf_reserve_extra(const sa1_context *ctx, sa1_meshbuf *b, uint32_t nd, uint32_t ns, uint32_t nl);
/* room for one meshset of nc corners / nt triangles in the scratch arrays (so later emits do not allocate) */
sa1_status sa1_meshbuf_reserve_scratch(const sa1_context *ctx, sa1_meshbuf *b, uint32_t nc, uint32_t nt);
/* appends a draw (no merging); NULL when out of memory */
sa1_draw *sa1_meshbuf_add_draw(const sa1_context *ctx, sa1_meshbuf *b);
/* the builder's arrays as an sa1_geometry (pointers into b; valid until b changes) */
void sa1_meshbuf_geometry(const sa1_meshbuf *b, sa1_geometry *g);
/* draws of a geometry made by the CPU evaluator (consecutive triangles with the same texture and material; archive 0,
 * SA1_SOURCE_POSE) into b's draw array; b's triangle arrays are not used */
sa1_status sa1_meshbuf_draws_of_geometry(const sa1_context *ctx, sa1_meshbuf *b, const sa1_geometry *g,
                                         const sa1_charset *cs);
/* round 0.9.3a: the same with the textures' archive (a General world's CPU pose of the Super body: SUPERSONIC) */
sa1_status sa1_meshbuf_draws_of_geometry_ex(const sa1_context *ctx, sa1_meshbuf *b, const sa1_geometry *g,
                                            const sa1_charset *cs, int32_t archive);

/* ---- the capture of a world (sa1_capture.c) ----------------------------------------------------------------- */
typedef struct sa1_capture sa1_capture;

typedef struct sa1_capture_stats {
    uint32_t version;           /* SA1_CAP_VERSION of the world's capture block */
    uint32_t records;           /* records of the snapshot (the last pair that drew) */
    uint32_t model_records;     /* of them, model draws */
    uint32_t dropped;           /* records lost in that pair (buffer full) */
    uint32_t total;             /* records over the world's life */
    uint32_t vis_calls;         /* visibility tests inside capture windows (both sub steps, the world's life) */
    uint32_t vis_shown;         /* v4: the snapshot's pair: sub-step-0 visibility tests that answered "visible" */
    uint32_t vis_hidden;        /* v4: ... that answered "not visible" */
    uint32_t hidden;            /* v4: the snapshot is HIDDEN (sa1_capture_hidden) */
    uint32_t hidden_windows;    /* v4: windows whose last pair was hidden */
    uint32_t calls[SA1_CAP_KINDS];
    uint32_t windows;           /* capture windows in which pairs ran */
    uint32_t empty_windows;     /* of them, windows whose last pair drew nothing (e.g. the damage blink); with the HUD
                                   pass (round 0.9.1, SA1_COPT_HUD_PARTS) a pair holds the HUD's 2D records too, so a
                                   pair in which the character drew nothing is not empty then (gsa1_070_hud.md R.4.7) */
    int32_t frame;              /* frame counter of the snapshot */
    uint32_t block;             /* guest address of the capture block */
    /* the last emit */
    uint32_t emitted;           /* model draws emitted */
    uint32_t excluded;          /* draws of exclude_model (sa1_character_tick excluded the blob shadow before the 0.6.0
                                   fixes; it now marks it: shadows) */
    uint32_t clipped;           /* draws beyond the clip radius (models the game parks far away) */
    uint32_t foreign;           /* draws with another texlist: left out (own_texlist_only) or emitted untextured */
    uint32_t bad_models;        /* draws whose model could not be read */
    uint32_t mirrored;          /* draws with a mirroring matrix */
    /* v2 render output of the last emit */
    uint32_t duplicates;        /* model draws left out: the same model and matrices as an earlier draw, under a fading
                                   constant material (Tails' afterimage copies of the body, which on the Dreamcast blend
                                   over themselves and change nothing) */
    uint32_t effect_draws;      /* model draws from another archive than 0 */
    uint32_t unresolved;        /* draws whose texture is not in the character's archives (drawn untextured) */
    uint32_t sprites, lines, polygons;  /* sprite / line / polygon draws emitted */
    uint32_t payload_lost;      /* records whose payload did not fit */
    uint32_t pay_used;          /* payload bytes of the snapshot */
    /* round 4c (the last emit) */
    uint32_t zero_sprites;      /* sprites of zero extent left out (njScale(0) on a sparkle's last draw) */
    uint32_t outlines;          /* polygon draws without fill (attr 0x20) emitted as their edges (line segments) */
    uint32_t poly_textured;     /* textured polygon draws emitted with their texture (a one-texture texlist) */
    uint32_t poly_untextured;   /* textured polygon draws left untextured (texlist with more or less than one
                                   texture, or not one of the character's archives: phase 2, the texture number) */
    /* 0.6.0 fixes (the last emit) */
    uint32_t shadows;           /* model draws of the blob shadow emitted and marked SA1_DRAW_SHADOW (shadow_model) */
    uint32_t others;            /* model draws left out because they are not only_model */
    /* round 0.9.4 (the last emit) */
    uint32_t object_models;     /* model draws of a game object's task emitted with SA1_DRAW_OBJECT (blob shadows included) */
    uint32_t object_sprites;    /* sprites (and line / polygon draws) emitted with SA1_DRAW_OBJECT */
    uint32_t object_records;    /* records of the snapshot made by a game object's task (sa1_capture_object_exec), any kind */
    /* round 0.9.4a (sadc docs/research/super2_plan.md 6.2; sa1_capture.c "golden Light Speed charge") */
    uint32_t super_gold;        /* the snapshot's pair was drawn with the General look on and SA1_SUPOPT_LSD_GOLD 1 */
    uint32_t gold_sprites;      /* the last emit: sprites marked SA1_DRAW_SUPER_GOLD (the plain charge's lcharge02 sparkles) */
    uint32_t gold_polygons;     /* the last emit: polygon draws marked (the plain charge's lcharge01 streaks) */
    uint32_t gold_models;       /* the last emit: model draws of the charged glow (8C0B4F00) in the complementary colours */
} sa1_capture_stats;
#define SA1_CAPTURE_STATS_GOLD 1        /* the four fields above exist */

/* Finds the capture block of a world (once per world; the world must have its image applied). SA1_ERR_NOT_FOUND:
 * the lowered runtime has no capture block (builds without game code): the caller falls back to the CPU pose. */
sa1_status sa1_capture_attach(sa1_context *ctx, struct sa1g_world *w, sa1_capture **out);
void sa1_capture_free(sa1_context *ctx, sa1_capture *c);
/* the emitter's buffers sized for nv vertices, nt triangles, nn draws (so later ticks do not allocate) */
sa1_status sa1_capture_reserve(sa1_context *ctx, sa1_capture *c, uint32_t nv, uint32_t nt, uint32_t nn);
/* the capture window, around a world tick (host side, outside the world) */
void sa1_capture_open(sa1_capture *c);
void sa1_capture_close(sa1_capture *c);
/* 1 once a capture window ran pairs: from then on the snapshot is the character's mesh (an empty snapshot = the
 * game drew nothing in the last pair) */
int sa1_capture_valid(const sa1_capture *c);
/* v4: 1 when the last pair asked the visibility test on sub step 0 and every answer was "not visible": the game's
 * camera did not see the display, which therefore drew nothing (game logic; live worlds have no other caller of the
 * test than the character's displays). Hosts still draw the character: sa1_character_tick shows the CPU pose for
 * such a pair. */
int sa1_capture_hidden(const sa1_capture *c);
/* the snapshot */
uint32_t sa1_capture_records(const sa1_capture *c, const sa1_cap_rec **recs);
void sa1_capture_get_stats(const sa1_capture *c, sa1_capture_stats *out);
/* The model -> SA1 world matrix of a record (3x4, column vectors): XMTRX * slot0^-1 in the row-vector convention,
 * transposed. 0 when slot 0 is singular (then m = XMTRX alone). */
int sa1_capture_world_matrix(const sa1_cap_rec *r, double m[12]);
/* the guest reader of the capture's world (the arena and the capture block's pcdata) */
const uint8_t *sa1_capture_read(void *capture, uint32_t g, uint32_t n);
/* the payload of a snapshot record (n bytes; NULL: none / short) */
const uint8_t *sa1_capture_payload(const sa1_capture *c, const sa1_cap_rec *r, uint32_t n);
/* v3: the model snapshot of a model record into ei's snap_* fields; 0 (fields cleared) when the record has none */
int sa1_capture_model_snapshot(const sa1_capture *c, const sa1_cap_rec *r, sa1_emit_info *ei);
/* archive / texture index of texture `id` of the guest texlist `texlist`: the NJS_TEXNAME it reaches lies in the texture
 * array of one of the character's archives (sa1_capture_set_archives); -1 if not */
int32_t sa1_capture_resolve(void *capture, uint32_t texlist, uint32_t id, int32_t *archive);
/* the character's archives for texture resolution (their texlists as the world holds them; at attach) */
void sa1_capture_set_archives(sa1_capture *c, const sa1_charset *cs);

/* what sa1_capture_emit emits */
typedef struct sa1_capture_emit_opts {
    uint32_t texlist0;          /* guest address of the character's archive-0 texlist: draws with another texlist are
                                   untextured (texture -1) */
    uint32_t kinds;             /* bit mask (1u << SA1_CAP_*) of the model kinds to emit; 0 = all of them */
    uint32_t exclude_model;     /* a model left out (guest, compared physically); 0 = none */
    int32_t own_texlist_only;   /* 1: leave out draws made with another texlist than texlist0 (effects drawn with
                                   the characters' effect archives, whose textures sa1_mesh cannot name) */
    int32_t clip;               /* 1: leave out draws whose origin is farther than clip_radius from clip_center (the
                                   game parks hidden models far away, e.g. E-102's at y = -1000000) */
    float clip_center[3];       /* SA1 space */
    float clip_radius;
    /* v2 render output (zero: the v1 behaviour) */
    const sa1_charset *cs;      /* the character: textures resolved into any of its archives (sa1_capture_set_archives),
                                   texture alpha for the draw classification */
    int32_t effects;            /* 1: also sprites (njDrawSprite3D, 8C09E778), lines (njDrawLine3D) and 3D polygons */
    int32_t dedupe;             /* 1: leave out repeated draws of the same model at the same matrices under a fading
                                   constant material (sa1_capture_stats.duplicates) */
    int32_t live_models;        /* 1: ignore the v3 model snapshots and read every model as it is now (version 2's
                                   emission; tests compare the two) */
    /* 0.6.0 fixes (sadc docs/research/gsa1_060_shadow.md L3 / L4 and its Review R.4.1; zero: none) */
    uint32_t shadow_model;      /* the blob shadow's model (sa1_capture_shadow_model): its draws are emitted with
                                   SA1_DRAW_SHADOW | SA1_DRAW_EFFECT added (node flag SA1_NODE_SHADOW) */
    uint32_t only_model;        /* emit only the model draws of this model (guest, compared physically) and no
                                   sprites / lines / polygons: the blob of a hidden pair (sa1_character.c) */
    /* round 0.9.4 (zero: none) */
    int32_t exec_arrays;        /* 1: fill the builder's node_exec / spr_exec (sa1_meshbuf; free them with
                                   sa1_meshbuf_free_exec). SA1_DRAW_OBJECT / SA1_NODE_OBJECT are set either way */
} sa1_capture_emit_opts;

/* frees a builder's node_exec / spr_exec (sa1_capture_emit_opts.exec_arrays); call it before sa1_meshbuf_free */
void sa1_meshbuf_free_exec(const sa1_context *ctx, sa1_meshbuf *b);
/* room for the builder's node_exec / spr_exec up to its node and sprite capacities (so later emits do not allocate) */
sa1_status sa1_meshbuf_reserve_exec(const sa1_context *ctx, sa1_meshbuf *b);

/* the guest address of the blob shadow's model (Character_Shadow_Blob->model in the world), 0 if unreadable */
uint32_t sa1_capture_shadow_model(sa1_capture *c);

/* Emits the model draws of the snapshot into c's builder; *g gets the result (valid until the next emit / free). */
sa1_status sa1_capture_emit(sa1_context *ctx, sa1_capture *c, const sa1_capture_emit_opts *o, sa1_geometry *g);
/* the same into a caller's builder (the double-buffered render output of sa1_character.c) */
sa1_status sa1_capture_emit_into(sa1_context *ctx, sa1_capture *c, const sa1_capture_emit_opts *o, sa1_meshbuf *b,
                                 sa1_geometry *g);

/* tests: the capture of a ticking character (NULL: none yet / no world; src/sa1_character.c) */
sa1_capture *sa1_character_capture(const sa1_context *ctx, sa1_character_id id);

/* tests: the interpolation plan between a ticking character's last two pairs, made by the first sa1_character_render
 * with 0 < alpha < 1 after a pair (all zero before that) */
typedef struct sa1_lerp_stats {
    uint32_t struct_size;
    uint32_t models;            /* drawn models of the last pair */
    uint32_t same_topology;     /* 1: the two pairs drew the same models, meshsets, sprites and lines */
    uint32_t matched;           /* models matched with their draw in the pair before (the others: as drawn) */
    uint32_t rigid;             /* interpolated as transforms of unchanged model-local points */
    uint32_t local;             /* as transforms of lerped local points (welded, morphed, shape motion) */
    uint32_t vertex_lerp;       /* world-space vertex lerp (no transform pair) */
    uint32_t snapped;           /* ambiguous turns without a motion hint: the last pair's rotation */
    uint32_t hinted;            /* models whose turn between the pairs came from their node's motion */
    uint32_t over_half_turn;    /* of them, turning more than half a turn */
    uint32_t followers;         /* welded copies that follow the vertex they copy */
    float frame_advance;        /* motion frames between the pairs (0: no hint) */
    /* 0.6.0 fixes (sadc docs/research/gsa1_060_render.md 3.6): of the transform-interpolated models, */
    uint32_t anchored;          /* those interpolated relative to another drawn model they are rigidly attached to */
    uint32_t root_frame;        /* those interpolated in the drawn root's frame */
    /* round 0.9.1 (sa1_character.c, the comment above ANCHOR_MAX; tests compile against this header): */
    uint32_t world;             /* those interpolated in SA1 world space (a companion: gsa1_070_bird.md Review R.4 F5) */
    uint32_t screw;             /* of root_frame, the roots of rigid groups moved along the screw (gsa1_070_tails.md F-T1) */
    /* round 0.9.4 (sadc docs/research/gsa1_094_plan.md 6.2 task 3, R-F1 for game objects): */
    uint32_t objects;           /* drawn models of the last pair drawn by a game object's task (SA1_NODE_OBJECT) */
    uint32_t object_matched;    /* of them, matched by their place in the world with an OBJECT draw of the pair before */
} sa1_lerp_stats;
#define SA1_LERP_STATS_WORLD_SCREW 1    /* the two fields above exist (tests built against older headers check it) */
#define SA1_LERP_STATS_OBJECTS 1        /* objects / object_matched exist */
sa1_status sa1_character_lerp_stats(const sa1_context *ctx, sa1_character_id id, sa1_lerp_stats *out);

#endif /* SA1_CAPTURE_H */
