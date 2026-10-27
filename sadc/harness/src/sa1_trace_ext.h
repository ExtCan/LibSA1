/*
 * sa1_trace_ext.h - trace sidecar files written next to a trace (gsa1_rounds_plan.md 2.6).
 *
 * The trace NAME.bin stays version 2 (sa1_trace.h): libsa1's readers and the existing references are
 * untouched.  Data a later test needs on top of it goes into optional sidecar files with the same step
 * numbering, one record per record of NAME.bin:
 *
 *   NAME.p1.bin   player 1 and the follower AI ("1.5P" Tails, func_8C081AC0 / func_8C081D4E):
 *                 `sa1h trace --pno1` (docs/research/follower_ai.md 6 F1, Review R.2.4, R.2.5, G13)
 *   NAME.obj.bin  the host-object stream of round 5 (`sa1h trace --hostobj FILE` / `--create-task`, defined at
 *                 the end of this file: sa1_trace_obj_header / sa1_trace_obj_rec; libsa1_interaction.md 4.3)
 *
 * Every sidecar starts with sa1_trace_ext_header (magic, version, header size, record size, count, the
 * stage of the run), then header.count records of header.record_size bytes.  Readers honour
 * header_size / record_size, so a later version may append fields.  Little-endian, naturally aligned,
 * sizes asserted below.  harness/tools/sa1trace.py reads them (info / dump / diff, `p1` and `ext`).
 */
#ifndef SA1_TRACE_EXT_H
#define SA1_TRACE_EXT_H

#include <stddef.h>
#include <stdint.h>

#include "sa1_trace.h"

#define SA1_TRACE_EXT_MAGIC_P1   "SA1XP101"   /* NAME.p1.bin: player 1 + follower AI (version 1) */
#define SA1_TRACE_EXT_MAGIC_OBJ  "SA1XOB01"   /* NAME.obj.bin: host objects of round 5 (version 1) */
#define SA1_TRACE_EXT_P1_VERSION 1

typedef struct sa1_trace_ext_header {
    char magic[8];              /* SA1_TRACE_EXT_MAGIC_* (not NUL-terminated) */
    uint32_t version;           /* format version of this kind (P1: 1) */
    uint32_t header_size;       /* sizeof(sa1_trace_ext_header) */
    uint32_t record_size;       /* sizeof the kind's record (P1: sizeof(sa1_trace_p1_rec)) */
    uint32_t count;             /* records (= the main trace's count) */
    uint32_t flags;             /* kind-specific (P1: SA1_P1H_*) */
    int16_t level, act;         /* the run's stage (as in the main header) */
    int8_t chr;                 /* CurrentCharacter */
    uint8_t pno;                /* the player slot the sidecar follows (P1: 1) */
    uint16_t reserved16;
    uint32_t reserved[3];
} sa1_trace_ext_header;

/* P1 header flags */
#define SA1_P1H_AI_SEEN      0x1u   /* the AI task existed at some step */
#define SA1_P1H_P1_SEEN      0x2u   /* playertwp[1] existed at some step */

/* P1 record flags */
#define SA1_P1F_PLAYER1      0x01u  /* playertwp[1] is a RAM address after the step */
#define SA1_P1F_AI_TASK      0x02u  /* B_8C78C248 (the AI task) is a RAM address after the step */
#define SA1_P1F_AI_ENTERED   0x04u  /* the AI exec 8C081D4E ran in this step (ai_* entry fields valid) */
#define SA1_P1F_P1_ENTERED   0x08u  /* player 1's exec ran in this step (p1.px_* valid) */

/*
 * NAME.p1.bin record (version 1, 1460 bytes).  The AI task runs in list 0 before the players (it was
 * created after the camera task and tasks are linked at the head: code_8C098444.c:195, 223-231), reads
 * XMTRX (njCalcPoint, the off-screen test 8C082570) and the camera position (func_8C049160) left by the
 * previous step and writes player 1's pad (per[1].on / press, analog[1]); Tails, created after Sonic,
 * runs before Sonic in list 1, so the first player exec of a step is player 1's.
 */
typedef struct sa1_trace_p1_rec {
    uint32_t step;              /* = the main record's step */
    uint32_t flags;             /* SA1_P1F_* */

    /* the AI task after the step (B_8C78C248; its taskwk) */
    uint32_t ai_tp, ai_twp;     /* guest addresses (0: none) */
    int8_t ai_mode, ai_smode, ai_id, ai_btimer;   /* supervisor mode (0 4 5 2 6), steering smode (0-3) */
    int16_t ai_flag;
    uint16_t ai_wtimer;
    uint32_t ai_counter;        /* taskwk +0x08, counter.w[0] (Uint16): the off-screen frame count of 8C082570
                                   (0x0201 = player 1, Tails, as func_8C081AC0 creates it, until the first test) */
    uint32_t ai_timer;          /* taskwk +0x0C */
    uint8_t ai_twp_raw[0x40];   /* the whole taskwk (DC layout) */

    /* at the first entry of the AI exec (func_8C081D4E) in this step */
    uint32_t ai_calls;          /* entries in this step (normally 1) */
    uint32_t ai_rand;           /* D_8C5FBB08 */
    uint32_t ai_xmtrx[16];      /* XMTRX, XF0..XF15 bit patterns (what njCalcPoint(NULL) uses) */
    uint32_t ai_cam_twp;        /* D_8C18B7EC (camera_twp) */
    uint8_t ai_cam_raw[0x40];   /* the camera taskwk (camera position for the respawn, 8C049160) */

    /* when the AI exec returned: its output for player 1 */
    uint32_t ai_on, ai_press;   /* B_8C754C08[1].on / .press (B_8C754C3C +0x08 / +0x10) */
    uint32_t ai_analog_ang;     /* B_8C754DCC[1].ang (B_8C754DD4, BAMS) */
    float ai_analog_stroke;     /* B_8C754DCC[1].stroke */

    /* the first player exec entry of the step (any slot) */
    uint32_t first_pno;         /* its player number (counter.b[0]); 0xFFFFFFFF: no player exec ran */
    uint32_t first_rand;        /* D_8C5FBB08 there */
    uint32_t reserved[3];

    /* player 1, as the main record holds player 0: pad_on / pad_press / analog_* = per[1] / analog[1]
     * after the step, stick_x / stick_y / buttons = 0 (no port-B pad), the decoded fields and raw blobs
     * of playertwp / mwp / pwp[1] and its csts, the px_* state at player 1's exec entry (px_calls == 0:
     * it did not run), the globals after the step and the blob addresses; step / frame / sub / icount
     * equal the main record's */
    sa1_trace_rec p1;
} sa1_trace_p1_rec;

typedef char sa1_trace_ext_header_check[(sizeof(sa1_trace_ext_header) == 48) ? 1 : -1];
typedef char sa1_trace_p1_rec_head_check[(offsetof(sa1_trace_p1_rec, p1) == 272) ? 1 : -1];
typedef char sa1_trace_p1_rec_check[(sizeof(sa1_trace_p1_rec) == 1460) ? 1 : -1];

/*
 * NAME.obj.bin (version 1; version 2 adds the --create-task scale and OBJ_CONDITION, below sa1_trace_obj_task): the
 * host-object stream of round 5 (gsa1_rounds_plan.md 9.4, libsa1_interaction.md 4.3).
 * `sa1h trace --hostobj FILE` runs libsa1's own host-object layer (LIB src/sa1_hostobj.c and
 * src/world/sa1_world_hostobj.c, compiled into the harness: harness/src/ho_oracle.c) on the original game code, and
 * `--create-task` makes game tasks after LoadPlayerTask; this sidecar holds what a libsa1 replay must feed (the host's
 * calls before each step, the created tasks) and what the step gave (events, the held state, the proxies), so
 * LIB/tests/world_replay.c replays the trace through the same public calls and writes the same sidecar of its own
 * world for `sa1trace.py diff`.
 *
 * Space: the ops, events and the held state are in the HOST space of libsa1's default coordinates (sa1_coords
 * SA1_COORDS_PRESET_SOURCE: host = 4 * (x, -z, y)), recorded in the header; the proxies' state is SA1 space (guest
 * memory). Objects are named by the script's handle (1..), never by libsa1's id (ids depend on the registry's history).
 */
#define SA1_TRACE_EXT_OBJ_VERSION 1
#define SA1_TOBJ_MAX_OPS     4      /* host calls before one step */
#define SA1_TOBJ_MAX_EVENTS  8      /* events of one step (more: events_lost) */
#define SA1_TOBJ_MAX_OBJS    6      /* proxies reported per step (the first live handles) */
#define SA1_TOBJ_MAX_TASKS   4      /* --create-task tasks in the header */

/* ops: the public libsa1 call made before the step */
#define SA1_TOBJ_OP_ADD      1u     /* sa1_hostobj_add: kind .. shape[] (the description) */
#define SA1_TOBJ_OP_UPDATE   2u     /* sa1_hostobj_update: the description */
#define SA1_TOBJ_OP_MOVE     3u     /* sa1_hostobj_move: pos, yaw */
#define SA1_TOBJ_OP_REMOVE   4u     /* sa1_hostobj_remove: arg[0] = SA1_HOSTOBJ_REMOVE_* */
#define SA1_TOBJ_OP_RELEASE  5u     /* sa1_hostobj_release */
#define SA1_TOBJ_OP_DAMAGE   6u     /* sa1_character_damage: arg[0] = SA1_DAMAGE_*, arg[1] = the credited handle, src */
#define SA1_TOBJ_OP_OPTION   7u     /* sa1_character_set_option: arg[0] = SA1_COPT_*, arg[1] = value */
#define SA1_TOBJ_OP_RINGS    8u     /* sa1_character_add_rings: arg[0] = n */
#define SA1_TOBJ_OP_MASK     9u     /* sa1_hostobj_world_mask: arg[0] = the kind mask */
#define SA1_TOBJ_OP_CONFIG  10u     /* sa1_hostobj_configure: arg[0] = window, src[0] / src[1] = enter / leave radius */

typedef struct sa1_trace_obj_shape {
    uint32_t form, flags;           /* SA1_SHAPE_*, SA1_HSHAPE_* */
    float center[3], size[3], yaw;  /* host space */
} sa1_trace_obj_shape;

typedef struct sa1_trace_obj_op {
    uint32_t op;                    /* SA1_TOBJ_OP_* (0: none) */
    uint32_t handle;                /* the object (the script's number), 0 for character calls */
    uint32_t kind, flags;           /* ADD / UPDATE: sa1_hostobj_desc */
    int32_t armor, touch;
    uint32_t cooldown, nshapes;
    float pos[3], yaw;              /* ADD / UPDATE / MOVE: host space, radians */
    float launch_speed, grab_reach; /* host units/s, host units */
    sa1_trace_obj_shape shape[4];
    int32_t arg[2];
    float src[3];                   /* DAMAGE: the source (host space); CONFIG: radii */
    uint32_t reserved;
} sa1_trace_obj_op;

/* events: SA1_HOSTOBJ_EVENT_* (sa1_poll_hostobj_events, host space) or SA1_TOBJ_WORLD | SA1_EVENT_* (the world events
 * of the layer: HURT, DEATH, CAPACITY of SA1_CAPACITY_HOST_OBJECTS; position SA1 space, arg, bank as pushed) */
#define SA1_TOBJ_WORLD       0x100u
typedef struct sa1_trace_obj_ev {
    uint32_t type;
    uint32_t handle;                /* the object (script handle; 0: none or not a script object) */
    uint32_t attack, level, damage, shape, flags, reason, target;  /* target: a script handle */
    uint32_t arg, bank;             /* world events */
    float pos[3], dir[3], speed;    /* speed: host units/s */
} sa1_trace_obj_ev;

/* a proxy after the step (guest memory, SA1 space) */
typedef struct sa1_trace_obj_st {
    uint32_t handle;                /* 0: unused */
    uint32_t twp;                   /* its taskwk (guest address; another heap may place it elsewhere: not compared) */
    uint16_t tflag, cflag;          /* taskwk.flag, colliwk.flag */
    uint16_t eflag, mode;           /* enemy work +0x4E (0 for a HOLDABLE), taskwk.mode */
    float pos[3];
    uint32_t angy;
} sa1_trace_obj_st;

typedef struct sa1_trace_obj_rec {
    uint32_t step;                  /* = the main record's step */
    uint32_t nops, nevents, nobjs;
    uint32_t proxies, created, destroyed, refused;  /* sa1_hostobj_info of the character after the step */
    uint32_t held_object, held_phase;               /* sa1_character_held: script handle, SA1_HOLD_* */
    float held_pos[3], held_hand[3], held_yaw;      /* host space */
    uint32_t events_lost;
    sa1_trace_obj_op op[SA1_TOBJ_MAX_OPS];
    sa1_trace_obj_ev ev[SA1_TOBJ_MAX_EVENTS];
    sa1_trace_obj_st obj[SA1_TOBJ_MAX_OBJS];
} sa1_trace_obj_rec;

/* a game task created after LoadPlayerTask (trace --create-task ADDR[:IM[:LIST]][@X,Y,Z[,ANGY]]): world_replay.c creates
 * it in the world after the boot (CreateElementalTask(im, list, exec), then the position / ang.y when has_pos) */
typedef struct sa1_trace_obj_task {
    uint32_t exec, im, list, has_pos;
    float pos[3];                   /* SA1 space */
    uint32_t angy;
} sa1_trace_obj_task;

/*
 * Version 2 (round 0.9.2, sadc docs/research/gsa1_090_plan.md 6.5, gsa1_090_rings.md 5.2 with its Review R.2.4 / R.4.3):
 * `--create-task ADDR[:IM[:LIST]][@X,Y,Z[,ANGY]][/SX,SY,SZ][+ocp][+late]` also sets the task's twp->scl (taskwk +0x2C,
 * SA1), makes it after the whole stage start instead of right after LoadPlayerTask (+late, SA1_TOBJ_T_LATE) and
 * gives it an OBJ_CONDITION as the SET spawner 8C091380 does (8C0914EA-8C091578): a slot of the game's array B_8C79A3E0
 * (16 bytes each: scCount, scUserFlag, ssCondition, pTask, pObjEditEntry, unionStatus) with scCount 1, ssCondition 0x8001,
 * pTask = the task, unionStatus.f = the default range 160,000 + 100 (pool L8C0914AC + L8C091648; the spawner's own
 * "+ 100") and tp->ocp (+0x1C) = the slot. The harness takes the first slot that is all zero (with --no-objects the
 * SET has no entries) and records its index, so a replay uses the same slot. Such a run's sidecar is version 2: the v1
 * header (280 bytes, unchanged, x.version 2, x.header_size 424) followed by one sa1_trace_obj_task_ext per task, and
 * each record is the v1 record followed by sa1_trace_obj_rec_ext (x.record_size 1768: the OBJ_CONDITIONs after the
 * step). A run whose tasks use neither option is written as version 1, byte for byte as before; readers take both
 * (header_size / record_size). Round 0.9.4: `+ocp=HEX` gives the condition another initial ssCondition (SA1_TOBJ_T_OCPW,
 * the word in reserved[0]; `+ocp=8005` = a ground item box already broken: the shell); still version 2.
 */
#define SA1_TRACE_EXT_OBJ_VERSION2 2
#define SA1_TOBJ_T_SCL       0x1u   /* scl[] written to taskwk +0x2C after the creation */
#define SA1_TOBJ_T_OCP       0x2u   /* an OBJ_CONDITION: B_8C79A3E0[ocp_slot], tp->ocp */
#define SA1_TOBJ_T_LATE      0x4u   /* +late: made after the whole stage start, not right after LoadPlayerTask (the
                                       newest task of its list, as an object made during play and as every task a
                                       libsa1 replay makes after its world's boot) */
#define SA1_TOBJ_T_OCPW      0x8u   /* round 0.9.4 (sadc docs/research/gsa1_094_plan.md 6.4, its Review R.7):
                                       +ocp=HEX, the OBJ_CONDITION's initial ssCondition is reserved[0] (low 16 bits;
                                       e.g. 0x8005: a ground item box broken before it spawned, the shell). Without
                                       this flag the initial word is 0x8001, as written before (header size and
                                       version unchanged: version 2) */
#define SA1_TOBJ_OCP_ARRAY   0x8C79A3E0u    /* B_8C79A3E0: OBJ_CONDITION[1024] */
#define SA1_TOBJ_OCP_SLOTS   1024u
#define SA1_TOBJ_OCP_RANGE   160100.0f      /* unionStatus.f as 8C091380 writes it for a list entry without a range */

typedef struct sa1_trace_obj_task_ext {
    uint32_t flags;                 /* SA1_TOBJ_T_* */
    float scl[3];                   /* SA1_TOBJ_T_SCL: taskwk.scl (SA1) */
    uint32_t ocp_slot;              /* SA1_TOBJ_T_OCP: the index into B_8C79A3E0 */
    float ocp_range;                /* SA1_TOBJ_T_OCP: unionStatus.f written (SA1_TOBJ_OCP_RANGE) */
    uint32_t reserved[2];           /* reserved[0]: SA1_TOBJ_T_OCPW: the initial ssCondition (else 0 and 0x8001 is
                                       meant); reserved[1]: 0 */
} sa1_trace_obj_task_ext;

/* the initial ssCondition of a task's OBJ_CONDITION (+ocp: 0x8001; +ocp=HEX: reserved[0]) */
#define SA1_TOBJ_OCP_WORD(x) (((x)->flags & SA1_TOBJ_T_OCPW) ? (uint16_t)((x)->reserved[0] & 0xFFFFu) : (uint16_t)0x8001u)

#define SA1_TOBJ_H_HOSTOBJ   0x1u   /* the run had the host-object layer (--hostobj) */
#define SA1_TOBJ_H_EVLOST    0x2u   /* some step lost events */

typedef struct sa1_trace_obj_header {
    sa1_trace_ext_header x;         /* SA1_TRACE_EXT_MAGIC_OBJ, version 1, header_size = sizeof this, pno 0 */
    float coords_scale;             /* the host space of the ops / events (libsa1 sa1_coords) */
    float coords_axis[9];
    uint32_t ntasks;
    sa1_trace_obj_task task[SA1_TOBJ_MAX_TASKS];
    char script[48];                /* basename of the --hostobj file */
    uint32_t reserved[3];
} sa1_trace_obj_header;

/* version 2: the v1 header, then the tasks' version-2 fields */
typedef struct sa1_trace_obj_header_v2 {
    sa1_trace_obj_header v1;        /* v1.x.version 2, v1.x.header_size = sizeof(sa1_trace_obj_header_v2),
                                       v1.x.record_size = sizeof(sa1_trace_obj_rec_v2) */
    sa1_trace_obj_task_ext task_ext[SA1_TOBJ_MAX_TASKS];
    uint32_t reserved2[4];
} sa1_trace_obj_header_v2;

/* version 2: each record is the v1 record followed by the state of the tasks' OBJ_CONDITIONs after the step (the game's
 * own "collected" signal: 8C091BA4 clears 0x8001 when a RING is taken, DestroyTask bit 0, 8C091C00 ORs a group's taken
 * rings into scUserFlag) */
typedef struct sa1_trace_obj_rec_ext {
    uint32_t ocp_word[SA1_TOBJ_MAX_TASKS];  /* word 0 of B_8C79A3E0[slot] of task k: scCount | scUserFlag << 8 |
                                               ssCondition << 16 (0 for a task without +ocp) */
    uint32_t reserved[4];
} sa1_trace_obj_rec_ext;

typedef struct sa1_trace_obj_rec_v2 {
    sa1_trace_obj_rec v1;
    sa1_trace_obj_rec_ext x;
} sa1_trace_obj_rec_v2;

typedef char sa1_trace_obj_op_check[(sizeof(sa1_trace_obj_op) == 224) ? 1 : -1];
typedef char sa1_trace_obj_ev_check[(sizeof(sa1_trace_obj_ev) == 72) ? 1 : -1];
typedef char sa1_trace_obj_st_check[(sizeof(sa1_trace_obj_st) == 32) ? 1 : -1];
typedef char sa1_trace_obj_rec_check[(sizeof(sa1_trace_obj_rec) == 1736) ? 1 : -1];
typedef char sa1_trace_obj_header_check[(sizeof(sa1_trace_obj_header) == 280) ? 1 : -1];
typedef char sa1_trace_obj_task_ext_check[(sizeof(sa1_trace_obj_task_ext) == 32) ? 1 : -1];
typedef char sa1_trace_obj_header_v2_check[(sizeof(sa1_trace_obj_header_v2) == 424) ? 1 : -1];
typedef char sa1_trace_obj_rec_ext_check[(sizeof(sa1_trace_obj_rec_ext) == 32) ? 1 : -1];
typedef char sa1_trace_obj_rec_v2_check[(sizeof(sa1_trace_obj_rec_v2) == 1768) ? 1 : -1];

#endif