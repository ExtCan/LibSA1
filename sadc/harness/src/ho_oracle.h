/*
 * ho_oracle.h - the round-5 host-object oracle as the trace command uses it (t_sim.c; implementation ho_oracle.c,
 * gsa1_rounds_plan.md 9.4, libsa1_interaction.md 4.3, sidecar format sa1_trace_ext.h).
 */
#ifndef HO_ORACLE_H
#define HO_ORACLE_H

#include <stdint.h>

#include "sa1_trace.h"
#include "sa1_trace_ext.h"

/* --hostobj FILE: reads the script (relative paths also from the repository root); 0 ok */
int ho_oracle_load(const char *path);
/* --create-task: a task for the sidecar header (world_replay.c makes it after the boot); tp: the task made here (its
 * taskwk is reported after every step with the proxies, handle 0x80 + its index) */
void ho_oracle_add_task(const sa1_trace_obj_task *t, uint32_t tp);
/* round 0.9.2: the same with the version-2 fields (x: /SX,SY,SZ and +ocp; x->flags 0: as ho_oracle_add_task). A run with
 * any flag set writes a version-2 sidecar (sa1_trace_ext.h sa1_trace_obj_header_v2) */
void ho_oracle_add_task_v2(const sa1_trace_obj_task *t, const sa1_trace_obj_task_ext *x, uint32_t tp);
/* a sidecar is written (--hostobj or --create-task) */
int ho_oracle_active(void);
/* after the stage start and the routing set-up: libsa1's layer, the hooks; 0 ok */
int ho_oracle_setup(void);
/* around each logic step (the trace's step hook): the script's calls before it, the sidecar record after it */
void ho_oracle_step_begin(int step);
void ho_oracle_step_end(int step, int nrec);
/* after the run */
void ho_oracle_summary(void);
int ho_oracle_write(const char *prefix, const sa1_trace_header *h, int nrecs);
int ho_oracle_check(const char *ref_path, int nrecs);

#endif
