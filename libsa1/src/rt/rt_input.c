/*
 * rt_input.c - Shinobi pdGetPeripheral 8C601874 (docs/libsa1_integration.md
 * 4.2, 6.6): the peripheral record of a port, built from a host-filled input
 * record.
 *
 * The SDK keeps 24 PDS_PERIPHERAL records at B_8C88E9A4 (4 ports x 6 units:
 * A0..A5, B0..B5, ...; the game asks for 0 / 6 / 12 / 18 = A0..D0, 8C050BE4).
 * On the DC the maple peripheral server 8C601E2A refreshes them every vblank
 * and pdGetPeripheral (a jump to 8C601F5A) only returns &B_8C88E9A4[port]
 * (no bounds check).  The runtime has no maple bus, so it builds the record
 * when the game asks for it, exactly like the reference simulator's pad HLE
 * (SADC/harness/src/t_sim.c, which recorded the reference traces):
 *
 *   connected main unit (A0, B0, C0, D0 with a host controller):
 *       id 1 (standard controller), support 0x000F06FE (digital + analog),
 *       on = held buttons (Katana PDD_DGT_* bits), off = ~on,
 *       press = on & ~old, release = old & ~on, where old = the record's
 *       previous on (also stored in old), r / l / x1 / y1 / x2 / y2 from the
 *       host, name / extend / info NULL;
 *   anything else (no controller, expansion units A1..A5 ...): an all-zero
 *       record (id 0, support 0: 8C050BE4 treats it as not connected).
 *
 * The record lives at the SDK's own address (B_8C88E9A4[port], per world in
 * the arena), as the original returns it; ports >= 24 return the same address
 * arithmetic (32-bit wrap on the DC and the harness) and build nothing.  Each
 * call is one refresh: press / release are relative to the previous call for
 * that unit, as with the HLE.
 *
 * Where the host input comes from:
 *   libsa1 profile   sa1_rt_pad (host data, pclower RT_DATA), which the
 *                    world core points at the ticking world's input latch
 *                    (sa1g_pad pad[4] of the world, the RT_PAD layout below,
 *                    ports A..D, filled from sa1_input; design 6.6) on tick
 *                    entry, like sa1_rt_xmtrx (libsa1/src/world/sa1_world.c);
 *                    NULL = nothing connected.  Replay mode sets it to NULL
 *                    and writes the pad records itself.
 *   harness profile  the harness is the host: its pad HLE answers 8C601F5A,
 *                    the SDK function behind pdGetPeripheral (trace / boot: a
 *                    scripted standard controller on port A, an all-zero
 *                    record elsewhere).  The runtime reads the host input from
 *                    the record the HLE returns and builds its own.  Without
 *                    the HLE (diff on the cold baseline, call) 8C601F5A is
 *                    the original lookup and returns &B_8C88E9A4[port] itself:
 *                    the SDK's maple server owns that record, so it is
 *                    returned untouched, exactly like the original.
 *
 * Verification (README.md): the routed sample traces (every script calls
 * pdGetPeripheral(0/6/12/18) through the native 8C050BE4 or the interpreted
 * one) compare the pad record the game copies (B_8C754C08[0]) and everything
 * that depends on it; diff on the cold baseline checks the address arithmetic;
 * a pad-sequence test behind the simulator's HLE compares every word of every
 * returned record with the HLE's record.  diff --warm compares with the HLE
 * (another record address), so it differs by design.
 */
#include "rt.h"
#include "sa1/input.h"

#define RT_PD_UNITS          24u        /* records at B_8C88E9A4 */
#define RT_PD_UNITS_PER_PORT 6u         /* A0..A5 */
#define RT_PD_ID_CONTROLLER  1u
#define RT_PD_SUPPORT_STD    0x000F06FEu

extern PDS_PERIPHERAL B_8C88E9A4[24];   /* the SDK's peripheral records (Shinobi BSS) */

/* host input of one port (main unit); libsa1/src/world/sa1_world.h sa1g_pad has the same layout */
typedef struct RT_PAD {
    Uint32 connected;                   /* 0: nothing in this port */
    Uint32 on;                          /* held buttons, PDD_DGT_* bits (A 0x4, B 0x2, X 0x400, Y 0x200, START 0x8) */
    Uint16 r, l;                        /* analog triggers */
    Sint16 x1, y1;                      /* main stick, -128..127, y < 0 = forward */
    Sint16 x2, y2;                      /* second stick */
} RT_PAD;

#if defined(TARGET_PC_HARNESS)
extern const void *func_8C601F5A(Uint32 port);   /* Shinobi: the record of a unit (harness: the pad HLE) */
#else
extern const RT_PAD *sa1_rt_pad;        /* world core: the ticking world's RT_PAD[4] (ports A..D), or NULL */
#endif

/* one refresh of a record from the host input */
static void rt_pd_build(PDS_PERIPHERAL *per, const RT_PAD *pad)
{
    Uint32 old = per->on;

    if (!pad->connected) {
        per->id = 0;
        per->support = 0;
        per->on = 0;
        per->off = 0;
        per->press = 0;
        per->release = 0;
        per->r = 0;
        per->l = 0;
        per->x1 = 0;
        per->y1 = 0;
        per->x2 = 0;
        per->y2 = 0;
        per->name = NULL;
        per->extend = NULL;
        per->old = 0;
        per->info = NULL;
        return;
    }
    per->id = RT_PD_ID_CONTROLLER;
    per->support = RT_PD_SUPPORT_STD;
    per->on = pad->on;
    per->off = ~pad->on;
    per->press = pad->on & ~old;
    per->release = old & ~pad->on;
    per->r = pad->r;
    per->l = pad->l;
    per->x1 = pad->x1;
    per->y1 = pad->y1;
    per->x2 = pad->x2;
    per->y2 = pad->y2;
    per->name = NULL;
    per->extend = NULL;
    per->old = old;
    per->info = NULL;
}

/* 8C601874 pdGetPeripheral */
const void *pdGetPeripheral(Uint32 port)
{
    PDS_PERIPHERAL *per = B_8C88E9A4 + port;
    RT_PAD pad;

    if (port >= RT_PD_UNITS) {
        return per;
    }
#if defined(TARGET_PC_HARNESS)
    {
        const PDS_PERIPHERAL *src = (const PDS_PERIPHERAL *)func_8C601F5A(port);

        if (src == per) {
            return per;
        }
        pad.connected = src->id != 0;
        pad.on = src->on;
        pad.r = src->r;
        pad.l = src->l;
        pad.x1 = src->x1;
        pad.y1 = src->y1;
        pad.x2 = src->x2;
        pad.y2 = src->y2;
    }
#else
    if (sa1_rt_pad != NULL && port % RT_PD_UNITS_PER_PORT == 0) {
        pad = sa1_rt_pad[port / RT_PD_UNITS_PER_PORT];
    } else {
        pad.connected = 0;
    }
#endif
    rt_pd_build(per, &pad);
    return per;
}
