/*
 * sa1_world_emote.c - emotes inside a character world (round 0.9.4a, SA1_API_EMOTE; sadc
 * docs/research/faces_wheel_plan.md 4.5 with its Review R.1, R.7, R.8, R.15-R.17; emote_wheel_lib.md 2, 4, 5.1). The
 * asset-layer side (the API, validation, the catalogue) is src/sa1_emote.c; the two meet through src/sa1_emote.h only
 * (review R.13).
 *
 * THE MECHANISM - what a cutscene does to a live player, minus the pad-off and the camera. Every player has an event
 * work (taskwk.ewp, made by PInitialize through 8C04B440) with an action queue: EV_SetAction's body 8C04B4D2 and
 * EV_SetMotion's 8C04B528 append {loop bit, blend frames, NJS_ACTION, texlist, speed} (one CAlloc(1, 0x18) each),
 * EV_ClrAction's body 8C04B48C frees the queue and the blend source; each character's own display draws the queue head
 * with 8C04B57C (the welds by 8C04BF00) instead of its gameplay action while the queue is not empty. The precedent is
 * the game's own cutscenes: EV001B (asm/overlays/ev001b, 0CB800A0-0CB80142) clears the queue and queues
 * SonicActionList_002_action at 0.5 frames per call, looped; 171 of the 207 event overlays import EV_SetAction. The
 * event-library wrappers 8C04A120 / 8C04A142 / 8C04A168 (asm, `if (tp)` + `tp->twp`) are not used: libsa1 calls the C
 * bodies they jump to.
 *
 * Nothing else is written into the game: no pad-off (8C04921A), no SetInputP, no event-running flag (B_8C7492AC), no
 * reqaction, no camera. The gameplay keeps running underneath in the character's standing mode (gravity, ground,
 * slopes, water, damage, moving floors, the idle timer). Identity (measured by the research probe on the 0.9.4 base and
 * again on this round's base, emote_test E1): two Sonic worlds fed the same 1,300 pairs, one emoting 1,100+ pairs with
 * cancels by input, keep position, speed, mode, action, flags, rings and rand identical in every pair.
 *
 * This file (the lane): w->emote.pre / post / fill, installed by the first sa1_character_emote / _emote_stop for a
 * world; a world that never gets one runs exactly as before (the reference worlds never do). Its block lives in segment
 * 0x18 (magic + self, as sa1_world_set.c's st_state); a rebuild resets the segment and with it ends the emote (the
 * arena and the queue are new as well); nothing persists.
 *
 * At the pair start (pre, sub step 0, after the pad update and before the executor; other sub steps: nothing):
 *   ready      player 0's taskwk, playerwk and event work; else everything stays latched (before the world's first
 *              pair)
 *   re-made    a player task other than the emote's (PInitialize again: jvmode 0) drops the emote without a call
 *              (review R.8)
 *   form       the form changed since the start (equipment 0x8000, the General look): cancel before the pair (its
 *              queued objects and texlists belong to the old form; the post rule would show them one pair longer)
 *   stop       NOW: 8C04B48C; BLEND: the groups not queued yet are dropped and the stand appended after the group being
 *              played (blended when it has that group's object; a loop of another object clears and queues the stand at
 *              its wrap, review R.1). (A group with several entries finishes before the stand: the queue's entries are
 *              never rewritten by the host.)
 *   start      only at a pair start that reads playerpwp[0]->mj.jvmode == 1 (8C04BF00 spins for ever otherwise: review
 *              R.8; else it stays latched); the gameplay rules (refusals in plain words: "in the air", "hurt",
 *              "carrying something", "on the board", "moving", "busy"; IN_AIR: only "hurt" and "on the board"), the
 *              input rule below ("moving"), the heap headroom (review R.7: the largest free block of the game heap's
 *              free list B_8C79F03C > 4 KB + 48 bytes per queue entry, else "the game's memory is full"); then 8C04B48C
 *              and the first group; Amy's attack rows 81-95 (class attack: not the dizzy 87 / 88): 8C0DE46C(0, 1.0)
 *              unless NO_PROPS. A refusal at the pair start is kept as a note for the API's next call (the world cannot
 *              log inside a pair). A request made before the world's first pair waits (instead of "busy" / "in the
 *              air") while the new character settles onto his floor (a world starts him falling, mode 12, for a pair
 *              or two).
 *   input      unless KEEP_INPUT: GetAnalog(twp, NULL, NULL) (8C06CD22: the dead zone and the no-control timer
 *              included) non-zero, or the pad's press (B_8C754C08[0].press) has the action / jump masks (D_8C19E38C /
 *              D_8C19E390): 8C04B48C before the pair, which then runs exactly as in a world that never emoted.
 * After every step (post): the queue ran empty -> the next group (a clear, then its entries: review R.1, below) or,
 *   at the end, 8C04B48C (it frees the blend source the queue keeps: the heap is back where it was) and Amy's 8C0DE46C(0,
 *   0.0). After the pair (sub step 1): cancel when the gameplay left the standing state (mode 1; kind 8: 71; not with
 *   IN_AIR), wtimer != 0 (hurt), the held-object flag (taskwk.flag 0x800) changed, the form changed, or a board mode
 *   (Sonic 62-68, Tails 48-54) - IN_AIR keeps every one of these but the standing rule (review R.15).
 *
 * THE ENTRIES. Per item: a table row - kind 8 with one of Sonic's objects: 8C04B528(twp, the substituted object of
 * sa1_manifest_char(8)->subst, the row's motion, SUPERSONIC 0C901EA0, ...); the General form (kind 0 with the look on):
 * plactptr's row as is (GT / GA), rows 130-134 from sa1w_world_super_lifted (the lifted copies; a fallback to the
 * B_CHAOS7 action with one warning while they are not built); every other world plactptr[row].actptr with 8C04B4D2 and
 * the display's texlist. An extra (the generated list's validated motions only: an arbitrary motion can fault the
 * process) - 8C04B528(twp, the kind's main body, its motion, NULL, ...). CHAIN: a once row also queues the game's
 * PL_ACTION.next chain up to its loop (not into the stand, not through a row the list does not offer; at most 16).
 * Speed (emote_wheel_lib.md 4.3, the TSVs' ev_speed): frames per 60 Hz step = racio (mtnmode 9 / 10 / 11: 1; mtnmode 12
 * or racio 0: 0.5) times the item's multiplier, times 2 for Sonic, Tails, Knuckles, Big and kind 8 (their displays
 * draw, and advance the queue, on sub step 0 only: the visibility test 8C038D00 fails on sub step 1) and 1 for Amy and
 * E-102 (twice per pair). The loop bit for the last entry when it plays LOOP; a once emote ends with a trailing stand
 * entry (the form's row 0 from plactptr; kind 8: 130) unless NO_TRAIL.
 *
 * GROUPS (review R.1). 8C04B57C's else branch keeps a finished entry as the blend source (buf44) WITHOUT freeing the
 * one it held (only the link branch frees it: sadc code_8C04B440.c), so an entry queued without a blend (linkframe 0)
 * behind another leaks 48 bytes of the game heap at its end (measured:
 * SADC/build/research/faces094a/review/out/probe_leak.txt). The lane never queues one: a blend (the emote's link,
 * default 8) only between entries of one object (a blend across objects of different node counts faults, 73 -> 74
 * nodes), and every unblended transition starts a new GROUP: when the queue has run empty, 8C04B48C (which frees the
 * blend source) and the next group's entries in the same post hook, so the next pair draws it.
 *
 * THE QUEUE MOVES ONLY ON DRAWN PAIRS (review R.17): 8C04B57C runs inside the display, which Sonic, Tails, Knuckles and
 * Big skip when the visibility test fails - live worlds test against the host camera (sa1_world_tick.c
 * sa1g_camera_view). An emote of a character outside the host's view pauses and goes on when he is seen again; nothing
 * here counts pairs.
 *
 * HOLD (round 0.9.4b, SA1_EMOTE_HOLD; sadc docs/research/objects2_plan.md 11.2: the owner's "body pose should stay").
 * An emote whose last entry plays once (its last item, or the last row of its CHAIN) keeps that entry's LAST FRAME instead
 * of the trailing stand entry, the way the game's own hold rows do: PSetMotion 8C079476 mtnmode 5 clamps the frame at
 * nbFrame - 1 (sadc code_8C079476.c, case 5). The queue cannot say "hold" by itself: a once entry advances when frame +
 * speed >= nbFrame - 1, so its last drawn frame is below nbFrame - 1 and the next pair would draw the gameplay stand (the
 * 0.9.4a pop the owner saw as "hold animations don't hold"). So the lane changes ITS OWN queue entry (one it queued through
 * 8C04B4D2 / 8C04B528; nothing of the gameplay is written): after a step in which the entry is the queue head in its
 * motion phase (not blending in from the entry before: buf44 == NULL or linkframe 0) and frame + speed reaches nbFrame - 1
 * (0.01 early: the prediction never lags the queue's own float test), it sets the entry's loop bit and its speed to the
 * rest (nbFrame - 1 - frame): the next drawn step lands on nbFrame - 1 and the loop rule (frame < nbFrame) keeps it there;
 * after that step the speed becomes 0 (a held step: every later drawn step draws nbFrame - 1). State HOLDING from then on.
 * The cancels are unchanged (input before a pair; the state, hurt, the held object, the form, the board after it; a new
 * emote clears the queue). STOP_BLEND releases the hold first: the entry gets back its once mode and its own speed, so it
 * ends at its next drawn step and the stand blends in from frame nbFrame - 1 (8C04B57C takes a once entry's blend source
 * at nbFrame - 1); STOP_NOW clears. A looping last entry is not held (it loops until a cancel or a stop). A held entry
 * never ends, so it never becomes a blend source behind an unblended entry: no leak (review R.1 of round 0.9.4a; E20).
 *
 * DEFERRED (faces_wheel_plan.md 4.8): the render hint and the CPU pose of a hidden pair read the gameplay action, not
 * the queue head (measured mid-pair deviation while emoting <= 0.81 against the gameplay's own 1.20; a later round
 * extends sa1_character.c's hint helper). UNVERIFIED: whether Big's rod shows in his fishing rows (the body's triangle
 * count does not change); board rows before 0.9.5 (the board is not drawn); how emotes look in Garry's Mod.
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_emote.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ---- the game's addresses ------------------------------------------------------------------------------------------ */
#define EM_FN_CLEAR         0x8C04B48Cu     /* EV_ClrAction's body (twp) */
#define EM_FN_SETACTION     0x8C04B4D2u     /* EV_SetAction's body (twp, NJS_ACTION *, texlist, speed, mode, linkframe) */
#define EM_FN_SETMOTION     0x8C04B528u     /* EV_SetMotion's body (twp, object, motion, texlist, speed, mode, linkframe) */
#define EM_FN_ANALOG        0x8C06CD22u     /* GetAnalog (twp, Angle *, Float *) */
#define EM_FN_HAMMER        0x8C0DE46Cu     /* Amy's hammer (pno, f): shown for f > 0 */
#define EM_ADDR_PADS        0x8C754C08u     /* PDS_PERIPHERAL B_8C754C08[8] (0x34 each), press at +0x10 */
#define EM_ADDR_MASK_ACTION 0x8C19E38Cu     /* Uint32 D_8C19E38C: the action button mask */
#define EM_ADDR_MASK_JUMP   0x8C19E390u     /* Uint32 D_8C19E390: the jump button mask */
#define EM_ADDR_HEAP_FREE   0x8C79F03Cu     /* MEMBLK *B_8C79F03C: the heap's free list (code_8C098998.c) */
#define EM_TEXLIST_SUPER    0x0C901EA0u     /* TEXLIST_SUPERSONIC (B_CHAOS7) */
#define EM_TWP_FLAG         0x04u           /* taskwk.flag (Sint16): 0x0003 ground, 0x0800 holding an object */
#define EM_TWP_WTIMER       0x06u           /* taskwk.wtimer (Uint16): hurt */
#define EM_TWP_EWP          0x3Cu           /* taskwk.ewp */
#define EM_EWP_F3C          0x3Cu           /* eventwk.f3C: the queue head's frame */
#define EM_EWP_LIST40       0x40u           /* eventwk.list40: the queue head */
#define EM_EWP_BUF44        0x44u           /* eventwk.buf44: the blend source (the entry before) */
#define EM_NODE_MODE        0x00u           /* EV_ACTION_LIST.mode (Uint8): bit 0 loop */
#define EM_NODE_LINK        0x01u           /* EV_ACTION_LIST.linkframe (Uint8) */
#define EM_NODE_MOTION      0x08u           /* EV_ACTION_LIST.action.motion */
#define EM_NODE_NEXT        0x10u           /* EV_ACTION_LIST.next */
#define EM_NODE_SPEED       0x14u           /* EV_ACTION_LIST.speed (Float): frames per drawn step */
#define EM_MOT_NBFRAME      0x04u           /* NJS_MOTION.nbFrame (Uint32) */
#define EM_HOLD_EARLY       0.01f           /* HOLD: engage this much before frame + speed reaches nbFrame - 1 */
#define EM_HOLD_AT          0.001f          /* HOLD: the frame counts as the last one this close to it */
#define EM_PWP_EQUIPMENT    0x004u          /* playerwk.equipment (0x8000 = Super Sonic) */
#define EM_PWP_SPD          0x038u          /* playerwk.spd (player space) */
#define EM_PWP_JVMODE       0x122u          /* playerwk.mj.jvmode (Sint16): 1 = the welds are set up */
#define EM_PWP_PLACT        0x140u          /* playerwk.mj.plactptr: PL_ACTION[] (16 bytes) */
#define EM_FLAG_GROUND      0x0003u
#define EM_FLAG_HOLD        0x0800u

#define EM_MAGIC            0x45544F4Du     /* "MOTE" */
#define EM_MAX_ENTRIES      48u             /* 8 items, CHAIN, the stand */
#define EM_CHAIN_MAX        16u
#define EM_ENTRY_HEAP       48u             /* game heap bytes per queue entry: CAlloc(1, 0x18) + the allocator's header */
#define EM_HEADROOM         4096u           /* review R.7 */
#define EM_LINK_DEFAULT     8u
#define EM_LINK_MAX         120

#define EM_HOW_ACTION       0u              /* 8C04B4D2 */
#define EM_HOW_MOTION       1u              /* 8C04B528 */

typedef struct em_entry {
    uint32_t how;
    uint32_t action;            /* EM_HOW_ACTION: the NJS_ACTION (guest; the arena or segment 0x18) */
    uint32_t object, motion;    /* EM_HOW_MOTION */
    uint32_t texlist;           /* guest, 0: keep the display's */
    uint32_t obj_key;           /* the drawn object (& 0x1FFFFFFF): the grouping */
    float speed;
    uint32_t loop;              /* the mode bit 0 */
    uint32_t link;              /* linkframe (0: the first entry of its group) */
    uint32_t group;
    int32_t item;               /* the host's item (0..count-1); count: a stand entry */
    int32_t row, extra;         /* -1 none */
    uint32_t stand;             /* 1: a stand entry (the trailing stand, the stop's stand): ENDING */
    uint32_t hold;              /* 1: SA1_EMOTE_HOLD's entry (the emote's last, a once entry): held on its last frame */
    uint32_t node;              /* guest address of its queue entry once queued (0: not queued) */
} em_entry;

typedef struct em_state {
    uint32_t magic, self;
    /* the latch (written by the API side between ticks; applied at the next pair start) */
    uint32_t req_start, req_early;  /* req_early: asked before the world's first pair (it waits while he settles) */
    sa1_emote req;
    uint32_t req_stop, req_how;
    /* the running emote */
    uint32_t state;             /* SA1_EMOTE_STATE_* */
    uint32_t ptwp;              /* player 0's taskwk at its start */
    uint32_t flags, count;
    int32_t kind;
    uint32_t form;
    uint32_t super_bit, held;   /* equipment & 0x8000, taskwk.flag & 0x800 at the start */
    int32_t look;               /* the General look at the start */
    uint32_t hammer;            /* Amy's hammer shown by this emote */
    uint32_t link;              /* the emote's link (frames) */
    uint32_t n, group, groups;  /* entries, the group being played, groups */
    uint32_t stop_wrap;         /* STOP_BLEND on a loop of another object: the stand group at the loop's wrap */
    float last_f3c;
    uint32_t last_head;
    /* HOLD (round 0.9.4b): 0 none, 1 engaged (the entry steps onto its last frame), 2 held (speed 0, state HOLDING) */
    uint32_t hold_phase;
    uint32_t hold_node;         /* the held entry's queue node (guest), 0: none */
    float hold_speed;           /* its own speed (restored when a stop releases it) */
    em_entry e[EM_MAX_ENTRIES];
    /* the note for the API side's next call */
    uint32_t note;
    char why[SA1EM_WHY_MAX];
    uint32_t warned_lift;
    /* counters (sa1em_world_info) */
    uint32_t starts, ends, cancels_input, cancels_state, stops, refused, dropped, groups_queued, entries_made;
    uint32_t waits_jvmode, heap_largest, waits_settle;
    uint32_t holds, hold_releases;
} em_state;

/* ---- guest memory: the arena and the allocated part of segment 0x18 (GT, GA and their actions live there) ----------- */
static const uint8_t *em_ptr(const sa1g_world *w, uint32_t g)
{
    uint32_t seg = (g >> 24) & 31u, off = g & 0xFFFFFCu;
    if (g == 0) {
        return NULL;
    }
    if (seg == SA1G_SEG_ARENA) {
        return off <= SA1G_ARENA_SIZE - 4u ? w->ram + off : NULL;
    }
    if (seg == SA1G_SEG_EXT && w->ext != NULL) {
        return (size_t)off + 4u <= w->ext_used ? w->ext + off : NULL;
    }
    return NULL;
}

static int em_valid(const sa1g_world *w, uint32_t g)
{
    return em_ptr(w, g) != NULL;
}

static int em_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static uint32_t em_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    const uint8_t *p = em_ptr(w, g & ~3u);
    if (p != NULL) {
        memcpy(&v, p, 4);
    }
    return v;
}

static uint32_t em_rd16(const sa1g_world *w, uint32_t g)
{
    return (em_rd32(w, g & ~3u) >> (8u * (g & 2u))) & 0xFFFFu;
}

static uint32_t em_rd8(const sa1g_world *w, uint32_t g)
{
    return (em_rd32(w, g & ~3u) >> (8u * (g & 3u))) & 0xFFu;
}

static float em_rdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = em_rd32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* writes: only into the lane's own queue entries (arena words; HOLD) */
static void em_wr32(sa1g_world *w, uint32_t g, uint32_t v)
{
    if (em_arena(g)) {
        sa1g_world_wr32(w, g & ~3u, v);
    }
}

static void em_wrf(sa1g_world *w, uint32_t g, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    em_wr32(w, g, v);
}

static void em_wr8(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint32_t a = g & ~3u, sh = 8u * (g & 3u), o = em_rd32(w, a);
    em_wr32(w, a, (o & ~(0xFFu << sh)) | ((v & 0xFFu) << sh));
}

static uint32_t em_key(uint32_t g)
{
    return g & 0x1FFFFFFFu;
}

/* ---- the block --------------------------------------------------------------------------------------------------------- */
static em_state *em_state_of(const sa1g_world *w)
{
    uint32_t g;
    em_state *S;
    if (w == NULL || w->ext == NULL || w->emote.state == 0) {
        return NULL;
    }
    g = w->emote.state;
    if (((g >> 24) & 31u) != SA1G_SEG_EXT || (g & 15u) != 0 || (size_t)(g & 0xFFFFFFu) + sizeof(em_state) > w->ext_used) {
        return NULL;
    }
    S = (em_state *)(void *)(w->ext + (g & 0xFFFFFFu));
    return S->magic == EM_MAGIC && S->self == g ? S : NULL;
}

static em_state *em_state_make(sa1g_world *w)
{
    em_state *S = em_state_of(w);
    uint32_t g;
    if (S != NULL) {
        return S;
    }
    g = sa1g_world_ext_alloc(w, sizeof(em_state), 16, (void **)&S);
    if (g == 0 || S == NULL) {
        return NULL;
    }
    memset(S, 0, sizeof *S);
    S->magic = EM_MAGIC;
    S->self = g;
    w->emote.state = g;
    return S;
}

static void em_note(em_state *S, const char *why)
{
    size_t n = strlen(why);
    if (n >= sizeof S->why) {
        n = sizeof S->why - 1u;
    }
    memcpy(S->why, why, n);
    S->why[n] = 0;
    S->note = 1;
}

/* ---- player 0 ---------------------------------------------------------------------------------------------------------- */
typedef struct em_player {
    uint32_t twp, pwp, ewp, plact;
    int32_t kind;
    uint32_t form;
    uint32_t mode, flag, wtimer, jvmode, equipment;
    int32_t look;
    float speed;                /* the largest |playerwk.spd| component */
} em_player;

static void em_kind_form(const sa1g_world *w, int32_t *kind, uint32_t *form)
{
    *kind = w->kind;
    *form = (w->kind == SA1_CHAR_SONIC && w->sg.look) ? SA1_EMOTE_FORM_SUPER : SA1_EMOTE_FORM_NORMAL;
    if (w->kind == SA1_CHAR_SUPER_SONIC) {
        *form = SA1_EMOTE_FORM_NORMAL;
    }
}

/* 1: player 0 has its taskwk, playerwk and event work */
static int em_player_of(const sa1g_world *w, em_player *p)
{
    memset(p, 0, sizeof *p);
    em_kind_form(w, &p->kind, &p->form);
    p->twp = em_rd32(w, SA1G_ADDR_PLAYER_TWP);
    p->pwp = em_rd32(w, SA1G_ADDR_PLAYER_PWP);
    if (!em_arena(p->twp) || !em_arena(p->pwp)) {
        return 0;
    }
    p->ewp = em_rd32(w, p->twp + EM_TWP_EWP);
    if (!em_arena(p->ewp)) {
        return 0;
    }
    p->plact = em_rd32(w, p->pwp + EM_PWP_PLACT);
    p->mode = em_rd8(w, p->twp);
    p->flag = em_rd16(w, p->twp + EM_TWP_FLAG);
    p->wtimer = em_rd16(w, p->twp + EM_TWP_WTIMER);
    p->jvmode = em_rd16(w, p->pwp + EM_PWP_JVMODE);
    p->equipment = em_rd16(w, p->pwp + EM_PWP_EQUIPMENT);
    p->look = w->sg.look;
    {
        uint32_t k;
        for (k = 0; k < 3u; k++) {
            float v = em_rdf(w, p->pwp + EM_PWP_SPD + 4u * k);
            v = v < 0.0f ? -v : v;
            p->speed = v > p->speed ? v : p->speed;
        }
    }
    return em_valid(w, p->plact);
}

static uint32_t em_stand_mode(int32_t kind)
{
    return kind == SA1_CHAR_SUPER_SONIC ? 71u : 1u;
}

static int em_board(const em_player *p)
{
    if (p->kind == SA1_CHAR_SONIC) {
        return p->mode >= 62u && p->mode <= 68u;
    }
    if (p->kind == SA1_CHAR_TAILS) {
        return p->mode >= 48u && p->mode <= 54u;
    }
    return 0;
}

/* the gameplay refusals of a start (NULL: it may start) */
static const char *em_refusal(const em_player *p, uint32_t flags)
{
    if (p->wtimer != 0) {
        return "hurt";
    }
    if (em_board(p)) {
        return "on the board";
    }
    if (flags & SA1_EMOTE_IN_AIR) {
        return NULL;
    }
    if (p->flag & EM_FLAG_HOLD) {
        return "carrying something";
    }
    if (p->mode == em_stand_mode(p->kind)) {
        return NULL;
    }
    if (!(p->flag & EM_FLAG_GROUND)) {
        return "in the air";
    }
    return p->speed > 0.01f ? "moving" : "busy";
}

/* ---- the game heap (as sa1_world_set.c st_heap_largest) ---------------------------------------------------------------- */
uint32_t sa1em_world_heap_largest(const sa1g_world *w)
{
    uint32_t blk = em_rd32(w, EM_ADDR_HEAP_FREE), l = 0, n = 0;
    while (em_arena(blk) && n < 0x10000u) {
        uint32_t end = em_rd32(w, blk + 8u), len;
        if (end == 0 || !em_arena(end) || (end & 0xFFFFFFu) <= (blk & 0xFFFFFFu)) {
            break;
        }
        len = (end & 0xFFFFFFu) - (blk & 0xFFFFFFu);
        l = len > l ? len : l;
        blk = em_rd32(w, blk);
        n++;
    }
    return l;
}

static int em_heap_ok(const sa1g_world *w, uint32_t entries, uint32_t *largest)
{
    uint32_t l = sa1em_world_heap_largest(w);
    if (largest != NULL) {
        *largest = l;
    }
    return l > EM_HEADROOM + EM_ENTRY_HEAP * entries;
}

/* ---- the plan ---------------------------------------------------------------------------------------------------------- */
static int em_loop_mode(int32_t mtnmode)
{
    return mtnmode == 3 || mtnmode == 9 || mtnmode == 10 || mtnmode == 11 || mtnmode == 12;
}

static int em_gated(int32_t kind)
{
    return kind != SA1_CHAR_AMY && kind != SA1_CHAR_GAMMA;
}

/* frames per 60 Hz step of a row (emote_wheel_lib.md 4.3, the TSVs' ev_speed) */
static float em_base(int32_t mtnmode, float racio)
{
    if (mtnmode == 9 || mtnmode == 10 || mtnmode == 11) {
        return 1.0f;
    }
    if (mtnmode == 12 || !(racio > 0.0f)) {
        return 0.5f;
    }
    return racio;
}

typedef struct em_plan_ctx {
    const sa1g_world *w;
    const em_player *p;
    const sa1em_list *list;
    const sa1_char_desc *desc;
    uint32_t num_actions;
    float rate;                 /* 2 (once per pair) or 1 (twice) */
    int lift_missing;
    char why[SA1EM_WHY_MAX];
} em_plan_ctx;

static void em_why(em_plan_ctx *c, const char *what, uint32_t index, const char *name)
{
    static const char *const names[SA1_CHAR_COUNT] = { "Sonic", "Eggman", "Tails", "Knuckles", "Tikal", "Amy", "E-102",
                                                       "Big", "Super Sonic" };
    const char *who = (c->p->kind >= 0 && c->p->kind < SA1_CHAR_COUNT) ? names[c->p->kind] : "this character";
    if (c->p->kind == SA1_CHAR_SONIC && c->p->form == SA1_EMOTE_FORM_SUPER) {
        who = "Super Sonic";
    }
    if (name != NULL) {
        snprintf(c->why, sizeof c->why, "%s is not available for %s", name, who);
    } else {
        snprintf(c->why, sizeof c->why, "%s %u is not available for %s", what, (unsigned)index, who);
    }
}

/* the entry of a table row in this world (0: not playable here) */
static int em_row_entry(em_plan_ctx *c, uint32_t row, float mult, em_entry *x)
{
    const sa1g_world *w = c->w;
    uint32_t pa, act, obj, mot;
    int32_t mm;
    float racio;
    if (row >= c->num_actions) {
        return 0;
    }
    pa = c->p->plact + 16u * row;
    act = em_rd32(w, pa);
    mm = (int32_t)(int8_t)em_rd8(w, pa + 5u);
    racio = em_rdf(w, pa + 12u);
    if (!em_valid(w, act)) {
        return 0;
    }
    obj = em_rd32(w, act);
    mot = em_rd32(w, act + 4u);
    if (!em_valid(w, obj) || !em_valid(w, mot)) {
        return 0;
    }
    memset(x, 0, sizeof *x);
    x->row = (int32_t)row;
    x->extra = -1;
    x->speed = em_base(mm, racio) * c->rate * mult;
    x->loop = (uint32_t)em_loop_mode(mm);    /* provisional: the caller keeps it only for the last entry */
    if (c->p->kind == SA1_CHAR_SUPER_SONIC && row < 130u) {
        /* kind 8: Sonic's objects substituted, the SUPERSONIC texlist (Sonic's own NJS_ACTION would draw Sonic's body with
           the SUPERSONIC textures: emote_wheel_lib.md 4.7) */
        uint32_t i, sub = 0;
        for (i = 0; c->desc != NULL && i < c->desc->num_subst; i++) {
            if (em_key(c->desc->subst[i][0]) == em_key(obj)) {
                sub = c->desc->subst[i][1];
            }
        }
        if (sub == 0 || !em_valid(w, sub)) {
            return 0;
        }
        x->how = EM_HOW_MOTION;
        x->object = sub;
        x->motion = mot;
        x->texlist = EM_TEXLIST_SUPER;
        x->obj_key = em_key(sub);
        return 1;
    }
    x->how = EM_HOW_ACTION;
    x->action = act;
#if defined(SA1W_HAS_SUPER_LIFTED)
    if (c->p->kind == SA1_CHAR_SONIC && c->p->form == SA1_EMOTE_FORM_SUPER && row >= 130u && row <= 134u) {
        uint32_t lifted = sa1w_world_super_lifted(w, row);
        if (lifted != 0 && em_valid(w, lifted) && em_valid(w, em_rd32(w, lifted))) {
            x->action = lifted;
            obj = em_rd32(w, lifted);
        } else {
            c->lift_missing = 1;    /* B_CHAOS7's own action: 1.0 low at Sonic's center_height (the toe dip) */
        }
    }
#else
    if (c->p->kind == SA1_CHAR_SONIC && c->p->form == SA1_EMOTE_FORM_SUPER && row >= 130u && row <= 134u) {
        c->lift_missing = 1;
    }
#endif
    x->obj_key = em_key(obj);
    return 1;
}

/* the form's stand entry: row 0 of plactptr (kind 8: 130 on the Super body) */
static int em_stand_entry(em_plan_ctx *c, em_entry *x)
{
    uint32_t row = c->p->kind == SA1_CHAR_SUPER_SONIC ? 130u : 0u;
    if (!em_row_entry(c, row, 1.0f, x)) {
        return 0;
    }
    x->loop = 0;
    x->stand = 1;
    return 1;
}

/* the entries of an emote in this world (0: refused, c->why says why) */
static int em_plan(em_plan_ctx *c, const sa1_emote *e, em_entry *out, uint32_t *n_out, uint32_t *link_out)
{
    uint32_t i, n = 0, j, link;
    int last_loops = 0;
    c->desc = sa1_manifest_char(c->p->kind);
    c->num_actions = c->desc != NULL ? c->desc->num_actions : 0u;
    c->rate = em_gated(c->p->kind) ? 2.0f : 1.0f;
    c->lift_missing = 0;
    c->why[0] = 0;
    link = e->link < 0 ? EM_LINK_DEFAULT : (uint32_t)(e->link > EM_LINK_MAX ? EM_LINK_MAX : e->link);
    for (i = 0; i < e->count; i++) {
        const sa1_emote_item *it = &e->items[i];
        float mult = it->speed > 0.0f ? it->speed : 1.0f;
        int last_item = i + 1u == e->count;
        uint32_t pos = 0;
        const sa1em_entry *ce = sa1em_find(c->list, it->source, it->index, &pos);
        em_entry x;
        if (ce == NULL) {
            em_why(c, it->source == SA1_EMOTE_EXTRA ? "extra" : "row", it->index, NULL);
            return 0;
        }
        if (n >= EM_MAX_ENTRIES - 1u) {
            snprintf(c->why, sizeof c->why, "the emote is too long");
            return 0;
        }
        if (it->source == SA1_EMOTE_EXTRA) {
            const sa1_char_desc *d = c->desc;
            uint32_t body = d != NULL ? d->main_object : 0u, mot = ce->motion;
            if (!em_valid(c->w, body) || !em_valid(c->w, mot) || em_rd16(c->w, mot + 4u) == 0u) {
                em_why(c, "extra", it->index, ce->name);
                return 0;
            }
            memset(&x, 0, sizeof x);
            x.how = EM_HOW_MOTION;
            x.object = body;
            x.motion = mot;
            x.obj_key = em_key(body);
            x.speed = 1.0f * c->rate * mult;
            x.loop = 0;
            x.row = -1;
            x.extra = (int32_t)it->index;
            x.item = (int32_t)i;
            last_loops = last_item && it->play == SA1_EMOTE_PLAY_LOOP;
            x.loop = (uint32_t)last_loops;
            out[n++] = x;
            continue;
        }
        if (!em_row_entry(c, it->index, mult, &x)) {
            em_why(c, "row", it->index, ce->name);
            return 0;
        }
        x.item = (int32_t)i;
        {
            int loops = it->play == SA1_EMOTE_PLAY_LOOP ? 1 : it->play == SA1_EMOTE_PLAY_ONCE ? 0 : (int)x.loop;
            x.loop = (uint32_t)(last_item && loops);
            out[n++] = x;
            last_loops = last_item && loops;
            if (!loops && (e->flags & SA1_EMOTE_CHAIN)) {
                /* the game's next chain up to its loop (the gameplay's own follow-ups: 4 > 5 > 6, 56 > 57) */
                uint32_t cur = it->index, k, seen[EM_CHAIN_MAX + 1u], ns = 0;
                uint32_t stand = c->p->kind == SA1_CHAR_SUPER_SONIC ? 130u : 0u;
                seen[ns++] = cur;
                for (k = 0; k < EM_CHAIN_MAX && n < EM_MAX_ENTRIES - 1u; k++) {
                    uint32_t nxt = em_rd8(c->w, c->p->plact + 16u * cur + 6u), m, dup = 0;
                    em_entry y;
                    if (nxt >= c->num_actions || nxt == stand || nxt == 0u) {
                        break;
                    }
                    for (m = 0; m < ns; m++) {
                        dup |= seen[m] == nxt;
                    }
                    if (dup || sa1em_find(c->list, SA1_EMOTE_ROW, nxt, NULL) == NULL || !em_row_entry(c, nxt, mult, &y)) {
                        break;
                    }
                    seen[ns++] = nxt;
                    y.item = (int32_t)i;
                    loops = (int)y.loop;
                    y.loop = (uint32_t)(last_item && loops);
                    out[n++] = y;
                    last_loops = last_item && loops;
                    if (loops) {
                        break;
                    }
                    cur = nxt;
                }
            }
        }
    }
    if (!last_loops && (e->flags & SA1_EMOTE_HOLD) && n > 0) {
        out[n - 1u].hold = 1;   /* round 0.9.4b: held on its last frame instead of the trailing stand */
    }
    if (!last_loops && !(e->flags & (SA1_EMOTE_NO_TRAIL | SA1_EMOTE_HOLD)) && n < EM_MAX_ENTRIES) {
        em_entry s;
        if (em_stand_entry(c, &s)) {
            s.item = (int32_t)e->count;
            out[n++] = s;
        }
    }
    /* groups (review R.1): a blend only between entries of one object; every unblended transition starts a group */
    for (j = 0; j < n; j++) {
        if (j == 0) {
            out[j].link = 0;
            out[j].group = 0;
        } else if (link > 0 && out[j].obj_key == out[j - 1u].obj_key) {
            out[j].link = link;
            out[j].group = out[j - 1u].group;
        } else {
            out[j].link = 0;
            out[j].group = out[j - 1u].group + 1u;
        }
        out[j].node = 0;
    }
    *n_out = n;
    *link_out = link;
    return n > 0;
}

static uint32_t em_group_entries(const em_entry *e, uint32_t n, uint32_t group)
{
    uint32_t j, k = 0;
    for (j = 0; j < n; j++) {
        k += e[j].group == group;
    }
    return k;
}

/* ---- calls into the game (inside the world) ----------------------------------------------------------------------------- */
typedef void (*em_clear_fn)(void *);
typedef void (*em_setaction_fn)(void *, void *, void *, float, int32_t, int32_t);
typedef void (*em_setmotion_fn)(void *, void *, void *, void *, float, int32_t, int32_t);
typedef int32_t (*em_analog_fn)(void *, void *, void *);
typedef int32_t (*em_hammer_fn)(uint8_t, float);

static void *em_fn(uint32_t dc)
{
    return sa1_fn(SA1_GFN_C(dc), 0u);
}

static void em_clear(em_state *S, uint32_t twp)
{
    ((em_clear_fn)em_fn(EM_FN_CLEAR))(sa1_decode(twp));
    S->last_head = 0;
    S->last_f3c = 0.0f;
    S->hold_phase = 0;          /* the clear freed the held entry */
    S->hold_node = 0;
}

static void em_hammer(em_state *S, float f)
{
    ((em_hammer_fn)em_fn(EM_FN_HAMMER))(0, f);
    S->hammer = f > 0.0f;
}

static void em_push(em_state *S, uint32_t twp, em_entry *x)
{
    void *tl = x->texlist != 0 ? sa1_decode(x->texlist) : NULL;
    if (x->how == EM_HOW_MOTION) {
        ((em_setmotion_fn)em_fn(EM_FN_SETMOTION))(sa1_decode(twp), sa1_decode(x->object), sa1_decode(x->motion), tl,
                                                   x->speed, (int32_t)x->loop, (int32_t)x->link);
    } else {
        ((em_setaction_fn)em_fn(EM_FN_SETACTION))(sa1_decode(twp), sa1_decode(x->action), tl, x->speed, (int32_t)x->loop,
                                                   (int32_t)x->link);
    }
    S->entries_made++;
}

/* the queue's last node (guest), 0: empty */
static uint32_t em_queue_last(const sa1g_world *w, uint32_t ewp)
{
    uint32_t node = em_rd32(w, ewp + EM_EWP_LIST40), last = 0, guard = 0;
    while (em_arena(node) && guard++ < 256u) {
        last = node;
        node = em_rd32(w, node + EM_NODE_NEXT);
    }
    return last;
}

/* HOLD (round 0.9.4b; the file comment): after a step (and right after a group is queued), the held entry at the queue
 * head in its motion phase is kept on its last frame: engaged when frame + speed reaches nbFrame - 1 (the loop bit and the
 * rest as its speed), held once the frame is there (speed 0, state HOLDING). Writes only the lane's own queue entry. */
static void em_hold_step(sa1g_world *w, em_state *S, const em_player *p)
{
    uint32_t head, j, mot, nb, h = 0xFFFFFFFFu;
    float f, last, spd;
    head = em_rd32(w, p->ewp + EM_EWP_LIST40);
    if (!em_arena(head)) {
        return;
    }
    for (j = 0; j < S->n && h == 0xFFFFFFFFu; j++) {
        if (S->e[j].node == head && S->e[j].group == S->group) {
            h = j;
        }
    }
    if (h == 0xFFFFFFFFu || !S->e[h].hold || (S->hold_node != 0 && S->hold_node != head)) {
        return;
    }
    if (em_rd32(w, p->ewp + EM_EWP_BUF44) != 0 && em_rd8(w, head + EM_NODE_LINK) != 0) {
        return;                 /* still blending in from the entry before: f3C counts the blend, not the motion */
    }
    mot = em_rd32(w, head + EM_NODE_MOTION);
    nb = em_valid(w, mot) ? em_rd32(w, mot + EM_MOT_NBFRAME) : 0u;
    if (nb == 0) {
        return;
    }
    last = (float)nb - 1.0f;
    f = em_rdf(w, p->ewp + EM_EWP_F3C);
    if (S->hold_phase == 0) {
        spd = em_rdf(w, head + EM_NODE_SPEED);
        if (f + spd < last - EM_HOLD_EARLY) {
            return;             /* its next drawn step does not end it yet */
        }
        S->hold_node = head;
        S->hold_speed = spd;
        S->hold_phase = 1;
        em_wr8(w, head + EM_NODE_MODE, em_rd8(w, head + EM_NODE_MODE) | 1u);
        em_wrf(w, head + EM_NODE_SPEED, last - f > EM_HOLD_AT ? last - f : 0.0f);
    }
    if (S->hold_phase == 1 && f >= last - EM_HOLD_AT) {
        em_wrf(w, head + EM_NODE_SPEED, 0.0f);
        S->hold_phase = 2;
        S->state = SA1_EMOTE_STATE_HOLDING;
        S->holds++;
    }
}

/* a stop with a blend (or anything that must end the emote by the queue's own rules): the held entry gets back its once
 * mode and its own speed (it ends at its next drawn step; a stand behind it blends in from nbFrame - 1), and no entry is
 * held any more */
static void em_hold_release(sa1g_world *w, em_state *S, const em_player *p)
{
    uint32_t j;
    if (S->hold_phase != 0 && S->hold_node != 0 && em_queue_last(w, p->ewp) != 0) {
        uint32_t node = em_rd32(w, p->ewp + EM_EWP_LIST40), guard = 0;
        while (em_arena(node) && node != S->hold_node && guard++ < 256u) {
            node = em_rd32(w, node + EM_NODE_NEXT);
        }
        if (node == S->hold_node) {
            em_wr8(w, node + EM_NODE_MODE, em_rd8(w, node + EM_NODE_MODE) & ~1u);
            em_wrf(w, node + EM_NODE_SPEED, S->hold_speed);
        }
        S->hold_releases++;
    }
    for (j = 0; j < S->n; j++) {
        S->e[j].hold = 0;
    }
    if (S->state == SA1_EMOTE_STATE_HOLDING) {
        S->state = SA1_EMOTE_STATE_PLAYING;
    }
    S->hold_phase = 0;
    S->hold_node = 0;
}

/* a clear, then group g's entries (review R.1: every group starts after a clear, so no entry is queued unblended behind
 * another and the blend source of the last group is freed) */
static void em_queue_group(sa1g_world *w, em_state *S, const em_player *p, uint32_t g)
{
    uint32_t j;
    em_clear(S, p->twp);
    for (j = 0; j < S->n; j++) {
        if (S->e[j].group == g) {
            em_push(S, p->twp, &S->e[j]);
            S->e[j].node = em_queue_last(w, p->ewp);
        }
    }
    S->group = g;
    S->groups_queued++;
    em_hold_step(w, S, p);      /* HOLD: a held entry that ends at its first drawn step */
}

/* ---- rules -------------------------------------------------------------------------------------------------------------- */
static int em_form_changed(const em_state *S, const em_player *p)
{
    return (p->equipment & 0x8000u) != S->super_bit || p->look != S->look;
}

static int em_input(const sa1g_world *w, const em_player *p)
{
    uint32_t press = em_rd32(w, EM_ADDR_PADS + 0x10u);
    uint32_t masks = em_rd32(w, EM_ADDR_MASK_ACTION) | em_rd32(w, EM_ADDR_MASK_JUMP);
    if (((em_analog_fn)em_fn(EM_FN_ANALOG))(sa1_decode(p->twp), NULL, NULL) != 0) {
        return 1;
    }
    return (press & masks) != 0;
}

static void em_end(em_state *S, const em_player *p, int call_clear)
{
    if (call_clear) {
        em_clear(S, p->twp);
    }
    if (S->hammer) {
        em_hammer(S, 0.0f);
    }
    S->state = SA1_EMOTE_STATE_NONE;
    S->stop_wrap = 0;
    S->n = 0;
    S->hold_phase = 0;
    S->hold_node = 0;
}

static void em_drop(em_state *S)
{
    /* the player task was made again: its new event work holds no queue; no call into the game */
    S->state = SA1_EMOTE_STATE_NONE;
    S->hammer = 0;
    S->stop_wrap = 0;
    S->n = 0;
    S->hold_phase = 0;
    S->hold_node = 0;
    S->dropped++;
}

static uint32_t em_head_index(const em_state *S, uint32_t head)
{
    uint32_t j;
    for (j = 0; head != 0 && j < S->n; j++) {
        if (S->e[j].node == head && S->e[j].group == S->group) {
            return j;
        }
    }
    return 0xFFFFFFFFu;
}

/* STOP_BLEND (at a pair start): the groups not queued yet go; the stand after the group being played */
static void em_stop_blend(sa1g_world *w, em_state *S, const em_player *p)
{
    em_plan_ctx c;
    em_entry s;
    uint32_t j, last = 0xFFFFFFFFu, keep = 0;
    em_hold_release(w, S, p);   /* round 0.9.4b: a held pose ends by the queue's own rules, into the stand */
    for (j = 0; j < S->n; j++) {
        if (S->e[j].group <= S->group) {
            S->e[keep++] = S->e[j];
        }
    }
    S->n = keep;
    S->groups = S->group + 1u;
    for (j = 0; j < S->n; j++) {
        if (S->e[j].group == S->group) {
            last = j;
        }
    }
    if (last == 0xFFFFFFFFu || S->e[last].stand || S->n >= EM_MAX_ENTRIES) {
        return;                 /* nothing queued, or the stand is already its end */
    }
    memset(&c, 0, sizeof c);
    c.w = w;
    c.p = p;
    c.list = sa1em_list_of(p->kind, p->form);
    c.desc = sa1_manifest_char(p->kind);
    c.num_actions = c.desc != NULL ? c.desc->num_actions : 0u;
    c.rate = em_gated(p->kind) ? 2.0f : 1.0f;
    if (!em_stand_entry(&c, &s)) {
        return;
    }
    s.item = (int32_t)S->count;
    if (S->link > 0 && s.obj_key == S->e[last].obj_key && em_queue_last(w, p->ewp) == S->e[last].node &&
        S->e[last].node != 0 && em_heap_ok(w, 1u, &S->heap_largest)) {
        s.link = S->link;
        s.group = S->group;
        em_push(S, p->twp, &s);
        s.node = em_queue_last(w, p->ewp);
        S->e[S->n++] = s;
        return;
    }
    s.link = 0;
    s.group = S->group + 1u;
    s.node = 0;
    S->e[S->n++] = s;
    S->groups = s.group + 1u;
    /* a loop never runs empty: the stand group at its wrap (post) */
    S->stop_wrap = S->e[last].loop != 0;
}

/* ---- the hooks ---------------------------------------------------------------------------------------------------------- */
static void em_pre(sa1g_world *w, int32_t sub, void *user)
{
    em_state *S = em_state_of(w);
    em_player p;
    (void)user;
    if (S == NULL || sub != 0) {
        return;
    }
    if (!em_player_of(w, &p)) {
        return;                 /* not ready: everything stays latched */
    }
    if (S->state != SA1_EMOTE_STATE_NONE && p.twp != S->ptwp) {
        em_drop(S);
    }
    if (S->state != SA1_EMOTE_STATE_NONE && em_form_changed(S, &p)) {
        em_end(S, &p, 1);
        S->cancels_state++;
    }
    if (S->req_stop && (S->req_how == SA1_EMOTE_STOP_NOW || p.jvmode == 1u)) {
        /* STOP_BLEND queues the stand entry: like every queueing only with jvmode 1 (review R.8), else it stays latched */
        S->req_stop = 0;
        if (S->state != SA1_EMOTE_STATE_NONE) {
            S->stops++;
            if (S->req_how == SA1_EMOTE_STOP_BLEND) {
                em_stop_blend(w, S, &p);
            } else {
                em_end(S, &p, 1);
            }
        }
    }
    if (S->req_start) {
        em_plan_ctx c;
        const char *why;
        uint32_t n = 0, link = 0;
        em_entry plan[EM_MAX_ENTRIES];  /* round 0.9.4b: a refused start leaves a running emote's entries as they are */
        if (p.jvmode != 1u) {
            S->waits_jvmode++;  /* review R.8: 8C04BF00 needs jvmode 1; the start waits (latched) */
            return;
        }
        S->req_start = 0;
        why = em_refusal(&p, S->req.flags);
        if (why == NULL && !(S->req.flags & SA1_EMOTE_KEEP_INPUT) && em_input(w, &p)) {
            why = "moving";
        }
        if (S->req_early && why != NULL && (!strcmp(why, "busy") || !strcmp(why, "in the air"))) {
            /* asked before the world's first pair: a new character starts falling onto its floor (mode 12) and lands
               in its first pairs - the request waits for his first standing pair (input, hurt, holding: refused) */
            S->req_start = 1;
            S->waits_settle++;
            return;
        }
        memset(&c, 0, sizeof c);
        c.w = w;
        c.p = &p;
        c.list = sa1em_list_of(p.kind, p.form);
        if (why == NULL && (c.list == NULL || !em_plan(&c, &S->req, plan, &n, &link))) {
            why = c.why[0] ? c.why : "busy";
        }
        if (why == NULL && !em_heap_ok(w, em_group_entries(plan, n, 0), &S->heap_largest)) {
            why = "the game's memory is full";
        }
        if (why != NULL) {
            em_note(S, why);
            S->refused++;
            if (S->state == SA1_EMOTE_STATE_NONE) {
                S->n = 0;
            }
            return;
        }
        if (c.lift_missing && !S->warned_lift) {
            S->warned_lift = 1;
            em_note(S, "the lifted Super motions are not built: actions 130-134 play 1.0 low");
        }
        if (S->hammer) {
            em_hammer(S, 0.0f);
        }
        memcpy(S->e, plan, sizeof(em_entry) * n);
        S->n = n;
        S->link = link;
        S->groups = S->e[n - 1u].group + 1u;
        S->flags = S->req.flags;
        S->count = S->req.count;
        S->kind = p.kind;
        S->form = p.form;
        S->ptwp = p.twp;
        S->super_bit = p.equipment & 0x8000u;
        S->look = p.look;
        S->held = p.flag & EM_FLAG_HOLD;
        S->stop_wrap = 0;
        /* the state before the queue: a held entry may be held right away (HOLDING, em_hold_step) */
        S->state = S->e[0].stand ? SA1_EMOTE_STATE_ENDING : SA1_EMOTE_STATE_PLAYING;
        em_queue_group(w, S, &p, 0);
        if (p.kind == SA1_CHAR_AMY && !(S->flags & SA1_EMOTE_NO_PROPS)) {
            uint32_t j, attack = 0;
            for (j = 0; j < S->n; j++) {
                const sa1em_entry *ce = S->e[j].row >= 81 && S->e[j].row <= 95 ?
                                            sa1em_find(c.list, SA1_EMOTE_ROW, (uint32_t)S->e[j].row, NULL) : NULL;
                attack |= ce != NULL && ce->cls == SA1_EMOTE_CLASS_ATTACK;     /* not the dizzy rows 87 / 88 */
            }
            if (attack) {
                em_hammer(S, 1.0f);
            }
        }
        S->starts++;
        return;
    }
    if (S->state != SA1_EMOTE_STATE_NONE && !(S->flags & SA1_EMOTE_KEEP_INPUT) && em_input(w, &p)) {
        em_end(S, &p, 1);
        S->cancels_input++;
    }
}

static void em_post(sa1g_world *w, int32_t sub, void *user)
{
    em_state *S = em_state_of(w);
    em_player p;
    uint32_t head, h;
    float f3c;
    (void)user;
    if (S == NULL || S->state == SA1_EMOTE_STATE_NONE) {
        return;
    }
    if (!em_player_of(w, &p) || p.twp != S->ptwp) {
        em_drop(S);
        return;
    }
    if (sub == 1) {
        int cancel = p.wtimer != 0 || em_form_changed(S, &p) || em_board(&p) || (p.flag & EM_FLAG_HOLD) != S->held;
        if (!(S->flags & SA1_EMOTE_IN_AIR) && p.mode != em_stand_mode(p.kind)) {
            cancel = 1;
        }
        if (cancel) {
            em_end(S, &p, 1);
            S->cancels_state++;
            return;
        }
    }
    head = em_rd32(w, p.ewp + EM_EWP_LIST40);
    f3c = em_rdf(w, p.ewp + EM_EWP_F3C);
    if (!em_arena(head)) {
        if (S->group + 1u < S->groups) {
            if (p.jvmode != 1u) {
                S->waits_jvmode++;      /* review R.8: the next group waits for jvmode 1 */
                return;
            }
            if (!em_heap_ok(w, em_group_entries(S->e, S->n, S->group + 1u), &S->heap_largest)) {
                em_note(S, "the game's memory is full");
                em_end(S, &p, 1);
                S->cancels_state++;
                return;
            }
            em_queue_group(w, S, &p, S->group + 1u);
            S->stop_wrap = 0;
            head = em_rd32(w, p.ewp + EM_EWP_LIST40);
            f3c = em_rdf(w, p.ewp + EM_EWP_F3C);
        } else {
            em_end(S, &p, 1);           /* the natural end: the clear frees the blend source the queue keeps */
            S->ends++;
            return;
        }
    } else if (S->stop_wrap && head == S->last_head && f3c < S->last_f3c && S->group + 1u < S->groups) {
        /* STOP_BLEND on a loop of another object than the stand: at the loop's wrap, the stand as its own group */
        if (p.jvmode == 1u && em_heap_ok(w, 1u, &S->heap_largest)) {
            em_queue_group(w, S, &p, S->group + 1u);
            S->stop_wrap = 0;
            head = em_rd32(w, p.ewp + EM_EWP_LIST40);
            f3c = em_rdf(w, p.ewp + EM_EWP_F3C);
        }
    }
    em_hold_step(w, S, &p);     /* round 0.9.4b: HOLD keeps the last entry on its last frame (never writes f3C) */
    h = em_head_index(S, head);
    if (h != 0xFFFFFFFFu && S->e[h].stand) {
        S->state = SA1_EMOTE_STATE_ENDING;
    }
    S->last_head = head;
    S->last_f3c = f3c;
}

static void em_fill(const sa1g_world *w, sa1w_pstate *o)
{
    const em_state *S = em_state_of(w);
    em_player p;
    uint32_t head, h;
    if (S == NULL || S->state == SA1_EMOTE_STATE_NONE) {
        return;
    }
    if (!em_player_of(w, &p) || p.twp != S->ptwp) {
        return;
    }
    head = em_rd32(w, p.ewp + EM_EWP_LIST40);
    o->emote_state = S->state;
    o->emote_frame = em_arena(head) ? em_rdf(w, p.ewp + EM_EWP_F3C) : 0.0f;
    h = em_head_index(S, head);
    if (h == 0xFFFFFFFFu) {
        uint32_t j;
        for (j = 0; j < S->n && h == 0xFFFFFFFFu; j++) {
            if (S->e[j].group == S->group + 1u) {
                h = j;          /* between groups (waiting for jvmode): the next one */
            }
        }
    }
    if (h != 0xFFFFFFFFu) {
        o->emote_item = S->e[h].item;
        o->emote_row = S->e[h].row;
        o->emote_extra = S->e[h].extra;
    }
}

/* ---- the API side (src/sa1_emote.c; outside ticks) ---------------------------------------------------------------------- */
void sa1em_world_form(const struct sa1g_world *w, int32_t *kind, uint32_t *form)
{
    em_kind_form(w, kind, form);
}

sa1_status sa1em_world_check(const struct sa1g_world *w, const sa1_emote *e, uint32_t entries, char *why, size_t n)
{
    em_player p;
    const char *r;
    em_plan_ctx c;
    em_entry tmp[EM_MAX_ENTRIES];
    uint32_t cnt = 0, link = 0;
    if (why != NULL && n > 0) {
        why[0] = 0;
    }
    if (w == NULL || e == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (!em_player_of(w, &p) || p.jvmode != 1u) {
        return SA1_PARTIAL;     /* before the world's first pair: the start waits for it */
    }
    r = em_refusal(&p, e->flags);
    if (r != NULL) {
        snprintf(why, n, "%s", r);
        return SA1_ERR_UNSUPPORTED;
    }
    memset(&c, 0, sizeof c);
    c.w = w;
    c.p = &p;
    c.list = sa1em_list_of(p.kind, p.form);
    if (c.list == NULL || !em_plan(&c, e, tmp, &cnt, &link)) {
        snprintf(why, n, "%s", c.why[0] ? c.why : "busy");
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (entries == 0) {
        entries = em_group_entries(tmp, cnt, 0);
    }
    if (!em_heap_ok(w, entries, NULL)) {
        snprintf(why, n, "the game's memory is full");
        return SA1_ERR_LIMIT;
    }
    return SA1_OK;
}

static em_state *em_attach(sa1_context *ctx, sa1g_world *w)
{
    em_state *S = em_state_make(w);
    if (S == NULL) {
        return NULL;
    }
    w->emote.pre = em_pre;
    w->emote.post = em_post;
    w->emote.fill = em_fill;
    w->emote.ctx = ctx;
    return S;
}

sa1_status sa1em_world_latch(sa1_context *ctx, struct sa1g_world *w, const sa1_emote *e, int early)
{
    em_state *S;
    if (w == NULL || e == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    S = em_attach(ctx, w);
    if (S == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    S->req = *e;
    S->req_start = 1;
    S->req_early = early != 0;
    S->req_stop = 0;            /* a start replaces a running emote and a stop asked before it */
    return SA1_OK;
}

sa1_status sa1em_world_stop(sa1_context *ctx, struct sa1g_world *w, uint32_t how)
{
    em_state *S;
    if (w == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    S = em_state_of(w);
    if (S == NULL) {
        return SA1_OK;          /* never had an emote: nothing to stop (no hooks are installed for it) */
    }
    (void)ctx;
    if (how == SA1_EMOTE_STOP_NOW) {
        S->req_start = 0;       /* the last call before a pair wins */
    }
    S->req_stop = 1;
    S->req_how = how;
    return SA1_OK;
}

int sa1em_world_take_note(struct sa1g_world *w, char *why, size_t n)
{
    em_state *S = em_state_of(w);
    if (S == NULL || !S->note) {
        return 0;
    }
    snprintf(why, n, "%s", S->why);
    S->note = 0;
    return 1;
}

void sa1em_world_info_get(const struct sa1g_world *w, sa1em_world_info *out)
{
    const em_state *S = em_state_of(w);
    memset(out, 0, sizeof *out);
    if (S == NULL) {
        return;
    }
    out->active = 1;
    out->state = S->state;
    out->starts = S->starts;
    out->ends = S->ends;
    out->cancels_input = S->cancels_input;
    out->cancels_state = S->cancels_state;
    out->stops = S->stops;
    out->refused = S->refused;
    out->dropped = S->dropped;
    out->groups = S->groups_queued;
    out->entries = S->entries_made;
    out->waits_jvmode = S->waits_jvmode;
    out->waits_settle = S->waits_settle;
    out->heap_largest = S->heap_largest;
    out->pending = S->req_start || S->req_stop;
    out->group = S->group;
    out->groups_total = S->state != SA1_EMOTE_STATE_NONE ? S->groups : 0u;
    out->holds = S->holds;
    out->hold_releases = S->hold_releases;
    out->hold_phase = S->hold_phase;
}
