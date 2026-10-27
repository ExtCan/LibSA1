/*
 * STG05 0C912E32: Egg Carrier light-direction debug task and the kill
 * collision object (chunk 0C912860-0C913774, functions 7-11 of 16, plus the
 * unlisted function at 0C91317C).
 *
 *  func_0C912E32  exec (no static callers): debug light direction on pad
 *                 port B (B_8C754DA8[1]). timer.w[0] / timer.w[1] are two
 *                 BAMS angles (initialised to 0 / 0x8000); X/A raise/lower
 *                 w[0], Y/B raise/lower w[1] by 1 (L trigger 0x20000),
 *                 0x100 (R trigger 0x10000) or 16 (no trigger). The light
 *                 vector (cos w0, sin w0, 0) is rotated about Y by w1 and
 *                 passed to func_8C63AE90 (light direction of B_8C760810).
 *  func_0C913028  exec (its address is in func_0C9122EE's pool): keeps
 *                 timer.ptr = &STG05_Egg_Carrier_Kill_Collision; mode 0
 *                 clears pos, registers the collision object (func_0C913102)
 *                 and installs disp/dest; mode 1 tail-calls func_0C91317C.
 *  func_0C9130D8  disp: only re-stores timer.ptr.
 *  func_0C9130E0  dest: unregisters and frees the pooled NJS_OBJECT
 *                 (counter.ptr).
 *  func_0C913102  allocates a pooled NJS_OBJECT (counter.ptr), copies
 *                 model/child/sibling of the source object (timer.ptr), sets
 *                 pos from twp->pos and registers it (func_8C0528AE, 0x1001).
 *  func_0C91317C  (no symbol: reached only by the BRA tail call of
 *                 func_0C913028, so the splitter merged it into
 *                 func_0C913102's range 0C913102-0C9131AA) copies twp->pos
 *                 into the object and sets twp->flag 0x100.
 *
 * Matching notes:
 *  - func_0C912E32 matches: the third test of each button block is
 *    "!((on & L) | (on & R))" written with two B_8C754DA8[1]->on reads; SHC
 *    keeps the CSE temporaries of B_8C754DA8[1] (four slots @(16..4,R15))
 *    and of ->on (@R15) on the stack, as in the original. The rotation is
 *    the "(Angle)a ? njRotateY(NULL, (Angle)a) : (void)0" expression
 *    statement (32-bit CSE temporary in the @R15 slot: MOV.L, not MOV.W).
 *  - func_0C9130D8 matches with the twp / obj locals in this order.
 *  - func_0C913028 (NON_MATCHING): the three timer.ptr stores are inline
 *    expansions (MOV R14,R5 parameter copies); the original keeps
 *    &STG05_Egg_Carrier_Kill_Collision in R4 for the whole function and has a
 *    dead "MOV R4,R5" in the BT/S delay slot. The closest form found (10 of
 *    47 instructions differ, register allocation only) inlines
 *    func_0C9130D8 at the top and a two-parameter helper in the cases with a
 *    local obj: SHC then keeps obj in R2 and the parameter copy in R4
 *    instead of R4 / R5. Tried: explicit stores, 1-parameter inline in all
 *    three places (8 bodies), helper parameter order, obj before/after,
 *    declaration orders, if/else instead of switch. Even when the
 *    registers match, shcdiff cannot match the BRA to func_0C91317C until
 *    the split gives 0C91317C a symbol.
 *  - func_0C913102 + func_0C91317C are instruction-identical to
 *    0C913102-0C9131AA (verified by comparing the concatenated listing),
 *    but shcdiff reports DIFF because its range for func_0C913102 includes
 *    the unlisted function, so func_0C913102 stays behind NON_MATCHING
 *    (splice would fail the build otherwise). func_0C91317C is plain C:
 *    splice ignores it until the split lists it. Once 0C91317C is a
 *    function in the manifest, both can lose the guard.
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_OBJECT STG05_Egg_Carrier_Kill_Collision;
extern NJS_MATRIX B_8C760810;   /* light of func_8C63AE90 (also code_8C011EA6.c: NJS_MATRIX view) */

void func_8C63AE90(NJS_MATRIX *m, Float x, Float y, Float z);  /* SDK: set light direction (code_8C011EA6.c prototype) */
NJS_OBJECT *func_8C052A1E(void);                        /* allocate a pooled NJS_OBJECT (code_8C052A00.c) */
void func_8C052A5E(NJS_OBJECT *obj);                    /* free it */
void func_8C0528AE(Uint32 attr, task *tp, NJS_OBJECT *obj); /* register a collision object (code_8C052518.c) */
void func_8C052940(task *tp, NJS_OBJECT *obj);          /* unregister it */

void func_0C9130D8(task *tp);
void func_0C9130E0(task *tp);
void func_0C913102(task *tp);
void func_0C91317C(task *tp);

void func_0C912E32(task *tp)
{
    taskwk *twp;
    NJS_VECTOR out;
    NJS_VECTOR v;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        twp->timer.w[0] = 0;
        twp->timer.w[1] = -0x8000;  /* 0x8000: 180 degrees */
        twp->mode = 1;
        /* fall through */
    case 1:
        if (B_8C754DA8[1]->on & 0x400) {
            if (B_8C754DA8[1]->on & 0x20000) {
                twp->timer.w[0] += 1;
            }
            if (B_8C754DA8[1]->on & 0x10000) {
                twp->timer.w[0] += 0x100;
            }
            if (!((B_8C754DA8[1]->on & 0x20000) | (B_8C754DA8[1]->on & 0x10000))) {
                twp->timer.w[0] += 16;
            }
        }
        if (B_8C754DA8[1]->on & 0x4) {
            if (B_8C754DA8[1]->on & 0x20000) {
                twp->timer.w[0] -= 1;
            }
            if (B_8C754DA8[1]->on & 0x10000) {
                twp->timer.w[0] -= 0x100;
            }
            if (!((B_8C754DA8[1]->on & 0x20000) | (B_8C754DA8[1]->on & 0x10000))) {
                twp->timer.w[0] -= 16;
            }
        }
        if (B_8C754DA8[1]->on & 0x200) {
            if (B_8C754DA8[1]->on & 0x20000) {
                twp->timer.w[1] += 1;
            }
            if (B_8C754DA8[1]->on & 0x10000) {
                twp->timer.w[1] += 0x100;
            }
            if (!((B_8C754DA8[1]->on & 0x20000) | (B_8C754DA8[1]->on & 0x10000))) {
                twp->timer.w[1] += 16;
            }
        }
        if (B_8C754DA8[1]->on & 0x2) {
            if (B_8C754DA8[1]->on & 0x20000) {
                twp->timer.w[1] -= 1;
            }
            if (B_8C754DA8[1]->on & 0x10000) {
                twp->timer.w[1] -= 0x100;
            }
            if (!((B_8C754DA8[1]->on & 0x20000) | (B_8C754DA8[1]->on & 0x10000))) {
                twp->timer.w[1] -= 16;
            }
        }
    }
    v.x = njCos(twp->timer.w[0]);
    v.y = njSin(twp->timer.w[0]);
    v.z = 0.0f;
    njPushMatrix(D_8C67C640);
    (Angle)twp->timer.w[1] ? njRotateY(NULL, (Angle)twp->timer.w[1]) : (void)0;
    njCalcVector(NULL, &v, &out);
    njPopMatrix(1);
    func_8C63AE90(&B_8C760810, out.x, out.y, out.z);
}

#ifdef NON_MATCHING
#ifndef TARGET_PC
#pragma inline(func_0C9130D8, SetKillCollision)
#endif
static void SetKillCollision(task *tp, NJS_OBJECT *obj)
{
    taskwk *twp;

    twp = tp->twp;
    twp->timer.ptr = obj;
}

void func_0C913028(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;

    twp = tp->twp;
    func_0C9130D8(tp);
    obj = &STG05_Egg_Carrier_Kill_Collision;
    switch (twp->mode) {
    case 0:
        twp->pos.x = twp->pos.y = twp->pos.z = 0.0f;
        SetKillCollision(tp, obj);
        func_0C913102(tp);
        tp->disp = func_0C9130D8;
        tp->dest = func_0C9130E0;
        twp->mode = 1;
        break;
    case 1:
        SetKillCollision(tp, obj);
        func_0C91317C(tp);
        break;
    }
}
#else
ASM_STUB(void func_0C913028(task *tp))
#endif

void func_0C9130D8(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;

    twp = tp->twp;
    obj = &STG05_Egg_Carrier_Kill_Collision;
    twp->timer.ptr = obj;
}

void func_0C9130E0(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (twp->counter.ptr) {
        func_8C052940(tp, twp->counter.ptr);
        func_8C052A5E(twp->counter.ptr);
    }
}

/* Instruction-identical to the original: kept under NON_MATCHING only because
 * the split has no symbol at 0C91317C, so shcdiff/splice compare this
 * function against 0C913102-0C9131AA, which includes func_0C91317C. */
#ifdef NON_MATCHING
void func_0C913102(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;

    twp = tp->twp;
    if (func_8C042DC4(tp) == 0) {
        twp->counter.ptr = obj = func_8C052A1E();
        obj->evalflags = NJD_EVAL_BREAK;
        obj->scl[0] = 1.0f;
        obj->scl[1] = 1.0f;
        obj->scl[2] = 1.0f;
        obj->ang[0] = 0;
        obj->ang[1] = 0;
        obj->ang[2] = 0;
        obj->pos[0] = twp->pos.x;
        obj->pos[1] = twp->pos.y;
        obj->pos[2] = twp->pos.z;
        obj->model = ((NJS_OBJECT *)twp->timer.ptr)->model;
        obj->child = ((NJS_OBJECT *)twp->timer.ptr)->child;
        obj->sibling = ((NJS_OBJECT *)twp->timer.ptr)->sibling;
        func_8C0528AE(0x1001, tp, obj);
    }
}
#else
ASM_STUB(void func_0C913102(task *tp))
#endif

void func_0C91317C(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;

    twp = tp->twp;
    obj = twp->counter.ptr;
    if (obj) {
        obj->pos[0] = twp->pos.x;
        obj->pos[1] = twp->pos.y;
        obj->pos[2] = twp->pos.z;
        twp->flag |= 0x100;
    }
}
