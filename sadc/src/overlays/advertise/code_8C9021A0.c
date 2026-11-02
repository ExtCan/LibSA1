/*
 * ADVERTISE 8C9021A0-8C902452: the menu's texture archives (chunk 8C9021A0,
 * all 11 functions). Each of the 26 menu PVMs has a bit in D_8C9610DC; the
 * PVM table D_8C942628[language] holds {file name, texlist} per PVM, and
 * D_8C94263C[screen] lists the PVMs a screen needs (ending with 26).
 *
 *  func_8C9021A0  clears all bits
 *  func_8C9021BE  is PVM n loaded?
 *  func_8C9021F0  marks PVM n loaded
 *  func_8C902224  marks PVM n released
 *  func_8C90225A  loads every PVM of a list (8C099750)
 *  func_8C9022D2  releases every loaded PVM of a list (8C6088BC)
 *  func_8C902326  loads the common PVMs (D_8C90F1A0); sound start allowed
 *  func_8C90233C  releases all 26
 *  func_8C90236E  loads the PVMs of a screen; holds the sound start while
 *                 the previous screen's PVMs are still loaded
 *  func_8C902396  is n in the list?
 *  func_8C9023E0  releases the previous screen's PVMs that the current one
 *                 does not use, then allows the sound start (callers pass
 *                 1, which it ignores)
 */
#include "common.h"
#include "sa1/sa1.h"

#define ADV_PVM_END 26

typedef struct ADV_PVM {
    const char *name;           /* 0x00 file name */
    NJS_TEXLIST *texlist;       /* 0x04 */
} ADV_PVM;                      /* 0x08 */

/* master task work (task.awp; func_8C90170E) */
typedef struct ADV_MASTERWK {
    void *seq;                  /* 0x00 */
    Sint32 mode;                /* 0x04 current screen */
    Sint32 prev;                /* 0x08 previous screen */
    Sint32 screen;              /* 0x0C */
    Sint32 next;                /* 0x10 */
    Sint32 result;              /* 0x14 */
    Sint32 req;                 /* 0x18 */
    Sint16 fade;                /* 0x1C */
    Sint8 fadereq;              /* 0x1E */
    Uint8 ready;                /* 0x1F */
    Sint8 b20;                  /* 0x20 */
    Sint8 b21;                  /* 0x21 */
} ADV_MASTERWK;                 /* 0x24 */

#define MASTERWK ((ADV_MASTERWK *)D_8C9610CC->awp)

extern task *D_8C9610CC;        /* the master task */
extern Sint32 D_8C9610D8;       /* sound start allowed */
extern Uint32 D_8C9610DC[2];    /* loaded PVMs, one bit each */
extern ADV_PVM *D_8C942628[];   /* PVM table per language */
extern Sint32 *D_8C94263C[];    /* PVM list per screen */
extern Sint32 D_8C90F1A0[];     /* PVMs every screen uses */
extern Sint32 B_8C754BFC;       /* language */

void func_8C099750(const char *name, NJS_TEXLIST *texlist);     /* load a PVM */

void func_8C9021A0(void)
{
    Sint32 i;

    for (i = 0; i < 2; i++) {
        D_8C9610DC[i] = 0;
    }
}

Uint32 func_8C9021BE(Sint32 n)
{
    Sint32 i;
    Sint32 b;

    i = n / 32;
    b = n % 32;
    return D_8C9610DC[i] & (1 << b);
}

void func_8C9021F0(Sint32 n)
{
    Sint32 i;
    Sint32 b;

    i = n / 32;
    b = n % 32;
    D_8C9610DC[i] |= 1 << b;
}

void func_8C902224(Sint32 n)
{
    Sint32 i;
    Sint32 b;

    i = n / 32;
    b = n % 32;
    D_8C9610DC[i] &= ~(1 << b);
}

void func_8C90225A(Sint32 *list)
{
    Sint32 n;
    ADV_PVM *tbl;

    if (list != NULL) {
        while ((n = *list++) != ADV_PVM_END) {
            func_8C9021F0(n);
            tbl = D_8C942628[B_8C754BFC];
            func_8C099750(tbl[n].name, tbl[n].texlist);
        }
    }
}

void func_8C9022D2(Sint32 *list)
{
    Sint32 n;
    ADV_PVM *tbl;

    if (list != NULL) {
        while ((n = *list++) != ADV_PVM_END) {
            if (func_8C9021BE(n)) {
                func_8C902224(n);
                tbl = D_8C942628[B_8C754BFC];
                func_8C6088BC(tbl[n].texlist);
            }
        }
    }
}

void func_8C902326(void)
{
    func_8C9021A0();
    func_8C90225A(D_8C90F1A0);
    D_8C9610D8 = 1;
}

void func_8C90233C(void)
{
    Sint32 list[ADV_PVM_END + 1];
    Sint32 i;

    for (i = 0; i < ADV_PVM_END; i++) {
        list[i] = i;
    }
    list[i] = ADV_PVM_END;
    func_8C9022D2(list);
}

void func_8C90236E(Sint32 screen)
{
    func_8C90225A(D_8C94263C[screen]);
    if (MASTERWK->prev != 0) {
        D_8C9610D8 = 0;
    }
}

Sint32 func_8C902396(Sint32 *list, Sint32 n)
{
    Sint32 found;

    found = 0;
    while (list != NULL && *list != ADV_PVM_END) {
        if (*list == n) {
            found = 1;
        }
        list++;
    }
    return found;
}

/*
 * NON_MATCHING: the original keeps the master work pointer in a stack slot
 * (MOV.L R6,@R15, frame 112: probably the return value of an inlined
 * accessor); a static inline accessor reproduces the frame but not the
 * register choice. Tried declaration orders, an array / struct for w, four
 * accessor shapes.
 */
#ifdef NON_MATCHING
void func_8C9023E0(Sint32 unused)
{
    Sint32 list[ADV_PVM_END + 1];
    Sint32 *cur;
    Sint32 *p;
    Sint32 *out;
    ADV_MASTERWK *w;

    w = MASTERWK;
    if (w->mode > 0 && w->mode < 14) {
        cur = D_8C94263C[MASTERWK->mode];
    } else {
        cur = NULL;
    }
    out = list;
    for (p = D_8C94263C[MASTERWK->prev]; *p != ADV_PVM_END; p++) {
        if (!func_8C902396(cur, *p)) {
            *out++ = *p;
        }
    }
    *out = ADV_PVM_END;
    func_8C9022D2(list);
    D_8C9610D8 = 1;
}
#else
ASM_STUB(void func_8C9023E0(Sint32 unused))
#endif
