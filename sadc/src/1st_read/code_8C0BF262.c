/*
 * 1ST_READ 8C0BF262-8C0BF804 (chunk 8C0BEDA0, functions 2-7 of 18): Tails.
 *
 *  func_8C0BF262  per-frame setup of Tails' four CCL_INFO shapes, called by
 *                 TailsMain (8C0B6148) before CCL_Entry: shape 0 radius
 *                 4.0 and its two attack/damage levels (0/0, 1/1 while
 *                 twp->flag & 0x100), shapes 1-3 off (attr 0x10); mode 20
 *                 turns shapes 1/2 on at the two tail tips of last frame
 *                 (pwp->free.f[0..2] / f[3..5], written by func_8C0BEDA0),
 *                 mode 15 shrinks shape 0 to 2.25 and turns shape 3 on
 *                 2.0 behind Tails, 8.25 up
 *  func_8C0BF3F4  dest of the head morph task: put the original head models
 *                 back into the head objects (normal and flight model)
 *  func_8C0BF402  exec of the head morph task (CreateElementalTask(3, 3),
 *                 TailsMain mode 0): Tails version of Sonic's func_8C0B2376.
 *                 A damped wobble (mwp->spd.y amplitude, mwp->ang_spd.z
 *                 phase) driven by the player's speed becomes twp->ang.z;
 *                 the vertices of the current head model (normal or flight,
 *                 chosen by the action's object) are rotated about three
 *                 pivots into the morph copy miles_head_z, which both head
 *                 objects then use
 *  func_8C0BF76A  face task (CreateElementalTask(2, 3), TailsMain mode 0):
 *                 allocate the PL_FACE into tp->mwp, exec func_8C0BF796
 *  func_8C0BF796  face task exec: pick the face node of the normal or the
 *                 flight model (player timer.b[3] & 0x20), then run the
 *                 face code 8C07B106
 *  func_8C0BF7CE  set up Tails' snowboard task (M_SBMOT.PRS object and
 *                 texlist, weight 0.02, exec/disp/dest 8C0BF804/8C0BF92C/
 *                 8C0BFADE)
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

extern NJS_MATRIX B_8C7A2190;       /* head morph: full angle */
extern NJS_MATRIX B_8C7A21D0;       /* head morph: half angle */

/* Tails' head (game data, extern only): object + original model of the
 * normal and of the flight character model, and the morph copy */
extern NJS_MODEL D_8C25F7D4;        /* head model (Tails) */
extern NJS_OBJECT D_8C25F7FC[];     /* head object (Tails), model = D_8C25F7D4 or miles_head_z (array:
                                     * SHC addresses ->model as base + 4) */
extern NJS_OBJECT D_8C25F830;       /* face node (Tails); its sibling is switched by the face code */
extern NJS_MODEL D_8C267508;        /* head model (Tails_Flight) */
extern NJS_OBJECT D_8C267530[];     /* head object (Tails_Flight) */
extern NJS_OBJECT D_8C267564;       /* face node (Tails_Flight) */
extern NJS_MODEL miles_head_z;      /* morph copy of the head model */
extern NJS_OBJECT Tails;            /* character model roots (TailsActionList[].actptr->object) */
extern NJS_OBJECT Tails_Flight;
extern NJS_OBJECT Tails_EV_Head;    /* morph face object */
extern NJS_MOTION miles_face_m_head; /* face shape motion */

/* M_SBMOT.PRS data at absolute addresses (snowboard object 0x0CB029C4 and
 * texlist 0x0CB025A0). PC: provided by the overlay loader. */
#if defined(TARGET_PC)
extern NJS_OBJECT M_SBMOT_Tails_Snowboard;
extern NJS_TEXLIST M_SBMOT_texlist_0CB025A0;
#define TAILS_SNOWBOARD_OBJECT  (&M_SBMOT_Tails_Snowboard)
#define TAILS_SNOWBOARD_TEXLIST (&M_SBMOT_texlist_0CB025A0)
#else
#define TAILS_SNOWBOARD_OBJECT  ((NJS_OBJECT *)0x0CB029C4)
#define TAILS_SNOWBOARD_TEXLIST ((NJS_TEXLIST *)0x0CB025A0)
#endif

/* face task (code_8C07B080.c) */
void func_8C07B0F6(task *tp);
void func_8C07B106(task *tp);
/* Tails' snowboard task (same chunk, after this file) */
void func_8C0BF804(task *tp);
void func_8C0BF92C(task *tp);
void func_8C0BFADE(task *tp);

void func_8C0BF796(task *tp);

void func_8C0BF262(taskwk *twp, playerwk *pwp)
{
    Sint32 i;

    CWP(twp)->info->a = 4.0f;
    CWP(twp)->info->flag = (CWP(twp)->info->flag & ~3) | 0;
    CWP(twp)->info->flag = (CWP(twp)->info->flag & ~0xC) | (0 << 2);
    for (i = 1; i < 4; i++) {
        CWP(twp)->info[i].attr |= 0x10;
    }
    if (twp->flag & 0x100) {
        CWP(twp)->info->flag = (CWP(twp)->info->flag & ~3) | 1;
        CWP(twp)->info->flag = (CWP(twp)->info->flag & ~0xC) | (1 << 2);
    }
    switch (twp->mode) {
    case 20:
        CWP(twp)->info[1].attr &= ~0x10;
        CWP(twp)->info[2].attr &= ~0x10;
        CWP(twp)->info[1].center = *(NJS_POINT3 *)&pwp->free.f[0];
        CWP(twp)->info[2].center = *(NJS_POINT3 *)&pwp->free.f[3];
        break;
    case 15:
        CWP(twp)->info->a = 2.25f;
        CWP(twp)->info[3].attr &= ~0x10;
        CWP(twp)->info[3].center.x = twp->pos.x - njCos(twp->ang.y) * 2.0f;
        CWP(twp)->info[3].center.y = twp->pos.y + 8.25f;
        CWP(twp)->info[3].center.z = twp->pos.z - njSin(twp->ang.y) * 2.0f;
        break;
    }
}

void func_8C0BF3F4(task *tp)
{
    D_8C25F7FC->model = &D_8C25F7D4;
    D_8C267530->model = &D_8C267508;
}

/*
 * NON_MATCHING (the C is complete and functionally faithful). Sister
 * functions: Sonic func_8C0B2376 (code_8C0B2338.c, matched), Knuckles
 * func_8C0C726E (code_8C0C71A0.c) and Amy func_8C0DD16E (code_8C0DD166.c),
 * both NON_MATCHING with the same leftovers.
 *  - Translation unit: the original TU starts at func_8C0BEDA0
 *    (code_8C0BEDA0.c). Its pending pool entry (func_8C0C023E, used at
 *    8C0BF166) makes SHC dump a pool inside func_8C0BF262 (8C0BF360:
 *    func_8C0C023E, 4.0f); only then are the two pools of this function
 *    placed as in the original (after the case 0 BRA at 8C0BF4A4 and after
 *    the loop at 8C0BF6DC). In this file 4.0f stays pending, so the first
 *    pool is dumped after the FreeTask BRA and the second one after the
 *    decay, which changes branches and the 0.99f load. Compiled as one TU
 *    with code_8C0BEDA0.c, 269 of the 328 instructions line up.
 *  - What reproduces the original: "Uint8 pno" shared by the playermwp
 *    index and the GetPlayerWorkPtr argument (one MOV.B kept in R4,
 *    EXTU.B R4,R3; a Sint8/Sint32 pno gets R14 and the plain expression is
 *    CSEd with the playertwp test); "src = ...; n = ...;" with the njCos
 *    result used inline (pwp R4, TailsActionList R5); the |z| < 1.7 block
 *    written first (loop constants 1.2/0.25/2.5/0.3 in FR12-FR15,
 *    B_8C7A21D0 in a register, the two B_8C7A2190 temporaries in sp+0 and
 *    sp+4, as in the original); the loop matrices as (NJS_MATRIX_PTR)&B_x
 *    (not CSEd with the njUnitMatrix arguments); the rotations as
 *    "c ? njRotateZ(m, c) : (void)0"; SH_ONLY "x*x + y*y + z*z" for the
 *    sqrt argument (SHC reassociates it into FMUL y*y, FMAC x*x, FMAC z*z;
 *    PC_ONLY spells out that fused order).
 *  - Remaining: tp/morph/n get R8/R10/R9 (original R9/R8/R10); the n load
 *    is put into the BT/S delay slot of the switch (the original BT has
 *    none); the |z| < 1.7 block is laid out first (the original puts it
 *    after the |z| >= 1.7 blocks and branches to it with BT; writing
 *    "fabs(z) >= 1.7f" first gives that layout, but then 0.25/2.5 swap
 *    FR13/FR14 and B_8C7A2190 gets the register); spd.x/y/z land in
 *    FR15/FR14/FR13 (original FR13/FR14/FR15, all term orders tried);
 *    0.99f is loaded with MOV.L + LDS/FSTS ("0.98999996f * mwp->spd.y"
 *    gives the original MOVA, but then tp is spilled instead of pmwp); the
 *    cos result stays in FR0 (a Float local gives the original
 *    FMOV FR0,FR4 but swaps pwp/TailsActionList to R5/R4). Declaration
 *    order has no effect; "register" makes it worse.
 */
#ifdef NON_MATCHING
void func_8C0BF402(task *tp)
{
    Sint32 a;
    NJS_VECTOR v;
    taskwk *twp;
    motionwk *mwp;
    motionwk *pmwp;
    playerwk *pwp;
    NJS_MODEL *model;
    NJS_MODEL *morph;
    NJS_POINT3 *src;
    NJS_POINT3 *dst;
    Uint32 n;
    Uint32 i;
    Float spd;
    Uint8 pno;

    twp = tp->twp;
    mwp = tp->mwp;
    if (playertwp[(Uint8)twp->counter.b[0]] == NULL) {
        FreeTask(tp);
        return;
    }
    pno = twp->counter.b[0];
    pmwp = playermwp[pno];
    pwp = GetPlayerWorkPtr(pno);
    morph = &miles_head_z;
    dst = morph->points;
    if (TailsActionList[pwp->mj.reqaction].actptr->object == &Tails) {
        model = &D_8C25F7D4;
    } else if (TailsActionList[pwp->mj.reqaction].actptr->object == &Tails_Flight) {
        model = &D_8C267508;
    } else {
        return;
    }
    src = model->points;
    n = model->nbPoint;
    switch (twp->mode) {
    case 0:
        twp->mode = 1;
        tp->dest = func_8C0BF3F4;
        break;
    case 1:
        spd = njSqrt(SH_ONLY(pmwp->spd.x * pmwp->spd.x + pmwp->spd.y * pmwp->spd.y + pmwp->spd.z * pmwp->spd.z)
                     PC_ONLY(SA1_FMAC(pmwp->spd.z, pmwp->spd.z, SA1_FMAC(pmwp->spd.x, pmwp->spd.x, pmwp->spd.y * pmwp->spd.y)))) * 2.0f;
        if (spd > _builtin_fabsf(mwp->spd.y)) {
            if (spd < 0.0f) {
                spd = -spd;
            }
            if (spd > 4.0f) {
                spd = 4.0f;
            }
            mwp->spd.y = spd;
        }
        if (mwp->spd.y >= 0.19999999f || mwp->spd.y <= -0.19999999f) {
            mwp->spd.y *= 0.98999996f;
        } else {
            mwp->spd.y = 0.0f;
        }
        a = SA1_FTOI(_builtin_fabsf(mwp->spd.y * 512.0f)) + 0x800;
        if (a > 0xC00) {
            a = 0xC00;
        }
        mwp->ang_spd.z += a;
        twp->ang.z = SA1_FTOI(njCos(mwp->ang_spd.z) * mwp->spd.y * 819.19997f);
        njUnitMatrix(B_8C7A2190);
        twp->ang.z ? njRotateZ(B_8C7A2190, twp->ang.z) : (void)0;
        njUnitMatrix(B_8C7A21D0);
        twp->ang.z >> 1 ? njRotateZ(B_8C7A21D0, twp->ang.z >> 1) : (void)0;
        for (i = 0; i < n; i++, src++, dst++) {
            if (_builtin_fabsf(src->z) < 1.69999996f) {
                if (src->x > 2.1999999f) {
                    v.x = src->x + 0.25f;
                    v.y = src->y;
                    v.z = src->z;
                    njCalcVector((NJS_MATRIX_PTR)&B_8C7A21D0, &v, dst);
                    dst->x -= 0.25f;
                }
            } else if (src->y < -3.0f) {
                v.x = src->x + 0.29999999f;
                v.y = src->y + 1.19999996f;
                v.z = src->z;
                njCalcVector((NJS_MATRIX_PTR)&B_8C7A2190, &v, dst);
                dst->x -= 0.29999999f;
                dst->y -= 1.19999996f;
            } else {
                v.x = src->x + 2.5f;
                v.y = src->y;
                v.z = src->z;
                njCalcVector((NJS_MATRIX_PTR)&B_8C7A2190, &v, dst);
                dst->x -= 2.5f;
            }
        }
        break;
    default:
        DestroyTask(tp);
        break;
    }
    D_8C25F7FC->model = morph;
    D_8C267530->model = morph;
}
#else
ASM_STUB(void func_8C0BF402(task *tp))
#endif

void func_8C0BF76A(task *tp)
{
    PL_FACE *fp;

    if ((fp = CAlloc(1, sizeof(PL_FACE))) != NULL) {
        tp->mwp = (motionwk *)fp;
        fp->object = &Tails_EV_Head;
        fp->motion = &miles_face_m_head;
        tp->dest = func_8C07B0F6;
        tp->exec = func_8C0BF796;
    }
}

void func_8C0BF796(task *tp)
{
    PL_FACE *fp;
    taskwk *ptwp;

    fp = (PL_FACE *)tp->mwp;
    ptwp = playertwp[(Uint8)tp->twp->counter.b[0]];
    if (fp == NULL || ptwp == NULL) {
        FreeTask(tp);
        return;
    }
    if (!(ptwp->timer.b[3] & 0x20)) {
        fp->parent = &D_8C25F830;
        fp->sibling = D_8C25F7FC;
    } else {
        fp->parent = &D_8C267564;
        fp->sibling = D_8C267530;
    }
    func_8C07B106(tp);
}

void func_8C0BF7CE(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    twp->timer.ptr = TAILS_SNOWBOARD_OBJECT;
    twp->value.ptr = TAILS_SNOWBOARD_TEXLIST;
    tp->mwp->weight = 0.02f;
    func_8C608C82(twp->value.ptr);
    tp->dest = func_8C0BFADE;
    tp->exec = func_8C0BF804;
    tp->disp = func_8C0BF92C;
}
