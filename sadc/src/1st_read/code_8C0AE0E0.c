#include "common.h"
#include "ninja.h"
/* include/sa1/player.h declares func_8C06C5CA(task *tp), but the function
 * takes the player number (the destructor below passes twp->counter.b[0]:
 * 8C0AE0FA MOV.B @(8,R14) + EXTU.B; see code_8C06C300.c). Until the header
 * is fixed its prototype is renamed out of the way here. */
#define func_8C06C5CA func_8C06C5CA_header_proto
#include "sa1/sa1.h"
#undef func_8C06C5CA

/*
 * 1ST_READ 8C0AE0E0-8C0AE588 (chunk 8C0A9660-8C0AE760, functions 3-6 of 8):
 * Sonic's task destructor, collision centre update, display routine and the
 * stand-action (idle / slope) selector.
 *  func_8C0AE0E0  destructor (tp->dest, set by SonicMain 8C0A7B36)
 *  func_8C0AE110  move collision shape 0 to the body centre (p.center_height
 *                 along the player's up axis) in world space
 *  func_8C0AE17A  display (tp->disp): Sonic / Super Sonic texlist, matrix from
 *                 twp->ang, spin-dash ball squash, action or event-queue draw
 *  func_8C0AE3D6  stand selector: idle action after 1800 frames on level
 *                 ground, slope stand actions 7/8 on steeper ground
 */

void func_8C06C5CA(Sint32 pno);                                         /* player free (code_8C06C300.c) */
extern void func_8C0B2170(taskwk *twp, motionwk *mwp, playerwk *pwp);   /* Sonic: per-frame effect helper (reads mj.reqaction) */
extern void func_8C0945D0(taskwk *twp);                                 /* stop the sound entries owned by twp */
extern void func_8C098FFC(Sint32 n);                                    /* release resource slot n */
extern Sint32 func_8C038D00(Float x, Float y, Float z, Float r);        /* sphere visibility test */
extern void func_8C056926(void);                                        /* save draw flags (code_8C056860.c) */
extern void func_8C056938(void);                                        /* restore draw flags */
extern void func_8C05694A(Uint32 a, Uint32 b);                          /* set draw flags */
extern void func_8C038CEC(Sint32 bank);                                 /* palette bank select */
extern void func_8C04B57C(taskwk *twp);                                 /* play the event action queue (code_8C04B440.c) */
extern void func_8C0B4F00(taskwk *twp, motionwk *mwp, playerwk *pwp);   /* Sonic: extra draw (twp->flag & 0x200) */
extern Sint32 func_8C04FE40(void);                                      /* game mode test */
extern void func_8C09247E(taskwk *twp, shadowwk *shadow);               /* blob shadow */
extern void func_8C07DCE0(taskwk *twp);                                 /* character voice (code_8C07DCE0.c) */

extern NJS_TEXLIST texlist_SONIC;                                       /* 8C1C6C94 */

/* Super Sonic texlist: overlay data at the absolute address 0x0C901EA0
 * (B_CHAOS7.BIN, next to texlist_LM_CHAOS7_0 0x0C901A18 and
 * B_CHAOS7_Super_Sonic 0x0C909D2C). PC: provided by the overlay loader. */
#if defined(TARGET_PC)
extern NJS_TEXLIST B_CHAOS7_texlist_SUPERSONIC;
#define TEXLIST_SUPERSONIC (&B_CHAOS7_texlist_SUPERSONIC)
#else
#define TEXLIST_SUPERSONIC ((NJS_TEXLIST *)0x0C901EA0)
#endif

void func_8C0AE0E0(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    pwp->mj.reqaction = 0;
    func_8C0B2170(twp, mwp, pwp);
    func_8C06C5CA((Uint8)twp->counter.b[0]);
    func_8C0945D0(twp);
    func_8C098FFC(0);
}

void func_8C0AE110(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    CCL_INFO *info;
    NJS_POINT3 v;

    info = CWP(twp)->info;
    v.x = v.z = 0.0f;
    v.y = pwp->p.center_height;
    PConvertVector_P2G(twp, &v);
    info->center = v;
    info->center.x += twp->pos.x;
    info->center.y += twp->pos.y;
    info->center.z += twp->pos.z;
}

/*
 * The rotations are "?:" expressions: SHC then keeps the angle in a stack
 * slot (@R15) as the original does (same idiom as code_8C028100.c). The
 * original frame has one more, never accessed, 4-byte slot below v; on SHC it
 * is reproduced with an unused array (stack_pad), absent on PC.
 */
void func_8C0AE17A(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    Uint32 action;
    NJS_VECTOR v;
    SH_ONLY(Sint32 stack_pad[1];) /* unused 4-byte slot between the ?: angle temporary (@R15) and v (@(8,R15)) */

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    if (!func_8C038D00(twp->pos.x, twp->pos.y, twp->pos.z, 15.0f)) {
        return;
    }
    func_8C056926();
    func_8C05694A(0, 0x80000);
    func_8C038CEC(2);
    action = pwp->mj.reqaction;
    if (pwp->mj.mtnmode == 2) {
        action = pwp->mj.action;
    }
    if (!(twp->wtimer & 2)) {
        if (action >= 130 && action <= 141) {
            njSetTexture(TEXLIST_SUPERSONIC);
        } else {
            njSetTexture(&texlist_SONIC);
        }
        func_8C0B2170(twp, mwp, pwp);
        njPushMatrix(NULL);
        njTranslateV(NULL, &CWP(twp)->info->center);
        twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
        twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
        (0x8000 - twp->ang.y) ? (njRotateY(NULL, 0x8000 - twp->ang.y), 0) : 0;
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
        if (((eventwk *)twp->ewp)->list40 != NULL) {
            func_8C04B57C(twp);
        } else {
            if (pwp->mj.mtnmode != 2) {
                if ((twp->flag & 0x100) && action != 141 && (pwp->free.w[1] & 0x11)) {
                    action = 32;
                }
                func_8C040942(pwp->mj.plactptr[action].actptr, pwp->mj.nframe);
            } else {
                func_8C040942(pwp->mj.actwkptr, pwp->mj.nframe);
            }
            if (twp->flag & 0x200) {
                func_8C0B4F00(twp, mwp, pwp);
            }
        }
        njPopMatrix(1);
    }
    func_8C038CEC(0);
    func_8C056938();
    if (func_8C04FE40()) {
        func_8C09247E(twp, &pwp->shadow);
    }
}

/*
 * Level ground (cos(0x11C7), about 25 degrees): requests 0-6 become the idle
 * action 4 (with a voice, 8C07DCE0) after 1800 frames, any other request is
 * reset to 0. Steeper ground (up to 45 degrees) without attr 0x4000: stand
 * action 7 or 8 depending on the angle between the player's forward axis and
 * gravity, both projected onto the ground plane (facing up or down the
 * slope). Request 29 is left alone and does not advance the idle timer.
 * Callers: SonicMain, SonicModeMachine, func_8C0A9660.
 */
void func_8C0AE3D6(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_LINE line;
    NJS_POINT3 v;
    NJS_POINT3 b;
    NJS_POINT3 a;
    Float dot;
    Angle ang;

    if (pwp->mj.reqaction == 29) {
        return;
    }
    dot = -njInnerProduct(&Gravity, &pwp->floor_normal);
    if (dot > njCos(0x11C7)) {
        if (pwp->mj.reqaction != 0 && pwp->mj.reqaction != 1 && pwp->mj.reqaction != 2
            && pwp->mj.reqaction != 3 && pwp->mj.reqaction != 4 && pwp->mj.reqaction != 5
            && pwp->mj.reqaction != 6) {
            pwp->mj.reqaction = 0;
        } else if (pwp->waittimer > 1800) {
            pwp->waittimer = 0;
            pwp->mj.reqaction = 4;
            func_8C07DCE0(twp);
        }
    } else if (!(pwp->attr & 0x4000)) {
        if (dot > njCos(0x2000)) {
            line.vx = pwp->floor_normal.x;
            line.vy = pwp->floor_normal.y;
            line.vz = pwp->floor_normal.z;
            line.px = line.py = line.pz = 0.0f;
            v.x = 1.0f;
            v.y = v.z = 0.0f;
            PConvertVector_P2G(twp, &v);
            njDistanceP2L(&v, &line, &a);
            njUnitVector(&a);
            njDistanceP2L(&Gravity, &line, &b);
            njUnitVector(&b);
            ang = VectorAngle(&a, &b, &pwp->floor_normal);
            if (ang > 0xC000 || ang < 0x4000) {
                pwp->mj.reqaction = 8;
            } else {
                pwp->mj.reqaction = 7;
            }
        }
    }
    pwp->waittimer++;
}
