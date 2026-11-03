/*
 * sa1_question.c - the question mark of Character Select as a character (SA1_API_QUESTION, round 0.9.9; sadc docs/research/gsa1_099_plan.md 5.3, playable099.md 5.1-5.5).
 *
 * The model lies in the menu's overlay ADVERTISE, which shares its physical slot with B_CHAOS7, so it can never enter a Super Sonic world: it is read from the disc into host memory and
 * appended to the character's render output after the capture's filter. The object 8C9412C0's model 8C941298: 350 points, one material (8C93DF5C: texture 7, CLAMP_U, CLAMP_V, USE_ENV), one
 * meshset of 37 strips, 692 triangles. The menu's exec 8C9027A6 turns it 0x2D8 BAMS a frame and draws it with njRotateY(-ang.y) (sadc src/overlays/advertise/code_8C902700.c:121-135).
 */
#include "sa1_question.h"
#include "sa1_api_world.h"

#include <math.h>
#include <string.h>

#define SA1Q_OVERLAY    0x0C900000u     /* the overlay's load address (sa1_manifest.c's row) */
#define SA1Q_FIRST      0x8C93DF5Cu     /* the material: the first byte the model reaches */
#define SA1Q_END        0x8C9412F4u     /* the end of the object 8C9412C0 */
#define SA1Q_MODEL      0x8C941298u
#define SA1Q_TEXLIST    0x8C941508u     /* D_8C941508: the menu's texlist of AVA_CHSEL(_E) */
#define SA1Q_FOOT       5.97011661529541 /* -(the model's lowest y), SA1 units: its foot stands on the ground */

typedef struct sa1q_state {
    sa1_charset set;        /* archives[0]: AVA_CHSEL_E */
    uint8_t *model;         /* the bytes SA1Q_FIRST .. SA1Q_END of the overlay */
    uint32_t size;
} sa1q_state;

sa1_status sa1q_load(sa1_context *ctx)
{
    const sa1_api_ext *x = sa1_api_ext_of(ctx);
    sa1q_state *q;
    uint8_t *overlay = NULL;
    uint32_t overlay_size = 0;
    const uint32_t first = (SA1Q_FIRST & 0x1FFFFFFFu) - SA1Q_OVERLAY, size = SA1Q_END - SA1Q_FIRST;
    sa1_status st;
    if (ctx->question != NULL) {
        return SA1_OK;
    }
    if (!ctx->disc_loaded || x == NULL || !x->gdi[0]) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_QUESTION: no disc is loaded");
    }
    q = (sa1q_state *)sa1_mem_alloc(ctx, sizeof *q);
    if (q == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    st = sa1_loader_question(ctx, x->gdi, &q->set, &overlay, &overlay_size);
    if (st == SA1_OK && (overlay_size < first || overlay_size - first < size)) {
        st = sa1_fail(ctx, SA1_ERR_NOT_LOADED, "SA1_CREATE_QUESTION: ADVERTISE.PRS is %u bytes, the model ends at %u", (unsigned)overlay_size, (unsigned)(first + size));
    }
    if (st == SA1_OK) {
        q->model = (uint8_t *)sa1_mem_alloc(ctx, size);
        if (q->model == NULL) {
            st = SA1_ERR_OUT_OF_MEMORY;
        }
    }
    if (st == SA1_OK) {
        memcpy(q->model, overlay + first, size);
        q->size = size;
    }
    sa1_mem_free(ctx, overlay);
    if (st != SA1_OK) {
        sa1_charset_free(ctx, &q->set);
        sa1_mem_free(ctx, q->model);
        sa1_mem_free(ctx, q);
        return st;
    }
    ctx->question = q;
    return SA1_OK;
}

void sa1q_free(sa1_context *ctx)
{
    sa1q_state *q = ctx->question;
    if (q != NULL) {
        ctx->question = NULL;
        sa1_charset_free(ctx, &q->set);
        sa1_mem_free(ctx, q->model);
        sa1_mem_free(ctx, q);
    }
}

const sa1_archive *sa1q_archive(const sa1_context *ctx)
{
    const sa1q_state *q = ctx->question;
    return q != NULL ? &q->set.archives[0] : NULL;
}

static const uint8_t *q_read(void *user, uint32_t g, uint32_t n)
{
    const sa1q_state *q = (const sa1q_state *)user;
    uint32_t off = (g & 0x1FFFFFFFu) - (SA1Q_FIRST & 0x1FFFFFFFu);
    if (off > q->size || n > q->size - off) {
        return NULL;
    }
    return q->model + off;
}

static int32_t q_resolve(void *user, uint32_t texlist, uint32_t id, int32_t *archive)
{
    (void)user;
    (void)texlist;
    *archive = (int32_t)SA1_ARCHIVE_QUESTION;
    return (int32_t)id;
}

/* the disc's textures of this archive have no alpha the draw's classification would use: the menu's own draw says opaque */
static uint32_t q_tex_alpha(void *user, int32_t archive, int32_t index)
{
    (void)user;
    (void)archive;
    (void)index;
    return SA1_TEXALPHA_NONE;
}

sa1_status sa1q_append(sa1_context *ctx, sa1_meshbuf *b, const float root[3], uint32_t ang)
{
    sa1q_state *q = ctx->question;
    sa1_emit_info ei;
    sa1_draw_state ds;
    double m[12], th, c, s;
    if (q == NULL) {
        return SA1_ERR_NOT_LOADED;
    }
    memset(&ei, 0, sizeof ei);
    memset(&ds, 0, sizeof ds);
    ds.hand = 1;            /* the character renderer 8C037098's rule, as func_8C036F12 draws it on the menu */
    ds.cmat[0] = ds.cmat[1] = ds.cmat[2] = ds.cmat[3] = 1.0f;
    ei.reader = q_read;
    ei.user = q;
    ei.ds = &ds;
    ei.ctx = ctx;
    ei.resolve = q_resolve;
    ei.texlist = SA1Q_TEXLIST;
    ei.textured = 1;
    ei.source = SA1_SOURCE_CHARACTER;
    ei.tex_alpha = q_tex_alpha;
    /* njRotateY(-ang), as the menu's exec: x' = c x - s z, z' = s x + c z. tests/question_test.c Q4 holds it against sa1_frontend_render */
    th = (double)(ang & 0xFFFFu) * (3.14159265358979323846 / 32768.0);
    c = cos(th);
    s = sin(th);
    m[0] = c;
    m[1] = 0.0;
    m[2] = -s;
    m[3] = (double)root[0];
    m[4] = 0.0;
    m[5] = 1.0;
    m[6] = 0.0;
    m[7] = (double)root[1] + SA1Q_FOOT;
    m[8] = s;
    m[9] = 0.0;
    m[10] = c;
    m[11] = (double)root[2];
    return sa1_pose_emit_model_ex(ctx, b, &ei, SA1Q_MODEL, m);
}

sa1_status sa1q_set_spin(sa1_context *ctx, sa1_character_id id, int32_t value)
{
    int32_t *still = sa1q_char_still(ctx, id);
    if (still == NULL) {
        return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "SA1_COPT_QUESTION_SPIN: only a character made with SA1_CREATE_QUESTION");
    }
    *still = value ? 0 : 1;
    return SA1_OK;
}
