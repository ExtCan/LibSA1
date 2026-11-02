/*
 * 1ST_READ chunk 8C098EE0 (part): the texture-archive (PVM) table helpers the
 * menu front end runs (gsa1_090_charselect.md 2.7, "other").
 *
 *   func_8C099080  releases every texlist of the 8 archive lists
 *                  D_8C1C2584[] whose reference count D_8C1C24B4[] is
 *                  positive, and clears every count (the stage / menu change
 *                  8C04E490, 8C053B60, 8C0544AE, 8C0544C8)
 *   func_8C0997BC  loads "<name>.PRS" from the disc: open with 3 tries
 *                  (busy-wait between), read whole sectors to the top of the
 *                  work area (0x0CF00000 - size), PRS-decompress to 0x0CD00000
 *                  and load the PVM there into the texlist (only caller: the
 *                  PVM load 8C099750, a void policy in libsa1, which loads
 *                  textures itself; returns -1 when the file cannot be opened)
 */
#include "sa1/sa1.h"

/* One archive of a texture list: {file name, texlist}; a list ends at a NULL
 * texlist (8C098FFC, 8C099080). */
typedef struct {
    const char *name;           /* 0x00 */
    NJS_TEXLIST *texlist;       /* 0x04 */
} MENU_PVM;                     /* 0x08 */

extern MENU_PVM *D_8C1C2584[8]; /* the archive lists */
extern Sint8 D_8C1C24B4[8];     /* their reference counts (8C098FFC decrements) */
extern const char D_8C159EA4[]; /* "%s.PRS" */

#if !defined(TARGET_PC)
extern Sint32 sprintf(char *s, const char *fmt, ...);
/* GD file system (gdFsOpen / gdFsGetFileSize / gdFsRead / gdFsClose; as code_8C0141E0.c) */
extern void *func_8C6016D6(const char *name, void *dir);
extern Sint32 func_8C600E2C(void *gdfs, Sint32 *size);
extern Sint32 func_8C600F1E(void *gdfs, Sint32 nsct, void *buf);
extern void func_8C600DF4(void *gdfs);
extern Sint32 func_8C04C5E0(void *src, void *dst);            /* PRS decompress */
extern Sint32 func_8C608134(void *pvm, NJS_TEXLIST *texlist); /* load a PVM from memory */
#endif

#if !defined(TARGET_PC)
/* 8C098EE0, 8C098FAE, 8C098FFC and 8C0997BC stay out of libsa1: 8C098FAE and 8C098FFC are void policies there (LIB
 * gen/policy/stubs.tsv: 15 calls per reference script would start counting) and libsa1 loads textures itself */
ASM_STUB(void func_8C098EE0(NJS_TEXLIST *texlist))
#endif
ASM_STUB(void func_8C099750(const char *name, NJS_TEXLIST *texlist))

#if !defined(TARGET_PC)
void func_8C098FAE(Sint32 n)
{
    MENU_PVM *e;

    e = D_8C1C2584[n];
    if (e == NULL) {
        return;
    }
    if (D_8C1C24B4[n] == 0) {
        while (e->texlist != NULL) {
            if (e->name == NULL) {
                func_8C098EE0(e->texlist);
            } else {
                func_8C099750(e->name, e->texlist);
            }
            e++;
        }
    }
    D_8C1C24B4[n]++;
}

void func_8C098FFC(Sint32 n)
{
    MENU_PVM *e;

    e = D_8C1C2584[n];
    if (e == NULL) {
        return;
    }
    if (D_8C1C24B4[n] == 1) {
        while (e->texlist != NULL) {
            func_8C6088BC(e->texlist);
            e++;
        }
    }
    if (D_8C1C24B4[n] > 0) {
        D_8C1C24B4[n]--;
    }
}

/* NON_MATCHING: complete and faithful; 4 instructions differ: the scratch register of the count / texlist tests (R3 for R2), a translation-unit register rotation (8C098EE0 precedes it in the original file) */
#endif

#ifdef NON_MATCHING
void func_8C099080(void)
{
    Sint32 i;
    MENU_PVM *e;

    for (i = 0; i < 8; i++) {
        if ((e = D_8C1C2584[i]) == NULL) {
            continue;
        }
        if (D_8C1C24B4[i] > 0) {
            while (e->texlist != NULL) {
                func_8C6088BC(e->texlist);
                e++;
            }
        }
        D_8C1C24B4[i] = 0;
    }
}
#else
ASM_STUB(void func_8C099080(void))
#endif

#if !defined(TARGET_PC)
Sint32 func_8C0997BC(const char *name, NJS_TEXLIST *texlist)
{
    char path[256];
    Sint32 size;
    void *gdfs;
    Sint32 tries;
    Sint32 nsct;
    Uint8 *buf;
    Sint32 wait;

    PC_ONLY(gdfs = NULL;)
    sprintf(path, D_8C159EA4, name);
    for (tries = 3; tries > 0; tries--) {
        gdfs = func_8C6016D6(path, NULL);
        if (gdfs != NULL) {
            break;
        }
        for (wait = 1000; wait != 0; wait -= 2) {
        }
    }
    if (gdfs == NULL) {
        return -1;
    }
    func_8C600E2C(gdfs, &size);
    nsct = (size + 0x7FF) >> 11;
    buf = (Uint8 *)0x0CF00000 - nsct * 0x800;
    for (tries = 3; tries > 0; tries--) {
        if (func_8C600F1E(gdfs, nsct, buf) == 0) {
            break;
        }
    }
    func_8C600DF4(gdfs);
    func_8C04C5E0(buf, (void *)0x0CD00000);
    return func_8C608134((void *)0x0CD00000, texlist);
}
#endif
