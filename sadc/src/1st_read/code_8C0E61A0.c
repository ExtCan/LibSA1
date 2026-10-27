/*
 * 1ST_READ 8C0E61A0-8C0E6376 (last three functions of chunk 8C0E5D20, Big):
 *
 *  func_8C0E61A0  places Big's eye nodes (called by his look task
 *                 func_8C0E5DF0 after the head/eye angles are updated):
 *                 like Tails (func_8C0BEA0C, code_8C0BEA0C.c) Big's eyes
 *                 are moved by translation instead of rotation. For each
 *                 eye the saved rest position is rotated by the eye angles
 *                 (z, then y) with a local matrix, and the line from the
 *                 origin in that direction is cut with the plane through the
 *                 rest position with a fixed normal; the cross point becomes
 *                 the node position.
 *  func_8C0E632A  0 while player 0 is in modes 32-37 or 40-45, else 1
 *                 (tp is not read; every caller passes its task in R4).
 *  func_8C0E6352  1 while the given (Big's) task is in modes 35-37 or 43-45.
 *
 * Matching notes: the rotations are written "a ? njRotateZ(m, a) : (void)0"
 * with no local (as in code_8C0BEA0C.c): SHC keeps the common subexpression
 * in a 4-byte stack slot of its own (stored in the BT/S delay slot, reloaded
 * for the call). The local matrix is passed as "(NJS_MATRIX_PTR)&mat" (the
 * address of the whole array): SHC then rebuilds R15+64 at every call like
 * the original, while the decayed "mat" (or "&mat[0]") is kept in R13.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"

/*
 * Look work of Big's look task: the shared lookwk layout (sa1/player.h
 * sa1_lookwk), except that 0x70..0x87 hold the saved eye node positions
 * (Float, FMOV.S here) instead of the saved eye angles, as for Tails
 * (TAILS_LOOKWK in code_8C0BEA0C.c).
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
} BIG_LOOKWK;                   /* 0x88 */

SA1_ASSERT_SIZE(BIG_LOOKWK, 0x88);
SA1_ASSERT_OFFSET(BIG_LOOKWK, eyeL_y, 0x18);
SA1_ASSERT_OFFSET(BIG_LOOKWK, eyeR_z, 0x24);
SA1_ASSERT_OFFSET(BIG_LOOKWK, eyeL, 0x5C);
SA1_ASSERT_OFFSET(BIG_LOOKWK, eyeL_pos, 0x70);
SA1_ASSERT_OFFSET(BIG_LOOKWK, eyeR_pos, 0x7C);

/*
 * plane: point = rest position (its first 3 floats, also the vector that is
 * rotated), fixed normal per eye; line: origin, rotated direction
 * (normalised). func_8C63FEAC is njDistanceL2PL-like (cross point of the
 * line and the plane into pos).
 */
void func_8C0E61A0(BIG_LOOKWK *lwp)
{
    NJS_MATRIX mat;
    NJS_LINE line;
    NJS_PLANE plane;
    NJS_POINT3 pos;

    njUnitMatrix((NJS_MATRIX_PTR)&mat);
    lwp->eyeL_z ? njRotateZ((NJS_MATRIX_PTR)&mat, lwp->eyeL_z) : (void)0;
    lwp->eyeL_y ? njRotateY((NJS_MATRIX_PTR)&mat, lwp->eyeL_y) : (void)0;
    plane.px = lwp->eyeL_pos[0];
    plane.py = lwp->eyeL_pos[1];
    plane.pz = lwp->eyeL_pos[2];
    plane.vx = 0.893131986f;
    plane.vy = -0.339210995f;
    plane.vz = 0.295383997f;
    line.px = 0.0f;
    line.py = 0.0f;
    line.pz = 0.0f;
    njCalcVector((NJS_MATRIX_PTR)&mat, (NJS_VECTOR *)&plane.px, (NJS_VECTOR *)&line.vx);
    njUnitVector((NJS_VECTOR *)&line.vx);
    func_8C63FEAC(&line, &plane, &pos);
    lwp->eyeL->pos[0] = pos.x;
    lwp->eyeL->pos[1] = pos.y;
    lwp->eyeL->pos[2] = pos.z;
    njUnitMatrix((NJS_MATRIX_PTR)&mat);
    lwp->eyeR_z ? njRotateZ((NJS_MATRIX_PTR)&mat, lwp->eyeR_z) : (void)0;
    lwp->eyeR_y ? njRotateY((NJS_MATRIX_PTR)&mat, lwp->eyeR_y) : (void)0;
    plane.px = lwp->eyeR_pos[0];
    plane.py = lwp->eyeR_pos[1];
    plane.pz = lwp->eyeR_pos[2];
    plane.vx = 0.893041983f;
    plane.vy = -0.339210995f;
    plane.vz = -0.295656003f;
    line.px = 0.0f;
    line.py = 0.0f;
    line.pz = 0.0f;
    njCalcVector((NJS_MATRIX_PTR)&mat, (NJS_VECTOR *)&plane.px, (NJS_VECTOR *)&line.vx);
    njUnitVector((NJS_VECTOR *)&line.vx);
    func_8C63FEAC(&line, &plane, &pos);
    lwp->eyeR->pos[0] = pos.x;
    lwp->eyeR->pos[1] = pos.y;
    lwp->eyeR->pos[2] = pos.z;
}

/* 0 while player 0 is in modes 32-37 or 40-45, else 1 */
Sint32 func_8C0E632A(task *tp)
{
    taskwk *twp;

    twp = playertwp[0];
    if ((twp->mode >= 32 && twp->mode <= 37) || (twp->mode >= 40 && twp->mode <= 45)) {
        return 0;
    }
    return 1;
}

/* Big: 1 in modes 35-37 and 43-45 */
Sint32 func_8C0E6352(taskwk *twp)
{
    if ((twp->mode >= 35 && twp->mode <= 37) || (twp->mode >= 43 && twp->mode <= 45)) {
        return 1;
    }
    return 0;
}
