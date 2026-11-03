/*
 * sa1_question.h - the question mark of Character Select as a character (SA1_API_QUESTION, round 0.9.9; sadc docs/research/gsa1_099_plan.md 5.3, playable099.md 5).
 * Internal: not part of the public API. A create flag on a kind-8 world: the world is byte for byte Super Sonic's, only the render output differs.
 *
 * Game thread only (sa1q_load, at the first create with the flag), or the character's own tick (sa1q_append, the emitter's rules). No FP state, no process static, no game state: the
 * parallel contract of src/world/sa1_world_follower.c. The model and the archive are read from the disc with their SHA-1 at run time, held in memory only and freed with the disc.
 */
#ifndef SA1_QUESTION_H
#define SA1_QUESTION_H

#include "sa1_internal.h"
#include "sa1_capture.h"

struct sa1q_state;

/* ADVERTISE.PRS's model and AVA_CHSEL_E.PRS's archive into ctx->question, once per disc. SA1_ERR_NOT_LOADED, with the reason in sa1_last_error, without the disc's files */
sa1_status sa1q_load(sa1_context *ctx);
/* sa1_unload_disc: frees what sa1q_load holds */
void sa1q_free(sa1_context *ctx);
/* the archive sa1_texture_get and sa1_texture_archive see as (kind 8, SA1_ARCHIVE_QUESTION); NULL before the load */
const sa1_archive *sa1q_archive(const sa1_context *ctx);
/* appends the "?" to b: its foot at the SA1-space point root, turned about the vertical by the angle ang (BAMS) as the menu turns it. The caller refreshes the geometry */
sa1_status sa1q_append(sa1_context *ctx, sa1_meshbuf *b, const float root[3], uint32_t ang);
/* SA1_COPT_QUESTION_SPIN (src/sa1_hostobj.c routes it here): 1 spins as on the menu, 0 faces the way the character faces */
sa1_status sa1q_set_spin(sa1_context *ctx, sa1_character_id id, int32_t value);
/* src/sa1_character.c: where a "?" keeps that option (0 spins, 1 does not); NULL: id is no "?" */
int32_t *sa1q_char_still(const sa1_context *ctx, sa1_character_id id);
/* src/sa1_loader.c: ADVERTISE.PRS unpacked (*overlay, freed by the caller with sa1_mem_free) and AVA_CHSEL_E into cs as its archive 0, both read and SHA-1 checked as a character's */
sa1_status sa1_loader_question(sa1_context *ctx, const char *gdi_path, sa1_charset *cs, uint8_t **overlay, uint32_t *overlay_size);

#endif /* SA1_QUESTION_H */
