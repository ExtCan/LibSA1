/*
 * 8C04BF00-8C04C5DE (chunk asm/1st_read/text/8C04BF00.src, one function).
 *
 * Weld (join vertex) update for an event action: the player's welds are
 * re-evaluated against an externally supplied (object, motion, frame)
 * instead of the player's current action. Caller: the event action queue
 * player func_8C04B57C (code_8C04B440.c), once per queued/blended action.
 *
 * Same algorithm as case 1 of PJoinVertexes (8C07A55C), with its own matrix
 * buffers (B_8C7492B0.. instead of B_8C798698..), the node count taken from
 * plactptr[0].objnum, and only weld entries whose objptr is `object`.
 * mj.jvmode must be 1 (welds set up by PJoinVertexes); for 0, 2 or any other
 * value the original spins in an endless loop (BRA to itself at 8C04C5BC).
 *
 * NOTE (portability): like PJoinVertexes, the weld writes go into the model
 * vertex/normal arrays of the character tree (per-instance copies on PC).
 */

#include "common.h"
#include "ninja.h"
#include "sa1/ninja_types.h"
#include "sa1/ninja_funcs.h"
#include "sa1/player.h"

extern NJS_MATRIX D_8C67C640;   /* unit matrix */
extern NJS_MATRIX B_8C7492B0;   /* source node matrix */
extern NJS_MATRIX B_8C7492F0;   /* destination node matrix */
extern NJS_MATRIX B_8C749330;   /* inverse of the source node matrix */
extern NJS_MATRIX B_8C749370;   /* inverse of the destination node matrix */

/*
 * Not taken from sa1/engine.h, whose prototype of func_8C0583A0 is
 * (NJS_OBJECT *, void *, void *, Sint32, Float); the call here passes
 * R4 = D_8C67C640 (base matrix), R5 = &action, R6 = node count, R7 = NULL,
 * FR4 = frame, as in code_8C0572D8.c.
 */
Sint32 func_8C0583A0(NJS_MATRIX_PTR base, NJS_ACTION *action, Float frame,
                     Sint32 node, NJS_MATRIX_PTR out);
Sint32 func_8C058434(Sint32 node, NJS_MATRIX_PTR m);   /* njSetMatrix(m, node matrix) */

#ifdef NON_MATCHING
/*
 * Instruction stream identical to the original (785 instructions, same
 * frame of 0xA0, same control flow, same pool); what is left are register
 * and stack-slot tie-breaks of SHC's allocator (46 differing words):
 *  - the 19 loop-invariant matrix addresses are hoisted per occurrence into
 *    stack temporaries; 5 of them (njInvertMatrix(&B_8C749330),
 *    func_8C058434(dstdepth), njInvertMatrix(&B_8C749370), case 2/3
 *    njPushMatrix/njMultiMatrix) get permuted slots in 80..120;
 *  - the hoisted float constants 0.5f/1.0f/0.0f get FR14/FR13/FR15 instead
 *    of FR15/FR14/FR13;
 *  - case 1 loop: R4/R5 swapped for i*2 and jvp->pnum.
 * Matching findings (keep them):
 *  - every hoisted matrix argument must be written as (NJS_MATRIX_PTR)&B_x
 *    (address of the whole array): only that form gives one stack temporary
 *    per occurrence; plain B_x is CSE'd into one register. The first
 *    argument of njSetMatrix is not hoisted in the original: plain B_x.
 *  - pno must be 32-bit (spilled to @R15 at entry); mjp = &pwp->mj gives
 *    the R13 base; switch on (Sint8)inpmode (no EXTU.B: inpmode is read
 *    as a signed byte, also in PJoinVertexes).
 *  - jvp->srcobj/dstobj must be re-read (jvp->srcobj->model->points ...):
 *    that produces the dead spills to @(76)/@(72) of the original.
 *  - p0/p2 force &org[i*4] and &org[i*4+2] to be computed before the first
 *    njCalcPoint (jvp->org loaded once), as in the original.
 *  - the six zero stores: their form only changes allocation tie-breaks.
 *    Six separate statements (below) give the closest slot order; the
 *    chained form "vec.x = vec.y = vec.z = pos.x = pos.y = pos.z = 0.0f"
 *    gives the right FR13/14/15 but more permuted slots (137 words).
 *    Declaration order, scopes, casts, NULL/0 and loop spellings have no
 *    effect.
 */
void func_8C04BF00(Sint32 pno, NJS_OBJECT *object, NJS_MOTION *motion, Float frame)
{
    NJS_ACTION action;
    NJS_POINT3 pos;
    NJS_VECTOR vec;
    playerwk *pwp;
    mtnjvwk *mjp;
    PL_JOIN_VERTEX *jvp;
    NJS_POINT3 *srcpts;
    NJS_POINT3 *dstpts;
    NJS_VECTOR *srcnrm;
    NJS_VECTOR *dstnrm;
    NJS_POINT3 *p0;
    NJS_POINT3 *p2;
    Sint32 si;
    Sint32 di;
    Uint16 i;

    /* inpmode 1-3 always come with dstobj != NULL in the weld tables; the
     * original leaves these unset on the other path */
    PC_ONLY(srcpts = dstpts = NULL; srcnrm = dstnrm = NULL;)
    PC_ONLY(pos.x = pos.y = pos.z = 0.0f; vec.x = vec.y = vec.z = 0.0f;)

    pwp = func_8C07CFDE((Uint8)pno);
    mjp = &pwp->mj;
    jvp = mjp->pljvptr;
    action.object = object;
    action.motion = motion;
    switch (mjp->jvmode) {
    case 1:
        func_8C0583A0(D_8C67C640, &action, frame, mjp->plactptr->objnum, NULL);
        while (jvp->objptr) {
            if (jvp->objptr == object) {
                func_8C058434(jvp->srcdepth, (NJS_MATRIX_PTR)&B_8C7492B0);
                if (jvp->dstobj) {
                    /* two welded models: node matrices and their inverses */
                    srcpts = jvp->srcobj->model->points;
                    srcnrm = jvp->srcobj->model->normals;
                    njSetMatrix(B_8C749330, (NJS_MATRIX_PTR)&B_8C7492B0);
                    njInvertMatrix((NJS_MATRIX_PTR)&B_8C749330);
                    func_8C058434(jvp->dstdepth, (NJS_MATRIX_PTR)&B_8C7492F0);
                    dstpts = jvp->dstobj->model->points;
                    dstnrm = jvp->dstobj->model->normals;
                    njSetMatrix(B_8C749370, (NJS_MATRIX_PTR)&B_8C7492F0);
                    njInvertMatrix((NJS_MATRIX_PTR)&B_8C749370);
                } else {
                    /* hand/foot/user position: node origin and one axis
                     * (numVertex selects x, y or z) */
                    pos.z = 0.0f;
                    pos.y = 0.0f;
                    pos.x = 0.0f;
                    vec.z = 0.0f;
                    vec.y = 0.0f;
                    vec.x = 0.0f;
                    if (jvp->numVertex == 0) {
                        vec.x = 1.0f;
                    } else if (jvp->numVertex == 1) {
                        vec.y = 1.0f;
                    } else {
                        vec.z = 1.0f;
                    }
                    njPushMatrix((NJS_MATRIX_PTR)&B_8C7492B0);
                }
                switch ((Sint8)jvp->inpmode) {
                case 1:
                    /* both models move to the average of the two poses */
                    for (i = 0; i < jvp->numVertex; i++) {
                        si = jvp->pnum[i * 2];
                        di = jvp->pnum[i * 2 + 1];
                        p0 = &jvp->org[i * 4];
                        p2 = &jvp->org[i * 4 + 2];
                        njCalcPoint((NJS_MATRIX_PTR)&B_8C7492B0, p0, &pos);
                        njCalcPoint((NJS_MATRIX_PTR)&B_8C7492F0, p2, &vec);
                        pos.x = (pos.x + vec.x) * 0.5f;
                        pos.y = (pos.y + vec.y) * 0.5f;
                        pos.z = (pos.z + vec.z) * 0.5f;
                        njCalcPoint((NJS_MATRIX_PTR)&B_8C749330, &pos, &srcpts[si]);
                        njCalcPoint((NJS_MATRIX_PTR)&B_8C749370, &pos, &dstpts[di]);
                        njCalcVector((NJS_MATRIX_PTR)&B_8C7492B0, &jvp->org[i * 4 + 1], &pos);
                        njCalcVector((NJS_MATRIX_PTR)&B_8C7492F0, &jvp->org[i * 4 + 3], &vec);
                        pos.x = pos.x + vec.x;
                        pos.y = pos.y + vec.y;
                        pos.z = pos.z + vec.z;
                        njUnitVector(&pos);
                        njCalcVector((NJS_MATRIX_PTR)&B_8C749330, &pos, &srcnrm[si]);
                        njCalcVector((NJS_MATRIX_PTR)&B_8C749370, &pos, &dstnrm[di]);
                    }
                    break;
                case 2:
                    /* destination follows the source */
                    njPushMatrix((NJS_MATRIX_PTR)&B_8C749370);
                    njMultiMatrix(NULL, (NJS_MATRIX_PTR)&B_8C7492B0);
                    for (i = 0; i < jvp->numVertex; i++) {
                        si = jvp->pnum[i * 2];
                        di = jvp->pnum[i * 2 + 1];
                        srcpts[si] = jvp->org[i * 4];
                        njCalcPoint(NULL, &jvp->org[i * 4], &dstpts[di]);
                        srcnrm[si] = jvp->org[i * 4 + 1];
                        njCalcVector(NULL, &jvp->org[i * 4 + 1], &dstnrm[di]);
                    }
                    njPopMatrix(1);
                    break;
                case 3:
                    /* source follows the destination */
                    njPushMatrix((NJS_MATRIX_PTR)&B_8C749330);
                    njMultiMatrix(NULL, (NJS_MATRIX_PTR)&B_8C7492F0);
                    for (i = 0; i < jvp->numVertex; i++) {
                        si = jvp->pnum[i * 2];
                        di = jvp->pnum[i * 2 + 1];
                        dstpts[di] = jvp->org[i * 4 + 2];
                        njCalcPoint(NULL, &jvp->org[i * 4 + 2], &srcpts[si]);
                        dstnrm[di] = jvp->org[i * 4 + 3];
                        njCalcVector(NULL, &jvp->org[i * 4 + 3], &srcnrm[si]);
                    }
                    njPopMatrix(1);
                    break;
                case 4:
                    njCalcPoint(NULL, &pos, &pwp->righthand_pos);
                    njCalcVector(NULL, &vec, &pwp->righthand_vec);
                    njPopMatrix(1);
                    break;
                case 5:
                    njCalcPoint(NULL, &pos, &pwp->lefthand_pos);
                    njCalcVector(NULL, &vec, &pwp->lefthand_vec);
                    njPopMatrix(1);
                    break;
                case 6:
                    njCalcPoint(NULL, &pos, &pwp->rightfoot_pos);
                    njCalcVector(NULL, &vec, &pwp->rightfoot_vec);
                    njPopMatrix(1);
                    break;
                case 7:
                    njCalcPoint(NULL, &pos, &pwp->leftfoot_pos);
                    njCalcVector(NULL, &vec, &pwp->leftfoot_vec);
                    njPopMatrix(1);
                    break;
                case 8:
                    njCalcPoint(NULL, &pos, &pwp->user0_pos);
                    njCalcVector(NULL, &vec, &pwp->user0_vec);
                    njPopMatrix(1);
                    break;
                case 9:
                    njCalcPoint(NULL, &pos, &pwp->user1_pos);
                    njCalcVector(NULL, &vec, &pwp->user1_vec);
                    njPopMatrix(1);
                    break;
                }
            }
            jvp++;
        }
        break;
    case 0:
    case 2:
    default:
        /* the original hangs here (welds not set up / being freed) */
        for (;;) {
        }
    }
}
#else
ASM_STUB(void func_8C04BF00(Sint32 pno, NJS_OBJECT *object, NJS_MOTION *motion, Float frame))
#endif
