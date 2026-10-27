#include "common.h"

/*
 * GD-ROM file loading helpers (chunk 8C09CC80).
 *   func_8C09CC80(name, buf): reads a whole file into buf, 0 / -1.
 *   func_8C09DA74(name): file size in bytes, -1 if it cannot be opened.
 * The two fixed buffers 0x0CC00000 / 0x0CD00000 are DC work RAM areas
 * (PRS archive destination / packed-file scratch).
 */

/* GD file system (gdFsOpen / gdFsGetFileSize / gdFsClose) */
extern void *func_8C6016D6(const char *name, void *dir);
extern Sint32 func_8C600E2C(void *gdfs, Sint32 *size);
extern void func_8C600DF4(void *gdfs);
/* syMalloc / syFree */
extern void *func_8C6021D6(Uint32 size);
extern void func_8C602288(void *p);
/* PRS decompression (src, dst): returns the unpacked size (ignored here) */
extern Sint32 func_8C04C5E0(void *src, void *dst);
/* sets CCR.ICI (instruction cache invalidate); must run from the P2 mirror */
extern void func_8C5FC0C2(void);

extern void *B_8C7A0A84;

extern const char D_8C15B044[]; /* "s_mot.PRS" */
extern const char D_8C15B050[]; /* "b_mot.PRS" */
extern const char D_8C15B05C[]; /* "a_mot.PRS" */
extern const char D_8C15B068[]; /* "k_mot.PRS" */
extern const char D_8C15B074[]; /* "e_mot.PRS" */

ASM_STUB(Sint32 func_8C09CC80(const char *name, void *buf))

void func_8C09DA08(void *p);
Sint32 func_8C09DA40(const char *name, void *buf);
Sint32 func_8C09DA44(const char *name, void *dst);
Sint32 func_8C09DA74(const char *name);

/* Calls fn through its uncached P2 mirror (cache control code). */
#if defined(TARGET_PC)
#define CALL_P2(fn) (fn)()
#else
#define CALL_P2(fn) ((void (*)(void))((Uint32)(fn) | 0xA0000000))()
#endif

/*
 * Loads the character's motion archive ("?_mot.PRS") to 0x0CC00000.
 * chr: 0 Sonic, 2 Tails (both S_MOT), 3 Knuckles, 5 Amy, 6 E-102, 7 Big;
 * any other id hangs.
 */
void func_8C09D980(Sint32 chr)
{
    switch (chr) {
    case 0:
    case 2:
        func_8C09DA44(D_8C15B044, (void *)0x0CC00000);
        break;
    case 7:
        func_8C09DA44(D_8C15B050, (void *)0x0CC00000);
        break;
    case 5:
        func_8C09DA44(D_8C15B05C, (void *)0x0CC00000);
        break;
    case 3:
        func_8C09DA44(D_8C15B068, (void *)0x0CC00000);
        break;
    case 6:
        func_8C09DA44(D_8C15B074, (void *)0x0CC00000);
        break;
    default:
        for (;;) {
        }
    }
}

/*
 * Allocates a buffer of the file's size and loads the file into it; NULL on
 * failure. Calls the wrappers func_8C09DA40/func_8C09DA08 defined below:
 * SHC (-speed) inlines them, which is what puts the R4 load before the R5
 * move at the func_8C09CC80 call and the result copy in R5.
 */
void *func_8C09D9C6(const char *name)
{
    Sint32 size;
    void *buf;

    if ((size = func_8C09DA74(name)) == -1) {
        return NULL;
    }
    if ((buf = func_8C6021D6(size)) == NULL) {
        return NULL;
    }
    if (func_8C09DA40(name, buf) == -1) {
        func_8C09DA08(buf);
        return NULL;
    }
    return buf;
}

/* syFree wrapper */
void func_8C09DA08(void *p)
{
    func_8C602288(p);
}

/* loads a whole file into buf: 0, or -1 if it cannot be opened */
Sint32 func_8C09DA40(const char *name, void *buf)
{
    return func_8C09CC80(name, buf);
}

/* Loads a PRS-packed file and unpacks it to dst. */
Sint32 func_8C09DA44(const char *name, void *dst)
{
    Sint32 ret;

    ret = func_8C09CC80(name, (void *)0x0CD00000);
    if (ret >= 0) {
        func_8C04C5E0((void *)0x0CD00000, dst);
    }
    CALL_P2(func_8C5FC0C2);
    return ret;
}

/* file size in bytes, -1 if the file cannot be opened or sized */
Sint32 func_8C09DA74(const char *name)
{
    Sint32 size;

    B_8C7A0A84 = func_8C6016D6(name, NULL);
    if (B_8C7A0A84 == NULL) {
        return -1;
    }
    if (func_8C600E2C(B_8C7A0A84, &size) == 0) {
        size = -1;
    }
    func_8C600DF4(B_8C7A0A84);
    return size;
}
