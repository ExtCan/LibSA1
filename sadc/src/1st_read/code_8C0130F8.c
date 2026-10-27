#include "common.h"
#include "ninja.h"
#include "sa1/ninja_funcs.h"

/* Katana PDS_PERIPHERAL (controller state, 0x34 bytes) */
typedef struct {
    Uint32 id;
    Uint32 support;
    Uint32 on;
    Uint32 off;
    Uint32 press;
    Uint32 release;
    Uint16 r;
    Uint16 l;
    Sint16 x1;
    Sint16 y1;
    Sint16 x2;
    Sint16 y2;
    char *name;
    void *extend;
    Uint32 old;
    void *info;
} PDS_PERIPHERAL;

typedef struct {
    Float unk0;
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

typedef struct {
    Sint32 pad[6];
    NJS_POINT3 pos;
    Float r;
} UNK_8C013280_t;

extern Sint32 B_8C6AF524;
extern Sint32 B_8C6AF528;
extern Float B_8C6AF52C;
extern Float B_8C6AF530;
extern Float B_8C6AF534;
extern Float B_8C6AF538;
extern Float B_8C6AF53C;
extern Float B_8C6AF540;
extern NJS_POINT3 B_8C6AC5C0;
extern NJS_POINT3 B_8C6AC5CC;
extern NJS_MATRIX B_8C6AEE28;
extern Float B_8C6AC5B4;
extern Float B_8C6AC5B8;
extern Float B_8C6AC5BC;
extern Sint32 B_8C6AF5D0;
extern Sint32 B_8C6AF558[];
extern UNK_8C6AEF84_t B_8C6AEF84[];
extern PDS_PERIPHERAL *B_8C754DA8[];
extern PDS_PERIPHERAL B_8C754C08[];
extern Sint32 B_8C6B6874;
extern Sint32 B_8C6B6870;
extern Sint32 B_8C6B686C;
extern Sint32 D_8C161CCC; /* palette number */
extern Sint32 D_8C161CD0; /* selected entry */
extern Sint32 D_8C161CD4; /* 0: base, 1: spec */
extern Sint32 D_8C161CD8;
extern Sint32 D_8C161CDC; /* range start */
extern Sint32 D_8C161CE0;
extern Sint32 D_8C161CE4;
extern Sint32 D_8C038F60[][512];

extern void func_8C038BB8(Float x, Float y, Float z);
extern void func_8C610CF8(Float x, Float y, Float z);
extern void func_8C610D30(Float a, Float b);
extern void func_8C610D3E(Float x, Float y, Float z);
extern void func_8C63AEE8(NJS_MATRIX *m, Float x, Float y, Float z);
extern void func_8C63AE2C(NJS_MATRIX *m, Float x, Float y, Float z);
extern void func_8C63ADC0(NJS_MATRIX *m, Sint32 a, Sint32 b);
extern void func_8C63AE90(NJS_MATRIX *m, Float x, Float y, Float z);
extern void func_8C038C24(void);

#ifdef NON_MATCHING
void func_8C0130F8(void) {
    NJS_POINT3 v;

    njPushMatrix(0);
    njUnitMatrix(0);
    if (*(Uint32 *)&B_8C6AF524) {
        njRotateY(0, B_8C6AF524);
    }
    if (*(Uint32 *)&B_8C6AF528) {
        njRotateZ(0, B_8C6AF528);
    }
    njCalcVector(0, &B_8C6AC5CC, &B_8C6AC5C0);
    njPopMatrix(1);
    njPushMatrix(0);
    njCalcVector(0, &B_8C6AC5C0, &v);
    njPopMatrix(1);
    func_8C038BB8(v.x, v.y, v.z);
    func_8C610CF8(v.x, v.y, v.z);
    func_8C610D30(B_8C6AF53C, (B_8C6AF540 + 0.099999996f) * 1.43999997f);
    func_8C610D3E(B_8C6AF52C, B_8C6AF530, B_8C6AF534);
    func_8C63AEE8(&B_8C6AEE28, B_8C6AF538, B_8C6AF53C, B_8C6AF540);
    func_8C63AE2C(&B_8C6AEE28, B_8C6AF52C, B_8C6AF530, B_8C6AF534);
    func_8C63ADC0(&B_8C6AEE28, 0xAAA, 0x31C7);
    func_8C63AE90(&B_8C6AEE28, B_8C6AC5C0.x, B_8C6AC5C0.y, B_8C6AC5C0.z);
    func_8C038C24();
}
#else
ASM_STUB(void func_8C0130F8(void))
#endif

#ifdef NON_MATCHING
Sint32 func_8C013280(UNK_8C013280_t *p) {
    NJS_POINT3 v;
    Float r;
    Float dx;
    Float dy;
    Float dz;
    Float t;
    Float d2;
    Sint32 ret;
    Sint32 n;

    njCalcPoint(0, &p->pos, &v);
    B_8C6AC5B4 = v.x;
    B_8C6AC5B8 = v.y;
    B_8C6AC5BC = v.z;
    r = p->r;
    ret = 0;
    if (B_8C6AF5D0 > 0) {
        n = B_8C6AF558[0];
        dx = B_8C6AEF84[n].pos.x;
        dy = B_8C6AEF84[n].pos.y;
        dz = B_8C6AEF84[n].pos.z;
        dx = B_8C6AC5B4 - dx;
        dy = B_8C6AC5B8 - dy;
        dz = B_8C6AC5BC - dz;
        /* FMUL dy*dy, then FMAC dx*dx, then FMAC dz*dz */
        d2 = SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy));
        t = B_8C6AEF84[n].unk10 + r;
        t *= t;
        if (t > d2) {
            ret++;
        }
    }
    return ret;
}
#else
ASM_STUB(Sint32 func_8C013280(UNK_8C013280_t *p))
#endif

void func_8C013310(void) {
}

void func_8C013314(Sint32 port, Uint32 buttons) {
    PDS_PERIPHERAL **pp;
    PDS_PERIPHERAL *cp;

    pp = &B_8C754DA8[port];
    buttons = ~buttons;
    (*pp)->press &= buttons;
    B_8C754C08[port].on &= buttons;
    B_8C754C08[port].old &= buttons;
    B_8C754C08[port].press &= buttons;
    (*pp)->x1 = 0;
    (*pp)->y1 = 0;
    B_8C754C08[port].x1 = 0;
    B_8C754C08[port].y1 = 0;
    (*pp)->l = 0;
    (*pp)->r = 0;
    B_8C754C08[port].l = 0;
    B_8C754C08[port].r = 0;
}

Uint32 func_8C013780(Sint32 unused, Sint32 c1, Sint32 c2, Float t);
void func_8C013860(void);

/* Palette editor debug screen: input handling. */
#ifdef NON_MATCHING
void func_8C013368(void) {
    Sint32 prev;
    Sint32 dir;
    Sint32 hdir;
    Sint32 speed;
    Sint32 all;
    Sint32 mode;
    Sint32 i;
    Sint32 end;
    Sint32 c;
    Sint32 a;
    Sint32 r;
    Sint32 g;
    Sint32 b;
    Sint32 lo;
    Sint32 hi;
    Sint32 c1;
    Sint32 c2;
    Float n;

    mode = 0;
    all = 0;
    prev = B_8C6B6874;
    B_8C6B6874 = 0;
    if (B_8C754DA8[0]->on & 0x400) {
        if (B_8C754DA8[0]->press & 0x80) {
            D_8C161CCC++;
        }
        if (B_8C754DA8[0]->press & 0x40) {
            D_8C161CD4++;
        }
        if (D_8C161CCC < 0) {
            D_8C161CCC = 5;
        }
        if (D_8C161CCC >= 6) {
            D_8C161CCC = 0;
        }
        D_8C161CD4 &= 1;
    } else {
        hdir = 0;
        dir = 0;
        if (B_8C754DA8[0]->on & 0x80) {
            dir = 1;
        }
        if (B_8C754DA8[0]->on & 0x40) {
            dir = -1;
        }
        if (B_8C754DA8[0]->on & 0x10) {
            hdir = -1;
        }
        if (B_8C754DA8[0]->on & 0x20) {
            hdir = 1;
        }
        if (B_8C754DA8[0]->on & 0x10000) {
            speed = 1;
        } else {
            speed = 8;
        }
        if (B_8C754DA8[0]->on & 0x20000) {
            all = 1;
        }
        if (B_8C754DA8[0]->on & 0x4) {
            mode = 4;
        }
        if (B_8C754DA8[0]->on & 0x200) {
            mode |= 2;
        }
        if (B_8C754DA8[0]->on & 0x2) {
            mode |= 1;
        }
        if (mode) {
            dir = -hdir;
            if (dir < 0) {
                dir = -speed;
            }
            if (dir > 0) {
                dir = speed;
            }
            if (dir) {
                B_8C6B6870--;
                if (B_8C6B6870 <= 0) {
                    if (D_8C161CE4) {
                        B_8C6B6870 = 1;
                    } else {
                        B_8C6B6870 = 8;
                    }
                    D_8C161CE4 = 1;
                    i = D_8C161CD0;
                    end = i;
                    if (all) {
                        end = 0xFF;
                        i = 0;
                    }
                    for (; i <= end; i++) {
                        c = D_8C038F60[D_8C161CCC][i * 2 + D_8C161CD4];
                        a = (Uint8)(c >> 24);
                        r = (Uint8)(c >> 16);
                        g = (Uint8)(c >> 8);
                        b = (Uint8)c;
                        if (mode & 4) {
                            r += dir;
                            if (r < 0) {
                                r = 0;
                            }
                            if (r > 0xFF) {
                                r = 0xFF;
                            }
                        }
                        if (mode & 2) {
                            g += dir;
                            if (g < 0) {
                                g = 0;
                            }
                            if (g > 0xFF) {
                                g = 0xFF;
                            }
                        }
                        if (mode & 1) {
                            b += dir;
                            if (b < 0) {
                                b = 0;
                            }
                            if (b > 0xFF) {
                                b = 0xFF;
                            }
                        }
                        D_8C038F60[D_8C161CCC][i * 2 + D_8C161CD4] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                }
            } else {
                B_8C6B6870 = 0;
                D_8C161CE4 = 0;
            }
            prev = 0;
        } else {
            if (dir < 0) {
                dir = -speed;
            }
            if (dir > 0) {
                dir = speed;
            }
            if (dir) {
                B_8C6B686C--;
                if (B_8C6B686C <= 0) {
                    if (D_8C161CE0) {
                        B_8C6B686C = 1;
                    } else {
                        B_8C6B686C = 8;
                    }
                    D_8C161CE0 = 1;
                    if (D_8C161CD0 == 0xFF && dir == -8) {
                        D_8C161CD0 = 0xF8;
                    } else {
                        D_8C161CD0 += dir;
                    }
                }
            } else {
                B_8C6B686C = 0;
                D_8C161CE0 = 0;
            }
            if (D_8C161CD0 < 0) {
                D_8C161CD0 = 0xFF;
            }
            if (D_8C161CD0 > 0xFF) {
                D_8C161CD0 = 0;
            }
            if (B_8C754DA8[0]->press & 8) {
                D_8C161CDC = D_8C161CD0;
            }
            if (B_8C754DA8[0]->on & 8) {
                B_8C6B6874 = 1;
            }
        }
        if (prev == 1 && B_8C6B6874 == 0 && D_8C161CDC != D_8C161CD0) {
            if (D_8C161CDC < D_8C161CD0) {
                lo = D_8C161CDC;
                hi = D_8C161CD0;
            } else {
                hi = D_8C161CDC;
                lo = D_8C161CD0;
            }
            c1 = D_8C038F60[D_8C161CCC][lo * 2 + D_8C161CD4];
            c2 = D_8C038F60[D_8C161CCC][hi * 2 + D_8C161CD4];
            n = (Float)(hi - lo);
            for (i = lo; i < hi; i++) {
                D_8C038F60[D_8C161CCC][i * 2 + D_8C161CD4] =
                    func_8C013780(D_8C161CD8, c1, c2, (Float)(i - lo) / n);
            }
        }
        if (B_8C6B6874 == 0) {
            D_8C161CDC = D_8C161CD0;
        }
    }
    func_8C013860();
    func_8C013314(0, 0x3060E);
}
#else
ASM_STUB(void func_8C013368(void))
#endif

ASM_STUB(Uint32 func_8C013780(Sint32 unused, Sint32 c1, Sint32 c2, Float t))
ASM_STUB(void func_8C013860(void))
