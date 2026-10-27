#include "common.h"

/* 12-byte record (8 of them in the save data at +0x2E8, live copy at B_8C799B20) */
typedef struct {
    Sint8 unk0;
    Sint16 unk2;
    Sint16 unk4;
    Sint16 unk6;
    Sint16 unk8;
    Sint16 unkA;
} UNK_8C799B20_t;

/* Save data block (0x4A0 bytes, built on the stack by func_8C088680) */
typedef struct {
    Uint8 unk0[0x260];
    Sint8 unk260[64];               /* 512 flag bits -> B_8C799920 */
    Sint8 unk2A0[64];               /* 512 flag bits -> B_8C799B88 */
    Sint8 unk2E0[8];                /* -> B_8C799B80 */
    UNK_8C799B20_t unk2E8[8];       /* -> B_8C799B20 */
    Sint8 unk348[8][43];            /* -> B_8C7997C8 */
} UNK_SAVE_t;

extern UNK_8C799B20_t B_8C799B20[8];
extern Sint8 B_8C799920[512];
extern Sint8 B_8C799B88[512];
extern Sint8 B_8C799B80[8];
extern Sint8 B_8C7997C8[8][43];

void func_8C088C74(UNK_SAVE_t *p) {
    Sint32 i;

    for (i = 0; i < 8; i++) {
        p->unk2E8[i].unk0 = B_8C799B20[i].unk0;
        p->unk2E8[i].unk2 = B_8C799B20[i].unk2;
        p->unk2E8[i].unk4 = B_8C799B20[i].unk4;
        p->unk2E8[i].unk6 = B_8C799B20[i].unk6;
        p->unk2E8[i].unk8 = B_8C799B20[i].unk8;
        p->unk2E8[i].unkA = B_8C799B20[i].unkA;
    }
}

void func_8C088D36(UNK_SAVE_t *p) {
    Sint32 i;
    Sint32 bits;

    for (i = 0; i < 512; i++) {
        if (i % 8 == 0) {
            bits = p->unk260[i / 8];
        }
        if (bits & 1) {
            B_8C799920[i] = 1;
        } else {
            B_8C799920[i] = 0;
        }
        bits >>= 1;
    }
}

void func_8C088E94(UNK_SAVE_t *p) {
    Sint32 i;
    Sint32 bits;

    for (i = 0; i < 512; i++) {
        if (i % 8 == 0) {
            bits = p->unk2A0[i / 8];
        }
        if (bits & 1) {
            B_8C799B88[i] = 1;
        } else {
            B_8C799B88[i] = 0;
        }
        bits >>= 1;
    }
}

void func_8C089020(UNK_SAVE_t *p) {
    Sint32 i;

    for (i = 0; i < 8; i++) {
        B_8C799B80[i] = p->unk2E0[i];
    }
}

void func_8C089048(UNK_SAVE_t *p) {
    Sint32 i;
    Sint32 j;

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 43; j++) {
            B_8C7997C8[i][j] = p->unk348[i][j];
        }
    }
}

void func_8C089086(UNK_SAVE_t *p) {
    Sint32 i;

    for (i = 0; i < 8; i++) {
        B_8C799B20[i].unk0 = p->unk2E8[i].unk0;
        B_8C799B20[i].unk2 = p->unk2E8[i].unk2;
        B_8C799B20[i].unk4 = p->unk2E8[i].unk4;
        B_8C799B20[i].unk6 = p->unk2E8[i].unk6;
        B_8C799B20[i].unk8 = p->unk2E8[i].unk8;
        B_8C799B20[i].unkA = p->unk2E8[i].unkA;
    }
}
