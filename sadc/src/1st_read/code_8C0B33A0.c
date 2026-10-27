/*
 * 8C0B33A0-8C0B37F2 (first functions of chunk asm/1st_read/text/8C0B33A0.src).
 *
 * A particle emitter attached to a player. The emitter's taskwk keeps:
 *   scl.z        the player number (as a float)
 *   counter.ptr  a table of 0x48-byte emitter parameters (EMITPRM_8C0B3480)
 *   btimer       the index of the active entry (read as an unsigned byte)
 *   value.ptr    the head of a doubly linked list of MAlloc'd particles
 *                (PTCL_8C0B33A0, 0x34 bytes; created by func_8C0B37F2)
 *
 *   func_8C0B33A0  allocate a particle and push it at the list head
 *   func_8C0B33CE  unlink a particle and free it
 *   func_8C0B33EE  transform a point, a vector and an optional point from
 *                  the player's local space (collision centre, ang z/x/y)
 *   func_8C0B3480  emission point and direction for the active entry:
 *                  attach 0 = user0 weld (user0_pos/user0_vec),
 *                  attach 1 = user1 weld, otherwise the eyes height
 */

#include "common.h"
#include "task.h"
#include "sa1/ninja_types.h"
#include "sa1/ninja_funcs.h"
#include "sa1/engine.h"
#include "sa1/ccl.h"
#include "sa1/player.h"

extern NJS_MATRIX D_8C67C640;   /* unit matrix */

/* particle, allocated by func_8C0B33A0 with MAlloc(sizeof) (0x34 on the DC).
 * Offsets are the DC layout; the struct only lives on the heap and never
 * overlays game data, so the native layout is fine on 64-bit hosts. */
typedef struct PTCL_8C0B33A0 {
    Float f00;                  /* 0x00 set from pwp->shadow.y_top by func_8C0B37F2 */
    NJS_POINT3 pos;             /* 0x04 */
    NJS_VECTOR spd;             /* 0x10 */
    Sint16 w1C;                 /* 0x1C */
    Sint16 w1E;                 /* 0x1E */
    Float f20;                  /* 0x20 */
    Float f24;                  /* 0x24 */
    Float f28;                  /* 0x28 */
    struct PTCL_8C0B33A0 *prev; /* 0x2C */
    struct PTCL_8C0B33A0 *next; /* 0x30 */
} PTCL_8C0B33A0;                /* 0x34 */

/* emitter parameter entry (table at twp->counter.ptr, index twp->btimer) */
typedef struct {
    Sint32 unk00;               /* 0x00 (not seen) */
    NJS_POINT3 ofs;             /* 0x04 offset from the attach point */
    Float dist;                 /* 0x10 distance along the attach direction */
    Float spd;                  /* 0x14 speed along the attach direction */
    Sint32 attach;              /* 0x18 0: user0 weld, 1: user1 weld, else eyes */
    Float f1C;                  /* 0x1C func_8C0B37F2 */
    Float f20;                  /* 0x20 func_8C0B37F2 */
    Float f24;                  /* 0x24 func_8C0B37F2 */
    Float f28;                  /* 0x28 (not seen) */
    Float f2C;                  /* 0x2C (not seen) */
    Float f30;                  /* 0x30 func_8C0B37F2 */
    Float rate;                 /* 0x34 emission probability (func_8C0B3B1A) */
    Float f38;                  /* 0x38 func_8C0B37F2 */
    Float f3C;                  /* 0x3C (not seen) */
    Float f40;                  /* 0x40 (not seen) */
    Float f44;                  /* 0x44 (not seen) */
} EMITPRM_8C0B3480;             /* 0x48 */

SA1_ASSERT_SIZE(PTCL_8C0B33A0, 0x34);
SA1_ASSERT_SIZE(EMITPRM_8C0B3480, 0x48);

#define EMITPRM(twp) (((EMITPRM_8C0B3480 *)(twp)->counter.ptr)[(Uint8)(twp)->btimer])

/* o + d * v + p: the original fuses o + d * v (FMAC) and then adds p (FADD).
 * SHC only matches with the o-first form, which SA1_FMAC cannot express. */
#if defined(TARGET_PC)
#define EMIT_POS(o, d, v, p) (SA1_FMAC((d), (v), (o)) + (p))
#else
#define EMIT_POS(o, d, v, p) ((o) + (d) * (v) + (p))
#endif

PTCL_8C0B33A0 *func_8C0B33A0(taskwk *twp)
{
    PTCL_8C0B33A0 *pp;

    pp = (PTCL_8C0B33A0 *)MAlloc(sizeof(PTCL_8C0B33A0));
    if (pp) {
        pp->prev = NULL;
        pp->next = (PTCL_8C0B33A0 *)twp->value.ptr;
        if (twp->value.ptr) {
            ((PTCL_8C0B33A0 *)twp->value.ptr)->prev = pp;
        }
        twp->value.ptr = pp;
    }
    return pp;
}

void func_8C0B33CE(taskwk *twp, PTCL_8C0B33A0 *pp)
{
    PTCL_8C0B33A0 *prev;
    PTCL_8C0B33A0 *next;

    prev = pp->prev;
    next = pp->next;
    if (prev) {
        prev->next = next;
    } else {
        twp->value.ptr = next;
    }
    if (next) {
        next->prev = prev;
    }
    MFree(pp);
}

void func_8C0B33EE(Sint32 pno, NJS_POINT3 *pos, NJS_VECTOR *vec, NJS_POINT3 *org)
{
    taskwk *twp;
    CCL_INFO *info;

    twp = playertwp[pno];
    info = CWP(twp)->info;
    njPushMatrix(D_8C67C640);
    njTranslateV(NULL, &info->center);
    /* ternary form: SHC keeps each angle in a spilled temp (shares the
     * stack slot of info) like the original */
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    njCalcPoint(NULL, pos, pos);
    njCalcVector(NULL, vec, vec);
    if (org) {
        njCalcPoint(NULL, org, org);
    }
    njPopMatrix(1);
}

void func_8C0B3480(taskwk *twp, NJS_POINT3 *pos, NJS_VECTOR *vec, NJS_POINT3 *org)
{
    playerwk *pwp;
    Float *p;

    /* the float player number is passed without EXTU.B in the original */
    pwp = GetPlayerWorkPtr(SA1_FTOI(twp->scl.z));
    switch (EMITPRM(twp).attach) {
    case 0:
        *vec = pwp->user0_vec;
        /* walking pointer: the original scales x, y, z through one register */
        p = &vec->x;
        *p++ *= EMITPRM(twp).spd;
        *p++ *= EMITPRM(twp).spd;
        *p *= EMITPRM(twp).spd;
        pos->x = EMIT_POS(EMITPRM(twp).ofs.x, EMITPRM(twp).dist, pwp->user0_vec.x, pwp->user0_pos.x);
        pos->y = EMIT_POS(EMITPRM(twp).ofs.y, EMITPRM(twp).dist, pwp->user0_vec.y, pwp->user0_pos.y);
        pos->z = EMIT_POS(EMITPRM(twp).ofs.z, EMITPRM(twp).dist, pwp->user0_vec.z, pwp->user0_pos.z);
        if (org) {
            *org = pwp->user0_pos;
        }
        break;
    case 1:
        *vec = pwp->user1_vec;
        p = &vec->x;
        *p++ *= EMITPRM(twp).spd;
        *p++ *= EMITPRM(twp).spd;
        *p *= EMITPRM(twp).spd;
        pos->x = EMIT_POS(EMITPRM(twp).ofs.x, EMITPRM(twp).dist, pwp->user1_vec.x, pwp->user1_pos.x);
        pos->y = EMIT_POS(EMITPRM(twp).ofs.y, EMITPRM(twp).dist, pwp->user1_vec.y, pwp->user1_pos.y);
        pos->z = EMIT_POS(EMITPRM(twp).ofs.z, EMITPRM(twp).dist, pwp->user1_vec.z, pwp->user1_pos.z);
        if (org) {
            *org = pwp->user1_pos;
        }
        break;
    default:
        *pos = EMITPRM(twp).ofs;
        pos->x += EMITPRM(twp).dist;
        pos->y += pwp->p.eyes_height;
        vec->x = EMITPRM(twp).spd;
        vec->y = 0.0f;
        vec->z = 0.0f;
        if (org) {
            org->x = 0.0f;
            org->y = pos->y;
            org->z = 0.0f;
        }
        break;
    }
    func_8C0B33EE((Sint8)SA1_FTOI(twp->scl.z), pos, vec, org);
}
