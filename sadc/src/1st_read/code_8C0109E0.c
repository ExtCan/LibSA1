#include "common.h"

/* Request block polled by func_8C6050B0: a command code, its arguments
 * (arg[0] also receives the result) and a busy flag cleared when done.
 * The fields must be addressed as B_8C6AC39C+off and the block must be
 * volatile: each access reloads its own address and ADD #off is used. */
typedef struct {
    Sint32 cmd;     /* 0x00 */
    Sint32 arg[8];  /* 0x04 */
    Sint32 busy;    /* 0x24 */
} UNK_8C6AC39C_t;

extern volatile UNK_8C6AC39C_t B_8C6AC39C;

extern void func_8C6050B0(void);
extern void func_8C608A0E(void *start, void *end);
extern void func_8C6418E8(Sint32 a, Sint32 b);

Sint32 func_8C0109E0(Sint32 a, Sint32 b) {
    Sint32 r;

    B_8C6AC39C.cmd = 1;
    B_8C6AC39C.arg[0] = a;
    B_8C6AC39C.arg[1] = b;
    B_8C6AC39C.busy = 1;
    do {
        func_8C6050B0();
    } while (B_8C6AC39C.busy != 0);
    r = B_8C6AC39C.arg[0];
    return r;
}

void func_8C010A0E(Sint32 a, Sint32 b, Sint32 c) {
    B_8C6AC39C.cmd = 2;
    B_8C6AC39C.arg[0] = a;
    B_8C6AC39C.arg[1] = b;
    B_8C6AC39C.arg[2] = c;
    B_8C6AC39C.busy = 1;
    do {
        func_8C6050B0();
    } while (B_8C6AC39C.busy != 0);
}

void func_8C010A3E(Sint32 a, Sint32 b, Sint32 c) {
    B_8C6AC39C.cmd = 3;
    B_8C6AC39C.arg[0] = a;
    B_8C6AC39C.arg[1] = b;
    B_8C6AC39C.arg[2] = c;
    B_8C6AC39C.busy = 1;
    do {
        func_8C6050B0();
    } while (B_8C6AC39C.busy != 0);
}

void func_8C010A6E(void) {
    void *start;
    void *end;

    start = (void *)0x0CD00000;
    end = (void *)0x0CE00000;
    func_8C608A0E(start, end);
    func_8C6418E8(1, 1);
}

Sint32 func_8C010A84(Sint32 a) {
    Sint32 r;

    B_8C6AC39C.cmd = 4;
    B_8C6AC39C.arg[0] = a;
    B_8C6AC39C.busy = 1;
    do {
        func_8C6050B0();
    } while (B_8C6AC39C.busy != 0);
    r = B_8C6AC39C.arg[0];
    return r;
}
