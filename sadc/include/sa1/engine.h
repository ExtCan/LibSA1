#ifndef SA1_ENGINE_H
#define SA1_ENGINE_H

/*
 * Engine API used by the player and character code: task system and heap,
 * event work (taskwk.ewp), angle and matrix helpers, motion drawing, game
 * state, frame-step globals, libc/libm wrappers, sound triggers, shadows.
 *
 * Prototypes use the address symbols of config/symbols.1st_read.txt; the
 * descriptive names are aliases (#define, both targets) taken from SADX
 * analogies and the code map (docs/research/sa1_code_map.md 5, 6). When a
 * symbol is renamed in config/symbols.1st_read.txt, delete its alias here.
 * Argument types follow matched code where it already calls the function
 * (src/1st_read), otherwise the registers seen at the call sites
 * ("R4-R6", "FR4" ...; n = call sites in 1ST_READ).
 */

#include "task.h"
#include "sa1/ninja_types.h"

/* ---- task system + game heap (8C098440-8C098EE0) ---- */

/* im bits: 1 = motionwk (CAlloc 1 x 0x40), 2 = taskwk (1 x 0x40),
 * 4 = force work (2 x 0x1C), 8 = any work (1 x 0x10) */
task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec);        /* R4-R6 -> R0 n=240; im is unsigned short in the original (MOV.W R4,@R15 / EXTU.W at 8C098538); canonical since 2026-10-20 */
task *CreateChildTask(Uint16 im, TaskFuncPtr exec, task *parent);         /* R4-R6 -> R0 n=43; copies parent pos/ang */
void DestroyTask(task *tp);                                           /* R4 n=87 */
void FreeTask(task *tp);                                           /* R4 n=200 (6 bytes) */
void *MAlloc(Sint32 size);                                       /* R4 -> R0 n=68 */
void *CAlloc(Sint32 n, Sint32 size);                             /* R4,R5 -> R0 n=57 (zeroed) */
void MFree(void *p);                                            /* R4 n=96 */
void func_8C098A10(void);                                               /* heap init n=8 */

/*
 * Heap block header (MAlloc 8C098A80, MFree 8C098B42, heap init 8C098A10,
 * 8C098C2C): 0x10 bytes in front of every block; the user pointer is
 * header + 1. next/prev link the free list (both NULL while allocated,
 * MAlloc 8C098AF0); nextphys/prevphys are the neighbouring blocks in memory
 * (block size = nextphys - blk; the arena-end header has nextphys NULL).
 * Canonical name MEMBLK: sa1/pending.h.
 */
typedef struct sa1_MEMBLK {
    struct sa1_MEMBLK *next;    /* 0x00 free list */
    struct sa1_MEMBLK *prev;    /* 0x04 free list */
    struct sa1_MEMBLK *nextphys; /* 0x08 following block in memory */
    struct sa1_MEMBLK *prevphys; /* 0x0C preceding block in memory */
} sa1_MEMBLK;                   /* 0x10 */

extern Uint8 B_8C7EE8E0[];      /* game heap arena: 0x90000 bytes + the end header (8C098A10) */
extern Sint32 B_8C79F040;       /* cleared by the heap init 8C098A10 */
extern task *B_8C79F044;        /* task pool (0x1000 tasks of 0x30, 8C09844A) */
extern task *B_8C79F048;        /* free task list */
extern task *B_8C79F04C[8];     /* task level lists (walkers map level 8 to 7: code_8C09878C.c) */
extern TaskFuncPtr D_8C1C1D40;  /* exec currently running (set before the call, 0 after: 8C09878C) */

/*
 * Child task description (func_8C098D06; table D_8C1B9778: exec 8C05AA26,
 * im 6, flag 0..4). The list ends at exec == NULL. Canonical name
 * CHILDTASKINFO: sa1/pending.h.
 */
typedef struct sa1_CHILDTASKINFO {
    TaskFuncPtr exec;           /* 0x00 */
    Sint16 im;                  /* 0x04 CreateElementalTask im (MOV.W) */
    Sint16 flag;                /* 0x06 -> twp->flag */
    Angle3 ang;                 /* 0x08 added to the parent's twp->ang */
    NJS_POINT3 pos;             /* 0x14 offset in the parent's frame (njCalcVector) */
    TaskWkUnion counter;        /* 0x20 -> twp->counter */
} sa1_CHILDTASKINFO;            /* 0x24 */

void func_8C098440(task *tp);                                           /* default exec (stored as a TaskFuncPtr by 8C0986C4) */
void func_8C098E60(task *tp);
void func_8C098E98(task *tp);                                           /* run the children of tp (DestroyTask-aware walk) */

/*
 * Event / look-at / face work (taskwk.ewp, EWP(twp)). Allocated by 8C04B440
 * (CAlloc(1, 0x7C), mode = 3) from PInitialize, freed by 8C04B468.
 * Field names of the original header are kept where src uses them
 * (f34, list40, ...); the comments give the meaning.
 *  - 0x28 is a union in the original: Angle ang[3] for locks 5/6
 *    (8C0AFE72 reads ang[1]) and an NJS_POINT3 for locks 2-4 (8C06EAC0,
 *    8C06EFCE, 8C07C1B4 copy it with MOV.L/FMOV.S): use EWP_POS(ewp).
 *  - 0x5C..0x6B (face block) must be reached through a pointer to it for
 *    matching (8C04BC20..8C04BD60 do ADD #92 then small displacements):
 *    sa1_facewk / EWP_FACE in sa1/pending.h. The flat fields stay for the
 *    PC paths of code_8C04BC70.c, code_8C04B7BA.c, code_8C07B080.c.
 */
typedef struct eventwk {
    Uint8 mode;                 /* 0x00 3 after 8C04B440; bits read as unsigned (EXTU.B) by 8C04BD9E/8C04B7BA: 1 = snap pos.y to the ground, 2 = copy the ground tilt to ang.x/z */
    Uint8 pad01[3];
    Uint8 look_mode;            /* 0x04 (unsigned: MOV.B+EXTU.B at 8C0B2AF2, 8C0BE3E8, 8C0C8C86, 8C0DDC06) look-at: 0 off, 1 angle, 2 position, 3 task + offset (written 8C07C376/8C07C34C/8C07C318/8C07C2E2; 8C0B28EE tests it) */
    Uint8 pad05[3];
    Angle3 look_ang;            /* 0x08 look_mode 1 (8C07C34C; 8C0B28EE reads y/z) */
    NJS_POINT3 look_pos;        /* 0x14 look_mode 2: position; 3: offset from look_tp (8C07C318, 8C07C2E2) */
    struct task *look_tp;       /* 0x20 look_mode 3 (8C07C2E2; 8C0B28EE reads its twp) */
    Uint8 lock;                 /* 0x24 event lock: 1 idle/arrived, 2 go to point, 3 move along vector, 4 push by vector, 5/6 turn (8C0AFE72), other: release (8C06EAC0, 8C06EFCE); read MOV.B + EXTU.B; GetAnalog 8C06CD22 tests != 0 */
    Uint8 pad25[3];
    Angle ang[3];               /* 0x28 locks 5/6: angles ([1]: 8C0AFE72/8C0BBD3C/8C0C5B3A); locks 2-4: NJS_POINT3 target (EWP_POS) */
    Float f34;                  /* 0x34 lock 2 target speed, 0 = p.run_speed (8C06EBAC); cleared by 8C07C1B4 */
    Float f38;                  /* 0x38 lock 2 accel limit, 0 = p.run_accel (8C06EBBC); cleared by 8C07C1B4 */
    Float f3C;                  /* 0x3C current action frame (8C04B48C, 8C04B57C, 8C0DA4F4) */
    void *list40;               /* 0x40 action queue head, EV_ACTION_LIST * (CAlloc(1, 0x18) by 8C04B4D2/8C04B528; freed by 8C04B48C; displays 8C0AE17A, 8C0BA820) */
    void *buf44;                /* 0x44 previous EV_ACTION_LIST entry while it is blended out (8C04B57C; MFree'd after linkframe frames) */
    Float f48;                  /* 0x48 current path frame (8C04B82E, 8C04B9E0) */
    void *list4C;               /* 0x4C path queue head, EV_PATH_LIST * (next at +0x0C, freed by 8C04B82E); non-NULL forces twp->mode 80 (8C0AFF1A) */
    NJS_POINT3 v50;             /* 0x50 path base position (8C04B9E0) */
    Uint8 face;                 /* 0x5C face block (sa1_facewk): face/mouth state, 8C04BCC8 */
    Uint8 face_next;            /* 0x5D */
    Sint16 face_time;           /* 0x5E */
    Sint16 face_count;          /* 0x60 */
    Uint8 pad62[2];
    const char *face_script;    /* 0x64 read char by char by 8C04BC20 */
    const char *face_seq;       /* 0x68 2-byte steps {time, face}, table D_8C19C32C (8C04BC70) */
    Uint8 pad6C[0x0C];          /* 0x6C NJS_POINT3 model offset: njTranslateV(NULL, ewp + 0x6C) by 8C04BD9E */
    Float f78;                  /* 0x78 shadow size: 8C04BD9E draws a shadow (8C0922EE) when > 0 */
} eventwk;                      /* 0x7C */

/* Face block of eventwk (0x5C..0x6B), the type 8C04BC20/8C04BC70/
 * 8C04BCC8/8C04BD60 address through ewp + 92. Canonical name facewk:
 * sa1/pending.h. */
typedef struct sa1_facewk {
    Uint8 face;                 /* 0x00 = eventwk 0x5C */
    Uint8 face_next;            /* 0x01 */
    Sint16 face_time;           /* 0x02 */
    Sint16 face_count;          /* 0x04 */
    Uint8 pad06[2];
    const char *face_script;    /* 0x08 = eventwk 0x64 */
    const char *face_seq;       /* 0x0C = eventwk 0x68 */
} sa1_facewk;                   /* 0x10 */

/*
 * Event action queue entry (eventwk.list40/buf44; CAlloc(1, 0x18) by
 * 8C04B4D2/8C04B528, played by 8C04B57C). Canonical name EV_ACTION_LIST:
 * sa1/pending.h.
 */
typedef struct sa1_EV_ACTION_LIST {
    Uint8 mode;                 /* 0x00 bit 0: loop (wrap) when it is the last entry (MOV.B + EXTU.B + TST #1) */
    Uint8 linkframe;            /* 0x01 blend frames from the previous action (EXTU.B, FLOAT) */
    Uint8 pad02[2];
    NJS_ACTION action;          /* 0x04 struct copy of the caller's action; &entry->action goes to 8C040942 */
    NJS_TEXLIST *texlist;       /* 0x0C NULL: keep the current texlist (njSetTexture) */
    struct sa1_EV_ACTION_LIST *next; /* 0x10 */
    Float speed;                /* 0x14 frames per step */
} sa1_EV_ACTION_LIST;           /* 0x18 */

/*
 * Event path: queue entry (eventwk.list4C, CAlloc'd by 8C04B7BA), path
 * header and point (game data in the event overlays, e.g. ev0029
 * D_0CB866A4 = {0x60, D_0CB860A4}; the ripper converts them on PC).
 * Canonical names EV_PATH_LIST / EV_PATH / EV_PATH_POINT: sa1/pending.h.
 */
typedef struct {
    Sint8 ang[3];               /* 0x00 x, y, z in 1/256 turns (value << 8 = Angle) */
    Sint8 pad03;
    NJS_POINT3 pos;             /* 0x04 */
} sa1_EV_PATH_POINT;            /* 0x10 */

typedef struct {
    Sint32 num;                 /* 0x00 number of points */
    sa1_EV_PATH_POINT *points;  /* 0x04 */
} sa1_EV_PATH;                  /* 0x08 */

typedef struct sa1_EV_PATH_LIST {
    Uint8 mode;                 /* 0x00 bit 0: loop when it is the last entry; bit 1: path relative to the task position */
    Uint8 pad01[3];
    sa1_EV_PATH *path;          /* 0x04 */
    Float speed;                /* 0x08 points per step */
    struct sa1_EV_PATH_LIST *next; /* 0x0C */
} sa1_EV_PATH_LIST;             /* 0x10 */

SA1_ASSERT_SIZE(sa1_MEMBLK, 0x10);
SA1_ASSERT_OFFSET(sa1_MEMBLK, nextphys, 0x08);
SA1_ASSERT_OFFSET(sa1_MEMBLK, prevphys, 0x0C);
SA1_ASSERT_SIZE(sa1_CHILDTASKINFO, 0x24);
SA1_ASSERT_OFFSET(sa1_CHILDTASKINFO, im, 0x04);
SA1_ASSERT_OFFSET(sa1_CHILDTASKINFO, flag, 0x06);
SA1_ASSERT_OFFSET(sa1_CHILDTASKINFO, ang, 0x08);
SA1_ASSERT_OFFSET(sa1_CHILDTASKINFO, pos, 0x14);
SA1_ASSERT_OFFSET(sa1_CHILDTASKINFO, counter, 0x20);
SA1_ASSERT_SIZE(eventwk, 0x7C);
SA1_ASSERT_OFFSET(eventwk, look_mode, 0x04);
SA1_ASSERT_OFFSET(eventwk, look_ang, 0x08);
SA1_ASSERT_OFFSET(eventwk, look_pos, 0x14);
SA1_ASSERT_OFFSET(eventwk, look_tp, 0x20);
SA1_ASSERT_OFFSET(eventwk, lock, 0x24);
SA1_ASSERT_OFFSET(eventwk, ang, 0x28);
SA1_ASSERT_OFFSET(eventwk, f34, 0x34);
SA1_ASSERT_OFFSET(eventwk, f3C, 0x3C);
SA1_ASSERT_OFFSET(eventwk, list40, 0x40);
SA1_ASSERT_OFFSET(eventwk, buf44, 0x44);
SA1_ASSERT_OFFSET(eventwk, f48, 0x48);
SA1_ASSERT_OFFSET(eventwk, list4C, 0x4C);
SA1_ASSERT_OFFSET(eventwk, v50, 0x50);
SA1_ASSERT_OFFSET(eventwk, face, 0x5C);
SA1_ASSERT_OFFSET(eventwk, face_time, 0x5E);
SA1_ASSERT_OFFSET(eventwk, face_count, 0x60);
SA1_ASSERT_OFFSET(eventwk, face_script, 0x64);
SA1_ASSERT_OFFSET(eventwk, face_seq, 0x68);
SA1_ASSERT_OFFSET(eventwk, pad6C, 0x6C);
SA1_ASSERT_OFFSET(eventwk, f78, 0x78);
SA1_ASSERT_SIZE(sa1_facewk, 0x10);
SA1_ASSERT_OFFSET(sa1_facewk, face_time, 0x02);
SA1_ASSERT_OFFSET(sa1_facewk, face_count, 0x04);
SA1_ASSERT_OFFSET(sa1_facewk, face_script, 0x08);
SA1_ASSERT_OFFSET(sa1_facewk, face_seq, 0x0C);
SA1_ASSERT_SIZE(sa1_EV_ACTION_LIST, 0x18);
SA1_ASSERT_OFFSET(sa1_EV_ACTION_LIST, linkframe, 0x01);
SA1_ASSERT_OFFSET(sa1_EV_ACTION_LIST, action, 0x04);
SA1_ASSERT_OFFSET(sa1_EV_ACTION_LIST, texlist, 0x0C);
SA1_ASSERT_OFFSET(sa1_EV_ACTION_LIST, next, 0x10);
SA1_ASSERT_OFFSET(sa1_EV_ACTION_LIST, speed, 0x14);
SA1_ASSERT_SIZE(sa1_EV_PATH_POINT, 0x10);
SA1_ASSERT_OFFSET(sa1_EV_PATH_POINT, pos, 0x04);
SA1_ASSERT_SIZE(sa1_EV_PATH, 0x08);
SA1_ASSERT_OFFSET(sa1_EV_PATH, points, 0x04);
SA1_ASSERT_SIZE(sa1_EV_PATH_LIST, 0x10);
SA1_ASSERT_OFFSET(sa1_EV_PATH_LIST, path, 0x04);
SA1_ASSERT_OFFSET(sa1_EV_PATH_LIST, speed, 0x08);
SA1_ASSERT_OFFSET(sa1_EV_PATH_LIST, next, 0x0C);

#define EWP(twp) ((eventwk *)(twp)->ewp)
#define EWP_POS(ewp) (*(NJS_POINT3 *)(ewp)->ang)

extern const char *D_8C19C32C[];    /* face sequences: {Sint8 time, Sint8 face} pairs ending with time 0 ([0]/[1] idle); PL_FACE_STEP in sa1/player.h */

void func_8C04B440(taskwk *twp);                                        /* allocate twp->ewp n=3 (PInitialize) */
void func_8C04B468(taskwk *twp);                                        /* free twp->ewp (task destructor 8C098998) */
void func_8C04B48C(taskwk *twp);                                        /* free the action queue */
void func_8C04B4D2(taskwk *twp, NJS_ACTION *action, NJS_TEXLIST *texlist, Float speed, Sint32 mode, Sint32 linkframe); /* queue an action */
void func_8C04B528(taskwk *twp, NJS_OBJECT *object, NJS_MOTION *motion, NJS_TEXLIST *texlist, Float speed, Sint32 mode, Sint32 linkframe);
void func_8C04B57C(taskwk *twp);                                        /* play the action queue */
void func_8C04B82E(taskwk *twp);                                        /* free the path queue */
void func_8C04B9E0(taskwk *twp);                                        /* follow the path queue */
Sint32 func_8C04BC20(taskwk *twp);
void func_8C04BC70(taskwk *twp, Sint32 n);                              /* start face sequence D_8C19C32C[n] */
void func_8C04BCC8(taskwk *twp);                                        /* face update */
void func_8C04BD60(taskwk *twp);
void func_8C04BD9E(task *tp);                                           /* event model display */
void func_8C04BF00(Sint32 pno, NJS_OBJECT *object, NJS_MOTION *motion, Float frame); /* event welds (pno must be 32-bit: MOV.L R4,@R15) */
void func_8C0922EE(Angle3 *ang, NJS_POINT3 *pos, Float r);              /* shadow draw (float last for matching: SHC evaluates arguments right to left) */

/* ---- angle / vector utilities (8C055D20-8C057460) ---- */
Angle AdjustAngle(Angle ang, Angle target, Angle spd);                /* R4-R6 -> R0 n=86 */
Sint32 SubAngle(Sint32 a, Sint32 b);                               /* R4,R5 -> R0 n=53 (16-bit result) */
Sint32 DiffAngle(Sint32 a, Sint32 b);                               /* R4,R5 -> R0 n=76 (16-bit result) */
Angle VectorAngle(void *a, void *b, void *c);                         /* R4-R6 -> R0 n=38: angle between NJS_VECTOR a and b (c: optional plane normal); the arguments go straight to njInnerProduct/njOuterProduct (code_8C055D20.c defines it with void *) */
Angle func_8C055D70(Angle ang, Angle target);                           /* R4,R5 -> R0 (code_8C055D20.c) */
Sint32 func_8C055E40(taskwk *twp);                                      /* R4 -> R0 Bool (code_8C055D20.c) */
/* ---- matrices / node matrices / motion ---- */
Sint32 func_8C057B40(void);                                             /* njPushUnitMatrix: 1, or 0 when the matrix stack is full (n=21; callers ignore R0) */
#define njPushUnitMatrix func_8C057B40
/*
 * Node matrices of an action at a frame (welds, PJoinVertexes 8C07A55C,
 * event welds 8C04BF00): R4 = base matrix (njSetMatrix(NULL, base) when not
 * NULL), R5 = action, FR4 = frame, R6 = node (B_8C78B44C = min(node + 1,
 * 0x80); the result is B_8C789448[node]), R7 = out (njSetMatrix(out, ...)
 * when not NULL). The float must be declared third: SHC spills parameters in
 * declaration order (code_8C058100.c, code_8C0572D8.c).
 */
Sint32 func_8C0583A0(NJS_MATRIX_PTR base, NJS_ACTION *action, Float frame, Sint32 node, NJS_MATRIX_PTR out);
Sint32 func_8C058434(Sint32 node, NJS_MATRIX_PTR m);                    /* R4,R5 n=21: njSetMatrix(m, &B_8C789448[node]); 0 when node >= B_8C78B448 */
Uint32 func_8C05842E(void);                                             /* returns B_8C78B448 */
extern NJS_MATRIX B_8C789448[128];  /* node matrices built by 8C0583A0 (bss .RES.B 8192) */
extern Uint32 B_8C78B448;           /* nodes evaluated (compared CMP/HS) */
extern Uint32 B_8C78B44C;           /* node limit: min(node + 1, 0x80) */

/* Node motion evaluation context (built on the stack by 8C0583A0 at
 * 8C0583DE, walked by 8C057BC0; never game data) */
typedef struct {
    void *mdata;                /* 0x00 NJS_MDATA2/3 of the current node (NJS_MOTION.mdata; += 0x18 per node at 8C058096) */
    NJS_MATRIX *mtx;            /* 0x04 output matrix of the current node (njGetMatrix target, += 0x40 at 8C0580A6) */
    Uint32 type;                /* 0x08 NJS_MOTION.type (MOV.W + EXTU.W) */
    Uint32 nbFrame;             /* 0x0C NJS_MOTION.nbFrame */
    Float frame;                /* 0x10 */
} NODE_MTN_EVAL;                /* 0x14 */

/*
 * Paths (SADX pathtag / pathtbl / pathinfo; path code 8C0895xx-8C089Axx).
 * A player on a path keeps the PATHTAG * in twp->value.ptr with
 * TWF_ONPATH (8C06F530, 8C07BE68, 8C07C120). PATHTAG/PATHTBL are game data
 * (pointers: converted by the ripper on PC); PATHINFO is a stack result
 * (8C06F530 at sp+0x3C, 8C078308 at sp+0x1C), no pointers. Layouts from
 * code_8C08966E.c (matched; also code_8C089AA0.c PATH_TAG/PATH_TBL).
 * Canonical names PATHTAG / PATHTBL / PATHINFO: sa1/pending.h.
 */
typedef struct {
    Sint16 slangx;              /* 0x00 */
    Sint16 slangz;              /* 0x02 */
    Float length;               /* 0x04 */
    Float xpos;                 /* 0x08 */
    Float ypos;                 /* 0x0C */
    Float zpos;                 /* 0x10 */
} sa1_PATHTBL;                  /* 0x14 */

typedef struct sa1_pathtag {
    Uint8 slattr;               /* 0x00 */
    Uint8 unk1;                 /* 0x01 */
    Uint16 points;              /* 0x02 MOV.W + EXTU.W (8C078308 compares with 1) */
    Float totallen;             /* 0x04 */
    sa1_PATHTBL *tblhead;       /* 0x08 */
    void *pathctrl;             /* 0x0C */
} sa1_PATHTAG;                  /* 0x10 */

typedef struct {
    Angle slangx;               /* 0x00 */
    Angle slangz;               /* 0x04 */
    Angle angx;                 /* 0x08 */
    Angle angz;                 /* 0x0C */
    Float onpathpos;            /* 0x10 */
    Float xpos;                 /* 0x14 */
    Float ypos;                 /* 0x18 */
    Float zpos;                 /* 0x1C */
    Float xpos2;                /* 0x20 */
    Float ypos2;                /* 0x24 */
    Float zpos2;                /* 0x28 */
    NJS_VECTOR normal;          /* 0x2C */
    NJS_VECTOR normala;         /* 0x38 */
    NJS_VECTOR front;           /* 0x44 */
} sa1_PATHINFO;                 /* 0x50 */

SA1_ASSERT_SIZE(sa1_PATHTBL, 0x14);
SA1_ASSERT_OFFSET(sa1_PATHTBL, length, 0x04);
SA1_ASSERT_OFFSET(sa1_PATHTBL, xpos, 0x08);
SA1_ASSERT_SIZE(sa1_PATHTAG, 0x10);
SA1_ASSERT_OFFSET(sa1_PATHTAG, points, 0x02);
SA1_ASSERT_OFFSET(sa1_PATHTAG, totallen, 0x04);
SA1_ASSERT_OFFSET(sa1_PATHTAG, tblhead, 0x08);
SA1_ASSERT_OFFSET(sa1_PATHTAG, pathctrl, 0x0C);
SA1_ASSERT_SIZE(sa1_PATHINFO, 0x50);
SA1_ASSERT_OFFSET(sa1_PATHINFO, onpathpos, 0x10);
SA1_ASSERT_OFFSET(sa1_PATHINFO, xpos2, 0x20);
SA1_ASSERT_OFFSET(sa1_PATHINFO, normal, 0x2C);
SA1_ASSERT_OFFSET(sa1_PATHINFO, front, 0x44);

/* Object draw list entry (func_8C057460; the list ends at obj == NULL).
 * Canonical name OBJECT_DRAW_FUNC: sa1/pending.h. */
typedef struct {
    NJS_OBJECT *obj;            /* 0x00 node of the drawn tree */
    void (*func)(NJS_OBJECT *obj); /* 0x04 NULL: node not drawn */
} sa1_OBJECT_DRAW_FUNC;         /* 0x08 */

/* Sprite digit layout of the number display 8C057780 (8C057934 copies the
 * whole block with MOV.L pairs). No pointers. */
typedef struct {
    Float x, y;                 /* 0x00 screen position of the first digit */
    Float sx, sy;               /* 0x08 sprite scale (NJS_SPRITE.sx/sy) */
    Float dx, dy;               /* 0x10 step between two digits */
} NUMLAYOUT;                    /* 0x18 */

/* Number display request of func_8C08EE98 (code_8C07BAC0.c NUMDISP,
 * code_8C08ABC0.c UNK_NUMDISP). Canonical name NUMDISP: sa1/pending.h. */
typedef struct {
    Uint8 flags;                /* 0x00 */
    Uint8 unk1;                 /* 0x01 */
    Uint16 unk2;                /* 0x02 */
    Uint32 max;                 /* 0x04 */
    Uint32 value;               /* 0x08 */
    Float x;                    /* 0x0C */
    Float y;                    /* 0x10 */
    Float unk14;                /* 0x14 */
    Float scale;                /* 0x18 */
} sa1_NUMDISP;                  /* 0x1C */

SA1_ASSERT_SIZE(NODE_MTN_EVAL, 0x14);
SA1_ASSERT_OFFSET(NODE_MTN_EVAL, mtx, 0x04);
SA1_ASSERT_OFFSET(NODE_MTN_EVAL, nbFrame, 0x0C);
SA1_ASSERT_OFFSET(NODE_MTN_EVAL, frame, 0x10);
SA1_ASSERT_SIZE(sa1_OBJECT_DRAW_FUNC, 0x08);
SA1_ASSERT_OFFSET(sa1_OBJECT_DRAW_FUNC, func, 0x04);
SA1_ASSERT_SIZE(NUMLAYOUT, 0x18);
SA1_ASSERT_OFFSET(NUMLAYOUT, dx, 0x10);
SA1_ASSERT_SIZE(sa1_NUMDISP, 0x1C);
SA1_ASSERT_OFFSET(sa1_NUMDISP, value, 0x08);
SA1_ASSERT_OFFSET(sa1_NUMDISP, x, 0x0C);
SA1_ASSERT_OFFSET(sa1_NUMDISP, scale, 0x18);

void func_8C040942(NJS_ACTION *action, Float frame);                    /* draw action at frame, n=38 */
void func_8C036F12(NJS_MODEL *model);                                   /* model draw wrapper, n=80 */
void func_8C03700C(NJS_MODEL *model);                                   /* hand-written basic model draw; returns early when B_8C754EC8 != 0 */
void func_8C037098(NJS_MODEL *model);                                   /* hand-written basic model draw (reads nbPoint at +8) */
Sint32 func_8C03FDC0(NJS_MOTION *motion, Float frame);                  /* node motion setup (MOTION_CTX B_8C88FE40) */
Sint32 func_8C03FE1A(NJS_MOTION *motion, Float frame);                  /* shape motion setup (B_8C88FE5C) */
void func_8C03FE60(NJS_OBJECT *object);
void func_8C040880(NJS_OBJECT *object);
void func_8C0409C0(NJS_OBJECT *object, NJS_MOTION *motion, Float frame);
void func_8C0409EE(NJS_OBJECT *object, NJS_MOTION *motion, NJS_MOTION *shape, Float frame);
void func_8C041C92(NJS_ACTION_LINK *action, Float rate);                /* link (blend) motion draw (code_8C04196C.c) */
void func_8C055ECE(NJS_MODEL *a, NJS_MODEL *b, NJS_MODEL *dst, Float t); /* model morph (code_8C055ECE.c) */
void func_8C055EFE(Float t, NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *dst, Sint32 n); /* point array blend (code_8C055ECE.c) */
void func_8C05646C(NJS_MODEL *model);                                   /* code_8C05646C.c */
Sint32 func_8C056712(NJS_OBJECT *root, NJS_OBJECT *target);            /* node index of target in root (PJoinVertexes, 8C018E40) */
Float func_8C056784(Float a, Float b);                                  /* floorf-based helper (code_8C05646C.c) */
Sint32 func_8C0567B0(NJS_OBJECT *object, NJS_OBJECT **objs);           /* flatten the tree, returns the node count */
void func_8C057A20(Angle3 *ang, NJS_POINT3 *pos, Float len);
void func_8C057AE0(NJS_VECTOR *a, NJS_VECTOR *b, NJS_VECTOR *out);
Float func_8C057B18(NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *c);       /* dot(b - a, c - a) */
Sint32 func_8C057682(NJS_POINT3 *p1, NJS_POINT3 *p2, Float r1, Float r2);
Sint32 func_8C0576CC(Sint32 a, Sint32 b);
Sint32 func_8C0576DA(Sint32 a, Sint32 b);
Sint32 func_8C0576F4(Sint32 ang, Sint32 target, Sint32 spd);
void func_8C043A20(Float a, Float r, Float g, Float b);                 /* njSetConstantMaterial wrapper (code_8C043A20.c) */
void func_8C043A40(Float x1, Float y1, Float z1, Float x2, Float y2, Float z2); /* debug line (code_8C043A20.c) */
void func_8C038CEC(Sint32 bank);                                        /* palette bank select: GBR+88 = D_8C038F60 + bank * 0x800 */
Sint32 func_8C038D00(Float x, Float y, Float z, Float r);               /* visibility test FR4-FR7 -> R0 */
Float func_8C02CF68(Float x, Float y, Float z);                         /* x*x + y*y + z*z (FIPR) */

/*
 * Draw flag words (code_8C056860.c): B_8C88F660/B_8C88F664 are OR-set by
 * func_8C05694A and cleared by func_8C05695C; func_8C056926 saves them to
 * B_8C788C38/B_8C788C3C and func_8C056938 restores them. B_8C88F62C and
 * its copy B_8C788C40 are not declared here: code_8C056860.c (Sint32
 * scalars) and code_8C05697C.c (Uint32, B_8C788C40 as a 1-element array)
 * only match with their own spellings.
 */
extern Uint32 B_8C88F660;
extern Uint32 B_8C88F664;
extern Uint32 B_8C788C38;
extern Uint32 B_8C788C3C;
void func_8C056926(void);
void func_8C056938(void);
void func_8C05694A(Uint32 a, Uint32 b);

/* ---- game state (8C04C680-8C04FEA0) ---- */
Uint16 GetStageNumber(void);                                             /* (CurrentLevel << 8) | CurrentAct, n=171; callers EXTU.W R0 (8C06CD22, 8C0730D8, 8C07BAC0, 8C0A77BC, 8C0AEFA0; older files declare it Uint16 locally) */
Sint32 GetPlayerNumber(void);                                             /* player/character number, n=85 */
Sint32 func_8C049A4E(void);                                             /* event running, n=43 */
Sint32 func_8C09068E(Sint32 flag);                                      /* event flag test (upgrades in PInitialize) n=12 */
Sint32 func_8C04FE40(void);                                             /* game mode (B_8C7493B4) */
Sint32 func_8C08A71E(void);                                             /* frame counter B_8C752BE8 (callers compare unsigned) */
Sint32 func_8C042DC4(task *tp);                                         /* RTS; MOV #0,R0 */
void func_8C0108EA(void);                                               /* switch on B_8C6AC374.mode (code_8C010400.c) */

extern Sint16 B_8C7493B4;       /* game mode (8C04FE40) */
extern Sint16 B_8C7493BA;       /* CurrentLevel */
extern Sint16 B_8C7493BC;       /* CurrentAct */
extern Sint16 B_8C7493C8;       /* CurrentCharacter (loader 8C04F3C0 reads the low byte) */
#define CurrentLevel     B_8C7493BA
#define CurrentAct       B_8C7493BC
#define CurrentCharacter B_8C7493C8

/*
 * Fixed 60 Hz logic: func_8C04E95A runs func_8C04E714 B_8C754EC4 (+ catch-up)
 * times per displayed frame; B_8C754EC8 is the step index (about 150
 * functions gate display-rate work on it). libsa1 sets 1 / 0 every tick.
 */
extern Sint32 B_8C754EC4;       /* logic steps this frame (written by 8C051760) */
extern Sint32 B_8C754EC8;       /* current step index */
extern Sint32 B_8C754C04;       /* 50 Hz/PAL-like mode flag read by 8C04E95A */

/* gravity (player physics 8C06D254.., set by 8C052A8A/8C052AA8) */
extern NJS_VECTOR B_8C754BDC;
extern Angle B_8C754BE8;        /* user gravity angle x */
extern Angle B_8C754BEC;        /* user gravity angle z */
#define Gravity B_8C754BDC

/* camera taskwk (pos +0x20, ang.x/y/z read by the player code: 8C055D20,
 * 8C0700C2 (smode != 0 mirrors the steering), 8C071700, 8C06DCA2) */
extern taskwk *D_8C18B7EC;
#define camera_twp D_8C18B7EC

/* frame timer (TMU1, 8C06C0C0/8C06C1D8) and the sprite-task debug display */
extern Uint32 B_8C78C600;       /* TCOR1 reload value 0xBEBC20 (written by 8C06C0C0) */
extern Uint32 B_8C78C604;       /* last TCNT1 sample (8C06C1D8) */
extern Sint32 B_8C78C5E0[8];    /* signed (CMP/PL in 8C06C240) */
extern Sint32 tcrf_SprTaskDisplayMode; /* 8C1BDFD8 */
extern Sint32 tcrf_LevelSelectVar;
extern Sint16 D_8C19B468;               /* MOV.W (8C07BAC0, 8C07BE68, 8C0869E0) */
extern Sint32 B_8C752BE8;               /* frame counter returned by func_8C08A71E */
extern Sint32 D_8C03F760[20];
extern NJS_TEXLIST D_8C1BF19C;          /* number display digits (8C057780) */
extern NJS_TEXANIM D_8C19EF64[];        /* cells 0-9 digits, 10 separator */
extern const NJS_VECTOR D_8C112AFC;     /* {0, 1, 0} */
extern const NJS_VECTOR D_8C112B08;     /* {1, 0, 0} */
extern NJS_POINT3COL D_8C19F0A0[];      /* debug axis gizmo {D_8C19F040, D_8C19F088, NULL, 6}; must be an array (code_8C058100.c) */
extern NJS_POINT3COL D_8C196034;        /* land collision debug triangle: func_8C63D66C(&D_8C196034, 3, 0x20) (8C029B00) */

/* ---- libc / libm / runtime (8C10C10C-8C111194) ---- */
Sint32 rand(void);                                             /* rand(): LCG *0x41C64E6D + 0x3039, 0..0x7FFF; n=215 */
Float atan2f(Float y, Float x);                                  /* atan2f-like FR4,FR5 -> FR0 n=165 */
Float func_8C10D058(Float x);                                           /* asinf-like n=50 (BAMS = asin(n) * 10430.381f: 8C056A04, 8C02B620) */
Float func_8C10D008(Float x);                                           /* acosf n=22 */
Float func_8C10D0A8(Float y, Float x);                                  /* atan2-like wrapper of core 8C10E6A0 (FR4 = y, FR5 = x -> FR0); verified in the simulator */
Float func_8C10D148(Float x);                                           /* libm wrapper n=6 */
Float func_8C10D198(Float x);                                           /* libm wrapper n=7 */
#if defined(TARGET_PC)
void *memset(void *dst, int c, size_t n);                        /* the C library one on PC */
#else
void *memset(void *dst, Sint32 c, Uint32 n);                     /* memset n=89 */
#endif
/* __divls/__divlu/__modls/__modlu (8C10CA5C..) and __quick_mvn (8C10CDDC)
 * are emitted by SHC itself; see SA1_DIV in common.h. */

/* ---- sound triggers (8C092FA0-8C095860); PC: event callback to the host ---- */
void func_8C0940DA(Sint32 se, Sint32 a, Sint32 b, Sint32 c);            /* play SE (SADX dsPlay_oneshot analogue) n=237 */
void func_8C09413A(Sint32 se, Sint32 a, Sint32 b, Sint32 c, Float x, Float y, Float z); /* positional SE n=59 */
void func_8C094330(Sint32 se, taskwk *twp, Sint32 a, Sint32 b, Sint32 c); /* 5th argument on the stack (code_8C0B11C0.c) */
void func_8C0945A0(Sint32 se);
void func_8C0945D0(taskwk *twp);
void func_8C098FAE(Sint32 n);
void func_8C098FFC(Sint32 n);

/* ---- movable objects (8C0586A0-8C058E48, code_8C0586A0.c; SADX ObjectMovable* names for orientation only) ---- */
Sint32 func_8C0586A0(taskwk *twp, motionwk *mwp, Sint32 n);            /* setup by lift class n 4..7: mwp->rad / weight, twp->flag |= 0x240 / 0x640 / 0x640 / 0x840, twp->id = n; -> 1, or 0 without mwp or for another n */
void func_8C05872A(task *tp);                                           /* the movable-object step: gravity, friction, land collision (8C056A04), CCL_Entry unless held (flag 0x1000) */
void func_8C058A1A(task *tp);                                           /* the heavy variant of the step (O_SAMPLE_M / L) */
void func_8C058D20(task *tp);                                           /* the held-object step: CCL_Entry, then 8C058D3C */
void func_8C058D3C(taskwk *twp, motionwk *mwp);                         /* twp->flag &= ~0x38, mwp->acc = 0 (mwp may be NULL) */
void func_8C058D5A(taskwk *twp, NJS_VECTOR *v);                         /* v rotated by the inverse of twp->ang (-Y, -X, -Z), in place */
void func_8C058DD4(taskwk *twp, NJS_VECTOR *v);                         /* v rotated by twp->ang (Z, X, Y), in place */

/* ---- shadows / SET / misc ---- */
Float func_8C091C20(Float x, Float y, Float z, Angle3 *ang);            /* GetShadowPos-like: ground height below point, n=18 */
Sint32 func_8C0919CC(task *tp);                                         /* SET manager query, n=85 */
void func_8C043B60(taskwk *twp, Sint16 mask);                           /* debug tint (code_8C043A20.c): when player 0's character bit (1 << 8C07CE40(0)) is in mask, the constant material grey 0.5 if 8C07CD16(&twp->pos, 20.0f), else 0 */
#define GetShadowPos func_8C091C20

#endif