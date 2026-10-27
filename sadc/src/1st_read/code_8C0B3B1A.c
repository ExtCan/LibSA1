/*
 * 1ST_READ 8C0B3B1A-8C0B3FF4 (chunk asm/1st_read/text/8C0B33A0.src).
 *
 * Underwater air bubbles breathed out by a player, plus three small task
 * initialisers of the same chunk.
 *
 *   func_8C0B3B1A  exec of the bubble task (installed by func_8C0B3A2E):
 *                  spawns bubbles (func_8C0B37F2) at a random rate while the
 *                  player is in water, moves them (damping, buoyancy, random
 *                  sideways jitter), pushes them out of a sphere placed by
 *                  func_8C0B3480 (player weld points user0_pos/user0_vec)
 *                  and removes them at the water surface.
 *   func_8C0B3F2C  disp: draws every bubble as a 3D sprite.
 *   func_8C0B3FA6, func_8C0B3FC0, func_8C0B3FDA
 *                  execs of the helper tasks created by func_8C0B2D40,
 *                  func_8C0B2D66 and func_8C0B2DB2 (code_8C0B2D40.c): set up
 *                  the work, install exec/disp and draw once.
 *
 * Bubble task taskwk usage (CreateElementalTask, taskwk only):
 *   smode      1 = breathing out (bubble burst), 0 = idle
 *   btimer     (read as Uint8) index of the parameter set: 0 normal,
 *              1/2 while the player's smode is 39
 *   wtimer     burst timer: set to 40 when a burst starts, decremented
 *              while smode is set, incremented after every movement step
 *   counter    .ptr -> group of 3 BUBBLE_PARAM in D_8C1C63A4 (one group per
 *              character, chosen by func_8C0B3A2E)
 *   value      .ptr -> first BUBBLE of the list
 *   scl.z      player number, kept as a float
 */

#include "sa1/sa1.h"

/* bubble parameter set (table D_8C1C63A4, groups of 3 x 0x48 bytes) */
typedef struct {
    Sint32 key;                 /* 0x00 compared with func_8C07CE40(0) by func_8C0B3A2E */
    NJS_POINT3 ofs;             /* 0x04 spawn offset (func_8C0B3480) */
    Float vec_scl;              /* 0x10 func_8C0B3480 */
    Float scl;                  /* 0x14 func_8C0B3480 */
    Sint32 type;                /* 0x18 func_8C0B3480: 0, 1, 2 */
    Float f1C;                  /* 0x1C func_8C0B37F2 */
    Float f20;                  /* 0x20 func_8C0B37F2 */
    Float f24;                  /* 0x24 func_8C0B37F2 */
    Float damp;                 /* 0x28 speed damping */
    Float buoyancy;             /* 0x2C added to spd.y every frame */
    Float f30;                  /* 0x30 func_8C0B37F2 */
    Float rate;                 /* 0x34 spawn probability per frame */
    Float f38;                  /* 0x38 func_8C0B37F2 */
    Float grow;                 /* 0x3C size increment per frame */
    Float jitter_rate;          /* 0x40 probability of a sideways kick */
    Float jitter;               /* 0x44 sideways kick amplitude */
} BUBBLE_PARAM;                 /* 0x48 */

SA1_ASSERT_SIZE(BUBBLE_PARAM, 0x48);

/* one bubble (MAlloc(0x34) by func_8C0B33A0, MFree by func_8C0B33CE) */
typedef struct bubble {
    Float surface;              /* 0x00 water surface height (pwp->shadow.y_top) */
    NJS_POINT3 pos;             /* 0x04 */
    NJS_POINT3 spd;             /* 0x10 */
    Sint16 ang;                 /* 0x1C sprite angle */
    Sint16 angspd;              /* 0x1E */
    Float aspect;               /* 0x20 sprite sy / sx */
    Float size;                 /* 0x24 sprite sx */
    Float maxsize;              /* 0x28 */
    struct bubble *prev;        /* 0x2C */
    struct bubble *next;        /* 0x30 */
} BUBBLE;                       /* 0x34 */

#define BUBBLE_PARAM_OF(twp) (&((BUBBLE_PARAM *)(twp)->counter.ptr)[(Uint8)(twp)->btimer])

extern char D_8C15BCB4[];               /* "player" */
extern NJS_TEXLIST D_8C1C3068;
extern NJS_TEXANIM D_8C1C67DC[];

extern Sint32 func_8C042DC4(task *tp);  /* debug check, always 0 */
extern void func_8C09E778(NJS_SPRITE *sp, Sint32 n, Uint32 attr); /* 3D sprite draw (skipped when B_8C754EC8) */

ASM_STUB(void func_8C0B33CE(taskwk *twp, BUBBLE *p))
ASM_STUB(void func_8C0B3480(taskwk *twp, NJS_POINT3 *pos, NJS_POINT3 *vec, NJS_POINT3 *pos2))
ASM_STUB(BUBBLE *func_8C0B37F2(taskwk *twp))

void func_8C0B3F2C(task *tp);
void func_8C0B4040(taskwk *twp);
void func_8C0B41E0(taskwk *twp);
void func_8C0B4332(taskwk *twp);
void func_8C0B4436(task *tp);
void func_8C0B4550(task *tp);
void func_8C0B4600(task *tp);
void func_8C0B46CC(task *tp);
void func_8C0B4724(task *tp);
void func_8C0B4820(task *tp);

#ifdef NON_MATCHING
/*
 * NON_MATCHING (SHC 5.1r03): control flow, statement order, every load,
 * store and operation follow the original; about 40 instructions differ:
 * - btimer selection: the original keeps two "MOV.B R0,@(3,R13)" stores
 *   (one after the 1/2 ternary, one for 0); SHC merges every if/else,
 *   switch and conditional-expression form tried into one store at the join
 *   (the temporary n gets closest: MOV R4,R0 before the shared store).
 * - register allocation / scheduling only: the call register of the two
 *   rand() calls (R2 vs R3), the pointer registers of the c = pos2 - pos
 *   walk, the stack slots of len and grow (44/40 swapped), the pos/spd walk
 *   pointers (R5/R4) and px/py (FR9/FR7), rx/rz (FR13/FR12) and the order
 *   of the loads in the distance test.
 * The x/y/z accesses are explicit Float pointer walks because the original
 * reads them with FMOV.S @Rn+ and writes them back with FMOV.S @-Rn; they
 * rely on NJS_POINT3 being three packed Floats (true on every target).
 * Note: a bubble released by func_8C0B33CE (MFree) is still updated by the
 * jitter and angle code below, exactly as in the original (writes into a
 * block the game heap has just released).
 */
void func_8C0B3B1A(task *tp)
{
    NJS_POINT3 pos;
    NJS_POINT3 vec;
    NJS_POINT3 pos2;
    NJS_POINT3 c;
    Float len;
    Float grow;
    Float ndy;
    Float ndx;
    Float buoyancy;
    Float jitter_rate;
    Float cy;
    Float cx;
    Float cz;
    Float jitter;
    Float r;
    Float damp;
    Float rx;
    Float ry;
    Float rz;
    Float dist2;
    Float s;
    Float size;
    Float d;
    taskwk *twp;
    BUBBLE_PARAM *prm;
    BUBBLE *p;
    BUBBLE *next;
    Float *a;
    Float *b;
    Float *v;
    Float *w;
    Sint32 n;
    Float sx;
    Float sy;
    Float sz;
    Float px;
    Float py;
    Float pz;
    Float y;
    Float max;
    Float top;

    twp = tp->twp;
    if (func_8C042DC4(tp)) {
        njPrintC(0x00120015, D_8C15BCB4);
        return;
    }
    /* the original tests the sum counter.ptr + btimer * 0x48 (not the base
     * pointer): done on integers, as a NULL test of &ptr[i] is folded into a
     * test of ptr by clang */
    if (!playertwp[(Sint8)SA1_FTOI(twp->scl.z)] ||
        !((SA1_UPTR)twp->counter.ptr + (Uint8)twp->btimer * sizeof(BUBBLE_PARAM))) {
        FreeTask(tp);
        return;
    }
    /* parameter set: 1 (smode set) or 2 while the player's smode is 39 */
    if (playertwp[(Sint8)SA1_FTOI(twp->scl.z)]->smode == 39) {
        n = twp->smode ? 1 : 2;
        twp->btimer = n;
    } else {
        twp->btimer = 0;
    }
    if (twp->smode) {
        /* burst: one bubble per frame (sets 1/2) or at the set's rate */
        if (twp->btimer || (Float)rand() * 0.000030517578125f < BUBBLE_PARAM_OF(twp)->rate) {
            func_8C0B37F2(twp);
        }
        if (twp->wtimer-- <= 0) {
            twp->smode = 0;
        }
    } else {
        if (twp->btimer && (Float)rand() * 0.000030517578125f < BUBBLE_PARAM_OF(twp)->rate) {
            func_8C0B37F2(twp);
        }
        /* start a burst when the player's motion wraps around */
        /* FCMP/GT nframe > 1.0f, branch when true: a NaN frame starts a burst */
        if (!(GetPlayerWorkPtr(SA1_FTOI(twp->scl.z))->mj.nframe > 1.0f)) {
            twp->smode = 1;
            twp->wtimer = 40;
        }
    }
    func_8C0B3F2C(tp);
    if (B_8C754EC8) {
        return;
    }

    /* sphere that pushes the bubbles away: centre c = pos + (pos2 - pos) * 2,
     * radius len = |(pos2 - pos) * 2| */
    func_8C0B3480(twp, &pos, &vec, &pos2);
    a = &pos.x;
    b = &pos2.x;
    v = &c.x;
    *v++ = *b++ - *a++;
    *v++ = *b++ - *a++;
    *v = *b - *a;
    v = &c.x;
    *v++ *= 2.0f;
    *v++ *= 2.0f;
    *v *= 2.0f;
    len = njScalor(&c);
    a = &pos.x;
    v = &c.x;
    *v++ += *a++;
    *v++ += *a++;
    *v += *a;
    cx = c.x;
    cy = c.y;
    cz = c.z;
    prm = BUBBLE_PARAM_OF(twp);
    damp = prm->damp;
    buoyancy = prm->buoyancy;
    grow = prm->grow;
    jitter_rate = prm->jitter_rate;
    jitter = prm->jitter;
    ndx = -cx;
    ndy = -cy;
    for (p = (BUBBLE *)twp->value.ptr; p; p = next) {
        next = p->next;

        /* move: pos += spd; spd = (spd + (0, buoyancy, 0)) * damp */
        w = &p->pos.x;
        v = &p->spd.x;
        px = *w++;
        sx = *v++;
        px += sx;
        sx *= damp;
        py = *w++;
        sy = *v++;
        py += sy;
        sy += buoyancy;
        pz = *w;
        sz = *v;
        pz += sz;
        sz *= damp;
        sy *= damp;
        *v = sz;
        *--v = sy;
        *--v = sx;
        *w = pz;
        *--w = py;
        *--w = px;

        /* push out of the sphere (never downwards) */
        v = &p->pos.x;
        rx = ndx + *v++;
        ry = ndy + *v++;
        rz = *v - cz;
        r = len + p->size;
        dist2 = SA1_FMAC(rz, rz, SA1_FMAC(rx, rx, ry * ry));   /* FMUL ry*ry, FMAC rx*rx, FMAC rz*rz */
        if (r * r > dist2 && dist2 > 0.01f) {
            s = (r + 0.099999996f) / njSqrt(dist2);   /* 0.1f */
            rx *= s;
            rz *= s;
            ry *= s;
            v = &p->pos.x;
            *v++ = cx + rx;
            if (!(ry < 0.0f)) {     /* FCMP/GT 0 > ry: a NaN ry takes this branch */
                *v++ = cy + ry;
                *v = cz + rz;
            } else {
                v[1] = cz + rz;
            }
        }

        /* grow, but stay below the water surface; burst at the surface */
        y = p->pos.y;
        max = p->maxsize;
        size = p->size + grow;
        if (size > max) {
            size = max;
        }
        top = p->surface;
        d = top - size - y;
        if (!(top > y)) {           /* FCMP/GT top > y: a NaN height bursts the bubble */
            func_8C0B33CE(twp, p);
        } else {
            if (d < 0.0f) {
                size += d;
            }
            p->size = size;
        }

        /* random sideways kick (the "+=" are FMACs) */
        if ((Float)rand() * 0.000030517578125f < jitter_rate) {
            w = &p->spd.x;
            SA1_FMAC_ACC(*w, jitter, SA1_FMAC((Float)rand(), 0.000030517578125f, -0.5f));
            w++;
            SA1_FMAC_ACC(w[1], jitter, SA1_FMAC((Float)rand(), 0.000030517578125f, -0.5f));
        }
        p->ang += p->angspd;
    }
    twp->wtimer++;
}
#else
ASM_STUB(void func_8C0B3B1A(task *tp))
#endif

void func_8C0B3F2C(task *tp)
{
    taskwk *twp;
    NJS_SPRITE sp;
    BUBBLE *p;

    twp = tp->twp;
    if (B_8C754EC8 > 0) {
        return;
    }
    sp.tlist = &D_8C1C3068;
    sp.tanim = D_8C1C67DC;
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 10);
    for (p = (BUBBLE *)twp->value.ptr; p; p = p->next) {
        sp.ang = p->ang;
        sp.p = p->pos;
        sp.sx = p->size;
        sp.sy = p->size * p->aspect;
        func_8C09E778(&sp, 0, 49);
    }
}

void func_8C0B3FA6(task *tp)
{
    func_8C0B4040(tp->twp);
    tp->exec = func_8C0B4550;
    tp->disp = func_8C0B4436;
    func_8C0B4436(tp);
}

void func_8C0B3FC0(task *tp)
{
    func_8C0B41E0(tp->twp);
    tp->exec = func_8C0B46CC;
    tp->disp = func_8C0B4600;
    func_8C0B4600(tp);
}

void func_8C0B3FDA(task *tp)
{
    func_8C0B4332(tp->twp);
    tp->exec = func_8C0B4820;
    tp->disp = func_8C0B4724;
    func_8C0B4724(tp);
}

ASM_STUB(void func_8C0B4040(taskwk *twp))
ASM_STUB(void func_8C0B41E0(taskwk *twp))
ASM_STUB(void func_8C0B4332(taskwk *twp))
ASM_STUB(void func_8C0B4436(task *tp))
ASM_STUB(void func_8C0B4550(task *tp))
ASM_STUB(void func_8C0B4600(task *tp))
ASM_STUB(void func_8C0B46CC(task *tp))
ASM_STUB(void func_8C0B4724(task *tp))
ASM_STUB(void func_8C0B4820(task *tp))
