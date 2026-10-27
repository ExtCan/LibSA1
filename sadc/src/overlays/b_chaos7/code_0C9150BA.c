/*
 * B_CHAOS7 0C9150A0 (Perfect Chaos stage): chunk 0C9150A0-0C915340,
 * functions 1-4 of 4 (the whole chunk).
 *
 *  func_0C9150A0  called (BSR) only by the boss main 0C917300: stops the
 *                 music (func_8C107AAA destroys the music task, jukebox.md)
 *                 and loads the level-19 act-0 SET and path files again
 *                 (func_8C014242(19, 0) "SL_..B.bin", func_8C014308(19, 0)
 *                 "PL_..B.bin", sa1/protos.h). It is not part of the sea
 *                 plane, but it is in this file on purpose: SHC places the
 *                 first literal pool of the chunk after the mode dispatch
 *                 of func_0C91514A (0C915186) only when func_0C9150A0's
 *                 three pool words are pending too. Without it the pool
 *                 moves behind mode 1 and the "event running" test of mode
 *                 1 becomes BF / BRA instead of the original BT.
 *
 * The sea plane: the dynamic collision object Sonic stands and runs on in
 * the Perfect Chaos stage (super_sonic.md Review R.2.1). The level task's
 * act-0 mode 0C914E36 creates it with CreateElementalTask(6, 0,
 * func_0C91514A) (0C914E54-0C914E62).
 *
 *  func_0C9150BA  follow: moves the plane under player 0 (x / z from
 *                 playertwp[0]->pos, y from twp->scl.y), copies the
 *                 position into the registered NJS_OBJECT (twp->counter)
 *                 and sets / clears twp->flag 0x100 from
 *                 func_8C07CD16(&twp->pos, 80.0f) (a player within 80
 *                 units of the plane's position).
 *  func_0C915120  dest: unregisters (func_8C052940) and frees
 *                 (func_8C052A5E) the collision object, then FreeTask.
 *  func_0C91514A  exec.
 *                 - Stage check, every step: GetStageNumber() != 0x1300
 *                   forces mode 3.
 *                 - Mode 0: mode 1; the plane starts at player 0's
 *                   position; twp->timer.ptr = B_CHAOS7_Collision_Plane
 *                   (0x0C966D30, evalflags 0x17, model D_0C966D08: an 80 x
 *                   80 quad at y = 0); dest = func_0C915120; takes a pooled
 *                   NJS_OBJECT (func_8C052A1E) into twp->counter.ptr with
 *                   the template's evalflags & ~3 and model, scale 1,
 *                   angle 0, pos (x, 0, z); registers it as a collision
 *                   object with attr 0x28000000 (func_8C0528AE); timer = 0;
 *                   falls through to mode 1.
 *                 - Mode 1: nothing while an event runs
 *                   (func_8C049A4E() == 1); counts twp->timer.l and does
 *                   nothing more (no follow) before 600 steps. From then on,
 *                   unless the B_CHAOS7 byte B_0CA6C249 is set, wtimer
 *                   counts; every second step once it has passed 60 the
 *                   player's Super bit is tested (equipment & 0x8000 of
 *                   GetPlayerWorkPtr(0)): clear -> mode 2; wtimer = 60
 *                   either way. Then the plane follows (tail call).
 *                 - Mode 2: unless B_0CA6C249 is set, the plane sinks 0.1
 *                   per step (twp->scl.y) and below -10 starts player 0's
 *                   death (func_8C07BBC2(0)) every step; then it follows.
 *                 - Mode 3: func_8C091BA4(tp) (tail call): the SET-object
 *                   removal, i.e. exec = DestroyTask.
 *
 * PORTABILITY: func_8C091BA4 (code_8C091B8A.c) first clears bits of
 * ((OBJ_CONDITION *)tp->ocp)->ssCondition. This task is not a SET object
 * (CreateElementalTask), so tp->ocp is NULL there: on the DC the two
 * 16-bit writes go to address 2 (boot ROM) and are lost; on a PC host
 * the same C dereferences NULL. Mode 3 runs whenever the stage number is
 * not 0x1300 (a world that creates this task on another stage).
 *
 * B_0CA6C249 lies past the end of the B_CHAOS7 image (0x0CA6B454), in the
 * overlay's BSS, which the split does not label: the pool holds the plain
 * number H'0CA6C249. As in code_0C911DC0.c (STG05), under SHC it is spelled
 * as that address so that the pool entry compares equal; on PC it is an
 * ordinary variable. It is a byte flag (MOV.B / TST) that the boss chunk
 * 0C916EE0 and the stage chunk 0C914D60 also read (role UNVERIFIED: it
 * freezes the plane's mode 1 / mode 2 logic but not the follow).
 *
 * Code generation:
 *  - mode 0 sets scl and ang with chained assignments
 *    (obj->scl[0] = obj->scl[1] = obj->scl[2] = 1.0f): SHC stores [2], [1],
 *    [0] through a base pointer, as in the original;
 *  - the 0.1f of mode 2 is loaded through FPUL (MOV.L / LDS / FSTS)
 *    because R0 holds the offset 48 of scl.y at that point (no MOVA): it
 *    needs no special C;
 *  - modes 1 and 2 each end with their own func_0C9150BA(twp) call and
 *    "break" (SHC merges them into one BRA tail call placed after mode 2,
 *    then mode 3, then the plain return, as in the original); a follow
 *    call after the switch puts mode 3 first;
 *  - mode 1 increments timer.l as its own statement (the original copies
 *    the new value before the compare: MOV R2,R1) and reads the player
 *    work into a local (MOV R0,R4);
 *  - mode 2 compares "twp->scl.y < -10.0f" (the original loads scl.y again
 *    after the store).
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"

/* B_CHAOS7 BSS byte flag (see above) */
#if defined(TARGET_PC)
extern Sint8 B_0CA6C249;
#else
#define B_0CA6C249 (*(Sint8 *)0x0CA6C249)
#endif

/* B_CHAOS7 data (extern only). Labelled basicmodel in the data label list,
 * but it is an NJS_OBJECT: evalflags at +0, model D_0C966D08 at +4. */
extern NJS_OBJECT B_CHAOS7_Collision_Plane;

/* 1ST_READ functions not declared in the sa1 headers */
NJS_OBJECT *func_8C052A1E(void);                        /* allocate a pooled NJS_OBJECT (code_8C052A00.c) */
void func_8C052A5E(NJS_OBJECT *obj);                    /* free it */
void func_8C0528AE(Uint32 attr, task *tp, NJS_OBJECT *obj); /* register a collision object (code_8C052518.c) */
void func_8C052940(task *tp, NJS_OBJECT *obj);          /* unregister it */
Sint32 func_8C07CD16(NJS_POINT3 *pos, Float r);         /* a player within r of pos (code_8C07CBEC.c) */
void func_8C091BA4(task *tp);                           /* SET object removal (code_8C091B8A.c) */

void func_8C107AAA(void);                               /* as in code_0C9122EE.c (STG05) */

void func_0C9150BA(taskwk *twp);
void func_0C915120(task *tp);

void func_0C9150A0(void)
{
    func_8C107AAA();
    func_8C014242(19, 0);
    func_8C014308(19, 0);
}

void func_0C9150BA(taskwk *twp)
{
    NJS_OBJECT *obj;

    obj = twp->counter.ptr;
    twp->pos.x = playertwp[0]->pos.x;
    twp->pos.y = twp->scl.y;
    twp->pos.z = playertwp[0]->pos.z;
    obj->pos[0] = twp->pos.x;
    obj->pos[1] = twp->pos.y;
    obj->pos[2] = twp->pos.z;
    if (func_8C07CD16(&twp->pos, 80.0f)) {
        twp->flag |= 0x100;
    } else {
        twp->flag &= ~0x100;
    }
}

void func_0C915120(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (twp->counter.ptr) {
        func_8C052940(tp, twp->counter.ptr);
        func_8C052A5E(twp->counter.ptr);
    }
    FreeTask(tp);
}

void func_0C91514A(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;
    playerwk *pwp;

    twp = tp->twp;
    if (GetStageNumber() != 0x1300) {
        twp->mode = 3;
    }
    switch (twp->mode) {
    case 0:
        twp->mode = 1;
        twp->pos = playertwp[0]->pos;
        twp->timer.ptr = &B_CHAOS7_Collision_Plane;
        tp->dest = func_0C915120;
        obj = func_8C052A1E();
        twp->counter.ptr = obj;
        obj->evalflags = ((NJS_OBJECT *)twp->timer.ptr)->evalflags & ~3;
        obj->scl[0] = obj->scl[1] = obj->scl[2] = 1.0f;
        obj->ang[0] = obj->ang[1] = obj->ang[2] = 0;
        obj->pos[0] = twp->pos.x;
        obj->pos[1] = 0.0f;
        obj->pos[2] = twp->pos.z;
        obj->model = ((NJS_OBJECT *)twp->timer.ptr)->model;
        obj->child = NULL;
        obj->sibling = NULL;
        func_8C0528AE(0x28000000, tp, obj);
        twp->timer.l = 0;
        /* fall through */
    case 1:
        if (func_8C049A4E() == 1) {
            break;
        }
        twp->timer.l++;
        if (twp->timer.l < 600) {
            break;
        }
        if (!B_0CA6C249) {
            if (twp->wtimer++ > 60) {
                pwp = GetPlayerWorkPtr(0);
                if (!(pwp->equipment & 0x8000)) {
                    twp->mode = 2;
                }
                twp->wtimer = 60;
            }
        }
        func_0C9150BA(twp);
        break;
    case 2:
        if (!B_0CA6C249) {
            twp->scl.y -= 0.099999996f;
            if (twp->scl.y < -10.0f) {
                func_8C07BBC2(0);
            }
        }
        func_0C9150BA(twp);
        break;
    case 3:
        func_8C091BA4(tp);
        break;
    }
}
