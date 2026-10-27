/*
 * ho_oracle.c - the round-5 host-object oracle against the original game (gsa1_rounds_plan.md 9.4,
 * docs/libsa1_interaction.md 4.3 and its addendum 6.7): `sa1h trace --hostobj FILE`.
 *
 * libsa1's own host-object layer runs on the original game code: LIB src/sa1_hostobj.c and src/world/
 * sa1_world_hostobj.c are compiled into the harness unchanged (32-bit Windows builds; ho_lib_glue.c gives them their
 * world and context). Their proxies are built exactly as in a libsa1 world: CreateElementalTask(2, 3, 0x7F000100) (a
 * HOLDABLE: (3, 3, ...)), the enemy work by the game's 8C047040 (a HOLDABLE: 8C0586A0), the OBJ_CONDITION stand-in as
 * tp->ocp and the CCL_INFO template in segment 0x18 (host memory mapped at its guest address 0x98000000, so the game
 * code reads them where libsa1's game code reads them), CCL_Init; every game function the layer calls (CreateElementalTask,
 * FreeTask, CCL_Init / CCL_Entry, the damage check 8C0489E4, the bounce, the spring request, E-102's marker release and
 * time bonus, the characters' drop functions, atan2f ...) runs in the interpreter through the typed wrappers below
 * (sh4_invoke; natively when the trace routes it). The layer's hooks run where libsa1's step runs them
 * (sa1_world_tick.c sa1g_step_game): `pre` at the entry of the task executor 8C0988A4 (after the pad update of
 * 8C04E714), `post` right after the CCL pass 8C0342E0. The proxies' exec 0x7F000100 is a special hook
 * (sh4_special_hook_add): an interpreted JSR runs it, and a native executor's call through the task's exec pointer
 * (routed builds, the DEP path) jumps to the layer's native exec.
 *
 * The script (FILE; '#' starts a comment; one call per line, made before logic step STEP; positions and lengths in
 * SA1 units, converted to the host space of libsa1's default coordinates, the Source preset: host = 4 * (x, -z, y)):
 *   STEP add H KIND [pos=X,Y,Z] [yaw=R] [flags=F|F..] [armor=N] [touch=N] [cooldown=N] [launch=V] [reach=R] shape=S..
 *        H: the object's handle 1..63; KIND: ENEMY TARGET BREAKABLE HAZARD HOLDABLE BALLOON; F: SOLID NOT_SOLID
 *        NO_HOMING NO_LOCKON NO_BONUS DISABLED REPORT_TOUCH HOLD_HITS HOLD_LOCKED HEAVY POP_ON_TOUCH LAUNCH_BOUNCE
 *        KEEP_HSPEED; launch: SA1 units per step upward; reach: SA1 units;
 *        S: sphere:R | cyl:R,HALFH | box:HX,HY,HZ, then [@CX,CY,CZ] (offset) [^YAW] (box) [!NO_DAMAGE|NO_TOUCH|OFF]
 *   STEP update H ... (as add)      STEP move H X,Y,Z [YAW]      STEP remove H KILLED|SILENT      STEP release H
 *   STEP damage [face=X,Y,Z] [kill] [obj=H]                     STEP option NO_DEATH|KEEP_RINGS|NO_DAMAGE 0|1
 *   STEP rings N                     STEP mask 0xBITS           STEP config [window=N] [enter=R] [leave=R]
 * (at most SA1_TOBJ_MAX_OPS calls per step). Every call, every event, the held state and the proxies of each step go
 * to the NAME.obj.bin sidecar (sa1_trace_ext.h), which LIB/tests/world_replay.c feeds to a libsa1 world and compares.
 */
#include "harness.h"
#include "sa1/guest.h"
#include "ho_glue.h"
#include "ho_oracle.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOO_EXEC      0x7F000100u   /* the proxies' exec (libsa1's LIBSA1_HOST image) */
#define HOO_EXECUTOR  0x8C0988A4u   /* the task executor (list 7 as level 8, then levels 0..6) */
#define HOO_CCL       0x8C0342E0u   /* the CCL pass */
#define HOO_STEP_IDX  0x8C754EC8u   /* B_8C754EC8: the sub step 0 / 1 */
#define HOO_EXT       0x98000000u   /* libsa1's extension segment 0x18 */
#define HOO_EXT_SIZE  0x00100000u
#define HOO_MAX_H     64u           /* handles 1..63 */

typedef struct hoo_sop {
    int step;
    sa1_trace_obj_op o;
} hoo_sop;

static hoo_sop *g_ops;
static int g_nops, g_capops;
static int g_loaded, g_ready, g_in_step;
static sa1_trace_obj_header g_hdr;
static sa1_trace_obj_rec g_cur;
static sa1_trace_obj_rec *g_recs;
static sa1_trace_obj_rec_ext *g_recx;   /* round 0.9.2: the version-2 part of each record (g_recs' index) */
static int g_nrecs, g_caprecs;
static uint32_t g_id[HOO_MAX_H];        /* handle -> libsa1 id (0: not registered) */
static uint32_t g_lastid[HOO_MAX_H];    /* the last id a handle had (events after a removal) */
static uint32_t g_kind[HOO_MAX_H];
static uint32_t g_task_tp[SA1_TOBJ_MAX_TASKS];    /* the --create-task tasks */
static sa1_trace_obj_task_ext g_task_ext[SA1_TOBJ_MAX_TASKS];   /* round 0.9.2: their /SX,SY,SZ and +ocp (version 2) */
static int g_v2;                                /* some task has a version-2 field: the sidecar is version 2 */
static uint8_t g_task_seen[SA1_TOBJ_MAX_TASKS], g_task_gone[SA1_TOBJ_MAX_TASKS];  /* reported live / ended */
static unsigned long g_nev[16], g_nwev[3], g_nlost;

void hoo_fatal(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "hostobj oracle: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(3);
}

/* ---- game functions: typed cdecl wrappers that run them in the interpreter -------------------------------------- */
#if defined(SA1H_HOSTOBJ)
static uint32_t hoo_invoke(uint32_t dc, const sh4_arg *a, int n, float *f0)
{
    uint32_t r0 = 0;
    float fr = 0.0f;
    int rc = sh4_invoke(dc, a, n, &r0, &fr);   /* inside a step a fault unwinds to the step's guard */
    if (rc != SH4_OK)
        hoo_fatal("game function %08X faulted (%s) under the host-object layer", dc, sh4_fault_name(rc));
    if (f0)
        *f0 = fr;
    return r0;
}

#define HOO_P(x) sh4_ai((uint32_t)(uintptr_t)(x))

static void *w_create_task(uint16_t im, int32_t level, sa1_gfn exec)
{
    sh4_arg a[3];
    a[0] = sh4_ai(im);
    a[1] = sh4_ai((uint32_t)level);
    a[2] = sh4_ai(exec.v);
    return (void *)(uintptr_t)hoo_invoke(0x8C09852Eu, a, 3, NULL);
}

#define HOO_V1(name, dc)                                \
    static void name(void *p)                           \
    {                                                   \
        sh4_arg a = HOO_P(p);                           \
        hoo_invoke(dc, &a, 1, NULL);                    \
    }
HOO_V1(w_freetask, 0x8C098786u)         /* FreeTask */
HOO_V1(w_ccl_entry, 0x8C02F63Cu)        /* CCL_Entry(twp) */
HOO_V1(w_ccl_range, 0x8C02F400u)        /* colliwk.range */
HOO_V1(w_marker_rel, 0x8C0D5D90u)       /* E-102: release a target's marker */
HOO_V1(w_time_bonus, 0x8C0D5064u)       /* E-102: the time bonus (group task or NULL) */

#define HOO_DROP3(name, dc)                             \
    static void name(void *twp, void *mwp, void *pwp)   \
    {                                                   \
        sh4_arg a[3];                                   \
        a[0] = HOO_P(twp);                              \
        a[1] = HOO_P(mwp);                              \
        a[2] = HOO_P(pwp);                              \
        hoo_invoke(dc, a, 3, NULL);                     \
    }
HOO_DROP3(w_drop_sonic, 0x8C0B1D60u)
HOO_DROP3(w_drop_knuckles, 0x8C0C6DE0u)
HOO_DROP3(w_drop_amy, 0x8C0DC75Eu)
HOO_DROP3(w_drop_e102, 0x8C0D15C0u)
HOO_DROP3(w_drop_big, 0x8C0E55E2u)

static void w_drop_tails(void *twp, void *pwp)
{
    sh4_arg a[2];
    a[0] = HOO_P(twp);
    a[1] = HOO_P(pwp);
    hoo_invoke(0x8C0BD440u, a, 2, NULL);
}

static int32_t w_movable_init(void *twp, void *mwp, int32_t cls)
{
    sh4_arg a[3];
    a[0] = HOO_P(twp);
    a[1] = HOO_P(mwp);
    a[2] = sh4_ai((uint32_t)cls);
    return (int32_t)hoo_invoke(0x8C0586A0u, a, 3, NULL);
}

static void *w_enemy_init(void *tp, void *twp)
{
    sh4_arg a[2];
    a[0] = HOO_P(tp);
    a[1] = HOO_P(twp);
    return (void *)(uintptr_t)hoo_invoke(0x8C047040u, a, 2, NULL);
}

static void w_ccl_init(void *tp, void *info, uint8_t id, int32_t one, uint8_t n)
{
    sh4_arg a[5];
    a[0] = HOO_P(tp);
    a[1] = HOO_P(info);
    a[2] = sh4_ai(id);
    a[3] = sh4_ai((uint32_t)one);
    a[4] = sh4_ai(n);
    hoo_invoke(0x8C02F4B0u, a, 5, NULL);
}

static int32_t w_damage(void *twp, void *ewp)
{
    sh4_arg a[2];
    a[0] = HOO_P(twp);
    a[1] = HOO_P(ewp);
    return (int32_t)hoo_invoke(0x8C0489E4u, a, 2, NULL);
}

static void w_bounce(int8_t pno)
{
    sh4_arg a = sh4_ai((uint32_t)(int32_t)pno);
    hoo_invoke(0x8C048980u, &a, 1, NULL);
}

static void w_score(int32_t n)
{
    sh4_arg a = sh4_ai((uint32_t)n);
    hoo_invoke(0x8C08A2A2u, &a, 1, NULL);
}

static void w_rings_add(int16_t n)
{
    sh4_arg a = sh4_ai((uint32_t)(int32_t)n);
    hoo_invoke(0x8C08A198u, &a, 1, NULL);
}

static void w_death(uint8_t pno)
{
    sh4_arg a = sh4_ai(pno);
    hoo_invoke(0x8C07BBC2u, &a, 1, NULL);
}

#define HOO_VEL(name, dc)                                   \
    static void name(uint8_t pno, float x, float y, float z) \
    {                                                       \
        sh4_arg a[4];                                       \
        a[0] = sh4_ai(pno);                                 \
        a[1] = sh4_af(x);                                   \
        a[2] = sh4_af(y);                                   \
        a[3] = sh4_af(z);                                   \
        hoo_invoke(dc, a, 4, NULL);                         \
    }
HOO_VEL(w_setvel, 0x8C07C676u)          /* SetVelocityP */
HOO_VEL(w_spring, 0x8C07C720u)          /* SetInputP(pno, 3) + SetVelocityP: the ground spring's request */

static void w_rumble(int32_t a0, int32_t a1, int32_t a2, int32_t a3)
{
    sh4_arg a[4];
    a[0] = sh4_ai((uint32_t)a0);
    a[1] = sh4_ai((uint32_t)a1);
    a[2] = sh4_ai((uint32_t)a2);
    a[3] = sh4_ai((uint32_t)a3);
    hoo_invoke(0x8C109EACu, a, 4, NULL);
}

static float w_atan2(float y, float x)
{
    sh4_arg a[2];
    float r = 0.0f;
    a[0] = sh4_af(y);
    a[1] = sh4_af(x);
    hoo_invoke(0x8C10D0F8u, a, 2, &r);
    return r;
}

void *hoo_game_fn(uint32_t dc)
{
    switch (dc) {
    case 0x8C09852Eu: return (void *)w_create_task;
    case 0x8C098786u: return (void *)w_freetask;
    case 0x8C02F63Cu: return (void *)w_ccl_entry;
    case 0x8C02F400u: return (void *)w_ccl_range;
    case 0x8C0D5D90u: return (void *)w_marker_rel;
    case 0x8C0D5064u: return (void *)w_time_bonus;
    case 0x8C0B1D60u: return (void *)w_drop_sonic;
    case 0x8C0BD440u: return (void *)w_drop_tails;
    case 0x8C0C6DE0u: return (void *)w_drop_knuckles;
    case 0x8C0DC75Eu: return (void *)w_drop_amy;
    case 0x8C0D15C0u: return (void *)w_drop_e102;
    case 0x8C0E55E2u: return (void *)w_drop_big;
    case 0x8C0586A0u: return (void *)w_movable_init;
    case 0x8C047040u: return (void *)w_enemy_init;
    case 0x8C02F4B0u: return (void *)w_ccl_init;
    case 0x8C0489E4u: return (void *)w_damage;
    case 0x8C048980u: return (void *)w_bounce;
    case 0x8C08A2A2u: return (void *)w_score;
    case 0x8C08A198u: return (void *)w_rings_add;
    case 0x8C07BBC2u: return (void *)w_death;
    case 0x8C07C676u: return (void *)w_setvel;
    case 0x8C07C720u: return (void *)w_spring;
    case 0x8C109EACu: return (void *)w_rumble;
    case 0x8C10D0F8u: return (void *)w_atan2;
    default: return NULL;
    }
}
#else
void *hoo_game_fn(uint32_t dc)
{
    (void)dc;
    return NULL;
}
#endif

/* ---- the script ------------------------------------------------------------------------------------------------ */
static const struct { const char *name; uint32_t v; } k_kinds[] = {
    { "ENEMY", SA1_HOSTOBJ_ENEMY }, { "TARGET", SA1_HOSTOBJ_TARGET }, { "BREAKABLE", SA1_HOSTOBJ_BREAKABLE },
    { "HAZARD", SA1_HOSTOBJ_HAZARD }, { "HOLDABLE", SA1_HOSTOBJ_HOLDABLE }, { "BALLOON", SA1_HOSTOBJ_BALLOON },
    { NULL, 0 } };
static const struct { const char *name; uint32_t v; } k_flags[] = {
    { "SOLID", SA1_HOSTOBJ_SOLID }, { "NOT_SOLID", SA1_HOSTOBJ_NOT_SOLID }, { "NO_HOMING", SA1_HOSTOBJ_NO_HOMING },
    { "NO_LOCKON", SA1_HOSTOBJ_NO_LOCKON }, { "NO_BONUS", SA1_HOSTOBJ_NO_BONUS }, { "DISABLED", SA1_HOSTOBJ_DISABLED },
    { "REPORT_TOUCH", SA1_HOSTOBJ_REPORT_TOUCH }, { "HOLD_HITS", SA1_HOSTOBJ_HOLD_HITS },
    { "HOLD_LOCKED", SA1_HOSTOBJ_HOLD_LOCKED }, { "HEAVY", SA1_HOSTOBJ_HEAVY }, { "POP_ON_TOUCH", SA1_HOSTOBJ_POP_ON_TOUCH },
    { "LAUNCH_BOUNCE", SA1_HOSTOBJ_LAUNCH_BOUNCE }, { "KEEP_HSPEED", SA1_HOSTOBJ_KEEP_HSPEED }, { NULL, 0 } };
static const struct { const char *name; uint32_t v; } k_sflags[] = {
    { "NO_DAMAGE", SA1_HSHAPE_NO_DAMAGE }, { "NO_TOUCH", SA1_HSHAPE_NO_TOUCH }, { "OFF", SA1_HSHAPE_OFF }, { NULL, 0 } };
static const struct { const char *name; uint32_t v; } k_opts[] = {
    { "NO_DEATH", SA1_COPT_NO_DEATH }, { "KEEP_RINGS", SA1_COPT_KEEP_RINGS }, { "NO_DAMAGE", SA1_COPT_NO_DAMAGE },
    { NULL, 0 } };

static int name_val(const void *tab_, const char *s, size_t n, uint32_t *v)
{
    const struct { const char *name; uint32_t v; } *tab = tab_;
    int i;
    for (i = 0; tab[i].name; i++)
        if (strlen(tab[i].name) == n && !strncmp(tab[i].name, s, n)) {
            *v = tab[i].v;
            return 0;
        }
    return -1;
}

/* "A|B|C" of a table, or a number */
static int parse_bits(const void *tab, const char *s, uint32_t *out)
{
    uint32_t v = 0;
    if (isdigit((unsigned char)s[0])) {
        *out = (uint32_t)strtoul(s, NULL, 0);
        return 0;
    }
    while (*s) {
        size_t n = strcspn(s, "|+");
        uint32_t b;
        if (name_val(tab, s, n, &b))
            return -1;
        v |= b;
        s += n;
        if (*s)
            s++;
    }
    *out = v;
    return 0;
}

/* n comma-separated floats */
static int parse_floats(const char *s, float *v, int n, const char **end)
{
    int i;
    for (i = 0; i < n; i++) {
        char *e;
        v[i] = strtof(s, &e);
        if (e == s)
            return -1;
        s = e;
        if (i + 1 < n) {
            if (*s != ',')
                return -1;
            s++;
        }
    }
    if (end)
        *end = s;
    return 0;
}

/* SA1 space -> the host space of libsa1's default coordinates (the Source preset): 4 * (x, -z, y) */
static void to_host(const float s[3], float h[3])
{
    h[0] = 4.0f * s[0];
    h[1] = -4.0f * s[2];
    h[2] = 4.0f * s[1];
}

/* half extents along the SA1 axes -> along the host axes */
static void ext_to_host(const float s[3], float h[3])
{
    h[0] = 4.0f * s[0];
    h[1] = 4.0f * s[2];
    h[2] = 4.0f * s[1];
}

static int parse_shape(const char *s, sa1_trace_obj_shape *sh)
{
    float v[3] = { 0.0f, 0.0f, 0.0f }, c[3] = { 0.0f, 0.0f, 0.0f };
    const char *e = NULL;
    memset(sh, 0, sizeof *sh);
    if (!strncmp(s, "sphere:", 7)) {
        sh->form = SA1_SHAPE_SPHERE;
        if (parse_floats(s + 7, v, 1, &e))
            return -1;
        sh->size[0] = 4.0f * v[0];
    } else if (!strncmp(s, "cyl:", 4)) {
        sh->form = SA1_SHAPE_CYLINDER;
        if (parse_floats(s + 4, v, 2, &e))
            return -1;
        sh->size[0] = 4.0f * v[0];
        sh->size[1] = 4.0f * v[1];
    } else if (!strncmp(s, "box:", 4)) {
        sh->form = SA1_SHAPE_BOX;
        if (parse_floats(s + 4, v, 3, &e))
            return -1;
        ext_to_host(v, sh->size);
    } else {
        return -1;
    }
    while (*e) {
        if (*e == '@') {
            if (parse_floats(e + 1, c, 3, &e))
                return -1;
            to_host(c, sh->center);
        } else if (*e == '^') {
            char *x;
            sh->yaw = strtof(e + 1, &x);
            e = x;
        } else if (*e == '!') {
            size_t n = strcspn(e + 1, "@^");
            char buf[64];
            snprintf(buf, sizeof buf, "%.*s", (int)n, e + 1);
            if (parse_bits(k_sflags, buf, &sh->flags))
                return -1;
            e += 1 + n;
        } else {
            return -1;
        }
    }
    return 0;
}

static int add_op(int step, const sa1_trace_obj_op *o)
{
    if (g_nops == g_capops) {
        g_capops = g_capops ? g_capops * 2 : 64;
        g_ops = (hoo_sop *)realloc(g_ops, sizeof *g_ops * (size_t)g_capops);
    }
    g_ops[g_nops].step = step;
    g_ops[g_nops].o = *o;
    g_nops++;
    return 0;
}

static int parse_handle(const char *s, uint32_t *h)
{
    char *e;
    unsigned long v = strtoul(s, &e, 10);
    if (e == s || *e || v == 0 || v >= HOO_MAX_H)
        return -1;
    *h = (uint32_t)v;
    return 0;
}

/* one script line, tokens tok[0..n) (tok[0] = the step) */
static int parse_line(char **tok, int n, int lineno)
{
    sa1_trace_obj_op o;
    int step, i;
    char *e;
    memset(&o, 0, sizeof o);
    step = (int)strtol(tok[0], &e, 10);
    if (e == tok[0] || *e || step < 0 || n < 2)
        goto bad;
    if (!strcmp(tok[1], "add") || !strcmp(tok[1], "update")) {
        o.op = !strcmp(tok[1], "add") ? SA1_TOBJ_OP_ADD : SA1_TOBJ_OP_UPDATE;
        if (n < 4 || parse_handle(tok[2], &o.handle) || name_val(k_kinds, tok[3], strlen(tok[3]), &o.kind))
            goto bad;
        o.armor = -1;
        o.touch = -1;
        for (i = 4; i < n; i++) {
            const char *t = tok[i];
            if (!strncmp(t, "pos=", 4)) {
                float p[3];
                if (parse_floats(t + 4, p, 3, NULL))
                    goto bad;
                to_host(p, o.pos);
            } else if (!strncmp(t, "yaw=", 4)) {
                o.yaw = strtof(t + 4, NULL);
            } else if (!strncmp(t, "flags=", 6)) {
                if (parse_bits(k_flags, t + 6, &o.flags))
                    goto bad;
            } else if (!strncmp(t, "armor=", 6)) {
                o.armor = atoi(t + 6);
            } else if (!strncmp(t, "touch=", 6)) {
                o.touch = atoi(t + 6);
            } else if (!strncmp(t, "cooldown=", 9)) {
                o.cooldown = (uint32_t)atoi(t + 9);
            } else if (!strncmp(t, "launch=", 7)) {
                o.launch_speed = strtof(t + 7, NULL) * 4.0f * 60.0f;    /* SA1 units per step -> host units/s */
            } else if (!strncmp(t, "reach=", 6)) {
                o.grab_reach = strtof(t + 6, NULL) * 4.0f;
            } else if (!strncmp(t, "shape=", 6)) {
                if (o.nshapes >= 4 || parse_shape(t + 6, &o.shape[o.nshapes]))
                    goto bad;
                o.nshapes++;
            } else {
                goto bad;
            }
        }
        if (!o.nshapes)
            goto bad;
    } else if (!strcmp(tok[1], "move")) {
        float p[3];
        o.op = SA1_TOBJ_OP_MOVE;
        if (n < 4 || parse_handle(tok[2], &o.handle) || parse_floats(tok[3], p, 3, NULL))
            goto bad;
        to_host(p, o.pos);
        if (n > 4)
            o.yaw = strtof(tok[4], NULL);
    } else if (!strcmp(tok[1], "remove")) {
        o.op = SA1_TOBJ_OP_REMOVE;
        if (n < 4 || parse_handle(tok[2], &o.handle))
            goto bad;
        if (!strcmp(tok[3], "KILLED"))
            o.arg[0] = (int32_t)SA1_HOSTOBJ_REMOVE_KILLED;
        else if (!strcmp(tok[3], "SILENT"))
            o.arg[0] = (int32_t)SA1_HOSTOBJ_REMOVE_SILENT;
        else
            goto bad;
    } else if (!strcmp(tok[1], "release")) {
        o.op = SA1_TOBJ_OP_RELEASE;
        if (n < 3 || parse_handle(tok[2], &o.handle))
            goto bad;
    } else if (!strcmp(tok[1], "damage")) {
        o.op = SA1_TOBJ_OP_DAMAGE;
        for (i = 2; i < n; i++) {
            if (!strncmp(tok[i], "face=", 5)) {
                float p[3];
                if (parse_floats(tok[i] + 5, p, 3, NULL))
                    goto bad;
                to_host(p, o.src);
                o.arg[0] |= (int32_t)SA1_DAMAGE_FACE_SOURCE;
            } else if (!strcmp(tok[i], "kill")) {
                o.arg[0] |= (int32_t)SA1_DAMAGE_KILL;
            } else if (!strncmp(tok[i], "obj=", 4)) {
                uint32_t h;
                if (parse_handle(tok[i] + 4, &h))
                    goto bad;
                o.arg[1] = (int32_t)h;
            } else {
                goto bad;
            }
        }
    } else if (!strcmp(tok[1], "option")) {
        uint32_t v;
        o.op = SA1_TOBJ_OP_OPTION;
        if (n < 4 || name_val(k_opts, tok[2], strlen(tok[2]), &v))
            goto bad;
        o.arg[0] = (int32_t)v;
        o.arg[1] = atoi(tok[3]);
    } else if (!strcmp(tok[1], "rings")) {
        o.op = SA1_TOBJ_OP_RINGS;
        if (n < 3)
            goto bad;
        o.arg[0] = atoi(tok[2]);
    } else if (!strcmp(tok[1], "mask")) {
        o.op = SA1_TOBJ_OP_MASK;
        if (n < 3)
            goto bad;
        o.arg[0] = (int32_t)strtoul(tok[2], NULL, 0);
    } else if (!strcmp(tok[1], "config")) {
        o.op = SA1_TOBJ_OP_CONFIG;
        for (i = 2; i < n; i++) {
            if (!strncmp(tok[i], "window=", 7))
                o.arg[0] = atoi(tok[i] + 7);
            else if (!strncmp(tok[i], "enter=", 6))
                o.src[0] = strtof(tok[i] + 6, NULL) * 4.0f;
            else if (!strncmp(tok[i], "leave=", 6))
                o.src[1] = strtof(tok[i] + 6, NULL) * 4.0f;
            else
                goto bad;
        }
    } else {
        goto bad;
    }
    return add_op(step, &o);
bad:
    fprintf(stderr, "--hostobj: line %d: cannot read \"%s%s%s ...\" (see harness/src/ho_oracle.c)\n", lineno, tok[0],
            n > 1 ? " " : "", n > 1 ? tok[1] : "");
    return -1;
}

int ho_oracle_load(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[2048], full[1400];
    int lineno = 0, rc = 0;
    const char *base = path;
    if (!f) {
        snprintf(full, sizeof full, "%s/%s", g_root, path);
        f = fopen(full, "r");
    }
    if (!f) {
        fprintf(stderr, "--hostobj: cannot read %s\n", path);
        return -1;
    }
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    snprintf(g_hdr.script, sizeof g_hdr.script, "%s", base);
    while (fgets(line, sizeof line, f)) {
        char *tok[48], *s = line, *h = strchr(line, '#');
        int n = 0;
        lineno++;
        if (h)
            *h = 0;
        while (n < 48) {
            while (*s && isspace((unsigned char)*s))
                s++;
            if (!*s)
                break;
            tok[n++] = s;
            while (*s && !isspace((unsigned char)*s))
                s++;
            if (*s)
                *s++ = 0;
        }
        if (n && parse_line(tok, n, lineno))
            rc = -1;
    }
    fclose(f);
    if (rc)
        return -1;
    /* the calls of one step, in script order; at most SA1_TOBJ_MAX_OPS each */
    for (int i = 0; i < g_nops; i++) {
        int k = 0;
        for (int j = 0; j < g_nops; j++)
            k += g_ops[j].step == g_ops[i].step;
        if (k > (int)SA1_TOBJ_MAX_OPS) {
            fprintf(stderr, "--hostobj: step %d has %d calls (at most %u)\n", g_ops[i].step, k,
                    (unsigned)SA1_TOBJ_MAX_OPS);
            return -1;
        }
    }
    g_loaded = 1;
    g_hdr.x.flags |= SA1_TOBJ_H_HOSTOBJ;
    printf("sim: --hostobj %s: %d calls\n", path, g_nops);
    return 0;
}

void ho_oracle_add_task_v2(const sa1_trace_obj_task *t, const sa1_trace_obj_task_ext *x, uint32_t tp)
{
    if (g_hdr.ntasks < SA1_TOBJ_MAX_TASKS) {
        if (x != NULL && x->flags != 0) {
            g_task_ext[g_hdr.ntasks] = *x;
            g_v2 = 1;
        }
        g_task_tp[g_hdr.ntasks] = tp;
        g_hdr.task[g_hdr.ntasks++] = *t;
    }
}

void ho_oracle_add_task(const sa1_trace_obj_task *t, uint32_t tp)
{
    ho_oracle_add_task_v2(t, NULL, tp);
}

/* the taskwk of a live task (its taskwk pointer unchanged and its exec not DestroyTask) into a proxy-state entry */
static void task_state(sa1_trace_obj_st *s, uint32_t tp, uint32_t handle)
{
    uint32_t twp, cwp, k;
    s->handle = handle;
    if (!sh4_is_ram(tp))
        return;
    twp = sh4_read32(tp + 0x20);
    if (!sh4_is_ram(twp) || (sh4_read32(tp + 0x10) & 0x1FFFFFFFu) == 0x0C09865Cu)
        return;
    s->twp = twp;
    s->mode = (uint16_t)sh4_read8(twp);
    s->tflag = sh4_read16(twp + 4);
    s->angy = sh4_read32(twp + 0x18) & 0xFFFFu;
    for (k = 0; k < 3; k++)
        s->pos[k] = sh4_readf(twp + 0x20 + 4 * k);
    cwp = sh4_read32(twp + 0x38);
    if (sh4_is_ram(cwp))
        s->cflag = sh4_read16(cwp + 2);
}

int ho_oracle_active(void)
{
    return g_loaded || g_hdr.ntasks != 0;
}

/* ---- the hooks ------------------------------------------------------------------------------------------------- */
static sh4_hook_fn ex_prev, ccl_prev;
static void *ex_prev_user, *ccl_prev_user;
static int prev_saved;

static void hoo_ex_hook(sh4_cpu *c, void *u);
static void hoo_ccl_hook(sh4_cpu *c, void *u);

#if defined(SA1H_HOSTOBJ)
static void hoo_exec_hook(sh4_cpu *c, void *u)
{
    (void)u;
    hoo_lib_exec(c->r[4]);
}
#endif

static void hoo_hooks_install(void)
{
    sh4_hook_fn fn = NULL;
    void *user = NULL;
    if (!prev_saved) {
        if (sh4_hook_find(HOO_EXECUTOR, &fn, &user, NULL) && fn != hoo_ex_hook) {
            ex_prev = fn;
            ex_prev_user = user;
        }
        fn = NULL;
        if (sh4_hook_find(HOO_CCL, &fn, &user, NULL) && fn != hoo_ccl_hook) {
            ccl_prev = fn;
            ccl_prev_user = user;
        }
        prev_saved = 1;
    }
    if (!(sh4_hook_find(HOO_EXECUTOR, &fn, NULL, NULL) && fn == hoo_ex_hook))
        sh4_hook_add(HOO_EXECUTOR, hoo_ex_hook, NULL, "task executor + host-object pre (--hostobj)");
    if (!(sh4_hook_find(HOO_CCL, &fn, NULL, NULL) && fn == hoo_ccl_hook))
        sh4_hook_add(HOO_CCL, hoo_ccl_hook, NULL, "CCL pass + host-object post (--hostobj)");
}

static void run_prev(uint32_t addr, sh4_hook_fn prev, void *user, sh4_hook_fn self, const char *name, sh4_cpu *c)
{
    if (prev) {
        prev(c, user);                          /* routed: the native function through its adapter */
    } else {
        sh4_hook_remove(addr);                  /* the original (a fault unwinds past the re-add: step_begin re-adds) */
        sh4_invoke(addr, NULL, 0, NULL, NULL);
        sh4_hook_add(addr, self, NULL, name);
    }
}

static void hoo_ex_hook(sh4_cpu *c, void *u)
{
    (void)u;
    if (g_in_step)
        hoo_lib_pre((int32_t)sh4_read32(HOO_STEP_IDX));
    run_prev(HOO_EXECUTOR, ex_prev, ex_prev_user, hoo_ex_hook, "task executor + host-object pre (--hostobj)", c);
}

static void hoo_ccl_hook(sh4_cpu *c, void *u)
{
    (void)u;
    run_prev(HOO_CCL, ccl_prev, ccl_prev_user, hoo_ccl_hook, "CCL pass + host-object post (--hostobj)", c);
    if (g_in_step)
        hoo_lib_post((int32_t)sh4_read32(HOO_STEP_IDX));
}

int ho_oracle_setup(void)
{
    static const float axis[9] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
    memcpy(g_hdr.x.magic, SA1_TRACE_EXT_MAGIC_OBJ, 8);
    g_hdr.x.version = SA1_TRACE_EXT_OBJ_VERSION;
    g_hdr.x.header_size = sizeof g_hdr;
    g_hdr.x.record_size = sizeof(sa1_trace_obj_rec);
    g_hdr.coords_scale = 4.0f;
    memcpy(g_hdr.coords_axis, axis, sizeof axis);
    if (!g_loaded)
        return 0;
#if defined(SA1H_HOSTOBJ)
    {
        uint8_t *ext = sh4_map_extra(HOO_EXT, HOO_EXT_SIZE);
        if (!ext) {
            fprintf(stderr, "--hostobj: cannot map libsa1's extension segment at %08X\n", HOO_EXT);
            return -1;
        }
        memset(ext, 0, HOO_EXT_SIZE);
        if (hoo_lib_init(sh4_ram(), ext, HOO_EXT, HOO_EXT_SIZE)) {
            fprintf(stderr, "--hostobj: libsa1's host-object layer did not attach\n");
            return -1;
        }
        if (sh4_special_hook_add(HOO_EXEC, hoo_exec_hook, NULL, hoo_lib_exec_native(),
                                 "hostobj_exec (libsa1 LIBSA1_HOST)")) {
            fprintf(stderr, "--hostobj: no special hook for %08X\n", HOO_EXEC);
            return -1;
        }
        hoo_hooks_install();
        g_ready = 1;
        printf("sim: --hostobj: libsa1's host-object layer on the original game (proxies' exec %08X, segment 0x18 at "
               "%08X, executor %08X%s, CCL %08X%s)\n", HOO_EXEC, HOO_EXT, HOO_EXECUTOR, ex_prev ? " routed" : "",
               HOO_CCL, ccl_prev ? " routed" : "");
        return 0;
    }
#else
    fprintf(stderr, "--hostobj needs the 32-bit Windows build (libsa1's host-object layer is compiled into it)\n");
    return -1;
#endif
}

/* ---- one step ---------------------------------------------------------------------------------------------------- */
static void desc_of(const sa1_trace_obj_op *o, sa1_hostobj_desc *d)
{
    uint32_t i;
    memset(d, 0, sizeof *d);
    d->struct_size = sizeof *d;
    d->kind = o->kind;
    d->flags = o->flags;
    memcpy(d->position, o->pos, sizeof d->position);
    d->yaw = o->yaw;
    d->armor = o->armor;
    d->touch = o->touch;
    d->cooldown_steps = o->cooldown;
    d->user = o->handle;
    d->num_shapes = o->nshapes;
    for (i = 0; i < o->nshapes && i < SA1_HOSTOBJ_MAX_SHAPES; i++) {
        d->shapes[i].form = o->shape[i].form;
        d->shapes[i].flags = o->shape[i].flags;
        memcpy(d->shapes[i].center, o->shape[i].center, sizeof d->shapes[i].center);
        memcpy(d->shapes[i].size, o->shape[i].size, sizeof d->shapes[i].size);
        d->shapes[i].yaw = o->shape[i].yaw;
    }
    d->launch_speed = o->launch_speed;
    d->grab_reach = o->grab_reach;
}

static const char *op_name(uint32_t op)
{
    static const char *const n[] = { "?", "add", "update", "move", "remove", "release", "damage", "option", "rings",
                                     "mask", "config" };
    return op < sizeof n / sizeof n[0] ? n[op] : "?";
}

static void apply_op(int step, const sa1_trace_obj_op *o)
{
#if defined(SA1H_HOSTOBJ)
    sa1_context *ctx = hoo_lib_ctx();
    sa1_character_id ch = hoo_lib_chr();
    sa1_status st = SA1_OK;
    uint32_t h = o->handle < HOO_MAX_H ? o->handle : 0u;
    switch (o->op) {
    case SA1_TOBJ_OP_ADD:
    case SA1_TOBJ_OP_UPDATE: {
        sa1_hostobj_desc d;
        desc_of(o, &d);
        if (o->op == SA1_TOBJ_OP_ADD) {
            uint32_t id = 0;
            st = sa1_hostobj_add(ctx, &d, &id);
            g_id[h] = g_lastid[h] = id;
        } else {
            st = sa1_hostobj_update(ctx, g_id[h], &d);
        }
        g_kind[h] = o->kind;
        break;
    }
    case SA1_TOBJ_OP_MOVE:
        st = sa1_hostobj_move(ctx, g_id[h], o->pos, o->yaw);
        break;
    case SA1_TOBJ_OP_REMOVE:
        st = sa1_hostobj_remove(ctx, g_id[h], (uint32_t)o->arg[0]);
        g_id[h] = 0;
        break;
    case SA1_TOBJ_OP_RELEASE:
        st = sa1_hostobj_release(ctx, g_id[h]);
        break;
    case SA1_TOBJ_OP_DAMAGE: {
        sa1_damage d;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.flags = (uint32_t)o->arg[0];
        memcpy(d.source, o->src, sizeof d.source);
        d.object = (uint32_t)o->arg[1] < HOO_MAX_H ? g_lastid[o->arg[1]] : 0u;
        st = sa1_character_damage(ctx, ch, &d);
        break;
    }
    case SA1_TOBJ_OP_OPTION:
        st = sa1_character_set_option(ctx, ch, (uint32_t)o->arg[0], o->arg[1]);
        break;
    case SA1_TOBJ_OP_RINGS:
        st = hoo_lib_add_rings(o->arg[0]);
        break;
    case SA1_TOBJ_OP_MASK:
        st = sa1_hostobj_world_mask(ctx, ch, (uint32_t)o->arg[0]);
        break;
    case SA1_TOBJ_OP_CONFIG: {
        sa1_hostobj_config c;
        memset(&c, 0, sizeof c);
        c.struct_size = sizeof c;
        c.window = (uint32_t)o->arg[0];
        c.enter_radius = o->src[0];
        c.leave_radius = o->src[1];
        st = sa1_hostobj_configure(ctx, &c);
        break;
    }
    default:
        break;
    }
    printf("hostobj: step %d: %s %u -> %s (%d)\n", step, op_name(o->op), (unsigned)o->handle,
           st == SA1_OK ? "ok" : "FAILED", (int)st);
#else
    (void)step;
    (void)o;
#endif
}

void ho_oracle_step_begin(int step)
{
    int i;
    memset(&g_cur, 0, sizeof g_cur);
    g_cur.step = (uint32_t)step;
    if (!g_ready)
        return;
    hoo_hooks_install();
    for (i = 0; i < g_nops; i++)
        if (g_ops[i].step == step && g_cur.nops < SA1_TOBJ_MAX_OPS) {
            g_cur.op[g_cur.nops++] = g_ops[i].o;
            apply_op(step, &g_ops[i].o);
        }
    g_in_step = 1;
}

static uint32_t handle_of(uint32_t id)
{
    uint32_t h;
    if (id == 0)
        return 0;
    for (h = 1; h < HOO_MAX_H; h++)
        if (g_lastid[h] == id)
            return h;
    return 0;
}

static const char *ev_name(uint32_t t)
{
    static const char *const n[] = { "?", "HIT", "LOCKED", "TARGETED", "UNLOCKED", "TOUCH", "LIFTED", "RELEASED" };
    if (t == (SA1_TOBJ_WORLD | SA1_EVENT_HURT))
        return "HURT";
    if (t == (SA1_TOBJ_WORLD | SA1_EVENT_DEATH))
        return "DEATH";
    if (t == (SA1_TOBJ_WORLD | SA1_EVENT_CAPACITY))
        return "CAPACITY";
    return t < sizeof n / sizeof n[0] ? n[t] : "?";
}

static void push_ev(const sa1_trace_obj_ev *e)
{
    if (g_cur.nevents < SA1_TOBJ_MAX_EVENTS)
        g_cur.ev[g_cur.nevents++] = *e;
    else {
        g_cur.events_lost++;
        g_nlost++;
    }
}

void ho_oracle_step_end(int step, int nrec)
{
    g_in_step = 0;
#if defined(SA1H_HOSTOBJ)
    if (g_ready) {
        sa1_context *ctx = hoo_lib_ctx();
        sa1_character_id ch = hoo_lib_chr();
        sa1_hostobj_event ev[16];
        sa1_event we[16];
        sa1_hostobj_held held;
        sa1_hostobj_info info;
        uint32_t n, k, h, mx = sh4_host_mxcsr_get();
        hoo_lib_step_done();
        /* the conversions to host space in the host's default FP mode, as a libsa1 host polls them */
        sh4_host_mxcsr_set(0x1F80);
        do {
            n = 0;
            sa1_poll_hostobj_events(ctx, ev, 16, &n);
            for (k = 0; k < n; k++) {
                sa1_trace_obj_ev e;
                memset(&e, 0, sizeof e);
                e.type = ev[k].type;
                e.handle = handle_of(ev[k].object);
                e.attack = ev[k].attack;
                e.level = ev[k].level;
                e.damage = ev[k].damage;
                e.shape = ev[k].shape;
                e.flags = ev[k].flags;
                e.reason = ev[k].reason;
                e.target = handle_of(ev[k].target);
                memcpy(e.pos, ev[k].position, sizeof e.pos);
                memcpy(e.dir, ev[k].direction, sizeof e.dir);
                e.speed = ev[k].speed;
                push_ev(&e);
                if (e.type < 16)
                    g_nev[e.type]++;
            }
        } while (n == 16);
        do {
            n = hoo_lib_world_events(we, 16);
            for (k = 0; k < n; k++) {
                sa1_trace_obj_ev e;
                if (we[k].type != SA1_EVENT_HURT && we[k].type != SA1_EVENT_DEATH &&
                    !(we[k].type == SA1_EVENT_CAPACITY && we[k].id == SA1_CAPACITY_HOST_OBJECTS))
                    continue;
                memset(&e, 0, sizeof e);
                e.type = SA1_TOBJ_WORLD | we[k].type;
                e.handle = handle_of(we[k].id);
                e.arg = we[k].arg;
                e.bank = we[k].bank;
                memcpy(e.pos, we[k].position, sizeof e.pos);
                push_ev(&e);
                g_nwev[we[k].type == SA1_EVENT_HURT ? 0 : we[k].type == SA1_EVENT_DEATH ? 1 : 2]++;
            }
        } while (n == 16);
        memset(&held, 0, sizeof held);
        held.struct_size = sizeof held;
        sa1_character_held(ctx, ch, &held);
        g_cur.held_object = handle_of(held.object);
        g_cur.held_phase = held.phase;
        memcpy(g_cur.held_pos, held.position, sizeof g_cur.held_pos);
        memcpy(g_cur.held_hand, held.hand, sizeof g_cur.held_hand);
        g_cur.held_yaw = held.yaw;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        sa1_hostobj_info_get(ctx, ch, &info);
        g_cur.proxies = info.proxies;
        g_cur.created = info.created;
        g_cur.destroyed = info.destroyed;
        g_cur.refused = info.refused;
        sh4_host_mxcsr_set(mx);
        /* the proxies (guest memory, SA1 space) */
        for (h = 1; h < HOO_MAX_H && g_cur.nobjs < SA1_TOBJ_MAX_OBJS; h++) {
            sa1_trace_obj_st *s;
            uint32_t twp;
            if (!g_id[h])
                continue;
            s = &g_cur.obj[g_cur.nobjs++];
            s->handle = h;
            twp = hoo_lib_proxy_twp(g_id[h]);
            if (!twp || !sh4_is_ram(twp))
                continue;
            s->twp = twp;
            s->mode = (uint16_t)sh4_read8(twp);
            s->tflag = sh4_read16(twp + 4);
            s->angy = sh4_read32(twp + 0x18) & 0xFFFFu;
            for (k = 0; k < 3; k++)
                s->pos[k] = sh4_readf(twp + 0x20 + 4 * k);
            {
                uint32_t cwp = sh4_read32(twp + 0x38), tp = sh4_is_ram(cwp) ? sh4_read32(cwp + 0x10) : 0;
                if (sh4_is_ram(cwp))
                    s->cflag = sh4_read16(cwp + 2);
                if (g_kind[h] != SA1_HOSTOBJ_HOLDABLE && sh4_is_ram(tp) && sh4_is_ram(sh4_read32(tp + 0x24)))
                    s->eflag = sh4_read16(sh4_read32(tp + 0x24) + 0x4E);
            }
        }
        for (k = 0; k < g_cur.nevents; k++) {
            const sa1_trace_obj_ev *e = &g_cur.ev[k];
            if (e->type == (SA1_TOBJ_WORLD | SA1_EVENT_HURT))
                printf("hostobj: step %d: HURT obj %u rings lost %u flags %X (SA1_HURT_*) rings left %d\n", step,
                       (unsigned)e->handle, (unsigned)(e->arg & 0xFFFF), (unsigned)(e->arg >> 16), (int)(int16_t)e->bank);
            else if (e->type & SA1_TOBJ_WORLD)
                printf("hostobj: step %d: %s obj %u arg %u\n", step, ev_name(e->type), (unsigned)e->handle,
                       (unsigned)e->arg);
            else
                printf("hostobj: step %d: %s obj %u attack %u level %u damage %u shape %u flags %X reason %u target %u "
                       "speed %.3f\n", step, ev_name(e->type), (unsigned)e->handle, (unsigned)e->attack,
                       (unsigned)e->level, (unsigned)e->damage, (unsigned)e->shape, (unsigned)e->flags,
                       (unsigned)e->reason, (unsigned)e->target, (double)e->speed);
        }
    }
#endif
    /* the --create-task tasks (handle 0x80 + index), after the proxies. Round 0.9.2: a task that has ended (seen live,
       then freed or DestroyTask) stays reported as gone: its freed task block may be reused by another task later
       (the item box after step 44), whose state is not this task's */
    {
        uint32_t k;
        for (k = 0; k < g_hdr.ntasks && k < SA1_TOBJ_MAX_TASKS && g_cur.nobjs < SA1_TOBJ_MAX_OBJS; k++) {
            sa1_trace_obj_st *s = &g_cur.obj[g_cur.nobjs++];
            if (g_task_gone[k]) {
                s->handle = 0x80u + k;
                continue;
            }
            task_state(s, g_task_tp[k], 0x80u + k);
            if (s->twp != 0)
                g_task_seen[k] = 1;
            else if (g_task_seen[k])
                g_task_gone[k] = 1;
        }
    }
    if (!ho_oracle_active() || nrec <= 0)
        return;
    if (nrec > g_caprecs) {
        int old = g_caprecs;
        g_caprecs = g_caprecs ? g_caprecs * 2 : 1024;
        if (g_caprecs < nrec)
            g_caprecs = nrec;
        g_recs = (sa1_trace_obj_rec *)realloc(g_recs, sizeof *g_recs * (size_t)g_caprecs);
        memset(g_recs + old, 0, sizeof *g_recs * (size_t)(g_caprecs - old));
        g_recx = (sa1_trace_obj_rec_ext *)realloc(g_recx, sizeof *g_recx * (size_t)g_caprecs);
        memset(g_recx + old, 0, sizeof *g_recx * (size_t)(g_caprecs - old));
    }
    g_recs[nrec - 1] = g_cur;
    /* round 0.9.2 (version 2): the +ocp tasks' OBJ_CONDITION word 0 after the step */
    {
        sa1_trace_obj_rec_ext x;
        uint32_t k;
        memset(&x, 0, sizeof x);
        for (k = 0; k < g_hdr.ntasks && k < SA1_TOBJ_MAX_TASKS; k++)
            if ((g_task_ext[k].flags & SA1_TOBJ_T_OCP) && g_task_ext[k].ocp_slot < SA1_TOBJ_OCP_SLOTS)
                x.ocp_word[k] = sh4_read32(SA1_TOBJ_OCP_ARRAY + 16u * g_task_ext[k].ocp_slot);
        g_recx[nrec - 1] = x;
    }
    if (nrec > g_nrecs)
        g_nrecs = nrec;
}

/* ---- after the run ----------------------------------------------------------------------------------------------- */
void ho_oracle_summary(void)
{
    if (!g_loaded)
        return;
    printf("sim: --hostobj: events: HIT %lu, LOCKED %lu, TARGETED %lu, UNLOCKED %lu, TOUCH %lu, LIFTED %lu, RELEASED "
           "%lu; HURT %lu, DEATH %lu, CAPACITY %lu%s\n", g_nev[1], g_nev[2], g_nev[3], g_nev[4], g_nev[5], g_nev[6],
           g_nev[7], g_nwev[0], g_nwev[1], g_nwev[2], g_nlost ? " (some steps lost events: SA1_TOBJ_MAX_EVENTS)" : "");
}

int ho_oracle_write(const char *prefix, const sa1_trace_header *h, int nrecs)
{
    char path[1300];
    FILE *f;
    int i;
    if (!ho_oracle_active())
        return 0;
    if (!g_hdr.x.version)
        ho_oracle_setup();      /* the header only (tasks without --hostobj) */
    g_hdr.x.count = (uint32_t)nrecs;
    g_hdr.x.level = h->level;
    g_hdr.x.act = h->act;
    g_hdr.x.chr = h->chr;
    if (g_nlost)
        g_hdr.x.flags |= SA1_TOBJ_H_EVLOST;
    snprintf(path, sizeof path, "%s.obj.bin", prefix);
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    if (g_v2) {
        /* round 0.9.2: version 2 (sa1_trace_ext.h sa1_trace_obj_header_v2): the v1 header, then the tasks' fields */
        sa1_trace_obj_header_v2 h2;
        memset(&h2, 0, sizeof h2);
        h2.v1 = g_hdr;
        h2.v1.x.version = SA1_TRACE_EXT_OBJ_VERSION2;
        h2.v1.x.header_size = sizeof h2;
        h2.v1.x.record_size = sizeof(sa1_trace_obj_rec_v2);
        memcpy(h2.task_ext, g_task_ext, sizeof h2.task_ext);
        fwrite(&h2, sizeof h2, 1, f);
    } else {
        fwrite(&g_hdr, sizeof g_hdr, 1, f);    /* version 1, as before */
    }
    for (i = 0; i < nrecs; i++) {
        sa1_trace_obj_rec_v2 r;
        memset(&r, 0, sizeof r);
        if (i < g_nrecs) {
            r.v1 = g_recs[i];
            r.x = g_recx[i];
        } else {
            r.v1.step = (uint32_t)i;
        }
        fwrite(&r, g_v2 ? sizeof r : sizeof r.v1, 1, f);    /* version 2: + the OBJ_CONDITIONs (sa1_trace_obj_rec_ext) */
    }
    fclose(f);
    printf("trace: %s (host-object sidecar version %u, %d records of %u bytes, %u task(s))\n", path,
           g_v2 ? (unsigned)SA1_TRACE_EXT_OBJ_VERSION2 : (unsigned)g_hdr.x.version, nrecs,
           g_v2 ? (unsigned)sizeof(sa1_trace_obj_rec_v2) : (unsigned)sizeof(sa1_trace_obj_rec), (unsigned)g_hdr.ntasks);
    return 0;
}

/* the fields a check compares: everything but the proxies' guest addresses */
static void obj_canon(sa1_trace_obj_rec *r)
{
    int k;
    for (k = 0; k < (int)SA1_TOBJ_MAX_OBJS; k++)
        r->obj[k].twp = 0;
}

int ho_oracle_check(const char *ref_path, int nrecs)
{
    char path[1300];
    FILE *f;
    sa1_trace_obj_header x;
    sa1_trace_obj_header_v2 x2;
    sa1_trace_obj_rec_v2 r, mine;
    size_t L = strlen(ref_path), rsz;
    int i, n, bad = 0, shown = 0, v1, v2;
    if (!ho_oracle_active())
        return 0;
    if (L > 4 && !strcmp(ref_path + L - 4, ".bin"))
        L -= 4;
    snprintf(path, sizeof path, "%.*s.obj.bin", (int)L, ref_path);
    f = fopen(path, "rb");
    if (!f) {
        printf("check: no reference sidecar %s (host objects not compared)\n", path);
        return 0;
    }
    memset(&x2, 0, sizeof x2);
    v1 = v2 = 0;
    if (fread(&x, sizeof x, 1, f) == 1 && !memcmp(x.x.magic, SA1_TRACE_EXT_MAGIC_OBJ, 8)) {
        v1 = x.x.version == SA1_TRACE_EXT_OBJ_VERSION && x.x.header_size == sizeof x &&
             x.x.record_size == sizeof(sa1_trace_obj_rec);
        v2 = x.x.version == SA1_TRACE_EXT_OBJ_VERSION2 && x.x.header_size == sizeof x2 &&
             x.x.record_size == sizeof(sa1_trace_obj_rec_v2) &&
             fread((uint8_t *)&x2 + sizeof x, sizeof x2 - sizeof x, 1, f) == 1;   /* round 0.9.2: the tasks' fields */
    }
    rsz = v2 ? sizeof(sa1_trace_obj_rec_v2) : sizeof(sa1_trace_obj_rec);
    if (!v1 && !v2) {
        printf("check: %s is not a version-%d / %d host-object sidecar\n", path, SA1_TRACE_EXT_OBJ_VERSION,
               SA1_TRACE_EXT_OBJ_VERSION2);
        fclose(f);
        return 1;
    }
    if (x.ntasks != g_hdr.ntasks || memcmp(x.task, g_hdr.task, sizeof x.task) ||
        memcmp(x2.task_ext, g_task_ext, sizeof x2.task_ext) || v2 != g_v2) {
        printf("check: host-object sidecar: other --create-task tasks than the reference\n");
        bad = 1;
    }
    n = (int)x.x.count < nrecs ? (int)x.x.count : nrecs;
    for (i = 0; i < n; i++) {
        memset(&r, 0, sizeof r);
        memset(&mine, 0, sizeof mine);
        if (fread(&r, rsz, 1, f) != 1)      /* version 1: the v1 record (r.x stays 0) */
            break;
        if (i < g_nrecs) {
            mine.v1 = g_recs[i];
            if (v2)
                mine.x = g_recx[i];
        } else {
            mine = r;
        }
        obj_canon(&r.v1);
        obj_canon(&mine.v1);
        if (!memcmp(&r, &mine, sizeof r))
            continue;
        if (!bad)
            printf("check: host-object sidecar: first difference at step %d\n", i);
        bad = 1;
        if (shown++ < 8) {
            size_t k;
            for (k = 0; k < sizeof r; k++)
                if (((uint8_t *)&r)[k] != ((uint8_t *)&mine)[k]) {
                    printf("  step %d: record byte +0x%X: %02X (reference) vs %02X (now)\n", i, (unsigned)k,
                           ((uint8_t *)&r)[k], ((uint8_t *)&mine)[k]);
                    break;
                }
        }
    }
    fclose(f);
    if (!bad && (int)x.x.count != nrecs) {
        printf("check: %u host-object records in %s, %d now\n", x.x.count, path, nrecs);
        bad = 1;
    }
    if (!bad)
        printf("check: host-object sidecar identical to %s (%d steps; proxy addresses not compared)\n", path, nrecs);
    return bad;
}
