/*
 * sa1_trace.h - binary formats written by `sa1h trace` (t_sim.c): the
 * per-step trace (.bin) and the landtable geometry dump (.tri / .col).
 * docs/harness_simulator.md describes them with the other outputs
 * (--log-sdk, --dump-world, --census, --dump-ram-at).
 *
 * TRACE
 * A trace is the reference behaviour of the original game for one input
 * script on one stage: one record per 60 Hz logic step (func_8C04E714), with
 * the pad input given to that step and the player state after it.  A libsa1
 * test replays the same input (rec.stick_x/stick_y/buttons, or directly the
 * camera-relative analog rec.analog_ang/analog_stroke and rec.pad_on/pad_press
 * that the player code reads) on the same stage geometry and compares its own
 * state to the record, step by step.
 *
 * File layout (little-endian, no padding surprises: every field is naturally
 * aligned and the sizes are asserted below):
 *     sa1_trace_header
 *     sa1_trace_rec[header.count]          (header.record_size bytes each)
 * The raw blobs are the Dreamcast (32-bit) layouts of taskwk, motionwk,
 * playerwk and csts as documented in include/task.h, include/sa1/player.h and
 * include/sa1/collision.h; pointer fields in them are guest addresses
 * (compare them by physical address, addr & 0x1FFFFFFF, or by structure:
 * tools/sa1trace.py diff --raw [--heap-by-structure]).  The decoded fields
 * duplicate what a comparison needs.  A CSV with the decoded fields (same
 * name, .csv) is written next to it.
 *
 * Versions (header.version; the magic stays "SA1TRC01" for both):
 *   1  972-byte records: everything up to csts_raw.
 *   2  1188-byte records: the v1 record unchanged (same offsets, same bytes
 *      for the same run) followed by the v2 block: the state the player
 *      code starts from (camera taskwk, rand, XMTRX, frame counter, rings,
 *      gravity at the entry of the player task's exec), the same globals
 *      after the step, and the guest addresses of the blobs.  A v1 reader
 *      that honours header.record_size can skip the v2 block.
 */
#ifndef SA1_TRACE_H
#define SA1_TRACE_H

#include <stddef.h>
#include <stdint.h>

#define SA1_TRACE_MAGIC        "SA1TRC01"   /* format family (v1 and v2) */
#define SA1_TRACE_VERSION      2
#define SA1_TRACE_REC_V1_SIZE  972          /* record size of version 1 = offset of the v2 block */

typedef struct sa1_trace_header {
    char magic[8];              /* "SA1TRC01" */
    uint32_t version;           /* 1 or 2 (SA1_TRACE_VERSION) */
    uint32_t header_size;       /* sizeof(sa1_trace_header) */
    uint32_t record_size;       /* 972 (v1) or sizeof(sa1_trace_rec) (v2) */
    uint32_t count;             /* number of records */
    int16_t level, act;         /* CurrentLevel / CurrentAct */
    int8_t chr;                 /* CurrentCharacter (0 Sonic, 2 Tails, 3 Knuckles, 5 Amy, 6 E-102, 7 Big) */
    uint8_t steps_per_frame;    /* B_8C754EC4 during the run (2 in stages: 30 fps display, 60 Hz logic) */
    uint16_t flags;             /* bit 0: display pass was run, bit 1: no objects (empty SET files),
                                   bit 2: native decompiled code was routed in (not a pure reference),
                                   bit 3: the screen was set up (njInitSystem's view part: 640 x 480,
                                   njInitView; the default since 2026-10-20, trace --no-screen clears it;
                                   without it the screen words stay 0 and the sphere visibility test
                                   8C038D00 never passes: docs/research/visibility_fidelity.md) */
    uint32_t rom_crc32;         /* CRC-32 of the file baserom/1ST_READ.BIN */
    uint32_t stage_crc32;       /* CRC-32 of the file baserom/overlays/STGnn.BIN: the decompressed stage
                                   image the game loads at 0x0C900000 (0 if the level has none) */
    uint32_t stage_size;        /* its size */
    uint32_t landtable;         /* guest address of the act's landtable (B_8C7596F4) */
    float start_pos[3];         /* player position after the stage start (before step 0) */
    uint32_t start_ang[3];
    char input_name[64];        /* script name (or "inline") */
} sa1_trace_header;

typedef struct sa1_trace_rec {
    /* ---------------- version 1 (offsets 0..971) ---------------- */
    uint32_t step;              /* logic step number, from 0 */
    uint32_t frame;             /* display frame (func_8C04E95A call), from 0 */
    uint32_t sub;               /* step index inside the frame (B_8C754EC8) */
    uint32_t icount;            /* SH-4 instructions executed by this step */

    /* input given to this step (pdGetPeripheral, port A) */
    int16_t stick_x, stick_y;   /* -128..127, y < 0 = stick up */
    uint32_t buttons;           /* held buttons (Katana PDD_DGT_* bits: A=4, B=2, X=0x400, Y=0x200, START=8) */

    /* the same input as the player code sees it after the pad update
     * 8C050BE4 and the camera-relative analog conversion 8C05117E */
    uint32_t pad_on;            /* B_8C754C08[0].on */
    uint32_t pad_press;         /* B_8C754C08[0].press */
    uint32_t analog_ang;        /* B_8C754DCC[0].ang (BAMS) */
    float analog_stroke;        /* B_8C754DCC[0].stroke (0..1) */

    /* decoded player state after the step */
    int8_t mode, smode, id, btimer;   /* taskwk */
    int16_t twp_flag; uint16_t wtimer;
    uint32_t ang[3];            /* taskwk.ang (BAMS, low 16 bits significant) */
    float pos[3];               /* taskwk.pos */
    float mspd[3];              /* motionwk.spd (world space) */
    float macc[3];              /* motionwk.acc */
    uint32_t ang_aim[3];        /* motionwk.ang_aim */
    float pspd[3];              /* playerwk.spd (player space) */
    float pacc[3];              /* playerwk.acc */
    float floor_normal[3];      /* playerwk.floor_normal */
    float dotp;                 /* playerwk.dotp */
    uint32_t attr, last_attr;   /* playerwk.attr / last_attr (surface flags) */
    int16_t pwp_flag;           /* playerwk.flag */
    int16_t jumptimer, nocontimer, waittimer;
    uint16_t reqaction, action, lastaction, nextaction;  /* playerwk.mj */
    float nframe;               /* playerwk.mj.nframe */
    uint16_t equipment, item;
    uint32_t land_count;        /* B_8C754EF0: land collision entries gathered this step */
    uint32_t rand_state;        /* D_8C5FBB08, state of the game's rand() (8C10D334) after the step;
                                   every task draws from it, so a port should seed its RNG with the
                                   previous record's value to compare one character in isolation
                                   (v2: px_rand is the exact value the player code starts from) */
    uint32_t reserved[3];

    /* raw DC-layout copies */
    uint8_t twp_raw[0x40];
    uint8_t mwp_raw[0x40];
    uint8_t pwp_raw[0x200];
    uint8_t csts_raw[0x84];

    /* ---------------- version 2 (offsets 972..1187) ---------------- */
    /* captured at the first entry of the player-0 task's exec in this step
     * (a hook on playertp[0]->exec, B_8C78C648[0] +0x10, called with R4 =
     * that task): the camera and level tasks run before the player in the
     * task executor and draw from rand(), so this is the state the player
     * code itself starts from.  px_calls == 0: the exec did not run (or ran
     * natively through a host pointer, --route-all): the px_* fields are 0. */
    uint32_t px_exec;           /* guest address of the exec that was hooked */
    uint32_t px_calls;          /* entries of the player-0 exec during this step (normally 1) */
    uint32_t px_rand;           /* D_8C5FBB08 (rand state) */
    uint32_t px_frame_counter;  /* B_8C752BE8 (frame counter, func_8C08A71E) */
    int16_t px_rings;           /* B_8C752BE4 (ring count) */
    uint16_t px_pad;
    float px_gravity[3];        /* B_8C754BDC (Gravity) */
    uint32_t px_xmtrx[16];      /* XMTRX = the FPU's XF bank as bit patterns, XF0..XF15
                                   (NJS_MATRIX order: m[k] = XF[k], libsa1 SA1_XMTRX) */
    uint32_t px_cam_twp;        /* D_8C18B7EC (camera taskwk *, camera_twp) */
    uint8_t px_cam_raw[0x40];   /* the camera taskwk (DC layout; zero if px_cam_twp is not RAM) */

    /* the same globals after the step (like the v1 fields) */
    uint32_t frame_counter;     /* B_8C752BE8 */
    int16_t rings;              /* B_8C752BE4 */
    uint16_t pad2;
    float gravity[3];           /* B_8C754BDC */

    /* guest addresses of the blobs of this record (playertp[0], playertwp[0],
     * playermwp[0], playerpwp[0], pwp->cstsp): lets a comparison map a pointer
     * found in a raw blob back to "twp+0x20", "pwp+0x38", ... (--heap-by-structure) */
    uint32_t ptp_addr, twp_addr, mwp_addr, pwp_addr, csts_addr;
    uint32_t reserved2[3];
} sa1_trace_rec;

/* compile-time layout checks (C89-compatible) */
typedef char sa1_trace_header_size_check[(sizeof(sa1_trace_header) == 136) ? 1 : -1];
typedef char sa1_trace_rec_v1_check[(offsetof(sa1_trace_rec, px_exec) == SA1_TRACE_REC_V1_SIZE) ? 1 : -1];
typedef char sa1_trace_rec_size_check[(sizeof(sa1_trace_rec) == 1188) ? 1 : -1];

/*
 * LANDTABLE GEOMETRY (--dump-land PREFIX, version 2)
 *
 * PREFIX.tri: sa1_land_header, then sa1_land_tri[ntri], in the order the
 * land collision cache build emits them: for each COL entry (landtable
 * order), 8C028EC2 -> 8C028B00 walks the entry's NJS_OBJECT (children first,
 * then the node itself) under unit * T(pos) * Rz * Ry * Rx (non-zero angles
 * only) * S(scl) per node - the evalflags are ignored, unlike the probes -
 * and 8C0287A0 emits per meshset, per polygon/strip, triangle i of the
 * polygon as (v[i], v[i+1], v[i+2]) when ((flip + i) & 1) == 0, else
 * (v[i], v[i+2], v[i+1]) (flip = strip header bit 0x8000; quads are 2-strip
 * polygons, strips longer than 0x400 are cut like the original).  World
 * vertices are njCalcPoint results of the original SDK code in the
 * interpreter, bit-exact with what the COLPOLY pool receives.
 *
 * PREFIX.col: sa1_land_col_header, then sa1_land_col[ncol]: every COL
 * entry with its object/model parameters and its triangle range.
 *
 * Version 1 (.tri only, 16-byte header {LAND, 1, ntri, ncol} and 44-byte
 * triangles {v[9], flags, col}) transformed only the root node, honoured the
 * evalflags and used a different vertex order; it is no longer written.
 */
#define SA1_LAND_MAGIC    0x444E414Cu   /* "LAND" */
#define SA1_LANDCOL_MAGIC 0x4C4F434Cu   /* "LCOL" */
#define SA1_LAND_VERSION  2

typedef struct sa1_land_header {
    uint32_t magic;             /* SA1_LAND_MAGIC */
    uint32_t version;           /* 2 */
    uint32_t ntri;
    uint32_t ncol;              /* landtable ssCount */
    uint32_t tri_size;          /* sizeof(sa1_land_tri) */
    uint32_t col_size;          /* sizeof(sa1_land_col) */
    uint32_t landtable;         /* guest address of the landtable */
    uint32_t nnodes;            /* NJS_OBJECT nodes walked over all COLs */
} sa1_land_header;

typedef struct sa1_land_tri {
    float v[9];                 /* world space, emitted order (COLPOLY.v[0..2]) */
    uint32_t flags;             /* COL slAttribute */
    uint32_t col;               /* COL index */
    float lv[9];                /* the same vertices in the node's model space (NJS_MODEL.points) */
    uint32_t attrflags;         /* NJS_MATERIAL.attrflags of the meshset (COLPOLY.attr; 0 without mats) */
    uint32_t emit;              /* emission index within the COL (0, 1, ...) */
    uint32_t node_obj;          /* guest address of the NJS_OBJECT node */
    uint16_t node;              /* node index in walk order (0 = first node emitted) */
    uint16_t mset;              /* meshset index in the node's model */
    uint16_t mesh;              /* polygon / strip index in the meshset */
    uint16_t tri;               /* triangle index in the polygon / strip */
    uint16_t idx[3];            /* point indices of v[0..2] */
    uint16_t type;              /* meshset type: 0 triangles, 0x4000 quads, 0x8000 / 0xC000 strips */
} sa1_land_tri;

typedef struct sa1_land_col_header {
    uint32_t magic;             /* SA1_LANDCOL_MAGIC */
    uint32_t version;           /* 2 */
    uint32_t ncol;
    uint32_t col_size;          /* sizeof(sa1_land_col) */
    uint32_t landtable;
    uint32_t ntri;
    uint32_t reserved[2];
} sa1_land_col_header;

typedef struct sa1_land_col {
    uint32_t index;             /* COL index */
    uint32_t flags;             /* slAttribute (+0x20) */
    float center[3];            /* xCenter/yCenter/zCenter (+0x00) */
    float radius;               /* xWidth (+0x0C): the bounding radius the gathers use */
    float ywidth, zwidth;       /* +0x10, +0x14 */
    uint32_t blockbit;          /* +0x1C */
    uint32_t object;            /* pObject (+0x18), guest address */
    uint32_t evalflags;         /* NJS_OBJECT */
    float pos[3];
    uint32_t ang[3];
    float scl[3];
    uint32_t model;             /* NJS_MODEL *, guest address (0: none) */
    float model_center[3];      /* NJS_MODEL.center */
    float model_r;              /* NJS_MODEL.r */
    float world_center[3];      /* M * model->center with M = unit * T * Rz * Ry * Rx * S of the object,
                                   computed like 8C028EC2 computes colobj.center */
    uint32_t tri_first;         /* first triangle of this COL in the .tri */
    uint32_t tri_count;
    uint32_t nodes;             /* NJS_OBJECT nodes the cache build walks (1 = no children) */
    uint32_t npoints;           /* of the root model */
    uint32_t nmeshsets;
    uint32_t nmats;
} sa1_land_col;

typedef char sa1_land_header_check[(sizeof(sa1_land_header) == 32) ? 1 : -1];
typedef char sa1_land_tri_check[(sizeof(sa1_land_tri) == 108) ? 1 : -1];
typedef char sa1_land_col_header_check[(sizeof(sa1_land_col_header) == 32) ? 1 : -1];
typedef char sa1_land_col_check[(sizeof(sa1_land_col) == 136) ? 1 : -1];

#endif
