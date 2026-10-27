/*
 * t_sim.c - reference simulator: the original game code loads a real stage
 * (landtable, objects, camera, player) through its own stage-load and
 * stage-start functions and runs its own per-frame logic at 60 Hz under a
 * scripted pad, with HLE stubs for the hardware.  A per-logic-step trace of
 * the player state is the reference that a PC port (libsa1) is compared to.
 *
 *   sa1h trace [--level N] [--act N] [--chr N] [--frames N | --steps N]
 *              [--input SCRIPT | --input @FILE] [--out PREFIX] [--check REF.bin]
 *              [--no-objects] [--spf N] [--disp] [--discover] [--route ...]
 *              [--dump-land PREFIX] [--dump-ram-at N,..] [--log-sdk FILE]
 *              [--dump-world FILE] [--census FILE] [--disc DIR] [-v]   (usage() below)
 *              [--no-image-guard] [--super-free] [--no-level-tasks] [--create-task A:IM:L]
 *              [--pno1] [--hostobj FILE]
 *
 * Outputs (formats: sa1_trace.h, docs/harness_simulator.md): the trace
 * (version 2: v1 record + the player-exec entry state, see px_hook), the
 * landtable geometry as the collision cache build emits it (dump_land),
 * RAM images before chosen steps, the boundary SDK call log, the per-step
 * values of the game's non-pointer globals and the write census.
 *
 * Sequence (all original code, see docs/harness_simulator.md):
 *   8C053AC0  game init (SDK, memory, GD file system, ...)
 *   8C04F5F4  select character: loads S_MOT.PRS (0x0CC00000) + textures
 *   8C04E490  stage load: stg01.PRS -> 0x0C900000 (game's PRS decoder),
 *             SET/CAM files, landtable init 8C052BA0, textures, heap + tasks
 *   8C04E660  stage start: pad, camera, landtable manager task 8C0519C0,
 *             stage tasks, LoadPlayerTask 8C04F3C0 (start position
 *             8C04F8FA), death zones, CCL, ...
 *   per display frame: 8C04E95A, which runs B_8C754EC4 (= 2, set by the
 *   stage start) logic steps 8C04E714 (pad 8C050BE4, task executor, CCL,
 *   land collision world 8C028EC2 -> landtable list 8C052518, ...)
 *
 * Overlay image guard (image_guard.h): a routed native function of an overlay
 * image (src/overlays/<dir>) replaces the original only while the image at its
 * load slot is that image (CRC-32 of its code ranges); --no-image-guard routes
 * by address alone (the negative control).
 *
 * Level-task options: --no-level-tasks (the stage start creates neither
 * D_8C19DE30[level] nor D_8C19E0E0[level]), --super-free (Perfect Chaos stage,
 * level 19: the level task 0C900000 is a native stand-in that creates the sea
 * plane 0C91514A and Super Sonic's trigger 0C914D60 and nothing else;
 * gsa1_rounds_plan.md 11.1), --create-task (CreateElementalTask after
 * LoadPlayerTask; with @X,Y,Z[,ANGY] also its position, /SX,SY,SZ its scale,
 * +ocp an OBJ_CONDITION as the SET spawner gives one, +ocp=HEX one with
 * another initial ssCondition, e.g. 8005: a ground item box already
 * broken, round 0.9.4).  --pno1 writes
 * the player-1 / follower-AI sidecar <out>.p1.bin (sa1_trace_ext.h).
 *
 * Host objects (round 5, ho_oracle.c): --hostobj FILE runs libsa1's own
 * host-object layer (compiled in) on the original game code under the
 * script's calls; <out>.obj.bin holds the calls, the events, the held state
 * and the proxies of every step (also written with --create-task).
 *
 * HLE (explicit, see sim_install_hle): GD-ROM file system (gdFs* -> files
 * of the user's disc folder), pdGetPeripheral (scripted pad), vsync waits,
 * TMU frame timer, and the hardware-facing SDK functions listed in
 * sim_stubs[].  Anything else that faults is reported (and with --discover
 * auto-stubbed, like `boot`).
 */
#include "harness.h"
#include "sh4_internal.h"
#include "sa1_trace.h"
#include "sa1_trace_ext.h"
#include "image_guard.h"
#include "ho_oracle.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <math.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define GAME_INIT     0x8C053AC0u
#define CHG_CHAR      0x8C04F5F4u
#define STAGE_LOAD    0x8C04E490u
#define STAGE_START   0x8C04E660u
#define FRAME_FN      0x8C04E95Au
#define STEP_FN       0x8C04E714u
#define TASK_DISP_ALL 0x8C0988C4u

#define CUR_LEVEL     0x8C7493BAu   /* Sint16 */
#define CUR_ACT       0x8C7493BCu   /* Sint16 */
#define CUR_CHAR      0x8C7493C8u   /* Sint8 */
#define GAME_MODE     0x8C760970u   /* Sint32 (tcrf_LevelSelectVar): 4 = in game */
#define VSYNC_CNT     0x8C754EC0u   /* decremented by the vblank handler 8C051810 */
#define STEPS_PER_FRM 0x8C754EC4u
#define STEP_INDEX    0x8C754EC8u
#define LAND_COUNT    0x8C754EF0u   /* entries in the per-frame land collision list */
#define LANDTABLE     0x8C7596F4u   /* current landtable (set by the landtable task) */
#define PLAYER_TWP    0x8C78C608u
#define PLAYER_MWP    0x8C78C628u
#define PLAYER_PWP    0x8C78C668u
#define PAD_REC0      0x8C754C08u   /* per-port pad record copied by 8C050BE4 */
#define ANALOG0       0x8C754DCCu   /* player 0 analog: angle, stroke */
#define RAND_STATE    0x8C5FBB08u   /* rand() 8C10D334 LCG state */
#define LIB_START     0x8C5FBB60u
#define PLAYER_TP     0x8C78C648u   /* playertp[0] (task *) */
#define CAMERA_TWP    0x8C18B7ECu   /* D_8C18B7EC camera_twp (taskwk *) */
#define FRAME_COUNTER 0x8C752BE8u   /* B_8C752BE8 frame counter (func_8C08A71E) */
#define RINGS         0x8C752BE4u   /* B_8C752BE4 ring count (Sint16) */
#define GRAVITY       0x8C754BDCu   /* B_8C754BDC Gravity (NJS_VECTOR) */
#define CRT_START     0x8C10C10Cu   /* SHC C runtime linked into the game code region */
#define CRT_END       0x8C111194u
#define PAD_REC_SIZE  0x34u         /* B_8C754C08[n]: per[1] = B_8C754C3C */
#define CREATE_ELEMENTAL_TASK 0x8C09852Eu   /* task *CreateElementalTask(im, list, exec) */
#define CREATE_CHILD_TASK     0x8C098CA0u   /* task *CreateChildTask(im, exec, parent) */
#define LOAD_PLAYER_TASK      0x8C04F3C0u
#define LEVEL_TASK_TAB  0x8C19DE30u /* D_8C19DE30[level]: level task exec; 8C04F506: B_8C752BD4 =
                                       CreateElementalTask(2, 0, it) at the stage start */
#define LEVEL_TASK2_TAB 0x8C19E0E0u /* D_8C19E0E0[level]: 8C04F5AC: CreateElementalTask(2, 2, it) */
#define FOLLOW_AI_TASK  0x8C78C248u /* B_8C78C248: the follower ("1.5P") AI task, func_8C081AC0 */
#define FOLLOW_AI_EXEC  0x8C081D4Eu /* its exec (the supervisor) */
#define SUPER_LEVEL_EXEC 0x0C900000u   /* B_CHAOS7: D_8C19DE30[19], the Perfect Chaos level task */
#define SUPER_TRIGGER    0x0C914D60u   /* B_CHAOS7: Super Sonic's trigger (SetInputP(0, 46)) */
#define SUPER_SEA_PLANE  0x0C91514Au   /* B_CHAOS7: the sea-plane task */
#define RUN_CHILDREN     0x8C098E98u   /* func_8C098E98(tp): runs the exec of every child of tp */

/* ------------------------------------------------------------------ */
/* configuration                                                       */

typedef struct sim_cfg {
    int level, act, chr;
    int frames;               /* display frames (each = STEPS_PER_FRM logic steps) */
    int steps;                /* if > 0: stop after this many logic steps */
    int discover;             /* auto-stub unknown faults (boot-style) */
    int disp;                 /* also run the display pass per frame */
    int screen;               /* default 1: njInitSystem's view set-up (hle_nj_init_system); --no-screen: 0, the old no-op */
    const char *log_vis;      /* --log-vis FILE: every call of the sphere visibility test 8C038D00 */
    const char *vis_force;    /* --vis-force 0|1|sub0: answer the visibility test instead of running it (experiments) */
    const char *vis_only;     /* --vis-only PR,..: run the visibility test only for these return addresses, else answer 0 */
    int no_objects;           /* serve empty SET files: no stage objects */
    int spf;                  /* override B_8C754EC4 (logic steps per display frame) */
    int no_cstack;            /* no shadow call stack during the frames (faster, fault reports lose the stack) */
    int route;                /* 1: native decompiled code replaces the originals during the run, 2: also in setup */
    const char *route_only;   /* comma list of functions / addresses / source files to route (others: original) */
    const char *route_except; /* comma list not to route */
    const char *dump_land;    /* write the act's collision geometry (PREFIX.tri / PREFIX.obj) */
    const char *dump_ram;     /* write the 16 MB of guest RAM after the stage start */
    const char *coverage;     /* write the functions executed during the logic steps */
    int trace_v1;             /* --trace-v1: write a version-1 trace (972-byte records) */
    const char *dump_ram_at;  /* comma list of steps: 16 MB RAM image before each of them */
    const char *log_sdk;      /* boundary SDK call log (text) */
    const char *sdk_list;     /* boundary list (default build/verify/sdk_boundary.txt) */
    const char *log_2d;       /* --log-2d FILE: the 2D draws (njDrawSprite2D / njDrawPolygon2D), text */
    const char *dump_world;   /* per-step values of the non-pointer globals (text) */
    const char *world_syms;   /* symbol list (default build/verify/world_syms.txt) */
    int world_all;            /* --world-all: also pointer / mixed / unreferenced symbols */
    const char *census;       /* write census (byte ranges changed, 4 KB blocks written) */
    int verbose;
    int print_every;
    const char *input;        /* script text or @file */
    const char *out;          /* trace prefix */
    const char *disc;         /* disc folder (default <root>/disc/SONICADV) */
    int no_guard;             /* --no-image-guard: route overlay functions by address alone (negative control) */
    int super_free;           /* --super-free: B_CHAOS7 level-task stand-in (plan 11.1) */
    int no_level_tasks;       /* --no-level-tasks: no D_8C19DE30[level] / D_8C19E0E0[level] at the stage start */
    int n_create;             /* --create-task ADDR[:IM[:LIST]][@X,Y,Z[,ANGY]] (CreateElementalTask after LoadPlayerTask) */
    uint32_t create_addr[8], create_im[8], create_list[8];
    int create_has_pos[8];    /* @X,Y,Z[,ANGY]: taskwk.pos (SA1 space) and ang.y (hex BAMS) after the creation */
    float create_pos[8][3];
    uint32_t create_angy[8];
    uint32_t create_flags[8];  /* round 0.9.2: SA1_TOBJ_T_SCL (/SX,SY,SZ: taskwk.scl), SA1_TOBJ_T_OCP (+ocp);
                                  round 0.9.4: SA1_TOBJ_T_OCPW (+ocp=HEX) */
    float create_scl[8][3];
    uint32_t create_ocpw[8];   /* round 0.9.4: +ocp=HEX, the initial ssCondition (default 0x8001) */
    const char *hostobj;      /* --hostobj FILE: libsa1's host-object layer under the script's calls (ho_oracle.c) */
    int pno1;                 /* --pno1: the player-1 / follower-AI sidecar <out>.p1.bin */
} sim_cfg;

/* image guard epoch: bumped whenever an overlay may have been (re)loaded; the guard re-identifies
 * the images at the first guarded call of a new epoch */
static unsigned guard_epoch = 1;

/* ------------------------------------------------------------------ */
/* input script                                                        */

typedef struct seg { int n; int16_t x, y; uint32_t buttons; } seg;
static seg *segs;
static int nsegs, capsegs;

static const struct { const char *name; uint32_t bit; } btn_names[] = {
    { "C", 0x0001 }, { "B", 0x0002 }, { "A", 0x0004 }, { "START", 0x0008 }, { "UP", 0x0010 },
    { "DOWN", 0x0020 }, { "LEFT", 0x0040 }, { "RIGHT", 0x0080 }, { "Z", 0x0100 }, { "Y", 0x0200 },
    { "X", 0x0400 }, { "D", 0x0800 }, { "L", 0x10000 }, { "R", 0x20000 },
};

static int parse_buttons(const char *s, uint32_t *out)
{
    char tok[32];
    uint32_t m = 0;
    while (*s) {
        int n = 0, i, found = 0;
        while (*s == '+' || *s == '|' || *s == ' ' || *s == ',')
            s++;
        while (*s && *s != '+' && *s != '|' && *s != ' ' && *s != ',' && n < 31)
            tok[n++] = (char)toupper((unsigned char)*s++);
        tok[n] = 0;
        if (!n)
            break;
        if (tok[0] == '0' && tok[1] == 'X') {
            m |= (uint32_t)strtoul(tok, NULL, 16);
            continue;
        }
        if (!strcmp(tok, "-") || !strcmp(tok, "NONE"))
            continue;
        for (i = 0; i < (int)(sizeof btn_names / sizeof btn_names[0]); i++)
            if (!strcmp(tok, btn_names[i].name)) {
                m |= btn_names[i].bit;
                found = 1;
            }
        if (!found) {
            fprintf(stderr, "input: unknown button '%s'\n", tok);
            return -1;
        }
    }
    *out = m;
    return 0;
}

static void add_seg(int n, int x, int y, uint32_t b)
{
    if (nsegs == capsegs) {
        capsegs = capsegs ? capsegs * 2 : 64;
        segs = (seg *)realloc(segs, sizeof(seg) * (size_t)capsegs);
    }
    segs[nsegs].n = n;
    segs[nsegs].x = (int16_t)x;
    segs[nsegs].y = (int16_t)y;
    segs[nsegs].buttons = b;
    nsegs++;
}

/* Script: segments separated by newlines or ';'.  Each segment:
 *     STEPS X Y [BUTTONS]          e.g.  "60 0 -127"  "1 0 -127 A"  "30 0 0 A+B"
 * or  STEPS:X,Y[,BUTTONS]          e.g.  "60:0,-127;1:0,-127,A;59:0,-127"
 * STEPS counts 60 Hz logic steps; X/Y are the analog stick (-128..127, Y<0 = up);
 * BUTTONS: A B C D X Y Z START UP DOWN LEFT RIGHT L R, joined by '+', or 0xMASK.
 * '#' starts a comment.  After the end of the script the pad is neutral. */
static int parse_script(const char *text)
{
    const char *p = text;
    nsegs = 0;
    while (*p) {
        char line[256];
        int n = 0, cnt, x = 0, y = 0;
        uint32_t b = 0;
        char *q, *bt;
        while (*p && *p != '\n' && *p != ';' && n < 255)
            line[n++] = *p++;
        line[n] = 0;
        if (*p)
            p++;
        if ((q = strchr(line, '#')) != NULL)
            *q = 0;
        for (q = line; *q; q++)
            if (*q == ':' || *q == ',' || *q == '\t' || *q == '\r')
                *q = ' ';
        q = line;
        while (*q == ' ')
            q++;
        if (!*q)
            continue;
        cnt = (int)strtol(q, &q, 10);
        x = (int)strtol(q, &q, 10);
        y = (int)strtol(q, &bt, 10);
        if (cnt <= 0 || bt == q) {
            fprintf(stderr, "input: bad segment '%s' (want: STEPS X Y [BUTTONS])\n", line);
            return -1;
        }
        if (parse_buttons(bt, &b))
            return -1;
        add_seg(cnt, x, y, b);
    }
    return 0;
}

static int load_script(const char *arg)
{
    if (!arg)
        return 0;
    if (arg[0] == '@') {
        FILE *f = fopen(arg + 1, "rb");
        long n;
        char *buf;
        int r;
        if (!f) {
            fprintf(stderr, "cannot open input script %s\n", arg + 1);
            return -1;
        }
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = (char *)malloc((size_t)n + 1);
        n = (long)fread(buf, 1, (size_t)n, f);
        buf[n] = 0;
        fclose(f);
        r = parse_script(buf);
        free(buf);
        return r;
    }
    return parse_script(arg);
}

static int script_steps(void)
{
    int i, n = 0;
    for (i = 0; i < nsegs; i++)
        n += segs[i].n;
    return n;
}

static void script_at(int step, int16_t *x, int16_t *y, uint32_t *b)
{
    int i;
    for (i = 0; i < nsegs; i++) {
        if (step < segs[i].n) {
            *x = segs[i].x;
            *y = segs[i].y;
            *b = segs[i].buttons;
            return;
        }
        step -= segs[i].n;
    }
    *x = 0;
    *y = 0;
    *b = 0;
}
/* ------------------------------------------------------------------ */
/* GD-ROM file system HLE: gdFsOpen / gdFsGetFileSize / gdFsRead /     */
/* gdFsClose (Shinobi) read the files of the user's disc folder.       */

#define GDFS_OPEN    0x8C6016D6u   /* GDFS gdFsOpen(const char *name, GDFS_DIRREC dir) */
#define GDFS_SIZE    0x8C600E2Cu   /* Bool gdFsGetFileSize(GDFS fs, Sint32 *size) */
#define GDFS_READ    0x8C600F1Eu   /* Sint32 gdFsRead(GDFS fs, Sint32 nsct, void *buf) */
#define GDFS_CLOSE   0x8C600DF4u   /* void gdFsClose(GDFS fs) */
#define GDFS_HANDLES 0x8C000800u   /* fake handles (16 bytes each, low RAM) */
#define GDFS_MAX     16

typedef struct gdfile { int used; uint8_t *data; long size, pos; char name[64]; } gdfile;
static gdfile gdf[GDFS_MAX];
static char disc_dir[1100];
static int sim_verbose;
static int no_objects;

typedef struct gdlog { char name[64]; long size; } gdlog;
static gdlog gdlogs[256];
static int ngdlogs;

static void guest_str(uint32_t a, char *buf, int n)
{
    int i;
    for (i = 0; i < n - 1; i++) {
        char ch = (char)sh4_read8(a + (uint32_t)i);
        if (!ch)
            break;
        buf[i] = ch;
    }
    buf[i] = 0;
}

/* copy host bytes into guest RAM (marks the blocks dirty for snapshots) */
static int guest_copy_in(uint32_t dst, const uint8_t *src, long n)
{
    uint32_t off;
    uint8_t *h;
    long i;
    if (n <= 0)
        return 0;
    if (!sh4_is_ram(dst))
        return -1;
    off = dst & 0x00FFFFFFu;
    if ((uint32_t)n > SH4_RAM_SIZE - off)
        return -1;
    h = sh4_ram() + off;
    memcpy(h, src, (size_t)n);
    for (i = (long)(off >> 12); i <= (long)((off + (uint32_t)n - 1) >> 12); i++)
        sh4_ram_dirty[i] = 1;
    return 0;
}

static void hle_gdfs_open(sh4_cpu *c, void *u)
{
    char name[64], path[1300];
    FILE *f;
    int i, k;
    long size;
    (void)u;
    guest_str(c->r[4], name, sizeof name);
    for (i = 0; name[i]; i++)
        name[i] = (char)toupper((unsigned char)name[i]);
    c->r[0] = 0;
    for (k = 0; k < GDFS_MAX && gdf[k].used; k++)
        ;
    snprintf(path, sizeof path, "%s/%s", disc_dir, name[0] == '/' || name[0] == '\\' ? name + 1 : name);
    if (no_objects && !strncmp(name, "SET", 3) && k < GDFS_MAX) {
        /* empty object layout: 0x20-byte header with count 0 */
        gdf[k].data = (uint8_t *)calloc(1, 32);
        gdf[k].used = 1;
        gdf[k].size = 32;
        gdf[k].pos = 0;
        snprintf(gdf[k].name, sizeof gdf[k].name, "%s", name);
        if (ngdlogs < 256) {
            snprintf(gdlogs[ngdlogs].name, sizeof gdlogs[ngdlogs].name, "%s (empty)", name);
            gdlogs[ngdlogs++].size = 32;
        }
        if (sim_verbose)
            printf("  gdFsOpen(\"%s\") -> empty (--no-objects)\n", name);
        c->r[0] = GDFS_HANDLES + (uint32_t)k * 16;
        return;
    }
    f = fopen(path, "rb");
    if (ngdlogs < 256) {
        snprintf(gdlogs[ngdlogs].name, sizeof gdlogs[ngdlogs].name, "%s", name);
        gdlogs[ngdlogs].size = -1;
    }
    if (!f || k == GDFS_MAX) {
        if (sim_verbose)
            printf("  gdFsOpen(\"%s\") -> not found\n", name);
        if (f)
            fclose(f);
        if (ngdlogs < 256)
            ngdlogs++;
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    gdf[k].data = (uint8_t *)malloc((size_t)size + 1);
    size = (long)fread(gdf[k].data, 1, (size_t)size, f);
    fclose(f);
    gdf[k].used = 1;
    gdf[k].size = size;
    gdf[k].pos = 0;
    snprintf(gdf[k].name, sizeof gdf[k].name, "%s", name);
    if (ngdlogs < 256)
        gdlogs[ngdlogs++].size = size;
    if (sim_verbose)
        printf("  gdFsOpen(\"%s\") -> %ld bytes\n", name, size);
    c->r[0] = GDFS_HANDLES + (uint32_t)k * 16;
}

static gdfile *gd_handle(uint32_t h)
{
    uint32_t k;
    if (h < GDFS_HANDLES || h >= GDFS_HANDLES + GDFS_MAX * 16 || (h & 15))
        return NULL;
    k = (h - GDFS_HANDLES) / 16;
    return gdf[k].used ? &gdf[k] : NULL;
}

static void hle_gdfs_size(sh4_cpu *c, void *u)
{
    gdfile *g = gd_handle(c->r[4]);
    (void)u;
    c->r[0] = 0;
    if (g && sh4_is_ram(c->r[5])) {
        sh4_write32(c->r[5], (uint32_t)g->size);
        c->r[0] = 1;
    }
}

static void hle_gdfs_read(sh4_cpu *c, void *u)
{
    gdfile *g = gd_handle(c->r[4]);
    long want = (long)c->r[5] * 2048, n;
    (void)u;
    c->r[0] = (uint32_t)-1;
    if (!g || (int32_t)c->r[5] < 0)
        return;
    n = g->size - g->pos;
    if (n > want)
        n = want;
    if (guest_copy_in(c->r[6], g->data + g->pos, n)) {
        printf("  gdFsRead(%s): destination %08X outside RAM\n", g->name, c->r[6]);
        return;
    }
    g->pos += want;
    c->r[0] = 0;
    guard_epoch++;
}

static void hle_gdfs_close(sh4_cpu *c, void *u)
{
    gdfile *g = gd_handle(c->r[4]);
    (void)u;
    if (g) {
        free(g->data);
        memset(g, 0, sizeof *g);
    }
    c->r[0] = 0;
    guard_epoch++;
}

/* ------------------------------------------------------------------ */
/* pad HLE: pdGetPeripheral(port) returns a standard controller on port A
 * whose state comes from the input script for the current logic step     */

#define PDS_PORT0 0x8C000400u
#define PDS_NONE  0x8C000480u
static int16_t pad_x, pad_y;
static uint32_t pad_buttons;

static void hle_get_peripheral(sh4_cpu *c, void *u)
{
    (void)u;
    if (c->r[4] == 0) {
        uint32_t old = sh4_read32(PDS_PORT0 + 8);
        sh4_write32(PDS_PORT0 + 0x00, 0x00000001u);          /* id: controller */
        sh4_write32(PDS_PORT0 + 0x04, 0x000F06FEu);          /* support: digital + analog */
        sh4_write32(PDS_PORT0 + 0x08, pad_buttons);          /* on */
        sh4_write32(PDS_PORT0 + 0x0C, ~pad_buttons);         /* off */
        sh4_write32(PDS_PORT0 + 0x10, pad_buttons & ~old);   /* press */
        sh4_write32(PDS_PORT0 + 0x14, old & ~pad_buttons);   /* release */
        sh4_write16(PDS_PORT0 + 0x18, 0);                    /* r trigger */
        sh4_write16(PDS_PORT0 + 0x1A, 0);                    /* l trigger */
        sh4_write16(PDS_PORT0 + 0x1C, (uint16_t)pad_x);
        sh4_write16(PDS_PORT0 + 0x1E, (uint16_t)pad_y);
        sh4_write16(PDS_PORT0 + 0x20, 0);
        sh4_write16(PDS_PORT0 + 0x22, 0);
        sh4_write32(PDS_PORT0 + 0x2C, old);                  /* old */
        c->r[0] = PDS_PORT0;
    } else {
        c->r[0] = PDS_NONE;     /* all zero: id 0, no capabilities = nothing connected */
    }
}

/* frame timer (8C06C1B2 reads TMU1 TCNT): "no time has passed", so the
 * frame loop 8C04E95A never adds catch-up steps */
static void hle_frame_time(sh4_cpu *c, void *u)
{
    (void)u;
    c->r[0] = 0;
    c->fr.f[0] = 0.0f;
}

/* ------------------------------------------------------------------ */
/* explicit stub list: hardware-facing functions that the simulator    */
/* replaces (returns 0 unless noted).  Found with --discover and then   */
/* checked by hand; see docs/harness_simulator.md.                      */

typedef struct sim_stub { uint32_t addr; uint32_t ret; const char *name; const char *why; } sim_stub;
static const sim_stub sim_stubs[] = {
    /* video */
    { 0x8C604594u, 0, "vsync wait (SDK)", "polls the vblank counter" },
    { 0x8C604702u, 0, "njInitSystem (Ninja; --no-screen only)",
      "display / Kamui / PVR set-up, busy-waits on Holly 0xA05F6900; by default replaced by its view part "
      "(hle_nj_init_system), with --no-screen a no-op that leaves the screen at 0" },
    { 0x8C051790u, 0, "frame-end vsync wait + display flip", "waits for B_8C754EC0, then Kamui render" },
    { 0x8C037108u, 0, "land strip renderer (SH-4 asm, called only by the land draw 8C051E56)",
      "rendering: reads strip data through Kamui/renderer state that is not set up" },
    /* timers: 8C06C182/8C06C15A read TMU1 TCNT (0xFFD80018) for the debug timing display;
     * the frame-time check 8C06C1B2 is hle_frame_time.  Native (decompiled) copies of these
     * would fault on the hardware address in --route runs. */
    { 0x8C06C182u, 0, "debug timer start (TMU1 TCNT -> B_8C78C600)", "hardware timer" },
    { 0x8C06C15Au, 0, "debug timer delta (TMU1 TCNT)", "hardware timer" },
    /* peripherals */
    { 0x8C601E2Au, 0, "pdExecPeripheralServer", "maple DMA (the pad is HLE'd at pdGetPeripheral)" },
    /* sound */
    { 0x8C65A5DCu, 0, "sound driver handshake (SDK, from the sound init 8C092FA0)", "waits for the AICA" },
    { 0x8C10C1C8u, 0xFFFFFFFFu, "AFS partition open (voice ADX, BGM task 8C107864)",
      "streams event_adx.afs through ADX/GD-ROM, busy-waits; -1 = failure" },
    { 0, 0, NULL, NULL }
};

/* ------------------------------------------------------------------ */
/* step runner with fault report / discovery                           */

typedef struct { uint32_t addr; char why[200]; } stub_rec;
static stub_rec dstubs[256];
static int ndstubs;
static uint8_t *snap;
static int discover;

static const char *fname(uint32_t a)
{
    static char buf[4][64];
    static int k;
    const char *n = sh4_sym_name(a);
    k = (k + 1) & 3;
    if (n)
        snprintf(buf[k], sizeof buf[k], "%s", n);
    else
        snprintf(buf[k], sizeof buf[k], "%08X", a);
    return buf[k];
}

static int is_dstubbed(uint32_t a)
{
    int i;
    for (i = 0; i < ndstubs; i++)
        if (dstubs[i].addr == a)
            return 1;
    return 0;
}

static uint32_t choose_stub(void)
{
    int d = sh4_cstack_depth < SH4_CSTACK_MAX ? sh4_cstack_depth : SH4_CSTACK_MAX;
    int i;
    for (i = d - 1; i >= 1; i--)
        if (sh4_cstack[i].target >= LIB_START && sh4_in_ram(sh4_cstack[i].target) && !is_dstubbed(sh4_cstack[i].target))
            return sh4_cstack[i].target;
    for (i = d - 1; i >= 1; i--)
        if (sh4_in_ram(sh4_cstack[i].target) && !is_dstubbed(sh4_cstack[i].target) &&
            sh4_cstack[i].target != STEP_FN)
            return sh4_cstack[i].target;
    return 0;
}

static void step_hook(sh4_cpu *c, void *u);
static int route_select(const char *only, const char *except);
static int step_hook_on;
static void (*retry_reset)(void);

static int sim_run(const char *what, uint32_t addr, const sh4_arg *args, int nargs, uint64_t budget)
{
    sh4_cpu save;
    int attempt;
    if (discover) {
        memcpy(snap, sh4_ram(), SH4_RAM_SIZE);
        memset(sh4_ram_dirty, 0, sizeof sh4_ram_dirty);
    }
    save = SH4;
    for (attempt = 0; attempt < 64; attempt++) {
        uint32_t target, r = 0;
        float f;
        int k, i, d;
        sh4_cstack_depth = 0;
        SH4.ilimit = SH4.icount + budget;
        k = sh4_invoke(addr, args, nargs, &r, &f);
        SH4.ilimit = 0;
        if (step_hook_on)
            sh4_hook_add(STEP_FN, step_hook, NULL, "logic step");
        if (k == SH4_OK)
            return 0;
        d = sh4_cstack_depth < SH4_CSTACK_MAX ? sh4_cstack_depth : SH4_CSTACK_MAX;
        {
            uint32_t off;
            const char *nearest = sh4_sym_near(SH4.fault.pc, &off);
            printf("  [%s] fault: %s at %08X (%s+%X): %s\n", what, sh4_fault_name(k), SH4.fault.pc,
                   nearest ? nearest : "?", off, SH4.fault.msg);
        }
        printf("        call stack:");
        for (i = 0; i < d; i++)
            printf(" %s", fname(sh4_cstack[i].target));
        printf("\n");
        if (!discover)
            return k;
        target = choose_stub();
        if (!target || ndstubs >= 256) {
            printf("  [%s] giving up: nothing left to stub\n", what);
            return k;
        }
        dstubs[ndstubs].addr = target;
        snprintf(dstubs[ndstubs].why, sizeof dstubs[ndstubs].why, "%s: %s", sh4_fault_name(k), SH4.fault.msg);
        ndstubs++;
        sh4_hook_add(target, sh4_hle_ret0, NULL, "autostub");
        printf("        -> stub %s (returns 0)\n", fname(target));
        harness_ram_restore(snap);
        memset(sh4_ram_dirty, 0, sizeof sh4_ram_dirty);
        {
            uint64_t ic = SH4.icount;
            SH4 = save;
            SH4.icount = ic;
        }
        if (retry_reset)
            retry_reset();
    }
    return -1;
}
/* ------------------------------------------------------------------ */
/* per-step bracket: the logic step 8C04E714 is hooked; the hook sets   */
/* the pad for this step, runs the original step and records the state */

static int cur_step, cur_frame, frame_step0, frame_nrec0;
static int sim_want_steps = 0x7FFFFFFF;  /* steps the trace keeps (the last frame may run one more) */
static sa1_trace_rec *recs;
static int nrecs, caprecs;
static uint64_t step_icount;

static void close_all_files(void)
{
    int k;
    for (k = 0; k < GDFS_MAX; k++)
        if (gdf[k].used) {
            free(gdf[k].data);
            memset(&gdf[k], 0, sizeof gdf[k]);
        }
}

static void d2d_retry(void);           /* --log-2d: drop the retried frame's lines */

static void frame_retry_reset(void)
{
    cur_step = frame_step0;
    nrecs = frame_nrec0;
    d2d_retry();
    close_all_files();
}

static void copy_guest(uint8_t *dst, uint32_t a, int n)
{
    int i;
    if (!sh4_is_ram(a) || !sh4_is_ram(a + (uint32_t)n - 1)) {
        memset(dst, 0, (size_t)n);
        return;
    }
    for (i = 0; i < n; i++)
        dst[i] = sh4_read8(a + (uint32_t)i);
}

static uint32_t rd32le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16le(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static float rdfle(const uint8_t *p) { uint32_t u = rd32le(p); float f; memcpy(&f, &u, 4); return f; }

static void make_dirs(const char *path);

/* the player fields of a record for player slot `slot` (0: the main trace, 1: the --pno1 sidecar) */
static void fill_record_slot(sa1_trace_rec *r, int slot)
{
    uint32_t so = 4u * (uint32_t)slot;
    uint32_t ptp = sh4_read32(PLAYER_TP + so);
    uint32_t twp = sh4_read32(PLAYER_TWP + so), mwp = sh4_read32(PLAYER_MWP + so), pwp = sh4_read32(PLAYER_PWP + so), cst;
    int i;
    if (!sh4_is_ram(pwp) && sh4_is_ram(mwp))
        pwp = sh4_read32(mwp);
    cst = sh4_is_ram(pwp) ? sh4_read32(pwp + 0x64) : 0;
    copy_guest(r->twp_raw, twp, 0x40);
    copy_guest(r->mwp_raw, mwp, 0x40);
    copy_guest(r->pwp_raw, pwp, 0x200);
    copy_guest(r->csts_raw, cst, 0x84);
    r->mode = (int8_t)r->twp_raw[0];
    r->smode = (int8_t)r->twp_raw[1];
    r->id = (int8_t)r->twp_raw[2];
    r->btimer = (int8_t)r->twp_raw[3];
    r->twp_flag = (int16_t)rd16le(r->twp_raw + 4);
    r->wtimer = rd16le(r->twp_raw + 6);
    for (i = 0; i < 3; i++) {
        r->ang[i] = rd32le(r->twp_raw + 0x14 + 4 * i);
        r->pos[i] = rdfle(r->twp_raw + 0x20 + 4 * i);
        r->mspd[i] = rdfle(r->mwp_raw + 0x04 + 4 * i);
        r->macc[i] = rdfle(r->mwp_raw + 0x10 + 4 * i);
        r->ang_aim[i] = rd32le(r->mwp_raw + 0x1C + 4 * i);
        r->pacc[i] = rdfle(r->pwp_raw + 0x2C + 4 * i);
        r->pspd[i] = rdfle(r->pwp_raw + 0x38 + 4 * i);
        r->floor_normal[i] = rdfle(r->pwp_raw + 0x50 + 4 * i);
    }
    r->dotp = rdfle(r->pwp_raw + 0x1C);
    r->attr = rd32le(r->pwp_raw + 0x5C);
    r->last_attr = rd32le(r->pwp_raw + 0x60);
    r->equipment = rd16le(r->pwp_raw + 0x04);
    r->item = rd16le(r->pwp_raw + 0x06);
    r->jumptimer = (int16_t)rd16le(r->pwp_raw + 0x08);
    r->nocontimer = (int16_t)rd16le(r->pwp_raw + 0x0A);
    r->waittimer = (int16_t)rd16le(r->pwp_raw + 0x0E);
    r->pwp_flag = (int16_t)rd16le(r->pwp_raw + 0x10);
    r->reqaction = rd16le(r->pwp_raw + 0x124);
    r->action = rd16le(r->pwp_raw + 0x126);
    r->lastaction = rd16le(r->pwp_raw + 0x128);
    r->nextaction = rd16le(r->pwp_raw + 0x12A);
    r->nframe = rdfle(r->pwp_raw + 0x130);
    r->land_count = sh4_read32(LAND_COUNT);
    r->rand_state = sh4_read32(RAND_STATE);
    /* v2: globals after the step, blob addresses */
    r->frame_counter = sh4_read32(FRAME_COUNTER);
    r->rings = (int16_t)sh4_read16(RINGS);
    for (i = 0; i < 3; i++)
        memcpy(&r->gravity[i], &(uint32_t){ sh4_read32(GRAVITY + 4 * (uint32_t)i) }, 4);
    r->ptp_addr = ptp;
    r->twp_addr = twp;
    r->mwp_addr = mwp;
    r->pwp_addr = pwp;
    r->csts_addr = cst;
}

static void fill_record(sa1_trace_rec *r)
{
    fill_record_slot(r, 0);
}

/* ------------------------------------------------------------------ */
/* trace v2: the state the player code starts from.  Hooks on the       */
/* character mains (and on whatever playertp[0]->exec, B_8C78C648[0]    */
/* +0x10, points to) capture, at the first entry for the player-0 task  */
/* in a logic step, the rand state, XMTRX, the camera taskwk, frame     */
/* counter, rings and Gravity, then run the exec unchanged: the         */
/* original in the interpreter (the hook steps aside for the call) or,  */
/* when native routing had a hook there, that hook (the routed native   */
/* exec).  The player-0 task is playertp[0]; before its main's first    */
/* run (PInitialize fills playertp[]) the task whose taskwk has player  */
/* number counter.b[0] == 0.                                            */

typedef struct px_ent {
    uint32_t addr;
    sh4_hook_fn prev_fn;                /* the hook it replaced (native routing), chained */
    void *prev_user;
    const char *prev_name;
} px_ent;
static px_ent px_ents[32];
static int n_px;
static int px_in_step;                  /* inside the hooked logic step */
static sa1_trace_rec px_stage;          /* px_* fields of the current step */
static int px_pno1;                     /* --pno1: also player 1's entry (the sidecar) */
static sa1_trace_rec px_stage1;         /* player 1's px_* fields of the current step */
static uint32_t px_first_pno = 0xFFFFFFFFu, px_first_rand;  /* the first player exec entry of the step */

/* SonicMain, TailsMain, KnucklesMain, AmyMain, E102Main, BigMain (LoadPlayerTask 8C04F3C0) */
static const uint32_t px_mains[] = { 0x8C0A77BCu, 0x8C0B6148u, 0x8C0C0640u, 0x8C0D6B40u, 0x8C0CBE00u, 0x8C0DFFA0u };

static void px_capture_into(sa1_trace_rec *s)
{
    int i;
    s->px_rand = sh4_read32(RAND_STATE);
    s->px_frame_counter = sh4_read32(FRAME_COUNTER);
    s->px_rings = (int16_t)sh4_read16(RINGS);
    for (i = 0; i < 3; i++)
        memcpy(&s->px_gravity[i], &(uint32_t){ sh4_read32(GRAVITY + 4 * (uint32_t)i) }, 4);
    memcpy(s->px_xmtrx, SH4.xf.u, sizeof s->px_xmtrx);
    s->px_cam_twp = sh4_read32(CAMERA_TWP);
    copy_guest(s->px_cam_raw, s->px_cam_twp, 0x40);
}

static int px_is_player(uint32_t tp, int slot)
{
    uint32_t p = sh4_read32(PLAYER_TP + 4u * (uint32_t)slot), twp;
    if (sh4_is_ram(p))
        return tp == p;
    twp = sh4_is_ram(tp) ? sh4_read32(tp + 0x20) : 0;
    return sh4_is_ram(twp) && sh4_read8(twp + 8) == (uint8_t)slot;
}

static int px_is_player0(uint32_t tp)
{
    return px_is_player(tp, 0);
}

static void px_hook(sh4_cpu *c, void *u)
{
    px_ent *e = (px_ent *)u;
    if (px_in_step && px_first_pno == 0xFFFFFFFFu) {
        uint32_t twp = sh4_is_ram(c->r[4]) ? sh4_read32(c->r[4] + 0x20) : 0;
        px_first_pno = px_is_player0(c->r[4]) ? 0u : px_is_player(c->r[4], 1) ? 1u :
                       sh4_is_ram(twp) ? (uint32_t)sh4_read8(twp + 8) : 0xFFFFFFFEu;
        px_first_rand = sh4_read32(RAND_STATE);
    }
    if (px_in_step && px_is_player0(c->r[4]) && px_stage.px_calls++ == 0) {
        px_stage.px_exec = e->addr;
        px_capture_into(&px_stage);
    } else if (px_in_step && px_pno1 && px_is_player(c->r[4], 1) && px_stage1.px_calls++ == 0) {
        px_stage1.px_exec = e->addr;
        px_capture_into(&px_stage1);
    }
    if (e->prev_fn) {                   /* routed: the native exec through its adapter */
        e->prev_fn(c, e->prev_user);
        return;
    }
    /* the original, with the registers as they are (R4 = task); a fault
     * unwinds past the re-add, px_install re-hooks at the next step */
    sh4_hook_remove(e->addr);
    sh4_invoke(e->addr, NULL, 0, NULL, NULL);
    sh4_hook_add(e->addr, px_hook, e, "player exec (trace v2)");
}

static void px_hook_at(uint32_t a)
{
    int i;
    sh4_hook_fn fn = NULL;
    px_ent *e = NULL;
    if (!sh4_is_ram(a) || (a & 1))
        return;
    for (i = 0; i < n_px; i++)
        if (px_ents[i].addr == a)
            e = &px_ents[i];
    if (e && sh4_hook_find(a, &fn, NULL, NULL) && fn == px_hook)
        return;                         /* installed */
    if (!e) {
        if (n_px == (int)(sizeof px_ents / sizeof px_ents[0]))
            return;
        e = &px_ents[n_px++];
        memset(e, 0, sizeof *e);
        e->addr = a;
        if (sh4_hook_find(a, &fn, &e->prev_user, &e->prev_name))
            e->prev_fn = fn;
    }
    sh4_hook_add(a, px_hook, e, "player exec (trace v2)");   /* new, or again after a fault */
}

/* every step: the mains, and the current exec of playertp[0] (a native
 * function pointer is mapped to its guest address) */
static void px_install(void)
{
    uint32_t tp = sh4_read32(PLAYER_TP);
    int i;
    for (i = 0; i < (int)(sizeof px_mains / sizeof px_mains[0]); i++)
        px_hook_at(px_mains[i]);
    for (i = 0; i < n_px; i++)
        px_hook_at(px_ents[i].addr);
    if (sh4_is_ram(tp))
        px_hook_at(sh4_canon_code_ptr(sh4_read32(tp + 0x10)));
    tp = sh4_read32(PLAYER_TP + 4);
    if (px_pno1 && sh4_is_ram(tp))
        px_hook_at(sh4_canon_code_ptr(sh4_read32(tp + 0x10)));
}

static void px_uninstall(void)
{
    int i;
    for (i = 0; i < n_px; i++) {
        px_ent *e = &px_ents[i];
        sh4_hook_fn fn = NULL;
        if (!sh4_hook_find(e->addr, &fn, NULL, NULL) || fn != px_hook)
            continue;
        if (e->prev_fn)
            sh4_hook_add(e->addr, e->prev_fn, e->prev_user, e->prev_name);
        else
            sh4_hook_remove(e->addr);
    }
    n_px = 0;
}

/* ------------------------------------------------------------------ */
/* --log-sdk FILE: every call from game code into the SDK (Ninja,       */
/* Kamui, Shinobi, ...) and the C runtime, one line per call, in the    */
/* text format libsa1's SA1_DEBUG_SDKLOG writes (harness_simulator.md): */
/*     <phase> <step> <seq> <ADDR> <name>(<args>) -> <ret>               */
/* Only boundary crossings are logged (SDK code calling SDK code, and   */
/* game callbacks run by the SDK, are not).  The functions come from    */
/* build/verify/sdk_boundary.txt (harness/tools/sdk_boundary.py:        */
/* address, name, signature); without it, from the thunk/native tables  */
/* of this build.  Arguments are printed per signature: integers and    */
/* pointers as hex, floats as f<bits>; unknown signature: raw R4-R7 and */
/* FR4-FR7.                                                             */

typedef struct sdk_ent {
    uint32_t addr;
    char name[48];
    char sig[96];                       /* "ret:arg,.." ("" = unknown) */
    sh4_hook_fn prev_fn;                /* hook that was there (HLE stub, native routing): chained */
    void *prev_user;
    const char *prev_name;
} sdk_ent;
static sdk_ent *sdk_ents;
static int n_sdk;
static FILE *sdk_fp;
static int sdk_depth;
static unsigned sdk_seq;
static char sdk_phase = 'F';
static int sdk_step = -1;

static void set_hookbit(uint32_t addr, int on)
{
    uint32_t hw = (addr & 0x00FFFFFFu) >> 1;
    if (on)
        sh4_hookbits[hw >> 3] |= (uint8_t)(1u << (hw & 7));
    else
        sh4_hookbits[hw >> 3] &= (uint8_t)~(1u << (hw & 7));
}

static int sig_is_float(const char *t) { return !strcmp(t, "f32") || !strcmp(t, "f64"); }

/* merge signature b into a (variants of one function from different
 * prototypes): the longer parameter list; a float wins at a position;
 * a non-void return wins, a float return over an integer one */
static void sig_merge(char *a, size_t n, const char *b)
{
    char ra[8], rb[8], pa[24][8], pb[24][8], out[160];
    int na, nb, i, k;
    if (!b || !*b)
        return;
    if (!*a) {
        snprintf(a, n, "%s", b);
        return;
    }
    na = sh4_sig_parse(a, ra, pa);
    nb = sh4_sig_parse(b, rb, pb);
    if (na < 0 || nb < 0)
        return;
    if (!strcmp(ra, "v") || (sig_is_float(rb) && !sig_is_float(ra)))
        snprintf(ra, sizeof ra, "%s", rb);
    for (i = 0; i < nb; i++) {
        if (i >= na)
            snprintf(pa[i], sizeof pa[i], "%s", pb[i]);
        else if (sig_is_float(pb[i]) && !sig_is_float(pa[i]))
            snprintf(pa[i], sizeof pa[i], "%s", pb[i]);
    }
    if (nb > na)
        na = nb;
    k = snprintf(out, sizeof out, "%s:", ra);
    for (i = 0; i < na && k < (int)sizeof out - 10; i++)
        k += snprintf(out + k, sizeof out - (size_t)k, "%s%s", i ? "," : "", pa[i]);
    snprintf(a, n, "%s", out);
}

static void sdk_table_sig(uint32_t addr, char *sig, size_t n)
{
    int i;
    for (i = 0; i < sh4_thunk_count; i++)
        if ((sh4_thunks[i].guest & 0x1FFFFFFFu) == (addr & 0x1FFFFFFFu))
            sig_merge(sig, n, sh4_thunks[i].sig);
    for (i = 0; i < sh4_native_fn_count; i++)
        if ((sh4_native_fns[i].guest & 0x1FFFFFFFu) == (addr & 0x1FFFFFFFu))
            sig_merge(sig, n, sh4_native_fns[i].sig);
}

static void sdk_args(char *buf, size_t n, const sdk_ent *e, const sh4_cpu *c)
{
    char ret[8], params[24][8];
    int np = e->sig[0] ? sh4_sig_parse(e->sig, ret, params) : -1;
    int i, k = 0, ni = 0, nf = 0, ns = 0;
    buf[0] = 0;
    if (np < 0) {
        snprintf(buf, n, "r4=%X,r5=%X,r6=%X,r7=%X,fr4=f%08X,fr5=f%08X,fr6=f%08X,fr7=f%08X", c->r[4], c->r[5], c->r[6],
                 c->r[7], c->fr.u[4], c->fr.u[5], c->fr.u[6], c->fr.u[7]);
        return;
    }
    for (i = 0; i < np && k < (int)n - 24; i++) {
        uint32_t v;
        const char *sep = i ? "," : "";
        if (!strcmp(params[i], "...")) {
            k += snprintf(buf + k, n - (size_t)k, "%s...", sep);
            break;
        }
        if (sig_is_float(params[i])) {
            v = nf < 8 ? c->fr.u[4 + nf++] : sh4_read32(c->r[15] + 4u * (uint32_t)ns++);
            k += snprintf(buf + k, n - (size_t)k, "%sf%08X", sep, v);
        } else {
            v = ni < 4 ? c->r[4 + ni++] : sh4_read32(c->r[15] + 4u * (uint32_t)ns++);
            k += snprintf(buf + k, n - (size_t)k, "%s%X", sep, sh4_extend(params[i], v));
        }
    }
}

static void sdk_ret(char *buf, size_t n, const sdk_ent *e, const sh4_cpu *c)
{
    char ret[8], params[24][8];
    int np = e->sig[0] ? sh4_sig_parse(e->sig, ret, params) : -1;
    if (np < 0)
        snprintf(buf, n, "r0=%X,fr0=f%08X", c->r[0], c->fr.u[0]);
    else if (!strcmp(ret, "v"))
        snprintf(buf, n, "-");
    else if (sig_is_float(ret))
        snprintf(buf, n, "f%08X", c->fr.u[0]);
    else
        snprintf(buf, n, "%X", sh4_extend(ret, c->r[0]));
}

static void sdk_hook(sh4_cpu *c, void *u)
{
    sdk_ent *e = (sdk_ent *)u;
    char args[400], ret[48];
    int top = sdk_depth == 0;
    if (top && sdk_fp)
        sdk_args(args, sizeof args, e, c);
    sdk_depth++;
    if (e->prev_fn) {
        e->prev_fn(c, e->prev_user);
    } else {
        set_hookbit(e->addr, 0);        /* the original runs unhooked (a fault leaves it so: sdk_rearm) */
        sh4_invoke(e->addr, NULL, 0, NULL, NULL);
        set_hookbit(e->addr, 1);
    }
    sdk_depth--;
    if (top && sdk_fp && sdk_step < sim_want_steps) {
        sdk_ret(ret, sizeof ret, e, c);
        fprintf(sdk_fp, "%c %d %u %08X %s(%s) -> %s\n", sdk_phase, sdk_step, sdk_seq++, e->addr, e->name, args, ret);
    }
}

static int sdk_cmp(const void *a, const void *b)
{
    uint32_t x = ((const sdk_ent *)a)->addr, y = ((const sdk_ent *)b)->addr;
    return x < y ? -1 : x > y;
}

static int sdk_install(const sim_cfg *cfg)
{
    char path[1300], line[512];
    FILE *f;
    int cap = 4096, i, from_list = 0;
    sdk_ents = (sdk_ent *)calloc((size_t)cap, sizeof(sdk_ent));
    if (cfg->sdk_list)
        snprintf(path, sizeof path, "%s", cfg->sdk_list);
    else
        snprintf(path, sizeof path, "%s/build/verify/sdk_boundary.txt", g_root);
    f = fopen(path, "r");
    if (f) {
        from_list = 1;
        while (fgets(line, sizeof line, f) && n_sdk < cap) {
            unsigned a;
            char name[64] = "", sig[128] = "";
            if (line[0] == '#' || sscanf(line, "%x %63s %127s", &a, name, sig) < 2)
                continue;
            sdk_ents[n_sdk].addr = a;
            snprintf(sdk_ents[n_sdk].name, sizeof sdk_ents[n_sdk].name, "%s", name);
            if (strcmp(sig, "?"))
                snprintf(sdk_ents[n_sdk].sig, sizeof sdk_ents[n_sdk].sig, "%s", sig);
            n_sdk++;
        }
        fclose(f);
    } else if (cfg->sdk_list) {
        fprintf(stderr, "log-sdk: cannot open %s\n", path);
        return -1;
    } else {
        /* no list: the SDK / C runtime functions the decompiled code of this build calls */
        for (i = 0; i < sh4_thunk_count + sh4_native_fn_count && n_sdk < cap; i++) {
            uint32_t a = i < sh4_thunk_count ? sh4_thunks[i].guest : sh4_native_fns[i - sh4_thunk_count].guest;
            const char *nm;
            int k, dup = 0;
            if (!((a >= LIB_START && sh4_is_ram(a)) || (a >= CRT_START && a < CRT_END)))
                continue;
            for (k = 0; k < n_sdk; k++)
                dup |= sdk_ents[k].addr == a;
            if (dup)
                continue;
            nm = sh4_sym_name(a);
            sdk_ents[n_sdk].addr = a;
            if (nm)
                snprintf(sdk_ents[n_sdk].name, sizeof sdk_ents[n_sdk].name, "%s", nm);
            else
                snprintf(sdk_ents[n_sdk].name, sizeof sdk_ents[n_sdk].name, "func_%08X", a);
            n_sdk++;
        }
    }
    qsort(sdk_ents, (size_t)n_sdk, sizeof(sdk_ent), sdk_cmp);
    for (i = 0; i < n_sdk; i++) {
        sdk_ent *e = &sdk_ents[i];
        sh4_hook_fn fn = NULL;
        if (!sh4_is_ram(e->addr) || (e->addr & 1) || (i && sdk_ents[i - 1].addr == e->addr)) {
            e->addr = 0;
            continue;
        }
        if (!e->sig[0])
            sdk_table_sig(e->addr, e->sig, sizeof e->sig);
        if (sh4_hook_find(e->addr, &fn, &e->prev_user, &e->prev_name))
            e->prev_fn = fn;
        sh4_hook_add(e->addr, sdk_hook, e, e->name);
    }
    sdk_fp = fopen(cfg->log_sdk, "w");
    if (!sdk_fp) {
        fprintf(stderr, "log-sdk: cannot write %s\n", cfg->log_sdk);
        return -1;
    }
    fprintf(sdk_fp, "# sa1 sdk call log v1: level %d act %d chr %d; %d boundary functions from %s\n", cfg->level,
            cfg->act, cfg->chr, n_sdk, from_list ? path : "the thunk/native tables of this build");
    fprintf(sdk_fp, "# <phase> <step> <seq> <ADDR> <name>(<args>) -> <ret>; phase S = inside logic step <step>, "
                    "F = frame code after step <step>, D = display pass after step <step>\n");
    printf("sim: logging calls into %d SDK/runtime functions -> %s\n", n_sdk, cfg->log_sdk);
    return 0;
}

/* after a fault: unhooked originals and a stale nesting depth */
static void sdk_rearm(void)
{
    int i;
    sdk_depth = 0;
    for (i = 0; i < n_sdk; i++)
        if (sdk_ents[i].addr && !sdk_ents[i].prev_fn)
            set_hookbit(sdk_ents[i].addr, 1);
}

static void sdk_phase_set(char phase, int step)
{
    if (phase != sdk_phase || step != sdk_step)
        sdk_seq = 0;
    sdk_phase = phase;
    sdk_step = step;
}

static void sdk_uninstall(void)
{
    int i;
    for (i = 0; i < n_sdk; i++) {
        sdk_ent *e = &sdk_ents[i];
        sh4_hook_fn fn = NULL;
        if (!e->addr || !sh4_hook_find(e->addr, &fn, NULL, NULL) || fn != sdk_hook)
            continue;
        if (e->prev_fn)
            sh4_hook_add(e->addr, e->prev_fn, e->prev_user, e->prev_name);
        else
            sh4_hook_remove(e->addr);
    }
    if (sdk_fp)
        fclose(sdk_fp);
    sdk_fp = NULL;
    n_sdk = 0;
}


/* ------------------------------------------------------------------ */
/* --log-2d FILE: the game's 2D draws (docs/research/gsa1_070_refs.md; */
/* gsa1_070_hud.md F7): njDrawSprite2D 8C63EEB4 and njDrawPolygon2D    */
/* 8C63EBC8, one line per call with what the call reads (the sprite    */
/* and its NJS_TEXANIM[n], the point list) and the draw state the game */
/* set before it, in the terms of libsa1's capture records             */
/* (libsa1/src/sa1_capture.h: texlist, ctl3d, attr_and / attr_or,      */
/* cmat, blend; the sprite payload = the NJS_SPRITE's 0x20 bytes + its */
/* texanim) plus what the HUD round adds (pri, the running exec        */
/* D_8C1C1D40) and the caller (PR).  The hook writes the line and then */
/* runs the original (or the hook it replaced, chained like --log-sdk),*/
/* so the run is unchanged.  Lines are kept per frame in memory (a     */
/* retried frame drops its own) and written at the end, after a header */
/* that names the columns.  Floats are bit patterns (8 hex digits);    */
/* harness/tools/draw2d.py decodes, prints and compares the files.     */

#define D2D_SPRITE2D  0x8C63EEB4u   /* njDrawSprite2D(NJS_SPRITE *sp, Int n, Float pri, Uint32 attr) */
#define D2D_POLY2D    0x8C63EBC8u   /* njDrawPolygon2D(NJS_POINT2COL *p, Int n, Float pri, Uint32 attr) */
#define D2D_EXEC      0x8C1C1D40u   /* D_8C1C1D40: the exec the task executor is running (0 outside it) */
#define D2D_TEXLIST   0x8C673480u   /* D_8C673480: Ninja's current texlist */
#define D2D_CTL3D     0x8C88F62Cu   /* B_8C88F62C */
#define D2D_ATTR_AND  0x8C88F660u   /* B_8C88F660 / B_8C88F664: njSetConstantAttr */
#define D2D_CMAT      0x8C88F668u   /* B_8C88F668[4]: njSetConstantMaterial (a, r, g, b as float bits) */
#define D2D_KMCTX     0x8C88FCB4u   /* B_8C88FCB4: the Kamui vertex context; +0x2C / +0x30 the blend modes */
#define D2D_MAXPTS    64
/* round 0.9.2 (gsa1_090_charselect.md CS2 with its Review RC6): the menu screens' 2D goes through njDrawTexture and
 * njDrawPolygon (the 3D-path quads with screen positions, 1/w and a PVR global index), logged as kinds T and G */
#define D2D_TEXTURE   0x8C63ED08u   /* njDrawTexture(NJS_TEXTURE_VTX *v {x y z u v col}, Int n, Int tex (GBIX), Int flag) */
#define D2D_POLYGON   0x8C63EC58u   /* njDrawPolygon(NJS_POLYGON_VTX *v {x y z col}, Int n, Int trans) */
#define D2D_NHOOK     4

static int d2d_on, d2d_disp;
static int d2d_menu = -1;       /* the menu command: its frame (phase M), -1 in a trace */
static int d2d_noexec;          /* the menu command: log the draw and do not run it (the draw writes only the TA) */
static char *d2d_buf;
static size_t d2d_len, d2d_cap, d2d_mark;
static unsigned d2d_seq, d2d_count, d2d_mark_count;
static char d2d_phase_last;
static int d2d_step_last = -2;
static sh4_hook_fn d2d_prev_fn[D2D_NHOOK];
static void *d2d_prev_user[D2D_NHOOK];
static const char *d2d_prev_name[D2D_NHOOK];
static const uint32_t d2d_addr[D2D_NHOOK] = { D2D_SPRITE2D, D2D_POLY2D, D2D_TEXTURE, D2D_POLYGON };
static const char *const d2d_hname[D2D_NHOOK] = { "log-2d njDrawSprite2D", "log-2d njDrawPolygon2D",
                                                  "log-2d njDrawTexture", "log-2d njDrawPolygon" };

static void d2d_put(const char *fmt, ...)
{
    va_list ap;
    int n;
    for (;;) {
        if (d2d_cap - d2d_len < 512) {
            d2d_cap = d2d_cap ? d2d_cap * 2 : ((size_t)1 << 20);
            d2d_buf = (char *)realloc(d2d_buf, d2d_cap);
        }
        va_start(ap, fmt);
        n = vsnprintf(d2d_buf + d2d_len, d2d_cap - d2d_len, fmt, ap);
        va_end(ap);
        if (n >= 0 && (size_t)n < d2d_cap - d2d_len) {
            d2d_len += (size_t)n;
            return;
        }
        d2d_cap *= 2;
        d2d_buf = (char *)realloc(d2d_buf, d2d_cap);
    }
}

/* guest RAM, or (round 0.9.2) host memory a routed native caller passed (its stack: the vertex lists of a native 2D
 * helper; sh4_host_readable, the interpreter's pass-through) */
static int d2d_ok(uint32_t a) { return sh4_is_ram(a) || sh4_host_readable(a, 4); }
static uint32_t d2d_rd(uint32_t a) { return d2d_ok(a) ? sh4_read32(a) : 0u; }
static int d2d_rd16(uint32_t a) { return sh4_is_ram(a) || sh4_host_readable(a, 2) ? (int)(int16_t)sh4_read16(a) : 0; }

/* the common columns: kind phase step seq frame sub exec pr obj n pri attr texlist cmat[4] blend ctl3d attr_and
 * attr_or */
static void d2d_common_pa(const sh4_cpu *c, char kind, int step, char phase, uint32_t pri, uint32_t attr)
{
    uint32_t ctx = d2d_rd(D2D_KMCTX);
    uint32_t blend = d2d_ok(ctx) ? ((d2d_rd(ctx + 0x2C) & 0xFFu) | (d2d_rd(ctx + 0x30) & 0xFFu) << 8) : 0u;
    if (phase != d2d_phase_last || step != d2d_step_last)
        d2d_seq = 0;
    d2d_phase_last = phase;
    d2d_step_last = step;
    d2d_put("%c %c %d %u %d %d %08X %08X %08X %d %08X %08X %08X %08X %08X %08X %08X %04X %08X %08X %08X", kind, phase,
            step, d2d_seq++, (int)d2d_rd(FRAME_COUNTER), (int)d2d_rd(STEP_INDEX), d2d_rd(D2D_EXEC), c->pr, c->r[4],
            (int)c->r[5], pri, attr, d2d_rd(D2D_TEXLIST), d2d_rd(D2D_CMAT), d2d_rd(D2D_CMAT + 4),
            d2d_rd(D2D_CMAT + 8), d2d_rd(D2D_CMAT + 12), blend, d2d_rd(D2D_CTL3D), d2d_rd(D2D_ATTR_AND),
            d2d_rd(D2D_ATTR_AND + 4));
}

static void d2d_common(const sh4_cpu *c, char kind, int step, char phase)
{
    d2d_common_pa(c, kind, step, phase, c->fr.u[4], c->r[6]);   /* the 2D calls: pri (fr4), attr (r6) */
}

static void d2d_sprite(const sh4_cpu *c, int step, char phase)
{
    uint32_t sp = c->r[4], tlist, tanim, ta, tex = 0;
    int n = (int)c->r[5], k, texid;
    char name[32];
    d2d_common(c, 'S', step, phase);
    if (!d2d_ok(sp)) {
        d2d_put(" -\n");
        return;
    }
    tlist = d2d_rd(sp + 0x18);
    tanim = d2d_rd(sp + 0x1C);
    d2d_put(" %08X %08X %08X %08X %08X %08X %08X %08X", d2d_rd(sp), d2d_rd(sp + 4), d2d_rd(sp + 8), d2d_rd(sp + 12),
            d2d_rd(sp + 16), d2d_rd(sp + 20), tlist, tanim);
    ta = tanim + 0x14u * (uint32_t)n;
    if (!d2d_ok(tanim) || n < 0 || !d2d_ok(ta)) {
        d2d_put(" - - - - - - - - - - - -\n");
        return;
    }
    for (k = 0; k < 9; k++)
        d2d_put(" %d", d2d_rd16(ta + 2u * (uint32_t)k));
    d2d_put(" %04X", (unsigned)(d2d_rd16(ta + 18) & 0xFFFF));
    texid = d2d_rd16(ta + 16);
    if (d2d_ok(tlist) && texid >= 0 && (uint32_t)texid < d2d_rd(tlist + 4) && d2d_ok(d2d_rd(tlist)))
        tex = d2d_rd(tlist) + 12u * (uint32_t)texid;
    name[0] = 0;
    if (tex && d2d_ok(d2d_rd(tex))) {
        uint32_t s = d2d_rd(tex);
        for (k = 0; k < (int)sizeof name - 1; k++) {
            int ch = sh4_read8(s + (uint32_t)k);
            if (!ch || !(isalnum(ch) || ch == '_' || ch == '-' || ch == '.'))
                break;
            name[k] = (char)ch;
        }
        name[k] = 0;
    }
    d2d_put(" %08X %s\n", tex, name[0] ? name : "-");
}

static void d2d_poly(const sh4_cpu *c, int step, char phase)
{
    uint32_t p = c->r[4], pts, col, tex, num;
    int n = (int)c->r[5], i, m;
    d2d_common(c, 'P', step, phase);
    if (!d2d_ok(p)) {
        d2d_put(" -\n");
        return;
    }
    pts = d2d_rd(p);
    col = d2d_rd(p + 4);
    tex = d2d_rd(p + 8);
    num = d2d_rd(p + 12);
    d2d_put(" %08X %08X %08X %u", pts, col, tex, num);
    m = n < 0 ? 0 : n > D2D_MAXPTS ? D2D_MAXPTS : n;
    for (i = 0; i < m && d2d_ok(pts); i++) {
        uint32_t o = 8u * (uint32_t)i;
        d2d_put(" %08X,%08X", d2d_rd(pts + o), d2d_rd(pts + o + 4));
        if (col && d2d_ok(col))
            d2d_put(",%08X", d2d_rd(col + 4u * (uint32_t)i));
        else
            d2d_put(",-");
        if (tex && d2d_ok(tex))
            d2d_put(",%08X", d2d_rd(tex + 4u * (uint32_t)i));
        else
            d2d_put(",-");
    }
    d2d_put("\n");
}

/* round 0.9.2: the name of the texture whose NJS_TEXNAME.texaddr (+8) in the current texlist is v (njDrawTexture's tex,
 * a PVR global index on the menu screens), "-" when none */
static void d2d_texname_of(uint32_t v, char *out, int n)
{
    uint32_t tl = d2d_rd(D2D_TEXLIST), arr = d2d_ok(tl) ? d2d_rd(tl) : 0, cnt = d2d_rd(tl + 4), i;
    int k;
    snprintf(out, (size_t)n, "-");
    if (!d2d_ok(arr) || cnt > 4096)
        return;
    for (i = 0; i < cnt; i++) {
        uint32_t e = arr + 12u * i, s = d2d_rd(e);
        if (d2d_rd(e + 8) != v)
            continue;
        if (!d2d_ok(s)) {
            snprintf(out, (size_t)n, "#%u", i);
            return;
        }
        for (k = 0; k < n - 8; k++) {
            int ch = sh4_read8(s + (uint32_t)k);
            if (!ch || !(isalnum(ch) || ch == '_' || ch == '-' || ch == '.'))
                break;
            out[k] = (char)ch;
        }
        snprintf(out + k, (size_t)(n - k), "#%u", i);
        return;
    }
}

/* T: njDrawTexture: common (pri 0, attr = flag), the tex argument (GBIX), its texlist name, then n vertices
 * x,y,z,u,v,col (z = 1/w) */
static void d2d_texture(const sh4_cpu *c, int step, char phase)
{
    uint32_t v = c->r[4];
    int n = (int)c->r[5], i, m;
    char name[48];
    d2d_common_pa(c, 'T', step, phase, 0u, c->r[7]);
    d2d_texname_of(c->r[6], name, sizeof name);
    d2d_put(" %08X %s", c->r[6], name);
    m = n < 0 ? 0 : n > D2D_MAXPTS ? D2D_MAXPTS : n;
    for (i = 0; i < m && d2d_ok(v); i++) {
        uint32_t o = v + 24u * (uint32_t)i;
        d2d_put(" %08X,%08X,%08X,%08X,%08X,%08X", d2d_rd(o), d2d_rd(o + 4), d2d_rd(o + 8), d2d_rd(o + 12),
                d2d_rd(o + 16), d2d_rd(o + 20));
    }
    d2d_put("\n");
}

/* G: njDrawPolygon: common (pri 0, attr = trans), then n vertices x,y,z,col */
static void d2d_polygon(const sh4_cpu *c, int step, char phase)
{
    uint32_t v = c->r[4];
    int n = (int)c->r[5], i, m;
    d2d_common_pa(c, 'G', step, phase, 0u, c->r[6]);
    m = n < 0 ? 0 : n > D2D_MAXPTS ? D2D_MAXPTS : n;
    for (i = 0; i < m && d2d_ok(v); i++) {
        uint32_t o = v + 16u * (uint32_t)i;
        d2d_put(" %08X,%08X,%08X,%08X", d2d_rd(o), d2d_rd(o + 4), d2d_rd(o + 8), d2d_rd(o + 12));
    }
    d2d_put("\n");
}

static void d2d_hook(sh4_cpu *c, void *u)
{
    int which = (int)(intptr_t)u;
    int step = d2d_menu >= 0 ? d2d_menu : px_in_step ? cur_step : cur_step - 1;
    char phase = d2d_menu >= 0 ? 'M' : px_in_step ? 'S' : d2d_disp ? 'D' : 'F';
    if (d2d_menu >= 0 || step < sim_want_steps) {
        if (which == 0)
            d2d_sprite(c, step, phase);
        else if (which == 1)
            d2d_poly(c, step, phase);
        else if (which == 2)
            d2d_texture(c, step, phase);
        else
            d2d_polygon(c, step, phase);
        d2d_count++;
    }
    if (d2d_noexec)
        return;             /* the menu command: logged, not run (a void call: the registers stay as they were) */
    if (d2d_prev_fn[which]) {
        d2d_prev_fn[which](c, d2d_prev_user[which]);
    } else {
        set_hookbit(d2d_addr[which], 0);        /* the original runs unhooked (a fault leaves it so: d2d_frame_begin) */
        sh4_invoke(d2d_addr[which], NULL, 0, NULL, NULL);
        set_hookbit(d2d_addr[which], 1);
    }
}

static int d2d_install(void)
{
    int k;
    for (k = 0; k < D2D_NHOOK; k++) {
        sh4_hook_fn fn = NULL;
        d2d_prev_fn[k] = NULL;
        if (sh4_hook_find(d2d_addr[k], &fn, &d2d_prev_user[k], &d2d_prev_name[k]))
            d2d_prev_fn[k] = fn;
        sh4_hook_add(d2d_addr[k], d2d_hook, (void *)(intptr_t)k, d2d_hname[k]);
    }
    d2d_on = 1;
    printf("sim: logging the 2D draws (njDrawSprite2D 8C63EEB4, njDrawPolygon2D 8C63EBC8, njDrawTexture 8C63ED08, "
           "njDrawPolygon 8C63EC58)\n");
    return 0;
}

/* at every frame start: the mark a retried frame goes back to; re-arm the hooks after a fault */
static void d2d_frame_begin(void)
{
    int k;
    if (!d2d_on)
        return;
    d2d_mark = d2d_len;
    d2d_mark_count = d2d_count;
    d2d_disp = 0;
    for (k = 0; k < D2D_NHOOK; k++)
        if (!d2d_prev_fn[k])
            set_hookbit(d2d_addr[k], 1);
}

static void d2d_retry(void)
{
    if (!d2d_on)
        return;
    d2d_len = d2d_mark;
    d2d_count = d2d_mark_count;
    d2d_step_last = -2;
}

static void d2d_uninstall(void)
{
    int k;
    if (!d2d_on)
        return;
    for (k = 0; k < D2D_NHOOK; k++) {
        sh4_hook_fn fn = NULL;
        if (!sh4_hook_find(d2d_addr[k], &fn, NULL, NULL) || fn != d2d_hook)
            continue;
        if (d2d_prev_fn[k])
            sh4_hook_add(d2d_addr[k], d2d_prev_fn[k], d2d_prev_user[k], d2d_prev_name[k]);
        else
            sh4_hook_remove(d2d_addr[k]);
    }
}

static void d2d_write_columns(FILE *f);

static int d2d_write(const char *path, const sim_cfg *cfg, const char *input_name, int nsteps)
{
    FILE *f;
    make_dirs(path);
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "log-2d: cannot write %s\n", path);
        return -1;
    }
    fprintf(f, "# sa1 draw2d log v1: level %d act %d chr %d, script %s, %d steps, %u draws%s%s\n", cfg->level,
            cfg->act, cfg->chr, input_name, nsteps, d2d_count, cfg->route ? ", ROUTED" : "",
            cfg->screen ? "" : ", zero screen");
    d2d_write_columns(f);
    if (d2d_len)
        fwrite(d2d_buf, 1, d2d_len, f);
    fclose(f);
    printf("sim: %u 2D draws -> %s\n", d2d_count, path);
    return 0;
}

static void d2d_write_columns(FILE *f)
{
    fprintf(f, "# phase: S inside logic step <step> (task executor, CCL, ...), F frame code after step <step> (the HUD "
               "8C08A4BA / 8C08A724 runs there), D display pass (--disp), M menu frame <step> (the menu command); seq "
               "counts the draws of (phase, step); floats are bit patterns\n");
    fprintf(f, "# common: kind phase step seq frame(B_8C752BE8) sub(B_8C754EC8) exec(D_8C1C1D40) pr obj n pri attr "
               "texlist(D_8C673480) cmat_a cmat_r cmat_g cmat_b(B_8C88F668) blend(src|dst<<8) ctl3d(B_8C88F62C) "
               "attr_and attr_or(B_8C88F660/4)\n");
    fprintf(f, "# S (njDrawSprite2D 8C63EEB4, obj = NJS_SPRITE *): common, p.x p.y p.z sx sy ang tlist tanim, "
               "NJS_TEXANIM[n]: sx sy cx cy u1 v1 u2 v2 texid attr, the NJS_TEXNAME address, its file name\n");
    fprintf(f, "# P (njDrawPolygon2D 8C63EBC8, obj = NJS_POINT2COL *): common, p col tex num, then n vertices "
               "x,y,argb,uv ('-': no colour / uv list)\n");
    fprintf(f, "# T (njDrawTexture 8C63ED08, obj = NJS_TEXTURE_VTX *, pri 0, attr = flag): common, tex (the PVR "
               "global index), the name of the texlist entry whose texaddr is tex, then n vertices x,y,z,u,v,argb (z = "
               "1/w)\n");
    fprintf(f, "# G (njDrawPolygon 8C63EC58, obj = NJS_POLYGON_VTX *, pri 0, attr = trans): common, then n vertices "
               "x,y,z,argb\n");
}

/* ------------------------------------------------------------------ */
/* overlay image guard (image_guard.h, harness/README.md)               */
/*                                                                      */
/* Overlays that share a load slot reuse DC code addresses (0C914D60 is */
/* Super Sonic's trigger in B_CHAOS7, a wrapper in MINICART and ADV02).  */
/* After the routing set-up every routed native function of an overlay  */
/* image loses its routing hook (sh4_call.c route_hook) to guard_hook,   */
/* which runs the native function only while its image is the one in    */
/* RAM (CRC-32 of the image's code ranges = the generated value), and    */
/* the original otherwise.  Calls that reach the address from native     */
/* code through a pointer (the DEP path of sh4_call.c) are not "routed"  */
/* any more and come back through the interpreter to the same hook.      */
/* Several images may have a function at one address: guard_ent lists    */
/* them.  The identity is re-checked once per guard epoch (every logic   */
/* step, every GD-ROM HLE read / close, every set-up phase).             */

static uint32_t crc32_buf(uint32_t crc, const uint8_t *p, size_t n);

#define GUARD_MAX_IMG 32
#define GUARD_MAX_ENT 1024
#define GUARD_MAX_CAND 4
typedef struct guard_ent {
    uint32_t guest;
    int n;
    const sh4_native_fn *nf[GUARD_MAX_CAND];
    int img[GUARD_MAX_CAND];
    unsigned long native_calls, orig_calls;
} guard_ent;
static guard_ent guard_ents[GUARD_MAX_ENT];
static int n_guard;
static unsigned guard_seen[GUARD_MAX_IMG];
static uint8_t guard_live[GUARD_MAX_IMG], guard_ever[GUARD_MAX_IMG];
static unsigned long guard_flips[GUARD_MAX_IMG];

/* is overlay image k the one in RAM (this epoch)? */
static int guard_image_live(int k)
{
    if (k < 0 || k >= sa1h_fn_image_count || k >= GUARD_MAX_IMG)
        return 0;
    if (guard_seen[k] != guard_epoch) {
        const sa1h_fn_image *m = &sa1h_fn_images[k];
        uint32_t crc = 0;
        int i, live = m->code_bytes != 0;
        for (i = 0; live && i < m->nranges; i++) {
            uint32_t lo = sa1h_fn_image_ranges[m->first_range + i][0], hi = sa1h_fn_image_ranges[m->first_range + i][1];
            if (!sh4_is_ram(lo) || !sh4_is_ram(hi - 1) || hi <= lo) {
                live = 0;
                break;
            }
            crc = crc32_buf(crc, sh4_ram() + (lo & 0x00FFFFFFu), hi - lo);
        }
        live = live && crc == m->crc32;
        if (guard_seen[k] && live != guard_live[k])
            guard_flips[k]++;
        guard_live[k] = (uint8_t)live;
        guard_ever[k] |= (uint8_t)live;
        guard_seen[k] = guard_epoch;
    }
    return guard_live[k];
}

static void guard_hook(sh4_cpu *c, void *u)
{
    guard_ent *g = (guard_ent *)u;
    int i;
    for (i = 0; i < g->n; i++)
        if (guard_image_live(g->img[i])) {
            g->native_calls++;
            g->nf[i]->adapter(c);
            return;
        }
    /* the image at the slot is another one: its own code at this address runs, in the interpreter */
    g->orig_calls++;
    set_hookbit(g->guest, 0);
    sh4_invoke(g->guest, NULL, 0, NULL, NULL);     /* a fault leaves the bit clear: guard_rearm */
    set_hookbit(g->guest, 1);
}

/* after a fault: hook bits a guard hook left clear */
static void guard_rearm(void)
{
    int i;
    for (i = 0; i < n_guard; i++) {
        sh4_hook_fn fn = NULL;
        void *user = NULL;
        if (sh4_hook_find(guard_ents[i].guest, &fn, &user, NULL) && fn == guard_hook && user == &guard_ents[i])
            set_hookbit(guard_ents[i].guest, 1);
    }
}

/* after the routing set-up: every overlay function whose routing hook is installed goes under the
 * guard; returns the number of guarded functions */
static int guard_install(void)
{
    int i, j, n = 0, first = n_guard;
    for (i = 0; i < sh4_native_fn_count; i++) {
        const sh4_native_fn *nf = &sh4_native_fns[i];
        int k = sa1h_native_fn_image[i];
        void *user = NULL;
        guard_ent *g = NULL;
        if (k < 0 || !nf->guest || !nf->adapter)
            continue;
        if (!sh4_hook_find(nf->guest, NULL, &user, NULL) || user != (void *)nf)
            continue;               /* not routed (--route-only / --route-except) or an explicit hook */
        for (j = first; j < n_guard; j++)
            if ((guard_ents[j].guest & 0x1FFFFFFFu) == (nf->guest & 0x1FFFFFFFu))
                g = &guard_ents[j];
        if (!g) {
            if (n_guard == GUARD_MAX_ENT)
                break;
            g = &guard_ents[n_guard++];
            memset(g, 0, sizeof *g);
            g->guest = nf->guest;
        }
        if (g->n < GUARD_MAX_CAND) {
            g->nf[g->n] = nf;
            g->img[g->n++] = k;
            n++;
        }
    }
    for (j = first; j < n_guard; j++)
        sh4_hook_add(guard_ents[j].guest, guard_hook, &guard_ents[j], "overlay image guard");
    if (n) {
        printf("sim: image guard: %d routed overlay functions at %d addresses run natively only while their image "
               "is loaded:", n, n_guard - first);
        for (i = 0; i < sa1h_fn_image_count && i < GUARD_MAX_IMG; i++) {
            int cnt = 0;
            for (j = first; j < n_guard; j++) {
                int q;
                for (q = 0; q < guard_ents[j].n; q++)
                    cnt += guard_ents[j].img[q] == i;
            }
            if (cnt)
                printf(" %s %d", sa1h_fn_images[i].name, cnt);
        }
        printf("\n");
    }
    return n;
}

static void guard_report(void)
{
    int i, j;
    for (i = 0; i < sa1h_fn_image_count && i < GUARD_MAX_IMG; i++) {
        unsigned long nat = 0, orig = 0, nfun = 0, called = 0;
        for (j = 0; j < n_guard; j++) {
            int q, mine = 0;
            for (q = 0; q < guard_ents[j].n; q++)
                mine |= guard_ents[j].img[q] == i;
            if (!mine)
                continue;
            nfun++;
            called += (guard_ents[j].native_calls + guard_ents[j].orig_calls) != 0;
            nat += guard_ents[j].native_calls;
            orig += guard_ents[j].orig_calls;
        }
        if (nfun)
            printf("sim: image guard %s: %lu guarded addresses (%lu reached), %lu native calls, %lu calls left to "
                   "the original (another image loaded), image %s%s\n", sa1h_fn_images[i].name, nfun, called, nat,
                   orig, guard_ever[i] ? "seen" : "never seen", guard_flips[i] ? " (changed during the run)" : "");
        if (called) {
            printf("sim: image guard %s: reached (address native/original):", sa1h_fn_images[i].name);
            for (j = 0; j < n_guard; j++) {
                int q, mine = 0;
                for (q = 0; q < guard_ents[j].n; q++)
                    mine |= guard_ents[j].img[q] == i;
                if (mine && (guard_ents[j].native_calls || guard_ents[j].orig_calls))
                    printf(" %08X %lu/%lu", guard_ents[j].guest, guard_ents[j].native_calls, guard_ents[j].orig_calls);
            }
            printf("\n");
        }
    }
}

/* ------------------------------------------------------------------ */
/* level tasks: --no-level-tasks, --super-free, --create-task           */
/*                                                                      */
/* The stage start 8C04E660 creates the level's tasks in 8C04F5AC       */
/* (D_8C19E0E0[level]: CreateElementalTask(2, 2, ..), fog / lighting)   */
/* and 8C04F506 (D_8C19DE30[level]: B_8C752BD4 = CreateElementalTask(2, */
/* 0, ..), the level task; 0 when the entry is 0).  The options clear    */
/* the table words for the duration of the stage start (and restore     */
/* them), so the game's own code skips the creation.                    */
/*                                                                      */
/* --super-free (gsa1_rounds_plan.md 11.1): level 19 (Perfect Chaos,    */
/* B_CHAOS7 at 0x0C900000), act 0, Sonic.  D_8C19E0E0[19] (0C900080) is */
/* not created; D_8C19DE30[19] is, by the game, but its exec 0C900000   */
/* is this native stand-in.  The real level task does, on its first     */
/* exec (mode 0, 0C914E36, act 0): 8C64036C, B_0CA6C248 = 0, the sea    */
/* plane CreateElementalTask(6, 0, 0C91514A) (0C914E54-0C914E62), the   */
/* stage loop SE, the child 0C916560, mode 3; two execs later mode 4,   */
/* two more and 0C914DC6 runs: textures, object lists, the trigger      */
/* CreateChildTask(6, 0C914D60, tp) (0C914DF4-0C914DFC), the boss       */
/* 0C917300, the music task 0C914D8E, mode 1.  Modes 1, 3 and 4 also    */
/* clamp playertwp[0]->pos.y to >= 0 (0C900040-0C900052, 0C914F40-     */
/* 0C914F54, 0C914F86-0C914F9A); modes 1, 3 and 4 run the task's       */
/* children first (func_8C098E98(tp): the trigger is a child task and   */
/* runs only from there).  The stand-in, on its first exec (taskwk.mode */
/* 0), creates the sea plane and then the trigger, in that (the game's) */
/* order, and sets taskwk.mode = 1; every later exec runs the children  */
/* (func_8C098E98(tp)) and nothing else: no boss, no drain, no clamp,   */
/* no music.  The trigger runs at steps 1 and 2 and sends SetInputP(0,  */
/* 46) at step 2 (the game: steps 5 and 6).                             */

static int superfree_n;

static void superfree_exec(sh4_cpu *c, void *u)
{
    uint32_t tp = c->r[4], twp = sh4_is_ram(tp) ? sh4_read32(tp + 0x20) : 0;
    (void)u;
    if (sh4_is_ram(twp) && sh4_read8(twp) == 0) {
        sh4_arg a[3];
        uint32_t plane = 0, trig = 0;
        a[0] = sh4_ai(6);
        a[1] = sh4_ai(0);
        a[2] = sh4_ai(SUPER_SEA_PLANE);
        sh4_invoke(CREATE_ELEMENTAL_TASK, a, 3, &plane, NULL);
        a[0] = sh4_ai(6);
        a[1] = sh4_ai(SUPER_TRIGGER);
        a[2] = sh4_ai(tp);
        sh4_invoke(CREATE_CHILD_TASK, a, 3, &trig, NULL);
        sh4_write8(twp, 1);
        if (superfree_n++ == 0)
            printf("sim: --super-free: level task %08X (stand-in at %08X): sea plane task %08X "
                   "(CreateElementalTask(6, 0, %08X)), trigger task %08X (CreateChildTask(6, %08X, tp))\n", tp,
                   SUPER_LEVEL_EXEC, plane, SUPER_SEA_PLANE, trig, SUPER_TRIGGER);
    } else if (sh4_is_ram(tp)) {
        sh4_arg a = sh4_ai(tp);
        sh4_invoke(RUN_CHILDREN, &a, 1, NULL, NULL);    /* as modes 1 / 3 / 4: the trigger is a child */
    }
    c->r[0] = 0;
}

static const sim_cfg *lpt_cfg;
static sh4_hook_fn lpt_prev_fn;
static void *lpt_prev_user;
static const char *lpt_prev_name;

/* round 0.9.2: +ocp. An OBJ_CONDITION as the SET spawner 8C091380 leaves it (8C0914EA-8C091578): scCount 0 + 1,
 * scUserFlag 0, ssCondition 0x8001 (the SET's "present" 0x8000 | spawned), pTask, no SET entry, unionStatus.f = the
 * default range 160,000 + 100. The stage start's SET manager task 8C090FA0 resets the whole array B_8C79A3E0 in its first
 * exec (mode 0: 8C091016 -> 8C0916AE clears every entry but pTask, 8C0916EE marks the SET's entries 0x8000; with
 * --no-objects the SET is empty and entry 0 gets 0x8000), which in the game comes before any object is spawned: so the
 * conditions are written again right after that reset (setinit_hook), and the tasks see what a spawned object sees. A
 * libsa1 world has no SET manager: the replay writes them once, before its first step (tests/world_replay.c).
 * Round 0.9.4 (gsa1_094_plan.md 6.4, Review R.7): +ocp=HEX writes another initial ssCondition (sidecar flag
 * SA1_TOBJ_T_OCPW, the word in the task's reserved[0]): +ocp=8005 is a condition whose 0x0004 ("used up", 8C091B54) is
 * already set - a ground item box broken before it spawned, whose mode 0 makes the shell (8C091B68 -> mode 3 ->
 * 8C05D4CE). */
#define SET_MANAGER_INIT 0x8C091016u
static uint32_t ocp_addr[8], ocp_tp[8], ocp_word[8];
static int n_ocp;

static void ocp_write(uint32_t c, uint32_t tp, uint32_t word)
{
    sh4_write8(c + 0, 1);                       /* scCount: 0 + 1 (8C0914EA-8C0914EE) */
    sh4_write8(c + 1, 0);                       /* scUserFlag */
    sh4_write16(c + 2, (uint16_t)word);         /* ssCondition: present (the SET's 0x8000) | spawned (8C0914F0-8C0914F4)
                                                   = 0x8001; +ocp=HEX: that word */
    sh4_write32(c + 4, tp);                     /* pTask (8C091578) */
    sh4_write32(c + 8, 0);                      /* pObjEditEntry: no SET entry */
    sh4_writef(c + 12, SA1_TOBJ_OCP_RANGE);     /* unionStatus.f = 160,000 + 100 (8C09156E-8C091576) */
}

static void setinit_hook(sh4_cpu *c, void *u)
{
    int i;
    (void)u;
    sh4_hook_remove(SET_MANAGER_INIT);          /* one-shot: the stage's first manager exec */
    sh4_invoke(SET_MANAGER_INIT, NULL, 0, NULL, NULL);
    for (i = 0; i < n_ocp; i++)
        ocp_write(ocp_addr[i], ocp_tp[i], ocp_word[i]);
    printf("sim: --create-task +ocp: %d OBJ_CONDITION(s) written again after the SET manager's reset (8C091016)\n",
           n_ocp);
    (void)c;
}

/* one --create-task task: CreateElementalTask, then its position / scale / OBJ_CONDITION; `when` for the log */
static void create_task_now(const sim_cfg *cfg, int i, const char *when)
{
    {
        sh4_arg a[3];
        uint32_t tp = 0;
        a[0] = sh4_ai(cfg->create_im[i]);
        a[1] = sh4_ai(cfg->create_list[i]);
        a[2] = sh4_ai(cfg->create_addr[i]);
        sh4_invoke(CREATE_ELEMENTAL_TASK, a, 3, &tp, NULL);
        printf("sim: --create-task: CreateElementalTask(%u, %u, %08X) %s -> task %08X\n",
               cfg->create_im[i], cfg->create_list[i], cfg->create_addr[i], when, tp);
        {
            sa1_trace_obj_task t;
            sa1_trace_obj_task_ext x;
            uint32_t twp = sh4_is_ram(tp) ? sh4_read32(tp + 0x20) : 0;
            int k;
            memset(&t, 0, sizeof t);
            memset(&x, 0, sizeof x);
            t.exec = cfg->create_addr[i];
            t.im = cfg->create_im[i];
            t.list = cfg->create_list[i];
            if (cfg->create_has_pos[i] && sh4_is_ram(twp)) {
                t.has_pos = 1;
                for (k = 0; k < 3; k++) {
                    t.pos[k] = cfg->create_pos[i][k];
                    sh4_writef(twp + 0x20 + 4 * (uint32_t)k, t.pos[k]);
                }
                t.angy = cfg->create_angy[i];
                sh4_write32(twp + 0x18, t.angy);
                printf("sim: --create-task: taskwk %08X pos (%g, %g, %g) ang.y %04X\n", twp, (double)t.pos[0],
                       (double)t.pos[1], (double)t.pos[2], t.angy);
            }
            /* round 0.9.2 (sa1_trace_ext.h version 2): /SX,SY,SZ = taskwk.scl (+0x2C), +ocp = an OBJ_CONDITION as the
               SET spawner 8C091380 writes it (8C0914EA-8C091578) in the first all-zero slot of B_8C79A3E0 */
            if ((cfg->create_flags[i] & SA1_TOBJ_T_SCL) && sh4_is_ram(twp)) {
                x.flags |= SA1_TOBJ_T_SCL;
                for (k = 0; k < 3; k++) {
                    x.scl[k] = cfg->create_scl[i][k];
                    sh4_writef(twp + 0x2C + 4 * (uint32_t)k, x.scl[k]);
                }
                printf("sim: --create-task: taskwk %08X scl (%g, %g, %g)\n", twp, (double)x.scl[0], (double)x.scl[1],
                       (double)x.scl[2]);
            }
            if ((cfg->create_flags[i] & SA1_TOBJ_T_OCP) && sh4_is_ram(tp)) {
                uint32_t s, c = 0;
                for (s = 0; s < SA1_TOBJ_OCP_SLOTS; s++) {
                    uint32_t a = SA1_TOBJ_OCP_ARRAY + 16u * s, j, any = 0;
                    for (j = 0; j < 16; j += 4)
                        any |= sh4_read32(a + j);
                    if (!any) {
                        c = a;
                        break;
                    }
                }
                if (!c) {
                    fprintf(stderr, "--create-task +ocp: no free OBJ_CONDITION slot in B_8C79A3E0\n");
                    exit(2);
                }
                if (cfg->create_flags[i] & SA1_TOBJ_T_OCPW) {   /* round 0.9.4: +ocp=HEX (Review R.7) */
                    x.flags |= SA1_TOBJ_T_OCPW;
                    x.reserved[0] = cfg->create_ocpw[i] & 0xFFFFu;
                }
                ocp_write(c, tp, SA1_TOBJ_OCP_WORD(&x));
                sh4_write32(tp + 0x1C, c);                  /* tp->ocp (8C0914F6) */
                if (n_ocp < 8) {
                    ocp_addr[n_ocp] = c;
                    ocp_word[n_ocp] = SA1_TOBJ_OCP_WORD(&x);
                    ocp_tp[n_ocp++] = tp;
                }
                if (!sh4_hook_find(SET_MANAGER_INIT, NULL, NULL, NULL))
                    sh4_hook_add(SET_MANAGER_INIT, setinit_hook, NULL, "SET manager mode 0 + --create-task +ocp");
                x.flags |= SA1_TOBJ_T_OCP;
                x.ocp_slot = (c - SA1_TOBJ_OCP_ARRAY) / 16u;
                x.ocp_range = SA1_TOBJ_OCP_RANGE;
                printf("sim: --create-task: task %08X ocp B_8C79A3E0[%u] (%08X): ssCondition %04X, range %g\n", tp,
                       x.ocp_slot, c, (unsigned)SA1_TOBJ_OCP_WORD(&x), (double)x.ocp_range);
            }
            if (cfg->create_flags[i] & SA1_TOBJ_T_LATE)
                x.flags |= SA1_TOBJ_T_LATE;
            ho_oracle_add_task_v2(&t, &x, tp); /* the <out>.obj.bin header: world_replay.c makes the same task */
        }
    }
}

/* LoadPlayerTask, then the --create-task tasks (one-shot; +late tasks: after the stage start, create_late_tasks) */
static void lpt_hook(sh4_cpu *c, void *u)
{
    uint32_t r0;
    int i;
    (void)u;
    sh4_hook_remove(LOAD_PLAYER_TASK);
    if (lpt_prev_fn) {
        sh4_hook_add(LOAD_PLAYER_TASK, lpt_prev_fn, lpt_prev_user, lpt_prev_name);
        lpt_prev_fn(c, lpt_prev_user);
    } else {
        sh4_invoke(LOAD_PLAYER_TASK, NULL, 0, NULL, NULL);
    }
    r0 = c->r[0];
    for (i = 0; lpt_cfg && i < lpt_cfg->n_create; i++)
        if (!(lpt_cfg->create_flags[i] & SA1_TOBJ_T_LATE))
            create_task_now(lpt_cfg, i, "after LoadPlayerTask");
    c->r[0] = r0;
}

/* round 0.9.2: the +late tasks, made after the whole stage start (as libsa1's replay makes every sidecar task after its
   world's boot, and as the game makes an object during play): such a task is the newest of its list, so it runs before
   the stage start's tasks of the same list (the item box before the item icon task 8C05E14E, both list 3) */
static void create_late_tasks(const sim_cfg *cfg)
{
    int i;
    for (i = 0; i < cfg->n_create; i++)
        if (cfg->create_flags[i] & SA1_TOBJ_T_LATE)
            create_task_now(cfg, i, "after the stage start (+late)");
}

static void level_tasks_install(const sim_cfg *cfg)
{
    if (cfg->super_free)
        sh4_hook_add(SUPER_LEVEL_EXEC, superfree_exec, NULL, "Perfect Chaos level task stand-in (--super-free)");
    if (cfg->n_create) {
        lpt_cfg = cfg;
        lpt_prev_fn = NULL;
        sh4_hook_find(LOAD_PLAYER_TASK, &lpt_prev_fn, &lpt_prev_user, &lpt_prev_name);
        sh4_hook_add(LOAD_PLAYER_TASK, lpt_hook, NULL, "LoadPlayerTask + --create-task");
    }
}

/* ------------------------------------------------------------------ */
/* --pno1: the player-1 / follower-AI sidecar <out>.p1.bin              */
/* (sa1_trace_ext.h; docs/research/follower_ai.md Review R.2.4, R.2.5,  */
/* G13).  The AI exec 8C081D4E is hooked like the player execs (px_*):  */
/* at its first entry of a step the rand state, XMTRX and the camera    */
/* taskwk, when it returns per[1] / analog[1]; after the step the AI    */
/* taskwk and player 1's record (fill_record_slot(.., 1) and the px_*   */
/* state at player 1's exec entry, px_stage1).                          */

static sa1_trace_p1_rec ai_stage;       /* entry / exit fields of the current step */
static sh4_hook_fn ai_prev_fn;
static void *ai_prev_user;
static const char *ai_prev_name;
static int ai_saved;
static sa1_trace_p1_rec *p1recs;
static int capp1;
static uint32_t p1_hdr_flags;

static void ai_hook(sh4_cpu *c, void *u);

static void ai_install(void)
{
    sh4_hook_fn fn = NULL;
    void *user = NULL;
    const char *nm = NULL;
    int has = sh4_hook_find(FOLLOW_AI_EXEC, &fn, &user, &nm);
    if (has && fn == ai_hook)
        return;
    if (!ai_saved) {
        if (has) {
            ai_prev_fn = fn;
            ai_prev_user = user;
            ai_prev_name = nm;
        }
        ai_saved = 1;
    }
    sh4_hook_add(FOLLOW_AI_EXEC, ai_hook, NULL, "follower AI exec (--pno1)");
}

static void ai_uninstall(void)
{
    sh4_hook_fn fn = NULL;
    if (!ai_saved || !sh4_hook_find(FOLLOW_AI_EXEC, &fn, NULL, NULL) || fn != ai_hook)
        return;
    if (ai_prev_fn)
        sh4_hook_add(FOLLOW_AI_EXEC, ai_prev_fn, ai_prev_user, ai_prev_name);
    else
        sh4_hook_remove(FOLLOW_AI_EXEC);
}

static void ai_hook(sh4_cpu *c, void *u)
{
    (void)u;
    if (px_in_step && ai_stage.ai_calls++ == 0) {
        ai_stage.ai_rand = sh4_read32(RAND_STATE);
        memcpy(ai_stage.ai_xmtrx, SH4.xf.u, sizeof ai_stage.ai_xmtrx);
        ai_stage.ai_cam_twp = sh4_read32(CAMERA_TWP);
        copy_guest(ai_stage.ai_cam_raw, ai_stage.ai_cam_twp, 0x40);
    }
    if (ai_prev_fn) {
        ai_prev_fn(c, ai_prev_user);            /* routed: the native exec through its adapter */
    } else {
        sh4_hook_remove(FOLLOW_AI_EXEC);        /* the original (a fault unwinds past the re-add: ai_install) */
        sh4_invoke(FOLLOW_AI_EXEC, NULL, 0, NULL, NULL);
        sh4_hook_add(FOLLOW_AI_EXEC, ai_hook, NULL, "follower AI exec (--pno1)");
    }
    if (px_in_step) {
        ai_stage.ai_on = sh4_read32(PAD_REC0 + PAD_REC_SIZE + 0x08);
        ai_stage.ai_press = sh4_read32(PAD_REC0 + PAD_REC_SIZE + 0x10);
        ai_stage.ai_analog_ang = sh4_read32(ANALOG0 + 8);
        ai_stage.ai_analog_stroke = sh4_readf(ANALOG0 + 12);
    }
}

/* after the step: the sidecar record of main record m (recs[nrecs - 1]) */
static void p1_record(const sa1_trace_rec *m)
{
    sa1_trace_p1_rec *q;
    uint32_t ai_tp, ai_twp;
    if (nrecs > capp1) {
        capp1 = capp1 ? capp1 * 2 : 1024;
        if (capp1 < nrecs)
            capp1 = nrecs;
        p1recs = (sa1_trace_p1_rec *)realloc(p1recs, sizeof(sa1_trace_p1_rec) * (size_t)capp1);
    }
    q = &p1recs[nrecs - 1];
    *q = ai_stage;                      /* the AI entry / exit fields */
    q->step = m->step;
    q->flags = 0;
    ai_tp = sh4_read32(FOLLOW_AI_TASK);
    ai_twp = sh4_is_ram(ai_tp) ? sh4_read32(ai_tp + 0x20) : 0;
    q->ai_tp = sh4_is_ram(ai_tp) ? ai_tp : 0;
    q->ai_twp = sh4_is_ram(ai_twp) ? ai_twp : 0;
    copy_guest(q->ai_twp_raw, q->ai_twp, 0x40);
    q->ai_mode = (int8_t)q->ai_twp_raw[0];
    q->ai_smode = (int8_t)q->ai_twp_raw[1];
    q->ai_id = (int8_t)q->ai_twp_raw[2];
    q->ai_btimer = (int8_t)q->ai_twp_raw[3];
    q->ai_flag = (int16_t)rd16le(q->ai_twp_raw + 4);
    q->ai_wtimer = rd16le(q->ai_twp_raw + 6);
    q->ai_counter = rd16le(q->ai_twp_raw + 8);     /* counter.w[0], the Uint16 cnt of 8C082570 */
    q->ai_timer = rd32le(q->ai_twp_raw + 12);
    if (q->ai_tp)
        q->flags |= SA1_P1F_AI_TASK;
    if (ai_stage.ai_calls)
        q->flags |= SA1_P1F_AI_ENTERED;
    q->first_pno = px_first_pno;
    q->first_rand = px_first_pno == 0xFFFFFFFFu ? 0 : px_first_rand;
    memset(&q->p1, 0, sizeof q->p1);
    q->p1.step = m->step;
    q->p1.frame = m->frame;
    q->p1.sub = m->sub;
    q->p1.icount = m->icount;
    q->p1.pad_on = sh4_read32(PAD_REC0 + PAD_REC_SIZE + 0x08);
    q->p1.pad_press = sh4_read32(PAD_REC0 + PAD_REC_SIZE + 0x10);
    q->p1.analog_ang = sh4_read32(ANALOG0 + 8);
    q->p1.analog_stroke = sh4_readf(ANALOG0 + 12);
    fill_record_slot(&q->p1, 1);
    memcpy(&q->p1.px_exec, &px_stage1.px_exec,
           offsetof(sa1_trace_rec, frame_counter) - offsetof(sa1_trace_rec, px_exec));
    if (sh4_is_ram(sh4_read32(PLAYER_TWP + 4)))
        q->flags |= SA1_P1F_PLAYER1;
    if (px_stage1.px_calls)
        q->flags |= SA1_P1F_P1_ENTERED;
    p1_hdr_flags |= (q->flags & SA1_P1F_AI_TASK ? SA1_P1H_AI_SEEN : 0u) |
                    (q->flags & SA1_P1F_PLAYER1 ? SA1_P1H_P1_SEEN : 0u);
}

/* ------------------------------------------------------------------ */
/* --dump-world FILE: the bytes of the game's globals, all of them      */
/* after the stage start ("@start") and after every logic step the      */
/* runs of bytes that changed ("@step N").  Symbols: the list of        */
/* harness/tools/world_syms.py (build/verify/world_syms.txt: address,   */
/* size, class v/p/m/u, referenced-by-C flag, name); by default the     */
/* non-pointer ones (class v or u) that the decompiled C references;    */
/* --world-all: every listed symbol.  Without the list: the data        */
/* symbols of this build's native manifest (sh4_native_datas).          */
/* Line rule (libsa1 writes the same): per symbol, each maximal run of  */
/* changed bytes, printed in pieces of at most 32 bytes:                */
/*     <name>+<offset hex> <bytes hex>                                  */

typedef struct wsym { uint32_t addr, size, off; char cls; char name[51]; } wsym;
static wsym *wsyms;
static int n_wsyms;
static uint8_t *world_prev;
static FILE *world_fp;

static int wsym_cap;

static void world_add(uint32_t a, uint32_t sz, char cls, const char *name)
{
    if (!sh4_is_ram(a) || !sz || sz > 0x10000 || !sh4_is_ram(a + sz - 1))
        return;
    if (n_wsyms == wsym_cap) {
        wsym_cap = wsym_cap ? wsym_cap * 2 : 1024;
        wsyms = (wsym *)realloc(wsyms, sizeof(wsym) * (size_t)wsym_cap);
    }
    wsyms[n_wsyms].addr = a;
    wsyms[n_wsyms].size = sz;
    wsyms[n_wsyms].off = 0;
    wsyms[n_wsyms].cls = cls;
    snprintf(wsyms[n_wsyms].name, sizeof wsyms[n_wsyms].name, "%s", name);
    n_wsyms++;
}

static int wsym_cmp(const void *a, const void *b)
{
    uint32_t x = ((const wsym *)a)->addr & 0x00FFFFFFu, y = ((const wsym *)b)->addr & 0x00FFFFFFu;
    return x < y ? -1 : x > y;
}

static int world_load(const sim_cfg *cfg)
{
    char path[1300], line[512];
    FILE *f;
    int i, k;
    uint32_t total = 0;
    const char *src;
    if (cfg->world_syms)
        snprintf(path, sizeof path, "%s", cfg->world_syms);
    else
        snprintf(path, sizeof path, "%s/build/verify/world_syms.txt", g_root);
    f = fopen(path, "r");
    if (!f && cfg->world_syms) {
        fprintf(stderr, "dump-world: cannot open %s\n", path);
        return -1;
    }
    src = f ? path : "the data symbols of this build's native manifest (sh4_native_datas)";
    if (f) {
        while (fgets(line, sizeof line, f)) {
            unsigned a, sz, ref;
            char c2[8], name[64];
            if (line[0] == '#' || sscanf(line, "%x %x %7s %u %63s", &a, &sz, c2, &ref, name) != 5)
                continue;
            if (!cfg->world_all && (!ref || (c2[0] != 'v' && c2[0] != 'u')))
                continue;
            world_add(a, sz, c2[0], name);
        }
        fclose(f);
    } else {
        for (i = 0; i < sh4_native_data_count; i++)
            world_add(sh4_native_datas[i].addr, sh4_native_datas[i].gap, 'u', sh4_native_datas[i].name);
    }
    if (n_wsyms)
        qsort(wsyms, (size_t)n_wsyms, sizeof(wsym), wsym_cmp);
    for (i = 0, k = 0; i < n_wsyms; i++)         /* one entry per address */
        if (!k || (wsyms[k - 1].addr & 0x00FFFFFFu) != (wsyms[i].addr & 0x00FFFFFFu))
            wsyms[k++] = wsyms[i];
    n_wsyms = k;
    for (i = 0; i < n_wsyms; i++) {
        wsyms[i].off = total;
        total += wsyms[i].size;
    }
    world_prev = (uint8_t *)calloc(total ? total : 1, 1);
    make_dirs(cfg->dump_world);
    world_fp = fopen(cfg->dump_world, "w");
    if (!world_fp) {
        fprintf(stderr, "dump-world: cannot write %s\n", cfg->dump_world);
        return -1;
    }
    fprintf(world_fp, "# sa1 world dump v1: level %d act %d chr %d; %d symbols, %u bytes, from %s%s\n", cfg->level,
            cfg->act, cfg->chr, n_wsyms, total, src, cfg->world_all ? " (all classes)" : "");
    fprintf(world_fp, "# @start = after the stage start (all bytes); @step N = after logic step N (changed runs); "
                      "<name>+<off> <hex>, at most 32 bytes per line\n");
    printf("sim: world dump: %d symbols, %u bytes -> %s\n", n_wsyms, total, cfg->dump_world);
    return 0;
}

static void world_emit(const wsym *w, uint32_t off, const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 32 ? 32 : n, i;
        fprintf(world_fp, "%s+%X ", w->name, off);
        for (i = 0; i < k; i++)
            fprintf(world_fp, "%02X", p[i]);
        fputc('\n', world_fp);
        off += k;
        p += k;
        n -= k;
    }
}

/* full = 1: every byte (start); else the runs that differ from the previous snapshot */
static void world_snapshot(const char *tag, int full)
{
    int i;
    if (!world_fp)
        return;
    fprintf(world_fp, "%s\n", tag);
    for (i = 0; i < n_wsyms; i++) {
        const wsym *w = &wsyms[i];
        const uint8_t *cur = sh4_ram() + (w->addr & 0x00FFFFFFu);
        uint8_t *prev = world_prev + w->off;
        uint32_t k = 0;
        if (full) {
            world_emit(w, 0, cur, w->size);
        } else if (memcmp(cur, prev, w->size)) {
            while (k < w->size) {
                uint32_t s;
                if (cur[k] == prev[k]) {
                    k++;
                    continue;
                }
                s = k;
                while (k < w->size && cur[k] != prev[k])
                    k++;
                world_emit(w, s, cur + s, k - s);
            }
        }
        memcpy(prev, cur, w->size);
    }
}

static void world_close(void)
{
    if (world_fp)
        fclose(world_fp);
    world_fp = NULL;
}

/* ------------------------------------------------------------------ */
/* --census FILE: what the game writes.  Setup = game init .. stage     */
/* start (compared with the RAM right after the ROM load), steps = the  */
/* frames of the run (compared step by step, so a value written and     */
/* later restored still counts).  Byte ranges that changed, and the     */
/* 4 KB blocks the interpreter wrote (sh4_ram_dirty: also writes of an  */
/* unchanged value; the copy-on-write granularity).  Native (routed)    */
/* code does not mark blocks dirty: take the census from pure runs.     */

static uint8_t *cens_base, *cens_prev;
static uint8_t *cens_chg[2];                    /* byte bitmaps: [0] setup, [1] steps */
static uint8_t cens_blk[2][SH4_RAM_SIZE >> 12];

static void census_begin(void)
{
    cens_base = (uint8_t *)malloc(SH4_RAM_SIZE);
    cens_prev = (uint8_t *)malloc(SH4_RAM_SIZE);
    cens_chg[0] = (uint8_t *)calloc(SH4_RAM_SIZE / 8, 1);
    cens_chg[1] = (uint8_t *)calloc(SH4_RAM_SIZE / 8, 1);
    memcpy(cens_base, sh4_ram(), SH4_RAM_SIZE);
    memset(sh4_ram_dirty, 0, sizeof sh4_ram_dirty);
}

static void census_end_setup(void)
{
    const uint8_t *ram = sh4_ram();
    uint32_t i;
    if (!cens_base)
        return;
    for (i = 0; i < SH4_RAM_SIZE; i++)
        if (ram[i] != cens_base[i])
            cens_chg[0][i >> 3] |= (uint8_t)(1u << (i & 7));
    memcpy(cens_blk[0], sh4_ram_dirty, sizeof cens_blk[0]);
    memcpy(cens_prev, ram, SH4_RAM_SIZE);
    memset(sh4_ram_dirty, 0, sizeof sh4_ram_dirty);
}

static void census_point(void)
{
    const uint8_t *ram = sh4_ram();
    uint32_t b, i;
    if (!cens_base)
        return;
    for (b = 0; b < (SH4_RAM_SIZE >> 12); b++) {
        if (!sh4_ram_dirty[b])
            continue;
        cens_blk[1][b] = 1;
        sh4_ram_dirty[b] = 0;
        for (i = b << 12; i < ((b + 1) << 12); i++)
            if (ram[i] != cens_prev[i]) {
                cens_chg[1][i >> 3] |= (uint8_t)(1u << (i & 7));
                cens_prev[i] = ram[i];
            }
    }
}

static void census_write(const char *path, const sim_cfg *cfg, const char *input_name, int nsteps)
{
    static const char *phase[2] = { "setup", "steps" };
    FILE *f;
    int p;
    if (!cens_base)
        return;
    make_dirs(path);
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "census: cannot write %s\n", path);
        return;
    }
    fprintf(f, "# sa1 write census v1: level %d act %d chr %d, script %s, %d steps%s\n", cfg->level, cfg->act,
            cfg->chr, input_name, nsteps, cfg->route ? ", ROUTED (native writes are not seen)" : "");
    fprintf(f, "# B <phase> <first block address> <blocks>: 4 KB blocks the interpreter wrote\n"
               "# W <phase> <start> <end>: bytes [start, end) that changed (P1 addresses)\n");
    for (p = 0; p < 2; p++) {
        uint32_t b = 0, i = 0;
        while (b < (SH4_RAM_SIZE >> 12)) {
            uint32_t s;
            if (!cens_blk[p][b]) {
                b++;
                continue;
            }
            s = b;
            while (b < (SH4_RAM_SIZE >> 12) && cens_blk[p][b])
                b++;
            fprintf(f, "B %s %08X %u\n", phase[p], SH4_RAM_P1 + (s << 12), b - s);
        }
        while (i < SH4_RAM_SIZE) {
            uint32_t s;
            if (!(cens_chg[p][i >> 3] & (1u << (i & 7)))) {
                i++;
                continue;
            }
            s = i;
            while (i < SH4_RAM_SIZE && (cens_chg[p][i >> 3] & (1u << (i & 7))))
                i++;
            fprintf(f, "W %s %08X %08X\n", phase[p], SH4_RAM_P1 + s, SH4_RAM_P1 + i);
        }
    }
    fclose(f);
    printf("sim: write census -> %s\n", path);
}

/* ------------------------------------------------------------------ */
/* --dump-ram-at N[,N..|end]: the 16 MB of guest RAM (0x8C000000-       */
/* 0x8CFFFFFF) at the start of logic step N, i.e. after step N-1 and    */
/* the frame code between them; N = 0 is the state after the stage     */
/* start (= --dump-ram), "end" after the last step.  Files:             */
/* <trace prefix>.step<N>.ram / .end.ram                                */

static int ramat_steps[64], n_ramat, ramat_end;
static char ramat_prefix[1200];

static int ramat_parse(const char *s)
{
    while (s && *s) {
        char *e;
        long v;
        while (*s == ',' || *s == ' ')
            s++;
        if (!*s)
            break;
        if (!strncmp(s, "end", 3)) {
            ramat_end = 1;
            s += 3;
            continue;
        }
        v = strtol(s, &e, 10);
        if (e == s || v < 0) {
            fprintf(stderr, "--dump-ram-at: want step numbers or 'end', got '%s'\n", s);
            return -1;
        }
        if (n_ramat < 64)
            ramat_steps[n_ramat++] = (int)v;
        s = e;
    }
    return 0;
}

static void ramat_write(const char *tag)
{
    char path[1300];
    FILE *f;
    snprintf(path, sizeof path, "%s.%s.ram", ramat_prefix, tag);
    make_dirs(path);
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    fwrite(sh4_ram(), 1, SH4_RAM_SIZE, f);
    fclose(f);
    printf("sim: guest RAM %s -> %s\n", tag, path);
}

static void ramat_maybe(int step)
{
    int i;
    for (i = 0; i < n_ramat; i++)
        if (ramat_steps[i] == step) {
            char tag[32];
            snprintf(tag, sizeof tag, "step%d", step);
            ramat_write(tag);
            ramat_steps[i] = -1;        /* once (a --discover retry repeats the step) */
        }
}

/* ------------------------------------------------------------------ */
/* the step hook                                                       */

static void step_hook(sh4_cpu *c, void *u)
{
    uint32_t r0 = 0;
    uint64_t ic0;
    sa1_trace_rec *r;
    (void)u;
    script_at(cur_step, &pad_x, &pad_y, &pad_buttons);
    if (n_ramat)
        ramat_maybe(cur_step);
    guard_epoch++;
    guard_rearm();
    px_install();
    memset(&px_stage, 0, sizeof px_stage);
    memset(&px_stage1, 0, sizeof px_stage1);
    px_first_pno = 0xFFFFFFFFu;
    px_first_rand = 0;
    if (px_pno1) {
        memset(&ai_stage, 0, sizeof ai_stage);
        ai_install();
    }
    ho_oracle_step_begin(cur_step);     /* --hostobj: the script's calls before this step */
    px_in_step = 1;
    if (sdk_fp) {
        sdk_rearm();
        sdk_phase_set('S', cur_step);
    }
    sh4_hook_remove(STEP_FN);
    ic0 = SH4.icount;
    sh4_invoke(STEP_FN, NULL, 0, &r0, NULL);      /* a fault unwinds to sim_run, which re-adds the hook */
    sh4_hook_add(STEP_FN, step_hook, NULL, "logic step");
    px_in_step = 0;
    if (sdk_fp)
        sdk_phase_set('F', cur_step);
    c->r[0] = r0;
    if (nrecs == caprecs) {
        caprecs = caprecs ? caprecs * 2 : 1024;
        recs = (sa1_trace_rec *)realloc(recs, sizeof(sa1_trace_rec) * (size_t)caprecs);
    }
    r = &recs[nrecs++];
    memset(r, 0, sizeof *r);
    r->step = (uint32_t)cur_step;
    r->frame = (uint32_t)cur_frame;
    r->sub = sh4_read32(STEP_INDEX);
    r->icount = (uint32_t)(SH4.icount - ic0);
    r->stick_x = pad_x;
    r->stick_y = pad_y;
    r->buttons = pad_buttons;
    r->pad_on = sh4_read32(PAD_REC0 + 0x08);
    r->pad_press = sh4_read32(PAD_REC0 + 0x10);
    r->analog_ang = sh4_read32(ANALOG0);
    r->analog_stroke = sh4_readf(ANALOG0 + 4);
    fill_record(r);
    memcpy(&r->px_exec, &px_stage.px_exec,
           offsetof(sa1_trace_rec, frame_counter) - offsetof(sa1_trace_rec, px_exec));
    if (px_pno1)
        p1_record(r);
    ho_oracle_step_end(cur_step, nrecs);   /* --hostobj / --create-task: the sidecar record */
    census_point();
    if (world_fp && cur_step < sim_want_steps) {
        char tag[32];
        snprintf(tag, sizeof tag, "@step %d", cur_step);
        world_snapshot(tag, 0);
    }
    step_icount += SH4.icount - ic0;
    cur_step++;
}

/* ------------------------------------------------------------------ */
/* the screen (default; --no-screen), --log-vis, --vis-force,        */
/* --vis-only (docs/research/visibility_fidelity.md)                  */
/*                                                                     */
/* 8C604702 is Ninja's njInitSystem(mode, frame, count) (sim_stubs[]   */
/* called it a "vsync wait" until 2026-10-20). The game's video init   */
/* 8C010180 (game init 8C053AC0 -> 8C0546C0) calls it once: display    */
/* mode, screen size, Kamui / PVR set-up (busy-waits on Holly          */
/* 0xA05F6900), the interrupt handlers and njInitView 8C604BA0         */
/* (distance 500, aspect 1:1, near / far, the 2D clip rectangle        */
/* (0, 0, w-1, h-1), ...).  As a no-op it leaves the screen size, the  */
/* aspect and the clip rectangle at 0 (and so the distance the game's  */
/* njSetPerspective 8C640500 derives from the width), and the sphere   */
/* visibility test 8C038D00 of the character / object displays never   */
/* passes.  By default (since 2026-10-20; before, only with --screen)  */
/* the HLE below does the RAM part of the original: the mode words     */
/* B_8C88F620/24/28, the screen size from the mode bits (floats        */
/* B_8C88F5E8/EC, integers B_8C88F644/48), then the original           */
/* njInitView in the interpreter; the hardware part stays out.  Trace  */
/* header flag bit 3 (8) marks such a run (every reference since       */
/* 2026-10-20).  --no-screen keeps the old no-op of sim_stubs[] (the   */
/* old references: build/traces/ref/<name>.noscreen.bin).              */
/* The game's hand-written model renderers 8C03700C / 8C037098 (sub    */
/* step 0 only; own frustum test 8C03718C, which never passed without  */
/* the view) then reach their TA output, which reads renderer state    */
/* the simulator does not set up (like the land strip renderer         */
/* 8C037108): with the screen they return at once, as their frustum    */
/* test made them do before.  They only draw (render state, store      */
/* queues).                                                            */
/* --log-vis FILE logs every call of the visibility test; --vis-force  */
/* 0|1|sub0 answers it instead of running it, --vis-only PR,.. runs it */
/* only for those return addresses (else 0): attribution experiments.  */
#define NJ_INIT_SYSTEM 0x8C604702u
#define NJ_INIT_VIEW   0x8C604BA0u
#define VIS_TEST       0x8C038D00u

static void hle_nj_init_system(sh4_cpu *c, void *u)
{
    uint32_t mode = c->r[4], frm = c->r[5], cnt = c->r[6], wi, hi;
    float w, h;
    int k;
    (void)u;
    sh4_write32(0x8C88F620u, mode);
    sh4_write32(0x8C88F624u, frm);
    sh4_write32(0x8C88F628u, cnt);
    if (mode & 0x10) { w = 640.0f; wi = 0x280; } else { w = 320.0f; wi = 0x140; }
    if (mode & 0x20) { h = 480.0f; hi = 0x1E0; } else { h = 240.0f; hi = 0xF0; }
    if ((mode & 3) == 1) {          /* VGA: 640 x 480 whatever the bits say */
        w = 640.0f; wi = 0x280;
        h = 480.0f; hi = 0x1E0;
    }
    if (mode & 0x10000) {
        w *= 2.0f;
        if (h == 240.0f)
            h = 480.0f;
    }
    sh4_writef(0x8C88F5E8u, w);
    sh4_writef(0x8C88F5ECu, h);
    sh4_write32(0x8C88F644u, wi);
    sh4_write32(0x8C88F648u, hi);
    k = sh4_invoke(NJ_INIT_VIEW, NULL, 0, NULL, NULL);
    printf("sim: screen: njInitSystem(%X, %X, %X) -> screen %.0f x %.0f, njInitView 8C604BA0 %s\n", mode, frm, cnt,
           (double)w, (double)h, k == SH4_OK ? "ran" : "FAULTED");
    c->r[0] = 0;
}

static FILE *vis_fp;
static int vis_force = -1;          /* --vis-force: -1 run the original, 0 / 1 answer that, 2 answer 1 on sub step 0 only */
static unsigned long vis_n, vis_yes, vis_n0, vis_yes0;
static uint32_t vis_only[64];
static int n_vis_only = -1;         /* -1: no --vis-only */
static uint32_t vis_last[16];
static sh4_hook_fn vis_prev_fn;
static void *vis_prev_user;

/* one line per call: phase (S = inside logic step <step>, O = outside),
 * step, B_8C754EC8, caller (PR), x y z r, the view-space centre
 * XMTRX * (x, y, z, 1), the result; the screen / clip words
 * B_8C88F5E4..B_8C88F61C as a '#' line whenever they change */
static void vis_hook(sh4_cpu *c, void *u)
{
    float in[4], v[4], m[16], r = c->fr.f[7];
    uint32_t pr = c->pr, sub = sh4_read32(STEP_INDEX), ret = 0, cur[16];
    int i;
    (void)u;
    for (i = 0; i < 16; i++)
        cur[i] = sh4_read32(0x8C88F5E4u + 4u * (uint32_t)i);
    if (vis_fp && memcmp(cur, vis_last, sizeof cur)) {
        float f[16];
        memcpy(vis_last, cur, sizeof cur);
        memcpy(f, cur, sizeof f);
        fprintf(vis_fp, "# screen: dist %g w %g h %g cx %g cy %g | clip2d %g %g %g %g | near %g far %g | aspect %g %g\n",
                (double)f[0], (double)f[1], (double)f[2], (double)f[3], (double)f[4], (double)f[7], (double)f[8],
                (double)f[9], (double)f[10], (double)f[11], (double)f[12], (double)f[13], (double)f[14]);
    }
    in[0] = c->fr.f[4];
    in[1] = c->fr.f[5];
    in[2] = c->fr.f[6];
    in[3] = 1.0f;
    memcpy(m, c->xf.f, sizeof m);
    sh4_ftrv(m, in, v);
    for (i = 0; i < n_vis_only && vis_only[i] != pr; i++)
        ;
    if (n_vis_only >= 0 && i == n_vis_only) {
        ret = 0;
        c->r[0] = 0;
    } else if (vis_force >= 0) {
        ret = vis_force == 2 ? (uint32_t)(sub == 0) : (uint32_t)vis_force;
        c->r[0] = ret;
    } else if (vis_prev_fn) {
        vis_prev_fn(c, vis_prev_user);
        ret = c->r[0];
    } else {
        set_hookbit(VIS_TEST, 0);
        sh4_invoke(VIS_TEST, NULL, 0, &ret, NULL);
        set_hookbit(VIS_TEST, 1);
    }
    vis_n++;
    vis_yes += ret != 0;
    vis_n0 += sub == 0;
    vis_yes0 += sub == 0 && ret != 0;
    if (vis_fp && cur_step < sim_want_steps)
        fprintf(vis_fp, "%c %d %u %08X %.9g %.9g %.9g %.9g | %.9g %.9g %.9g -> %u\n", px_in_step ? 'S' : 'O', cur_step,
                sub, pr, (double)in[0], (double)in[1], (double)in[2], (double)r, (double)v[0], (double)v[1],
                (double)v[2], ret);
}

static int vis_install(const char *path, const char *force, const char *only)
{
    if (only) {
        const char *s = only;
        n_vis_only = 0;
        while (*s && n_vis_only < 64) {
            char *e;
            vis_only[n_vis_only++] = (uint32_t)strtoul(s, &e, 16);
            s = *e ? e + 1 : e;
        }
        printf("sim: --vis-only: the visibility test runs for %d callers, answers 0 for the others\n", n_vis_only);
    }
    if (force) {
        vis_force = !strcmp(force, "sub0") ? 2 : !strcmp(force, "1") ? 1 : !strcmp(force, "0") ? 0 : -2;
        if (vis_force == -2) {
            fprintf(stderr, "vis-force: 0, 1 or sub0\n");
            return -1;
        }
        printf("sim: --vis-force %s: the visibility test 8C038D00 answers %s\n", force,
               vis_force == 2 ? "1 on sub step 0, else 0" : vis_force ? "1" : "0");
    }
    if (path) {
        vis_fp = fopen(path, "w");
        if (!vis_fp) {
            fprintf(stderr, "log-vis: cannot write %s\n", path);
            return -1;
        }
        fprintf(vis_fp, "# sa1 visibility test 8C038D00 log: <S|O> <step> <sub> <caller> x y z r | view x y z -> result%s\n",
                force ? " (forced)" : "");
    }
    vis_prev_fn = NULL;
    vis_prev_user = NULL;
    sh4_hook_find(VIS_TEST, &vis_prev_fn, &vis_prev_user, NULL);
    sh4_hook_add(VIS_TEST, vis_hook, NULL, "visibility test (--log-vis)");
    return 0;
}

static void vis_close(void)
{
    if (vis_fp)
        fclose(vis_fp);
    vis_fp = NULL;
    if (!vis_n && vis_force < 0)
        return;
    printf("sim: visibility test 8C038D00: %lu calls, %lu visible; on sub step 0: %lu calls, %lu visible\n", vis_n,
           vis_yes, vis_n0, vis_yes0);
}

/* ------------------------------------------------------------------ */
/* setup                                                               */

static void sleep_nop(sh4_cpu *c) { (void)c; }
static void trap_nop(sh4_cpu *c, uint32_t imm) { (void)c; (void)imm; }

static void print_player(const char *tag)
{
    uint32_t twp = sh4_read32(PLAYER_TWP);
    if (!sh4_is_ram(twp)) {
        printf("  %s: no player task\n", tag);
        return;
    }
    printf("  %s: mode %3d smode %3d flag %04X pos (%10.4f %10.4f %10.4f) ang (%04X %04X %04X)\n", tag,
           (int8_t)sh4_read8(twp), (int8_t)sh4_read8(twp + 1), sh4_read16(twp + 4), (double)sh4_readf(twp + 0x20),
           (double)sh4_readf(twp + 0x24), (double)sh4_readf(twp + 0x28), sh4_read32(twp + 0x14) & 0xFFFF,
           sh4_read32(twp + 0x18) & 0xFFFF, sh4_read32(twp + 0x1C) & 0xFFFF);
}

static void install_hle(void)
{
    int i;
    sh4_sleep_handler = sleep_nop;
    sh4_trap_handler = trap_nop;
    hle_install_bios();
    sh4_hook_add(GDFS_OPEN, hle_gdfs_open, NULL, "gdFsOpen");
    sh4_hook_add(GDFS_SIZE, hle_gdfs_size, NULL, "gdFsGetFileSize");
    sh4_hook_add(GDFS_READ, hle_gdfs_read, NULL, "gdFsRead");
    sh4_hook_add(GDFS_CLOSE, hle_gdfs_close, NULL, "gdFsClose");
    sh4_hook_add(0x8C601F5Au, hle_get_peripheral, NULL, "pdGetPeripheral");
    sh4_hook_add(0x8C06C1B2u, hle_frame_time, NULL, "frame timer");
    for (i = 0; sim_stubs[i].name; i++)
        sh4_hook_add(sim_stubs[i].addr, sim_stubs[i].ret ? sh4_hle_retu : sh4_hle_ret0,
                     (void *)(uintptr_t)sim_stubs[i].ret, sim_stubs[i].name);
}

static int sim_setup(const sim_cfg *cfg)
{
    sh4_arg none[1], a;
    uint64_t ic0;
    if (harness_load_rom())
        return -1;
    if (cfg->disc)
        snprintf(disc_dir, sizeof disc_dir, "%s", cfg->disc);
    else
        snprintf(disc_dir, sizeof disc_dir, "%s/disc/SONICADV", g_root);
    {
        char probe[1300];
        FILE *pf;
        snprintf(probe, sizeof probe, "%s/S_MOT.PRS", disc_dir);
        pf = fopen(probe, "rb");
        if (!pf) {
            fprintf(stderr, "sim: %s not found: the simulator reads the game's files (stage PRS, SET/CAM,\n"
                            "     motions, sound banks) from the extracted disc folder; pass --disc DIR\n", probe);
            return -1;
        }
        fclose(pf);
    }
    snap = (uint8_t *)malloc(SH4_RAM_SIZE);
    discover = cfg->discover;
    sim_verbose = cfg->verbose;
    no_objects = cfg->no_objects;
    sh4_strict_io = 1;
    sh4_cstack_on = 1;
    install_hle();
    if (cfg->screen) {
        /* replaces the no-op of sim_stubs[] (sh4_hook_add replaces a hook) */
        sh4_hook_add(NJ_INIT_SYSTEM, hle_nj_init_system, NULL, "njInitSystem view set-up (screen)");
        sh4_hook_add(0x8C03700Cu, sh4_hle_ret0, NULL, "hand-written model renderer (screen: draw only)");
        sh4_hook_add(0x8C037098u, sh4_hle_ret0, NULL, "hand-written model renderer (screen: draw only)");
    }
    level_tasks_install(cfg);
    if (cfg->census)
        census_begin();         /* RAM as loaded (ROM image, zero BSS) */
    if (cfg->route == 2) {
        sh4_route_native(1);
        if (cfg->route_only || cfg->route_except)
            route_select(cfg->route_only, cfg->route_except);
        if (!cfg->no_guard)
            guard_install();
    }
    ic0 = SH4.icount;

    printf("sim: game init %s\n", fname(GAME_INIT));
    if (sim_run("init", GAME_INIT, none, 0, 400000000))
        return -1;
    guard_epoch++;
    sh4_write32(GAME_MODE, 4);
    sh4_write8(CUR_CHAR, (uint8_t)cfg->chr);
    printf("sim: character %d: %s (motion file, textures)\n", cfg->chr, fname(CHG_CHAR));
    a = sh4_ai((uint32_t)cfg->chr);
    if (sim_run("chr", CHG_CHAR, &a, 1, 400000000))
        return -1;
    guard_epoch++;
    sh4_write16(CUR_LEVEL, (uint16_t)cfg->level);
    sh4_write16(CUR_ACT, (uint16_t)cfg->act);
    printf("sim: stage load %s (level %d act %d)\n", fname(STAGE_LOAD), cfg->level, cfg->act);
    if (sim_run("load", STAGE_LOAD, none, 0, 1000000000))
        return -1;
    guard_epoch++;
    printf("sim: stage start %s\n", fname(STAGE_START));
    {
        /* --no-level-tasks / --super-free: the table words the stage start reads, cleared for its duration */
        uint32_t lv = (uint32_t)(uint16_t)sh4_read16(CUR_LEVEL);
        uint32_t t1 = LEVEL_TASK_TAB + 4u * lv, t2 = LEVEL_TASK2_TAB + 4u * lv;
        uint32_t v1 = sh4_read32(t1), v2 = sh4_read32(t2);
        int rc;
        if (cfg->no_level_tasks || cfg->super_free) {
            if (cfg->no_level_tasks)
                sh4_write32(t1, 0);
            sh4_write32(t2, 0);
            printf("sim: %s: level %u: D_8C19DE30[%u] = %08X %s, D_8C19E0E0[%u] = %08X not created\n",
                   cfg->super_free ? "--super-free" : "--no-level-tasks", lv, lv, v1,
                   cfg->no_level_tasks ? "not created" : "created by the game (exec: the stand-in)", lv, v2);
        }
        rc = sim_run("start", STAGE_START, none, 0, 400000000);
        sh4_write32(t1, v1);
        sh4_write32(t2, v2);
        guard_epoch++;
        if (rc)
            return -1;
    }
    create_late_tasks(cfg);     /* --create-task ...+late */
    printf("sim: ready after %llu instructions; landtable %08X, player task work %08X, %d logic steps per frame\n",
           (unsigned long long)(SH4.icount - ic0), sh4_read32(LANDTABLE), sh4_read32(PLAYER_TWP),
           (int)sh4_read32(STEPS_PER_FRM));
    print_player("start");
    return 0;
}
/* ------------------------------------------------------------------ */
/* output                                                              */

static uint32_t crc32_buf(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t tab[256];
    size_t i;
    if (!tab[1]) {
        uint32_t k, j, c;
        for (k = 0; k < 256; k++) {
            c = k;
            for (j = 0; j < 8; j++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tab[k] = c;
        }
    }
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = tab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void make_dirs(const char *path)
{
    char buf[1300];
    size_t i;
    snprintf(buf, sizeof buf, "%s", path);
    for (i = 1; buf[i]; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char ch = buf[i];
            buf[i] = 0;
#ifdef _WIN32
            CreateDirectoryA(buf, NULL);
#endif
            buf[i] = ch;
        }
    }
}

static uint8_t *read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)*size + 1);
    *size = (long)fread(b, 1, (size_t)*size, f);
    fclose(f);
    return b;
}

/* CRC-32 of baserom/1ST_READ.BIN and of baserom/overlays/<stage>.BIN (the
 * decompressed stage image the game loads at 0x0C900000); also reports how
 * many bytes of the image in RAM differ from the file after the stage load */
/* the overlay the stage load put at 0x0C900000: the last file read through the GD-ROM HLE (during
 * the set-up) whose name is an overlay that loads there (STG01.PRS, B_CHAOS7.PRS for level 19,
 * ADV02.PRS for Mystic Ruins, MINICART.PRS ...); STGnn of the level when none was read */
static int stage_image(const sim_cfg *cfg)
{
    char nm[64];
    int i, k, best = -1;
    for (i = 0; i < ngdlogs; i++) {
        const char *s = gdlogs[i].name;
        int n = 0;
        if (gdlogs[i].size < 0)
            continue;
        while (*s == '/' || *s == '\\')
            s++;
        while (s[n] && s[n] != '.' && s[n] != ' ' && n < 63) {
            nm[n] = s[n];
            n++;
        }
        nm[n] = 0;
        for (k = 0; k < sh4_overlay_count; k++)
            if (sh4_overlays[k].load == 0x0C900000u && !strcmp(sh4_overlays[k].name, nm))
                best = k;
    }
    if (best >= 0)
        return best;
    snprintf(nm, sizeof nm, "STG%02d", cfg->level);
    for (k = 0; k < sh4_overlay_count; k++)
        if (!strcmp(sh4_overlays[k].name, nm))
            return k;
    return -1;
}

static void file_crcs(const sim_cfg *cfg, uint32_t *rom_crc, uint32_t *stage_crc, uint32_t *stage_size)
{
    char path[1300], name[16];
    long n = 0;
    uint8_t *b;
    int k;
    snprintf(path, sizeof path, "%s/baserom/1ST_READ.BIN", g_root);
    b = read_file(path, &n);
    *rom_crc = b ? crc32_buf(0, b, (size_t)n) : 0;
    free(b);
    *stage_crc = *stage_size = 0;
    k = stage_image(cfg);
    if (k < 0)
        return;
    snprintf(name, sizeof name, "%s", sh4_overlays[k].name);
    snprintf(path, sizeof path, "%s/%s", g_root, sh4_overlays[k].bin);
    b = read_file(path, &n);
    if (!b)
        return;
    *stage_crc = crc32_buf(0, b, (size_t)n);
    *stage_size = (uint32_t)n;
    if (sh4_overlays[k].load == 0x0C900000u && n <= 0x700000) {
        long i, diff = 0;
        const uint8_t *ram = sh4_ram() + 0x00900000u;
        for (i = 0; i < n; i++)
            diff += ram[i] != b[i];
        printf("sim: stage image %s: %ld of %ld bytes differ from %s after the stage load\n", name, diff, n,
               sh4_overlays[k].bin);
    }
    free(b);
}

static int trace_v1;        /* --trace-v1: write version 1 (972-byte records) */

static void fill_header(sa1_trace_header *h, const sim_cfg *cfg, const char *input_name, uint32_t stage_size)
{
    memset(h, 0, sizeof *h);
    memcpy(h->magic, SA1_TRACE_MAGIC, 8);
    h->version = trace_v1 ? 1 : SA1_TRACE_VERSION;
    h->header_size = sizeof *h;
    h->record_size = trace_v1 ? SA1_TRACE_REC_V1_SIZE : (uint32_t)sizeof(sa1_trace_rec);
    h->count = (uint32_t)nrecs;
    h->level = (int16_t)cfg->level;
    h->act = (int16_t)cfg->act;
    h->chr = (int8_t)cfg->chr;
    h->steps_per_frame = (uint8_t)sh4_read32(STEPS_PER_FRM);
    h->flags = (uint16_t)((cfg->disp ? 1 : 0) | (cfg->no_objects ? 2 : 0) | (cfg->route ? 4 : 0) | (cfg->screen ? 8 : 0));
    (void)stage_size;
    file_crcs(cfg, &h->rom_crc32, &h->stage_crc32, &h->stage_size);
    h->landtable = sh4_read32(LANDTABLE);
    snprintf(h->input_name, sizeof h->input_name, "%s", input_name);
}

static int write_trace(const char *prefix, const sa1_trace_header *h)
{
    char path[1300];
    FILE *f;
    int i;
    uint32_t mx = sh4_host_mxcsr_get();
    make_dirs(prefix);
    snprintf(path, sizeof path, "%s.bin", prefix);
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    fwrite(h, sizeof *h, 1, f);
    for (i = 0; i < nrecs; i++)
        fwrite(&recs[i], h->record_size, 1, f);
    fclose(f);
    printf("trace: %s (version %u, %d records of %u bytes)\n", path, h->version, nrecs, h->record_size);
    snprintf(path, sizeof path, "%s.csv", prefix);
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    sh4_host_mxcsr_set(0x1F80);      /* print in the host's default rounding mode */
    fprintf(f, "step,frame,sub,in_x,in_y,in_buttons,pad_on,pad_press,an_ang,an_stroke,mode,smode,twp_flag,"
               "ang_x,ang_y,ang_z,pos_x,pos_y,pos_z,mspd_x,mspd_y,mspd_z,macc_x,macc_y,macc_z,aim_y,"
               "pspd_x,pspd_y,pspd_z,pacc_x,pacc_y,pacc_z,fn_x,fn_y,fn_z,dotp,attr,pwp_flag,jumptimer,"
               "nocontimer,waittimer,reqaction,action,nframe,land_count,icount%s\n",
            h->version >= 2 ? ",px_calls,px_rand,px_frame,px_rings,cam_x,cam_y,cam_z,cam_ang_y,frame_counter,rings,"
                              "grav_x,grav_y,grav_z" : "");
    for (i = 0; i < nrecs; i++) {
        const sa1_trace_rec *r = &recs[i];
        fprintf(f, "%u,%u,%u,%d,%d,%X,%X,%X,%04X,%.9g,%d,%d,%04X,%04X,%04X,%04X,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
                   "%.9g,%.9g,%.9g,%04X,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%08X,%04X,%d,%d,%d,"
                   "%u,%u,%.9g,%u,%u",
                r->step, r->frame, r->sub, r->stick_x, r->stick_y, r->buttons, r->pad_on, r->pad_press,
                r->analog_ang & 0xFFFF, (double)r->analog_stroke, r->mode, r->smode, (uint16_t)r->twp_flag,
                r->ang[0] & 0xFFFF, r->ang[1] & 0xFFFF, r->ang[2] & 0xFFFF, (double)r->pos[0], (double)r->pos[1],
                (double)r->pos[2], (double)r->mspd[0], (double)r->mspd[1], (double)r->mspd[2], (double)r->macc[0],
                (double)r->macc[1], (double)r->macc[2], r->ang_aim[1] & 0xFFFF, (double)r->pspd[0],
                (double)r->pspd[1], (double)r->pspd[2], (double)r->pacc[0], (double)r->pacc[1], (double)r->pacc[2],
                (double)r->floor_normal[0], (double)r->floor_normal[1], (double)r->floor_normal[2],
                (double)r->dotp, r->attr, (uint16_t)r->pwp_flag, r->jumptimer, r->nocontimer, r->waittimer,
                r->reqaction, r->action, (double)r->nframe, r->land_count, r->icount);
        if (h->version >= 2)
            fprintf(f, ",%u,%08X,%u,%d,%.9g,%.9g,%.9g,%04X,%u,%d,%.9g,%.9g,%.9g", r->px_calls, r->px_rand,
                    r->px_frame_counter, r->px_rings, (double)rdfle(r->px_cam_raw + 0x20),
                    (double)rdfle(r->px_cam_raw + 0x24), (double)rdfle(r->px_cam_raw + 0x28),
                    rd32le(r->px_cam_raw + 0x18) & 0xFFFF, r->frame_counter, r->rings, (double)r->gravity[0],
                    (double)r->gravity[1], (double)r->gravity[2]);
        fputc('\n', f);
    }
    sh4_host_mxcsr_set(mx);
    fclose(f);
    printf("trace: %s\n", path);
    return 0;
}

/* compare with a previous trace: every record must be bitwise identical,
 * except icount (the instruction count changes with --route).  A version-1
 * reference is compared with the v1 part of the records (offsets 0..971) */
typedef struct rec_field { const char *name; size_t off, size; } rec_field;
#define RF(f) { #f, offsetof(sa1_trace_rec, f), sizeof(((sa1_trace_rec *)0)->f) }
static const rec_field rec_fields[] = {
    RF(step), RF(frame), RF(sub), RF(icount), RF(stick_x), RF(stick_y), RF(buttons), RF(pad_on), RF(pad_press),
    RF(analog_ang), RF(analog_stroke), RF(mode), RF(smode), RF(id), RF(btimer), RF(twp_flag), RF(wtimer),
    RF(ang), RF(pos), RF(mspd), RF(macc), RF(ang_aim), RF(pspd), RF(pacc), RF(floor_normal), RF(dotp),
    RF(attr), RF(last_attr), RF(pwp_flag), RF(jumptimer), RF(nocontimer), RF(waittimer), RF(reqaction),
    RF(action), RF(lastaction), RF(nextaction), RF(nframe), RF(equipment), RF(item), RF(land_count),
    RF(rand_state), RF(reserved), RF(twp_raw), RF(mwp_raw), RF(pwp_raw), RF(csts_raw),
    RF(px_exec), RF(px_calls), RF(px_rand), RF(px_frame_counter), RF(px_rings), RF(px_pad), RF(px_gravity),
    RF(px_xmtrx), RF(px_cam_twp), RF(px_cam_raw), RF(frame_counter), RF(rings), RF(pad2), RF(gravity),
    RF(ptp_addr), RF(twp_addr), RF(mwp_addr), RF(pwp_addr), RF(csts_addr), RF(reserved2),
};

static const char *rec_field_at(size_t off, size_t *rel)
{
    size_t i;
    for (i = 0; i < sizeof rec_fields / sizeof rec_fields[0]; i++)
        if (off >= rec_fields[i].off && off < rec_fields[i].off + rec_fields[i].size) {
            *rel = off - rec_fields[i].off;
            return rec_fields[i].name;
        }
    *rel = 0;
    return "?";
}

static int check_decoded;   /* --check-decoded: ignore the raw blobs (and v2: XMTRX, camera blob, addresses) */

/* the fields --check-decoded leaves out */
static void strip_raw(sa1_trace_rec *r)
{
    memset(r->twp_raw, 0, sizeof r->twp_raw + sizeof r->mwp_raw + sizeof r->pwp_raw + sizeof r->csts_raw);
    memset(r->reserved, 0, sizeof r->reserved);
    memset(r->px_xmtrx, 0, sizeof r->px_xmtrx);
    r->px_cam_twp = 0;
    memset(r->px_cam_raw, 0, sizeof r->px_cam_raw);
    r->ptp_addr = r->twp_addr = r->mwp_addr = r->pwp_addr = r->csts_addr = 0;
    memset(r->reserved2, 0, sizeof r->reserved2);
}

static int check_trace(const char *path)
{
    FILE *f = fopen(path, "rb");
    sa1_trace_header h;
    sa1_trace_rec r, mine;
    size_t cmp;
    int i, n, bad = 0, ndiff = 0;
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, SA1_TRACE_MAGIC, 8) ||
        !((h.version == 1 && h.record_size == SA1_TRACE_REC_V1_SIZE) ||
          (h.version == 2 && h.record_size == sizeof r))) {
        fprintf(stderr, "%s: not a version-1 or version-2 trace\n", path);
        fclose(f);
        return -1;
    }
    cmp = h.record_size;
    fseek(f, (long)h.header_size, SEEK_SET);
    n = (int)h.count < nrecs ? (int)h.count : nrecs;
    for (i = 0; i < n; i++) {
        size_t k;
        memset(&r, 0, sizeof r);
        if (fread(&r, h.record_size, 1, f) != 1)
            break;
        mine = recs[i];
        if (cmp < sizeof mine)
            memset((uint8_t *)&mine + cmp, 0, sizeof mine - cmp);
        r.icount = mine.icount = 0;
        if (check_decoded) {
            strip_raw(&r);
            strip_raw(&mine);
        }
        if (!memcmp(&r, &mine, cmp))
            continue;
        if (!bad)
            printf("check: first difference at step %d:\n", i);
        bad = 1;
        for (k = 0; k < cmp; k++) {
            if (((uint8_t *)&r)[k] != ((uint8_t *)&mine)[k]) {
                size_t rel;
                const char *fn = rec_field_at(k, &rel);
                if (ndiff < 12)
                    printf("  step %d: %s+0x%X: %02X (reference) vs %02X (now)\n", i, fn, (unsigned)rel,
                           ((uint8_t *)&r)[k], ((uint8_t *)&mine)[k]);
                ndiff++;
                /* skip the rest of this field */
                while (k + 1 < cmp && rec_field_at(k + 1, &rel) == fn)
                    k++;
            }
        }
        if (ndiff >= 12)
            break;
    }
    fclose(f);
    if (!bad && (int)h.count != nrecs) {
        printf("check: %u records in %s, %d now\n", h.count, path, nrecs);
        bad = 1;
    }
    if (!bad)
        printf("check: identical to %s (%d steps; icount%s not compared%s)\n", path, nrecs,
               check_decoded ? ", raw blobs, XMTRX, camera blob, addresses" : "",
               h.version == 1 ? "; version-1 reference: the v1 part of the records compared" : "");
    return bad;
}


/* ------------------------------------------------------------------ */
/* the --pno1 sidecar: write, check, summary                            */

static void p1_header(sa1_trace_ext_header *x, const sa1_trace_header *h, uint32_t count)
{
    memset(x, 0, sizeof *x);
    memcpy(x->magic, SA1_TRACE_EXT_MAGIC_P1, 8);
    x->version = SA1_TRACE_EXT_P1_VERSION;
    x->header_size = sizeof *x;
    x->record_size = sizeof(sa1_trace_p1_rec);
    x->count = count;
    x->flags = p1_hdr_flags;
    x->level = h->level;
    x->act = h->act;
    x->chr = h->chr;
    x->pno = 1;
}

static int write_p1(const char *prefix, const sa1_trace_header *h)
{
    char path[1300];
    sa1_trace_ext_header x;
    FILE *f;
    int i;
    uint32_t mx = sh4_host_mxcsr_get();
    p1_header(&x, h, (uint32_t)nrecs);
    snprintf(path, sizeof path, "%s.p1.bin", prefix);
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    fwrite(&x, sizeof x, 1, f);
    for (i = 0; i < nrecs; i++)
        fwrite(&p1recs[i], sizeof p1recs[i], 1, f);
    fclose(f);
    printf("trace: %s (player-1 sidecar version %u, %d records of %u bytes)\n", path, x.version, nrecs, x.record_size);
    snprintf(path, sizeof path, "%s.p1.csv", prefix);
    f = fopen(path, "w");
    if (!f)
        return -1;
    sh4_host_mxcsr_set(0x1F80);
    fprintf(f, "step,flags,ai_mode,ai_smode,ai_counter,ai_calls,ai_on,ai_press,ai_an_ang,ai_an_stroke,first_pno,"
               "first_rand,p1_mode,p1_smode,p1_action,p1_x,p1_y,p1_z,p1_spd_x,p1_spd_y,p1_attr,p1_px_calls,dist,"
               "cam_x,cam_y,cam_z\n");
    for (i = 0; i < nrecs; i++) {
        const sa1_trace_p1_rec *q = &p1recs[i];
        const sa1_trace_rec *r = &recs[i];
        double dx = (double)q->p1.pos[0] - r->pos[0], dy = (double)q->p1.pos[1] - r->pos[1],
               dz = (double)q->p1.pos[2] - r->pos[2];
        fprintf(f, "%u,%X,%d,%d,%u,%u,%X,%X,%04X,%.9g,%d,%08X,%d,%d,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%08X,%u,%.9g,"
                   "%.9g,%.9g,%.9g\n",
                q->step, q->flags, q->ai_mode, q->ai_smode, q->ai_counter, q->ai_calls, q->ai_on, q->ai_press,
                q->ai_analog_ang & 0xFFFF, (double)q->ai_analog_stroke, (int)q->first_pno, q->first_rand, q->p1.mode,
                q->p1.smode, q->p1.action, (double)q->p1.pos[0], (double)q->p1.pos[1], (double)q->p1.pos[2],
                (double)q->p1.pspd[0], (double)q->p1.pspd[1], q->p1.attr, q->p1.px_calls,
                (q->flags & SA1_P1F_PLAYER1) ? sqrt(dx * dx + dy * dy + dz * dz) : -1.0,
                (double)rdfle(q->ai_cam_raw + 0x20), (double)rdfle(q->ai_cam_raw + 0x24),
                (double)rdfle(q->ai_cam_raw + 0x28));
    }
    sh4_host_mxcsr_set(mx);
    fclose(f);
    printf("trace: %s\n", path);
    return 0;
}

/* the follower in plain words: AI modes, the off-screen counter, every respawn with its cause */
static void p1_summary(void)
{
    int i, modes[16] = { 0 }, ai_steps = 0, p1_steps = 0, nresp = 0;
    uint32_t maxc = 0;
    int maxc_step = -1;
    for (i = 0; i < nrecs; i++) {
        const sa1_trace_p1_rec *q = &p1recs[i];
        if (q->flags & SA1_P1F_AI_TASK) {
            ai_steps++;
            if (q->ai_mode >= 0 && q->ai_mode < 16)
                modes[q->ai_mode]++;
            if (q->ai_counter > maxc && i > 0 && p1recs[i - 1].ai_mode != 0) {    /* not the creation value */
                maxc = q->ai_counter;
                maxc_step = i;
            }
        }
        p1_steps += (q->flags & SA1_P1F_PLAYER1) != 0;
    }
    printf("sim: --pno1: AI task in %d of %d steps, player 1 in %d; AI supervisor modes:", ai_steps, nrecs, p1_steps);
    for (i = 0; i < 16; i++)
        if (modes[i])
            printf(" %d x%d", i, modes[i]);
    printf("; highest off-screen counter %u (step %d)\n", maxc, maxc_step);
    for (i = 1; i < nrecs; i++) {
        const sa1_trace_p1_rec *q = &p1recs[i], *p = &p1recs[i - 1];
        if ((q->flags & SA1_P1F_AI_TASK) && q->ai_mode == 2 && p->ai_mode != 2) {
            const sa1_trace_rec *r = &recs[i - 1];
            double dx = (double)p->p1.pos[0] - r->pos[0], dy = (double)p->p1.pos[1] - r->pos[1],
                   dz = (double)p->p1.pos[2] - r->pos[2], d = sqrt(dx * dx + dy * dy + dz * dz);
            nresp++;
            printf("sim: --pno1: respawn requested in step %d: distance %.1f, off-screen counter %u before the step "
                   "-> %s\n", i, d, p->ai_counter, d > 1000.0 ? "by distance (> 1000)" :
                   p->ai_counter >= 300 ? "by the off-screen counter (> 300 frames)" : "cause not decided");
        }
    }
    if (!nresp)
        printf("sim: --pno1: no respawn\n");
}

/* --check: the reference's sidecar <ref minus .bin>.p1.bin against this run's (icount aside) */
static int check_p1(const char *ref_path)
{
    char path[1300];
    FILE *f;
    sa1_trace_ext_header x;
    sa1_trace_p1_rec r, mine;
    size_t L = strlen(ref_path);
    int i, n, bad = 0, ndiff = 0;
    if (L > 4 && !strcmp(ref_path + L - 4, ".bin"))
        L -= 4;
    snprintf(path, sizeof path, "%.*s.p1.bin", (int)L, ref_path);
    f = fopen(path, "rb");
    if (!f) {
        printf("check: no reference sidecar %s (player 1 not compared)\n", path);
        return 0;
    }
    if (fread(&x, sizeof x, 1, f) != 1 || memcmp(x.magic, SA1_TRACE_EXT_MAGIC_P1, 8) ||
        x.record_size != sizeof r || x.header_size < sizeof x) {
        printf("check: %s is not a version-%d player-1 sidecar\n", path, SA1_TRACE_EXT_P1_VERSION);
        fclose(f);
        return 1;
    }
    fseek(f, (long)x.header_size, SEEK_SET);
    n = (int)x.count < nrecs ? (int)x.count : nrecs;
    for (i = 0; i < n; i++) {
        size_t k;
        if (fread(&r, sizeof r, 1, f) != 1)
            break;
        mine = p1recs[i];
        r.p1.icount = mine.p1.icount = 0;
        if (check_decoded) {
            strip_raw(&r.p1);
            strip_raw(&mine.p1);
            memset(r.ai_twp_raw, 0, sizeof r.ai_twp_raw);
            memset(mine.ai_twp_raw, 0, sizeof mine.ai_twp_raw);
            memset(r.ai_xmtrx, 0, sizeof r.ai_xmtrx);
            memset(mine.ai_xmtrx, 0, sizeof mine.ai_xmtrx);
            memset(r.ai_cam_raw, 0, sizeof r.ai_cam_raw);
            memset(mine.ai_cam_raw, 0, sizeof mine.ai_cam_raw);
            r.ai_tp = r.ai_twp = r.ai_cam_twp = mine.ai_tp = mine.ai_twp = mine.ai_cam_twp = 0;
        }
        if (!memcmp(&r, &mine, sizeof r))
            continue;
        if (!bad)
            printf("check: player-1 sidecar: first difference at step %d:\n", i);
        bad = 1;
        for (k = 0; k < sizeof r; k++)
            if (((uint8_t *)&r)[k] != ((uint8_t *)&mine)[k]) {
                size_t rel = 0;
                const char *fn = "ai/header fields";
                if (k >= offsetof(sa1_trace_p1_rec, p1))
                    fn = rec_field_at(k - offsetof(sa1_trace_p1_rec, p1), &rel);
                else
                    rel = k;
                if (ndiff < 12)
                    printf("  step %d: %s%s+0x%X: %02X (reference) vs %02X (now)\n", i,
                           k >= offsetof(sa1_trace_p1_rec, p1) ? "p1." : "", fn, (unsigned)rel, ((uint8_t *)&r)[k],
                           ((uint8_t *)&mine)[k]);
                ndiff++;
                if (k >= offsetof(sa1_trace_p1_rec, p1)) {
                    size_t rr;
                    while (k + 1 < sizeof r && rec_field_at(k + 1 - offsetof(sa1_trace_p1_rec, p1), &rr) == fn)
                        k++;
                }
            }
        if (ndiff >= 12)
            break;
    }
    fclose(f);
    if (!bad && (int)x.count != nrecs) {
        printf("check: %u sidecar records in %s, %d now\n", x.count, path, nrecs);
        bad = 1;
    }
    if (!bad)
        printf("check: player-1 sidecar identical to %s (%d steps)\n", path, nrecs);
    return bad;
}

/* ------------------------------------------------------------------ */
/* landtable geometry export (--dump-land PREFIX, version 2): every COL */
/* entry of the act's landtable, the triangles exactly as the land      */
/* collision cache build emits them into the COLPOLY pool (8C028EC2 ->  */
/* 8C028B00 / 8C0287A0 / 8C028666: unit * T * Rz * Ry * Rx * S per node, */
/* children first, evalflags ignored; strip winding by (flip + i) & 1), */
/* with the original SDK matrix code running in the interpreter.        */
/* PREFIX.tri + PREFIX.col (sa1_trace.h) and PREFIX.obj (collidable).   */

#define NJ_CALC_POINT  0x8C638ECCu
#define NJ_POP_MATRIX  0x8C639B98u
#define NJ_PUSH_MATRIX 0x8C639C70u
#define NJ_ROTATE_X    0x8C639EC8u
#define NJ_ROTATE_Y    0x8C639F5Cu
#define NJ_ROTATE_Z    0x8C63A1CCu
#define NJ_SCALE_V     0x8C63A5ECu
#define NJ_TRANSLATE_V 0x8C63A804u
#define UNIT_MATRIX    0x8C67C640u   /* D_8C67C640, the matrix 8C028EC2 pushes */
#define SCRATCH        0x8C000C00u
#define STRIP_MAX      0x400         /* STRIPBUF vertices (8C028666 cuts longer strips) */

static void nj1(uint32_t fn, uint32_t a)
{
    sh4_arg args[1];
    args[0] = sh4_ai(a);
    sh4_invoke(fn, args, 1, NULL, NULL);
}

static void nj2(uint32_t fn, uint32_t a, uint32_t b)
{
    sh4_arg args[2];
    args[0] = sh4_ai(a);
    args[1] = sh4_ai(b);
    sh4_invoke(fn, args, 2, NULL, NULL);
}

/* njCalcPoint(NULL, in, out) on raw float bits (no host float round trip) */
static void calc_point_u(const uint32_t in[3], uint32_t out[3])
{
    sh4_arg args[3];
    int k;
    for (k = 0; k < 3; k++)
        sh4_write32(SCRATCH + 4 * (uint32_t)k, in[k]);
    args[0] = sh4_ai(0);
    args[1] = sh4_ai(SCRATCH);
    args[2] = sh4_ai(SCRATCH + 16);
    sh4_invoke(NJ_CALC_POINT, args, 3, NULL, NULL);
    for (k = 0; k < 3; k++)
        out[k] = sh4_read32(SCRATCH + 16 + 4 * (uint32_t)k);
}

/* the node transform of 8C028EC2 / 8C028B00 (angles skipped when 0) */
static void land_node_matrix(uint32_t obj)
{
    nj2(NJ_TRANSLATE_V, 0, obj + 0x08);
    if (sh4_read32(obj + 0x1C))
        nj2(NJ_ROTATE_Z, 0, sh4_read32(obj + 0x1C));
    if (sh4_read32(obj + 0x18))
        nj2(NJ_ROTATE_Y, 0, sh4_read32(obj + 0x18));
    if (sh4_read32(obj + 0x14))
        nj2(NJ_ROTATE_X, 0, sh4_read32(obj + 0x14));
    nj2(NJ_SCALE_V, 0, obj + 0x20);
}

typedef struct land_ctx {
    sa1_land_tri *tris;
    int ntri, cap;
    int nnodes;                 /* over all COLs */
    int col_nodes;              /* nodes of the current COL */
    uint32_t col, flags, emit;
} land_ctx;

static void land_put(land_ctx *L, const uint32_t w[][3], const uint32_t lv[][3], const uint16_t *idx, int a, int b,
                     int c, uint32_t obj, int node, int mset, int mesh, int tri, uint32_t attr, uint16_t type)
{
    sa1_land_tri *t;
    int v[3], k, j;
    if (L->ntri == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 4096;
        L->tris = (sa1_land_tri *)realloc(L->tris, sizeof(sa1_land_tri) * (size_t)L->cap);
    }
    t = &L->tris[L->ntri++];
    memset(t, 0, sizeof *t);
    v[0] = a;
    v[1] = b;
    v[2] = c;
    for (k = 0; k < 3; k++) {
        for (j = 0; j < 3; j++) {
            memcpy(&t->v[3 * k + j], &w[v[k]][j], 4);
            memcpy(&t->lv[3 * k + j], &lv[v[k]][j], 4);
        }
        t->idx[k] = idx[v[k]];
    }
    t->flags = L->flags;
    t->col = L->col;
    t->attrflags = attr;
    t->emit = L->emit++;
    t->node_obj = obj;
    t->node = (uint16_t)node;
    t->mset = (uint16_t)mset;
    t->mesh = (uint16_t)mesh;
    t->tri = (uint16_t)tri;
    t->type = type;
}

/* 8C0287A0 + 8C028666: the triangles of one node's model under the current matrix */
static void land_polys(land_ctx *L, uint32_t obj)
{
    static uint32_t w[STRIP_MAX][3], lv[STRIP_MAX][3];
    static uint16_t idx[STRIP_MAX];
    uint32_t model = sh4_read32(obj + 4), pts, msets, mats;
    int nms, m, node = L->col_nodes++;
    L->nnodes++;
    if (!sh4_is_ram(model))
        return;
    pts = sh4_read32(model);
    msets = sh4_read32(model + 0x0C);
    mats = sh4_read32(model + 0x10);
    nms = sh4_read16(model + 0x14);
    for (m = 0; m < nms; m++) {
        uint32_t ms = msets + (uint32_t)m * 0x18, mp = sh4_read32(ms + 4), attr = 0;
        uint16_t tm = sh4_read16(ms);
        int type = tm & 0xC000, nmesh = sh4_read16(ms + 2), n = type == 0 ? 3 : 4, p;
        if (mats)
            attr = sh4_read32(mats + 0x14u * (tm & 0x3FFFu) + 0x10);
        for (p = 0; p < nmesh && sh4_is_ram(mp); p++) {
            int flip = 0, t;
            if (type & 0x8000) {
                uint16_t hd = sh4_read16(mp);
                n = hd & 0x7FFF;
                flip = (hd & 0x8000) != 0;
                if (n > STRIP_MAX)
                    n = STRIP_MAX;      /* the rest of the strip is read as the next header, like the original */
                mp += 2;
            }
            for (t = 0; t < n; t++) {
                uint32_t pa;
                int k;
                idx[t] = sh4_read16(mp);
                mp += 2;
                pa = pts + 12u * (uint32_t)(int32_t)(int16_t)idx[t];   /* points[(Sint16)index] */
                for (k = 0; k < 3; k++)
                    lv[t][k] = sh4_read32(pa + 4 * (uint32_t)k);
                calc_point_u(lv[t], w[t]);
            }
            for (t = 0; t < n - 2; t++) {
                if (((flip + t) & 1) == 0)
                    land_put(L, (const uint32_t (*)[3])w, (const uint32_t (*)[3])lv, idx, t, t + 1, t + 2, obj,
                             node, m, p, t, attr, (uint16_t)type);
                else
                    land_put(L, (const uint32_t (*)[3])w, (const uint32_t (*)[3])lv, idx, t, t + 2, t + 1, obj,
                             node, m, p, t, attr, (uint16_t)type);
            }
        }
    }
}

/* 8C028B00: returns the sibling */
static uint32_t land_build(land_ctx *L, uint32_t obj, int depth)
{
    uint32_t child;
    nj1(NJ_PUSH_MATRIX, 0);
    land_node_matrix(obj);
    child = sh4_read32(obj + 0x2C);
    while (sh4_is_ram(child) && depth < 64)
        child = land_build(L, child, depth + 1);
    land_polys(L, obj);
    nj1(NJ_POP_MATRIX, 1);
    return sh4_read32(obj + 0x30);
}

static int dump_land(const char *prefix)
{
    uint32_t lt = sh4_read32(LANDTABLE), cols;
    int ncol, i, ncoll = 0, multi = 0;
    land_ctx L;
    sa1_land_col *cs;
    char path[1300];
    FILE *f;
    uint32_t mx;
    if (!sh4_is_ram(lt)) {
        printf("dump-land: no landtable loaded\n");
        return -1;
    }
    memset(&L, 0, sizeof L);
    ncol = (int16_t)sh4_read16(lt);
    if (ncol < 0)
        ncol = 0;
    cols = sh4_read32(lt + 0x0C);
    cs = (sa1_land_col *)calloc((size_t)(ncol ? ncol : 1), sizeof(sa1_land_col));
    sh4_cstack_depth = 0;
    nj1(NJ_PUSH_MATRIX, UNIT_MATRIX);
    for (i = 0; i < ncol; i++) {
        uint32_t e = cols + (uint32_t)i * 0x24, obj = sh4_read32(e + 0x18), model;
        sa1_land_col *c = &cs[i];
        int k;
        c->index = (uint32_t)i;
        c->flags = sh4_read32(e + 0x20);
        for (k = 0; k < 3; k++)
            c->center[k] = sh4_readf(e + 4 * (uint32_t)k);
        c->radius = sh4_readf(e + 0x0C);
        c->ywidth = sh4_readf(e + 0x10);
        c->zwidth = sh4_readf(e + 0x14);
        c->blockbit = sh4_read32(e + 0x1C);
        c->object = obj;
        c->tri_first = (uint32_t)L.ntri;
        if (c->flags & 0x400003u)
            ncoll++;
        if (!sh4_is_ram(obj))
            continue;
        c->evalflags = sh4_read32(obj);
        model = sh4_read32(obj + 4);
        for (k = 0; k < 3; k++) {
            memcpy(&c->pos[k], &(uint32_t){ sh4_read32(obj + 0x08 + 4 * (uint32_t)k) }, 4);
            c->ang[k] = sh4_read32(obj + 0x14 + 4 * (uint32_t)k);
            memcpy(&c->scl[k], &(uint32_t){ sh4_read32(obj + 0x20 + 4 * (uint32_t)k) }, 4);
        }
        c->model = model;
        if (sh4_is_ram(model)) {
            uint32_t in[3], out[3];
            for (k = 0; k < 3; k++) {
                in[k] = sh4_read32(model + 0x18 + 4 * (uint32_t)k);
                memcpy(&c->model_center[k], &in[k], 4);
            }
            memcpy(&c->model_r, &(uint32_t){ sh4_read32(model + 0x24) }, 4);
            c->npoints = sh4_read32(model + 8);
            c->nmeshsets = sh4_read16(model + 0x14);
            c->nmats = sh4_read16(model + 0x16);
            /* colobj.center: push(unit), T Rz Ry Rx S, njCalcPoint(model->center), pop */
            nj1(NJ_PUSH_MATRIX, UNIT_MATRIX);
            land_node_matrix(obj);
            calc_point_u(in, out);
            nj1(NJ_POP_MATRIX, 1);
            memcpy(c->world_center, out, sizeof out);
        }
        L.col = (uint32_t)i;
        L.flags = c->flags;
        L.emit = 0;
        L.col_nodes = 0;
        land_build(&L, obj, 0);
        c->nodes = (uint32_t)L.col_nodes;
        c->tri_count = (uint32_t)L.ntri - c->tri_first;
        if (L.col_nodes > 1)
            multi++;
    }
    nj1(NJ_POP_MATRIX, 1);
    make_dirs(prefix);
    snprintf(path, sizeof path, "%s.tri", prefix);
    f = fopen(path, "wb");
    if (f) {
        sa1_land_header h;
        memset(&h, 0, sizeof h);
        h.magic = SA1_LAND_MAGIC;
        h.version = SA1_LAND_VERSION;
        h.ntri = (uint32_t)L.ntri;
        h.ncol = (uint32_t)ncol;
        h.tri_size = sizeof(sa1_land_tri);
        h.col_size = sizeof(sa1_land_col);
        h.landtable = lt;
        h.nnodes = (uint32_t)L.nnodes;
        fwrite(&h, sizeof h, 1, f);
        fwrite(L.tris, sizeof(sa1_land_tri), (size_t)L.ntri, f);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s.col", prefix);
    f = fopen(path, "wb");
    if (f) {
        sa1_land_col_header h;
        memset(&h, 0, sizeof h);
        h.magic = SA1_LANDCOL_MAGIC;
        h.version = SA1_LAND_VERSION;
        h.ncol = (uint32_t)ncol;
        h.col_size = sizeof(sa1_land_col);
        h.landtable = lt;
        h.ntri = (uint32_t)L.ntri;
        fwrite(&h, sizeof h, 1, f);
        fwrite(cs, sizeof(sa1_land_col), (size_t)ncol, f);
        fclose(f);
    }
    snprintf(path, sizeof path, "%s.obj", prefix);
    f = fopen(path, "w");
    mx = sh4_host_mxcsr_get();
    sh4_host_mxcsr_set(0x1F80);
    if (f) {
        int last = -1, nv = 0;
        fprintf(f, "# landtable %08X: %d COL entries, %d collidable (flags & 0x400003), %d triangles\n", lt, ncol,
                ncoll, L.ntri);
        fprintf(f, "# only collidable entries are written here; the .tri file has all, with flags\n");
        for (i = 0; i < L.ntri; i++) {
            const sa1_land_tri *tr = &L.tris[i];
            if (!(tr->flags & 0x400003u))
                continue;
            if ((int)tr->col != last) {
                fprintf(f, "o col%03u_%08X\n", tr->col, tr->flags);
                last = (int)tr->col;
            }
            fprintf(f, "v %.9g %.9g %.9g\nv %.9g %.9g %.9g\nv %.9g %.9g %.9g\nf %d %d %d\n", (double)tr->v[0],
                    (double)tr->v[1], (double)tr->v[2], (double)tr->v[3], (double)tr->v[4], (double)tr->v[5],
                    (double)tr->v[6], (double)tr->v[7], (double)tr->v[8], nv + 1, nv + 2, nv + 3);
            nv += 3;
        }
        fclose(f);
    }
    sh4_host_mxcsr_set(mx);
    printf("dump-land: landtable %08X: %d COL entries (%d collidable), %d nodes (%d COLs with children), "
           "%d triangles -> %s.tri/.col/.obj (version %d)\n",
           lt, ncol, ncoll, L.nnodes, multi, L.ntri, prefix, SA1_LAND_VERSION);
    free(L.tris);
    free(cs);
    return 0;
}

/* ------------------------------------------------------------------ */
/* routing subsets (bisecting a divergence to one decompiled function)  */

static int route_match(const sh4_native_fn *nf, const char *list)
{
    const char *p = list;
    while (p && *p) {
        char item[128];
        const char *base;
        int n = 0;
        while (*p == ',' || *p == ' ')
            p++;
        while (*p && *p != ',' && n < 127)
            item[n++] = *p++;
        item[n] = 0;
        if (!n)
            continue;
        if (!strcmp(item, nf->name))
            return 1;
        if (strtoul(item, NULL, 16) == nf->guest && nf->guest)
            return 1;
        base = nf->file ? strrchr(nf->file, '/') : NULL;
        base = base ? base + 1 : nf->file;
        if (base && !strcmp(item, base))
            return 1;
    }
    return 0;
}

/* after sh4_route_native(1): undo the routing of the functions not selected */
static int route_select(const char *only, const char *except)
{
    int i, n = 0;
    for (i = 0; i < sh4_native_fn_count; i++) {
        const sh4_native_fn *nf = &sh4_native_fns[i];
        void *user = NULL;
        int keep;
        if (!nf->guest || !nf->adapter)
            continue;
        keep = (!only || route_match(nf, only)) && !(except && route_match(nf, except));
        if (sh4_hook_find(nf->guest, NULL, &user, NULL) && user == (void *)nf) {
            if (keep)
                n++;
            else
                sh4_hook_remove(nf->guest);
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* commands                                                            */

static void usage(void)
{
    puts("usage: sa1h trace [--level N] [--act N] [--chr N] [--frames N | --steps N]\n"
         "                  [--input \"STEPS X Y [BUTTONS];...\" | --input @script.txt]\n"
         "                  [--out PREFIX] [--check OLD.bin | --check-decoded OLD.bin] [--no-objects] [--spf N]\n"
         "                  [--disp] [--no-screen] [--log-vis FILE] [--vis-force 0|1|sub0] [--vis-only PR,..]\n"
         "                  [--discover]\n"
         "                  [--trace-v1 | --trace-v2]\n"
         "                  [--route | --route-all] [--route-only F,..] [--route-except F,..]\n"
         "                  [--dump-land PREFIX]   (act collision geometry -> PREFIX.tri/.col (v2) + PREFIX.obj)\n"
         "                  [--dump-ram FILE]      (16 MB guest RAM after the stage start)\n"
         "                  [--dump-ram-at N,..]   (16 MB guest RAM before logic step N ('end': after the last)\n"
         "                                          -> <out>.stepN.ram / <out>.end.ram)\n"
         "                  [--log-sdk FILE [--sdk-list FILE]]   (calls from game code into the SDK / C runtime)\n"
         "                  [--log-2d FILE]        (the 2D draws njDrawSprite2D / njDrawPolygon2D with their sprite,\n"
         "                                          points and draw state; harness/tools/draw2d.py)\n"
         "                  [--dump-world FILE [--world-syms FILE] [--world-all]]   (globals per step)\n"
         "                  [--census FILE]        (bytes / 4 KB blocks the game writes; pure runs)\n"
         "                  [--coverage FILE]      (functions executed during the logic steps)\n"
         "                  [--disc DIR] [-v] [--print N] [--fast]\n"
         "                  [--no-image-guard]     (route overlay functions by address alone: negative control)\n"
         "                  [--no-level-tasks]     (the stage start creates no D_8C19DE30 / D_8C19E0E0 level task)\n"
         "                  [--super-free]         (level 19 act 0 chr 0: the B_CHAOS7 level task is a stand-in that\n"
         "                                          creates only the sea plane 0C91514A and the trigger 0C914D60)\n"
         "                  [--create-task ADDR[:IM[:LIST]]]   (CreateElementalTask(IM=2, LIST=0, ADDR) after\n"
         "                                          LoadPlayerTask; repeatable; ...[@X,Y,Z[,ANGY]][/SX,SY,SZ][+ocp[=HEX]]\n"
         "                                          [+late]: taskwk pos / ang.y / scl, an OBJ_CONDITION in B_8C79A3E0 as\n"
         "                                          the SET spawner writes it (ssCondition 8001, or HEX: 8005 = a ground\n"
         "                                          item box already broken), made after the whole stage start; /SX..,\n"
         "                                          +ocp or +late: host-object sidecar version 2)\n"
         "                  [--pno1]               (player 1 + follower AI sidecar <out>.p1.bin, sa1_trace_ext.h)\n"
         "                  [--hostobj FILE]       (libsa1's host-object layer under FILE's calls: <out>.obj.bin;\n"
         "                                          harness/src/ho_oracle.c; --create-task ...@X,Y,Z[,ANGY] places a task)\n"
         "  traces are version 2 (sa1_trace.h) unless --trace-v1; --check accepts v1 and v2 references\n"
         "  the screen (njInitSystem's view part, header flag 8) is set up unless --no-screen (the zero screen\n"
         "  of the references before 2026-10-20, kept as build/traces/ref/<name>.noscreen.bin)\n"
         "  F = function name, guest address or source file code_XXXXXXXX.c\n"
         "  STEPS = 60 Hz logic steps; X,Y = analog stick (-128..127, Y<0 = forward/up);\n"
         "  BUTTONS = A B C D X Y Z START UP DOWN LEFT RIGHT L R joined by '+', or 0xMASK\n"
         "  example: --input \"60 0 0; 60 0 -127; 1 0 -127 A; 59 0 -127\"");
}

int cmd_trace(int argc, char **argv)
{
    sim_cfg cfg;
    const char *check = NULL, *input_name = "none";
    char outbuf[1200];
    int i, f, spf, want_steps;
    uint32_t stage_size = 0;
    sa1_trace_header h;
    memset(&cfg, 0, sizeof cfg);
    cfg.level = 1;
    cfg.print_every = 30;
    cfg.screen = 1;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--level") && v) { cfg.level = atoi(v); i++; }
        else if (!strcmp(a, "--act") && v) { cfg.act = atoi(v); i++; }
        else if (!strcmp(a, "--chr") && v) { cfg.chr = atoi(v); i++; }
        else if (!strcmp(a, "--frames") && v) { cfg.frames = atoi(v); i++; }
        else if (!strcmp(a, "--steps") && v) { cfg.steps = atoi(v); i++; }
        else if (!strcmp(a, "--input") && v) { cfg.input = v; i++; }
        else if (!strcmp(a, "--out") && v) { cfg.out = v; i++; }
        else if (!strcmp(a, "--check") && v) { check = v; i++; }
        else if (!strcmp(a, "--check-decoded") && v) { check = v; check_decoded = 1; i++; }
        else if (!strcmp(a, "--disc") && v) { cfg.disc = v; i++; }
        else if (!strcmp(a, "--print") && v) { cfg.print_every = atoi(v); i++; }
        else if (!strcmp(a, "--spf") && v) { cfg.spf = atoi(v); i++; }
        else if (!strcmp(a, "--disp")) cfg.disp = 1;
        else if (!strcmp(a, "--screen")) cfg.screen = 1;        /* the default; accepted for old command lines */
        else if (!strcmp(a, "--no-screen")) cfg.screen = 0;
        else if (!strcmp(a, "--log-vis") && v) { cfg.log_vis = v; i++; }
        else if (!strcmp(a, "--vis-force") && v) { cfg.vis_force = v; i++; }
        else if (!strcmp(a, "--vis-only") && v) { cfg.vis_only = v; i++; }
        else if (!strcmp(a, "--no-objects")) cfg.no_objects = 1;
        else if (!strcmp(a, "--fast")) cfg.no_cstack = 1;
        else if (!strcmp(a, "--route")) cfg.route = 1;
        else if (!strcmp(a, "--route-all")) cfg.route = 2;
        else if (!strcmp(a, "--trace-v1")) cfg.trace_v1 = 1;
        else if (!strcmp(a, "--trace-v2")) cfg.trace_v1 = 0;
        else if (!strcmp(a, "--dump-land") && v) { cfg.dump_land = v; i++; }
        else if (!strcmp(a, "--dump-ram") && v) { cfg.dump_ram = v; i++; }
        else if (!strcmp(a, "--dump-ram-at") && v) { cfg.dump_ram_at = v; i++; }
        else if (!strcmp(a, "--log-sdk") && v) { cfg.log_sdk = v; i++; }
        else if (!strcmp(a, "--log-2d") && v) { cfg.log_2d = v; i++; }
        else if (!strcmp(a, "--sdk-list") && v) { cfg.sdk_list = v; i++; }
        else if (!strcmp(a, "--dump-world") && v) { cfg.dump_world = v; i++; }
        else if (!strcmp(a, "--world-syms") && v) { cfg.world_syms = v; i++; }
        else if (!strcmp(a, "--world-all")) cfg.world_all = 1;
        else if (!strcmp(a, "--census") && v) { cfg.census = v; i++; }
        else if (!strcmp(a, "--coverage") && v) { cfg.coverage = v; i++; }
        else if (!strcmp(a, "--route-only") && v) { cfg.route_only = v; cfg.route = cfg.route ? cfg.route : 1; i++; }
        else if (!strcmp(a, "--route-except") && v) { cfg.route_except = v; cfg.route = cfg.route ? cfg.route : 1; i++; }
        else if (!strcmp(a, "--discover")) cfg.discover = 1;
        else if (!strcmp(a, "--no-image-guard")) cfg.no_guard = 1;
        else if (!strcmp(a, "--super-free")) cfg.super_free = 1;
        else if (!strcmp(a, "--no-level-tasks")) cfg.no_level_tasks = 1;
        else if (!strcmp(a, "--pno1")) cfg.pno1 = 1;
        else if (!strcmp(a, "--hostobj") && v) { cfg.hostobj = v; i++; }
        else if (!strcmp(a, "--create-task") && v && cfg.n_create < 8) {
            char *e;
            cfg.create_addr[cfg.n_create] = (uint32_t)strtoul(v, &e, 16);
            cfg.create_im[cfg.n_create] = 2;
            cfg.create_list[cfg.n_create] = 0;
            if (*e == ':')
                cfg.create_im[cfg.n_create] = (uint32_t)strtoul(e + 1, &e, 10);
            if (*e == ':')
                cfg.create_list[cfg.n_create] = (uint32_t)strtoul(e + 1, &e, 10);
            if (*e == '@') {
                int k;
                cfg.create_has_pos[cfg.n_create] = 1;
                for (k = 0; k < 3; k++) {
                    char *e2;
                    cfg.create_pos[cfg.n_create][k] = strtof(e + 1, &e2);
                    e = e2;
                    if (k < 2 && *e != ',')
                        break;
                }
                if (*e == ',')
                    cfg.create_angy[cfg.n_create] = (uint32_t)strtoul(e + 1, &e, 16) & 0xFFFFu;
            }
            /* round 0.9.2: [/SX,SY,SZ] taskwk.scl, [+ocp] an OBJ_CONDITION (sidecar version 2); round 0.9.4: [+ocp=HEX]
               its initial ssCondition */
            for (;;) {
                if (*e == '/' && !(cfg.create_flags[cfg.n_create] & SA1_TOBJ_T_SCL)) {
                    int k;
                    cfg.create_flags[cfg.n_create] |= SA1_TOBJ_T_SCL;
                    for (k = 0; k < 3; k++) {
                        char *e2;
                        cfg.create_scl[cfg.n_create][k] = strtof(e + 1, &e2);
                        if (e2 == e + 1)
                            break;
                        e = e2;
                        if (k < 2 && *e != ',')
                            break;
                    }
                    if (k < 3)
                        e = (char *)"!";
                } else if (!strncmp(e, "+ocp", 4) && !(cfg.create_flags[cfg.n_create] & SA1_TOBJ_T_OCP)) {
                    cfg.create_flags[cfg.n_create] |= SA1_TOBJ_T_OCP;
                    cfg.create_ocpw[cfg.n_create] = 0x8001u;
                    e += 4;
                    if (*e == '=') {    /* round 0.9.4: +ocp=HEX, the initial ssCondition (Review R.7) */
                        char *e2;
                        unsigned long w = strtoul(e + 1, &e2, 16);
                        if (e2 == e + 1 || w > 0xFFFFul) {
                            e = (char *)"!";        /* refused below: want 1-4 hex digits */
                        } else {
                            cfg.create_flags[cfg.n_create] |= SA1_TOBJ_T_OCPW;
                            cfg.create_ocpw[cfg.n_create] = (uint32_t)w;
                            e = e2;
                        }
                    }
                } else if (!strncmp(e, "+late", 5) && !(cfg.create_flags[cfg.n_create] & SA1_TOBJ_T_LATE)) {
                    cfg.create_flags[cfg.n_create] |= SA1_TOBJ_T_LATE;
                    e += 5;
                } else {
                    break;
                }
            }
            if (*e || !cfg.create_addr[cfg.n_create]) {
                fprintf(stderr, "--create-task %s: want ADDR[:IM[:LIST]][@X,Y,Z[,ANGY]][/SX,SY,SZ][+ocp[=HEX]][+late] (hex address, "
                                "decimal im / list, SA1 position, hex ang.y, SA1 scale)\n", v);
                return 2;
            }
            cfg.n_create++;
            i++;
        }
        else if (!strcmp(a, "-v")) cfg.verbose = 1;
        else { usage(); return 2; }
    }
    trace_v1 = cfg.trace_v1;
    if (cfg.super_free && (cfg.level != 19 || cfg.act != 0 || cfg.chr != 0 || cfg.no_level_tasks)) {
        fprintf(stderr, "--super-free needs --level 19 --act 0 --chr 0 (the Perfect Chaos stage, Sonic) and the "
                        "level task (not --no-level-tasks)\n");
        return 2;
    }
    px_pno1 = cfg.pno1;
    if (cfg.hostobj && ho_oracle_load(cfg.hostobj))
        return 2;
    if (cfg.dump_ram_at && ramat_parse(cfg.dump_ram_at))
        return 2;
    if (load_script(cfg.input))
        return 2;
    if (cfg.input) {
        input_name = cfg.input[0] == '@' ? cfg.input + 1 : "inline";
        for (i = 0; cfg.input[0] == '@' && cfg.input[i]; i++)       /* basename of the script */
            if (cfg.input[i] == '/' || cfg.input[i] == '\\')
                input_name = cfg.input + i + 1;
    }
    if (!cfg.out) {
        char nm[64], *dot;
        snprintf(nm, sizeof nm, "%s", input_name);
        if ((dot = strrchr(nm, '.')) != NULL)
            *dot = 0;
        snprintf(outbuf, sizeof outbuf, "%s/build/traces/L%02dA%dC%d_%s", g_root, cfg.level, cfg.act, cfg.chr, nm);
        cfg.out = outbuf;
    }
    snprintf(ramat_prefix, sizeof ramat_prefix, "%s", cfg.out);
    if (sim_setup(&cfg))
        return 1;
    census_end_setup();
    fill_header(&h, &cfg, input_name, stage_size);
    if (cfg.dump_ram) {
        FILE *rf;
        make_dirs(cfg.dump_ram);
        rf = fopen(cfg.dump_ram, "wb");
        if (rf) {
            fwrite(sh4_ram(), 1, SH4_RAM_SIZE, rf);
            fclose(rf);
            printf("sim: guest RAM after the stage start (0x8C000000-0x8CFFFFFF) -> %s\n", cfg.dump_ram);
        }
    }
    {
        uint32_t twp = sh4_read32(PLAYER_TWP);
        if (sh4_is_ram(twp))
            for (i = 0; i < 3; i++) {
                h.start_pos[i] = sh4_readf(twp + 0x20 + 4 * (uint32_t)i);
                h.start_ang[i] = sh4_read32(twp + 0x14 + 4 * (uint32_t)i);
            }
    }
    if (cfg.spf > 0)
        sh4_write32(STEPS_PER_FRM, (uint32_t)cfg.spf);
    spf = (int)sh4_read32(STEPS_PER_FRM);
    h.steps_per_frame = (uint8_t)spf;
    if (spf < 1)
        spf = 1;
    want_steps = cfg.steps > 0 ? cfg.steps : cfg.frames > 0 ? cfg.frames * spf : nsegs ? script_steps() : 120;
    sim_want_steps = want_steps;
    retry_reset = frame_retry_reset;
    if (cfg.route) {
        int nr = 0;
        for (i = 0; i < sh4_native_fn_count; i++)
            if (sh4_native_fns[i].guest && sh4_native_fns[i].adapter)
                nr++;
        sh4_route_native(1);
        if (cfg.route_only || cfg.route_except)
            nr = route_select(cfg.route_only, cfg.route_except);
        printf("sim: routing %d native decompiled functions into the run\n", nr);
        if (cfg.no_guard)
            printf("sim: --no-image-guard: overlay functions are routed by address, whatever image is loaded\n");
        else
            guard_install();
        if (cfg.verbose)
            for (i = 0; i < sh4_native_fn_count; i++) {
                void *user = NULL;
                if (sh4_native_fns[i].guest && sh4_hook_find(sh4_native_fns[i].guest, NULL, &user, NULL) &&
                    user == (void *)&sh4_native_fns[i])
                    printf("  routed %08X %s\n", sh4_native_fns[i].guest, sh4_native_fns[i].name);
            }
    }
    /* after the routing set-up: these chain to the routed native hooks they replace */
    if (cfg.log_sdk && sdk_install(&cfg))
        return 1;
    if (cfg.log_2d && d2d_install())
        return 1;
    if ((cfg.log_vis || cfg.vis_force || cfg.vis_only) && vis_install(cfg.log_vis, cfg.vis_force, cfg.vis_only))
        return 1;
    if (cfg.dump_world) {
        if (world_load(&cfg))
            return 1;
        world_snapshot("@start", 1);
    }
    /* --hostobj: libsa1's layer, its hooks on the executor / CCL (after the routing set-up: they chain to the routed
     * native hooks they replace) and the proxies' exec 0x7F000100 */
    if (ho_oracle_setup())
        return 1;
    step_hook_on = 1;
    sh4_hook_add(STEP_FN, step_hook, NULL, "logic step");
    if (cfg.no_cstack && !cfg.discover)
        sh4_cstack_on = 0;
    printf("sim: running %d logic steps (%d per display frame)\n", want_steps, spf);
    if (cfg.coverage)
        sh4_cov = (uint8_t *)calloc(1u << 20, 1);
    for (f = 0; cur_step < want_steps; f++) {
        char what[32];
        cur_frame = f;
        frame_step0 = cur_step;
        frame_nrec0 = nrecs;
        sh4_write32(VSYNC_CNT, 0);
        snprintf(what, sizeof what, "frame %d", f);
        d2d_frame_begin();
        if (sdk_fp) {
            sdk_rearm();
            sdk_phase_set('F', cur_step - 1);
        }
        if (sim_run(what, FRAME_FN, NULL, 0, 100000000))
            break;
        if (cfg.disp) {
            snprintf(what, sizeof what, "disp %d", f);
            if (sdk_fp) {
                sdk_rearm();
                sdk_phase_set('D', cur_step - 1);
            }
            d2d_disp = 1;
            if (sim_run(what, TASK_DISP_ALL, NULL, 0, 100000000))
                break;
        }
        if (cfg.print_every > 0 && nrecs > 0) {
            int k;
            for (k = frame_nrec0; k < nrecs; k++) {
                const sa1_trace_rec *r = &recs[k];
                if ((int)r->step % cfg.print_every == 0 || (int)r->step == want_steps - 1)
                    printf("  step %4u: in (%4d,%4d,%04X) mode %3d act %3u pos (%10.4f %10.4f %10.4f) spd (%8.4f %8.4f %8.4f) "
                           "ang %04X attr %08X land %u\n",
                           r->step, r->stick_x, r->stick_y, r->buttons, r->mode, r->action, (double)r->pos[0],
                           (double)r->pos[1], (double)r->pos[2], (double)r->pspd[0], (double)r->pspd[1],
                           (double)r->pspd[2], r->ang[1] & 0xFFFF, r->attr, r->land_count);
            }
        }
    }
    step_hook_on = 0;
    sh4_hook_remove(STEP_FN);
    px_uninstall();
    ai_uninstall();
    census_point();             /* frame code after the last step */
    if (ramat_end)
        ramat_write("end");
    sdk_uninstall();
    d2d_uninstall();
    vis_close();
    world_close();
    if (cfg.census)
        census_write(cfg.census, &cfg, input_name, nrecs < want_steps ? nrecs : want_steps);
    if (cfg.coverage && sh4_cov) {
        /* functions whose entry ran in the interpreter during the logic steps
         * (natively routed functions are not seen: record coverage unrouted) */
        FILE *cf = fopen(cfg.coverage, "w");
        int ncov = 0;
        for (i = 0; cf && i < sh4_function_count; i++) {
            uint32_t a = sh4_functions[i].addr;
            if (sh4_cov[(a & 0x00FFFFFFu) >> 4] & (1u << ((a >> 1) & 7))) {
                fprintf(cf, "%08X %s\n", a, sh4_functions[i].name);
                ncov++;
            }
        }
        if (cf)
            fclose(cf);
        printf("sim: coverage: %d functions executed -> %s\n", ncov, cfg.coverage);
        free(sh4_cov);
        sh4_cov = NULL;
    }
    if (nrecs > want_steps)
        nrecs = want_steps;
    h.count = (uint32_t)nrecs;
    h.landtable = sh4_read32(LANDTABLE);
    printf("sim: %d steps, %.0f instructions per step\n", nrecs, nrecs ? (double)step_icount / nrecs : 0.0);
    {
        int miss = 0;
        for (i = 0; i < nrecs; i++)
            miss += recs[i].px_calls == 0;
        if (miss)
            printf("sim: note: the player exec entry was not seen in %d of %d steps (px_* fields 0)\n", miss, nrecs);
    }
    guard_report();
    if (px_pno1)
        p1_summary();
    ho_oracle_summary();
    if (ndstubs) {
        printf("sim: %d functions auto-stubbed (--discover):\n", ndstubs);
        for (i = 0; i < ndstubs; i++)
            printf("  %-28s %s%s\n", fname(dstubs[i].addr), dstubs[i].addr >= LIB_START ? "[SDK] " : "[game] ",
                   dstubs[i].why);
    }
    if (cfg.verbose) {
        printf("sim: files read through the GD-ROM HLE:\n");
        for (i = 0; i < ngdlogs; i++)
            printf("  %-16s %ld\n", gdlogs[i].name, gdlogs[i].size);
    }
    if (write_trace(cfg.out, &h))
        return 1;
    if (px_pno1 && write_p1(cfg.out, &h))
        return 1;
    if (ho_oracle_active() && ho_oracle_write(cfg.out, &h, nrecs))
        return 1;
    if (cfg.log_2d && d2d_write(cfg.log_2d, &cfg, input_name, nrecs))
        return 1;
    if (cfg.dump_land)
        dump_land(cfg.dump_land);
    if (check) {
        int bad = check_trace(check) ? 1 : 0;
        if (px_pno1)
            bad |= check_p1(check);
        bad |= ho_oracle_check(check, nrecs);
        return bad;
    }
    return nrecs == want_steps ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* menu: the ADVERTISE menu overlay as the main loop runs it (round     */
/* 0.9.2, docs/research/gsa1_090_charselect.md CS2 with its Review     */
/* RC4 / RC6; ported from build/research/charselect/harness/          */
/* t_menu_part*.c).  Game init 8C053AC0, ADVERTISE.PRS to 0x0C900000    */
/* by 8C09DA44 (main loop 8C053B60 case 10), the menu init             */
/* 8C9000F4(&submode) with the buInit HLE (8C602B68 -> D_8C961344 = 1), */
/* then one menu exec 8C9001DA per frame under the scripted pad.        */
/* RC4 boot (the default): the game's own direct entry submode 1 (the   */
/* screen sequence 0x8C90ED74 begins {1, 7, 9, 9}), the sequence ended  */
/* after that entry (the word 0x8C90ED84 = 0, written after the load),  */
/* no 8C90179E request, no title: the character select screen is made  */
/* in the game's own 130 creation frames (voice 4, BGM 24 at frame     */
/* 131).  --submode 0 --full-sequence --request 150:1 is the research's */
/* E1 boot (title, then the request as the main menu's Adventure       */
/* button makes it).                                                    */
/* Outputs: --log FILE, the research's menu log (state M / slot P each  */
/* frame, sound / voice / music / event action / SetInputP N, the menu  */
/* 2D helpers A / C, requests R, input I; with --log-draws also its    */
/* draw lines T / G / S / Q), and --log-2d FILE, the draws in the       */
/* format of trace --log-2d (kinds S, P, T, G, phase M, step = frame;   */
/* harness/tools/draw2d.py).  The four draw functions are logged and    */
/* not run (they only feed the TA), the 3D model renderers 8C03700C /   */
/* 8C037098 return at once (the models are counted at 8C036F12).        */
/* --coverage FILE [FROM]: every executed code word from frame FROM on  */
/* (default -1: from the ADVERTISE load) as address ranges.  --route,   */
/* --route-only F,.., --route-except F,..: native decompiled code       */
/* (staged ADVERTISE C under the image guard) replaces the originals.   */

#define ADV_INIT     0x8C9000F4u
#define ADV_EXEC     0x8C9001DAu
#define ADV_LOAD     0x8C09DA44u   /* file load + PRS decode (main loop 8C053B60 case 10) */
#define ADV_NAME     0x8C112B68u   /* "advertise.prs" */
#define ADV_SEQ_END  0x8C90ED84u   /* the submode-1 screen sequence's word after its first entry (RC4) */
#define GAME_SUBMODE 0x8C760968u
#define MENU_MASTER  0x8C9610CCu
#define CS_TASK3D    0x8C961348u
#define CS_SCREEN    0x8C9613A4u
#define CS_SEL       0x8C78B45Du
#define CS_TRIAL     0x8C78B45Au
#define CS_ALLOPEN   0x8C78B450u
#define BU_INIT      0x8C602B68u
#define BU_READY     0x8C961344u
#define REQ_MODE     0x8C90179Eu

static char *mlog_buf;
static size_t mlog_len, mlog_cap, mlog_mark;
static int mlog_on, mlog_draws, mframe;
static unsigned long m_counts[8];

static void m_put(const char *fmt, ...)
{
    va_list ap;
    int n;
    if (!mlog_on)
        return;
    for (;;) {
        if (mlog_cap - mlog_len < 512) {
            mlog_cap = mlog_cap ? mlog_cap * 2 : ((size_t)1 << 20);
            mlog_buf = (char *)realloc(mlog_buf, mlog_cap);
        }
        va_start(ap, fmt);
        n = vsnprintf(mlog_buf + mlog_len, mlog_cap - mlog_len, fmt, ap);
        va_end(ap);
        if (n >= 0 && (size_t)n < mlog_cap - mlog_len) {
            mlog_len += (size_t)n;
            return;
        }
        mlog_cap *= 2;
        mlog_buf = (char *)realloc(mlog_buf, mlog_cap);
    }
}

static void menu_retry_reset(void)
{
    mlog_len = mlog_mark;
    d2d_retry();
}

/* the name of texlist tl's entry idx, or of the entry whose texaddr is texaddr (when not 0): "name#idx" */
static void m_name_of(uint32_t tl, uint32_t idx, uint32_t texaddr, char *out, int n)
{
    uint32_t arr, cnt, i, e = 0, s;
    int k;
    out[0] = 0;
    if (!sh4_is_ram(tl)) {
        snprintf(out, (size_t)n, "-");
        return;
    }
    arr = sh4_read32(tl);
    cnt = sh4_read32(tl + 4);
    if (!sh4_is_ram(arr) || cnt > 4096) {
        snprintf(out, (size_t)n, "-");
        return;
    }
    if (texaddr) {
        for (i = 0; i < cnt; i++)
            if (sh4_read32(arr + 12 * i + 8) == texaddr) {
                e = arr + 12 * i;
                idx = i;
                break;
            }
    } else if (idx < cnt)
        e = arr + 12 * idx;
    if (!e) {
        snprintf(out, (size_t)n, "?");
        return;
    }
    s = sh4_read32(e);
    if (!sh4_is_ram(s)) {
        snprintf(out, (size_t)n, "#%u", idx);
        return;
    }
    for (k = 0; k < n - 8; k++) {
        int ch = sh4_read8(s + (uint32_t)k);
        if (!ch || !(isalnum(ch) || ch == '_' || ch == '-' || ch == '.'))
            break;
        out[k] = (char)ch;
    }
    out[k] = 0;
    snprintf(out + k, (size_t)(n - k), "#%u", idx);
}

/* the hooks the menu's logging hooks replaced (--route: the routing hook of a native function), chained to */
static struct { uint32_t addr; sh4_hook_fn fn; void *user; } m_prev[32];
static int n_m_prev;

static void m_chain(sh4_cpu *c, uint32_t addr)
{
    int k;
    for (k = 0; k < n_m_prev; k++)
        if (m_prev[k].addr == addr) {
            m_prev[k].fn(c, m_prev[k].user);     /* routed: the native function through its adapter */
            return;
        }
    set_hookbit(addr, 0);
    sh4_invoke(addr, NULL, 0, NULL, NULL);
    set_hookbit(addr, 1);
}

/* the research log's draw lines (--log-draws): logged, not run */
static void mh_ntex(sh4_cpu *c, void *u)
{
    uint32_t v = c->r[4], tl = sh4_read32(D2D_TEXLIST);
    int n = (int)c->r[5], i;
    char nm[48];
    (void)u;
    m_counts[0]++;
    m_name_of(tl, 0, c->r[6], nm, sizeof nm);
    m_put("T %d pr=%08X n=%d tex=%08X flag=%u tl=%08X name=%s", mframe, c->pr, n, c->r[6], c->r[7], tl, nm);
    for (i = 0; i < n && i < 8 && sh4_is_ram(v); i++) {
        uint32_t o = v + 24u * (uint32_t)i;
        m_put(" | %.2f,%.2f,%g uv %.4f,%.4f c %08X", (double)sh4_readf(o), (double)sh4_readf(o + 4),
              (double)sh4_readf(o + 8) != 0.0 ? 1.0 / (double)sh4_readf(o + 8) : 0.0, (double)sh4_readf(o + 12),
              (double)sh4_readf(o + 16), sh4_read32(o + 20));     /* z printed as w = 1 / z */
    }
    m_put("\n");
}

static void mh_npoly(sh4_cpu *c, void *u)
{
    uint32_t v = c->r[4];
    int n = (int)c->r[5], i;
    (void)u;
    m_counts[1]++;
    m_put("G %d pr=%08X n=%d trans=%u", mframe, c->pr, n, c->r[6]);
    for (i = 0; i < n && i < 8 && sh4_is_ram(v); i++) {
        uint32_t o = v + 16u * (uint32_t)i;
        m_put(" | %.2f,%.2f,%g c %08X", (double)sh4_readf(o), (double)sh4_readf(o + 4),
              (double)sh4_readf(o + 8) != 0.0 ? 1.0 / (double)sh4_readf(o + 8) : 0.0, sh4_read32(o + 12));
    }
    m_put("\n");
}

static void mh_spr(sh4_cpu *c, void *u)
{
    uint32_t sp = c->r[4], tl, ta;
    int n = (int)c->r[5];
    char nm[48] = "-";
    (void)u;
    m_counts[2]++;
    if (!sh4_is_ram(sp))
        return;
    tl = sh4_read32(sp + 0x18);
    ta = sh4_read32(sp + 0x1C) + 0x14u * (uint32_t)n;
    if (sh4_is_ram(ta))
        m_name_of(tl, (uint32_t)(uint16_t)sh4_read16(ta + 16), 0, nm, sizeof nm);
    m_put("S %d pr=%08X p=%.2f,%.2f sc=%.3f,%.3f ang=%08X n=%d attr=%08X name=%s", mframe, c->pr,
          (double)sh4_readf(sp), (double)sh4_readf(sp + 4), (double)sh4_readf(sp + 12), (double)sh4_readf(sp + 16),
          sh4_read32(sp + 20), n, c->r[6], nm);
    if (sh4_is_ram(ta))
        m_put(" sz=%d,%d c=%d,%d uv=%d,%d,%d,%d", (int)(int16_t)sh4_read16(ta), (int)(int16_t)sh4_read16(ta + 2),
              (int)(int16_t)sh4_read16(ta + 4), (int)(int16_t)sh4_read16(ta + 6), (int)(int16_t)sh4_read16(ta + 8),
              (int)(int16_t)sh4_read16(ta + 10), (int)(int16_t)sh4_read16(ta + 12), (int)(int16_t)sh4_read16(ta + 14));
    m_put(" cmat=%08X,%08X,%08X,%08X\n", sh4_read32(D2D_CMAT), sh4_read32(D2D_CMAT + 4), sh4_read32(D2D_CMAT + 8),
          sh4_read32(D2D_CMAT + 12));
}

static void mh_poly2d(sh4_cpu *c, void *u)
{
    uint32_t p = c->r[4], pts, col;
    int n = (int)c->r[5], i;
    (void)u;
    m_counts[3]++;
    if (!sh4_is_ram(p))
        return;
    pts = sh4_read32(p);
    col = sh4_read32(p + 4);
    m_put("Q %d pr=%08X n=%d attr=%08X", mframe, c->pr, n, c->r[6]);
    for (i = 0; i < n && i < 8 && sh4_is_ram(pts); i++)
        m_put(" | %.2f,%.2f c %08X", (double)sh4_readf(pts + 8u * (uint32_t)i),
              (double)sh4_readf(pts + 8u * (uint32_t)i + 4), sh4_is_ram(col) ? sh4_read32(col + 4u * (uint32_t)i) : 0u);
    m_put("\n");
}

/* the menu's own 2D helpers in 1ST_READ (8C07E0AE / 8C07E480 / 8C07E642: texture index | flags in r4, floats in
 * fr4..fr9) and the colour setters 8C07DF30 / 8C07DF6E: logged, then run */
static void mh_ava(sh4_cpu *c, void *u)
{
    uint32_t a = (uint32_t)(uintptr_t)u, tl = sh4_read32(D2D_TEXLIST);
    char nm[48];
    m_counts[4]++;
    m_name_of(tl, c->r[4] & 0xFFFFu, 0, nm, sizeof nm);
    m_put("A %d %08X pr=%08X r4=%08X f=%.2f,%.2f,%.3f,%.3f,%.3f,%.3f tl=%08X name=%s\n", mframe, a, c->pr, c->r[4],
          (double)c->fr.f[4], (double)c->fr.f[5], (double)c->fr.f[6], (double)c->fr.f[7], (double)c->fr.f[8],
          (double)c->fr.f[9], tl, nm);
    m_chain(c, a);
}

static void mh_col(sh4_cpu *c, void *u)
{
    uint32_t a = (uint32_t)(uintptr_t)u;
    m_put("C %d %08X pr=%08X %08X %08X %08X\n", mframe, a, c->pr, c->r[4], c->r[5], c->r[6]);
    m_chain(c, a);
}

/* sound: SE 8C0940DA(id, ...), BGM 8C107AC6(n), voice 8C107BA0(n), music stop 8C107AAA; event action 8C04A120;
 * SetInputP 8C07C65E; the model draw wrapper 8C036F12 (counted) */
static void mh_note(sh4_cpu *c, void *u)
{
    uint32_t a = (uint32_t)(uintptr_t)u;
    const char *what = a == 0x8C0940DAu ? "SE" : a == 0x8C107AC6u ? "BGM" : a == 0x8C107BA0u ? "VOICE" :
                       a == 0x8C107AAAu ? "MUSICSTOP" : a == 0x8C04A120u ? "EVACTION" : a == 0x8C07C65Eu ? "SETINPUTP" :
                       a == 0x8C036F12u ? "MODEL" : "?";
    if (a == 0x8C036F12u)
        m_counts[5]++;
    else
        m_put("N %d %s pr=%08X r4=%08X r5=%08X r6=%08X r7=%08X fr4=%.3f\n", mframe, what, c->pr, c->r[4], c->r[5],
              c->r[6], c->r[7], (double)c->fr.f[4]);
    m_chain(c, a);
}

static void hle_bu_init(sh4_cpu *c, void *u)
{
    (void)u;
    sh4_write32(BU_READY, 1);   /* what the init callback 8C9026B8 sets at its end: "no memory card work pending" */
    c->r[0] = 0;
}

/* M: the master task's mode / previous / result, the selection, the screen's state; P: the 7 character slots */
static void m_state(void)
{
    uint32_t mt = sh4_read32(MENU_MASTER), mw = sh4_is_ram(mt) ? sh4_read32(mt + 0x2C) : 0;
    uint32_t st = sh4_read32(CS_SCREEN), sw = sh4_is_ram(st) ? sh4_read32(st + 0x2C) : 0;
    uint32_t t3 = sh4_read32(CS_TASK3D), w3 = sh4_is_ram(t3) ? sh4_read32(t3 + 0x2C) : 0;
    int i;
    m_put("M %d master_mode=%d prev=%d result=%d sel=%u trial=%u", mframe,
          sh4_is_ram(mw) ? (int)sh4_read32(mw + 4) : -1, sh4_is_ram(mw) ? (int)sh4_read32(mw + 8) : -1,
          sh4_is_ram(mw) ? (int)sh4_read32(mw + 0x14) : -1, sh4_read8(CS_SEL), sh4_read8(CS_TRIAL));
    if (sh4_is_ram(sw))
        m_put(" cs_state=%d cs_proc=%d cs_sel=%d cs_fade=%.3f cs_slide=%.3f cs_rot=%08X cs_timer=%d",
              (int)sh4_read32(sw), (int)sh4_read32(sw + 0x1C), (int)sh4_read32(sw + 0x20), (double)sh4_readf(sw + 0x10),
              (double)sh4_readf(sw + 0x28), sh4_read32(sw + 0x18), (int)(int16_t)sh4_read16(sw + 0x2C));
    m_put("\n");
    if (sh4_is_ram(w3) && sh4_is_ram(t3) && sh4_read32(CS_TASK3D)) {
        for (i = 0; i < 7; i++) {
            uint32_t tp = sh4_read32(w3 + 4u * (uint32_t)i), twp, ewp, l;
            if (!sh4_is_ram(tp))
                continue;
            twp = sh4_read32(tp + 0x20);
            if (!sh4_is_ram(twp))
                continue;
            ewp = sh4_read32(twp + 0x3C);
            l = sh4_is_ram(ewp) ? sh4_read32(ewp + 0x40) : 0;
            m_put("P %d slot=%d exec=%08X mode=%d smode=%d flag=%04X chr=%u pos=%.2f,%.2f,%.2f ang=%04X,%04X,%04X",
                  mframe, i, sh4_read32(tp + 0x10), (int)(int8_t)sh4_read8(twp), (int)(int8_t)sh4_read8(twp + 1),
                  sh4_read16(twp + 4), sh4_read8(twp + 9), (double)sh4_readf(twp + 0x20), (double)sh4_readf(twp + 0x24),
                  (double)sh4_readf(twp + 0x28), sh4_read32(twp + 0x14) & 0xFFFF, sh4_read32(twp + 0x18) & 0xFFFF,
                  sh4_read32(twp + 0x1C) & 0xFFFF);
            {
                uint32_t cwp = sh4_read32(twp + 0x38), info = sh4_is_ram(cwp) ? sh4_read32(cwp + 0xC) : 0;
                if (sh4_is_ram(info))   /* the display translates to the collision info's +8 (8C0AE17A) */
                    m_put(" draw_at=%.2f,%.2f,%.2f", (double)sh4_readf(info + 8), (double)sh4_readf(info + 12),
                          (double)sh4_readf(info + 16));
            }
            if (sh4_is_ram(l))
                m_put(" ev: obj=%08X mot=%08X speed=%.3f mode=%u link=%u next=%08X frame=%.2f", sh4_read32(l + 4),
                      sh4_read32(l + 8), (double)sh4_readf(l + 0x14), sh4_read8(l), sh4_read8(l + 1),
                      sh4_read32(l + 0x10), (double)sh4_readf(ewp + 0x3C));
            m_put("\n");
        }
    }
}

/* a draw that is not run and not logged (neither --log-draws nor --log-2d) */
static void mh_skip(sh4_cpu *c, void *u)
{
    (void)c;
    (void)u;
}

static void m_hook(uint32_t a, sh4_hook_fn fn, const char *name)
{
    sh4_hook_fn prev = NULL;
    void *user = NULL;
    if (sh4_hook_find(a, &prev, &user, NULL) && prev && n_m_prev < 32) {
        m_prev[n_m_prev].addr = a;
        m_prev[n_m_prev].fn = prev;
        m_prev[n_m_prev++].user = user;
    }
    sh4_hook_add(a, fn, (void *)(uintptr_t)a, name);
}

static void menu_usage(void)
{
    puts("usage: sa1h menu [--frames N] [--input SCRIPT|@file] [--log FILE [--log-draws]] [--log-2d FILE]\n"
         "                 [--submode N] [--full-sequence] [--request FRAME:MODE]... [--unlock] [--trial]\n"
         "                 [--set8 ADDR=VAL]... [--state-every N] [--coverage FILE [FROM]] [--disc DIR]\n"
         "                 [--route | --route-only F,.. | --route-except F,..] [--no-image-guard]\n"
         "  the ADVERTISE menu overlay as the main loop runs it (docs/research/gsa1_090_charselect.md CS2, RC4):\n"
         "  default submode 1 with the screen sequence ended after its first entry (0x8C90ED84 = 0): the character\n"
         "  select screen built by the game in 130 frames. --submode 0 --full-sequence --request 150:1 = the\n"
         "  research's E1 boot (the title, then the Adventure request). --log: state / sound / 2D-helper lines\n"
         "  (--log-draws: also the draw lines T G S Q); --log-2d: the draws in trace --log-2d's format (S P T G,\n"
         "  phase M; harness/tools/draw2d.py). --set8 writes after the menu init. --coverage FROM -1 (default):\n"
         "  from the ADVERTISE load. Frames are 60 Hz menu frames (one exec 8C9001DA each).");
}

static int menu_write(const char *path, const char *what, const char *data, size_t n, const char *head)
{
    FILE *f;
    make_dirs(path);
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "menu: cannot write %s %s\n", what, path);
        return -1;
    }
    if (head)
        fputs(head, f);
    if (n)
        fwrite(data, 1, n, f);
    fclose(f);
    return 0;
}

int cmd_menu(int argc, char **argv)
{
    sh4_arg none[1], a2[2], a1;
    int frames = 600, i, f, unlock = 0, trial = 0, submode = 1, full_seq = 0, nreq = 0, state_every = 1, nset = 0;
    int req_frame[8], req_mode[8], cov_from = -1, route = 0, no_guard = 0, failed = 0;
    uint32_t set_addr[16], set_val[16];
    const char *input = NULL, *logpath = NULL, *log2d = NULL, *cov_path = NULL, *route_only = NULL, *route_except = NULL;
    const char *input_name = "-";
    char head[512];
    sim_cfg cfg;
    memset(&cfg, 0, sizeof cfg);
    memset(none, 0, sizeof none);
    cfg.screen = 1;
    for (i = 0; i < argc; i++) {
        const char *o = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(o, "--frames") && v) { frames = atoi(v); i++; }
        else if (!strcmp(o, "--input") && v) { input = v; i++; }
        else if (!strcmp(o, "--log") && v) { logpath = v; i++; }
        else if (!strcmp(o, "--log-draws")) mlog_draws = 1;
        else if (!strcmp(o, "--log-2d") && v) { log2d = v; i++; }
        else if (!strcmp(o, "--unlock")) unlock = 1;
        else if (!strcmp(o, "--trial")) trial = 1;
        else if (!strcmp(o, "--submode") && v) { submode = atoi(v); i++; }
        else if (!strcmp(o, "--full-sequence")) full_seq = 1;
        else if (!strcmp(o, "--state-every") && v) { state_every = atoi(v); i++; }
        else if (!strcmp(o, "--coverage") && v) {
            cov_path = v;
            i++;
            if (i + 1 < argc && (isdigit((unsigned char)argv[i + 1][0]) || argv[i + 1][0] == '-') &&
                strncmp(argv[i + 1], "--", 2)) {
                cov_from = atoi(argv[i + 1]);
                i++;
            }
        } else if (!strcmp(o, "--set8") && v && nset < 16) {
            /* --set8 ADDR=VAL (hex) written after the menu init: e.g. event flag bytes, 8C754BFC = 1 English */
            if (sscanf(v, "%x=%x", &set_addr[nset], &set_val[nset]) == 2)
                nset++;
            i++;
        } else if (!strcmp(o, "--request") && v && nreq < 8) {
            if (sscanf(v, "%d:%d", &req_frame[nreq], &req_mode[nreq]) == 2)
                nreq++;
            i++;
        } else if (!strcmp(o, "--disc") && v) { cfg.disc = v; i++; }
        else if (!strcmp(o, "--route")) route = 1;
        else if (!strcmp(o, "--route-only") && v) { route_only = v; route = 1; i++; }
        else if (!strcmp(o, "--route-except") && v) { route_except = v; route = 1; i++; }
        else if (!strcmp(o, "--no-image-guard")) no_guard = 1;
        else { fprintf(stderr, "menu: unknown option %s\n", o); menu_usage(); return 2; }
    }
    if (load_script(input))
        return 2;
    if (input) {
        input_name = input[0] == '@' ? input + 1 : "inline";
        for (i = 0; input[0] == '@' && input[i]; i++)
            if (input[i] == '/' || input[i] == '\\')
                input_name = input + i + 1;
    }
    if (harness_load_rom())
        return 1;
    if (cfg.disc)
        snprintf(disc_dir, sizeof disc_dir, "%s", cfg.disc);
    else
        snprintf(disc_dir, sizeof disc_dir, "%s/disc/SONICADV", g_root);
    snap = (uint8_t *)malloc(SH4_RAM_SIZE);
    discover = 1;
    sh4_strict_io = 1;
    sh4_cstack_on = 1;
    retry_reset = menu_retry_reset;
    mlog_on = logpath != NULL;
    install_hle();
    sh4_hook_add(NJ_INIT_SYSTEM, hle_nj_init_system, NULL, "njInitSystem view set-up (screen)");
    sh4_hook_add(0x8C03700Cu, sh4_hle_ret0, NULL, "hand-written model renderer (draw only)");
    sh4_hook_add(0x8C037098u, sh4_hle_ret0, NULL, "hand-written model renderer (draw only)");
    sh4_hook_add(BU_INIT, hle_bu_init, NULL, "buInit (memory card): ready at once");
    printf("menu: game init\n");
    if (sim_run("init", GAME_INIT, none, 0, 400000000))
        return 1;
    if (cov_path && cov_from < 0 && !sh4_cov)
        sh4_cov = (uint8_t *)calloc(1u << 20, 1);   /* coverage from the ADVERTISE load on */
    a2[0] = sh4_ai(ADV_NAME);
    a2[1] = sh4_ai(0x0C900000u);
    printf("menu: load advertise.prs through %s\n", fname(ADV_LOAD));
    if (sim_run("load", ADV_LOAD, a2, 2, 400000000))
        return 1;
    guard_epoch++;
    printf("menu: ADVERTISE entry table %08X %08X %08X\n", sh4_read32(0x8C900000u), sh4_read32(0x8C900004u),
           sh4_read32(0x8C900008u));
    if (submode == 1 && !full_seq) {
        sh4_write32(ADV_SEQ_END, 0);    /* RC4: the submode-1 sequence {1, 7, 9, 9} ends after its first entry */
        printf("menu: submode 1, the screen sequence ended after its first entry (%08X = 0)\n", ADV_SEQ_END);
    }
    if (route) {
        int nr = 0;
        for (i = 0; i < sh4_native_fn_count; i++)
            if (sh4_native_fns[i].guest && sh4_native_fns[i].adapter)
                nr++;
        sh4_route_native(1);
        if (route_only || route_except)
            nr = route_select(route_only, route_except);
        printf("menu: routing %d native decompiled functions into the run\n", nr);
        if (!no_guard)
            guard_install();
    }
    sh4_write32(GAME_SUBMODE, (uint32_t)submode);
    sh4_write32(GAME_MODE, 11);
    a1 = sh4_ai(GAME_SUBMODE);
    if (sim_run("adv init", ADV_INIT, &a1, 1, 400000000))
        return 1;
    if (unlock)
        sh4_write8(CS_ALLOPEN, 1);
    if (trial)
        sh4_write8(CS_TRIAL, 1);
    for (i = 0; i < nset; i++)
        sh4_write8(set_addr[i], (uint8_t)set_val[i]);
    if (mlog_draws) {
        m_hook(D2D_TEXTURE, mh_ntex, "njDrawTexture (menu log)");
        m_hook(D2D_POLYGON, mh_npoly, "njDrawPolygon (menu log)");
        m_hook(D2D_SPRITE2D, mh_spr, "njDrawSprite2D (menu log)");
        m_hook(D2D_POLY2D, mh_poly2d, "njDrawPolygon2D (menu log)");
    }
    if (log2d) {
        d2d_noexec = !mlog_draws;   /* the menu log's hooks (chained to) do not run the draw either */
        d2d_menu = 0;
        d2d_install();
    } else if (!mlog_draws) {
        m_hook(D2D_TEXTURE, mh_skip, "njDrawTexture (not run)");
        m_hook(D2D_POLYGON, mh_skip, "njDrawPolygon (not run)");
        m_hook(D2D_SPRITE2D, mh_skip, "njDrawSprite2D (not run)");
        m_hook(D2D_POLY2D, mh_skip, "njDrawPolygon2D (not run)");
    }
    m_hook(0x8C07E0AEu, mh_ava, "ava draw A");
    m_hook(0x8C07E480u, mh_ava, "ava draw B");
    m_hook(0x8C07E642u, mh_ava, "ava draw C");
    m_hook(0x8C07DF30u, mh_col, "ava colour 1");
    m_hook(0x8C07DF6Eu, mh_col, "ava colour 2");
    m_hook(0x8C0940DAu, mh_note, "SE");
    m_hook(0x8C107AC6u, mh_note, "BGM");
    m_hook(0x8C107BA0u, mh_note, "voice");
    m_hook(0x8C107AAAu, mh_note, "music stop");
    m_hook(0x8C04A120u, mh_note, "EV_SetAction");
    m_hook(0x8C07C65Eu, mh_note, "SetInputP");
    m_hook(0x8C036F12u, mh_note, "model draw");
    for (f = 0; f < frames; f++) {
        uint64_t ic = SH4.icount;
        char what[32];
        int k;
        mframe = f;
        d2d_menu = f;
        mlog_mark = mlog_len;
        d2d_frame_begin();
        for (k = 0; k < nreq; k++)
            if (req_frame[k] == f) {
                a1 = sh4_ai((uint32_t)req_mode[k]);
                m_put("R %d request mode %d (8C90179E)\n", f, req_mode[k]);
                sim_run("request", REQ_MODE, &a1, 1, 100000000);
            }
        if (cov_path && f == cov_from && !sh4_cov)
            sh4_cov = (uint8_t *)calloc(1u << 20, 1);
        script_at(f, &pad_x, &pad_y, &pad_buttons);
        if (pad_buttons)
            m_put("I %d buttons=%04X\n", f, pad_buttons);
        sh4_write32(VSYNC_CNT, 0);
        snprintf(what, sizeof what, "menu %d", f);
        if (sim_run(what, ADV_EXEC, none, 0, 200000000)) {
            printf("menu: frame %d failed\n", f);
            failed = 1;
            break;
        }
        if (state_every > 0 && f % state_every == 0)
            m_state();
        if (f % 60 == 0)
            printf("menu: frame %4d  %llu instructions  draws tex %lu poly %lu spr %lu p2d %lu ava %lu model %lu\n", f,
                   (unsigned long long)(SH4.icount - ic), m_counts[0], m_counts[1], m_counts[2], m_counts[3],
                   m_counts[4], m_counts[5]);
    }
    if (ndstubs) {
        printf("menu: %d functions auto-stubbed:\n", ndstubs);
        for (i = 0; i < ndstubs; i++)
            printf("  %-28s %s%s\n", fname(dstubs[i].addr), dstubs[i].addr >= LIB_START ? "[SDK] " : "[game] ",
                   dstubs[i].why);
    }
    if (cov_path && sh4_cov) {
        FILE *cf;
        uint32_t a, start = 0;
        int in = 0;
        make_dirs(cov_path);
        cf = fopen(cov_path, "w");
        for (a = 0x8C010000u; a <= 0x8C9E4C30u && cf; a += 2) {
            int on = (sh4_cov[(a & 0x00FFFFFFu) >> 4] >> ((a >> 1) & 7)) & 1;
            if (on && !in) {
                start = a;
                in = 1;
            }
            if (!on && in) {
                fprintf(cf, "%08X %08X\n", start, a);
                in = 0;
            }
        }
        if (cf)
            fclose(cf);
        printf("menu: coverage from %s -> %s\n", cov_from < 0 ? "the ADVERTISE load" : "the frame", cov_path);
    }
    snprintf(head, sizeof head, "# charselect menu log: frames %d unlock %d trial %d submode %d script %s%s\n", frames,
             unlock, trial, submode, input ? input : "-", route ? " ROUTED" : "");
    if (logpath) {
        if (menu_write(logpath, "log", mlog_buf, mlog_len, head))
            return 1;
        printf("menu: %s (menu log)\n", logpath);
    }
    if (log2d) {
        FILE *fd;
        make_dirs(log2d);
        fd = fopen(log2d, "wb");
        if (!fd) {
            fprintf(stderr, "menu: cannot write %s\n", log2d);
            return 1;
        }
        fprintf(fd, "# sa1 draw2d log v1: menu ADVERTISE submode %d%s, script %s, %d frames, %u draws%s\n", submode,
                submode == 1 && !full_seq ? " (sequence ended after its first entry)" : "", input_name, frames,
                d2d_count, route ? ", ROUTED" : "");
        d2d_write_columns(fd);
        if (d2d_len)
            fwrite(d2d_buf, 1, d2d_len, fd);
        fclose(fd);
        printf("menu: %u 2D draws -> %s\n", d2d_count, log2d);
    }
    printf("menu: files read through the GD-ROM HLE:\n");
    for (i = 0; i < ngdlogs; i++)
        printf("  %-20s %ld\n", gdlogs[i].name, gdlogs[i].size);
    return failed ? 1 : 0;
}
