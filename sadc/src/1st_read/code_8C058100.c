/*
 * 8C058100-8C05846A: node matrix buffer (evaluate an action into
 * B_8C789448[], fetch a node matrix, debug axis display per node).
 * Chunk asm/1st_read/text/8C057BC0.src, functions 2-6.
 */

#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_types.h"
#include "sa1/ninja_funcs.h"
#include "sa1/motion.h"
#include "sa1/ccl.h"
#include "sa1/player.h"

/*
 * Evaluation state passed down the object tree by func_8C057BC0 (0x14 bytes,
 * built on the caller's stack, never game data). func_8C057BC0 advances
 * mdata by one node's motion data (8C058096), stores the node matrix with
 * njGetMatrix(mtx) and advances mtx by one matrix (8C0580A0).
 */
typedef struct {
    void *mdata;                /* 0x00 NJS_MOTION.mdata, current node */
    NJS_MATRIX *mtx;            /* 0x04 output matrix of the current node */
    Uint32 type;                /* 0x08 NJS_MOTION.type */
    Uint32 nbFrame;             /* 0x0C NJS_MOTION.nbFrame */
    Float frame;                /* 0x10 */
} NODE_MTN_EVAL;                /* 0x14 */

SA1_ASSERT_SIZE(NODE_MTN_EVAL, 0x14);
SA1_ASSERT_OFFSET(NODE_MTN_EVAL, mtx, 0x04);
SA1_ASSERT_OFFSET(NODE_MTN_EVAL, frame, 0x10);

extern NJS_MATRIX B_8C789448[128];  /* node matrices of the last evaluated action */
extern Uint32 B_8C78B448;           /* number of node matrices written */
extern Uint32 B_8C78B44C;           /* node limit of the evaluation */
/* Debug axes: one NJS_POINT3COL, 6 points, 3 lines. Declared as an array:
 * with "&scalar" SHC keeps njSetMatrix instead of it in a register. */
extern NJS_POINT3COL D_8C19F0A0[];

/* Evaluates obj (and its children/siblings) at ctx->frame into ctx->mtx;
 * returns obj->sibling (0 once the node limit B_8C78B44C is exceeded). */
ASM_STUB(NJS_OBJECT *func_8C057BC0(NJS_OBJECT *obj, NODE_MTN_EVAL *ctx))

/*
 * Debug: evaluates the player's current action at its current frame and
 * draws axes at every node. The rotations are conditional expressions (not
 * if statements): SHC then keeps the angle in a stack temporary, as in the
 * original (same as func_8C086D00).
 */
void func_8C058100(taskwk *twp, playerwk *pwp, PL_ACTION *pla)
{
    NODE_MTN_EVAL ctx;
    NJS_MATRIX *m;
    Uint32 i;

    B_8C78B448 = 0;
    B_8C78B44C = 128;
    njPushMatrix(NULL);
    njTranslateV(NULL, &CWP(twp)->info->center);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    ctx.mdata = pla[pwp->mj.reqaction].actptr->motion->mdata;
    ctx.mtx = B_8C789448;
    ctx.type = pla[pwp->mj.reqaction].actptr->motion->type;
    ctx.nbFrame = pla[pwp->mj.reqaction].actptr->motion->nbFrame;
    ctx.frame = pwp->mj.nframe;
    func_8C057BC0(pla[pwp->mj.reqaction].actptr->object, &ctx);
    njPopMatrix(1);
    m = B_8C789448;
    for (i = 0; i < B_8C78B448 && i < 128; i++, m++) {
        njPushMatrix(NULL);
        njSetMatrix(NULL, *m);
        njDrawLine3D(D_8C19F0A0, 3, 0);
        njPopMatrix(1);
    }
}

/* Debug: evaluates the player's current action up to node and draws axes
 * at that node. */
Sint32 func_8C05822C(taskwk *twp, playerwk *pwp, PL_ACTION *pla, Sint32 node)
{
    NODE_MTN_EVAL ctx;

    B_8C78B448 = 0;
    if ((B_8C78B44C = node + 1) > 128) {
        B_8C78B44C = 128;
    }
    njPushMatrix(NULL);
    njTranslateV(NULL, &CWP(twp)->info->center);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    ctx.mdata = pla[pwp->mj.reqaction].actptr->motion->mdata;
    ctx.mtx = B_8C789448;
    ctx.type = pla[pwp->mj.reqaction].actptr->motion->type;
    ctx.nbFrame = pla[pwp->mj.reqaction].actptr->motion->nbFrame;
    ctx.frame = pwp->mj.nframe;
    func_8C057BC0(pla[pwp->mj.reqaction].actptr->object, &ctx);
    njPopMatrix(1);
    njPushMatrix(NULL);
    njSetMatrix(NULL, B_8C789448[node]);
    njDrawLine3D(D_8C19F0A0, 3, 0);
    njPopMatrix(1);
    return 1;
}

/*
 * Evaluates action at frame (base: optional root matrix) into B_8C789448[],
 * stopping after node; out (optional) receives the matrix of node.
 * Parameter order: frame is declared third. The registers are the same as
 * for (base, action, node, out, frame) (R4-R7, FR4), but SHC spills the
 * parameters in declaration order and only this order matches 8C0583AC-BA.
 */
Sint32 func_8C0583A0(NJS_MATRIX_PTR base, NJS_ACTION *action, Float frame,
                     Sint32 node, NJS_MATRIX_PTR out)
{
    NODE_MTN_EVAL ctx;

    B_8C78B448 = 0;
    if ((B_8C78B44C = node + 1) > 128) {
        B_8C78B44C = 128;
    }
    njPushMatrix(NULL);
    if (base != NULL) {
        njSetMatrix(NULL, base);
    }
    ctx.mdata = action->motion->mdata;
    ctx.mtx = B_8C789448;
    ctx.type = action->motion->type;
    ctx.nbFrame = action->motion->nbFrame;
    ctx.frame = frame;
    func_8C057BC0(action->object, &ctx);
    if (out != NULL) {
        njSetMatrix(out, B_8C789448[node]);
    }
    njPopMatrix(1);
    return 1;
}

/* Number of node matrices written by the last evaluation. */
Uint32 func_8C05842E(void)
{
    return B_8C78B448;
}

/* Copies matrix node of the last evaluation into m (NULL: current matrix);
 * 0 if node was not evaluated (unsigned compare: negative node fails too). */
Sint32 func_8C058434(Sint32 node, NJS_MATRIX_PTR m)
{
    if (node >= B_8C78B448) {
        return 0;
    }
    njSetMatrix(m, B_8C789448[node]);
    return 1;
}
