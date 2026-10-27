#ifndef SA1_MOTION_H
#define SA1_MOTION_H

/*
 * Player motion and weld data (action lists, weld lists, motion work).
 * The PL_ACTION and PL_JOIN_VERTEX tables are game DATA in 1ST_READ
 * (SonicActionList 8C1C55A0, SonicWeldList 8C1C5FA4, ...): referenced via
 * extern only; on the PC target the runtime ripper builds them. NOTE
 * PJoinVertexes (8C07A55C) writes org/srcdepth/dstdepth back into the weld
 * table, so every character instance needs its own copy (critique 4.2).
 */

#include "ninja.h"
#include "sa1/ninja_types.h"

/*
 * Action list entry (character action tables, 16 bytes).
 * Verified on SonicActionList: entry 0 = {8C1FE1A4, 73, 3, 0, 0, 0.0625, 0.5}.
 * next is read as a byte (PSetMotion 8C0795C2: MOV.B @(6,R0) + EXTU.B), so
 * it is Uint8 in SA1 DC (SADX uses Uint16).
 */
typedef struct {
    NJS_ACTION *actptr;         /* 0x00 */
    Uint8 objnum;               /* 0x04 number of nodes (73 for Sonic) */
    Sint8 mtnmode;              /* 0x05 -> mtnjvwk.mtnmode (PSetMotion 8C0795E8) */
    Uint8 next;                 /* 0x06 -> mtnjvwk.nextaction (PSetMotion 8C0795C2) */
    Uint8 pad07;                /* 0x07 0 in every Sonic entry checked */
    Float frame;                /* 0x08 link (blend) speed */
    Float racio;                /* 0x0C playback speed ratio */
} PL_ACTION;                    /* 0x10 */

/*
 * Weld list entry (0x18). Verified on SonicWeldList entry 0:
 * {8C1CE76C, 8C1C9F60, 8C1CA0E8, 4, 2, 0, 0, NULL, 8C1C5F24}.
 * PJoinVertexes 8C07A55C (first use, mtnjvwk.jvmode == 0): for inpmode 1-3
 * MAllocs numVertex * 0x30 bytes (src and dst point + normal per vertex) into
 * org and fills srcdepth/dstdepth; for 4-9 (hand/foot/user positions) sets
 * org = NULL. These writes go into the shared table.
 */
typedef struct {
    NJS_OBJECT *objptr;         /* 0x00 root of the character tree */
    NJS_OBJECT *srcobj;         /* 0x04 */
    NJS_OBJECT *dstobj;         /* 0x08 */
    Uint8 numVertex;            /* 0x0C */
    Sint8 inpmode;              /* 0x0D weld type 1-9: switch is MOV.B + CMP/EQ with no EXTU.B (PJoinVertexes 8C07A5AC, 8C04C10E); SonicMain rewrites it (2/0) for Super Sonic (8C0A77BC) */
    Uint8 srcdepth;             /* 0x0E written by 8C07A55C */
    Uint8 dstdepth;             /* 0x0F written by 8C07A55C */
    NJS_POINT3 *org;            /* 0x10 written by 8C07A55C (MAlloc): 4 NJS_POINT3 per vertex pair (src point, src normal, dst point, dst normal; 8C04BF00) */
    Uint16 *pnum;               /* 0x14 vertex index pairs (16 bytes for 4 vertices) */
} PL_JOIN_VERTEX;               /* 0x18 */

/*
 * Motion work (playerwk.mj at +0x120). Set up by PInitialize 8C06C350
 * (mtnmode = jvmode = 0, spdp = &pwp->spd, workp = &pwp->work), driven by
 * PSetMotion 8C079476 (15-mode switch on mtnmode).
 */
typedef struct mtnjvwk {
    Sint16 mtnmode;             /* 0x00 (pwp 0x120) */
    Sint16 jvmode;              /* 0x02 (0x122) 0 = welds not set up, PJoinVertexes */
    Uint16 reqaction;           /* 0x04 (0x124) written by the character code (114 functions) */
    Uint16 action;              /* 0x06 (0x126) */
    Uint16 lastaction;          /* 0x08 (0x128) = action on entry to PSetMotion */
    Uint16 nextaction;          /* 0x0A (0x12A) from PL_ACTION.next; mode 8 stores reqaction = (Uint8)nextaction (8C07995A) */
    Uint16 acttimer;            /* 0x0C (0x12C) reset when a new reqaction starts, else incremented */
    Sint16 flag;                /* 0x0E (0x12E) cleared every call; bit 0 = action ended or chained (8C079A16, 8C079A40, 8C079B8E) */
    Float nframe;               /* 0x10 (0x130) current frame (44 readers) */
    Float start_frame;          /* 0x14 (0x134) used by the loop modes (PInitMotionFrame) */
    Float *spdp;                /* 0x18 (0x138) = &pwp->spd.x; speed multiplier of modes 9-11 */
    Float *workp;               /* 0x1C (0x13C) = &pwp->work.f; speed multiplier of modes 9-11 */
    PL_ACTION *plactptr;        /* 0x20 (0x140) character action list (set in each main's case 0) */
    PL_JOIN_VERTEX *pljvptr;    /* 0x24 (0x144) character weld list */
    NJS_ACTION *actwkptr;       /* 0x28 (0x148) link action built by PSetMotion mode 0 (MAlloc(8) + NJS_MOTION MAlloc(12), mdata CAlloc(111, 0x18)); freed by modes 13/14 */
} mtnjvwk;                      /* 0x2C */

SA1_ASSERT_SIZE(PL_ACTION, 0x10);
SA1_ASSERT_OFFSET(PL_ACTION, next, 0x06);
SA1_ASSERT_OFFSET(PL_ACTION, frame, 0x08);
SA1_ASSERT_SIZE(PL_JOIN_VERTEX, 0x18);
SA1_ASSERT_OFFSET(PL_JOIN_VERTEX, numVertex, 0x0C);
SA1_ASSERT_OFFSET(PL_JOIN_VERTEX, inpmode, 0x0D);
SA1_ASSERT_OFFSET(PL_ACTION, mtnmode, 0x05);
SA1_ASSERT_OFFSET(PL_ACTION, racio, 0x0C);
SA1_ASSERT_OFFSET(mtnjvwk, lastaction, 0x08);
SA1_ASSERT_OFFSET(mtnjvwk, acttimer, 0x0C);
SA1_ASSERT_OFFSET(mtnjvwk, flag, 0x0E);
SA1_ASSERT_OFFSET(mtnjvwk, workp, 0x1C);
SA1_ASSERT_OFFSET(PL_JOIN_VERTEX, org, 0x10);
SA1_ASSERT_OFFSET(PL_JOIN_VERTEX, pnum, 0x14);
SA1_ASSERT_SIZE(mtnjvwk, 0x2C);
SA1_ASSERT_OFFSET(mtnjvwk, reqaction, 0x04);
SA1_ASSERT_OFFSET(mtnjvwk, nframe, 0x10);
SA1_ASSERT_OFFSET(mtnjvwk, spdp, 0x18);
SA1_ASSERT_OFFSET(mtnjvwk, plactptr, 0x20);
SA1_ASSERT_OFFSET(mtnjvwk, actwkptr, 0x28);

/*
 * Weld list entry of the event/NPC models (func_8C0572D8; data e.g.
 * D_8C4BEFA0 passed by 8C0DE8C0). The list ends at num == 0. 8C057302 reads
 * @R14 .W, @(2) .B, @(3) .B, @(4) .L with stride 8.
 */
typedef struct {
    Sint16 num;                 /* 0x00 number of (src, dst) vertex index pairs */
    Sint8 src_node;             /* 0x02 node index (func_8C0567B0 order) of the source object */
    Sint8 dst_node;             /* 0x03 node index of the destination object */
    Sint16 *index;              /* 0x04 num pairs: src vertex, dst vertex */
} NODE_WELD;                    /* 0x08 */

/*
 * Motion playback context of the hand-written motion evaluator
 * (8C03FDC0-8C041CA8): B_8C88FE40 (node motion), B_8C88FE5C = B_8C88FE40 +
 * 0x1C (shape motion), B_8C88FEB8[2]/B_8C88FEF0[2] (link motions, node and
 * shape). The splitter gives frame (+0x0C: B_8C88FE4C, B_8C88FE68) and idx
 * (+0x14: B_8C88FE70, B_8C88FF04) their own symbols; write them as members.
 * Evidence: the key-channel handlers store/read @(4), @(8), @(20)
 * (8C0405D2, 8C040612, 8C04124E, 8C0402D0); 8C03FDC0/8C03FE1A set mdata,
 * frame, nbFrame and clear idx/unk18; 8C040DF0 indexes p/nb with idx << 2.
 * Canonical name MOTION_CTX (MOTION_EVAL in two files): sa1/pending.h.
 */
typedef struct {
    void *mdata;                /* 0x00 current NJS_MDATAn entry (advanced per node) */
    void **p;                   /* 0x04 key lists of the current entry */
    Uint32 *nb;                 /* 0x08 key counts of the current entry */
    Float frame;                /* 0x0C */
    Uint32 nbFrame;             /* 0x10 */
    Uint32 idx;                 /* 0x14 next key channel (pos/ang/scl) of the entry; Sint32 in code_8C03FF90.c/code_8C040596.c */
    Sint32 unk18;               /* 0x18 cleared on setup, use unknown */
} sa1_MOTION_CTX;               /* 0x1C */

/* Node/shape step and channel handlers of the evaluator (every one takes
 * the object in R4). Canonical names MOTION_FUNC / ROTATE_FUNC:
 * sa1/pending.h (function pointer types: the externs below are compatible
 * with the local MOTION_FUNC declarations). */
typedef void (*sa1_MOTION_FUNC)(NJS_OBJECT *object);
typedef void (*sa1_ROTATE_FUNC)(NJS_MATRIX_PTR m, Angle angx, Angle angy, Angle angz);

extern sa1_MOTION_FUNC B_8C88FE24;  /* node dispatcher (8C03FE60: D_8C19ACE0[B_8C749100]) */
extern sa1_MOTION_FUNC B_8C88FE28;  /* shape handler (8C03FDC0: D_8C19ACF4[idx]) */
extern sa1_MOTION_FUNC B_8C88FE2C;  /* translate */
extern sa1_MOTION_FUNC B_8C88FE30;  /* rotate */
extern sa1_MOTION_FUNC B_8C88FE34;  /* scale */
extern sa1_MOTION_FUNC B_8C890074;  /* optional per-node callback (8C0408EA) */
extern sa1_MOTION_FUNC B_8C88FE80[2]; /* link motions: node step per motion */
extern sa1_MOTION_FUNC B_8C88FE88[2]; /* link motions: shape step per motion */
extern sa1_MOTION_FUNC B_8C88FE98[2]; /* link: translate */
extern sa1_MOTION_FUNC B_8C88FEA0[2]; /* link: rotate */
extern sa1_MOTION_FUNC B_8C88FEA8[2]; /* link: scale */
extern sa1_MOTION_FUNC D_8C19ACE0[]; /* data: node handlers (5) */
extern sa1_MOTION_FUNC D_8C19ACF4[]; /* data: shape handlers (4) */
extern sa1_MOTION_FUNC D_8C19AD20[]; /* data: link node handlers (5) */
extern sa1_MOTION_FUNC D_8C19AD34[]; /* data: link shape handlers (4) */
extern sa1_ROTATE_FUNC D_8C19AD44;  /* rotation function (njRotateXYZ/njRotateZXY), initially 0 */
extern Uint16 B_8C749100;           /* node inp_fn & 0xFF3F */
extern Uint16 B_8C749102;           /* node motion type */
extern Uint16 B_8C749104;           /* shape channel mode 1/2/3 (MOV.W + EXTU.W) */
extern Uint16 B_8C749106;           /* shape motion type */
extern Uint16 B_8C88FF28[2];        /* link: inp_fn & 0xFF3F */
extern Uint16 B_8C88FF2C[2];        /* link: type */
extern Uint16 B_8C88FF30[2];        /* link: shape handler index / mode 1/2/3 */
extern Uint16 B_8C88FF34[2];        /* link: shape type */
extern Uint16 B_8C88FF38;           /* link motion being evaluated (read as a byte for the context index) */
extern Float B_8C88FF3C;            /* link blend rate (8C041C92/8C041D68/8C041DC6) */
extern Uint16 B_8C88FF40;           /* link: translation keys written */
extern Uint16 B_8C88FF42;           /* link: rotation keys written */
extern Uint16 B_8C88FF44;           /* link: scale keys written */
extern Uint16 B_8C88FF46;           /* link: vertex keys present (CMP/PL after EXTU.W) */
extern Uint16 B_8C88FF48;           /* link: normal keys present */
extern NJS_POINT3 B_8C88FF4C[2];    /* link: position per motion */
extern NJS_POINT3 B_8C88FF7C[2];    /* link: scale per motion */
extern Angle3 B_8C88FFAC[2];        /* link: rotation per motion */
extern NJS_POINT3 *B_8C88FFFC[2];   /* link: vertex arrays (bss is 16 bytes, only [0]/[1] used) */
extern NJS_VECTOR *B_8C89000C[2];   /* link: normal arrays (bss is 16 bytes) */
extern NJS_POINT3 *B_8C89002C;      /* shape scratch buffer: nbPoint vertices then nbPoint normals */
extern Uint16 B_8C89002A;           /* (inp_fn & 0xC0) != 0 */
extern NJS_MATRIX_PTR B_8C88FE58;   /* matrix for NJD_EVAL_SKIP nodes (also read by the library, 8C60D0DA) */
extern NJS_MATRIX B_8C7492B0;       /* event welds: source node matrix (8C04BF00) */
extern NJS_MATRIX B_8C7492F0;       /* destination node matrix */
extern NJS_MATRIX B_8C749330;       /* inverse of the source matrix */
extern NJS_MATRIX B_8C749370;       /* inverse of the destination matrix */

SA1_ASSERT_SIZE(NODE_WELD, 0x08);
SA1_ASSERT_OFFSET(NODE_WELD, src_node, 0x02);
SA1_ASSERT_OFFSET(NODE_WELD, dst_node, 0x03);
SA1_ASSERT_OFFSET(NODE_WELD, index, 0x04);
SA1_ASSERT_SIZE(sa1_MOTION_CTX, 0x1C);
SA1_ASSERT_OFFSET(sa1_MOTION_CTX, p, 0x04);
SA1_ASSERT_OFFSET(sa1_MOTION_CTX, nb, 0x08);
SA1_ASSERT_OFFSET(sa1_MOTION_CTX, frame, 0x0C);
SA1_ASSERT_OFFSET(sa1_MOTION_CTX, nbFrame, 0x10);
SA1_ASSERT_OFFSET(sa1_MOTION_CTX, idx, 0x14);
SA1_ASSERT_OFFSET(sa1_MOTION_CTX, unk18, 0x18);

/* PSetMotion (8C079476, 13 callers: the character mains and 8C0700C2) */
void PSetMotion(mtnjvwk *mjp);
void func_8C079C20(mtnjvwk *mjp);   /* motion link interpolation (BSR from PSetMotion with R4 = mjp: 8C0795AA, 8C07995E) */
void func_8C07A49C(mtnjvwk *mjp);   /* motion link free (BSR with R4 = mjp: 8C0795EC, 8C0799AC, 8C079B04) */
/* Character action and weld lists (data; extern only) */
extern PL_ACTION SonicActionList[];         /* 8C1C55A0, 142 entries */
extern PL_JOIN_VERTEX SonicWeldList[];      /* 8C1C5FA4 */
extern PL_ACTION TailsActionList[];         /* 8C256D6C, 132 entries */
extern PL_JOIN_VERTEX TailsWeldList[];      /* 8C2576EC */
extern PL_ACTION KnucklesActionList[];      /* 8C3528CC, 111 entries */
extern PL_JOIN_VERTEX KnucklesWeldList[];   /* 8C353114 */
extern PL_ACTION GammaActionList[];         /* 8C4225D4 */
extern PL_JOIN_VERTEX GammaWeldList[];      /* 8C422BE8 */
extern PL_ACTION AmyActionList[];           /* 8C4BB1D0 */
extern PL_JOIN_VERTEX AmyWeldList[];        /* 8C4BB8B4 */
extern PL_ACTION BigActionList[];           /* 8C525564 */
extern PL_JOIN_VERTEX BigWeldList[];        /* 8C525C90 */
extern PL_ACTION EggmanActionList[];        /* 8C57008C */
extern PL_JOIN_VERTEX EggmanWeldList[];     /* 8C5702BC */

#endif