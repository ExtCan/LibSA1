/*
 * Ninja basic-model and motion deserializers.
 *
 * Converts the 32-bit Dreamcast layouts into native structures (sa1_internal.h).
 * On-disc layouts (little-endian, 32-bit pointers):
 *
 *   NJS_OBJECT   0x34  00 u32 eval flags; 04 NJS_MODEL*; 08 f32 pos[3];
 *                      14 s32 ang[3] (BAMS); 20 f32 scl[3]; 2C child; 30 sibling
 *   NJS_MODEL    0x28  00 points; 04 normals; 08 s32 nbPoint; 0C meshsets;
 *                      10 materials; 14 u16 nbMeshset; 16 u16 nbMat;
 *                      18 f32 center[3]; 24 f32 radius
 *   NJS_MESHSET  0x18  00 u16 type_matId (bits 14-15 type, 0-13 material);
 *                      02 u16 nbMesh; 04 polys; 08 attrs; 0C normals;
 *                      10 vertex colours; 14 UVs (s16 u,v per corner)
 *   NJS_MATERIAL 0x14  00 diffuse; 04 specular; 08 f32 exponent; 0C attr_texId;
 *                      10 attrflags
 *   NJS_MOTION   0x0C  00 mdata; 04 u32 nbFrame; 08 u16 type; 0A u16 inp_fn
 *     mdata entry      per node: void *key[f]; u32 nbKeys[f]  (f = inp_fn & 15)
 *     NJS_MKEY_F 0x10  u32 frame; f32 x, y, z         (POS, SCL)
 *     NJS_MKEY_A 0x10  u32 frame; s32 x, y, z (BAMS)  (ANG)
 *   PL_JOIN_VERTEX 0x18 00 base object; 04 src object; 08 dst object; 0C u8 count;
 *                      0D u8 type; 0E u8 srcdepth; 0F u8 dstdepth; 10 org; 14 u16 *pairs
 *
 * Every pointer is resolved through sa1_view_get (bounds-checked); counts are capped
 * (sa1_internal.h limits); vertex indices are checked against the model; floats must
 * be finite.  Polygon types: 0 triangles, 1 quads (strip order 0,1,2 + 2,1,3),
 * 2 N-gons and 3 strips (u16 header: count | 0x8000 = reversed winding; triangle j
 * uses corners j, j+1, j+2 and swaps the first two when j is odd XOR reversed;
 * degenerate triangles are dropped).
 */
#include "sa1_internal.h"

/* pointer + bytes available until the end of the containing image */
static const uint8_t *view_span(const sa1_view *v, uint32_t addr, uint32_t *avail, const sa1_image **which)
{
    const sa1_image *im = NULL;
    const uint8_t *p = sa1_view_get(v, addr, 1, &im);
    if (!p)
        return NULL;
    *avail = im->size - (uint32_t)(p - im->data);
    if (which)
        *which = im;
    return p;
}

static int read_f3(const uint8_t *p, float out[3])
{
    out[0] = sa1_rdf32(p);
    out[1] = sa1_rdf32(p + 4);
    out[2] = sa1_rdf32(p + 8);
    return sa1_finite(out[0]) && sa1_finite(out[1]) && sa1_finite(out[2]);
}

/* ------------------------------------------------------------------------- */
/* Models                                                                    */
/* ------------------------------------------------------------------------- */

static sa1_status load_meshset(sa1_charset *cs, const sa1_view *v, const uint8_t *m, uint32_t npts,
                               sa1_meshset *out)
{
    uint32_t tm = sa1_rd16(m), nmesh = sa1_rd16(m + 2);
    uint32_t polys = sa1_rd32(m + 4), vcol = sa1_rd32(m + 16), uvp = sa1_rd32(m + 20);
    uint32_t ptype = tm >> 14, avail = 0, pos = 0, ncorn = 0, maxtri = 0, i, k, t;
    const uint8_t *s;

    out->poly_type = (uint8_t)ptype;
    out->mat_id = (uint16_t)(tm & 0x3FFFu);
    out->num_polys = nmesh;
    if (nmesh == 0)
        return SA1_OK;
    s = view_span(v, polys, &avail, NULL);
    if (!s)
        return SA1_ERR_BAD_FORMAT;

    /* pass 1: size the corner stream */
    if (ptype < 2) {
        uint32_t per = ptype == 0 ? 3u : 4u;
        ncorn = nmesh * per;
        if ((uint64_t)ncorn * 2u > avail)
            return SA1_ERR_BAD_FORMAT;
        maxtri = ptype == 0 ? nmesh : nmesh * 2u;
    } else {
        for (i = 0; i < nmesh; i++) {
            uint32_t n;
            if (pos + 2u > avail)
                return SA1_ERR_BAD_FORMAT;
            n = sa1_rd16(s + pos) & 0x7FFFu;
            pos += 2;
            if ((uint64_t)pos + 2u * n > avail)
                return SA1_ERR_BAD_FORMAT;
            pos += 2u * n;
            ncorn += n;
            if (ncorn > SA1_MAX_CORNERS)
                return SA1_ERR_LIMIT;
            if (n >= 3)
                maxtri += n - 2u;
        }
    }
    if (ncorn > SA1_MAX_CORNERS)
        return SA1_ERR_LIMIT;
    out->num_corners = ncorn;
    out->corner_vtx = (uint16_t *)sa1_arena_array(&cs->arena, ncorn ? ncorn : 1u, sizeof(uint16_t));
    out->tris = (uint32_t *)sa1_arena_array(&cs->arena, maxtri ? 3u * (size_t)maxtri : 1u, sizeof(uint32_t));
    if (!out->corner_vtx || !out->tris)
        return SA1_ERR_OUT_OF_MEMORY;

    /* pass 2: corners and triangles */
    pos = 0;
    k = 0;
    t = 0;
    for (i = 0; i < nmesh; i++) {
        uint32_t n, j, rev = 0;
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
            if (vi >= npts)
                return SA1_ERR_BAD_FORMAT;
            out->corner_vtx[k + j] = (uint16_t)vi;
        }
        if (ptype == 0) {
            out->tris[3 * t] = k; out->tris[3 * t + 1] = k + 1; out->tris[3 * t + 2] = k + 2;
            t++;
        } else if (ptype == 1) {
            out->tris[3 * t] = k; out->tris[3 * t + 1] = k + 1; out->tris[3 * t + 2] = k + 2;
            t++;
            out->tris[3 * t] = k + 2; out->tris[3 * t + 1] = k + 1; out->tris[3 * t + 2] = k + 3;
            t++;
        } else {
            for (j = 0; j + 2 < n; j++) {
                uint32_t a = k + j, b = k + j + 1, d = k + j + 2;
                if ((j & 1u) ^ rev) {
                    uint32_t tmp = a; a = b; b = tmp;
                }
                if (out->corner_vtx[a] != out->corner_vtx[b] && out->corner_vtx[b] != out->corner_vtx[d] &&
                    out->corner_vtx[a] != out->corner_vtx[d]) {
                    out->tris[3 * t] = a; out->tris[3 * t + 1] = b; out->tris[3 * t + 2] = d;
                    t++;
                }
            }
        }
        k += n;
    }
    out->num_tris = t;

    if (uvp && ncorn) {
        const uint8_t *q = sa1_view_get(v, uvp, 4u * ncorn, NULL);
        if (!q)
            return SA1_ERR_BAD_FORMAT;
        out->uv = (int16_t *)sa1_arena_array(&cs->arena, 2u * (size_t)ncorn, sizeof(int16_t));
        if (!out->uv)
            return SA1_ERR_OUT_OF_MEMORY;
        for (i = 0; i < 2u * ncorn; i++)
            out->uv[i] = (int16_t)sa1_rd16(q + 2u * i);
    }
    if (vcol && ncorn) {
        const uint8_t *q = sa1_view_get(v, vcol, 4u * ncorn, NULL);
        if (!q)
            return SA1_ERR_BAD_FORMAT;
        out->vcolor = (uint32_t *)sa1_arena_array(&cs->arena, ncorn, sizeof(uint32_t));
        if (!out->vcolor)
            return SA1_ERR_OUT_OF_MEMORY;
        for (i = 0; i < ncorn; i++)
            out->vcolor[i] = sa1_rd32(q + 4u * i);
    }
    return SA1_OK;
}

static int32_t load_model(sa1_context *ctx, sa1_charset *cs, const sa1_view *v, uint32_t addr, sa1_status *st)
{
    const sa1_image *im = NULL;
    const uint8_t *p = sa1_view_get(v, addr, 0x28, &im), *q;
    uint32_t phys = addr & 0x1FFFFFFFu, i, npts, nms, nmat, ptsp, nrmp, msp, matp;
    int32_t np;
    sa1_model *m;
    sa1_model tmp;

    if (!p) {
        *st = SA1_ERR_BAD_FORMAT;
        return -1;
    }
    for (i = 0; i < cs->models.count; i++) {
        const sa1_model *e = &SA1_VEC(cs->models, sa1_model)[i];
        if (e->addr == phys && e->image == im->id)
            return (int32_t)i;
    }
    memset(&tmp, 0, sizeof tmp);
    tmp.addr = phys;
    tmp.image = im->id;
    ptsp = sa1_rd32(p);
    nrmp = sa1_rd32(p + 4);
    np = sa1_rds32(p + 8);
    msp = sa1_rd32(p + 12);
    matp = sa1_rd32(p + 16);
    nms = sa1_rd16(p + 20);
    nmat = sa1_rd16(p + 22);
    if (!read_f3(p + 24, tmp.center))
        tmp.center[0] = tmp.center[1] = tmp.center[2] = 0.0f;
    tmp.radius = sa1_rdf32(p + 36);
    if (!sa1_finite(tmp.radius))
        tmp.radius = 0.0f;
    if (np < 0 || (uint32_t)np > SA1_MAX_POINTS || nms > SA1_MAX_MESHSETS || nmat > SA1_MAX_MATERIALS) {
        *st = SA1_ERR_LIMIT;
        return -1;
    }
    npts = (uint32_t)np;
    tmp.num_points = npts;
    tmp.num_meshsets = nms;
    tmp.num_mats = nmat;
    *st = SA1_ERR_BAD_FORMAT;
    if (npts) {
        q = sa1_view_get(v, ptsp, 12u * npts, NULL);
        tmp.points = (float *)sa1_arena_array(&cs->arena, 3u * (size_t)npts, sizeof(float));
        if (!q || !tmp.points)
            goto fail;
        for (i = 0; i < npts; i++)
            if (!read_f3(q + 12u * i, tmp.points + 3u * i))
                goto fail;
        if (nrmp) {
            q = sa1_view_get(v, nrmp, 12u * npts, NULL);
            tmp.normals = (float *)sa1_arena_array(&cs->arena, 3u * (size_t)npts, sizeof(float));
            if (!q || !tmp.normals)
                goto fail;
            for (i = 0; i < npts; i++)
                if (!read_f3(q + 12u * i, tmp.normals + 3u * i))
                    goto fail;
        }
    }
    if (nmat) {
        q = sa1_view_get(v, matp, 20u * nmat, NULL);
        tmp.mats = (sa1_material *)sa1_arena_array(&cs->arena, nmat, sizeof(sa1_material));
        if (!q || !tmp.mats)
            goto fail;
        for (i = 0; i < nmat; i++) {
            const uint8_t *e = q + 20u * i;
            tmp.mats[i].diffuse = sa1_rd32(e);
            tmp.mats[i].specular = sa1_rd32(e + 4);
            tmp.mats[i].exponent = sa1_rdf32(e + 8);
            tmp.mats[i].tex_id = sa1_rd32(e + 12) & 0x3FFFFFFFu;
            tmp.mats[i].attr = sa1_rd32(e + 16);
        }
    }
    if (nms) {
        q = sa1_view_get(v, msp, 24u * nms, NULL);
        tmp.meshsets = (sa1_meshset *)sa1_arena_array(&cs->arena, nms, sizeof(sa1_meshset));
        if (!q || !tmp.meshsets)
            goto fail;
        for (i = 0; i < nms; i++) {
            sa1_status s2 = load_meshset(cs, v, q + 24u * i, npts, &tmp.meshsets[i]);
            if (s2 != SA1_OK) {
                *st = s2;
                goto fail;
            }
            tmp.num_tris += tmp.meshsets[i].num_tris;
        }
    }
    m = (sa1_model *)sa1_vec_push(ctx, &cs->models);
    if (!m) {
        *st = SA1_ERR_OUT_OF_MEMORY;
        return -1;
    }
    *m = tmp;
    *st = SA1_OK;
    return (int32_t)(cs->models.count - 1u);
fail:
    sa1_log(ctx, SA1_LOG_WARN, "invalid NJS_MODEL at 0x%08X (%s)", (unsigned)addr, sa1_status_string(*st));
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Object trees                                                              */
/* ------------------------------------------------------------------------- */

typedef struct pending {
    uint32_t addr;
    int32_t  parent, prev;
} pending;

int32_t sa1_ninja_object(sa1_context *ctx, sa1_charset *cs, const sa1_view *v, uint32_t addr, sa1_status *st)
{
    const sa1_image *rim = NULL;
    uint32_t phys = addr & 0x1FFFFFFFu, i, n = 0, sp = 0, vb = 0;
    sa1_node *nodes;
    pending stack[SA1_MAX_DEPTH], cur;
    sa1_object *o;

    if (!sa1_view_get(v, addr, 0x34, &rim)) {
        *st = SA1_ERR_BAD_FORMAT;
        return -1;
    }
    for (i = 0; i < cs->objects.count; i++) {
        const sa1_object *e = &SA1_VEC(cs->objects, sa1_object)[i];
        if (e->addr == phys && e->image == rim->id)
            return (int32_t)i;
    }
    nodes = (sa1_node *)sa1_mem_array(ctx, SA1_MAX_NODES, sizeof(sa1_node));
    if (!nodes) {
        *st = SA1_ERR_OUT_OF_MEMORY;
        return -1;
    }
    *st = SA1_ERR_BAD_FORMAT;
    cur.addr = addr;
    cur.parent = -1;
    cur.prev = -1;
    /* pre-order: node, its children, then its siblings (the NJS_MOTION node order) */
    for (;;) {
        if (cur.addr) {
            const uint8_t *p = sa1_view_get(v, cur.addr, 0x34, NULL);
            uint32_t cphys = cur.addr & 0x1FFFFFFFu, model;
            sa1_node *nd;
            if (!p)
                goto fail;
            if (n >= SA1_MAX_NODES) {
                *st = SA1_ERR_LIMIT;
                goto fail;
            }
            for (i = 0; i < n; i++)
                if (nodes[i].addr == cphys)
                    goto fail;                  /* cycle / shared node */
            nd = &nodes[n];
            memset(nd, 0, sizeof *nd);
            nd->addr = cphys;
            nd->flags = sa1_rd32(p);
            model = sa1_rd32(p + 4);
            if (!read_f3(p + 8, nd->pos) || !read_f3(p + 32, nd->scl))
                goto fail;
            nd->ang[0] = sa1_rds32(p + 20);
            nd->ang[1] = sa1_rds32(p + 24);
            nd->ang[2] = sa1_rds32(p + 28);
            nd->parent = cur.parent;
            nd->child = nd->sibling = -1;
            nd->model = -1;
            nd->vbase = 0xFFFFFFFFu;
            if (model) {
                nd->model = load_model(ctx, cs, v, model, st);
                if (nd->model < 0)
                    goto fail;
            }
            if (cur.prev >= 0)
                nodes[cur.prev].sibling = (int32_t)n;
            else if (cur.parent >= 0)
                nodes[cur.parent].child = (int32_t)n;
            if (sp >= SA1_MAX_DEPTH) {
                *st = SA1_ERR_LIMIT;
                goto fail;
            }
            stack[sp].addr = sa1_rd32(p + 48);  /* sibling continues after the child subtree */
            stack[sp].parent = cur.parent;
            stack[sp].prev = (int32_t)n;
            sp++;
            cur.addr = sa1_rd32(p + 44);
            cur.parent = (int32_t)n;
            cur.prev = -1;
            n++;
        } else {
            if (sp == 0)
                break;
            cur = stack[--sp];
        }
    }

    o = (sa1_object *)sa1_vec_push(ctx, &cs->objects);
    if (!o) {
        *st = SA1_ERR_OUT_OF_MEMORY;
        goto fail_nolog;
    }
    o->addr = phys;
    o->ptr = addr;
    o->image = rim->id;
    o->num_nodes = n;
    o->nodes = (sa1_node *)sa1_arena_array(&cs->arena, n, sizeof(sa1_node));
    if (!o->nodes) {
        cs->objects.count--;
        *st = SA1_ERR_OUT_OF_MEMORY;
        goto fail_nolog;
    }
    memcpy(o->nodes, nodes, (size_t)n * sizeof(sa1_node));
    sa1_mem_free(ctx, nodes);
    /* static draw traversal (HIDE/BREAK are never animated) and work-buffer layout */
    {
        uint8_t visited[SA1_MAX_NODES];
        for (i = 0; i < n; i++) {
            sa1_node *nd = &o->nodes[i];
            int32_t par = nd->parent;
            visited[i] = (uint8_t)(par < 0 ? 1 : (visited[par] && !(o->nodes[par].flags & SA1_EVAL_BREAK)));
            if (nd->model >= 0) {
                const sa1_model *m = &SA1_VEC(cs->models, sa1_model)[nd->model];
                nd->vbase = vb;
                vb += m->num_points;
                nd->drawn = (uint8_t)(visited[i] && !(nd->flags & SA1_EVAL_HIDE));
                if (nd->drawn) {
                    o->drawn_vertices += m->num_points;
                    o->drawn_tris += m->num_tris;
                }
            }
        }
        o->work_vertices = vb;
    }
    *st = SA1_OK;
    return (int32_t)(cs->objects.count - 1u);

fail:
    sa1_log(ctx, SA1_LOG_WARN, "invalid NJS_OBJECT tree at 0x%08X (%s)", (unsigned)addr, sa1_status_string(*st));
fail_nolog:
    sa1_mem_free(ctx, nodes);
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Motions                                                                   */
/* ------------------------------------------------------------------------- */

static sa1_status load_keys(sa1_charset *cs, const sa1_view *v, uint32_t ptr, uint32_t count, int angle,
                            void **out, uint32_t *out_n, uint32_t num_frames)
{
    const uint8_t *q;
    uint32_t i, prev = 0;
    (void)num_frames;
    *out = NULL;
    *out_n = 0;
    if (!ptr || !count)
        return SA1_OK;
    if (count > SA1_MAX_KEYS)
        return SA1_ERR_LIMIT;
    q = sa1_view_get(v, ptr, 16u * count, NULL);
    if (!q)
        return SA1_ERR_BAD_FORMAT;
    if (angle) {
        sa1_keya *k = (sa1_keya *)sa1_arena_array(&cs->arena, count, sizeof(sa1_keya));
        if (!k)
            return SA1_ERR_OUT_OF_MEMORY;
        for (i = 0; i < count; i++) {
            const uint8_t *e = q + 16u * i;
            k[i].frame = sa1_rd32(e);
            k[i].v[0] = sa1_rds32(e + 4);
            k[i].v[1] = sa1_rds32(e + 8);
            k[i].v[2] = sa1_rds32(e + 12);
            if (k[i].frame > SA1_MAX_FRAMES || (i && k[i].frame < prev))
                return SA1_ERR_BAD_FORMAT;
            prev = k[i].frame;
        }
        *out = k;
    } else {
        sa1_key3 *k = (sa1_key3 *)sa1_arena_array(&cs->arena, count, sizeof(sa1_key3));
        if (!k)
            return SA1_ERR_OUT_OF_MEMORY;
        for (i = 0; i < count; i++) {
            const uint8_t *e = q + 16u * i;
            k[i].frame = sa1_rd32(e);
            if (!read_f3(e + 4, k[i].v))
                return SA1_ERR_BAD_FORMAT;
            if (k[i].frame > SA1_MAX_FRAMES || (i && k[i].frame < prev))
                return SA1_ERR_BAD_FORMAT;
            prev = k[i].frame;
        }
        *out = k;
    }
    *out_n = count;
    return SA1_OK;
}

int32_t sa1_ninja_motion(sa1_context *ctx, sa1_charset *cs, const sa1_view *v, uint32_t addr, uint32_t num_nodes,
                         sa1_status *st)
{
    const sa1_image *im = NULL;
    const uint8_t *p = sa1_view_get(v, addr, 12, &im), *md;
    uint32_t phys = addr & 0x1FFFFFFFu, i, mdata, nfac, nbits = 0, bit, bits[16];
    sa1_motion tmp, *m;

    if (!p) {
        *st = SA1_ERR_BAD_FORMAT;
        return -1;
    }
    for (i = 0; i < cs->motions.count; i++) {
        const sa1_motion *e = &SA1_VEC(cs->motions, sa1_motion)[i];
        if (e->addr == phys && e->image == im->id && e->num_nodes == num_nodes)
            return (int32_t)i;
    }
    memset(&tmp, 0, sizeof tmp);
    tmp.addr = phys;
    tmp.image = im->id;
    mdata = sa1_rd32(p);
    tmp.num_frames = sa1_rd32(p + 4);
    tmp.type = sa1_rd16(p + 8);
    tmp.inp_fn = sa1_rd16(p + 10);
    tmp.num_nodes = num_nodes;
    nfac = tmp.inp_fn & 0xFu;
    *st = SA1_ERR_BAD_FORMAT;
    if (tmp.num_frames == 0 || tmp.num_frames > SA1_MAX_FRAMES || nfac == 0 || nfac > SA1_MAX_FACTORS ||
        num_nodes == 0 || num_nodes > SA1_MAX_NODES)
        goto fail;
    for (bit = 0; bit < 16; bit++)
        if (tmp.type & (1u << bit))
            bits[nbits++] = 1u << bit;
    md = sa1_view_get(v, mdata, 8u * nfac * num_nodes, NULL);
    tmp.nodes = (sa1_motion_node *)sa1_arena_array(&cs->arena, num_nodes, sizeof(sa1_motion_node));
    if (!md || !tmp.nodes) {
        if (!tmp.nodes)
            *st = SA1_ERR_OUT_OF_MEMORY;
        goto fail;
    }
    for (i = 0; i < num_nodes; i++) {
        const uint8_t *e = md + 8u * nfac * i;
        sa1_motion_node *mn = &tmp.nodes[i];
        uint32_t f;
        for (f = 0; f < nfac && f < nbits; f++) {
            uint32_t ptr = sa1_rd32(e + 4u * f), cnt = sa1_rd32(e + 4u * (nfac + f));
            sa1_status s2 = SA1_OK;
            void *keys = NULL;
            uint32_t got = 0;
            if (bits[f] == SA1_MTYPE_POS) {
                s2 = load_keys(cs, v, ptr, cnt, 0, &keys, &got, tmp.num_frames);
                mn->pos = (sa1_key3 *)keys;
                mn->num_pos = got;
            } else if (bits[f] == SA1_MTYPE_ANG) {
                s2 = load_keys(cs, v, ptr, cnt, 1, &keys, &got, tmp.num_frames);
                mn->ang = (sa1_keya *)keys;
                mn->num_ang = got;
            } else if (bits[f] == SA1_MTYPE_SCL) {
                s2 = load_keys(cs, v, ptr, cnt, 0, &keys, &got, tmp.num_frames);
                mn->scl = (sa1_key3 *)keys;
                mn->num_scl = got;
            }   /* other factors (shape, vectors, lights) are not used by player motions */
            if (s2 != SA1_OK) {
                *st = s2;
                goto fail;
            }
            tmp.num_keys += got;
        }
    }
    m = (sa1_motion *)sa1_vec_push(ctx, &cs->motions);
    if (!m) {
        *st = SA1_ERR_OUT_OF_MEMORY;
        return -1;
    }
    *m = tmp;
    *st = SA1_OK;
    return (int32_t)(cs->motions.count - 1u);
fail:
    sa1_log(ctx, SA1_LOG_WARN, "invalid NJS_MOTION at 0x%08X (%s)", (unsigned)addr, sa1_status_string(*st));
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Weld lists                                                                */
/* ------------------------------------------------------------------------- */

static int32_t node_by_addr(const sa1_object *o, uint32_t addr)
{
    uint32_t i, phys = addr & 0x1FFFFFFFu;
    if (!addr)
        return -1;
    for (i = 0; i < o->num_nodes; i++)
        if (o->nodes[i].addr == phys)
            return (int32_t)i;
    return -1;
}

sa1_status sa1_ninja_welds(sa1_context *ctx, sa1_charset *cs, const sa1_view *v, uint32_t table)
{
    uint32_t n = 0, i, kept = 0;
    const uint8_t *p;
    if (!table)
        return SA1_OK;
    /* count entries up to the NULL terminator */
    for (;;) {
        p = sa1_view_get(v, table + 24u * n, 24, NULL);
        if (!p)
            return SA1_ERR_BAD_FORMAT;
        if (sa1_rd32(p) == 0)
            break;
        if (++n > SA1_MAX_WELDS)
            return SA1_ERR_LIMIT;
    }
    cs->welds = (sa1_weld *)sa1_arena_array(&cs->arena, n ? n : 1u, sizeof(sa1_weld));
    if (!cs->welds)
        return SA1_ERR_OUT_OF_MEMORY;
    for (i = 0; i < n; i++) {
        uint32_t base, src, dst, pairs, k;
        sa1_weld w;
        sa1_status st;
        const sa1_object *o;
        p = sa1_view_get(v, table + 24u * i, 24, NULL);
        base = sa1_rd32(p);
        src = sa1_rd32(p + 4);
        dst = sa1_rd32(p + 8);
        memset(&w, 0, sizeof w);
        w.count = p[12];
        w.type = p[13];
        w.src_depth = p[14];
        w.dst_depth = p[15];
        pairs = sa1_rd32(p + 20);
        w.object = sa1_ninja_object(ctx, cs, v, base, &st);
        if (w.object < 0) {
            sa1_log(ctx, SA1_LOG_DEBUG, "weld %u: base object 0x%08X not available", (unsigned)i, (unsigned)base);
            continue;
        }
        o = &SA1_VEC(cs->objects, sa1_object)[w.object];
        w.src_node = node_by_addr(o, src);
        w.dst_node = node_by_addr(o, dst);
        if (w.src_node < 0)
            continue;                           /* e.g. an upgrade part that is not in the tree */
        if (w.type >= 1 && w.type <= 3) {
            const uint8_t *q;
            const sa1_model *ms, *md;
            if (w.dst_node < 0 || !pairs || !w.count)
                continue;
            if (o->nodes[w.src_node].model < 0 || o->nodes[w.dst_node].model < 0)
                continue;
            ms = &SA1_VEC(cs->models, sa1_model)[o->nodes[w.src_node].model];
            md = &SA1_VEC(cs->models, sa1_model)[o->nodes[w.dst_node].model];
            q = sa1_view_get(v, pairs, 4u * w.count, NULL);
            if (!q)
                return SA1_ERR_BAD_FORMAT;
            w.num_pairs = w.count;
            w.pairs = (uint16_t *)sa1_arena_array(&cs->arena, 2u * (size_t)w.count, sizeof(uint16_t));
            if (!w.pairs)
                return SA1_ERR_OUT_OF_MEMORY;
            for (k = 0; k < w.count; k++) {
                w.pairs[2 * k] = sa1_rd16(q + 4u * k);
                w.pairs[2 * k + 1] = sa1_rd16(q + 4u * k + 2);
                if (w.pairs[2 * k] >= ms->num_points || w.pairs[2 * k + 1] >= md->num_points)
                    return SA1_ERR_BAD_FORMAT;
            }
        } else if (w.type < 4 || w.type > 9) {
            continue;                           /* unknown weld type */
        }
        cs->welds[kept++] = w;
    }
    cs->num_welds = kept;
    return SA1_OK;
}