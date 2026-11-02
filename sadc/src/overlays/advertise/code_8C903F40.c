/*
 * ADVERTISE 8C903F40: opens dialog n (descriptor D_8C910568[n], 60 bytes;
 * 1ST_READ 8C0A682A); chunk 8C903F40, function 1 of 5 (8C903F54-8C903FC8,
 * the other dialog wrappers, are not executed by the character select).
 * Dialogs 6 / 7: Adventure pick (story not started / started), 8 / 9: the
 * same for Super Sonic.
 */
#include "common.h"
#include "sa1/sa1.h"

typedef struct ADV_DIALOG {
    Uint8 data[60];
} ADV_DIALOG;                   /* 0x3C */

extern ADV_DIALOG D_8C910568[];

void func_8C0A682A(ADV_DIALOG *d);

void func_8C903F40(Sint32 n)
{
    func_8C0A682A(&D_8C910568[n]);
}
