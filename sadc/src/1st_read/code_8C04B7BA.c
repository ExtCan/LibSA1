/*
 * Event work (taskwk.ewp, eventwk in sa1/engine.h): path movement queue and
 * face script reader. Chunk asm/1st_read/text/8C04B440.src, functions
 * 8C04B7BA-8C04BC20.
 *  func_8C04B7BA  append a path to the path queue (ewp->list4C); task
 *                 wrapper 8C04A1AC, called by the event overlays
 *  func_8C04B82E  clear the path queue
 *  func_8C04B8A0  turn an (x, y, z) BAMS angle triple into the z/x/y
 *                 rotation order used by the event display
 *  func_8C04B9E0  move the task along the current path (character mains)
 *  func_8C04BBCE  set the face script (wrapper 8C04B39E)
 *  func_8C04BC20  read the next face script character
 */
#include "common.h"
#include "ninja.h"
/* include/sa1/ninja_funcs.h declares 8C63A980 as Float njRandom(void), but
 * it takes a Float in FR4 and returns modff(n, &static) (8C10EB50 calls
 * modff 8C10F164 and stores the integer part through R4): the fractional
 * part of n (njFraction-like). Until the header is fixed its prototype is
 * renamed out of the way here. */
#define njRandom njRandom_header_proto
#include "sa1/sa1.h"
#undef njRandom

#if defined(TARGET_PC)
Float njFraction(Float n);      /* libsa1 runtime: n - (Float)(Sint32)n (modff fraction) */
#else
Float njRandom(Float n);        /* 8C63A980, see above */
#define njFraction njRandom
#endif

/* Path point (game data, 16 bytes): BAMS angles in 1/256 of a turn and a
 * position relative to ewp->v50 */
typedef struct {
    Sint8 ang[3];               /* DC 0x00 x, y, z; value * 256 = Angle */
    Sint8 pad03;
    NJS_POINT3 pos;             /* DC 0x04 */
} EV_PATH_POINT;                /* DC 0x10 */

/* Path (game data in the event overlays, e.g. D_0CB866A4 in ev0029 =
 * {0x60, D_0CB860A4}); holds a pointer: ripped into this native layout on PC */
typedef struct {
    Sint32 num;                 /* DC 0x00 number of points */
    EV_PATH_POINT *points;      /* DC 0x04 */
} EV_PATH;                      /* DC 0x08 */

/* Path queue entry (CAlloc(1, 0x10)); eventwk.list4C is the queue head,
 * eventwk.f48 the current frame (point index + fraction) */
typedef struct {
    Uint8 mode;                 /* DC 0x00 bit 0: loop when it is the last entry;
                                 * bit 1: path relative to the task position */
    Uint8 pad01[3];
    EV_PATH *path;              /* DC 0x04 */
    Float speed;                /* DC 0x08 points per step */
    void *next;                 /* DC 0x0C EV_PATH_LIST * (eventwk.list4C is void *) */
} EV_PATH_LIST;                 /* DC 0x10, CAlloc'd: sizeof on PC */

SA1_ASSERT_SIZE(EV_PATH_POINT, 0x10);
SA1_ASSERT_OFFSET(EV_PATH_POINT, pos, 0x04);
SA1_ASSERT_SIZE(EV_PATH, 0x08);
SA1_ASSERT_SIZE(EV_PATH_LIST, 0x10);
SA1_ASSERT_OFFSET(EV_PATH_LIST, path, 0x04);
SA1_ASSERT_OFFSET(EV_PATH_LIST, speed, 0x08);
SA1_ASSERT_OFFSET(EV_PATH_LIST, next, 0x0C);

/*
 * Face block of eventwk (0x5C-0x6B). The game reaches it through a pointer
 * to ewp + 0x5C (8C04BBCE, 8C04BC20 and 8C04BC70-8C04BD60 all use "ADD #92"
 * + small displacements), i.e. a nested struct in the original eventwk.
 * Until include/sa1/engine.h has it, SHC uses this view; the PC build uses
 * the flat eventwk fields of the same names (native layout).
 */
#if defined(TARGET_PC)
typedef eventwk facewk;
#define FACEWK(ewp) (ewp)
#else
typedef struct facewk {
    Uint8 face;                 /* 0x00 = eventwk 0x5C */
    Uint8 face_next;            /* 0x01 */
    Sint16 face_time;           /* 0x02 */
    Sint16 face_count;          /* 0x04 */
    Uint8 pad06[2];
    const char *face_script;    /* 0x08 = eventwk 0x64 */
    const char *face_seq;       /* 0x0C = eventwk 0x68 */
} facewk;
#define FACEWK(ewp) ((facewk *)&(ewp)->face)
#endif

/* c + a * b where SHC emitted FMAC (fused on PC); SA1_FMAC's a * b + c
 * operand order changes SHC's code here */
#if defined(TARGET_PC)
#define FMAC_ADD(c, a, b) SA1_FMAC((a), (b), (c))
#else
#define FMAC_ADD(c, a, b) ((c) + (a) * (b))
#endif

extern const NJS_VECTOR D_8C112AFC;     /* {0, 1, 0} */
extern const NJS_VECTOR D_8C112B08;     /* {1, 0, 0} */

void func_8C04B8A0(Angle3 *ang);

/* queue path with speed (points per step); the first entry sets the base
 * position ewp->v50 (task position for mode bit 1, else the origin) */
void func_8C04B7BA(taskwk *twp, EV_PATH *path, Float speed, Sint32 mode)
{
    eventwk *ewp;
    void **pp;
    EV_PATH_LIST *node;

    ewp = twp->ewp;
    pp = &ewp->list4C;
    if (*pp != NULL) {
        do {
            pp = &((EV_PATH_LIST *)*pp)->next;
        } while (*pp != NULL);
    } else if (mode & 2) {
        ewp->v50 = twp->pos;
    } else {
        ewp->v50.x = ewp->v50.y = ewp->v50.z = 0.0f;
    }
    node = CAlloc(1, sizeof(EV_PATH_LIST));
    *pp = node;
    node->mode = mode;
    node->path = path;
    node->speed = speed;
}

void func_8C04B82E(taskwk *twp)
{
    EV_PATH_LIST *p;
    EV_PATH_LIST *next;

    p = ((eventwk *)twp->ewp)->list4C;
    while (p != NULL) {
        next = p->next;
        MFree(p);
        p = next;
    }
    ((eventwk *)twp->ewp)->list4C = NULL;
    ((eventwk *)twp->ewp)->f48 = 0.0f;
}

/*
 * Re-express the rotation ang (njRotateXYZ order) as z, x, y angles: z and x
 * from the rotated Y axis, y from the X axis after undoing z and x.
 * The rotations by the new z and x are skipped for a zero angle; the angle
 * goes through an unsigned local, which SHC keeps in a stack slot, and only
 * the ?: form (not an if statement) gives the original register choice.
 */
void func_8C04B8A0(Angle3 *ang)
{
    NJS_MATRIX m;
    NJS_VECTOR vy;
    NJS_VECTOR vx;
    NJS_VECTOR v;
    Uint32 a;

    vy = D_8C112AFC;
    vx = D_8C112B08;
    njUnitMatrix((NJS_MATRIX_PTR)&m);
    njRotateXYZ((NJS_MATRIX_PTR)&m, ang->x, ang->y, ang->z);
    njCalcVector((NJS_MATRIX_PTR)&m, &vy, &v);
    ang->z = SA1_FTOI(atan2f(-v.x, v.y) * 10430.381f);
    ang->x = SA1_FTOI(atan2f(v.z, njSqrt(SA1_FMAC(v.y, v.y, v.x * v.x))) * 10430.381f);
    njInvertMatrix((NJS_MATRIX_PTR)&m);
    (a = ang->z) != 0 ? njRotateZ((NJS_MATRIX_PTR)&m, (Angle)a) : (void)0;
    (a = ang->x) != 0 ? njRotateX((NJS_MATRIX_PTR)&m, (Angle)a) : (void)0;
    njCalcVector((NJS_MATRIX_PTR)&m, &vx, &v);
    ang->y = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f);
}

/*
 * Per-frame path step (character mains, event-work test task 8C04BD9E):
 * interpolate position (relative to ewp->v50) and angles between point
 * floor(frame) and the next one (the first one after the last), convert the
 * angles with 8C04B8A0, mirror y (0x8000 - y; also the player's ang_aim.y),
 * optionally snap to the ground (ewp->mode bit 0: height, bit 1: slope
 * angles), then advance frame by the entry speed. At the end of the path a
 * looping entry (mode bit 0) without a successor wraps; otherwise the next
 * entry starts at frame 0 (relative to the current position if its mode bit 1
 * is set). "register" on p1 and frame reproduces SHC's register choice.
 */
void func_8C04B9E0(taskwk *twp)
{
    Angle3 shang;
    eventwk *ewp;
    EV_PATH_LIST *node;
    EV_PATH *path;
    EV_PATH_POINT *p0;
    register EV_PATH_POINT *p1;
    Sint32 i;
    register Float frame;
    Float num;
    Float t;

    ewp = twp->ewp;
    if (ewp == NULL) {
        return;
    }
    node = ewp->list4C;
    if (node == NULL) {
        return;
    }
    path = node->path;
    num = (Float)path->num;
    frame = ewp->f48;
    i = SA1_FTOI(func_8C10D198(frame));
    t = njFraction(frame);
    p0 = &path->points[i];
    p1 = path->points;
    if (i + 1 < path->num) {
        p1 = p0 + 1;
    }
    twp->pos.x = FMAC_ADD(ewp->v50.x + p0->pos.x, p1->pos.x - p0->pos.x, t);
    twp->pos.y = FMAC_ADD(ewp->v50.y + p0->pos.y, p1->pos.y - p0->pos.y, t);
    twp->pos.z = FMAC_ADD(ewp->v50.z + p0->pos.z, p1->pos.z - p0->pos.z, t);
    twp->ang.x = SA1_FTOI(SA1_FMAC(FMAC_ADD(p0->ang[0], (Sint8)(p1->ang[0] - p0->ang[0]), t), 256.0f, 0.5f));
    twp->ang.y = SA1_FTOI(SA1_FMAC(FMAC_ADD(p0->ang[1], (Sint8)(p1->ang[1] - p0->ang[1]), t), 256.0f, 0.5f));
    twp->ang.z = SA1_FTOI(SA1_FMAC(FMAC_ADD(p0->ang[2], (Sint8)(p1->ang[2] - p0->ang[2]), t), 256.0f, 0.5f));
    func_8C04B8A0(&twp->ang);
    twp->ang.y = 0x8000 - twp->ang.y;
    if ((Uint8)twp->id == 1) {
        playermwp[(Uint8)twp->counter.b[0]]->ang_aim.y = twp->ang.y;
    }
    if (((Uint8)ewp->mode & 1) || ((Uint8)ewp->mode & 2)) {
        Float y;

        y = func_8C091C20(twp->pos.x, twp->pos.y, twp->pos.z, &shang);
        if ((Uint8)ewp->mode & 1) {
            twp->pos.y = y;
        }
        if ((Uint8)ewp->mode & 2) {
            twp->ang.x = shang.x;
            twp->ang.z = shang.z;
        }
    }
    frame += node->speed;
    if (node->mode & 1) {
        if (frame >= num) {
            if (node->next != NULL) {
                goto next;
            }
            frame -= num;
        }
    } else if (frame >= num - 1.0f) {
next:
        ewp->list4C = node->next;
        MFree(node);
        frame = 0.0f;
        if (node->mode & 2) {
            ewp->v50 = twp->pos;
        } else {
            ewp->v50.x = ewp->v50.y = ewp->v50.z = 0.0f;
        }
    }
    ewp->f48 = frame;
}

/* set the face script; starts the face sequence if none is running */
void func_8C04BBCE(taskwk *twp, const char *script)
{
    facewk *fwp;

    fwp = FACEWK((eventwk *)twp->ewp);
    fwp->face_script = script;
    if (fwp->face_time == 0) {
        fwp->face_count = 0;
        fwp->face_time = 1;
        fwp->face = 0;
        fwp->face_next = 0;
    }
}

/* next face script character: 'A'-'Z' -> 0-25, 'a'-'z' -> 26-51, '0' -> -1,
 * anything else as is; 0 at the end of the script (which is then dropped) */
Sint32 func_8C04BC20(taskwk *twp)
{
    facewk *fwp;
    Sint32 c;

    fwp = FACEWK((eventwk *)twp->ewp);
    if (fwp->face_script != NULL) {
        c = *fwp->face_script++;
        if (c != 0) {
            if (c >= 'A' && c <= 'Z') {
                return c - 'A';
            } else if (c >= 'a' && c <= 'z') {
                return c - 'a' + 26;
            } else if (c == '0') {
                return -1;
            }
            goto end;
        }
        fwp->face_script = NULL;
    }
    return 0;
end:
    return c;
}
