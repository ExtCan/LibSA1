#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0B4F00-8C0B54AC (chunk 8C0B4F00-8C0B5720, functions 1-5 of 8):
 * Sonic's Light Speed aura draw and the homing-trail ribbon task.
 *  func_8C0B4F00  Light Speed aura: draws the aura skeleton (S_MOT overlay)
 *                 three times with the current action/frame, pulsing blue
 *                 constant material, scaled 1.05 per layer
 *  func_8C0B504A  trail display (tp->disp): polygon strip of the points
 *  func_8C0B50AE  trail step: advance the head (twp->pos) towards the
 *                 target (twp->scl) and append up to n points
 *  func_8C0B5424  trail destructor (tp->dest): free the point arrays
 *  func_8C0B5460  trail fade-out (tp->exec after the strip is complete)
 *
 * Trail task (created by func_8C0B5690(target, max) with
 * CreateElementalTask(2, 3, func_8C0B5520)): taskwk only; the task's mwp
 * slot holds an MAlloc'd NJS_POINT3COL (func_8C0B5520) with MAlloc'd p, col
 * and tex arrays of twp->counter.l entries. taskwk use:
 *   btimer     texlist index into D_8C1C6A94
 *   counter.l  maximum number of points
 *   timer.l    ARGB colour of new points (-1 = white)
 *   value.ptr  NJS_POINT3 *target
 *   ang        direction of the head, ang.z spins by 0x180 per point
 *   pos        head position, scl copy of *target
 */

/* NJS_POINT3COL kept in the task's motion-work slot (func_8C0B5520:
 * tp->mwp = MAlloc(0x10)) */
#define TRAIL_PC(tp) ((NJS_POINT3COL *)(tp)->mwp)

/*
 * Light Speed aura object in S_MOT.PRS (overlay linked at 0x0CC00000; head
 * node 0x0CC0252C, see code_8C0B2890.c). PC: provided by the overlay loader.
 */
#if defined(TARGET_PC)
extern NJS_OBJECT S_MOT_object_0CC02AD4;
#define AURA_OBJECT (&S_MOT_object_0CC02AD4)
#else
#define AURA_OBJECT ((NJS_OBJECT *)0x0CC02AD4)
#endif

extern Sint32 B_8C752BEC;          /* frame counter (code_8C08A4BA.c) */
extern NJS_TEXLIST D_8C1C6A94[];   /* trail texlists, indexed by twp->btimer */

/*
 * NON_MATCHING: only the order of the three argument copies before both
 * njScale calls differs (original FR6, FR4, FR5; SHC gives FR4, FR6, FR5).
 * Tried: literal vs variable scale, separate/chained sx/sy/sz, double
 * literals, const/register, assignment inside the argument list, every
 * declaration order, ternary forms of the action/motion selection; none
 * reproduces the order (instruction-scheduling tie). Everything else,
 * registers and stack layout included, matches.
 * Second pass (standalone SHC experiments, ~2000 call forms): three plain
 * copies of one register into FR4-FR6 always come out FR4,FR6,FR5 (also with
 * 0 / cast / variable matrix argument, unprototyped or double-parameter
 * callee, function-pointer call, #pragma inline helper, block-local or
 * self-assigned scale variables, comma/cast/unary/identity-arithmetic
 * argument forms, static/global const scale). FR6-first copy orders only
 * appear when another instruction in the block feeds FR6 (FMUL/FNEG/load)
 * or when extra integer arguments are present (R5-R7); the event overlays
 * ev008b/ev00de/ev00fd show the same 6,4,5 order in such calls. This is the
 * only plain 3-copy 6,4,5 site in 1ST_READ.
 * Third pass: reusing f as the scale, sz = 1.05f; sx = sz; sy = sz copy
 * chains, -code=machinecode (same 4,6,5 bytes in the SYSROF object) and every
 * SHC 5.1 release (r01-r13 all give 4,6,5) change nothing.
 */
#ifdef NON_MATCHING
void func_8C0B4F00(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_ACTION action;
    NJS_ARGB mat;
    Sint32 act;
    Float f;

    if (B_8C754EC8 == 0) {
        action.object = AURA_OBJECT;
        act = pwp->mj.reqaction;
        if (pwp->mj.mtnmode == 2) {
            act = pwp->mj.action;
        }
        njPushMatrix(NULL);
        if (pwp->mj.mtnmode != 2) {
            action.motion = pwp->mj.plactptr[act].actptr->motion;
        } else {
            action.motion = pwp->mj.actwkptr->motion;
        }
        f = (Float)(B_8C752BEC & 0x7F);
        if (f >= 64.0f) {
            f = 128.0f - f;
        }
        f *= 0.015625f;
        mat.r = 0.0f;
        mat.g = 0.27999998f - f * 0.179999996f;
        mat.b = 1.0f;
        mat.a = 0.0f;
        njSetConstantMaterial(&mat);
        func_8C60E49A(&action, pwp->mj.nframe);
        mat.r = 0.049999998f;
        mat.g += -0.149999995f;
        njSetConstantMaterial(&mat);
        njScale(NULL, 1.05f, 1.05f, 1.05f);
        func_8C60E49A(&action, pwp->mj.nframe);
        mat.g -= 0.049999998f;
        mat.b = 0.29999999f;
        njSetConstantMaterial(&mat);
        njScale(NULL, 1.05f, 1.05f, 1.05f);
        func_8C60E49A(&action, pwp->mj.nframe);
        mat.r = 0.0f;
        mat.g = 0.0f;
        mat.b = 0.0f;
        njSetConstantMaterial(&mat);
        njPopMatrix(1);
    }
}
#else
ASM_STUB(void func_8C0B4F00(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C0B504A(task *tp)
{
    NJS_POINT3COL *pc;
    task *ttp;            /* copy of tp: keeps it in a callee-saved register */

    ttp = tp;
    pc = TRAIL_PC(ttp);
    if (pc->num >= 3 && B_8C754EC8 == 0) {
        njColorBlendingMode(0, 8);
        njColorBlendingMode(1, 10);
        njSetTexture(&D_8C1C6A94[ttp->twp->btimer]);
        njSetTextureNum(0);
        func_8C63D66C(pc, pc->num - 2, 0x80000062);
        njColorBlendingMode(0, 1);
        njColorBlendingMode(1, 1);
    }
}

void func_8C0B50AE(taskwk *twp, NJS_POINT3COL *pc, Sint32 n)
{
    NJS_POINT3 v;
    NJS_POINT3 out;
    NJS_POINT3 *p;
    NJS_COLOR *col;
    NJS_TEX *tex;
    Sint32 num;
    Sint32 max;
    Sint32 i;
    Sint32 m;
    Angle angy;
    Uint32 ang;
    Float side;
    Float len;

    p = pc->p;
    col = pc->col;
    num = pc->num;
    tex = pc->tex;
    max = twp->counter.l;
    if (num == max) {
        return;
    }
    if (num & 1) {
        side = 0.39999998f;
    } else {
        side = -0.39999998f;
    }
    for (i = 0; i < n; i++) {
        v.x = twp->scl.x - twp->pos.x;
        v.y = twp->scl.y - twp->pos.y;
        v.z = twp->scl.z - twp->pos.z;
        len = njScalor(&v) / (Float)(n - i);
        if (!(len > 0.049999998f)) {
            return;
        }
        ang = -SA1_FTOI(func_8C10D058(v.y / len) * 10430.381f);
        angy = SA1_FTOI(atan2f(v.x, v.z) * 10430.381f);
        twp->ang.x = ang;
        twp->ang.y = angy;
        twp->ang.z += 0x180;
        njPushMatrix(&D_8C67C640[0]);
        (ang = twp->ang.y) ? njRotateY(NULL, ang) : (void)0;
        (ang = twp->ang.x) ? njRotateX(NULL, ang) : (void)0;
        (ang = twp->ang.z) ? njRotateZ(NULL, ang) : (void)0;
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = len;
        njCalcPoint(NULL, &v, &out);
        twp->pos.x += out.x;
        twp->pos.y += out.y;
        twp->pos.z += out.z;
        v.x = side;
        v.y = 0.0f;
        v.z = len;
        njCalcPoint(NULL, &v, &out);
        out.x += twp->pos.x;
        out.y += twp->pos.y;
        out.z += twp->pos.z;
        p[num].x = out.x;
        p[num].y = out.y;
        p[num].z = out.z;
        side = -side;
        njPopMatrix(1);
        col[num].color = twp->timer.l;
        if (num == 1) {
            tex[num].u = 0;
            tex[num].v = 0xFF;
        } else if (num == max - 1) {
            tex[num].u = 0xFF;
            tex[num].v = 0xFF;
        } else if (num == max - 2) {
            tex[num].u = 0xFF;
            tex[num].v = 0;
        } else {
            m = num % 4;
            if (m == 2) {
                tex[num].u = 0x70;
                tex[num].v = 0;
            } else if (m == 3) {
                tex[num].u = 0x70;
                tex[num].v = 0xFF;
            } else if (m == 0) {
                tex[num].u = 0x90;
                tex[num].v = 0;
            } else {
                tex[num].u = 0x90;
                tex[num].v = 0xFF;
            }
        }
        pc->num = ++num;
        if (num >= max) {
            break;
        }
    }
}

void func_8C0B5424(task *tp)
{
    NJS_POINT3COL *pc;

    pc = TRAIL_PC(tp);
    if (pc->p != NULL) {
        MFree(pc->p);
        pc->p = NULL;
    }
    if (pc->col != NULL) {
        MFree(pc->col);
        pc->col = NULL;
    }
    if (pc->tex != NULL) {
        MFree(pc->tex);
        pc->col = NULL;     /* sic: clears col, not tex */
    }
}

void func_8C0B5460(task *tp)
{
    NJS_POINT3COL *pc;
    NJS_COLOR *col;
    Sint32 i;
    Bool alive;

    pc = TRAIL_PC(tp);
    col = pc->col;
    func_8C0B504A(tp);
    alive = FALSE;
    for (i = pc->num; i > 0; i--) {
        if (col->argb.a > 8) {
            alive = TRUE;
            col->argb.a -= 5;
        }
        col++;
    }
    if (!alive) {
        FreeTask(tp);
        tp->disp = NULL;
    }
}
