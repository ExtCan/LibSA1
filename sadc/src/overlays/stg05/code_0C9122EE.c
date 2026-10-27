/*
 * STG05 0C9122EE: Red Mountain act setup and the rock pillar object
 * (chunk 0C911DC0-0C912860, functions 5-10 of 10, plus two functions the
 * chunk list does not know about).
 *
 *  func_0C9122EE  act setup (called by func_0C911E0A and func_0C91253C):
 *                 keeps the task's taskwk in D_0CADA294, clears the lava
 *                 texture scroll counters, then per act (GetStageNumber()
 *                 & 0xFF) creates the stage child tasks, sets the fog
 *                 (njSetFogColor / njGenerateFogTable3 / njSetFogTable /
 *                 njFogEnable on the table D_0CADD5CC), creates the
 *                 func_0C9057B8 task and the func_0C911DC0 child (mode 73
 *                 or 74, wtimer 3) and starts the looping ambient sound
 *                 946/948; act 0 and 2 also create the func_8C046120 light
 *                 task at (0, 10000, 0); act 2 adds func_0C913028 for
 *                 Knuckles (player number 3), act 1 the empty func_0C9127EC
 *                 task. twp->mode becomes 1 in every case.
 *  func_0C912534  (no symbol) D_0CADA294 = NULL
 *  func_0C91253C  (no symbol, tail-called by func_0C911E0A) stage restart:
 *                 reset helpers, then func_8C098E60(tp) and the act setup
 *  func_0C91257A  rock pillar init (STG05_Rock_Pillar, texlist D_0CA39524):
 *                 copies the model into a pooled NJS_OBJECT
 *                 (func_8C052A1E) and registers it as moving land
 *                 collision with an attribute chosen from the model radius
 *                 (> 20, > 10, > 3); same code as the generic 1ST_READ
 *                 object init func_8C059482. Returns 0.
 *  func_0C9126E0  dest: unregister and free the NJS_OBJECT
 *  func_0C912702  exec: follow the task position/angle, taskwk.flag 0x100
 *                 while a player is within r + 16 of it; display while the
 *                 task is still alive (dest unchanged)
 *  func_0C912784  disp: texture, translate, rotate Z/X/Y, draw the object
 *  func_0C9127EC  empty exec
 *
 * Status: func_0C9126E0, func_0C912702, func_0C912784 and func_0C9127EC
 * match. func_0C9122EE, func_0C912534, func_0C91253C and func_0C91257A are
 * complete C kept under NON_MATCHING because they cannot match in this file:
 *  1. Function boundaries: the chunk function list (and the STG05 symbols)
 *     have no entries for 0C912534 and 0C91253C, so the original side of
 *     func_0C9122EE runs on to 0C91257A and includes both functions (35
 *     extra instructions). Needs func_0C912534 = 0x0C912534 and
 *     func_0C91253C = 0x0C91253C in config/symbols.stg05.txt and a re-split.
 *  2. Symbols: the pool entries H'0CADD4C4, H'0CADD4C8, H'0CADD5CC (bss
 *     past the end of STG05.BIN) have no names; needs D_0CADD4C4,
 *     D_0CADD4C8 (Sint32) and D_0CADD5CC (Float[128]) in the symbol file.
 *  3. Translation unit: the original pools are shared across the whole
 *     chunk. The first pool of func_0C9122EE (0C912420) starts with the
 *     two pending entries of func_0C911F5C (njDrawSprite2D,
 *     njSetConstantMaterial), which makes SHC dump it after act 1, so act 2
 *     is out of BT range ("BF +6; BRA act2; BRA default"); the next pool
 *     (0C912640, act 2 + both unnamed functions + func_0C91257A) is dumped
 *     inside func_0C91257A ("TST R0,R0; BT +6; BRA end"). Only a single C
 *     file for the whole chunk 0C911DC0-0C912860 (with func_0C911DC0 ..
 *     func_0C911F5C) can reproduce that.
 * Checked with an aligned compare against the chunk (pool values resolved,
 * NON_MATCHING compile): func_0C912534 and func_0C91253C are identical;
 * func_0C9122EE differs only in the act dispatch above (3), in which of
 * two equally used addresses SHC keeps in R10 (original CreateElementalTask,
 * here func_0C9057B8; declaration orders, prototype placement, &func,
 * casts and statement forms tried) and in the R2/R3 temporaries of the
 * act 1 "mode = 74" store; func_0C91257A differs only in (3) and in the
 * schedule of its first five instructions (the original stores timer/value
 * before the register saves).
 */
#include "common.h"
#include "sa1/sa1.h"

extern taskwk *D_0CADA294;          /* taskwk of the act setup task (cleared by func_0C912534) */
extern Sint32 D_0CADD4C4;           /* lava texture scroll counters (no symbol yet: H'0CADD4C4) */
extern Sint32 D_0CADD4C8;           /* (H'0CADD4C8); both decremented and wrapped by 0C912860 code */
extern Float D_0CADD5CC[128];       /* fog table, NJS_FOG_TABLE (H'0CADD5CC) */
extern NJS_OBJECT STG05_Rock_Pillar;
extern NJS_TEXLIST D_0CA39524;      /* rock pillar texlist */

void func_0C90A6BC(void);           /* creates two func_0C90A6D4 tasks */
void func_0C9131AA(task *tp);
void func_0C9057B8(task *tp);
void func_0C913028(task *tp);       /* kill collision task (code_0C912E32.c) */
void func_0C911DC0(task *tp);

void func_8C63FE20(Uint32 color);                       /* njSetFogColor */
void func_8C63FB48(Float *fog, Float n, Float f);       /* njGenerateFogTable3 */
void func_8C63FE48(Float *fog);                         /* njSetFogTable */
void func_8C046120(NJS_POINT3 *pos);                    /* light task at pos */
Sint32 func_8C0941FC(Sint32 se, SA1_UPTR id, Sint32 pri, Sint32 volofs); /* looping SE (dsPlay_iloop-like); id: owner or -1 */
void func_8C107AAA(void);
void func_8C051C6A(Sint32 n);
void func_8C0918E6(Sint32 n);
void func_8C01976C(Sint32 n);
void func_8C04F84E(Sint32 n);
void func_8C04F8FA(taskwk *twp);
void func_8C012240(void);
Sint32 func_8C099538(void);         /* sets the forced texlist and returns 1 when there is one */
Sint32 func_8C07CD16(NJS_POINT3 *pos, Float r);
NJS_OBJECT *func_8C052A1E(void);
void func_8C052A5E(NJS_OBJECT *obj);
void func_8C0528AE(Uint32 attr, task *tp, NJS_OBJECT *obj);
void func_8C052940(task *tp, NJS_OBJECT *obj);

void func_0C9126E0(task *tp);
void func_0C912702(task *tp);
void func_0C912784(task *tp);
void func_0C9127EC(task *tp);

#ifdef NON_MATCHING
void func_0C9122EE(task *tp)
{
    taskwk *twp;
    task *ctp;
    NJS_POINT3 v;
    Uint16 stage;

    twp = tp->twp;
    D_0CADA294 = twp;
    D_0CADD4C4 = D_0CADD4C8 = 0;
    stage = GetStageNumber();
    switch ((Uint8)stage) {
    case 0:
        twp->mode = 1;
        func_0C90A6BC();
        CreateChildTask(2, func_0C9131AA, tp);
        func_8C63FE20(0xFFFFFFFF);
        func_8C63FB48(D_0CADD5CC, -550.0f, -9600.0f);
        func_8C63FE48(D_0CADD5CC);
        func_8C63FB18();
        CreateElementalTask(2, 3, func_0C9057B8);
        v.x = 0.0f;
        v.y = 10000.0f;
        v.z = 0.0f;
        func_8C046120(&v);
        ctp = CreateChildTask(2, func_0C911DC0, tp);
        ctp->twp->mode = 73;
        ctp->twp->wtimer = 3;
        func_8C0941FC(946, -1, 8, 0);
        break;
    case 1:
        twp->mode = 1;
        CreateChildTask(2, func_0C9131AA, tp);
        func_8C63FE20(0xFF000000);
        func_8C63FB48(D_0CADD5CC, -850.0f, -1200.0f);
        func_8C63FE48(D_0CADD5CC);
        func_8C63FB18();
        CreateElementalTask(2, 3, func_0C9057B8);
        ctp = CreateChildTask(2, func_0C911DC0, tp);
        ctp->twp->mode = 74;
        ctp->twp->wtimer = 3;
        func_8C0941FC(948, -1, 8, 0);
        CreateChildTask(0, func_0C9127EC, tp);
        break;
    case 2:
        twp->mode = 1;
        if (GetPlayerNumber() == 3) {
            CreateChildTask(6, func_0C913028, tp);
        }
        CreateChildTask(2, func_0C9131AA, tp);
        func_8C63FE20(0xFFFFFFFF);
        func_8C63FB48(D_0CADD5CC, -500.0f, -5000.0f);
        func_8C63FE48(D_0CADD5CC);
        func_8C63FB18();
        CreateElementalTask(2, 3, func_0C9057B8);
        v.x = 0.0f;
        v.y = 10000.0f;
        v.z = 0.0f;
        func_8C046120(&v);
        ctp = CreateChildTask(2, func_0C911DC0, tp);
        ctp->twp->mode = 73;
        ctp->twp->wtimer = 3;
        func_8C0941FC(946, -1, 8, 0);
        break;
    default:
        twp->mode = 1;
        break;
    }
}

/* 0C912534, no symbol: probably the act setup task's dest */
void func_0C912534(task *tp)
{
    PC_ONLY((void)tp;)
    D_0CADA294 = NULL;
}

/* 0C91253C, no symbol (func_0C911E0A tail-calls it). "register" puts tp in
 * R14 as in the original; without it SHC keeps tp on the stack. */
void func_0C91253C(register task *tp)
{
    func_8C107AAA();
    func_8C051C6A(1);
    func_8C0918E6(1);
    func_8C01976C(1);
    func_8C04F84E(1);
    func_8C04F8FA(B_8C78C608[0]);
    func_8C012240();
    func_8C098E60(tp);
    func_0C9122EE(tp);
}

/*
 * Generic part of the init (the code of 1ST_READ func_8C059482 from its
 * func_8C042DC4 test on), inlined: as a separate inline body its parameter
 * stays in R4, so the timer/value stores of func_0C91257A use R4/R5 and the
 * func_8C042DC4 call needs no "MOV R13,R4", as in the original. The
 * attribute is assigned before the radius tests: only then does SHC keep
 * it in R14 and branch with "BT/S join; MOV #1,R14".
 */
#ifndef TARGET_PC
#pragma inline(func_0C91257A_init)
#endif
static Sint32 func_0C91257A_init(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;
    Sint32 attr;

    twp = tp->twp;
    if (!func_8C042DC4(tp)) {
        obj = func_8C052A1E();
        twp->counter.ptr = obj;
        obj->evalflags = ((NJS_OBJECT *)twp->timer.ptr)->evalflags & ~3;
        obj->scl[0] = 1.0f;
        obj->scl[1] = 1.0f;
        obj->scl[2] = 1.0f;
        obj->ang[0] = twp->ang.x;
        obj->ang[1] = twp->ang.y;
        obj->ang[2] = twp->ang.z;
        obj->pos[0] = twp->pos.x;
        obj->pos[1] = twp->pos.y;
        obj->pos[2] = twp->pos.z;
        obj->model = ((NJS_OBJECT *)twp->timer.ptr)->model;
        obj->child = ((NJS_OBJECT *)twp->timer.ptr)->child;
        obj->sibling = ((NJS_OBJECT *)twp->timer.ptr)->sibling;
        attr = 0x00000001;
        if (obj->model->r > 20.0f) {
            /* attr 0x00000001 */
        } else if (obj->model->r > 10.0f) {
            attr = 0x20000001;
        } else if (obj->model->r > 3.0f) {
            attr = 0x40000001;
        } else {
            attr = 0x60000001;
        }
        func_8C0528AE(attr, tp, obj);
    }
    tp->dest = func_0C9126E0;
    tp->exec = func_0C912702;
    tp->disp = func_0C912784;
    return 0;
}

Sint32 func_0C91257A(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    twp->timer.ptr = &STG05_Rock_Pillar;
    twp->value.ptr = &D_0CA39524;
    return func_0C91257A_init(tp);
}
#else
ASM_STUB(void func_0C9122EE(task *tp))
#endif

void func_0C9126E0(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (twp->counter.ptr) {
        func_8C052940(tp, (NJS_OBJECT *)twp->counter.ptr);
        func_8C052A5E((NJS_OBJECT *)twp->counter.ptr);
    }
}

void func_0C912702(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;

    twp = tp->twp;
    obj = (NJS_OBJECT *)twp->counter.ptr;
    if (obj) {
        obj->pos[0] = twp->pos.x;
        obj->pos[1] = twp->pos.y;
        obj->pos[2] = twp->pos.z;
        obj->ang[0] = twp->ang.x;
        obj->ang[1] = twp->ang.y;
        obj->ang[2] = twp->ang.z;
        if (func_8C07CD16(&twp->pos, obj->model->r + 16.0f)) {
            twp->flag |= 0x100;
        } else {
            twp->flag &= ~0x100;
        }
    }
    if (tp->dest == func_0C9126E0) {
        func_0C912784(tp);
    }
}

void func_0C912784(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (!func_8C099538()) {
        njSetTexture((NJS_TEXLIST *)twp->value.ptr);
    }
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    /* func_8C036F12 draws an NJS_OBJECT tree (header declares NJS_MODEL *) */
    func_8C036F12((NJS_MODEL *)twp->timer.ptr);
    njPopMatrix(1);
}

void func_0C9127EC(task *tp)
{
    PC_ONLY((void)tp;)
}
