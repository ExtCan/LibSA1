/*
 * CCL (object collision) management (1ST_READ 8C02F27C-8C02F81A):
 * shape debug draw dispatch, range sphere debug draw, range computation,
 * CCL_Init and the per-frame entry CCL_Entry (func_8C02F63C).
 */

#include "sa1/sa1.h"

#ifdef TARGET_PC
/*
 * libsa1 (LIB/src/world/sa1_world.h): game-detected fatal state. Used in
 * CCL_Init's endless "CANT GET COLLI_WORK" loop, which on the PC would hang
 * the host's thread (docs/libsa1_interaction.md 2.8 and 4.1 request 1).
 */
void sa1g_fatal(Uint32 dc_func, const char *what);
#endif

/* per-id entry lists filled by func_8C02F63C (BSS; counts are MOV.W/EXTU.W) */
extern Uint16 B_8C7456D8;               /* id 0 count (max 8) */
extern Uint16 B_8C7456DA;               /* id 1 count (max 128) */
extern Uint16 B_8C7456DC;               /* id 4 count (max 512) */
extern Uint16 B_8C7456DE;               /* id 5 count (max 128) */
extern Uint16 B_8C7456E0;               /* id 6 count (max 256) */
extern Uint16 B_8C7456E2;               /* id 7 count (max 256) */
extern Uint16 B_8C7456E4;               /* id 8 count (max 256) */
extern Uint16 B_8C752BD8;               /* id 2 count (max 128) */
extern Uint16 B_8C752BDA;               /* id 3 count (max 256) */
extern colliwk *B_8C7456E8[];           /* id 0 list [8] */
extern colliwk *B_8C745708[];           /* id 1 list [128] */
extern colliwk *B_8C745908[];           /* id 2 list [128] */
extern colliwk *B_8C745B08[];           /* id 3 list [256] */
extern colliwk *B_8C745F08[];           /* id 4 list [512] */
extern colliwk *B_8C746708[];           /* id 5 list [128] */
extern colliwk *B_8C746908[];           /* id 6 list [256] */
extern colliwk *B_8C746D08[];           /* id 7 list [256] */
extern colliwk *B_8C747108[];           /* id 8 list [256] */

/*
 * Shape debug draws (code_8C02E820.c, code_8C02EC92.c), reached with BSR.
 *
 * Literal pool layout. The original translation unit is the whole chunk
 * 8C02E820-8C0318C0, and where SHC dumps the shared pool changes the branch
 * forms: the pool of func_8C02F0A0 sits in the middle of func_8C02F27C
 * (8C02F2BC, after the BRA of the compare chain), which gives "BT/S body;
 * BRA end" for the attr & 0x100 test; the pool of func_8C02F400/CCL_Init is
 * dumped before func_8C02F5E0 (8C02F5A0), so that the pool of
 * func_8C02F5E0/func_8C02F63C lands after case 3 of func_8C02F63C
 * (8C02F740) and cases 4-8 branch to the end with a plain BT. The stubs
 * below are spread over the file (2 before func_8C02F27C, 2 after CCL_Init,
 * 3 after func_8C02F5E0, 1 at the end) so that their own pool entry
 * (__asm_stub_sink) reproduces those dumps. The splicer ignores the stub
 * bodies and keeps the original pools, so their position does not matter
 * otherwise.
 *
 * func_8C02F0A0 is called with three arguments (R6 = pos is set up at
 * 8C02F326), as code_8C02EC92.c defines it.
 */
void func_8C02E886(CCL_INFO *info, NJS_POINT3 *pos);
void func_8C02E8DA(CCL_INFO *info, NJS_POINT3 *pos);
void func_8C02E980(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
void func_8C02EAD2(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
void func_8C02EBCE(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
void func_8C02EC92(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
void func_8C02EE9E(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
void func_8C02F0A0(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos);
ASM_STUB(void func_8C02E886(CCL_INFO *info, NJS_POINT3 *pos))
ASM_STUB(void func_8C02E8DA(CCL_INFO *info, NJS_POINT3 *pos))

/* debug draw of one shape (once per frame: attr 0x100) */
void func_8C02F27C(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos)
{
    if (info->attr & 0x100) {
        return;
    }
    switch (info->attr & 0xF) {
    case 0:
        func_8C02E886(info, pos);
        break;
    case 1:
        func_8C02E8DA(info, pos);
        break;
    case 2:
        func_8C02E980(twp, info, pos);
        break;
    case 3:
        func_8C02EC92(twp, info, pos);
        break;
    case 4:
        func_8C02EAD2(twp, info, pos);
        break;
    case 5:
        func_8C02EBCE(twp, info, pos);
        break;
    case 6:
        func_8C02EE9E(twp, info, pos);
        break;
    case 7:
        func_8C02F0A0(twp, info, pos);
        break;
    }
    info->attr |= 0x100;
}

/* debug draw of the colliwk range sphere, scale range / 10 (no callers) */
void func_8C02F33E(colliwk *cwp)
{
    taskwk *twp;
    Float r;

    twp = cwp->mytask->twp;
    r = cwp->range * 0.099999996f;
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    njScale(NULL, r, r, r);
    func_8C043A20(-0.5f, 0.5f, 0.5f, 0.0f);
    func_8C6215D6(&Debug_Sphere);
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}

/* cwp->range = 1.01 * max over the shapes of |center| + extent */
void func_8C02F400(colliwk *cwp)
{
    CCL_INFO *top;
    CCL_INFO *info;
    CCL_INFO *end;
    Float max;
    Float r;
    Float a;
    Float b;
    Float c;

    top = cwp->info;
    end = &top[cwp->nbInfo];
    max = 0.0f;
    for (info = top; info < end; info++) {
        a = info->a;
        b = info->b;
        c = info->c;
        if ((info->attr & 0xF) == 3) {
            r = njScalor(&info->center) + a + b;
        } else {
            /*
             * The SH-4 computes SA1_FMAC(c, c, SA1_FMAC(a, a, b * b)) (FMUL b*b,
             * FMAC a*a, FMAC c*c). SHC emits that order only for the
             * left-nested sum: it rotates c*c + (a*a + b*b), the SA1_FMAC
             * spelling, into (c*c + a*a) + b*b. Hence the matching-only
             * idiom below; the PC gets the fused form of the original.
             */
            r = njScalor(&info->center) +
                njSqrt(SH_ONLY(a * a + b * b + c * c) PC_ONLY(SA1_FMAC(c, c, SA1_FMAC(a, a, b * b))));
        }
        if (r > max) {
            max = r;
        }
    }
    cwp->range = max * 1.01f;
    if (max < 0.099999996f) {
        cwp->range = 25.0f;
    }
}

/*
 * CCL_Init: allocate the colliwk of tp and a copy of its shape table.
 * id and nbInfo are Uint8 parameters: R6 is spilled with MOV.B and reloaded
 * with MOV.B + EXTU.B, and the fifth (stack) argument is read with MOV.B
 * @(32,R15) (Sint32 parameters with casts or Uint8 locals do not match).
 * sa1/ccl.h still declares them Sint32, hence the rename around the include.
 */
void CCL_Init(task *tp, CCL_INFO *info, Uint8 id, Sint32 unused, Uint8 nbInfo)
{
    colliwk *cwp;
    CCL_INFO *p;

    if ((cwp = CAlloc(1, sizeof(colliwk))) == NULL) {
        for (;;) {
            /* the original spins here forever (no colliwk); libsa1 stops the world */
            PC_ONLY(sa1g_fatal(0x8C02F4B0, "CCL_Init: CAlloc failed");)
            if (tcrf_CollisionDisplayVar >= 0) {
                njPrintC(0x001E0028, "CANT GET COLLI_WORK!!!!");
            }
        }
    } else {
        if ((p = CAlloc(nbInfo, sizeof(CCL_INFO))) != NULL) {
            func_8C604500(p, info, nbInfo * sizeof(CCL_INFO));
            cwp->info = p;
            cwp->flag |= 0x20;
            cwp->id = id;
            if (id == 2 || id == 3 || id == 7) {
                cwp->flag |= 0x800;
            }
            cwp->nbInfo = nbInfo;
            cwp->mytask = tp;
            cwp->flag &= 0xFFFD;
            tp->twp->cwp = cwp;
            if (id == 0 || id == 1) {
                cwp->range = 25.0f;
            } else {
                func_8C02F400(cwp);
            }
        }
    }
}

/* pool layout: see the note on the shape debug draw stubs at the top */
ASM_STUB(void func_8C02E980(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos))
ASM_STUB(void func_8C02EAD2(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos))

/* per-frame reset of the hit state */
void func_8C02F5E0(taskwk *twp, colliwk *cwp)
{
    CCL_INFO *info;
    Uint16 n;

    n = cwp->nbInfo;
    info = cwp->info;
    cwp->flag &= 0x7FFF;
    cwp->my_num = -1;
    cwp->hit_num = -1;
    cwp->hit_cwp = NULL;
    while (n--) {
        info->hit_info = -1;
        info->hit_cwp = NULL;
        info->attr &= ~0x100;
        info->flag &= ~0x10;
        info++;
    }
    twp->flag &= ~0x3C;
    cwp->flag &= 0xBF21;
}

/* pool layout: see the note on the shape debug draw stubs at the top */
ASM_STUB(void func_8C02EBCE(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos))
ASM_STUB(void func_8C02EC92(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos))
ASM_STUB(void func_8C02EE9E(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos))

/*
 * CCL_Entry: reset the hit state of the colliwk and enter it in the list of
 * its id (once per frame, from the object's exec).
 * Matching: the cases end with break and the switch with an explicit return
 * (SHC duplicates the epilogue into every case); "return" in the cases
 * alternates the scratch registers R2/R3, "break" alone leaves BRA end.
 */
void func_8C02F63C(taskwk *twp)
{
    colliwk *cwp;

    cwp = CWP(twp);
    if (D_8C19B468 == 0 && cwp != NULL && cwp->mytask->exec != DestroyTask) {
        func_8C02F5E0(twp, cwp);
        switch (cwp->id) {
        case 0:
            if (B_8C7456D8 < 8) {
                B_8C7456E8[B_8C7456D8] = cwp;
                B_8C7456D8++;
            }
            break;
        case 1:
            if (B_8C7456DA < 128) {
                B_8C745708[B_8C7456DA] = cwp;
                B_8C7456DA++;
            }
            break;
        case 2:
            if (B_8C752BD8 < 128) {
                B_8C745908[B_8C752BD8] = cwp;
                B_8C752BD8++;
            }
            break;
        case 3:
            if (B_8C752BDA < 256) {
                B_8C745B08[B_8C752BDA] = cwp;
                B_8C752BDA++;
            }
            break;
        case 4:
            if (B_8C7456DC < 512) {
                B_8C745F08[B_8C7456DC] = cwp;
                B_8C7456DC++;
            }
            break;
        case 5:
            if (B_8C7456DE < 128) {
                B_8C746708[B_8C7456DE] = cwp;
                B_8C7456DE++;
            }
            break;
        case 6:
            if (B_8C7456E0 < 256) {
                B_8C746908[B_8C7456E0] = cwp;
                B_8C7456E0++;
            }
            break;
        case 7:
            if (B_8C7456E2 < 256) {
                B_8C746D08[B_8C7456E2] = cwp;
                B_8C7456E2++;
            }
            break;
        case 8:
            if (B_8C7456E4 < 256) {
                B_8C747108[B_8C7456E4] = cwp;
                B_8C7456E4++;
            }
            break;
        default:
            return;
        }
        return;
    }
}

/* pool layout: see the note on the shape debug draw stubs at the top */
ASM_STUB(void func_8C02F0A0(taskwk *twp, CCL_INFO *info, NJS_POINT3 *pos))
