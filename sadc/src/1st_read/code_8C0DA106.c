#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"
#include "sa1/upgdisp.h"     /* SA1_GA only: the upgrades' display set (docs/research/gsa1_100_select.md 4.3.3) */

/*
 * 1ST_READ 8C0DA106-8C0DA6E8 (chunk 8C0D6B40, functions 3-8 of 32): Amy.
 *  func_8C0DA106  destructor (tp->dest, set by AmyMain)
 *  func_8C0DA122  display (tp->disp): hammer morph scale, texlist, matrix
 *                 from twp->ang, action / event-queue draw, shadow
 *  func_8C0DA4F4  current event-queue action and frame (helper of 8C0DA560)
 *  func_8C0DA560  Warrior Feather draw (equipment 0x100) on node 22
 *  func_8C0DA662  hammer model: Long Hammer (equipment 0x80) or the normal one
 *  func_8C0DA6E0  twp->mode = 1, waittimer = 0, then the stand selector
 *                 func_8C0DA6E8 (tail call placed right after: no BRA)
 */

extern Sint32 func_8C0DE2A0(taskwk *twp, motionwk *mwp, playerwk *pwp); /* hammer effect draw (code_8C0DE2A0.c); 1: the body is not drawn */

extern NJS_TEXLIST texlist_AMY;         /* 8C4BF3AC */
/* Amy's hand node; its sibling is the hammer object. Declared as an array
 * (node of the object tree) and read with "->": with a plain object SHC folds
 * D_8C4C3B20+0x30 into the literal, the original loads _D_8C4C3B20 and uses
 * @(48,Rn) (8C0DA1EC/8C0DA1F4, 8C0DA664/8C0DA66E). */
extern NJS_OBJECT D_8C4C3B20[];
extern NJS_OBJECT D_8C4C38B8;           /* normal hammer object */
extern NJS_OBJECT Amy_Long_Hammer;      /* 8C4CBA84 */
extern NJS_OBJECT Amy_Warrior_Feather;  /* 8C4CC1D0 */
extern NJS_MATRIX B_8C7A4058;           /* node matrix out of func_8C0583A0 */

#if defined(SA1_GA)
SA1_UPGDISP_DEFINE(SA1_UPGDISP_TAG_DA10);
/*
 * SA1_GA: the shown hammer, for the display's draws only (sa1/upgdisp.h). func_8C0DA662 keeps putting the real bit's hammer
 * into the hand's sibling, and node 36 of that part is the spin's hit sphere (func_8C0DCCA8, after this display): so when
 * the shown Long Hammer bit differs from the real one, the other hammer goes into the sibling after the morph block with
 * the real one's scale and NJD_EVAL_HIDE / NJD_EVAL_UNIT_SCL bits (object and child), and func_8C0DA122 puts the real one
 * back at its end (one exit). Returns the real object to put back, or NULL when nothing was swapped.
 */
static void sa1_upg_hammer_copy(NJS_OBJECT *to, NJS_OBJECT *from)
{
    to->scl[0] = from->scl[0];
    to->scl[1] = from->scl[1];
    to->scl[2] = from->scl[2];
    to->evalflags = (to->evalflags & ~(NJD_EVAL_HIDE | NJD_EVAL_UNIT_SCL))
                  | (from->evalflags & (NJD_EVAL_HIDE | NJD_EVAL_UNIT_SCL));
}

static NJS_OBJECT *sa1_upg_hammer(playerwk *pwp)
{
    NJS_OBJECT *real;
    NJS_OBJECT *shown;

    if (!SA1_UPG_DIFFERS(pwp->equipment, 0x80)) {
        return NULL;
    }
    real = D_8C4C3B20->sibling;
    shown = (sa1_upgdisp.shown & 0x80) ? &Amy_Long_Hammer : &D_8C4C38B8;
    if (real == shown || (real != &Amy_Long_Hammer && real != &D_8C4C38B8)) {
        return NULL;
    }
    sa1_upg_hammer_copy(shown, real);
    if (shown->child != NULL && real->child != NULL) {
        sa1_upg_hammer_copy(shown->child, real->child);
    }
    D_8C4C3B20->sibling = shown;
    return real;
}
#endif

#if !defined(TARGET_PC)
/*
 * Translation-unit context model (SHC check only; never spliced: the
 * function has no address in the image, and it does not exist on PC).
 * In the original TU these functions follow AmyMain and AmyModeMachine
 * (code_8C0D6B40.c, code_8C0D7960.c). Two things carry over from
 * AmyModeMachine's last case (mode 49, 8C0DA0E4-8C0DA104):
 *  - its pool entry for B_8C754C08 is still pending, so SHC dumps the
 *    literal pool inside func_8C0DA122 after the BRAs at 8C0DA1BC and
 *    8C0DA3E4 and after func_8C0DA4F4, as in the game (otherwise the pool
 *    lands inside func_8C0DA560 and its early exit becomes BF + BRA);
 *  - SHC's register choice at the start of func_8C0DA106 depends on the
 *    end of the preceding function (twp in R2 and the callee address
 *    hoisted into R3 only after a leaf ending in a store like this one; an
 *    empty function or one ending in a call gives R3/R3 as in the Knuckles
 *    and Big destructors 8C0C4690/8C0EE420).
 * The stores below only age the pending entry (20..36 of them work).
 */
void tu_tail_8C0D7960(taskwk *twp, playerwk *pwp)
{
    if (B_8C754C08[0].press & 4) {
        twp->mode = 2;
        pwp->mj.reqaction = 9;
    }
    pwp->free.b[0] = 1;   pwp->free.b[1] = 2;   pwp->free.b[2] = 3;   pwp->free.b[3] = 4;
    pwp->free.b[4] = 5;   pwp->free.b[5] = 6;   pwp->free.b[6] = 7;   pwp->free.b[7] = 8;
    pwp->free.b[8] = 9;   pwp->free.b[9] = 10;  pwp->free.b[10] = 11; pwp->free.b[11] = 12;
    pwp->free.b[12] = 13; pwp->free.b[13] = 14; pwp->free.b[14] = 15; pwp->free.b[15] = 16;
    pwp->free.b[16] = 17; pwp->free.b[17] = 18; pwp->free.b[18] = 19; pwp->free.b[19] = 20;
    pwp->free.b[20] = 21; pwp->free.b[21] = 22; pwp->free.b[22] = 23; pwp->free.b[23] = 24;
    pwp->free.b[24] = 25; pwp->free.b[25] = 26; pwp->free.b[26] = 27; pwp->free.b[27] = 28;
    twp->flag &= ~4;
}
#endif

void func_8C0DA106(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    func_8C06C5CA((Uint8)twp->counter.b[0]);
    func_8C098FFC(5);
}

/*
 * Display. free.f[6] is the target and free.f[7] the current weight of the
 * hammer morph (AmyModeMachine); free.b[5] == 1 (forced weight, set with
 * f[6]/f[7] by func_8C0DE46C) keeps the target.
 * f[7] moves towards f[6] by 0.1 per frame and scales the hammer object and
 * its child (hidden at 0, NJD_EVAL_UNIT_SCL at 1). Then, unless wtimer bit 1
 * blinks the player off: texlist, func_8C0DE2A0, collision centre = pos +
 * the body centre (p.height / 2 rotated by the player angles), and the body
 * draw (event action queue; in mode 49 with button 0x400 held, the object
 * of the requested action as a static model; else the action list entry or
 * the link action). The blob shadow is drawn in every case.
 * Matching notes: the first test is written with an empty then-branch (a
 * plain "!= 1" block gives the same code with func_8C049A4E's address in R2
 * instead of R3); the rotations are "?:" expressions (SHC keeps the angle in
 * a stack slot, as in code_8C028100.c and code_8C0AE0E0.c); the collision
 * centre is copied through a CCL_INFO * local (R3/R4 registers).
 */
void func_8C0DA122(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    Uint32 action;
    Sint32 ret;
    NJS_VECTOR in;
    NJS_VECTOR out;
    NJS_VECTOR v;
    NJS_OBJECT *obj;
    NJS_OBJECT *child;
    CCL_INFO *info;
#if defined(SA1_GA)
    NJS_OBJECT *sa1_hammer;
#endif

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    if (pwp->free.b[5] == 1) {
    } else if (func_8C049A4E() == 1) {
        pwp->free.f[6] = 0.0f;
    } else if (pwp->mj.reqaction == 13) {
        pwp->free.f[6] = 1.0f;
    } else if (pwp->mj.reqaction < 81) {
        pwp->free.f[6] = 0.0f;
    }
    if (pwp->free.f[6] > pwp->free.f[7] + 0.099999996f) {
        pwp->free.f[7] += 0.099999996f;
    } else if (pwp->free.f[6] < pwp->free.f[7] - 0.099999996f) {
        pwp->free.f[7] -= 0.099999996f;
    } else {
        pwp->free.f[7] = pwp->free.f[6];
    }
    obj = D_8C4C3B20->sibling;
    child = D_8C4C3B20->sibling->child;
    obj->scl[0] = pwp->free.f[7];
    obj->scl[1] = pwp->free.f[7];
    obj->scl[2] = pwp->free.f[7];
    child->scl[0] = pwp->free.f[7];
    child->scl[1] = pwp->free.f[7];
    child->scl[2] = pwp->free.f[7];
    if (pwp->free.f[7] == 0.0f) {
        obj->evalflags |= NJD_EVAL_HIDE;
        child->evalflags |= NJD_EVAL_HIDE;
    } else {
        obj->evalflags &= ~NJD_EVAL_HIDE;
        child->evalflags &= ~NJD_EVAL_HIDE;
        if (pwp->free.f[7] == 1.0f) {
            obj->evalflags |= NJD_EVAL_UNIT_SCL;
            child->evalflags |= NJD_EVAL_UNIT_SCL;
        } else {
            obj->evalflags &= ~NJD_EVAL_UNIT_SCL;
            child->evalflags &= ~NJD_EVAL_UNIT_SCL;
        }
    }
#if defined(SA1_GA)
    sa1_hammer = sa1_upg_hammer(pwp);  /* the shown hammer for the draws below; the real one back at the end */
#endif
    action = pwp->mj.reqaction;
    if (pwp->mj.mtnmode == 2) {
        action = pwp->mj.action;
    }
    if (!(twp->wtimer & 2)) {
        njSetTexture(&texlist_AMY);
        ret = func_8C0DE2A0(twp, mwp, pwp);
        func_8C038CEC(2);
        njPushMatrix(NULL);
        in.z = 0.0f;
        in.x = 0.0f;
        in.y = pwp->p.height * 0.5f;
        njPushMatrix(D_8C67C640);
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        (-twp->ang.y) ? njRotateY(NULL, -twp->ang.y) : (void)0;
        njCalcVector(NULL, &in, &out);
        njPopMatrix(1);
        njAddVector(&out, &twp->pos);
        njTranslateV(NULL, &out);
        info = CWP(twp)->info;
        info->center = out;
        if (!ret) {
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
            (0x8000 - twp->ang.y) ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
            if (action == 14 && (twp->flag & 3)) {
                v.x = 0.0f;
                v.y = -1.0f;
                v.z = 0.0f;
                njTranslateV(NULL, &v);
                njRotateZ(NULL, 0x2000);
                v.x = 0.7f;
                v.y = 1.09999993f;
                v.z = 0.79999997f;
                njScaleV(NULL, &v);
            }
            if (EWP(twp)->list40 != NULL) {
                func_8C04B57C(twp);
            } else if ((B_8C754C08[0].on & 0x400) && twp->mode == 49) {
                /* func_8C036F12 takes an NJS_OBJECT (8C036BC0 walks evalflags) */
                func_8C036F12((NJS_MODEL *)pwp->mj.plactptr[pwp->mj.reqaction].actptr->object);
            } else if (pwp->mj.mtnmode != 2) {
                func_8C040942(pwp->mj.plactptr[action].actptr, pwp->mj.nframe);
            } else {
                func_8C040942(pwp->mj.actwkptr, pwp->mj.nframe);
            }
        }
        njPopMatrix(1);
        func_8C038CEC(0);
    }
    if (func_8C04FE40()) {
        func_8C09247E(twp, &pwp->shadow);
    }
#if defined(SA1_GA)
    if (sa1_hammer != NULL) {
        D_8C4C3B20->sibling = sa1_hammer;
    }
#endif
}

/*
 * Current action of the event action queue (eventwk.list40) and its frame:
 * 0 while the previous action is still blended in (buf44 and linkframe set,
 * cf. func_8C04B57C), else ewp->f3C. Returns 0 without a queue.
 */
Sint32 func_8C0DA4F4(taskwk *twp, NJS_ACTION **action, Float *frame)
{
    eventwk *ewp;
    EV_ACTION_LIST *node;
    EV_ACTION_LIST *prev;

    ewp = twp->ewp;
    if (ewp != NULL) {
        node = ewp->list40;
        prev = ewp->buf44;
        if (node != NULL) {
            if (prev != NULL && node->linkframe) {
                *action = &node->action;
                *frame = 0.0f;
            } else {
                *action = &node->action;
                *frame = ewp->f3C;
            }
            return 1;
        }
    }
    return 0;
}

/*
 * Warrior Feather (equipment 0x100): node 22's matrix of the current action
 * (event queue or player motion), 0.1 back along z. Not drawn while
 * B_8C754EC8 (draw step index) is non-zero. The two nested ifs are needed:
 * "a && b" loads njPushMatrix's address into R3 instead of R1.
 */
void func_8C0DA560(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_ACTION *action;
    Float frame;

    if (SA1_UPG_SHOWN(pwp->equipment) & 0x100) {
        if (B_8C754EC8 == 0) {
            njPushMatrix(NULL);
            njTranslateV(NULL, &CWP(twp)->info->center);
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
            (0x8000 - twp->ang.y) ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
            if (EWP(twp)->list40 == NULL || !func_8C0DA4F4(twp, &action, &frame)) {
                if (pwp->mj.mtnmode != 2) {
                    action = pwp->mj.plactptr[pwp->mj.reqaction].actptr;
                } else {
                    action = pwp->mj.actwkptr;
                }
                frame = pwp->mj.nframe;
            }
            if (func_8C0583A0(NULL, action, frame, 22, B_8C7A4058) == 1) {
                njSetMatrix(NULL, B_8C7A4058);
                njTranslate(NULL, 0.0f, 0.0f, -0.099999996f);
                njSetTexture(&texlist_AMY);
                func_8C036F12((NJS_MODEL *)&Amy_Warrior_Feather);
            }
            njPopMatrix(1);
        }
    }
}

void func_8C0DA662(playerwk *pwp)
{
    if (pwp->equipment & 0x80) {
        D_8C4C3B20->sibling = &Amy_Long_Hammer;
    } else {
        D_8C4C3B20->sibling = &D_8C4C38B8;
    }
}

void func_8C0DA6E8(taskwk *twp, motionwk *mwp, playerwk *pwp);

void func_8C0DA6E0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    twp->mode = 1;
    pwp->waittimer = 0;
    func_8C0DA6E8(twp, mwp, pwp);
}

ASM_STUB(void func_8C0DA6E8(taskwk *twp, motionwk *mwp, playerwk *pwp))
