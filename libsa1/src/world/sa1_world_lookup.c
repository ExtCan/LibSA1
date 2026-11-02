/*
 * sa1_world_lookup.c - what the world core asks about the build, from any thread (round 0.9.8; 098-lib-threads: L4, sadc
 * docs/research/gsa1_098_plan.md 6.4, follow096_scale.md 2.7 and 5.2 L4, L9). See sa1_world_lookup.h.
 *
 * sa1g_game_ready and sa1g_particles_linked are properties of the build. They are resolved once, at the process's first world
 * creation under the process lock (sa1g_process_ref). Before that (sa1_load_disc, sa1_character_prepare with no world yet) they
 * answer from the base map, which never changes, and write nothing. No thread ever reads sa1g_fn_active here.
 */
#include "sa1_world.h"
#include "sa1_world_lookup.h"

static int sa1g_ready_value = -1;       /* -1: not resolved yet */
static int sa1g_particles_value = -1;

const char *sa1g_fn_name_base(uint32_t g)
{
    const sa1g_fnent *e = sa1g_fn_lookup_base(g);
    return e != NULL ? e->name : NULL;
}

static int sa1g_ready_compute(void)
{
    return sa1g_game_linked() && sa1g_fn_name_base(SA1G_FN_PAD_UPDATE) != NULL && sa1g_fn_name_base(SA1G_FN_EXECUTOR) != NULL
           && sa1g_fn_name_base(SA1G_FN_CCL) != NULL && sa1g_fn_name_base(SA1G_FN_LAND_UPDATE) != NULL;
}

/* the particle manager has C in this build (a --lower subset may leave it out: then it is not run) */
static int sa1g_particles_compute(void)
{
    return sa1g_game_linked() && sa1g_fn_name_base(SA1G_FN_PARTICLES) != NULL;
}

void sa1g_lookup_resolve(void)
{
    if (__atomic_load_n(&sa1g_ready_value, __ATOMIC_ACQUIRE) < 0) {
        __atomic_store_n(&sa1g_particles_value, sa1g_particles_compute(), __ATOMIC_RELEASE);
        __atomic_store_n(&sa1g_ready_value, sa1g_ready_compute(), __ATOMIC_RELEASE);
    }
}

int sa1g_game_ready(void)
{
    int v = __atomic_load_n(&sa1g_ready_value, __ATOMIC_ACQUIRE);
    return v >= 0 ? v : sa1g_ready_compute();
}

int sa1g_particles_linked(void)
{
    int v = __atomic_load_n(&sa1g_particles_value, __ATOMIC_ACQUIRE);
    return v >= 0 ? v : sa1g_particles_compute();
}
