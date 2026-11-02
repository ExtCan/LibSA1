/*
 * 1ST_READ chunk 8C051760 (part): func_8C051760, the frame-rate set-up
 * (gsa1_090_charselect.md review R.2 "Frame rate": the menu init 8C900020
 * calls it with 1, stages with 2): B_8C754EBC = B_8C754EC0 = the frame
 * interval in vsyncs, B_8C754EC4 = the logic steps per frame, the step index
 * B_8C754EC8 = 0, the vsync callback 8C051810 is installed through the SDK
 * 8C605234, and 8C055C44 clears B_8C788C34.
 *
 * libsa1: the world tick owns B_8C754EC4 / B_8C754EC8 (one step per tick,
 * docs/portability.md 9) and there is no vsync, so a 'void' policy is the
 * right choice for worlds (LANDING.md); this C is the faithful game code.
 */
#include "sa1/sa1.h"

extern Sint32 B_8C754EBC;       /* frame interval (vsyncs per frame) */
extern Sint32 B_8C754EC0;       /* its copy (UNVERIFIED role) */
extern void func_8C051810(void);                /* the vsync callback (same chunk, asm) */
extern void func_8C605234(void (*cb)(void));    /* SDK: install the vsync callback (UNVERIFIED name) */
extern void func_8C055C44(void);

/* NON_MATCHING: complete and faithful; the original has a dead MOV R4,R3 and loads the four addresses in another order */
#ifdef NON_MATCHING
void func_8C051760(Sint32 interval, Sint32 steps)
{
    B_8C754EBC = interval;
    B_8C754EC0 = interval;
    B_8C754EC4 = steps;
    B_8C754EC8 = 0;
    func_8C605234(func_8C051810);
    func_8C055C44();
}
#else
ASM_STUB(void func_8C051760(Sint32 interval, Sint32 steps))
#endif
