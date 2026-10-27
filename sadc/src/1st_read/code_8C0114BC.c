#include "common.h"

/* Backup (VMU) save/load helpers. */

typedef struct {
    Uint8 data[152];
} BACKUP_INFO;

typedef struct {
    char str[16];
} BACKUP_FNAME;

typedef struct {
    Uint8 data[56];
} BACKUP_INFO2;

typedef struct {
    Uint8 data[0xDE0];
} BACKUP_PAL;


extern Sint32 D_8C161CA8; /* backup drive number */
extern Sint32 D_8C161CAC; /* nonzero: backup unavailable */
extern Uint8 B_8C6B2654[];
extern BACKUP_FNAME D_8C111279;
extern BACKUP_FNAME D_8C111289;
extern Uint8 B_8C7989A0[];

extern void func_8C0883E8(void);
extern void func_8C088560(void);
extern void func_8C088608(void);
extern Sint32 func_8C0887C8(Uint8 *data);
extern void func_8C088680(void);
extern Sint32 func_8C6033E4(Sint32 drive);
extern Sint32 func_8C602F5C(Sint32 drive, void *info);
extern Sint32 func_8C603474(Sint32 drive, char *name);
extern Sint32 func_8C603508(Sint32 drive, char *name, void *buf, Sint32 size);

ASM_STUB(Sint32 func_8C010FCC(Sint32 drive, char *name))
ASM_STUB(Sint32 func_8C0112E6(void))

Sint32 func_8C0114BC(void)
{
    func_8C0883E8();
    if (D_8C161CAC != 0) {
        return -1;
    }
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    return func_8C0112E6();
}

Sint32 func_8C0114F0(void)
{
    func_8C088560();
    if (D_8C161CAC != 0) {
        return -1;
    }
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    return func_8C0112E6();
}

Sint32 func_8C011524(void)
{
    func_8C088560();
    if (D_8C161CAC != 0) {
        return -1;
    }
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    if (func_8C0112E6() != 0) {
        return -1;
    }
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    return 0;
}

Sint32 func_8C011592(void)
{
    func_8C088608();
    if (D_8C161CAC != 0) {
        return -1;
    }
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    if (func_8C0112E6() != 0) {
        return -1;
    }
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    return 0;
}

Sint32 func_8C0115D0(void)
{
    func_8C088608();
    if (D_8C161CAC != 0) {
        return -1;
    }
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    if (func_8C0112E6() != 0) {
        return -1;
    }
    return 0;
}

#ifdef NON_MATCHING
Sint32 func_8C011606(void)
{
    Uint8 pad[8];
    BACKUP_FNAME name;
    BACKUP_INFO info;
    Sint32 size;
    Uint8 *src;
    Uint8 *dst;
    char *fname;
    Uint32 i;

    fname = (char *)&name;
    name = D_8C111279;
    src = B_8C6B2654;
    if (D_8C161CAC != 0) {
        return -3;
    }
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    func_8C602F5C(D_8C161CA8, &info);
    dst = B_8C7989A0;
    size = func_8C603474(D_8C161CA8, fname);
    i = func_8C010FCC(D_8C161CA8, fname);
    if (i != 0) {
        return -1;
    }
    func_8C603508(D_8C161CA8, fname, src, size);
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    src += 0x480;
    if (func_8C0887C8(src) == 0) {
        return -2;
    }
    i = 0;
    do {
        i += 3;
        *dst++ = *src++;
        *dst++ = *src++;
        *dst++ = *src++;
    } while (i < 0xDE0);
    func_8C088680();
    return 0;
}
#else
ASM_STUB(Sint32 func_8C011606(void))
#endif

#ifdef NON_MATCHING
Uint8 *func_8C011766(void)
{
    BACKUP_FNAME name = {"SONICADV_INT"};
    BACKUP_INFO info;
    Sint32 size;

    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    func_8C602F5C(D_8C161CA8, &info);
    size = func_8C603474(D_8C161CA8, name.str);
    if (func_8C010FCC(D_8C161CA8, name.str) != 0) {
        return 0;
    }
    func_8C603508(D_8C161CA8, name.str, B_8C6B2654, size);
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    return B_8C6B2654 + 0x480;
}
#else
ASM_STUB(Uint8 *func_8C011766(void))
#endif

Sint32 func_8C011828(char *name)
{
    BACKUP_INFO2 info;

    func_8C602F5C(D_8C161CA8, &info);
    return func_8C010FCC(D_8C161CA8, name);
}
