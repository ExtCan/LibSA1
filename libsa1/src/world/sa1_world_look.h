/*
 * sa1_world_look.h - the head look's two sides (SA1_API_LOOK, round 0.9.8; sadc docs/research/gsa1_098_plan.md 6.3 part 2, look096.md
 * 7.1). Internal. src/sa1_look.c (the API, asset-layer flags: no world headers) and src/sa1_hostobj.c (option 7's routing, inside
 * #if !defined(SA1H_HOSTOBJ)) call these; src/world/sa1_world_look.c (the lane inside a world) defines the sa1wl_* functions. Only
 * opaque world types cross this header.
 */
#ifndef SA1_WORLD_LOOK_H
#define SA1_WORLD_LOOK_H

#include "sa1_internal.h"

struct sa1g_world;

#define SA1WL_WHY_MAX 128u

/* ---- the head look ---- */
/* the API side: sa1_character_set_option(id, SA1_COPT_SUPER_LOOK, value), src/sa1_look.c */
sa1_status sa1_look_set_super(sa1_context *ctx, sa1_character_id id, int32_t value);

/* the world side (between ticks): latches the host's look in the lane (installed by the first call for the world); a dead world's
   status, SA1_ERR_UNSUPPORTED with `why` for a replay world */
sa1_status sa1wl_request(sa1_context *ctx, struct sa1g_world *w, const sa1_look *look, char *why, size_t n);
/* SA1_COPT_SUPER_LOOK: 1 copies the look onto the Super body's nodes, 0 puts back what they held (installs the lane either way) */
sa1_status sa1wl_super_option(sa1_context *ctx, struct sa1g_world *w, int32_t value, char *why, size_t n);

/* ---- the game's camera requests (src/sa1_camreq.c the API, src/world/sa1_world_camreq.c the world side) ---- */
/* SA1 space; outside the world, through the world's own accessors */
typedef struct sa1wc_state {
    uint32_t flags, kind;       /* SA1_CAMSTATE_*, SA1_CAMKIND_* */
    int32_t level;
    uint32_t adjust, function;
    float pos[3], fwd[3], tgt[3], fov;
} sa1wc_state;
void sa1wc_state_get(const struct sa1g_world *w, float alpha, sa1wc_state *o);
/* S9 through src/sa1_camreq.c's gate: sets w->camreq.pre (live worlds only) */
void sa1g_camreq_arm(struct sa1g_world *w);

#endif /* SA1_WORLD_LOOK_H */
