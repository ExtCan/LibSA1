/*
 * 1ST_READ 8C0DE540-8C0DE8C0 (chunk 8C0DE540): Amy.
 *  func_8C0DE540  player 0's playerwk: free.b[6] |= 1 (returns 0 if none)
 *  func_8C0DE56C  player 0's playerwk: free.b[6] |= 4 (returns 0 if none)
 *  func_8C0DE598  after-image task (exec): draws the hammer hand action
 *                 darkened for btimer 2..3, frees itself at 4
 *  func_8C0DE7DC  creates the after-image task from Amy's twp/pwp
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST texlist_AMY;         /* 8C4BF3AC */
extern NJS_OBJECT D_8C4C3B20[];         /* Amy's hand node: sibling = hammer object */

Bool func_8C0DE540(void)
{
    playerwk *pwp;

    if ((pwp = GetPlayerWorkPtr(0)) == NULL) {
        return FALSE;
    }
    pwp->free.b[6] |= 1;
    return TRUE;
}

Bool func_8C0DE56C(void)
{
    playerwk *pwp;

    if ((pwp = GetPlayerWorkPtr(0)) == NULL) {
        return FALSE;
    }
    pwp->free.b[6] |= 4;
    return TRUE;
}

/*
 * After-image task. twp: btimer = age, wtimer = action number (AmyActionList),
 * counter.f = action frame, timer.f = hammer scale, value.f = player height,
 * ang/pos = player's.
 */
void func_8C0DE598(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *child;
    NJS_OBJECT *obj;
    NJS_VECTOR vs;
    NJS_VECTOR vd;
    Float alpha;

    twp = tp->twp;
    if (++twp->btimer >= 4) {
        FreeTask(tp);
        return;
    }
    if (twp->btimer < 2) {
        return;
    }
    if (B_8C754EC8 != 0) {
        return;
    }
    child = D_8C4C3B20->sibling->child;
    obj = D_8C4C3B20->sibling;
    obj->scl[0] = twp->timer.f;
    obj->scl[1] = twp->timer.f;
    obj->scl[2] = twp->timer.f;
    child->scl[0] = twp->timer.f;
    child->scl[1] = twp->timer.f;
    child->scl[2] = twp->timer.f;
    if (twp->timer.f == 0.0f) {
        obj->evalflags |= NJD_EVAL_HIDE;
        child->evalflags |= NJD_EVAL_HIDE;
    } else {
        obj->evalflags &= ~NJD_EVAL_HIDE;
        child->evalflags &= ~NJD_EVAL_HIDE;
        if (twp->timer.f == 1.0f) {
            obj->evalflags |= NJD_EVAL_UNIT_SCL;
            child->evalflags |= NJD_EVAL_UNIT_SCL;
        } else {
            obj->evalflags &= ~NJD_EVAL_UNIT_SCL;
            child->evalflags &= ~NJD_EVAL_UNIT_SCL;
        }
    }
    func_8C038CEC(2);
    func_8C056926();
    func_8C05694A(0, 0x100000);
    alpha = -0.5f - twp->btimer * 0.099999996f;
    func_8C043A20(alpha, 0.0f, 0.0f, 0.0f);
    njSetTexture(&texlist_AMY);
    njPushMatrix(NULL);
    vs.z = 0.0f;
    vs.x = 0.0f;
    vs.y = twp->value.f * 0.5f;
    njPushMatrix(D_8C67C640);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
    njCalcVector(NULL, &vs, &vd);
    njPopMatrix(1);
    vd.x += twp->pos.x;
    vd.y += twp->pos.y;
    vd.z += twp->pos.z;
    njTranslateV(NULL, &vd);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    func_8C040942(AmyActionList[twp->wtimer].actptr, twp->counter.f);
    njPopMatrix(1);
    func_8C056938();
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
    func_8C038CEC(0);
}

void func_8C0DE7DC(taskwk *twp, playerwk *pwp)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 6, func_8C0DE598)) != NULL) {
        tp->twp->pos = twp->pos;
        tp->twp->ang = twp->ang;
        tp->twp->wtimer = pwp->mj.reqaction;
        tp->twp->counter.f = pwp->mj.nframe;
        tp->twp->value.f = pwp->p.height;
        tp->twp->timer.f = pwp->free.f[7];
    }
}
