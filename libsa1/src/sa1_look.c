/*
 * sa1_look.c - the head look, the API side (SA1_API_LOOK, round 0.9.8; sadc docs/research/gsa1_098_plan.md 5.3, 6.3 part 2,
 * gsa1_096_plan.md 6.2, look096.md 7.1): sa1_character_look and sa1_character_set_option's SA1_COPT_SUPER_LOOK - the arguments, the
 * character's kind, building its world (as sa1_character_face), the refusals in plain words (logged at DEBUG through sa1_refuse: a refusal
 * is the call's normal answer). Owned by 098-lib-lookcam from lanes/lib_contract.txt (gsa1_098_plan.md 5.2). Plain C99 with the
 * asset-layer flags (no world headers); the lane inside the world is src/world/sa1_world_look.c, through src/world/sa1_world_look.h.
 *
 * Gate (gsa1_098_plan.md 3.2): SA1LK_BUILT 0: in the optimised archives both calls answer SA1_ERR_UNSUPPORTED "the head look is not built
 * yet" after their argument checks. The -O0 build runs the real code for the lane's own tests.
 *
 * Kinds (look096.md 3.1): Sonic, Tails, Knuckles, Amy, Big and Super Sonic have a look task in a libsa1 world (Super Sonic: Sonic's, on
 * Sonic's normal head, which the Super body does not draw: option 7); E-102's is never created in the game; Eggman's main makes none; Tikal's look task is not wired.
 */
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "world/sa1_world_look.h"

#include <math.h>
#include <stddef.h>

#define SA1LK_BUILT 1
#if defined(__OPTIMIZE__) && !SA1LK_BUILT
#define SA1LK_GATED 1
#else
#define SA1LK_GATED 0
#endif
#define SA1LK_NOT_BUILT "the head look is not built yet"

static int lk_float_ok(float f)
{
    return isfinite(f) && f <= 1e30f && f >= -1e30f;
}

/* the plain words for a kind without a look (NULL: it has one) */
static const char *lk_kind_refusal(int32_t kind)
{
    switch (kind) {
    case SA1_CHAR_SONIC:
    case SA1_CHAR_TAILS:
    case SA1_CHAR_KNUCKLES:
    case SA1_CHAR_AMY:
    case SA1_CHAR_BIG:
    case SA1_CHAR_SUPER_SONIC:
        return NULL;
    case SA1_CHAR_GAMMA:
        return "E-102 has no look in the game";
    case SA1_CHAR_TIKAL:
        return "Tikal has no look in libsa1 yet";
    default:
        return "this character has no look in libsa1";
    }
}

sa1_status sa1_character_look(sa1_context *ctx, sa1_character_id id, const sa1_look *look)
{
    sa1_status st;
    int32_t kind = -1;
    uint32_t cflags = 0;
    const char *no;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (look == NULL || look->struct_size < offsetof(sa1_look, pitch) + sizeof look->pitch) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_look: NULL or struct_size too small");
    }
    if (look->kind > SA1_LOOK_CAMERA) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_look: kind %u (SA1_LOOK_OFF / POINT / ANGLES / CAMERA)",
                        (unsigned)look->kind);
    }
    if (look->flags & ~SA1_LOOKF_KNOWN) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_look: unknown flags 0x%X", (unsigned)look->flags);
    }
    if ((look->flags & SA1_LOOKF_OVERRIDE_GAME) && (look->kind == SA1_LOOK_OFF || look->kind == SA1_LOOK_CAMERA)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_look: SA1_LOOKF_OVERRIDE_GAME takes POINT or ANGLES");
    }
    if (!lk_float_ok(look->point[0]) || !lk_float_ok(look->point[1]) || !lk_float_ok(look->point[2])
        || !lk_float_ok(look->yaw) || !lk_float_ok(look->pitch)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_look: a value is not finite or above 1e30 in magnitude");
    }
    st = sa1w_character_peek(ctx, id, &kind, &cflags, NULL);
    if (st != SA1_OK) {
        return st == SA1_ERR_NOT_FOUND ? sa1_fail(ctx, st, "sa1_character_look: no character %d", (int)id) : st;
    }
    if (SA1LK_GATED) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, SA1LK_NOT_BUILT);
    }
    no = lk_kind_refusal(kind);
    if (no != NULL) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "%s", no);
    }
    if (cflags & SA1_CREATE_PUPPET) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "character %d is a puppet (SA1_CREATE_PUPPET): it has no world", (int)id);
    }
#if defined(SA1_NO_WORLD)
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no look");
#else
    {
        struct sa1g_world *w = NULL;
        char why[SA1WL_WHY_MAX];
        st = sa1w_character_world_ensure(ctx, id, &w);
        if (SA1_FAILED(st) || w == NULL) {
            return SA1_FAILED(st) ? st : SA1_ERR_NOT_LOADED;
        }
        st = sa1wl_request(ctx, w, look, why, sizeof why);
        if (st == SA1_ERR_UNSUPPORTED) {
            return sa1_refuse(ctx, st, SA1_LOG_DEBUG, "%s", why[0] ? why : "no look here");
        }
        if (SA1_FAILED(st)) {
            return sa1_fail(ctx, st, "sa1_character_look: %s", why[0] ? why : sa1w_world_fault(w));
        }
        return SA1_OK;
    }
#endif
}

/* sa1_character_set_option(id, SA1_COPT_SUPER_LOOK, value): src/sa1_hostobj.c's routing */
sa1_status sa1_look_set_super(sa1_context *ctx, sa1_character_id id, int32_t value)
{
    sa1_status st;
    int32_t kind = -1;
    uint32_t cflags = 0;
    st = sa1w_character_peek(ctx, id, &kind, &cflags, NULL);
    if (st != SA1_OK) {
        return st;
    }
    if (SA1LK_GATED) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, SA1LK_NOT_BUILT);
    }
    if (kind != SA1_CHAR_SUPER_SONIC && !(kind == SA1_CHAR_SONIC && (cflags & SA1_CREATE_SUPER_CAPABLE))) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "SA1_COPT_SUPER_LOOK: only Super Sonic and a Sonic created with "
                          "SA1_CREATE_SUPER_CAPABLE");
    }
#if defined(SA1_NO_WORLD)
    (void)value;
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "this libsa1 build has no world core (SA1_NO_WORLD): no look");
#else
    {
        struct sa1g_world *w = NULL;
        char why[SA1WL_WHY_MAX];
        st = sa1w_character_world_ensure(ctx, id, &w);
        if (SA1_FAILED(st) || w == NULL) {
            return SA1_FAILED(st) ? st : SA1_ERR_NOT_LOADED;
        }
        st = sa1wl_super_option(ctx, w, value, why, sizeof why);
        if (st == SA1_ERR_UNSUPPORTED) {
            return sa1_refuse(ctx, st, SA1_LOG_DEBUG, "%s", why[0] ? why : "no look here");
        }
        if (SA1_FAILED(st)) {
            return sa1_fail(ctx, st, "SA1_COPT_SUPER_LOOK: %s", why[0] ? why : sa1w_world_fault(w));
        }
        return SA1_OK;
    }
#endif
}
