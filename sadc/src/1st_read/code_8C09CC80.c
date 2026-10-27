#include "common.h"

/* GD file system (gdFsOpen / gdFsGetFileSize / gdFsReqRd32 / gdFsClose) */
extern void *func_8C6016D6(const char *name, void *dir);
extern Sint32 func_8C600E2C(void *gdfs, Sint32 *size);
extern Sint32 func_8C600F1E(void *gdfs, Sint32 nsct, void *buf);
extern void func_8C600DF4(void *gdfs);

/* currently open GD file handle */
extern void *B_8C7A0A84;

/* Reads the whole file "name" into buf (rounded up to 2048-byte sectors);
 * returns 0 on success, -1 if the file cannot be opened. */
Sint32 func_8C09CC80(const char *name, void *buf) {
    Sint32 size;
    Sint32 rem;

    B_8C7A0A84 = func_8C6016D6(name, NULL);
    if (B_8C7A0A84 == NULL) {
        return -1;
    }
    func_8C600E2C(B_8C7A0A84, &size);
    if (size % 2048) {
        rem = 1;
    } else {
        rem = 0;
    }
    size = size / 2048 + rem;
    func_8C600F1E(B_8C7A0A84, size, buf);
    func_8C600DF4(B_8C7A0A84);
    return 0;
}
