/*
 * ADVERTISE 8C901280-8C901368: leaving the menu for a game (chunk 8C901280,
 * all 5 functions).
 *
 *  func_8C901280  sets up the chosen character and stage: B_8C788C10 from
 *                 func_8C901AB4, D_8C1C001C = 1 for Super Sonic (slot 6), the
 *                 character id D_8C90EC6A[slot] (8C04F692), the stage / act
 *                 D_8C9610C8 (in Trial mode with B_8C78B450[1] set, four
 *                 stages are replaced: Sonic 22 -> 15, Tails 21 -> 20,
 *                 Knuckles 18 -> 16, E-102 25 -> 24, act 0), then 8C099080
 *  func_8C901348, func_8C901356, func_8C90135C, func_8C901362: wrappers of
 *                 1ST_READ functions (not executed by the character select)
 */
#include "common.h"
#include "sa1/sa1.h"

typedef struct ADV_STAGEACT {
    Uint8 stage;                /* 0x00 */
    Uint8 act;                  /* 0x01 */
} ADV_STAGEACT;                 /* 0x02 */

/* the menu's 18-byte block B_8C78B450..B_8C78B461 (array in the original):
 * [1] B_8C78B451, [0xD] selected select slot */
extern Uint8 B_8C78B450[18];
extern Uint8 B_8C788C10;
extern Sint32 D_8C1C001C;       /* 1 = Super Sonic chosen */
extern Uint16 D_8C90EC6A[];     /* character id per select slot */
extern ADV_STAGEACT D_8C9610C8; /* stage / act to start */

/* 1ST_READ */
void func_8C04F692(Sint32 chr);                 /* set the character */
void func_8C04F76C(Sint32 stage, Sint32 act);     /* set the stage and act */
void func_8C099080(void);
void func_8C08FD80(void);
void func_8C088280(void);
Sint32 func_8C01140A(void);
Sint32 func_8C0114F0(void);
Sint32 func_8C011606(void);

/* ADVERTISE, other chunk */
Sint8 func_8C901AB4(void);

void func_8C901280(void)
{
    ADV_STAGEACT st;

    B_8C788C10 = func_8C901AB4() == 0 ? 1 : 0;
    D_8C1C001C = 0;
    if (B_8C78B450[13] == 6) {
        D_8C1C001C = 1;
    }
    func_8C04F692(D_8C90EC6A[B_8C78B450[13]]);
    st = D_8C9610C8;
    if (B_8C78B450[1] != 0) {
        switch (B_8C78B450[13]) {
        case 0:
            if (st.stage == 22) {
                st.stage = 15;
                st.act = 0;
            }
            break;
        case 1:
            if (st.stage == 21) {
                st.stage = 20;
                st.act = 0;
            }
            break;
        case 2:
            if (st.stage == 18) {
                st.stage = 16;
                st.act = 0;
            }
            break;
        case 5:
            if (st.stage == 25) {
                st.stage = 24;
                st.act = 0;
            }
            break;
        }
    }
    func_8C04F76C(st.stage, st.act);
    func_8C099080();
}

void func_8C901348(void)
{
    func_8C08FD80();
    func_8C088280();
}

Sint32 func_8C901356(void)
{
    return func_8C01140A();
}

Sint32 func_8C90135C(void)
{
    return func_8C0114F0();
}

Sint32 func_8C901362(void)
{
    return func_8C011606();
}
