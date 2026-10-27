#include "common.h"

/* Save data block (0x4A0 bytes, CRC16 of bytes 4..0x49F stored at +0). */
typedef struct {
    Uint32 crc;                 /* 0x000 */
    Uint8 unk004[0x25C];        /* 0x004 */
    Uint8 flags1[0x40];         /* 0x260 bit array of B_8C799920 */
    Uint8 flags2[0x40];         /* 0x2A0 bit array of B_8C799B88 */
    Uint8 unk2E0[8];            /* 0x2E0 */
    Uint8 unk2E8[0x60];         /* 0x2E8 */
    Uint8 unk348[8][43];        /* 0x348 */
} SAVEDATA;                     /* 0x4A0 */

extern Uint8 B_8C799920[512];
extern Sint8 B_8C799B88[512];
extern Uint8 B_8C799B80[8];
extern Uint8 B_8C7997C8[8][43];

/* CRC-16 (reflected CCITT, poly 0x8408) over bytes 4..0x49F */
Uint16 func_8C0886F4(Uint8 *data)
{
    Sint32 i;
    Uint16 crc;
    Sint32 j;
    Uint8 *p;

    crc = 0xFFFF;
    for (i = 4, p = data + 4; i < sizeof(SAVEDATA); i++) {
        crc ^= *p++;
        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0x8408;
            } else {
                crc >>= 1;
            }
        }
    }
    crc ^= 0xFFFF;
    return crc;
}

Sint32 func_8C0887C8(SAVEDATA *data)
{
    Sint32 i;
    Uint16 crc;
    Uint16 calc;

    for (i = 0; i < 3; i++) {
        crc = data->crc;
        calc = func_8C0886F4((Uint8 *)data);
        if (calc != crc) {
            return 0;
        } else {
            data++;
        }
    }
    return 1;
}

void func_8C088806(SAVEDATA *data)
{
    Sint32 i;
    Sint32 idx;
    Sint32 bit;

    for (i = 0; i < 512; i++) {
        if (i % 8 == 0) {
            data->flags1[i / 8] = 0;
        }
        if (B_8C799920[i] == 1) {
            idx = i / 8;
            bit = 1 << (i % 8);
            data->flags1[idx] |= bit;
        }
    }
}

void func_8C0889F6(SAVEDATA *data)
{
    Sint32 i;
    Sint32 idx;
    Sint32 bit;

    for (i = 0; i < 512; i++) {
        if (i % 8 == 0) {
            data->flags2[i / 8] = 0;
        }
        if (B_8C799B88[i] == 1) {
            idx = i / 8;
            bit = 1 << (i % 8);
            data->flags2[idx] |= bit;
        }
    }
}

void func_8C088C00(SAVEDATA *data)
{
    Sint32 i;

    for (i = 0; i < 8; i++) {
        data->unk2E0[i] = B_8C799B80[i];
    }
}

void func_8C088C36(SAVEDATA *data)
{
    Sint32 i;
    Sint32 j;

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 43; j++) {
            data->unk348[i][j] = B_8C7997C8[i][j];
        }
    }
}
