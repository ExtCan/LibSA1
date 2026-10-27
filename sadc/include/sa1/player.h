#ifndef SA1_PLAYER_H
#define SA1_PLAYER_H

/*
 * Player work and the shared player code (player common 8C06C0C0-8C07DEC0,
 * character mains 8C0A77A0-8C0EEAA0). Every offset below was observed in the
 * binary (register-tracked loads/stores relative to the playerwk pointer
 * taken from motionwk.work or from playerpwp[]); the function after each
 * field is one that uses it. Names follow SADX (same order and sizes);
 * fields marked "not seen" are unverified.
 *
 * PORTABILITY: playerwk holds pointers (cstsp, htp, ttp, pfp, lclop,
 * island_list, mj.*, free.ptr). PInitialize allocates it with the literal
 * CAlloc(1, 0x200); decompiled C must write CAlloc(1, sizeof(playerwk))
 * (identical on SHC, asserted below).
 *
 * Types named sa1_<name> (PL_FACE, PL_TARGET, the helper task works,
 * lookwk, BUBBLE, ...) get their canonical name from sa1/pending.h
 * (docs/header_migration.md).
 */

#include "task.h"
#include "sa1/motion.h"
#include "sa1/collision.h"

/* character ids (taskwk.counter.b[1], index into the physics table) */
#define SA1_CHR_SONIC    0
#define SA1_CHR_EGGMAN   1
#define SA1_CHR_TAILS    2
#define SA1_CHR_KNUCKLES 3
#define SA1_CHR_TIKAL    4
#define SA1_CHR_AMY      5
#define SA1_CHR_E102     6
#define SA1_CHR_BIG      7

/*
 * Physics parameters (0x84). Table of 8 at 8C1BDFE4 indexed by character id;
 * PInitialize copies entry [id] to pwp->p. Values in the comments are
 * Sonic's (read from the table). Readers: offsets relative to pwp (+0x9C).
 */
typedef struct player_parameter {
    Sint32 jump2_timer;         /* 0x00 60 */
    Float pos_error;            /* 0x04 2.0   (pwp 0xA0: 8C074CF0, 8C078856; used as the gravity strength by 8C07506E) */
    Float lim_h_spd;            /* 0x08 16.0  (0xA4: 8C0723E0) */
    Float lim_v_spd;            /* 0x0C 16.0  (0xA8: 8C0723E0, 8C07D7B4) */
    Float max_x_spd;            /* 0x10 3.0   (0xAC: 8C06D254, 8C06EAC0, 8C0700C2, 8C0711D2; saved/restored by the speed tasks 8C07D2A2, 8C0B2EA0) */
    Float max_psh_spd;          /* 0x14 0.6   (0xB0: 8C07266E) */
    Float jmp_y_spd;            /* 0x18 1.66  (0xB4: mains 8C0A77BC, 8C0A966C, 19 fns) */
    Float nocon_speed;          /* 0x1C 3.0   (0xB8: mains) */
    Float slide_speed;          /* 0x20 0.23  (0xBC: mode machines) */
    Float jog_speed;            /* 0x24 0.46  (0xC0: 42 fns) */
    Float run_speed;            /* 0x28 1.39  (0xC4: 48 fns) */
    Float rush_speed;           /* 0x2C 2.3   (0xC8) */
    Float crash_speed;          /* 0x30 3.7   (0xCC: PSetPosition 8C072B44) */
    Float dash_speed;           /* 0x34 5.09  (0xD0) */
    Float jmp_addit;            /* 0x38 0.076 (0xD4: mode machines) */
    Float run_accel;            /* 0x3C 0.05  (0xD8: 8C06D254) */
    Float air_accel;            /* 0x40 0.031 (0xDC: air acceleration 8C06E904) */
    Float slow_down;            /* 0x44 -0.06 (0xE0) */
    Float run_break;            /* 0x48 -0.18 (0xE4: braking scale with mwp->weight in 8C070EDA, PGetSpeed 8C072174) */
    Float air_break;            /* 0x4C -0.17 (0xE8: 8C06E904) */
    Float air_resist_air;       /* 0x50 -0.028 (0xEC) */
    Float air_resist;           /* 0x54 -0.008 (0xF0) */
    Float air_resist_y;         /* 0x58 -0.01 (0xF4) */
    Float air_resist_z;         /* 0x5C -0.4  (0xF8) */
    Float grd_frict;            /* 0x60 -0.1  (0xFC) */
    Float grd_frict_z;          /* 0x64 -0.6  (0x100) */
    Float lim_frict;            /* 0x68 -0.2825 (0x104) */
    Float rat_bound;            /* 0x6C 0.3   (0x108, not seen) */
    Float rad;                  /* 0x70 4.0   (0x10C: PInitialize, mains) */
    Float height;               /* 0x74 10.0  (0x110: 8C0730D8, 22 fns) */
    Float weight;               /* 0x78 0.08  (0x114: 8C06D254, 21 fns) */
    Float eyes_height;          /* 0x7C 7.0   (0x118: 8C06C6F8) */
    Float center_height;        /* 0x80 5.4   (0x11C: 8C05E7D4, 8C0CFACE) */
} player_parameter;             /* 0x84 */

/* Land position list entry (playerwk.island_list, CAlloc(15, 0x20) by
 * 8C07D67C; 8C07D6E0 adds entries, 8C07D7B4 steers towards them) */
typedef struct {
    Float x, y, z;              /* 0x00 */
    Float r;                    /* 0x0C */
    Float d;                    /* 0x10 3D distance minus r (8C07D6E0) */
    Float h;                    /* 0x14 horizontal distance player-spot, njSqrt(dx*dx + dz*dz) (written and read by 8C07D7B4) */
    Angle angx;                 /* 0x18 SubAngle(twp->ang.y, yaw toward the spot) */
    Angle angz;                 /* 0x1C absolute yaw toward the spot, atan2f(dz, dx) * 10430.381f */
} PL_LANDPOSI;                  /* 0x20 */

/* Shadow work (playerwk.shadow; character shadow 8C0925A0 reads/writes
 * 0x00-0x1C, blob shadow 8C09247E reads scl). The "top" query is also the
 * water probe of the player code. */
typedef struct {
    Angle angx;                 /* 0x00 (pwp 0x1DC) ground tilt (8C07061C reads angx/angz) */
    Angle angz;                 /* 0x04 */
    Uint32 Attr_top;            /* 0x08 (pwp 0x1E4) surface attributes above: PGetSpeed 8C072174 tests 0x400000/0x400002, 8C075214 tests water bit 2 and 0x400000 */
    Uint32 Attr_bottom;         /* 0x0C */
    Float y_top;                /* 0x10 (pwp 0x1EC) height of that surface: the water surface (8C06C6F8 compares it with the eye height; bubbles 8C0B37F2) */
    Float y_bottom;             /* 0x14 */
    Uint32 _Attr_top;           /* 0x18 (pwp 0x1F4) tested & 0x400002 (water) by 8C06C6F8, bit 2 by 8C0B37F2 */
    Uint32 _Attr_bottom;        /* 0x1C */
    Float scl;                  /* 0x20 (pwp 0x1FC) = p.rad * 0.2f (PInitialize literal 0x3E4CCCCC) */
} shadowwk;                     /* 0x24 */

/*
 * Face work (playerwk.pfp, 0x20 bytes: the face task 8C0B2338 CAllocs it,
 * keeps it in tp->mwp and stores it in pwp->pfp; exec 8C07B106 (chunk
 * 8C07B080) blends the face shape motion; 8C07B2B8 starts a sequence).
 * Every byte is read with MOV.B and no EXTU.B (8C07B190, 8C07B19A,
 * 8C07B1F8): signed. The tag stays incomplete here because
 * code_8C07B080.c and code_8C07B2B8.c still complete struct pl_face
 * locally; the full layout is sa1_PL_FACE below (PL_FACE in sa1/pending.h).
 */
struct pl_face;

/* One step of a face sequence (D_8C19C32C[n]; a struct is required to
 * match: 8C07B080 reads time with ADD + @R0 and face with @(1,R0)). */
typedef struct {
    Sint8 time;                 /* 0x00 frames to blend to face; 0 ends the sequence */
    Sint8 face;                 /* 0x01 key index */
} sa1_PL_FACE_STEP;             /* 0x02 */

typedef struct {
    Sint8 face;                 /* 0x00 key index blended from */
    Sint8 face_next;            /* 0x01 key index blended to (= seq[step].face) */
    Sint8 unk02;                /* 0x02 not seen */
    Sint8 step;                 /* 0x03 current step of seq */
    Float rate;                 /* 0x04 blend ratio 0..1 */
    Float rate_spd;             /* 0x08 1.0f / seq[step].time */
    const sa1_PL_FACE_STEP *seq; /* 0x0C sequence (8C07B2B8: D_8C19C32C[n]); NULL = none */
    NJS_OBJECT *parent;         /* 0x10 node whose sibling (+0x30) is switched (8C0B2338: D_8C1CDABC) */
    NJS_OBJECT *sibling;        /* 0x14 original sibling, the static face (D_8C1CD82C) */
    NJS_OBJECT *object;         /* 0x18 morph face object (Sonic_EV_Head) */
    NJS_MOTION *motion;         /* 0x1C face shape motion (mdata = NJS_MDATA2[]) */
} sa1_PL_FACE;                  /* 0x20 */

typedef struct playerwk {
    TaskWkUnion work;           /* 0x000 Sonic 8C0A966C writes .f (28 fns); mj.workp = &work.f; Sonic: spin dash charge speed (8C0B11C0) */
    Sint16 equipment;           /* 0x004 upgrade bits from event flags (PInitialize), 29 readers; read MOV.W + TST with no EXTU.W (8C06F6C0, 8C071D40, 8C0A77BC, 8C0AEFA0, 8C0AFB10, 8C0B11C0, 8C0B3280): signed; 0x8000 = Super Sonic */
    Sint16 item;                /* 0x006 power-up bits (barriers etc.: 8C07D560, ring 8C0869E0); signed like equipment (8C07D3A0 item &= ~3 is MOV #-4/AND; 8C0A77BC) */
    Sint16 jumptimer;           /* 0x008 8C06CEFE, mains */
    Sint16 nocontimer;          /* 0x00A no-control timer: GetAnalog 8C06CD22, 8C0718C0 */
    Sint16 breathtimer;         /* 0x00C 8C06C6F8 */
    Sint16 waittimer;           /* 0x00E idle timer, stand selectors 8C0AE3D6.. (> 1800: idle action) */
    Sint16 flag;                /* 0x010 copy of twp->flag taken by 8C0730D8; & 3 tested by 8C074CF0, 8C0742D4, 8C076640 */
    Sint16 island_num;          /* 0x012 index into island_list (8C07D6E0, 8C07D7B4) */
    Sint32 path_point;          /* 0x014 path point index. NOTE: written as a Sint16 (MOV.W R0,@(20,pwp): 8C07C0E4, 8C07C120), 0x016 unused; kept Sint32 because existing field sizes are frozen: write *(Sint16 *)&pwp->path_point (code_8C07BE68.c) */
    Float hpos;                 /* 0x018 position along a path (8C078308, 8C0784F8, 8C078710) */
    Float dotp;                 /* 0x01C 8C06D254, 8C0714B6; written 8C079096 */
    NJS_POINT3 eff;             /* 0x020 written 8C06C6F8 */
    NJS_POINT3 acc;             /* 0x02C PGetAcceleration 8C06D254 family (12-byte struct copy at its end); PGetSpeed 8C072174 */
    NJS_POINT3 spd;             /* 0x038 player-space speed (~110 readers); mj.spdp = &spd.x */
    NJS_POINT3 wall_normal;     /* 0x044 written by PSetPosition 8C072B44, 8C072946, 8C075A40 (TOUCHINFO.normal2), 8C07266E (8C0344DC push-out) */
    NJS_POINT3 floor_normal;    /* 0x050 compared with gravity (8C06DCA2, 8C071B26, 8C0AE3D6); PGetSpeed passes it to 8C079000 */
    Uint32 attr;                /* 0x05C surface attributes under the player (22 readers): 0x4000 -> 8C06E4E0 acceleration (PGetAcceleration), 0x20000 -> slide floor 8C06DCA2, 0x800000 tested by 8C071A20 path, 4 = no friction (SADX ColFlags_NoFriction); 8C075214 ORs the land attr in */
    Uint32 last_attr;           /* 0x060 & 0xC (8C079096), & 0x4000 (8C0742D4, 8C075A40, 8C075F8C) */
    csts *cstsp;                /* 0x064 CAlloc(1, 0x84) by PInitialize (sa1/collision.h) */
    task *htp;                  /* 0x068 held task (->twp, ->mwp read by Sonic's code, 33 fns) */
    task *ttp;                  /* 0x06C task of the moving land object stood on (8C0730D8, 8C0777E0) */
    struct pl_face *pfp;        /* 0x070 written by 8C07B106 */
    NJS_OBJECT *lclop;          /* 0x074 land object stood on, compared with _OBJ_LANDCOLL.pObject (8C0730D8) */
    PL_LANDPOSI *island_list;   /* 0x078 8C07D7B4 */
    union {
        Sint8 b[32];
        Sint16 w[16];
        Sint32 l[8];
        Float f[8];
        void *ptr[8];           /* E-102 keeps a work pointer in ptr[0] (8C0CBE00, 8C0CFACE) */
    } free;                     /* 0x07C per-character scratch. Sonic: b[0] (0x7C) per-frame bit history (|= 1, then << 1 each frame), w[1] (0x7E) spin dash charge timer / counter while flag 0x100, w[2] (0x80) timer (10, cleared in mode 6), w[3] (0x82) timer (0/180/300), f[6]/f[7] (0x94/0x98) morph blend of parts A/B (8C0B2170); Tails 0x7C-0x90 floats; Amy bytes; E-102 ptr[0] */
    player_parameter p;         /* 0x09C */
    mtnjvwk mj;                 /* 0x120 */
    NJS_POINT3 righthand_pos;   /* 0x14C weld types 4-9 (PJoinVertexes 8C07A55C); read 8C0BD4CC, 8C0DC7DE */
    NJS_POINT3 lefthand_pos;    /* 0x158 */
    NJS_POINT3 rightfoot_pos;   /* 0x164 */
    NJS_POINT3 leftfoot_pos;    /* 0x170 */
    NJS_POINT3 user0_pos;       /* 0x17C Big's fishing 8C0E79D4 */
    NJS_POINT3 user1_pos;       /* 0x188 */
    NJS_POINT3 righthand_vec;   /* 0x194 8C0BD4CC */
    NJS_POINT3 lefthand_vec;    /* 0x1A0 */
    NJS_POINT3 rightfoot_vec;   /* 0x1AC */
    NJS_POINT3 leftfoot_vec;    /* 0x1B8 */
    NJS_POINT3 user0_vec;       /* 0x1C4 8C0E79D4 */
    NJS_POINT3 user1_vec;       /* 0x1D0 */
    shadowwk shadow;            /* 0x1DC */
} playerwk;                     /* 0x200 */

SA1_ASSERT_SIZE(player_parameter, 0x84);
SA1_ASSERT_OFFSET(player_parameter, pos_error, 0x04);
SA1_ASSERT_OFFSET(player_parameter, max_x_spd, 0x10);
SA1_ASSERT_OFFSET(player_parameter, jmp_y_spd, 0x18);
SA1_ASSERT_OFFSET(player_parameter, run_speed, 0x28);
SA1_ASSERT_OFFSET(player_parameter, dash_speed, 0x34);
SA1_ASSERT_OFFSET(player_parameter, air_accel, 0x40);
SA1_ASSERT_OFFSET(player_parameter, air_resist_air, 0x50);
SA1_ASSERT_OFFSET(player_parameter, grd_frict, 0x60);
SA1_ASSERT_OFFSET(player_parameter, lim_frict, 0x68);
SA1_ASSERT_OFFSET(player_parameter, height, 0x74);
SA1_ASSERT_OFFSET(player_parameter, eyes_height, 0x7C);
SA1_ASSERT_OFFSET(player_parameter, run_accel, 0x3C);
SA1_ASSERT_OFFSET(player_parameter, rad, 0x70);
SA1_ASSERT_OFFSET(player_parameter, center_height, 0x80);
SA1_ASSERT_SIZE(PL_LANDPOSI, 0x20);
SA1_ASSERT_OFFSET(PL_LANDPOSI, d, 0x10);
SA1_ASSERT_OFFSET(PL_LANDPOSI, h, 0x14);
SA1_ASSERT_OFFSET(PL_LANDPOSI, angx, 0x18);
SA1_ASSERT_SIZE(shadowwk, 0x24);
SA1_ASSERT_OFFSET(shadowwk, Attr_top, 0x08);
SA1_ASSERT_OFFSET(shadowwk, y_top, 0x10);
SA1_ASSERT_OFFSET(shadowwk, _Attr_top, 0x18);
SA1_ASSERT_SIZE(sa1_PL_FACE_STEP, 0x02);
SA1_ASSERT_SIZE(sa1_PL_FACE, 0x20);
SA1_ASSERT_OFFSET(sa1_PL_FACE, step, 0x03);
SA1_ASSERT_OFFSET(sa1_PL_FACE, rate, 0x04);
SA1_ASSERT_OFFSET(sa1_PL_FACE, seq, 0x0C);
SA1_ASSERT_OFFSET(sa1_PL_FACE, parent, 0x10);
SA1_ASSERT_OFFSET(sa1_PL_FACE, motion, 0x1C);
SA1_ASSERT_SIZE(playerwk, 0x200);
SA1_ASSERT_OFFSET(playerwk, equipment, 0x004);
SA1_ASSERT_OFFSET(playerwk, item, 0x006);
SA1_ASSERT_OFFSET(playerwk, jumptimer, 0x008);
SA1_ASSERT_OFFSET(playerwk, nocontimer, 0x00A);
SA1_ASSERT_OFFSET(playerwk, breathtimer, 0x00C);
SA1_ASSERT_OFFSET(playerwk, waittimer, 0x00E);
SA1_ASSERT_OFFSET(playerwk, flag, 0x010);
SA1_ASSERT_OFFSET(playerwk, island_num, 0x012);
SA1_ASSERT_OFFSET(playerwk, path_point, 0x014);
SA1_ASSERT_OFFSET(playerwk, hpos, 0x018);
SA1_ASSERT_OFFSET(playerwk, dotp, 0x01C);
SA1_ASSERT_OFFSET(playerwk, acc, 0x02C);
SA1_ASSERT_OFFSET(playerwk, wall_normal, 0x044);
SA1_ASSERT_OFFSET(playerwk, last_attr, 0x060);
SA1_ASSERT_OFFSET(playerwk, ttp, 0x06C);
SA1_ASSERT_OFFSET(playerwk, pfp, 0x070);
SA1_ASSERT_OFFSET(playerwk, p.pos_error, 0x0A0);
SA1_ASSERT_OFFSET(playerwk, p.max_x_spd, 0x0AC);
SA1_ASSERT_OFFSET(playerwk, p.run_speed, 0x0C4);
SA1_ASSERT_OFFSET(playerwk, p.run_accel, 0x0D8);
SA1_ASSERT_OFFSET(playerwk, p.lim_frict, 0x104);
SA1_ASSERT_OFFSET(playerwk, p.height, 0x110);
SA1_ASSERT_OFFSET(playerwk, p.eyes_height, 0x118);
SA1_ASSERT_OFFSET(playerwk, mj.action, 0x126);
SA1_ASSERT_OFFSET(playerwk, mj.workp, 0x13C);
SA1_ASSERT_OFFSET(playerwk, shadow.Attr_top, 0x1E4);
SA1_ASSERT_OFFSET(playerwk, shadow.y_top, 0x1EC);
SA1_ASSERT_OFFSET(playerwk, shadow._Attr_top, 0x1F4);
SA1_ASSERT_OFFSET(playerwk, eff, 0x020);
SA1_ASSERT_OFFSET(playerwk, spd, 0x038);
SA1_ASSERT_OFFSET(playerwk, floor_normal, 0x050);
SA1_ASSERT_OFFSET(playerwk, attr, 0x05C);
SA1_ASSERT_OFFSET(playerwk, cstsp, 0x064);
SA1_ASSERT_OFFSET(playerwk, htp, 0x068);
SA1_ASSERT_OFFSET(playerwk, lclop, 0x074);
SA1_ASSERT_OFFSET(playerwk, island_list, 0x078);
SA1_ASSERT_OFFSET(playerwk, free, 0x07C);
SA1_ASSERT_OFFSET(playerwk, p, 0x09C);
SA1_ASSERT_OFFSET(playerwk, p.rad, 0x10C);
SA1_ASSERT_OFFSET(playerwk, p.weight, 0x114);
SA1_ASSERT_OFFSET(playerwk, mj, 0x120);
SA1_ASSERT_OFFSET(playerwk, mj.reqaction, 0x124);
SA1_ASSERT_OFFSET(playerwk, mj.nframe, 0x130);
SA1_ASSERT_OFFSET(playerwk, mj.spdp, 0x138);
SA1_ASSERT_OFFSET(playerwk, mj.plactptr, 0x140);
SA1_ASSERT_OFFSET(playerwk, mj.pljvptr, 0x144);
SA1_ASSERT_OFFSET(playerwk, mj.actwkptr, 0x148);
SA1_ASSERT_OFFSET(playerwk, righthand_pos, 0x14C);
SA1_ASSERT_OFFSET(playerwk, user0_pos, 0x17C);
SA1_ASSERT_OFFSET(playerwk, righthand_vec, 0x194);
SA1_ASSERT_OFFSET(playerwk, user0_vec, 0x1C4);
SA1_ASSERT_OFFSET(playerwk, shadow, 0x1DC);
SA1_ASSERT_OFFSET(playerwk, shadow.scl, 0x1FC);

/* motionwk.work.pwp of a player task (what PInitialize stored) */
#define PWP(mwp) ((mwp)->work.pwp)

/* ---- player slots (8 each; PInitialize 8C06C350 fills slot pno) ---- */
extern taskwk *B_8C78C608[8];
extern motionwk *B_8C78C628[8];
extern task *B_8C78C648[8];
extern playerwk *B_8C78C668[8];
#define playertwp B_8C78C608
#define playermwp B_8C78C628
#define playertp  B_8C78C648
#define playerpwp B_8C78C668

/* Physics table: 8 entries from 8C1BDFE4. The base symbol carries Sonic's
 * label; index by character id. (config/symbols.1st_read.txt has per-entry
 * labels; build/1ST_READ.symbols.txt keeps the old wrong ones until the next
 * configure.py --split, so reference only the base.) */
extern player_parameter Sonic_Physics_Data[8];
#define PhysicsTable Sonic_Physics_Data

/* ---- player common (address symbols; aliases are SADX analogies) ---- */
void func_8C06C300(void);                                               /* clear player slots */
void PInitialize(Sint32 pno, task *tp);                               /* PInitialize (8 callers: the mains + 8C107020) */
void func_8C06C5CA(Sint32 pno);                                         /* player free: indexes the player slots with R4 (8C06C5D6 SHLL2); callers pass (Uint8)twp->counter.b[0] (8C0AE0FA) */
Sint32 func_8C06CD22(taskwk *twp, Angle *ang, Float *mag);              /* GetAnalog (57 callers) */
void PGetAcceleration(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* PGetAcceleration */
void func_8C06E904(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* air acceleration */
void func_8C071700(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* rotation from input */
void PGetSpeed(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* PGetSpeed */
void func_8C07256A(motionwk *mwp, playerwk *pwp);                       /* PClearSpeed: zero mwp->spd and pwp->spd (each may be NULL) */
Sint32 PSetPosition(taskwk *twp, motionwk *mwp, playerwk *pwp);        /* PSetPosition (status used by the mains) */
Sint32 func_8C0730D8(taskwk *twp, motionwk *mwp, playerwk *pwp);        /* PSetPosition core (land collision); status in R0 (PSetPosition 8C072FF8/8C0730AE keeps it) */
void func_8C078E40(taskwk *twp, NJS_POINT3 *v);                         /* PConvertVector_G2P */
void func_8C078EB0(taskwk *twp, NJS_POINT3 *v);                         /* PConvertVector_P2G */
void func_8C079096(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* global mwp->spd -> player-space pwp->spd with snapping; masks twp->ang and mwp->ang_aim to 16 bits; sets pwp->dotp (code_8C078E40.c) */
void PJoinVertexes(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* PJoinVertexes (welds) */
void SetInputP(Uint8 pno, Sint32 mode);                             /* SetInputP: twp->flag |= 0x1000, twp->smode = mode */
Sint32 func_8C07CE82(Uint8 pno, Sint32 n, NJS_POINT3 *pos, Angle3 *ang); /* GetPlayerPosition (history) */
Sint32 func_8C07CEF2(Uint8 pno, Sint32 frame, NJS_POINT3 *pos, Angle3 *ang); /* SetPlayerPosition: EXTU.B R4, R7 = NULL-checked Angle3 * copied into B_8C792690, returns 0/1 (code_8C07CE40.c); code_8C081D00.c/code_8C083E5C.c still use (Sint32, Sint32, NJS_POINT3 *, Sint32) locally */
task *func_8C07CFA0(Uint8 pno);                                         /* GetPlayerTaskPointer */
playerwk *func_8C07CFDE(Uint8 pno);                                     /* GetPlayerWorkPtr */
void func_8C07CB6A(Uint8 pno, Float x, Float y, Float z);               /* SetPositionP */
void func_8C07CB54(Uint8 pno, Float x, Float y, Float z);               /* SetAccelerationP: mwp->acc = (x, y, z) */
#define GetAnalog            func_8C06CD22
#define PClearSpeed          func_8C07256A
#define PConvertVector_G2P   func_8C078E40
#define PConvertVector_P2G   func_8C078EB0
#define GetPlayerPosition    func_8C07CE82
#define SetPlayerPosition    func_8C07CEF2
#define GetPlayerTaskPointer func_8C07CFA0
#define GetPlayerWorkPtr     func_8C07CFDE
#define SetPositionP         func_8C07CB6A
#define SetAccelerationP     func_8C07CB54

/*
 * More player common functions (registers at the call sites; the file that
 * defines each one in src/1st_read is the reference). Not declared here
 * because unmigrated files disagree on the signature (docs/
 * header_migration.md): func_8C06CFCC/func_8C06CF60 (defined with
 * (taskwk *), called with (taskwk *, motionwk *)), func_8C06CEBE ((taskwk *)
 * vs (taskwk *, playerwk *)), func_8C075F8C (4 vs 5 arguments),
 * func_8C07CE40 (Uint8 vs Sint32), func_8C07C9D2/func_8C07C580/
 * func_8C07C676 (Uint8 vs Sint8/Sint32 pno), func_8C0AFE72, func_8C0AF5A0,
 * func_8C07BA0A.
 */
void func_8C06C1D8(void);                                               /* frame timer sample (registered with func_8C64133E by 8C06C0C0) */
Sint32 func_8C06C32C(Sint32 n);                                         /* reads B_8C754C08[n].press */
void func_8C06C6A0(Uint8 pno);                                          /* position history push */
void func_8C06C6F8(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* per-frame player update (water, breath, eff) */
Sint32 func_8C06C7DE(taskwk *twp);                                      /* hit object id (EXTU.B) */
Sint32 func_8C06C880(taskwk *twp);                                      /* object class query: bitmask (0x400 tested by PSetPosition 8C072E9E) */
Sint32 func_8C06CB40(taskwk *twp, motionwk *mwp);                       /* CCL push-back (sets D_8C1BDFE0) */
Sint32 func_8C06D118(Uint8 pno);                                        /* target list non-empty (B_8C73EE38/B_8C742288) */
void func_8C06DCA2(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* slide-floor acceleration (attr & 0x20000) */
void func_8C06E4E0(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* acceleration for attr & 0x4000 */
void func_8C06EAC0(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* event-locked player (twp->flag & 0x4000) */
void func_8C06EFCE(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* event-locked acceleration */
void func_8C06F530(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* path follow (twp->flag & 0x2000) */
void func_8C0700C2(taskwk *twp, motionwk *mwp, playerwk *pwp, Float max_spd); /* snowboard (FR4 spilled at 8C0700E2) */
void func_8C0714B6(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C071656(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0718C0(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* no input and dotp < 0.71 */
void func_8C071A20(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* dotp < 0.94 and attr & 0x800000 */
void func_8C071B26(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* airborne motion (Sonic/Tails mains) */
void func_8C071D40(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* PResetAngle-like (mwp unused) */
void func_8C071DD8(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C071EC0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleY-like: R7 = mwp->ang_aim.y at every call site */
void func_8C072000(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleYQ-like: P2G spd, AdjustAngle(ang.y, ang, 0x2000), G2P */
void func_8C0720A0(taskwk *twp, motionwk *mwp, playerwk *pwp, Angle ang); /* PAdjustAngleYS-like */
void func_8C0723E0(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* clamp mwp->spd to lim_h_spd/lim_v_spd */
void func_8C072510(taskwk *twp, motionwk *mwp);
Sint32 func_8C0742D4(taskwk *twp, csts *cp, _OBJ_LANDCOLL *lc);
Sint32 func_8C0747D2(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp,
                     NJS_POINT3 *pos, NJS_POINT3 *ofs, Sint32 ret, Uint32 attr); /* 4 stack arguments; attr = *R10 of the caller */
Sint32 func_8C074AFC(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp,
                     NJS_POINT3 *pos, NJS_POINT3 *ofs, Sint32 ret, Uint32 attr);
void func_8C074CF0(motionwk *mwp, playerwk *pwp, csts *cp, NJS_POINT3 *v);
void func_8C074EE4(taskwk *twp, playerwk *pwp, csts *cp);
void func_8C07506E(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *ofs); /* moving land object carries the player (forcewk of pwp->ttp) */
void func_8C0751C0(taskwk *twp, motionwk *mwp, csts *cp);
void func_8C075214(taskwk *twp, motionwk *mwp, playerwk *pwp, Uint32 attr); /* ORs attr into pwp->attr */
Sint32 func_8C075416(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp, Uint32 attr); /* attr = _OBJ_LANDCOLL.slAttribute (stack) */
Sint32 func_8C075686(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp, Uint32 attr);
Sint32 func_8C075860(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp);
Sint32 func_8C075A40(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp, Uint32 attr);
void func_8C0763A0(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *v);
void func_8C076640(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *v);
Sint32 func_8C0777E0(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C078856(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C078F1E(NJS_POINT3 *nrm, NJS_POINT3 *v, Angle angy);
void func_8C079000(NJS_POINT3 *nrm, NJS_POINT3 *v, Angle angy);        /* convert along a ground normal (PGetSpeed passes &pwp->floor_normal) */
void func_8C079234(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0792DC(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07B2B8(Uint8 pno, Sint32 n);                                /* pfp->seq = D_8C19C32C[n] */
void func_8C07B320(taskwk *twp);                                        /* starts task 8C044680 with counter.b[0] */
void func_8C07C298(Uint8 pno, Angle ang);
void func_8C07C2E2(Uint8 pno, task *tp, NJS_POINT3 *pos);               /* look at a task (+ offset): ewp->look_mode 3 */
void func_8C07C318(Uint8 pno, NJS_POINT3 *pos);                         /* look at a position: look_mode 2 */
void func_8C07C34C(Uint8 pno, Angle3 *ang);                             /* look angle: look_mode 1 */
void func_8C07C376(Uint8 pno);                                          /* look off */
void func_8C07C550(Uint8 pno, task *htp);                               /* pwp->htp = htp */
void func_8C07C5CA(Uint8 pno1, Uint8 pno2, Float spd);
void func_8C07C720(Uint8 pno, Float x, Float y, Float z);
void func_8C07C9FA(Uint8 pno, Float x, Float y, Float z);
void func_8C07CAD0(Uint8 pno, Float x, Float y, Float z);
void func_8C07CB40(Uint8 pno, Float x, Float y, Float z);
void func_8C07CBBC(Uint8 pno, Uint8 type);
void func_8C07C1B4(Uint8 pno, Float x, Float z);                        /* event lock 2 target (clears f34/f38) */
void func_8C07C1EE(Uint8 pno, Float x, Float z);
void func_8C07C240(Uint8 pno, Float x, Float z);
void func_8C07C270(Uint8 pno);
Sint32 func_8C07CE58(Uint8 pno, Float *spd);
Sint32 func_8C07CFAA(task *tp);                                         /* character id of a player task, -1 if none (EXTU.B counter.b[1]) */
Sint32 func_8C07CFE8(void);                                             /* number of occupied player slots */
Sint32 func_8C07D18E(Uint8 pno);                                        /* partner player index or -1 */
void func_8C07D2DA(task *tp);                                           /* speed-up task exec (PL_SPDUPWK) */
void func_8C07D67C(playerwk *pwp);                                      /* allocate island_list (CAlloc(15, 0x20)) */
void func_8C07D6C0(playerwk *pwp);                                      /* free island_list */
void func_8C07D6E0(Float x, Float y, Float z, Float r);                 /* add a land position */
Sint32 func_8C07D7B4(taskwk *twp, motionwk *mwp, playerwk *pwp);        /* steer towards island_list */
void func_8C07DC2A(taskwk *twp, taskwk *htwp, playerwk *pwp);           /* push the held object out to p.rad + info[1].a */
Sint32 func_8C05E6E8(taskwk *twp);                                      /* -1/0/1 (hit task wtimer < 30.0f) */
Sint32 func_8C05E7D4(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C05E788(taskwk *twp);
void func_8C09247E(taskwk *twp, shadowwk *shadow);                      /* blob shadow (&pwp->shadow) */
void func_8C0925A0(taskwk *twp, shadowwk *shadow);                      /* character shadow */
void func_8C107C80(Sint32 no);
void func_8C109FDE(Sint32 pno, Sint32 a, Sint32 b, Sint32 c);           /* vibration (callee SHLL2s R4 unextended) */
/* Sonic */
void func_8C0A9660(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* back to standing */
void func_8C0B1D60(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0B2170(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* morph blend of the shoe parts (free.f[6]/f[7]) */
void func_8C0B2EA0(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* start the speed-limit task (SPDLIMITWK) */
void func_8C0B32B6(task *tp);
void func_8C0B3A2E(task *tp);                                           /* bubble task setup */
void func_8C0B4F00(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0B60B8(Sint32 pno);                                         /* pno must stay 32-bit (MOV.L R4,@R15) */
void SonicModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0AE17A(task *tp);                                           /* Sonic display */
void func_8C0AE3D6(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* stand action selector (tail call from 8C0A9660) */
Sint32 func_8C0AF6C4(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0B1DE0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0B1FD8(taskwk *twp, motionwk *mwp, playerwk *pwp);
Angle func_8C0B2022(taskwk *twp, motionwk *mwp, playerwk *pwp);         /* added to ang.y */
void func_8C0B2120(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0B2338(task *tp);                                           /* face task (PL_FACE in tp->mwp) */
void func_8C0B2890(task *tp);
void func_8C0B28EE(task *tp);                                           /* look task (lookwk in tp->mwp) */
void func_8C0B2D40(taskwk *twp);
void func_8C0B2DB2(taskwk *twp);                                        /* dash effects */
void func_8C0B2DD8(taskwk *twp);
void func_8C0B3480(taskwk *twp, NJS_POINT3 *pos, NJS_POINT3 *vec, NJS_POINT3 *org); /* bubble emitter position (org may be NULL) */
void func_8C0B3FA6(task *tp);
void func_8C0B4040(taskwk *twp);
void func_8C0B41E0(taskwk *twp);
void func_8C0B4332(taskwk *twp);
void func_8C0B4860(NJS_VECTOR *v, Angle3 *ang);
void func_8C0B48B4(task *tp);
void func_8C0B5B80(taskwk *twp, Sint32 n);
void func_8C06D136(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* nearest target (B_8C73EE38/B_8C742288), then 8C071700 */
Sint32 func_8C06D080(taskwk *twp, motionwk *mwp);

/* ---- player data ---- */
/* Position history: ring of 256 frames x 8 players (index B_8C78C688[pno]);
 * written by 8C06C6A0, read by GetPlayerPosition 8C07CE82, written by
 * SetPlayerPosition/SetPositionP. B_8C792690 = B_8C78C690 + 0x6000. */
extern Uint8 B_8C78C688[8];
extern NJS_POINT3 B_8C78C690[256][8];
extern Angle3 B_8C792690[256][8];
extern Sint32 D_8C1BDFE0;               /* CCL push-back flag: set by 8C06CB40, cleared each frame by 8C06C6F8 (just before the physics table) */
extern Sint8 B_8C798690[8];             /* per character id (8C07D5FC, MOV.B) */
extern TaskFuncPtr B_8C798798[8];       /* saved exec per player (8C07D226/8C07D250) */
extern TaskFuncPtr B_8C7987B8[8];       /* saved disp per player */
extern NJS_TEXLIST texlist_SONIC;       /* 8C1C6C94 */
/* Sonic model/effect data (1ST_READ, extern only; the ripper provides them
 * on PC). The NJS_OBJECT nodes whose sibling (+0x30) SonicMain swaps must
 * stay arrays: a plain struct extern makes SHC fold sym+0x30 into the pool
 * (code_8C0A77BC.c). */
extern NJS_OBJECT D_8C1C7D70[];         /* shoe / crystal ring parts (8C0A77BC) */
extern NJS_OBJECT D_8C1C8CA4[];
extern NJS_OBJECT D_8C1CAD20[];
extern NJS_OBJECT D_8C1C7504;           /* evalflags 0x16 + model swapped by 8C0B2170 */
extern NJS_OBJECT D_8C1C8454;
extern NJS_MODEL *D_8C1C5EE8[4];        /* morph {dst, a, b, original} (8C0B2170) */
extern NJS_MODEL *D_8C1C5EF8[4];
extern void *D_8C1C5F08[];              /* pointer table read by SonicMain */
extern NJS_MODEL Sonic_Jumpball_Effect; /* 8C25654C */
extern NJS_MODEL Sonic_Ice;             /* 8C256B7C */
extern NJS_MODEL Sonic_Ice_Particles;
extern NJS_TEXLIST D_8C25658C;          /* {&D_8C256580, 1} (ice) */
extern NJS_TEXLIST D_8C1C694C;          /* effect texlists (twp->value.ptr of 8C0B4040..) */
extern NJS_TEXLIST D_8C1C6954;
extern NJS_ACTION *D_8C1C6318[];        /* snowboard actions for player actions 102..124 (NULL: static model) */
extern NJS_TEXLIST D_8C1C6380[];
extern Sint32 D_8C1C63A0;
extern NJS_SPRITE D_8C1C6AD0[];         /* njDrawSprite3D cells (8C0B58E0) */
extern NJS_TEXLIST D_8C1C3068;          /* bubble texlist */
extern NJS_TEXANIM D_8C1C67DC[];

/* ---- task work layouts of player helper tasks (task.awp unless noted) ---- */

/* Speed-up item task (8C07D2A2/8C07D2DA/8C07D274; CreateElementalTask(8,
 * 2, func_8C07D2DA)): saves and boosts p.run_accel/max_x_spd/lim_frict.
 * Canonical name PL_SPDUPWK: sa1/pending.h. */
typedef struct {
    Uint8 pno;                  /* 0x00 */
    Uint8 pad01;
    Uint16 timer;               /* 0x02 MOV.W @(2) + EXTU.W, unsigned compare with 600 (8C07D274) */
    Float run_accel;            /* 0x04 saved p.run_accel */
    Float max_x_spd;            /* 0x08 saved p.max_x_spd */
    Float lim_frict;            /* 0x0C saved p.lim_frict */
} sa1_PL_SPDUPWK;               /* 0x10 */

/* Drowning task (exec 8C07BE6E, disp 8C07BDA0; created by 8C06C6F8).
 * pno is read MOV.B + EXTU.B. Canonical name DROWNWK: sa1/pending.h. */
typedef struct {
    Uint8 pno;                  /* 0x00 */
    Uint8 pad01[3];
    Uint32 timer;               /* 0x04 frames, counts up (CMP/HS 0x30C) */
} sa1_DROWNWK;                  /* 0x08 (in the 0x10 any work) */

/* Look timer task (8C07C3C0). Canonical name PL_LOOKTIMEWK: pending.h. */
typedef struct {
    Uint8 pno;                  /* 0x00 (EXTU.B) */
    Uint8 pad01[3];
    Uint32 timer;               /* 0x04 post-increment, CMP/HI */
    Uint32 time;                /* 0x08 frames to look */
} sa1_PL_LOOKTIMEWK;            /* 0x0C */

/* Super Sonic parameter task (func_8C0B32B6 init, 8C0B3280 exec, 8C0B3240
 * dest; created by 8C0AFF1A/8C0B0E02 with the player number in byte 0).
 * Saves air_resist/air_resist_air/air_accel but restores them into
 * max_x_spd/run_accel/air_accel (as in the game). No pointers. */
typedef struct {
    Sint8 pno;                  /* 0x00 player number (MOV.B R0,@R3 at 8C0B0E02) */
    Uint8 pad01[3];
    Float save0;                /* 0x04 saved p.air_resist (restored into p.max_x_spd) */
    Float save1;                /* 0x08 saved p.air_resist_air (restored into p.run_accel) */
    Float save2;                /* 0x0C saved p.air_accel */
} SONIC_SUPERWK;                /* 0x10 */

/* Sonic speed-limit task (func_8C0B2EA0 creates it; 8C0B2DFE/8C0B2E30
 * restore p.max_x_spd). Canonical name SPDLIMITWK: sa1/pending.h. */
typedef struct {
    taskwk *twp;                /* 0x00 player taskwk (8C0B2DFE tests flag & 0x200) */
    struct playerwk *pwp;       /* 0x04 */
    Float max_x_spd;            /* 0x08 saved pwp->p.max_x_spd */
} sa1_SPDLIMITWK;               /* 0x0C */

/* Target list entry (B_8C73EE38[1161] for player 0, B_8C742288[1161] for the
 * others; .RES.B 9288 each; written by the CCL code 8C0334E8, ended by a
 * NULL twp; read by 8C06D136, 8C06D118, 8C0B199A). Canonical name
 * PL_TARGET (SONIC_TARGET in code_8C0B16BC.c): sa1/pending.h. */
typedef struct {
    taskwk *twp;                /* 0x00 target (its pos and first CCL shape center are used) */
    Float dist;                 /* 0x04 */
} sa1_PL_TARGET;                /* 0x08 */

/*
 * Head/eye look work of the character faces (tp->mwp of the look task,
 * MAlloc(0x88) by 8C0B27EE; 8C07AEF8/8C07AF8C update it; 8C0B28EE).
 * The head angles are stored as (Uint16) (EXTU.W), the eye ones unextended.
 * Canonical name lookwk (LOOKWK in code_8C0B2338.c): sa1/pending.h.
 */
typedef struct {
    Angle head_tgt_y;           /* 0x00 */
    Angle head_tgt_z;           /* 0x04 */
    Angle head_y;               /* 0x08 */
    Angle head_z;               /* 0x0C */
    Angle eye_tgt_y;            /* 0x10 */
    Angle eye_tgt_z;            /* 0x14 */
    Angle eyeL_y;               /* 0x18 */
    Angle eyeL_z;               /* 0x1C */
    Angle eyeR_y;               /* 0x20 */
    Angle eyeR_z;               /* 0x24 copy of eyeL_z */
    Angle head_y_max;           /* 0x28 */
    Angle head_y_min;           /* 0x2C */
    Angle head_z_max;           /* 0x30 */
    Angle head_z_min;           /* 0x34 */
    Angle eyeL_y_max;           /* 0x38 */
    Angle eyeL_y_min;           /* 0x3C */
    Angle eyeL_z_max;           /* 0x40 */
    Angle eyeL_z_min;           /* 0x44 */
    Angle eyeR_y_max;           /* 0x48 */
    Angle eyeR_y_min;           /* 0x4C */
    Angle eye_z_max;            /* 0x50 both eyes */
    Angle eye_z_min;            /* 0x54 */
    NJS_OBJECT *head;           /* 0x58 obj->ang[1]/[2] written */
    NJS_OBJECT *eyeL;           /* 0x5C */
    NJS_OBJECT *eyeR;           /* 0x60 */
    Angle head_ang[3];          /* 0x64 saved head->ang */
    Angle eyeL_ang[3];          /* 0x70 saved eyeL->ang */
    Angle eyeR_ang[3];          /* 0x7C saved eyeR->ang */
} sa1_lookwk;                   /* 0x88 */
/*
 * Air bubbles of the characters under water (Sonic chunk 8C0B33A0-8C0B3F2C):
 * parameter table D_8C1C63A4 (3 entries per character, stride 0x48, through
 * twp->counter.ptr, index twp->btimer) and the MAlloc(0x34) bubble list
 * (head twp->value.ptr). Field use from func_8C0B3480 (ofs/dist/spd along
 * the weld vector), 8C0B37F2 (spawn), 8C0B3B1A (move) and 8C0B3F2C (draw:
 * sprite sx = size, sy = size * aspect, ang = (Sint16)ang). Canonical names
 * BUBBLE / BUBBLE_PARAM: sa1/pending.h.
 */
typedef struct {
    Sint32 chr;                 /* 0x00 character id, compared with func_8C07CE40(0) (8C0B3A2E) */
    NJS_POINT3 ofs;             /* 0x04 offset from the attach point (8C0B3480) */
    Float dist;                 /* 0x10 distance along the attach (weld) vector */
    Float spd;                  /* 0x14 speed along the attach vector */
    Sint32 attach;              /* 0x18 0: user0 weld, 1: user1 weld, else eyes_height (8C0B3480 switch) */
    Float size_max;             /* 0x1C bubble size range (8C0B37F2) */
    Float size_min;             /* 0x20 */
    Float aspect_rand;          /* 0x24 random part of the start aspect */
    Float damp;                 /* 0x28 speed damping (8C0B3B1A) */
    Float buoyancy;             /* 0x2C added to spd.y every frame */
    Float spd_rand;             /* 0x30 random speed added on each axis */
    Float rate;                 /* 0x34 spawn probability per frame (rand() / 32768) */
    Float ang_rand;             /* 0x38 random tilt in degrees */
    Float grow;                 /* 0x3C size increment per frame */
    Float jitter_rate;          /* 0x40 probability of a sideways kick */
    Float jitter;               /* 0x44 sideways kick amplitude */
} sa1_BUBBLE_PARAM;             /* 0x48 */

typedef struct sa1_BUBBLE {
    Float surface;              /* 0x00 water surface height (pwp->shadow.y_top) */
    NJS_POINT3 pos;             /* 0x04 */
    NJS_POINT3 spd;             /* 0x10 */
    Sint16 ang;                 /* 0x1C sprite angle */
    Sint16 angspd;              /* 0x1E */
    Float aspect;               /* 0x20 sprite sy / sx (start 1 - rand * aspect_rand) */
    Float size;                 /* 0x24 sprite sx (starts at maxsize * 0.1; grows by grow) */
    Float maxsize;              /* 0x28 random in [size_min, size_max] (8C0B37F2) */
    struct sa1_BUBBLE *prev;    /* 0x2C */
    struct sa1_BUBBLE *next;    /* 0x30 */
} sa1_BUBBLE;                   /* 0x34: func_8C0B33A0 MAlloc(52) must become MAlloc(sizeof(BUBBLE)) */

/*
 * Partner/2P AI work (task.awp of the AI tasks 8C082D60..8C085660, CAlloc'd
 * with its own size 0x9C: code_8C082D60.c, code_8C082FD4.c, code_8C084260.c,
 * code_8C084324.c, code_8C084AE0.c, code_8C084BA0.c, code_8C083E5C.c,
 * code_8C085532.c, code_8C085660.c each use a view of it). Two player
 * blocks (tp, twp, mwp, pwp) at 0x44 and 0x54: the AI-driven player and the
 * player it follows; the AI writes a virtual pad (on/press, angle/stroke).
 */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 smode;                /* 0x01 */
    Sint8 id;                   /* 0x02 */
    Sint8 btimer;               /* 0x03 index into modetbl (compared < 3) */
    Sint16 timer;               /* 0x04 */
    Sint16 count;               /* 0x06 */
    Sint8 *modetbl;             /* 0x08 mode sequence */
    Uint32 flag;                /* 0x0C */
    Sint32 unk10;               /* 0x10 */
    Sint32 unk14;               /* 0x14 */
    NJS_POINT3 tgt;             /* 0x18 target position */
    Angle ang;                  /* 0x24 virtual stick angle */
    Float dist;                 /* 0x28 */
    Angle ang2;                 /* 0x2C */
    Float stroke;               /* 0x30 virtual stick stroke */
    Sint32 unk34;               /* 0x34 */
    Sint32 unk38;               /* 0x38 */
    Uint32 on;                  /* 0x3C virtual buttons held */
    Uint32 press;               /* 0x40 virtual buttons pressed */
    struct task *tp;            /* 0x44 AI-driven player */
    taskwk *twp;                /* 0x48 */
    motionwk *mwp;              /* 0x4C */
    struct playerwk *pwp;       /* 0x50 */
    struct task *tp2;           /* 0x54 leader */
    taskwk *twp2;               /* 0x58 */
    motionwk *mwp2;             /* 0x5C */
    struct playerwk *pwp2;      /* 0x60 */
    Sint32 unk64[3];            /* 0x64 */
    Float range[3];             /* 0x70 */
    Sint32 unk7C;               /* 0x7C */
    Sint16 time80;              /* 0x80 */
    Sint16 time82;              /* 0x82 */
    Sint16 time84;              /* 0x84 */
    Sint16 unk86;               /* 0x86 */
    Sint16 count88;             /* 0x88 */
    Sint16 unk8A;               /* 0x8A */
    Sint32 unk8C;               /* 0x8C */
    NJS_POINT3 pos;             /* 0x90 */
} PL_AIWK;                      /* 0x9C */

SA1_ASSERT_SIZE(sa1_PL_SPDUPWK, 0x10);
SA1_ASSERT_OFFSET(sa1_PL_SPDUPWK, timer, 0x02);
SA1_ASSERT_OFFSET(sa1_PL_SPDUPWK, run_accel, 0x04);
SA1_ASSERT_OFFSET(sa1_PL_SPDUPWK, lim_frict, 0x0C);
SA1_ASSERT_SIZE(sa1_DROWNWK, 0x08);
SA1_ASSERT_OFFSET(sa1_DROWNWK, timer, 0x04);
SA1_ASSERT_SIZE(sa1_PL_LOOKTIMEWK, 0x0C);
SA1_ASSERT_OFFSET(sa1_PL_LOOKTIMEWK, timer, 0x04);
SA1_ASSERT_OFFSET(sa1_PL_LOOKTIMEWK, time, 0x08);
SA1_ASSERT_SIZE(SONIC_SUPERWK, 0x10);
SA1_ASSERT_OFFSET(SONIC_SUPERWK, save0, 0x04);
SA1_ASSERT_OFFSET(SONIC_SUPERWK, save2, 0x0C);
SA1_ASSERT_SIZE(sa1_SPDLIMITWK, 0x0C);
SA1_ASSERT_OFFSET(sa1_SPDLIMITWK, pwp, 0x04);
SA1_ASSERT_OFFSET(sa1_SPDLIMITWK, max_x_spd, 0x08);
SA1_ASSERT_SIZE(sa1_PL_TARGET, 0x08);
SA1_ASSERT_OFFSET(sa1_PL_TARGET, dist, 0x04);
SA1_ASSERT_SIZE(sa1_lookwk, 0x88);
SA1_ASSERT_OFFSET(sa1_lookwk, head_y_max, 0x28);
SA1_ASSERT_OFFSET(sa1_lookwk, eye_z_max, 0x50);
SA1_ASSERT_OFFSET(sa1_lookwk, head, 0x58);
SA1_ASSERT_OFFSET(sa1_lookwk, eyeR, 0x60);
SA1_ASSERT_OFFSET(sa1_lookwk, head_ang, 0x64);
SA1_ASSERT_OFFSET(sa1_lookwk, eyeR_ang, 0x7C);
SA1_ASSERT_SIZE(sa1_BUBBLE_PARAM, 0x48);
SA1_ASSERT_OFFSET(sa1_BUBBLE_PARAM, dist, 0x10);
SA1_ASSERT_OFFSET(sa1_BUBBLE_PARAM, attach, 0x18);
SA1_ASSERT_OFFSET(sa1_BUBBLE_PARAM, rate, 0x34);
SA1_ASSERT_OFFSET(sa1_BUBBLE_PARAM, jitter, 0x44);
SA1_ASSERT_SIZE(sa1_BUBBLE, 0x34);
SA1_ASSERT_OFFSET(sa1_BUBBLE, ang, 0x1C);
SA1_ASSERT_OFFSET(sa1_BUBBLE, size, 0x24);
SA1_ASSERT_OFFSET(sa1_BUBBLE, prev, 0x2C);
SA1_ASSERT_OFFSET(sa1_BUBBLE, next, 0x30);
SA1_ASSERT_SIZE(PL_AIWK, 0x9C);
SA1_ASSERT_OFFSET(PL_AIWK, modetbl, 0x08);
SA1_ASSERT_OFFSET(PL_AIWK, tgt, 0x18);
SA1_ASSERT_OFFSET(PL_AIWK, stroke, 0x30);
SA1_ASSERT_OFFSET(PL_AIWK, on, 0x3C);
SA1_ASSERT_OFFSET(PL_AIWK, tp, 0x44);
SA1_ASSERT_OFFSET(PL_AIWK, pwp, 0x50);
SA1_ASSERT_OFFSET(PL_AIWK, tp2, 0x54);
SA1_ASSERT_OFFSET(PL_AIWK, pwp2, 0x60);
SA1_ASSERT_OFFSET(PL_AIWK, range, 0x70);
SA1_ASSERT_OFFSET(PL_AIWK, time80, 0x80);
SA1_ASSERT_OFFSET(PL_AIWK, pos, 0x90);

#endif
