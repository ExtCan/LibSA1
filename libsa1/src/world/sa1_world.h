/*
 * sa1_world.h - the libsa1 world core (GUEST-ARENA, sadc docs/libsa1_integration.md sections 2.1,
 * 2.2, 2.5-2.7, 6, 7.4, 8.1). Internal: not part of the public API (include/libsa1.h).
 *
 * A world is one 16 MB "DC RAM" arena laid out like the Dreamcast's (1ST_READ.BIN at 0x8C010000, BSS,
 * the character's overlays at their load addresses) plus a 32-entry guest segment table:
 *
 *   seg 0x00        NULL: decode(0) is host NULL; other words of segment 0 trap
 *   seg 0x08-0x0B   static collision segment (64 MB, shared by the worlds of a sa1g_shared,
 *                   read-only once sealed)
 *   seg 0x0C        the arena (P0 0x0C.., P1 0x8C.., P2 0xAC.. alias), 64 KB no-access guards
 *   seg 0x13        trap for negated arena pointers (COLOBJ_HEAD_LINK, 0x73xxxxxx / 0xF3xxxxxx);
 *                   accesses fault, re-encoding a decoded word gives the raw word back
 *   seg 0x18        per-world extension (pcdata, placeholders, kill planes, window landtable ...),
 *                   bump-allocated, committed on demand
 *   seg 0x1D        host-stack window: [entry frame - 16 MB, entry frame) while the world runs
 *   all others      no-access trap reservations (one per segment on 64-bit hosts, so a fault names
 *                   its guest segment; one shared reservation on 32-bit hosts)
 *
 * Running game code: sa1g_world_call (any function, e.g. a lowered call sequence) and the tick
 * functions of sa1_world_tick.c enter the world (process segment globals, MXCSR 0xFFC0, rt
 * pointers, stack window, process tick lock) and arm a recovery point. Every world fault - a
 * software fault raised by the lowered code (encode, code map, fatal stub, sa1g_fatal) or an access
 * violation in game/rt text on a trap/guard/NULL/stack/collision address - longjmps back, marks the
 * world dead and leaves a diagnostic in w->fault (guest address, segment, DC function).
 *
 * One world runs at a time per process (the segment table is a process global, guarded by a lock).
 *
 * A world steps the game only once it is booted (sa1_world_boot.c): sa1g_world_boot builds the
 * runtime, heap, tasks, collision, the tick-contract globals, the camera adapter, the landtable and
 * player 0 on an applied image (6.2-6.5); sa1g_world_transplant takes a reference RAM image instead
 * (9.1 V4). Replay mode (w->replay, w->inject) feeds trace records: the pad at step start, the
 * player-entry state through the world's task walker (6.6, 9.1).
 */
#ifndef SA1_WORLD_H
#define SA1_WORLD_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#include "libsa1.h"
#include "sa1/guest.h"

/*
 * Recovery point: the C library's setjmp. On x86_64 mingw (zig's windows-gnu) setjmp() passes the
 * frame address and longjmp then unwinds with RtlUnwindEx, which cannot walk through the fault
 * trampoline; with a NULL frame longjmp restores the saved registers only (what we want). LLVM's
 * __builtin_setjmp/__builtin_longjmp are NOT usable: on win64 they corrupted volatile locals of the
 * frame that called __builtin_setjmp (phase 1 spike finding).
 */
#if defined(_WIN64) && defined(__MINGW32__)
#define SA1G_SETJMP(b) _setjmp((b), NULL)
#else
#define SA1G_SETJMP(b) setjmp(b)
#endif

/* ---- guest address space (2.1) ----------------------------------------------------------------- */
#define SA1G_SEG_SIZE      0x01000000u     /* one guest segment: 16 MB */
#define SA1G_ARENA_SIZE    0x01000000u     /* DC RAM, guest 0x8C000000-0x8CFFFFFF */
#define SA1G_GUARD_SIZE    0x00010000u     /* no-access pages on each side of a reservation */
/* sa1/guest.h defines the segment numbers it shares with the world (SA1G_SEG_ARENA ...) */
#define SA1G_SEG_NULL      0x00u
#define SA1G_SEG_COL0      0x08u           /* static collision segment 0x08-0x0B */
#define SA1G_SEG_COL_COUNT 4u
#ifndef SA1G_SEG_ARENA
#define SA1G_SEG_ARENA     0x0Cu
#endif
#ifndef SA1G_SEG_NEGTRAP
#define SA1G_SEG_NEGTRAP   0x13u
#endif
#ifndef SA1G_SEG_EXT
#define SA1G_SEG_EXT       0x18u
#endif
#ifndef SA1G_SEG_STACK
#define SA1G_SEG_STACK     0x1Du
#endif
#define SA1G_COL_GUEST     0x88000000u
#define SA1G_COL_SIZE      (SA1G_SEG_COL_COUNT * SA1G_SEG_SIZE)
#define SA1G_EXT_GUEST     0x98000000u
#define SA1G_EXT_SIZE      SA1G_SEG_SIZE
#define SA1G_STACK_GUEST   0x9D000000u
#define SA1G_STACK_WINDOW  SA1G_SEG_SIZE

/* DC memory map of a world image (2.6) */
#define SA1G_FIRST_READ_ADDR 0x8C010000u
#define SA1G_FIRST_READ_SIZE 6735488u      /* 0x66C680 */
#define SA1G_BSS_START       0x8C67C680u
#define SA1G_BSS_END         0x8C900000u   /* also where the overlay slots start (P0 0x0C900000) */

/* ---- faults (6.9) ------------------------------------------------------------------------------ */
enum sa1g_fault_kind {
    SA1G_FAULT_NONE = 0,
    SA1G_FAULT_TRAP,            /* access through a word of an unmapped guest segment */
    SA1G_FAULT_NEGTRAP,         /* access through a negated pointer (segment 0x13) */
    SA1G_FAULT_NULL,            /* NULL + field offset (never-mapped low 64 KB) */
    SA1G_FAULT_GUARD,           /* just outside the arena or the extension segment */
    SA1G_FAULT_STACK,           /* stack-window word outside the live host stack */
    SA1G_FAULT_COLSEG,          /* collision segment: write after sealing, or uncommitted part */
    SA1G_FAULT_EXT,             /* extension segment beyond what is allocated */
    SA1G_FAULT_ENCODE,          /* a host address in no segment was stored into guest memory */
    SA1G_FAULT_CODEMAP,         /* indirect call: no native function / signature mismatch */
    SA1G_FAULT_UNIMPL,          /* fatal boundary stub reached (10.5) */
    SA1G_FAULT_FATAL            /* sa1g_fatal: game-detected fatal state (MFree double free ...) */
};

#define SA1G_ACCESS_READ  0
#define SA1G_ACCESS_WRITE 1
#define SA1G_ACCESS_EXEC  2

typedef struct sa1g_fault {
    int32_t kind;               /* enum sa1g_fault_kind */
    int32_t seg;                /* guest segment of the address, -1 if unknown */
    uint32_t guest;             /* guest data address or DC code address involved (0: unknown) */
    uint32_t sig;               /* code-map faults: signature id of the call site */
    uint32_t dc_func;           /* DC address of the game function that faulted (0: unknown) */
    int32_t access;             /* SA1G_ACCESS_* (access violations) */
    uintptr_t host;             /* host address involved */
    uintptr_t pc;               /* host instruction address of an access violation */
    uintptr_t text_off;         /* pc - start of the game text section (if inside it) */
    uint64_t step;              /* world step counter when it happened */
    char func[48];              /* name of dc_func ("" if unknown) */
    char msg[256];              /* one-line diagnostic */
} sa1g_fault;

/* ---- input (6.6): one port of the runtime's host input, layout of rt_input.c RT_PAD ------------ */
typedef struct sa1g_pad {
    uint32_t connected;         /* 0: nothing in this port */
    uint32_t on;                /* held buttons, Katana PDD_DGT_* (A 0x4, B 0x2, X 0x400, Y 0x200, START 0x8) */
    uint16_t r, l;              /* analog triggers 0..255 */
    int16_t x1, y1;             /* main stick -128..127, y < 0 = forward */
    int16_t x2, y2;
} sa1g_pad;

#define SA1G_PORTS 4u
#define SA1G_PAD_A     0x0004u
#define SA1G_PAD_B     0x0002u
#define SA1G_PAD_X     0x0400u
#define SA1G_PAD_Y     0x0200u
#define SA1G_PAD_START 0x0008u

/* ---- events (7.4) ------------------------------------------------------------------------------ */
#define SA1G_EVENT_QUEUE 256u

typedef struct sa1g_events {
    sa1_event ev[SA1G_EVENT_QUEUE];
    uint32_t head, count;
    uint32_t dropped;           /* pushes lost because the queue was full */
} sa1g_events;

/* ---- overlay images of the code map (2.5) ------------------------------------------------------ */
/* sadc tools/pclower.py gives every overlay image (src/overlays/<dir>: STG01, STG05, TIKAL_PROG ...) its own code
 * map in sa1g_fntab.c, because overlays that share a load slot have overlapping DC code addresses. A world whose
 * image holds that overlay file (the very PRS the C was decompiled from: SHA-1 compared) lists the image in
 * fn_images; while it runs, sa1g_fn_active holds that list and sa1g_fn_lookup / sa1_fn try those images (by code
 * range) before the base map (1ST_READ and the runtime). Keep this struct in step with pclower's gen_fntab(). */
typedef struct sa1g_fnimage {
    const char *name;           /* the overlay: STG01 (the file STG01.PRS) */
    const char *prs;            /* its file on the disc */
    const char *prs_sha1;       /* SHA-1 of that file: the bytes this C was decompiled from */
    uint32_t lo, hi;            /* code range (addr & 0x1FFFFFFF) */
    const sa1g_fnent *(*lookup)(uint32_t g);
    const sa1g_fnent *table;
    size_t count;
} sa1g_fnimage;

#define SA1G_FN_ACTIVE_MAX 4u
extern const sa1g_fnimage sa1g_fnimages[];      /* pclower's sa1g_fntab.c (sa1_world_nogame.c: none) */
extern const size_t sa1g_fnimage_count;
/* the running world's images, NULL-terminated (sa1g_world_enter / leave, like sa1g_seg) */
extern const sa1g_fnimage *sa1g_fn_active[SA1G_FN_ACTIVE_MAX + 1];
/* a function of the running world's images at a DC code address (NULL: none; outside a world: none) */
const sa1g_fnent *sa1g_fn_image_lookup(uint32_t g);
/* the registered image of that name ("STG01"), or NULL */
const sa1g_fnimage *sa1g_fnimage_find(const char *name);
/* LIBSA1_HOST (sa1_world_hostobj.c): the synthetic code image of libsa1's own task execs (0x7F000100 = the host-object
 * proxies' exec), appended to every world's fn_images (sa1_world_image.c) so the game's executor, DestroyTask and the
 * replay walker reach it through sa1_fn like overlay code (sadc docs/libsa1_interaction.md 2.3, 6.4 row 1) */
extern const sa1g_fnimage sa1g_hostobj_image;
/* LIBSA1_BOARD (round 0.9.5, sa1_world_board.c): libsa1's board execs (the BOARD preview 0x7F010100, the start push
   0x7F010200, Sonic's board pick 0x7F010300),
   appended after LIBSA1_HOST to every world's fn_images (sa1_world_image.c) */
extern const sa1g_fnimage sa1g_board_image;
/* a rebuild (apply_image / apply_ram) forgets the world's queued requests and per-step host-object state; the options and
 * the kind mask stay (sa1_world_hostobj.c) */
struct sa1g_world;
void sa1g_hostobj_reset_run(struct sa1g_world *w);

/* ---- context-wide state: the static collision segment (2.1, 5.4) ------------------------------- */
typedef struct sa1g_shared {
    uint8_t *col_reserve;       /* collision segment + guard */
    size_t col_reserve_size;
    uint8_t *col;               /* guest 0x88000000 */
    size_t col_used, col_committed;
    int32_t col_sealed;         /* pages are read-only */
} sa1g_shared;

/* ---- images (2.5-2.7) --------------------------------------------------------------------------- */
#define SA1G_IMAGE_MAX_SPANS 8u
#define SA1G_KIND_BARE       (-1)   /* 1ST_READ.BIN only, no character patches (tests, transplant) */

/* sa1g_image_compose flags */
#define SA1G_IMAGE_NO_OPTIONAL   0x1u   /* skip S_SBMOT / M_SBMOT / B_CHAOS7 */
#define SA1G_IMAGE_ACCEPT_MODIFIED SA1_LOAD_ACCEPT_MODIFIED  /* overlays with an unknown SHA-1 (1ST_READ must match) */

typedef struct sa1g_span {
    char name[32];              /* "1ST_READ.BIN", "S_MOT.PRS", ... */
    uint32_t guest;             /* load address as spelled in pointers */
    uint32_t size;
    const uint8_t *data;
    uint8_t *owned;             /* buffer to free (sa1_mem_free), or NULL */
    char sha1[41];              /* SHA-1 of the file it came from (the packed PRS; "" unknown): an overlay image's
                                   code map applies to exactly that file (2.5) */
} sa1g_span;

enum sa1g_patch_kind {
    SA1G_PATCH_WORD = 1,        /* store `value` at `guest` */
    SA1G_PATCH_ACTION_PLACEHOLDER /* PL_ACTION.actptr at `guest` -> the world's placeholder action */
};

typedef struct sa1g_patch {
    int32_t kind;               /* enum sa1g_patch_kind */
    uint32_t guest;             /* patched word */
    uint32_t value;             /* WORD: new value; PLACEHOLDER: the original actptr */
    uint32_t arg;               /* PLACEHOLDER: action index */
    const char *why;            /* static text */
} sa1g_patch;

typedef struct sa1g_image {
    sa1_context *ctx;           /* allocations and logging */
    int32_t kind;               /* SA1_CHAR_* or SA1G_KIND_BARE */
    uint32_t flags;
    sa1g_span spans[SA1G_IMAGE_MAX_SPANS];
    uint32_t num_spans;
    sa1g_patch *patches;
    uint32_t num_patches, cap_patches;
    int32_t finalized;
    uint32_t main_object;       /* root NJS_OBJECT of the character (placeholder actions) */
} sa1g_image;

/* ---- the world (6.1) ---------------------------------------------------------------------------- */
struct sa1g_world;
typedef void (*sa1g_step_fn)(struct sa1g_world *w, int32_t sub, void *user);
typedef void (*sa1g_world_fn)(struct sa1g_world *w, void *user);
/* a native replacement of a task exec (replay executor, sa1_world_boot.c): tp = the task (guest) */
typedef void (*sa1g_exec_fn)(struct sa1g_world *w, uint32_t tp, void *user);

/* Replay inputs of one step (design 6.6, 9.1 V4-V6; trace v2 records): what sa1g_step_game writes
 * instead of the live host input. The pad goes in at the start of the step (the real pad update
 * 8C050BE4 runs on the record's raw input, then on/press and the analog are set to the record's);
 * the "player-entry" state (rand, XMTRX, camera taskwk, frame counter, rings) at the first entry of
 * player 0's exec in the step, where trace v2 captured it (harness t_sim.c px_capture). */
#define SA1G_INJ_PAD        0x01u   /* raw stick / buttons + B_8C754C08[0].on/.press */
#define SA1G_INJ_ANALOG     0x02u   /* B_8C754DCC[0] ang / stroke */
#define SA1G_INJ_RAND       0x04u   /* D_8C5FBB08 at player entry */
#define SA1G_INJ_XMTRX      0x08u   /* XMTRX at player entry */
#define SA1G_INJ_CAMERA     0x10u   /* camera taskwk blob (and D_8C18B7EC) at player entry */
#define SA1G_INJ_FRAME      0x20u   /* B_8C752BE8 at player entry */
#define SA1G_INJ_RINGS      0x40u   /* B_8C752BE4 at player entry */
#define SA1G_INJ_ALL        0x7Fu

typedef struct sa1g_inject {
    uint32_t flags;             /* SA1G_INJ_* used by the replay steps */
    /* raw input (pdGetPeripheral port A) and what the pad update must leave */
    int16_t stick_x, stick_y;   /* -128..127, y < 0 = forward */
    uint32_t buttons;           /* held, Katana PDD_DGT_* bits */
    uint32_t pad_on, pad_press; /* B_8C754C08[0].on / .press */
    uint32_t analog_ang;        /* B_8C754DCC[0].ang (BAMS) */
    float analog_stroke;        /* B_8C754DCC[0].stroke */
    /* player-entry state */
    uint32_t rand;
    uint32_t xmtrx[16];         /* bit patterns, XF0..XF15 */
    uint32_t cam_twp;           /* guest taskwk the blob goes to (0: the world's camera adapter) */
    uint8_t cam_raw[0x40];
    uint32_t frame_counter;
    int16_t rings;
    /* results of the last step */
    uint32_t player_entries;    /* entries of player 0's exec (trace v2 px_calls) */
    uint32_t player_exec;       /* the exec that was entered first (px_exec) */
    uint32_t pre_rand;          /* D_8C5FBB08 at that entry before the injection */
    uint32_t pre_xmtrx[16];     /* XMTRX at that entry before the injection */
    /* the world's trace v2 px_* (state at that entry, after the injection) */
    uint32_t px_rand, px_frame_counter;
    int16_t px_rings;
    uint32_t px_gravity[3];
    uint32_t px_xmtrx[16];
    uint32_t px_cam_twp;
    uint8_t px_cam_raw[0x40];
    uint32_t pad_computed_on, pad_computed_press;   /* what the real pad update produced */
    uint32_t analog_computed_ang;
    float analog_computed_stroke;
} sa1g_inject;

/* replay-executor override of one task exec address (freeze stubs, the landtable manager; 9.1 V4) */
#define SA1G_MAX_XHOOKS 16u
typedef struct sa1g_xhook {
    uint32_t exec;              /* DC code address (compared physically) */
    sa1g_exec_fn fn;            /* NULL: frozen (the task is skipped) */
    void *user;
    uint32_t calls;
} sa1g_xhook;

/* landtable of a booted world (6.2, 5.3) */
enum sa1g_land_mode {
    SA1G_LAND_NONE = 0,         /* B_8C7596F4 untouched (transplants: the RAM image's own) */
    SA1G_LAND_EMPTY,            /* an empty landtable in segment 0x18 (M0) */
    SA1G_LAND_DISC,             /* the stage's own landtable in the image (D_8C19E9FC[level][act]) */
    SA1G_LAND_WINDOW            /* the window landtable in segment 0x18, filled by the collision adapter */
};

/* host objects, the character side of a world (API 0.3 round 5, sa1_world_hostobj.c; sadc docs/libsa1_interaction.md 2.5
 * and 6.1.2): the options and the kind mask (kept across image rebuilds), the queued damage requests and the bookkeeping
 * of one step (hostobj_pre -> hostobj_post), the held state after the last step */
#define SA1G_HO_DAMAGE_MAX 4u
typedef struct sa1g_ho_damage {
    uint32_t flags;             /* SA1_DAMAGE_* */
    uint32_t object;            /* the host object credited */
    float source[3];            /* host space (converted inside the world, in its FP mode) */
} sa1g_ho_damage;

typedef struct sa1g_ho_char {
    uint32_t opts;              /* 1 << SA1_COPT_* */
    uint32_t kind_mask;         /* SA1_HOSTOBJ_KIND_BIT bits of the kinds the world mirrors; 0 = every kind */
    uint32_t ndmg;              /* queued sa1_character_damage requests */
    sa1g_ho_damage dmg[SA1G_HO_DAMAGE_MAX];
    /* one step */
    int32_t rings_before;       /* B_8C752BE4 before the executor */
    uint32_t item_before;       /* player 0's playerwk.item before the executor (before a phantom barrier) */
    uint32_t src_object;        /* hurt attribution of the pending hit: the host object (0: none) */
    uint32_t src_flags;         /* SA1_HURT_HOST / SA1_HURT_SURFACE */
    uint32_t phantom;           /* the pre hook set a phantom barrier (playerwk.item 1) */
    uint32_t death_cause, death_object;     /* a KILL request of this step (SA1_DEATH_HOST) */
    /* the held state after the last step (SA1 space) */
    uint32_t held_object, held_phase, held_ang;
    float held_pos[3], held_prev[3], held_hand[3];
    /* round 0.9.1 (E1, sadc docs/research/gsa1_070_drown.md with its Review R3.6 / R4.2): what the death rules of
       hostobj_post compare with, taken before the executor of each step */
    int32_t smode_before;       /* player 0's taskwk.smode (drowning: 8C07BE6E's SetInputP(0, 39) in this step) */
    uint32_t dz_task;           /* the death-zone task (exec 8C07BAC0, whoever created it), 0: none */
    uint32_t dz_child_before;   /* its first child (tp->ctp): 8C07BD48 adds the death task when the zone kills */
    uint32_t ev_pre;            /* the world's queued events: the game's RESTART requests of this step follow them */
    uint32_t death_seen;        /* an SA1_EVENT_DEATH or a RESTART (id 2) since the world started (the fallback's latch) */
    /* round 0.9.1 (companions, src/world/sa1_world_charopt.c): companions whose game-made task is freed right after the
       world's first pair (SA1_COMPANION_* bits; the wish was off and the game's own gate passed at boot) */
    uint32_t companion_off;
    /* round 0.9.3a (sadc docs/research/super_general_design.md 4.12): the held proxy at the drawn step (sub step 0) of
       the last pair and of the pair before, and the holder's yaw there (BAMS); held_drawn_obj: the object they belong to
       (0: none yet) */
    float held_drawn[3], held_drawn_prev[3];
    uint32_t held_drawn_ang, held_drawn_prev_ang, held_drawn_obj;
} sa1g_ho_char;

/* round 0.9.1 HUD (src/world/sa1_world_hud.c; sadc docs/research/gsa1_070_hud.md F3 / F4 with its Review R.5.1 / R.5.2):
 * the game's HUD calls of a pair and the game clock's hold. Zero in every world whose host never asked for them (the
 * reference replays, the tests): such a world runs exactly as before. Kept across image rebuilds. */
#define SA1G_HUD_NPARTS 3u          /* the parts of the pass: 0 rings (8C08A4BA, rings), 1 lives (8C08A4BA, lives), 2 time */
typedef struct sa1g_hud {
    uint32_t parts;                 /* SA1_HUD_RINGS | SA1_HUD_LIVES | SA1_HUD_TIME of the pass (0: no pass) */
    int32_t hold_clock;             /* 1: D_8C1BF198 = 0 right before the game clock 8C08A5CC of every step (E-102 without
                                       his time limit: his countdown never moves; game code that starts the clock again,
                                       8C06132A, cannot run it) */
    int32_t single;                 /* tests: the game's own single calls (8C08A4BA, then 8C08A724 unless the level type is
                                       1) instead of the split, for the RAM comparison of hud_test */
    uint32_t cap_block;             /* guest address of the runtime's display-capture header (0: not looked for yet, 1:
                                       none in this build) */
    /* the last pass (record index ranges of the pair's capture, for sa1_character_screen's parts) */
    uint64_t pass_pair;             /* w->pairs + 1 when the pass ran (0: never) */
    int32_t pass_frame;             /* B_8C752BE8 during the pass (the frame counter its records carry) */
    uint32_t first[SA1G_HUD_NPARTS], end[SA1G_HUD_NPARTS];
    uint32_t passes;                /* passes run over the world's life */
} sa1g_hud;

/* round 0.9.2 (WTR-A; sadc docs/research/gsa1_090_water.md WTR-A with its Review R4 a-c, R3.3-R3.5): the host's water at
 * the character, sa1_input.host_flags of the tick, kept per world by sa1g_world_host_flags (sa1_world_tick.c) when the
 * tick's input arrives, with player 0's position at that moment (the state the host tested: the pairs of a tick share the
 * flags, so the window keeps the water the character came down into during the tick); and what the collision window did
 * with it and with round 0.9.1's deep-water rule W1 (sa1_coladapt_world.c cw_water*: tests and diagnostics). All zero in a
 * world whose host gives no flags (the reference replays, older hosts): such a world runs exactly as before. */
#define SA1G_HOSTWATER 1
typedef struct sa1g_hostwater {
    uint32_t flags;                 /* SA1_INPUT_HOST_* of the tick (0: none given, or IN_WATER without KNOWN) */
    int32_t ref_valid;              /* ref holds player 0's taskwk.pos when the flags came */
    float ref[3];                   /* SA1 space */
    uint32_t dry_steps;             /* refreshes under KNOWN without IN_WATER (player 0 not E-102): no W1 */
    uint32_t dry_left_out;          /* water objects those refreshes left out of the window (a water polygon over the
                                       character) */
    uint32_t dry_entered;           /* ... kept: the character came down through that surface in this tick (below ref, no
                                       land between) */
    uint32_t wet_steps;             /* refreshes under KNOWN | IN_WATER that ran W1 */
    uint32_t wet_through;           /* W1 widenings without the land test (deep water over a submerged ledge) */
    uint32_t wet_far;               /* W1 left alone under IN_WATER: land between and the water more than
                                       CW_WATER_WET_REACH above (a cut water top: Review R3.3) */
    uint32_t moving_blocked;        /* W1 left alone because a moving object lies between (Review R3.4) */
} sa1g_hostwater;

/* the host camera of live mode (6.7), written into the camera adapter taskwk before each step */
typedef struct sa1g_camera {
    uint32_t ang[3];            /* BAMS; ang.z >= 0x4000 inverts the steering (8C06FB8C) */
    float pos[3];               /* SA1 space */
    int8_t smode;
    /* round 0.9.5 (SA1_INPUT_HOST_CAMERA; sadc docs/research/board095_cam.md 5.3, gsa1_095_plan.md 5.3): the host's view of
       this tick, latched by sa1w_world_input_ex (src/world/sa1_api_world.c) in HOST space with the context's coords; the
       live step converts host_pos into pos (sa1g_camera_host, sa1_world_tick.c) inside the world's FP mode. host 0: none
       (every replay, preview world, test and host without the bit: the virtual eye, as 0.9.4f). host_tgt is kept for
       later rounds (0.9.6's look CAMERA mode, 0.9.7's camera seeding); 0.9.5's step does not read it */
    int32_t host;
    float host_pos[3], host_tgt[3];
    float co_scale, co_axis[9];
} sa1g_camera;

/* round 0.9.3a (src/world/sa1_world_super.c): the General form's options and run state */
typedef struct sa1g_super_gen {
    /* options (sa1_character_set_super_option; SA1_SUPOPT_*): kept across rebuilds */
    int32_t speed;              /* SA1_SUPER_SPEED_* */
    int32_t no_aura;            /* 1: no aura task in a General world */
    int32_t voice;              /* the transformation tone (0: none) */
    int32_t effects;            /* SA1_SUPER_EFFECTS_* bits (round 0.9.4a; 0.9.3a's 1 = CLONES): the Super trail / ball
                                   after-images (the clones) and the golden dust (the SA1_SUPERDUST block) while Super */
    /* round 0.9.4a (super2_plan.md 5.4, sa1_world_super.c): SA1_SUPOPT_SUPER_ANIMS / SA1_SUPOPT_LSD_GOLD. Unlike the four
       above, sa1g_world_reset_run zeroes them (sa1_world_image.c keeps only the 0.9.3a options): a character world gets
       them again from its attach (sa1_world_charopt.c co_supopts), which runs after the world's only image application */
    int32_t anims;              /* 1: GA (the game's Super motions for standing and moving) while the look is on */
    int32_t lsd_gold;           /* 1: the Light Speed charge recoloured gold while the look is on (the capture's decision) */
    /* run state (reset by sa1g_world_reset_run) */
    uint32_t gt;                /* guest address of the action-table copy GT (0: not built) */
    uint32_t gw;                /* the weld list GW (Sonic's 34 entries with the 12 Super ones set up, + 5 hand entries) */
    uint32_t tmp;               /* the set-up list TMP */
    uint32_t gw_entries;        /* entries of GW (no terminator) */
    int32_t welds;              /* 0: not tried, 1: set up, -1: failed (the look runs without the Super welds) */
    int32_t look;               /* 1: the General look is on (GT, the SUPERSONIC names in texlist_SONIC) */
    int32_t names_super;        /* the names texlist_SONIC holds now (1: SUPERSONIC's) */
    int32_t pending;            /* an order waiting for the first pair (SA1_ORDER_SUPER / NORMAL, 0: none) */
    int32_t pending_voice;      /* the waiting order came from the host (the voice plays with it) */
    int32_t start_done;         /* SA1G_SUPER_START was carried out */
    int32_t voice_armed;        /* request worlds: the host's request 46 was sent; the voice plays when the bit is set */
    int32_t guard_on;           /* RC7: the last pair drew an action without a Super object (Sonic's names put back) */
    int32_t leak_seen;          /* a General world reached the game's Super modes 71-78 */
    uint32_t transforms;        /* General transformations (orders and START) */
    uint32_t reverts;           /* General reverts (orders, the drain at 0, the game clearing the bit) */
    uint32_t drain_reverts;     /* ... of them by the drain at 0 rings */
    uint32_t bit_reverts;       /* ... of them because the game cleared the bit (the look followed) */
    uint32_t voices;            /* voice tones played */
    uint32_t guard_warns;       /* RC7 warnings */
    uint32_t leaks;             /* pairs in modes 71-78 in a General world */
    uint32_t link_fixes;        /* link actions re-pointed at a switch (mtnmode 2) */
    uint32_t speed_hooks;       /* first execs of the physics task the speed set followed */
    uint32_t effect_clones;     /* first execs of the trail / ball after-images run as the Super clones */
    uint32_t warn_pending;      /* SA1_LOG_WARN texts waiting for the bridge (bit 0 guard, bit 1 leak, bit 2 welds) */
    /* round 0.9.4a (super2_plan.md 6.1 tasks 2-3 with owner answer O1): GA, the action table with the game's Super motions
       for standing and moving (a copy of GT whose stand / move rows draw the lifted 130-134), built right after GT */
    uint32_t ga;                /* guest address of GA (0: not built) */
    int32_t ga_failed;          /* 1: the build failed (one SA1_LOG_WARN: SA1W_SUPER_WARN_GA; the look keeps GT) */
    uint32_t ga_act[4];         /* the NJS_ACTIONs {Super body, lifted motion} of 130, 131, 132, 133 (= 134) */
    uint32_t ga_mot[4];         /* the lifted NJS_MOTION copies of 0C914D00, 0C90EEA4, 0C90FFC8, 0C91116C */
    float lift;                 /* L = the physics task's center_height (6.4) minus Sonic's (5.4), read from the image */
    int32_t drawn_look;         /* the look as texlist_SONIC had it during the last pair's display (latched by
                                   sa1g_super_pair before it can change the look): sa1w_world_lsd_gold */
    int32_t drawn_pair;         /* 1: a pair ran since the last image application (drawn_look is meaningful) */
    uint32_t ga_switches;       /* plactptr re-pointed GA <-> GT by the option while the look was on */
} sa1g_super_gen;

/* round 0.9.4a (sadc docs/research/faces_wheel_plan.md 5.4): the hooks of a host feature of a character world, installed by
   the feature's own code at its first call for the world (faces: src/world/sa1_world_face.c, emotes: src/world/sa1_world_emote.c);
   all NULL / 0 until then, so a world that never gets such a call runs exactly as before. Kept across image rebuilds (zeroed
   with the world); `state` points into segment 0x18, which every rebuild resets: the feature validates its block by magic and
   self (as sa1_world_set.c's st_state) and makes it again; persist[] holds the feature's values that survive a rebuild. */
#define SA1W_HAS_LANE_HOOKS 1
struct sa1w_pstate;
typedef struct sa1g_lane {
    sa1g_step_fn pre;           /* sa1g_step_game, every step: right after set_pre (before the executor; sub 0 = pair start) */
    sa1g_step_fn post;          /* sa1g_step_game, every step: right after set_post */
    void (*fill)(const struct sa1g_world *w, struct sa1w_pstate *o);   /* sa1w_world_pstate, last */
    sa1_context *ctx;           /* the context whose calls the world runs */
    uint32_t state;             /* guest address of the feature's block in segment 0x18 (0: none) */
    uint32_t persist[8];
} sa1g_lane;

typedef struct sa1g_world {
    uintptr_t seg[32];          /* host base per guest segment ((g >> 24) & 31) */
    uint8_t *ram;               /* the arena: guest 0x8C000000 */
    uint8_t *reserve;           /* guard + arena + guard */
    size_t reserve_size;
    uint8_t *ext_reserve;       /* extension segment 0x18 + guard */
    size_t ext_reserve_size;
    uint8_t *ext;
    size_t ext_used, ext_committed;
    sa1g_shared *shared;        /* collision segment owner (may be NULL) */
    const sa1g_image *image;    /* pristine image last applied (may be NULL) */

    float xmtrx[16];            /* SH-4 XMTRX (rt: sa1_rt_xmtrx) */
    sa1g_pad pad[SA1G_PORTS];   /* what rt's pdGetPeripheral sees during a step (sa1_rt_pad) */
    sa1g_pad host_pad[SA1G_PORTS];  /* latest host input */
    uint32_t taps[SA1G_PORTS];  /* buttons pressed by the host since the last pair (ORed) */
    int32_t replay;             /* replay mode: no live pad (sa1_rt_pad = NULL), records write the pad */
    int32_t display;            /* run the display hook after each pair */

    sa1g_step_fn step;          /* one 60 Hz step (default: sa1g_step_game) */
    sa1g_world_fn display_fn;   /* display pass after a pair (default: sa1g_display_game) */
    sa1g_world_fn adapter_fn;   /* collision adapter refresh inside a step (may be NULL) */
    /* host objects (API 0.3, sa1_world_hostobj.c; sadc docs/libsa1_interaction.md 2.4): hooks of the default step, NULL
       unless the world was attached (sa1ho_world_attach): pre after the pad update and before the executor (window,
       proxies, latched poses on sub step 0), post right after the CCL pass (the hit snapshot) */
    sa1g_step_fn hostobj_pre;
    sa1g_step_fn hostobj_post;
    sa1_context *hostobj_ctx;   /* the context whose host objects the world mirrors */
    uint32_t hostobj;           /* guest address of the world's host-object state in segment 0x18 (0: none yet) */
    uint32_t ho_wtimer;         /* player 0's taskwk.wtimer before the executor of the current step */
    uint32_t ho_hurt_steps;     /* steps since player 0 was last hurt (wtimer became 120); 0xFFFFFFFF: not yet */
    sa1g_ho_char ho;            /* round 5: the character side (options, damage requests, hurt bookkeeping, held state) */
    void *user;

    double acc;                 /* host time not yet consumed (seconds) */
    uint64_t steps, pairs;      /* counters */
    uint32_t backlog_drops;     /* pairs skipped because a tick asked for too many */

    sa1g_events events;
    uint32_t missing_actions[8];    /* bit i: action i was replaced by the placeholder */
    uint32_t placeholder_action;    /* guest address of the placeholder NJS_ACTION (0: none) */
    uint32_t patches_applied;
    uint32_t pcdata_size;           /* bytes of R4 pcdata at the start of segment 0x18 */
    int32_t kind;                   /* SA1_CHAR_* of a character world (-1: none) */
    float start_pos[3];             /* SA1 space: where player creation (6.3) puts the character */
    uint32_t start_ang_y;           /* BAMS */
    uint32_t sr;                    /* SH-4 SR as set_cr / set_imask leave it (startup code only) */
    uint32_t gbr;                   /* SH-4 GBR as a guest word (sa1g_set_gbr) */

    /* boot and player 0 (sa1_world_boot.c, 6.2-6.5, 6.7); reset by apply_image / apply_ram */
    int32_t booted;                 /* boot (or a transplant) ran: the default step may run */
    int32_t chr;                    /* game character id of player 0 (0 Sonic, 2 Tails ... 7 Big), -1 none */
    int16_t level, act;             /* CurrentLevel / CurrentAct the world was booted with */
    uint32_t player_tp;             /* guest address of player 0's task (0: not created by boot) */
    uint32_t camera_tp, camera_twp; /* the camera adapter task and its taskwk (D_8C18B7EC) */
    sa1g_camera camera;             /* live mode: host camera copied into camera_twp before each step */
    int32_t land_mode;              /* enum sa1g_land_mode */
    uint32_t landtable;             /* guest address B_8C7596F4 was set to by boot (0: none) */
    uint32_t window_cols;           /* EMPTY / WINDOW: guest address of the landtable's COL array */
    uint32_t window_cap;            /* its capacity in COL entries */

    /* overlay images of the code map this world holds (2.5; apply_image), NULL-terminated */
    const sa1g_fnimage *fn_images[SA1G_FN_ACTIVE_MAX + 1];
    uint32_t num_fn_images;

    /* stage tasks (SA1G_BOOT_STAGE_TASKS, sa1_world_stage.c) */
    uint32_t level_tp;              /* B_8C752BD4: the level task (0: none) */
    uint32_t stage_tasks;           /* tasks the stage start created (level task, list-2 task) */
    char stage_note[256];           /* what the world has of the stage's own tasks */

    /* replay (6.6, 9.1): inputs of the next step, executor overrides */
    sa1g_inject inject;
    sa1g_xhook xhooks[SA1G_MAX_XHOOKS];
    uint32_t num_xhooks;
    int32_t freeze_unknown;         /* replay executor: skip tasks whose exec has no native code */
    uint32_t frozen_skips;          /* execs skipped by freeze_unknown */
    uint32_t frozen_last;           /* the last exec address skipped */

    /* the static collision this world maps (segments 0x08-0x0B are its segment), when the context's geometry was a
       sa1_collision_prepared at the world's creation: the bridge (sa1_api_world.c) holds a reference until the world is
       destroyed; NULL: the context's own collision segment (w->shared) */
    struct sa1_collision_prepared *prepared;
    /* set by a caller right after sa1g_world_create, before anything wrote the arena: its pages are as the OS committed
       them (zero), so sa1g_world_apply_image does not zero them again (16 MB, half of a world's set-up); cleared by it */
    int32_t fresh;

    /* The drawn step (sa1_world_tick.c sa1g_note_drawn; sadc docs/research/gsa1_vertical_flicker.md fix B): player 0's
       taskwk.pos / ang right after sub step 0 of the last pair, the step whose display the character renderer 8C037098
       draws (it draws on sub step 0 only, sa1_capture.h). The render output roots the drawn body here instead of at the
       state after the pair, one 60 Hz step further. Only read from guest memory: the game state does not change. */
    float drawn_pos[3];             /* SA1 space */
    uint32_t drawn_ang[3];          /* BAMS */
    int32_t drawn_valid;            /* 0: no player 0 after the last sub step 0 (or none ran yet) */
    uint64_t drawn_step;            /* w->steps right after that sub step 0 (the pair's sub step 1 makes it steps - 1) */

    jmp_buf jb;                 /* recovery point of the active SA1G_WORLD_TRY */
    int32_t entered;
    int32_t dead;               /* a fault happened; re-apply the image to revive */
    sa1_status dead_status;
    uint32_t saved_mxcsr;
    uintptr_t stack_top;        /* host frame address at entry (0: no stack window) */
    uint32_t faults;            /* faults recorded over the world's life */
    sa1g_fault fault;

    /* round 0.9.1 (src/world/sa1_world_charopt.c): the per-character settings of the world layer (lives, companions, game
       options), installed by the bridge (sa1w_world_create) in character worlds; NULL in other worlds (tests, the
       harness's host-object oracle, which compiles sa1_world_hostobj.c). Kept across image rebuilds. */
    sa1_status (*charopt_attach)(sa1_context *ctx, struct sa1g_world *w);  /* sa1ho_world_attach calls it last */
    void (*charopt_pre)(struct sa1g_world *w, int32_t sub);                 /* hostobj_pre calls it before its work */

    /* round 0.9.1 HUD (src/world/sa1_world_hud.c): the game's HUD calls of a live pair, run by sa1g_world_pair after sub
       step 1 and before the frame advance 8C08A5BE (where the game's frame steps 8C04E95A / 8C04EA4A make them); NULL in
       every other world and never run in replay mode. hud.hold_clock is read by the step tail (sa1_world_tick.c). */
    void (*hud_fn)(struct sa1g_world *w);
    sa1g_hud hud;

    /* round 0.9.2 (WTR-A): the host's water at the character (sa1g_world_host_flags, sa1_world_tick.c) and the window's
       water bookkeeping (sa1_coladapt_world.c) */
    sa1g_hostwater hostwater;

    /* round 6a (Super Sonic): SA1G_SUPER_* of the boot (0 in every other world), the revert fixes applied, the drain calls
       (src/world/sa1_world_super.c) */
    uint32_t super;
    uint32_t super_reverts;
    uint32_t super_drains;

    /* round 0.9.3a (the General form, src/world/sa1_world_super.c): the host's Super options (kept across image rebuilds:
       the bridge applies them at the world's attach) and the run state of the General form (reset by every rebuild) */
    sa1g_super_gen sg;

    /* round 0.9.4 (SET objects: src/world/sa1_world_set.c; sadc docs/research/gsa1_094_plan.md 6.3 with its Review R.12):
       hooks of the default step, NULL unless the bridge attached the world (sa1g_set_attach, called by sa1g_charopt_attach):
       set_pre right after hostobj_pre, before the executor (sub step 0: the window, the forced state, the manager task;
       every step: the spawn hold of the heap guard), set_post right after hostobj_post (the touch latch, the events).
       set_ctx: the context whose records the world runs; set_state: guest address of the world's SET state in segment
       0x18 (0: none yet; a rebuild resets the segment, the state is made again at the next pair start). Tests only:
       set_no_eye (the live step leaves the camera taskwk's position alone) and set_test_pre (run after set_pre). */
    sa1g_step_fn set_pre;
    sa1g_step_fn set_post;
    sa1_context *set_ctx;
    uint32_t set_state;
    int32_t set_no_eye;
    void (*set_test_pre)(struct sa1g_world *w, int32_t sub);

    /* round 0.9.4a (faces_wheel_plan.md 5.4): host features of a character world (sa1g_lane above) */
    sa1g_lane face;             /* round 0.9.4a: faces (094a-lib-faces) */
    sa1g_lane emote;            /* round 0.9.4a: emotes (094a-lib-emote) */

    /* round 0.9.4a (the owner's 0.9.4 note "the run animations break a little bit when moving at extreme speed"; with the
       drawn step drawn_pos / drawn_ang above, latched by sa1g_note_drawn): player 0's playerwk.mj.nframe and mj.action
       right after sub step 0 of the last pair - the frame the character renderer drew. The render interpolation's motion
       hint starts there (sa1_character.c pair_info / plan_build), not at the frame after the pair one 60 Hz step later
       (at Super speeds that step is up to 6.7 frames of Sonic's 33-frame sprint, and the hint, measured over the wrong
       stretch of the motion, turned limbs the long way round). Kept at the end: the lanes' offsets above stay. */
    float drawn_frame;
    uint32_t drawn_action;
    int32_t drawn_frame_valid;      /* 0: not read with the last drawn_valid */

    /* round 0.9.5 (the board, src/world/sa1_world_board.c; sadc docs/research/gsa1_095_plan.md 6.2): the lane (installed by
       the first sa1_character_board for the world; NULL hooks until then) and the collision window's riding word,
       computed by the lane after every step: 0 on foot (or no lane), else SA1WB_COL_RIDING | the terrain in force
       (SA1_BOARD_TERRAIN_*); read by sa1_coladapt_world.c's window copy and moving objects. Kept at the end: the offsets
       above stay */
    sa1g_lane board;
    uint32_t board_col;
    uint32_t board_col_applied;     /* the word the window last registered its moving objects with (sa1_coladapt_world.c) */
} sa1g_world;
#define SA1WB_COL_RIDING 0x80000000u

/* ---- context-wide collision segment ------------------------------------------------------------ */
int sa1g_shared_create(sa1g_shared *s);
void sa1g_shared_destroy(sa1g_shared *s);
/* Bump-allocate n bytes (zeroed, read/write until sealed) in the collision segment; returns the
 * guest address (0x88000000 + offset) or 0. */
uint32_t sa1g_colseg_alloc(sa1g_shared *s, size_t n, size_t align, void **host);
/* sealed = 1: committed pages read-only (game writes fault); 0: writable again. 0 on success. */
int sa1g_colseg_seal(sa1g_shared *s, int sealed);

/* ---- world lifetime ----------------------------------------------------------------------------- */
/* Reserve the arena (16 MB read/write, zero) between no-access guards, the extension segment, and fill
 * the segment table. `shared` (may be NULL) provides segments 0x08-0x0B. 0 on success. */
int sa1g_world_create(sa1g_world *w, sa1g_shared *shared);
void sa1g_world_destroy(sa1g_world *w);

/* Copy bytes into the arena at a guest address (any P0/P1/P2 spelling). 0 on success. */
int sa1g_world_load(sa1g_world *w, uint32_t guest, const void *data, size_t n);
/* Host pointer of a guest address through this world's segment table (NULL for 0); no checks. */
void *sa1g_world_ptr(const sa1g_world *w, uint32_t guest);
/* Bounded access to arena words (0 / -1 if the range is not inside the arena) */
int sa1g_world_rd32(const sa1g_world *w, uint32_t guest, uint32_t *v);
int sa1g_world_wr32(sa1g_world *w, uint32_t guest, uint32_t v);
/* Bump-allocate n zeroed bytes in the extension segment 0x18; returns the guest address or 0. */
uint32_t sa1g_world_ext_alloc(sa1g_world *w, size_t n, size_t align, void **host);
/* Forget every extension allocation and zero the committed pages (apply_image). */
void sa1g_world_ext_reset(sa1g_world *w);
/* Forget a fault (tests and diagnostics; the arena may be inconsistent afterwards). */
void sa1g_world_clear_fault(sa1g_world *w);

/* ---- running code in a world -------------------------------------------------------------------- */
/* Run fn(w, user) inside the world: tick lock, enter (segments, MXCSR 0xFFC0, rt pointers), host-stack
 * window from this frame, recovery point. Returns SA1_OK, the fault status (SA1_ERR_GAME_FAULT,
 * SA1_ERR_NOT_IMPLEMENTED for fatal stubs) or the dead world's status. */
sa1_status sa1g_world_call(sa1g_world *w, sa1g_world_fn fn, void *user);

/* Lower-level pieces (tests): enter/leave without the lock or the stack window. Leave restores the
 * host's exact MXCSR. */
void sa1g_world_enter(sa1g_world *w);
void sa1g_world_leave(sa1g_world *w);
void sa1g_world_stack_open(sa1g_world *w, uintptr_t top);
void sa1g_world_stack_close(sa1g_world *w);
void sa1g_tick_lock(void);
void sa1g_tick_unlock(void);
/* A process lock for short table updates that host worker threads share with the thread that ticks (the world images of
 * sa1_character_prepare, the per-context world state's creation): never held while game code runs or a file is read. */
void sa1g_table_lock(void);
void sa1g_table_unlock(void);

/* Recovery point (a setjmp in the caller's frame, which must stay active while game code runs). True
 * on the first pass, false after a fault. Locals of the calling function that are modified after the
 * TRY and used after a fault must be volatile. */
int sa1g_world_arm(sa1g_world *w);
void sa1g_world_disarm(void);
#define SA1G_WORLD_TRY(w) (sa1g_world_arm(w), SA1G_SETJMP((w)->jb) == 0)

/* The world currently entered (NULL outside) */
sa1g_world *sa1g_world_current(void);
/* Status a fault maps to */
sa1_status sa1g_fault_status(const sa1g_fault *f);
const char *sa1g_fault_name(int32_t kind);
/* sa1_status_string plus the world-core codes (SA1_ERR_GAME_FAULT) */
const char *sa1g_status_string(sa1_status st);

/* Game-detected fatal state (e.g. PC_ONLY(sa1g_fatal(...)) for MFree's double-free loop). */
SA1G_NORETURN void sa1g_fatal(uint32_t dc_func, const char *what);

/* Fault-handler policy, exposed for tests: is `pc` inside the game text section? */
int sa1g_text_contains(uintptr_t pc);
void sa1g_text_bounds(uintptr_t *begin, uintptr_t *end);

/* ---- images (sa1_world_image.c) ------------------------------------------------------------------ */
/* Compose the pristine image of a character kind from the user's disc (2.5): 1ST_READ.BIN (SHA-1
 * must match) at 0x8C010000, the kind's overlays at their load addresses (one occupant per shared
 * slot; SHA-1 checked unless SA1G_IMAGE_ACCEPT_MODIFIED), PRS-unpacked; then the patch list (2.7). */
sa1_status sa1g_image_compose(sa1_context *ctx, const char *gdi_path, int32_t kind, uint32_t flags, sa1g_image *img);
/* An image from a 1ST_READ.BIN already in memory (copied; SHA-1 checked); overlays can be added. */
sa1_status sa1g_image_from_memory(sa1_context *ctx, int32_t kind, const void *first_read, size_t n, sa1g_image *img);
/* Add an overlay span (copied). Fails on a slot conflict with an existing span. */
sa1_status sa1g_image_add_span(sa1g_image *img, const char *name, uint32_t guest, const void *data, size_t n);
/* Build the patch list (compose does it); idempotent. */
sa1_status sa1g_image_finalize(sa1g_image *img);
void sa1g_image_free(sa1g_image *img);
/* The span holding a guest range, or NULL */
const sa1g_span *sa1g_image_span_at(const sa1g_image *img, uint32_t guest, uint32_t len);
/* Number of patches of one kind */
uint32_t sa1g_image_count_patches(const sa1g_image *img, int32_t kind);

/* (Re)build the world from a pristine image (2.6 steps 1-4): zero the arena and the extension
 * segment, copy the spans, apply the patches (placeholders in segment 0x18). Revives a dead world. */
sa1_status sa1g_world_apply_image(sa1g_world *w, const sa1g_image *img);
/* (Re)build the world from a raw 16 MB DC RAM image (guest 0x8C000000-0x8CFFFFFF, e.g. the harness's
 * `trace --dump-ram-at N`): the arena is that image, the extension segment holds fresh pcdata; no
 * patches, not booted (9.1 V4 transplant: sa1g_world_transplant). Revives a dead world. */
sa1_status sa1g_world_apply_ram(sa1g_world *w, const void *ram, size_t n);
/* DISC mode (5.3, 2.5 "Test: DISC mode"): add the stage overlay STGnn.PRS of `level` from the user's
 * disc (SHA-1 checked unless SA1G_IMAGE_ACCEPT_MODIFIED) at 0x0C900000. The slot must be free: compose
 * with SA1G_IMAGE_NO_OPTIONAL (no B_CHAOS7). Rebuilds the patch list. */
sa1_status sa1g_image_add_stage(sa1g_image *img, const char *gdi_path, int32_t level, uint32_t flags);

/* ---- tick (sa1_world_tick.c, 6.4-6.6, 7.4, 8.1) -------------------------------------------------- */
#define SA1G_PAIR_SECONDS (1.0 / 30.0)  /* one pair = two 60 Hz logic steps */
#define SA1G_MAX_PAIRS_PER_TICK 8u

/* The global the tick contract writes (6.5) */
#define SA1G_ADDR_STEPS_PER_FRAME 0x8C754EC4u   /* B_8C754EC4 = 2 */
#define SA1G_ADDR_SUB_STEP        0x8C754EC8u   /* B_8C754EC8 = 0, 1 */
#define SA1G_ADDR_PAD_PTRS        0x8C754DA8u   /* B_8C754DA8[4]: PDS_PERIPHERAL * per port */
#define SA1G_ADDR_STEP_TAIL_FLAG  0x8C752BDDu   /* B_8C752BDD = 0 after each step */

/* DC functions the default step calls through the code map (6.4) */
#define SA1G_FN_PAD_UPDATE   0x8C050BE4u
#define SA1G_FN_EXECUTOR     0x8C0988A4u
#define SA1G_FN_CCL          0x8C0342E0u
#define SA1G_FN_LAND_UPDATE  0x8C028EC2u
#define SA1G_FN_PARTICLES    0x8C09DF64u     /* particle manager (6.4 step 6): execs, node recycling, the draw on sub 0 */
#define SA1G_FN_DISPLAY      0x8C0988C4u

/* Host input: latest state of a port; presses between pairs are ORed into the next pair. */
void sa1g_world_set_pad(sa1g_world *w, uint32_t port, const sa1g_pad *pad);
/* sa1_input (API 0.1) -> port pad (buttons to Katana bits, stick to -128..127, y < 0 forward) */
void sa1g_pad_from_input(const sa1_input *in, sa1g_pad *pad);
/* round 0.9.2 (WTR-A): sa1_input.host_flags (read only when struct_size covers it; NULL or IN_WATER without KNOWN: none)
 * into w->hostwater for the pairs of this tick, with player 0's position now (outside ticks) */
void sa1g_world_host_flags(sa1g_world *w, const sa1_input *in);

/* One pair (1/30 s): B_8C754EC4 = 2, steps with B_8C754EC8 = 0 then 1, the pad of the pair (presses
 * of the pair seen by sub 0), the press bits of per[0] / per[1] ORed across the two steps and written
 * back, then the display hook if enabled. */
sa1_status sa1g_world_pair(sa1g_world *w);
/* Accumulator: adds dt seconds and runs whole pairs (at most max_pairs, 0 = SA1G_MAX_PAIRS_PER_TICK;
 * a larger backlog is dropped and counted). */
sa1_status sa1g_world_tick(sa1g_world *w, float dt, uint32_t max_pairs, uint32_t *pairs_run);
/* One single step with an explicit sub flag (replay mode, 6.4: records carry their own sub). */
sa1_status sa1g_world_step(sa1g_world *w, int32_t sub);

/* R4 pcdata of the lowered code (pclower's sa1g_pcdata.c; sa1_world_nogame.c / build.py: none) */
uint32_t sa1g_pcd_size(void);
void sa1g_pcd_init(void);

/* Is the game code needed by the default step linked (code map has the step functions)? */
int sa1g_game_ready(void);
/* Generated per build: 1 when lowered game code is linked (sa1_world_nogame.c: 0) */
int sa1g_game_linked(void);
/* 1 when the particle manager 8C09DF64 has C in the code map (the default step runs it after CCL) */
int sa1g_particles_linked(void);
const char *sa1g_game_info(void);

/* The default step / display (6.4), callable from custom hooks */
void sa1g_step_game(sa1g_world *w, int32_t sub, void *user);
void sa1g_display_game(sa1g_world *w, void *user);
/* Call a DC function (void (void)) through the code map (a miss is a world fault) */
void sa1g_call_dc(uint32_t dc);
/* The same for an int-returning DC function (int32_t (void)) */
int32_t sa1g_call_dc_i32(uint32_t dc);

/* ---- boot, player creation, replay (sa1_world_boot.c: 6.2-6.7, 9.1 V3-V5, 11 M0/M1) -------------- */
/* globals boot and the step contract set or inject (6.5) */
#define SA1G_ADDR_RAND          0x8C5FBB08u     /* D_8C5FBB08: rand() state */
#define SA1G_ADDR_CUR_LEVEL     0x8C7493BAu     /* Sint16 CurrentLevel */
#define SA1G_ADDR_CUR_ACT       0x8C7493BCu     /* Sint16 CurrentAct */
#define SA1G_ADDR_CUR_CHAR      0x8C7493C8u     /* Sint16 CurrentCharacter */
#define SA1G_ADDR_GAME_MODE     0x8C760970u     /* Sint32 tcrf_LevelSelectVar (4: in game) */
#define SA1G_ADDR_COLDISP       0x8C752BDCu     /* Sint8 tcrf_CollisionDisplayVar (-1: debug display off) */
#define SA1G_ADDR_FORCE_MODE83  0x8C19B468u     /* Sint16 D_8C19B468 (1 forces Sonic mode 83) */
#define SA1G_ADDR_EVENT_RUNNING 0x8C7492ACu     /* B_8C7492AC */
#define SA1G_ADDR_CAMERA_TWP    0x8C18B7ECu     /* D_8C18B7EC camera_twp */
#define SA1G_ADDR_LANDTABLE     0x8C7596F4u     /* B_8C7596F4: current landtable */
#define SA1G_ADDR_LAND_MGR_TWP  0x8C7596F8u     /* B_8C7596F8: landtable manager taskwk */
#define SA1G_ADDR_LAND_LEVEL    0x8C19E970u     /* Sint8 D_8C19E970 / D_8C19E971: latched level / act */
#define SA1G_ADDR_LAND_PRESENT  0x8C19E974u     /* D_8C19E974 */
#define SA1G_ADDR_LAND_GATHER   0x8C19E978u     /* D_8C19E978: 8C052518 gathers landtable entries */
#define SA1G_ADDR_LAND_RANGE    0x8C19E96Cu     /* D_8C19E96C: gather range of player 1 (400.0f) */
#define SA1G_ADDR_LAND_ALL      0x8C196044u     /* D_8C196044: next update gathers everything */
#define SA1G_ADDR_LANDTABLES    0x8C19E9FCu     /* D_8C19E9FC[level][8]: the stages' landtables */
#define SA1G_ADDR_LAND_COUNT    0x8C754EF0u     /* B_8C754EF0: entries gathered this step */
#define SA1G_ADDR_FRAME_COUNTER 0x8C752BE8u     /* B_8C752BE8 */
#define SA1G_ADDR_RINGS         0x8C752BE4u     /* Sint16 B_8C752BE4 */
#define SA1G_ADDR_EVENT_FLAGS   0x8C799920u     /* Uint8 B_8C799920[]: event / upgrade flags */
#define SA1G_ADDR_GRAVITY       0x8C754BDCu     /* NJS_VECTOR Gravity */
#define SA1G_ADDR_PLAYER_TWP    0x8C78C608u     /* playertwp[8] */
#define SA1G_ADDR_PLAYER_MWP    0x8C78C628u     /* playermwp[8] */
#define SA1G_ADDR_PLAYER_TP     0x8C78C648u     /* playertp[8] */
#define SA1G_ADDR_PLAYER_PWP    0x8C78C668u     /* playerpwp[8] */
#define SA1G_ADDR_PAD_REC       0x8C754C08u     /* PDS_PERIPHERAL B_8C754C08[8] (0x34 each) */
#define SA1G_ADDR_ANALOG        0x8C754DCCu     /* PL_ANALOG B_8C754DCC[8] {ang, stroke} */
#define SA1G_ADDR_TASK_LISTS    0x8C79F04Cu     /* task *B_8C79F04C[8] */
#define SA1G_ADDR_CUR_EXEC      0x8C1C1D40u     /* D_8C1C1D40: exec that is running */
#define SA1G_ADDR_MATRIX_STACK  0x8C7EC8E0u     /* B_8C7EC8E0: 128 x 64 B */
#define SA1G_ADDR_HEAP          0x8C7EE8E0u     /* the game heap, 0x90000 B + end header */
#define SA1G_HEAP_SIZE          0x90010u
#define SA1G_ADDR_TAIL_FLAG2    0x8C752BDEu     /* B_8C752BDE */

/* DC functions boot and the step call through the code map */
#define SA1G_FN_INIT_MATRIX     0x8C638FE4u     /* njInitMatrix(m, n, flag) */
#define SA1G_FN_HEAP_INIT       0x8C098A10u
#define SA1G_FN_TASK_INIT       0x8C09844Au
#define SA1G_FN_CREATE_TASK     0x8C09852Eu     /* CreateElementalTask(im, level, exec) */
#define SA1G_FN_EMPTY_EXEC      0x8C098440u     /* the empty exec of a task created with exec NULL */
#define SA1G_FN_COL_INIT        0x8C028E60u
#define SA1G_FN_GRAVITY         0x8C052A8Au     /* Gravity = (0, -1, 0) */
#define SA1G_FN_PLAYER_SLOTS    0x8C06C300u     /* clear the 8 player slots */
#define SA1G_FN_POS_HISTORY     0x8C07CEF2u     /* SetPlayerPosition(pno, frame, pos, ang) */
#define SA1G_FN_TIMER_RESET     0x8C08A580u
#define SA1G_FN_OBJPOOL_CLEAR   0x8C052A00u
#define SA1G_FN_LANDMGR         0x8C0519C0u     /* landtable manager task exec */
#define SA1G_FN_LANDMGR_RESET   0x8C051A16u
#define SA1G_FN_SET_MANAGER     0x8C090FA0u     /* round 0.9.4: the SET manager task exec (sa1_world_set.c) */
#define SA1G_FN_EVENT_RUNNING   0x8C049A4Eu     /* step tail (8C04E714) */
#define SA1G_FN_LEVEL_TYPE      0x8C04C960u
#define SA1G_FN_GAME_CLOCK      0x8C08A5CCu
#define SA1G_FN_FRAME_ADVANCE   0x8C08A5BEu     /* B_8C752BE8 += B_8C754EC4 */
#define SA1G_FN_SONIC_MAIN      0x8C0A77BCu
#define SA1G_FN_DEATHZONE       0x8C07BAC0u     /* death zone task exec */
#define SA1G_FN_CCL_RESET       0x8C02F81Au     /* reset of the CCL entry counters */
#define SA1G_FN_CLOCK_ON        0x8C08A5AEu     /* D_8C1BF198 = 1 */
#define SA1G_FN_NEW_GAME        0x8C08A04Au     /* rings 0, lives 4, B_8C752BF8 0 */
#define SA1G_ADDR_SHAPE_BUFFER  0x8C89002Cu     /* NJS_POINT3 *B_8C89002C: the SDK's shape-motion buffer */
#define SA1G_SHAPE_BUFFER       0x8C782984u     /* what the game init 8C0546C0 gives njInitShape 8C60D1F2 */
#define SA1G_ADDR_INIT_FLAG     0x8C754C00u     /* Sint32 B_8C754C00 (game init, 1 in the references) */
#define SA1G_FN_CLOCK_OFF       0x8C08A5B6u     /* D_8C1BF198 = 0 */
#define SA1G_ADDR_CLOCK_SWITCH  0x8C1BF198u     /* Sint32 D_8C1BF198: the game clock 8C08A5CC counts while it is not 0 */

/* sa1g_boot_cfg.flags */
#define SA1G_BOOT_POS            0x01u  /* start at cfg pos / ang (else the game's start table, 8C04F8FA) */
#define SA1G_BOOT_NO_PLAYER      0x02u  /* no player creation (tests) */
#define SA1G_BOOT_NO_WORLD_UPDATE 0x04u /* no land collision update after creation: the cache is first built
                                           at the end of step 0, like the stage start (M1b) */
#define SA1G_BOOT_NO_CAMERA      0x08u  /* no camera adapter task (D_8C18B7EC stays NULL) */
#define SA1G_BOOT_DEATHZONE      0x10u  /* the stage start's death zone task 8C07BAC0 (DeathZoneTable of the
                                           image: the stage's own in DISC mode; kill planes come in Phase 6) */
#define SA1G_BOOT_CLOCK          0x20u  /* start the game clock (8C08A5AE: the timer, E-102's countdown);
                                           default stopped (8C08A5B6), as at the stage start of the
                                           references (the state machine 8C04CA40 starts it after the fade) */
#define SA1G_BOOT_STAGE_TASKS    0x40u  /* the stage's own tasks (sa1_world_stage.c): the level task of the stage
                                           start (8C04F506, D_8C19DE30[level]) and the level's list-2 task
                                           (D_8C19E0E0[level]) when the world's overlay image has their code, or a
                                           stand-in that creates the decompiled tasks of the level task's first
                                           exec (Emerald Coast); needs the stage overlay in the image */

typedef struct sa1g_boot_cfg {
    int32_t chr;                /* game character id: 0 Sonic, 2 Tails, 3 Knuckles, 5 Amy, 6 E-102, 7 Big */
    int16_t level, act;         /* CurrentLevel / CurrentAct (default 1 / 0 = stage 0x0100) */
    uint32_t seed;              /* rand() state (the game's srand(0xDEAD0CAB) gives 0xDEAD0CAB) */
    uint32_t flags;             /* SA1G_BOOT_* */
    float pos[3];               /* SA1G_BOOT_POS: SA1 space */
    uint32_t ang[3];            /* SA1G_BOOT_POS: BAMS */
    int32_t land_mode;          /* enum sa1g_land_mode */
    uint32_t landtable;         /* DISC: guest address (0: D_8C19E9FC[level][act] of the image) */
    uint32_t window_cap;        /* EMPTY / WINDOW: COL entries (0: 191, design 5.4) */
    uint32_t event_flags[4];    /* bits 0..127 of B_8C799920 to set (upgrades read by PInitialize) */
    uint32_t super;             /* round 6a: SA1G_SUPER_* (copied to sa1g_world.super by the boot) */
} sa1g_boot_cfg;

/* sa1g_boot_cfg.super / sa1g_world.super (round 6a, Super Sonic: src/world/sa1_world_super.c, the B_CHAOS7 level task
 * stand-in of src/world/sa1_world_stage.c, the water floor of src/world/sa1_coladapt_world.c) */
#define SA1G_SUPER_WORLD     0x01u  /* a super-capable world (kind 8, or Sonic with SA1_CREATE_SUPER_CAPABLE): the level
                                       task stand-in with the drain, sa1_character_order, the revert fix */
#define SA1G_SUPER_TRIGGER   0x02u  /* the stand-in creates the trigger 0C914D60 (kind 8; the stage's own tasks) */
#define SA1G_SUPER_PLANE     0x04u  /* the stand-in creates the sea plane 0C91514A (worlds with the stage's landtable) */
#define SA1G_SUPER_NO_DRAIN  0x08u  /* SA1_SUPER_NO_DRAIN */
#define SA1G_SUPER_KEEP      0x10u  /* SA1_SUPER_KEEP_AT_ZERO */
#define SA1G_SUPER_FAITHFUL  0x20u  /* SA1_SUPER_FAITHFUL_REVERT: no revert fix */
#define SA1G_WATER_FLOOR     0x40u  /* SA1_CREATE_WATER_FLOOR: the GRID window copies water objects as solid floors */
/* round 0.9.3a (the General form: src/world/sa1_world_super.c; sadc docs/research/super_general_design.md 4) */
#define SA1G_SUPER_GENERAL   0x80u  /* SA1_SUPER_GENERAL: the General form (a level-1 world: the stand-in made directly) */
#define SA1G_SUPER_START     0x100u /* SA1_SUPER_START: the General transformation right after the first pair */
#define SA1G_SUPER_WATER     0x200u /* SA1_SUPER_WATER_WHILE_SUPER: the water floor only while player 0 is Super */

/* defaults: Sonic, level 1 act 0, seed 0xDEAD0CAB, start table, land mode EMPTY */
void sa1g_boot_defaults(sa1g_boot_cfg *cfg);
/* Boot an applied image (6.2): runtime (njInitMatrix, screen state, rand), heap 8C098A10, tasks
 * 8C09844A, collision init 8C028E60, the globals of 6.5, the camera adapter task (6.7), the
 * landtable of cfg->land_mode, player creation (6.3), then one world update (8C028EC2) unless
 * SA1G_BOOT_NO_WORLD_UPDATE. Runs inside the world; a fault leaves the world dead. */
sa1_status sa1g_world_boot(sa1g_world *w, const sa1g_boot_cfg *cfg);
/* The character main of a game character id (0 if none in 1ST_READ) */
uint32_t sa1g_character_main(int32_t chr);
/* The stage's own tasks (sa1_world_stage.c; called by boot with SA1G_BOOT_STAGE_TASKS, inside the world) */
void sa1g_stage_tasks(sa1g_world *w);
/* CreateElementalTask(im, list, exec) through the code map (inside the world); returns the task (guest) or 0 */
uint32_t sa1g_create_task(uint16_t im, int32_t list, uint32_t exec);
/* round 6a (src/world/sa1_world_super.c; inside the world): the Super Sonic pieces of a world with SA1G_SUPER_WORLD -
 * the ring drain step of the level task stand-in (sa1_world_stage.c), and the boot's part (the revert fix's executor
 * override of the physics task 8C0B3280 unless SA1G_SUPER_FAITHFUL) */
void sa1g_super_drain(sa1g_world *w, uint32_t level_twp);
void sa1g_super_boot(sa1g_world *w);
/* round 0.9.3a (src/world/sa1_world_super.c): after each live pair of a super-capable world (sa1g_world_pair, inside the
 * world): the General form's weld set-up after the first pair, a waiting order or SA1_SUPER_START, the look following the
 * bit and its guard, the leak check; request worlds: the voice of a taken request 46 */
void sa1g_super_pair(sa1g_world *w);
/* the Perfect Chaos level-task stand-in made directly as the world's level task (a General world: no level tables;
 * src/world/sa1_world_stage.c, inside the world) */
void sa1g_stage_pc_create(sa1g_world *w);
/* The start rings of a booted world (sa1_character_desc.start_rings, before its first step; outside ticks): the game's
 * 8C08A074 (rings 0) then 8C08A198(n) with n clamped to 0..99 (no 1-up at a start) */
sa1_status sa1g_world_start_rings(sa1g_world *w, int32_t n);
/* The life counter of a booted world (round 0.9.1, sa1_character_set_lives; outside ticks): B_8C752BE0 = n, clamped to
 * 0..127 (8C08A120's cap); the game's new game 8C08A04A gave 4 */
sa1_status sa1g_world_set_lives(sa1g_world *w, int32_t n);

/* ---- round 0.9.1 HUD (src/world/sa1_world_hud.c; sadc docs/research/gsa1_070_hud.md F3 / F4 with its Review R.5) ----- */
/* the pass the bridge installs in sa1g_world.hud_fn (inside the world, between sub step 1 and the frame advance) */
void sa1g_hud_pass(sa1g_world *w);

/* ---- round 0.9.1: per-character settings of the world layer (src/world/sa1_world_charopt.c) ------------------------ */
/* the hooks the bridge installs in a character world (sa1g_world.charopt_attach / charopt_pre) */
sa1_status sa1g_charopt_attach(sa1_context *ctx, sa1g_world *w);
void sa1g_charopt_pre(sa1g_world *w, int32_t sub);
/* round 0.9.4a: the SA1_SUPERDUST block of a General world from its SA1_SUPOPT_SUPER_EFFECTS bits (sa1_world_charopt.c;
   SA1_ERR_NOT_IMPLEMENTED: the dust bit is set and the lowered code has no block) */
sa1_status sa1g_charopt_superdust(sa1g_world *w);
/* round 0.9.4 (src/world/sa1_world_set.c): the SET layer's hooks of a character world (sa1g_charopt_attach calls it
 * first, on every world create) */
void sa1g_set_attach(sa1_context *ctx, sa1g_world *w);

/* W1 (src/world/sa1_coladapt_world.c cw_window; sadc docs/research/gsa1_070_drown.md W1 with its Review R4.1): the water
 * surface above the character, kept in the character's window within the game's 128-entry gather and the polygon pool
 * with a COL radius the water probe's 200-unit gather (8C0925A0 -> 8C0522C0) reaches. Per world, for tests and
 * diagnostics; zero without a GRID window. */
typedef struct sa1g_water_stats {
    uint32_t found;             /* refreshes with a water object above the character (not E-102: no water probe) */
    uint32_t inserted;          /* ... that put it into the window (it was not among the nearest objects) */
    uint32_t replaced;          /* window entries given up for it (the farthest first, never one within 40 units) */
    uint32_t widened;           /* refreshes that raised its COL radius to reach the character's probe */
    uint32_t refused;           /* refreshes in which it did not fit (a full window of objects within 40 units) */
    uint32_t blocked;           /* refreshes that left it alone: land lies between the character and it (a dry cave or
                                   room under the water: the character is not in that water) */
    uint32_t last_index;        /* its GRID COL index at the last refresh that found one (0xFFFFFFFF: none yet) */
    uint32_t last_slot;         /* its window index then (< 128, the game's gather copies them in order) */
    float last_bottom;          /* the bottom of its box (SA1 y) */
    float last_radius;          /* the COL radius its window copy had */
} sa1g_water_stats;
void sa1ca_world_water(const sa1g_world *w, sa1g_water_stats *out);

/* RAM transplant (9.1 V4, M1a): the arena is a RAM image of the reference at the start of a step.
 * Applies the image, marks the world steppable and sets up the replay executor: tasks whose exec has
 * no native code are frozen (skipped), the landtable manager 8C0519C0 runs its mode-0 set-up without
 * its per-frame draw step 8C051D50 (a fatal stub), then is frozen; round 0.9.4: the SET manager 8C090FA0
 * (native since then; the image's stage manager is in mode 0, whose 8C091016 is a fatal stub) is frozen. */
sa1_status sa1g_world_transplant(sa1g_world *w, const void *ram, size_t n);

/* Replay executor overrides (used when w->replay or any override is set) */
int sa1g_world_xhook(sa1g_world *w, uint32_t exec, sa1g_exec_fn fn, void *user);
/* The landtable manager's first exec without 8C051D50 (an sa1g_exec_fn) */
void sa1g_exec_landmgr_setup(sa1g_world *w, uint32_t tp, void *user);
/* the task walker of the replay executor (func_8C0988A4 semantics: level 8 = 7, then 0..6) */
void sa1g_run_tasks(sa1g_world *w);

/* EMPTY / WINDOW landtable of a booted world: host pointers of the _OBJ_LANDTABLE header (ssCount
 * at +0, pLandEntry at +0xC) and of the COL array (cap entries of 0x24 B), for the collision adapter
 * (adapter_fn runs inside each step before 8C028EC2). 0 on success. */
int sa1g_world_window(sa1g_world *w, void **header, void **cols, uint32_t *cap);

/* Guest addresses of player 0's blobs (0 when absent): the task, taskwk, motionwk, playerwk, csts */
typedef struct sa1g_player_ptrs {
    uint32_t tp, twp, mwp, pwp, csts;
} sa1g_player_ptrs;
void sa1g_world_player(const sa1g_world *w, sa1g_player_ptrs *out);

/* Events: push into the current world (inside a tick; no allocation), or into a given world. */
void sa1g_event_push(uint32_t type, uint32_t id, uint32_t bank, const float pos[3], uint32_t arg);
int sa1g_world_event_push(sa1g_world *w, uint32_t type, uint32_t id, uint32_t bank, const float pos[3],
                          uint32_t arg);
/* Pop up to cap events (outside ticks); returns the number written. */
uint32_t sa1g_world_poll_events(sa1g_world *w, sa1_event *out, uint32_t cap);

/* ---- API sketch (sa1_world_api.c): the public character API of include/libsa1.h over worlds ------ */
/* World-side context: the pristine image per character kind (composed once from the disc) and the
 * static collision segment of the context's worlds. */
typedef struct sa1g_wctx {
    sa1_context *ctx;
    char gdi[1024];
    sa1g_image images[SA1_CHAR_COUNT];
    int32_t composed[SA1_CHAR_COUNT];
    sa1g_shared shared;
} sa1g_wctx;

sa1_status sa1g_wctx_init(sa1g_wctx *c, sa1_context *ctx, const char *gdi_path);
void sa1g_wctx_free(sa1g_wctx *c);
/* the pristine image of a kind, composed on first use */
sa1_status sa1g_wctx_image(sa1g_wctx *c, int32_t kind, const sa1g_image **out);

/* sa1_character_create: one world per ticking character (D2), built from the kind's image and booted
 * (6.2, 6.3: stage 0x0100, WINDOW landtable) when the game code is linked; position (host space, may be
 * NULL: the game's start table) and yaw give the start. Tikal / Super Sonic: SA1_ERR_UNSUPPORTED (6.3a). */
sa1_status sa1g_character_world_create(sa1g_wctx *c, int32_t kind, const float position[3], float yaw,
                                       sa1g_world **out);
void sa1g_character_world_destroy(sa1g_wctx *c, sa1g_world *w);
/* sa1_character_tick: input -> port A, whole pairs from dt. SA1_ERR_NOT_IMPLEMENTED until the step's game
 * code is linked (sa1g_game_ready); state_out (may be NULL) is filled from the player once it exists. */
sa1_status sa1g_character_world_tick(sa1g_world *w, const sa1_input *input, float dt, sa1_state *state_out);
/* sa1_state of player 0 (7.1): position / yaw / velocity (per second) in host space when the world has an
 * image (its context's coordinate system), SA1 space otherwise; action and frame of pwp->mj */
void sa1g_world_state(const sa1g_world *w, sa1_state *out);
/* sa1_poll_events (0.2 draft): positions converted to host space with ctx's coordinate system */
sa1_status sa1g_character_world_poll_events(const sa1g_wctx *c, sa1g_world *w, sa1_event *out,
                                            uint32_t capacity, uint32_t *count);

#endif /* SA1_WORLD_H */
