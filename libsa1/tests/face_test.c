/*
 * face_test.c - round 0.9.4a: the game's event faces in libsa1 (include/libsa1.h "faces", SA1_API_FACE; round 0.9.4c:
 * the host's mouth SA1_API_FACE_MOUTH, SA1_FACE_SAME_PAIR, the per-kind ladders, sadc docs/research/phoneme_plan.md 6.1; sadc
 * docs/research/faces_wheel_plan.md 4.2-4.4, 5.3 and 6.2 with its Review R.2-R.6, R.8, R.10, R.11, R.13, R.19;
 * faces_events.md 5.4 / 7 for the expected numbers).
 *
 *   face_test [--only NAME[,NAME...]] [--verbose] [--guard-alloc] GDI
 *
 * Built and run by tests/build_face.py (a snapshot of build/<target>/libsa1.a and its headers; --run also runs
 * rebuild_memory under SA1_GUARD_ALLOC=1). SA1 space (SA1_COORDS_PRESET_NATIVE), a flat GRID floor, one world per character.
 * Everything is read through the public API (sa1_state_ex's face fields, sa1_character_render's draws, sa1_character_damage
 * for the hit) plus, for the checks of what the game drew, the world's own memory (the face block eventwk 0x5C-0x6B, the face
 * work playerwk.pfp, the EV head's model points) and the display capture's model records (found by magic in segment 0x18, as
 * the research probe SADC/build/research/faces094a/res_faces/probe_faces.c did).
 *
 * ---- API NOTES (094a-lib-faces -> 094a-gsa1-faces; the report's api_notes summarise this) ----------------------------
 * CALLS (src/sa1_face.c, src/world/sa1_world_face.c). Every call is latched and applied at the character's next pair start
 * (before the executor); the last call of a kind before a pair wins. A world that never gets a call runs exactly as before.
 *   sa1_character_face_mode(ctx, id, SA1_FACE_MODE_GAME | SA1_FACE_MODE_EVENT [| SA1_FACE_PACE_CUTSCENE])
 *     GAME in a world that never had EVENT writes nothing (F25: safe at creation). EVENT: the EV head always with the game's
 *     idle ('A' 75 % / 'B' 25 %, 8 steps each: rand() - random outcomes of that world differ, F23). EVENT off: EV_ClrFace and
 *     PFaceCancel (a waiting hurt face is dropped). PACE_CUTSCENE (only with EVENT): the game's own steps doubled.
 *   sa1_character_face(ctx, id, script, SA1_FACE_NOW | SA1_FACE_LOOP)  (tests, console, 2.0)
 *     1..64 of 'A'-'Z', 'a'-'e', '0'; NULL / "" = stop. GAME appends '0' (back to the regular face) unless LOOP; a LOOP
 *     script with '0' is refused. Without NOW the game's queue (starts when the running animation ends). 'A' in a script is
 *     the game's random idle choice (rand): a face animation plays 'A' itself.
 *   sa1_character_face_sequence(ctx, id, entries, count, SA1_FACE_HOLD_LAST)  (the UI's call)
 *     Always NOW (the running step ends at the next step on the nearer of its two keys). count 0 = stop. 1..32 entries
 *     {anim 0..30, reserved 0, enter 0..127 (0 = the game's first step), hold 0..3600, reserved2 0}; G-SA1's appended
 *     {A, back, 0} counts (31 own entries when back != 0 and keep is off, review R.10). Frames = 60 Hz steps. The enter /
 *     hold frames are never scaled by PACE_CUTSCENE (the game's steps inside an entry are).
 *   sa1_character_face_level(ctx, id, level, SA1_FACE_LEVEL_LADDER | SA1_FACE_LEVEL_TALK)  (per frame)
 *     0..1, < 0 off; a state until the next level call. No allocation, no log line (errors only in sa1_last_error). In a
 *     world without any face call a level < 0 installs nothing (send -1 freely). Off: the mouth closes over 6 steps, then a
 *     held face comes back ({8, key}) or the base (EVENT idle / GAME regular face).
 *   sa1_face_info_get(ctx, kind, &info)  (no world)
 *     Reads 1ST_READ.BIN (Tikal: also TIKAL_PROG.PRS) from the disc of the last sa1_load_disc on EVERY call (17-25 ms
 *     measured): call it once per kind and keep the result. No disc: SA1_ERR_NOT_LOADED; kind outside 0..8: INVALID_ARGUMENT.
 * WHICH FACE SHOWS (LIB enforces it): a running face animation or NOW script > lip sync > a held face (HOLD_LAST, a LOOP
 *   script) > the base. Talking resumes the pair after an animation's last pair (lip sync takes over in the pair where the
 *   animation's last step ends). While a host face runs a hit's 'I' waits and is dropped when it ends (GAME); a host face
 *   started over a running 'I' drops it (PFaceCancel) and shows from the 2nd pair. With EVENT on a hit shows no hurt face.
 * sa1_face_info (every kind the same steps / first / last_key; measured, F13):
 *   keys: Sonic 21, Tails / Knuckles / Amy / Tikal 20, Big 8, E-102 / Eggman / Super Sonic 0. letters 0x7FFFFFFF; Big
 *   0x01FC0C1F (A B C D E K L S T U V W X Y); none 0. ladder (round 0.9.4c, by kind: F13, F34): Sonic 0 4 16, Tails 0 4 7 8,
 *   Knuckles 0 4 7 16, Amy 0 7 4 8 16, Big 0 4 5, Tikal 0 5 4 6 7; none 0 (round 0.9.4a: 0 5 4 6 7 for all but Big).
 *   steps/first/last_key: A 8/8/0 B 8/2/0 C 48/5/7 D 32/4/5 E 16/4/5 F 32/3/7 G 48/5/19 H 44/2/13 I 32/4/9 J 48/4/9 K 64/6/0
 *   L 16/2/0 M 64/4/6 N 64/48/11 O 64/48/12 P 16/4/17 Q 32/3/16 R 64/18/6 S 16/6/7 T 64/4/1 U 32/8/3 V 16/16/3 W 32/10/2
 *   X 32/8/0 Y 10/10/1 Z 10/10/8 a 10/10/11 b 10/10/17 c 10/10/18 d 10/10/19 e 182/2/8.
 * sa1_state_ex (after a pair): face_mode as applied (| 0x100 paced); face_shown 1 = the EV head is linked into the tree the
 *   display draws (drawn from the NEXT pair; 0 while a General Sonic is Super, in the ball / other models; at a pair whose
 *   logic changes the action before its display the prediction can miss by that pair, F28); face_from / face_to / face_rate
 *   (the face block's while it runs, else a gameplay face's while it blends, else -1 / -1 / 0); face_letter 0..30, -2 a host
 *   step (hold, lip sync, close), -1 none; face_activity SA1_FACE_ACT_*. No world / no call: 0, 0, -1, -1, 0, -1, 0.
 * TIMELINES for G-SA1's core test (Sonic, flat floor, 12 pairs after creation; pair 0 = the first pair after the call):
 *   F3  EVENT on, 4 pairs, "CDE" queued: letter 2 for 24 pairs (from pair 0-4: when the idle sequence ends), 3 for 16, 4
 *       for 8, then the idle 0 / 1 (activity NONE); every C / D / E pair draws the EV-only textures (Sonic 25 26 27, Tails 19
 *       20, Knuckles 2 3 4, Amy 9 10; flags 0x30).
 *   F7  LADDER level L: p = L x (n - 1) in 1/64 (rounded), i = p / 64 (at most n - 2), r = p - 64 i; after the pair
 *       face_from / face_to = ladder[i] / ladder[i + 1], face_rate = (r + 1) / 64 (r = 63 / 64: the upper key, rate 0 / 1/127),
 *       letter -2, activity LIPSYNC; drawn by the next pair exactly (rms < 1e-6).
 *   F15 GAME {Z, 6, 30}: pairs 0-2 0>8 at 1/6, 3/6, 5/6 (letter 25, shown 1, SEQUENCE); pairs 3-17 8>8 (letter -2,
 *       SEQUENCE); pair 18 shown 0, NONE, letter -1.
 * REFUSALS (sa1_last_error): "<call>: E-102 has no face in the game (no face task)", "<call>: Super Sonic has no event head in
 *   the game", "<call>: Tikal cannot tick in libsa1 yet", "<call>: Big's face cannot show F, G, Z" (UNSUPPORTED);
 *   "<call>: 'q' (byte 113) is not a face letter A-Z, a-e or 0", "<call>: more than 64 letters", "<call>: a looping script may
 *   not contain '0' (it ends with the next face call)", "<call>: entry N: animation .. (0-30), enter .. (0-127), hold ..
 *   (0-3600), reserved .. / .. (0)", "<call>: N entries (at most 32)", "<call>: flags 0x.. (...)" (INVALID_ARGUMENT);
 *   a puppet: "character N is a puppet (SA1_CREATE_PUPPET): it has no world" (UNSUPPORTED); a dead world: its fault.
 * README LINES: Event faces: a switch between the regular face and the cutscene (event) head with the game's own blinking
 *   idle; face animations you make (the game's 31 expressions, each with frames to enter and to hold, 60 = 1 s); lip sync -
 *   the mouth opens with your voice. Big shows 14 of the 31 expressions; E-102 and Super Sonic have no event head in the
 *   game; a General Super Sonic shows the face again when he turns back. With event faces on, a hit shows no hurt face (as
 *   in the game's cutscenes). Known limits: a face change shows one pair (33 ms) after it is applied (the game's order);
 *   the cutscene speed option is not checked against a real cutscene (UNVERIFIED); a pair drawn from the CPU pose (the game's
 *   visibility test failed) shows the regular face.
 *
 *
 * ---- API NOTES, round 0.9.4c (094c-lib-mouth -> 094c-gsa1-voice / 094c-gsa1-lipsync; sadc docs/research/phoneme_plan.md
 * 4.2-4.6, 5.3-5.6 with its Review; the report's api_notes summarise this) ---------------------------------------------
 * CALL sa1_character_face_mouth(ctx, id, key_a, key_b, weight, flags)  (SA1_API_FACE_MOUTH 1; per frame)
 *   Each pair: face = key_a, face_next = key_b, face_time 64, face_count = round(weight x 64) - 1, face_script NULL,
 *   face_seq = the held step {127, key_b}: the writes of SA1_FACE_LEVEL_LADDER with the host's keys (F29: a mouth call and
 *   the level call that lands on the same keys give bit-identical face blocks, face works and drawn points). Quantised as
 *   the level: q = weight x 4096 rounded (> 1 = 4096, < 0 = off), r64 = (q x 64 + 2048) / 4096. It shares the level's
 *   latch: the last level or mouth call before a pair wins; a switch between them while talking is no restart (F31: no
 *   close, no PFaceCancel). weight < 0 = off exactly as a level < 0 (the close over 6 steps, then a held face or the base;
 *   F30, F32). Priority as lip sync (F32: a face animation's timeline is unchanged while mouth calls arrive every frame; the
 *   mouth shows from the pair after its end). A world given only weight < 0 calls installs nothing (F36).
 *   Refusals (nothing latched, no log line, sa1_last_error only): a key >= the character's sa1_face_info.keys
 *   "sa1_character_face_mouth: key 8 is not one of Big's 8 face keys (0-7)" (Sonic 0-20, Tails / Knuckles / Amy 0-19);
 *   a NaN weight or flags other than 0 / SA1_FACE_SAME_PAIR "sa1_character_face_mouth: flags 0x1 (0 or SA1_FACE_SAME_PAIR),
 *   weight ok" (SA1_ERR_INVALID_ARGUMENT); E-102, kind 8, a puppet: SA1_ERR_UNSUPPORTED with the texts of every face call; a
 *   General Sonic while Super: accepted, face_shown 0.
 * DRAWN WEIGHT (the face block after the pair: from / to / rate = the write + 2 steps, F7 / F29's numbers):
 *   without SAME_PAIR: drawn at the NEXT pair = lerp(key_a, key_b, min(r64 + 1, 64) / 64) (rms <= 3e-7 measured);
 *   with SA1_FACE_SAME_PAIR: drawn in the SAME pair = lerp(key_a, key_b, r64 / 64) (F33 (a) / (e), rms <= 3e-7), and in GAME
 *   mode the first talking pair after silence already draws the EV head with this pair's mouth (F33 (b): without the flag the
 *   static head); F35: the first opened draw at pair n with the flag, n + 1 without (every face kind). The face blocks are
 *   equal with and without it after every pair.
 * SA1_FACE_SAME_PAIR 0x200 (BUILT): in the mouth's flags or OR'ed into the level's style with SA1_FACE_LEVEL_LADDER
 *   (sa1_character_face_level(.., SA1_FACE_LEVEL_TALK | SA1_FACE_SAME_PAIR) and style 3: SA1_ERR_INVALID_ARGUMENT,
 *   "sa1_character_face_level: style 0x3 (0 ladder, 1 talk; SA1_FACE_SAME_PAIR only with the ladder), level ok"). At the pair
 *   start libsa1 runs the face task's own step-0 blend (8C07B080 with the values the step's advance leaves) before the
 *   step's draw, when the face task is in its mode 1 / 4 and no gameplay face blends (rate_spd 0), and links the EV head one
 *   step early when the static face is linked. F33 (c), two worlds x 600 pairs (Sonic, Tails): position, speed, mode,
 *   action, rings, rand, the face block, the face work, the EV head's points, player 0's taskwk / motionwk / playerwk /
 *   eventwk and the whole 16 MB arena identical after every pair; in segment 0x18 only the display capture (the draw records
 *   of the step-0 draws), the face block's counters and the collision adapter's two host pointers (cw_state.cand / tmp:
 *   they differ between any two worlds) differ; the drawn EV head differs in 317-318 of 328 talking pairs (non-vacuity).
 *   (d): a gameplay face blending at the pair start: the guard skips, nothing drawn early. (f): Tails' face task switches
 *   its parent (the flight-model bit) at the step of the early link: the link is put back after the step, the arena
 *   identical. If libsa1.h does not define
 *   SA1_FACE_SAME_PAIR (not shipped), 0x200 is refused as an unknown flag (F30).
 * LADDERS (sa1_face_info, F34): Sonic 0 4 16, Tails 0 4 7 8, Knuckles 0 4 7 16, Amy 0 7 4 8 16, Big 0 4 5, Tikal 0 5 4 6 7:
 *   the level opens monotonically (louder always opens wider). Their tops carry a little expression (Sonic 16, Tails 8,
 *   Knuckles 16: phoneme_shapes.md 3); UNVERIFIED by the owner's eye in game (rest-pose renders and numbers).
 * README LINES (round 0.9.4c): the mouth can follow the sounds you say (G-SA1 names the mouth shape, libsa1 draws it with
 *   the game's own blend); the faster mouth (shown in the same game frame: about 1/30 s sooner, nothing else changes); the
 *   loudness mouth fixed: louder always opens wider (in 0.9.4a Knuckles' mouth closed again when talking loudly).
 *
 * Cases (each fails before the round: the calls do not exist):
 *   info no_call event_mode script_queue queue_vs_now loop game_end lipsync_ladder lipsync_off refusals_big no_face_kinds
 *   pace event_off_cancel threads sequence_timing sequence_game_timing sequence_event now_over_long_step hold_last
 *   priority sequence_args hurt_during identity rebuild_memory game_calls_only over_gameplay_face talk_game
 *   (F1-F27 of faces_wheel_plan.md 6.2) and shown_matches_draws (F28, an extra check of face_shown); round 0.9.4c
 *   (phoneme_plan.md 6.1): mouth_block mouth_args mouth_mix mouth_priority same_pair ladders latency_pairs no_call_mouth
 *   (F29-F36); each prints what it checks. "FACE_TIMELINE <hash>": a hash of a fixed face scenario (build_face.py compares x86 and x64).
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_world.h"
#include "sa1_coladapt.h"     /* sa1ca_world_attach: the rebuild of rebuild_memory */
#include "sa1_face.h"         /* round 0.9.4c: sa1wf_get_stats (the face lane's counters) */
#include "test_util.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

/* the page-guard allocator (tests/guard_alloc.h): SA1_GUARD_ALLOC=1 puts libsa1's heap blocks against no-access pages */
#include "guard_alloc.c"
#define sa1_host_defaults(h) (sa1_host_defaults(h), ga_host(h))

static int g_verbose, g_fail, g_ok;
static int g_logs;              /* round 0.9.4c: every log callback (F30: the per-frame calls log nothing) */
#define CHECK(cond, ...)                                                                                                \
    do {                                                                                                                \
        int c_ = (cond) ? 1 : 0;                                                                                        \
        printf(c_ ? "  ok    " : "  FAIL  ");                                                                           \
        printf(__VA_ARGS__);                                                                                            \
        printf("\n");                                                                                                   \
        if (c_) {                                                                                                       \
            g_ok++;                                                                                                     \
        } else {                                                                                                        \
            g_fail++;                                                                                                   \
        }                                                                                                               \
    } while (0)

static const char *g_gdi;
static sa1_context *g_ctx;

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    g_logs++;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

/* ---- addresses (US v1.005) ----------------------------------------------------------------------------------------- */
#define A_PLAYER_TWP    0x8C78C608u
#define A_PLAYER_PWP    0x8C78C668u
#define A_FACETAB       0x8C19C32Cu
#define A_RAND          0x8C5FBB08u
#define TWP_EWP         0x3Cu
#define PWP_PFP         0x70u
#define EXT_GUEST       0x98000000u

static int mapped(uint32_t g)
{
    uint32_t seg = (g >> 24) & 31u;
    return g != 0 && (seg == 0x0Cu || seg == 0x18u);
}

static uint32_t g32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (mapped(g)) {
        memcpy(&v, sa1g_world_ptr(w, g), 4);
    }
    return v;
}

static int32_t gs16(const sa1g_world *w, uint32_t g)
{
    int16_t v = 0;
    if (mapped(g)) {
        memcpy(&v, sa1g_world_ptr(w, g), 2);
    }
    return v;
}

static int32_t gs8(const sa1g_world *w, uint32_t g)
{
    return mapped(g) ? *(const int8_t *)sa1g_world_ptr(w, g) : 0;
}

static uint32_t gu8(const sa1g_world *w, uint32_t g)
{
    return mapped(g) ? *(const uint8_t *)sa1g_world_ptr(w, g) : 0u;
}

static int same(uint32_t a, uint32_t b)
{
    return a && b && (a & 0x1FFFFFFFu) == (b & 0x1FFFFFFFu);
}

static char letter(int i)
{
    return (char)(i < 26 ? 'A' + i : 'a' + i - 26);
}

/* ---- the scene ------------------------------------------------------------------------------------------------------- */
static int scene(sa1_context *ctx)
{
    const float s = 2000.0f, h = 12000.0f;
    sa1_collision_tri *t = (sa1_collision_tri *)calloc(2u * 12u * 12u, sizeof *t);
    uint32_t n = 0;
    float x, z;
    sa1_status st;
    for (x = -h; x < h - 1.0f; x += s) {
        for (z = -h; z < h - 1.0f; z += s) {
            float q[4][3] = { { x, 0, z }, { x + s, 0, z }, { x + s, 0, z + s }, { x, 0, z + s } };
            memcpy(t[n].v[0], q[0], 12);
            memcpy(t[n].v[1], q[2], 12);
            memcpy(t[n].v[2], q[1], 12);
            t[n++].surface = SA1_SURFACE_SOLID;
            memcpy(t[n].v[0], q[0], 12);
            memcpy(t[n].v[1], q[3], 12);
            memcpy(t[n].v[2], q[2], 12);
            t[n++].surface = SA1_SURFACE_SOLID;
        }
    }
    st = sa1_collision_set_static(ctx, t, n);
    free(t);
    if (SA1_FAILED(st)) {
        printf("  set_static: %s\n", sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

/* ---- the character under test ----------------------------------------------------------------------------------------- */
typedef struct nodeset {
    uint32_t model[32];
    int n;
} nodeset;

static void walk(const sa1g_world *w, uint32_t o, nodeset *s)
{
    if (!mapped(o) || s->n >= 32) {
        return;
    }
    s->model[s->n++] = g32(w, o + 4u);
    walk(w, g32(w, o + 0x2Cu), s);
    walk(w, g32(w, o + 0x30u), s);
}

static int in_set(const nodeset *s, uint32_t model)
{
    int i;
    for (i = 0; i < s->n; i++) {
        if (same(s->model[i], model)) {
            return 1;
        }
    }
    return 0;
}

typedef struct fc {
    sa1_context *ctx;
    sa1_character_id id;
    sa1g_world *w;
    int32_t kind;
    uint32_t twp, pwp, ewp, pfp;
    uint32_t parent, object, motion, key0, nkeys, npts0;
    nodeset ev, st;
    int evtex[4], nevtex;
    uint32_t cap;
    float stick_x, stick_y;
    uint32_t buttons;
} fc;

typedef struct pinfo {
    int ev, st, evsnap;         /* model draws of the EV head / the static face in the pair's capture; EV draws with points */
    int key;                    /* the drawn EV node 0: key k exactly, -1 a blend, -2 not drawn */
    int ka, kb;                 /* its best fit lerp(key ka, key kb, t) */
    float t, resid;
    int rtex;                   /* sa1_character_render draws with an EV-only texture */
    float pts[3 * 512];         /* the drawn EV node 0 points (npts0 <= 512) */
    uint32_t npts;
} pinfo;

typedef struct fblock {
    uint32_t face, next, script, seq;
    int32_t time, count;
} fblock;

static void face_work_read(fc *c);

static void refresh(fc *c)
{
    c->w = sa1_character_world(c->ctx, c->id);
    c->twp = g32(c->w, A_PLAYER_TWP);
    c->pwp = g32(c->w, A_PLAYER_PWP);
    c->ewp = mapped(c->twp) ? g32(c->w, c->twp + TWP_EWP) : 0u;
    c->pfp = mapped(c->pwp) ? g32(c->w, c->pwp + PWP_PFP) : 0u;
}

static void fb_read(const fc *c, fblock *b)
{
    memset(b, 0, sizeof *b);
    if (!mapped(c->ewp)) {
        return;
    }
    b->face = gu8(c->w, c->ewp + 0x5Cu);
    b->next = gu8(c->w, c->ewp + 0x5Du);
    b->time = gs16(c->w, c->ewp + 0x5Eu);
    b->count = gs16(c->w, c->ewp + 0x60u);
    b->script = g32(c->w, c->ewp + 0x64u);
    b->seq = g32(c->w, c->ewp + 0x68u);
}

static uint32_t wseq(const fc *c)
{
    return mapped(c->pfp) ? g32(c->w, c->pfp + 0x0Cu) : 0u;
}

static int linked(const fc *c)
{
    return mapped(c->pfp) && same(g32(c->w, c->parent + 0x30u), c->object);
}

static void cap_find(fc *c)
{
    uint32_t n = sa1g_pcd_size(), off;
    c->cap = 0;
    for (off = 0; off + 0xA0u <= n; off += 4u) {
        uint32_t g = EXT_GUEST + off;
        if (g32(c->w, g) == 0x31414153u && g32(c->w, g + 4) == 0x50414352u && g32(c->w, g + 8) == 0x7E3C9A5Du &&
            g32(c->w, g + 12) == 0xC0DE7202u) {
            c->cap = g;
            return;
        }
    }
}

/* the drawn EV node 0 as lerp(key a, key b, t): least squares over every key pair */
static void fit(const fc *c, const float *P, uint32_t np, pinfo *pi)
{
    uint32_t a, b, i;
    double best = 1e30;
    const float *K[32];
    for (a = 0; a < c->nkeys && a < 32; a++) {
        K[a] = (const float *)sa1g_world_ptr(c->w, g32(c->w, c->key0 + 8u * a + 4u));
    }
    pi->ka = pi->kb = -1;
    for (a = 0; a < c->nkeys && a < 32; a++) {
        for (b = 0; b < c->nkeys && b < 32; b++) {
            double num = 0, den = 0, t, r = 0;
            for (i = 0; i < 3u * np; i++) {
                double d = (double)K[b][i] - K[a][i];
                num += ((double)P[i] - K[a][i]) * d;
                den += d * d;
            }
            t = den > 0 ? num / den : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            for (i = 0; i < 3u * np; i++) {
                double e = (double)P[i] - (K[a][i] + t * ((double)K[b][i] - K[a][i]));
                r += e * e;
            }
            if (r < best - 1e-12) {
                best = r;
                pi->ka = (int)a;
                pi->kb = (int)b;
                pi->t = (float)t;
            }
        }
    }
    pi->resid = (float)sqrt(best / (np ? np : 1));
    if (pi->ka == pi->kb || pi->t < 1e-4f) {
        pi->kb = pi->ka;
        pi->t = 0;
    } else if (pi->t > 1.0f - 1e-4f) {
        pi->ka = pi->kb;
        pi->t = 0;
    }
}

/* rms distance of the drawn EV node 0 points from lerp(key a, key b, t) */
static double rms_from(const fc *c, const pinfo *pi, uint32_t a, uint32_t b, double t)
{
    const float *A = (const float *)sa1g_world_ptr(c->w, g32(c->w, c->key0 + 8u * a + 4u));
    const float *B = (const float *)sa1g_world_ptr(c->w, g32(c->w, c->key0 + 8u * b + 4u));
    double r = 0;
    uint32_t i;
    if (pi->npts == 0 || A == NULL || B == NULL) {
        return 1e9;
    }
    for (i = 0; i < 3u * pi->npts; i++) {
        double e = (double)pi->pts[i] - ((double)A[i] + t * ((double)B[i] - A[i]));
        r += e * e;
    }
    return sqrt(r / pi->npts);
}

static void analyse(fc *c, pinfo *pi, int want_fit)
{
    uint32_t count, rec, rsz, pay, i;
    const sa1_render *rd = NULL;
    memset(pi, 0, sizeof *pi);
    pi->key = -2;
    pi->ka = pi->kb = -1;
    if (!c->cap) {
        cap_find(c);
    }
    if (c->cap) {
        count = g32(c->w, c->cap + 0x2Cu);
        rsz = g32(c->w, c->cap + 0x18u);
        rec = g32(c->w, c->cap + 0x20u);
        pay = g32(c->w, c->cap + 0x88u);
        for (i = 0; i < count && i < 4096u; i++) {
            uint32_t r = rec + i * rsz, kind = g32(c->w, r), obj = g32(c->w, r + 0x10u);
            if (kind < 7u || kind > 11u) {
                continue;
            }
            if (in_set(&c->ev, obj)) {
                uint32_t po = g32(c->w, r + 0xC4u), pl = g32(c->w, r + 0xC8u);
                pi->ev++;
                if (po != 0xFFFFFFFFu && pl >= 16u) {
                    pi->evsnap++;
                    if (same(obj, c->ev.model[0]) && c->key0) {
                        uint32_t s = pay + po, np = g32(c->w, s), nm = g32(c->w, s + 4u), k;
                        const void *pts = sa1g_world_ptr(c->w, s + 16u + 20u * nm);
                        pi->key = -1;
                        if (np == c->npts0 && np <= 512u) {
                            memcpy(pi->pts, pts, 12u * np);
                            pi->npts = np;
                            if (want_fit) {
                                fit(c, pi->pts, np, pi);
                            }
                            for (k = 0; k < c->nkeys; k++) {
                                uint32_t kp = g32(c->w, c->key0 + 8u * k + 4u);
                                if (mapped(kp) && memcmp(pts, sa1g_world_ptr(c->w, kp), 12u * np) == 0) {
                                    pi->key = (int)k;
                                    break;
                                }
                            }
                        }
                    }
                }
            } else if (in_set(&c->st, obj)) {
                pi->st++;
            }
        }
    }
    if (c->nevtex && sa1_character_render(c->ctx, c->id, 1.0f, &rd) == SA1_OK && rd) {
        for (i = 0; i < rd->num_draws; i++) {
            const sa1_draw *d = &rd->draws[i];
            int k;
            if (d->archive != 0) {
                continue;
            }
            for (k = 0; k < c->nevtex; k++) {
                if (d->texture == c->evtex[k]) {
                    pi->rtex++;
                }
            }
        }
    }
}

static void state(const fc *c, sa1_state_ex *x)
{
    memset(x, 0, sizeof *x);
    x->struct_size = sizeof *x;
    sa1_character_state_ex(c->ctx, c->id, x);
}

/* one pair; x / pi optional */
static int pair(fc *c, sa1_state_ex *x, pinfo *pi, int want_fit)
{
    sa1_input in;
    sa1_event ev[64];
    uint32_t n;
    sa1_status st;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = c->stick_x;
    in.stick_y = c->stick_y;
    in.buttons = c->buttons;
    st = sa1_character_tick(c->ctx, c->id, &in, 1.0f / 30.0f, NULL, NULL);
    if (st != SA1_OK) {
        printf("    tick: %s: %s\n", sa1_status_string(st), sa1_last_error(c->ctx));
        return -1;
    }
    do {
        n = 0;
        sa1_poll_events(c->ctx, ev, 64, &n);
    } while (n == 64);
    refresh(c);
    if (c->object == 0u && mapped(c->pfp)) {
        face_work_read(c);          /* a world created right before its first pair (game_calls_only) */
    }
    if (x != NULL) {
        state(c, x);
    }
    if (pi != NULL) {
        analyse(c, pi, want_fit);
    }
    return 0;
}

static int pairs(fc *c, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (pair(c, NULL, NULL, 0) != 0) {
            return -1;
        }
    }
    return 0;
}

static const int k_evtex[9][4] = { { 25, 26, 27, 0 }, { 0 }, { 19, 20, 0, 0 }, { 2, 3, 4, 0 }, { 0 }, { 9, 10, 0, 0 },
                                   { 0 }, { 0 }, { 0 } };
static const int k_nevtex[9] = { 3, 0, 2, 3, 0, 2, 0, 0, 0 };
static const char *const k_name[9] = { "Sonic", "Eggman", "Tails", "Knuckles", "Tikal", "Amy", "E-102", "Big",
                                       "Super Sonic" };

static sa1_character_id create(sa1_context *ctx, int32_t kind, uint32_t cflags, int super_given, uint32_t sflags,
                               sa1_status *st_out)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = cflags | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_START_RINGS;
    if (!(cflags & SA1_CREATE_PUPPET)) {
        d.flags |= SA1_CREATE_WORLD;
    }
    d.position[0] = 100.0f;
    d.position[1] = 0.5f;
    d.position[2] = 100.0f;
    d.start_rings = 50;
    if (super_given) {
        d.flags |= SA1_CREATE_SUPER;
        d.super_flags = sflags;
    }
    st = sa1_character_create_ex(ctx, &d, &id);
    if (st_out != NULL) {
        *st_out = st;
    }
    if (SA1_FAILED(st)) {
        if (st_out == NULL) {
            printf("  create %s: %s\n", kind >= 0 && kind < 9 ? k_name[kind] : "?", sa1_last_error(ctx));
        }
        return 0;
    }
    return id;
}

/* the face work the creator made and the EV head's points, keys and node sets */
static void face_work_read(fc *c)
{
    memset(&c->ev, 0, sizeof c->ev);
    memset(&c->st, 0, sizeof c->st);
    c->parent = g32(c->w, c->pfp + 0x10u);
    c->object = g32(c->w, c->pfp + 0x18u);
    c->motion = g32(c->w, c->pfp + 0x1Cu);
    walk(c->w, c->object, &c->ev);
    walk(c->w, g32(c->w, c->pfp + 0x14u), &c->st);
    c->key0 = g32(c->w, g32(c->w, c->motion));
    c->nkeys = g32(c->w, g32(c->w, c->motion) + 8u);
    c->npts0 = g32(c->w, c->ev.model[0] + 8u);
}

/* a character with `warm` pairs run, its face work and EV head read */
static int setup(sa1_context *ctx, fc *c, int32_t kind, uint32_t cflags, int super_given, uint32_t sflags, int warm)
{
    memset(c, 0, sizeof *c);
    c->ctx = ctx;
    c->kind = kind;
    c->id = create(ctx, kind, cflags, super_given, sflags, NULL);
    if (!c->id) {
        return -1;
    }
    refresh(c);
    if (warm > 0 && pairs(c, warm) != 0) {
        return -1;
    }
    memcpy(c->evtex, k_evtex[kind], sizeof c->evtex);
    c->nevtex = k_nevtex[kind];
    if (mapped(c->pfp)) {
        face_work_read(c);
    }
    return 0;
}

static void destroy(fc *c)
{
    if (c->id) {
        sa1_character_destroy(c->ctx, c->id);
        c->id = 0;
    }
}

static void hurt(const fc *c)
{
    sa1_damage d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    if (SA1_FAILED(sa1_character_damage(c->ctx, c->id, &d))) {
        printf("    damage: %s\n", sa1_last_error(c->ctx));
    }
}

static sa1_status seq1(const fc *c, int anim, int enter, int hold, uint32_t flags)
{
    sa1_face_entry e;
    memset(&e, 0, sizeof e);
    e.anim = (uint8_t)anim;
    e.enter = (uint16_t)enter;
    e.hold = (uint16_t)hold;
    return sa1_character_face_sequence(c->ctx, c->id, &e, 1, flags);
}

static int none_fields(const sa1_state_ex *x)
{
    return x->face_mode == 0 && x->face_shown == 0 && x->face_from == -1 && x->face_to == -1 && x->face_rate == 0.0f &&
           x->face_letter == -1 && x->face_activity == 0;
}

/* the steps of a sequence since it began: the reader is at step index k of table sequence n (face_seq = start + 2(k + 1))
   with face_count steps done in it */
static uint32_t seq_elapsed(const fc *c, int n, const fblock *b)
{
    uint32_t start = g32(c->w, A_FACETAB + 4u * (uint32_t)n), k, e = 0, idx;
    if (b->seq < start + 2u) {
        return 0;
    }
    idx = (b->seq - start) / 2u - 1u;
    for (k = 0; k < idx; k++) {
        e += (uint32_t)gs8(c->w, start + 2u * k);
    }
    return e + (uint32_t)b->count;
}

/* ==== round 0.9.4c: the host's mouth (SA1_API_FACE_MOUTH, SA1_FACE_SAME_PAIR, the per-kind ladders; F29-F36 of
 * sadc docs/research/phoneme_plan.md 6.1 with its Review R.2, R.3, R.16) =============================================== */
#if defined(SA1_FACE_SAME_PAIR)
#define T_SP SA1_FACE_SAME_PAIR
#else
#define T_SP 0u
#endif

typedef struct mentry {
    int a, b;
    float w;
} mentry;

/* the quantisation of sa1_character_face_mouth / _level: q = w x 4096 rounded (> 1: 4096; < 0: -1) */
static int32_t m_q(float w)
{
    return w < 0.0f ? -1 : w >= 1.0f ? 4096 : (int32_t)(w * 4096.0f + 0.5f);
}

/* the mouth's r64 = (q x 64 + 2048) / 4096 */
static uint32_t m_r64(float w)
{
    int32_t q = m_q(w);
    return q < 0 ? 0u : ((uint32_t)q * 64u + 2048u) / 4096u;
}

/* the ladder position of a level (the 0.9.4a mapping): keys and r64 */
static void lad_pos(const sa1_face_info *fi, float level, uint32_t *ka, uint32_t *kb, uint32_t *r64)
{
    uint32_t q = (uint32_t)m_q(level), n = fi->ladder_len, p64 = (q * (n - 1u) * 64u + 2048u) / 4096u, i = p64 / 64u;
    if (i > n - 2u) {
        i = n - 2u;
    }
    *r64 = p64 - 64u * i;
    *ka = fi->ladder[i];
    *kb = fi->ladder[i + 1u];
}

/* the face block after the pair of a lip-sync write (a, b, r64): the face task's two steps on face_count r64 - 1 */
static int m_block_ok(const sa1_state_ex *x, int a, int b, uint32_t r64)
{
    uint32_t cnt = r64 + 1u;
    int xa = a, xb = b;
    float xr = (float)cnt / 64.0f;
    if (cnt >= 64u) {
        xa = b;
        xr = (float)(cnt - 64u) / 127.0f;
    }
    return x->face_from == xa && x->face_to == xb && fabsf(x->face_rate - xr) < 1e-6f && x->face_letter == -2 &&
           x->face_activity == SA1_FACE_ACT_LIPSYNC;
}

/* the drawn weight of a write: the pair after it (+ 1/64, at most 1: review R.16) or, with SAME_PAIR, the pair itself */
static double m_drawn_t(uint32_t r64, int same_pair)
{
    uint32_t c = same_pair ? r64 : r64 + 1u;
    return (double)(c > 64u ? 64u : c) / 64.0;
}

static sa1_face_info face_info_of(int32_t kind)
{
    sa1_face_info fi;
    memset(&fi, 0, sizeof fi);
    fi.struct_size = sizeof fi;
    sa1_face_info_get(g_ctx, kind, &fi);
    return fi;
}

static sa1wf_stats stats_of(const fc *c)
{
    sa1wf_stats s;
    sa1wf_get_stats(c->w, &s);
    return s;
}

/* the EV head's model points of the world (every node), into buf; returns the bytes */
static size_t ev_points(const fc *c, uint8_t *buf, size_t cap)
{
    size_t n = 0;
    int i;
    for (i = 0; i < c->ev.n; i++) {
        uint32_t m = c->ev.model[i], pts = mapped(m) ? g32(c->w, m) : 0u, np = mapped(m) ? g32(c->w, m + 8u) : 0u;
        if (!mapped(pts) || np == 0u || n + 12u * np > cap) {
            continue;
        }
        memcpy(buf + n, sa1g_world_ptr(c->w, pts), 12u * np);
        n += 12u * np;
    }
    return n;
}

/* the 4.3 table of phoneme_plan.md (every entry: normal, quiet, loud, plain; F-V's ladder cap as a mouth), closed first */
static int table_of(int32_t kind, mentry *e)
{
    static const mentry sonic[] = { { 0, 0, 0 }, { 4, 16, .30f }, { 0, 7, 1 }, { 4, 16, .60f }, { 0, 4, 1 }, { 6, 16, .40f },
                                    { 6, 7, .30f }, { 0, 6, 1 }, { 0, 4, .50f } };
    static const mentry tails[] = { { 0, 0, 0 }, { 0, 16, 1 }, { 16, 8, .50f }, { 0, 4, 1 }, { 0, 6, 1 }, { 0, 7, 1 },
                                    { 0, 5, 1 }, { 0, 4, .50f } };
    static const mentry knux[] = { { 0, 0, 0 }, { 7, 16, .30f }, { 0, 7, 1 }, { 7, 16, .60f }, { 0, 5, 1 }, { 7, 14, .30f },
                                   { 0, 4, 1 }, { 0, 4, .50f } };
    static const mentry amy[] = { { 0, 0, 0 }, { 0, 8, 1 }, { 8, 16, .50f }, { 0, 7, 1 }, { 0, 10, 1 }, { 0, 5, 1 },
                                  { 4, 6, .30f }, { 0, 4, 1 }, { 0, 6, 1 }, { 0, 7, .50f } };
    static const mentry big[] = { { 0, 0, 0 }, { 0, 4, 1 }, { 0, 5, 1 }, { 0, 4, .40f }, { 0, 4, .70f }, { 0, 4, .30f },
                                  { 0, 4, .25f } };
    const mentry *t = NULL;
    int n = 0;
    switch (kind) {
    case SA1_CHAR_SONIC: t = sonic; n = (int)(sizeof sonic / sizeof sonic[0]); break;
    case SA1_CHAR_TAILS: t = tails; n = (int)(sizeof tails / sizeof tails[0]); break;
    case SA1_CHAR_KNUCKLES: t = knux; n = (int)(sizeof knux / sizeof knux[0]); break;
    case SA1_CHAR_AMY: t = amy; n = (int)(sizeof amy / sizeof amy[0]); break;
    case SA1_CHAR_BIG: t = big; n = (int)(sizeof big / sizeof big[0]); break;
    default: break;
    }
    if (t != NULL) {
        memcpy(e, t, (size_t)n * sizeof *e);
    }
    return n;
}

static uint32_t g_lcg = 0x094Cu;
static uint32_t lcg(void)
{
    g_lcg = g_lcg * 1103515245u + 12345u;
    return (g_lcg >> 8) & 0xFFFFFFu;
}

static const int32_t k_face5[5] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_BIG };

/* ==== F13 info ======================================================================================================== */
static const uint32_t k_head[9] = { 0x8C1DFBD8u, 0, 0x8C279198u, 0x8C37273Cu, 0x0CB0DA04u, 0x8C4D0718u, 0, 0x8C5367C4u, 0 };
static const uint32_t k_motion[9] = { 0x8C1FBBECu, 0, 0x8C28E9BCu, 0x8C38C870u, 0x0CB23298u, 0x8C4E591Cu, 0, 0x8C53C310u, 0 };

static void t_info(void)
{
    static const uint32_t keys[9] = { 21, 0, 20, 20, 20, 20, 0, 8, 0 };
    static const int32_t kinds[] = { 0, 2, 3, 5, 7, 6, 8, 4, 1 };
    sa1_context *c2 = NULL, *c3 = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_face_info fi;
    sa1_status st;
    size_t i;
    printf("info (F13): sa1_face_info_get without any world, from the disc\n");
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &c2) != SA1_OK || sa1_init(&h, &c3) != SA1_OK) {
        g_fail++;
        return;
    }
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = 1u << SA1_CHAR_SONIC;
    lo.flags = SA1_LOAD_DEFAULT;
    st = sa1_load_disc(c2, g_gdi, &lo);
    CHECK(!SA1_FAILED(st), "a fresh context with the disc (Sonic only): %s", sa1_status_string(st));
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        int32_t k = kinds[i];
        double t0 = tu_now_ms(), ms;
        memset(&fi, 0, sizeof fi);
        fi.struct_size = sizeof fi;
        st = sa1_face_info_get(c2, k, &fi);
        ms = tu_now_ms() - t0;
        CHECK(st == SA1_OK && fi.keys == keys[k], "%s: keys %u (expected %u), letters %08X, ladder %u: %u %u %u %u %u "
              "(%.1f ms)", k_name[k], (unsigned)fi.keys, (unsigned)keys[k], (unsigned)fi.letters, (unsigned)fi.ladder_len,
              fi.ladder[0], fi.ladder[1], fi.ladder[2], fi.ladder[3], fi.ladder[4], ms);
        if (keys[k] >= 20u) {
            /* round 0.9.4c: the per-kind ladders (phoneme_plan.md 4.6) */
            static const uint8_t lad[9][6] = { { 3, 0, 4, 16 }, { 0 }, { 4, 0, 4, 7, 8 }, { 4, 0, 4, 7, 16 },
                                               { 5, 0, 5, 4, 6, 7 }, { 5, 0, 7, 4, 8, 16 }, { 0 }, { 3, 0, 4, 5 }, { 0 } };
            CHECK(fi.letters == 0x7FFFFFFFu && fi.ladder_len == lad[k][0] && memcmp(fi.ladder, &lad[k][1], lad[k][0]) == 0,
                  "%s: all 31 letters, ladder %u-%u-%u%s", k_name[k], lad[k][1], lad[k][2], lad[k][3],
                  k == SA1_CHAR_SONIC ? "" : k == SA1_CHAR_AMY ? "-8-16" : k == SA1_CHAR_TAILS ? "-8"
                                      : k == SA1_CHAR_KNUCKLES ? "-16" : "-6-7");
        } else if (keys[k] == 8u) {
            CHECK(fi.letters == 0x01FC0C1Fu && fi.ladder_len == 3 && fi.ladder[0] == 0 && fi.ladder[1] == 4 &&
                      fi.ladder[2] == 5, "Big: letters 01FC0C1F (A-E, K, L, S-Y), ladder 0-4-5");
        } else {
            CHECK(fi.letters == 0 && fi.ladder_len == 0, "%s: no letters, no ladder", k_name[k]);
        }
        if (k == SA1_CHAR_SONIC) {
            int n;
            printf("    steps / first / last_key of the 31 (every kind):");
            for (n = 0; n < 31; n++) {
                printf(" %c %u/%u/%u", letter(n), fi.steps[n], fi.first[n], fi.last_key[n]);
            }
            printf("\n");
        }
        CHECK(fi.steps[2] == 48 && fi.first[2] == 5 && fi.last_key[25] == 8 && fi.steps[30] == 182 && fi.steps[0] == 8 &&
                  fi.steps[1] == 8 && fi.last_key[27] == 17, "%s: steps[2] %u, first[2] %u, last_key[25] %u, steps[30] %u "
              "(48, 5, 8, 182)", k_name[k], fi.steps[2], fi.first[2], fi.last_key[25], fi.steps[30]);
    }
    memset(&fi, 0, sizeof fi);
    fi.struct_size = sizeof fi;
    CHECK(sa1_face_info_get(c2, 9, &fi) == SA1_ERR_INVALID_ARGUMENT, "kind 9: SA1_ERR_INVALID_ARGUMENT");
    fi.struct_size = 8;
    fi.keys = 0;
    fi.letters = 0x12345678u;
    CHECK(sa1_face_info_get(c2, 0, &fi) == SA1_OK && fi.keys == 21 && fi.letters == 0x12345678u,
          "struct_size 8: only the first 8 bytes written (keys %u, letters untouched)", (unsigned)fi.keys);
    fi.struct_size = sizeof fi;
    st = sa1_face_info_get(c3, 0, &fi);
    CHECK(st == SA1_ERR_NOT_LOADED, "a context without a disc: %s (SA1_ERR_NOT_LOADED)", sa1_status_string(st));
    sa1_shutdown(c2);
    sa1_shutdown(c3);
    /* the address table against the face work every creator made (the per-kind numbers are verified at run time) */
    {
        static const int32_t face_kinds[] = { 0, 2, 3, 5, 7 };
        for (i = 0; i < sizeof face_kinds / sizeof face_kinds[0]; i++) {
            fc c;
            int32_t k = face_kinds[i];
            if (setup(g_ctx, &c, k, 0, 0, 0, 4) != 0) {
                g_fail++;
                continue;
            }
            CHECK(same(c.object, k_head[k]) && same(c.motion, k_motion[k]),
                  "%s's face work: EV head %08X (table %08X), shape motion %08X (table %08X), %u keys on its node 0",
                  k_name[k], (unsigned)c.object, (unsigned)k_head[k], (unsigned)c.motion, (unsigned)k_motion[k],
                  (unsigned)c.nkeys);
            destroy(&c);
        }
    }
}

/* ==== F1 no_call ===================================================================================================== */
static void t_no_call(void)
{
    static const int32_t kinds[] = { 0, 2, 3, 5, 7 };
    size_t i;
    printf("no_call (F1): no face call: the game's hurt face, no hook\n");
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        fc c;
        sa1_state_ex x;
        pinfo pi;
        int p, first = -1, last = -1, evn = 0, rt = 0, bad = 0;
        if (setup(g_ctx, &c, kinds[i], 0, 0, 0, 12) != 0) {
            g_fail++;
            continue;
        }
        state(&c, &x);
        CHECK(c.w->face.pre == NULL && c.w->face.fill == NULL && none_fields(&x), "%s: no hook, the face fields 'none'",
              k_name[c.kind]);
        hurt(&c);
        for (p = 0; p < 40; p++) {
            if (pair(&c, &x, &pi, 0) != 0) {
                break;
            }
            if (pi.ev) {
                if (first < 0) {
                    first = p;
                }
                last = p;
                evn++;
                rt += pi.rtex ? 1 : 0;
            }
            bad += none_fields(&x) ? 0 : 1;
        }
        if (c.kind == SA1_CHAR_BIG) {
            CHECK(evn == 0, "Big: no hurt face (the game's damage path has none): %d EV pairs", evn);
        } else {
            CHECK(evn >= 6 && first >= 1 && last <= 17 && rt == evn, "%s: the hurt EV head drawn in %d pairs (first %d, "
                  "last %d: pairs 1-17), EV-only textures in %d of them", k_name[c.kind], evn, first, last, rt);
        }
        CHECK(c.w->face.pre == NULL && bad == 0, "%s: still no hook after the hit, fields 'none' in every pair (%d not)",
              k_name[c.kind], bad);
        destroy(&c);
    }
}

/* ==== F2 event_mode ================================================================================================== */
static void t_event_mode(void)
{
    fc c;
    sa1_state_ex x;
    pinfo pi;
    int p, na = 0, nb = 0, bad = 0;
    printf("event_mode (F2): the EV head always, with the game's idle\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    CHECK(sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT) == SA1_OK, "EVENT on");
    pair(&c, &x, &pi, 0);
    CHECK(x.face_shown == 1 && x.face_mode == SA1_FACE_MODE_EVENT && pi.rtex == 0,
          "after the first pair: face_shown %u, face_mode %u; this pair drew the static face (EV-only draws %d)",
          (unsigned)x.face_shown, (unsigned)x.face_mode, pi.rtex);
    pair(&c, &x, &pi, 0);
    CHECK(pi.rtex > 0 && pi.ev > 0, "the second pair draws the EV head with its own textures (%d EV draws, %d EV-only)",
          pi.ev, pi.rtex);
    for (p = 0; p < 1800; p++) {
        fblock b;
        if (pair(&c, &x, NULL, 0) != 0) {
            break;
        }
        fb_read(&c, &b);
        if ((x.face_letter == 0 || x.face_letter == 1) && seq_elapsed(&c, x.face_letter, &b) < 2u) {
            if (x.face_letter == 0) {
                na++;
            } else {
                nb++;
            }
        }
        bad += (x.face_shown == 1 && (x.face_letter == 0 || x.face_letter == 1)) ? 0 : 1;
    }
    CHECK(na + nb >= 449 && na + nb <= 451 && nb * 100 >= 20 * (na + nb) && nb * 100 <= 30 * (na + nb) && bad == 0,
          "1,800 pairs: %d idle sequences ('A' %d, 'B' %d = %.1f %%), every pair the idle on the EV head (%d not)",
          na + nb, na, nb, 100.0 * nb / (na + nb ? na + nb : 1), bad);
    CHECK(sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_GAME) == SA1_OK, "EVENT off");
    pair(&c, &x, &pi, 0);
    CHECK(x.face_shown == 0 && x.face_mode == 0 && x.face_activity == 0, "after one pair: face_shown %u, face_mode %u",
          (unsigned)x.face_shown, (unsigned)x.face_mode);
    pair(&c, &x, &pi, 0);
    CHECK(pi.ev == 0 && pi.rtex == 0 && pi.st > 0, "the next pair draws the static face (EV draws %d, static %d)", pi.ev,
          pi.st);
    destroy(&c);
}

/* ==== F3 script_queue ================================================================================================ */
static void t_script_queue(void)
{
    static const int32_t kinds[] = { 0, 2, 3, 5 };
    size_t i;
    printf("script_queue (F3): \"CDE\" in EVENT mode (the game's queue)\n");
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        fc c;
        sa1_state_ex x;
        pinfo pi;
        int p, let[100], tex[100], ev[100], s = -1, rc = 0, rd = 0, re = 0, k, after = -9, texbad = 0;
        if (setup(g_ctx, &c, kinds[i], 0, 0, 0, 12) != 0) {
            g_fail++;
            continue;
        }
        sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT);
        pairs(&c, 4);
        CHECK(sa1_character_face(g_ctx, c.id, "CDE", 0) == SA1_OK, "%s: \"CDE\" queued", k_name[c.kind]);
        for (p = 0; p < 100; p++) {
            pair(&c, &x, &pi, 0);
            let[p] = x.face_letter;
            tex[p] = pi.rtex;
            ev[p] = pi.ev;
        }
        for (p = 0; p < 100 && s < 0; p++) {
            if (let[p] == 2) {
                s = p;
            }
        }
        k = s;
        while (k >= 0 && k < 100 && let[k] == 2) {
            rc++;
            k++;
        }
        while (k >= 0 && k < 100 && let[k] == 3) {
            rd++;
            k++;
        }
        while (k >= 0 && k < 100 && let[k] == 4) {
            re++;
            k++;
        }
        if (k >= 0 && k < 100) {
            after = let[k];
        }
        for (p = s + 1; s >= 0 && p <= k && p < 100; p++) {
            texbad += (ev[p] > 0 && tex[p] > 0) ? 0 : 1;
        }
        CHECK(s >= 0 && s <= 4 && rc == 24 && rd == 16 && re == 8 && (after == 0 || after == 1),
              "%s: C from pair %d for %d pairs, D %d, E %d (24, 16, 8), then the idle (%d)", k_name[c.kind], s, rc, rd, re,
              after);
        CHECK(texbad == 0, "%s: every pair of C, D, E draws the EV head with its EV-only textures (%d not)", k_name[c.kind],
              texbad);
        destroy(&c);
    }
}

/* ==== F4 queue_vs_now ================================================================================================ */
static void t_queue_now(void)
{
    fc c;
    sa1_state_ex x;
    fblock b;
    int p, at = -1;
    uint32_t snap;
    printf("queue_vs_now (F4): \"V\" during 'C'\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    sa1_character_face(g_ctx, c.id, "C", 0);
    pairs(&c, 3);
    sa1_character_face(g_ctx, c.id, "V", 0);
    for (p = 0; p < 30; p++) {
        pair(&c, &x, NULL, 0);
        if (x.face_letter == 21 && at < 0) {
            at = p;
        }
    }
    CHECK(at == 21, "queue: 'V' starts %d pairs later (21: when 'C' ends)", at);
    sa1_character_face(g_ctx, c.id, NULL, 0);
    pairs(&c, 6);
    sa1_character_face(g_ctx, c.id, "C", 0);
    pairs(&c, 3);
    fb_read(&c, &b);
    snap = (uint32_t)(b.count * 2 < b.time ? b.face : b.next);
    sa1_character_face(g_ctx, c.id, "V", SA1_FACE_NOW);
    pair(&c, &x, NULL, 0);
    CHECK(x.face_letter == 21 && x.face_from == (int32_t)snap && x.face_to == 3 && x.face_activity == SA1_FACE_ACT_SCRIPT,
          "NOW: 'V' from the next step (letter %d), blending from %d (the running step %u>%u at %d/%d snapped to its %s "
          "key %u) to 3", x.face_letter, x.face_from, b.face, b.next, b.count, b.time,
          b.count * 2 < b.time ? "start" : "end", snap);
    destroy(&c);
}

/* ==== F5 loop ======================================================================================================= */
static void t_loop(void)
{
    int mode;
    printf("loop (F5): \"Z\" LOOP holds key 8 (GAME and EVENT)\n");
    for (mode = 0; mode < 2; mode++) {
        fc c;
        sa1_state_ex x;
        int p, bad = 0, shownbad = 0;
        sa1_status st;
        if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
            g_fail++;
            continue;
        }
        if (mode) {
            sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT);
            pairs(&c, 3);
        }
        sa1_character_face(g_ctx, c.id, "Z", SA1_FACE_LOOP | (mode ? SA1_FACE_NOW : 0u));
        for (p = 0; p < 20; p++) {
            pair(&c, &x, NULL, 0);
            if (p >= 5) {
                bad += (x.face_from == 8 && x.face_to == 8 && x.face_activity == SA1_FACE_ACT_HELD) ? 0 : 1;
            }
            shownbad += x.face_shown == 1 ? 0 : 1;
        }
        CHECK(bad == 0 && shownbad == 0, "%s: key 8 held from pair 5 to 19, activity HELD (%d not), face_shown 1 in all 20 "
              "pairs (%d not)", mode ? "EVENT" : "GAME", bad, shownbad);
        st = sa1_character_face(g_ctx, c.id, "Z0", SA1_FACE_LOOP);
        pair(&c, &x, NULL, 0);
        CHECK(st == SA1_ERR_INVALID_ARGUMENT && x.face_activity == SA1_FACE_ACT_HELD && x.face_to == 8,
              "%s: \"Z0\" with LOOP: %s, nothing applied (still HELD on 8)", mode ? "EVENT" : "GAME", sa1_status_string(st));
        destroy(&c);
    }
}

/* ==== F6 game_end ==================================================================================================== */
static void t_game_end(void)
{
    fc c;
    sa1_state_ex x;
    pinfo pi;
    int p, ne = 0, end = -1;
    printf("game_end (F6): GAME \"E\" then the regular face (the appended '0')\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    sa1_character_face(g_ctx, c.id, "E", 0);
    for (p = 0; p < 14; p++) {
        pair(&c, &x, &pi, 0);
        if (x.face_letter == 4 && x.face_shown == 1) {
            ne++;
        } else if (end < 0) {
            end = p;
        }
    }
    CHECK(ne == 8 && end == 8, "'E' for %d pairs (8), the regular face from pair %d", ne, end);
    CHECK(x.face_shown == 0 && x.face_activity == SA1_FACE_ACT_NONE && x.face_letter == -1 && wseq(&c) == 0 && pi.ev == 0,
          "then: face_shown %u, activity %u, letter %d, the face work's sequence NULL, no EV draw (%d)",
          (unsigned)x.face_shown, (unsigned)x.face_activity, x.face_letter, pi.ev);
    destroy(&c);
}

/* ==== F7 lipsync_ladder ============================================================================================== */
/* F7's envelope on one kind (F34 runs it on the five face kinds: round 0.9.4c's per-kind ladders) */
static void ladder_envelope(int32_t kind)
{
    static const float lv[] = { 0, .1f, .3f, .5f, .7f, .9f, 1, .8f, .55f, .3f, .12f, 0, 0, .45f, .62f, .2f };
    {
        fc c;
        sa1_face_info fi;
        sa1_state_ex x;
        pinfo pi;
        int i, blockbad = 0, drawbad = 0, ladbad = 0;
        uint32_t ea = 0, eb = 0, li;
        double et = 0, worst = 0;
        char ltxt[48];
        if (setup(g_ctx, &c, kind, 0, 0, 0, 12) != 0) {
            g_fail++;
            return;
        }
        memset(&fi, 0, sizeof fi);
        fi.struct_size = sizeof fi;
        sa1_face_info_get(g_ctx, c.kind, &fi);
        for (i = 0; i < (int)(sizeof lv / sizeof lv[0]); i++) {
            int32_t q = (int32_t)(lv[i] * 4096.0f + 0.5f);
            uint32_t n = fi.ladder_len, p64 = ((uint32_t)q * (n - 1u) * 64u + 2048u) / 4096u, li = p64 / 64u, r64, cnt;
            uint32_t ka, kb, xa, xb;
            float xr;
            if (li > n - 2u) {
                li = n - 2u;
            }
            r64 = p64 - 64u * li;
            ka = fi.ladder[li];
            kb = fi.ladder[li + 1u];
            sa1_character_face_level(g_ctx, c.id, lv[i], SA1_FACE_LEVEL_LADDER);
            pair(&c, &x, &pi, 1);
            /* the drawn mouth of this pair is the position written at the previous pair + 1/64 */
            if (i > 0) {
                double r = rms_from(&c, &pi, ea, eb, et);
                worst = r > worst ? r : worst;
                drawbad += r < 1e-4 ? 0 : 1;
                if (g_verbose || r >= 1e-4) {
                    printf("    pair %2d: drawn %d>%d@%.3f, expected %u>%u@%.3f (rms %.6f)\n", i, pi.ka, pi.kb, (double)pi.t,
                           ea, eb, et, r);
                }
            }
            /* after this pair: two face-task steps on the written count r64 - 1 */
            cnt = r64 + 1u;
            if (cnt < 64u) {
                xa = ka;
                xb = kb;
                xr = (float)cnt / 64.0f;
                ea = ka;
                eb = kb;
                et = cnt / 64.0;
            } else {
                xa = kb;
                xb = kb;
                xr = (float)(cnt - 64u) / 127.0f;
                ea = kb;
                eb = kb;
                et = 0;
            }
            blockbad += (x.face_from == (int32_t)xa && x.face_to == (int32_t)xb && fabsf(x.face_rate - xr) < 1e-6f &&
                         x.face_letter == -2 && x.face_activity == SA1_FACE_ACT_LIPSYNC) ? 0 : 1;
            ladbad += (memchr(fi.ladder, x.face_from, fi.ladder_len) && memchr(fi.ladder, x.face_to, fi.ladder_len)) ? 0 : 1;
        }
        ltxt[0] = 0;
        for (li = 0; li < fi.ladder_len && li < 8u; li++) {
            snprintf(ltxt + strlen(ltxt), sizeof ltxt - strlen(ltxt), li ? "-%u" : "%u", (unsigned)fi.ladder[li]);
        }
        CHECK(blockbad == 0 && ladbad == 0, "%s: %d levels: the face block after each pair is the written position + 2 "
              "steps (%d not), keys on the ladder %s (%d not)", k_name[c.kind], i, blockbad, ltxt, ladbad);
        CHECK(drawbad == 0, "%s: drawn at pair n + 1 = written at pair n + 1/64 (worst rms %.7f, %d over 1e-4)",
              k_name[c.kind], worst, drawbad);
        destroy(&c);
    }
}

static void t_lipsync_ladder(void)
{
    printf("lipsync_ladder (F7): the mouth on the ladder, drawn one pair later\n");
    ladder_envelope(SA1_CHAR_SONIC);
    ladder_envelope(SA1_CHAR_BIG);
}

/* ==== F8 lipsync_off ================================================================================================= */
static void t_lipsync_off(void)
{
    int mode;
    printf("lipsync_off (F8): level < 0 closes the mouth over 6 steps\n");
    for (mode = 0; mode < 2; mode++) {
        fc c;
        sa1_state_ex x[6];
        int p;
        if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
            g_fail++;
            continue;
        }
        if (mode) {
            sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT);
            pairs(&c, 3);
        }
        for (p = 0; p < 10; p++) {
            sa1_character_face_level(g_ctx, c.id, 0.6f, SA1_FACE_LEVEL_LADDER);
            pair(&c, NULL, NULL, 0);
        }
        for (p = 0; p < 6; p++) {
            sa1_character_face_level(g_ctx, c.id, -1.0f, SA1_FACE_LEVEL_LADDER);
            pair(&c, &x[p], NULL, 0);
        }
        CHECK(x[0].face_to == 0 && x[1].face_to == 0 && x[2].face_to == 0 && x[0].face_letter == -2 &&
                  x[2].face_letter == -2 && x[1].face_activity == SA1_FACE_ACT_LIPSYNC,
              "%s: pairs 0-2 close to key 0 (to %d %d %d, rate %.3f %.3f %.3f)", mode ? "EVENT" : "GAME", x[0].face_to,
              x[1].face_to, x[2].face_to, (double)x[0].face_rate, (double)x[1].face_rate, (double)x[2].face_rate);
        if (!mode) {
            CHECK(x[3].face_shown == 0 && x[3].face_activity == SA1_FACE_ACT_NONE && x[4].face_shown == 0 &&
                      x[3].face_letter == -1, "GAME: then the regular face from pair 3 (shown %u, activity %u)",
                  (unsigned)x[3].face_shown, (unsigned)x[3].face_activity);
        } else {
            CHECK(x[3].face_shown == 1 && x[3].face_activity == SA1_FACE_ACT_NONE &&
                      (x[3].face_letter == 0 || x[3].face_letter == 1), "EVENT: then the idle from pair 3 (letter %d)",
                  x[3].face_letter);
        }
        destroy(&c);
    }
}

/* ==== F9 refusals_big ================================================================================================ */
static void t_refusals_big(void)
{
    fc c;
    sa1_state_ex x;
    sa1_status s1, s2, s3, s4, s5, s6;
    int p, alive = 1;
    printf("refusals_big (F9): Big's letters\n");
    if (setup(g_ctx, &c, SA1_CHAR_BIG, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    CHECK(sa1_character_face(g_ctx, c.id, "C", SA1_FACE_LOOP | SA1_FACE_NOW) == SA1_OK, "Big: \"C\" LOOP NOW accepted");
    pairs(&c, 6);
    s1 = sa1_character_face(g_ctx, c.id, "G", 0);
    s2 = sa1_character_face(g_ctx, c.id, "Z", SA1_FACE_NOW);
    s3 = sa1_character_face(g_ctx, c.id, "F", 0);
    CHECK(s1 == SA1_ERR_UNSUPPORTED && s2 == SA1_ERR_UNSUPPORTED && s3 == SA1_ERR_UNSUPPORTED &&
              strstr(sa1_last_error(g_ctx), "Big's face cannot show F") != NULL,
          "\"G\", \"Z\", \"F\": SA1_ERR_UNSUPPORTED (\"%s\")", sa1_last_error(g_ctx));
    s4 = sa1_character_face(g_ctx, c.id, "q", 0);
    s5 = sa1_character_face(g_ctx, c.id, "1", 0);
    s6 = seq1(&c, 6, 0, 0, 0);
    CHECK(s4 == SA1_ERR_INVALID_ARGUMENT && s5 == SA1_ERR_INVALID_ARGUMENT && s6 == SA1_ERR_UNSUPPORTED,
          "\"q\", \"1\": SA1_ERR_INVALID_ARGUMENT; a sequence with G: SA1_ERR_UNSUPPORTED");
    for (p = 0; p < 100; p++) {
        if (pair(&c, &x, NULL, 0) != 0 || (x.flags & SA1_STATE_FAULTED)) {
            alive = 0;
            break;
        }
        if (p == 0) {
            CHECK(x.face_activity == SA1_FACE_ACT_HELD && x.face_letter == 2, "the running \"C\" goes on (activity %u, "
                  "letter %d)", (unsigned)x.face_activity, x.face_letter);
        }
    }
    CHECK(alive, "the world is alive after 100 pairs (no fault)");
    destroy(&c);
}

/* ==== F10 no_face_kinds ============================================================================================== */
static void t_no_face_kinds(void)
{
    static const int32_t kinds[] = { SA1_CHAR_GAMMA, SA1_CHAR_SUPER_SONIC };
    size_t i;
    printf("no_face_kinds (F10): E-102, Super Sonic, the General form, a puppet\n");
    for (i = 0; i < 2; i++) {
        fc c;
        sa1_status a, b, d, e, f;
        if (setup(g_ctx, &c, kinds[i], 0, 0, 0, 2) != 0) {
            g_fail++;
            continue;
        }
        a = sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT);
        b = sa1_character_face(g_ctx, c.id, "C", 0);
        d = seq1(&c, 2, 0, 0, 0);
        e = sa1_character_face_level(g_ctx, c.id, 0.5f, 0);
        f = sa1_character_face(g_ctx, c.id, NULL, 0);
        CHECK(a == SA1_ERR_UNSUPPORTED && b == a && d == a && e == a && f == a && c.w->face.pre == NULL,
              "%s: every call SA1_ERR_UNSUPPORTED, no hook (\"%s\")", k_name[c.kind], sa1_last_error(g_ctx));
        destroy(&c);
    }
    {
        fc c;
        sa1_state_ex x;
        int p, shown_super = 0, link_super = 0, sup = 0, back = -1;
        if (setup(g_ctx, &c, SA1_CHAR_SONIC, SA1_CREATE_SUPER_CAPABLE, 1, SA1_SUPER_GENERAL | SA1_SUPER_START, 6) == 0) {
            CHECK(sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT) == SA1_OK, "the General form: accepted");
            for (p = 0; p < 6; p++) {
                pair(&c, &x, NULL, 0);
                sup += (x.flags & SA1_STATE_SUPER) ? 1 : 0;
                shown_super += x.face_shown;
                link_super += linked(&c);
            }
            CHECK(sup == 6 && shown_super == 0 && link_super > 0, "while Super: face_shown 0 in %d Super pairs although the "
                  "face task links Sonic's EV head into his undrawn normal tree (%d pairs)", sup, link_super);
            sa1_character_order(g_ctx, c.id, SA1_ORDER_NORMAL);
            for (p = 0; p < 4; p++) {
                pair(&c, &x, NULL, 0);
                if (x.face_shown == 1 && back < 0) {
                    back = p;
                }
            }
            CHECK(back >= 0 && back <= 1 && !(x.flags & SA1_STATE_SUPER), "after SA1_ORDER_NORMAL: face_shown 1 from pair %d "
                  "(within 2)", back);
            destroy(&c);
        } else {
            g_fail++;
        }
    }
    {
        sa1_status st, cs;
        sa1_character_id id = create(g_ctx, SA1_CHAR_SONIC, SA1_CREATE_PUPPET, 0, 0, &cs);
        st = sa1_character_face_mode(g_ctx, id, SA1_FACE_MODE_EVENT);
        CHECK(id != 0 && st == SA1_ERR_UNSUPPORTED, "a puppet: %s", sa1_status_string(st));
        if (id) {
            sa1_character_destroy(g_ctx, id);
        }
    }
}

/* ==== F11 pace ====================================================================================================== */
static void t_pace(void)
{
    fc c;
    sa1_state_ex x;
    uint32_t orig[31], n, same_n = 0;
    int p, nc = 0, nc0 = 0;
    printf("pace (F11): PACE_CUTSCENE doubles the game's step times\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    for (n = 0; n < 31; n++) {
        orig[n] = g32(c.w, A_FACETAB + 4u * n);
    }
    sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT | SA1_FACE_PACE_CUTSCENE);
    pairs(&c, 2);
    sa1_character_face(g_ctx, c.id, "C", SA1_FACE_NOW);
    for (p = 0; p < 60; p++) {
        pair(&c, &x, NULL, 0);
        nc += x.face_letter == 2 ? 1 : 0;
    }
    CHECK(nc == 48 && (x.face_mode & SA1_FACE_PACE_CUTSCENE), "paced 'C': %d pairs (48), face_mode %#x", nc,
          (unsigned)x.face_mode);
    sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT);
    pairs(&c, 2);
    for (n = 0; n < 31; n++) {
        same_n += g32(c.w, A_FACETAB + 4u * n) == orig[n] ? 1u : 0u;
    }
    CHECK(same_n == 31, "pace off: the table's 31 pointers equal the image's (%u)", (unsigned)same_n);
    sa1_character_face(g_ctx, c.id, "C", SA1_FACE_NOW);
    for (p = 0; p < 40; p++) {
        pair(&c, &x, NULL, 0);
        nc0 += x.face_letter == 2 ? 1 : 0;
    }
    CHECK(nc0 == 24, "'C' at the gameplay pace: %d pairs (24)", nc0);
    destroy(&c);
}

/* ==== F12 event_off_cancel =========================================================================================== */
static void t_event_off_cancel(void)
{
    fc c;
    sa1_state_ex x;
    pinfo pi;
    int p, iseen = 0, waiting = 0, evafter = 0, seqafter = 0;
    printf("event_off_cancel (F12): a hit during event faces\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT);
    pairs(&c, 4);
    hurt(&c);
    for (p = 0; p < 10; p++) {
        pair(&c, &x, NULL, 0);
        iseen += x.face_letter == 8 ? 1 : 0;
        waiting += wseq(&c) != 0 ? 1 : 0;
    }
    CHECK(iseen == 0 && waiting > 0, "while EVENT stays on the hit's 'I' never shows (%d pairs) - it waits behind the event "
          "face (%d pairs; the game's priority)", iseen, waiting);
    sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_GAME);
    for (p = 0; p < 30; p++) {
        pair(&c, &x, &pi, 0);
        if (p >= 2) {
            evafter += pi.ev;
            seqafter += wseq(&c) != 0 ? 1 : 0;
        }
    }
    CHECK(evafter == 0 && seqafter == 0, "EVENT off: no 'I' afterwards (EV draws %d, gameplay sequence pairs %d: "
          "PFaceCancel)", evafter, seqafter);
    destroy(&c);
}

/* ==== F15 sequence_timing (and its timeline for F20) ================================================================= */
typedef struct tl {
    int32_t from, to, letter;
    uint32_t shown, act;
    float rate;
} tl;

static tl g_f15[25];
static int g_f15_ok;

static void t_sequence_timing(void)
{
    fc c;
    sa1_state_ex x;
    int p, bad = 0;
    printf("sequence_timing (F15): GAME {Z, 6, 30}\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    CHECK(seq1(&c, 25, 6, 30, 0) == SA1_OK, "{Z, 6, 30} accepted");
    for (p = 0; p < 25; p++) {
        pair(&c, &x, NULL, 0);
        g_f15[p].from = x.face_from;
        g_f15[p].to = x.face_to;
        g_f15[p].letter = x.face_letter;
        g_f15[p].shown = x.face_shown;
        g_f15[p].act = x.face_activity;
        g_f15[p].rate = x.face_rate;
        if (g_verbose) {
            printf("    pair %2d: %d>%d@%.3f letter %d shown %u act %u\n", p, x.face_from, x.face_to, (double)x.face_rate,
                   x.face_letter, (unsigned)x.face_shown, (unsigned)x.face_activity);
        }
        if (p < 3) {
            bad += (x.face_from == 0 && x.face_to == 8 && x.face_letter == 25 && x.face_shown == 1 &&
                    x.face_activity == SA1_FACE_ACT_SEQUENCE && fabsf(x.face_rate - (float)(2 * p + 1) / 6.0f) < 1e-6f)
                       ? 0 : 1;
        } else if (p < 18) {
            bad += (x.face_from == 8 && x.face_to == 8 && x.face_letter == -2 && x.face_shown == 1 &&
                    x.face_activity == SA1_FACE_ACT_SEQUENCE) ? 0 : 1;
        } else {
            bad += (x.face_shown == 0 && x.face_activity == SA1_FACE_ACT_NONE && x.face_letter == -1) ? 0 : 1;
        }
    }
    CHECK(bad == 0, "face 0 -> 8 over 6 steps (pairs 0-2, letter 25), 8 held 30 steps (pairs 3-17, letter -2), then the "
          "regular face (activity SEQUENCE then NONE): %d pairs off", bad);
    g_f15_ok = bad == 0;
    destroy(&c);
}

/* ==== F16 sequence_game_timing ======================================================================================= */
static void t_sequence_game_timing(void)
{
    fc a, b;
    sa1_state_ex xa, xb;
    int p, diff = 0, ca = 0, cb = 0;
    printf("sequence_game_timing (F16): {C, 0, 0} = the game's \"C\"\n");
    if (setup(g_ctx, &a, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0 || setup(g_ctx, &b, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    seq1(&a, 2, 0, 0, 0);
    sa1_character_face(g_ctx, b.id, "C", SA1_FACE_NOW);
    for (p = 0; p < 28; p++) {
        pair(&a, &xa, NULL, 0);
        pair(&b, &xb, NULL, 0);
        diff += (xa.face_from == xb.face_from && xa.face_to == xb.face_to && xa.face_rate == xb.face_rate &&
                 xa.face_shown == xb.face_shown) ? 0 : 1;
        ca += xa.face_letter == 2 ? 1 : 0;
        cb += xb.face_letter == 2 ? 1 : 0;
    }
    CHECK(diff == 0 && ca == 24 && cb == 24, "28 pairs: the same face_from / face_to / face_rate (%d pairs differ); both "
          "show 'C' for %d / %d pairs (24)", diff, ca, cb);
    destroy(&a);
    destroy(&b);
}

/* ==== F17 sequence_event ============================================================================================= */
static void t_sequence_event(void)
{
    fc c;
    sa1_state_ex x;
    sa1_face_entry e[2];
    int p, let[80], act[80], r27 = 0, rh = 0, r0 = 0, k = 0, idle = 1;
    printf("sequence_event (F17): EVENT {b, 10, 90}, {A, 12, 0}\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT);
    pairs(&c, 4);
    memset(e, 0, sizeof e);
    e[0].anim = 27;
    e[0].enter = 10;
    e[0].hold = 90;
    e[1].anim = 0;
    e[1].enter = 12;
    CHECK(sa1_character_face_sequence(g_ctx, c.id, e, 2, 0) == SA1_OK, "accepted");
    for (p = 0; p < 80; p++) {
        pair(&c, &x, NULL, 0);
        let[p] = x.face_letter;
        act[p] = (int)x.face_activity;
    }
    while (k < 80 && let[k] == 27) {
        r27++;
        k++;
    }
    while (k < 80 && let[k] == -2) {
        rh++;
        k++;
    }
    while (k < 80 && let[k] == 0 && act[k] == SA1_FACE_ACT_SEQUENCE) {
        r0++;
        k++;
    }
    for (p = k; p < 80; p++) {
        idle &= (let[p] == 0 || let[p] == 1) && act[p] == SA1_FACE_ACT_NONE;
    }
    CHECK(r27 == 5 && rh == 45 && r0 == 6 && idle && k == 56, "letter 27 for %d pairs (10 steps), -2 for %d (90), 0 for %d "
          "(12), then the idle (0 / 1, activity NONE) from pair %d", r27, rh, r0, k);
    destroy(&c);
}

/* ==== F18 now_over_long_step ========================================================================================= */
static void t_now_long_step(void)
{
    fc c;
    sa1_state_ex x;
    fblock b;
    int p, resumed = -1;
    printf("now_over_long_step (F18): NOW into 'N' and over lip sync\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    sa1_character_face(g_ctx, c.id, "N", 0);
    pairs(&c, 3);
    fb_read(&c, &b);
    sa1_character_face(g_ctx, c.id, "Z", SA1_FACE_NOW);
    pair(&c, &x, NULL, 0);
    CHECK(b.count == 5 && b.time == 48 && b.next == 11 && x.face_from == (int32_t)b.face && x.face_to == 8 &&
              x.face_letter == 25, "'N' (48, 11) at count %d: the snap to its start key %u (rate 0.10 -> 0), then Z (%d>%d)",
          b.count, b.face, x.face_from, x.face_to);
    sa1_character_face(g_ctx, c.id, NULL, 0);
    pairs(&c, 4);
    for (p = 0; p < 3; p++) {
        sa1_character_face_level(g_ctx, c.id, 0.5f, SA1_FACE_LEVEL_LADDER);
        pair(&c, NULL, NULL, 0);
    }
    sa1_character_face(g_ctx, c.id, "V", SA1_FACE_NOW);
    sa1_character_face_level(g_ctx, c.id, 0.5f, SA1_FACE_LEVEL_LADDER);
    pair(&c, &x, NULL, 0);
    CHECK(x.face_letter == 21 && x.face_activity == SA1_FACE_ACT_SCRIPT, "NOW over lip sync: 'V' from the next step (letter "
          "%d, activity %u)", x.face_letter, (unsigned)x.face_activity);
    for (p = 0; p < 14; p++) {
        sa1_character_face_level(g_ctx, c.id, 0.5f, SA1_FACE_LEVEL_LADDER);
        pair(&c, &x, NULL, 0);
        if (x.face_activity == SA1_FACE_ACT_LIPSYNC && resumed < 0) {
            resumed = p;
        }
    }
    CHECK(resumed >= 7 && resumed <= 9, "lip sync resumes after 'V' (16 steps): pair %d", resumed);
    destroy(&c);
}

/* ==== F19 hold_last ================================================================================================== */
static void t_hold_last(void)
{
    fc c;
    sa1_state_ex x;
    int p, bad = 0;
    printf("hold_last (F19): {b, 10, 0} HOLD_LAST\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    seq1(&c, 27, 10, 0, SA1_FACE_HOLD_LAST);
    for (p = 0; p < 600; p++) {
        pair(&c, &x, NULL, 0);
        if (p >= 5) {
            bad += (x.face_from == 17 && x.face_to == 17 && x.face_activity == SA1_FACE_ACT_HELD && x.face_shown == 1 &&
                    x.face_letter == -2) ? 0 : 1;
        }
    }
    CHECK(bad == 0, "key 17 held for 600 pairs (the held step re-armed every 127 steps), activity HELD: %d pairs off", bad);
    seq1(&c, 25, 6, 0, 0);
    pair(&c, &x, NULL, 0);
    CHECK(x.face_from == 17 && x.face_to == 8 && x.face_activity == SA1_FACE_ACT_SEQUENCE,
          "a new face animation starts from 17 (%d>%d)", x.face_from, x.face_to);
    destroy(&c);
}

/* ==== F20 priority =================================================================================================== */
static void t_priority(void)
{
    fc c;
    sa1_state_ex x;
    int p, diff = 0, lipfrom = -1, held = -1, closing = 0, ladder = 0;
    printf("priority (F20): a face animation over lip sync; lip sync over a held face\n");
    if (!g_f15_ok) {
        printf("  (sequence_timing did not run or failed: its timeline is the reference)\n");
    }
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    seq1(&c, 25, 6, 30, 0);
    for (p = 0; p < 25; p++) {
        sa1_character_face_level(g_ctx, c.id, 0.8f, SA1_FACE_LEVEL_LADDER);
        pair(&c, &x, NULL, 0);
        if (p < 18 && g_f15_ok) {
            diff += (x.face_from == g_f15[p].from && x.face_to == g_f15[p].to && x.face_rate == g_f15[p].rate &&
                     x.face_letter == g_f15[p].letter && x.face_shown == g_f15[p].shown) ? 0 : 1;
        }
        if (x.face_activity == SA1_FACE_ACT_LIPSYNC && lipfrom < 0) {
            lipfrom = p;
        }
    }
    CHECK(g_f15_ok && diff == 0 && lipfrom == 18, "lip sync at 0.8 during F15's sequence: its timeline unchanged (%d pairs "
          "differ), lip sync from pair %d (the pair after its end, 18)", diff, lipfrom);
    sa1_character_face_level(g_ctx, c.id, -1.0f, 0);
    pairs(&c, 6);
    seq1(&c, 27, 10, 0, SA1_FACE_HOLD_LAST);
    pairs(&c, 8);
    for (p = 0; p < 6; p++) {
        sa1_character_face_level(g_ctx, c.id, 0.6f, SA1_FACE_LEVEL_LADDER);
        pair(&c, &x, NULL, 0);
        ladder += (x.face_activity == SA1_FACE_ACT_LIPSYNC && x.face_letter == -2 && x.face_from != 17) ? 1 : 0;
    }
    CHECK(ladder == 6, "a held face + lip sync: the mouth ladder shows (%d of 6 pairs)", ladder);
    for (p = 0; p < 14; p++) {
        sa1_character_face_level(g_ctx, c.id, -1.0f, SA1_FACE_LEVEL_LADDER);
        pair(&c, &x, NULL, 0);
        if (p < 3) {
            closing += x.face_to == 0 ? 1 : 0;
        }
        if (x.face_activity == SA1_FACE_ACT_HELD && x.face_from == 17 && x.face_to == 17 && held < 0) {
            held = p;
        }
    }
    CHECK(closing == 3 && held >= 6 && held <= 8 && x.face_activity == SA1_FACE_ACT_HELD && x.face_to == 17,
          "level -1: the close (6 steps, %d pairs to 0), then {8, 17} {127, 17}: HELD on 17 again from pair %d", closing,
          held);
    destroy(&c);
}

/* ==== F21 sequence_args ============================================================================================== */
static void t_sequence_args(void)
{
    fc c;
    sa1_state_ex x;
    sa1_face_entry e[33];
    sa1_status st[8];
    int i, ok = 1;
    printf("sequence_args (F21): refusals of sa1_character_face_sequence\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    seq1(&c, 25, 6, 60, 0);
    pairs(&c, 2);
    memset(e, 0, sizeof e);
    for (i = 0; i < 33; i++) {
        e[i].anim = 2;
    }
    st[0] = sa1_character_face_sequence(g_ctx, c.id, e, 33, 0);
    e[0].anim = 31;
    st[1] = sa1_character_face_sequence(g_ctx, c.id, e, 1, 0);
    e[0].anim = 2;
    e[0].enter = 128;
    st[2] = sa1_character_face_sequence(g_ctx, c.id, e, 1, 0);
    e[0].enter = 0;
    e[0].hold = 3601;
    st[3] = sa1_character_face_sequence(g_ctx, c.id, e, 1, 0);
    e[0].hold = 0;
    e[0].reserved = 1;
    st[4] = sa1_character_face_sequence(g_ctx, c.id, e, 1, 0);
    e[0].reserved = 0;
    e[0].reserved2 = 1;
    st[5] = sa1_character_face_sequence(g_ctx, c.id, e, 1, 0);
    e[0].reserved2 = 0;
    st[6] = sa1_character_face_sequence(g_ctx, c.id, e, 1, 0x8u);
    st[7] = sa1_character_face_sequence(g_ctx, c.id, NULL, 1, 0);
    for (i = 0; i < 8; i++) {
        ok &= st[i] == SA1_ERR_INVALID_ARGUMENT;
    }
    pair(&c, &x, NULL, 0);
    CHECK(ok && x.face_activity == SA1_FACE_ACT_SEQUENCE && x.face_to == 8,
          "count 33, anim 31, enter 128, hold 3601, reserved 1, reserved2 1, flags 8, entries NULL: SA1_ERR_INVALID_ARGUMENT, "
          "nothing applied (the running {Z} goes on: %d>%d)", x.face_from, x.face_to);
    CHECK(sa1_character_face_sequence(g_ctx, c.id, NULL, 0, 0) == SA1_OK, "count 0 = stop: accepted");
    pairs(&c, 1);
    pair(&c, &x, NULL, 0);
    CHECK(x.face_activity == SA1_FACE_ACT_NONE && x.face_shown == 0, "stopped: the regular face (activity %u, shown %u)",
          (unsigned)x.face_activity, (unsigned)x.face_shown);
    destroy(&c);
}

/* ==== F22 hurt_during ================================================================================================ */
static void t_hurt_during(void)
{
    fc c;
    sa1_state_ex x;
    pinfo pi;
    int p, iseen = 0, waited = 0, end = -1, evafter = 0, seqafter = 0;
    printf("hurt_during (F22): GAME, a hit during a face animation\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    seq1(&c, 25, 6, 90, 0);
    pairs(&c, 4);
    hurt(&c);
    for (p = 0; p < 70 && end < 0; p++) {
        pair(&c, &x, NULL, 0);
        iseen += x.face_letter == 8 ? 1 : 0;
        waited += wseq(&c) != 0 ? 1 : 0;
        if (x.face_activity == SA1_FACE_ACT_NONE) {
            end = p;
        }
    }
    CHECK(end > 0 && iseen == 0 && waited > 0, "the 'I' waits behind the face animation (%d pairs; shown in %d) until its end "
          "(pair %d)", waited, iseen, end);
    for (p = 0; p < 30; p++) {
        pair(&c, &x, &pi, 0);
        if (p >= 1) {
            evafter += pi.ev;
            seqafter += wseq(&c) != 0 ? 1 : 0;
        }
    }
    CHECK(evafter == 0 && seqafter == 0, "no 'I' after it (EV draws %d, gameplay sequence pairs %d: PFaceCancel at its "
          "end)", evafter, seqafter);
    destroy(&c);
}

/* ==== F23 identity =================================================================================================== */
typedef struct idrec {
    float pos[3], spd[3];
    int32_t mode, action, rings;
    uint32_t rand;
} idrec;

static void id_input(fc *c, int p)
{
    c->stick_x = (float)sin(p * 0.013);
    c->stick_y = (float)cos(p * 0.013) * (p % 300 < 250 ? 1.0f : 0.0f);
    c->buttons = (p % 97) < 3 ? SA1_BUTTON_JUMP : 0u;
}

/* the calls of the identity run: GAME-mode face animations, scripts without 'A', lip sync (both styles), holds */
static void id_calls(fc *c, int p, int with_a)
{
    static const sa1_face_entry e1[2] = { { 2, 0, 0, 0, 0 }, { 21, 0, 4, 20, 0 } };   /* anim, reserved, enter, hold, reserved2 */
    static const sa1_face_entry e2[1] = { { 25, 0, 6, 40, 0 } };
    if (p == 100 && with_a) {
        sa1_character_face(c->ctx, c->id, "A", SA1_FACE_NOW);
        return;
    }
    if (with_a) {
        return;
    }
    switch (p % 400) {
    case 10: sa1_character_face_sequence(c->ctx, c->id, e1, 2, 0); break;
    case 60: sa1_character_face(c->ctx, c->id, "CDE", SA1_FACE_NOW); break;
    case 120: sa1_character_face(c->ctx, c->id, "KL", 0); break;
    case 170: sa1_character_face(c->ctx, c->id, "Z", SA1_FACE_LOOP | SA1_FACE_NOW); break;
    case 200: sa1_character_face(c->ctx, c->id, NULL, 0); break;
    case 230: sa1_character_face_sequence(c->ctx, c->id, e2, 1, SA1_FACE_HOLD_LAST); break;
    case 300: sa1_character_face_sequence(c->ctx, c->id, NULL, 0, 0); break;
    default: break;
    }
    if (p % 400 >= 320 && p % 400 < 360) {
        sa1_character_face_level(c->ctx, c->id, (float)fabs(sin(p * 0.4)), SA1_FACE_LEVEL_LADDER);
    } else if (p % 400 >= 360 && p % 400 < 390) {
        sa1_character_face_level(c->ctx, c->id, 0.5f, SA1_FACE_LEVEL_TALK);
    } else if (p % 400 == 390) {
        sa1_character_face_level(c->ctx, c->id, -1.0f, SA1_FACE_LEVEL_TALK);
    }
}

static int id_run(int variant, idrec *out, int n)
{
    fc c;
    sa1_state_ex x;
    int p;
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        return -1;
    }
    if (variant == 2) {
        sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT);
    }
    for (p = 0; p < n; p++) {
        id_input(&c, p);
        if (variant >= 1) {
            id_calls(&c, p, variant == 3);
        }
        if (pair(&c, &x, NULL, 0) != 0) {
            destroy(&c);
            return -1;
        }
        memcpy(out[p].pos, x.sa1_position, sizeof out[p].pos);
        memcpy(out[p].spd, x.speed, sizeof out[p].spd);
        out[p].mode = x.mode;
        out[p].action = x.action;
        out[p].rings = x.rings;
        out[p].rand = x.rand_state;
    }
    destroy(&c);
    return 0;
}

static void t_identity(void)
{
    enum { N = 1300 };
    static idrec ref[N], run[N];
    int v, p;
    printf("identity (F23): face calls change nothing of the game's state (rand only through the idle)\n");
    if (id_run(0, ref, N) != 0) {
        g_fail++;
        return;
    }
    for (v = 1; v <= 3; v++) {
        int state_diff = -1, rand_diff = -1;
        if (id_run(v, run, N) != 0) {
            g_fail++;
            continue;
        }
        for (p = 0; p < N; p++) {
            if (state_diff < 0 && (memcmp(ref[p].pos, run[p].pos, sizeof ref[p].pos) ||
                                   memcmp(ref[p].spd, run[p].spd, sizeof ref[p].spd) || ref[p].mode != run[p].mode ||
                                   ref[p].action != run[p].action || ref[p].rings != run[p].rings)) {
                state_diff = p;
            }
            if (rand_diff < 0 && ref[p].rand != run[p].rand) {
                rand_diff = p;
            }
        }
        if (v == 1) {
            CHECK(state_diff < 0 && rand_diff < 0, "GAME mode with face animations, scripts, lip sync and holds: position, "
                  "speed, mode, action, rings and rand identical in all %d pairs (first differing pair %d / rand %d)", N,
                  state_diff, rand_diff);
        } else if (v == 2) {
            CHECK(state_diff < 0 && rand_diff >= 0 && rand_diff <= 8, "EVENT mode: position, speed, mode, action, rings "
                  "identical (first differing pair %d); rand from the first idle sequence on: pair %d", state_diff,
                  rand_diff);
        } else {
            CHECK(rand_diff == 100, "a script with 'A' (the game's random idle choice) at pair 100: rand differs from pair "
                  "%d", rand_diff);
        }
    }
}

/* ==== F24 rebuild_memory ============================================================================================= */
static int rebuild(sa1_context *ctx, sa1g_world *w)
{
    sa1g_boot_cfg cfg;
    uint32_t notes = 0;
    int32_t land = w->land_mode;
    uint32_t cap = w->window_cap;
    float pos[3];
    uint32_t ang = w->start_ang_y;
    memcpy(pos, w->start_pos, sizeof pos);
    if (sa1g_world_apply_image(w, w->image) != SA1_OK) {
        return -1;
    }
    sa1g_boot_defaults(&cfg);
    cfg.chr = 0;
    cfg.flags |= SA1G_BOOT_POS;
    memcpy(cfg.pos, pos, sizeof cfg.pos);
    cfg.ang[1] = ang;
    cfg.land_mode = land;
    cfg.window_cap = cap;
    if (sa1g_world_boot(w, &cfg) != SA1_OK) {
        return -1;
    }
    return sa1ca_world_attach(ctx, w, &notes) == SA1_OK ? 0 : -1;
}

static void t_rebuild_memory(void)
{
    fc c;
    sa1_state_ex x;
    ga_stats g0, g1;
    size_t ext10 = 0, ext250 = 0, extr = 0, ext_end = 0;
    int i, back = -1, bad = 0;
    printf("rebuild_memory (F24): 500 calls of each kind around an image rebuild (guard allocator: %s)\n",
           ga_enabled() ? "on" : "OFF - run with SA1_GUARD_ALLOC=1");
    /* the same world once without face calls (with its rebuild): the context's own caches are made before g0 */
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0 || rebuild(g_ctx, c.w) != 0 || pairs(&c, 4) != 0) {
        g_fail++;
        destroy(&c);
        return;
    }
    destroy(&c);
    ga_stats_get(&g0);
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    for (i = 0; i < 500; i++) {
        sa1_status s[4];
        s[0] = sa1_character_face_mode(g_ctx, c.id, (i % 7) == 3 ? SA1_FACE_MODE_EVENT : SA1_FACE_MODE_GAME);
        s[1] = sa1_character_face(g_ctx, c.id, (i % 3) ? "CD" : "Z", (i % 2) ? SA1_FACE_NOW : SA1_FACE_LOOP);
        s[2] = seq1(&c, (i * 7) % 31, i % 128, (i * 13) % 300, (i % 5) == 0 ? SA1_FACE_HOLD_LAST : 0u);
        s[3] = sa1_character_face_level(g_ctx, c.id, (i % 4) ? 0.3f : -1.0f, (uint32_t)(i % 2));
        bad += (s[0] == SA1_OK && s[1] == SA1_OK && s[2] == SA1_OK && s[3] == SA1_OK) ? 0 : 1;
        if (pair(&c, &x, NULL, 0) != 0) {
            bad++;
            break;
        }
        if (i == 10) {
            ext10 = c.w->ext_used;
        }
        if (i == 249) {
            ext250 = c.w->ext_used;
            sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT);
            if (rebuild(g_ctx, c.w) != 0) {
                printf("    rebuild failed: %s\n", c.w->dead ? c.w->fault.msg : "boot");
                bad++;
                break;
            }
            extr = c.w->ext_used;
            for (back = 0; back < 3; back++) {
                pair(&c, &x, NULL, 0);
                if (x.face_mode == SA1_FACE_MODE_EVENT && x.face_shown == 1) {
                    break;
                }
            }
        }
    }
    ext_end = c.w->ext_used;
    CHECK(bad == 0, "500 x {mode, script, sequence, level} + a pair each: every call SA1_OK, every pair ran (%d not)", bad);
    CHECK(back >= 0 && back <= 1, "after the image rebuild EVENT mode is back within 2 pairs (pair %d: persist[0])", back);
    CHECK(ext10 == ext250 && ext_end <= extr + 65536u, "segment 0x18: %u bytes after 10 calls, %u after 250 (no growth), "
          "%u after the rebuild, %u at the end (one block per world)", (unsigned)ext10, (unsigned)ext250, (unsigned)extr,
          (unsigned)ext_end);
    destroy(&c);
    ga_stats_get(&g1);
    CHECK(!ga_enabled() || g1.live == g0.live, "after destroy: %llu live host blocks (%llu before; the same world without "
          "face calls was made and destroyed first: the lane allocates no host memory)", (unsigned long long)g1.live,
          (unsigned long long)g0.live);
}

/* ==== F25 game_calls_only ============================================================================================ */
typedef struct gc_rec {
    uint8_t fb[16], fw[32];
    int ev, st, key;
    float pos[3];
    int32_t mode, action;
    uint32_t rand;
} gc_rec;

static int gc_run(int calls, gc_rec *out, int n)
{
    fc c;
    sa1_state_ex x;
    pinfo pi;
    int p;
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 0) != 0) {
        return -1;
    }
    if (calls) {
        sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_GAME);
    }
    for (p = 0; p < n; p++) {
        if (calls) {
            sa1_character_face_level(g_ctx, c.id, -1.0f, SA1_FACE_LEVEL_LADDER);
        }
        if (p == 30) {
            hurt(&c);
        }
        if (pair(&c, &x, &pi, 0) != 0) {
            destroy(&c);
            return -1;
        }
        memset(&out[p], 0, sizeof out[p]);
        if (mapped(c.ewp)) {
            memcpy(out[p].fb, sa1g_world_ptr(c.w, c.ewp + 0x5Cu), 16);
        }
        if (mapped(c.pfp)) {
            memcpy(out[p].fw, sa1g_world_ptr(c.w, c.pfp), 32);
        }
        out[p].ev = pi.ev;
        out[p].st = pi.st;
        out[p].key = pi.key;
        memcpy(out[p].pos, x.sa1_position, sizeof out[p].pos);
        out[p].mode = x.mode;
        out[p].action = x.action;
        out[p].rand = x.rand_state;
    }
    if (calls) {
        CHECK(c.w->face.pre != NULL, "the calls installed the face hook");
    }
    destroy(&c);
    return 0;
}

static void t_game_calls_only(void)
{
    enum { N = 200 };
    static gc_rec a[N], b[N];
    int p, diff = -1, hurtpairs = 0;
    printf("game_calls_only (F25): FaceMode(GAME) + FaceLevel(-1) every pair = the game\n");
    if (gc_run(0, a, N) != 0 || gc_run(1, b, N) != 0) {
        g_fail++;
        return;
    }
    for (p = 0; p < N; p++) {
        if (diff < 0 && memcmp(&a[p], &b[p], sizeof a[p]) != 0) {
            diff = p;
        }
        hurtpairs += a[p].ev > 0 ? 1 : 0;
    }
    CHECK(diff < 0 && hurtpairs >= 6, "the face block, the face work, the drawn faces (the hurt 'I' in %d pairs), position, "
          "mode, action and rand equal the no-call world pair for pair (first difference %d)", hurtpairs, diff);
}

/* ==== F26 over_gameplay_face ========================================================================================= */
static void t_over_gameplay_face(void)
{
    int lip;
    printf("over_gameplay_face (F26): a host face started during the hurt 'I'\n");
    for (lip = 0; lip < 2; lip++) {
        fc c;
        sa1_state_ex x;
        pinfo pi[4];
        int p, before;
        if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
            g_fail++;
            continue;
        }
        hurt(&c);
        pairs(&c, 4);
        before = wseq(&c) != 0;
        if (lip) {
            sa1_character_face_level(g_ctx, c.id, 0.7f, SA1_FACE_LEVEL_LADDER);
        } else {
            seq1(&c, 25, 6, 30, 0);
        }
        for (p = 0; p < 4; p++) {
            if (lip) {
                sa1_character_face_level(g_ctx, c.id, 0.7f, SA1_FACE_LEVEL_LADDER);
            }
            pair(&c, &x, &pi[p], 1);
        }
        if (!lip) {
            CHECK(before && wseq(&c) == 0 && pi[2].ev > 0 && pi[2].ka >= 0 &&
                      ((pi[2].ka == 0 && pi[2].kb == 8) || (pi[2].ka == 8 && pi[2].kb == 0)) && pi[1].key != 9 &&
                      pi[1].ka != 11 && pi[1].kb != 11,
                  "{Z, 6, 30} during 'I': the 'I' dropped at the start, Z's first step drawn from the 2nd pair after the call "
                  "(%d>%d@%.2f; pair 1: %d EV draws, key %d)", pi[2].ka, pi[2].kb, (double)pi[2].t, pi[1].ev, pi[1].key);
        } else {
            /* round 0.9.4c: the level 0.7 on Sonic's ladder 0 4 16 (sa1_face_info) */
            sa1_face_info fi = face_info_of(SA1_CHAR_SONIC);
            uint32_t ka, kb, r64;
            int on_ladder;
            lad_pos(&fi, 0.7f, &ka, &kb, &r64);
            on_ladder = pi[2].ev > 0 && (pi[2].ka == (int)ka || pi[2].ka == (int)kb || pi[2].kb == (int)ka ||
                                         pi[2].kb == (int)kb);
            CHECK(before && wseq(&c) == 0 && on_ladder && x.face_activity == SA1_FACE_ACT_LIPSYNC,
                  "lip sync started during 'I': the 'I' dropped, the mouth (%d>%d@%.2f) drawn from the 2nd pair",
                  pi[2].ka, pi[2].kb, (double)pi[2].t);
        }
        destroy(&c);
    }
}

/* ==== F27 talk_game ================================================================================================== */
static void t_talk_game(void)
{
    fc c;
    sa1_state_ex x;
    pinfo pi;
    int p, bad = 0, stat = 0, closing = 0, end = -1;
    printf("talk_game (F27): GAME mode, style TALK\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    for (p = 0; p < 100; p++) {
        sa1_character_face_level(g_ctx, c.id, 0.6f, SA1_FACE_LEVEL_TALK);
        pair(&c, &x, &pi, 0);
        bad += (x.face_shown == 1 && x.face_letter >= 2 && x.face_letter <= 4 &&
                x.face_activity == SA1_FACE_ACT_LIPSYNC) ? 0 : 1;
        if (p >= 1) {
            stat += pi.st;
        }
    }
    CHECK(bad == 0 && stat == 0, "100 pairs: C / D / E on the EV head in every pair (%d not), never the static face (%d "
          "static draws)", bad, stat);
    for (p = 0; p < 8; p++) {
        sa1_character_face_level(g_ctx, c.id, -1.0f, SA1_FACE_LEVEL_TALK);
        pair(&c, &x, NULL, 0);
        if (p < 3) {
            closing += (x.face_to == 0 && x.face_letter == -2) ? 1 : 0;
        }
        if (x.face_shown == 0 && end < 0) {
            end = p;
        }
    }
    CHECK(closing == 3 && end == 3 && x.face_activity == SA1_FACE_ACT_NONE, "level -1: closes over 6 steps (%d pairs), then "
          "the regular face (pair %d)", closing, end);
    destroy(&c);
}

/* ==== F28 shown_matches_draws (an extra check of face_shown, review R.5) ============================================== */
static void t_shown_draws(void)
{
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_BIG };
    size_t i;
    printf("shown_matches_draws (F28): EVENT mode while jumping, rolling, flying, gliding: face_shown after a pair = the EV "
           "head drawn in the next pair\n");
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        fc c;
        sa1_state_ex x;
        pinfo pi;
        int p, prev = -1, cmp = 0, bad = 0, off = 0, trans = 0, a;
        int32_t pact = -1;
        int hidden[160];                /* face_shown 0 pairs per action (the verbose summary) */
        memset(hidden, 0, sizeof hidden);
        if (setup(g_ctx, &c, kinds[i], 0, 0, 0, 12) != 0) {
            g_fail++;
            continue;
        }
        sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_EVENT);
        for (p = 0; p < 360; p++) {
            int ph = p % 90;
            c.stick_x = 0.0f;
            c.stick_y = ph < 70 ? 1.0f : 0.0f;
            c.buttons = (ph >= 20 && ph < 22) || (ph >= 30 && ph < 60) ? SA1_BUTTON_JUMP : 0u;
            if (pair(&c, &x, &pi, 0) != 0) {
                break;
            }
            /* face_shown predicts the next pair's draw from the action after the pair: a pair whose logic changes the
               action before its display (landing, starting to roll) may draw another tree - excused, counted */
            if (prev >= 0 && (pi.ev > 0 || pi.st > 0) && x.action != pact) {
                trans += (prev == 1) == (pi.ev > 0) ? 0 : 1;
            } else if (prev >= 0 && (pi.ev > 0 || pi.st > 0)) {
                cmp++;
                bad += (prev == 1) == (pi.ev > 0) ? 0 : 1;
                if ((prev == 1) != (pi.ev > 0) && g_verbose) {
                    printf("    pair %d: face_shown %d before, EV draws %d, static draws %d, action %d\n", p, prev, pi.ev,
                           pi.st, (int)x.action);
                }
            }
            off += x.face_shown == 0 ? 1 : 0;
            if (x.face_shown == 0 && x.action >= 0 && x.action < 160) {
                hidden[x.action]++;
            }
            prev = (int)x.face_shown;
            pact = x.action;
        }
        CHECK(bad == 0 && cmp > 200, "%s: %d pairs compared, %d differ (face_shown 0 after %d pairs: ball, other models; "
              "%d pairs that changed the action before their display differ, not counted)", k_name[c.kind], cmp, bad, off,
              trans);
        if (g_verbose) {
            printf("    face_shown 0 by action:");
            for (a = 0; a < 160; a++) {
                if (hidden[a]) {
                    printf(" %d x%d", a, hidden[a]);
                }
            }
            printf("\n");
        }
        destroy(&c);
    }
}

/* ==== F14 threads (and FACE_TIMELINE) ================================================================================ */
enum { TL_PAIRS = 200 };

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h = (h ^ b[i]) * 0x100000001B3ull;
    }
    return h;
}

/* a fixed face scenario: every kind of call, both modes, the pace; one hash per pair (face fields, rand, position) */
static int tl_run(sa1_context *ctx, uint64_t *out)
{
    static const sa1_face_entry e[2] = { { 2, 0, 0, 0, 0 }, { 27, 0, 10, 20, 0 } };
    fc c;
    sa1_state_ex x;
    int p;
    if (setup(ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        return -1;
    }
    for (p = 0; p < TL_PAIRS; p++) {
        uint64_t h = 0xCBF29CE484222325ull;
        if (p == 5) {
            sa1_character_face_sequence(ctx, c.id, e, 2, 0);
        } else if (p >= 40 && p < 60) {
            sa1_character_face_level(ctx, c.id, (float)(p - 40) / 19.0f, SA1_FACE_LEVEL_LADDER);
        } else if (p == 60) {
            sa1_character_face_level(ctx, c.id, -1.0f, SA1_FACE_LEVEL_LADDER);
        } else if (p == 70) {
            sa1_character_face(ctx, c.id, "Z", SA1_FACE_LOOP | SA1_FACE_NOW);
        } else if (p == 90) {
            sa1_character_face(ctx, c.id, NULL, 0);
        } else if (p == 100) {
            sa1_character_face_mode(ctx, c.id, SA1_FACE_MODE_EVENT);
        } else if (p == 110) {
            sa1_character_face(ctx, c.id, "CDE", SA1_FACE_NOW);
        } else if (p == 150) {
            sa1_character_face_mode(ctx, c.id, SA1_FACE_MODE_EVENT | SA1_FACE_PACE_CUTSCENE);
        } else if (p == 160) {
            sa1_character_face(ctx, c.id, "K", SA1_FACE_NOW);
        } else if (p == 190) {
            sa1_character_face_mode(ctx, c.id, SA1_FACE_MODE_GAME);
        }
        if (pair(&c, &x, NULL, 0) != 0) {
            destroy(&c);
            return -1;
        }
        h = fnv(h, &x.face_mode, sizeof x.face_mode);
        h = fnv(h, &x.face_shown, sizeof x.face_shown);
        h = fnv(h, &x.face_from, sizeof x.face_from);
        h = fnv(h, &x.face_to, sizeof x.face_to);
        h = fnv(h, &x.face_rate, sizeof x.face_rate);
        h = fnv(h, &x.face_letter, sizeof x.face_letter);
        h = fnv(h, &x.face_activity, sizeof x.face_activity);
        h = fnv(h, &x.rand_state, sizeof x.rand_state);
        h = fnv(h, x.sa1_position, sizeof x.sa1_position);
        out[p] = h;
    }
    destroy(&c);
    return 0;
}

typedef struct tl_job {
    uint64_t h[TL_PAIRS];
    int rc;
} tl_job;

static void tl_thread(void *arg)
{
    tl_job *j = (tl_job *)arg;
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    j->rc = -1;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = 1u << SA1_CHAR_SONIC;
    lo.flags = SA1_LOAD_DEFAULT;
    if (!SA1_FAILED(sa1_load_disc(ctx, g_gdi, &lo)) && scene(ctx) == 0) {
        j->rc = tl_run(ctx, j->h);
    }
    sa1_shutdown(ctx);
}

typedef void (*thr_fn)(void *);
typedef struct thr {
    thr_fn fn;
    void *arg;
#if defined(_WIN32)
    HANDLE h;
#else
    pthread_t t;
#endif
} thr;

#if defined(_WIN32)
static DWORD WINAPI thr_main(LPVOID p)
{
    thr *t = (thr *)p;
    t->fn(t->arg);
    return 0;
}
#else
static void *thr_main(void *p)
{
    thr *t = (thr *)p;
    t->fn(t->arg);
    return NULL;
}
#endif

static int thr_start(thr *t, thr_fn fn, void *arg)
{
    t->fn = fn;
    t->arg = arg;
#if defined(_WIN32)
    t->h = CreateThread(NULL, 0, thr_main, t, 0, NULL);
    return t->h != NULL ? 0 : -1;
#else
    return pthread_create(&t->t, NULL, thr_main, t);
#endif
}

static void thr_join(thr *t)
{
#if defined(_WIN32)
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
#else
    pthread_join(t->t, NULL);
#endif
}

static void t_threads(void)
{
    static tl_job main_job, j1, j2;
    thr a, b;
    uint64_t total = 0xCBF29CE484222325ull;
    int p, d1 = -1, d2 = -1;
    printf("threads (F14): two worlds on two threads, the same calls; FACE_TIMELINE for x86 = x64\n");
    main_job.rc = tl_run(g_ctx, main_job.h);
    if (main_job.rc != 0) {
        g_fail++;
        return;
    }
    for (p = 0; p < TL_PAIRS; p++) {
        total = fnv(total, &main_job.h[p], sizeof main_job.h[p]);
    }
    printf("FACE_TIMELINE %016llx\n", (unsigned long long)total);
    if (thr_start(&a, tl_thread, &j1) != 0 || thr_start(&b, tl_thread, &j2) != 0) {
        g_fail++;
        return;
    }
    thr_join(&a);
    thr_join(&b);
    for (p = 0; p < TL_PAIRS; p++) {
        if (d1 < 0 && j1.h[p] != main_job.h[p]) {
            d1 = p;
        }
        if (d2 < 0 && j2.h[p] != main_job.h[p]) {
            d2 = p;
        }
    }
    CHECK(j1.rc == 0 && j2.rc == 0 && d1 < 0 && d2 < 0, "two threads, two contexts: the face timelines equal the main "
          "thread's in all %d pairs (first difference %d / %d)", TL_PAIRS, d1, d2);
}

/* ==== F29 mouth_block ================================================================================================= */
static void t_mouth_block(void)
{
    size_t ki;
    printf("mouth_block (F29): the host's mouth = the ladder's writes with the host's keys, drawn one pair later\n");
    for (ki = 0; ki < 5; ki++) {
        fc c;
        sa1_state_ex x;
        pinfo pi;
        mentry e[64];
        int n = table_of(k_face5[ki], e), i, blockbad = 0, drawbad = 0;
        double worst = 0;
        sa1_face_info fi = face_info_of(k_face5[ki]);
        if (setup(g_ctx, &c, k_face5[ki], 0, 0, 0, 12) != 0) {
            g_fail++;
            continue;
        }
        while (n < 9 + 50) {                    /* 50 seeded random mouths with keys below the count */
            e[n].a = (int)(lcg() % fi.keys);
            e[n].b = (int)(lcg() % fi.keys);
            e[n].w = (float)(lcg() % 4097u) / 4096.0f;
            n++;
        }
        for (i = 0; i < 3; i++) {               /* GAME mode: the EV head is linked after the first talking pair */
            sa1_character_face_mouth(g_ctx, c.id, 0, 0, 0.0f, 0);
            pair(&c, NULL, NULL, 0);
        }
        for (i = 0; i <= n; i++) {
            if (i < n) {
                sa1_status st = sa1_character_face_mouth(g_ctx, c.id, (uint32_t)e[i].a, (uint32_t)e[i].b, e[i].w, 0);
                blockbad += st == SA1_OK ? 0 : 1;
            }
            pair(&c, &x, &pi, 0);
            if (i < n) {
                int ok = m_block_ok(&x, e[i].a, e[i].b, m_r64(e[i].w));
                blockbad += ok ? 0 : 1;
                if (!ok || g_verbose) {
                    printf("    %s %d>%d@%.4f: block %d>%d@%.4f letter %d act %u%s\n", k_name[c.kind], e[i].a, e[i].b,
                           (double)e[i].w, x.face_from, x.face_to, (double)x.face_rate, x.face_letter,
                           (unsigned)x.face_activity, ok ? "" : "  <- FAIL");
                }
            }
            if (i > 0) {
                double r = rms_from(&c, &pi, (uint32_t)e[i - 1].a, (uint32_t)e[i - 1].b, m_drawn_t(m_r64(e[i - 1].w), 0));
                worst = r > worst ? r : worst;
                drawbad += r < 1e-4 ? 0 : 1;
                if (r >= 1e-4) {
                    printf("    %s pair %d: drawn rms %.6f from %d>%d@%.4f\n", k_name[c.kind], i, r, e[i - 1].a, e[i - 1].b,
                           m_drawn_t(m_r64(e[i - 1].w), 0));
                }
            }
        }
        CHECK(blockbad == 0, "%s: %d mouths (the 4.3 table and 50 random): the face block after each pair is the written "
              "position + 2 steps, letter -2, LIPSYNC (%d not)", k_name[c.kind], n, blockbad);
        CHECK(drawbad == 0, "%s: drawn at pair n + 1 = lerp(a, b, min(r64 + 1, 64) / 64) (worst rms %.7f, %d over 1e-4)",
              k_name[c.kind], worst, drawbad);
        destroy(&c);
    }
    /* the mouth IS the ladder's mechanism: a level and the mouth call that lands on the same keys and r64, two worlds */
    for (ki = 0; ki < 5; ki++) {
        fc a, b;
        sa1_face_info fi = face_info_of(k_face5[ki]);
        int p, diff = 0, steps = 0;
        if (setup(g_ctx, &a, k_face5[ki], 0, 0, 0, 12) != 0 || setup(g_ctx, &b, k_face5[ki], 0, 0, 0, 12) != 0) {
            g_fail++;
            destroy(&a);
            continue;
        }
        for (p = 0; p < 40; p++) {
            float lv = (float)((p * 37) % 41) / 40.0f;
            uint32_t ka, kb, r64;
            pinfo pa, pb;
            fblock fa, fb2;
            lad_pos(&fi, lv, &ka, &kb, &r64);
            sa1_character_face_level(g_ctx, a.id, lv, SA1_FACE_LEVEL_LADDER);
            sa1_character_face_mouth(g_ctx, b.id, ka, kb, (float)r64 / 64.0f, 0);
            pair(&a, NULL, &pa, 0);
            pair(&b, NULL, &pb, 0);
            fb_read(&a, &fa);
            fb_read(&b, &fb2);
            diff += (memcmp(&fa, &fb2, sizeof fa) == 0 && mapped(a.pfp) && mapped(b.pfp) &&
                     memcmp(sa1g_world_ptr(a.w, a.pfp), sa1g_world_ptr(b.w, b.pfp), 32) == 0 && pa.npts == pb.npts &&
                     memcmp(pa.pts, pb.pts, 12u * pa.npts) == 0 && pa.ev == pb.ev) ? 0 : 1;
            steps++;
        }
        CHECK(diff == 0, "%s: %d pairs of level calls and the mouth calls landing there (two worlds): the face block, the "
              "face work and the drawn points bit-identical (%d differ)", k_name[a.kind], steps, diff);
        destroy(&a);
        destroy(&b);
    }
}

/* ==== F30 mouth_args ================================================================================================== */
static void t_mouth_args(void)
{
    fc c;
    sa1_state_ex x;
    sa1_status st;
    int p, alive = 1, logs;
    size_t ki;
    printf("mouth_args (F30): refusals of sa1_character_face_mouth\n");
    if (setup(g_ctx, &c, SA1_CHAR_BIG, 0, 0, 0, 12) == 0) {
        for (p = 0; p < 3; p++) {
            sa1_character_face_mouth(g_ctx, c.id, 0, 4, 1.0f, 0);
            pair(&c, NULL, NULL, 0);
        }
        logs = g_logs;
        st = sa1_character_face_mouth(g_ctx, c.id, 8, 0, 0.5f, 0);
        CHECK(st == SA1_ERR_INVALID_ARGUMENT && g_logs == logs &&
                  strstr(sa1_last_error(g_ctx), "key 8 is not one of Big's 8 face keys (0-7)") != NULL,
              "Big key_a 8: SA1_ERR_INVALID_ARGUMENT, no log line (\"%s\")", sa1_last_error(g_ctx));
        st = sa1_character_face_mouth(g_ctx, c.id, 0, 8, 0.5f, 0);
        CHECK(st == SA1_ERR_INVALID_ARGUMENT && g_logs == logs, "Big key_b 8: SA1_ERR_INVALID_ARGUMENT, no log line");
        st = sa1_character_face_mouth(g_ctx, c.id, 0, 200, 0.5f, 0);
        CHECK(st == SA1_ERR_INVALID_ARGUMENT, "Big key_b 200: SA1_ERR_INVALID_ARGUMENT");
        pair(&c, &x, NULL, 0);
        CHECK(m_block_ok(&x, 0, 4, 64), "nothing latched: the running mouth 0>4@1 at the next pair (%d>%d@%.4f)",
              x.face_from, x.face_to, (double)x.face_rate);
        for (p = 0; p < 100; p++) {
            sa1_character_face_mouth(g_ctx, c.id, 8, 15, 1.0f, 0);
            if (pair(&c, &x, NULL, 0) != 0 || (x.flags & SA1_STATE_FAULTED)) {
                alive = 0;
                break;
            }
        }
        CHECK(alive && m_block_ok(&x, 0, 4, 64), "the world is alive after 100 pairs of refused calls, the mouth still 0>4");
        st = sa1_character_face_mouth(g_ctx, c.id, 0, 4, 1.5f, 0);
        pair(&c, &x, NULL, 0);
        CHECK(st == SA1_OK && m_block_ok(&x, 0, 4, 64), "weight 1.5 acts as 1 (%d>%d@%.4f)", x.face_from, x.face_to,
              (double)x.face_rate);
        sa1_character_face_mouth(g_ctx, c.id, 0, 4, 0.5f, 0);
        pair(&c, &x, NULL, 0);
        st = sa1_character_face_mouth(g_ctx, c.id, 0, 4, -0.5f, 0);
        pair(&c, &x, NULL, 0);
        CHECK(st == SA1_OK && x.face_to == 0 && x.face_letter == -2 && x.face_activity == SA1_FACE_ACT_LIPSYNC,
              "weight -0.5 = off: the close (to %d, letter %d)", x.face_to, x.face_letter);
        pairs(&c, 4);
        state(&c, &x);
        CHECK(x.face_activity == SA1_FACE_ACT_NONE && x.face_shown == 0, "then the regular face (activity %u, shown %u)",
              (unsigned)x.face_activity, (unsigned)x.face_shown);
        st = sa1_character_face_mouth(g_ctx, c.id, 0, 4, NAN, 0);
        CHECK(st == SA1_ERR_INVALID_ARGUMENT, "a NaN weight: SA1_ERR_INVALID_ARGUMENT (\"%s\")", sa1_last_error(g_ctx));
        CHECK(sa1_character_face_mouth(g_ctx, c.id, 0, 4, 0.5f, 0x1u) == SA1_ERR_INVALID_ARGUMENT &&
                  sa1_character_face_mouth(g_ctx, c.id, 0, 4, 0.5f, 0x400u) == SA1_ERR_INVALID_ARGUMENT,
              "flags 0x1 and 0x400: SA1_ERR_INVALID_ARGUMENT");
#if defined(SA1_FACE_SAME_PAIR)
        CHECK(sa1_character_face_mouth(g_ctx, c.id, 0, 4, 0.5f, SA1_FACE_SAME_PAIR) == SA1_OK,
              "SA1_FACE_SAME_PAIR (0x200, built): accepted");
#else
        CHECK(sa1_character_face_mouth(g_ctx, c.id, 0, 4, 0.5f, 0x200u) == SA1_ERR_INVALID_ARGUMENT,
              "SAME_PAIR not built: 0x200 refused as an unknown flag (review R.2)");
#endif
        logs = g_logs;
        CHECK(sa1_character_face_level(g_ctx, c.id, 0.5f, SA1_FACE_LEVEL_TALK | 0x200u) == SA1_ERR_INVALID_ARGUMENT &&
                  sa1_character_face_level(g_ctx, c.id, 0.5f, 3u) == SA1_ERR_INVALID_ARGUMENT && g_logs == logs,
              "sa1_character_face_level with TALK | 0x200 and with style 3: SA1_ERR_INVALID_ARGUMENT, no log line (\"%s\")",
              sa1_last_error(g_ctx));
        CHECK(sa1_character_face_level(g_ctx, c.id, 0.5f, SA1_FACE_LEVEL_LADDER | T_SP) == SA1_OK,
              "sa1_character_face_level with LADDER%s: accepted", T_SP ? " | SA1_FACE_SAME_PAIR" : "");
        destroy(&c);
    } else {
        g_fail++;
    }
    for (ki = 0; ki < 4; ki++) {
        uint32_t lim = ki == 0 ? 21u : 20u;
        if (setup(g_ctx, &c, k_face5[ki], 0, 0, 0, 2) != 0) {
            g_fail++;
            continue;
        }
        st = sa1_character_face_mouth(g_ctx, c.id, lim, 0, 0.5f, 0);
        CHECK(st == SA1_ERR_INVALID_ARGUMENT && sa1_character_face_mouth(g_ctx, c.id, 0, lim - 1u, 0.5f, 0) == SA1_OK,
              "%s: key %u refused (\"%s\"), %u accepted", k_name[c.kind], (unsigned)lim, sa1_last_error(g_ctx),
              (unsigned)(lim - 1u));
        destroy(&c);
    }
    {
        static const int32_t kinds[] = { SA1_CHAR_GAMMA, SA1_CHAR_SUPER_SONIC };
        for (ki = 0; ki < 2; ki++) {
            if (setup(g_ctx, &c, kinds[ki], 0, 0, 0, 2) != 0) {
                g_fail++;
                continue;
            }
            st = sa1_character_face_mouth(g_ctx, c.id, 0, 4, 0.5f, 0);
            CHECK(st == SA1_ERR_UNSUPPORTED && c.w->face.pre == NULL, "%s: SA1_ERR_UNSUPPORTED, no hook (\"%s\")",
                  k_name[c.kind], sa1_last_error(g_ctx));
            destroy(&c);
        }
    }
    {
        sa1_status cs;
        sa1_character_id id = create(g_ctx, SA1_CHAR_SONIC, SA1_CREATE_PUPPET, 0, 0, &cs);
        st = sa1_character_face_mouth(g_ctx, id, 0, 4, 0.5f, 0);
        CHECK(id != 0 && st == SA1_ERR_UNSUPPORTED, "a puppet: %s", sa1_status_string(st));
        if (id) {
            sa1_character_destroy(g_ctx, id);
        }
    }
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, SA1_CREATE_SUPER_CAPABLE, 1, SA1_SUPER_GENERAL | SA1_SUPER_START, 6) == 0) {
        int shown = 0, sup = 0;
        for (p = 0; p < 6; p++) {
            st = sa1_character_face_mouth(g_ctx, c.id, 0, 7, 1.0f, T_SP);
            pair(&c, &x, NULL, 0);
            shown += (int)x.face_shown;
            sup += (x.flags & SA1_STATE_SUPER) ? 1 : 0;
        }
        CHECK(st == SA1_OK && sup == 6 && shown == 0, "a General Sonic while Super: accepted, face_shown 0 (%d Super pairs)",
              sup);
        destroy(&c);
    } else {
        g_fail++;
    }
}

/* ==== F31 mouth_mix =================================================================================================== */
static void t_mouth_mix(void)
{
    fc c;
    sa1_state_ex x;
    sa1_face_info fi = face_info_of(SA1_CHAR_SONIC);
    sa1wf_stats s0, s1;
    int p, bad = 0, actbad = 0;
    uint32_t ka, kb, r64;
    printf("mouth_mix (F31): level and mouth calls mixed pair by pair: the last before a pair wins, no restart\n");
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    sa1_character_face_mouth(g_ctx, c.id, 0, 7, 0.5f, 0);
    pair(&c, NULL, NULL, 0);
    s0 = stats_of(&c);
    lad_pos(&fi, 0.6f, &ka, &kb, &r64);
    for (p = 0; p < 40; p++) {
        int ea, eb;
        uint32_t er;
        switch (p % 4) {
        case 0:
            sa1_character_face_level(g_ctx, c.id, 0.6f, SA1_FACE_LEVEL_LADDER);
            ea = (int)ka; eb = (int)kb; er = r64;
            break;
        case 1:
            sa1_character_face_mouth(g_ctx, c.id, 0, 7, 0.5f, 0);
            ea = 0; eb = 7; er = m_r64(0.5f);
            break;
        case 2:                                         /* two calls before one pair: the mouth (the last) wins */
            sa1_character_face_level(g_ctx, c.id, 0.6f, SA1_FACE_LEVEL_LADDER);
            sa1_character_face_mouth(g_ctx, c.id, 6, 16, 0.25f, 0);
            ea = 6; eb = 16; er = m_r64(0.25f);
            break;
        default:                                        /* the level (the last) wins */
            sa1_character_face_mouth(g_ctx, c.id, 6, 16, 0.25f, 0);
            sa1_character_face_level(g_ctx, c.id, 0.6f, SA1_FACE_LEVEL_LADDER);
            ea = (int)ka; eb = (int)kb; er = r64;
            break;
        }
        pair(&c, &x, NULL, 0);
        bad += m_block_ok(&x, ea, eb, er) ? 0 : 1;
        actbad += x.face_activity == SA1_FACE_ACT_LIPSYNC && x.face_letter == -2 ? 0 : 1;
        if (g_verbose || !m_block_ok(&x, ea, eb, er)) {
            printf("    pair %d: expected %d>%d r64 %u, block %d>%d@%.4f letter %d act %u\n", p, ea, eb, (unsigned)er,
                   x.face_from, x.face_to, (double)x.face_rate, x.face_letter, (unsigned)x.face_activity);
        }
    }
    s1 = stats_of(&c);
    CHECK(bad == 0 && actbad == 0, "40 pairs alternating level / mouth (and two calls before a pair): the last call before "
          "each pair wins (%d not), LIPSYNC with the lip-sync step throughout (%d not: no close {6, 0})", bad, actbad);
    CHECK(s1.cancels == s0.cancels && s1.requests == s0.requests, "no restart: PFaceCancel %u -> %u, requests %u -> %u",
          (unsigned)s0.cancels, (unsigned)s1.cancels, (unsigned)s0.requests, (unsigned)s1.requests);
    destroy(&c);
}

/* ==== F32 mouth_priority ============================================================================================== */
static void t_mouth_priority(void)
{
    fc c;
    sa1_state_ex x;
    int p, diff = 0, lipfrom = -1, held = -1, closing = 0, mouth = 0;
    printf("mouth_priority (F32): a face animation over the mouth; the mouth over a held face\n");
    if (!g_f15_ok) {
        printf("  (sequence_timing did not run or failed: its timeline is the reference)\n");
    }
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    seq1(&c, 25, 6, 30, 0);
    for (p = 0; p < 25; p++) {
        sa1_character_face_mouth(g_ctx, c.id, 0, 7, 0.8f, T_SP);
        pair(&c, &x, NULL, 0);
        if (p < 18 && g_f15_ok) {
            diff += (x.face_from == g_f15[p].from && x.face_to == g_f15[p].to && x.face_rate == g_f15[p].rate &&
                     x.face_letter == g_f15[p].letter && x.face_shown == g_f15[p].shown) ? 0 : 1;
        }
        if (x.face_activity == SA1_FACE_ACT_LIPSYNC && lipfrom < 0) {
            lipfrom = p;
        }
    }
    CHECK(g_f15_ok && diff == 0 && lipfrom == 18, "mouth calls (0>7@0.8%s) every pair during F15's sequence: its timeline "
          "unchanged (%d pairs differ), the mouth from pair %d (the pair after its end, 18)", T_SP ? ", SAME_PAIR" : "",
          diff, lipfrom);
    sa1_character_face_mouth(g_ctx, c.id, 0, 0, -1.0f, 0);
    pairs(&c, 6);
    seq1(&c, 27, 10, 0, SA1_FACE_HOLD_LAST);
    pairs(&c, 8);
    for (p = 0; p < 6; p++) {
        sa1_character_face_mouth(g_ctx, c.id, 0, 7, 0.6f, 0);
        pair(&c, &x, NULL, 0);
        mouth += (x.face_activity == SA1_FACE_ACT_LIPSYNC && x.face_letter == -2 && x.face_from == 0 &&
                  x.face_to == 7) ? 1 : 0;
    }
    CHECK(mouth == 6, "a held face + mouth calls: the mouth 0>7 shows (%d of 6 pairs)", mouth);
    for (p = 0; p < 14; p++) {
        sa1_character_face_mouth(g_ctx, c.id, 0, 7, -1.0f, 0);
        pair(&c, &x, NULL, 0);
        if (p < 3) {
            closing += x.face_to == 0 ? 1 : 0;
        }
        if (x.face_activity == SA1_FACE_ACT_HELD && x.face_from == 17 && x.face_to == 17 && held < 0) {
            held = p;
        }
    }
    CHECK(closing == 3 && held >= 6 && held <= 8 && x.face_activity == SA1_FACE_ACT_HELD && x.face_to == 17,
          "weight -1: the close (6 steps, %d pairs to 0), then {8, 17} {127, 17}: HELD on 17 again from pair %d", closing,
          held);
    destroy(&c);
}

/* ==== F33 same_pair =================================================================================================== */
/* (a) / (e): steady talking with the flag: drawn at pair n = lerp(a, b, r64 / 64) of the call latched before pair n */
static void sp_steady(int32_t kind, int ladder)
{
    fc c;
    pinfo pi;
    sa1_face_info fi = face_info_of(kind);
    int p, bad = 0, first = -1;
    double worst = 0;
    sa1wf_stats s;
    if (setup(g_ctx, &c, kind, 0, 0, 0, 12) != 0) {
        g_fail++;
        return;
    }
    for (p = 0; p < 40; p++) {
        uint32_t ka, kb, r64;
        double r;
        if (ladder) {
            float lv = (float)((p * 29) % 33) / 32.0f;
            lad_pos(&fi, lv, &ka, &kb, &r64);
            sa1_character_face_level(g_ctx, c.id, lv, SA1_FACE_LEVEL_LADDER | T_SP);
        } else {
            mentry e[64];
            int n = table_of(kind, e);
            const mentry *m = &e[(p * 5 + 1) % n];
            ka = (uint32_t)m->a;
            kb = (uint32_t)m->b;
            r64 = m_r64(m->w);
            sa1_character_face_mouth(g_ctx, c.id, ka, kb, m->w, T_SP);
        }
        pair(&c, NULL, &pi, 0);
        r = rms_from(&c, &pi, ka, kb, m_drawn_t(r64, 1));
        if (p == 0) {
            first = pi.ev > 0 && r < 1e-4;
        } else {
            worst = r > worst ? r : worst;
            bad += r < 1e-4 ? 0 : 1;
            if (r >= 1e-4 && bad < 4) {
                printf("    %s pair %d: drawn rms %.6f from %u>%u@%u/64 (ev draws %d)\n", k_name[kind], p, r, (unsigned)ka,
                       (unsigned)kb, (unsigned)r64, pi.ev);
            }
        }
    }
    s = stats_of(&c);
    CHECK(bad == 0 && s.sp_runs >= 40, "%s, %s with SAME_PAIR: drawn at pair n = lerp(a, b, r64 / 64) of the call latched "
          "before pair n in pairs 1-39 (worst rms %.7f, %d over 1e-4); step-0 blends %u, links %u, skips %u",
          k_name[kind], ladder ? "the ladder (LADDER | SAME_PAIR)" : "mouth calls", worst, bad, (unsigned)s.sp_runs,
          (unsigned)s.sp_links, (unsigned)s.sp_skips);
    CHECK(first == 1, "%s: the first talking pair (GAME mode) already draws the EV head with this pair's mouth (early link)",
          k_name[kind]);
    destroy(&c);
}

/* (b): the first talking pair after silence */
static void sp_first(int32_t kind, int event)
{
    fc a, b;
    pinfo pa, pb;
    int p;
    if (setup(g_ctx, &a, kind, 0, 0, 0, 12) != 0 || setup(g_ctx, &b, kind, 0, 0, 0, 12) != 0) {
        g_fail++;
        destroy(&a);
        return;
    }
    if (event) {
        sa1_character_face_mode(g_ctx, a.id, SA1_FACE_MODE_EVENT);
        sa1_character_face_mode(g_ctx, b.id, SA1_FACE_MODE_EVENT);
    }
    for (p = 0; p < 6; p++) {
        pair(&a, NULL, NULL, 0);
        pair(&b, NULL, NULL, 0);
    }
    sa1_character_face_mouth(g_ctx, a.id, 0, 7, 1.0f, 0);
    sa1_character_face_mouth(g_ctx, b.id, 0, 7, 1.0f, T_SP);
    pair(&a, NULL, &pa, 0);
    pair(&b, NULL, &pb, 0);
    if (!event) {
        CHECK(pb.ev > 0 && rms_from(&b, &pb, 0, 7, 1.0) < 1e-4 && pa.ev == 0 && pa.st > 0,
              "%s GAME, the first talking pair: with SAME_PAIR the EV head with this pair's mouth 0>7@1 (%d EV draws, rms "
              "%.7f); without: the static head (%d EV, %d static draws)", k_name[kind], pb.ev, rms_from(&b, &pb, 0, 7, 1.0),
              pa.ev, pa.st);
    } else {
        CHECK(pb.ev > 0 && rms_from(&b, &pb, 0, 7, 1.0) < 1e-4 && pa.ev > 0 && rms_from(&a, &pa, 0, 7, 1.0) > 1e-4,
              "%s EVENT, the first talking pair: with SAME_PAIR this pair's mouth (rms %.7f); without: the idle face (rms "
              "%.5f from the mouth)", k_name[kind], rms_from(&b, &pb, 0, 7, 1.0), rms_from(&a, &pa, 0, 7, 1.0));
    }
    destroy(&a);
    destroy(&b);
}

/* (c) identity: whole-arena ranges that differ (merged over the run) */
typedef struct drange {
    uint32_t lo, hi, pairs;
} drange;
typedef struct dset {
    drange r[64];
    int n, overflow;
} dset;

static void dset_add(dset *d, uint32_t lo, uint32_t hi)
{
    int i;
    for (i = 0; i < d->n; i++) {
        if (lo <= d->r[i].hi + 64u && hi + 64u >= d->r[i].lo) {
            d->r[i].lo = lo < d->r[i].lo ? lo : d->r[i].lo;
            d->r[i].hi = hi > d->r[i].hi ? hi : d->r[i].hi;
            d->r[i].pairs++;
            return;
        }
    }
    if (d->n < 64) {
        d->r[d->n].lo = lo;
        d->r[d->n].hi = hi;
        d->r[d->n].pairs = 1;
        d->n++;
    } else {
        d->overflow++;
    }
}

/* the differing byte runs of two memories of n bytes (guest base g0), outside [skip_lo, skip_hi); returns the bytes */
static size_t mem_diff(dset *d, const uint8_t *x, const uint8_t *y, size_t n, uint32_t g0, uint32_t skip_lo,
                       uint32_t skip_hi)
{
    size_t off, total = 0;
    if (memcmp(x, y, n) == 0) {
        return 0;
    }
    for (off = 0; off < n; off += 4096u) {
        size_t len = n - off < 4096u ? n - off : 4096u, i;
        if (memcmp(x + off, y + off, len) == 0) {
            continue;
        }
        for (i = 0; i < len; i++) {
            if (x[off + i] != y[off + i]) {
                size_t j = i, k;
                uint32_t g = g0 + (uint32_t)(off + i);
                for (k = i + 1; k < len && k <= j + 16u; k++) {     /* a run: differing bytes at most 16 apart */
                    if (x[off + k] != y[off + k]) {
                        j = k;
                    }
                }
                if (!(g >= skip_lo && g < skip_hi)) {
                    dset_add(d, g, g0 + (uint32_t)(off + j));
                    total += j - i + 1;
                }
                i = j;
            }
        }
    }
    return total;
}

static void sp_id_input(fc *c, int p)
{
    int ph = p % 97;
    c->stick_x = (float)sin(p * 0.013);
    c->stick_y = (float)cos(p * 0.013) * (p % 300 < 250 ? 1.0f : 0.0f);
    c->buttons = (ph < 3 || (ph >= 14 && ph < 30)) ? SA1_BUTTON_JUMP : 0u;    /* a jump, then (Tails) flight */
}

/* the calls of the identity run (both worlds; the flag only in the second); *cur = the mouth called (a -1: none, -2 a level) */
static void sp_id_calls(fc *c, int p, uint32_t fl, mentry *cur)
{
    mentry e[64];
    int n = table_of(c->kind, e);
    int talk = (p >= 20 && p < 120) || (p >= 150 && p < 260) || (p >= 304 && p < 400) || (p >= 430 && p < 520) ||
               (p >= 540 && p < 560) || (p >= 575 && p < 590);
    cur->a = -1;
    if (p == 140) {
        seq1(c, 25, 6, 30, 0);                  /* a face animation in between (the talking from 150 waits for its end) */
    }
    if (p == 300 || p == 470) {
        hurt(c);                                /* a hit 4 pairs before talking, a hit while talking */
    }
    if (talk) {
        if ((p / 7) % 5 == 3) {
            float lv = (float)((p * 13) % 17) / 16.0f;
            sa1_character_face_level(c->ctx, c->id, lv, SA1_FACE_LEVEL_LADDER | fl);
            cur->a = -2;
            cur->w = lv;
        } else {
            const mentry *m = &e[((p / 2) * 7 + 1) % n];
            sa1_character_face_mouth(c->ctx, c->id, (uint32_t)m->a, (uint32_t)m->b, m->w, fl);
            *cur = *m;
        }
    } else if (p == 120 || p == 260 || p == 400 || p == 520 || p == 560 || p == 590) {
        sa1_character_face_level(c->ctx, c->id, -1.0f, SA1_FACE_LEVEL_LADDER);
    }
}

static int works_differ(const fc *a, const fc *b)
{
    uint32_t mwa = g32(a->w, 0x8C78C628u), mwb = g32(b->w, 0x8C78C628u);     /* playermwp[0] */
    return (mapped(a->twp) && memcmp(sa1g_world_ptr(a->w, a->twp), sa1g_world_ptr(b->w, b->twp), 0x40) != 0) ||
           (mapped(mwa) && memcmp(sa1g_world_ptr(a->w, mwa), sa1g_world_ptr(b->w, mwb), 0x40) != 0) ||
           (mapped(a->pwp) && memcmp(sa1g_world_ptr(a->w, a->pwp), sa1g_world_ptr(b->w, b->pwp), 0x200) != 0) ||
           (mapped(a->ewp) && memcmp(sa1g_world_ptr(a->w, a->ewp), sa1g_world_ptr(b->w, b->ewp), 0x7C) != 0);
}

/* segment 0x18 runs that differ between the worlds a (no flag) and b (SAME_PAIR), each classified: the display capture of
   the pair (rt_draw.c's records, payload pool, header: what the step-0 draws recorded - draw scratch, read only by the host
   after the pair), b's face block (its counters), or bytes that differ between a and the control world c (the same calls,
   no flag) too (world-to-world, not the flag); anything else is unexplained */
typedef struct extcls {
    size_t capture, block, control, other;
    dset other_runs, control_runs;
} extcls;

static void ext_classify(extcls *k, const fc *a, const fc *b, const fc *c)
{
    const uint8_t *x = a->w->ext, *y = b->w->ext, *z = c->w->ext;
    size_t n = a->w->ext_used, off;
    sa1wf_stats s = stats_of(b);
    uint32_t cap = b->cap, rec = cap ? g32(b->w, cap + 0x20u) : 0u, rsz = cap ? g32(b->w, cap + 0x18u) : 0u;
    uint32_t ncap = cap ? g32(b->w, cap + 0x1Cu) : 0u, pay = cap ? g32(b->w, cap + 0x88u) : 0u;
    uint32_t paycap = cap ? g32(b->w, cap + 0x8Cu) : 0u;
    if (x == NULL || y == NULL || z == NULL || b->w->ext_used != n || c->w->ext_used != n || memcmp(x, y, n) == 0) {
        return;
    }
    for (off = 0; off < n; off += 4096u) {
        size_t len = n - off < 4096u ? n - off : 4096u, i;
        if (memcmp(x + off, y + off, len) == 0) {
            continue;
        }
        for (i = 0; i < len; i++) {
            size_t o = off + i;
            uint32_t g = EXT_GUEST + (uint32_t)o;
            if (x[o] == y[o]) {
                continue;
            }
            if (cap && ((g >= rec && g < rec + ncap * rsz) || (g >= pay && g < pay + paycap) || (g >= cap && g < cap + 0xA0u))) {
                k->capture++;
            } else if (g >= s.block && g < s.block + s.block_size) {
                k->block++;
            } else if (x[o] != z[o]) {
                k->control++;
                dset_add(&k->control_runs, g, g);
            } else {
                k->other++;
                dset_add(&k->other_runs, g, g);
            }
        }
    }
}

static void sp_identity(int32_t kind)
{
    enum { N = 600 };
    static uint8_t ea[0x20000], eb[0x20000];
    static dset arena;
    static extcls ek;
    fc a, b, c;
    sa1_state_ex xa, xb, xc;
    pinfo pa, pb, pc;
    int p, wk_diff = -1, fb_diff = -1, fw_diff = -1, ev_diff = -1, st_diff = -1, talkpairs = 0, drawdiff = 0, novac = 0;
    int evswitch = 0, i;
    size_t arena_bytes = 0;
    sa1wf_stats sb;
    memset(&arena, 0, sizeof arena);
    memset(&ek, 0, sizeof ek);
    if (setup(g_ctx, &a, kind, 0, 0, 0, 12) != 0 || setup(g_ctx, &b, kind, 0, 0, 0, 12) != 0 ||
        setup(g_ctx, &c, kind, 0, 0, 0, 12) != 0) {
        g_fail++;
        destroy(&a);
        destroy(&b);
        return;
    }
    for (p = 0; p < N; p++) {
        mentry ca, cb, cc;
        uint32_t runs0 = stats_of(&b).sp_runs;
        size_t na, nb;
        sp_id_input(&a, p);
        sp_id_input(&b, p);
        sp_id_input(&c, p);
        sp_id_calls(&a, p, 0u, &ca);
        sp_id_calls(&b, p, T_SP, &cb);
        sp_id_calls(&c, p, 0u, &cc);
        if (pair(&a, &xa, &pa, 0) != 0 || pair(&b, &xb, &pb, 0) != 0 || pair(&c, &xc, &pc, 0) != 0) {
            g_fail++;
            break;
        }
        if (st_diff < 0 && (memcmp(xa.sa1_position, xb.sa1_position, sizeof xa.sa1_position) ||
                            memcmp(xa.speed, xb.speed, sizeof xa.speed) || xa.mode != xb.mode || xa.action != xb.action ||
                            xa.rings != xb.rings || xa.rand_state != xb.rand_state)) {
            st_diff = p;
        }
        if (fb_diff < 0 && mapped(a.ewp) &&
            memcmp(sa1g_world_ptr(a.w, a.ewp + 0x5Cu), sa1g_world_ptr(b.w, b.ewp + 0x5Cu), 16) != 0) {
            fb_diff = p;
        }
        if (fw_diff < 0 && mapped(a.pfp) && memcmp(sa1g_world_ptr(a.w, a.pfp), sa1g_world_ptr(b.w, b.pfp), 0x20) != 0) {
            fw_diff = p;
        }
        if (wk_diff < 0 && works_differ(&a, &b)) {
            wk_diff = p;
        }
        na = ev_points(&a, ea, sizeof ea);
        nb = ev_points(&b, eb, sizeof eb);
        if (ev_diff < 0 && (na != nb || memcmp(ea, eb, na) != 0)) {
            ev_diff = p;
        }
        arena_bytes += mem_diff(&arena, a.w->ram, b.w->ram, SA1G_ARENA_SIZE, 0x8C000000u, 0u, 0u);
        ext_classify(&ek, &a, &b, &c);
        /* NON-VACUITY (review R.3): a pair whose step-0 blend ran draws this pair's mouth, so its drawn EV head differs
           from the world without the flag (or only the flagged world drew the EV head: the early link) */
        if (stats_of(&b).sp_runs > runs0 && cb.a >= 0) {
            int same_draw = pa.npts == pb.npts && pa.npts > 0 && memcmp(pa.pts, pb.pts, 12u * pa.npts) == 0;
            int switched = (pa.ev > 0) != (pb.ev > 0);
            talkpairs++;
            evswitch += switched;
            if (switched || !same_draw) {
                drawdiff++;
            } else if (rms_from(&b, &pb, (uint32_t)cb.a, (uint32_t)cb.b, m_drawn_t(m_r64(cb.w), 1)) >= 1e-4) {
                novac++;                        /* the flagged world did not draw its own mouth */
            } else if (g_verbose) {
                printf("    pair %d: both worlds drew the same mouth (%d>%d@%.3f: the two expected mouths coincide)\n", p,
                       cb.a, cb.b, (double)cb.w);
            }
        }
    }
    sb = stats_of(&b);
    CHECK(st_diff < 0 && fb_diff < 0 && fw_diff < 0 && ev_diff < 0 && wk_diff < 0,
          "%s, %d pairs (spurts, silences, a face animation, a hit before and one while talking): position, speed, mode, "
          "action, rings, rand, the face block, the face work (0x20), the EV head's points and every byte of player 0's "
          "taskwk / motionwk / playerwk / eventwk identical after every pair with and without SAME_PAIR (first differing "
          "pair: state %d, block %d, work %d, points %d, works %d)", k_name[kind], N, st_diff, fb_diff, fw_diff, ev_diff,
          wk_diff);
    printf("    arena: %d differing runs%s (%llu bytes over the run)\n", arena.n, arena.overflow ? " (more)" : "",
           (unsigned long long)arena_bytes);
    for (i = 0; i < arena.n; i++) {
        printf("      %08X-%08X (%u hits)\n", (unsigned)arena.r[i].lo, (unsigned)arena.r[i].hi, (unsigned)arena.r[i].pairs);
    }
    printf("    segment 0x18, bytes x pairs that differ: %llu in the display capture (rt_draw.c's records / payload / "
           "header at %08X: what the step-0 draws recorded, draw scratch), %llu in the face block %08X+%X (its counters), "
           "%llu that differ between two worlds without the flag too (world-to-world, not the flag), %llu unexplained\n",
           (unsigned long long)ek.capture, (unsigned)b.cap, (unsigned long long)ek.block, (unsigned)sb.block,
           (unsigned)sb.block_size, (unsigned long long)ek.control, (unsigned long long)ek.other);
    for (i = 0; i < ek.control_runs.n && i < 8; i++) {
        printf("      world-to-world: %08X-%08X\n", (unsigned)ek.control_runs.r[i].lo, (unsigned)ek.control_runs.r[i].hi);
    }
    for (i = 0; i < ek.other_runs.n && i < 16; i++) {
        printf("      UNEXPLAINED: %08X-%08X (%u bytes x pairs)\n", (unsigned)ek.other_runs.r[i].lo,
               (unsigned)ek.other_runs.r[i].hi, (unsigned)ek.other_runs.r[i].pairs);
    }
    CHECK(arena.n == 0 && arena.overflow == 0 && ek.other == 0,
          "%s: the whole arena (16 MB) identical after every pair (%d runs differ); segment 0x18: only draw records, the "
          "face block and world-to-world bytes differ (%llu unexplained bytes x pairs)", k_name[kind], arena.n,
          (unsigned long long)ek.other);
    CHECK(sb.sp_runs > 100 && talkpairs > 100 && novac == 0 && drawdiff * 10 >= talkpairs * 9,
          "%s: NON-VACUITY: %d talking pairs with a step-0 blend (%u blends, %u early links, %u guard skips, %u links put "
          "back): the step-0 drawn EV head differs from the world without the flag in %d (%d by the early link), the "
          "flagged world drew another mouth than its own in %d", k_name[kind], talkpairs, (unsigned)sb.sp_runs,
          (unsigned)sb.sp_links, (unsigned)sb.sp_skips, (unsigned)sb.sp_unlinks, drawdiff, evswitch, novac);
    destroy(&a);
    destroy(&b);
    destroy(&c);
}

/* (d) the guard: a gameplay face blends (rate_spd != 0) - nothing drawn early, the face work as without the flag */
static void sp_guard(void)
{
    fc a, b;
    pinfo pa, pb;
    int p, gp = 0, bad = 0, drawn_early = 0;
    sa1wf_stats s0, s1;
    if (setup(g_ctx, &a, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0 || setup(g_ctx, &b, SA1_CHAR_SONIC, 0, 0, 0, 12) != 0) {
        g_fail++;
        destroy(&a);
        return;
    }
    hurt(&a);
    hurt(&b);
    pairs(&a, 4);
    pairs(&b, 4);
    s0 = stats_of(&b);
    for (p = 0; p < 40; p++) {
        float spd = 0.0f;
        uint32_t v = mapped(b.pfp) ? g32(b.w, b.pfp + 8u) : 0u;
        memcpy(&spd, &v, 4);
        if (p == 20) {
            hurt(&a);
            hurt(&b);
        }
        sa1_character_face_mouth(g_ctx, a.id, 0, 7, 0.75f, 0);
        sa1_character_face_mouth(g_ctx, b.id, 0, 7, 0.75f, T_SP);
        pair(&a, NULL, &pa, 0);
        pair(&b, NULL, &pb, 0);
        if (spd != 0.0f) {
            gp++;
            bad += memcmp(sa1g_world_ptr(a.w, a.pfp), sa1g_world_ptr(b.w, b.pfp), 0x20) == 0 ? 0 : 1;
            drawn_early += (pa.ev != pb.ev || pa.npts != pb.npts || memcmp(pa.pts, pb.pts, 12u * pa.npts) != 0) ? 1 : 0;
        }
    }
    s1 = stats_of(&b);
    CHECK(gp > 0 && bad == 0 && drawn_early == 0 && s1.sp_skips - s0.sp_skips >= (uint32_t)gp,
          "the guard: a hit 4 pairs before talking (and one while talking): %d pairs began with a gameplay face blending "
          "(rate_spd != 0): the face work after them as without the flag (%d differ), nothing drawn early (%d), the guard "
          "skipped %u pairs", gp, bad, drawn_early, (unsigned)(s1.sp_skips - s0.sp_skips));
    destroy(&a);
    destroy(&b);
}

/* (f) Tails' face task exec (8C0BF796) picks W.parent by the player's timer.b[3] & 0x20 at every step: when that bit changes
   at the step that takes the early link, the face task links the other parent and sf_post puts the early link back. Both
   worlds get the same bit (a host write of the test), so they must stay identical: the whole arena after every pair */
static void sp_parent_switch(void)
{
    fc a, b;
    pinfo pa, pb;
    dset d;
    int p;
    size_t bytes = 0;
    sa1wf_stats s;
    memset(&d, 0, sizeof d);
    if (setup(g_ctx, &a, SA1_CHAR_TAILS, 0, 0, 0, 12) != 0 || setup(g_ctx, &b, SA1_CHAR_TAILS, 0, 0, 0, 12) != 0) {
        g_fail++;
        destroy(&a);
        return;
    }
    for (p = 0; p < 16; p++) {
        if (p == 6) {
            /* the bit the exec reads (taskwk.timer at 0x0C, b[3] at 0x0F): the talking pair's step 0 switches the parent */
            uint8_t *ta = (uint8_t *)sa1g_world_ptr(a.w, a.twp + 0x0Fu), *tb = (uint8_t *)sa1g_world_ptr(b.w, b.twp + 0x0Fu);
            *ta |= 0x20u;
            *tb |= 0x20u;
        }
        if (p >= 6) {
            sa1_character_face_mouth(g_ctx, a.id, 0, 7, 1.0f, 0);
            sa1_character_face_mouth(g_ctx, b.id, 0, 7, 1.0f, SA1_FACE_SAME_PAIR);
        }
        pair(&a, NULL, &pa, 0);
        pair(&b, NULL, &pb, 0);
        bytes += mem_diff(&d, a.w->ram, b.w->ram, SA1G_ARENA_SIZE, 0x8C000000u, 0u, 0u);
    }
    s = stats_of(&b);
    CHECK(d.n == 0 && s.sp_links >= 1 && s.sp_unlinks >= 1, "Tails: the parent switched (flight-model bit) at the step of "
          "the early link: the link put back after the step (%u links, %u put back), the whole arena identical with and "
          "without SAME_PAIR in all 16 pairs (%d runs, %llu bytes differ)", (unsigned)s.sp_links, (unsigned)s.sp_unlinks,
          d.n, (unsigned long long)bytes);
    destroy(&a);
    destroy(&b);
}

static void t_same_pair(void)
{
    printf("same_pair (F33): SA1_FACE_SAME_PAIR: the mouth drawn in the pair it is applied; game state identical\n");
#if !defined(SA1_FACE_SAME_PAIR)
    printf("  (SAME_PAIR not built: libsa1.h does not define SA1_FACE_SAME_PAIR - nothing to test)\n");
#else
    sp_steady(SA1_CHAR_SONIC, 0);
    sp_steady(SA1_CHAR_BIG, 0);
    sp_first(SA1_CHAR_SONIC, 0);
    sp_first(SA1_CHAR_SONIC, 1);
    sp_first(SA1_CHAR_TAILS, 0);
    sp_identity(SA1_CHAR_SONIC);
    sp_identity(SA1_CHAR_TAILS);
    sp_guard();
    sp_parent_switch();
    sp_steady(SA1_CHAR_SONIC, 1);
    sp_steady(SA1_CHAR_BIG, 1);
#endif
}

/* ==== F34 ladders ===================================================================================================== */
static void t_ladders(void)
{
    static const int32_t kinds[] = { 0, 2, 3, 5, 7, 4, 6, 8 };
    static const uint8_t lad[9][6] = { { 3, 0, 4, 16 }, { 0 }, { 4, 0, 4, 7, 8 }, { 4, 0, 4, 7, 16 }, { 5, 0, 5, 4, 6, 7 },
                                       { 5, 0, 7, 4, 8, 16 }, { 0 }, { 3, 0, 4, 5 }, { 0 } };
    size_t i;
    printf("ladders (F34): the per-kind loudness ladders (phoneme_plan.md 4.6)\n");
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        int32_t k = kinds[i];
        sa1_face_info fi = face_info_of(k);
        CHECK(fi.ladder_len == lad[k][0] && memcmp(fi.ladder, &lad[k][1], lad[k][0]) == 0,
              "%s: ladder_len %u: %u %u %u %u %u (expected %u)", k_name[k], (unsigned)fi.ladder_len, fi.ladder[0],
              fi.ladder[1], fi.ladder[2], fi.ladder[3], fi.ladder[4], (unsigned)lad[k][0]);
    }
    for (i = 0; i < 5; i++) {
        ladder_envelope(k_face5[i]);
    }
}

/* ==== F35 latency_pairs =============================================================================================== */
static void t_latency_pairs(void)
{
    size_t ki;
    printf("latency_pairs (F35): the first pair that draws an opened mouth: n with SAME_PAIR, n + 1 without\n");
#if !defined(SA1_FACE_SAME_PAIR)
    printf("  (SAME_PAIR not built)\n");
#else
    for (ki = 0; ki < 5; ki++) {
        fc a, b;
        pinfo pa, pb;
        int p, fa = -1, fb2 = -1, blk = 0;
        uint32_t kb = k_face5[ki] == SA1_CHAR_BIG ? 5u : 7u;
        if (setup(g_ctx, &a, k_face5[ki], 0, 0, 0, 12) != 0 || setup(g_ctx, &b, k_face5[ki], 0, 0, 0, 12) != 0) {
            g_fail++;
            destroy(&a);
            continue;
        }
        for (p = 0; p < 12; p++) {
            uint32_t k = p < 6 ? 0u : kb;
            float wv = p < 6 ? 0.0f : 1.0f;
            fblock x, y;
            sa1_character_face_mouth(g_ctx, a.id, 0, k, wv, 0);
            sa1_character_face_mouth(g_ctx, b.id, 0, k, wv, SA1_FACE_SAME_PAIR);
            pair(&a, NULL, &pa, 0);
            pair(&b, NULL, &pb, 0);
            fb_read(&a, &x);
            fb_read(&b, &y);
            blk += memcmp(&x, &y, sizeof x) == 0 ? 0 : 1;
            if (p >= 6) {
                if (fa < 0 && pa.ev > 0 && rms_from(&a, &pa, 0, 0, 0) > 1e-4) {
                    fa = p - 6;
                }
                if (fb2 < 0 && pb.ev > 0 && rms_from(&b, &pb, 0, 0, 0) > 1e-4) {
                    fb2 = p - 6;
                }
            }
        }
        CHECK(fb2 == 0 && fa == 1 && blk == 0, "%s: 0>%u@1 latched before pair n after closed talking: the first opened "
              "draw at n + %d with SAME_PAIR, n + %d without; the face blocks equal after every pair (%d differ)",
              k_name[k_face5[ki]], (unsigned)kb, fb2, fa, blk);
        destroy(&a);
        destroy(&b);
    }
#endif
}

/* ==== F36 no_call_mouth =============================================================================================== */
static int gc_run_mouth(int mode_call, gc_rec *out, int n, int *hook)
{
    fc c;
    sa1_state_ex x;
    pinfo pi;
    int p;
    if (setup(g_ctx, &c, SA1_CHAR_SONIC, 0, 0, 0, 0) != 0) {
        return -1;
    }
    if (mode_call) {
        sa1_character_face_mode(g_ctx, c.id, SA1_FACE_MODE_GAME);
    }
    for (p = 0; p < n; p++) {
        sa1_character_face_mouth(g_ctx, c.id, 0, 0, -1.0f, T_SP);
        if (p == 30) {
            hurt(&c);
        }
        if (pair(&c, &x, &pi, 0) != 0) {
            destroy(&c);
            return -1;
        }
        memset(&out[p], 0, sizeof out[p]);
        if (mapped(c.ewp)) {
            memcpy(out[p].fb, sa1g_world_ptr(c.w, c.ewp + 0x5Cu), 16);
        }
        if (mapped(c.pfp)) {
            memcpy(out[p].fw, sa1g_world_ptr(c.w, c.pfp), 32);
        }
        out[p].ev = pi.ev;
        out[p].st = pi.st;
        out[p].key = pi.key;
        memcpy(out[p].pos, x.sa1_position, sizeof out[p].pos);
        out[p].mode = x.mode;
        out[p].action = x.action;
        out[p].rand = x.rand_state;
    }
    *hook = c.w->face.pre != NULL;
    destroy(&c);
    return 0;
}

static void t_no_call_mouth(void)
{
    enum { N = 200 };
    static gc_rec a[N], b[N], d[N];
    int p, diff = -1, diff2 = -1, hook0 = 0, hook1 = 0, hurtpairs = 0;
    printf("no_call_mouth (F36): only sa1_character_face_mouth(.., -1, ..) = the game\n");
    if (gc_run(0, a, N) != 0 || gc_run_mouth(0, b, N, &hook0) != 0 || gc_run_mouth(1, d, N, &hook1) != 0) {
        g_fail++;
        return;
    }
    for (p = 0; p < N; p++) {
        if (diff < 0 && memcmp(&a[p], &b[p], sizeof a[p]) != 0) {
            diff = p;
        }
        if (diff2 < 0 && memcmp(&a[p], &d[p], sizeof a[p]) != 0) {
            diff2 = p;
        }
        hurtpairs += a[p].ev > 0 ? 1 : 0;
    }
    CHECK(!hook0 && diff < 0 && hurtpairs >= 6, "mouth -1 before every pair: no hook (face.pre NULL), and the face block, "
          "the face work, the drawn faces (the hurt 'I' in %d pairs), position, mode, action and rand equal F1's world "
          "pair for pair (first difference %d)", hurtpairs, diff);
    CHECK(hook1 && diff2 < 0, "F25 with mouth -1 calls (FaceMode GAME first: the hook installed): identical too (first "
          "difference %d)", diff2);
}

/* ==== main ============================================================================================================ */
int main(int argc, char **argv)
{
    static const struct { const char *name; void (*fn)(void); } tests[] = {
        { "info", t_info }, { "no_call", t_no_call }, { "event_mode", t_event_mode }, { "script_queue", t_script_queue },
        { "queue_vs_now", t_queue_now }, { "loop", t_loop }, { "game_end", t_game_end },
        { "lipsync_ladder", t_lipsync_ladder }, { "lipsync_off", t_lipsync_off }, { "refusals_big", t_refusals_big },
        { "no_face_kinds", t_no_face_kinds }, { "pace", t_pace }, { "event_off_cancel", t_event_off_cancel },
        { "threads", t_threads }, { "sequence_timing", t_sequence_timing },
        { "sequence_game_timing", t_sequence_game_timing }, { "sequence_event", t_sequence_event },
        { "now_over_long_step", t_now_long_step }, { "hold_last", t_hold_last }, { "priority", t_priority },
        { "sequence_args", t_sequence_args }, { "hurt_during", t_hurt_during }, { "identity", t_identity },
        { "rebuild_memory", t_rebuild_memory }, { "game_calls_only", t_game_calls_only },
        { "over_gameplay_face", t_over_gameplay_face }, { "talk_game", t_talk_game },
        { "shown_matches_draws", t_shown_draws },
        /* round 0.9.4c (phoneme_plan.md 6.1) */
        { "mouth_block", t_mouth_block }, { "mouth_args", t_mouth_args }, { "mouth_mix", t_mouth_mix },
        { "mouth_priority", t_mouth_priority }, { "same_pair", t_same_pair }, { "ladders", t_ladders },
        { "latency_pairs", t_latency_pairs }, { "no_call_mouth", t_no_call_mouth },
    };
    const char *only = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_status st;
    size_t i;
    int a, ran = 0;
    ga_args(&argc, argv);
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--verbose")) {
            g_verbose = 1;
        } else if (!strcmp(argv[a], "--only") && a + 1 < argc) {
            only = argv[++a];
        } else {
            g_gdi = argv[a];
        }
    }
    if (g_gdi == NULL) {
        g_gdi = getenv("SA1_TEST_GDI");
    }
    if (g_gdi == NULL) {
        printf("usage: face_test [--only NAME[,NAME...]] [--verbose] GDI\n");
        return 2;
    }
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_INFO;
    if (sa1_init(&h, &g_ctx) != SA1_OK) {
        printf("sa1_init failed\n");
        return 1;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(g_ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) |
                    (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SUPER_SONIC);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS;
    st = sa1_load_disc(g_ctx, g_gdi, &lo);
    if (SA1_FAILED(st) || scene(g_ctx) != 0) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(g_ctx));
        sa1_shutdown(g_ctx);
        return 1;
    }
    printf("libsa1 %s (SA1_API_FACE %d, SA1_API_FACE_MOUTH %d, SA1_FACE_SAME_PAIR %s)\n", sa1_version_string(),
           SA1_API_FACE, SA1_API_FACE_MOUTH, T_SP ? "built" : "not built");
    for (i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        if (only != NULL) {
            const char *p = strstr(only, tests[i].name);
            size_t n = strlen(tests[i].name);
            if (p == NULL || (p != only && p[-1] != ',') || (p[n] != 0 && p[n] != ',')) {
                continue;
            }
        }
        tests[i].fn();
        ran++;
    }
    sa1_shutdown(g_ctx);
    printf("face_test: %d ok, %d failed (%d cases)\n", g_ok, g_fail, ran);
    return g_fail ? 1 : 0;
}
