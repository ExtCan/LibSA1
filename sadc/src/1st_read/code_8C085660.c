#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ccl.h"
#include "sa1/player.h"

/* tp->mwp: the 0x23C-byte work that func_8C047040 (asm, called by the
 * creator func_8C085532) CAllocs in place of the motionwk; no shared type
 * yet. */
typedef struct {
    Uint8 pad0[0x40];
    task *unk40;                /* 0x40 */
} UNK_8C085660_mwp;

extern void FreeTask(task *tp);
extern task *func_8C0D5D80(task *tp);
extern Float func_8C085A1C(NJS_POINT3 *a, NJS_POINT3 *b);
extern Float func_8C085980(NJS_POINT3 *src, NJS_POINT3 *dst); /* returns 1/|src| in FR0 (code_8C085980.c) */
extern void func_8C0D4996(task *tp, NJS_POINT3 *v);
extern void func_8C0D5DF6(task *tp);
extern void func_8C0D5D90(task *tp);
extern void func_8C02F63C(taskwk *twp);

void func_8C085868(task *tp);

/* Child object exec created by func_8C085532 (twp->value.ptr = parent task,
 * twp->counter.b[1] = mode). */
void func_8C085660(task *tp)
{
    taskwk *twp;
    PL_AIWK *awp;
    UNK_8C085660_mwp *mwp;
    task *ptp;
    taskwk *ptwp;
    playerwk *pwp2;
    task *obj;
    task *ret;
    NJS_POINT3 unused;          /* unreferenced; needed for the 24-byte frame */
    NJS_POINT3 v;

    twp = tp->twp;
    mwp = (UNK_8C085660_mwp *)tp->mwp;
    awp = (PL_AIWK *)((task *)twp->value.ptr)->awp;
    ptp = awp->tp2;
    /* the original reads ptp->twp before the NULL test below (harmless on
     * the DC: boot ROM) */
    PC_ONLY(if (ptp == NULL) { return; })
    ptwp = ptp->twp;
    pwp2 = awp->pwp2;
    if (ptp == NULL) {
        return;
    }
    if (twp->cwp != NULL) {
        ((colliwk *)twp->cwp)->flag &= 0xF7FF;
    }
    switch ((Uint8)twp->counter.b[1]) {
    case 0:
        break;
    case 1:
        twp->pos = ptwp->pos;
        if (pwp2 != NULL) {
            SA1_FMAC_ACC(twp->pos.y, pwp2->p.height, 0.5f);
        }
    }
    switch (twp->mode) {
    case 0:
        tp->dest = func_8C085868;
        ((colliwk *)twp->cwp)->flag &= 0xF7FF;
        twp->mode = 1;
    case 1:
        if (mwp->unk40 != NULL) {
            twp->mode = 2;
            break;
        }
        if (awp->flag & 1) {
            break;
        }
        FreeTask(tp);
        return;
    case 2:
        if (mwp->unk40 == NULL) {
            FreeTask(tp);
            return;
        }
        if (!(((colliwk *)twp->cwp)->flag & 2)) {
            break;
        }
        if (((colliwk *)twp->cwp)->hit_cwp->id != 1) {
            break;
        }
        obj = ((colliwk *)twp->cwp)->hit_cwp->mytask;
        if ((ret = func_8C0D5D80(obj)) == NULL || mwp->unk40 != ret) {
            break;
        }
        if (func_8C085A1C(&twp->pos, &ptwp->pos) < 100.0f) {
            if (ptwp->flag & 0x400) {
                v.x = awp->twp->pos.x - awp->twp2->pos.x;
                v.y = awp->twp->pos.y - awp->twp2->pos.y;
                v.z = awp->twp->pos.z - awp->twp2->pos.z;
                func_8C085980(&v, &v);
                v.x *= 5.0f;
                v.y *= 5.0f;
                v.z *= 5.0f;
                func_8C0D4996(obj, &v);
                func_8C0D5DF6(tp);
            } else {
                ptwp->flag |= 4;
                func_8C0D5D90(tp);
            }
            FreeTask(tp);
            return;
        } else {
            func_8C0D5D90(tp);
            FreeTask(tp);
            return;
        }
    }
    func_8C02F63C(twp);
}

void func_8C085868(task *tp)
{
}
