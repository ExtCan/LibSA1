#include "common.h"

typedef struct {
    Sint8 a;
    Sint8 b;
} UNK_8C06C02A_s;

typedef struct {
    Sint32 pad[8];
    UNK_8C06C02A_s *p;
} UNK_8C06C02A_t;

ASM_STUB(void func_8C06BEA0(UNK_8C06C02A_t *t))

void func_8C06C02A(UNK_8C06C02A_t *t) {
    t->p->b = 1;
    func_8C06BEA0(t);
}
