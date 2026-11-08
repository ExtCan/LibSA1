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
 *
 * Round 0.9.9g. THE WAY BETWEEN A WALK AND A RUN (the owner: "uses Sonic's animation instead of Super Sonic's"; MEASURED sadc build/research/r099g/build/notes.md): a fade in never mixes in Sonic's row of the
 * pair, which is his speedwalk under the game's own blend: it starts from the pose he last showed, held against his root (held_r, h), and that is his walk; through the game's links (mtnmode 2) a flight goes on, up to 5 pairs;
 * the top of a walking row, faster than any held walk (anim_fast), flies. A fade out ends on the row he then is in, once the game's link is over. HOST DEVIATION D5.
 * THE GLOW: where the game draws Sonic's aura (func_8C0B4F00: its three passes, read off the capture with their colours, so its pulse is the game's), the parts again, offset along their normals at load. HOST DEVIATION D6.
 *
 * Round 1.0.0-beta1 (SA1_API_SKIN 3; sadc docs/research/gsa1_100_select.md 4.3.1, 4.3.2). MANY SKINS: the loaded skins are a list from ctx->skin (the first loaded; each state's `next`), at most
 * SA1_SKINS_MAX, known by their identity (sa1_skin_desc.id, else the model file, its root and the texture file); slot k draws from archive SA1_ARCHIVE_SKIN_FIRST + k. Every wearer points at its own
 * state. THE TRAILS: a skin with a trail colour keeps the four Light Speed charge textures of Sonic's SON_EFF (lcharge01-04) in its hue, value and saturation kept (the gold variant's method,
 * sa1_texture.c), made at its load: sa1_texture_variant_get's SA1_TEXTURE_VARIANT_SKIN_FIRST + k. HOST DEVIATION D8 of select 5; the capture marks the draws (SA1_DRAW_SKIN_TRAIL).
 * Round 1.0.0-beta1, the owner's answer 17 (Mecha Sonic "runs instead of flies ... he uses the Jog"; gsa1_100_select.md 4.2.1). A DRAWN ROW OF SONIC'S OWN TREE (his body's object: Mecha's
 * rows 12 and 13 drawn as row 11) is posed by the mesh nodes Sonic's own drawing uses (s_flown), so it is the live row's pose, part for part; between two speed loops of that tree (mtnmode 9 or 10)
 * the drawn phase runs k = (racio(drawn) / racio(world row)) x (frames(world row) / frames(drawn)) times the world row's rate (sa1k_step): the game's own step of the drawn row at the same speed.
 * THE PLANT (HOST DEVIATION D9): a skin that never leaves Sonic's own tree (every row of its tables in his body's tree, lift 0) moves along his up axis each pair so that its lowest point is Sonic's
 * own lowest point in the pose it was drawn from (his plain models' points, no weld): Mecha's shorter legs and long shoe fronts then never sink into the floor. Metal Sonic's rows are all in
 * the Super tree: neither change reaches him.
 */
#include "sa1_skin.h"
#include "sa1_api_world.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
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
    /* round 0.9.9g */
    uint16_t anim_fast[8][2];
    uint32_t num_anim_fast;
    double fast_on, fast_off;
    double glow_rgb[3];
    int32_t glow;           /* the shells are built */
    uint8_t *glow_block;    /* the shells' points and the material, in one block */
    uint32_t glow_bytes;
    const uint8_t *glow_pts[SA1K_PARTS_MAX];    /* 3 shells of np points, 12 bytes each */
    const uint8_t *glow_nrm[SA1K_PARTS_MAX];    /* the part's normals, unit length */
    uint32_t glow_np[SA1K_PARTS_MAX], glow_nmat[SA1K_PARTS_MAX];
    const uint8_t *glow_mats;                   /* the same material, as many as a part asks */
    uint32_t glow_no_normals;
    /* round 1.0.0-beta1: the slot (archive SA1_ARCHIVE_SKIN_FIRST + slot), the identity, the next skin loaded, the trails */
    uint32_t slot;
    char ident[256];
    struct sa1k_state *next;
    int32_t trail;                              /* the four variants are made */
    double trail_hue;                           /* degrees, of trail_rgb */
    uint32_t trail_arc;                         /* Sonic's SON_EFF archive */
    uint32_t trail_tex[4];                      /* lcharge01-04 in it */
    uint8_t *trail_rgba[4];
    /* round 1.0.0-beta1, the owner's answer 17: the plant (the top of this file) */
    int32_t plant;                              /* every row of its tables is in Sonic's own tree, lift 0 */
    const uint8_t *part_pts[SA1K_PARTS_MAX];    /* each part's points in `model`, part_np of them */
    uint32_t part_np[SA1K_PARTS_MAX];
    int32_t body_model[SA1K_NODES];             /* the model (sa1_charset.models) each drawn node of Sonic's body holds, -1: none */
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
    /* the way between a walk and a run (the top of this file) */
    int32_t fast_on, rising, hold_take, last_src_ok, link_held;
    float h;                /* the held pose against Sonic's own row of the pair, in what a flight fades from */
    double held_r[SA1K_PARTS_MAX][9], held_p0[3];       /* against the root */
    double last_src_r[SA1K_PARTS_MAX][9], last_src_p0[3];
    /* the game's aura this pair: a, r, g, b of each pass */
    float glow_cmat[3][4];
    int32_t glow_n;
    /* the last append, for the tests */
    int32_t drew;
    double out_r[SA1K_PARTS_MAX][9], out_p[SA1K_PARTS_MAX][3], fly_root[12];
    float out_live;
    int32_t out_have_flown;
    double out_flown[SA1K_PARTS_MAX][9];
    /* round 1.0.0-beta1: the world row `rate` was measured in (-1: none), the plant's last shift along the up axis */
    int32_t rate_row;
    double out_shift;
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

static void m3_t_mul(const double a[9], const double b[9], double o[9])     /* transpose(a) * b */
{
    double t[9];
    int r, c;
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            t[3 * r + c] = a[r] * b[c] + a[3 + r] * b[3 + c] + a[6 + r] * b[6 + c];
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
    *archive = (int32_t)(SA1_ARCHIVE_SKIN_FIRST + q->slot);     /* round 1.0.0-beta1: the slot's own archive */
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

/* The shells. Each part's points are moved along its own vertex normals once, here: a third, two thirds and all of glow_inflate (SA1 units; the figure's own units by its scale). A vertex with no normal
   moves away from the part's middle. The meshsets stay the part's. Nothing here touches the parts' own bytes */
static void wr_f32(uint8_t *p, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    p[0] = (uint8_t)u; p[1] = (uint8_t)(u >> 8); p[2] = (uint8_t)(u >> 16); p[3] = (uint8_t)(u >> 24);
}

static sa1_status s_glow_build(sa1_context *ctx, sa1k_state *q, const sa1_skin_desc *d)
{
    enum { SHELLS = 3 };
    uint32_t i, total = 0, max_mat = 1, off;
    uint8_t *m;
    q->glow_rgb[0] = (double)d->glow_rgb[0];
    q->glow_rgb[1] = (double)d->glow_rgb[1];
    q->glow_rgb[2] = (double)d->glow_rgb[2];
    for (i = 0; i < q->num_parts; i++) {
        const uint8_t *mp;
        int32_t np;
        if (q->parts[i].model == 0u) {
            continue;
        }
        mp = s_read(q, q->parts[i].model, 0x28u);
        if (mp == NULL) {
            return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SKIN: part %u's model is outside the file", (unsigned)i);
        }
        np = sa1_rds32(mp + 8);
        if (np < 0 || (uint32_t)np > SA1_MAX_POINTS) {
            return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SKIN: part %u has %d points", (unsigned)i, (int)np);
        }
        q->glow_np[i] = (uint32_t)np;
        q->glow_nmat[i] = sa1_rd16(mp + 22);
        if (q->glow_nmat[i] > max_mat) {
            max_mat = q->glow_nmat[i];
        }
        total += (uint32_t)np * 12u * (SHELLS + 1u);
    }
    q->glow_bytes = total + 20u * max_mat;
    q->glow_block = (uint8_t *)sa1_mem_alloc(ctx, q->glow_bytes);
    if (q->glow_block == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    /* the aura's material as the disc's S_MOT has it (lightspeed_visuals.md 3.2): diffuse FF000000, attr 26182400: ONE / ONE, unlit, no texture */
    m = q->glow_block + total;
    for (i = 0; i < max_mat; i++) {
        memset(m + 20u * i, 0, 20u);
        m[20u * i + 3u] = 0xFFu;
        m[20u * i + 16u] = 0x00u;
        m[20u * i + 17u] = 0x24u;
        m[20u * i + 18u] = 0x18u;
        m[20u * i + 19u] = 0x26u;
    }
    q->glow_mats = m;
    off = 0;
    for (i = 0; i < q->num_parts; i++) {
        const uint8_t *mp, *pts, *nrm = NULL;
        uint8_t *out, *nout;
        double mid[3] = { 0.0, 0.0, 0.0 };
        uint32_t np = q->glow_np[i], v;
        int k;
        if (q->parts[i].model == 0u || np == 0u) {
            continue;
        }
        mp = s_read(q, q->parts[i].model, 0x28u);
        pts = s_read(q, sa1_rd32(mp), 12u * np);
        if (sa1_rd32(mp + 4) != 0u) {
            nrm = s_read(q, sa1_rd32(mp + 4), 12u * np);
        }
        if (pts == NULL) {
            return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_SKIN: part %u's points are outside the file", (unsigned)i);
        }
        out = q->glow_block + off;
        nout = out + 12u * np * SHELLS;
        off += 12u * np * (SHELLS + 1u);
        q->glow_pts[i] = out;
        q->glow_nrm[i] = nout;
        for (v = 0; v < np; v++) {
            for (k = 0; k < 3; k++) {
                mid[k] += (double)sa1_rdf32(pts + 12u * v + 4u * (uint32_t)k) / (double)np;
            }
        }
        for (v = 0; v < np; v++) {
            double pt[3], n[3], l;
            int shell;
            for (k = 0; k < 3; k++) {
                pt[k] = (double)sa1_rdf32(pts + 12u * v + 4u * (uint32_t)k);
                n[k] = nrm != NULL ? (double)sa1_rdf32(nrm + 12u * v + 4u * (uint32_t)k) : 0.0;
            }
            l = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (!(l > 1e-6)) {
                q->glow_no_normals++;
                for (k = 0; k < 3; k++) {
                    n[k] = pt[k] - mid[k];
                }
                l = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (!(l > 1e-6)) {
                    n[0] = 0.0; n[1] = 1.0; n[2] = 0.0;
                    l = 1.0;
                }
            }
            for (k = 0; k < 3; k++) {
                n[k] /= l;
                wr_f32(nout + 12u * v + 4u * (uint32_t)k, (float)n[k]);
            }
            for (shell = 0; shell < SHELLS; shell++) {
                double o = (double)d->glow_inflate * (double)(shell + 1) / (double)SHELLS / q->scale;
                for (k = 0; k < 3; k++) {
                    wr_f32(out + 12u * np * (uint32_t)shell + 12u * v + 4u * (uint32_t)k, (float)(pt[k] + o * n[k]));
                }
            }
        }
    }
    q->glow = 1;
    return SA1_OK;
}

/* ---- round 1.0.0-beta1: many skins, the trails ---------------------------------------------------------------------------------------------------- */
static int sk_name_is(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a, y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

/* the identity a description names (select 4.3.1); 0: too long */
static int sk_ident(const sa1_skin_desc *d, char *out, size_t cap)
{
    int n;
    if (d->struct_size >= offsetof(sa1_skin_desc, id) + sizeof d->id && d->id != NULL && d->id[0] != '\0') {
        n = snprintf(out, cap, "id:%s", d->id);
    } else {
        n = snprintf(out, cap, "disc:%s:%08X:%s", d->model_file, (unsigned)d->model_root, d->tex_file);
    }
    return n > 0 && (size_t)n < cap;
}

static uint8_t sk_byte(double v)
{
    if (!(v > 0.0)) {
        return 0;
    }
    return v >= 255.0 ? 255 : (uint8_t)(v + 0.5);
}

/* the gold variant's texel (sa1_texture.c sa1_texture_gold_texel) with the hue a parameter: HSV value and saturation kept, alpha kept */
static void sk_hue_texel(const uint8_t in[4], uint8_t out[4], double hue)
{
    double mx = in[0], mn = in[0], h = hue / 60.0, f, q, t;
    int i;
    if (in[1] > mx) mx = in[1];
    if (in[2] > mx) mx = in[2];
    if (in[1] < mn) mn = in[1];
    if (in[2] < mn) mn = in[2];
    i = (int)h;
    if (i < 0 || i > 5) {
        i = 0;
    }
    f = h - (double)i;
    q = mx - (mx - mn) * f;
    t = mn + (mx - mn) * f;
    switch (i) {
    case 0:  out[0] = sk_byte(mx); out[1] = sk_byte(t);  out[2] = sk_byte(mn); break;
    case 1:  out[0] = sk_byte(q);  out[1] = sk_byte(mx); out[2] = sk_byte(mn); break;
    case 2:  out[0] = sk_byte(mn); out[1] = sk_byte(mx); out[2] = sk_byte(t);  break;
    case 3:  out[0] = sk_byte(mn); out[1] = sk_byte(q);  out[2] = sk_byte(mx); break;
    case 4:  out[0] = sk_byte(t);  out[1] = sk_byte(mn); out[2] = sk_byte(mx); break;
    default: out[0] = sk_byte(mx); out[1] = sk_byte(mn); out[2] = sk_byte(q);  break;
    }
    out[3] = in[3];
}

/* the hue of an RGB colour, degrees in [0, 360) */
static double sk_hue_of(const float rgb[3])
{
    double r = rgb[0], g = rgb[1], b = rgb[2], mx = r, mn = r, h;
    if (g > mx) mx = g;
    if (b > mx) mx = b;
    if (g < mn) mn = g;
    if (b < mn) mn = b;
    if (!(mx > mn)) {
        return 0.0;
    }
    if (mx == r) {
        h = 60.0 * (g - b) / (mx - mn);
    } else if (mx == g) {
        h = 60.0 * (b - r) / (mx - mn) + 120.0;
    } else {
        h = 60.0 * (r - g) / (mx - mn) + 240.0;
    }
    while (h < 0.0) {
        h += 360.0;
    }
    while (h >= 360.0) {
        h -= 360.0;
    }
    return h;
}

/* lcharge01-04 of Sonic's SON_EFF in the skin's hue (found by the archive's and the textures' names, as the gold variant). Without them: no trail (q->trail 0) */
static sa1_status sk_trail_build(sa1_context *ctx, sa1k_state *q, const sa1_skin_desc *d)
{
    static const char *const k_names[4] = { "lcharge01", "lcharge02", "lcharge03", "lcharge04" };
    const sa1_charset *cs = &ctx->chars[SA1_CHAR_SONIC];
    uint32_t a, i, k, found = 0;
    if (!(d->trail_rgb[0] > 0.0f || d->trail_rgb[1] > 0.0f || d->trail_rgb[2] > 0.0f)) {
        return SA1_OK;
    }
    q->trail_hue = sk_hue_of(d->trail_rgb);
    for (a = 0; a < cs->num_archives && !sk_name_is(cs->archives[a].name, "SON_EFF"); a++)
        ;
    if (a == cs->num_archives) {
        sa1_log(ctx, SA1_LOG_WARN, "skin %s: Sonic's SON_EFF archive is not loaded: the trails keep the game's colour", q->ident);
        return SA1_OK;
    }
    q->trail_arc = a;
    for (k = 0; k < 4u; k++) {
        const sa1_archive *ar = &cs->archives[a];
        for (i = 0; i < ar->num_textures && !sk_name_is(ar->tex[i].name, k_names[k]); i++)
            ;
        if (i < ar->num_textures && ar->tex[i].rgba != NULL && ar->tex[i].width != 0u && ar->tex[i].height != 0u) {
            size_t np = (size_t)ar->tex[i].width * ar->tex[i].height, p;
            q->trail_tex[k] = i;
            q->trail_rgba[k] = (uint8_t *)sa1_mem_alloc(ctx, 4u * np);
            if (q->trail_rgba[k] == NULL) {
                return SA1_ERR_OUT_OF_MEMORY;
            }
            for (p = 0; p < np; p++) {
                sk_hue_texel(ar->tex[i].rgba + 4u * p, q->trail_rgba[k] + 4u * p, q->trail_hue);
            }
            found++;
        } else {
            q->trail_tex[k] = 0xFFFFFFFFu;
        }
    }
    q->trail = found != 0u;
    return SA1_OK;
}

static void sk_state_free(sa1_context *ctx, sa1k_state *q)
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        sa1_mem_free(ctx, q->trail_rgba[k]);
    }
    sa1_mem_free(ctx, q->glow_block);
    sa1_charset_free(ctx, &q->set);
    sa1_mem_free(ctx, q->model);
    sa1_mem_free(ctx, q);
}

/* ---- round 1.0.0-beta1, the owner's answer 17: Sonic's own tree, the plant (the top of this file) ------------------------------------------------------ */
static int s_row_ok(const sa1_charset *cs, int32_t row);

/* 1: the row's object is Sonic's own body (the tree his live rows draw) */
static int s_own_tree(const sa1_charset *cs, int32_t row)
{
    return s_row_ok(cs, row) && cs->actions[row].object == cs->main_object;
}

/* whether the skin plants (every row of its tables in Sonic's own tree, lift 0); each part's points; the model of each drawn node of Sonic's body */
static void s_plant_init(sa1k_state *q, const sa1_charset *cs)
{
    uint32_t i;
    int own = q->lift == 0.0;
    for (i = 0; i < q->num_anim; i++) {
        own = own && s_own_tree(cs, (int32_t)q->anim[i][1]);
    }
    for (i = 0; i < q->num_anim_walk; i++) {
        own = own && s_own_tree(cs, (int32_t)q->anim_walk[i][1]);
    }
    for (i = 0; i < q->num_anim_fast; i++) {
        own = own && s_own_tree(cs, (int32_t)q->anim_fast[i][1]);
    }
    for (i = 0; i < SA1K_NODES; i++) {
        q->body_model[i] = -1;
    }
    if (cs->main_object >= 0 && (uint32_t)cs->main_object < cs->objects.count) {
        const sa1_object *o = &SA1_VEC(cs->objects, sa1_object)[cs->main_object];
        for (i = 0; i < o->num_nodes && i < SA1K_NODES; i++) {
            if (o->nodes[i].model >= 0 && (uint32_t)o->nodes[i].model < cs->models.count && o->nodes[i].drawn) {
                q->body_model[i] = o->nodes[i].model;
            }
        }
    }
    for (i = 0; i < q->num_parts; i++) {
        const uint8_t *mp = q->parts[i].model != 0u ? s_read(q, q->parts[i].model, 0x28u) : NULL;
        int32_t np = mp != NULL ? sa1_rds32(mp + 8) : 0;
        q->part_pts[i] = (np > 0 && (uint32_t)np <= SA1_MAX_POINTS) ? s_read(q, sa1_rd32(mp), 12u * (uint32_t)np) : NULL;
        q->part_np[i] = q->part_pts[i] != NULL ? (uint32_t)np : 0u;
    }
    q->plant = own;
}

/* the least of up . (R v + t) over Sonic's plain model `model` (the charset's points); R, t of a node's 3x4 matrix m (row major) */
static double s_model_low(const sa1_charset *cs, int32_t model, const double m[12], const double up[3], double low)
{
    const sa1_model *md = &SA1_VEC(cs->models, sa1_model)[model];
    double a0 = up[0] * m[0] + up[1] * m[4] + up[2] * m[8];
    double a1 = up[0] * m[1] + up[1] * m[5] + up[2] * m[9];
    double a2 = up[0] * m[2] + up[1] * m[6] + up[2] * m[10];
    double b = up[0] * m[3] + up[1] * m[7] + up[2] * m[11];
    uint32_t v;
    if (md->points == NULL) {
        return low;
    }
    for (v = 0; v < md->num_points; v++) {
        const float *p = md->points + 3u * v;
        double h = a0 * (double)p[0] + a1 * (double)p[1] + a2 * (double)p[2] + b;
        if (h < low) {
            low = h;
        }
    }
    return low;
}

sa1_status sa1k_load(sa1_context *ctx, const sa1_skin_desc *d, sa1k_state **out)
{
    const sa1_api_ext *x = sa1_api_ext_of(ctx);
    const sa1_file_desc *fm, *ft;
    sa1k_state *q, *last = NULL, *e;
    uint8_t *overlay = NULL;
    uint32_t overlay_size = 0, first, i, n = 0, count = 0;
    sa1_status st;
    char ident[256];
    if (out != NULL) {
        *out = NULL;
    }
    if (d == NULL || d->struct_size < offsetof(sa1_skin_desc, anim_fast) || d->model_file == NULL || d->tex_file == NULL || d->parts == NULL || d->tex_keep == NULL ||
        d->num_parts == 0u || d->num_parts > SA1K_PARTS_MAX || d->num_tex_keep == 0u || d->num_tex_keep > 64u || d->num_anim > 16u || d->num_anim_walk > 8u ||
        !(d->scale > 0.0f) || d->model_end <= d->model_first) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_CREATE_SKIN: the skin's description is not whole");
    }
    /* round 1.0.0-beta1: the skin by its identity; a loaded one is the same skin (the one-skin refusal of 0.9.9f is gone) */
    if (d->struct_size >= offsetof(sa1_skin_desc, base_kind) + sizeof d->base_kind && d->base_kind != SA1_CHAR_SONIC) {
        return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "SA1_CREATE_SKIN: base_kind %d: a skin is worn over Sonic (kind 0) only in this version", (int)d->base_kind);
    }
    if (!sk_ident(d, ident, sizeof ident)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_CREATE_SKIN: the skin's id or file names are too long");
    }
    for (e = ctx->skin; e != NULL; e = e->next) {
        if (strcmp(e->ident, ident) == 0) {
            if (out != NULL) {
                *out = e;
            }
            return SA1_OK;
        }
        last = e;
        count++;
    }
    if (count >= SA1_SKINS_MAX) {
        return sa1_fail(ctx, SA1_ERR_LIMIT, "SA1_CREATE_SKIN: %u skins are loaded (SA1_SKINS_MAX): %s would be one more", (unsigned)count, ident);
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
    q->slot = count;
    memcpy(q->ident, ident, sizeof q->ident);
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
    if (d->struct_size >= sizeof *d && d->anim_fast != NULL) {
        q->num_anim_fast = d->num_anim_fast > 8u ? 8u : d->num_anim_fast;
        for (i = 0; i < q->num_anim_fast; i++) {
            q->anim_fast[i][0] = d->anim_fast[i][0];
            q->anim_fast[i][1] = d->anim_fast[i][1];
        }
        q->fast_on = (double)d->fast_on;
        q->fast_off = (double)d->fast_off;
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
    if (st == SA1_OK) {
        s_plant_init(q, &ctx->chars[SA1_CHAR_SONIC]);      /* round 1.0.0-beta1 */
    }
    if (st == SA1_OK && d->struct_size >= offsetof(sa1_skin_desc, glow_inflate) + sizeof d->glow_inflate && (d->glow_rgb[0] > 0.0f || d->glow_rgb[1] > 0.0f || d->glow_rgb[2] > 0.0f)) {
        st = s_glow_build(ctx, q, d);
    }
    if (st == SA1_OK && d->struct_size >= offsetof(sa1_skin_desc, trail_rgb) + sizeof d->trail_rgb) {
        st = sk_trail_build(ctx, q, d);     /* round 1.0.0-beta1 */
    }
    if (st != SA1_OK) {
        sk_state_free(ctx, q);
        return st;
    }
    for (i = 0; i < q->num_keep; i++) {
        const sa1_texture *t = &q->set.archives[0].tex[i];
        q->alpha[i] = t->rgba != NULL ? sa1_texture_alpha_kind(t->rgba, t->width, t->height) : SA1_TEXALPHA_NONE;
    }
    if (last != NULL) {
        last->next = q;
    } else {
        ctx->skin = q;
    }
    if (out != NULL) {
        *out = q;
    }
    return SA1_OK;
}

void sa1k_free(sa1_context *ctx)
{
    sa1k_state *q = ctx->skin;
    ctx->skin = NULL;
    while (q != NULL) {
        sa1k_state *nx = q->next;
        sk_state_free(ctx, q);
        q = nx;
    }
}

/* the state of slot k, NULL: none */
static const sa1k_state *sk_slot(const sa1_context *ctx, uint32_t k)
{
    const sa1k_state *q;
    for (q = ctx->skin; q != NULL && q->slot != k; q = q->next)
        ;
    return q;
}

const sa1_archive *sa1k_archive(const sa1_context *ctx, uint32_t archive)
{
    const sa1k_state *q = NULL;
    if (archive == SA1_ARCHIVE_SKIN) {
        q = sk_slot(ctx, 0u);       /* "the first skin loaded", for this version only */
    } else if (archive >= SA1_ARCHIVE_SKIN_FIRST && archive <= SA1_ARCHIVE_SKIN_LAST) {
        q = sk_slot(ctx, archive - SA1_ARCHIVE_SKIN_FIRST);
    }
    return q != NULL ? &q->set.archives[0] : NULL;
}

const uint8_t *sa1k_trail_rgba(const sa1_context *ctx, uint32_t slot, uint32_t archive, uint32_t index)
{
    const sa1k_state *q = sk_slot(ctx, slot);
    uint32_t k;
    if (q == NULL || !q->trail || archive != q->trail_arc) {
        return NULL;
    }
    for (k = 0; k < 4u; k++) {
        if (q->trail_tex[k] == index && q->trail_rgba[k] != NULL) {
            return q->trail_rgba[k];
        }
    }
    return NULL;
}

uint32_t sa1k_char_archive(const sa1k_char *sc)
{
    return SA1_ARCHIVE_SKIN_FIRST + sc->q->slot;
}

int sa1k_trail_on(const sa1k_char *sc)
{
    return sc != NULL && sc->q->trail && (sc->anim & SA1_SKIN_ANIM_TRAIL) != 0u;
}

const sa1k_state *sa1k_char_state(const sa1k_char *sc)
{
    return sc != NULL ? sc->q : NULL;
}

sa1_status sa1_character_skin_archive(sa1_context *ctx, sa1_character_id id, uint32_t *archive)
{
    sa1k_char *sc;
    if (archive != NULL) {
        *archive = 0u;
    }
    if (ctx == NULL || archive == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (sa1w_character_peek(ctx, id, NULL, NULL, NULL) != SA1_OK) {
        return SA1_ERR_NOT_FOUND;
    }
    sc = sa1k_char_of(ctx, id);
    if (sc == NULL) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "sa1_character_skin_archive: character %d wears no skin", (int)id);
    }
    *archive = sa1k_char_archive(sc);
    return SA1_OK;
}

/* ---- per character --------------------------------------------------------------------------------------------------------------------------------- */
sa1_status sa1k_char_new(sa1_context *ctx, const sa1k_state *q, uint32_t anim, sa1k_char **out)
{
    sa1k_char *sc;
    if (q == NULL) {
        return SA1_ERR_NOT_LOADED;
    }
    sc = (sa1k_char *)sa1_mem_alloc(ctx, sizeof *sc);
    if (sc == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    sc->q = q;
    sc->anim = anim;
    sc->cur = sc->prev = sc->last_row = -1;
    sc->rate_row = -1;
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
    sc->glow_n = 0;
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

void sa1k_note_glow(sa1k_char *sc, const sa1_draw_state *ds)
{
    /* three passes of 20 models each: a new pass by its colour */
    if (sc->glow_n > 0 && memcmp(sc->glow_cmat[sc->glow_n - 1], ds->cmat, sizeof ds->cmat) == 0) {
        return;
    }
    if (sc->glow_n < 3) {
        memcpy(sc->glow_cmat[sc->glow_n++], ds->cmat, sizeof ds->cmat);
    }
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
    *archive = (int32_t)(SA1_ARCHIVE_SKIN_FIRST + ((const sa1k_ball_ctx *)user)->q->slot);
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

/* round 1.0.0-beta1 (gsa1_100_select.md 4.2.1 change 2): a speed loop (mtnmode 9 or 10) advances racio x speed frames a step (PSetMotion, code_8C079476.c:117-136), so the drawn row of Sonic's own
   tree steps k times the world row's phase: k = (racio(drawn) / racio(world)) x (frames(world) / frames(drawn)); 1 for any other pair (a flown row of another tree: Metal Sonic's) */
static double s_rate_k(const sa1_charset *cs, int32_t drawn, int32_t world)
{
    const sa1_action *a, *b;
    uint32_t fa, fb;
    if (drawn < 0 || world < 0 || drawn == world || !s_own_tree(cs, drawn) || !s_own_tree(cs, world)) {
        return 1.0;
    }
    a = &cs->actions[drawn];
    b = &cs->actions[world];
    fa = s_frames(cs, drawn);
    fb = s_frames(cs, world);
    if ((a->mode != 9 && a->mode != 10) || (b->mode != 9 && b->mode != 10) || !(a->speed > 0.0f) || !(b->speed > 0.0f) || fa == 0u || fb == 0u) {
        return 1.0;
    }
    return ((double)a->speed / (double)b->speed) * ((double)fb / (double)fa);
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
        /* the top of a walking row: faster than any held walk (MEASURED: a stick up to 0.8 holds at most 0.205; at 0.85 and over he only passes through): the way to a run or from it. A slow walk stays Sonic's */
        if (sc->anim & SA1_SKIN_ANIM_FLY) {
            int32_t fast = 0;
            for (i = 0; i < q->num_anim_fast; i++) {
                if ((int32_t)q->anim_fast[i][0] == d->row) {
                    double v = fabs((double)d->speed);
                    fast = 1;
                    sc->fast_on = v >= (sc->fast_on ? q->fast_off : q->fast_on);
                    if (sc->fast_on) {
                        target = (int32_t)q->anim_fast[i][1];
                    }
                }
            }
            if (!fast) {
                sc->fast_on = target >= 0;
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
    /* the game's own blend between two of Sonic's rows is no reason to show it: a flight goes on through it, then fades to the row it ends on. At most 5 pairs: a link between running rows takes 2 (RM.7, 4 steps), the stop's chain from row 10 through the walk's top takes 5 before the long one;
       the one from a walk to the stand takes 12 (MEASURED), and he should not hang in the air for all of it */
    if (target >= 0 || !d->linking) {
        sc->link_held = 0;
    } else if ((sc->anim & SA1_SKIN_ANIM_FLY) && sc->cur >= 0 && (sc->w >= 1.0f || sc->rising) && sc->link_held < 5) {
        target = sc->cur;
        sc->link_held++;
    }
    /* a fade in starts from what he last showed, held against his root, and not from Sonic's row of the pair (sa1k_append) */
    if (target >= 0) {
        if (!sc->rising && sc->w < 1.0f) {
            sc->rising = 1;
            sc->hold_take = 1;
        }
    } else {
        sc->rising = 0;
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
            sc->rate_row = d->row;
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
    /* round 1.0.0-beta1 (the owner's answer 17): a drawn speed loop of Sonic's own tree steps as the game steps it at this speed (k, the top of this file); any other row at the world row's rate */
    {
        double k = s_rate_k(cs, sc->cur, sc->rate_row);
        sc->phase = fmod(sc->phase + (k != 1.0 ? sc->rate * k : sc->rate), 1.0);
    }
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
    /* the source's way back from the held pose to the row he is in, when a fade in turns round half way */
    if (sc->w >= 1.0f) {
        sc->rising = 0;
        sc->h = 0.0f;
    } else if (!sc->rising && sc->h > 0.0f) {
        sc->h = sc->h > 0.125f ? sc->h - 0.125f : 0.0f;
    }
    if (sc->w <= 0.0f && target < 0) {
        sc->cur = sc->prev = -1;
        sc->x = 1.0f;
        sc->phase_have = 0;
        sc->rate = 0.0;
        sc->rate_row = -1;
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

/* each part's rotation and the torso's place in a flown row: the joint column of a private evaluation of that row's own tree (RM.3). Round 1.0.0-beta1 (the owner's answer 17): a row of Sonic's
   own tree by the mesh column instead, the nodes his own drawing turns each part by (sa1k_append's live pose), so the row is drawn as Sonic's live row would be; *low: with the plant, the least
   of Sonic's own body along up in that pose (the plant's reference), else 1e30 */
static sa1_status s_flown(sa1_context *ctx, sa1k_char *sc, const sa1_charset *cs, int32_t row, const double root[12], const double up[3], double r[][9], double p0[3], double *low)
{
    const sa1k_state *q = sc->q;
    const sa1_action *ac = &cs->actions[row];
    const sa1_motion *mo = &SA1_VEC(cs->motions, sa1_motion)[ac->motion];
    double rr[9], f = sc->phase * (double)mo->num_frames;
    uint32_t i;
    sa1_status st = sa1_pose_evaluate(ctx, cs, &sc->inst, ac->object, ac->motion, (float)f, SA1_POSE_MODEL_SPACE | SA1_POSE_NO_WELD);
    const float *t0;
    int own = ac->object == cs->main_object;
    *low = 1e30;
    if (st != SA1_OK) {
        return st;
    }
    m3_of(root, rr);
    for (i = 0; i < q->num_parts; i++) {
        const sk_part *pt = &q->parts[i];
        int32_t n = (own && pt->mesh >= 0 && (uint32_t)pt->mesh < q->body_nodes) ? pt->mesh : pt->joint;
        const float *m = sc->inst.node_mtx + 12u * (uint32_t)n;
        double a[9], b[9];
        a[0] = m[0]; a[1] = m[1]; a[2] = m[2];
        a[3] = m[4]; a[4] = m[5]; a[5] = m[6];
        a[6] = m[8]; a[7] = m[9]; a[8] = m[10];
        m3_rot(a, b);
        m3_mul(rr, b, a);
        m3_mul_t(a, q->rest_r[n], b);
        m3_mul(b, pt->ag, r[i]);
    }
    t0 = sc->inst.node_mtx + 12u * (uint32_t)((own && q->parts[0].mesh >= 0 && (uint32_t)q->parts[0].mesh < q->body_nodes) ? q->parts[0].mesh : q->parts[0].joint);
    p0[0] = root[0] * t0[3] + root[1] * t0[7] + root[2] * t0[11] + root[3];
    p0[1] = root[4] * t0[3] + root[5] * t0[7] + root[6] * t0[11] + root[7];
    p0[2] = root[8] * t0[3] + root[9] * t0[7] + root[10] * t0[11] + root[11];
    if (own && q->plant) {
        double l = 1e30;
        for (i = 0; i < q->body_nodes && i < SA1K_NODES; i++) {
            const float *m = sc->inst.node_mtx + 12u * i;
            double w[12];
            int k;
            if (q->body_model[i] < 0) {
                continue;
            }
            /* root * node (model space) */
            for (k = 0; k < 3; k++) {
                const double *rk = root + 4 * k;
                w[4 * k + 0] = rk[0] * m[0] + rk[1] * m[4] + rk[2] * m[8];
                w[4 * k + 1] = rk[0] * m[1] + rk[1] * m[5] + rk[2] * m[9];
                w[4 * k + 2] = rk[0] * m[2] + rk[1] * m[6] + rk[2] * m[10];
                w[4 * k + 3] = rk[0] * m[3] + rk[1] * m[7] + rk[2] * m[11] + rk[3];
            }
            l = s_model_low(cs, q->body_model[i], w, up, l);
        }
        *low = l;
    }
    return SA1_OK;
}

/* ---- the append ------------------------------------------------------------------------------------------------------------------------------------ */
sa1_status sa1k_append(sa1_context *ctx, sa1k_char *sc, const sa1_charset *cs, sa1_meshbuf *b)
{
    const sa1k_state *q = sc->q;
    double turn[SA1K_PARTS_MAX][9], r[SA1K_PARTS_MAX][9], fr[SA1K_PARTS_MAX][9], fo[SA1K_PARTS_MAX][9], p0[3], pos[SA1K_PARTS_MAX][3];
    double up[3] = { 0.0, 1.0, 0.0 }, low_live = 1e30, low_fly = 1e30;      /* round 1.0.0-beta1: the plant */
    const sa1_motion *mo = NULL;
    sa1_emit_info ei;
    sa1_draw_state ds;
    uint32_t i;
    sa1_status st;
    int torso = q->parts[0].mesh;
    sc->drew = 0;
    sc->out_live = 1.0f;
    sc->out_have_flown = 0;
    if (torso < 0 || !sc->have[torso]) {
        sc->last_src_ok = 0;
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
    /* what a flight fades from: Sonic's row of the pair, but for a fade in: the pose of the pair before it started, held against his root (the top of this file) */
    if (sc->drive.have_root) {
        double base[12], rt[9], cen[3], d0[3];
        s_fly_root(&sc->drive, 0.0, base);
        m3_of(base, rt);
        cen[0] = base[3];
        cen[1] = base[7];
        cen[2] = base[11];
        if (sc->hold_take) {
            if (sc->last_src_ok) {
                memcpy(sc->held_r, sc->last_src_r, q->num_parts * sizeof sc->held_r[0]);
                memcpy(sc->held_p0, sc->last_src_p0, sizeof sc->held_p0);
            } else {
                /* nothing drawn before it (a landing out of the ball): this pair's pose, no pop to mind */
                for (i = 0; i < q->num_parts; i++) {
                    m3_t_mul(rt, r[i], sc->held_r[i]);
                }
                d0[0] = p0[0] - cen[0];
                d0[1] = p0[1] - cen[1];
                d0[2] = p0[2] - cen[2];
                sc->held_p0[0] = rt[0] * d0[0] + rt[3] * d0[1] + rt[6] * d0[2];
                sc->held_p0[1] = rt[1] * d0[0] + rt[4] * d0[1] + rt[7] * d0[2];
                sc->held_p0[2] = rt[2] * d0[0] + rt[5] * d0[1] + rt[8] * d0[2];
            }
            sc->hold_take = 0;
            sc->h = 1.0f;
        }
        if (sc->h > 0.0f) {
            double hp[3];
            for (i = 0; i < q->num_parts; i++) {
                double a[9];
                m3_mul(rt, sc->held_r[i], a);
                m3_slerp(r[i], a, (double)sc->h, r[i]);
            }
            hp[0] = cen[0] + rt[0] * sc->held_p0[0] + rt[1] * sc->held_p0[1] + rt[2] * sc->held_p0[2];
            hp[1] = cen[1] + rt[3] * sc->held_p0[0] + rt[4] * sc->held_p0[1] + rt[5] * sc->held_p0[2];
            hp[2] = cen[2] + rt[6] * sc->held_p0[0] + rt[7] * sc->held_p0[1] + rt[8] * sc->held_p0[2];
            p0[0] += (double)sc->h * (hp[0] - p0[0]);
            p0[1] += (double)sc->h * (hp[1] - p0[1]);
            p0[2] += (double)sc->h * (hp[2] - p0[2]);
            sc->out_live = 1.0f - sc->h;
        }
        for (i = 0; i < q->num_parts; i++) {
            m3_t_mul(rt, r[i], sc->last_src_r[i]);
        }
        d0[0] = p0[0] - cen[0];
        d0[1] = p0[1] - cen[1];
        d0[2] = p0[2] - cen[2];
        sc->last_src_p0[0] = rt[0] * d0[0] + rt[3] * d0[1] + rt[6] * d0[2];
        sc->last_src_p0[1] = rt[1] * d0[0] + rt[4] * d0[1] + rt[7] * d0[2];
        sc->last_src_p0[2] = rt[2] * d0[0] + rt[5] * d0[1] + rt[8] * d0[2];
        sc->last_src_ok = 1;
    }
    /* round 1.0.0-beta1, the plant (the top of this file): Sonic's up axis, and his own lowest point in the pose the world drew */
    if (q->plant) {
        if (sc->drive.have_root) {
            double ub[12];
            s_fly_root(&sc->drive, 0.0, ub);
            up[0] = ub[1];
            up[1] = ub[5];
            up[2] = ub[9];
        }
        for (i = 0; i < q->body_nodes && i < SA1K_NODES; i++) {
            if (sc->have[i] && q->body_model[i] >= 0) {
                low_live = s_model_low(cs, q->body_model[i], sc->mtx[i], up, low_live);
            }
        }
    }
    /* the flying (4.4) */
    if (sc->w > 0.0f && sc->cur >= 0 && sc->drive.have_root) {
        double root[12], pf[3], po[3], lf = 1e30, lo = 1e30;
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
        st = s_flown(ctx, sc, cs, sc->cur, root, up, fr, pf, &lf);
        if (st != SA1_OK) {
            return st;
        }
        if (sc->prev >= 0 && sc->x < 1.0f && s_row_ok(cs, sc->prev)) {
            st = s_flown(ctx, sc, cs, sc->prev, root, up, fo, po, &lo);
            if (st != SA1_OK) {
                return st;
            }
            for (i = 0; i < q->num_parts; i++) {
                m3_slerp(fo[i], fr[i], (double)sc->x, fr[i]);
            }
            pf[0] = po[0] + (double)sc->x * (pf[0] - po[0]);
            pf[1] = po[1] + (double)sc->x * (pf[1] - po[1]);
            pf[2] = po[2] + (double)sc->x * (pf[2] - po[2]);
            if (lf < 1e29 && lo < 1e29) {
                lf = lo + (double)sc->x * (lf - lo);
            }
        }
        low_fly = lf;
        memcpy(sc->out_flown, fr, q->num_parts * sizeof fr[0]);
        sc->out_have_flown = 1;
        sc->out_live *= 1.0f - sc->w;
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
    /* round 1.0.0-beta1, the plant: the figure along up, its lowest point on Sonic's own lowest point of the pose it shows (a flight's share by w) */
    sc->out_shift = 0.0;
    if (q->plant) {
        double ls = low_live, lm = 1e30;
        if (sc->out_have_flown && low_fly < 1e29) {
            ls = ls < 1e29 ? ls + (double)sc->w * (low_fly - ls) : low_fly;
        }
        for (i = 0; i < q->num_parts; i++) {
            const uint8_t *pts = q->part_pts[i];
            double a0, a1, a2, bb;
            uint32_t v;
            if (pts == NULL) {
                continue;
            }
            a0 = q->scale * (up[0] * r[i][0] + up[1] * r[i][3] + up[2] * r[i][6]);
            a1 = q->scale * (up[0] * r[i][1] + up[1] * r[i][4] + up[2] * r[i][7]);
            a2 = q->scale * (up[0] * r[i][2] + up[1] * r[i][5] + up[2] * r[i][8]);
            bb = up[0] * pos[i][0] + up[1] * pos[i][1] + up[2] * pos[i][2];
            for (v = 0; v < q->part_np[i]; v++) {
                double hgt = a0 * (double)sa1_rdf32(pts + 12u * v) + a1 * (double)sa1_rdf32(pts + 12u * v + 4u) + a2 * (double)sa1_rdf32(pts + 12u * v + 8u) + bb;
                if (hgt < lm) {
                    lm = hgt;
                }
            }
        }
        if (ls < 1e29 && lm < 1e29) {
            double s = ls - lm;
            for (i = 0; i < q->num_parts; i++) {
                pos[i][0] += s * up[0];
                pos[i][1] += s * up[1];
                pos[i][2] += s * up[2];
            }
            sc->out_shift = s;
        }
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
    /* the glow: the parts again, one shell for each pass the game drew of Sonic's aura. Its colour: the entry's where the game's is blue, the game's green over the rest, so the pulse is the game's; shared out by the three passes'
       blue (1 + 1 + 0.3 = 2.3) so that shells over shells add up to the colour once: Sonic's blue can saturate and stay blue, a colour with two channels would turn white */
    if ((sc->anim & SA1_SKIN_ANIM_GLOW) && q->glow && sc->glow_n > 0) {
        int32_t pass;
        for (pass = 0; pass < sc->glow_n; pass++) {
            const float *c = sc->glow_cmat[pass];
            double gg = c[2] > 0.0f ? (double)c[2] : 0.0, bb = c[3] > 0.0f ? (double)c[3] : 0.0;
            sa1_emit_info gi;
            sa1_draw_state gs;
            memset(&gs, 0, sizeof gs);
            gs.material = 1;
            gs.cmat[0] = 1.0f;
            gs.cmat[1] = (float)((q->glow_rgb[0] * bb + (1.0 - q->glow_rgb[0]) * gg) / 2.3);
            gs.cmat[2] = (float)((q->glow_rgb[1] * bb + (1.0 - q->glow_rgb[1]) * gg) / 2.3);
            gs.cmat[3] = (float)((q->glow_rgb[2] * bb + (1.0 - q->glow_rgb[2]) * gg) / 2.3);
            memset(&gi, 0, sizeof gi);
            gi.reader = s_read;
            gi.user = (void *)(uintptr_t)q;
            gi.ds = &gs;
            gi.ctx = ctx;
            gi.texlist = q->root;
            gi.source = SA1_SOURCE_MODEL;
            gi.snap = 1;
            gi.snap_mats = q->glow_mats;
            for (i = 0; i < q->num_parts; i++) {
                const sk_part *p = &q->parts[i];
                uint32_t nn0 = b->nn, k;
                double m[12];
                if (p->model == 0u) {
                    continue;
                }
                m[0] = q->scale * r[i][0]; m[1] = q->scale * r[i][1]; m[2] = q->scale * r[i][2]; m[3] = pos[i][0];
                m[4] = q->scale * r[i][3]; m[5] = q->scale * r[i][4]; m[6] = q->scale * r[i][5]; m[7] = pos[i][1];
                m[8] = q->scale * r[i][6]; m[9] = q->scale * r[i][7]; m[10] = q->scale * r[i][8]; m[11] = pos[i][2];
                gi.depth = p->depth + 1;
                gi.snap_np = q->glow_np[i];
                gi.snap_pts = q->glow_pts[i] + 12u * q->glow_np[i] * (uint32_t)pass;
                gi.snap_nrm = q->glow_nrm[i];
                gi.snap_nmat = q->glow_nmat[i];
                st = sa1_pose_emit_model_ex(ctx, b, &gi, p->model, m);
                if (st != SA1_OK) {
                    return st;
                }
                for (k = nn0; k < b->nn; k++) {
                    b->node_fl[k] |= (uint8_t)SA1_NODE_SKIN_GLOW;
                }
            }
        }
    }
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
    out->live_mix = sc->out_live;
    out->have_flown = sc->out_have_flown;
    memcpy(out->flown_r, sc->out_flown, sizeof out->flown_r);
    memcpy(out->src_r, sc->last_src_r, sizeof out->src_r);
    out->glow_passes = sc->glow_n;
}

uint32_t sa1k_test_glow_bytes(const sa1k_char *sc)
{
    return sc->q->glow_bytes;
}
