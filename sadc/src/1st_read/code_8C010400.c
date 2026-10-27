#include "common.h"
#include "ninja.h"
#include "task.h"

/* Saved position/rotation of an object */
typedef struct {
    NJS_POINT3 pos;
    Angle3 ang;
} SAVEPOS_t;

/* Save/restore request (B_8C6AC374..B_8C6AC39C) */
typedef struct {
    Sint32 mode;            /* 0x374 */
    Sint32 work[8];         /* 0x378 */
    Sint32 req;             /* 0x398 */
} SAVEREQ;

/* Save/restore work area (B_8C6AC39C..B_8C6AC5AC) */
typedef struct {
    Sint32 cmd;             /* 0x39C */
    Sint32 arg[8];          /* 0x3A0 */
    Sint32 busy;            /* 0x3C0 */
    SAVEPOS_t cam;          /* 0x3C4 */
    SAVEPOS_t pl[8];        /* 0x3DC */
    Sint32 unk49C;          /* 0x49C */
    Sint32 unk4A0;          /* 0x4A0 */
    Sint32 unk4A4;          /* 0x4A4 */
    SAVEPOS_t cam2;         /* 0x4A8 */
    SAVEPOS_t pl2[8];       /* 0x4C0 */
    Sint32 unk580;          /* 0x580 */
    Sint32 unk584;          /* 0x584 */
    Sint32 unk588;          /* 0x588 */
    Sint16 evt_unk0;        /* 0x58C */
    Sint16 evt_angx;        /* 0x58E */
    Sint16 evt_angy;        /* 0x590 */
    Sint16 evt_angz;        /* 0x592 */
    NJS_POINT3 evt_pos;     /* 0x594 */
    NJS_POINT3 evt_scl;     /* 0x5A0 */
} SAVEWK;

extern taskwk *D_8C18B7EC;
extern taskwk *B_8C749130;
extern Sint16 D_8C19B468;
extern Uint16 D_8C19B488;

extern volatile SAVEREQ B_8C6AC374;
extern volatile SAVEWK B_8C6AC39C;

extern task *func_8C07CFA0(Uint8 pno); /* GetPlayerTaskPointer (sa1/player.h) */

void func_8C010A6E(void);

#ifdef NON_MATCHING
void func_8C010400(void)
{
    Sint32 i;
    task *tp;
    taskwk *twp;

    B_8C6AC39C.cam.pos.x = D_8C18B7EC->pos.x;
    B_8C6AC39C.cam.pos.y = D_8C18B7EC->pos.y;
    B_8C6AC39C.cam.pos.z = D_8C18B7EC->pos.z;
    B_8C6AC39C.cam.ang.x = D_8C18B7EC->ang.x;
    B_8C6AC39C.cam.ang.y = D_8C18B7EC->ang.y;
    B_8C6AC39C.cam.ang.z = D_8C18B7EC->ang.z;
    for (i = 0; i < 8; i++) {
        tp = func_8C07CFA0(i);
        if (tp) {
            twp = tp->twp;
            B_8C6AC39C.pl[i].pos.x = twp->pos.x;
            B_8C6AC39C.pl[i].pos.y = twp->pos.y;
            B_8C6AC39C.pl[i].pos.z = twp->pos.z;
            B_8C6AC39C.pl[i].ang.x = twp->ang.x;
            B_8C6AC39C.pl[i].ang.y = twp->ang.y;
            B_8C6AC39C.pl[i].ang.z = twp->ang.z;
        } else {
            B_8C6AC39C.pl[i].pos.x = 0.0f;
            B_8C6AC39C.pl[i].pos.y = 0.0f;
            B_8C6AC39C.pl[i].pos.z = 0.0f;
            B_8C6AC39C.pl[i].ang.x = 0;
            B_8C6AC39C.pl[i].ang.y = 0;
            B_8C6AC39C.pl[i].ang.z = 0;
        }
    }
}
#else
ASM_STUB(void func_8C010400(void))
#endif

#ifdef NON_MATCHING
void func_8C010640(void)
{
    Sint32 i;
    task *tp;
    taskwk *twp;

    D_8C18B7EC->pos.x = B_8C6AC39C.cam2.pos.x;
    D_8C18B7EC->pos.y = B_8C6AC39C.cam2.pos.y;
    D_8C18B7EC->pos.z = B_8C6AC39C.cam2.pos.z;
    D_8C18B7EC->ang.x = B_8C6AC39C.cam2.ang.x;
    D_8C18B7EC->ang.y = B_8C6AC39C.cam2.ang.y;
    D_8C18B7EC->ang.z = B_8C6AC39C.cam2.ang.z;
    for (i = 0; i < 8; i++) {
        tp = func_8C07CFA0(i);
        if (tp) {
            twp = tp->twp;
            twp->pos.x = B_8C6AC39C.pl2[i].pos.x;
            twp->pos.y = B_8C6AC39C.pl2[i].pos.y;
            twp->pos.z = B_8C6AC39C.pl2[i].pos.z;
            twp->ang.x = B_8C6AC39C.pl2[i].ang.x;
            twp->ang.y = B_8C6AC39C.pl2[i].ang.y;
            twp->ang.z = B_8C6AC39C.pl2[i].ang.z;
        }
    }
}
#else
ASM_STUB(void func_8C010640(void))
#endif

void func_8C0107A6(void)
{
    if (D_8C19B468 == 1) {
        B_8C6AC39C.evt_unk0 = D_8C19B488;
        B_8C6AC39C.evt_angx = B_8C749130->ang.x;
        B_8C6AC39C.evt_angy = B_8C749130->ang.y;
        B_8C6AC39C.evt_angz = B_8C749130->ang.z;
        B_8C6AC39C.evt_pos.x = B_8C749130->pos.x;
        B_8C6AC39C.evt_pos.y = B_8C749130->pos.y;
        B_8C6AC39C.evt_pos.z = B_8C749130->pos.z;
        B_8C6AC39C.evt_scl.x = B_8C749130->scl.x;
        B_8C6AC39C.evt_scl.y = B_8C749130->scl.y;
        B_8C6AC39C.evt_scl.z = B_8C749130->scl.z;
    }
}

void func_8C010810(void)
{
    if (D_8C19B468 == 1) {
        D_8C19B488 = B_8C6AC39C.evt_unk0;
        B_8C749130->ang.x = B_8C6AC39C.evt_angx;
        B_8C749130->ang.y = B_8C6AC39C.evt_angy;
        B_8C749130->ang.z = B_8C6AC39C.evt_angz;
        B_8C749130->pos.x = B_8C6AC39C.evt_pos.x;
        B_8C749130->pos.y = B_8C6AC39C.evt_pos.y;
        B_8C749130->pos.z = B_8C6AC39C.evt_pos.z;
        B_8C749130->scl.x = B_8C6AC39C.evt_scl.x;
        B_8C749130->scl.y = B_8C6AC39C.evt_scl.y;
        B_8C749130->scl.z = B_8C6AC39C.evt_scl.z;
    }
}

void func_8C0108E0(task *tp)
{
    func_8C010400();
}

void func_8C0108E4(task *tp)
{
    tp->exec = func_8C0108E0;
}

void func_8C0108EA(void)
{
    if (B_8C6AC374.req) {
        switch (B_8C6AC374.mode) {
        case 0:
            break;
        case 1:
            func_8C010A6E();
            break;
        case 2:
            func_8C010640();
            break;
        case 3:
            func_8C0107A6();
            break;
        case 4:
            func_8C010810();
            break;
        }
        B_8C6AC374.req = 0;
    }
}

void func_8C010938(void)
{
    Sint32 i;

    B_8C6AC374.mode = 0;
    B_8C6AC39C.cmd = 0;
    for (i = 0; i < 8; i++) {
        B_8C6AC374.work[i] = 0;
        B_8C6AC39C.arg[i] = 0;
    }
    B_8C6AC374.req = 0;
    B_8C6AC39C.busy = 0;
    B_8C6AC39C.unk49C = 0xC3A000;
    B_8C6AC39C.unk4A0 = 0;
    B_8C6AC39C.unk4A4 = 0;
    B_8C6AC39C.unk580 = 0xC3A000;
    B_8C6AC39C.unk584 = 0;
    B_8C6AC39C.unk588 = 0;
}

ASM_STUB(void func_8C010A6E(void))
