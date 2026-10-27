/*
 * link_test.c - LIB/build.py's link test (sadc docs/libsa1_integration.md, build integration).
 *
 * Static variant (-DSA1_LINK_TEST_STATIC): linked with -Wl,--whole-archive libsa1.a, so EVERY object of
 * the archive (lowered game code, runtime, code map, stubs, world core, asset layer) is part of the
 * executable: an unresolved symbol anywhere fails the link. At run time it checks the code map: the
 * character mains, the functions of the libsa1 step (design 6.4), some runtime entries and a stub must
 * resolve by DC address (-DSA1_LINK_TEST_FULL: the whole game is linked; a subset such as the phase 1 spike
 * only reports how many resolve). Overlay functions are looked up in their image's own code map (sadc
 * docs/libsa1_integration.md 2.5: pclower's sa1g_fnimages[]; outside a world only the base map is active).
 * Shared variant: linked against sa1.dll / libsa1.so; calls exported API functions only.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libsa1.h"

#if defined(SA1_LINK_TEST_STATIC)
int sa1g_game_linked(void);
const char *sa1g_game_info(void);
const char *sa1_fn_name(uint32_t g);

/* the overlay images of the code map (pclower's sa1g_fntab.c; the layouts of sa1/guest.h sa1g_fnent and
 * src/world/sa1_world.h sa1g_fnimage) */
typedef struct lt_fnent {
    uint32_t key, addr, sig;
    void (*fn)(void);
    const char *name;
} lt_fnent;
typedef struct lt_fnimage {
    const char *name, *prs, *prs_sha1;
    uint32_t lo, hi;
    const lt_fnent *(*lookup)(uint32_t g);
    const lt_fnent *table;
    size_t count;
} lt_fnimage;
extern const lt_fnimage sa1g_fnimages[];
extern const size_t sa1g_fnimage_count;

static const char *image_fn_name(const char *image, uint32_t g)
{
    size_t k;
    for (k = 0; k < sa1g_fnimage_count; k++) {
        if (strcmp(sa1g_fnimages[k].name, image) == 0) {
            const lt_fnent *e = sa1g_fnimages[k].lookup(g);
            return e != NULL ? e->name : NULL;
        }
    }
    return NULL;
}

static const struct {
    uint32_t addr;
    const char *what;
    const char *image;          /* NULL: the base map (1ST_READ, the runtime) */
} k_need[] = {
    { 0x8C0A77BCu, "SonicMain", NULL }, { 0x8C0B6148u, "TailsMain", NULL }, { 0x8C0C0640u, "KnucklesMain", NULL },
    { 0x8C0CBE00u, "E102Main", NULL }, { 0x8C0D6B40u, "AmyMain", NULL }, { 0x8C0DFFA0u, "BigMain", NULL },
    { 0x8C050BE4u, "pad update", NULL }, { 0x8C0988A4u, "executor", NULL }, { 0x8C0342E0u, "CCL", NULL },
    { 0x8C028E60u, "land init", NULL }, { 0x8C028EC2u, "land update", NULL }, { 0x8C0988C4u, "display pass", NULL },
    { 0x8C098A10u, "heap init", NULL }, { 0x8C09844Au, "task init", NULL }, { 0x8C098786u, "FreeTask", NULL },
    { 0x8C63A9B8u, "njSin (rt)", NULL }, { 0x8C639C70u, "njPushMatrix (rt)", NULL }, { 0x8C10D0F8u, "atan2f (rt)", NULL },
    { 0x8C601874u, "pdGetPeripheral (rt)", NULL }, { 0x8C0940DAu, "play SE (stub: event)", NULL },
    { 0x0CB00000u, "TIKAL_PROG", "TIKAL_PROG" }, { 0x0C909FDAu, "the Emerald Coast ocean", "STG01" },
    { 0x0C9061DAu, "the Emerald Coast footprints", "STG01" }, { 0x0C911E0Au, "the Red Mountain level task", "STG05" },
    { 0x0C9131AAu, "an STG05 image stub", "STG05" },
};
#endif

int main(void)
{
    int bad = 0;
    printf("libsa1 %s, API %u.%u\n", sa1_version_string(), (unsigned)(SA1_API_VERSION >> 16),
           (unsigned)(SA1_API_VERSION & 0xFFFFu));
    if (strcmp(sa1_status_string(SA1_OK), "") == 0) {
        bad = 1;
    }
#if defined(SA1_LINK_TEST_STATIC)
    printf("game: %s\n", sa1g_game_info());
    if (sa1g_game_linked()) {
        size_t i, found = 0;
        for (i = 0; i < sizeof k_need / sizeof k_need[0]; i++) {
            const char *n = k_need[i].image ? image_fn_name(k_need[i].image, k_need[i].addr)
                                            : sa1_fn_name(k_need[i].addr);
            if (n == NULL) {
#if defined(SA1_LINK_TEST_FULL)
                printf("code map: %08X (%s) MISSING\n", (unsigned)k_need[i].addr, k_need[i].what);
                bad = 1;            /* the whole game is linked: every entry must be there */
#endif
            } else {
                found++;
            }
        }
        printf("code map: %u/%u required entries resolve\n", (unsigned)found,
               (unsigned)(sizeof k_need / sizeof k_need[0]));
    }
#endif
    printf("link test %s\n", bad ? "FAILED" : "ok");
    return bad;
}
