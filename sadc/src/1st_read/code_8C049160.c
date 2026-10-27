#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 8C049160 (chunk 8C049160.src, 18 B): copies the camera task's position
 * (camera_twp = D_8C18B7EC, taskwk.pos at +0x20) into *pos. The only caller
 * is the follower AI's supervisor func_8C081D4E (code_8C081D00.c:142), which
 * respawns the slot-1 follower 50 units above this point.
 *
 * The other two functions of the chunk (8C049172: B_8C749158 +0x44 = arg;
 * 8C04917A: tail call 8C01AD2C(8C048BAE, 0, 2)) are not part of this task
 * and stay assembly.
 */
void func_8C049160(NJS_POINT3 *pos)
{
    *pos = camera_twp->pos;
}
