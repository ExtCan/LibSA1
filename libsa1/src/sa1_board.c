/*
 * sa1_board.c - the board API (SA1_API_BOARD, round 0.9.5; sadc docs/research/gsa1_095_plan.md 4.4, 5.3, 6.2 with its
 * Review, board095_lib.md 4): sa1_character_board / sa1_character_board_state - the arguments, the character's kind, building
 * its world (as sa1_character_order), the refusals in plain words (logged at DEBUG through sa1_refuse: a refusal is the
 * call's normal answer, board095_perf.md 4), the state in host space. The lane inside the world is
 * src/world/sa1_world_board.c (sa1wb_*, src/sa1_api_world.h).
 *
 * Kinds: Sonic (0) and Tails (2) only; Super Sonic (8) has no case 44 in his order handler 8C0B0F24 and no board actions in
 * his world. A kind-0 world in the General form may board (the game takes order 44 in Sonic's normal modes: board095_lib.md
 * 2.3 F); whether a host lets it is the host's rule (G-SA1: gsa1_board_super).
 */
#include "sa1_internal.h"
#include "sa1_api_world.h"

#include <stddef.h>
#include <string.h>

/* the hand-over gate: SA1WB_GATED (src/sa1_api_world.h) */
#define SA1B_GATED SA1WB_GATED
#define SA1B_NOT_BUILT SA1WB_NOT_BUILT

/* the kind's board actions are in its loaded set (SA1_LOAD_BOARDS: Sonic 102.., Tails 107..; the world images always hold
   S_SBMOT / M_SBMOT outside DISC mode, the asset set only with the flag: without it the board would be drawn untextured and
   its poses missing) */
static int bd_boards_loaded(const sa1_context *ctx, int32_t kind)
{
    const sa1_charset *cs = &ctx->chars[kind];
    uint32_t a = kind == SA1_CHAR_TAILS ? 107u : 102u;
    return cs->loaded && cs->actions != NULL && a < cs->num_actions && cs->actions[a].image >= 0;
}

static const char *bd_kind_name(int32_t kind)
{
    switch (kind) {
    case SA1_CHAR_KNUCKLES: return "Knuckles";
    case SA1_CHAR_AMY: return "Amy";
    case SA1_CHAR_GAMMA: return "E-102";
    case SA1_CHAR_BIG: return "Big";
    case SA1_CHAR_SUPER_SONIC: return "Super Sonic";
    default: return "this character";
    }
}

sa1_status sa1_character_board(sa1_context *ctx, sa1_character_id id, const sa1_board_desc *d)
{
    sa1_status st;
    int32_t kind = -1;
    uint32_t cflags = 0;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (d == NULL || d->struct_size < offsetof(sa1_board_desc, flags) + sizeof d->flags) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_board_desc: NULL or struct_size %u (at least %u)",
                        d != NULL ? (unsigned)d->struct_size : 0u,
                        (unsigned)(offsetof(sa1_board_desc, flags) + sizeof d->flags));
    }
    if (d->on < SA1_BOARD_OFF || d->on > SA1_BOARD_FREE) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_board_desc: on %d (SA1_BOARD_OFF / ON / PUSH / FREE)",
                        (int)d->on);
    }
    if (d->terrain > SA1_BOARD_TERRAIN_SAND) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_board_desc: terrain %u (SA1_BOARD_TERRAIN_MAP / SNOW / SAND)",
                        (unsigned)d->terrain);
    }
    if (d->flags & ~SA1_BOARD_KNOWN_FLAGS) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_board_desc: unknown flags 0x%X", (unsigned)d->flags);
    }
    st = sa1w_character_peek(ctx, id, &kind, &cflags, NULL);
    if (st != SA1_OK) {
        return st == SA1_ERR_NOT_FOUND ? sa1_fail(ctx, st, "sa1_character_board: no character %d", (int)id) : st;
    }
    if (SA1B_GATED) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, SA1B_NOT_BUILT);
    }
    if (kind != SA1_CHAR_SONIC && kind != SA1_CHAR_TAILS) {
        if (kind == SA1_CHAR_SUPER_SONIC) {
            return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "Super Sonic doesn't ride a board");
        }
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "only Sonic and Tails ride a board (%s doesn't)",
                          bd_kind_name(kind));
    }
    if (!bd_boards_loaded(ctx, kind)) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "sa1_character_board: %s's board actions are not loaded (SA1_LOAD_BOARDS)",
                        kind == SA1_CHAR_TAILS ? "Tails" : "Sonic");
    }
    if (cflags & SA1_CREATE_PUPPET) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "character %d is a puppet (SA1_CREATE_PUPPET): it has "
                          "no world", (int)id);
    }
#if defined(SA1_NO_WORLD)
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no board");
#else
    {
        struct sa1g_world *w = NULL;
        char why[SA1WB_WHY_MAX];
        st = sa1w_character_world_ensure(ctx, id, &w);
        if (SA1_FAILED(st) || w == NULL) {
            return SA1_FAILED(st) ? st : SA1_ERR_NOT_LOADED;
        }
        st = sa1wb_request(ctx, w, kind, d, why, sizeof why);
        if (st == SA1_ERR_UNSUPPORTED) {
            return sa1_refuse(ctx, st, SA1_LOG_DEBUG, "%s", why[0] ? why : "can't get on a board now");
        }
        if (SA1_FAILED(st)) {
            return sa1_fail(ctx, st, "sa1_character_board: %s", why[0] ? why : sa1w_world_fault(w));
        }
        return SA1_OK;
    }
#endif
}

sa1_status sa1_character_board_state(const sa1_context *ctx, sa1_character_id id, sa1_board_state *out)
{
    sa1_board_state b;
    sa1_status st;
    struct sa1g_world *w = NULL;
    if (ctx == NULL || out == NULL || out->struct_size < sizeof(uint32_t)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1w_character_peek(ctx, id, NULL, NULL, &w);
    if (st != SA1_OK) {
        return st;
    }
    if (SA1B_GATED) {
        return SA1_ERR_UNSUPPORTED;
    }
    memset(&b, 0, sizeof b);
    b.struct_size = sizeof b;
    b.board_kind = -1;
    b.model_rotation[0] = b.model_rotation[4] = b.model_rotation[8] = 1.0f;
#if !defined(SA1_NO_WORLD)
    if (w != NULL) {
        sa1wb_state s;
        uint32_t i, j, k;
        sa1wb_state_get(w, &s);
        b.flags = s.flags;
        b.terrain = s.terrain;
        b.board_task = s.board_task;
        b.push_steps = s.push_steps;
        b.saved_jmp_y_spd = s.saved_jmp;
        b.board_kind = s.kind;
        b.board_variant = s.variant;
        b.still_steps = s.still;
        if (s.board_task != 0 && s.pose_valid) {
            /* host space: the origin as sa1_state's position, the rotation as A R A^T (host axes) */
            const float *a = ctx->coords.axis;
            float m[9];
            sa1_to_host_point(ctx, s.mpos, b.model_position);
            for (i = 0; i < 3; i++) {           /* m = R A^T */
                for (j = 0; j < 3; j++) {
                    float v = 0.0f;
                    for (k = 0; k < 3; k++) {
                        v += s.mrot[3 * i + k] * a[3 * j + k];
                    }
                    m[3 * i + j] = v;
                }
            }
            for (i = 0; i < 3; i++) {           /* A m */
                for (j = 0; j < 3; j++) {
                    float v = 0.0f;
                    for (k = 0; k < 3; k++) {
                        v += a[3 * i + k] * m[3 * k + j];
                    }
                    b.model_rotation[3 * i + j] = v;
                }
            }
        }
    }
#else
    (void)w;
#endif
    sa1_copy_out(out, &b, sizeof b);
    return SA1_OK;
}
