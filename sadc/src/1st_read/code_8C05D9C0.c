/*
 * Air item box O ItemBoxAir (1ST_READ 8C05D9C0-8C05DC2E).
 * Chunk asm/1st_read/text/8C05D9C0.src.
 *
 *  func_8C05D9C0         spawn an air item box at pos with item kind item
 *                        (called by 8C0C8534 with a random item)
 *  objexec_O_ItemBoxAir  exec: item kind in twp->scl.x (0..8), work block in
 *                        tp->awp work[0] (mirrored in B_8C78C380), modes
 *                        0 init, 1 idle (spin), 2 scale-up after the hit,
 *                        3 display only, 4 remove
 *  func_8C05DC0C         dest: free the work block
 *
 * Two of the exec's tail calls (BRA) go to functions that have no symbol in
 * the split: 8C05E0A0 (inside func_8C05DF18's range: work.scl += 0.2 up to a
 * limit, then mode 4) and 8C05E612 (inside func_8C05E5C2's range: debug
 * print "-> ITEM NUMBER " and the item name). They are ASM_STUBs here under
 * the names func_8C05E0A0 / func_8C05E612; the split needs symbols at those
 * addresses before the exec can be compared or spliced:
 *   func_8C05E0A0(tp): B_8C78C380->scl += 0.2f; if (!(5.0f > scl))
 *                      { scl = 5.0f; tp->twp->mode = 4; }
 *   func_8C05E612(tp): njPrintC(0x00110013, D_8C154C58 "-> ITEM NUMBER ");
 *                      njPrintC(0x00140014, D_8C1B9C8C[(Sint32)twp->scl.x]);
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * Air item box work block: MAlloc(32) and zeroed by func_8C05DD90, kept in
 * AWP(tp)->work[0].ptr and copied to B_8C78C380 every frame.
 */
typedef struct {
    Sint32 unk00;               /* 0x00 */
    Sint32 item;                /* 0x04 item kind ((Uint32)twp->scl.x); index of the 8-byte table D_8C1B9BA4 (func_8C05DF18) */
    Sint32 unk08[3];            /* 0x08 */
    Sint32 ang;                 /* 0x14 spin angle, += 0x222 per frame in mode 1 */
    Float scl;                  /* 0x18 box scale: 1.0 at init, += 0.2 per frame in mode 2 */
    Float scl2;                 /* 0x1C += 0.05 up to 1.0 (func_8C05DF18); drawn scale in modes 0/1 (func_8C05DC60) */
} ITEMBOXAIR_WK;                /* 0x20 */

SA1_ASSERT_SIZE(ITEMBOXAIR_WK, 0x20);
SA1_ASSERT_OFFSET(ITEMBOXAIR_WK, item, 0x04);
SA1_ASSERT_OFFSET(ITEMBOXAIR_WK, ang, 0x14);
SA1_ASSERT_OFFSET(ITEMBOXAIR_WK, scl, 0x18);
SA1_ASSERT_OFFSET(ITEMBOXAIR_WK, scl2, 0x1C);

extern ITEMBOXAIR_WK *B_8C78C380;       /* current air item box work */
extern CCL_INFO D_8C1B9C08[];           /* collision shape of the air item box (CCL_Init id 2, n 1) */

void func_8C091B5E(task *tp);           /* ocp->ssCondition |= 0x10 (code_8C091B5E.c) */
void func_8C091BA4(task *tp);           /* SET object removal (code_8C091B8A.c) */

void objexec_O_ItemBoxAir(task *tp);
void func_8C05DC0C(task *tp);
void func_8C05DC60(task *tp);
Uint8 *func_8C05DD90(void);
void func_8C05DF18(task *tp);
void func_8C05E0A0(task *tp);
void func_8C05E612(task *tp);

void func_8C05D9C0(NJS_POINT3 *pos, Sint32 item)
{
    task *tp;
    taskwk *twp;

    tp = CreateElementalTask(15, 3, objexec_O_ItemBoxAir);
    twp = tp->twp;
    twp->pos = *pos;
    twp->scl.x = (Float)item;
}

#ifdef NON_MATCHING
/*
 * Exec of the air item box.
 * NON_MATCHING (C complete and functionally faithful): every instruction
 * matches except
 *  - the two tail calls to 8C05E0A0 and 8C05E612 (BRA to unnamed labels in
 *    the split; shcdiff cannot resolve func_8C05E0A0/func_8C05E612 until the
 *    symbols exist, see the file header);
 *  - case 0's "if (tp->ocp)" test: the original uses the far form
 *    BF +6 / BRA end / NOP at 8C05DB30, SHC gives a short BT end here (2
 *    instructions less; all later branch targets are shifted by 2).
 *    SHC's branch-range estimate here is 7..12 bytes short of the original's
 *    (padding case 3 by 12 bytes flips it). Not changed by: return/break
 *    forms of every case, && vs nested ifs, == / != NULL spellings, static
 *    callees, extra stubs for the rest of the chunk, case 4/default order.
 * Matching notes: the "B_8C78C380 = ...; if (B_8C78C380 != NULL)" store
 * then test (MOV R2,R3 copy), "!= 0" / "== 0" on twp->mode (register
 * choice), item as Sint32 with an explicit (Uint32) cast (the FADD FR2,FR2 /
 * FSUB unsigned-conversion template) and the switch in the else branch of
 * the debug test (MOV #1,R2 in case 0) are all needed.
 */
void objexec_O_ItemBoxAir(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (func_8C0919CC(tp)) {
        return;
    }
    B_8C78C380 = AWP(tp)->work[0].ptr;
    if (B_8C78C380 != NULL) {
        if (SA1_FTOI(twp->scl.x) < 0) {
            twp->scl.x = 8.0f;
        } else if (SA1_FTOI(twp->scl.x) >= 9) {
            twp->scl.x = 0.0f;
        }
        if (twp->mode != 0) {
            /* in range: the clamp above leaves -1 < scl.x < 9 (NaN gives 8.0) */
            B_8C78C380->item = (Uint32)twp->scl.x;
        }
    }
    if (func_8C042DC4(tp)) {
        if (twp->mode == 0) {
            twp->mode++;
            AWP(tp)->work[0].ptr = func_8C05DD90();
            tp->dest = func_8C05DC0C;
            tp->disp = func_8C05DC60;
        }
        B_8C78C380 = AWP(tp)->work[0].ptr;
        func_8C05DC60(tp);
        func_8C05E612(tp);
    } else {
        switch (twp->mode) {
        case 0:
            twp->mode = 1;
            AWP(tp)->work[0].ptr = func_8C05DD90();
            if (tp->mwp) {
                MFree(tp->mwp);
            }
            /* 0x23C-byte block of unknown type in place of the motionwk (objexec_O_ITEMBOX does the same) */
            tp->mwp = (motionwk *)CAlloc(1, 0x23C);
            B_8C78C380 = AWP(tp)->work[0].ptr;
            tp->dest = func_8C05DC0C;
            tp->disp = func_8C05DC60;
            CCL_Init(tp, D_8C1B9C08, 2, 1, 1);
            B_8C78C380->scl = 1.0f;
            if ((GetStageNumber() & 0xFF00) != 0x2300) {
                if (tp->ocp) {
                    func_8C091B5E(tp);
                }
            }
            break;
        case 1:
            func_8C05DC60(tp);
            func_8C05DF18(tp);
            B_8C78C380->ang += 0x222;
            break;
        case 2:
            func_8C05DC60(tp);
            func_8C05E0A0(tp);
            break;
        case 3:
            func_8C05DC60(tp);
            break;
        case 4:
        default:
            if (tp->ocp) {
                func_8C091BA4(tp);
            } else {
                FreeTask(tp);
            }
            break;
        }
    }
}
#else
ASM_STUB(void objexec_O_ItemBoxAir(task *tp))
#endif

void func_8C05DC0C(task *tp)
{
    if (AWP(tp)->work[0].ptr) {
        MFree(AWP(tp)->work[0].ptr);
        AWP(tp)->work[0].ptr = NULL;
    }
}

ASM_STUB(void func_8C05DC60(task *tp))
ASM_STUB(Uint8 *func_8C05DD90(void))
ASM_STUB(void func_8C05DF18(task *tp))
ASM_STUB(void func_8C05E0A0(task *tp))
ASM_STUB(void func_8C05E612(task *tp))