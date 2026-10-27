/*
 * 1ST_READ 8C0EC212-8C0EC6C8 (chunk 8C0EC0A8; Big the Cat, fishing line):
 *
 *  func_8C0EC212  spring force on line point i (called by func_8C0EC0A8 for
 *                 i = 1..10 with len = {min, max}): for each neighbour
 *                 (i-1, then i+1) whose squared distance exceeds max^2 it adds
 *                 0.24 * (neighbour - pt[i]) to acc[i], else if it exceeds
 *                 min^2 it adds 0.08 * (neighbour - pt[i]); then gravity,
 *                 acc[i].y -= 0.05.
 *  func_8C0EC4E8  integrates point i: spd += acc; spd -= spd * k (k = 0.2
 *                 when bit i of flag is set, else 0.1); pt += spd; acc = 0;
 *                 the point is kept 2 units above the ground below it
 *                 (func_8C091C20, GetShadowPos-like).
 *  func_8C0EC60C  vertex alpha of point i for the line display func_8C0EC180:
 *                 255 - |pt[i] - camera|^2 / 4, clamped to [0x40, 0xE0];
 *                 0x80 when there is no camera task.
 *
 * Matching notes: the original accesses its local vectors through their
 * address ("(&d)->x", vector macros in the original source): written as
 * d.x, SHC keeps the frame address in a register instead of using
 * @(R0,R15). The zero stored into acc (func_8C0EC4E8) is a variable set
 * before the branch, which makes SHC load it early (FLDI0 in the BT/S delay
 * slot) and puts k in FR5.
 */
#include "common.h"
#include "sa1/sa1.h"

/* Fishing line work (any work block work[2] of the lure task, see
 * code_8C0E91E4.c; code_8C0EC180.c declares only pt[]). Size unknown
 * (>= 0x1B4). */
typedef struct {
    NJS_POINT3 pt[12];          /* 0x000 line points (pt[0], pt[11] set by func_8C0EC0A8) */
    NJS_VECTOR spd[12];         /* 0x090 point velocity */
    NJS_VECTOR acc[12];         /* 0x120 force accumulated by func_8C0EC212, cleared each step */
    Uint32 flag;                /* 0x1B0 bit i: stronger damping (0.2) for point i */
} FISHLINE;

SA1_ASSERT_OFFSET(FISHLINE, spd, 0x90);
SA1_ASSERT_OFFSET(FISHLINE, acc, 0x120);
SA1_ASSERT_OFFSET(FISHLINE, flag, 0x1B0);

#ifdef NON_MATCHING
/*
 * Functionally complete. Remaining difference (16 instructions): FP register
 * allocation in the two squared-length blocks. The original gives v.y FR5 and
 * the forwarded copy of v.z FR6 (second block: v.x FR8, v.z FR6) and loads
 * 0.08 (pool first) before 0.24; this version swaps those two registers and
 * loads 0.24 first. Literal constants, both factors as variables, their
 * placement and the order of the squares were tried (see process notes).
 */
void func_8C0EC212(FISHLINE *lw, NJS_POINT2 *len, Sint32 i)
{
    NJS_VECTOR v;
    NJS_VECTOR d;
    NJS_POINT3 *p;
    NJS_VECTOR *a;
    Float l2;
    Float k1;

    p = lw->pt;
    a = lw->acc;
    (&v)->x = p[i].x - p[i - 1].x;
    (&v)->y = p[i].y - p[i - 1].y;
    (&v)->z = p[i].z - p[i - 1].z;
    k1 = 0.24f;
    l2 = SH_ONLY((&v)->x * (&v)->x + (&v)->y * (&v)->y + (&v)->z * (&v)->z)
         PC_ONLY(SA1_FMAC((&v)->z, (&v)->z, SA1_FMAC((&v)->x, (&v)->x, (&v)->y * (&v)->y)));
    if (l2 > len->y * len->y) {
        (&d)->x = p[i - 1].x - p[i].x;
        (&d)->y = p[i - 1].y - p[i].y;
        (&d)->z = p[i - 1].z - p[i].z;
        (&d)->x *= k1;
        (&d)->y *= k1;
        (&d)->z *= k1;
        a[i].x += (&d)->x;
        a[i].y += (&d)->y;
        a[i].z += (&d)->z;
    } else if (l2 > len->x * len->x) {
        (&d)->x = p[i - 1].x - p[i].x;
        (&d)->y = p[i - 1].y - p[i].y;
        (&d)->z = p[i - 1].z - p[i].z;
        (&d)->x *= 0.08f;
        (&d)->y *= 0.08f;
        (&d)->z *= 0.08f;
        a[i].x += (&d)->x;
        a[i].y += (&d)->y;
        a[i].z += (&d)->z;
    }
    (&v)->x = p[i].x - p[i + 1].x;
    (&v)->y = p[i].y - p[i + 1].y;
    (&v)->z = p[i].z - p[i + 1].z;
    l2 = SH_ONLY((&v)->x * (&v)->x + (&v)->y * (&v)->y + (&v)->z * (&v)->z)
         PC_ONLY(SA1_FMAC((&v)->z, (&v)->z, SA1_FMAC((&v)->x, (&v)->x, (&v)->y * (&v)->y)));
    if (l2 > len->y * len->y) {
        (&d)->x = p[i + 1].x - p[i].x;
        (&d)->y = p[i + 1].y - p[i].y;
        (&d)->z = p[i + 1].z - p[i].z;
        (&d)->x *= k1;
        (&d)->y *= k1;
        (&d)->z *= k1;
        a[i].x += (&d)->x;
        a[i].y += (&d)->y;
        a[i].z += (&d)->z;
    } else if (l2 > len->x * len->x) {
        (&d)->x = p[i + 1].x - p[i].x;
        (&d)->y = p[i + 1].y - p[i].y;
        (&d)->z = p[i + 1].z - p[i].z;
        (&d)->x *= 0.08f;
        (&d)->y *= 0.08f;
        (&d)->z *= 0.08f;
        a[i].x += (&d)->x;
        a[i].y += (&d)->y;
        a[i].z += (&d)->z;
    }
    a[i].y += -0.049999998f;
}
#else
ASM_STUB(void func_8C0EC212(FISHLINE *lw, NJS_POINT2 *len, Sint32 i))
#endif

void func_8C0EC4E8(FISHLINE *lw, Sint32 i)
{
    Angle3 ang;
    NJS_VECTOR d;
    NJS_POINT3 *p;
    NJS_VECTOR *v;
    NJS_VECTOR *a;
    Uint32 bit;
    Float k;
    Float h;
    Float zero;

    p = &lw->pt[i];
    v = &lw->spd[i];
    a = &lw->acc[i];
    bit = 1 << i;
    zero = 0.0f;
    if (lw->flag & bit) {
        v->x += a->x;
        v->y += a->y;
        v->z += a->z;
        k = 0.19999999f;
    } else {
        v->x += a->x;
        v->y += a->y;
        v->z += a->z;
        k = 0.099999996f;
    }
    (&d)->x = v->x * k;
    (&d)->y = v->y * k;
    (&d)->z = v->z * k;
    v->x -= (&d)->x;
    v->y -= (&d)->y;
    v->z -= (&d)->z;
    p->x += v->x;
    p->y += v->y;
    p->z += v->z;
    a->x = zero;
    a->y = zero;
    a->z = zero;
    h = func_8C091C20(p->x, p->y + 2.0f, p->z, &ang);
    if (!(p->y > h)) {
        p->y = h + 2.0f;
    }
}

Sint32 func_8C0EC60C(FISHLINE *lw, Sint32 i)
{
    NJS_VECTOR v;
    NJS_POINT3 *p;
    Float l2;
    Sint32 alpha;

    p = lw->pt;
    if (D_8C18B7EC) {
        (&v)->x = p[i].x - D_8C18B7EC->pos.x;
        (&v)->y = p[i].y - D_8C18B7EC->pos.y;
        (&v)->z = p[i].z - D_8C18B7EC->pos.z;
        l2 = SH_ONLY((&v)->x * (&v)->x + (&v)->y * (&v)->y + (&v)->z * (&v)->z)
             PC_ONLY(SA1_FMAC((&v)->z, (&v)->z, SA1_FMAC((&v)->x, (&v)->x, (&v)->y * (&v)->y)));
        alpha = SA1_FTOI(l2 * 0.25f);
        alpha = 0xFF - alpha;
        if (alpha > 0xE0) {
            alpha = 0xE0;
        }
        if (alpha < 0x40) {
            alpha = 0x40;
        }
        return alpha;
    }
    return 0x80;
}
