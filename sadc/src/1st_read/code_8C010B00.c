#include "common.h"

/* Memory card (VMU) save state, one 76-byte entry per port (8 ports). */
typedef struct {
    Uint16 unk0;
    Uint16 unk2;
    Sint32 unk4;
    Sint32 unk8;
    Sint32 unkC;
    Sint32 unk10;
    Uint8 info[56];
} UNK_8C6B23F4_t;

typedef struct {
    Sint32 unk0;
    Sint32 unk4;
    Uint32 unk8;
} UNK_8C754DA8_t;

extern UNK_8C6B23F4_t B_8C6B23F4[8];
extern Uint8 B_8C6B05D4[];
extern Sint32 B_8C6B6854;
extern Sint32 B_8C754EC8;
extern UNK_8C754DA8_t *B_8C754DA8;
extern Sint32 D_8C161CA8;
extern Sint32 D_8C161CB0;
extern Sint32 D_8C161CB4;
extern Sint32 D_8C161CB8;
extern Sint32 D_8C1BDFDC;

extern void *memset(void *dst, Sint32 c, Uint32 n);
extern Sint32 func_8C602B68(Sint32 a, Sint32 b, void *buf, void (*cb)(void));
extern Sint32 func_8C6033E4(Sint32 port);
extern void func_8C041EA4(Uint32 color);
extern void func_8C042416(Sint32 pos, const char *str);
extern Sint32 func_8C602F5C(Sint32 port, void *info);
extern void func_8C602BF0(void);
extern void func_8C602DDA(Uint32 (*cb)(Sint32, Sint32, Sint32));
extern void func_8C602DE0(Sint32 (*cb)(Sint32, Sint32, Sint32, Sint32));

void func_8C010F22(void);
Sint32 func_8C0113DA(void);

void func_8C010B00(void) {
    memset(B_8C6B23F4, 0, sizeof(B_8C6B23F4));
    func_8C602B68(15, 1 << D_8C161CA8, B_8C6B05D4, func_8C010F22);
    D_8C161CB0 = 0;
    D_8C161CB4 = 0;
    D_8C161CB8 = 1;
}

void func_8C010B38(void) {
    while (func_8C6033E4(D_8C161CA8) == -1) {
    }
    D_8C161CB8 = 0;
}

Sint32 func_8C010B58(void) {
    if (D_8C161CB8 == 0) {
        return 0;
    }
    if (D_8C161CB4 > 1 && D_8C161CB4 <= 3) {
        return -1;
    }
    return 0;
}

#define MAKEPOS(x, y) (((x) << 16) | (y))

#ifdef NON_MATCHING
void func_8C010B7A(void) {
    UNK_8C6B23F4_t *p;
    Sint32 status;
    Sint32 cur;
    Sint32 total;
    Float rate;
    Sint32 i;
    UNK_8C754DA8_t *pad;
    Sint32 saved;

    p = &B_8C6B23F4[D_8C161CA8];
    if (D_8C161CB4 != 0 && D_8C161CB8 != 0) {
        switch (D_8C161CB4) {
        case 4:
            B_8C754EC8 = 0;
            func_8C041EA4(0xF0F08080);
            func_8C042416(MAKEPOS(12, 5), "- MEMORY CARD -");
            func_8C042416(MAKEPOS(12, 6), " - NOT FOUND - ");
            D_8C1BDFDC = 0;
            D_8C161CB4--;
            break;
        case 3:
            D_8C161CB4--;
            break;
        case 2:
            D_8C1BDFDC = 1;
            pad = B_8C754DA8;
            if ((pad->unk8 & 2) || (pad->unk8 & 0x400)) {
                D_8C161CB4 = 1;
                D_8C1BDFDC = 0;
            } else if (p->unk0 == 1) {
                func_8C0113DA();
                D_8C161CB4 = 1;
                D_8C1BDFDC = 0;
            }
            break;
        case 1:
            D_8C161CB4 = 0;
            break;
        }
        return;
    }
    B_8C6B6854 = func_8C6033E4(D_8C161CA8);
    D_8C161CB0--;
    if (D_8C161CB0 < 0) {
        D_8C161CB0 = 0;
    }
    status = p->unk10;
    cur = p->unk8;
    total = p->unkC;
    if (status == 12) {
        char text[11] = "NOW SAVING";
        char buf[16];

        rate = (Float)cur / (Float)total * 10.0f;
        for (i = 0; i < 10; i++) {
            if ((Float)i < rate) {
                func_8C041EA4(0xF060A0F0);
            } else {
                func_8C041EA4(0xF0F0F0F0);
            }
            buf[0] = text[i];
            buf[1] = 0;
            saved = B_8C754EC8;
            B_8C754EC8 = 0;
            func_8C042416(MAKEPOS(i + 27, 3), buf);
            B_8C754EC8 = saved;
        }
        D_8C161CB0 = 4;
    }
}
#else
ASM_STUB(void func_8C010B7A(void))
#endif

Uint32 func_8C010E9E(Sint32 port, Sint32 op, Sint32 arg) {
    UNK_8C6B23F4_t *p;
    Sint32 ret;

    p = &B_8C6B23F4[port];
    switch (op) {
    case 1:
        if (arg == 0) {
            p->unk0 = 1;
            ret = func_8C602F5C(port, p->info);
            if (ret == 0) {
                p->unk2 = 1;
            }
            p->unk4 = 0;
        }
        break;
    case 2:
        p->unk0 = 0;
        p->unk2 = 0;
        p->unk8 = 0;
        p->unkC = 0;
        p->unk10 = 0;
        memset(p->info, 0, sizeof(p->info));
        break;
    default:
        p->unk4 = arg;
        func_8C602F5C(port, p->info);
        break;
    }
    p->unk10 = 0;
    return 0;
}

Sint32 func_8C010F0E(Sint32 port, Sint32 status, Sint32 cur, Sint32 total) {
    UNK_8C6B23F4_t *p;

    p = &B_8C6B23F4[port];
    p->unk8 = cur;
    p->unkC = total;
    p->unk10 = status;
    return 0;
}

void func_8C010F22(void) {
    func_8C602BF0();
    func_8C602DDA(func_8C010E9E);
    func_8C602DE0(func_8C010F0E);
}

Sint32 func_8C010F80(void) {
    Uint8 info[56];

    func_8C602F5C(D_8C161CA8, info);
    return *(Uint16 *)&info[36];
}

ASM_STUB(Sint32 func_8C0113DA(void))
