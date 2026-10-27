/*
 * 1ST_READ 8C0E7720-8C0E79D4 (chunk 8C0E6620; Big the Cat, fishing):
 *
 *  func_8C0E7720  lure task display (called by the lure task exec
 *                 func_8C0E943C): while the lure task B_8C7A40C8 exists and
 *                 player 0 is Big (exec BigMain), computes the line point
 *                 at the lure (func_8C0EA942) and the rod tip
 *                 (func_8C0E79D4; result unused), sets the fishing texlist
 *                 and render state (func_8C0EA9F6); unless the fishing bit
 *                 0x800 is set draws the lure model (D_8C565FC4 by number
 *                 of upgrades) at the lure position, turned by -ang.y,
 *                 ang.x, ang.z, offset (-0.3, 0.6, 0) before mode 3 and
 *                 scaled by 0.3. On the first logic step (B_8C754EC8 == 0)
 *                 from mode 3 on: func_8C0EC180 on the any work block
 *                 work[2] and the line (func_8C0E727A, func_8C0E72DA,
 *                 func_8C0E76A0 from player 0's position to the lure
 *                 point).
 *  func_8C0E78C0  the same without the lure task and step checks.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

extern task *B_8C7A40C8;        /* lure task (func_8C0E6DA0's result) */
extern Uint16 B_8C7A40E8;       /* fishing state bits (MOV.W + EXTU.W) */
extern NJS_TEXLIST texlist_FISHING;     /* 8C54CB4C */
extern NJS_OBJECT *D_8C565FC4[];        /* lure objects (_Big_Normal_Lure, ...) */

void BigMain(task *tp);
void func_8C0EA942(NJS_POINT3 *pos);    /* code_8C0EA942.c */
void func_8C0EA9F6(void);
void func_8C0EC180(void *p);
void func_8C0E79D4(task *tp, NJS_POINT3 *pos);  /* code_8C0E79D4.c */

/* the rotation is skipped for a zero angle; the angle goes through an
 * unsigned local, which SHC keeps in a stack slot (as in code_8C0876F4.c).
 * The conditional-expression form is needed: the if-statement form of
 * code_8C0E66C4.c swaps R2/R3 in the x and z rotations. */
#define ROTATEX(m, a) ((ang = (a)) != 0 ? njRotateX((m), ang) : (void)0)
#define ROTATEY(m, a) ((ang = (a)) != 0 ? njRotateY((m), ang) : (void)0)
#define ROTATEZ(m, a) ((ang = (a)) != 0 ? njRotateZ((m), ang) : (void)0)

ASM_STUB(Sint32 func_8C0E6E80(void))
ASM_STUB(void func_8C0E727A(void))
ASM_STUB(void func_8C0E72DA(void))
ASM_STUB(void func_8C0E76A0(NJS_POINT3 *a, NJS_POINT3 *b))

void func_8C0E7720(task *tp)
{
    NJS_POINT3 tip;
    NJS_POINT3 pos;
    taskwk *ptwp;
    Uint32 ang;
    taskwk *twp;

    twp = tp->twp;
    ptwp = playertwp[0];
    if (B_8C7A40C8 == NULL) {
        return;
    }
    if (GetPlayerTaskPointer(0)->exec != BigMain) {
        return;
    }
    func_8C0EA942(&pos);
    /* func_8C0E79D4 adds into *pos: the original passes an uninitialised
     * local and never reads the result */
    PC_ONLY(tip.x = tip.y = tip.z = 0.0f;)
    func_8C0E79D4(tp, &tip);
    njSetTexture(&texlist_FISHING);
    func_8C0EA9F6();
    if (!(B_8C7A40E8 & 0x800)) {
        njPushMatrix(NULL);
        njTranslate(NULL, twp->pos.x, twp->pos.y, twp->pos.z);
        ROTATEY(NULL, -twp->ang.y);
        ROTATEX(NULL, twp->ang.x);
        ROTATEZ(NULL, twp->ang.z);
        if (twp->mode < 3) {
            njTranslate(NULL, 0.59999998f, -0.29999999f, 0.0f);
        }
        njScale(NULL, 0.29999999f, 0.29999999f, 0.29999999f);
        /* func_8C036F12 takes an NJS_OBJECT * (code_8C036BC0.c); the cast
         * only bridges the NJS_MODEL * prototype in sa1/engine.h */
        func_8C036F12((NJS_MODEL *)D_8C565FC4[func_8C0E6E80()]);
        njPopMatrix(1);
    }
    if (B_8C754EC8 != 0) {
        return;
    }
    if (twp->mode >= 3) {
        func_8C0EC180(AWP(tp)->work[2].ptr);
    }
    if (tp->twp->mode >= 3) {
        njSetTexture(&texlist_FISHING);
        func_8C0E727A();
        func_8C0E72DA();
        func_8C0E76A0(&ptwp->pos, &pos);
    }
}

void func_8C0E78C0(task *tp)
{
    NJS_POINT3 tip;
    NJS_POINT3 pos;
    taskwk *ptwp;
    Uint32 ang;
    taskwk *twp;

    twp = tp->twp;
    ptwp = playertwp[0];
    if (GetPlayerTaskPointer(0)->exec != BigMain) {
        return;
    }
    func_8C0EA942(&pos);
    /* func_8C0E79D4 adds into *pos: the original passes an uninitialised
     * local and never reads the result */
    PC_ONLY(tip.x = tip.y = tip.z = 0.0f;)
    func_8C0E79D4(tp, &tip);
    njSetTexture(&texlist_FISHING);
    func_8C0EA9F6();
    if (!(B_8C7A40E8 & 0x800)) {
        njPushMatrix(NULL);
        njTranslate(NULL, twp->pos.x, twp->pos.y, twp->pos.z);
        ROTATEY(NULL, -twp->ang.y);
        ROTATEX(NULL, twp->ang.x);
        ROTATEZ(NULL, twp->ang.z);
        if (twp->mode < 3) {
            njTranslate(NULL, 0.59999998f, -0.29999999f, 0.0f);
        }
        njScale(NULL, 0.29999999f, 0.29999999f, 0.29999999f);
        /* func_8C036F12 takes an NJS_OBJECT * (code_8C036BC0.c); the cast
         * only bridges the NJS_MODEL * prototype in sa1/engine.h */
        func_8C036F12((NJS_MODEL *)D_8C565FC4[func_8C0E6E80()]);
        njPopMatrix(1);
    }
    if (twp->mode >= 3) {
        func_8C0EC180(AWP(tp)->work[2].ptr);
    }
    if (tp->twp->mode >= 3) {
        njSetTexture(&texlist_FISHING);
        func_8C0E727A();
        func_8C0E72DA();
        func_8C0E76A0(&ptwp->pos, &pos);
    }
}

ASM_STUB(void func_8C0E79D4(task *tp, NJS_POINT3 *pos))
