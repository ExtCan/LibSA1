/*
 * 8C0B37F2-8C0B3B1A (chunk asm/1st_read/text/8C0B33A0.src, functions 5-7).
 *
 * Underwater breath bubbles of a player. PInitialize (8C06C350) creates a
 * helper task per player with func_8C0B3A2E as its first exec and stores the
 * player number as a float in twp->scl.z. The task keeps:
 *   twp->counter.ptr  the character's 3 bubble parameter sets (D_8C1C63A4)
 *   twp->btimer       the parameter set in use (0..2, chosen by the exec)
 *   twp->value.ptr    head of a doubly linked list of MAlloc'd bubbles
 *                     (func_8C0B33A0 adds one, func_8C0B33CE removes one)
 *
 *   func_8C0B37F2  spawn one bubble at the emitter point (func_8C0B3480)
 *                  when the player is below a water surface
 *                  (shadow._Attr_top & 2) by less than 500 units
 *   func_8C0B3A2E  init: install exec/disp/dest, find the parameter sets
 *                  of the current character
 *   func_8C0B3AFC  dest: free every bubble
 */

#include "common.h"
#include "task.h"
#include "sa1/player.h"
#include "sa1/engine.h"

/* Bubble parameters: D_8C1C63A4, 5 characters (Tails, Big, Sonic, Knuckles,
 * Amy) x 3 sets of 0x48 bytes. Numbers only, so the layout is the same on PC. */
typedef struct BUBBLEPARAM {
    Sint32 chr;                 /* 0x00 character id, compared with func_8C07CE40(0) */
    NJS_POINT3 ofs;             /* 0x04 emitter offset (func_8C0B3480) */
    Float ofs_scl;              /* 0x10 (func_8C0B3480) */
    Float vec_scl;              /* 0x14 (func_8C0B3480) */
    Sint32 type;                /* 0x18 emitter type 0/1/2 (func_8C0B3480) */
    Float scl_max;              /* 0x1C bubble size range */
    Float scl_min;              /* 0x20 */
    Float alpha_rand;           /* 0x24 random part of the start alpha */
    Float f28;                  /* 0x28 */
    Float f2C;                  /* 0x2C */
    Float spd_rand;             /* 0x30 random speed added on each axis */
    Float f34;                  /* 0x34 */
    Float ang_rand;             /* 0x38 random tilt in degrees */
    Float f3C;                  /* 0x3C */
    Float f40;                  /* 0x40 */
    Float f44;                  /* 0x44 */
} BUBBLEPARAM;

/* One bubble, MAlloc'd by func_8C0B33A0 (MAlloc(0x34) on the DC). The two
 * list links are the last members, so only they move on 64-bit hosts. */
typedef struct BUBBLE {
    Float surface;              /* 0x00 water surface height (pwp->shadow.y_top) */
    NJS_POINT3 pos;             /* 0x04 */
    NJS_POINT3 spd;             /* 0x10 */
    Sint16 angy;                /* 0x1C */
    Sint16 angx;                /* 0x1E */
    Float alpha;                /* 0x20 */
    Float scl_spd;              /* 0x24 */
    Float scl;                  /* 0x28 */
    struct BUBBLE *prev;        /* DC +2C */
    struct BUBBLE *next;        /* DC +30 */
} BUBBLE;

extern BUBBLEPARAM D_8C1C63A4[15];

extern Sint32 func_8C042DC4(task *tp);
extern Sint32 func_8C07CE40(Uint8 pno);    /* canonical: sa1/protos.h */
void func_8C0B3B1A(task *tp);               /* exec (same chunk, still assembly) */
void func_8C0B3F2C(task *tp);               /* disp (same chunk, still assembly) */
void func_8C0B3AFC(task *tp);

ASM_STUB(BUBBLE *func_8C0B33A0(taskwk *twp))
ASM_STUB(void func_8C0B33CE(taskwk *twp, BUBBLE *bp))
ASM_STUB(void func_8C0B3480(taskwk *twp, NJS_POINT3 *pos, NJS_POINT3 *vec, NJS_POINT3 *pos2))

/* current parameter set (re-read from the task work at every use, as the original does) */
#define BPARAM(twp) (&((BUBBLEPARAM *)(twp)->counter.ptr)[(Uint8)(twp)->btimer])

/*
 * NON_MATCHING: register allocation only. The original keeps pno and bp in
 * stack slots (pno stored as a byte at sp+4, the slot SHC later reuses for
 * its spilled BPARAM temp; bp at sp+0, reloaded at every use) and loads
 * pos.y/y_top into FR14/FR15. Closest form found: "(void)&bp;" plus a static
 * inlined "GetPwp(Uint8 pno) { return GetPlayerWorkPtr(pno); }" reproduces
 * everything except the pno byte store and the FR14/FR15 choice.
 */
#ifdef NON_MATCHING
BUBBLE *func_8C0B37F2(taskwk *twp)
{
    NJS_POINT3 pos;
    NJS_POINT3 spd;
    Uint8 pno;
    BUBBLE *bp;
    playerwk *pwp;
    Float scl;

    pno = (Uint8)SA1_FTOI(twp->scl.z);
    pwp = GetPlayerWorkPtr(pno);
    bp = NULL;
    if (!(pwp->shadow._Attr_top & 2)) {
        return NULL;
    }
    func_8C0B3480(twp, &pos, &spd, NULL);
    if (pwp->shadow.y_top > pos.y && pos.y + 500.0f > pwp->shadow.y_top) {
        bp = func_8C0B33A0(twp);
        if (bp) {
            /* FMAC: ((max - min) * rand) * (1/32768) + min */
            scl = SA1_FMAC((BPARAM(twp)->scl_max - BPARAM(twp)->scl_min) * (Float)rand(),
                           3.0517578125e-05f, BPARAM(twp)->scl_min);
            /* FMAC rand * (1/32768) + -0.5, FMUL spd_rand, FMAC * 2.0 + spd */
            spd.x = SA1_FMAC(SA1_FMAC((Float)rand(), 3.0517578125e-05f, -0.5f) * BPARAM(twp)->spd_rand,
                             2.0f, spd.x);
            spd.y = SA1_FMAC(SA1_FMAC((Float)rand(), 3.0517578125e-05f, -0.5f) * BPARAM(twp)->spd_rand,
                             2.0f, spd.y);
            spd.z = SA1_FMAC(SA1_FMAC((Float)rand(), 3.0517578125e-05f, -0.5f) * BPARAM(twp)->spd_rand,
                             2.0f, spd.z);
            bp->pos = pos;
            bp->spd = spd;
            /* 0x40BFFFFF (just below 6.0) */
            bp->angy = (Sint16)SA1_FTOI((Float)rand() * 5.9999996f);
            /* degrees to Angle: 0x43360B60 (65536 / 360 truncated) */
            bp->angx = (Sint16)SA1_FTOI(SA1_FMAC((Float)rand(), 3.0517578125e-05f, -0.5f)
                                        * BPARAM(twp)->ang_rand * 182.044437f);
            bp->surface = pwp->shadow.y_top;
            bp->alpha = 1.0f - (Float)rand() * BPARAM(twp)->alpha_rand * 3.0517578125e-05f;
            bp->scl_spd = scl * 0.099999996f;
            bp->scl = scl;
        }
    }
    return bp;
}
#else
ASM_STUB(BUBBLE *func_8C0B37F2(taskwk *twp))
#endif

void func_8C0B3A2E(task *tp)
{
    taskwk *twp;
    Sint32 i;

    twp = tp->twp;
    tp->disp = func_8C0B3F2C;
    tp->exec = func_8C0B3B1A;
    tp->dest = func_8C0B3AFC;
    twp->value.ptr = NULL;
    twp->smode = 0;
    twp->wtimer = 0;
    if (func_8C042DC4(tp) == 0) {
        twp->btimer = 0;
        twp->counter.ptr = NULL;
        /* unsigned compare (CMP/HS) and a kept counter: plain "i < 15"
         * makes SHC compare the element pointer instead */
        for (i = 0; (Uint32)i < 15; i += 3) {
            if (func_8C07CE40(0) == D_8C1C63A4[i].chr) {
                twp->counter.ptr = &D_8C1C63A4[i];
                return;
            }
        }
    }
}

void func_8C0B3AFC(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    while (twp->value.ptr != NULL) {
        func_8C0B33CE(twp, (BUBBLE *)twp->value.ptr);
    }
}
