/*
 * sa1_world_nogame.c - the code map of a libsa1 build without lowered game code.
 *
 * LIB/build.py links this file only when no lowered game code is linked; otherwise pclower's
 * sa1g_fntab.c defines sa1_fn / sa1_fn_addr / sa1_fn_name and the build generates sa1g_game_info.c.
 * Every indirect call is then a code-map fault, and the tick reports SA1_ERR_NOT_IMPLEMENTED before
 * running anything (sa1g_game_ready).
 */
#include "sa1_world.h"

void *sa1_fn(sa1_gfn g, uint32_t sig)
{
    sa1g_fault_fn(g.v, sig);
}

uint32_t sa1_fn_addr(const void *native)
{
    (void)native;
    return 0u;
}

const char *sa1_fn_name(uint32_t g)
{
    (void)g;
    return (const char *)0;
}

int sa1g_game_linked(void)
{
    return 0;
}

const char *sa1g_game_info(void)
{
    return "no game code linked";
}

/* no overlay image has a code map (sadc docs/libsa1_integration.md 2.5) */
const sa1g_fnimage sa1g_fnimages[1] = { { "", "", "", 0u, 0u, NULL, NULL, 0u } };
const size_t sa1g_fnimage_count = 0;

const sa1g_fnent *sa1g_fn_image_lookup(uint32_t g)
{
    (void)g;
    return NULL;
}

uint32_t sa1g_pcd_size(void)
{
    return 0u;
}

void sa1g_pcd_init(void)
{
}
