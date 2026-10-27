/*
 * sa1_world_face.c - the game's event faces inside a character world (API SA1_API_FACE, round 0.9.4a; sadc
 * docs/research/faces_wheel_plan.md 4.2-4.4 with its Review R.3-R.6, R.8, R.10, R.11; faces_events.md 2-8). The API side
 * (validation, the world, the letter mask) is src/sa1_face.c; both meet in src/sa1_face.h.
 *
 * libsa1 takes the place of a cutscene's face script. The game's own face code runs in every world already (faces_events.md
 * 5.1): the face task 8C07B106 (with the reader 8C04BCC8 -> 8C04BC20 / 8C04BC70 and the blend 8C07B080 / 8C055EFE) advances
 * player 0's event face block (eventwk 0x5C-0x6B) every step and shows the EV head while face_time != 0; EV_SetFace's body
 * 8C04BBCE and EV_ClrFace's body 8C04BD60 are called here exactly as an event calls them (the asm wrappers 8C04B39E /
 * 8C04B3B4 only add "if (tp)" and tp->twp). Nothing here runs unless the host called sa1_character_face_*: the first call for
 * a world installs w->face.pre / fill / ctx (the lane hooks of sa1_world.h); a world without a call runs exactly as before,
 * so every reference world replays bit-exact by construction.
 *
 * Per world, in segment 0x18 (reset by every image rebuild; validated by magic and self as sa1_world_set.c's st_state and
 * made again at the next call or pair start), one sf_state block: the latched request (the last call of each kind before a
 * pair wins), the applied mode and activity, two script buffers and two step buffers (alternating, so a pointer the game's
 * reader still holds - face_script, face_seq - is never overwritten in the same pair), the lip-sync held step, the
 * terminator scripts "" (EVENT: the game's idle) and "0" (GAME: back to the regular face), "CDE" (the TALK style), and the 31
 * doubled sequences of the cutscene pace with the 31 table pointers they replace. w->face.persist[] keeps what survives a
 * rebuild: [0] the requested mode, [1] the lip-sync level (q + 1, 0 = off: a state that lasts until the next level call,
 * review R.6), [2] its style (round 0.9.4c: or the mouth's, | SAME_PAIR), [3] the mouth's keys (round 0.9.4c).
 *
 * At every pair start (face.pre, sub step 0, before the executor; other sub steps: nothing), faces_wheel_plan.md 4.2:
 *   1 not ready   no player 0 taskwk P, no P->ewp or no face work W (playerpwp[0]->pfp; E-102 never has one): keep the
 *                 latch. P is read every pair: a re-created player task drops the activity and the hold, and the requested
 *                 mode is applied again to its new face block (review R.8).
 *   2 mode        EVENT on: 8C04BBCE(P, "") when face_time == 0 (and PFaceCancel first when W runs a gameplay face, review
 *                 R.4); a running GAME-mode end ("0" terminator, an appended '0') becomes the EVENT end. EVENT off:
 *                 8C04BD60(P), 8C07B2DC(0) (PFaceCancel: a gameplay face that waited behind the event face is dropped), the
 *                 activity and the hold cleared. PACE_CUTSCENE (EVENT only): the world's D_8C19C32C[0..30] point at the
 *                 doubled copies; off (or EVENT off): the original pointers back.
 *   3 request     STOP: the end (EVENT: interrupt("", NULL), the idle; GAME: 8C04BD60 and PFaceCancel, only when a host face
 *                 runs - review R.6). SCRIPT: copied into the next script buffer; GAME mode without '0' and without LOOP: '0'
 *                 appended (review R.3: never with LOOP); without NOW 8C04BBCE(P, buffer) (the game's queue), with NOW
 *                 interrupt(buffer, NULL). SEQUENCE: the entries become the next step buffer (4.3) and interrupt(terminator,
 *                 buffer). A SEQUENCE or a NOW SCRIPT started over a running gameplay face (W.seq != NULL: the face task in its
 *                 modes 2 / 3, code_8C07B080.c:176-210) is preceded by PFaceCancel (review R.4).
 *   4 lip sync    level >= 0 and no SEQUENCE / running NOW SCRIPT (one whose last step ends within this pair and leaves the
 *                 reader nothing more counts as ended: talking takes over at once, with no regular-face pair between
 *                 the animation and the mouth; the animation loses at most its last two steps): LADDER - face = ladder[i], face_next = ladder[i + 1],
 *                 face_time 64, face_count = round(r x 64) - 1, face_script NULL, face_seq = the held step {127, ladder[i +
 *                 1]} (p = level x (n - 1), i = floor(p) clamped to n - 2, r = p - i; integer arithmetic on the level the
 *                 API side quantized to 1/4096); TALK - "CDE" NOW | LOOP at the first talking pair. PFaceCancel first when it
 *                 starts over a gameplay face (review R.4). level < 0 after lip sync: interrupt(end, {6, 0}) - the mouth
 *                 closes over 6 steps, then the end: a held key comes back ({8, key}, {127, key}, held again), a held LOOP
 *                 script is started again, else the terminator.
 *   5 upkeep      (its end detection runs before step 4, so talking resumes the pair after an animation ended)
 *                 a HOLD_LAST sequence (or the comeback) on its held step: HELD, the step re-read (face_seq set back onto it:
 *                 the game's way of holding a face, faces_events.md 6.4); a LOOP script (and TALK) re-armed by 8C04BBCE when
 *                 its reader took the last letter (face_script NULL or at its 0); any other activity that ended (face_time
 *                 0, face_seq left its buffer, a script dropped): NONE, and in GAME mode 8C07B2DC(0) ONCE on that transition
 *                 (review R.6: never on a pair that merely is NONE - a world with calls but no activity keeps every hurt
 *                 face); EVENT mode, face_time 0 and nothing active: 8C04BBCE(P, "") (keep-alive: the game's idle).
 * interrupt(script, seq) (the NOW of faces_events.md 6.3): face_time 0: 8C04BBCE(P, script); else the running step ends at
 * the next step on the nearer of its two keys (face_count * 2 < face_time: face_next = face), face_count = face_time - 1,
 * face_script = script; then face_seq = seq (NULL: the reader takes the script's first letter at the step's end).
 *
 * HOST-ONLY WRITES (only in worlds that get these calls; faces_wheel_plan.md 4.2's list): the interrupt's face_next /
 * face_count / face_script; face_seq pointing at a step buffer of this block (face animations, holds, the close and the
 * comeback) or at the lip-sync held step; the lip-sync writes of face / face_next / face_time / face_count / face_script; the
 * face table's 31 pointers while PACE_CUTSCENE. Everything else is the game's code called the way an event calls it
 * (8C04BBCE, 8C04BD60, 8C07B2DC); the reader, the blend, the link of the EV head and the draw are untouched.
 *
 * ROUND 0.9.4c (SA1_API_FACE_MOUTH; sadc docs/research/phoneme_plan.md 4.2-4.6, 5.4 with its Review R.2, R.3, R.16, R.20):
 *   the mouth source   sa1_character_face_mouth latches the host's keys (persist[SF_P_KEYS] = key_a | key_b << 8), its
 *                      weight as the level's q (persist[SF_P_LEVEL]) and the style SF_STYLE_MOUTH (persist[SF_P_STYLE],
 *                      with SA1F_SAME_PAIR as a bit of that slot). Step 4's LADDER branch takes ka, kb from the latch and
 *                      r64 = (q x 64 + 2048) / 4096 instead of the ladder position; the six writes, the activity, the
 *                      priority, the close and the comeback are the LADDER code as it was. A style change between LADDER and
 *                      MOUTH while talking is no restart (no cancel, no close). The keys are checked against the EV head's
 *                      count at the latch and again here (a key >= the count would read wrong data; Big's >= 16 fault).
 *   SAME_PAIR          host-only work at a pair start (only with the flag, only in worlds that get it): after the six writes,
 *                      the face task's own step-0 blend - W.face / face_next / rate as the step's advance leaves them, then
 *                      8C07B080 through the code map - before the step's draw, and the face task's mode-4 link of the EV
 *                      head one step early (when the static face is linked and W.seq is NULL); after the step, sf_post puts
 *                      back an early link whose parent the face task did not keep (Tails' flight model). The face task
 *                      then computes the same values: every byte after the step equals the world without the flag (face_test
 *                      F33: the whole arena, player 0's works, the face block and work, pair by pair); only the points the
 *                      step-0 draw sees differ. Guard: the face task (found by its mwp) in its mode 1 or 4, and the face
 *                      work's rate_spd 0 (no gameplay face blending) - else nothing.
 *   the ladders        per kind in sa1f_compute (src/sa1_face.c): Sonic 0 4 16, Tails 0 4 7 8, Knuckles 0 4 7 16, Amy 0 7 4
 *                      8 16, Big 0 4 5, Tikal 0 5 4 6 7. UNVERIFIED: the ladders and the keys' classes come from rest-pose
 *                      renders and measured openings (phoneme_shapes.md 3, 6); the owner's eye in game decides.
 *
 * PRIORITY (faces_wheel_plan.md 4.4): a running face animation (SEQUENCE) or NOW script > lip sync > a held face (HOLD_LAST,
 * a LOOP script) > the base (EVENT: the game's idle; GAME: the regular face). While any host face runs, a gameplay face (a
 * hit's 'I', the frozen 'e') waits behind it as in the game's events, and is dropped by PFaceCancel when the host face ends
 * (GAME mode) or when event faces go off. With event faces on, a hit shows no hurt face (the game's events do the same).
 *
 * The fill (sa1w_world_pstate -> sa1_state_ex): face_mode as applied; face_shown = W.parent->sibling == W.object AND the tree
 * holding W.parent is the one the display draws (review R.5): not while the General look is on (w->sg.look: the face task
 * keeps linking Sonic's EV head into his normal tree, which is not drawn while Super), not while the event action queue's head
 * draws another object (ball, curled body, flight model), nor while the gameplay action's object does not hold the parent
 * (Sonic's ball; the action after the pair predicts the next pair's draw: a pair whose logic changes the action before its
 * display can differ for that pair); face_from / face_to / face_rate from the face block while face_time != 0, else from
 * the face work while a gameplay face blends (rate_spd set: the face task's modes 2 / 3; one that waits behind a host face is
 * not reported); face_letter the face animation the reader is in (the table or its paced copy: n; a step buffer: the
 * entry's animation from a parallel byte map; a held, lip-sync, close or comeback step: -2), else -1; face_activity.
 *
 * UNVERIFIED: the mouth ladders (round 0.9.4c: by the measured opening of rest-pose renders, not checked by eye per
 * character in game; sa1_face_info.ladder lets a host change it); the cutscene pace (asm and the wiki, not run in the harness: only an
 * option); a hidden pair (the visibility test failed) is drawn with the CPU pose, which shows the static face (deferred,
 * faces_wheel_plan.md 4.8). Game functions are called inside the world (the hooks run inside sa1g_step_game) through the code
 * map, with native pointers (sa1_decode) to guest memory, as sa1_world_set.c does.
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_face.h"

#include <stddef.h>
#include <string.h>

/* ---- the game's addresses and offsets ------------------------------------------------------------------------------ */
#define SF_TWP_EWP          0x3Cu       /* taskwk.ewp (eventwk *) */
#define SF_PWP_PFP          0x070u      /* playerwk.pfp (PL_FACE *) */
#define SF_PWP_ACTION       0x126u      /* playerwk.mj.action (Uint16) */
#define SF_PWP_PLACTPTR     0x140u      /* playerwk.mj.plactptr (PL_ACTION[], 16 bytes, actptr at +0) */
#define SF_EWP_LIST         0x40u       /* eventwk.list40: the event action queue's head (EV_ACTION_LIST, action at +4) */
#define SF_EWP_FACE         0x5Cu       /* Uint8 face */
#define SF_EWP_FACE_NEXT    0x5Du       /* Uint8 face_next */
#define SF_EWP_FACE_TIME    0x5Eu       /* Sint16 face_time: 0 = no event face */
#define SF_EWP_FACE_COUNT   0x60u       /* Sint16 face_count */
#define SF_EWP_FACE_SCRIPT  0x64u       /* const char *face_script */
#define SF_EWP_FACE_SEQ     0x68u       /* const {Sint8 time, Sint8 key} *face_seq: the next unread step */
#define SF_PFP_FACE         0x00u       /* Sint8 face */
#define SF_PFP_FACE_NEXT    0x01u
#define SF_PFP_RATE         0x04u       /* Float */
#define SF_PFP_RATE_SPD     0x08u       /* Float: 1 / time while the face task blends a gameplay sequence (modes 2 / 3), else 0 */
#define SF_PFP_SEQ          0x0Cu       /* the gameplay sequence (PFaceChange 8C07B2B8), NULL = none */
#define SF_PFP_PARENT       0x10u       /* the node whose sibling link is switched */
#define SF_PFP_OBJECT       0x18u       /* the EV head */
#define SF_OBJ_CHILD        0x2Cu
#define SF_OBJ_SIBLING      0x30u
#define SF_FN_SETFACE       0x8C04BBCEu /* EV_SetFace's body: void (taskwk *, const char *) */
#define SF_FN_CLRFACE       0x8C04BD60u /* EV_ClrFace's body: void (taskwk *) */
#define SF_FN_PFACECANCEL   0x8C07B2DCu /* PFaceCancel: void (Uint8 pno) */
#define SF_FN_BLEND         0x8C07B080u /* round 0.9.4c: the face blend: void (NJS_OBJECT *, PL_FACE *, Sint16 *idx) */
#define SF_FN_FACETASK      0x8C07B106u /* the face task's exec (Sonic, Knuckles, Amy, Big) */
#define SF_FN_FACETASK_TAILS 0x8C0BF796u /* Tails' face task exec: the parent of the normal / flight model, then 8C07B106 */
#define SF_TASK_EXEC        0x10u       /* task.exec */
#define SF_TASK_TWP         0x20u       /* task.twp (taskwk *: mode at +0) */
#define SF_TASK_MWP         0x24u       /* task.mwp: the face task keeps its PL_FACE here (code_8C0B2338.c) */

#define SF_MAGIC            0x45434146u /* "FACE" */
#define SF_STEPS_MAX        1536u       /* 32 x (18 + 29) + 2 fits (faces_wheel_plan.md 4.3) */
#define SF_SCRIPT_BUF       68u         /* 64 letters, an appended '0', the NUL */
#define SF_PACE_STEPS       33u         /* per doubled sequence: the longest has 18 steps; + the terminator */
#define SF_LADDER_TIME      64          /* face_time of a lip-sync pair */

/* persist[] (kept across rebuilds) */
#define SF_P_MODE           0u
#define SF_P_LEVEL          1u          /* q + 1 (q = level x 4096, 0..4096); 0: lip sync off */
#define SF_P_STYLE          2u          /* SA1_FACE_LEVEL_LADDER / _TALK or SF_STYLE_MOUTH, | SA1F_SAME_PAIR (round 0.9.4c) */
#define SF_P_KEYS           3u          /* round 0.9.4c: the mouth's keys, key_a | key_b << 8 (SF_STYLE_MOUTH) */
#define SF_P_NKEYS          4u          /* round 0.9.4c: the EV head's key count + 1 (0: not read yet), cached by the first
                                           mouth call so the per-frame key check never reads the tables again */

/* the internal lip-sync style of sa1_character_face_mouth (round 0.9.4c), next to LADDER 0 / TALK 1 */
#define SF_STYLE_MOUTH      2u
#define SF_STYLE_MASK       0xFFu       /* the style without SA1F_SAME_PAIR */

enum { SF_REQ_NONE = 0, SF_REQ_STOP, SF_REQ_SCRIPT, SF_REQ_SEQUENCE };
/* the activity: SA1_FACE_ACT_* values, plus the lip-sync close (reported as LIPSYNC) */
#define SF_ACT_CLOSE        6u

typedef struct sf_state {
    uint32_t magic, self;                       /* self: the block's guest address */
    /* the latch (written by sa1wf_latch_* between pairs) */
    uint32_t req, req_flags, req_len, req_count;
    char req_script[SF_SCRIPT_BUF];
    sa1_face_entry req_entries[SA1_FACE_SEQ_MAX];
    /* applied */
    uint32_t event;                             /* EVENT mode applied to player's face block */
    uint32_t pace_on;                           /* the table points at the doubled copies */
    uint32_t player;                            /* P of the last pair start (guest) */
    uint32_t act;                               /* SA1_FACE_ACT_* or SF_ACT_CLOSE */
    uint32_t act_now;                           /* SCRIPT: started with NOW */
    uint32_t appended;                          /* SCRIPT: GAME mode's '0' was appended (its last byte) */
    uint32_t act_buf, act_len;                  /* the activity's buffer (guest) and its bytes */
    uint32_t loop_buf;                          /* a held LOOP script (guest; 0: none) */
    uint32_t talk;                              /* lip sync TALK's "CDE" runs */
    int32_t hold_key;                           /* HOLD_LAST's key, -1: none */
    uint32_t held_g;                            /* the held step {127, hold_key} the reader re-reads (guest; 0: none) */
    uint32_t next_script, next_steps;           /* the buffers' alternation */
    uint32_t ladder_n;
    uint8_t ladder[8];
    uint32_t keys;                              /* round 0.9.4c: the EV head's key count (sa1f_compute; 0: unknown) */
    /* round 0.9.4c SAME_PAIR */
    uint32_t sp_runs, sp_links, sp_skips, sp_unlinks;   /* step-0 blends run, early links, guard skips, links put back */
    uint32_t sp_task;                           /* the face task (guest; 0: not found yet) */
    uint32_t sp_link_parent, sp_link_old, sp_link_obj;  /* this step's early link (sf_post checks it after the step) */
    int16_t sp_idx;                             /* 8C07B080's node counter (game-visible: its address is handed over) */
    int16_t sp_pad;
    uint32_t orig_tab[SA1F_NUM_ANIMS], orig_len[SA1F_NUM_ANIMS];   /* the image's table and each sequence's steps */
    /* counters (tests, diagnostics) */
    uint32_t pairs, requests, cancels, rearms;
    /* game-visible buffers (their guest addresses are handed to the game) */
    char script[2][SF_SCRIPT_BUF];
    int8_t steps[2][2u * SF_STEPS_MAX + 2u];
    int8_t smap[2][SF_STEPS_MAX + 1u];          /* the face animation of each step (0..30), -2 a host step */
    uint32_t steps_n[2];
    int8_t lip[4];                              /* {127, key}, {0, 0} */
    char term_event[4];                         /* "" */
    char term_game[4];                          /* "0" */
    char talk_script[4];                        /* "CDE" */
    int8_t paced[SA1F_NUM_ANIMS][2u * SF_PACE_STEPS];
} sf_state;

/* ---- guest memory -------------------------------------------------------------------------------------------------- */
static int sf_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static int sf_same(uint32_t a, uint32_t b)
{
    return a != 0 && b != 0 && (a & 0x1FFFFFFFu) == (b & 0x1FFFFFFFu);
}

/* host bytes of [g, g + n) in the arena or the allocated part of segment 0x18; NULL elsewhere */
static uint8_t *sf_ptr(const sa1g_world *w, uint32_t g, uint32_t n)
{
    uint32_t off = g & 0xFFFFFFu;
    if (g == 0) {
        return NULL;
    }
    if (((g >> 24) & 31u) == SA1G_SEG_ARENA) {
        return (size_t)off + n <= SA1G_ARENA_SIZE ? w->ram + off : NULL;
    }
    if (((g >> 24) & 31u) == SA1G_SEG_EXT && w->ext != NULL) {
        return (size_t)off + n <= w->ext_used ? w->ext + off : NULL;
    }
    return NULL;
}

static uint32_t sf_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    const uint8_t *p = sf_ptr(w, g, 4u);
    if (p != NULL) {
        memcpy(&v, p, 4);
    }
    return v;
}

static int32_t sf_rds16(const sa1g_world *w, uint32_t g)
{
    int16_t v = 0;
    const uint8_t *p = sf_ptr(w, g, 2u);
    if (p != NULL) {
        memcpy(&v, p, 2);
    }
    return v;
}

static uint32_t sf_rd16(const sa1g_world *w, uint32_t g)
{
    return (uint32_t)(uint16_t)sf_rds16(w, g);
}

static int32_t sf_rds8(const sa1g_world *w, uint32_t g)
{
    const uint8_t *p = sf_ptr(w, g, 1u);
    return p != NULL ? (int32_t)(int8_t)*p : 0;
}

static uint32_t sf_rd8(const sa1g_world *w, uint32_t g)
{
    const uint8_t *p = sf_ptr(w, g, 1u);
    return p != NULL ? (uint32_t)*p : 0u;
}

static float sf_rdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = sf_rd32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static void sf_wr32(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint8_t *p = sf_ptr(w, g, 4u);
    if (p != NULL) {
        memcpy(p, &v, 4);
    }
}

static void sf_wr16(sa1g_world *w, uint32_t g, int32_t v)
{
    int16_t x = (int16_t)v;
    uint8_t *p = sf_ptr(w, g, 2u);
    if (p != NULL) {
        memcpy(p, &x, 2);
    }
}

static void sf_wr8(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint8_t *p = sf_ptr(w, g, 1u);
    if (p != NULL) {
        *p = (uint8_t)v;
    }
}

static const uint8_t *sf_view_ptr(const void *user, uint32_t guest, uint32_t n)
{
    return sf_ptr((const sa1g_world *)user, guest, n);
}

/* ---- the block ------------------------------------------------------------------------------------------------------- */
static sf_state *sf_of(const sa1g_world *w)
{
    sf_state *S;
    uint32_t g;
    if (w == NULL || w->ext == NULL || w->face.state == 0) {
        return NULL;
    }
    g = w->face.state;
    if (((g >> 24) & 31u) != SA1G_SEG_EXT || (size_t)(g & 0xFFFFFFu) + sizeof(sf_state) > w->ext_used) {
        return NULL;
    }
    S = (sf_state *)(void *)(w->ext + (g & 0xFFFFFFu));
    return S->magic == SF_MAGIC && S->self == g ? S : NULL;
}

static uint32_t sf_guest(const sf_state *S, const void *p)
{
    return S->self + (uint32_t)((const uint8_t *)p - (const uint8_t *)S);
}

static int sf_in(uint32_t g, uint32_t base, uint32_t len)
{
    return base != 0 && g >= base && g - base <= len;
}

/* the steps of a table sequence at g (guest), at most cap */
static uint32_t sf_seq_len(const sa1g_world *w, uint32_t g, uint32_t cap)
{
    uint32_t n = 0;
    while (n < cap) {
        const uint8_t *s = sf_ptr(w, g + 2u * n, 2u);
        if (s == NULL || s[0] == 0) {
            break;
        }
        n++;
    }
    return n;
}

static sf_state *sf_make(sa1g_world *w)
{
    sf_state *S = NULL;
    sa1_face_info info;
    sa1f_mem m;
    uint32_t g = sa1g_world_ext_alloc(w, sizeof(sf_state), 16, (void **)&S), n, i;
    if (g == 0 || S == NULL) {
        return NULL;
    }
    memset(S, 0, sizeof *S);
    S->magic = SF_MAGIC;
    S->self = g;
    S->hold_key = -1;
    S->lip[0] = SA1F_STEP_TIME_MAX;
    S->term_game[0] = '0';
    memcpy(S->talk_script, "CDE", 4);
    /* the image's table (a fresh block: after a creation or a rebuild the table is the image's) and its doubled copies */
    for (n = 0; n < SA1F_NUM_ANIMS; n++) {
        uint32_t p = sf_rd32(w, SA1F_ADDR_FACETAB + 4u * n), len = sf_seq_len(w, p, SF_PACE_STEPS - 1u);
        S->orig_tab[n] = p;
        S->orig_len[n] = len;
        for (i = 0; i < len; i++) {
            int32_t t = sf_rds8(w, p + 2u * i) * 2;
            S->paced[n][2u * i] = (int8_t)(t > SA1F_STEP_TIME_MAX ? SA1F_STEP_TIME_MAX : t);
            S->paced[n][2u * i + 1u] = (int8_t)sf_rds8(w, p + 2u * i + 1u);
        }
    }
    /* the mouth ladder of the world's character (sa1f_compute on its own arena) */
    memset(&info, 0, sizeof info);
    m.ptr = sf_view_ptr;
    m.user = w;
    if (sa1f_compute(&m, w->kind, &info) == SA1_OK) {
        S->ladder_n = info.ladder_len;
        memcpy(S->ladder, info.ladder, sizeof S->ladder);
        S->keys = info.keys;
    }
    w->face.state = g;
    return S;
}

/* the EV head's key count of a world without a face block (sa1f_compute on its own arena; 0: none / unreadable) */
static uint32_t sf_world_keys(const sa1g_world *w)
{
    sa1_face_info info;
    sa1f_mem m;
    memset(&info, 0, sizeof info);
    m.ptr = sf_view_ptr;
    m.user = w;
    return sa1f_compute(&m, w->kind, &info) == SA1_OK ? info.keys : 0u;
}

static void sf_pre(sa1g_world *w, int32_t sub, void *user);
static void sf_post(sa1g_world *w, int32_t sub, void *user);
static void sf_fill(const sa1g_world *w, sa1w_pstate *o);

/* the block of a world that got a call (the hooks installed at the first one); NULL: segment 0x18 is full */
static sf_state *sf_attach(sa1_context *ctx, sa1g_world *w)
{
    sf_state *S;
    if (w->face.pre == NULL) {
        w->face.pre = sf_pre;
        w->face.fill = sf_fill;
    }
    if (w->face.post == NULL) {
        w->face.post = sf_post;         /* round 0.9.4c: SAME_PAIR's check after the step (nothing without an early link) */
    }
    w->face.ctx = ctx;
    S = sf_of(w);
    return S != NULL ? S : sf_make(w);
}

/* ---- the game's calls (inside the world: the hooks run inside sa1g_step_game) -------------------------------------- */
static void *sf_fn(uint32_t dc)
{
    return sa1_fn(SA1_GFN_C(dc), 0u);
}

static void sf_setface(uint32_t P, uint32_t script)
{
    ((void (*)(void *, const void *))sf_fn(SF_FN_SETFACE))(sa1_decode(P), sa1_decode(script));
}

static void sf_clrface(uint32_t P)
{
    ((void (*)(void *))sf_fn(SF_FN_CLRFACE))(sa1_decode(P));
}

static void sf_pfacecancel(sf_state *S)
{
    ((void (*)(uint8_t))sf_fn(SF_FN_PFACECANCEL))(0u);
    S->cancels++;
}

/* review R.4: a host face that STARTS while the face work runs a gameplay sequence (W.seq: the face task in its modes 2 / 3,
   which show the gameplay face to its end and run the face block unseen) drops it first */
static void sf_cancel_gameplay(sa1g_world *w, sf_state *S, uint32_t W)
{
    if (sf_rd32(w, W + SF_PFP_SEQ) != 0) {
        sf_pfacecancel(S);
    }
}

static void sf_interrupt(sa1g_world *w, uint32_t P, uint32_t E, uint32_t script, uint32_t seq)
{
    int32_t t = sf_rds16(w, E + SF_EWP_FACE_TIME);
    if (t == 0) {
        sf_setface(P, script);
    } else {
        int32_t c = sf_rds16(w, E + SF_EWP_FACE_COUNT);
        if (c * 2 < t) {
            sf_wr8(w, E + SF_EWP_FACE_NEXT, sf_rd8(w, E + SF_EWP_FACE));     /* below half: its start key */
        }
        sf_wr16(w, E + SF_EWP_FACE_COUNT, t - 1);
        sf_wr32(w, E + SF_EWP_FACE_SCRIPT, script);
    }
    sf_wr32(w, E + SF_EWP_FACE_SEQ, seq);
}

static void sf_drop(sf_state *S)
{
    S->act = SA1_FACE_ACT_NONE;
    S->act_now = 0;
    S->appended = 0;
    S->act_buf = 0;
    S->act_len = 0;
    S->loop_buf = 0;
    S->talk = 0;
    S->hold_key = -1;
    S->held_g = 0;
}

/* the activity ended by itself: NONE; GAME mode: PFaceCancel once, on this transition (review R.6) */
static void sf_to_none(sf_state *S)
{
    sf_drop(S);
    if (!S->event) {
        sf_pfacecancel(S);
    }
}

/* ---- the step buffers ---------------------------------------------------------------------------------------------- */
typedef struct sf_build {
    int8_t *st, *map;
    uint32_t n;
    int overflow;
} sf_build;

static void sf_push(sf_build *b, int32_t t, int32_t key, int32_t anim)
{
    if (b->n >= SF_STEPS_MAX) {
        b->overflow = 1;
        return;
    }
    b->st[2u * b->n] = (int8_t)t;
    b->st[2u * b->n + 1u] = (int8_t)key;
    b->map[b->n] = (int8_t)anim;
    b->n++;
}

static void sf_push_hold(sf_build *b, uint32_t h, int32_t key)
{
    while (h > 0u) {
        uint32_t q = h > (uint32_t)SA1F_STEP_TIME_MAX ? (uint32_t)SA1F_STEP_TIME_MAX : h;
        sf_push(b, (int32_t)q, key, -2);
        h -= q;
    }
}

/* faces_wheel_plan.md 4.3: per entry enter = the first step's time (0: the game's, from the world's current table - the
 * paced copy while PACE_CUTSCENE), the other steps as the game's, hold as steps {<= 127, last key}; HOLD_LAST: the held step
 * {127, last key}; the terminator {0, 0}. Into step buffer j. Returns the steps, 0 on overflow. */
static uint32_t sf_build_seq(const sa1g_world *w, sf_state *S, uint32_t j, const sa1_face_entry *e, uint32_t count,
                             uint32_t flags, int32_t *last_out)
{
    sf_build b;
    uint32_t k, i;
    int32_t last = 0;
    memset(&b, 0, sizeof b);
    b.st = S->steps[j];
    b.map = S->smap[j];
    for (k = 0; k < count; k++) {
        uint32_t p = sf_rd32(w, SA1F_ADDR_FACETAB + 4u * e[k].anim);
        for (i = 0; i < SA1F_MAX_STEPS; i++) {
            const uint8_t *s = sf_ptr(w, p + 2u * i, 2u);
            int32_t t, key;
            if (s == NULL || s[0] == 0) {
                break;
            }
            t = (int8_t)s[0];
            key = (int8_t)s[1];
            if (i == 0 && e[k].enter != 0u) {
                t = (int32_t)e[k].enter;
            }
            sf_push(&b, t, key, (int32_t)e[k].anim);
            last = key;
        }
        sf_push_hold(&b, e[k].hold, last);
    }
    if (flags & SA1_FACE_HOLD_LAST) {
        sf_push(&b, SA1F_STEP_TIME_MAX, last, -2);
    }
    if (b.overflow || b.n == 0) {
        return 0;
    }
    b.st[2u * b.n] = 0;
    b.st[2u * b.n + 1u] = 0;
    b.map[b.n] = -2;
    S->steps_n[j] = b.n;
    *last_out = last;
    return b.n;
}

/* the steps a sequence needs (the latch refuses more than a buffer holds) */
static uint32_t sf_count_seq(const sa1g_world *w, const sa1_face_entry *e, uint32_t count, uint32_t flags)
{
    uint32_t k, n = 0;
    for (k = 0; k < count; k++) {
        uint32_t p = sf_rd32(w, SA1F_ADDR_FACETAB + 4u * e[k].anim);
        n += sf_seq_len(w, p, SA1F_MAX_STEPS) + (e[k].hold + (uint32_t)SA1F_STEP_TIME_MAX - 1u) / SA1F_STEP_TIME_MAX;
    }
    return n + ((flags & SA1_FACE_HOLD_LAST) ? 1u : 0u);
}

/* ---- the cutscene pace ------------------------------------------------------------------------------------------- */
static void sf_pace(sa1g_world *w, sf_state *S, uint32_t on)
{
    uint32_t n;
    for (n = 0; n < SA1F_NUM_ANIMS; n++) {
        sf_wr32(w, SA1F_ADDR_FACETAB + 4u * n, on ? sf_guest(S, S->paced[n]) : S->orig_tab[n]);
    }
    S->pace_on = on;
}

/* ---- one pair start -------------------------------------------------------------------------------------------------- */
static void sf_mode(sa1g_world *w, sf_state *S, uint32_t P, uint32_t E, uint32_t W)
{
    uint32_t want = w->face.persist[SF_P_MODE];
    uint32_t want_event = (want & 0xFFu) == SA1_FACE_MODE_EVENT, want_pace = want_event && (want & SA1_FACE_PACE_CUTSCENE);
    if (want_event && !S->event) {
        uint32_t scr = sf_rd32(w, E + SF_EWP_FACE_SCRIPT);
        sf_cancel_gameplay(w, S, W);
        if (sf_rds16(w, E + SF_EWP_FACE_TIME) == 0) {
            sf_setface(P, sf_guest(S, S->term_event));
        } else if (sf_same(scr, sf_guest(S, S->term_game))) {
            sf_wr32(w, E + SF_EWP_FACE_SCRIPT, sf_guest(S, S->term_event));     /* a running GAME end: the idle instead */
        } else if (S->act == SA1_FACE_ACT_SCRIPT && S->appended && sf_in(scr, S->act_buf, S->act_len) &&
                   S->act_len >= 2u) {
            char *b = (char *)sf_ptr(w, S->act_buf, S->act_len);
            if (b != NULL && b[S->act_len - 2u] == '0' && scr <= S->act_buf + S->act_len - 2u) {
                b[S->act_len - 2u] = 0;                                         /* the appended '0' not read yet */
                S->appended = 0;
            }
        }
        S->event = 1;
    } else if (!want_event && S->event) {
        sf_clrface(P);
        sf_pfacecancel(S);
        sf_drop(S);
        S->event = 0;
    }
    if (want_pace != S->pace_on) {
        sf_pace(w, S, want_pace);
    }
}

static void sf_end(sa1g_world *w, sf_state *S, uint32_t P, uint32_t E)
{
    if (S->event) {
        sf_interrupt(w, P, E, sf_guest(S, S->term_event), 0u);
        sf_drop(S);
    } else if (S->act != SA1_FACE_ACT_NONE || sf_rds16(w, E + SF_EWP_FACE_TIME) != 0) {
        uint32_t had = S->act != SA1_FACE_ACT_NONE;
        sf_clrface(P);
        sf_drop(S);
        if (had) {
            sf_pfacecancel(S);
        }
    }
}

static void sf_request(sa1g_world *w, sf_state *S, uint32_t P, uint32_t E, uint32_t W)
{
    uint32_t r = S->req, flags = S->req_flags, appended = 0;
    S->req = SF_REQ_NONE;
    if (r == SF_REQ_NONE) {
        return;
    }
    S->requests++;
    if (r == SF_REQ_STOP) {
        sf_end(w, S, P, E);
    } else if (r == SF_REQ_SCRIPT) {
        uint32_t k = S->next_script, len = S->req_len, g;
        char *b = S->script[k];
        S->next_script ^= 1u;
        memcpy(b, S->req_script, len);
        if (!S->event && !(flags & SA1_FACE_LOOP) && memchr(b, '0', len) == NULL) {
            b[len++] = '0';                         /* GAME mode: back to the regular face at the end */
            appended = 1;
        }
        b[len] = 0;
        g = sf_guest(S, b);
        sf_drop(S);
        if (flags & SA1_FACE_NOW) {
            sf_cancel_gameplay(w, S, W);
            sf_interrupt(w, P, E, g, 0u);
        } else {
            sf_setface(P, g);                       /* the game's queue: it starts when the running animation ends */
        }
        S->act = (flags & SA1_FACE_LOOP) ? SA1_FACE_ACT_HELD : SA1_FACE_ACT_SCRIPT;
        S->act_now = (flags & SA1_FACE_NOW) ? 1u : 0u;
        S->appended = appended;
        S->act_buf = g;
        S->act_len = len + 1u;
        S->loop_buf = (flags & SA1_FACE_LOOP) ? g : 0u;
    } else {
        uint32_t j = S->next_steps, n;
        int32_t last = 0;
        n = sf_build_seq(w, S, j, S->req_entries, S->req_count, flags, &last);
        if (n == 0) {
            return;                                 /* cannot happen: the latch counted the steps */
        }
        S->next_steps ^= 1u;
        sf_drop(S);
        sf_cancel_gameplay(w, S, W);
        sf_interrupt(w, P, E, sf_guest(S, S->event ? S->term_event : S->term_game), sf_guest(S, S->steps[j]));
        S->act = SA1_FACE_ACT_SEQUENCE;
        S->act_buf = sf_guest(S, S->steps[j]);
        S->act_len = 2u * n + 2u;
        if (flags & SA1_FACE_HOLD_LAST) {
            S->hold_key = last;
            S->held_g = sf_guest(S, &S->steps[j][2u * (n - 1u)]);
        }
    }
}

/* the running SEQUENCE / NOW SCRIPT ends within this pair: its running step ends within the pair's two steps and the reader
   then finds nothing more (the step buffer's or the table sequence's terminator, and for a script no letter left). Lip sync
   takes over at this pair start, so talking resumes the pair after the animation's last pair with no regular-face pair in
   between (faces_wheel_plan.md 4.4, face_test F20); the animation loses at most the last two steps of its last blend. */
static int sf_ending(const sa1g_world *w, const sf_state *S, uint32_t E)
{
    int32_t t = sf_rds16(w, E + SF_EWP_FACE_TIME), c = sf_rds16(w, E + SF_EWP_FACE_COUNT);
    uint32_t seq = sf_rd32(w, E + SF_EWP_FACE_SEQ), scr;
    const uint8_t *b = sf_ptr(w, seq, 1u);
    if (t == 0 || t - c > 2 || b == NULL || *b != 0) {
        return 0;
    }
    if (S->act == SA1_FACE_ACT_SEQUENCE) {
        return sf_in(seq, S->act_buf, S->act_len);
    }
    scr = sf_rd32(w, E + SF_EWP_FACE_SCRIPT);
    b = sf_ptr(w, scr, 1u);
    return scr == 0u || (b != NULL && (*b == 0 || *b == '0'));
}

/* ---- SAME_PAIR (round 0.9.4c; sadc docs/research/phoneme_plan.md 4.5 with its Review R.2, R.3, R.20) ------------------ */
/* the face task of the face work W: a top-level task whose mwp is W and whose exec is 8C07B106 (Tails: 8C0BF796, which picks
   the parent of the normal or the flight model and then runs 8C07B106); cached in S->sp_task, re-checked every pair */
static int sf_task_is(const sa1g_world *w, uint32_t tp, uint32_t W)
{
    uint32_t ex = sf_rd32(w, tp + SF_TASK_EXEC) & 0x1FFFFFFFu;
    return sf_arena(tp) && sf_same(sf_rd32(w, tp + SF_TASK_MWP), W) &&
           (ex == (SF_FN_FACETASK & 0x1FFFFFFFu) || ex == (SF_FN_FACETASK_TAILS & 0x1FFFFFFFu));
}

static uint32_t sf_face_task(const sa1g_world *w, sf_state *S, uint32_t W)
{
    uint32_t lv;
    if (S->sp_task != 0u && sf_task_is(w, S->sp_task, W)) {
        return S->sp_task;
    }
    S->sp_task = 0u;
    for (lv = 0; lv < 8u; lv++) {
        uint32_t tp = sf_rd32(w, SA1G_ADDR_TASK_LISTS + 4u * lv), guard = 0;
        while (tp != 0u && guard++ < 0x1000u) {
            if (sf_task_is(w, tp, W)) {
                S->sp_task = tp;
                return tp;
            }
            tp = sf_rd32(w, tp);
        }
    }
    return 0u;
}

/* After the six writes of a LADDER / MOUTH pair (sub step 0, before the executor): what the face task will compute at this
 * pair's step 0, run before the step's draw. The face task (8C07B106, code_8C07B080.c:145-239) first advances the face block
 * (the reader 8C04BCC8, code_8C04BC70.c:84-104): face_count r64 - 1 becomes r64; r64 >= 64 ends the step on the held step
 * {127, kb} (face = face_next = kb, face_count 0, face_time 127). Then, in its mode 1 (face_time != 0: falls into mode 4) or
 * mode 4, it sets W.face = face, W.face_next = face_next, W.rate = (Float)face_count / (Float)face_time - kb, kb, 0 / 127 = 0
 * or ka, kb, r64 / 64 (exact in binary floating point, any rounding) - calls 8C07B080(W.object, W, &idx) with idx 0 and links
 * W.object under W.parent. 8C07B080 writes only the points of the EV head's models (8C055EFE: lerp of two keys, deterministic)
 * and the node counter idx. So the same call here, with the same inputs, gives the points the face task gives a moment later,
 * and the face task then rewrites W.face / face_next / rate and the points with identical values: after the step every byte
 * equals the world without the flag; only the points (and the link) the step-0 draw sees are this pair's. The guard: the face
 * task found and in its mode 1 or 4 (twp->mode), and W.rate_spd == 0 (no gameplay face blends: modes 2 / 3), else nothing.
 * The early link (mode 1: the static face is linked) is mode 4's own `fp->parent->sibling = fp->object` (a raw copy of the
 * guest word, as the lowered code does), done one step early, only when W.seq is NULL; Tails' exec may switch W.parent at
 * step 0 (its flight-model bit), so sf_post puts a parent linked here back when the face task did not keep it. Runs inside
 * the world's step (sa1g_step_game: the world's FP environment, as sf_setface's game calls). */
static void sf_same_pair(sa1g_world *w, sf_state *S, uint32_t W, uint32_t ka, uint32_t kb, uint32_t r64)
{
    uint32_t tp = sf_face_task(w, S, W), twp = tp != 0u ? sf_rd32(w, tp + SF_TASK_TWP) : 0u;
    uint32_t obj = sf_rd32(w, W + SF_PFP_OBJECT), parent = sf_rd32(w, W + SF_PFP_PARENT), rbits;
    int32_t mode = sf_arena(twp) ? sf_rds8(w, twp) : -1;
    float rate;
    if ((mode != 1 && mode != 4) || sf_rdf(w, W + SF_PFP_RATE_SPD) != 0.0f || !sf_arena(obj)) {
        S->sp_skips++;
        return;
    }
    if (r64 >= (uint32_t)SF_LADDER_TIME) {
        ka = kb;
        rate = 0.0f;
    } else {
        rate = (float)r64 / (float)SF_LADDER_TIME;
    }
    memcpy(&rbits, &rate, 4);
    sf_wr8(w, W + SF_PFP_FACE, ka);
    sf_wr8(w, W + SF_PFP_FACE_NEXT, kb);
    sf_wr32(w, W + SF_PFP_RATE, rbits);
    S->sp_idx = 0;
    ((void (*)(void *, void *, void *))sf_fn(SF_FN_BLEND))(sa1_decode(obj), sa1_decode(W),
                                                            sa1_decode(sf_guest(S, &S->sp_idx)));
    S->sp_runs++;
    if (sf_arena(parent) && sf_rd32(w, W + SF_PFP_SEQ) == 0u) {
        uint32_t sib = sf_rd32(w, parent + SF_OBJ_SIBLING);
        if (!sf_same(sib, obj)) {
            S->sp_link_parent = parent;
            S->sp_link_old = sib;
            S->sp_link_obj = obj;
            sf_wr32(w, parent + SF_OBJ_SIBLING, obj);
            S->sp_links++;
        }
    }
}

static void sf_lipsync(sa1g_world *w, sf_state *S, uint32_t P, uint32_t E, uint32_t W)
{
    uint32_t lv = w->face.persist[SF_P_LEVEL], style = w->face.persist[SF_P_STYLE] & SF_STYLE_MASK;
    uint32_t fast = w->face.persist[SF_P_STYLE] & SA1F_SAME_PAIR;
    int running = (S->act == SA1_FACE_ACT_SEQUENCE || (S->act == SA1_FACE_ACT_SCRIPT && S->act_now)) &&
                  !(w->face.persist[SF_P_LEVEL] != 0u && sf_ending(w, S, E));
    if (lv != 0u && !running) {
        if (style == SA1_FACE_LEVEL_TALK) {
            if (S->act != SA1_FACE_ACT_LIPSYNC || !S->talk) {
                uint32_t k = S->next_script, g;
                S->next_script ^= 1u;
                memcpy(S->script[k], S->talk_script, 4);
                g = sf_guest(S, S->script[k]);
                if (S->act != SA1_FACE_ACT_LIPSYNC) {
                    sf_cancel_gameplay(w, S, W);
                }
                sf_interrupt(w, P, E, g, 0u);
                S->act = SA1_FACE_ACT_LIPSYNC;
                S->talk = 1;
                S->act_buf = g;
                S->act_len = 4u;
            }
        } else if (style == SF_STYLE_MOUTH ? ((w->face.persist[SF_P_KEYS] & 0xFFu) < S->keys &&
                                              ((w->face.persist[SF_P_KEYS] >> 8) & 0xFFu) < S->keys)
                                           : S->ladder_n >= 2u) {
            uint32_t q = lv - 1u, r64;
            int32_t ka, kb;
            if (style == SF_STYLE_MOUTH) {
                /* round 0.9.4c: the host's mouth (keys checked again against the world's count: a key >= the count
                   would read wrong data, Big's >= 16 fault the world in 8C055EFE) */
                ka = (int32_t)(w->face.persist[SF_P_KEYS] & 0xFFu);
                kb = (int32_t)((w->face.persist[SF_P_KEYS] >> 8) & 0xFFu);
                r64 = (q * 64u + 2048u) / 4096u;
            } else {
                /* p = level x (n - 1) in 1/64 steps: (q x (n - 1) x 64 + 2048) / 4096, i = p / 64 clamped to n - 2 */
                uint32_t n = S->ladder_n, p64 = (q * (n - 1u) * 64u + 2048u) / 4096u, i = p64 / 64u;
                if (i > n - 2u) {
                    i = n - 2u;
                }
                r64 = p64 - 64u * i;
                ka = S->ladder[i];
                kb = S->ladder[i + 1u];
            }
            if (S->act != SA1_FACE_ACT_LIPSYNC || S->talk) {
                sf_cancel_gameplay(w, S, W);
                S->act = SA1_FACE_ACT_LIPSYNC;
                S->talk = 0;
                S->act_buf = 0;
                S->act_len = 0;
            }
            S->lip[0] = SA1F_STEP_TIME_MAX;
            S->lip[1] = (int8_t)kb;
            S->lip[2] = 0;
            S->lip[3] = 0;
            sf_wr8(w, E + SF_EWP_FACE, (uint32_t)ka);
            sf_wr8(w, E + SF_EWP_FACE_NEXT, (uint32_t)kb);
            sf_wr16(w, E + SF_EWP_FACE_TIME, SF_LADDER_TIME);
            sf_wr16(w, E + SF_EWP_FACE_COUNT, (int32_t)r64 - 1);
            sf_wr32(w, E + SF_EWP_FACE_SCRIPT, 0u);
            sf_wr32(w, E + SF_EWP_FACE_SEQ, sf_guest(S, S->lip));
            if (fast) {
                sf_same_pair(w, S, W, (uint32_t)ka, (uint32_t)kb, r64);
            }
        }
    } else if (lv == 0u && S->act == SA1_FACE_ACT_LIPSYNC) {
        /* the close {6, 0}, then a held key's comeback {8, key} {127, key}, a held LOOP script, or the end */
        uint32_t j = S->next_steps, n = 0, end;
        int8_t *st = S->steps[j], *map = S->smap[j];
        S->next_steps ^= 1u;
        st[0] = 6;
        st[1] = 0;
        map[n++] = -2;
        if (S->hold_key >= 0) {
            st[2] = 8;
            st[3] = (int8_t)S->hold_key;
            st[4] = SA1F_STEP_TIME_MAX;
            st[5] = (int8_t)S->hold_key;
            map[n++] = -2;
            map[n++] = -2;
        }
        st[2u * n] = 0;
        st[2u * n + 1u] = 0;
        map[n] = -2;
        S->steps_n[j] = n;
        end = (S->hold_key < 0 && S->loop_buf != 0u) ? S->loop_buf
                                                      : sf_guest(S, S->event ? S->term_event : S->term_game);
        sf_interrupt(w, P, E, end, sf_guest(S, st));
        S->act = SF_ACT_CLOSE;
        S->talk = 0;
        S->act_buf = sf_guest(S, st);
        S->act_len = 2u * n + 2u;
        S->held_g = S->hold_key >= 0 ? sf_guest(S, &st[4]) : 0u;
    }
}

/* a LOOP script (or TALK's "CDE") whose reader took its last letter: armed again (8C04BBCE stores only the pointer while
   a face runs: no gap; faces_events.md 6.4) */
static void sf_rearm(sa1g_world *w, sf_state *S, uint32_t P, uint32_t E, uint32_t buf)
{
    uint32_t scr = sf_rd32(w, E + SF_EWP_FACE_SCRIPT);
    const uint8_t *c = sf_ptr(w, scr, 1u);
    if (scr == 0 || sf_rds16(w, E + SF_EWP_FACE_TIME) == 0 || (sf_in(scr, buf, SF_SCRIPT_BUF) && c != NULL && *c == 0)) {
        sf_setface(P, buf);
        S->rearms++;
    }
}

/* the activity as the end detection sees the face block now (no side effects; the fill reports it too): HELD when a
   HOLD_LAST sequence or the comeback reached its held step or the close gave a held LOOP script back, NONE when the activity
   ended (face_time 0, face_seq left its buffer, the script was dropped, something else took the held step) */
static uint32_t sf_settled(const sa1g_world *w, const sf_state *S, uint32_t E)
{
    int32_t t = sf_rds16(w, E + SF_EWP_FACE_TIME);
    uint32_t seq = sf_rd32(w, E + SF_EWP_FACE_SEQ), scr = sf_rd32(w, E + SF_EWP_FACE_SCRIPT);
    switch (S->act) {
    case SA1_FACE_ACT_SEQUENCE:
    case SF_ACT_CLOSE:
        if (S->held_g != 0u && seq == S->held_g + 2u && t != 0) {
            return SA1_FACE_ACT_HELD;
        }
        if (t == 0 || !sf_in(seq, S->act_buf, S->act_len)) {
            return S->act == SF_ACT_CLOSE && S->loop_buf != 0u && S->hold_key < 0 && t != 0 ? SA1_FACE_ACT_HELD
                                                                                          : SA1_FACE_ACT_NONE;
        }
        return S->act;
    case SA1_FACE_ACT_SCRIPT:
        return scr == 0 || t == 0 ? SA1_FACE_ACT_NONE : SA1_FACE_ACT_SCRIPT;
    case SA1_FACE_ACT_HELD:
        if (S->held_g != 0u && (t == 0 || (seq != S->held_g && seq != S->held_g + 2u))) {
            return SA1_FACE_ACT_NONE;
        }
        return SA1_FACE_ACT_HELD;
    default:
        return S->act;
    }
}

/* step 5's end detection (before lip sync, so talking resumes the pair after an animation ended) */
static void sf_settle(sa1g_world *w, sf_state *S, uint32_t E)
{
    uint32_t a = sf_settled(w, S, E);
    if (a == S->act) {
        return;
    }
    if (a == SA1_FACE_ACT_NONE) {
        sf_to_none(S);
    } else if (a == SA1_FACE_ACT_HELD) {
        if (S->act == SF_ACT_CLOSE && S->hold_key < 0) {
            S->act_buf = S->loop_buf;               /* the held LOOP script came back */
            S->act_len = SF_SCRIPT_BUF;
        }
        S->act = SA1_FACE_ACT_HELD;
    }
}

/* step 5's holds and the keep-alive */
static void sf_upkeep(sa1g_world *w, sf_state *S, uint32_t P, uint32_t E)
{
    if (S->act == SA1_FACE_ACT_LIPSYNC && S->talk) {
        sf_rearm(w, S, P, E, S->act_buf);
    } else if (S->act == SA1_FACE_ACT_HELD) {
        if (S->held_g != 0u) {
            if (sf_rd32(w, E + SF_EWP_FACE_SEQ) == S->held_g + 2u) {
                sf_wr32(w, E + SF_EWP_FACE_SEQ, S->held_g);     /* the held step re-reads itself */
                S->rearms++;
            }
        } else if (S->loop_buf != 0u) {
            sf_rearm(w, S, P, E, S->loop_buf);
        }
    }
    if (S->event && S->act == SA1_FACE_ACT_NONE && sf_rds16(w, E + SF_EWP_FACE_TIME) == 0) {
        sf_setface(P, sf_guest(S, S->term_event));      /* keep-alive: the game's idle */
    }
}

static void sf_pre(sa1g_world *w, int32_t sub, void *user)
{
    sf_state *S;
    uint32_t P, E, pwp, W;
    (void)user;
    if (sub != 0) {
        return;
    }
    S = sf_of(w);
    if (S == NULL) {
        S = sf_make(w);                             /* after an image rebuild (the latched request went with it) */
        if (S == NULL) {
            return;
        }
    }
    S->pairs++;
    P = sf_rd32(w, SA1G_ADDR_PLAYER_TWP);
    pwp = sf_rd32(w, SA1G_ADDR_PLAYER_PWP);
    E = sf_arena(P) ? sf_rd32(w, P + SF_TWP_EWP) : 0u;
    W = sf_arena(pwp) ? sf_rd32(w, pwp + SF_PWP_PFP) : 0u;
    if (!sf_arena(P) || !sf_arena(E) || !sf_arena(W)) {
        return;                                     /* not ready: everything stays latched */
    }
    if (S->player != P) {
        if (S->player != 0u) {
            sf_drop(S);                             /* a re-created player task (review R.8): its face block is new */
            S->event = 0;
        }
        S->player = P;
    }
    sf_mode(w, S, P, E, W);
    sf_request(w, S, P, E, W);
    sf_settle(w, S, E);
    sf_lipsync(w, S, P, E, W);
    sf_upkeep(w, S, P, E);
}

/* round 0.9.4c SAME_PAIR, after the pair's step 0 (face.post, sub 0, after the executor): an early link whose parent the face
   task did not keep (Tails' exec switched W.parent between the normal and the flight model at this step) is put back to its
   sibling of before the link, as the world without the flag has it; nothing else (no early link this step: nothing) */
static void sf_post(sa1g_world *w, int32_t sub, void *user)
{
    sf_state *S;
    uint32_t pwp, W, cur;
    (void)user;
    if (sub != 0) {
        return;
    }
    S = sf_of(w);
    if (S == NULL || S->sp_link_parent == 0u) {
        return;
    }
    pwp = sf_rd32(w, SA1G_ADDR_PLAYER_PWP);
    W = sf_arena(pwp) ? sf_rd32(w, pwp + SF_PWP_PFP) : 0u;
    cur = sf_arena(W) ? sf_rd32(w, W + SF_PFP_PARENT) : 0u;
    if (!sf_same(cur, S->sp_link_parent) && sf_same(sf_rd32(w, S->sp_link_parent + SF_OBJ_SIBLING), S->sp_link_obj)) {
        sf_wr32(w, S->sp_link_parent + SF_OBJ_SIBLING, S->sp_link_old);
        S->sp_unlinks++;
    }
    S->sp_link_parent = 0u;
}

/* ---- the fill ---------------------------------------------------------------------------------------------------- */
static int sf_tree_has(const sa1g_world *w, uint32_t obj, uint32_t target, uint32_t *budget)
{
    while (obj != 0u && *budget > 0u) {
        uint32_t child;
        (*budget)--;
        if (sf_same(obj, target)) {
            return 1;
        }
        if (sf_ptr(w, obj, 0x34u) == NULL) {
            return 0;
        }
        child = sf_rd32(w, obj + SF_OBJ_CHILD);
        if (child != 0u && sf_tree_has(w, child, target, budget)) {
            return 1;
        }
        obj = sf_rd32(w, obj + SF_OBJ_SIBLING);
    }
    return 0;
}

static int32_t sf_letter_at(const sa1g_world *w, const sf_state *S, uint32_t seq)
{
    uint32_t n, j;
    if (seq == 0u) {
        return -1;
    }
    if (S->held_g != 0u && seq == S->held_g) {
        return -2;                                  /* the held step, set back onto itself by the upkeep (still in it) */
    }
    for (n = 0; n < SA1F_NUM_ANIMS; n++) {
        if (sf_in(seq & 0x1FFFFFFFu, S->orig_tab[n] & 0x1FFFFFFFu, 2u * S->orig_len[n]) ||
            sf_in(seq, sf_guest(S, S->paced[n]), 2u * S->orig_len[n])) {
            return (int32_t)n;
        }
    }
    for (j = 0; j < 2u; j++) {
        uint32_t base = sf_guest(S, S->steps[j]);
        if (sf_in(seq, base, 2u * S->steps_n[j] + 2u)) {
            int32_t idx = (int32_t)((seq - base) / 2u) - 1;
            return S->smap[j][idx < 0 ? 0 : idx];
        }
    }
    if (sf_in(seq, sf_guest(S, S->lip), 4u)) {
        return -2;
    }
    (void)w;
    return -1;
}

static void sf_fill(const sa1g_world *w, sa1w_pstate *o)
{
    const sf_state *S = sf_of(w);
    uint32_t P = sf_rd32(w, SA1G_ADDR_PLAYER_TWP), pwp = sf_rd32(w, SA1G_ADDR_PLAYER_PWP), E, W, wseq;
    int32_t t;
    if (S == NULL) {
        return;
    }
    o->face_mode = (S->event ? SA1_FACE_MODE_EVENT : SA1_FACE_MODE_GAME) | (S->pace_on ? SA1_FACE_PACE_CUTSCENE : 0u);
    E = sf_arena(P) ? sf_rd32(w, P + SF_TWP_EWP) : 0u;
    W = sf_arena(pwp) ? sf_rd32(w, pwp + SF_PWP_PFP) : 0u;
    if (!sf_arena(E)) {
        return;
    }
    t = sf_rds16(w, E + SF_EWP_FACE_TIME);
    /* a gameplay face that BLENDS (the face task in its modes 2 / 3: rate_spd set); one that waits behind a host face
       (W.seq set, the task in mode 4 or 1) is not reported */
    wseq = sf_arena(W) && sf_rdf(w, W + SF_PFP_RATE_SPD) != 0.0f ? sf_rd32(w, W + SF_PFP_SEQ) : 0u;
    if (t != 0) {
        int32_t c = sf_rds16(w, E + SF_EWP_FACE_COUNT);
        float r = (float)c / (float)t;
        o->face_from = (int32_t)(uint8_t)sf_rds8(w, E + SF_EWP_FACE);
        o->face_to = (int32_t)(uint8_t)sf_rds8(w, E + SF_EWP_FACE_NEXT);
        o->face_rate = r < 0.0f ? 0.0f : r > 1.0f ? 1.0f : r;
        o->face_letter = sf_letter_at(w, S, sf_rd32(w, E + SF_EWP_FACE_SEQ));
    } else if (wseq != 0u) {
        o->face_from = sf_rds8(w, W + SF_PFP_FACE);
        o->face_to = sf_rds8(w, W + SF_PFP_FACE_NEXT);
        o->face_rate = sf_rdf(w, W + SF_PFP_RATE);
        o->face_letter = sf_letter_at(w, S, wseq);
    }
    o->face_activity = sf_settled(w, S, E);
    if (o->face_activity == SF_ACT_CLOSE) {
        o->face_activity = SA1_FACE_ACT_LIPSYNC;
    }
    if (o->face_activity == SA1_FACE_ACT_NONE && t == 0 && wseq != 0u) {
        o->face_activity = SA1_FACE_ACT_GAMEPLAY;
    }
    /* face_shown (review R.5): the EV head linked under the face parent, and the parent in the tree the display draws */
    if (sf_arena(W)) {
        uint32_t parent = sf_rd32(w, W + SF_PFP_PARENT), object = sf_rd32(w, W + SF_PFP_OBJECT);
        if (sf_arena(parent) && sf_same(sf_rd32(w, parent + SF_OBJ_SIBLING), object) && !w->sg.look) {
            uint32_t head = sf_rd32(w, E + SF_EWP_LIST), drawn = 0, budget = 512u;
            if (head != 0u) {
                drawn = sf_rd32(w, head + 4u);                  /* the queue head's action.object */
            } else if (sf_arena(pwp)) {
                uint32_t pl = sf_rd32(w, pwp + SF_PWP_PLACTPTR), act = sf_rd16(w, pwp + SF_PWP_ACTION);
                uint32_t ap = pl != 0u ? sf_rd32(w, pl + 16u * act) : 0u;
                drawn = ap != 0u ? sf_rd32(w, ap) : 0u;         /* the gameplay action's NJS_ACTION.object */
            }
            o->face_shown = drawn == 0u || sf_tree_has(w, drawn, parent, &budget) ? 1u : 0u;
        }
    }
}

/* ---- the API side's calls (src/sa1_face.c; outside ticks) ----------------------------------------------------------- */
void sa1wf_get_stats(const struct sa1g_world *w, sa1wf_stats *out)
{
    const sf_state *S = sf_of(w);
    memset(out, 0, sizeof *out);
    if (S == NULL) {
        return;
    }
    out->pairs = S->pairs;
    out->requests = S->requests;
    out->cancels = S->cancels;
    out->rearms = S->rearms;
    out->sp_runs = S->sp_runs;
    out->sp_links = S->sp_links;
    out->sp_skips = S->sp_skips;
    out->sp_unlinks = S->sp_unlinks;
    out->keys = S->keys;
    out->block = S->self;
    out->block_size = (uint32_t)sizeof(sf_state);
}

int32_t sa1wf_world_kind(const struct sa1g_world *w)
{
    return w != NULL ? w->kind : -1;
}

uint32_t sa1wf_world_letters(const struct sa1g_world *w)
{
    sa1_face_info info;
    sa1f_mem m;
    if (w == NULL) {
        return 0u;
    }
    memset(&info, 0, sizeof info);
    m.ptr = sf_view_ptr;
    m.user = w;
    return sa1f_compute(&m, w->kind, &info) == SA1_OK ? info.letters : 0u;
}

static sa1_status sf_begin(sa1_context *ctx, struct sa1g_world *w, sf_state **out)
{
    *out = NULL;
    if (w->dead) {
        return sa1_fail(ctx, w->dead_status != SA1_OK ? w->dead_status : SA1_ERR_GAME_FAULT, "the character's world faulted: "
                        "%s", w->fault.msg);
    }
    *out = sf_attach(ctx, w);
    if (*out == NULL) {
        return sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "faces: the world's extension segment is full");
    }
    return SA1_OK;
}

sa1_status sa1wf_latch_mode(sa1_context *ctx, struct sa1g_world *w, uint32_t mode)
{
    sf_state *S;
    sa1_status st = sf_begin(ctx, w, &S);
    if (st != SA1_OK) {
        return st;
    }
    w->face.persist[SF_P_MODE] = mode;
    return SA1_OK;
}

sa1_status sa1wf_latch_script(sa1_context *ctx, struct sa1g_world *w, const char *script, uint32_t len, uint32_t flags)
{
    sf_state *S;
    sa1_status st;
    if (len == 0u || len > SA1F_SCRIPT_MAX) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sf_begin(ctx, w, &S);
    if (st != SA1_OK) {
        return st;
    }
    memcpy(S->req_script, script, len);
    S->req_script[len] = 0;
    S->req_len = len;
    S->req_flags = flags;
    S->req = SF_REQ_SCRIPT;
    return SA1_OK;
}

sa1_status sa1wf_latch_sequence(sa1_context *ctx, struct sa1g_world *w, const sa1_face_entry *e, uint32_t count,
                                uint32_t flags)
{
    sf_state *S;
    sa1_status st;
    uint32_t need;
    if (count == 0u || count > SA1_FACE_SEQ_MAX) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    need = sf_count_seq(w, e, count, flags);
    if (need == 0u || need > SF_STEPS_MAX) {
        return sa1_fail(ctx, SA1_ERR_LIMIT, "sa1_character_face_sequence: %u steps (a face animation holds at most %u)",
                        (unsigned)need, (unsigned)SF_STEPS_MAX);
    }
    st = sf_begin(ctx, w, &S);
    if (st != SA1_OK) {
        return st;
    }
    memcpy(S->req_entries, e, (size_t)count * sizeof *e);
    S->req_count = count;
    S->req_flags = flags;
    S->req = SF_REQ_SEQUENCE;
    return SA1_OK;
}

sa1_status sa1wf_latch_stop(sa1_context *ctx, struct sa1g_world *w)
{
    sf_state *S;
    sa1_status st = sf_begin(ctx, w, &S);
    if (st != SA1_OK) {
        return st;
    }
    S->req = SF_REQ_STOP;
    S->req_flags = 0;
    return SA1_OK;
}

sa1_status sa1wf_latch_level(sa1_context *ctx, struct sa1g_world *w, int32_t q, uint32_t style)
{
    if (w->dead) {
        return w->dead_status != SA1_OK ? w->dead_status : SA1_ERR_GAME_FAULT;     /* the per-frame path: no log */
    }
    if (w->face.pre == NULL && q < 0) {
        return SA1_OK;          /* lip sync off in a world that never had a face call: nothing to latch, no hook */
    }
    if (sf_attach(ctx, w) == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    w->face.persist[SF_P_LEVEL] = q < 0 ? 0u : (uint32_t)q + 1u;
    w->face.persist[SF_P_STYLE] = style;            /* LADDER (| SA1F_SAME_PAIR) or TALK */
    return SA1_OK;
}

/* round 0.9.4c (SA1_API_FACE_MOUTH): the host's mouth shares the level's latch (the last level or mouth call before a pair
   wins: a switch between them while talking is no restart - sf_lipsync's LADDER branch serves both, act stays LIPSYNC) */
sa1_status sa1wf_latch_mouth(sa1_context *ctx, struct sa1g_world *w, uint32_t key_a, uint32_t key_b, int32_t q,
                             uint32_t flags, uint32_t *keys_out)
{
    sf_state *S;
    uint32_t keys;
    *keys_out = 0u;
    if (w->dead) {
        return w->dead_status != SA1_OK ? w->dead_status : SA1_ERR_GAME_FAULT;     /* the per-frame path: no log */
    }
    if (w->face.persist[SF_P_NKEYS] != 0u) {
        keys = w->face.persist[SF_P_NKEYS] - 1u;        /* the kind never changes: read once per world */
    } else {
        S = sf_of(w);
        keys = S != NULL ? S->keys : sf_world_keys(w);  /* the block's count, else read from the world's own arena */
        if (keys != 0u) {
            w->face.persist[SF_P_NKEYS] = keys + 1u;    /* host memory only (no hook, no game write) */
        }
    }
    *keys_out = keys;
    if (key_a >= keys || key_b >= keys) {
        return SA1_ERR_INVALID_ARGUMENT;                /* nothing latched, nothing installed */
    }
    if (w->face.pre == NULL && q < 0) {
        return SA1_OK;          /* off in a world that never had a face call: nothing to latch, no hook (as the level) */
    }
    if (sf_attach(ctx, w) == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    w->face.persist[SF_P_KEYS] = key_a | key_b << 8;
    w->face.persist[SF_P_LEVEL] = q < 0 ? 0u : (uint32_t)q + 1u;
    w->face.persist[SF_P_STYLE] = SF_STYLE_MOUTH | (flags & SA1F_SAME_PAIR);
    return SA1_OK;
}
