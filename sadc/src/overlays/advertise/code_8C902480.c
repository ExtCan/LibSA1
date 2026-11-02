/*
 * ADVERTISE 8C902480-8C9026DA: memory card (VMU) start-up (chunk 8C902480,
 * functions 1, 2 and 5 of 5; 8C9024CC and 8C902518, the VMU voice files,
 * are not executed by the character select).
 *
 *  func_8C902480  backup library init with a 32-byte aligned work area after
 *                 the overlay (D_8C9E4C20 = its end address), then waits for
 *                 the completion callback func_8C9026B8 (D_8C961344)
 *  func_8C9024C6  wrapper of 1ST_READ 8C010B00 (the menu's cleanup)
 *  func_8C9026B8  the completion callback
 */
#include "common.h"
#include "sa1/sa1.h"

extern SA1_UPTR D_8C9E4C20;     /* first free address after the overlay */
extern Uint8 D_8C9610E4[];      /* 0x260 bytes of card state */
/* set by the callback func_8C9026B8 and polled: volatile on PC (SHC reloads
 * it anyway; declaring it volatile for SHC changes the code) */
#if defined(TARGET_PC)
extern volatile Sint32 D_8C961344;
#else
extern Sint32 D_8C961344;
#endif

/* 1ST_READ / SDK */
void func_8C010B38(void);
void func_8C010B00(void);
Uint32 func_8C010E9E(Sint32 port, Sint32 op, Sint32 arg);
Sint32 func_8C010F0E(Sint32 port, Sint32 status, Sint32 cur, Sint32 total);
/* buInit-like (R4-R7); the work area is passed as an address word */
Sint32 func_8C602B68(Sint32 a, Sint32 b, void *work, void (*cb)(void));
void func_8C602BF0(void);
void func_8C602DDA(Uint32 (*cb)(Sint32, Sint32, Sint32));
void func_8C602DE0(Sint32 (*cb)(Sint32, Sint32, Sint32, Sint32));

void func_8C9026B8(void);

void func_8C902480(void)
{
    SA1_UPTR work;

    work = (D_8C9E4C20 + 31) & ~31;
    func_8C010B38();
    memset(D_8C9610E4, 0, 0x260);
    D_8C961344 = 0;
    func_8C602B68(122, 0xFF, (void *)work, func_8C9026B8);
    while (D_8C961344 == 0) {
    }
}

void func_8C9024C6(void)
{
    func_8C010B00();
}

void func_8C9026B8(void)
{
    func_8C602BF0();
    func_8C602DDA(func_8C010E9E);
    func_8C602DE0(func_8C010F0E);
    D_8C961344 = 1;
}
