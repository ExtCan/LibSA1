#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ccl.h"

/* Work referenced by the parent task's awp */
typedef struct {
    Uint8 pad0[0x38];
    Float unk38;
} UNK_8C085532_scl;

typedef struct {
    Uint8 pad0[0x58];
    taskwk *unk58;              /* 0x58 */
    Uint8 *unk5C;               /* 0x5C */
    UNK_8C085532_scl *unk60;    /* 0x60 */
} UNK_8C085532_awp;

extern task *CreateChildTask(Uint16 flags, TaskFuncPtr exec, task *parent);
extern void func_8C085660(task *tp);
extern void *func_8C047040(task *tp, taskwk *twp);     /* returns the enemy work (code_8C047040.c), unused here */
extern Float func_8C085980(NJS_VECTOR *v, NJS_VECTOR *out);
extern CCL_INFO D_8C1BEFE0[];

/* pos (R6 = &twp->pos at the call sites in 8C084BA0) is passed but not read */
task *func_8C085532(task *parent, Sint32 mode, NJS_POINT3 *pos)
{
    UNK_8C085532_awp *awp;
    task *tp;
    taskwk *twp;

    PC_ONLY((void)pos;)
    awp = parent->awp;
    tp = CreateChildTask(7, func_8C085660, parent);
    if (tp == NULL) {
        return NULL;
    }
    func_8C047040(tp, tp->twp);
    CCL_Init(tp, D_8C1BEFE0, 2, 1, 1);
    twp = tp->twp;
    twp->value.ptr = parent;
    twp->counter.b[1] = mode;
    switch (mode) {
    case 0:
        func_8C085980((NJS_VECTOR *)(awp->unk5C + 4), &twp->pos);
        twp->pos.x = twp->pos.x * awp->unk60->unk38 * 20.0f;
        twp->pos.y = twp->pos.y * awp->unk60->unk38 * 20.0f;
        twp->pos.z = twp->pos.z * awp->unk60->unk38 * 20.0f;
        twp->pos.x = awp->unk58->pos.x + twp->pos.x;
        twp->pos.y = awp->unk58->pos.y + twp->pos.y;
        twp->pos.z = awp->unk58->pos.z + twp->pos.z;
        twp->pos.y += 5.0f;
        break;
    case 1:
        break;
    }
    return tp;
}
