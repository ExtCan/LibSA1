/*
 * CCL debug display, part 1 (8C02E820-8C02EC92): draws a collision shape
 * (CCL_INFO) with the debug models. Called per shape form by
 * func_8C02F27C(twp, info, pos) (form = info->attr & 0xF).
 *  func_8C02E820  debug draw color from info->flag & 3
 *  func_8C02E886  form 0: sphere
 *  func_8C02E8DA  form 1: cylinder (no rotation)
 *  func_8C02E980  form 2: cylinder, full rotation
 *  func_8C02EAD2  form 4: box (y rotation only)
 *  func_8C02EBCE  form 5: box (y and z rotation)
 */
#include "sa1/sa1.h"

extern NJS_MODEL Debug_Cylinder;            /* 8C199918 */
/* declared as an array like code_8C02EC92.c */
extern NJS_MODEL Debug_Cube_1[];            /* 8C199B6C */

/* set the debug draw color from info->flag & 3 */
void func_8C02E820(CCL_INFO *info)
{
    switch (info->flag & 3) {
    case 0:
        func_8C043A20(0.03f, 0.0f, 1.0f, 0.0f);
        break;
    case 1:
        func_8C043A20(0.03f, 0.5f, 0.5f, 0.0f);
        break;
    case 2:
        func_8C043A20(0.03f, 0.5f, 0.25f, 0.0f);
        break;
    case 3:
        func_8C043A20(0.03f, 1.0f, 0.0f, 0.0f);
        break;
    }
}

/* form 0: sphere */
void func_8C02E886(CCL_INFO *info, NJS_POINT3 *pos)
{
    Float r;

    r = info->a * 0.099999996f;
    njPushMatrix(NULL);
    njTranslate(NULL, pos->x, pos->y, pos->z);
    njScale(NULL, r, r, r);
    func_8C02E820(info);
    func_8C6215D6(&Debug_Sphere);
    njPopMatrix(1);
    return; /* the explicit return gives the 4-byte frame (ADD #-4) instead of a push of info */
}

/* form 1: cylinder */
void func_8C02E8DA(CCL_INFO *info, NJS_POINT3 *pos)
{
    Float r;
    Float h;

    r = info->a * 0.099999996f;
    h = info->b * 0.099999996f;
    njPushMatrix(NULL);
    njTranslate(NULL, pos->x, pos->y, pos->z);
    njScale(NULL, r, h, r);
    func_8C02E820(info);
    func_8C6215D6(&Debug_Cylinder);
    njPopMatrix(1);
}

/* form 2: cylinder, full rotation */
void func_8C02E980(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos)
{
    Float r;
    Float h;

    r = info->a * 0.099999996f;
    h = info->b * 0.099999996f;
    njPushMatrix(NULL);
    njTranslate(NULL, pos->x, pos->y, pos->z);
    if (info->attr & 0x40) {
        if (!(info->attr & 0x20) && !(info->attr & 0x2000)) {
            twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        }
        info->angy ? njRotateY(NULL, info->angy) : (void)0;
        info->angz ? njRotateZ(NULL, info->angz) : (void)0;
        info->angx ? njRotateX(NULL, info->angx) : (void)0;
    } else {
        if (!(info->attr & 0x20) && !(info->attr & 0x2000)) {
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
            twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        }
        info->angz ? njRotateZ(NULL, info->angz) : (void)0;
        info->angx ? njRotateX(NULL, info->angx) : (void)0;
        info->angy ? njRotateY(NULL, info->angy) : (void)0;
    }
    func_8C02E820(info);
    njScale(NULL, r, h, r);
    func_8C02E820(info);
    func_8C6215D6(&Debug_Cylinder);
    njPopMatrix(1);
}

/* form 4: box, y rotation only */
void func_8C02EAD2(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos)
{
    Float x;
    Float y;
    Float z;

    x = info->a * 0.099999996f;
    y = info->b * 0.099999996f;
    z = info->c * 0.099999996f;
    njPushMatrix(NULL);
    njTranslate(NULL, pos->x, pos->y, pos->z);
    if ((info->attr & 0x20) || (info->attr & 0x2000)) {
        info->angy ? njRotateY(NULL, info->angy) : (void)0;
    } else {
        (twp->ang.y + info->angy) ? njRotateY(NULL, twp->ang.y + info->angy) : (void)0;
    }
    njScale(NULL, x * 2.0f, y * 2.0f, z * 2.0f);
    func_8C02E820(info);
    func_8C6215D6(Debug_Cube_1);
    njPopMatrix(1);
}

/* form 5: box, y and z rotation */
void func_8C02EBCE(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos)
{
    Float x;
    Float y;
    Float z;

    x = info->a * 0.099999996f;
    y = info->b * 0.099999996f;
    z = info->c * 0.099999996f;
    njPushMatrix(NULL);
    njTranslate(NULL, pos->x, pos->y, pos->z);
    if ((info->attr & 0x20) || (info->attr & 0x2000)) {
        info->angy ? njRotateY(NULL, info->angy) : (void)0;
    } else {
        (twp->ang.y + info->angy) ? njRotateY(NULL, twp->ang.y + info->angy) : (void)0;
    }
    info->angz ? njRotateZ(NULL, info->angz) : (void)0;
    njScale(NULL, x * 2.0f, y * 2.0f, z * 2.0f);
    func_8C02E820(info);
    func_8C6215D6(Debug_Cube_1);
    njPopMatrix(1);
}
