#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/collision.h"

/*
 * Landtable entry (SADX _OBJ_LANDENTRY / COL), 0x24 bytes; same layout as
 * the file-local copy in code_8C0522C0.c. File-local until the shared
 * headers carry it.
 */
typedef struct _OBJ_LANDENTRY {
    Float xCenter;              /* 0x00 */
    Float yCenter;              /* 0x04 */
    Float zCenter;              /* 0x08 */
    Float xWidth;               /* 0x0C bounding radius */
    Float yWidth;               /* 0x10 */
    Float zWidth;               /* 0x14 */
    NJS_OBJECT *pObject;        /* 0x18 */
    Sint32 blockbit;            /* 0x1C */
    Uint32 slAttribute;         /* 0x20 */
} _OBJ_LANDENTRY;               /* 0x24 */

SA1_ASSERT_SIZE(_OBJ_LANDENTRY, 0x24);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, pObject, 0x18);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, slAttribute, 0x20);

/*
 * Landtable header (SADX _OBJ_LANDTABLE). Only ssCount (MOV.W @Rn) and
 * pLandEntry (+0x0C) are read here (and by 8C052BA0, "COL list at +0x0C");
 * the fields in between follow SADX and are unverified.
 */
typedef struct _OBJ_LANDTABLE {
    Sint16 ssCount;             /* 0x00 number of entries in pLandEntry */
    Sint16 ssVisibleCount;      /* 0x02 */
    Sint16 ssMotLandEntryCount; /* 0x04 */
    Sint16 ssAttribute;         /* 0x06 */
    Float fFarClipping;         /* 0x08 */
    _OBJ_LANDENTRY *pLandEntry; /* 0x0C */
} _OBJ_LANDTABLE;

SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, pLandEntry, 0x0C);

extern Sint16 B_8C759704;               /* number of entries in B_8C759708 (MOV.W) */
extern _OBJ_LANDCOLL B_8C759708[];      /* registered (dynamic) land collision objects */
extern _OBJ_LANDENTRY B_8C7584F4[];     /* landtable entry copies (max 128), count D_8C19E968 */
extern Sint32 D_8C19E968;
extern Sint32 D_8C19E978;               /* 1 = also take the landtable entries */
extern _OBJ_LANDTABLE *B_8C7596F4;      /* current landtable */
extern Float D_8C19E96C;                /* set by 8C052F34 / 8C052F3A (default 400.0f) */

/*
 * 8C052E30: gather every land collision object, without a range test
 * (8C028EC2 calls it instead of the sphere gathers 8C0522C0/8C052518 when
 * D_8C196044 is set).
 * The registered list B_8C759708 (count B_8C759704) is walked from its last
 * entry: each entry is copied into the source list B_8C757EF4 (count
 * D_8C19E964, max 128) and appended to the per-query list B_8C754EF4 (count
 * B_8C754EF0, max 1024). Then, when D_8C19E978 == 1 and a landtable is set,
 * every landtable entry with an attribute in 0x400003 is copied into
 * B_8C7584F4 (count D_8C19E968, max 128) and appended to B_8C754EF4 as
 * {slAttribute, pObject, pTask = NULL}.
 *
 * NON_MATCHING: complete and functionally faithful (no FMAC in the asm).
 * Same instructions, branches, callee-saved registers and stack slot (the
 * spilled B_8C759708 / B_8C7596F4 temporary at @R15); the only difference
 * is the scratch register choice inside the landtable loop (22 insns): the
 * original uses only R2/R3 there (36-byte copy pairs R2/R3, then the
 * counters alternate R2/R3), SHC here takes R1 for the second word of each
 * copy pair and for the B_8C754EF0 >= 1024 check. Tried: if/else, continue,
 * while/goto shapes, return vs break, a landtable local (drops the stack
 * slot), statement and declaration order, attr/mask locals, block-scoped
 * locals, field-type changes, merged copy pointer.
 * Matched forms found on the way: "obj = src->pObject" before the 12-byte
 * copy reproduces the R0 -> R2 move; separate "col++" / "lent++" statements
 * put both copy pointers in R11; "continue" gives the BRA to the loop test;
 * "dst =; B_8C754EF0 = 0; col =; D_8C19E964 = 0;" gives the pool load order.
 */
#ifdef NON_MATCHING
void func_8C052E30(void)
{
    _OBJ_LANDCOLL *dst;
    _OBJ_LANDCOLL *col;
    _OBJ_LANDCOLL *src;
    _OBJ_LANDENTRY *lent;
    _OBJ_LANDENTRY *ent;
    NJS_OBJECT *obj;
    Sint16 i;

    dst = B_8C754EF4;
    B_8C754EF0 = 0;
    col = B_8C757EF4;
    D_8C19E964 = 0;
    for (src = &B_8C759708[B_8C759704 - 1]; src >= B_8C759708; src--) {
        obj = src->pObject;
        *col = *src;
        col++;
        D_8C19E964++;
        dst->pObject = obj;
        dst->pTask = src->pTask;
        dst->slAttribute = src->slAttribute;
        B_8C754EF0++;
        dst++;
        if (D_8C19E964 >= 128) {
            break;
        }
        if (B_8C754EF0 >= 1024) {
            break;
        }
    }
    lent = B_8C7584F4;
    D_8C19E968 = 0;
    if (D_8C19E978 == 1 && B_8C7596F4 != NULL) {
        ent = B_8C7596F4->pLandEntry;
        for (i = B_8C7596F4->ssCount; i > 0; ent++, i--) {
            if (!(ent->slAttribute & 0x400003)) {
                continue;
            }
            *lent = *ent;
            lent++;
            D_8C19E968++;
            dst->slAttribute = ent->slAttribute;
            dst->pTask = NULL;
            dst->pObject = ent->pObject;
            B_8C754EF0++;
            dst++;
            if (D_8C19E968 >= 128) {
                return;
            }
            if (B_8C754EF0 >= 1024) {
                return;
            }
        }
    }
}
#else
ASM_STUB(void func_8C052E30(void))
#endif

/* 8C052F34: set D_8C19E96C */
void func_8C052F34(Float f)
{
    D_8C19E96C = f;
}

/* 8C052F3A: reset D_8C19E96C to 400.0f (0x43C80000) */
void func_8C052F3A(void)
{
    D_8C19E96C = 400.0f;
}
