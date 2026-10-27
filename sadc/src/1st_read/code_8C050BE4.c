/*
 * Input module: pad update (1ST_READ 8C050BE4-8C05117E).
 * Gets the Katana peripheral of each port (A0/B0/C0/D0 = 0/6/12/18), packs the
 * connected ones (support & 0xC000E) into B_8C754DA8[] (slot -> peripheral,
 * B_8C754E9C[] = its port number, B_8C754E8C[port] = connected flag), copies
 * slots 0/1 into the game's pad records B_8C754C08[] when pad reading and that
 * slot are enabled and resets every other record, then runs the soft-reset
 * check (8C050A40), the analog conversion (8C05117E) and the digital/key-repeat
 * state of each slot (8C050ACE).
 *
 * NON_MATCHING (func_8C050BE4): 4 instructions differ, all in the last port
 * block: the stack slots of cnt * 4 and &B_8C754DA8[cnt] are swapped (orig
 * cnt * 4 at @(4,R15), the slot address at @(8,R15); here the reverse). Code,
 * control flow and every register match otherwise.
 * What the attempts established:
 *  - the chained init "[0] = [1] = [2] = [3] = x" gives the 12/8/4/0 store order;
 *  - the port number must be a variable (the original spills it); ports 0-2
 *    go through the local pp (no cnt * 4 CSE), the last one indexes
 *    B_8C754DA8[cnt] directly (pp there gives pp/port/cnt*4 = 0/4/8, direct
 *    gives port/&[cnt]/cnt*4 = 0/4/8, the original has port/cnt*4/&[cnt]);
 *    pdGetPeripheral(port * 6) and B_8C754E8C[port] compile identically;
 *    index syntax, casts, statement order and block scopes do not move the
 *    slots;
 *  - the two record loops need different counters (i, j) and the reset loop
 *    must walk a local pointer (per = B_8C754C08): this is what makes SHC
 *    split the B_8C754DA8 live range at the copy loop (MOV R9,R11), give
 *    &D_8C19E3A9[0]/[1] R8/R9 and reuse the EXTU of the counter in the 4th
 *    unrolled copy, as in the original.
 */
#include "common.h"
#include "sa1/input.h"
#include "sa1/ninja_funcs.h"

extern Uint8 D_8C19E3A8;                /* pad reading enabled (code_8C0509A0.c) */
extern Uint8 D_8C19E3A9[2];             /* per-slot enable, slots 0 and 1 (code_8C0509A0.c) */
extern PDS_PERIPHERAL D_8C19E3AC;       /* empty peripheral for unused slots */
extern Sint32 B_8C754E8C[4];            /* per-port connected flag (A..D) */
extern Sint32 B_8C754E9C[4];            /* per-slot port number, -1 = none */

extern void func_8C034DE0(void);

ASM_STUB(void func_8C050A40(void))
ASM_STUB(void func_8C050ACE(Sint32 n))

#ifdef NON_MATCHING
void func_8C050BE4(void) {
    Sint32 cnt;
    Sint32 port;
    Uint8 i;
    Uint8 j;
    PDS_PERIPHERAL **pp;
    PDS_PERIPHERAL *per;

    B_8C754DA8[0] = B_8C754DA8[1] = B_8C754DA8[2] = B_8C754DA8[3] = &D_8C19E3AC;
    B_8C754E9C[0] = B_8C754E9C[1] = B_8C754E9C[2] = B_8C754E9C[3] = -1;
    cnt = 0;
    port = 0;
    pp = &B_8C754DA8[cnt];
    *pp = (PDS_PERIPHERAL *)pdGetPeripheral(0);
    if (((*pp)->support & 0xC000E) == 0xC000E) {
        B_8C754E8C[0] = 1;
        B_8C754E9C[cnt] = port;
        cnt++;
    } else {
        B_8C754E8C[0] = 0;
    }
    port = 1;
    pp = &B_8C754DA8[cnt];
    *pp = (PDS_PERIPHERAL *)pdGetPeripheral(6);
    if (((*pp)->support & 0xC000E) == 0xC000E) {
        B_8C754E8C[1] = 1;
        B_8C754E9C[cnt] = port;
        cnt++;
    } else {
        B_8C754E8C[1] = 0;
    }
    port = 2;
    pp = &B_8C754DA8[cnt];
    *pp = (PDS_PERIPHERAL *)pdGetPeripheral(12);
    if (((*pp)->support & 0xC000E) == 0xC000E) {
        B_8C754E8C[2] = 1;
        B_8C754E9C[cnt] = port;
        cnt++;
    } else {
        B_8C754E8C[2] = 0;
    }
    port = 3;
    B_8C754DA8[cnt] = (PDS_PERIPHERAL *)pdGetPeripheral(18);
    if ((B_8C754DA8[cnt]->support & 0xC000E) == 0xC000E) {
        B_8C754E8C[3] = 1;
        B_8C754E9C[cnt] = port;
        cnt++;
    } else {
        B_8C754E8C[3] = 0;
    }
    if (cnt == 0) {
        B_8C754DA8[0] = &D_8C19E3AC;
    }
    func_8C034DE0();
    if (D_8C19E3A8 == 1) {
        for (i = 0; i < 8; i++) {
            if (i == 0 && D_8C19E3A9[0] == 1) {
                B_8C754C08[0] = *B_8C754DA8[0];
            } else if (i == 1 && D_8C19E3A9[1] == 1) {
                B_8C754C08[1] = *B_8C754DA8[1];
            } else {
                B_8C754C08[i].old = B_8C754C08[i].on;
                B_8C754C08[i].on = 0;
                B_8C754C08[i].off = 0xFFFFFFFF;
                B_8C754C08[i].press = 0;
                B_8C754C08[i].release = 0;
                B_8C754C08[i].x1 = 0;
                B_8C754C08[i].y1 = 0;
                B_8C754C08[i].l = 0x8000;
                B_8C754C08[i].r = 0x8000;
                B_8C754C08[i].x2 = -0x8000;
                B_8C754C08[i].y2 = -0x8000;
            }
        }
    } else {
        per = B_8C754C08;
        for (j = 0; j < 8; j++) {
            per[j].old = per[j].on;
            per[j].on = 0;
            per[j].off = 0xFFFFFFFF;
            per[j].press = 0;
            per[j].release = 0;
            per[j].x1 = 0;
            per[j].y1 = 0;
            per[j].l = 0x8000;
            per[j].r = 0x8000;
            per[j].x2 = -0x8000;
            per[j].y2 = -0x8000;
        }
    }
    func_8C050A40();
    func_8C05117E();
    for (port = 0; port < 4; port++) {
        func_8C050ACE(port);
    }
}
#else
ASM_STUB(void func_8C050BE4(void))
#endif

ASM_STUB(void func_8C05117E(void))
