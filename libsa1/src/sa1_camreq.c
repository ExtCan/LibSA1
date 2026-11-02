/*
 * sa1_camreq.c - the game's camera requests, the API side (SA1_API_CAMERA, round 0.9.8; sadc docs/research/gsa1_098_plan.md 5.3, 6.3
 * C2, fish098.md 3.4). Owned by 098-lib-lookcam from lanes/lib_contract.txt (gsa1_098_plan.md 5.2). Plain C99 with the asset-layer
 * flags (no world headers); the world side is src/world/sa1_world_camreq.c, through src/world/sa1_world_look.h.
 *
 * Gate (gsa1_098_plan.md 3.2): SA1CQ_BUILT 0: in the optimised archives sa1_character_camera answers SA1_ERR_UNSUPPORTED after its
 * argument checks (a const call: sa1_last_error not touched) and the bit SA1_INPUT_GAME_CAMERA is accepted and inert
 * (sa1g_camreq_install does nothing). The -O0 build runs the real code for the lane's own tests.
 */
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "world/sa1_world_look.h"

#include <stddef.h>
#include <string.h>

#define SA1CQ_BUILT 1
#if defined(__OPTIMIZE__) && !SA1CQ_BUILT
#define SA1CQ_GATED 1
#else
#define SA1CQ_GATED 0
#endif

void sa1g_camreq_install(struct sa1g_world *w)
{
#if SA1CQ_GATED || defined(SA1_NO_WORLD)
    (void)w;
#else
    sa1g_camreq_arm(w);
#endif
}

sa1_status sa1_character_camera(const sa1_context *ctx, sa1_character_id id, float alpha, sa1_camera_state *out)
{
    struct sa1g_world *w = NULL;
    sa1_status st;
    if (ctx == NULL || out == NULL || out->struct_size < offsetof(sa1_camera_state, fov) + sizeof out->fov) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1w_character_peek(ctx, id, NULL, NULL, &w);
    if (st != SA1_OK) {
        return st;
    }
    if (SA1CQ_GATED) {
        return SA1_ERR_UNSUPPORTED;
    }
#if defined(SA1_NO_WORLD)
    (void)alpha;
    return SA1_ERR_NOT_LOADED;
#else
    if (w == NULL) {
        return SA1_ERR_NOT_LOADED;
    }
    {
        sa1wc_state s;
        sa1_camera_state x;
        sa1wc_state_get(w, alpha, &s);
        memset(&x, 0, sizeof x);
        x.struct_size = sizeof x;
        x.flags = s.flags;
        x.kind = s.kind;
        x.level = s.level;
        x.adjust = s.adjust;
        x.function = s.function;
        x.fov = s.fov;
        if (s.flags & SA1_CAMSTATE_RUNNING) {
            sa1_to_host_point(ctx, s.pos, x.position);
            sa1_to_host_dir(ctx, s.fwd, x.forward);
            sa1_to_host_point(ctx, s.tgt, x.target);
        }
        sa1_copy_out(out, &x, sizeof x);
    }
    return SA1_OK;
#endif
}
