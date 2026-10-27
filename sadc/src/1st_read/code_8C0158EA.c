#include "common.h"
#include "task.h"

void func_8C01593E(task *tp, Sint32 kind);

void func_8C0158EA(task *tp) {
    func_8C01593E(tp, 8);
}

void func_8C015920(task *tp) {
    func_8C01593E(tp, 6);
}

void func_8C015924(task *tp) {
    func_8C01593E(tp, 7);
}

void func_8C015928(task *tp) {
    func_8C01593E(tp, 12);
}

void func_8C01592C(task *tp) {
    func_8C01593E(tp, 11);
}

void func_8C015930(task *tp) {
    func_8C01593E(tp, 10);
}

void func_8C015934(task *tp) {
    func_8C01593E(tp, 9);
}

void func_8C015938(task *tp) {
    func_8C01593E(tp, 14);
}

/* func_8C01593C (MOV #2,R5 falling into func_8C01593E) sits between the
 * last thunk and its target; without it SHC drops the tail BRA. */
ASM_STUB(void func_8C01593C(task *tp))
ASM_STUB(void func_8C01593E(task *tp, Sint32 kind))
