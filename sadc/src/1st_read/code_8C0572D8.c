/*
 * 8C0572D8-8C05741C: float approach helper and vertex weld by node matrices
 * (chunk asm/1st_read/text/8C055D20.src, last two functions).
 */

#include "common.h"
#include "ninja.h"
#include "sa1/ninja_types.h"
#include "sa1/ninja_funcs.h"

/*
 * Node weld list entry, terminated by num == 0 (game data, e.g. D_8C4BEFA0
 * passed by func_8C0DE8C0 with the action D_8C4BEF90; extern only).
 * DC layout: num +0, src_node +2, dst_node +3, index +4, size 8.
 */
typedef struct {
    Sint16 num;                 /* number of (src, dst) vertex index pairs */
    Sint8 src_node;             /* node index (func_8C0567B0 order) of the source object */
    Sint8 dst_node;             /* node index of the destination object */
    Sint16 *index;              /* num pairs: src vertex, dst vertex */
} NODE_WELD;

SA1_ASSERT_SIZE(NODE_WELD, 0x08);
SA1_ASSERT_OFFSET(NODE_WELD, src_node, 0x02);
SA1_ASSERT_OFFSET(NODE_WELD, index, 0x04);

extern NJS_MATRIX D_8C67C640;
extern NJS_MATRIX_PTR B_8C88F5F8;   /* current matrix pointer of the Ninja stack */

/*
 * Not taken from sa1/engine.h, whose prototype of func_8C0583A0 is
 * (NJS_OBJECT *, void *, void *, Sint32, Float). The body (8C0583A0) does:
 * base != NULL -> njSetMatrix(NULL, base); evaluate action (R5: object,
 * motion) at frame into the node matrix table B_8C789448 (node count
 * B_8C78B44C = node + 1, at most 128); out != NULL -> njSetMatrix(out,
 * &B_8C789448[node]). The float must come third: with it last, SHC
 * schedules the argument setup differently.
 */
Sint32 func_8C0583A0(NJS_MATRIX_PTR base, NJS_ACTION *action, Float frame,
                     Sint32 node, NJS_MATRIX_PTR out);
Sint32 func_8C058434(Sint32 node, NJS_MATRIX_PTR m);   /* njSetMatrix(m, &B_8C789448[node]) */

/* objs[0] = object, then the rest of the tree (func_8C0567D4); returns the node count */
ASM_STUB(Sint32 func_8C0567B0(NJS_OBJECT *object, NJS_OBJECT **objs))

/* Moves cur towards target by step without overshooting. */
Float func_8C0572D8(Float cur, Float target, Float step)
{
    Float r;

    if (cur > target) {
        r = cur - step;
        if (target > r) {
            r = target;
        }
    } else if (target > cur) {
        r = cur + step;
        if (r > target) {
            r = target;
        }
    } else {
        r = cur;
    }
    return r;
}

/*
 * Evaluates the node matrices of (object, motion) at frame and, for every
 * weld entry, transforms the source node's vertices and normals into the
 * destination node's space (inverse(dst) * src) and writes them over the
 * destination vertices/normals. Callers: 8C0DE8C0, 8C0EC6E0.
 * Note: the weld rewrites model vertex data (per-instance copies on PC).
 */
void func_8C057302(NJS_OBJECT *object, NJS_MOTION *motion, NODE_WELD *weld, Float frame)
{
    Sint32 n;
    NJS_ACTION action;
    NJS_OBJECT *objs[64];
    Sint32 b;
    NJS_VECTOR *nrm1;
    NJS_POINT3 *pts1;
    NJS_VECTOR *nrm2;
    NJS_POINT3 *pts2;
    NJS_OBJECT *obj1;
    NJS_OBJECT *obj2;
    NJS_MATRIX_PTR m;
    Sint32 i;
    Sint32 a;

    action.object = object;
    action.motion = motion;
    n = func_8C0567B0(object, objs);
    func_8C0583A0(&D_8C67C640[0], &action, frame, n, NULL);
    while (weld->num != 0) {
        obj1 = objs[weld->src_node];
        obj2 = objs[weld->dst_node];
        njPushMatrix(NULL);
        func_8C058434(weld->src_node, NULL);
        m = B_8C88F5F8;
        pts1 = obj1->model->points;
        nrm1 = obj1->model->normals;
        njPushMatrix(NULL);
        func_8C058434(weld->dst_node, NULL);
        pts2 = obj2->model->points;
        nrm2 = obj2->model->normals;
        njInvertMatrix(NULL);
        njMultiMatrix(NULL, m);
        for (i = 0; i < weld->num; i++) {
            a = weld->index[i * 2];
            b = weld->index[i * 2 + 1];
            njCalcPoint(NULL, &pts1[a], &pts2[b]);
            njCalcVector(NULL, &nrm1[a], &nrm2[b]);
        }
        njPopMatrix(2);
        weld++;
    }
}
