/*
 * ADVERTISE 8C902700-8C903174: the character select's 3D part (chunk
 * 8C902700, functions 1-16 of 17; func_8C903158 stays assembly). One task (D_8C961348, work ADV_3DWK) owns a
 * camera task and the six real player tasks of the select slots plus the
 * "?" model of slot 6; their mains run every frame, there is no land.
 *
 *  func_8C902700  loads a character's texture archives (reference count
 *                 D_8C1C24B4[chr]; list D_8C942760[chr])
 *  func_8C90275A  releases them
 *  func_8C9027A6  the "?" model (slot 6): spins 0x2D8 per frame, drawn with
 *                 AVA_CHSEL texture list D_8C941508
 *  func_8C902860  parks every slot at (400, 0, -20) facing the camera
 *  func_8C902982  ... then puts the selected slot at its position D_8C961350
 *  func_8C902A20  creates the seven slot tasks (CreateElementalTask(7, 1,
 *                 CharSelData D_8C9101F8[i].exec), counter.b = {slot, chr})
 *  func_8C902D64  idle / pick motions through the event action queue
 *                 (8C04A120, speed 0.25); every slot holds input 12 (Tails
 *                 also 0x31)
 *  func_8C902EA0  destroys the slot tasks, releases the textures
 *  func_8C902FAE  3D init: light set 6, palette bank 2, camera task
 *                 8C07F18C at the origin, the slots
 *  func_8C903020  exec (two frames of parking, then the selected slot)
 *  func_8C9030A6  disp (projection 0x31C7, then the motions)
 *  func_8C9030D6  dest
 *  func_8C9030F0  creates the 3D task
 *  func_8C903132  destroys it
 *  func_8C90314C  sets the "pick" flag (plays the pick sequence)
 */
#include "common.h"
#include "sa1/sa1.h"

/* CharSelData D_8C9101F8, one per select slot */
typedef struct ADV_CHARSEL {
    TaskFuncPtr exec;           /* 0x00 the character's main (8C9027A6 for "?") */
    NJS_TEXLIST *texlist;       /* 0x04 */
    void **actions;             /* 0x08 idle action, then the pick sequence (NULL ends) */
    Sint32 *links;              /* 0x0C per action: second EV_SetAction argument */
} ADV_CHARSEL;                  /* 0x10 */

/* texture archive list entry (D_8C942760[chr], ends with texlist NULL);
 * name NULL = the texlist is loaded from memory (8C608C82) */
typedef struct ADV_PVM {
    const char *name;           /* 0x00 */
    NJS_TEXLIST *texlist;       /* 0x04 */
} ADV_PVM;                      /* 0x08 */

/* 3D task work (task.awp, CAlloc(1, 0x38) by func_8C9030F0) */
typedef struct ADV_3DWK {
    task *tp[7];                /* 0x00 slot tasks */
    task *cam;                  /* 0x1C camera task */
    Uint8 pad20[0x0C];
    Sint8 frames;               /* 0x2C parking frames done */
    Sint8 act[7];               /* 0x2D per slot: next action of the sequence */
    Sint8 pick;                 /* 0x34 play the selected slot's pick sequence */
    Uint8 pad35[3];
} ADV_3DWK;                     /* 0x38 */

extern ADV_PVM *D_8C942760[];   /* texture archives per character */
extern Sint8 D_8C1C24B4[];      /* their reference counts */
extern NJS_TEXLIST D_8C941508;  /* ava_chsel */
extern NJS_MODEL ADVERTISE_Character_Select_Question_Mark;
extern Sint32 D_8C942698;       /* Sonic's ang_aim.y (0x4000) */
extern ADV_CHARSEL D_8C9101F8[];
extern Uint16 D_8C90EC6A[];     /* character id per select slot */
extern NJS_POINT3 D_8C910268[]; /* position of the selected slot */
extern Float D_8C96134C;
extern NJS_POINT3 D_8C961350[]; /* the positions in use */
extern task *D_8C961348;        /* the 3D task */
/* the menu's 18-byte block B_8C78B450..B_8C78B461 (array in the original):
 * [0xD] selected select slot */
extern Uint8 B_8C78B450[18];
extern Uint8 B_8C78B45D;        /* = B_8C78B450[13] (func_8C902D64 reads it by this name) */

/* 1ST_READ */
void func_8C099750(const char *name, NJS_TEXLIST *texlist);     /* load a PVM */
void func_8C04A120(task *tp, void *action, NJS_TEXLIST *texlist, Float speed, Sint32 mode, Sint32 link);
Sint32 func_8C014242(Uint16 n, Sint32 m);
Sint32 func_8C014308(Uint16 n, Sint32 m);
void func_8C038BB8(Float x, Float y, Float z);
void func_8C07F18C(task *tp);                   /* camera task exec */
void func_8C07F21C(void);
void func_8C640500(Sint32 a);                   /* projection angle */
void func_8C036F60(void);

void func_8C902700(Sint32 chr)
{
    ADV_PVM *p;

    p = D_8C942760[chr];
    if (p != NULL) {
        if (D_8C1C24B4[chr] == 0) {
            for (; p->texlist != NULL; p++) {
                if (p->name == NULL) {
                    func_8C608C82(p->texlist);
                } else {
                    func_8C099750(p->name, p->texlist);
                }
            }
        }
        D_8C1C24B4[chr]++;
    }
}

void func_8C90275A(Sint32 chr)
{
    ADV_PVM *p;

    p = D_8C942760[chr];
    if (p != NULL) {
        if (D_8C1C24B4[chr] == 1) {
            for (; p->texlist != NULL; p++) {
                func_8C6088BC(p->texlist);
            }
        }
        if (D_8C1C24B4[chr] > 0) {
            D_8C1C24B4[chr]--;
        }
    }
}

void func_8C9027A6(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    twp->ang.y += 0x2D8;
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
    njSetTexture(&D_8C941508);
    func_8C036F12(&ADVERTISE_Character_Select_Question_Mark);
    njPopMatrix(1);
}

void func_8C902860(ADV_3DWK *w)
{
    Sint32 i;
    task *tp;
    taskwk *twp;

    for (i = 0; i < 7; i++) {
        tp = w->tp[i];
        twp = tp->twp;
        twp->pos.x = 400.0f;
        twp->pos.y = 0.0f;
        twp->pos.z = -20.0f;
        twp->ang.x = 0;
        twp->ang.z = 0;
        if (i != 6) {
            twp->ang.y = 0x4000;
        }
        if (i == 0) {
            tp->mwp->ang_aim.y = D_8C942698;
        }
    }
}

/*
 * NON_MATCHING: the original loads &B_8C78B450 once and reaches [13] with
 * ADD (first access) and @(13,Rn) (the others); SHC makes &B_8C78B450[13]
 * the common address (a pointer local makes it ADD #13 to the base).
 */
#ifdef NON_MATCHING
void func_8C902982(ADV_3DWK *w)
{
    taskwk *twp;

    func_8C902860(w);
    twp = w->tp[B_8C78B450[13]]->twp;
    twp->pos.x = D_8C961350[B_8C78B450[13]].x;
    twp->pos.y = D_8C961350[B_8C78B450[13]].y;
    twp->pos.z = D_8C961350[B_8C78B450[13]].z;
}
#else
ASM_STUB(void func_8C902982(ADV_3DWK *w))
#endif

void func_8C902A20(ADV_3DWK *w)
{
    Sint32 i;
    task *tp;
    taskwk *twp;

    for (i = 0; i < 7; i++) {
        w->tp[i] = NULL;
        w->act[i] = 0;
        D_8C961350[i].x = D_8C910268[i].x;
        D_8C961350[i].y = D_8C910268[i].y;
        D_8C961350[i].z = D_8C910268[i].z;
        if (i != 6) {
            func_8C902700(D_8C90EC6A[i]);
        }
        tp = w->tp[i] = CreateElementalTask(7, 1, D_8C9101F8[i].exec);
        if (tp != NULL) {
            twp = tp->twp;
            twp->ang.y = 0x4000;
            if (i == 5) {
                twp->id = 2;
            }
            tp->twp->counter.b[1] = D_8C90EC6A[i];
            tp->twp->counter.b[0] = i;
        }
    }
    func_8C902860(w);
    func_8C640500(0x31C7);
    func_8C036F60();
}

/*
 * NON_MATCHING: the original loads the 0.25 speed into FR15 inside the slot
 * loop (after the empty "i == 3 || i == 5" test); SHC hoists it out of the
 * loop, which shifts the register allocation (tp R9 / R10). A speed
 * variable set to 0.25 in both branches of that test is propagated the
 * same way. The rest (loop shape, the cross-jumped EV_SetAction calls) is
 * as in the original.
 */
#ifdef NON_MATCHING
void func_8C902D64(ADV_3DWK *w)
{
    Sint32 i;
    task *tp;
    ADV_CHARSEL *cs;
    Sint32 n;

    for (i = 0; i < 7; i++) {
        if ((tp = w->tp[i]) != NULL && i != 6) {
            cs = &D_8C9101F8[i];
            SetInputP(i, 12);
            if (i == 1) {
                SetInputP(i, 0x31);
            }
            if (i == 3 || i == 5) {
                /* empty in the original (the compares remain) */
            }
            if (w->act[i] == 0) {
                func_8C04A120(tp, cs->actions[0], cs->texlist, 0.25f, 1, 0);
                w->act[i] = 1;
            } else if (w->act[i] == 1 && w->pick != 0 && B_8C78B45D == i) {
                while (cs->actions[n = w->act[i]] != NULL) {
                    w->act[i]++;
                    if (cs->actions[w->act[i]] != NULL) {
                        func_8C04A120(tp, cs->actions[n], cs->texlist, 0.25f, 0, cs->links[n]);
                    } else {
                        func_8C04A120(tp, cs->actions[n], cs->texlist, 0.25f, 1, cs->links[n]);
                    }
                }
                w->pick = 0;
            }
        }
    }
}
#else
ASM_STUB(void func_8C902D64(ADV_3DWK *w))
#endif

void func_8C902EA0(ADV_3DWK *w)
{
    Sint32 i;

    for (i = 0; i < 7; i++) {
        if (w->tp[i] != NULL) {
            DestroyTask(w->tp[i]);
        }
        if (i != 6) {
            func_8C90275A(D_8C90EC6A[i]);
        }
    }
}

void func_8C902FAE(task *tp)
{
    ADV_3DWK *w;
    taskwk *twp;

    w = tp->awp;
    func_8C07F21C();
    func_8C014242(6, 0);
    func_8C014308(6, 0);
    func_8C038CEC(2);
    func_8C038BB8(0.57699997f, 0.57699997f, 0.57699997f);
    if ((w->cam = CreateElementalTask(6, 0, func_8C07F18C)) != NULL) {
        twp = w->cam->twp;
        twp->ang.x = twp->ang.y = twp->ang.z = 0;
        twp->pos.x = twp->pos.y = twp->pos.z = 0.0f;
    }
    func_8C902A20(w);
    w->frames = 0;
    w->pick = 0;
}

void func_8C903020(task *tp)
{
    ADV_3DWK *w;

    w = tp->awp;
    if (w->frames < 2) {
        func_8C902860(w);
        w->frames++;
    } else {
        func_8C902982(w);
    }
}

void func_8C9030A6(task *tp)
{
    ADV_3DWK *w;

    w = tp->awp;
    func_8C640500(0x31C7);
    func_8C036F60();
    if (w->frames >= 2) {
        func_8C902D64(w);
    }
}

void func_8C9030D6(task *tp)
{
    ADV_3DWK *w;

    w = tp->awp;
    if (w->cam != NULL) {
        DestroyTask(w->cam);
    }
    func_8C902EA0(w);
}

task *func_8C9030F0(void)
{
    task *tp;
    ADV_3DWK *w;

    if ((D_8C961348 = tp = CreateElementalTask(0, 3, func_8C903020)) != NULL) {
        w = CAlloc(1, sizeof(ADV_3DWK));
        tp->awp = w;
        func_8C902FAE(tp);
        tp->exec = func_8C903020;
        tp->disp = func_8C9030A6;
        tp->dest = func_8C9030D6;
        D_8C96134C = 0.0f;
    }
    return tp;
}

void func_8C903132(void)
{
    if (D_8C961348 != NULL) {
        DestroyTask(D_8C961348);
    }
    D_8C961348 = NULL;
}

void func_8C90314C(Sint8 pick)
{
    ((ADV_3DWK *)D_8C961348->awp)->pick = pick;
}

