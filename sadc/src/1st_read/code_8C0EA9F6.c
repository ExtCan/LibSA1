/*
 * 1ST_READ 8C0EA9F6 (chunk 8C0E6620, last function): resets the draw state
 * after an effect display (called by func_8C0E7720 and func_8C0E78C0):
 * clears flag bit 0x10 and sets bit 0x20 of the global draw flag word
 * (func_8C056990/func_8C056986), sets both colour blending modes to 1 and
 * clears the constant material colour.
 */
#include "common.h"
#include "sa1/sa1.h"

extern void func_8C056986(Uint32 bits);  /* B_8C88F62C |= bits  (code_8C05697C.c) */
extern void func_8C056990(Uint32 bits);  /* B_8C88F62C &= ~bits (code_8C05697C.c) */

void func_8C0EA9F6(void)
{
    func_8C056990(0x10);
    func_8C056986(0x20);
    njColorBlendingMode(0, 1);
    njColorBlendingMode(1, 1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}
