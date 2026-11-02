/*
 * 1ST_READ 8C0239C2 (chunk 8C022580): the FISHING camera mode (mode table
 * rows 26 / 27; staged by dc-fishcam for 0.9.6; gsa1_090_bigfish.md 2.1).
 *
 *  func_8C0239C2  Big's fishing camera while he aims and the lure flies:
 *                 target = the fishing camera target B_8C6B6E68.tgt (the
 *                 cast cursor, 8C0EAB68); the camera sits 50 units from
 *                 Big + (0, 20, 0) on the line target -> Big + (0, 20, 0)
 *                 (the placement of func_8C0EAA80), then camera collision:
 *                 func_8C01B684 (push out of CAM COLI volumes),
 *                 func_8C01BD3C (blend from the previous camera; its
 *                 counter 8C18B86C has no writer, so it does nothing:
 *                 bigfish review R2.3) and func_8C01E32A (camera against
 *                 the land), both from the previous camera position
 *                 (camera_twp->pos). Clears the camera roll.
 *                 Pushed by func_8C0E6586 (8C01AD2C(8C0239C2, 3, 2)) and
 *                 func_8C0E91E4 / func_8C0E943C (adjust 0, a cut). It does
 *                 not read the camera parameters the camera task passes in
 *                 R4; the prototype follows its callers' declarations
 *                 (code_8C0E6500.c, code_8C0E91E4.c).
 */
#include "sa1/sa1.h"

/* camera work *D_8C112048 (v1_triggers.md 1.2; the same view as
 * CAMCONTWK_EAA80 in code_8C0EAA80.c). No pointers. */
typedef struct {
    Sint32 w00;                 /* 0x00 */
    Sint32 w04;                 /* 0x04 */
    Sint32 w08;                 /* 0x08 */
    NJS_POINT3 pos;             /* 0x0C camera position */
    NJS_POINT3 tgt;             /* 0x18 camera target */
    Angle angx;                 /* 0x24 */
    Angle angy;                 /* 0x28 */
    Angle angz;                 /* 0x2C roll */
} CAMWK_39C2;

/* Big's fishing camera points (B_8C6B6E68; as BIGFISHCAM_EAA80) */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 lure position */
    NJS_POINT3 tgt;             /* 0x0C camera target */
} BIGFISHCAM_39C2;

extern CAMWK_39C2 *D_8C112048;
extern BIGFISHCAM_39C2 B_8C6B6E68;

/* camera collision helpers (asm; LIB policies: gsa1_090_bigfish.md F1 and
 * review R2.3 / R2.4) */
Sint32 func_8C01B684(NJS_POINT3 *pos);
void func_8C01BD3C(NJS_POINT3 *from, NJS_POINT3 *pos);
void func_8C01E32A(NJS_POINT3 *from, NJS_POINT3 *pos);

void func_8C0239C2(void)
{
    NJS_POINT3 cpos;
    NJS_VECTOR v;

    cpos = camera_twp->pos;
    D_8C112048->tgt.x = B_8C6B6E68.tgt.x;
    D_8C112048->tgt.y = B_8C6B6E68.tgt.y;
    D_8C112048->tgt.z = B_8C6B6E68.tgt.z;
    v.x = playertwp[0]->pos.x - D_8C112048->tgt.x;
    v.y = playertwp[0]->pos.y - D_8C112048->tgt.y + 20.0f;
    v.z = playertwp[0]->pos.z - D_8C112048->tgt.z;
    njUnitVector(&v);
    v.x *= 50.0f;
    v.y *= 50.0f;
    v.z *= 50.0f;
    D_8C112048->pos.x = playertwp[0]->pos.x + v.x;
    D_8C112048->pos.y = playertwp[0]->pos.y + v.y + 20.0f;
    D_8C112048->pos.z = playertwp[0]->pos.z + v.z;
    func_8C01B684(&D_8C112048->pos);
    func_8C01BD3C(&cpos, &D_8C112048->pos);
    func_8C01E32A(&cpos, &D_8C112048->pos);
    D_8C112048->angz = 0;
}
