#include "common.h"

/*
 * 1ST_READ 8C09DAB2-8C09DB2A (chunk 8C09CC80): a small table of 16 named
 * entries (B_8C7A0A8C, 40 bytes each) followed by a 32-bit word B_8C7A0D0C.
 *   8C09DAB2   clear the table (and B_8C7A0D0C)
 *   8C09DAD8   read B_8C7A0D0C
 *   8C09DADE   claim the first free entry: copy a name and store a value
 */

#if defined(TARGET_PC)
#include <string.h>
/* 8C10D3DC is the SHC library strcpy */
#define func_8C10D3DC(dst, src) strcpy((dst), (src))
#else
extern char *func_8C10D3DC(char *dst, const char *src); /* 8C10D3DC strcpy */
#endif

typedef struct NAMED_ENTRY {
    Sint32 used;     /* 0x00: 0 = free */
    char name[32];   /* 0x04: strcpy destination */
    Sint32 value;    /* 0x24 */
} NAMED_ENTRY;       /* 0x28 */
SA1_ASSERT_SIZE(NAMED_ENTRY, 0x28);

#define NAMED_ENTRY_NUM 16

extern NAMED_ENTRY B_8C7A0A8C[NAMED_ENTRY_NUM];
extern Sint32 B_8C7A0D0C;

void func_8C09DAB2(void)
{
    Sint32 i;

    B_8C7A0D0C = 0;
    for (i = 0; i < NAMED_ENTRY_NUM; i++) {
        B_8C7A0A8C[i].used = 0;
    }
}

Sint32 func_8C09DAD8(void)
{
    return B_8C7A0D0C;
}

Sint32 func_8C09DADE(const char *name, Sint32 value)
{
    Sint32 i;
    Sint32 ret;

    ret = 0;
    for (i = 0; i < NAMED_ENTRY_NUM; i++) {
        if (B_8C7A0A8C[i].used == 0) {
            ret = 1;
            B_8C7A0A8C[i].used = 1;
            B_8C7A0A8C[i].value = value;
            func_8C10D3DC(B_8C7A0A8C[i].name, name);
            break;
        }
    }
    return ret;
}
