/*
 * 1ST_READ 8C043C00-8C044480 (chunk 8C043C00): the power-up effect tasks of
 * the item box items (magnetic barrier, barrier, invincibility). The task
 * inits / execs func_8C043C9E + lbl_8C043CBE (code_8C043C9E.c),
 * func_8C044060 + lbl_8C044080 (code_8C044060.c) and func_8C044284
 * (code_8C044284.c) are already C; this file has the rest of the chunk:
 *
 *  func_8C043C00  follow the player: twp->scl.x = ptwp->scl.x, twp->scl.y =
 *                 player 0's p.height * scl.x * 0.6, twp->pos = the point
 *                 (0, scl.y, 0) in the player's frame (pos, ang.z, ang.x)
 *  func_8C043D66  magnetic barrier spark (child) exec: 11 frames with a
 *                 random texture / model pick in counter.b[0] / b[1], then
 *                 value.f (fade) from min(value, 0.5) down by 0.025 per
 *                 frame; follows the parent; draws itself (func_8C043E70)
 *  func_8C043E70  magnetic barrier spark disp: a model (D_8C1A0388) and a
 *                 textured quad (func_8C63D66C) whose vertex colours
 *                 (D_8C19B740, game data the code writes) fade with value.f
 *  func_8C043FEC  magnetic barrier task dest (empty)
 *  func_8C044122  barrier spark (child) exec: 30 frames, follows the parent
 *  func_8C044166  barrier spark disp: model D_8C1A07F8 with texture
 *                 btimer / 2 of D_8C19B650
 *  func_8C044280  barrier / invincibility task dest (empty)
 *  func_8C044290  invincibility disp: model D_8C1A1264
 *  func_8C04430A  invincibility exec (set by func_8C044284): while the
 *                 player exists and twp->timer.l (post-incremented) has not
 *                 passed 0x4EC (1,260 frames), keeps player 0's item bit
 *                 0x8000 set, follows the player, three tries per frame
 *                 (p 0.3 each) of a child spark (exec func_8C044480, disp
 *                 func_8C0444C4: chunk 8C044480, code_8C044480.c), draws
 *                 itself and runs the children; then clears item 0x8000 and
 *                 frees itself (asm 8C044338-8C044354)
 *
 * Task fields: counter.b[0] = player number (the effect tasks) or random
 * picks (sparks); btimer = spark age; value.f = spark fade (magnetic
 * barrier); timer.l = invincibility frames; scl.x / scl.y = size / height
 * taken from the player task's scl (func_8C043C00).
 *
 * PORTABILITY: D_8C19B740 (NJS_COLOR[4]) is game data that
 * func_8C043E70 writes (vertex colours of the spark quad): per instance on
 * PC (docs/portability.md 4). func_8C621574 is a Ninja-internal model draw
 * (the node draw of Ninja's action walker 8C60E31C -> 8C621574): libsa1 needs
 * an rt entry for it (LANDING_b.md).
 *
 * Matching notes (all nine functions MATCH):
 *  - the texture index is a plain local n stored before njSetTexture
 *    (n = ...; njSetTexture(&tbl[n])): SHC keeps it in the @R15 slot that
 *    the "a ? njRotateY(NULL, a) : (void)0" temporaries share, as the
 *    original does; the index inline in the call drops the store;
 *  - func_8C043D66: "else if" for the 0.5 clamp (two plain ifs reload the
 *    fade into FR3 instead of FR2);
 *  - func_8C043E70: the colour bytes as chained assignments b = g = r = c
 *    per vertex (separate statements swap R5 / R6 for the tex / col
 *    pointers);
 *  - func_8C04430A only matches with func_8C044480 / func_8C0444C4 defined
 *    in the same file (ASM_STUBs at the end): with plain prototypes SHC
 *    hoists the &func_8C0444C4 pool load out of the spark loop into R8.
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST D_8C19B5E8[];        /* magnetic barrier spark quad textures (8 x 1 texture) */
extern NJS_TEXLIST D_8C19B630[];        /* magnetic barrier spark model textures */
extern NJS_TEXLIST D_8C19B650[];        /* barrier spark textures (btimer / 2: 0..14) */
extern NJS_TEXLIST D_8C19B708;          /* invincibility texture */
extern NJS_POINT3 D_8C19B710[];         /* spark quad points (4) */
extern NJS_COLOR D_8C19B740[];          /* spark quad vertex colours (4); written below */
extern NJS_MODEL D_8C1A0388;            /* magnetic barrier spark model */
extern NJS_MODEL D_8C1A07F8;            /* barrier spark model */
extern NJS_MODEL D_8C1A1264;            /* invincibility model */

void func_8C621574(NJS_MODEL *model);   /* Ninja model draw (8C60E31C's node draw) */

void func_8C043E70(task *tp);
void func_8C044166(task *tp);
void func_8C044290(task *tp);
void func_8C044480(task *tp);           /* invincibility spark exec (code_8C044480.c) */
void func_8C0444C4(task *tp);           /* invincibility spark disp (code_8C044480.c) */

void func_8C043C00(taskwk *twp, taskwk *ptwp)
{
    NJS_POINT3 v;

    v.x = v.y = v.z = 0.0f;
    twp->scl.x = ptwp->scl.x;
    twp->scl.y = GetPlayerWorkPtr(0)->p.height * twp->scl.x * 0.59999997f;
    njPushMatrix(D_8C67C640);
    njTranslateV(NULL, &ptwp->pos);
    ptwp->ang.z ? njRotateZ(NULL, ptwp->ang.z) : (void)0;
    ptwp->ang.x ? njRotateX(NULL, ptwp->ang.x) : (void)0;
    njTranslate(NULL, 0.0f, twp->scl.y, 0.0f);
    njCalcPoint(NULL, &v, &twp->pos);
    njPopMatrix(1);
}

void func_8C043D66(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;

    twp = tp->twp;
    ptwp = tp->ptp->twp;
    if (twp->btimer++ > 10) {
        if (twp->value.f <= 0.0f) {
            FreeTask(tp);
            return;
        } else if (twp->value.f > 0.5f) {
            twp->value.f = 0.5f;
        }
        twp->value.f -= 0.024999999f;
    } else {
        twp->counter.b[0] = SA1_FTOI((Float)rand() * 0.00048828125f);
        twp->counter.b[1] = SA1_FTOI((Float)rand() * 9.1552734375e-05f);
    }
    twp->pos = ptwp->pos;
    twp->scl.x = ptwp->scl.y * 0.19999999f;
    func_8C043E70(tp);
}

void func_8C043E70(task *tp)
{
    NJS_POINT3COL pc;
    NJS_TEX tex[4];
    taskwk *twp;
    Float f;
    Sint32 c;
    Sint32 u;
    Sint32 n;

    twp = tp->twp;
    if (B_8C754EC8 == 0) {
        njColorBlendingMode(0, 10);
        njColorBlendingMode(1, 10);
        njPushMatrix(NULL);
        f = twp->value.f - 1.0f;
        func_8C043A20(0.0f, f, f, f);
        n = (Uint8)twp->counter.b[1];
        njSetTexture(&D_8C19B630[n]);
        njTranslateV(NULL, &twp->pos);
        twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        njScale(NULL, twp->scl.x, twp->scl.x, twp->scl.x);
        func_8C621574(&D_8C1A0388);
        func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
        pc.p = D_8C19B710;
        pc.col = D_8C19B740;
        pc.tex = tex;
        pc.num = 4;
        if ((Uint8)twp->counter.b[0] & 1) {
            u = 128;
        } else {
            u = 0;
        }
        tex[0].u = u;
        tex[0].v = 0;
        tex[1].u = u + 127;
        tex[1].v = 0;
        tex[2].u = u;
        tex[2].v = 255;
        tex[3].u = u + 127;
        tex[3].v = 255;
        c = SA1_FTOI(twp->value.f * 255.0f);
        D_8C19B740[0].argb.b = D_8C19B740[0].argb.g = D_8C19B740[0].argb.r = c;
        D_8C19B740[1].argb.b = D_8C19B740[1].argb.g = D_8C19B740[1].argb.r = c;
        D_8C19B740[2].argb.b = D_8C19B740[2].argb.g = D_8C19B740[2].argb.r = c;
        D_8C19B740[3].argb.b = D_8C19B740[3].argb.g = D_8C19B740[3].argb.r = c;
        n = (Uint8)twp->counter.b[0] / 2;
        njSetTexture(&D_8C19B5E8[n]);
        njSetTextureNum(0);
        func_8C63D66C(&pc, 2, 0x80000062);
        njPopMatrix(1);
    }
}

void func_8C043FEC(task *tp)
{
}

void func_8C044122(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;

    twp = tp->twp;
    ptwp = tp->ptp->twp;
    if (twp->btimer >= 30) {
        FreeTask(tp);
        return;
    }
    twp->pos = ptwp->pos;
    twp->scl.x = ptwp->scl.y * 0.19999999f;
    func_8C044166(tp);
    twp->btimer++;
}

void func_8C044166(task *tp)
{
    taskwk *twp;
    Sint32 n;

    twp = tp->twp;
    if (B_8C754EC8 == 0) {
        njPushMatrix(NULL);
        n = twp->btimer >> 1;
        njSetTexture(&D_8C19B650[n]);
        njTranslateV(NULL, &twp->pos);
        twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        njScale(NULL, twp->scl.x, twp->scl.x, twp->scl.x);
        func_8C621574(&D_8C1A07F8);
        njPopMatrix(1);
    }
}

void func_8C044280(task *tp)
{
}

void func_8C044290(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (B_8C754EC8 == 0) {
        njPushMatrix(NULL);
        njSetTexture(&D_8C19B708);
        njTranslateV(NULL, &twp->pos);
        twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        njScale(NULL, twp->scl.x, twp->scl.x, twp->scl.x);
        func_8C621574(&D_8C1A1264);
        njPopMatrix(1);
    }
}

void func_8C04430A(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;
    playerwk *pwp;
    task *ctp;
    taskwk *ctwp;
    Sint32 i;

    twp = tp->twp;
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    pwp = GetPlayerWorkPtr(0);
    if (ptwp == NULL || twp->timer.l++ > 0x4EC) {
        pwp->item &= ~0x8000;
        FreeTask(tp);
        return;
    }
    pwp->item |= 0x8000;
    func_8C043C00(twp, ptwp);
    for (i = 0; i < 3; i++) {
        if ((Float)rand() * 3.0517578125e-05f > 0.7f) {
            if ((ctp = CreateChildTask(2, func_8C044480, tp)) != NULL) {
                ctwp = ctp->twp;
                ctwp->ang.x = SA1_FTOI((Float)rand() * 2.0f);
                ctwp->ang.y = SA1_FTOI((Float)rand() * 2.0f);
                ctp->disp = func_8C0444C4;
            }
        }
    }
    func_8C044290(tp);
    func_8C098E98(tp);
}

/*
 * The invincibility spark (chunk 8C044480, its own file code_8C044480.c),
 * defined after func_8C04430A in the original translation unit: SHC only
 * reloads &func_8C0444C4 inside func_8C04430A's loop (8C0443BE) when it is
 * a function of this file.
 */
ASM_STUB(void func_8C044480(task *tp))
ASM_STUB(void func_8C0444C4(task *tp))
