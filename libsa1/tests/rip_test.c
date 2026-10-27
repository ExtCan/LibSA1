/*
 * rip_test: loads the user's GD-ROM dump through libsa1 and writes, per character,
 * <char>.obj + <char>.mtl + tex_NN_<name>.png (like sadc/tools/research/rip_character.py)
 * plus a texture atlas per archive (atlas_<ARCHIVE>.png/.txt).  It compares the
 * counts with the Python proof of concept (expected table below, and optionally its
 * OBJ output), checks the physics table, poses every available action frame by frame,
 * and reports timings.  Uses only the public API.
 *
 * usage: rip_test <game.gdi> <outdir> [options]
 *   --char NAME        character to rip (repeatable; default: all)
 *   --action N         pose action N (default: bind pose of the main model)
 *   --frame F          frame for --action (default 0)
 *   --coords native|source   output units (default native = SA1 units, as the PoC)
 *   --no-weld          skip the weld list (raw rigid parts, as the PoC)
 *   --all-textures     write every texture of archive 0 (default: the used ones)
 *   --compare DIR      compare with rip_character.py output (DIR/<char>/<char>.obj
 *                      or DIR/<char>.obj): counts, texture ids, vertex positions
 *   --load-all         also load MINICART and Sonic's stage overlays
 *   --no-sweep         skip posing every action frame
 *   --verbose          library log at DEBUG level
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#include "libsa1.h"
#include "test_util.h"

#include <math.h>

/* the page-guard allocator (tests/guard_alloc.h): SA1_GUARD_ALLOC=1 puts libsa1's heap blocks against no-access pages */
#include "guard_alloc.c"
#define sa1_host_defaults(h) (sa1_host_defaults(h), ga_host(h))

typedef struct expect {
    const char *name;
    uint32_t nodes, verts, tris, uvs, tex_used, tex_total;
    float max_x_spd, rush_speed, rad, height, weight;
} expect;

/* Bind-pose results of rip_character.py (sadc/docs/research/sa1_assets.md section 10)
 * and player_parameter values (section 7.1). */
static const expect k_expect[SA1_CHAR_COUNT] = {
    { "sonic",      73, 635, 1001, 3003, 16, 28, 3.0f, 2.3f, 4.0f, 10.0f, 0.08f },
    { "eggman",     24, 834, 1138, 3384, 19, 19, 3.0f, 2.3f, 8.5f, 18.0f, 0.08f },
    { "tails",      68, 649,  997, 2991, 14, 24, 2.0f, 2.8f, 3.5f,  9.0f, 0.08f },
    { "knuckles",   73, 624,  981, 2940, 12, 22, 2.5f, 3.1f, 4.0f, 11.4f, 0.08f },
    { "tikal",     103, 621,  948, 2844, 16, 18, 3.0f, 2.3f, 4.0f, 10.0f, 0.08f },
    { "amy",        78, 656, 1000, 2994, 29, 53, 0.05f, 2.3f, 3.5f, 10.0f, 0.04f },
    { "gamma",      77, 680,  970, 2841, 34, 35, 2.5f, 2.5f, 4.0f, 21.0f, 0.1f },
    { "big",        73, 729, 1134, 3396, 29, 36, 1.0f, 2.3f, 9.5f, 17.0f, 0.135f },
    { "supersonic", 73, 642, 1046, 3138, 15, 73, 3.0f, 2.3f, 4.0f, 10.0f, 0.08f },
};

typedef struct options {
    const char *gdi, *outdir, *compare;
    uint32_t chars;
    int32_t action;
    float frame;
    int native, no_weld, all_textures, load_all, sweep, verbose;
} options;

static int g_failures;

static void check(int ok, const char *what, const char *name)
{
    if (!ok) {
        g_failures++;
        printf("  MISMATCH %s: %s\n", name, what);
    }
}

static void log_cb(void *user, int32_t level, const char *msg)
{
    static const char *const lv[] = { "debug", "info", "warn", "error" };
    (void)user;
    printf("  [libsa1 %s] %s\n", (level >= 0 && level <= 3) ? lv[level] : "?", msg);
}

static void join(char *out, size_t cap, const char *a, const char *b)
{
    snprintf(out, cap, "%s/%s", a, b);
}

/* ------------------------------------------------------------------------- */
/* PoC OBJ comparison                                                        */
/* ------------------------------------------------------------------------- */

typedef struct poc_obj {
    uint32_t v, vn, vt, f;
    float   *pos;
    uint8_t  tex_used[1024];
} poc_obj;

static int load_poc(const char *dir, const char *name, poc_obj *o)
{
    char path[2048];
    char *text, *p;
    size_t len = 0;
    uint32_t cap = 0;
    memset(o, 0, sizeof *o);
    snprintf(path, sizeof path, "%s/%s/%s.obj", dir, name, name);
    text = tu_read_file(path, &len);
    if (!text) {
        snprintf(path, sizeof path, "%s/%s.obj", dir, name);
        text = tu_read_file(path, &len);
    }
    if (!text)
        return 0;
    for (p = text; *p;) {
        char *e = p;
        while (*e && *e != '\n')
            e++;
        if (p[0] == 'v' && p[1] == ' ') {
            float x, y, z;
            if (sscanf(p + 2, "%f %f %f", &x, &y, &z) == 3) {
                if (o->v == cap) {
                    float *np;
                    cap = cap ? cap * 2 : 1024;
                    np = (float *)realloc(o->pos, (size_t)cap * 3 * sizeof(float));
                    if (!np)
                        break;
                    o->pos = np;
                }
                o->pos[3 * o->v] = x;
                o->pos[3 * o->v + 1] = y;
                o->pos[3 * o->v + 2] = z;
            }
            o->v++;
        } else if (p[0] == 'v' && p[1] == 'n' && p[2] == ' ') {
            o->vn++;
        } else if (p[0] == 'v' && p[1] == 't' && p[2] == ' ') {
            o->vt++;
        } else if (p[0] == 'f' && p[1] == ' ') {
            o->f++;
        } else if (strncmp(p, "usemtl tex", 10) == 0) {
            int id = atoi(p + 10);
            if (id >= 0 && id < 1024)
                o->tex_used[id] = 1;
        }
        p = *e ? e + 1 : e;
    }
    free(text);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Writers                                                                   */
/* ------------------------------------------------------------------------- */

static int write_obj(const char *dir, const char *name, const sa1_geometry *g, const char *const *texfile,
                     uint32_t ntex)
{
    char path[2048];
    FILE *f;
    uint32_t i, t, k, vt = 0;
    int32_t tid;
    uint32_t *vtbase = (uint32_t *)malloc((size_t)(g->num_triangles ? g->num_triangles : 1) * sizeof(uint32_t));
    if (!vtbase)
        return 0;
    snprintf(path, sizeof path, "%s/%s.mtl", dir, name);
    f = fopen(path, "w");
    if (!f) {
        free(vtbase);
        return 0;
    }
    for (tid = 0; tid < (int32_t)ntex; tid++) {
        int used = 0;
        for (t = 0; t < g->num_triangles && !used; t++)
            used = g->textures[t] == tid;
        if (used)
            fprintf(f, "newmtl tex%02d\nKd 1 1 1\nmap_Kd %s\n\n", (int)tid, texfile[tid] ? texfile[tid] : "missing.png");
    }
    fprintf(f, "newmtl untextured\nKd 0.8 0.8 0.8\n");
    fclose(f);

    snprintf(path, sizeof path, "%s/%s.obj", dir, name);
    f = fopen(path, "w");
    if (!f) {
        free(vtbase);
        return 0;
    }
    fprintf(f, "# %s ripped from the user's GD-ROM by libsa1 rip_test\nmtllib %s.mtl\no %s\n", name, name, name);
    for (i = 0; i < g->num_vertices; i++)
        fprintf(f, "v %.5f %.5f %.5f\n", g->positions[3 * i], g->positions[3 * i + 1], g->positions[3 * i + 2]);
    for (i = 0; i < g->num_vertices; i++)
        fprintf(f, "vn %.5f %.5f %.5f\n", g->normals[3 * i], g->normals[3 * i + 1], g->normals[3 * i + 2]);
    for (t = 0; t < g->num_triangles; t++) {
        vtbase[t] = 0xFFFFFFFFu;
        if (!(g->tri_flags[t] & SA1_TRI_HAS_UV))
            continue;
        vtbase[t] = vt;
        for (k = 0; k < 3; k++)
            fprintf(f, "vt %.5f %.5f\n", g->uvs[6 * t + 2 * k], 1.0 - g->uvs[6 * t + 2 * k + 1]);
        vt += 3;
    }
    /* faces grouped by texture id, untextured first (same order as rip_character.py) */
    for (tid = -1; tid < 32768; tid++) {
        int any = 0;
        for (t = 0; t < g->num_triangles; t++) {
            if (g->textures[t] != tid)
                continue;
            if (!any) {
                if (tid < 0)
                    fprintf(f, "usemtl untextured\n");
                else
                    fprintf(f, "usemtl tex%02d\n", (int)tid);
                any = 1;
            }
            fprintf(f, "f");
            for (k = 0; k < 3; k++) {
                uint32_t v = g->indices[3 * t + k] + 1;
                if (vtbase[t] != 0xFFFFFFFFu)
                    fprintf(f, " %u/%u/%u", (unsigned)v, (unsigned)(vtbase[t] + k + 1), (unsigned)v);
                else
                    fprintf(f, " %u//%u", (unsigned)v, (unsigned)v);
            }
            fprintf(f, "\n");
        }
        if (tid > 4096)
            break;
    }
    fclose(f);
    free(vtbase);
    return 1;
}

static void safe_name(char *dst, size_t cap, const char *src)
{
    size_t i;
    for (i = 0; i + 1 < cap && src[i]; i++) {
        char c = src[i];
        dst[i] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
                     ? c : '_';
    }
    dst[i] = 0;
}

static uint32_t dump_atlas(sa1_context *ctx, int32_t kind, uint32_t archive, const char *dir)
{
    const char *aname = NULL;
    uint32_t n = 0, w = 0, h = 0, i;
    uint8_t *rgba;
    sa1_atlas_rect *rects;
    char path[2048], sname[64];
    FILE *f;
    if (sa1_texture_archive(ctx, kind, archive, &aname, &n) != SA1_OK || n == 0)
        return 0;
    if (sa1_texture_atlas(ctx, kind, archive, NULL, &w, &h, NULL, 0) != SA1_OK)
        return 0;
    rgba = (uint8_t *)malloc((size_t)w * h * 4);
    rects = (sa1_atlas_rect *)calloc(n, sizeof *rects);
    if (!rgba || !rects || sa1_texture_atlas(ctx, kind, archive, rgba, &w, &h, rects, n) != SA1_OK) {
        free(rgba);
        free(rects);
        return 0;
    }
    safe_name(sname, sizeof sname, aname);
    snprintf(path, sizeof path, "%s/atlas_%s.png", dir, sname);
    tu_write_png(path, w, h, rgba, w);
    snprintf(path, sizeof path, "%s/atlas_%s.txt", dir, sname);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "# atlas of %s: %ux%u, %u textures (index name x y w h)\n", aname, (unsigned)w, (unsigned)h, (unsigned)n);
        for (i = 0; i < n; i++) {
            sa1_texture_info ti;
            memset(&ti, 0, sizeof ti);
            ti.struct_size = sizeof ti;
            sa1_texture_get(ctx, kind, archive, i, &ti);
            fprintf(f, "%u %s %u %u %u %u\n", (unsigned)i, ti.name ? ti.name : "", (unsigned)rects[i].x,
                    (unsigned)rects[i].y, (unsigned)rects[i].w, (unsigned)rects[i].h);
        }
        fclose(f);
    }
    free(rgba);
    free(rects);
    return w * h;
}

/* ------------------------------------------------------------------------- */

/* Axis map and world placement: with the Source preset, a model-space pose must equal
 * 4 * (x, -z, y) of the native pose; a world pose at yaw 0 is the model turned by 180
 * degrees about +Y (host = 4 * (-x, z, y)); yaw/position act as a rotation about host +Z
 * plus a translation. */
static void check_coords(sa1_context *ctx, sa1_character_id id, int32_t kind, const sa1_coords *restore)
{
    sa1_coords src;
    sa1_pose p;
    const sa1_geometry *g;
    float *nat, *base;
    uint32_t n, i;
    double e1 = 0, e2 = 0, e3 = 0;
    const double yaw = 0.7;
    memset(&p, 0, sizeof p);
    p.struct_size = sizeof p;
    p.flags = SA1_POSE_BIND | SA1_POSE_MODEL_SPACE;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &src);
    sa1_set_coords(ctx, &src);
    if (sa1_character_pose(ctx, id, &p, NULL) != SA1_OK || sa1_character_geometry(ctx, id, &g) != SA1_OK)
        return;
    n = g->num_vertices;
    nat = (float *)malloc((size_t)n * 3 * sizeof(float) + 1);
    base = (float *)malloc((size_t)n * 3 * sizeof(float) + 1);
    if (!nat || !base) {
        free(nat);
        free(base);
        return;
    }
    memcpy(nat, g->positions, (size_t)n * 3 * sizeof(float));
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &src);
    sa1_set_coords(ctx, &src);
    sa1_character_pose(ctx, id, &p, NULL);
    for (i = 0; i < n; i++) {
        e1 = fmax(e1, fabs(g->positions[3 * i] - 4.0 * nat[3 * i]));
        e1 = fmax(e1, fabs(g->positions[3 * i + 1] + 4.0 * nat[3 * i + 2]));
        e1 = fmax(e1, fabs(g->positions[3 * i + 2] - 4.0 * nat[3 * i + 1]));
    }
    p.flags = SA1_POSE_BIND;
    sa1_character_pose(ctx, id, &p, NULL);
    for (i = 0; i < n; i++) {
        e2 = fmax(e2, fabs(g->positions[3 * i] + 4.0 * nat[3 * i]));
        e2 = fmax(e2, fabs(g->positions[3 * i + 1] - 4.0 * nat[3 * i + 2]));
        e2 = fmax(e2, fabs(g->positions[3 * i + 2] - 4.0 * nat[3 * i + 1]));
    }
    memcpy(base, g->positions, (size_t)n * 3 * sizeof(float));
    p.yaw = (float)yaw;
    p.position[0] = 100.0f;
    p.position[1] = -50.0f;
    p.position[2] = 25.0f;
    sa1_character_pose(ctx, id, &p, NULL);
    for (i = 0; i < n; i++) {
        double x = base[3 * i], y = base[3 * i + 1], z = base[3 * i + 2];
        double rx = cos(yaw) * x - sin(yaw) * y + 100.0, ry = sin(yaw) * x + cos(yaw) * y - 50.0, rz = z + 25.0;
        e3 = fmax(e3, fabs(g->positions[3 * i] - rx));
        e3 = fmax(e3, fabs(g->positions[3 * i + 1] - ry));
        e3 = fmax(e3, fabs(g->positions[3 * i + 2] - rz));
    }
    printf("  coords: source axis map err %.2g, world turn err %.2g, yaw+position err %.2g\n", e1, e2, e3);
    check(e1 < 1e-3 && e2 < 1e-3 && e3 < 2e-3, "coordinate conversion", k_expect[kind].name);
    sa1_set_coords(ctx, restore);
    free(nat);
    free(base);
}

static void rip_character(sa1_context *ctx, int32_t kind, const options *opt)
{
    const expect *ex = &k_expect[kind];
    sa1_character_info ci;
    sa1_physics ph;
    sa1_character_id id = 0;
    sa1_pose pose;
    sa1_mesh mesh;
    const sa1_geometry *g = NULL;
    char dir[2048];
    char *texfile[1024];
    uint32_t ntex = 0, a, t, uvs = 0, used = 0, written = 0, k;
    uint8_t usedmap[1024];
    float *soup_pos = NULL;
    double t0, tpose, twrite;
    int bind = opt->action < 0;
    sa1_status st;

    memset(texfile, 0, sizeof texfile);
    memset(&ci, 0, sizeof ci);
    ci.struct_size = sizeof ci;
    printf("\n== %s ==\n", ex->name);
    if (sa1_character_info_get(ctx, kind, &ci) != SA1_OK || !ci.loaded) {
        printf("  not loaded\n");
        check(0, "character not loaded", ex->name);
        return;
    }
    join(dir, sizeof dir, opt->outdir, ex->name);
    tu_mkdir(dir);

    if (sa1_character_create(ctx, kind, NULL, 0.0f, &id) != SA1_OK) {
        check(0, sa1_last_error(ctx), ex->name);
        return;
    }
    memset(&pose, 0, sizeof pose);
    pose.struct_size = sizeof pose;
    pose.action = bind ? 0 : opt->action;
    pose.frame = opt->frame;
    pose.flags = SA1_POSE_MODEL_SPACE | (bind ? SA1_POSE_BIND : 0u) | (opt->no_weld ? SA1_POSE_NO_WELD : 0u);

    /* triangle soup into caller buffers, sized from the character info */
    memset(&mesh, 0, sizeof mesh);
    mesh.struct_size = sizeof mesh;
    mesh.max_triangles = ci.max_triangles;
    soup_pos = (float *)malloc((size_t)ci.max_triangles * 9 * sizeof(float) + 1);
    mesh.position = soup_pos;
    t0 = tu_now_ms();
    for (k = 0; k < 100; k++) {
        st = sa1_character_pose(ctx, id, &pose, &mesh);
        if (st != SA1_OK)
            break;
    }
    tpose = (tu_now_ms() - t0) / (k ? k : 1);
    if (st != SA1_OK) {
        check(0, sa1_last_error(ctx), ex->name);
        sa1_character_destroy(ctx, id);
        free(soup_pos);
        return;
    }
    sa1_character_geometry(ctx, id, &g);
    memset(usedmap, 0, sizeof usedmap);
    for (t = 0; t < g->num_triangles; t++) {
        if (g->tri_flags[t] & SA1_TRI_HAS_UV)
            uvs += 3;
        if (g->textures[t] >= 0 && g->textures[t] < 1024 && !usedmap[g->textures[t]]) {
            usedmap[g->textures[t]] = 1;
            used++;
        }
    }
    sa1_texture_archive(ctx, kind, 0, NULL, &ntex);
    if (ntex > 1024)
        ntex = 1024;
    printf("  model: %u nodes, %u vertices, %u triangles, %u UVs; textures used %u / %u; pose %.3f ms%s\n",
           (unsigned)g->num_nodes, (unsigned)g->num_vertices, (unsigned)g->num_triangles, (unsigned)uvs, (unsigned)used,
           (unsigned)ntex, tpose, bind ? " (bind)" : "");
    printf("  actions %u/%u available, %u motions, %u welds, %u archives, max %u triangles\n",
           (unsigned)ci.num_actions_available, (unsigned)ci.num_actions, (unsigned)ci.num_motions, (unsigned)ci.num_welds,
           (unsigned)ci.num_archives, (unsigned)ci.max_triangles);
    if (bind) {
        check(g->num_nodes == ex->nodes, "node count", ex->name);
        check(g->num_vertices == ex->verts, "vertex count", ex->name);
        check(g->num_triangles == ex->tris, "triangle count", ex->name);
        check(uvs == ex->uvs, "UV count", ex->name);
        check(used == ex->tex_used, "textures used", ex->name);
        check(ntex == ex->tex_total, "textures in PVM", ex->name);
    }
    /* the soup must equal the indexed geometry */
    for (t = 0; t < g->num_triangles && soup_pos; t++)
        for (k = 0; k < 3; k++)
            if (memcmp(soup_pos + 9 * t + 3 * k, g->positions + 3 * g->indices[3 * t + k], 3 * sizeof(float)) != 0) {
                check(0, "triangle soup differs from indexed geometry", ex->name);
                t = g->num_triangles;
                break;
            }
    free(soup_pos);

    /* physics */
    if (sa1_physics_get(ctx, kind, &ph) == SA1_OK) {
        printf("  physics: max_x_spd %g rush_speed %g rad %g height %g weight %g\n", ph.max_x_spd, ph.rush_speed,
               ph.rad, ph.height, ph.weight);
        check(fabsf(ph.max_x_spd - ex->max_x_spd) < 1e-4f && fabsf(ph.rush_speed - ex->rush_speed) < 1e-4f &&
                  fabsf(ph.rad - ex->rad) < 1e-4f && fabsf(ph.height - ex->height) < 1e-4f &&
                  fabsf(ph.weight - ex->weight) < 1e-4f,
              "physics entry", ex->name);
    } else {
        check(0, "no physics", ex->name);
    }

    /* textures + atlas */
    t0 = tu_now_ms();
    for (t = 0; t < ntex; t++) {
        sa1_texture_info ti;
        char fn[128], sname[64], path[2300];
        memset(&ti, 0, sizeof ti);
        ti.struct_size = sizeof ti;
        if (sa1_texture_get(ctx, kind, 0, t, &ti) != SA1_OK || !ti.rgba) {
            printf("  texture %u not decodable\n", (unsigned)t);
            continue;
        }
        if (!opt->all_textures && !usedmap[t])
            continue;
        safe_name(sname, sizeof sname, ti.name);
        snprintf(fn, sizeof fn, "tex_%02u_%s.png", (unsigned)t, sname);
        snprintf(path, sizeof path, "%s/%s", dir, fn);
        if (tu_write_png(path, ti.width, ti.height, ti.rgba, ti.width)) {
            texfile[t] = (char *)malloc(strlen(fn) + 1);
            if (texfile[t])
                memcpy(texfile[t], fn, strlen(fn) + 1);
            written++;
        }
    }
    for (a = 0; a < ci.num_archives; a++) {
        const char *an = NULL;
        uint32_t n = 0, px;
        sa1_texture_archive(ctx, kind, a, &an, &n);
        px = dump_atlas(ctx, kind, a, dir);
        printf("  archive %u %-11s %3u textures, atlas %s\n", (unsigned)a, an ? an : "?", (unsigned)n,
               px ? "written" : "FAILED");
        check(px != 0, "atlas", ex->name);
    }
    write_obj(dir, ex->name, g, (const char *const *)texfile, ntex);
    twrite = tu_now_ms() - t0;
    printf("  wrote %s/%s.obj, %u textures, atlases (%.1f ms)\n", dir, ex->name, (unsigned)written, twrite);
    for (t = 0; t < 1024; t++)
        free(texfile[t]);

    /* comparison with the Python PoC output */
    if (opt->compare) {
        poc_obj po;
        if (load_poc(opt->compare, ex->name, &po)) {
            double maxd = 0.0;
            uint32_t i, tex_ok = 1;
            for (i = 0; i < 1024; i++)
                if (po.tex_used[i] != usedmap[i])
                    tex_ok = 0;
            if (po.v == g->num_vertices && opt->native && po.pos)
                for (i = 0; i < 3 * po.v; i++) {
                    double d = fabs((double)po.pos[i] - (double)g->positions[i]);
                    if (d > maxd)
                        maxd = d;
                }
            printf("  PoC: v %u vn %u vt %u f %u | ours: v %u vn %u vt %u f %u | texture ids %s | max |dv| %.6f\n",
                   (unsigned)po.v, (unsigned)po.vn, (unsigned)po.vt, (unsigned)po.f, (unsigned)g->num_vertices,
                   (unsigned)g->num_vertices, (unsigned)uvs, (unsigned)g->num_triangles, tex_ok ? "equal" : "DIFFER",
                   maxd);
            check(po.v == g->num_vertices && po.vn == g->num_vertices && po.vt == uvs && po.f == g->num_triangles,
                  "counts differ from the PoC OBJ", ex->name);
            check(tex_ok, "texture ids differ from the PoC OBJ", ex->name);
            if (opt->native && opt->no_weld)
                check(maxd < 2e-3, "vertex positions differ from the PoC OBJ", ex->name);
            free(po.pos);
        } else {
            printf("  (no PoC output for %s in %s)\n", ex->name, opt->compare);
        }
    }

    /* weld effect on the bind pose: how far do welded vertices move? */
    if (!opt->no_weld && bind) {
        sa1_pose p2 = pose;
        const sa1_geometry *g2;
        float *welded = (float *)malloc((size_t)g->num_vertices * 3 * sizeof(float) + 1);
        uint32_t moved = 0, nv = g->num_vertices, amask = g->attach_valid;
        double maxm = 0.0, summ = 0.0;
        if (welded) {
            memcpy(welded, g->positions, (size_t)nv * 3 * sizeof(float));
            p2.flags |= SA1_POSE_NO_WELD;
            if (sa1_character_pose(ctx, id, &p2, NULL) == SA1_OK && sa1_character_geometry(ctx, id, &g2) == SA1_OK) {
                for (k = 0; k < nv; k++) {
                    double dx = welded[3 * k] - g2->positions[3 * k], dy = welded[3 * k + 1] - g2->positions[3 * k + 1],
                           dz = welded[3 * k + 2] - g2->positions[3 * k + 2], d = sqrt(dx * dx + dy * dy + dz * dz);
                    if (d > 1e-6) {
                        moved++;
                        summ += d;
                        if (d > maxm)
                            maxm = d;
                    }
                }
                printf("  welds: %u vertices moved in the bind pose (mean %.4f, max %.4f units), attach mask 0x%X\n",
                       (unsigned)moved, moved ? summ / moved : 0.0, maxm, (unsigned)amask);
            }
            free(welded);
        }
    }

    /* pose every available action at every frame */
    if (opt->sweep) {
        uint32_t poses = 0, bad = 0;
        double ts = tu_now_ms();
        for (a = 0; a < ci.num_actions; a++) {
            sa1_action_info ai;
            uint32_t fr;
            memset(&ai, 0, sizeof ai);
            ai.struct_size = sizeof ai;
            if (sa1_action_info_get(ctx, kind, a, &ai) != SA1_OK || ai.status != SA1_ACTION_OK)
                continue;
            for (fr = 0; fr < ai.num_frames; fr++) {
                sa1_pose p3;
                const sa1_geometry *g3;
                memset(&p3, 0, sizeof p3);
                p3.struct_size = sizeof p3;
                p3.action = (int32_t)a;
                p3.frame = (float)fr + 0.5f;
                p3.yaw = 0.25f * (float)fr;
                p3.position[0] = 100.0f;
                if (sa1_character_pose(ctx, id, &p3, NULL) != SA1_OK || sa1_character_geometry(ctx, id, &g3) != SA1_OK) {
                    bad++;
                    continue;
                }
                for (k = 0; k < 3 * g3->num_vertices; k++)
                    if (!(g3->positions[k] == g3->positions[k]) || fabsf(g3->positions[k]) > 1e6f) {
                        bad++;
                        break;
                    }
                poses++;
            }
        }
        printf("  sweep: %u poses of %u available actions, %u bad, %.3f ms per pose\n", (unsigned)poses,
               (unsigned)ci.num_actions_available, (unsigned)bad, poses ? (tu_now_ms() - ts) / poses : 0.0);
        check(bad == 0, "sweep produced invalid poses", ex->name);
    }
    {
        sa1_coords cur;
        memset(&cur, 0, sizeof cur);
        cur.struct_size = sizeof cur;
        sa1_get_coords(ctx, &cur);
        check_coords(ctx, id, kind, &cur);
    }
    sa1_character_destroy(ctx, id);
}

static void usage(void)
{
    printf("usage: rip_test <game.gdi> <outdir> [--char NAME]... [--action N] [--frame F]\n"
           "                [--coords native|source] [--no-weld] [--all-textures] [--compare DIR]\n"
           "                [--load-all] [--no-sweep] [--verbose]\n");
}

int main(int argc, char **argv)
{
    options opt;
    sa1_host host;
    sa1_context *ctx = NULL;
    sa1_load_options lo;
    sa1_coords coords;
    sa1_status st;
    double t0, tload;
    int i;
    uint32_t n, k;

    setvbuf(stdout, NULL, _IONBF, 0);
    memset(&opt, 0, sizeof opt);
    opt.action = -1;
    opt.native = 1;
    opt.sweep = 1;
    for (i = 1; i < argc; i++) {
        const char *s = argv[i];
        if (strcmp(s, "--char") == 0 && i + 1 < argc) {
            const char *cn = argv[++i];
            int32_t kind = strcmp(cn, "all") == 0 ? -2 : sa1_character_kind_from_name(cn);
            if (kind == -1) {
                printf("unknown character %s\n", cn);
                return 2;
            }
            opt.chars |= kind == -2 ? SA1_CHAR_MASK_ALL : (1u << kind);
        } else if (strcmp(s, "--action") == 0 && i + 1 < argc) {
            opt.action = atoi(argv[++i]);
        } else if (strcmp(s, "--frame") == 0 && i + 1 < argc) {
            opt.frame = (float)atof(argv[++i]);
        } else if (strcmp(s, "--coords") == 0 && i + 1 < argc) {
            opt.native = strcmp(argv[++i], "source") != 0;
        } else if (strcmp(s, "--no-weld") == 0) {
            opt.no_weld = 1;
        } else if (strcmp(s, "--all-textures") == 0) {
            opt.all_textures = 1;
        } else if (strcmp(s, "--compare") == 0 && i + 1 < argc) {
            opt.compare = argv[++i];
        } else if (strcmp(s, "--load-all") == 0) {
            opt.load_all = 1;
        } else if (strcmp(s, "--no-sweep") == 0) {
            opt.sweep = 0;
        } else if (strcmp(s, "--verbose") == 0) {
            opt.verbose = 1;
        } else if (s[0] == '-') {
            usage();
            return 2;
        } else if (!opt.gdi) {
            opt.gdi = s;
        } else if (!opt.outdir) {
            opt.outdir = s;
        } else {
            usage();
            return 2;
        }
    }
    if (!opt.gdi || !opt.outdir) {
        usage();
        return 2;
    }
    if (!opt.chars)
        opt.chars = SA1_CHAR_MASK_ALL;
    tu_mkdir(opt.outdir);

    sa1_host_defaults(&host);
    host.log = log_cb;
    host.log_level = opt.verbose ? SA1_LOG_DEBUG : SA1_LOG_INFO;
    st = sa1_init(&host, &ctx);
    if (st != SA1_OK) {
        printf("sa1_init: %s\n", sa1_status_string(st));
        return 1;
    }
    printf("libsa1 %s\n", sa1_version_string());
    sa1_coords_preset(opt.native ? SA1_COORDS_PRESET_NATIVE : SA1_COORDS_PRESET_SOURCE, &coords);
    sa1_set_coords(ctx, &coords);

    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = opt.chars;
    lo.flags = opt.load_all ? SA1_LOAD_ALL : SA1_LOAD_DEFAULT;
    t0 = tu_now_ms();
    st = sa1_load_disc(ctx, opt.gdi, &lo);
    tload = tu_now_ms() - t0;
    printf("sa1_load_disc: %s (%.1f ms)%s%s\n", sa1_status_string(st), tload, SA1_FAILED(st) ? ": " : "",
           SA1_FAILED(st) ? sa1_last_error(ctx) : "");
    if (SA1_FAILED(st)) {
        sa1_shutdown(ctx);
        return 1;
    }
    n = sa1_disc_file_count(ctx);
    printf("files read from the disc:\n");
    for (k = 0; k < n; k++) {
        sa1_disc_file_info fi;
        static const char *const hs[] = { "sha1 ok", "SHA1 MISMATCH", "unknown" };
        memset(&fi, 0, sizeof fi);
        fi.struct_size = sizeof fi;
        sa1_disc_file(ctx, k, &fi);
        printf("  %-26s %8u -> %8u  %s %s %s %6.2f ms\n", fi.name, (unsigned)fi.size, (unsigned)fi.unpacked_size, fi.sha1,
               (fi.hash_status >= 0 && fi.hash_status <= 2) ? hs[fi.hash_status] : "?", fi.used ? "used" : "REJECTED",
               fi.read_ms);
        if (fi.hash_status != SA1_HASH_OK)
            g_failures++;
    }
    for (k = 0; k < SA1_CHAR_COUNT; k++)
        if (opt.chars & (1u << k))
            rip_character(ctx, (int32_t)k, &opt);

    /* API skeleton checks */
    {
        sa1_state state;
        sa1_input in;
        uint32_t cnt = 99;
        sa1_character_id id;
        memset(&state, 0, sizeof state);
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        state.struct_size = sizeof state;
        if (sa1_character_create(ctx, SA1_CHAR_SONIC, NULL, 0.0f, &id) == SA1_OK) {
            /* SA1_OK when the library holds the game code (its first tick builds and boots Sonic's world from
             * the loaded disc); SA1_ERR_NOT_IMPLEMENTED in builds without it (--no-game, SA1_NO_WORLD, MSVC) */
            sa1_status tst = sa1_character_tick(ctx, id, &in, 1.0f / 60.0f, &state, NULL);
            check(tst == SA1_OK || tst == SA1_ERR_NOT_IMPLEMENTED, "tick must succeed (game code linked) or report not "
                  "implemented", "api");
            printf("  sa1_character_tick: %s\n", tst == SA1_OK ? "ok (game code linked)" :
                   tst == SA1_ERR_NOT_IMPLEMENTED ? "not implemented (no game code in this build)" : sa1_last_error(ctx));
            sa1_character_destroy(ctx, id);
        }
        check(sa1_poll_sound_events(ctx, NULL, 0, &cnt) == SA1_OK && cnt == 0, "sound queue", "api");
    }

    printf("\nload %.1f ms; %d mismatch(es)\n", tload, g_failures);
    sa1_shutdown(ctx);
    return g_failures ? 1 : 0;
}