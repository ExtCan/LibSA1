/*
 * 1ST_READ 8C0E6500-8C0E65D0 (Big, mode handlers called by BigModeMachine
 * 8C0E0E70):
 *
 *  func_8C0E6500  camera reset (func_8C01B158), camera put 10 above player
 *                 0 and pulled 99% of the way to him in x/z, then action
 *                 63, mode 39 and two sound effects (0x323, 0x4BC).
 *  func_8C0E6586  event camera: func_8C01ABDC(8, 5) when func_8C04C960()
 *                 is 1, else camera function func_8C0239C2 (3, 2).
 *  func_8C0E65A8  func_8C01B158 when bit 0x40 of B_8C7A40E8 (Big's status
 *                 bits, code_8C0DFFA0.c) is set, else func_8C01AE60
 *                 (release the event camera).
 *  func_8C0E65BE  mode 1, action 0, idle timer 0 (also called by several
 *                 other Big mode handlers).
 *
 * Matching notes: the two FMACs of func_8C0E6500 must be written addend
 * first ("c + a * b"): SA1_FMAC's "a * b + c" makes SHC load the camera
 * pointer once (copy) instead of twice and reorders the loads.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"         /* func_8C01AD2C */

extern void func_8C01B158(void);
extern Sint32 func_8C04C960(void);
extern void func_8C01ABDC(Sint32 mode, Sint32 a);
extern void func_8C0239C2(void);
extern void func_8C01AE60(void);
extern Uint16 B_8C7A40E8;   /* MOV.W + EXTU.W before TST #64 */

/* c + a * b at an FMAC site, addend evaluated first; fused like SA1_FMAC on PC */
#if defined(TARGET_PC)
#define FMAC_ADD(c, a, b) SA1_FMAC((a), (b), (c))
#else
#define FMAC_ADD(c, a, b) ((c) + (a) * (b))
#endif

void func_8C0E6500(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    func_8C01B158();
    camera_twp->pos.y = playertwp[0]->pos.y + 10.0f;
    camera_twp->pos.x = FMAC_ADD(playertwp[0]->pos.x,
                                 camera_twp->pos.x - playertwp[0]->pos.x, 0.0099999998f);
    camera_twp->pos.z = FMAC_ADD(playertwp[0]->pos.z,
                                 camera_twp->pos.z - playertwp[0]->pos.z, 0.0099999998f);
    pwp->mj.reqaction = 63;
    twp->mode = 39;
    func_8C0940DA(0x323, 0, 0, 0);
    func_8C0940DA(0x4BC, 0, 0, 0);
}

void func_8C0E6586(void)
{
    if (func_8C04C960() == 1) {
        func_8C01ABDC(8, 5);
    } else {
        func_8C01AD2C(func_8C0239C2, 3, 2);
    }
}

void func_8C0E65A8(void)
{
    if (B_8C7A40E8 & 0x40) {
        func_8C01B158();
    } else {
        func_8C01AE60();
    }
}

void func_8C0E65BE(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    twp->mode = 1;
    pwp->mj.reqaction = 0;
    pwp->waittimer = 0;
}
