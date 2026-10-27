/*
 * 1ST_READ 8C0BF804-8C0BFD40 (chunk 8C0BEDA0, functions 8-13 of 18): Tails.
 *  func_8C0BF804  exec of Tails' snowboard task (set up by 8C0BF7CE: model
 *                 twp->timer.ptr, texlist twp->value.ptr, mwp->weight 0.02).
 *                 Frees itself when player slot counter.b[0] is empty.
 *                 While the player's action is 107..127 (snowboarding) it is
 *                 held: pos = player's first collision shape center, ang =
 *                 player's (y mirrored), mwp->spd = player's. Otherwise it
 *                 moves as a free object (func_8C05872A, with pos.y offset by
 *                 the model's -5.19 base). Tail-calls the display.
 *  func_8C0BF92C  display: model (free) or, while held, the action
 *                 D_8C2579BC[action - 107] at the player's frame.
 *  func_8C0BFADE  dest: release the texlist.
 *  func_8C0BFAEE  per-level setup (levels 4, 2/6/9/8, default; Sand Hill 38
 *                 changes player 0's height).
 *  func_8C0BFCA2  play a sound effect unless the player is number 1.
 *  func_8C0BFCC0  sound 15 on an object of class 0x400 unless player 1.
 *
 * Matching notes: func_8C0BF804 needs "register" on twp, ptwp and action
 * (without it SHC gives action R5 and the EXTS.W temporary R4; with it on
 * action alone tp/ptwp and the two stack slots swap). The rotations are the
 * "a ? njRotateZ(NULL, a) : (void)0" form (the angle goes through the 4-byte
 * stack slot); "action -= 107" (not a cast) gives func_8C0BF92C's register
 * order; B_8C78B451[0] (array) loads its address into R0; func_8C0BFCC0
 * needs one && condition. The literal pools need the layout model below.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

extern NJS_ACTION *D_8C2579BC[];    /* actions of the held object, by player action - 107 */
extern PATHTAG D_8C1BD2C4;          /* path (65 points) */
extern Uint8 B_8C78B451[];

void func_8C0213E0(PATHTAG *path, Sint32 a, Sint32 b);
void func_8C04EF22(Sint32 n);
void func_8C04EF52(Sint32 n);
task *func_8C062704(void);
Sint32 func_8C062620(void);
void func_8C0818CE(void);
void func_8C0818D6(void);
Sint32 func_8C0818DE(void);
void func_8C0818E4(task *tp);

void func_8C0BF804(task *tp);
void func_8C0BF92C(task *tp);
void func_8C0BFADE(task *tp);
void func_8C0BFD82(Sint32 n);

#if !defined(TARGET_PC)
/*
 * Translation-unit layout model (SHC only; never spliced: these names have
 * no address in the image). The original object starts before this file
 * (code_8C0BF262.c holds functions 2-7) and SHC shares one literal pool
 * across it: when func_8C0BF804 starts, the entries of the end of
 * func_8C0BF402 (first use 8C0BF740, right after the pool at 8C0BF714) and
 * of func_8C0BF76A/796/7CE are pending, and the pool is dumped inside
 * func_8C0BF804 (8C0BF860, after the FreeTask tail call). That decides
 * where the later pools fall (8C0BFA60 inside func_8C0BF92C, 8C0BFB94
 * inside func_8C0BFAEE, which gives case 4's far "BF; BRA; NOP" return).
 * tu_model_8C0BF740 stands in for the end of func_8C0BF402 (the 32-byte
 * aligned code 8C0BF740-8C0BF769 after that pool) and the other three are
 * copies of the functions matched in code_8C0BF262.c (in the copy of
 * func_8C0BF7CE two other symbols stand in for the absolute M_SBMOT
 * addresses: only the number of pool entries matters). With the model the
 * pools fall where they do in the original and all six functions match in
 * the same build.
 */
extern NJS_MODEL D_8C25F7D4;
extern NJS_OBJECT D_8C25F7FC[];
extern NJS_OBJECT D_8C25F830;
extern NJS_MODEL D_8C267508;
extern NJS_OBJECT D_8C267530[];
extern NJS_OBJECT D_8C267564;
extern NJS_OBJECT Tails_EV_Head;
extern NJS_MOTION miles_face_m_head;
void func_8C07B0F6(task *tp);
void func_8C07B106(task *tp);
void func_8C0BF796(task *tp);

void tu_model_8C0BF740(task *tp, NJS_MODEL *morph)
{
    DestroyTask(tp);
    D_8C25F7FC->model = morph;
    D_8C267530->model = morph;
}

void tu_model_8C0BF76A(task *tp)
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

void tu_model_8C0BF796(task *tp)
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

void tu_model_8C0BF7CE(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    twp->timer.ptr = &D_8C25F7D4;   /* stands in for the object 0x0CB029C4 */
    twp->value.ptr = &D_8C267508;   /* stands in for the texlist 0x0CB025A0 */
    tp->mwp->weight = 0.02f;
    func_8C608C82(twp->value.ptr);
    tp->dest = func_8C0BFADE;
    tp->exec = func_8C0BF804;
    tp->disp = func_8C0BF92C;
}
#endif

void func_8C0BF804(task *tp)
{
    register taskwk *twp;
    register taskwk *ptwp;
    mtnjvwk *mjp;
    motionwk *pmwp;
    motionwk *mwp;
    register Sint16 action;
    Uint8 pno;
    Float ofs;

    twp = tp->twp;
    pno = twp->counter.b[0];
    ptwp = playertwp[pno];
    pmwp = playermwp[pno];
    mwp = tp->mwp;
    mjp = &GetPlayerWorkPtr(pno)->mj;
    action = mjp->action;
    if (ptwp == NULL) {
        FreeTask(tp);
        return;
    }
    if (action < 107 || action > 127) {
        ofs = -5.190199f;
        twp->pos.y += ofs;
        func_8C05872A(tp);
        twp->pos.y -= ofs;
    } else {
        twp->pos = CWP(ptwp)->info->center;
        twp->ang.x = ptwp->ang.x;
        twp->ang.y = 0x8000 - ptwp->ang.y;
        twp->ang.z = ptwp->ang.z;
        mwp->spd = pmwp->spd;
    }
    func_8C0BF92C(tp);
}
void func_8C0BF92C(task *tp)
{
    taskwk *twp;
    mtnjvwk *mjp;
    NJS_ACTION *act;
    Sint16 action;
    NJS_MOTION *motion;

    twp = tp->twp;
    mjp = &GetPlayerWorkPtr(twp->counter.b[0])->mj;
    action = mjp->action;
    if (!func_8C038D00(twp->pos.x, twp->pos.y, twp->pos.z, 12.0f)) {
        return;
    }
    njSetTexture(twp->value.ptr);
    njPushMatrix(NULL);
    if (action < 107 || action > 127) {
        njTranslateV(NULL, &twp->pos);
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        njTranslate(NULL, 0.0f, -5.190199f, 0.0f);
        njDrawModel(((NJS_OBJECT *)twp->timer.ptr)->model);
    } else {
        action -= 107;
        act = D_8C2579BC[action];
        njTranslateV(NULL, &twp->pos);
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        if (act != NULL) {
            motion = act->motion;
            if (twp->flag & 2) {
                njTranslate(NULL, 0.0f, -5.190199f, 0.0f);
            }
            func_8C60E080(twp->timer.ptr, motion, mjp->nframe);
        } else {
            if (twp->flag & 2) {
                njTranslate(NULL, 0.0f, -5.190199f, 0.0f);
            }
            njDrawObject(twp->timer.ptr);
        }
    }
    njPopMatrix(1);
}

void func_8C0BFADE(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    func_8C6088BC(twp->value.ptr);
}

/*
 * Translation-unit context: in the original the literal pool of
 * func_8C0BF92C/func_8C0BFADE is dumped after the case 4 tail call
 * (8C0BFB94), so case 4's "return" branch is out of BT range ("BF; BRA;
 * NOP"), and the two identical tails are not cross-jumped. The layout model
 * at the top of the file reproduces that pool placement.
 */
void func_8C0BFAEE(void)
{
    playerwk *pwp;
    Sint32 level;

    level = (Uint8)(GetStageNumber() >> 8);
    switch (level) {
    case 4:
        if (func_8C0818DE() == 1) {
            func_8C0213E0(&D_8C1BD2C4, 3, 720);
            func_8C04EF22(1348);
            func_8C04EF52(75);
        } else {
            func_8C0BFD82(225);
        }
        if (tcrf_LevelSelectVar == 9 && B_8C78B451[0] == 1) {
            return;
        }
        CreateElementalTask(2, 3, func_8C0818E4);
        break;
    case 2:
    case 6:
    case 9:
    case 8:
        if (func_8C062704() != NULL) {
            if (func_8C062620() != 1) {
                func_8C0818CE();
            } else {
                func_8C0818D6();
            }
        }
        if (func_8C0818DE() == 1) {
            func_8C0213E0(&D_8C1BD2C4, 3, 720);
            func_8C04EF22(1348);
            func_8C04EF52(75);
        } else {
            func_8C0BFD82(214);
        }
        if (tcrf_LevelSelectVar == 9 && B_8C78B451[0] == 1) {
            return;
        }
        CreateElementalTask(2, 3, func_8C0818E4);
        break;
    default:
        func_8C0818CE();
        if (level == 38) {
            pwp = GetPlayerWorkPtr(0);
            pwp->p.height = 9.0f;
            pwp->p.center_height = pwp->p.height * 0.5f;
        }
        func_8C0213E0(&D_8C1BD2C4, 3, 720);
        func_8C04EF22(1348);
        func_8C04EF52(75);
        break;
    }
}

void func_8C0BFCA2(taskwk *twp, Sint32 se)
{
    if ((Uint8)twp->counter.b[0] != 1) {
        func_8C0940DA(se, 0, 0, 0);
    }
}

void func_8C0BFCC0(taskwk *twp)
{
    if ((Uint8)twp->counter.b[0] != 1 && (func_8C06C880(twp) & 0x400)) {
        func_8C094330(15, twp, 1, 0, 2);
    }
}

ASM_STUB(void func_8C0BFD82(Sint32 n))
