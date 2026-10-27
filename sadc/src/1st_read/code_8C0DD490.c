#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * 1ST_READ 8C0DD490-8C0DDA00 (chunk 8C0DCBA0, functions 10-12 of 12): Amy.
 *  func_8C0DD490  exec of a CreateElementalTask(3, 3, ...) task started by
 *                 AmyMain (twp->counter.b[0] = player number): soft-body
 *                 morph of one of Amy's models (dress). The original model
 *                 D_8C4BFEA4 drives the morph copy amy_y (45 points each):
 *                 points away from the axis (x*x + z*z > 2.89) are pushed
 *                 out of four node positions (61, 51, 62, 52 of the current
 *                 action) and towards the player's speed, then eased
 *                 (0.2 per frame) towards the result. Object D_8C4BFECC
 *                 draws amy_y; the dest func_8C0DD488 puts the original
 *                 model back. Only while the action uses the Amy object.
 *  func_8C0DD8DC  create the look-at task 8C0DDA00 for player pno with the
 *                 head and eye objects objs[0..2] (Amy's copy of Sonic's
 *                 func_8C0B27EE; also clears NJD_EVAL_UNIT_ANG on the head)
 *  func_8C0DD944  dest of the look task: put the saved head and eye angles
 *                 back (same as Sonic's func_8C0B2890)
 */

extern NJS_OBJECT Amy;              /* 8C4C7C3C Amy's root object */
extern NJS_MODEL D_8C4BFEA4;        /* original model (45 points) of object D_8C4BFECC */
extern NJS_OBJECT D_8C4BFECC[];     /* node drawn with the morph copy (model = D_8C4BFEA4 / amy_y) */
extern NJS_MODEL amy_y[];           /* 8C4C9088 morph copy of D_8C4BFEA4 */
extern NJS_MATRIX B_8C7A4018;       /* base matrix for func_8C0583A0 */
extern NJS_MATRIX B_8C7A4058;       /* node matrix out of func_8C0583A0 */

void func_8C0DD488(task *tp);       /* dest: D_8C4BFECC.model back to the original */
void func_8C0DDA00(task *tp);       /* look task exec */

#ifdef NON_MATCHING
/*
 * NON_MATCHING: same instructions and stack frame as the original, register
 * allocation differs. (1) SHC 5.1 keeps three loop-invariant float constants
 * in FR12-FR14: the original keeps 0.43005181 (2 uses), 0.001 and 1.31 and
 * reloads the ease factor 0.2 (3 uses) from the pool; this source keeps
 * 1.31, 0.001 and 0.2 and reloads 0.43. Making 0.2 a 2-use constant (test
 * only, changes behaviour) reproduces the original choice and leaves ~50
 * differing instructions. (2) The original puts those constants and the
 * &v1/&v3 base registers after the "0 < n" loop guard, this source before
 * it. (3) tp/twp are in R14/R13 in the original, R13/R14 here.
 * Tried without effect: while/do-while/explicit guard loops, Sint32/Uint32
 * counters, separate distance/ease variables, ternary/ABS forms, constant
 * spellings and casts, block-scoped vs shared temporaries, pointer inner loop.
 * The distance uses x*x + y*y + z*z on SHC (FMUL, FMAC, FMAC, which matches)
 * and the explicit SA1_FMAC chain on PC so that both round the same way.
 * FMAC audit: the 3 fused ops of this source (x*x + z*z, and the two of the
 * node distance) are the 3 FMACs of the asm; clang fully unrolls the
 * 4-iteration node loop, so the PC object shows 1 + 4 * 2 = 9 FMAs. Checked
 * against the original on every call of amy_tp2 (whole RAM compared after
 * each call: identical, all branches but the zero-length v2 one taken).
 */
void func_8C0DD490(task *tp)
{
    NJS_POINT3 pts[4];
    taskwk *twp;
    playerwk *pwp;
    NJS_POINT3 *dst;
    NJS_POINT3 *src;
    Sint32 j;
    Uint32 n;
    Uint32 i;
    Float d;

    twp = tp->twp;
    if (playertwp[(Uint8)twp->counter.b[0]] == NULL) {
        DestroyTask(tp);
        return;
    }
    pwp = GetPlayerWorkPtr(twp->counter.b[0]);
    dst = amy_y->points;
    if (AmyActionList[pwp->mj.reqaction].actptr->object != &Amy) {
        return;
    }
    n = D_8C4BFEA4.nbPoint;
    src = D_8C4BFEA4.points;
    switch (twp->mode) {
    case 0:
        twp->mode = 1;
        tp->dest = func_8C0DD488;
        break;
    case 1:
        {
            NJS_POINT3 zero;
            NJS_POINT3 pos;

            zero.x = 0.0f;
            zero.y = 0.0f;
            zero.z = 0.0f;
            /* a node the action does not have leaves its pts[] entry as
             * stack garbage on the DC; PC builds start from the origin */
            PC_ONLY(for (j = 0; j < 4; j++) { pts[j] = zero; })
            njUnitMatrix(B_8C7A4018);
            if (func_8C0583A0(B_8C7A4018, AmyActionList[pwp->mj.reqaction].actptr, pwp->mj.nframe, 62, (NJS_MATRIX_PTR)&B_8C7A4058) == 1) {
                njCalcPoint((NJS_MATRIX_PTR)&B_8C7A4058, &zero, &pos);
                pts[2] = pos;
            }
            if (func_8C058434(52, (NJS_MATRIX_PTR)&B_8C7A4058)) {
                njCalcPoint((NJS_MATRIX_PTR)&B_8C7A4058, &zero, &pos);
                pts[3] = pos;
            }
            if (func_8C058434(61, (NJS_MATRIX_PTR)&B_8C7A4058)) {
                njCalcPoint((NJS_MATRIX_PTR)&B_8C7A4058, &zero, &pos);
                pts[0] = pos;
            }
            if (func_8C058434(51, (NJS_MATRIX_PTR)&B_8C7A4058)) {
                njCalcPoint((NJS_MATRIX_PTR)&B_8C7A4058, &zero, &pos);
                pts[1] = pos;
            }
        }
        for (i = 0; i < n; i++, dst++, src++) {
            NJS_POINT3 v1;
            NJS_POINT3 v2;
            NJS_POINT3 v3;

            d = SA1_FMAC(src->x, src->x, src->z * src->z);
            if (d > 2.8899997f) {
                v1.x = src->x * 0.43005181f;
                v1.y = -0.47f;
                v1.z = src->z * 0.43005181f;
                v2.x = src->x + pwp->spd.x - v1.x;
                v2.y = src->y - pwp->spd.y - v1.y;
                v2.z = src->z + pwp->spd.z - v1.z;
                if (v2.x * v1.x < 0.0f) {
                    v2.x = 0.0f;
                }
                if (v2.z * v1.z < 0.0f) {
                    v2.z = 0.0f;
                }
                if (v2.y > 0.37599996f) {
                    v2.y = 0.37599996f;
                }
                for (j = 0; j < 4; j++) {
                    v3.x = v2.x + v1.x - pts[j].x;
                    v3.y = v2.y + v1.y - pts[j].y;
                    v3.z = v2.z + v1.z - pts[j].z;
                    d = SH_ONLY(v3.x * v3.x + v3.y * v3.y + v3.z * v3.z) PC_ONLY(SA1_FMAC(v3.z, v3.z, SA1_FMAC(v3.x, v3.x, v3.y * v3.y)));
                    if (d < 1.75f) {
                        if (v3.y < 0.0f) {
                            v3.y *= -1.0f;
                        }
                        v2.x += v3.x;
                        v2.y += v3.y;
                        v2.z += v3.z;
                        break;
                    }
                }
                if (njScalor(&v2) == 0.0f) {
                    v2.x = 1.0f;
                    v2.y = v2.z = 0.0f;
                } else {
                    njUnitVector(&v2);
                }
                v2.x *= 1.31f;
                v2.y *= 1.31f;
                v2.z *= 1.31f;
                d = (v2.x + v1.x - dst->x) * 0.19999999f;
                if (_builtin_fabsf(d) < 0.00099999994f) {
                    d = 0.0f;
                }
                dst->x += d;
                d = (v2.y + v1.y - dst->y) * 0.19999999f;
                if (_builtin_fabsf(d) < 0.00099999994f) {
                    d = 0.0f;
                }
                dst->y += d;
                d = (v2.z + v1.z - dst->z) * 0.19999999f;
                if (_builtin_fabsf(d) < 0.00099999994f) {
                    d = 0.0f;
                }
                dst->z += d;
            }
        }
        break;
    default:
        DestroyTask(tp);
        break;
    }
    D_8C4BFECC->model = amy_y;
}
#else
ASM_STUB(void func_8C0DD490(task *tp))
#endif

Bool func_8C0DD8DC(Sint8 pno, NJS_OBJECT **objs)
{
    task *tp;
    lookwk *lwp;

    if ((tp = CreateElementalTask(2, 2, func_8C0DDA00)) != NULL) {
        if ((lwp = MAlloc(sizeof(lookwk))) == NULL) {
            FreeTask(tp);
            return FALSE;
        }
        tp->mwp = (motionwk *)lwp;
        tp->twp->counter.b[0] = pno;
        lwp->head = objs[0];
        objs[0]->evalflags &= ~NJD_EVAL_UNIT_ANG;
        lwp->eyeL = objs[1];
        lwp->eyeR = objs[2];
        goto ok;
    }
    return FALSE;
ok:
    PC_ONLY(return TRUE;)
    ;
}

void func_8C0DD944(task *tp)
{
    lookwk *lwp;

    lwp = (lookwk *)tp->mwp;
    lwp->head->ang[0] = lwp->head_ang[0];
    lwp->head->ang[1] = lwp->head_ang[1];
    lwp->head->ang[2] = lwp->head_ang[2];
    lwp->eyeR->ang[0] = lwp->eyeR_ang[0];
    lwp->eyeR->ang[1] = lwp->eyeR_ang[1];
    lwp->eyeR->ang[2] = lwp->eyeR_ang[2];
    lwp->eyeL->ang[0] = lwp->eyeL_ang[0];
    lwp->eyeL->ang[1] = lwp->eyeL_ang[1];
    lwp->eyeL->ang[2] = lwp->eyeL_ang[2];
}
