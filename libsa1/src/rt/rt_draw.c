/*
 * rt_draw.c - Ninja draw state and draw entry points (docs/libsa1_integration.md
 * 4.2, 2.9, 7.2).
 *
 * Clean-room: written from the observed behaviour of the SDK functions,
 * verified bit-exact against them in the harness where they are comparable
 * (README.md).
 *
 * 1. Draw-state stores.  Game code reads and writes the SDK's draw state
 *    directly (B_8C88F62C, B_8C88F660/664 in code_8C056860.c, code_8C05697C.c,
 *    code_8C0BA820.c; the asm renderers read B_8C88F668, B_8C890104 ...), so
 *    the runtime stores exactly the words the original stores, at the SDK's
 *    own addresses (per world in the arena):
 *      B_8C88F62C          control-3D flags (8C60EDE0)
 *      B_8C88F660 / 664    constant attribute AND / OR masks (njSetConstantAttr)
 *      B_8C88F668[4]       constant material ARGB, float bits (njSetConstantMaterial)
 *      B_8C88FCB4          -> the current Kamui vertex context (0xC0 bytes, one
 *                          of B_8C88F834[]); Ninja writes its update mask (+0x00),
 *                          blending modes (+0x2C / +0x30), the +0x3C switch and
 *                          the texture words (+0x90..+0x9C)
 *      B_8C88FCD4          -> the current material words [0..4]
 *      B_8C890104[9]       the draw-state save area (8C605DAC / 8C605E0A)
 *    A NULL context / material pointer reads as zeros and drops stores: the
 *    two pointers stay 0 until the SDK's Kamui init selects a context
 *    (8C604FD0: &B_8C88F834[n]), which the reference simulator's game init
 *    never does; the original then reads the DC's boot ROM area (the harness's
 *    NULL page: zeros) and its stores there are lost.  libsa1's arena starts
 *    with both 0 as well, so the world boot needs no Kamui context and the
 *    runtime behaves as in the reference traces.  (The harness treats the
 *    whole page 0x0000-0xFFFF that way; the runtime models the NULL pointer,
 *    the only value these pointers have outside random diff inputs.)
 *    Stores follow the original's order, but a context or material pointer
 *    that makes those words overlap the save area B_8C890104 is not
 *    reproduced store for store (the SDK's layout never does that, and C may
 *    assume distinct objects do not overlap).
 *
 * 2. Kamui.  After a context change Ninja calls Kamui (8C654E60: render state
 *    -> ISP/TSP/TCW words; 8C655620: context -> global parameter words), which
 *    only feeds the PowerVR.  libsa1 has no Kamui: RT_KM_* do nothing.  The
 *    harness still has the original Kamui code in the image and runs it, so
 *    diff and the routed traces see exactly the original's RAM.
 *
 * 3. Draws are no-ops, apart from the state the original leaves behind for
 *    game code: the sprite draws select the sprite's texlist with
 *    njSetTexture (D_8C673480 stays set, as after the original); njPrintColor
 *    is a plain store.  Not reproduced, because only the SDK's renderer and
 *    the game's hand-written asm renderers (replaced by capture in libsa1)
 *    read it: the texture-key cache D_8C67348C / D_8C673490 (sprites: the
 *    sprite texture's key; prints: -2), the draw-state save / vertex scratch
 *    B_8C890104-B_8C89012C, the print state B_8C89059C, the context's
 *    texture words and the material words the draws rewrite through
 *    8C605E0A, and every TA / store-queue write.  XMTRX is not changed by
 *    the originals either (the 3D draws only read it with FTRV / njCalcPoint).
 *    Measured in the harness with plausible sprites / point lists: these are
 *    the only words that differ (README.md, rt_draw section).
 *
 * 4. DRAW CAPTURE (design 7.2; libsa1 profile only, section 5 below).  Every
 *    draw entry point calls RT_DRAW_CAPTURE(kind, object, n, attr) first (v2:
 *    sprite and point-list draws also snapshot their data into the pair's
 *    payload pool, the game reuses those buffers; njColorBlendingMode's modes
 *    are kept for the records; the game's own 3D sprite draw 8C09E778 is
 *    defined here, recording only; v3: model draws snapshot the model's
 *    materials, points and normals as the draw read them, rt_cap_model).  In
 *    the libsa1 profile it records the draw into the world's capture buffer
 *    while the host holds a capture window open; section 5 also defines the
 *    model draws the game calls (njDrawModel, njDrawObject, 8C6215D6,
 *    8C60E080, 8C60E49A, 8C60E49A's node draw 8C621574 (round 0.9.2a: the
 *    power-up models), the hand-written renderers 8C03700C / 8C037098 with
 *    their early return and the palette bank 8C038CEC), which did nothing as
 *    boundary stubs and still do exactly that outside a capture window:
 *    capture only adds recording.  The harness profile (TARGET_PC_HARNESS)
 *    has none of it: the interpreter keeps the original draws and
 *    RT_DRAW_CAPTURE stays empty.
 *
 * 6. The sphere visibility test 8C038D00 (both profiles; section 6 below)
 *    is the original's arithmetic on the world's screen state and XMTRX:
 *    game logic.  A capture window does not change its answer (until
 *    2026-10-20 it answered "visible" inside one and 0 outside, the zero
 *    screen of the reference simulator then); the libsa1 profile counts the
 *    pair's answers, so the host can tell a display the game's camera did
 *    not see (sa1_capture.h: it then draws the character all the same).
 */
#include "rt.h"
#include "task.h"

extern Uint32 B_8C88F62C;              /* control-3D flags (bit 0x4000 picks the material word order below) */
extern Uint32 B_8C88F660;               /* constant attribute AND mask */
extern Uint32 B_8C88F664;               /* constant attribute OR mask */
extern Uint32 B_8C88F668[4];            /* constant material a, r, g, b (float bits) */
extern Uint32 *B_8C88FCB4;              /* current Kamui vertex context */
extern Uint32 *B_8C88FCD4;              /* current material words */
extern Uint32 B_8C890104[9];            /* draw-state save area */
extern Uint32 B_8C89055C;               /* debug print colour */
extern Uint32 B_8C88F7D0;               /* vsync: wait flag */
extern Uint32 B_8C88F7D8;               /* vsync: vblank seen */
extern void (*D_8C673478)(void);        /* vsync user callback */

/* Kamui vertex context words written by Ninja */
#define RT_KC_UPDATE    (0x00 / 4)      /* which render state changed: 0x40 src blend, 0x80 dst blend, 0x100 +0x3C */
#define RT_KC_SRCBLEND  (0x2C / 4)
#define RT_KC_DSTBLEND  (0x30 / 4)
#define RT_KC_SWITCH    (0x3C / 4)      /* set by 8C63FAE8 (2) / 8C63FB18 (0) */
#define RT_KC_TEX0      (0x90 / 4)      /* +0x90..+0x9C: texture words (saved / restored) */

/* a word of the context / material, NULL = zeros / dropped (see 1. above) */
#define RT_LD(p, i)    ((p) != NULL ? (p)[i] : 0u)
#define RT_ST(p, i, v) do { Uint32 *rt_p_ = (p); if (rt_p_ != NULL) rt_p_[i] = (v); } while (0)

#if defined(TARGET_PC_HARNESS)
extern void func_8C654E60(Uint32 *ctx);
extern void func_8C655620(Uint32 *ctx);
#define RT_KM_STATE(ctx)   func_8C654E60(ctx)
#define RT_KM_CONTEXT(ctx) func_8C655620(ctx)
#else
#define RT_KM_STATE(ctx)   ((void)0)
#define RT_KM_CONTEXT(ctx) ((void)0)
#endif

#define RT_CAP_SPRITE2D  1
#define RT_CAP_SPRITE3D  2
#define RT_CAP_LINE3D    3
#define RT_CAP_TRI3D     4
#define RT_CAP_POLY3D    5
#define RT_CAP_PRINT     6

#if !defined(TARGET_PC_HARNESS)
/* ------------------------------------------------------------------ */
/* 5a. capture state (design 7.2): one buffer per world               */
/*                                                                     */
/* rt_cap / rt_cap_rec are R4 pcdata: every world has its own copy in  */
/* segment 0x18, initialised at world creation (pclower).  The host    */
/* (libsa1/src/sa1_capture.c, its mirror of these layouts) finds the   */
/* header by its magic in the world's pcdata, sets `on` around a tick  */
/* (sa1_character_tick) and reads the records afterwards.  A record    */
/* holds what the draw used: the model / object (guest address),       */
/* XMTRX, slot 0 of the matrix stack (the matrix the outermost push of */
/* the drawing code saved: the display's base, so the host can take it */
/* out and keep the model -> world part), the texlist, the control-3D  */
/* flags, the constant attribute masks and material, the palette bank, */
/* the sub step, the frame counter and (v5, round 0.9.4) the running   */
/* exec D_8C1C1D40: who drew.  The records of one pair share           */
/* the frame counter (B_8C752BE8, +2 after each pair, 8C08A5BE): the   */
/* first draw of a new frame value restarts the buffer (the host also */
/* restarts it when it opens a window), so it holds the last pair that */
/* drew (both of its steps).                                           */
extern Sint32 B_8C754EC8;               /* logic sub step 0 / 1: the hand-written renderers draw on 0 only */
extern Sint32 B_8C752BE8;               /* frame counter: one capture per pair */
extern NJS_TEXLIST *D_8C673480;         /* current texlist (rt_texture.c) */
extern NJS_MATRIX_PTR B_8C88F5FC;       /* matrix stack base (rt_matrix.c): slot 0 */
extern TaskFuncPtr D_8C1C1D40;          /* the exec the task executor runs (8C09878C; 0 outside it): 2D payloads */

#define RT_CAP_RENDER    7      /* 8C037098: hand-written basic-model renderer (sub 0 only) */
#define RT_CAP_RENDER2   8      /* 8C03700C: the same inside a draw-state save / restore */
#define RT_CAP_NJMODEL   9      /* njDrawModel */
#define RT_CAP_NJOBJECT 10      /* a node model of njDrawObject */
#define RT_CAP_NJMOTION 11      /* a node model of the Ninja motion draws 8C60E080 / 8C60E49A, or of a direct call of
                                   8C60E49A's node draw 8C621574 (round 0.9.2a FX3: the power-up models) */
#define RT_CAP_DEBUG    12      /* 8C6215D6: collision / debug display object (recorded, not walked) */
#define RT_CAP_POLY2D   13      /* round 0.9.1 (HUD): njDrawPolygon2D 8C63EBC8 (rt_cap_poly2d) */
#define RT_CAP_KINDS    16

/* round 0.9.1 (the HUD: sadc docs/research/gsa1_070_hud.md F2 with its Review R.5.3): the 2D draws keep what the 3D
 * ones did not need. The record layout (0xD0) and version 4 are unchanged; only these payloads are longer (a reader that
 * asks for the first 0x34 bytes of a SPRITE2D payload still gets them: sa1_capture_payload checks pay_len >= n).
 *   SPRITE2D (njDrawSprite2D): +0x00 the NJS_SPRITE (0x20), +0x20 its NJS_TEXANIM cell n (0x14), +0x34 pri (float
 *            bits: Ninja's 2D depth, -1 front .. more negative behind), +0x38 the running exec D_8C1C1D40 (raw guest
 *            word: the DC address of the task exec the executor runs, 0 outside it), +0x3C D_8C673480 before the draw
 *            (the draw sets it to sp->tlist; the record's texlist is sp->tlist)
 *   POLY2D   (njDrawPolygon2D): +0x00 u32 points kept (n, at most RT_CAP_MAXPTS), +0x04 u32 has colours, +0x08 u32 has
 *            uvs, +0x0C u32 p->num, +0x10 pri, +0x14 exec, +0x18 D_8C673480 at the draw, +0x1C the points (x, y: 8
 *            bytes each), then the NJS_COLORs (4 bytes each), then the NJS_TEXs (4 bytes each) */
#define RT_CAP_SPRITE2D_PAY 0x40u
#define RT_CAP_POLY2D_HEAD  0x1Cu

#define RT_CAP_MAGIC0   0x31414153u     /* "SAA1" */
#define RT_CAP_MAGIC1   0x50414352u     /* "RCAP" */
#define RT_CAP_MAGIC2   0x7E3C9A5Du
#define RT_CAP_MAGIC3   0xC0DE7202u
/* v5 (round 0.9.4, sadc docs/research/gsa1_094_plan.md 6.2 task 1, gsa1_091b_visuals.md 4.2 R-F4): every record carries
 * the running exec D_8C1C1D40 (RT_CAP_REC.exec, record 0xD4): who drew it */
#define RT_CAP_VERSION  5u
#define RT_CAP_MAX      2048u           /* records per pair (Sonic draws 30-40). 512 until round 0.9.4b; then 2,048:
                                           objects2_plan.md 6.1 task 2 / review R.4 - set_test r5, the window maxima
                                           200 / 100 in view with blob shadows, a hurt scatter and a dig fountain,
                                           dropped 101 records and the HUD at 1,024 */
#define RT_CAP_PAY      0x40000u        /* payload bytes per pair: snapshots of sprites / point lists (v2), models (v3) */
#define RT_CAP_MSNAP_MAXPTS  8192u      /* v3: points of a model snapshot (a larger model is read live by the host) */
#define RT_CAP_MSNAP_MAXMATS 1024u      /* v3: materials of a model snapshot */
#define RT_CAP_MSNAP_NORMALS 0x1u       /* v3: model snapshot flag: the normals follow the points */
#define RT_CAP_MAXPTS   512u            /* points of one line / polygon draw kept in the payload */
#define RT_CAP_NOPAY    0xFFFFFFFFu     /* RT_CAP_REC.pay of a record without payload */
#define RT_CAPF_GAME_SPRITE 0x1u        /* RT_CAP_REC.flags: drawn by the game's own 3D sprite draw 8C09E778 */
#define RT_CAPF_PAY_LOST    0x2u        /* the payload did not fit (RT_CAP_PAY) */
#define RT_CAPF_NJ_ACTION   0x4u        /* made inside Ninja's action draw 8C60E49A (round 4c F2(a)): the original draws
                                           these nodes with 8C60E31C -> 8C621574 -> 8C6209A8 (Ninja's material colour
                                           rules), not with 8C60E080's njDrawModel 8C6108B0; also every record of a
                                           direct call of that node draw 8C621574 (round 0.9.2a FX3) */

typedef struct RT_CAP_REC {
    Uint32 kind;                /* 0x00 RT_CAP_* */
    Sint32 sub;                 /* 0x04 B_8C754EC8 */
    Sint32 frame;               /* 0x08 B_8C752BE8 */
    Sint32 depth;               /* 0x0C matrix stack depth B_8C88F69C */
    const void *obj;            /* 0x10 NJS_MODEL * (model draws), NJS_OBJECT * (debug), sprite / points */
    NJS_TEXLIST *texlist;       /* 0x14 D_8C673480 */
    Uint32 ctl3d;               /* 0x18 B_8C88F62C */
    Uint32 attr_and;            /* 0x1C B_8C88F660 */
    Uint32 attr_or;             /* 0x20 B_8C88F664 */
    Uint32 cmat[4];             /* 0x24 B_8C88F668 (float bits a, r, g, b) */
    Uint32 n;                   /* 0x34 count argument of sprite / line / polygon draws */
    Uint32 attr;                /* 0x38 attribute argument of those */
    Sint32 bank;                /* 0x3C palette bank (8C038CEC) */
    Float m[16];                /* 0x40 XMTRX */
    Float base[16];             /* 0x80 slot 0 of the matrix stack */
    Uint32 blend;               /* 0xC0 v2: njColorBlendingMode at the draw: source mode | destination mode << 8 */
    Uint32 pay;                 /* 0xC4 v2: offset of the draw's payload in rt_cap_pay (RT_CAP_NOPAY: none) */
    Uint32 pay_len;             /* 0xC8 v2: payload bytes */
    Uint32 flags;               /* 0xCC v2: RT_CAPF_* */
    Uint32 exec;                /* 0xD0 v5: the running exec D_8C1C1D40, the raw guest word (the DC address of the task
                                   exec the executor 8C09878C runs; 0 outside it: the display walker, the HUD pass).
                                   Model payloads are shared between records of one model (rt_cap_model), so the
                                   record carries it (sa1_capture.c: the OBJECT draws of round 0.9.4) */
} RT_CAP_REC;                   /* 0xD4 */

typedef struct RT_CAP_HDR {
    Uint32 magic[4];            /* 0x00 RT_CAP_MAGIC0..3 */
    Uint32 version;             /* 0x10 */
    Uint32 hdr_size;            /* 0x14 sizeof(RT_CAP_HDR) */
    Uint32 rec_size;            /* 0x18 sizeof(RT_CAP_REC) */
    Uint32 capacity;            /* 0x1C RT_CAP_MAX */
    RT_CAP_REC *rec;            /* 0x20 the records */
    Uint32 on;                  /* 0x24 host: 1 while a capture window is open */
    Uint32 frame;               /* 0x28 frame counter of the records */
    Uint32 count;               /* 0x2C records of that frame */
    Uint32 dropped;             /* 0x30 records of that frame lost (buffer full) */
    Uint32 total;               /* 0x34 records over the world's life */
    Sint32 bank;                /* 0x38 current palette bank (8C038CEC) */
    Uint32 ninja;               /* 0x3C nesting of the Ninja motion draws (8C037098 then draws on every sub step) */
    Uint32 vis_calls;           /* 0x40 visibility tests (8C038D00) inside capture windows (both sub steps) */
    Uint32 calls[RT_CAP_KINDS]; /* 0x44 draws per kind inside capture windows */
    Uint32 blend;               /* 0x84 v2: the current njColorBlendingMode (source | destination << 8), kept
                                   whether a window is open or not (the context store is dropped, see 1.) */
    Uint8 *pay;                 /* 0x88 v2: the payload pool of the current pair */
    Uint32 pay_cap;             /* 0x8C v2: RT_CAP_PAY */
    Uint32 pay_used;            /* 0x90 v2: bytes used (the host clears it with count when it opens a window) */
    Uint32 vis_frame;           /* 0x94 v4: frame counter (B_8C752BE8) of the pair vis_shown / vis_hidden count */
    Uint32 vis_shown;           /* 0x98 v4: visibility tests of that pair's sub step 0 inside capture windows that
                                   answered "visible" (section 6) */
    Uint32 vis_hidden;          /* 0x9C v4: ... that answered "not visible": a display the game's camera does not see
                                   draws nothing, the host shows the character all the same */
} RT_CAP_HDR;                   /* 0xA0 */

SA1_ASSERT_SIZE(RT_CAP_REC, 0xD4);
SA1_ASSERT_SIZE(RT_CAP_HDR, 0xA0);

static RT_CAP_REC rt_cap_rec[RT_CAP_MAX];
static Uint8 rt_cap_pay[RT_CAP_PAY];
/* nesting of 8C60E49A (Ninja's action draw): its records carry RT_CAPF_NJ_ACTION. Not in RT_CAP_HDR, whose layout the
 * host mirrors; recording only, no game-visible word */
static Uint32 rt_cap_action;
/* blend 1 | 1 << 8: NJD_COLOR_BLENDING_BOTHSRCALPHA for both, the mode the game restores after its effects */
static RT_CAP_HDR rt_cap = { { RT_CAP_MAGIC0, RT_CAP_MAGIC1, RT_CAP_MAGIC2, RT_CAP_MAGIC3 }, RT_CAP_VERSION,
                             sizeof(RT_CAP_HDR), sizeof(RT_CAP_REC), RT_CAP_MAX, rt_cap_rec, 0, 0, 0, 0, 0, 0, 0, 0,
                             { 0 }, 0x101u, rt_cap_pay, RT_CAP_PAY, 0, 0, 0, 0 };

static void rt_cap_copy(Uint8 *dst, const void *src, Uint32 n);

/* the next record of the current pair (NULL when the buffer is full) */
static RT_CAP_REC *rt_cap_new(Uint32 kind)
{
    RT_CAP_REC *r;
    const Float *b;
    Sint32 frame = B_8C752BE8;
    int i;

    if (rt_cap.frame != (Uint32)frame) {
        rt_cap.frame = (Uint32)frame;
        rt_cap.count = 0;
        rt_cap.dropped = 0;
        rt_cap.pay_used = 0;
    }
    if (kind < RT_CAP_KINDS)
        rt_cap.calls[kind]++;
    if (rt_cap.count >= RT_CAP_MAX) {
        rt_cap.dropped++;
        return NULL;
    }
    r = &rt_cap_rec[rt_cap.count];
    rt_cap.count++;
    rt_cap.total++;
    r->kind = kind;
    r->sub = B_8C754EC8;
    r->frame = frame;
    r->depth = B_8C88F69C;
    r->obj = NULL;
    r->texlist = D_8C673480;
    r->ctl3d = B_8C88F62C;
    r->attr_and = B_8C88F660;
    r->attr_or = B_8C88F664;
    for (i = 0; i < 4; i++)
        r->cmat[i] = B_8C88F668[i];
    r->n = 0;
    r->attr = 0;
    r->bank = rt_cap.bank;
    rt_xmtrx_store(r->m);
    b = B_8C88F5FC;
    for (i = 0; i < 16; i++)
        r->base[i] = b != NULL ? b[i] : 0.0f;
    r->blend = rt_cap.blend;
    r->pay = RT_CAP_NOPAY;
    r->pay_len = 0;
    r->flags = rt_cap_action != 0 ? RT_CAPF_NJ_ACTION : 0u;
    rt_cap_copy((Uint8 *)&r->exec, &D_8C1C1D40, 4u);    /* v5: the raw guest word, as the 2D payloads' +0x38 */
    return r;
}

/* v2: len bytes of payload for record r (4-byte aligned, in the pool of the current pair); NULL when it is full */
static Uint8 *rt_cap_payload(RT_CAP_REC *r, Uint32 len)
{
    Uint32 off = (rt_cap.pay_used + 3u) & ~3u;

    if (len > rt_cap.pay_cap || off > rt_cap.pay_cap - len) {
        r->flags |= RT_CAPF_PAY_LOST;
        return NULL;
    }
    rt_cap.pay_used = off + len;
    r->pay = off;
    r->pay_len = len;
    return rt_cap_pay + off;
}

static void rt_cap_copy(Uint8 *dst, const void *src, Uint32 n)
{
    const Uint8 *s = (const Uint8 *)src;
    Uint32 i;

    for (i = 0; i < n; i++)
        dst[i] = s[i];
}

static void rt_cap_put32(Uint8 *dst, Uint32 v)
{
    dst[0] = (Uint8)v;
    dst[1] = (Uint8)(v >> 8);
    dst[2] = (Uint8)(v >> 16);
    dst[3] = (Uint8)(v >> 24);
}

/* v2: a sprite draw (njDrawSprite2D / 3D, the game's 8C09E778): the NJS_SPRITE (0x20 bytes) and the NJS_TEXANIM
 * cell n (0x14 bytes; zeros without one) as they are at the draw - the game reuses its sprite structures. pri != NULL
 * (njDrawSprite2D, round 0.9.1): the SPRITE2D tail (pri, the running exec, the texlist before the draw) */
static void rt_cap_sprite(Uint32 kind, NJS_SPRITE *sp, Int n, Uint32 attr, Uint32 flags, const Float *pri)
{
    RT_CAP_REC *r;
    Uint8 *p;
    Uint32 i;

    if (rt_cap.on == 0 || sp == NULL)
        return;
    r = rt_cap_new(kind);
    if (r == NULL)
        return;
    r->obj = sp;
    r->texlist = sp->tlist;
    r->n = (Uint32)n;
    r->attr = attr;
    r->flags |= flags;
    p = rt_cap_payload(r, pri != NULL ? RT_CAP_SPRITE2D_PAY : 0x34u);
    if (p == NULL)
        return;
    rt_cap_copy(p, sp, 0x20u);
    if (sp->tanim != NULL && n >= 0) {
        rt_cap_copy(p + 0x20, &sp->tanim[n], 0x14u);
    } else {
        for (i = 0; i < 0x14u; i++)
            p[0x20 + i] = 0;
    }
    if (pri != NULL) {
        rt_cap_copy(p + 0x34, pri, 4u);
        rt_cap_copy(p + 0x38, &D_8C1C1D40, 4u);     /* the raw guest word: a DC code address */
        rt_cap_copy(p + 0x3C, &D_8C673480, 4u);     /* the raw guest word, before njSetTexture(sp->tlist) */
    }
}

/* round 0.9.1: a 2D polygon draw (njDrawPolygon2D 8C63EBC8): the payload of RT_CAP_POLY2D above, the points, colours and
 * uvs as they are at the draw (the game's are stack buffers: Big's tension bar 8C0E72DA) */
static void rt_cap_poly2d(NJS_POINT2COL *p, Int n, Float pri, Uint32 attr)
{
    RT_CAP_REC *r;
    Uint8 *q;
    Uint32 num, hasc, hast, len;

    if (rt_cap.on == 0 || p == NULL)
        return;
    r = rt_cap_new(RT_CAP_POLY2D);
    if (r == NULL)
        return;
    r->obj = p;
    r->n = (Uint32)n;
    r->attr = attr;
    num = (p->p != NULL && n > 0) ? (Uint32)n : 0u;
    if (num > RT_CAP_MAXPTS)
        num = RT_CAP_MAXPTS;
    hasc = p->col != NULL ? 1u : 0u;
    hast = p->tex != NULL ? 1u : 0u;
    len = RT_CAP_POLY2D_HEAD + num * (8u + 4u * hasc + 4u * hast);
    q = rt_cap_payload(r, len);
    if (q == NULL)
        return;
    rt_cap_put32(q, num);
    rt_cap_put32(q + 4, hasc);
    rt_cap_put32(q + 8, hast);
    rt_cap_put32(q + 12, p->num);
    rt_cap_copy(q + 16, &pri, 4u);
    rt_cap_copy(q + 20, &D_8C1C1D40, 4u);
    rt_cap_copy(q + 24, &D_8C673480, 4u);
    q += RT_CAP_POLY2D_HEAD;
    if (num != 0) {
        rt_cap_copy(q, p->p, 8u * num);
        q += 8u * num;
        if (hasc) {
            rt_cap_copy(q, p->col, 4u * num);
            q += 4u * num;
        }
        if (hast)
            rt_cap_copy(q, p->tex, 4u * num);
    }
}

/* v2: a point list draw (njDrawLine3D, the 3D triangle / polygon draws): u32 points kept, u32 has colours, u32 has
 * uvs, u32 p->num, then the points (12 bytes each), the colours (NJS_COLOR) and the uvs (NJS_TEX) as they are at the
 * draw (at most RT_CAP_MAXPTS points; the game rewrites its point buffers for every draw) */
static void rt_cap_points(Uint32 kind, NJS_POINT3COL *p, Int n, Uint32 attr)
{
    RT_CAP_REC *r;
    Uint8 *q;
    Uint32 num, hasc, hast, len;

    if (rt_cap.on == 0 || p == NULL)
        return;
    r = rt_cap_new(kind);
    if (r == NULL)
        return;
    r->obj = p;
    r->n = (Uint32)n;
    r->attr = attr;
    num = p->p != NULL ? p->num : 0u;
    if (num > RT_CAP_MAXPTS)
        num = RT_CAP_MAXPTS;
    hasc = p->col != NULL ? 1u : 0u;
    hast = p->tex != NULL ? 1u : 0u;
    len = 16u + num * (12u + 4u * hasc + 4u * hast);
    q = rt_cap_payload(r, len);
    if (q == NULL)
        return;
    rt_cap_put32(q, num);
    rt_cap_put32(q + 4, hasc);
    rt_cap_put32(q + 8, hast);
    rt_cap_put32(q + 12, p->num);
    q += 16;
    if (num != 0) {
        rt_cap_copy(q, p->p, 12u * num);
        q += 12u * num;
        if (hasc) {
            rt_cap_copy(q, p->col, 4u * num);
            q += 4u * num;
        }
        if (hast)
            rt_cap_copy(q, p->tex, 4u * num);
    }
}

/* v3: 1 when the n bytes at a equal those at b */
static int rt_cap_same(const Uint8 *a, const void *b, Uint32 n)
{
    const Uint8 *s = (const Uint8 *)b;
    Uint32 i;

    for (i = 0; i < n; i++)
        if (a[i] != s[i])
            return 0;
    return 1;
}

/* v3: 1 when the model snapshot at p (header already compared) holds the model's materials, points and normals as
 * they are now */
static int rt_cap_same_model(const Uint8 *p, const NJS_MODEL *model, Uint32 np, Uint32 nmat, Uint32 hasn)
{
    p += 16;
    if (nmat != 0 && !rt_cap_same(p, model->mats, 20u * nmat))
        return 0;
    p += 20u * nmat;
    if (np != 0 && !rt_cap_same(p, model->points, 12u * np))
        return 0;
    p += 12u * np;
    return hasn == 0 || rt_cap_same(p, model->normals, 12u * np);
}

/* A model draw at XMTRX. v3: with a snapshot of what the draw read from the model (payload: u32 points, u32
 * materials, u32 RT_CAP_MSNAP_* flags, u32 0, then the materials (20 bytes each), the points and the normals (12
 * bytes each) as they are at the draw). The host emits these, not the arrays as they are when it reads the records
 * after the pair, because the game rewrites them in between: PJoinVertexes moves the welded vertices to the next
 * step's pose before the display of sub step 1 (which does not draw), Tails' display bends its tail models
 * (8C0BDC80) and blends their morph (8C055ECE) after drawing them, on both sub steps, the shape motion draws
 * (8C04196C) point model->points at a scratch buffer only for the draw, and effects set a material's texture id or
 * alpha just before their draw (Knuckles' Maximum Heat draws one model with texture 1 and texture 2, E-102's hover
 * jet and beam animate theirs). A snapshot equal to an earlier one of the same model in the pair shares its bytes
 * (Tails' afterimage passes redraw the unchanged body). Without room (RT_CAPF_PAY_LOST) or for a model beyond the
 * RT_CAP_MSNAP_* limits the record has no payload and the host reads the model live, as in v2. */
static void rt_cap_model(Uint32 kind, NJS_MODEL *model)
{
    RT_CAP_REC *r = rt_cap_new(kind), *q;
    Uint32 np, nmat, hasn, len, i;
    Uint8 *p;

    if (r == NULL)
        return;
    r->obj = model;
    if (model == NULL)
        return;
    np = (model->points != NULL && model->nbPoint > 0) ? (Uint32)model->nbPoint : 0u;
    nmat = model->mats != NULL ? (Uint32)model->nbMat : 0u;
    if (np > RT_CAP_MSNAP_MAXPTS || nmat > RT_CAP_MSNAP_MAXMATS)
        return;
    hasn = (np != 0 && model->normals != NULL) ? 1u : 0u;
    len = 16u + 20u * nmat + 12u * np * (1u + hasn);
    for (i = rt_cap.count - 1u; i-- > 0u;) {
        q = &rt_cap_rec[i];
        if (q->obj != (const void *)model || q->pay == RT_CAP_NOPAY || q->pay_len != len || q->kind < RT_CAP_RENDER ||
            q->kind > RT_CAP_NJMOTION)
            continue;
        p = rt_cap_pay + q->pay;
        if (p[0] == (Uint8)np && p[1] == (Uint8)(np >> 8) && p[4] == (Uint8)nmat && p[5] == (Uint8)(nmat >> 8) &&
            p[8] == (Uint8)hasn && rt_cap_same_model(p, model, np, nmat, hasn)) {
            r->pay = q->pay;
            r->pay_len = len;
            return;
        }
    }
    p = rt_cap_payload(r, len);
    if (p == NULL)
        return;
    rt_cap_put32(p, np);
    rt_cap_put32(p + 4, nmat);
    rt_cap_put32(p + 8, hasn != 0 ? RT_CAP_MSNAP_NORMALS : 0u);
    rt_cap_put32(p + 12, 0u);
    p += 16;
    if (nmat != 0) {
        rt_cap_copy(p, model->mats, 20u * nmat);
        p += 20u * nmat;
    }
    if (np != 0) {
        rt_cap_copy(p, model->points, 12u * np);
        p += 12u * np;
        if (hasn != 0)
            rt_cap_copy(p, model->normals, 12u * np);
    }
}

/* the RT_DRAW_CAPTURE hook of the sprite / line / polygon / print entries */
static void rt_cap_misc(Uint32 kind, const void *obj, Uint32 n, Uint32 attr)
{
    RT_CAP_REC *r;

    if (rt_cap.on == 0)
        return;
    r = rt_cap_new(kind);
    if (r != NULL) {
        r->obj = obj;
        r->n = n;
        r->attr = attr;
    }
}

#define RT_DRAW_CAPTURE(kind, obj, n, attr) rt_cap_misc((Uint32)(kind), (const void *)(obj), (Uint32)(n), (Uint32)(attr))
#endif /* !TARGET_PC_HARNESS */

#ifndef RT_DRAW_CAPTURE
#define RT_DRAW_CAPTURE(kind, obj, n, attr) ((void)(kind), (void)(obj), (void)(n), (void)(attr))
#endif

/* ------------------------------------------------------------------ */
/* draw-state stores                                                   */

/* 8C605628 njColorBlendingMode: target 0 = source, 1 = destination blending
 * (any other target only re-runs the Kamui update); the mode is stored as
 * given (the original's 12-way switch maps 0..11 to themselves) */
void njColorBlendingMode(Int target, Int mode)
{
#if !defined(TARGET_PC_HARNESS)
    /* v2: the capture keeps the modes (the context store below is dropped while the context pointer is NULL) */
    if (target == 0)
        rt_cap.blend = (rt_cap.blend & 0xFF00u) | ((Uint32)mode & 0xFFu);
    if (target == 1)
        rt_cap.blend = (rt_cap.blend & 0x00FFu) | (((Uint32)mode & 0xFFu) << 8);
#endif
    if (target == 0) {
        RT_ST(B_8C88FCB4, RT_KC_UPDATE, 0x40u);
        RT_ST(B_8C88FCB4, RT_KC_SRCBLEND, (Uint32)mode);
    }
    if (target == 1) {
        RT_ST(B_8C88FCB4, RT_KC_UPDATE, 0x80u);
        RT_ST(B_8C88FCB4, RT_KC_DSTBLEND, (Uint32)mode);
    }
    RT_KM_STATE(B_8C88FCB4);
    RT_KM_CONTEXT(B_8C88FCB4);
}

/* 8C605DAC save draw state: the context's texture words and the material
 * words into B_8C890104 (material words 2 and 4 swap places when
 * B_8C88F62C & 0x4000) */
void func_8C605DAC(void)
{
    B_8C890104[0] = RT_LD(B_8C88FCB4, RT_KC_TEX0 + 0);
    B_8C890104[1] = RT_LD(B_8C88FCB4, RT_KC_TEX0 + 1);
    B_8C890104[2] = RT_LD(B_8C88FCB4, RT_KC_TEX0 + 2);
    B_8C890104[3] = RT_LD(B_8C88FCB4, RT_KC_TEX0 + 3);
    B_8C890104[4] = RT_LD(B_8C88FCD4, 0);
    B_8C890104[7] = RT_LD(B_8C88FCD4, 1);
    B_8C890104[8] = RT_LD(B_8C88FCD4, 3);
    if (B_8C88F62C & 0x4000u) {
        B_8C890104[5] = RT_LD(B_8C88FCD4, 4);
        B_8C890104[6] = RT_LD(B_8C88FCD4, 2);
    } else {
        B_8C890104[5] = RT_LD(B_8C88FCD4, 2);
        B_8C890104[6] = RT_LD(B_8C88FCD4, 4);
    }
}

/* 8C605E0A restore draw state: the inverse of 8C605DAC (no Kamui update) */
void func_8C605E0A(void)
{
    RT_ST(B_8C88FCB4, RT_KC_TEX0 + 0, B_8C890104[0]);
    RT_ST(B_8C88FCB4, RT_KC_TEX0 + 1, B_8C890104[1]);
    RT_ST(B_8C88FCB4, RT_KC_TEX0 + 2, B_8C890104[2]);
    RT_ST(B_8C88FCB4, RT_KC_TEX0 + 3, B_8C890104[3]);
    RT_ST(B_8C88FCD4, 0, B_8C890104[4]);
    RT_ST(B_8C88FCD4, 1, B_8C890104[7]);
    RT_ST(B_8C88FCD4, 3, B_8C890104[8]);
    if (B_8C88F62C & 0x4000u) {
        RT_ST(B_8C88FCD4, 4, B_8C890104[5]);
        RT_ST(B_8C88FCD4, 2, B_8C890104[6]);
    } else {
        RT_ST(B_8C88FCD4, 2, B_8C890104[5]);
        RT_ST(B_8C88FCD4, 4, B_8C890104[6]);
    }
}

/* 8C60EDE0: control-3D flags */
void func_8C60EDE0(Uint32 v)
{
    B_8C88F62C = v;
}

/* 8C60EDE6 njSetConstantAttr */
void njSetConstantAttr(Uint32 and_attr, Uint32 or_attr)
{
    B_8C88F660 = and_attr;
    B_8C88F664 = or_attr;
}

/* 8C60EDF0 njSetConstantMaterial: 4 floats copied as bits (FMOV.S), in order */
void njSetConstantMaterial(NJS_ARGB *mat)
{
    const Uint32 *src = (const Uint32 *)mat;

    B_8C88F668[0] = src[0];
    B_8C88F668[1] = src[1];
    B_8C88F668[2] = src[2];
    B_8C88F668[3] = src[3];
}

/* 8C63FAE8 / 8C63FB18: render-state switch, context +0x3C = 2 / 0 */
void func_8C63FAE8(void)
{
    RT_ST(B_8C88FCB4, RT_KC_UPDATE, 0x100u);
    RT_ST(B_8C88FCB4, RT_KC_SWITCH, 2u);
    RT_KM_STATE(B_8C88FCB4);
    RT_KM_CONTEXT(B_8C88FCB4);
}

void func_8C63FB18(void)
{
    RT_ST(B_8C88FCB4, RT_KC_UPDATE, 0x100u);
    RT_ST(B_8C88FCB4, RT_KC_SWITCH, 0u);
    RT_KM_STATE(B_8C88FCB4);
    RT_KM_CONTEXT(B_8C88FCB4);
}

/* ------------------------------------------------------------------ */
/* draws (no-ops: capture hook only)                                   */

#if !defined(TARGET_PC_HARNESS)
#define RT_CAP_SPRITE(kind, sp, n, attr) rt_cap_sprite((Uint32)(kind), (sp), (Int)(n), (Uint32)(attr), 0u, NULL)
#define RT_CAP_SPRITE2D_PRI(sp, n, pri, attr) \
    rt_cap_sprite((Uint32)RT_CAP_SPRITE2D, (sp), (Int)(n), (Uint32)(attr), 0u, &(pri))
#define RT_CAP_POINTS(kind, p, n, attr)  rt_cap_points((Uint32)(kind), (p), (Int)(n), (Uint32)(attr))
#define RT_CAP_POLY2D_PRI(p, n, pri, attr) rt_cap_poly2d((p), (Int)(n), (pri), (Uint32)(attr))
#else
#define RT_CAP_SPRITE(kind, sp, n, attr) RT_DRAW_CAPTURE(kind, sp, n, attr)
#define RT_CAP_SPRITE2D_PRI(sp, n, pri, attr) ((void)(pri), RT_DRAW_CAPTURE(RT_CAP_SPRITE2D, sp, n, attr))
#define RT_CAP_POINTS(kind, p, n, attr)  RT_DRAW_CAPTURE(kind, p, n, attr)
#define RT_CAP_POLY2D_PRI(p, n, pri, attr) ((void)(pri), RT_DRAW_CAPTURE(13, p, n, attr))
#endif

/* 8C63EEB4 njDrawSprite2D (round 0.9.1: the record keeps pri, the running exec and the texlist before the draw) */
void njDrawSprite2D(NJS_SPRITE *sp, Int n, Float pri, Uint32 attr)
{
    RT_CAP_SPRITE2D_PRI(sp, n, pri, attr);
    njSetTexture(sp->tlist);
}

/* 8C63EBC8 njDrawPolygon2D (round 0.9.1; a "void" boundary stub before, gen/policy/stubs.tsv): n points of p, one
 * polygon at the 2D depth pri. The original (Ghidra 8C63EBC8.c) saves the draw state (8C605DAC), picks its routines by
 * attr: 0x80000000 textured (the current texture), 0x40 translucent (passed on as the blend switch), 0x20 filled (else
 * the outline), draws through Kamui (8C6069A4) and restores the draw state (8C605E0A): renderer state only, as the
 * other draws (section 3 above: a no-op here; sadc docs/research/gsa1_070_hud.md Review R.3.2). Callers: Big's tension
 * bar 8C0E72DA (attr 0x20: an opaque filled gouraud quad), the camera's underwater tint (8C01C260 / 8C01C270, attr
 * 0x60), front-end 2D screens (code_8C013860.c / code_8C013F20.c). Recorded as RT_CAP_POLY2D (13). */
void func_8C63EBC8(NJS_POINT2COL *p, Sint32 n, Float pri, Uint32 attr);     /* as the game's callers declare it */
void func_8C63EBC8(NJS_POINT2COL *p, Sint32 n, Float pri, Uint32 attr)
{
    RT_CAP_POLY2D_PRI(p, n, pri, attr);
}

/* 8C63EF74 njDrawSprite3D */
void njDrawSprite3D(NJS_SPRITE *sp, Int n, Uint32 attr)
{
    RT_CAP_SPRITE(RT_CAP_SPRITE3D, sp, n, attr);
    njSetTexture(sp->tlist);
}

/* 8C63CE24 njDrawLine3D: n lines of the point list (pairs of points; p->num points and colours) */
void njDrawLine3D(NJS_POINT3COL *p, Int n, Uint32 attr)
{
    RT_CAP_POINTS(RT_CAP_LINE3D, p, n, attr);
}

/* 8C63D414 njDrawPolygon3D: n points, one convex polygon (8C63D480-8C63D494 transform all n, 8C63D536-8C63D54C clip
 * them as one polygon); attr 0x20 fill (without it the outline: every edge as a line quad, 8C64FE0C / 8C650144), 0x40
 * translucent, 0x80000000 textured. Recorded as RT_CAP_TRI3D (the kind's old name; lightspeed_visuals.md 2.5) */
void func_8C63D414(NJS_POINT3COL *p, Int n, Uint32 attr)
{
    RT_CAP_POINTS(RT_CAP_TRI3D, p, n, attr);
}

/* 8C63D66C njDrawTriangle3D: n triangles; stride = attr & 2 ? 1 : attr & 1 ? 2 : 3, triangle i = (attr & 1 ? 0 :
 * i * stride, 1 + i * stride, 2 + i * stride) (8C63D716-8C63D7AE, 8C63D922-8C63D93A): 0x2 a strip, 0x3 a fan, 0x1 a
 * fan of every other point, 0 separate triangles; 0x20 fill, 0x40 translucent, 0x80000000 textured. Recorded as
 * RT_CAP_POLY3D */
void func_8C63D66C(NJS_POINT3COL *p, Int n, Uint32 attr)
{
    RT_CAP_POINTS(RT_CAP_POLY3D, p, n, attr);
}

/* ------------------------------------------------------------------ */
/* debug print                                                         */

/* 8C640766 njPrintColor: the colour of later prints */
void njPrintColor(Uint32 color)
{
    B_8C89055C = color;
}

/* 8C640922 njPrintC: a string at loc (x << 16 | y) - not drawn */
void njPrintC(Int loc, const char *str)
{
    RT_DRAW_CAPTURE(RT_CAP_PRINT, str, loc, 0);
}

/* 8C640A80: a number at loc, digits wide - not drawn */
void func_8C640A80(Int loc, Int value, Int digits)
{
    RT_DRAW_CAPTURE(RT_CAP_PRINT, 0, loc, value);
    (void)digits;
}

/* ------------------------------------------------------------------ */
/* vblank                                                              */

/* 8C64133E: registers a vblank handler (+ argument) in the SDK's interrupt
 * table (8C06C0C0 registers the game's frame callback 8C06C1D8 with it) and
 * returns 0.  libsa1 has no interrupts and runs the frame logic itself
 * (design 6.4), so nothing is registered: no store, 0 */
Sint32 func_8C64133E(void *handler, Uint32 arg)
{
    (void)handler;
    (void)arg;
    return 0;
}

/* 8C604546: the SDK's vsync step (run from the game's vblank handler
 * 8C06C1D8): clear the wait flag, mark the vblank, call the user callback if
 * one is set (the arena starts with none) */
void func_8C604546(void)
{
    void (*cb)(void);

    B_8C88F7D0 = 0;
    B_8C88F7D8 = 1;
    cb = D_8C673478;
    if (cb != NULL) {
        cb();
    }
}

#if !defined(TARGET_PC_HARNESS)
/* ------------------------------------------------------------------ */
/* 5b. the model draws of the game (libsa1 profile)                    */
/*                                                                     */
/* These were boundary stubs (gen/policy/stubs.tsv: void); outside a   */
/* capture window each still does exactly that.  Inside one:           */
/*  - 8C037098 / 8C03700C, the hand-written basic-model renderers of   */
/*    the motion player (Sonic's body ...), keep their early return    */
/*    while B_8C754EC8 != 0: the game draws them once per pair, on     */
/*    sub step 0; their frustum test (8C03718C) is not reproduced;     */
/*  - njDrawModel records the model at XMTRX; njDrawObject walks the   */
/*    tree like Ninja (push; translate / rotate XYZ or ZXY / scale     */
/*    unless NJD_EVAL_UNIT_*; the model unless NJD_EVAL_HIDE; the      */
/*    child unless NJD_EVAL_BREAK; pop) with the runtime's matrix      */
/*    functions and records every node model;                          */
/*  - 8C60E080 (object, motion, frame) and 8C60E49A (action, frame),   */
/*    Ninja's motion draws (Light Speed aura, snowboard), run the      */
/*    game's own copy of the Ninja motion player (8C03FE1A, 8C03FE60,  */
/*    8C040784; design D4) to walk the nodes: its node draws (8C037098)*/
/*    are then recorded on every sub step as RT_CAP_NJMOTION.  The     */
/*    originals draw the nodes with Ninja, not with that renderer:     */
/*    8C60E080 through its walker 8C60DDE4 -> njDrawModel 8C611028 ->  */
/*    8C6108B0, 8C60E49A through its own walker 8C60E31C -> 8C621574   */
/*    -> 8C617C34 -> 8C6209A8 (lightspeed_visuals.md Review R.2.1).    */
/*    The colour rules of the two differ, so 8C60E49A's records carry  */
/*    RT_CAPF_NJ_ACTION (sa1_capture.c gives them 8C6209A8's rules);   */
/*  - 8C621574, that node draw of 8C60E49A's walker, which game code   */
/*    also calls itself (the power-up models, round 0.9.2a FX3), is    */
/*    recorded as such a node: RT_CAP_NJMOTION with RT_CAPF_NJ_ACTION; */
/*  - 8C6215D6 (collision / debug display objects) is recorded         */
/*    without walking the object; 8C038CEC keeps the palette bank.     */
/* None of them draws anything.                                        */
Sint32 func_8C03FE1A(NJS_MOTION *motion, Float frame);     /* node motion setup */
void func_8C03FE60(NJS_OBJECT *object);                    /* node handlers */
void func_8C040784(NJS_OBJECT *object);                    /* tree walk with node motion, draws with 8C037098 */
void func_8C03700C(NJS_MODEL *model);
void func_8C037098(NJS_MODEL *model);
void func_8C038CEC(Sint32 bank);

/* 8C038CEC: palette bank of the hand-written renderer (GBR + 0x58) */
void func_8C038CEC(Sint32 bank)
{
    if (rt_cap.on != 0)
        rt_cap.bank = bank;
}

/* 8C037098: hand-written basic-model renderer */
void func_8C037098(NJS_MODEL *model)
{
    if (rt_cap.on == 0)
        return;
    if (B_8C754EC8 != 0 && rt_cap.ninja == 0)
        return;
    if (model == NULL || model->nbPoint <= 0)
        return;
    rt_cap_model(rt_cap.ninja != 0 ? RT_CAP_NJMOTION : RT_CAP_RENDER, model);
}

/* 8C03700C: the same between 8C605DAC / 8C605E0A */
void func_8C03700C(NJS_MODEL *model)
{
    if (rt_cap.on == 0)
        return;
    if (B_8C754EC8 != 0)
        return;
    if (model == NULL || model->nbPoint <= 0)
        return;
    rt_cap_model(RT_CAP_RENDER2, model);
}

/* 8C611028 njDrawModel */
void njDrawModel(NJS_MODEL *model)
{
    if (rt_cap.on == 0 || model == NULL)
        return;
    rt_cap_model(RT_CAP_NJMODEL, model);
}

/* njDrawObject's tree walk */
static void rt_cap_tree(NJS_OBJECT *obj)
{
    Uint32 fl;

    do {
        fl = obj->evalflags;
        njPushMatrix(NULL);
        if (!(fl & NJD_EVAL_UNIT_POS))
            njTranslate(NULL, obj->pos[0], obj->pos[1], obj->pos[2]);
        if (!(fl & NJD_EVAL_UNIT_ANG)) {
            if (fl & NJD_EVAL_ZXY_ANG)
                njRotateZXY(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
            else
                njRotateXYZ(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
        }
        if (!(fl & NJD_EVAL_UNIT_SCL))
            njScale(NULL, obj->scl[0], obj->scl[1], obj->scl[2]);
        if (obj->model != NULL && !(fl & NJD_EVAL_HIDE))
            rt_cap_model(RT_CAP_NJOBJECT, obj->model);
        if (obj->child != NULL && !(fl & NJD_EVAL_BREAK))
            rt_cap_tree(obj->child);
        njPopMatrix(1);
        obj = obj->sibling;
    } while (obj != NULL);
}

/* 8C61103C njDrawObject */
void njDrawObject(NJS_OBJECT *object)
{
    if (rt_cap.on == 0 || object == NULL)
        return;
    rt_cap_tree(object);
}

/* 8C09E778: the game's own 3D sprite draw (1ST_READ asm, a "void" boundary stub before capture v2; 13,632 calls in
 * the reference scripts: the characters' effect sprites). The original returns at once on sub step 1, then draws
 * sprite cell n of sp with Kamui (attr: Ninja sprite bits, 0x20 translucent, 0x2 the constant material's colour, 0x10
 * scaled) between a draw-state save / restore and leaves sp->tlist set with njSetTexture. Here: nothing outside a
 * capture window (as the stub; the texlist is not touched either, so a capture window changes no game-visible word)
 * and a sprite record inside one. */
void func_8C09E778(NJS_SPRITE *sp, Sint32 n, Uint32 attr)
{
    if (rt_cap.on == 0 || B_8C754EC8 != 0)
        return;
    rt_cap_sprite(RT_CAP_SPRITE3D, sp, (Int)n, attr, RT_CAPF_GAME_SPRITE, NULL);
}

/* 8C6215D6: collision / debug display object */
void func_8C6215D6(void *model)
{
    RT_CAP_REC *r;

    if (rt_cap.on == 0 || model == NULL)
        return;
    r = rt_cap_new(RT_CAP_DEBUG);
    if (r != NULL)
        r->obj = model;
}

/* 8C60E080: object with a node motion at a frame (Ninja) */
void func_8C60E080(void *a, void *b, Float f)
{
    NJS_OBJECT *object = (NJS_OBJECT *)a;

    if (rt_cap.on == 0 || object == NULL)
        return;
    rt_cap.ninja++;
    if (func_8C03FE1A((NJS_MOTION *)b, f) == 0) {
        func_8C03FE60(object);
        func_8C040784(object);
    }
    rt_cap.ninja--;
}

/* 8C60E49A: an action at a frame (Ninja); its records carry RT_CAPF_NJ_ACTION */
void func_8C60E49A(void *a, Float f)
{
    NJS_ACTION *action = (NJS_ACTION *)a;

    if (rt_cap.on == 0 || action == NULL)
        return;
    rt_cap_action++;
    func_8C60E080(action->object, action->motion, f);
    rt_cap_action--;
}

/* 8C621574: the node draw of 8C60E49A's walker 8C60E31C (the original: draw-state save 8C605DAC, 8C617C34 -> 8C6215BA
 * -> 8C6209A8 with the model at XMTRX and the current texlist, restore 8C605E0A; 8C620F2C.src, sadc docs/research/
 * lightspeed_visuals.md Review R.2.1). Game code calls it directly for the power-up models, each between its own
 * njPushMatrix / njPopMatrix on sub step 0 only (sadc code_8C043C00.c: 8C043E70 the magnetic barrier's spark model
 * D_8C1A0388, 8C044166 the barrier's spark model D_8C1A07F8, 8C044290 the invincibility model D_8C1A1264;
 * code_8C044480.c: 8C0444C4 the invincibility spark model D_8C1A0910). A "void" boundary stub until round 0.9.2a
 * (gen/policy/stubs.tsv; the items worked, their models were not drawn: sadc docs/research/gsa1_091b_itembox.md FX3 with
 * its Review R.4.4). Here, like the other model draws: nothing outside a capture window; inside one, one record of the
 * model at XMTRX (with its v3 snapshot) flagged RT_CAPF_NJ_ACTION, so the host gives it 8C6209A8's colour rules (the
 * diffuse ARGB, a textured material's RGB 1.0 under control-3D 0x200, the constant material added under 0x20: the
 * magnetic barrier's fade) and the material's own blending. The draw-state save / restore is not reproduced (renderer
 * state only, section 3), so a capture window changes no game-visible word. */
void func_8C621574(NJS_MODEL *model)
{
    if (rt_cap.on == 0 || model == NULL)
        return;
    rt_cap_action++;
    rt_cap_model(RT_CAP_NJMOTION, model);
    rt_cap_action--;
}
#endif /* !TARGET_PC_HARNESS */

/* ------------------------------------------------------------------ */
/* 6. 8C038D00: the sphere visibility test (both profiles)            */
/*                                                                     */
/* Hand-written SH-4 (160 bytes, no C in sadc).  The character and     */
/* object displays ask it whether a sphere (centre x, y, z in world    */
/* space, radius r) can be on screen; their visible path draws and,    */
/* for Tails (8C0BA820) and Knuckles (8C0C46AC), runs logic (the       */
/* collision shape's centre, Tails' tail morph and flight actions,     */
/* counters).  Reproduced exactly (sadc docs/research/                 */
/* visibility_fidelity.md 1):                                          */
/*  - 0 on sub step 1 (B_8C754EC8 != 0): the displays draw once per     */
/*    pair;                                                            */
/*  - FTRV of (x, y, z, 1) with XMTRX: the camera's view while a       */
/*    display runs (replays: the record's px_xmtrx; live worlds: the   */
/*    camera adapter's view, src/world/sa1_world_tick.c);              */
/*  - the depth window: near B_8C88F610 - r < z <= far B_8C88F614 + r; */
/*  - the projection with iz = 1 / (z + r) and the clip rectangle      */
/*    B_8C88F600..B_8C88F608, in the original's operation order (FMUL  */
/*    and FADD, no FMAC: one rounding each) and with its two quirks:   */
/*    the y scale is aspect y * screen centre x (B_8C88F5F0, not the    */
/*    distance B_8C88F5E4), and the last test scales by the x scale    */
/*    and compares with the x maximum B_8C88F608 (B_8C88F60C, the y    */
/*    maximum, is loaded and never used).                               */
/* With the zero screen of the reference simulator before 2026-10-20   */
/* (kx = 0: the x-maximum test compares the screen centre 320 with 0)  */
/* it answers 0 in every call; with the screen njInitSystem sets       */
/* (src/world/sa1_world_boot.c) the player is on screen on every sub   */
/* step 0, as on the Dreamcast.  The answer is game logic: a capture   */
/* window does not change it.  The libsa1 profile counts the answers   */
/* of the pair inside capture windows (RT_CAP_HDR vis_*), so the host  */
/* can show a character the game's camera did not see (sa1_capture.h). */
extern Sint32 B_8C754EC8;               /* logic sub step 0 / 1 */
extern Float B_8C88F5E4;                /* screen distance */
extern Float B_8C88F5F0;                /* screen centre x */
extern Float B_8C88F5F4;                /* screen centre y */
extern Float B_8C88F600[4];             /* 2D clip rectangle x0, y0, x1, y1 */
extern Float B_8C88F610;                /* near clip */
extern Float B_8C88F614;                /* far clip */
extern Float B_8C88F618;                /* aspect x */
extern Float B_8C88F61C;                /* aspect y */

Sint32 func_8C038D00(Float x, Float y, Float z, Float r);

#if !defined(TARGET_PC_HARNESS)
/* 6a. round 0.9.4b (sadc docs/research/objects2_plan.md 6.1 task 6 with its Review R.15; objects2_range.md 6; libsa1.h
 * sa1_set_config.view_tan): the object displays' screen width. R4 pcdata like rt_cap (one copy per world in segment
 * 0x18): libsa1/src/world/sa1_world_set.c (st_objview) finds it by its magic and writes, at a pair start, kx_scale = the
 * game's horizontal tan / the host's (0: off) and the execs of the four SET types (RING 0C0869E8, O_GRING 0C087B80 -
 * which runs its rings -, O_ITEMBOX 0C05D10C, O_ItemBoxAir 0C05D9F6; their displays' visible paths only draw: read in
 * sa1_world_set.c st_objview). While kx_scale > 0 and the exec the executor runs (D_8C1C1D40, the raw 4-byte guest word
 * as rt_cap_new copies it, & 0x1FFFFFFF) is one of them, 8C038D00's two SIDEWAYS tests use kx * kx_scale; the near /
 * far tests, the up test and the last test (y by kx against the x maximum) keep the game's kx, and RT_VIS counts as
 * before. With kx_scale 0 the function does what it did, operation for operation. Not in the harness profile (the
 * capture block's guard): there RT_OBJV_KX(kx) is kx and 8C038D00 is unchanged byte for byte. */
#define RT_OBJV_MAGIC0  0x564A424Fu     /* "OBJV" */
#define RT_OBJV_MAGIC1  0x30304438u     /* "8D00" */
typedef struct RT_OBJVIEW {
    Uint32 magic[2];            /* 0x00 RT_OBJV_MAGIC0, RT_OBJV_MAGIC1 */
    Uint32 version;             /* 0x08 1 */
    Float kx_scale;             /* 0x0C 0: the game's screen */
    Uint32 n;                   /* 0x10 execs in exec[] (at most 4) */
    Uint32 exec[4];             /* 0x14 DC addresses & 0x1FFFFFFF */
} RT_OBJVIEW;                   /* 0x24 */
SA1_ASSERT_SIZE(RT_OBJVIEW, 0x24);
static RT_OBJVIEW rt_objview = { { RT_OBJV_MAGIC0, RT_OBJV_MAGIC1 }, 1u, 0.0f, 0u, { 0u, 0u, 0u, 0u } };

/* kx, or kx * kx_scale while one of the block's execs runs */
static Float rt_objview_kx(Float kx)
{
    Uint32 ex, i;

    if (!(rt_objview.kx_scale > 0.0f))
        return kx;
    rt_cap_copy((Uint8 *)&ex, &D_8C1C1D40, 4u);
    ex &= 0x1FFFFFFFu;
    for (i = 0; i < rt_objview.n && i < 4u; i++) {
        if (rt_objview.exec[i] == ex)
            return RT_FMUL(kx, rt_objview.kx_scale);
    }
    return kx;
}
#define RT_OBJV_KX(kx) rt_objview_kx(kx)

/* the answer, counted while a capture window is open */
static Sint32 rt_vis_answer(Sint32 vis)
{
    Uint32 frame;

    if (rt_cap.on == 0)
        return vis;
    rt_cap.vis_calls++;
    if (B_8C754EC8 != 0)
        return vis;
    frame = (Uint32)B_8C752BE8;
    if (rt_cap.vis_frame != frame) {
        rt_cap.vis_frame = frame;
        rt_cap.vis_shown = 0;
        rt_cap.vis_hidden = 0;
    }
    if (vis != 0)
        rt_cap.vis_shown++;
    else
        rt_cap.vis_hidden++;
    return vis;
}
#define RT_VIS(v) rt_vis_answer(v)
#else
#define RT_VIS(v) (v)
#define RT_OBJV_KX(kx) kx          /* no parentheses: the harness profile's tokens stay the original's */
#endif

Sint32 func_8C038D00(Float x, Float y, Float z, Float r)
{
    Float v[4], c[4], iz, kx, ky;

    if (B_8C754EC8 != 0)
        return RT_VIS(0);
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = 1.0f;
    RT_FTRV(v, c);                                                      /* FTRV XMTRX,FV4 */
    if (!(c[2] > RT_FSUB(B_8C88F610, r)))                               /* near */
        return RT_VIS(0);
    if (c[2] > RT_FADD(B_8C88F614, r))                                  /* far */
        return RT_VIS(0);
    iz = RT_FDIV(1.0f, RT_FADD(c[2], r));
    kx = RT_FMUL(RT_FMUL(B_8C88F618, B_8C88F5E4), iz);                  /* aspect x * distance / (z + r) */
    if (!(RT_FADD(RT_FMUL(RT_FADD(c[0], r), RT_OBJV_KX(kx)), B_8C88F5F0) > B_8C88F600[0]))   /* 6a: the sides */
        return RT_VIS(0);
    if (RT_FADD(RT_FMUL(RT_FSUB(c[0], r), RT_OBJV_KX(kx)), B_8C88F5F0) > B_8C88F600[2])
        return RT_VIS(0);
    ky = RT_FMUL(RT_FMUL(B_8C88F61C, B_8C88F5F0), iz);                  /* sic: aspect y * centre x */
    if (!(RT_FADD(RT_FMUL(RT_FADD(c[1], r), ky), B_8C88F5F4) > B_8C88F600[1]))
        return RT_VIS(0);
    if (RT_FADD(RT_FMUL(RT_FSUB(c[1], r), kx), B_8C88F5F4) > B_8C88F600[2])   /* sic: kx, the x maximum */
        return RT_VIS(0);
    return RT_VIS(1);
}
