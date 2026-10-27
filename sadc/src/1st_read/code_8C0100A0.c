#include "common.h"

/*
 * Startup: fill 0x8C00C000-0x8C00F400 with "SEGA", initialise the C sections
 * (clear B, copy D from ROM) and call the system/library init functions.
 *
 * NOTE: this translation unit was NOT compiled with -speed. With the project
 * flags (-speed) SHC inlines func_8C0100C8/func_8C0100D0 into func_8C0100A0
 * and rotates the loops of func_8C0100D8 (test duplicated at the top). With
 * --flags="-define=NON_MATCHING -nospeed" (or -size) all of the code below is
 * byte-identical to the original:
 *   func_8C0100D8 MATCH; func_8C0100A0 identical except that shcdiff cannot
 *   resolve its tail call BRA to func_8C0100D0, which is missing from the
 *   split/symbol list; func_8C0100C8 as split (8C0100C8-8C0100D8) is really
 *   two functions, func_8C0100C8 (8C0100C8) and func_8C0100D0 (8C0100D0).
 * Kept under NON_MATCHING until the build supports per-file compiler flags
 * and func_8C0100D0 is added to the split.
 */

#ifdef NON_MATCHING

extern char *D_8C66C3AC;            /* library version string pointer */
extern void *D_8C66C2C0;

/* section address table (byte pointers) */
extern char *D_8C111194;            /* D section ROM image */
extern char *D_8C11119C;            /* D section RAM start */
extern char *D_8C1111A8;            /* D section RAM end */
extern char *D_8C1111AC;            /* B section start */
extern char *D_8C1111B8;            /* B section end */
extern char *D_8C66C2D0;            /* second D section ROM image */
extern char *D_8C66C2D8;            /* second D section RAM start */
extern char *D_8C66C2E4;            /* second D section RAM end */
extern char *D_8C66C2EC;            /* second B section start */
extern char *D_8C66C2F8;            /* second B section end */

extern void func_8C053940(void);
extern void func_8C5FF8B8(void *p);
extern void func_8C5FF936(void *p);

void func_8C0100C8(void);
void func_8C0100D0(void);
void func_8C0100D8(void);

void func_8C0100A0(void) {
    Uint32 *p;

    *(volatile char *)D_8C66C3AC;
    for (p = (Uint32 *)0x8C00C000; p < (Uint32 *)0x8C00F400; p++) {
        *p = 0x41474553;
    }
    func_8C0100D8();
    func_8C0100C8();
    func_8C053940();
    func_8C0100D0();
}

void func_8C0100C8(void) {
    func_8C5FF8B8(D_8C66C2C0);
}

void func_8C0100D0(void) {
    func_8C5FF936(D_8C66C2C0);
}

void func_8C0100D8(void) {
    char *p;
    char *q;

    for (p = D_8C1111AC; p < D_8C1111B8; p++) {
        *p = 0;
    }
    for (p = D_8C11119C, q = D_8C111194; p < D_8C1111A8; p++, q++) {
        *p = *q;
    }
    for (p = D_8C66C2EC; p < D_8C66C2F8; p++) {
        *p = 0;
    }
    for (p = D_8C66C2D8, q = D_8C66C2D0; p < D_8C66C2E4; p++, q++) {
        *p = *q;
    }
}

#endif /* NON_MATCHING */
