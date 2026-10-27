/*
 * 1ST_READ 8C05D764-8C05D900 (chunk asm/1st_read/text/8C05D764.src): the
 * pickup handlers of the air item box O_ItemBoxAir (dc-itembox-a, staged
 * for G-SA1 0.9.2; gsa1_090_faults.md F2a).
 *
 * The handlers are reached only through the item table D_8C1B9BA0
 * ({icon texture, handler} per item kind; func_8C05DF18 calls
 * D_8C1B9BA0[kind].func(twp) with the box's taskwk when a player breaks
 * the box). Each one gives its item and plays the pickup sound effects:
 * SE 0x4AD / 0x4AC (E-102's voice, character 6 only) and SE 11.
 *
 *  kind 0  func_8C05D764  speed shoes (to the player that broke the box)
 *  kind 2  func_8C05D7A6  5 rings
 *  kind 3  func_8C05D7D6  10 rings
 *  kind 4  func_8C05D806  random rings (1, 5, 10, 15, 20 or 40: D_8C154C40)
 *  kind 6  func_8C05D854  1-up
 *  kind 1  func_8C05D884  invincibility
 * (kinds 5, 7, 8 are in code_8C05D900.c)
 */
#include "common.h"
#include "task.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"     /* func_8C08A198: add rings */

extern Sint16 D_8C154C40[6];    /* random ring amounts {1, 5, 10, 15, 20, 40} */
extern Sint32 B_8C78C37C;       /* amount of the last random-rings item (copied into the item icon list) */

void func_8C07D3A0(Uint8 pno);  /* speed shoes (code_8C07D3A0.c) */
void func_8C07D4A0(Uint8 pno);  /* invincibility (code_8C07D3A0.c) */
void func_8C08A120(Sint16 n);   /* add lives (code_8C089F20.c) */

/* kind 0: speed shoes for the player whose collision broke the box */
void func_8C05D764(taskwk *twp)
{
    colliwk *hit;

    hit = CWP(twp)->hit_cwp;
    func_8C07D3A0(hit->mytask->twp->counter.b[0]);
    if (GetPlayerNumber() == 6) {
        func_8C0940DA(0x4AD, 0, 0, 0);
    }
    func_8C0940DA(11, 0, 0, 0);
}

/* kind 2: 5 rings */
void func_8C05D7A6(taskwk *twp)
{
    func_8C08A198(5);
    if (GetPlayerNumber() == 6) {
        func_8C0940DA(0x4AC, 0, 0, 0);
    }
    func_8C0940DA(11, 0, 0, 0);
}

/* kind 3: 10 rings */
void func_8C05D7D6(taskwk *twp)
{
    func_8C08A198(10);
    if (GetPlayerNumber() == 6) {
        func_8C0940DA(0x4AC, 0, 0, 0);
    }
    func_8C0940DA(11, 0, 0, 0);
}

/*
 * kind 4: a random amount of rings (rand() * 6 / 32768 -> D_8C154C40).
 * rand() is 0..0x7FFF, so the product is in [0, 6): the plain float -> int
 * conversion cannot leave the range (no SA1_FTOI needed).
 */
void func_8C05D806(taskwk *twp)
{
    B_8C78C37C = D_8C154C40[(Sint32)((Float)rand() * 0.00018310546875f)];
    func_8C08A198(B_8C78C37C);
    if (GetPlayerNumber() == 6) {
        func_8C0940DA(0x4AC, 0, 0, 0);
    }
    func_8C0940DA(11, 0, 0, 0);
}

/* kind 6: 1-up */
void func_8C05D854(taskwk *twp)
{
    func_8C08A120(1);
    if (GetPlayerNumber() == 6) {
        func_8C0940DA(0x4AC, 0, 0, 0);
    }
    func_8C0940DA(11, 0, 0, 0);
}

/* kind 1: invincibility (player 0) */
void func_8C05D884(taskwk *twp)
{
    func_8C07D4A0(0);
    if (GetPlayerNumber() == 6) {
        func_8C0940DA(0x4AC, 0, 0, 0);
    }
    func_8C0940DA(11, 0, 0, 0);
}
