#include "common.h"

/* id -> value lookup table (8-byte entries) */
typedef struct {
    Uint8 id;
    Sint32 val;
} UNK_8C082A66_ent;

typedef struct {
    Sint32 num;
    UNK_8C082A66_ent *list;
} UNK_8C082A66_tbl;

typedef struct {
    Sint32 *unk0;
} UNK_8C1BEF54_t;

typedef struct {
    Uint8 unk0;
    Uint8 unk1;
    Uint8 unk2;
    Uint8 unk3;
    Uint16 unk4;
    UNK_8C1BEF54_t *unk8;
} UNK_8C79892C_t;

extern Sint32 B_8C78C24C;
extern UNK_8C79892C_t B_8C79892C;
extern Sint32 B_8C754BFC;
extern Sint32 D_8C161CB0;
extern Sint32 D_8C1BDFDC;
extern UNK_8C082A66_tbl D_8C1BEEC8[];
extern UNK_8C082A66_tbl D_8C1BEEF8;
extern UNK_8C082A66_tbl D_8C1BEF00;
extern UNK_8C082A66_tbl D_8C1BEF08[];
extern UNK_8C082A66_tbl D_8C1BEF38;
extern UNK_8C082A66_tbl D_8C1BEF40;
extern UNK_8C1BEF54_t D_8C1BEF54;
extern Sint8 D_8C1BEF5C[];

extern void func_8C107AAA(void);
extern void func_8C07DEC0(void);
extern void func_8C051760(Sint32 a, Sint32 b);
extern void func_8C07EC90(void);
extern void func_8C63FAE8(void);
extern void func_8C60526A(Uint32 a, Uint32 b, Uint32 c);
extern Uint16 GetStageNumber(void);
extern Sint32 GetPlayerNumber(void);
extern Sint32 func_8C608C82(UNK_8C1BEF54_t *p);
extern Sint32 njSetTexture(UNK_8C1BEF54_t *p);   /* NJS_TEXLIST *: sa1/ninja_funcs.h */
extern void func_8C07E0AE(Uint32 a, Float x, Float y, Float z);
extern Sint32 func_8C6088BC(UNK_8C1BEF54_t *p);  /* NJS_TEXLIST *: sa1/ninja_funcs.h */

void func_8C082A60(Sint32 a) {
    B_8C78C24C = a;
}

Sint32 func_8C082A66(UNK_8C082A66_tbl *t, Sint32 id) {
    UNK_8C082A66_ent *e;
    Sint32 i;

    e = t->list;
    for (i = 0; i < t->num; i++, e++) {
        if (e->id == id) {
            return e->val;
        }
    }
    return 0;
}

/* Looks up a per-level entry (level = high byte of GetStageNumber()) in
 * one of two table sets selected by B_8C754BFC (probably the language).
 * Matching notes: lang/tblA/tblB locals give the hoisted loads and pool
 * order; the `register` keywords (w, lang, tblB) and the if/else for unk4
 * are needed for the original register allocation (R10/R11/R12, R4). */
Sint32 func_8C082A8C(Sint32 time) {
    register UNK_8C79892C_t *w;
    Sint32 val;
    Uint16 stg;
    Sint32 idx;
    register Sint32 *lang;
    UNK_8C082A66_tbl *tblA;
    register UNK_8C082A66_tbl *tblB;

    w = &B_8C79892C;
    func_8C107AAA();
    D_8C1BDFDC = 0;
    func_8C07DEC0();
    func_8C051760(2, 2);
    func_8C07EC90();
    func_8C63FAE8();
    func_8C60526A(0xFF000000, 0xFF000000, 0xFF000000);
    w->unk0 = 2;
    w->unk1 = 0;
    if (time == 0) {
        w->unk4 = 180;
    } else {
        w->unk4 = time;
    }
    w->unk8 = &D_8C1BEF54;
    stg = GetStageNumber();
    w->unk2 = (stg & 0xFF00) >> 8;
    lang = &B_8C754BFC;
    tblA = &D_8C1BEF00;
    tblB = &D_8C1BEF40;
    if (w->unk2 == 0) {
        if (*lang == 0) {
            val = func_8C082A66(tblA, w->unk2);
        } else {
            val = func_8C082A66(tblB, w->unk2);
        }
        w->unk3 = 2;
    } else if (w->unk2 > 0 && w->unk2 <= 12) {
        idx = D_8C1BEF5C[GetPlayerNumber()];
        if (idx == -1) {
            return -1;
        }
        if (*lang == 0) {
            val = func_8C082A66(&D_8C1BEEC8[idx], w->unk2);
        } else {
            val = func_8C082A66(&D_8C1BEF08[idx], w->unk2);
        }
        w->unk3 = 0;
    } else if (w->unk2 >= 26 && w->unk2 <= 34) {
        if (*lang == 0) {
            val = func_8C082A66(&D_8C1BEEF8, w->unk2);
        } else {
            val = func_8C082A66(&D_8C1BEF38, w->unk2);
        }
        w->unk3 = 1;
    } else if (w->unk2 >= 35 && w->unk2 <= 38) {
        if (*lang == 0) {
            val = func_8C082A66(tblA, w->unk2);
        } else {
            val = func_8C082A66(tblB, w->unk2);
        }
        w->unk3 = 2;
    } else {
        return -1;
    }
    if (val == 0) {
        return -1;
    }
    *w->unk8->unk0 = val;
    idx = func_8C608C82(w->unk8);
    if (idx == -1) {
        return -1;
    }
    return 0;
}

Sint32 func_8C082C5A(void) {
    UNK_8C79892C_t *w;
    Sint32 ret;

    w = &B_8C79892C;
    ret = 0;
    njSetTexture(w->unk8);
    switch (w->unk3) {
    case 0:
        func_8C07E0AE(0x04000000, 352.0f, 240.0f, 1000.0f);
        break;
    case 1:
        func_8C07E0AE(0x04000000, 320.0f, 240.0f, 1000.0f);
        break;
    case 2:
        func_8C07E0AE(0x04000000, 328.0f, 240.0f, 1000.0f);
        break;
    }
    if (w->unk1++ > w->unk4 && D_8C161CB0 == 0) {
        func_8C6088BC(w->unk8);
        ret = 1;
        D_8C1BDFDC = 1;
    }
    return ret;
}
