/*
 * 1ST_READ 8C0EAA80-8C0EAD6A (Big the Cat, fishing camera helpers):
 *
 *  func_8C0EAA80  copies the fishing camera target (B_8C6B6E68.tgt) into the
 *                 camera control work and places the camera 50 units from
 *                 player 0 on the line target -> player (20 units up).
 *                 Callers: func_8C0E91E4, func_8C0E943C, func_8C0E9CC6.
 *  func_8C0EAB68  sets the fishing camera target from the rod tip task tp
 *                 (exec func_8C0E6CC0): the horizontal offset player 0 ->
 *                 tp, limited to 30 units, at the height kept in tp's any
 *                 work.
 *  func_8C0EABEE  when the lure task exists: stores tp's position in
 *                 B_8C6B6E68.pos and moves the camera target towards a point
 *                 10 units from tp, towards the hooked fish (B_8C7A40CC,
 *                 unless flag 1 of B_8C7A40E8) or 10 units away from
 *                 player 0; it snaps when closer than 5, else steps 10.
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * Camera control work (SADX _camcontwk), reached through the pointer
 * D_8C112048 (also used by the camera code 8C019400-8C023C20). Only the
 * fields used here; no pointers, so the layout is the same on PC.
 */
typedef struct {
    Sint32 cammode;             /* 0x00 */
    Sint32 camreqmode;          /* 0x04 */
    Sint32 prevcammode;         /* 0x08 */
    NJS_POINT3 pos;             /* 0x0C camera position */
    NJS_POINT3 tgtpos;          /* 0x18 target position */
    Angle angx;                 /* 0x24 */
    Angle angy;                 /* 0x28 */
    Angle angz;                 /* 0x2C cleared by func_8C0EAA80 */
} CAMCONTWK_EAA80;

/* any work (task.awp) of the task func_8C0E6620 creates (exec
 * func_8C0E6CC0); same view as BIGCHILDWK in code_8C0E6620.c. No pointers. */
typedef struct {
    Float rad;                  /* 0x00 */
    Float y;                    /* 0x04 height used for the camera target */
    Sint16 angy;                /* 0x08 */
    Sint16 pad0A;
    Sint16 w0C;                 /* 0x0C */
    Sint16 w0E;                 /* 0x0E */
} BIGCHILDWK_EAA80;             /* 0x10 */

extern CAMCONTWK_EAA80 *D_8C112048;
/* Big's fishing camera points (B_8C6B6E68; the code reaches the target
 * through the base address with displacement 12). No pointers. */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 rod tip position */
    NJS_POINT3 tgt;             /* 0x0C camera target */
} BIGFISHCAM_EAA80;

extern BIGFISHCAM_EAA80 B_8C6B6E68;
extern task *B_8C7A40C8;            /* lure task (func_8C0E6DA0's result) */
extern task *B_8C7A40CC;            /* hooked fish task */
extern Uint16 B_8C7A40E8;           /* Big fishing state bits */

void func_8C0EAA80(void)
{
    NJS_POINT3 cpos;
    NJS_VECTOR v;

    cpos = camera_twp->pos;
    D_8C112048->tgtpos.x = B_8C6B6E68.tgt.x;
    D_8C112048->tgtpos.y = B_8C6B6E68.tgt.y;
    D_8C112048->tgtpos.z = B_8C6B6E68.tgt.z;
    v.x = playertwp[0]->pos.x - D_8C112048->tgtpos.x;
    v.y = playertwp[0]->pos.y - D_8C112048->tgtpos.y + 20.0f;
    v.z = playertwp[0]->pos.z - D_8C112048->tgtpos.z;
    njUnitVector(&v);
    v.x *= 50.0f;
    v.y *= 50.0f;
    v.z *= 50.0f;
    D_8C112048->pos.x = playertwp[0]->pos.x + v.x;
    D_8C112048->pos.y = playertwp[0]->pos.y + v.y + 20.0f;
    D_8C112048->pos.z = playertwp[0]->pos.z + v.z;
    D_8C112048->angz = 0;
}

void func_8C0EAB68(task *tp)
{
    NJS_VECTOR v;
    taskwk *ptwp;

    ptwp = playertwp[0];
    v.x = tp->twp->pos.x - ptwp->pos.x;
    v.y = 0.0f;
    v.z = tp->twp->pos.z - ptwp->pos.z;
    if (njScalor(&v) < 30.0f) {
        njUnitVector(&v);
        v.x *= 30.0f;
        v.z *= 30.0f;
    }
    B_8C6B6E68.tgt.x = ptwp->pos.x + v.x;
    B_8C6B6E68.tgt.y = ((BIGCHILDWK_EAA80 *)tp->awp)->y;
    B_8C6B6E68.tgt.z = ptwp->pos.z + v.z;
}

void func_8C0EABEE(task *tp)
{
    NJS_VECTOR v;
    Float d;

    if (B_8C7A40C8 == NULL) {
        return;
    }
    B_8C6B6E68.pos = tp->twp->pos;
    if (B_8C7A40CC != NULL && !(B_8C7A40E8 & 1)) {
        v = B_8C7A40CC->twp->pos;
        njSubVector(&v, &tp->twp->pos);
        d = 10.0f;
    } else {
        v = playertwp[0]->pos;
        njSubVector(&v, &tp->twp->pos);
        d = -10.0f;
        v.y = tp->twp->pos.y;
    }
    njUnitVector(&v);
    v.x = SA1_FMAC(d, v.x, tp->twp->pos.x);
    v.y = SA1_FMAC(d, v.y, tp->twp->pos.y);
    v.z = SA1_FMAC(d, v.z, tp->twp->pos.z);
    if (njDistanceP2P(&v, &B_8C6B6E68.tgt) < 5.0f) {
        B_8C6B6E68.tgt.x = v.x;
        B_8C6B6E68.tgt.y = v.y;
        B_8C6B6E68.tgt.z = v.z;
    } else {
        njSubVector(&v, &B_8C6B6E68.tgt);
        njUnitVector(&v);
        SA1_FMAC_ACC(B_8C6B6E68.tgt.x, 10.0f, v.x);
        SA1_FMAC_ACC(B_8C6B6E68.tgt.y, 10.0f, v.y);
        SA1_FMAC_ACC(B_8C6B6E68.tgt.z, 10.0f, v.z);
    }
}
