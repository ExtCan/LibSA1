/*
 * rt_texture.c - Ninja texture state (docs/libsa1_integration.md 4.2, 2.9):
 * njSetTexture 8C608CCC, njSetTextureNum 8C608D14, texlist load 8C608C82 and
 * release 8C6088BC.
 *
 * Clean-room: written from the observed behaviour of the SDK functions,
 * verified against them in the harness where they are comparable (README.md).
 *
 * libsa1 has no texture memory: a texlist "load" succeeds without uploading
 * anything and leaves NJS_TEXNAME.texaddr as the game data has it (0 for the
 * texlists in 1ST_READ), a release succeeds too.  The Ninja-level state stays
 * exact, at the SDK's own addresses, because game code reads it:
 *   D_8C673480   current texlist (njSetTexture; read by game renderers)
 *   D_8C67348C / D_8C673490   the texture-memory key of the current texture
 *                (njSetTextureNum; the asm renderers reset D_8C67348C to -2)
 *   B_8C88F7A8   the texture error record {index, -1, empty-list flag, 0, 0}
 * Mesh capture (design 7.2) reads the current texlist from D_8C673480 and
 * gets the texture number through the capture hook in njSetTextureNum.
 */
#include "rt.h"

extern NJS_TEXLIST *D_8C673480;         /* current texlist */
extern Uint32 D_8C67348C;               /* current texture: texture-memory word 0 */
extern Uint32 D_8C673490;               /* current texture: texture-memory word 1 */
extern Sint32 B_8C88F7A8[5];            /* texture error record (texlist load / release) */
extern Uint32 *B_8C88FCB4;              /* current Kamui vertex context (0xC0 bytes, one of B_8C88F834[]) */

#define RT_KC_TEXTSP  (0x98 / 4)        /* context word: low 6 bits = the texture's TSP size bits */
#define RT_KC_TEXTCW  (0x9C / 4)        /* context word: texture control word */

/* NULL pointers read as zeros and drop stores, as the DC's boot ROM area does
 * in the harness (its NULL page): the Kamui context pointer stays 0 until the
 * SDK's Kamui init, which the reference simulator never runs and libsa1 does
 * not have (rt_draw.c, 1.) */
#define RT_LD(p, i)    ((p) != NULL ? (p)[i] : 0u)
#define RT_ST(p, i, v) do { Uint32 *rt_p_ = (p); if (rt_p_ != NULL) rt_p_[i] = (v); } while (0)

/* Kamui below Ninja: the context -> TA parameter words (8C655620).  libsa1
 * has no Kamui: nothing to do (the capture records the Ninja-level state).
 * The harness still has the original Kamui code in the image and runs it, so
 * diff and the routed traces see the same RAM as with the original. */
#if defined(TARGET_PC_HARNESS)
extern void func_8C655620(Uint32 *ctx);
#define RT_KM_CONTEXT(ctx) func_8C655620(ctx)
#else
#define RT_KM_CONTEXT(ctx) ((void)0)
#endif

/* ---- draw capture hook (design 7.2; NOT IMPLEMENTED YET) ----
 * RT_TEX_CAPTURE(n) reports the texture number selected by njSetTextureNum to
 * the capture back end (world core, Phase 6).  Until it exists, nothing. */
#ifndef RT_TEX_CAPTURE
#define RT_TEX_CAPTURE(n) ((void)(n))
#endif

/* 8C608CCC njSetTexture: NULL -> -1 (nothing stored), else current = texlist, 1 */
Sint32 njSetTexture(NJS_TEXLIST *texlist)
{
    if (texlist == NULL) {
        return -1;
    }
    D_8C673480 = texlist;
    return 1;
}

/* 8C608D14 njSetTextureNum: select texture n of the current texlist.
 * tm = textures[n].texaddr, the texture-memory record {key0, key1, tsp, tcw}:
 *   same key as the current texture -> 1, nothing stored;
 *   else key -> D_8C67348C / D_8C673490, the TSP size bits (tsp & 63) into
 *   the context, then tcw (unless (tcw & 0x1FFFFF) > 0x100000: -1, the TCW
 *   left as it was) and the Kamui context update; 1.
 * An entry no load gave texture memory (texaddr 0: libsa1, see above) reads
 * as an all-zero record, the value the harness's NULL page gives the original.
 * The texlist itself must exist (it always does once njSetTexture ran). */
Sint32 njSetTextureNum(Uint32 n)
{
    Uint32 zero[4] = { 0, 0, 0, 0 };
    const Uint32 *tm;
    Uint32 tcw;

    RT_TEX_CAPTURE(n);
    tm = (const Uint32 *)D_8C673480->textures[n].texaddr;
    if (tm == NULL) {
        tm = zero;
    }
    if (tm[0] == D_8C67348C && tm[1] == D_8C673490) {
        return 1;
    }
    D_8C67348C = tm[0];
    D_8C673490 = tm[1];
    RT_ST(B_8C88FCB4, RT_KC_TEXTSP, (RT_LD(B_8C88FCB4, RT_KC_TEXTSP) & ~63u) | (tm[2] & 63u));
    tcw = tm[3];
    if ((tcw & 0x1FFFFFu) > 0x100000u) {
        return -1;
    }
    RT_ST(B_8C88FCB4, RT_KC_TEXTCW, tcw);
    RT_KM_CONTEXT(B_8C88FCB4);
    return 1;
}

/* 8C608C82 texlist load: resets the error record to {-1, -1, 0, 0, 0}; an
 * empty texlist sets its flag word and fails (-1); otherwise every texture
 * "loads" (no texture memory in libsa1): 1 */
Sint32 func_8C608C82(NJS_TEXLIST *texlist)
{
    B_8C88F7A8[0] = -1;
    B_8C88F7A8[1] = -1;
    B_8C88F7A8[2] = 0;
    B_8C88F7A8[3] = 0;
    B_8C88F7A8[4] = 0;
    if (texlist->nbTexture == 0) {
        B_8C88F7A8[2] = 1;
        return -1;
    }
    return 1;
}

/* 8C6088BC texlist release: every texture releases (nothing was uploaded): 1 */
Sint32 func_8C6088BC(NJS_TEXLIST *texlist)
{
    (void)texlist;
    return 1;
}
