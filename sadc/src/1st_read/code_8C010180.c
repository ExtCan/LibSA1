#include "common.h"

extern void _builtin_set_imask(int mask);
#define set_imask(mask) _builtin_set_imask(mask)

extern Uint32 D_8C66C2F8;
extern char D_8C1111EC[];
extern Uint8 B_8C67C680[];
extern Uint8 B_8C6886A0[];
extern Uint8 B_8C6946C0[];
extern Uint8 B_8C695A54[];

extern void func_8C6024D0(void);
extern void func_8C02E720(void);
extern void func_8C602128(Uint32 addr, Uint32 size);
extern void func_8C604702(Sint32 a, Sint32 b, Sint32 c);
extern void func_8C6024FA(void);
extern void func_8C601A34(Sint32 a, void *b, void *c);
extern void func_8C10BA48(void);
extern Sint32 func_8C600032(Sint32 a, Uint32 b, Sint32 c, Uint32 d);
extern void func_8C60434C(void);
extern void func_8C600DDC(char *name);
extern void func_8C60164A(void);
extern void func_8C10BA72(void);
extern void func_8C601F84(void);
extern void func_8C6052E0(void);
extern void func_8C60215E(void);
extern void func_8C60250A(void);

void func_8C01025A(void);

void func_8C010180(Sint32 a, Sint32 b, Sint32 c) {
    Sint32 i;
    Sint32 r;
    Uint32 buf;
    Uint32 work;

    set_imask(15);
    func_8C6024D0();
    func_8C02E720();
    func_8C602128(((D_8C66C2F8 & 0xE0000000) | 0x0D000000) - 0x100000, 0x100000);
    func_8C604702(a, b, c);
    func_8C6024FA();
    func_8C601A34(0, B_8C67C680, B_8C6886A0);
    func_8C10BA48();
    set_imask(0);
    buf = ((Uint32)B_8C6946C0 & ~31) + 32;
    work = ((Uint32)B_8C695A54 & ~31) + 32;
    for (i = 8; i > 0; i--) {
        r = func_8C600032(8, buf, 0x834, work);
        if (r == -23 || r == -33) {
            func_8C01025A();
            func_8C60434C();
        } else if (r == 0) {
            break;
        }
    }
    if (i == 0) {
        func_8C01025A();
        func_8C60434C();
    }
    func_8C600DDC(D_8C1111EC);
}

void func_8C01025A(void) {
    func_8C60164A();
    func_8C10BA72();
    func_8C601F84();
    func_8C6052E0();
    func_8C60215E();
    func_8C60250A();
    set_imask(15);
}
