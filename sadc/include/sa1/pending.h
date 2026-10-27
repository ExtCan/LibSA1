#ifndef SA1_PENDING_H
#define SA1_PENDING_H

/*
 * Canonical names of shared types that unmigrated src files still define
 * locally (docs/header_migration.md).
 *
 * The shared definitions live in the core headers under the name
 * sa1_<name> (so they are in every translation unit and in the
 * "tools/pc_check.py --layout" table); this header maps <name> to them and
 * declares the globals and functions whose types are such names. It is NOT
 * included by sa1/sa1.h: a src file includes it after it has deleted ALL of
 * its local copies of the names below (otherwise SHC reports 2114/2115
 * "Multiple ... declarations"). When no src file defines a name any more,
 * its typedef moves into the core header (renaming sa1_<name> back to
 * <name>) and its line here is deleted; at the end this header is folded
 * into sa1/sa1.h.
 */

#include "sa1/sa1.h"

/* ---- task.h ---- */
typedef sa1_forcewk forcewk;
typedef sa1_anywk anywk;
typedef sa1_OBJ_CONDITION OBJ_CONDITION;
#define FWP(tp) ((forcewk *)(tp)->fwp)          /* task.fwp is still void * */
#define AWP(tp) ((anywk *)(tp)->awp)

/* ---- sa1/ninja_types.h ---- */
typedef sa1_NJS_MKEY_F NJS_MKEY_F;
typedef sa1_NJS_MKEY_A NJS_MKEY_A;
typedef sa1_NJS_MKEY_P NJS_MKEY_P;
typedef sa1_NJS_MDATA1 NJS_MDATA1;
typedef sa1_NJS_MDATA2 NJS_MDATA2;
typedef sa1_NJS_MDATA3 NJS_MDATA3;
typedef sa1_NJS_MDATA4 NJS_MDATA4;

/* ---- sa1/engine.h ---- */
typedef sa1_MEMBLK MEMBLK;
extern MEMBLK *B_8C79F03C;                  /* heap free-list head */
void func_8C098C2C(Uint32 *total, Uint32 *largest);   /* heap statistics */

typedef sa1_CHILDTASKINFO CHILDTASKINFO;
task *func_8C098D06(CHILDTASKINFO *info, task *parent);

typedef sa1_facewk facewk;
#if SA1_PTR32
/* the face block of eventwk as matching code reaches it (ADD #92). Only
 * with the DC layout: on 64-bit hosts eventwk pads face_script differently,
 * so PC code uses the flat eventwk.face* fields until eventwk gets a nested
 * facewk member. */
#define EWP_FACE(ewp) ((facewk *)&(ewp)->face)
#endif

typedef sa1_EV_ACTION_LIST EV_ACTION_LIST;
typedef sa1_EV_PATH_POINT EV_PATH_POINT;
typedef sa1_EV_PATH EV_PATH;
typedef sa1_EV_PATH_LIST EV_PATH_LIST;

typedef sa1_PATHTBL PATHTBL;
typedef sa1_PATHTAG PATHTAG;
typedef sa1_PATHINFO PATHINFO;
Bool func_8C0897F8(PATHTAG *tag, PATHINFO *info);                       /* position on a path (8C06F530, 8C078308) */
Bool func_8C089A40(PATHTAG *tag, Uint32 n, Float *out);
Float func_8C089AA0(PATHTAG *path, NJS_POINT3 *pos, NJS_POINT3 *outPos, Float *outDist);
void func_8C07C120(Uint8 pno, PATHTAG *path, Sint32 point, Angle3 *ang); /* put a player on a path (value = path, TWF_ONPATH) */
Sint32 func_8C07C17A(PATHTAG *path);

typedef sa1_OBJECT_DRAW_FUNC OBJECT_DRAW_FUNC;
void func_8C057460(NJS_OBJECT *obj, OBJECT_DRAW_FUNC *list);

typedef sa1_NUMDISP NUMDISP;
void func_8C08EE98(NUMDISP *nd);                                       /* number display request */

NJS_OBJECT *func_8C057BC0(NJS_OBJECT *obj, NODE_MTN_EVAL *ctx);        /* evaluate one node, returns obj->sibling (0 past the node limit) */

/* ---- sa1/motion.h ---- */
typedef sa1_MOTION_CTX MOTION_CTX;          /* MOTION_EVAL in code_8C03FF90.c, code_8C040596.c */
typedef sa1_MOTION_FUNC MOTION_FUNC;
typedef sa1_ROTATE_FUNC ROTATE_FUNC;
extern MOTION_CTX B_8C88FE40;               /* node motion (code_8C0406A0.c: probably MOTION_CTX B_8C88FE40[2] with B_8C88FE5C = [1]) */
extern MOTION_CTX B_8C88FE5C;               /* shape motion */
extern MOTION_CTX B_8C88FEB8[2];            /* link motions: node */
extern MOTION_CTX B_8C88FEF0[2];            /* link motions: shape (B_8C88FF04 = &B_8C88FEF0[0].idx) */

/* ---- sa1/collision.h ---- */
typedef sa1_TOUCHINFO TOUCHINFO;            /* COLHIT (code_8C028100.c), CL_HITPOLY (code_8C02770E.c) */
typedef sa1_TOUCHPOLY TOUCHPOLY;
typedef sa1_colpoly colpoly;                /* COLPOLY, NEARPOLY, FDPOLY, LCPOLY, LANDPOLY */
typedef sa1_colobj colobj;                  /* NEARLIST, FDOBJ, LCOBJ, LANDPOLYLIST */
typedef sa1_colbucket colbucket;
typedef sa1_xssunit xssunit;
typedef sa1_zxsdwstr zxsdwstr;
typedef sa1_xyyzzxsdwstr xyyzzxsdwstr;
typedef sa1_MESHBUF MESHBUF;                /* STRIPBUF, CL_STRIPBUF */
typedef sa1_COLHITCTX COLHITCTX;            /* CL_HITLIST */
typedef sa1_LANDRAY LANDRAY;
typedef sa1_LINECOLLI LINECOLLI;
typedef sa1_DEATHZONE DEATHZONE;

extern TOUCHINFO B_8C6BACF0[16];            /* touch records (count csts.find_count) */
extern TOUCHPOLY B_8C73E404[16];            /* TOUCH-poly buffer (count B_8C73E400) */
extern colpoly *D_8C02D540;                 /* free polygons (data word in the code section after 8C02D528) */
extern colobj *D_8C02D544;                  /* free objects */
extern colobj *D_8C02D548;                  /* objects in use */
extern colbucket D_8C02D560[128];           /* z buckets */
extern colpoly *B_8C73E280[96];             /* NEAR-poly buffer (count B_8C73E27C) */
extern colobj B_8C6BB27C[192];              /* OBJECT buf (0xC0 * 64 = 0x3000, ends at B_8C6BE27C) */
extern colpoly B_8C6BE27C[];                /* RANGE-poly buf: 8C028E60 initialises 0x2000 entries, which would overrun symbol B_8C6C398C at +0x5710: check the split before sizing it */
extern colpoly *B_8C6BB270;                 /* polygon cursor (8C028620) */
extern Sint32 B_8C6BB274;                   /* its index */
extern DEATHZONE **DeathZoneTable[];        /* 8C1BE404 [level][act] -> zone list */

Sint32 func_8C029400(xyyzzxsdwstr *xyzp, NJS_OBJECT *object, Uint32 attr); /* six axis probes (attr overwritten before use) */
Sint32 func_8C02AD22(zxsdwstr *zssp, Float r);                          /* ground/ceiling under zssp->pos (JSR from 8C0BD8B2) */
Sint32 func_8C02BC60(zxsdwstr *zssp, NJS_OBJECT *obj, Uint32 attr);    /* attr masked, >> 13 indexes a float table */
Bool func_8C0273A2(MESHBUF *mb);
Bool func_8C0274B8(MESHBUF *mb);
Bool func_8C0275E0(MESHBUF *mb, NJS_OBJECT *obj);
void func_8C02770E(csts *cp, NJS_OBJECT *obj, COLHITCTX *ctx);
void func_8C028100(csts *cp, COLHITCTX *ctx);
NJS_OBJECT *func_8C028280(csts *cp, NJS_OBJECT *obj, COLHITCTX *ctx);   /* returns obj->sibling */
NJS_OBJECT *func_8C028B00(NJS_OBJECT *obj, Uint32 attr, colobj *op);   /* code_8C028620.c defines it with void *grp */
NJS_OBJECT *func_8C02B1A0(LANDRAY *ray, NJS_OBJECT *obj);
void func_8C02CE80(colpoly *buf, Sint32 n);                             /* build the polygon free list (hand-written) */
void func_8C02CECA(colobj *buf, Sint32 n);                              /* build the object free list, clears D_8C02D548 */
colobj *func_8C02CEF4(void);                                            /* take a free object, link it in use (NULL: none) */
void func_8C02CF20(colobj *op);                                         /* unlink, back to the free list */
void func_8C02CF48(colobj *op);
void func_8C02CFA4(void);                                               /* clear the z buckets */
colpoly *func_8C02CFC0(Float z);                                        /* take a free polygon into the bucket of z */
void func_8C02D00E(colobj *op);                                         /* chain the buckets into op->polys/last */

/* ---- sa1/ccl.h, sa1/player.h ---- */
typedef sa1_PL_TARGET PL_TARGET;            /* SONIC_TARGET in code_8C0B16BC.c */
extern PL_TARGET B_8C73EE38[];              /* targets for player 0 (1161 entries) */
extern PL_TARGET B_8C742288[];              /* targets for the other players */

typedef sa1_PL_FACE PL_FACE;                /* FACEWK in code_8C0B2338.c */
typedef sa1_PL_FACE_STEP PL_FACE_STEP;
#define PFP(pwp) ((PL_FACE *)(pwp)->pfp)    /* playerwk.pfp is still struct pl_face * */
void func_8C07B080(NJS_OBJECT *obj, PL_FACE *fp, Sint16 *idx);

typedef sa1_PL_SPDUPWK PL_SPDUPWK;
typedef sa1_DROWNWK DROWNWK;
typedef sa1_PL_LOOKTIMEWK PL_LOOKTIMEWK;
typedef sa1_SPDLIMITWK SPDLIMITWK;          /* SONIC_8C0B2EA0_AWP in code_8C0B2D40.c */
typedef sa1_lookwk lookwk;                  /* LOOKWK in code_8C0B2338.c */
void func_8C07AEF8(lookwk *lwp);
void func_8C07AF8C(lookwk *lwp);
typedef sa1_BUBBLE BUBBLE;
typedef sa1_BUBBLE_PARAM BUBBLE_PARAM;      /* BUBBLEPARAM in code_8C0B37F2.c, EMITPRM_8C0B3480 in code_8C0B33A0.c */
extern BUBBLE_PARAM D_8C1C63A4[];           /* 3 per character */
BUBBLE *func_8C0B33A0(taskwk *twp);         /* MAlloc a bubble and link it at twp->value.ptr (PTCL_8C0B33A0 in code_8C0B33A0.c) */
BUBBLE *func_8C0B37F2(taskwk *twp);         /* spawn */

#endif
