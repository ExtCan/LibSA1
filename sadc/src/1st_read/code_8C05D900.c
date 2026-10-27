/*
 * 1ST_READ 8C05D900-8C05D9C0 (chunk asm/1st_read/text/8C05D900.src): the
 * pickup handlers of the air item box O_ItemBoxAir for the item kinds 5, 7
 * and 8 (dc-itembox-a, staged for G-SA1 0.9.2; gsa1_090_faults.md F2a).
 * Reached only through the item table D_8C1B9BA0 (see code_8C05D764.c).
 *
 *  kind 5  func_8C05D900  barrier (player 0)
 *  kind 7  func_8C05D930  bomb: no item; the bomb sphere starts at the box
 *                         (func_8C05E582, tail call), and every enemy it
 *                         reaches is destroyed (func_8C05E5C2)
 *  kind 8  func_8C05D964  magnetic barrier (player 0)
 */
#include "common.h"
#include "task.h"
#include "sa1/sa1.h"

void func_8C07D3DC(Uint8 pno);          /* magnetic barrier (code_8C07D3A0.c) */
void func_8C07D43E(Uint8 pno);          /* barrier (code_8C07D3A0.c) */
void func_8C05E582(NJS_POINT3 *pos);    /* start the bomb sphere at pos */

/* kind 5: barrier */
void func_8C05D900(taskwk *twp)
{
    func_8C07D43E(0);
    if (GetPlayerNumber() == 6) {
        func_8C0940DA(0x4AC, 0, 0, 0);
    }
    func_8C0940DA(11, 0, 0, 0);
}

/* kind 7: bomb */
void func_8C05D930(taskwk *twp)
{
    if (GetPlayerNumber() == 6) {
        func_8C0940DA(0x4AC, 0, 0, 0);
    }
    func_8C0940DA(11, 0, 0, 0);
    func_8C05E582(&twp->pos);
}

/* kind 8: magnetic barrier */
void func_8C05D964(taskwk *twp)
{
    func_8C07D3DC(0);
    if (GetPlayerNumber() == 6) {
        func_8C0940DA(0x4AC, 0, 0, 0);
    }
    func_8C0940DA(11, 0, 0, 0);
}
