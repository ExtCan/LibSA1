/*
 * sa1_follower.c - the follower API (SA1_API_FOLLOWER, round 0.9.8; sadc docs/research/gsa1_098_plan.md 5.3, 6.2,
 * gsa1_096_plan.md 5.3, 6.1, follow096_lib.md 5). Owner: 098-lib-follow. The world side is src/world/sa1_world_follower.c.
 *
 * Layering: plain C99 with the asset-layer flags (-std=c99 -Iinclude -Isrc; MSVC through CMake with SA1_NO_WORLD), as
 * sa1_face.c and sa1_board.c: libsa1.h, sa1_internal.h, sa1_api_world.h only, never world/sa1_world.h.
 *
 * The gate (gsa1_098_plan.md 3.2): the optimised archives G-SA1 snapshots answer the contract's text until SA1FO_BUILT is 1
 * (set right before the lane's final gate); the lane's own tests run the -O0 build. Internal boots, world code and test hooks
 * are never gated: FOLLOW and FOLLOWC run on the optimised archives.
 */
#include "sa1_internal.h"
#include "sa1_api_world.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define SA1FO_BUILT 1
#if defined(__OPTIMIZE__) && !SA1FO_BUILT
#define SA1FO_GATED 1
#else
#define SA1FO_GATED 0
#endif
#define SA1FO_NOT_BUILT "followers are not built yet"

/* round 0.9.9 (gsa1_099_plan.md 5.4 S1): sa1_follower_speed and sa1_follower_spot; 099-lib-follow lands their bodies and sets this to 1 right before its final gate */
#define SA1FO2_BUILT 1
#if defined(__OPTIMIZE__) && !SA1FO2_BUILT
#define SA1FO2_GATED 1
#else
#define SA1FO2_GATED 0
#endif
#define SA1FO2_NOT_BUILT "follower speed and spots are not built yet"

#define FO_KIND_BIT(k) (1u << (k))
/* the kinds whose bring-up passed (tests/follower_test.c --long; gsa1_098_plan.md 6.2): a kind without its bit has its reason */
#define FO_KINDS (FO_KIND_BIT(SA1_CHAR_TAILS) | FO_KIND_BIT(SA1_CHAR_SONIC) | FO_KIND_BIT(SA1_CHAR_KNUCKLES) | \
                  FO_KIND_BIT(SA1_CHAR_GAMMA) | FO_KIND_BIT(SA1_CHAR_AMY) | FO_KIND_BIT(SA1_CHAR_BIG))

static int fo_finite3(const float *v)
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

/* the arguments of a leader state: struct_size covers kind, every float finite, flags known, kind -1 or a character */
static const char *fo_leader_bad(const sa1_leader_state *s)
{
    if (s == NULL || s->struct_size < offsetof(sa1_leader_state, kind) + sizeof s->kind) {
        return "sa1_leader_state: NULL or struct_size too small";
    }
    if (!fo_finite3(s->position) || !fo_finite3(s->velocity) || !isfinite(s->yaw)) {
        return "sa1_leader_state: a value is not finite";
    }
    if (s->flags & ~SA1_LEADER_ON_GROUND) {
        return "sa1_leader_state: unknown flags";
    }
    if (s->kind < -1 || s->kind >= SA1_CHAR_COUNT) {
        return "sa1_leader_state: kind is not a character";
    }
    return NULL;
}

uint32_t sa1_follower_kinds(void)
{
    return SA1FO_GATED ? 0u : FO_KINDS;
}

const char *sa1_follower_kind_reason(int32_t kind)
{
    if (SA1FO_GATED) {
        return SA1FO_NOT_BUILT;
    }
    if (kind >= 0 && kind < SA1_CHAR_COUNT && (FO_KINDS & FO_KIND_BIT(kind))) {
        return NULL;
    }
    switch (kind) {
    case SA1_CHAR_SUPER_SONIC: return "Super Sonic cannot follow: a Super follower is not a configuration of the game";
    case SA1_CHAR_EGGMAN: return "Eggman cannot follow: the game has no follower modes in his code";
    case SA1_CHAR_TIKAL: return "Tikal cannot follow: the game has no follower modes in her code";
    default: return "this is not a character";
    }
}

#if !defined(SA1_NO_WORLD)
static struct sa1g_world *fo_world(const sa1_context *ctx, sa1_character_id f)
{
    struct sa1g_world *w = NULL;
    if (sa1w_character_peek(ctx, f, NULL, NULL, &w) != SA1_OK || !sa1w_character_is_follower(ctx, f)) {
        return NULL;
    }
    return w;
}
#endif

sa1_status sa1_follower_create(sa1_context *ctx, const sa1_follower_desc *d, sa1_character_id *out_id)
{
    const char *bad;
    int32_t lkind = -1;
    uint32_t lflags = 0;
    if (out_id != NULL) {
        *out_id = 0;
    }
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (d == NULL || d->struct_size < offsetof(sa1_follower_desc, num_upgrade_flags) + sizeof d->num_upgrade_flags
        || out_id == NULL) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_create: NULL or struct_size too small");
    }
    if (d->flags & ~SA1_FOLLOW_KNOWN_FLAGS) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_desc: unknown flags 0x%X", (unsigned)d->flags);
    }
    if ((d->flags & SA1_FOLLOW_UPGRADES) && d->num_upgrade_flags > 32u) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_desc: num_upgrade_flags %u (at most 32)",
                        (unsigned)d->num_upgrade_flags);
    }
    if (d->leader != 0) {
        if (sa1w_character_peek(ctx, d->leader, &lkind, &lflags, NULL) != SA1_OK) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_desc: leader %d is not a character of this context",
                            (int)d->leader);
        }
    } else if ((bad = fo_leader_bad(&d->host_leader)) != NULL) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s", bad);
    }
    if (SA1FO_GATED) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, SA1FO_NOT_BUILT);
    }
    if (d->kind < 0 || d->kind >= SA1_CHAR_COUNT || !(FO_KINDS & FO_KIND_BIT(d->kind))) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "%s", sa1_follower_kind_reason(d->kind));
    }
    if (d->leader != 0 && (lflags & SA1_CREATE_PUPPET)) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "the leader %d is a puppet (SA1_CREATE_PUPPET): it has no "
                          "world to follow", (int)d->leader);
    }
#if defined(SA1_NO_WORLD)
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no followers");
#else
    if (sa1w_followers_count(ctx) >= SA1_FOLLOWER_MAX) {
        return sa1_fail(ctx, SA1_ERR_LIMIT, "sa1_follower_create: %u followers already (SA1_FOLLOWER_MAX)", SA1_FOLLOWER_MAX);
    }
    {
        sa1_character_desc cd;
        struct sa1w_follow_create fc;
        uint32_t i;
        memset(&cd, 0, sizeof cd);
        cd.struct_size = sizeof cd;
        cd.kind = d->kind;
        cd.flags = SA1_CREATE_WORLD;
        memset(&fc, 0, sizeof fc);
        fc.flags = d->flags;
        fc.leader = d->leader;
        if (d->flags & SA1_FOLLOW_SEED) {
            cd.flags |= SA1_CREATE_SEED;
            cd.seed = d->seed;
        }
        if (d->flags & SA1_FOLLOW_UPGRADES) {
            for (i = 0; i < d->num_upgrade_flags; i++) {
                if (d->upgrade_flags[i] >= 512u) {
                    return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_desc: upgrade flag %u (the game's event flags, below 512)",
                                    (unsigned)d->upgrade_flags[i]);
                }
            }
        }
        if (d->leader != 0) {
            sa1_state s;
            sa1_status st = sa1w_character_world_ensure(ctx, d->leader, &fc.leader_world);
            if (SA1_FAILED(st) || fc.leader_world == NULL) {
                return SA1_FAILED(st) ? st : SA1_ERR_NOT_LOADED;
            }
            fc.leader_kind = lkind;
            memset(&s, 0, sizeof s);
            s.struct_size = sizeof s;
            st = sa1_character_state(ctx, d->leader, &s);
            if (SA1_FAILED(st)) {
                return st;
            }
            memcpy(cd.position, s.position, sizeof cd.position);
            cd.yaw = s.yaw;
        } else {
            fc.host = d->host_leader;
            fc.leader_kind = d->host_leader.kind;
            memcpy(cd.position, d->host_leader.position, sizeof cd.position);
            cd.yaw = d->host_leader.yaw;
        }
        {
            sa1_status cs = sa1w_character_create_follower(ctx, &cd, &fc, out_id);
            if (!SA1_FAILED(cs) && (d->flags & SA1_FOLLOW_UPGRADES)) {
                /* PInitialize reads them at the first step of slot 1 (the game's upgrade flags are above 127: the world's own bytes) */
                struct sa1g_world *w = NULL;
                sa1w_character_peek(ctx, *out_id, NULL, NULL, &w);
                for (i = 0; w != NULL && i < d->num_upgrade_flags; i++) {
                    sa1wf_event_flag(w, d->upgrade_flags[i]);
                }
            }
            return cs;
        }
    }
#endif
}

sa1_status sa1_follower_set_leader(sa1_context *ctx, sa1_character_id f, sa1_character_id leader)
{
    sa1_status st;
    uint32_t lflags = 0;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1w_character_peek(ctx, f, NULL, NULL, NULL);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "sa1_follower_set_leader: no character %d", (int)f);
    }
    if (leader != 0 && sa1w_character_peek(ctx, leader, NULL, &lflags, NULL) != SA1_OK) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_set_leader: leader %d is not a character of this context",
                        (int)leader);
    }
    if (SA1FO_GATED) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, SA1FO_NOT_BUILT);
    }
#if defined(SA1_NO_WORLD)
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no followers");
#else
    {
        struct sa1g_world *w = fo_world(ctx, f), *lw = NULL;
        if (w == NULL) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_set_leader: character %d is not a follower", (int)f);
        }
        if (leader != 0) {
            if (lflags & SA1_CREATE_PUPPET) {
                return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "the leader %d is a puppet (SA1_CREATE_PUPPET): it has "
                                  "no world to follow", (int)leader);
            }
            if (sa1w_follow_cycle(ctx, f, leader)) {
                return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "a follower cannot follow itself or one of its own followers");
            }
            st = sa1w_character_world_ensure(ctx, leader, &lw);
            if (SA1_FAILED(st) || lw == NULL) {
                return SA1_FAILED(st) ? st : SA1_ERR_NOT_LOADED;
            }
        }
        sa1wf_set_leader(w, lw, leader);
        return SA1_OK;
    }
#endif
}

sa1_status sa1_follower_feed(sa1_context *ctx, sa1_character_id f, const sa1_leader_state *s)
{
    sa1_status st;
    const char *bad;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if ((bad = fo_leader_bad(s)) != NULL) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s", bad);
    }
    st = sa1w_character_peek(ctx, f, NULL, NULL, NULL);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "sa1_follower_feed: no character %d", (int)f);
    }
    if (SA1FO_GATED) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, SA1FO_NOT_BUILT);
    }
#if defined(SA1_NO_WORLD)
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no followers");
#else
    {
        struct sa1g_world *w = fo_world(ctx, f);
        if (w == NULL) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_feed: character %d is not a follower", (int)f);
        }
        return sa1wf_feed(ctx, w, s);
    }
#endif
}

sa1_status sa1_follower_respawn(sa1_context *ctx, sa1_character_id f)
{
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1w_character_peek(ctx, f, NULL, NULL, NULL);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "sa1_follower_respawn: no character %d", (int)f);
    }
    if (SA1FO_GATED) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, SA1FO_NOT_BUILT);
    }
#if defined(SA1_NO_WORLD)
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no followers");
#else
    {
        struct sa1g_world *w = fo_world(ctx, f);
        if (w == NULL) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_respawn: character %d is not a follower", (int)f);
        }
        st = sa1wf_respawn(w);
        return st == SA1_OK ? SA1_OK : sa1_fail(ctx, st, "sa1_follower_respawn: %s", sa1w_world_fault(w));
    }
#endif
}

sa1_status sa1_follower_hold(sa1_context *ctx, sa1_character_id f, int on)
{
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1w_character_peek(ctx, f, NULL, NULL, NULL);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "sa1_follower_hold: no character %d", (int)f);
    }
    if (SA1FO_GATED) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, SA1FO_NOT_BUILT);
    }
#if defined(SA1_NO_WORLD)
    (void)on;
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no followers");
#else
    {
        struct sa1g_world *w = fo_world(ctx, f);
        if (w == NULL) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_hold: character %d is not a follower", (int)f);
        }
        st = sa1wf_hold(w, on);
        return st == SA1_OK ? SA1_OK : sa1_fail(ctx, st, "sa1_follower_hold: %s", sa1w_world_fault(w));
    }
#endif
}

sa1_status sa1_follower_speed(sa1_context *ctx, sa1_character_id f, float speed)
{
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1w_character_peek(ctx, f, NULL, NULL, NULL);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "sa1_follower_speed: no character %d", (int)f);
    }
#if defined(SA1_NO_WORLD)
    (void)speed;
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no followers");
#else
    if (fo_world(ctx, f) == NULL) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_speed: character %d is not a follower", (int)f);
    }
    if (!(speed >= 1.0f && speed <= SA1_FOLLOWER_SPEED_MAX)) {     /* a NaN and the infinities fail too */
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_speed: speed must be finite, 1 .. %g", (double)SA1_FOLLOWER_SPEED_MAX);
    }
    if (SA1FO2_GATED) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, SA1FO2_NOT_BUILT);
    }
    {
        struct sa1g_world *w = fo_world(ctx, f);
        st = sa1wf_speed(w, speed);
        return st == SA1_OK ? SA1_OK : sa1_fail(ctx, st, "sa1_follower_speed: %s", sa1w_world_fault(w));
    }
#endif
}

sa1_status sa1_follower_spot(sa1_context *ctx, sa1_character_id f, const float offset[3])
{
    sa1_status st;
    if (ctx == NULL || offset == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1w_character_peek(ctx, f, NULL, NULL, NULL);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "sa1_follower_spot: no character %d", (int)f);
    }
#if defined(SA1_NO_WORLD)
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no followers");
#else
    if (fo_world(ctx, f) == NULL) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_spot: character %d is not a follower", (int)f);
    }
    if (!isfinite(offset[0]) || !isfinite(offset[1]) || !isfinite(offset[2])) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_spot: the offset must be finite");
    }
    if (SA1FO2_GATED) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, SA1FO2_NOT_BUILT);
    }
    {
        struct sa1g_world *w = fo_world(ctx, f);
        st = sa1wf_spot(ctx, w, offset);
        return st == SA1_OK ? SA1_OK : sa1_fail(ctx, st, "sa1_follower_spot: %s", sa1w_world_fault(w));
    }
#endif
}

/* round 0.9.9a (N7, sadc build/research/r099a/stop/diagnosis.md). on: a follower that stops reaches the game's own stand and idle; off, as if never called: the game's AI, whose stick keeps it in its walk at speed 0 on one frame (the real game's Tails does that) */

sa1_status sa1_follower_stand(sa1_context *ctx, sa1_character_id f, int on)
{
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1w_character_peek(ctx, f, NULL, NULL, NULL);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "sa1_follower_stand: no character %d", (int)f);
    }
#if defined(SA1_NO_WORLD)
    (void)on;
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no followers");
#else
    {
        struct sa1g_world *w = fo_world(ctx, f);
        if (w == NULL) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_follower_stand: character %d is not a follower", (int)f);
        }
        st = sa1wf_stand(w, on);
        return st == SA1_OK ? SA1_OK : sa1_fail(ctx, st, "sa1_follower_stand: %s", sa1w_world_fault(w));
    }
#endif
}

/* a const call: sa1_last_error is not touched */
sa1_status sa1_follower_info_get(const sa1_context *ctx, sa1_character_id f, sa1_follower_info *out)
{
    sa1_status st;
    if (ctx == NULL || out == NULL || out->struct_size < sizeof(uint32_t)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1w_character_peek(ctx, f, NULL, NULL, NULL);
    if (st != SA1_OK) {
        return st;
    }
    if (SA1FO_GATED) {
        return SA1_ERR_UNSUPPORTED;
    }
#if defined(SA1_NO_WORLD)
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    {
        const struct sa1g_world *w = fo_world(ctx, f), *lw = NULL;
        sa1_follower_info o;
        if (w == NULL) {
            return SA1_ERR_INVALID_ARGUMENT;
        }
        if (sa1wf_leader_id(w) != 0) {
            struct sa1g_world *l = NULL;
            if (sa1w_character_peek(ctx, sa1wf_leader_id(w), NULL, NULL, &l) == SA1_OK) {
                lw = l;
            }
        }
        memset(&o, 0, sizeof o);
        o.struct_size = sizeof o;
        sa1wf_info(ctx, w, lw, &o);
        sa1_copy_out(out, &o, sizeof o);
        return SA1_OK;
    }
#endif
}
