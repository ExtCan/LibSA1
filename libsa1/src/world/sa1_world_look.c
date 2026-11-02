/*
 * sa1_world_look.c - the head look inside a character world (SA1_API_LOOK, round 0.9.8; sadc docs/research/gsa1_098_plan.md 6.3
 * part 2, gsa1_096_plan.md 6.2, look096.md 7.1). The API side (arguments, kinds, building the world) is src/sa1_look.c; the two
 * meet through src/world/sa1_world_look.h (sa1wl_*).
 *
 * THE MECHANISM - what the game's objects and cutscenes do: the setters 8C07C318 (point) / 8C07C34C (angles) / 8C07C376 (off) take
 * a player number and write the event look of that player (eventwk +0x04 look_mode, +0x08 look_ang, +0x14 look_pos), which the
 * look tasks (Sonic 8C0B28EE, Tails 8C0BE1CE, Knuckles 8C0C8A80, Amy 8C0DDA00, Big 8C0E5DF0) read in their mode 3; the Z button's
 * bit 0x100 in B_8C754C08[pno].on makes them look at camera_twp->pos (mode 2, also Tails' flight model task 8C0BE558). The lane
 * only calls those setters and ORs that bit; the heads turn in the game's own code.
 *
 * The lane (look096.md 7.1.2-7.1.3): installed by the first sa1_character_look (or option 7) of a world; a world that never gets one
 * runs exactly as before. The latch is host memory (w->look.persist[]) and survives an image rebuild; the block in segment 0x18
 * (the setter's argument, what LIB wrote last, the look task, the Super nodes) is made again after one, validated by magic and self
 * as sa1_world_set.c's st_state.
 *
 * The step (pre, every live step, right after the emote lane's pre: the pad update has run, so its old / press bookkeeping never
 * sees the Z bit), on the world's character slot w->slot (0, follower worlds 1), through sa1g_world_slot_player:
 *   no player: owned = 0, nothing
 *   owned and the event look differs from what LIB wrote last: the game set or cleared it, owned = 0
 *   POINT / ANGLES: unless the game has a look (not owned, look_mode != 0; SA1_LOOKF_OVERRIDE_GAME writes over it): the setter, in the
 *                   world's FP mode (sa1w_set_point with the context's coords, as 0.9.5's camera: x86 = x64), then the record; owned = 1
 *   CAMERA:         if owned: 8C07C376, owned = 0 (mode 3 would win over mode 2); on |= 0x100
 *   OFF:            if owned: 8C07C376, owned = 0
 * Game calls go through the code map (sa1_fn(SA1_GFN_C(addr), 0): a --no-game build links), as sa1_world_board.c.
 *
 * The Z bit of slot 0 is gone at the next pad update. A follower's slot 1 is written by the AI with read-modify-write
 * (code_8C082160.c:94-96, 213-215), so the bit could stay: the step clears it once when CAMERA ends (S->z_set), on every slot.
 *
 * The fill (S5; outside the world, possibly on a worker: SA1_API_PARALLEL, review R.17): reads the arena through the world's own
 * accessors, never sa1_decode / SA1_GV. The look task is found in pre (sub step 0) by its exec and slot and revalidated (exec,
 * counter.b[0] == slot) on every read; a free look task's memory is reused.
 *
 * Option 7 (SA1_COPT_SUPER_LOOK, deviation D1 of look096.md 7, default 0: the game; the game's own precedent is the Light Speed aura's
 * head, code_8C0B2890.c:296-298): post, right after the emote lane's post, copies ang[0..2] of the look work's head / eyeL / eyeR objects
 * (+0x58, +0x5C, +0x60) onto the Super body's nodes 11 / 21 / 17, found by walking the Super body 0C909D2C (8C07B080's order: the node,
 * its child subtree, its sibling chain); the angles they held are saved when the copy starts and written back when it ends.
 */
#include "sa1_world.h"
#include "sa1_api_world.h"
#include "sa1_world_look.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LK_MAGIC            0x4B4F4F4Cu     /* "LOOK" */
#define LK_FN_POINT         0x8C07C318u     /* func_8C07C318(Uint8 pno, NJS_POINT3 *pos) */
#define LK_FN_ANGLES        0x8C07C34Cu     /* func_8C07C34C(Uint8 pno, Angle3 *ang) */
#define LK_FN_OFF           0x8C07C376u     /* func_8C07C376(Uint8 pno) */
#define LK_ADDR_TASK_LISTS  0x8C79F04Cu     /* the executor's eight lists */
#define LK_TWP_EWP          0x3Cu           /* taskwk.ewp */
#define LK_EWP_MODE         0x04u           /* eventwk.look_mode (Uint8) */
#define LK_EWP_ANG          0x08u           /* eventwk.look_ang */
#define LK_EWP_POS          0x14u           /* eventwk.look_pos */
#define LK_EWP_TP           0x20u           /* eventwk.look_tp */
#define LK_PAD_ON           0x08u           /* PDS_PERIPHERAL.on */
#define LK_PAD_STRIDE       0x34u
#define LK_PAD_Z            0x100u          /* the Shinobi pad's Z button: only the look tasks read it (look096.md 3.3) */
#define LK_TP_EXEC          0x10u           /* task.exec */
#define LK_TP_TWP           0x20u           /* task.twp */
#define LK_TP_LWP           0x24u           /* task.mwp: the look work */
#define LK_TWP_PNO          0x08u           /* taskwk.counter.b[0] */
#define LK_LWP_HEAD_Y       0x08u
#define LK_LWP_HEAD_Z       0x0Cu
#define LK_LWP_EYEL_Y       0x18u
#define LK_LWP_EYEL_Z       0x1Cu
#define LK_LWP_NODES        0x58u           /* head, eyeL, eyeR objects */
#define LK_OBJ_ANG          0x14u           /* NJS_OBJECT.ang */
#define LK_OBJ_CHILD        0x2Cu
#define LK_OBJ_SIBLING      0x30u
#define LK_SUPER_BODY       0x0C909D2Cu     /* the Super body's tree (B_CHAOS7), 73 nodes, as Sonic's */

/* persist[]: what survives an image rebuild */
enum { LK_P_KIND = 0, LK_P_FLAGS, LK_P_A, LK_P_B, LK_P_C, LK_P_SUPER };

typedef struct lk_state {
    uint32_t magic, self;
    float arg_pos[3];           /* the setter's NJS_POINT3 (segment 0x18, a guest-visible block) */
    int32_t arg_ang[3];         /* the setter's Angle3 */
    int32_t owned;              /* the event look is the one LIB wrote */
    uint32_t rec_mode;          /* what LIB wrote last */
    uint32_t rec_ang[3], rec_pos[3], rec_tp;
    uint32_t task;              /* the slot's look task (0: not found yet) */
    int32_t z_set;              /* the Z bit was ORed in the last step */
    int32_t super_on;           /* the copy runs: sup_node / sup_saved hold */
    uint32_t sup_node[3];
    uint32_t sup_saved[3][3];
    uint32_t setter_calls, z_steps; /* bookkeeping (tests) */
} lk_state;

/* ---- arena access (never sa1_decode / SA1_GV outside the world: review R.17) ---------------------------------------- */
static uint32_t lk_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (g == 0 || sa1g_world_rd32(w, g, &v) != 0) {
        return 0;
    }
    return v;
}

static int lk_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static uint32_t lk_rd8(const sa1g_world *w, uint32_t g)
{
    return (lk_rd32(w, g & ~3u) >> (8u * (g & 3u))) & 0xFFu;
}

/* a BAMS word as the signed 16-bit angle the look tasks mean (they keep it in an Angle, after 0x10000 turns too) */
static int32_t lk_ang16(uint32_t a)
{
    a &= 0xFFFFu;
    return a >= 0x8000u ? (int32_t)a - 0x10000 : (int32_t)a;
}

/* ---- the block ------------------------------------------------------------------------------------------------------- */
static lk_state *lk_state_of(const sa1g_world *w)
{
    uint32_t g;
    lk_state *S;
    if (w == NULL || w->ext == NULL || w->look.state == 0) {
        return NULL;
    }
    g = w->look.state;
    if (((g >> 24) & 31u) != SA1G_SEG_EXT || (g & 15u) != 0 || (size_t)(g & 0xFFFFFFu) + sizeof(lk_state) > w->ext_used) {
        return NULL;
    }
    S = (lk_state *)(void *)(w->ext + (g & 0xFFFFFFu));
    return S->magic == LK_MAGIC && S->self == g ? S : NULL;
}

static lk_state *lk_state_make(sa1g_world *w)
{
    lk_state *S = lk_state_of(w);
    uint32_t g;
    if (S != NULL) {
        return S;
    }
    g = sa1g_world_ext_alloc(w, sizeof(lk_state), 16, (void **)&S);
    if (g == 0 || S == NULL) {
        return NULL;
    }
    memset(S, 0, sizeof *S);
    S->magic = LK_MAGIC;
    S->self = g;
    w->look.state = g;
    return S;
}

/* ---- the look task of the slot -------------------------------------------------------------------------------------- */
static int lk_look_exec(uint32_t exec)
{
    exec &= 0x1FFFFFFFu;
    return exec == 0x0C0B28EEu || exec == 0x0C0BE1CEu || exec == 0x0C0C8A80u || exec == 0x0C0DDA00u || exec == 0x0C0E5DF0u;
}

static int lk_task_ok(const sa1g_world *w, uint32_t tp, uint32_t slot)
{
    uint32_t twp;
    if (!lk_arena(tp) || !lk_look_exec(lk_rd32(w, tp + LK_TP_EXEC))) {
        return 0;
    }
    twp = lk_rd32(w, tp + LK_TP_TWP);
    return lk_arena(twp) && lk_rd8(w, twp + LK_TWP_PNO) == slot && lk_arena(lk_rd32(w, tp + LK_TP_LWP));
}

static uint32_t lk_find_task(const sa1g_world *w, uint32_t slot)
{
    uint32_t lvl;
    for (lvl = 0; lvl < 8u; lvl++) {
        uint32_t tp = lk_rd32(w, LK_ADDR_TASK_LISTS + 4u * lvl), guard = 0;
        while (lk_arena(tp) && guard++ < 0x2000u) {
            if (lk_task_ok(w, tp, slot)) {
                return tp;
            }
            tp = lk_rd32(w, tp);
        }
    }
    return 0;
}

/* ---- the event look -------------------------------------------------------------------------------------------------- */
static void lk_record(const sa1g_world *w, lk_state *S, uint32_t ewp)
{
    uint32_t k;
    S->rec_mode = lk_rd8(w, ewp + LK_EWP_MODE);
    for (k = 0; k < 3u; k++) {
        S->rec_ang[k] = lk_rd32(w, ewp + LK_EWP_ANG + 4u * k);
        S->rec_pos[k] = lk_rd32(w, ewp + LK_EWP_POS + 4u * k);
    }
    S->rec_tp = lk_rd32(w, ewp + LK_EWP_TP);
}

static int lk_same(const sa1g_world *w, const lk_state *S, uint32_t ewp)
{
    uint32_t k;
    if (lk_rd8(w, ewp + LK_EWP_MODE) != S->rec_mode || lk_rd32(w, ewp + LK_EWP_TP) != S->rec_tp) {
        return 0;
    }
    for (k = 0; k < 3u; k++) {
        if (lk_rd32(w, ewp + LK_EWP_ANG + 4u * k) != S->rec_ang[k] || lk_rd32(w, ewp + LK_EWP_POS + 4u * k) != S->rec_pos[k]) {
            return 0;
        }
    }
    return 1;
}

static void lk_off(uint32_t slot)
{
    ((void (*)(uint8_t))sa1_fn(SA1_GFN_C(LK_FN_OFF), 0u))((uint8_t)slot);
}

/* radians -> BAMS in the world's FP mode (10430.378 = 65536 / 2 pi, as the SET layer's) */
static int32_t lk_bams(float rad)
{
    float t = rad * 10430.378f;
    return (int32_t)(t >= 0.0f ? t + 0.5f : t - 0.5f);
}

static void lk_pre(sa1g_world *w, int32_t sub, void *user)
{
    lk_state *S;
    sa1g_player_ptrs pp;
    uint32_t kind = w->look.persist[LK_P_KIND], flags = w->look.persist[LK_P_FLAGS], slot = (uint32_t)w->slot, ewp, on_addr;
    (void)user;
    if (w->replay) {
        return;
    }
    if (kind == SA1_LOOK_OFF && w->look.persist[LK_P_SUPER] == 0u && lk_state_of(w) == NULL) {
        return;                     /* OFF on a world that has nothing to end: no block, nothing */
    }
    S = lk_state_make(w);
    if (S == NULL) {
        return;
    }
    if (sub == 0 && !lk_task_ok(w, S->task, slot)) {
        S->task = lk_find_task(w, slot);
    }
    sa1g_world_slot_player(w, &pp);
    if (pp.twp == 0) {
        S->owned = 0;
        S->z_set = 0;
        return;
    }
    on_addr = SA1G_ADDR_PAD_REC + LK_PAD_STRIDE * slot + LK_PAD_ON;
    if (S->z_set && kind != SA1_LOOK_CAMERA) {
        sa1g_world_wr32(w, on_addr, lk_rd32(w, on_addr) & ~(uint32_t)LK_PAD_Z);
        S->z_set = 0;
    }
    ewp = lk_rd32(w, pp.twp + LK_TWP_EWP);
    if (!lk_arena(ewp)) {
        S->owned = 0;
        return;
    }
    if (S->owned && !lk_same(w, S, ewp)) {
        S->owned = 0;               /* the game set or cleared it (review R3.12) */
    }
    switch (kind) {
    case SA1_LOOK_POINT:
    case SA1_LOOK_ANGLES:
        if (S->owned || lk_rd8(w, ewp + LK_EWP_MODE) == 0u || (flags & SA1_LOOKF_OVERRIDE_GAME)) {
            float a[3];
            memcpy(&a[0], &w->look.persist[LK_P_A], 4);
            memcpy(&a[1], &w->look.persist[LK_P_B], 4);
            memcpy(&a[2], &w->look.persist[LK_P_C], 4);
            if (kind == SA1_LOOK_POINT) {
                sa1w_set_point(&w->look.ctx->coords, a, S->arg_pos);
                ((void (*)(uint8_t, void *))sa1_fn(SA1_GFN_C(LK_FN_POINT), 0u))((uint8_t)slot, S->arg_pos);
            } else {
                S->arg_ang[0] = 0;
                S->arg_ang[1] = -lk_bams(a[0]);
                S->arg_ang[2] = -lk_bams(a[1]);
                ((void (*)(uint8_t, void *))sa1_fn(SA1_GFN_C(LK_FN_ANGLES), 0u))((uint8_t)slot, S->arg_ang);
            }
            S->setter_calls++;
            lk_record(w, S, ewp);
            S->owned = 1;
        }
        break;
    case SA1_LOOK_CAMERA:
        if (S->owned) {
            lk_off(slot);
            S->owned = 0;
        }
        sa1g_world_wr32(w, on_addr, lk_rd32(w, on_addr) | LK_PAD_Z);
        S->z_set = 1;
        S->z_steps++;
        break;
    default:                        /* SA1_LOOK_OFF */
        if (S->owned) {
            lk_off(slot);
            S->owned = 0;
        }
        break;
    }
}

/* ---- option 7 ------------------------------------------------------------------------------------------------------- */
/* node `index` of a tree in 8C07B080's order (the node, its child subtree, its sibling chain) */
static uint32_t lk_node(const sa1g_world *w, uint32_t root, uint32_t index)
{
    uint32_t stack[64], n = 0, i = 0, o = root;
    while (lk_arena(o) && i < 400u) {
        uint32_t child, sib;
        if (i == index) {
            return o;
        }
        i++;
        child = lk_rd32(w, o + LK_OBJ_CHILD);
        sib = lk_rd32(w, o + LK_OBJ_SIBLING);
        if (child != 0) {
            if (sib != 0) {
                if (n >= 64u) {
                    return 0;
                }
                stack[n++] = sib;
            }
            o = child;
        } else if (sib != 0) {
            o = sib;
        } else if (n > 0) {
            o = stack[--n];
        } else {
            break;
        }
    }
    return 0;
}

static int lk_super_prepare(sa1g_world *w, lk_state *S)
{
    static const uint32_t k_index[3] = { 11u, 21u, 17u };
    uint32_t k, j;
    for (k = 0; k < 3u; k++) {
        S->sup_node[k] = lk_node(w, LK_SUPER_BODY, k_index[k]);
        if (S->sup_node[k] == 0) {
            return 0;
        }
    }
    for (k = 0; k < 3u; k++) {
        for (j = 0; j < 3u; j++) {
            S->sup_saved[k][j] = lk_rd32(w, S->sup_node[k] + LK_OBJ_ANG + 4u * j);
        }
    }
    S->super_on = 1;
    return 1;
}

static void lk_post(sa1g_world *w, int32_t sub, void *user)
{
    lk_state *S = lk_state_of(w);
    uint32_t want = w->look.persist[LK_P_SUPER], k, j, lwp;
    (void)sub;
    (void)user;
    if (w->replay || (want == 0u && (S == NULL || !S->super_on))) {
        return;
    }
    if (S == NULL) {
        S = lk_state_make(w);       /* after a rebuild, the option on: the block comes back here without a look call */
        if (S == NULL) {
            return;
        }
    }
    if (want == 0u) {
        for (k = 0; k < 3u; k++) {
            for (j = 0; j < 3u; j++) {
                sa1g_world_wr32(w, S->sup_node[k] + LK_OBJ_ANG + 4u * j, S->sup_saved[k][j]);
            }
        }
        S->super_on = 0;
        return;
    }
    if (!lk_task_ok(w, S->task, (uint32_t)w->slot)) {
        S->task = lk_find_task(w, (uint32_t)w->slot);
        if (S->task == 0) {
            return;
        }
    }
    if (!S->super_on && !lk_super_prepare(w, S)) {
        return;
    }
    lwp = lk_rd32(w, S->task + LK_TP_LWP);
    for (k = 0; k < 3u; k++) {
        uint32_t from = lk_rd32(w, lwp + LK_LWP_NODES + 4u * k);
        if (!lk_arena(from)) {
            continue;
        }
        for (j = 0; j < 3u; j++) {
            sa1g_world_wr32(w, S->sup_node[k] + LK_OBJ_ANG + 4u * j, lk_rd32(w, from + LK_OBJ_ANG + 4u * j));
        }
    }
}

/* ---- the fill (S5) --------------------------------------------------------------------------------------------------- */
static void lk_fill(const sa1g_world *w, sa1w_pstate *o)
{
    const lk_state *S = lk_state_of(w);
    sa1g_player_ptrs pp;
    uint32_t ewp, slot = (uint32_t)w->slot;
    o->look_kind = w->look.persist[LK_P_KIND];
    if (S == NULL) {
        return;
    }
    if (lk_task_ok(w, S->task, slot)) {
        uint32_t lwp = lk_rd32(w, S->task + LK_TP_LWP);
        o->head_angle[0] = lk_ang16(lk_rd32(w, lwp + LK_LWP_HEAD_Y));
        o->head_angle[1] = lk_ang16(lk_rd32(w, lwp + LK_LWP_HEAD_Z));
        o->eye_angle[0] = lk_ang16(lk_rd32(w, lwp + LK_LWP_EYEL_Y));
        o->eye_angle[1] = lk_ang16(lk_rd32(w, lwp + LK_LWP_EYEL_Z));
    }
    sa1g_world_slot_player(w, &pp);
    ewp = pp.twp != 0 ? lk_rd32(w, pp.twp + LK_TWP_EWP) : 0u;
    if (lk_arena(ewp) && lk_rd8(w, ewp + LK_EWP_MODE) != 0u) {
        o->look_owner = S->owned && lk_same(w, S, ewp) ? 1u : 2u;
    }
}

/* ---- between ticks --------------------------------------------------------------------------------------------------- */
static void lk_install(sa1_context *ctx, sa1g_world *w)
{
    if (w->look.pre == NULL) {
        w->look.pre = lk_pre;
        w->look.post = lk_post;
        w->look.fill = lk_fill;
    }
    w->look.ctx = ctx;
}

sa1_status sa1wl_request(sa1_context *ctx, struct sa1g_world *w, const sa1_look *look, char *why, size_t n)
{
    if (why != NULL && n > 0) {
        why[0] = 0;
    }
    if (w->dead) {
        if (why != NULL) {
            snprintf(why, n, "%s", w->fault.msg);
        }
        return w->dead_status;
    }
    if (w->replay) {
        if (why != NULL) {
            snprintf(why, n, "a replay world takes no look");
        }
        return SA1_ERR_UNSUPPORTED;
    }
    lk_install(ctx, w);
    w->look.persist[LK_P_KIND] = look->kind;
    w->look.persist[LK_P_FLAGS] = look->flags;
    if (look->kind == SA1_LOOK_POINT) {
        memcpy(&w->look.persist[LK_P_A], &look->point[0], 4);
        memcpy(&w->look.persist[LK_P_B], &look->point[1], 4);
        memcpy(&w->look.persist[LK_P_C], &look->point[2], 4);
    } else if (look->kind == SA1_LOOK_ANGLES) {
        memcpy(&w->look.persist[LK_P_A], &look->yaw, 4);
        memcpy(&w->look.persist[LK_P_B], &look->pitch, 4);
    }
    return SA1_OK;
}

sa1_status sa1wl_super_option(sa1_context *ctx, struct sa1g_world *w, int32_t value, char *why, size_t n)
{
    if (why != NULL && n > 0) {
        why[0] = 0;
    }
    if (w->dead) {
        if (why != NULL) {
            snprintf(why, n, "%s", w->fault.msg);
        }
        return w->dead_status;
    }
    if (w->replay) {
        if (why != NULL) {
            snprintf(why, n, "a replay world takes no look");
        }
        return SA1_ERR_UNSUPPORTED;
    }
    lk_install(ctx, w);
    w->look.persist[LK_P_SUPER] = value ? 1u : 0u;
    return SA1_OK;
}
