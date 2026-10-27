/*
 * sa1_preview.h - object previews (API SA1_API_PREVIEW, round 0.9.4b; sadc docs/research/objects2_plan.md 5.3 and 6.2 with
 * its Review R.1-R.3, R.10, R.20, R.21; objects2_view.md 3 and 5). Internal: not part of the public API.
 *
 *   src/sa1_preview.c               the context side: the registry (a process table keyed by the context, as sa1_set.c's),
 *                                   the API of include/libsa1.h "object previews", the per-kind showcase lifetime, the
 *                                   capture of each showcase pair and its split into one render output per copy
 *   src/world/sa1_world_preview.c   the world side: the showcase world (boot without a player), the copies' tasks and
 *                                   conditions, the view per exec, the pair, the ring places
 *
 * The showcase of a context and character kind is a world of that kind's image (the character worlds' image cache) booted
 * with SA1G_BOOT_NO_PLAYER at level 1 act 0, an empty landtable, no SET manager, no host-object / SET / face / emote hooks;
 * it is never in the character table and its events are drained and dropped after every pair. A copy is the type's own task
 * made directly (CreateElementalTask with the catalogue's im / level / exec) at grid slot k (the free slot nearest the
 * origin): SA1 ((k mod 16) - 7.5, 0, (k div 16) - 7.5) x 2,048 (review R.1), with an OBJ_CONDITION of its own in the game's condition array B_8C79A3E0 at
 * index k + 1 (index 0 stays unused): scCount 1, scUserFlag = the taken rings, ssCondition 0x8001 (| 0x0004 broken), pTask,
 * a 0x20-byte SET entry in segment 0x18 (usID = type - 1, the BAMS angles, the slot, scl = params), unionStatus 0 (no
 * range-out: 8C0919CC tests nothing for a range of 0). Every exec of the four types runs under its own view
 * (sa1g_world_xhook): XMTRX, slot 0 of the matrix stack and the camera taskwk's position are loaded with a view that looks
 * at the copy's slot along -z from SA1PV_VIEW_UP above and the copy's distance in front, so the game's visibility test
 * 8C038D00 passes for every part of the copy; a group's rings run inside their group's exec (8C098E98) and see its view.
 */
#ifndef SA1_PREVIEW_H
#define SA1_PREVIEW_H

#include "sa1_internal.h"

struct sa1g_world;

/* ---- the slot grid (review R.1) ------------------------------------------------------------------------------------ */
#define SA1PV_GRID       16u                    /* 16 x 16 slots */
#define SA1PV_SLOTS      (SA1PV_GRID * SA1PV_GRID)
#define SA1PV_PITCH      2048.0f                /* SA1 units between neighbouring slots */
#define SA1PV_REACH      1000.0f                /* every part of a copy lies within this of its slot (the split's rule) */
#define SA1PV_COND0      1u                     /* condition index of slot 0 (index 0 of the array stays unused) */
#define SA1PV_VIEW_UP    12.0f                  /* the eye above the slot (SA1 units) */
#define SA1PV_VIEW_MIN   70.0f                  /* the eye's smallest distance in front of the slot (the probe's) */
#define SA1PV_HEADROOM   32768u                 /* the showcase heap's largest free block needed for a new copy (the SET
                                                   layer's heap guard: CCL_Init never returns from a failed CAlloc) */
#define SA1PV_CAP_SPARE  32u                    /* capture records kept free of the copies' expected records */

/* the SA1 position of slot k (exact floats) */
static inline void sa1pv_slot_pos(uint32_t k, float p[3])
{
    p[0] = ((float)(int32_t)(k % SA1PV_GRID) - 7.5f) * SA1PV_PITCH;
    p[1] = 0.0f;
    p[2] = ((float)(int32_t)(k / SA1PV_GRID) - 7.5f) * SA1PV_PITCH;
}

/* the slot nearest to an SA1 point (x, z only: every slot lies at y 0) */
static inline uint32_t sa1pv_slot_of(double x, double z)
{
    double gx = x / (double)SA1PV_PITCH + 7.5, gz = z / (double)SA1PV_PITCH + 7.5;
    int32_t ix, iz;
    if (!(gx == gx) || !(gz == gz)) {
        return 0u;
    }
    gx = gx < 0.0 ? 0.0 : gx > 15.0 ? 15.0 : gx;
    gz = gz < 0.0 ? 0.0 : gz > 15.0 ? 15.0 : gz;
    ix = (int32_t)(gx + 0.5);
    iz = (int32_t)(gz + 0.5);
    return (uint32_t)iz * SA1PV_GRID + (uint32_t)ix;
}

/* ---- the context side (src/sa1_preview.c) --------------------------------------------------------------------------- */
/* frees every showcase and copy of ctx and its registry (sa1_characters_destroy_all: at sa1_unload_disc, at
 * sa1_load_disc's start and at sa1_shutdown, BEFORE the context's world state - the image cache and shared segments the
 * showcases were made from - is destroyed: review R.2) */
void sa1pv_destroy_all(sa1_context *ctx);

/* tests (tests/preview_test.c): a fault forced into a copy (its task's exec becomes an address without code: the kind's
 * showcase faults at its next pair); SA1_ERR_NOT_FOUND unknown id */
sa1_status sa1pv_test_fault(sa1_context *ctx, uint32_t id);
/* tests: the showcase of a kind and its counts (all zero: none) */
typedef struct sa1pv_test_info {
    uint32_t exists;            /* the kind has a showcase */
    uint32_t dead;              /* it faulted */
    uint32_t copies, leaving;   /* live copies; slots whose task was freed and whose condition is not released yet */
    uint32_t idle_pairs;        /* pairs without copies */
    uint32_t pairs;             /* pairs the showcase ran */
    uint32_t heap_largest;      /* the game heap's largest free block (bytes) */
    uint32_t heap_free;         /* the game heap's free bytes */
    uint32_t free_tasks;        /* tasks on the task system's free list */
    uint32_t live_tasks;        /* tasks of the four SET execs in the task lists (a group's rings not counted) */
    uint32_t records_est;       /* the copies' expected capture records */
    uint32_t cap_capacity;      /* the capture's records per pair */
    uint32_t cap_records;       /* records of the last pair */
} sa1pv_test_info;
/* world (may be NULL): the showcase world, for tests that read its memory */
sa1_status sa1pv_test_showcase(const sa1_context *ctx, int32_t kind, sa1pv_test_info *out, struct sa1g_world **world);
/* tests: the grid slot of a copy (-1: unknown id) */
int32_t sa1pv_test_slot(const sa1_context *ctx, uint32_t id);

/* ---- the world side (src/world/sa1_world_preview.c; only in builds with the world core) ---------------------------- */
/* a copy as the world makes it */
typedef struct sa1pv_wcopy {
    uint32_t type;              /* SA1_OBJTYPE_* */
    uint32_t slot;              /* grid slot 0..255 */
    float params[3];
    float rotation[9];          /* host rotation of an INNER type (all zero: none) */
    uint32_t cond;              /* ssCondition */
    uint32_t bits;              /* scUserFlag */
} sa1pv_wcopy;

/* the showcase world of a kind, booted (errors are recorded with sa1_fail) */
sa1_status sa1wp_create(sa1_context *ctx, int32_t kind, struct sa1g_world **out);
void sa1wp_destroy(sa1_context *ctx, struct sa1g_world *w);
/* the eye's distance in front of slot k for its copy (SA1 units) */
void sa1wp_set_view(struct sa1g_world *w, uint32_t slot, float dist);
/* makes the copy's task (outside ticks); *tp its guest address (0 when the task system is full) */
sa1_status sa1wp_add(struct sa1g_world *w, const sa1pv_wcopy *c, uint32_t *tp);
/* frees the copy's task if it still points at slot k (FreeTask: DestroyTask runs at its next exec; a group frees its rings
 * as the game does) */
sa1_status sa1wp_remove(struct sa1g_world *w, uint32_t slot, uint32_t tp);
/* 1: a live task still points at slot k's condition (its exec is not DestroyTask) */
int sa1wp_slot_busy(const struct sa1g_world *w, uint32_t slot);
/* zeroes slot k's condition and SET entry */
void sa1wp_release(struct sa1g_world *w, uint32_t slot);
/* one pair, its events drained and dropped; the fault's status (the world is dead then) */
sa1_status sa1wp_pair(struct sa1g_world *w);
/* the fault message of a dead showcase ("" alive) */
const char *sa1wp_fault(const struct sa1g_world *w);
/* the game heap's largest free block (bytes) */
uint32_t sa1wp_heap_largest(const struct sa1g_world *w);
/* the ring places of an O_GRING record (8C087EEE / 8C088000 on a scratch task work: pos 0, the angles of the host
 * rotation, scl = params), SA1 space relative to the record; 3 floats per ring, the ring's index i in idx (its bit 1 << i);
 * *n = the rings (at most 16) */
sa1_status sa1wp_ring_places(struct sa1g_world *w, const float params[3], const float rotation[9], float places[48],
                             uint32_t idx[16], uint32_t *n);
/* 1 when the code map has native code for DC address a */
int sa1wp_native(uint32_t a);
/* tests: the counts of sa1pv_test_info that the world holds; the forced fault */
void sa1wp_test_counts(const struct sa1g_world *w, sa1pv_test_info *out);
sa1_status sa1wp_test_break(struct sa1g_world *w, uint32_t tp);

#endif /* SA1_PREVIEW_H */
