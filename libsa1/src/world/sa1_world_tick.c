/*
 * sa1_world_tick.c - the tick contract (sadc docs/libsa1_integration.md 6.4-6.6, 7.4, 8.1, decision D5).
 *
 * Steps run in pairs, as the game's frame wrapper 8C04E95A runs them: B_8C754EC4 = 2 and
 * B_8C754EC8 = 0 then 1; the pad is sampled per step, presses of the host between two pairs are
 * delivered to sub 0, and the "pressed" bits of per[0] / per[1] (B_8C754DA8[0..1] + 0x10) are ORed
 * across the two steps and written back after them. The display pass (8C0988C4) runs after sub 1 when
 * enabled. A host accumulator consumes whole pairs (1/30 s); leftover time carries over.
 *
 * One step (sa1g_step_game, replacing the asm-only 8C04E714): the camera adapter taskwk gets the host
 * camera (live mode), pad update 8C050BE4 (replay mode: on the record's raw input, then the record's
 * on/press/analog over what it computed), task executor 8C0988A4 (replay mode, or with executor
 * overrides: the world's walker sa1g_run_tasks with the same semantics, which injects the
 * player-entry state of the record), CCL 8C0342E0, the particle manager 8C09DF64 (where 8C04E714 runs it:
 * every particle's exec, the nodes of ended particles back to the free pool, released lists freed, and on sub
 * step 0 the particles drawn as game sprites 8C09E778, which the display capture records), collision adapter
 * refresh, land collision world update 8C028EC2, then the original tail (8C049A4E / 8C04C960 / game clock
 * 8C08A5CC, B_8C752BDD, B_8C752BDE). Attached character worlds also run the host-object hooks (API 0.3,
 * sa1_world_hostobj.c): w->hostobj_pre after the pad update, w->hostobj_post right after CCL; both are NULL otherwise
 * and change nothing in a world without host objects. 8C099A50 (event coroutines) and 8C06C182 (debug timer) are
 * skipped. Every call goes through the code map, so a build without the game code faults at the
 * first call; sa1g_game_ready() lets the API report SA1_ERR_NOT_IMPLEMENTED before that, and a world
 * that was not booted (sa1g_world_boot / sa1g_world_transplant) refuses the default step with
 * SA1_ERR_NOT_LOADED (its landtable, heap and tasks do not exist yet).
 *
 * Every run enters the world: MXCSR = 0xFFC0 (round toward zero, FTZ, DAZ) for the tick and the host's
 * exact value afterwards, also after a fault; no host callback, allocation or CRT maths inside.
 *
 * Live steps start with the camera adapter's view in XMTRX (sa1g_camera_view): the game's camera task leaves its view
 * there for the displays and the screen tests of the next step, and keeps the player on screen. Round 0.9.4 (sadc
 * docs/research/gsa1_094_plan.md 6.3 task 4 with its Review R.11): the view is computed first and its eye goes into the
 * camera adapter's position, which sa1g_camera_refresh then copies into the taskwk - this step's eye, so the game code
 * that reads camera_twp->pos (the SET spawner 8C091380 while Big's lure is in the water, Tails' head look 8C0BE558, Big's
 * line 8C0EC212 ...) sees the world's virtual camera instead of the SA1 origin. Replay steps are unchanged (they inject the
 * record's camera blob). Round 0.9.5 (SA1_INPUT_HOST_CAMERA, board095_cam.md 5.3): when the tick's input gave the host's
 * view, sa1g_camera_host puts the host's eye there instead (XMTRX stays the virtual view).
 *
 * Round 0.9.4 also runs the SET layer's hooks (src/world/sa1_world_set.c) of attached character worlds: w->set_pre right
 * after hostobj_pre, w->set_post right after hostobj_post; NULL otherwise.
 */
#include "sa1_world.h"
#include "sa1_coladapt.h"
#include "sa1_api_world.h"     /* round 0.9.5: sa1w_set_point (the host camera's conversion) */
#include "pc/sh4_fpmodel.h"

#include <stddef.h>
#include <string.h>

/* ---- the default step and display pass -------------------------------------------------------------- */
void sa1g_call_dc(uint32_t dc)
{
    void (*f)(void) = (void (*)(void))sa1_fn(SA1_GFN_C(dc), 0u);
    f();
}

int32_t sa1g_call_dc_i32(uint32_t dc)
{
    int32_t (*f)(void) = (int32_t (*)(void))sa1_fn(SA1_GFN_C(dc), 0u);
    return f();
}

/* live mode: the host camera into the camera adapter taskwk (6.7; replay injects the record's blob) */
static void sa1g_camera_refresh(sa1g_world *w)
{
    uint8_t *twp;
    uint32_t k;
    if (w->replay || w->camera_twp == 0) {
        return;
    }
    twp = (uint8_t *)sa1g_world_ptr(w, w->camera_twp);
    twp[1] = (uint8_t)w->camera.smode;
    for (k = 0; k < 3; k++) {
        memcpy(twp + 0x14 + 4u * k, &w->camera.ang[k], 4);
        memcpy(twp + 0x20 + 4u * k, &w->camera.pos[k], 4);
    }
}

/* Live mode: the camera adapter's view (6.7). The game's camera task (8C0197BA, not in libsa1 worlds) leaves its view
 * matrix in XMTRX, where the next step's displays find it: the sphere visibility test 8C038D00 (rt_draw.c section 6)
 * and njProjectScreen measure against it, and on the Dreamcast the camera keeps the player on screen, so his display's
 * visible path - drawing, and for Tails and Knuckles logic - runs on every sub step 0 (sadc
 * docs/research/visibility_fidelity.md). The adapter has no camera of its own; before each live step the world loads
 * the view of a camera SA1G_VIEW_DIST units behind player 0 along the host camera's forward direction and
 * SA1G_VIEW_HEIGHT above him, looking along that direction (horizontal): with camera ang.y = c (sa1_api_world.c:
 * c = host yaw - 0x4000) a forward stick moves the player along f = (-sin c, 0, -cos c), so the view's axes are x
 * right = (cos c, 0, -sin c), y down = (0, -1, 0), z forward = f, in Ninja's row-vector convention (v * M, translation
 * in m[12..14]): diag(1, -1, -1, 1) for c = 0 at the origin, njInitMatrix's start value. The player's position is then
 * at view (0, SA1G_VIEW_HEIGHT, SA1G_VIEW_DIST), a little below the screen centre as with the game's camera (Tails at
 * (0, 7.1, 38.0) in tails_sh1), well inside the frustum of the 640 x 480 screen the boot sets (sa1_world_boot.c).
 * sin / cos are the FSCA model's; the arithmetic is single precision in the world's rounding mode (deterministic on
 * every target). Without player 0 XMTRX stays as it is. Replay mode injects the record's XMTRX at player entry. */
#define SA1G_VIEW_DIST      40.0f
#define SA1G_VIEW_HEIGHT    7.0f
static void sa1g_camera_view(sa1g_world *w)
{
    uint32_t twp = 0, c;
    float p[3], e[3], ax[3][3], s, co;
    int i, j;
    if (w->replay || sa1g_world_rd32(w, SA1G_ADDR_PLAYER_TWP, &twp) != 0 || twp == 0
        || ((twp >> 24) & 31u) != SA1G_SEG_ARENA) {
        return;
    }
    memcpy(p, (const uint8_t *)sa1g_world_ptr(w, twp + 0x20u), sizeof p);
    if (!(p[0] == p[0] && p[1] == p[1] && p[2] == p[2])) {
        return;
    }
    c = w->camera.ang[1] & 0xFFFFu;
    sh4_fsca(c, &s, &co);
    ax[0][0] = co;      ax[0][1] = 0.0f;  ax[0][2] = -s;        /* x: right */
    ax[1][0] = 0.0f;    ax[1][1] = -1.0f; ax[1][2] = 0.0f;      /* y: down */
    ax[2][0] = -s;      ax[2][1] = 0.0f;  ax[2][2] = -co;       /* z: forward */
    for (i = 0; i < 3; i++) {
        e[i] = p[i] - ax[2][i] * SA1G_VIEW_DIST;
    }
    e[1] = e[1] + SA1G_VIEW_HEIGHT;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            w->xmtrx[4 * i + j] = ax[j][i];
        }
        w->xmtrx[4 * i + 3] = 0.0f;
    }
    for (j = 0; j < 3; j++) {
        w->xmtrx[12 + j] = -(e[0] * ax[j][0] + e[1] * ax[j][1] + e[2] * ax[j][2]);
    }
    w->xmtrx[15] = 1.0f;
    if (!w->set_no_eye) {
        /* round 0.9.4: the eye into the camera adapter's position (sa1g_camera_refresh copies it into the taskwk next) */
        for (j = 0; j < 3; j++) {
            w->camera.pos[j] = e[j];
        }
    }
}

/* Round 0.9.5 (SA1_INPUT_HOST_CAMERA; sadc docs/research/board095_cam.md 5.3 item 3): the host's eye of this tick into the
 * camera adapter's position, between sa1g_camera_view (XMTRX and the virtual eye: unchanged, the game's visibility tests
 * keep the virtual view) and sa1g_camera_refresh (the copy into the taskwk). Converted with the SET layer's own arithmetic
 * (sa1w_set_point: s = A^T h / scale, single precision) here, inside the world's FP mode (MXCSR 0xFFC0), so x86 and x64 give
 * the same bits. Not in replays, not with set_no_eye; a world without the latch (every reference replay, test, preview
 * world, a host without the bit) does one flag test. It does not depend on player 0 (unlike the virtual eye). */
static void sa1g_camera_host(sa1g_world *w)
{
    sa1_coords co;
    float s[3];
    if (w->replay || !w->camera.host || w->set_no_eye) {
        return;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    co.scale = w->camera.co_scale;
    memcpy(co.axis, w->camera.co_axis, sizeof co.axis);
    sa1w_set_point(&co, w->camera.host_pos, s);
    memcpy(w->camera.pos, s, sizeof w->camera.pos);
}

/* replay mode: what the pad update computed, then the record's values over it (6.6) */
static void sa1g_inject_pad(sa1g_world *w)
{
    sa1g_inject *in = &w->inject;
    uint32_t v = 0;
    sa1g_world_rd32(w, SA1G_ADDR_PAD_REC + 0x08u, &in->pad_computed_on);
    sa1g_world_rd32(w, SA1G_ADDR_PAD_REC + 0x10u, &in->pad_computed_press);
    sa1g_world_rd32(w, SA1G_ADDR_ANALOG, &in->analog_computed_ang);
    sa1g_world_rd32(w, SA1G_ADDR_ANALOG + 4u, &v);
    memcpy(&in->analog_computed_stroke, &v, 4);
    if (in->flags & SA1G_INJ_PAD) {
        sa1g_world_wr32(w, SA1G_ADDR_PAD_REC + 0x08u, in->pad_on);
        sa1g_world_wr32(w, SA1G_ADDR_PAD_REC + 0x10u, in->pad_press);
    }
    if (in->flags & SA1G_INJ_ANALOG) {
        memcpy(&v, &in->analog_stroke, 4);
        sa1g_world_wr32(w, SA1G_ADDR_ANALOG, in->analog_ang);
        sa1g_world_wr32(w, SA1G_ADDR_ANALOG + 4u, v);
    }
}

/* the tail of 8C04E714 after the land update */
static void sa1g_step_tail(sa1g_world *w)
{
    int16_t level = 0;
    uint32_t mode = 0;
    if (w->hud.hold_clock) {
        /* round 0.9.1 HUD (sa1_world_hud.c; sadc docs/research/gsa1_070_hud.md Review R.5.2): the host option that holds
           the game clock (E-102 without his time limit) clears its switch D_8C1BF198 before every clock call, so game
           code that starts the clock again (8C06132A -> 8C08A5AE) cannot run it. Never set in the reference worlds */
        *(int32_t *)SA1_GV(SA1G_ADDR_CLOCK_SWITCH) = 0;
    }
    if (sa1g_call_dc_i32(SA1G_FN_EVENT_RUNNING) == 0) {
        if (sa1g_call_dc_i32(SA1G_FN_LEVEL_TYPE) != 1) {
            sa1g_call_dc(SA1G_FN_GAME_CLOCK);
        }
        *(int8_t *)SA1_GV(SA1G_ADDR_STEP_TAIL_FLAG) = 0;
    } else {
        *(int8_t *)SA1_GV(SA1G_ADDR_STEP_TAIL_FLAG) = -1;
    }
    sa1g_world_rd32(w, SA1G_ADDR_GAME_MODE, &mode);
    memcpy(&level, (const uint8_t *)SA1_GV(SA1G_ADDR_CUR_LEVEL), 2);
    if (mode == 9 && level < 0x1A && level > 0xE) {
        *(int8_t *)SA1_GV(SA1G_ADDR_TAIL_FLAG2) = -1;
    }
}

void sa1g_step_game(sa1g_world *w, int32_t sub, void *user)
{
    (void)sub;
    (void)user;
    sa1ca_world_step_begin(w);              /* host worlds: a matrix push 8C029B00 left behind is undone (PC only) */
    sa1g_camera_view(w);                    /* round 0.9.4: first, so its eye is this step's camera position */
    sa1g_camera_host(w);                    /* round 0.9.5: the host's eye instead, when the tick latched one */
    sa1g_camera_refresh(w);
    sa1g_call_dc(SA1G_FN_PAD_UPDATE);       /* rt pdGetPeripheral, then the analog conversion 8C05117E */
    if (w->replay) {
        sa1g_inject_pad(w);
    }
    if (w->hostobj_pre != NULL) {
        w->hostobj_pre(w, sub, w->user);    /* host objects: window, proxies, latched poses (sa1_world_hostobj.c) */
    }
    if (w->set_pre != NULL) {
        w->set_pre(w, sub, w->user);        /* round 0.9.4: SET objects: window, forced state, manager (sa1_world_set.c) */
    }
    if (w->face.pre != NULL) {
        w->face.pre(w, sub, w->user);       /* round 0.9.4a: latched face calls (sa1_world_face.c) */
    }
    if (w->emote.pre != NULL) {
        w->emote.pre(w, sub, w->user);      /* round 0.9.4a: emote start / input cancel (sa1_world_emote.c) */
    }
    if (w->board.pre != NULL) {
        w->board.pre(w, sub, w->user);      /* round 0.9.5: the latched board request (sa1_world_board.c) */
    }
    /* 8C099A50 (event coroutines) and 8C06C182 (debug timer) are skipped */
    if (w->replay || w->num_xhooks != 0 || w->freeze_unknown) {
        sa1g_run_tasks(w);                  /* the same walk, with player-entry injection and overrides */
    } else {
        sa1g_call_dc(SA1G_FN_EXECUTOR);     /* list 7 as level 8, then levels 0..6 */
    }
    sa1g_call_dc(SA1G_FN_CCL);
    if (w->hostobj_post != NULL) {
        w->hostobj_post(w, sub, w->user);   /* host objects: the hits of this pass (sa1_world_hostobj.c) */
    }
    if (w->set_post != NULL) {
        w->set_post(w, sub, w->user);       /* round 0.9.4: SET objects: the touch latch, the events (sa1_world_set.c) */
    }
    if (w->emote.post != NULL) {
        w->emote.post(w, sub, w->user);     /* round 0.9.4a: the state cancel, the end (sa1_world_emote.c) */
    }
    if (w->face.post != NULL) {
        w->face.post(w, sub, w->user);      /* round 0.9.4a (unused today; kept symmetric) */
    }
    if (w->board.post != NULL) {
        w->board.post(w, sub, w->user);     /* round 0.9.5: edges, the kept board, the riding word (sa1_world_board.c) */
    }
    /* the particle manager, after CCL as in 8C04E714 (its debug print of tcrf_SprTaskDisplayMode and the timer
     * resets 8C06C182 around it are left out; the manager's own timer reads are the emulated TMU's) */
    if (sa1g_particles_linked()) {
        sa1g_call_dc(SA1G_FN_PARTICLES);
    }
    if (w->adapter_fn != NULL) {
        w->adapter_fn(w, w->user);          /* window landtable, moving objects (5.4, 5.5) */
    }
    sa1g_call_dc(SA1G_FN_LAND_UPDATE);      /* calls the landtable gather 8C052518 */
    sa1g_step_tail(w);
}

/* the particle manager has C in this build (a --lower subset may leave it out: then it is not run) */
int sa1g_particles_linked(void)
{
    static int linked = -1;
    if (linked < 0) {
        linked = sa1g_game_linked() && sa1_fn_name(SA1G_FN_PARTICLES) != NULL;
    }
    return linked;
}

void sa1g_display_game(sa1g_world *w, void *user)
{
    (void)w;
    (void)user;
    sa1g_call_dc(SA1G_FN_DISPLAY);
}

int sa1g_game_ready(void)
{
    return sa1g_game_linked() && sa1_fn_name(SA1G_FN_PAD_UPDATE) != NULL && sa1_fn_name(SA1G_FN_EXECUTOR) != NULL
           && sa1_fn_name(SA1G_FN_CCL) != NULL && sa1_fn_name(SA1G_FN_LAND_UPDATE) != NULL;
}

/* ---- input (6.6) ------------------------------------------------------------------------------------- */
static int16_t sa1g_stick(float v)
{
    float s;
    if (!(v == v)) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    if (v < -1.0f) {
        v = -1.0f;
    }
    s = v * 127.0f;
    return (int16_t)(s >= 0.0f ? s + 0.5f : s - 0.5f);
}

void sa1g_pad_from_input(const sa1_input *in, sa1g_pad *pad)
{
    sa1_input v;
    memset(pad, 0, sizeof *pad);
    if (in == NULL || in->struct_size < sizeof(uint32_t)) {
        return;
    }
    memset(&v, 0, sizeof v);
    memcpy(&v, in, in->struct_size < sizeof v ? in->struct_size : sizeof v);
    pad->connected = 1;
    if (v.buttons & SA1_BUTTON_JUMP) {
        pad->on |= SA1G_PAD_A;
    }
    if (v.buttons & SA1_BUTTON_ACTION) {
        pad->on |= SA1G_PAD_B;
    }
    if (v.buttons & SA1_BUTTON_ACTION2) {
        pad->on |= SA1G_PAD_Y;
    }
    if (v.buttons & SA1_BUTTON_START) {
        pad->on |= SA1G_PAD_START;
    }
    pad->l = (v.buttons & SA1_BUTTON_L) ? 255u : 0u;
    pad->r = (v.buttons & SA1_BUTTON_R) ? 255u : 0u;
    pad->x1 = sa1g_stick(v.stick_x);
    pad->y1 = sa1g_stick(-v.stick_y);       /* +y forward on the host, y < 0 forward on the pad */
}

void sa1g_world_set_pad(sa1g_world *w, uint32_t port, const sa1g_pad *pad)
{
    if (w == NULL || port >= SA1G_PORTS) {
        return;
    }
    if (pad == NULL) {
        memset(&w->host_pad[port], 0, sizeof w->host_pad[port]);
        return;
    }
    w->taps[port] |= pad->on & ~w->host_pad[port].on;     /* presses since the last pair are kept */
    w->host_pad[port] = *pad;
}

/* round 0.9.2 (WTR-A; sadc docs/research/gsa1_090_water.md WTR-A with its Review R3.5): the host's water at the character
 * for the pairs of this tick (sa1_input.host_flags, struct_size gated; IN_WATER means nothing without KNOWN), and player 0's
 * taskwk.pos now: the state after the previous tick, where the host tested its water (the collision window keeps the water
 * the character comes down into during the tick, sa1_coladapt_world.c cw_water_dry). Read only: nothing of the game
 * changes; without KNOWN the world runs exactly as before. */
void sa1g_world_host_flags(sa1g_world *w, const sa1_input *in)
{
    sa1g_hostwater *h;
    sa1g_player_ptrs pp;
    uint32_t f = 0, k, v;
    if (w == NULL) {
        return;
    }
    h = &w->hostwater;
    if (in != NULL && in->struct_size >= offsetof(sa1_input, host_flags) + sizeof in->host_flags
        && (in->host_flags & SA1_INPUT_HOST_WATER_KNOWN)) {
        f = in->host_flags & (SA1_INPUT_HOST_WATER_KNOWN | SA1_INPUT_HOST_IN_WATER);
    }
    h->flags = f;
    h->ref_valid = 0;
    if (f == 0 || w->ram == NULL) {
        return;
    }
    sa1g_world_player(w, &pp);
    if (pp.twp == 0) {
        return;
    }
    for (k = 0; k < 3; k++) {
        if (sa1g_world_rd32(w, pp.twp + 0x20u + 4u * k, &v) != 0) {
            return;
        }
        memcpy(&h->ref[k], &v, 4);
        if (!(h->ref[k] == h->ref[k]) || h->ref[k] > 1e30f || h->ref[k] < -1e30f) {
            return;
        }
    }
    h->ref_valid = 1;
}

/* what rt's pdGetPeripheral sees for this step: the host state, plus the pair's presses on sub 0;
 * replay mode: the record's raw input on port A (a standard controller, like the reference
 * simulator's pad HLE), nothing elsewhere */
static void sa1g_latch_sub(sa1g_world *w, int32_t sub)
{
    uint32_t p;
    if (w->replay) {
        memset(w->pad, 0, sizeof w->pad);
        w->pad[0].connected = 1;
        w->pad[0].on = w->inject.buttons;
        w->pad[0].x1 = w->inject.stick_x;
        w->pad[0].y1 = w->inject.stick_y;
        return;
    }
    for (p = 0; p < SA1G_PORTS; p++) {
        w->pad[p] = w->host_pad[p];
        if (sub == 0 && w->pad[p].connected) {
            w->pad[p].on |= w->taps[p];
        }
    }
}

/* press bits of per[0] / per[1] (records the pad update stored in B_8C754DA8), arena-bounded */
static uint32_t sa1g_press_addr(const sa1g_world *w, uint32_t k)
{
    uint32_t rec = 0;
    if (sa1g_world_rd32(w, SA1G_ADDR_PAD_PTRS + 4u * k, &rec) != 0 || rec == 0
        || ((rec >> 24) & 31u) != SA1G_SEG_ARENA) {
        return 0;
    }
    return rec + 0x10u;
}

static void sa1g_press_collect(const sa1g_world *w, uint32_t acc[2])
{
    uint32_t k, v;
    for (k = 0; k < 2; k++) {
        uint32_t a = sa1g_press_addr(w, k);
        if (a != 0 && sa1g_world_rd32(w, a, &v) == 0) {
            acc[k] |= v;
        }
    }
}

static void sa1g_press_writeback(sa1g_world *w, const uint32_t acc[2])
{
    uint32_t k;
    for (k = 0; k < 2; k++) {
        uint32_t a = sa1g_press_addr(w, k);
        if (a != 0) {
            sa1g_world_wr32(w, a, acc[k]);
        }
    }
}

/* ---- events (7.4) ------------------------------------------------------------------------------------ */
int sa1g_world_event_push(sa1g_world *w, uint32_t type, uint32_t id, uint32_t bank, const float pos[3], uint32_t arg)
{
    sa1g_events *q;
    sa1_event *e;
    if (w == NULL) {
        return -1;
    }
    q = &w->events;
    if (q->count >= SA1G_EVENT_QUEUE) {
        q->dropped++;
        return -1;
    }
    e = &q->ev[(q->head + q->count) % SA1G_EVENT_QUEUE];
    q->count++;
    memset(e, 0, sizeof *e);
    e->type = type;
    e->id = id;
    e->bank = bank;
    if (pos != NULL) {
        e->position[0] = pos[0];
        e->position[1] = pos[1];
        e->position[2] = pos[2];
    }
    e->arg = arg;
    e->step = (uint32_t)w->steps;
    return 0;
}

void sa1g_event_push(uint32_t type, uint32_t id, uint32_t bank, const float pos[3], uint32_t arg)
{
    sa1g_world_event_push(sa1g_world_current(), type, id, bank, pos, arg);
}

uint32_t sa1g_world_poll_events(sa1g_world *w, sa1_event *out, uint32_t cap)
{
    sa1g_events *q;
    uint32_t n = 0;
    if (w == NULL || out == NULL) {
        return 0;
    }
    q = &w->events;
    while (n < cap && q->count > 0) {
        out[n++] = q->ev[q->head];
        q->head = (q->head + 1u) % SA1G_EVENT_QUEUE;
        q->count--;
    }
    return n;
}

/* ---- pairs, steps, accumulator ------------------------------------------------------------------------- */
static sa1_status sa1g_pre_run(const sa1g_world *w)
{
    if (w == NULL || w->reserve == NULL || w->step == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    if (w->step == sa1g_step_game && !sa1g_game_ready()) {
        return SA1_ERR_NOT_IMPLEMENTED;     /* sa1_character_tick until the game code is linked */
    }
    if (w->step == sa1g_step_game && !w->booted) {
        return SA1_ERR_NOT_LOADED;          /* no heap, tasks or landtable yet: sa1g_world_boot first */
    }
    return SA1_OK;
}

static void sa1g_fault_event(sa1g_world *w)
{
    sa1g_world_event_push(w, w->fault.kind == SA1G_FAULT_UNIMPL ? SA1_EVENT_UNIMPLEMENTED : SA1_EVENT_FAULT,
                          w->fault.guest, 0, NULL, w->fault.dc_func);
}

/* The drawn step (sa1_world.h drawn_*; sadc docs/research/gsa1_vertical_flicker.md fix B): player 0's taskwk.pos (+0x20)
 * and ang (+0x14) right after sub step 0, whose display the character renderer 8C037098 draws. Recorded at the end of the
 * step, not at the renderer's first record (rt_draw.c): CCL, the particle manager and the land update run after the
 * player's display and do not move the player in a character world (tests/flicker_test.c: the drawn root node is this
 * root + the display centre within 0.001 HU in Sonic's fall pose up to 1586 HU/s and in the jump ball; riding moving
 * collision objects UNVERIFIED). Reads only (no FP arithmetic, no guest write): the game state and the replays do not
 * change. */
static void sa1g_note_drawn(sa1g_world *w)
{
    sa1g_player_ptrs pp;
    uint32_t k, v;
    sa1g_world_player(w, &pp);
    w->drawn_valid = 0;
    w->drawn_frame_valid = 0;
    if (pp.twp == 0) {
        return;
    }
    for (k = 0; k < 3; k++) {
        if (sa1g_world_rd32(w, pp.twp + 0x20u + 4u * k, &v) != 0) {
            return;
        }
        memcpy(&w->drawn_pos[k], &v, 4);
        if (sa1g_world_rd32(w, pp.twp + 0x14u + 4u * k, &w->drawn_ang[k]) != 0) {
            return;
        }
    }
    /* round 0.9.4a: the motion frame and action the character renderer drew (sa1_world.h drawn_frame); mj at 0x120 */
    if (pp.pwp != 0 && sa1g_world_rd32(w, pp.pwp + 0x130u, &v) == 0) {
        uint32_t ra;
        memcpy(&w->drawn_frame, &v, 4);
        if (sa1g_world_rd32(w, pp.pwp + 0x124u, &ra) == 0) {
            w->drawn_action = ra >> 16;         /* mj.action (0x126; 0x124 is mj.reqaction) */
            w->drawn_frame_valid = 1;
        }
    }
    w->drawn_step = w->steps;
    w->drawn_valid = 1;
}

sa1_status sa1g_world_pair(sa1g_world *w)
{
    volatile sa1_status st = sa1g_pre_run(w);
    uint32_t press[2] = { 0, 0 };
    int32_t sub;
    if (st != SA1_OK) {
        return st;
    }
    sa1g_tick_lock();
    sa1g_world_enter(w);
    sa1g_world_stack_open(w, (uintptr_t)__builtin_frame_address(0));
    if (SA1G_WORLD_TRY(w)) {
        sa1g_world_wr32(w, SA1G_ADDR_STEPS_PER_FRAME, 2u);
        for (sub = 0; sub < 2; sub++) {
            sa1g_world_wr32(w, SA1G_ADDR_SUB_STEP, (uint32_t)sub);
            sa1g_latch_sub(w, sub);
            w->step(w, sub, w->user);
            w->steps++;
            if (sub == 0) {
                sa1g_note_drawn(w);
            }
            sa1g_press_collect(w, press);
        }
        sa1g_press_writeback(w, press);
        memset(w->taps, 0, sizeof w->taps);
        if ((w->super & SA1G_SUPER_WORLD) && w->step == sa1g_step_game && !w->replay) {
            /* round 0.9.3a (sa1_world_super.c): the General form after each pair (the weld set-up, a waiting order, the
               look following the bit), the voice of a taken request 46 in request worlds; nothing else in kind-8 worlds */
            sa1g_super_pair(w);
        }
        if (w->hud_fn != NULL && w->step == sa1g_step_game && !w->replay) {
            /* round 0.9.1 HUD (sa1_world_hud.c): the game's HUD calls, where its frame steps 8C04E95A / 8C04EA4A make
               them: after both logic steps, before the frame advance (the capture keeps one frame counter per pair) */
            w->hud_fn(w);
        }
        if (w->step == sa1g_step_game && !w->replay) {
            sa1g_call_dc(SA1G_FN_FRAME_ADVANCE);    /* B_8C752BE8 += 2 per pair (6.4) */
        }
        if (w->display && w->display_fn != NULL) {
            w->display_fn(w, w->user);
        }
        w->pairs++;
        sa1g_world_disarm();
    } else {
        st = w->dead_status;
    }
    sa1g_world_stack_close(w);
    sa1g_world_leave(w);
    sa1g_tick_unlock();
    if (st != SA1_OK) {
        sa1g_fault_event(w);
    }
    return st;
}

sa1_status sa1g_world_step(sa1g_world *w, int32_t sub)
{
    volatile sa1_status st = sa1g_pre_run(w);
    if (st != SA1_OK) {
        return st;
    }
    if (sub != 0 && sub != 1) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    sa1g_tick_lock();
    sa1g_world_enter(w);
    sa1g_world_stack_open(w, (uintptr_t)__builtin_frame_address(0));
    if (SA1G_WORLD_TRY(w)) {
        sa1g_world_wr32(w, SA1G_ADDR_STEPS_PER_FRAME, 2u);
        sa1g_world_wr32(w, SA1G_ADDR_SUB_STEP, (uint32_t)sub);
        sa1g_latch_sub(w, sub);
        w->step(w, sub, w->user);
        w->steps++;
        if (sub == 0) {
            sa1g_note_drawn(w);
        }
        if (sub == 1) {
            memset(w->taps, 0, sizeof w->taps);
        }
        sa1g_world_disarm();
    } else {
        st = w->dead_status;
    }
    sa1g_world_stack_close(w);
    sa1g_world_leave(w);
    sa1g_tick_unlock();
    if (st != SA1_OK) {
        sa1g_fault_event(w);
    }
    return st;
}

sa1_status sa1g_world_tick(sa1g_world *w, float dt, uint32_t max_pairs, uint32_t *pairs_run)
{
    sa1_status st;
    uint32_t n = 0;
    if (pairs_run != NULL) {
        *pairs_run = 0;
    }
    if (w == NULL || !(dt >= 0.0f) || dt > 3600.0f) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1g_pre_run(w);
    if (st != SA1_OK) {
        return st;
    }
    if (max_pairs == 0) {
        max_pairs = SA1G_MAX_PAIRS_PER_TICK;
    }
    w->acc += (double)dt;
    while (w->acc + 1e-9 >= SA1G_PAIR_SECONDS && n < max_pairs) {
        st = sa1g_world_pair(w);
        if (st != SA1_OK) {
            break;
        }
        w->acc -= SA1G_PAIR_SECONDS;
        n++;
    }
    if (st == SA1_OK && w->acc + 1e-9 >= SA1G_PAIR_SECONDS) {
        uint32_t lost = (uint32_t)((w->acc + 1e-9) / SA1G_PAIR_SECONDS);
        w->backlog_drops += lost;
        w->acc -= (double)lost * SA1G_PAIR_SECONDS;
    }
    if (w->acc < 0.0) {
        w->acc = 0.0;
    }
    if (pairs_run != NULL) {
        *pairs_run = n;
    }
    return st;
}
