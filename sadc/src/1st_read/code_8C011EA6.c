#include "common.h"
#include "ninja.h"
#include "sa1/ninja_funcs.h"

/* Backup (VMU) lantern/palette data save/load, lighting init. */

/* Katana BUS_TIME */
typedef struct {
    Uint16 year;
    Uint8 month;
    Uint8 day;
    Uint8 hour;
    Uint8 minute;
    Uint8 second;
    Uint8 dayofweek;
} BUS_TIME;

/* backup file image description (buMakeBackupFileImage) */
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

typedef struct {
    Sint32 flag;
    Float unk4;
    Float unk8;
    Float unkC;
    Float unk10;
    Float unk14;
    Float unk18;
    Float unk1C;
    Float unk20;
    NJS_POINT3 pos;
} UNK_8C6AEF84_t;

extern Sint32 D_8C161CA8; /* backup drive number */
extern Uint8 B_8C6B2654[];
extern Uint8 B_8C6B6654[];
extern UNK_8C6AEF84_t B_8C6AEF84[];
extern Uint32 D_8C038F60[6][512];
extern Sint32 B_8C6AC5AC;
extern Sint32 B_8C6AF524;
extern Sint32 B_8C6AF528;
extern Float B_8C6AF52C;
extern Float B_8C6AF530;
extern Float B_8C6AF534;
extern Float B_8C6AF538;
extern Float B_8C6AF53C;
extern Float B_8C6AF540;
extern Float B_8C6AF544;
extern Float B_8C6AF548;
extern Float B_8C6AF54C;
extern Float B_8C6AF550;
extern Float B_8C6AF554;
extern Sint32 B_8C6AF5D0;
extern NJS_POINT3 B_8C6AC5C0;
extern NJS_POINT3 B_8C6AC5CC;
extern NJS_MATRIX B_8C6AEE28;

extern char *func_8C10D3DC(char *dst, const char *src);
extern Sint32 func_8C6037A0(void *buf, BUS_FILEINFO *info);
extern Sint32 func_8C603560(Sint32 drive, const char *name, void *buf, Sint32 blocks, BUS_TIME *time, Uint32 flag);
extern Sint32 func_8C6033E4(Sint32 drive);
extern Sint32 func_8C603508(Sint32 drive, char *name, void *buf, Sint32 size);
extern Sint32 func_8C603474(Sint32 drive, char *name);
extern Sint32 func_8C010FCC(Sint32 drive, char *name);
extern Uint16 GetStageNumber(void);
extern void func_8C601F5A(Sint32 a);
extern Sint32 func_8C602DF2(Sint32 drive);
extern Sint32 func_8C014242(Uint16 n, Sint32 m);   /* canonical: sa1/protos.h */
extern Sint32 func_8C014308(Uint16 n, Sint32 m);   /* canonical: sa1/protos.h */
extern void func_8C038BB8(Float x, Float y, Float z);
extern void func_8C610CF8(Float x, Float y, Float z);
extern void func_8C610D30(Float a, Float b);
extern void func_8C610D3E(Float x, Float y, Float z);
extern void func_8C63AA94(NJS_MATRIX *m, Sint32 n);
extern void func_8C63AEE8(NJS_MATRIX *m, Float x, Float y, Float z);
extern void func_8C63AE2C(NJS_MATRIX *m, Float x, Float y, Float z);
extern void func_8C63ADC0(NJS_MATRIX *m, Sint32 a, Sint32 b);
extern void func_8C63AE90(NJS_MATRIX *m, Float x, Float y, Float z);

/* Game name literal, shared with func_8C011C00 which comes earlier in the
 * original file and emits it first; that leaves the char array initializers
 * below at odd offsets, so they are copied bytewise. */
static const char D_8C1113D8[] = "\xBF\xC6\xAF\xB8\xB1\xC4\xDE\xCD\xDE\xDD\xC1\xAC\xB1" "999";

ASM_STUB(void func_8C010F9A(BUS_TIME *time))
ASM_STUB(Sint32 func_8C011D46(Uint16 level, Uint16 act))

#ifdef NON_MATCHING
Sint32 func_8C011EA6(Uint16 level, Uint16 act)
{
    BUS_TIME time;
    BUS_FILEINFO fi;
    char fname[16] = "SONICADV_S00";
    Uint8 *data;
    Uint8 *buf;
    Sint32 n;
    Sint32 c;
    Sint32 blocks;

    data = (Uint8 *)B_8C6AEF84;
    buf = B_8C6B2654;
    n = (level << 3) | act;
    c = n >> 5;
    c &= 0x1F;
    if (c <= 9) {
        fname[10] = c + 48;
    } else {
        fname[10] = c + 55;
    }
    c = n;
    c &= 0x1F;
    if (c <= 9) {
        fname[11] = c + 48;
    } else {
        fname[11] = c + 55;
    }
    func_8C010F9A(&time);
    func_8C10D3DC(fi.vms_comment, "LANTERN_SOURCE");
    func_8C10D3DC(fi.btr_comment, "\x82\x6B\x82\x60\x82\x6D\x82\x73\x82\x64\x82\x71\x82\x6D\x81\x5E\x8A\xC2\x8B\xAB\x8F\xEE\x95\xF1");
    func_8C10D3DC(fi.game_name, D_8C1113D8);
    fi.icon_palette = B_8C6B6654;
    fi.icon_data = B_8C6B6654;
    fi.icon_num = 1;
    fi.icon_speed = 1;
    fi.visual_data = NULL;
    fi.visual_type = 0;
    fi.save_data = data;
    fi.save_size = 0x600;
    if (func_8C603998(fi.icon_num, fi.visual_type, fi.save_size) == -256) {
        return -1;
    }
    blocks = func_8C6037A0(buf, &fi);
    if (func_8C603560(D_8C161CA8, fname, buf, blocks, &time, 0x80000000) != 0) {
        return -1;
    }
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C011EA6(Uint16 level, Uint16 act))
#endif

#ifdef NON_MATCHING
Sint32 func_8C012014(Uint16 level, Uint16 act)
{
    BUS_TIME time;
    char fname[16] = "SONICADV_S00";
    BUS_FILEINFO fi;
    Uint8 *buf;
    Uint8 *dst;
    Sint32 n;
    Sint32 c;
    Uint32 i;
    Sint32 r;

    buf = B_8C6B2654;
    dst = (Uint8 *)B_8C6AEF84;
    n = (level << 3) | act;
    c = n >> 5;
    c &= 0x1F;
    if (c <= 9) {
        fname[10] = c + 48;
    } else {
        fname[10] = c + 55;
    }
    c = n;
    c &= 0x1F;
    if (c <= 9) {
        fname[11] = c + 48;
    } else {
        fname[11] = c + 55;
    }
    r = func_8C010FCC(D_8C161CA8, fname);
    if (r != 0) {
        fname[4] = '0';
        r = func_8C010FCC(D_8C161CA8, fname);
        if (r != 0) {
            return -1;
        }
    }
    func_8C603508(D_8C161CA8, fname, buf, 5);
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    buf += 0x280;
    for (i = 0x600; i != 0; i -= 3) {
        *dst++ = *buf++;
        *dst++ = *buf++;
        *dst++ = *buf++;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C012014(Uint16 level, Uint16 act))
#endif

Sint32 func_8C01216A(void)
{
    char fname[8] = "XZXZXZXZ";
    BUS_FILEINFO fi;
    Uint8 *buf;

    buf = B_8C6B2654;
    func_8C010FCC(D_8C161CA8, fname);
    func_8C603508(D_8C161CA8, fname, buf, 1);
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    return 0;
}

Sint32 func_8C0121CC(char *name)
{
    BUS_TIME time;
    BUS_FILEINFO fi;
    Uint8 *buf;
    Sint32 size;
    Sint32 r;

    buf = (Uint8 *)0x0CD00000;
    size = func_8C603474(D_8C161CA8, name);
    r = func_8C603508(D_8C161CA8, name, buf, size);
    if (r != 0) {
        return -1;
    }
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    return size << 9;
}

#ifdef NON_MATCHING
void func_8C012240(void)
{
    NJS_POINT3 v;
    Uint16 stage;
    Sint32 level;
    Sint32 act;
    Sint32 r1;
    Sint32 r2;
    UNK_8C6AEF84_t *p;
    Sint32 i;
    Sint32 j;

    B_8C6AC5AC = 0;
    stage = GetStageNumber();
    r1 = -1;
    r2 = -1;
    level = (stage & 0xFF00) >> 8;
    act = (Uint8)stage;
    func_8C601F5A(0);
    if (func_8C602DF2(D_8C161CA8) != 0) {
        r1 = func_8C012014(level, act);
        r2 = func_8C011D46(level, act);
    }
    if (r1 != 0 || r2 != 0) {
        for (i = 0; i < 30; i++) {
            B_8C6AEF84[i].flag = 0;
        }
        B_8C6AF5D0 = 0;
        B_8C6AF544 = 20.0f;
        B_8C6AF548 = 1.0f;
        B_8C6AF554 = 1.0f;
        B_8C6AF550 = 1.0f;
        B_8C6AF54C = 1.0f;
        B_8C6AF524 = 0;
        B_8C6AF528 = 0;
        B_8C6AF534 = 1.0f;
        B_8C6AF530 = 1.0f;
        B_8C6AF52C = 1.0f;
        B_8C6AF538 = 0.8f;
        B_8C6AF53C = 1.0f;
        B_8C6AF540 = 0.25f;
        for (i = 0; i < 6; i++) {
            for (j = 0; j < 512; j += 2) {
                D_8C038F60[i][j] = 0xFFFFFFFF;
                D_8C038F60[i][j + 1] = 0xFF000000;
            }
        }
        func_8C014242((Uint16)level, act);
        func_8C014308((Uint16)level, act);
    }
    B_8C6AC5CC.x = 0.0f;
    B_8C6AC5CC.y = -1.0f;
    B_8C6AC5CC.z = 0.0f;
    njPushMatrix(NULL);
    njUnitMatrix(NULL);
    B_8C6AF524 ? njRotateY(NULL, B_8C6AF524) : (void)0;
    B_8C6AF528 ? njRotateZ(NULL, B_8C6AF528) : (void)0;
    njCalcVector(NULL, &B_8C6AC5CC, &B_8C6AC5C0);
    njPopMatrix(1);
    njPushMatrix(NULL);
    njCalcVector(NULL, &B_8C6AC5C0, &v);
    njPopMatrix(1);
    func_8C038BB8(v.x, v.y, v.z);
    func_8C610CF8(v.x, v.y, v.z);
    func_8C610D30(B_8C6AF53C, (B_8C6AF540 + 0.1f) * 1.44f);
    func_8C610D3E(B_8C6AF52C, B_8C6AF530, B_8C6AF534);
    func_8C63AA94(&B_8C6AEE28, 19);
    func_8C63AEE8(&B_8C6AEE28, B_8C6AF538, B_8C6AF53C, B_8C6AF540);
    func_8C63AE2C(&B_8C6AEE28, B_8C6AF52C, B_8C6AF530, B_8C6AF534);
    func_8C63ADC0(&B_8C6AEE28, 0xAAA, 0x31C7);
    func_8C63AE90(&B_8C6AEE28, B_8C6AC5C0.x, B_8C6AC5C0.y, B_8C6AC5C0.z);
}
#else
ASM_STUB(void func_8C012240(void))
#endif
