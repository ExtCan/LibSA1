#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_funcs.h"
#include "sa1/collision.h"

/*
 * 8C0522C0 ListGroundForCollision(x, y, z, r): per-query landtable gather
 * (19 callers). Collects into B_8C754EF4 (count B_8C754EF0, at most 1024
 * entries) every land collision object of the source list B_8C757EF4
 * (walked from the last entry down) whose model bounding sphere comes
 * within r + 30 of (x, y, z); with UseRotation (0x10000000) the model
 * centre is first rotated by the object angles (Z, X, Y). Then, when
 * D_8C19E978 == 1 and a landtable is loaded, every landtable entry of
 * B_8C7584F4 with an attribute in 0x400003 whose bounding sphere touches
 * the query sphere is appended as {attr, object, NULL}.
 *
 * Matching notes (NON_MATCHING, 1 instruction short):
 *  - Everything matches except the end of the landtable test: the original
 *    copies the njSqrt result to FR4 (FMOV FR0,FR4) and loads xWidth into
 *    FR5 before the FSUB, i.e. the result went through a variable. Every
 *    spelling with a variable for it (new, reused dist/rr/dx/dy/dz, block
 *    scoped, "register", assignment inside the condition, xWidth or r in a
 *    variable, a #pragma inline compare helper) moves y/z or the loop-2
 *    differences between FR12-FR15 and the stack (230+ differing), unless
 *    the squares are written "dx = dx * dx", which costs FMOV copies.
 *  - Found on the way (all needed for the rest to match): the guarded
 *    rotations are the "a ? f(a) : (void)0" macro form (one load into R8),
 *    rr is squared in place ("rr *= rr"), the landtable centre is read
 *    through a Float pointer (@R4+ loads), the squares are written dy, dx,
 *    dz (register choice FR15/FR13/FR14), the loop uses "continue" (jump
 *    to the bottom test instead of a duplicated guard) and "ent++, i--".
 *  - The first distance is FMUL dy*dy, FMAC dx*dx, FMAC dz*dz; SHC only
 *    gives that order and allocation for the plain left-to-right sum, so
 *    the SHC form of LEN2 is the plain sum and the TARGET_PC form spells
 *    out the same fusion with SA1_FMAC (as DOT in code_8C02A680.c).
 */

/*
 * Landtable entry (SADX _OBJ_LANDENTRY / COL), 0x24 bytes. B_8C7584F4
 * holds up to 128 of them (bss 4608 bytes), copied per step by 8C052518
 * and transformed by 8C052BA0, which writes the world-space bounding
 * sphere: centre (njCalcPoint) and radius at +0x0C (model->r when the
 * three widths are 0, else njSqrt of their squares). D_8C19E968 = count.
 * File-local until the shared headers carry it (sa1/collision.h).
 */
typedef struct _OBJ_LANDENTRY {
    NJS_POINT3 center;          /* 0x00 xCenter, yCenter, zCenter */
    Float xWidth;               /* 0x0C bounding radius after 8C052BA0 */
    Float yWidth;               /* 0x10 */
    Float zWidth;               /* 0x14 */
    NJS_OBJECT *pObject;        /* 0x18 copied to _OBJ_LANDCOLL.pObject */
    Sint32 blockbit;            /* 0x1C */
    Uint32 slAttribute;         /* 0x20 copied to _OBJ_LANDCOLL.slAttribute */
} _OBJ_LANDENTRY;               /* 0x24 */

SA1_ASSERT_SIZE(_OBJ_LANDENTRY, 0x24);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, xWidth, 0x0C);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, pObject, 0x18);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, slAttribute, 0x20);

extern _OBJ_LANDENTRY B_8C7584F4[];         /* landtable entries of the current step, count D_8C19E968 */
extern Sint32 D_8C19E968;                   /* number of entries in B_8C7584F4 */
extern Sint32 D_8C19E978;                   /* 1 = gather landtable entries too (8C05289E sets 0, 8C0528A6 sets 1) */
extern struct _OBJ_LANDTABLE *B_8C7596F4;   /* current landtable (COL list at +0x0C, 8C052BA0); only tested here */

#ifdef NON_MATCHING

#ifdef TARGET_PC
#define LEN2(x, y, z) SA1_FMAC((z), (z), SA1_FMAC((x), (x), (y) * (y)))
#else
#define LEN2(x, y, z) ((x) * (x) + (y) * (y) + (z) * (z))
#endif

void func_8C0522C0(Float x, Float y, Float z, Float r)
{
    _OBJ_LANDCOLL *dst;
    _OBJ_LANDCOLL *src;
    NJS_OBJECT *obj;
    _OBJ_LANDENTRY *ent;
    Sint16 i;
    Float dx;
    Float dy;
    Float dz;
    Float dist;
    Float rr;
    Float *p;
    NJS_POINT3 c;

    dst = B_8C754EF4;
    B_8C754EF0 = 0;
    njPushMatrix(NULL);
    for (src = &B_8C757EF4[D_8C19E964 - 1]; src >= B_8C757EF4; src--) {
        obj = src->pObject;
        if (!(src->slAttribute & 0x10000000)) {
            dx = x - obj->pos[0] - obj->model->center.x;
            dy = y - obj->pos[1] - obj->model->center.y;
            dz = z - obj->pos[2] - obj->model->center.z;
        } else {
            njUnitMatrix(NULL);
            obj->ang[2] ? njRotateZ(NULL, obj->ang[2]) : (void)0;
            obj->ang[0] ? njRotateX(NULL, obj->ang[0]) : (void)0;
            obj->ang[1] ? njRotateY(NULL, obj->ang[1]) : (void)0;
            njCalcPoint(NULL, &obj->model->center, &c);
            dx = x - obj->pos[0] - c.x;
            dy = y - obj->pos[1] - c.y;
            dz = z - obj->pos[2] - c.z;
        }
        dist = LEN2(dx, dy, dz);
        rr = obj->model->r + r + 30.0f;
        rr *= rr;
        if (dist < rr) {
            dst->pObject = obj;
            dst->pTask = src->pTask;
            dst->slAttribute = src->slAttribute;
            B_8C754EF0++;
            dst++;
            if (B_8C754EF0 >= 1024) {
                njPopMatrix(1);
                return;
            }
        }
    }
    njPopMatrix(1);
    if (D_8C19E978 == 1 && B_8C7596F4 != NULL) {
        ent = B_8C7584F4;
        for (i = D_8C19E968; i > 0; ent++, i--) {
            if (!(ent->slAttribute & 0x400003)) {
                continue;
            }
            p = &ent->center.x;
            dx = *p++ - x;
            dy = *p++ - y;
            dz = *p - z;
            dy *= dy;
            dx *= dx;
            dz *= dz;
            if (njSqrt(dx + dy + dz) - r < ent->xWidth) {
                dst->slAttribute = ent->slAttribute;
                dst->pTask = NULL;
                dst->pObject = ent->pObject;
                B_8C754EF0++;
                dst++;
                if (B_8C754EF0 >= 1024) {
                    return;
                }
            }
        }
    }
}

#else
ASM_STUB(void func_8C0522C0(Float x, Float y, Float z, Float r))
#endif /* NON_MATCHING */
