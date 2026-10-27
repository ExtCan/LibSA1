/*
 * 1ST_READ 8C028BDC-8C028E60: land collision, NEAR-poly list.
 *
 * 8C028BDC is the compiled squared distance |a - b|^2 (the FIPR inline_asm
 * version 8C028BC0 precedes it in the same object).
 *
 * 8C028BFE gathers, for the player collision sphere csts (radius grown by
 * |spd|), the land polygons of every polygon list in D_8C02D548 whose land
 * object is in the current gather output (B_8C754EF4, built here by
 * 8C0522C0). Each polygon list is a doubly linked list of polygons sorted by
 * the z of their bounding sphere centre; it is walked forwards from the head
 * or backwards from the tail, whichever end is nearer the query z range
 * [lo, hi], and every polygon whose centre is within 1.3 * (r^2 + R^2) (all
 * squared) of the sphere centre is appended to B_8C73E280 (96 entries, count
 * B_8C73E27C). Every list records its first entry and its number of entries.
 * Returns the total count.
 *
 * SA1_GA (libsa1's lowered game code only; not the SHC build, the harness's
 * SHC / TARGET_PC builds or their traces): a PC-only capacity option (libsa1
 * docs/libsa1_integration.md 2.1 / 5.7, docs/research/gsa1_ramp_freeze.md
 * fix 3). The list may be raised to SA1_NEARCAP_MAX entries in a pcdata block
 * (segment 0x18 in libsa1: one per world) that a host world's collision
 * adapter (LIB src/world/sa1_coladapt_world.c) finds by its magic words and
 * enables by setting cap. While cap <= 96, the initial value, the function
 * is the original: the DC list B_8C73E280[96] and its "== 96" stop, so worlds
 * that never set it (the reference / exactness worlds) run the game's code.
 * Full lists are counted (full) and the fullest one kept (peak) for the
 * adapter's capacity reports.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_funcs.h"
/* sa1/collision.h declares 8C028BFE as void; it returns the NEAR-poly count */
#define func_8C028BFE func_8C028BFE_void
#include "sa1/collision.h"
#undef func_8C028BFE

/* polygon node: bounding sphere of one land polygon (start of the node) */
typedef struct NEARPOLY {
    struct NEARPOLY *next;      /* 0x00 towards larger center.z */
    struct NEARPOLY *prev;      /* 0x04 towards smaller center.z */
    NJS_POINT3 center;          /* 0x08 bounding sphere centre (list sorted by z) */
    Float rr;                   /* 0x14 bounding sphere radius squared */
} NEARPOLY;

/* polygon list of one land object (singly linked from D_8C02D548) */
typedef struct NEARLIST {
    struct NEARLIST *next;      /* 0x00 */
    Sint32 unk04;               /* 0x04 not accessed here (may be a pointer) */
    NJS_OBJECT *object;         /* 0x08 compared with _OBJ_LANDCOLL.pObject */
    Sint32 unk0C;               /* 0x0C not accessed here (may be a pointer) */
    NEARPOLY *head;             /* 0x10 smallest center.z */
    NEARPOLY *tail;             /* 0x14 largest center.z */
    Sint32 unk18;               /* 0x18 not accessed here */
    NEARPOLY **near;            /* 0x1C first entry in B_8C73E280, NULL if none */
    Sint32 unk20;               /* 0x20 not accessed here */
    Sint32 near_num;            /* 0x24 number of entries in B_8C73E280 */
    Float zr;                   /* 0x28 half extent in z */
} NEARLIST;

SA1_ASSERT_OFFSET(NEARPOLY, center, 0x08);
SA1_ASSERT_OFFSET(NEARPOLY, rr, 0x14);
SA1_ASSERT_OFFSET(NEARLIST, object, 0x08);
SA1_ASSERT_OFFSET(NEARLIST, head, 0x10);
SA1_ASSERT_OFFSET(NEARLIST, near, 0x1C);
SA1_ASSERT_OFFSET(NEARLIST, near_num, 0x24);
SA1_ASSERT_OFFSET(NEARLIST, zr, 0x28);

extern NEARLIST *D_8C02D548;            /* polygon lists */
extern Sint32 B_8C73E27C;               /* NEAR-poly count */
extern NEARPOLY *B_8C73E280[];          /* NEAR-poly buffer (96 entries) */
extern Sint8 tcrf_CollisionDisplayVar;  /* 8C752BDC */
extern const char D_8C11248C[];         /* "WARNING: NEAR-poly is FULL" */

Sint32 func_8C028BFE(csts *cp);

#if defined(SA1_GA)
#define SA1_NEARCAP_MAGIC0 0x5041434E   /* "NCAP" (sa1_coladapt_world.c CW_NEARCAP_*) */
#define SA1_NEARCAP_MAGIC1 0x45464238   /* "8BFE" */
/* 512 since round 0.9.2 (sadc docs/research/gsa1_090_collision.md F5: 256 filled up on sgm_skate3_ultramegapark);
 * LIB's CW_NEARCAP_BYTES / CW_NEAR_RAISED follow it */
#define SA1_NEARCAP_MAX 512
typedef struct SA1_NEARCAP {
    Uint32 magic[2];            /* 0x00 SA1_NEARCAP_MAGIC0, SA1_NEARCAP_MAGIC1 */
    Sint32 cap;                 /* 0x08 entries per query (<= 96: the DC list) */
    Sint32 full;                /* 0x0C queries that filled the list */
    Sint32 peak;                /* 0x10 the fullest list since the adapter cleared it */
    NEARPOLY *pool[SA1_NEARCAP_MAX];    /* 0x14 the raised list */
} SA1_NEARCAP;
SA1_ASSERT_OFFSET(SA1_NEARCAP, pool, 0x14);
static SA1_NEARCAP sa1_nearcap = { { SA1_NEARCAP_MAGIC0, SA1_NEARCAP_MAGIC1 }, 96, 0, 0 };
#define SA1_NEAR_CAP cap
#else
#define SA1_NEAR_CAP 96
#endif

/*
 * NON_MATCHING: the original walks both points with post-increment pointers
 * (FMOV.S @R4+/@R5+) and loads all six values into separate registers.
 * "dx = *a++ - *b++" style code interleaves the loads (b values in FR3);
 * the closest found (4 instructions off: b0 in FR7 instead of FR5) is
 *   Float *a, *b; ax = *a++; bx = *b++; ay = *a++; az = *a; ax -= bx;
 *   by = *b++; bz = *b; ay -= by; az -= bz;
 *   return ax * ax + ay * ay + az * az;
 * with the statement order a0 b0 a1 s0 a2 b1 b2 s1 s2 giving the right
 * schedule but FR5/FR6 swapped. SHC computes dy*dy first, then FMACs dx*dx
 * and dz*dz, from the plain "dx*dx + dy*dy + dz*dz".
 */
#ifdef NON_MATCHING
Float func_8C028BDC(NJS_POINT3 *a, NJS_POINT3 *b)
{
    Float dx, dy, dz;

    dx = a->x - b->x;
    dy = a->y - b->y;
    dz = a->z - b->z;
    return SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy));
}
#else
ASM_STUB(Float func_8C028BDC(NJS_POINT3 *a, NJS_POINT3 *b))
#endif

/*
 * NON_MATCHING: same instructions, control flow, scheduling and literal pool
 * as the original; only register allocation differs (56 of 247 instructions):
 * r/rr are in FR15/FR14 instead of FR14/FR15, the 1.3f constant in FR7
 * instead of FR6, and in the sphere test SHC computes d = t * t into t's own
 * register (the original keeps d in FR7 and t in FR5/FR8), so lo/lim/p/q
 * shift as well. Notes for matching:
 * - "*buf = np; buf++;" (not "*buf++ = np") and "goto end" (not an early
 *   "return B_8C73E27C") are needed for buf/&B_8C73E27C/cp in R14/R13/R12;
 * - "rr = njScalor(..); r = rr + cp->radius;" fixes R8/R9 (njPrintC/&tcrf);
 * - "d += t * t" gives FMAC (SA1_FMAC_ACC: fused on PC); "d = t * t + d" or
 *   "d = d + t * t" add 10 instructions;
 * - the explicit pointers p/q are needed for the @R6+/@R7+ loads.
 */
#ifdef NON_MATCHING
Sint32 func_8C028BFE(csts *cp)
{
    NEARPOLY **buf;
    NEARLIST *lp;
    NEARPOLY *np;
    NEARPOLY *tail;
    _OBJ_LANDCOLL *lc;
    Sint32 i;
    Sint32 found;
    Float *p, *q;
    Float r;
    Float rr;
    Float lo, hi;
    Float lim, t, d;
#if defined(SA1_GA)
    Sint32 cap;
#endif

    B_8C73E27C = 0;
    buf = B_8C73E280;
#if defined(SA1_GA)
    cap = 96;
    if (sa1_nearcap.cap > 96) {
        cap = sa1_nearcap.cap < SA1_NEARCAP_MAX ? sa1_nearcap.cap : SA1_NEARCAP_MAX;
        buf = sa1_nearcap.pool;
    }
#endif
    rr = njScalor(&cp->spd);
    r = rr + cp->radius;
    rr = r * r;
    func_8C0522C0(cp->pos.x, cp->pos.y, cp->pos.z, cp->radius);
    for (lp = D_8C02D548; lp != NULL; lp = lp->next) {
        lp->near = NULL;
        lp->near_num = 0;
        found = FALSE;
        lc = B_8C754EF4;
        for (i = B_8C754EF0; i > 0; i--, lc++) {
            if (lc->pObject == lp->object) {
                found = TRUE;
                break;
            }
        }
        if (!found) {
            continue;
        }
        np = lp->head;
        tail = lp->tail;
        /* the original reads head->center.z / tail->center.z unchecked */
        PC_ONLY(if (np == NULL || tail == NULL) continue;)
        lo = cp->pos.z - lp->zr - r;
        hi = lp->zr + r + cp->pos.z;
        if (lo > np->center.z && tail->center.z > hi) {
            while (!(np->center.z > lo)) {
                np = np->next;
                if (np == NULL) {
                    break;
                }
            }
        }
        if (np != NULL && np->center.z > lo) {
            for (; np != NULL && hi > np->center.z; np = np->next) {
                lim = (rr + np->rr) * 1.3f;
                p = &np->center.x;
                q = &cp->pos.x;
                t = *p++ - *q++;
                d = t * t;
                if (d > lim) {
                    continue;
                }
                t = *p++ - *q++;
                SA1_FMAC_ACC(d, t, t);
                if (d > lim) {
                    continue;
                }
                t = *p - *q;
                SA1_FMAC_ACC(d, t, t);
                if (d > lim) {
                    continue;
                }
                if (lp->near == NULL) {
                    lp->near = buf;
                }
                lp->near_num++;
                *buf = np;
                buf++;
                if (++B_8C73E27C == SA1_NEAR_CAP) {
#if defined(SA1_GA)
                    sa1_nearcap.full++;
#endif
                    if (tcrf_CollisionDisplayVar >= 0) {
                        njPrintC(0x00020015, D_8C11248C);
                    }
                    goto end;
                }
            }
        } else {
            /* the original tests np->center.z before np != NULL, i.e. it
             * reads NULL->center.z (DC boot ROM) once it walks past the head;
             * the PC build tests NULL first (same result) */
#ifdef TARGET_PC
            for (np = tail; np != NULL && np->center.z > lo; np = np->prev) {
#else
            for (np = tail; np->center.z > lo && np != NULL; np = np->prev) {
#endif
                lim = (rr + np->rr) * 1.3f;
                p = &np->center.x;
                q = &cp->pos.x;
                t = *p++ - *q++;
                d = t * t;
                if (d > lim) {
                    continue;
                }
                t = *p++ - *q++;
                SA1_FMAC_ACC(d, t, t);
                if (d > lim) {
                    continue;
                }
                t = *p - *q;
                SA1_FMAC_ACC(d, t, t);
                if (d > lim) {
                    continue;
                }
                if (lp->near == NULL) {
                    lp->near = buf;
                }
                lp->near_num++;
                *buf = np;
                buf++;
                if (++B_8C73E27C == SA1_NEAR_CAP) {
#if defined(SA1_GA)
                    sa1_nearcap.full++;
#endif
                    if (tcrf_CollisionDisplayVar >= 0) {
                        njPrintC(0x00020015, D_8C11248C);
                    }
                    goto end;
                }
            }
        }
    }
end:
#if defined(SA1_GA)
    if (B_8C73E27C > sa1_nearcap.peak) {
        sa1_nearcap.peak = B_8C73E27C;
    }
#endif
    return B_8C73E27C;
}
#else
ASM_STUB(Sint32 func_8C028BFE(csts *cp))
#endif
