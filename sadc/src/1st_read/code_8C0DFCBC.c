/*
 * 1ST_READ 8C0DFCBC-8C0DFE40 (chunk 8C0DF760): Amy's shockwave ring effect
 * (model Amy_Shockwave_Effect, texlist_AMY_EFF).
 *  func_8C0DFCBC  disp: draws the ring at twp->pos, rotated by ang.z/ang.x,
 *                 scaled by twp->scl
 *  func_8C0DFD3E  exec: on the first frame sets scl.x = 3, counter.f = 1 and
 *                 installs the disp; then counter.f -= 1/30 per frame (frees
 *                 the task below 0), widens the ring by 0.5 per frame and
 *                 sets its height scl.y = 3 * sin(counter.f * 0x8000)
 *
 * Effect taskwk use: mode = initialised flag, counter.f = remaining life 1..0,
 * scl = ring scale.
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST texlist_AMY_EFF;     /* 8C4BF3E4 */
extern NJS_MODEL Amy_Shockwave_Effect;  /* 8C4CC538 basic model */

/* the rotation is skipped for a zero angle; the angle goes through an
 * unsigned local, which SHC keeps in a stack slot (as in code_8C0876F4.c);
 * the conditional-expression form (not if) gives the original R3 choice */
void func_8C0DFCBC(task *tp)
{
    taskwk *twp;
    Uint32 ang;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    njSetTexture(&texlist_AMY_EFF);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    (ang = twp->ang.z) ? njRotateZ(NULL, (Angle)ang) : (void)0;
    (ang = twp->ang.x) ? njRotateX(NULL, (Angle)ang) : (void)0;
    njScaleV(NULL, &twp->scl);
    func_8C036F12(&Amy_Shockwave_Effect);
    njPopMatrix(1);
}

void func_8C0DFD3E(task *tp)
{
    taskwk *twp;
    Float sy;

    twp = tp->twp;
    sy = 3.0f;
    if (twp->mode == 0) {
        twp->scl.x = sy;
        twp->counter.f = 1.0f;
        twp->mode = 1;
        tp->disp = func_8C0DFCBC;
    }
    if ((twp->counter.f += -0.033333333f) < 0.0f) {
        FreeTask(tp);
        return;
    }
    twp->scl.x += 0.5f;
    twp->scl.y = njSin(SA1_FTOI(twp->counter.f * 32768.0f)) * sy;
    twp->scl.z = twp->scl.x;
    func_8C0DFCBC(tp);
    func_8C0919CC(tp);
}
