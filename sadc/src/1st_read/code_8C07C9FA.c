/*
 * Player rotation/speed/position setters (1ST_READ 8C07C9FA-8C07CBEC).
 * Chunk asm/1st_read/text/8C07BAC0.src.
 *
 *  func_8C07C9FA  rotation from a direction vector (x, y, z)
 *  func_8C07CAD0  event request smode 11 with acceleration (x, y, z)
 *  func_8C07CB40  acceleration (x, y, z) / weight (tail call into
 *                 func_8C07CB54; SHC drops the BRA to the next function)
 *  func_8C07CB54  mwp->acc = (x, y, z)
 *  func_8C07CB6A  SetPositionP: twp->pos and the current history entry
 *  func_8C07CBBC  event request smode 10 (type 3), 9 (type 2) or 12
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/* Player position history (code_8C06C6A0.c) */
extern Uint8 B_8C78C688[8];             /* per-player ring index */
extern NJS_POINT3 B_8C78C690[256][8];   /* positions (0x60 per frame) */

/*
 * Rotation of player pno from the direction (x, y, z): ang.y from
 * atan2f(z, x), ang.x and ang.z from the libm wrappers 8C10D008/8C10D058.
 * Matching notes: len * s is written twice (SHC CSEs it into FR13 and keeps
 * len and s in stack homes); x * x + y * y likewise; the ang_aim.y store
 * reads twp->ang.y back (forwarded; leaves the dead MOV R13,R2).
 */
void func_8C07C9FA(Uint8 pno, Float x, Float y, Float z)
{
    taskwk *twp;
    motionwk *mwp;
    Float len;
    Float s;
    Float h;
    Angle angx;
    Angle angy;
    Angle angz;
#if defined(TARGET_PC)
    Float xy;
#endif

    twp = playertwp[pno];
    mwp = playermwp[pno];
#if defined(TARGET_PC)
    /* FMAC x * x + (y * y), then FMAC z * z + that (CSE'd for h below) */
    xy = SA1_FMAC(x, x, y * y);
    len = njSqrt(SA1_FMAC(z, z, xy));
#else
    len = njSqrt(x * x + y * y + z * z);
#endif
    angy = SA1_FTOI(atan2f(z, x) * 10430.381f);
    s = njSin(angy);
    angx = SA1_FTOI(func_8C10D008(-z / _builtin_fabsf(len * s)) * 10430.381f);
#if defined(TARGET_PC)
    h = njSqrt(xy);
#else
    h = njSqrt(x * x + y * y);
#endif
    angz = SA1_FTOI(func_8C10D058(y / h) * 10430.381f)
        - SA1_FTOI(func_8C10D058(_builtin_fabsf(len * s) * njSin(angx) / h) * 10430.381f);
    twp->ang.x = angx;
    twp->ang.y = angy;
    mwp->ang_aim.y = twp->ang.y;
    twp->ang.z = angz;
}

/* event request smode 11 with acceleration (x, y, z) */
void func_8C07CAD0(Uint8 pno, Float x, Float y, Float z)
{
    taskwk *twp;

    twp = playertwp[pno];
    twp->flag |= 0x1000;
    twp->smode = 11;
    func_8C07CB54(pno, x, y, z);
}

/* acceleration (x, y, z) divided by the player's weight */
void func_8C07CB40(Uint8 pno, Float x, Float y, Float z)
{
    playerwk *pwp;

    pwp = playerpwp[pno];
    x /= pwp->p.weight;
    y /= pwp->p.weight;
    z /= pwp->p.weight;
    func_8C07CB54(pno, x, y, z);
}

/* mwp->acc = (x, y, z) */
void func_8C07CB54(Uint8 pno, Float x, Float y, Float z)
{
    motionwk *mwp;

    mwp = playermwp[pno];
    mwp->acc.x = x;
    mwp->acc.y = y;
    mwp->acc.z = z;
}

/* SetPositionP: player position and the current history entry
 * (the early return is needed for the register allocation) */
void func_8C07CB6A(Uint8 pno, Float x, Float y, Float z)
{
    Uint8 idx;

    idx = B_8C78C688[pno];
    if (playertwp[pno] == NULL) {
        return;
    }
    playertwp[pno]->pos.x = x;
    playertwp[pno]->pos.y = y;
    playertwp[pno]->pos.z = z;
    B_8C78C690[idx][pno].x = x;
    B_8C78C690[idx][pno].y = y;
    B_8C78C690[idx][pno].z = z;
}

/* event request smode 10 (type 3), 9 (type 2) or 12 */
void func_8C07CBBC(Uint8 pno, Uint8 type)
{
    taskwk *twp;

    twp = playertwp[pno];
    twp->flag |= 0x1000;
    if (type == 3) {
        twp->smode = 10;
    } else if (type == 2) {
        twp->smode = 9;
    } else {
        twp->smode = 12;
    }
}
