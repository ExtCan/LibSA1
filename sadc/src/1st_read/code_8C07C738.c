/*
 * Player speed/rotation setters (1ST_READ 8C07C738-8C07C9FA).
 * Chunk asm/1st_read/text/8C07BAC0.src.
 *
 *  func_8C07C738  event request smode 6/7/8 (by type) with speed (x, y, z)
 *  func_8C07C768  event request smode 4: player-space speed (0, spd, 0),
 *                 rotation ang and no-control time tm
 *  func_8C07C7E8  event request smode 5: face direction dir and move
 *                 along it with speed spd
 *  func_8C07C8E0  player-space speed, rotation and no-control time
 *  func_8C07C952  same as func_8C07C8E0 with event request smode 25
 *  func_8C07C9D2  SetRotationP: twp->ang = (x, 0x4000 - y, z),
 *                 mwp->ang_aim.y = 0x4000 - y
 *
 * Matching notes (func_8C07C768, func_8C07C8E0, func_8C07C952): the rotation
 * part is SetRotationP's "twp->ang.y = 0x4000 - y; mwp->ang_aim.y =
 * twp->ang.y" with y = ang->y. The original computes it as
 * "MOV.L @(4,ang),Ry; SUB Ry,Rc; MOV Rc,Ry" (constant register Rc, dead
 * forward copy of twp->ang.y into Ry) and stores Rc twice. SUB instead of
 * NEG/ADD (which every form without a variable gives, e.g.
 * "twp->ang.y = 0x4000 - ang->y") needs ang->y in a variable, but SHC puts
 * that variable in the free parameter register R4, while the original has it
 * in a scratch register (R3 in 8C07C768/8C07C8E0, R2 in 8C07C952). With R4
 * replaced by that register the code below is the original (8C07C952 differs
 * in exactly those 2 instructions; in the other two the constant and copy
 * registers shift as a consequence). About 70 source variants were tried
 * (expression forms, variable type/scope/const/register, declaration and
 * statement order, parameter types, #pragma inline helper, extra locals);
 * none moves the variable out of R4.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

ASM_STUB(void func_8C07C676(Uint8 pno, Float x, Float y, Float z))
void func_8C07C9D2(Uint8 pno, Angle x, Angle y, Angle z);

/* event request smode 8 (type 3), 7 (type 2) or 6, with speed (x, y, z) */
void func_8C07C738(Uint8 pno, Uint8 type, Float x, Float y, Float z)
{
    taskwk *twp;

    twp = playertwp[pno];
    twp->flag |= 0x1000;
    if (type == 3) {
        twp->smode = 8;
    } else if (type == 2) {
        twp->smode = 7;
    } else {
        twp->smode = 6;
    }
    func_8C07C676(pno, x, y, z);
}

/* event request smode 4: player-space speed (0, spd, 0), rotation ang,
 * no-control time tm */
#ifdef NON_MATCHING
void func_8C07C768(Uint8 pno, Float spd, Angle3 *ang, Sint16 tm)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    NJS_POINT3 v;
    Angle ay;

    twp = playertwp[pno];
    mwp = playermwp[pno];
    pwp = playerpwp[pno];
    pwp->nocontimer = tm;
    twp->flag |= 0x1000;
    twp->smode = 4;
    v.x = 0.0f;
    v.y = spd;
    v.z = 0.0f;
    pwp->spd = v;
    twp->ang.x = ang->x;
    ay = ang->y;
    twp->ang.y = 0x4000 - ay;
    mwp->ang_aim.y = twp->ang.y;
    twp->ang.z = ang->z;
    PConvertVector_P2G(twp, &v);
    mwp->spd = v;
}
#else
ASM_STUB(void func_8C07C768(Uint8 pno, Float spd, Angle3 *ang, Sint16 tm))
#endif

/* event request smode 5: face direction dir and move along it at spd */
void func_8C07C7E8(Uint8 pno, Float spd, NJS_POINT3 *dir)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    NJS_POINT3 v;

    twp = playertwp[pno];
    mwp = playermwp[pno];
    pwp = playerpwp[pno];
    twp->flag |= 0x1000;
    twp->smode = 5;
    func_8C07C9D2(pno, 0, SA1_FTOI(atan2f(dir->x, dir->z) * 10430.381f), 0);
    v = *dir;
    v.x *= spd;
    v.y *= spd;
    v.z *= spd;
    mwp->spd = v;
    PConvertVector_G2P(twp, &v);
    pwp->spd = v;
}

/* player-space speed spd, rotation ang, no-control time tm */
#ifdef NON_MATCHING
void func_8C07C8E0(Uint8 pno, NJS_POINT3 *spd, Angle3 *ang, Sint16 tm)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    NJS_POINT3 v;
    Angle ay;

    twp = playertwp[pno];
    mwp = playermwp[pno];
    pwp = playerpwp[pno];
    pwp->nocontimer = tm;
    pwp->spd = *spd;
    twp->ang.x = ang->x;
    ay = ang->y;
    twp->ang.y = 0x4000 - ay;
    mwp->ang_aim.y = twp->ang.y;
    twp->ang.z = ang->z;
    v = *spd;
    PConvertVector_P2G(twp, &v);
    mwp->spd = v;
}
#else
ASM_STUB(void func_8C07C8E0(Uint8 pno, NJS_POINT3 *spd, Angle3 *ang, Sint16 tm))
#endif

/* event request smode 25 with player-space speed spd, rotation ang,
 * no-control time tm */
#ifdef NON_MATCHING
void func_8C07C952(Uint8 pno, NJS_POINT3 *spd, Angle3 *ang, Sint16 tm)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    NJS_POINT3 v;
    Angle ay;

    twp = playertwp[pno];
    mwp = playermwp[pno];
    pwp = playerpwp[pno];
    pwp->nocontimer = tm;
    twp->flag |= 0x1000;
    twp->smode = 25;
    pwp->spd = *spd;
    twp->ang.x = ang->x;
    ay = ang->y;
    twp->ang.y = 0x4000 - ay;
    mwp->ang_aim.y = twp->ang.y;
    twp->ang.z = ang->z;
    v = *spd;
    PConvertVector_P2G(twp, &v);
    mwp->spd = v;
}
#else
ASM_STUB(void func_8C07C952(Uint8 pno, NJS_POINT3 *spd, Angle3 *ang, Sint16 tm))
#endif

/* SetRotationP: rotation (x, y, z) of player pno */
void func_8C07C9D2(Uint8 pno, Angle x, Angle y, Angle z)
{
    taskwk *twp;
    motionwk *mwp;

    twp = playertwp[pno];
    mwp = playermwp[pno];
    twp->ang.x = x;
    twp->ang.y = 0x4000 - y;
    mwp->ang_aim.y = twp->ang.y;
    twp->ang.z = z;
}
