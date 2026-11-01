/*
 * STG08 0C90E810 (Ice Cap): chunk 0C90DD40-0C90EB20 (asm/overlays/stg08/text/0C90DD40.src), function 11 of 15
 * (the other functions of the chunk are still assembly).
 *
 *  func_0C90E810  exec of Ice Cap's start push (created by the course start 0C90E900 as a task with an anywk,
 *                 AllocAnywk = CAlloc(1, ...), so the counter starts at 0): every frame it raises player 0's and
 *                 (when it exists) player 1's player-space speed spd.x to at least 4.0; after the counter passed
 *                 100 (102 frames: the compare is made before the increment) the task frees itself.
 *
 * From the asm (0C90DD40.src:1458-1504): player 0's work pointer is not tested for NULL, player 1's is (TST R4,R4);
 * the constant 4.0f (pool L0C90E8DC, H'40800000) is loaded once into FR4 and shared by both compares; the awp word is
 * compared unsigned (CMP/HI #100) BEFORE the ADD #1 and stored back; FreeTask (pool L0C90E8E0) is a JMP tail call.
 * Callees: GetPlayerWorkPtr (func_8C07CFDE, 1ST_READ) twice and FreeTask (1ST_READ); the only memory written is the
 * two players' spd.x (playerwk +0x38) and the task's own awp word.
 *
 * Code generation: the constant needs the local 'push' (two literal 4.0f give a second MOVA / FMOV.S load and a
 * reloaded R0 = 56 for player 1). shcdiff --image STG08: MATCH (43 instructions). No FMAC in the asm.
 */
#include "common.h"
#include "sa1/sa1.h"

void func_0C90E810(task *tp)
{
    playerwk *p0;
    playerwk *p1;
    Float push;

    p0 = GetPlayerWorkPtr(0);
    p1 = GetPlayerWorkPtr(1);
    push = 4.0f;
    if (push > p0->spd.x) {
        p0->spd.x = push;
    }
    if (p1 != NULL && push > p1->spd.x) {
        p1->spd.x = push;
    }
    if ((*(Uint32 *)tp->awp)++ > 100) {
        FreeTask(tp);
    }
}