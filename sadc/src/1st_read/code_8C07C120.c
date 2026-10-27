#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C07C120-8C07C298): event control of the players
 * by player number (the SetInputP family, playertwp[pno]).
 *  func_8C07C120  put a player on a path: flag |= 0x1000, smode = 31
 *                 (path-follow request), value = path, path_point/hpos from
 *                 the path, then the rotation setter func_8C07C9D2
 *  func_8C07C17A  bitmask (1 = player 0, 2 = player 1) of the players
 *                 following the given path (flag & 0x2000)
 *  func_8C07C1B4  event lock 2: walk to the point (x, z) (default speed/accel)
 *  func_8C07C1EE  event lock 3: run along the direction (x, z)
 *  func_8C07C240  event lock 4: push along the vector (x, z)
 *  func_8C07C270  event lock 1: hold still
 * The locks are executed by func_8C06EAC0 / func_8C06EFCE (twp->flag & 0x4000).
 *
 * Matching notes: the lock setters need the early return and the ewp local
 * (an "if (playertwp[pno]) { ... }" body keeps &playertwp[pno] in R14);
 * func_8C07C17A needs the assignment inside the NULL test and the nested if
 * (hoists 0x2000 and gives ret R12).
 */

struct pathtag;                 /* PATHTAG of code_8C08966E.c */

/* path length up to point n -> *out (code_8C08966E.c) */
extern Bool func_8C089A40(struct pathtag *tag, Uint32 n, Float *out);

/* SetRotationP-like: twp->ang = (x, 0x4000 - y, z), mwp->ang_aim.y too */
ASM_STUB(void func_8C07C9D2(Uint8 pno, Angle x, Angle y, Angle z))

/* eventwk +0x28 holds a point/vector (three floats) for locks 2-4; the
 * header declares it as Angle ang[3] (same size and alignment) */
#define EWP_POS(ewp) (*(NJS_POINT3 *)(ewp)->ang)

/* playerwk +0x14 is written with MOV.W here (the header has Sint32 path_point;
 * this low-half view is only correct on little-endian hosts) */
#define PATH_POINT(pwp) (*(Sint16 *)&(pwp)->path_point)

void func_8C07C120(Uint8 pno, struct pathtag *path, Sint32 point, Angle3 *ang)
{
    taskwk *twp;
    playerwk *pwp;

    twp = playertwp[pno];
    pwp = playerpwp[pno];
    twp->flag |= 0x1000;
    twp->smode = 31;
    twp->value.ptr = path;
    PATH_POINT(pwp) = point;
    func_8C089A40(path, point, &pwp->hpos);
    func_8C07C9D2(pno, ang->x, ang->y, ang->z);
}

Sint32 func_8C07C17A(struct pathtag *path)
{
    taskwk *twp;
    Sint32 i;
    Sint32 ret;

    ret = 0;
    for (i = 0; i < 2; i++) {
        if ((twp = playertwp[i]) != NULL) {
            if ((twp->flag & 0x2000) && twp->value.ptr == path) {
                ret += i + 1;
            }
        }
    }
    return ret;
}

void func_8C07C1B4(Uint8 pno, Float x, Float z)
{
    eventwk *ewp;

    if (playertwp[pno] == NULL) {
        return;
    }
    ewp = (eventwk *)playertwp[pno]->ewp;
    ewp->lock = 2;
    EWP_POS(ewp).x = x;
    EWP_POS(ewp).z = z;
    ewp->f34 = 0.0f;
    ewp->f38 = 0.0f;
    playertwp[pno]->flag |= 0x4000;
}

void func_8C07C1EE(Uint8 pno, Float x, Float z)
{
    eventwk *ewp;

    if (playertwp[pno] == NULL) {
        return;
    }
    ewp = (eventwk *)playertwp[pno]->ewp;
    ewp->lock = 3;
    EWP_POS(ewp).x = x;
    EWP_POS(ewp).z = z;
    playertwp[pno]->flag |= 0x4000;
}

void func_8C07C240(Uint8 pno, Float x, Float z)
{
    eventwk *ewp;

    if (playertwp[pno] == NULL) {
        return;
    }
    ewp = (eventwk *)playertwp[pno]->ewp;
    ewp->lock = 4;
    EWP_POS(ewp).x = x;
    EWP_POS(ewp).z = z;
    playertwp[pno]->flag |= 0x4000;
}

void func_8C07C270(Uint8 pno)
{
    eventwk *ewp;

    if (playertwp[pno] == NULL) {
        return;
    }
    ewp = (eventwk *)playertwp[pno]->ewp;
    ewp->lock = 1;
    playertwp[pno]->flag |= 0x4000;
}
