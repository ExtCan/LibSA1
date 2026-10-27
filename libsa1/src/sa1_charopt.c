/*
 * sa1_charopt.c - the per-character settings of the world layer (round 0.9.1, API 0.3 tail; include/libsa1.h
 * "lives, companions, game options"; sadc docs/research/gsa1_070_drown.md E2, gsa1_070_bird.md F1 with its Review R.4,
 * gsa1_rounds_plan.md 14.1h): sa1_character_set_lives, sa1_character_set_companion / sa1_character_companion and
 * sa1_character_set_game_option.
 *
 * Asset layer (plain C99, no world headers): the context keeps every setting by character slot (sa1_api_ext.charopt,
 * src/sa1_api_world.h) and hands it to the character's world at once when there is one (the bridge,
 * src/world/sa1_world_charopt.c sa1w_world_charopt); a world built later gets it from its attach hook (sa1ho_world_attach ->
 * sa1g_charopt_attach) before its first step. A build without the world core (SA1_NO_WORLD) stores them only.
 */
#include "sa1_internal.h"
#include "sa1_api_world.h"

#include <string.h>

/* the record of a live character (NULL: no such character), fresh when the slot held another id */
static sa1w_charopt *charopt_of(sa1_context *ctx, sa1_character_id id, int32_t *kind)
{
    uint32_t slot = ((uint32_t)id & 0xFFFFu) - 1u, gen = ((uint32_t)id >> 16) & 0x7FFFu;
    sa1w_charopt *o;
    if (ctx == NULL || id <= 0 || slot >= SA1_MAX_INSTANCES || ctx->inst[slot] == NULL || ctx->inst_gen[slot] != gen) {
        return NULL;
    }
    if (kind != NULL) {
        *kind = ctx->inst[slot]->kind;
    }
    o = &sa1_api_ext_of(ctx)->charopt[slot];
    if (o->id != id) {
        memset(o, 0, sizeof *o);
        o->id = id;
    }
    return o;
}

#if !defined(SA1_NO_WORLD)
static sa1_status charopt_apply(sa1_context *ctx, sa1_character_id id, const sa1w_charopt *o, uint32_t what)
{
    struct sa1g_world *w = sa1_character_world(ctx, id);
    return w != NULL ? sa1w_world_charopt(ctx, w, o, what) : SA1_OK;
}
#endif

sa1_status sa1_character_set_lives(sa1_context *ctx, sa1_character_id id, int32_t lives)
{
    sa1w_charopt *o;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    o = charopt_of(ctx, id, NULL);
    if (o == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    o->lives = lives < 0 ? 0 : lives > 127 ? 127 : lives;
    o->set |= SA1W_SET_LIVES;
#if !defined(SA1_NO_WORLD)
    return charopt_apply(ctx, id, o, SA1W_SET_LIVES);
#else
    return SA1_OK;
#endif
}

sa1_status sa1_character_set_companion(sa1_context *ctx, sa1_character_id id, uint32_t companion, int32_t on)
{
    sa1w_charopt *o;
    int32_t kind = -1;
    uint32_t bit;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    o = charopt_of(ctx, id, &kind);
    if (o == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    if (companion != SA1_COMPANION_BIRD) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_character_set_companion: unknown companion %u",
                        (unsigned)companion);
    }
    if (kind != SA1_CHAR_AMY) {
        /* the game: 8C0DEDC0 makes the bird only when GetPlayerNumber() == 5 */
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_character_set_companion: Amy's bird is Amy's only "
                        "(character %d is kind %d)", (int)id, (int)kind);
    }
    bit = 1u << companion;
    o->known |= bit;
    o->companions = on ? (o->companions | bit) : (o->companions & ~bit);
    o->set |= SA1W_SET_COMPANIONS;
#if !defined(SA1_NO_WORLD)
    return charopt_apply(ctx, id, o, SA1W_SET_COMPANIONS);
#else
    return SA1_OK;
#endif
}

sa1_status sa1_character_companion(const sa1_context *ctx, sa1_character_id id, uint32_t companion, int32_t *present)
{
    uint32_t slot = ((uint32_t)id & 0xFFFFu) - 1u, gen = ((uint32_t)id >> 16) & 0x7FFFu;
    if (present != NULL) {
        *present = 0;
    }
    if (ctx == NULL || present == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (id <= 0 || slot >= SA1_MAX_INSTANCES || ctx->inst[slot] == NULL || ctx->inst_gen[slot] != gen) {
        return SA1_ERR_NOT_FOUND;
    }
    if (companion != SA1_COMPANION_BIRD) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
#if !defined(SA1_NO_WORLD)
    {
        struct sa1g_world *w = sa1_character_world(ctx, id);
        return w != NULL ? sa1w_world_companion(w, companion, present) : SA1_OK;
    }
#else
    return SA1_OK;
#endif
}

sa1_status sa1_character_set_game_option(sa1_context *ctx, sa1_character_id id, uint32_t option, int32_t value)
{
    sa1w_charopt *o;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    o = charopt_of(ctx, id, NULL);
    if (o == NULL) {
        return SA1_ERR_NOT_FOUND;
    }
    if (option != SA1_GOPT_TAILS_SNAP_TAILS && option != SA1_GOPT_SUPER_IDLE_BY_FORM && option != SA1_GOPT_SUPER_AURA_ANIM &&
        option != SA1_GOPT_ITEM_ICON_LIMIT) {     /* round 0.9.4: the item icon list's bound */
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_character_set_game_option: unknown option %u",
                        (unsigned)option);
    }
    if (option == SA1_GOPT_SUPER_AURA_ANIM && (value < SA1_AURA_ANIM_DREAMCAST || value > SA1_AURA_ANIM_SLOW)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_GOPT_SUPER_AURA_ANIM: value %d (0 Dreamcast, 1 calm, 2 slow)",
                        (int)value);
    }
    o->gopts = value ? (o->gopts | (1u << option)) : (o->gopts & ~(1u << option));
    o->gopt_value[option] = value;     /* round 0.9.3a: SA1_GOPT_SUPER_AURA_ANIM's value (the others are switches) */
    o->set |= SA1W_SET_GOPTS;
#if !defined(SA1_NO_WORLD)
    return charopt_apply(ctx, id, o, SA1W_SET_GOPTS);
#else
    return SA1_OK;
#endif
}
