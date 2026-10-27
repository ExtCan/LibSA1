#include "common.h"

/* Katana PDS_PERIPHERAL (only the fields used here) */
typedef struct {
    Uint32 id;
    Uint32 support;
    Uint32 on;
} PDS_PERIPHERAL;

/* The original translation unit also held func_8C012520's code, whose string
 * "LINIER" (D_8C111590, 7 bytes) precedes the name templates in the C section;
 * this keeps the templates at the same misaligned offset, which makes SHC copy
 * them byte by byte. */
static const char D_8C111590[] = "LINIER";

extern Uint8 B_8C6AEF84[];
extern Uint8 D_8C038F60[];
extern Sint32 B_8C6AC5AC;
extern PDS_PERIPHERAL *B_8C754DA8[];

/* GD file system (gdFsOpen / gdFsGetFileSize / gdFsRead / gdFsClose) */
extern void *func_8C6016D6(const char *name, void *dir);
extern Sint32 func_8C600E2C(void *gdfs, Sint32 *size);
extern Sint32 func_8C600F1E(void *gdfs, Sint32 nsct, void *buf);
extern void func_8C600DF4(void *gdfs);
extern void func_8C012240(void);
extern void func_8C012520(void);

/* Loads a whole file into buf; returns 1 on success, 0 if it cannot be opened. */
Sint32 func_8C0141E0(const char *name, void *buf) {
    void *gdfs;
    Sint32 size;
    Sint32 rem;

    gdfs = func_8C6016D6(name, NULL);
    if (gdfs == NULL) {
        return 0;
    }
    func_8C600E2C(gdfs, &size);
    rem = (size % 2048) ? 1 : 0;
    size = size / 2048 + rem;
    func_8C600F1E(gdfs, size, buf);
    func_8C600DF4(gdfs);
    return 1;
}

/* Loads "SL_<n><m>B.bin" (falls back to "SL_<n>0B.bin"). */
Sint32 func_8C014242(Uint16 n, Sint32 m) {
    char name[16] = "SL_";
    Uint8 *dst;
    Sint32 ret;

    dst = B_8C6AEF84;
    if (n <= 9) {
        name[3] = n + '0';
    } else {
        name[3] = n + 'A' - 10;
    }
    if (name[3] > 'Z') {
        name[3] -= 26;
        name[2] = '1';
    }
    name[4] = m + '0';
    name[5] = 'B';
    name[6] = '.';
    name[7] = 'b';
    name[8] = 'i';
    name[9] = 'n';
    name[10] = 0;
    ret = func_8C0141E0(name, dst);
    if (ret != 1) {
        name[4] = '0';
        ret = func_8C0141E0(name, dst);
    }
    return ret;
}

/* Loads "PL_<n><m>B.bin" (falls back to "PL_<n>0B.bin"). */
Sint32 func_8C014308(Uint16 n, Sint32 m) {
    char name[16] = "PL_";
    Uint8 *dst;
    Sint32 ret;

    dst = D_8C038F60;
    if (n <= 9) {
        name[3] = n + '0';
    } else {
        name[3] = n + 'A' - 10;
    }
    if (name[3] > 'Z') {
        name[3] -= 26;
        name[2] = '1';
    }
    name[4] = m + '0';
    name[5] = 'B';
    name[6] = '.';
    name[7] = 'b';
    name[8] = 'i';
    name[9] = 'n';
    name[10] = 0;
    ret = func_8C0141E0(name, dst);
    if (ret != 1) {
        name[4] = '0';
        ret = func_8C0141E0(name, dst);
    }
    return ret;
}

void func_8C014422(void) {
    func_8C012240();
}

Sint32 func_8C014428(void) {
    if (B_8C6AC5AC == 0 && (B_8C754DA8[0]->on & 4)) {
        return -1;
    }
    func_8C012520();
    return 0;
}
