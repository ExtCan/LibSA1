#include "common.h"
#include "ninja.h"
#include "sa1/engine.h"

/*
 * Head / eye "look at" turning (1ST_READ 8C07AEF8-8C07B04A, the tail of the
 * 8C07A55C chunk after PJoinVertexes).
 *
 * func_8C07AEF8  turn the head angles (y, z) towards their targets with
 *                AdjustAngle (step 0x1000), clamp them to [min, max] and
 *                write them into the head object's ang[1]/ang[2].
 * func_8C07AF8C  same for the eyes (step 0x800): both eyes follow one
 *                target pair; the left eye's y is clamped to its own range,
 *                the right eye's y to its own range, and one z (clamped to
 *                the 0x50/0x54 range) is used for both eyes.
 * Callers (event/NPC character objects): 8C0B28EE, 8C0BE1CE, 8C0C8A80,
 * 8C0D1916, 8C0DDA00, 8C0E5DF0.
 */

typedef struct {
    Angle head_tgt_y;           /* 0x00 */
    Angle head_tgt_z;           /* 0x04 */
    Angle head_y;               /* 0x08 */
    Angle head_z;               /* 0x0C */
    Angle eye_tgt_y;            /* 0x10 */
    Angle eye_tgt_z;            /* 0x14 */
    Angle eyeL_y;               /* 0x18 */
    Angle eyeL_z;               /* 0x1C */
    Angle eyeR_y;               /* 0x20 */
    Angle eyeR_z;               /* 0x24 */
    Angle head_y_max;           /* 0x28 */
    Angle head_y_min;           /* 0x2C */
    Angle head_z_max;           /* 0x30 */
    Angle head_z_min;           /* 0x34 */
    Angle eyeL_y_max;           /* 0x38 */
    Angle eyeL_y_min;           /* 0x3C */
    Angle eyeL_z_max;           /* 0x40 not used here */
    Angle eyeL_z_min;           /* 0x44 not used here */
    Angle eyeR_y_max;           /* 0x48 */
    Angle eyeR_y_min;           /* 0x4C */
    Angle eye_z_max;            /* 0x50 */
    Angle eye_z_min;            /* 0x54 */
    NJS_OBJECT *head;           /* 0x58 */
    NJS_OBJECT *eyeL;           /* 0x5C */
    NJS_OBJECT *eyeR;           /* 0x60 */
} lookwk;                       /* >= 0x64 */

SA1_ASSERT_OFFSET(lookwk, head_y_max, 0x28);
SA1_ASSERT_OFFSET(lookwk, eye_z_min, 0x54);
SA1_ASSERT_OFFSET(lookwk, head, 0x58);
SA1_ASSERT_OFFSET(lookwk, eyeR, 0x60);

void func_8C07AEF8(lookwk *lwp)
{
    Angle ang;

    ang = AdjustAngle(lwp->head_y, lwp->head_tgt_y, 0x1000);
    if (ang > 0x8000)
        ang |= ~0xFFFF;
    if (ang > lwp->head_y_max)
        ang = lwp->head_y_max;
    else if (ang < lwp->head_y_min)
        ang = lwp->head_y_min;
    lwp->head_y = ang;
    lwp->head->ang[1] = (Uint16)lwp->head_y;

    ang = AdjustAngle(lwp->head_z, lwp->head_tgt_z, 0x1000);
    if (ang > 0x8000)
        ang |= ~0xFFFF;
    if (ang > lwp->head_z_max)
        ang = lwp->head_z_max;
    else if (ang < lwp->head_z_min)
        ang = lwp->head_z_min;
    lwp->head_z = ang;
    lwp->head->ang[2] = (Uint16)lwp->head_z;
}

void func_8C07AF8C(lookwk *lwp)
{
    Angle ang;

    ang = AdjustAngle(lwp->eyeL_y, lwp->eye_tgt_y, 0x800);
    if (ang & 0x8000)
        ang |= ~0xFFFF;
    if (ang > lwp->eyeL_y_max)
        ang = lwp->eyeL_y_max;
    else if (ang < lwp->eyeL_y_min)
        ang = lwp->eyeL_y_min;
    lwp->eyeL_y = ang;
    lwp->eyeL->ang[1] = lwp->eyeL_y;

    ang = AdjustAngle(lwp->eyeR_y, lwp->eye_tgt_y, 0x800);
    if (ang & 0x8000)
        ang |= ~0xFFFF;
    if (ang < lwp->eyeR_y_min)
        ang = lwp->eyeR_y_min;
    else if (ang > lwp->eyeR_y_max)
        ang = lwp->eyeR_y_max;
    lwp->eyeR_y = ang;
    lwp->eyeR->ang[1] = lwp->eyeR_y;

    ang = AdjustAngle(lwp->eyeL_z, lwp->eye_tgt_z, 0x800);
    if (ang & 0x8000)
        ang |= ~0xFFFF;
    if (ang < lwp->eye_z_min)
        ang = lwp->eye_z_min;
    else if (ang > lwp->eye_z_max)
        ang = lwp->eye_z_max;
    lwp->eyeL_z = ang;
    lwp->eyeL->ang[2] = lwp->eyeL_z;
    lwp->eyeR_z = ang;
    lwp->eyeR->ang[2] = lwp->eyeR_z;
}
