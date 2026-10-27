/*
 * ho_glue.h - the two halves of the round-5 host-object oracle (gsa1_rounds_plan.md 9.4, libsa1_interaction.md 4.3):
 *
 *   ho_oracle.c    the harness side: the --hostobj script, the hooks around the task executor and the CCL pass, the
 *                  special hook at the proxies' exec 0x7F000100, typed wrappers that run game functions with
 *                  sh4_invoke, the NAME.obj.bin sidecar (sa1_trace_ext.h)
 *   ho_lib_glue.c  libsa1's side: the world and the context that libsa1's own sa1_world_hostobj.c and sa1_hostobj.c
 *                  (compiled into the harness unchanged) run against
 *
 * Plain types and libsa1's public header only: the two files include headers that do not mix (sh4.h declares the FP
 * models that include/pc/sh4_fpmodel.h defines inline).
 */
#ifndef HO_GLUE_H
#define HO_GLUE_H

#include <stdint.h>

#include "libsa1.h"

/* ---- ho_oracle.c ---- */
/* the typed cdecl wrapper that runs game function dc in the interpreter (routed when the trace routes it); NULL: none */
void *hoo_game_fn(uint32_t dc);
/* the oracle cannot go on (a fault inside a game call, an unknown function): message, exit */
void hoo_fatal(const char *fmt, ...);

/* ---- ho_lib_glue.c ---- */
/* the world (arena = DC RAM at ram, extension segment 0x18 at ext / ext_guest, ext_size bytes) and the context with
 * libsa1's default coordinates; character id 1. 0: ok */
int hoo_lib_init(uint8_t *ram, uint8_t *ext, uint32_t ext_guest, uint32_t ext_size);
sa1_context *hoo_lib_ctx(void);
sa1_character_id hoo_lib_chr(void);
/* the world's hooks of one step (libsa1: sa1g_step_game; sub = B_8C754EC8) */
void hoo_lib_pre(int32_t sub);
void hoo_lib_post(int32_t sub);
void hoo_lib_step_done(void);
/* the proxies' exec (LIBSA1_HOST, 0x7F000100) and its native function (cdecl void (task *)) */
void hoo_lib_exec(uint32_t tp);
const void *hoo_lib_exec_native(void);
/* the world events of the layer (HURT, DEATH, CAPACITY ...: libsa1's sa1g_world_poll_events), SA1 space */
uint32_t hoo_lib_world_events(sa1_event *out, uint32_t cap);
/* the taskwk of an object's proxy (0: none) */
uint32_t hoo_lib_proxy_twp(uint32_t object);
/* sa1_character_add_rings inside the world (libsa1: sa1ho_world_add_rings, the game's 8C08A198) */
sa1_status hoo_lib_add_rings(int32_t n);

#endif
