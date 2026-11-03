/*
 * sa1_world_follower.c - a follower's world (SA1_API_FOLLOWER, round 0.9.8; sadc docs/research/gsa1_098_plan.md 5.3, 6.2,
 * gsa1_096_plan.md 4.4-4.8, 6.1, follow096_lib.md 5.3-5.8, follower_ai.md option C with its Review R.2.3-R.2.7, G1-G14).
 *
 * The game's own "1.5P" situation in a world of its own: CurrentCharacter 0, B_8C78B45A 1, slot 0 holds a PROXY of the leader
 * (an empty-exec task with a playerwk and a csts, written here at every step, never PInitialize), slot 1 the follower, made by
 * the game's own code, and the game's AI task 8C081D4E steers slot 1. Every game function runs unchanged. The proxy is libsa1's
 * stand-in for a world it does not run (the leader), as the empty-landtable worlds are.
 *
 * Timing (R.2.6, R.2.7): every character world keeps a ring of its slot's state after each step (sa1g_ring_note, read only: the
 * reference replays do not change). A follower of a libsa1 character runs exactly the leader's steps it has not run: the proxy
 * holds the leader after ITS step k - 1 when the follower's step k starts (follow_pre, right after the pad update: what the DC's
 * list-0 AI sees) and the leader after step k right after the executor (follow_mid, before CCL and the land update). A host-fed
 * follower's two refreshes use the state the host fed, and it runs its own accumulator.
 *
 * Reads and writes go through the world's own accessors (sa1g_world_rd32 / wr32): the hooks run inside this world; the calls
 * from the API layer run outside any world and may run on any host thread (gsa1_096_plan.md review R.17). No writable process
 * static here (the parallel contract).
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_emote.h"
#include "pc/sh4_fpmodel.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define FW_A_CUR_CHAR   SA1G_ADDR_CUR_CHAR
#define FW_A_15P_FLAG   0x8C78B45Au     /* Sint8 B_8C78B45A: the 1.5P flag (func_8C0829E6) */
#define FW_A_AI_TASK    0x8C78C248u     /* task *B_8C78C248 */
#define FW_A_AI_HOLD    0x8C78C24Cu     /* Sint32 B_8C78C24C */
#define FW_A_PHYSICS    0x8C1BDFE4u     /* player_parameter[8] (0x84 each), Sonic_Physics_Data */
#define FW_A_PERIPHERAL 0x8C754C08u     /* PDS_PERIPHERAL B_8C754C08[8] (0x34 each), on at +0x08, press at +0x10 */
#define FW_F_CALLOC     0x8C098B0Cu     /* CAlloc(n, size) */
#define FW_F_AI_CREATE  0x8C081AC0u
#define FW_F_AI_EXEC    0x8C081D4Eu
#define FW_F_AI_DEST    0x8C081B66u
#define FW_F_PLACE      0x8C0826B0u     /* func_8C0826B0(leader twp, &pos, dist) */
#define FW_F_P1_CREATE  0x8C083F7Cu     /* func_8C083F7C(&{kind, 0, NULL}): SonicMain 0/1, KnucklesMain 2/3, E102Main 4/5 */
#define FW_F_RESPAWN    0x8C081C36u     /* order 24, port off, AI mode 2 */
#define FW_F_HOLD       0x8C082A60u     /* B_8C78C24C = on */
#define FW_F_SETINPUTP  0x8C07C65Eu
#define FW_F_ISLAND     0x8C07D67Cu     /* func_8C07D67C(pwp): allocates playerwk.island_list (PInitialize) */
#define FW_F_TIME_SET   0x8C08AB10u     /* the game clock's minutes, seconds */
#define FW_F_TIME_GET   0x8C08AB02u
#define FW_MAIN_AMY     0x8C0D6B40u
#define FW_MAIN_BIG     0x8C0DFFA0u

/* taskwk / motionwk / playerwk / csts offsets (sadc include/task.h, include/sa1/player.h, include/sa1/collision.h) */
#define FW_TP_TWP       0x20u
#define FW_TP_MWP       0x24u
#define FW_TP_DEST      0x18u
#define FW_TWP_FLAG     0x04u
#define FW_TWP_B0       0x08u
#define FW_TWP_B1       0x09u
#define FW_TWP_ANG      0x14u
#define FW_TWP_POS      0x20u
#define FW_MWP_SPD      0x04u
#define FW_PWP_EQUIP    0x04u
#define FW_PWP_ISLAND_N 0x12u
#define FW_PWP_ITEM     0x06u
#define FW_PWP_SPD      0x38u
#define FW_PWP_CSTS     0x64u
#define FW_PWP_PARAM    0x9Cu
#define FW_PWP_JOG      0xC0u
#define FW_PWP_RUN      0xC4u
#define FW_PWP_ACTION   0x126u
#define FW_CSTS_TNORM   0x1Cu

#define FW_ITEM_INVINCIBLE 0x8000u
#define FW_ITEM_BARRIER    0x0001u
#define FW_ITEM_MAGNET     0x0002u

/* the same construction as sa1g_camera_view (sa1_world_tick.c) */
#define FW_VIEW_DIST    40.0f
#define FW_VIEW_HEIGHT  7.0f

/* ---- arena access ------------------------------------------------------------------------------------------------- */
static uint32_t fw_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (g == 0 || sa1g_world_rd32(w, g, &v) != 0) {
        return 0;
    }
    return v;
}

static float fw_rdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = fw_rd32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static uint32_t fw_rd16(const sa1g_world *w, uint32_t g)
{
    uint32_t v = fw_rd32(w, g & ~3u);
    return (v >> (8u * (g & 2u))) & 0xFFFFu;
}

static uint32_t fw_rd8(const sa1g_world *w, uint32_t g)
{
    uint32_t v = fw_rd32(w, g & ~3u);
    return (v >> (8u * (g & 3u))) & 0xFFu;
}

static void fw_wr32(sa1g_world *w, uint32_t g, uint32_t v)
{
    sa1g_world_wr32(w, g, v);
}

static void fw_wrf(sa1g_world *w, uint32_t g, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    sa1g_world_wr32(w, g, v);
}

static void fw_wr16(sa1g_world *w, uint32_t g, uint32_t val)
{
    uint32_t v = fw_rd32(w, g & ~3u), sh = 8u * (g & 2u);
    fw_wr32(w, g & ~3u, (v & ~(0xFFFFu << sh)) | ((val & 0xFFFFu) << sh));
}

static void fw_wr8(sa1g_world *w, uint32_t g, uint32_t val)
{
    uint32_t v = fw_rd32(w, g & ~3u), sh = 8u * (g & 3u);
    fw_wr32(w, g & ~3u, (v & ~(0xFFu << sh)) | ((val & 0xFFu) << sh));
}

static int fw_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static void *fw_gfn(uint32_t dc)
{
    return sa1_fn(SA1_GFN_C(dc), 0u);
}

/* ---- the ring (every character world) ----------------------------------------------------------------------------------- */
void sa1g_snap_of_world(const sa1g_world *w, sa1g_snap *s)
{
    sa1g_player_ptrs pp;
    uint32_t twp, k;
    memset(s, 0, sizeof *s);
    sa1g_world_slot_player(w, &pp);
    twp = pp.twp;
    if (twp == 0 && pp.tp != 0) {
        twp = fw_rd32(w, pp.tp + FW_TP_TWP);    /* a follower before its first step: its task has the placement */
    }
    if (!fw_arena(twp)) {
        return;
    }
    for (k = 0; k < 3; k++) {
        s->pos[k] = fw_rdf(w, twp + FW_TWP_POS + 4u * k);
        s->ang[k] = fw_rd32(w, twp + FW_TWP_ANG + 4u * k);
    }
    s->flag = fw_rd16(w, twp + FW_TWP_FLAG) & 3u;
    s->game_kind = fw_rd8(w, twp + FW_TWP_B1);
    if (fw_arena(pp.mwp)) {
        for (k = 0; k < 3; k++) {
            s->mspd[k] = fw_rdf(w, pp.mwp + FW_MWP_SPD + 4u * k);
        }
    }
    if (fw_arena(pp.pwp)) {
        for (k = 0; k < 3; k++) {
            s->pspd[k] = fw_rdf(w, pp.pwp + FW_PWP_SPD + 4u * k);
        }
        s->jog_speed = fw_rdf(w, pp.pwp + FW_PWP_JOG);
        s->run_speed = fw_rdf(w, pp.pwp + FW_PWP_RUN);
        s->equipment = fw_rd16(w, pp.pwp + FW_PWP_EQUIP);
        s->action = fw_rd16(w, pp.pwp + FW_PWP_ACTION);
    }
    if (fw_arena(pp.csts)) {
        for (k = 0; k < 3; k++) {
            s->tnorm[k] = fw_rdf(w, pp.csts + FW_CSTS_TNORM + 4u * k);
        }
    } else {
        s->tnorm[1] = 1.0f;
    }
    s->valid = 1;
}

/* after every step (sa1g_step_game) and once at the boot: the slot's state, read only */
void sa1g_ring_note(sa1g_world *w, int first)
{
    if (first) {
        w->ring_step = 0;
    } else {
        w->ring_step++;
    }
    sa1g_snap_of_world(w, &w->ring[w->ring_step % SA1G_RING]);
}

/* ---- the proxy ------------------------------------------------------------------------------------------------------- */
static void fw_write_proxy(sa1g_world *w, const sa1g_follow *f, const sa1g_snap *s)
{
    uint32_t k;
    for (k = 0; k < 3; k++) {
        fw_wrf(w, f->proxy_twp + FW_TWP_POS + 4u * k, s->pos[k]);
        fw_wr32(w, f->proxy_twp + FW_TWP_ANG + 4u * k, s->ang[k]);
        fw_wrf(w, f->proxy_mwp + FW_MWP_SPD + 4u * k, s->mspd[k]);
        fw_wrf(w, f->proxy_pwp + FW_PWP_SPD + 4u * k, s->pspd[k]);
        fw_wrf(w, f->proxy_csts + FW_CSTS_TNORM + 4u * k, s->tnorm[k]);
    }
    /* round 0.9.9 (sa1_follower_spot): the follower's own place, x and z only: the AI's jump rule sees the leader's true height; f->last, the ring and sa1wf_info stay the true leader.
       Only when there is a spot: an add of 0 would turn a -0.0f into +0.0f */
    if (f->spot_cur[0] != 0.0f || f->spot_cur[1] != 0.0f) {
        fw_wrf(w, f->proxy_twp + FW_TWP_POS, s->pos[0] + f->spot_cur[0] * f->spot_part);
        fw_wrf(w, f->proxy_twp + FW_TWP_POS + 8u, s->pos[2] + f->spot_cur[1] * f->spot_part);
    }
    fw_wr16(w, f->proxy_twp + FW_TWP_FLAG, (fw_rd16(w, f->proxy_twp + FW_TWP_FLAG) & ~3u) | (s->flag & 3u));
    if (s->jog_speed != 0.0f || s->run_speed != 0.0f) {
        /* the live speeds: upgrades and Super. 0: the leader has no playerwk yet: the physics row stays */
        fw_wrf(w, f->proxy_pwp + FW_PWP_JOG, s->jog_speed);
        fw_wrf(w, f->proxy_pwp + FW_PWP_RUN, s->run_speed);
    }
    fw_wr16(w, f->proxy_pwp + FW_PWP_EQUIP, s->equipment);
}

/* an item the game gave player 0 (a box the follower broke: code_8C05D900.c) is reported and taken off: the proxy is not a
   character. The item displays free themselves when the bit is clear (code_8C044060.c:62, code_8C043C9E.c:63) */
static void fw_item_off(sa1g_world *w, const sa1g_follow *f)
{
    uint32_t item = fw_rd16(w, f->proxy_pwp + FW_PWP_ITEM);
    if (item != 0) {
        sa1g_player_ptrs pp;
        float p[3] = { 0.0f, 0.0f, 0.0f };
        uint32_t k;
        sa1g_world_slot_player(w, &pp);
        for (k = 0; k < 3 && pp.twp != 0; k++) {
            p[k] = fw_rdf(w, pp.twp + FW_TWP_POS + 4u * k);
        }
        if (item & FW_ITEM_INVINCIBLE) {
            sa1g_world_event_push(w, SA1_EVENT_FOLLOW, SA1_FOLLOW_EV_ITEM, 0u, p, 1u);
        }
        if (item & FW_ITEM_BARRIER) {
            sa1g_world_event_push(w, SA1_EVENT_FOLLOW, SA1_FOLLOW_EV_ITEM, 0u, p, 5u);
        }
        if (item & FW_ITEM_MAGNET) {
            sa1g_world_event_push(w, SA1_EVENT_FOLLOW, SA1_FOLLOW_EV_ITEM, 0u, p, 8u);
        }
        fw_wr16(w, f->proxy_pwp + FW_PWP_ITEM, 0u);
    }
}

static void fw_refresh(sa1g_world *w, uint32_t when)
{
    sa1g_follow *f = &w->fol;
    const sa1g_snap *s;
    if (f->leader != NULL) {
        s = &f->leader->ring[(f->consumed + when) % SA1G_RING];
    } else {
        s = when ? &f->host_mid : &f->host_pre;
    }
    fw_item_off(w, f);
    /* the landing spots O_ITEMBOX registers for player 0 first (8C07D6E0): the character's exec would use and reset them */
    fw_wr16(w, f->proxy_pwp + FW_PWP_ISLAND_N, 0u);
    if (s->valid) {
        fw_write_proxy(w, f, s);
        f->last = *s;
    }
}

/* ---- round 0.9.9: staying apart (sa1_follower_spot; follow099.md 4.2) ---------------------------------------------------- */
/* In fw_pre, before the refresh. The offset in force follows the wanted one by 2 units per step, so a change of the host's list never makes the AI's target jump. The fall rule: a
   follower 30 units below its leader's height and in the air gives its spot up until the leader runs again (back by 0.2 % a step while the leader is on the ground with a speed over 1): without
   it a spot beyond a ledge is a loop (run to the spot, fall, drop in, run to the spot: 27 drop-ins in 40 s, follow099.md 4.1). No spot: nothing is read and nothing is written */
static void fw_spot_step(sa1g_world *w, sa1g_follow *f)
{
    sa1g_player_ptrs pp;
    int a;
    if (f->spot[0] == 0.0f && f->spot[1] == 0.0f && f->spot_cur[0] == 0.0f && f->spot_cur[1] == 0.0f) {
        return;
    }
    for (a = 0; a < 2; a++) {
        float e = f->spot[a] - f->spot_cur[a];
        f->spot_cur[a] += e > 2.0f ? 2.0f : e < -2.0f ? -2.0f : e;
    }
    if (f->spot[0] == 0.0f && f->spot[1] == 0.0f) {
        return;
    }
    sa1g_world_slot_player(w, &pp);
    if (fw_arena(pp.twp)) {
        float y = fw_rdf(w, pp.twp + FW_TWP_POS + 4u), ly = fw_rdf(w, f->proxy_twp + FW_TWP_POS + 4u);
        if (ly - y > 30.0f && !(fw_rd16(w, pp.twp + FW_TWP_FLAG) & 3u)) {
            f->spot_part = 0.0f;
        }
        if (f->last.pspd[0] > 1.0f && f->spot_part < 1.0f && (f->last.flag & 3u)) {
            f->spot_part = f->spot_part < 0.998f ? f->spot_part + 0.002f : 1.0f;
        }
    }
}

/* ---- round 0.9.9: keeping up (sa1_follower_speed; follow099.md 3.5) ---------------------------------------------------------- */
/* player_parameter offsets (sadc include/sa1/player.h; the live copy is playerwk.p, FW_PWP_PARAM): max_x_spd, jog_speed, run_speed, rush_speed, crash_speed, dash_speed, air_accel, slow_down,
   run_break, air_break, lim_h_spd, lim_frict, grd_frict_z */
static const uint32_t k_fw_speed_fields[13] = { 0x10u, 0x24u, 0x28u, 0x2Cu, 0x30u, 0x34u, 0x40u, 0x44u, 0x48u, 0x4Cu, 0x08u, 0x68u, 0x64u };
#define FW_PARAM_RUN_ACCEL 0x3Cu

/* In fw_pre, after the refresh: k = 1 + (speed - 1) * clamp((d - 20) / 30, 0, 1), d the horizontal distance from slot 1 to the proxy (its spot). The fields are absolute values from the table's,
   every step while the option is on, so the game's own save / restore pairs (8C07D2A2, 8C0B2EA0) cannot leave a stale scale. run_accel goes into the world's own table row: func_8C075214 rewrites
   p.run_accel from the table in every land step, so a write into the live field is undone (measured, follow099.md 3.1). Back at speed 1: the table's values once, then nothing. Speed 1 from the
   start: no write, ever, after these two float tests. No extra step runs: timers, drowning and the animation's rules stay the game's */
static void fw_speed_step(sa1g_world *w, sa1g_follow *f)
{
    sa1g_player_ptrs pp;
    float k = 1.0f;
    uint32_t i, row;
    if (f->speed == 1.0f && f->speed_k == 1.0f) {
        return;
    }
    sa1g_world_slot_player(w, &pp);
    if (!fw_arena(pp.twp) || !fw_arena(pp.pwp)) {
        return;
    }
    if (!f->speed_based) {
        f->speed_based = 1;
        f->speed_row = fw_rd8(w, pp.twp + FW_TWP_B1);
        row = FW_A_PHYSICS + f->speed_row * 0x84u;
        for (i = 0; i < 13u; i++) {
            f->speed_base[i] = fw_rdf(w, row + k_fw_speed_fields[i]);
        }
        f->speed_base_accel = fw_rdf(w, row + FW_PARAM_RUN_ACCEL);
    }
    if (f->speed != 1.0f) {
        float dx = fw_rdf(w, pp.twp + FW_TWP_POS) - fw_rdf(w, f->proxy_twp + FW_TWP_POS);
        float dz = fw_rdf(w, pp.twp + FW_TWP_POS + 8u) - fw_rdf(w, f->proxy_twp + FW_TWP_POS + 8u);
        float t = (sa1g_sqrtf(dx * dx + dz * dz) - 20.0f) / 30.0f;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        k = 1.0f + (f->speed - 1.0f) * t;
    }
    for (i = 0; i < 13u; i++) {
        fw_wrf(w, pp.pwp + FW_PWP_PARAM + k_fw_speed_fields[i], f->speed_base[i] * k);
    }
    fw_wrf(w, FW_A_PHYSICS + f->speed_row * 0x84u + FW_PARAM_RUN_ACCEL, f->speed_base_accel * k);
    f->speed_k = k;
}

static void fw_pre(sa1g_world *w, int32_t sub, void *user)
{
    (void)sub;
    (void)user;
    fw_spot_step(w, &w->fol);
    fw_refresh(w, 0u);
    fw_speed_step(w, &w->fol);
}

static void fw_mid(sa1g_world *w, int32_t sub, void *user)
{
    (void)sub;
    (void)user;
    fw_refresh(w, 1u);
    if (w->fol.leader != NULL) {
        w->fol.consumed++;
    }
}

/* ---- the view behind slot 1 (decision 4.7 c; SA1_FOLLOW_GAME_VIEW turns it off) ------------------------------------------ */
static void fw_view(sa1g_world *w)
{
    sa1g_player_ptrs pp;
    uint32_t c, k;
    float p[3], e[3], ax[3][3], s, co;
    int i, j;
    sa1g_world_slot_player(w, &pp);
    if (w->replay || pp.twp == 0) {
        return;
    }
    for (k = 0; k < 3; k++) {
        p[k] = fw_rdf(w, pp.twp + FW_TWP_POS + 4u * k);
    }
    if (!(p[0] == p[0] && p[1] == p[1] && p[2] == p[2])) {
        return;
    }
    c = w->camera.ang[1] & 0xFFFFu;
    sh4_fsca(c, &s, &co);
    ax[0][0] = co;      ax[0][1] = 0.0f;  ax[0][2] = -s;
    ax[1][0] = 0.0f;    ax[1][1] = -1.0f; ax[1][2] = 0.0f;
    ax[2][0] = -s;      ax[2][1] = 0.0f;  ax[2][2] = -co;
    for (i = 0; i < 3; i++) {
        e[i] = p[i] - ax[2][i] * FW_VIEW_DIST;
    }
    e[1] = e[1] + FW_VIEW_HEIGHT;
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
}

/* ---- the AI's exec: respawn events and the history rewrite (decision 4.8) ------------------------------------------------- */
static void fw_ai_exec(sa1g_world *w, uint32_t tp, void *user)
{
    sa1g_follow *f = &w->fol;
    uint32_t twp = fw_rd32(w, tp + FW_TP_TWP), p1 = fw_rd32(w, SA1G_ADDR_PLAYER_TWP + 4u);
    int32_t m0 = (int32_t)(int8_t)fw_rd8(w, twp), m1;
    (void)user;
    ((void (*)(void *))fw_gfn(FW_F_AI_EXEC))(sa1_decode(tp));
    if (sa1em_world_wants_hold(w)) {
        /* round 0.9.9 (an emote on a follower id; follow099.md 2.4, 5.2): while it plays or waits to start the AI's output is dropped, as a player who lets go of the stick. Only its output: its state
           machine and its respawn test run on. Its stroke of 0.005 at rest would keep four kinds out of mode 1, and the emote's cancel rule would end it at once (on in 0 of 60 pairs, measured) */
        fw_wr32(w, FW_A_PERIPHERAL + 0x34u + 0x08u, 0u);        /* per[1].on */
        fw_wr32(w, FW_A_PERIPHERAL + 0x34u + 0x10u, 0u);        /* per[1].press */
        fw_wrf(w, SA1G_ADDR_ANALOG + 8u + 4u, 0.0f);            /* B_8C754DCC[1].stroke */
    }
    if (f->respawn_req) {
        /* sa1_follower_respawn, where the game's own respawn test makes the request (the AI's exec, list 0): order 24 reaches slot 1
           in this step (a rider's board handler takes only 24), the placement and order 48 come at the AI's next entry */
        f->respawn_req = 0;
        (void)((int32_t (*)(void))fw_gfn(FW_F_RESPAWN))();
        f->pending_cause = 2u;
    }
    m1 = (int32_t)(int8_t)fw_rd8(w, twp);
    if (m0 == 2) {
        uint32_t cause = f->pending_cause, k;
        float p[3] = { 0.0f, 0.0f, 0.0f };
        p1 = fw_rd32(w, SA1G_ADDR_PLAYER_TWP + 4u);
        if (fw_arena(p1) && f->kind != SA1_CHAR_TAILS && !(f->flags & SA1_FOLLOW_GAME_HISTORY)) {
            /* the glide's landing turn 8C0C7C26 re-applies pos - history[1] right after the teleport
               (code_8C0C7786.c:177-181): the whole history stands at the new place */
            uint32_t fr;
            int32_t (*setpos)(uint8_t, int32_t, void *, void *) =
                (int32_t (*)(uint8_t, int32_t, void *, void *))fw_gfn(SA1G_FN_POS_HISTORY);
            for (fr = 1; fr < 256u; fr++) {
                setpos(1, (int32_t)fr, sa1g_world_ptr(w, p1 + FW_TWP_POS), NULL);
            }
        }
        for (k = 0; k < 3 && fw_arena(p1); k++) {
            p[k] = fw_rdf(w, p1 + FW_TWP_POS + 4u * k);
        }
        f->respawns++;
        if (cause == 0u) {
            f->respawns_distance++;
        } else if (cause == 1u) {
            f->respawns_offscreen++;
        }
        sa1g_world_event_push(w, SA1_EVENT_FOLLOW, SA1_FOLLOW_EV_RESPAWN, 0u, p, cause);
        f->pending_cause = 1u;
    }
    if (m1 == 2 && m0 != 2 && fw_arena(p1) && f->pending_cause != 2u) {
        /* the game's test asked for it (func_8C082570: distance > 1000, or 300 frames off the leader's camera) */
        uint32_t p0 = fw_rd32(w, SA1G_ADDR_PLAYER_TWP), k;
        float d = 0.0f, v;
        for (k = 0; k < 3 && fw_arena(p0); k++) {
            v = fw_rdf(w, p0 + FW_TWP_POS + 4u * k) - fw_rdf(w, p1 + FW_TWP_POS + 4u * k);
            d += v * v;
        }
        f->pending_cause = d > 1000.0f * 1000.0f ? 0u : 1u;
    }
}

/* ---- the boot (sa1g_boot_cb, inside the world) --------------------------------------------------------------------------- */
void sa1g_follower_boot(sa1g_world *w, const sa1g_boot_cfg *c)
{
    sa1g_follow *f = &w->fol;
    const sa1g_follow_cfg *fc = &c->follow;
    void *(*create)(uint16_t, int32_t, sa1_gfn) = (void *(*)(uint16_t, int32_t, sa1_gfn))fw_gfn(SA1G_FN_CREATE_TASK);
    void *(*cal)(int32_t, int32_t) = (void *(*)(int32_t, int32_t))fw_gfn(FW_F_CALLOC);
    uint8_t *tp;
    uint32_t gtp, twp, mwp, pwp, csts, k, kind_row;
    void *ai, *ftp = NULL;
    memset(f, 0, sizeof *f);
    f->on = 1;
    f->speed = 1.0f;
    f->speed_k = 1.0f;
    f->spot_part = 1.0f;
    f->flags = fc->flags;
    f->kind = fc->kind;
    f->pending_cause = 1u;
    f->host_pre = f->host_mid = f->last = fc->first;
    /* the game's 1.5P situation; the predicate 8C082820 holds through the flag on stage 0x0100 */
    fw_wr8(w, FW_A_15P_FLAG, 1u);
    fw_wr32(w, FW_A_AI_TASK, 0u);
    fw_wr32(w, FW_A_AI_HOLD, 0u);
    sa1g_call_dc(SA1G_FN_PLAYER_SLOTS);
    /* the proxy: the empty exec 8C098440 (never PInitialize: no breath helper for player 0) */
    tp = (uint8_t *)create(7, 1, SA1_GFN_NULL);
    if (tp == NULL) {
        sa1g_fatal(SA1G_FN_CREATE_TASK, "follower boot: CreateElementalTask failed for the proxy");
    }
    gtp = sa1_encode(tp);
    twp = fw_rd32(w, gtp + FW_TP_TWP);
    mwp = fw_rd32(w, gtp + FW_TP_MWP);
    pwp = sa1_encode(cal(1, 0x200));
    csts = sa1_encode(cal(1, 0x84));
    if (!fw_arena(twp) || !fw_arena(mwp) || !fw_arena(pwp) || !fw_arena(csts)) {
        sa1g_fatal(FW_F_CALLOC, "follower boot: no heap for the proxy");
    }
    ((void (*)(void *))fw_gfn(FW_F_ISLAND))(sa1g_world_ptr(w, pwp));    /* O_ITEMBOX writes player 0's list (8C07D6E0) */
    kind_row = fc->leader_chr >= 0 && fc->leader_chr < 8 ? (uint32_t)fc->leader_chr : 0u;
    for (k = 0; k < 0x84u; k += 4u) {
        fw_wr32(w, pwp + FW_PWP_PARAM + k, fw_rd32(w, FW_A_PHYSICS + kind_row * 0x84u + k));
    }
    fw_wr32(w, pwp + FW_PWP_CSTS, csts);
    fw_wr32(w, mwp + 0u, pwp);                  /* mwp->work.pwp */
    fw_wr8(w, twp + FW_TWP_B0, 0u);             /* counter.b[0]: player 0 */
    fw_wr8(w, twp + FW_TWP_B1, kind_row);       /* counter.b[1]: the leader's game kind (Review G6; Super: 0) */
    fw_wr32(w, SA1G_ADDR_PLAYER_TP, gtp);
    fw_wr32(w, SA1G_ADDR_PLAYER_TWP, twp);
    fw_wr32(w, SA1G_ADDR_PLAYER_MWP, mwp);
    fw_wr32(w, SA1G_ADDR_PLAYER_PWP, pwp);
    f->proxy_tp = gtp;
    f->proxy_twp = twp;
    f->proxy_mwp = mwp;
    f->proxy_pwp = pwp;
    f->proxy_csts = csts;
    fw_write_proxy(w, f, &fc->first);
    ((int32_t (*)(uint8_t, int32_t, void *, void *))fw_gfn(SA1G_FN_POS_HISTORY))(0, 0, sa1g_world_ptr(w, twp + FW_TWP_POS),
                                                                                 NULL);
    if (fc->kind == SA1_CHAR_GAMMA) {
        /* E-102's start in sa1g_create_player: his main measures the game clock (the clock stays stopped, no time limit runs) */
        uint8_t mm = 0, ss = 0;
        ((void (*)(int8_t, int8_t))fw_gfn(FW_F_TIME_SET))(3, 0);
        ((void (*)(void *, void *))fw_gfn(FW_F_TIME_GET))(&mm, &ss);
        if ((uint32_t)mm * 60u + ss < 60u) {
            ((void (*)(int8_t, int8_t))fw_gfn(FW_F_TIME_SET))(1, 0);
        }
    }
    /* the follower, by the game's own code */
    if (fc->kind == SA1_CHAR_TAILS) {
        ai = ((void *(*)(void *))fw_gfn(FW_F_AI_CREATE))(tp);
        if (ai == NULL) {
            sa1g_fatal(FW_F_AI_CREATE, "follower boot: 8C081AC0 did not make the AI task (the predicate 8C082820)");
        }
        f->ai_tp = sa1_encode(ai);
        /* the newest task of list 1: tasks are linked at the head (code_8C098444.c:195, 223-231) */
        w->player_tp = fw_rd32(w, SA1G_ADDR_TASK_LISTS + 4u);
    } else {
        /* the AI half of 8C081AC0 (code_8C081AC0.c:34-40, 51-52) */
        uint32_t atwp;
        ai = create(2, 0, SA1_GFN_C(FW_F_AI_EXEC));
        if (ai == NULL) {
            sa1g_fatal(SA1G_FN_CREATE_TASK, "follower boot: CreateElementalTask failed for the AI task");
        }
        f->ai_tp = sa1_encode(ai);
        atwp = fw_rd32(w, f->ai_tp + FW_TP_TWP);
        fw_wr32(w, FW_A_AI_TASK, f->ai_tp);
        fw_wr8(w, atwp + FW_TWP_B1, 2u);
        fw_wr8(w, atwp + FW_TWP_B0, 1u);
        fw_wr32(w, f->ai_tp + FW_TP_DEST, FW_F_AI_DEST);
        if (fc->kind == SA1_CHAR_SONIC || fc->kind == SA1_CHAR_KNUCKLES || fc->kind == SA1_CHAR_GAMMA) {
            /* the game's own creation of a slot-1 player (Review G11; `kind` at +0 of its request) */
            uint32_t req = sa1_encode(cal(1, 12));
            if (!fw_arena(req)) {
                sa1g_fatal(FW_F_CALLOC, "follower boot: no heap for the request");
            }
            fw_wr32(w, req, fc->kind == SA1_CHAR_SONIC ? 0u : fc->kind == SA1_CHAR_KNUCKLES ? 2u : 4u);
            ftp = ((void *(*)(void *))fw_gfn(FW_F_P1_CREATE))(sa1g_world_ptr(w, req));
        } else {
            ftp = create(7, 1, SA1_GFN_C(fc->kind == SA1_CHAR_AMY ? FW_MAIN_AMY : FW_MAIN_BIG));
            if (ftp != NULL) {
                uint32_t g = fw_rd32(w, sa1_encode(ftp) + FW_TP_TWP);
                fw_wr8(w, g + FW_TWP_B1, (uint32_t)fc->kind);
                fw_wr8(w, g + FW_TWP_B0, 1u);
            }
        }
        if (ftp == NULL) {
            sa1g_fatal(FW_F_P1_CREATE, "follower boot: no task for the follower");
        }
        {
            uint32_t g = fw_rd32(w, sa1_encode(ftp) + FW_TP_TWP);
            ((void (*)(void *, void *, float))fw_gfn(FW_F_PLACE))(sa1g_world_ptr(w, twp), sa1g_world_ptr(w, g + FW_TWP_POS), 10.0f);
        }
        fw_wr8(w, atwp + 0u, 0u);       /* mode 0 */
        w->player_tp = sa1_encode(ftp);
    }
    f->ai_twp = fw_rd32(w, f->ai_tp + FW_TP_TWP);
    w->slot = 1;
    /* live worlds: the hooks. The test configuration of tests/follower_replay.c clears them and feeds the proxy itself */
    w->follow_pre = fw_pre;
    w->follow_mid = fw_mid;
    if (!(f->flags & SA1_FOLLOW_GAME_VIEW)) {
        w->follow_view = fw_view;
    }
}

/* after the boot (outside the world): the AI's exec through the walker (a follower world always walks with sa1g_run_tasks:
   sa1g_step_game) */
sa1_status sa1g_follower_attach(sa1g_world *w)
{
    if (sa1g_world_xhook(w, FW_F_AI_EXEC, fw_ai_exec, NULL) != 0) {
        return SA1_ERR_LIMIT;
    }
    return SA1_OK;
}

/* ---- the API's parts (outside any world) --------------------------------------------------------------------------------- */
int sa1wf_on(const struct sa1g_world *w)
{
    return w != NULL && w->fol.on;
}

int32_t sa1wf_kind(const struct sa1g_world *w)
{
    return w != NULL ? w->fol.kind : -1;
}

static uint32_t fw_bams_of_yaw(float yaw)
{
    double t = floor(-(double)yaw / 6.28318530717958647692 * 65536.0 + 0.5);
    return (uint32_t)(int64_t)fmod(t, 65536.0) & 0xFFFFu;
}

/* a host-fed state as a snapshot: SA1 space, speeds per 60 Hz step */
static void fw_snap_of_host(const sa1_context *ctx, const sa1g_world *w, const sa1_leader_state *ls, sa1g_snap *s)
{
    float v[3], h;
    uint32_t row;
    memset(s, 0, sizeof *s);
    sa1_from_host_point(ctx, ls->position, s->pos);
    sa1_from_host_dir(ctx, ls->velocity, v);
    v[0] /= 60.0f;
    v[1] /= 60.0f;
    v[2] /= 60.0f;
    memcpy(s->mspd, v, sizeof v);
    h = sqrtf(v[0] * v[0] + v[2] * v[2]);
    s->pspd[0] = h;
    if (h >= 0.01f) {
        /* the game moves along (cos a, 0, sin a) for ang.y = a: measured on a live leader (follow096_lib.md 5.3) */
        double t = floor(atan2((double)v[2], (double)v[0]) / 6.28318530717958647692 * 65536.0 + 0.5);
        s->ang[1] = (uint32_t)(int64_t)fmod(t, 65536.0) & 0xFFFFu;
    } else {
        s->ang[1] = fw_bams_of_yaw(ls->yaw);
    }
    s->flag = (ls->flags & SA1_LEADER_ON_GROUND) ? 1u : 0u;
    row = ls->kind >= 0 && ls->kind < 8 ? (uint32_t)ls->kind : 0u;
    s->game_kind = row;
    if (w != NULL && w->ram != NULL) {
        s->jog_speed = fw_rdf(w, FW_A_PHYSICS + row * 0x84u + 0x24u);
        s->run_speed = fw_rdf(w, FW_A_PHYSICS + row * 0x84u + 0x28u);
    }
    s->tnorm[1] = 1.0f;
    s->valid = 1;
}

/* the first snapshot of a new follower world (sa1w_world_create): the leader's current state, or host_leader */
void sa1g_follow_first(const sa1_context *ctx, const struct sa1g_world *leader, const sa1_leader_state *host, sa1g_snap *out)
{
    if (leader != NULL) {
        sa1g_snap_of_world(leader, out);
    } else {
        fw_snap_of_host(ctx, NULL, host, out);
    }
}

sa1_status sa1wf_feed(const sa1_context *ctx, struct sa1g_world *w, const sa1_leader_state *s)
{
    sa1g_snap n;
    if (w == NULL || !w->fol.on) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    fw_snap_of_host(ctx, w, s, &n);
    w->fol.host_pre = n;
    w->fol.host_mid = n;
    return SA1_OK;
}

/* a new leader, or none: host-fed from the last snapshot, standing (the leader's speeds no longer mean anything) */
void sa1wf_set_leader(struct sa1g_world *w, const struct sa1g_world *leader, sa1_character_id id)
{
    sa1g_follow *f = &w->fol;
    f->leader_id = id;
    f->leader = NULL;
    f->lost = 0;
    if (leader != NULL) {
        f->consumed = leader->ring_step;
    } else {
        f->host_pre = f->last;
        memset(f->host_pre.mspd, 0, sizeof f->host_pre.mspd);
        memset(f->host_pre.pspd, 0, sizeof f->host_pre.pspd);
        f->host_mid = f->host_pre;
    }
}

/* B_8C799920[n] = 1, before the world's first step */
void sa1wf_event_flag(struct sa1g_world *w, uint32_t n)
{
    fw_wr8(w, SA1G_ADDR_EVENT_FLAGS + n, 1u);
}

sa1_character_id sa1wf_leader_id(const struct sa1g_world *w)
{
    return w != NULL ? w->fol.leader_id : 0;
}

void sa1wf_leader_lost(struct sa1g_world *w, uint32_t arg)
{
    sa1g_follow *f = &w->fol;
    float p[3] = { 0.0f, 0.0f, 0.0f };
    sa1g_player_ptrs pp;
    uint32_t k;
    if (f->lost) {
        return;
    }
    sa1wf_set_leader(w, NULL, 0);
    f->lost = 1;
    sa1g_world_slot_player(w, &pp);
    for (k = 0; k < 3 && pp.twp != 0; k++) {
        p[k] = fw_rdf(w, pp.twp + FW_TWP_POS + 4u * k);
    }
    sa1g_world_event_push(w, SA1_EVENT_FOLLOW, SA1_FOLLOW_EV_LEADER_LOST, 0u, p, arg);
}

/* one tick of a follower, before its pairs: leader = its libsa1 leader's world (NULL: host-fed). Returns the dt the world runs
   with: slaved, the accumulator holds exactly the pairs the leader ran (at most 8: the ring) and the tick's dt is 0 */
float sa1wf_slave(struct sa1g_world *w, const struct sa1g_world *leader, float dt)
{
    sa1g_follow *f = &w->fol;
    uint64_t behind, n;
    f->leader = leader;
    if (leader == NULL) {
        return dt;
    }
    if (leader->ring_step < f->consumed) {
        f->consumed = leader->ring_step;    /* a new world under the same id: its ring starts again */
    }
    behind = leader->ring_step - f->consumed;
    if (behind > SA1G_RING - 1u) {
        uint64_t drop = behind - (SA1G_RING - 1u);
        f->steps_dropped += (uint32_t)drop;
        f->consumed += drop;
        behind = SA1G_RING - 1u;
        {
            float p[3] = { 0.0f, 0.0f, 0.0f };
            sa1g_player_ptrs pp;
            uint32_t k;
            sa1g_world_slot_player(w, &pp);
            for (k = 0; k < 3 && pp.twp != 0; k++) {
                p[k] = fw_rdf(w, pp.twp + FW_TWP_POS + 4u * k);
            }
            sa1g_world_event_push(w, SA1_EVENT_FOLLOW, SA1_FOLLOW_EV_LEADER_LOST, 0u, p, 2u);
        }
    }
    n = behind / 2u;
    w->acc = (double)n * SA1G_PAIR_SECONDS + 1e-6;
    return 0.0f;
}

/* what the leader link means after the tick: nothing may keep the pointer (the leader's world can go) */
void sa1wf_tick_end(struct sa1g_world *w)
{
    w->fol.leader = NULL;
    if (w->fol.leader_id != 0) {
        w->acc = 0.0;
    }
}

sa1_status sa1wf_respawn(struct sa1g_world *w)
{
    if (w == NULL || !w->fol.on) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    w->fol.respawn_req = 1;     /* made at the AI's next exec: fw_ai_exec */
    return SA1_OK;
}

static void fw_hold_cb(sa1g_world *w, void *user)
{
    int32_t on = *(const int32_t *)user;
    (void)w;
    ((void (*)(int32_t))fw_gfn(FW_F_HOLD))(on);
    if (!on) {
        /* the supervisor's order 12 parked slot 1 (Tails' mode 18: nothing runs); order 24 is the game's way out (AI mode 8) */
        ((void (*)(uint8_t, int32_t))fw_gfn(FW_F_SETINPUTP))(1, 24);
    }
}

/* sa1_follower_speed: the value fw_speed_step reads at the world's next step (the caller checked its range) */
sa1_status sa1wf_speed(struct sa1g_world *w, float speed)
{
    if (w == NULL || !w->fol.on) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    w->fol.speed = speed;
    return SA1_OK;
}

/* sa1_follower_spot: an offset in host space, x and z of SA1 space in force. It scales as a point does: G-SA1's 64 Hammer units are 16 SA1 units */
sa1_status sa1wf_spot(const sa1_context *ctx, struct sa1g_world *w, const float offset[3])
{
    float o[3];
    if (w == NULL || !w->fol.on) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    sa1_from_host_point(ctx, offset, o);
    w->fol.spot[0] = o[0];
    w->fol.spot[1] = o[2];
    if (w->steps == 0u) {
        w->fol.spot_cur[0] = o[0];      /* before the world's first step: at once */
        w->fol.spot_cur[1] = o[2];
    }
    return SA1_OK;
}

sa1_status sa1wf_hold(struct sa1g_world *w, int on)
{
    int32_t v = on ? 1 : 0;
    sa1_status st;
    if (w == NULL || !w->fol.on) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1g_world_call(w, fw_hold_cb, &v);
    if (st == SA1_OK) {
        w->fol.held = v;
    }
    return st;
}

/* one pair's visibility, the last 300 pairs (Review G2) */
void sa1wf_note_hidden(struct sa1g_world *w, int hidden)
{
    sa1g_follow *f = &w->fol;
    uint32_t i = f->hidden_pos % 300u, *wd = &f->hidden_bits[i / 32u], bit = 1u << (i % 32u);
    if (*wd & bit) {
        f->hidden_count--;
    }
    *wd &= ~bit;
    if (hidden) {
        *wd |= bit;
        f->hidden_count++;
    }
    f->hidden_pos++;
}

void sa1wf_info(const sa1_context *ctx, const struct sa1g_world *w, const struct sa1g_world *leader, sa1_follower_info *o)
{
    const sa1g_follow *f = &w->fol;
    sa1g_player_ptrs pp;
    float d = 0.0f, v;
    uint32_t k;
    o->leader = f->leader_id;
    o->flags = (f->leader_id == 0 ? SA1_FINFO_HOST_FED : 0u) | (f->lost ? SA1_FINFO_LEADER_LOST : 0u) |
               (f->held ? SA1_FINFO_HELD : 0u);
    if (fw_arena(f->ai_twp)) {
        o->ai_mode = (int32_t)(int8_t)fw_rd8(w, f->ai_twp);
        o->ai_smode = (int32_t)(int8_t)fw_rd8(w, f->ai_twp + 1u);
        o->offscreen_steps = fw_rd16(w, f->ai_twp + FW_TWP_B0);
    }
    sa1g_world_slot_player(w, &pp);
    if (pp.twp != 0) {
        const sa1g_snap *ls = leader != NULL ? &leader->ring[leader->ring_step % SA1G_RING] : &f->last;
        for (k = 0; k < 3; k++) {
            v = fw_rdf(w, pp.twp + FW_TWP_POS + 4u * k) - ls->pos[k];
            d += v * v;
        }
        /* the host's unit: the coordinate scale (sa1_coords.scale) */
        o->distance = sqrtf(d) * ctx->coords.scale;
    }
    o->respawns = f->respawns;
    o->respawns_distance = f->respawns_distance;
    o->respawns_offscreen = f->respawns_offscreen;
    o->steps_behind = leader != NULL ? (uint32_t)(leader->ring_step - f->consumed) : 0u;
    o->steps_dropped = f->steps_dropped;
    o->hidden_pairs = f->hidden_count;
    o->speed_factor = f->speed_k;
}
