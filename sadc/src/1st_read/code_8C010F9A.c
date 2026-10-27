#include "common.h"

/* Backup (VMU) save file helpers: file search, save file creation. */

/* RTC date as filled by func_8C10BAAA (syRtcGetDate-like); 12 bytes on the stack */
typedef struct {
    Uint16 year;
    Uint8 month;
    Uint8 day;
    Uint8 hour;
    Uint8 minute;
    Uint8 second;
    Uint8 dayofweek;
    Uint32 unk8;
} SYS_RTC_DATE;

/* backup file time stamp (Katana BUS_TIME) */
typedef struct {
    Uint16 year;
    Uint8 month;
    Uint8 day;
    Uint8 hour;
    Uint8 minute;
    Uint8 second;
    Uint8 dayofweek;
} BUS_TIME;

/* backup disk info (56 bytes) */
typedef struct {
    Uint8 data[56];
} BUS_DISKINFO;

/* backup file name (byte aligned, copied byte by byte) */
typedef struct {
    char name[16];
} BUS_FNAME;

/* backup file image description (buMakeBackupFileImage-like) */
typedef struct {
    char vms_comment[18];
    char btr_comment[34];
    char game_name[16];
    void *icon_palette;
    void *icon_data;
    Uint16 icon_num;
    Uint16 icon_speed;
    void *visual_data;
    Uint16 visual_type;
    Uint16 reserved;
    void *save_data;
    Uint32 save_size;
} BUS_FILEINFO;

/* per-drive state (76 bytes) */
typedef struct {
    Sint16 unk0;
    Uint8 pad[74];
} UNK_8C6B23F4_t;

extern Sint32 D_8C161CA8; /* backup drive number */
extern Sint32 D_8C161CAC; /* nonzero: backup unavailable */
extern Sint32 D_8C161CB4;
extern char D_8C161CBC[]; /* "SONICADV_INT" */
extern BUS_FNAME D_8C111223; /* "SONICADV_INT" */
extern void *D_8C161C90[5]; /* icon palettes per save file type */
extern void *D_8C161C7C[5]; /* icon data per save file type */
extern Sint32 D_8C161C68[5]; /* icon counts per save file type */
extern char D_8C19ACB8[]; /* "0123456789ABCDEF" */
extern UNK_8C6B23F4_t B_8C6B23F4[];
extern Uint8 B_8C6B2654[]; /* save file image buffer */
extern Sint32 B_8C6B6858; /* save file image size */
extern BUS_TIME B_8C6B685C; /* save file time stamp */
extern Uint8 B_8C7989A0[]; /* save data */

extern void func_8C10BAAA(SYS_RTC_DATE *date);
extern Sint32 func_8C602FB8(Sint32 drive, void *name);
extern Sint32 func_8C602FE6(Sint32 drive, void *name);
extern Sint32 func_8C10D3C8(const void *a, const char *b);
extern char *func_8C10D3DC(char *dst, const char *src);
extern Sint32 func_8C602F5C(Sint32 drive, BUS_DISKINFO *info);
extern Sint32 func_8C603998(Uint32 icon_num, Uint32 visual_type, Uint32 save_size); /* canonical: sa1/ninja_funcs.h */
extern Sint32 func_8C6037A0(void *buf, BUS_FILEINFO *info);
extern Sint32 func_8C603560(Sint32 drive, const void *name, void *buf, Sint32 blocks, BUS_TIME *time,
                            Uint32 flag);
extern void func_8C088280(void);
extern Sint32 func_8C6033E4(Sint32 drive);

Sint32 func_8C0113DA(void);

/* Stamps a BUS_TIME with the current RTC date. */
void func_8C010F9A(BUS_TIME *time)
{
    SYS_RTC_DATE date;

    func_8C10BAAA(&date);
    time->year = date.year;
    time->month = date.month;
    time->day = date.day;
    time->hour = date.hour;
    time->minute = date.minute;
    time->second = date.second;
    time->dayofweek = date.dayofweek;
}

/* Returns 0 if a file called name exists on the drive. */
Sint32 func_8C010FCC(Sint32 drive, const char *name)
{
    char buf[16];
    Sint32 r;
    Sint32 cmp;

    if (D_8C161CAC != 0) {
        return -1;
    }
    r = func_8C602FB8(drive, buf);
    cmp = func_8C10D3C8(&buf, name);
    while (r >= 0 && cmp != 0) {
        r = func_8C602FE6(drive, buf);
        cmp = func_8C10D3C8(&buf, name);
    }
    return cmp;
}

/* Hex digit value of c, or -1. */
Sint32 func_8C011042(char c)
{
    Sint32 i;
    Sint32 r;

    r = -1;
    for (i = 0; i < 16; i++) {
        if (c == D_8C19ACB8[i]) {
            r = i;
            break;
        }
    }
    return r;
}

/* Highest NNN among the files SONICADV_NNN (hex) on the drive, or -1. */
Sint32 func_8C011060(Sint32 drive)
{
    char buf[16];
    Sint32 r;
    Sint32 v;
    Sint32 max;

    max = -1;
    if (D_8C161CAC != 0) {
        return -1;
    }
    r = func_8C602FB8(drive, buf);
    if (buf[0] == 'S' && buf[1] == 'O' && buf[2] == 'N' && buf[3] == 'I'
        && buf[4] == 'C' && buf[5] == 'A' && buf[6] == 'D' && buf[7] == 'V'
        && buf[8] == '_' && func_8C011042(buf[9]) >= 0
        && func_8C011042(buf[10]) >= 0 && func_8C011042(buf[11]) >= 0) {
        v = func_8C011042(buf[9]) * 256 + func_8C011042(buf[10]) * 16
            + func_8C011042(buf[11]);
        if (v > max) {
            max = v;
        }
    }
    while (r >= 0) {
        r = func_8C602FE6(drive, buf);
        if (buf[0] == 'S' && buf[1] == 'O' && buf[2] == 'N' && buf[3] == 'I'
            && buf[4] == 'C' && buf[5] == 'A' && buf[6] == 'D' && buf[7] == 'V'
            && buf[8] == '_' && func_8C011042(buf[9]) >= 0
            && func_8C011042(buf[10]) >= 0 && func_8C011042(buf[11]) >= 0) {
            v = func_8C011042(buf[9]) * 256 + func_8C011042(buf[10]) * 16
                + func_8C011042(buf[11]);
            if (v > max) {
                max = v;
            }
        }
    }
    return max;
}

/* 1 if the main save file exists. The original copies the name from a byte
 * aligned 16-byte initializer (probably a char array initialized with a
 * string); a struct copy from the extern reproduces the byte-wise copy. */
Sint32 func_8C011238(void)
{
    BUS_FNAME name;
    BUS_DISKINFO info;
    Sint32 r;

    name = D_8C111223;
    if (D_8C161CAC != 0) {
        return 0;
    }
    func_8C602F5C(D_8C161CA8, &info);
    r = 0;
    if (func_8C010FCC(D_8C161CA8, name.name) == 0) {
        r = 1;
    }
    return r;
}

#ifdef NON_MATCHING
/* Builds and writes the main save file.
 * Only difference: the original keeps the stack store of "data" before the BSR
 * to func_8C010F9A (NOP in the delay slot); this compiles it into the delay
 * slot (the two pool loads come out in the other order). Assigning p before buf
 * fixes that spot but breaks the prologue schedule instead. */
Sint32 func_8C0112E6(void)
{
    BUS_FILEINFO fi;
    BUS_DISKINFO info;
    Uint8 *data;
    UNK_8C6B23F4_t *p;
    Uint8 *buf;
    Sint32 size;

    buf = B_8C6B2654;
    p = &B_8C6B23F4[D_8C161CA8];
    func_8C602F5C(D_8C161CA8, &info);
    data = B_8C7989A0;
    func_8C010F9A(&B_8C6B685C);
    func_8C10D3DC(fi.vms_comment, "MAIN_SAVE_FILE");
    func_8C10D3DC(fi.btr_comment, "SONIC ADVENTURE / Main Save File");
    func_8C10D3DC(fi.game_name, "0000000000000000");
    fi.icon_palette = D_8C161C90[0];
    fi.icon_data = D_8C161C7C[0];
    fi.icon_num = D_8C161C68[0];
    fi.icon_speed = 20;
    fi.visual_data = NULL;
    fi.visual_type = 0;
    fi.save_data = data;
    fi.save_size = 0xF80;
    size = func_8C603998(fi.icon_num, fi.visual_type, fi.save_size);
    if (size == -256) {
        return -1;
    }
    B_8C6B6858 = func_8C6037A0(buf, &fi);
    /* the original falls off the end here (R0 happens to hold 4) */
    if (p->unk0 == 0) {
        D_8C161CB4 = 4;
    } else {
        return func_8C0113DA();
    }
}
#else
ASM_STUB(Sint32 func_8C0112E6(void))
#endif

#ifdef NON_MATCHING
/* Writes the save file image (buSaveFile-like).
 * Only difference: the original loads &B_8C6B6858 after the first argument push
 * and B_8C6B2654 (R6) before the value of B_8C6B6858 (R7); this schedules them
 * the other way round. Note: the register choice here depends on the tail of
 * func_8C0112E6 compiled just before it: with the fall-off-the-end tail below the
 * two pushed constants also swap R2/R3; with explicit returns in every path of
 * func_8C0112E6 they come out as in the original (R3 then R2). */
Sint32 func_8C0113DA(void)
{
    Sint32 r;

    r = func_8C603560(D_8C161CA8, D_8C161CBC, B_8C6B2654, B_8C6B6858, &B_8C6B685C, 0x80000000);
    if (r != 0) {
        return -1;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0113DA(void))
#endif

Sint32 func_8C01140A(void)
{
    func_8C088280();
    if (D_8C161CAC != 0) {
        return -1;
    }
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    return func_8C0112E6();
}
