/*
 * 8C07A55C-8C07AEF8: PJoinVertexes (player welds).
 * Chunk asm/1st_read/text/8C07A55C.src, function 1/3.
 *
 * jvmode 0: set up the weld list (save the original vertices/normals of
 * every weld into a MAlloc'd org buffer, find node depths); 1: evaluate the
 * current action's node matrices and weld the vertices / compute the hand,
 * foot and user positions; 2: restore the original vertices and free org.
 * NOTE: writes the shared weld table and rewrites model vertex arrays
 * (per-instance copies on PC, critique 4.2).
 */

#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_types.h"
#include "sa1/ninja_funcs.h"
#include "sa1/motion.h"
#include "sa1/ccl.h"
#include "sa1/player.h"
#include "sa1/engine.h"

extern NJS_MATRIX B_8C798698;   /* weld matrices (bss) */
extern NJS_MATRIX B_8C7986D8;
extern NJS_MATRIX B_8C798718;
extern NJS_MATRIX B_8C798758;

/*
 * NON_MATCHING: same instructions as the original apart from register and
 * stack-slot choices (1113 vs 1112 insns). Remaining differences:
 * - the 11 hoisted &B_8C7987xx address temps of the jvmode 1 loop and the
 *   srcpnt/srcnml/dstpnt/dstnml homes get a different slot permutation
 *   (declaration order has no effect on them; the inner-loop temps at
 *   40..68 and srcobj/dstobj at 76/72 do match);
 * - jvmode 0: the trailing "dstdepth = ..." store of the default arm is
 *   cross-jumped with the one of cases 4-9 (original keeps both);
 * - jvmode 2 loop: MFree/b/pnum/srcmdl/dstmdl registers rotated
 *   (orig R10/R12/R12/R6/R5, here R12/R10/R5/R7/R6);
 * - loop-test and 0xFF-constant temps use R2/R1 where the original has R3.
 * The if (0) block after the jvmode 1 loop is dead code that keeps srcobj
 * and dstobj live, which reproduces the original's dead stores of both to
 * the stack (76/72); without it the frame shrinks by 8 bytes.
 * Tried without success: if/else (+continue, goto, do-while(0)) instead of
 * the switch on srcdepth, while loops, other loop-local variable shapes,
 * matrices passed as arrays instead of (NJS_MATRIX_PTR)&B (disables the
 * hoisting), case orders, dead stores to block the cross-jump.
 */
#ifdef NON_MATCHING
void PJoinVertexes(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR pos;
    NJS_VECTOR vec;
    NJS_OBJECT *srcobj;
    NJS_OBJECT *dstobj;
    NJS_POINT3 *srcpnt;
    NJS_POINT3 *dstpnt;
    NJS_VECTOR *dstnml;
    NJS_VECTOR *srcnml;
    NJS_MODEL *srcmdl;
    NJS_MODEL *dstmdl;
    PL_JOIN_VERTEX *pljvptr;
    mtnjvwk *mjp;
    NJS_POINT3 *p0;
    NJS_POINT3 *p2;
    Sint32 a;
    Sint32 b;
    Uint16 i;

    mjp = &pwp->mj;
    pljvptr = mjp->pljvptr;
    switch (mjp->jvmode) {
    case 0:
        for (; pljvptr->objptr != NULL; pljvptr++) {
            switch (pljvptr->inpmode) {
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
                pljvptr->org = NULL;
                pljvptr->srcdepth = func_8C056712(pljvptr->objptr, pljvptr->srcobj);
                pljvptr->dstdepth = 0;
                break;
            case 0:
                break;
            default:
                pljvptr->org = MAlloc(pljvptr->numVertex * (Sint32)(4 * sizeof(NJS_POINT3)));
                srcmdl = pljvptr->srcobj->model;
                dstmdl = pljvptr->dstobj->model;
                for (i = 0; i < pljvptr->numVertex; i++) {
                    pljvptr->org[i * 4] = srcmdl->points[pljvptr->pnum[i * 2]];
                    pljvptr->org[i * 4 + 1] = srcmdl->normals[pljvptr->pnum[i * 2]];
                    pljvptr->org[i * 4 + 2] = dstmdl->points[pljvptr->pnum[i * 2 + 1]];
                    pljvptr->org[i * 4 + 3] = dstmdl->normals[pljvptr->pnum[i * 2 + 1]];
                }
                pljvptr->srcdepth = func_8C056712(pljvptr->objptr, pljvptr->srcobj);
                pljvptr->dstdepth = func_8C056712(pljvptr->objptr, pljvptr->dstobj);
                break;
            }
        }
        mjp->jvmode = 1;
        break;
    case 1:
        /* the original leaves these unset when a type 1-3 weld has no dstobj */
        PC_ONLY(srcpnt = dstpnt = NULL; srcnml = dstnml = NULL;)
        if (mjp->mtnmode == 2) {
            func_8C0583A0((NJS_MATRIX_PTR)&D_8C67C640, mjp->actwkptr, mjp->nframe,
                          mjp->plactptr[mjp->reqaction].objnum, NULL);
        } else {
            func_8C0583A0((NJS_MATRIX_PTR)&D_8C67C640, mjp->plactptr[mjp->reqaction].actptr, mjp->nframe,
                          mjp->plactptr[mjp->reqaction].objnum, NULL);
        }
        for (; pljvptr->objptr != NULL; pljvptr++) {
            if (pljvptr->objptr == mjp->plactptr[mjp->reqaction].actptr->object) {
                switch (pljvptr->srcdepth) {
                case 0xFF:
                    pljvptr->srcdepth = func_8C056712(pljvptr->objptr, pljvptr->srcobj);
                    break;
                default:
                    func_8C058434(pljvptr->srcdepth, (NJS_MATRIX_PTR)&B_8C798698);
                    if (pljvptr->dstobj != NULL) {
                        srcobj = pljvptr->srcobj;
                        srcpnt = srcobj->model->points;
                        srcnml = srcobj->model->normals;
                        njSetMatrix((NJS_MATRIX_PTR)&B_8C798718, (NJS_MATRIX_PTR)&B_8C798698);
                        njInvertMatrix((NJS_MATRIX_PTR)&B_8C798718);
                        if (pljvptr->dstdepth == 0xFF) {
                            pljvptr->dstdepth = func_8C056712(pljvptr->objptr, pljvptr->dstobj);
                            break;
                        }
                        func_8C058434(pljvptr->dstdepth, (NJS_MATRIX_PTR)&B_8C7986D8);
                        dstobj = pljvptr->dstobj;
                        dstpnt = dstobj->model->points;
                        dstnml = dstobj->model->normals;
                        njSetMatrix((NJS_MATRIX_PTR)&B_8C798758, (NJS_MATRIX_PTR)&B_8C7986D8);
                        njInvertMatrix((NJS_MATRIX_PTR)&B_8C798758);
                    } else {
                        pos.x = pos.y = pos.z = 0.0f;
                        vec.x = vec.y = vec.z = 0.0f;
                        if (pljvptr->numVertex == 0) {
                            vec.x = 1.0f;
                        } else if (pljvptr->numVertex == 1) {
                            vec.y = 1.0f;
                        } else {
                            vec.z = 1.0f;
                        }
                        njPushMatrix((NJS_MATRIX_PTR)&B_8C798698);
                    }
                    switch (pljvptr->inpmode) {
                    case 1:
                        for (i = 0; i < pljvptr->numVertex; i++) {
                            a = pljvptr->pnum[i * 2];
                            b = pljvptr->pnum[i * 2 + 1];
                            p0 = &pljvptr->org[i * 4];
                            p2 = &pljvptr->org[i * 4 + 2];
                            njCalcPoint((NJS_MATRIX_PTR)&B_8C798698, p0, &pos);
                            njCalcPoint((NJS_MATRIX_PTR)&B_8C7986D8, p2, &vec);
                            pos.x = (pos.x + vec.x) * 0.5f;
                            pos.y = (pos.y + vec.y) * 0.5f;
                            pos.z = (pos.z + vec.z) * 0.5f;
                            njCalcPoint((NJS_MATRIX_PTR)&B_8C798718, &pos, &srcpnt[a]);
                            njCalcPoint((NJS_MATRIX_PTR)&B_8C798758, &pos, &dstpnt[b]);
                            njCalcVector((NJS_MATRIX_PTR)&B_8C798698, &pljvptr->org[i * 4 + 1], &pos);
                            njCalcVector((NJS_MATRIX_PTR)&B_8C7986D8, &pljvptr->org[i * 4 + 3], &vec);
                            pos.x = pos.x + vec.x;
                            pos.y = pos.y + vec.y;
                            pos.z = pos.z + vec.z;
                            njUnitVector(&pos);
                            njCalcVector((NJS_MATRIX_PTR)&B_8C798718, &pos, &srcnml[a]);
                            njCalcVector((NJS_MATRIX_PTR)&B_8C798758, &pos, &dstnml[b]);
                        }
                        break;
                    case 2:
                        njPushMatrix((NJS_MATRIX_PTR)&B_8C798758);
                        njMultiMatrix(NULL, (NJS_MATRIX_PTR)&B_8C798698);
                        for (i = 0; i < pljvptr->numVertex; i++) {
                            a = pljvptr->pnum[i * 2];
                            b = pljvptr->pnum[i * 2 + 1];
                            srcpnt[a] = pljvptr->org[i * 4];
                            njCalcPoint(NULL, &pljvptr->org[i * 4], &dstpnt[b]);
                            srcnml[a] = pljvptr->org[i * 4 + 1];
                            njCalcVector(NULL, &pljvptr->org[i * 4 + 1], &dstnml[b]);
                        }
                        njPopMatrix(1);
                        break;
                    case 3:
                        njPushMatrix((NJS_MATRIX_PTR)&B_8C798718);
                        njMultiMatrix(NULL, (NJS_MATRIX_PTR)&B_8C7986D8);
                        for (i = 0; i < pljvptr->numVertex; i++) {
                            a = pljvptr->pnum[i * 2];
                            b = pljvptr->pnum[i * 2 + 1];
                            dstpnt[b] = pljvptr->org[i * 4 + 2];
                            njCalcPoint(NULL, &pljvptr->org[i * 4 + 2], &srcpnt[a]);
                            dstnml[b] = pljvptr->org[i * 4 + 3];
                            njCalcVector(NULL, &pljvptr->org[i * 4 + 3], &srcnml[a]);
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
                    break;
                }
            }
        }
        if (0) {
            srcobj->model = NULL;
            dstobj->model = NULL;
        }
        break;
    case 2:
    default:
        for (; pljvptr->objptr != NULL; pljvptr++) {
            if (pljvptr->org != NULL) {
                srcmdl = pljvptr->srcobj->model;
                dstmdl = pljvptr->dstobj->model;
                for (i = 0; i < pljvptr->numVertex; i++) {
                    a = pljvptr->pnum[i * 2];
                    b = pljvptr->pnum[i * 2 + 1];
                    srcmdl->points[a] = pljvptr->org[i * 4];
                    srcmdl->normals[a] = pljvptr->org[i * 4 + 1];
                    dstmdl->points[b] = pljvptr->org[i * 4 + 2];
                    dstmdl->normals[b] = pljvptr->org[i * 4 + 3];
                }
                MFree(pljvptr->org);
                pljvptr->org = NULL;
            }
        }
        break;
    }
}
#else
ASM_STUB(void PJoinVertexes(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
