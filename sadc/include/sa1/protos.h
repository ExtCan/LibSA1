#ifndef SA1_PROTOS_H
#define SA1_PROTOS_H

/*
 * Canonical prototypes of game functions that translation units declared
 * with different C signatures (pclower rule R8: the cross-TU check of
 * build/pclower/survey/report.json "prototype_conflicts"). Each prototype
 * was decided from the original machine code: the argument registers the
 * callee reads and how it extends them, the registers its callers set, and
 * whether R0/FR0 is returned and used. The evidence for every function, and
 * the SDK and core-header ones, is in build/verify/canonical_protos.json.
 *
 * SHC 5.1r03 behaviour behind the choices (checked with test compiles):
 *  - a char/short parameter is extended by the callee (EXTU.B/EXTS.B/EXTU.W
 *    at entry, or a MOV.B/MOV.W spill); callers do not extend narrow
 *    arguments. A conversion at a call site can still change SHC's register
 *    allocation (e.g. Sint32 -> Uint32 in func_8C082F80), so every matched
 *    caller and definition whose declaration has to change was recompiled
 *    with the canonical prototype;
 *  - a char/short result is extended by the caller (EXTU.W/EXTS.B R0);
 *  - void and Sint32 results give the same code in callers that ignore them;
 *  - the last named parameter of a variadic prototype goes on the stack.
 * Parameters that the callee never reads but every caller passes (mwp/pwp of
 * player functions, attr of the land probes) belong to the prototype: adding
 * them to the matched definitions leaves the SHC output unchanged (tested for
 * each one). Arguments whose order differs only between float and integer
 * registers follow the matched code (SHC evaluates arguments right to left).
 * Where one matched file needs a different declaration for its own code
 * (GetStageNumber in code_8C015720.c), that file keeps it for SHC only and
 * uses the canonical prototype under TARGET_PC, which is what pclower sees.
 *
 * Not included by sa1/sa1.h yet: as long as a src file still declares one of
 * these functions differently, SHC rejects the translation unit (2118
 * Prototype mismatch). A src file includes "sa1/protos.h" once its local
 * declarations agree with it or are deleted. When no src file disagrees any
 * more, the declarations move into the core headers (player functions into
 * sa1/player.h, collision into sa1/ccl.h and sa1/collision.h, ...) and this
 * header is folded into sa1/sa1.h. A struct that is still file-local in every
 * user (animal work, AI work, probe structs) is passed as void * here.
 */

#include "sa1/sa1.h"

/* ---- stage files (8C0141E0) ---- */
Sint32 func_8C014242(Uint16 n, Sint32 m);                               /* R4,R5 -> R0: load "SL_<n><m>B.bin" (falls back to m = 0); EXTU.W R4 */
Sint32 func_8C014308(Uint16 n, Sint32 m);                               /* R4,R5 -> R0: load "PL_<n><m>B.bin"; EXTU.W R4 */

/* ---- objects, animals, camera (8C015820-8C01AD2C) ---- */
taskwk *func_8C015820(Float x, Float y, Float z, TaskFuncPtr exec);     /* FR4-FR6,R4 -> R0: CreateElementalTask(3, 4, exec) at (x, y, z), returns its taskwk or NULL */
void func_8C017360(void *wk, taskwk *twp, task *tp, task *tp2, ...);    /* R4-R6 + stack: animal dispatch; callers push a 4th argument (variadic: last named one on the stack) */
void func_8C01AD2C(void (*func)(void), Sint8 adjust, Sint8 direct);     /* R4-R6: set the camera function (stored MOV.L) and two signed bytes (MOV.B) */

/* ---- landtable probes (hand-written module, code_8C02D060.c) ---- */
Sint32 func_8C02D060(sa1_zxsdwstr *zssp, NJS_OBJECT *object, Uint32 attr); /* R4,R5 -> R0; R6 = attr is passed by every caller but not read */
Sint32 func_8C02D2E0(sa1_zxsdwstr *zssp, NJS_OBJECT *object, Uint32 attr); /* R4,R5 -> R0; R6 = attr is passed by every caller but not read */

/* ---- CCL collision (8C02E820-8C034940) ---- */
Sint8 func_8C02FB3A(colliwk *cwp1, CCL_INFO *ci1, colliwk *cwp2, CCL_INFO *ci2); /* R4-R7 -> R0: 1 = the id masks allow the hit (sets cwp1->flag 0x80); callers EXTS.B R0 */
void func_8C031F88(colliwk *cwp1, colliwk *cwp2);                       /* R4,R5: shape pair test, no result */
void func_8C032406(colliwk *cwp1, colliwk *cwp2);                       /* R4,R5: shape pair test, no result */

/* ---- power-up effects (chunks 8C043C00, 8C044480; landed 0.9.2) ---- */
void func_8C043C00(taskwk *twp, taskwk *ptwp);                          /* R4,R5: power-up effects follow the player (scl, pos at 0.6 * height) */
void func_8C043D66(task *tp);                                           /* magnetic barrier spark exec */
void func_8C043E70(task *tp);                                           /* magnetic barrier spark disp */
void func_8C043FEC(task *tp);                                           /* magnetic barrier dest (empty) */
void func_8C044122(task *tp);                                           /* barrier spark exec */
void func_8C044166(task *tp);                                           /* barrier spark disp */
void func_8C044280(task *tp);                                           /* barrier / invincibility dest (empty) */
void func_8C044290(task *tp);                                           /* invincibility disp */
void func_8C04430A(task *tp);                                           /* invincibility exec: item 0x8000 for 0x4EC frames, then FreeTask */
void func_8C044480(task *tp);                                           /* invincibility spark exec */
void func_8C0444C4(task *tp);                                           /* invincibility spark disp */

/* ---- ground item box O_ITEMBOX (chunks 8C05D0A0, 8C05D760; landed 0.9.4, build/staging/itembox_ground/LANDING.md 3) ---- */
void func_8C05D0A0(NJS_POINT3 *pos, Sint32 item);                       /* spawn a ground item box (no SET entry), mode 1 (caller 8C066AA6) */
void objexec_O_ITEMBOX(task *tp);                                       /* ground item box exec (SET type 13), item kind in twp->scl.x */
void func_8C05D360(taskwk *twp);                                        /* mode 1: grow in (scl.z += 0.11 up to 5) */
void func_8C05D386(task *tp);                                           /* mode 2: break test (lid / hurt), item give, panel spin */
void func_8C05D4CE(task *tp);                                           /* mode 3: condition 0x0004, debris, shell collision (CCL id 4) */
void func_8C05D56E(task *tp);                                           /* disp: whole box with the item panel */
void func_8C05D67E(task *tp);                                           /* disp: broken box (base only) */
void func_8C05D6AC(taskwk *twp);                                        /* break debris (one particle at y + 5) */
void func_8C05D760(task *tp);                                           /* dest (empty) */
/* ---- SET condition helpers (chunk 8C090FA0) ---- */
void func_8C091B54(task *tp);                                           /* ocp->ssCondition |= 4 (used up, stays in place) */
Sint32 func_8C091B68(task *tp);                                         /* ocp->ssCondition & 4 */

/* ---- air item box O_ItemBoxAir (chunks 8C05D764, 8C05D900, 8C05D9C0; landed 0.9.2) ---- */
void func_8C05D764(taskwk *twp);                                        /* item 0 handler (table D_8C1B9BA0): speed shoes to the player that broke the box */
void func_8C05D7A6(taskwk *twp);                                        /* item 2: 5 rings */
void func_8C05D7D6(taskwk *twp);                                        /* item 3: 10 rings */
void func_8C05D806(taskwk *twp);                                        /* item 4: random rings (D_8C154C40, kept in B_8C78C37C) */
void func_8C05D854(taskwk *twp);                                        /* item 6: 1-up */
void func_8C05D884(taskwk *twp);                                        /* item 1: invincibility */
void func_8C05D900(taskwk *twp);                                        /* item 5: barrier */
void func_8C05D930(taskwk *twp);                                        /* item 7: bomb (tail call of func_8C05E582(&twp->pos)) */
void func_8C05D964(taskwk *twp);                                        /* item 8: magnetic barrier */
void func_8C05DC60(task *tp);                                           /* air item box display */
void func_8C05DDBE(NJS_OBJECT *obj);                                    /* node draw (draw list D_8C1B9C4C): constant material during the break */
void func_8C05DEA0(NJS_OBJECT *obj);                                    /* item panel node draw (D_8C1B9C3C / D_8C1B9C4C): item and 1-up icon texture ids */
void func_8C05E0CE(void);                                               /* create the item icon list task (last call of the stage start 8C058E48) */
void func_8C05E14E(task *tp);                                           /* item icon list exec (tail BRA to func_8C05E59A) */
void func_8C05E1E0(void);                                               /* item icon list mode 1: slide in */
void func_8C05E28E(void);                                               /* item icon list mode 2: hold 60 frames */
void func_8C05E2EE(void);                                               /* item icon list mode 3: shrink, then empty the list */
void func_8C05E33C(task *tp);                                           /* item icon list disp */
void func_8C05E3A0(Sint32 kind, Float x, Sint32 value, Float scl);      /* R4,FR4,R5,FR5: draw one item icon (2D sprite) */
void func_8C05E57E(task *tp);                                           /* item icon list dest (empty) */
void func_8C05E582(NJS_POINT3 *pos);                                    /* bomb: start the sphere at pos, radius 10 */
void func_8C05E59A(task *tp);                                           /* bomb: sphere step (+10 per frame, off above 500) */
Sint32 func_8C05E5C2(NJS_POINT3 *pos);                                  /* R4 -> R0: 1 when pos is inside the bomb sphere (enemy test of 8C0486A0) */

/* ---- engine helpers ---- */
void func_8C05695C(Uint32 a, Uint32 b);                                 /* R4,R5: clear the bits a / b in two 32-bit flag globals (NOT/AND) */
void func_8C057586(NJS_POINT3 *from, NJS_POINT3 *to, Float dist, NJS_POINT3 *out); /* R4-R6,FR4: point at distance dist from 'from' towards 'to' (the float is third) */

/* ---- player common (8C06C0C0-8C07DEC0); unused mwp/pwp are passed by every caller ---- */
Sint32 func_8C06CA30(taskwk *twp, motionwk *mwp, playerwk *pwp);        /* R4 (R5,R6 unused) -> R0: hit object's taskwk id is 7 */
Bool func_8C06CE40(taskwk *twp, motionwk *mwp);                         /* R4 (R5 unused) -> R0: event lock released */
Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp);                       /* R4 (R5 unused) -> R0: button pressed, 2 when 8C05E6E8 reports > 0, else 1 (0: not pressed) */
Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);                       /* R4 (R5 unused) -> R0: angle class 1..4 */
Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);                       /* R4 (R5 unused) -> R0: stick direction vs. CCL push-out direction, 0..4 */
Sint32 func_8C076782(taskwk *twp, motionwk *mwp, playerwk *pwp);        /* R4-R6 -> R0: carried by a moving land object; 1/2 slower/faster than run speed */
void func_8C07B2DC(Uint8 pno);                                          /* R4: EXTU.B */
void func_8C07B346(Uint8 pno, NJS_POINT3 *pos, Angle *ang);             /* R4,R5 (R6 unused): effect under the player; EXTU.B R4 */
void func_8C07BA0A(Sint32 se, Sint8 pno, Sint16 mode);                  /* R4-R6: create the sound task (R6 spilled MOV.W) */
void func_8C07BBC2(Uint8 pno);                                          /* R4: start the death sequence of player pno; EXTU.B */
void func_8C07C3F2(Uint8 pno, task *target, Uint32 time);               /* R4-R6: look at a task for a time; EXTU.B R4 */
void func_8C07C580(Uint8 pno, Float x, Float y, Float z);               /* R4,FR4-FR6: launch (smode 35); EXTU.B R4 */
void func_8C07C676(Uint8 pno, Float x, Float y, Float z);               /* R4,FR4-FR6: SetVelocityP; EXTU.B R4 */
void func_8C07C9D2(Uint8 pno, Angle x, Angle y, Angle z);               /* R4-R7: SetRotationP; EXTU.B R4 */
Sint32 func_8C07CE40(Uint8 pno);                                        /* R4 -> R0; EXTU.B R4 */

/* ---- objects (8C0826B0-8C08A198) ---- */
void func_8C0826B0(taskwk *twp, NJS_POINT3 *out, Float r);              /* R4,R5,FR4 (float last, as the matched callers pass it) */
void func_8C084074(Uint8 pno1, Uint8 pno2, Float f);                    /* R4,R5,FR4: both player numbers spilled MOV.B / EXTU.B */
Sint32 func_8C084118(void *awp);                                        /* R4 -> R0: flag word of an AI work (file-local struct) */
void func_8C084200(Sint32 a, NJS_POINT3 *p);                            /* R4,R5: tail-calls 8C07E0AE(a, p->x, p->y, p->z) */
task *func_8C085532(task *parent, Sint32 mode, NJS_POINT3 *pos);        /* R4,R5 (R6 = &twp->pos unused) -> R0: the created task */
Float func_8C085980(NJS_VECTOR *v, NJS_VECTOR *out);                    /* R4,R5 -> FR0 */
Float func_8C0859A0(NJS_VECTOR *v, NJS_VECTOR *out);                    /* R4,R5 -> FR0 */
void func_8C08776C(task *tp);                                           /* RTS/NOP, used as a task function */
void func_8C08A198(Sint16 n);                                           /* R4 (added and stored only, no extension) */

/* ---- SET-0 spawn path (chunk 8C090FA0; landed 0.9.4, build/staging/set0/LANDING.md 6) ---- */
void func_8C090FA0(task *tp);                                           /* SET manager exec: mode 0 init, 1 spawn, 2 act change */
void func_8C090FFE(taskwk *twp);                                        /* manager mode-1 tail: act-change request -> mode 2 */
void func_8C091016(void);                                               /* manager mode 0, stage-start init (asm) */
Sint32 func_8C0910C0(Float *pos, Float *p1, Float x, Float y, Float z, Float r); /* range test of 8C091148 (in range, or within 40 of player 1) */
Sint32 func_8C09111A(Float *pos, Float x, Float y, Float z, Float r);   /* range test of 8C091380; callers pass &entry->pos.x */
void func_8C091148(void);                                               /* spawner when a player 1 other than Tails exists */
void func_8C091380(void);                                               /* spawner (no player 1, player 1 Tails, event mode) */
void func_8C0915A2(void);                                               /* spawner choice, then D_8C1C0ED8 = 1 */
void func_8C0915D2(taskwk *twp);                                        /* manager mode 2, act change (asm) */

/* ---- system ---- */
Sint32 func_8C099500(void);                                             /* -> R0: 1 after njSetTexture(B_8C79F0F0) (8C0995C4 is the literal-pool slot holding njSetTexture's address, 8C608CCC), 0 when B_8C79F0F0 is NULL; the blob-shadow callers ignore R0 (code_8C099500.c) */
Sint32 func_8C099538(void);                                             /* -> R0: the same for B_8C79F0F4; the overlay callers skip their own njSetTexture when it returns 1 (code_8C099500.c) */

/* ---- characters (8C0A77A0-8C0EEAA0); unused mwp/pwp are passed by every caller ---- */
void func_8C0AF012(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* Sonic free movement (debug): R4 (R5,R6 unused) */
taskwk *func_8C0AF5A0(taskwk *twp, playerwk *pwp);                      /* Sonic homing target: R4,R5 -> R0 */
Sint32 func_8C0AFE72(taskwk *twp, motionwk *mwp, playerwk *pwp);        /* Sonic rotation helper: R4 (R5,R6 unused) -> R0 */
Bool func_8C0B27EE(Sint8 pno, NJS_OBJECT **objs);                       /* Sonic look-at task: R4 spilled MOV.B, R5 -> R0 */
void func_8C0C5680(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* Knuckles free movement (debug): R4 (R5,R6 unused) */
Bool func_8C0C896C(Sint8 pno, NJS_OBJECT **objs);                       /* Knuckles look-at task: R4 spilled MOV.B, R5 -> R0 */
void func_8C0CA780(NJS_POINT3 *pos, Sint32 n);                          /* Knuckles particles: R4 (R5 = 0 from the caller, unused) */
Sint32 func_8C0D06F4(taskwk *twp, motionwk *mwp, playerwk *pwp);        /* E-102 rotation helper: R4 (R5,R6 unused) -> R0 */
Sint32 func_8C0D1D76(taskwk *twp, motionwk *mwp, playerwk *pwp);        /* E-102: R4-R6 -> R0 */
Sint32 func_8C0D23E4(taskwk *twp, playerwk *pwp);                       /* E-102: R4,R5 -> R0 */
Uint32 func_8C0D278A(taskwk *twp);                                      /* E-102 ground probe: R4 -> R0 = landtable attribute word */
void func_8C0DAE96(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* Amy free movement (debug): R4 (R5,R6 unused) */
Bool func_8C0DD8DC(Sint8 pno, NJS_OBJECT **objs);                       /* Amy look-at task: R4 spilled MOV.B, R5 -> R0 */
task *func_8C0DEDC0(void);                                              /* -> R0: the created task or NULL */
Bool func_8C0DEFB6(NJS_POINT3 *pos);                                    /* R4 -> R0: create one object at *pos */
Bool func_8C0DF060(void);                                               /* -> R0: create the objects of the current stage (Amy) */
Sint32 func_8C0E632A(task *tp);                                         /* Big: R4 (unused) -> R0: 0 while player 0 is in modes 32-37 or 40-45 */
Float func_8C0E9D24(Float x, Float y, Float z, Sint32 flags, void *out); /* Big: FR4-FR6,R4,R5 -> FR0: ground/ceiling/water probe (floats first, as the matched caller) */

#endif
