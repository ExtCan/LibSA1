/*
 * sa1_world_stage.c - the stage's own tasks (sadc docs/libsa1_integration.md 6.2, "stage tasks"; Phase 7).
 *
 * Besides the camera, the sky and the landtable manager, the game's stage start 8C04E660 creates two tasks per
 * level from tables of 1ST_READ, whose execs are stage-overlay code (STGnn at 0x0C900000):
 *
 *   the level's list-2 task   CreateElementalTask(2, 2, D_8C19E0E0[level])                     (8C04F5xx)
 *   the level task            B_8C752BD4 = CreateElementalTask(2, 0, D_8C19DE30[level])         (8C04F506)
 *
 * in that order, before LoadPlayerTask. The level task's first exec (step 0, list 0, before the player in list 1)
 * creates the stage's tasks: Emerald Coast's ocean (0C909F20: the water plane DynamicCOL 0x88000002 that follows
 * the player, so every land gather counts one more entry) and footprints (O_ASIATO 0C906082 -> 0C9061DA: draws
 * rand() for every print, after the player), Red Mountain's act setup (0C9122EE: the kill collision 0C913028,
 * DynamicCOL 0x1001, registered by the level task's children from step 1) ...
 *
 * A world booted with SA1G_BOOT_STAGE_TASKS does the same, as far as the stage's code is decompiled:
 *
 *   - an exec that the world's code map has (the lowered stage C of the overlay image the world holds, pclower
 *     images: Red Mountain's level task 0C911E0A) is created with the stage start's im / list and runs as it is;
 *     what it creates in turn without C is a boundary stub of that image (gen/policy/stubs.tsv: STG05__...);
 *   - an exec without C that has a stand-in here (k_standins) is created with its real DC address and the world's
 *     executor runs the stand-in for it (an executor override, sa1g_world_xhook): Emerald Coast's level task
 *     0C900000 (asm only) creates, in its first exec, the decompiled tasks of its act at the same point and in the
 *     same order as the original (sa1g_stage_ec_level), and nothing else; the Perfect Chaos level task 0C900000 of
 *     B_CHAOS7 (round 6a, sa1g_stage_pc_level: the sea plane, the Super Sonic trigger, the ring drain), which Super
 *     Sonic's worlds get without SA1_CREATE_STAGE_TASKS too (sa1_api_world.c, gsa1_rounds_plan.md 11.1);
 *   - any other exec: the task is not created (w->stage_note names it).
 *
 * The stage overlay must be in the world image with its code map active: DISC mode, or any mode with
 * SA1_CREATE_STAGE_TASKS (src/world/sa1_api_world.c composes the kind's image with STGnn then).
 */
#include "sa1_world.h"

#include <stdio.h>
#include <string.h>

#define SA1G_ADDR_LEVEL_TASK  0x8C752BD4u   /* task *B_8C752BD4: the level task (8C04F506) */
#define SA1G_ADDR_LEVEL_EXEC  0x8C19DE30u   /* D_8C19DE30[level]: exec of the level task (list 0) */
#define SA1G_ADDR_LEVEL_EXEC2 0x8C19E0E0u   /* D_8C19E0E0[level]: exec of the level's list-2 task */
#define SA1G_STAGE_LEVELS     43

static uint32_t st_rd32(sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (sa1g_world_rd32(w, g, &v) != 0) {
        sa1g_fatal(0, "stage tasks: guest word outside the arena");
    }
    return v;
}

static void st_wr32(sa1g_world *w, uint32_t g, uint32_t v)
{
    if (sa1g_world_wr32(w, g, v) != 0) {
        sa1g_fatal(0, "stage tasks: guest word outside the arena");
    }
}

static int st_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

uint32_t sa1g_create_task(uint16_t im, int32_t list, uint32_t exec)
{
    void *(*create)(uint16_t, int32_t, sa1_gfn) =
        (void *(*)(uint16_t, int32_t, sa1_gfn))sa1_fn(SA1_GFN_C(SA1G_FN_CREATE_TASK), 0u);
    void *tp = create(im, list, SA1_GFN_C(exec));
    return tp != NULL ? sa1_encode(tp) : 0u;
}

/* the running world's code map has a function at this DC code address (overlay images first). Round 6a: at an address of
 * the overlay slots (0x0C900000 and up) only the C of an overlay image the world holds counts: a base-map stub at that
 * address is another overlay's function (SBOARD's 0C900080 shares its address with B_CHAOS7's list-2 task: Perfect
 * Chaos' D_8C19E0E0[19], which the Super free-roam contract leaves out, gsa1_rounds_plan.md 11.1) */
static int st_has_code(uint32_t exec)
{
    if ((exec & 0x1FFFFFFFu) >= 0x0C900000u && (exec & 0x1FFFFFFFu) < 0x0D000000u) {
        return sa1g_fn_image_lookup(exec) != NULL;
    }
    return sa1_fn_name(exec) != NULL;
}

/* ---- Emerald Coast (STG01; decompiled: sadc src/overlays/stg01) -------------------------------------- */
#define EC_LEVEL_TASK   0x0C900000u     /* the level task (asm only) */
#define EC_OCEAN_INIT   0x0C909F20u     /* the ocean task's first exec (code_0C909F20.c) */
#define EC_ASIATO       0x0C906082u     /* objexec_O_ASIATO, the footprints (code_0C906082.c) */
#define EC_SE_START     0x8C0941FCu     /* looping SE with an owner (a boundary stub: event SOUND) */

/*
 * 0C900000, mode 0 = its first exec (step 0). The original, for CurrentAct a:
 *   SE 0x39E and 0x3A0 (8C0941FC(se, -1, 8, 0));
 *   a == 0: 0C908620 (the ocean manager: D_0CAB19FC = 0, D_0CAB1A08 = (0, 0, 0), 0C9087E0 = the floating pier
 *           CreateElementalTask(2, 3, 0C908820) [no C], D_0CAB1A04 = CreateElementalTask(6, 3, 0C909F20) = the
 *           ocean, D_0CAB1A20 = 0), then O_ASIATO, then 0C904980 / 0C9049EA (10, 3) [no C] and 0C90002C (2, 0)
 *           with mode 28, wtimer 10 [no C]; mode = 1;
 *   a == 1: O_ASIATO, 0C90B3E0(2), 0C904AFE / 0C904A8C / 0C904B96 (10, 1), 0C90002C mode 29 [no C]; mode = 2
 *           (then a call whose result 2 tail-calls another one [no C]);
 *   a == 2: O_ASIATO, 0C90B3E0(3), 0C904C02 / 0C904C9A / 0C904D06 (10, 1), 0C90002C mode 30 [no C]; mode = 3;
 *   other:  mode = 1.
 * O_ASIATO is CreateElementalTask(2, 3, objexec_O_ASIATO) with taskwk scl.x = 0.0 (the player number).
 * The stand-in does the decompiled part in that order; the tasks marked [no C] (sky, cloud and sea effects, the
 * pier, the act's camera and music controllers) are not created: none of them acts on the player in the
 * reference traces (ec1_*, big_ec3: every record reproduced). Modes 1-3 (the act's frame work) do nothing here.
 */
static void sa1g_stage_ec_level(sa1g_world *w, uint32_t tp, void *user)
{
    int32_t (*se)(int32_t, void *, int32_t, int32_t);
    uint32_t twp = st_rd32(w, tp + 0x20u), prints, ocean = 0;
    uint8_t *t;
    uint16_t act = 0;
    (void)user;
    if (!st_arena(twp)) {
        return;
    }
    t = (uint8_t *)sa1g_world_ptr(w, twp);
    if (t[0] != 0) {
        return;
    }
    memcpy(&act, sa1g_world_ptr(w, SA1G_ADDR_CUR_ACT), 2);
    act &= 0xFFu;                       /* (Uint8)GetStageNumber() */
    se = (int32_t (*)(int32_t, void *, int32_t, int32_t))sa1_fn(SA1_GFN_C(EC_SE_START), 0u);
    se(0x39E, NULL, 8, 0);              /* the original passes -1: no owner */
    se(0x3A0, NULL, 8, 0);
    if (act == 0) {
        st_wr32(w, 0x0CAB19FCu, 0);
        st_wr32(w, 0x0CAB1A08u, 0);     /* 0.0f x 3 */
        st_wr32(w, 0x0CAB1A0Cu, 0);
        st_wr32(w, 0x0CAB1A10u, 0);
        if (st_has_code(EC_OCEAN_INIT)) {
            ocean = sa1g_create_task(6, 3, EC_OCEAN_INIT);
        }
        st_wr32(w, 0x0CAB1A04u, ocean);
        st_wr32(w, 0x0CAB1A20u, 0);
    }
    if (st_has_code(EC_ASIATO)) {
        prints = sa1g_create_task(2, 3, EC_ASIATO);
        if (prints != 0) {
            st_wr32(w, st_rd32(w, prints + 0x20u) + 0x2Cu, 0);     /* scl.x = 0.0f: player 0 */
        }
    }
    t[0] = (uint8_t)(act == 1 ? 2 : act == 2 ? 3 : 1);
}

/* ---- Perfect Chaos (B_CHAOS7, level 19; decompiled: sadc src/overlays/b_chaos7) -------------------------------------- */
#define PC_LEVEL_TASK   0x0C900000u     /* the level task (asm only) */
#define PC_SEA_PLANE    0x0C91514Au     /* the sea plane task (code_0C9150BA.c) */
#define PC_TRIGGER      0x0C914D60u     /* the Super Sonic trigger (code_0C914D60.c) */
#define PC_FN_CHILD     0x8C098CA0u     /* CreateChildTask(im, exec, parent) */
#define PC_FN_CHILDREN  0x8C098E98u     /* func_8C098E98(tp): run the children of tp */

/*
 * 0C900000 (gsa1_rounds_plan.md 11.1, the Super free-roam boot contract shared with the reference simulator's
 * --super-free, sadc harness/src/t_sim.c superfree_exec). The original: exec 1 (mode 0, act 0, 0C914E36): 8C64036C,
 * B_0CA6C248 = 0, the sea plane CreateElementalTask(6, 0, 0C91514A) (0C914E54-0C914E62), the stage loop SE, the child
 * 0C916560, mode 3; two execs later mode 4, two more and 0C914DC6 runs: textures, object lists, the trigger
 * CreateChildTask(6, 0C914D60, tp) (0C914DF4-0C914DFC), the boss 0C917300, the music task 0C914D8E, mode 1; modes 1, 3 and
 * 4 run the task's children (func_8C098E98) and clamp playertwp[0]->pos.y to >= 0. The stand-in, in its first exec
 * (taskwk.mode 0), creates the sea plane (SA1G_SUPER_PLANE: worlds with the stage's landtable) and then the trigger
 * (SA1G_SUPER_TRIGGER: Super Sonic's world, and the stage's own tasks), in that (the game's) order, and sets mode 1;
 * every later exec runs the children (the trigger is a child and runs only from there) and then, in a super-capable world
 * (SA1G_SUPER_WORLD), the ring drain (sa1_world_super.c). No boss, no clamp, no music, no fog (0C900080, the list-2 task,
 * has no C and is not created). The trigger runs at steps 1 and 2 and sends SetInputP(0, 46) at step 2.
 */
static void sa1g_stage_pc_level(sa1g_world *w, uint32_t tp, void *user)
{
    uint32_t twp = st_rd32(w, tp + 0x20u);
    uint8_t *t;
    (void)user;
    if (!st_arena(twp)) {
        return;
    }
    t = (uint8_t *)sa1g_world_ptr(w, twp);
    if (t[0] == 0) {
        if ((w->super & SA1G_SUPER_PLANE) && st_has_code(PC_SEA_PLANE)) {
            (void)sa1g_create_task(6, 0, PC_SEA_PLANE);
        }
        if ((w->super & SA1G_SUPER_TRIGGER) && st_has_code(PC_TRIGGER)) {
            void *(*child)(uint16_t, sa1_gfn, void *) = (void *(*)(uint16_t, sa1_gfn, void *))sa1_fn(SA1_GFN_C(PC_FN_CHILD),
                                                                                                    0u);
            (void)child(6, SA1_GFN_C(PC_TRIGGER), sa1_decode(tp));
        }
        t[0] = 1;
        return;
    }
    ((void (*)(void *))sa1_fn(SA1_GFN_C(PC_FN_CHILDREN), 0u))(sa1_decode(tp));
    if (w->super & SA1G_SUPER_WORLD) {
        sa1g_super_drain(w, twp);
    }
}

/* round 0.9.3a (the General form, sadc docs/research/super_general_design.md 4.1): a General world is booted on the host's
 * stage (level 1 by default) with B_CHAOS7 in the overlay slot, so the level's own exec tables (D_8C19DE30 / D_8C19E0E0)
 * name addresses of another overlay (level 1's list-2 task 0C90023E would be looked up in B_CHAOS7's code map). The
 * Perfect Chaos stand-in is made directly instead: the executor override on 0x0C900000, CreateElementalTask(2, 0,
 * 0x0C900000) stored as the level task (B_8C752BD4). Its world flags have neither SA1G_SUPER_TRIGGER nor SA1G_SUPER_PLANE,
 * so after its first exec it only runs its children and the drain. Inside the world (the boot). */
void sa1g_stage_pc_create(sa1g_world *w)
{
    uint32_t tp;
    if (sa1g_world_xhook(w, PC_LEVEL_TASK, sa1g_stage_pc_level, NULL) != 0) {
        sa1g_fatal(0, "stage tasks: no executor override left for the Perfect Chaos stand-in");
    }
    tp = sa1g_create_task(2, 0, PC_LEVEL_TASK);
    st_wr32(w, SA1G_ADDR_LEVEL_TASK, tp);
    w->level_tp = tp;
    w->stage_tasks = tp != 0 ? 1u : 0u;
    snprintf(w->stage_note, sizeof w->stage_note, "list 0: %08X Perfect Chaos level task (stand-in made directly: the "
             "General form's ring drain; level %d act %d's own tasks not created)", (unsigned)PC_LEVEL_TASK, (int)w->level,
             (int)w->act);
}

/* ---- stand-ins: stage execs without C whose first exec creates decompiled tasks ---------------------- */
typedef struct sa1g_standin {
    const char *image;          /* the overlay image whose code the stand-in creates */
    uint32_t exec;              /* the exec it stands in for */
    sa1g_exec_fn fn;
    const char *what;
} sa1g_standin;

static const sa1g_standin k_standins[] = {
    { "STG01", EC_LEVEL_TASK, sa1g_stage_ec_level, "Emerald Coast level task (stand-in: ocean, footprints)" },
    /* round 6a: the same address in B_CHAOS7 (one occupant of 0x0C900000 per world: the image decides) */
    { "B_CHAOS7", PC_LEVEL_TASK, sa1g_stage_pc_level, "Perfect Chaos level task (stand-in: sea plane, Super trigger, "
                                                       "ring drain)" },
};

static const sa1g_standin *sa1g_standin_of(const sa1g_world *w, uint32_t exec)
{
    size_t i;
    uint32_t k;
    for (i = 0; i < sizeof k_standins / sizeof k_standins[0]; i++) {
        if ((k_standins[i].exec & 0x1FFFFFFFu) != (exec & 0x1FFFFFFFu)) {
            continue;
        }
        for (k = 0; k < w->num_fn_images; k++) {
            if (strcmp(w->fn_images[k]->name, k_standins[i].image) == 0) {
                return &k_standins[i];
            }
        }
    }
    return NULL;
}

void sa1g_stage_tasks(sa1g_world *w)
{
    uint16_t lv = 0;
    int32_t level, k;
    size_t n = 0;
    memcpy(&lv, sa1g_world_ptr(w, SA1G_ADDR_CUR_LEVEL), 2);
    level = (int16_t)lv;
    w->stage_note[0] = 0;
    w->stage_tasks = 0;
    w->level_tp = 0;
    for (k = 0; k < 2; k++) {
        /* the stage start's order: the list-2 task, then the level task */
        uint32_t table = k == 0 ? SA1G_ADDR_LEVEL_EXEC2 : SA1G_ADDR_LEVEL_EXEC, exec = 0, tp = 0;
        int32_t list = k == 0 ? 2 : 0;
        const sa1g_standin *si = NULL;
        const char *how;
        const char *name;
        if (level >= 0 && level < SA1G_STAGE_LEVELS) {
            exec = st_rd32(w, table + 4u * (uint32_t)level);
        }
        if (exec == 0) {
            how = NULL;
        } else if (st_has_code(exec)) {
            tp = sa1g_create_task(2, list, exec);
            how = "runs";
        } else if ((si = sa1g_standin_of(w, exec)) != NULL) {
            if (sa1g_world_xhook(w, exec, si->fn, NULL) != 0) {
                sa1g_fatal(0, "stage tasks: no executor override left");
            }
            tp = sa1g_create_task(2, list, exec);
            how = si->what;
        } else {
            how = "not created: no C";
        }
        if (tp != 0) {
            w->stage_tasks++;
        }
        if (k == 1) {
            st_wr32(w, SA1G_ADDR_LEVEL_TASK, tp);
            w->level_tp = tp;
        }
        if (how != NULL && n < sizeof w->stage_note) {
            name = sa1_fn_name(exec);
            n += (size_t)snprintf(w->stage_note + n, sizeof w->stage_note - n, "%slist %d: %08X%s%s %s",
                                  n ? "; " : "", (int)list, (unsigned)exec, name ? " " : "", name ? name : "", how);
        }
    }
    if (w->stage_note[0] == 0) {
        snprintf(w->stage_note, sizeof w->stage_note, "stage %d has no level tasks", (int)level);
    }
}
