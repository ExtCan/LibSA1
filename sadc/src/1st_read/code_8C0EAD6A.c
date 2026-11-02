/*
 * 1ST_READ 8C0EAD6A (chunk 8C0EAA80, between code_8C0EAA80.c and
 * code_8C0EAE0A.c; staged by dc-fishcam for 0.9.6; gsa1_090_bigfish.md 2.1).
 *
 *  func_8C0EAD6A  the catch shot's camera target: B_8C6B6E68.tgt = player 0
 *                 + (100, 0, 0) turned by ang.z, ang.x and 0x8000 - ang.y
 *                 on a pushed unit matrix. PConvertVector_P2G turns by
 *                 -ang.y (code_8C078E40.c), so the point is 100 units
 *                 BEHIND the player; func_8C0EAA80 then places the camera
 *                 50 units in front of him, looking at him (bigfish review
 *                 R1, UNVERIFIED in game). R4 (the lure task its callers
 *                 pass) is not read; the prototype follows the callers'
 *                 declarations (code_8C0E0E70.c, code_8C0E91E4.c).
 *                 Called every step of BigModeMachine cases 37 / 45 (the
 *                 catch shown) and by the lure code (8C0E91E4, 8C0E943C,
 *                 8C0E9CC6).
 *
 * Matching note: "a ? njRotateN(NULL, a) : (void)0" gives the stack
 * temporary of the rotations (as code_8C078E40.c).
 */
#include "sa1/sa1.h"

/* Big's fishing camera points (B_8C6B6E68; as BIGFISHCAM_EAA80 in
 * code_8C0EAA80.c). No pointers. */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 lure position */
    NJS_POINT3 tgt;             /* 0x0C camera target */
} BIGFISHCAM_EAD6A;

extern BIGFISHCAM_EAD6A B_8C6B6E68;

void func_8C0EAD6A(task *tp)
{
    NJS_POINT3 vs;
    NJS_POINT3 v;
    taskwk *twp;

    twp = playertwp[0];
    njPushMatrix(D_8C67C640);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    vs.x = 100.0f;
    vs.y = 0.0f;
    vs.z = 0.0f;
    njCalcVector(NULL, &vs, &v);
    njPopMatrix(1);
    B_8C6B6E68.tgt.x = v.x + twp->pos.x;
    B_8C6B6E68.tgt.y = v.y + twp->pos.y;
    B_8C6B6E68.tgt.z = v.z + twp->pos.z;
}
