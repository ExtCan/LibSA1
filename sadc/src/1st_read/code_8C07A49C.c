/*
 * Player motion (1ST_READ 8C07A49C-8C07A55C).
 *
 *  func_8C07A49C  frees the key arrays of the link (blend) motion that
 *                 PSetMotion 8C079476 builds in mjp->actwkptr: for every
 *                 node of the current action (PL_ACTION.objnum of
 *                 mjp->reqaction) each non-NULL key pointer p[k] of the
 *                 node's NJS_MDATA2/NJS_MDATA3 (inp_fn 2 or 3) is MFree'd
 *                 and both p[k] and its key count nb[k] are cleared.
 *                 PSetMotion calls it (BSR, 3 sites) before rebuilding the
 *                 link and, in its default case, before freeing the link.
 *
 * The original walks mdata as a flat array of 32-bit slots: nb[k] is the
 * slot inp_fn after p[k], and a node is 4 (MDATA2) or 6 (MDATA3) slots.
 * That only holds for the DC layout, so the slot walk goes through the
 * MDATA_* macros below: plain slot arithmetic under SHC (the matching
 * code), typed NJS_MDATA2/NJS_MDATA3 access on the PC target.
 */
#include "sa1/sa1.h"

/* Ninja motion data of one node (Ninja SDK layout; no shared header yet) */
typedef struct {
    void *p[2];                 /* key arrays: pos, ang */
    Uint32 nb[2];               /* key counts */
} NJS_MDATA2;                   /* DC: 0x10 */

typedef struct {
    void *p[3];                 /* key arrays: pos, ang, scl */
    Uint32 nb[3];               /* key counts */
} NJS_MDATA3;                   /* DC: 0x18 */

SA1_ASSERT_SIZE(NJS_MDATA2, 0x10);
SA1_ASSERT_SIZE(NJS_MDATA3, 0x18);

/*
 * p points at p[k] of the current node (k = 0..2), inp is NJS_MOTION.inp_fn.
 * MDATA_CLEAR_NB clears nb[k]; MDATA2_NEXT/MDATA3_NEXT step from p[2]'s slot
 * (k = 2) to p[0] of the next node.
 */
#ifdef TARGET_PC
#define MDATA_NB(p, k, inp)                                              \
    (*((inp) == 3 ? &((NJS_MDATA3 *)((p) - (k)))->nb[k]                  \
                  : &((NJS_MDATA2 *)((p) - (k)))->nb[k]))
#define MDATA_CLEAR_NB(p, k, inp) (MDATA_NB(p, k, inp) = 0)
#define MDATA3_NEXT(p) ((p) = ((NJS_MDATA3 *)((p) - 2) + 1)->p)
#define MDATA2_NEXT(p) ((p) = ((NJS_MDATA2 *)((p) - 2) + 1)->p)
#else
#define MDATA_CLEAR_NB(p, k, inp) ((p)[inp] = NULL)
#define MDATA3_NEXT(p) ((p) += 4)
#define MDATA2_NEXT(p) ((p) += 2)
#endif

void func_8C07A49C(mtnjvwk *mjp)
{
    NJS_ACTION *act;
    NJS_MOTION *motion;
    Uint16 inp_fn;
    void **p;
    Sint32 n;

    motion = mjp->actwkptr->motion;
    act = mjp->actwkptr;
    p = motion->mdata;
    inp_fn = motion->inp_fn;
    for (n = mjp->plactptr[mjp->reqaction].objnum; n > 0; n--) {
        if (*p) {
            MFree(*p);
            *p = NULL;
            MDATA_CLEAR_NB(p, 0, inp_fn);
        }
        p++;
        if (*p) {
            MFree(*p);
            *p = NULL;
            MDATA_CLEAR_NB(p, 1, inp_fn);
        }
        p++;
        if (inp_fn == 3) {
            if (*p) {
                MFree(*p);
                *p = NULL;
                MDATA_CLEAR_NB(p, 2, inp_fn);
            }
        }
        if (inp_fn == 3) {
            MDATA3_NEXT(p);
        } else {
            MDATA2_NEXT(p);
        }
        if (0) {
            /*
             * Never executed. The original kept a reference to act here
             * (most likely compiled-out debug code): SHC still gives act
             * a stack slot (MOV.L R5,@R15 at 8C07A4B2, never read back),
             * which also decides the R9/R10/R11 assignment. Without it the
             * function compiles 3 instructions shorter.
             */
            act->motion = NULL;
        }
    }
}
