/*
 * 1ST_READ 8C09E2A0 (chunk 8C09E2A0-8C09E340): creation of the effect
 * (particle) lists from definition tables.
 *
 *  func_8C09E2A0  walk a NULL-terminated array of effect definitions: create
 *                 each list with func_8C09DE3A(desc, extra), store it in the
 *                 definition and call its init callback (if any) with it
 *  func_8C09E2DA  create the common effect lists (D_8C1C40C4) and those of
 *                 the current stage (D_8C1C4140[level], may be NULL)
 */
#include "common.h"
#include "sa1/sa1.h"

/* Effect list (CAlloc(1, 0x1C + extra) by func_8C09DE3A; full layout in
 * code_8C09DDFC.c); opaque here */
struct sa1_effect_list;

/* Effect list definition (0x10 bytes): e.g. D_8C1C42E4 = {&D_8C1C4294, 0,
 * 0} followed by its list slot D_8C1C42F0; D_8C4BB1B4 = {desc, 0, 0} with the
 * list in D_8C4BB1C0 (func_8C0D68FC) */
typedef struct sa1_effect_def {
    void *desc;                 /* DC 0x00 sprite descriptor */
    Uint32 extra;               /* DC 0x04 extra work bytes of the list */
    void (*init)(struct sa1_effect_list *list); /* DC 0x08 called with the new list, or NULL */
    struct sa1_effect_list *list; /* DC 0x0C created list */
} SA1_EFFECT_DEF;               /* DC 0x10 */

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT_DEF, 0x10);
SA1_ASSERT_OFFSET(SA1_EFFECT_DEF, init, 0x08);
SA1_ASSERT_OFFSET(SA1_EFFECT_DEF, list, 0x0C);
#endif

extern SA1_EFFECT_DEF *D_8C1C40C4[];    /* common effect lists, NULL-terminated */
extern SA1_EFFECT_DEF **D_8C1C4140[];   /* per level: NULL-terminated definition arrays, or NULL */

struct sa1_effect_list *func_8C09DE3A(void *desc, Uint32 extra);

void func_8C09E2A0(SA1_EFFECT_DEF **defs)
{
    SA1_EFFECT_DEF *def;

    while (*defs != NULL) {
        def = *defs;
        def->list = func_8C09DE3A(def->desc, def->extra);
        if (def->init != NULL) {
            def->init(def->list);
        }
        defs++;
    }
}

void func_8C09E2DA(void)
{
    Sint32 level;
    SA1_EFFECT_DEF **defs;

    func_8C09E2A0(D_8C1C40C4);
    level = (GetStageNumber() & 0xFF00) >> 8;
    if ((defs = D_8C1C4140[(Uint16)level]) != NULL) {
        func_8C09E2A0(defs);
    }
}
