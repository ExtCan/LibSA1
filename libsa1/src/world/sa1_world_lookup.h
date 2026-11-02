/*
 * sa1_world_lookup.h - code map lookups outside a world (round 0.9.8, seam S8; 098-lib-threads: L4, sadc docs/research/gsa1_098_plan.md
 * 6.4, follow096_scale.md 2.7).
 *
 * The generated sa1g_fn_lookup / sa1_fn walk sa1g_fn_active, the images of the world that is running. Only code inside a world may
 * do that: another thread's sa1g_world_enter / _leave rewrites the list (libsa1 0.2 / 0.3: an access violation in sa1_fn_name, in 3 of
 * 63 runs of 99 worlds on several contexts and threads). Code that can run outside a world looks up the base map, which never
 * changes: SA1G_FN_NAME_OUTSIDE, sa1g_game_ready, sa1g_particles_linked.
 */
#ifndef SA1_WORLD_LOOKUP_H
#define SA1_WORLD_LOOKUP_H

#include <stdint.h>

#include "sa1_world.h"

/* pclower's template (sa1_world_nogame.c without game code): the base image only, never sa1g_fn_active */
const sa1g_fnent *sa1g_fn_lookup_base(uint32_t g);
/* pclower's template: sa1_fn_addr's reverse table, built once (L5) */
void sa1g_fn_addr_build(void);

/* the name of a base-map function, from any thread, in or outside a world */
const char *sa1g_fn_name_base(uint32_t g);
#define SA1G_FN_NAME_OUTSIDE(a) sa1g_fn_name_base(a)

/* sa1_world.c, the process's first world creation, under the process lock: sa1g_game_ready and sa1g_particles_linked resolved */
void sa1g_lookup_resolve(void);

/* sa1_world.c: the process lock (trap reservations; sa1_world_image.c's pcdata template, L6) */
void sa1g_proc_lock(void);
void sa1g_proc_unlock(void);
/* sa1_world_image.c: the pcdata template goes with the process state, under the process lock */
void sa1g_pcd_template_free(void);

#endif /* SA1_WORLD_LOOKUP_H */
