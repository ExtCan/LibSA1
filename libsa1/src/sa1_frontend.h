/*
 * sa1_frontend.h - the game's own front end (API SA1_API_FRONTEND, round 0.9.8; include/libsa1.h "the game's own front end";
 * sadc docs/research/gsa1_098_plan.md 6.5, charsel098.md 5). Internal: not part of the public API.
 *
 *   src/sa1_frontend.c              the API: one open per context, the frames, the host's pad (presses, the dialog skip's held
 *                                   keys), the events, the capture window and the render output
 *   src/world/sa1_world_frontend.c  the world side: the image, the boot, the menu's texlists, the game's init, one frame
 *   src/sa1_screen.c                sa1_screen from the capture's kinds 14 and 15 (sa1_screen_fe_*)
 *   src/sa1_loader.c                the menu's nine archives (SA1_KIND_FRONTEND, ctx->frontend_set)
 *
 * Game thread only, never inside a character's tick (libsa1.h): the front end is outside the parallel contract, its rt is not
 * (rt_draw.c's 8C63ED08 is reached from E-102's effect display on a pool worker: its state lies in the world's pcdata).
 * Nothing here holds a writable process static. Only opaque world types cross this header.
 */
#ifndef SA1_FRONTEND_H
#define SA1_FRONTEND_H

#include "sa1_internal.h"
#include "sa1_capture.h"

struct sa1g_world;

/* ---- the menu's archives (the game's own list: D_8C94263C[1], D_8C942628[language]) ---- */
typedef struct sa1fe_archive {
    char name[32];              /* the disc file without ".PRS", upper case */
    uint32_t texlist;           /* the guest NJS_TEXLIST the game draws it with */
} sa1fe_archive;

/* src/sa1_loader.c: ctx->frontend_set holds exactly these archives after a success (kept until the disc is unloaded; loaded again
 * only when the list differs: another language). SHA-1 checked against the manifest */
sa1_status sa1_loader_frontend_set(sa1_context *ctx, const char *gdi_path, const sa1fe_archive *list, uint32_t n);

/* ---- the screen source (src/sa1_screen.c) ---- */
struct sa1s_fe;
/* set: the archives whose global indices the draws name (the sorted map is made here) */
sa1_status sa1_screen_fe_create(sa1_context *ctx, const sa1_charset *set, struct sa1s_fe **out);
void sa1_screen_fe_free(sa1_context *ctx, struct sa1s_fe *s);
/* the screen of the capture's snapshot; drew 0: an empty screen (the last frame drew nothing) */
sa1_status sa1_screen_fe_build(sa1_context *ctx, struct sa1s_fe *s, sa1_capture *c, int drew, const sa1_screen **out);
/* draws whose global index is in no archive (a counted error: frontend_test requires 0), over the front end's life */
uint32_t sa1_screen_fe_misses(const struct sa1s_fe *s);

#if !defined(SA1_NO_WORLD)
/* ---- the world side (src/world/sa1_world_frontend.c; game thread) ---- */
/* the Katana bits of the pad (rt_input.c) */
#define SA1FW_PAD_LEFT  0x40u
#define SA1FW_PAD_RIGHT 0x80u
#define SA1FW_PAD_UP    0x10u
#define SA1FW_PAD_DOWN  0x20u
#define SA1FW_PAD_A     0x04u
#define SA1FW_PAD_B     0x02u
#define SA1FW_PAD_START 0x08u

/* what one frame gets */
typedef struct sa1fw_pad {
    uint32_t on;                /* Katana bits */
    int16_t x1, y1;             /* the stick, -128..127, y < 0 = forward */
    int32_t confirm;            /* 1: a new A or START press in `on`: the dialog skip's test (no SA1_FRONTEND_SHOW_DIALOG) */
} sa1fw_pad;

/* what the game's state says after a frame */
typedef struct sa1fw_info {
    int32_t select_alive;       /* the screen's task D_8C9613A4 and its work exist */
    int32_t proc, slot, timer, choice;
    int32_t slots;              /* 8C901400: 6 or 7 */
    int32_t master_alive;       /* the master task D_8C9610CC and its work exist */
    int32_t mode, result;       /* the master's current screen and result */
    int32_t frame_result;       /* what 8C9001DA returned */
    int32_t skipped;            /* the dialog skip ran in this frame (the press was withheld, choice 0 and proc 8 written) */
    int32_t ring;               /* round 1.0.0-beta1: ADV_SELWK.ring (+0x18), the name's pulse phase */
} sa1fw_info;

/* The world: image (1ST_READ + ADVERTISE.PRS), boot, the archives (sa1_loader_frontend_set), the texlists, the game's init with the
 * desc's flags. No frame ran yet. */
sa1_status sa1fw_create(sa1_context *ctx, const sa1_frontend_desc *d, struct sa1g_world **out);
void sa1fw_destroy(sa1_context *ctx, struct sa1g_world *w);
/* One frame of 1/60 s: the view, the pad, 8C9001DA. Not SA1_OK: the world is dead (sa1fw_fault names it) */
sa1_status sa1fw_frame(struct sa1g_world *w, const sa1fw_pad *pad, sa1fw_info *info);
/* The game's state now, outside a frame */
void sa1fw_read(const struct sa1g_world *w, sa1fw_info *info);
/* the world's queued events, oldest first; returns the count (< cap: none left) */
uint32_t sa1fw_poll(struct sa1g_world *w, sa1_event *out, uint32_t cap);
const char *sa1fw_fault(const struct sa1g_world *w);
/* the select slot's main (CharSelData 0x8C9101F8 + 16 slot), slot 6's 8C9027A6 */
uint32_t sa1fw_slot_exec(const struct sa1g_world *w, int32_t slot);
/* SA1_CHAR_* of the slot (the game's D_8C90EC6A), SA1_KIND_FRONTEND for slot 6 */
int32_t sa1fw_slot_kind(const struct sa1g_world *w, int32_t slot);
/* the capture block's frame word: the menu's frame counter stands still, so every window restarts the buffer (charsel098 3.3 item 3) */
void sa1fw_reset_cap_frame(struct sa1g_world *w, uint32_t block);
#endif

/* ---- tests (tests/frontend_test.c) ---- */
typedef struct sa1fe_test_info {
    uint32_t screen_misses;     /* sa1_screen_fe_misses */
    uint32_t events_other;      /* the world's events that are not SOUND / VOICE / JINGLE (left out of sa1_frontend_poll) */
    uint32_t events_lost;       /* events dropped: nobody polled (queue of 512) */
    uint32_t draws_frame;       /* the capture's kinds 14 + 15 in the last frame */
    uint32_t cap_block;         /* guest address of the capture block */
    sa1_capture *cap;
    struct sa1g_world *world;
} sa1fe_test_info;
void sa1fe_test_get(const sa1_frontend *fe, sa1fe_test_info *out);
/* 1: frames keep running after EV_DONE / EV_CANCEL (the references go on after the master's result; the game's main loop leaves
 * there and so does a tick, frames_run 0, without this) */
void sa1fe_test_run_on(sa1_frontend *fe, int32_t on);

#endif /* SA1_FRONTEND_H */
