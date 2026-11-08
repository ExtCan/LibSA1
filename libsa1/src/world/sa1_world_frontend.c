/*
 * sa1_world_frontend.c - the game's own front end, the world side (API SA1_API_FRONTEND, round 0.9.8; sadc
 * docs/research/gsa1_098_plan.md 6.5 with its Review R98.9, R98.19, charsel098.md 5.2, 3.3, 3.4; the study gsa1_090_charselect.md 2
 * with its review RC2, RC3, RC4, RC10). The context side and the overview: src/sa1_frontend.h, src/sa1_frontend.c.
 *
 * One private world per open, of an image of its own: 1ST_READ + ADVERTISE.PRS (kind SA1_KIND_FRONTEND, sa1_world_image.c's row),
 * composed from the disc with the SHA-1s of the manifest. The code map of ADVERTISE is active in it (sa1g_world_fn_images), and the
 * world's functions are reached through it: a build without game code links. Modelled on sa1wp_create (sa1_world_preview.c): no
 * sa1w_world_create, no player, no collision adapter, no SET layer, no host objects, every hook NULL, an empty landtable, no camera
 * adapter (the game's init 8C900020 starts the heap and the task system again; what the boot made is gone then). Its own step
 * function (w->step), run by sa1g_world_call and not by a tick: the menu runs at 60 Hz with B_8C754EC4 = 1 (8C051760), a tick's
 * pair would write 2 (sa1_world_tick.c), and sa1_world_tick.c needs no patch.
 *
 * The boot goes through sa1g_world_boot, so sa1g_camreq_boot runs: the game's camera pushes are real since the fishing camera
 * landed (they were void stubs in the probe's 300,559 draws; review R98.19: tests/frontend_test.c 1 compares the draws again).
 *
 * The texlists (charsel098.md 5.2; the study's gap): the game's 2D library reads NJS_TEXNAME.texaddr as an NJS_TEXMEMLIST (+0x00
 * global index, +0x20 pixel format, +0x24 width, +0x28 height: code_8C07DEC0.c), and libsa1's texture load is a no-op (8C099750
 * stays void). So the boot writes a block per texture of the nine archives of the screen's own list (D_8C94263C[1]; names and
 * texlists from D_8C942628[language]), in segment 0x18. The global index is the PVMH entry's (flags 0x109), as sa1_pvm_parse reads.
 *
 * One frame (charsel098.md 5.2): the view (diag(1, -1, -1, 1) into XMTRX, slot 0 of the matrix stack while the stack is at depth 0),
 * the pad in Katana bits, 8C9001DA. The capture window lies around it (the context side, the running frame word is reset there).
 *
 * The dialog skip (no SA1_FRONTEND_SHOW_DIALOG; charsel098.md 3.4, a host deviation behind a flag): a new A or START with proc 0, the
 * sound switch D_8C9610D8 = 1 and a slot the game lets you pick: the press is withheld, the frame runs, and after it the two words the
 * game's dialog leaves are written, choice 0 ("Game") and proc 8. The game's own proc 8 does the rest. Measured: frames equal the
 * reference shifted by 42.
 *
 * Game thread only, between phases (libsa1.h). It never touches a character world. No writable process static.
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_frontend.h"

#include <stddef.h>
#include <string.h>

/* ---- the game's addresses (ADVERTISE: sadc src/overlays/advertise; 1ST_READ) ---------------------------------------------- */
#define FW_ADDR_SEQ_END     0x8C90ED84u     /* the screen sequence's next entry: 0 ends it after the first (review RC4) */
#define FW_ADDR_GAME_MODE   0x8C760970u     /* the main loop's mode: 11 */
#define FW_ADDR_MENU_BLOCK  0x8C78B450u     /* B_8C78B450[18]: [0] every character open, [0xD] the selected slot */
#define FW_ADDR_LANGUAGE    0x8C754BFCu     /* Sint32 B_8C754BFC */
#define FW_ADDR_MASTER      0x8C9610CCu     /* task *D_8C9610CC: the master task */
#define FW_ADDR_SELECT      0x8C9613A4u     /* task *D_8C9613A4: the Character Select screen */
#define FW_ADDR_SOUND_OK    0x8C9610D8u     /* Sint32 D_8C9610D8: 1 when the screen's sound has started */
#define FW_ADDR_PVM_TABLE   0x8C942628u     /* ADV_PVM *D_8C942628[language]: { name, texlist } per PVM */
#define FW_ADDR_PVM_SCREEN  0x8C94263Cu     /* Sint32 *D_8C94263C[screen]: the PVMs a screen uses, ended by 26 */
#define FW_PVM_END          26u
#define FW_ADDR_CHARSEL     0x8C9101F8u     /* ADV_CHARSEL D_8C9101F8[]: 16 bytes per slot, +0 the character's main */
#define FW_ADDR_CHRID       0x8C90EC6Au     /* Uint16 D_8C90EC6A[]: the game character per slot */
#define FW_EXEC_QUESTION    0x8C9027A6u     /* the "?" model's task exec (slot 6) */
#define FW_FN_INIT          0x8C9000F4u     /* Sint32 8C9000F4(Sint32 *submode) */
#define FW_FN_EXEC          0x8C9001DAu     /* Sint32 8C9001DA(void): one frame; the frame result, or the master's result */
#define FW_FN_CAN_PICK      0x8C9031CEu     /* Sint32 8C9031CE(Sint32 slot) */
#define FW_FN_SLOTS         0x8C901400u     /* Sint32 8C901400(void): 6 or 7 */
#define FW_ADDR_MTX_DEPTH   0x8C88F69Cu     /* Sint32 B_8C88F69C: the matrix stack's depth (rt_matrix.c) */
#define FW_ADDR_MTX_BASE    0x8C88F5FCu     /* NJS_MATRIX_PTR B_8C88F5FC: the matrix stack's slot 0 (rt_matrix.c) */
#define FW_SELECT_SCREEN    1u              /* screen type 1: Character Select */
/* task and work offsets */
#define FW_TASK_AWP         0x2Cu
#define FW_SEL_PROC         0x1Cu
#define FW_SEL_SLOT         0x20u
#define FW_SEL_TIMER        0x2Cu
#define FW_SEL_CHOICE       0x30u
#define FW_SEL_RING         0x18u           /* round 1.0.0-beta1: Sint32 ring, the highlight phase (code_8C9031C0.c ADV_SELWK) */
#define FW_MASTER_MODE      0x04u
#define FW_MASTER_RESULT    0x14u
/* the NJS_TEXMEMLIST block and NJS_TEXNAME */
#define FW_TEXMEM_SIZE      0x44u
#define FW_TEXNAME_SIZE     12u
#define FW_TEXNAME_ADDR     8u

typedef struct fw_world {
    sa1g_world w;                       /* first: the world the context side holds */
    sa1_context *ctx;
    sa1g_image image;                   /* the world's pristine image (a kind of its own: not the character worlds' cache) */
    int32_t image_ok;
    sa1_frontend_desc d;
    int32_t show_dialog;
    /* the running frame */
    const sa1fw_pad *pad;
    sa1fw_info *info;
} fw_world;

static fw_world *fw_of(struct sa1g_world *w)
{
    return (fw_world *)(void *)w;
}

/* ---- guest memory (the arena, bounded) ----------------------------------------------------------------------------------- */
static int fw_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static uint32_t fw_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (!fw_arena(g) || sa1g_world_rd32(w, g, &v) != 0) {
        return 0;
    }
    return v;
}

static void fw_wr32(sa1g_world *w, uint32_t g, uint32_t v)
{
    if (fw_arena(g)) {
        sa1g_world_wr32(w, g, v);
    }
}

/* the work of a task the guest word `task_word` points at (0: no task or no work) */
static uint32_t fw_work(const sa1g_world *w, uint32_t task_word)
{
    uint32_t tp = fw_rd32(w, task_word);
    return fw_arena(tp) ? fw_rd32(w, tp + FW_TASK_AWP) : 0u;
}

/* ---- one frame ------------------------------------------------------------------------------------------------------------ */
static void fw_load_view(sa1g_world *w)
{
    static const float view[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    uint32_t base, k;
    memcpy(w->xmtrx, view, sizeof view);
    if (fw_rd32(w, FW_ADDR_MTX_DEPTH) == 0) {
        base = fw_rd32(w, FW_ADDR_MTX_BASE);
        if (fw_arena(base) && fw_arena(base + 63u)) {
            for (k = 0; k < 16u; k++) {
                uint32_t b;
                memcpy(&b, &view[k], 4);
                sa1g_world_wr32(w, base + 4u * k, b);
            }
        }
    }
}

static void fw_step(sa1g_world *w, int32_t sub, void *user)
{
    fw_world *fw = (fw_world *)user;
    const sa1fw_pad *p = fw->pad;
    sa1fw_info *inf = fw->info;
    uint32_t on = p->on, awp;
    int skip = 0;
    (void)sub;
    fw_load_view(w);
    if (p->confirm && !fw->show_dialog) {
        awp = fw_work(w, FW_ADDR_SELECT);
        if (fw_arena(awp) && fw_rd32(w, awp + FW_SEL_PROC) == 0u && fw_rd32(w, FW_ADDR_SOUND_OK) == 1u &&
            ((int32_t (*)(int32_t))sa1_fn(SA1_GFN_C(FW_FN_CAN_PICK), 0u))((int32_t)fw_rd32(w, awp + FW_SEL_SLOT)) != 0) {
            /* the press never reaches the game. The game tests A before B, so a B of this frame goes with it */
            skip = 1;
            on &= ~(SA1FW_PAD_A | SA1FW_PAD_START | SA1FW_PAD_B);
        }
    }
    memset(&w->pad[0], 0, sizeof w->pad[0]);
    w->pad[0].connected = 1u;
    w->pad[0].on = on;
    w->pad[0].x1 = p->x1;
    w->pad[0].y1 = p->y1;
    inf->frame_result = ((int32_t (*)(void))sa1_fn(SA1_GFN_C(FW_FN_EXEC), 0u))();
    inf->skipped = 0;
    if (skip) {
        /* the frame saw no press; the two words the game's "Game" leaves. A slide that this frame started (the game tests LEFT and
           RIGHT first) wins */
        awp = fw_work(w, FW_ADDR_SELECT);
        if (fw_arena(awp) && fw_rd32(w, awp + FW_SEL_PROC) == 0u) {
            fw_wr32(w, awp + FW_SEL_CHOICE, 0u);
            fw_wr32(w, awp + FW_SEL_PROC, 8u);
            inf->skipped = 1;
        }
    }
    inf->slots = ((int32_t (*)(void))sa1_fn(SA1_GFN_C(FW_FN_SLOTS), 0u))();
}

static void fw_frame_cb(sa1g_world *w, void *user)
{
    (void)user;
    w->step(w, 0, w->user);
}

void sa1fw_read(const struct sa1g_world *w, sa1fw_info *info)
{
    uint32_t awp = fw_work(w, FW_ADDR_SELECT);
    info->select_alive = fw_arena(awp);
    if (info->select_alive) {
        info->proc = (int32_t)fw_rd32(w, awp + FW_SEL_PROC);
        info->slot = (int32_t)fw_rd32(w, awp + FW_SEL_SLOT);
        info->timer = (int32_t)(int16_t)(fw_rd32(w, awp + FW_SEL_TIMER) & 0xFFFFu);
        info->choice = (int32_t)fw_rd32(w, awp + FW_SEL_CHOICE);
        info->ring = (int32_t)fw_rd32(w, awp + FW_SEL_RING);
    }
    awp = fw_work(w, FW_ADDR_MASTER);
    info->master_alive = fw_arena(awp);
    if (info->master_alive) {
        info->mode = (int32_t)fw_rd32(w, awp + FW_MASTER_MODE);
        info->result = (int32_t)fw_rd32(w, awp + FW_MASTER_RESULT);
    }
}

sa1_status sa1fw_frame(struct sa1g_world *w, const sa1fw_pad *pad, sa1fw_info *info)
{
    fw_world *fw = fw_of(w);
    sa1_status st;
    fw->pad = pad;
    fw->info = info;
    st = sa1g_world_call(w, fw_frame_cb, NULL);
    fw->pad = NULL;
    fw->info = NULL;
    if (st == SA1_OK) {
        sa1fw_read(w, info);
    }
    return st;
}

uint32_t sa1fw_poll(struct sa1g_world *w, sa1_event *out, uint32_t cap)
{
    return sa1g_world_poll_events(w, out, cap);
}

const char *sa1fw_fault(const struct sa1g_world *w)
{
    return w != NULL && w->dead ? w->fault.msg : "";
}

uint32_t sa1fw_slot_exec(const struct sa1g_world *w, int32_t slot)
{
    if (slot == 6) {
        return FW_EXEC_QUESTION;
    }
    return slot >= 0 && slot < 6 ? fw_rd32(w, FW_ADDR_CHARSEL + 16u * (uint32_t)slot) : 0u;
}

int32_t sa1fw_slot_kind(const struct sa1g_world *w, int32_t slot)
{
    const uint8_t *p;
    if (slot < 0 || slot >= 6) {
        return SA1_KIND_FRONTEND;
    }
    p = (const uint8_t *)sa1g_world_ptr(w, FW_ADDR_CHRID + 2u * (uint32_t)slot);
    return p != NULL ? (int32_t)sa1_rd16(p) : SA1_KIND_FRONTEND;
}

void sa1fw_reset_cap_frame(struct sa1g_world *w, uint32_t block)
{
    uint32_t none = 0xFFFFFFFFu;
    uint8_t *h = block != 0 ? (uint8_t *)sa1g_world_ptr(w, block) : NULL;   /* segment 0x18: sa1g_world_wr32 is for the arena */
    if (h != NULL) {
        memcpy(h + offsetof(sa1_cap_hdr, frame), &none, 4);
    }
}

/* ---- the texlists -------------------------------------------------------------------------------------------------------- */
/* the screen's list from the game's data */
static sa1_status fw_list(const sa1g_world *w, int32_t language, sa1fe_archive *list, uint32_t *n)
{
    uint32_t table = fw_rd32(w, FW_ADDR_PVM_TABLE + 4u * (uint32_t)language);
    uint32_t ids = fw_rd32(w, FW_ADDR_PVM_SCREEN + 4u * FW_SELECT_SCREEN);
    uint32_t c = 0, id;
    *n = 0;
    if (!fw_arena(table) || !fw_arena(ids)) {
        return SA1_ERR_BAD_FORMAT;
    }
    while ((id = fw_rd32(w, ids + 4u * c)) != FW_PVM_END) {
        uint32_t name = fw_rd32(w, table + 8u * id), k;
        const char *s;
        if (id > FW_PVM_END || c >= SA1_MAX_ARCHIVES || !fw_arena(name)) {
            return SA1_ERR_BAD_FORMAT;
        }
        s = (const char *)sa1g_world_ptr(w, name);
        for (k = 0; s != NULL && k + 1u < sizeof list[c].name && s[k] != 0; k++) {
            char ch = s[k];
            list[c].name[k] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 'a' + 'A') : ch;
        }
        list[c].name[k] = 0;
        list[c].texlist = fw_rd32(w, table + 8u * id + 4u);
        c++;
    }
    *n = c;
    return c != 0 ? SA1_OK : SA1_ERR_BAD_FORMAT;
}

static void fw_fill_cb(sa1g_world *w, void *user)
{
    const sa1_charset *cs = (const sa1_charset *)user;
    uint32_t a, i;
    for (a = 0; a < cs->num_archives; a++) {
        const sa1_archive *ar = &cs->archives[a];
        uint32_t tex = fw_rd32(w, ar->texlist_addr), count = fw_rd32(w, ar->texlist_addr + 4u);
        if (!fw_arena(tex)) {
            sa1g_fatal(0, "front end: an archive's texlist has no texture array");
        }
        for (i = 0; i < ar->num_textures && i < count; i++) {
            const sa1_texture *t = &ar->tex[i];
            uint8_t *m = NULL;
            uint32_t g = sa1g_world_ext_alloc(w, FW_TEXMEM_SIZE, 4, (void **)&m), pix = (uint32_t)t->pixel_format << 27;
            if (g == 0 || m == NULL) {
                sa1g_fatal(0, "front end: no room for the texture blocks in segment 0x18");
            }
            memset(m, 0, FW_TEXMEM_SIZE);
            memcpy(m + 0x00, &t->gbix, 4);
            memcpy(m + 0x20, &pix, 4);
            memcpy(m + 0x24, &t->width, 4);
            memcpy(m + 0x28, &t->height, 4);
            fw_wr32(w, tex + FW_TEXNAME_SIZE * i + FW_TEXNAME_ADDR, g);
        }
    }
}

/* ---- the game's init ------------------------------------------------------------------------------------------------------ */
/* the game's flag per slot: 8C90117C (slot 4 Big 0x46, slot 5 E-102 0x45) */
static const uint8_t k_fw_slot_flag[SA1_FRONTEND_SLOTS] = { 0x41u, 0x42u, 0x43u, 0x44u, 0x46u, 0x45u, 0x47u };

static void fw_init_cb(sa1g_world *w, void *user)
{
    fw_world *fw = (fw_world *)user;
    int32_t *sub = NULL;
    uint32_t g = sa1g_world_ext_alloc(w, 4, 4, (void **)&sub), k;
    uint8_t *block = (uint8_t *)sa1g_world_ptr(w, FW_ADDR_MENU_BLOCK);
    sa1_event ev[16];
    if (g == 0 || sub == NULL || block == NULL) {
        sa1g_fatal(0, "front end: no room for the init's argument");
    }
    /* review RC4: the game's own direct entry (submode 1), the sequence ended after it */
    fw_wr32(w, FW_ADDR_SEQ_END, 0u);
    fw_wr32(w, FW_ADDR_GAME_MODE, 11u);
    *sub = 1;
    ((int32_t (*)(void *))sa1_fn(SA1_GFN_C(FW_FN_INIT), 0u))(sa1_decode(g));
    /* who may be picked */
    if (fw->d.open_slots == 0xFFFFFFFFu) {
        block[0] = 1;
    } else {
        for (k = 0; k < SA1_FRONTEND_SLOTS; k++) {
            if (fw->d.open_slots & (1u << k)) {
                *(uint8_t *)sa1g_world_ptr(w, SA1G_ADDR_EVENT_FLAGS + k_fw_slot_flag[k]) = 1;
            }
        }
    }
    fw_wr32(w, FW_ADDR_LANGUAGE, (uint32_t)fw->d.language);
    block[0xD] = (uint8_t)fw->d.slot;
    /* what the init said (its music stop) is not the screen's */
    while (sa1g_world_poll_events(w, ev, 16u) == 16u) {
    }
}

/* ---- the world ------------------------------------------------------------------------------------------------------------ */
static int fw_has_code(const sa1g_world *w)
{
    uint32_t k;
    for (k = 0; k < w->num_fn_images; k++) {
        if (w->fn_images[k] != NULL && strcmp(w->fn_images[k]->name, "ADVERTISE") == 0) {
            return 1;
        }
    }
    return 0;
}

void sa1fw_destroy(sa1_context *ctx, struct sa1g_world *w)
{
    fw_world *fw;
    if (w == NULL) {
        return;
    }
    fw = fw_of(w);
    sa1g_world_destroy(w);
    if (fw->image_ok) {
        sa1g_image_free(&fw->image);
    }
    sa1_mem_free(ctx, fw);
}

sa1_status sa1fw_create(sa1_context *ctx, const sa1_frontend_desc *d, struct sa1g_world **out)
{
    struct sa1w_ctx *wc = NULL;
    sa1g_wctx *gc;
    fw_world *fw;
    sa1g_boot_cfg cfg;
    sa1fe_archive list[SA1_MAX_ARCHIVES];
    uint32_t n = 0;
    sa1_status st;
    *out = NULL;
    if (!sa1g_game_ready()) {
        return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "the front end needs the lowered game code (not in this build)");
    }
    st = sa1w_ctx_get(ctx, &wc);
    if (st != SA1_OK || wc == NULL) {
        return sa1_fail(ctx, st != SA1_OK ? st : SA1_ERR_NOT_LOADED, "the front end: no world state for this context (%s)",
                        sa1g_status_string(st));
    }
    gc = sa1w_ctx_wc(wc);
    fw = (fw_world *)sa1_mem_alloc(ctx, sizeof *fw);
    if (fw == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    fw->ctx = ctx;
    fw->d = *d;
    fw->show_dialog = (d->flags & SA1_FRONTEND_SHOW_DIALOG) != 0;
    st = sa1g_image_compose(ctx, gc->gdi, SA1_KIND_FRONTEND, 0, &fw->image);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, fw);
        return sa1_fail(ctx, st, "the front end: the world image: %s", sa1g_status_string(st));
    }
    fw->image_ok = 1;
    if (sa1g_world_create(&fw->w, &gc->shared) != 0) {
        sa1g_image_free(&fw->image);
        sa1_mem_free(ctx, fw);
        return sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "the front end: cannot reserve its 16 MB arena");
    }
    fw->w.fresh = 1;                    /* nothing wrote the arena yet: apply_image need not zero it */
    st = sa1g_world_apply_image(&fw->w, &fw->image);
    if (st == SA1_OK && !fw_has_code(&fw->w)) {
        st = sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "the front end: this build has no code map of ADVERTISE");
    }
    if (st == SA1_OK) {
        fw->w.kind = -1;
        sa1g_boot_defaults(&cfg);
        cfg.chr = 0;
        cfg.level = 1;
        cfg.act = 0;
        cfg.flags = SA1G_BOOT_NO_PLAYER | SA1G_BOOT_NO_CAMERA;
        cfg.land_mode = SA1G_LAND_EMPTY;
        st = sa1g_world_boot(&fw->w, &cfg);
    }
    if (st == SA1_OK) {
        fw->w.step = fw_step;
        fw->w.user = fw;
        st = fw_list(&fw->w, d->language, list, &n);
        if (st != SA1_OK) {
            st = sa1_fail(ctx, st, "the front end: the game's list of the screen's archives cannot be read");
        }
    }
    if (st == SA1_OK) {
        st = sa1_loader_frontend_set(ctx, gc->gdi, list, n);
    }
    if (st == SA1_OK) {
        st = sa1g_world_call(&fw->w, fw_fill_cb, &ctx->frontend_set);
    }
    if (st == SA1_OK) {
        st = sa1g_world_call(&fw->w, fw_init_cb, fw);
    }
    if (st != SA1_OK) {
        sa1_fail(ctx, st, "the front end failed: %s", fw->w.dead ? fw->w.fault.msg : sa1g_status_string(st));
        sa1fw_destroy(ctx, &fw->w);
        return st;
    }
    *out = &fw->w;
    return SA1_OK;
}
