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
 *   stop       NOW: 8C04B48C; BLEND: the groups not queued yet are dropped and a bridge back to the gameplay's pose
 *              starts from where the character is (THE BRIDGES, below).
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
 *   at the end, the bridge back, then 8C04B48C (it frees the blend source the queue keeps: the heap is back where it was)
 *   and Amy's 8C0DE46C(0, 0.0). After the pair (sub step 1): cancel when the gameplay left the standing state (mode 1; kind 8: 71; not with
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
 * E-102 (twice per pair). The loop bit for the last entry when it plays LOOP; a once emote ends with the bridge back to the
 * gameplay's pose unless NO_TRAIL.
 *
 * GROUPS (review R.1). 8C04B57C's else branch keeps a finished entry as the blend source (buf44) WITHOUT freeing the
 * one it held (only the link branch frees it: sadc code_8C04B440.c), so an entry queued without a blend (linkframe 0)
 * behind another leaks 48 bytes of the game heap at its end (measured:
 * SADC/build/research/faces094a/review/out/probe_leak.txt). The lane never queues one: every entry is its own GROUP: when
 * the queue has run empty, 8C04B48C (which frees the blend source) and the next group's entry in the same post hook, so the
 * next pair draws it.
 *
 * THE BRIDGES (round 0.9.9a; sadc build/research/r099a/anim/diagnosis.md, the owner's N5 and N6). The queue's own link
 * (8C04B57C with linkframe > 0, what 0.9.4a used between entries, at a stop and into the trailing stand) is the game's code,
 * made for the cutscenes' neighbouring poses. From an arbitrary pose it breaks the model in three ways (all MEASURED in
 * tests/anim_test.c and READ in sadc): the Euler angles blend as plain integers (8C0417C8): a key of 64947 towards 389 turns
 * the node a whole turn (Tails, leaving m_kimepose0: the whole body turns over, 14 units off its way); the scale blend reads
 * the position buffers (8C0417C8, the original's bug, kept in the decompilation): a motion with scale keys collapses the
 * node (Big, leaving b_throw0: the torso gone, seams 29 units open); the welds are evaluated at the source pose for the
 * first half and at the target for the second, not at the drawn one (8C04B57C: 8C04BF00 with link.motion[0 or 1]).
 * So the lane never uses it. A transition is a BRIDGE: an entry of its own whose motion the lane makes in its block in
 * segment 0x18, built like the gameplay's own link (PSetMotion's actwkptr, 8C079C76: nbFrame 2, the pose it comes from, the pose
 * it goes to). The queue's plain branch draws it: 8C04BF00 and the draw at the same frame, so the welds fit the drawn pose
 * exactly. Where the gameplay's link interpolates the three angles of each node on their own (a limb far from its target
 * circles on the way: 21 units of detour leaving s_standup0, MEASURED), the bridge turns each node the shortest way: before every
 * draw the lane writes both keys with the pose at the bridge's frame (each node's rotation as a quaternion, normalised lerp,
 * back to the angles of the object's rotation order; position and scale lerped). No libm: the same bits on x86 and x64. It lasts
 * `link` pairs for every kind (speed = rate / (2 link)). It needs both objects to have the same tree (node count and shape;
 * the gameplay's link asks for the same object: the bodies and flight models of one character qualify, the curled body does
 * not): otherwise the transition is the unblended pop it always was. Where it comes from: the queue head at its frame, or the
 * gameplay's pose (its display's rule: the action's own, or actwkptr while it links). Where it goes: an entry's first frame, or,
 * going back, the gameplay's pose LIVE: key 1 is written again after every pair, so it fades into whatever the gameplay is
 * doing. A cancel (input, a hit, the gameplay leaving the stand) is the same fade with the emote already over: state NONE at
 * once, the gameplay of that pair runs exactly as in a world that never emoted, the queue only draws (FADING; a new emote or a
 * gameplay pose of another tree ends it). Nothing of this runs in a world that never gets an emote call.
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
 * emote clears the queue). STOP_BLEND starts the bridge back from the held frame; STOP_NOW clears. A looping last entry is not held (it loops until a cancel or a stop). A held entry
 * never ends, so it never becomes a blend source behind an unblended entry: no leak (review R.1 of round 0.9.4a; E20).
 *
 * A FOLLOWER (round 0.9.9, SA1_API_FOLLOWER 2; sadc docs/research/follow099.md 2.4, 5.2). In a follower world "the character" is slot 1 (w->slot; em_player_of, the hammer) and there is no pad: its input is the AI's, which
 * fw_ai_exec drops while sa1em_world_wants_hold says an emote plays or waits (per[1].on, press, the analog stroke; the AI's state machine and its respawn test run on, so a drop-in ends the emote). So em_input is 0
 * there, and the call cannot demand the stand: the AI's stroke of 0.005 keeps four kinds out of mode 1 at rest (on in 0 of 60 pairs with the hold off, measured). em_refusal_of takes a call on the ground, not hurt, not on a
 * board, not holding, under speed 1 and within 30 units of the proxy (its spot; else "on the way"), and the pair start waits up to 30 pairs for the stand. SA1_EMOTE_IN_AIR is refused in src/sa1_emote.c. No reference world
 * makes the call: w->slot is 0 in every world but a follower's.
 *
 * THE CUTSCENE ANIMATIONS (round 100-EV, SA1_API_EMOTE_EVENT; sadc docs/research/emotes_ev.md 3.4-3.7). An EVENT item is
 * played exactly as an extra (8C04B528 with the kind's body, Tails' flight body for four of his), but its motion is not in
 * the world's memory: src/sa1_emote_ev.c reads it from the cutscene's file into a host block, and sa1em_world_latch_ev
 * (between ticks) copies the request's blocks into one of two slabs of SA1EM_SLAB_SIZE in segment 0x18 (made at the first
 * EVENT emote of the world), the one that the running, holding or fading emote does NOT use, relocated to guest addresses;
 * two requests before one pair go into that same slab (the second replaces the first, which never ran), so the running
 * emote's motion stays valid while the next one's bridge samples it at the pair start. Speed: the cutscene's pace (frames
 * per 1/30 s) x rate / 2 x the item's multiplier. The lift (host deviation D-EV1, render only): each EVENT entry carries the
 * catalogue's lift (round r1-fix-ev: the game's drawn body measured in a LIB world, tests/emote_test.c --measure-lift);
 * sa1em_world_lift says what is drawn now (an entry its own, a bridge from one end's lift to the other's by its
 * frame); src/sa1_character.c adds it to the body's draws when SA1_COPT_EMOTE_LIFT is on. A world without an EVENT emote
 * never makes the slabs; the reference worlds never call any of it.
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
#include "sa1_emote_ev.h"           /* round 100-EV: the cutscene animations (sa1ev_entry_of, sa1ev_body) */

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
#define EM_TWP_POS          0x20u           /* taskwk.pos */
#define EM_TWP_EWP          0x3Cu           /* taskwk.ewp */
#define EM_EWP_F3C          0x3Cu           /* eventwk.f3C: the queue head's frame */
#define EM_EWP_LIST40       0x40u           /* eventwk.list40: the queue head */
#define EM_EWP_BUF44        0x44u           /* eventwk.buf44: the blend source (the entry before) */
#define EM_NODE_MODE        0x00u           /* EV_ACTION_LIST.mode (Uint8): bit 0 loop */
#define EM_NODE_LINK        0x01u           /* EV_ACTION_LIST.linkframe (Uint8) */
#define EM_NODE_OBJECT      0x04u           /* EV_ACTION_LIST.action.object */
#define EM_NODE_MOTION      0x08u           /* EV_ACTION_LIST.action.motion */
#define EM_NODE_NEXT        0x10u           /* EV_ACTION_LIST.next */
#define EM_NODE_SPEED       0x14u           /* EV_ACTION_LIST.speed (Float): frames per drawn step */
#define EM_MOT_NBFRAME      0x04u           /* NJS_MOTION.nbFrame (Uint32) */
#define EM_HOLD_EARLY       0.01f           /* HOLD: engage this much before frame + speed reaches nbFrame - 1 */
#define EM_HOLD_AT          0.001f          /* HOLD: the frame counts as the last one this close to it */
#define EM_PWP_EQUIPMENT    0x004u          /* playerwk.equipment (0x8000 = Super Sonic) */
#define EM_PWP_SPD          0x038u          /* playerwk.spd (player space) */
#define EM_PWP_MTNMODE      0x120u          /* playerwk.mj.mtnmode (Sint16): 2 = linking, the display draws actwkptr */
#define EM_PWP_JVMODE       0x122u          /* playerwk.mj.jvmode (Sint16): 1 = the welds are set up */
#define EM_PWP_REQACTION    0x124u          /* playerwk.mj.reqaction (Uint16) */
#define EM_PWP_NFRAME       0x130u          /* playerwk.mj.nframe (Float) */
#define EM_PWP_PLACT        0x140u          /* playerwk.mj.plactptr: PL_ACTION[] (16 bytes) */
#define EM_PWP_ACTWK        0x148u          /* playerwk.mj.actwkptr: NJS_ACTION, the gameplay's link */
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

/* a node's pose in the bridge's block: the two ends of the bridge */
typedef struct em_nodepose {
    float pos[3], q[4], scl[3];
} em_nodepose;

/* the bridge's block in segment 0x18 (THE BRIDGES, the file comment): NJS_ACTION, NJS_MOTION, NJS_MDATA3[], then per node its three channels of two keys */
#define EM_BRIDGE_NODES     128u            /* the node matrix table of 8C0583A0 holds 128 */
#define EM_OBJ_POS          0x08u           /* NJS_OBJECT.pos */
#define EM_OBJ_ANG          0x14u           /* NJS_OBJECT.ang */
#define EM_OBJ_SCL          0x20u           /* NJS_OBJECT.scl */
#define EM_OBJ_CHILD        0x2Cu
#define EM_OBJ_SIBLING      0x30u
#define EM_MOT_TYPE         0x08u           /* NJS_MOTION.type (Uint16): bit 0 pos, 1 ang, 2 scl */
#define EM_MOT_INP          0x0Au           /* NJS_MOTION.inp_fn (Uint16): low nibble = the channels of its NJS_MDATAn */
#define EM_MKEY             16u             /* NJS_MKEY_F / NJS_MKEY_A: keyframe, key[3] */
#define EM_BR_ACTION        0x00u
#define EM_BR_MOTION        0x08u
#define EM_BR_MDATA         0x20u
#define EM_BR_KEYS          (EM_BR_MDATA + EM_BRIDGE_NODES * 24u)
#define EM_BR_POSES         (EM_BR_KEYS + EM_BRIDGE_NODES * 3u * 2u * EM_MKEY)
#define EM_BR_SIZE          (EM_BR_POSES + EM_BRIDGE_NODES * 2u * (uint32_t)sizeof(em_nodepose))
#define EM_ZXY_ANG          0x20u           /* NJS_OBJECT.evalflags: njRotateZXY; the root's decides for the whole draw (func_8C040C02) */

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
    uint32_t hold;              /* 1: SA1_EMOTE_HOLD's entry (the emote's last, a once entry): held on its last frame */
    uint32_t node;              /* guest address of its queue entry once queued (0: not queued) */
    float lift;                 /* round 100-EV: an EVENT entry's lift (SA1 units, the table's); 0 for the others */
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
    float rate;                 /* 2 (once per pair) or 1 (twice) */
    uint32_t back;              /* a once emote: when its last entry ends, the bridge back */
    uint32_t in_bridge;         /* the queue holds the bridge into group `group`, not its entry */
    uint32_t in_back;           /* the queue holds the bridge back to the gameplay's pose: ENDING */
    uint32_t fading;            /* the emote is over (state NONE), the queue only draws the fade into the gameplay's pose */
    uint32_t bridge;            /* the bridge's block (guest), 0: none */
    uint32_t br_obj;            /* the drawn object of the bridge in the queue */
    uint32_t br_n, br_zxy;      /* its nodes, its rotation order */
    float br_speed;             /* its speed: frames of 0..1 per draw */
    float br_last;              /* its frame at the last pair; a pair that did not move it: the character is not drawn (br_stall) */
    uint32_t br_stall;
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
    /* round 0.9.9 (an emote on a follower id, gsa1_099_plan.md 6.4 item 4): the world's character slot; pair starts a follower's start has waited for its stand */
    uint32_t slot, settle_wait;
    uint32_t bridges;           /* round 0.9.9a: bridges queued */
    /* round 100-EV (THE CUTSCENE ANIMATIONS, the file comment): two slabs of SA1EM_SLAB_SIZE in segment 0x18, made at the
       first EVENT emote; the copies of the latched request go into the one the running, holding or fading emote does not use */
    uint32_t slab[2];           /* guest, 0: not made */
    uint32_t run_slab;          /* the running emote's slab, 2: it holds no copy */
    uint32_t req_slab, req_has_ev;
    uint32_t req_ev[SA1_EMOTE_MAX_ITEMS];   /* the latched items' motions (guest, in req_slab); 0: not an EVENT item */
    float br_lift_from, br_lift_to; /* the lift at the two ends of the bridge in the queue */
    uint32_t ev_copies, ev_bytes;   /* motions copied into a slab, their bytes */
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

/* ---- the world's character: player 0, slot 1 in a follower world (round 0.9.9) ---------------------------------------------- */
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

/* 1: the world's character has its taskwk, playerwk and event work */
static int em_player_of(const sa1g_world *w, em_player *p)
{
    memset(p, 0, sizeof *p);
    em_kind_form(w, &p->kind, &p->form);
    p->twp = em_rd32(w, SA1G_ADDR_PLAYER_TWP + 4u * (uint32_t)w->slot);
    p->pwp = em_rd32(w, SA1G_ADDR_PLAYER_PWP + 4u * (uint32_t)w->slot);
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

/* round 0.9.9: a call on a follower id (follow099.md 5.2). At rest the AI's stroke of 0.005 keeps four kinds out of the stand's mode, so the call cannot demand it: on the ground, not hurt, not on a
   board, not holding and with a speed under 1 it is taken when the follower has reached its place, within 30 units of the proxy (its spot); from the latch on the AI's output is dropped and the start
   waits for the stand (em_pre). Everything else keeps the player's plain words */
static const char *em_refusal_of(const sa1g_world *w, const em_player *p, uint32_t flags)
{
    const char *r = em_refusal(p, flags);
    if (w->slot != 0 && (r == NULL || ((p->flag & EM_FLAG_GROUND) && p->wtimer == 0 && !em_board(p) && !(p->flag & EM_FLAG_HOLD) && p->speed < 1.0f))) {
        uint32_t proxy = em_rd32(w, SA1G_ADDR_PLAYER_TWP);
        float dx = em_rdf(w, p->twp + EM_TWP_POS) - em_rdf(w, proxy + EM_TWP_POS);
        float dz = em_rdf(w, p->twp + EM_TWP_POS + 8u) - em_rdf(w, proxy + EM_TWP_POS + 8u);
        r = dx * dx + dz * dz < 30.0f * 30.0f ? NULL : "on the way";
    }
    return r;
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
    int back;                   /* the bridge back after its last entry */
    const uint32_t *ev_mot;     /* round 100-EV: the latched items' motions in the slab (NULL: a check, no copy yet) */
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
        const sa1em_entry *ce;
        em_entry x;
        if (it->source == SA1_EMOTE_EVENT) {
            /* round 100-EV: a cutscene animation, its motion copied into the slab by the latch (sa1em_world_latch_ev):
               EV_SetMotion's body with the kind's body (Tails' flight body for four), as an extra (the file comment) */
            const sa1ev_entry *ev = c->p->form == SA1_EMOTE_FORM_NORMAL ? sa1ev_entry_of(c->p->kind, it->index) : NULL;
            uint32_t body = ev != NULL ? sa1ev_body(c->p->kind, ev->object) : 0u, mot = c->ev_mot != NULL ? c->ev_mot[i] : 0u;
            if (ev == NULL || !em_valid(c->w, body) ||
                (c->ev_mot != NULL && (!em_valid(c->w, mot) || em_rd32(c->w, mot + EM_MOT_NBFRAME) != ev->frames))) {
                em_why(c, "cutscene animation", it->index, ev != NULL ? ev->name : NULL);
                return 0;
            }
            if (n >= EM_MAX_ENTRIES - 1u) {
                snprintf(c->why, sizeof c->why, "the emote is too long");
                return 0;
            }
            memset(&x, 0, sizeof x);
            x.how = EM_HOW_MOTION;
            x.object = body;
            x.motion = mot;
            x.obj_key = em_key(body);
            /* the cutscene's pace is frames per 1/30 s, one pair (the event library runs one step a frame at 30 Hz:
               emotes_ev.md 2.5): as is for the kinds whose queue moves once a pair, half for Amy and E-102 (twice) */
            x.speed = ev->pace * (c->rate * 0.5f) * mult;
            x.row = -1;
            x.extra = -1;
            x.item = (int32_t)i;
            x.lift = ev->lift;
            last_loops = last_item && (it->play == SA1_EMOTE_PLAY_LOOP || (it->play == SA1_EMOTE_PLAY_DEFAULT && ev->loop));
            x.loop = (uint32_t)last_loops;
            out[n++] = x;
            continue;
        }
        ce = sa1em_find(c->list, it->source, it->index, &pos);
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
            last_loops = last_item && (it->play == SA1_EMOTE_PLAY_LOOP || (it->play == SA1_EMOTE_PLAY_DEFAULT && (ce->flags & SA1EM_F_LOOP)));
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
        out[n - 1u].hold = 1;   /* round 0.9.4b: held on its last frame instead of going back */
    }
    /* groups (review R.1): every entry its own; the transitions are bridges (the file comment) */
    for (j = 0; j < n; j++) {
        out[j].link = 0;
        out[j].group = j;
        out[j].node = 0;
    }
    *n_out = n;
    *link_out = link;
    c->back = !last_loops && !(e->flags & (SA1_EMOTE_NO_TRAIL | SA1_EMOTE_HOLD));
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
    S->hold_phase = 0;          /* the clear freed the held entry */
    S->hold_node = 0;
    S->in_bridge = 0;
    S->in_back = 0;
    S->fading = 0;
}

static void em_hammer(em_state *S, float f)
{
    ((em_hammer_fn)em_fn(EM_FN_HAMMER))((uint8_t)S->slot, f);
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

/* ---- the bridges (the file comment) ------------------------------------------------------------------------------------------ */
typedef struct em_pose {
    uint32_t obj, mot;
    float frame;
} em_pose;

/* what the display draws when the queue is empty: the action's own motion at nframe, or the gameplay's link while it links (8C0AE17A and the other displays) */
static int em_gameplay_pose(const sa1g_world *w, const em_player *p, em_pose *o)
{
    int32_t mtn = (int16_t)em_rd16(w, p->pwp + EM_PWP_MTNMODE);
    uint32_t act = mtn == 2 ? em_rd32(w, p->pwp + EM_PWP_ACTWK) : em_rd32(w, p->plact + 16u * em_rd16(w, p->pwp + EM_PWP_REQACTION));
    if (!em_valid(w, act)) {
        return 0;
    }
    o->obj = em_rd32(w, act);
    o->mot = em_rd32(w, act + 4u);
    o->frame = em_rdf(w, p->pwp + EM_PWP_NFRAME);
    return em_valid(w, o->obj) && em_valid(w, o->mot);
}

/* what is on the screen: the queue head at its frame, or the gameplay's pose */
static int em_drawn_now(const sa1g_world *w, const em_player *p, em_pose *o)
{
    uint32_t head = em_rd32(w, p->ewp + EM_EWP_LIST40);
    if (!em_arena(head)) {
        return em_gameplay_pose(w, p, o);
    }
    o->obj = em_rd32(w, head + EM_NODE_OBJECT);
    o->mot = em_rd32(w, head + EM_NODE_MOTION);
    o->frame = em_rdf(w, p->ewp + EM_EWP_F3C);
    return em_valid(w, o->obj) && em_valid(w, o->mot);
}

static int em_entry_pose(const sa1g_world *w, const em_entry *x, em_pose *o)
{
    if (x->how == EM_HOW_MOTION) {
        o->obj = x->object;
        o->mot = x->motion;
    } else {
        o->obj = em_rd32(w, x->action);
        o->mot = em_rd32(w, x->action + 4u);
    }
    o->frame = 0.0f;
    return em_valid(w, o->obj) && em_valid(w, o->mot);
}

/* an object's nodes in the order its motions key them (func_8C0567B0), with the tree's shape: 0 when it does not fit */
typedef struct em_tree {
    uint32_t n;
    uint32_t node[EM_BRIDGE_NODES];
    uint8_t shape[EM_BRIDGE_NODES];
} em_tree;

static int em_tree_walk(const sa1g_world *w, uint32_t obj, em_tree *t, uint32_t depth)
{
    uint32_t guard = 0;
    while (obj != 0) {
        uint32_t i = t->n, child, sib;
        if (!em_valid(w, obj) || i >= EM_BRIDGE_NODES || depth > 64u || guard++ > EM_BRIDGE_NODES) {
            return 0;
        }
        t->n = i + 1u;
        t->node[i] = obj;
        child = em_rd32(w, obj + EM_OBJ_CHILD);
        sib = em_rd32(w, obj + EM_OBJ_SIBLING);
        t->shape[i] = (uint8_t)((child != 0 ? 1u : 0u) | (sib != 0 ? 2u : 0u));
        if (child != 0 && !em_tree_walk(w, child, t, depth + 1u)) {
            return 0;
        }
        obj = sib;
    }
    return 1;
}

static int em_tree_of(const sa1g_world *w, uint32_t obj, em_tree *t)
{
    t->n = 0;
    return em_tree_walk(w, obj, t, 0u) && t->n > 0;
}

/* a motion's channels: NJS_MDATAn is n pointers, then n counts */
typedef struct em_mot {
    uint32_t md, nf, n;
    int ch_pos, ch_ang, ch_scl;
} em_mot;

static int em_mot_of(const sa1g_world *w, uint32_t mot, em_mot *m)
{
    uint32_t type = em_rd16(w, mot + EM_MOT_TYPE), c = 0;
    m->md = em_rd32(w, mot);
    m->nf = em_rd32(w, mot + EM_MOT_NBFRAME);
    m->n = em_rd16(w, mot + EM_MOT_INP) & 0xFu;
    m->ch_pos = (type & 1u) ? (int)c++ : -1;
    m->ch_ang = (type & 2u) ? (int)c++ : -1;
    m->ch_scl = (type & 4u) ? (int)c++ : -1;
    return em_valid(w, m->md) && m->nf > 0u && m->n > 0u && c <= m->n;
}

/* the keys of a node's channel, 0: none */
static uint32_t em_keys_of(const sa1g_world *w, const em_mot *m, uint32_t node, int ch, uint32_t *count)
{
    uint32_t e = m->md + 8u * m->n * node, p, nb;
    if (ch < 0) {
        return 0;
    }
    p = em_rd32(w, e + 4u * (uint32_t)ch);
    nb = em_rd32(w, e + 4u * m->n + 4u * (uint32_t)ch);
    if (nb == 0u || nb > 0x10000u || !em_valid(w, p)) {
        return 0;
    }
    *count = nb;
    return p;
}

/* the key before and after a frame as the gameplay's link builder finds them (8C079C76: past the last key it wraps to key 0 over nbFrame); t: how far between them */
static void em_key_span(const sa1g_world *w, uint32_t keys, uint32_t nb, uint32_t nf, float f, uint32_t *k, uint32_t *j, float *t)
{
    uint32_t a = 0, b = 0, diff;
    while (f > (float)em_rd32(w, keys + EM_MKEY * b)) {
        if (b >= nb - 1u) {
            b = 0;
            a = nb - 1u;
            break;
        }
        a = b++;
    }
    *k = a;
    *j = b;
    *t = 0.0f;
    if (a != b) {
        uint32_t fa = em_rd32(w, keys + EM_MKEY * a);
        diff = em_rd32(w, keys + EM_MKEY * b) - fa;
        if (b < a) {
            diff += nf;
        }
        if (diff != 0u) {
            *t = (f - (float)fa) / (float)diff;
        }
    }
}

/* the sample of a node's float channel (a position, a scale); no keys: the node's own */
static void em_sample3(const sa1g_world *w, const em_mot *m, uint32_t node, int ch, float f, uint32_t own, float o[3])
{
    uint32_t nb = 0, keys = em_keys_of(w, m, node, ch, &nb), c;
    if (keys == 0) {
        for (c = 0; c < 3u; c++) {
            o[c] = em_rdf(w, own + 4u * c);
        }
        return;
    }
    {
        uint32_t k, j;
        float t;
        em_key_span(w, keys, nb, m->nf, f, &k, &j, &t);
        for (c = 0; c < 3u; c++) {
            float a = em_rdf(w, keys + EM_MKEY * k + 4u + 4u * c), b = em_rdf(w, keys + EM_MKEY * j + 4u + 4u * c);
            o[c] = a + (b - a) * t;
        }
    }
}

/* the sample of a node's angles: between two keys the short way (SubAngle, as 8C079C76), reduced to signed 16 bits */
static void em_sample_ang(const sa1g_world *w, const em_mot *m, uint32_t node, int ch, float f, uint32_t own, int32_t o[3])
{
    uint32_t nb = 0, keys = em_keys_of(w, m, node, ch, &nb), c;
    if (keys == 0) {
        for (c = 0; c < 3u; c++) {
            o[c] = (int16_t)(uint16_t)em_rd32(w, own + 4u * c);
        }
        return;
    }
    {
        uint32_t k, j;
        float t;
        em_key_span(w, keys, nb, m->nf, f, &k, &j, &t);
        for (c = 0; c < 3u; c++) {
            uint32_t a = em_rd32(w, keys + EM_MKEY * k + 4u + 4u * c), b = em_rd32(w, keys + EM_MKEY * j + 4u + 4u * c);
            int32_t d = (int16_t)(uint16_t)(b - a);
            o[c] = (int16_t)(uint16_t)(a + (uint32_t)(int32_t)((float)d * t));
        }
    }
}

/* no libm: the same bits on x86 and x64 (the arithmetic is SSE in both builds) */
#define EM_PI 3.14159265358979323846

static void em_sincos(double a, double *s, double *c)
{
    double k = a / (0.5 * EM_PI), x, x2, sn, cs;
    int64_t q = (int64_t)(k + (k < 0.0 ? -0.5 : 0.5));
    x = a - (double)q * (0.5 * EM_PI);
    x2 = x * x;
    sn = x * (1.0 + x2 * (-1.0 / 6.0 + x2 * (1.0 / 120.0 + x2 * (-1.0 / 5040.0 + x2 * (1.0 / 362880.0 + x2 * (-1.0 / 39916800.0))))));
    cs = 1.0 + x2 * (-0.5 + x2 * (1.0 / 24.0 + x2 * (-1.0 / 720.0 + x2 * (1.0 / 40320.0 + x2 * (-1.0 / 3628800.0 + x2 * (1.0 / 479001600.0))))));
    switch ((int)(q & 3)) {
    case 0: *s = sn; *c = cs; break;
    case 1: *s = cs; *c = -sn; break;
    case 2: *s = -sn; *c = -cs; break;
    default: *s = -cs; *c = sn; break;
    }
}

static double em_sqrt(double v)
{
    double x = v > 1.0 ? v : 1.0;
    int i;
    if (!(v > 0.0)) {
        return 0.0;
    }
    for (i = 0; i < 64; i++) {
        double n = 0.5 * (x + v / x);
        if (n >= x) {
            break;
        }
        x = n;
    }
    return x;
}

/* the angle of (x, y), a start from a polynomial and Newton on sin / cos */
static double em_atan2(double y, double x)
{
    double ax = x < 0.0 ? -x : x, ay = y < 0.0 ? -y : y, z, a, r;
    int i;
    if (ax == 0.0 && ay == 0.0) {
        return 0.0;
    }
    z = ax > ay ? ay / ax : ax / ay;
    a = z * (0.995354 + z * z * (-0.288679 + z * z * 0.079331));
    if (ay > ax) {
        a = 0.5 * EM_PI - a;
    }
    if (x < 0.0) {
        a = EM_PI - a;
    }
    if (y < 0.0) {
        a = -a;
    }
    r = em_sqrt(x * x + y * y);
    for (i = 0; i < 3; i++) {
        double sn, cs;
        em_sincos(a, &sn, &cs);
        a += (y * cs - x * sn) / r;
    }
    return a;
}

static void em_qmul(const double a[4], const double b[4], double o[4])
{
    double t[4];
    t[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    t[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
    t[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
    t[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
    memcpy(o, t, sizeof t);
}

/* a node's angles (BAMS) as a quaternion: njRotateXYZ is Rz Ry Rx in column vectors, njRotateZXY Ry Rx Rz (sa1_pose.c mtx_local) */
static void em_quat_of(const int32_t ang[3], uint32_t zxy, float q[4])
{
    double qx[4] = { 1.0, 0.0, 0.0, 0.0 }, qy[4] = { 1.0, 0.0, 0.0, 0.0 }, qz[4] = { 1.0, 0.0, 0.0, 0.0 }, o[4], sn, cs;
    uint32_t k;
    em_sincos((double)ang[0] * (EM_PI / 65536.0), &sn, &cs);
    qx[0] = cs;
    qx[1] = sn;
    em_sincos((double)ang[1] * (EM_PI / 65536.0), &sn, &cs);
    qy[0] = cs;
    qy[2] = sn;
    em_sincos((double)ang[2] * (EM_PI / 65536.0), &sn, &cs);
    qz[0] = cs;
    qz[3] = sn;
    if (zxy) {
        em_qmul(qy, qx, o);
        em_qmul(o, qz, o);
    } else {
        em_qmul(qz, qy, o);
        em_qmul(o, qx, o);
    }
    for (k = 0; k < 4u; k++) {
        q[k] = (float)o[k];
    }
}

static int32_t em_bams(double rad)
{
    double b = rad * (65536.0 / (2.0 * EM_PI));
    return (int32_t)(b + (b < 0.0 ? -0.5 : 0.5));
}

/* the angles back (the two orders' extractions; at the gimbal the last angle 0) */
static void em_angles_of(const double q[4], uint32_t zxy, int32_t ang[3])
{
    double w = q[0], x = q[1], y = q[2], z = q[3];
    double r00 = 1.0 - 2.0 * (y * y + z * z), r01 = 2.0 * (x * y - w * z), r02 = 2.0 * (x * z + w * y);
    double r10 = 2.0 * (x * y + w * z), r11 = 1.0 - 2.0 * (x * x + z * z), r12 = 2.0 * (y * z - w * x);
    double r20 = 2.0 * (x * z - w * y), r21 = 2.0 * (y * z + w * x), r22 = 1.0 - 2.0 * (x * x + y * y), v, c;
    if (zxy) {
        /* R = Ry Rx Rz: r12 = -sin x, r10 = cos x sin z, r11 = cos x cos z, r02 = sin y cos x, r22 = cos y cos x */
        v = -r12;
        v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
        c = em_sqrt(1.0 - v * v);
        ang[0] = em_bams(em_atan2(v, c));
        if (c > 1e-6) {
            ang[1] = em_bams(em_atan2(r02, r22));
            ang[2] = em_bams(em_atan2(r10, r11));
        } else {
            ang[1] = em_bams(em_atan2(-r20, r00));
            ang[2] = 0;
        }
    } else {
        /* R = Rz Ry Rx: r20 = -sin y, r21 = cos y sin x, r22 = cos y cos x, r10 = sin z cos y, r00 = cos z cos y */
        v = -r20;
        v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
        c = em_sqrt(1.0 - v * v);
        ang[1] = em_bams(em_atan2(v, c));
        if (c > 1e-6) {
            ang[0] = em_bams(em_atan2(r21, r22));
            ang[2] = em_bams(em_atan2(r10, r00));
        } else {
            ang[0] = em_bams(em_atan2(-r12, r11));
            ang[2] = 0;
        }
    }
    (void)r01;
}

/* one end of the bridge (0: where it comes from, 1: where it goes) from a pose; 0: the pose's motion cannot be read */
static int em_bridge_end(sa1g_world *w, const em_state *S, const em_tree *tree, const em_pose *pose, uint32_t side)
{
    em_mot m;
    uint32_t i, zxy = em_rd32(w, pose->obj) & EM_ZXY_ANG;
    em_nodepose *P;
    if (!em_mot_of(w, pose->mot, &m)) {
        return 0;
    }
    P = (em_nodepose *)(void *)(w->ext + (S->bridge & 0xFFFFFFu) + EM_BR_POSES);
    for (i = 0; i < tree->n; i++) {
        em_nodepose *o = &P[2u * i + side];
        int32_t ang[3];
        em_sample3(w, &m, i, m.ch_pos, pose->frame, tree->node[i] + EM_OBJ_POS, o->pos);
        em_sample_ang(w, &m, i, m.ch_ang, pose->frame, tree->node[i] + EM_OBJ_ANG, ang);
        em_sample3(w, &m, i, m.ch_scl, pose->frame, tree->node[i] + EM_OBJ_SCL, o->scl);
        em_quat_of(ang, zxy, o->q);
    }
    return 1;
}

/* both keys of every node: the pose at the bridge's frame f (0: where it comes from; the last draw, 1 - speed: where it goes, exactly) */
static void em_bridge_write(sa1g_world *w, const em_state *S, float f3c)
{
    uint8_t *B = w->ext + (S->bridge & 0xFFFFFFu);
    const em_nodepose *P = (const em_nodepose *)(const void *)(B + EM_BR_POSES);
    uint32_t i, c, f;
    float t = S->br_speed < 1.0f ? f3c / (1.0f - S->br_speed) : 1.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    for (i = 0; i < S->br_n; i++) {
        const em_nodepose *a = &P[2u * i], *b = &P[2u * i + 1u];
        uint8_t *K = B + EM_BR_KEYS + 3u * 2u * EM_MKEY * i;
        float pos[3], scl[3];
        double q[4], d = 0.0, l = 0.0;
        int32_t ang[3];
        for (c = 0; c < 3u; c++) {
            pos[c] = a->pos[c] + (b->pos[c] - a->pos[c]) * t;
            scl[c] = a->scl[c] + (b->scl[c] - a->scl[c]) * t;
        }
        for (c = 0; c < 4u; c++) {
            d += (double)a->q[c] * (double)b->q[c];
        }
        for (c = 0; c < 4u; c++) {
            q[c] = (double)a->q[c] * (1.0 - (double)t) + (double)b->q[c] * (d < 0.0 ? -(double)t : (double)t);
            l += q[c] * q[c];
        }
        if (l > 1e-12) {
            l = em_sqrt(l);
            for (c = 0; c < 4u; c++) {
                q[c] /= l;
            }
        } else {
            for (c = 0; c < 4u; c++) {
                q[c] = (double)b->q[c];     /* exactly half a turn apart at its middle: on to the end */
            }
        }
        em_angles_of(q, S->br_zxy, ang);
        for (f = 0; f < 2u; f++) {
            memcpy(K + EM_MKEY * f, &f, 4);
            memcpy(K + EM_MKEY * f + 4u, pos, 12);
            memcpy(K + 2u * EM_MKEY + EM_MKEY * f, &f, 4);
            memcpy(K + 2u * EM_MKEY + EM_MKEY * f + 4u, ang, 12);
            memcpy(K + 4u * EM_MKEY + EM_MKEY * f, &f, 4);
            memcpy(K + 4u * EM_MKEY + EM_MKEY * f + 4u, scl, 12);
        }
    }
}

/* makes the bridge from one pose to another and fills its queue entry; 0: no bridge (no block, trees that differ, the heap): the transition is the plain pop */
static int em_bridge_make(sa1g_world *w, em_state *S, const em_pose *from, const em_pose *to, uint32_t texlist, em_entry *x)
{
    em_tree *a, *b;
    uint32_t i, t7 = (3u << 16) | 7u;
    uint8_t *B;
    int ok;
    /* (the two trees on the world's stack would do; a struct on the host stack costs less than a page) */
    em_tree ta, tb;
    a = &ta;
    b = &tb;
    if (S->bridge == 0 || S->link == 0u || !em_tree_of(w, from->obj, a) || !em_tree_of(w, to->obj, b) || a->n != b->n || memcmp(a->shape, b->shape, a->n) != 0) {
        return 0;
    }
    if (!em_heap_ok(w, 1u, &S->heap_largest)) {
        return 0;
    }
    B = w->ext + (S->bridge & 0xFFFFFFu);
    ok = em_bridge_end(w, S, a, from, 0u);
    if (ok) {
        ok = em_bridge_end(w, S, b, to, 1u);
    }
    if (!ok) {
        return 0;
    }
    {
        uint32_t mot = S->bridge + EM_BR_MOTION, md = S->bridge + EM_BR_MDATA, two = 2u;
        memcpy(B + EM_BR_ACTION, &to->obj, 4);
        memcpy(B + EM_BR_ACTION + 4u, &mot, 4);
        memcpy(B + EM_BR_MOTION, &md, 4);
        memcpy(B + EM_BR_MOTION + 4u, &two, 4);
        memcpy(B + EM_BR_MOTION + 8u, &t7, 4);
        for (i = 0; i < b->n; i++) {
            uint8_t *E = B + EM_BR_MDATA + 24u * i;
            uint32_t c;
            for (c = 0; c < 3u; c++) {
                uint32_t keys = S->bridge + EM_BR_KEYS + 3u * 2u * EM_MKEY * i + 2u * EM_MKEY * c;
                memcpy(E + 4u * c, &keys, 4);
                memcpy(E + 12u + 4u * c, &two, 4);
            }
        }
    }
    memset(x, 0, sizeof *x);
    x->how = EM_HOW_ACTION;
    x->action = S->bridge + EM_BR_ACTION;
    x->texlist = texlist;
    x->obj_key = em_key(to->obj);
    x->speed = S->rate / (2.0f * (float)S->link);
    x->row = -1;
    x->extra = -1;
    S->br_obj = to->obj;
    S->br_n = b->n;
    S->br_zxy = em_rd32(w, to->obj) & EM_ZXY_ANG;
    S->br_speed = x->speed;
    S->br_last = 0.0f;
    S->br_stall = 0;
    em_bridge_write(w, S, 0.0f);
    return 1;
}

/* a clear, then the bridge's queue entry */
static void em_bridge_queue(sa1g_world *w, em_state *S, const em_player *p, em_entry *x)
{
    (void)w;
    em_clear(S, p->twp);
    em_push(S, p->twp, x);
    S->bridges++;
}

/* round 100-EV (the lift, host deviation D-EV1): the lift of what the queue draws now - a bridge from one end's lift to the
   other's by its frame, an entry its own; 0 without an emote (the option is the caller's: sa1ev_lift_of) */
static float em_lift_now(const sa1g_world *w, const em_state *S, const em_player *p)
{
    uint32_t head = em_rd32(w, p->ewp + EM_EWP_LIST40), j, h = 0xFFFFFFFFu;
    if (S->in_bridge || S->in_back || (S->state == SA1_EMOTE_STATE_NONE && S->fading)) {
        float f = em_arena(head) ? em_rdf(w, p->ewp + EM_EWP_F3C) : 1.0f;
        float t = S->br_speed < 1.0f ? f / (1.0f - S->br_speed) : 1.0f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return S->br_lift_from + (S->br_lift_to - S->br_lift_from) * t;
    }
    if (S->state == SA1_EMOTE_STATE_NONE) {
        return 0.0f;
    }
    for (j = 0; j < S->n && h == 0xFFFFFFFFu; j++) {
        if (S->e[j].group == S->group && (S->e[j].node == head || !em_arena(head))) {
            h = j;              /* the head, or (the queue ran empty: between groups) the group that just played */
        }
    }
    return h != 0xFFFFFFFFu ? S->e[h].lift : 0.0f;
}

/* group g, by a bridge from `from` when one can be made; from_lift: the lift drawn where the bridge starts (round 100-EV) */
static void em_begin_group(sa1g_world *w, em_state *S, const em_player *p, uint32_t g, const em_pose *from, float from_lift)
{
    uint32_t j;
    for (j = 0; j < S->n; j++) {
        if (S->e[j].group == g) {
            em_pose to;
            em_entry x;
            S->br_lift_from = from_lift;
            S->br_lift_to = S->e[j].lift;
            if (from != NULL && em_entry_pose(w, &S->e[j], &to) && em_bridge_make(w, S, from, &to, S->e[j].texlist, &x)) {
                em_bridge_queue(w, S, p, &x);
                S->group = g;
                S->in_bridge = 1;
                S->groups_queued++;
                return;
            }
            break;
        }
    }
    em_queue_group(w, S, p, g);
}

/* the bridge back: from `from` to the gameplay's pose (live: em_bridge_live). 0: no bridge */
static int em_begin_back(sa1g_world *w, em_state *S, const em_player *p, const em_pose *from)
{
    em_pose to;
    em_entry x;
    float lf = em_lift_now(w, S, p);    /* round 100-EV: before the bridge replaces what is drawn */
    S->br_lift_from = lf;
    S->br_lift_to = 0.0f;
    if (p->jvmode != 1u || !em_gameplay_pose(w, p, &to) || !em_bridge_make(w, S, from, &to, 0u, &x)) {
        return 0;
    }
    em_bridge_queue(w, S, p, &x);
    return 1;
}

/* key 1 of the bridge back follows the gameplay (after every pair); 0: the gameplay draws another tree now: nothing to fade into. Also 0 when the bridge has
   stood still for two pairs: the queue moves only on drawn pairs (review R.17), and nobody sees a character off the host's camera; it must not wait there (a follower's AI is dropped until its emote is over) */
static int em_bridge_live(sa1g_world *w, em_state *S, const em_player *p)
{
    em_pose to;
    em_tree t;
    float f3c = em_rdf(w, p->ewp + EM_EWP_F3C);
    S->br_stall = f3c == S->br_last ? S->br_stall + 1u : 0u;
    S->br_last = f3c;
    if (p->jvmode != 1u || S->br_stall > 2u || !em_gameplay_pose(w, p, &to)) {
        return 0;
    }
    if (to.obj != S->br_obj) {
        em_tree d;
        if (!em_tree_of(w, to.obj, &t) || !em_tree_of(w, S->br_obj, &d) || t.n != d.n || memcmp(t.shape, d.shape, t.n) != 0) {
            return 0;
        }
    } else if (!em_tree_of(w, to.obj, &t)) {
        return 0;
    }
    return em_bridge_end(w, S, &t, &to, 1u);
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
    if (w->slot != 0) {
        return 0;               /* round 0.9.9: a follower's input is the AI's, dropped while the emote plays (fw_ai_exec): only the host's stop and the state rules end it */
    }
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
    S->n = 0;
    S->hold_phase = 0;
    S->hold_node = 0;
    S->in_bridge = 0;
    S->in_back = 0;
}

/* a cancel: the emote is over now (state NONE, the gameplay of this pair is a world's that never emoted); the queue only draws the fade into the gameplay's pose, when one can be made */
static void em_leave(sa1g_world *w, em_state *S, const em_player *p)
{
    em_pose from;
    int fade = p->jvmode == 1u && em_drawn_now(w, p, &from) && em_begin_back(w, S, p, &from);
    em_end(S, p, !fade);
    S->fading = fade != 0;
}

static void em_drop(em_state *S)
{
    /* the player task was made again: its new event work holds no queue; no call into the game */
    S->state = SA1_EMOTE_STATE_NONE;
    S->hammer = 0;
    S->n = 0;
    S->hold_phase = 0;
    S->hold_node = 0;
    S->in_bridge = 0;
    S->in_back = 0;
    S->fading = 0;
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

/* STOP_BLEND (at a pair start): the groups not queued yet go; the bridge back from where the character is, at once (a held frame, a loop in its cycle) */
static void em_stop_blend(sa1g_world *w, em_state *S, const em_player *p)
{
    em_pose from;
    if (S->in_back) {
        return;                 /* it is on its way back */
    }
    if (S->hold_phase != 0) {
        S->hold_releases++;
    }
    S->groups = S->group + 1u;
    S->back = 0;
    if (em_drawn_now(w, p, &from) && em_begin_back(w, S, p, &from)) {
        S->in_back = 1;
        S->state = SA1_EMOTE_STATE_ENDING;
        return;
    }
    em_end(S, p, 1);            /* another tree: the plain pop */
    S->ends++;
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
    if ((S->state != SA1_EMOTE_STATE_NONE || S->fading) && p.twp != S->ptwp) {
        em_drop(S);
    }
    if (S->state != SA1_EMOTE_STATE_NONE && em_form_changed(S, &p)) {
        em_end(S, &p, 1);
        S->cancels_state++;
    }
    if (S->fading && em_form_changed(S, &p)) {
        em_clear(S, p.twp);
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
        } else if (S->fading && S->req_how == SA1_EMOTE_STOP_NOW) {
            em_clear(S, p.twp);
        }
    }
    if (S->req_start) {
        em_plan_ctx c;
        const char *why;
        uint32_t n = 0, link = 0;
        em_pose from;
        int have_from;
        float lift0;
        em_entry plan[EM_MAX_ENTRIES];  /* round 0.9.4b: a refused start leaves a running emote's entries as they are */
        if (p.jvmode != 1u) {
            S->waits_jvmode++;  /* review R.8: 8C04BF00 needs jvmode 1; the start waits (latched) */
            return;
        }
        if (w->slot != 0 && p.mode != em_stand_mode(p.kind) && (p.flag & EM_FLAG_GROUND) && p.wtimer == 0 && !em_board(&p) && S->settle_wait < 30u) {
            /* round 0.9.9, a follower: the AI's output has been dropped since the latch (fw_ai_exec): it slows down and stands in a few steps; 30 pairs at most, then the rule decides */
            S->settle_wait++;
            S->waits_settle++;
            return;
        }
        S->settle_wait = 0;
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
        c.ev_mot = S->req_ev;   /* round 100-EV: the slab copies of the latch (0 for the other items) */
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
        lift0 = (S->state != SA1_EMOTE_STATE_NONE || S->fading) ? em_lift_now(w, S, &p) : 0.0f;    /* round 100-EV */
        memcpy(S->e, plan, sizeof(em_entry) * n);
        S->run_slab = S->req_has_ev ? S->req_slab : 2u;     /* round 100-EV: the other slab is free for the next request */
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
        S->back = (uint32_t)c.back;
        S->rate = c.rate;
        /* the state before the queue: a held entry may be held right away (HOLDING, em_hold_step) */
        S->state = SA1_EMOTE_STATE_PLAYING;
        have_from = em_drawn_now(w, &p, &from);
        em_begin_group(w, S, &p, 0u, have_from ? &from : NULL, lift0);
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
        em_leave(w, S, &p);
        S->cancels_input++;
    }
}

static void em_post(sa1g_world *w, int32_t sub, void *user)
{
    em_state *S = em_state_of(w);
    em_player p;
    uint32_t head;
    (void)user;
    if (S == NULL || (S->state == SA1_EMOTE_STATE_NONE && !S->fading)) {
        return;
    }
    if (!em_player_of(w, &p) || p.twp != S->ptwp) {
        em_drop(S);
        return;
    }
    if (S->state == SA1_EMOTE_STATE_NONE) {
        /* the fade after a cancel: until the queue has run empty, or the gameplay draws another tree */
        if (!em_arena(em_rd32(w, p.ewp + EM_EWP_LIST40)) || (sub == 1 && !em_bridge_live(w, S, &p))) {
            em_clear(S, p.twp);
        } else {
            em_bridge_write(w, S, em_rdf(w, p.ewp + EM_EWP_F3C));
        }
        return;
    }
    if (sub == 1) {
        int cancel = p.wtimer != 0 || em_form_changed(S, &p) || em_board(&p) || (p.flag & EM_FLAG_HOLD) != S->held;
        if (!(S->flags & SA1_EMOTE_IN_AIR) && p.mode != em_stand_mode(p.kind)) {
            cancel = 1;
        }
        if (cancel) {
            if (em_form_changed(S, &p)) {
                em_end(S, &p, 1);
            } else {
                em_leave(w, S, &p);
            }
            S->cancels_state++;
            return;
        }
    }
    head = em_rd32(w, p.ewp + EM_EWP_LIST40);
    if (!em_arena(head)) {
        if (S->in_back) {
            em_end(S, &p, 1);           /* the natural end: the clear frees the blend source the queue keeps */
            S->ends++;
            return;
        }
        if (S->in_bridge || S->group + 1u < S->groups) {
            uint32_t g = S->in_bridge ? S->group : S->group + 1u;
            if (p.jvmode != 1u) {
                S->waits_jvmode++;      /* review R.8: the next group waits for jvmode 1 */
                return;
            }
            if (!em_heap_ok(w, em_group_entries(S->e, S->n, g), &S->heap_largest)) {
                em_note(S, "the game's memory is full");
                em_end(S, &p, 1);
                S->cancels_state++;
                return;
            }
            if (S->in_bridge) {
                S->in_bridge = 0;
                em_queue_group(w, S, &p, g);
            } else {
                em_pose from;
                uint32_t j, have = 0;
                for (j = 0; j < S->n && !have; j++) {
                    if (S->e[j].group == S->group && em_entry_pose(w, &S->e[j], &from)) {
                        from.frame = (float)em_rd32(w, from.mot + EM_MOT_NBFRAME) - 1.0f;
                        have = 1;
                    }
                }
                em_begin_group(w, S, &p, g, have ? &from : NULL, em_lift_now(w, S, &p));
            }
        } else {
            em_pose from;
            uint32_t j, have = 0;
            for (j = 0; S->back && j < S->n && !have; j++) {
                if (S->e[j].group == S->group && em_entry_pose(w, &S->e[j], &from)) {
                    from.frame = (float)em_rd32(w, from.mot + EM_MOT_NBFRAME) - 1.0f;
                    have = 1;
                }
            }
            if (have && em_begin_back(w, S, &p, &from)) {
                S->in_back = 1;
                S->state = SA1_EMOTE_STATE_ENDING;
                return;
            }
            em_end(S, &p, 1);           /* the natural end: the clear frees the blend source the queue keeps */
            S->ends++;
            return;
        }
    } else if (S->in_back && sub == 1 && !em_bridge_live(w, S, &p)) {
        em_end(S, &p, 1);               /* the gameplay draws another tree now */
        S->ends++;
        return;
    }
    if (S->in_bridge || S->in_back) {
        em_bridge_write(w, S, em_rdf(w, p.ewp + EM_EWP_F3C));
    } else {
        em_hold_step(w, S, &p); /* round 0.9.4b: HOLD keeps the last entry on its last frame (never writes f3C); a freed node's address may be a bridge's now */
    }
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
    /* in a bridge: the entry it goes to, frame 0; on the way back: past the last item (count, no row) */
    o->emote_frame = em_arena(head) && !S->in_bridge && !S->in_back ? em_rdf(w, p.ewp + EM_EWP_F3C) : 0.0f;
    if (S->in_back) {
        o->emote_item = (int32_t)S->count;
        return;
    }
    h = em_head_index(S, head);
    if (h == 0xFFFFFFFFu) {
        uint32_t j, g = S->in_bridge ? S->group : S->group + 1u;
        for (j = 0; j < S->n && h == 0xFFFFFFFFu; j++) {
            if (S->e[j].group == g) {
                h = j;          /* in a bridge, or between groups (waiting for jvmode): the next one */
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
    r = em_refusal_of(w, &p, e->flags);
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

int sa1em_world_wants_hold(const struct sa1g_world *w)
{
    const em_state *S = em_state_of(w);
    return S != NULL && (S->state != SA1_EMOTE_STATE_NONE || S->req_start);
}

static em_state *em_attach(sa1_context *ctx, sa1g_world *w)
{
    em_state *S = em_state_make(w);
    if (S == NULL) {
        return NULL;
    }
    S->slot = (uint32_t)w->slot;
    if (S->bridge == 0) {
        /* round 0.9.9a: the bridge's block (a failure leaves the transitions as plain pops) */
        S->bridge = sa1g_world_ext_alloc(w, EM_BR_SIZE, 16, NULL);
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
    S->req_has_ev = 0;          /* round 100-EV: no copies (sa1em_world_latch_ev) */
    memset(S->req_ev, 0, sizeof S->req_ev);
    return SA1_OK;
}

/* round 100-EV: the latch with the request's cutscene animations, copied into the free slab (src/sa1_emote.h) */
sa1_status sa1em_world_latch_ev(sa1_context *ctx, struct sa1g_world *w, const sa1_emote *e, int early,
                                const sa1em_motion_block *const *blocks)
{
    em_state *S;
    uint32_t i, j, k, slab, busy, used = 0, ev[SA1_EMOTE_MAX_ITEMS];
    uint8_t *base;
    if (w == NULL || e == NULL || blocks == NULL || e->count > SA1_EMOTE_MAX_ITEMS) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    S = em_attach(ctx, w);
    if (S == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    for (k = 0; k < 2u; k++) {
        if (S->slab[k] == 0) {
            S->slab[k] = sa1g_world_ext_alloc(w, SA1EM_SLAB_SIZE, 16, NULL);
            if (S->slab[k] == 0) {
                return SA1_ERR_OUT_OF_MEMORY;
            }
        }
    }
    busy = (S->state != SA1_EMOTE_STATE_NONE || S->fading) ? S->run_slab : 2u;
    slab = busy == 0u ? 1u : 0u;
    base = w->ext + (S->slab[slab] & 0xFFFFFFu);
    memset(ev, 0, sizeof ev);
    for (i = 0; i < e->count; i++) {
        const sa1em_motion_block *b = blocks[i];
        uint32_t size, g, off, nf, nodes, dup = 0;
        uint8_t *d;
        if (b == NULL) {
            continue;
        }
        for (j = 0; j < i && !dup; j++) {
            if (blocks[j] == b) {
                ev[i] = ev[j];
                dup = 1;
            }
        }
        if (dup) {
            continue;
        }
        size = (b->size + 15u) & ~15u;
        if (b->size < 12u || used + size > SA1EM_SLAB_SIZE) {
            return SA1_ERR_LIMIT;
        }
        g = S->slab[slab] + used;
        d = base + used;
        memcpy(d, b->data, b->size);
        /* relocate: the header's mdata and every channel pointer of the per-node table, offsets -> guest addresses */
        nf = (uint32_t)(b->inp_fn & 0xFu);
        nodes = b->nodes;
        {
            uint32_t v = g + sa1_rd32(d);
            memcpy(d, &v, 4);
        }
        for (off = 0; off < nodes * nf; off++) {
            uint8_t *q = d + 12u + 8u * nf * (off / nf) + 4u * (off % nf);
            uint32_t v = sa1_rd32(q);
            if (v != 0u) {
                v += g;
                memcpy(q, &v, 4);
            }
        }
        ev[i] = g;
        used += size;
        S->ev_copies++;
        S->ev_bytes += b->size;
    }
    S->req = *e;
    S->req_start = 1;
    S->req_early = early != 0;
    S->req_stop = 0;
    S->req_slab = slab;
    S->req_has_ev = used > 0u;
    memcpy(S->req_ev, ev, sizeof ev);
    return SA1_OK;
}

float sa1em_world_lift(const struct sa1g_world *w)
{
    const em_state *S = em_state_of(w);
    em_player p;
    if (S == NULL || (S->state == SA1_EMOTE_STATE_NONE && !S->fading)) {
        return 0.0f;
    }
    if (!em_player_of(w, &p) || p.twp != S->ptwp) {
        return 0.0f;
    }
    return em_lift_now(w, S, &p);
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
