/*
 * B_CHAOS7 0C900480-0C900520 (chunk asm/overlays/b_chaos7/text/0C900480.src,
 * functions 1-5 of 5): the Super Sonic task wrappers.
 *
 *  func_0C900480  [SADX Sonic_SuperAura_Load] initial exec of the aura task
 *                 (CreateElementalTask(2, 2, ...) on request 46,
 *                 code_8C0AFF1A.c:408 / code_8C0B07E4.c:296 SUPERSONIC_EXEC):
 *                 installs exec 0C91D486
 *                 [Sonic_SuperAura_Main], disp 0C91D222
 *                 [Sonic_SuperAura_Display] and dest 0C90049C, draws once,
 *                 then resets the whoosh state byte 0x0CA6C310 to 0.
 *  func_0C90049C  [SADX Sonic_SuperAura_Delete] dest: stops the whoosh
 *                 tones 1035 and 1036 (0x40B / 0x40C).
 *  func_0C9004B6  initial exec of the jump-ball after-image task (created by
 *                 func_8C0B2D8C, the Super Sonic twin of func_8C0B2D66,
 *                 code_8C0B2D40.c); super variant of func_8C0B3FC0
 *                 (code_8C0B3B1A.c): set-up
 *                 0C91DB60 (= 8C0B41E0), exec 0C91DE82 (= 8C0B46CC),
 *                 disp 0C91DDB6 (= 8C0B4600), draw once (tail call).
 *  func_0C9004D4  initial exec of the homing start ball (created by
 *                 func_8C0B2DD8, the twin of func_8C0B2DB2); super variant
 *                 of func_8C0B3FDA: set-up 0C91DCB2
 *                 (= 8C0B4332), exec 0C91DFE0 (= 8C0B4820), disp 0C91DED2
 *                 (= 8C0B4724), draw once (tail call).
 *  func_0C9004F2  initial exec of the trail ("dash") task (created by
 *                 func_8C0B2CB2, the twin of func_8C0B2C8C,
 *                 code_8C0B2890.c); super variant of func_8C0B48B4
 *                 (code_8C0B4600.c): exec/disp/dest = 0C91E26A / 0C91E1D2 /
 *                 0C91E5E2 (= 8C0B4B20 / 8C0B4A48 / 8C0B4E62) when the
 *                 set-up 0C91E06A (= 8C0B48E0) returns 1, otherwise
 *                 FreeTask.
 *
 * The callees live in other chunks of this image (JSR / JMP through the
 * pool), so they are extern prototypes here, with the signatures of their
 * 1ST_READ originals (super_sonic.md 1.9, clone table).
 *
 * B_0CA6C310 lies past the end of the B_CHAOS7 image (0x0CA6B454), in the
 * overlay's BSS, which the split does not label: the pool holds the plain
 * number H'0CA6C310. As in code_0C911DC0.c (STG05), under SHC it is spelled
 * as that address so that the pool entry compares equal; on PC it is an
 * ordinary variable. The aura exec 0C91D486 reads it with MOV.B (signed)
 * and switches on 0 / 1 / 2 (the whoosh state of super_sonic.md 1.9).
 */
#include "common.h"
#include "sa1/sa1.h"

/* B_CHAOS7 BSS: whoosh sound state of the aura (see above) */
#if defined(TARGET_PC)
extern Sint8 B_0CA6C310;
#else
#define B_0CA6C310 (*(Sint8 *)0x0CA6C310)
#endif

/* B_CHAOS7 functions of other chunks (0C91D200.src, 0C91DB60.src) */
extern void func_0C91D222(task *tp);            /* aura disp  [Sonic_SuperAura_Display] */
extern void func_0C91D486(task *tp);            /* aura exec  [Sonic_SuperAura_Main] */
extern void func_0C91DB60(taskwk *twp);         /* = func_8C0B41E0 */
extern void func_0C91DCB2(taskwk *twp);         /* = func_8C0B4332 */
extern void func_0C91DDB6(task *tp);            /* = func_8C0B4600 (disp) */
extern void func_0C91DE82(task *tp);            /* = func_8C0B46CC (exec) */
extern void func_0C91DED2(task *tp);            /* = func_8C0B4724 (disp) */
extern void func_0C91DFE0(task *tp);            /* = func_8C0B4820 (exec) */
extern Sint32 func_0C91E06A(taskwk *twp);       /* = func_8C0B48E0 (set-up, 1 = ok) */
extern void func_0C91E1D2(task *tp);            /* = func_8C0B4A48 (disp) */
extern void func_0C91E26A(task *tp);            /* = func_8C0B4B20 (exec) */
extern void func_0C91E5E2(task *tp);            /* = func_8C0B4E62 (dest) */

void func_0C90049C(task *tp);

/* [SADX Sonic_SuperAura_Load] */
void func_0C900480(task *tp)
{
    tp->exec = func_0C91D486;
    tp->disp = func_0C91D222;
    tp->dest = func_0C90049C;
    func_0C91D222(tp);
    B_0CA6C310 = 0;
}

/* [SADX Sonic_SuperAura_Delete] */
void func_0C90049C(task *tp)
{
    func_8C0945A0(0x40B);
    func_8C0945A0(0x40C);
}

void func_0C9004B6(task *tp)
{
    func_0C91DB60(tp->twp);
    tp->exec = func_0C91DE82;
    tp->disp = func_0C91DDB6;
    func_0C91DDB6(tp);
}

void func_0C9004D4(task *tp)
{
    func_0C91DCB2(tp->twp);
    tp->exec = func_0C91DFE0;
    tp->disp = func_0C91DED2;
    func_0C91DED2(tp);
}

void func_0C9004F2(task *tp)
{
    if (func_0C91E06A(tp->twp) == 1) {
        tp->exec = func_0C91E26A;
        tp->disp = func_0C91E1D2;
        tp->dest = func_0C91E5E2;
    } else {
        FreeTask(tp);
    }
}
