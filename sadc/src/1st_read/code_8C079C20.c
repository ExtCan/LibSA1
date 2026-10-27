#include "sa1/sa1.h"
#include "sa1/pending.h"    /* NJS_MKEY_F/A, NJS_MDATA2/3 */

/*
 * Player motion link (1ST_READ 8C079C20-8C07A49C), called by PSetMotion
 * 8C079476 when the requested action starts from the same object tree as
 * the current one.
 *
 * NOTE (function boundary): the manifest treats 8C079C20-8C07A49C as one
 * function, but it is two. func_8C079C20 (8C079C20-8C079C76, 86 bytes) only
 * classifies the two motions and tail-calls (BRA) func_8C079C76, which
 * follows it directly and has its own prologue; func_8C079C76
 * (8C079C76-8C07A49C) builds the link motion.
 * Both functions compile to the original code (0 of 1003 instructions differ
 * when the two are compared as one range), but shcdiff and the splice see
 * only the manifest's single function, so func_8C079C20 stays under
 * NON_MATCHING until config/symbols.1st_read.txt names
 * "func_8C079C76 = 0x8C079C76;" and the image is re-split.
 *
 * The link motion mjp->actwkptr (allocated by PSetMotion: 2 frames, mdata
 * CAlloc'd for 111 nodes) gets, for every node of the requested action's
 * object tree, a two-key channel for position, rotation and (when either
 * motion has one) scale:
 *   key 0 = the current action's motion sampled at mjp->nframe (linear
 *           interpolation between the surrounding keys; past the last key
 *           it wraps to key 0 over the motion's nbFrame), or the node's own
 *           pos/ang/scl when that motion has no keys for the channel;
 *   key 1 = the first key of the requested action's motion, or again the
 *           node's own pos/ang/scl.
 * Rotation keys are reduced to signed 16-bit angles and key 1 is moved by
 * whole turns to the side of key 0 (func_8C055D70), so the link interpolates
 * the short way round.
 */

/*
 * The current motion's key counts are read through the NJS_MDATA2 view even
 * when it is an NJS_MDATA3 (actnum == 3), an original bug: on the DC layout
 * nb[0] then reads p[2] (the scale key pointer: 0, or an address far above
 * any key index, so the position key search is unbounded either way) and
 * nb[1] reads nb[0] (the position key count is used as the rotation bound).
 * On native PC layouts the same offsets hold other data, so the PC build
 * reproduces the DC values explicitly. SHC: plain field reads.
 */
#if defined(TARGET_PC)
#define MD2_NB0(md, n) ((n) == 3 ? 0 : (md)->nb[0])
#define MD2_NB1(md, n) ((n) == 3 ? ((NJS_MDATA3 *)(md))->nb[0] : (md)->nb[1])
#else
#define MD2_NB0(md, n) ((md)->nb[0])
#define MD2_NB1(md, n) ((md)->nb[1])
#endif

/*
 * x += a * b: one FMAC on SH-4. The compound assignment is the form that
 * matches (SHC evaluates the address of x once; "x = SA1_FMAC(a, b, x)"
 * costs a register copy), so SHC keeps it and the PC build fuses the
 * multiply-add with SA1_FMAC (6 uses = the 6 FMACs of func_8C079C76).
 */
#if defined(TARGET_PC)
#define LNK_FMAC(x, a, b) ((x) = SA1_FMAC((a), (b), (x)))
#else
#define LNK_FMAC(x, a, b) ((x) += (a) * (b))
#endif

/*
 * func_8C079C76 lies inside the manifest's func_8C079C20 (see the top of the
 * file), so everything checked per function (the 6 FMACs of the link, the
 * harness's routing of 8C079C20) belongs to func_8C079C20. The PC build gives
 * it internal linkage (libsa1 has no other caller), so clang folds it into
 * its only caller like the manifest range does; the definition below keeps
 * the internal linkage of this first declaration. SHC: an ordinary external
 * function, as in the original.
 */
#if defined(TARGET_PC)
static void func_8C079C76(mtnjvwk *mjp);
#else
void func_8C079C76(mtnjvwk *mjp);
#endif

/*
 * Classifies the requested and the current action's motions (2 = pos + ang,
 * 3 = with scale) and builds the link. Both branches make the same tail call
 * (SHC merges them and only the test remains, 8C079C68-8C079C72); the test
 * is kept because it is in the original code.
 */
#ifdef NON_MATCHING
void func_8C079C20(mtnjvwk *mjp)
{
    Uint16 req;
    Uint16 act;
    NJS_MOTION *reqmtn;
    NJS_MOTION *actmtn;
    Uint16 reqnum;
    Uint16 actnum;

    req = mjp->reqaction;
    act = mjp->action;
    reqmtn = mjp->plactptr[req].actptr->motion;
    actmtn = mjp->plactptr[act].actptr->motion;
    if (reqmtn->type & NJD_MTYPE_SCL_2) {
        reqnum = 3;
    } else {
        reqnum = 2;
    }
    if (actmtn->type & NJD_MTYPE_SCL_2) {
        actnum = 3;
    } else {
        actnum = 2;
    }
    if (reqnum == 3 || actnum == 3) {
        func_8C079C76(mjp);
    } else {
        func_8C079C76(mjp);
    }
}
#else
ASM_STUB(void func_8C079C20(mtnjvwk *mjp))
#endif

/* Builds the two-key link motion mjp->actwkptr (see the top of the file). */
void func_8C079C76(mtnjvwk *mjp)
{
    NJS_OBJECT *objs[111];      /* nodes of the requested action's object (PSetMotion allocates 111) */
    Uint16 reqnum;
    Uint32 i;
    NJS_MOTION *actmtn;
    Uint16 actnum;
    NJS_MKEY_F *lpos;
    NJS_MKEY_F *lscl;
    NJS_MKEY_A *lang;
    NJS_MOTION *reqmtn;
    NJS_MDATA2 *reqmd;
    NJS_MDATA2 *actmd;
    NJS_MDATA2 *lnkmd;
    NJS_MDATA3 *reqmd3;
    NJS_MDATA3 *actmd3;
    NJS_MDATA3 *lnkmd3;
    NJS_OBJECT *obj;
    NJS_MKEY_F *kpos;
    NJS_MKEY_A *kang;
    NJS_MKEY_F *kscl;
    Uint32 j;
    Uint32 k;
    Uint32 diff;
    Uint32 nb;
    Sint16 ang16;
    Float t;
    Uint16 req;
    Uint16 act;

    req = mjp->reqaction;
    act = mjp->action;
    obj = mjp->plactptr[req].actptr->object;
    mjp->actwkptr->object = obj;
    func_8C0567B0(obj, objs);
    reqmtn = mjp->plactptr[req].actptr->motion;
    actmtn = mjp->plactptr[act].actptr->motion;
    if (reqmtn->type & NJD_MTYPE_SCL_2) {
        reqnum = 3;
    } else {
        reqnum = 2;
    }
    if (actmtn->type & NJD_MTYPE_SCL_2) {
        actnum = 3;
    } else {
        actnum = 2;
    }
    reqmd = (NJS_MDATA2 *)reqmtn->mdata;
    actmd = (NJS_MDATA2 *)actmtn->mdata;
    lnkmd = (NJS_MDATA2 *)mjp->actwkptr->motion->mdata;
    for (i = 0; i < mjp->plactptr[req].objnum; i++) {
        obj = objs[i];

        /* position */
        if ((reqmd->p[0] != NULL || actmd->p[0] != NULL)
            && (lpos = (NJS_MKEY_F *)MAlloc(sizeof(NJS_MKEY_F) * 2)) != NULL) {
            lnkmd->p[0] = lpos;
            lpos[0].keyframe = 0;
            if (actmd->p[0] != NULL) {
                kpos = (NJS_MKEY_F *)actmd->p[0];
                nb = MD2_NB0(actmd, actnum);
                k = 0;
                j = 0;
                while (mjp->nframe > (Float)kpos[j].keyframe) {
                    if (j >= nb - 1) {
                        j = 0;
                        k = nb - 1;
                        break;
                    }
                    k = j++;
                }
                lpos[0].key[0] = kpos[k].key[0];
                lpos[0].key[1] = kpos[k].key[1];
                lpos[0].key[2] = kpos[k].key[2];
                if (j != k) {
                    diff = kpos[j].keyframe - kpos[k].keyframe;
                    if (j < k) {
                        diff += actmtn->nbFrame;
                    }
                    t = (mjp->nframe - (Float)kpos[k].keyframe) / (Float)diff;
                    LNK_FMAC(lpos[0].key[0], t, kpos[j].key[0] - kpos[k].key[0]);
                    LNK_FMAC(lpos[0].key[1], t, kpos[j].key[1] - kpos[k].key[1]);
                    LNK_FMAC(lpos[0].key[2], t, kpos[j].key[2] - kpos[k].key[2]);
                }
            } else {
                lpos[0].key[0] = obj->pos[0];
                lpos[0].key[1] = obj->pos[1];
                lpos[0].key[2] = obj->pos[2];
            }
            lpos[1].keyframe = 1;
            if (reqmd->p[0] != NULL) {
                kpos = (NJS_MKEY_F *)reqmd->p[0];
                lpos[1].key[0] = kpos[0].key[0];
                lpos[1].key[1] = kpos[0].key[1];
                lpos[1].key[2] = kpos[0].key[2];
            } else {
                lpos[1].key[0] = obj->pos[0];
                lpos[1].key[1] = obj->pos[1];
                lpos[1].key[2] = obj->pos[2];
            }
            lnkmd->nb[0] = 2;
        } else {
            lnkmd->p[0] = NULL;
            lnkmd->nb[0] = 0;
        }

        /* rotation */
        if ((reqmd->p[1] != NULL || actmd->p[1] != NULL)
            && (lang = (NJS_MKEY_A *)MAlloc(sizeof(NJS_MKEY_A) * 2)) != NULL) {
            lnkmd->p[1] = lang;
            lang[0].keyframe = 0;
            if (actmd->p[1] != NULL) {
                kang = (NJS_MKEY_A *)actmd->p[1];
                nb = MD2_NB1(actmd, actnum);
                k = 0;
                j = 0;
                while (mjp->nframe > (Float)kang[j].keyframe) {
                    if (j >= nb - 1) {
                        j = 0;
                        k = nb - 1;
                        break;
                    }
                    k = j++;
                }
                lang[0].key[0] = kang[k].key[0];
                lang[0].key[1] = kang[k].key[1];
                lang[0].key[2] = kang[k].key[2];
                if (j != k) {
                    diff = kang[j].keyframe - kang[k].keyframe;
                    if (j < k) {
                        diff += actmtn->nbFrame;
                    }
                    t = (mjp->nframe - (Float)kang[k].keyframe) / (Float)diff;
                    lang[0].key[0] += SA1_FTOI((Float)SubAngle(kang[k].key[0], kang[j].key[0]) * t);
                    lang[0].key[1] += SA1_FTOI((Float)SubAngle(kang[k].key[1], kang[j].key[1]) * t);
                    lang[0].key[2] += SA1_FTOI((Float)SubAngle(kang[k].key[2], kang[j].key[2]) * t);
                }
            } else {
                lang[0].key[0] = obj->ang[0];
                lang[0].key[1] = obj->ang[1];
                lang[0].key[2] = obj->ang[2];
            }
            lang[1].keyframe = 1;
            if (reqmd->p[1] != NULL) {
                kang = (NJS_MKEY_A *)reqmd->p[1];
                lang[1].key[0] = kang[0].key[0];
                lang[1].key[1] = kang[0].key[1];
                lang[1].key[2] = kang[0].key[2];
            } else {
                lang[1].key[0] = obj->ang[0];
                lang[1].key[1] = obj->ang[1];
                lang[1].key[2] = obj->ang[2];
            }
            /* signed 16-bit angles; through a temporary (the original
             * reloads lang for the load and for the store) */
            ang16 = lang[0].key[0];
            lang[0].key[0] = ang16;
            ang16 = lang[0].key[1];
            lang[0].key[1] = ang16;
            ang16 = lang[0].key[2];
            lang[0].key[2] = ang16;
            ang16 = lang[1].key[0];
            lang[1].key[0] = ang16;
            ang16 = lang[1].key[1];
            lang[1].key[1] = ang16;
            ang16 = lang[1].key[2];
            lang[1].key[2] = ang16;
            lang[1].key[0] = func_8C055D70(lang[0].key[0], lang[1].key[0]);
            lang[1].key[1] = func_8C055D70(lang[0].key[1], lang[1].key[1]);
            lang[1].key[2] = func_8C055D70(lang[0].key[2], lang[1].key[2]);
            lnkmd->nb[1] = 2;
        } else {
            lnkmd->p[1] = NULL;
            lnkmd->nb[1] = 0;
        }

        /* scale: the link node becomes an NJS_MDATA3, its counts move up */
        if (actnum == 3 || reqnum == 3) {
            reqmd3 = (NJS_MDATA3 *)reqmd;
            actmd3 = (NJS_MDATA3 *)actmd;
            lnkmd3 = (NJS_MDATA3 *)lnkmd;
            lnkmd3->nb[1] = lnkmd->nb[1];
            lnkmd3->nb[0] = lnkmd->nb[0];
            if (((reqnum == 3 && reqmd3->p[2] != NULL) || (actnum == 3 && actmd3->p[2] != NULL))
                && (lscl = (NJS_MKEY_F *)MAlloc(sizeof(NJS_MKEY_F) * 2)) != NULL) {
                lnkmd3->p[2] = lscl;
                lscl[0].keyframe = 0;
                if (actnum == 3 && actmd3->p[2] != NULL) {
                    kscl = (NJS_MKEY_F *)actmd3->p[2];
                    nb = actmd3->nb[2];
                    k = 0;
                    j = 0;
                    while (mjp->nframe > (Float)kscl[j].keyframe) {
                        if (j >= nb - 1) {
                            j = 0;
                            k = nb - 1;
                            break;
                        }
                        k = j++;
                    }
                    lscl[0].key[0] = kscl[k].key[0];
                    lscl[0].key[1] = kscl[k].key[1];
                    lscl[0].key[2] = kscl[k].key[2];
                    if (j != k) {
                        diff = kscl[j].keyframe - kscl[k].keyframe;
                        if (j < k) {
                            diff += actmtn->nbFrame;
                        }
                        t = (mjp->nframe - (Float)kscl[k].keyframe) / (Float)diff;
                        LNK_FMAC(lscl[0].key[0], t, kscl[j].key[0] - kscl[k].key[0]);
                        LNK_FMAC(lscl[0].key[1], t, kscl[j].key[1] - kscl[k].key[1]);
                        LNK_FMAC(lscl[0].key[2], t, kscl[j].key[2] - kscl[k].key[2]);
                    }
                } else {
                    lscl[0].key[0] = obj->scl[0];
                    lscl[0].key[1] = obj->scl[1];
                    lscl[0].key[2] = obj->scl[2];
                }
                lscl[1].keyframe = 1;
                if (reqnum == 3 && reqmd3->p[2] != NULL) {
                    kscl = (NJS_MKEY_F *)reqmd3->p[2];
                    lscl[1].key[0] = kscl[0].key[0];
                    lscl[1].key[1] = kscl[0].key[1];
                    lscl[1].key[2] = kscl[0].key[2];
                } else {
                    lscl[1].key[0] = obj->scl[0];
                    lscl[1].key[1] = obj->scl[1];
                    lscl[1].key[2] = obj->scl[2];
                }
                lnkmd3->nb[2] = 2;
            } else {
                lnkmd3->p[2] = NULL;
                lnkmd3->nb[2] = 0;
            }
        }
        lnkmd = (reqnum == 3 || actnum == 3) ? (NJS_MDATA2 *)((NJS_MDATA3 *)lnkmd + 1) : lnkmd + 1;
        reqmd = (reqnum == 3) ? (NJS_MDATA2 *)((NJS_MDATA3 *)reqmd + 1) : reqmd + 1;
        actmd = (actnum == 3) ? (NJS_MDATA2 *)((NJS_MDATA3 *)actmd + 1) : actmd + 1;
    }
    if (reqnum == 3 || actnum == 3) {
        mjp->actwkptr->motion->type = 7;    /* pos | ang | scl */
        mjp->actwkptr->motion->inp_fn = 3;
    } else {
        mjp->actwkptr->motion->type = 3;    /* pos | ang */
        mjp->actwkptr->motion->inp_fn = 2;
    }
}
