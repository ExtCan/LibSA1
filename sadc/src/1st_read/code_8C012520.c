#include "common.h"
#include "ninja.h"
#include "task.h"

/* Light editor debug screen (free / slave lights, environment light,
 * VMS load/save of the light set). */

typedef struct {
    Sint32 flag;                /* 0x00 */
    NJS_POINT3 pos;             /* 0x04 */
    Float rd;                   /* 0x10 */
    Float rt;                   /* 0x14 */
    Float r;                    /* 0x18 */
    Float g;                    /* 0x1C */
    Float b;                    /* 0x20 */
    Uint8 pad[12];              /* 0x24 */
} UNK_8C6AEF84_t;               /* 0x30 */

typedef struct {
    Uint8 data[344];
} UNK_8C6AC5D8_t;

/* backup disk info */
typedef struct {
    Uint8 pad0[36];
    Uint16 free_blocks;         /* 0x24 */
    Uint8 pad26[30];
} BUS_DISKINFO;

extern Uint32 D_8C038F60[6][512];
extern taskwk *B_8C78C608[];
extern Sint32 B_8C6AC5B0;
extern Float B_8C6AC5B4;
extern Float B_8C6AC5B8;
extern Float B_8C6AC5BC;
extern Sint32 B_8C6AEF80;
extern UNK_8C6AEF84_t B_8C6AEF84[30];
extern UNK_8C6AC5D8_t B_8C6AC5D8[30];
extern Sint32 B_8C6AC5AC;
extern Sint32 B_8C6B6868;
extern Sint32 B_8C6B6864;
extern Sint32 D_8C161CA8; /* backup drive number */
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

extern char D_8C111450[]; /* "   /   " */
extern char D_8C111458[]; /* "[SAVE]   ->SURE" */
extern char D_8C111468[]; /* "[LOAD] SURE<-" */
extern char D_8C111478[]; /* "[FILE] LOAD<- ->SAVE" */
extern char D_8C111490[]; /* " - SLOT 1 -" */
extern char D_8C11149C[]; /* "FREE:   BLK" */
extern char D_8C1114A8[]; /* " - SLOT 2 -" */
extern char D_8C1114B4[]; /* "[VMS->PC] ->SURE" */
extern char D_8C1114C8[]; /* "[VMS->PC] ->PC" */
extern char D_8C1114D8[]; /* "[PC->VMS] ->SURE" */
extern char D_8C11150C[]; /* "[PC->VMS] ->VMS" */
extern char D_8C11151C[]; /* "[FREE]  RD RT R  G  B" */
extern char D_8C111534[]; /* "[SLAVE] RD RT R  G  B" */
extern char D_8C11154C[]; /* "[ENV]   RY RZ R  G  B  SP DI AM" */
extern char D_8C11156C[]; /* "__" */

extern Uint16 GetStageNumber(void);
extern Sint32 func_8C011EA6(Uint16 level, Uint16 act);
extern Sint32 func_8C012014(Uint16 level, Uint16 act);
extern void func_8C63AC14(UNK_8C6AC5D8_t *lp);
extern void njPrintC(Sint32 loc, char *str);
extern void func_8C640DE2(Sint32 loc, Sint32 val, Sint32 digits);
extern void func_8C640A80(Int loc, Int value, Int digits); /* sa1/ninja_funcs.h: value is signed */
extern Sint32 func_8C602F5C(Sint32 drive, BUS_DISKINFO *info);
extern Sint32 func_8C602FB8(Sint32 drive, char *name);
extern Sint32 func_8C602FE6(Sint32 drive, char *name);

extern Sint32 UNK_PcWrite(char *name, void *buf, Sint32 size);

ASM_STUB(Sint32 func_8C0121CC(char *name))

void func_8C013368(void);

#ifdef NON_MATCHING
void func_8C012520(void)
{
    BUS_DISKINFO info;
    char buf[16];
    Sint32 i;
    Sint32 j;
    Sint32 n;
    Sint32 max;
    Sint32 best;
    Sint32 size;
    Float d0;
    Float min;
    Float x;
    Float y;
    Float z;
    Float d;
    Uint16 stage;
    Uint16 level;
    Uint16 act;
    UNK_8C6AEF84_t *p;

    for (i = 0; i < 6; i++) {
        for (j = 0; j < 512; j++) {
            D_8C038F60[i][j] |= 0xFF000000;
        }
    }
    B_8C6AC5B4 = B_8C78C608[0]->pos.x;
    B_8C6AC5B8 = B_8C78C608[0]->pos.y;
    B_8C6AC5BC = B_8C78C608[0]->pos.z;
    stage = GetStageNumber();
    level = (stage & 0xFF00) >> 8;
    act = stage & 0xFF;
    B_8C6AEF80 = 0;
    for (i = 0; i < 30; i++) {
        if (B_8C6AEF84[i].flag) {
            B_8C6AEF80++;
            func_8C63AC14(&B_8C6AC5D8[i]);
        }
    }
    d0 = 0.0f;
    if (B_8C6AC5AC == 0) {
        return;
    }
    if (B_8C6AC5AC != 1) {
        njPrintC(0x0007001B, D_8C111450);
        func_8C640DE2(0x0007001B, B_8C6AEF80, 3);
        func_8C640DE2(0x000B001B, 30, 3);
    }
    switch (B_8C6AC5AC) {
    case 1:
        func_8C013368();
        break;
    case 5:
        if (B_8C6B6868 == 1) {
            njPrintC(0x0007001A, D_8C111458);
            if (0) {
                func_8C011EA6(level, act);
                B_8C6B6868 = 0;
            }
        } else if (B_8C6B6868 == 2) {
            njPrintC(0x0007001A, D_8C111468);
            if (0) {
                func_8C012014(level, act);
                B_8C6B6868 = 0;
                B_8C6AEF80 = 0;
                for (i = 0; i < 30; i++) {
                    if (B_8C6AEF84[i].flag) {
                        B_8C6AEF80++;
                    }
                }
            }
        } else {
            njPrintC(0x0007001A, D_8C111478);
        }
        func_8C602F5C(D_8C161CA8, &info);
        njPrintC(0x00050003, D_8C111490);
        njPrintC(0x00050004, D_8C11149C);
        func_8C640A80(0x000A0004, info.free_blocks, 3);
        n = 5;
        i = func_8C602FB8(D_8C161CA8, buf);
        while (i >= 0) {
            njPrintC(n++ | 0x00050000, buf);
            i = func_8C602FE6(D_8C161CA8, buf);
        }
        func_8C602F5C(1, &info);
        njPrintC(0x00140003, D_8C1114A8);
        njPrintC(0x00140004, D_8C11149C);
        func_8C640A80(0x00190004, info.free_blocks, 3);
        n = 5;
        i = func_8C602FB8(1, buf);
        while (i >= 0) {
            njPrintC(n++ | 0x00140000, buf);
            i = func_8C602FE6(1, buf);
        }
        break;
    case 3:
        if (B_8C6B6868 == 1) {
            njPrintC(0x0007001A, D_8C1114B4);
            if (0) {
                i = func_8C602FB8(D_8C161CA8, buf);
                while (i >= 0) {
                    if ((buf[9] == 'P' || buf[9] == 'S') && buf[10] == 'L') {
                        size = func_8C0121CC(buf);
                        for (j = 0; buf[j]; j++) {
                        }
                        buf[j++] = '.';
                        buf[j++] = 'b';
                        buf[j++] = 'i';
                        buf[j++] = 'n';
                        buf[j] = 0;
                        switch (buf[11]) {
                        case 'B':
                            UNK_PcWrite(buf, (void *)0x0CD00080, size - 0x80);
                            break;
                        case 'C':
                            buf[11] = 'B';
                            UNK_PcWrite(buf, (void *)0x0CD00280, size - 0x280);
                            break;
                        default:
                            UNK_PcWrite(buf, (void *)0x0CD00000, size);
                            break;
                        }
                    } else {
                        UNK_PcWrite(buf, (void *)0x0CD00000, func_8C0121CC(buf));
                    }
                    i = func_8C602FE6(D_8C161CA8, buf);
                }
                B_8C6B6868 = 0;
            }
        } else {
            njPrintC(0x0007001A, D_8C1114C8);
        }
        func_8C602F5C(D_8C161CA8, &info);
        njPrintC(0x00050003, D_8C111490);
        njPrintC(0x00050004, D_8C11149C);
        func_8C640A80(0x000A0004, info.free_blocks, 3);
        n = 5;
        i = func_8C602FB8(D_8C161CA8, buf);
        while (i >= 0) {
            njPrintC(n++ | 0x00050000, buf);
            i = func_8C602FE6(D_8C161CA8, buf);
        }
        break;
    case 4:
        if (B_8C6B6868 == 1) {
            njPrintC(0x0007001A, D_8C1114D8);
        } else {
            njPrintC(0x0007001A, D_8C11150C);
        }
        break;
    case 7:
        njPrintC(0x0007001A, D_8C11151C);
        func_8C640DE2(0x000F001B, B_8C6AF544, 2);
        func_8C640DE2(0x0012001B, B_8C6AF548 * 255.0f, 2);
        func_8C640DE2(0x0015001B, B_8C6AF54C * 255.0f, 2);
        func_8C640DE2(0x0018001B, B_8C6AF550 * 255.0f, 2);
        func_8C640DE2(0x001B001B, B_8C6AF554 * 255.0f, 2);
        if (0) {
            if (B_8C6AEF80 < 30) {
                for (i = 0; B_8C6AEF84[i].flag && i < 30; i++) {
                }
                p = &B_8C6AEF84[i];
                p->flag = 1;
                p->rd = B_8C6AF544;
                p->rt = B_8C6AF548;
                p->r = B_8C6AF54C;
                p->g = B_8C6AF550;
                p->b = B_8C6AF554;
                p->pos.x = B_8C6AC5B4;
                p->pos.y = B_8C6AC5B8;
                p->pos.z = B_8C6AC5BC;
            }
        } else if (0) {
            min = 1000000.0f;
            best = -1;
            for (i = 0; i < 30; i++) {
                x = B_8C6AC5B4 - B_8C6AEF84[i].pos.x;
                y = B_8C6AC5B8 - B_8C6AEF84[i].pos.y;
                z = B_8C6AC5BC - B_8C6AEF84[i].pos.z;
                d = x * x + y * y + z * z;
                if (d < min) {
                    min = d;
                    best = i;
                }
            }
            if (best >= 0) {
                B_8C6AC5B0 = best;
                p = &B_8C6AEF84[best];
                p->flag = 0;
                B_8C6AC5B4 = p->pos.x;
                B_8C6AC5B8 = p->pos.y;
                B_8C6AC5BC = p->pos.z;
                B_8C6AF544 = p->rd;
                B_8C6AF548 = p->rt;
                B_8C6AF54C = p->r;
                B_8C6AF550 = p->g;
                B_8C6AF554 = p->b;
                B_8C78C608[0]->pos.x = B_8C6AC5B4;
                B_8C78C608[0]->pos.y = B_8C6AC5B8;
                B_8C78C608[0]->pos.z = B_8C6AC5BC;
            }
        }
        max = 5;
        break;
    case 8:
        njPrintC(0x0007001A, D_8C111534);
        func_8C640DE2(0x000F001B, B_8C6AF544, 2);
        func_8C640DE2(0x0012001B, B_8C6AF548 * 255.0f, 2);
        func_8C640DE2(0x0015001B, B_8C6AF54C * 255.0f, 2);
        func_8C640DE2(0x0018001B, B_8C6AF550 * 255.0f, 2);
        func_8C640DE2(0x001B001B, B_8C6AF554 * 255.0f, 2);
        max = 4;
        break;
    case 2:
        njPrintC(0x0007001A, D_8C11154C);
        func_8C640DE2(0x000F001B, B_8C6AF524 / 256, 2);
        func_8C640DE2(0x0012001B, B_8C6AF528 / 256, 2);
        func_8C640DE2(0x0015001B, B_8C6AF52C * 255.0f, 2);
        func_8C640DE2(0x0018001B, B_8C6AF530 * 255.0f, 2);
        func_8C640DE2(0x001B001B, B_8C6AF534 * 255.0f, 2);
        func_8C640DE2(0x001E001B, B_8C6AF538 * 255.0f, 2);
        func_8C640DE2(0x0021001B, B_8C6AF53C * 255.0f, 2);
        func_8C640DE2(0x0024001B, B_8C6AF540 * 255.0f, 2);
        max = 8;
        break;
    default:
        max = 3;
        break;
    }
    if (B_8C6B6864 >= max) {
        B_8C6B6864 = 0;
    }
    if (B_8C6B6864 < 0) {
        B_8C6B6864 = max - 1;
    }
    if (B_8C6AC5AC != 5 && B_8C6AC5AC != 3 && B_8C6AC5AC != 1) {
        njPrintC(((B_8C6B6864 * 3 + 15) << 16) | 25, D_8C11156C);
    }
    if (B_8C6AC5AC == 7 || B_8C6AC5AC == 8) {
        switch (B_8C6B6864) {
        case 0:
            B_8C6AF544 += d0;
            if (B_8C6AF544 < 0.0f) {
                B_8C6AF544 = 0.0f;
            }
            break;
        case 1:
            B_8C6AF548 += 0.0f;
            if (B_8C6AF548 < 0.0f) {
                B_8C6AF548 = 0.0f;
            }
            if (B_8C6AF548 > 1.0f) {
                B_8C6AF548 = 1.0f;
            }
            break;
        case 2:
            B_8C6AF54C += 0.0f;
            if (B_8C6AF54C < 0.0f) {
                B_8C6AF54C = 0.0f;
            }
            if (B_8C6AF54C > 1.0f) {
                B_8C6AF54C = 1.0f;
            }
            break;
        case 3:
            B_8C6AF550 += 0.0f;
            if (B_8C6AF550 < 0.0f) {
                B_8C6AF550 = 0.0f;
            }
            if (B_8C6AF550 > 1.0f) {
                B_8C6AF550 = 1.0f;
            }
            break;
        case 4:
            B_8C6AF554 += 0.0f;
            if (B_8C6AF554 < 0.0f) {
                B_8C6AF554 = 0.0f;
            }
            if (B_8C6AF554 > 1.0f) {
                B_8C6AF554 = 1.0f;
            }
            break;
        }
    } else if (B_8C6AC5AC == 2) {
        switch (B_8C6B6864) {
        case 0:
            B_8C6AF524 = (Uint16)B_8C6AF524;
            break;
        case 1:
            B_8C6AF528 = (Uint16)B_8C6AF528;
            break;
        case 2:
            B_8C6AF52C += 0.0f;
            if (B_8C6AF52C < 0.0f) {
                B_8C6AF52C = 0.0f;
            }
            if (B_8C6AF52C > 1.0f) {
                B_8C6AF52C = 1.0f;
            }
            break;
        case 3:
            B_8C6AF530 += 0.0f;
            if (B_8C6AF530 < 0.0f) {
                B_8C6AF530 = 0.0f;
            }
            if (B_8C6AF530 > 1.0f) {
                B_8C6AF530 = 1.0f;
            }
            break;
        case 4:
            B_8C6AF534 += 0.0f;
            if (B_8C6AF534 < 0.0f) {
                B_8C6AF534 = 0.0f;
            }
            if (B_8C6AF534 > 1.0f) {
                B_8C6AF534 = 1.0f;
            }
            break;
        case 5:
            B_8C6AF538 += 0.0f;
            if (B_8C6AF538 < 0.0f) {
                B_8C6AF538 = 0.0f;
            }
            if (B_8C6AF538 > 1.0f) {
                B_8C6AF538 = 1.0f;
            }
            break;
        case 6:
            B_8C6AF53C += 0.0f;
            if (B_8C6AF53C < 0.0f) {
                B_8C6AF53C = 0.0f;
            }
            if (B_8C6AF53C > 1.0f) {
                B_8C6AF53C = 1.0f;
            }
            break;
        case 7:
            B_8C6AF540 += 0.0f;
            if (B_8C6AF540 < 0.0f) {
                B_8C6AF540 = 0.0f;
            }
            if (B_8C6AF540 > 1.0f) {
                B_8C6AF540 = 1.0f;
            }
            break;
        }
    }
}
#else
ASM_STUB(void func_8C012520(void))
#endif

ASM_STUB(void func_8C013368(void))
