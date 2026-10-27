/*
 * 1ST_READ 8C0E6812-8C0E6DA0 (Big the Cat, fishing: the cast cursor task
 * created by func_8C0E6620 with CreateElementalTask(11, 2, func_8C0E6CC0)):
 *
 *  func_8C0E6812  empty destructor of the cursor task.
 *  func_8C0E6880  moves the cursor with player 0's stick (3 units per frame
 *                 along the camera-relative stick angle, swept against the
 *                 collision with func_8C0569E0, radius 1, which also returns
 *                 the angle of the surface it hit), probes the ground under
 *                 the new position (func_8C0E9D24) and accepts the move only
 *                 when the target is valid (func_8C0E6B64) and the sweep did
 *                 not hit a wall (func_8C0E69CA); the cursor then floats 20
 *                 above the ground, at most 49.9 above player 0. Always
 *                 stores the horizontal distance to player 0.
 *  func_8C0E69CA  1 when the surface normal (up rotated by ang) has y <=
 *                 0.01 (wall or ceiling) and, when tp is given, it also
 *                 faces along the direction player 0 -> tp (dot > 0.7).
 *  func_8C0E6AEE  1 when the surface normal is horizontal within 0.01 (a
 *                 vertical wall); tp is unused.
 *  func_8C0E6B64  0 when (x, y, z) is a valid cast target: horizontal
 *                 distance to player 0 in 18..250 (350 with the power rod,
 *                 equipment 0x800), direction within about 60 degrees of the
 *                 cast angle, ground tilt within +-90 degrees, and height
 *                 within +-50 of player 0; else 1.
 *  func_8C0E6CC0  cursor exec: mode 0 initialises (dest, disp, ground
 *                 height, rod tip node angle); mode 1 publishes the ground
 *                 height (D_8C565F6C), spins, moves (func_8C0E6880), draws,
 *                 turns player 0 towards the cursor and frees the task when
 *                 player 0 leaves the fishing modes (func_8C0E632A).
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * Any work of the cursor task (task.awp, 0x10 bytes from
 * CreateElementalTask im & 8). No pointers: same layout on every host.
 */
typedef struct {
    Float dist;                 /* 0x00 horizontal distance to player 0 (func_8C0E6620 sets 20.0f) */
    Float height;               /* 0x04 ground height under the cursor */
    Sint16 angy;                /* 0x08 cast angle (func_8C0E6620: creator's twp->ang.y) */
    Sint16 unk0A;               /* 0x0A */
    Sint16 angx;                /* 0x0C ground tilt */
    Sint16 angz;                /* 0x0E */
} BIGCASTWK;                    /* 0x10 */

SA1_ASSERT_SIZE(BIGCASTWK, 0x10);
SA1_ASSERT_OFFSET(BIGCASTWK, angy, 0x08);
SA1_ASSERT_OFFSET(BIGCASTWK, angx, 0x0C);

/*
 * Vertical probe result of func_8C0E9D24 (0x30 bytes on the caller's
 * stack; not the 0x20-byte xssunit of sa1/collision.h). No pointers.
 */
typedef struct {
    Uint32 attr;                /* 0x00 */
    Angle angx;                 /* 0x04 */
    Angle angz;                 /* 0x08 */
    Float onpos;                /* 0x0C height of the hit */
} BIGPROBE_UNIT;                /* 0x10 */

typedef struct {
    BIGPROBE_UNIT upper;        /* 0x00 lowest hit above y (init 1000000) */
    BIGPROBE_UNIT lower;        /* 0x10 highest hit at or below y (init -1000000) */
    BIGPROBE_UNIT nearest;      /* 0x20 hit closest to y */
} BIGPROBE;                     /* 0x30 */

SA1_ASSERT_SIZE(BIGPROBE_UNIT, 0x10);
SA1_ASSERT_SIZE(BIGPROBE, 0x30);
SA1_ASSERT_OFFSET(BIGPROBE, lower, 0x10);

extern NJS_OBJECT *D_8C565FC4[];    /* rod objects by number of fishing upgrades */
extern Float D_8C565F6C;            /* ground height under the cursor */

Bool func_8C0569E0(NJS_POINT3 *pos, NJS_POINT3 *v, Angle3 *ang, Float r);   /* sphere sweep (code_8C05697C.c) */
Float func_8C0E9D24(Float x, Float y, Float z, Sint32 flags, BIGPROBE *pr); /* returns pr->lower.onpos */
void func_8C0EAB68(task *tp);
Sint32 func_8C0E632A(task *tp);     /* code_8C0E61A0.c defines it (void); the call passes tp */

void func_8C0E6880(task *tp);
Sint32 func_8C0E69CA(task *tp, Angle3 *ang);
Sint32 func_8C0E6B64(task *tp, BIGPROBE *pr, Float x, Float y, Float z);
Sint32 func_8C0E6E80(void);

ASM_STUB(void func_8C0E66C4(task *tp))

void func_8C0E6812(task *tp)
{
}

void func_8C0E6880(task *tp)
{
    Angle3 ang;
    NJS_POINT3 pos;
    NJS_VECTOR v;
    BIGPROBE pr;
    taskwk *twp;
    taskwk *ptwp;
    BIGCASTWK *cwp;
    Angle a;
    Float stroke;
    Float x;
    Float y;
    Float z;
    Float h;

    twp = tp->twp;
    ptwp = playertwp[0];
    cwp = (BIGCASTWK *)tp->awp;
    a = B_8C754DCC[(Uint8)twp->counter.b[0]].ang;
    stroke = B_8C754DCC[(Uint8)twp->counter.b[0]].stroke;
    ang.x = ang.y = ang.z = 0;
    pos = twp->pos;
    if (stroke == 0.0f) {
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = 0.0f;
    } else {
        v.x = njCos(a) * 3.0f;
        v.y = 0.0f;
        v.z = njSin(a) * 3.0f;
        func_8C0569E0(&pos, &v, &ang, 1.0f);
    }
    x = twp->pos.x + v.x;
    y = twp->pos.y + v.y;
    z = twp->pos.z + v.z;
    func_8C0E9D24(x, y, z, 4, &pr);
    h = pr.lower.onpos;
    if (func_8C0E6B64(tp, &pr, x, h, z) == 0 && func_8C0E69CA(NULL, &ang) == 0) {
        twp->pos.x = x;
        twp->pos.z = z;
        cwp->height = h;
        cwp->angx = pr.lower.angx;
        cwp->angz = pr.lower.angz;
        h += 20.0f;
        if (h >= ptwp->pos.y + 49.899999f) {
            twp->pos.y = ptwp->pos.y + 49.899999f;
        } else {
            twp->pos.y = h;
        }
    }
    v.x = twp->pos.x - ptwp->pos.x;
    v.z = twp->pos.z - ptwp->pos.z;
    cwp->dist = njScalor(&v);
}

Sint32 func_8C0E69CA(task *tp, Angle3 *ang)
{
    NJS_VECTOR up;
    NJS_VECTOR v;
    NJS_MATRIX m;
    Float d;

    up.x = up.z = v.x = v.z = 0.0f;
    up.y = v.y = 1.0f;
    njUnitMatrix((NJS_MATRIX_PTR)&m);
    njRotateXYZ((NJS_MATRIX_PTR)&m, ang->x, ang->y, ang->z);
    njCalcVector((NJS_MATRIX_PTR)&m, &up, &up);
    d = njInnerProduct(&up, &v);
    if (d <= 0.01f) {
        if (tp == NULL) {
            return 1;
        }
        v.x = tp->twp->pos.x - playertwp[0]->pos.x;
        v.y = 0.0f;
        v.z = tp->twp->pos.z - playertwp[0]->pos.z;
        njUnitVector(&v);
        d = njInnerProduct(&up, &v);
        if (d > 0.7f) {
            return 1;
        }
    }
    return 0;
}

Sint32 func_8C0E6AEE(task *tp, Angle3 *ang)
{
    NJS_VECTOR up;
    NJS_VECTOR v;
    NJS_MATRIX m;
    Float d;

    up.x = up.z = v.x = v.z = 0.0f;
    up.y = v.y = 1.0f;
    njUnitMatrix((NJS_MATRIX_PTR)&m);
    njRotateXYZ((NJS_MATRIX_PTR)&m, ang->x, ang->y, ang->z);
    njCalcVector((NJS_MATRIX_PTR)&m, &up, &up);
    d = njInnerProduct(&up, &v);
    if (d > 0.01f || d < -0.01f) {
        return 0;
    }
    return 1;
}

/*
 * NON_MATCHING: complete and functionally faithful; the only difference is a
 * register swap: SHC keeps pr in R13 and the constant 0x4000 in R12, the
 * original the other way round (same instructions otherwise). Statement
 * order, if/|| structure, locals, casts, struct views and register hints
 * did not change the allocation.
 */
#ifdef NON_MATCHING
Sint32 func_8C0E6B64(task *tp, BIGPROBE *pr, Float x, Float y, Float z)
{
    NJS_VECTOR v;
    playerwk *pwp;
    taskwk *ptwp;
    Float d;
    Float lim;
    Sint32 diff;
    Float dy;

    ptwp = playertwp[0];
    pwp = playermwp[0]->work.pwp;
    v.x = x - ptwp->pos.x;
    v.y = 0.0f;
    v.z = z - ptwp->pos.z;
    d = njScalor(&v);
    if (pwp->equipment & 0x800) {
        lim = 350.0f;
    } else {
        lim = 250.0f;
    }
    if (d > lim || d < 18.0f) {
        return 1;
    }
    diff = ((0x4000 - SA1_FTOI(atan2f(v.x, v.z) * 10430.381f)) & 0xFFFF) - ((BIGCASTWK *)tp->awp)->angy;
    if (diff > 0x2B00 && diff < 0xD500) {
        return 1;
    }
    if (diff < -0x2B00 && diff > -0xD500) {
        return 1;
    }
    if (pr->lower.angx > 0x4000 || pr->lower.angx < -0x4000
        || pr->lower.angz > 0x4000 || pr->lower.angz < -0x4000) {
        return 1;
    }
    dy = y - ptwp->pos.y;
    if (dy <= -50.0f || dy >= 50.0f) {
        return 1;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0E6B64(task *tp, BIGPROBE *pr, Float x, Float y, Float z))
#endif

void func_8C0E6CC0(task *tp)
{
    taskwk *ptwp;
    BIGCASTWK *cwp;
    taskwk *twp;
    NJS_OBJECT *obj;

    twp = tp->twp;
    cwp = (BIGCASTWK *)tp->awp;
    ptwp = playertwp[0];
    obj = D_8C565FC4[func_8C0E6E80()]->child;
    switch (twp->mode) {
    case 0:
        twp->mode = 1;
        tp->dest = func_8C0E6812;
        tp->disp = func_8C0E66C4;
        cwp->height = twp->pos.y;
        cwp->angz = 0;
        cwp->angx = 0;
        obj->ang[0] = 0x1000;
        obj->evalflags &= ~NJD_EVAL_UNIT_ANG;
        break;
    case 1:
        D_8C565F6C = cwp->height;
        twp->ang.y += 0x200;
        func_8C0EAB68(tp);
        func_8C0E6880(tp);
        func_8C0E66C4(tp);
        playermwp[0]->ang_aim.y = playertwp[0]->ang.y
            = 0x4000 - SA1_FTOI(atan2f(twp->pos.x - ptwp->pos.x, twp->pos.z - ptwp->pos.z) * 10430.381f);
        if (func_8C0E632A(tp)) {
            FreeTask(tp);
        }
        break;
    }
}

ASM_STUB(Sint32 func_8C0E6E80(void))
