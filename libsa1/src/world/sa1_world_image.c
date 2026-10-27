/*
 * sa1_world_image.c - world images from the user's disc (sadc docs/libsa1_integration.md 2.5-2.7).
 *
 * A pristine image is composed once per character kind: 1ST_READ.BIN at 0x8C010000 (its SHA-1 must be
 * US v1.005's), BSS 0x8C67C680-0x8C900000 zero, and the kind's overlays at their load addresses,
 * PRS-unpacked, with one occupant per shared overlay slot (B_CHAOS7 / STGxx / MINICART share
 * 0x0C900000, TIKAL_PROG overlaps S_SBMOT). Game data is used in place: no conversion, only a short
 * explicit patch list (2.7), applied and logged per world. Nothing here holds game bytes: every byte
 * comes from the disc layer (sa1_disc.c, sa1_prs.c) and is checked against the manifest
 * (sa1_manifest.c) at run time.
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_coladapt.h"

#include <stdio.h>
#include <string.h>

/* kind -> overlays of its world (2.5). Optional ones are dropped by SA1G_IMAGE_NO_OPTIONAL. */
typedef struct sa1g_kind_image {
    int32_t kind;
    const char *files[3];
    uint8_t optional[3];
} sa1g_kind_image;

static const sa1g_kind_image k_kind_images[] = {
    { SA1_CHAR_SONIC, { "S_MOT.PRS", "S_SBMOT.PRS", "B_CHAOS7.PRS" }, { 0, 1, 1 } },
    { SA1_CHAR_SUPER_SONIC, { "S_MOT.PRS", "S_SBMOT.PRS", "B_CHAOS7.PRS" }, { 0, 1, 0 } },  /* 6.3a */
    { SA1_CHAR_TAILS, { "M_SBMOT.PRS", NULL, NULL }, { 1, 0, 0 } },    /* Tails' motions are in 1ST_READ */
    { SA1_CHAR_KNUCKLES, { "K_MOT.PRS", NULL, NULL }, { 0, 0, 0 } },
    { SA1_CHAR_AMY, { "A_MOT.PRS", NULL, NULL }, { 0, 0, 0 } },
    { SA1_CHAR_GAMMA, { "E_MOT.PRS", NULL, NULL }, { 0, 0, 0 } },
    { SA1_CHAR_BIG, { "B_MOT.PRS", NULL, NULL }, { 0, 0, 0 } },
    { SA1_CHAR_TIKAL, { "TIKAL_PROG.PRS", NULL, NULL }, { 0, 0, 0 } },        /* no S_SBMOT (overlap) */
    { SA1_CHAR_EGGMAN, { NULL, NULL, NULL }, { 0, 0, 0 } },
};

static const sa1g_kind_image *sa1g_kind_image_of(int32_t kind)
{
    size_t i;
    for (i = 0; i < sizeof k_kind_images / sizeof k_kind_images[0]; i++) {
        if (k_kind_images[i].kind == kind) {
            return &k_kind_images[i];
        }
    }
    return NULL;
}

static int sa1g_ends_with_prs(const char *s)
{
    size_t n = strlen(s);
    return n >= 4 && strcmp(s + n - 4, ".PRS") == 0;
}

/* ---- spans ---------------------------------------------------------------------------------------- */
const sa1g_span *sa1g_image_span_at(const sa1g_image *img, uint32_t guest, uint32_t len)
{
    uint32_t i, phys = guest & 0x1FFFFFFFu;
    if (guest == 0) {
        return NULL;
    }
    for (i = 0; i < img->num_spans; i++) {
        const sa1g_span *s = &img->spans[i];
        uint32_t sp = s->guest & 0x1FFFFFFFu;
        if (phys >= sp && phys - sp <= s->size && len <= s->size - (phys - sp)) {
            return s;
        }
    }
    return NULL;
}

static int sa1g_image_rd32(const sa1g_image *img, uint32_t guest, uint32_t *v)
{
    const sa1g_span *s = sa1g_image_span_at(img, guest, 4);
    if (s == NULL) {
        *v = 0;
        return -1;
    }
    *v = sa1_rd32(s->data + ((guest & 0x1FFFFFFFu) - (s->guest & 0x1FFFFFFFu)));
    return 0;
}

/* takes ownership of `data` (sa1_mem_alloc'd) on success and on failure; sha1: of the file it came from */
static sa1_status sa1g_image_add_owned(sa1g_image *img, const char *name, uint32_t guest, uint8_t *data, size_t n,
                                       const char *sha1)
{
    uint32_t phys = guest & 0x1FFFFFFFu, off = guest & 0xFFFFFFu, i;
    int first = guest == SA1G_FIRST_READ_ADDR;
    sa1g_span *s;
    if ((phys >> 24) != SA1G_SEG_ARENA || n > SA1G_ARENA_SIZE - off || n == 0) {
        sa1_mem_free(img->ctx, data);
        return sa1_fail(img->ctx, SA1_ERR_BAD_FORMAT, "world image: %s at %08X (%u bytes) does not fit DC RAM",
                        name, (unsigned)guest, (unsigned)n);
    }
    if (!first && off < (SA1G_BSS_END & 0xFFFFFFu)) {
        sa1_mem_free(img->ctx, data);
        return sa1_fail(img->ctx, SA1_ERR_UNSUPPORTED, "world image: overlay %s at %08X overlaps 1ST_READ/BSS",
                        name, (unsigned)guest);
    }
    for (i = 0; i < img->num_spans; i++) {
        uint32_t a0 = img->spans[i].guest & 0x1FFFFFFFu, a1 = a0 + img->spans[i].size;
        if (phys < a1 && a0 < phys + (uint32_t)n) {
            sa1_mem_free(img->ctx, data);
            return sa1_fail(img->ctx, SA1_ERR_UNSUPPORTED,
                            "world image: %s (%08X-%08X) overlaps %s (%08X-%08X): one occupant per slot", name,
                            (unsigned)guest, (unsigned)(guest + (uint32_t)n), img->spans[i].name,
                            (unsigned)img->spans[i].guest, (unsigned)(img->spans[i].guest + img->spans[i].size));
        }
    }
    if (img->num_spans >= SA1G_IMAGE_MAX_SPANS) {
        sa1_mem_free(img->ctx, data);
        return sa1_fail(img->ctx, SA1_ERR_LIMIT, "world image: too many spans");
    }
    s = &img->spans[img->num_spans++];
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", name);
    snprintf(s->sha1, sizeof s->sha1, "%s", sha1 ? sha1 : "");
    s->guest = guest;
    s->size = (uint32_t)n;
    s->data = data;
    s->owned = data;
    img->finalized = 0;
    return SA1_OK;
}

sa1_status sa1g_image_add_span(sa1g_image *img, const char *name, uint32_t guest, const void *data, size_t n)
{
    uint8_t *copy;
    char hex[41];
    if (img == NULL || img->ctx == NULL || name == NULL || data == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    copy = (uint8_t *)sa1_mem_alloc(img->ctx, n ? n : 1u);
    if (copy == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    memcpy(copy, data, n);
    sa1_sha1_hex(copy, n, hex);         /* of the bytes given (unpacked): no overlay code map matches it */
    return sa1g_image_add_owned(img, name, guest, copy, n, hex);
}

void sa1g_image_free(sa1g_image *img)
{
    uint32_t i;
    if (img == NULL || img->ctx == NULL) {
        return;
    }
    for (i = 0; i < img->num_spans; i++) {
        sa1_mem_free(img->ctx, img->spans[i].owned);
    }
    sa1_mem_free(img->ctx, img->patches);
    memset(img, 0, sizeof *img);
}

static void sa1g_image_init(sa1g_image *img, sa1_context *ctx, int32_t kind, uint32_t flags)
{
    memset(img, 0, sizeof *img);
    img->ctx = ctx;
    img->kind = kind;
    img->flags = flags;
}

static sa1_status sa1g_check_first_read(sa1_context *ctx, const uint8_t *data, size_t n)
{
    char hex[41];
    sa1_sha1_hex(data, n, hex);
    if (n != SA1G_FIRST_READ_SIZE || strcmp(hex, SA1_FIRST_READ_SHA1) != 0) {
        return sa1_fail(ctx, SA1_ERR_BAD_HASH, "world image: 1ST_READ.BIN SHA-1 %s (%u bytes) is not US v1.005 (%s)",
                        hex, (unsigned)n, SA1_FIRST_READ_SHA1);
    }
    return SA1_OK;
}

sa1_status sa1g_image_from_memory(sa1_context *ctx, int32_t kind, const void *first_read, size_t n, sa1g_image *img)
{
    sa1_status st;
    if (ctx == NULL || first_read == NULL || img == NULL || kind < SA1G_KIND_BARE || kind >= SA1_CHAR_COUNT) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    sa1g_image_init(img, ctx, kind, 0);
    st = sa1g_check_first_read(ctx, (const uint8_t *)first_read, n);
    if (st == SA1_OK) {
        st = sa1g_image_add_span(img, SA1_FIRST_READ_NAME, SA1G_FIRST_READ_ADDR, first_read, n);
    }
    if (st != SA1_OK) {
        sa1g_image_free(img);
    }
    return st;
}

/* one file from the disc: SHA-1 against the manifest (sha1: of the file as read), PRS-unpacked */
static sa1_status sa1g_read_disc_file(sa1_context *ctx, sa1_disc *disc, const char *bare, uint32_t flags,
                                      uint8_t **out, uint32_t *size, char sha1[41])
{
    const sa1_file_desc *fd = sa1_manifest_file(bare);
    int root = strcmp(bare, SA1_FIRST_READ_NAME) == 0;
    char iso[64], hex[41];
    uint8_t *raw = NULL;
    uint32_t n = 0;
    sa1_status st;
    *out = NULL;
    *size = 0;
    snprintf(iso, sizeof iso, "%s%s", root ? "" : "SONICADV/", bare);
    st = sa1_disc_read_file(disc, iso, &raw, &n);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "world image: cannot read %s (%s)", iso, sa1_status_string(st));
    }
    sa1_sha1_hex(raw, n, hex);
    memcpy(sha1, hex, sizeof hex);
    if (!(fd && fd->size == n && strcmp(fd->sha1, hex) == 0)) {
        if (root || !(flags & SA1G_IMAGE_ACCEPT_MODIFIED)) {
            sa1_mem_free(ctx, raw);
            return sa1_fail(ctx, SA1_ERR_BAD_HASH, "world image: %s SHA-1 %s does not match US v1.005", iso, hex);
        }
        sa1_log(ctx, SA1_LOG_WARN, "world image: %s has an unknown SHA-1 %s (accepted)", iso, hex);
    }
    if (!sa1g_ends_with_prs(bare)) {
        *out = raw;
        *size = n;
        return SA1_OK;
    }
    st = sa1_prs_decompress(ctx, raw, n, SA1_MAX_UNPACKED_SIZE, out, size);
    sa1_mem_free(ctx, raw);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "world image: %s: PRS decompression failed", iso);
    }
    return SA1_OK;
}

sa1_status sa1g_image_compose(sa1_context *ctx, const char *gdi_path, int32_t kind, uint32_t flags, sa1g_image *img)
{
    const sa1g_kind_image *ki = kind >= 0 ? sa1g_kind_image_of(kind) : NULL;
    sa1_disc *disc;
    sa1_status st;
    uint8_t *data;
    uint32_t size, k;
    char sha1[41];
    if (ctx == NULL || gdi_path == NULL || img == NULL || kind < SA1G_KIND_BARE || (kind >= 0 && ki == NULL)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    sa1g_image_init(img, ctx, kind, flags);
    disc = (sa1_disc *)sa1_mem_alloc(ctx, sizeof *disc);
    if (disc == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    st = sa1_disc_open(ctx, gdi_path, disc);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, disc);
        return st;
    }
    st = sa1g_read_disc_file(ctx, disc, SA1_FIRST_READ_NAME, flags, &data, &size, sha1);
    if (st == SA1_OK) {
        st = sa1g_check_first_read(ctx, data, size);
        if (st == SA1_OK) {
            st = sa1g_image_add_owned(img, SA1_FIRST_READ_NAME, SA1G_FIRST_READ_ADDR, data, size, sha1);
        } else {
            sa1_mem_free(ctx, data);
        }
    }
    for (k = 0; st == SA1_OK && ki != NULL && k < 3; k++) {
        const sa1_file_desc *fd;
        if (ki->files[k] == NULL || (ki->optional[k] && (flags & SA1G_IMAGE_NO_OPTIONAL))) {
            continue;
        }
        fd = sa1_manifest_file(ki->files[k]);
        if (fd == NULL || fd->load_addr == 0) {
            st = sa1_fail(ctx, SA1_ERR_NOT_FOUND, "world image: %s is not a memory image", ki->files[k]);
            break;
        }
        st = sa1g_read_disc_file(ctx, disc, ki->files[k], flags, &data, &size, sha1);
        if (st == SA1_OK) {
            st = sa1g_image_add_owned(img, ki->files[k], fd->load_addr, data, size, sha1);
        }
    }
    sa1_disc_close(disc);
    sa1_mem_free(ctx, disc);
    if (st == SA1_OK) {
        st = sa1g_image_finalize(img);
    }
    if (st != SA1_OK) {
        sa1g_image_free(img);
        return st;
    }
    sa1_log(ctx, SA1_LOG_INFO, "world image %d: %u spans, %u patches", (int)kind, (unsigned)img->num_spans,
            (unsigned)img->num_patches);
    return SA1_OK;
}

/* ---- the patch list (2.7) ------------------------------------------------------------------------- */
static sa1_status sa1g_patch_add(sa1g_image *img, int32_t kind, uint32_t guest, uint32_t value, uint32_t arg,
                                 const char *why)
{
    sa1g_patch *p;
    if (img->num_patches == img->cap_patches) {
        uint32_t cap = img->cap_patches ? img->cap_patches * 2u : 64u;
        sa1g_patch *np = (sa1g_patch *)sa1_mem_array(img->ctx, cap, sizeof(sa1g_patch));
        if (np == NULL) {
            return SA1_ERR_OUT_OF_MEMORY;
        }
        if (img->patches) {
            memcpy(np, img->patches, img->num_patches * sizeof(sa1g_patch));
            sa1_mem_free(img->ctx, img->patches);
        }
        img->patches = np;
        img->cap_patches = cap;
    }
    p = &img->patches[img->num_patches++];
    p->kind = kind;
    p->guest = guest;
    p->value = value;
    p->arg = arg;
    p->why = why;
    return SA1_OK;
}

static const sa1_action_override *sa1g_find_override(const sa1_char_desc *cd, uint32_t i)
{
    uint32_t k;
    for (k = 0; k < cd->num_overrides; k++) {
        if (i >= cd->overrides[k].first && i <= cd->overrides[k].last) {
            return &cd->overrides[k];
        }
    }
    return NULL;
}

/*
 * Actions outside the loaded views (2.7): every PL_ACTION.actptr of the kind's action table that is
 * NULL, points outside the image, points into an overlay slot whose occupant is not the file the
 * manifest's override table names, or whose NJS_ACTION object / motion lies outside the image, is
 * replaced by the world's placeholder action (segment 0x18). Without this, Sonic's stage-overlay
 * actions would decode into B_CHAOS7 bytes, and PSetMotion dereferences actptr unconditionally.
 */
static sa1_status sa1g_patch_actions(sa1g_image *img, const sa1_char_desc *cd)
{
    uint32_t i;
    sa1_status st = SA1_OK;
    for (i = 0; i < cd->num_actions && st == SA1_OK; i++) {
        uint32_t slot = cd->action_table + 16u * i, act, obj = 0, mot = 0;
        const sa1_action_override *ov = sa1g_find_override(cd, i);
        const sa1g_span *sp;
        const char *why = NULL;
        if (sa1g_image_rd32(img, slot, &act) != 0) {
            return sa1_fail(img->ctx, SA1_ERR_BAD_FORMAT, "world image: action table %08X outside the image",
                            (unsigned)cd->action_table);
        }
        sp = sa1g_image_span_at(img, act, 8);
        if (act == 0) {
            why = "NULL action entry";
        } else if (sp == NULL) {
            why = "NJS_ACTION outside the world image";
        } else if (ov != NULL && strcmp(sp->name, ov->image) != 0) {
            why = "NJS_ACTION in an overlay slot held by another file";
        } else if (sa1g_image_rd32(img, act, &obj) != 0 || sa1g_image_rd32(img, act + 4u, &mot) != 0
                   || sa1g_image_span_at(img, obj, 0x34) == NULL || sa1g_image_span_at(img, mot, 12) == NULL) {
            why = "action object or motion outside the world image";
        }
        if (why != NULL) {
            st = sa1g_patch_add(img, SA1G_PATCH_ACTION_PLACEHOLDER, slot, act, i, why);
        }
    }
    return st;
}

sa1_status sa1g_image_finalize(sa1g_image *img)
{
    const sa1_char_desc *cd;
    sa1_status st = SA1_OK;
    if (img == NULL || img->ctx == NULL || img->num_spans == 0) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (img->finalized) {
        return SA1_OK;
    }
    img->num_patches = 0;
    img->main_object = 0;
    cd = img->kind >= 0 ? sa1_manifest_char(img->kind) : NULL;
    if (cd != NULL) {
        img->main_object = cd->main_object;
        if (cd->action_table && cd->num_actions) {
            st = sa1g_patch_actions(img, cd);
        }
    }
    /*
     * Further entries of 2.7 arrive with their phases: DeathZoneTable 8C1BE404[level][act] -> the
     * world's kill-plane list (a SA1G_PATCH_WORD to an extension-segment address, Phase 6); D_8C1BDF24 is
     * left as is (its code words resolve to trap entries of the code map).
     */
    if (st == SA1_OK) {
        img->finalized = 1;
    }
    return st;
}

sa1_status sa1g_image_add_stage(sa1g_image *img, const char *gdi_path, int32_t level, uint32_t flags)
{
    sa1ca_stage stage;
    sa1_status st;
    if (img == NULL || img->ctx == NULL || gdi_path == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1ca_disc_stage(img->ctx, gdi_path, level, flags & SA1G_IMAGE_ACCEPT_MODIFIED, &stage);
    if (st != SA1_OK) {
        return st;
    }
    /* the span takes the unpacked buffer (sa1_mem_alloc'd by the disc layer) */
    st = sa1g_image_add_owned(img, stage.name, stage.load, stage.data, stage.size, stage.sha1);
    stage.data = NULL;
    if (st != SA1_OK) {
        return st;
    }
    img->finalized = 0;
    st = sa1g_image_finalize(img);
    if (st == SA1_OK) {
        sa1_log(img->ctx, SA1_LOG_INFO, "world image %d: stage overlay %s at %08X (%u bytes, SHA-1 %s), %u patches",
                (int)img->kind, img->spans[img->num_spans - 1].name, (unsigned)img->spans[img->num_spans - 1].guest,
                (unsigned)img->spans[img->num_spans - 1].size, stage.sha1, (unsigned)img->num_patches);
    }
    return st;
}

uint32_t sa1g_image_count_patches(const sa1g_image *img, int32_t kind)
{
    uint32_t i, n = 0;
    for (i = 0; img != NULL && i < img->num_patches; i++) {
        n += img->patches[i].kind == kind;
    }
    return n;
}

/* ---- applying an image to a world (2.6) ------------------------------------------------------------ */

/* nodes of an NJS_OBJECT tree in the arena (child +0x2C, sibling +0x30), at most 1024 */
static uint32_t sa1g_count_nodes(const sa1g_world *w, uint32_t root)
{
    uint32_t stack[64], n = 0, sp = 0;
    if (root == 0) {
        return 0;
    }
    stack[sp++] = root;
    while (sp > 0 && n < 1024u) {
        uint32_t o = stack[--sp], child = 0, sibling = 0;
        if (sa1g_world_rd32(w, o + 0x2Cu, &child) != 0 || sa1g_world_rd32(w, o + 0x30u, &sibling) != 0) {
            continue;
        }
        n++;
        if (sibling && sp < 64u) {
            stack[sp++] = sibling;
        }
        if (child && sp < 64u) {
            stack[sp++] = child;
        }
    }
    return n;
}

/*
 * The placeholder (2.7): a 1-frame NJS_ACTION on the character's root object whose NJS_MOTION has no
 * keys (type ANG, one empty NJS_MDATA1 per node). Provisional: how the decompiled motion player treats
 * an empty track is checked when the player is linked (Phase 4 boot / M1).
 */
static uint32_t sa1g_build_placeholder(sa1g_world *w, uint32_t root)
{
    uint32_t nodes = sa1g_count_nodes(w, root), mdata, motion, action;
    uint8_t *m, *a;
    uint16_t type = 2, inp = 0;
    uint32_t frames = 1;
    if (nodes == 0) {
        nodes = 1;
    }
    mdata = sa1g_world_ext_alloc(w, (size_t)nodes * 8u, 4, NULL);
    motion = sa1g_world_ext_alloc(w, 12, 4, (void **)&m);
    action = sa1g_world_ext_alloc(w, 8, 4, (void **)&a);
    if (mdata == 0 || motion == 0 || action == 0) {
        return 0;
    }
    memcpy(m, &mdata, 4);
    memcpy(m + 4, &frames, 4);
    memcpy(m + 8, &type, 2);
    memcpy(m + 10, &inp, 2);
    memcpy(a, &root, 4);
    memcpy(a + 4, &motion, 4);
    return action;
}

static void sa1g_pcd_init_cb(sa1g_world *w, void *user)
{
    (void)w;
    (void)user;
    sa1g_pcd_init();
}

/* the run state of a rebuilt world: input, events, counters, boot and replay state, faults */
static void sa1g_world_reset_run(sa1g_world *w)
{
    memset(w->xmtrx, 0, sizeof w->xmtrx);
    memset(w->pad, 0, sizeof w->pad);
    memset(w->host_pad, 0, sizeof w->host_pad);
    memset(w->taps, 0, sizeof w->taps);
    memset(&w->events, 0, sizeof w->events);
    w->acc = 0.0;
    w->steps = w->pairs = 0;
    w->sr = 0;
    w->gbr = 0;
    w->booted = 0;
    w->chr = -1;
    w->level = 0;
    w->act = 0;
    w->player_tp = 0;
    w->camera_tp = 0;
    w->camera_twp = 0;
    memset(&w->camera, 0, sizeof w->camera);
    w->land_mode = SA1G_LAND_NONE;
    w->landtable = 0;
    w->window_cols = 0;
    w->window_cap = 0;
    memset(&w->inject, 0, sizeof w->inject);
    memset(w->xhooks, 0, sizeof w->xhooks);
    w->num_xhooks = 0;
    w->freeze_unknown = 0;
    w->frozen_skips = 0;
    w->frozen_last = 0;
    w->level_tp = 0;
    w->stage_tasks = 0;
    w->stage_note[0] = 0;
    w->super = 0;                   /* round 6a: the boot sets it (sa1_world_super.c) */
    w->super_reverts = 0;
    w->super_drains = 0;
    {
        /* round 0.9.3a: the General form's run state lived in segment 0x18 (GT, GW, TMP) or followed the last boot; the
           host's Super options stay (sa1_world_super.c) */
        sa1g_super_gen keep = w->sg;
        memset(&w->sg, 0, sizeof w->sg);
        w->sg.speed = keep.speed;
        w->sg.no_aura = keep.no_aura;
        w->sg.voice = keep.voice;
        w->sg.effects = keep.effects;
    }
    /* host objects: the state lived in segment 0x18, which the rebuild reset (the hooks stay: they rebuild it) */
    w->hostobj = 0;
    w->ho_wtimer = 0;
    w->ho_hurt_steps = 0xFFFFFFFFu;
    sa1g_hostobj_reset_run(w);      /* round 5: requests and per-step state (options and kind mask stay) */
    sa1g_world_clear_fault(w);
}

/* segment 0x18 from scratch: R4 pcdata (C-defined variables of the lowered code) owns its start */
static sa1_status sa1g_world_reset_ext(sa1g_world *w)
{
    sa1g_world_ext_reset(w);
    w->pcdata_size = sa1g_pcd_size();
    if (w->pcdata_size != 0 && sa1g_world_ext_alloc(w, w->pcdata_size, 16, NULL) != SA1G_EXT_GUEST) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    return SA1_OK;
}

/* ---- overlay images of the code map (2.5) ---------------------------------------------------------- */
const sa1g_fnimage *sa1g_fnimage_find(const char *name)
{
    size_t k;
    for (k = 0; name != NULL && k < sa1g_fnimage_count; k++) {
        if (strcmp(sa1g_fnimages[k].name, name) == 0) {
            return &sa1g_fnimages[k];
        }
    }
    return NULL;
}

/* the code-map images of an image's spans: same file (name and the SHA-1 of the PRS the C was decompiled from),
 * code range inside the span */
static void sa1g_world_fn_images(sa1g_world *w, const sa1g_image *img)
{
    uint32_t i, n = 0;
    size_t k;
    memset(w->fn_images, 0, sizeof w->fn_images);
    for (i = 0; img != NULL && i < img->num_spans; i++) {
        const sa1g_span *s = &img->spans[i];
        uint32_t lo = s->guest & 0x1FFFFFFFu, hi = lo + s->size;
        for (k = 0; k < sa1g_fnimage_count; k++) {
            const sa1g_fnimage *m = &sa1g_fnimages[k];
            if (strcmp(s->name, m->prs) == 0 && strcmp(s->sha1, m->prs_sha1) == 0 && m->lo >= lo && m->hi <= hi
                && n < SA1G_FN_ACTIVE_MAX) {
                w->fn_images[n++] = m;
                sa1_log(img->ctx, SA1_LOG_DEBUG, "world image: the code map of overlay %s (%u functions) is active",
                        m->name, (unsigned)m->count);
            }
        }
    }
    /* libsa1's own task execs (the host-object proxies, 0x7F000100: SH-4 area 7, never game code), in every world after
     * every rebuild (apply_image and apply_ram both come here): sadc docs/libsa1_interaction.md 6.4 row 1 */
    if (n < SA1G_FN_ACTIVE_MAX) {
        w->fn_images[n++] = &sa1g_hostobj_image;
    }
    w->num_fn_images = n;
}

sa1_status sa1g_world_apply_ram(sa1g_world *w, const void *ram, size_t n)
{
    if (w != NULL) {
        w->fresh = 0;
    }
    sa1_status st;
    if (w == NULL || w->ram == NULL || ram == NULL || n != SA1G_ARENA_SIZE) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (sa1g_world_current() == w) {
        return SA1_ERR_INVALID_ARGUMENT;    /* never rebuild a running world */
    }
    st = sa1g_world_reset_ext(w);
    if (st != SA1_OK) {
        return st;
    }
    memcpy(w->ram, ram, SA1G_ARENA_SIZE);
    w->image = NULL;
    sa1g_world_fn_images(w, NULL);      /* a RAM image's overlays are not verified: base code map only */
    w->placeholder_action = 0;
    memset(w->missing_actions, 0, sizeof w->missing_actions);
    w->patches_applied = 0;
    sa1g_world_reset_run(w);
    if (w->pcdata_size != 0) {
        return sa1g_world_call(w, sa1g_pcd_init_cb, NULL);
    }
    return SA1_OK;
}

sa1_status sa1g_world_apply_image(sa1g_world *w, const sa1g_image *img)
{
    uint32_t i;
    if (w == NULL || w->ram == NULL || img == NULL || !img->finalized) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (sa1g_world_current() == w) {
        return SA1_ERR_INVALID_ARGUMENT;    /* never rebuild a running world */
    }
    /* 1-3: zero arena (1ST_READ, BSS, overlay slots), extension segment, then the spans; a fresh arena (just committed,
       never written: w->fresh) is zero already, and touching all of it would only fault in pages the world may never use */
    if (!w->fresh) {
        memset(w->ram, 0, SA1G_ARENA_SIZE);
    }
    w->fresh = 0;
    if (sa1g_world_reset_ext(w) != SA1_OK) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    for (i = 0; i < img->num_spans; i++) {
        if (sa1g_world_load(w, img->spans[i].guest, img->spans[i].data, img->spans[i].size) != 0) {
            return SA1_ERR_BAD_FORMAT;
        }
    }
    /* 4: patches */
    w->placeholder_action = 0;
    memset(w->missing_actions, 0, sizeof w->missing_actions);
    w->patches_applied = 0;
    for (i = 0; i < img->num_patches; i++) {
        const sa1g_patch *p = &img->patches[i];
        uint32_t v = p->value;
        if (p->kind == SA1G_PATCH_ACTION_PLACEHOLDER) {
            if (w->placeholder_action == 0) {
                w->placeholder_action = sa1g_build_placeholder(w, img->main_object);
                if (w->placeholder_action == 0) {
                    return SA1_ERR_OUT_OF_MEMORY;
                }
            }
            v = w->placeholder_action;
            if (p->arg < 256u) {
                w->missing_actions[p->arg >> 5] |= 1u << (p->arg & 31u);
            }
        }
        if (sa1g_world_wr32(w, p->guest, v) != 0) {
            return SA1_ERR_BAD_FORMAT;
        }
        w->patches_applied++;
        sa1_log(img->ctx, SA1_LOG_DEBUG, "world patch %u: %08X = %08X (was %08X%s%s, arg %u)", (unsigned)i,
                (unsigned)p->guest, (unsigned)v, (unsigned)p->value, p->why ? ": " : "", p->why ? p->why : "",
                (unsigned)p->arg);
    }
    /* the runtime state (6.2) comes with boot (sa1g_world_boot); reset the run state */
    w->image = img;
    sa1g_world_fn_images(w, img);
    sa1g_world_reset_run(w);
    /* 5: pcdata initial values (pointer initialisers encoded now), inside the world */
    if (w->pcdata_size != 0) {
        return sa1g_world_call(w, sa1g_pcd_init_cb, NULL);
    }
    return SA1_OK;
}
