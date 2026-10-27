/*
 * texdump.c - writes every texture libsa1 decodes, and renders characters from sa1_character_render, as PNG files
 * (local inspection; nothing is committed: the output is under LIB/build/, which is ignored).
 *
 *   texdump [--gdi GDI] [--out DIR] [--char NAME ...] [--render] [--script S] [--size N]
 *
 *   GDI     the user's disc image (or $SA1_TEST_GDI); never copied
 *   DIR     default build/texdump (relative to the working directory)
 *   textures: DIR/<char>/<AA>_<ARCHIVE>/<II>_<name>.png and DIR/<char>/<AA>_<ARCHIVE>/index.txt
 *             ("index name gbix pf df w h file archive alpha", the format tests/texdump_ref.py reads)
 *   --render: DIR/render_<char>[_<label>].png: the character ticking in its world on Emerald Coast's own collision
 *             (DISC), drawn from the front, side and back with a small software rasterizer that follows the
 *             render-state rules of include/libsa1.h (opaque / alpha test with depth writes, blended draws in the
 *             game's order without depth writes, culling, clamp / mirror addressing, sprites facing the view, lines)
 *   --script S: the inputs before the snapshot, groups "<pairs><cmd>": n nothing, f forward, a action, b action +
 *             forward, j jump, J jump + forward, y action2 (default "20n")
 *
 * Verify the decoder with the independent reference: py -3.12 tests/texdump_ref.py --dump DIR
 *
 * Build (from LIB, after py -3.12 build.py --target x86_64-windows):
 *   py -3.12 -m ziglang cc -target x86_64-windows-gnu -std=gnu11 -Iinclude -Itests -O2 -DSA1_STATIC tests/texdump.c
 *       build/x86_64-windows/libsa1.a -o build/x86_64-windows/texdump.exe
 *   (or py -3.12 tests/build_capture.py)
 */
#include "libsa1.h"
#include "test_util.h"

#include <math.h>

/* ---- software rasterizer ---------------------------------------------------------------------------------- */
typedef struct rast {
    int w, h;
    uint8_t *rgb;               /* RGBA */
    float *z;                   /* larger = closer */
} rast;

typedef struct rtex {
    int w, h;
    const uint8_t *px;
} rtex;

static int rast_init(rast *r, int w, int h)
{
    int i;
    r->w = w;
    r->h = h;
    r->rgb = (uint8_t *)malloc((size_t)w * h * 4u);
    r->z = (float *)malloc((size_t)w * h * sizeof(float));
    if (!r->rgb || !r->z)
        return -1;
    for (i = 0; i < w * h; i++) {
        r->z[i] = -1e30f;
        r->rgb[4 * i] = 58;
        r->rgb[4 * i + 1] = 66;
        r->rgb[4 * i + 2] = 78;
        r->rgb[4 * i + 3] = 255;
    }
    return 0;
}

static int wrap(int x, int n, int clamp, int mirror)
{
    if (clamp)
        return x < 0 ? 0 : x >= n ? n - 1 : x;
    if (mirror) {
        int p = 2 * n;
        x = ((x % p) + p) % p;
        return x >= n ? p - 1 - x : x;
    }
    return ((x % n) + n) % n;
}

static void sample(const rtex *t, float u, float v, uint32_t mat, float out[4])
{
    int x, y;
    const uint8_t *q;
    if (!t || !t->px) {
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        return;
    }
    x = wrap((int)floorf(u * (float)t->w), t->w, (mat & SA1_MAT_CLAMP_U) != 0, (mat & SA1_MAT_FLIP_U) != 0);
    y = wrap((int)floorf(v * (float)t->h), t->h, (mat & SA1_MAT_CLAMP_V) != 0, (mat & SA1_MAT_FLIP_V) != 0);
    q = t->px + 4 * (y * t->w + x);
    out[0] = q[0] / 255.0f;
    out[1] = q[1] / 255.0f;
    out[2] = q[2] / 255.0f;
    out[3] = q[3] / 255.0f;
}

static float bf(uint8_t f, const float s[4], const float d[4], int k, int is_src)
{
    switch (f) {
    case SA1_BLEND_ZERO: return 0.0f;
    case SA1_BLEND_ONE: return 1.0f;
    case SA1_BLEND_OTHER_COLOR: return is_src ? d[k] : s[k];
    case SA1_BLEND_INV_OTHER_COLOR: return 1.0f - (is_src ? d[k] : s[k]);
    case SA1_BLEND_SRC_ALPHA: return s[3];
    case SA1_BLEND_INV_SRC_ALPHA: return 1.0f - s[3];
    case SA1_BLEND_DST_ALPHA: return d[3];
    default: return 1.0f - d[3];
    }
}

/* p: 3 corners (screen x, y, depth); uv 6; col 12 */
static void rast_tri(rast *r, const float p[9], const float uv[6], const uint8_t col[12], const rtex *t,
                     const sa1_draw *d, float shade)
{
    float minx = fminf(p[0], fminf(p[3], p[6])), maxx = fmaxf(p[0], fmaxf(p[3], p[6]));
    float miny = fminf(p[1], fminf(p[4], p[7])), maxy = fmaxf(p[1], fmaxf(p[4], p[7]));
    float area = (p[3] - p[0]) * (p[7] - p[1]) - (p[4] - p[1]) * (p[6] - p[0]);
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy), x, y;
    int blend = (d->flags & SA1_DRAW_BLEND) != 0, test = (d->flags & SA1_DRAW_ALPHA_TEST) != 0;
    if (fabsf(area) < 1e-9f)
        return;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > r->w - 1)
        x1 = r->w - 1;
    if (y1 > r->h - 1)
        y1 = r->h - 1;
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f, b0, b1, b2, z, c[4], s[4], dd[4];
            uint8_t *o;
            int k;
            b0 = ((p[3] - px) * (p[7] - py) - (p[4] - py) * (p[6] - px)) / area;
            b1 = ((p[6] - px) * (p[1] - py) - (p[7] - py) * (p[0] - px)) / area;
            b2 = 1.0f - b0 - b1;
            if (b0 < 0 || b1 < 0 || b2 < 0)
                continue;
            z = b0 * p[2] + b1 * p[5] + b2 * p[8];
            if (z < r->z[y * r->w + x])
                continue;
            sample(t, b0 * uv[0] + b1 * uv[2] + b2 * uv[4], b0 * uv[1] + b1 * uv[3] + b2 * uv[5], d->material, s);
            for (k = 0; k < 4; k++)
                c[k] = s[k] * (b0 * col[k] + b1 * col[4 + k] + b2 * col[8 + k]) / 255.0f;
            if (!(d->flags & SA1_DRAW_UNLIT))
                for (k = 0; k < 3; k++)
                    c[k] *= shade;
            o = r->rgb + 4 * (y * r->w + x);
            if (!blend) {
                if (test && c[3] < 0.5f)
                    continue;
                for (k = 0; k < 3; k++)
                    o[k] = (uint8_t)fminf(255.0f, c[k] * 255.0f);
                r->z[y * r->w + x] = z;
            } else {
                for (k = 0; k < 4; k++)
                    dd[k] = o[k] / 255.0f;
                for (k = 0; k < 3; k++) {
                    float v = c[k] * bf(d->blend_src, c, dd, k, 1) + dd[k] * bf(d->blend_dst, c, dd, k, 0);
                    o[k] = (uint8_t)fminf(255.0f, fmaxf(0.0f, v) * 255.0f);
                }
            }
        }
    }
}

/* ---- a view of an sa1_render ---------------------------------------------------------------------------- */
typedef struct view {
    float c[3];                 /* centre */
    float f[3], rt[3], up[3];   /* towards the camera, screen right, up */
    float scale, ox, oy;        /* pixels per host unit, screen centre */
} view;

static void project(const view *v, const float p[3], float out[3])
{
    float d[3];
    d[0] = p[0] - v->c[0];
    d[1] = p[1] - v->c[1];
    d[2] = p[2] - v->c[2];
    out[0] = v->ox + v->scale * (d[0] * v->rt[0] + d[1] * v->rt[1] + d[2] * v->rt[2]);
    out[1] = v->oy - v->scale * (d[0] * v->up[0] + d[1] * v->up[1] + d[2] * v->up[2]);
    out[2] = d[0] * v->f[0] + d[1] * v->f[1] + d[2] * v->f[2];
}

static int load_tex(const sa1_context *ctx, int32_t kind, int32_t archive, int32_t index, rtex *t)
{
    sa1_texture_info ti;
    memset(t, 0, sizeof *t);
    if (archive < 0 || index < 0)
        return 0;
    memset(&ti, 0, sizeof ti);
    ti.struct_size = sizeof ti;
    if (sa1_texture_get(ctx, kind, (uint32_t)archive, (uint32_t)index, &ti) != SA1_OK || !ti.rgba)
        return 0;
    t->w = (int)ti.width;
    t->h = (int)ti.height;
    t->px = ti.rgba;
    return 1;
}

static void draw_line(rast *r, const float a[3], const float b[3], const uint8_t ca[4], const uint8_t cb[4])
{
    float dx = b[0] - a[0], dy = b[1] - a[1], len = fmaxf(fabsf(dx), fabsf(dy));
    int n = (int)len + 1, i, k;
    for (i = 0; i <= n; i++) {
        float t = (float)i / (float)n;
        int x = (int)(a[0] + dx * t), y = (int)(a[1] + dy * t);
        float z = a[2] + (b[2] - a[2]) * t;
        if (x < 0 || y < 0 || x >= r->w || y >= r->h || z < r->z[y * r->w + x])
            continue;
        for (k = 0; k < 3; k++)
            r->rgb[4 * (y * r->w + x) + k] = (uint8_t)(ca[k] + (cb[k] - ca[k]) * t);
    }
}

/* one view of the render into the panel at (ox, oy) of r */
static void render_view(const sa1_context *ctx, int32_t kind, const sa1_render *rd, const view *v, rast *r)
{
    uint32_t d, t, k, pass, n;
    float light[3], l;
    light[0] = v->f[0] + 0.6f * v->up[0] + 0.3f * v->rt[0];
    light[1] = v->f[1] + 0.6f * v->up[1] + 0.3f * v->rt[1];
    light[2] = v->f[2] + 0.6f * v->up[2] + 0.3f * v->rt[2];
    l = sqrtf(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    for (k = 0; k < 3; k++)
        light[k] /= l;
    for (pass = 0; pass < 2; pass++) {
        for (d = 0; d < rd->num_draws; d++) {
            const sa1_draw *dr = &rd->draws[d];
            rtex tx;
            int has = load_tex(ctx, kind, dr->archive, dr->texture, &tx);
            if (((dr->flags & SA1_DRAW_BLEND) != 0) != (pass == 1))
                continue;
            if (dr->prim == SA1_PRIM_TRIANGLES) {
                for (t = dr->first; t < dr->first + dr->count; t++) {
                    const float *p = rd->positions + 9u * t, *nn = rd->normals + 9u * t;
                    float sp[9], e1[3], e2[3], gn[3], shade;
                    for (k = 0; k < 3; k++) {
                        e1[k] = p[3 + k] - p[k];
                        e2[k] = p[6 + k] - p[k];
                    }
                    gn[0] = e1[1] * e2[2] - e1[2] * e2[1];
                    gn[1] = e1[2] * e2[0] - e1[0] * e2[2];
                    gn[2] = e1[0] * e2[1] - e1[1] * e2[0];
                    if (!(dr->flags & SA1_DRAW_DOUBLE_SIDED) && gn[0] * v->f[0] + gn[1] * v->f[1] + gn[2] * v->f[2] <= 0.0f)
                        continue;
                    for (k = 0; k < 3; k++)
                        project(v, p + 3u * k, sp + 3u * k);
                    shade = 0.5f + 0.5f * fmaxf(0.0f, (nn[0] + nn[3] + nn[6]) / 3.0f * light[0] +
                                                         (nn[1] + nn[4] + nn[7]) / 3.0f * light[1] +
                                                         (nn[2] + nn[5] + nn[8]) / 3.0f * light[2]);
                    rast_tri(r, sp, rd->uvs + 6u * t, rd->colors + 12u * t, has ? &tx : NULL, dr, shade);
                }
            } else if (dr->prim == SA1_PRIM_SPRITES) {
                for (n = dr->first; n < dr->first + dr->count; n++) {
                    const sa1_sprite *s = &rd->sprites[n];
                    float corner[4][3], sc[4][3], ca = cosf(s->angle), sa = sinf(s->angle);
                    static const int cx[4] = { 0, 2, 2, 0 }, cy[4] = { 1, 1, 3, 3 };
                    float uv[4][2];
                    uint8_t col[12];
                    float tri[9], tuv[6];
                    for (k = 0; k < 4; k++) {
                        float x = s->rect[cx[k]], y = s->rect[cy[k]], xr = x * ca - y * sa, yr = x * sa + y * ca;
                        uint32_t j;
                        for (j = 0; j < 3; j++)
                            corner[k][j] = s->position[j] + v->rt[j] * xr - v->up[j] * yr;
                        project(v, corner[k], sc[k]);
                        sc[k][2] += 0.01f;
                        uv[k][0] = cx[k] == 0 ? s->uv[0] : s->uv[2];
                        uv[k][1] = cy[k] == 1 ? s->uv[1] : s->uv[3];
                    }
                    for (k = 0; k < 12; k++)
                        col[k] = s->color[k % 4];
                    for (k = 0; k < 2; k++) {
                        static const int tri_idx[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
                        uint32_t j;
                        for (j = 0; j < 3; j++) {
                            memcpy(tri + 3 * j, sc[tri_idx[k][j]], 3 * sizeof(float));
                            tuv[2 * j] = uv[tri_idx[k][j]][0];
                            tuv[2 * j + 1] = uv[tri_idx[k][j]][1];
                        }
                        rast_tri(r, tri, tuv, col, has ? &tx : NULL, dr, 1.0f);
                    }
                }
            } else {
                for (n = dr->first; n < dr->first + dr->count; n++) {
                    const sa1_line_vertex *a = &rd->line_vertices[2u * n], *b = a + 1;
                    float pa[3], pb[3];
                    project(v, a->position, pa);
                    project(v, b->position, pb);
                    draw_line(r, pa, pb, a->color, b->color);
                }
            }
        }
    }
}

/* front, side and back views of a render, centred on the character's triangles (and its effects), into path */
static int render_png(const sa1_context *ctx, int32_t kind, const sa1_render *rd, int size, const char *path)
{
    rast r;
    view v;
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f }, ext = 1.0f;
    uint32_t i, k;
    int pnl, ok;
    if (rast_init(&r, 3 * size, size) != 0)
        return 0;
    for (i = 0; i < 3u * rd->num_triangles; i++)
        for (k = 0; k < 3; k++) {
            lo[k] = fminf(lo[k], rd->positions[3 * i + k]);
            hi[k] = fmaxf(hi[k], rd->positions[3 * i + k]);
        }
    for (i = 0; i < rd->num_sprites; i++)
        for (k = 0; k < 3; k++) {
            lo[k] = fminf(lo[k], rd->sprites[i].position[k]);
            hi[k] = fmaxf(hi[k], rd->sprites[i].position[k]);
        }
    for (k = 0; k < 3; k++) {
        v.c[k] = rd->num_triangles ? 0.5f * (lo[k] + hi[k]) : rd->position[k];
        if (hi[k] - lo[k] > ext)
            ext = hi[k] - lo[k];
    }
    if (ext > 400.0f)
        ext = 400.0f;
    for (pnl = 0; pnl < 3; pnl++) {
        float th = rd->yaw + (pnl == 0 ? 0.0f : pnl == 1 ? 1.5707963f : 3.1415927f);
        v.f[0] = cosf(th);
        v.f[1] = sinf(th);
        v.f[2] = 0.0f;
        v.up[0] = 0.0f;
        v.up[1] = 0.0f;
        v.up[2] = 1.0f;
        v.rt[0] = -v.f[1];
        v.rt[1] = v.f[0];
        v.rt[2] = 0.0f;
        v.scale = 0.9f * (float)size / ext;
        v.ox = (float)size * ((float)pnl + 0.5f);
        v.oy = 0.5f * (float)size;
        render_view(ctx, kind, rd, &v, &r);
    }
    ok = tu_write_png(path, (uint32_t)(3 * size), (uint32_t)size, r.rgb, (uint32_t)(3 * size));
    free(r.rgb);
    free(r.z);
    return ok;
}

/* ---- texture dump ---------------------------------------------------------------------------------------- */
static void safe(char *dst, size_t n, const char *src)
{
    size_t i;
    for (i = 0; i + 1 < n && src && src[i]; i++) {
        char c = src[i];
        dst[i] = (char)(((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                         c == '-') ? c : '_');
    }
    dst[i] = 0;
    if (i == 0 && n > 1) {
        dst[0] = '-';
        dst[1] = 0;
    }
}

static int dump_textures(const sa1_context *ctx, int32_t kind, const char *out, uint32_t *ntex, uint32_t *nbad)
{
    sa1_character_info ci;
    char dir[1024], path[1300];
    uint32_t a;
    memset(&ci, 0, sizeof ci);
    ci.struct_size = sizeof ci;
    if (sa1_character_info_get(ctx, kind, &ci) != SA1_OK)
        return -1;
    snprintf(dir, sizeof dir, "%s/%s", out, sa1_character_name(kind));
    tu_mkdir(dir);
    for (a = 0; a < ci.num_archives; a++) {
        const char *an = NULL;
        char adir[1100], sn[64];
        uint32_t n = 0, t;
        FILE *f;
        if (sa1_texture_archive(ctx, kind, a, &an, &n) != SA1_OK)
            continue;
        snprintf(adir, sizeof adir, "%s/%02u_%s", dir, (unsigned)a, an);
        tu_mkdir(adir);
        snprintf(path, sizeof path, "%s/index.txt", adir);
        f = fopen(path, "w");
        if (!f)
            return -1;
        fprintf(f, "# %s archive %u %s: index name gbix pf df w h file archive alpha\n", sa1_character_name(kind),
                (unsigned)a, an);
        for (t = 0; t < n; t++) {
            sa1_texture_info ti;
            char fn[128];
            memset(&ti, 0, sizeof ti);
            ti.struct_size = sizeof ti;
            if (sa1_texture_get(ctx, kind, a, t, &ti) != SA1_OK)
                continue;
            safe(sn, sizeof sn, ti.name);
            if (ti.rgba) {
                snprintf(fn, sizeof fn, "%02u_%s.png", (unsigned)t, sn);
                snprintf(path, sizeof path, "%s/%s", adir, fn);
                if (!tu_write_png(path, ti.width, ti.height, ti.rgba, ti.width))
                    (*nbad)++;
            } else {
                snprintf(fn, sizeof fn, "-");
                (*nbad)++;
            }
            fprintf(f, "%u %s %u %u %u %u %u %s %s %u\n", (unsigned)t, sn, (unsigned)ti.global_index,
                    (unsigned)ti.pixel_format, (unsigned)ti.data_format, (unsigned)ti.width, (unsigned)ti.height, fn, an,
                    (unsigned)ti.alpha);
            (*ntex)++;
        }
        fclose(f);
    }
    return 0;
}

/* ---- scripted characters --------------------------------------------------------------------------------- */
static int run_script(sa1_context *ctx, sa1_character_id id, const char *script)
{
    sa1_input in;
    const char *p = script;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    while (*p) {
        char *e;
        long n = strtol(p, &e, 10), i;
        char cmd;
        if (e == p || !*e)
            return -1;
        cmd = *e;
        p = e + 1;
        in.stick_x = 0.0f;
        in.stick_y = (cmd == 'f' || cmd == 'b' || cmd == 'J') ? 1.0f : 0.0f;
        in.buttons = (cmd == 'a' || cmd == 'b') ? SA1_BUTTON_ACTION : (cmd == 'j' || cmd == 'J') ? SA1_BUTTON_JUMP
                   : (cmd == 'y') ? SA1_BUTTON_ACTION2 : 0u;
        for (i = 0; i < n; i++) {
            sa1_status st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
            if (SA1_FAILED(st)) {
                printf("  tick: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
                return -1;
            }
        }
    }
    return 0;
}

typedef struct shot {
    int32_t kind;
    const char *label, *script;
} shot;

static const shot k_shots[] = {
    { SA1_CHAR_SONIC, "stand", "20n" },
    { SA1_CHAR_SONIC, "spindash", "20n14a" },
    { SA1_CHAR_SONIC, "release", "20n14a6n" },
    { SA1_CHAR_TAILS, "stand", "20n" },
    { SA1_CHAR_TAILS, "run", "20n20f" },
    { SA1_CHAR_KNUCKLES, "stand", "20n" },
    { SA1_CHAR_KNUCKLES, "punch", "20n1a5n" },
    { SA1_CHAR_KNUCKLES, "combo", "20n1a3n1a3n1a3n" },
    { SA1_CHAR_AMY, "stand", "20n" },
    { SA1_CHAR_AMY, "hammer", "20n1a4n" },
    { SA1_CHAR_GAMMA, "stand", "20n" },
    { SA1_CHAR_GAMMA, "lockon", "20n20a" },
    { SA1_CHAR_BIG, "stand", "20n" },
    { SA1_CHAR_BIG, "cast", "20n10a20n" },
};

static int g_verbose;

static void print_draws(const sa1_render *rr)
{
    uint32_t d, nb = 0, nt = 0, ne = 0, nu = 0;
    for (d = 0; d < rr->num_draws; d++) {
        const sa1_draw *dr = &rr->draws[d];
        if (g_verbose || (dr->flags & (SA1_DRAW_BLEND | SA1_DRAW_EFFECT | SA1_DRAW_NO_TEXTURE)))
            printf("      draw %2u: prim %u src %u first %4u count %4u archive %2d tex %3d mat %08X flags %04X blend %u/%u "
                   "diffuse %02X%02X%02X%02X order %u\n", (unsigned)d, (unsigned)dr->prim, (unsigned)dr->source,
                   (unsigned)dr->first, (unsigned)dr->count, (int)dr->archive, (int)dr->texture, (unsigned)dr->material,
                   (unsigned)dr->flags, (unsigned)dr->blend_src, (unsigned)dr->blend_dst, dr->diffuse[0], dr->diffuse[1],
                   dr->diffuse[2], dr->diffuse[3], (unsigned)dr->order);
        nb += (dr->flags & SA1_DRAW_BLEND) != 0;
        nt += (dr->flags & SA1_DRAW_ALPHA_TEST) != 0;
        ne += (dr->flags & SA1_DRAW_EFFECT) != 0;
        nu += (dr->flags & SA1_DRAW_USE_ALPHA) != 0;
    }
    printf("    %u triangles, %u draws (%u blended, %u alpha-tested, %u USE_ALPHA drawn opaque, %u effects), %u sprites, "
           "%u line segments\n", (unsigned)rr->num_triangles, (unsigned)rr->num_draws, (unsigned)nb, (unsigned)nt,
           (unsigned)nu, (unsigned)ne, (unsigned)rr->num_sprites, (unsigned)(rr->num_line_vertices / 2u));
}

int main(int argc, char **argv)
{
    const char *gdi = getenv("SA1_TEST_GDI"), *out = "build/texdump", *script = NULL;
    int render = 0, size = 360, i, fails = 0;
    uint32_t chars = 0, ntex = 0, nbad = 0;
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_status st;
    int32_t k;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--gdi") && i + 1 < argc)
            gdi = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
        else if (!strcmp(argv[i], "--char") && i + 1 < argc) {
            int32_t kk = sa1_character_kind_from_name(argv[++i]);
            if (kk < 0) {
                printf("unknown character %s\n", argv[i]);
                return 2;
            }
            chars |= 1u << kk;
        } else if (!strcmp(argv[i], "--render"))
            render = 1;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc)
            script = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc)
            size = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--verbose"))
            g_verbose = 1;
        else {
            printf("usage: texdump [--gdi GDI] [--out DIR] [--char NAME ...] [--render] [--script S] [--size N]\n");
            return 2;
        }
    }
    if (!gdi) {
        printf("no disc: --gdi or SA1_TEST_GDI\n");
        return 2;
    }
    if (size < 64)
        size = 64;
    sa1_host_defaults(&h);
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK)
        return 1;
    st = sa1_load_disc(ctx, gdi, NULL);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return 1;
    }
    tu_mkdir("build");
    tu_mkdir(out);
    for (k = 0; k < SA1_CHAR_COUNT; k++) {
        if (chars && !(chars & (1u << k)))
            continue;
        if (dump_textures(ctx, k, out, &ntex, &nbad) != 0)
            continue;
    }
    printf("texdump: %u textures written to %s (%u not decodable / not written)\n", (unsigned)ntex, out, (unsigned)nbad);
    if (render) {
        sa1_collision_static col;
        size_t s;
        memset(&col, 0, sizeof col);
        col.struct_size = sizeof col;
        col.mode = SA1_COLLISION_MODE_DISC;
        col.disc_level = 1;
        col.disc_act = 0;
        st = sa1_collision_set_static_ex(ctx, &col);
        if (st != SA1_OK)
            printf("DISC collision: %s\n", sa1_last_error(ctx));
        for (s = 0; s < sizeof k_shots / sizeof k_shots[0]; s++) {
            const shot *sh = &k_shots[s];
            sa1_character_desc d;
            sa1_character_id id;
            const sa1_render *rr = NULL;
            char path[1200];
            if (chars && !(chars & (1u << sh->kind)))
                continue;
            if (script && strcmp(sh->label, "stand") != 0)
                continue;
            /* a fresh context per shot: a context composes at most 4 DISC-mode world images */
            sa1_shutdown(ctx);
            ctx = NULL;
            if (sa1_init(&h, &ctx) != SA1_OK || SA1_FAILED(sa1_load_disc(ctx, gdi, NULL)) ||
                sa1_collision_set_static_ex(ctx, &col) != SA1_OK) {
                printf("  context: %s\n", ctx ? sa1_last_error(ctx) : "-");
                fails++;
                break;
            }
            memset(&d, 0, sizeof d);
            d.struct_size = sizeof d;
            d.kind = sh->kind;
            d.flags = SA1_CREATE_WORLD | SA1_CREATE_START_TABLE;
            if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
                printf("  %s: %s\n", sa1_character_name(sh->kind), sa1_last_error(ctx));
                fails++;
                continue;
            }
            if (run_script(ctx, id, script ? script : sh->script) != 0 ||
                sa1_character_render(ctx, id, 1.0f, &rr) != SA1_OK) {
                fails++;
            } else {
                snprintf(path, sizeof path, "%s/render_%s_%s.png", out, sa1_character_name(sh->kind),
                         script ? "script" : sh->label);
                printf("  %s %s: %s\n", sa1_character_name(sh->kind), script ? script : sh->script, path);
                print_draws(rr);
                if (!render_png(ctx, sh->kind, rr, size, path))
                    fails++;
            }
            sa1_character_destroy(ctx, id);
        }
    }
    sa1_shutdown(ctx);
    return fails ? 1 : 0;
}