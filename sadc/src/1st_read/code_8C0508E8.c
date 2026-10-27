#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0508E8-8C05096E (chunk 8C0502D8-8C0509A0, functions 11-12):
 * the single-sprite effect task of the effect system 8C04FEA0-8C050A00.
 *   func_8C0508E8  exec: update + user callback, frees the task when the
 *                  callback returns -1
 *   func_8C050914  creates the task (caller: E-102 hit flash 8C0D6AA0)
 */

struct eff_spranim;             /* EFF_SPRANIM (sprite list, code_8C0D6960.c) */
struct eff_sprwk;

typedef Sint32 (*EFF_SPRFUNC)(struct eff_sprwk *wp);

/*
 * Parameters of 8C050914 (EFF_SPRPARAM in code_8C0D6960.c), copied by
 * 8C050400 into the work below.
 */
typedef struct {
    EFF_SPRFUNC func;           /* 0x00 -> work+0x00; non-NULL: the task gets the exec 8C0508E8 */
    NJS_POINT3 ofs;             /* 0x04 -> work+0x14 (8C04FEA0) */
    Float unk10;                /* 0x10 -> work+0x20 */
    Float scl;                  /* 0x14 -> work+0x24 */
    Float unk18;                /* 0x18 -> work+0x28 */
    Float unk1C;                /* 0x1C -> work+0x2C */
    struct eff_spranim *anim;   /* 0x20 -> work+0x30 */
    Float unk24;                /* 0x24 -> work+0x38 */
} EFF_SPRPARAM;                 /* 0x28 */

/*
 * Task work (task.awp, MAlloc(0x44) by 8C050914, filled by 8C050400).
 */
typedef struct eff_sprwk {
    EFF_SPRFUNC func;           /* 0x00 called by the exec 8C0508E8; -1 frees the task */
    NJS_POINT3 *posp;           /* 0x04 flag bit 0: &pos, else the caller's pos pointer (8C050400) */
    NJS_POINT3 pos;             /* 0x08 8C04FEA0: copy of the caller's pos (or 0) */
    NJS_POINT3 ofs;             /* 0x14 8C04FEA0: prm->ofs (or 0) */
    Float unk20;                /* 0x20 prm->unk10 (8C04FEA0 sets 1.0f first) */
    Float scl;                  /* 0x24 prm->scl */
    Float unk28;                /* 0x28 prm->unk18; 8C0504DA passes &unk28 to 8C04FEF6 */
    Float unk2C;                /* 0x2C prm->unk1C */
    struct eff_spranim *anim;   /* 0x30 prm->anim */
    Float frame;                /* 0x34 8C0503B6: 0.0f (0x34-0x43 is one 0x10 block; names guessed) */
    Float speed;                /* 0x38 8C0503B6: 1.0f, then prm->unk24 */
    Sint32 unk3C;               /* 0x3C 8C0503B6: 0 */
    Sint32 unk40;               /* 0x40 8C0503B6: 0 */
} EFF_SPRWK;                    /* 0x44 */

SA1_ASSERT_SIZE(EFF_SPRPARAM, 0x28);
SA1_ASSERT_OFFSET(EFF_SPRPARAM, anim, 0x20);
SA1_ASSERT_SIZE(EFF_SPRWK, 0x44);
SA1_ASSERT_OFFSET(EFF_SPRWK, anim, 0x30);
SA1_ASSERT_OFFSET(EFF_SPRWK, frame, 0x34);

ASM_STUB(void func_8C050400(EFF_SPRWK *wp, NJS_POINT3 *pos, EFF_SPRPARAM *prm, Sint32 flag))
ASM_STUB(void func_8C050862(EFF_SPRWK *wp))
void func_8C05088E(task *tp);
void func_8C050892(task *tp);

/*
 * C copy of the disp func_8C05088E (8C05088E: BRA 8C050862 with tp->awp).
 * The exec 8C0508E8 (and 8C050892) call it and SHC -speed inlined it there:
 * that is why the asm loads tp->awp twice (once for wp, once as the BSR
 * argument). The original most likely called func_8C05088E itself, which is
 * defined in this translation unit but is not part of this file.
 */
static void eff_sprdisp(task *tp)
{
    EFF_SPRWK *wp = (EFF_SPRWK *)tp->awp;

    func_8C050862(wp);
}

void func_8C0508E8(task *tp)
{
    EFF_SPRWK *wp = (EFF_SPRWK *)tp->awp;

    eff_sprdisp(tp);
    if (wp->func(wp) == -1) {
        FreeTask(tp);
    }
}

task *func_8C050914(NJS_POINT3 *pos, EFF_SPRPARAM *prm, Sint32 flag)
{
    task *tp;
    EFF_SPRWK *wp;

    if ((tp = CreateElementalTask(2, 4, func_8C050892)) != NULL) {
        wp = (EFF_SPRWK *)MAlloc(sizeof(EFF_SPRWK));
        if (wp != NULL) {
            tp->awp = wp;
            func_8C050400(wp, pos, prm, flag);
            if (wp->func != NULL) {
                tp->exec = func_8C0508E8;
            }
            tp->disp = func_8C05088E;
        } else {
            DestroyTask(tp);
            tp = NULL;
        }
    }
    return tp;
}
