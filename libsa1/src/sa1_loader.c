/*
 * sa1_load_disc: reads the user's disc, verifies it, maps the memory images and
 * converts the assets of every requested character into native structures.
 *
 * Image model: 1ST_READ.BIN is mapped at 0x8C010000 and each overlay at its load
 * address.  Several overlays share an address (all *_MOT at 0x0CC00000; STGxx,
 * MINICART and B_CHAOS7 at 0x0C900000), so pointers are resolved per character in an
 * ordered view of images, and actions that live in a shared-address overlay are
 * resolved in exactly that overlay (sa1_action_override).  A pointer into an overlay
 * that is not loaded makes the action "not loaded"; it is never followed.
 */
#include "sa1_internal.h"
#include "sa1_capture.h"    /* sa1_texture_make_variants (round 0.9.4a) */

#include <stdio.h>

typedef struct loader {
    sa1_context *ctx;
    sa1_disc     disc;
    uint32_t     flags;
    sa1_image    images[SA1_MAX_IMAGES];
    uint32_t     num_images;
    char         failed[SA1_MAX_IMAGES][32];
    uint32_t     num_failed;
    int          partial;
} loader;

/* ------------------------------------------------------------------------- */
/* Files                                                                     */
/* ------------------------------------------------------------------------- */

/* Reads SONICADV/<bare> (or 1ST_READ.BIN from the root), checks its SHA-1 against the
 * manifest and records the result.  On success *rec is the file record index. */
static sa1_status read_checked(loader *L, const char *bare, uint8_t **data, uint32_t *size, uint32_t *rec)
{
    sa1_context *ctx = L->ctx;
    const sa1_file_desc *fd = sa1_manifest_file(bare);
    int is_root = strcmp(bare, SA1_FIRST_READ_NAME) == 0;
    char iso[64];
    double t0 = sa1_now_ms();
    sa1_file_record *r;
    sa1_status st;

    *data = NULL;
    *size = 0;
    snprintf(iso, sizeof iso, "%s%s", is_root ? "" : "SONICADV/", bare);
    st = sa1_disc_read_file(&L->disc, iso, data, size);
    r = (sa1_file_record *)sa1_vec_push(ctx, &ctx->files);
    if (!r) {
        sa1_mem_free(ctx, *data);
        *data = NULL;
        return SA1_ERR_OUT_OF_MEMORY;
    }
    *rec = ctx->files.count - 1u;
    snprintf(r->name, sizeof r->name, "%s", iso);
    if (st != SA1_OK) {
        r->hash_status = SA1_HASH_UNKNOWN;
        sa1_log(ctx, SA1_LOG_WARN, "cannot read %s (%s)", iso, sa1_status_string(st));
        return st;
    }
    r->size = r->unpacked_size = *size;
    sa1_sha1_hex(*data, *size, r->sha1);
    if (!fd)
        r->hash_status = SA1_HASH_UNKNOWN;
    else
        r->hash_status = (fd->size == *size && strcmp(fd->sha1, r->sha1) == 0) ? SA1_HASH_OK : SA1_HASH_MISMATCH;
    r->read_ms = sa1_now_ms() - t0;
    if (r->hash_status != SA1_HASH_OK && (is_root || !(L->flags & SA1_LOAD_ACCEPT_MODIFIED))) {
        sa1_log(ctx, SA1_LOG_WARN, "%s: SHA-1 %s %s; file rejected", iso, r->sha1,
                r->hash_status == SA1_HASH_MISMATCH ? "does not match US v1.005" : "is not in the manifest");
        sa1_mem_free(ctx, *data);
        *data = NULL;
        return SA1_ERR_BAD_HASH;
    }
    if (r->hash_status != SA1_HASH_OK)
        sa1_log(ctx, SA1_LOG_WARN, "%s: unknown SHA-1 %s accepted (SA1_LOAD_ACCEPT_MODIFIED)", iso, r->sha1);
    r->used = 1;
    return SA1_OK;
}

static int ends_with_prs(const char *s)
{
    size_t n = strlen(s);
    return n >= 4 && strcmp(s + n - 4, ".PRS") == 0;
}

/* Reads, verifies and (if compressed) unpacks a whole file. */
static sa1_status read_unpacked(loader *L, const char *bare, uint8_t **out, uint32_t *out_size)
{
    uint8_t *raw;
    uint32_t rawsize, rec;
    double t0 = sa1_now_ms();
    sa1_status st = read_checked(L, bare, &raw, &rawsize, &rec);
    if (st != SA1_OK)
        return st;
    if (!ends_with_prs(bare)) {
        *out = raw;
        *out_size = rawsize;
        return SA1_OK;
    }
    st = sa1_prs_decompress(L->ctx, raw, rawsize, SA1_MAX_UNPACKED_SIZE, out, out_size);
    sa1_mem_free(L->ctx, raw);
    if (st != SA1_OK) {
        SA1_VEC(L->ctx->files, sa1_file_record)[rec].used = 0;
        sa1_log(L->ctx, SA1_LOG_WARN, "%s: PRS decompression failed (%s)", bare, sa1_status_string(st));
        return st;
    }
    SA1_VEC(L->ctx->files, sa1_file_record)[rec].unpacked_size = *out_size;
    SA1_VEC(L->ctx->files, sa1_file_record)[rec].read_ms += sa1_now_ms() - t0;
    return SA1_OK;
}

static const sa1_image *find_image(const loader *L, const char *name)
{
    uint32_t i;
    for (i = 0; i < L->num_images; i++)
        if (strcmp(L->images[i].name, name) == 0)
            return &L->images[i];
    return NULL;
}

/* Returns the mapped image, loading it on first use; NULL if missing or rejected. */
static const sa1_image *get_image(loader *L, const char *name)
{
    const sa1_file_desc *fd;
    const sa1_image *have = find_image(L, name);
    sa1_image *im;
    uint32_t i;
    sa1_status st;
    if (have)
        return have;
    for (i = 0; i < L->num_failed; i++)
        if (strcmp(L->failed[i], name) == 0)
            return NULL;
    fd = sa1_manifest_file(name);
    if (!fd || !fd->load_addr || L->num_images >= SA1_MAX_IMAGES)
        return NULL;
    im = &L->images[L->num_images];
    memset(im, 0, sizeof *im);
    st = read_unpacked(L, name, &im->data, &im->size);
    if (st == SA1_OK && (uint64_t)(fd->load_addr & 0x1FFFFFFFu) + im->size > 0x20000000u)
        st = SA1_ERR_BAD_FORMAT;
    if (st != SA1_OK) {
        sa1_mem_free(L->ctx, im->data);
        im->data = NULL;
        if (L->num_failed < SA1_MAX_IMAGES)
            snprintf(L->failed[L->num_failed++], 32, "%s", name);
        L->partial = 1;
        return NULL;
    }
    snprintf(im->name, sizeof im->name, "%s", name);
    im->load_addr = fd->load_addr;
    im->phys = fd->load_addr & 0x1FFFFFFFu;
    im->id = (int32_t)L->num_images;
    snprintf(L->ctx->image_names[L->num_images], 32, "%s", name);
    L->num_images++;
    L->ctx->num_image_names = L->num_images;
    return im;
}

/* ------------------------------------------------------------------------- */
/* Textures                                                                  */
/* ------------------------------------------------------------------------- */

static int read_cstring(const sa1_view *v, uint32_t addr, char *out, size_t cap)
{
    size_t i;
    for (i = 0; i + 1 < cap; i++) {
        const uint8_t *p = sa1_view_get(v, addr + (uint32_t)i, 1, NULL);
        if (!p)
            return 0;
        if (*p == 0) {
            out[i] = 0;
            return i > 0;
        }
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_'))
            return 0;
        out[i] = (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p);
    }
    return 0;
}

static void load_archive(loader *L, sa1_charset *cs, const char *name, uint32_t texlist, const sa1_view *v)
{
    sa1_context *ctx = L->ctx;
    sa1_archive *ar;
    if (cs->num_archives >= SA1_MAX_ARCHIVES) {
        sa1_log(ctx, SA1_LOG_WARN, "%s: more than %u texture archives", name, (unsigned)SA1_MAX_ARCHIVES);
        L->partial = 1;
        return;
    }
    ar = &cs->archives[cs->num_archives++];
    char file[48];
    uint8_t *data = NULL;
    uint32_t size = 0, n = 0, i;
    sa1_pvm_entry *ents = NULL;
    const uint8_t *tl;
    sa1_status st;

    snprintf(ar->name, sizeof ar->name, "%s", name);
    ar->texlist_addr = texlist;
    tl = sa1_view_get(v, texlist, 8, NULL);
    if (tl)
        ar->texlist_count = sa1_rd32(tl + 4);
    snprintf(file, sizeof file, "%s.PRS", name);
    st = read_unpacked(L, file, &data, &size);
    if (st != SA1_OK) {
        L->partial = 1;
        return;
    }
    ents = (sa1_pvm_entry *)sa1_mem_array(ctx, SA1_MAX_TEXTURES, sizeof(sa1_pvm_entry));
    st = ents ? sa1_pvm_parse(data, size, ents, SA1_MAX_TEXTURES, &n) : SA1_ERR_OUT_OF_MEMORY;
    if (st != SA1_OK) {
        sa1_log(ctx, SA1_LOG_WARN, "%s: invalid PVM archive (%s)", file, sa1_status_string(st));
        L->partial = 1;
        goto done;
    }
    if (tl && ar->texlist_count != n)
        sa1_log(ctx, SA1_LOG_WARN, "%s: PVM has %u textures, texlist 0x%08X expects %u", file, (unsigned)n,
                (unsigned)texlist, (unsigned)ar->texlist_count);
    ar->tex = (sa1_texture *)sa1_arena_array(&cs->arena, n ? n : 1u, sizeof(sa1_texture));
    if (!ar->tex) {
        L->partial = 1;
        goto done;
    }
    ar->num_textures = n;
    for (i = 0; i < n; i++) {
        sa1_texture *t = &ar->tex[i];
        const uint8_t *chunk = data + ents[i].offset;
        uint32_t w, h, pf, df;
        snprintf(t->name, sizeof t->name, "%s", ents[i].name);
        t->gbix = ents[i].gbix;
        st = sa1_pvr_header(chunk, ents[i].length, &w, &h, &pf, &df);
        if (st == SA1_OK) {
            t->width = w;
            t->height = h;
            t->pixel_format = (uint8_t)pf;
            t->data_format = (uint8_t)df;
            t->rgba = (uint8_t *)sa1_arena_alloc(&cs->arena, (size_t)w * h * 4u);
            st = t->rgba ? sa1_pvr_decode(chunk, ents[i].length, t->rgba, (size_t)w * h * 4u) : SA1_ERR_OUT_OF_MEMORY;
        }
        if (st != SA1_OK) {
            t->rgba = NULL;
            sa1_log(ctx, SA1_LOG_WARN, "%s texture %u (%s): %s", file, (unsigned)i, t->name, sa1_status_string(st));
        }
    }
done:
    sa1_mem_free(ctx, ents);
    sa1_mem_free(ctx, data);
}

/* Round 0.9.5 (the board; sadc docs/research/gsa1_095_plan.md 6.2 task 4, board095_lib.md 2.6 / 4.3): a texture archive of
 * ONE standalone PVR file (<name>.PVR, uncompressed, checked against the manifest): an optional GBIX chunk skipped by its
 * own length (MIW_B001's is 4 bytes long, SB_BOARD1's and MTX_BOARD0's 8), then the PVRT, decoded with sa1_pvr_header /
 * sa1_pvr_decode. texlist: the game's texlist that names it (the board task's njSetTexture), so the capture resolves the
 * board's draws into this archive (sa1_capture_resolve, by the texlist's TEXNAME address). */
static void load_pvr_archive(loader *L, sa1_charset *cs, const char *name, uint32_t texlist, const sa1_view *v)
{
    sa1_context *ctx = L->ctx;
    sa1_archive *ar;
    char file[48];
    uint8_t *data = NULL;
    uint32_t size = 0, pos = 0, gbix = 0, w, h, pf, df;
    const uint8_t *tl;
    sa1_texture *t;
    sa1_status st;
    if (cs->num_archives >= SA1_MAX_ARCHIVES) {
        sa1_log(ctx, SA1_LOG_WARN, "%s: more than %u texture archives", name, (unsigned)SA1_MAX_ARCHIVES);
        L->partial = 1;
        return;
    }
    snprintf(file, sizeof file, "%s.PVR", name);
    st = read_unpacked(L, file, &data, &size);
    if (st != SA1_OK) {
        L->partial = 1;
        return;
    }
    if (size >= 8u && memcmp(data, "GBIX", 4) == 0) {
        uint32_t len = sa1_rd32(data + 4);
        if (len >= 4u && len <= size - 8u) {
            gbix = sa1_rd32(data + 8);
        }
        pos = len <= size - 8u ? 8u + len : size;
    }
    st = pos < size ? sa1_pvr_header(data + pos, size - pos, &w, &h, &pf, &df) : SA1_ERR_BAD_FORMAT;
    if (st != SA1_OK) {
        sa1_log(ctx, SA1_LOG_WARN, "%s: not a PVR texture (%s)", file, sa1_status_string(st));
        L->partial = 1;
        sa1_mem_free(ctx, data);
        return;
    }
    ar = &cs->archives[cs->num_archives];
    memset(ar, 0, sizeof *ar);
    ar->tex = (sa1_texture *)sa1_arena_array(&cs->arena, 1u, sizeof(sa1_texture));
    if (!ar->tex) {
        L->partial = 1;
        sa1_mem_free(ctx, data);
        return;
    }
    cs->num_archives++;
    snprintf(ar->name, sizeof ar->name, "%s", name);
    ar->texlist_addr = texlist;
    tl = sa1_view_get(v, texlist, 8, NULL);
    ar->texlist_count = tl ? sa1_rd32(tl + 4) : 0u;
    if (tl && ar->texlist_count != 1u) {
        sa1_log(ctx, SA1_LOG_WARN, "%s: texlist 0x%08X expects %u textures (1 in the file)", file, (unsigned)texlist,
                (unsigned)ar->texlist_count);
    }
    ar->num_textures = 1;
    t = &ar->tex[0];
    snprintf(t->name, sizeof t->name, "%s", name);
    t->gbix = gbix;
    t->width = w;
    t->height = h;
    t->pixel_format = (uint8_t)pf;
    t->data_format = (uint8_t)df;
    t->rgba = (uint8_t *)sa1_arena_alloc(&cs->arena, (size_t)w * h * 4u);
    st = t->rgba ? sa1_pvr_decode(data + pos, size - pos, t->rgba, (size_t)w * h * 4u) : SA1_ERR_OUT_OF_MEMORY;
    if (st != SA1_OK) {
        t->rgba = NULL;
        sa1_log(ctx, SA1_LOG_WARN, "%s: %s", file, sa1_status_string(st));
    }
    sa1_mem_free(ctx, data);
}

/* the board archives, after every other archive (no index of an existing one changes): Sonic MIW_B001 (texlist D_8C1C6380
 * in 1ST_READ) and SB_BOARD1 (0x0CB36F94 in S_SBMOT), Tails MTX_BOARD0 (0x0CB025A0 in M_SBMOT) - the texlists the board
 * tasks' inits 8C0B2EE6 / 8C0BF7CE set (sadc code_8C0B2EA0.c:84-89, code_8C0BF262.c:291-303). Only with SA1_LOAD_BOARDS
 * (the overlays that hold two of the texlists come with it); never for Super Sonic (kind 8 does not ride) */
static void load_board_textures(loader *L, sa1_charset *cs, int32_t kind, const sa1_view *base)
{
    if (!(L->flags & SA1_LOAD_BOARDS)) {
        return;
    }
    if (kind == SA1_CHAR_SONIC) {
        load_pvr_archive(L, cs, "MIW_B001", 0x8C1C6380u, base);
        load_pvr_archive(L, cs, "SB_BOARD1", 0x0CB36F94u, base);
    } else if (kind == SA1_CHAR_TAILS) {
        load_pvr_archive(L, cs, "MTX_BOARD0", 0x0CB025A0u, base);
    }
}

/* ------------------------------------------------------------------------- */
/* Characters                                                                */
/* ------------------------------------------------------------------------- */

static int view_add(sa1_view *v, const sa1_image *im)
{
    uint32_t i;
    for (i = 0; i < v->n; i++) {
        if (v->img[i] == im)
            return 1;
        if (sa1_images_overlap(v->img[i], im))
            return 0;
    }
    if (v->n >= SA1_VIEW_MAX)
        return 0;
    v->img[v->n++] = im;
    return 1;
}

static const sa1_action_override *find_override(const sa1_char_desc *cd, uint32_t i)
{
    uint32_t k;
    for (k = 0; k < cd->num_overrides; k++)
        if (i >= cd->overrides[k].first && i <= cd->overrides[k].last)
            return &cd->overrides[k];
    return NULL;
}

static void load_actions(loader *L, sa1_charset *cs, const sa1_char_desc *cd, const sa1_view *base)
{
    sa1_context *ctx = L->ctx;
    const uint8_t *tbl;
    uint32_t i;
    if (!cd->action_table || !cd->num_actions)
        return;
    tbl = sa1_view_get(base, cd->action_table, 16u * cd->num_actions, NULL);
    cs->actions = (sa1_action *)sa1_arena_array(&cs->arena, cd->num_actions, sizeof(sa1_action));
    if (!tbl || !cs->actions) {
        L->partial = 1;
        return;
    }
    cs->num_actions = cd->num_actions;
    for (i = 0; i < cd->num_actions; i++) {
        const uint8_t *e = tbl + 16u * i, *pa;
        sa1_action *a = &cs->actions[i];
        const sa1_action_override *ov = find_override(cd, i);
        const sa1_image *aim = NULL;
        sa1_view view;
        uint32_t objaddr, motaddr, k;
        sa1_status st;

        a->addr = sa1_rd32(e);
        a->objnum = e[4];
        a->mode = (int8_t)e[5];
        a->next = sa1_rd16(e + 6);
        a->blend = sa1_rdf32(e + 8);
        a->speed = sa1_rdf32(e + 12);
        a->object = a->motion = a->image = -1;
        if (a->addr == 0) {
            a->status = SA1_ACTION_EMPTY;
            continue;
        }
        a->status = SA1_ACTION_NOT_LOADED;
        view = *base;
        if (ov) {
            const sa1_image *oim = (L->flags & ov->load_flag) ? get_image(L, ov->image) : find_image(L, ov->image);
            if (!oim)
                continue;
            view.n = 0;
            view.img[view.n++] = oim;
            for (k = 0; k < base->n; k++)
                view_add(&view, base->img[k]);
        }
        pa = sa1_view_get(&view, a->addr, 8, &aim);
        if (!pa)
            continue;                           /* lives in an overlay that is not loaded */
        a->image = aim->id;
        objaddr = sa1_rd32(pa);
        motaddr = sa1_rd32(pa + 4);
        a->object_addr = objaddr;
        a->motion_addr = motaddr;
        for (k = 0; k < cd->num_subst; k++)
            if ((objaddr & 0x1FFFFFFFu) == (cd->subst[k][0] & 0x1FFFFFFFu))
                objaddr = cd->subst[k][1];
        if (!sa1_view_get(&view, objaddr, 0x34, NULL) || !sa1_view_get(&view, motaddr, 12, NULL))
            continue;
        a->status = SA1_ACTION_INVALID;
        a->object = sa1_ninja_object(ctx, cs, &view, objaddr, &st);
        if (a->object < 0)
            continue;
        a->motion = sa1_ninja_motion(ctx, cs, &view, motaddr, SA1_VEC(cs->objects, sa1_object)[a->object].num_nodes, &st);
        if (a->motion < 0) {
            a->object = -1;
            continue;
        }
        if (a->objnum != SA1_VEC(cs->objects, sa1_object)[a->object].num_nodes)
            sa1_log(ctx, SA1_LOG_DEBUG, "%s action %u: objnum %u, object has %u nodes", cd->name, (unsigned)i,
                    (unsigned)a->objnum, (unsigned)SA1_VEC(cs->objects, sa1_object)[a->object].num_nodes);
        a->object_addr = objaddr;
        a->status = SA1_ACTION_OK;
        cs->num_available++;
    }
}

static void load_physics(sa1_charset *cs, const sa1_char_desc *cd, const sa1_view *base)
{
    const uint8_t *p;
    float *f = &cs->physics.pos_error;
    uint32_t i;
    if (cd->game_id < 0)
        return;
    p = sa1_view_get(base, SA1_ADDR_PHYSICS + SA1_PHYSICS_STRIDE * (uint32_t)cd->game_id, SA1_PHYSICS_STRIDE, NULL);
    if (!p)
        return;
    cs->physics.jump2_timer = sa1_rds32(p);
    for (i = 0; i < 32; i++)
        f[i] = sa1_rdf32(p + 4u + 4u * i);
    cs->has_physics = 1;
}

/* The common archives of the stage start that a character's own draws use, after its own archives, in the game's
 * order (the stage start's list SA1_ADDR_PVM_COMMON, entry 0 first): OBJ_REGULAR, then EFF_REGULAR.
 *   OBJ_REGULAR (entry 0, texlist 0x8C19F530, 96 textures): the blob shadow's. The shadow texture setup 8C099500 sets
 *     the stage-start texlist B_8C79F0F0 (sa1_world_boot.c sa1g_boot_stage_texlist: OBJ_REGULAR) before every
 *     Character_Shadow_Blob, whose material is its texture 58 "shadow" (sadc docs/research/gsa1_060_shadow.md L2).
 *   EFF_REGULAR (entry 1): the particle lists' texlists (sadc code_8C09E0B8.c: the dust, the water splash, sparks ...
 *     drawn by the particle manager 8C09DF64 with the game's sprite draw 8C09E778) are slices of its texname array.
 *   CON_REGULAR (entry 2, round 0.9.1; texlist 0x8C1C3310, 56 textures): the HUD's (the rings / time / lives digits and
 *     labels, the life icons, Big's HUD life icon; sadc docs/research/gsa1_070_hud.md 1.2 / F5): the HUD's texlists
 *     D_8C1BF1A4 (its first 24 names) and D_8C1BF19C (names 24-34) are slices of its texname array.
 * The display capture resolves their textures into these archives (sa1_capture_resolve). Archive indices: Sonic 2 / 3 / 4,
 * Tails 1 / 2 / 3, Knuckles and Amy 2 / 3 / 4, E-102 5 / 6 / 7 (8 archives: SA1_MAX_ARCHIVES is 12 since round 0.9.1),
 * Big 3 / 4 / 5, Eggman and Tikal 1 / 2 / 3, Super Sonic 1 / 2 / 3 (before OBJ_REGULAR was loaded, EFF_REGULAR had
 * OBJ_REGULAR's index); hosts find them by name (sa1_texture_archive). Round 6a: Super Sonic, and Sonic with
 * SA1_LOAD_SUPER_FORMS, load the other form's body archives after these (load_other_body). */
static void load_common_textures(loader *L, sa1_charset *cs, const sa1_view *base)
{
    static const char *const want[] = { "OBJ_REGULAR", "EFF_REGULAR", "CON_REGULAR" };
    uint32_t i, k;
    for (i = 0; i < 16u; i++) {
        char name[28];
        const uint8_t *e = sa1_view_get(base, SA1_ADDR_PVM_COMMON + 8u * i, 8, NULL);
        if (!e || sa1_rd32(e) == 0 || !read_cstring(base, sa1_rd32(e), name, sizeof name))
            break;
        for (k = 0; k < sizeof want / sizeof want[0]; k++)
            if (strcmp(name, want[k]) == 0)
                load_archive(L, cs, name, sa1_rd32(e + 4), base);
    }
}

/* the archives of the PVM list of game character `game_id` (SA1_ADDR_PVM_LISTS[game_id]) */
static void load_pvm_list(loader *L, sa1_charset *cs, const sa1_char_desc *cd, int32_t game_id, const sa1_view *base)
{
    const uint8_t *p;
    uint32_t list, i;
    p = sa1_view_get(base, SA1_ADDR_PVM_LISTS + 4u * (uint32_t)game_id, 4, NULL);
    if (!p)
        return;
    list = sa1_rd32(p);
    for (i = 0; i < SA1_MAX_ARCHIVES; i++) {
        char name[28];
        const uint8_t *e = sa1_view_get(base, list + 8u * i, 8, NULL);
        if (!e || sa1_rd32(e) == 0)
            break;
        if (!read_cstring(base, sa1_rd32(e), name, sizeof name)) {
            sa1_log(L->ctx, SA1_LOG_WARN, "%s: invalid texture archive name in the PVM list", cd->name);
            L->partial = 1;
            break;
        }
        load_archive(L, cs, name, sa1_rd32(e + 4), base);
    }
}

static void load_textures(loader *L, sa1_charset *cs, const sa1_char_desc *cd, const sa1_view *base)
{
    if (cd->archive) {
        load_archive(L, cs, cd->archive, cd->archive_texlist, base);
        return;
    }
    if (cd->game_id < 0)
        return;
    load_pvm_list(L, cs, cd, cd->game_id, base);
}

/* Round 6a (Super Sonic, include/libsa1.h "Super Sonic (API 0.4)"; super_sonic.md Review R.2.5 / R.3.3): Sonic's world can
 * turn Super and Super Sonic's can turn back, so each of the two kinds also loads the other form's body archives, after
 * all of its other archives (no index of before changes):
 *   Super Sonic  Sonic's PVM list 0x8C1C2584[0]: SONIC (texlist 0x8C1C6C94) and SON_EFF (0x8C1C6AB4): archives 4 and 5
 *   Sonic        SUPERSONIC (texlist 0x0C901EA0, in B_CHAOS7) with SA1_LOAD_SUPER_FORMS when B_CHAOS7 is part of his
 *                images (SA1_LOAD_SUPER): archive 5 (opt-in: a host that never turns Sonic Super paints no extra archive) */
#define SA1_SUPER_TEXLIST 0x0C901EA0u
static void load_other_body(loader *L, sa1_charset *cs, int32_t kind, const sa1_char_desc *cd, const sa1_view *base)
{
    const sa1_image *which = NULL;
    if (kind == SA1_CHAR_SUPER_SONIC) {
        load_pvm_list(L, cs, cd, SA1_CHAR_SONIC, base);
    } else if (kind == SA1_CHAR_SONIC && (L->flags & SA1_LOAD_SUPER_FORMS) &&
               sa1_view_get(base, SA1_SUPER_TEXLIST, 8, &which) != NULL && which != NULL &&
               strcmp(which->name, "B_CHAOS7.PRS") == 0) {
        load_archive(L, cs, "SUPERSONIC", SA1_SUPER_TEXLIST, base);
    }
}

static void build_character(loader *L, int32_t kind, const sa1_image *first)
{
    sa1_context *ctx = L->ctx;
    const sa1_char_desc *cd = sa1_manifest_char(kind);
    sa1_charset *cs = &ctx->chars[kind];
    sa1_view base;
    sa1_status st;
    uint32_t k;
    double t0 = sa1_now_ms();

    memset(cs, 0, sizeof *cs);
    cs->arena.ctx = ctx;
    cs->models.elem = sizeof(sa1_model);
    cs->objects.elem = sizeof(sa1_object);
    cs->motions.elem = sizeof(sa1_motion);
    cs->main_object = -1;

    memset(&base, 0, sizeof base);
    base.img[base.n++] = first;
    for (k = 0; k < 4; k++) {
        const sa1_image *im;
        if (!cd->images[k] || (cd->image_flags[k] && !(L->flags & cd->image_flags[k])))
            continue;
        im = get_image(L, cd->images[k]);
        if (!im) {
            sa1_log(ctx, SA1_LOG_WARN, "%s: overlay %s is not available", cd->name, cd->images[k]);
            continue;
        }
        if (!view_add(&base, im)) {
            sa1_log(ctx, SA1_LOG_WARN, "%s: overlay %s overlaps another mapped image", cd->name, cd->images[k]);
            L->partial = 1;
        }
    }

    cs->main_object_addr = cd->main_object;
    cs->main_object = sa1_ninja_object(ctx, cs, &base, cd->main_object, &st);
    if (cs->main_object < 0) {
        sa1_log(ctx, SA1_LOG_WARN, "%s: main model 0x%08X not available (%s)", cd->name, (unsigned)cd->main_object,
                sa1_status_string(st));
        L->partial = 1;
        sa1_charset_free(ctx, cs);
        return;
    }
    load_actions(L, cs, cd, &base);
    st = sa1_ninja_welds(ctx, cs, &base, cd->weld_table);
    if (st != SA1_OK) {
        sa1_log(ctx, SA1_LOG_WARN, "%s: weld list unusable (%s)", cd->name, sa1_status_string(st));
        cs->num_welds = 0;
        L->partial = 1;
    }
    load_physics(cs, cd, &base);
    load_textures(L, cs, cd, &base);
    load_common_textures(L, cs, &base);
    load_other_body(L, cs, kind, cd, &base);
    /* round 0.9.4a: libsa1's texture variants of the set (Sonic with SA1_LOAD_SUPER_FORMS: the golden Light Speed
       textures, sa1_texture_variant_get; sa1_texture.c), in the set's arena */
    sa1_texture_make_variants(ctx, cs, kind, L->flags);
    /* round 0.9.5: the board's three PVRs, after every other archive */
    load_board_textures(L, cs, kind, &base);

    for (k = 0; k < cs->objects.count; k++) {
        const sa1_object *o = &SA1_VEC(cs->objects, sa1_object)[k];
        if (o->num_nodes > cs->max_nodes) cs->max_nodes = o->num_nodes;
        if (o->work_vertices > cs->max_work_vertices) cs->max_work_vertices = o->work_vertices;
        if (o->drawn_vertices > cs->max_vertices) cs->max_vertices = o->drawn_vertices;
        if (o->drawn_tris > cs->max_triangles) cs->max_triangles = o->drawn_tris;
    }
    cs->loaded = 1;
    sa1_log(ctx, SA1_LOG_INFO, "%s: %u nodes, %u/%u actions, %u motions, %u welds, %u archives, %.0f KB (%.1f ms)",
            cd->name, (unsigned)SA1_VEC(cs->objects, sa1_object)[cs->main_object].num_nodes,
            (unsigned)cs->num_available, (unsigned)cs->num_actions, (unsigned)cs->motions.count,
            (unsigned)cs->num_welds, (unsigned)cs->num_archives, (double)cs->arena.total / 1024.0,
            sa1_now_ms() - t0);
}

void sa1_charset_free(sa1_context *ctx, sa1_charset *cs)
{
    sa1_arena_release(&cs->arena);
    sa1_vec_free(ctx, &cs->models);
    sa1_vec_free(ctx, &cs->objects);
    sa1_vec_free(ctx, &cs->motions);
    memset(cs, 0, sizeof *cs);
    cs->main_object = -1;
}

void sa1_unload_disc(sa1_context *ctx)
{
    int32_t k;
    if (!ctx)
        return;
    sa1_characters_destroy_all(ctx);
    for (k = 0; k < SA1_CHAR_COUNT; k++)
        sa1_charset_free(ctx, &ctx->chars[k]);
    ctx->files.count = 0;
    ctx->num_image_names = 0;
    ctx->disc_loaded = 0;
}

sa1_status sa1_load_disc(sa1_context *ctx, const char *gdi_path, const sa1_load_options *options)
{
    sa1_load_options opt;
    loader *L;
    const sa1_image *first;
    sa1_status st;
    uint32_t mask, i;
    int32_t k;
    double t0 = sa1_now_ms();

    if (!ctx || !gdi_path)
        return SA1_ERR_INVALID_ARGUMENT;
    memset(&opt, 0, sizeof opt);
    opt.flags = SA1_LOAD_DEFAULT;
    if (options) {
        if (options->struct_size < sizeof(uint32_t))
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_load_options.struct_size not set");
        memset(&opt, 0, sizeof opt);
        memcpy(&opt, options, options->struct_size < sizeof opt ? options->struct_size : sizeof opt);
    }
    mask = opt.characters & SA1_CHAR_MASK_ALL;
    if (mask == 0) {
        mask = SA1_CHAR_MASK_ALL;
        if (!(opt.flags & SA1_LOAD_SUPER))
            mask &= ~(1u << SA1_CHAR_SUPER_SONIC);
    }
    sa1_unload_disc(ctx);
    ctx->error[0] = 0;

    L = (loader *)sa1_mem_alloc(ctx, sizeof *L);
    if (!L)
        return SA1_ERR_OUT_OF_MEMORY;
    L->ctx = ctx;
    L->flags = opt.flags;
    st = sa1_disc_open(ctx, gdi_path, &L->disc);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, L);
        return st;
    }
    first = get_image(L, SA1_FIRST_READ_NAME);
    if (!first || first->load_addr != SA1_FIRST_READ_LOAD) {
        const sa1_file_record *r = ctx->files.count ? &SA1_VEC(ctx->files, sa1_file_record)[0] : NULL;
        st = (r && r->sha1[0] && r->hash_status != SA1_HASH_OK) ? SA1_ERR_BAD_HASH : SA1_ERR_NOT_FOUND;
        sa1_disc_close(&L->disc);
        sa1_mem_free(ctx, L);
        if (st == SA1_ERR_BAD_HASH)
            return sa1_fail(ctx, st, "1ST_READ.BIN SHA-1 %s: not Sonic Adventure US v1.005 (expected %s)",
                            r->sha1, SA1_FIRST_READ_SHA1);
        return sa1_fail(ctx, st, "1ST_READ.BIN not found on the disc");
    }
    for (k = 0; k < SA1_CHAR_COUNT; k++)
        if (mask & (1u << k))
            build_character(L, k, first);

    for (i = 0; i < L->num_images; i++)
        sa1_mem_free(ctx, L->images[i].data);
    sa1_disc_close(&L->disc);
    st = L->partial ? SA1_PARTIAL : SA1_OK;
    sa1_mem_free(ctx, L);
    ctx->disc_loaded = 1;
    if (opt.flags & SA1_LOAD_WORLD_IMAGES) {
        /* the world images of the loaded characters that tick (sa1_character_prepare), here on the loading thread */
        sa1_status ws = sa1_character_prepare(ctx, mask);
        if (SA1_FAILED(ws) && ws != SA1_ERR_NOT_IMPLEMENTED)
            sa1_log(ctx, SA1_LOG_WARN, "SA1_LOAD_WORLD_IMAGES: the world images were not composed (%s); worlds compose "
                    "them when they are created", sa1_status_string(ws));
    }
    sa1_log(ctx, SA1_LOG_INFO, "disc loaded in %.1f ms (%u files read)%s", sa1_now_ms() - t0,
            (unsigned)ctx->files.count, st == SA1_PARTIAL ? ", some optional content missing" : "");
    if (st == SA1_PARTIAL)
        snprintf(ctx->error, sizeof ctx->error, "some optional overlays or archives are missing (see log)");
    return st;
}