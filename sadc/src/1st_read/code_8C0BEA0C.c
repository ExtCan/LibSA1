/*
 * 1ST_READ 8C0BEA0C-8C0BEBE0 (last function of chunk 8C0BE160):
 *
 *  func_8C0BEA0C  places Tails' eye nodes (called by his look task
 *                 func_8C0BE1CE, code_8C0BE160.c, after the head/eye
 *                 angles are updated): Tails' eyes are moved by
 *                 translation instead of rotation. For each eye the saved
 *                 rest position is rotated by the eye angles (z, then y,
 *                 around the head origin), and the line from the origin
 *                 in that direction is cut with the plane through the rest
 *                 position with a fixed normal (z component mirrored for
 *                 the right eye); the cross point becomes the node
 *                 position.
 *
 * Matching notes: the rotations are written "a ? njRotateZ(NULL, a) :
 * (void)0" with no local (as in code_8C07061C.c): SHC keeps the common
 * subexpression in a 4-byte stack slot of its own (stored in the BT/S delay
 * slot, reloaded for the call) and alternates R2/R3 for the two loads like
 * the original; a volatile local gives the same store/reload but loads
 * every angle into R2.
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * Look work of Tails' look task (same layout as in code_8C0BE160.c): the
 * shared lookwk layout (sa1/player.h sa1_lookwk), except that 0x70..0x87
 * hold the saved eye node positions (Float) instead of the saved eye
 * angles.
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
    Angle eyeL_z_max;           /* 0x40 */
    Angle eyeL_z_min;           /* 0x44 */
    Angle eyeR_y_max;           /* 0x48 */
    Angle eyeR_y_min;           /* 0x4C */
    Angle eye_z_max;            /* 0x50 */
    Angle eye_z_min;            /* 0x54 */
    NJS_OBJECT *head;           /* 0x58 */
    NJS_OBJECT *eyeL;           /* 0x5C */
    NJS_OBJECT *eyeR;           /* 0x60 */
    Angle head_ang[3];          /* 0x64 saved head->ang */
    Float eyeL_pos[3];          /* 0x70 saved eyeL->pos */
    Float eyeR_pos[3];          /* 0x7C saved eyeR->pos */
} TAILS_LOOKWK;                 /* 0x88 */

SA1_ASSERT_SIZE(TAILS_LOOKWK, 0x88);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, eyeL_y, 0x18);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, eyeR_z, 0x24);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, eyeL, 0x5C);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, eyeL_pos, 0x70);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, eyeR_pos, 0x7C);

extern Float func_8C067414(NJS_VECTOR *v);  /* normalise (falls back to (0, 1, 0) when too short); returns the length */

/*
 * plane: point = rest position (its first 3 floats, also the vector that is
 * rotated), normal = (0.932, 0.0618, +-0.356); line: origin, rotated
 * direction (normalised). func_8C63FEAC is njDistanceL2PL-like (cross point
 * of the line and the plane into pos).
 */
void func_8C0BEA0C(TAILS_LOOKWK *lwp)
{
    NJS_LINE line;
    NJS_PLANE plane;
    NJS_POINT3 pos;

    njPushMatrix(D_8C67C640);
    lwp->eyeL_z ? njRotateZ(NULL, lwp->eyeL_z) : (void)0;
    lwp->eyeL_y ? njRotateY(NULL, lwp->eyeL_y) : (void)0;
    plane.px = lwp->eyeL_pos[0];
    plane.py = lwp->eyeL_pos[1];
    plane.pz = lwp->eyeL_pos[2];
    plane.vx = 0.93228497f;
    plane.vy = 0.061793f;
    plane.vz = 0.35640699f;
    line.px = 0.0f;
    line.py = 0.0f;
    line.pz = 0.0f;
    njCalcVector(NULL, (NJS_VECTOR *)&plane.px, (NJS_VECTOR *)&line.vx);
    func_8C067414((NJS_VECTOR *)&line.vx);
    func_8C63FEAC(&line, &plane, &pos);
    lwp->eyeL->pos[0] = pos.x;
    lwp->eyeL->pos[1] = pos.y;
    lwp->eyeL->pos[2] = pos.z;
    njUnitMatrix(NULL);
    lwp->eyeR_z ? njRotateZ(NULL, lwp->eyeR_z) : (void)0;
    lwp->eyeR_y ? njRotateY(NULL, lwp->eyeR_y) : (void)0;
    plane.px = lwp->eyeR_pos[0];
    plane.py = lwp->eyeR_pos[1];
    plane.pz = lwp->eyeR_pos[2];
    plane.vx = 0.93228497f;
    plane.vy = 0.061793f;
    plane.vz = -0.35640699f;
    line.px = 0.0f;
    line.py = 0.0f;
    line.pz = 0.0f;
    njCalcVector(NULL, (NJS_VECTOR *)&plane.px, (NJS_VECTOR *)&line.vx);
    func_8C067414((NJS_VECTOR *)&line.vx);
    func_8C63FEAC(&line, &plane, &pos);
    lwp->eyeR->pos[0] = pos.x;
    lwp->eyeR->pos[1] = pos.y;
    lwp->eyeR->pos[2] = pos.z;
    njPopMatrix(1);
}
