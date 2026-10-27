/*
 * 1ST_READ 8C05DF18-8C05E0CE (chunk 8C05D9C0-8C05E680, air item box
 * O_ItemBoxAir). Two mode steps of objexec_O_ItemBoxAir:
 *
 *  func_8C05DF18  (BSR at 8C05DBA4) the open timer of the box work rises by
 *                 0.05 per frame; past 1.0 the box breaks if a player
 *                 (colliwk id 0/1) hit it: mode 2, break effect, and the
 *                 item handler of the box's item kind is run when the hit
 *                 came from player 0, or the character (GetPlayerNumber /
 *                 func_8C07CE40) allows it; then the lock-on marker is
 *                 released and the collision is replaced by a single shape
 *                 (CCL id 4). Otherwise the collision entry keeps running.
 *  func_8C05E0A0  (8C05E0A0, no symbol: BRA tail call at 8C05DBC8) unk18
 *                 grows by 0.2 per frame up to 5.0, then mode 4.
 *
 * NON_MATCHING only because of the split: the manifest has no function at
 * 8C05E0A0, so shcdiff counts 8C05E0A0-8C05E0CC as part of func_8C05DF18
 * (171 instructions) while SHC emits it as a separate function. Both
 * functions together are byte-identical to 8C05DF18-8C05E0CC (148 + 23
 * instructions, pool values included). Move them out of NON_MATCHING once
 * 8C05E0A0 is added to the split.
 */
#include "sa1/sa1.h"
#include "sa1/protos.h"     /* func_8C07CE40: character id of player pno (code_8C07CE40.c) */

/*
 * Air item box work: 32 bytes from func_8C05DD90 (MAlloc(32) + clear),
 * stored in tp->awp->work[0] by objexec_O_ItemBoxAir and cached in
 * B_8C78C380 every frame.
 */
typedef struct {
    Sint32 unk00;               /* 0x00 */
    Sint32 kind;                /* 0x04 item kind (index into D_8C1B9BA0); from twp->scl.z (8C05DA5E) */
    Sint32 unk08[3];            /* 0x08 unknown */
    Angle rot;                  /* 0x14 += 0x222 per frame (objexec_O_ItemBoxAir 8C05DBAE) */
    Float unk18;                /* 0x18 1.0f at init (8C05DB12); += 0.2f per frame up to 5.0f by func_8C05E0A0 */
    Float open;                 /* 0x1C += 0.05f per frame by func_8C05DF18, clamped to 1.0f */
} ITEMBOXAIR_WK;                /* 0x20 */

SA1_ASSERT_SIZE(ITEMBOXAIR_WK, 0x20);

/* Item kind table (game data; native layout on PC, ripped: the handler is
 * a code pointer remapped by the loader): icon number and pickup handler. */
typedef struct {
    Sint32 tex;                 /* 0x00 read by func_8C05DEA0, 8C05E3E8 */
    void (*func)(taskwk *twp);  /* 0x04 pickup handler, called with the box twp */
} ITEMBOX_ITEM;                 /* 0x08 */

extern ITEMBOXAIR_WK *B_8C78C380;
extern ITEMBOX_ITEM D_8C1B9BA0[];
extern NJS_POINT3 D_8C154C4C;           /* break effect vector */
extern CCL_INFO D_8C1B9C3C[];           /* one shape, CCL id 4 */

extern void func_8C09E3E8(NJS_POINT3 *pos, NJS_POINT3 *vec, Float scl); /* break effect */
extern void func_8C0D5D90(task *tp);     /* release the lock-on marker (code_8C0D5D90.c) */

void func_8C05E0F8(Sint32 kind);
void func_8C05E0A0(task *tp);

#ifdef NON_MATCHING
void func_8C05DF18(task *tp)
{
    taskwk *twp;
    NJS_POINT3 pos;

    twp = tp->twp;
    B_8C78C380->open += 0.049999998f;
    if (B_8C78C380->open > 1.0f) {
        B_8C78C380->open = 1.0f;
        if ((CWP(twp)->flag & 2)
            && (CWP(twp)->hit_cwp->id == 0 || CWP(twp)->hit_cwp->id == 1)) {
            twp->mode = 2;
            pos = twp->pos;
            pos.y += 15.0f;
            func_8C09E3E8(&pos, &D_8C154C4C, 1.19999996f);
            if (D_8C1B9BA0[B_8C78C380->kind].func != NULL) {
                if (CWP(twp)->hit_cwp->mytask == GetPlayerTaskPointer(0)
                    || GetPlayerNumber() == 6 || GetPlayerNumber() == 7
                    || GetPlayerNumber() == 0
                    || (func_8C07CE40(1) == 2 && func_8C07CE40(0) == 0)
                    || (func_8C07CE40(0) == 2 && func_8C07CE40(1) != 0)) {
                    D_8C1B9BA0[B_8C78C380->kind].func(twp);
                    func_8C05E0F8(B_8C78C380->kind);
                }
            }
            func_8C0D5D90(tp);
            func_8C0344A0(twp);
            CCL_Init(tp, D_8C1B9C3C, 4, 1, 1);
        } else {
            func_8C02F63C(twp);
        }
    }
}

/*
 * 8C05E0A0 (no symbol; reached only by the tail call BRA at 8C05DBC8 in
 * objexec_O_ItemBoxAir, so static in the original TU): grows unk18 by 0.2
 * per frame; at 5.0 it is clamped and the box goes to mode 4. Not static
 * here: SHC drops unreferenced statics, and the PC build calls it from
 * objexec_O_ItemBoxAir's file.
 */
void func_8C05E0A0(task *tp)
{
    B_8C78C380->unk18 += 0.19999999f;
    if (!(5.0f > B_8C78C380->unk18)) {
        B_8C78C380->unk18 = 5.0f;
        tp->twp->mode = 4;
    }
}
#else
ASM_STUB(void func_8C05DF18(task *tp))
#endif

ASM_STUB(void func_8C05E0F8(Sint32 kind))
