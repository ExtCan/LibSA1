/*
 * 1ST_READ chunk 8C0548E0 (part): func_8C055C44, B_8C788C34 = 0 (tail of the
 * frame-rate setter 8C051760 and of 8C05FB74; run by the menu init,
 * gsa1_090_charselect.md review RC5). Meaning of B_8C788C34 UNVERIFIED.
 */
#include "sa1/sa1.h"

extern Sint32 B_8C788C34;

/* The 2D picture work 8C055C22 releases (only the fields it touches; the
 * same head as the texture viewer of 8C07EF88: a texlist embedded at +0x28) */
typedef struct {
    Uint8 pad00[0x20];
    Sint32 state;               /* 0x20 cleared */
    Uint8 pad24[0x28 - 0x24];
    NJS_TEXLIST texlist;        /* 0x28 released when loaded */
} PICTWK;

void func_8C055C22(PICTWK *p)
{
    if (p->texlist.textures != NULL) {
        func_8C6088BC(&p->texlist);
        p->texlist.textures = NULL;
    }
    p->state = 0;
}

void func_8C055C44(void)
{
    B_8C788C34 = 0;
}
