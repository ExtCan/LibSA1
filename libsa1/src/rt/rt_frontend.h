/*
 * rt_frontend.h - the declarations of the runtime functions of the game's own front end (round 0.9.8, sadc docs/research/charsel098.md 5.3).
 * Included by rt_draw.c and rt_misc.c only: never rt.h, whose owner is another lane (gsa1_098_plan.md 2.4 C12). libsa1 profile only: the
 * harness keeps the original SDK code at these addresses.
 */
#ifndef RT_FRONTEND_H
#define RT_FRONTEND_H

#if !defined(TARGET_PC_HARNESS)
void func_8C63ED08(void *vtx, Sint32 n, Sint32 tex, Sint32 trans);          /* njDrawTexture: rt_draw.c */
void func_8C63EC58(void *vtx, Sint32 n, Sint32 trans);                      /* njDrawPolygon: rt_draw.c */
Sint32 func_8C602B68(Sint32 a, Sint32 b, void *work, void (*cb)(void));     /* buInit: rt_misc.c */
#endif

#endif /* RT_FRONTEND_H */
