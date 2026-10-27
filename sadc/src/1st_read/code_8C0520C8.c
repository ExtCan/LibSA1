#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_funcs.h"
#include "sa1/engine.h"

/*
 * 8C0520C8: landtable display list (SADX ListGroundForDrawing-like).
 * Builds B_8C75A30C (count B_8C75A308, max 1024) from the entries of the
 * current landtable B_8C7596F4 whose slAttribute has 0x80000000 (visible),
 * whose blockbit is 0 or shares a bit with D_8C19E9F8, and whose bounding
 * sphere (xWidth = radius) is within the clip distance of a point half the
 * clip distance in front of the camera. The clip distance is the
 * landtable's fFarClipping when ssAttribute has 4, else B_8C88F614;
 * entries with slAttribute 0x20 always use B_8C88F614 (unhalved).
 * Also resets B_8C75C30C to -1. Called by func_8C0519C0 and func_8C051E00.
 */

/*
 * Landtable entry (SADX _OBJ_LANDENTRY / COL), 0x24 bytes. Same layout as
 * the file-local copy in code_8C0522C0.c; file-local until the shared
 * headers carry it.
 */
typedef struct _OBJ_LANDENTRY {
    Float xCenter;              /* 0x00 */
    Float yCenter;              /* 0x04 */
    Float zCenter;              /* 0x08 */
    Float xWidth;               /* 0x0C bounding radius */
    Float yWidth;               /* 0x10 */
    Float zWidth;               /* 0x14 */
    NJS_OBJECT *pObject;        /* 0x18 */
    Sint32 blockbit;            /* 0x1C */
    Uint32 slAttribute;         /* 0x20 */
} _OBJ_LANDENTRY;               /* 0x24 */

SA1_ASSERT_SIZE(_OBJ_LANDENTRY, 0x24);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, pObject, 0x18);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, blockbit, 0x1C);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, slAttribute, 0x20);

/*
 * Landtable header (SADX _OBJ_LANDTABLE), only the fields used here.
 * File-local until the shared headers carry it.
 */
typedef struct _OBJ_LANDTABLE {
    Sint16 ssCount;             /* 0x00 number of entries in pLandEntry */
    Sint16 ssMotCount;          /* 0x02 */
    Sint16 ssAttribute;         /* 0x04 bit 0x4: use fFarClipping */
    Sint16 ssLoadFlag;          /* 0x06 */
    Float fFarClipping;         /* 0x08 */
    _OBJ_LANDENTRY *pLandEntry; /* 0x0C */
} _OBJ_LANDTABLE;

SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, ssAttribute, 0x04);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, fFarClipping, 0x08);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, pLandEntry, 0x0C);

extern _OBJ_LANDTABLE *B_8C7596F4;     /* current landtable */
extern Sint16 B_8C75C30C;              /* reset to -1 here */
extern Float B_8C88F614;               /* default clip distance */
extern Sint32 D_8C19E9F8;              /* active block bits (data) */
extern _OBJ_LANDENTRY *B_8C75A30C[];   /* display list, max 1024 */
extern Sint16 B_8C75A308;              /* count of B_8C75A30C */

/*
 * NON_MATCHING: same instructions, same order and same control flow as the
 * original; only register allocation differs (29 lines): y/z of the view
 * point are in FR9/FR10 instead of FR10/FR9, and the loop invariants
 * 0x80000000 / &B_8C75A308 / 32 / 1024 (and &B_8C7596F4 / camera_twp before
 * the loop) get R7/R11/R6/R12 (R11/R12) instead of R12/R6/R7/R11 (R12/R11).
 * Structural details that the original needs and this source has:
 * blockbit read in two statements (no CSE), the 1024 exit storing the count
 * and returning (hoists &B_8C75A308), "ent++, i--" order, the empty-then
 * compare in the first branch (BF add; BRA next), and the squared distance
 * written left-nested (SHC emits FMUL dy*dy, FMAC dx, FMAC dz).
 */
#ifdef NON_MATCHING
void func_8C0520C8(void)
{
    _OBJ_LANDENTRY *ent;
    Float clip;
    Float x;
    Float y;
    Float z;
    Float r;
    Float dx;
    Float dy;
    Float dz;
    Float d;
    Sint16 i;
    Sint16 n;
    NJS_POINT3 v;

    ent = B_8C7596F4->pLandEntry;
    B_8C75C30C = -1;
    if (B_8C7596F4->ssAttribute & 4) {
        clip = B_8C7596F4->fFarClipping;
    } else {
        clip = B_8C88F614;
    }
    v.x = v.y = 0.0f;
    v.z = -(clip * 0.5f);
    clip *= 0.5f;
    njPushMatrix(D_8C67C640);
    camera_twp->ang.y ? njRotateY(NULL, camera_twp->ang.y) : (void)0;
    camera_twp->ang.x ? njRotateX(NULL, camera_twp->ang.x) : (void)0;
    njCalcPoint(NULL, &v, &v);
    njPopMatrix(1);
    x = camera_twp->pos.x + v.x;
    y = camera_twp->pos.y + v.y;
    z = camera_twp->pos.z + v.z;
    n = 0;
    for (i = B_8C7596F4->ssCount - 1; i >= 0; ent++, i--) {
        if (ent->blockbit != 0) {
            if (!(ent->blockbit & D_8C19E9F8)) {
                continue;
            }
        }
        if (ent->slAttribute & 0x80000000) {
            if (!(ent->slAttribute & 0x20)) {
                r = clip + ent->xWidth;
                dx = x - ent->xCenter;
                dy = y - ent->yCenter;
                dz = z - ent->zCenter;
                r *= r;
                /* SH-4: FMUL dy*dy, FMAC dx*dx, FMAC dz*dz */
                d = SH_ONLY(dx * dx + dy * dy + dz * dz)
                    PC_ONLY(SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy)));
                if (d <= r) {
                } else {
                    continue;
                }
            } else {
                r = B_8C88F614 + ent->xWidth;
                dx = x - ent->xCenter;
                dy = y - ent->yCenter;
                dz = z - ent->zCenter;
                r *= r;
                d = SH_ONLY(dx * dx + dy * dy + dz * dz)
                    PC_ONLY(SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy)));
                if (d > r) {
                    continue;
                }
            }
            B_8C75A30C[n++] = ent;
            if (n >= 1024) {
                B_8C75A308 = n;
                return;
            }
        }
    }
    B_8C75A308 = n;
}
#else
ASM_STUB(void func_8C0520C8(void))
#endif
