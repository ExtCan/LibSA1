/*
 * Landtable manager (1ST_READ 8C0519C0 chunk).
 * 8C0519C0: the landtable manager task (exec). Mode 0 resets the manager
 * state (8C051A16), publishes its taskwk in B_8C7596F8, sets disp/dest and
 * falls into mode 2, which selects the current landtable (8C051CC0) and
 * switches to mode 1; modes 0-2 then run the per-frame update 8C051D50
 * (tail call), any other mode is reset to 0.
 * 8C051A16: clears the manager globals (also run by the dest 8C051D38).
 * 8C051A52: loads landtable D_8C19E9FC[level][act]: with ssAttribute 0x10
 * the binary file pBinaryFileName is loaded once (ssLoadFlag bits 2-3 count
 * the users) and the object/motion pointers of the entries, stored as
 * offsets into it, are relocated; then the textures are loaded once
 * (ssLoadFlag bits 0-1): the landtable texlist (0x2), the PVM file
 * pPvmFileName (0x8) or the texlists of the motion entries (0x1).
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_funcs.h"
#include "sa1/collision.h"

/*
 * Landtable entry (SADX _OBJ_LANDENTRY / COL), 0x24 bytes. Same layout as
 * the file-local copies in code_8C0522C0.c, code_8C0520C8.c and
 * code_8C052E30.c; file-local until the shared headers carry it.
 */
typedef struct _OBJ_LANDENTRY {
    Float xCenter;              /* 0x00 */
    Float yCenter;              /* 0x04 */
    Float zCenter;              /* 0x08 */
    Float xWidth;               /* 0x0C bounding radius */
    Float yWidth;               /* 0x10 */
    Float zWidth;               /* 0x14 */
    NJS_OBJECT *pObject;        /* 0x18 offset into the binary file until 8C051A52 relocates it */
    Sint32 blockbit;            /* 0x1C */
    Uint32 slAttribute;         /* 0x20 */
} _OBJ_LANDENTRY;               /* 0x24 */

SA1_ASSERT_SIZE(_OBJ_LANDENTRY, 0x24);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, pObject, 0x18);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, slAttribute, 0x20);

/*
 * Animated landtable entry (SADX _OBJ_MOTLANDENTRY / GeoAnimData), 0x18
 * bytes (stride 24 in 8C051A52). pObject and pAction are offsets into the
 * binary file until 8C051A52 relocates them. File-local.
 */
typedef struct _OBJ_MOTLANDENTRY {
    Float fFrame;               /* 0x00 */
    Float fStep;                /* 0x04 */
    Float fMaxFrame;            /* 0x08 */
    NJS_OBJECT *pObject;        /* 0x0C */
    NJS_ACTION *pAction;        /* 0x10 */
    NJS_TEXLIST *pTexList;      /* 0x14 loaded by 8C051A52 (ssAttribute & 1) */
} _OBJ_MOTLANDENTRY;            /* 0x18 */

SA1_ASSERT_SIZE(_OBJ_MOTLANDENTRY, 0x18);
SA1_ASSERT_OFFSET(_OBJ_MOTLANDENTRY, pObject, 0x0C);
SA1_ASSERT_OFFSET(_OBJ_MOTLANDENTRY, pAction, 0x10);
SA1_ASSERT_OFFSET(_OBJ_MOTLANDENTRY, pTexList, 0x14);

/*
 * Landtable header (SADX _OBJ_LANDTABLE), 0x24 bytes. File-local until the
 * shared headers carry it.
 */
typedef struct _OBJ_LANDTABLE {
    Sint16 ssCount;             /* 0x00 number of entries in pLandEntry */
    Sint16 ssMotCount;          /* 0x02 number of entries in pMotLandEntry */
    Sint16 ssAttribute;         /* 0x04 0x10 binary file, 0x8 PVM file, 0x4 fFarClipping, 0x2 texlist, 0x1 motion entries */
    Sint16 ssLoadFlag;          /* 0x06 bits 0-1 texture users, bits 2-3 binary users */
    Float fFarClipping;         /* 0x08 */
    _OBJ_LANDENTRY *pLandEntry; /* 0x0C */
    _OBJ_MOTLANDENTRY *pMotLandEntry; /* 0x10 */
    char *pPvmFileName;         /* 0x14 */
    NJS_TEXLIST *pTexList;      /* 0x18 */
    char *pBinaryFileName;      /* 0x1C */
    void *pLoadBinary;          /* 0x20 */
} _OBJ_LANDTABLE;               /* 0x24 */

SA1_ASSERT_SIZE(_OBJ_LANDTABLE, 0x24);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, ssAttribute, 0x04);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, ssLoadFlag, 0x06);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, pLandEntry, 0x0C);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, pMotLandEntry, 0x10);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, pPvmFileName, 0x14);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, pTexList, 0x18);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, pBinaryFileName, 0x1C);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, pLoadBinary, 0x20);

extern taskwk *B_8C7596F8;              /* landtable manager task's taskwk */
extern Sint8 D_8C19E970;                /* latched level */
extern Sint8 D_8C19E971;                /* latched act */
extern _OBJ_LANDTABLE *B_8C7596F4;      /* current landtable */
extern Sint32 B_8C7596FC;               /* only cleared here */
extern Float B_8C759700;                /* passed three times to 8C043A20 by 8C051D50 */
extern Sint32 D_8C19E974;               /* 1 = landtable present */
extern Sint32 D_8C19E978;               /* 1 = also gather landtable entries (8C0522C0) */
extern Sint16 B_8C759704;               /* number of entries in B_8C759708 */
extern Sint16 B_8C75A308;               /* count of B_8C75A30C */
extern Sint32 D_8C19E968;               /* number of entries in B_8C7584F4 */
extern Float D_8C19E96C;
extern _OBJ_LANDTABLE *D_8C19E9FC[][8]; /* [level][act] landtables (data) */

extern void func_8C052A00(void);
extern void *func_8C09D9C6(char *name);         /* load a file, returns the buffer or NULL */
extern Sint32 func_8C09F3E0(void *bin);
extern void func_8C099750(char *name, NJS_TEXLIST *texlist); /* load a PVM file into texlist */

void func_8C051A16(task *tp);
void func_8C051CC0(void);
void func_8C051D38(task *tp);
void func_8C051D50(task *tp);
void func_8C051E00(task *tp);

/*
 * NON_MATCHING only because of tooling (C complete and functionally
 * faithful): with -define=NON_MATCHING all 43 instructions are identical,
 * but the tail call BRA goes to 8C051D50, which has no symbol (the splitter
 * did not see it as a function: it is only reached by this BRA and follows
 * func_8C051D38 without a symbol), so shcdiff cannot resolve the target
 * _func_8C051D50. Needs func_8C051D50 = 0x8C051D50 in the symbol list and
 * a function boundary at 8C051D50 in the chunk; then drop the guard.
 * The Float local is needed: a direct D_8C19E96C = 400.0f uses FR3 and
 * loads &D_8C19E96C before the disp/dest stores.
 */
#ifdef NON_MATCHING
void func_8C0519C0(task *tp)
{
    taskwk *twp;
    Float f;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        func_8C051A16(tp);
        B_8C7596F8 = twp;
        func_8C052A00();
        tp->disp = func_8C051E00;
        tp->dest = func_8C051D38;
        f = 400.0f;
        D_8C19E96C = f;
        /* fall through */
    case 2:
        func_8C051CC0();
        twp->mode = 1;
        /* fall through */
    case 1:
        func_8C051D50(tp);
        break;
    default:
        twp->mode = 0;
        break;
    }
}
#else
ASM_STUB(void func_8C0519C0(task *tp))
#endif

/*
 * Placed before func_8C051A16 (not in address order) on purpose: while
 * func_8C0519C0 is an ASM_STUB, this keeps enough code ahead of
 * func_8C051A52 for SHC to dump the shared literal pool in the middle of
 * func_8C051A52 (after the BRA at 8C051AEA), as in the original. Also
 * matches with the C body of func_8C0519C0 (-define=NON_MATCHING).
 */
ASM_STUB(void func_8C051CC0(void))

void func_8C051A16(task *tp)
{
    B_8C754EF0 = 0;
    D_8C19E970 = 0;
    D_8C19E971 = 0;
    B_8C7596F4 = NULL;
    B_8C7596F8 = NULL;
    B_8C7596FC = 0;
    B_8C759700 = 0.0f;
    D_8C19E974 = 1;
    D_8C19E978 = 1;
    B_8C759704 = 0;
    B_8C75A308 = 0;
    D_8C19E964 = 0;
    D_8C19E968 = 0;
}

void func_8C051A52(Sint32 level, Sint32 act)
{
    _OBJ_LANDTABLE *lt;
    _OBJ_LANDENTRY *ent;
    _OBJ_MOTLANDENTRY *mot;
    Sint8 *bin;
    Sint32 n;
    Sint32 i;

    lt = D_8C19E9FC[level][act];
    if (lt == NULL) {
        return;
    }
    n = lt->ssCount;
    if (lt->ssAttribute & 0x10) {
        if (!(lt->ssLoadFlag & 0xC)) {
            bin = func_8C09D9C6(lt->pBinaryFileName);
            lt->pLoadBinary = bin;
            func_8C09F3E0(bin);
            /* pObject/pAction hold offsets into the loaded file */
            ent = lt->pLandEntry;
            for (i = n; i > 0; i--) {
                ent->pObject = (NJS_OBJECT *)(bin + (SA1_UPTR)ent->pObject);
                ent++;
            }
            if (lt->ssAttribute & 1) {
                /* the original relocates ssCount (not ssMotCount) motion entries */
                mot = lt->pMotLandEntry;
                for (i = n; i > 0; i--) {
                    mot->pObject = (NJS_OBJECT *)(bin + (SA1_UPTR)mot->pObject);
                    mot->pAction = (NJS_ACTION *)(bin + (SA1_UPTR)mot->pAction);
                    mot++;
                }
            }
        }
        lt->ssLoadFlag += 4;
    }
    if (lt->ssAttribute & 2) {
        if (!(lt->ssLoadFlag & 3)) {
            func_8C608C82(lt->pTexList);
        }
        lt->ssLoadFlag++;
    } else if (lt->ssAttribute & 8) {
        if (lt->pPvmFileName != NULL) {
            if (!(lt->ssLoadFlag & 3)) {
                func_8C099750(lt->pPvmFileName, lt->pTexList);
            }
            lt->ssLoadFlag++;
        }
    } else if (lt->ssAttribute & 1) {
        mot = lt->pMotLandEntry;
        n = lt->ssMotCount;
        for (i = n; i > 0; i--) {
            if (mot->pTexList != NULL) {
                func_8C608C82(mot->pTexList);
            }
            mot++;
        }
    }
}

ASM_STUB(void func_8C051D38(task *tp))
ASM_STUB(void func_8C051D50(task *tp))
ASM_STUB(void func_8C051E00(task *tp))
