/*
 * sa1_skin.c - a worn skin: Metal Sonic over Sonic's world (SA1_API_SKIN, round 0.9.9f; sadc docs/research/metal099e.md 3.2, 4.3, 4.4 with its review RM.1-16).
 *
 * HOST DEVIATIONS, all in the render step and all off unless a host asks (metal099e.md 4.1): D1 the parts table and the scale, D2 running rows drawn with another row's motion, D4 the spin ball's
 * texture. No game logic: the world under him is kind 0, Sonic's decompiled code. The figure is rigid: 25 parts, each turned by the world-space change of one of Sonic's nodes against his rest:
 *
 *     R(i)   = R(W(j)) * transpose(R(W0(j))) * A(i) * G
 *     pos(0) = position of W(the torso's mesh node)
 *     pos(i) = pos(parent) + R(parent) * scale * offset(i)
 *
 * W is the world's drawn matrix of Sonic's mesh node (the only matrix a draw carries); in a flown row the joint node of a private evaluation of the flown row's own tree (RM.3: the Super tree's mesh nodes
 * rest elsewhere: the head would turn 173 degrees). W0 is always Sonic's own body at rest. G turns the figure's axes to Sonic's, A(i) lays its bone along his, from the two skeletons, at load.
 */
#include "sa1_skin.h"
#include "sa1_api_world.h"

#include <math.h>
#include <string.h>

#define SK_PI 3.14159265358979323846

typedef struct sk_part {
    int32_t parent, joint, mesh, depth;
    uint32_t model;         /* guest address, 0: none */
    double off[3];          /* from the parent, the figure's own units */
    double ag[9];           /* A(i) * G */
    double align_deg;
} sk_part;

typedef struct sk_lookup {
    uint32_t model;         /* physical */
    int32_t node;
} sk_lookup;

typedef struct sa1k_state {
    sa1_charset set;        /* archives[0]: the kept textures, by position */
    uint8_t *model;         /* the bytes first .. end of the file's image */
    uint32_t first, size, root;
    uint16_t keep[64];
    uint32_t alpha[64];
    uint32_t num_keep;
    int32_t ball_pos;       /* position of ball_texture in keep, -1: none */
    double scale, lift;
    sk_part parts[SA1K_PARTS_MAX];
    uint32_t num_parts;
    uint16_t anim[16][2], anim_walk[8][2];
    uint32_t num_anim, num_anim_walk;
    /* Sonic's body at rest (model space) */
    uint32_t body_nodes;
    double rest_r[SA1K_NODES][9];
    /* models that are always the node they are: the plain body (RM.1: one draw of row 0 in 122 has the plain head), the curled body by index - 1 (metal099e.md section 2) */
    sk_lookup statics[2u * SA1K_NODES];
    uint32_t num_statics;
    uint32_t ball_model;    /* physical; the spin ball's model, 0: none */
} sa1k_state;

struct sa1k_char {
    const sa1k_state *q;
    uint32_t anim;          /* SA1_SKIN_ANIM_* */
    /* the last emit's table */
    uint8_t have[SA1K_NODES];
    double mtx[SA1K_NODES][12];
    sa1_draw_state ds;
    int32_t have_ds;
    /* the world's tree: the live model of each node, the live rest where it is not the plain body's */
    sk_lookup live[SA1K_NODES];
    uint32_t num_live;
    uint8_t has_fix[SA1K_NODES];
    double fix[SA1K_NODES][9];
    /* the fades (4.4, RM.7) */
    sa1k_drive drive;
    float w, x, w_was;
    int32_t cur, prev;      /* the flown row, the one x fades from; -1: none */
    double phase, rate;     /* through the flown motion, 0 .. 1; per pair */
    int32_t phase_have;     /* 0 none yet, 1 the last pair was a steady one of last_row, 2 it was not */
    double last_ph;
    int32_t last_row;
    sa1_instance inst;      /* the private evaluation of a flown row */
    int32_t inst_ok;
    /* the last append, for the tests */
    int32_t drew;
    double out_r[SA1K_PARTS_MAX][9], out_p[SA1K_PARTS_MAX][3], fly_root[12];
};

/* ---- small maths --------------------------------------------------------------------------------------------------------------------------------- */
static void m3_mul(const double a[9], const double b[9], double o[9])
{
    double t[9];
    int r, c;
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            t[3 * r + c] = a[3 * r] * b[c] + a[3 * r + 1] * b[3 + c] + a[3 * r + 2] * b[6 + c];
        }
    }
    memcpy(o, t, sizeof t);
}

static void m3_mul_t(const double a[9], const double b[9], double o[9])     /* a * transpose(b) */
{
    double t[9];
    int r, c;
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            t[3 * r + c] = a[3 * r] * b[3 * c] + a[3 * r + 1] * b[3 * c + 1] + a[3 * r + 2] * b[3 * c + 2];
        }
    }
    memcpy(o, t, sizeof t);
}

static void m3_ident(double o[9])
{
    memset(o, 0, 9 * sizeof(double));
    o[0] = o[4] = o[8] = 1.0;
}

/* the 3x3 of a 3x4 */
static void m3_of(const double m[12], double o[9])
{
    o[0] = m[0]; o[1] = m[1]; o[2] = m[2];
    o[3] = m[4]; o[4] = m[5]; o[5] = m[6];
    o[6] = m[8]; o[7] = m[9]; o[8] = m[10];
}

/* the rotation of a matrix that may carry a scale (RM.13: the display's squash, the scale keys of ten rows). Identity when it has none: nothing here divides by a bad matrix */
static int m3_rot(const double a[9], double q[9])
{
    if (sa1_pose_polar3(a, q)) {
        return 1;
    }
    m3_ident(q);
    return 0;
}

/* the rotation that takes the direction a to the direction b (none when they are opposite or nothing) */
static void rot_between(const double a0[3], const double b0[3], double o[9])
{
    double la = sqrt(a0[0] * a0[0] + a0[1] * a0[1] + a0[2] * a0[2]), lb = sqrt(b0[0] * b0[0] + b0[1] * b0[1] + b0[2] * b0[2]);
    double a[3], b[3], v[3], c, s, k;
    m3_ident(o);
    if (!(la > 1e-9) || !(lb > 1e-9)) {
        return;
    }
    a[0] = a0[0] / la; a[1] = a0[1] / la; a[2] = a0[2] / la;
    b[0] = b0[0] / lb; b[1] = b0[1] / lb; b[2] = b0[2] / lb;
    v[0] = a[1] * b[2] - a[2] * b[1];
    v[1] = a[2] * b[0] - a[0] * b[2];
    v[2] = a[0] * b[1] - a[1] * b[0];
    c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    s = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (s < 1e-9) {
        return;
    }
    k = (1.0 - c) / (s * s);
    o[0] = 1.0 + k * (-v[2] * v[2] - v[1] * v[1]);
    o[1] = -v[2] + k * v[0] * v[1];
    o[2] = v[1] + k * v[0] * v[2];
    o[3] = v[2] + k * v[0] * v[1];
    o[4] = 1.0 + k * (-v[2] * v[2] - v[0] * v[0]);
    o[5] = -v[0] + k * v[1] * v[2];
    o[6] = -v[1] + k * v[0] * v[2];
    o[7] = v[0] + k * v[1] * v[2];
    o[8] = 1.0 + k * (-v[1] * v[1] - v[0] * v[0]);
}

static void q_of(const double m[9], double q[4])
{
    double tr = m[0] + m[4] + m[8], s;
    if (tr > 0.0) {
        s = sqrt(tr + 1.0) * 2.0;
        q[3] = 0.25 * s;
        q[0] = (m[7] - m[5]) / s;
        q[1] = (m[2] - m[6]) / s;
        q[2] = (m[3] - m[1]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        s = sqrt(1.0 + m[0] - m[4] - m[8]) * 2.0;
        q[3] = (m[7] - m[5]) / s;
        q[0] = 0.25 * s;
        q[1] = (m[1] + m[3]) / s;
        q[2] = (m[2] + m[6]) / s;
    } else if (m[4] > m[8]) {
        s = sqrt(1.0 + m[4] - m[0] - m[8]) * 2.0;
        q[3] = (m[2] - m[6]) / s;
        q[0] = (m[1] + m[3]) / s;
        q[1] = 0.25 * s;
        q[2] = (m[5] + m[7]) / s;
    } else {
        s = sqrt(1.0 + m[8] - m[0] - m[4]) * 2.0;
        q[3] = (m[3] - m[1]) / s;
        q[0] = (m[2] + m[6]) / s;
        q[1] = (m[5] + m[7]) / s;
        q[2] = 0.25 * s;
    }
}

static void m3_of_q(const double q[4], double m[9])
{
    double x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1.0 - 2.0 * (y * y + z * z);
    m[1] = 2.0 * (x * y - z * w);
    m[2] = 2.0 * (x * z + y * w);
    m[3] = 2.0 * (x * y + z * w);
    m[4] = 1.0 - 2.0 * (x * x + z * z);
    m[5] = 2.0 * (y * z - x * w);
    m[6] = 2.0 * (x * z - y * w);
    m[7] = 2.0 * (y * z + x * w);
    m[8] = 1.0 - 2.0 * (x * x + y * y);
}

/* a -> b by t, the shorter way */
static void m3_slerp(const double a[9], const double b[9], double t, double o[9])
{
    double qa[4], qb[4], q[4], d, s0, s1, l;
    int i;
    if (t <= 0.0) {
        memmove(o, a, 9 * sizeof(double));
        return;
    }
    if (t >= 1.0) {
        memmove(o, b, 9 * sizeof(double));
        return;
    }
    q_of(a, qa);
    q_of(b, qb);
    d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    if (d < 0.0) {
        d = -d;
        for (i = 0; i < 4; i++) {
            qb[i] = -qb[i];
        }
    }
    if (d > 0.9995) {
        s0 = 1.0 - t;
        s1 = t;
    } else {
        double th = acos(d), st = sin(th);
        s0 = sin((1.0 - t) * th) / st;
        s1 = sin(t * th) / st;
    }
    for (i = 0; i < 4; i++) {
        q[i] = s0 * qa[i] + s1 * qb[i];
    }
    l = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (i = 0; i < 4; i++) {
        q[i] /= l;
    }
    m3_of_q(q, o);
}

/* ---- the load -------------------------------------------------------------------------------------------------------------------------------------- */
static const uint8_t *s_read(void *user, uint32_t g, uint32_t n)
{
    const sa1k_state *q = (const sa1k_state *)user;
    uint32_t off = (g & 0x1FFFFFFFu) - q->first;
    if (off > q->size || n > q->size - off) {
        return NULL;
    }
    return q->model + off;
}

static int32_t s_keep_pos(const sa1k_state *q, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < q->num_keep; i++) {
        if (q->keep[i] == id) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t s_resolve(void *user, uint32_t texlist, uint32_t id, int32_t *archive)
{
    const sa1k_state *q = (const sa1k_state *)user;
    (void)texlist;
    *archive = (int32_t)SA1_ARCHIVE_SKIN;
    return s_keep_pos(q, id);
}

/* RM.9: the skin's own archive: textures 97 and 98 have alpha */
static uint32_t s_alpha(void *user, int32_t archive, int32_t index)
{
    const sa1k_state *q = (const sa1k_state *)user;
    (void)archive;
    return index >= 0 && (uint32_t)index < q->num_keep ? q->alpha[index] : SA1_TEXALPHA_NONE;
}

/* the figure's tree, depth first, child before sibling: the table's node order (metal099e.md section 2) */
static int s_walk(sa1k_state *q, const sa1_skin_desc *d, uint32_t addr, int32_t parent, int32_t depth, uint32_t *n)
{
    while (addr != 0u) {
        const uint8_t *o = s_read(q, addr, 52u);
        uint32_t i = *n;
        sk_part *p;
        if (o == NULL || i >= d->num_parts || i >= SA1K_PARTS_MAX) {
            return 0;
        }
        p = &q->parts[i];
        p->parent = parent;
        p->depth = depth;
        p->joint = d->parts[i].joint_node;
        p->mesh = d->parts[i].mesh_node;
        p->model = sa1_rd32(o + 4);
        p->off[0] = sa1_rdf32(o + 8);
        p->off[1] = sa1_rdf32(o + 12);
        p->off[2] = sa1_rdf32(o + 16);
        (*n)++;
        if (!s_walk(q, d, sa1_rd32(o + 0x2C), (int32_t)i, depth + 1, n)) {
            return 0;
        }
        addr = sa1_rd32(o + 0x30);
        /* (the root has no sibling on the disc; a walk past the table stops above) */
    }
    return 1;
}

/* A(i) * G from the two skeletons (3.2): the figure's bone, in Sonic's axes, on Sonic's bone at rest */
static void s_align(sa1k_state *q, const sa1_skin_desc *d, const float *rest_mtx)
{
    static const double g_minus_z[9] = { 0, 0, 1, 0, 1, 0, -1, 0, 0 };
    double g[9], zero[SA1K_PARTS_MAX][3];
    uint32_t i;
    if (d->turn == SA1_SKIN_TURN_MINUS_Z) {
        memcpy(g, g_minus_z, sizeof g);
    } else {
        m3_ident(g);
    }
    for (i = 0; i < q->num_parts; i++) {
        const sk_part *p = &q->parts[i];
        int k;
        for (k = 0; k < 3; k++) {
            zero[i][k] = p->parent < 0 ? 0.0 : zero[p->parent][k] + p->off[k];
        }
    }
    for (i = 0; i < q->num_parts; i++) {
        sk_part *p = &q->parts[i];
        double a[9], bm[3], bg[3], bs[3];
        int be = d->parts[i].bone_end, se = d->parts[i].driver_bone_end;
        m3_ident(a);
        if (be >= 0 && (uint32_t)be < q->num_parts && se >= 0 && (uint32_t)se < q->body_nodes && p->joint >= 0) {
            int k;
            for (k = 0; k < 3; k++) {
                bm[k] = zero[be][k] - zero[i][k];
            }
            bg[0] = g[0] * bm[0] + g[1] * bm[1] + g[2] * bm[2];
            bg[1] = g[3] * bm[0] + g[4] * bm[1] + g[5] * bm[2];
            bg[2] = g[6] * bm[0] + g[7] * bm[1] + g[8] * bm[2];
            bs[0] = (double)rest_mtx[12u * (uint32_t)se + 3u] - (double)rest_mtx[12u * (uint32_t)p->joint + 3u];
            bs[1] = (double)rest_mtx[12u * (uint32_t)se + 7u] - (double)rest_mtx[12u * (uint32_t)p->joint + 7u];
            bs[2] = (double)rest_mtx[12u * (uint32_t)se + 11u] - (double)rest_mtx[12u * (uint32_t)p->joint + 11u];
            rot_between(bg, bs, a);
        }
        {
            double tr = a[0] + a[4] + a[8], c = 0.5 * (tr - 1.0);
            p->align_deg = acos(c > 1.0 ? 1.0 : (c < -1.0 ? -1.0 : c)) * 180.0 / SK_PI;
        }
        m3_mul(a, g, p->ag);
    }
}

/* Sonic's rest, the statics and the ball, from the charset */
static sa1_status s_from_body(sa1_context *ctx, sa1k_state *q, const sa1_charset *cs, const sa1_skin_desc *d)
{
    sa1_instance inst;
    const sa1_object *o;
    sa1_status st;
    uint32_t i;
    if (cs->main_object < 0 || (uint32_t)cs->main_object >= cs->objects.count) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SKIN: Sonic's body is not loaded");
    }
    o = &SA1_VEC(cs->objects, sa1_object)[cs->main_object];
    if (o->num_nodes > SA1K_NODES) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SKIN: Sonic's body has %u nodes", (unsigned)o->num_nodes);
    }
    memset(&inst, 0, sizeof inst);
    inst.kind = SA1_CHAR_SONIC;
    st = sa1_instance_alloc(ctx, cs, &inst);
    if (st != SA1_OK) {
        return st;
    }
    st = sa1_pose_evaluate(ctx, cs, &inst, cs->main_object, -1, 0.0f, SA1_POSE_MODEL_SPACE | SA1_POSE_NO_WELD);
    if (st == SA1_OK) {
        q->body_nodes = o->num_nodes;
        for (i = 0; i < o->num_nodes; i++) {
            const float *m = inst.node_mtx + 12u * i;
            double a[9];
            a[0] = m[0]; a[1] = m[1]; a[2] = m[2];
            a[3] = m[4]; a[4] = m[5]; a[5] = m[6];
            a[6] = m[8]; a[7] = m[9]; a[8] = m[10];
            m3_rot(a, q->rest_r[i]);
            if (o->nodes[i].model >= 0 && q->num_statics < 2u * SA1K_NODES) {
                q->statics[q->num_statics].model = SA1_VEC(cs->models, sa1_model)[o->nodes[i].model].addr & 0x1FFFFFFFu;
                q->statics[q->num_statics++].node = (int32_t)i;
            }
        }
        s_align(q, d, inst.node_mtx);
    }
    sa1_instance_free(ctx, &inst);
    if (st != SA1_OK) {
        return st;
    }
    /* the curled body: exactly the body plus one extra node at index 1, its own 28 models (metal099e.md section 2) */
    if (cs->num_actions > 14u && cs->actions[14].status == SA1_ACTION_OK) {
        const sa1_object *co = &SA1_VEC(cs->objects, sa1_object)[cs->actions[14].object];
        if (co->num_nodes == q->body_nodes + 1u && cs->actions[14].object != cs->main_object) {
            for (i = 1; i < co->num_nodes; i++) {
                if (co->nodes[i].model >= 0 && q->num_statics < 2u * SA1K_NODES) {
                    q->statics[q->num_statics].model = SA1_VEC(cs->models, sa1_model)[co->nodes[i].model].addr & 0x1FFFFFFFu;
                    q->statics[q->num_statics++].node = (int32_t)i - 1;
                }
            }
        }
    }
    /* the spin ball, row 32's only model */
    if (cs->num_actions > 32u && cs->actions[32].status == SA1_ACTION_OK) {
        const sa1_object *bo = &SA1_VEC(cs->objects, sa1_object)[cs->actions[32].object];
        for (i = 0; i < bo->num_nodes; i++) {
            if (bo->nodes[i].model >= 0) {
                q->ball_model = SA1_VEC(cs->models, sa1_model)[bo->nodes[i].model].addr & 0x1FFFFFFFu;
                break;
            }
        }
    }
    return SA1_OK;
}

sa1_status sa1k_load(sa1_context *ctx, const sa1_skin_desc *d)
{
    const sa1_api_ext *x = sa1_api_ext_of(ctx);
    const sa1_file_desc *fm, *ft;
    sa1k_state *q;
    uint8_t *overlay = NULL;
    uint32_t overlay_size = 0, first, i, n = 0;
    sa1_status st;
    if (d == NULL || d->struct_size < sizeof *d || d->model_file == NULL || d->tex_file == NULL || d->parts == NULL || d->tex_keep == NULL ||
        d->num_parts == 0u || d->num_parts > SA1K_PARTS_MAX || d->num_tex_keep == 0u || d->num_tex_keep > 64u || d->num_anim > 16u || d->num_anim_walk > 8u ||
        !(d->scale > 0.0f) || d->model_end <= d->model_first) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_CREATE_SKIN: the skin's description is not whole");
    }
    if (ctx->skin != NULL) {
        return ctx->skin->root == d->model_root ? SA1_OK : sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "SA1_CREATE_SKIN: one skin per disc load for now, 0x%08X is there", (unsigned)ctx->skin->root);
    }
    if (!ctx->disc_loaded || x == NULL || !x->gdi[0]) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SKIN: no disc is loaded");
    }
    if (!ctx->chars[SA1_CHAR_SONIC].loaded) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SKIN: Sonic is not loaded");
    }
    fm = sa1_manifest_file(d->model_file);
    ft = sa1_manifest_file(d->tex_file);
    if (fm == NULL || d->model_sha1 == NULL || strcmp(fm->sha1, d->model_sha1) != 0) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SKIN: %s: the skin names another file than the disc's (SHA-1)", d->model_file);
    }
    if (ft == NULL || d->tex_sha1 == NULL || strcmp(ft->sha1, d->tex_sha1) != 0) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SKIN: %s: the skin names another file than the disc's (SHA-1)", d->tex_file);
    }
    q = (sa1k_state *)sa1_mem_alloc(ctx, sizeof *q);
    if (q == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    q->first = d->model_first & 0x1FFFFFFFu;
    q->size = d->model_end - d->model_first;
    q->root = d->model_root;
    q->scale = (double)d->scale;
    q->lift = (double)d->lift;
    q->num_keep = d->num_tex_keep;
    q->ball_pos = -1;
    for (i = 0; i < d->num_tex_keep; i++) {
        q->keep[i] = d->tex_keep[i];
        if ((int32_t)d->tex_keep[i] == d->ball_texture) {
            q->ball_pos = (int32_t)i;
        }
    }
    q->num_anim = d->num_anim;
    for (i = 0; i < d->num_anim; i++) {
        q->anim[i][0] = d->anim[i][0];
        q->anim[i][1] = d->anim[i][1];
    }
    q->num_anim_walk = d->num_anim_walk;
    for (i = 0; i < d->num_anim_walk; i++) {
        q->anim_walk[i][0] = d->anim_walk[i][0];
        q->anim_walk[i][1] = d->anim_walk[i][1];
    }
    st = sa1_loader_skin(ctx, x->gdi, &q->set, d->model_file, &overlay, &overlay_size, d->tex_file, d->tex_keep, d->num_tex_keep);
    first = (d->model_first & 0x1FFFFFFFu) - (d->model_load & 0x1FFFFFFFu);
    if (st == SA1_OK && (overlay_size < first || overlay_size - first < q->size)) {
        st = sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SKIN: %s is %u bytes, the model ends at %u", d->model_file, (unsigned)overlay_size, (unsigned)(first + q->size));
    }
    if (st == SA1_OK) {
        q->model = (uint8_t *)sa1_mem_alloc(ctx, q->size);
        if (q->model == NULL) {
            st = SA1_ERR_OUT_OF_MEMORY;
        }
    }
    if (st == SA1_OK) {
        memcpy(q->model, overlay + first, q->size);
    }
    sa1_mem_free(ctx, overlay);
    if (st == SA1_OK) {
        if (!s_walk(q, d, d->model_root, -1, 0, &n) || n != d->num_parts) {
            st = sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SKIN: %s: the tree at 0x%08X is not the %u parts the skin names (%u)", d->model_file, (unsigned)d->model_root,
                          (unsigned)d->num_parts, (unsigned)n);
        }
        q->num_parts = n;
    }
    if (st == SA1_OK) {
        st = s_from_body(ctx, q, &ctx->chars[SA1_CHAR_SONIC], d);
    }
    if (st != SA1_OK) {
        sa1_charset_free(ctx, &q->set);
        sa1_mem_free(ctx, q->model);
        sa1_mem_free(ctx, q);
        return st;
    }
    for (i = 0; i < q->num_keep; i++) {
        const sa1_texture *t = &q->set.archives[0].tex[i];
        q->alpha[i] = t->rgba != NULL ? sa1_texture_alpha_kind(t->rgba, t->width, t->height) : SA1_TEXALPHA_NONE;
    }
    ctx->skin = q;
    return SA1_OK;
}

void sa1k_free(sa1_context *ctx)
{
    sa1k_state *q = ctx->skin;
    if (q != NULL) {
        ctx->skin = NULL;
        sa1_charset_free(ctx, &q->set);
        sa1_mem_free(ctx, q->model);
        sa1_mem_free(ctx, q);
    }
}

const sa1_archive *sa1k_archive(const sa1_context *ctx)
{
    const sa1k_state *q = ctx->skin;
    return q != NULL ? &q->set.archives[0] : NULL;
}

/* ---- per character --------------------------------------------------------------------------------------------------------------------------------- */
sa1_status sa1k_char_new(sa1_context *ctx, uint32_t anim, sa1k_char **out)
{
    sa1k_char *sc;
    if (ctx->skin == NULL) {
        return SA1_ERR_NOT_LOADED;
    }
    sc = (sa1k_char *)sa1_mem_alloc(ctx, sizeof *sc);
    if (sc == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    sc->q = ctx->skin;
    sc->anim = anim;
    sc->cur = sc->prev = sc->last_row = -1;
    sc->drive.row = -1;     /* (Character Select never steps: no row is known there) */
    sc->x = 1.0f;
    *out = sc;
    return SA1_OK;
}

void sa1k_char_free(sa1_context *ctx, sa1k_char *sc)
{
    if (sc != NULL) {
        if (sc->inst_ok) {
            sa1_instance_free(ctx, &sc->inst);
        }
        sa1_mem_free(ctx, sc);
    }
}

void sa1k_set_anim(sa1k_char *sc, uint32_t anim)
{
    sc->anim = anim;
}

/* ---- the note ------------------------------------------------------------------------------------------------------------------------------------- */
void sa1k_note_begin(sa1k_char *sc, sa1_capture *c, const sa1_charset *cs)
{
    const sa1_object *plain = NULL;
    uint32_t stack[SA1K_NODES + 2u], addr = cs->main_object_addr;
    uint32_t sp = 0, node = 0;
    memset(sc->have, 0, sizeof sc->have);
    memset(sc->has_fix, 0, sizeof sc->has_fix);
    sc->have_ds = 0;
    sc->num_live = 0;
    if (cs->main_object >= 0 && (uint32_t)cs->main_object < cs->objects.count) {
        plain = &SA1_VEC(cs->objects, sa1_object)[cs->main_object];
    }
    /* RM.1, RM.2: the world's own tree: the head's morph copy, the worn shoes and ring, row 13's toes, today and whatever else the game swaps. Reads only */
    while (node < SA1K_NODES) {
        const uint8_t *o;
        uint32_t model, child, sib;
        if (addr == 0u) {
            if (sp == 0u) {
                break;
            }
            addr = stack[--sp];
            continue;
        }
        o = sa1_capture_read(c, addr, 52u);
        if (o == NULL) {
            break;
        }
        model = sa1_rd32(o + 4) & 0x1FFFFFFFu;
        if (model != 0u) {
            sc->live[sc->num_live].model = model;
            sc->live[sc->num_live++].node = (int32_t)node;
        }
        /* RM.2: where the live object rests otherwise than the plain body (the worn feet: flags 05, angle Z -16384), the rotation between the two, for a row with no key on that node */
        if (plain != NULL && node < plain->num_nodes) {
            uint32_t fl = sa1_rd32(o);
            int32_t ang[3];
            const sa1_node *nd = &plain->nodes[node];
            ang[0] = (int32_t)sa1_rd32(o + 20);
            ang[1] = (int32_t)sa1_rd32(o + 24);
            ang[2] = (int32_t)sa1_rd32(o + 28);
            if ((fl & (SA1_EVAL_UNIT_ANG | SA1_EVAL_ZXY_ANG)) != (nd->flags & (SA1_EVAL_UNIT_ANG | SA1_EVAL_ZXY_ANG)) ||
                (!(fl & SA1_EVAL_UNIT_ANG) && (ang[0] != nd->ang[0] || ang[1] != nd->ang[1] || ang[2] != nd->ang[2]))) {
                double live[9], pl[9], lt[9];
                sa1_pose_rest_rotation(fl, ang, live);
                sa1_pose_rest_rotation(nd->flags, nd->ang, pl);
                /* W' = W * transpose(live) * plain */
                lt[0] = live[0]; lt[1] = live[3]; lt[2] = live[6];
                lt[3] = live[1]; lt[4] = live[4]; lt[5] = live[7];
                lt[6] = live[2]; lt[7] = live[5]; lt[8] = live[8];
                m3_mul(lt, pl, sc->fix[node]);
                sc->has_fix[node] = 1;
            }
        }
        child = sa1_rd32(o + 0x2C);
        sib = sa1_rd32(o + 0x30);
        node++;
        if (child != 0u) {
            if (sib != 0u && sp < SA1K_NODES) {
                stack[sp++] = sib;
            }
            addr = child;
        } else {
            addr = sib;
        }
    }
}

static int32_t s_lookup(const sa1k_char *sc, uint32_t model)
{
    uint32_t i, m = model & 0x1FFFFFFFu;
    for (i = 0; i < sc->num_live; i++) {
        if (sc->live[i].model == m) {
            return sc->live[i].node;
        }
    }
    for (i = 0; i < sc->q->num_statics; i++) {
        if (sc->q->statics[i].model == m) {
            return sc->q->statics[i].node;
        }
    }
    return -1;
}

int sa1k_note_ball(const sa1k_char *sc, uint32_t model)
{
    return sc->q->ball_model != 0u && (model & 0x1FFFFFFFu) == sc->q->ball_model;
}

void sa1k_note_draw(sa1k_char *sc, uint32_t model, const double m[12], const sa1_draw_state *ds)
{
    int32_t node;
    if (ds->material < 0 && ds->cmat[0] < 0.0f) {
        return;             /* a fading copy: no pose of his */
    }
    node = s_lookup(sc, model);
    if (node < 0 || (uint32_t)node >= SA1K_NODES) {
        return;
    }
    memcpy(sc->mtx[node], m, 12u * sizeof(double));
    sc->have[node] = 1;
    if (!sc->have_ds || node == sc->q->parts[0].mesh) {
        sc->ds = *ds;
        sc->have_ds = 1;
    }
}

static const uint8_t *ball_read(void *user, uint32_t g, uint32_t n)
{
    return sa1_capture_read(((const sa1k_ball_ctx *)user)->cap, g, n);
}

static int32_t ball_resolve(void *user, uint32_t texlist, uint32_t id, int32_t *archive)
{
    (void)texlist;
    (void)id;
    *archive = (int32_t)SA1_ARCHIVE_SKIN;
    return ((const sa1k_ball_ctx *)user)->q->ball_pos;
}

static uint32_t ball_alpha(void *user, int32_t archive, int32_t index)
{
    return s_alpha((void *)(uintptr_t)((const sa1k_ball_ctx *)user)->q, archive, index);
}

void sa1k_ball_info(const sa1k_char *sc, sa1_capture *c, sa1k_ball_ctx *bc, sa1_emit_info *ei)
{
    bc->cap = c;
    bc->q = sc->q;
    ei->reader = ball_read;
    ei->user = bc;
    ei->resolve = ball_resolve;
    ei->tex_alpha = ball_alpha;
    ei->cs = NULL;
}

void sa1k_note_pose(sa1k_char *sc, const sa1_charset *cs, int32_t object, const float *node_mtx, const float t[3])
{
    const sa1_object *o;
    uint32_t i, base = 0u;
    memset(sc->have, 0, sizeof sc->have);
    sc->have_ds = 0;
    if (object < 0 || (uint32_t)object >= cs->objects.count) {
        return;
    }
    o = &SA1_VEC(cs->objects, sa1_object)[object];
    if (o->num_nodes == sc->q->body_nodes + 1u) {
        base = 1u;          /* the curled body */
    } else if (o->num_nodes != sc->q->body_nodes) {
        return;
    }
    for (i = base; i < o->num_nodes && i - base < SA1K_NODES; i++) {
        const float *m = node_mtx + 12u * i;
        double *w = sc->mtx[i - base];
        int k;
        for (k = 0; k < 12; k++) {
            w[k] = (double)m[k];
        }
        w[3] += (double)t[0];
        w[7] += (double)t[1];
        w[11] += (double)t[2];
        sc->have[i - base] = 1;
    }
}

/* ---- the fades (4.4, RM.7) --------------------------------------------------------------------------------------------------------------------------- */
static int s_row_ok(const sa1_charset *cs, int32_t row)
{
    return row >= 0 && (uint32_t)row < cs->num_actions && cs->actions[row].status == SA1_ACTION_OK && cs->actions[row].motion >= 0;
}

static uint32_t s_frames(const sa1_charset *cs, int32_t row)
{
    if (!s_row_ok(cs, row)) {
        return 0u;
    }
    return SA1_VEC(cs->motions, sa1_motion)[cs->actions[row].motion].num_frames;
}

void sa1k_step(sa1k_char *sc, const sa1_charset *cs, const sa1k_drive *d)
{
    const sa1k_state *q = sc->q;
    int32_t target = -1;
    uint32_t i, nf;
    sc->drive = *d;
    if (d->row >= 0) {
        if (sc->anim & SA1_SKIN_ANIM_FLY) {
            for (i = 0; i < q->num_anim; i++) {
                if ((int32_t)q->anim[i][0] == d->row) {
                    target = (int32_t)q->anim[i][1];
                }
            }
        }
        if ((sc->anim & SA1_SKIN_ANIM_WALK) && (sc->anim & SA1_SKIN_ANIM_FLY)) {
            for (i = 0; i < q->num_anim_walk; i++) {
                if ((int32_t)q->anim_walk[i][0] == d->row) {
                    target = (int32_t)q->anim_walk[i][1];
                }
            }
        }
    }
    if (target >= 0 && !s_row_ok(cs, target)) {
        target = -1;
    }
    if (target >= 0) {
        if (sc->cur < 0) {
            sc->cur = target;
            sc->prev = -1;
            sc->x = 1.0f;
        } else if (target != sc->cur) {
            /* (133 and 134 are one motion: no fade) */
            if (cs->actions[target].motion == cs->actions[sc->cur].motion && cs->actions[target].object == cs->actions[sc->cur].object) {
                sc->cur = target;
            } else {
                sc->prev = sc->cur;
                sc->cur = target;
                sc->x = 0.0f;
            }
        }
    }
    /* The flown motion's phase goes on by itself at the rate the world's row runs, and starts where that row is. It never snaps to the row's frame: under mtnmode 2 nframe counts the blend, not the row;
       a new row starts at its own frame, which would pop the 8-frame flown motion by up to half its loop (MEASURED: 27.5 degrees a pair at row 10 to 11); on the way out the world's row has no flown row
       to name (RM.7). Nothing stands on this loop's frame: he flies, no foot touches. */
    nf = s_frames(cs, d->row);
    if (target >= 0 && d->steady && nf > 0u && sa1_finite(d->frame)) {
        double ph = fmod((double)d->frame / (double)nf, 1.0);
        if (ph < 0.0) {
            ph += 1.0;
        }
        if (sc->phase_have == 1 && sc->last_row == d->row) {
            double dr = ph - sc->last_ph;
            sc->rate = dr < 0.0 ? dr + 1.0 : dr;
        } else if (sc->phase_have == 0) {
            sc->phase = ph;
        }
        sc->last_ph = ph;
        sc->last_row = d->row;
        sc->phase_have = 1;
    } else if (sc->phase_have == 1) {
        sc->phase_have = 2;     /* the row's frame says nothing now: the next steady pair only reads a new rate from the pair after it */
    }
    if (sc->phase_have == 2 && target >= 0 && d->steady) {
        sc->phase_have = 1;
    }
    sc->phase = fmod(sc->phase + sc->rate, 1.0);
    sc->w_was = sc->w;
    sc->w += target >= 0 ? 0.125f : -0.125f;
    if (sc->w > 1.0f) {
        sc->w = 1.0f;
    }
    if (sc->w < 0.0f) {
        sc->w = 0.0f;
    }
    sc->x += 0.125f;
    if (sc->x >= 1.0f) {
        sc->x = 1.0f;
        sc->prev = -1;
    }
    if (sc->w <= 0.0f && target < 0) {
        sc->cur = sc->prev = -1;
        sc->x = 1.0f;
        sc->phase_have = 0;
        sc->rate = 0.0;
    }
}

int32_t sa1k_flying(const sa1k_char *sc)
{
    return sc->w > 0.0f || sc->w_was > 0.0f;
}

int32_t sa1k_part_mesh(const sa1k_char *sc, uint32_t model)
{
    const sa1k_state *q = sc->q;
    uint32_t i, m = model & 0x1FFFFFFFu;
    if (m < q->first || m - q->first >= q->size) {
        return -1;
    }
    for (i = 0; i < q->num_parts; i++) {
        if ((q->parts[i].model & 0x1FFFFFFFu) == m) {
            return q->parts[i].mesh;
        }
    }
    return -1;
}

/* the display's root from the world's drawn step (func_8C0AE17A:118-121, func_8C0AE110's centre): T(centre) Rz Rx Ry(0x8000 - y) */
static void s_fly_root(const sa1k_drive *d, double lift, double m[12])
{
    double th, c, s, rz[9], rx[9], ry[9], t[9];
    double up[3];
    th = (double)(d->ang[2] & 0xFFFFu) * (SK_PI / 32768.0);
    c = cos(th);
    s = sin(th);
    rz[0] = c; rz[1] = -s; rz[2] = 0; rz[3] = s; rz[4] = c; rz[5] = 0; rz[6] = 0; rz[7] = 0; rz[8] = 1;
    th = (double)(d->ang[0] & 0xFFFFu) * (SK_PI / 32768.0);
    c = cos(th);
    s = sin(th);
    rx[0] = 1; rx[1] = 0; rx[2] = 0; rx[3] = 0; rx[4] = c; rx[5] = -s; rx[6] = 0; rx[7] = s; rx[8] = c;
    th = (double)((0x8000u - d->ang[1]) & 0xFFFFu) * (SK_PI / 32768.0);
    c = cos(th);
    s = sin(th);
    ry[0] = c; ry[1] = 0; ry[2] = s; ry[3] = 0; ry[4] = 1; ry[5] = 0; ry[6] = -s; ry[7] = 0; ry[8] = c;
    m3_mul(rz, rx, t);
    up[0] = t[1];
    up[1] = t[4];
    up[2] = t[7];
    m3_mul(t, ry, t);
    m[0] = t[0]; m[1] = t[1]; m[2] = t[2];
    m[4] = t[3]; m[5] = t[4]; m[6] = t[5];
    m[8] = t[6]; m[9] = t[7]; m[10] = t[8];
    m[3] = (double)d->pos[0] + up[0] * ((double)d->center_height + lift);
    m[7] = (double)d->pos[1] + up[1] * ((double)d->center_height + lift);
    m[11] = (double)d->pos[2] + up[2] * ((double)d->center_height + lift);
}

/* each part's rotation and the torso's place in a flown row: the joint column of a private evaluation of that row's own tree (RM.3) */
static sa1_status s_flown(sa1_context *ctx, sa1k_char *sc, const sa1_charset *cs, int32_t row, const double root[12], double r[][9], double p0[3])
{
    const sa1k_state *q = sc->q;
    const sa1_action *ac = &cs->actions[row];
    const sa1_motion *mo = &SA1_VEC(cs->motions, sa1_motion)[ac->motion];
    double rr[9], f = sc->phase * (double)mo->num_frames;
    uint32_t i;
    sa1_status st = sa1_pose_evaluate(ctx, cs, &sc->inst, ac->object, ac->motion, (float)f, SA1_POSE_MODEL_SPACE | SA1_POSE_NO_WELD);
    const float *t0;
    if (st != SA1_OK) {
        return st;
    }
    m3_of(root, rr);
    for (i = 0; i < q->num_parts; i++) {
        const sk_part *pt = &q->parts[i];
        const float *m = sc->inst.node_mtx + 12u * (uint32_t)pt->joint;
        double a[9], b[9];
        a[0] = m[0]; a[1] = m[1]; a[2] = m[2];
        a[3] = m[4]; a[4] = m[5]; a[5] = m[6];
        a[6] = m[8]; a[7] = m[9]; a[8] = m[10];
        m3_rot(a, b);
        m3_mul(rr, b, a);
        m3_mul_t(a, q->rest_r[pt->joint], b);
        m3_mul(b, pt->ag, r[i]);
    }
    t0 = sc->inst.node_mtx + 12u * (uint32_t)q->parts[0].joint;
    p0[0] = root[0] * t0[3] + root[1] * t0[7] + root[2] * t0[11] + root[3];
    p0[1] = root[4] * t0[3] + root[5] * t0[7] + root[6] * t0[11] + root[7];
    p0[2] = root[8] * t0[3] + root[9] * t0[7] + root[10] * t0[11] + root[11];
    return SA1_OK;
}

/* ---- the append ------------------------------------------------------------------------------------------------------------------------------------ */
sa1_status sa1k_append(sa1_context *ctx, sa1k_char *sc, const sa1_charset *cs, sa1_meshbuf *b)
{
    const sa1k_state *q = sc->q;
    double turn[SA1K_PARTS_MAX][9], r[SA1K_PARTS_MAX][9], fr[SA1K_PARTS_MAX][9], fo[SA1K_PARTS_MAX][9], p0[3], pos[SA1K_PARTS_MAX][3];
    const sa1_motion *mo = NULL;
    sa1_emit_info ei;
    sa1_draw_state ds;
    uint32_t i;
    sa1_status st;
    int torso = q->parts[0].mesh;
    sc->drew = 0;
    if (torso < 0 || !sc->have[torso]) {
        return SA1_OK;      /* the world drew no body this pair: the hit blink blinks him too, the spin ball replaces him */
    }
    if (s_row_ok(cs, sc->drive.row)) {
        mo = &SA1_VEC(cs->motions, sa1_motion)[cs->actions[sc->drive.row].motion];
    }
    /* the world's pose: Sonic's mesh node against its rest; a node the world did not draw keeps its parent's turn */
    for (i = 0; i < q->num_parts; i++) {
        const sk_part *p = &q->parts[i];
        int32_t n = p->mesh;
        if (n >= 0 && (uint32_t)n < SA1K_NODES && sc->have[n]) {
            double a[9], rw[9];
            m3_of(sc->mtx[n], a);
            if (!m3_rot(a, rw)) {
                if (p->parent >= 0) {
                    memcpy(turn[i], turn[p->parent], sizeof turn[i]);
                } else {
                    m3_ident(turn[i]);
                }
            } else {
                /* RM.2: the worn feet rest 90 degrees from the plain ones; the motions' keys replace that in 124 of the 125 body rows. Row 47 has no keys there: the plain rest in its place */
                if (sc->has_fix[n] && mo != NULL && (uint32_t)n < mo->num_nodes && mo->nodes[n].num_ang == 0u) {
                    m3_mul(rw, sc->fix[n], rw);
                }
                m3_mul_t(rw, q->rest_r[n], turn[i]);
            }
        } else if (p->parent >= 0) {
            memcpy(turn[i], turn[p->parent], sizeof turn[i]);
        } else {
            m3_ident(turn[i]);
        }
        m3_mul(turn[i], p->ag, r[i]);
    }
    p0[0] = sc->mtx[torso][3];
    p0[1] = sc->mtx[torso][7];
    p0[2] = sc->mtx[torso][11];
    /* the flying (4.4) */
    if (sc->w > 0.0f && sc->cur >= 0 && sc->drive.have_root) {
        double root[12], pf[3], po[3];
        if (!sc->inst_ok) {
            memset(&sc->inst, 0, sizeof sc->inst);
            sc->inst.kind = SA1_CHAR_SONIC;
            st = sa1_instance_alloc(ctx, cs, &sc->inst);
            if (st != SA1_OK) {
                return st;
            }
            sc->inst_ok = 1;
        }
        s_fly_root(&sc->drive, q->lift, root);
        memcpy(sc->fly_root, root, sizeof root);
        st = s_flown(ctx, sc, cs, sc->cur, root, fr, pf);
        if (st != SA1_OK) {
            return st;
        }
        if (sc->prev >= 0 && sc->x < 1.0f && s_row_ok(cs, sc->prev)) {
            st = s_flown(ctx, sc, cs, sc->prev, root, fo, po);
            if (st != SA1_OK) {
                return st;
            }
            for (i = 0; i < q->num_parts; i++) {
                m3_slerp(fo[i], fr[i], (double)sc->x, fr[i]);
            }
            pf[0] = po[0] + (double)sc->x * (pf[0] - po[0]);
            pf[1] = po[1] + (double)sc->x * (pf[1] - po[1]);
            pf[2] = po[2] + (double)sc->x * (pf[2] - po[2]);
        }
        for (i = 0; i < q->num_parts; i++) {
            m3_slerp(r[i], fr[i], (double)sc->w, r[i]);
        }
        p0[0] += (double)sc->w * (pf[0] - p0[0]);
        p0[1] += (double)sc->w * (pf[1] - p0[1]);
        p0[2] += (double)sc->w * (pf[2] - p0[2]);
    }
    /* the places: each part starts where its parent says: joints can never come apart */
    pos[0][0] = p0[0];
    pos[0][1] = p0[1];
    pos[0][2] = p0[2];
    for (i = 1; i < q->num_parts; i++) {
        const sk_part *p = &q->parts[i];
        const double *pr = r[p->parent], *pp = pos[p->parent];
        pos[i][0] = pp[0] + q->scale * (pr[0] * p->off[0] + pr[1] * p->off[1] + pr[2] * p->off[2]);
        pos[i][1] = pp[1] + q->scale * (pr[3] * p->off[0] + pr[4] * p->off[1] + pr[5] * p->off[2]);
        pos[i][2] = pp[2] + q->scale * (pr[6] * p->off[0] + pr[7] * p->off[1] + pr[8] * p->off[2]);
    }
    memcpy(sc->out_r, r, q->num_parts * sizeof r[0]);
    memcpy(sc->out_p, pos, q->num_parts * sizeof pos[0]);
    /* the torso's draw state (so a palette or a flash is the same); a hidden pair's: the renderer's plain one */
    if (sc->have_ds) {
        ds = sc->ds;
    } else {
        memset(&ds, 0, sizeof ds);
        ds.hand = 1;
        ds.cmat[0] = ds.cmat[1] = ds.cmat[2] = ds.cmat[3] = 1.0f;
    }
    memset(&ei, 0, sizeof ei);
    ei.reader = s_read;
    ei.user = (void *)(uintptr_t)q;
    ei.ds = &ds;
    ei.ctx = ctx;
    ei.resolve = s_resolve;
    ei.texlist = q->root;       /* one constant for all parts: the anchor test compares it (RM.8) */
    ei.textured = 1;
    ei.source = SA1_SOURCE_CHARACTER;
    ei.tex_alpha = s_alpha;
    for (i = 0; i < q->num_parts; i++) {
        const sk_part *p = &q->parts[i];
        double m[12];
        if (p->model == 0u) {
            continue;
        }
        m[0] = q->scale * r[i][0]; m[1] = q->scale * r[i][1]; m[2] = q->scale * r[i][2]; m[3] = pos[i][0];
        m[4] = q->scale * r[i][3]; m[5] = q->scale * r[i][4]; m[6] = q->scale * r[i][5]; m[7] = pos[i][1];
        m[8] = q->scale * r[i][6]; m[9] = q->scale * r[i][7]; m[10] = q->scale * r[i][8]; m[11] = pos[i][2];
        ei.depth = p->depth + 1;
        st = sa1_pose_emit_model_ex(ctx, b, &ei, p->model, m);
        if (st != SA1_OK) {
            return st;
        }
    }
    sc->drew = 1;
    return SA1_OK;
}

/* ---- tests ------------------------------------------------------------------------------------------------------------------------------------------ */
void sa1k_test_get(const sa1k_char *sc, sa1k_test_info *out)
{
    const sa1k_state *q = sc->q;
    uint32_t i;
    memset(out, 0, sizeof *out);
    out->num_parts = q->num_parts;
    out->drew = sc->drew;
    out->w = sc->w;
    out->x = sc->x;
    out->cur = sc->cur;
    out->prev = sc->prev;
    out->phase = sc->phase;
    out->rate = sc->rate;
    memcpy(out->r, sc->out_r, sizeof out->r);
    memcpy(out->p, sc->out_p, sizeof out->p);
    out->scale = q->scale;
    for (i = 0; i < q->num_parts; i++) {
        out->align_deg[i] = q->parts[i].align_deg;
    }
    memcpy(out->have, sc->have, sizeof out->have);
    memcpy(out->mtx, sc->mtx, sizeof out->mtx);
    memcpy(out->rest, q->rest_r, sizeof out->rest);
    memcpy(out->fly_root, sc->fly_root, sizeof out->fly_root);
    out->lookups = sc->num_live + q->num_statics;
    out->model_bytes = q->size;
    out->drive_row = sc->drive.row;
    out->has_fix52 = sc->has_fix[52];
}
