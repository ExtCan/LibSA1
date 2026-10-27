/*
 * Player input requests and speed setters (1ST_READ 8C07C550-8C07C738).
 * Chunk asm/1st_read/text/8C07BAC0.src.
 *
 *  func_8C07C550  hold task htp (pwp->htp), event request smode 30
 *  func_8C07C580  launch: smode 35, rotation from (-x, 0, -z) and speed (x, y, z)
 *  func_8C07C5CA  push players pno1 and pno2 apart (horizontal, scaled)
 *  SetInputP      event request: twp->flag |= 0x1000, twp->smode = mode
 *  func_8C07C676  SetVelocityP: world speed (x, y, z) into mwp->spd and,
 *                 converted to player space, into pwp->spd
 *  func_8C07C720  smode 3 with speed (x, y, z)
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

void func_8C07C676(Uint8 pno, Float x, Float y, Float z);
ASM_STUB(void func_8C07C9FA(Uint8 pno, Float x, Float y, Float z))

/* player pno holds task htp (event request smode 30) unless flag 0x800 */
void func_8C07C550(Uint8 pno, task *htp)
{
    taskwk *twp;

    twp = playertwp[pno];
    if (!(twp->flag & 0x800)) {
        twp->flag |= 0x1000;
        twp->smode = 30;
        playerpwp[pno]->htp = htp;
    }
}

/* event request smode 35; face away from (x, z) and move with (x, y, z) */
void func_8C07C580(Uint8 pno, Float x, Float y, Float z)
{
    taskwk *twp;

    twp = playertwp[pno];
    twp->flag |= 0x1000;
    twp->smode = 35;
    func_8C07C9FA(pno, -x, 0.0f, -z);
    func_8C07C676(pno, x, y, z);
}

/* push players pno1 and pno2 apart horizontally with speed spd */
void func_8C07C5CA(Uint8 pno1, Uint8 pno2, Float spd)
{
    taskwk *twp1;
    taskwk *twp2;
    NJS_VECTOR v;

    twp1 = playertwp[pno1];
    twp2 = playertwp[pno2];
    v.x = twp1->pos.x - twp2->pos.x;
    v.y = twp1->pos.y - twp2->pos.y;
    v.z = twp1->pos.z - twp2->pos.z;
    njUnitVector(&v);
    v.x *= spd;
    v.y = 0.0f;
    v.z *= spd;
    func_8C07C580(pno1, v.x, v.y, v.z);
    func_8C07C580(pno2, -v.x, -v.y, -v.z);
}

/* event request: twp->flag |= 0x1000, twp->smode = mode */
void SetInputP(Uint8 pno, Sint32 mode)
{
    taskwk *twp;

    twp = playertwp[pno];
    twp->flag |= 0x1000;
    twp->smode = mode;
}

/* SetVelocityP: world speed (x, y, z) */
void func_8C07C676(Uint8 pno, Float x, Float y, Float z)
{
    NJS_VECTOR v;
    taskwk *twp;
    playerwk *pwp;
    motionwk *mwp;

    twp = playertwp[pno];
    mwp = playermwp[pno];
    pwp = playerpwp[pno];
    mwp->spd.x = x;
    mwp->spd.y = y;
    mwp->spd.z = z;
    v.x = x;
    v.y = y;
    v.z = z;
    PConvertVector_G2P(twp, &v);
    /* |v| <= 0.001 -> 0, written as !(>) like the FCMP/GT code so that a NaN
     * is cleared on PC too (0.00099999996f = SHC 0.001f, 0x3A83126E) */
    if (!(v.x > 0.00099999996f) && !(v.x < -0.00099999996f)) {
        v.x = 0.0f;
    }
    if (!(v.z > 0.00099999996f) && !(v.z < -0.00099999996f)) {
        v.z = 0.0f;
    }
    pwp->spd = v;
}

/* event request smode 3 with speed (x, y, z) */
void func_8C07C720(Uint8 pno, Float x, Float y, Float z)
{
    taskwk *twp;

    twp = playertwp[pno];
    twp->flag |= 0x1000;
    twp->smode = 3;
    func_8C07C676(pno, x, y, z);
}
