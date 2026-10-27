/*
 * CPU mesh evaluator: poses an object tree with one motion frame, applies the weld
 * list, and emits indexed + per-corner geometry in host space.
 *
 * Transform of a node (Ninja, column vectors, 3x4 row-major):
 *     local = T(pos) * Rz * Ry * Rx * S(scl)      (NJD_EVAL_ZXY_ANG: T * Ry * Rx * Rz * S)
 *     world = parent_world * local
 * with UNIT_POS/ANG/SCL skipping a component.  Motion keys replace pos/ang/scl where a
 * track exists (and clear the matching UNIT_ flag).  Keys are interpolated linearly
 * (every player motion uses inp_fn 2/3, linear); angles are interpolated as plain
 * integers (open question in sa1_assets.md 12.2), and a frame past the last key
 * interpolates towards the first key at nbFrame (loop closure).
 *
 * Pipeline:  node matrices -> every model vertex to world space (per-instance work
 * buffer) -> welds rewrite work vertices -> drawn nodes are emitted.
 * Welds (PL_JOIN_VERTEX), processed in table order on world-space vertices:
 *     type 1 (average):     src and dst vertex := their average (normals: normalised sum)
 *     type 2 (source):      dst vertex := src vertex
 *     type 3 (destination): src vertex := dst vertex
 *     types 4-9:            attach points (right/left hand, right/left foot, user0/1);
 *                           provisional: origin of the source node.
 * The game rewrites the destination model's vertex array in place; working on
 * per-instance world-space copies gives the same result without touching shared data.
 *
 * The same file holds the emitter of the display capture (design 7.2, sa1_capture.h): models
 * the game itself drew in a character's world (materials, points and normals as the draw read
 * them: the capture's v3 snapshot; meshsets, UVs and vertex colours from the world's arena) and
 * emitted with the conversions of this evaluator (host space, winding, UVs, colours, texture ids),
 * and the draws of the render output (SA1_API_RENDER: one sa1_draw per meshset, texture ids
 * resolved into the character's archives, the material state classified).
 *
 * Colours: the characters are drawn by the hand-written renderer 8C037098, which takes the
 * vertex RGB from the stage's light palette (N.L lookup, 8C037350) and only the alpha from
 * the material (8C037872); the material's diffuse RGB is not used.  Both the evaluator and the
 * capture therefore emit white RGB with the material's alpha for the characters (the host's
 * lighting stands in for the palette): Sonic's irises have a black diffuse colour, a host
 * that multiplied it in drew them black.
 */
#include "sa1_capture.h"

#define BAMS_TO_RAD (6.283185307179586 / 65536.0)

static void mtx_ident(float m[12])
{
    memset(m, 0, 12 * sizeof(float));
    m[0] = m[5] = m[10] = 1.0f;
}

/* r = a * b (3x4 affine) */
static void mtx_mul(const float a[12], const float b[12], float r[12])
{
    float t[12];
    int i;
    for (i = 0; i < 3; i++) {
        const float *ar = a + 4 * i;
        t[4 * i + 0] = ar[0] * b[0] + ar[1] * b[4] + ar[2] * b[8];
        t[4 * i + 1] = ar[0] * b[1] + ar[1] * b[5] + ar[2] * b[9];
        t[4 * i + 2] = ar[0] * b[2] + ar[1] * b[6] + ar[2] * b[10];
        t[4 * i + 3] = ar[0] * b[3] + ar[1] * b[7] + ar[2] * b[11] + ar[3];
    }
    memcpy(r, t, sizeof t);
}

static void mtx_rot(float m[12], int axis, double bams)
{
    float r[12];
    double a = bams * BAMS_TO_RAD;
    float c = (float)cos(a), s = (float)sin(a);
    mtx_ident(r);
    if (axis == 0) {
        r[5] = c; r[6] = -s; r[9] = s; r[10] = c;
    } else if (axis == 1) {
        r[0] = c; r[2] = s; r[8] = -s; r[10] = c;
    } else {
        r[0] = c; r[1] = -s; r[4] = s; r[5] = c;
    }
    mtx_mul(m, r, m);
}

static void mtx_local(uint32_t flags, const float pos[3], const double ang[3], const float scl[3], float m[12])
{
    mtx_ident(m);
    if (!(flags & SA1_EVAL_UNIT_POS)) {
        m[3] = pos[0];
        m[7] = pos[1];
        m[11] = pos[2];
    }
    if (!(flags & SA1_EVAL_UNIT_ANG)) {
        static const int zyx[3] = { 2, 1, 0 }, yxz[3] = { 1, 0, 2 };
        const int *order = (flags & SA1_EVAL_ZXY_ANG) ? yxz : zyx;
        int k;
        for (k = 0; k < 3; k++)
            if (ang[order[k]] != 0.0)
                mtx_rot(m, order[k], ang[order[k]]);
    }
    if (!(flags & SA1_EVAL_UNIT_SCL)) {
        int i;
        for (i = 0; i < 3; i++) {
            m[4 * i + 0] *= scl[0];
            m[4 * i + 1] *= scl[1];
            m[4 * i + 2] *= scl[2];
        }
    }
}

static void xform_point(const float m[12], const float v[3], float o[3])
{
    float x = v[0], y = v[1], z = v[2];
    o[0] = m[0] * x + m[1] * y + m[2] * z + m[3];
    o[1] = m[4] * x + m[5] * y + m[6] * z + m[7];
    o[2] = m[8] * x + m[9] * y + m[10] * z + m[11];
}

static void xform_dir_norm(const float m[12], const float v[3], float o[3])
{
    float x = v[0], y = v[1], z = v[2], l;
    o[0] = m[0] * x + m[1] * y + m[2] * z;
    o[1] = m[4] * x + m[5] * y + m[6] * z;
    o[2] = m[8] * x + m[9] * y + m[10] * z;
    l = sqrtf(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
    if (l > 1e-20f) {
        o[0] /= l; o[1] /= l; o[2] /= l;
    } else {
        o[0] = 0.0f; o[1] = 1.0f; o[2] = 0.0f;
    }
}

/* Key search: index lo with key[lo].frame <= f < key[lo+1].frame, or n-1. */
#define KEY_SEARCH(keys, n, f, lo)                                  \
    do {                                                            \
        uint32_t hi_ = (n) - 1u;                                    \
        lo = 0;                                                     \
        if ((f) >= (float)(keys)[hi_].frame) { lo = hi_; break; }   \
        while (hi_ - lo > 1u) {                                     \
            uint32_t mid_ = (lo + hi_) / 2u;                        \
            if ((float)(keys)[mid_].frame <= (f)) lo = mid_;        \
            else hi_ = mid_;                                        \
        }                                                           \
    } while (0)

/* Interpolation weights: returns 0 = use key a only, else blend a->b with *t. */
static int key_pair(uint32_t n, uint32_t fa, uint32_t fb_or_first, uint32_t lo, float f, uint32_t nf,
                    int at_end, float *t)
{
    double span;
    (void)n;
    (void)lo;
    if (at_end) {
        /* loop closure: last key -> first key at nbFrame */
        if (fb_or_first != 0 || fa >= nf)
            return 0;
        span = (double)nf - fa;
    } else {
        span = (double)fb_or_first - fa;
    }
    if (span <= 0.0)
        return 0;
    *t = (float)(((double)f - fa) / span);
    if (*t <= 0.0f)
        return 0;
    if (*t > 1.0f)
        *t = 1.0f;
    return 1;
}

static void sample3(const sa1_key3 *k, uint32_t n, float f, uint32_t nf, float out[3])
{
    uint32_t lo, j;
    const sa1_key3 *a, *b;
    float t;
    if (n == 1 || f <= (float)k[0].frame) {
        memcpy(out, k[0].v, 3 * sizeof(float));
        return;
    }
    KEY_SEARCH(k, n, f, lo);
    a = &k[lo];
    b = lo + 1u < n ? &k[lo + 1u] : &k[0];
    if (!key_pair(n, a->frame, b->frame, lo, f, nf, lo + 1u >= n, &t)) {
        memcpy(out, a->v, 3 * sizeof(float));
        return;
    }
    for (j = 0; j < 3; j++)
        out[j] = a->v[j] + (b->v[j] - a->v[j]) * t;
}

static void sample_ang(const sa1_keya *k, uint32_t n, float f, uint32_t nf, double out[3])
{
    uint32_t lo, j;
    const sa1_keya *a, *b;
    float t;
    if (n == 1 || f <= (float)k[0].frame) {
        for (j = 0; j < 3; j++)
            out[j] = (double)k[0].v[j];
        return;
    }
    KEY_SEARCH(k, n, f, lo);
    a = &k[lo];
    b = lo + 1u < n ? &k[lo + 1u] : &k[0];
    if (!key_pair(n, a->frame, b->frame, lo, f, nf, lo + 1u >= n, &t)) {
        for (j = 0; j < 3; j++)
            out[j] = (double)a->v[j];
        return;
    }
    for (j = 0; j < 3; j++)
        out[j] = (double)a->v[j] + ((double)b->v[j] - (double)a->v[j]) * (double)t;
}

/* ------------------------------------------------------------------------- */

void sa1_instance_free(sa1_context *ctx, sa1_instance *inst)
{
    sa1_mem_free(ctx, inst->node_mtx);
    sa1_mem_free(ctx, inst->wpos);
    sa1_mem_free(ctx, inst->wnrm);
    sa1_mem_free(ctx, inst->remap);
    sa1_mem_free(ctx, inst->out_pos);
    sa1_mem_free(ctx, inst->out_nrm);
    sa1_mem_free(ctx, inst->out_uv);
    sa1_mem_free(ctx, inst->out_node_mtx);
    sa1_mem_free(ctx, inst->out_idx);
    sa1_mem_free(ctx, inst->out_mat);
    sa1_mem_free(ctx, inst->out_col);
    sa1_mem_free(ctx, inst->out_flags);
    sa1_mem_free(ctx, inst->out_tex);
}

sa1_status sa1_instance_alloc(sa1_context *ctx, const sa1_charset *cs, sa1_instance *inst)
{
    size_t nn = cs->max_nodes ? cs->max_nodes : 1u, nw = cs->max_work_vertices ? cs->max_work_vertices : 1u;
    size_t nv = cs->max_vertices ? cs->max_vertices : 1u, nt = cs->max_triangles ? cs->max_triangles : 1u;
    inst->cap_nodes = (uint32_t)nn;
    inst->cap_work = (uint32_t)nw;
    inst->cap_vertices = (uint32_t)nv;
    inst->cap_tris = (uint32_t)nt;
    inst->node_mtx = (float *)sa1_mem_array(ctx, 12u * nn, sizeof(float));
    inst->out_node_mtx = (float *)sa1_mem_array(ctx, 12u * nn, sizeof(float));
    inst->wpos = (float *)sa1_mem_array(ctx, 3u * nw, sizeof(float));
    inst->wnrm = (float *)sa1_mem_array(ctx, 3u * nw, sizeof(float));
    inst->remap = (uint32_t *)sa1_mem_array(ctx, nw, sizeof(uint32_t));
    inst->out_pos = (float *)sa1_mem_array(ctx, 3u * nv, sizeof(float));
    inst->out_nrm = (float *)sa1_mem_array(ctx, 3u * nv, sizeof(float));
    inst->out_idx = (uint32_t *)sa1_mem_array(ctx, 3u * nt, sizeof(uint32_t));
    inst->out_uv = (float *)sa1_mem_array(ctx, 6u * nt, sizeof(float));
    inst->out_col = (uint8_t *)sa1_mem_array(ctx, 12u * nt, 1);
    inst->out_tex = (int16_t *)sa1_mem_array(ctx, nt, sizeof(int16_t));
    inst->out_mat = (uint32_t *)sa1_mem_array(ctx, nt, sizeof(uint32_t));
    inst->out_flags = (uint8_t *)sa1_mem_array(ctx, nt, 1);
    if (!inst->node_mtx || !inst->out_node_mtx || !inst->wpos || !inst->wnrm || !inst->remap || !inst->out_pos ||
        !inst->out_nrm || !inst->out_idx || !inst->out_uv || !inst->out_col || !inst->out_tex || !inst->out_mat ||
        !inst->out_flags) {
        sa1_instance_free(ctx, inst);
        return SA1_ERR_OUT_OF_MEMORY;
    }
    return SA1_OK;
}

static void avg_normal(float *a, float *b)
{
    float n[3], l;
    n[0] = a[0] + b[0];
    n[1] = a[1] + b[1];
    n[2] = a[2] + b[2];
    l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l > 1e-20f) {
        n[0] /= l; n[1] /= l; n[2] /= l;
        memcpy(a, n, sizeof n);
        memcpy(b, n, sizeof n);
    }
}

static void apply_welds(const sa1_charset *cs, const sa1_object *o, int32_t object, sa1_instance *inst,
                        uint32_t *attach_valid, float attach[SA1_ATTACH_COUNT][3])
{
    uint32_t i, k;
    for (i = 0; i < cs->num_welds; i++) {
        const sa1_weld *w = &cs->welds[i];
        const sa1_node *s, *d;
        if (w->object != object || w->src_node < 0 || (uint32_t)w->src_node >= o->num_nodes)
            continue;
        s = &o->nodes[w->src_node];
        if (w->type >= 4 && w->type <= 9) {
            const float *m = inst->node_mtx + 12u * (uint32_t)w->src_node;
            uint32_t slot = (uint32_t)w->type - 4u;
            attach[slot][0] = m[3];
            attach[slot][1] = m[7];
            attach[slot][2] = m[11];
            *attach_valid |= 1u << slot;
            continue;
        }
        if (w->dst_node < 0 || (uint32_t)w->dst_node >= o->num_nodes)
            continue;
        d = &o->nodes[w->dst_node];
        if (s->vbase == 0xFFFFFFFFu || d->vbase == 0xFFFFFFFFu)
            continue;
        for (k = 0; k < w->num_pairs; k++) {
            uint32_t a = s->vbase + w->pairs[2 * k], b = d->vbase + w->pairs[2 * k + 1];
            float *pa = inst->wpos + 3u * a, *pb = inst->wpos + 3u * b;
            float *na = inst->wnrm + 3u * a, *nb = inst->wnrm + 3u * b;
            if (w->type == 1) {
                int j;
                for (j = 0; j < 3; j++)
                    pa[j] = pb[j] = 0.5f * (pa[j] + pb[j]);
                avg_normal(na, nb);
            } else if (w->type == 2) {
                memcpy(pb, pa, 3 * sizeof(float));
                memcpy(nb, na, 3 * sizeof(float));
            } else {
                memcpy(pa, pb, 3 * sizeof(float));
                memcpy(na, nb, 3 * sizeof(float));
            }
        }
    }
}

sa1_status sa1_pose_evaluate(sa1_context *ctx, const sa1_charset *cs, sa1_instance *inst, int32_t object,
                             int32_t motion, float frame, uint32_t flags)
{
    const sa1_object *o;
    const sa1_motion *mo = NULL;
    float root[12], f = 0.0f;
    uint32_t i, nf = 0, vcount = 0, tcount = 0, attach_valid = 0;
    float attach[SA1_ATTACH_COUNT][3];
    int flip = (ctx->coords.flags & SA1_COORDS_FLIP_WINDING) != 0;
    int model_space = (flags & SA1_POSE_MODEL_SPACE) != 0;
    const float *hp = inst->position;
    sa1_geometry *g = &inst->geom;

    if (object < 0 || (uint32_t)object >= cs->objects.count)
        return SA1_ERR_NOT_FOUND;
    o = &SA1_VEC(cs->objects, sa1_object)[object];
    if (motion >= 0 && (uint32_t)motion < cs->motions.count)
        mo = &SA1_VEC(cs->motions, sa1_motion)[motion];
    if (o->num_nodes > inst->cap_nodes || o->work_vertices > inst->cap_work || o->drawn_vertices > inst->cap_vertices ||
        o->drawn_tris > inst->cap_tris)
        return SA1_ERR_LIMIT;
    memset(attach, 0, sizeof attach);

    /* model -> SA1 world: the game turns models by 180 degrees (they face -X) */
    mtx_ident(root);
    if (!model_space) {
        double a = 3.141592653589793 + (double)inst->yaw;
        float c = (float)cos(a), s = (float)sin(a);
        root[0] = c; root[2] = s; root[8] = -s; root[10] = c;
    }
    if (mo) {
        nf = mo->num_frames;
        f = sa1_finite(frame) ? frame : 0.0f;
        f = fmodf(f, (float)nf);
        if (f < 0.0f)
            f += (float)nf;
        if (f >= (float)nf)
            f = 0.0f;
    }

    /* 1. node matrices */
    for (i = 0; i < o->num_nodes; i++) {
        const sa1_node *nd = &o->nodes[i];
        uint32_t fl = nd->flags;
        float pos[3], scl[3], local[12];
        double ang[3];
        memcpy(pos, nd->pos, sizeof pos);
        memcpy(scl, nd->scl, sizeof scl);
        ang[0] = nd->ang[0];
        ang[1] = nd->ang[1];
        ang[2] = nd->ang[2];
        if (mo && i < mo->num_nodes) {
            const sa1_motion_node *mn = &mo->nodes[i];
            if (mn->num_pos) {
                sample3(mn->pos, mn->num_pos, f, nf, pos);
                fl &= ~SA1_EVAL_UNIT_POS;
            }
            if (mn->num_ang) {
                sample_ang(mn->ang, mn->num_ang, f, nf, ang);
                fl &= ~SA1_EVAL_UNIT_ANG;
            }
            if (mn->num_scl) {
                sample3(mn->scl, mn->num_scl, f, nf, scl);
                fl &= ~SA1_EVAL_UNIT_SCL;
            }
        }
        mtx_local(fl, pos, ang, scl, local);
        mtx_mul(nd->parent >= 0 ? inst->node_mtx + 12u * (uint32_t)nd->parent : root, local,
                inst->node_mtx + 12u * i);
    }

    /* 2. every model vertex into the world-space work buffer */
    for (i = 0; i < o->num_nodes; i++) {
        const sa1_node *nd = &o->nodes[i];
        const sa1_model *m;
        const float *mt = inst->node_mtx + 12u * i;
        uint32_t v;
        if (nd->model < 0)
            continue;
        m = &SA1_VEC(cs->models, sa1_model)[nd->model];
        for (v = 0; v < m->num_points; v++) {
            float *wp = inst->wpos + 3u * (nd->vbase + v), *wn = inst->wnrm + 3u * (nd->vbase + v);
            xform_point(mt, m->points + 3u * v, wp);
            if (m->normals) {
                xform_dir_norm(mt, m->normals + 3u * v, wn);
            } else {
                wn[0] = 0.0f; wn[1] = 1.0f; wn[2] = 0.0f;
            }
        }
    }

    /* 3. welds */
    if (!(flags & SA1_POSE_NO_WELD))
        apply_welds(cs, o, object, inst, &attach_valid, attach);

    /* 4. emit drawn nodes in host space */
    for (i = 0; i < o->num_nodes; i++) {
        const sa1_node *nd = &o->nodes[i];
        const sa1_model *m;
        uint32_t v;
        if (!nd->drawn)
            continue;
        m = &SA1_VEC(cs->models, sa1_model)[nd->model];
        for (v = 0; v < m->num_points; v++) {
            uint32_t w = nd->vbase + v;
            float *op = inst->out_pos + 3u * vcount;
            inst->remap[w] = vcount;
            sa1_to_host_point(ctx, inst->wpos + 3u * w, op);
            if (!model_space) {
                op[0] += hp[0];
                op[1] += hp[1];
                op[2] += hp[2];
            }
            sa1_to_host_dir(ctx, inst->wnrm + 3u * w, inst->out_nrm + 3u * vcount);
            vcount++;
        }
    }
    for (i = 0; i < o->num_nodes; i++) {
        const sa1_node *nd = &o->nodes[i];
        const sa1_model *m;
        uint32_t s;
        if (!nd->drawn)
            continue;
        m = &SA1_VEC(cs->models, sa1_model)[nd->model];
        for (s = 0; s < m->num_meshsets; s++) {
            const sa1_meshset *ms = &m->meshsets[s];
            const sa1_material *mat = ms->mat_id < m->num_mats ? &m->mats[ms->mat_id] : NULL;
            uint32_t attr = mat ? mat->attr : 0u, diffuse = mat ? mat->diffuse : 0xFFFFFFFFu, t;
            int16_t tex = -1;
            uint8_t tfl = (uint8_t)((ms->uv ? SA1_TRI_HAS_UV : 0u) | (ms->vcolor ? SA1_TRI_HAS_VCOLOR : 0u));
            if (mat && (attr & SA1_MAT_USE_TEXTURE))
                tex = (int16_t)(mat->tex_id > 32767u ? 32767u : mat->tex_id);
            for (t = 0; t < ms->num_tris; t++) {
                uint32_t c[3], k;
                c[0] = ms->tris[3u * t];
                c[1] = ms->tris[3u * t + 1u];
                c[2] = ms->tris[3u * t + 2u];
                if (flip) {
                    uint32_t tmp = c[1]; c[1] = c[2]; c[2] = tmp;
                }
                for (k = 0; k < 3; k++) {
                    /* the character renderer's colours (as the capture emits them, sa1_capture.h ds_color): white
                       light, the material's alpha; vertex colours are not used by it */
                    uint32_t ci = c[k], col = (diffuse & 0xFF000000u) | 0x00FFFFFFu;
                    uint8_t *oc = inst->out_col + 12u * tcount + 4u * k;
                    inst->out_idx[3u * tcount + k] = inst->remap[nd->vbase + ms->corner_vtx[ci]];
                    if (ms->uv) {
                        inst->out_uv[6u * tcount + 2u * k] = (float)ms->uv[2u * ci] / 255.0f;
                        inst->out_uv[6u * tcount + 2u * k + 1u] = (float)ms->uv[2u * ci + 1u] / 255.0f;
                    } else {
                        inst->out_uv[6u * tcount + 2u * k] = 0.0f;
                        inst->out_uv[6u * tcount + 2u * k + 1u] = 0.0f;
                    }
                    oc[0] = (uint8_t)(col >> 16);
                    oc[1] = (uint8_t)(col >> 8);
                    oc[2] = (uint8_t)col;
                    oc[3] = (uint8_t)(col >> 24);
                }
                inst->out_tex[tcount] = tex;
                inst->out_mat[tcount] = attr;
                inst->out_flags[tcount] = tfl;
                tcount++;
            }
        }
    }

    /* node matrices and attach points in host space */
    for (i = 0; i < o->num_nodes; i++) {
        const float *m = inst->node_mtx + 12u * i;
        float *om = inst->out_node_mtx + 12u * i;
        int col;
        for (col = 0; col < 4; col++) {
            float in[3], out[3];
            in[0] = m[col];
            in[1] = m[4 + col];
            in[2] = m[8 + col];
            sa1_to_host_point(ctx, in, out);
            if (col == 3 && !model_space) {
                out[0] += hp[0];
                out[1] += hp[1];
                out[2] += hp[2];
            }
            om[col] = out[0];
            om[4 + col] = out[1];
            om[8 + col] = out[2];
        }
    }
    for (i = 0; i < SA1_ATTACH_COUNT; i++) {
        if (attach_valid & (1u << i)) {
            float out[3];
            sa1_to_host_point(ctx, attach[i], out);
            if (!model_space) {
                out[0] += hp[0];
                out[1] += hp[1];
                out[2] += hp[2];
            }
            memcpy(g->attach[i], out, sizeof out);
        } else {
            g->attach[i][0] = g->attach[i][1] = g->attach[i][2] = 0.0f;
        }
    }

    g->num_vertices = vcount;
    g->positions = inst->out_pos;
    g->normals = inst->out_nrm;
    g->num_triangles = tcount;
    g->indices = inst->out_idx;
    g->uvs = inst->out_uv;
    g->colors = inst->out_col;
    g->textures = inst->out_tex;
    g->materials = inst->out_mat;
    g->tri_flags = inst->out_flags;
    g->num_nodes = o->num_nodes;
    g->node_matrices = inst->out_node_mtx;
    g->attach_valid = attach_valid;
    g->object_addr = o->ptr;
    return SA1_OK;
}

/* ------------------------------------------------------------------------- */
/* Emitter of models drawn by the game (display capture, design 7.2)         */
/* ------------------------------------------------------------------------- */

void sa1_meshbuf_reset(sa1_meshbuf *b)
{
    b->nv = 0;
    b->nt = 0;
    b->nn = 0;
    b->bad_models = 0;
    b->mirrored = 0;
    b->ndraw = 0;
    b->nspr = 0;
    b->nlv = 0;
    b->order = 0;
    b->sig = 1469598103934665603ull;
}

void sa1_meshbuf_free(const sa1_context *ctx, sa1_meshbuf *b)
{
    sa1_mem_free(ctx, b->pos);
    sa1_mem_free(ctx, b->nrm);
    sa1_mem_free(ctx, b->idx);
    sa1_mem_free(ctx, b->uv);
    sa1_mem_free(ctx, b->col);
    sa1_mem_free(ctx, b->tex);
    sa1_mem_free(ctx, b->mat);
    sa1_mem_free(ctx, b->fl);
    sa1_mem_free(ctx, b->tarc);
    sa1_mem_free(ctx, b->tidx);
    sa1_mem_free(ctx, b->node_mtx);
    sa1_mem_free(ctx, b->node_v);
    sa1_mem_free(ctx, b->node_wm);
    sa1_mem_free(ctx, b->node_model);
    sa1_mem_free(ctx, b->node_key);
    sa1_mem_free(ctx, b->node_fl);
    sa1_mem_free(ctx, b->node_depth);
    sa1_mem_free(ctx, b->node_tl);
    sa1_mem_free(ctx, b->lpos);
    sa1_mem_free(ctx, b->lnrm);
    sa1_mem_free(ctx, b->corner);
    sa1_mem_free(ctx, b->tri);
    sa1_mem_free(ctx, b->draws);
    sa1_mem_free(ctx, b->spr);
    sa1_mem_free(ctx, b->lv);
    memset(b, 0, sizeof *b);
}

/* a larger copy of the first `used` elements of an array (NULL on failure, the old array is kept) */
static void *grow_array(const sa1_context *ctx, void *old, size_t used, size_t n, size_t elem)
{
    void *p = sa1_mem_array(ctx, n, elem);
    if (!p)
        return NULL;
    if (old && used)
        memcpy(p, old, used * elem);
    return p;
}

static uint32_t grow_cap(uint32_t cur, uint32_t need, uint32_t floor_)
{
    uint64_t c = cur ? cur : floor_;
    while (c < need)
        c *= 2u;
    return c > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)c;
}

sa1_status sa1_meshbuf_reserve(const sa1_context *ctx, sa1_meshbuf *b, uint32_t nv, uint32_t nt, uint32_t nn)
{
    if (nv > b->cap_v) {
        uint32_t c = grow_cap(b->cap_v, nv, 1024u);
        float *p = (float *)grow_array(ctx, b->pos, 3u * (size_t)b->nv, 3u * (size_t)c, sizeof(float));
        float *q = (float *)grow_array(ctx, b->nrm, 3u * (size_t)b->nv, 3u * (size_t)c, sizeof(float));
        float *lp = (float *)grow_array(ctx, b->lpos, 3u * (size_t)b->nv, 3u * (size_t)c, sizeof(float));
        float *ln = (float *)grow_array(ctx, b->lnrm, 3u * (size_t)b->nv, 3u * (size_t)c, sizeof(float));
        if (!p || !q || !lp || !ln) {
            sa1_mem_free(ctx, p);
            sa1_mem_free(ctx, q);
            sa1_mem_free(ctx, lp);
            sa1_mem_free(ctx, ln);
            return SA1_ERR_OUT_OF_MEMORY;
        }
        sa1_mem_free(ctx, b->pos);
        sa1_mem_free(ctx, b->nrm);
        sa1_mem_free(ctx, b->lpos);
        sa1_mem_free(ctx, b->lnrm);
        b->pos = p;
        b->nrm = q;
        b->lpos = lp;
        b->lnrm = ln;
        b->cap_v = c;
    }
    if (nt > b->cap_t) {
        uint32_t c = grow_cap(b->cap_t, nt, 1024u);
        size_t u = b->nt;
        uint32_t *idx = (uint32_t *)grow_array(ctx, b->idx, 3u * u, 3u * (size_t)c, sizeof(uint32_t));
        float *uv = (float *)grow_array(ctx, b->uv, 6u * u, 6u * (size_t)c, sizeof(float));
        uint8_t *col = (uint8_t *)grow_array(ctx, b->col, 12u * u, 12u * (size_t)c, 1);
        int16_t *tex = (int16_t *)grow_array(ctx, b->tex, u, c, sizeof(int16_t));
        uint32_t *mat = (uint32_t *)grow_array(ctx, b->mat, u, c, sizeof(uint32_t));
        uint8_t *fl = (uint8_t *)grow_array(ctx, b->fl, u, c, 1);
        int16_t *tarc = (int16_t *)grow_array(ctx, b->tarc, u, c, sizeof(int16_t));
        int16_t *tidx = (int16_t *)grow_array(ctx, b->tidx, u, c, sizeof(int16_t));
        if (!idx || !uv || !col || !tex || !mat || !fl || !tarc || !tidx) {
            sa1_mem_free(ctx, idx);
            sa1_mem_free(ctx, uv);
            sa1_mem_free(ctx, col);
            sa1_mem_free(ctx, tex);
            sa1_mem_free(ctx, mat);
            sa1_mem_free(ctx, fl);
            sa1_mem_free(ctx, tarc);
            sa1_mem_free(ctx, tidx);
            return SA1_ERR_OUT_OF_MEMORY;
        }
        sa1_mem_free(ctx, b->idx);
        sa1_mem_free(ctx, b->uv);
        sa1_mem_free(ctx, b->col);
        sa1_mem_free(ctx, b->tex);
        sa1_mem_free(ctx, b->mat);
        sa1_mem_free(ctx, b->fl);
        sa1_mem_free(ctx, b->tarc);
        sa1_mem_free(ctx, b->tidx);
        b->idx = idx;
        b->uv = uv;
        b->col = col;
        b->tex = tex;
        b->mat = mat;
        b->fl = fl;
        b->tarc = tarc;
        b->tidx = tidx;
        b->cap_t = c;
    }
    if (nn > b->cap_n) {
        uint32_t c = grow_cap(b->cap_n, nn, 64u);
        size_t u = b->nn;
        float *p = (float *)grow_array(ctx, b->node_mtx, 12u * u, 12u * (size_t)c, sizeof(float));
        uint32_t *nv_ = (uint32_t *)grow_array(ctx, b->node_v, 2u * u, 2u * (size_t)c, sizeof(uint32_t));
        double *wm = (double *)grow_array(ctx, b->node_wm, 12u * u, 12u * (size_t)c, sizeof(double));
        uint32_t *md = (uint32_t *)grow_array(ctx, b->node_model, u, c, sizeof(uint32_t));
        uint64_t *ky = (uint64_t *)grow_array(ctx, b->node_key, u, c, sizeof(uint64_t));
        uint8_t *fl = (uint8_t *)grow_array(ctx, b->node_fl, u, c, 1);
        int16_t *dp = (int16_t *)grow_array(ctx, b->node_depth, u, c, sizeof(int16_t));
        uint32_t *tl = (uint32_t *)grow_array(ctx, b->node_tl, u, c, sizeof(uint32_t));
        if (!p || !nv_ || !wm || !md || !ky || !fl || !dp || !tl) {
            sa1_mem_free(ctx, p);
            sa1_mem_free(ctx, nv_);
            sa1_mem_free(ctx, wm);
            sa1_mem_free(ctx, md);
            sa1_mem_free(ctx, ky);
            sa1_mem_free(ctx, fl);
            sa1_mem_free(ctx, dp);
            sa1_mem_free(ctx, tl);
            return SA1_ERR_OUT_OF_MEMORY;
        }
        sa1_mem_free(ctx, b->node_mtx);
        sa1_mem_free(ctx, b->node_v);
        sa1_mem_free(ctx, b->node_wm);
        sa1_mem_free(ctx, b->node_model);
        sa1_mem_free(ctx, b->node_key);
        sa1_mem_free(ctx, b->node_fl);
        sa1_mem_free(ctx, b->node_depth);
        sa1_mem_free(ctx, b->node_tl);
        b->node_mtx = p;
        b->node_v = nv_;
        b->node_wm = wm;
        b->node_model = md;
        b->node_key = ky;
        b->node_fl = fl;
        b->node_depth = dp;
        b->node_tl = tl;
        b->cap_n = c;
    }
    return SA1_OK;
}

sa1_status sa1_meshbuf_reserve_extra(const sa1_context *ctx, sa1_meshbuf *b, uint32_t nd, uint32_t ns, uint32_t nl)
{
    if (b->ndraw + (uint64_t)nd > b->cap_draw) {
        uint32_t c = grow_cap(b->cap_draw, b->ndraw + nd, 64u);
        sa1_draw *p = (sa1_draw *)grow_array(ctx, b->draws, b->ndraw, c, sizeof(sa1_draw));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        sa1_mem_free(ctx, b->draws);
        b->draws = p;
        b->cap_draw = c;
    }
    if (b->nspr + (uint64_t)ns > b->cap_spr) {
        uint32_t c = grow_cap(b->cap_spr, b->nspr + ns, 32u);
        sa1_sprite *p = (sa1_sprite *)grow_array(ctx, b->spr, b->nspr, c, sizeof(sa1_sprite));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        sa1_mem_free(ctx, b->spr);
        b->spr = p;
        b->cap_spr = c;
    }
    if (b->nlv + (uint64_t)nl > b->cap_lv) {
        uint32_t c = grow_cap(b->cap_lv, b->nlv + nl, 64u);
        sa1_line_vertex *p = (sa1_line_vertex *)grow_array(ctx, b->lv, b->nlv, c, sizeof(sa1_line_vertex));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        sa1_mem_free(ctx, b->lv);
        b->lv = p;
        b->cap_lv = c;
    }
    return SA1_OK;
}

sa1_draw *sa1_meshbuf_add_draw(const sa1_context *ctx, sa1_meshbuf *b)
{
    sa1_draw *d;
    if (sa1_meshbuf_reserve_extra(ctx, b, 1u, 0u, 0u) != SA1_OK)
        return NULL;
    d = &b->draws[b->ndraw++];
    memset(d, 0, sizeof *d);
    d->order = b->order++;
    return d;
}

/* scratch for one meshset: ncorner corners, ntri triangles */
static sa1_status meshbuf_scratch(const sa1_context *ctx, sa1_meshbuf *b, uint32_t ncorner, uint32_t ntri)
{
    if (ncorner > b->cap_corner) {
        uint32_t c = grow_cap(b->cap_corner, ncorner, 1024u);
        uint16_t *p = (uint16_t *)sa1_mem_array(ctx, c, sizeof(uint16_t));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        sa1_mem_free(ctx, b->corner);
        b->corner = p;
        b->cap_corner = c;
    }
    if (ntri > b->cap_tri) {
        uint32_t c = grow_cap(b->cap_tri, ntri, 1024u);
        uint32_t *p = (uint32_t *)sa1_mem_array(ctx, 3u * (size_t)c, sizeof(uint32_t));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        sa1_mem_free(ctx, b->tri);
        b->tri = p;
        b->cap_tri = c;
    }
    return SA1_OK;
}
sa1_status sa1_meshbuf_reserve_scratch(const sa1_context *ctx, sa1_meshbuf *b, uint32_t nc, uint32_t nt)
{
    return meshbuf_scratch(ctx, b, nc, nt);
}

/* One NJS_MESHSET (0x18 bytes at ms) of a model with np points: corners into b->corner, triangles (corner
 * indices, the loader's rules) into b->tri; *nc / *ntri get the counts. */
static sa1_status decode_meshset(const sa1_context *ctx, sa1_meshbuf *b, sa1_guest_read_fn reader, void *user,
                                 const uint8_t *ms, uint32_t np, uint32_t *nc, uint32_t *ntri)
{
    uint32_t tm = sa1_rd16(ms), nmesh = sa1_rd16(ms + 2), polys = sa1_rd32(ms + 4);
    uint32_t ptype = tm >> 14, ncorn = 0, maxtri = 0, pos = 0, i, j, k = 0, t = 0;
    const uint8_t *s = NULL;
    sa1_status st;

    *nc = 0;
    *ntri = 0;
    if (nmesh == 0)
        return SA1_OK;
    if (ptype < 2) {
        ncorn = nmesh * (ptype == 0 ? 3u : 4u);
        maxtri = ptype == 0 ? nmesh : 2u * nmesh;
        s = reader(user, polys, 2u * ncorn);
        if (!s)
            return SA1_ERR_BAD_FORMAT;
    } else {
        /* strips / N-gons: u16 header (count | 0x8000 reversed), then the count indices */
        for (i = 0; i < nmesh; i++) {
            const uint8_t *h = reader(user, polys + pos, 2);
            uint32_t n;
            if (!h)
                return SA1_ERR_BAD_FORMAT;
            n = sa1_rd16(h) & 0x7FFFu;
            pos += 2u + 2u * n;
            ncorn += n;
            if (ncorn > SA1_MAX_CORNERS)
                return SA1_ERR_LIMIT;
            if (n >= 3)
                maxtri += n - 2u;
        }
        s = reader(user, polys, pos);
        if (!s)
            return SA1_ERR_BAD_FORMAT;
    }
    st = meshbuf_scratch(ctx, b, ncorn, maxtri);
    if (st != SA1_OK)
        return st;
    pos = 0;
    for (i = 0; i < nmesh; i++) {
        uint32_t n, rev = 0;
        if (ptype < 2) {
            n = ptype == 0 ? 3u : 4u;
        } else {
            uint32_t hdr = sa1_rd16(s + pos);
            pos += 2;
            n = hdr & 0x7FFFu;
            rev = (hdr >> 15) & 1u;
        }
        for (j = 0; j < n; j++) {
            uint32_t vi = sa1_rd16(s + pos);
            pos += 2;
            if (vi >= np)
                return SA1_ERR_BAD_FORMAT;
            b->corner[k + j] = (uint16_t)vi;
        }
        if (ptype == 0) {
            b->tri[3 * t] = k; b->tri[3 * t + 1] = k + 1; b->tri[3 * t + 2] = k + 2;
            t++;
        } else if (ptype == 1) {
            b->tri[3 * t] = k; b->tri[3 * t + 1] = k + 1; b->tri[3 * t + 2] = k + 2;
            t++;
            b->tri[3 * t] = k + 2; b->tri[3 * t + 1] = k + 1; b->tri[3 * t + 2] = k + 3;
            t++;
        } else {
            for (j = 0; j + 2 < n; j++) {
                uint32_t a = k + j, c1 = k + j + 1, c2 = k + j + 2;
                if ((j & 1u) ^ rev) {
                    uint32_t tmp = a; a = c1; c1 = tmp;
                }
                if (b->corner[a] != b->corner[c1] && b->corner[c1] != b->corner[c2] && b->corner[a] != b->corner[c2]) {
                    b->tri[3 * t] = a; b->tri[3 * t + 1] = c1; b->tri[3 * t + 2] = c2;
                    t++;
                }
            }
        }
        k += n;
    }
    *nc = ncorn;
    *ntri = t;
    return SA1_OK;
}

/* one colour channel (0..255) with the draw state's constant material channel (-1..1) */
static uint8_t ds_channel(uint32_t v, int32_t mode, float c)
{
    double x = mode > 0 ? (double)c * 255.0 : (double)v + (double)c * 255.0;
    if (!(x > 0.0))
        return 0;
    return x >= 255.0 ? 255 : (uint8_t)(x + 0.5);
}

/* x (computed exactly in double) rounded to single precision toward zero and flushed to zero below the smallest normal:
 * the game's FPU mode (FPSCR 0x00040001: RM round to zero, DN; sadc include/pc/sh4_fpmodel.h), whatever the host's mode */
static float f32_rz(double x)
{
    float f = (float)x;
    if ((double)f != x && fabs((double)f) > fabs(x)) {
        uint32_t u;
        memcpy(&u, &f, 4);
        u -= 1u;                    /* one step toward zero (sign and magnitude) */
        memcpy(&f, &u, 4);
    }
    if (fabs((double)f) < 1.17549435e-38)
        f = 0.0f;
    return f;
}

/* The character renderer's vertex alpha, as its colour set-up 8C037872 computes it in both branches (lit, and the
 * IGNORE_LIGHT one at 8C0378F4; asm/1st_read/text/8C036FFC.src): a = the material's diffuse alpha byte x D_8C038F10
 * (H'3B808083, about 1/255); an offset material (mode < 0, control-3D 0x20) adds the constant material's alpha and
 * clamps to 0..1; a constant material (mode > 0, 0x10) takes the constant alpha alone, not clamped; then a x 255.0
 * (D_8C038F1C) and FTRC - truncation, saturating -, whose low byte is stored. In single precision with the game's
 * rounding toward zero: Amy's first hammer ghost (offset -0.7) is 0.30000001 x 255 = 76.5 -> 76 as on the Dreamcast,
 * where the old rounding (ds_channel) gave 77; every other copy alpha is unchanged (sadc docs/research/
 * gsa1_070_translucency.md 4.1 and T4 with its Review R2.7 / R3.7). Without a constant material it is the byte itself
 * (all 256 values checked). */
static uint8_t hand_alpha(uint32_t byte, int32_t mode, float c)
{
    static const uint32_t k_bits = 0x3B808083u;    /* D_8C038F10 */
    float k, a;
    double x;
    int32_t i;
    memcpy(&k, &k_bits, 4);
    if (!(fabs((double)c) >= 1.17549435e-38))
        c = 0.0f;                   /* denormals are zero (DN) */
    a = f32_rz((double)(byte & 0xFFu) * (double)k);
    if (mode > 0) {
        a = c;
    } else if (mode < 0) {
        a = f32_rz((double)a + (double)c);
        if (a > 1.0f)
            a = 1.0f;
        if (!(a > 0.0f))
            a = 0.0f;
    }
    x = (double)f32_rz((double)a * 255.0);
    if (x != x || x <= -2147483648.0)
        i = (int32_t)0x80000000u;   /* FTRC: NaN and below the range give H'80000000 */
    else if (x >= 2147483647.0)
        i = 0x7FFFFFFF;
    else
        i = (int32_t)x;             /* toward zero */
    return (uint8_t)((uint32_t)i & 0xFFu);
}

/* An ARGB colour of a material with attribute attr through the draw state. The character renderer (ds->hand) takes
 * the vertex RGB from the stage's light palette by N.L (8C037350) - white light here, the host lights - and only the
 * material alpha (with the constant material's alpha offset, 8C037872); IGNORE_LIGHT materials white as well. Ninja's
 * action draw (8C6209A8, ds->tex_white: control-3D bit 0x200) gives a textured material RGB 1.0 and keeps the diffuse
 * alpha (8C620A94-8C620AB8) before the constant material (round 4c F2(a); a vertex-coloured model under it is
 * UNVERIFIED: none is drawn so). */
static uint32_t ds_color(const sa1_draw_state *ds, uint32_t attr, uint32_t col)
{
    if (!ds)
        return col;
    if (ds->hand) {
        uint32_t a = hand_alpha(col >> 24, ds->material, ds->cmat[0]);
        return (a << 24) | 0x00FFFFFFu;
    }
    if (ds->tex_white && (attr & SA1_MAT_USE_TEXTURE))
        col |= 0x00FFFFFFu;
    if (!ds->material)
        return col;
    return ((uint32_t)ds_channel(col >> 24, ds->material, ds->cmat[0]) << 24) |
           ((uint32_t)ds_channel((col >> 16) & 0xFFu, ds->material, ds->cmat[1]) << 16) |
           ((uint32_t)ds_channel((col >> 8) & 0xFFu, ds->material, ds->cmat[2]) << 8) |
           (uint32_t)ds_channel(col & 0xFFu, ds->material, ds->cmat[3]);
}

uint32_t sa1_texture_alpha_kind(const uint8_t *rgba, uint32_t w, uint32_t h)
{
    size_t i, n = (size_t)w * h;
    uint32_t kind = SA1_TEXALPHA_NONE;
    if (!rgba)
        return SA1_TEXALPHA_NONE;
    for (i = 0; i < n; i++) {
        uint8_t a = rgba[4u * i + 3u];
        if (a == 255)
            continue;
        if (a != 0)
            return SA1_TEXALPHA_BLEND;
        kind = SA1_TEXALPHA_BINARY;
    }
    return kind;
}

uint32_t sa1_draw_classify(uint32_t attr, int hand, uint32_t tex_alpha, uint32_t min_alpha, uint8_t *bsrc, uint8_t *bdst)
{
    uint32_t f = 0, src = (attr >> 29) & 7u, dst = (attr >> 26) & 7u;
    if (hand)
        f |= SA1_DRAW_PALETTE_LIT;
    if (attr & SA1_MAT_IGNORE_LIGHT)
        f |= SA1_DRAW_UNLIT;
    if (attr & SA1_MAT_DOUBLE_SIDE)
        f |= SA1_DRAW_DOUBLE_SIDED;
    if (attr & SA1_MAT_USE_ENV)
        f |= SA1_DRAW_ENV_MAP;
    if (!(attr & SA1_MAT_USE_ALPHA)) {
        /* the opaque list: the texture's alpha is not used */
        *bsrc = (uint8_t)SA1_BLEND_ONE;
        *bdst = (uint8_t)SA1_BLEND_ZERO;
        return f;
    }
    *bsrc = (uint8_t)src;
    *bdst = (uint8_t)dst;
    if (src == SA1_BLEND_SRC_ALPHA && dst == SA1_BLEND_INV_SRC_ALPHA) {
        if (tex_alpha == SA1_TEXALPHA_BLEND || min_alpha < 255u)
            f |= SA1_DRAW_BLEND;
        else if (tex_alpha == SA1_TEXALPHA_BINARY)
            f |= SA1_DRAW_ALPHA_TEST;
        else
            f |= SA1_DRAW_USE_ALPHA;
    } else {
        /* other factors: with nothing translucent (alpha 1 everywhere) SRC_ALPHA acts as ONE and INV_SRC_ALPHA as
           ZERO, so e.g. E-102's SRC_ALPHA / ZERO materials are opaque */
        uint32_t es = src, ed = dst;
        if (tex_alpha == SA1_TEXALPHA_NONE && min_alpha >= 255u) {
            es = src == SA1_BLEND_SRC_ALPHA ? SA1_BLEND_ONE : src == SA1_BLEND_INV_SRC_ALPHA ? SA1_BLEND_ZERO : src;
            ed = dst == SA1_BLEND_SRC_ALPHA ? SA1_BLEND_ONE : dst == SA1_BLEND_INV_SRC_ALPHA ? SA1_BLEND_ZERO : dst;
        }
        if (es == SA1_BLEND_ONE && ed == SA1_BLEND_ZERO)
            f |= SA1_DRAW_USE_ALPHA;
        else
            f |= SA1_DRAW_BLEND;
    }
    if ((f & SA1_DRAW_BLEND) && dst == SA1_BLEND_ONE)
        f |= SA1_DRAW_ADDITIVE;
    return f;
}

uint8_t sa1_blend_of_ninja(uint32_t mode, int dst)
{
    switch (mode) {
    case 0:  /* NJD_COLOR_BLENDING_BOTHINVALPHA */
        return (uint8_t)(dst ? SA1_BLEND_SRC_ALPHA : SA1_BLEND_INV_SRC_ALPHA);
    case 1:  /* BOTHSRCALPHA */
        return (uint8_t)(dst ? SA1_BLEND_INV_SRC_ALPHA : SA1_BLEND_SRC_ALPHA);
    case 2:  /* DESTALPHA */
        return (uint8_t)SA1_BLEND_DST_ALPHA;
    case 3:  /* DESTCOLOR (a source factor) */
        return (uint8_t)(dst ? SA1_BLEND_ONE : SA1_BLEND_OTHER_COLOR);
    case 4:  /* INVDESTALPHA */
        return (uint8_t)SA1_BLEND_INV_DST_ALPHA;
    case 5:  /* INVDESTCOLOR */
        return (uint8_t)(dst ? SA1_BLEND_ONE : SA1_BLEND_INV_OTHER_COLOR);
    case 6:  /* INVSRCALPHA */
        return (uint8_t)SA1_BLEND_INV_SRC_ALPHA;
    case 7:  /* INVSRCCOLOR (a destination factor) */
        return (uint8_t)(dst ? SA1_BLEND_INV_OTHER_COLOR : SA1_BLEND_ONE);
    case 8:  /* SRCALPHA */
        return (uint8_t)SA1_BLEND_SRC_ALPHA;
    case 9:  /* SRCCOLOR */
        return (uint8_t)(dst ? SA1_BLEND_OTHER_COLOR : SA1_BLEND_ONE);
    case 10: /* ONE */
        return (uint8_t)SA1_BLEND_ONE;
    case 11: /* ZERO */
        return (uint8_t)SA1_BLEND_ZERO;
    default:
        return (uint8_t)(dst ? SA1_BLEND_INV_SRC_ALPHA : SA1_BLEND_SRC_ALPHA);
    }
}

static uint64_t sig_mix(uint64_t h, uint32_t v)
{
    uint32_t k;
    for (k = 0; k < 4; k++) {
        h ^= (v >> (8u * k)) & 0xFFu;
        h *= 1099511628211ull;
    }
    return h;
}

/* appends a triangle draw for triangles [first, first + n) of b, merged into the last draw when that one is a triangle
 * draw ending at `first` with the same state */
static sa1_status add_tri_draw(const sa1_context *ctx, sa1_meshbuf *b, uint32_t first, uint32_t n, int32_t arc,
                               int32_t tex, uint32_t attr, uint32_t flags, uint8_t bs, uint8_t bd, uint8_t source,
                               uint32_t diffuse)
{
    sa1_draw *d = b->ndraw ? &b->draws[b->ndraw - 1u] : NULL;
    if (d && d->prim == SA1_PRIM_TRIANGLES && d->first + d->count == first && d->archive == arc && d->texture == tex &&
        d->material == attr && d->flags == flags && d->blend_src == bs && d->blend_dst == bd && d->source == source) {
        d->count += n;
        return SA1_OK;
    }
    d = sa1_meshbuf_add_draw(ctx, b);
    if (!d)
        return SA1_ERR_OUT_OF_MEMORY;
    d->prim = SA1_PRIM_TRIANGLES;
    d->first = first;
    d->count = n;
    d->archive = arc;
    d->texture = tex;
    d->material = attr;
    d->flags = flags;
    d->blend_src = bs;
    d->blend_dst = bd;
    d->source = source;
    d->diffuse[0] = (uint8_t)(diffuse >> 16);
    d->diffuse[1] = (uint8_t)(diffuse >> 8);
    d->diffuse[2] = (uint8_t)diffuse;
    d->diffuse[3] = (uint8_t)(diffuse >> 24);
    return SA1_OK;
}

sa1_status sa1_pose_emit_model(sa1_context *ctx, sa1_meshbuf *b, sa1_guest_read_fn reader, void *user, uint32_t model,
                               const double m[12], int textured, const sa1_draw_state *ds)
{
    sa1_emit_info ei;
    memset(&ei, 0, sizeof ei);
    ei.reader = reader;
    ei.user = user;
    ei.ds = ds;
    ei.textured = textured;
    ei.source = (uint8_t)((ds && ds->hand) ? SA1_SOURCE_CHARACTER : SA1_SOURCE_MODEL);
    return sa1_pose_emit_model_ex(ctx, b, &ei, model, m);
}

sa1_status sa1_pose_emit_model_ex(sa1_context *ctx, sa1_meshbuf *b, const sa1_emit_info *ei, uint32_t model,
                                  const double m[12])
{
    sa1_guest_read_fn reader = ei->reader;
    void *user = ei->user;
    const sa1_draw_state *ds = ei->ds;
    const uint8_t *mp = reader(user, model, 0x28), *pts = NULL, *nrms = NULL, *mats = NULL;
    uint32_t nv0 = b->nv, nt0 = b->nt, nn0 = b->nn, nd0 = b->ndraw, ord0 = b->order, np, nms, nmat, msp, i, s;
    uint64_t sig0 = b->sig, key = 1469598103934665603ull;
    int32_t snp;
    double det;
    int flip;
    sa1_status st = SA1_ERR_BAD_FORMAT;
    float *om;

    if (!mp)
        goto bad;
    snp = sa1_rds32(mp + 8);
    msp = sa1_rd32(mp + 12);
    nms = sa1_rd16(mp + 20);
    nmat = sa1_rd16(mp + 22);
    if (ei->snap) {
        /* v3: the materials, points and normals as the draw read them (the meshsets are static data) */
        snp = (int32_t)ei->snap_np;
        nmat = ei->snap_nmat;
    }
    if (snp < 0 || (uint32_t)snp > SA1_MAX_POINTS || nms > SA1_MAX_MESHSETS || nmat > SA1_MAX_MATERIALS)
        goto bad;
    np = (uint32_t)snp;
    if (ei->snap) {
        pts = ei->snap_pts;
        nrms = ei->snap_nrm;
        mats = ei->snap_mats;
        if ((np && !pts) || (nmat && !mats))
            goto bad;
    } else {
        if (np) {
            pts = reader(user, sa1_rd32(mp), 12u * np);
            if (!pts)
                goto bad;
            if (sa1_rd32(mp + 4))
                nrms = reader(user, sa1_rd32(mp + 4), 12u * np);
        }
        if (nmat) {
            mats = reader(user, sa1_rd32(mp + 16), 20u * nmat);
            if (!mats)
                goto bad;
        }
    }
    det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
          m[2] * (m[4] * m[9] - m[5] * m[8]);
    flip = ((ctx->coords.flags & SA1_COORDS_FLIP_WINDING) != 0) ^ (det < 0.0);
    st = sa1_meshbuf_reserve(ctx, b, b->nv + np, b->nt, b->nn + 1u);
    if (st != SA1_OK)
        return st;
    b->sig = sig_mix(sig_mix(b->sig, model & 0x1FFFFFFFu), np);

    /* vertices: model -> SA1 world (double) -> host space; the model-local points and normals are kept for the render
       interpolation (sa1_character.c), with a hash of them */
    for (i = 0; i < 12u * np; i++)
        key = (key ^ pts[i]) * 1099511628211ull;
    if (nrms) {
        for (i = 0; i < 12u * np; i++)
            key = (key ^ nrms[i]) * 1099511628211ull;
    }
    for (i = 0; i < np; i++) {
        const uint8_t *q = pts + 12u * i;
        double x = sa1_rdf32(q), y = sa1_rdf32(q + 4), z = sa1_rdf32(q + 8);
        float w[3], n[3];
        float *lp = b->lpos + 3u * (nv0 + i), *ln = b->lnrm + 3u * (nv0 + i);
        lp[0] = (float)x;
        lp[1] = (float)y;
        lp[2] = (float)z;
        if (nrms) {
            ln[0] = sa1_rdf32(nrms + 12u * i);
            ln[1] = sa1_rdf32(nrms + 12u * i + 4u);
            ln[2] = sa1_rdf32(nrms + 12u * i + 8u);
        } else {
            ln[0] = 0.0f;
            ln[1] = 1.0f;
            ln[2] = 0.0f;
        }
        w[0] = (float)(m[0] * x + m[1] * y + m[2] * z + m[3]);
        w[1] = (float)(m[4] * x + m[5] * y + m[6] * z + m[7]);
        w[2] = (float)(m[8] * x + m[9] * y + m[10] * z + m[11]);
        sa1_to_host_point(ctx, w, b->pos + 3u * (nv0 + i));
        if (nrms) {
            const uint8_t *r = nrms + 12u * i;
            double nx = sa1_rdf32(r), ny = sa1_rdf32(r + 4), nz = sa1_rdf32(r + 8), l;
            double ox = m[0] * nx + m[1] * ny + m[2] * nz, oy = m[4] * nx + m[5] * ny + m[6] * nz;
            double oz = m[8] * nx + m[9] * ny + m[10] * nz;
            l = sqrt(ox * ox + oy * oy + oz * oz);
            if (l > 1e-20 && l == l) {
                n[0] = (float)(ox / l); n[1] = (float)(oy / l); n[2] = (float)(oz / l);
            } else {
                n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f;
            }
        } else {
            n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f;
        }
        sa1_to_host_dir(ctx, n, b->nrm + 3u * (nv0 + i));
    }
    b->nv = nv0 + np;

    /* triangles, meshset by meshset: one draw each (merged with the previous one when the state is the same) */
    for (s = 0; s < nms; s++) {
        const uint8_t *ms = reader(user, msp + 24u * s, 24), *mat = NULL, *uvs = NULL, *vcs = NULL;
        uint32_t nc = 0, ntri = 0, mat_id, attr = 0, diffuse = 0xFFFFFFFFu, t, tfirst, min_a = 255u, cls, ta;
        int32_t arc = -1, tex = -1;
        uint8_t tfl, bs, bd;
        if (!ms) {
            st = SA1_ERR_BAD_FORMAT;
            goto bad;
        }
        st = decode_meshset(ctx, b, reader, user, ms, np, &nc, &ntri);
        if (st != SA1_OK)
            goto bad;
        b->sig = sig_mix(b->sig, ntri);
        if (ntri == 0)
            continue;
        if (sa1_rd32(ms + 20)) {
            uvs = reader(user, sa1_rd32(ms + 20), 4u * nc);
            if (!uvs) {
                st = SA1_ERR_BAD_FORMAT;
                goto bad;
            }
        }
        if (sa1_rd32(ms + 16)) {
            vcs = reader(user, sa1_rd32(ms + 16), 4u * nc);
            if (!vcs) {
                st = SA1_ERR_BAD_FORMAT;
                goto bad;
            }
        }
        mat_id = sa1_rd16(ms) & 0x3FFFu;
        cls = 0;
        if (mat_id < nmat)
            mat = mats + 20u * mat_id;
        else if (sa1_rd32(mp + 16) != 0)
            /* Ninja reads mats[id] without bounding id by nbMat (8C6108B0 at 8C610A5E-8C610A7C, 8C6209A8 at
               8C620A30-8C620A62): the Light Speed aura's 20 node models have nbMat 0 and use material 0 at mats (round
               4c F3, host half: read live through the bounded reader; the v3 snapshot holds nbMat materials) */
            mat = reader(user, sa1_rd32(mp + 16) + 20u * mat_id, 20);
        if (mat) {
            diffuse = sa1_rd32(mat);
            attr = sa1_rd32(mat + 16);
            if (ds && ds->attr_masks)
                attr = (attr & ds->attr_and) | ds->attr_or;
            if (attr & SA1_MAT_USE_TEXTURE) {
                uint32_t id = sa1_rd32(mat + 12) & 0x3FFFFFFFu;
                if (ei->resolve) {
                    tex = ei->resolve(user, ei->texlist, id, &arc);
                    if (tex < 0) {
                        arc = -1;
                        cls |= SA1_DRAW_NO_TEXTURE;
                    }
                } else if (ei->textured) {
                    arc = 0;
                    tex = (int16_t)(id > 32767u ? 32767u : id);
                }
                if (tex > 32767)
                    tex = 32767;
            }
        }
        tfl = (uint8_t)((uvs ? SA1_TRI_HAS_UV : 0u) | (vcs ? SA1_TRI_HAS_VCOLOR : 0u));
        st = sa1_meshbuf_reserve(ctx, b, b->nv, b->nt + ntri, b->nn + 1u);
        if (st != SA1_OK)
            goto bad;
        tfirst = b->nt;
        for (t = 0; t < ntri; t++) {
            uint32_t c[3], k, o = b->nt;
            c[0] = b->tri[3u * t];
            c[1] = b->tri[3u * t + 1u];
            c[2] = b->tri[3u * t + 2u];
            if (flip) {
                uint32_t tmp = c[1]; c[1] = c[2]; c[2] = tmp;
            }
            for (k = 0; k < 3; k++) {
                uint32_t ci = c[k], col = ds_color(ds, attr, (vcs && !(ds && ds->hand)) ? sa1_rd32(vcs + 4u * ci) : diffuse);
                uint8_t *oc = b->col + 12u * o + 4u * k;
                b->idx[3u * o + k] = nv0 + b->corner[ci];
                if (uvs) {
                    b->uv[6u * o + 2u * k] = (float)(int16_t)sa1_rd16(uvs + 4u * ci) / 255.0f;
                    b->uv[6u * o + 2u * k + 1u] = (float)(int16_t)sa1_rd16(uvs + 4u * ci + 2u) / 255.0f;
                } else {
                    b->uv[6u * o + 2u * k] = 0.0f;
                    b->uv[6u * o + 2u * k + 1u] = 0.0f;
                }
                oc[0] = (uint8_t)(col >> 16);
                oc[1] = (uint8_t)(col >> 8);
                oc[2] = (uint8_t)col;
                oc[3] = (uint8_t)(col >> 24);
                if (oc[3] < min_a)
                    min_a = oc[3];
            }
            b->tex[o] = (int16_t)(arc == 0 ? tex : -1);
            b->tarc[o] = (int16_t)arc;
            b->tidx[o] = (int16_t)tex;
            b->mat[o] = attr;
            b->fl[o] = tfl;
            b->nt = o + 1u;
        }
        ta = SA1_TEXALPHA_NONE;
        if (arc >= 0 && tex >= 0) {
            if (ei->tex_alpha)
                ta = ei->tex_alpha(user, arc, tex);
            else if (ei->ctx && ei->cs)
                ta = sa1_charset_texture_alpha(ei->ctx, ei->cs, arc, tex);
        }
        cls |= sa1_draw_classify(attr, ds && ds->hand, ta, min_a, &bs, &bd) | ei->extra_flags;
        /* round 6a: a draw with the body's own texlist (the capture's `own`: ei->textured) is the body whatever archive
           holds it (Sonic and Super Sonic draw either body: sa1_character.c body_texlist); before, own draws always
           resolved into archive 0 */
        if (arc > 0 && !(ei->resolve && ei->textured))
            cls |= SA1_DRAW_EFFECT;
        st = add_tri_draw(ctx, b, tfirst, ntri, arc, tex, attr, cls, bs, bd, ei->source, diffuse);
        if (st != SA1_OK)
            goto bad;
    }

    /* the model's matrix in host space (as the evaluator's node matrices) */
    om = b->node_mtx + 12u * nn0;
    for (i = 0; i < 4; i++) {
        float in[3], out[3];
        in[0] = (float)m[i];
        in[1] = (float)m[4 + i];
        in[2] = (float)m[8 + i];
        sa1_to_host_point(ctx, in, out);
        om[i] = out[0];
        om[4 + i] = out[1];
        om[8 + i] = out[2];
    }
    b->node_v[2u * nn0] = nv0;
    b->node_v[2u * nn0 + 1u] = np;
    memcpy(b->node_wm + 12u * nn0, m, 12 * sizeof(double));
    b->node_model[nn0] = model;
    b->node_key[nn0] = key;
    b->node_fl[nn0] = (uint8_t)((nrms ? SA1_NODE_NORMALS : 0u) |
                                ((ei->extra_flags & SA1_DRAW_SHADOW) ? SA1_NODE_SHADOW : 0u) |
                                ((ei->extra_flags & SA1_DRAW_AFTERIMAGE) ? SA1_NODE_FADE : 0u));
    b->node_depth[nn0] = (int16_t)(ei->depth < -32768 ? -32768 : ei->depth > 32767 ? 32767 : ei->depth);
    b->node_tl[nn0] = ei->texlist;
    b->nn = nn0 + 1u;
    if (det < 0.0)
        b->mirrored++;
    return SA1_OK;
bad:
    b->nv = nv0;
    b->nt = nt0;
    b->nn = nn0;
    b->ndraw = nd0;
    b->order = ord0;
    b->sig = sig0;
    b->bad_models++;
    return st == SA1_ERR_OUT_OF_MEMORY ? st : SA1_ERR_BAD_FORMAT;
}

void sa1_meshbuf_geometry(const sa1_meshbuf *b, sa1_geometry *g)
{
    g->num_vertices = b->nv;
    g->positions = b->pos;
    g->normals = b->nrm;
    g->num_triangles = b->nt;
    g->indices = b->idx;
    g->uvs = b->uv;
    g->colors = b->col;
    g->textures = b->tex;
    g->materials = b->mat;
    g->tri_flags = b->fl;
    g->num_nodes = b->nn;
    g->node_matrices = b->node_mtx;
}

sa1_status sa1_meshbuf_draws_of_geometry(const sa1_context *ctx, sa1_meshbuf *b, const sa1_geometry *g,
                                         const sa1_charset *cs)
{
    return sa1_meshbuf_draws_of_geometry_ex(ctx, b, g, cs, 0);
}

sa1_status sa1_meshbuf_draws_of_geometry_ex(const sa1_context *ctx, sa1_meshbuf *b, const sa1_geometry *g,
                                            const sa1_charset *cs, int32_t archive)
{
    uint32_t t = 0;
    if (archive < 0 || !cs || (uint32_t)archive >= cs->num_archives)
        archive = 0;
    b->ndraw = 0;
    b->nspr = 0;
    b->nlv = 0;
    b->order = 0;
    while (t < g->num_triangles) {
        uint32_t e = t + 1u, k, min_a = 255u, ta, cls;
        int16_t tex = g->textures[t];
        uint32_t attr = g->materials[t];
        uint8_t bs, bd;
        sa1_status st;
        while (e < g->num_triangles && g->textures[e] == tex && g->materials[e] == attr)
            e++;
        for (k = 12u * t; k < 12u * e; k += 4u)
            if (g->colors[k + 3u] < min_a)
                min_a = g->colors[k + 3u];
        ta = (cs && tex >= 0) ? sa1_charset_texture_alpha(ctx, cs, archive, tex) : SA1_TEXALPHA_NONE;
        cls = sa1_draw_classify(attr, 1, ta, min_a, &bs, &bd);
        st = add_tri_draw(ctx, b, t, e - t, tex >= 0 ? archive : -1, tex, attr, cls, bs, bd, (uint8_t)SA1_SOURCE_POSE,
                          0xFFFFFFFFu);
        if (st != SA1_OK)
            return st;
        t = e;
    }
    return SA1_OK;
}
/* ------------------------------------------------------------------------- */
/* Render interpolation of drawn models (sa1_capture.h sa1_xlerp)            */
/* ------------------------------------------------------------------------- */

#define XL_PI 3.14159265358979323846

static double det3(const double a[9])
{
    return a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) + a[2] * (a[3] * a[7] - a[4] * a[6]);
}

/* r = a * b (3x3 row-major) */
static void mul3(const double a[9], const double b[9], double r[9])
{
    double t[9];
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            t[3 * i + j] = a[3 * i] * b[j] + a[3 * i + 1] * b[3 + j] + a[3 * i + 2] * b[6 + j];
    memcpy(r, t, sizeof t);
}

/* r = a^T * b */
static void mul3_tn(const double a[9], const double b[9], double r[9])
{
    double t[9];
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            t[3 * i + j] = a[i] * b[j] + a[3 + i] * b[3 + j] + a[6 + i] * b[6 + j];
    memcpy(r, t, sizeof t);
}

/* r = a * b^T */
static void mul3_nt(const double a[9], const double b[9], double r[9])
{
    double t[9];
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            t[3 * i + j] = a[3 * i] * b[3 * j] + a[3 * i + 1] * b[3 * j + 1] + a[3 * i + 2] * b[3 * j + 2];
    memcpy(r, t, sizeof t);
}

static double frob_diff(const double a[9], const double b[9])
{
    double s = 0.0;
    int i;
    for (i = 0; i < 9; i++)
        s += (a[i] - b[i]) * (a[i] - b[i]);
    return sqrt(s);
}

/* The rotation of the polar decomposition of a (det > 0) by the scaled Newton iteration X <- (g X + X^-T / g) / 2;
 * 0 if a is (nearly) singular or the iteration does not settle on a rotation. */
static int polar3(const double a[9], double q[9])
{
    double x[9], qtq[9], n = 0.0;
    int it, i;
    memcpy(x, a, sizeof x);
    for (it = 0; it < 64; it++) {
        double c[9], det, nx = 0.0, nc = 0.0, g, d = 0.0;
        c[0] = x[4] * x[8] - x[5] * x[7];
        c[1] = x[5] * x[6] - x[3] * x[8];
        c[2] = x[3] * x[7] - x[4] * x[6];
        c[3] = x[2] * x[7] - x[1] * x[8];
        c[4] = x[0] * x[8] - x[2] * x[6];
        c[5] = x[1] * x[6] - x[0] * x[7];
        c[6] = x[1] * x[5] - x[2] * x[4];
        c[7] = x[2] * x[3] - x[0] * x[5];
        c[8] = x[0] * x[4] - x[1] * x[3];
        det = x[0] * c[0] + x[1] * c[1] + x[2] * c[2];
        if (!(det > 1e-300) || det != det)
            return 0;
        for (i = 0; i < 9; i++) {
            nx += x[i] * x[i];
            nc += c[i] * c[i];
        }
        g = sqrt(sqrt(nc) / det / sqrt(nx));   /* (|X^-1|_F / |X|_F)^(1/2) */
        if (!(g > 0.0) || g != g)
            return 0;
        for (i = 0; i < 9; i++) {
            double y = 0.5 * (g * x[i] + c[i] / (g * det));
            d = fmax(d, fabs(y - x[i]));
            x[i] = y;
        }
        if (d < 1e-15)
            break;
    }
    mul3_tn(x, x, qtq);
    for (i = 0; i < 9; i++)
        n = fmax(n, fabs(qtq[i] - ((i % 4) == 0 ? 1.0 : 0.0)));
    if (!(n < 1e-9))
        return 0;
    memcpy(q, x, sizeof x);
    return 1;
}

/* unit quaternion (w, x, y, z) of a rotation matrix (row-major, column vectors) */
static void quat_of(const double r[9], double q[4])
{
    double tr = r[0] + r[4] + r[8], s, l;
    if (tr > 0.0) {
        s = sqrt(tr + 1.0) * 2.0;
        q[0] = 0.25 * s;
        q[1] = (r[7] - r[5]) / s;
        q[2] = (r[2] - r[6]) / s;
        q[3] = (r[3] - r[1]) / s;
    } else if (r[0] > r[4] && r[0] > r[8]) {
        s = sqrt(1.0 + r[0] - r[4] - r[8]) * 2.0;
        q[0] = (r[7] - r[5]) / s;
        q[1] = 0.25 * s;
        q[2] = (r[1] + r[3]) / s;
        q[3] = (r[2] + r[6]) / s;
    } else if (r[4] > r[8]) {
        s = sqrt(1.0 + r[4] - r[0] - r[8]) * 2.0;
        q[0] = (r[2] - r[6]) / s;
        q[1] = (r[1] + r[3]) / s;
        q[2] = 0.25 * s;
        q[3] = (r[5] + r[7]) / s;
    } else {
        s = sqrt(1.0 + r[8] - r[0] - r[4]) * 2.0;
        q[0] = (r[3] - r[1]) / s;
        q[1] = (r[2] + r[6]) / s;
        q[2] = (r[5] + r[7]) / s;
        q[3] = 0.25 * s;
    }
    l = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    q[0] /= l;
    q[1] /= l;
    q[2] /= l;
    q[3] /= l;
}

static void mat_of_quat(const double q[4], double r[9])
{
    double w = q[0], x = q[1], y = q[2], z = q[3];
    r[0] = 1.0 - 2.0 * (y * y + z * z);
    r[1] = 2.0 * (x * y - w * z);
    r[2] = 2.0 * (x * z + w * y);
    r[3] = 2.0 * (x * y + w * z);
    r[4] = 1.0 - 2.0 * (x * x + z * z);
    r[5] = 2.0 * (y * z - w * x);
    r[6] = 2.0 * (x * z - w * y);
    r[7] = 2.0 * (y * z + w * x);
    r[8] = 1.0 - 2.0 * (x * x + y * y);
}

/* r = a * b (Hamilton product) */
static void quat_mul(const double a[4], const double b[4], double r[4])
{
    double t[4];
    t[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    t[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
    t[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
    t[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
    memcpy(r, t, sizeof t);
}

int32_t sa1_xlerp_plan(const double m0[12], const double m1[12], double turn, double max_angle, sa1_xlerp *x)
{
    double a0[9], a1[9], r0[9], r1[9], pl0[9], pl1[9], pr0[9], pr1[9], q1[4], c0[4], dq[4], d0, d1, vn, s;
    int i, j;
    memset(x, 0, sizeof *x);
    x->kind = SA1_XLERP_NONE;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            a0[3 * i + j] = m0[4 * i + j];
            a1[3 * i + j] = m1[4 * i + j];
        }
        x->t0[i] = m0[4 * i + 3];
        x->t1[i] = m1[4 * i + 3];
    }
    for (i = 0; i < 3; i++)
        if (x->t0[i] != x->t0[i] || x->t1[i] != x->t1[i])
            return SA1_XLERP_NONE;
    d0 = det3(a0);
    d1 = det3(a1);
    if (!(fabs(d0) > 1e-12) || !(fabs(d1) > 1e-12) || (d0 < 0.0) != (d1 < 0.0))
        return SA1_XLERP_NONE;
    s = d0 < 0.0 ? -1.0 : 1.0;
    memcpy(x->a1, a1, sizeof x->a1);
    for (i = 0; i < 9; i++) {
        a0[i] *= s;
        a1[i] *= s;
    }
    if (!polar3(a0, r0) || !polar3(a1, r1))
        return SA1_XLERP_NONE;
    /* A = P Q (left: the stretch in the parent's frame) or A = Q P (right: in the model's) */
    mul3_nt(a0, r0, pl0);
    mul3_nt(a1, r1, pl1);
    mul3_tn(r0, a0, pr0);
    mul3_tn(r1, a1, pr1);
    x->left = frob_diff(pl0, pl1) <= frob_diff(pr0, pr1);
    memcpy(x->p0, x->left ? pl0 : pr0, sizeof x->p0);
    memcpy(x->p1, x->left ? pl1 : pr1, sizeof x->p1);
    x->sign = s;
    quat_of(r0, x->q0);
    quat_of(r1, q1);
    c0[0] = x->q0[0];
    c0[1] = -x->q0[1];
    c0[2] = -x->q0[2];
    c0[3] = -x->q0[3];
    quat_mul(q1, c0, dq);                       /* Q1 = dQ Q0 */
    if (dq[0] < 0.0)
        for (i = 0; i < 4; i++)
            dq[i] = -dq[i];
    vn = sqrt(dq[1] * dq[1] + dq[2] * dq[2] + dq[3] * dq[3]);
    x->theta = 2.0 * atan2(vn, dq[0]);
    if (vn > 1e-12) {
        x->axis[0] = dq[1] / vn;
        x->axis[1] = dq[2] / vn;
        x->axis[2] = dq[3] / vn;
    } else {
        x->axis[0] = 1.0;
    }
    x->phi = x->theta;
    if (turn >= 0.0 && turn == turn) {
        /* the rotations about axis that end at Q1: theta + 2 pi k, or the other way round theta - 2 pi k; the one
           whose size is nearest the motion's turn */
        double ka = floor((turn - x->theta) / (2.0 * XL_PI) + 0.5), kb = floor((turn + x->theta) / (2.0 * XL_PI) + 0.5);
        double pa, pb;
        if (ka < 0.0)
            ka = 0.0;
        if (kb < 1.0)
            kb = 1.0;
        if (ka > 64.0)
            ka = 64.0;
        if (kb > 64.0)
            kb = 64.0;
        pa = x->theta + 2.0 * XL_PI * ka;
        pb = x->theta - 2.0 * XL_PI * kb;
        x->phi = fabs(fabs(pb) - turn) < fabs(fabs(pa) - turn) ? pb : pa;
        /* more than a whole turn per pair cannot be shown (the Dreamcast's 30 Hz display showed the turn modulo a
           whole one: the rolling ball after a spin dash turns almost twice per pair): as without the hint */
        if (fabs(x->phi) > 2.0 * XL_PI + 1e-9) {
            x->phi = x->theta;
            turn = -1.0;
        } else {
            max_angle = 2.0 * XL_PI;    /* the hint decided: never SNAP */
        }
    }
    if (turn < 0.0 && x->theta > max_angle) {
        x->kind = SA1_XLERP_SNAP;
        return x->kind;
    }
    x->kind = SA1_XLERP_SLERP;
    return x->kind;
}

void sa1_xlerp_at(const sa1_xlerp *x, double alpha, double m[12])
{
    double a[9], t[3];
    int i, j;
    if (x->kind == SA1_XLERP_SNAP) {
        memcpy(a, x->a1, sizeof a);
    } else {
        double h = 0.5 * alpha * x->phi, sh = sin(h), dq[4], q[4], r[9], p[9];
        dq[0] = cos(h);
        dq[1] = x->axis[0] * sh;
        dq[2] = x->axis[1] * sh;
        dq[3] = x->axis[2] * sh;
        quat_mul(dq, x->q0, q);
        mat_of_quat(q, r);
        for (i = 0; i < 9; i++)
            p[i] = x->p0[i] + (x->p1[i] - x->p0[i]) * alpha;
        if (x->left)
            mul3(p, r, a);
        else
            mul3(r, p, a);
        for (i = 0; i < 9; i++)
            a[i] *= x->sign;
    }
    for (i = 0; i < 3; i++)
        t[i] = x->t0[i] + (x->t1[i] - x->t0[i]) * alpha;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++)
            m[4 * i + j] = a[3 * i + j];
        m[4 * i + 3] = t[i];
    }
}

int32_t sa1_pose_node_of_model(const sa1_charset *cs, int32_t object, uint32_t model)
{
    const sa1_object *o;
    uint32_t i;
    if (!cs || object < 0 || (uint32_t)object >= cs->objects.count || model == 0)
        return -1;
    o = &SA1_VEC(cs->objects, sa1_object)[object];
    for (i = 0; i < o->num_nodes; i++) {
        int32_t mi = o->nodes[i].model;
        if (mi >= 0 && (uint32_t)mi < cs->models.count &&
            (SA1_VEC(cs->models, sa1_model)[mi].addr & 0x1FFFFFFFu) == (model & 0x1FFFFFFFu))
            return (int32_t)i;
    }
    return -1;
}

/* sample_ang for the motion hint: the same except that the loop closure (the last key towards the first at nbFrame)
 * goes the shorter way round. The game's evaluator 8C03FF90 interpolates it as plain integers (Sonic's spin ball:
 * 61887 -> 0 sweeps back 340 degrees between frames 6 and 7), which no drawn pair of frames is meant to show. */
static void sample_ang_loop(const sa1_keya *k, uint32_t n, float f, uint32_t nf, double out[3])
{
    uint32_t lo, j;
    const sa1_keya *a;
    float t;
    if (n == 1 || f <= (float)k[0].frame || n == 0) {
        sample_ang(k, n, f, nf, out);
        return;
    }
    KEY_SEARCH(k, n, f, lo);
    if (lo + 1u < n) {
        sample_ang(k, n, f, nf, out);
        return;
    }
    a = &k[lo];
    if (!key_pair(n, a->frame, k[0].frame, lo, f, nf, 1, &t)) {
        for (j = 0; j < 3; j++)
            out[j] = (double)a->v[j];
        return;
    }
    for (j = 0; j < 3; j++) {
        double d = (double)(int16_t)(uint16_t)(((uint32_t)k[0].v[j] - (uint32_t)a->v[j]) & 0xFFFFu);
        out[j] = (double)a->v[j] + d * (double)t;
    }
}

/* the rotation of node `node` relative to the object's root at frame f (the parents' and its own local rotations
 * with the motion's angle keys); 0 if the chain is too deep */
static int node_chain_rot(const sa1_object *o, const sa1_motion *mo, int32_t node, float f, double r[9])
{
    int32_t chain[64], n = 0, j = node, k;
    double acc[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    while (j >= 0 && (uint32_t)j < o->num_nodes) {
        if (n >= 64)
            return 0;
        chain[n++] = j;
        j = o->nodes[j].parent;
    }
    for (k = n - 1; k >= 0; k--) {
        const sa1_node *nd = &o->nodes[chain[k]];
        uint32_t fl = nd->flags | SA1_EVAL_UNIT_POS | SA1_EVAL_UNIT_SCL;
        double ang[3], lr[9];
        float pos[3] = { 0.0f, 0.0f, 0.0f }, scl[3] = { 1.0f, 1.0f, 1.0f }, lm[12];
        int i;
        ang[0] = nd->ang[0];
        ang[1] = nd->ang[1];
        ang[2] = nd->ang[2];
        if ((uint32_t)chain[k] < mo->num_nodes && mo->nodes[chain[k]].num_ang) {
            sample_ang_loop(mo->nodes[chain[k]].ang, mo->nodes[chain[k]].num_ang, f, mo->num_frames, ang);
            fl &= ~SA1_EVAL_UNIT_ANG;
        }
        if (fl & SA1_EVAL_UNIT_ANG)
            continue;
        mtx_local(fl, pos, ang, scl, lm);
        for (i = 0; i < 3; i++) {
            lr[3 * i] = lm[4 * i];
            lr[3 * i + 1] = lm[4 * i + 1];
            lr[3 * i + 2] = lm[4 * i + 2];
        }
        mul3(acc, lr, acc);
    }
    memcpy(r, acc, sizeof acc);
    return 1;
}

double sa1_pose_node_turn(const sa1_charset *cs, int32_t object, int32_t motion, int32_t node, double f0, double df)
{
    const sa1_object *o;
    const sa1_motion *mo;
    double r0[9], r1[9], rel[9], q[4] = { 1.0, 0.0, 0.0, 0.0 }, nf, vn;
    uint32_t n, s;
    if (!cs || object < 0 || (uint32_t)object >= cs->objects.count || motion < 0 ||
        (uint32_t)motion >= cs->motions.count || df != df || f0 != f0)
        return -1.0;
    o = &SA1_VEC(cs->objects, sa1_object)[object];
    mo = &SA1_VEC(cs->motions, sa1_motion)[motion];
    if (node < 0 || (uint32_t)node >= o->num_nodes || mo->num_frames == 0)
        return -1.0;
    nf = (double)mo->num_frames;
    n = (uint32_t)ceil(fabs(df) * 4.0);
    if (n < 1u)
        n = 1u;
    if (n > 512u)
        n = 512u;
    /* the rotation composed step by step as a continuous quaternion (each step's short way): its angle is the net turn
       the motion makes, up to a whole one, and beyond half a turn when the motion goes the long way round (a path
       that wobbles between axes is not counted as turning) */
    for (s = 0; s <= n; s++) {
        double f = fmod(f0 + df * (double)s / (double)n, nf), dq[4];
        if (f < 0.0)
            f += nf;
        if (!node_chain_rot(o, mo, node, (float)f, s ? r1 : r0))
            return -1.0;
        if (!s)
            continue;
        mul3_tn(r0, r1, rel);
        quat_of(rel, dq);
        if (dq[0] < 0.0) {
            dq[0] = -dq[0];
            dq[1] = -dq[1];
            dq[2] = -dq[2];
            dq[3] = -dq[3];
        }
        quat_mul(q, dq, q);
        memcpy(r0, r1, sizeof r0);
    }
    vn = sqrt(q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    return 2.0 * atan2(vn, q[0]);
}
