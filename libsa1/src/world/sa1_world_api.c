/*
 * sa1_world_api.c - per-context world state (sa1g_wctx: the pristine image of each character kind, composed once
 * per context, and the shared collision segment) and the internal character-over-world sketch used by the world
 * tests (sadc docs/libsa1_integration.md 6.1, 6.7, 6.8, D2).
 *
 * The public API (include/libsa1.h: sa1_character_create[_ex], sa1_character_tick, sa1_character_state[_ex],
 * sa1_poll_events) goes through src/sa1_character.c and the bridge src/world/sa1_api_world.c, which use the
 * sa1g_wctx functions below. The sa1g_character_world_* / sa1g_world_state entry points are the bridge's
 * predecessor, kept for tests/spike_test.c and tests/world_boot_test.c; they follow the public API's conventions:
 *   yaw      host yaw (radians, counter-clockwise about the up axis, 0 = SA1 +X) <-> taskwk.ang.y (BAMS): the
 *            game moves along (cos a, 0, sin a) for ang.y = a, a clockwise turn seen from +Y, so ang.y = -yaw
 *            (rounded to the nearest BAMS) and yaw = -(Sint16)ang.y (sa1_character.c yaw_to_bams / bams_to_yaw);
 *   camera   the camera adapter's ang.y = round(camera_yaw / 2 pi * 65536) - 0x4000: 8C05117E turns the stick
 *            by -camera ang.y and forward (stick y < 0) is -0x4000, so a forward stick follows the host camera yaw
 *            (sa1_api_world.c sa1w_world_tick); a non-finite camera_yaw leaves the camera as it was.
 */
#include "sa1_world.h"
#include "sa1_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define SA1G_TWO_PI 6.28318530717958647692

sa1_status sa1g_wctx_init(sa1g_wctx *c, sa1_context *ctx, const char *gdi_path)
{
    if (c == NULL || ctx == NULL || gdi_path == NULL || strlen(gdi_path) >= sizeof c->gdi) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    memset(c, 0, sizeof *c);
    c->ctx = ctx;
    snprintf(c->gdi, sizeof c->gdi, "%s", gdi_path);
    if (sa1g_shared_create(&c->shared) != 0) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    return SA1_OK;
}

void sa1g_wctx_free(sa1g_wctx *c)
{
    int32_t k;
    if (c == NULL) {
        return;
    }
    for (k = 0; k < SA1_CHAR_COUNT; k++) {
        if (c->composed[k]) {
            sa1g_image_free(&c->images[k]);
        }
    }
    sa1g_shared_destroy(&c->shared);
    memset(c, 0, sizeof *c);
}

sa1_status sa1g_wctx_image(sa1g_wctx *c, int32_t kind, const sa1g_image **out)
{
    sa1_status st;
    *out = NULL;
    if (c == NULL || kind < 0 || kind >= SA1_CHAR_COUNT) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (!c->composed[kind]) {
        st = sa1g_image_compose(c->ctx, c->gdi, kind, 0, &c->images[kind]);
        if (st != SA1_OK) {
            return st;
        }
        c->composed[kind] = 1;
    }
    *out = &c->images[kind];
    return SA1_OK;
}

/* host yaw -> taskwk.ang.y (sa1_character.c yaw_to_bams) */
static uint32_t sa1g_yaw_to_bams(float yaw)
{
    double t = -(double)yaw / SA1G_TWO_PI * 65536.0;
    t = floor(t + 0.5);
    t = fmod(t, 65536.0);
    if (t < 0.0) {
        t += 65536.0;
    }
    return (uint32_t)t & 0xFFFFu;
}

/* taskwk.ang.y -> host yaw (sa1_character.c bams_to_yaw) */
static float sa1g_bams_to_yaw(uint32_t a)
{
    return (float)(-(double)(int16_t)(uint16_t)(a & 0xFFFFu) * (SA1G_TWO_PI / 65536.0));
}

/* host camera yaw -> the camera adapter's ang.y (sa1_api_world.c sa1w_world_tick) */
static uint32_t sa1g_camera_bams(float camera_yaw)
{
    double t = floor((double)camera_yaw / SA1G_TWO_PI * 65536.0 + 0.5);
    int64_t yb = (int64_t)fmod(t, 65536.0);
    return (uint32_t)(yb - 0x4000) & 0xFFFFu;
}

sa1_status sa1g_character_world_create(sa1g_wctx *c, int32_t kind, const float position[3], float yaw,
                                       sa1g_world **out)
{
    const sa1g_image *img;
    sa1g_world *w;
    sa1_status st;
    if (out == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (kind == SA1_CHAR_EGGMAN) {
        return SA1_ERR_UNSUPPORTED;     /* D14: Eggman is out of scope (stubbed) */
    }
    if (!isfinite(yaw)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1g_wctx_image(c, kind, &img);
    if (st != SA1_OK) {
        return st;
    }
    w = (sa1g_world *)sa1_mem_alloc(c->ctx, sizeof *w);
    if (w == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    if (sa1g_world_create(w, &c->shared) != 0) {
        sa1_mem_free(c->ctx, w);
        return sa1_fail(c->ctx, SA1_ERR_OUT_OF_MEMORY, "world: cannot reserve the 16 MB arena");
    }
    st = sa1g_world_apply_image(w, img);
    if (st != SA1_OK) {
        sa1g_world_destroy(w);
        sa1_mem_free(c->ctx, w);
        return st;
    }
    w->kind = kind;
    if (position != NULL) {
        sa1_from_host_point(c->ctx, position, w->start_pos);
    }
    w->start_ang_y = sa1g_yaw_to_bams(yaw);
    /* boot (6.2) and player creation (6.3): stage 0x0100, the window landtable for the collision
     * adapter, the API's start (or the game's start table without a position) */
    if (sa1g_game_ready()) {
        sa1g_boot_cfg cfg;
        sa1g_boot_defaults(&cfg);
        cfg.chr = kind;
        cfg.land_mode = SA1G_LAND_WINDOW;
        if (position != NULL) {
            cfg.flags |= SA1G_BOOT_POS;
            memcpy(cfg.pos, w->start_pos, sizeof cfg.pos);
            cfg.ang[1] = w->start_ang_y;
        }
        st = sa1g_character_main(kind) != 0 ? sa1g_world_boot(w, &cfg) : SA1_ERR_UNSUPPORTED;
        if (st != SA1_OK) {
            sa1_log(c->ctx, SA1_LOG_ERROR, "world: boot of character %d failed: %s %s", (int)kind,
                    sa1g_status_string(st), w->dead ? w->fault.msg : "");
            sa1g_world_destroy(w);
            sa1_mem_free(c->ctx, w);
            return st;
        }
    }
    *out = w;
    return SA1_OK;
}

void sa1g_character_world_destroy(sa1g_wctx *c, sa1g_world *w)
{
    if (c == NULL || w == NULL) {
        return;
    }
    sa1g_world_destroy(w);
    sa1_mem_free(c->ctx, w);
}

sa1_status sa1g_character_world_tick(sa1g_world *w, const sa1_input *input, float dt, sa1_state *state_out)
{
    sa1g_pad pad;
    uint32_t pairs = 0;
    sa1_status st;
    if (w == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (state_out != NULL && state_out->struct_size >= sizeof(uint32_t)) {
        uint32_t n = state_out->struct_size < sizeof *state_out ? state_out->struct_size : (uint32_t)sizeof *state_out;
        memset((uint8_t *)state_out + sizeof(uint32_t), 0, n - sizeof(uint32_t));
    }
    if (!sa1g_game_ready()) {
        return SA1_ERR_NOT_IMPLEMENTED;
    }
    w->replay = 0;
    sa1g_pad_from_input(input, &pad);
    sa1g_world_set_pad(w, 0, &pad);
    if (input != NULL && input->struct_size >= offsetof(sa1_input, camera_yaw) + sizeof(float)
        && isfinite(input->camera_yaw)) {
        w->camera.ang[1] = sa1g_camera_bams(input->camera_yaw);    /* 6.7 */
    }
    st = sa1g_world_tick(w, dt, 0, &pairs);
    if (state_out != NULL && state_out->struct_size >= sizeof(uint32_t)) {
        sa1g_world_state(w, state_out);
    }
    return st;
}

/* 7.1: twp->pos / ang.y, mwp->spd x 60 (per second), pwp->mj.action / nframe, in host space */
void sa1g_world_state(const sa1g_world *w, sa1_state *out)
{
    sa1g_player_ptrs pp;
    sa1_state s;
    float pos[3], spd[3];
    uint32_t angy = 0, k;
    uint16_t action = 0;
    const sa1_context *ctx = w->image != NULL ? w->image->ctx : NULL;
    if (out == NULL || out->struct_size < sizeof(uint32_t)) {
        return;
    }
    memset(&s, 0, sizeof s);
    s.struct_size = (uint32_t)sizeof s;
    sa1g_world_player(w, &pp);
    if (pp.twp != 0) {
        const uint8_t *twp = (const uint8_t *)sa1g_world_ptr(w, pp.twp);
        memcpy(pos, twp + 0x20, sizeof pos);
        memcpy(&angy, twp + 0x18, 4);
        if (ctx != NULL) {
            sa1_to_host_point(ctx, pos, s.position);
        } else {
            memcpy(s.position, pos, sizeof pos);
        }
        s.yaw = sa1g_bams_to_yaw(angy);
    }
    if (pp.mwp != 0) {
        memcpy(spd, (const uint8_t *)sa1g_world_ptr(w, pp.mwp) + 0x04, sizeof spd);
        for (k = 0; k < 3; k++) {
            spd[k] *= 60.0f;
        }
        if (ctx != NULL) {
            sa1_to_host_dir(ctx, spd, s.velocity);
        } else {
            memcpy(s.velocity, spd, sizeof spd);
        }
    }
    if (pp.pwp != 0) {
        const uint8_t *pwp = (const uint8_t *)sa1g_world_ptr(w, pp.pwp);
        memcpy(&action, pwp + 0x126, 2);
        memcpy(&s.frame, pwp + 0x130, 4);
        s.action = action;
    }
    k = out->struct_size;
    memcpy((uint8_t *)out + sizeof(uint32_t), (const uint8_t *)&s + sizeof(uint32_t),
           (k < sizeof s ? k : (uint32_t)sizeof s) - sizeof(uint32_t));
}

sa1_status sa1g_character_world_poll_events(const sa1g_wctx *c, sa1g_world *w, sa1_event *out, uint32_t capacity,
                                            uint32_t *count)
{
    uint32_t n, i;
    if (count != NULL) {
        *count = 0;
    }
    if (c == NULL || w == NULL || (out == NULL && capacity != 0)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    n = sa1g_world_poll_events(w, out, capacity);
    for (i = 0; i < n; i++) {
        float p[3];
        memcpy(p, out[i].position, sizeof p);
        sa1_to_host_point(c->ctx, p, out[i].position);
    }
    if (count != NULL) {
        *count = n;
    }
    return SA1_OK;
}
