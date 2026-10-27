#include "common.h"

/* Background file-load queue: 16 request slots serviced one at a time
 * through the GD file system (asynchronous reads). */
typedef struct {
    Sint32 stat;        /* 0 free, 1 requested, -1 read issued */
    char fname[32];     /* file name passed to gdFsOpen */
    void *buf;          /* destination buffer */
} FILEREQ;              /* native on PC; DC layout asserted below */
SA1_ASSERT_SIZE(FILEREQ, 0x28);
SA1_ASSERT_OFFSET(FILEREQ, fname, 0x04);
SA1_ASSERT_OFFSET(FILEREQ, buf, 0x24);

#define FILEREQ_NUM 16

extern void *B_8C7A0A88;                 /* GDFS handle of the active request */
extern FILEREQ B_8C7A0A8C[FILEREQ_NUM];  /* request slots */
extern Sint32 B_8C7A0D0C;                /* active slot number (1-based), 0 = none */

/* GD file system: gdFsClose / gdFsGetFileSize / gdFsReqRd32 / gdFsGetStat / gdFsOpen */
extern void func_8C600DF4(void *gdfs);
extern Sint32 func_8C600E2C(void *gdfs, Sint32 *size);
extern Sint32 func_8C601084(void *gdfs, Sint32 nsct, void *buf);
extern Sint32 func_8C6013A4(void *gdfs);
extern void *func_8C6016D6(const char *name, void *dir);

/* Clears all request slots and closes the active file. */
void func_8C09DB80(void) {
    Sint32 i;

    for (i = 0; i < FILEREQ_NUM; i++) {
        B_8C7A0A8C[i].stat = 0;
    }
    if (B_8C7A0D0C != 0) {
        func_8C600DF4(B_8C7A0A88);
    }
    B_8C7A0D0C = 0;
}

/* Services the request queue: while a read is active, waits for it (stat 2),
 * reopens and reissues it on error (stat 5) or closes it when done; with no
 * active read, starts the first requested slot. */
void func_8C09DBBC(void) {
    char unused[32];    /* never referenced; keeps the original 36-byte frame */
    Sint32 size;
    Sint32 stat;
    Sint32 rem;
    Sint32 i;

    if (B_8C7A0D0C != 0) {
        stat = func_8C6013A4(B_8C7A0A88);
        if (stat == 2) {
            return;
        }
        if (stat == 5) {
            func_8C600DF4(B_8C7A0A88);
            B_8C7A0A88 = func_8C6016D6(B_8C7A0A8C[B_8C7A0D0C - 1].fname, NULL);
            if (B_8C7A0A88 == NULL) {
                return;
            }
            func_8C600E2C(B_8C7A0A88, &size);
            if (size % 2048) {
                rem = 1;
            } else {
                rem = 0;
            }
            size = size / 2048 + rem;
            func_8C601084(B_8C7A0A88, size, B_8C7A0A8C[B_8C7A0D0C - 1].buf);
            B_8C7A0A8C[B_8C7A0D0C - 1].stat = -1;
            return;
        } else {
            func_8C600DF4(B_8C7A0A88);
            B_8C7A0A8C[B_8C7A0D0C - 1].stat = 0;
            B_8C7A0D0C = 0;
        }
    }
    for (i = 0; i < FILEREQ_NUM; i++) {
        if (B_8C7A0A8C[i].stat == 1) {
            B_8C7A0D0C = i + 1;
            B_8C7A0A88 = func_8C6016D6(B_8C7A0A8C[B_8C7A0D0C - 1].fname, NULL);
            if (B_8C7A0A88 == NULL) {
                return;
            }
            func_8C600E2C(B_8C7A0A88, &size);
            if (size % 2048) {
                rem = 1;
            } else {
                rem = 0;
            }
            size = size / 2048 + rem;
            func_8C601084(B_8C7A0A88, size, B_8C7A0A8C[B_8C7A0D0C - 1].buf);
            B_8C7A0A8C[B_8C7A0D0C - 1].stat = -1;
            break;
        }
    }
}
