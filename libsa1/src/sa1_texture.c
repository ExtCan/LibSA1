/*
 * Texture queries and the per-archive texture atlas.
 */
#include "sa1_capture.h"

#include <stdlib.h>

#define ATLAS_PAD 2u
#define ATLAS_MAX 4096u

static const sa1_archive *get_archive(const sa1_context *ctx, int32_t kind, uint32_t archive, sa1_status *st)
{
    const sa1_charset *cs;
    if (!ctx || kind < 0 || kind >= SA1_CHAR_COUNT) {
        *st = SA1_ERR_INVALID_ARGUMENT;
        return NULL;
    }
    cs = &ctx->chars[kind];
    if (!cs->loaded) {
        *st = SA1_ERR_NOT_LOADED;
        return NULL;
    }
    if (archive >= cs->num_archives) {
        *st = SA1_ERR_NOT_FOUND;
        return NULL;
    }
    *st = SA1_OK;
    return &cs->archives[archive];
}

sa1_status sa1_texture_archive(const sa1_context *ctx, int32_t kind, uint32_t archive, const char **name,
                               uint32_t *num_textures)
{
    sa1_status st;
    const sa1_archive *ar = get_archive(ctx, kind, archive, &st);
    if (!ar)
        return st;
    if (name)
        *name = ar->name;
    if (num_textures)
        *num_textures = ar->num_textures;
    return SA1_OK;
}

/* the texture's sa1_texture_info with the pixels rgba (the texture's own, or one of its variants) */
static void texture_info_out(const sa1_texture *t, const uint8_t *rgba, sa1_texture_info *out)
{
    sa1_texture_info info;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.width = t->width;
    info.height = t->height;
    info.rgba = rgba;
    info.name = t->name;
    info.global_index = t->gbix;
    info.pixel_format = t->pixel_format;
    info.data_format = t->data_format;
    info.alpha = sa1_texture_alpha_kind(rgba, t->width, t->height);
    sa1_copy_out(out, &info, sizeof info);
}

sa1_status sa1_texture_get(const sa1_context *ctx, int32_t kind, uint32_t archive, uint32_t index,
                           sa1_texture_info *out)
{
    sa1_status st;
    const sa1_archive *ar = get_archive(ctx, kind, archive, &st);
    if (!out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    if (!ar)
        return st;
    if (index >= ar->num_textures)
        return SA1_ERR_NOT_FOUND;
    texture_info_out(&ar->tex[index], ar->tex[index].rgba, out);
    return SA1_OK;
}

/* ---- texture variants (round 0.9.4a; sadc docs/research/super2_plan.md 5.3 and 6.2 task 1, super2_lsd.md 4.3) ---------
 * SA1_TEXTURE_VARIANT_SUPER_GOLD ("T1, colourise"): the Light Speed charge's sparkle and streak textures lcharge01-04 of
 * Sonic's SON_EFF archive (the one-texture texlists 8C1C6A94 / 8C1C6A9C / 8C1C6AA4 / 8C1C6AAC point at its texnames 25-28)
 * with every texel's HSV value and saturation kept and the hue SA1_SUPER_GOLD_HUE (45 degrees: the weighted mean hue of the
 * game's own golden Super art, s1sonic01-08 and ss_ball_a / _b), alpha kept. Per texel, in double from the bytes:
 *   mx = max(r, g, b), mn = min(r, g, b) (V = mx, S x V = mx - mn); h = SA1_SUPER_GOLD_HUE / 60, i = floor(h), f = h - i;
 *   p = mn, q = mx - (mx - mn) f, t = mn + (mx - mn) f; (r', g', b') = HSV sector i: 0 (mx, t, p), 1 (q, mx, p),
 *   2 (p, mx, t), 3 (p, q, mx), 4 (t, p, mx), 5 (mx, p, q); each rounded to the nearest byte (halves up).
 * For 45 degrees: (mx, (3 mx + mn) / 4, mn). The game has no golden version of these textures (super2_lsd.md 3): the colour
 * is libsa1's, derived from the game's art, so every host gets the same gold; the capture marks the draws that use it
 * (SA1_DRAW_SUPER_GOLD, sa1_capture.c). Computed when Sonic (kind 0) is loaded with SA1_LOAD_SUPER_FORMS, found by the
 * archive's and the textures' names (not by fixed indices), stored in the texture (rgba_gold) in the charset's arena:
 * freed with the charset (sa1_charset_free), never written anywhere. 4 x 32 x 32 x 4 bytes on the owner's disc. */
static const char *const k_gold_textures[] = { "lcharge01", "lcharge02", "lcharge03", "lcharge04" };

static int name_is(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a, y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (x != y)
            return 0;
    }
    return *a == *b;
}

static uint8_t gold_byte(double x)
{
    if (!(x > 0.0))
        return 0;
    return x >= 255.0 ? 255 : (uint8_t)(x + 0.5);
}

void sa1_texture_gold_texel(const uint8_t in[4], uint8_t out[4])
{
    double mx = in[0], mn = in[0], h = (double)(((SA1_SUPER_GOLD_HUE % 360) + 360) % 360) / 60.0, f, q, t;
    int i;
    if (in[1] > mx) mx = in[1];
    if (in[2] > mx) mx = in[2];
    if (in[1] < mn) mn = in[1];
    if (in[2] < mn) mn = in[2];
    i = (int)h;
    f = h - (double)i;
    q = mx - (mx - mn) * f;
    t = mn + (mx - mn) * f;
    switch (i) {
    case 0:  out[0] = gold_byte(mx); out[1] = gold_byte(t);  out[2] = gold_byte(mn); break;
    case 1:  out[0] = gold_byte(q);  out[1] = gold_byte(mx); out[2] = gold_byte(mn); break;
    case 2:  out[0] = gold_byte(mn); out[1] = gold_byte(mx); out[2] = gold_byte(t);  break;
    case 3:  out[0] = gold_byte(mn); out[1] = gold_byte(q);  out[2] = gold_byte(mx); break;
    case 4:  out[0] = gold_byte(t);  out[1] = gold_byte(mn); out[2] = gold_byte(mx); break;
    default: out[0] = gold_byte(mx); out[1] = gold_byte(mn); out[2] = gold_byte(q);  break;
    }
    out[3] = in[3];
}

uint32_t sa1_texture_make_variants(sa1_context *ctx, sa1_charset *cs, int32_t kind, uint32_t load_flags)
{
    uint32_t a, i, k, made = 0;
    (void)ctx;
    if (!cs || kind != SA1_CHAR_SONIC || !(load_flags & SA1_LOAD_SUPER_FORMS))
        return 0;
    for (a = 0; a < cs->num_archives; a++) {
        sa1_archive *ar = &cs->archives[a];
        if (!name_is(ar->name, "SON_EFF"))
            continue;
        for (i = 0; i < ar->num_textures; i++) {
            sa1_texture *t = &ar->tex[i];
            size_t n = (size_t)t->width * t->height, p;
            for (k = 0; k < sizeof k_gold_textures / sizeof k_gold_textures[0]; k++)
                if (name_is(t->name, k_gold_textures[k]))
                    break;
            if (k == sizeof k_gold_textures / sizeof k_gold_textures[0] || !t->rgba || n == 0 || t->rgba_gold)
                continue;
            t->rgba_gold = (uint8_t *)sa1_arena_alloc(&cs->arena, n * 4u);
            if (!t->rgba_gold)
                continue;   /* out of memory: no variant (SA1_ERR_NOT_FOUND), the texture itself is unchanged */
            for (p = 0; p < n; p++)
                sa1_texture_gold_texel(t->rgba + 4u * p, t->rgba_gold + 4u * p);
            made++;
        }
    }
    return made;
}

sa1_status sa1_texture_variant_get(const sa1_context *ctx, int32_t kind, uint32_t archive, uint32_t index,
                                   uint32_t variant, sa1_texture_info *out)
{
    sa1_status st;
    const sa1_archive *ar;
    const sa1_texture *t;
    if (!ctx || !out || out->struct_size < sizeof(uint32_t) || kind < 0 || kind >= SA1_CHAR_COUNT)
        return SA1_ERR_INVALID_ARGUMENT;
    if (kind != SA1_CHAR_SONIC || variant != SA1_TEXTURE_VARIANT_SUPER_GOLD)
        return SA1_ERR_NOT_FOUND;
    ar = get_archive(ctx, kind, archive, &st);
    if (!ar)
        return st;
    if (index >= ar->num_textures)
        return SA1_ERR_NOT_FOUND;
    t = &ar->tex[index];
    if (!t->rgba_gold)
        return SA1_ERR_NOT_FOUND;
    texture_info_out(t, t->rgba_gold, out);
    return SA1_OK;
}

uint32_t sa1_charset_texture_alpha(const sa1_context *ctx, const sa1_charset *cs, int32_t archive, int32_t index)
{
    const sa1_texture *t;
    (void)ctx;
    if (!cs || archive < 0 || (uint32_t)archive >= cs->num_archives || index < 0 ||
        (uint32_t)index >= cs->archives[archive].num_textures)
        return SA1_TEXALPHA_NONE;
    t = &cs->archives[archive].tex[index];
    return sa1_texture_alpha_kind(t->rgba, t->width, t->height);
}

typedef struct atlas_item {
    uint32_t idx, w, h, x, y;
} atlas_item;

static int item_cmp(const void *pa, const void *pb)
{
    const atlas_item *a = (const atlas_item *)pa, *b = (const atlas_item *)pb;
    if (a->h != b->h) return a->h > b->h ? -1 : 1;
    if (a->w != b->w) return a->w > b->w ? -1 : 1;
    return a->idx < b->idx ? -1 : (a->idx > b->idx ? 1 : 0);
}

static uint32_t next_pow2(uint32_t v)
{
    uint32_t p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

/* Shelf-packs the items; chooses the smallest power-of-two width whose packed height
 * fits in a square of that width. */
static sa1_status pack(atlas_item *it, uint32_t n, uint32_t *out_w, uint32_t *out_h)
{
    uint32_t W;
    qsort(it, n, sizeof *it, item_cmp);
    for (W = 16; W <= ATLAS_MAX; W *= 2u) {
        uint32_t x = 0, y = 0, shelf = 0, i, ok = 1;
        for (i = 0; i < n && ok; i++) {
            uint32_t iw = it[i].w + 2u * ATLAS_PAD, ih = it[i].h + 2u * ATLAS_PAD;
            if (iw > W) {
                ok = 0;
                break;
            }
            if (x + iw > W) {
                y += shelf;
                x = 0;
                shelf = 0;
            }
            it[i].x = x + ATLAS_PAD;
            it[i].y = y + ATLAS_PAD;
            x += iw;
            if (ih > shelf)
                shelf = ih;
        }
        if (ok && y + shelf <= W) {
            *out_w = W;
            *out_h = next_pow2(y + shelf ? y + shelf : 1u);
            return SA1_OK;
        }
    }
    return SA1_ERR_LIMIT;
}

sa1_status sa1_texture_atlas(const sa1_context *ctx, int32_t kind, uint32_t archive, uint8_t *rgba_out,
                             uint32_t *width, uint32_t *height, sa1_atlas_rect *rects_out, uint32_t rects_capacity)
{
    sa1_status st;
    const sa1_archive *ar = get_archive(ctx, kind, archive, &st);
    atlas_item *it;
    uint32_t n = 0, i, W = 0, H = 0;
    if (!width || !height || (rects_capacity && !rects_out))
        return SA1_ERR_INVALID_ARGUMENT;
    if (!ar)
        return st;
    it = (atlas_item *)sa1_mem_array(ctx, ar->num_textures ? ar->num_textures : 1u, sizeof(atlas_item));
    if (!it)
        return SA1_ERR_OUT_OF_MEMORY;
    for (i = 0; i < ar->num_textures; i++) {
        if (!ar->tex[i].rgba)
            continue;
        it[n].idx = i;
        it[n].w = ar->tex[i].width;
        it[n].h = ar->tex[i].height;
        n++;
    }
    st = pack(it, n, &W, &H);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, it);
        return st;
    }
    if (rects_out) {
        uint32_t lim = rects_capacity < ar->num_textures ? rects_capacity : ar->num_textures;
        memset(rects_out, 0, (size_t)lim * sizeof *rects_out);
        for (i = 0; i < n; i++)
            if (it[i].idx < lim) {
                rects_out[it[i].idx].x = it[i].x;
                rects_out[it[i].idx].y = it[i].y;
                rects_out[it[i].idx].w = it[i].w;
                rects_out[it[i].idx].h = it[i].h;
            }
    }
    if (!rgba_out) {
        *width = W;
        *height = H;
        sa1_mem_free(ctx, it);
        return SA1_OK;
    }
    if (*width < W || *height < H) {
        *width = W;
        *height = H;
        sa1_mem_free(ctx, it);
        return SA1_ERR_BUFFER_TOO_SMALL;
    }
    {
        uint32_t pitch = *width;
        memset(rgba_out, 0, (size_t)pitch * (*height) * 4u);
        for (i = 0; i < n; i++) {
            const sa1_texture *t = &ar->tex[it[i].idx];
            int32_t gx, gy, w = (int32_t)t->width, h = (int32_t)t->height, pad = (int32_t)ATLAS_PAD;
            for (gy = -pad; gy < h + pad; gy++) {
                int32_t sy = gy < 0 ? 0 : (gy >= h ? h - 1 : gy);
                uint8_t *row = rgba_out + ((size_t)(it[i].y + (uint32_t)(gy + pad) - ATLAS_PAD) * pitch) * 4u;
                for (gx = -pad; gx < w + pad; gx++) {
                    int32_t sx = gx < 0 ? 0 : (gx >= w ? w - 1 : gx);
                    memcpy(row + ((size_t)(it[i].x + (uint32_t)(gx + pad) - ATLAS_PAD)) * 4u,
                           t->rgba + ((size_t)sy * (size_t)w + (size_t)sx) * 4u, 4);
                }
            }
        }
    }
    sa1_mem_free(ctx, it);
    return SA1_OK;
}